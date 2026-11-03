// SPDX-License-Identifier: ISC
/*
 * Attach and detach of the hardware MAC layer (docs/re/spec/bmac-attach.md;
 * the DMA parameters of bmac-data.md P54, the FIFO size table and the
 * backplane's interrupt callbacks of bmac-run.md R8, R9).
 *
 * The MAC layer's state is still read in Broadcom's layout by code that is
 * not open (bmac-attach.md, "Consequence for an open attach"), so the attach
 * builds exactly that: the blocks below with the offsets of the
 * specification's Data section. Everything outside this file is reached
 * through the services of bcm4360/bmac_attach.h.
 */
#include <bcm4360/bmac_attach.h>
#include <bcm4360/utils.h>
#include <bcm4360/hw.h>

#define P(b, off)	(*(void **)((u8 *)(b) + (off)))
#define U8(b, off)	(*(u8 *)((u8 *)(b) + (off)))
#define U16(b, off)	(*(u16 *)((u8 *)(b) + (off)))
#define U32(b, off)	(*(u32 *)((u8 *)(b) + (off)))
#define ADDR(b, off)	((void *)((u8 *)(b) + (off)))

/* wlc_hw (Data: "`wlc_hw` after a successful attach") */
#define HW_SIZE			0x1d0
#define HW_WLC			0x000
#define HW_CORESTATE		0x008
#define HW_OSH			0x010
#define HW_UNIT			0x018	/* u32 */
#define HW_PIOMODE		0x01c	/* u8 */
#define HW_DI(f)		(0x020 + 8 * (f))
#define HW_VENDID		0x080	/* u16 */
#define HW_DEVICEID		0x082	/* u16 */
#define HW_COREREV		0x084	/* u32 */
#define HW_SROMREV		0x088	/* u8 */
#define HW_BOARDREV		0x08a	/* u16 */
#define HW_BOARDFLAGS		0x08c
#define HW_BOARDFLAGS2		0x090
#define HW_DEFMACINTMASK	0x09c
#define HW_MACHWCAP		0x0a4
#define HW_MACHWCAP_BACKUP	0x0a8
#define HW_AC			0x0ac	/* u16, read by the host-flag rules of 13.7 */
#define HW_BTC			0x0b0
#define HW_SIH			0x0b8
#define HW_VARS			0x0c0
#define HW_VARS_SIZE		0x0c8	/* u32 */
#define HW_REGS			0x0d0
#define HW_PHYSHIM		0x0d8
#define HW_PHY_SH		0x0e0
#define HW_BAND			0x0e8	/* the current band state */
#define HW_BANDSTATE(u)		(0x0f0 + 8 * (u))
#define HW_SHORTSLOT		0x102	/* u8 */
#define HW_SRL			0x104	/* u16 */
#define HW_LRL			0x106
#define HW_SFBL			0x108
#define HW_LFBL			0x10a
#define HW_UP			0x10c	/* u8 (bmac-run.md R9) */
#define HW_NBANDS		0x118	/* u32 */
#define HW_CHANSPEC		0x11c	/* u16 */
#define HW_TXAVAIL(f)		(0x120 + 8 * (f))
#define HW_XMTFIFO_SZ_TBL	0x150
#define HW_ETHERADDR		0x178	/* 6 bytes */
#define HW_LED			0x198
#define HW_INTRCVLAZY		0x1ac
#define HW_BTSWITCH		0x1bc	/* u8 */
#define HW_ANTSWCTL2G		0x1bd	/* u8 */
#define HW_ANTSWCTL5G		0x1be	/* u8 */
#define HW_PCIE2_WAR_FREQ	0x1c0
#define HW_1C4			0x1c4	/* written 0; purpose unknown (Q4) */

#define BTC_SIZE		0x20
#define BTC_AA2G		0x18	/* u8 */
#define BANDS_SIZE		0x70	/* both band states, 0x38 each */
#define BAND_SIZE		0x38
#define CORESTATE_SIZE		0x60
#define CORESTATE_DI(f)		(8 * (f))
#define SHIM_SIZE		0x18

/* a band state of wlc_hw */
#define BS_BANDTYPE		0x00	/* u32: 2 = 2.4 GHz, 1 = 5 GHz */
#define BS_BANDUNIT		0x04	/* u32 */
#define BS_MHFS			0x08	/* five u16 */
#define BS_MHFS_N		5
#define BS_CWMIN		0x14	/* u16 */
#define BS_CWMAX		0x16
#define BS_CORE_FLAGS		0x18	/* u32 */
#define BS_PHYTYPE		0x1c	/* u16; the u32 at 0x1c is type | rev << 16 */
#define BS_PHYREV		0x1e
#define BS_RADIOID		0x20
#define BS_RADIOREV		0x22
#define BS_PI			0x28
#define BS_ENCORE		0x30	/* u8 */

/* wlc_info_t */
#define WLC_PUB			0x000
#define WLC_WL			0x010
#define WLC_REGS		0x018
#define WLC_HW			0x020
#define WLC_CORESTATE		0x028
#define WLC_CORE		0x038	/* its first u32: the core index */
#define WLC_BAND		0x040	/* the upper layer's current band state */
#define WLC_BANDSTATE(u)	(0x050 + 8 * (u))
#define WLC_PCI_WAR		0x060	/* u8 */
#define WLC_VENDID		0x228	/* u16 */
#define WLC_DEVID		0x22a	/* u16 */
#define WLC_RXOFFSET		0x6bc	/* u32 */
#define WLC_4331_REQSIZE	0x72c	/* u8 */
#define WLC_TEMPLATE(i)		(0x730 + 4 * (i))
#define WLC_RXOFFSET_ALIGN	0x818	/* u32 */

/* an upper-layer band state */
#define UBS_BANDTYPE		0x00	/* u32 */
#define UBS_BANDUNIT		0x04	/* u32 */
#define UBS_PHYTYPE		0x08	/* u16 */
#define UBS_PHYREV		0x0a
#define UBS_RADIOID		0x0c
#define UBS_RADIOREV		0x0e
#define UBS_PI			0x10
#define UBS_ENCORE		0x18	/* u8 */

