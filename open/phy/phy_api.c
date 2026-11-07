/*
 * The control entry points of the PHY API: the small stores and getters, the
 * channel list, the orchestration functions that are not transmit power,
 * receive or the watchdog. Written from docs/re/spec/acphy-api.md
 * (sections A1 to A15) for chip 0x4360, AC-PHY (PHY type 11) revisions 0 and 1.
 *
 * Each function names the acphy-api.md section it implements. The register
 * notation PHY(a), SHM(o), D11(o) is docs/re/spec/access.md.
 */
#include <bcm4360/phy.h>
#include <bcm4360/phy_env.h>

/* |x| for a 16-bit signed value */
static u16 abs16(s16 x)
{
	return x < 0 ? (u16)-x : (u16)x;
}

/* the PHY type the AC code is, D11_PHY_VERSION_TYPE (acphy-api.md A3) */
#define ACPHY_TYPE		11

/* A6: the front-end control override register and its bits */
#define ACPHY_REG_FEMCTRL_OVR	0x418
#define FEMCTRL_OVR_VAL		0x0001
#define FEMCTRL_OVR_EN		0x0002

/* A8: the value wlc_phy_BSSinit writes into the two 16-entry arrays */
#define BSSINIT_NOISE		((s16)-92)
/* A8: the interference code's mode-3 time bias */
#define BSSINIT_FLAGS_TIME_BIAS	8

/* ----------------------------------------------------- A2: second-band attach */

/*
 * acphy-api.md, A2 (wlc_phy_attach for the second band). On the 4360 one PHY
 * serves both bands: the second call resets the core with the PHY's core
 * flags and counts one more attach. Returns true if the PHY serves the band.
 */
bool bcm4360_phy_attach_band(struct bcm4360_phy *phy, u32 bandtype)
{
	u32 sflags = bcm4360_chip_core_sflags(phy->hw, 0, 0);

	if (bandtype == 1 && (sflags & (D11_SFLAGS_PHY_5G | D11_SFLAGS_PHY_BOTH)) == 0)
		return false;
	if (!(sflags & D11_SFLAGS_PHY_BOTH))
		return false;
	bcm4360_mac_corereset(phy->hw, phy->ver.coreflags);
	phy->refcnt++;
	return true;
}

/* ----------------------------------------------------- A3: stores and no-ops */

/* A3: wlc_phy_get_encore - 0 for the AC-PHY (only the A/G attach writes it) */
bool bcm4360_phy_get_encore(struct bcm4360_phy *phy)
{
	return phy->encore != 0;
}

/* A3: wlc_phy_preamble_override_get */
s8 bcm4360_phy_preamble_override_get(struct bcm4360_phy *phy)
{
	return phy->preamble_override;
}

/* A3: wlc_phy_preamble_override_set - a non-zero value is ignored by the AC-PHY */
void bcm4360_phy_preamble_override_set(struct bcm4360_phy *phy, s8 val)
{
	if (val == 0 || phy->ver.phy_type != ACPHY_TYPE)
		phy->preamble_override = val;
}

/* A3: wlc_phy_btclock_war - a store read only by the LCN-PHY */
void bcm4360_phy_btclock_war(struct bcm4360_phy *phy, bool on)
{
	phy->btclock_war = on;
}

/* A3: wlc_phy_ofdm_rateset_war - a store read only by the A/G-PHY */
void bcm4360_phy_ofdm_rateset_war(struct bcm4360_phy *phy, bool on)
{
	phy->ofdm_rateset_war = on;
}

/* A3: no effect on the AC-PHY (the body is for other PHY types) */
void bcm4360_phy_chanspec_ch14_widefilter_set(struct bcm4360_phy *phy, bool on)
{
	(void)phy;
	(void)on;
}

void bcm4360_phy_bf_preempt_enable(struct bcm4360_phy *phy, bool on)
{
	(void)phy;
	(void)on;
}

void bcm4360_phy_initcal_enable(struct bcm4360_phy *phy, bool on)
{
	(void)phy;
	(void)on;
}

void bcm4360_phy_set_filt_war(struct bcm4360_phy *phy, bool on)
{
	(void)phy;
	(void)on;
}

void bcm4360_phy_tkip_rifs_war(struct bcm4360_phy *phy, u8 val)
{
	(void)phy;
	(void)val;
}

