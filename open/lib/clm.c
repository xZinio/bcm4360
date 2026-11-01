// SPDX-License-Identifier: ISC
/*
 * The regulatory database interpreter (Broadcom's wlc_clm.c): two data sets
 * (base and incremental), countries, aggregates, advertised codes, locales,
 * valid channels, flags and per-rate transmit power limits.
 *
 * Written from docs/re/spec/clm.md. The database is data handed in by the
 * caller (`clm_header` and the tables it reaches); this code only reads it,
 * byte by byte (little endian), as the specification describes.
 */
#include <bcm4360/clm.h>

/* clm.md, "Overview": result codes */
#define CLM_OK			0
#define CLM_ERR			1
#define CLM_NOT_FOUND		(-1)

/* clm.md, "State of the layer": the slots */
#define SET_INC			0
#define SET_BASE		1

/* clm.md, "Identifiers" */
#define ID_SET_SHIFT		30
#define ID_IDX_MASK		0x3fffffffU

/* clm.md, "struct clm_country_locales": special locale indices */
#define LOC_NONE		0xfff	/* no locale (valid, empty) */
#define LOC_INHERIT		0xffe	/* invalid / same as the base set's record */
#define LOC_DELETED		0xffd	/* country deleted */

/* clm.md, "Header" */
#define HDR_MAJOR		0x0a
#define HDR_MINOR		0x0c
#define HDR_SELF		0x30
#define HDR_ROOT		0x38
#define HDR_APP_VERSION		0x5e
#define OLD_FMT_MAJOR		5
#define OLD_FMT_MINOR		1
#define NEW_FMT_MAJOR_MIN	7
#define NEW_FMT_MAJOR_SPAN	9	/* 7..16 */

/* clm.md, "Root table" */
#define ROOT_COMB_2G_20		0x00
#define ROOT_COMB_5G_20		0x08
#define ROOT_RANGES_20		0x10
#define ROOT_RESTRICTED_SETS	0x18
#define ROOT_VALID_SETS		0x20
#define ROOT_RATESETS_5G_20	0x28
#define ROOT_LOCALES		0x30	/* + 8 * locale type */
#define ROOT_COUNTRIES		0x50
#define ROOT_ADV_CC		0x58
#define ROOT_AGGREGATES		0x60
#define ROOT_FLAGS		0x68
#define ROOT_RULES_80		0x70
#define ROOT_RULES_160		0x78
#define ROOT_RANGES_40		0x80
#define ROOT_RANGES_80		0x88
#define ROOT_RANGES_160		0x90
#define ROOT_RATESETS_5G_40	0x98
#define ROOT_RATESETS_5G_80	0xa0
#define ROOT_RATESETS_5G_160	0xa8
#define ROOT_RATESETS_2G_20	0xb0
#define ROOT_RATESETS_2G_40	0xb8
#define ROOT_EXTRA_REGIONS	0xe8
#define ROOT_REV_REMAP		0x168

/* clm.md, "Root table": root flags */
#define RF_CC_SIZE9		BIT(0)
#define RF_APP_VERSION		BIT(1)
#define RF_RULES_160		BIT(3)
#define RF_PER_BW_TABLES	BIT(4)
#define RF_RATESETS_2G		BIT(5)
#define RF_COMB_TABLES		BIT(7)
#define RF_CHECK_SHIFT		8
#define RF_CHECK_MASK		0xff
#define RF_CHECK_OK		0x50
#define RF_CC_LAYOUT		BIT(16)
#define RF_LOC_HI_SHIFT		18
#define RF_LOC_HI_MASK		3
#define RF_REGION_IDX16		BIT(21)
#define RF_REV_HI		BIT(22)
#define RF_EXT_RATESETS		BIT(23)
#define RF_FLAG24		BIT(24)
#define RF_REV_REMAP		BIT(25)

/* clm.md, "Country record" */
#define CC_REV_LO		2
#define CC_LOCALE		3	/* + locale type */
#define CC_EXTRA		7	/* first extra byte */
#define CC_SIZE_PLAIN		7
#define CC_SIZE9		9
#define REV8_ANY		0xff	/* 8-bit revision 0xff */
#define REV_ANY			0xffff

/* clm.md, "Country record": country flags byte */
#define CF_RULES_MASK		0x47
#define CF_FLAG3		BIT(3)
#define CF_FLAG4		BIT(4)
#define CF_FLAG5		BIT(5)

/* clm.md, "Locale tables" */
#define BASE_VALID_SET		0
#define BASE_FLAGS		1
#define BASE_RESTRICTED_SET	2
#define BASE_NREG		3
#define BASE_HDR		4
#define SET_NONE		0xff	/* channel set index: none */
#define RANGE_ALL		0xff	/* channel set byte: the whole comb */
#define LF_DFS_MASK		3
#define LF_FLAG2		BIT(2)

/* clm.md, "Locale tables": power group flags */
#define GF_FLAGS2		BIT(7)
#define GF_WIDTH_SHIFT		4
#define GF_WIDTH_MASK		3
#define GF_WIDTH_MIN		3
#define GF_MORE			BIT(2)
#define GF_EIRP			BIT(1)
#define GF_BW_MASK		0x49
#define GF2_EXT			BIT(1)
#define EXT_RATE_OFFSET		104
#define POWER_DISABLED		0x80
#define REC_POWER		0
#define REC_RANGE		1
#define REC_RATESET		2

/* clm.md, "limits[]" and "clm_limits", step 4 */
#define LIMIT_DISABLED		(-128)
#define LIMIT_NOT_SET		(-127)
#define LIMIT_FILL		0x81

/* clm.md, "Sub-channel rule sets" */
#define RULE_STRIDE_80		7
#define RULE_STRIDE_160		15
#define TABLE_ITEM		16	/* {count, ptr} items */

/* clm.md, "struct clm_limits_params" */
#define BW_20			0
#define BW_40			1
#define BW_80			2
#define BW_160			3
#define BW_80_80		4
#define SAR_DEFAULT		0x7f
#define NUM_CATEGORIES		4
#define NUM_LIMITS_TYPES	15

/* clm.md, "flags" */
#define FL_INIT			0xe6000000U
#define FL_FLAG2		0x4
#define FL_CC3			0x8
#define FL_PER_CATEGORY		0x10
#define FL_CC4			0x20
#define FL_CC5			0x40
#define FL_NO_80_80		0x02000000U
#define FL_NO_160		0x04000000U
#define FL_HAS_OFDM_EIRP	0x08000000U
#define FL_HAS_DSSS_EIRP	0x10000000U
#define FL_NO_MIMO		0x20000000U
#define FL_NO_40		0x40000000U
#define FL_NO_80		0x80000000U

/* clm.md, "flags": rate classes */
#define RC_DSSS			0
#define RC_OFDM			1
#define RC_MCS			2

#define CHAN_BITS		(BCM4360_CLM_CHANVEC * 8)

/* clm.md, "Constant tables": "CLM DATA", the empty code, the default app version */
static const char clm_magic[] = "CLM DATA";
static const char clm_ww[2] = { 'w', 'w' };
static const char clm_default_app_version[] = "Broadcom-0.0";

/* clm.md, "Constant tables": power column per params.category */
static const u8 clm_category_col[NUM_CATEGORIES] = { 0, 3, 4, 5 };

/* clm.md, "limits_type": depth (high nibble) and path per type */
static const u8 clm_limits_type_code[NUM_LIMITS_TYPES] = {
	0x00, 0x10, 0x11, 0x20, 0x21, 0x22, 0x23,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
};

/* clm.md, "Constant tables": (band, bw, root offset) */
struct clm_table_ref {
	u8 band;
	u8 bw;
	u16 off;
};

