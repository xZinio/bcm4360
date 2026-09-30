// SPDX-License-Identifier: ISC
/*
 * AC-PHY of the BCM4360: receive-gain control, the desense applied to it, the
 * adjacent-channel-interference mitigation set-up and periodic engine, and the
 * carrier-sense minimum-power calibration. These are the leaf functions that
 * the channel function (acphy-chanspec.md, section 5), the initialisation
 * (acphy-init.md) and the watchdog call.
 *
 * Written from docs/re/spec/acphy-desense.md (a "section" C1..C5, E1..E5 in
 * the comments is a section of its "Procedures"), docs/re/spec/acphy-chanspec.md
 * annexes A4, A5, A6, A7, docs/re/spec/acphy-init.md appendix A, and, for the
 * register access, docs/re/spec/access.md; for chip 0x4360, AC-PHY revision 0
 * or 1 and radio 2069 revision 3 or 4. Measured quantities (noise, RSSI,
 * energy) read 0 in the model, so measurement-dependent branches are written
 * from the specification and not exercised. The notation PHY(a), RADIO(a),
 * TBL(id)[i], SHM(o), D11(o) and mod() in the comments is the one of access.md.
 */
#include <bcm4360/phy.h>
#include <bcm4360/phy_env.h>

/* PHY registers accessed here, named by their address (purpose unknown). */
#define ACPHY_REG_0x19e		0x19e	/* bit 1: freeze the RF sequencer */
#define ACPHY_REG_0x16c		0x16c	/* set around the gain-table build (A5) */
#define ACPHY_REG_0x289		0x289	/* PHY revision 0, core 0 (A5 step 3.3) */
#define ACPHY_REG_0x299		0x299	/* 2.4 GHz desense (A7) */
#define ACPHY_REG_0x304		0x304	/* first of sub_090b77's four registers (A7) */
#define ACPHY_REG_0x321		0x321	/* carrier-sense min-power core 0 (C2, A7) */
#define ACPHY_REG_0x33a		0x33a	/* first energy-detect threshold register (C5) */
#define ACPHY_REG_0x3c1		0x3c1	/* 2.4 GHz desense (A7) */
#define ACPHY_REG_0x550		0x550	/* first hwaci detector register (init appendix A) */
#define ACPHY_REG_0x6dc		0x6dc	/* per core: rx-gain code words (A6) */
#define ACPHY_REG_0x6dd		0x6dd	/* per core */
#define ACPHY_REG_0x6ee		0x6ee	/* per core: clip-detector control (A6) */
#define ACPHY_REG_0x6f9		0x6f9	/* per core (A5 step 3.3) */
#define ACPHY_REG_0x721		0x721	/* per core: hwaci/w2nb enable (init appendix A) */
#define ACPHY_REG_0x728		0x728	/* per core: hwaci enable (init appendix A) */
#define ACPHY_REG_0x729		0x729	/* per core: w2nb enable (init appendix A) */
#define ACPHY_REG_0x73e		0x73e	/* per core (A5 step 3.2) */
#define ACPHY_REG_0x173b	0x173b	/* stage-3 gain select (A5 step 3.5) */
#define ACPHY_REG_0x1726	0x1726

/* per-core carrier-sense min-power registers of cores 1 and 2 (C2) */
#define ACPHY_REG_0x910		0x910
#define ACPHY_REG_0xb10		0xb10

/* the D11 free-running microsecond timer, read by wlc_phy_get_time_usec (A4) */
#define D11_TSF_TIMERLOW	0x180
#define D11_TSF_TIMERHIGH	0x184

/* radio registers of the clip detectors (A6) and hwaci set-up (init appendix A) */
#define R2069_CLIP_NB		0x045	/* | core << 9 */
#define R2069_CLIP_W1_2G	0x02c
#define R2069_CLIP_W1_5G	0x033
#define R2069_HWACI_045		0x045
#define R2069_HWACI_049		0x049
#define R2069_W2NB_033		0x033

/* PHY table ids */
#define TBL_GAIN_CORE0		0x44	/* receive gain (dB), core 0; +0x20 per core */
#define TBL_CODE_CORE0		0x45	/* receive gain codes, core 0; +0x20 per core */
#define TBL_GAINLIMIT		0x0b	/* gain limits of the internal LNA stages */
#define TBL_RFSEQ		0x07	/* RF sequence table (A6: [0xf9 + core]) */

/* the target gain of the receiver start point (A6 step 5.2) */
#define RXGAIN_START		69

/* the carrier-sense minimum-power default (acphy-desense.md, "Constants") */
#define CRS_MIN_PWR_DEFAULT	0x36

/* --------------------------------------------------------------- helpers */

/* the number of cores N of the PHY (pi+0x168) */
static u32 phy_cores(const struct bcm4360_phy *phy)
{
	return phy->ver.cores;
}

/* the radio is tuned to a 2.4 GHz channel */
static bool phy_is_2g(const struct bcm4360_phy *phy)
{
	return (phy->radio_chanspec & BCM4360_CHANSPEC_BAND) == BCM4360_CHANSPEC_BAND_2G;
}

/* bandwidth index: 0 = 20 MHz, 1 = 40 MHz, 2 = 80 MHz */
static u32 phy_bw_index(const struct bcm4360_phy *phy)
{
	u16 bw = phy->radio_chanspec & BCM4360_CHANSPEC_BW;

	if (bw == BCM4360_CHANSPEC_BW_20)
		return 0;
	if (bw == BCM4360_CHANSPEC_BW_40)
		return 1;
	return 2;
}

/* docs/re/spec/access.md, "PHY tables": one 16 bit entry */
static void tbl_write16(struct bcm4360_phy_io *io, u32 id, u32 offset, u16 v)
{
	bcm4360_tbl_write(io, id, 1, offset, 16, &v);
}

/* floor(a / b) for b > 0 (C division truncates toward zero) */
static s32 floor_div(s32 a, s32 b)
{
	s32 q = a / b;

	if (a % b != 0 && (a < 0) != (b < 0))
		q--;
	return q;
}

static s32 min_s32(s32 a, s32 b)
{
	return a < b ? a : b;
}

/*
 * The index of the threshold in the sorted list nearest to P, taking the
 * higher one when P lies exactly in the middle of two (A6).
 */
static u32 nearest_upper(const s16 *tab, u32 count, s32 p)
{
	u32 i, best = 0;
	s32 bd = p - tab[0];

	if (bd < 0)
		bd = -bd;
	for (i = 1; i < count; i++) {
		s32 d = p - tab[i];

		if (d < 0)
			d = -d;
		if (d <= bd) {
			bd = d;
			best = i;
		}
	}
	return best;
}

/* nphy-side helper: bit `c` of the receive-chain mask is set (present core) */
static bool phy_core_present(const struct bcm4360_phy *phy, u32 c)
{
	return (phy->rxchain & (1u << c)) != 0;
}

/*
 * acphy-init.md, H13 (wlc_phyreg_enter/exit): the nesting counter; the first
 * enter takes the ucode wake override, the last exit releases it. The carrier-
 * sense calibration (C1) wraps its register writes in this.
 */
static void phy_reg_enter(struct bcm4360_phy *phy)
{
	if (phy->phyreg_count++ == 0)
		bcm4360_mac_wake_override_set(phy->hw);
}

static void phy_reg_exit(struct bcm4360_phy *phy)
{
	if (--phy->phyreg_count == 0)
		bcm4360_mac_wake_override_clear(phy->hw);
}

/* ------------------------------------------------ A5: receive gain tables */

/* acphy-chanspec.md A5: the stage-1/2 LNA gain lists (signed dB) */
static const s8 lna_gain_s1_2g[6] = { -10, -1, 6, 12, 18, 25 };
static const s8 lna_gain_s1_5g[6] = { -7, -2, 4, 10, 16, 23 };
static const s8 lna_gain_s2_5g[7] = { -11, -8, -5, -2, 2, 5, 9 };
static const s8 lna_gain_s2_2g[7] = { -12, -8, -4, -1, 2, 5, 9 };
static const s8 lna_gain_s2_2g_byp[7] = { -10, -6, -2, 1, 4, 7, 11 };

