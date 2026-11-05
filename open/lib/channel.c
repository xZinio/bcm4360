// SPDX-License-Identifier: ISC
/*
 * Chanspecs and the validity of channels: the chanspec helpers of Broadcom's
 * bcmwifi_channels.c (H1-H6) and the validity checks of channels and
 * chanspecs of its channel manager, wlc_channel.c (V1-V7).
 *
 * Written from docs/re/spec/channel.md. Pure computation: no hardware access,
 * no state outside the arguments. The checks read the struct bcm4360_chan the
 * caller filled (C4) and reach the regulatory database only through the
 * bcm4360_chan_env_clm_* services of the caller.
 */
#include <bcm4360/channel.h>

/* docs/re/spec/channel.md, "The chanspec": the fields */
#define CS_CHAN_MASK		0x00ff
#define CS_SB_MASK		0x0700
#define CS_SB_SHIFT		8
#define CS_BW_MASK		0x3800
#define CS_BW_SHIFT		11
#define CS_BAND_MASK		0xc000

/* "The chanspec": the values of the bandwidth field */
#define CS_BW_20		0x1000
#define CS_BW_40		0x1800
#define CS_BW_80		0x2000
#define CS_BW_160		0x2800
#define CS_BW_8080		0x3000

/* "The chanspec": the values of the band field */
#define CS_BAND_2G		0x0000
#define CS_BAND_5G		0xc000

/* "The chanspec": sideband values (0 = lowest 20 MHz part) */
#define CS_SB_0			0x0000
#define CS_SB_1			0x0100
#define CS_SB_2			0x0200
#define CS_SB_3			0x0300
#define CS_SB_STEP		0x0100

/* H1, H2: the two 4-bit segment indices of an 80+80 MHz chanspec */
#define CS_SEG_MASK		0x0f
#define CS_SEG_HI_SHIFT		4
#define CS_SEG_MAX		5	/* the highest well-formed segment index */
#define CS_8080_SB_PER_SEG	4	/* sidebands 0..3 in segment lo, 4..7 in hi */

/* H1: the highest well-formed channel number of a 5 GHz chanspec */
#define CS_CHAN_MAX_5G		224

/* H2: the control channel; channel numbers are 5 MHz apart */
#define CHANS_PER_20MHZ		4	/* from one 20 MHz part to the next */
#define BW_MHZ_20		20
#define BW_MHZ_8080		80	/* width of one segment of an 80+80 channel */
#define CTL_OFFSET_DIV		10	/* (W - 20) / 10: centre to the lowest part */

/* H4, H5: chanspecs made by the helpers (band | bandwidth) */
#define CS_5G_40		(CS_BAND_5G | CS_BW_40)		/* 0xd800 */
#define CS_5G_80		(CS_BAND_5G | CS_BW_80)		/* 0xe000 */
#define CS_80_NONE		0x00ff	/* H5: no sideband gives the primary */

/* H4, V6 step 5.5: centre of an 80 MHz channel to the centres of its 40 MHz halves */
#define CH_HALF_80		4
/* V6 step 4.3: centre of a 40 MHz channel to the channels of its 20 MHz halves */
#define CH_HALF_40		2

/* H6, V6 step 2: the channels of the 2.4 GHz band */
#define CHAN_FACTOR_2G		4814	/* start factor of the 2.4 GHz band */
#define CHAN_2G_FIRST		1
#define CHAN_2G_LAST		14
#define CHAN_14_MHZ		2484
#define CHAN_MHZ_MAX_CHANNEL	200
#define MHZ_PER_CHAN		5
#define MHZ_ERROR		(-1)

/* C4: the bands of struct bcm4360_chan (and of the regulatory database) */
#define BAND_2G			0
#define BAND_5G			1
#define BAND_COUNT		2

/* V1: channels of a valid-channel vector, 0..223 */
#define CHAN_VEC_CHANNELS	(BCM4360_CHAN_VEC_BYTES * 8)

