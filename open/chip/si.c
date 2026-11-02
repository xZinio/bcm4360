// SPDX-License-Identifier: ISC
/*
 * The backplane of the BCM4360: selecting the current core (the two movable
 * windows of BAR0), register access to any core, the AXI wrapper of the
 * current core, the GPIOs of ChipCommon and the small chip helpers.
 *
 * Written from docs/re/spec/chip.md for the AXI backplane (type 1 or 3) on a
 * PCI bus, chips of the 4360 family behind a PCIe Gen2 core. Each function is
 * the "ai_*" procedure of its pair (the dispatch on the backplane type is the
 * caller's). The SB backplane and the system bus, whose cores are mapped one
 * by one instead of shown through windows of BAR0, are not served here.
 *
 * Windows (docs/re/spec/chip.md, "Overview"): window 1 = hw->bar0 (hw->d11),
 * window 2 (the current core's AXI wrapper) = hw->wrap, the fixed windows
 * hw->pcie (BAR0 + 0x2000) and hw->cc (BAR0 + 0x3000).
 */
#include <bcm4360/si.h>

/* docs/re/spec/chip.md, "si_info_t", offset 0x04: bus types */
#define SI_BUS_PCI		1
#define SI_BUS_TYPE_2		2	/* PCMCIA/SDIO-like, tested by si_clkctl_xtal only */

/* core ids (docs/re/spec/chip.md, "Overview", "si_info_t") */
#define CORE_ID_CC		0x800	/* ChipCommon */
#define CORE_ID_PCI		0x804	/* PCI bridge */
#define CORE_ID_PCIE		0x820	/* PCIe bridge */
#define CORE_ID_PCIE2		0x83c	/* PCIe Gen2 bridge (this card) */
#define CORE_ID_ARMCR4		0x83e	/* ARM CR4 */
/* cores named by the guards of ai_core_cflags for other chips */
#define CORE_ID_0x50b		0x50b
#define CORE_ID_0x819		0x819
#define CORE_ID_0x82c		0x82c

/* chip ids (docs/re/spec/chip.md, procedures) */
#define CHIP_ID_4324		0x4324
#define CHIP_ID_4334		0x4334
#define CHIP_ID_4335		0x4335
#define CHIP_ID_4350		0x4350
#define CHIP_ID_4360		0x4360
#define CHIP_ID_4749		0x4749
#define CHIP_ID_5357		0x5357
#define CHIP_ID_43242		0xa8ea
#define CHIP_ID_43243		0xa8eb
#define CHIP_ID_43460		0xa9c4
#define CHIP_ID_43526		0xaa06
#define CHIP_ID_47162		0xb83a
#define CHIP_ID_53010		0xcf12
#define CHIP_ID_53018		0xcf1a

/* ai_core_cflags: the chip revision and package of the guards */
#define GUARD_47162_CHIPREV	0
#define GUARD_4749_CHIPPKG	11

/*
 * FIXEDWIN (docs/re/spec/chip.md, "Overview"): a PCI core (0x804) has the
 * fixed windows from this revision on
 */
#define PCI_FIXEDWIN_REV	13
/* si_pci_war16165: PCI cores (0x804) below this revision */
#define PCI_WAR16165_REV	11

/* PCI configuration registers (32-bit accesses) */
#define PCICFG_SIZE		4	/* bytes of every configuration access */
#define PCICFG_ID		0x00	/* bits 0..15 vendor id */
#define PCICFG_REV		0x08	/* bits 0..7 revision id, 8..31 class */
#define PCICFG_BAR0_WIN2	0x70	/* window 2 behind a PCIe Gen2 core */
#define PCICFG_BAR0_WIN		0x80	/* window 1 */
#define PCICFG_0xa8		0xa8	/* polled by si_ldo_war */
#define PCICFG_BAR0_WIN2_OLD	0xac	/* window 2 behind other bridges */
#define PCICFG_GPIO_IN		0xb0	/* GPIO of old PCI bridges */
#define PCICFG_GPIO_OUT		0xb4
#define PCICFG_GPIO_OUTEN	0xb8

#define PCI_VENDOR_ID_MASK	0xffff
#define PCI_VENDOR_BROADCOM	0x14e4
#define PCI_REV_ID_MASK		0xff

/* BAR0 (docs/re/spec/chip.md, "Overview") */
#define BAR0_WIN2		0x1000	/* the current core's wrapper */
#define BAR0_PCI_REGS_OLD	0x1800	/* old PCI cores without FIXEDWIN */
#define BAR0_PCIE		0x2000	/* the bus core, fixed */
#define BAR0_CC			0x3000	/* ChipCommon, fixed */
#define PCI_REGS_OLD_LIMIT	0xf00	/* old PCI core registers below this at 0x1800 */

