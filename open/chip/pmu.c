// SPDX-License-Identifier: ISC
/*
 * Power management unit (PMU) and clocks of the BCM4360.
 *
 * Written from docs/re/spec/pmu.md, for the chips 0x4360 and 0x4352 on a
 * PCIe card. ChipCommon and the PCIe core are reached through their fixed
 * windows.
 */
#include <bcm4360/pmu.h>

/* corereg() with this mask replaces the whole register */
#define COREREG_ALL		0xffffffff

/* si_pmu_measure_alpclk: the first PMU revision that can measure the crystal */
#define PMU_REV_XTALFREQ	10
/*
 * si_pmu_measure_alpclk: CC_PMU_XTALFREQ counts the periods of the crystal
 * during 4 periods of the low power oscillator of 32768 Hz; the result is
 * rounded to a multiple of 100 kHz.
 */
#define LPO_HZ			32768
#define XTALFREQ_LPO_PERIODS	4
#define XTALFREQ_ROUND_HZ	100000
#define XTALFREQ_MEASURE_US	1000	/* time given to the measurement */
#define HZ_PER_KHZ		1000
#define KHZ_PER_MHZ		1000

/*
 * The baseband PLL of the chips 0x4360 and 0x4352. The arithmetic of the
 * driver is that of a crystal of 40 MHz.
 */
#define PLL_4360_FIRST_REV	3	/* first chip rev whose PLL is programmed */
#define PLL_4360_VCO_MHZ	960
#define PLL_4360_VCO_FRAC	98	/* in units of 100 Hz */
#define PLL_XTAL_MHZ		40
#define PLL_UNITS_PER_MHZ	10000	/* frequencies are in units of 100 Hz */
#define PLL_XTAL_UNITS		(PLL_XTAL_MHZ * PLL_UNITS_PER_MHZ)

/*
 * si_pmu_res_init for the chips 0x4360 and 0x4352. The meaning of the values
 * of the PLL control registers is not known.
 */
#define RES_4360_REV_MASKS	3	/* first chip rev with a maximum resource mask */
#define RES_4360_REV_NEW	4	/* first chip rev with the longer table */
#define PLLCTL_0x6_UNTIL_REV3	0x09048562	/* also PMU_PLLCTL[0xe] */
#define PLLCTL_0x6_FROM_REV4	0x080004e2	/* also PMU_PLLCTL[0xe] */
#define PLLCTL_0x7_FROM_REV4	0x0000000e	/* also PMU_PLLCTL[0xf] */
#define RES_INIT_DELAY_US	2000	/* time for the resources to settle */

/* si_pmu_res_deps looks at the resources 0..30 */
#define RES_DEPS_COUNT		31

/* si_pmu_alp_clock: the ALP clock is the clock of the crystal, in Hz */
#define ALP_HZ_XTAL_40MHZ	40000000
#define ALP_HZ_XTAL_20MHZ	20000000

/*
 * si_pmu_ilp_clock: the periods of the ILP clock are counted for 10 ms, a
 * hundredth of a second
 */
#define ILP_MEASURE_US		10000
#define ILP_MEASURES_PER_SEC	100

/* si_pmu_si_clock: the VCO of the baseband PLL has 960 MHz */
#define PLL_4360_VCO_KHZ	(PLL_4360_VCO_MHZ * KHZ_PER_MHZ)

/* si_pmu_get_bb_vcofreq: rounds the fraction of the divider to the nearest */
#define PLL_FRAC_HALF		(1U << (PMU_PLLCTL_DIV_FRAC_BITS - 1))
#define U32_MAX_VALUE		0xffffffff

/* si_pmu_fast_pwrup_delay of the chips 0x4360 and 0x4352, microseconds */
#define PWRUP_DELAY_US_UNTIL_REV3	1500
#define PWRUP_DELAY_US_FROM_REV4	3000

/* SPINWAIT: the condition is looked at every 10 microseconds */
#define SPINWAIT_STEP_US	10

/* si_pmu_otp_power, microseconds */
#define OTP_POWER_DELAY_US	1000	/* before the resource is looked at */
#define OTP_POWER_WAIT_US	20000	/* longest wait for the resource */
#define OTP_READY_WAIT_US	3000	/* longest wait for the OTP status */

/* docs/re/spec/pmu.md, "Resource tables of the 4360": an up/down timer */
struct pmu_res_updown {
	u8 res;		/* number of the resource */
	u32 timer;	/* value for CC_PMU_RES_UPDN_TIMER */
};

