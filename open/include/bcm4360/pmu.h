/* SPDX-License-Identifier: ISC */
/*
 * Power management unit (PMU) and clocks of the BCM4360.
 *
 * Specification: docs/re/spec/pmu.md. The notation CC(o), PCIE(o),
 * PMU_CHIPCTL[n], PMU_REGCTL[n] and PMU_PLLCTL[n] in the comments is the one
 * of docs/re/spec/access.md.
 *
 * For the chips 0x4360 and 0x4352 on a PCIe card. ChipCommon and the PCIe
 * core are reached through their fixed windows. What the specification says
 * about one chip only is done for these two chips only: for another chip such
 * a function makes no access and returns 0.
 */
#ifndef BCM4360_PMU_H
#define BCM4360_PMU_H

#include <bcm4360/types.h>
#include <bcm4360/hw.h>

/* the chips this code handles, chip id */
#define BCM4360_CHIP_ID_4360	0x4360
#define BCM4360_CHIP_ID_4352	0x4352

/*
 * Registers of ChipCommon, CC(o). The registers from 0x600 on are those of
 * the PMU. A register whose purpose is not known is named by its address.
 */
#define CC_CAPS			0x004	/* capabilities */
#define CC_OTP_STATUS		0x010
#define CC_CHIP_STATUS		0x02c
#define CC_CAPS_EXT		0x0ac	/* extended capabilities */
#define CC_CLK_CTL_ST		0x1e0	/* clock control and status */
#define CC_PMU_CTL		0x600
#define CC_PMU_CAPS		0x604
#define CC_PMU_STATUS		0x608
#define CC_PMU_RES_STATE	0x60c	/* the resources that are up */
#define CC_PMU_TIMER		0x614	/* counts periods of the ILP clock */
#define CC_PMU_MIN_RES_MASK	0x618
#define CC_PMU_MAX_RES_MASK	0x61c
#define CC_PMU_RES_SELECT	0x620	/* the resource the next two are of */
#define CC_PMU_RES_DEP_MASK	0x624	/* the resources it depends on */
#define CC_PMU_RES_UPDN_TIMER	0x628	/* its up/down timer */
#define CC_PMU_CHIPCTL_ADDR	0x650	/* PMU_CHIPCTL[n]: n */
#define CC_PMU_CHIPCTL_DATA	0x654
#define CC_PMU_REGCTL_ADDR	0x658	/* PMU_REGCTL[n]: n */
#define CC_PMU_REGCTL_DATA	0x65c
#define CC_PMU_PLLCTL_ADDR	0x660	/* PMU_PLLCTL[n]: n */
#define CC_PMU_PLLCTL_DATA	0x664
#define CC_PMU_XTALFREQ		0x66c	/* measurement of the crystal frequency */

/* every core has a clock control and status register at this offset */
#define CORE_CLK_CTL_ST		CC_CLK_CTL_ST

/* CC_CAPS */
#define CC_CAPS_PMU		0x10000000	/* the chip has a PMU */

/* CC_OTP_STATUS */
#define CC_OTP_STATUS_READY	0x00001000

/* CC_CHIP_STATUS of the chips 0x4360 and 0x4352 */
#define CHIPST_4360_XTAL_40MHZ	0x00000001	/* the crystal has 40 MHz */
#define CHIPST_4360_BIT_0x20	0x00000020	/* purpose unknown */

/* CC_CLK_CTL_ST */
#define CLK_CTL_ST_FORCE_HT	0x00000002
#define CLK_CTL_ST_HT_REQ	0x00000010	/* request the HT clock */
#define CLK_CTL_ST_HT_AVAIL	0x00020000	/* the HT clock is available */

/* CC_PMU_CTL */
#define PMU_CTL_ALP_REQ_EN	0x00000080
#define PMU_CTL_HT_REQ_EN	0x00000100
#define PMU_CTL_NOILP_ON_WAIT	0x00000200
#define PMU_CTL_PLL_UPD		0x00000400	/* latch the PLL control registers */

/* CC_PMU_CAPS */
#define PMU_CAPS_REV_MASK	0x000000ff

/* CC_PMU_STATUS */
#define PMU_STATUS_EXT_LPO_AVAIL 0x00000100	/* external low power oscillator */

/* CC_PMU_XTALFREQ */
#define PMU_XTALFREQ_START	0x80000000	/* start measuring */
#define PMU_XTALFREQ_COUNT_MASK	0x00001fff	/* periods of the crystal counted */

/*
 * Resources of the chips 0x4360 and 0x4352: bits of CC_PMU_RES_STATE and of
 * the resource masks. The purposes of the resources 0..7 are not known.
 */
#define PMU_RES_4360_OTP	8		/* power supply of the OTP */
#define PMU_RES_4360_MIN_REV4	0x00000103	/* kept up from chip rev 4 on */
#define PMU_RES_4360_ALL	0x000001ff	/* the resources the driver allows */