/* AXI wrapper registers (docs/re/spec/chip.md, "AXI wrapper registers") */
#define AI_IOCTRL		0x408
#define AI_IOSTATUS		0x500
#define AI_RESETCTRL		0x800
#define AI_RESETSTATUS		0x804

#define SICF_CLOCK_EN		0x1	/* ioctrl: clock enable */
#define SICF_FGC		0x2	/* ioctrl: force gated clocks */
#define AIRC_RESET		0x1	/* resetctrl: core in reset */
#define AI_WRAP_REG_ALIGN	0x3	/* ai_wrap_reg: offsets are 32-bit aligned */

/* ChipCommon registers (docs/re/spec/chip.md, "ChipCommon registers used here") */
#define CC_GPIOIN		0x60
#define CC_GPIOOUT		0x64
#define CC_GPIOOUTEN		0x68
#define CC_GPIOCONTROL		0x6c
#define CC_GPIOTIMERVAL		0x88
#define CC_GPIOTIMEROUTMASK	0x8c	/* "gpioled" */
#define CC_0xf8			0xf8	/* fab id in bits 0..3 (other chips) */
#define CC_SROMCONTROL		0x190
#define CC_0x618		0x618	/* written by si_ldo_war, purpose unknown */
#define CC_0x658		0x658
#define CC_0x65c		0x65c

#define CC_FABID_0xf8_MASK	0xf

/* si_gpioled, si_gpiotimerval: first ChipCommon revision with the registers */
#define CCREV_GPIOTIMER		16
/* si_set_sromctl: first ChipCommon revision with the SROM control register */
#define CCREV_SROMCTL		32
/* si_ccreg: registers below this offset only */
#define CCREG_LIMIT		0xe09

/* the "no such register" result of si_gpioled/si_gpiotimerval */
#define SI_NOREG		0xffffffff
/* si_set_sromctl: the error for a ChipCommon revision below 32 */
#define SI_ERR_SROMCTL		(-23)
/* si_clkctl_xtal: "not done" */
#define SI_XTAL_NONE		(-1)

/* SPINWAIT: the condition is looked at every 10 microseconds */
#define SPINWAIT_STEP_US	10

/* ai_core_reset, ai_core_disable: waits and delays, microseconds */
#define RESET_WAIT_US		5000	/* resetstatus waits of the reset */
#define RESET_DELAY_US		10	/* after putting the core into reset */
#define RESET_TRIES		10	/* reads of resetctrl at most */
#define RESET_DONE_DELAY_US	1
#define DISABLE_WAIT_US		300	/* first resetstatus wait of the disable */
#define DISABLE_WAIT_LONG_US	10000	/* second one, after a time-out */
#define DISABLE_RESET_DELAY_US	1
#define DISABLE_DONE_DELAY_US	10

/* si_clkctl_xtal: the GPIOs of old PCI bridges and their delays */
#define XTAL_WHAT_XTAL		0x1	/* `what`: the crystal */
#define XTAL_WHAT_PLL		0x2	/* `what`: the PLL */
#define XTAL_GPIO_XTAL		0x40	/* GPIO bit 6: crystal on */
#define XTAL_GPIO_PLL		0x80	/* GPIO bit 7: PLL off */
#define XTAL_ON_DELAY_US	1000
#define PLL_ON_DELAY_US		2000

/* si_chip_hostif: results and the bits of package and chip status */
#define HOSTIF_PCIE		1
#define HOSTIF_2		2
#define HOSTIF_4		4
#define HOSTIF_4360_PKG_BIT	0x1
#define HOSTIF_4360_ST_BIT	0x100
#define HOSTIF_4335_ST_4	0x1	/* chip status bit 0: result 4 */
#define HOSTIF_4335_ST_2	0x4	/* chip status bit 2: result 2 */
#define HOSTIF_4335_ST_SHIFT	3	/* else: chip status bit 3 */

/* si_ldo_war: the PCI devices of the work-around, values and waits */
#define LDO_DEVID_10F6		0x10f6
#define LDO_DEVID_4322		0x4322
#define LDO_DEVID_432B		0x432b
#define LDO_DEVID_432C		0x432c
#define LDO_DEVID_432D		0x432d
#define LDO_CC_BASE		0x18000000	/* backplane address of ChipCommon */
#define LDO_CC_0x65c_VAL	0x3001
#define LDO_CC_0x618_VAL	0xd
#define LDO_DELAY_US		5000
#define LDO_WAIT_US		20000
#define LDO_PCICFG_0xa8_BIT	0x10000		/* bit 16 */

/* si_tcm_size: ARM CR4 registers and the bank arithmetic */
#define ARMCR4_CAP		0x04	/* bits 0..3, 4..7: numbers of banks */
#define ARMCR4_BANKIDX		0x40
#define ARMCR4_BANKINFO		0x44
#define ARMCR4_CAP_NB_MASK	0xf
#define ARMCR4_CAP_NB2_SHIFT	4
#define ARMCR4_BANKINFO_MASK	0x3f	/* bank size - 1, in units of 8 kB */
#define ARMCR4_BANK_UNIT	0x2000
#define ARMCR4_RESET_BITS	0x20	/* bits and resetbits of the core reset */