/* docs/re/spec/pmu.md, "Resource tables of the 4360": chip rev < 4 */
static const struct pmu_res_updown pmu_res_updown_4360[] = {
	{ 6, 0x00200001 },
};

/* docs/re/spec/pmu.md, "Resource tables of the 4360": chip rev >= 4 */
static const struct pmu_res_updown pmu_res_updown_4360_rev4[] = {
	{ 0, 0x00000001 },
	{ 1, 0x00000001 },
	{ 2, 0x00000001 },
	{ 3, 0x00000001 },
	{ 4, 0x00860002 },
	{ 5, 0x00000000 },
	{ 6, 0x00020001 },
	{ 7, 0x00080001 },
	{ 8, 0x00000000 },
};

/*
 * docs/re/spec/pmu.md, notation at the top, "corereg(reg, mask, val)": the
 * access pattern of si_corereg on a register of the core at `core`. `val` is
 * not limited to `mask`.
 */
static u32 pmu_corereg(void *core, u32 reg, u32 mask, u32 val)
{
	u32 old;

	if (mask || val) {
		old = hw_read32(core, reg);
		hw_write32(core, reg, (old & ~mask) | val);
	}
	return hw_read32(core, reg);
}

/*
 * docs/re/spec/pmu.md, "si_pmu_chipcontrol / si_pmu_regcontrol /
 * si_pmu_pllcontrol": the access to an indirect register through its address
 * and its data register.
 */
static u32 pmu_indirect(struct bcm4360_pmu *p, u32 addr_reg, u32 data_reg,
			u32 reg, u32 mask, u32 val)
{
	pmu_corereg(p->hw->cc, addr_reg, COREREG_ALL, reg);
	return pmu_corereg(p->hw->cc, data_reg, mask, val);
}

/* docs/re/spec/pmu.md, "si_pmu_chipcontrol / si_pmu_regcontrol / si_pmu_pllcontrol" */
u32 bcm4360_pmu_chipcontrol(struct bcm4360_pmu *p, u32 reg, u32 mask, u32 val)
{
	return pmu_indirect(p, CC_PMU_CHIPCTL_ADDR, CC_PMU_CHIPCTL_DATA, reg, mask, val);
}

/* docs/re/spec/pmu.md, "si_pmu_chipcontrol / si_pmu_regcontrol / si_pmu_pllcontrol" */
u32 bcm4360_pmu_regcontrol(struct bcm4360_pmu *p, u32 reg, u32 mask, u32 val)
{
	return pmu_indirect(p, CC_PMU_REGCTL_ADDR, CC_PMU_REGCTL_DATA, reg, mask, val);
}

/* docs/re/spec/pmu.md, "si_pmu_chipcontrol / si_pmu_regcontrol / si_pmu_pllcontrol" */
u32 bcm4360_pmu_pllcontrol(struct bcm4360_pmu *p, u32 reg, u32 mask, u32 val)
{
	return pmu_indirect(p, CC_PMU_PLLCTL_ADDR, CC_PMU_PLLCTL_DATA, reg, mask, val);
}

/* docs/re/spec/pmu.md, "si_pmu_pllupd" */
void bcm4360_pmu_pllupd(struct bcm4360_pmu *p)
{
	pmu_corereg(p->hw->cc, CC_PMU_CTL, PMU_CTL_PLL_UPD, PMU_CTL_PLL_UPD);
}

/*
 * docs/re/spec/pmu.md, "si_pmu_init". Step 2 is for another chip and not
 * described.
 */
void bcm4360_pmu_init(struct bcm4360_pmu *p)
{
	u32 ctl;

	if (p->info.pmurev == 0)
		return;

	ctl = cc_read32(p->hw, CC_PMU_CTL);
	if (p->info.pmurev == 1)
		ctl &= ~PMU_CTL_NOILP_ON_WAIT;
	else
		ctl |= PMU_CTL_NOILP_ON_WAIT;
	cc_write32(p->hw, CC_PMU_CTL, ctl);
}

/*
 * docs/re/spec/pmu.md, the chips in the scope of docs/re/tasks/pmu.md: what
 * the specification says about the chips 0x4360 and 0x4352 is done for them.
 */
static bool pmu_is_4360(struct bcm4360_pmu *p)
{
	return p->info.chip == BCM4360_CHIP_ID_4360 ||
	       p->info.chip == BCM4360_CHIP_ID_4352;
}

