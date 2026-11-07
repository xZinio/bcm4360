/*
 * The per-rate transmit power container (Broadcom's wlc_ppr.c, `ppr_t`): one
 * heap block holding a 32-bit bandwidth word and 1, 2 or 3 sets of 182 power
 * bytes (signed, quarter dBm, -128 = rate disabled), with group access,
 * whole-container arithmetic, queries, element-wise mapping and a serialised
 * form.
 *
 * Written from docs/re/spec/ppr.md. The bytes of a container are those of
 * the specification's "Data" section. Pure computation: no hardware access,
 * no state outside the containers and buffers handed in.
 */
#include <bcm4360/ratepwr.h>
#include <bcm4360/hw.h>

/* docs/re/spec/ppr.md, "ppr_t": the bandwidth word, then the sets */
struct bcm4360_ratepwr {
	u32 ch_bw;
	s8 data[];
};

/* docs/re/spec/ppr.md, "Enumerations": wl_tx_bw_t */
#define TX_BW_20		0
#define TX_BW_40		1
#define TX_BW_80		2
#define TX_BW_20IN40		3
#define TX_BW_20IN80		4
#define TX_BW_40IN80		5
#define TX_BW_NUM_CH		3	/* container bandwidths with data: 20, 40, 80 */

/* docs/re/spec/ppr.md, "Enumerations": wl_tx_mode_t */
#define TX_MODE_TXBF		3

/* docs/re/spec/ppr.md, "Overview": the value of a disabled rate */
#define RATE_DISABLED		(-128)

/* docs/re/spec/ppr.md, "ppr_t" and "Sets": the header and the sets */
#define HDR_SIZE		4
#define SET_SIZE		182
#define SET_1ST			0x04	/* offsets in the ppr_t */
#define SET_2ND			0xba
#define SET_3RD			0x170

/* docs/re/spec/ppr.md, "The 182-byte set": group offsets in a set */
#define GRP_DSSS_1		0x00
#define GRP_OFDM_1		0x04
#define GRP_MCS_1X1		0x0c
#define GRP_DSSS_2		0x16
#define GRP_OFDM_CDD_2		0x1a
#define GRP_MCS_1X2_CDD		0x22
#define GRP_MCS_2X2_STBC	0x2c
#define GRP_MCS_2X2_SDM		0x36
#define GRP_DSSS_3		0x40
#define GRP_OFDM_CDD_3		0x44
#define GRP_MCS_1X3_CDD		0x4c
#define GRP_MCS_2X3_STBC	0x56
#define GRP_MCS_2X3_SDM		0x60
#define GRP_MCS_3X3_SDM		0x6a
#define GRP_OFDM_TXBF_2		0x74
#define GRP_MCS_1X2_TXBF	0x7c
#define GRP_MCS_2X2_TXBF	0x86
#define GRP_OFDM_TXBF_3		0x90
#define GRP_MCS_1X3_TXBF	0x98
#define GRP_MCS_2X3_TXBF	0xa2
#define GRP_MCS_3X3_TXBF	0xac

/* docs/re/spec/ppr.md, "The 182-byte set": group sizes */
#define DSSS_LEN		4
#define OFDM_LEN		8
#define HT_MCS_LEN		8	/* MCS 0..7 */
#define VHT_MCS_LEN		10	/* MCS 0..9 */

/* docs/re/spec/ppr.md, ".rodata tables": the MCS group offsets */
#define MCS_NONE		(-1)
#define MCS_TBL_NSS3		0	/* index: mode */
#define MCS_TBL_NSS2		4	/* index: mode + (chains - 2) * 4 */
#define MCS_TBL_NSS1		12	/* index: mode + (chains - 1) * 4 */
#define MCS_TBL_LEN		24
#define MCS_TBL_CHAIN_STEP	4

/* docs/re/spec/ppr.md, "Serialised form" */
#define SER_HDR_SIZE		8
#define SER_VERSION		1
#define SER_POS_VERSION		0
#define SER_POS_BW		1
#define SER_POS_FLAGS		4
#define SER_FLAGS_ALL		7
#define SER_FLAG_CHAINS		3	/* bits 0..1: chains (0 or 1 = 1, 2, 3) */
#define SER_FLAG_TXBF		BIT(2)
#define SER_INIT_FILL		0x80
#define SER_MAGIC_LEN		4

/* docs/re/spec/ppr.md, "Serialised form": error codes (BCME_*) */
#define BCME_BADARG		(-2)
#define BCME_BUFTOOSHORT	(-14)
#define BCME_NOMEM		(-27)
#define BCME_VERSION		(-37)

/* docs/re/spec/ppr.md, "ppr_get_vht_mcs_min" */
#define VHT_MIN_START		127
#define VHT_MIN_NO_GROUP	(-1)

/* docs/re/spec/ppr.md, "sub_164a23": rates that disable the whole group */
#define FORCE_GROUP_FIRST	8

/* docs/re/spec/ppr.md, "sub_164c6a": 3 dB and 4.75 dB in quarter dB */
#define TOTAL_TX_2_CHAINS	12
#define TOTAL_TX_3_CHAINS	19

/* docs/re/spec/ppr.md, "ppr_multiply_percentage" */
#define PERCENT			100

/* docs/re/spec/ppr.md, "Data": data bytes by container bandwidth 0, 1, 2 */
static const u32 data_size_tbl[TX_BW_NUM_CH] = { 0xb6, 0x16c, 0x222 };