/* --- small helpers --------------------------------------------------- */

/*
 * docs/re/spec/chip.md, "Overview": the condition FIXEDWIN - ChipCommon and
 * the bus core are shown in fixed windows of BAR0.
 */
static bool si_fixedwin(const struct bcm4360_si *si)
{
	if (si->bustype != SI_BUS_PCI)
		return false;
	return si->buscoreid == CORE_ID_PCIE2 || si->buscoreid == CORE_ID_PCIE ||
	       (si->buscoreid == CORE_ID_PCI && si->buscorerev >= PCI_FIXEDWIN_REV);
}

/*
 * docs/re/spec/chip.md, "si_coreid": the core id of core `idx` (0 for an
 * index outside the tables).
 */
static u32 si_idx_coreid(const struct bcm4360_si *si, u32 idx)
{
	return idx < BCM4360_SI_MAXCORES ? si->coreid[idx] : 0;
}

/*
 * docs/re/spec/chip.md, "Interrupt callbacks": "interrupts off" before a core
 * switch, only when the callback is set and the current core is the one
 * that was current at registration. Result: the saved state, else 0.
 */
static u32 si_intr_off(struct bcm4360_si *si)
{
	if (si->intr_off && bcm4360_si_coreid(si) == si->intr_coreid)
		return si->intr_off(si->intr_arg);
	return 0;
}

/*
 * docs/re/spec/chip.md, "Interrupt callbacks": "interrupts restore" after a
 * core switch, under the same condition on the (now) current core.
 */
static void si_intr_restore(struct bcm4360_si *si, u32 state)
{
	if (si->intr_restore && bcm4360_si_coreid(si) == si->intr_coreid)
		si->intr_restore(si->intr_arg, state);
}

/*
 * docs/re/spec/chip.md, "Polling loops": SPINWAIT(R w+0x804 != 0, usec) on
 * the current core's wrapper. Result: the last value read (not 0 after a
 * time-out).
 */
static u32 wrap_wait_idle(struct bcm4360_si *si, u32 usec)
{
	u32 waited = 0;
	u32 st;

	st = hw_read32(si->hw->wrap, AI_RESETSTATUS);
	while (st != 0 && waited < usec) {
		hw_udelay(SPINWAIT_STEP_US);
		waited += SPINWAIT_STEP_US;
		st = hw_read32(si->hw->wrap, AI_RESETSTATUS);
	}
	return st;
}

/*
 * docs/re/spec/chip.md, "si_corereg" step 3 and 4: the optional
 * read-modify-write (`val` not limited to `mask`), then always a read.
 */
static u32 reg_mod32(void *base, u32 off, u32 mask, u32 val)
{
	u32 old;

	if (mask || val) {
		old = hw_read32(base, off);
		hw_write32(base, off, (old & ~mask) | val);
	}
	return hw_read32(base, off);
}

/* docs/re/spec/chip.md, "sub_021d1d" (si_cc_corereg): corereg on core 0 */
static u32 si_cc_corereg(struct bcm4360_si *si, u32 regoff, u32 mask, u32 val)
{
	return bcm4360_si_corereg(si, 0, regoff, mask, val);
}

/* --- accessors without hardware access ------------------------------- */

/* docs/re/spec/chip.md, "si_coreidx" */
u32 bcm4360_si_coreidx(const struct bcm4360_si *si)
{
	return *si->curidx;
}

/* docs/re/spec/chip.md, "si_coreid" */
u32 bcm4360_si_coreid(const struct bcm4360_si *si)
{
	return si_idx_coreid(si, *si->curidx);
}

/* docs/re/spec/chip.md, "si_corerev and ai_corerev": byte 3 of CIB */
u32 bcm4360_si_corerev(const struct bcm4360_si *si)
{
	u32 idx = *si->curidx;

	if (idx >= BCM4360_SI_MAXCORES)
		return 0;
	return si->cib[idx] >> 24;
}

/* docs/re/spec/chip.md, "si_corevendor and ai_corevendor": CIA bits 20..31 */
u32 bcm4360_si_corevendor(const struct bcm4360_si *si)
{
	u32 idx = *si->curidx;

	if (idx >= BCM4360_SI_MAXCORES)
		return 0;
	return si->cia[idx] >> 20;
}

/* docs/re/spec/chip.md, "si_backplane64": ChipCommon capabilities bit 27 */
bool bcm4360_si_backplane64(const struct bcm4360_si *si)
{
	return (si->cccaps & BIT(27)) != 0;
}