/* wlc_pub_t */
#define PUB_COREREV		0x014	/* u32 */
#define PUB_TUNABLES		0x038	/* u32 words: 0 ntxd, 1 nrxd, 2 rxbufsize, 3 nrxbufpost, 14 rxbnd */
#define PUB_NBANDS		0x044	/* u32 */
#define PUB_BOARDREV		0x092	/* u16 */
#define PUB_SROMREV		0x094	/* u8 */
#define PUB_BOARDFLAGS		0x098
#define PUB_BOARDFLAGS2		0x09c
#define PUB_SIH			0x100
#define PUB_VARS		0x108
#define PUB_VARS_SIZE		0x110	/* u32 */

/* the backplane handle's public part (spec chip) */
#define SIH_BUSTYPE		0x04
#define SIH_BUSCORETYPE		0x08
#define SIH_BUSCOREREV		0x0c
#define SIH_BOARDTYPE		0x28
#define SIH_BOARDVENDOR		0x30
#define SIH_CHIP		0x3c
#define SIH_CHIPREV		0x40
#define SIH_CHIPPKG		0x44
#define SIH_CHIPST		0x48

/* the descriptor of wlc_phy_shared_attach (Data, 0x58 bytes) */
#define DESC_SIZE		0x58
#define DESC_OSH		0x00
#define DESC_SIH		0x08
#define DESC_PHYSHIM		0x10
#define DESC_UNIT		0x18
#define DESC_COREREV		0x1c
#define DESC_BUSTYPE		0x20
#define DESC_BUSCOREREV		0x24
#define DESC_VARS		0x28
#define DESC_VID		0x30	/* u16 */
#define DESC_DID		0x32	/* u16 */
#define DESC_CHIP		0x34
#define DESC_CHIPREV		0x38
#define DESC_CHIPPKG		0x3c
#define DESC_SROMREV		0x40
#define DESC_BOARDTYPE		0x44
#define DESC_BOARDREV		0x48
#define DESC_BOARDVENDOR	0x4c
#define DESC_BOARDFLAGS		0x50
#define DESC_BOARDFLAGS2	0x54

#define VENDOR_BROADCOM		0x14e4
#define VENDOR_APPLE		0x106b
#define BUS_SI			0
#define BUS_PCI			1
#define PCIE2_CORE_ID		0x83c
#define D11_CORE_ID		0x812
#define D11_MACHWCAP		0x15c	/* register of the 802.11 core */
#define PHY_TYPE_AC		11
#define NFIFO			6
#define BAND_2G			0
#define BAND_5G			1
#define BANDTYPE_2G		2
#define BANDTYPE_5G		1

/* error codes of wlc_bmac_attach (Data, "Error codes") */
#define E_HW_ALLOC		0x3f2	/* .. 0x3f5: the four blocks of wlc_hw_attach */
#define E_CHIPMATCH		0x0c
#define E_UNSUPPORTED		0x0d
#define E_CHIP_ACCESS		0x0e
#define E_BOARDREV		0x0f
#define E_PHY_SHARED		0x10
#define E_PHY_ATTACH		0x11
#define E_PHY_VERSION		0x12
#define E_DMA			0x13
#define E_NO_MACADDR		0x15
#define E_BAD_MACADDR		0x16
#define E_LED			0x17
#define E_SHIM			0x19

/* Constant tables (Data) */

static const u16 chipmatch_ids[] = {	/* wlc_chipmatch, vendor 0x14e4 */
	0x4320, 0x4325, 0x4303, 0x4321, 0x4324, 0x4318, 0x4319, 0x431a, 0x4311, 0x4313,
	0x4312, 0x4314, 0x4315, 0x4316, 0x431b, 0x431c, 0x431d, 0x4328, 0x4329, 0x432a,
	0x432b, 0x432c, 0x432d, 0x4341, 0x4340, 0x4350, 0x4351, 0x4352, 0x4353, 0x4357,
	0xa99d, 0x0576, 0x4354, 0x435f, 0x433f, 0x434f, 0x432f, 0x4334, 0x4335, 0x4336,
	0x4337, 0x4338, 0x4339, 0x4716, 0x4748, 0x4727, 0x4343, 0x4360, 0x4346, 0x4347,
	0x4348, 0x4331, 0x4332, 0x4333, 0x43aa, 0x4358, 0x4359, 0x435a, 0x43a9, 0x4355,
	0x4356, 0x4363, 0x4380, 0x4381, 0x4382, 0x4364, 0x4365, 0x4366, 0x4374, 0x4367,
	0x43a0, 0x43a1, 0x43a2, 0x43ae, 0x43af, 0x43b0, 0x43b1, 0x43b2, 0x43b3, 0x43a3,
	0x43a4, 0x43a5,
};

static const u16 singleband_5g_ids[] = {
	0x4313, 0x4321, 0x432a, 0x431a, 0x431d, 0x4316, 0x4352, 0x432d, 0x4348, 0x435a,
	0x43a2, 0x4333, 0x43b3, 0x43b0, 0x434f,
};

static const u16 dualband_ids[] = {	/* step 10.3 */
	0x4319, 0x4324, 0x4312, 0x4328, 0x432b, 0x4314, 0x431b, 0x4350, 0x4353, 0x0576,
	0xa99d, 0x4354, 0x4346, 0x435f, 0x4331, 0x4334, 0x4359, 0x4374, 0x4367, 0x4380,
	0x43a0, 0x43ae, 0x43b1, 0x43a3,
};

static const u32 singleband_chips[] = {	/* step 10.3: forced to one band */
	0xa8df, 0xa8d5, 0xa8d9, 0xa8e3, 0xa87b, 0xa8d1, 0xa8db,
};

static const u16 tunables_ids[] = {	/* wlc_tunables_override */
	0x43a0, 0x43a1, 0x43a2, 0x43b1, 0x43b2, 0x43b3, 0x43a3, 0x43a4, 0x43a5,
};

