// SPDX-License-Identifier: ISC
/*
 * The PHY watchdog of the BCM4360 (wlc_phy_watchdog), called once a second by
 * the MAC, and the periodic pieces only it calls that were not in open/ yet:
 * the desense-aci engine (acphy-desense.md E3), the high-RSSI eLNA-bypass
 * engine (acphy-desense.md H1, H2), wlc_phy_stop_bt_toggle_acphy
 * (acphy-rxgain.md section 8) and the AC-PHY part of the watchdog behind the
 * function pointer pi+0x100 (acphy-attach.md, "Function pointer table"). The
 * noise-sample request is the receive group's (open/phy/phy_rx.c,
 * bcm4360_phy_noise_sample_request).
 *
 * Where the whole-session test (tools/re/hybrid.py) and the specification
 * disagree the code follows the test; docs/re/questions/phy-api-watchdog.md
 * lists those places.
 *
 * Written from docs/re/spec/acphy-api-watchdog.md (a "step" in the comments is
 * a step of its "Procedure") for chip 0x4360, AC-PHY (PHY type 11) revisions 0
 * and 1. The register notation PHY(a), SHM(o), D11(o) is docs/re/spec/access.md.
 */
#include <bcm4360/phy.h>
#include <bcm4360/phy_env.h>

/* step 3: the PHY register pairs the three overrides go to */
#define ACPHY_REG_0x45a		0x45a
#define ACPHY_REG_0x45c		0x45c
#define ACPHY_REG_0x44c		0x44c
#define ACPHY_REG_0x448		0x448
#define ACPHY_REG_0x446		0x446
#define ACPHY_REG_0x44a		0x44a

/* acphy-rxgain.md section 8: the front-end control BT/WLAN override */
#define ACPHY_REG_FEMCTRL_OVR	0x418
#define FEMCTRL_OVR_VAL		0x0001
#define FEMCTRL_OVR_EN		0x0002

/* acphy-attach.md, sub_09737d: the core the watchdog selects, bits 2..3 */
#define ACPHY_REG_0x520		0x520
#define ACPHY_0x520_CORE	0x000c
#define ACPHY_0x520_CORE_SHIFT	2

/* the statistics counters the MAC keeps in shared memory (step 5) */
#define SHM_WD_CNT_0x10e	0x10e
#define SHM_WD_CNT_0x10c	0x10c
#define SHM_WD_CNT_0x158	0x158

/* the hold flags, pi+0x19c, that gate the steps */
#define WD_HOLD_NO_STATS	0x206	/* step 4: scan, PLT or similar */
#define WD_HOLD_NO_NOISE	0x20e	/* step 10 */
#define WD_HOLD_NO_PERIODIC	0x20f	/* step 13 */

/* step 10: the reasons of the noise-sample requests (acphy-desense.md C4: 4) */
#define WD_NOISE_REASON		1
#define WD_NOISE_REASON_CRSMIN	4

/* step 11: a pending noise sample is forgotten after this many seconds */
#define WD_NOISE_STALE_S	6
/* step 13.2: the reason "periodic" of wlc_phy_cal_perical */
#define WD_CAL_PERIODIC		2

/* boardflags bits of wlc_phy_stop_bt_toggle_acphy (acphy-rxgain.md section 8) */
#define BOARDFLAGS_BT_TOGGLE	0x00400000	/* bit 22 */
#define BOARDFLAGS_BT_CHAIN1	0x00800000	/* bit 23: chain mask 1, else 2 */

/* acphy-desense.md, "Constants" and E3: the desense-aci engine */
#define DACI_RINGS		4	/* entries of the narrow and wide rings */
#define DACI_NARROW_TRIGGER	301
#define DACI_NARROW_LOW		100
#define DACI_NARROW_MAX		24
#define DACI_NARROW_RESET	100
#define DACI_WIDE_TRIGGER	601
#define DACI_WIDE_LOW		300
#define DACI_WIDE_MAX		48
#define DACI_WIDE_RESET		300
#define DACI_RSSI_CAP_BPHY	90
#define DACI_RSSI_CAP_OFDM	85