void bcm4360_phy_antsel_init(struct bcm4360_phy *phy, bool lut_init)
{
	(void)phy;
	(void)lut_init;
}

void bcm4360_phy_freqtrack_start(struct bcm4360_phy *phy)
{
	(void)phy;
}

void bcm4360_phy_freqtrack_end(struct bcm4360_phy *phy)
{
	(void)phy;
}

/* A3: wlc_phy_interference_set - only PHY types 4/7 and chip 0xa886; no effect here */
void bcm4360_phy_interference_set(struct bcm4360_phy *phy, bool init)
{
	(void)phy;
	(void)init;
}

/* A3: wlc_phy_acimode_noisemode_reset - only N/HT revisions > 2; no effect here */
void bcm4360_phy_acimode_noisemode_reset(struct bcm4360_phy *phy, u32 channel, bool clear_aci,
					 bool clear_noise, bool disassoc)
{
	(void)phy;
	(void)channel;
	(void)clear_aci;
	(void)clear_noise;
	(void)disassoc;
}

/* ----------------------------------------------------- A15: antenna-select type */

/* A15: wlc_phy_antsel_type_set */
void bcm4360_phy_antsel_type_set(struct bcm4360_phy *phy, u8 type)
{
	phy->antsel_type = type;
	phy->antsel_type_lut = (type == 4 || type == 5);
}

/* ----------------------------------------------------- A4: the channel list */

/*
 * acphy-api.md, A4: the PHY's channel list - channel number and centre
 * frequency in MHz, in the object's order. A channel below 15 is 2.4 GHz.
 */
struct acphy_chan {
	u8 chan;
	u16 freq;
};

static const struct acphy_chan acphy_chans[] = {
	{ 1, 2412 }, { 2, 2417 }, { 3, 2422 }, { 4, 2427 }, { 5, 2432 }, { 6, 2437 },
	{ 7, 2442 }, { 8, 2447 }, { 9, 2452 }, { 10, 2457 }, { 11, 2462 }, { 12, 2467 },
	{ 13, 2472 }, { 14, 2484 },
	{ 34, 5170 }, { 38, 5190 }, { 42, 5210 }, { 46, 5230 },
	{ 36, 5180 }, { 40, 5200 }, { 44, 5220 }, { 48, 5240 }, { 52, 5260 }, { 54, 5270 },
	{ 56, 5280 }, { 60, 5300 }, { 62, 5310 }, { 64, 5320 },
	{ 100, 5500 }, { 102, 5510 }, { 104, 5520 }, { 108, 5540 }, { 110, 5550 },
	{ 112, 5560 }, { 116, 5580 }, { 118, 5590 }, { 120, 5600 }, { 124, 5620 },
	{ 126, 5630 }, { 128, 5640 }, { 132, 5660 }, { 134, 5660 }, { 136, 5680 },
	{ 140, 5700 }, { 144, 5720 },
	{ 149, 5745 }, { 151, 5755 }, { 153, 5765 }, { 157, 5785 }, { 159, 5795 },
	{ 161, 5805 }, { 165, 5825 },
	{ 184, 4920 }, { 185, 4925 }, { 187, 4935 }, { 188, 4940 }, { 189, 4945 },
	{ 192, 4960 }, { 196, 4980 }, { 200, 5000 }, { 204, 5020 }, { 207, 5035 },
	{ 208, 5040 }, { 209, 5045 }, { 210, 5050 }, { 212, 5060 }, { 216, 5080 },
};

#define ACPHY_CHAN_2G_MAX	14	/* channels <= 14 are 2.4 GHz (A4: number < 15) */
#define CHAN_BAND_2G		2
#define CHAN_BAND_5G		1

/* A4: wlc_phy_chanspec_band_firstch */
u16 bcm4360_phy_chanspec_band_firstch(struct bcm4360_phy *phy, u32 band)
{
	u32 i;

	(void)phy;	/* pi+0xfac (channels 149..165 excluded) is 0 for the AC-PHY */
	for (i = 0; i < ARRAY_SIZE(acphy_chans); i++) {
		u8 c = acphy_chans[i].chan;
		bool is2g = c <= ACPHY_CHAN_2G_MAX;

		if ((band == CHAN_BAND_2G) == is2g) {
			u16 cs = c | BCM4360_CHANSPEC_BW_20;

			return band == CHAN_BAND_2G ? (cs | BCM4360_CHANSPEC_BAND_2G)
						    : (cs | BCM4360_CHANSPEC_BAND_5G);
		}
	}
	return 0x00ff;
}

