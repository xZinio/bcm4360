// SPDX-License-Identifier: ISC
/*
 * The receive and measurement entry points of the PHY API: the signal strength
 * of a received frame, the noise average and the noise-sample path, the
 * interference-RSSI update, deaf mode, the receive chains and the antenna
 * diversity. Written from docs/re/spec/acphy-api-rx.md, with the AC bodies of
 * docs/re/spec/acphy-cal-rx.md (wlc_phy_rssi_compute_acphy,
 * wlc_phy_upd_gain_wrt_temp_phy, wlc_phy_stf_chain_temp_throttle_acphy) and
 * docs/re/spec/acphy-desense.md (C3, C4, E4), for chip 0x4360, AC-PHY (PHY
 * type 11) revisions 0 and 1.
 *
 * Where a specification is silent or the specifications disagree, the choice
 * made is listed in docs/re/questions/phy-api-rx.md. The register notation
 * PHY(a), SHM(o), D11(o) is docs/re/spec/access.md.
 */
#include <bcm4360/phy.h>
#include <bcm4360/phy_env.h>

/* ----------------------------------------------------------- constants */

/* the receive header `rxh` (acphy-api-rx.md, "The received-frame header") */
#define RXH_RSSI_CORE0		0x09	/* u8: raw power of core 0 */
#define RXH_RSSI_CORE12		0x0a	/* u16: low byte core 1, high byte core 2 */
#define RXH_RXSTATUS2		0x12	/* u16: received-frame type bits */
#define RXH_RSSI		0x1c	/* out, s8: the RSSI of the frame in dBm */
#define RXH_RSSI_INVALID	0x1f	/* out, u8: 0 = the RSSI is valid */
#define RXH_RSSI_PER_CORE	0x20	/* out, s8 per core (acphy-cal-rx.md, step 7) */

/*
 * The early-out of wlc_phy_rssi_compute: only for MAC core revisions above
 * 10; the bit of RXH_RXSTATUS2 that it tests is a guess (questions, Q1).
 */
#define RSSI_CORE_REV_MIN	11
#define RXH_RXSTATUS2_PHYST_OK	0x0100

/* the floor of a per-core RSSI (acphy-cal-rx.md, step 7) */
#define RSSI_FLOOR		(-128)

/* the combination of the cores, sh+0xa8 (acphy-api-rx.md, step 6) */
#define RSSI_MODE_MAX		0
#define RSSI_MODE_MIN		1
#define RSSI_MODE_AVG		2

/* wlc_phy_upd_gain_wrt_temp_phy (acphy-cal-rx.md) */
#define TEMP_CLAMP_MAX		0x69	/* the temperature is clamped to 0..105 */
#define TEMP_REF_NONE		0xff	/* rawtempsense: no reference */
#define TEMP_SLOPE_ACPHY	7
#define TEMP_ROUND		25
#define TEMP_DIV		50

/* the noise path (acphy-api-rx.md, "Noise", acphy-desense.md C3, C4) */
#define SHM_NOISE_HANDSHAKE	0x8c	/* read, value ignored */
#define SHM_NOISE_RESULT	0x308	/* per core c: low word 0x308 + 4c, high 0x30a + 4c */
#define SHM_NOISE_WORDS		(2 * BCM4360_PHY_CORES_MAX)	/* 0x308..0x312 */
#define NOISE_POWER_SHIFT	9
#define NOISE_FLOOR_DBM		(-92)	/* 0xa4 */
#define NOISE_HISTORY		16	/* entries of the per-core history (pi+0xed4, pi+0xef4) */
#define NOISE_REASON_CRSMIN	4	/* acphy-desense.md, "Data": the sample is for C1 */
#define NOISE_REASON_EXTERNAL	2	/* guessed (questions, Q6) */

/* D11(0x124), the MAC command register (bmac-init.md); bit 4 starts the noise measurement */
#define D11_REG_MACCMD		0x124
#define D11_MACCMD_NOISE	0x00000010

/* software antenna diversity (acphy-api-rx.md, "Deaf, chains, antenna diversity") */
#define D11_REG_0x49c		0x49c

/* wlc_phy_stf_chain_temp_throttle_acphy: the tx mask to throttle to, by tx mask */
static const u8 throttle_txmask[8] = { 1, 1, 2, 1, 4, 1, 2, 1 };

/* ----------------------------------------------------------- helpers */

