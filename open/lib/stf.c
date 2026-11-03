// SPDX-License-Identifier: ISC
/*
 * The per-rate transmit power offsets of the transmit chains: what the
 * driver does after every computation of the PHY's transmit power targets.
 *
 * Written from docs/re/spec/cca-stf.md, "Procedures, part 2":
 * wlc_update_txppr_offset, sub_172c6c (its callback) and sub_174cd5 (the
 * spatial policy of the band); wlc_stf_spatial_policy_set and
 * wlc_stf_spatialpolicy_set_complete (the transmit cores per rate class,
 * with sub_172af1), wlc_stf_txcore_shmem_write (with sub_172da9),
 * wlc_stf_ss_update and wlc_stf_ss_algo_channel_get. The stf block is Broadcom's and is used in
 * place; everything else of the driver goes through the bcm4360_stf_env_*
 * services.
 *
 * The minimum transmit power (the iovar "min_txpower") is 0: the driver has
 * no such iovar, and the object reads a stack word it never wrote in its
 * place (cca-stf.md, "The uninitialised stack read") - which made the
 * ceiling and the chain mask of the real driver depend on whatever earlier
 * code left on the stack.
 */
#include <bcm4360/stf.h>
#include <bcm4360/rate.h>
#include <bcm4360/hw.h>

/* cca-stf.md, Data: "The stf state" (offsets in the stf block) */
#define STF_HW_TXCHAIN		0x00
#define STF_TXCORE_MASK(i)	(0x17 + 2 * (i))	/* txcore[i].mask */
#define STF_PPR			0x30			/* the stf ppr_t */
#define STF_POLICY_2G		0x5f	/* spatial_policy_band[0]: 2.4 GHz */
#define STF_POLICY_5G_LOW	0x60	/* 5 GHz, channel <= 51 */
#define STF_POLICY_5G_MID	0x61	/* <= 99 */
#define STF_POLICY_5G_HIGH	0x62	/* <= 148 */
#define STF_POLICY_5G_TOP	0x63	/* above */
#define STF_CHAIN_PWR_OFFSET	0x64	/* s8 per chain, 4 chains */
#define STF_RATE_SHM_OFF	0x6c	/* u16 */
#define STF_TXPWR_CEIL		0x88

#define TXCORE_CCK		0	/* rate classes of the txcore table */
#define TXCORE_OFDM		1
#define STF_CHAINS		4

/* sub_174cd5, step 2: the upper channel numbers of the 5 GHz subbands */
#define CH_5G_LOW_MAX		51
#define CH_5G_MID_MAX		99
#define CH_5G_HIGH_MAX		148

/* chanspec bits (cca-stf.md, notation of part 2) */
#define CHSPEC_BAND_MASK	0xc000	/* 0: 2.4 GHz */
#define CHSPEC_BW_MASK		0x3800
#define CHSPEC_BW_80		0x2000
#define CHSPEC_BW_40		0x1800
#define CHSPEC_CHAN_MASK	0x00ff

/* wl_tx_bw_t (ppr.md) */
#define TX_BW_20		0
#define TX_BW_40		1
#define TX_BW_80		2
#define TX_BW_20IN40		3
#define TX_BW_20IN80		4

/* ppr_get_ofdm modes (ppr.md) */
#define OFDM_MODE_NONE		0
#define OFDM_MODE_CDD		2

#define PPR_RATE_DISABLED	0x80	/* an offset byte: rate disabled */
#define TXPWR_CEIL_MAX		31

/* step 10: the microcode's rate table is not touched on older cores */
#define COREREV_RATE_TABLE_MIN	22
/* step 16: from this core revision the offset sits in bits 3..8 of the word */
#define COREREV_OFFSET_FIELD	40
#define OFFSET_FIELD_SHIFT	3
#define OFFSET_FIELD_BITS	0x3f
#define OFFSET_WORD_KEEP	0xfe07

