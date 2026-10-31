// SPDX-License-Identifier: ISC
/*
 * Hybrid bridge, layer "pmu": the power management unit and clock functions
 * of Broadcom's object (hndpmu.c) are done by the open code (open/chip/pmu.c,
 * written from docs/re/spec/pmu.md).
 *
 * The open code keeps one piece of state across calls, the measured ILP
 * clock; the object keeps it in a static variable of its own. So the bridge
 * keeps one struct bcm4360_pmu per card, found by the object's `sih`, and
 * fills its identification from the object's `sih` on every call.
 *
 * Integer arguments are taken as u32 and truncated (see access.c).
 */
#include <bcm4360/pmu.h>
#include "blob_abi.h"

#define PMU_CARDS	4

struct pmu_card {
	void *sih;
	struct bcm4360_hw hw;
	struct bcm4360_pmu pmu;
};

static struct pmu_card pmu_cards[PMU_CARDS];

static struct bcm4360_pmu *pmu_of(void *sih)
{
	struct pmu_card *c = NULL;
	struct bcm4360_chip_info *info;
	u8 *bar0;
	int i;

	for (i = 0; i < PMU_CARDS; i++) {
		if (pmu_cards[i].sih == sih) {
			c = &pmu_cards[i];
			break;
		}
		if (!c && !pmu_cards[i].sih)
			c = &pmu_cards[i];
	}
	if (!c)
		c = &pmu_cards[0];	/* more cards than slots: share the first */
	if (c->sih != sih) {
		hw_memset(c, 0, sizeof(*c));
		c->sih = sih;
	}
	bar0 = BLOB_PTR(sih, SIH_REGS);
	c->hw.bar0 = bar0;
	c->hw.pdev = BLOB_PTR(sih, SIH_OSH);
	c->hw.d11 = bar0;
	c->hw.wrap = bar0 + BAR0_WRAP;
	c->hw.pcie = bar0 + BAR0_PCIE;
	c->hw.cc = bar0 + BAR0_CC;
	c->pmu.hw = &c->hw;
	info = &c->pmu.info;
	info->chip = BLOB_U32(sih, SIH_CHIP);
	info->chiprev = BLOB_U32(sih, SIH_CHIPREV);
	info->chippkg = BLOB_U32(sih, SIH_CHIPPKG);
	info->chipst = BLOB_U32(sih, SIH_CHIPST);
	info->ccrev = BLOB_U32(sih, SIH_CCREV);
	info->cccaps = BLOB_U32(sih, SIH_CCCAPS);
	info->cccaps_ext = BLOB_U32(sih, SIH_CCCAPS_EXT);
	info->pmurev = BLOB_U32(sih, SIH_PMUREV);
	info->pmucaps = BLOB_U32(sih, SIH_PMUCAPS);
	return &c->pmu;
}

u32 si_pmu_chipcontrol(void *sih, u32 reg, u32 mask, u32 val);
u32 si_pmu_regcontrol(void *sih, u32 reg, u32 mask, u32 val);
u32 si_pmu_pllcontrol(void *sih, u32 reg, u32 mask, u32 val);
void si_pmu_pllupd(void *sih);
void si_pmu_init(void *sih, void *osh);
u32 si_pmu_measure_alpclk(void *sih, void *osh);
void si_pmu_pll_init(void *sih, void *osh, u32 xtalfreq);
void si_pmu_res_init(void *sih, void *osh);
u32 si_pmu_alp_clock(void *sih, void *osh);
u32 si_pmu_ilp_clock(void *sih, void *osh);
u32 si_pmu_si_clock(void *sih, void *osh);
u32 si_pmu_get_bb_vcofreq(void *sih, void *osh, u32 xtalfreq);
u32 si_pmu_fast_pwrup_delay(void *sih, void *osh);
void si_pmu_otp_power(void *sih, void *osh, u32 on);
u32 si_pmu_is_otp_powered(void *sih, void *osh);
void si_pmu_rfldo(void *sih, u32 on);

/* pmu.md: the indirect registers */
u32 si_pmu_chipcontrol(void *sih, u32 reg, u32 mask, u32 val)
{
	return bcm4360_pmu_chipcontrol(pmu_of(sih), reg, mask, val);
}

u32 si_pmu_regcontrol(void *sih, u32 reg, u32 mask, u32 val)
{
	return bcm4360_pmu_regcontrol(pmu_of(sih), reg, mask, val);
}

u32 si_pmu_pllcontrol(void *sih, u32 reg, u32 mask, u32 val)
{
	return bcm4360_pmu_pllcontrol(pmu_of(sih), reg, mask, val);
}

void si_pmu_pllupd(void *sih)
{
	bcm4360_pmu_pllupd(pmu_of(sih));
}

/* pmu.md: initialisation */
void si_pmu_init(void *sih, void *osh)
{
	bcm4360_pmu_init(pmu_of(sih));
}

u32 si_pmu_measure_alpclk(void *sih, void *osh)
{
	return bcm4360_pmu_measure_alpclk(pmu_of(sih));
}

void si_pmu_pll_init(void *sih, void *osh, u32 xtalfreq)
{
	bcm4360_pmu_pll_init(pmu_of(sih), xtalfreq);
}

void si_pmu_res_init(void *sih, void *osh)
{
	bcm4360_pmu_res_init(pmu_of(sih));
}

/* pmu.md: clocks */
u32 si_pmu_alp_clock(void *sih, void *osh)
{
	return bcm4360_pmu_alp_clock(pmu_of(sih));
}

u32 si_pmu_ilp_clock(void *sih, void *osh)
{
	return bcm4360_pmu_ilp_clock(pmu_of(sih));
}

u32 si_pmu_si_clock(void *sih, void *osh)
{
	return bcm4360_pmu_si_clock(pmu_of(sih));
}

u32 si_pmu_get_bb_vcofreq(void *sih, void *osh, u32 xtalfreq)
{
	return bcm4360_pmu_get_bb_vcofreq(pmu_of(sih), xtalfreq);
}

u32 si_pmu_fast_pwrup_delay(void *sih, void *osh)
{
	return bcm4360_pmu_fast_pwrup_delay(pmu_of(sih));
}

/* pmu.md: power of the OTP and of the radio */
void si_pmu_otp_power(void *sih, void *osh, u32 on)
{
	bcm4360_pmu_otp_power(pmu_of(sih), (u8)on != 0);
}

u32 si_pmu_is_otp_powered(void *sih, void *osh)
{
	return bcm4360_pmu_is_otp_powered(pmu_of(sih));
}

void si_pmu_rfldo(void *sih, u32 on)
{
	bcm4360_pmu_rfldo(pmu_of(sih), (u8)on != 0);
}