/*
 * docs/re/spec/pmu.md, "si_pmu_measure_alpclk". Result: the frequency of the
 * crystal in kHz, 0 if it cannot be measured.
 */
u32 bcm4360_pmu_measure_alpclk(struct bcm4360_pmu *p)
{
	u32 count, hz;

	if (p->info.pmurev < PMU_REV_XTALFREQ)
		return 0;
	if (!(cc_read32(p->hw, CC_PMU_STATUS) & PMU_STATUS_EXT_LPO_AVAIL))
		return 0;

	cc_write32(p->hw, CC_PMU_XTALFREQ, PMU_XTALFREQ_START);
	hw_udelay(XTALFREQ_MEASURE_US);
	count = cc_read32(p->hw, CC_PMU_XTALFREQ) & PMU_XTALFREQ_COUNT_MASK;
	cc_write32(p->hw, CC_PMU_XTALFREQ, 0);

	hz = count * (LPO_HZ / XTALFREQ_LPO_PERIODS);
	return ((hz + XTALFREQ_ROUND_HZ / 2) / XTALFREQ_ROUND_HZ) *
	       (XTALFREQ_ROUND_HZ / HZ_PER_KHZ);
}

/*
 * docs/re/spec/pmu.md, "sub_016dbd = si_set_bb_vcofreq_frac": set the VCO of
 * the baseband PLL to `vco` MHz + `frac` * 100 Hz. The PLL is only programmed
 * while the HT clock is not in use.
 */
static void pmu_set_bb_vcofreq_frac(struct bcm4360_pmu *p, u32 vco, u32 frac)
{
	u32 rem, mode, div;
	u64 frac_bits;

	if (cc_read32(p->hw, CC_CLK_CTL_ST) & CLK_CTL_ST_HT_AVAIL)
		return;

	rem = (vco * PLL_UNITS_PER_MHZ + frac) % PLL_XTAL_UNITS;
	mode = rem ? PMU_PLLCTL_DIV_MODE_FRAC : PMU_PLLCTL_DIV_MODE_INT;
	div = vco / PLL_XTAL_MHZ;

	bcm4360_pmu_pllcontrol(p, PMU_PLLCTL_DIV, COREREG_ALL,
			       PMU_PLLCTL_DIV_BIT_0x1 |
			       mode << PMU_PLLCTL_DIV_MODE_SHIFT |
			       div << PMU_PLLCTL_DIV_INT_SHIFT);
	if (mode != PMU_PLLCTL_DIV_MODE_INT) {
		frac_bits = (u64)rem << PMU_PLLCTL_DIV_FRAC_BITS;
		bcm4360_pmu_pllcontrol(p, PMU_PLLCTL_DIV_FRAC, COREREG_ALL,
				       frac_bits / PLL_XTAL_UNITS);
	}
	bcm4360_pmu_pllupd(p);
}

/*
 * docs/re/spec/pmu.md, "si_pmu_pll_init". `xtalfreq` (kHz) is not used for
 * these chips: 40 MHz are built into the arithmetic of the PLL.
 */
void bcm4360_pmu_pll_init(struct bcm4360_pmu *p, u32 xtalfreq)
{
	if (!pmu_is_4360(p) || p->info.chiprev < PLL_4360_FIRST_REV)
		return;

	pmu_set_bb_vcofreq_frac(p, PLL_4360_VCO_MHZ, PLL_4360_VCO_FRAC);
}

/*
 * docs/re/spec/pmu.md, "si_pmu_res_init", step 2: the up/down timers of the
 * table of the chip, from the last entry to the first.
 */
static void pmu_res_init_timers(struct bcm4360_pmu *p)
{
	const struct pmu_res_updown *table;
	u32 n;

	if (!pmu_is_4360(p))
		return;

	if (p->info.chiprev < RES_4360_REV_NEW) {
		table = pmu_res_updown_4360;
		n = ARRAY_SIZE(pmu_res_updown_4360);
	} else {
		table = pmu_res_updown_4360_rev4;
		n = ARRAY_SIZE(pmu_res_updown_4360_rev4);
	}

	while (n--) {
		cc_write32(p->hw, CC_PMU_RES_SELECT, table[n].res);
		cc_write32(p->hw, CC_PMU_RES_UPDN_TIMER, table[n].timer);
	}
}