/* --------------------------------------------------------------- helpers */

/* the radio is tuned to a 2.4 GHz channel */
static bool wd_is_2g(const struct bcm4360_phy *phy)
{
	return (phy->radio_chanspec & BCM4360_CHANSPEC_BAND) == BCM4360_CHANSPEC_BAND_2G;
}

/*
 * acphy-init.md, H13 (wlc_phyreg_enter/exit): a nesting counter; the first
 * enter takes the ucode wake override, the last exit releases it.
 */
static void wd_reg_enter(struct bcm4360_phy *phy)
{
	if (phy->phyreg_count++ == 0)
		bcm4360_mac_wake_override_set(phy->hw);
}

static void wd_reg_exit(struct bcm4360_phy *phy)
{
	if (--phy->phyreg_count == 0)
		bcm4360_mac_wake_override_clear(phy->hw);
}

/* ------------------------------------------- acphy-desense.md E3 */

/* E3 step 3: the average of the two largest of the four ring entries */
static u32 daci_top2_avg(const u32 *ring)
{
	u32 a = 0, b = 0, i;

	for (i = 0; i < DACI_RINGS; i++) {
		if (ring[i] > a) {
			b = a;
			a = ring[i];
		} else if (ring[i] > b) {
			b = ring[i];
		}
	}
	return (a + b) / 2;
}

/* E3 step 2: the latest delta of a statistics history, sel = next ? next - 1 : 1 */
static u32 daci_latest(const struct bcm4360_phy_wd_hist *h)
{
	return h->ring[h->next ? h->next - 1 : 1];
}

/* a desense level and its hysteresis state (E3 steps 5 and 6) */
struct daci_path {
	u8 *lo;			/* lower bound */
	u8 *hi;			/* upper bound */
	u8 *dwell;		/* dwell counter */
	u32 trigger;		/* 301 / 601 */
	u32 low;		/* 100 / 300 */
	u32 max;		/* 24 / 48 */
};

/* E3 steps 5 and 6: the new level of one path from its ring average */
static u8 daci_step(const struct daci_path *p, u8 level, u32 avg)
{
	s32 lvl = level, lo = *p->lo, hi = *p->hi, n;
	bool reset = false;

	if (avg >= p->trigger) {
		n = lvl + 4;
		if (lvl < hi) {
			n = (hi + lvl) / 2;
			if (n < lvl + 1)
				n = lvl + 1;
		}
		lo = lvl;
		reset = true;
	} else {
		u32 thr = (lvl - lo == 1) ? 12 : (avg < p->low ? 4 : 8);

		if (*p->dwell > thr) {
			if (lo < lvl) {
				n = (lo + lvl) / 2;
				if (n > lvl - 1)
					n = lvl - 1;
				if (n < 0)
					n = 0;
			} else {
				n = lvl - 4;
				if (n < 0)
					n = 0;
			}
			reset = true;
		} else {
			n = lvl;
		}
	}
	if (n > (s32)p->max)
		n = p->max;

	*p->lo = (u8)lo;
	*p->hi = (u8)hi;
	if (reset)
		*p->dwell = 0;
	else if (*p->dwell < 0xff)
		(*p->dwell)++;
	return (u8)n;
}

/*
 * acphy-desense.md E3 (wlc_phy_desense_aci_engine_acphy): interference
 * mitigation by the glitch statistics; sets the OFDM and b-PHY desense of the
 * channel. The statistics are the watchdog's (step 5), read 0 in the model.
 */