/* A4: wlc_phy_chanspec_band_validch - a 28-byte channel bit vector */
void bcm4360_phy_chanspec_band_validch(struct bcm4360_phy *phy, u32 band, u8 *chanvec)
{
	u32 i;

	(void)phy;
	hw_memset(chanvec, 0, 28);
	for (i = 0; i < ARRAY_SIZE(acphy_chans); i++) {
		u8 c = acphy_chans[i].chan;
		bool is2g = c <= ACPHY_CHAN_2G_MAX;

		if ((band == CHAN_BAND_2G) == is2g)
			chanvec[c >> 3] |= (u8)(1 << (c & 7));
	}
}

/* ----------------------------------------------------- A5: block_bbpll_change */

/* A5 (acphy-chanspec.md, section 12): on the 4360 spur_pending stays 0 */
void bcm4360_phy_block_bbpll_change(struct bcm4360_phy *phy, bool block, bool going_down)
{
	if (block) {
		phy->bbpll_blocked = 1;
		return;
	}
	phy->bbpll_blocked = 0;
	(void)going_down;	/* spur_pending is never set on the 4360, so nothing follows */
}

/* ----------------------------------------------------- A6: femctrl BT/WLAN override */

/* A6: wlc_phy_get_femctrl_bt_wlan_ovrd */
s8 bcm4360_phy_get_femctrl_bt_wlan_ovrd(struct bcm4360_phy *phy)
{
	u16 a, b;
	s8 r = (s8)0xff;

	bcm4360_mac_suspend(phy->hw);
	a = bcm4360_phy_read(&phy->io, ACPHY_REG_FEMCTRL_OVR);
	b = bcm4360_phy_read(&phy->io, ACPHY_REG_FEMCTRL_OVR);
	if (b & FEMCTRL_OVR_EN)
		r = (s8)(a & FEMCTRL_OVR_VAL);
	bcm4360_mac_enable(phy->hw);
	return r;
}

/* A6: wlc_phy_set_femctrl_bt_wlan_ovrd */
void bcm4360_phy_set_femctrl_bt_wlan_ovrd(struct bcm4360_phy *phy, s8 state)
{
	phy->femctrl_ovrd = state;
	bcm4360_mac_suspend(phy->hw);
	if (state == 1) {
		bcm4360_phy_mod(&phy->io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_VAL, 1);
		bcm4360_phy_mod(&phy->io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_EN, FEMCTRL_OVR_EN);
	} else if (state == 0) {
		bcm4360_phy_mod(&phy->io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_VAL, 0);
		bcm4360_phy_mod(&phy->io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_EN, FEMCTRL_OVR_EN);
	} else {
		bcm4360_phy_mod(&phy->io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_EN, 0);
		bcm4360_phy_mod(&phy->io, ACPHY_REG_FEMCTRL_OVR, FEMCTRL_OVR_VAL, 0);
	}
	bcm4360_mac_enable(phy->hw);
}

/* ----------------------------------------------------- A7: OFDM rate table */

/* A7: wlc_phy_get_ofdm_rate_lookup - rate in 500 kbit/s per 3-bit SIGNAL field */
static const u8 ofdm_rate_lookup[8] = { 96, 48, 24, 12, 108, 72, 36, 18 };

const u8 *bcm4360_phy_get_ofdm_rate_lookup(void)
{
	return ofdm_rate_lookup;
}

/* ----------------------------------------------------- A8: BSSinit */

/* A8: wlc_phy_BSSinit */
void bcm4360_phy_bssinit(struct bcm4360_phy *phy, bool bonlyap, s8 noise)
{
	u32 i;

	(void)bonlyap;
	for (i = 0; i < ARRAY_SIZE(phy->interference.noise_window); i++)
		phy->interference.noise_window[i] = (u8)noise;
	phy->interference.noise_index = 0;
	if (phy->interference.mode == BCM4360_INTERFERENCE_DESENSE &&
	    (phy->interference.flags & 1))
		phy->interference.flags_time = phy->now + BSSINIT_FLAGS_TIME_BIAS;
	for (i = 0; i < ARRAY_SIZE(phy->bssinit_a); i++) {
		phy->bssinit_a[i] = BSSINIT_NOISE;
		phy->bssinit_b[i] = BSSINIT_NOISE;
	}
	phy->bssinit_f54 = 0;
}