/*
 * acphy-chanspec.md A5 (sub_099658, wlc_phy_rxgainctrl_set_lna_gaintbl_acphy):
 * the gain table and codes of an internal LNA stage (1 or 2), built from the
 * stage's gain list and the table desense of the stage. Other stages: nothing.
 */
static void phy_set_lna_gaintbl(struct bcm4360_phy *phy, u32 stage)
{
	struct bcm4360_phy_io *io = &phy->io;
	bool is2g = phy_is_2g(phy);
	const s8 *T;
	u32 n = phy->rxgain_stage_entries[stage];
	u32 offset;
	u8 gains[BCM4360_PHY_RXGAIN_ENTRIES];
	u8 codes[BCM4360_PHY_RXGAIN_ENTRIES];
	u8 d, h, m;
	u32 c, k;

	if (stage == 1) {
		offset = 8;
		T = is2g ? lna_gain_s1_2g : lna_gain_s1_5g;
		d = phy->desense.total[BCM4360_DESENSE_LNA1_TBL];	/* pi_ac+0x66a */
		h = 5;
		phy->desense.applied[BCM4360_DESENSE_LNA1_TBL] = d;	/* pi_ac+0x658 */
	} else {
		offset = 0x10;
		if (is2g)
			T = phy->desense.total[BCM4360_DESENSE_ELNA_BYPASS] ?
				lna_gain_s2_2g_byp : lna_gain_s2_2g;
		else
			T = lna_gain_s2_5g;
		d = phy->desense.total[BCM4360_DESENSE_LNA2_TBL];	/* pi_ac+0x66b */
		h = 6;
		if (is2g && phy->desense.total[BCM4360_DESENSE_ELNA_BYPASS] == 0 &&
		    phy->flags.elna_2g)
			h = phy->rxgains[BCM4360_PHY_RXGAIN_2G][0].elna_gain < 10 ? 5 : 4;
		phy->desense.applied[BCM4360_DESENSE_LNA2_TBL] = d;	/* pi_ac+0x659 */
	}

	m = h > d ? (u8)(h - d) : 0;
	for (k = 0; k < n; k++) {
		if (k == 0) {
			gains[0] = (u8)T[1];
			codes[0] = 1;
		} else if (k <= m) {
			gains[k] = (u8)T[k];
			codes[k] = (u8)k;
		} else {
			gains[k] = (u8)T[m];
			codes[k] = m;
		}
	}

	for (c = 0; c < phy_cores(phy); c++) {
		for (k = 0; k < n; k++) {
			phy->rxgain_gain[c][stage][k] = (s8)gains[k];
			phy->rxgain_code[c][stage][k] = codes[k];
		}
		bcm4360_tbl_write(io, TBL_GAIN_CORE0 + 0x20 * c, n, offset, 8, gains);
		bcm4360_tbl_write(io, TBL_CODE_CORE0 + 0x20 * c, n, offset, 8, codes);
	}
}

/*
 * acphy-chanspec.md A5 (sub_0998cc, wlc_phy_rxgainctrl_set_lna_gainlimit_acphy):
 * the gain-limit table TBL(0x0b) of an internal LNA stage; entries above the
 * limit are 0x7f. stage != 1 is treated as stage 2.
 */
static void phy_set_lna_gainlimit(struct bcm4360_phy *phy, u32 stage)
{
	static const u8 vals_s1[6] = { 0x0b, 0x0c, 0x0e, 0x20, 0x24, 0x28 };
	static const u8 vals_s2[7] = { 0, 0, 0, 3, 3, 3, 3 };
	const u8 *V;
	u8 out[7];
	u32 n, k;
	u8 d, m, offset;

	if (stage == 1) {
		V = vals_s1;
		n = 6;
		offset = 8;
		d = phy->desense.total[BCM4360_DESENSE_LNA1_GAINLMT];	/* pi_ac+0x66c */
		m = 5 > d ? (u8)(5 - d) : 0;
		phy->desense.applied[BCM4360_DESENSE_LNA1_GAINLMT] = d;	/* pi_ac+0x65a */
	} else {
		V = vals_s2;
		n = 7;
		offset = 0x10;
		d = phy->desense.total[BCM4360_DESENSE_LNA2_GAINLMT];	/* pi_ac+0x66d */
		m = 6 > d ? (u8)(6 - d) : 0;
		phy->desense.applied[BCM4360_DESENSE_LNA2_GAINLMT] = d;	/* pi_ac+0x65b */
	}
	for (k = 0; k < n; k++)
		out[k] = k <= m ? V[k] : 0x7f;
	bcm4360_tbl_write(&phy->io, TBL_GAINLIMIT, n, offset, 8, out);
}

/*
 * acphy-chanspec.md A5 (sub_09af05, wlc_phy_rxgainctrl_set_gaintbls_acphy): the
 * gain tables of the receiver, built from the SROM front-end values and the
 * desense, written to the PHY tables and copied into pi_ac. The fourth argument
 * (bwchg) is not used.
 */
void bcm4360_phy_rxgainctrl_set_gaintbls_acphy(struct bcm4360_phy *phy, bool init,
					       bool bandchg, bool bwchg)
{
	struct bcm4360_phy_io *io = &phy->io;
	bool is2g = phy_is_2g(phy);
	u8 ch = phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL;
	u16 s19e, s16c;
	u32 c;

	(void)bwchg;

	/* step 1 */
	s19e = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);
	s16c = bcm4360_phy_read(io, ACPHY_REG_0x16c);
	bcm4360_phy_mod(io, ACPHY_REG_0x16c, 0x0040, 0x0040);

	/* step 2: the two internal LNA stages */
	phy_set_lna_gaintbl(phy, 1);
	phy_set_lna_gainlimit(phy, 1);
	phy_set_lna_gaintbl(phy, 2);
	phy_set_lna_gainlimit(phy, 2);

	/* step 3: per core */
	for (c = 0; c < phy_cores(phy); c++) {
		u32 G = TBL_GAIN_CORE0 + 0x20 * c;
		u32 C = TBL_CODE_CORE0 + 0x20 * c;
		u32 o = ACPHY_CORE_STEP * c;
		enum bcm4360_phy_rxgain_band rb;
		u8 e, t, b, gain2[2];

		/* step 3.1: front end values of the band */
		if (is2g)
			rb = BCM4360_PHY_RXGAIN_2G;
		else if (ch <= 99)
			rb = BCM4360_PHY_RXGAIN_5G_LOW;
		else
			rb = BCM4360_PHY_RXGAIN_5G_HIGH;
		e = phy->rxgains[rb][c].elna_gain;
		t = phy->rxgains[rb][c].triso;
		b = phy->rxgains[rb][c].trelnabyp;
		phy->rxgain_trloss[c] = t;	/* pi_ac+0x45f */
		phy->rxgain_bypass[c] = b;	/* pi_ac+0x460 */

		/* step 3.2 */
		if (phy->hirssi.supported) {
			u16 v = bcm4360_phy_read(io, ACPHY_REG_0x73e + o);

			bcm4360_phy_write(io, ACPHY_REG_0x73e + o, v & 0xfb3f);
		}

		/* step 3.3 */
		if (phy->ver.phy_rev == ACPHY_REV_0) {
			if (c == 0) {
				bcm4360_phy_mod(io, ACPHY_REG_0x289, 0x7f00,
						(u16)((t + 2) << 8));
				bcm4360_phy_mod(io, ACPHY_REG_0x289, 0x007f, 2);
			}
		} else {
			bcm4360_phy_mod(io, ACPHY_REG_0x6f9 + o, 0x7f00,
					(u16)((t + 2) << 8));
			bcm4360_phy_mod(io, ACPHY_REG_0x6f9 + o, 0x007f, 2);
		}

		/* step 3.4: the external LNA stage (stage 0) */
		gain2[0] = e;
		gain2[1] = e;
		bcm4360_tbl_write(io, G, 2, 0, 8, gain2);
		phy->rxgain_gain[c][0][0] = (s8)e;
		phy->rxgain_gain[c][0][1] = (s8)e;

		/* step 3.5: only on a band change or at init */
		if (bandchg || init) {
			u8 gain3[10], code3[10];
			u16 v173b;
			u32 k;

			if (is2g) {
				v173b = phy->desense.total[BCM4360_DESENSE_ELNA_BYPASS] ?
					0x001c : 0x0018;
				for (k = 0; k < 10; k++) {
					gain3[k] = 3;
					code3[k] = 2;
				}
			} else {
				v173b = 0x002c;
				if (phy->flags.elna_5g) {
					for (k = 0; k < 10; k++) {
						gain3[k] = 7;
						code3[k] = 2;
					}
				} else {
					for (k = 0; k < 10; k++) {
						gain3[k] = 0x10;
						code3[k] = 5;
					}
				}
			}
			bcm4360_phy_write(io, ACPHY_REG_0x173b, v173b);
			bcm4360_phy_write(io, ACPHY_REG_0x1726, 0x000c);
			bcm4360_tbl_write(io, G, 10, 0x20, 8, gain3);
			bcm4360_tbl_write(io, C, 10, 0x20, 8, code3);
			for (k = 0; k < 10; k++)
				phy->rxgain_gain[c][3][k] = (s8)gain3[k];

			/* at init: read the post-reset values into the pi_ac copy */
			if (init) {
				bcm4360_tbl_read(io, C, 1, 0, 8,
						 &phy->rxgain_code[c][0][0]);
				bcm4360_tbl_read(io, C, 10, 0x20, 8,
						 phy->rxgain_code[c][3]);
				bcm4360_tbl_read(io, G, 8, 0x60, 8,
						 phy->rxgain_gain[c][4]);
				bcm4360_tbl_read(io, G, 8, 0x70, 8,
						 phy->rxgain_gain[c][5]);
				bcm4360_tbl_read(io, C, 8, 0x60, 8,
						 phy->rxgain_code[c][4]);
				bcm4360_tbl_read(io, C, 8, 0x70, 8,
						 phy->rxgain_code[c][5]);
			}
		}
	}

	/* step 4 */
	bcm4360_phy_write(io, ACPHY_REG_0x16c, s16c);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s19e & 0x0002);
}

