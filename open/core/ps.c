// SPDX-License-Identifier: ISC
/*
 * Station power save (docs/re/spec/ps-keys.md, section 1) for a platform that
 * sends the null data frames and parses the TIM itself (mac80211's
 * IEEE80211_HW_PS_NULLFUNC_STACK, ps-keys.md 1.9). What the core keeps of the
 * object's power save:
 *
 *  - the HPS bit (MAC control bit 25) and the WAKE bit (bit 26) with the
 *    wake requests: change to power save pending, a frame that ends power
 *    save on its way, awake until the first beacon after the join or a
 *    channel change, TBTT update at the next beacon (1.1, 1.3, 1.4);
 *  - the completion of the change by the transmit status (1.5);
 *  - the beacon part: the TBTT check and re-alignment (1.6 steps 2 to 4),
 *    and WAKE held while beacons are missed (1.8).
 *
 * Not here, because the platform does it: the TIM, PS-Poll and U-APSD (1.6,
 * 1.7), PM_FAST with its PM2 timer (mac80211's dynamic power save), and the
 * watchdog's repeat of the null frame (1.5; replaced by giving up, below).
 *
 * The change to power save is announced by the platform's null frame with
 * the PM bit set. The core sets the HPS bit when that frame is posted (the
 * object's order: HPS, then the frame), so that the microcode puts the PM bit
 * on the air whether or not it overrides the host's bit (ps-keys.md, open
 * question 1); the core never sets a PM bit itself.
 */
#include "core.h"

#define D11_TSF_LO		0x180
#define D11_TBTT		0x18c
#define MCTL_HPS		0x02000000

#define W0_PM_INDICATED		0x0008	/* frames.md 4.2: the receiver was indicated to be in PS */
#define W0_ACKED		0x8000

#define PS_ABORT_TICKS		5	/* watchdog ticks before a change that was never completed
					 * is given up (the object repeats the frame instead) */
#define BCN_TIMEOUT_MIN_MS	2000	/* ps-keys.md 1.8: at least 2000 ms; guess */
#define BCN_TIMEOUT_BEACONS	10
#define TBTT_RANGE_MULT		2	/* sub_025fb0: 2 x beacon interval ... */

static void mctrl(struct bcm4360_core *c, u32 mask, u32 val)
{
	CORE_MAC_CALL(c, bcm4360_bmac_mctrl(m, mask, val));
}

/* ps-keys.md 1.2 (wlc_ps_allowed): the parts the core knows */
static bool ps_allowed(struct bcm4360_core *c)
{
	const struct core_ps *ps = &c->pk.ps;

	return c->up && c->w.associated && c->w.dtim_period &&
	       (!ps->wpa || ps->group_key);
}

bool core_ps_wake_req(struct bcm4360_core *c)
{
	const struct core_ps *ps = &c->pk.ps;

	return ps->req_pending || ps->req_leave || ps->awake_bcn || ps->uatbtt;
}

/* ps-keys.md 1.3: the WAKE bit follows the requests (wlc_set_wake_ctrl, cca_stats_upd) */
static void wake_changed(struct bcm4360_core *c)
{
	if (!c->up)
		return;
	core_w_set_wake_ctrl(c);
	if (c->cca)
		bcm4360_cca_stats_upd(c->cca, 0);
}

/*
 * ps-keys.md 1.3, 1.4 steps 2 and 3 (wlc_set_ps_ctrl, wlc_set_pmpending): the HPS bit
 * follows `want` and wlc_ps_allowed. Entering power save makes the change pending, which
 * holds WAKE until the frame with the PM bit is acknowledged.
 */
static void ps_apply(struct bcm4360_core *c)
{
	struct core_ps *ps = &c->pk.ps;
	bool on = ps->conf && ps_allowed(c);

	if (on == (ps->hps != 0))
		return;
	ps->hps = on;
	if (c->up)
		mctrl(c, MCTL_HPS, on ? MCTL_HPS : 0);
	ps->pending = on;
	ps->req_pending = on;
	ps->ticks = 0;
	wake_changed(c);
}

