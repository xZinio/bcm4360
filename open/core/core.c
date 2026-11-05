// SPDX-License-Identifier: ISC
/*
 * The core of the open driver: attach, up, down, channel, periodic work,
 * interrupts (docs/re/07-open-driver.md, milestone 1). The calls into the open
 * layers are made in the order of Broadcom's wlc_attach / wlc_up / wlc_down
 * as bmac-init.md C1 describes them; what the upper layer adds around them
 * and that no specification covers yet is marked `wlc-hw:` (the spec
 * docs/re/spec/wlc-hw.md is still being written).
 */
#include "core.h"
#include <bcm4360/bmac_attach.h>
#include <bcm4360/phy_api.h>

#define TICK_MS		1000

void *core_alloc(struct bcm4360_core *c, u32 size)
{
	(void)c;
	return hw_zalloc(size);
}

static void free_blocks(struct bcm4360_core *c)
{
	if (c->pubcnt)
		hw_free(c->pubcnt, PUBCNT_SIZE);
	if (c->pm)
		hw_free(c->pm, PM_SIZE);
	if (c->bsscfg)
		hw_free(c->bsscfg, BSSCFG_SIZE);
	if (c->coex)
		hw_free(c->coex, COEX_SIZE);
	if (c->stf_state)
		hw_free(c->stf_state, STF_STATE_SIZE);
	if (c->uband[1])
		hw_free(c->uband[1], UBAND_SIZE);
	if (c->uband[0])
		hw_free(c->uband[0], UBAND_SIZE);
	if (c->wlc_core)
		hw_free(c->wlc_core, WLC_CORESTATE_SIZE);
	if (c->tunables)
		hw_free(c->tunables, TUNABLES_N * 4);
	if (c->pub)
		hw_free(c->pub, PUB_SIZE);
	if (c->wlc)
		hw_free(c->wlc, WLC_SIZE);
	hw_free(c, sizeof(*c));
}

/* the blocks of Broadcom's layout, with the defaults wlc_attach gives them */
static bool alloc_blocks(struct bcm4360_core *c)
{
	c->wlc = core_alloc(c, WLC_SIZE);
	c->pub = core_alloc(c, PUB_SIZE);
	c->tunables = core_alloc(c, TUNABLES_N * 4);
	c->wlc_core = core_alloc(c, WLC_CORESTATE_SIZE);
	c->uband[0] = core_alloc(c, UBAND_SIZE);
	c->uband[1] = core_alloc(c, UBAND_SIZE);
	c->stf_state = core_alloc(c, STF_STATE_SIZE);
	c->coex = core_alloc(c, COEX_SIZE);
	c->bsscfg = core_alloc(c, BSSCFG_SIZE);
	c->pm = core_alloc(c, PM_SIZE);
	c->pubcnt = core_alloc(c, PUBCNT_SIZE);
	if (!c->wlc || !c->pub || !c->tunables || !c->wlc_core || !c->uband[0] || !c->uband[1] ||
	    !c->stf_state || !c->coex || !c->bsscfg || !c->pm || !c->pubcnt)
		return false;

	/* tunables (bmac-data.md D7): ntxd, nrxd, rxbufsize, nrxbufpost, rxbnd */
	c->tunables[0] = 256;
	c->tunables[1] = 256;
	c->tunables[2] = 2048;
	c->tunables[3] = 32;
	c->tunables[14] = 16;

	FIELD(c->wlc, WLC_PUB, void *) = c->pub;
	FIELD(c->wlc, WLC_OSH, void *) = c->osh;
	FIELD(c->wlc, WLC_WL, void *) = c;
	FIELD(c->wlc, WLC_CORE, void *) = c->wlc_core;
	FIELD(c->wlc, WLC_BANDSTATE(0), void *) = c->uband[0];
	FIELD(c->wlc, WLC_BANDSTATE(1), void *) = c->uband[1];
	FIELD(c->wlc, WLC_STF, void *) = c->stf_state;
	FIELD(c->wlc, WLC_COEX, void *) = c->coex;
	FIELD(c->wlc, WLC_PRIMARY_BSSCFG, void *) = c->bsscfg;
	FIELD(c->bsscfg, 0x338, void *) = c->pm;
	FIELD(c->wlc, WLC_CORE_PTR, struct bcm4360_core *) = c;
	FIELD(c->wlc, 0x6bc, u32) = 0x26;	/* before the attach: wlc_attach's default */
	FIELD(c->wlc, WLC_CHANSPEC, u16) = 0x1001;
	FIELD(c->pub, PUB_TUNABLES, void *) = c->tunables;
	FIELD(c->pub, PUB_CNT, void *) = c->pubcnt;
	return true;
}

