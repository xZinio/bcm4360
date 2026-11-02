// SPDX-License-Identifier: ISC
/*
 * Hybrid build: the backplane functions of Broadcom's core that the other
 * layers call - core selection, register access to any core, the AXI wrapper
 * of the current core, GPIO, small chip helpers (siutils.c, aiutils.c) - by
 * the open code of open/chip/si.c (docs/re/spec/chip.md; task
 * docs/re/tasks/chip-core.md). Switch open_si.
 *
 * si_attach (the scan of the enumeration ROM) stays Broadcom's, and with it
 * the structure si_info_t. No code outside siutils.c/aiutils.c/nicpci.c reads
 * its private part (chip.md, "Who uses si_info_t"), so the open functions
 * can work on it in place: the adapter hands them pointers to the current
 * core index and the per-core tables, and copies of the other fields they
 * read. The open code is used for an AXI backplane on PCI with a chip of the
 * BCM4360 family; anything else runs Broadcom's function (bcm4360_blob_*).
 *
 * Parameters of type bool or u8 of the object are bytes: taken as u32 and
 * truncated.
 */
#include "hybrid.h"
#include <bcm4360/si.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_si = true;

#ifdef __KERNEL__
module_param_named(open_si, bcm4360_open_si, bool, 0444);
MODULE_PARM_DESC(open_si, "backplane: core selection, register access, wrapper, GPIO: open code (1, default) or Broadcom's (0)");
#endif

/* si_info_t: docs/re/spec/chip.md, "Data" (the public part is struct hyb_si_pub) */
#define SI_INTR_COREID		0x068	/* u32 */
#define SI_INTR_ARG		0x070	/* void * */
#define SI_INTR_OFF		0x078	/* u32 (*)(void *) */
#define SI_INTR_RESTORE		0x080	/* void (*)(void *, u32) */
#define SI_CURIDX		0x1c0	/* u32 */
#define SI_NUMCORES		0x1c4	/* u32 */
#define SI_COREID		0x1c8	/* u32 [32] */
#define SI_COREREGBASE		0x248	/* u32 [32] */
#define SI_WRAPBASE		0x650	/* u32 [32] */
#define SI_CIA			0x6d0	/* u32 [32] */
#define SI_CIB			0x750	/* u32 [32] */

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

typedef u32 (*intr_off_fn)(void *arg);
typedef void (*intr_restore_fn)(void *arg, u32 state);

/* fills the open view of the backplane; false: Broadcom's function must run */
static bool si_view(void *sih, struct bcm4360_si *si)
{
	const struct hyb_si_pub *pub = sih;
	struct hyb_card *card;

	if (!bcm4360_open_si || !sih)
		return false;
	if ((pub->socitype != 1 && pub->socitype != 3) || pub->bustype != 1)
		return false;
	if (pub->chip != 0x4360 && pub->chip != 0x4352 && pub->chip != 0xaa06 &&
	    pub->chip != 0xa9c4)
		return false;
	card = hyb_card_of(sih);
	if (!card)
		return false;
	si->hw = &card->hw;
	si->bustype = pub->bustype;
	si->buscoreid = pub->buscoretype;
	si->buscorerev = pub->buscorerev;
	si->buscoreidx = pub->buscoreidx;
	si->ccrev = (u32)pub->ccrev;
	si->cccaps = pub->cccaps;
	si->chip = pub->chip;
	si->chiprev = pub->chiprev;
	si->chippkg = pub->chippkg;
	si->chipst = pub->chipst;
	si->numcores = FIELD(sih, SI_NUMCORES, u32);
	si->curidx = &FIELD(sih, SI_CURIDX, u32);
	si->coreid = &FIELD(sih, SI_COREID, u32);
	si->coreregbase = &FIELD(sih, SI_COREREGBASE, u32);
	si->wrapbase = &FIELD(sih, SI_WRAPBASE, u32);
	si->cia = &FIELD(sih, SI_CIA, u32);
	si->cib = &FIELD(sih, SI_CIB, u32);
	si->intr_coreid = FIELD(sih, SI_INTR_COREID, u32);
	si->intr_arg = FIELD(sih, SI_INTR_ARG, void *);
	si->intr_off = FIELD(sih, SI_INTR_OFF, intr_off_fn);
	si->intr_restore = FIELD(sih, SI_INTR_RESTORE, intr_restore_fn);
	return true;
}