/* docs/re/spec/chip.md, "si_findcoreidx" */
u32 bcm4360_si_findcoreidx(const struct bcm4360_si *si, u32 coreid, u32 unit)
{
	u32 n = min(si->numcores, (u32)BCM4360_SI_MAXCORES);
	u32 found = 0;
	u32 i;

	for (i = 0; i < n; i++) {
		if (si->coreid[i] != coreid)
			continue;
		if (found == unit)
			return i;
		found++;
	}
	return BCM4360_SI_BADIDX;
}

/* --- selecting the current core -------------------------------------- */

/*
 * docs/re/spec/chip.md, "si_setcoreidx and ai_setcoreidx": move both windows
 * (always both configuration writes), make `idx` the current core. On a bus
 * other than PCI (step 4) only the index changes; the system bus (step 3,
 * mapping of the cores) is not served here.
 */
void *bcm4360_si_setcoreidx(struct bcm4360_si *si, u32 idx)
{
	struct bcm4360_hw *hw = si->hw;

	if (idx >= min(si->numcores, (u32)BCM4360_SI_MAXCORES))
		return NULL;
	if (si->bustype != SI_BUS_PCI) {
		*si->curidx = idx;
		return NULL;
	}
	hw_pcicfg_write(hw, PCICFG_BAR0_WIN, PCICFG_SIZE, si->coreregbase[idx]);
	if (si->buscoreid == CORE_ID_PCIE2)
		hw_pcicfg_write(hw, PCICFG_BAR0_WIN2, PCICFG_SIZE, si->wrapbase[idx]);
	else
		hw_pcicfg_write(hw, PCICFG_BAR0_WIN2_OLD, PCICFG_SIZE, si->wrapbase[idx]);
	*si->curidx = idx;
	return hw->bar0;
}

/* docs/re/spec/chip.md, "si_setcore" */
void *bcm4360_si_setcore(struct bcm4360_si *si, u32 coreid, u32 unit)
{
	u32 idx = bcm4360_si_findcoreidx(si, coreid, unit);

	if (idx >= BCM4360_SI_MAXCORES)
		return NULL;
	return bcm4360_si_setcoreidx(si, idx);
}

/*
 * docs/re/spec/chip.md, "si_switch_core": ChipCommon and the bus core through
 * their fixed windows (no access); any other core is made current, with
 * "interrupts off" before. *intr_val stays as it is when the callback is not
 * called.
 */
void *bcm4360_si_switch_core(struct bcm4360_si *si, u32 coreid, u32 *origidx, u32 *intr_val)
{
	if (si_fixedwin(si)) {
		*origidx = coreid;
		if (coreid == CORE_ID_CC)
			return (u8 *)si->hw->bar0 + BAR0_CC;
		if (coreid == si->buscoreid)
			return (u8 *)si->hw->bar0 + BAR0_PCIE;
	}
	if (si->intr_off && bcm4360_si_coreid(si) == si->intr_coreid)
		*intr_val = si->intr_off(si->intr_arg);
	*origidx = *si->curidx;
	return bcm4360_si_setcore(si, coreid, 0);
}

/*
 * docs/re/spec/chip.md, "si_restore_core": nothing after a fixed window,
 * else back to `origidx` and "interrupts restore".
 */
void bcm4360_si_restore_core(struct bcm4360_si *si, u32 origidx, u32 intr_val)
{
	if (si_fixedwin(si) && (origidx == CORE_ID_CC || origidx == si->buscoreid))
		return;
	bcm4360_si_setcoreidx(si, origidx);
	si_intr_restore(si, intr_val);
}

/* --- register access of any core ------------------------------------- */

/*
 * docs/re/spec/chip.md, "si_corereg and ai_corereg", step 2 (the fast
 * paths): the register window of core `idx` if it is reached without a
 * switch, else NULL.
 */
static void *corereg_fixed_window(const struct bcm4360_si *si, u32 idx, u32 regoff)
{
	if (si->bustype != SI_BUS_PCI)
		return NULL;
	if (si_idx_coreid(si, idx) == CORE_ID_CC && si_fixedwin(si))
		return si->hw->cc;
	if (idx != si->buscoreidx)
		return NULL;
	if (si_fixedwin(si))
		return si->hw->pcie;
	/* an old PCI core: its registers at 0x1800, those from 0xf00 on at 0x1000 */
	if (regoff < PCI_REGS_OLD_LIMIT)
		return (u8 *)si->hw->bar0 + BAR0_PCI_REGS_OLD;
	return (u8 *)si->hw->bar0 + BAR0_WIN2;
}

/*
 * docs/re/spec/chip.md, "si_corereg and ai_corereg": ChipCommon and the bus
 * core through the fixed windows; any other core with a switch there and
 * back, bracketed by the interrupt callbacks. Result: the register after the
 * optional write.
 */