static void wd_desense_aci_engine(struct bcm4360_phy *phy)
{
	const struct bcm4360_phy_wd_hist *h = phy->wd_stats.hist;
	struct bcm4360_phy_interf_record *rec;
	struct daci_path narrow, wide;
	u32 m_narrow, m_wide, avg_n, avg_w, i;
	u8 bphy, ofdm;
	bool changed = false;

	/* step 1 */
	rec = bcm4360_phy_desense_chan_record(phy);
	if (rec->settle != 0) {
		rec->settle--;
		return;
	}

	/* step 2: histories 1 (narrow), 4 (narrow, x2), 2 (wide), 3 (wide, x2) */
	m_narrow = daci_latest(&h[1]) + 2 * daci_latest(&h[4]);
	m_wide = daci_latest(&h[2]) + 2 * daci_latest(&h[3]);
	rec->narrow_ring[rec->ring_idx] = m_narrow;
	rec->wide_ring[rec->ring_idx] = m_wide;
	rec->ring_idx = (u8)((rec->ring_idx + 1) % DACI_RINGS);

	/* steps 3, 4 */
	avg_n = daci_top2_avg(rec->narrow_ring);
	avg_w = daci_top2_avg(rec->wide_ring);
	if (avg_n < DACI_NARROW_TRIGGER && rec->desense[BCM4360_DESENSE_ON] == 0 &&
	    avg_w < DACI_WIDE_TRIGGER)
		return;

	/* steps 5, 6 */
	narrow = (struct daci_path){ &rec->narrow_lo, &rec->narrow_hi, &rec->narrow_dwell,
				     DACI_NARROW_TRIGGER, DACI_NARROW_LOW, DACI_NARROW_MAX };
	wide = (struct daci_path){ &rec->wide_lo, &rec->wide_hi, &rec->wide_dwell,
				   DACI_WIDE_TRIGGER, DACI_WIDE_LOW, DACI_WIDE_MAX };
	bphy = daci_step(&narrow, rec->desense[BCM4360_DESENSE_BPHY], avg_n);
	ofdm = daci_step(&wide, rec->desense[BCM4360_DESENSE_OFDM], avg_w);

	/* step 7: the RSSI cap */
	if (phy->acphy_0x671) {
		s32 cb = rec->rssi + DACI_RSSI_CAP_BPHY;
		s32 co = rec->rssi + DACI_RSSI_CAP_OFDM;

		if (cb < 0)
			cb = 0;
		if (co < 0)
			co = 0;
		if (bphy > cb)
			bphy = (u8)cb;
		if (ofdm > co)
			ofdm = (u8)co;
	}

	/* step 8 */
	if (bphy != rec->desense[BCM4360_DESENSE_BPHY]) {
		rec->desense[BCM4360_DESENSE_BPHY] = bphy;
		for (i = 0; i < DACI_RINGS; i++)
			rec->narrow_ring[i] = DACI_NARROW_RESET;
		changed = true;
	}
	if (ofdm != rec->desense[BCM4360_DESENSE_OFDM]) {
		rec->desense[BCM4360_DESENSE_OFDM] = ofdm;
		for (i = 0; i < DACI_RINGS; i++)
			rec->wide_ring[i] = DACI_WIDE_RESET;
		changed = true;
	}

	/* step 9 */
	rec->desense[BCM4360_DESENSE_ON] =
		hw_memcmp(rec->desense, phy->desense.base, BCM4360_DESENSE_BYTES) != 0;

	/* step 10 */
	if (changed) {
		bcm4360_phy_desense_apply_acphy(phy, 1);
		rec->settle = 1;
	}
}

/* ------------------------------------------- acphy-desense.md H1, H2 */

/*
 * acphy-desense.md H2 (wlc_phy_hirssi_elnabypass_apply_acphy): apply the
 * changed eLNA bypass to the desense and the receive gain tables.
 */
static void wd_hirssi_apply(struct bcm4360_phy *phy)
{
	bool is2g = wd_is_2g(phy);

	if (!phy->clk)
		return;
	bcm4360_mac_suspend(phy->hw);
	bcm4360_phy_desense_aci_reset_params_acphy(phy, false, is2g, !is2g);	/* step 1 */
	bcm4360_phy_desense_calc_total_acphy(phy);				/* step 2 */
	bcm4360_phy_desense_apply_acphy(phy, 0);				/* step 3 */
	bcm4360_phy_rxgainctrl_set_gaintbls_acphy(phy, true, true, true);	/* step 4 */
	bcm4360_phy_rxgainctrl_set_init_clip_gain_acphy(phy);
	bcm4360_phy_resetcca(phy);						/* step 5 */
	bcm4360_phy_force_rfseq(phy, 2);
	bcm4360_mac_enable(phy->hw);
}

