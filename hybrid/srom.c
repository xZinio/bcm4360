// SPDX-License-Identifier: ISC
/*
 * Hybrid build: the board variables of Broadcom's core - srom_var_init (the
 * SROM or OTP image of the card turned into "name=value" strings) and
 * srom_read (bcmsrom.c) - by the open code of open/chip/srom.c
 * (docs/re/spec/srom-otp.md). Switch open_srom.
 *
 * What stays Broadcom's: the backplane functions the procedures call (core
 * windows, chip control, SROM present), the OTP (bcmotp.c) and the global
 * NVRAM (nvram_ro.c); they are the services of open/include/bcm4360/srom.h,
 * implemented below by calling them. For a bus other than PCI or an SROM
 * image of a revision other than 11 the open code reports
 * BCM4360_SROM_UNHANDLED and Broadcom's own function runs instead (on this
 * card: never).
 *
 * Parameters of type bool of the object are bytes (u8).
 */
#include "hybrid.h"
#include <bcm4360/srom.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_srom = true;

#ifdef __KERNEL__
module_param_named(open_srom, bcm4360_open_srom, bool, 0444);
MODULE_PARM_DESC(open_srom, "board variables from SROM/OTP: open code (1, default) or Broadcom's (0)");
#endif

HYB_TAKE(s32, srom_var_init, (void *sih, u32 bustype, void *curmap, void *osh, char **vars,
			      u32 *count));
HYB_TAKE(s32, srom_read, (void *sih, u32 bustype, void *curmap, void *osh, u32 byteoff,
			  u32 nbytes, u16 *buf, u32 check_crc));

/* Broadcom's functions the services call (hybrid/takeover.txt: use) */
u8 si_is_sprom_available(void *sih);
u32 si_chipcontrl_read(void *sih);
void si_chipcontrl_restore(void *sih, u32 val);
void si_chipcontrl_srom4360(void *sih, u32 on);
s32 otp_read_region(void *sih, s32 region, u16 *data, u32 *wlen);
char *si_getdevpathvar(void *sih, const char *name);
char *si_getnvramflvar(void *sih, const char *name);
s32 nvram_getall(char *buf, s32 count);
s32 si_devpath(void *sih, char *path, s32 size);
char *si_coded_devpathvar(void *sih, char *out, s32 size, const char *name);
void *si_osh(void *sih);

/* the card from Broadcom's si_t (struct si_pub, hybrid.h) */
static void srom_card(struct bcm4360_srom_card *c, void *sih, u32 bustype, void *curmap)
{
	const struct hyb_si_pub *pub = sih;

	c->ctx = sih;
	c->osh = si_osh(sih);	/* srom-otp.md: the osh argument is replaced by it on PCI */
	c->regs = curmap;
	c->bustype = bustype;
	c->ccrev = (u32)pub->ccrev;
	c->cccaps = pub->cccaps;
	c->chip = pub->chip;
	c->chiprev = pub->chiprev;
}

s32 srom_var_init(void *sih, u32 bustype, void *curmap, void *osh, char **vars, u32 *count)
{
	struct bcm4360_srom_card c;
	s32 r;

	if (!bcm4360_open_srom || !sih)
		return bcm4360_blob_srom_var_init(sih, bustype, curmap, osh, vars, count);
	srom_card(&c, sih, bustype, curmap);
	r = bcm4360_srom_var_init(&c, vars, count);
	if (r == BCM4360_SROM_UNHANDLED)
		return bcm4360_blob_srom_var_init(sih, bustype, curmap, osh, vars, count);
	return r;
}

s32 srom_read(void *sih, u32 bustype, void *curmap, void *osh, u32 byteoff, u32 nbytes,
	      u16 *buf, u32 check_crc)
{
	struct bcm4360_srom_card c;
	s32 r;

	if (!bcm4360_open_srom || !sih)
		return bcm4360_blob_srom_read(sih, bustype, curmap, osh, byteoff, nbytes, buf,
					      check_crc);
	srom_card(&c, sih, bustype, curmap);
	r = bcm4360_srom_read(&c, byteoff, nbytes, buf, (u8)check_crc != 0);
	if (r == BCM4360_SROM_UNHANDLED)
		return bcm4360_blob_srom_read(sih, bustype, curmap, osh, byteoff, nbytes, buf,
					      check_crc);
	return r;
}

/* the services of open/include/bcm4360/srom.h */
bool bcm4360_srom_sprom_available(void *ctx)
{
	return si_is_sprom_available(ctx) != 0;
}

u32 bcm4360_srom_chipcontrol_read(void *ctx)
{
	return si_chipcontrl_read(ctx);
}

void bcm4360_srom_chipcontrol_restore(void *ctx, u32 val)
{
	si_chipcontrl_restore(ctx, val);
}

void bcm4360_srom_chipcontrol_srom4360(void *ctx, bool on)
{
	si_chipcontrl_srom4360(ctx, on ? 1 : 0);
}

s32 bcm4360_srom_otp_read_region(void *ctx, s32 region, u16 *data, u32 *wlen)
{
	return otp_read_region(ctx, region, data, wlen);
}

const char *bcm4360_srom_getdevpathvar(void *ctx, const char *name)
{
	return si_getdevpathvar(ctx, name);
}

const char *bcm4360_srom_getnvramflvar(void *ctx, const char *name)
{
	return si_getnvramflvar(ctx, name);
}

s32 bcm4360_srom_nvram_getall(void *ctx, char *buf, s32 count)
{
	(void)ctx;
	return nvram_getall(buf, count);
}

s32 bcm4360_srom_devpath(void *ctx, char *path, s32 size)
{
	return si_devpath(ctx, path, size);
}

const char *bcm4360_srom_coded_devpathvar(void *ctx, char *out, s32 size, const char *name)
{
	return si_coded_devpathvar(ctx, out, size, name);
}
