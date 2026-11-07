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
#include <bcm4360/clm.h>

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

/* wlc_watchdog, once a second while up (wlchw.c) */
static void core_watchdog(void *arg)
{
	core_w_watchdog(arg);
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

	bcm4360_plat_trace(plat, "attach: backplane (si)");
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

	bcm4360_plat_trace(plat, "attach: regulatory database");
	/* the regulatory database (clm.md, "Loading"), given by the platform */
	{
		const void *hdr = bcm4360_plat_clm(plat);

		c->clm_ok = hdr && !bcm4360_clm_init(&c->clm, hdr);
	}
	bcm4360_plat_trace(plat, "attach: MAC and PHY (bmac attach)");
	/* wlc-hw: the rest of wlc_attach (modules, stf and channel manager defaults, country) */
	e = bcm4360_bmac_attach(c->wlc, vendor, device, 0, 0, osh, hw->bar0, 1, NULL);
	if (e) {
		*err = -(int)e;
		core_si_detach(c);
		free_blocks(c);
		return NULL;
	}
	hw_memcpy((u8 *)c->pub + PUB_ETHERADDR, (u8 *)CORE_WLC_HW(c) + 0x178, 6);
	bcm4360_plat_trace(plat, "attach: driver state");
	core_w_attach(c);
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
	core_w_detach(c);
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

/* wlc_up (wlchw.c) */
int bcm4360_core_up(struct bcm4360_core *c, u16 chanspec)
{
	int r;

	if (!c->attached)
		return -19;
	r = core_w_up(c, chanspec);
	if (!r && c->up)
		c->chanspec = chanspec;
	return r;
}

/* wlc_down (wlchw.c) */
void bcm4360_core_down(struct bcm4360_core *c)
{
	if (!c->attached || !c->up)
		return;
	core_w_down(c);
}

/* the iovar `chanspec` (wlchw.c) */
int bcm4360_core_set_chanspec(struct bcm4360_core *c, u16 chanspec)
{
	return core_w_set_chanspec(c, chanspec);
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
	return core_cp_set_country(c, ccode);	/* channel-power.md 1.3 */
}

u64 bcm4360_core_get_tsf(struct bcm4360_core *c)
{
	u32 lo = 0, hi = 0;

	if (c->up)
		CORE_MAC_CALL(c, bcm4360_bmac_read_tsf(m, &lo, &hi));
	return ((u64)hi << 32) | lo;
}

