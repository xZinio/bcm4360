// SPDX-License-Identifier: ISC
/*
 * The lower layers that Broadcom's si_attach and its helpers did: the
 * enumeration of the cores, the backplane handle, the PCIe bridge, the OTP,
 * ChipCommon clock setup. Written from docs/re/spec/chip-attach.md (A1-A6),
 * chip.md ("PCIe bridge", "Attach and detach"), srom-otp.md (OTP, SROM
 * helpers) and pmu.md (si_clkctl_init, si_clkctl_fast_pwrup_delay).
 *
 * The functions the specifications do not describe yet (chip-attach.md B
 * and D are still being written) are marked `TODO chip-attach` and listed
 * in docs/re/questions/core-env.md.
 */
#include "core.h"
#include <bcm4360/nvram.h>
#include <bcm4360/srom.h>
#include <bcm4360/utils.h>

#define PCICFG_0x08		0x08
#define PCICFG_BAR0_WIN		0x80
#define PCICFG_0x88		0x88
#define PCICFG_PCIE_INTMASK	0x94
#define PCICFG_SUBSYS		0x2c
#define PCI_CAP_PCIE		0x10
#define PCI_CAP_PM		0x01

#define CHIPID_ALL_ONES		0xffffffffU
#define EROM_LIMIT		0xe00
#define EROM_END		0x0000000fU
#define ASD_SLAVE		0
#define ASD_BRIDGE		1
#define ASD_SLAVE_WRAP		2
#define ASD_MASTER_WRAP		3
#define CORE_ID_PCIE_GEN2	0x83c
#define CORE_ID_PCIE		0x820
#define CORE_ID_PCI		0x804
#define CORE_ID_CC		0x800
#define CORE_ID_D11		0x812
#define CORE_ID_OOB		0x367

#define VENDOR_APPLE		0x106b

/* --- variables --- */

static const char *nvram_lookup(const char *name)
{
	return bcm4360_nvram_get(name);
}

/* docs/re/spec/utils.md: getvar with the global NVRAM as the fallback */
const char *core_getvar(const char *vars, const char *name)
{
	return bcm4360_getvar(vars, name, nvram_lookup);
}

s32 core_getintvar(const char *vars, const char *name)
{
	return bcm4360_getintvar(vars, name, nvram_lookup);
}

/* --- configuration space --- */

static u32 cfg_read(struct bcm4360_core *c, u32 off)
{
	return hw_pcicfg_read(c->hw, off, 4);
}

static void cfg_write(struct bcm4360_core *c, u32 off, u32 v)
{
	hw_pcicfg_write(c->hw, off, 4, v);
}

/* chip-attach.md B2 / chip.md: pcicore_find_pci_capability(osh, capid) */
static u32 find_capability(struct bcm4360_core *c, u32 capid)
{
	u32 v, ptr, n;

	v = cfg_read(c, 0x0c);
	if ((v >> 16) & 0x7f)
		return 0;
	v = cfg_read(c, 0x04);
	if (!(v & BIT(20)))
		return 0;
	ptr = cfg_read(c, 0x34) & 0xff;
	for (n = 0; ptr && n < 48; n++) {
		u32 id = (cfg_read(c, ptr & ~3U) >> (8 * (ptr & 3))) & 0xff;

		if (id == capid)
			return ptr;
		ptr = (cfg_read(c, (ptr + 1) & ~3U) >> (8 * ((ptr + 1) & 3))) & 0xff;
	}
	return 0;
}

/* --- the public part of the handle and the views of the layers --- */

/* copy what the open layers read of the handle into their views */
void core_si_sync(struct bcm4360_core *c)
{
	struct core_sihpub *p = &c->sihp;
	struct bcm4360_si *si = &c->si;
	struct bcm4360_chip_info *ci = &c->pmu.info;

	si->hw = c->hw;
	si->bustype = p->bustype;
	si->buscoreid = p->buscoretype;
	si->buscorerev = p->buscorerev;
	si->buscoreidx = p->buscoreidx;
	si->ccrev = p->ccrev;
	si->cccaps = p->cccaps;
	si->chip = p->chip;
	si->chiprev = p->chiprev;
	si->chippkg = p->chippkg;
	si->chipst = p->chipst;
	si->curidx = &c->curidx;
	si->coreid = c->coreid;
	si->coreregbase = c->coreregbase;
	si->wrapbase = c->wrapbase;
	si->cia = c->cia;
	si->cib = c->cib;

	c->pmu.hw = c->hw;
	ci->chip = p->chip;
	ci->chiprev = p->chiprev;
	ci->chippkg = p->chippkg;
	ci->chipst = p->chipst;
	ci->ccrev = p->ccrev;
	ci->cccaps = p->cccaps;
	ci->cccaps_ext = p->cccaps_ext;
	ci->pmurev = p->pmurev;
	ci->pmucaps = p->pmucaps;
}

