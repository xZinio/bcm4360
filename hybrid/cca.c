/*
 * Hybrid build: Broadcom's channel statistics module (wlc_cca.c) by the open
 * code of open/lib/cca.c (docs/re/spec/cca-stf.md, part 1). Switch open_cca:
 * read when the module is attached (wlc_cca_attach); the other entry points
 * follow what attach chose, because the state is either Broadcom's or the
 * open code's.
 *
 * The open attach registers the open module functions (watchdog, down, iovar
 * handler) in the module table; the rest of the object only stores and tests
 * the pointer wlc+0x628. The services read the fields of `wlc` at the
 * offsets of the specification.
 */
#include "hybrid.h"
#include <bcm4360/cca.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_cca = true;

#ifdef __KERNEL__
module_param_named(open_cca, bcm4360_open_cca, bool, 0444);
MODULE_PARM_DESC(open_cca, "channel statistics (cca_get_stats): open code (1, default) or Broadcom's (0)");
#endif

/* cca-stf.md, Data: "Fields of wlc, pub and the band state read here" */
#define WLC_PUB			0x000
#define WLC_OSH			0x008
#define WLC_HW			0x020
#define WLC_PRIMARY_BSSCFG	0x2f8
#define BSSCFG_PM		0x338
#define PM_ENABLED		0x8
#define WLC_CHANSPEC		0x518
#define WLC_CCA			0x628
#define PUB_UNIT		0x010
#define PUB_CORENEV		0x014

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

/* the object's functions the services call (takeover.txt: take or use) */
s32 wlc_bmac_cca_stats_read(void *wlc_hw, u32 *v);
void wlc_read_tsf(void *wlc, u32 *lo, u32 *hi);
s32 wlc_module_register(void *pub, const struct bcm4360_iovar *iovars, const char *name, void *hdl,
			bcm4360_cca_iovar_fn iovar_fn, bcm4360_cca_fn watchdog_fn, bcm4360_cca_fn up_fn,
			bcm4360_cca_fn down_fn);
s32 wlc_module_unregister(void *pub, const char *name, void *hdl);

HYB_TAKE(void *, wlc_cca_attach, (void *wlc));
HYB_TAKE(void, wlc_cca_detach, (void *cca));
HYB_TAKE(void, cca_stats_upd, (void *wlc, s32 calc));
HYB_TAKE(void, cca_stats_tsf_upd, (void *wlc));
HYB_TAKE(s32, cca_query_stats, (void *wlc, u32 cs, s32 n, void *out, u32 size));
HYB_TAKE(u16, wlc_cca_get_chanspec, (void *wlc, s32 i));

/*
 * Which side owns the state of the attached module. One driver instance per
 * card, and the switch cannot change while the module is loaded; the
 * variable only makes the other entry points follow attach.
 */
static bool hyb_cca_open = true;

static struct bcm4360_cca *hyb_cca(void *wlc)
{
	return FIELD(wlc, WLC_CCA, struct bcm4360_cca *);
}

void *wlc_cca_attach(void *wlc)
{
	void *pub = FIELD(wlc, WLC_PUB, void *);

	hyb_cca_open = bcm4360_open_cca;
	if (!hyb_cca_open)
		return bcm4360_blob_wlc_cca_attach(wlc);
	return bcm4360_cca_attach(wlc, FIELD(wlc, WLC_OSH, void *), FIELD(pub, PUB_UNIT, u32),
				  FIELD(pub, PUB_CORENEV, u32));
}

void wlc_cca_detach(void *cca)
{
	if (!hyb_cca_open) {
		bcm4360_blob_wlc_cca_detach(cca);
		return;
	}
	bcm4360_cca_detach(cca);
}

void cca_stats_upd(void *wlc, s32 calc)
{
	if (!hyb_cca_open) {
		bcm4360_blob_cca_stats_upd(wlc, calc);
		return;
	}
	bcm4360_cca_stats_upd(hyb_cca(wlc), calc);
}

void cca_stats_tsf_upd(void *wlc)
{
	if (!hyb_cca_open) {
		bcm4360_blob_cca_stats_tsf_upd(wlc);
		return;
	}
	bcm4360_cca_stats_tsf_upd(hyb_cca(wlc));
}

s32 cca_query_stats(void *wlc, u32 cs, s32 n, void *out, u32 size)
{
	if (!hyb_cca_open)
		return bcm4360_blob_cca_query_stats(wlc, cs, n, out, size);
	return bcm4360_cca_query_stats(hyb_cca(wlc), (u16)cs, n, out, size);
}

u16 wlc_cca_get_chanspec(void *wlc, s32 i)
{
	if (!hyb_cca_open)
		return bcm4360_blob_wlc_cca_get_chanspec(wlc, i);
	return bcm4360_cca_get_chanspec(hyb_cca(wlc), i);
}

void *bcm4360_cca_env_osh(void *wlc)
{
	return FIELD(wlc, WLC_OSH, void *);
}

u16 bcm4360_cca_env_chanspec(void *wlc)
{
	return FIELD(wlc, WLC_CHANSPEC, u16);
}

s32 bcm4360_cca_env_stats_read(void *wlc, u32 v[9])
{
	return wlc_bmac_cca_stats_read(FIELD(wlc, WLC_HW, void *), v);
}

bool bcm4360_cca_env_pm_enabled(void *wlc)
{
	u8 *cfg = FIELD(wlc, WLC_PRIMARY_BSSCFG, u8 *);
	u8 *pm = FIELD(cfg, BSSCFG_PM, u8 *);

	return pm[PM_ENABLED] != 0;
}

void bcm4360_cca_env_read_tsf(void *wlc, u32 *lo, u32 *hi)
{
	wlc_read_tsf(wlc, lo, hi);
}

s32 bcm4360_cca_env_module_register(void *wlc, const struct bcm4360_iovar *iovars,
				    const char *name, void *hdl, bcm4360_cca_iovar_fn doiovar,
				    bcm4360_cca_fn watchdog, bcm4360_cca_fn up, bcm4360_cca_fn down)
{
	return wlc_module_register(FIELD(wlc, WLC_PUB, void *), iovars, name, hdl, doiovar, watchdog,
				   up, down);
}

s32 bcm4360_cca_env_module_unregister(void *wlc, const char *name, void *hdl)
{
	return wlc_module_unregister(FIELD(wlc, WLC_PUB, void *), name, hdl);
}