HYB_TAKE(u32, si_coreidx, (void *sih));
HYB_TAKE(u32, si_coreid, (void *sih));
HYB_TAKE(u32, si_corerev, (void *sih));
HYB_TAKE(u32, si_corevendor, (void *sih));
HYB_TAKE(u8, si_backplane64, (void *sih));
HYB_TAKE(u32, si_findcoreidx, (void *sih, u32 coreid, u32 unit));
HYB_TAKE(void *, si_setcoreidx, (void *sih, u32 idx));
HYB_TAKE(void *, si_setcore, (void *sih, u32 coreid, u32 unit));
HYB_TAKE(void *, si_switch_core, (void *sih, u32 coreid, u32 *origidx, u32 *intr_val));
HYB_TAKE(void, si_restore_core, (void *sih, u32 origidx, u32 intr_val));
HYB_TAKE(u32, si_corereg, (void *sih, u32 idx, u32 regoff, u32 mask, u32 val));
HYB_TAKE(u32, si_ccreg, (void *sih, u32 regoff, u32 mask, u32 val));
HYB_TAKE(u8, si_iscoreup, (void *sih));
HYB_TAKE(u32, si_core_cflags, (void *sih, u32 mask, u32 val));
HYB_TAKE(void, si_core_cflags_wo, (void *sih, u32 mask, u32 val));
HYB_TAKE(u32, si_core_sflags, (void *sih, u32 mask, u32 val));
HYB_TAKE(void, si_core_reset, (void *sih, u32 bits, u32 resetbits));
HYB_TAKE(void, si_core_disable, (void *sih, u32 bits));
HYB_TAKE(u32, si_wrapperreg, (void *sih, u32 offset, u32 mask, u32 val));
HYB_TAKE(u32, si_gpiocontrol, (void *sih, u32 mask, u32 val, u32 priority));
HYB_TAKE(u32, si_gpioouten, (void *sih, u32 mask, u32 val, u32 priority));
HYB_TAKE(u32, si_gpioout, (void *sih, u32 mask, u32 val, u32 priority));
HYB_TAKE(u32, si_gpioin, (void *sih));
HYB_TAKE(u32, si_gpioled, (void *sih, u32 mask, u32 val));
HYB_TAKE(u32, si_gpiotimerval, (void *sih, u32 mask, u32 val));
HYB_TAKE(s32, si_clkctl_xtal, (void *sih, u32 what, u32 on));
HYB_TAKE(u8, si_deviceremoved, (void *sih));
HYB_TAKE(u32, si_get_sromctl, (void *sih));
HYB_TAKE(s32, si_set_sromctl, (void *sih, u32 value));
HYB_TAKE(u32, si_chip_hostif, (void *sih));
HYB_TAKE(u16, si_fabid, (void *sih));
HYB_TAKE(u8, si_pci_war16165, (void *sih));
HYB_TAKE(u8, si_ldo_war, (void *sih, u32 devid));
HYB_TAKE(u32, si_tcm_size, (void *sih));

/* R(type, name, (params), (open args after si), (blob args)) */
#define R(type, name, params, open_args, blob_args)			\
type si_##name params							\
{									\
	struct bcm4360_si si;						\
									\
	if (!si_view(sih, &si))						\
		return bcm4360_blob_si_##name blob_args;		\
	return bcm4360_si_##name open_args;				\
}
#define V(name, params, open_args, blob_args)				\
void si_##name params							\
{									\
	struct bcm4360_si si;						\
									\
	if (!si_view(sih, &si)) {					\
		bcm4360_blob_si_##name blob_args;			\
		return;							\
	}								\
	bcm4360_si_##name open_args;					\
}

