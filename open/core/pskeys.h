/* SPDX-License-Identifier: ISC */
/*
 * State of the core's power save and hardware keys (docs/re/spec/ps-keys.md),
 * embedded in struct bcm4360_core as `pk`. Owned by keys.c and ps.c.
 */
#ifndef BCM4360_CORE_PSKEYS_H
#define BCM4360_CORE_PSKEYS_H

#include <bcm4360/types.h>

#define PK_NKEYS		54	/* ps-keys.md 2.1: key entries of MAC revision 42 */
#define PK_NGROUP		4	/* entries 0..3: the default (group) keys */

/* hardware cipher numbers of MAC revisions >= 40 (ps-keys.md 2.1) */
#define PK_CIPHER_WEP40		1
#define PK_CIPHER_TKIP		2
#define PK_CIPHER_WEP104	3
#define PK_CIPHER_CCMP		5

/* one entry of the key table, as the core keeps it to write it again after an init */
struct core_key {
	u8  used;
	u8  cipher;		/* PK_CIPHER_*; 0: none */
	u8  has_addr;		/* a pairwise entry (entries 4 and up) */
	u8  addr[6];
	u8  key[16];		/* the first 16 bytes: what goes into the table */
	u8  txmic[8], rxmic[8];	/* TKIP (kept for a hardware MIC, which is not used) */
	u16 p1k[5];		/* TKIP: the receive phase-1 key */
	u32 iv32;		/* TKIP: the IV32 it is valid for */
};

/* ps-keys.md 1.1: the power save state (the fields of `pm` and the wake requests) */
struct core_ps {
	u8 conf;		/* the platform wants power save (IEEE80211_CONF_PS) */
	u8 hps;			/* MAC control bit HPS is set by us */
	u8 pending;		/* pm+0x0a: the change to power save is not yet acknowledged */
	u8 req_pending;		/* wlc+0x2ad: WAKE request while pending */
	u8 req_leave;		/* WAKE request: a frame announcing the end of power save is out */
	u8 awake_bcn;		/* wlc+0x2af: awake until the next beacon */
	u8 uatbtt;		/* wlc+0x2ac: update the TBTT at the next beacon */
	u8 wpa;			/* a key was set since the association (bsscfg+0x90 != 0) */
	u8 group_key;		/* a group key was set (bsscfg+0x9d) */
	u8 ticks;		/* watchdog ticks since pending / req_leave began */
	u8 bcn_missed;		/* watchdog ticks since the last beacon */
	u16 null_id;		/* frame id of the last null frame that left power save */
	u8 null_id_valid;
	u32 nnull_enter, nnull_leave, ncomplete, nabort;	/* counters */
};

struct core_pskeys {
	struct core_key key[PK_NKEYS];
	bool hw_mic;		/* hardware TKIP MIC: keys written, RxStatus2 bits 3/4 used (never) */
	struct core_ps ps;
};

#endif
