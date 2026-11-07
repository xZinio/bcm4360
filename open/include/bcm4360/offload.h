/*
 * The semaphore that the driver shares with the offload firmware of the
 * on-chip ARM (Broadcom's tcm_sem_enter / tcm_sem_exit of wlc_offloads.c):
 * taken around every access of an object memory of the MAC.
 *
 * Specification: docs/re/spec/bmac-init.md, A8 (the paragraphs on
 * tcm_sem_enter / tcm_sem_exit, "Exactly").
 */
#ifndef BCM4360_OFFLOAD_H
#define BCM4360_OFFLOAD_H

#include <bcm4360/types.h>

struct bcm4360_ol_sem {
	/*
	 * The structure shared with the ARM (its 32 bit words at byte offsets
	 * 0x14, 0x18 and 0x1c are the semaphore), or NULL when there is none:
	 * no offload state, offloads not up, or no shared structure - the
	 * caller finds that out (A8, the three conditions). NULL: the
	 * semaphore does nothing.
	 */
	volatile u32 *shared;
	/* the driver-wide largest spin count (A8, max_spins) */
	s32 *max_spins;
};

/* A8, tcm_sem_enter */
void bcm4360_ol_sem_enter(const struct bcm4360_ol_sem *s);
/* A8, tcm_sem_exit */
void bcm4360_ol_sem_exit(const struct bcm4360_ol_sem *s);

#endif
