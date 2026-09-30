// SPDX-License-Identifier: ISC
/*
 * AC-PHY of the BCM4360: setting the channel. wlc_phy_chanspec_set and the
 * channel function sub_0a7089 (wlc_phy_chanspec_set_acphy), with its small
 * helpers (sub-band of a channel, receive gain error, gain encoding).
 *
 * Written from docs/re/spec/acphy-chanspec.md (a "section" in the comments is
 * a section of its "Procedures"; the annex A1..A7 describe the leaf functions
 * that are stubbed in phy_todo.c) and, for the register access, from
 * docs/re/spec/access.md; for chip 0x4360, AC-PHY revision 0 or 1 and radio
 * 2069 revision 3 or 4. The radio is tuned by the finished functions of
 * open/phy/radio2069.c; the front end, receive gain/desense and transmit
 * power leaves belong to later tasks.
 */
#include <bcm4360/phy.h>
#include <bcm4360/phy_env.h>

/* PHY registers accessed here, named by their address (purpose unknown) */
#define ACPHY_REG_0x003		0x003	/* bit 8: band select (5 GHz) */
#define ACPHY_REG_0x19e		0x19e	/* bits set around the radio tuning */
#define ACPHY_REG_0x641		0x641	/* TSSI visible threshold (written to all cores) */

/* section 5, step 12 / acphy-radio.md, section 11, step 1: the PLL reset pulse */
#define ACPHY_0x728_PLL_RESET	0x0100

/* section 5, step 32: the target gain of the signal-strength correction, in dB */
#define RXGAIN_CORR_TARGET	69

/* section 5, step 28.4: the TSSI visible threshold register value */
#define TSSI_THRESH_BASE	0x7f00

/* the number of cores N of the PHY (pi+0x168) */
static u32 phy_cores(const struct bcm4360_phy *phy)
{
	return phy->ver.cores;
}

/* ------------------------------------------------ section 6, 7, 8, 9, 10 */

/*
 * acphy-chanspec.md, section 6 (wlc_phy_get_chan_freq_range_acphy): the
 * sub-band of a channel (0 = 2.4 GHz, 1..4 in 5 GHz). Channel 0 = the current
 * channel. No hardware access.
 */
u8 bcm4360_phy_get_chan_freq_range(struct bcm4360_phy *phy, u8 channel)
{
	u16 f;

	if (channel == 0)
		channel = phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL;
	bcm4360_radio_chan_entry(&phy->radio, channel, &f);

	if (channel <= 14)
		return 0;

	switch (phy->subband5gver) {
	case 4:
		if ((u16)(f - 5170) <= 79)
			return 1;
		if ((u16)(f - 5250) <= 249)
			return 2;
		if ((u16)(f - 5500) <= 244)
			return 3;
		return 4;
	case 0:
		if ((u16)(f - 5170) <= 329)
			return 1;
		if ((u16)(f - 5500) <= 244)
			return 2;
		return 3;
	case 1:
		if ((u16)(f - 5170) <= 79)
			return 1;
		if ((u16)(f - 5250) <= 494)
			return 2;
		return 3;
	default:
		if ((u16)(f - 4900) <= 199)
			return 1;
		if ((u16)(f - 5100) <= 399)
			return 2;
		return 3;
	}
}

/*
 * acphy-chanspec.md, section 7 (wlc_phy_chanspec_bandrange_get), AC-PHY path:
 * the sub-band of the chanspec's channel as a 32 bit value.
 */
u32 bcm4360_phy_chanspec_bandrange_get(struct bcm4360_phy *phy, u16 chanspec)
{
	return bcm4360_phy_get_chan_freq_range(phy, chanspec & BCM4360_CHANSPEC_CHANNEL);
}

/*
 * acphy-chanspec.md, section 8 (wlc_phy_get_rxgainerr_phy), AC-PHY path: the
 * receive gain error per core for the channel group (by channel number, not
 * the sub-band of section 6). Result: 1 if the SROM has no values.
 */
