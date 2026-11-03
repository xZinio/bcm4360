// SPDX-License-Identifier: ISC
/*
 * The per-rate transmit power offsets of the transmit chains: what the
 * driver does after every computation of the PHY's transmit power targets.
 *
 * Written from docs/re/spec/cca-stf.md, "Procedures, part 2":
 * wlc_update_txppr_offset, sub_172c6c (its callback) and sub_174cd5 (the
 * spatial policy of the band). The stf block is Broadcom's and is used in
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
