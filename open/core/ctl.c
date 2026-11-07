/*
 * The control policies of Broadcom's upper layer, for the open core:
 *   G2  LEDs (wlc_led.c): which pin shows what, blink while frames pass
 *   G4  radio switch, rfkill and MPC (wlc.c): the disable bits, the radio monitor
 *   G3  Bluetooth coexistence policy (wlc_enable_btc_ps_protection, the predictor)
 * Specification: docs/re/spec/coverage.md part 2, sections G2, G3 and G4. The join
 * (G7) is in wlchw.c, next to the rate and channel code it uses.
 */
#include "core.h"
#include <bcm4360/bmac_attach.h>
#include <bcm4360/bmac_led.h>

#define D11_TSF_LO		0x180
#define TICK_ACTIVITY_MS	200
#define MONITOR_MS		2000
#define LED_ACT_ON_MS		80
#define LED_ACT_OFF_MS		20
#define PLL_REQ_MONITOR		2
#define PLL_REQ_RADIO_OFF	4

#define LEDB_NONE		0
#define LEDB_ON_UP		1
#define LEDB_ACTIVITY		2
#define LEDB_ON_UP3		3
#define LEDB_RADIO_5G		4
#define LEDB_RADIO_2G		5
#define LEDB_UNUSED11		11
#define LEDB_OFF		21
#define LEDB_MAX		0x18

static void *hw_of(struct bcm4360_core *c)
{
	return CORE_WLC_HW(c);
}

/* ---------------------------------------------------------------------- */
/* G2: LEDs                                                                */
/* ---------------------------------------------------------------------- */

/* "ledbh<i>" or "wl0gpio<i>": the name in buf */
static void led_varname(char *buf, const char *prefix, u32 i)
{
	u32 n = 0;

	while (*prefix)
		buf[n++] = *prefix++;
	if (i >= 10)
		buf[n++] = (char)('0' + i / 10);
	buf[n++] = (char)('0' + i % 10);
	buf[n] = 0;
}

/* wlc_led_attach (G2 "Which pin shows what"), the timer and the table; no hardware access */
void core_led_attach(struct bcm4360_core *c)
{
	struct core_ctl *t = &c->ctl;
	u32 i;
	char name[16];

	for (i = 0; i < CTL_NLED; i++) {
		t->led[i].pin = (u8)i;
		t->led[i].behaviour = LEDB_NONE;
		t->led[i].activehi = 1;
		t->led[i].sync = 1;
	}
	t->led[0].behaviour = c->sihp.boardvendor == 0x0e11 ? LEDB_ON_UP3 : LEDB_ACTIVITY;
	t->led[1].behaviour = LEDB_RADIO_2G;
	t->led[2].behaviour = LEDB_RADIO_5G;
	for (i = 0; i < CTL_NLED; i++) {
		const char *v;
		s32 x;

		led_varname(name, "ledbh", i);
		v = core_getvar(c->vars, name);
		if (!v) {
			led_varname(name, "wl0gpio", i);
			v = core_getvar(c->vars, name);
		}
		if (!v)
			continue;
		x = core_getintvar(c->vars, name);
		if ((x & 0x7f) > LEDB_MAX)
			continue;
		t->led[i].pin = (u8)i;
		t->led[i].behaviour = (u8)(x & 0x7f);
		t->led[i].activehi = (x & 0x80) ? 0 : 1;
	}
	t->led_activity = 0;
	for (i = 0; i < CTL_NLED; i++)
		if (t->led[i].behaviour == LEDB_ACTIVITY)
			t->led_activity = 1;
	t->led_attached = 1;
}

/* the activity timer function (sub_07d6fb) */
static void led_activity_timer(void *arg)
{
	struct bcm4360_core *c = arg;
	struct core_ctl *t = &c->ctl;

	if (!c->up)
		return;
	core_led_event(c);
	if (!t->led_seen && t->led_armed) {
		bcm4360_plat_timer_del(c->plat, t->led_timer);
		t->led_armed = 0;
	}
	t->led_seen = 0;
}