/* --- the enumeration ROM (chip-attach.md A4) --- */

static u32 erom_read(struct bcm4360_core *c, u32 pos)
{
	return hw_read32(c->hw->d11, pos);
}

static u32 erom_ent(struct bcm4360_core *c, u32 *pos, u32 mask, u32 match)
{
	for (;;) {
		u32 w = erom_read(c, *pos);

		*pos += 4;
		if (!mask)
			return w;
		if (!(w & 1))
			continue;
		if (w == EROM_END)
			return w;
		if ((w & mask) == match)
			return w;
	}
}

struct asd {
	u32 addr_lo, addr_hi, size_lo, size_hi;
};

static u32 erom_asd(struct bcm4360_core *c, u32 *pos, u32 port, u32 type, struct asd *a)
{
	u32 w = erom_ent(c, pos, 1, 1);

	if ((w & 6) != 4 || ((w >> 8) & 0xf) != port || ((w >> 6) & 3) != type) {
		*pos -= 4;
		return 0;
	}
	a->addr_lo = w & 0xfffff000;
	a->addr_hi = (w & 8) ? erom_ent(c, pos, 0, 0) : 0;
	a->size_hi = 0;
	if (((w >> 4) & 3) == 3) {
		u32 s = erom_ent(c, pos, 0, 0);

		a->size_lo = s & 0xfffff000;
		if (s & 8)
			a->size_hi = erom_ent(c, pos, 0, 0);
	} else {
		a->size_lo = 0x1000U << ((w >> 4) & 3);
	}
	return w;
}

/* a wrapper descriptor must exist, with a size of 4 KB */
static bool erom_wrapper(struct bcm4360_core *c, u32 *pos, u32 port, u32 type, struct asd *a)
{
	return erom_asd(c, pos, port, type, a) && a->size_lo == 0x1000 && a->size_hi == 0;
}

/* chip-attach.md A4, ai_scan; fills the tables and si.numcores */
static void ai_scan(struct bcm4360_core *c)
{
	struct bcm4360_si *si = &c->si;
	u32 pos = 0, erom;

	si->numcores = 0;
	erom = hw_read32(c->hw->d11, CC_EROMPTR);
	cfg_write(c, PCICFG_BAR0_WIN, erom);

	while (pos < EROM_LIMIT) {
		u32 cia, cib, id, vendor, nmp, nsp, nmw, nsw, i, k, port;
		struct asd a;
		bool bridge = false;

		cia = erom_ent(c, &pos, 0xe, 0);
		if (cia == EROM_END)
			return;
		cib = erom_ent(c, &pos, 0, 0);
		if (cib & 0xe)
			goto fail;
		id = (cia >> 8) & 0xfff;
		vendor = cia >> 20;
		nmp = (cib >> 4) & 0x1f;
		nsp = (cib >> 9) & 0x1f;
		nmw = (cib >> 14) & 0x1f;
		nsw = (cib >> 19) & 0x1f;
		if ((id == 0xfff && vendor == 0x43b) || nsp == 0)
			continue;
		if (nmw == 0 && nsw == 0) {
			if (id == CORE_ID_OOB) {
				if (erom_asd(c, &pos, 0, ASD_SLAVE, &a))
					c->oob_router = a.addr_lo;
				continue;
			}
			if (id != 0x50b && id != 0x5dc)
				continue;
		}
		i = si->numcores;
		if (i >= CORE_MAXCORES)
			goto fail;
		c->cib[i] = cib;
		c->cia[i] = cia;
		c->coreid[i] = id;
		for (k = 0; k < nmp; k++) {
			u32 w = erom_ent(c, &pos, 1, 1);

			if ((w & 0xe) != 2)
				goto fail;
		}
		if (!erom_asd(c, &pos, 0, ASD_SLAVE, &a)) {
			bridge = true;
			while (erom_asd(c, &pos, 0, ASD_BRIDGE, &a))
				;
		}
		c->coreregbase[i] = a.addr_lo;
		/* further slave regions of port 0: none is used */
		while (erom_asd(c, &pos, 0, ASD_SLAVE, &a))
			;
		for (port = 1; port < nsp; port++) {
			if (!erom_asd(c, &pos, port, ASD_SLAVE, &a))
				goto fail;
			while (erom_asd(c, &pos, port, ASD_SLAVE, &a))
				;
		}
		for (k = 0; k < nmw; k++) {
			if (!erom_wrapper(c, &pos, k, ASD_MASTER_WRAP, &a))
				goto fail;
			if (k == 0)
				c->wrapbase[i] = a.addr_lo;
		}
		for (k = 0; k < nsw; k++) {
			if (!erom_wrapper(c, &pos, k + (nsp != 1), ASD_SLAVE_WRAP, &a))
				goto fail;
			if (k == 0 && nmw == 0)
				c->wrapbase[i] = a.addr_lo;
		}
		if (!bridge)
			si->numcores++;
	}
fail:
	si->numcores = 0;
}

