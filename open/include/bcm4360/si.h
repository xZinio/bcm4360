/* SPDX-License-Identifier: ISC */
/*
 * The backplane of a card: selecting a core (the two movable windows of
 * BAR0), register access to any core, the AXI wrapper of the current core
 * (core flags, reset, disable), the GPIOs of ChipCommon and the small chip
 * helpers. The PCIe bridge and the attach (the scan of the enumeration ROM)
 * are not here yet.
 *
 * Specification: docs/re/spec/chip.md (Broadcom's siutils.c / aiutils.c),
 * sections "Accessors", "Selecting the current core", "Register access of
 * any core", "AXI wrapper of the current core", "GPIO", "Crystal, device
 * presence, SROM control, host interface". The functions are those of the
 * specification with the prefix bcm4360_si_ instead of si_, for the AXI
 * backplane (type 1 or 3) on a PCI bus; the caller handles other types.
 *
 * The windows of BAR0 are those of struct bcm4360_hw (hw.h): window 1 =
 * hw->d11 (= hw->bar0), window 2 (the current core's wrapper) = hw->wrap,
 * the fixed windows hw->cc (ChipCommon) and hw->pcie (the bus core); PCI
 * configuration registers with hw_pcicfg_read/hw_pcicfg_write.
 */
#ifndef BCM4360_SI_H
#define BCM4360_SI_H

#include <bcm4360/types.h>
#include <bcm4360/hw.h>

#define BCM4360_SI_MAXCORES	32
#define BCM4360_SI_BADIDX	0x21	/* "no such core" of findcoreidx */

/*
 * The backplane as the functions see it: the fields of Broadcom's si_info_t
 * that chip.md names. The tables and the current core index are the
 * caller's storage, read (and, for curidx, written) in place.
 */
struct bcm4360_si {
	struct bcm4360_hw *hw;
	u32 bustype;		/* 1 = PCI */
	u32 buscoreid;		/* 0x83c: PCIe Gen2 */
	u32 buscorerev;
	u32 buscoreidx;		/* index of the bus core */
	u32 ccrev;		/* ChipCommon revision */
	u32 cccaps;		/* ChipCommon capabilities */
	u32 chip, chiprev, chippkg, chipst;
	u32 numcores;
	u32 *curidx;		/* the current core (written by setcoreidx) */
	const u32 *coreid;	/* [32] core id of core i */
	const u32 *coreregbase;	/* [32] backplane address of core i's registers */
	const u32 *wrapbase;	/* [32] backplane address of core i's wrapper */
	const u32 *cia;		/* [32] enumeration word "CIA" of core i */
	const u32 *cib;		/* [32] enumeration word "CIB" of core i */
	/* the interrupt callbacks of si_register_intr_callback (NULL: none) */
	u32 intr_coreid;	/* core id that was current at registration */
	void *intr_arg;
	u32 (*intr_off)(void *arg);
	void (*intr_restore)(void *arg, u32 state);
};

/* accessors without hardware access */
u32 bcm4360_si_coreidx(const struct bcm4360_si *si);
u32 bcm4360_si_coreid(const struct bcm4360_si *si);
u32 bcm4360_si_corerev(const struct bcm4360_si *si);
u32 bcm4360_si_corevendor(const struct bcm4360_si *si);
bool bcm4360_si_backplane64(const struct bcm4360_si *si);
u32 bcm4360_si_findcoreidx(const struct bcm4360_si *si, u32 coreid, u32 unit);

/* selecting the current core: results are the register window or NULL */
void *bcm4360_si_setcoreidx(struct bcm4360_si *si, u32 idx);
void *bcm4360_si_setcore(struct bcm4360_si *si, u32 coreid, u32 unit);
void *bcm4360_si_switch_core(struct bcm4360_si *si, u32 coreid, u32 *origidx, u32 *intr_val);
void bcm4360_si_restore_core(struct bcm4360_si *si, u32 origidx, u32 intr_val);

/* register access of any core */
u32 bcm4360_si_corereg(struct bcm4360_si *si, u32 idx, u32 regoff, u32 mask, u32 val);
u32 bcm4360_si_ccreg(struct bcm4360_si *si, u32 regoff, u32 mask, u32 val);

/* the AXI wrapper of the current core */
bool bcm4360_si_iscoreup(struct bcm4360_si *si);
u32 bcm4360_si_core_cflags(struct bcm4360_si *si, u32 mask, u32 val);
void bcm4360_si_core_cflags_wo(struct bcm4360_si *si, u32 mask, u32 val);
u32 bcm4360_si_core_sflags(struct bcm4360_si *si, u32 mask, u32 val);
void bcm4360_si_core_reset(struct bcm4360_si *si, u32 bits, u32 resetbits);
void bcm4360_si_core_disable(struct bcm4360_si *si, u32 bits);
u32 bcm4360_si_wrapperreg(struct bcm4360_si *si, u32 offset, u32 mask, u32 val);

/* GPIO of ChipCommon (priority: 0 driver, 1 ..., 2 high) */
u32 bcm4360_si_gpiocontrol(struct bcm4360_si *si, u32 mask, u32 val, u8 priority);
u32 bcm4360_si_gpioouten(struct bcm4360_si *si, u32 mask, u32 val, u8 priority);
u32 bcm4360_si_gpioout(struct bcm4360_si *si, u32 mask, u32 val, u8 priority);
u32 bcm4360_si_gpioin(struct bcm4360_si *si);
u32 bcm4360_si_gpioled(struct bcm4360_si *si, u32 mask, u32 val);
u32 bcm4360_si_gpiotimerval(struct bcm4360_si *si, u32 mask, u32 val);

/* crystal, device presence, SROM control, host interface, small helpers */
s32 bcm4360_si_clkctl_xtal(struct bcm4360_si *si, u32 what, bool on);
bool bcm4360_si_deviceremoved(struct bcm4360_si *si);
u32 bcm4360_si_get_sromctl(struct bcm4360_si *si);
s32 bcm4360_si_set_sromctl(struct bcm4360_si *si, u32 value);
u32 bcm4360_si_chip_hostif(const struct bcm4360_si *si);
u16 bcm4360_si_fabid(struct bcm4360_si *si);
bool bcm4360_si_pci_war16165(const struct bcm4360_si *si);
bool bcm4360_si_ldo_war(struct bcm4360_si *si, u32 devid);
u32 bcm4360_si_tcm_size(struct bcm4360_si *si);

/*
 * The PCIe bridge (chip.md: nicpci.c and the si_pci* functions; file
 * open/chip/si_pcie.c): si_survive_perst_war with pcie_survive_perst. The
 * registers of the PCIe core are reached through the fixed window hw->pcie.
 */
void bcm4360_si_survive_perst_war(struct bcm4360_si *si, bool reset, u32 mask, u32 val);

#endif