/* the number of cores N of the PHY (pi+0x168), at most the cores the state holds */
static u32 phy_cores(const struct bcm4360_phy *phy)
{
	return min((u32)phy->ver.cores, (u32)BCM4360_PHY_CORES_MAX);
}

/* a 16 bit field of the receive header (little endian, as the MAC hands it over) */
static u16 rxh_u16(const u8 *h, u32 off)
{
	return (u16)(h[off] | (h[off + 1] << 8));
}

/* ------------------------------------------- the signal strength of a frame */

/*
 * acphy-cal-rx.md, "wlc_phy_upd_gain_wrt_temp_phy" (acphy-api-rx.md, the same
 * name): the gain correction for the drift of the temperature since the SROM
 * reference, AC-PHY path.
 */
static s32 rssi_temp_gain(const struct bcm4360_phy *phy)
{
	s32 t = phy->temp.measured_temp;		/* pi_ac+0x8dc */
	s32 d;

	if (t < 0)
		t = 0;
	if (t > TEMP_CLAMP_MAX)
		t = TEMP_CLAMP_MAX;
	if ((phy->temp.rawtempsense & 0xff) == TEMP_REF_NONE)
		d = 0;
	else
		d = t - phy->temp.rawtempsense;		/* pi+0x210 */
	return (d * 2 * TEMP_SLOPE_ACPHY + (d >= 0 ? TEMP_ROUND : -TEMP_ROUND)) / TEMP_DIV;
}

/* acphy-cal-rx.md, rssi_compute_acphy step 5: x / 4 rounded with the sign of x */
static s32 rssi_round_quarter(s32 x)
{
	if (x >= 0)
		return (x + 2) >> 2;
	return -((2 - x) >> 2);
}

/*
 * acphy-cal-rx.md, rssi_compute_acphy step 6 (acphy-api-rx.md step 5): the
 * correction of core c for the sub-band sb and the bandwidth of the channel:
 * rssicorrnorm_c<c> on 2.4 GHz, rssicorrnorm5g_c<c> (three per sub-band) on
 * 5 GHz.
 */
static s32 rssi_subband_corr(const struct bcm4360_phy *phy, u32 c, u8 sb)
{
	u16 bw = phy->radio_chanspec & BCM4360_CHANSPEC_BW;
	u32 k;

	if (bw == BCM4360_CHANSPEC_BW_80)
		k = 2;
	else if (bw == BCM4360_CHANSPEC_BW_40)
		k = 1;
	else
		k = 0;

	if (sb == BCM4360_PHY_BAND_2G)
		return phy->rssi.corrnorm_c[c][k == 1 ? 1 : 0];
	return phy->rssi.corrnorm5g_c[c][3 * (sb - BCM4360_PHY_BAND_5G) + k];
}

/*
 * acphy-api-rx.md step 6 (qm_div16 for the mode "sum"): the sum of the active
 * cores divided by their number, rounded to the nearest, halves away from zero
 * (the exact rounding of qm_div16 is not specified: questions, Q3).
 */
static s32 rssi_div_round(s32 num, s32 den)
{
	s32 a = num < 0 ? -num : num;
	s32 q = (2 * a + den) / (2 * den);

	return num < 0 ? -q : q;
}

/*
 * acphy-api-rx.md step 6, acphy-cal-rx.md step 9: combine the cores whose bit
 * is set in the receive-chain mask sh+0xa7 by the mode sh+0xa8.
 */
static s32 rssi_combine(const struct bcm4360_phy *phy, const s32 *rssi, u32 n)
{
	s32 sum = 0, best = 0;
	u32 c, count = 0;

	for (c = 0; c < n; c++) {
		if (!(phy->rxchain & BIT(c)))
			continue;
		if (count == 0)
			best = rssi[c];
		else if (phy->rssi.mode == RSSI_MODE_MIN)
			best = min(best, rssi[c]);
		else
			best = max(best, rssi[c]);
		sum += rssi[c];
		count++;
	}
	if (count == 0)
		return 0;
	if (phy->rssi.mode == RSSI_MODE_AVG)
		return rssi_div_round(sum, count);
	return best;
}

/*
 * acphy-cal-rx.md, "wlc_phy_rssi_compute_acphy" (acphy-api-rx.md, steps 1 to
 * 7): the RSSI of a received frame in dBm from the per-core power bytes of its
 * header. No hardware access. The CCK work-around of step 2 is for PHY
 * revisions 2, 5 and 6 only and is not reached on revisions 0 and 1.
 */
