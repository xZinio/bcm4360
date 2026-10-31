/* SPDX-License-Identifier: ISC */
/*
 * Generic helpers: board variables, number parsing, bit count, CRC-8.
 *
 * Specification: docs/re/spec/utils.md. The variables are one block of
 * NUL-terminated "name=value" strings ended by an empty string.
 *
 * `fallback`: what to ask for a name that is not in the block (the object
 * asks the driver's global NVRAM); NULL: nothing, the name has no value.
 */
#ifndef BCM4360_UTILS_H
#define BCM4360_UTILS_H

#include <bcm4360/types.h>

typedef const char *(*bcm4360_var_fallback)(const char *name);

/* utils.md, getvar: the value of `name`, or NULL */
const char *bcm4360_getvar(const char *vars, const char *name, bcm4360_var_fallback fallback);

/* utils.md, getintvar: the value of `name` as a number, 0 if it has none */
s32 bcm4360_getintvar(const char *vars, const char *name, bcm4360_var_fallback fallback);

/* utils.md, getintvararray: element `index` of a comma-separated list, 0 if none */
s32 bcm4360_getintvararray(const char *vars, const char *name, s32 index,
			   bcm4360_var_fallback fallback);

/* utils.md, getintvararraysize: the number of elements of the list */
s32 bcm4360_getintvararraysize(const char *vars, const char *name,
			       bcm4360_var_fallback fallback);

/* utils.md, bcm_strtoul: 64 bit result; *endp (if endp) = first byte not used */
u64 bcm4360_strtoul(const char *cp, const char **endp, u32 base);

/* utils.md, bcm_bitcount: bits set in `length` bytes */
u32 bcm4360_bitcount(const u8 *bitmap, u32 length);

/* utils.md, hndcrc8: CRC-8 (reflected, polynomial 0xab) of `nbytes` bytes, from `crc` */
u8 bcm4360_crc8(const u8 *data, u32 nbytes, u8 crc);

#endif