/* --- the PCIe bridge (chip.md "PCIe bridge") --- */

static void pcie_init(struct bcm4360_core *c)
{
	struct core_pcie *p = &c->pcie;
	u32 cap;

	hw_memset(p, 0, sizeof(*p));
	p->valid = true;
	if (c->sihp.buscoretype == CORE_ID_PCIE_GEN2 || c->sihp.buscoretype == CORE_ID_PCIE) {
		cap = find_capability(c, PCI_CAP_PCIE);
		if (cap) {
			p->devctl = cap + 8;
			p->linkctl = cap + 0x10;
			p->devctl2 = cap + 0x28;
		}
	}
}

static bool bus_is_pcie(struct bcm4360_core *c)
{
	return c->sihp.buscoretype == CORE_ID_PCIE_GEN2 || c->sihp.buscoretype == CORE_ID_PCIE;
}

/* chip.md pcicore_attach: on this card only the request size and the C0 work-around */
static void pcie_attach(struct bcm4360_core *c)
{
	struct core_pcie *p = &c->pcie;
	u32 chip = c->sihp.chip;

	/* Apple boards 0x129 and 0x135 program a SerDes register first (TODO chip-attach) */
	if (chip == 0xa9c4 || chip == 0x4360 || chip == 0x4350 || chip == 0x4352)
		p->mrrs = 3;
	if (chip == 0x4360 && c->sihp.chiprev >= 4) {
		u32 v = hw_read32(c->hw->pcie, 0);

		hw_write32(c->hw->pcie, 0, (v & ~0x300U) | 0x200);
		(void)hw_read32(c->hw->pcie, 0);
	}
}

/* chip.md si_pci_fixcfg: the PCIe core's SROM shadow word 0 names the bus core's index */
s32 core_si_pci_fixcfg(struct bcm4360_core *c)
{
	struct bcm4360_si *si = &c->si;
	u32 orig = c->curidx;
	u32 bt = c->sihp.buscoretype;
	void *p;
	u16 v;

	if (bt != CORE_ID_PCIE_GEN2 && bt != CORE_ID_PCIE && bt != CORE_ID_PCI)
		return -1;
	p = bcm4360_si_setcore(si, bt, 0);
	if (!p)
		return -1;
	v = hw_read16(p, 0x800);
	if ((v >> 12) != (c->curidx & 0xf))
		hw_write16(p, 0x800, (v & 0xfff) | ((c->curidx & 0xf) << 12));
	bcm4360_si_setcoreidx(si, orig);
	/* pcicore_hwup: nothing for board 0x117 (TODO chip-attach: boards 0x129, 0x135) */
	return 0;
}