/* PMU_CHIPCTL[n] */
#define PMU_CHIPCTL_0x1			1
#define PMU_CHIPCTL_0x1_BIT_0x800	0x00000800	/* purpose unknown */

/* PMU_REGCTL[n] */
#define PMU_REGCTL_RFLDO		0	/* holds the switch of the radio supply */
#define PMU_REGCTL_RFLDO_OFF		0x00000002	/* 1 = radio supply off */

/* PMU_PLLCTL[n] */
#define PMU_PLLCTL_DIV			2	/* divider of the baseband PLL */
#define PMU_PLLCTL_DIV_FRAC		3	/* its fraction, in 1/2^24 */
#define PMU_PLLCTL_BP_CLK		5	/* holds the divider of the backplane clock */
#define PMU_PLLCTL_0x6			0x6
#define PMU_PLLCTL_0x7			0x7
#define PMU_PLLCTL_0xe			0xe
#define PMU_PLLCTL_0xf			0xf

/* PMU_PLLCTL_DIV */
#define PMU_PLLCTL_DIV_BIT_0x1		0x00000001	/* set by the driver, purpose unknown */
#define PMU_PLLCTL_DIV_MODE_MASK	0x00000070
#define PMU_PLLCTL_DIV_MODE_SHIFT	4
#define PMU_PLLCTL_DIV_MODE_INT		0	/* integer divider */
#define PMU_PLLCTL_DIV_MODE_FRAC	3	/* divider with a fraction */
#define PMU_PLLCTL_DIV_INT_SHIFT	7	/* the integer divider: bits 7 and above */

/* PMU_PLLCTL_DIV_FRAC */
#define PMU_PLLCTL_DIV_FRAC_BITS	24

/* PMU_PLLCTL_BP_CLK */
#define PMU_PLLCTL_BP_CLK_DIV_MASK	0x0000ff00
#define PMU_PLLCTL_BP_CLK_DIV_SHIFT	8

/* identification of the chip, read at attach (docs/re/spec/pmu.md, "Overview", 1.2) */
struct bcm4360_chip_info {
	u32 chip;		/* chip id, 0x4360 */
	u32 chiprev;
	u32 chippkg;
	u32 chipst;		/* chip status, CC(0x2c) */
	u32 ccrev;		/* revision of ChipCommon */
	u32 cccaps;		/* CC(0x04) */
	u32 cccaps_ext;		/* CC(0xac) */
	u32 pmurev;		/* bits 0..7 of CC(0x604) */
	u32 pmucaps;		/* CC(0x604) */
};

/*
 * The PMU of one card. The platform fills in `hw` and `info` and sets the
 * rest to 0 before it calls the functions below.
 */
struct bcm4360_pmu {
	struct bcm4360_hw *hw;
	struct bcm4360_chip_info info;
	u32 ilp_hz;		/* measured ILP clock, 0 = not measured yet */
};

/* the accessors of the indirect registers; result: the value read last */
u32 bcm4360_pmu_chipcontrol(struct bcm4360_pmu *p, u32 reg, u32 mask, u32 val);
u32 bcm4360_pmu_regcontrol(struct bcm4360_pmu *p, u32 reg, u32 mask, u32 val);
u32 bcm4360_pmu_pllcontrol(struct bcm4360_pmu *p, u32 reg, u32 mask, u32 val);
void bcm4360_pmu_pllupd(struct bcm4360_pmu *p);

void bcm4360_pmu_init(struct bcm4360_pmu *p);			/* si_pmu_init */
u32 bcm4360_pmu_measure_alpclk(struct bcm4360_pmu *p);		/* kHz */
void bcm4360_pmu_pll_init(struct bcm4360_pmu *p, u32 xtalfreq);	/* si_pmu_pll_init */
void bcm4360_pmu_res_init(struct bcm4360_pmu *p);		/* si_pmu_res_init */

u32 bcm4360_pmu_alp_clock(struct bcm4360_pmu *p);		/* Hz */
u32 bcm4360_pmu_ilp_clock(struct bcm4360_pmu *p);		/* Hz */
u32 bcm4360_pmu_si_clock(struct bcm4360_pmu *p);		/* Hz */
u32 bcm4360_pmu_get_bb_vcofreq(struct bcm4360_pmu *p, u32 xtal_mhz);
u32 bcm4360_pmu_fast_pwrup_delay(struct bcm4360_pmu *p);	/* microseconds */

void bcm4360_pmu_otp_power(struct bcm4360_pmu *p, bool on);	/* si_pmu_otp_power */
bool bcm4360_pmu_is_otp_powered(struct bcm4360_pmu *p);
void bcm4360_pmu_rfldo(struct bcm4360_pmu *p, bool on);

#endif