static void fill_info(struct bcm4360_core *c)
{
	struct bcm4360_core_info *i = &c->info;
	const char *s;
	u32 nbands = FIELD(CORE_WLC_HW(c), 0x118, u32);

	hw_memcpy(i->etheraddr, (u8 *)CORE_WLC_HW(c) + 0x178, 6);
	i->bands = nbands == 2 ? 3 : (c->device == 0x43a2 || c->device == 0x43b3 ? 2 : 1);
	i->txchain = i->rxchain = 7;	/* wlc-hw: from the PHY's chains */
	i->chip_id = c->sihp.chip;
	i->chip_rev = c->sihp.chiprev;
	i->boardvendor = c->sihp.boardvendor;
	i->boardtype = c->sihp.boardtype;
	s = core_getvar(c->vars, "ccode");
	hw_memset(i->ccode, 0, sizeof(i->ccode));
	if (s) {
		i->ccode[0] = s[0];
		if (s[0])
			i->ccode[1] = s[1];
	}
}

/* Broadcom's watchdog (wlc_watchdog), once a second while up; wlc-hw: the rest of it */
static void core_watchdog(void *arg)
{
	struct bcm4360_core *c = arg;

	if (!c->up || !c->phy)
		return;
	bcm4360_phy_watchdog(c->phy);
}

struct bcm4360_core *bcm4360_core_attach(struct bcm4360_hw *hw, void *osh, u16 vendor,
					 u16 device, void *plat, int *err)
{
	struct bcm4360_core *c;
	void *sih;
	u32 e = 0;

	c = hw_zalloc(sizeof(*c));
	if (!c) {
		*err = -12;
		return NULL;
	}
	hw->owner = c;
	c->hw = hw;
	c->osh = osh;
	c->plat = plat;
	c->vendor = vendor;
	c->device = device;
	if (!alloc_blocks(c)) {
		*err = -12;
		free_blocks(c);
		return NULL;
	}

	/* wlc_bmac_si_attach (bmac-attach.md, Overview) */
	sih = core_si_attach(c, device, osh, hw->bar0, 1, &c->vars, &c->varsz);
	if (!sih) {
		*err = -19;
		free_blocks(c);
		return NULL;
	}
	FIELD(c->pub, PUB_SIH, void *) = sih;
	FIELD(c->pub, PUB_VARS, void *) = c->vars;
	FIELD(c->pub, PUB_VARS_SIZE, u32) = c->varsz;

	/* wlc-hw: the rest of wlc_attach (modules, stf and channel manager defaults, country) */
	e = bcm4360_bmac_attach(c->wlc, vendor, device, 0, 0, osh, hw->bar0, 1, NULL);
	if (e) {
		*err = -(int)e;
		core_si_detach(c);
		free_blocks(c);
		return NULL;
	}
	hw_memcpy((u8 *)c->pub + PUB_ETHERADDR, (u8 *)CORE_WLC_HW(c) + 0x178, 6);
	c->cca = bcm4360_cca_attach(c->wlc, osh, 0, FIELD(c->pub, PUB_COREREV, u32));
	FIELD(c->wlc, WLC_CCA, void *) = c->cca;
	c->wd_timer = bcm4360_plat_timer_init(plat, core_watchdog, c, "watchdog");
	c->attached = true;
	c->chanspec = 0x1001;
	fill_info(c);
	*err = 0;
	return c;
}