void core_si_pci_setup(struct bcm4360_core *c, u32 coremask)
{
	u32 old;

	if (c->sihp.bustype != 1 || !bus_is_pcie(c))
		return;
	old = cfg_read(c, PCICFG_PCIE_INTMASK);
	cfg_write(c, PCICFG_PCIE_INTMASK, old | (coremask << 8));
}

void core_si_pci_up(struct bcm4360_core *c)
{
	struct core_pcie *p = &c->pcie;

	if (c->sihp.bustype != 1 || !p->valid || !bus_is_pcie(c))
		return;
	if (c->sihp.buscoretype == CORE_ID_PCIE_GEN2) {
		if (p->devctl) {
			u32 v = cfg_read(c, p->devctl);

			cfg_write(c, p->devctl, (v & 0xffff8fff) | ((p->mrrs & 7U) << 12));
			(void)cfg_read(c, p->devctl);
		}
		if (c->sihp.chip == 0x4360 && c->sihp.chiprev > 3) {
			u32 v = hw_read32(c->hw->pcie, 0);

			hw_write32(c->hw->pcie, 0, (v & ~0x300U) | 0x200);
			(void)hw_read32(c->hw->pcie, 0);
		}
	}
}

void core_si_pci_down(struct bcm4360_core *c)
{
	(void)c;	/* pcicore_down acts for bus core 0x820 only */
}

void core_si_pci_sleep(struct bcm4360_core *c)
{
	(void)c;	/* clears do_4360_pcie2_war (kept by the MAC layer); pcicore_sleep: 0x820 only */
}

u32 core_si_pcieltrenable(struct bcm4360_core *c, u32 mask, u32 val)
{
	struct core_pcie *p = &c->pcie;
	u32 v;

	if (c->sihp.bustype != 1 || !bus_is_pcie(c) || !p->devctl2)
		return 0;
	v = cfg_read(c, p->devctl2);
	if (mask) {
		cfg_write(c, p->devctl2, val ? (v | BIT(10)) : (v & ~BIT(10)));
		(void)cfg_read(c, p->devctl2);
	}
	return (v >> 10) & 1;
}

u32 core_si_pcieobffenable(struct bcm4360_core *c, u32 mask, u32 val)
{
	struct core_pcie *p = &c->pcie;
	u32 v;

	if (c->sihp.bustype != 1 || !bus_is_pcie(c) || !p->devctl2)
		return 0;
	v = cfg_read(c, p->devctl2);
	if (mask) {
		cfg_write(c, p->devctl2, (v & ~0x6000U) | ((val & 3) << 13));
		(void)cfg_read(c, p->devctl2);
	}
	return (v >> 13) & 3;
}

void core_si_pcie_ltr_war(struct bcm4360_core *c)
{
	struct bcm4360_si *si = &c->si;
	u32 on, orig;

	if (c->sihp.bustype != 1 || c->sihp.buscoretype != CORE_ID_PCIE_GEN2 ||
	    c->sihp.buscorerev != 1)
		return;
	on = core_si_pcieltrenable(c, 0, 0);
	orig = c->curidx;
	bcm4360_si_setcore(si, CORE_ID_D11, 0);
	bcm4360_si_wrapperreg(si, 0x160, 0xffffffff, on ? 0x02838280 : 0x02848180);
	bcm4360_si_wrapperreg(si, 0x164, 0xffffffff, 3);
	bcm4360_si_setcoreidx(si, orig);
}

/* --- ChipCommon: clocks (pmu.md) --- */

void core_si_clkctl_init(struct bcm4360_core *c)
{
	void *cc = c->hw->cc;
	u32 v, div, f;

	if (!(c->sihp.cccaps & BIT(18)))
		return;
	if (c->sihp.ccrev >= 10) {
		v = hw_read32(cc, 0xc0);
		hw_write32(cc, 0xc0, (v & 0xffff) | 0x40000);
	}
	div = 4 * ((hw_read32(cc, 0xc0) >> 16) + 1);
	f = 19800000 / div;
	hw_write32(cc, 0xb0, (f * 150 + 999999) / 1000000);
	hw_write32(cc, 0xb4, (f * 200 + 999999) / 1000000);
	hw_udelay(20000);
}