u32 bcm4360_si_corereg(struct bcm4360_si *si, u32 idx, u32 regoff, u32 mask, u32 val)
{
	void *base;
	u32 orig, s, v;

	if (idx >= BCM4360_SI_MAXCORES)
		return 0;

	base = corereg_fixed_window(si, idx, regoff);
	if (base)
		return reg_mod32(base, regoff, mask, val);

	/* the slow path, step 2 */
	s = si_intr_off(si);
	orig = *si->curidx;
	base = bcm4360_si_setcoreidx(si, idx);
	/*
	 * No such core (an index between the number of cores and 31): the
	 * object would access address `regoff`; this code leaves it out.
	 */
	v = base ? reg_mod32(base, regoff, mask, val) : 0;

	/* step 5 */
	if (orig != idx)
		bcm4360_si_setcoreidx(si, orig);
	si_intr_restore(si, s);
	return v;
}

/* docs/re/spec/chip.md, "si_ccreg and write_ccreg" */
u32 bcm4360_si_ccreg(struct bcm4360_si *si, u32 regoff, u32 mask, u32 val)
{
	if (regoff >= CCREG_LIMIT)
		return 0;
	return si_cc_corereg(si, regoff, mask, val);
}

/* --- the AXI wrapper of the current core ----------------------------- */

/*
 * docs/re/spec/chip.md, "si_iscoreup and ai_iscoreup": clock enabled without
 * force gated clocks, and not in reset.
 */
bool bcm4360_si_iscoreup(struct bcm4360_si *si)
{
	u32 v;

	v = hw_read32(si->hw->wrap, AI_IOCTRL);
	if ((v & (SICF_FGC | SICF_CLOCK_EN)) != SICF_CLOCK_EN)
		return false;
	v = hw_read32(si->hw->wrap, AI_RESETCTRL);
	return (v & AIRC_RESET) == 0;
}

/*
 * docs/re/spec/chip.md, "si_core_cflags and ai_core_cflags", step 1: the
 * cores of other chips whose flags are not touched. None applies to the
 * 4360 family.
 */
static bool core_flags_guarded(const struct bcm4360_si *si)
{
	u32 cur;

	switch (si->chip) {
	case CHIP_ID_47162:
		if (si->chiprev != GUARD_47162_CHIPREV)
			return false;
		cur = CORE_ID_0x82c;
		break;
	case CHIP_ID_4749:
	case CHIP_ID_5357:
		if (si->chippkg != GUARD_4749_CHIPPKG)
			return false;
		cur = CORE_ID_0x819;
		break;
	case CHIP_ID_53018:
	case CHIP_ID_53010:
		cur = CORE_ID_0x50b;
		break;
	default:
		return false;
	}
	return bcm4360_si_coreid(si) == cur;
}

/* docs/re/spec/chip.md, "si_core_cflags and ai_core_cflags" (ioctrl) */
u32 bcm4360_si_core_cflags(struct bcm4360_si *si, u32 mask, u32 val)
{
	if (core_flags_guarded(si))
		return 0;
	return reg_mod32(si->hw->wrap, AI_IOCTRL, mask, val);
}

/* docs/re/spec/chip.md, "si_core_cflags_wo and ai_core_cflags_wo" */
void bcm4360_si_core_cflags_wo(struct bcm4360_si *si, u32 mask, u32 val)
{
	u32 old;

	if (core_flags_guarded(si))
		return;
	if (mask || val) {
		old = hw_read32(si->hw->wrap, AI_IOCTRL);
		hw_write32(si->hw->wrap, AI_IOCTRL, (old & ~mask) | val);
	}
}

/* docs/re/spec/chip.md, "si_core_sflags and ai_core_sflags" (iostatus) */
u32 bcm4360_si_core_sflags(struct bcm4360_si *si, u32 mask, u32 val)
{
	if (core_flags_guarded(si))
		return 0;
	return reg_mod32(si->hw->wrap, AI_IOSTATUS, mask, val);
}

/*
 * docs/re/spec/chip.md, "si_core_reset and ai_core_reset": flags with clock
 * and force gated clocks, a pulse of the reset bit, then the flags with the
 * clock only.
 */
void bcm4360_si_core_reset(struct bcm4360_si *si, u32 bits, u32 resetbits)
{
	void *w = si->hw->wrap;
	int i;

	/* steps 1..4 */
	wrap_wait_idle(si, RESET_WAIT_US);
	hw_write32(w, AI_IOCTRL, bits | resetbits | SICF_FGC | SICF_CLOCK_EN);
	(void)hw_read32(w, AI_IOCTRL);
	wrap_wait_idle(si, RESET_WAIT_US);

	/* step 5 */
	hw_write32(w, AI_RESETCTRL, AIRC_RESET);
	hw_udelay(RESET_DELAY_US);

	/* step 6: at most 10 reads of resetctrl, the body at most 9 times */
	for (i = 0; i < RESET_TRIES; i++) {
		if (hw_read32(w, AI_RESETCTRL) == 0)
			break;
		if (i == RESET_TRIES - 1)
			break;
		wrap_wait_idle(si, RESET_WAIT_US);
		hw_write32(w, AI_RESETCTRL, 0);
		wrap_wait_idle(si, RESET_WAIT_US);
	}

	/* steps 7, 8 */
	hw_write32(w, AI_IOCTRL, bits | SICF_CLOCK_EN);
	(void)hw_read32(w, AI_IOCTRL);
	hw_udelay(RESET_DONE_DELAY_US);
}