static s32 rssi_compute_acphy(struct bcm4360_phy *phy, u8 *h)
{
	s32 rssi[BCM4360_PHY_CORES_MAX] = { 0 };
	u32 n = phy_cores(phy), c;
	u16 w = rxh_u16(h, RXH_RSSI_CORE12);
	s32 g;
	u8 sb;

	/* step 1 */
	if (n > 0)
		rssi[0] = h[RXH_RSSI_CORE0];
	if (n > 1)
		rssi[1] = w & 0xff;
	if (n > 2)
		rssi[2] = w >> 8;

	/* step 3: signed 8 bit values */
	for (c = 0; c < n; c++)
		if (rssi[c] > 0x7f)
			rssi[c] -= 0x100;

	/* steps 4, 5: the temperature and gain correction of the non-zero cores */
	g = rssi_temp_gain(phy);
	for (c = 0; c < n; c++)
		if (rssi[c] != 0)
			rssi[c] -= rssi_round_quarter(phy->rssi_gain_corr[c] * 2 - g);

	/* step 6: the correction of the sub-band */
	sb = bcm4360_phy_get_chan_freq_range(phy, phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL);
	if (sb < BCM4360_PHY_BANDS)
		for (c = 0; c < n; c++)
			rssi[c] += rssi_subband_corr(phy, c, sb);

	/* step 7: the floor, the per-core values into the header */
	for (c = 0; c < n; c++) {
		if (rssi[c] < RSSI_FLOOR)
			rssi[c] = RSSI_FLOOR;
		h[RXH_RSSI_PER_CORE + c] = (u8)(s8)rssi[c];
	}

	/* step 8 */
	h[RXH_RSSI_INVALID] = 0;

	/* step 9 */
	return rssi_combine(phy, rssi, n);
}

/*
 * acphy-api-rx.md, "wlc_phy_rssi_compute (AC-PHY)": the RSSI of a received
 * frame into its header (rxh+0x1c), or the frame marked as without a valid
 * RSSI.
 */
void bcm4360_phy_rssi_compute(struct bcm4360_phy *phy, void *rxh)
{
	u8 *h = rxh;
	s32 rssi;

	if (phy->board.corerev >= RSSI_CORE_REV_MIN &&
	    !(rxh_u16(h, RXH_RXSTATUS2) & RXH_RXSTATUS2_PHYST_OK)) {
		h[RXH_RSSI] = 0;
		h[RXH_RSSI_INVALID] = 1;
		return;
	}

	rssi = rssi_compute_acphy(phy, h);
	h[RXH_RSSI] = (u8)(s8)rssi;
	h[RXH_RSSI_INVALID] = 0;
}

/* ------------------------------------------------------------ noise */

/*
 * acphy-api-rx.md, "wlc_phy_noise_read_shmem" (acphy-desense.md C3): read the
 * noise power of each core that the microcode measured, keep the per-core
 * values in the history, and return the largest. sub_0b701e, the write-back to
 * the shared memory that a non-zero measurement goes through first, is not
 * specified (questions, Q5); in the model every measurement is 0, so it is
 * not reached, nor is the carrier-sense calibration.
 */
static s8 noise_read_shmem(struct bcm4360_phy *phy)
{
	s16 dbm[BCM4360_PHY_CORES_MAX] = { 0 };
	s16 *hist[2] = { phy->bssinit_a, phy->bssinit_b };
	u32 n = phy_cores(phy), c, total = 0;
	s16 best = NOISE_FLOOR_DBM;

	for (c = 0; c < n; c++) {
		u16 lo = bcm4360_shm_read(phy->hw, (u16)(SHM_NOISE_RESULT + 4 * c));
		u16 hi = bcm4360_shm_read(phy->hw, (u16)(SHM_NOISE_RESULT + 4 * c + 2));
		u32 power = (u32)hi << 16 | lo;

		total += power;
		dbm[c] = power ? (s16)(power >> NOISE_POWER_SHIFT) : NOISE_FLOOR_DBM;
	}

	if (total != 0) {
		/* sub_0b701e(pi, power, dbm, 0): not specified (questions, Q5) */
		if (phy->noise_reason == NOISE_REASON_CRSMIN || phy->crsmincal.every_sample)
			bcm4360_phy_crs_min_pwr_cal_acphy(phy, 0);
	}

	/* the per-core history (pi+0xed4, pi+0xef4; index pi+0xf54) */
	for (c = 0; c < n && c < ARRAY_SIZE(hist); c++)
		hist[c][phy->bssinit_f54] = dbm[c];
	phy->bssinit_f54 = (u8)((phy->bssinit_f54 + 1) % NOISE_HISTORY);

	for (c = 0; c < n; c++)
		if (c == 0 || dbm[c] > best)
			best = dbm[c];

	/* the current slot of the noise window */
	phy->interference.noise_window[phy->interference.noise_index] = (u8)(s8)best;
	return (s8)best;
}