/* pmu.md si_pmu_fast_pwrup_delay (no register access) */
u16 core_si_clkctl_fast_pwrup_delay(struct bcm4360_core *c)
{
	return (u16)bcm4360_pmu_fast_pwrup_delay(&c->pmu);
}

/* chip-attach.md D1: si_pmu_res_req_timer_clr */
void core_si_pmu_res_req_timer_clr(struct bcm4360_core *c)
{
	bcm4360_si_corereg(&c->si, 0, 0x644, c->sihp.chip == 0x4328 ? 0xc00 : 0x03000000, 0);
	bcm4360_si_corereg(&c->si, 0, 0x644, 0, 0);
}

/* --- SROM and OTP helpers (srom-otp.md) --- */

/* srom-otp.md si_is_sprom_available */
bool core_si_is_sprom_available(struct bcm4360_core *c)
{
	struct bcm4360_si *si = &c->si;
	u32 orig, v;
	void *cc;

	if (c->sihp.ccrev < 31)
		return false;	/* TODO chip-attach: older ChipCommon revisions */
	if (!(c->sihp.cccaps & BIT(30)))
		return false;
	orig = c->curidx;
	cc = bcm4360_si_setcoreidx(si, 0);
	v = hw_read32(cc, CC_SROM_CTL);
	bcm4360_si_setcoreidx(si, orig);
	return v & 1;
}

static bool otp_powered(struct bcm4360_core *c)
{
	if (!(c->sihp.cccaps & BIT(28)))
		return true;
	return bcm4360_pmu_is_otp_powered(&c->pmu);
}

static void otp_power(struct bcm4360_core *c, bool on)
{
	if (c->sihp.cccaps & BIT(28))
		bcm4360_pmu_otp_power(&c->pmu, on);
	hw_udelay(1000);
}

struct otpinfo {
	u16 wsize, cols;
	u32 status;
	s32 otpgu_base;
};

/* srom-otp.md ipxotp_read_bit: one bit, -1 on error */
static s32 otp_read_bit(struct bcm4360_core *c, void *cc, struct otpinfo *oi, u32 off)
{
	u32 row = off / oi->cols, col = off % oi->cols, n, v = 0;

	hw_write32(cc, CC_OTP_PROG, 0);
	hw_write32(cc, 0xf4, 0);
	(void)hw_read32(cc, CC_OTP_LAYOUT);
	hw_write32(cc, CC_OTP_CMD, 0x80000000 | ((row << 8) & 0xffff) | (col & 0xff));
	for (n = 0; n < 10000000; n++) {
		v = hw_read32(cc, CC_OTP_CMD);
		if (!(v & 0x80000000))
			break;
	}
	if (n == 10000000 || (v & BIT(28)))
		return -1;
	return (v >> 29) & 1;
}

/* srom-otp.md ipxotp_otpr: one word */
static u16 otp_word(struct bcm4360_core *c, void *cc, struct otpinfo *oi, u32 wn)
{
	u32 b;
	u16 word = 0;

	if (core_si_is_sprom_available(c)) {
		for (b = 0; b < 16; b++) {
			s32 bit = otp_read_bit(c, cc, oi, 16 * wn + b);

			if (bit < 0)
				return 0xffff;
			word |= (u16)(bit << b);
		}
		return word;
	}
	return hw_read16(cc, 0x800 + 2 * wn);
}

