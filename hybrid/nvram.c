// SPDX-License-Identifier: ISC
/*
 * Hybrid build: Broadcom's global NVRAM (nvram_ro.c: the board variables of
 * a host file "nvram.txt") by the open code of open/chip/nvram.c
 * (docs/re/spec/srom-otp.md, "nvram_ro.c: the global NVRAM"). Switch
 * open_nvram.
 *
 * Every function of nvram_ro.c that reaches the list is taken over, and the
 * list is the open code's own (nothing else in the object reads it). The
 * switch is read at every call; as a module parameter it cannot change while
 * the module is loaded, so one side keeps the list for the whole time.
 */
#include "hybrid.h"
#include <bcm4360/nvram.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_nvram = true;

#ifdef __KERNEL__
module_param_named(open_nvram, bcm4360_open_nvram, bool, 0444);
MODULE_PARM_DESC(open_nvram, "board variables of nvram.txt (global NVRAM): open code (1, default) or Broadcom's (0)");
#endif

/* the object's OS handle of a backplane (siutils.c) */
void *si_osh(void *sih);

HYB_TAKE(s32, nvram_init, (void *sih));
HYB_TAKE(s32, nvram_append, (void *sih, char *data, u32 len));
HYB_TAKE(char *, nvram_get, (const char *name));
HYB_TAKE(s32, nvram_getall, (char *buf, s32 count));
HYB_TAKE(void, nvram_exit, (void *sih));
HYB_TAKE(void, nvram_get_global_vars, (char **vars, u32 *len));

s32 nvram_init(void *sih)
{
	if (!bcm4360_open_nvram)
		return bcm4360_blob_nvram_init(sih);
	return bcm4360_nvram_init(si_osh(sih), ((const struct hyb_si_pub *)sih)->bustype);
}

s32 nvram_append(void *sih, char *data, u32 len)
{
	if (!bcm4360_open_nvram)
		return bcm4360_blob_nvram_append(sih, data, len);
	return bcm4360_nvram_append(si_osh(sih), data, len);
}

char *nvram_get(const char *name)
{
	if (!bcm4360_open_nvram)
		return bcm4360_blob_nvram_get(name);
	return bcm4360_nvram_get(name);
}

s32 nvram_getall(char *buf, s32 count)
{
	if (!bcm4360_open_nvram)
		return bcm4360_blob_nvram_getall(buf, count);
	return bcm4360_nvram_getall(buf, count);
}

void nvram_exit(void *sih)
{
	if (!bcm4360_open_nvram) {
		bcm4360_blob_nvram_exit(sih);
		return;
	}
	bcm4360_nvram_exit(si_osh(sih));
}

void nvram_get_global_vars(char **vars, u32 *len)
{
	if (!bcm4360_open_nvram) {
		bcm4360_blob_nvram_get_global_vars(vars, len);
		return;
	}
	bcm4360_nvram_get_global_vars(vars, len);
}