/* docs/re/spec/ppr.md, ".rodata tables": the three MCS tables, contiguous */
static const s32 mcs_group_tbl[MCS_TBL_LEN] = {
	/* nss 3, chains 3 */
	GRP_MCS_3X3_SDM, MCS_NONE, MCS_NONE, GRP_MCS_3X3_TXBF,
	/* nss 2, chains 2 */
	GRP_MCS_2X2_SDM, GRP_MCS_2X2_STBC, MCS_NONE, GRP_MCS_2X2_TXBF,
	/* nss 2, chains 3 */
	GRP_MCS_2X3_SDM, GRP_MCS_2X3_STBC, MCS_NONE, GRP_MCS_2X3_TXBF,
	/* nss 1, chains 1 */
	GRP_MCS_1X1, MCS_NONE, MCS_NONE, MCS_NONE,
	/* nss 1, chains 2 */
	MCS_NONE, MCS_NONE, GRP_MCS_1X2_CDD, GRP_MCS_1X2_TXBF,
	/* nss 1, chains 3 */
	MCS_NONE, MCS_NONE, GRP_MCS_1X3_CDD, GRP_MCS_1X3_TXBF,
};

/* docs/re/spec/ppr.md, "Serialised form": the sets in container order */
static const u16 set_offs[TX_BW_NUM_CH] = { SET_1ST, SET_2ND, SET_3RD };

/* docs/re/spec/ppr.md, "Serialised form": the magic of a prepared buffer */
static const u8 ser_magic[SER_MAGIC_LEN] = { 0xbe, 0xef, 0xc0, 0xff };

/* docs/re/spec/ppr.md, "The 182-byte set": the five chain blocks */
enum chain_blk { BLK_A, BLK_B, BLK_C, BLK_D, BLK_E, BLK_NUM };

struct span {
	u8 off;
	u8 len;
};

static const struct span chain_blks[BLK_NUM] = {
	[BLK_A] = { 0x00, 22 },		/* 1 chain */
	[BLK_B] = { 0x16, 42 },		/* 2 chains */
	[BLK_C] = { 0x40, 52 },		/* 3 chains */
	[BLK_D] = { 0x74, 28 },		/* TXBF, 2 chains */
	[BLK_E] = { 0x90, 38 },		/* TXBF, 3 chains */
};

/* docs/re/spec/ppr.md, "ppr_force_disabled": the 21 groups in visiting order */
static const struct span force_groups[] = {
	{ GRP_DSSS_1, DSSS_LEN },	{ GRP_OFDM_1, OFDM_LEN },
	{ GRP_MCS_1X1, VHT_MCS_LEN },	{ GRP_DSSS_2, DSSS_LEN },
	{ GRP_OFDM_CDD_2, OFDM_LEN },	{ GRP_MCS_1X2_CDD, VHT_MCS_LEN },
	{ GRP_MCS_2X2_STBC, VHT_MCS_LEN }, { GRP_MCS_2X2_SDM, VHT_MCS_LEN },
	{ GRP_OFDM_TXBF_2, OFDM_LEN },	{ GRP_MCS_1X2_TXBF, VHT_MCS_LEN },
	{ GRP_MCS_2X2_TXBF, VHT_MCS_LEN }, { GRP_DSSS_3, DSSS_LEN },
	{ GRP_OFDM_CDD_3, OFDM_LEN },	{ GRP_MCS_1X3_CDD, VHT_MCS_LEN },
	{ GRP_MCS_2X3_STBC, VHT_MCS_LEN }, { GRP_MCS_2X3_SDM, VHT_MCS_LEN },
	{ GRP_MCS_3X3_SDM, VHT_MCS_LEN }, { GRP_OFDM_TXBF_3, OFDM_LEN },
	{ GRP_MCS_1X3_TXBF, VHT_MCS_LEN }, { GRP_MCS_2X3_TXBF, VHT_MCS_LEN },
	{ GRP_MCS_3X3_TXBF, VHT_MCS_LEN },
};

/* docs/re/spec/ppr.md, "ppr_copy_struct", step 2: the order of the sets */
static const u32 copy_bw_order[] = {
	TX_BW_20, TX_BW_40, TX_BW_20IN40, TX_BW_80, TX_BW_20IN80, TX_BW_40IN80,
};

/* docs/re/spec/ppr.md, "Data": data bytes of a bandwidth (0 for >= 3) */
static u32 data_size(u32 bw)
{
	return bw < TX_BW_NUM_CH ? data_size_tbl[bw] : 0;
}

/* docs/re/spec/ppr.md, "Procedures": N(p) */
static u32 data_len(const struct bcm4360_ratepwr *p)
{
	return data_size(p->ch_bw);
}

/* the set at offset `off` of the ppr_t */
static s8 *set_at(const struct bcm4360_ratepwr *p, u32 off)
{
	return (s8 *)p + off;
}

/* docs/re/spec/ppr.md, "sub_164690" (ppr_get_bw_powers_20) */
static s8 *bw_powers_20(const struct bcm4360_ratepwr *p, u32 bw)
{
	return bw == TX_BW_20 ? set_at(p, SET_1ST) : NULL;
}

