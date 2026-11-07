/*
 * State of the core's frame path (docs/re/spec/frames.md), embedded in
 * struct bcm4360_core as `fr`. Owned by frames.c (interrupts), rx.c
 * (receive), tx.c (transmit and transmit status).
 */
#ifndef BCM4360_CORE_FRAMES_H
#define BCM4360_CORE_FRAMES_H

#include <bcm4360/types.h>

struct bcm4360_core;
struct bcm4360_rxstatus;

#define FR_NFIFO	4	/* transmit FIFOs with an engine (frames.md 3.1) */

/* frames.md 3.2, 3.3: prefix, header, rate entries (shared by tx.c and ampdu.c) */
#define TX_PREFIX_LEN		4
#define TX_HDR_LEN		0x7c
#define TX_HEADROOM		(TX_PREFIX_LEN + TX_HDR_LEN)
#define TX_NRATES		4
#define TX_RATE_ENTRY(i)	(0x14 + 0x14 * (i))
#define TX_RATE_PLCP		6	/* the PLCP bytes in a rate entry */

/* header offsets (frames.md 3.3) */
#define TXH_CTL_LOW		0x02
#define TXH_CTL_HIGH		0x04
#define TXH_CHANSPEC		0x06
#define TXH_IVOFFSET		0x08
#define TXH_FRAMELEN		0x0a
#define TXH_FRAMEID		0x0c
#define TXH_SEQ			0x0e

/* A-MPDU (docs/re/spec/ampdu.md): sessions the platform has started */
#define AMPDU_NPEERS		8	/* peers (stations) with sessions */
#define AMPDU_NTID		8

struct core_ampdu_sess {
	bool started;		/* tx_start called */
	bool on;		/* operational: MPDUs of the TID are aggregated */
	u8  window;		/* Block Ack window granted (1..64) */
	u16 next_seq;		/* the sequence number expected next (ampdu.md 1.7) */
};

struct core_ampdu_peer {
	u8 max_mpdus;		/* ampdu.md 1.3: scb_ampdu+9 */
	u8 density;		/* ampdu.md 1.3: MPDU spacing code (scb_ampdu+8) */
};

/* ampdu.md 1.6: the last frame posted to a FIFO */
struct core_ampdu_fifo {
	u8 epoch;		/* the current epoch bit (0 or 1) */
	u8 kind;		/* 0 not aggregated, 1 HT, 2 VHT */
	u8 peer, tid;
	u8 plcp[5];		/* HT: bytes 0 and 3 (at 0, 3); VHT: bytes 0..4 */
};

struct core_ampdu {
	struct core_ampdu_sess sess[AMPDU_NPEERS][AMPDU_NTID];
	struct core_ampdu_peer peer[AMPDU_NPEERS];
	struct core_ampdu_fifo fifo[FR_NFIFO];
	u32 repost;		/* consecutive re-posts of suppressed MPDUs (ampdu.md 2.3) */
	u32 nstatus, nholes, nreposts;	/* counters */
};

struct core_frames {
	/* interrupts (frames.md 2) */
	bool resched;		/* the last deferred run left work: re-read the status first */
	u32 txphyerr;		/* count of MI_PHYTXERR */
	u32 noted;		/* interrupt statuses logged since the last up (first 8) */

	/* receive (frames.md 1) */
	u32 rx_filter;		/* BCM4360_FILTER_* (set by bcm4360_core_set_filter) */
	bool rate_valid;	/* the rate of the last A-MPDU's first MPDU (1.5) */
	u8  rate_enc, rate_rate, rate_nss, rate_bw, rate_flags;
	u32 rx_frames, rx_dropped;

	/* transmit (frames.md 3) */
	u16 txid;		/* the driver-wide frame id counter (3.4) */
	bool stopped[FR_NFIFO];	/* the platform's queues are stopped (tx returned 1) */

	/* transmit status (frames.md 4) */
	u32 tx_status, tx_fatal;

	/* A-MPDU transmit (ampdu.md; ampdu.c) */
	struct core_ampdu ampdu;
};

/* interrupts (frames.c) */
bool core_rx_run(struct bcm4360_core *c, u32 bound);
bool core_txstatus_run(struct bcm4360_core *c, u32 bound, bool *fatal);


/* A-MPDU (ampdu.c) */
struct bcm4360_txparams;
bool core_ampdu_is_agg(struct bcm4360_core *c, const struct bcm4360_txparams *p);
void core_ampdu_header(struct bcm4360_core *c, u8 *prefix, u8 *h, const u8 *f,
		       const struct bcm4360_txparams *p);

#endif