R(u32, coreidx, (void *sih), (&si), (sih))
R(u32, coreid, (void *sih), (&si), (sih))
R(u32, corerev, (void *sih), (&si), (sih))
R(u32, corevendor, (void *sih), (&si), (sih))
R(u8, backplane64, (void *sih), (&si), (sih))
R(u32, findcoreidx, (void *sih, u32 coreid, u32 unit), (&si, coreid, unit), (sih, coreid, unit))
R(void *, setcoreidx, (void *sih, u32 idx), (&si, idx), (sih, idx))
R(void *, setcore, (void *sih, u32 coreid, u32 unit), (&si, coreid, unit), (sih, coreid, unit))
R(void *, switch_core, (void *sih, u32 coreid, u32 *origidx, u32 *intr_val),
  (&si, coreid, origidx, intr_val), (sih, coreid, origidx, intr_val))
V(restore_core, (void *sih, u32 origidx, u32 intr_val), (&si, origidx, intr_val),
  (sih, origidx, intr_val))
R(u32, corereg, (void *sih, u32 idx, u32 regoff, u32 mask, u32 val),
  (&si, idx, regoff, mask, val), (sih, idx, regoff, mask, val))
R(u32, ccreg, (void *sih, u32 regoff, u32 mask, u32 val), (&si, regoff, mask, val),
  (sih, regoff, mask, val))
R(u8, iscoreup, (void *sih), (&si), (sih))
R(u32, core_cflags, (void *sih, u32 mask, u32 val), (&si, mask, val), (sih, mask, val))
V(core_cflags_wo, (void *sih, u32 mask, u32 val), (&si, mask, val), (sih, mask, val))
R(u32, core_sflags, (void *sih, u32 mask, u32 val), (&si, mask, val), (sih, mask, val))
V(core_reset, (void *sih, u32 bits, u32 resetbits), (&si, bits, resetbits),
  (sih, bits, resetbits))
V(core_disable, (void *sih, u32 bits), (&si, bits), (sih, bits))
R(u32, wrapperreg, (void *sih, u32 offset, u32 mask, u32 val), (&si, offset, mask, val),
  (sih, offset, mask, val))
R(u32, gpiocontrol, (void *sih, u32 mask, u32 val, u32 priority), (&si, mask, val, (u8)priority),
  (sih, mask, val, priority))
R(u32, gpioouten, (void *sih, u32 mask, u32 val, u32 priority), (&si, mask, val, (u8)priority),
  (sih, mask, val, priority))
R(u32, gpioout, (void *sih, u32 mask, u32 val, u32 priority), (&si, mask, val, (u8)priority),
  (sih, mask, val, priority))
R(u32, gpioin, (void *sih), (&si), (sih))
R(u32, gpioled, (void *sih, u32 mask, u32 val), (&si, mask, val), (sih, mask, val))
R(u32, gpiotimerval, (void *sih, u32 mask, u32 val), (&si, mask, val), (sih, mask, val))
R(s32, clkctl_xtal, (void *sih, u32 what, u32 on), (&si, what, (u8)on != 0), (sih, what, on))
R(u8, deviceremoved, (void *sih), (&si), (sih))
R(u32, get_sromctl, (void *sih), (&si), (sih))
R(s32, set_sromctl, (void *sih, u32 value), (&si, value), (sih, value))
R(u32, chip_hostif, (void *sih), (&si), (sih))
R(u16, fabid, (void *sih), (&si), (sih))
R(u8, pci_war16165, (void *sih), (&si), (sih))
R(u8, ldo_war, (void *sih, u32 devid), (&si, devid), (sih, devid))
R(u32, tcm_size, (void *sih), (&si), (sih))