/*
 * docs/re/spec/pmu.md, "sub_0111b0 = si_pmu_res_masks", with the table of the
 * resource masks in "Resource tables of the 4360". A mask of 0 means: leave
 * the register as it is.
 */
static void pmu_res_masks(struct bcm4360_pmu *p, u32 *min, u32 *max)
{
	*min = 0;
	*max = 0;
	if (!pmu_is_4360(p))
		return;

	if (p->info.chiprev >= RES_4360_REV_NEW)
		*min = PMU_RES_4360_MIN_REV4;
	if (p->info.chiprev >= RES_4360_REV_MASKS &&
	    !(p->info.chipst & CHIPST_4360_BIT_0x20))
		*max = PMU_RES_4360_ALL;
}

/*
 * docs/re/spec/pmu.md, "sub_0118f2 = si_pmu_res_deps": the resources that
 * the resources `rsrcs` depend on themselves.
 */
static u32 pmu_res_deps_direct(struct bcm4360_pmu *p, u32 rsrcs)
{
	u32 deps = 0;
	u32 i;

	for (i = 0; i < RES_DEPS_COUNT; i++) {
		if (!(rsrcs & BIT(i)))
			continue;
		cc_write32(p->hw, CC_PMU_RES_SELECT, i);
		deps |= cc_read32(p->hw, CC_PMU_RES_DEP_MASK);
	}
	return deps;
}

/*
 * docs/re/spec/pmu.md, "sub_0118f2 = si_pmu_res_deps": the resources that
 * the resources `rsrcs` depend on; with `all` also those that these depend
 * on, and so on. What the specification describes as a call of the function
 * by itself is a loop here, with the same accesses in the same order. It ends
 * after as many levels as there are resources: without a circle in the
 * dependencies there cannot be more.
 */
static u32 pmu_res_deps(struct bcm4360_pmu *p, u32 rsrcs, bool all)
{
	u32 found = pmu_res_deps_direct(p, rsrcs);
	u32 deps = found;
	u32 level;

	if (!all)
		return deps;

	for (level = 1; found && level < RES_DEPS_COUNT; level++) {
		found = pmu_res_deps_direct(p, found);
		deps |= found;
	}
	return deps;
}

/*
 * docs/re/spec/pmu.md, "si_pmu_res_init", step 8: a PLL control register is
 * replaced with two plain writes.
 */
static void pmu_pllctl_write(struct bcm4360_pmu *p, u32 reg, u32 val)
{
	cc_write32(p->hw, CC_PMU_PLLCTL_ADDR, reg);
	cc_write32(p->hw, CC_PMU_PLLCTL_DATA, val);
}

/* docs/re/spec/pmu.md, "si_pmu_res_init", step 8: PLL and chip control registers */
static void pmu_res_init_pll(struct bcm4360_pmu *p)
{
	u32 ctl;

	if (!pmu_is_4360(p) || (p->info.chipst & CHIPST_4360_BIT_0x20))
		return;

	if (p->info.chiprev < RES_4360_REV_NEW) {
		pmu_pllctl_write(p, PMU_PLLCTL_0x6, PLLCTL_0x6_UNTIL_REV3);
		pmu_pllctl_write(p, PMU_PLLCTL_0xe, PLLCTL_0x6_UNTIL_REV3);
	} else {
		cc_write32(p->hw, CC_PMU_CHIPCTL_ADDR, PMU_CHIPCTL_0x1);
		ctl = cc_read32(p->hw, CC_PMU_CHIPCTL_DATA);
		cc_write32(p->hw, CC_PMU_CHIPCTL_DATA, ctl | PMU_CHIPCTL_0x1_BIT_0x800);

		pmu_pllctl_write(p, PMU_PLLCTL_0x6, PLLCTL_0x6_FROM_REV4);
		pmu_pllctl_write(p, PMU_PLLCTL_0x7, PLLCTL_0x7_FROM_REV4);
		pmu_pllctl_write(p, PMU_PLLCTL_0xe, PLLCTL_0x6_FROM_REV4);
		pmu_pllctl_write(p, PMU_PLLCTL_0xf, PLLCTL_0x7_FROM_REV4);
	}
	bcm4360_pmu_pllupd(p);
}

/*
 * docs/re/spec/pmu.md, "si_pmu_res_init", steps 9 to 11: the resource masks.
 * The resources are allowed before they are asked for.
 */