/* MAC core revisions 0..31 and 32..63 accepted (step 5) */
#define COREREV_OK_LOW		0xffffffb0u
#define COREREV_OK_HIGH		0x0000ff07u

/*
 * The transmit FIFO sizes of MAC revisions >= 40 (FIFOs 0..5, units of the
 * microcode). wlc_hw+0x150 points here; wlc_bmac_ampdu_set may rewrite the
 * table of revisions 26 and 29, so it is not const.
 */
static u16 xmtfifo_sz_rev40[NFIFO] = { 0x0009, 0x00b7, 0x0019, 0x0011, 0x0011, 0x0008 };

/* wlc_bmac_ampdu_set's tables for revisions 26 and 29 (bmac-run.md R8) */
static const u16 xmtfifo_sz_ampdu2[NFIFO] = { 0x62, 0xff, 0xf3, 0x15, 0x11, 0x05 };
static const u16 xmtfifo_sz_ampdu[NFIFO] = { 0x96, 0xdf, 0xdf, 0x15, 0x11, 0x05 };

/*
 * The burst and prefetch parameters (bmac-data.md P54): written by the attach
 * before the first engine, read by bcm4360_bmac_dma_param_init.
 */
static u16 dma_txmultioutstdrd, dma_txprefetchctl, dma_txprefetchthresh, dma_txburstlen;
static u16 dma_rxprefetchctl, dma_rxprefetchthresh, dma_rxburstlen;

/* the PHY versions accepted (step 13.5): bit `phyrev & 31` of the mask */
static bool phy_version_ok(u16 phytype, u16 phyrev)
{
	u32 mask;

	switch (phytype) {
	case 0:
		mask = 0x1ec;
		break;
	case 2:
		mask = 0x3c6;
		break;
	case 4:
		mask = 0x1f07ff;
		break;
	case 5:
		mask = 0x1f;
		break;
	case 6:
	case 8:
		mask = 0xf;
		break;
	case 7:
		mask = 0x3;
		break;
	case 10:
		mask = 0x4f;
		break;
	case PHY_TYPE_AC:
		mask = 0xff;
		break;
	default:
		return false;
	}
	return (mask >> (phyrev & 31)) & 1;
}

static bool in_list16(const u16 *list, u32 n, u32 v)
{
	u32 i;

	for (i = 0; i < n; i++)
		if (list[i] == v)
			return true;
	return false;
}

#define IN_LIST(list, v)	in_list16(list, sizeof(list) / sizeof(list[0]), v)

/* --- helpers --- */

void *bcm4360_calloc(void *osh, u32 unit, u32 size)
{
	void *p = osl_malloc(osh, size);

	(void)unit;
	if (p)
		osl_memset(p, 0, size);
	return p;
}

bool bcm4360_chipmatch(u16 vendor, u16 device)
{
	return vendor == VENDOR_BROADCOM && IN_LIST(chipmatch_ids, device);
}

bool bcm4360_is_singleband_5g(u32 device)
{
	return IN_LIST(singleband_5g_ids, device);
}

void bcm4360_tunables_override(u32 *t, u32 device, u32 corerev)
{
	if (!IN_LIST(tunables_ids, device) || corerev <= 41 || corerev == 44)
		return;
	t[0] = 0x400;		/* ntxd */
	t[1] = 0x400;		/* nrxd */
	t[14] = 0x20;		/* rxbnd */
	t[3] = 0x100;		/* nrxbufpost */
}

bool bcm4360_ether_atoe(const char *s, u8 *ea)
{
	const char *end;
	int k;

	for (k = 0; k < 6; k++) {
		ea[k] = (u8)bcm4360_strtoul(s, &end, 16);
		if (!*end)
			return k == 5;
		s = end + 1;
	}
	return true;
}

void bcm4360_hw_set_di(void *hw, u32 fifo, void *di)
{
	P(hw, HW_DI(fifo)) = di;
	P(P(hw, HW_CORESTATE), CORESTATE_DI(fifo)) = di;
}

void bcm4360_hw_get_txavail(void *hw, void **out)
{
	int f;

	for (f = 0; f < NFIFO; f++)
		out[f] = P(hw, HW_TXAVAIL(f));
}

u32 bcm4360_hw_get_bandunit(void *hw)
{
	return U32(P(hw, HW_BAND), BS_BANDUNIT);
}

void bcm4360_template_cfg_init(void *wlc, u32 corerev)
{
	bool rev40 = corerev >= 40;

	U32(wlc, WLC_TEMPLATE(0)) = rev40 ? 0x6e8 : 0x640;
	U32(wlc, WLC_TEMPLATE(1)) = rev40 ? 0x200 : 0x068;
	U32(wlc, WLC_TEMPLATE(2)) = rev40 ? 0x200 : 0x100;
	U32(wlc, WLC_TEMPLATE(3)) = rev40 ? 1 : 2;
	U32(wlc, WLC_TEMPLATE(4)) = rev40 ? 1 : 2;
}

void *bcm4360_bmac_si_attach(u32 device, void *osh, void *regs, u32 bustype, void *btparam,
			     char **vars, u32 *varsz)
{
	return bcm4360_attach_env_si_attach(device, osh, regs, bustype, btparam, vars, varsz);
}

void bcm4360_bmac_si_detach(void *osh, void *sih)
{
	(void)osh;
	if (sih)
		bcm4360_attach_env_si_detach(sih);
}

void *bcm4360_phy_shim_attach(void *hw, void *wl, void *wlc)
{
	void *s = osl_malloc(P(hw, HW_OSH), SHIM_SIZE);

	if (s) {
		osl_memset(s, 0, SHIM_SIZE);
		P(s, 0x00) = hw;
		P(s, 0x08) = wlc;
		P(s, 0x10) = wl;
	}
	return s;
}

void bcm4360_phy_shim_detach(void *s)
{
	if (s)
		osl_mfree(P(P(s, 0x00), HW_OSH), s, SHIM_SIZE);
}

/* --- bmac-run.md R8 --- */

