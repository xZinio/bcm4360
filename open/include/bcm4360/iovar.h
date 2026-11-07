/*
 * The iovar dispatcher (Broadcom's wlc_iovar_op): finds a named driver
 * variable in the iovar tables of the driver's modules and hands the get or
 * set to the module's handler.
 *
 * Specification: docs/re/spec/iovar.md. The module table, the check of a
 * request, the BSS configurations and the handlers stay the caller's: they
 * are reached through the services at the end.
 */
#ifndef BCM4360_IOVAR_H
#define BCM4360_IOVAR_H

#include <bcm4360/types.h>

/*
 * iovar.md, "The iovar table entry": the layout of Broadcom's public
 * bcm_iovar_t (src/include/bcmutils.h). A table ends with an entry whose
 * name is NULL.
 */
struct bcm4360_iovar {
	const char *name;
	u16 varid;
	u16 flags;
	u16 type;
	u16 minlen;
};

#define BCM4360_IOVAR_MODULES		64	/* slots of the module table */

#define BCM4360_IOVT_VOID		0
#define BCM4360_IOVT_BUFFER		8

#define BCM4360_BCME_BUFTOOSHORT	(-14)
#define BCM4360_BCME_UNSUPPORTED	(-23)

/* iovar.md, "Fields updated after a set": the caller's fields, and its handle */
struct bcm4360_iovar_wlc {
	void *wlc;		/* the caller's handle, passed to every service */
	u8 *txc_valid;		/* wlc+0x2a3 */
	const u8 *txc_enab;	/* wlc+0x2a4 */
	u32 *txc_gen;		/* wlc+0x2a8 */
};

/* iovar.md, "Procedure: wlc_iovar_op" */
s32 bcm4360_iovar_op(const struct bcm4360_iovar_wlc *w, const char *name, void *params,
		     s32 p_len, void *arg, s32 len, bool set, void *wlcif);

/*
 * iovar.md, "Module register and unregister": a slot of the module table
 * (64 of them), the layout the caller's other code reads. The functions are
 * stored, not called, so their type does not matter here.
 */
typedef void (*bcm4360_module_fn)(void);

struct bcm4360_module {
	char name[32];
	const struct bcm4360_iovar *iovars;
	void *hdl;
	bcm4360_module_fn watchdog_fn;
	bcm4360_module_fn iovar_fn;
	bcm4360_module_fn up_fn;
	bcm4360_module_fn down_fn;
};

#define BCM4360_BCME_NORESOURCE		(-22)
#define BCM4360_BCME_NOTFOUND		(-30)

/* wlc_module_register; `table`: the 64 slots */
s32 bcm4360_module_register(struct bcm4360_module *table, const struct bcm4360_iovar *iovars,
			    const char *name, void *hdl, bcm4360_module_fn iovar_fn,
			    bcm4360_module_fn watchdog_fn, bcm4360_module_fn up_fn,
			    bcm4360_module_fn down_fn);
/* wlc_module_unregister; `table` NULL: the caller has no wlc (step 1) */
s32 bcm4360_module_unregister(struct bcm4360_module *table, const char *name, void *hdl);

/* services of the caller (iovar.md, Scope) */

/* the iovar table of module slot `slot` (0..63), NULL for an empty slot */
const struct bcm4360_iovar *bcm4360_iovar_env_table(void *wlc, u32 slot);
/* wlc_bsscfg_find: the BSS configuration `idx`; sets *err on failure */
void *bcm4360_iovar_env_bsscfg_find(void *wlc, s32 idx, s32 *err);
/* the interface of a BSS configuration */
void *bcm4360_iovar_env_bsscfg_wlcif(void *wlc, void *bsscfg);
/* wlc_iovar_check */
s32 bcm4360_iovar_env_check(void *wlc, const struct bcm4360_iovar *vi, void *arg, s32 len,
			    bool set, void *wlcif);
/* the iovar handler of module slot `slot`, called with the module's handle */
s32 bcm4360_iovar_env_handler(void *wlc, u32 slot, const struct bcm4360_iovar *vi, u32 actionid,
			      const char *name, void *params, s32 p_len, void *arg, s32 len,
			      s32 val_size, void *wlcif);

#endif