/* ----------------------------------------------------- A12 to A14 */

/* A12: wlc_phy_down - cancel the calibration timer, reset the spur mode */
u32 bcm4360_phy_down(struct bcm4360_phy *phy)
{
	u32 r = 0;

	if (phy->timer_phycal) {
		if (!bcm4360_timer_del(phy->hw, phy->timer_phycal))
			r = 1;	/* the timer was not pending */
	}
	phy->spur_mode = 0;
	phy->spur_pending = 0;
	return r;
}

/* A13: wlc_phy_hold_upd */
void bcm4360_phy_hold_upd(struct bcm4360_phy *phy, u32 id, bool on)
{
	if (on)
		phy->hold |= id;
	else
		phy->hold &= ~id;

	if (id & BCM4360_PHY_HOLD_SCAN) {
		if (on)
			return;		/* AC-PHY: nothing on scan start */
		bcm4360_phy_scanroam_restore(phy);	/* scan ended: restore the cached cal */
	}
}

/* A14: wlc_phy_mute_upd */
void bcm4360_phy_mute_upd(struct bcm4360_phy *phy, bool mute, u32 flags)
{
	if (mute) {
		phy->hold |= 0x10;
		return;
	}
	phy->hold &= ~0x10U;
	if (flags & 1) {
		if (phy->now > phy->glacial_timer)
			phy->cal.state.last_cal_time = phy->now - phy->glacial_timer;
		else
			phy->cal.state.last_cal_time = 0;
	}
}

/* ----------------------------------------------------- A9: cal_init, cal_perical */

/* acphy-cal-tx.md, section 8, steps 3 and 4: the start values of the watchdog histories */
#define CALINIT_H0_VALUE	100	/* pi+0x488, 0x48a and the entries pi+0x492.. */
#define CALINIT_H0_SUM		200	/* pi+0x490 (AC-PHY) */
#define CALINIT_HIST_VALUE	10	/* the averages, previous averages and entries */
#define CALINIT_HIST_SUM	0x14	/* the running sums (AC-PHY) */

/*
 * acphy-cal-tx.md, section 8, steps 3 and 4: the part of the search
 * parameters that the watchdog's statistics use as its six histories
 * (acphy-api-watchdog.md, "Corrections and answers", Q5). The fields not
 * set here (the indices of H0 to H3, the entries of H1) keep their value.
 */
static void cal_init_wd_stats(struct bcm4360_phy_wd_stats *st)
{
	struct bcm4360_phy_wd_hist *h;
	u32 i, k;

	st->shm_0x10e = 0;					/* pi+0x48c */
	st->shm_0x158 = 0;					/* pi+0x3f0 */
	st->shm_0x10c = 0;					/* pi+0x300 */
	st->pi_0x302 = 0;

	h = &st->hist[BCM4360_WD_H0];
	h->avg = CALINIT_H0_VALUE;
	h->prev_avg = CALINIT_H0_VALUE;
	h->sum = CALINIT_H0_SUM;
	for (k = 0; k < BCM4360_PHY_WD_HIST_DEPTH; k++)
		h->ring[k] = CALINIT_H0_VALUE;

	for (i = BCM4360_WD_H1; i < BCM4360_PHY_WD_HISTS; i++) {
		h = &st->hist[i];
		h->avg = CALINIT_HIST_VALUE;
		h->prev_avg = CALINIT_HIST_VALUE;
		h->sum = CALINIT_HIST_SUM;
		if (i == BCM4360_WD_H1)
			continue;			/* pi+0x2ea.. is not set */
		for (k = 0; k < BCM4360_PHY_WD_HIST_DEPTH; k++)
			h->ring[k] = CALINIT_HIST_VALUE;
	}
	st->hist[BCM4360_WD_H4].next = 0;			/* pi+0x418 */
	st->hist[BCM4360_WD_H5].next = 0;			/* pi+0x420 */
}

/*
 * acphy-cal-tx.md, section 8 (wlc_phy_cal_init): set the generic search
 * parameters once. On the AC-PHY it makes no hardware access; the search
 * parameters the transmit calibration reads are computed by that code itself,
 * so here only the "initialised" flag and the watchdog histories matter.
 */
