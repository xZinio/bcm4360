// SPDX-License-Identifier: ISC
/*
 * The power field of PHY control word 1 of the transmit header's rate
 * entries (docs/re/spec/coverage.md part 2, G1; frames.md 3.5): the per-rate
 * back-off from the stf container, the spatial expansion of the rate's
 * class, and the open-loop power offset with its clamp.
 *
 * A rate is handled as a 32-bit rate specification (coverage.md G1 "Rate
 * specification fields used"): legacy rate / MCS in bits 0..6 (VHT: MCS
 * bits 0..3, streams bits 4..7), bandwidth bits 16..18 (1 = 20, 2 = 40,
 * 3 = 80 MHz), STBC bit 20, beamforming bit 21, encoding bits 24..25.
 */
#include "core.h"
#include <bcm4360/ratepwr.h>
#include <bcm4360/phy_api.h>

#define RSPEC_RATE_MASK		0x0000007f
#define RSPEC_BW_SHIFT		16
#define RSPEC_STBC		0x00100000
#define RSPEC_BF		0x00200000
#define RSPEC_ENC_SHIFT		24

#define STF_TXSTREAMS		0x02
#define STF_TXCORE_NSTS(i)	(0x16 + 2 * (i))
#define STF_TXCORE_MASK(i)	(0x17 + 2 * (i))
#define STF_TXCORE_OVR(i)	(0x22 + (i))
#define STF_PPR			0x30
#define STF_CEIL		0x88
#define PUB_TXBF_CAP		0xe8
#define STF_CLASSES		6

#define PPR_NONE		0x80	/* a disabled rate / a missing group */

static u32 popcount8(u32 v)
{
	u32 n = 0;

	for (v &= 0xff; v; v >>= 1)
		n += v & 1;
	return n;
}

/* G1 "Rate specification": the platform's rate as a rate specification */
u32 core_txrate_rspec(const struct bcm4360_txrate *r)
{
	u32 v = (u32)(r->bw + 1) << RSPEC_BW_SHIFT;

	v |= (u32)r->enc << RSPEC_ENC_SHIFT;
	if (r->enc == 2)
		v |= (r->rate & 0xf) | ((u32)(r->nss ? r->nss : 1) << 4);
	else
		v |= r->rate & RSPEC_RATE_MASK;
	if (r->flags & BCM4360_TX_STBC)
		v |= RSPEC_STBC;
	return v;
}

static u32 rs_enc(u32 rspec)
{
	return (rspec >> RSPEC_ENC_SHIFT) & 3;
}

static bool rs_cck(u32 rspec)
{
	u32 r = rspec & RSPEC_RATE_MASK;

	return !rs_enc(rspec) && (r == 2 || r == 4 || r == 11 || r == 22);
}

static bool rs_ofdm(u32 rspec)
{
	u32 r = rspec & RSPEC_RATE_MASK;

	return !rs_enc(rspec) && (r == 12 || r == 18 || r == 24 || r == 36 || r == 48 ||
				  r == 72 || r == 96 || r == 108);
}

/* G1 "Derived": the spatial streams of a rate */
static u32 rs_nss(u32 rspec)
{
	u32 mcs = rspec & RSPEC_RATE_MASK;

	switch (rs_enc(rspec)) {
	case 2:
		return (rspec >> 4) & 0xf;
	case 1:
		return mcs == 32 ? 1 : (mcs >> 3) + 1;
	default:
		return 1;
	}
}

/* G1 "Derived": the space-time streams of a rate */
static u32 rs_nsts(u32 rspec)
{
	u32 nss = rs_nss(rspec);

	if (!(rspec & RSPEC_STBC))
		return nss;
	if (rs_enc(rspec) == 2)
		return nss * 2;
	if (rs_enc(rspec) == 1 && (rspec & RSPEC_RATE_MASK) == 32)
		return 1;
	return nss + 1;
}

/* the class of a rate in txcore[] (cca-stf.md, G1.1 step 1) */
static u32 rs_class(const u8 *stf, u32 rspec)
{
	u32 i;

	if (rspec & RSPEC_BF)
		i = stf[STF_TXSTREAMS] + 1;
	else if (rs_cck(rspec))
		i = 0;
	else if (rs_ofdm(rspec))
		i = 1;
	else
		i = rs_nsts(rspec) + 1;
	return i > 5 ? 5 : i;
}

/* docs/re/spec/coverage.md, G1: wlc_stf_txchain_get */
u32 core_stf_txchain_get(struct bcm4360_core *c, u32 rspec)
{
	const u8 *stf = c->stf_state;
	u32 i = rs_class(stf, rspec), m = stf[STF_TXCORE_MASK(i)];

	/* the override is not looked at (verified): the policy has folded it into the mask */
	return popcount8(m);
}

/* docs/re/spec/cca-stf.md, sub_172da9 (wlc_stf_spatial_expansion_idx) */
static u32 expansion_idx(const u8 *stf, u32 i)
{
	u32 b = popcount8(stf[STF_TXCORE_MASK(i)]), s = stf[STF_TXCORE_NSTS(i)];

	if (s > stf[STF_TXSTREAMS] || !b || b == s)
		return 0;
	if (b == 2 && s == 1)
		return 1;
	if (b == 3 && s == 1)
		return 2;
	if (b == 3 && s == 2)
		return 3;
	return 0;
}

/* docs/re/spec/coverage.md, G1.1: wlc_stf_spatial_expansion_get */
u32 core_stf_spatial_expansion_get(struct bcm4360_core *c, u32 rspec)
{
	const u8 *stf = c->stf_state;

	if (!stf)
		return 0;
	return expansion_idx(stf, rs_class(stf, rspec));
}

