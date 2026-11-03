// SPDX-License-Identifier: ISC
/*
 * Rate sets: the helpers that build and narrow the sets of rates the driver
 * may use - the default set of a PHY type and band, the filter by kind of
 * rate, the intersection with the hardware's set, the MCS maps by stream
 * count and bandwidth, the reference rate of a rate specification.
 *
 * Written from docs/re/spec/rate.md. No state of its own: everything works
 * on the rate sets it is given and on the constant tables of "Data".
 */
#include <bcm4360/rate.h>
#include <bcm4360/hw.h>

/* docs/re/spec/rate.md, "The rate set" */
#define RATE_MASK		0x7f	/* bits 0-6: the rate in 500 kbit/s */
#define RATE_BASIC		0x80	/* bit 7: basic rate */
#define RATESET_SIZE		0x28
#define RATESET_MCS_LEN		16
#define VHT_STREAMS		8	/* two bits per stream in vht_mcsmap */
#define VHT_MCS_NONE		3	/* "not supported" */
#define VHT_MCSMAP_NONE		0xffff
#define HT_MCS_STREAMS		4	/* mcs[0..3]: one byte per stream */
#define HT_MCS32_BYTE		4	/* byte 4 bit 0 is MCS 32 */
#define HT_MCS32_BIT		0x01

/* docs/re/spec/rate.md, "rate_info": codes 0..108 are rates */
#define RATE_CODES		109
#define RATE_INFO_OFDM		0x80	/* bit 7 of an entry: an OFDM rate */

/* the rate kinds of wlc_rateset_filter's argument "rates" */
#define RATES_ALL		0	/* CCK and OFDM */
#define RATES_CCK		1	/* CCK only */
#define RATES_OFDM		2	/* OFDM only */

/* wlc_rateset_filter's argument "mcsflags" */
#define MCSFLAG_HT		0x01	/* keep the HT MCS map */
#define MCSFLAG_VHT		0x02	/* keep the VHT MCS map */

/* wlc_rateset_filter, step 3: the fix-up runs only for this mask */
#define XMASK_ALL		0xff

/* docs/re/spec/rate.md, "The default rate sets": phy_type and bandtype */
#define PHY_TYPE_A		0
#define PHY_TYPE_G		2
#define PHY_TYPE_LP		5
#define BANDTYPE_5G		1
#define BW_20			20
#define BW_40			40

/* docs/re/spec/rate.md, "wlc_rate_rspec_reference_rate": bits 24-25 */
#define RSPEC_KIND_SHIFT	24
#define RSPEC_KIND_MASK		0x3
#define RSPEC_KIND_HT		1
#define RSPEC_KIND_VHT		2
#define RSPEC_RATE_MASK		0xff	/* the low byte */
#define RSPEC_VHT_MCS_MASK	0xf	/* bits 0-3 */
#define RSPEC_HT_MCS_MAX	0x1f	/* largest low byte that is an HT MCS here */
#define RSPEC_HT_MCS_MASK	0x7	/* bits 0-2 */

/*
 * docs/re/spec/rate.md, "rate_info": a byte per rate code (0 = not a rate,
 * bit 7 = OFDM). The entries for 109..127 are the bytes that follow the
 * table in the object, which wlc_rateset_filter reads for "CCK only" (a
 * rate byte with bit 7 cleared can be anything up to 127).
 */
static const u8 rate_info[RATE_MASK + 1] = {
	[2] = 0x0a, [4] = 0x14, [11] = 0x37, [12] = 0x8b, [18] = 0x8f, [22] = 0x6e,
	[24] = 0x8a, [36] = 0x8e, [48] = 0x89, [72] = 0x8d, [96] = 0x88, [108] = 0x8c,
	/* codes 109..127 */
	[112] = 0x64, [113] = 0x19, [116] = 0xbc, [117] = 0x34, [120] = 0x37, [121] = 0x1c,
	[124] = 0x98, [125] = 0x3a,
};

