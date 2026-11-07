/*
 * The semaphore that the driver shares with the offload firmware of the
 * on-chip ARM (Broadcom's tcm_sem_enter / tcm_sem_exit, wlc_offloads.c),
 * taken around every access of an object memory of the MAC.
 *
 * Written from docs/re/spec/bmac-init.md, A8 (the paragraphs on
 * tcm_sem_enter / tcm_sem_exit, "Exactly"). The shared structure and the
 * driver-wide largest spin count are the caller's (struct bcm4360_ol_sem);
 * the words of the structure are plain memory, each read done again in every
 * round. No register of the card is involved.
 */
#include <bcm4360/offload.h>

/*
 * The 32 bit words of the structure shared with the ARM (A8: byte offsets
 * 0x14, 0x18 and 0x1c), as indices of 32 bit words.
 */
#define OL_SHARED_WORD(off)	((off) / 4)
#define OL_SHARED_0x14		OL_SHARED_WORD(0x14)	/* 1 by enter, 0 by exit */
#define OL_SHARED_0x18		OL_SHARED_WORD(0x18)	/* spin while 1 ... */
#define OL_SHARED_0x1c		OL_SHARED_WORD(0x1c)	/* ... and this is 1 (1 by enter) */

#define OL_SEM_TAKEN		1	/* the value stored and tested */
#define OL_SEM_FREE		0	/* the value stored by exit */
#define OL_SEM_MAX_ROUNDS	100000	/* rounds of the spin loop at most */

/*
 * docs/re/spec/bmac-init.md, A8, "Exactly", enter: mark the words 0x14 and
 * 0x1c, spin while 0x18 and 0x1c are both 1 (at most 100000 rounds, no
 * delay), keep the largest number of rounds in *max_spins.
 */
void bcm4360_ol_sem_enter(const struct bcm4360_ol_sem *s)
{
	volatile u32 *sh = s->shared;
	s32 n = 0;

	if (!sh)
		return;
	sh[OL_SHARED_0x14] = OL_SEM_TAKEN;
	sh[OL_SHARED_0x1c] = OL_SEM_TAKEN;
	for (;;) {
		if (sh[OL_SHARED_0x18] != OL_SEM_TAKEN)
			break;
		if (sh[OL_SHARED_0x1c] != OL_SEM_TAKEN)
			break;
		if (n == OL_SEM_MAX_ROUNDS)
			break;
		n++;
	}
	if (n != 0 && n > *s->max_spins)
		*s->max_spins = n;
}

/*
 * docs/re/spec/bmac-init.md, A8, "Exactly", exit: clear the word 0x14;
 * max_spins is not touched.
 */
void bcm4360_ol_sem_exit(const struct bcm4360_ol_sem *s)
{
	volatile u32 *sh = s->shared;

	if (!sh)
		return;
	sh[OL_SHARED_0x14] = OL_SEM_FREE;
}