void bcm4360_core_detach(struct bcm4360_core *c)
{
	if (!c)
		return;
	if (c->wd_timer) {
		bcm4360_plat_timer_del(c->plat, c->wd_timer);
		bcm4360_plat_timer_free(c->plat, c->wd_timer);
	}
	if (c->cca)
		bcm4360_cca_detach(c->cca);
	if (c->wlc)
		bcm4360_bmac_detach(c->wlc);
	core_si_detach(c);
	c->hw->owner = NULL;
	free_blocks(c);
}

void bcm4360_core_get_info(struct bcm4360_core *c, struct bcm4360_core_info *info)
{
	*info = c->info;
}

/* wl_init: wl_reset (wl_intrsoff, wlc_reset, wl_intrsrestore), then wlc_init (C1) */
static void core_init(struct bcm4360_core *c, u16 chanspec)
{
	u32 m = core_intrsoff(c);
	u8 ea[6];

	CORE_MAC_CALL(c, bcm4360_bmac_reset(m));
	core_intrsrestore(c, m);

	/* wlc_init: wlc-hw: choice of the channel, SHM reads, keys, BSSID, rate sets */
	CORE_MAC_CALL(c, bcm4360_bmac_init(m, chanspec, false));
	hw_memcpy(ea, (u8 *)c->pub + PUB_ETHERADDR, 6);
	CORE_MAC_CALL(c, bcm4360_bmac_set_hw_etheraddr(m, ea));
	/* wlc-hw: wlc_set_phy_chanspec: the power limits of the channel (channel-power) */
	CORE_MAC_CALL(c, bcm4360_bmac_set_chanspec(m, chanspec, false, NULL));
	FIELD(c->wlc, WLC_CHANSPEC, u16) = chanspec;
	CORE_MAC_CALL(c, bcm4360_bmac_enable(m));
	/* wlc-hw: sub_03a14c (MAC control bits for the operating mode) */
	d11_write32(c->hw, 0x3dc, 10000000);
}

/* wlc_up (bmac-init.md C1) */
int bcm4360_core_up(struct bcm4360_core *c, u16 chanspec)
{
	s32 r = 0;
	bool gone = false;

	if (!c->attached)
		return -19;
	CORE_MAC_CALL(c, gone = bcm4360_bmac_deviceremoved(m));
	if (gone)
		return -9;
	if (!FIELD(c->pub, PUB_HW_UP, u8)) {
		/* wlc-hw: wlc_led_init */
		CORE_MAC_CALL(c, bcm4360_bmac_hw_up(m));
		FIELD(c->pub, PUB_HW_UP, u8) = 1;
	}
	/* wlc_bmac_4331_epa_init: nothing for the 4360 */
	CORE_MAC_CALL(c, bcm4360_bmac_mhf(m, 4, 0x0080, FIELD(c->stf_state, 0x59, u8) ? 0x0080 : 0,
					  3));
	CORE_MAC_CALL(c, r = bcm4360_bmac_up_prep(m));
	if (r)
		return r;
	/* wlc_bmac_set_ctrl_ePA: nothing for the 4360 */
	if (FIELD(CORE_WLC_HW(c), 0x1bc, u8) == 0xff) {
		CORE_MAC_CALL(c, bcm4360_bmac_set_ctrl_bt_shd0(m, true));
	} else {
		CORE_MAC_CALL(c, bcm4360_bmac_set_btswitch(m, FIELD(CORE_WLC_HW(c), 0x1bc, s8)));
	}
	/* wlc-hw: WME is enabled by default (pub+0x54) */
	CORE_MAC_CALL(c, bcm4360_bmac_mhf(m, 0, 0x0100, 0x0100, 3));
	if (FIELD(c->wlc, 0x60, u8))
		CORE_MAC_CALL(c, bcm4360_bmac_mhf(m, 1, 0x0008, 0x0008, 3));
	CORE_MAC_CALL(c, bcm4360_bmac_seci_upd(m));
	core_init(c, chanspec);
	FIELD(c->pub, PUB_UP, u8) = 1;
	/* wlc-hw: wlc_enable_probe_req, wlc_rfaware_lifetime_set */
	CORE_MAC_CALL(c, r = bcm4360_bmac_up_finish(m));
	c->up = true;
	c->chanspec = chanspec;
	if (c->wd_timer)
		bcm4360_plat_timer_add(c->plat, c->wd_timer, TICK_MS, true);
	/* wlc-hw: LEDs */
	return r;
}