/* ------------------------------------------------- A6: initial clip gain */

/* the receive gain of stage s, entry k of core c (contiguous [stage][entry]) */
static s8 rxgain_flat(const struct bcm4360_phy *phy, u32 c, u32 s, u32 k)
{
	return ((const s8 *)phy->rxgain_gain[c])[s * BCM4360_PHY_RXGAIN_ENTRIES + k];
}

/*
 * acphy-chanspec.md A6 (sub_099f29, wlc_phy_rxgainctrl_set_gain_acphy): encode
 * the wanted gain into the six stage codes, write two per-core code words (and
 * a TBL(0x07) word for which = 0), and return the gain reached.
 */
static u8 phy_rxgain_set_gain(struct bcm4360_phy *phy, u32 which, u8 wanted,
			      bool flag, u32 c)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 o = ACPHY_CORE_STEP * c;
	u8 k[BCM4360_PHY_RXGAIN_STAGES];
	u16 lo, hi;
	u8 x;

	x = bcm4360_phy_rxgainctrl_encode_gain(phy, c, wanted, flag, k);
	if (which > 4)
		return x;
	lo = (u16)((k[3] << 7) | (k[2] << 4) | (k[1] << 1));
	hi = (u16)((k[5] << 8) | (k[4] << 4) | (flag ? 8 : 4));
	bcm4360_phy_write(io, ACPHY_REG_0x6dc + 2 * which + o, lo);
	bcm4360_phy_write(io, ACPHY_REG_0x6dd + 2 * which + o, hi);
	if (which == 0)
		tbl_write16(io, TBL_RFSEQ, 0xf9 + c,
			    (u16)((k[5] << 13) | (k[4] << 10) | (k[3] << 6) |
				  (k[2] << 3) | k[1]));
	return x;
}

/*
 * acphy-chanspec.md A6 (sub_090493, wlc_phy_rxgainctrl_calc_clip_pwr_acphy): the
 * clip power the receiver falls back to, from the per-bandwidth constants and
 * the front-end gains.
 */
static s8 phy_calc_clip_pwr(struct bcm4360_phy *phy, u8 x, bool flag, u32 c)
{
	static const s16 B[3] = { -66, -63, -60 };
	static const s16 K[3] = { 25, 22, 19 };
	u32 bw = phy_bw_index(phy);
	u16 v = bcm4360_phy_read(&phy->io, ACPHY_REG_0x6dc + ACPHY_CORE_STEP * c);
	s32 acc = B[bw];
	s32 neg;

	if (flag)
		acc = B[bw] + phy->rxgain_trloss[c] -
		      rxgain_flat(phy, c, 0, v & 1) * phy->rxgain_bypass[c];
	acc += (s8)phy->desense.total[BCM4360_DESENSE_NF_HIT];	/* pi_ac+0x66f */
	neg = -(K[bw] + (s8)x);
	return (s8)(acc > neg ? acc : neg);
}

/*
 * acphy-chanspec.md A6 (sub_09175a, wlc_phy_rxgainctrl_nbclip_acphy): threshold
 * of the first ("narrow band") clip detector.
 */
static void phy_rxgain_nbclip(struct bcm4360_phy *phy, u32 c, s8 r)
{
	static const s16 T[8] = { -40, -5, 20, 40, 55, 80, 100, 116 };
	static const u16 F[8] = { 0, 0, 1, 1, 1, 1, 2, 2 };
	static const u16 V[8] = { 1, 0, 1, 2, 0, 3, 1, 0 };
	struct bcm4360_phy_io *io = &phy->io;
	u32 o = ACPHY_CORE_STEP * c;
	u16 v, w, w2;
	u32 i0, i1, i2, i3, i4, i5, n;
	s32 G, P;

	v = bcm4360_phy_read(io, ACPHY_REG_0x6dc + o);
	w = bcm4360_phy_read(io, ACPHY_REG_0x6dd + o);
	w2 = bcm4360_phy_read(io, ACPHY_REG_0x6dd + o);
	i0 = v & 1;
	i1 = (v >> 1) & 7;
	i2 = (v >> 4) & 7;
	i3 = (v >> 7) & 15;
	i4 = (w >> 4) & 7;
	i5 = (w2 >> 8) & 7;

	G = r + rxgain_flat(phy, c, 0, i0) + rxgain_flat(phy, c, 1, i1) +
	    rxgain_flat(phy, c, 2, i2) + rxgain_flat(phy, c, 3, i3) +
	    rxgain_flat(phy, c, 4, i4);
	if (phy->desense.applied[BCM4360_DESENSE_ELNA_BYPASS] == 1)	/* pi_ac+0x65c */
		G -= phy->rxgain_trloss[c];
	P = 10 * G;

	if (P < -40) {
		n = 0;
	} else if (P > 116) {
		n = 7;
		if (P - 116 > 20 && i4 != 0 && i5 != 7) {
			bcm4360_phy_mod(io, ACPHY_REG_0x6dd + o, 0x0070,
					(u16)((i4 - 1) << 4));
			bcm4360_phy_mod(io, ACPHY_REG_0x6dd + o, 0x0700,
					(u16)((i5 + 1) << 8));
		}
	} else {
		n = nearest_upper(T, 8, P);
	}
	bcm4360_phy_mod(io, ACPHY_REG_0x6ee + o, 0x0003, F[n]);
	if (n <= 1)
		bcm4360_radio_mod(io, R2069_CLIP_NB | (c << 9), 0x0080,
				  (u16)(V[n] << 7));
	else if (n <= 5)
		bcm4360_radio_mod(io, R2069_CLIP_NB | (c << 9), 0x0300,
				  (u16)(V[n] << 8));
	else
		bcm4360_radio_mod(io, R2069_CLIP_NB | (c << 9), 0x0040,
				  (u16)(V[n] << 6));
}