/* V6 step 4.3: the edges of the 20 MHz halves of a 40 MHz channel */
#define CH40_LO_MIN		2	/* lo = ch - 2 above this, else 0 */
#define CH40_HI_LIMIT		222	/* hi = ch + 2 below this, else 0 */

/* C4: bits of locale_flags[b] (purpose not given by the specification) */
#define LOCALE_FLAG_0x04	0x04
#define LOCALE_FLAG_0x08	0x08
#define LOCALE_FLAG_0x10	0x10
#define LOCALE_NO_40		(LOCALE_FLAG_0x08 | LOCALE_FLAG_0x10)	/* V4 */
#define LOCALE_NO_80		(LOCALE_FLAG_0x04 | LOCALE_FLAG_0x10)	/* V5 */

/* C4: bits of bw_cap[b] */
#define BW_CAP_40		0x02
#define BW_CAP_80		0x04

/* C4: the bit of vht_features 80 MHz needs (purpose not given) */
#define VHT_FEATURE_0x100	0x0100

/* V7: the bandwidths of struct bcm4360_clm_limits_params (clm.h) */
#define CLM_BW_20		0
#define CLM_BW_40		1
#define CLM_BW_80		2

/* V7: arguments of clm_limits */
#define CLM_ANT_GAIN		0
#define CLM_LIMITS_TYPE		0

/* C1: the flags of the 20 MHz channel table (names of brcmsmac) */
#define CH_UPPER_SB		0x01
#define CH_LOWER_SB		0x02
#define CH_EWA_VALID		0x04
#define CH_40_LOWER_HALF	(CH_UPPER_SB | CH_EWA_VALID)	/* V6 step 4.5 */

/* docs/re/spec/channel.md, C1: an entry of the 20 MHz channel table */
struct chan_info {
	u8 chan;
	u8 flags;
};

/* docs/re/spec/channel.md, C1: the 20 MHz channel table, in the object's order */
static const struct chan_info chan_info_20[] = {
	{ 1, CH_UPPER_SB | CH_EWA_VALID },
	{ 2, CH_UPPER_SB | CH_EWA_VALID },
	{ 3, CH_UPPER_SB | CH_EWA_VALID },
	{ 4, CH_UPPER_SB | CH_EWA_VALID },
	{ 5, CH_UPPER_SB | CH_LOWER_SB | CH_EWA_VALID },
	{ 6, CH_UPPER_SB | CH_LOWER_SB | CH_EWA_VALID },
	{ 7, CH_UPPER_SB | CH_LOWER_SB | CH_EWA_VALID },
	{ 8, CH_UPPER_SB | CH_LOWER_SB | CH_EWA_VALID },
	{ 9, CH_UPPER_SB | CH_LOWER_SB | CH_EWA_VALID },
	{ 10, CH_LOWER_SB | CH_EWA_VALID },
	{ 11, CH_LOWER_SB | CH_EWA_VALID },
	{ 12, CH_LOWER_SB },
	{ 13, CH_LOWER_SB },
	{ 14, CH_LOWER_SB },
	{ 34, CH_UPPER_SB },
	{ 38, CH_LOWER_SB },
	{ 42, CH_LOWER_SB },
	{ 46, CH_LOWER_SB },
	{ 36, CH_UPPER_SB | CH_EWA_VALID },
	{ 40, CH_LOWER_SB | CH_EWA_VALID },
	{ 44, CH_UPPER_SB | CH_EWA_VALID },
	{ 48, CH_LOWER_SB | CH_EWA_VALID },
	{ 52, CH_UPPER_SB | CH_EWA_VALID },
	{ 56, CH_LOWER_SB | CH_EWA_VALID },
	{ 60, CH_UPPER_SB | CH_EWA_VALID },
	{ 64, CH_LOWER_SB | CH_EWA_VALID },
	{ 100, CH_UPPER_SB | CH_EWA_VALID },
	{ 104, CH_LOWER_SB | CH_EWA_VALID },
	{ 108, CH_UPPER_SB | CH_EWA_VALID },
	{ 112, CH_LOWER_SB | CH_EWA_VALID },
	{ 116, CH_UPPER_SB | CH_EWA_VALID },
	{ 120, CH_LOWER_SB | CH_EWA_VALID },
	{ 124, CH_UPPER_SB | CH_EWA_VALID },
	{ 128, CH_LOWER_SB | CH_EWA_VALID },
	{ 132, CH_UPPER_SB | CH_EWA_VALID },
	{ 136, CH_LOWER_SB | CH_EWA_VALID },
	{ 140, CH_UPPER_SB | CH_EWA_VALID },
	{ 144, CH_LOWER_SB },
	{ 149, CH_UPPER_SB | CH_EWA_VALID },
	{ 153, CH_LOWER_SB | CH_EWA_VALID },
	{ 157, CH_UPPER_SB | CH_EWA_VALID },
	{ 161, CH_LOWER_SB | CH_EWA_VALID },
	{ 165, CH_LOWER_SB },
	{ 184, CH_UPPER_SB },
	{ 188, CH_LOWER_SB },
	{ 192, CH_UPPER_SB },
	{ 196, CH_LOWER_SB },
	{ 200, CH_UPPER_SB },
	{ 204, CH_LOWER_SB },
	{ 208, CH_UPPER_SB },
	{ 212, CH_LOWER_SB },
	{ 216, CH_LOWER_SB },
};