/* the next slot of the noise window sh+0x96 (sh+0xa0, modulo 8) */
static void noise_window_advance(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_interference *in = &phy->interference;

	in->noise_index = (in->noise_index + 1) % ARRAY_SIZE(in->noise_window);
}

/*
 * acphy-api-rx.md, "wlc_phy_noise_sample_acphy" (acphy-desense.md C3): one
 * noise sample into the noise window; -92 dBm while the clock is off.
 */
static s8 noise_sample_acphy(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_interference *in = &phy->interference;
	s8 v;

	if (!phy->clk)
		return NOISE_FLOOR_DBM;
	bcm4360_shm_read(phy->hw, SHM_NOISE_HANDSHAKE);
	v = noise_read_shmem(phy);
	in->noise_window[in->noise_index] = (u8)v;
	noise_window_advance(phy);
	return v;
}

/*
 * acphy-api-rx.md, "wlc_phy_noise_avg": the noise level in dBm - the answer of
 * the on-chip offload if there is one, else a fresh sample written into the
 * whole noise window and the average of its non-zero entries.
 */
s8 bcm4360_phy_noise_avg(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_interference *in = &phy->interference;
	s8 r = bcm4360_mac_ol_noise_avg(phy->hw);		/* step 1 */
	s32 sum = 0, count = 0;
	u32 i;
	s8 v;

	if (r != 0)
		return r;

	v = noise_sample_acphy(phy);				/* step 2 */
	for (i = 0; i < ARRAY_SIZE(in->noise_window); i++)
		in->noise_window[i] = (u8)v;

	for (i = 0; i < ARRAY_SIZE(in->noise_window); i++) {	/* step 3 */
		s8 b = (s8)in->noise_window[i];

		if (b != 0) {
			sum += b;
			count++;
		}
	}
	return (s8)(count ? sum / count : sum);
}

/*
 * acphy-api-rx.md, "sub_0baff6" (acphy-desense.md C4): the common noise-sample
 * request. Nothing while a sample is pending (pi+0x18f set); otherwise note
 * the reason and the time (pi+0x18f, pi+0x190), clear the per-core result
 * words SHM(0x308..0x312) and start the measurement with D11(0x124) bit 4.
 * The accesses are those the whole-session test shows, not the classifier set-up
 * acphy-api-rx.md names (docs/re/questions/phy-api-watchdog.md, Q3).
 */
void bcm4360_phy_noise_sample_request(struct bcm4360_phy *phy, u8 reason, u8 channel)
{
	u32 c, v;

	(void)channel;
	if (phy->noise_reason)
		return;
	phy->noise_reason = reason;
	phy->noise_time = phy->now;
	for (c = 0; c < SHM_NOISE_WORDS; c++)
		bcm4360_shm_write(phy->hw, (u16)(SHM_NOISE_RESULT + 2 * c), 0);
	v = d11_read32(phy->hw, D11_REG_MACCMD);
	d11_write32(phy->hw, D11_REG_MACCMD, v | D11_MACCMD_NOISE);
}

/*
 * acphy-api-rx.md, "wlc_phy_noise_sample_intr": the microcode has measured the
 * noise; read the result as wlc_phy_noise_read_shmem does, move on in the
 * noise window and complete the pending request.
 */
void bcm4360_phy_noise_sample_intr(struct bcm4360_phy *phy)
{
	noise_read_shmem(phy);
	noise_window_advance(phy);
	phy->noise_reason = 0;
}

/*
 * acphy-api-rx.md, "wlc_phy_noise_sample_request_external": a noise sample
 * that the MAC asks for, for the current channel.
 */
void bcm4360_phy_noise_sample_request_external(struct bcm4360_phy *phy)
{
	bcm4360_phy_noise_sample_request(phy, NOISE_REASON_EXTERNAL,
					 phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL);
}

/* ------------------------------------------- interference RSSI, deaf */

