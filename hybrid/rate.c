// SPDX-License-Identifier: ISC
/*
 * Hybrid build: the rate set helpers of Broadcom's wlc_rate.c by the open
 * code of open/lib/rate.c (docs/re/spec/rate.md). Switch open_rate, read at
 * every call.
 *
 * The rate sets are the caller's (struct bcm4360_rateset has their layout);
 * nothing else is marshalled. Parameters of type bool or u8 of the object
 * are bytes: taken as u32 and truncated.
 */
#include "hybrid.h"
#include <bcm4360/rate.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_rate = true;

#ifdef __KERNEL__
module_param_named(open_rate, bcm4360_open_rate, bool, 0444);
MODULE_PARM_DESC(open_rate, "rate set helpers: open code (1, default) or Broadcom's (0)");
#endif

_Static_assert(sizeof(struct bcm4360_rateset) == 0x28, "rate set of rate.md");

typedef struct bcm4360_rateset rs_t;

HYB_TAKE(u32, wlc_rate_hwrs_filter_sort_validate, (rs_t *rs, const rs_t *hw_rs, u32 check_brate,
						   u32 txstreams));
HYB_TAKE(void, wlc_rateset_filter, (const rs_t *src, rs_t *dst, u32 basic_only, u32 rates,
				    u32 xmask, u32 mcsflags));
HYB_TAKE(void, wlc_rateset_ofdm_fixup, (rs_t *rs));
HYB_TAKE(void, wlc_rateset_default, (rs_t *rs_tgt, const rs_t *rs_hw, u32 phy_type, u32 bandtype,
				     u32 cck_only, u32 rate_mask, u32 mcsallow, u32 bw,
				     u32 txstreams));
HYB_TAKE(void, wlc_rateset_vhtmcs_upd, (rs_t *rs, u32 nstreams));
HYB_TAKE(void, wlc_rateset_mcs_upd, (rs_t *rs, u32 txstreams));
HYB_TAKE(void, wlc_rateset_mcs_build, (rs_t *rs, u32 txstreams));
HYB_TAKE(void, wlc_rateset_bw_mcs_filter, (rs_t *rs, u32 bw));
HYB_TAKE(void, wlc_rateset_copy, (const rs_t *src, rs_t *dst));
HYB_TAKE(u32, wlc_rate_rspec_reference_rate, (u32 rspec));

u32 wlc_rate_hwrs_filter_sort_validate(rs_t *rs, const rs_t *hw_rs, u32 check_brate,
				       u32 txstreams)
{
	if (!bcm4360_open_rate)
		return bcm4360_blob_wlc_rate_hwrs_filter_sort_validate(rs, hw_rs, check_brate,
									txstreams);
	/* rate.md: the fourth argument is not read */
	return bcm4360_rate_hwrs_filter_sort_validate(rs, hw_rs, (u8)check_brate != 0);
}

void wlc_rateset_filter(const rs_t *src, rs_t *dst, u32 basic_only, u32 rates, u32 xmask,
			u32 mcsflags)
{
	if (!bcm4360_open_rate) {
		bcm4360_blob_wlc_rateset_filter(src, dst, basic_only, rates, xmask, mcsflags);
		return;
	}
	bcm4360_rateset_filter(src, dst, (u8)basic_only != 0, (u8)rates, xmask, (u8)mcsflags);
}

void wlc_rateset_ofdm_fixup(rs_t *rs)
{
	if (!bcm4360_open_rate) {
		bcm4360_blob_wlc_rateset_ofdm_fixup(rs);
		return;
	}
	bcm4360_rateset_ofdm_fixup(rs);
}

void wlc_rateset_default(rs_t *rs_tgt, const rs_t *rs_hw, u32 phy_type, u32 bandtype,
			 u32 cck_only, u32 rate_mask, u32 mcsallow, u32 bw, u32 txstreams)
{
	if (!bcm4360_open_rate) {
		bcm4360_blob_wlc_rateset_default(rs_tgt, rs_hw, phy_type, bandtype, cck_only,
						 rate_mask, mcsallow, bw, txstreams);
		return;
	}
	bcm4360_rateset_default(rs_tgt, rs_hw, phy_type, bandtype, (u8)cck_only != 0, rate_mask,
				(u8)mcsallow, (u8)bw, (u8)txstreams);
}

#define ADAPT_U8(name)							\
void wlc_##name(rs_t *rs, u32 v)					\
{									\
	if (!bcm4360_open_rate) {					\
		bcm4360_blob_wlc_##name(rs, v);				\
		return;							\
	}								\
	bcm4360_##name(rs, (u8)v);					\
}

ADAPT_U8(rateset_vhtmcs_upd)
ADAPT_U8(rateset_mcs_upd)
ADAPT_U8(rateset_mcs_build)
ADAPT_U8(rateset_bw_mcs_filter)

void wlc_rateset_copy(const rs_t *src, rs_t *dst)
{
	if (!bcm4360_open_rate) {
		bcm4360_blob_wlc_rateset_copy(src, dst);
		return;
	}
	bcm4360_rateset_copy(src, dst);
}

u32 wlc_rate_rspec_reference_rate(u32 rspec)
{
	if (!bcm4360_open_rate)
		return bcm4360_blob_wlc_rate_rspec_reference_rate(rspec);
	return bcm4360_rate_rspec_reference_rate(rspec);
}
