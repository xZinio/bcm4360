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

#define BCM4360_CLM_BANDS	2	/* 2.4 GHz, 5 GHz */
#define BCM4360_CLM_BWS		5	/* 20, 40, 80, 160, 80+80 MHz */
#define BCM4360_CLM_LOC_TYPES	4	/* base 2.4, base 5, HT 2.4, HT 5 GHz */

/* clm.md, "Channel combs": the (first, last, step) triples of a band and bandwidth */
struct bcm4360_clm_comb {
	u32 count;
	const u8 *triples;
};

/*
 * clm.md, "State of the layer": one data-set slot. Pointers are stored
 * relocated (the database's pointer plus `delta`).
 */
struct bcm4360_clm_set {
	const u8 *root;		/* the set's root table, NULL when not loaded */
	u32 delta;		/* relocation delta of the set */
	struct bcm4360_clm_comb comb[BCM4360_CLM_BANDS][BCM4360_CLM_BWS];
	bool has_comb_tables;
	s32 rev_hi_idx;		/* extra-byte indices of a country record, -1: none */
	u32 country_size;
	const u8 *header;	/* the header as passed (not relocated) */
	bool has_160_rules;
	const u8 *ranges[BCM4360_CLM_BWS];
	const u8 *ratesets[BCM4360_CLM_BANDS][BCM4360_CLM_BWS];
	s32 loc_hi1_idx;
	s32 loc_hi2_idx;
	s32 flags_idx;
	u32 region_fmt;
	const u8 *ext_ratesets[BCM4360_CLM_BANDS][BCM4360_CLM_BWS];
	u32 flag24;		/* root flags bit 24, not used */
	const u8 *rev_remap;

	/*
	 * clm.md, "Notes for an implementation": cache of the locale walk, per
	 * locale type the last record reached (index and address); dropped
	 * when the set is loaded.
	 */
	bool walk_valid[BCM4360_CLM_LOC_TYPES];
	u32 walk_idx[BCM4360_CLM_LOC_TYPES];
	const u8 *walk_rec[BCM4360_CLM_LOC_TYPES];
};

#define BCM4360_CLM_RATE_CLASS_BYTES	((BCM4360_CLM_NUMRATES + 3) / 4)

/*
 * The state of the layer: the two data sets (clm.md, "State of the layer"),
 * slot 0 incremental, slot 1 base, and what the specification keeps beside
 * them. The caller provides one, zeroed, per driver.
 */
struct bcm4360_clm {
	struct bcm4360_clm_set set[2];
	/* 2 bits per rate index: 0 DSSS, 1 OFDM, 2 HT/VHT; rebuilt by every load */
	u8 rate_class[BCM4360_CLM_RATE_CLASS_BYTES];
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
