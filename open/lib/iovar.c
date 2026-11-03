// SPDX-License-Identifier: ISC
/*
 * The iovar dispatcher (Broadcom's wlc_iovar_op, wlc.c): finds a named driver
 * variable in the iovar tables of the driver's modules and hands the get or
 * set to the module's handler.
 *
 * Written from docs/re/spec/iovar.md. The module table, the check of a
 * request, the BSS configurations and the handlers are the caller's, reached
 * through the bcm4360_iovar_env_* services; the table entries are only read.
 */
#include <bcm4360/iovar.h>
#include <bcm4360/hw.h>

/* docs/re/spec/iovar.md, "Procedure", step 1: module and variable names */
#define NAME_SEP		':'

/* step 2: the value size of an integer variable (types 1..7) */
#define VAL_SIZE_INT		4

/* step 3: the BSS configuration prefix and the index that follows it in params */
#define BSSCFG_PREFIX		"bsscfg:"
#define BSSCFG_PREFIX_LEN	7
#define BSSCFG_IDX_SIZE		4

/* step 6: the value of "transmit header cache enabled" that marks it valid */
#define TXC_ENABLED		1

/*
 * docs/re/spec/iovar.md, "Procedure", step 1: the part of `name` after its
 * last ':', or the whole name if it has none.
 */
static const char *iovar_base_name(const char *name)
{
	const char *base = name;
	const char *p;

	for (p = name; *p != '\0'; p++)
		if (*p == NAME_SEP)
			base = p + 1;
	return base;
}

/*
 * docs/re/spec/iovar.md, "Procedure", step 1: the first entry named `base` in
 * the iovar table of the lowest module slot that has one, NULL if there is
 * none; *slot = its slot.
 */
static const struct bcm4360_iovar *iovar_lookup(void *wlc, const char *base, u32 *slot)
{
	const struct bcm4360_iovar *vi;
	u32 i;

	for (i = 0; i < BCM4360_IOVAR_MODULES; i++) {
		vi = bcm4360_iovar_env_table(wlc, i);
		if (!vi)
			continue;	/* an empty slot */
		for (; vi->name; vi++) {
			if (hw_strcmp(vi->name, base) == 0) {
				*slot = i;
				return vi;
			}
		}
	}
	return NULL;
}

/* docs/re/spec/iovar.md, "Procedure", step 2.2: val_size */
static s32 iovar_val_size(const struct bcm4360_iovar *vi, s32 len)
{
	if (vi->type == BCM4360_IOVT_VOID)
		return 0;
	if (vi->type == BCM4360_IOVT_BUFFER)
		return len;
	return VAL_SIZE_INT;
}

/*
 * docs/re/spec/iovar.md, "Procedure", step 3.2: the BSS configuration index,
 * the first 4 bytes of params (little endian, not necessarily aligned).
 */
static s32 iovar_bsscfg_idx(const void *params)
{
	const u8 *p = params;

	return (s32)((u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24);
}

/*
 * docs/re/spec/iovar.md, "Procedure", step 6: after a set, the transmit header
 * cache is valid if it is enabled (read after the handler), and its
 * generation counts on.
 */
static void iovar_after_set(const struct bcm4360_iovar_wlc *w)
{
	*w->txc_valid = *w->txc_enab == TXC_ENABLED ? 1 : 0;
	*w->txc_gen += 1;
}

/*
 * docs/re/spec/iovar.md, "Procedure: wlc_iovar_op": a get or set of the
 * variable `name`. Returns 0 or a Broadcom error code.
 */
s32 bcm4360_iovar_op(const struct bcm4360_iovar_wlc *w, const char *name, void *params,
		     s32 p_len, void *arg, s32 len, bool set, void *wlcif)
{
	const struct bcm4360_iovar *vi;
	void *bsscfg;
	s32 val_size, r;
	s32 err = 0;
	u32 actionid;
	u32 slot = 0;

	/* step 1 */
	vi = iovar_lookup(w->wlc, iovar_base_name(name), &slot);
	if (!vi)
		return BCM4360_BCME_UNSUPPORTED;

	/* step 2 */
	if (!params) {
		params = arg;
		p_len = len;
	}
	val_size = iovar_val_size(vi, len);
	actionid = (u32)vi->varid * 2 + (set ? 1 : 0);

	/* step 3, on the full name */
	if (hw_strncmp(name, BSSCFG_PREFIX, BSSCFG_PREFIX_LEN) == 0) {
		if (p_len < BSSCFG_IDX_SIZE)
			return BCM4360_BCME_BUFTOOSHORT;
		bsscfg = bcm4360_iovar_env_bsscfg_find(w->wlc, iovar_bsscfg_idx(params), &err);
		if (err != 0)
			return err;
		if (set) {
			arg = (u8 *)arg + BSSCFG_IDX_SIZE;
			len -= BSSCFG_IDX_SIZE;
		}
		name += BSSCFG_PREFIX_LEN;
		p_len -= BSSCFG_IDX_SIZE;
		params = (u8 *)params + BSSCFG_IDX_SIZE;
		wlcif = bcm4360_iovar_env_bsscfg_wlcif(w->wlc, bsscfg);
	}

	/* step 4 */
	r = bcm4360_iovar_env_check(w->wlc, vi, arg, len, set, wlcif);
	if (r != 0)
		return r;

	/* step 5 */
	err = bcm4360_iovar_env_handler(w->wlc, slot, vi, actionid, name, params, p_len, arg,
					len, val_size, wlcif);

	/* step 6, whatever the handler returned */
	if (set)
		iovar_after_set(w);

	/* step 7 */
	return err;
}
