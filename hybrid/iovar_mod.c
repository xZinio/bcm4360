/*
 * Hybrid build: Broadcom's module table functions (wlc_module_register /
 * wlc_module_unregister, wlc.c) by the open code of open/lib/module.c
 * (docs/re/spec/iovar.md, "Module register and unregister"). Part of the
 * iovar layer: switch open_iovar.
 *
 * The table stays where the object keeps it (wlc+0x358, 64 slots of
 * Broadcom's layout, struct bcm4360_module); the object's dispatcher,
 * watchdog and up/down code read it.
 */
#include "hybrid.h"
#include <bcm4360/iovar.h>

#define WLC_MODULECB	0x358	/* iovar.md, Scope: the module table */

_Static_assert(sizeof(struct bcm4360_module) == 0x50, "module slot of iovar.md");

HYB_TAKE(s32, wlc_module_register, (void *pub, const struct bcm4360_iovar *iovars,
				    const char *name, void *hdl, bcm4360_module_fn iovar_fn,
				    bcm4360_module_fn watchdog_fn, bcm4360_module_fn up_fn,
				    bcm4360_module_fn down_fn));
HYB_TAKE(s32, wlc_module_unregister, (void *pub, const char *name, void *hdl));

static struct bcm4360_module *hyb_modules(void *wlc)
{
	return *(struct bcm4360_module **)((u8 *)wlc + WLC_MODULECB);
}

s32 wlc_module_register(void *pub, const struct bcm4360_iovar *iovars, const char *name,
			void *hdl, bcm4360_module_fn iovar_fn, bcm4360_module_fn watchdog_fn,
			bcm4360_module_fn up_fn, bcm4360_module_fn down_fn)
{
	if (!bcm4360_open_iovar)
		return bcm4360_blob_wlc_module_register(pub, iovars, name, hdl, iovar_fn,
							watchdog_fn, up_fn, down_fn);
	/* the first word of pub is wlc (iovar.md); register does not check it */
	return bcm4360_module_register(hyb_modules(*(void **)pub), iovars, name, hdl, iovar_fn,
				       watchdog_fn, up_fn, down_fn);
}

s32 wlc_module_unregister(void *pub, const char *name, void *hdl)
{
	void *wlc;

	if (!bcm4360_open_iovar)
		return bcm4360_blob_wlc_module_unregister(pub, name, hdl);
	wlc = *(void **)pub;
	return bcm4360_module_unregister(wlc ? hyb_modules(wlc) : NULL, name, hdl);
}