void bcm4360_bmac_ampdu_set(void *hw, u8 mode)
{
	u32 corerev = U32(hw, HW_COREREV);
	const u16 *from = mode == 2 ? xmtfifo_sz_ampdu2 : xmtfifo_sz_ampdu;
	u16 *to = P(hw, HW_XMTFIFO_SZ_TBL);
	int f;

	if (corerev != 26 && corerev != 29)
		return;
	for (f = 0; f < NFIFO; f++)
		to[f] = from[f];
}

s32 bcm4360_bmac_xmtfifo_sz_get(void *hw, u32 fifo, u32 *out)
{
	const u16 *t = P(hw, HW_XMTFIFO_SZ_TBL);

	if (fifo >= NFIFO)
		return -29;
	*out = t[fifo];
	return 0;
}

/* --- bmac-run.md R9: around the backplane's moves of its register window --- */

u32 bcm4360_bmac_wlintrsoff(void *hw)
{
	if (!U8(hw, HW_UP))
		return 0;
	return bcm4360_attach_env_intrsoff(P(P(hw, HW_WLC), WLC_WL));
}

void bcm4360_bmac_wlintrsrestore(void *hw, u32 mask)
{
	if (U8(hw, HW_UP))
		bcm4360_attach_env_intrsrestore(P(P(hw, HW_WLC), WLC_WL), mask);
}

/* --- bmac-data.md P54 --- */

void bcm4360_bmac_dma_param_init(void *hw, u32 bustype, void *di)
{
	void *sih = P(hw, HW_SIH);
	u32 chip = U32(sih, SIH_CHIP);
	u32 corerev = U32(hw, HW_COREREV);

	if (bustype == BUS_PCI) {
		if (corerev < 32)
			return;
		if (chip == 0xa9c4 || chip == 0x4360 || chip == 0x4350 || chip == 0x4352) {
			bcm4360_attach_env_dma_param_set(di, 0x000, dma_txmultioutstdrd);
			bcm4360_attach_env_dma_param_set(di, 0x001, dma_txprefetchctl);
			bcm4360_attach_env_dma_param_set(di, 0x002, dma_txprefetchthresh);
			bcm4360_attach_env_dma_param_set(di, 0x003, dma_txburstlen);
			bcm4360_attach_env_dma_param_set(di, 0x100, dma_rxprefetchctl);
			bcm4360_attach_env_dma_param_set(di, 0x101, dma_rxprefetchthresh);
			bcm4360_attach_env_dma_param_set(di, 0x102, dma_rxburstlen);
		} else {
			bcm4360_attach_env_dma_burstlen_set(di, 3, 3);
		}
	} else if (bustype == BUS_SI) {
		if (corerev < 32)
			return;
		if (chip != 0xaa06 && chip != 0x4360 && chip != 0xa9c4 && chip != 0x4352 &&
		    chip != 0x4350 && chip != 0x4335)
			return;
		bcm4360_attach_env_dma_param_set(di, 0x000, 0);
		bcm4360_attach_env_dma_param_set(di, 0x001, 0);
		bcm4360_attach_env_dma_param_set(di, 0x002, 0);
		bcm4360_attach_env_dma_param_set(di, 0x003, chip == 0x4335 ? 1 : 2);
		bcm4360_attach_env_dma_param_set(di, 0x100, 0);
		bcm4360_attach_env_dma_param_set(di, 0x101, 0);
		bcm4360_attach_env_dma_param_set(di, 0x102, 2);
	}
}

/* --- wlc_hw_attach, wlc_hw_detach --- */

void bcm4360_hw_detach(void *hw)
{
	void *osh;

	if (!hw)
		return;
	osh = P(hw, HW_OSH);
	P(P(hw, HW_WLC), WLC_CORESTATE) = NULL;
	if (P(hw, HW_BTC))
		osl_mfree(osh, P(hw, HW_BTC), BTC_SIZE);
	if (P(hw, HW_BANDSTATE(0)))
		osl_mfree(osh, P(hw, HW_BANDSTATE(0)), BANDS_SIZE);
	if (P(hw, HW_CORESTATE))
		osl_mfree(osh, P(hw, HW_CORESTATE), CORESTATE_SIZE);
	osl_mfree(osh, hw, HW_SIZE);
}

void *bcm4360_hw_attach(void *wlc, void *osh, u32 unit, u32 *err)
{
	void *hw, *p;

	hw = bcm4360_calloc(osh, unit, HW_SIZE);
	if (!hw) {
		*err = E_HW_ALLOC;
		return NULL;
	}
	P(hw, HW_WLC) = wlc;
	P(hw, HW_OSH) = osh;
	U32(hw, HW_UNIT) = unit;
	P(hw, HW_BTC) = bcm4360_calloc(osh, unit, BTC_SIZE);
	if (!P(hw, HW_BTC)) {
		*err = E_HW_ALLOC + 1;
		goto fail;
	}
	p = bcm4360_calloc(osh, unit, BANDS_SIZE);
	P(hw, HW_BANDSTATE(0)) = p;
	if (!p) {
		*err = E_HW_ALLOC + 2;
		goto fail;
	}
	P(hw, HW_BANDSTATE(1)) = (u8 *)p + BAND_SIZE;
	P(hw, HW_CORESTATE) = bcm4360_calloc(osh, unit, CORESTATE_SIZE);
	if (!P(hw, HW_CORESTATE)) {
		*err = E_HW_ALLOC + 3;
		goto fail;
	}
	*err = 0;
	P(wlc, WLC_CORESTATE) = P(hw, HW_CORESTATE);
	return hw;
fail:
	bcm4360_hw_detach(hw);
	return NULL;
}

/* --- wlc_bmac_attach --- */