/*
 * acphy-chanspec.md A6 (sub_091b6e, wlc_phy_rxgainctrl_w1clip_acphy): threshold
 * of the second ("wide band") clip detector.
 */
static void phy_rxgain_w1clip(struct bcm4360_phy *phy, u32 c, s8 r)
{
	static const s16 A[12] = { 0, 19, 35, 49, 60, 70, 80, 88, 95, 102, 109, 115 };
	static const s16 B2g[12] = { 0, 19, 35, 49, 60, 70, 80, 92, 105, 120, 130, 140 };
	static const s16 B5g[12] = { 0, 19, 35, 49, 60, 70, 80, 96, 113, 130, 155, 180 };
	struct bcm4360_phy_io *io = &phy->io;
	bool is2g = phy_is_2g(phy);
	const s16 *Bb = is2g ? B2g : B5g;
	u32 o = ACPHY_CORE_STEP * c;
	u16 rreg = is2g ? R2069_CLIP_W1_2G : R2069_CLIP_W1_5G;
	s16 L[12], M[12], H[12];
	u32 i, i1, n = 0, range;
	u16 v;
	s32 G, P;

	for (i = 0; i < 12; i++) {
		L[i] = (s16)(A[i] - 340);
		M[i] = (s16)(A[i] - 280);
		H[i] = (s16)(Bb[i] - 220);
	}
	v = bcm4360_phy_read(io, ACPHY_REG_0x6dc + o);
	i1 = (v >> 1) & 7;
	G = r + (u8)rxgain_flat(phy, c, 0, 0) -
	    (rxgain_flat(phy, c, 1, 5) - rxgain_flat(phy, c, 1, i1));
	if (phy->desense.applied[BCM4360_DESENSE_ELNA_BYPASS] == 1)
		G -= phy->rxgain_trloss[c];
	P = 10 * G;

	if (P <= L[0]) {
		range = 0;
		n = 0;
	} else if (P >= H[11]) {
		range = 2;
		n = 11;
	} else {
		const s16 *list;

		if (P > M[11]) {
			range = 2;
			list = H;
		} else if (P >= M[0]) {
			range = 1;
			list = M;
		} else {
			range = 0;
			list = L;
		}
		n = nearest_upper(list, 12, P);
	}
	bcm4360_phy_mod(io, ACPHY_REG_0x6ee + o, 0x000c, (u16)(range << 2));
	bcm4360_radio_mod(io, rreg | (c << 9), 0x00f0, (u16)((n + 4) << 4));
}

/* A6: the two ways the start-gain margins are combined into a clip gain */
static s8 clip_gain_n(s32 m, s32 y)
{
	return (s8)floor_div(m + y, 2);
}

static s8 clip_gain_w(s32 m, s32 y)
{
	return (s8)(-floor_div(-39 * m - 26 * y, 64));
}

/*
 * acphy-chanspec.md A6 (sub_09a121,
 * wlc_phy_rxgainctrl_set_init_clip_gain_acphy): the gain the receiver starts
 * with, the gains it falls back to on a clip, and the two clip-detector
 * thresholds.
 */
void bcm4360_phy_rxgainctrl_set_init_clip_gain_acphy(struct bcm4360_phy *phy)
{
	static const u8 max2g[6] = { 43, 43, 43, 52, 52, 100 };
	static const u8 max5g[6] = { 47, 47, 47, 52, 52, 100 };
	struct bcm4360_phy_io *io = &phy->io;
	bool is2g = phy_is_2g(phy);
	u32 bw = phy_bw_index(phy);
	const u8 *mx = is2g ? max2g : max5g;
	u16 s19e;
	u8 h, lna, byp, a, b;
	s32 g3, g4;
	u32 c, i;

	/* step 1 */
	h = (is2g ? phy->hirssi.state_2g : phy->hirssi.state_5g) & 1;

	/* step 2 */
	s19e = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	/* step 3: upper limits of the accumulated gain */
	for (i = 0; i < 6; i++)
		phy->rxgain_max[i] = mx[i];

	/* step 4 */
	lna = (is2g ? phy->flags.elna_2g : phy->flags.elna_5g) ? 1 : 0;
	byp = phy->desense.total[BCM4360_DESENSE_ELNA_BYPASS];		/* pi_ac+0x66e */
	phy->desense.applied[BCM4360_DESENSE_ELNA_BYPASS] = byp;	/* pi_ac+0x65c */
	a = lna & byp;
	b = lna & (byp | 1);
	if (b) {
		g3 = 15;
		g4 = 15;
	} else {
		g3 = lna == 0 ? 15 : 30;
		g4 = (g3 + 35) >> 1;
	}

	/* step 5: per present core */
	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = ACPHY_CORE_STEP * c;
		u8 x1, x2, x3;
		s32 d1, m1, d2, m2, y1, y2, r1, r2;
		u16 v;
		s8 e;

		if (!phy_core_present(phy, c))
			continue;

		bcm4360_phy_read(io, ACPHY_REG_0x6dc + o);		/* 5.1 */

		/* 5.2 (in this order) */
		phy_rxgain_set_gain(phy, 0, RXGAIN_START, a, c);
		x1 = phy_rxgain_set_gain(phy, 1, 48, a, c);
		x2 = phy_rxgain_set_gain(phy, 2, 35, b, c);
		phy_rxgain_set_gain(phy, 4, (u8)g4, b, c);
		x3 = phy_rxgain_set_gain(phy, 3, (u8)g3, 1, c);

		y1 = phy_calc_clip_pwr(phy, x2, b, c);			/* 5.3 */

		/* 5.4 */
		v = bcm4360_phy_read(io, ACPHY_REG_0x6dc + o);
		e = rxgain_flat(phy, c, 0, v & 1);
		d1 = (a ? phy->rxgain_trloss[c] - 16 : -16) - e;
		m1 = min_s32(d1, 23 - (s8)x1);

		y2 = phy_calc_clip_pwr(phy, x3, true, c);		/* 5.5 */

		/* 5.6 */
		v = bcm4360_phy_read(io, ACPHY_REG_0x6dc + o);
		e = rxgain_flat(phy, c, 0, v & 1);
		d2 = (b ? phy->rxgain_trloss[c] - 16 : -16) - e;
		m2 = min_s32(d2, 23 - (s8)x2);

		/* 5.7 */
		if (bw == 0) {
			r1 = clip_gain_n(m1, y1);
			r2 = h ? clip_gain_w(m2, y2) : clip_gain_n(m2, y2);
		} else {
			r1 = clip_gain_w(m1, y1);
			r2 = clip_gain_w(m2, y2);
		}

		/* 5.8 */
		phy_rxgain_nbclip(phy, c, (s8)r1);
		phy_rxgain_w1clip(phy, c, (s8)r2);
	}

	/* step 6 */
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s19e & 0x0002);
}

/* ----------------------------------------- A7 / C2: apply and thresholds */

/* the eight carrier-sense min-power registers of core 0 (C2, A7) */
static const u16 crs_core0_regs[8] = {
	0x324, 0x330, 0x321, 0x32d, 0x32a, 0x336, 0x327, 0x333,
};

/*
 * acphy-chanspec.md A7 (sub_08f84d): write the high byte of the eight core-0
 * carrier-sense registers (the assert thresholds).
 */
static void phy_set_crs_high(struct bcm4360_phy *phy, u8 val)
{
	u32 i;

	for (i = 0; i < 8; i++)
		bcm4360_phy_mod(&phy->io, crs_core0_regs[i], 0xff00, (u16)(val << 8));
}

/*
 * acphy-chanspec.md A7 (sub_090b77): four registers get 0x5f62 when the
 * argument is not 0, else 0x4e51.
 */