u8 bcm4360_phy_get_rxgainerr(struct bcm4360_phy *phy, s16 *err)
{
	u8 ch = phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL;
	u32 g, c;

	if (ch < 15)
		g = 0;
	else if (ch < 49)
		g = 1;
	else if (ch < 65)
		g = 2;
	else if (ch < 129)
		g = 3;
	else
		g = 4;

	for (c = 0; c < phy_cores(phy); c++)
		err[c] = phy->rxgainerr[g].err[c];
	return phy->rxgainerr[g].empty ? 1 : 0;
}

/*
 * acphy-chanspec.md, section 9 (sub_08f086,
 * wlc_phy_rxgainctrl_encode_gain_acphy): split a wanted receive gain into the
 * codes of the six gain stages of a core. Result: the gain reached in dB. No
 * hardware access. The stage tables are filled by the receive gain code
 * (acphy-rxgain), a leaf here, so they are zero until that task is done.
 */
u8 bcm4360_phy_rxgainctrl_encode_gain(struct bcm4360_phy *phy, u8 core, u8 wanted,
				      bool include_tr, u8 *codes)
{
	u8 tr = include_tr ? phy->rxgain_trloss[core] : 0;
	u16 need = (u16)(tr + wanted);
	s16 total = 0;
	s16 min[BCM4360_PHY_RXGAIN_STAGES];
	u8 limit[BCM4360_PHY_RXGAIN_STAGES];
	u32 s;
	int k;

	for (s = 0; s < BCM4360_PHY_RXGAIN_STAGES; s++) {
		limit[s] = (u8)(phy->rxgain_max[s] + tr);
		min[s] = phy->rxgain_gain[core][s][0];
	}

	for (s = 0; s < BCM4360_PHY_RXGAIN_STAGES; s++) {
		s16 rest = 0;
		s8 room;
		u32 j;

		if (s == 4) {
			if (need % 3 == 2)
				need += 1;
			if (need > 30)
				need = 30;
		}
		for (j = s + 1; j < BCM4360_PHY_RXGAIN_STAGES; j++)
			rest += min[j];
		room = (s8)((s16)need - rest);

		for (k = (int)phy->rxgain_stage_entries[s] - 1; k >= 0; k--) {
			s8 g = phy->rxgain_gain[core][s][k];
			u8 code = phy->rxgain_code[core][s][k];

			if (code == phy->rxgain_code[core][s][0] ||
			    (g <= room && (s16)(total + g) <= (s16)limit[s])) {
				codes[s] = code;
				total = (s16)(total + g);
				need = (u16)(need - g);
				break;
			}
		}
	}

	return (u8)(total - tr);
}

/*
 * acphy-chanspec.md, section 10 (sub_0995ab,
 * wlc_phy_btc_txpwr_core_offset_acphy): does nothing on chip 0x4360 (only
 * chip 0x4352 applies Bluetooth transmit power offsets).
 */
static void phy_btc_txpwr_core_offset(struct bcm4360_phy *phy, bool bt_active)
{
	if (phy->board.chip != BCM4360_CHIP_ID_4352)
		return;
	/* chip 0x4352: not in the scope of this code */
	(void)bt_active;
}

/*
 * acphy-chanspec.md, section 5, "Value of t in step 28"
 * (wlc_phy_tssivisible_thresh_acphy): the TSSI visible threshold, decided by
 * the board. No hardware access here; only the value is reproduced (the
 * function's own accesses belong to acphy-txpower).
 */
static u8 phy_tssivisible_thresh(struct bcm4360_phy *phy, u8 ch)
{
	const struct bcm4360_phy_board *b = &phy->board;

	if (phy->femctrl == 3 && phy->flags.bf3_bits0_2 == 2)
		return 0x26;
	if (b->chip == BCM4360_CHIP_ID_4360 &&
	    (b->boardtype == BCM4360_BOARDTYPE_0x137 || b->boardtype == BCM4360_BOARDTYPE_0x117))
		return 0x14;
	if (b->chip == BCM4360_CHIP_ID_4360 &&
	    (b->boardtype == 0x134 || b->boardtype == 0x112) && ch > 148)
		return 0x16;
	return 0x18;
}

/* ------------------------------------------------ section 5: the channel function */