/* the platform's IEEE80211_CONF_PS (ps-keys.md 1.9) */
void bcm4360_core_set_ps(struct bcm4360_core *c, bool ps)
{
	struct core_ps *p = &c->pk.ps;
	bool was = p->hps;

	p->conf = ps;
	ps_apply(c);
	if (was && !p->hps) {
		/* 1.9 "Leave": HPS is off; WAKE is held until the frame that tells the AP is out */
		p->req_leave = 1;
		p->ticks = 0;
		p->null_id_valid = 0;
		wake_changed(c);
	}
}

/*
 * ps-keys.md 1.4 step 4: a null data frame to the BSSID passes the core. With the PM bit it
 * announces power save (HPS on, change pending); without, the end of it (HPS off, WAKE held
 * until this frame's transmit status).
 */
void core_ps_tx_frame(struct bcm4360_core *c, const u8 *f, u32 len, u16 frame_id)
{
	struct core_ps *p = &c->pk.ps;
	u8 st = f[0] & 0xfc;

	if (!c->w.associated || len < 24 || (f[0] & 0x0c) != 0x08 || (st != 0x48 && st != 0xc8) ||
	    (f[1] & 3) != 1 || hw_memcmp(f + 4, c->w.bssid, 6))
		return;
	if (f[1] & 0x10) {
		p->nnull_enter++;
		p->conf = 1;
		ps_apply(c);
		return;
	}
	p->nnull_leave++;
	bcm4360_core_set_ps(c, false);
	if (p->req_leave) {
		p->null_id = frame_id;
		p->null_id_valid = 1;
	}
}

/*
 * ps-keys.md 1.5 (wlc_update_pmstate, sub_03b36a, wlc_pm_pending_complete): the status of a
 * frame completes the change when the frame went out with the PM bit and was acknowledged.
 */
void core_ps_txstatus(struct bcm4360_core *c, u32 w0)
{
	struct core_ps *p = &c->pk.ps;
	bool changed = false;

	if (p->pending && (w0 & W0_ACKED) && (w0 & W0_PM_INDICATED)) {
		p->pending = 0;
		p->ncomplete++;
	}
	if (p->req_pending && !p->pending) {
		p->req_pending = 0;
		changed = true;
	}
	if (p->req_leave && p->null_id_valid && (w0 >> 16) == p->null_id) {
		p->req_leave = 0;
		p->null_id_valid = 0;
		changed = true;
	}
	if (changed)
		wake_changed(c);
}

/* ps-keys.md 1.8: association and disassociation; the DTIM period known or a key changes */
void core_ps_bss(struct bcm4360_core *c, bool joined, bool left)
{
	struct core_ps *p = &c->pk.ps;

	if (left) {
		/* wlc_reset_pmstate: power save off, every request dropped */
		if (p->hps && c->up)
			mctrl(c, MCTL_HPS, 0);
		hw_memset(p, 0, sizeof(*p));
		wake_changed(c);
		return;
	}
	if (joined) {
		/* wlc_sta_assoc_upd: awake until the first beacon */
		p->awake_bcn = 1;
		p->bcn_missed = 0;
		wake_changed(c);
	}
	ps_apply(c);
}

/* the platform says what keys exist (ps-keys.md 1.2 step 4) */
void bcm4360_core_key_state(struct bcm4360_core *c, bool wpa, bool group)
{
	c->pk.ps.wpa = wpa;
	c->pk.ps.group_key = group;
	ps_apply(c);
}

/* ps-keys.md 1.8: wlc_set_chanspec holds WAKE until the next beacon */
void core_ps_chanspec(struct bcm4360_core *c)
{
	struct core_ps *p = &c->pk.ps;

	if (!c->w.associated)
		return;
	p->awake_bcn = 1;
	p->bcn_missed = 0;
	wake_changed(c);
}

void core_ps_down(struct bcm4360_core *c)
{
	hw_memset(&c->pk.ps, 0, sizeof(c->pk.ps));
}

static u32 rd32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* ps-keys.md 1.6 step 3 (sub_025fb0): is the TBTT register out of range? */
static bool tbtt_out_of_range(struct bcm4360_core *c)
{
	u32 range = TBTT_RANGE_MULT * c->w.beacon_int * (c->w.dtim_period ? c->w.dtim_period : 1);
	u32 tsf = d11_read32(c->hw, D11_TSF_LO), tbtt = d11_read32(c->hw, D11_TBTT);

	return (s32)(tbtt - tsf) < 0 || tbtt - tsf > range * 1024;
}

