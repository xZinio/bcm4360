// SPDX-License-Identifier: ISC
/*
 * Hybrid build: Broadcom's chanspec helpers (bcmwifi_channels.c) and the
 * validity checks of channels and chanspecs of its channel manager
 * (wlc_channel.c) by the open code of open/lib/channel.c
 * (docs/re/spec/channel.md). Switch open_chan, read at every call.
 *
 * The checks get Broadcom's channel manager (wlc_cm_info); the adapter copies
 * the fields they read (channel.md, C4) into struct bcm4360_chan before each
 * call. The regulatory database is reached through the clm_* functions of the
 * object, which hybrid/clm.c takes over in turn.
 *
 * Results are returned zero-extended to 32 bits: the object's callers test
 * the low byte (bool, u8) or the low 16 bits (chanspec), some the whole
 * register. Arguments narrower than 32 bit arrive as u32 and are truncated.
 */
#include "hybrid.h"
#include <bcm4360/channel.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_chan = true;

#ifdef __KERNEL__
module_param_named(open_chan, bcm4360_open_chan, bool, 0444);
MODULE_PARM_DESC(open_chan, "chanspec helpers and channel validity: open code (1, default) or Broadcom's (0)");
#endif

/* channel.md, C4: Broadcom's channel manager, wlc and pub (offsets) */
#define CM_WLC			0x08	/* struct wlc_info * */
#define CM_COUNTRY		0x18	/* u32 clm_country_t */
#define CM_BAND			0x28	/* per band: u8 locale flags, then the valid vector */
#define CM_BAND_SIZE		0x30
#define WLC_PUB			0x00	/* struct wlc_pub * */
#define WLC_BAND		0x40	/* the current band (wlcband_t *) */
#define WLC_BANDSTATE		0x50	/* wlcband_t *bandstate[2] */
#define WLC_BANDLOCKED		0x24e	/* u8 */
#define BAND_UNIT		0x04	/* u32 bandunit of a wlcband_t */
#define BAND_BW_CAP		0xf1	/* u8 bandwidth capabilities of a wlcband_t */
#define PUB_VHT_ENAB		0x6a	/* u8 _vht_enab */
#define PUB_BW40_CAPABLE	0x6d	/* u8 phy_bw40_capable */
#define PUB_BW80_CAPABLE	0x6e	/* u8 phy_bw80_capable */
#define PUB_VHT_FEATURES	0xf0	/* u16 vht_features */

/* the object's regulatory database functions (hybrid/clm.c) */
s32 clm_limits_params_init(struct bcm4360_clm_limits_params *p);
s32 clm_country_def(u32 country_id, struct bcm4360_clm_country_locales *loc);
s32 clm_limits(const struct bcm4360_clm_country_locales *loc, u32 band, u32 channel,
	       u32 ant_gain, u32 limits_type, const struct bcm4360_clm_limits_params *p,
	       s8 *limits);

HYB_TAKE(u32, wf_chspec_malformed, (u32 cs));
HYB_TAKE(u32, wf_chspec_ctlchan, (u32 cs));
HYB_TAKE(u32, wf_chspec_ctlchspec, (u32 cs));
HYB_TAKE(u32, wf_chspec_primary40_chspec, (u32 cs));
HYB_TAKE(u32, wf_chspec_80, (u32 centre, u32 primary));
HYB_TAKE(s32, wf_channel2mhz, (u32 channel, u32 start_factor));
HYB_TAKE(u32, wlc_valid_channel20_in_band, (void *cm, u32 band, u32 channel));
HYB_TAKE(u32, wlc_valid_channel20, (void *cm, u32 channel));
HYB_TAKE(u32, wlc_valid_channel20_db, (void *cm, u32 channel));
HYB_TAKE(u32, wlc_valid_40chanspec_in_band, (void *cm, u32 band));
HYB_TAKE(u32, wlc_valid_80chanspec_in_band, (void *cm, u32 band));
HYB_TAKE(u32, wlc_valid_chanspec, (void *cm, u32 cs));
HYB_TAKE(u32, wlc_valid_chanspec_db, (void *cm, u32 cs));

static inline const u8 *hyb_ptr(const u8 *base, u32 off)
{
	return *(const u8 * const *)(base + off);
}

/* channel.md, C4: the inputs of the checks, from Broadcom's structures */
static void hyb_chan_view(const void *cm, struct bcm4360_chan *c)
{
	const u8 *m = cm;
	const u8 *wlc = hyb_ptr(m, CM_WLC);
	const u8 *pub = hyb_ptr(wlc, WLC_PUB);
	u32 b;

	c->env = NULL;	/* the object's database functions need no handle */
	c->bandunit = *(const u32 *)(hyb_ptr(wlc, WLC_BAND) + BAND_UNIT);
	c->bandlocked = wlc[WLC_BANDLOCKED] != 0;
	c->bw40_capable = pub[PUB_BW40_CAPABLE] != 0;
	c->bw80_capable = pub[PUB_BW80_CAPABLE] != 0;
	c->vht_enab = pub[PUB_VHT_ENAB] != 0;
	c->vht_features = *(const u16 *)(pub + PUB_VHT_FEATURES);
	c->country = *(const u32 *)(m + CM_COUNTRY);
	for (b = 0; b < 2; b++) {
		const u8 *bs = hyb_ptr(wlc, WLC_BANDSTATE + 8 * b);

		c->band[b].locale_flags = m[CM_BAND + b * CM_BAND_SIZE];
		c->band[b].valid = m + CM_BAND + b * CM_BAND_SIZE + 1;
		c->band[b].bw_cap = bs ? bs[BAND_BW_CAP] : 0;
	}
}