/*
 * docs/re/spec/chip.md, "si_core_disable and ai_core_disable": nothing if
 * the core is in reset; else wait for the backplane, reset, then the flags.
 */
void bcm4360_si_core_disable(struct bcm4360_si *si, u32 bits)
{
	void *w = si->hw->wrap;

	/* step 1 */
	if (hw_read32(w, AI_RESETCTRL) & AIRC_RESET)
		return;

	/* step 2 */
	if (wrap_wait_idle(si, DISABLE_WAIT_US) != 0)
		wrap_wait_idle(si, DISABLE_WAIT_LONG_US);

	/* step 3 */
	hw_write32(w, AI_RESETCTRL, AIRC_RESET);
	(void)hw_read32(w, AI_RESETCTRL);
	hw_udelay(DISABLE_RESET_DELAY_US);

	/* step 4 */
	hw_write32(w, AI_IOCTRL, bits);
	(void)hw_read32(w, AI_IOCTRL);
	hw_udelay(DISABLE_DONE_DELAY_US);
}

/* docs/re/spec/chip.md, "si_wrapperreg and ai_wrap_reg" */
u32 bcm4360_si_wrapperreg(struct bcm4360_si *si, u32 offset, u32 mask, u32 val)
{
	return reg_mod32(si->hw->wrap, offset & ~AI_WRAP_REG_ALIGN, mask, val);
}

/* --- GPIO of ChipCommon ---------------------------------------------- */

/*
 * docs/re/spec/chip.md, "si_gpiocontrol, si_gpioouten, si_gpioout": on PCI
 * `mask` and `val` pass unchanged (the reservation of GPIOs applies to the
 * system bus only, not served here; `priority` matters only there).
 */
static u32 si_gpio_mod(struct bcm4360_si *si, u32 reg, u32 mask, u32 val, u8 priority)
{
	(void)priority;
	return si_cc_corereg(si, reg, mask, val);
}

/* docs/re/spec/chip.md, "si_gpiocontrol, si_gpioouten, si_gpioout" */
u32 bcm4360_si_gpiocontrol(struct bcm4360_si *si, u32 mask, u32 val, u8 priority)
{
	return si_gpio_mod(si, CC_GPIOCONTROL, mask, val, priority);
}

/* docs/re/spec/chip.md, "si_gpiocontrol, si_gpioouten, si_gpioout" */
u32 bcm4360_si_gpioouten(struct bcm4360_si *si, u32 mask, u32 val, u8 priority)
{
	return si_gpio_mod(si, CC_GPIOOUTEN, mask, val, priority);
}

/* docs/re/spec/chip.md, "si_gpiocontrol, si_gpioouten, si_gpioout" */
u32 bcm4360_si_gpioout(struct bcm4360_si *si, u32 mask, u32 val, u8 priority)
{
	return si_gpio_mod(si, CC_GPIOOUT, mask, val, priority);
}

/* docs/re/spec/chip.md, "si_gpioin" */
u32 bcm4360_si_gpioin(struct bcm4360_si *si)
{
	return si_cc_corereg(si, CC_GPIOIN, 0, 0);
}

/* docs/re/spec/chip.md, "si_gpioled, si_gpiotimerval" (gpiotimeroutmask) */
u32 bcm4360_si_gpioled(struct bcm4360_si *si, u32 mask, u32 val)
{
	if (si->ccrev < CCREV_GPIOTIMER)
		return SI_NOREG;
	return si_cc_corereg(si, CC_GPIOTIMEROUTMASK, mask, val);
}

/* docs/re/spec/chip.md, "si_gpioled, si_gpiotimerval" */
u32 bcm4360_si_gpiotimerval(struct bcm4360_si *si, u32 mask, u32 val)
{
	if (si->ccrev < CCREV_GPIOTIMER)
		return SI_NOREG;
	return si_cc_corereg(si, CC_GPIOTIMERVAL, mask, val);
}

/* --- crystal, device presence, SROM control, host interface ---------- */

/*
 * docs/re/spec/chip.md, "si_clkctl_xtal", old PCI bridges: crystal and PLL
 * through the GPIOs in the PCI configuration space.
 */