void core_led_detach(struct bcm4360_core *c)
{
	struct core_ctl *t = &c->ctl;

	if (t->led_timer) {
		bcm4360_plat_timer_del(c->plat, t->led_timer);
		bcm4360_plat_timer_free(c->plat, t->led_timer);
		t->led_timer = NULL;
	}
	t->led_armed = 0;
}

/* wlc_led_init: the GPIO mask (pins of every entry with a behaviour other than 0 and 11) */
u32 core_led_mask(struct bcm4360_core *c)
{
	struct core_ctl *t = &c->ctl;
	u32 i, mask = 0;

	if (!t->led_timer && c->plat)
		t->led_timer = bcm4360_plat_timer_init(c->plat, led_activity_timer, c, "led");
	for (i = 0; i < CTL_NLED; i++)
		if (t->led[i].behaviour != LEDB_NONE && t->led[i].behaviour != LEDB_UNUSED11)
			mask |= 1u << t->led[i].pin;
	t->led_mask = mask;
	return mask;
}

/* the LEDs show "up": wlc_led_up was called, or MPC alone holds the radio off */
static bool led_is_up(struct bcm4360_core *c)
{
	return c->ctl.led_up || core_radio_mpc_down(c);
}

static void led_set(struct bcm4360_core *c, const struct core_led_entry *e, bool on)
{
	bcm4360_bmac_led(hw_of(c), 1u << e->pin, on ? 1u << e->pin : 0, e->activehi);
}

/* sub_07d753: stop blinking */
static void led_blink_stop(struct bcm4360_core *c, struct core_led_entry *e)
{
	if (!e->on_ms && !e->off_ms)
		return;
	e->on_ms = e->off_ms = 0;
	c->ctl.led_sync_mask &= ~(1u << e->pin);
	bcm4360_bmac_led_blink(hw_of(c), e->pin, 0, 0);
}

/* sub_07daf9 with blink set 1 (80 ms on, 20 ms off) */
static void led_blink_start(struct bcm4360_core *c, struct core_led_entry *e)
{
	struct core_ctl *t = &c->ctl;

	if (e->sync)
		t->led_sync_mask |= 1u << e->pin;
	if (e->on_ms == LED_ACT_ON_MS && e->off_ms == LED_ACT_OFF_MS)
		return;
	e->on_ms = LED_ACT_ON_MS;
	e->off_ms = LED_ACT_OFF_MS;
	bcm4360_bmac_led_blink(hw_of(c), e->pin, LED_ACT_ON_MS, LED_ACT_OFF_MS);
	if (t->led_sync_mask)
		bcm4360_bmac_blink_sync(hw_of(c), t->led_sync_mask);
}

/* the band LEDs (sub_07cf8d, sub_07d011): both bands on a card with two bands and no lock */
static bool led_band_on(struct bcm4360_core *c, u32 unit)
{
	bool two = FIELD(c->pub, PUB_NBANDS, u32) != 1;
	u32 cur = FIELD(FIELD(c->wlc, WLC_BAND, void *), 0x04, u32);

	return two || cur == unit;
}

static void led_behaviour(struct bcm4360_core *c, struct core_led_entry *e)
{
	struct core_ctl *t = &c->ctl;
	bool up = led_is_up(c);

	switch (e->behaviour) {
	case LEDB_ON_UP:
	case LEDB_ON_UP3:
		led_set(c, e, up);
		break;
	case LEDB_OFF:
		led_set(c, e, false);
		break;
	case LEDB_RADIO_2G:
		led_set(c, e, up && led_band_on(c, 0));
		break;
	case LEDB_RADIO_5G:
		led_set(c, e, up && led_band_on(c, 1));
		break;
	case LEDB_ACTIVITY:
		if (!t->led_up)
			t->led_seen = 0;
		if (!t->led_seen) {
			led_blink_stop(c, e);
			led_set(c, e, false);
		} else {
			led_blink_start(c, e);
		}
		break;
	default:			/* association patterns: not described */
		break;
	}
}