/* docs/re/spec/channel.md, C2: the 80 MHz centre channels V6 accepts */
static const u8 chan80_centres[] = { 42, 58, 106, 122, 138, 155 };

/* docs/re/spec/channel.md, C3: bandwidth in MHz by bandwidth field (bw >> 11) */
static const u8 bw_mhz[8] = { 5, 10, 20, 40, 80, 160, 160, 0 };

/*
 * docs/re/spec/channel.md, C3: 80+80 centre channels by segment index; 8..15
 * are the values the object reads past its 8-byte table.
 */
static const u8 chan8080_centres[16] = {
	42, 58, 106, 122, 138, 155, 50, 114,
	5, 10, 20, 40, 80, 160, 160, 0,
};

/* docs/re/spec/channel.md, H1 step 4: the sideband test */
static bool chspec_sb_malformed(u32 bw, u32 sb)
{
	switch (bw) {
	case CS_BW_20:
		return sb != CS_SB_0;
	case CS_BW_40:
		return sb > CS_SB_1;
	case CS_BW_80:
		return sb > CS_SB_3;
	default:			/* 160 MHz */
		return false;
	}
}

/* docs/re/spec/channel.md, H1 step 2: an 80+80 MHz chanspec */
static bool chspec_8080_malformed(u16 cs)
{
	u32 lo = cs & CS_SEG_MASK;
	u32 hi = (cs >> CS_SEG_HI_SHIFT) & CS_SEG_MASK;

	if (hi > CS_SEG_MAX || lo > CS_SEG_MAX)
		return true;
	return hi <= lo;
}

/* docs/re/spec/channel.md, H1: wf_chspec_malformed */
bool bcm4360_chspec_malformed(u16 cs)
{
	u32 band = cs & CS_BAND_MASK;
	u32 bw = cs & CS_BW_MASK;
	u32 sb = cs & CS_SB_MASK;
	u32 ch = cs & CS_CHAN_MASK;

	if (band == CS_BAND_2G) {
		/* step 1: the channel number is not checked */
		if (bw != CS_BW_20 && bw != CS_BW_40)
			return true;
		return chspec_sb_malformed(bw, sb);
	}
	if (band != CS_BAND_5G)		/* step 3 */
		return true;

	/* step 2 */
	switch (bw) {
	case CS_BW_8080:
		return chspec_8080_malformed(cs);
	case CS_BW_20:
	case CS_BW_40:
	case CS_BW_80:
	case CS_BW_160:
		if (ch > CS_CHAN_MAX_5G)
			return true;
		return chspec_sb_malformed(bw, sb);
	default:
		return true;
	}
}