static s32 clkctl_xtal_pcigpio(struct bcm4360_si *si, u32 what, bool on)
{
	struct bcm4360_hw *hw = si->hw;
	u32 in, out, outen, bits = 0;

	in = hw_pcicfg_read(hw, PCICFG_GPIO_IN, PCICFG_SIZE);
	out = hw_pcicfg_read(hw, PCICFG_GPIO_OUT, PCICFG_SIZE);
	outen = hw_pcicfg_read(hw, PCICFG_GPIO_OUTEN, PCICFG_SIZE);
	if (what & XTAL_WHAT_XTAL)
		bits |= XTAL_GPIO_XTAL;
	if (what & XTAL_WHAT_PLL)
		bits |= XTAL_GPIO_PLL;

	if (!on) {
		/* crystal off (bit cleared), PLL off (bit set) */
		outen |= bits;
		out &= ~(bits & XTAL_GPIO_XTAL);
		out |= bits & XTAL_GPIO_PLL;
		hw_pcicfg_write(hw, PCICFG_GPIO_OUT, PCICFG_SIZE, out);
		hw_pcicfg_write(hw, PCICFG_GPIO_OUTEN, PCICFG_SIZE, outen);
		return 0;
	}

	if ((what & XTAL_WHAT_XTAL) && !(in & XTAL_GPIO_XTAL)) {
		/* crystal on, the PLL kept off until it has settled */
		out |= bits;
		outen |= bits;
		hw_pcicfg_write(hw, PCICFG_GPIO_OUT, PCICFG_SIZE, out);
		hw_pcicfg_write(hw, PCICFG_GPIO_OUTEN, PCICFG_SIZE, outen);
		hw_udelay(XTAL_ON_DELAY_US);
	}
	if (what & XTAL_WHAT_PLL) {
		out &= ~XTAL_GPIO_PLL;
		hw_pcicfg_write(hw, PCICFG_GPIO_OUT, PCICFG_SIZE, out);
		hw_udelay(PLL_ON_DELAY_US);
	}
	return 0;
}

/*
 * docs/re/spec/chip.md, "si_clkctl_xtal": -1 without access behind a PCIe
 * bridge (this card).
 */
s32 bcm4360_si_clkctl_xtal(struct bcm4360_si *si, u32 what, bool on)
{
	if (si->bustype != SI_BUS_PCI)
		return si->bustype == SI_BUS_TYPE_2 ? 0 : SI_XTAL_NONE;
	if (si->buscoreid == CORE_ID_PCIE2 || si->buscoreid == CORE_ID_PCIE)
		return SI_XTAL_NONE;
	return clkctl_xtal_pcigpio(si, what, on);
}

/*
 * docs/re/spec/chip.md, "si_deviceremoved": on PCI one read of the vendor
 * id; the card is gone when it is not Broadcom's.
 */
bool bcm4360_si_deviceremoved(struct bcm4360_si *si)
{
	u32 id;

	if (si->bustype != SI_BUS_PCI)
		return false;
	id = hw_pcicfg_read(si->hw, PCICFG_ID, PCICFG_SIZE);
	return (id & PCI_VENDOR_ID_MASK) != PCI_VENDOR_BROADCOM;
}

/*
 * docs/re/spec/chip.md, "si_get_sromctl": ChipCommon's SROM control register
 * through window 1, the current core restored.
 */
u32 bcm4360_si_get_sromctl(struct bcm4360_si *si)
{
	u32 orig = *si->curidx;
	void *p;
	u32 v = 0;

	p = bcm4360_si_setcoreidx(si, 0);
	if (p)
		v = hw_read32(p, CC_SROMCONTROL);
	bcm4360_si_setcoreidx(si, orig);
	return v;
}

/*
 * docs/re/spec/chip.md, "si_set_sromctl": write ChipCommon's SROM control
 * register through window 1. Below ChipCommon revision 32 the error is
 * returned without restoring the current core, as specified.
 */
s32 bcm4360_si_set_sromctl(struct bcm4360_si *si, u32 value)
{
	u32 orig = *si->curidx;
	void *p;

	p = bcm4360_si_setcoreidx(si, 0);
	if (bcm4360_si_corerev(si) < CCREV_SROMCTL)
		return SI_ERR_SROMCTL;
	if (p)
		hw_write32(p, CC_SROMCONTROL, value);
	bcm4360_si_setcoreidx(si, orig);
	return 0;
}

/* docs/re/spec/chip.md, "si_chip_hostif": no access */
u32 bcm4360_si_chip_hostif(const struct bcm4360_si *si)
{
	switch (si->chip) {
	case CHIP_ID_4360:
	case CHIP_ID_43460:
	case CHIP_ID_43526:
		if (!(si->chippkg & HOSTIF_4360_PKG_BIT) || !(si->chipst & HOSTIF_4360_ST_BIT))
			return HOSTIF_PCIE;
		return HOSTIF_2;
	case CHIP_ID_4335:
		if (si->chipst & HOSTIF_4335_ST_4)
			return HOSTIF_4;
		if (si->chipst & HOSTIF_4335_ST_2)
			return HOSTIF_2;
		return (si->chipst >> HOSTIF_4335_ST_SHIFT) & 1;
	default:
		return 0;
	}
}