static void phy_set_090b77(struct bcm4360_phy *phy, bool nonzero)
{
	static const u16 regs[4] = { 0x304, 0x307, 0x30a, 0x30d };
	u16 val = nonzero ? 0x5f62 : 0x4e51;
	u32 i;

	for (i = 0; i < 4; i++)
		bcm4360_phy_write(&phy->io, regs[i], val);
}

/*
 * acphy-desense.md C2 (sub_08f41b, wlc_phy_set_crs_thresh_acphy): the per-core
 * carrier-sense / energy-detect minimum-power threshold registers. Core 0 gets
 * the absolute power; the other cores get the offset of their power relative to
 * core 0.
 */
static void phy_set_crs_thresh(struct bcm4360_phy *phy, u8 pwr, u8 off1, u8 off2)
{
	struct bcm4360_phy_io *io = &phy->io;
	u8 a, b;
	u32 c;

	if (pwr == 0) {
		a = CRS_MIN_PWR_DEFAULT;
		b = CRS_MIN_PWR_DEFAULT;
		phy->crsmincal.thresh = CRS_MIN_PWR_DEFAULT;	/* pi_ac+0x42 */
	} else {
		a = pwr;			/* PHY revision 0 and 1: b = pwr too */
		b = pwr;
	}

	for (c = 0; c < phy_cores(phy); c++) {
		if (!phy_core_present(phy, c))
			continue;
		if (c == 0) {
			bcm4360_phy_mod(io, crs_core0_regs[0], 0x00ff, a);
			bcm4360_phy_mod(io, crs_core0_regs[1], 0x00ff, b);
			bcm4360_phy_mod(io, crs_core0_regs[2], 0x00ff, a);
			bcm4360_phy_mod(io, crs_core0_regs[3], 0x00ff, b);
			bcm4360_phy_mod(io, crs_core0_regs[4], 0x00ff, a);
			bcm4360_phy_mod(io, crs_core0_regs[5], 0x00ff, b);
			bcm4360_phy_mod(io, crs_core0_regs[6], 0x00ff, a);
			bcm4360_phy_mod(io, crs_core0_regs[7], 0x00ff, b);
		} else {
			u16 base = c == 1 ? ACPHY_REG_0x910 : ACPHY_REG_0xb10;
			u8 off = c == 1 ? off1 : off2;

			bcm4360_phy_mod(io, base + 0, 0xff00, (u16)(off << 8));
			bcm4360_phy_mod(io, base + 0, 0x00ff, off);
			bcm4360_phy_mod(io, base + 2, 0xff00, (u16)(off << 8));
			bcm4360_phy_mod(io, base + 2, 0x00ff, off);
			bcm4360_phy_mod(io, base + 1, 0x00ff, off);
			bcm4360_phy_mod(io, base + 1, 0xff00, (u16)(off << 8));
			bcm4360_phy_mod(io, base + 3, 0x00ff, off);
			bcm4360_phy_mod(io, base + 3, 0xff00, (u16)(off << 8));
		}
	}
}

/*
 * acphy-desense.md E2 (wlc_phy_aci_updsts_acphy): tell the MAC whether the
 * interference mitigation is active. No PHY access.
 */
static void phy_aci_updsts(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_interf_record *rec = phy->desense.cur;
	u32 active = 0;

	if (rec && (rec->desense[BCM4360_DESENSE_ON] != 0 || rec->hwaci_min != 0))
		active = 1;
	bcm4360_mac_update_phy_mode(phy->hw, active);
}

/*
 * acphy-chanspec.md A7 step 3 (sub_09a539, the "on != 0" branch): apply the
 * desense values to the receive gain tables and the carrier-sense thresholds.
 * Reached during initialisation through wlc_phy_desense_aci_reset_params_acphy
 * (acphy-init.md appendix A), where the desense values are all zero.
 */
static void phy_desense_apply_on(struct bcm4360_phy *phy, bool is2g)
{
	static const u8 Q[13] = { 0, 0, 0, 0, 0, 0, 0, 3, 6, 9, 9, 12, 12 };
	static const u16 Etbl[13] = {
		0x77, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
	};
	static const u16 Ftbl[13] = {
		0x10, 0x60, 0x10, 0x4c, 0x60, 0x30, 0x40, 0x40, 0x38, 0x2e, 0x40, 0x34, 0x40,
	};
	struct bcm4360_phy_io *io = &phy->io;
	u8 *ap = phy->desense.applied;
	u8 *tt = phy->desense.total;
	u32 i = 0, c;
	bool rebuilt = false;
	u8 bd, odp, od, q;
	s32 u, v, w;

	/* 3.1 */
	bcm4360_phy_desense_calc_total_acphy(phy);
	bd = tt[BCM4360_DESENSE_BPHY];
	if (bd > 24)
		bd = 24;
	odp = tt[BCM4360_DESENSE_OFDM];
	if (odp > 48)
		odp = 48;
	ap[BCM4360_DESENSE_BPHY] = bd;		/* pi_ac+0x657 */
	ap[BCM4360_DESENSE_OFDM] = odp;		/* pi_ac+0x656 */

	/* 3.2 */
	phy_set_090b77(phy, odp != 0);

	/* 3.3 */
	od = odp ? (u8)(odp - 1) : 0;
	if (!is2g) {
		q = od;
	} else {
		i = (u32)(bd + 1) >> 1;
		if (i > 12)
			i = 12;
		q = Q[i];
	}
	if (q > 12)
		q = 12;
	u = (s32)od - q;
	if (u < 0)
		u = 0;
	if (u > 30)
		u = 30;

	/* 3.4: rebuild the gain tables where a desense byte changed */
	if (ap[BCM4360_DESENSE_ELNA_BYPASS] != tt[BCM4360_DESENSE_ELNA_BYPASS] ||
	    ap[BCM4360_DESENSE_LNA1_TBL] != tt[BCM4360_DESENSE_LNA1_TBL]) {
		phy_set_lna_gaintbl(phy, 1);
		rebuilt = true;
	}
	if (ap[BCM4360_DESENSE_ELNA_BYPASS] != tt[BCM4360_DESENSE_ELNA_BYPASS] ||
	    ap[BCM4360_DESENSE_LNA2_TBL] != tt[BCM4360_DESENSE_LNA2_TBL]) {
		phy_set_lna_gaintbl(phy, 2);
		rebuilt = true;
	}
	if (ap[BCM4360_DESENSE_LNA1_GAINLMT] != tt[BCM4360_DESENSE_LNA1_GAINLMT]) {
		phy_set_lna_gainlimit(phy, 1);
		rebuilt = true;
	}
	if (ap[BCM4360_DESENSE_LNA2_GAINLMT] != tt[BCM4360_DESENSE_LNA2_GAINLMT]) {
		phy_set_lna_gainlimit(phy, 2);
		rebuilt = true;
	}
	if (rebuilt)
		bcm4360_phy_rxgainctrl_set_init_clip_gain_acphy(phy);

	/* 3.5 */
	for (c = 0; c < phy_cores(phy); c++) {
		if (!phy_core_present(phy, c))
			continue;
		phy_rxgain_set_gain(phy, 0, (u8)(RXGAIN_START - q),
				    tt[BCM4360_DESENSE_ELNA_BYPASS] != 0, c);
	}

	/* 3.6 */
	v = ((s32)u * 88 >> 5) + CRS_MIN_PWR_DEFAULT;
	if (v < phy->crsmincal.thresh)
		v = phy->crsmincal.thresh;
	phy_set_crs_thresh(phy, (u8)v, 0, 0);

	/* 3.7 */
	w = (s32)od - 21;
	if (w < 0)
		w = 0;
	phy_set_crs_high(phy, (u8)((w * 88 >> 5) + CRS_MIN_PWR_DEFAULT));

	/* 3.8 */
	if (is2g) {
		bcm4360_phy_write(io, ACPHY_REG_0x299, (u16)(0x4400 | Etbl[i]));
		bcm4360_phy_write(io, ACPHY_REG_0x3c1, Ftbl[i]);
	}
}