/* srom-otp.md ipxotp_init, the parts that decide the word reads (chip 0x4360) */
static bool otp_init(struct bcm4360_core *c, struct otpinfo *oi)
{
	struct bcm4360_si *si = &c->si;
	u32 orig, z, l, rows = 0, cols = 0, n;
	void *cc;

	hw_memset(oi, 0, sizeof(*oi));
	if ((c->sihp.ccrev < 23 && c->sihp.ccrev != 21) || !otp_powered(c))
		return false;
	orig = c->curidx;
	cc = bcm4360_si_setcoreidx(si, 0);
	z = (c->sihp.cccaps >> 19) & 7;
	if (z == 0) {
		bcm4360_si_setcoreidx(si, orig);
		return false;
	}
	l = (hw_read32(cc, CC_OTP_LAYOUT) >> 16) & 7;
	if (l == 1) {
		static const u16 r[8] = { 0, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0, 0x100 };

		if (z != 6) {
			rows = r[z];
			cols = 0x20;
		}
		oi->wsize = rows * cols / 16;
	} else if (l == 0) {
		if (z == 1) {
			rows = 0x20; cols = 0x40; oi->wsize = 0x80;
		} else if (z == 2) {
			rows = 0x40; cols = 0x40; oi->wsize = 0x100;
		} else if (z == 5) {
			rows = 0x60; cols = 0x40; oi->wsize = 0x180;
		} else if (z == 7) {
			rows = 0x10; cols = 0x40; oi->wsize = 0x40;
		}
	}
	oi->cols = cols;
	if (c->sihp.ccrev >= 40)
		oi->otpgu_base = ((hw_read32(cc, CC_OTP_LAYOUT) & 0xfff) >> 4) - 4;
	else
		oi->otpgu_base = 20;	/* TODO chip-attach: other ChipCommon revisions */
	if (((hw_read32(cc, CC_OTP_LAYOUT)) & 0x70000) != 0x10000) {
		hw_write32(cc, CC_OTP_CMD, 0x84000000);
		for (n = 0; n < 10000000; n++)
			if (!(hw_read32(cc, CC_OTP_CMD) & 0x80000000))
				break;
		if (n == 10000000) {
			bcm4360_si_setcoreidx(si, orig);
			return true;
		}
	}
	oi->status = hw_read32(cc, CC_OTP_CTL);
	if (c->sihp.chip == 0x4360 || c->sihp.chip == 0x4352 || c->sihp.chip == 0xaa06 ||
	    c->sihp.chip == 0xa9c4) {
		u16 w = otp_word(c, cc, oi, oi->otpgu_base + 3);

		oi->status |= (u32)(w >> 12) << 8;
	}
	if (oi->status & BIT(8)) {
		/* the hardware region's limit (otpr of the first general-use word) */
		(void)otp_word(c, cc, oi, oi->otpgu_base);
	}
	bcm4360_si_setcoreidx(si, orig);
	/* the fusebits read (ccrev > 34, CC(0x4c)) and the software region's limit follow
	 * in the object; they do not decide a word read */
	orig = c->curidx;
	cc = bcm4360_si_setcoreidx(si, 0);
	if (c->sihp.ccrev > 34)
		(void)hw_read32(cc, 0x4c);
	if (oi->status & BIT(9))
		(void)otp_word(c, cc, oi, oi->otpgu_base + 1);
	bcm4360_si_setcoreidx(si, orig);
	return true;
}

/* srom-otp.md otp_read_word; phy_env.h bcm4360_otp_read_word */
int core_otp_read_word(struct bcm4360_core *c, u32 word, u16 *data)
{
	struct bcm4360_si *si = &c->si;
	struct otpinfo oi;
	bool was_on = otp_powered(c);
	u32 orig;
	void *cc;
	int r;

	if (!was_on)
		otp_power(c, true);
	if (!otp_powered(c)) {
		r = -25;
		goto out;
	}
	if (!otp_init(c, &oi)) {
		r = -1;
		goto out;
	}
	orig = c->curidx;
	cc = bcm4360_si_setcoreidx(si, 0);
	*data = otp_word(c, cc, &oi, word);
	bcm4360_si_setcoreidx(si, orig);
	r = 0;
out:
	if (!was_on)
		otp_power(c, false);
	return r;
}

/* --- si_attach (chip-attach.md A2, A3) --- */

static u32 cc_setmask(struct bcm4360_core *c, u32 off, u32 mask, u32 val)
{
	return bcm4360_si_corereg(&c->si, 0, off, mask, val);
}