void bcm4360_phy_cal_init(struct bcm4360_phy *phy)
{
	if (phy->cal.init_done)					/* step 2 */
		return;
	cal_init_wd_stats(&phy->wd_stats);			/* steps 3, 4 */
	phy->cal.init_done = true;				/* step 6 */
}

/* acphy-cal-tx.md, section 3 step 6: the first phase runs 5 ms after the arming */
#define CAL_MPHASE_FIRST_MS	5

/* acphy-cal-tx.md, sections 4 and 6: arm / reset the multi-phase timer */
static void cal_mphase_schedule(struct bcm4360_phy *phy, u32 ms)
{
	if (phy->cal.mode == 2 || phy->cal.mode == 3) {
		bcm4360_timer_del(phy->hw, phy->timer_phycal);
		phy->cal.state.phase = 1;
		bcm4360_timer_add(phy->hw, phy->timer_phycal, ms, false);
	}
}

static void cal_mphase_reset(struct bcm4360_phy *phy)
{
	if (phy->timer_phycal)
		bcm4360_timer_del(phy->hw, phy->timer_phycal);
	phy->cal.state.phase = 0;
	phy->cal.state.subphase = 0;
}

/*
 * acphy-api.md, A9 / acphy-cal-tx.md, section 3 (wlc_phy_cal_perical): decide
 * whether and how to calibrate. AC-PHY only.
 */
void bcm4360_phy_cal_perical(struct bcm4360_phy *phy, u8 reason)
{
	phy->cal.state.throttle = 0;
	phy->cal_skip = 1;

	if (phy->cal.mode == 0 || phy->cal.mode == 3)
		return;

	switch (reason) {
	case 2:		/* periodic (from the watchdog) */
		if (phy->hold & BCM4360_PHY_HOLD_NOT_ASSOC)
			return;
		phy->cal_skip = 0;
		phy->cal_wide = (phy->ver.phy_rev > 0x12);
		if (phy->temp.cal_delta != 0) {
			u32 t = phy->now;
			s16 temp;

			if (t - phy->cal.state.last_temp_time < phy->glacial_timer) {
				temp = phy->cal.state.last_temp;
			} else {
				phy->cal.state.last_temp_time = t;
				temp = (s16)bcm4360_phy_tempsense(phy);
			}
			if (abs16(temp - phy->cal.state.last_temp) < phy->temp.cal_delta &&
			    t - phy->cal.state.last_cal_time < 900 &&
			    phy->cal.state.last_chanspec == phy->radio_chanspec) {
				phy->cal.state.throttle = phy->glacial_timer;
				return;
			}
			phy->cal.state.last_temp = temp;
		}
		if (phy->cal.mode == 1) {
			bcm4360_phy_cals(phy, 0);
			return;
		}
		/* mode 2: start the multi-phase run unless one is in progress */
		if (phy->cal.state.phase != 0)
			return;
		phy->cal.state.search_mode = 1;
		break;
	case 3:
		if (phy->cal.mode != 2)
			return;
		if (phy->cal.state.phase != 0)
			cal_mphase_reset(phy);
		phy->cal.state.search_mode = 0;
		break;
	case 4:
	case 5:
	case 6:		/* forced full calibration */
		if (phy->cal.mode == 2 && phy->cal.state.phase != 0)
			cal_mphase_reset(phy);
		phy->cal.state.first_cal = 1;
		if (phy->temp.cal_delta != 0)
			phy->cal.state.last_temp = (s16)bcm4360_phy_tempsense(phy);
		bcm4360_phy_cals(phy, 0);
		return;
	default:
		return;
	}
	phy->cal.state.search_mode = 1;
	cal_mphase_schedule(phy, CAL_MPHASE_FIRST_MS);			/* step 6 */
}

/* ----------------------------------------------------- A10, A11: ioctl, iovar */

/* ioctl command numbers and error codes (acphy-api.md A10) */
#define PHY_IOCTL_GET_INTERFERENCE	0xd3
#define PHY_IOCTL_SET_INTERFERENCE	0xd4
#define PHY_IOCTL_SET_INTERFERENCE_OVR	0x137
#define PHY_IOCTL_GET_INTERFERENCE_OVR	0x138
#define PHY_IOCTL_UNINIT		0x1b
#define PHY_IOCTL_GET_NOISE		0x87
#define PHY_IOCTL_MAX_INTERF_MODE	7
#define PHY_E_NOTSUP			(-23)
#define PHY_E_RANGE			(-29)
#define PHY_E_NOTDOWN			(-5)
#define PHY_E_NOCLK			(-11)