/* docs/re/spec/ppr.md, "sub_1646a2" (ppr_get_bw_powers_40) */
static s8 *bw_powers_40(const struct bcm4360_ratepwr *p, u32 bw)
{
	switch (bw) {
	case TX_BW_40:
		return set_at(p, SET_1ST);
	case TX_BW_20:
	case TX_BW_20IN40:
		return set_at(p, SET_2ND);
	default:
		return NULL;
	}
}

/* docs/re/spec/ppr.md, "sub_1646c1" (ppr_get_bw_powers_80) */
static s8 *bw_powers_80(const struct bcm4360_ratepwr *p, u32 bw)
{
	switch (bw) {
	case TX_BW_80:
		return set_at(p, SET_1ST);
	case TX_BW_20:
	case TX_BW_20IN40:
	case TX_BW_20IN80:
		return set_at(p, SET_2ND);
	case TX_BW_40:
	case TX_BW_40IN80:
		return set_at(p, SET_3RD);
	default:
		return NULL;
	}
}

/* docs/re/spec/ppr.md, "sub_1646fd" (ppr_get_bw_powers): S(p, bw) */
static s8 *bw_powers(const struct bcm4360_ratepwr *p, u32 bw)
{
	if (!p)
		return NULL;
	switch (p->ch_bw) {
	case TX_BW_20:
		return bw_powers_20(p, bw);
	case TX_BW_40:
		return bw_powers_40(p, bw);
	case TX_BW_80:
		return bw_powers_80(p, bw);
	default:
		return NULL;
	}
}

/*
 * docs/re/spec/ppr.md, "sub_16472e" (ppr_get_mcs_group). An index outside the
 * three MCS tables (nss 1 with chains 0, nss 1 / chains 3 with mode >= 4,
 * large modes) is undefined in the object: no group here.
 */
static s8 *mcs_group(s8 *set, u32 nss, u32 mode, u32 chains)
{
	u64 idx;
	s32 o;

	switch (nss) {
	case 1:
		if (chains > 3 || chains == 0)
			return NULL;
		idx = MCS_TBL_NSS1 + (u64)mode + (u64)(chains - 1) * MCS_TBL_CHAIN_STEP;
		break;
	case 2:
		if (chains != 2 && chains != 3)
			return NULL;
		idx = MCS_TBL_NSS2 + (u64)mode + (u64)(chains - 2) * MCS_TBL_CHAIN_STEP;
		break;
	case 3:
		if (chains != 3)
			return NULL;
		idx = MCS_TBL_NSS3 + (u64)mode;
		break;
	default:
		return NULL;
	}
	if (idx >= MCS_TBL_LEN)
		return NULL;
	o = mcs_group_tbl[idx];
	return o == MCS_NONE ? NULL : set + o;
}

/* docs/re/spec/ppr.md, "sub_16472e", DSSS: the group by chains */
static s8 *dsss_group(s8 *set, u32 chains)
{
	switch (chains) {
	case 1:
		return set + GRP_DSSS_1;
	case 2:
		return set + GRP_DSSS_2;
	case 3:
		return set + GRP_DSSS_3;
	default:
		return NULL;
	}
}

/* docs/re/spec/ppr.md, "sub_16472e", OFDM: the group by chains and mode */
static s8 *ofdm_group(s8 *set, u32 mode, u32 chains)
{
	switch (chains) {
	case 1:
		return set + GRP_OFDM_1;
	case 2:
		return set + (mode == TX_MODE_TXBF ? GRP_OFDM_TXBF_2 : GRP_OFDM_CDD_2);
	case 3:
		return set + (mode == TX_MODE_TXBF ? GRP_OFDM_TXBF_3 : GRP_OFDM_CDD_3);
	default:
		return NULL;
	}
}

/* docs/re/spec/ppr.md, "Group get / set": the DSSS group of S(p, bw) */
static s8 *dsss_of(const struct bcm4360_ratepwr *p, u32 bw, u32 chains)
{
	s8 *s = bw_powers(p, bw);

	return s ? dsss_group(s, chains) : NULL;
}

/* docs/re/spec/ppr.md, "Group get / set": the OFDM group of S(p, bw) */
static s8 *ofdm_of(const struct bcm4360_ratepwr *p, u32 bw, u32 mode, u32 chains)
{
	s8 *s = bw_powers(p, bw);

	return s ? ofdm_group(s, mode, chains) : NULL;
}

/* docs/re/spec/ppr.md, "Group get / set": the MCS group of S(p, bw) */
static s8 *mcs_of(const struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode, u32 chains)
{
	s8 *s = bw_powers(p, bw);

	return s ? mcs_group(s, nss, mode, chains) : NULL;
}

/* docs/re/spec/ppr.md, "Group get / set": getters (0x80 on failure) */
static s32 get_group(const s8 *g, s8 *out, u32 n)
{
	if (!g) {
		hw_memset(out, RATE_DISABLED, n);
		return 0;
	}
	hw_memcpy(out, g, n);
	return n;
}

/* docs/re/spec/ppr.md, "Group get / set": setters (nothing on failure) */
static s32 set_group(s8 *g, const s8 *in, u32 n)
{
	if (!g)
		return 0;
	hw_memcpy(g, in, n);
	return n;
}

/* docs/re/spec/ppr.md, "ppr_set_same_*": every byte of the group := v */
static s32 set_same_group(s8 *g, s8 v, u32 n)
{
	if (!g)
		return 0;
	hw_memset(g, v, n);
	return n;
}