/* section 5, step 5: start the high-RSSI timer of the band that is being left */
static void phy_chan_hirssi_leave(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_hirssi *h = &phy->hirssi;

	if (!(h->supported && bcm4360_phy_hirssi_shmem_read_clear(phy)))
		return;
	if (phy->band_2g_last != 0 && h->state_2g != 0)
		h->timer_2g = (s16)h->acphy_0x908;
	if (phy->band_2g_last == 0 && h->state_5g != 0)
		h->timer_5g = (s16)h->acphy_0x908;
}

/* section 5, step 32: the signal-strength correction per core */
static void phy_chan_rssi_gain_corr(struct bcm4360_phy *phy)
{
	s16 err[BCM4360_PHY_CORES_MAX];
	u8 codes[BCM4360_PHY_RXGAIN_STAGES];
	u32 c;

	if (bcm4360_phy_get_rxgainerr(phy, err) != 0) {
		for (c = 0; c < phy_cores(phy); c++)
			phy->rssi_gain_corr[c] = 0;
		return;
	}
	for (c = 0; c < phy_cores(phy); c++) {
		u8 x = bcm4360_phy_rxgainctrl_encode_gain(phy, c, RXGAIN_CORR_TARGET,
							  false, codes);

		phy->rssi_gain_corr[c] = (s8)(err[c] + 2 * (RXGAIN_CORR_TARGET - x));
	}
}

/*
 * acphy-chanspec.md, section 5 (sub_0a7089, wlc_phy_chanspec_set_acphy): tune
 * radio and PHY to a chanspec. INIT (pi_ac+0x32c) marks the call made by the
 * initialisation; band and bandwidth changes select what is done.
 */