/*
 * docs/re/spec/chip.md, "si_fabid and si_otp_fabid": 0 without access for
 * the 4360. Of the other chips only those with the fab id in CC(0xf8) are
 * served; 0x4330 (CC(0x28), extraction not specified) and the chips whose
 * fab id is in the OTP (si_otp_fabid, srom-otp.md) get 0 here.
 */
u16 bcm4360_si_fabid(struct bcm4360_si *si)
{
	switch (si->chip) {
	case CHIP_ID_4324:
	case CHIP_ID_4334:
	case CHIP_ID_4335:
	case CHIP_ID_4350:
	case CHIP_ID_43242:
	case CHIP_ID_43243:
		return si_cc_corereg(si, CC_0xf8, 0, 0) & CC_FABID_0xf8_MASK;
	default:
		return 0;
	}
}

/* docs/re/spec/chip.md, "si_pci_war16165": no access */
bool bcm4360_si_pci_war16165(const struct bcm4360_si *si)
{
	return si->bustype == SI_BUS_PCI && si->buscoreid == CORE_ID_PCI &&
	       si->buscorerev < PCI_WAR16165_REV;
}

/* docs/re/spec/chip.md, "si_ldo_war": the devices of the work-around */
static bool ldo_war_device(u32 devid)
{
	switch (devid) {
	case LDO_DEVID_10F6:
	case LDO_DEVID_4322:
	case LDO_DEVID_432B:
	case LDO_DEVID_432C:
	case LDO_DEVID_432D:
		return true;
	default:
		return false;
	}
}

/*
 * docs/re/spec/chip.md, "si_ldo_war": one configuration read; for some old
 * devices of PCI revision 0 a regulator sequence through ChipCommon in
 * window 1. Result 1, 0 when the wait at the end timed out.
 */
bool bcm4360_si_ldo_war(struct bcm4360_si *si, u32 devid)
{
	struct bcm4360_hw *hw = si->hw;
	u32 rev, win, waited = 0;

	rev = hw_pcicfg_read(hw, PCICFG_REV, PCICFG_SIZE);
	if (!ldo_war_device(devid) || (rev & PCI_REV_ID_MASK) != 0)
		return true;

	win = hw_pcicfg_read(hw, PCICFG_BAR0_WIN, PCICFG_SIZE);
	hw_pcicfg_write(hw, PCICFG_BAR0_WIN, PCICFG_SIZE, LDO_CC_BASE);
	hw_write32(hw->bar0, CC_0x658, 0);
	hw_write32(hw->bar0, CC_0x65c, LDO_CC_0x65c_VAL);
	hw_udelay(LDO_DELAY_US);
	hw_write32(hw->bar0, CC_0x618, LDO_CC_0x618_VAL);
	while (!(hw_pcicfg_read(hw, PCICFG_0xa8, PCICFG_SIZE) & LDO_PCICFG_0xa8_BIT)) {
		if (waited >= LDO_WAIT_US)
			return false;
		hw_udelay(SPINWAIT_STEP_US);
		waited += SPINWAIT_STEP_US;
	}
	hw_pcicfg_write(hw, PCICFG_BAR0_WIN, PCICFG_SIZE, win);
	return true;
}

/*
 * docs/re/spec/chip.md, "si_tcm_size": the size of the ARM CR4's tightly
 * coupled memory, from its bank registers; the core is reset for the reads
 * and disabled again if it was down. The core switch is bracketed by the
 * interrupt callbacks.
 */
u32 bcm4360_si_tcm_size(struct bcm4360_si *si)
{
	u32 s, orig, cap, nb, i, size = 0;
	bool up;
	void *p;

	/* steps 1, 2 */
	s = si_intr_off(si);
	orig = *si->curidx;
	p = bcm4360_si_setcore(si, CORE_ID_ARMCR4, 0);
	if (p) {
		/* step 3 */
		up = bcm4360_si_iscoreup(si);
		if (!up)
			bcm4360_si_core_reset(si, ARMCR4_RESET_BITS, ARMCR4_RESET_BITS);

		/* steps 4, 5 */
		cap = hw_read32(p, ARMCR4_CAP);
		nb = ((cap >> ARMCR4_CAP_NB2_SHIFT) & ARMCR4_CAP_NB_MASK) +
		     (cap & ARMCR4_CAP_NB_MASK);
		for (i = 0; i < nb; i++) {
			hw_write32(p, ARMCR4_BANKIDX, i);
			size += ((hw_read32(p, ARMCR4_BANKINFO) & ARMCR4_BANKINFO_MASK) + 1) *
				ARMCR4_BANK_UNIT;
		}

		/* step 6 */
		if (!up)
			bcm4360_si_core_disable(si, 0);
		bcm4360_si_setcoreidx(si, orig);
	}

	/* step 7 */
	si_intr_restore(si, s);
	return size;
}