static void pmu_res_init_masks(struct bcm4360_pmu *p, u32 min, u32 max)
{
	u32 allow, old;

	if (max)
		max |= min;
	allow = max ? max : min;
	if (allow) {
		old = cc_read32(p->hw, CC_PMU_MAX_RES_MASK);
		cc_write32(p->hw, CC_PMU_MAX_RES_MASK, old | allow);
	}
	if (min)
		cc_write32(p->hw, CC_PMU_MIN_RES_MASK, min);
	if (max)
		cc_write32(p->hw, CC_PMU_MAX_RES_MASK, max);
}

/*
 * docs/re/spec/pmu.md, "si_pmu_res_init", step 12: the HT clock is requested
 * for the core with index 3, on these chips the PCIe core.
 */
static void pmu_res_init_ht_req(struct bcm4360_pmu *p)
{
	u32 val;

	if (!pmu_is_4360(p) || p->info.chiprev >= RES_4360_REV_NEW)
		return;

	val = pmu_corereg(p->hw->pcie, CORE_CLK_CTL_ST, 0, 0);
	pmu_corereg(p->hw->pcie, CORE_CLK_CTL_ST, COREREG_ALL, val | CLK_CTL_ST_HT_REQ);
}

/*
 * docs/re/spec/pmu.md, "si_pmu_res_init". The steps 1, 3 and 5 belong to the
 * variables that override registers, which are not implemented; step 4: these
 * chips have no dependency table.
 */
void bcm4360_pmu_res_init(struct bcm4360_pmu *p)
{
	u32 min, max;

	pmu_res_init_timers(p);
	pmu_res_masks(p, &min, &max);
	min |= pmu_res_deps(p, min, false);
	pmu_res_init_pll(p);
	pmu_res_init_masks(p, min, max);
	pmu_res_init_ht_req(p);
	hw_udelay(RES_INIT_DELAY_US);
}

/* docs/re/spec/pmu.md, "si_pmu_alp_clock". Result in Hz. */
u32 bcm4360_pmu_alp_clock(struct bcm4360_pmu *p)
{
	if (!pmu_is_4360(p))
		return 0;

	if (p->info.chipst & CHIPST_4360_XTAL_40MHZ)
		return ALP_HZ_XTAL_40MHZ;
	return ALP_HZ_XTAL_20MHZ;
}

/*
 * docs/re/spec/pmu.md, "si_pmu_ilp_clock": the timer of the PMU. It is read
 * again when two reads in a row do not agree.
 */
static u32 pmu_timer_read(struct bcm4360_pmu *p)
{
	u32 timer = cc_read32(p->hw, CC_PMU_TIMER);

	if (cc_read32(p->hw, CC_PMU_TIMER) != timer)
		timer = cc_read32(p->hw, CC_PMU_TIMER);
	return timer;
}

/*
 * docs/re/spec/pmu.md, "si_pmu_ilp_clock". Result in Hz. The clock is
 * measured once, p->ilp_hz keeps the result.
 */
u32 bcm4360_pmu_ilp_clock(struct bcm4360_pmu *p)
{
	u32 start, end;

	if (!p->ilp_hz) {
		start = pmu_timer_read(p);
		hw_udelay(ILP_MEASURE_US);
		end = pmu_timer_read(p);
		p->ilp_hz = (end - start) * ILP_MEASURES_PER_SEC;
	}
	return p->ilp_hz;
}

/*
 * docs/re/spec/pmu.md, "si_pmu_si_clock, si_pmu_cpu_clock": the clock of the
 * backplane in Hz. With a divider of 0 the object divides by 0; the result
 * is 0 here.
 */
u32 bcm4360_pmu_si_clock(struct bcm4360_pmu *p)
{
	u32 div;

	if (!pmu_is_4360(p))
		return 0;

	cc_write32(p->hw, CC_PMU_PLLCTL_ADDR, PMU_PLLCTL_BP_CLK);
	div = (cc_read32(p->hw, CC_PMU_PLLCTL_DATA) & PMU_PLLCTL_BP_CLK_DIV_MASK) >>
	      PMU_PLLCTL_BP_CLK_DIV_SHIFT;
	if (!div)
		return 0;
	return (PLL_4360_VCO_KHZ / div) * HZ_PER_KHZ;
}

/*
 * docs/re/spec/pmu.md, "si_pmu_get_bb_vcofreq": the frequency of the VCO of
 * the baseband PLL in units of 100 Hz for a crystal of `xtal_mhz` MHz; 0 when
 * it does not fit into 32 bit. With an integer divider of 0 the object
 * divides by 0; the result is 0 here.
 */