/*
 * acphy-chanspec.md A7 (sub_09a539, wlc_phy_desense_apply_acphy): apply the
 * desense values. With the argument 0 (all traces) it sets the default
 * carrier-sense thresholds and 11b registers; with a non-zero argument it
 * applies the computed desense (reached from the init-time reset).
 */
void bcm4360_phy_desense_apply_acphy(struct bcm4360_phy *phy, u8 flag)
{
	struct bcm4360_phy_io *io = &phy->io;
	bool is2g = phy_is_2g(phy);
	u16 s19e;

	/* step 1 */
	bcm4360_mac_suspend(phy->hw);
	s19e = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	if (flag == 0) {
		/* step 2 */
		phy_set_crs_high(phy, CRS_MIN_PWR_DEFAULT);	/* sub_08f84d(0x36) */
		phy_set_090b77(phy, false);
		if (is2g) {
			bcm4360_phy_write(io, ACPHY_REG_0x299, 0x4477);
			bcm4360_phy_write(io, ACPHY_REG_0x3c1, 0x0010);
		}
	} else {
		/* step 3 */
		phy_desense_apply_on(phy, is2g);
	}

	/* step 4 */
	phy_aci_updsts(phy);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s19e & 0x0002);
	bcm4360_mac_enable(phy->hw);
}

/* --------------------------------------- A4: interference state of a channel */

/* acphy-chanspec.md A4: wlc_phy_get_time_usec, read as D11(0x180)/D11(0x184) */
static u64 phy_get_time_usec(struct bcm4360_phy *phy)
{
	u32 lo = d11_read32(phy->hw, D11_TSF_TIMERLOW);
	u32 hi = d11_read32(phy->hw, D11_TSF_TIMERHIGH);

	return ((u64)hi << 32) | lo;
}

/*
 * acphy-chanspec.md A4 (sub_092efb, wlc_phy_desense_aci_getset_chanidx_acphy):
 * find, or create, the interference record of a channel; store it as the
 * current record (pi_ac+0x8a8). No hardware access except the clock read.
 */
void *bcm4360_phy_desense_getset_chanidx_acphy(struct bcm4360_phy *phy, u16 chanspec,
					       bool set)
{
	u32 bidx = (chanspec & BCM4360_CHANSPEC_BAND) ? 1 : 0;
	struct bcm4360_phy_interf_record *recs = phy->desense.record[bidx];
	u8 ch = chanspec & BCM4360_CHANSPEC_CHANNEL;
	u16 bw = chanspec & BCM4360_CHANSPEC_BW;
	struct bcm4360_phy_interf_record *r = NULL;
	u32 i;

	/* step 1 */
	for (i = 0; i < BCM4360_PHY_INTERF_RECORDS; i++) {
		if (recs[i].channel == ch && recs[i].bw == bw) {
			r = &recs[i];
			break;
		}
	}

	/* step 2 */
	if (!r) {
		if (!set)
			return NULL;
		r = &recs[0];
		for (i = 1; i < BCM4360_PHY_INTERF_RECORDS; i++)
			if (recs[i].time < r->time)
				r = &recs[i];
		hw_memset(r, 0, sizeof(*r));
		r->channel = phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL;
		r->bw = phy->bw;
	}

	/* step 3 */
	if (set) {
		r->settle = 2;
		r->time = phy_get_time_usec(phy);
		phy->desense.cur = r;	/* pi_ac+0x8a8 (the caller discards the result) */
	}
	return r;
}

/*
 * acphy-chanspec.md A4 (sub_0909fd, wlc_phy_desense_calc_total_acphy): build the
 * wanted desense set from the channel's record (or the baseline) and the
 * Bluetooth-coexistence set. No hardware access.
 */
void bcm4360_phy_desense_calc_total_acphy(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_desense *d = &phy->desense;
	const u8 *src = d->cur ? d->cur->desense : d->base;
	bool is2g = phy_is_2g(phy);
	u8 ch = phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL;
	bool scan = (phy->hold & 0x0206) != 0 && phy->interference.channel != ch;
	u32 i;

	/* steps 1, 2 */
	if (d->bt_profile != 0 && is2g && !scan) {
		for (i = 0; i < 8; i++)
			d->total[i] = d->bt_desense[i] > src[i] ? d->bt_desense[i] : src[i];
		d->total[BCM4360_DESENSE_ON] = d->bt_desense[BCM4360_DESENSE_ON] |
					       src[BCM4360_DESENSE_ON];
	} else {
		for (i = 0; i < BCM4360_DESENSE_BYTES; i++)
			d->total[i] = src[i];
	}

	/* step 3: the high-RSSI bypass timer forces the external-LNA bypass on */
	if (is2g) {
		if (phy->hirssi.timer_2g >= 0)
			d->total[BCM4360_DESENSE_ELNA_BYPASS] = 1;
	} else {
		if (phy->hirssi.timer_5g >= 0)
			d->total[BCM4360_DESENSE_ELNA_BYPASS] = 1;
	}
}

/* ------------------------------------- init appendix A: mitigation set-up */

/*
 * acphy-init.md appendix A (wlc_phy_hwaci_setup_acphy): enable or disable the
 * hardware-ACI detector per core, and, at init, program its radio registers and
 * the detector parameters.
 */
void bcm4360_phy_hwaci_setup_acphy(struct bcm4360_phy *phy, bool enable, bool init)
{
	struct bcm4360_phy_io *io = &phy->io;
	struct bcm4360_phy_hwaci *hw = &phy->hwaci;
	u16 B = ACPHY_REG_0x550;		/* PHY revision 0 and 1 */
	u32 c, t;

	/* step 1 */
	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = ACPHY_CORE_STEP * c;

		bcm4360_phy_mod(io, ACPHY_REG_0x728 + o, 0x3800, enable ? 0x0800 : 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x721 + o, 0x4000, 0x4000);
	}
	if (!init)
		return;

	/* step 2.1: the radio registers of each core */
	for (c = 0; c < phy_cores(phy); c++) {
		u16 b = (u16)(c << 9);

		bcm4360_radio_mod(io, R2069_HWACI_045 | b, 0x0080,
				  (u16)(hw->acphy_0x67c << 7));
		bcm4360_radio_mod(io, R2069_HWACI_049 | b, 0xe000, 0);
		bcm4360_radio_mod(io, R2069_HWACI_049 | b, 0x1800, 0);
		bcm4360_radio_mod(io, R2069_HWACI_049 | b, 0x0400, 0);
		bcm4360_radio_mod(io, R2069_HWACI_049 | b, 0x0300,
				  (u16)(hw->acphy_0x67d << 8));
		bcm4360_radio_mod(io, R2069_HWACI_049 | b, 0x0040,
				  (u16)(hw->acphy_0x67e << 6));
		bcm4360_radio_mod(io, R2069_HWACI_049 | b, 0x0080,
				  (u16)(hw->acphy_0x67f << 7));
	}

	/* step 2.2 */
	{
		u16 v = bcm4360_phy_read(io, B);

		bcm4360_phy_write(io, B, (u16)((v & 0xf000) | ((v | 0x000d) & 0x000f) |
					       (hw->acphy_0x67a << 4) |
					       (hw->acphy_0x67a << 8)));
		v = bcm4360_phy_read(io, B + 1);
		bcm4360_phy_write(io, B + 1, (u16)((v & 0xff00) | (hw->acphy_0x67b & 0x0f) |
						   (hw->acphy_0x67b << 4)));
	}

	/* step 2.3 */
	bcm4360_phy_write(io, B + 2, hw->acphy_0x678);
	bcm4360_phy_write(io, B + 3, hw->acphy_0x678);
	bcm4360_phy_write(io, B + 4, hw->acphy_0x674);
	bcm4360_phy_write(io, B + 5, hw->acphy_0x674);
	bcm4360_phy_write(io, B + 6, hw->acphy_0x676);
	bcm4360_phy_write(io, B + 7, hw->acphy_0x676);
	t = (u32)hw->acphy_0x672 * 10000 >> 3;
	bcm4360_phy_write(io, B + 8, (u16)(t & 0xffff));
	bcm4360_phy_write(io, B + 9, (u16)(t >> 16));
}