/* docs/re/spec/ppr.md, "ppr_size" */
u32 bcm4360_ratepwr_size(u32 bw)
{
	return HDR_SIZE + data_size(bw);
}

/* docs/re/spec/ppr.md, "ppr_ser_size" */
u32 bcm4360_ratepwr_ser_size(const struct bcm4360_ratepwr *p)
{
	return SER_HDR_SIZE + data_len(p);
}

/* docs/re/spec/ppr.md, "ppr_ser_size_by_bw" */
u32 bcm4360_ratepwr_ser_size_by_bw(u32 bw)
{
	return SER_HDR_SIZE + data_size(bw);
}

/* docs/re/spec/ppr.md, "ppr_get_ch_bw" */
u32 bcm4360_ratepwr_get_ch_bw(const struct bcm4360_ratepwr *p)
{
	return p->ch_bw;
}

/* docs/re/spec/ppr.md, "ppr_get_max_bw" */
u32 bcm4360_ratepwr_get_max_bw(void)
{
	return TX_BW_80;
}

/* docs/re/spec/ppr.md, "ppr_create" */
struct bcm4360_ratepwr *bcm4360_ratepwr_create(void *osh, u32 bw)
{
	struct bcm4360_ratepwr *p = osl_malloc(osh, HDR_SIZE + data_size(bw));

	if (!p)
		return NULL;
	bcm4360_ratepwr_init(p, bw);
	return p;
}

/* docs/re/spec/ppr.md, "ppr_init" */
void bcm4360_ratepwr_init(struct bcm4360_ratepwr *p, u32 bw)
{
	hw_memset(p, RATE_DISABLED, HDR_SIZE + data_size(bw));
	p->ch_bw = bw;
}

/* docs/re/spec/ppr.md, "ppr_delete" (no NULL check) */
void bcm4360_ratepwr_delete(void *osh, struct bcm4360_ratepwr *p)
{
	osl_mfree(osh, p, HDR_SIZE + data_len(p));
}

/* docs/re/spec/ppr.md, "ppr_clear" */
void bcm4360_ratepwr_clear(struct bcm4360_ratepwr *p)
{
	hw_memset(p->data, RATE_DISABLED, data_len(p));
}

/* docs/re/spec/ppr.md, "ppr_set_cmn_val" */
void bcm4360_ratepwr_set_cmn_val(struct bcm4360_ratepwr *p, s8 v)
{
	hw_memset(p->data, v, data_len(p));
}

/* docs/re/spec/ppr.md, "ppr_copy_struct" */
void bcm4360_ratepwr_copy_struct(const struct bcm4360_ratepwr *src,
				 struct bcm4360_ratepwr *dst)
{
	u32 i;

	if (src->ch_bw == dst->ch_bw) {
		hw_memcpy(dst->data, src->data, data_len(src));
		return;
	}
	for (i = 0; i < ARRAY_SIZE(copy_bw_order); i++) {
		s8 *s = bw_powers(src, copy_bw_order[i]);
		s8 *d = bw_powers(dst, copy_bw_order[i]);

		if (s && d)
			hw_memcpy(d, s, SET_SIZE);
	}
}

