/* SPDX-License-Identifier: ISC */
/*
 * The MAC core's control state and the control primitives built on it: the
 * software copy of the MAC control register with its overrides, the microcode
 * wake override, and suspend/enable of the MAC core.
 *
 * Specification: docs/re/spec/bmac-init.md, sections A1-A7 and A9 (the
 * microcode host flags); the object memories and the setters on them: A8,
 * B4-B6, B11, E1, E3, E7, F1, F2; clocks and reset: A10-A17, C7, C9, C11,
 * C13, C14; the initialisation: B1-B3, B6-B10, B12, E6, E10; up and down:
 * C2-C6, C8, C10, C15, D3; the channel and the band: acphy-chanspec.md
 * sections 1-3 and bmac-init.md E4.
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
	u8  up;			/* the interface is up (E7; written by C4, C6) */
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

	/* clocks, core reset, crystal, core disable (bmac-init.md A10-A17, C7,
	 * C9, C11, C13, C14; task docs/re/tasks/bmac-clk.md). Services:
	 * bcm4360/mac_env.h; `env` is the platform's handle for them. */
	void *env;
	u8  noreset;		/* the core reset skips the reset itself (A14 step 4) */
	u8  forcefastclk;	/* the fast clock is forced (A13) */
	u8  sbclk;		/* the crystal / backplane clock is on (C9) */
	u8  phyclk;		/* argument of the last core_phy_clk (A12) */
	u8  suspended_fifos;	/* suspended transmit FIFOs (cleared by A14 step 7) */
	u8  dma_fifos;		/* bit i: transmit FIFO i (0..5) has a DMA engine */
	u16 gpio_sel;		/* the 16-bit word of C7 step 3 */
	u32 pllreq;		/* PLL request mask (C9) */
	u32 btc_mode;		/* Bluetooth coexistence mode (A17) */
	/* the current band */
	u8  band_valid;		/* there is a current band */
	u8  band_has_phy;	/* it has a PHY (a `pi`) */
	u16 band_phytype;	/* its PHY type (11 = AC-PHY) */
	u32 band_coreflags;	/* its core flags (A14 step 1) */
	/* the chip */
	u32 bustype;		/* 1 = PCI */
	u32 cccaps;		/* ChipCommon capabilities (bit 28: PMU) */
	u32 pmurev;		/* PMU revision */
	u32 chiprev;		/* chip revision */

	/* the initialisation (bmac-init.md B1-B3, B6-B10, B12, E6 and the two
	 * coexistence helpers of E10; task docs/re/tasks/bmac-coreinit.md) */
	u8  ucode_loaded;	/* the microcode is downloaded (B2; wlc_hw+0xae) */
	u8  antsel_type;	/* antenna selection type (B1 step 13) */
	u8  shortslot;		/* short slot time requested (E6, B10) */
	u8  stf_ss_mode;	/* mode of band_stf_ss_set (B12; wlc_hw+0x1a0) */
	u32 band_type;		/* the current band's type: 1 = 5 GHz, 2 = 2.4 GHz */
	u16 band_phyrev;	/* the current band's PHY revision */
	u16 sfbl, lfbl;		/* short / long fallback limit (B1 step 28) */
	u32 boardflags;		/* wlc_hw+0x8c */
	u32 boardflags2;	/* wlc_hw+0x90 */
	u32 machwcap;		/* MAC capabilities (wlc_hw+0xa4) */
	u32 intrcvlazy;		/* receive interrupt control (B1 step 21) */
	u32 gpio_chipctl;	/* the 32-bit word of B1 step 15 */
	u32 cccaps_ext;		/* ChipCommon extended capabilities */
	u32 boardvendor;	/* 0x106b: Apple */
	u32 boardtype;
	/* the coexistence state (E10; all 0 if there is none) */
	u8  btc_valid;		/* there is a coexistence state */
	u16 btc_flags;		/* +0x08 */
	u32 btc_gpio_mask;	/* +0x0c */
	u32 btc_gpio_out;	/* +0x10 */
	u16 btc_shm_base;	/* +0x1a: 2 * SHM(0x92) (B1 step 32) */

	/* up and down (bmac-init.md C2-C6, C8, C10, C15, D3; task
	 * docs/re/tasks/bmac-updown.md). Also `up`, `clk`, `sbclk`, `noreset`
	 * and the fields above. */
	u32 ccrev;		/* ChipCommon revision (sih+0x14; C2 step 9) */
	u32 led_gpio_mask;	/* wlc_hw+0x180: the GPIO lines of the LEDs (C2 step 9) */
	u32 pcie2_war_freq;	/* wlc_hw+0x1c0: the argument of C2 step 7 */

	/* the channel and the band (acphy-chanspec.md sections 1-3, bmac-init.md
	 * E4; task docs/re/tasks/bmac-chan.md) */
	u32 nbands;		/* wlc_hw+0x118: number of bands (2 for the 4360) */
	u32 band_unit;		/* the current band's unit, its 32-bit word +0x04 */
	u16 devid;		/* wlc_hw+0x82: PCI device id (0x43a0) */
	u16 chanspec;		/* wlc_hw+0x11c: the current chanspec (acphy-chanspec.md
				 * section 1 step 2; E9 reads its bandwidth bits) */

	/* Bluetooth coexistence (bmac-run.md R3; task docs/re/tasks/bmac-btc.md).
	 * Also btc_valid, btc_mode, btc_flags, btc_gpio_mask, btc_gpio_out above,
	 * which R3 writes. */
	s32 btc_wire;		/* btc+0x04: 2-, 3- or 4-wire interface */
	u8  btc_aa2g;		/* btc+0x18: the SROM variable aa2g */
	u32 machwcap_backup;	/* wlc_hw+0xa8: the copy of machwcap made at attach */
	s8  btswitch;		/* wlc_hw+0x1bc: the Bluetooth switch (-1 = 0xff: none) */
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