/*
 * docs/re/spec/channel.md, H2: wf_chspec_ctlchan. All in unsigned 32-bit
 * arithmetic; D = (W - 20) / 10 wraps for W < 20 (D = 152 for 5 and 10 MHz,
 * 151 for W = 0) and the result is the low 8 bits.
 */
u8 bcm4360_chspec_ctlchan(u16 cs)
{
	u32 bw = cs & CS_BW_MASK;
	u32 s, centre, w;
	u8 d;

	/* step 1 */
	if (bw == CS_BW_20)
		return (u8)(cs & CS_CHAN_MASK);

	/* step 2 */
	s = (cs & CS_SB_MASK) >> CS_SB_SHIFT;
	if (bw == CS_BW_8080) {
		u32 seg;

		if (s < CS_8080_SB_PER_SEG) {
			seg = cs & CS_SEG_MASK;
		} else {
			seg = (cs >> CS_SEG_HI_SHIFT) & CS_SEG_MASK;
			s -= CS_8080_SB_PER_SEG;
		}
		centre = chan8080_centres[seg];
		w = BW_MHZ_8080;
	} else {
		centre = cs & CS_CHAN_MASK;
		w = bw_mhz[bw >> CS_BW_SHIFT];
	}

	/* step 3 */
	d = (u8)((w - BW_MHZ_20) / CTL_OFFSET_DIV);
	return (u8)(centre + CHANS_PER_20MHZ * s - d);
}

/* docs/re/spec/channel.md, H3: wf_chspec_ctlchspec */
u16 bcm4360_chspec_ctlchspec(u16 cs)
{
	if ((cs & CS_BW_MASK) == CS_BW_20)
		return cs;
	return (u16)((cs & CS_BAND_MASK) | CS_BW_20 | bcm4360_chspec_ctlchan(cs));
}

/*
 * docs/re/spec/channel.md, H4: wf_chspec_primary40_chspec. The parts are
 * combined with a bitwise OR in 32 bits (ch - 4 below 0 wraps; ch + 4 above
 * 255 runs into the sideband); the result is the low 16 bits.
 */
u16 bcm4360_chspec_primary40(u16 cs)
{
	u32 ch = cs & CS_CHAN_MASK;
	u32 sb = cs & CS_SB_MASK;
	u32 c, s;

	if ((cs & CS_BW_MASK) != CS_BW_80)
		return cs;

	switch (sb) {
	case CS_SB_2:
		c = ch + CH_HALF_80;
		s = CS_SB_0;
		break;
	case CS_SB_3:
		c = ch + CH_HALF_80;
		s = CS_SB_1;
		break;
	default:
		c = ch - CH_HALF_80;
		s = sb;
		break;
	}
	return (u16)(s | c | CS_5G_40);
}

/* docs/re/spec/channel.md, H5: wf_chspec_80 */
u16 bcm4360_chspec_80(u8 centre, u8 primary)
{
	u32 s;

	for (s = CS_SB_0; s <= CS_SB_3; s += CS_SB_STEP) {
		u16 cs = (u16)(centre | s | CS_5G_80);

		if (bcm4360_chspec_ctlchan(cs) == primary)
			return cs;
	}
	return CS_80_NONE;
}

/* docs/re/spec/channel.md, H6: wf_channel2mhz (unsigned arithmetic) */
s32 bcm4360_channel2mhz(u32 channel, u32 start_factor)
{
	if (start_factor == CHAN_FACTOR_2G) {
		/* step 1 */
		if (channel < CHAN_2G_FIRST || channel > CHAN_2G_LAST)
			return MHZ_ERROR;
		if (channel == CHAN_2G_LAST)
			return CHAN_14_MHZ;
	} else if (channel > CHAN_MHZ_MAX_CHANNEL) {
		/* step 2 */
		return MHZ_ERROR;
	}

	/* step 3 */
	return (s32)(start_factor / 2 + channel * MHZ_PER_CHAN);
}