/* docs/re/spec/ppr.md, "ppr_get_dsss" */
s32 bcm4360_ratepwr_get_dsss(const struct bcm4360_ratepwr *p, u32 bw, u32 chains, s8 *out)
{
	return get_group(dsss_of(p, bw, chains), out, DSSS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_get_ofdm" */
s32 bcm4360_ratepwr_get_ofdm(const struct bcm4360_ratepwr *p, u32 bw, u32 mode, u32 chains,
			     s8 *out)
{
	return get_group(ofdm_of(p, bw, mode, chains), out, OFDM_LEN);
}

/* docs/re/spec/ppr.md, "ppr_get_ht_mcs" */
s32 bcm4360_ratepwr_get_ht_mcs(const struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
			       u32 chains, s8 *out)
{
	return get_group(mcs_of(p, bw, nss, mode, chains), out, HT_MCS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_get_vht_mcs" */
s32 bcm4360_ratepwr_get_vht_mcs(const struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
				u32 chains, s8 *out)
{
	return get_group(mcs_of(p, bw, nss, mode, chains), out, VHT_MCS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_set_dsss" */
s32 bcm4360_ratepwr_set_dsss(struct bcm4360_ratepwr *p, u32 bw, u32 chains, const s8 *in)
{
	return set_group(dsss_of(p, bw, chains), in, DSSS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_set_ofdm" */
s32 bcm4360_ratepwr_set_ofdm(struct bcm4360_ratepwr *p, u32 bw, u32 mode, u32 chains,
			     const s8 *in)
{
	return set_group(ofdm_of(p, bw, mode, chains), in, OFDM_LEN);
}

/* docs/re/spec/ppr.md, "ppr_set_ht_mcs" */
s32 bcm4360_ratepwr_set_ht_mcs(struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
			       u32 chains, const s8 *in)
{
	return set_group(mcs_of(p, bw, nss, mode, chains), in, HT_MCS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_set_vht_mcs" */
s32 bcm4360_ratepwr_set_vht_mcs(struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
				u32 chains, const s8 *in)
{
	return set_group(mcs_of(p, bw, nss, mode, chains), in, VHT_MCS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_set_same_dsss" */
s32 bcm4360_ratepwr_set_same_dsss(struct bcm4360_ratepwr *p, u32 bw, u32 chains, s8 v)
{
	return set_same_group(dsss_of(p, bw, chains), v, DSSS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_set_same_ofdm" */
s32 bcm4360_ratepwr_set_same_ofdm(struct bcm4360_ratepwr *p, u32 bw, u32 mode, u32 chains,
				  s8 v)
{
	return set_same_group(ofdm_of(p, bw, mode, chains), v, OFDM_LEN);
}

/* docs/re/spec/ppr.md, "ppr_set_same_ht_mcs" */
s32 bcm4360_ratepwr_set_same_ht_mcs(struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
				    u32 chains, s8 v)
{
	return set_same_group(mcs_of(p, bw, nss, mode, chains), v, HT_MCS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_set_same_vht_mcs" */
s32 bcm4360_ratepwr_set_same_vht_mcs(struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
				     u32 chains, s8 v)
{
	return set_same_group(mcs_of(p, bw, nss, mode, chains), v, VHT_MCS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_get_vht_mcs_min" */
s32 bcm4360_ratepwr_get_vht_mcs_min(const struct bcm4360_ratepwr *p, u32 bw, u32 nss,
				    u32 mode, u32 chains, s8 *min)
{
	const s8 *g;
	u32 i;

	*min = VHT_MIN_START;
	g = mcs_of(p, bw, nss, mode, chains);
	if (!g)
		return VHT_MIN_NO_GROUP;
	for (i = 0; i < VHT_MCS_LEN; i++)
		if (g[i] != RATE_DISABLED && g[i] < *min)
			*min = g[i];
	return 0;
}

/* docs/re/spec/ppr.md, "ppr_apply_max": a ceiling */
void bcm4360_ratepwr_apply_max(struct bcm4360_ratepwr *p, s8 v)
{
	u32 i;

	for (i = 0; i < data_len(p); i++)
		if (p->data[i] > v)
			p->data[i] = v;
}

/* docs/re/spec/ppr.md, "ppr_apply_min": a floor, disabled bytes included */
void bcm4360_ratepwr_apply_min(struct bcm4360_ratepwr *p, s8 v)
{
	u32 i;

	for (i = 0; i < data_len(p); i++)
		if (p->data[i] < v)
			p->data[i] = v;
}

/* docs/re/spec/ppr.md, "ppr_plus_cmn_val" */
void bcm4360_ratepwr_plus_cmn_val(struct bcm4360_ratepwr *p, s8 v)
{
	u32 i;

	for (i = 0; i < data_len(p); i++)
		if (p->data[i] != RATE_DISABLED)
			p->data[i] = (s8)(p->data[i] + v);
}

/* docs/re/spec/ppr.md, "ppr_minus_cmn_val" */
void bcm4360_ratepwr_minus_cmn_val(struct bcm4360_ratepwr *p, s8 v)
{
	u32 i;

	for (i = 0; i < data_len(p); i++) {
		s8 x = p->data[i];

		if (x != RATE_DISABLED)
			p->data[i] = x > v ? (s8)(x - v) : 0;
	}
}

/* docs/re/spec/ppr.md, "ppr_cmn_val_minus" */
void bcm4360_ratepwr_cmn_val_minus(struct bcm4360_ratepwr *p, s8 v)
{
	u32 i;

	for (i = 0; i < data_len(p); i++)
		if (p->data[i] != RATE_DISABLED)
			p->data[i] = (s8)(v - p->data[i]);
}

/* docs/re/spec/ppr.md, "ppr_multiply_percentage" */
void bcm4360_ratepwr_multiply_percentage(struct bcm4360_ratepwr *p, u8 pct)
{
	u32 i;

	for (i = 0; i < data_len(p); i++)
		if (p->data[i] != RATE_DISABLED)
			p->data[i] = (s8)((s32)p->data[i] * (s32)pct / PERCENT);
}

/* docs/re/spec/ppr.md, "ppr_compare_min" */
void bcm4360_ratepwr_compare_min(struct bcm4360_ratepwr *a, const struct bcm4360_ratepwr *b)
{
	u32 i;

	if (a->ch_bw != b->ch_bw)
		return;
	for (i = 0; i < data_len(a); i++)
		if (b->data[i] < a->data[i])
			a->data[i] = b->data[i];
}

/* docs/re/spec/ppr.md, "ppr_compare_max" */
void bcm4360_ratepwr_compare_max(struct bcm4360_ratepwr *a, const struct bcm4360_ratepwr *b)
{
	u32 i;

	if (a->ch_bw != b->ch_bw)
		return;
	for (i = 0; i < data_len(a); i++)
		if (b->data[i] > a->data[i])
			a->data[i] = b->data[i];
}

/* docs/re/spec/ppr.md, "ppr_apply_vector_ceiling" */
u32 bcm4360_ratepwr_apply_vector_ceiling(struct bcm4360_ratepwr *a,
					 const struct bcm4360_ratepwr *b)
{
	u32 i;

	if (a->ch_bw != b->ch_bw)
		return 0;
	for (i = 0; i < data_len(a); i++)
		if (b->data[i] < a->data[i])
			a->data[i] = b->data[i];
	return data_len(a);
}

/* docs/re/spec/ppr.md, "ppr_apply_vector_floor": compared as unsigned bytes */
u32 bcm4360_ratepwr_apply_vector_floor(struct bcm4360_ratepwr *a,
				       const struct bcm4360_ratepwr *b)
{
	u32 i;

	if (a->ch_bw != b->ch_bw)
		return 0;
	for (i = 0; i < data_len(a); i++)
		if ((u8)b->data[i] > (u8)a->data[i])
			a->data[i] = b->data[i];
	return data_len(a);
}

/* docs/re/spec/ppr.md, "sub_164a23" (ppr_force_disabled_group) */
static void force_disabled_group(s8 *g, s8 min, u32 n)
{
	u32 first = n < FORCE_GROUP_FIRST ? n : FORCE_GROUP_FIRST;
	u32 i;

	for (i = 0; i < first; i++) {
		if (g[i] < min) {
			hw_memset(g, RATE_DISABLED, n);
			return;
		}
	}
	for (i = FORCE_GROUP_FIRST; i < n; i++)
		if (g[i] < min)
			g[i] = RATE_DISABLED;
}

/* docs/re/spec/ppr.md, "ppr_force_disabled" */
s32 bcm4360_ratepwr_force_disabled(struct bcm4360_ratepwr *p, s8 min)
{
	u32 bw, i;

	for (bw = TX_BW_20; bw <= TX_BW_80; bw++) {
		s8 *s = bw_powers(p, bw);

		if (!s)
			continue;
		for (i = 0; i < ARRAY_SIZE(force_groups); i++)
			force_disabled_group(s + force_groups[i].off, min, force_groups[i].len);
	}
	return 0;
}

/* every byte of `n` from `b` := min(byte, v) */
static void cap_bytes(s8 *b, u32 n, s8 v)
{
	u32 i;

	for (i = 0; i < n; i++)
		if (b[i] > v)
			b[i] = v;
}

/* docs/re/spec/ppr.md, "sub_164c6a" (ppr_apply_constraint_total_tx_set) */
static u32 constraint_total_tx_set(s8 *set, s8 v)
{
	s8 v2 = (s8)(v - TOTAL_TX_2_CHAINS);
	s8 v3 = (s8)(v - TOTAL_TX_3_CHAINS);
	const s8 cap[BLK_NUM] = {
		[BLK_A] = v, [BLK_B] = v2, [BLK_C] = v3, [BLK_D] = v2, [BLK_E] = v3,
	};
	u32 i;

	for (i = 0; i < BLK_NUM; i++)
		cap_bytes(set + chain_blks[i].off, chain_blks[i].len, cap[i]);
	return SET_SIZE;
}

/* docs/re/spec/ppr.md, "ppr_apply_constraint_total_tx" */
u32 bcm4360_ratepwr_apply_constraint_total_tx(struct bcm4360_ratepwr *p, s8 v)
{
	u32 n = 0, i;

	if (p->ch_bw >= TX_BW_NUM_CH)
		return 0;
	for (i = 0; i <= p->ch_bw; i++)
		n += constraint_total_tx_set(set_at(p, set_offs[i]), v);
	return n;
}

/* docs/re/spec/ppr.md, "ppr_get_max" (-128 for a container without data) */
s8 bcm4360_ratepwr_get_max(const struct bcm4360_ratepwr *p)
{
	u32 n = data_len(p), i;
	s8 m;

	if (!n)
		return RATE_DISABLED;
	m = p->data[0];
	for (i = 1; i < n; i++)
		if (p->data[i] > m)
			m = p->data[i];
	return m;
}

/* docs/re/spec/ppr.md, "ppr_get_min" */
s8 bcm4360_ratepwr_get_min(const struct bcm4360_ratepwr *p, s8 skip)
{
	u32 n, i = 0;
	s8 m = RATE_DISABLED;

	if (p->ch_bw >= TX_BW_NUM_CH)
		return RATE_DISABLED;
	n = data_len(p);
	while (i < n && (m == skip || m == RATE_DISABLED))
		m = p->data[i++];
	for (; i < n; i++) {
		s8 x = p->data[i];

		if (x != RATE_DISABLED && x != skip && x < m)
			m = x;
	}
	return m;
}

/* docs/re/spec/ppr.md, "ppr_get_max_for_bw" */
s8 bcm4360_ratepwr_get_max_for_bw(const struct bcm4360_ratepwr *p, u32 bw)
{
	const s8 *s = bw_powers(p, bw);
	s8 m;
	u32 i;

	if (!s)
		return RATE_DISABLED;
	m = s[0];
	for (i = 1; i < SET_SIZE; i++)
		if (s[i] > m)
			m = s[i];
	return m;
}

/* docs/re/spec/ppr.md, "ppr_get_min_for_bw" (disabled bytes included) */
s8 bcm4360_ratepwr_get_min_for_bw(const struct bcm4360_ratepwr *p, u32 bw)
{
	const s8 *s = bw_powers(p, bw);
	s8 m;
	u32 i;

	if (!s)
		return RATE_DISABLED;
	m = s[0];
	for (i = 1; i < SET_SIZE; i++)
		if (s[i] < m)
			m = s[i];
	return m;
}

/* docs/re/spec/ppr.md, "Mapping": one call per byte, both pointers together */
static void map_span(bcm4360_ratepwr_mapfn fn, void *ctx, s8 *ga, s8 *gb, u32 n)
{
	u32 k;

	for (k = 0; k < n; k++)
		fn(ctx, &ga[k], &gb[k]);
}

/* docs/re/spec/ppr.md, "ppr_map_vec_dsss" */
void bcm4360_ratepwr_map_vec_dsss(bcm4360_ratepwr_mapfn fn, void *ctx,
				  struct bcm4360_ratepwr *a, struct bcm4360_ratepwr *b,
				  u32 bw, u32 chains)
{
	s8 *ga = dsss_of(a, bw, chains);
	s8 *gb = dsss_of(b, bw, chains);

	if (ga && gb)
		map_span(fn, ctx, ga, gb, DSSS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_map_vec_ofdm" */
void bcm4360_ratepwr_map_vec_ofdm(bcm4360_ratepwr_mapfn fn, void *ctx,
				  struct bcm4360_ratepwr *a, struct bcm4360_ratepwr *b,
				  u32 bw, u32 mode, u32 chains)
{
	s8 *ga = ofdm_of(a, bw, mode, chains);
	s8 *gb = ofdm_of(b, bw, mode, chains);

	if (ga && gb)
		map_span(fn, ctx, ga, gb, OFDM_LEN);
}

/* docs/re/spec/ppr.md, "ppr_map_vec_ht_mcs" */
void bcm4360_ratepwr_map_vec_ht_mcs(bcm4360_ratepwr_mapfn fn, void *ctx,
				    struct bcm4360_ratepwr *a, struct bcm4360_ratepwr *b,
				    u32 bw, u32 nss, u32 mode, u32 chains)
{
	s8 *ga = mcs_of(a, bw, nss, mode, chains);
	s8 *gb = mcs_of(b, bw, nss, mode, chains);

	if (ga && gb)
		map_span(fn, ctx, ga, gb, HT_MCS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_map_vec_vht_mcs" */
void bcm4360_ratepwr_map_vec_vht_mcs(bcm4360_ratepwr_mapfn fn, void *ctx,
				     struct bcm4360_ratepwr *a, struct bcm4360_ratepwr *b,
				     u32 bw, u32 nss, u32 mode, u32 chains)
{
	s8 *ga = mcs_of(a, bw, nss, mode, chains);
	s8 *gb = mcs_of(b, bw, nss, mode, chains);

	if (ga && gb)
		map_span(fn, ctx, ga, gb, VHT_MCS_LEN);
}

/* docs/re/spec/ppr.md, "ppr_map_vec_all": the sets of S(a, bw) and S(b, bw) */
static bool map_set(bcm4360_ratepwr_mapfn fn, void *ctx, struct bcm4360_ratepwr *a,
		    struct bcm4360_ratepwr *b, u32 bw)
{
	s8 *sa = bw_powers(a, bw);
	s8 *sb = bw_powers(b, bw);

	if (!sa || !sb)
		return false;
	map_span(fn, ctx, sa, sb, SET_SIZE);
	return true;
}

/* docs/re/spec/ppr.md, "ppr_map_vec_all" (order and repetitions kept) */
void bcm4360_ratepwr_map_vec_all(bcm4360_ratepwr_mapfn fn, void *ctx,
				 struct bcm4360_ratepwr *a, struct bcm4360_ratepwr *b)
{
	map_set(fn, ctx, a, b, TX_BW_20);
	if (map_set(fn, ctx, a, b, TX_BW_40))
		map_set(fn, ctx, a, b, TX_BW_20IN40);
	if (map_set(fn, ctx, a, b, TX_BW_80)) {
		map_set(fn, ctx, a, b, TX_BW_20IN80);
		map_set(fn, ctx, a, b, TX_BW_40IN80);
	}
}

/* docs/re/spec/ppr.md, "Serialised form": whether `flags` select a block */
static bool blk_selected(u32 blk, u32 flags)
{
	u32 c = flags & SER_FLAG_CHAINS;
	bool txbf = flags & SER_FLAG_TXBF;

	switch (blk) {
	case BLK_A:
		return true;
	case BLK_B:
		return c >= 2;
	case BLK_C:
		return c == 3;
	case BLK_D:
		return txbf;
	default:
		return txbf && c == 3;
	}
}

/* docs/re/spec/ppr.md, "Serialised form": bytes per set for `flags` */
static u32 ser_set_size(u32 flags)
{
	u32 n = 0, i;

	for (i = 0; i < BLK_NUM; i++)
		if (blk_selected(i, flags))
			n += chain_blks[i].len;
	return n;
}

/* big-endian 32-bit word */
static u32 get_be32(const u8 *b)
{
	return (u32)b[0] << 24 | (u32)b[1] << 16 | (u32)b[2] << 8 | b[3];
}

/* docs/re/spec/ppr.md, "ppr_init_ser_mem_by_bw" */
s32 bcm4360_ratepwr_init_ser_mem_by_bw(u8 *buf, u32 bw, u32 len)
{
	if (!buf)
		return BCME_BADARG;
	if (len < SER_HDR_SIZE + data_size(bw))
		return BCME_BADARG;
	hw_memcpy(buf, ser_magic, SER_MAGIC_LEN);
	buf[SER_POS_FLAGS] = 0;
	buf[SER_POS_FLAGS + 1] = 0;
	buf[SER_POS_FLAGS + 2] = 0;
	buf[SER_POS_FLAGS + 3] = SER_FLAGS_ALL;
	hw_memset(buf + SER_HDR_SIZE, SER_INIT_FILL, len - SER_HDR_SIZE);
	return 0;
}

/* docs/re/spec/ppr.md, "ppr_init_ser_mem" */
s32 bcm4360_ratepwr_init_ser_mem(u8 *buf, const struct bcm4360_ratepwr *p, u32 len)
{
	return bcm4360_ratepwr_init_ser_mem_by_bw(buf, p->ch_bw, len);
}

/* docs/re/spec/ppr.md, "sub_165780" (ppr_ser_set) */
static u32 ser_set(const s8 *set, u8 **cursor, u32 flags)
{
	u32 n = 0, i;

	for (i = 0; i < BLK_NUM; i++) {
		if (!blk_selected(i, flags))
			continue;
		hw_memcpy(*cursor, set + chain_blks[i].off, chain_blks[i].len);
		*cursor += chain_blks[i].len;
		n += chain_blks[i].len;
	}
	return n;
}

/* docs/re/spec/ppr.md, "ppr_serialize" */
s32 bcm4360_ratepwr_serialize(const struct bcm4360_ratepwr *p, u8 *buf, u32 len,
			      u32 *out_len)
{
	u32 flags = SER_FLAGS_ALL, need, n = 0, i;
	u8 bw;
	u8 *cur;

	if (len <= SER_HDR_SIZE)
		return BCME_BUFTOOSHORT;
	if (!hw_memcmp(buf, ser_magic, SER_MAGIC_LEN))
		flags = get_be32(buf + SER_POS_FLAGS);
	need = ser_set_size(flags);
	if (p->ch_bw == TX_BW_40)
		need *= 2;
	else if (p->ch_bw == TX_BW_80)
		need *= 3;
	if (len < need)
		return BCME_BUFTOOSHORT;

	bw = (u8)p->ch_bw;
	buf[SER_POS_VERSION] = SER_VERSION;
	buf[SER_POS_FLAGS] = 0;
	buf[SER_POS_FLAGS + 1] = 0;
	buf[SER_POS_FLAGS + 2] = 0;
	buf[SER_POS_FLAGS + 3] = SER_FLAGS_ALL;
	buf[SER_POS_BW] = bw;

	cur = buf + SER_HDR_SIZE;
	if (bw < TX_BW_NUM_CH)
		for (i = 0; i <= bw; i++)
			n += ser_set(set_at(p, set_offs[i]), &cur, flags);
	*out_len = SER_HDR_SIZE + n;
	return 0;
}

/* docs/re/spec/ppr.md, "sub_165642" (ppr_deser_set) */
static void deser_set(s8 *set, const u8 **cursor, u32 flags)
{
	u32 i;

	for (i = 0; i < BLK_NUM; i++) {
		if (!blk_selected(i, flags))
			continue;
		hw_memcpy(set + chain_blks[i].off, *cursor, chain_blks[i].len);
		*cursor += chain_blks[i].len;
	}
}

/* docs/re/spec/ppr.md, "sub_1656f7" (ppr_deser_cpy) */
static void deser_cpy(struct bcm4360_ratepwr *p, const u8 *data, u32 flags, u32 bw)
{
	u32 i;

	p->ch_bw = bw;
	if (bw >= TX_BW_NUM_CH)
		return;
	for (i = 0; i <= bw; i++)
		deser_set(set_at(p, set_offs[i]), &data, flags);
}

/* docs/re/spec/ppr.md, "ppr_deserialize" */
s32 bcm4360_ratepwr_deserialize(struct bcm4360_ratepwr *p, const u8 *buf, u32 len)
{
	if (len <= SER_HDR_SIZE)
		return BCME_BUFTOOSHORT;
	if (!buf)
		return BCME_BADARG;
	if (buf[SER_POS_VERSION] != SER_VERSION)
		return BCME_VERSION;
	if (buf[SER_POS_BW] != p->ch_bw)
		return BCME_BADARG;
	bcm4360_ratepwr_set_cmn_val(p, RATE_DISABLED);
	deser_cpy(p, buf + SER_HDR_SIZE, get_be32(buf + SER_POS_FLAGS), buf[SER_POS_BW]);
	return 0;
}

/* docs/re/spec/ppr.md, "ppr_deserialize_create": failure, no container */
static s32 deserialize_fail(struct bcm4360_ratepwr **out, s32 err)
{
	*out = NULL;
	return err;
}

/* docs/re/spec/ppr.md, "ppr_deserialize_create" */
s32 bcm4360_ratepwr_deserialize_create(void *osh, const u8 *buf, u32 len,
				       struct bcm4360_ratepwr **out)
{
	struct bcm4360_ratepwr *p;
	u32 bw;

	if (len <= SER_HDR_SIZE)
		return deserialize_fail(out, BCME_BUFTOOSHORT);
	if (!buf)
		return deserialize_fail(out, BCME_BADARG);
	if (buf[SER_POS_VERSION] != SER_VERSION)
		return deserialize_fail(out, BCME_VERSION);
	bw = buf[SER_POS_BW];
	if (len < SER_HDR_SIZE + data_size(bw))
		return deserialize_fail(out, BCME_BUFTOOSHORT);
	p = bcm4360_ratepwr_create(osh, bw);
	if (!p)
		return deserialize_fail(out, BCME_NOMEM);
	bcm4360_ratepwr_set_cmn_val(p, RATE_DISABLED);
	deser_cpy(p, buf + SER_HDR_SIZE, get_be32(buf + SER_POS_FLAGS), bw);
	*out = p;
	return 0;
}