/* docs/re/spec/rate.md, "The default rate sets" */
#define CCK_RATES	0x82, 0x84, 0x0b, 0x16
#define OFDM_RATES	0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c
#define CCK_OFDM_RATES	0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c
#define MCS_MIMO	0xff, 0xff, 0xff, 0xff, 0x00
#define MCS_40BW_MIMO	0xff, 0xff, 0xff, 0xff, 0x01
#define VHT_MIMO	0x002a

static const struct bcm4360_rateset cck_rates = {
	.count = 4, .rates = { CCK_RATES }, .vht_mcsmap = VHT_MCSMAP_NONE,
};
static const struct bcm4360_rateset ofdm_rates = {
	.count = 8, .rates = { OFDM_RATES }, .vht_mcsmap = VHT_MCSMAP_NONE,
};
static const struct bcm4360_rateset cck_ofdm_rates = {
	.count = 12, .rates = { CCK_OFDM_RATES }, .vht_mcsmap = VHT_MCSMAP_NONE,
};
static const struct bcm4360_rateset ofdm_mimo_rates = {
	.count = 8, .rates = { OFDM_RATES }, .mcs = { MCS_MIMO }, .vht_mcsmap = VHT_MIMO,
};
static const struct bcm4360_rateset ofdm_40bw_mimo_rates = {
	.count = 8, .rates = { OFDM_RATES }, .mcs = { MCS_40BW_MIMO }, .vht_mcsmap = VHT_MIMO,
};
static const struct bcm4360_rateset cck_ofdm_mimo_rates = {
	.count = 12, .rates = { CCK_OFDM_RATES }, .mcs = { MCS_MIMO }, .vht_mcsmap = VHT_MIMO,
};
static const struct bcm4360_rateset cck_ofdm_40bw_mimo_rates = {
	.count = 12, .rates = { CCK_OFDM_RATES }, .mcs = { MCS_40BW_MIMO },
	.vht_mcsmap = VHT_MIMO,
};

/* docs/re/spec/rate.md, "Reference rates of wlc_rate_rspec_reference_rate" */
static const u32 reference_rate[] = { 12, 24, 36, 48, 72, 96, 108, 108, 108, 108 };

/* the two bits of stream s in a VHT MCS map */
static u32 vht_get(u16 map, u32 s)
{
	return (map >> (2 * s)) & VHT_MCS_NONE;
}

static u16 vht_set(u16 map, u32 s, u32 v)
{
	return (map & ~(VHT_MCS_NONE << (2 * s))) | (v << (2 * s));
}

/* docs/re/spec/rate.md, "wlc_rateset_copy(src, dst)" */
void bcm4360_rateset_copy(const struct bcm4360_rateset *src, struct bcm4360_rateset *dst)
{
	hw_memcpy(dst, src, RATESET_SIZE);
}

/* docs/re/spec/rate.md, "wlc_rateset_mcs_upd(rs, txstreams)" */
void bcm4360_rateset_mcs_upd(struct bcm4360_rateset *rs, u8 txstreams)
{
	u32 i;

	for (i = txstreams; i < HT_MCS_STREAMS; i++)
		rs->mcs[i] = 0;
}

/* docs/re/spec/rate.md, "wlc_rateset_mcs_build(rs, txstreams)" */
void bcm4360_rateset_mcs_build(struct bcm4360_rateset *rs, u8 txstreams)
{
	hw_memcpy(rs->mcs, cck_ofdm_mimo_rates.mcs, RATESET_MCS_LEN);
	bcm4360_rateset_mcs_upd(rs, txstreams);
}

/* docs/re/spec/rate.md, "wlc_rateset_vhtmcs_upd(rs, nstreams)" */
void bcm4360_rateset_vhtmcs_upd(struct bcm4360_rateset *rs, u8 nstreams)
{
	u32 s;

	for (s = nstreams; s < VHT_STREAMS; s++)
		rs->vht_mcsmap = vht_set(rs->vht_mcsmap, s, VHT_MCS_NONE);
}