static const struct clm_table_ref clm_ext_rateset_refs[] = {
	{ 0, 0, 0xf0 }, { 0, 1, 0xf8 }, { 1, 0, 0x100 }, { 1, 1, 0x108 },
	{ 1, 2, 0x110 }, { 1, 4, 0x110 }, { 1, 3, 0x118 },
};

static const struct clm_table_ref clm_comb_refs[] = {
	{ 0, 1, 0xc8 }, { 1, 1, 0xd0 }, { 1, 2, 0xd8 }, { 1, 3, 0xe0 },
};

/* clm.md, "Constant tables": built-in combs of wide channels */
static const u8 clm_def_comb_2g_40[] = { 3, 11, 1 };
static const u8 clm_def_comb_5g_40[] = { 38, 62, 8, 102, 142, 8, 151, 159, 8 };
static const u8 clm_def_comb_5g_80[] = { 42, 58, 16, 106, 138, 16, 155, 155, 16 };
static const u8 clm_def_comb_5g_160[] = { 50, 50, 32, 114, 114, 32 };

#define TRIPLE			3

/* clm.md, "Constant tables": rate class runs (start, count, class), in order */
struct clm_rate_run {
	u16 start;
	u16 count;
	u8 class;
};

static const struct clm_rate_run clm_rate_runs[] = {
	{ 0, 336, RC_MCS }, { 0, 4, RC_DSSS }, { 24, 4, RC_DSSS }, { 104, 4, RC_DSSS },
	{ 4, 8, RC_OFDM }, { 28, 8, RC_OFDM }, { 108, 8, RC_OFDM }, { 72, 8, RC_OFDM },
	{ 164, 8, RC_OFDM }, { 208, 4, RC_DSSS }, { 212, 8, RC_OFDM }, { 280, 8, RC_OFDM },
};

/* clm.md, "Constant tables": group bandwidth map, index group flags & 0x49 */
static const u8 clm_bw_map[GF_BW_MASK + 1] = {
	[0x00] = BW_20, [0x01] = BW_40, [0x08] = BW_80, [0x09] = BW_160, [0x48] = BW_80_80,
};

/* clm.md, "flags": the "no ..." bit of a group bandwidth */
static const u32 clm_bw_flag[BCM4360_CLM_BWS] = {
	0, FL_NO_40, FL_NO_80, FL_NO_160, FL_NO_80_80,
};

/* clm.md, "sub_07502a": a country code and revision */
struct clm_ccrev {
	char cc[2];
	u16 rev;
};

/* clm.md, "sub_076708": an aggregate (list not relocated) */
struct clm_agg {
	struct clm_ccrev id;
	s32 n;
	u64 list;
};

/* clm.md, "sub_07425c": struct clm_locale_def */
struct clm_locale_def {
	const u8 *rec;
	const u8 *const *ranges;
	const u8 *const *ratesets;
	const u8 *valid_sets;
	const u8 *restricted_sets;
	const struct bcm4360_clm_comb *comb;	/* comb[bw] for bw 0..4 */
	const u8 *const *ext_ratesets;
};

/* clm.md, "Locale tables": one power group */
struct clm_group {
	u8 flags;
	u8 flags2;
	u32 width;
	u32 count;
	const u8 *recs;
};

/* clm.md, "Data": little-endian fields */
static inline u16 get_u16(const u8 *p)
{
	return p[0] | (u16)p[1] << 8;
}

static inline u32 get_u32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline u64 get_u64(const u8 *p)
{
	return get_u32(p) | (u64)get_u32(p + 4) << 32;
}

/* clm.md, "Data": `ptr ? ptr + delta : NULL` with the set's delta */
static const u8 *clm_reloc(const struct bcm4360_clm_set *s, u64 ptr)
{
	if (!ptr)
		return NULL;
	return (const u8 *)(unsigned long)(ptr + s->delta);
}

/* clm.md, "Procedures": rptr(k, off) */
static const u8 *clm_rptr(const struct bcm4360_clm_set *s, u32 off)
{
	if (!s->root)
		return NULL;
	return clm_reloc(s, get_u64(s->root + off));
}

/* clm.md, "Procedures": split(id) */
static void clm_split(u32 id, u32 *set, u32 *idx)
{
	*set = ((id - 1) >> ID_SET_SHIFT) & 1;
	*idx = (id - 1) & ID_IDX_MASK;
}

/* clm.md, "Procedures": mkid(set, idx) */
static u32 clm_mkid(u32 set, u32 idx)
{
	return ((set << ID_SET_SHIFT) | (idx & ID_IDX_MASK)) + 1;
}

/* clm.md, "Root table": the root flags (0 for the old format or no data) */
static u32 clm_root_flags(const struct bcm4360_clm_set *s)
{
	return s->root ? get_u32(s->root + ROOT_FLAGS) : 0;
}

