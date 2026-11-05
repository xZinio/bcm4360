/* SPDX-License-Identifier: ISC */
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

struct core_frames {
	/* interrupts (frames.md 2) */
	bool resched;		/* the last deferred run left work: re-read the status first */
	u32 txphyerr;		/* count of MI_PHYTXERR */

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
};

/* interrupts (frames.c) */
bool core_rx_run(struct bcm4360_core *c, u32 bound);
bool core_txstatus_run(struct bcm4360_core *c, u32 bound, bool *fatal);

#endif