/* find the bus core and the core in the window (A3 step 7) */
static u32 find_bus_core(struct bcm4360_core *c, u32 win0)
{
	struct bcm4360_si *si = &c->si;
	struct core_sihpub *p = &c->sihp;
	u32 i, home = 0, pci_idx = 0x21, pci_rev = 0xffffffff, pcie_idx = 0x21, pcie_rev = 0xffffffff;
	bool seen_pci = false, seen_pcie = false, seen_gen2 = false;

	p->buscoretype = 0x700;
	p->buscorerev = 0xffffffff;
	p->buscoreidx = 0x21;
	for (i = 0; i < si->numcores; i++) {
		bcm4360_si_setcoreidx(si, i);
		core_si_sync(c);
		if (p->bustype == 1) {
			if (c->coreid[i] == CORE_ID_PCI) {
				seen_pci = true;
				pci_idx = i;
				pci_rev = bcm4360_si_corerev(si);
			} else if (c->coreid[i] == CORE_ID_PCIE_GEN2 ||
				   c->coreid[i] == CORE_ID_PCIE) {
				seen_pcie = true;
				if (c->coreid[i] == CORE_ID_PCIE_GEN2)
					seen_gen2 = true;
				pcie_idx = i;
				pcie_rev = bcm4360_si_corerev(si);
			}
		}
		if (win0 && c->coreregbase[i] == win0)
			home = i;
	}
	if (seen_pcie && (!seen_pci || find_capability(c, PCI_CAP_PCIE))) {
		p->buscoretype = seen_gen2 ? CORE_ID_PCIE_GEN2 : CORE_ID_PCIE;
		p->buscorerev = pcie_rev;
		p->buscoreidx = pcie_idx;
	} else if (seen_pci) {
		p->buscoretype = CORE_ID_PCI;
		p->buscorerev = pci_rev;
		p->buscoreidx = pci_idx;
	}
	core_si_sync(c);
	return home;
}

/* the board identity (A3 step 12, A5 for the NVRAM forms, which are empty here) */
static void board_identity(struct bcm4360_core *c)
{
	struct core_sihpub *p = &c->sihp;
	u32 ss = cfg_read(c, PCICFG_SUBSYS);

	/* TODO chip-attach A5: si_getdevpathintvar(sih, "boardvendor"/"boardtype") in the
	 * global NVRAM; the NVRAM of this driver holds no device-path variables */
	p->boardvendor = ss & 0xffff;
	p->boardtype = (u32)core_getintvar(c->vars, "boardtype");
	if (!p->boardtype)
		p->boardtype = ss >> 16;
	if (p->boardvendor == VENDOR_APPLE &&
	    (p->boardtype == 0x111 || p->boardtype == 0x112 || p->boardtype == 0x129 ||
	     p->boardtype == 0x134 || p->boardtype == 0x135))
		p->boardtype = ss >> 16;
}

static bool pmu_start(struct bcm4360_core *c)
{
	u32 x;

	bcm4360_pmu_init(&c->pmu);
	/* TODO chip-attach: si_pmu_chip_init (pmu.md; two sub-calls, window switches only) */
	x = (u32)core_getintvar(c->vars, "xtalfreq");
	if (!x)
		x = bcm4360_pmu_measure_alpclk(&c->pmu);
	bcm4360_pmu_pll_init(&c->pmu, x);
	bcm4360_pmu_res_init(&c->pmu);
	/* TODO chip-attach: si_pmu_swreg_init (pmu.md; window switches only on this card) */
	return true;
}

