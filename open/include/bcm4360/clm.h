/* SPDX-License-Identifier: ISC */
/*
 * The regulatory database interpreter (Broadcom's wlc_clm.c): country and
 * locale lookup, valid channels, flags, per-rate power limits.
 *
 * Specification: docs/re/spec/clm.md. The database itself (`clm_header` and
 * the tables it reaches) is Broadcom's data; this code only reads it.
 *
 * The structures below have the layout of the specification ("Data"):
 * callers pass them in and get them filled exactly as the object does.
 * Results: 0 ok, 1 error (bad argument), -1 not found, as the spec says per
 * function.
 */
#ifndef BCM4360_CLM_H
#define BCM4360_CLM_H

#include <bcm4360/types.h>

/* clm.md, "struct clm_country_locales" (bytes 0x12..0x13 are never written) */
struct bcm4360_clm_country_locales {
	u32 locale[4];		/* locale ids: base 2.4, base 5, HT 2.4, HT 5 GHz */
	u8 flags;		/* the country's flags byte */
	u8 set;			/* data set of the country record: 0 incremental, 1 base */
	u8 pad[2];
};

/* clm.md, "struct clm_limits_params" */
struct bcm4360_clm_limits_params {
	u32 bw;			/* 0 = 20, 1 = 40, 2 = 80, 3 = 160, 4 = 80+80 MHz */
	s32 sar;		/* upper bound of every limit that is not disabled */
	u32 category;		/* power column 0..3 */
	u32 other_80_80;	/* the other segment of an 80+80 channel */
};

#define BCM4360_CLM_NUMRATES	336	/* entries of the limits array */
#define BCM4360_CLM_CHANVEC	25	/* bytes of a channel bit vector */

/*
 * The state of the layer: the two data sets (clm.md, "State of the layer"),
 * slot 0 incremental, slot 1 base, and what the specification keeps beside
 * them. The caller provides one, zeroed, per driver.
 */
struct bcm4360_clm {
	/* the implementation's fields (implementer: replace this placeholder) */
	u64 placeholder;
};

/* clm.md, "Loading" */
int bcm4360_clm_init(struct bcm4360_clm *c, const void *hdr);
int bcm4360_clm_set_inc_data(struct bcm4360_clm *c, const void *hdr);

/* clm.md, small entry points */
int bcm4360_clm_iter_init(u32 *iter);
int bcm4360_clm_limits_params_init(struct bcm4360_clm_limits_params *p);
const char *bcm4360_clm_get_base_app_version_string(struct bcm4360_clm *c);
const char *bcm4360_clm_get_inc_app_version_string(struct bcm4360_clm *c);

/* clm.md, "Countries" */
int bcm4360_clm_country_lookup(struct bcm4360_clm *c, const char cc[2], u32 rev, u32 *country_id);
int bcm4360_clm_country_iter(struct bcm4360_clm *c, u32 *iter, char cc[2], u32 *rev);
int bcm4360_clm_country_def(struct bcm4360_clm *c, u32 country_id,
			    struct bcm4360_clm_country_locales *loc);
int bcm4360_clm_country_advertised_cc(struct bcm4360_clm *c, u32 country_id, char cc[2]);
int bcm4360_clm_country_flags(struct bcm4360_clm *c, const struct bcm4360_clm_country_locales *loc,
			      u32 band, u64 *flags);
int bcm4360_clm_country_channels(struct bcm4360_clm *c,
				 const struct bcm4360_clm_country_locales *loc, u32 band,
				 u8 *valid, u8 *restricted);

/* clm.md, "Power limits" */
int bcm4360_clm_limits(struct bcm4360_clm *c, const struct bcm4360_clm_country_locales *loc,
		       u32 band, u32 channel, s32 ant_gain, u32 limits_type,
		       const struct bcm4360_clm_limits_params *p, s8 *limits);
int bcm4360_clm_regulatory_limit(struct bcm4360_clm *c,
				 const struct bcm4360_clm_country_locales *loc, u32 band,
				 u32 channel, u32 *limit);

/* clm.md, "Aggregates and region lists" */
int bcm4360_clm_agg_country_lookup(struct bcm4360_clm *c, const char cc[2], u32 rev, u32 *agg_id);
int bcm4360_clm_agg_country_map_lookup(struct bcm4360_clm *c, u32 agg_id, const char cc[2],
				       u32 *rev);

#endif