void bcm4360_phy_chanspec_set_acphy(struct bcm4360_phy *phy, u16 chanspec)
{
	struct bcm4360_phy_io *io = &phy->io;
	bool init = phy->init_chan;
	u8 ch = chanspec & BCM4360_CHANSPEC_CHANNEL;
	bool is2g = (chanspec & BCM4360_CHANSPEC_BAND) == BCM4360_CHANSPEC_BAND_2G;
	u16 bw = chanspec & BCM4360_CHANSPEC_BW;
	bool bandchg, bwchg;
	const u16 *entry;
	u16 s1, s2, v;
	u16 freq;
	u32 c;

	/* step 2: look up the channel; nothing happens for an unknown channel */
	entry = bcm4360_radio_chan_entry(&phy->radio, ch, &freq);
	if (!entry)
		return;

	bandchg = init || (is2g != (phy->band_2g_last != 0));
	bwchg = init || (bw != phy->bw_last);

	/* step 5 */
	if (bandchg) {
		phy_chan_hirssi_leave(phy);
		phy->band_2g_last = is2g;
	}

	/* step 6 */
	s1 = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	s2 = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0001, 0x0001);

	/* step 7 */
	if (bwchg) {
		phy->bw_last = bw;
		if (!init) {
			phy->bw = bw;			/* wlapi_bmac_bw_set keeps pi+0x182 */
			bcm4360_mac_bw_set(phy->hw, bw);
		}
		hw_udelay(2);
	}

	/* step 8: band select */
	bcm4360_phy_mod(io, ACPHY_REG_0x003, 0x0100, is2g ? 0 : 0x0100);

	/* step 9: enter the carrier search */
	phy->csearch_count = 0;
	bcm4360_phy_stay_in_carriersearch(phy, true);

	/* step 11 */
	phy->radio_chanspec = chanspec;

	/* step 12: radio tuning (acphy-radio.md, section 11) */
	if (bandchg || bwchg) {
		bcm4360_phy_mod(io, ACPHY_REG_0x728, ACPHY_0x728_PLL_RESET, ACPHY_0x728_PLL_RESET);
		hw_udelay(1);
		bcm4360_phy_mod(io, ACPHY_REG_0x728, ACPHY_0x728_PLL_RESET, 0);
	}
	bcm4360_radio_tune(&phy->radio, entry, ch, !is2g);

	/* step 14: restore PHY(0x19e) */
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s1 & 0x0002);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0001, s2 & 0x0001);

	/* step 15 */
	if (init) {
		bcm4360_phy_set_reg_on_reset_acphy(phy);
		bcm4360_phy_set_regtbl_femctrl_acphy(phy);
	}
	/* step 16 */
	if (bandchg)
		bcm4360_phy_set_regtbl_on_band_change_acphy(phy);
	/* step 17 */
	if (bwchg)
		bcm4360_phy_set_regtbl_on_bw_change_acphy(phy);
	/* step 18 */
	if (phy->hirssi.supported && (bandchg || bwchg))
		bcm4360_phy_hirssi_set_ucode_params(phy);

	/* step 19 */
	bcm4360_phy_set_regtbl_on_chan_change_acphy(phy, entry);

	/* step 20: interference (desense) state of the channel */
	if ((phy->hold & 0x0206) && phy->interference.channel != ch) {
		/* the channel is only visited during a scan: nothing more */
	} else {
		phy->interference.channel = ch;
		(void)bcm4360_phy_desense_getset_chanidx_acphy(phy, chanspec, true);
	}

	/* steps 21..24 */
	bcm4360_phy_desense_calc_total_acphy(phy);
	bcm4360_phy_rxgainctrl_set_gaintbls_acphy(phy, init, bandchg, bwchg);
	bcm4360_phy_rxgainctrl_set_init_clip_gain_acphy(phy);
	bcm4360_phy_desense_apply_acphy(phy, 0);

	/* step 25: converter calibration */
	if (bwchg || init) {
		bcm4360_phy_resetcca(phy);
		hw_udelay(1);
		bcm4360_radio_afecal(&phy->radio);
	}

	/* step 27: transmit gain of the last index (leaf) */
	for (c = 0; c < phy_cores(phy); c++)
		bcm4360_phy_txpwr_by_index_acphy(phy, (u8)(1u << c), phy->txpwr.index[c]);

	/* step 28: transmit power control and the TSSI visible threshold */
	{
		bool saved = phy->txpwr.hwpwrctrl;
		u8 t;

		bcm4360_phy_txpwrctrl_enable_acphy(phy, false);
		t = phy_tssivisible_thresh(phy, ch);
		bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x641, TSSI_THRESH_BASE + t);
		phy_btc_txpwr_core_offset(phy, false);
		bcm4360_phy_txpwrctrl_enable_acphy(phy, saved);
	}

	/* step 29: wait for the VCO calibration started in step 12 */
	bcm4360_radio_vcocal_wait(&phy->radio, false);

	/* step 30: the receive cores (INIT only) */
	if (init) {
		u32 n;
		u8 full;

		v = bcm4360_phy_read(io, ACPHY_REG_0x00b);
		n = v & 7;
		full = (u8)((1u << n) - 1);
		if (phy->rxchain != full || phy->hw_txchain != phy->rxchain)
			bcm4360_phy_rxcore_setstate(phy, phy->rxchain);
	}

	/* step 31 */
	bcm4360_phy_resetcca(phy);

	/* step 32 */
	phy_chan_rssi_gain_corr(phy);

	/* step 34: leave the carrier search */
	bcm4360_phy_stay_in_carriersearch(phy, false);
}

/*
 * acphy-chanspec.md, section 4 (wlc_phy_chanspec_set): the chanspec to shared
 * memory, the interference mode of the band, then the channel function.
 */
void bcm4360_phy_chanspec_set(struct bcm4360_phy *phy, u16 chanspec)
{
	struct bcm4360_phy_interference *in = &phy->interference;
	bool is2g = (chanspec & BCM4360_CHANSPEC_BAND) == BCM4360_CHANSPEC_BAND_2G;

	bcm4360_phy_chanspec_shm_set(phy, chanspec);

	if (in->forced)
		in->mode = is2g ? in->forced_2g : in->forced_5g;
	else
		in->mode = is2g ? in->mode_2g : in->mode_5g;

	bcm4360_phy_chanspec_set_acphy(phy, chanspec);

	bcm4360_mac_update_bt_chanspec(phy->hw, chanspec,
				       (phy->hold & BCM4360_PHY_HOLD_SCAN) != 0,
				       (phy->hold & BCM4360_PHY_HOLD_BIT2) != 0);
}