/* docs/re/spec/channel.md, V1: wlc_valid_channel20_in_band */
bool bcm4360_chan_valid20_in_band(const struct bcm4360_chan *c, u32 band, u32 channel)
{
	/*
	 * The specification knows bands 0 and 1 only (every caller passes one);
	 * the object would read beyond the channel manager's two band records.
	 */
	if (band >= BAND_COUNT)
		return false;
	if (channel >= CHAN_VEC_CHANNELS)
		return false;
	return (c->band[band].valid[channel >> 3] >> (channel & 7)) & 1;
}

/* docs/re/spec/channel.md, V2: wlc_valid_channel20 (the current band) */
bool bcm4360_chan_valid20(const struct bcm4360_chan *c, u32 channel)
{
	return bcm4360_chan_valid20_in_band(c, c->bandunit, channel);
}

/* docs/re/spec/channel.md, V3: wlc_valid_channel20_db (dual band) */
bool bcm4360_chan_valid20_db(const struct bcm4360_chan *c, u32 channel)
{
	u32 other;

	/* step 1 */
	if (bcm4360_chan_valid20(c, channel))
		return true;
	/* step 2 */
	if (c->bandlocked)
		return false;
	/* step 3 */
	other = c->bandunit != BAND_5G ? BAND_5G : BAND_2G;
	return bcm4360_chan_valid20_in_band(c, other, channel);
}

/* docs/re/spec/channel.md, V4: wlc_valid_40chanspec_in_band */
bool bcm4360_chan_valid40_in_band(const struct bcm4360_chan *c, u32 band)
{
	if (band >= BAND_COUNT)		/* as V1 */
		return false;
	return !(c->band[band].locale_flags & LOCALE_NO_40) &&
	       (c->band[band].bw_cap & BW_CAP_40);
}

/* docs/re/spec/channel.md, V5: wlc_valid_80chanspec_in_band */
bool bcm4360_chan_valid80_in_band(const struct bcm4360_chan *c, u32 band)
{
	if (band >= BAND_COUNT)		/* as V1 */
		return false;
	return !(c->band[band].locale_flags & LOCALE_NO_80) &&
	       (c->band[band].bw_cap & BW_CAP_80);
}

/*
 * docs/re/spec/channel.md, V6: "valid20(x)" - V3 for a dual-band check, V2
 * (the current band, not the band of the chanspec) otherwise.
 */
static bool chan_valid20_mode(const struct bcm4360_chan *c, u32 channel, bool dualband)
{
	if (dualband)
		return bcm4360_chan_valid20_db(c, channel);
	return bcm4360_chan_valid20(c, channel);
}

/*
 * docs/re/spec/channel.md, V7: whether the regulatory database has power
 * limits for `cs` (sub_06fa51); the limits themselves are thrown away.
 */
static bool chan_clm_has_limits(const struct bcm4360_chan *c, u16 cs)
{
	struct bcm4360_clm_limits_params p;
	struct bcm4360_clm_country_locales loc;
	s8 limits[BCM4360_CLM_NUMRATES];
	u32 country = c->country;		/* step 1 */
	u32 bw = cs & CS_BW_MASK;
	u32 band;

	/* step 2 */
	if (bcm4360_chan_env_clm_limits_params_init(&p) != 0)
		return false;
	/* step 3 */
	if (bcm4360_chan_env_clm_country_def(c, country, &loc) != 0)
		return false;

	/* step 4 */
	if (bw == CS_BW_40)
		p.bw = CLM_BW_40;
	else if (bw == CS_BW_80)
		p.bw = CLM_BW_80;
	else
		p.bw = CLM_BW_20;

	/* step 5 */
	band = (cs & CS_BAND_MASK) == CS_BAND_5G ? BAND_5G : BAND_2G;
	return bcm4360_chan_env_clm_limits(c, &loc, band, cs & CS_CHAN_MASK, CLM_ANT_GAIN,
					   CLM_LIMITS_TYPE, &p, limits) == 0;
}

