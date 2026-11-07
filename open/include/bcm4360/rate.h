/*
 * Rate sets: the helpers of Broadcom's wlc_rate.c that build and narrow the
 * sets of rates the driver may use (default set of a PHY type, filter by
 * kind of rate, intersection with the hardware's set).
 *
 * Specification: docs/re/spec/rate.md. No state of their own: they work on
 * the rate sets they are given and on constant tables.
 */
#ifndef BCM4360_RATE_H
#define BCM4360_RATE_H

#include <bcm4360/types.h>

/* rate.md, "The rate set": the caller's layout (0x28 bytes) */
struct bcm4360_rateset {
	u32 count;
	u8 rates[16];
	u8 htphy_membership;
	u8 mcs[16];
	u8 unused;
	u16 vht_mcsmap;
};

/* rate.md, "Procedures" (one function each, same order of arguments) */
bool bcm4360_rate_hwrs_filter_sort_validate(struct bcm4360_rateset *rs,
					    const struct bcm4360_rateset *hw_rs, bool check_brate);
void bcm4360_rateset_filter(const struct bcm4360_rateset *src, struct bcm4360_rateset *dst,
			    bool basic_only, u8 rates, u32 xmask, u8 mcsflags);
void bcm4360_rateset_ofdm_fixup(struct bcm4360_rateset *rs);
void bcm4360_rateset_default(struct bcm4360_rateset *rs_tgt, const struct bcm4360_rateset *rs_hw,
			     u32 phy_type, u32 bandtype, bool cck_only, u32 rate_mask, u8 mcsallow,
			     u8 bw, u8 txstreams);
void bcm4360_rateset_vhtmcs_upd(struct bcm4360_rateset *rs, u8 nstreams);
void bcm4360_rateset_mcs_upd(struct bcm4360_rateset *rs, u8 txstreams);
void bcm4360_rateset_mcs_build(struct bcm4360_rateset *rs, u8 txstreams);
void bcm4360_rateset_bw_mcs_filter(struct bcm4360_rateset *rs, u8 bw);
void bcm4360_rateset_copy(const struct bcm4360_rateset *src, struct bcm4360_rateset *dst);
u32 bcm4360_rate_rspec_reference_rate(u32 rspec);

#endif
