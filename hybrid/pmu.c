/*
 * Hybrid build: the PMU and the clocks (Broadcom's hndpmu.c) by the open
 * code of open/chip/pmu.c (docs/re/spec/pmu.md).
 *
 * Every function here has the name and the arguments of the function of the
 * object it takes over (hybrid/takeover.txt), so that the object's calls
 * arrive here. With the switch open_pmu off, or for a chip the open code
 * does not know, the object's own function runs (bcm4360_blob_*).
 *
 * The open code reaches ChipCommon through its fixed window and never moves
 * the movable one, so the object's record of the current core stays right.
 *
 * The parameters of type bool of the object are bytes: they arrive as u8.
 */
#include "hybrid.h"
#include <bcm4360/pmu.h>

/* the functions taken over, and the object's own bodies of them */
HYB_TAKE(void, si_pmu_init, (const void *sih, void *osh));
HYB_TAKE(void, si_pmu_pll_init, (const void *sih, void *osh, u32 xtalfreq));
HYB_TAKE(void, si_pmu_res_init, (const void *sih, void *osh));
HYB_TAKE(u32, si_pmu_measure_alpclk, (const void *sih, void *osh));
HYB_TAKE(u32, si_pmu_alp_clock, (const void *sih, void *osh));
HYB_TAKE(u32, si_pmu_ilp_clock, (const void *sih, void *osh));
HYB_TAKE(u32, si_pmu_si_clock, (const void *sih, void *osh));
HYB_TAKE(u32, si_pmu_get_bb_vcofreq, (const void *sih, void *osh, int xtalfreq));
HYB_TAKE(u32, si_pmu_fast_pwrup_delay, (const void *sih, void *osh));
HYB_TAKE(void, si_pmu_otp_power, (const void *sih, void *osh, u8 on));
HYB_TAKE(u8, si_pmu_is_otp_powered, (const void *sih, void *osh));
HYB_TAKE(u32, si_pmu_chipcontrol, (const void *sih, u32 reg, u32 mask, u32 val));
HYB_TAKE(u32, si_pmu_regcontrol, (const void *sih, u32 reg, u32 mask, u32 val));
HYB_TAKE(u32, si_pmu_pllcontrol, (const void *sih, u32 reg, u32 mask, u32 val));
HYB_TAKE(void, si_pmu_pllupd, (const void *sih));
HYB_TAKE(void, si_pmu_rfldo, (const void *sih, u8 on));

/* the open PMU state of a card, one per backplane handle */
struct hyb_pmu {
	struct bcm4360_pmu pmu;
	const void *sih;
};

#define HYB_PMUS	4
static struct hyb_pmu hyb_pmus[HYB_PMUS];

/* hybrid.h: the open PMU of the card of `sih`, whatever the switch says */
struct bcm4360_pmu *hyb_pmu_of(const void *sih)
{
	const struct hyb_si_pub *pub = sih;
	struct hyb_card *c;
	struct hyb_pmu *h = NULL;
	int i;

	if (!sih)
		return NULL;
	c = hyb_card_of(sih);
	if (!c)
		return NULL;
	for (i = 0; i < HYB_PMUS; i++) {
		if (hyb_pmus[i].sih == sih) {
			h = &hyb_pmus[i];
			break;
		}
	}
	for (i = 0; i < HYB_PMUS && !h; i++) {
		if (!hyb_pmus[i].sih) {
			h = &hyb_pmus[i];
			hw_memset(h, 0, sizeof(*h));
			h->sih = sih;
		}
	}
	if (!h)
		return NULL;
	/* what the object knows of the chip at this moment (it may still be attaching) */
	h->pmu.hw = &c->hw;
	h->pmu.info.chip = pub->chip;
	h->pmu.info.chiprev = pub->chiprev;
	h->pmu.info.chippkg = pub->chippkg;
	h->pmu.info.chipst = pub->chipst;
	h->pmu.info.ccrev = pub->ccrev;
	h->pmu.info.cccaps = pub->cccaps;
	h->pmu.info.cccaps_ext = pub->cccaps_ext;
	h->pmu.info.pmurev = pub->pmurev;
	h->pmu.info.pmucaps = pub->pmucaps;
	return &h->pmu;
}

/* the open PMU of the card of `sih`, or NULL for the object's code */
static struct bcm4360_pmu *hyb_pmu(const void *sih)
{
	const struct hyb_si_pub *pub = sih;

