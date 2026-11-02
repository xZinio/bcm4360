/* SPDX-License-Identifier: ISC */
/*
 * The MAC core's control state and the control primitives built on it: the
 * software copy of the MAC control register with its overrides, the microcode
 * wake override, and suspend/enable of the MAC core.
 *
 * Specification: docs/re/spec/bmac-init.md, sections A1-A7 and A9 (the
 * microcode host flags); the object memories and the setters on them: A8,
 * B4-B6, B11, E1, E3, E7, F1, F2.
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

	/* object memories and the state setters built on them (bmac-init.md
	 * A8, B4-B6, B11, E1, E3, E7, F1, F2; task docs/re/tasks/bmac-mem.md) */
	u8  up;			/* the interface is up (E7) */
	u8  hw_etheraddr[6];	/* the saved hardware MAC address (E1) */
	u16 srl, lrl;		/* short / long retry limit (E7) */
	u16 cwmin, cwmax;	/* contention window of the current band (B11) */
	/* The semaphore shared with the on-chip ARM's offload firmware (A8,
	 * `tcm_sem_enter`/`tcm_sem_exit`): called around every access of an
	 * object memory where the specification says so, with sem_ctx as the
	 * argument. NULL: no offload firmware, nothing to call. */
	void *sem_ctx;
	void (*sem_enter)(void *ctx);
	void (*sem_exit)(void *ctx);
};

void bcm4360_bmac_mctrl(struct bcm4360_mac *mac, u32 mask, u32 val);	/* A2 (+A1) */
void bcm4360_bmac_wake_override_set(struct bcm4360_mac *mac, u32 bit);	/* A3 (+A5) */
void bcm4360_bmac_wake_override_clear(struct bcm4360_mac *mac, u32 bit);	/* A4 */
void bcm4360_bmac_suspend(struct bcm4360_mac *mac);			/* A6 */
void bcm4360_bmac_enable(struct bcm4360_mac *mac);			/* A7 */
void bcm4360_bmac_mhf(struct bcm4360_mac *mac, u8 idx, u16 mask, u16 val,
		     u32 bands);					/* A9 */

/*
 * Object memories (shared memory, microcode scratch registers, internal
 * hardware registers, address match table, template RAM) and the state
 * setters that write through them. `offset` and `len` are in bytes; `sel` is
 * the memory selector of A8/section F (memory number << 16). Buffers hold
 * bytes in the order of rising addresses.
 */
void bcm4360_bmac_set_shm(struct bcm4360_mac *mac, u32 offset, u16 v, s32 len);	/* A8 */
void bcm4360_bmac_copyto_objmem(struct bcm4360_mac *mac, u32 offset, const void *buf,
				s32 len, u32 sel);				/* B4 */
void bcm4360_bmac_copyfrom_objmem(struct bcm4360_mac *mac, u32 offset, void *buf,
				  s32 len, u32 sel);				/* B4 */
void bcm4360_bmac_write_amt(struct bcm4360_mac *mac, s32 idx, const u8 *addr,
			    u16 attr);						/* B5 */
void bcm4360_bmac_write_inits(struct bcm4360_mac *mac, const void *inits);	/* B6 */
void bcm4360_bmac_set_cwmin(struct bcm4360_mac *mac, u16 v);			/* B11 */
void bcm4360_bmac_set_cwmax(struct bcm4360_mac *mac, u16 v);			/* B11 */
void bcm4360_bmac_set_hw_etheraddr(struct bcm4360_mac *mac, const u8 *addr);	/* E1 */
void bcm4360_bmac_hw_etheraddr(struct bcm4360_mac *mac, u8 *out);		/* E1 */
void bcm4360_bmac_set_rcmta(struct bcm4360_mac *mac, s32 idx, const u8 *addr);	/* E3 */
void bcm4360_bmac_retrylimit_upd(struct bcm4360_mac *mac, u16 srl, u16 lrl);	/* E7 */
void bcm4360_bmac_write_ihr(struct bcm4360_mac *mac, u32 reg, u16 v);		/* F1 */
void bcm4360_bmac_write_template_ram(struct bcm4360_mac *mac, s32 offset, s32 len,
				     const void *buf);				/* F2 */

#endif