/* wlc_down (bmac-init.md C1) */
void bcm4360_core_down(struct bcm4360_core *c)
{
	if (!c->attached || !c->up)
		return;
	CORE_MAC_CALL(c, bcm4360_bmac_down_prep(m));
	/* wlc-hw: scan and association aborted, the modules' down functions */
	if (c->wd_timer)
		bcm4360_plat_timer_del(c->plat, c->wd_timer);
	FIELD(c->pub, PUB_UP, u8) = 0;
	c->up = false;
	if (c->phy)
		bcm4360_phy_mute_upd(c->phy, false, 0xffffffff);
	/* wlc-hw: LEDs, queues flushed */
	CORE_MAC_CALL(c, bcm4360_bmac_down_finish(m));
}

/* wlc_set_chanspec; wlc-hw / channel-power: the power limits of the channel */
int bcm4360_core_set_chanspec(struct bcm4360_core *c, u16 chanspec)
{
	if (!c->up)
		return -19;
	CORE_MAC_CALL(c, bcm4360_bmac_set_chanspec(m, chanspec, false, NULL));
	FIELD(c->wlc, WLC_CHANSPEC, u16) = chanspec;
	c->chanspec = chanspec;
	return 0;
}

void bcm4360_core_set_txpower(struct bcm4360_core *c, u32 qdbm)
{
	c->txpower_qdbm = qdbm;	/* wlc-hw: wlc_phy_txpower_set */
}

int bcm4360_core_set_country(struct bcm4360_core *c, const char *ccode)
{
	c->ccode[0] = ccode[0];
	c->ccode[1] = ccode[0] ? ccode[1] : 0;
	c->ccode[2] = 0;
	return 0;	/* channel-power: the regulatory limits */
}

void bcm4360_core_set_macaddr(struct bcm4360_core *c, const u8 *addr)
{
	hw_memcpy((u8 *)c->pub + PUB_ETHERADDR, addr, 6);
	if (c->up)
		CORE_MAC_CALL(c, bcm4360_bmac_set_hw_etheraddr(m, addr));
}

/* wlc-hw ("Joining"): BSSID, beacon interval, TSF, power save, filters, EDCA */
void bcm4360_core_set_bss(struct bcm4360_core *c, const struct bcm4360_bss *bss)
{
	(void)c; (void)bss;
}

void bcm4360_core_set_filter(struct bcm4360_core *c, u32 filter)
{
	(void)c; (void)filter;
}

void bcm4360_core_set_edca(struct bcm4360_core *c, u8 fifo, u16 cwmin, u16 cwmax, u8 aifs,
			   u16 txop)
{
	(void)c; (void)fifo; (void)cwmin; (void)cwmax; (void)aifs; (void)txop;
}

void bcm4360_core_set_scanning(struct bcm4360_core *c, bool on)
{
	(void)c; (void)on;
}

u64 bcm4360_core_get_tsf(struct bcm4360_core *c)
{
	u32 lo = 0, hi = 0;

	if (c->up)
		CORE_MAC_CALL(c, bcm4360_bmac_read_tsf(m, &lo, &hi));
	return ((u64)hi << 32) | lo;
}