/*
 * acphy-desense.md H1 (wlc_phy_hirssi_elnabypass_engine): count down the
 * bypass of the external LNA and start or end it on the microcode's
 * high-RSSI event.
 */
static void wd_hirssi_engine(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_hirssi *hr = &phy->hirssi;
	bool is2g = wd_is_2g(phy);
	s16 *cur = is2g ? &hr->timer_2g : &hr->timer_5g;
	s16 *other = is2g ? &hr->timer_5g : &hr->timer_2g;
	u8 en = is2g ? hr->state_2g : hr->state_5g;
	s16 rem = *cur;
	bool apply;

	/* step 1 */
	if (*other > -1)
		(*other)--;

	/* step 2 */
	if (!en)
		return;

	/* step 3 */
	if (rem < 0) {
		apply = bcm4360_phy_hirssi_shmem_read_clear(phy);
		if (apply)
			rem = (s16)hr->acphy_0x908;
	} else {
		rem--;
		apply = rem < 0;
		if (apply && bcm4360_phy_hirssi_shmem_read_clear(phy))
			rem = (s16)hr->acphy_0x908;
	}

	/* steps 4, 5 */
	*cur = rem;
	if (apply) {
		bcm4360_phy_hirssi_set_ucode_params(phy);
		wd_hirssi_apply(phy);
	}
}

/* ------------------------------------------- acphy-rxgain.md section 8 */

/*
 * acphy-rxgain.md, section 8 (wlc_phy_set_femctrl_bt_wlan_ovrd_acphy): force
 * or release the BT/WLAN priority override of the front-end control.
 */
static void wd_femctrl_bt_wlan_ovrd(struct bcm4360_phy *phy, s8 v)
{
	struct bcm4360_phy_io *io = &phy->io;

	bcm4360_mac_suspend(phy->hw);
	if (v == 1) {
		bcm4360_phy_mod(io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_VAL, FEMCTRL_OVR_VAL);
		bcm4360_phy_mod(io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_EN, FEMCTRL_OVR_EN);
	} else if (v == 0) {
		bcm4360_phy_mod(io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_VAL, 0);
		bcm4360_phy_mod(io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_EN, FEMCTRL_OVR_EN);
	} else {
		bcm4360_phy_mod(io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_EN, 0);
		bcm4360_phy_mod(io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_VAL, 0);
	}
	bcm4360_mac_enable(phy->hw);
}

/*
 * acphy-rxgain.md, section 8 (wlc_phy_stop_bt_toggle_acphy): force the
 * override on or release it, with boardflags bit 22 and no override set by
 * the user (pi_ac+0x32e = -1).
 */
static void wd_stop_bt_toggle(struct bcm4360_phy *phy)
{
	u32 bf = phy->board.boardflags;
	u8 m;
	s8 v;

	if (!(bf & BOARDFLAGS_BT_TOGGLE) || phy->femctrl_ovrd != -1)
		return;
	m = (bf & BOARDFLAGS_BT_CHAIN1) ? 1 : 2;			/* step 1 */
	wd_reg_enter(phy);						/* step 2 */
	v = ((m & phy->txchain) || (m & phy->rxchain)) ? -1 : 1;	/* step 3 */
	wd_femctrl_bt_wlan_ovrd(phy, v);
	wd_reg_exit(phy);						/* step 4 */
}

/* ------------------------------------------- the watchdog */

/*
 * acphy-attach.md, "Function pointer table", slot 0x100 (sub_09737d,
 * wlc_phy_watchdog_acphy): request the CRS min-power noise sample and step the
 * core selected in PHY(0x520).
 */
