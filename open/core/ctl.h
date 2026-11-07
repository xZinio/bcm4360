/*
 * State of the control policies of Broadcom's upper layer: LEDs (G2), the
 * radio switch, rfkill and MPC (G4), the start of a join (G7), Bluetooth
 * coexistence (G3). docs/re/spec/coverage.md part 2. Code: ctl.c, and the join
 * in wlchw.c. Embedded in struct bcm4360_core as `ctl`.
 */
#ifndef BCM4360_CORE_CTL_H
#define BCM4360_CORE_CTL_H

#include <bcm4360/types.h>

#define CTL_NLED		32

/* G4: the bits of the radio-disable word (core_wlchw.radio_dis, pub+0x84) */
#define RADIO_DIS_SW		1	/* software (WLC_SET_RADIO) */
#define RADIO_DIS_HW		2	/* the hardware switch */
#define RADIO_DIS_MPC		4	/* MPC */

/* G2: one entry of the LED table (wlc_led.c, led+0x10 + 16 i) */
struct core_led_entry {
	u8 pin;
	u8 behaviour;		/* 0..24 */
	u8 activehi;
	u8 sync;		/* blink-synchronised */
	u16 on_ms, off_ms;	/* 0: not blinking */
};

struct core_ctl {
	/* G2 */
	struct core_led_entry led[CTL_NLED];
	u32 led_mask;		/* GPIO mask of the LEDs (led+0x214) */
	u32 led_sync_mask;	/* pins blinking in synchrony (led+0x248) */
	void *led_timer;	/* the activity timer */
	u8 led_attached;
	u8 led_activity;	/* some entry has behaviour 2 (led+0x229) */
	u8 led_seen;		/* activity seen since the last tick (led+0x228) */
	u8 led_armed;		/* the timer runs (led+0x238) */
	u8 led_blink_run;	/* the blink engine started (led+0x239) */
	u8 led_up;		/* LEDs "up" (led+0x23a) */

	/* G4 */
	void *radio_timer;	/* the radio monitor's timer (wlc+0x280) */
	u8 monitor;		/* the monitor runs (wlc+0x250) */
	u8 mpc_idle;		/* the platform reports idle (MPC may switch the radio off) */
	u8 hw_prev;		/* the last hardware switch state reported to the platform */
	u8 hw_known;
	u16 up_chanspec;	/* the channel to come back to after MPC or a radio switch */

	/* G7 */
	u8 joining;		/* the association state machine is busy: PHY hold 1, WAKE */
	u8 join_ticks;
	u8 cal_valid;		/* a join calibration was made for join_cs / join_bssid */
	u16 join_cs;
	u8 join_bssid[6];

	/* G3 */
	s8 rssi;		/* the primary BSS's RSSI, dBm; 0: unknown */
	u8 btc_thr;		/* B+3 */
	u8 btc_thr6, btc_thr7;	/* B+6, B+7 */
};

struct bcm4360_core;

/* ctl.c: G2 */
void core_led_attach(struct bcm4360_core *c);
void core_led_detach(struct bcm4360_core *c);
u32 core_led_mask(struct bcm4360_core *c);
void core_led_up(struct bcm4360_core *c);
void core_led_down(struct bcm4360_core *c);
void core_led_event(struct bcm4360_core *c);
/* ctl.c: G4 */
bool core_radio_mpc_down(struct bcm4360_core *c);	/* wlc_down_for_mpc */
void core_radio_attach(struct bcm4360_core *c);
void core_radio_detach(struct bcm4360_core *c);
void core_radio_monitor_start(struct bcm4360_core *c);
void core_radio_monitor_stop(struct bcm4360_core *c);
void core_radio_upd(struct bcm4360_core *c);
bool core_radio_poll_hw(struct bcm4360_core *c);	/* sub_030b4b: true if it changed */
/* ctl.c: G3 */
void core_btc_ps_protection(struct bcm4360_core *c);
void core_btc_predictor(struct bcm4360_core *c);	/* interrupt bit 0x00800000 (frames.md 2.2) */
/* keys.c */
void core_keys_forget(struct bcm4360_core *c);
void core_keys_hostflag(struct bcm4360_core *c);

#endif