/* "wl%d" into an 8-byte buffer (step 13.6.1) */
static void unit_name(char *buf, u32 unit)
{
	char digits[12];
	int n = 0, i = 0;
	u32 v = unit;

	buf[i++] = 'w';
	buf[i++] = 'l';
	if ((s32)unit < 0) {
		buf[i++] = '-';
		v = 0 - unit;
	}
	do {
		digits[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n && i < 7)
		buf[i++] = digits[--n];
	buf[i] = 0;
}

/* step 7.2: a valid board revision */
static bool boardrev_ok(u16 r)
{
	if (r == 0)
		return false;
	if (r <= 0xff)
		return true;
	return ((r >> 12) == 1 || (r >> 12) == 2) &&
	       ((r >> 8) & 0xf) >= 1 && ((r >> 8) & 0xf) <= 9 &&
	       ((r >> 4) & 0xf) <= 9 && (r & 0xf) <= 9;
}

/* step 13.7: the host-flag copies of a band */
static void band_mhfs(void *hw, void *band)
{
	void *sih = P(hw, HW_SIH);
	u16 *mhfs = ADDR(band, BS_MHFS);
	u16 phytype = U16(band, BS_PHYTYPE), phyrev = U16(band, BS_PHYREV);
	u16 radioid = U16(band, BS_RADIOID), radiorev = U16(band, BS_RADIOREV);
	u32 boardflags = U32(hw, HW_BOARDFLAGS);
	u32 chip = U32(sih, SIH_CHIP), pkg = U32(sih, SIH_CHIPPKG);

	osl_memset(mhfs, 0, 2 * BS_MHFS_N);
	if (U32(band, BS_PHYTYPE) == 0x00010002)
		mhfs[0] |= 0x0020;
	if (phytype == 2 && (boardflags & 0x2) && U16(hw, HW_AC))
		mhfs[0] |= 0x0040;
	if (phytype == 2 && phyrev <= 2) {
		if (!(mhfs[0] & 0x0040))
			mhfs[0] |= 0x0008;
	} else if ((chip == 0x4331 || chip == 0xa9a7) && (pkg == 9 || pkg == 0xb)) {
		mhfs[0] |= 0x0008;
	}
	if (boardflags & 0x20)
		mhfs[0] |= 0x0400;
	if (radioid == 0x2050 && radiorev <= 5)
		mhfs[1] |= 0x0004;
	if (phytype == 4 && phyrev <= 1) {
		mhfs[1] |= 0x0800;
		mhfs[0] |= 0x0200;
	}
	if (phytype == 4 && U8(hw, HW_SROMREV) > 8)
		mhfs[1] |= 0x0080;
	if (U32(hw, HW_COREREV) >= 40 && bcm4360_attach_env_si_chip_hostif(sih) == 2)
		mhfs[2] |= 0x0010;
}

/* step 13.6: the DMA engines of FIFOs 0..3 */
static u32 attach_dma(void *wlc, void *hw)
{
	void *pub = P(wlc, WLC_PUB), *osh = P(hw, HW_OSH), *sih = P(hw, HW_SIH);
	u8 *regs = P(hw, HW_REGS);
	const u32 *t = P(pub, PUB_TUNABLES);
	static const u32 txregs[4] = { 0x200, 0x240, 0x280, 0x2c0 };
	char name[8];
	void *di;
	u32 f, w;

	unit_name(name, U32(hw, HW_UNIT));
	w = bcm4360_attach_env_dma_addrwidth(sih, regs + txregs[0]);
	if (!bcm4360_attach_env_alloc_dma_resources(P(wlc, WLC_WL), w))
		return E_DMA;
	di = bcm4360_attach_env_dma_attach(osh, name, sih, regs + txregs[0], regs + 0x220,
					   t[0], t[1], t[2], 0, t[3], U32(wlc, WLC_RXOFFSET));
	if (!di)
		return E_DMA;
	if (U32(sih, SIH_BUSTYPE) == BUS_PCI) {
		dma_txmultioutstdrd = 1;
		dma_txprefetchthresh = 3;
		dma_txprefetchctl = 3;
		dma_txburstlen = 6;
		dma_rxprefetchthresh = 3;
		dma_rxprefetchctl = 3;
		dma_rxburstlen = 3;
		bcm4360_bmac_dma_param_init(hw, BUS_PCI, di);
	} else if (U32(sih, SIH_BUSTYPE) == BUS_SI) {
		bcm4360_bmac_dma_param_init(hw, BUS_SI, di);
	}
	bcm4360_hw_set_di(hw, 0, di);
	for (f = 1; f < 4; f++) {
		di = bcm4360_attach_env_dma_attach(osh, name, sih, regs + txregs[f], NULL,
						   t[0], 0, 0, -1, 0, 0);
		if (!di)
			return E_DMA;
		bcm4360_bmac_dma_param_init(hw, U32(sih, SIH_BUSTYPE), di);
		bcm4360_hw_set_di(hw, f, di);
	}
	for (f = 0; f < NFIFO; f++)
		if (P(hw, HW_DI(f)))
			P(hw, HW_TXAVAIL(f)) = bcm4360_attach_env_dma_txavail(P(hw, HW_DI(f)));
	return 0;
}

/* step 13: one band; 0 or an error code */
static u32 attach_band(void *wlc, void *hw, u32 unit)
{
	void *sih = P(hw, HW_SIH), *band, *ubs, *pi;
	u32 corerev = U32(hw, HW_COREREV), type = unit == BAND_2G ? BANDTYPE_2G : BANDTYPE_5G;
	u32 c;

	/* 13.1 */
	bcm4360_attach_env_setxband(hw, unit);
	band = P(hw, HW_BAND);
	U32(band, BS_BANDUNIT) = unit;
	U32(band, BS_BANDTYPE) = type;
	ubs = P(wlc, WLC_BAND);
	U32(ubs, UBS_BANDUNIT) = unit;
	U32(ubs, UBS_BANDTYPE) = type;
	U32(P(wlc, WLC_CORE), 0) = bcm4360_attach_env_si_coreidx(sih);

	/* 13.2: the MAC capabilities */
	if (corerev > 12) {
		c = hw_read32(P(hw, HW_REGS), D11_MACHWCAP);
		if (corerev == 0x1d || corerev == 0x1e || corerev == 0x21 || corerev == 0x22 ||
		    corerev == 0x28 || corerev == 0x29 || corerev == 0x2b || corerev == 0x2c ||
		    (corerev == 0x1a && U32(sih, SIH_CHIPREV) == 0))
			c &= 0x7fffffff;
		U32(hw, HW_MACHWCAP) = c;
		U32(hw, HW_MACHWCAP_BACKUP) = c;
	}

	/* 13.3: revisions below 40 are refused in step 5 */
	P(hw, HW_XMTFIFO_SZ_TBL) = xmtfifo_sz_rev40;
	bcm4360_bmac_ampdu_set(hw, 1);

	/* 13.4: the PHY */
	pi = bcm4360_attach_env_phy_attach(P(hw, HW_PHY_SH), P(hw, HW_REGS),
					   (s32)U32(band, BS_BANDTYPE), P(hw, HW_VARS));
	P(band, BS_PI) = pi;
	if (!pi)
		return E_PHY_ATTACH;
	if (U16(band, BS_PHYTYPE) != PHY_TYPE_AC)
		bcm4360_attach_env_set_btswitch(hw, -1);
	bcm4360_attach_env_phy_machwcap_set(pi, U32(hw, HW_MACHWCAP));
	bcm4360_attach_env_phy_get_phyversion(pi, ADDR(band, BS_PHYTYPE), ADDR(band, BS_PHYREV),
					      ADDR(band, BS_RADIOID), ADDR(band, BS_RADIOREV));
	U8(band, BS_ENCORE) = bcm4360_attach_env_phy_get_encore(pi);
	U8(ubs, UBS_ENCORE) = bcm4360_attach_env_phy_get_encore(pi);
	U32(band, BS_CORE_FLAGS) = bcm4360_attach_env_phy_get_coreflags(pi);

	/* 13.5: the PHY version */
	if (!phy_version_ok(U16(band, BS_PHYTYPE), U16(band, BS_PHYREV)))
		return E_PHY_VERSION;
	P(ubs, UBS_PI) = P(band, BS_PI);
	U16(ubs, UBS_PHYTYPE) = U16(band, BS_PHYTYPE);
	U16(ubs, UBS_PHYREV) = U16(band, BS_PHYREV);
	U16(ubs, UBS_RADIOID) = U16(band, BS_RADIOID);
	U16(ubs, UBS_RADIOREV) = U16(band, BS_RADIOREV);
	U16(band, BS_CWMIN) = 0x000f;
	U16(band, BS_CWMAX) = 0x03ff;

	/* 13.6: the DMA engines, in the first pass */
	if (!P(hw, HW_DI(0))) {
		c = attach_dma(wlc, hw);
		if (c)
			return c;
	}

	/* 13.7 */
	band_mhfs(hw, band);
	return 0;
}

u32 bcm4360_bmac_attach(void *wlc, u16 vendor, u16 device, u32 unit, u8 piomode, void *osh,
			void *regsva, u32 bustype, void *btparam)
{
	void *pub = P(wlc, WLC_PUB);
	void *hw, *sih, *regs, *v;
	const char *vars, *s;
	u8 desc[DESC_SIZE];
	u32 e, corerev, chip, boardtype, nbands, i;
	u16 boardrev;
	s32 r;
	u8 *ea;
	bool all0, allff;
	s32 m;

	(void)regsva;
	(void)btparam;

	/* step 1: state and defaults */
	hw = bcm4360_hw_attach(wlc, osh, unit, &e);
	if (!hw)
		return e;
	P(wlc, WLC_HW) = hw;
	U8(hw, HW_SHORTSLOT) = 0;
	U16(hw, HW_SFBL) = 3;
	U16(hw, HW_LFBL) = 2;
	U16(hw, HW_SRL) = 7;
	U16(hw, HW_LRL) = 4;
	U8(hw, HW_PIOMODE) = piomode;
	U32(hw, HW_DEFMACINTMASK) = 0xb0e7a860;
	U32(hw, HW_1C4) = 0;
	U16(hw, HW_CHANSPEC) = 0x1001;
	U8(hw, HW_BTSWITCH) = 0xff;
	sih = P(pub, PUB_SIH);
	P(hw, HW_SIH) = sih;
	vars = P(pub, PUB_VARS);
	P(hw, HW_VARS) = (void *)vars;
	U32(hw, HW_VARS_SIZE) = U32(pub, PUB_VARS_SIZE);

	/* step 2: identity */
	if (bustype != BUS_SI) {
		s = bcm4360_attach_env_getvar(vars, "vendid");
		if (s)
			vendor = (u16)bcm4360_strtoul(s, NULL, 0);
		s = bcm4360_attach_env_getvar(vars, "devid");
		if (s && (u16)bcm4360_strtoul(s, NULL, 0) != 0xffff)
			device = (u16)bcm4360_strtoul(s, NULL, 0);
		if (!bcm4360_chipmatch(vendor, device))
			return E_CHIPMATCH;
	}
	U16(hw, HW_DEVICEID) = device;
	U16(hw, HW_VENDID) = vendor;

	/* step 3: current band pointers */
	P(hw, HW_BAND) = P(hw, HW_BANDSTATE(bcm4360_is_singleband_5g(device) ? BAND_5G : BAND_2G));
	P(wlc, WLC_BAND) = P(wlc, WLC_BANDSTATE(bcm4360_is_singleband_5g(U16(hw, HW_DEVICEID)) ?
					       BAND_5G : BAND_2G));

	/* step 4: the core */
	regs = bcm4360_attach_env_si_setcore(sih, D11_CORE_ID, 0);
	P(hw, HW_REGS) = regs;
	corerev = bcm4360_attach_env_si_corerev(sih);
	U32(hw, HW_COREREV) = corerev;
	bcm4360_tunables_override(P(pub, PUB_TUNABLES), device, corerev);
	P(wlc, WLC_REGS) = regs;

	/* step 5: refused chips and revisions (and, here, revisions below 40) */
	chip = U32(sih, SIH_CHIP);
	if (chip == 0x4306 && U32(sih, SIH_CHIPREV) > 2 && (device == 0x4324 || device == 0x4321) &&
	    U32(sih, SIH_CHIPPKG) != 1)
		return E_UNSUPPORTED;
	if (chip == 0x4311 && U32(sih, SIH_CHIPREV) == 0)
		return E_UNSUPPORTED;
	if (corerev >= 64 ||
	    !((corerev < 32 ? COREREV_OK_LOW >> corerev : COREREV_OK_HIGH >> (corerev - 32)) & 1))
		return E_UNSUPPORTED;
	if (corerev < 40)
		return E_UNSUPPORTED;

	/* step 6: first contact */
	bcm4360_attach_env_si_clkctl_init(sih);
	bcm4360_attach_env_si_pcie_ltr_war(sih);
	if (chip == 0x4350 && (U32(sih, SIH_CHIPST) & 0x700000) == 0x300000)
		bcm4360_attach_env_si_pcieltrenable(sih, 1, 1);
	if (U32(sih, SIH_BUSTYPE) == BUS_PCI && U32(sih, SIH_BUSCORETYPE) == PCIE2_CORE_ID &&
	    U32(sih, SIH_BUSCOREREV) <= 4)
		bcm4360_attach_env_si_pcieobffenable(sih, 1, 0);
	bcm4360_attach_env_clkctl_clk(hw, 0);
	bcm4360_attach_env_corereset(hw, 0xffffffff);
	if (!bcm4360_attach_env_validate_chip_access(hw))
		return E_CHIP_ACCESS;

	/* step 7: board revision */
	r = bcm4360_attach_env_getintvar(vars, "boardrev");
	if (r == 0xff)
		r = 1;
	boardrev = (u16)r;
	U16(hw, HW_BOARDREV) = boardrev;
	if (U32(sih, SIH_BOARDVENDOR) == VENDOR_BROADCOM) {
		boardtype = U32(sih, SIH_BOARDTYPE);
		if ((boardtype == 0x417 || boardtype == 0x418) && boardrev <= 0x3f)
			return E_BOARDREV;
		if (boardtype == 0x40c)
			return E_BOARDREV;
		if (boardtype == 0x421 && boardrev <= 0x50)
			return E_BOARDREV;
	}
	if (!boardrev_ok(boardrev))
		return E_BOARDREV;

	/* step 8: board variables */
	U8(hw, HW_SROMREV) = (u8)bcm4360_attach_env_getintvar(vars, "sromrev");
	U32(hw, HW_BOARDFLAGS) = (u32)bcm4360_attach_env_getintvar(vars, "boardflags");
	U32(hw, HW_BOARDFLAGS2) = (u32)bcm4360_attach_env_getintvar(vars, "boardflags2");
	U8(hw, HW_ANTSWCTL2G) = (u8)bcm4360_attach_env_getintvar(vars, "antswctl2g");
	U8(hw, HW_ANTSWCTL5G) = (u8)bcm4360_attach_env_getintvar(vars, "antswctl5g");

	/* step 9: Apple fix-ups */
	if (U32(sih, SIH_BOARDVENDOR) == VENDOR_APPLE) {
		boardtype = U32(sih, SIH_BOARDTYPE);
		if (boardtype == 0x4e) {
			if (boardrev > 0x40)
				U32(hw, HW_BOARDFLAGS) |= 0x00000002;
		} else if ((boardtype == 0xe4 && boardrev <= 0x1500) ||
			   (boardtype == 0xef && boardrev <= 0x1201)) {
			U32(hw, HW_BOARDFLAGS) |= 0x00400000;
			U32(hw, HW_BOARDFLAGS2) = 0;
		}
	}

	/* step 10: PLL request, band count, copies */
	if (corerev <= 4 || (U32(hw, HW_BOARDFLAGS) & 0x20))
		bcm4360_attach_env_pllreq(hw, true, 1);
	if (U32(sih, SIH_BUSTYPE) == BUS_PCI && bcm4360_attach_env_si_pci_war16165(sih))
		U8(wlc, WLC_PCI_WAR) = 1;
	nbands = IN_LIST(dualband_ids, U16(hw, HW_DEVICEID)) ? 2 : 1;
	for (i = 0; i < sizeof(singleband_chips) / sizeof(singleband_chips[0]); i++)
		if (chip == singleband_chips[i])
			nbands = 1;
	U32(hw, HW_NBANDS) = nbands;
	U16(wlc, WLC_VENDID) = U16(hw, HW_VENDID);
	U16(wlc, WLC_DEVID) = U16(hw, HW_DEVICEID);
	P(pub, PUB_SIH) = sih;
	U32(pub, PUB_COREREV) = corerev;
	U8(pub, PUB_SROMREV) = U8(hw, HW_SROMREV);
	U16(pub, PUB_BOARDREV) = U16(hw, HW_BOARDREV);
	U32(pub, PUB_BOARDFLAGS) = U32(hw, HW_BOARDFLAGS);
	U32(pub, PUB_BOARDFLAGS2) = U32(hw, HW_BOARDFLAGS2);
	U32(pub, PUB_NBANDS) = nbands;

	/* step 11: PHY shim */
	P(hw, HW_PHYSHIM) = bcm4360_phy_shim_attach(hw, P(wlc, WLC_WL), wlc);
	if (!P(hw, HW_PHYSHIM))
		return E_SHIM;

	/* step 12: shared PHY state */
	P(desc, DESC_OSH) = osh;
	P(desc, DESC_SIH) = P(hw, HW_SIH);
	P(desc, DESC_PHYSHIM) = P(hw, HW_PHYSHIM);
	U32(desc, DESC_UNIT) = unit;
	U32(desc, DESC_COREREV) = corerev;
	U32(desc, DESC_BUSTYPE) = U32(sih, SIH_BUSTYPE);
	U32(desc, DESC_BUSCOREREV) = U32(sih, SIH_BUSCOREREV);
	P(desc, DESC_VARS) = P(hw, HW_VARS);
	U16(desc, DESC_VID) = U16(hw, HW_VENDID);
	U16(desc, DESC_DID) = U16(hw, HW_DEVICEID);
	U32(desc, DESC_CHIP) = chip;
	U32(desc, DESC_CHIPREV) = U32(sih, SIH_CHIPREV);
	U32(desc, DESC_CHIPPKG) = U32(sih, SIH_CHIPPKG);
	U32(desc, DESC_SROMREV) = U8(hw, HW_SROMREV);
	U32(desc, DESC_BOARDTYPE) = U32(sih, SIH_BOARDTYPE);
	U32(desc, DESC_BOARDREV) = U16(hw, HW_BOARDREV);
	U32(desc, DESC_BOARDVENDOR) = U32(sih, SIH_BOARDVENDOR);
	U32(desc, DESC_BOARDFLAGS) = U32(hw, HW_BOARDFLAGS);
	U32(desc, DESC_BOARDFLAGS2) = U32(hw, HW_BOARDFLAGS2);
	P(hw, HW_PHY_SH) = bcm4360_attach_env_phy_shared_attach(desc);
	if (!P(hw, HW_PHY_SH))
		return E_PHY_SHARED;
	U32(hw, HW_INTRCVLAZY) = 0x01000000;
	U32(hw, HW_PCIE2_WAR_FREQ) = 0x1fe;
	e = corerev >= 40 ? 0x28 : 0x26;
	U32(wlc, WLC_RXOFFSET) = e;
	U32(wlc, WLC_RXOFFSET_ALIGN) = (e & 2) ? e : e + 2;

	/* step 13: per band */
	for (i = 0; i < nbands; i++) {
		if (bcm4360_is_singleband_5g(U16(hw, HW_DEVICEID)))
			i = BAND_5G;
		e = attach_band(wlc, hw, i);
		if (e)
			return e;
	}

	/* step 14: after the bands */
	if (chip == 0x4331 && bcm4360_attach_env_si_pcie_get_request_size(sih) > 0x80) {
		for (i = 0; i < NFIFO; i++)
			if (P(hw, HW_DI(i)))
				bcm4360_attach_env_dma_ctrlflags(P(hw, HW_DI(i)), 0x20, 0x20);
		U8(wlc, WLC_4331_REQSIZE) = 1;
	}
	bcm4360_attach_env_btc_wire_set(hw, 0);
	U8(P(hw, HW_BTC), BTC_AA2G) = (u8)bcm4360_attach_env_getintvar(vars, "aa2g");
	v = P(hw, HW_BAND);
	if (U16(v, BS_PHYTYPE) == 6)
		m = 1;
	else if (U32(v, BS_PHYTYPE) == 0x0003000a)
		m = 5;
	else
		m = 8;
	bcm4360_attach_env_btc_mode_set(hw, m);
	bcm4360_attach_env_coredisable(hw);
	if (chip == 0x4352 || chip == 0x4360 || chip == 0xaa06)
		bcm4360_attach_env_si_pmu_rfldo(sih, false);
	if (U32(sih, SIH_BUSTYPE) == BUS_PCI)
		bcm4360_attach_env_si_pci_down(sih);
	bcm4360_attach_env_si_register_intr_callback(sih, bcm4360_bmac_wlintrsoff,
						     bcm4360_bmac_wlintrsrestore, hw);
	bcm4360_attach_env_xtal(hw, false);

	/* step 15: MAC address and the rest */
	s = bcm4360_attach_env_getvar(P(hw, HW_VARS), "macaddr");
	if (!s)
		return E_NO_MACADDR;
	ea = ADDR(hw, HW_ETHERADDR);
	bcm4360_ether_atoe(s, ea);
	all0 = allff = true;
	for (i = 0; i < 6; i++) {
		if (ea[i] != 0)
			all0 = false;
		if (ea[i] != 0xff)
			allff = false;
	}
	if (all0 || allff)
		return E_BAD_MACADDR;
	P(hw, HW_LED) = bcm4360_attach_env_led_attach(hw);
	if (!P(hw, HW_LED))
		return E_LED;
	bcm4360_template_cfg_init(wlc, corerev);
	return 0;
}

/* --- wlc_bmac_detach --- */

s32 bcm4360_bmac_detach(void *wlc)
{
	void *hw = P(wlc, WLC_HW), *sih, *b;
	u32 f, k, nbands;
	s32 r = 0;

	if (!hw)
		return 0;
	sih = P(hw, HW_SIH);
	if (sih) {
		bcm4360_attach_env_si_deregister_intr_callback(sih);
		if (U32(sih, SIH_BUSTYPE) == BUS_PCI)
			bcm4360_attach_env_si_pci_sleep(sih);
	}
	for (f = 0; f < NFIFO; f++) {
		if (P(hw, HW_DI(f))) {
			bcm4360_attach_env_dma_detach(P(hw, HW_DI(f)));
			bcm4360_hw_set_di(hw, f, NULL);
		}
	}
	b = P(hw, HW_BAND);
	nbands = U32(hw, HW_NBANDS);
	for (k = 0; k < nbands; k++) {
		if (bcm4360_is_singleband_5g(U16(hw, HW_DEVICEID)))
			k = 1;
		if (b && P(b, BS_PI)) {
			bcm4360_attach_env_phy_detach(P(b, BS_PI));
			P(b, BS_PI) = NULL;
		}
		/* the band other than the upper layer's current one */
		b = P(hw, HW_BANDSTATE(U32(P(wlc, WLC_BAND), UBS_BANDUNIT) == 1 ? 0 : 1));
	}
	bcm4360_attach_env_phy_shared_detach(P(hw, HW_PHY_SH));
	bcm4360_phy_shim_detach(P(hw, HW_PHYSHIM));
	P(hw, HW_VARS) = NULL;
	P(hw, HW_SIH) = NULL;
	if (P(hw, HW_LED)) {
		r = bcm4360_attach_env_led_detach(hw);
		P(hw, HW_LED) = NULL;
	}
	bcm4360_hw_detach(hw);
	P(wlc, WLC_HW) = NULL;
	return r;
}