	if (!bcm4360_open_pmu || !sih)
		return NULL;
	if (pub->chip != BCM4360_CHIP_ID_4360 && pub->chip != BCM4360_CHIP_ID_4352)
		return NULL;
	return hyb_pmu_of(sih);
}

/* hybrid.h */
void hyb_pmu_forget(const void *sih)
{
	int i;

	for (i = 0; i < HYB_PMUS; i++) {
		if (hyb_pmus[i].sih == sih)
			hyb_pmus[i].sih = NULL;
	}
}

void si_pmu_init(const void *sih, void *osh)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p) {
		bcm4360_blob_si_pmu_init(sih, osh);
		return;
	}
	bcm4360_pmu_init(p);
}

void si_pmu_pll_init(const void *sih, void *osh, u32 xtalfreq)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p) {
		bcm4360_blob_si_pmu_pll_init(sih, osh, xtalfreq);
		return;
	}
	bcm4360_pmu_pll_init(p, xtalfreq);
}

void si_pmu_res_init(const void *sih, void *osh)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p) {
		bcm4360_blob_si_pmu_res_init(sih, osh);
		return;
	}
	bcm4360_pmu_res_init(p);
}

u32 si_pmu_measure_alpclk(const void *sih, void *osh)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p)
		return bcm4360_blob_si_pmu_measure_alpclk(sih, osh);
	return bcm4360_pmu_measure_alpclk(p);
}

u32 si_pmu_alp_clock(const void *sih, void *osh)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p)
		return bcm4360_blob_si_pmu_alp_clock(sih, osh);
	return bcm4360_pmu_alp_clock(p);
}

u32 si_pmu_ilp_clock(const void *sih, void *osh)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p)
		return bcm4360_blob_si_pmu_ilp_clock(sih, osh);
	return bcm4360_pmu_ilp_clock(p);
}

u32 si_pmu_si_clock(const void *sih, void *osh)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p)
		return bcm4360_blob_si_pmu_si_clock(sih, osh);
	return bcm4360_pmu_si_clock(p);
}

u32 si_pmu_get_bb_vcofreq(const void *sih, void *osh, int xtalfreq)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p)
		return bcm4360_blob_si_pmu_get_bb_vcofreq(sih, osh, xtalfreq);
	return bcm4360_pmu_get_bb_vcofreq(p, xtalfreq);
}

u32 si_pmu_fast_pwrup_delay(const void *sih, void *osh)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p)
		return bcm4360_blob_si_pmu_fast_pwrup_delay(sih, osh);
	return bcm4360_pmu_fast_pwrup_delay(p);
}

void si_pmu_otp_power(const void *sih, void *osh, u8 on)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p) {
		bcm4360_blob_si_pmu_otp_power(sih, osh, on);
		return;
	}
	bcm4360_pmu_otp_power(p, on != 0);
}

u8 si_pmu_is_otp_powered(const void *sih, void *osh)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p)
		return bcm4360_blob_si_pmu_is_otp_powered(sih, osh);
	return bcm4360_pmu_is_otp_powered(p);
}

u32 si_pmu_chipcontrol(const void *sih, u32 reg, u32 mask, u32 val)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p)
		return bcm4360_blob_si_pmu_chipcontrol(sih, reg, mask, val);
	return bcm4360_pmu_chipcontrol(p, reg, mask, val);
}

u32 si_pmu_regcontrol(const void *sih, u32 reg, u32 mask, u32 val)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p)
		return bcm4360_blob_si_pmu_regcontrol(sih, reg, mask, val);
	return bcm4360_pmu_regcontrol(p, reg, mask, val);
}

u32 si_pmu_pllcontrol(const void *sih, u32 reg, u32 mask, u32 val)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p)
		return bcm4360_blob_si_pmu_pllcontrol(sih, reg, mask, val);
	return bcm4360_pmu_pllcontrol(p, reg, mask, val);
}

void si_pmu_pllupd(const void *sih)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p) {
		bcm4360_blob_si_pmu_pllupd(sih);
		return;
	}
	bcm4360_pmu_pllupd(p);
}

void si_pmu_rfldo(const void *sih, u8 on)
{
	struct bcm4360_pmu *p = hyb_pmu(sih);

	if (!p) {
		bcm4360_blob_si_pmu_rfldo(sih, on);
		return;
	}
	bcm4360_pmu_rfldo(p, on != 0);
}