static void wd_watchdog_acphy(struct bcm4360_phy *phy)
{
	u32 n = phy->ver.cores;

	/*
	 * wlc_phy_noise_sample_request_crsmincal (acphy-desense.md C4): the
	 * common request sub_0baff6 with reason 4, which does nothing while a
	 * sample is pending (docs/re/questions/phy-api-watchdog.md, Q3)
	 */
	if (phy->crsmincal.enable)
		bcm4360_phy_noise_sample_request(phy, WD_NOISE_REASON_CRSMIN,
						 phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL);
	bcm4360_mac_suspend(phy->hw);
	bcm4360_phy_mod(&phy->io, ACPHY_REG_0x520, ACPHY_0x520_CORE,
			(u16)(phy->watchdog_core << ACPHY_0x520_CORE_SHIFT));
	phy->watchdog_core = n ? (u8)((phy->watchdog_core + 1) % n) : 0;
	bcm4360_mac_enable(phy->hw);
}

/* step 3: an override value, if set, into its pair of PHY registers */
static void wd_reg_override(struct bcm4360_phy *phy, u16 val, u16 a, u16 b)
{
	if (!val)
		return;
	bcm4360_phy_write(&phy->io, a, val);
	bcm4360_phy_write(&phy->io, b, val);
}

/* step 5: push a non-negative per-tick delta into a two-deep history */
static void wd_hist_push(struct bcm4360_phy_wd_hist *h, s32 delta)
{
	if (delta < 0)
		return;
	h->sum -= h->ring[h->next];
	h->ring[h->next] = (u16)delta;
	h->sum += (u16)delta;
	h->next = (u8)((h->next + 1) % BCM4360_PHY_WD_HIST_DEPTH);
	h->avg = (u16)(h->sum >> 1);
}

/*
 * step 5: the glitch / bad-PLCP statistics. Three SHM counters, their deltas
 * since the last tick, folded by band into the four per-tick deltas. No
 * hardware access other than the three reads.
 */
static void wd_statistics(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_wd_stats *st = &phy->wd_stats;
	struct bcm4360_phy_wd_hist *h = st->hist;
	u16 v;
	s32 d0, d1, d2;

	v = bcm4360_shm_read(phy->hw, SHM_WD_CNT_0x10e);
	d0 = (s32)v - st->shm_0x10e;
	st->shm_0x10e = v;
	v = bcm4360_shm_read(phy->hw, SHM_WD_CNT_0x10c);
	d1 = (s32)v - st->shm_0x10c;
	st->shm_0x10c = v;
	v = bcm4360_shm_read(phy->hw, SHM_WD_CNT_0x158);
	d2 = (s32)v - st->shm_0x158;
	st->shm_0x158 = v;

	wd_hist_push(&h[0], d0);
	if (wd_is_2g(phy)) {
		/* 2.4 GHz: the second and third counters folded into the first and fourth */
		wd_hist_push(&h[1], d0 + d1);
		wd_hist_push(&h[2], 0);
		wd_hist_push(&h[3], 0);
		wd_hist_push(&h[4], d2);
	} else {
		wd_hist_push(&h[1], d0);
		wd_hist_push(&h[2], d1);
		wd_hist_push(&h[3], d2);
		wd_hist_push(&h[4], 0);
	}
}

/* step 6: the ACI/noise engines of the AC-PHY by the interference mode */
static void wd_aci_engines(struct bcm4360_phy *phy)
{
	u32 mode = phy->interference.mode;

	if (mode & BCM4360_INTERFERENCE_DESENSE)
		wd_desense_aci_engine(phy);
	if (mode & (BCM4360_INTERFERENCE_HWACI | BCM4360_INTERFERENCE_W2NB))
		bcm4360_phy_hwaci_engine_acphy(phy);
}

/* step 12: the transmit-power recalculation tick */
static void wd_txpower_tick(struct bcm4360_phy *phy)
{
	if (phy->txpwr_tick_time != 0 &&
	    phy->now - phy->txpwr_tick_time < phy->fast_timer)
		return;
	if (phy->hold & BCM4360_PHY_HOLD_SCAN)
		return;
	if (bcm4360_phy_txpower_recalc_tick(phy))
		phy->txpwr_tick_time = phy->now;
}

