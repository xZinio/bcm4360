/*
 * Hybrid build: Broadcom's regulatory database interpreter (wlc_clm.c) by the
 * open code of open/lib/clm.c (docs/re/spec/clm.md). The database stays
 * Broadcom's data; the object's callers (wlc_channel.c, wlc_cntry.c) pass it
 * in through clm_init, as before.
 *
 * Every function here has the name and the arguments of the function of the
 * object it takes over (hybrid/takeover.txt). The structures the callers
 * pass have the layout of the specification, which the open structures
 * share, so they are passed through. The object keeps the state of the layer
 * in globals of its own (one per driver); the open code keeps it in one
 * struct bcm4360_clm here. With the switch open_clm off the object's own
 * functions run (bcm4360_blob_*).
 *
 * Integer arguments narrower than 32 bit arrive as u32; the open code takes
 * the bits the specification names.
 */
#include "hybrid.h"
#include <bcm4360/clm.h>

HYB_TAKE(s32, clm_init, (const void *hdr));
HYB_TAKE(s32, clm_set_inc_data, (const void *hdr));
HYB_TAKE(s32, clm_iter_init, (u32 *iter));
HYB_TAKE(s32, clm_limits_params_init, (struct bcm4360_clm_limits_params *p));
HYB_TAKE(s32, clm_country_channels, (const struct bcm4360_clm_country_locales *loc, u32 band,
				    u8 *valid, u8 *restricted));
HYB_TAKE(s32, clm_country_flags, (const struct bcm4360_clm_country_locales *loc, u32 band,
				 u64 *flags));
HYB_TAKE(s32, clm_limits, (const struct bcm4360_clm_country_locales *loc, u32 band, u32 channel,
			  u32 ant_gain, u32 limits_type,
			  const struct bcm4360_clm_limits_params *p, s8 *limits));
HYB_TAKE(s32, clm_regulatory_limit, (const struct bcm4360_clm_country_locales *loc, u32 band,
				    u32 channel, u32 *limit));
HYB_TAKE(const char *, clm_get_base_app_version_string, (void));
HYB_TAKE(const char *, clm_get_inc_app_version_string, (void));
HYB_TAKE(s32, clm_agg_country_lookup, (const char *cc, u32 rev, u32 *agg_id));
HYB_TAKE(s32, clm_agg_country_map_lookup, (u32 agg_id, const char *cc, u32 *rev));
HYB_TAKE(s32, clm_country_lookup, (const char *cc, u32 rev, u32 *country_id));
HYB_TAKE(s32, clm_country_iter, (u32 *iter, char *cc, u32 *rev));
HYB_TAKE(s32, clm_country_def, (u32 country_id, struct bcm4360_clm_country_locales *loc));
HYB_TAKE(s32, clm_country_advertised_cc, (u32 country_id, char *cc));

/* the layer's state: one per driver, as the object's globals */
static struct bcm4360_clm hyb_clm;

s32 clm_init(const void *hdr)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_init(hdr);
	return bcm4360_clm_init(&hyb_clm, hdr);
}

s32 clm_set_inc_data(const void *hdr)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_set_inc_data(hdr);
	return bcm4360_clm_set_inc_data(&hyb_clm, hdr);
}

s32 clm_iter_init(u32 *iter)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_iter_init(iter);
	return bcm4360_clm_iter_init(iter);
}

s32 clm_limits_params_init(struct bcm4360_clm_limits_params *p)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_limits_params_init(p);
	return bcm4360_clm_limits_params_init(p);
}

s32 clm_country_channels(const struct bcm4360_clm_country_locales *loc, u32 band, u8 *valid,
			 u8 *restricted)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_country_channels(loc, band, valid, restricted);
	return bcm4360_clm_country_channels(&hyb_clm, loc, band, valid, restricted);
}

s32 clm_country_flags(const struct bcm4360_clm_country_locales *loc, u32 band, u64 *flags)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_country_flags(loc, band, flags);
	return bcm4360_clm_country_flags(&hyb_clm, loc, band, flags);
}

s32 clm_limits(const struct bcm4360_clm_country_locales *loc, u32 band, u32 channel,
	       u32 ant_gain, u32 limits_type, const struct bcm4360_clm_limits_params *p,
	       s8 *limits)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_limits(loc, band, channel, ant_gain, limits_type, p,
					       limits);
	return bcm4360_clm_limits(&hyb_clm, loc, band, channel, (s8)ant_gain, limits_type, p,
				  limits);
}

s32 clm_regulatory_limit(const struct bcm4360_clm_country_locales *loc, u32 band,
			 u32 channel, u32 *limit)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_regulatory_limit(loc, band, channel, limit);
	return bcm4360_clm_regulatory_limit(&hyb_clm, loc, band, channel, limit);
}

const char *clm_get_base_app_version_string(void)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_get_base_app_version_string();
	return bcm4360_clm_get_base_app_version_string(&hyb_clm);
}

const char *clm_get_inc_app_version_string(void)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_get_inc_app_version_string();
	return bcm4360_clm_get_inc_app_version_string(&hyb_clm);
}

s32 clm_agg_country_lookup(const char *cc, u32 rev, u32 *agg_id)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_agg_country_lookup(cc, rev, agg_id);
	return bcm4360_clm_agg_country_lookup(&hyb_clm, cc, rev, agg_id);
}

s32 clm_agg_country_map_lookup(u32 agg_id, const char *cc, u32 *rev)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_agg_country_map_lookup(agg_id, cc, rev);
	return bcm4360_clm_agg_country_map_lookup(&hyb_clm, agg_id, cc, rev);
}

s32 clm_country_lookup(const char *cc, u32 rev, u32 *country_id)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_country_lookup(cc, rev, country_id);
	return bcm4360_clm_country_lookup(&hyb_clm, cc, rev, country_id);
}

s32 clm_country_iter(u32 *iter, char *cc, u32 *rev)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_country_iter(iter, cc, rev);
	return bcm4360_clm_country_iter(&hyb_clm, iter, cc, rev);
}

s32 clm_country_def(u32 country_id, struct bcm4360_clm_country_locales *loc)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_country_def(country_id, loc);
	return bcm4360_clm_country_def(&hyb_clm, country_id, loc);
}

s32 clm_country_advertised_cc(u32 country_id, char *cc)
{
	if (!bcm4360_open_clm)
		return bcm4360_blob_clm_country_advertised_cc(country_id, cc);
	return bcm4360_clm_country_advertised_cc(&hyb_clm, country_id, cc);
}