u32 wf_chspec_malformed(u32 cs)
{
	if (!bcm4360_open_chan)
		return bcm4360_blob_wf_chspec_malformed(cs);
	return bcm4360_chspec_malformed((u16)cs);
}

u32 wf_chspec_ctlchan(u32 cs)
{
	if (!bcm4360_open_chan)
		return bcm4360_blob_wf_chspec_ctlchan(cs);
	return bcm4360_chspec_ctlchan((u16)cs);
}

u32 wf_chspec_ctlchspec(u32 cs)
{
	if (!bcm4360_open_chan)
		return bcm4360_blob_wf_chspec_ctlchspec(cs);
	return bcm4360_chspec_ctlchspec((u16)cs);
}

u32 wf_chspec_primary40_chspec(u32 cs)
{
	if (!bcm4360_open_chan)
		return bcm4360_blob_wf_chspec_primary40_chspec(cs);
	return bcm4360_chspec_primary40((u16)cs);
}

u32 wf_chspec_80(u32 centre, u32 primary)
{
	if (!bcm4360_open_chan)
		return bcm4360_blob_wf_chspec_80(centre, primary);
	return bcm4360_chspec_80((u8)centre, (u8)primary);
}

s32 wf_channel2mhz(u32 channel, u32 start_factor)
{
	if (!bcm4360_open_chan)
		return bcm4360_blob_wf_channel2mhz(channel, start_factor);
	return bcm4360_channel2mhz(channel, start_factor);
}

u32 wlc_valid_channel20_in_band(void *cm, u32 band, u32 channel)
{
	struct bcm4360_chan c;

	if (!bcm4360_open_chan)
		return bcm4360_blob_wlc_valid_channel20_in_band(cm, band, channel);
	hyb_chan_view(cm, &c);
	return bcm4360_chan_valid20_in_band(&c, band, channel);
}

u32 wlc_valid_channel20(void *cm, u32 channel)
{
	struct bcm4360_chan c;

	if (!bcm4360_open_chan)
		return bcm4360_blob_wlc_valid_channel20(cm, channel);
	hyb_chan_view(cm, &c);
	return bcm4360_chan_valid20(&c, channel);
}

u32 wlc_valid_channel20_db(void *cm, u32 channel)
{
	struct bcm4360_chan c;

	if (!bcm4360_open_chan)
		return bcm4360_blob_wlc_valid_channel20_db(cm, channel);
	hyb_chan_view(cm, &c);
	return bcm4360_chan_valid20_db(&c, channel);
}

u32 wlc_valid_40chanspec_in_band(void *cm, u32 band)
{
	struct bcm4360_chan c;

	if (!bcm4360_open_chan)
		return bcm4360_blob_wlc_valid_40chanspec_in_band(cm, band);
	hyb_chan_view(cm, &c);
	return bcm4360_chan_valid40_in_band(&c, band);
}

u32 wlc_valid_80chanspec_in_band(void *cm, u32 band)
{
	struct bcm4360_chan c;

	if (!bcm4360_open_chan)
		return bcm4360_blob_wlc_valid_80chanspec_in_band(cm, band);
	hyb_chan_view(cm, &c);
	return bcm4360_chan_valid80_in_band(&c, band);
}

u32 wlc_valid_chanspec(void *cm, u32 cs)
{
	struct bcm4360_chan c;

	if (!bcm4360_open_chan)
		return bcm4360_blob_wlc_valid_chanspec(cm, cs);
	hyb_chan_view(cm, &c);
	return bcm4360_chan_valid_chanspec(&c, (u16)cs, false);
}

u32 wlc_valid_chanspec_db(void *cm, u32 cs)
{
	struct bcm4360_chan c;

	if (!bcm4360_open_chan)
		return bcm4360_blob_wlc_valid_chanspec_db(cm, cs);
	hyb_chan_view(cm, &c);
	return bcm4360_chan_valid_chanspec(&c, (u16)cs, true);
}

/* services of open/lib/channel.c: the object's database functions */
int bcm4360_chan_env_clm_limits_params_init(struct bcm4360_clm_limits_params *p)
{
	return clm_limits_params_init(p);
}

int bcm4360_chan_env_clm_country_def(const struct bcm4360_chan *c, u32 country,
				     struct bcm4360_clm_country_locales *loc)
{
	return clm_country_def(country, loc);
}

int bcm4360_chan_env_clm_limits(const struct bcm4360_chan *c,
				const struct bcm4360_clm_country_locales *loc, u32 band,
				u32 channel, s32 ant_gain, u32 limits_type,
				const struct bcm4360_clm_limits_params *p, s8 *limits)
{
	return clm_limits(loc, band, channel, (u32)ant_gain, limits_type, p, limits);
}