static bool doattach(struct bcm4360_core *c, u32 devid, void *osh, void *regs, u32 bustype,
		     char **vars, u32 *varsz)
{
	struct core_sihpub *p = &c->sihp;
	struct bcm4360_si *si = &c->si;
	struct bcm4360_srom_card card;
	u32 id, win0, home, v, l;
	void *w;

	(void)regs;
	hw_memset(p, 0, sizeof(*p));
	p->buscoreidx = 0x21;
	core_si_sync(c);
	if (bustype == 1) {
		if (cfg_read(c, PCICFG_0x88) == CHIPID_ALL_ONES)
			return false;	/* a system-bus core: not this driver */
		win0 = cfg_read(c, PCICFG_BAR0_WIN);
		if (win0 < 0x18000000 || win0 > 0x1801ffff || (win0 & 0xfff))
			win0 = 0x18000000;
		cfg_write(c, PCICFG_BAR0_WIN, 0x18000000);
	} else {
		return false;
	}
	p->bustype = bustype;
	core_si_sync(c);
	if (!bcm4360_si_ldo_war(si, devid))
		return false;
	if (!find_capability(c, PCI_CAP_PCIE))
		bcm4360_si_clkctl_xtal(si, 3, true);

	id = hw_read32(c->hw->d11, CC_CHIPID);
	p->chip = id & 0xffff;
	p->chiprev = (id >> 16) & 0xf;
	p->chippkg = (id >> 20) & 0xf;
	p->socitype = id >> 28;
	if (p->chip == 0x4329 && p->chiprev == 0 && p->chippkg)
		p->chippkg = 1;
	p->pkg_14_15 = p->chippkg == 14 || p->chippkg == 15;
	core_si_sync(c);

	if (p->socitype != 1 && p->socitype != 3)
		return false;
	ai_scan(c);
	if (!si->numcores)
		return false;

	/* ChipCommon identity (step 6) */
	w = bcm4360_si_setcoreidx(si, 0);
	p->ccrev = bcm4360_si_corerev(si);
	core_si_sync(c);
	if (p->ccrev > 10)
		p->chipst = hw_read32(w, 0x2c);
	p->cccaps = hw_read32(w, 0x04);
	if (p->ccrev > 34)
		p->cccaps_ext = hw_read32(w, 0xac);
	if (p->cccaps & BIT(28)) {
		p->pmucaps = hw_read32(w, 0x604);
		p->pmurev = p->pmucaps & 0xff;
	}
	core_si_sync(c);

	home = find_bus_core(c, win0);

	/* step 8: the PCIe bridge */
	if (bustype == 1 && (p->buscoretype == CORE_ID_PCIE_GEN2 ||
			     p->buscoretype == CORE_ID_PCIE ||
			     (p->buscoretype == CORE_ID_PCI && p->buscorerev > 12))) {
		pcie_init(c);
		if (core_si_pci_fixcfg(c))
			goto fail;
	}
	bcm4360_si_setcoreidx(si, home);

	/* step 10: chip revision < 3 of the 4360 family (TODO chip-attach B16) */

	/* step 11: the variables */
	bcm4360_nvram_init(osh, bustype);
	card.ctx = p;
	card.osh = osh;
	card.regs = c->hw->bar0;
	card.bustype = bustype;
	card.ccrev = p->ccrev;
	card.cccaps = p->cccaps;
	card.chip = p->chip;
	card.chiprev = p->chiprev;
	c->vars = NULL;
	c->varsz = 0;
	if (bcm4360_srom_var_init(&card, &c->vars, &c->varsz))
		goto fail;
	if (vars)
		*vars = c->vars;
	if (varsz)
		*varsz = c->varsz;

	/* steps 12, 13 */
	board_identity(c);
	p->boardrev = (u32)core_getintvar(c->vars, "boardrev");
	p->boardflags = (u32)core_getintvar(c->vars, "boardflags");
	core_si_sync(c);

	/* step 14 */
	if (p->ccrev > 19) {
		w = bcm4360_si_setcore(si, CORE_ID_CC, 0);
		hw_write32(w, 0x58, 0);
		hw_write32(w, 0x5c, 0);
		bcm4360_si_setcoreidx(si, home);
	}
	/* step 15 */
	if (p->cccaps & BIT(28))
		pmu_start(c);
	/* step 16 */
	if (p->ccrev > 15) {
		l = (u32)core_getintvar(c->vars, "leddc");
		if (!l)
			l = 0xa005a;
		cc_setmask(c, CC_GPIOTIMERVAL, 0xffffffff, l);
	}
	if (bustype == 1 && c->pcie.valid && bus_is_pcie(c))
		pcie_attach(c);
	/* step 18: muxenab (not described, absent on this card) */
	/* step 19 */
	if (p->ccrev > 20) {
		v = cc_setmask(c, 0x08, 0, 0);
		cc_setmask(c, 0x08, v | 4, v | 4);
	}
	return true;
fail:
	c->pcie.valid = false;
	return false;
}

/* chip-attach.md A2; the handle is the core's `sihp` */
void *core_si_attach(struct bcm4360_core *c, u32 device, void *osh, void *regs, u32 bustype,
		     char **vars, u32 *varsz)
{
	if (!doattach(c, device, osh, regs, bustype, vars, varsz))
		return NULL;
	return &c->sihp;
}

/* chip-attach.md A6; no hardware access */
void core_si_detach(struct bcm4360_core *c)
{
	bcm4360_nvram_exit(c->osh);
	c->pcie.valid = false;
}