/* wlc_led_event */
void core_led_event(struct bcm4360_core *c)
{
	struct core_ctl *t = &c->ctl;
	bool blinking = false;
	u32 i;

	for (i = 0; i < CTL_NLED; i++)
		if (t->led[i].behaviour != LEDB_NONE)
			led_behaviour(c, &t->led[i]);
	for (i = 0; i < CTL_NLED; i++)
		if (t->led[i].on_ms || t->led[i].off_ms)
			blinking = true;
	if (led_is_up(c) && blinking) {
		if (!t->led_blink_run) {
			bcm4360_bmac_led_blink_event(hw_of(c), 1);
			t->led_blink_run = 1;
		}
	} else if (t->led_blink_run) {
		bcm4360_bmac_led_blink_event(hw_of(c), 0);
		t->led_blink_run = 0;
	}
}

/* wlc_led_up: the end of wlc_up */
void core_led_up(struct bcm4360_core *c)
{
	c->ctl.led_up = 1;
	core_led_event(c);
}

/* wlc_led_down: from wlc_down */
void core_led_down(struct bcm4360_core *c)
{
	struct core_ctl *t = &c->ctl;

	if (t->led_armed && t->led_timer) {
		bcm4360_plat_timer_del(c->plat, t->led_timer);
		t->led_armed = 0;
	}
	t->led_seen = 0;
	t->led_up = 0;
	core_led_event(c);
}

/* wlc_led_start_activity_timer: a frame was transmitted or delivered (G2 "Activity") */
void bcm4360_core_led_activity(struct bcm4360_core *c)
{
	struct core_ctl *t = &c->ctl;

	if (!t->led_activity || !t->led_up || !t->led_timer)
		return;
	t->led_seen = 1;
	if (!t->led_armed) {
		bcm4360_plat_timer_add(c->plat, t->led_timer, TICK_ACTIVITY_MS, true);
		t->led_armed = 1;
	}
}

/* ---------------------------------------------------------------------- */
/* G4: radio switch, rfkill, MPC                                           */
/* ---------------------------------------------------------------------- */

/* wlc_down_for_mpc: the driver is down only because MPC switched the radio off */
bool core_radio_mpc_down(struct bcm4360_core *c)
{
	return !FIELD(c->pub, PUB_UP, u8) && c->w.radio_dis == RADIO_DIS_MPC;
}

/* sub_026c55, wlc_radio_monitor_start: keep the backplane clock so the switch can be read */
void core_radio_monitor_start(struct bcm4360_core *c)
{
	struct core_ctl *t = &c->ctl;

	if (t->monitor)
		return;
	t->monitor = 1;
	CORE_MAC_CALL(c, bcm4360_bmac_pllreq(m, true, PLL_REQ_MONITOR));
	if (t->radio_timer)
		bcm4360_plat_timer_add(c->plat, t->radio_timer, MONITOR_MS, true);
}

/* wlc_radio_monitor_stop (from wlc_up, wlc_radio_mpc_upd, detach) */
void core_radio_monitor_stop(struct bcm4360_core *c)
{
	struct core_ctl *t = &c->ctl;

	if (!t->monitor)
		return;
	t->monitor = 0;
	CORE_MAC_CALL(c, bcm4360_bmac_pllreq(m, false, PLL_REQ_MONITOR));
	if (t->radio_timer)
		bcm4360_plat_timer_del(c->plat, t->radio_timer);
}

/* sub_030b4b: read the hardware switch (bmac-init C13); true if the state changed */
static bool poll_hw(struct bcm4360_core *c, bool notify)
{
	struct core_wlchw *w = &c->w;
	struct core_ctl *t = &c->ctl;
	bool dis = false, changed;

	CORE_MAC_CALL(c, dis = bcm4360_bmac_radio_read_hwdisabled(m));
	if (dis)
		w->radio_dis |= RADIO_DIS_HW;
	else
		w->radio_dis &= ~RADIO_DIS_HW;
	changed = !t->hw_known || t->hw_prev != dis;
	t->hw_known = 1;
	t->hw_prev = dis;
	if (changed && notify)
		bcm4360_plat_rfkill(c->plat, dis);
	return changed;
}

