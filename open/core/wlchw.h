/* SPDX-License-Identifier: ISC */
/*
 * The state of what Broadcom's upper layer added around the open layers
 * (docs/re/spec/wlc-hw.md), embedded in struct bcm4360_core as `w`.
 * The code is in wlchw.c; the fields that open modules (stf, cca, MAC layer)
 * read at Broadcom's offsets stay in the blocks of Broadcom's layout.
 */
#ifndef BCM4360_CORE_WLCHW_H
#define BCM4360_CORE_WLCHW_H

#include <bcm4360/types.h>
#include <bcm4360/rate.h>

#define WLCHW_NBANDS		2
#define WLCHW_RATEMAP		109

struct core_wlchw_edca {
	u8 valid;
	u8 aifs;
	u16 cwmin, cwmax, txop;
};

struct core_wlchw {
	/* wlc-hw.md "Data" */
	u8 wme;				/* pub+0x54 */
	u8 radio_dis;			/* pub+0x84, bit 1: the hardware switch */
	u8 clk_up;			/* wlc+0x31 */
	u8 associated;			/* pub+0x59 */
	u8 probe_req;			/* wlc+0x704: requesters of probe requests */
	u8 mhf2_bit0;			/* wlc+0x708 != 0 */
	u8 ldpc;			/* stf+0x13 */
	u8 band_switch_pending;		/* wlc+0x24f */
	u16 defchanspec;		/* defbss+0x32 */
	u16 home_chanspec;		/* wlc+0x516 */
	u16 retry[4];			/* wlc+0x2be: retry limits per access category */
	u16 lifetime;			/* wlc+0x5f8 */
	u16 key_base;			/* wlc+0x3c0: byte offset of the hardware key table */
	u16 bcn_li;			/* wlc+0x2b1, +0x2b2 */
	u8 dtimnoslp;			/* wlc+0x290 */
	u8 shortslot_ovr;		/* wlc+0x533: 0xff = none */
	u8 shortslot;			/* wlc+0x532 */
	u8 mc_version_known;
	u32 machwcap;			/* wlc+0x230 */
	u32 ticks;			/* pub+0x48 */
	u32 stats_prev[64];
	u32 fcs_errors;
	u8 stats_valid;

	/* the rate sets and the response-rate map (wlc-hw.md 1.6) */
	u16 rdir_ofdm[16];		/* wlc+0x454: copy of SHM(0x1c0..) */
	u16 rdir_cck[16];		/* wlc+0x474: copy of SHM(0x200..) */
	struct bcm4360_rateset hwrs[WLCHW_NBANDS];	/* band+0x5c */
	u8 rmap[WLCHW_NBANDS][WLCHW_RATEMAP];		/* band+0x84 */

	/* BSS state (wlc-hw.md 5) */
	u8 bssid[6];
	u8 macaddr[6];
	u16 beacon_int;
	u8 dtim_period;
	u64 last_tsf;
	struct core_wlchw_edca edca[4];	/* by transmit FIFO */

	/* filters and scanning (wlc-hw.md 6) */
	u32 filter;
	u8 scanning;			/* a scan runs: WAKE and BCNS_PROMISC held */
	u8 bcn_promisc_scan;		/* wlc+0x28d */

	/* coexistence (wlc-hw.md 3 step 7) */
	u8 bt_active;
	u8 bt_count;

	/* LEDs (bmac-run.md R4) */
	u32 led_mask;
	u8 led_state;			/* 0 down, 1 up */

	/* the regulatory state (channel-power.md 1; chanpow.c) */
	u32 country;			/* cm+0x18: the CLM country id */
	u32 regrev, srom_regrev;
	char srom_ccode[4], ccode[4], abbrev[4];
	u8 valid[WLCHW_NBANDS][28];	/* valid 20 MHz channels */
	u8 quiet[28];			/* quiet channels */
	u8 radar[28];
	u8 locale_flags[WLCHW_NBANDS];
	u8 country_ok;
	void *ol;			/* wlc+0x750: the offload state (empty: no engine) */
};

#endif
