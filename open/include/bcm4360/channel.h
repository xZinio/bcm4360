/* SPDX-License-Identifier: ISC */
/*
 * Chanspecs and the validity of channels: the chanspec helpers of Broadcom's
 * bcmwifi_channels.c and the validity checks of its channel manager
 * (wlc_channel.c).
 *
 * Specification: docs/re/spec/channel.md. A chanspec is the 16-bit channel
 * description of that specification ("The chanspec").
 *
 * The validity checks read the state of the channel manager and a few
 * capability fields of the driver; the caller copies them into
 * struct bcm4360_chan before each call (channel.md, C4). The regulatory
 * database is reached through the three services at the end, which have the
 * meaning of the clm_* functions of the same names (clm.md).
 */
#ifndef BCM4360_CHANNEL_H
#define BCM4360_CHANNEL_H

#include <bcm4360/types.h>
#include <bcm4360/clm.h>

/* channel.md, H1-H6: pure functions of their arguments */
bool bcm4360_chspec_malformed(u16 cs);
u8 bcm4360_chspec_ctlchan(u16 cs);
u16 bcm4360_chspec_ctlchspec(u16 cs);
u16 bcm4360_chspec_primary40(u16 cs);
u16 bcm4360_chspec_80(u8 centre, u8 primary);
s32 bcm4360_channel2mhz(u32 channel, u32 start_factor);

/* bytes of a valid-channel vector: channels 0..223, bit c & 7 of byte c >> 3 */
#define BCM4360_CHAN_VEC_BYTES	28

/* channel.md, C4: what the checks read of one band (0 = 2.4 GHz, 1 = 5 GHz) */
struct bcm4360_chan_band {
	const u8 *valid;	/* valid[b]: BCM4360_CHAN_VEC_BYTES bytes */
	u8 locale_flags;	/* locale_flags[b] */
	u8 bw_cap;		/* bw_cap[b] */
};

/* channel.md, C4: the inputs of the validity checks */
struct bcm4360_chan {
	u32 bandunit;		/* the current band */
	bool bandlocked;
	bool bw40_capable;
	bool bw80_capable;
	bool vht_enab;
	u16 vht_features;
	u32 country;		/* country of the regulatory database */
	struct bcm4360_chan_band band[2];
	void *env;		/* the caller's handle for its bcm4360_chan_env_* services */
};

/* channel.md, V1-V6; results 0 or 1 */
bool bcm4360_chan_valid20_in_band(const struct bcm4360_chan *c, u32 band, u32 channel);
bool bcm4360_chan_valid20(const struct bcm4360_chan *c, u32 channel);
bool bcm4360_chan_valid20_db(const struct bcm4360_chan *c, u32 channel);
bool bcm4360_chan_valid40_in_band(const struct bcm4360_chan *c, u32 band);
bool bcm4360_chan_valid80_in_band(const struct bcm4360_chan *c, u32 band);
/* V6 (with V7 inside): dualband false = wlc_valid_chanspec, true = wlc_valid_chanspec_db */
bool bcm4360_chan_valid_chanspec(const struct bcm4360_chan *c, u16 cs, bool dualband);

/* services of the caller: the regulatory database, as clm_* (clm.md) */
int bcm4360_chan_env_clm_limits_params_init(struct bcm4360_clm_limits_params *p);
int bcm4360_chan_env_clm_country_def(const struct bcm4360_chan *c, u32 country,
				     struct bcm4360_clm_country_locales *loc);
int bcm4360_chan_env_clm_limits(const struct bcm4360_chan *c,
				const struct bcm4360_clm_country_locales *loc, u32 band,
				u32 channel, s32 ant_gain, u32 limits_type,
				const struct bcm4360_clm_limits_params *p, s8 *limits);

#endif
