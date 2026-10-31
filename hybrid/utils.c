// SPDX-License-Identifier: ISC
/*
 * Hybrid bridge, layer "utils": the board-variable lookup, number parsing,
 * bit count and CRC-8 of Broadcom's object (bcmutils.c) are done by the open
 * code (open/lib/utils.c, written from docs/re/spec/utils.md).
 *
 * The only thing that stays the object's is the driver's global NVRAM, which
 * the lookup asks for names that are not among the board variables: the
 * object's nvram_get is handed to the open code as the fallback.
 *
 * Integer arguments narrower than 32 bit are taken as u32 and truncated (see
 * access.c).
 */
#include <bcm4360/utils.h>

char *nvram_get(const char *name);

static const char *object_nvram(const char *name)
{
	return nvram_get(name);
}

char *getvar(char *vars, const char *name);
s32 getintvar(char *vars, const char *name);
s32 getintvararray(char *vars, const char *name, s32 index);
s32 getintvararraysize(char *vars, const char *name);
u64 bcm_strtoul(const char *cp, char **endp, u32 base);
u32 bcm_bitcount(const u8 *bitmap, u32 length);
u32 hndcrc8(const u8 *data, u32 nbytes, u32 crc);

char *getvar(char *vars, const char *name)
{
	return (char *)bcm4360_getvar(vars, name, object_nvram);
}

s32 getintvar(char *vars, const char *name)
{
	return bcm4360_getintvar(vars, name, object_nvram);
}

s32 getintvararray(char *vars, const char *name, s32 index)
{
	return bcm4360_getintvararray(vars, name, index, object_nvram);
}

s32 getintvararraysize(char *vars, const char *name)
{
	return bcm4360_getintvararraysize(vars, name, object_nvram);
}

u64 bcm_strtoul(const char *cp, char **endp, u32 base)
{
	return bcm4360_strtoul(cp, (const char **)endp, base);
}

u32 bcm_bitcount(const u8 *bitmap, u32 length)
{
	return bcm4360_bitcount(bitmap, length);
}

u32 hndcrc8(const u8 *data, u32 nbytes, u32 crc)
{
	return bcm4360_crc8(data, nbytes, (u8)crc);
}