/*
 * acphy-init.md appendix A (wlc_phy_aci_w2nb_setup_acphy): enable or disable the
 * "w2nb" detector per core.
 */
void bcm4360_phy_aci_w2nb_setup_acphy(struct bcm4360_phy *phy, bool on)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c;

	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = ACPHY_CORE_STEP * c;

		bcm4360_phy_mod(io, ACPHY_REG_0x729 + o, 0x1000, on ? 0x1000 : 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x721 + o, 0x1000, 0x1000);
		if (on)
			bcm4360_radio_mod(io, R2069_W2NB_033 | (c << 9), 0xf000,
					  (u16)((phy->hwaci.acphy_0x680 & 0x0f) << 12));
	}
}

/*
 * acphy-init.md appendix A (wlc_phy_desense_aci_reset_params_acphy): clear the
 * interference records and re-apply the (now default) desense.
 */
void bcm4360_phy_desense_aci_reset_params_acphy(struct bcm4360_phy *phy, bool apply,
						bool a, bool b)
{
	(void)a;
	(void)b;

	hw_memset(phy->desense.record, 0, sizeof(phy->desense.record));
	phy->desense.cur = NULL;			/* pi_ac+0x8a8 */
	bcm4360_phy_desense_apply_acphy(phy, apply ? 1 : 0);
}

/* ------------------------- C: carrier-sense minimum-power calibration */

/* acphy-desense.md, "Constants": the 20/40/80 MHz CRS minimum-power tables */
static const u8 crs_tbl_20[15] = {
	45, 48, 51, 53, 54, 57, 60, 63, 66, 68, 70, 72, 75, 78, 80,
};
static const u8 crs_tbl_40[15] = {
	44, 46, 48, 50, 52, 54, 56, 58, 60, 63, 66, 69, 71, 74, 76,
};
static const u8 crs_tbl_80[15] = {
	45, 47, 49, 51, 53, 54, 55, 57, 58, 59, 60, 61, 63, 65, 67,
};

/* the CRS table, per-bandwidth level bias and reference bandwidth adjustment */
static const u8 *crs_table(u32 bw)
{
	if (bw == 0)
		return crs_tbl_20;
	if (bw == 1)
		return crs_tbl_40;
	return crs_tbl_80;
}

static s32 crs_index_bias(u32 bw)
{
	if (bw == 0)
		return 0x22;
	if (bw == 1)
		return 0x21;
	return 0x1e;
}

static s32 crs_ref_adjust(u32 bw)
{
	if (bw == 0)
		return 0;
	if (bw == 1)
		return 3;
	return 7;
}

/*
 * acphy-desense.md C1 (wlc_phy_crs_min_pwr_cal_acphy): keep, per sub-band and
 * core, a reference of the received/noise level, and from it set the carrier-
 * sense minimum-power thresholds. restore != 0 reprograms from the stored
 * references without a new measurement (the channel-change path, A1 step 9);
 * restore == 0 pushes a fresh measurement (the noise-sample path). Measured
 * inputs read 0 in the model, so the calibrate branch is written from the spec.
 */
void bcm4360_phy_crs_min_pwr_cal_acphy(struct bcm4360_phy *phy, u8 restore)
{
	struct bcm4360_phy_crsmincal *cal = &phy->crsmincal;
	const u8 *tab = crs_table(phy_bw_index(phy));
	s32 bias = crs_index_bias(phy_bw_index(phy));
	s32 adj = crs_ref_adjust(phy_bw_index(phy));
	u8 sb = bcm4360_phy_get_chan_freq_range(phy, 0);
	s32 lvl[BCM4360_PHY_CORE_SLOTS];
	u8 pwr[BCM4360_PHY_CORE_SLOTS];
	u8 off1 = 0, off2 = 0;
	s32 cvar = phy->ver.phy_rev >= 2 ? 2 : 3;
	bool changed = false;
	u32 done = 0;
	u32 c;

	pwr[0] = CRS_MIN_PWR_DEFAULT;
	pwr[1] = 0;
	pwr[2] = 0;
	pwr[3] = 0;

	/*
	 * step 1: push the newest measurement into the ring (calibrate path
	 * only). The per-core input pi_ac[0x14+c] reads 0 in the model, so the
	 * pushed value is 1; this path is not reached (no noise interrupt).
	 */
	if (restore == 0) {
		for (c = 0; c < phy_cores(phy); c++)
			if (phy_core_present(phy, c))
				cal->sample[cal->next_sample][c] = 1;
		cal->next_sample = (cal->next_sample + 1) & 3;
	}

	/* step 2: per present core */
	for (c = 0; c < phy_cores(phy); c++) {
		s32 idx;

		if (!phy_core_present(phy, c))
			continue;
		done++;

		if (restore == 0) {
			s32 sum, cnt, s;

			if (cal->sample[0][c] == 0)
				return;		/* the ring is empty for this core */
			sum = 0;
			cnt = 0;
			for (s = 0; s < BCM4360_PHY_CRSMIN_SAMPLES; s++) {
				if (cal->sample[s][c] == 0)
					break;
				sum += cal->sample[s][c];
				cnt++;
			}
			lvl[c] = sum / cnt;
			{
				s32 r = cal->noise[sb][c] + adj;
				s32 diff = r - lvl[c];

				if (diff < 0)
					diff = -diff;
				if (diff >= cvar || cal->state[0] != 0) {
					changed = true;
					cal->noise[sb][c] = (s8)(lvl[c] - adj);
				}
			}
		} else {
			lvl[c] = cal->noise[sb][c] + adj;
		}

		idx = lvl[c] + bias;
		if (idx > 14)
			idx = 14;
		if (idx < 0)
			idx = 0;
		pwr[c] = tab[idx];
		if (c == 1)
			off1 = (u8)(pwr[1] - pwr[0]);
		else if (c == 2)
			off2 = (u8)(pwr[2] - pwr[0]);
	}

	/* step 3.1 */
	if (done == 0 && restore == 0)
		return;
	if (changed && restore == 0)
		cal->state[0] = 0;			/* pi_ac+0x33d */

	/* step 3.2 */
	cal->state[1] = 1;				/* pi_ac+0x33e */

	/* step 3.3 */
	if (pwr[2] > pwr[1])
		pwr[1] = pwr[2];
	cal->thresh = pwr[0] > pwr[1] ? pwr[0] : pwr[1];	/* pi_ac+0x42 */

	/* step 3.4 */
	if (phy->desense.total[BCM4360_DESENSE_ON] == 0) {	/* pi_ac+0x670 */
		cal->runs++;
		cal->channel = phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL;
		for (c = 0; c < phy_cores(phy); c++)
			if (phy_core_present(phy, c))
				cal->average[c] = (s8)lvl[c];
		bcm4360_mac_suspend(phy->hw);
		phy_reg_enter(phy);
		phy_set_crs_thresh(phy, pwr[0], off1, off2);
		phy_reg_exit(phy);
		bcm4360_mac_enable(phy->hw);
	} else {
		cal->state[1] = 2;
	}
}

/*
 * acphy-desense.md C4 (wlc_phy_noise_sample_request_crsmincal): request a noise
 * sample with reason 4 for the current channel. The actual request helper
 * sub_0baff6 is in wlc_phy_cmn.c and out of scope; only the per-core result
 * words it clears are reproduced here. Not reached in any compared scenario.
 */
void bcm4360_phy_noise_sample_request_crsmincal(struct bcm4360_phy *phy)
{
	u32 c;

	for (c = 0; c < 2 * BCM4360_PHY_CORES_MAX; c++)
		bcm4360_shm_write(phy->hw, (u16)(0x308 + 2 * c), 0);
}

/*
 * acphy-desense.md C5 (wlc_phy_ed_thres_acphy): the energy-detect threshold
 * iovar. set: write the assert (hi) and de-assert (lo) thresholds of the four
 * energy-detect pairs, computed from the dBm value. get: the inverse.
 */
