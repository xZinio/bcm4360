// SPDX-License-Identifier: ISC
/*
 * Hybrid build: Broadcom's offload semaphore (tcm_sem_enter / tcm_sem_exit,
 * wlc_offloads.c), taken around every access of an object memory of the MAC,
 * by the open code of open/mac/offload.c (docs/re/spec/bmac-init.md A8).
 * Part of the MAC layer: switch open_mac.
 *
 * The adapter reads the offload state of `wlc` at the offsets of A8 and hands
 * the open code the shared structure (or NULL when there is none) and the
 * address of Broadcom's largest spin count, a static variable of
 * wlc_offloads.c that the offload code reads too (named by
 * hybrid/takeover.txt).
 */
#include "hybrid.h"
#include <bcm4360/offload.h>

/* bmac-init.md A8 */
#define WLC_OL		0x750	/* the offload state (pointer, may be NULL) */
#define OL_UP		0x8c	/* u8: offloads are up */
#define OL_SHARED	0x90	/* the structure shared with the ARM (pointer) */

/* .bss+0x3004 of the object (takeover.txt: name tcm_sem_max_spins) */
extern s32 tcm_sem_max_spins;

HYB_TAKE(void, tcm_sem_enter, (void *wlc));
HYB_TAKE(void, tcm_sem_exit, (void *wlc));

static void hyb_ol_sem(void *wlc, struct bcm4360_ol_sem *s)
{
	const u8 *ol = *(const u8 **)((const u8 *)wlc + WLC_OL);

	s->shared = NULL;
	if (ol && ol[OL_UP])
		s->shared = *(volatile u32 *const *)(ol + OL_SHARED);
	s->max_spins = &tcm_sem_max_spins;
}

void tcm_sem_enter(void *wlc)
{
	struct bcm4360_ol_sem s;

	if (!hyb_mac_on(HYB_MAC_SEM)) {
		bcm4360_blob_tcm_sem_enter(wlc);
		return;
	}
	hyb_ol_sem(wlc, &s);
	bcm4360_ol_sem_enter(&s);
}

void tcm_sem_exit(void *wlc)
{
	struct bcm4360_ol_sem s;

	if (!hyb_mac_on(HYB_MAC_SEM)) {
		bcm4360_blob_tcm_sem_exit(wlc);
		return;
	}
	hyb_ol_sem(wlc, &s);
	bcm4360_ol_sem_exit(&s);
}
