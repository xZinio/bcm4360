/* SPDX-License-Identifier: ISC */
/*
 * The MAC core's control state and the control primitives built on it: the
 * software copy of the MAC control register with its overrides, the microcode
 * wake override, and suspend/enable of the MAC core.
 *
 * Specification: docs/re/spec/bmac-init.md, sections A1-A7 and A9 (the
 * microcode host flags).
 */
#ifndef BCM4360_MAC_H
#define BCM4360_MAC_H

#include <bcm4360/types.h>
#include <bcm4360/hw.h>

/* The MAC core's control state (the subset of Broadcom's `wlc_hw` that the
 * control primitives work from). The caller fills in every field; in the
 * tests they come from the object's `wlc_hw` at the entry of the call. */
struct bcm4360_mac {
	struct bcm4360_hw *hw;	/* for D11() register and SHM() access */
	u32 maccontrol;		/* software copy of D11(0x120) WITHOUT the overrides */
	u32 mac_suspend_depth;	/* nesting counter of suspend/enable */
	u32 wake_override;	/* bitmask of reasons to keep the microcode awake */
	u32 mute_override;	/* not 0: transmitter muted */
	u16 fastpwrup_dly;	/* fast-clock power-up delay, microseconds (A5 budget) */
	u8  clk;		/* the core's clock is on */
	u8  mac_rev;		/* MAC core revision (42) */
	u32 chip_id;		/* chip id (0x4360); only for the d144/5357 comments */
	/* you may add fields below this line */

	/* microcode host flags (bmac-init.md A9) */
	u16 mhf[2][5];		/* copies per band: [0] = 2.4 GHz, [1] = 5 GHz */
	u8  current_band;	/* 0 = 2.4 GHz is the current band, 1 = 5 GHz */
	u8  band_present;	/* bit 0: 2.4 GHz band state exists, bit 1: 5 GHz exists */
};

void bcm4360_bmac_mctrl(struct bcm4360_mac *mac, u32 mask, u32 val);	/* A2 (+A1) */
void bcm4360_bmac_wake_override_set(struct bcm4360_mac *mac, u32 bit);	/* A3 (+A5) */
void bcm4360_bmac_wake_override_clear(struct bcm4360_mac *mac, u32 bit);	/* A4 */
void bcm4360_bmac_suspend(struct bcm4360_mac *mac);			/* A6 */
void bcm4360_bmac_enable(struct bcm4360_mac *mac);			/* A7 */
void bcm4360_bmac_mhf(struct bcm4360_mac *mac, u8 idx, u16 mask, u16 val,
		     u32 bands);					/* A9 */

#endif