/* step 14: one entry per rate: u8 rate, u8, u16 shm offset, u16 word */
#define ENTRY_SIZE		6
#define ENTRY_WORD		4

#define DSSS_RATES		4
#define OFDM_RATES		8

/* step 11: the rate set cck_ofdm_rates (cca-stf.md, "Tables of wlc_stf.c") */
static const struct bcm4360_rateset cck_ofdm_rates = {
	.count = 12,
	.rates = { 0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c },
	.vht_mcsmap = 0xffff,
};

/* bmac-run.md R6, rate_info: bit 7 marks the OFDM rates */
static bool rate_is_ofdm(u8 rate)
{
	switch (rate) {
	case 12: case 18: case 24: case 36: case 48: case 72: case 96: case 108:
		return true;
	default:
		return false;
	}
}

static u32 bitcount8(u8 v)
{
	u32 n = 0;

	for (; v; v &= v - 1)
		n++;
	return n;
}

/* cca-stf.md, part 2, notation: bw(cs) */
static u32 chanspec_tx_bw(u16 cs)
{
	switch (cs & CHSPEC_BW_MASK) {
	case CHSPEC_BW_80:
		return TX_BW_80;
	case CHSPEC_BW_40:
		return TX_BW_40;
	default:
		return TX_BW_20;
	}
}

static void *stf_ppr(const u8 *stf)
{
	void *p;

	hw_memcpy(&p, stf + STF_PPR, sizeof(p));
	return p;
}

static void stf_set_ppr(u8 *stf, void *p)
{
	hw_memcpy(stf + STF_PPR, &p, sizeof(p));
}

static u16 get_u16(const u8 *p)
{
	return (u16)(p[0] | (p[1] << 8));
}