/* docs/re/spec/rate.md, "wlc_rateset_bw_mcs_filter(rs, bw)" */
void bcm4360_rateset_bw_mcs_filter(struct bcm4360_rateset *rs, u8 bw)
{
	if (bw == BW_40)
		rs->mcs[HT_MCS32_BYTE] |= HT_MCS32_BIT;
	else
		rs->mcs[HT_MCS32_BYTE] &= (u8)~HT_MCS32_BIT;
}

/* docs/re/spec/rate.md, "wlc_rate_rspec_reference_rate(rspec)" */
u32 bcm4360_rate_rspec_reference_rate(u32 rspec)
{
	u32 i;

	switch ((rspec >> RSPEC_KIND_SHIFT) & RSPEC_KIND_MASK) {
	case RSPEC_KIND_VHT:
		i = rspec & RSPEC_VHT_MCS_MASK;
		if (i >= ARRAY_SIZE(reference_rate))
			i = 0;
		return reference_rate[i];
	case RSPEC_KIND_HT:
		i = (rspec & RSPEC_RATE_MASK) <= RSPEC_HT_MCS_MAX ? rspec & RSPEC_HT_MCS_MASK : 0;
		return reference_rate[i];
	default:
		return rspec & RSPEC_RATE_MASK;
	}
}

/* docs/re/spec/rate.md, "wlc_rateset_ofdm_fixup(rs)" */
void bcm4360_rateset_ofdm_fixup(struct bcm4360_rateset *rs)
{
	u32 i, j;

	for (i = 0; i < rs->count; i++) {
		for (j = 0; j < ofdm_rates.count; j++) {
			if ((ofdm_rates.rates[j] & RATE_MASK) != (rs->rates[i] & RATE_MASK))
				continue;
			if (ofdm_rates.rates[j] & RATE_BASIC)
				rs->rates[i] |= RATE_BASIC;
			break;
		}
	}
}

/* wlc_rateset_filter, step 1: the rate kinds that "OFDM only" drops (CCK) */
static bool rate_is_cck(u8 code)
{
	return code == 2 || code == 4 || code == 11 || code == 22;
}

/* docs/re/spec/rate.md, "wlc_rateset_filter(src, dst, basic_only, rates, xmask, mcsflags)" */
void bcm4360_rateset_filter(const struct bcm4360_rateset *src, struct bcm4360_rateset *dst,
			    bool basic_only, u8 rates, u32 xmask, u8 mcsflags)
{
	u32 i, n = 0;

	/* step 1 */
	for (i = 0; i < src->count; i++) {
		u8 r = src->rates[i];

		if (basic_only && !(r & RATE_BASIC))
			continue;
		if (rates == RATES_CCK && (rate_info[r & RATE_MASK] & RATE_INFO_OFDM))
			continue;
		if (rates == RATES_OFDM && rate_is_cck(r & RATE_MASK))
			continue;
		dst->rates[n++] = (u8)(r & xmask);
	}
	/* step 2 */
	dst->count = n;
	/* step 3 */
	if (xmask == XMASK_ALL && rates == RATES_OFDM)
		bcm4360_rateset_ofdm_fixup(dst);
	/* step 4 */
	dst->htphy_membership = src->htphy_membership;
	/* step 5 */
	if (!(mcsflags & MCSFLAG_HT) || rates == RATES_CCK) {
		hw_memset(dst->mcs, 0, RATESET_MCS_LEN);
		dst->vht_mcsmap = VHT_MCSMAP_NONE;
	} else {
		hw_memcpy(dst->mcs, src->mcs, RATESET_MCS_LEN);
		dst->vht_mcsmap = (mcsflags & MCSFLAG_VHT) ? src->vht_mcsmap : VHT_MCSMAP_NONE;
	}
}