/*
 * acphy-api-rx.md, "wlc_phy_interf_rssi_update" -> acphy-desense.md E4
 * (wlc_phy_desense_aci_upd_chan_stats_acphy): the RSSI-based input of the
 * desense into the interference record of the channel, if it has one. No
 * hardware access.
 */
void bcm4360_phy_interf_rssi_update(struct bcm4360_phy *phy, u16 chanspec, s8 rssi)
{
	struct bcm4360_phy_interf_record *rec;

	rec = bcm4360_phy_desense_getset_chanidx_acphy(phy, chanspec, false);
	if (rec)
		rec->rssi = rssi;
}

/*
 * acphy-api-rx.md, "wlc_phy_set_deaf": always deaf on (acphy-init.md H9); the
 * argument of the MAC is ignored, as by the object.
 */
void bcm4360_phy_set_deaf(struct bcm4360_phy *phy, bool user)
{
	(void)user;
	bcm4360_phy_deaf(phy, true);
}

/* ---------------------------------------------------------- chains */

/*
 * acphy-api-rx.md, "wlc_phy_stf_chain_set", as acphy-init.md H7 states it: the
 * tx mask into sh+0xa6, then the rx mask into the hardware (H7, which also
 * drives the tx mask).
 */
void bcm4360_phy_stf_chain_set(struct bcm4360_phy *phy, u8 txchain, u8 rxchain)
{
	phy->txchain = txchain;
	bcm4360_phy_rxcore_setstate(phy, rxchain);
}

/*
 * acphy-cal-rx.md, "wlc_phy_stf_chain_temp_throttle_acphy": one chain only
 * while the chip is too hot, all again when it has cooled down; the result is
 * kept in the active-chain mask pi+0xc33 (rx mask in the high nibble).
 */
static void stf_chain_temp_throttle(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_temp *tp = &phy->temp;
	s32 t;
	u8 tx;

	bcm4360_mac_suspend(phy->hw);				/* step 1 */
	t = (s16)bcm4360_phy_tempsense(phy);
	bcm4360_mac_enable(phy->hw);

	if (!tp->heated) {					/* step 2 */
		if (t < tp->thresh)
			return;
		tp->heated = true;
		tx = throttle_txmask[phy->txchain & 7];
	} else {						/* step 3 */
		if (t > tp->thresh_on)
			return;
		tp->heated = false;
		tx = phy->hw_txchain;
	}
	tp->chain_bitmap = (u8)(phy->rxchain << 4 | tx);	/* step 4 */
}

/*
 * acphy-api-rx.md, "wlc_phy_stf_chain_active_get": the active-chain mask
 * pi+0xc33, after the temperature check when pi+0x18c is set.
 */
u8 bcm4360_phy_stf_chain_active_get(struct bcm4360_phy *phy)
{
	if (phy->watchdog_override)
		stf_chain_temp_throttle(phy);
	return phy->temp.chain_bitmap;
}

/* ------------------------------------------------ antenna diversity */

/* acphy-api-rx.md, "wlc_phy_ant_rxdiv_set": a store (sh+0x95) on the AC-PHY */
void bcm4360_phy_ant_rxdiv_set(struct bcm4360_phy *phy, u8 val)
{
	phy->rx_antdiv = val;
}

/* acphy-api-rx.md, "wlc_phy_ant_rxdiv_get" */
bool bcm4360_phy_ant_rxdiv_get(struct bcm4360_phy *phy, u8 *val)
{
	*val = phy->rx_antdiv;
	return true;
}

/*
 * acphy-api-rx.md, "wlc_phy_swdiv_ant_get": bit `bit` of D11(0x49c) (the path
 * of every chip but 0xa886/0xa887). Result 0 (questions, Q9).
 */
s32 bcm4360_phy_swdiv_ant_get(struct bcm4360_phy *phy, u8 bit, u8 *val)
{
	*val = (d11_read16(phy->hw, D11_REG_0x49c) >> bit) & 1;
	return 0;
}

/* acphy-api-rx.md, "wlc_phy_swdiv_ant_set": set or clear bit `bit` of D11(0x49c) */
s32 bcm4360_phy_swdiv_ant_set(struct bcm4360_phy *phy, u8 bit, bool on)
{
	u16 v = d11_read16(phy->hw, D11_REG_0x49c);

	if (on)
		v |= (u16)BIT(bit);
	else
		v &= (u16)~BIT(bit);
	d11_write16(phy->hw, D11_REG_0x49c, v);
	return 0;
}