/* docs/re/spec/channel.md, C1: the flags of `channel` in the table, 0 if none */
static u8 chan_info_flags(u32 channel)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(chan_info_20); i++)
		if (chan_info_20[i].chan == channel)
			return chan_info_20[i].flags;
	return 0;
}

/* docs/re/spec/channel.md, C2: whether `channel` is an 80 MHz centre channel */
static bool chan_is_80_centre(u32 channel)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(chan80_centres); i++)
		if (chan80_centres[i] == channel)
			return true;
	return false;
}

/* docs/re/spec/channel.md, V6 step 4: a 40 MHz chanspec */
static bool chan_valid_chanspec40(const struct bcm4360_chan *c, u16 cs, bool dualband)
{
	u32 ch = cs & CS_CHAN_MASK;
	u32 b = (cs & CS_BAND_MASK) == CS_BAND_5G ? BAND_5G : BAND_2G;
	u32 lo, hi;

	/* steps 4.1, 4.2 */
	if (!c->bw40_capable)
		return false;
	if (!bcm4360_chan_valid40_in_band(c, b))
		return false;

	/* step 4.3 */
	lo = ch > CH40_LO_MIN ? ch - CH_HALF_40 : 0;
	hi = ch < CH40_HI_LIMIT ? ch + CH_HALF_40 : 0;
	if (!chan_valid20_mode(c, lo, dualband) || !chan_valid20_mode(c, hi, dualband))
		return false;

	/* step 4.4 */
	if (!chan_clm_has_limits(c, cs))
		return false;

	/* step 4.5 */
	return (chan_info_flags(lo) & CH_40_LOWER_HALF) == CH_40_LOWER_HALF;
}

/* docs/re/spec/channel.md, V6 step 5: an 80 MHz chanspec */
static bool chan_valid_chanspec80(const struct bcm4360_chan *c, u16 cs, bool dualband)
{
	u32 ch = cs & CS_CHAN_MASK;

	/* step 5.1 */
	if ((cs & CS_BAND_MASK) != CS_BAND_5G || !c->bw80_capable ||
	    !(c->band[BAND_5G].bw_cap & BW_CAP_80) || !c->vht_enab ||
	    !(c->vht_features & VHT_FEATURE_0x100))
		return false;

	/* steps 5.2 - 5.4 */
	if (!bcm4360_chan_valid80_in_band(c, BAND_5G))
		return false;
	if (!chan_is_80_centre(ch))
		return false;
	if (!chan_clm_has_limits(c, cs))
		return false;

	/* step 5.5: both 40 MHz halves (5 GHz, sideband 0), the lower first */
	return bcm4360_chan_valid_chanspec(c, (u16)((ch - CH_HALF_80) | CS_5G_40), dualband) &&
	       bcm4360_chan_valid_chanspec(c, (u16)((ch + CH_HALF_80) | CS_5G_40), dualband);
}

/*
 * docs/re/spec/channel.md, V6: valid chanspec (sub_06faf3); dualband false is
 * wlc_valid_chanspec, true wlc_valid_chanspec_db.
 */
bool bcm4360_chan_valid_chanspec(const struct bcm4360_chan *c, u16 cs, bool dualband)
{
	u32 ch = cs & CS_CHAN_MASK;
	bool is_5g = (cs & CS_BAND_MASK) == CS_BAND_5G;

	/* step 1 */
	if (bcm4360_chspec_malformed(cs))
		return false;

	/* step 2: 5 GHz exactly when the channel is above 14 */
	if (is_5g != (ch > CHAN_2G_LAST))
		return false;

	switch (cs & CS_BW_MASK) {
	case CS_BW_20:		/* step 3 */
		return chan_valid20_mode(c, ch, dualband);
	case CS_BW_40:		/* step 4 */
		return chan_valid_chanspec40(c, cs, dualband);
	case CS_BW_80:		/* step 5 */
		return chan_valid_chanspec80(c, cs, dualband);
	default:		/* step 6: 160, 80+80 MHz */
		return false;
	}
}