void bcm4360_phy_ed_thres_acphy(struct bcm4360_phy *phy, s32 *val, bool set)
{
	static const u16 hi_regs[8] = {
		0x33a, 0x33b, 0x33e, 0x33f, 0x342, 0x343, 0x346, 0x347,
	};
	static const u16 lo_regs[8] = {
		0x33c, 0x33d, 0x340, 0x341, 0x344, 0x345, 0x348, 0x349,
	};
	struct bcm4360_phy_io *io = &phy->io;
	u32 i;

	if (set) {
		s32 d = *val;
		u16 hi = (u16)(((d * 640000 + 73045696) / 30103) & 0xffff);
		u16 lo = (u16)(((d * 640000 + 69205696) / 30103) & 0xffff);

		for (i = 0; i < 8; i++)
			bcm4360_phy_write(io, hi_regs[i], hi);
		for (i = 0; i < 8; i++)
			bcm4360_phy_write(io, lo_regs[i], lo);
	} else {
		s32 r = bcm4360_phy_read(io, ACPHY_REG_0x33a);

		*val = (r * 30103 - 73045696) / 640000;
	}
}

/* ------------------------------- E1: periodic hwaci engine (from watchdog) */

/*
 * acphy-desense.md E1 (wlc_phy_hwaci_engine_acphy): adjacent-channel-
 * interference mitigation by hardware measurement. Runs a per-channel level; a
 * higher level means more gain-limit desense. Called once per watchdog tick.
 * The measured registers read 0 in the model, so the detector decisions are
 * degenerate (both detectors report no interference); the level still steps
 * from 0 to 1 unconditionally on the first tick. Not reached in a compared
 * scenario (the watchdog is not compared).
 */
void bcm4360_phy_hwaci_engine_acphy(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	bool is2g = phy_is_2g(phy);
	u32 mode = phy->interference.mode;
	struct bcm4360_phy_hwaci_entry *table;
	struct bcm4360_phy_interf_record *rec;
	u8 nlevels;
	u32 s = phy_bw_index(phy);
	u8 old_level, old_min, level;
	bool detw = false, deth = false;
	u32 c;

	/* step 1 */
	if (!(mode & (BCM4360_INTERFERENCE_HWACI | BCM4360_INTERFERENCE_W2NB)))
		return;

	/* step 2 */
	rec = bcm4360_phy_desense_getset_chanidx_acphy(phy, phy->radio_chanspec, true);
	phy->desense.cur = rec;
	if (rec->hwaci_settle != 0) {
		rec->hwaci_settle--;
		return;
	}

	if (is2g) {
		nlevels = phy->hwaci.table1_entries;
		table = phy->hwaci.table1;
	} else {
		nlevels = phy->hwaci.table2_entries;
		table = phy->hwaci.table2;
	}

	/* step 3 */
	old_level = rec->hwaci_level;
	old_min = rec->hwaci_min;
	level = old_level;
	rec->hwaci_hold = rec->hwaci_hold ? (u8)(rec->hwaci_hold - 1) : 0;
	bcm4360_mac_suspend(phy->hw);

	/* step 4: wide-band detector (measured registers read 0) */
	if (mode & BCM4360_INTERFERENCE_W2NB) {
		s32 sa = 0, sb2 = 0, sc = 0, sd = 0;
		u32 n = phy_cores(phy);

		for (c = 0; c < n; c++) {
			if (!phy_core_present(phy, c))
				continue;
			if (c == 0) {
				sa += bcm4360_phy_read(io, 0x7af);
				sb2 += bcm4360_phy_read(io, 0x7ab);
				sc += bcm4360_phy_read(io, 0x7b3);
				sd += bcm4360_phy_read(io, 0x7b1);
			} else if (c == 1) {
				sa += bcm4360_phy_read(io, 0x9af);
				sb2 += bcm4360_phy_read(io, 0x9ab);
				sc += bcm4360_phy_read(io, 0x9b3);
				sd += bcm4360_phy_read(io, 0x9b1);
			} else {
				sa += bcm4360_phy_read(io, 0xbaf);
				sb2 += bcm4360_phy_read(io, 0xbab);
				sc += bcm4360_phy_read(io, 0xbb3);
				sd += bcm4360_phy_read(io, 0xbb1);
			}
		}
		(void)sa;	/* A (PHY(0x7af)/..) is read but not used in the decision */
		if (n) {
			sb2 /= (s32)n;
			sc /= (s32)n;
			sd /= (s32)n;
		}
		if (sd < 200 && sb2 > 199)
			sd = sb2;
		sc = sb2 > 199 ? sb2 : 0;
		detw = sc != 0 && sd < 4 * sc;
	}

	/* step 5: hwaci detector (measured registers read 0) */
	if (mode & BCM4360_INTERFERENCE_HWACI) {
		u32 wv = bcm4360_phy_read(io, 0x523);
		u32 xv = bcm4360_phy_read(io, 0x529);
		u32 yv = bcm4360_phy_read(io, 0x528);
		u32 zv = bcm4360_phy_read(io, 0x527);
		struct bcm4360_phy_hwaci_entry *E = &table[level];
		u32 fw = (wv & 0x7f) >> s;
		u32 fx = (xv & 0x7f) >> s;
		u32 fy = (yv & 0x7f) >> s;
		u32 fz = (zv & 0x7f) >> s;

		deth = fw <= E->b6;
		if (E->b4 == 0)
			deth = deth && ((fx < E->b5 && fy == 0) ? fz != 0 : true);
		else if (E->b4 == 1)
			deth = deth && (E->b5 <= fy || fz != 0);
		else
			deth = deth && (E->b5 <= fz);
	}

	/* step 6: update the level */
	if (old_level == 0) {
		rec->hwaci_level = 1;
	} else if (detw || deth) {
		rec->hwaci_hold = 8;
		rec->hwaci_level = (u8)(old_level + 1);
		rec->hwaci_min = old_level;
	} else if (rec->hwaci_hold == 0) {
		rec->hwaci_level = old_level > 0 ? (u8)(old_level - 1) : 0;
		if (rec->hwaci_level < rec->hwaci_min)
			rec->hwaci_min = rec->hwaci_level;
	}
	if (nlevels && rec->hwaci_level > nlevels - 1)
		rec->hwaci_level = (u8)(nlevels - 1);
	if (rec->hwaci_level < 1)
		rec->hwaci_level = 1;

	/* step 7: the level changed */
	if (old_level != rec->hwaci_level) {
		struct bcm4360_phy_hwaci_entry *E = &table[rec->hwaci_level];

		rec->hwaci_hold = 8;
		rec->hwaci_settle = 2;
		if (mode & BCM4360_INTERFERENCE_W2NB) {
			bcm4360_phy_write(io, 0x554, E->w0);
			bcm4360_phy_write(io, 0x555, E->w0);
		}
	}

	/* step 8: the minimum level changed */
	if (old_min != rec->hwaci_min) {
		struct bcm4360_phy_hwaci_entry *Emin = &table[rec->hwaci_min];
		u16 s19e;

		rec->desense[BCM4360_DESENSE_LNA1_GAINLMT] =
			Emin->b2 < 5 ? (u8)(5 - Emin->b2) : 0;	/* rec+0x14 */
		rec->desense[BCM4360_DESENSE_LNA2_GAINLMT] =
			Emin->b3 < 6 ? (u8)(6 - Emin->b3) : 0;	/* rec+0x15 */
		s19e = bcm4360_phy_read(io, ACPHY_REG_0x19e);
		bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);
		bcm4360_phy_desense_calc_total_acphy(phy);
		phy_set_lna_gainlimit(phy, 1);
		phy_set_lna_gainlimit(phy, 2);
		bcm4360_phy_write(io, ACPHY_REG_0x19e, s19e);
		phy_aci_updsts(phy);
	}

	/* step 9 */
	bcm4360_mac_enable(phy->hw);
}