/* docs/re/spec/rate.md, "wlc_rate_hwrs_filter_sort_validate(rs, hw_rs, check_brate)" */
bool bcm4360_rate_hwrs_filter_sort_validate(struct bcm4360_rateset *rs,
					    const struct bcm4360_rateset *hw_rs, bool check_brate)
{
	/*
	 * step 1: the table by rate code. It covers every code a rate byte can
	 * have; codes above 108 are never entered (step 2), so a hardware rate
	 * with such a code (undefined in the object, step 3) finds nothing.
	 */
	u8 t[RATE_MASK + 1];
	u32 i, s, n = 0;

	hw_memset(t, 0, sizeof(t));
	/* step 2 */
	for (i = 0; i < rs->count; i++) {
		u8 c = rs->rates[i] & RATE_MASK;

		if (c < RATE_CODES && rate_info[c])
			t[c] = rs->rates[i];
	}
	/* step 3 */
	for (i = 0; i < hw_rs->count; i++) {
		u8 r = t[hw_rs->rates[i] & RATE_MASK];

		if (r)
			rs->rates[n++] = r;
	}
	/* step 4 */
	rs->count = n;
	/* step 5 */
	for (i = 0; i < RATESET_MCS_LEN; i++)
		rs->mcs[i] &= hw_rs->mcs[i];
	/* step 6 */
	for (s = 0; s < VHT_STREAMS; s++) {
		u32 a = vht_get(rs->vht_mcsmap, s), b = vht_get(hw_rs->vht_mcsmap, s);

		if (b < a && a != VHT_MCS_NONE)
			rs->vht_mcsmap = vht_set(rs->vht_mcsmap, s, b);
	}
	/* step 7 */
	if (!n)
		return false;
	if (!check_brate)
		return true;
	for (i = 0; i < n; i++)
		if (rs->rates[i] & RATE_BASIC)
			return true;
	return false;
}

/* wlc_rateset_default, step 1: the default set of a PHY type and band */
static const struct bcm4360_rateset *rateset_of_phy(u32 phy_type, u32 bandtype, u8 bw)
{
	switch (phy_type) {
	case 4: case 6: case 7: case 8: case 10: case 11:	/* the MIMO PHYs; 11 = AC */
		if (bandtype == BANDTYPE_5G)
			return bw == BW_20 ? &ofdm_mimo_rates : &ofdm_40bw_mimo_rates;
		return bw == BW_20 ? &cck_ofdm_mimo_rates : &cck_ofdm_40bw_mimo_rates;
	case PHY_TYPE_LP:
		return bandtype == BANDTYPE_5G ? &ofdm_rates : &cck_ofdm_rates;
	case PHY_TYPE_A:
		return &ofdm_rates;
	case PHY_TYPE_G:
		return &cck_ofdm_rates;
	default:
		return &cck_rates;
	}
}

/* docs/re/spec/rate.md, "wlc_rateset_default(...)" */
void bcm4360_rateset_default(struct bcm4360_rateset *rs_tgt, const struct bcm4360_rateset *rs_hw,
			     u32 phy_type, u32 bandtype, bool cck_only, u32 rate_mask, u8 mcsallow,
			     u8 bw, u8 txstreams)
{
	struct bcm4360_rateset s;

	/* steps 1 and 2 */
	if (!rs_hw)
		rs_hw = rateset_of_phy(phy_type, bandtype, bw);
	/* step 3 */
	bcm4360_rateset_copy(rs_hw, &s);
	bcm4360_rateset_mcs_upd(&s, txstreams);
	bcm4360_rateset_vhtmcs_upd(&s, txstreams);
	/* step 4 */
	bcm4360_rateset_filter(&s, rs_tgt, false, cck_only ? RATES_CCK : RATES_ALL, rate_mask,
			       mcsallow);
	/* step 5 */
	bcm4360_rate_hwrs_filter_sort_validate(rs_tgt, rs_hw, false);
}
