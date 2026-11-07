/*
 * The board variables of a card ("boardflags=0x...", "pa2ga0=...") made from
 * its SROM, or from the SROM image in its OTP, or taken from the platform's
 * NVRAM: the PCIe path of Broadcom's srom_var_init, and srom_read.
 *
 * Specification: docs/re/spec/srom-otp.md (srom_var_init, the conversion
 * and its complete revision-11 table, varbuf_append, initvars_table,
 * sprom_read_pci, srom_read, otp_read_pci, initvars_flash_pci).
 *
 * The backplane, the OTP and the NVRAM are services of the platform (below):
 * the specification describes what they do; this code calls them where the
 * specification's procedures call them.
 */
#ifndef BCM4360_SROM_H
#define BCM4360_SROM_H

#include <bcm4360/types.h>

/* The card as srom_var_init sees it (the fields of Broadcom's si_t it reads). */
struct bcm4360_srom_card {
	void *ctx;		/* the platform's handle, passed to every service */
	void *osh;		/* allocation handle: osl_malloc(osh, ...) / osl_mfree */
	void *regs;		/* `curmap`: the mapped BAR0 window (NULL: none) */
	u32 bustype;		/* 1 = PCI; other bus types are not handled here */
	u32 ccrev;		/* ChipCommon revision (sih+0x14) */
	u32 cccaps;		/* ChipCommon capabilities (sih+0x18) */
	u32 chip;		/* chip id (sih+0x3c) */
	u32 chiprev;		/* chip revision (sih+0x40) */
};

/*
 * Result of bcm4360_srom_var_init / bcm4360_srom_read when the card needs
 * what this code does not do: a bus type other than PCI, or an image of an
 * SROM revision other than 11 (revisions 1..5 and 8..10 are converted by
 * other tables, srom-otp.md "Revisions other than 11"). Everything this code
 * allocated is freed again; the caller then uses another implementation.
 */
#define BCM4360_SROM_UNHANDLED	0x7fff0001

/*
 * srom-otp.md, srom_var_init: 0 or the specification's error results; on
 * success *vars is a fresh osl_malloc(osh, *count) block of NUL-terminated
 * "name=value" strings ended by an empty string.
 */
s32 bcm4360_srom_var_init(const struct bcm4360_srom_card *card, char **vars, u32 *count);

/* srom-otp.md, srom_read: 0 on success, 1 on failure (a truth value "failed") */
s32 bcm4360_srom_read(const struct bcm4360_srom_card *card, u32 byteoff, u32 nbytes,
		      u16 *buf, bool check_crc);

/*
 * Services of the platform. `ctx` is card->ctx. Each does what the function
 * of Broadcom's named in the comment does (srom-otp.md describes them).
 */
bool bcm4360_srom_sprom_available(void *ctx);		/* si_is_sprom_available */
u32 bcm4360_srom_chipcontrol_read(void *ctx);		/* si_chipcontrl_read */
void bcm4360_srom_chipcontrol_restore(void *ctx, u32 val);	/* si_chipcontrl_restore */
void bcm4360_srom_chipcontrol_srom4360(void *ctx, bool on);	/* si_chipcontrl_srom4360 */
s32 bcm4360_srom_otp_read_region(void *ctx, s32 region, u16 *data, u32 *wlen);
							/* otp_read_region */
const char *bcm4360_srom_getdevpathvar(void *ctx, const char *name); /* si_getdevpathvar */
const char *bcm4360_srom_getnvramflvar(void *ctx, const char *name); /* si_getnvramflvar */
s32 bcm4360_srom_nvram_getall(void *ctx, char *buf, s32 count);	/* nvram_getall */
s32 bcm4360_srom_devpath(void *ctx, char *path, s32 size);	/* si_devpath */
const char *bcm4360_srom_coded_devpathvar(void *ctx, char *out, s32 size,
					  const char *name);	/* si_coded_devpathvar */

#endif