u32 bcm4360_pmu_get_bb_vcofreq(struct bcm4360_pmu *p, u32 xtal_mhz)
{
	u32 ctl, mode, div, frac, ref;
	u32 part = 0;

	if (!pmu_is_4360(p))
		return 0;

	ctl = bcm4360_pmu_pllcontrol(p, PMU_PLLCTL_DIV, 0, 0);
	mode = (ctl & PMU_PLLCTL_DIV_MODE_MASK) >> PMU_PLLCTL_DIV_MODE_SHIFT;
	div = ctl >> PMU_PLLCTL_DIV_INT_SHIFT;
	ref = xtal_mhz * PLL_UNITS_PER_MHZ;

	if (mode != PMU_PLLCTL_DIV_MODE_INT) {
		frac = bcm4360_pmu_pllcontrol(p, PMU_PLLCTL_DIV_FRAC, 0, 0);
		part = ((u64)ref * frac + PLL_FRAC_HALF) >> PMU_PLLCTL_DIV_FRAC_BITS;
	}

	if (!div)
		return 0;
	if ((s32)ref > (s32)((U32_MAX_VALUE - part) / div))
		return 0;
	return part + div * ref;
}

/* docs/re/spec/pmu.md, "si_pmu_fast_pwrup_delay". Result in microseconds. */
u32 bcm4360_pmu_fast_pwrup_delay(struct bcm4360_pmu *p)
{
	if (!pmu_is_4360(p))
		return 0;

	if (p->info.chiprev < RES_4360_REV_NEW)
		return PWRUP_DELAY_US_UNTIL_REV3;
	return PWRUP_DELAY_US_FROM_REV4;
}

/*
 * docs/re/spec/pmu.md, notation at the top, "SPINWAIT(cond, us)", for the
 * conditions that occur: wait while the bits `mask` of CC(reg) are not
 * `want`, at most `usec` microseconds.
 */
static void pmu_spinwait(struct bcm4360_pmu *p, u32 reg, u32 mask, u32 want, u32 usec)
{
	u32 waited = 0;

	while ((cc_read32(p->hw, reg) & mask) != want && waited < usec) {
		hw_udelay(SPINWAIT_STEP_US);
		waited += SPINWAIT_STEP_US;
	}
}

/* docs/re/spec/pmu.md, "si_pmu_otp_power" */
void bcm4360_pmu_otp_power(struct bcm4360_pmu *p, bool on)
{
	u32 rsrc = BIT(PMU_RES_4360_OTP);
	u32 deps, min, max, old;

	if (!pmu_is_4360(p))
		return;

	deps = pmu_res_deps(p, rsrc, true);
	pmu_res_masks(p, &min, &max);
	deps &= ~min;

	old = cc_read32(p->hw, CC_PMU_MIN_RES_MASK);
	if (on) {
		cc_write32(p->hw, CC_PMU_MIN_RES_MASK, old | rsrc | deps);
		hw_udelay(OTP_POWER_DELAY_US);
		pmu_spinwait(p, CC_PMU_RES_STATE, rsrc, rsrc, OTP_POWER_WAIT_US);
	} else {
		cc_write32(p->hw, CC_PMU_MIN_RES_MASK, old & ~(rsrc | deps));
	}

	pmu_spinwait(p, CC_OTP_STATUS, CC_OTP_STATUS_READY,
		     on ? CC_OTP_STATUS_READY : 0, OTP_READY_WAIT_US);
}

/* docs/re/spec/pmu.md, "si_pmu_is_otp_powered" */
bool bcm4360_pmu_is_otp_powered(struct bcm4360_pmu *p)
{
	if (!pmu_is_4360(p))
		return false;

	return (cc_read32(p->hw, CC_PMU_RES_STATE) & BIT(PMU_RES_4360_OTP)) != 0;
}

/* docs/re/spec/pmu.md, "si_pmu_rfldo": the supply of the radio */
void bcm4360_pmu_rfldo(struct bcm4360_pmu *p, bool on)
{
	if (!pmu_is_4360(p))
		return;

	bcm4360_pmu_regcontrol(p, PMU_REGCTL_RFLDO, PMU_REGCTL_RFLDO_OFF,
			       on ? 0 : PMU_REGCTL_RFLDO_OFF);
}
