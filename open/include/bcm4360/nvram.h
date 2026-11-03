/* SPDX-License-Identifier: ISC */
/*
 * The global NVRAM: board variables read from a text file "nvram.txt" of the
 * host (Broadcom's nvram_ro.c), kept as a list of blocks of NUL-separated
 * "name=value" strings, the newest block first. The list belongs to this
 * code (one per driver, empty at load).
 *
 * Specification: docs/re/spec/srom-otp.md, "nvram_ro.c: the global NVRAM"
 * and "Notes for an implementation of nvram_ro.c".
 *
 * `osh` is the OS handle for osl_malloc/osl_mfree (bcm4360/hw.h); Broadcom's
 * functions get it with si_osh(sih), the caller here passes it.
 */
#ifndef BCM4360_NVRAM_H
#define BCM4360_NVRAM_H

#include <bcm4360/types.h>

/* nvram_init; bustype: the bus type of the backplane (1 = PCI) */
s32 bcm4360_nvram_init(void *osh, u32 bustype);
/* nvram_append: a block of `len` bytes at `data` (the list keeps the pointer) */
s32 bcm4360_nvram_append(void *osh, char *data, u32 len);
/* nvram_get: the value of `name`, or NULL */
char *bcm4360_nvram_get(const char *name);
/* nvram_getall: all strings into buf (count bytes), then a NUL */
s32 bcm4360_nvram_getall(char *buf, s32 count);
/* nvram_exit */
void bcm4360_nvram_exit(void *osh);
/* nvram_get_global_vars: the data and length of the newest block */
void bcm4360_nvram_get_global_vars(char **vars, u32 *len);

#endif