bool core_radio_poll_hw(struct bcm4360_core *c)
{
	return poll_hw(c, true);
}

/* wlc_radio_disable, the driver-down branch: the LEDs off, with the PLL requested around it */
static void radio_off_while_down(struct bcm4360_core *c)
{
	CORE_MAC_CALL(c, bcm4360_bmac_pllreq(m, true, PLL_REQ_RADIO_OFF));
	core_led_event(c);
	CORE_MAC_CALL(c, bcm4360_bmac_pllreq(m, false, PLL_REQ_RADIO_OFF));
}

/* sub_037024, the monitor timer: poll the switch; while the radio is off, hold the PLL
 * request and keep the LEDs off. The platform brings the interface back up (rfkill). */
static void radio_timer_fn(void *arg)
{
	struct bcm4360_core *c = arg;
	bool gone = false;

	if (!c->attached)
		return;
	CORE_MAC_CALL(c, gone = bcm4360_bmac_deviceremoved(m));
	if (gone) {
		bcm4360_plat_note(c->plat, "fatal: device gone (radio timer)", 0);
		bcm4360_plat_fatal(c->plat);
		return;
	}
	poll_hw(c, true);
	if (c->w.radio_dis && !c->up)
		radio_off_while_down(c);
}

void core_radio_attach(struct bcm4360_core *c)
{
	if (c->plat)
		c->ctl.radio_timer = bcm4360_plat_timer_init(c->plat, radio_timer_fn, c, "radio");
}

void core_radio_detach(struct bcm4360_core *c)
{
	struct core_ctl *t = &c->ctl;

	if (t->radio_timer) {
		bcm4360_plat_timer_del(c->plat, t->radio_timer);
		bcm4360_plat_timer_free(c->plat, t->radio_timer);
		t->radio_timer = NULL;
	}
	t->monitor = 0;
}

/* wlc_radio_disable: down, with the monitor running; or only the LEDs when already down */
static void radio_disable(struct bcm4360_core *c)
{
	if (!c->up) {
		radio_off_while_down(c);
		return;
	}
	core_radio_monitor_start(c);
	core_w_down(c);
}

/* wlc_radio_upd: every disable bit clear brings the driver up, else it goes down */
static int radio_upd(struct bcm4360_core *c)
{
	int r = 0;

	if (!c->w.radio_dis) {
		if (!c->up && c->attached) {
			r = core_w_up(c, c->chanspec);
			if (!r && c->up)
				bcm4360_core_intrson(c);
		}
	} else {
		radio_disable(c);
	}
	return r;
}

/*
 * The software switch (WLC_SET_RADIO, bit 0). Not used by the mac80211 driver, whose
 * start/stop is the software switch; it is the interface of the radio off/on steps of the
 * check (docs/re/spec/coverage.md G4).
 */
int bcm4360_core_radio_set(struct bcm4360_core *c, bool off)
{
	if (off)
		c->w.radio_dis |= RADIO_DIS_SW;
	else
		c->w.radio_dis &= ~RADIO_DIS_SW;
	return radio_upd(c);
}

/*
 * The hardware switch for rfkill (G4 "For the open driver"): its state, polled. Up: the
 * watchdog keeps it (read here as well); down: the backplane clock is requested while the
 * switch is off, as the radio monitor does.
 */
bool bcm4360_core_rfkill_poll(struct bcm4360_core *c)
{
	if (c->attached && FIELD(c->pub, PUB_HW_UP, u8) && !c->up) {
		core_radio_monitor_start(c);
		(void)poll_hw(c, false);	/* the caller reports the state itself */
		if (!c->w.radio_dis)
			core_radio_monitor_stop(c);
	}
	return (c->w.radio_dis & RADIO_DIS_HW) != 0;
}

/* wlc_radio_upd for the watchdog: the disable bits changed while up (MPC, the hardware switch) */
void core_radio_upd(struct bcm4360_core *c)
{
	(void)radio_upd(c);
}

