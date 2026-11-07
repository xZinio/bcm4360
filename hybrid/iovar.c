/*
 * Hybrid build: Broadcom's iovar dispatcher (wlc_iovar_op, wlc.c) by the open
 * code of open/lib/iovar.c (docs/re/spec/iovar.md). Switch open_iovar, read
 * at every call.
 *
 * The module table, the request check (wlc_iovar_check), the BSS
 * configurations (wlc_bsscfg_find) and the modules' handlers stay Broadcom's:
 * the services below reach them at the offsets of the specification
 * ("Scope", "Fields updated after a set").
 */
#include "hybrid.h"
#include <bcm4360/iovar.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_iovar = true;

#ifdef __KERNEL__
module_param_named(open_iovar, bcm4360_open_iovar, bool, 0444);
MODULE_PARM_DESC(open_iovar, "lookup and dispatch of driver variables (iovars): open code (1, default) or Broadcom's (0)");
#endif

/* iovar.md, Scope and "Fields updated after a set" (offsets) */
#define WLC_MODULECB		0x358	/* the module table (pointer to 64 slots) */
#define MODULECB_SIZE		0x50
#define MODULECB_IOVARS		0x20	/* const bcm_iovar_t * */
#define MODULECB_HDL		0x28	/* void * */
#define MODULECB_IOVAR_FN	0x38	/* the handler */
#define BSSCFG_WLCIF		0x10
#define WLC_TXC_VALID		0x2a3
#define WLC_TXC_ENAB		0x2a4
#define WLC_TXC_GEN		0x2a8

typedef s32 (*hyb_iovar_fn)(void *hdl, const struct bcm4360_iovar *vi, u32 actionid,
			    const char *name, void *params, s32 p_len, void *arg, s32 len,
			    s32 val_size, void *wlcif);

/* the object's functions the services call (made global: takeover.txt `use`) */
s32 wlc_iovar_check(void *wlc, const struct bcm4360_iovar *vi, void *arg, s32 len, u8 set,
		    void *wlcif);
void *wlc_bsscfg_find(void *wlc, s32 idx, s32 *err);

HYB_TAKE(s32, wlc_iovar_op, (void *wlc, const char *name, void *params, s32 p_len, void *arg,
			     s32 len, u8 set, void *wlcif));

static inline u8 *hyb_slot(void *wlc, u32 slot)
{
	return *(u8 **)((u8 *)wlc + WLC_MODULECB) + slot * MODULECB_SIZE;
}

s32 wlc_iovar_op(void *wlc, const char *name, void *params, s32 p_len, void *arg, s32 len,
		 u8 set, void *wlcif)
{
	struct bcm4360_iovar_wlc w;

	if (!bcm4360_open_iovar)
		return bcm4360_blob_wlc_iovar_op(wlc, name, params, p_len, arg, len, set, wlcif);
	w.wlc = wlc;
	w.txc_valid = (u8 *)wlc + WLC_TXC_VALID;
	w.txc_enab = (const u8 *)wlc + WLC_TXC_ENAB;
	w.txc_gen = (u32 *)((u8 *)wlc + WLC_TXC_GEN);
	return bcm4360_iovar_op(&w, name, params, p_len, arg, len, set != 0, wlcif);
}

const struct bcm4360_iovar *bcm4360_iovar_env_table(void *wlc, u32 slot)
{
	return *(const struct bcm4360_iovar **)(hyb_slot(wlc, slot) + MODULECB_IOVARS);
}

void *bcm4360_iovar_env_bsscfg_find(void *wlc, s32 idx, s32 *err)
{
	return wlc_bsscfg_find(wlc, idx, err);
}

void *bcm4360_iovar_env_bsscfg_wlcif(void *wlc, void *bsscfg)
{
	(void)wlc;
	return *(void **)((u8 *)bsscfg + BSSCFG_WLCIF);
}

s32 bcm4360_iovar_env_check(void *wlc, const struct bcm4360_iovar *vi, void *arg, s32 len,
			    bool set, void *wlcif)
{
	return wlc_iovar_check(wlc, vi, arg, len, set, wlcif);
}

s32 bcm4360_iovar_env_handler(void *wlc, u32 slot, const struct bcm4360_iovar *vi, u32 actionid,
			      const char *name, void *params, s32 p_len, void *arg, s32 len,
			      s32 val_size, void *wlcif)
{
	u8 *m = hyb_slot(wlc, slot);
	hyb_iovar_fn fn = *(hyb_iovar_fn *)(m + MODULECB_IOVAR_FN);

	return fn(*(void **)(m + MODULECB_HDL), vi, actionid, name, params, p_len, arg, len,
		  val_size, wlcif);
}