/* step 13.2: the periodic calibration trigger (acphy-cal-tx.md section 11) */
static void wd_periodic_cal(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_cal_state *cs = &phy->cal.state;

	if (phy->pi_0x199 || phy->cal.mode == 0 || phy->cal.mode == 3 || cs->throttle)
		return;
	if (phy->now - cs->last_cal_time < phy->glacial_timer)
		return;
	bcm4360_phy_cal_perical(phy, WD_CAL_PERIODIC);
}

/*
 * step 13.5, the callback pi+0xf8 (wlc_phy_btc_adjust_acphy, acphy-attach.md,
 * "Function pointer table"): no specification describes its body, so nothing
 * is done here (see docs/re/questions/phy-api-watchdog.md).
 */
static void wd_btc_adjust(struct bcm4360_phy *phy, u8 state)
{
	(void)phy;
	(void)state;
}

/* step 13.5: the Bluetooth coexistence period and state */
static void wd_btc(struct bcm4360_phy *phy)
{
	u16 period = 0;
	u8 state = 0;

	if (!bcm4360_mac_btc_mode(phy->hw))
		return;
	bcm4360_mac_btc_period_get(phy->hw, &period, &state);
	if (state != phy->bt_active)
		wd_btc_adjust(phy, state);
	phy->bt_period = period;
	phy->bt_active = state;
}

/* steps 13.1 to 13.6: none of the hold flags 0x20f is set */
static void wd_periodic(struct bcm4360_phy *phy)
{
	if (phy->hirssi.supported)				/* 13.1 */
		wd_hirssi_engine(phy);
	wd_periodic_cal(phy);					/* 13.2 */
	if (phy->cal.state.throttle)				/* 13.3 */
		phy->cal.state.throttle--;
	wd_watchdog_acphy(phy);					/* 13.4 */
	wd_btc(phy);						/* 13.5 */
	wd_stop_bt_toggle(phy);					/* 13.6 */
}

/* acphy-api-watchdog.md, "Procedure" (wlc_phy_watchdog) */
void bcm4360_phy_watchdog(struct bcm4360_phy *phy)
{
	phy->now++;						/* step 1 */
	if (!phy->watchdog_override)				/* step 2 */
		return;

	/* step 3 */
	wd_reg_override(phy, phy->wd_ovr_0x1136, ACPHY_REG_0x45a, ACPHY_REG_0x45c);
	wd_reg_override(phy, phy->wd_ovr_0x113a, ACPHY_REG_0x44c, ACPHY_REG_0x448);
	wd_reg_override(phy, phy->wd_ovr_0x1138, ACPHY_REG_0x446, ACPHY_REG_0x44a);

	/* steps 4 to 8 */
	if (!(phy->hold & WD_HOLD_NO_STATS)) {
		wd_statistics(phy);
		wd_aci_engines(phy);
	}

	/* step 9 */
	if (phy->fast_timer && phy->now % phy->fast_timer == 0) {
		wd_reg_enter(phy);
		bcm4360_mac_update_bt_chanspec(phy->hw, phy->radio_chanspec,
					       (phy->hold & BCM4360_PHY_HOLD_SCAN) != 0,
					       (phy->hold & BCM4360_PHY_HOLD_BIT2) != 0);
		wd_reg_exit(phy);
	}

	/* step 10 */
	if (!(phy->hold & WD_HOLD_NO_NOISE))
		bcm4360_phy_noise_sample_request(phy, WD_NOISE_REASON,
						 phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL);

	/* step 11 */
	if (phy->noise_reason && phy->now - phy->noise_time > WD_NOISE_STALE_S)
		phy->noise_reason = 0;

	wd_txpower_tick(phy);					/* step 12 */

	if (!(phy->hold & WD_HOLD_NO_PERIODIC))			/* step 13 */
		wd_periodic(phy);
}