/* clocks, core reset, crystal, core disable */
void bcm4360_bmac_phyclk_fgc(struct bcm4360_mac *mac, bool on);			/* A10 */
void bcm4360_bmac_macphyclk_set(struct bcm4360_mac *mac, bool on);		/* A11 */
void bcm4360_bmac_core_phy_clk(struct bcm4360_mac *mac, bool on);		/* A12 */
void bcm4360_bmac_corereset(struct bcm4360_mac *mac, u32 flags);		/* A14 (+A13) */
void bcm4360_bmac_phy_reset(struct bcm4360_mac *mac);				/* A15 */
void bcm4360_bmac_core_phypll_ctl(struct bcm4360_mac *mac, bool on);		/* A16 */
void bcm4360_bmac_core_phypll_reset(struct bcm4360_mac *mac);			/* A16 */
u32 bcm4360_bmac_btc_mode_get(struct bcm4360_mac *mac);				/* A17 */
void bcm4360_bmac_coredisable(struct bcm4360_mac *mac);				/* C7 */
void bcm4360_bmac_xtal(struct bcm4360_mac *mac, bool want);			/* C9 */
void bcm4360_bmac_pllreq(struct bcm4360_mac *mac, bool set, u32 req_bit);	/* C9 */
void bcm4360_bmac_set_clk(struct bcm4360_mac *mac, bool on);			/* C9 */
s32 bcm4360_bmac_set_ctrl_bt_shd0(struct bcm4360_mac *mac, bool on);		/* C11 */
void bcm4360_bmac_set_ctrl_srom(struct bcm4360_mac *mac);			/* C11 */
bool bcm4360_bmac_radio_read_hwdisabled(struct bcm4360_mac *mac);		/* C13 */
bool bcm4360_bmac_deviceremoved(struct bcm4360_mac *mac);			/* C14 */
/* the fast (mode 0) or dynamic (mode 2) clock of A13, sub_064887, in bmac_clk.c */
void bcm4360_bmac_clkctl_clk(struct bcm4360_mac *mac, u32 mode);		/* A13 */

/* the initialisation: B1 with B2 (microcode download), B6, B7 (buffer memory
 * controller), B8, B10 (band init), B12 and the E10 helpers inside */
void bcm4360_bmac_init(struct bcm4360_mac *mac, u16 chanspec, bool mute);	/* B1 */
s32 bcm4360_bmac_wowlucode_start(struct bcm4360_mac *mac);			/* B3 */
void bcm4360_bmac_switch_macfreq(struct bcm4360_mac *mac, u8 spurmode);	/* B9 */
void bcm4360_bmac_set_extlna_pwrsave_shmem(struct bcm4360_mac *mac);		/* B10.15 */
void bcm4360_bmac_txbw_update(struct bcm4360_mac *mac);				/* B12 */
void bcm4360_bmac_band_stf_ss_set(struct bcm4360_mac *mac, u8 mode);		/* B12 */
void bcm4360_bmac_set_shortslot(struct bcm4360_mac *mac, bool shortslot);	/* E6 */

/* up and down: C2-C6 and C15, with C8's sub_05fefa (flush of the queues) and
 * C10 (sub_06333d, the coexistence GPIOs released) inside; the reset of C8;
 * the check of the chip access at attach, D3 */
