/*
 * Hybrid build: the board-variable lookup, number parsing, bit count and
 * CRC-8 of Broadcom's core (bcmutils.c) by the open code of open/lib/utils.c
 * (docs/re/spec/utils.md). The variable lookup alone was about a fifth of
 * everything the driver executes in a session.
 *
 * What stays the object's is the driver's global NVRAM, which the lookup
 * asks for names that are not among the board variables: the object's
 * nvram_get is handed to the open code as the fallback. With the switch
 * open_utils off the object's own functions run (bcm4360_blob_*).
 *
 * Integer arguments narrower than 32 bit arrive as u32 and are truncated.
 */
#include "hybrid.h"
#include <bcm4360/utils.h>

HYB_TAKE(char *, getvar, (char *vars, const char *name));
HYB_TAKE(s32, getintvar, (char *vars, const char *name));
HYB_TAKE(s32, getintvararray, (char *vars, const char *name, s32 index));
HYB_TAKE(s32, getintvararraysize, (char *vars, const char *name));
HYB_TAKE(u64, bcm_strtoul, (const char *cp, char **endp, u32 base));
HYB_TAKE(u32, bcm_bitcount, (const u8 *bitmap, u32 length));
HYB_TAKE(u32, hndcrc8, (const u8 *data, u32 nbytes, u32 crc));
HYB_TAKE(char *, phy_getvar, (void *pi, const char *name));

/* phy_info_t (`pi`): docs/re/spec/acphy-attach.md, "Data" */
#define PI_VARS			0x158	/* the board variables of the PHY (char *) */

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

/* the driver's global NVRAM (nvram_ro.c), the fallback of the lookup */
char *nvram_get(const char *name);

static const char *object_nvram(const char *name)
{
	return nvram_get(name);
}

char *getvar(char *vars, const char *name)
{
	if (!bcm4360_open_utils)
		return bcm4360_blob_getvar(vars, name);
	return (char *)bcm4360_getvar(vars, name, object_nvram);
}

s32 getintvar(char *vars, const char *name)
{
	if (!bcm4360_open_utils)
		return bcm4360_blob_getintvar(vars, name);
	return bcm4360_getintvar(vars, name, object_nvram);
}

s32 getintvararray(char *vars, const char *name, s32 index)
{
	if (!bcm4360_open_utils)
		return bcm4360_blob_getintvararray(vars, name, index);
	return bcm4360_getintvararray(vars, name, index, object_nvram);
}

s32 getintvararraysize(char *vars, const char *name)
{
	if (!bcm4360_open_utils)
		return bcm4360_blob_getintvararraysize(vars, name);
	return bcm4360_getintvararraysize(vars, name, object_nvram);
}

u64 bcm_strtoul(const char *cp, char **endp, u32 base)
{
	if (!bcm4360_open_utils)
		return bcm4360_blob_bcm_strtoul(cp, endp, base);
	return bcm4360_strtoul(cp, (const char **)endp, base);
}

u32 bcm_bitcount(const u8 *bitmap, u32 length)
{
	if (!bcm4360_open_utils)
		return bcm4360_blob_bcm_bitcount(bitmap, length);
	return bcm4360_bitcount(bitmap, length);
}

u32 hndcrc8(const u8 *data, u32 nbytes, u32 crc)
{
	if (!bcm4360_open_utils)
		return bcm4360_blob_hndcrc8(data, nbytes, crc);
	return bcm4360_crc8(data, nbytes, (u8)crc);
}

/*
 * The lookup of Broadcom's PHY (wlc_phy_cmn.c) in its own pointer to the
 * board variables, without the NVRAM fallback (utils.md, phy_getvar): with
 * Broadcom's PHY running (open_phy off) this was the closed function the
 * driver executed most.
 */
char *phy_getvar(void *pi, const char *name)
{
	if (!bcm4360_open_utils)
		return bcm4360_blob_phy_getvar(pi, name);
	return (char *)bcm4360_getvar(FIELD(pi, PI_VARS, const char *), name, NULL);
}