/* wlc-hw.md 5.3 (wlc_compute_bcntsfoff): the air time of the beacon up to its timestamp */
static u32 bcn_tsf_offset(u8 rate, u8 enc, u8 flags)
{
	u32 pre = enc ? 20 : (rate <= 4 || rate == 11 || rate == 22) ? 192 : 20;

	if (!enc && (flags & BCM4360_RX_SHORTPRE) && pre == 192)
		pre = 96;
	if (!rate || enc)
		rate = 12;
	return pre + (24 * 8 * 2 + rate - 1) / rate;		/* the 24 header bytes */
}

/* wlc-hw.md 5.3, wlc_tbtt_align: D11(0x18c) from the beacon's timestamp */
static void tbtt_align(struct bcm4360_core *c, const u8 *f, u8 rate, u8 enc, u8 flags)
{
	u32 b = f[32] | (u32)f[33] << 8, p, lo, hi, o, n;
	u64 t;

	if (!b)
		b = 1024;
	p = b * 1024;
	lo = rd32(f + 24);
	hi = rd32(f + 28);
	t = ((u64)hi << 32 | lo) - bcn_tsf_offset(rate, enc, flags);
	lo = (u32)t;
	o = (u32)((((t >> 10) % b) << 10) | (lo & 0x3ff));
	n = (lo - o) + p * ((o + 0x18fff + p) / p);
	if ((s32)(o >> 10) >= (s32)c->w.dtimnoslp - 1)
		n += o;
	d11_write32(c->hw, D11_TBTT, n);
}

/*
 * ps-keys.md 1.6 steps 2 to 4: a beacon of the BSS (frame control 0x80, address 3 = BSSID,
 * `len` bytes with the FCS). The TIM is the platform's.
 */
void core_ps_beacon(struct bcm4360_core *c, const u8 *f, u32 len, u8 rate, u8 enc, u8 flags)
{
	struct core_ps *p = &c->pk.ps;
	bool changed = false;

	if (!c->w.associated || len < 24 + 12 + 4 || f[0] != 0x80 ||
	    hw_memcmp(f + 16, c->w.bssid, 6))
		return;
	p->bcn_missed = 0;
	if (p->awake_bcn) {
		p->awake_bcn = 0;
		changed = true;
	}
	if (p->hps) {
		if (!p->uatbtt && tbtt_out_of_range(c)) {
			p->uatbtt = 1;
			changed = true;
			wake_changed(c);
		}
		if (p->uatbtt) {
			tbtt_align(c, f, rate, enc, flags);
			p->uatbtt = 0;
			changed = true;
		}
	}
	if (changed)
		wake_changed(c);
}

/* ps-keys.md 1.5, 1.8: once a second */
void core_ps_watchdog(struct bcm4360_core *c)
{
	struct core_ps *p = &c->pk.ps;
	u32 timeout, bi_ms;

	if (!c->w.associated)
		return;
	if (p->pending || p->req_leave) {
		if (++p->ticks >= PS_ABORT_TICKS) {
			/* the frame never completed it: stay awake instead of repeating it */
			p->nabort++;
			p->ticks = 0;
			p->req_leave = 0;
			p->null_id_valid = 0;
			if (p->pending) {
				p->conf = 0;
				ps_apply(c);
			}
			wake_changed(c);
		}
	} else {
		p->ticks = 0;
	}
	if (!p->hps)
		return;
	if (p->bcn_missed < 255)
		p->bcn_missed++;
	bi_ms = c->w.beacon_int * 1024 / 1000;
	timeout = (BCN_TIMEOUT_BEACONS * bi_ms * (c->w.dtim_period ? c->w.dtim_period : 1) + 999) /
		  1000;
	if (timeout < BCN_TIMEOUT_MIN_MS / 1000)
		timeout = BCN_TIMEOUT_MIN_MS / 1000;
	if (p->bcn_missed >= timeout / 2 && p->bcn_missed < timeout) {
		if (!p->uatbtt) {
			p->uatbtt = 1;
			wake_changed(c);
		}
	} else if (p->bcn_missed >= timeout && p->uatbtt) {
		p->uatbtt = 0;
		wake_changed(c);
	}
}