/* byte compare up to the first NUL of either string (clm.md, sub_07499e step 2) */
static bool clm_str_equal(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

/* clm.md, "Countries": a code of two NUL characters means "ww" */
static const char *clm_cc_or_ww(const char *cc)
{
	return (!cc[0] && !cc[1]) ? clm_ww : cc;
}

/* clm.md, "Rate set lists" / "Channel set lists": string k of a list */
static const u8 *clm_list_item(const u8 *list, u32 k)
{
	while (k--)
		list += 1 + list[0];
	return list;
}

/* clm.md, "Constant tables": the class of rate index i */
static u32 clm_rate_class(const struct bcm4360_clm *c, u32 i)
{
	return (c->rate_class[i >> 2] >> (2 * (i & 3))) & 3;
}

/* clm.md, "sub_07499e", step 10: rebuild the rate class table */
static void clm_build_rate_class(struct bcm4360_clm *c)
{
	u32 j, r;

	for (j = 0; j < ARRAY_SIZE(clm_rate_runs); j++) {
		const struct clm_rate_run *run = &clm_rate_runs[j];

		for (r = run->start; r < run->start + run->count; r++) {
			u32 sh = 2 * (r & 3);

			c->rate_class[r >> 2] = (c->rate_class[r >> 2] & ~(3 << sh)) |
						run->class << sh;
		}
	}
}

/* clm.md, "sub_0744d5" step 4: the built-in comb of a band and bandwidth */
static void clm_set_comb(struct bcm4360_clm_comb *cb, const u8 *triples, u32 len)
{
	cb->count = len / TRIPLE;
	cb->triples = triples;
}

/* clm.md, "sub_0744d5" (clm_set_chan_combs) */
static void clm_set_chan_combs(struct bcm4360_clm *c, u32 k)
{
	struct bcm4360_clm_set *s = &c->set[k];
	const u8 *src[BCM4360_CLM_BANDS][BCM4360_CLM_BWS] = { { NULL } };
	u32 band, bw, j;

	if (!s->root) {
		for (band = 0; band < BCM4360_CLM_BANDS; band++)
			for (bw = 0; bw < BCM4360_CLM_BWS; bw++)
				s->comb[band][bw].count = 0;
		return;
	}

	src[0][BW_20] = clm_rptr(s, ROOT_COMB_2G_20);
	src[1][BW_20] = clm_rptr(s, ROOT_COMB_5G_20);
	if (s->has_comb_tables)
		for (j = 0; j < ARRAY_SIZE(clm_comb_refs); j++)
			src[clm_comb_refs[j].band][clm_comb_refs[j].bw] =
				clm_rptr(s, clm_comb_refs[j].off);

	for (band = 0; band < BCM4360_CLM_BANDS; band++) {
		for (bw = 0; bw < BCM4360_CLM_BWS; bw++) {
			struct bcm4360_clm_comb *base = &c->set[SET_BASE].comb[band][bw];
			const u8 *t = src[band][bw];

			if (!t || !get_u32(t)) {
				if (k == SET_INC)
					s->comb[band][bw] = *base;
				continue;
			}
			s->comb[band][bw].count = get_u32(t);
			s->comb[band][bw].triples = clm_reloc(s, get_u64(t + 8));
			if (k == SET_INC && !base->count)
				*base = s->comb[band][bw];
		}
	}

	if (!s->has_comb_tables) {
		clm_set_comb(&s->comb[0][BW_40], clm_def_comb_2g_40, sizeof(clm_def_comb_2g_40));
		clm_set_comb(&s->comb[1][BW_40], clm_def_comb_5g_40, sizeof(clm_def_comb_5g_40));
		clm_set_comb(&s->comb[1][BW_80], clm_def_comb_5g_80, sizeof(clm_def_comb_5g_80));
		clm_set_comb(&s->comb[1][BW_160], clm_def_comb_5g_160,
			     sizeof(clm_def_comb_5g_160));
	}
}

/* clm.md, "sub_07499e" step 6 and "Country record": the record layout */
static void clm_set_country_layout(struct bcm4360_clm_set *s, bool new_fmt, u32 f)
{
	s32 next;
	u32 n;

	if (new_fmt && (f & RF_CC_LAYOUT)) {
		n = (f >> RF_LOC_HI_SHIFT) & RF_LOC_HI_MASK;
		s->rev_hi_idx = (f & RF_REV_HI) ? 0 : -1;
		next = s->rev_hi_idx + 1;
		s->loc_hi1_idx = n >= 1 ? next++ : -1;
		s->loc_hi2_idx = n == 2 ? next++ : -1;
		s->flags_idx = next;
		s->country_size = s->flags_idx + CC_EXTRA + 1;
		s->region_fmt = (f & RF_REGION_IDX16) ? 2 : 1;
	} else if (new_fmt && (f & RF_CC_SIZE9)) {
		s->rev_hi_idx = -1;
		s->loc_hi1_idx = 0;
		s->loc_hi2_idx = -1;
		s->flags_idx = 1;
		s->country_size = CC_SIZE9;
		s->region_fmt = 0;
	} else {
		s->rev_hi_idx = -1;
		s->loc_hi1_idx = -1;
		s->loc_hi2_idx = -1;
		s->flags_idx = -1;
		s->country_size = CC_SIZE_PLAIN;
		s->region_fmt = 0;
	}
}

/* clm.md, "sub_07499e" step 7: range tables and rate set lists */
static void clm_set_tables(struct bcm4360_clm_set *s, bool new_fmt, u32 f)
{
	u32 bw, j;

	if (new_fmt && (f & RF_PER_BW_TABLES)) {
		s->ranges[BW_20] = clm_rptr(s, ROOT_RANGES_20);
		s->ranges[BW_40] = clm_rptr(s, ROOT_RANGES_40);
		s->ranges[BW_80] = clm_rptr(s, ROOT_RANGES_80);
		s->ranges[BW_160] = clm_rptr(s, ROOT_RANGES_160);
		s->ranges[BW_80_80] = clm_rptr(s, ROOT_RANGES_80);
		s->ratesets[1][BW_20] = clm_rptr(s, ROOT_RATESETS_5G_20);
		s->ratesets[1][BW_40] = clm_rptr(s, ROOT_RATESETS_5G_40);
		s->ratesets[1][BW_80] = clm_rptr(s, ROOT_RATESETS_5G_80);
		s->ratesets[1][BW_160] = clm_rptr(s, ROOT_RATESETS_5G_160);
		s->ratesets[1][BW_80_80] = clm_rptr(s, ROOT_RATESETS_5G_80);
		if (f & RF_RATESETS_2G) {
			s->ratesets[0][BW_20] = clm_rptr(s, ROOT_RATESETS_2G_20);
			s->ratesets[0][BW_40] = clm_rptr(s, ROOT_RATESETS_2G_40);
		} else {
			s->ratesets[0][BW_20] = s->ratesets[1][BW_20];
			s->ratesets[0][BW_40] = s->ratesets[1][BW_40];
		}
		if (f & RF_EXT_RATESETS)
			for (j = 0; j < ARRAY_SIZE(clm_ext_rateset_refs); j++)
				s->ext_ratesets[clm_ext_rateset_refs[j].band]
					       [clm_ext_rateset_refs[j].bw] =
					clm_rptr(s, clm_ext_rateset_refs[j].off);
		return;
	}

	for (bw = 0; bw < BCM4360_CLM_BWS; bw++) {
		s->ranges[bw] = clm_rptr(s, ROOT_RANGES_20);
		s->ratesets[0][bw] = clm_rptr(s, ROOT_RATESETS_5G_20);
		s->ratesets[1][bw] = s->ratesets[0][bw];
	}
}

/* clm.md, "sub_07499e" (clm_set_data) */
static int clm_set_data(struct bcm4360_clm *c, const void *hdr, u32 k)
{
	struct bcm4360_clm_set *s = &c->set[k];
	const u8 *h = hdr;
	bool new_fmt = true;
	u32 f = 0, j;
	s16 major;

	if (!h) {
		s->delta = 0;
		s->root = NULL;
		for (j = 0; j < BCM4360_CLM_LOC_TYPES; j++)
			s->walk_valid[j] = false;
		goto combs;
	}

	if (!clm_str_equal((const char *)h, clm_magic))
		return CLM_ERR;
	major = (s16)get_u16(h + HDR_MAJOR);
	if (major == OLD_FMT_MAJOR && get_u16(h + HDR_MINOR) == OLD_FMT_MINOR)
		new_fmt = false;
	else if ((u32)(major - NEW_FMT_MAJOR_MIN) > NEW_FMT_MAJOR_SPAN)
		return CLM_ERR;

	for (j = 0; j < BCM4360_CLM_LOC_TYPES; j++)
		s->walk_valid[j] = false;
	s->delta = (u32)(unsigned long)h - (u32)get_u64(h + HDR_SELF);
	s->root = clm_reloc(s, get_u64(h + HDR_ROOT));

	if (new_fmt) {
		f = clm_root_flags(s);
		if (((f >> RF_CHECK_SHIFT) & RF_CHECK_MASK) != RF_CHECK_OK &&
		    ((f >> RF_CHECK_SHIFT) & RF_CHECK_MASK) != 0)
			return CLM_ERR;
		if (f & RF_REV_REMAP)
			s->rev_remap = clm_rptr(s, ROOT_REV_REMAP);
	}

	s->header = h;
	clm_set_country_layout(s, new_fmt, f);
	s->has_160_rules = new_fmt && (f & RF_RULES_160);
	clm_set_tables(s, new_fmt, f);
	s->has_comb_tables = new_fmt && (f & RF_COMB_TABLES);
	s->flag24 = (new_fmt && (f & RF_FLAG24)) ? 1 : 0;

combs:
	clm_set_chan_combs(c, k);
	if (k == SET_BASE)
		clm_set_chan_combs(c, SET_INC);
	clm_build_rate_class(c);
	return CLM_OK;
}

/* clm.md, "clm_init" */
int bcm4360_clm_init(struct bcm4360_clm *c, const void *hdr)
{
	return clm_set_data(c, hdr, SET_BASE);
}

/* clm.md, "clm_set_inc_data" */
int bcm4360_clm_set_inc_data(struct bcm4360_clm *c, const void *hdr)
{
	return clm_set_data(c, hdr, SET_INC);
}

/* clm.md, "Entry point prototypes": clm_iter_init */
int bcm4360_clm_iter_init(u32 *iter)
{
	if (!iter)
		return CLM_ERR;
	*iter = 0;
	return CLM_OK;
}

/* clm.md, "clm_limits_params_init" */
int bcm4360_clm_limits_params_init(struct bcm4360_clm_limits_params *p)
{
	if (!p)
		return CLM_ERR;
	p->bw = BW_20;
	p->category = 0;
	p->sar = SAR_DEFAULT;
	p->other_80_80 = 0;
	return CLM_OK;
}

/* clm.md, "clm_get_base_app_version_string, clm_get_inc_app_version_string" */
static const char *clm_app_version(const struct bcm4360_clm_set *s)
{
	const char *v;

	if (!(clm_root_flags(s) & RF_APP_VERSION) || !s->header)
		return NULL;
	v = (const char *)s->header + HDR_APP_VERSION;
	return clm_str_equal(v, clm_default_app_version) ? NULL : v;
}

const char *bcm4360_clm_get_base_app_version_string(struct bcm4360_clm *c)
{
	return clm_app_version(&c->set[SET_BASE]);
}

const char *bcm4360_clm_get_inc_app_version_string(struct bcm4360_clm *c)
{
	return clm_app_version(&c->set[SET_INC]);
}

/* clm.md, "Locale tables": parse the group at p, return the address after it */
static const u8 *clm_parse_group(const u8 *p, struct clm_group *g)
{
	g->flags = *p++;
	g->flags2 = (g->flags & GF_FLAGS2) ? *p++ : 0;
	g->width = ((g->flags >> GF_WIDTH_SHIFT) & GF_WIDTH_MASK) + GF_WIDTH_MIN;
	g->count = *p++;
	g->recs = p;
	return p + g->count * g->width;
}

/* clm.md, "sub_07425c" step 4: skip one locale record */
static const u8 *clm_skip_locale(const u8 *p, bool base)
{
	struct clm_group g;

	if (base)
		p += BASE_HDR + 2 * p[BASE_NREG];
	do {
		p = clm_parse_group(p, &g);
	} while (g.flags & GF_MORE);
	return p;
}

/*
 * clm.md, "sub_07425c" step 4: locale record idx of a table, with the walk
 * cache of the set (the walk continues from the last record reached when it
 * is not beyond the one wanted).
 */
static const u8 *clm_walk_locales(struct bcm4360_clm_set *s, u32 type, u32 idx, bool base)
{
	const u8 *p;
	u32 i = 0;

	if (s->walk_valid[type] && s->walk_idx[type] <= idx) {
		i = s->walk_idx[type];
		p = s->walk_rec[type];
	} else {
		p = clm_rptr(s, ROOT_LOCALES + 8 * type);
	}
	for (; i < idx; i++)
		p = clm_skip_locale(p, base);
	s->walk_valid[type] = true;
	s->walk_idx[type] = idx;
	s->walk_rec[type] = p;
	return p;
}

/* clm.md, "sub_07425c" (clm_get_locale_def) */
static bool clm_get_locale_def(struct bcm4360_clm *c,
			       const struct bcm4360_clm_country_locales *loc, u32 type,
			       struct clm_locale_def *d)
{
	struct bcm4360_clm_set *s;
	u32 set, idx, band;
	bool base;

	*d = (struct clm_locale_def){ 0 };
	if (type >= BCM4360_CLM_LOC_TYPES)
		return false;
	band = type & 1;
	base = type < 2;

	clm_split(loc->locale[type], &set, &idx);
	if (idx == LOC_NONE)
		return true;
	if (idx == LOC_INHERIT)
		return false;
	s = &c->set[set];

	d->rec = clm_walk_locales(s, type, idx, base);
	d->ranges = s->ranges;
	d->ratesets = s->ratesets[band];
	d->valid_sets = clm_rptr(s, ROOT_VALID_SETS);
	d->restricted_sets = clm_rptr(s, ROOT_RESTRICTED_SETS);
	d->comb = s->comb[band];
	d->ext_ratesets = s->ext_ratesets[band];
	return true;
}

/* clm.md, "sub_07444e" (clm_get_band_locales) */
static bool clm_get_band_locales(struct bcm4360_clm *c,
				 const struct bcm4360_clm_country_locales *loc, u32 band,
				 struct clm_locale_def d[2], u8 *base_flags)
{
	const u8 *rec;

	if (base_flags)
		*base_flags = 0;
	if (!clm_get_locale_def(c, loc, band ? 1 : 0, &d[0]))
		return false;
	rec = d[0].rec;
	if (rec) {
		if (base_flags)
			*base_flags = rec[BASE_FLAGS];
		d[0].rec = rec + BASE_HDR + 2 * rec[BASE_NREG];
	}
	return clm_get_locale_def(c, loc, band ? 3 : 2, &d[1]);
}

/* clm.md, "sub_0748c3" step 3: whether ch lies on the grid of triple t */
static bool clm_on_grid(s32 ch, const u8 *t)
{
	if (!t[2])
		return ch == t[0];
	return (u32)(ch - t[0]) % t[2] == 0;
}

/* clm.md, "sub_0748c3" step 3: first triple (list order) holding ch on its grid */
static const u8 *clm_comb_find(const struct bcm4360_clm_comb *cb, s32 ch)
{
	u32 j;

	for (j = 0; j < cb->count; j++) {
		const u8 *t = cb->triples + TRIPLE * j;

		if (t[0] <= ch && ch <= t[1] && clm_on_grid(ch, t))
			return t;
	}
	return NULL;
}

/* clm.md, "Channel combs": the triple with the smallest first channel > ch */
static const u8 *clm_comb_next(const struct bcm4360_clm_comb *cb, s32 ch)
{
	const u8 *best = NULL;
	u32 j;

	for (j = 0; j < cb->count; j++) {
		const u8 *t = cb->triples + TRIPLE * j;

		if (t[0] > ch && (!best || t[0] < best[0]))
			best = t;
	}
	return best;
}

/* clm.md, "sub_0748c3" (clm_channel_in_range) */
static bool clm_channel_in_range(u32 chan, const u8 *range, const struct bcm4360_clm_comb *cb,
				 u32 other)
{
	s32 ch = (s32)chan;
	const u8 *t;

	if (!cb->count)
		return chan == range[0] && other == range[1];
	if (ch < range[0] || ch > range[1])
		return false;
	t = clm_comb_find(cb, range[0]);
	if (!t)
		return false;
	while (ch > t[1]) {
		t = clm_comb_next(cb, t[1]);
		if (!t)
			return false;
	}
	return ch >= t[0] && clm_on_grid(ch, t);
}

/* set bit ch of a channel vector */
static void clm_vec_set(u8 *vec, u32 ch)
{
	if (ch < CHAN_BITS)
		vec[ch >> 3] |= 1 << (ch & 7);
}

/* clm.md, "sub_07473a" step 3: the channels of the comb in range (first, last) */
static void clm_set_range_channels(u8 *vec, const u8 *range, const struct bcm4360_clm_comb *cb)
{
	s32 first = range[0], last = range[1], ch;
	const u8 *t;

	t = clm_comb_find(cb, first);
	if (!t)
		return;
	ch = first;
	for (;;) {
		clm_vec_set(vec, ch);
		if (ch >= last)
			return;
		if (ch < t[1] && t[2]) {
			ch += t[2];
		} else {
			t = clm_comb_next(cb, ch);
			if (!t || t[0] > last)
				return;
			ch = t[0];
		}
	}
}

/* clm.md, "sub_07473a" step 3: every channel of the comb */
static void clm_set_comb_channels(u8 *vec, const struct bcm4360_clm_comb *cb)
{
	u32 j, ch;

	for (j = 0; j < cb->count; j++) {
		const u8 *t = cb->triples + TRIPLE * j;

		for (ch = t[0]; ch <= t[1]; ch += t[2]) {
			clm_vec_set(vec, ch);
			if (!t[2])
				break;
		}
	}
}

/* clm.md, "sub_07473a" (clm_get_channel_set) */
static void clm_get_channel_set(u8 *vec, const u8 *set_list, u8 n, const u8 *ranges,
				const struct bcm4360_clm_comb *cb)
{
	const u8 *set;
	u32 j;

	if (!vec)
		return;
	for (j = 0; j < BCM4360_CLM_CHANVEC; j++)
		vec[j] = 0;
	if (!set_list || n == SET_NONE)
		return;
	set = clm_list_item(set_list, n);
	for (j = 1; j <= set[0]; j++) {
		if (set[j] == RANGE_ALL)
			clm_set_comb_channels(vec, cb);
		else
			clm_set_range_channels(vec, ranges + 2 * set[j], cb);
	}
}

/* clm.md, "clm_limits" step 2: the channel at each bandwidth level, 1 on a bad type */
static int clm_limits_chans(u32 bw, u32 channel, u32 limits_type, u8 chans[BCM4360_CLM_BWS])
{
	u32 code = clm_limits_type_code[limits_type];
	s32 lev = bw == BW_80_80 ? BW_80 : (s32)bw;
	u32 half = 2 << lev;
	u32 m = 1 << (code >> 4);
	u32 ch = channel;

	for (;;) {
		m >>= 1;
		chans[lev] = (u8)ch;
		if (!m)
			return CLM_OK;
		if (--lev < 0)
			return CLM_ERR;
		half >>= 1;
		if (m & code)
			ch += half;
		else
			ch -= half;
	}
}

/* clm.md, "Sub-channel rule sets": rule set i of the table at root offset off */
static void clm_rule_set(const struct bcm4360_clm_set *s, u32 off, u32 i, const u8 **rules,
			 u32 *n)
{
	const u8 *t = clm_rptr(s, off);
	const u8 *item;

	if (!t || i >= get_u32(t))
		return;
	item = clm_reloc(s, get_u64(t + 8)) + TABLE_ITEM * i;
	*n = get_u32(item);
	*rules = clm_reloc(s, get_u64(item + 8));
}

/* clm.md, "sub_07512f" (clm_apply_subchan_rule) */
static void clm_apply_subchan_rule(u8 lc[BCM4360_CLM_BWS], const u8 chans[BCM4360_CLM_BWS],
				   u32 limits_type, u32 channel, const u8 *ranges,
				   const struct bcm4360_clm_comb *cb, const u8 *rules, u32 n,
				   u32 stride)
{
	u32 j, i;

	for (j = 0; j < n; j++) {
		const u8 *rule = rules + j * stride;

		if (clm_channel_in_range(channel, ranges + 2 * rule[0], cb, 0)) {
			for (i = 0; i < BCM4360_CLM_BWS; i++)
				if (rule[limits_type] & BIT(i))
					lc[i] = chans[i];
			return;
		}
	}
}

/* clm.md, "clm_limits" step 7.1: the channel to match per group bandwidth */
static void clm_limits_match_chans(struct bcm4360_clm *c,
				   const struct bcm4360_clm_country_locales *loc, u32 band,
				   u32 channel, u32 limits_type,
				   const struct bcm4360_clm_limits_params *p,
				   const u8 chans[BCM4360_CLM_BWS], u8 lc[BCM4360_CLM_BWS])
{
	const struct bcm4360_clm_set *cs = &c->set[loc->set & 1];
	const u8 *rules80 = NULL, *rules160 = NULL;
	u32 n80 = 0, n160 = 0, f, i;
	u8 ch = (u8)channel;

	for (i = 0; i < BCM4360_CLM_BWS; i++)
		lc[i] = 0;
	if (!limits_type) {
		lc[p->bw] = ch;
		return;
	}

	/* clm_limits, step 6: the sub-channel rule sets of the country's set */
	f = loc->flags & CF_RULES_MASK;
	if (f) {
		i = ((f & 7) | ((f >> 3) & 8)) - 1;
		clm_rule_set(cs, ROOT_RULES_80, i, &rules80, &n80);
		if (cs->has_160_rules)
			clm_rule_set(cs, ROOT_RULES_160, i, &rules160, &n160);
	}

	switch (p->bw) {
	case BW_40:
		lc[BW_40] = ch;
		break;
	case BW_80:
	case BW_80_80:
		clm_apply_subchan_rule(lc, chans, limits_type, ch, cs->ranges[BW_80],
				       &cs->comb[band][BW_80], rules80, n80, RULE_STRIDE_80);
		if (p->bw == BW_80_80) {
			lc[BW_80_80] = lc[BW_80];
			lc[BW_80] = 0;
		}
		break;
	case BW_160:
		clm_apply_subchan_rule(lc, chans, limits_type, ch, cs->ranges[BW_160],
				       &cs->comb[band][BW_160], rules160, n160, RULE_STRIDE_160);
		break;
	}
}

/* clm.md, "clm_limits" step 7.2: "apply v to limits[i]" */
static void clm_apply_limit(s8 *limits, u32 i, s8 v)
{
	s8 cur;

	if (i >= BCM4360_CLM_NUMRATES)
		return;
	cur = limits[i];
	if (cur == LIMIT_NOT_SET || (v < cur && cur != LIMIT_DISABLED))
		limits[i] = v;
}

/* clm.md, "clm_limits" step 7.2: the class of rate index i (none beyond the table) */
static u32 clm_rate_class_at(const struct bcm4360_clm *c, u32 i)
{
	return i < BCM4360_CLM_NUMRATES ? clm_rate_class(c, i) : RC_MCS;
}

/* clm.md, "clm_limits": the query of one call */
struct clm_limits_query {
	u32 ant_gain;
	const struct bcm4360_clm_limits_params *p;
	const u8 *lc;
	u8 dsss_chan;
	s8 *limits;
	bool found;
};

/* clm.md, "clm_limits" step 7.2: the power v of record r (category column col) */
static s8 clm_record_power(const struct clm_limits_query *q, const struct clm_group *g, u8 v)
{
	if (v == POWER_DISABLED)
		return LIMIT_DISABLED;
	if (g->flags & GF_EIRP)
		v = (u8)(v - q->ant_gain);
	if (q->p->sar < (s8)v)
		v = (u8)q->p->sar;
	return (s8)v;
}

/* clm.md, "clm_limits" step 7.2: one power group of a locale */
static void clm_limits_group(struct bcm4360_clm *c, struct clm_limits_query *q,
			     const struct clm_locale_def *l, const struct clm_group *g)
{
	u32 b = clm_bw_map[g->flags & GF_BW_MASK];
	const struct bcm4360_clm_comb *cb = &l->comb[b];
	const u8 *ranges = l->ranges[b];
	u32 lch = q->lc[b];
	u32 col = g->width == GF_WIDTH_MIN ? 0 : clm_category_col[q->p->category];
	const u8 *list = (g->flags2 & GF2_EXT) ? l->ext_ratesets[b] : l->ratesets[b];
	u32 offset = (g->flags2 & GF2_EXT) ? EXT_RATE_OFFSET : 0;
	u32 j, x;

	for (j = 0; j < g->count; j++) {
		const u8 *r = g->recs + j * g->width;
		const u8 *rng = ranges + 2 * r[REC_RANGE];
		const u8 *rs = list ? clm_list_item(list, r[REC_RATESET]) : NULL;
		s8 v;

		if (col >= g->width) {
			if (!q->found && lch &&
			    clm_channel_in_range(lch, rng, cb, q->p->other_80_80))
				q->found = r[REC_POWER] != POWER_DISABLED;
			continue;
		}

		v = clm_record_power(q, g, r[col]);
		if (lch && clm_channel_in_range(lch, rng, cb, q->p->other_80_80)) {
			for (x = 1; rs && x <= rs[0]; x++) {
				u32 i = rs[x] + offset;

				if (!q->dsss_chan || clm_rate_class_at(c, i) != RC_DSSS)
					clm_apply_limit(q->limits, i, v);
			}
			if (v != LIMIT_DISABLED)
				q->found = true;
		}
		if (b == BW_20 && q->dsss_chan &&
		    clm_channel_in_range(q->dsss_chan, rng, &l->comb[BW_20], 0)) {
			for (x = 1; rs && x <= rs[0]; x++) {
				u32 i = rs[x] + offset;

				if (clm_rate_class_at(c, i) == RC_DSSS)
					clm_apply_limit(q->limits, i, v);
			}
		}
	}
}

/* clm.md, "clm_limits" */
int bcm4360_clm_limits(struct bcm4360_clm *c, const struct bcm4360_clm_country_locales *loc,
		       u32 band, u32 channel, s32 ant_gain, u32 limits_type,
		       const struct bcm4360_clm_limits_params *p, s8 *limits)
{
	struct clm_locale_def d[2];
	struct clm_limits_query q;
	u8 chans[BCM4360_CLM_BWS] = { 0 };
	u8 lc[BCM4360_CLM_BWS];
	struct clm_group g;
	const u8 *gp;
	u32 i, k;

	if (!limits || !loc || !p || band >= BCM4360_CLM_BANDS ||
	    limits_type >= NUM_LIMITS_TYPES || p->bw >= BCM4360_CLM_BWS ||
	    p->category >= NUM_CATEGORIES)
		return CLM_ERR;
	if (clm_limits_chans(p->bw, channel, limits_type, chans))
		return CLM_ERR;

	q.ant_gain = (u8)ant_gain;
	q.p = p;
	q.lc = lc;
	q.dsss_chan = (band == 0 && p->bw != BW_20 && chans[0]) ? chans[0] : 0;
	q.limits = limits;
	q.found = false;

	for (i = 0; i < BCM4360_CLM_NUMRATES; i++)
		limits[i] = (s8)LIMIT_FILL;
	if (!clm_get_band_locales(c, loc, band, d, NULL))
		return CLM_ERR;

	clm_limits_match_chans(c, loc, band, channel, limits_type, p, chans, lc);
	for (k = 0; k < 2; k++) {
		if (!d[k].rec)
			continue;
		gp = d[k].rec;
		do {
			gp = clm_parse_group(gp, &g);
			clm_limits_group(c, &q, &d[k], &g);
		} while (g.flags & GF_MORE);
	}

	if (!q.found)
		return CLM_NOT_FOUND;
	for (i = 0; i < BCM4360_CLM_NUMRATES; i++)
		if (limits[i] == LIMIT_NOT_SET)
			limits[i] = LIMIT_DISABLED;
	return CLM_OK;
}

/* clm.md, "clm_regulatory_limit" */
int bcm4360_clm_regulatory_limit(struct bcm4360_clm *c,
				 const struct bcm4360_clm_country_locales *loc, u32 band,
				 u32 channel, u32 *limit)
{
	struct clm_locale_def d;
	const u8 *pair;
	u32 j;

	if (band >= BCM4360_CLM_BANDS || !loc || !limit)
		return CLM_ERR;
	if (!clm_get_locale_def(c, loc, band, &d) || !d.rec)
		return CLM_ERR;
	for (j = 0; j < d.rec[BASE_NREG]; j++) {
		pair = d.rec + BASE_HDR + 2 * j;
		if (clm_channel_in_range(channel, d.ranges[BW_20] + 2 * pair[1], &d.comb[BW_20], 0)) {
			*limit = pair[0];
			return CLM_OK;
		}
	}
	return CLM_NOT_FOUND;
}

/* clm.md, "clm_country_flags" step 5: one power group */
static void clm_flags_group(const struct bcm4360_clm *c, const struct clm_locale_def *l,
			    const struct clm_group *g, u64 *flags)
{
	u32 b = clm_bw_map[g->flags & GF_BW_MASK];
	u32 bwbit = clm_bw_flag[b];
	const u8 *list = (g->flags2 & GF2_EXT) ? l->ext_ratesets[b] : l->ratesets[b];
	u32 offset = (g->flags2 & GF2_EXT) ? EXT_RATE_OFFSET : 0;
	u32 j, x;

	if (g->width != GF_WIDTH_MIN)
		*flags |= FL_PER_CATEGORY;
	for (j = 0; j < g->count; j++) {
		const u8 *r = g->recs + j * g->width;
		const u8 *rs;

		if (r[REC_POWER] == POWER_DISABLED)
			continue;
		if (bwbit)
			*flags &= ~(u64)bwbit;
		rs = list ? clm_list_item(list, r[REC_RATESET]) : NULL;
		for (x = 1; rs && x <= rs[0]; x++) {
			u32 i = rs[x] + offset;
			u32 cls;

			if (i >= BCM4360_CLM_NUMRATES)
				continue;
			cls = clm_rate_class(c, i);
			if (cls == RC_OFDM && (g->flags & GF_EIRP))
				*flags |= FL_HAS_OFDM_EIRP;
			else if (cls == RC_MCS)
				*flags &= ~(u64)FL_NO_MIMO;
			else if (cls == RC_DSSS && (g->flags & GF_EIRP))
				*flags |= FL_HAS_DSSS_EIRP;
		}
		bwbit = 0;
	}
}

/* clm.md, "clm_country_flags" */
int bcm4360_clm_country_flags(struct bcm4360_clm *c, const struct bcm4360_clm_country_locales *loc,
			      u32 band, u64 *flags)
{
	struct clm_locale_def d[2];
	struct clm_group g;
	const u8 *gp;
	u8 bf;
	u32 k;

	if (!flags || !loc || band >= BCM4360_CLM_BANDS)
		return CLM_ERR;
	*flags = FL_INIT;
	if (!clm_get_band_locales(c, loc, band, d, &bf))
		return CLM_ERR;

	*flags |= bf & LF_DFS_MASK;
	if (bf & LF_FLAG2)
		*flags |= FL_FLAG2;
	for (k = 0; k < 2; k++) {
		if (!d[k].rec)
			continue;
		gp = d[k].rec;
		do {
			gp = clm_parse_group(gp, &g);
			clm_flags_group(c, &d[k], &g, flags);
		} while (g.flags & GF_MORE);
	}

	if (loc->flags & CF_FLAG3)
		*flags |= FL_CC3;
	if (loc->flags & CF_FLAG4)
		*flags |= FL_CC4;
	if (loc->flags & CF_FLAG5)
		*flags |= FL_CC5;
	return CLM_OK;
}

/* clm.md, "clm_country_channels" */
int bcm4360_clm_country_channels(struct bcm4360_clm *c,
				 const struct bcm4360_clm_country_locales *loc, u32 band,
				 u8 *valid, u8 *restricted)
{
	struct clm_locale_def d;
	u8 local[32];
	u32 j;

	if (band >= BCM4360_CLM_BANDS || !loc)
		return CLM_ERR;
	if (!valid && !restricted)
		return CLM_OK;
	if (!valid)
		valid = local;
	if (!clm_get_locale_def(c, loc, band, &d))
		return CLM_ERR;

	if (!d.rec) {
		for (j = 0; j < BCM4360_CLM_CHANVEC; j++) {
			valid[j] = 0;
			if (restricted)
				restricted[j] = 0;
		}
		return CLM_OK;
	}

	clm_get_channel_set(valid, d.valid_sets, d.rec[BASE_VALID_SET], d.ranges[BW_20],
			    &d.comb[BW_20]);
	clm_get_channel_set(restricted, d.restricted_sets, d.rec[BASE_RESTRICTED_SET],
			    d.ranges[BW_20], &d.comb[BW_20]);
	if (restricted)
		for (j = 0; j < BCM4360_CLM_CHANVEC; j++)
			restricted[j] &= valid[j];
	return CLM_OK;
}

/* clm.md, "sub_07657e" (clm_get_table_item) */
static const u8 *clm_get_table_item(const struct bcm4360_clm_set *s, u32 off, u32 stride, s32 i,
				    s32 *count)
{
	const u8 *t = clm_rptr(s, off);
	u64 base;

	if (!t) {
		if (count)
			*count = -1;
		return NULL;
	}
	if (count)
		*count = (s32)get_u32(t);
	if (i >= (s32)get_u32(t))
		return NULL;
	base = get_u64(t + 8);
	base = base ? base + s->delta : 0;
	return (const u8 *)(unsigned long)(base + (u64)(s64)i * stride);
}

/* clm.md, "sub_0765ec" (clm_get_country_rec) */
static const u8 *clm_get_country_rec(const struct bcm4360_clm_set *s, s32 i, s32 *count)
{
	return clm_get_table_item(s, ROOT_COUNTRIES, s->country_size, i, count);
}

/* clm.md, "sub_074f7b" (clm_remap_rev) and "Revision remap table" */
static void clm_remap_rev(const struct bcm4360_clm_set *s, struct clm_ccrev *ccrev)
{
	const u8 *r = s->rev_remap;
	const u8 *countries, *entries, *e;
	u32 n, j, first, last, m;

	n = get_u32(r);
	countries = clm_reloc(s, get_u64(r + 8));
	entries = clm_reloc(s, get_u64(r + 16));
	for (j = 0; j < n; j++) {
		e = countries + 4 * j;
		if (e[0] == (u8)ccrev->cc[0] && e[1] == (u8)ccrev->cc[1])
			break;
	}
	if (j == n)
		return;
	first = get_u16(countries + 4 * j + 2);
	last = get_u16(countries + 4 * (j + 1) + 2);
	for (m = first; m < last; m++) {
		e = entries + 3 * m;
		if (e[2] == ccrev->rev) {
			ccrev->rev = (u16)(e[0] << 8 | e[1]);
			return;
		}
	}
}

/* clm.md, "sub_07502a" (clm_get_country_ccrev) */
static void clm_get_country_ccrev(const struct bcm4360_clm_set *s, const u8 *rec,
				  struct clm_ccrev *out)
{
	out->cc[0] = rec[0];
	out->cc[1] = rec[1];
	out->rev = rec[CC_REV_LO];
	if (s->rev_hi_idx >= 0)
		out->rev = (u16)(rec[CC_EXTRA + s->rev_hi_idx] << 8 | rec[CC_REV_LO]);
	else if (rec[CC_REV_LO] == REV8_ANY)
		out->rev = REV_ANY;
	else if (s->rev_remap)
		clm_remap_rev(s, out);
}

/* clm.md, "sub_07508d" (clm_get_country_locale_idx) */
static u32 clm_get_country_locale_idx(const struct bcm4360_clm_set *s, const u8 *rec, u32 t)
{
	u32 v = rec[CC_LOCALE + t];
	u32 full = 0xff;

	if (s->loc_hi1_idx >= 0) {
		v |= ((rec[CC_EXTRA + s->loc_hi1_idx] >> (2 * t)) & 3) << 8;
		full = (full << 2) | 3;
	}
	if (s->loc_hi2_idx >= 0) {
		v |= ((rec[CC_EXTRA + s->loc_hi2_idx] >> (2 * t)) & 3) << 10;
		full = (full << 2) | 3;
	}
	if (v == full)
		return LOC_NONE;
	if (v == (full & LOC_INHERIT))
		return LOC_INHERIT;
	if (v == (full & LOC_DELETED))
		return LOC_DELETED;
	return v;
}

static bool clm_ccrev_equal(const struct clm_ccrev *a, const struct clm_ccrev *b)
{
	return a->cc[0] == b->cc[0] && a->cc[1] == b->cc[1] && a->rev == b->rev;
}

/* clm.md, "sub_076c6c" (clm_find_country) */
static const u8 *clm_find_country(const struct bcm4360_clm_set *s, const struct clm_ccrev *key,
				  s32 *index)
{
	struct clm_ccrev cr;
	const u8 *rec;
	s32 i;

	for (i = 0;; i++) {
		rec = clm_get_country_rec(s, i, NULL);
		if (!rec)
			return NULL;
		clm_get_country_ccrev(s, rec, &cr);
		if (clm_ccrev_equal(&cr, key)) {
			if (index)
				*index = i;
			return rec;
		}
	}
}

/* clm.md, "clm_country_lookup" */
int bcm4360_clm_country_lookup(struct bcm4360_clm *c, const char cc[2], u32 rev, u32 *country_id)
{
	struct clm_ccrev key;
	const u8 *rec;
	u32 k = SET_INC;
	s32 i = 0;

	if (!cc || !country_id)
		return CLM_ERR;
	cc = clm_cc_or_ww(cc);
	key.cc[0] = cc[0];
	key.cc[1] = cc[1];
	key.rev = (u16)rev;

	rec = clm_find_country(&c->set[k], &key, &i);
	if (!rec) {
		k = SET_BASE;
		rec = clm_find_country(&c->set[k], &key, &i);
		if (!rec)
			return CLM_NOT_FOUND;
	}
	if (clm_get_country_locale_idx(&c->set[k], rec, 0) == LOC_DELETED)
		return CLM_NOT_FOUND;
	*country_id = clm_mkid(k, i);
	return CLM_OK;
}

/* clm.md, "clm_country_iter" */
int bcm4360_clm_country_iter(struct bcm4360_clm *c, u32 *iter, char cc[2], u32 *rev)
{
	struct clm_ccrev cr;
	const u8 *rec;
	u32 k = SET_INC, idx = 0;
	s32 i, n;

	if (!iter || !cc || !rev)
		return CLM_ERR;
	if (*iter) {
		clm_split(*iter, &k, &idx);
		idx++;
	}
	i = (s32)idx;

	for (;; i++) {
		rec = clm_get_country_rec(&c->set[k], i, &n);
		if (!rec) {
			if (k == SET_INC) {
				k = SET_BASE;
				i = -1;
				continue;
			}
			*iter = clm_mkid(SET_BASE, n >= 0 ? n : 0);
			return CLM_NOT_FOUND;
		}
		clm_get_country_ccrev(&c->set[k], rec, &cr);
		if (clm_get_country_locale_idx(&c->set[k], rec, 0) == LOC_DELETED)
			continue;
		if (k == SET_BASE && c->set[SET_INC].root &&
		    clm_find_country(&c->set[SET_INC], &cr, NULL))
			continue;
		cc[0] = cr.cc[0];
		cc[1] = cr.cc[1];
		*rev = cr.rev;
		*iter = clm_mkid(k, i);
		return CLM_OK;
	}
}

/* clm.md, "clm_country_def" */
int bcm4360_clm_country_def(struct bcm4360_clm *c, u32 country_id,
			    struct bcm4360_clm_country_locales *loc)
{
	const struct bcm4360_clm_set *s;
	const u8 *rec, *base = NULL;
	struct clm_ccrev cr;
	u32 k, idx, t, v, vset;

	if (!loc)
		return CLM_ERR;
	clm_split(country_id, &k, &idx);
	s = &c->set[k];
	rec = clm_get_country_rec(s, (s32)idx, NULL);
	if (!rec)
		return CLM_NOT_FOUND;

	for (t = 0; t < BCM4360_CLM_LOC_TYPES; t++) {
		v = clm_get_country_locale_idx(s, rec, t);
		vset = k;
		if (v == LOC_INHERIT) {
			if (!base) {
				clm_get_country_ccrev(s, rec, &cr);
				base = clm_find_country(&c->set[SET_BASE], &cr, NULL);
			}
			v = base ? clm_get_country_locale_idx(&c->set[SET_BASE], base, t) :
				   LOC_NONE;
			vset = SET_BASE;
		}
		loc->locale[t] = clm_mkid(vset, v);
	}
	loc->flags = s->flags_idx >= 0 ? rec[CC_EXTRA + s->flags_idx] : 0;
	loc->set = k;
	return CLM_OK;
}

/* clm.md, "Region lists": an inline 3-byte region (cc[2], rev8) */
static void clm_region_inline(const struct bcm4360_clm_set *s, const u8 *e, struct clm_ccrev *out)
{
	out->cc[0] = e[0];
	out->cc[1] = e[1];
	out->rev = e[2];
	if (e[2] == REV8_ANY)
		out->rev = REV_ANY;
	else if (s->rev_remap)
		clm_remap_rev(s, out);
}

/* clm.md, "sub_076611" (clm_get_region_ccrev) */
static void clm_get_region_ccrev(const struct bcm4360_clm_set *s, struct clm_ccrev *out,
				 const u8 *list, s32 i)
{
	const u8 *e, *rec;
	s32 x, n;

	if (s->region_fmt == 0) {
		clm_region_inline(s, list + 3 * i, out);
		return;
	}
	if (s->region_fmt > 2)
		return;

	x = s->region_fmt == 1 ? list[i] : get_u16(list + 2 * i);
	rec = clm_get_country_rec(s, x, &n);
	if (rec) {
		clm_get_country_ccrev(s, rec, out);
		return;
	}
	e = clm_get_table_item(s, ROOT_EXTRA_REGIONS, s->rev_hi_idx >= 0 ? 4 : 3, x - n, NULL);
	if (!e)
		return;
	if (s->rev_hi_idx >= 0) {
		out->cc[0] = e[0];
		out->cc[1] = e[1];
		out->rev = get_u16(e + 2);
		return;
	}
	clm_region_inline(s, e, out);
}

/* clm.md, "clm_country_advertised_cc": whether set 0 has an item for code cc */
static bool clm_inc_has_adv_cc(const struct bcm4360_clm *c, const u8 *cc)
{
	const u8 *item;
	s32 j;

	for (j = 0;; j++) {
		item = clm_get_table_item(&c->set[SET_INC], ROOT_ADV_CC, TABLE_ITEM, j, NULL);
		if (!item)
			return false;
		if (item[0] == cc[0] && item[1] == cc[1])
			return true;
	}
}

/* clm.md, "clm_country_advertised_cc" */
int bcm4360_clm_country_advertised_cc(struct bcm4360_clm *c, u32 country_id, char cc[2])
{
	struct clm_ccrev key, out = { { 0 }, 0 };
	const struct bcm4360_clm_set *s;
	const u8 *rec, *item, *list;
	u32 k, idx;
	s32 j, r, n;

	if (!cc)
		return CLM_ERR;
	clm_split(country_id, &k, &idx);
	rec = clm_get_country_rec(&c->set[k], (s32)idx, NULL);
	if (!rec)
		return CLM_ERR;
	clm_get_country_ccrev(&c->set[k], rec, &key);

	for (k = SET_INC; k <= SET_BASE; k++) {
		s = &c->set[k];
		for (j = 0;; j++) {
			item = clm_get_table_item(s, ROOT_ADV_CC, TABLE_ITEM, j, NULL);
			if (!item)
				break;
			n = (s32)get_u32(item + 4);
			list = clm_reloc(s, get_u64(item + 8));
			if (n == -1)
				continue;
			if (k == SET_BASE && c->set[SET_INC].root && clm_inc_has_adv_cc(c, item))
				continue;
			for (r = 0; r < n; r++) {
				clm_get_region_ccrev(s, &out, list, r);
				if (clm_ccrev_equal(&out, &key)) {
					cc[0] = item[0];
					cc[1] = item[1];
					return CLM_OK;
				}
			}
		}
	}
	cc[0] = key.cc[0];
	cc[1] = key.cc[1];
	return CLM_OK;
}

/* clm.md, "sub_076708" (clm_get_aggregate) */
static bool clm_get_aggregate(const struct bcm4360_clm_set *s, s32 i, struct clm_agg *out,
			      s32 *count)
{
	const u8 *e = clm_get_table_item(s, ROOT_AGGREGATES, TABLE_ITEM, i, count);

	if (!e)
		return false;
	if (s->region_fmt == 0) {
		clm_get_region_ccrev(s, &out->id, e, 0);
	} else {
		out->id.cc[0] = e[0];
		out->id.cc[1] = e[1];
		out->id.rev = get_u16(e + 2);
	}
	out->n = (s32)get_u32(e + 4);
	out->list = get_u64(e + 8);
	return true;
}

/* clm.md, "sub_076781" (clm_find_aggregate) */
static bool clm_find_aggregate(const struct bcm4360_clm_set *s, const struct clm_ccrev *key,
			       s32 *index, struct clm_agg *out)
{
	s32 i;

	for (i = 0;; i++) {
		if (!clm_get_aggregate(s, i, out, NULL))
			return false;
		if (clm_ccrev_equal(&out->id, key)) {
			if (index)
				*index = i;
			return true;
		}
	}
}

/* clm.md, "sub_07699b" (clm_agg_find_region) */
static bool clm_agg_find_region(const struct bcm4360_clm_set *s, const struct clm_agg *agg,
				const char *cc, struct clm_ccrev *out)
{
	const u8 *list;
	s32 r;

	if (!agg || !agg->list)
		return false;
	list = (const u8 *)(unsigned long)(agg->list + s->delta);
	for (r = 0; r < agg->n; r++) {
		clm_get_region_ccrev(s, out, list, r);
		if (out->cc[0] == cc[0] && out->cc[1] == cc[1])
			return true;
	}
	return false;
}

/* clm.md, "clm_agg_country_lookup" */
int bcm4360_clm_agg_country_lookup(struct bcm4360_clm *c, const char cc[2], u32 rev, u32 *agg_id)
{
	struct clm_ccrev key;
	struct clm_agg a;
	u32 k = SET_INC;
	s32 i = 0;

	if (!agg_id || !cc)
		return CLM_ERR;
	cc = clm_cc_or_ww(cc);
	key.cc[0] = cc[0];
	key.cc[1] = cc[1];
	key.rev = (u16)rev;

	if (!clm_find_aggregate(&c->set[k], &key, &i, &a)) {
		k = SET_BASE;
		if (!clm_find_aggregate(&c->set[k], &key, &i, &a))
			return CLM_NOT_FOUND;
	}
	if (a.n == -1)
		return CLM_NOT_FOUND;
	*agg_id = clm_mkid(k, i);
	return CLM_OK;
}

/* clm.md, "clm_agg_country_map_lookup" */
int bcm4360_clm_agg_country_map_lookup(struct bcm4360_clm *c, u32 agg_id, const char cc[2],
				       u32 *rev)
{
	struct clm_ccrev r = { { 0 }, 0 }, key;
	struct clm_agg a;
	u32 k, idx;

	if (!rev || !cc)
		return CLM_ERR;
	cc = clm_cc_or_ww(cc);
	clm_split(agg_id, &k, &idx);
	if (!clm_get_aggregate(&c->set[k], (s32)idx, &a, NULL))
		return CLM_NOT_FOUND;

	for (;;) {
		if (clm_agg_find_region(&c->set[k], &a, cc, &r)) {
			if (r.rev == REV_ANY)
				return CLM_NOT_FOUND;
			*rev = r.rev;
			return CLM_OK;
		}
		if (k == SET_BASE)
			return CLM_NOT_FOUND;
		k = SET_BASE;
		key = a.id;
		if (!clm_find_aggregate(&c->set[k], &key, NULL, &a))
			return CLM_NOT_FOUND;
	}
}