/*
 * MPC (wlc_radio_mpc_upd) for mac80211's idle state: idle = not associated and not scanning.
 * Idle sets bit 2; the watchdog's radio update then takes the driver down (as in Broadcom's
 * code, G4). Not idle clears it and brings the driver up on `chanspec` at once.
 */
int bcm4360_core_mpc(struct bcm4360_core *c, bool idle, u16 chanspec)
{
	struct core_wlchw *w = &c->w;
	int r;

	c->ctl.mpc_idle = idle;
	if (!c->attached)
		return 0;
	if (idle) {
		if (!w->associated && !c->ctl.joining && !w->scanning)
			w->radio_dis |= RADIO_DIS_MPC;
		return 0;
	}
	w->radio_dis &= ~RADIO_DIS_MPC;
	c->chanspec = chanspec;
	r = radio_upd(c);
	if (!w->radio_dis)
		core_radio_monitor_stop(c);
	return r;
}

/* ---------------------------------------------------------------------- */
/* G3: Bluetooth coexistence                                               */
/* ---------------------------------------------------------------------- */

/* wlc_enable_btc_ps_protection (G3 steps 1 to 7) */
void core_btc_ps_protection(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	struct core_ctl *t = &c->ctl;
	s32 wire = 0;
	u32 mode = 0, p = 0, f = 0, a, cc, value;
	bool wpa_unkeyed, connected = w->associated != 0;

	CORE_MAC_CALL(c, wire = bcm4360_bmac_btc_wire_get(m); mode = m->btc_mode;
		      p = m->btc_shm_base; f = m->btc_flags);
	if (wire <= 2 || mode == 0)
		return;
	if (w->radio_dis || !FIELD(c->pub, PUB_UP, u8) || core_radio_mpc_down(c))
		return;
	if (p) {				/* wlc_bmac_btc_rssi_threshold_get */
		t->btc_thr = bcm4360_shm_read(c->hw, (u16)(p + 0x92)) & 0xff;
		t->btc_thr6 = bcm4360_shm_read(c->hw, (u16)(p + 0xc8)) & 0xff;
		t->btc_thr7 = bcm4360_shm_read(c->hw, (u16)(p + 0xca)) & 0xff;
	}
	cc = ((t->btc_thr && t->rssi && (u32)(-(s32)t->rssi) > t->btc_thr) || (f & 4)) ? 8 : 0;
	a = (f & 0x10) ? 0x0200 : 0;
	wpa_unkeyed = c->pk.ps.wpa && !c->pk.ps.group_key;
	if (connected)			/* the one configuration: infrastructure, associated */
		value = wpa_unkeyed ? 0 : (a | cc);
	else
		value = 0;
	CORE_MAC_CALL(c, bcm4360_bmac_mhf(m, 2, 0x0208, (u16)value, 2));
}

/* wlc_bmac_btc_update_predictor (G3): the next Bluetooth slot after now */
void core_btc_predictor(struct bcm4360_core *c)
{
	u32 p = 0, period, now, lo, hi, lo2, t;
	int n;

	CORE_MAC_CALL(c, p = m->btc_shm_base);
	if (!p)
		return;
	period = bcm4360_shm_read(c->hw, (u16)(p + 8));
	if (!period)
		return;
	now = d11_read32(c->hw, D11_TSF_LO);
	/* bounded: this runs in the interrupt path under the device lock */
	for (n = 0; n < 8; n++) {
		lo = bcm4360_shm_read(c->hw, (u16)(p + 0x18));
		hi = bcm4360_shm_read(c->hw, (u16)(p + 0x1a));
		lo2 = bcm4360_shm_read(c->hw, (u16)(p + 0x18));
		if (lo == lo2)
			break;
	}
	if (lo != lo2)
		return;
	t = lo | hi << 16;
	bcm4360_shm_write(c->hw, (u16)(p + 0x1c), (u16)(((now - t) / period + 1) * period + t));
}

void bcm4360_core_set_rssi(struct bcm4360_core *c, s8 dbm)
{
	c->ctl.rssi = dbm;
}