/*
 * acphy-api.md, A10 "apply": re-apply the interference mode. If the
 * interface is not up, nothing. Otherwise, under a MAC suspend, run the
 * interference procedure (acphy-init.md section 8) with mode 0 first when
 * the new mode is not 0, then with the mode in use.
 */
static void phy_ioctl_apply(struct bcm4360_phy *phy, s32 newmode)
{
	if (!phy->up)
		return;
	bcm4360_mac_suspend(phy->hw);
	if (newmode != 0)
		bcm4360_phy_interference_apply(phy, 0, true);
	bcm4360_phy_interference_apply(phy, phy->interference.mode, true);
	bcm4360_mac_enable(phy->hw);
}

/* acphy-api.md, A10 (wlc_phy_ioctl) */
s32 bcm4360_phy_ioctl(struct bcm4360_phy *phy, s32 cmd, u32 len, void *arg)
{
	u32 *p = arg;
	s32 v = (arg && len >= 4) ? (s32)*p : 0;
	struct bcm4360_phy_interference *in = &phy->interference;
	bool is5g = (phy->radio_chanspec & BCM4360_CHANSPEC_BAND) == BCM4360_CHANSPEC_BAND_5G;

	switch (cmd) {
	case PHY_IOCTL_GET_INTERFERENCE:
		*p = in->mode | ((in->flags & 1) ? 0x80 : 0);
		return 0;
	case PHY_IOCTL_SET_INTERFERENCE:
		if (v < 0 || v > PHY_IOCTL_MAX_INTERF_MODE)
			return PHY_E_RANGE;
		if ((u32)v == in->mode)
			return 0;
		in->mode = v;
		phy_ioctl_apply(phy, v);
		return 0;
	case PHY_IOCTL_SET_INTERFERENCE_OVR:
		if (v < -1 || v > PHY_IOCTL_MAX_INTERF_MODE)
			return PHY_E_RANGE;
		if (v == -1) {
			in->forced = false;
			in->mode = is5g ? in->mode_5g : in->mode_2g;
		} else {
			in->forced = true;
			in->forced_2g = v;
			in->forced_5g = v;
			in->mode = v;
		}
		phy_ioctl_apply(phy, in->mode);
		return 0;
	case PHY_IOCTL_GET_INTERFERENCE_OVR:
		*p = in->forced ? (s32)in->mode : -1;
		return 0;
	case PHY_IOCTL_UNINIT:
		if (phy->up)
			return PHY_E_NOTDOWN;
		bcm4360_phy_set_uninitted(phy);
		return 0;
	case PHY_IOCTL_GET_NOISE:
		if (!phy->clk)
			return PHY_E_NOCLK;
		*p = (u32)(s32)bcm4360_phy_noise_avg(phy);
		return 0;
	default:
		return PHY_E_NOTSUP;
	}
}

/* iovar ids the MAC uses (acphy-api.md A11) */
#define PHY_IOV_MIN_TXPOWER	0x142
#define PHY_IOV_PHY_RXANTSEL	0x29
#define PHY_IOV_PHY_AFEOVERRIDE	0x1af

/* acphy-api.md, A11 (wlc_phy_iovar_dispatch): only the three iovars the MAC uses */
s32 bcm4360_phy_iovar_dispatch(struct bcm4360_phy *phy, u32 actionid, u16 type, void *params,
			       u32 plen, void *arg, s32 alen, s32 vsize)
{
	u32 id = actionid >> 1;
	bool set = actionid & 1;
	u32 *pin = params;
	u32 *pout = arg;
	u32 v = (params && plen >= 4) ? *pin : 0;

	(void)type;
	(void)alen;
	(void)vsize;
	switch (id) {
	case PHY_IOV_MIN_TXPOWER:
		if (set)
			phy->txpwr.min_power = (u8)v;
		else
			*pout = phy->txpwr.min_power;
		return 0;
	case PHY_IOV_PHY_AFEOVERRIDE:
		if (set)
			phy->acphy_1169 = (u8)v;
		else
			*pout = phy->acphy_1169;
		return 0;
	case PHY_IOV_PHY_RXANTSEL:
		return 0;	/* N-PHY only; no effect, arg left as is */
	default:
		return PHY_E_NOTSUP;	/* the other iovars are not specified yet */
	}
}
