// SPDX-License-Identifier: ISC
/*
 * The module table of the driver (Broadcom's wlc_module_register /
 * wlc_module_unregister, wlc.c): the modules enter themselves in it when they
 * are attached and leave it when they are detached.
 *
 * Written from docs/re/spec/iovar.md, "Module register and unregister". The
 * 64 slots are the caller's (struct bcm4360_module); the functions given to
 * register are only stored.
 */
#include <bcm4360/iovar.h>
#include <bcm4360/hw.h>

/*
 * docs/re/spec/iovar.md, "Procedure: wlc_module_register", step 2: the name
 * is copied with at most 31 bytes; byte 31 of the slot's name is not written
 */
#define MODULE_NAME_COPY	(sizeof(((struct bcm4360_module *)0)->name) - 1)

/* "The module slot": a first byte 0 of the name marks a free slot */
#define MODULE_SLOT_FREE	'\0'

/* docs/re/spec/iovar.md, "Procedure: wlc_module_register" */
s32 bcm4360_module_register(struct bcm4360_module *table, const struct bcm4360_iovar *iovars,
			    const char *name, void *hdl, bcm4360_module_fn iovar_fn,
			    bcm4360_module_fn watchdog_fn, bcm4360_module_fn up_fn,
			    bcm4360_module_fn down_fn)
{
	struct bcm4360_module *m;
	u32 i;

	for (i = 0; i < BCM4360_IOVAR_MODULES; i++) {
		m = &table[i];
		if (m->name[0] != MODULE_SLOT_FREE)
			continue;
		hw_strncpy(m->name, name, MODULE_NAME_COPY);
		m->iovars = iovars;
		m->hdl = hdl;
		m->iovar_fn = iovar_fn;
		m->watchdog_fn = watchdog_fn;
		m->up_fn = up_fn;
		m->down_fn = down_fn;
		return 0;
	}
	return BCM4360_BCME_NORESOURCE;
}

/*
 * docs/re/spec/iovar.md, "Procedure: wlc_module_unregister": the first slot
 * (free slots too) with this name and handle is zeroed.
 */
s32 bcm4360_module_unregister(struct bcm4360_module *table, const char *name, void *hdl)
{
	struct bcm4360_module *m;
	u32 i;

	if (!table)
		return BCM4360_BCME_NOTFOUND;
	for (i = 0; i < BCM4360_IOVAR_MODULES; i++) {
		m = &table[i];
		if (hw_strcmp(m->name, name) == 0 && m->hdl == hdl) {
			hw_memset(m, 0, sizeof(*m));
			return 0;
		}
	}
	return BCM4360_BCME_NOTFOUND;
}