/* G1.2: the chains an expansion mode uses */
static u32 exp_chains(u32 e)
{
	return e == 1 ? 2 : e == 2 ? 3 : 1;
}

/* G1.2 step 5: the offset of a legacy rate in its group */
static u32 legacy_index(u32 rspec)
{
	switch (rspec & RSPEC_RATE_MASK) {
	case 2: case 12: return 0;
	case 4: case 18: return 1;
	case 11: case 24: return 2;
	case 22: case 36: return 3;
	case 48: return 4;
	case 72: return 5;
	case 96: return 6;
	default: return 7;
	}
}

/* G1.2 step 6: the group of an HT / VHT rate; false: no group (result 0) */
static bool mcs_group(u32 rspec, u32 e, bool bf, u32 n, u32 *nss, u32 *mode, u32 *chains)
{
	if (rspec & RSPEC_STBC) {
		*nss = 2;
		*mode = 1;
		*chains = e == 3 ? 3 : 2;
	} else if (bf) {
		*nss = rs_nss(rspec);
		*mode = 3;
		*chains = n;
	} else {
		switch (rs_nsts(rspec)) {
		case 1:
			*nss = 1;
			*mode = (e == 1 || e == 2) ? 2 : 0;
			*chains = exp_chains(e);
			break;
		case 2:
			*nss = 2;
			*mode = 0;
			*chains = e == 3 ? 3 : 2;
			break;
		case 3:
			*nss = 3;
			*mode = 0;
			*chains = 3;
			break;
		default:
			return false;
		}
	}
	return true;
}

/* docs/re/spec/coverage.md, G1.2: wlc_stf_get_pwrperrate */
s8 core_stf_get_pwrperrate(struct bcm4360_core *c, u32 rspec, u32 e)
{
	const u8 *stf = c->stf_state;
	struct bcm4360_ratepwr *p;
	u32 bw = (rspec >> RSPEC_BW_SHIFT) & 7, w, enc = rs_enc(rspec);
	u32 nss, mode, chains, n = 0, idx;
	bool bf;
	s8 g[10];

	if (!stf)
		return 0;
	hw_memcpy(&p, stf + STF_PPR, sizeof(p));
	if (!p)
		return 0;
	hw_memset(g, PPR_NONE, sizeof(g));
	if (rs_cck(rspec))
		w = bw == 3 ? 4 : bw == 2 ? 3 : 0;
	else
		w = bw == 3 ? 2 : bw == 2 ? 1 : 0;
	bf = FIELD(c->pub, PUB_TXBF_CAP, u8) && (rspec & RSPEC_BF) && stf[STF_TXSTREAMS] >= 2;
	if (bf)
		n = core_stf_txchain_get(c, rspec);
	if (!enc) {
		if (rs_cck(rspec)) {
			bcm4360_ratepwr_get_dsss(p, w, exp_chains(e), g);
		} else if (rs_ofdm(rspec)) {
			if (bf)
				bcm4360_ratepwr_get_ofdm(p, w, 3, n, g);
			else
				bcm4360_ratepwr_get_ofdm(p, w, (e == 1 || e == 2) ? 2 : 0,
							 exp_chains(e), g);
		} else {
			return 0;
		}
		idx = legacy_index(rspec);
	} else {
		if (!mcs_group(rspec, e, bf, n, &nss, &mode, &chains))
			return 0;
		bcm4360_ratepwr_get_vht_mcs(p, w, nss, mode, chains, g);
		idx = enc == 2 ? (rspec & 0xf) : (rspec & 7);
	}
	return g[idx] == (s8)PPR_NONE ? (s8)stf[STF_CEIL] : g[idx];
}

static s32 sext6(u32 v)
{
	v &= 0x3f;
	return v & 0x20 ? (s32)v - 64 : (s32)v;
}

/* G1.3 step 3: the open-loop offset of the rate's bandwidth, 0 above the threshold */
static s32 olpc_offset(struct bcm4360_core *c, u32 bw, u32 p)
{
	s32 t, th;

	if (!c->phy)
		return 0;
	t = bcm4360_phy_txpower_get_target_max(c->phy);
	th = bcm4360_phy_tssivisible_thresh(c->phy);
	if (t - 2 * (s32)p >= th)
		return 0;
	switch (bw) {
	case 1: return bcm4360_phy_get_olpc_pwroffset(c->phy, 0x1000);
	case 2: return bcm4360_phy_get_olpc_pwroffset(c->phy, 0x1800);
	case 3: return bcm4360_phy_get_olpc_pwroffset(c->phy, 0x2000);
	default: return 0;
	}
}

/* docs/re/spec/coverage.md, G1.3: wlc_acphy_txctl1_calc */
u16 core_txctl1_calc(struct bcm4360_core *c, u32 rspec)
{
	u32 bw = (rspec >> RSPEC_BW_SHIFT) & 7, e, p, v;
	s32 o6, s;

	e = core_stf_spatial_expansion_get(c, rspec);
	p = (u8)core_stf_get_pwrperrate(c, rspec, e);
	v = p & 0x3f;
	if (c->sihp.chip == 0x4360) {
		o6 = sext6((u32)olpc_offset(c, bw, p));
		if (o6) {
			s = (s8)(o6 + sext6(p));
			if (s < -32)
				s = -32;
			if (s > 31)
				s = 31;
			v = s & 0x3f;
		}
	}
	return (u16)(v << 3 | (((FIELD(c->wlc, WLC_CHANSPEC, u16) >> 8) & 7) >> ((bw - 1) & 31)));
}