static void put_u16(u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

/* cca-stf.md, "sub_172c6c (wlc_stf_txppr_offset_mapfn)" */
void bcm4360_stf_txppr_mapfn(void *ctx, s8 *a, s8 *b)
{
	u8 ceil = *(const u8 *)ctx;
	u8 v = (u8)*a;

	if (v == PPR_RATE_DISABLED) {
		*b = (s8)PPR_RATE_DISABLED;
		return;
	}
	v >>= 1;
	*b = (s8)(v < ceil ? v : ceil);
}

/* cca-stf.md, "sub_174cd5 (wlc_stf_spatial_mode_upd)" */
static void stf_spatial_mode_upd(void *wlc, const u8 *stf, u16 cs)
{
	u32 at;
	u8 ch;

	if (!(cs & CHSPEC_BAND_MASK)) {
		at = STF_POLICY_2G;
	} else {
		/* the band from the argument, the channel from the current chanspec */
		ch = bcm4360_stf_env_chanspec(wlc) & CHSPEC_CHAN_MASK;
		if (ch <= CH_5G_LOW_MAX)
			at = STF_POLICY_5G_LOW;
		else if (ch <= CH_5G_MID_MAX)
			at = STF_POLICY_5G_MID;
		else if (ch <= CH_5G_HIGH_MAX)
			at = STF_POLICY_5G_HIGH;
		else
			at = STF_POLICY_5G_TOP;
	}
	bcm4360_stf_env_spatial_policy_set(wlc, (s8)stf[at]);
}

/* wlc_update_txppr_offset, steps 11 to 17: the legacy rates' offsets into
 * the microcode's rate table */
static void stf_rate_table_upd(void *wlc, u8 *stf, void *ppr, u16 cs)
{
	struct bcm4360_rateset rs;
	s8 dsss[DSSS_RATES], ofdm[OFDM_RATES];
	u32 k, n, bw = chanspec_tx_bw(cs), size, di = 0, oi = 0;
	void *osh = bcm4360_stf_env_osh(wlc);
	u16 off = get_u16(stf + STF_RATE_SHM_OFF);
	u8 *buf;

	/* step 11 */
	hw_memcpy(&rs, &cck_ofdm_rates, sizeof(rs));
	/* step 12: the DSSS rates use the 20 MHz set within the channel */
	hw_memset(dsss, 0, sizeof(dsss));
	n = bitcount8(stf[STF_TXCORE_MASK(TXCORE_CCK)]);
	if (n >= 1 && n <= 3)
		bcm4360_stf_env_ppr_get_dsss(ppr, bw == TX_BW_80 ? TX_BW_20IN80 :
					     bw == TX_BW_40 ? TX_BW_20IN40 : TX_BW_20, n, dsss);
	/* step 13 */
	hw_memset(ofdm, 0, sizeof(ofdm));
	n = bitcount8(stf[STF_TXCORE_MASK(TXCORE_OFDM)]);
	if (n == 1)
		bcm4360_stf_env_ppr_get_ofdm(ppr, bw, OFDM_MODE_NONE, 1, ofdm);
	else if (n == 2 || n == 3)
		bcm4360_stf_env_ppr_get_ofdm(ppr, bw, OFDM_MODE_CDD, n, ofdm);
	/* step 14 */
	size = ENTRY_SIZE * rs.count;
	buf = osl_malloc(osh, size);
	if (!buf)
		return;
	hw_memset(buf, 0, size);
	/* step 15 */
	bcm4360_stf_env_get_rateset_shm_offset(wlc, &rs, off, buf);
	/* step 16 */
	for (k = 0; k < rs.count; k++) {
		u8 *word = buf + ENTRY_SIZE * k + ENTRY_WORD;
		s8 p = rate_is_ofdm(rs.rates[k] & 0x7f) ? ofdm[oi++] : dsss[di++];

		if ((u8)p == PPR_RATE_DISABLED)
			p = (s8)stf[STF_TXPWR_CEIL];
		if (bcm4360_stf_env_corerev(wlc) >= COREREV_OFFSET_FIELD)
			put_u16(word, (u16)(((p & OFFSET_FIELD_BITS) << OFFSET_FIELD_SHIFT) |
					    (get_u16(word) & OFFSET_WORD_KEEP)));
		else
			put_u16(word, (u16)(s16)p);
	}
	/* step 17 */
	bcm4360_stf_env_set_rateset_shm_offset(wlc, &rs, off, buf);
	osl_mfree(osh, buf, size);
}

/* cca-stf.md, "wlc_update_txppr_offset" */
void bcm4360_stf_update_txppr_offset(void *wlc, void *ofs)
{
	u8 *stf = bcm4360_stf_env_state(wlc);
	void *osh = bcm4360_stf_env_osh(wlc);
	u16 cs = bcm4360_stf_env_chanspec(wlc);
	u32 bw = chanspec_tx_bw(cs), i;
	s32 min4 = 0, c;	/* step 1: min_txpower is 0 */
	void *ppr;
	s8 tmax, tmin;
	u8 ceil, mask;

	/* steps 2, 3 */
	tmax = bcm4360_stf_env_target_max(wlc);
	bcm4360_stf_env_ol_curpwr_upd(wlc, tmax, cs);
	/* step 4: a container of the channel's bandwidth */
	ppr = stf_ppr(stf);
	if (ppr && bcm4360_stf_env_ppr_get_ch_bw(ppr) != bw) {
		bcm4360_stf_env_ppr_delete(osh, ppr);
		ppr = NULL;
		stf_set_ppr(stf, NULL);
	}
	/* step 5 */
	if (!ppr) {
		ppr = bcm4360_stf_env_ppr_create(osh, bw);
		stf_set_ppr(stf, ppr);
		if (!ppr)
			return;
	}
	/* step 6: the ceiling, then the PHY's offsets mapped into the container */
	c = (s8)((u8)tmax - (u8)min4);
	c >>= 1;
	if (c < 0)
		c = 0;
	if (c > TXPWR_CEIL_MAX)
		c = TXPWR_CEIL_MAX;
	ceil = (u8)c;
	stf[STF_TXPWR_CEIL] = ceil;
	bcm4360_stf_env_ppr_map_vec_all(bcm4360_stf_txppr_mapfn, &ceil, ofs, ppr);
	/* step 7: the chains that cannot reach the minimum */
	tmin = bcm4360_stf_env_target_min(wlc);
	mask = stf[STF_HW_TXCHAIN];
	for (i = 0; i < STF_CHAINS; i++)
		if ((s32)tmin + (s8)stf[STF_CHAIN_PWR_OFFSET + i] < min4)
			mask &= (u8)~(1U << i);
	/* step 8 */
	bcm4360_stf_env_txchain_set(wlc, mask);
	/* step 9 */
	stf_spatial_mode_upd(wlc, stf, cs);
	/* step 10 */
	ppr = stf_ppr(stf);
	if (!ppr || !bcm4360_stf_env_clk(wlc) ||
	    bcm4360_stf_env_corerev(wlc) < COREREV_RATE_TABLE_MIN)
		return;
	stf_rate_table_upd(wlc, stf, ppr, cs);
}

/* ---- the spatial policy: transmit cores per rate class ------------------- */

/* cca-stf.md, Data: "The stf state" */
#define STF_TXCHAIN		0x01
#define STF_TXSTREAMS		0x02	/* T */
#define STF_PHYTXANT		0x0a	/* u16 */
#define STF_SS_OPMODE		0x0c
#define STF_SS_ALGOSEL_AUTO	0x0d
#define STF_SS_ALGO_CHANNEL	0x0e	/* u16 */
#define STF_NO_CDDSTBC		0x10
#define STF_TXCORE_NSTS(i)	(0x16 + 2 * (i))
#define STF_TXCORE_OVERRIDE(i)	(0x22 + (i))
#define STF_SPATIAL_POLICY	0x28
#define STF_POLICY_PENDING	0x29
#define STF_SHMEM_BASE		0x2a	/* u16 */
#define STF_SS_0X44		0x44	/* purpose unknown (wlc_stf_ss_update step 5) */
#define STF_CORE1_FIRST		0x8a

#define STF_CLASSES		6	/* CCK, OFDM, 1, 2, 3, 4 streams */
#define STF_SHMEM_CLASSES	5	/* class 5 is not written */
#define TXCORE_1SS		2
#define TXCORE_2SS		3

#define POLICY_STREAMS		0	/* the first column of the txcore table */
#define POLICY_AUTO		0xff	/* -1: by the power comparison */

#define PHY_TYPE_AC		0x0b
#define PHY_TYPE_HT		7
#define BANDTYPE_5G		1
#define BANDTYPE_2G		2

/* wlc_stf_spatialpolicy_set_complete, step 4.2: boardflags2 bit 12 */
#define BFL2_HI_CCK_CORE0	0x10
/* wlc_stf_ss_update, step 5: boardflags2 bit 14 */
#define BFL2_HI_SS_MHF		0x40
#define PHY_CAP_SS_AUTO		0x02	/* wlc_phy_cap_get bit 1 */
#define MHF4			3	/* host flags word index */
#define MHF4_SS_FLAG		0x40

/* wlc_stf_txcore_shmem_write */
#define COREREV_TXCORE_SHMEM_MIN	26
#define SHM_COEX_WORD		0x5de
#define VENDOR_APPLE		0x106b
#define CHIP_4360		0x4360
#define CHIP_4331		0x4331

#define BCME_UNSUPPORTED	(-23)

#define SS_ALGO_UNKNOWN		0xffff
#define SS_ALGO_SISO		0x01
#define SS_ALGO_CDD		0x02
#define SS_ALGO_STBC		0x04

/* the array gains of two and three chains in the 0.5 dB units of the container */
#define GAIN_2CHAINS		6
#define GAIN_3CHAINS		10
#define GAIN_2SS_3CHAINS	4

#define HT_MCS_RATES		8
#define VHT_MCS_RATES		10

/* N: the stream counts of the classes (txcore table, first column) */
static const u8 class_nsts[STF_CLASSES] = { 1, 1, 1, 2, 3, 4 };

static bool has(u8 mask, u8 bits)
{
	return (mask & bits) == bits;
}

/* cca-stf.md, "sub_172af1 (wlc_stf_txcore_default)" */
static u8 stf_txcore_default(const u8 *stf, u32 nsts)
{
	u8 txchain = stf[STF_TXCHAIN];

	if (nsts > stf[STF_TXSTREAMS])
		return 0;
	if (nsts == 1) {
		if (!stf[STF_CORE1_FIRST])
			return has(txchain, 1) ? 1 : has(txchain, 2) ? 2 : 4;
		return has(txchain, 2) ? 2 : has(txchain, 4) ? 4 : 1;
	}
	if (nsts == 2)
		return has(txchain, 5) ? 5 : has(txchain, 3) ? 3 : 6;
	return 7;
}

/* cca-stf.md, "sub_172da9 (wlc_stf_spatial_expansion_idx)" */
static u8 stf_spatial_expansion_idx(const u8 *stf, u32 i)
{
	u32 b = bitcount8(stf[STF_TXCORE_MASK(i)]);
	u32 s = stf[STF_TXCORE_NSTS(i)];

	if (s > stf[STF_TXSTREAMS] || b == 0 || b == s)
		return 0;
	if (b == 2 && s == 1)
		return 1;
	if (b == 3 && s == 1)
		return 2;
	if (b == 3 && s == 2)
		return 3;
	return 0;
}

/* set_complete, step 3: a byte of a group after fix() */
static u8 stf_fix(const u8 *stf, s8 x)
{
	return (u8)x == PPR_RATE_DISABLED ? stf[STF_TXPWR_CEIL] : (u8)x;
}

/* set_complete, step 3: the largest byte of a group (unsigned, after fix()) */
static s32 stf_umax(const u8 *stf, const s8 *g, u32 n)
{
	u32 i;
	u8 m = 0;

	for (i = 0; i < n; i++) {
		u8 v = stf_fix(stf, g[i]);

		if (v > m)
			m = v;
	}
	return m;
}

/* set_complete, step 3, one class: 1, 2 or 3 streams by the comparison of the
 * offsets of 1, 2 and 3 chains (g3 only when T is not 2) */
static u8 stf_auto_class(const u8 *stf, const s8 *g1, const s8 *g2, const s8 *g3, u32 n,
			 u8 streams)
{
	s32 a = -stf_umax(stf, g1, n), b = GAIN_2CHAINS - stf_umax(stf, g2, n), c;

	if (b > a)
		streams = 2;
	if (stf[STF_TXSTREAMS] != 2) {
		c = GAIN_3CHAINS - stf_umax(stf, g3, n);
		if (c > a && c > b)
			streams = 3;
	}
	return streams;
}

/* set_complete, step 3: the stream count of each class by the power comparison */
static void stf_auto_streams(void *wlc, const u8 *stf, u8 n[STF_CLASSES])
{
	s8 g1[VHT_MCS_RATES], g2[VHT_MCS_RATES], g3[VHT_MCS_RATES];
	void *p = stf_ppr(stf);
	u32 i, w, dw, t = stf[STF_TXSTREAMS];

	for (i = 0; i < STF_CLASSES; i++)
		n[i] = class_nsts[i];
	if (!p || t < 2)
		return;
	w = chanspec_tx_bw((u16)bcm4360_stf_env_chanspec(wlc));
	dw = w == TX_BW_80 ? TX_BW_20IN80 : w == TX_BW_40 ? TX_BW_20IN40 : TX_BW_20;
	hw_memset(g3, 0, sizeof(g3));

	/* step 3.1: CCK */
	bcm4360_stf_env_ppr_get_dsss(p, dw, 1, g1);
	bcm4360_stf_env_ppr_get_dsss(p, dw, 2, g2);
	if (t != 2)
		bcm4360_stf_env_ppr_get_dsss(p, dw, 3, g3);
	n[TXCORE_CCK] = stf_auto_class(stf, g1, g2, g3, DSSS_RATES, n[TXCORE_CCK]);
	/* step 3.2: OFDM */
	bcm4360_stf_env_ppr_get_ofdm(p, w, OFDM_MODE_NONE, 1, g1);
	bcm4360_stf_env_ppr_get_ofdm(p, w, OFDM_MODE_CDD, 2, g2);
	if (t != 2)
		bcm4360_stf_env_ppr_get_ofdm(p, w, OFDM_MODE_CDD, 3, g3);
	n[TXCORE_OFDM] = stf_auto_class(stf, g1, g2, g3, OFDM_RATES, n[TXCORE_OFDM]);
	/* step 3.3: one stream */
	bcm4360_stf_env_ppr_get_vht_mcs(p, w, 1, OFDM_MODE_NONE, 1, g1);
	bcm4360_stf_env_ppr_get_vht_mcs(p, w, 1, OFDM_MODE_CDD, 2, g2);
	if (t != 2)
		bcm4360_stf_env_ppr_get_vht_mcs(p, w, 1, OFDM_MODE_CDD, 3, g3);
	n[TXCORE_1SS] = stf_auto_class(stf, g1, g2, g3, VHT_MCS_RATES, n[TXCORE_1SS]);
	/* step 3.4: two streams (SDM), only with more than two chains */
	if (t != 2) {
		bcm4360_stf_env_ppr_get_vht_mcs(p, w, 2, OFDM_MODE_NONE, 3, g3);
		bcm4360_stf_env_ppr_get_vht_mcs(p, w, 2, OFDM_MODE_NONE, 2, g2);
		if (GAIN_2SS_3CHAINS - stf_umax(stf, g3, VHT_MCS_RATES) >
		    -stf_umax(stf, g2, VHT_MCS_RATES))
			n[TXCORE_2SS] = 3;
	}
}

/* cca-stf.md, "wlc_stf_txcore_shmem_write" */
void bcm4360_stf_txcore_shmem_write(void *wlc, bool force)
{
	u8 *stf = bcm4360_stf_env_state(wlc);
	u16 phytype = bcm4360_stf_env_phytype(wlc), base;
	u32 i, chip;
	u16 v;

	/* steps 1 to 3 */
	if (!force && !bcm4360_stf_env_clk(wlc))
		return;
	if (bcm4360_stf_env_corerev(wlc) < COREREV_TXCORE_SHMEM_MIN ||
	    (phytype != PHY_TYPE_AC && phytype != PHY_TYPE_HT))
		return;
	base = get_u16(stf + STF_SHMEM_BASE);
	if (base != bcm4360_stf_env_shm_core_base(wlc, 0) &&
	    base != bcm4360_stf_env_shm_core_base(wlc, 1))
		return;
	/* step 4 */
	for (i = 0; i < STF_SHMEM_CLASSES; i++) {
		v = stf[STF_TXCORE_MASK(i)] & 0x0f;
		if (phytype == PHY_TYPE_HT)
			v |= (u16)(stf_spatial_expansion_idx(stf, i) << 8);
		bcm4360_stf_env_write_shm(wlc, 2 * base + 2 * i, v);
	}
	/* step 5 */
	chip = bcm4360_stf_env_chip(wlc);
	if (bcm4360_stf_env_board_vendor(wlc) == VENDOR_APPLE &&
	    (chip == CHIP_4360 || chip == CHIP_4331))
		bcm4360_stf_env_write_shm(wlc, SHM_COEX_WORD, bcm4360_stf_env_coex_word(wlc));
}

/* set_complete, step 4.7: the class of the band's basic rates sets the
 * transmit antenna word, and the beacon's if one is sent */
static void stf_basic_class_antenna(void *wlc, u8 *stf, u8 m)
{
	u32 bandtype = bcm4360_stf_env_bandtype(wlc), rate;

	put_u16(stf + STF_PHYTXANT, (u16)(m << 6));
	bcm4360_stf_env_txant_set(wlc, get_u16(stf + STF_PHYTXANT));
	rate = bcm4360_stf_env_beacon_rate(wlc);
	if (bcm4360_stf_env_clk(wlc) && bcm4360_stf_env_valid_rate(wlc, rate, bandtype)) {
		bcm4360_stf_env_suspend_mac_and_wait(wlc);
		bcm4360_stf_env_beacon_phytxctl_txant_upd(wlc, rate);
		bcm4360_stf_env_beacon_phytxctl(wlc, rate, bcm4360_stf_env_chanspec(wlc));
		bcm4360_stf_env_enable_mac(wlc);
	}
}

static bool stf_is_basic_class(void *wlc, u32 i)
{
	u32 bandtype = bcm4360_stf_env_bandtype(wlc);

	return (i == TXCORE_OFDM && bandtype == BANDTYPE_5G) ||
	       (i == TXCORE_CCK && bandtype == BANDTYPE_2G);
}

/* set_complete, step 4: the core mask of class i (d = its default) */
static void stf_class_mask(void *wlc, u8 *stf, u32 i, u8 d)
{
	u8 txchain = stf[STF_TXCHAIN], t = stf[STF_TXSTREAMS];
	u8 m;

	/* steps 4.1 to 4.5 */
	m = stf[STF_TXCORE_OVERRIDE(i)];
	if (!m)
		m = d;
	if (i == TXCORE_CCK && (bcm4360_stf_env_boardflags2_hi(wlc) & BFL2_HI_CCK_CORE0) &&
	    bcm4360_stf_env_phytype(wlc) == PHY_TYPE_AC)
		m = 1;
	if (stf[STF_TXCORE_NSTS(i)] > t)
		m = 0;
	if (bitcount8(m) > t)
		m = 0;
	if (bitcount8(m) == t && ((m & ~txchain) || !(m & txchain)))
		m = txchain;
	/* step 4.6 */
	if (i <= TXCORE_OFDM && !m) {
		if (txchain) {
			stf[STF_TXCORE_MASK(i)] = txchain;
			m = txchain;
		}
	} else {
		stf[STF_TXCORE_MASK(i)] = m;
	}
	/* step 4.7: the class of the band's basic rates sets the antenna word */
	if (stf_is_basic_class(wlc, i))
		stf_basic_class_antenna(wlc, stf, m);
	/* step 4.8 */
	bcm4360_stf_env_txc_gen_inc(wlc);
}

/* cca-stf.md, "wlc_stf_spatialpolicy_set_complete" */
void bcm4360_stf_spatialpolicy_set_complete(void *wlc)
{
	u8 *stf = bcm4360_stf_env_state(wlc);
	u8 def[STF_CLASSES], n[STF_CLASSES], pol;
	u32 i;

	/* step 1 */
	stf[STF_SPATIAL_POLICY] = stf[STF_POLICY_PENDING];
	stf[STF_POLICY_PENDING] = 0;
	pol = stf[STF_SPATIAL_POLICY];
	/* steps 2 and 3 */
	if (pol == POLICY_AUTO)
		stf_auto_streams(wlc, stf, n);
	for (i = 0; i < STF_CLASSES; i++) {
		if (pol == POLICY_STREAMS)
			def[i] = stf_txcore_default(stf, class_nsts[i]);
		else if (pol == POLICY_AUTO)
			def[i] = stf_txcore_default(stf, n[i]);
		else
			def[i] = stf[STF_TXCHAIN];
	}
	/* step 4 */
	for (i = 0; i < STF_CLASSES; i++)
		stf_class_mask(wlc, stf, i, def[i]);
	/* step 5 */
	bcm4360_stf_txcore_shmem_write(wlc, false);
	bcm4360_stf_env_txc_upd(wlc);
	bcm4360_stf_env_txc_gen_inc(wlc);
}

/* cca-stf.md, "wlc_stf_spatial_policy_set" */
s32 bcm4360_stf_spatial_policy_set(void *wlc, s32 policy)
{
	u8 *stf = bcm4360_stf_env_state(wlc);
	u16 phytype = bcm4360_stf_env_phytype(wlc);

	/* step 1 */
	if (phytype != PHY_TYPE_AC && phytype != PHY_TYPE_HT)
		return BCME_UNSUPPORTED;
	/* steps 2 to 4 */
	stf[STF_POLICY_PENDING] = (u8)policy;
	if (stf[STF_POLICY_PENDING] == stf[STF_SPATIAL_POLICY] ||
	    bcm4360_stf_env_txpending_sum(wlc) == 0)
		bcm4360_stf_spatialpolicy_set_complete(wlc);
	else
		bcm4360_stf_env_policy_defer(wlc);
	return 0;
}

/* cca-stf.md, "wlc_stf_ss_update" */
s32 bcm4360_stf_ss_update(void *wlc, void *bs)
{
	u8 *stf = bcm4360_stf_env_state(wlc);
	u8 old = stf[STF_SS_OPMODE], new, t = stf[STF_TXSTREAMS];
	bool no_ss = stf[STF_NO_CDDSTBC] || t == 1;

	/* steps 1 to 3 */
	if ((bcm4360_stf_env_phy_cap(wlc) & PHY_CAP_SS_AUTO) && stf[STF_SS_ALGOSEL_AUTO] &&
	    get_u16(stf + STF_SS_ALGO_CHANNEL) != SS_ALGO_UNKNOWN) {
		new = no_ss ? 0 : (u8)((get_u16(stf + STF_SS_ALGO_CHANNEL) & 1) ^ 1);
	} else {
		if (bs != bcm4360_stf_env_band(wlc))
			return 0;
		new = no_ss ? 0 : bcm4360_stf_env_band_ss_mode(bs);
	}
	/* step 4 */
	if (new != old) {
		stf[STF_SS_OPMODE] = new;
		bcm4360_stf_env_band_stf_ss_set(wlc, new);
	}
	/* step 5 */
	bcm4360_stf_env_mhf(wlc, MHF4, MHF4_SS_FLAG,
			    (!stf[STF_SS_0X44] && (bcm4360_stf_env_boardflags2_hi(wlc) & BFL2_HI_SS_MHF))
			    ? 0 : MHF4_SS_FLAG, 0);
	return 0;
}

/* cca-stf.md, "wlc_stf_ss_algo_channel_get" */
void bcm4360_stf_ss_algo_channel_get(void *wlc, u16 *out, u32 chanspec)
{
	u8 *stf = bcm4360_stf_env_state(wlc);
	s8 g[HT_MCS_RATES];
	u32 w, t = stf[STF_TXSTREAMS];
	s32 a, b = 0, c = 0;
	void *p;
	u16 v;

	/* steps 1, 2 */
	*out = 0;
	if (!bcm4360_stf_env_up(wlc)) {
		*out = SS_ALGO_UNKNOWN;
		return;
	}
	/* steps 3, 4 */
	w = chanspec_tx_bw((u16)chanspec);
	p = stf_ppr(stf);
	bcm4360_stf_env_ppr_get_ht_mcs(p, w, 1, OFDM_MODE_NONE, 1, g);
	a = (s8)stf_fix(stf, g[0]);
	if (t > 1) {
		bcm4360_stf_env_ppr_get_ht_mcs(p, w, 1, OFDM_MODE_CDD, 2, g);
		b = (s8)stf_fix(stf, g[0]);
		bcm4360_stf_env_ppr_get_ht_mcs(p, w, 2, 1, 2, g);
		c = (s8)stf_fix(stf, g[0]);
	}
	/* step 5 */
	v = *out;
	if (t == 1 || a + GAIN_2CHAINS < b)
		v |= SS_ALGO_SISO;
	else
		v |= SS_ALGO_CDD;
	if (t > 1 && a + GAIN_2CHAINS >= c)
		v |= SS_ALGO_STBC;
	*out = v;
}