void bcm4360_bmac_hw_up(struct bcm4360_mac *mac);				/* C2 */
s32 bcm4360_bmac_up_prep(struct bcm4360_mac *mac);				/* C3 */
s32 bcm4360_bmac_up_finish(struct bcm4360_mac *mac);				/* C4 */
s32 bcm4360_bmac_down_prep(struct bcm4360_mac *mac);				/* C5 */
s32 bcm4360_bmac_down_finish(struct bcm4360_mac *mac);				/* C6 */
void bcm4360_bmac_hw_down(struct bcm4360_mac *mac);				/* C15 */
void bcm4360_bmac_reset(struct bcm4360_mac *mac);				/* C8 */
bool bcm4360_bmac_validate_chip_access(struct bcm4360_mac *mac);		/* D3 */

/* the channel and the band: acphy-chanspec.md sections 1-3, bmac-init.md E4.
 * `limits` is the PHY's handle of the channel's power limits, passed on. */
void bcm4360_bmac_set_chanspec(struct bcm4360_mac *mac, u16 chanspec, bool mute,
			       void *limits);					/* chanspec 1 */
void bcm4360_bmac_setxband(struct bcm4360_mac *mac, u32 unit);		/* chanspec 2 */
void bcm4360_bmac_bw_set(struct bcm4360_mac *mac, u16 bw);			/* chanspec 3 */
void bcm4360_bmac_bw_reset(struct bcm4360_mac *mac);				/* chanspec 3 */
void bcm4360_bmac_mute(struct bcm4360_mac *mac, bool on, u32 reason);		/* E4 */
/* exported for it from bmac_ctrl.c and bmac_init.c */
void bcm4360_bmac_mctrl_write(struct bcm4360_mac *mac);				/* A1 */
void bcm4360_bmac_bsinit(struct bcm4360_mac *mac, u16 chanspec, bool bandswitch); /* B10 */

/*
 * At run time (docs/re/spec/bmac-run.md R1, R2, R5, R6 and R7's TSF read;
 * task docs/re/tasks/bmac-runtime.md): the energy detect, the transmit FIFO
 * suspend/resume, the CCA counters, the rate offsets in shared memory.
 *
 * A rate set (R6) is the upper layer's: `count` rates (500 kbit/s units, bit 7
 * may be set) from `rates`. Its 6-byte entries (`entries`, R6) are in the
 * byte layout the specification gives: +0 the rate (u8), +2 the offset (u16),
 * +4 the value (u16), little-endian; byte +1 is never written.
 */
void bcm4360_bmac_ifsctl_edcrs_set(struct bcm4360_mac *mac, bool on);		/* R1 */
void bcm4360_bmac_tx_fifo_suspend(struct bcm4360_mac *mac, u32 fifo);		/* R2 */
void bcm4360_bmac_tx_fifo_resume(struct bcm4360_mac *mac, u32 fifo);		/* R2 */
bool bcm4360_bmac_tx_fifo_suspended(struct bcm4360_mac *mac, u32 fifo);		/* R2 */
void bcm4360_bmac_cca_stats_read(struct bcm4360_mac *mac, u32 out[9]);		/* R5 */
s32 bcm4360_bmac_cca_stats_get(struct bcm4360_mac *mac, u32 out[9]);
					/* R5 wlc_bmac_cca_stats_read: the read, result 0 */
u32 bcm4360_bmac_rate_shm_offset(struct bcm4360_mac *mac, u8 rate);		/* R6 */
void bcm4360_bmac_stf_get_rateset_shm_offset(struct bcm4360_mac *mac, const u8 *rates,
					     u32 count, u16 off, u8 *entries);	/* R6 */
void bcm4360_bmac_stf_set_rateset_shm_offset(struct bcm4360_mac *mac, u32 count, u16 off,
					     const u8 *entries);		/* R6 */
void bcm4360_bmac_read_tsf(struct bcm4360_mac *mac, u32 *lo, u32 *hi);		/* R7 */

/* Bluetooth coexistence (bmac-run.md R3) */
s32 bcm4360_bmac_btc_mode_set(struct bcm4360_mac *mac, s32 mode);		/* R3 */
s32 bcm4360_bmac_btc_wire_set(struct bcm4360_mac *mac, s32 wire);		/* R3 */
s32 bcm4360_bmac_btc_wire_get(struct bcm4360_mac *mac);			/* R3 */
s32 bcm4360_bmac_set_btswitch(struct bcm4360_mac *mac, s8 sw);			/* R3 */
void bcm4360_bmac_update_bt_chanspec(struct bcm4360_mac *mac, u16 chanspec, bool scanning,
				     bool p4);					/* R3 */
void bcm4360_bmac_seci_upd(struct bcm4360_mac *mac);				/* R3 */
/* exported for them from bmac_init.c (the two coexistence helpers of B10/E10) */
void bcm4360_bmac_btc_flags_to_mhf(struct bcm4360_mac *mac);		/* R3 sub_062b79 */
void bcm4360_bmac_btc_gpio_enable(struct bcm4360_mac *mac);		/* R3 sub_06106b */

#endif
