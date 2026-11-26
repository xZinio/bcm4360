// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hybrid build: a guard in front of the object's wlc_recvamsdu().
 *
 * The hardware splits a received A-MSDU into several receive buffers: a
 * first one, intermediate ones and a last one, told apart by the aggregation
 * type in the second receive status word. wlc_recvamsdu() chains them again
 * and hands the chain on when the last buffer arrives. It does not check
 * that the buffers it chains belong to the same MPDU.
 *
 * Under load the hardware drops receive buffers. The first buffer of a
 * damaged MPDU can then be followed by the intermediate and last buffers of
 * a later, good MPDU. The object accepts that chain with the receive status
 * of its last buffer, which is good, although the first buffer (802.11
 * header, CCMP header) failed its checks. The packet number of the damaged
 * CCMP header becomes the replay counter of the TID. If a high bit is
 * flipped in it, every later frame of that TID counts as a replay and is
 * dropped until the next rekey: the link stays associated and receives
 * nothing. A low bit costs some dozen frames.
 *
 * Bits 15:10 of the second receive status word count the receive buffers
 * (found on the card: consecutive over a thousand buffers but for the one
 * that started a wrong chain). An intermediate or last buffer that does not
 * directly follow the buffer before it, while a chain is open, is a wrong
 * chain: the guard takes the object's reassembly out of the state "first
 * buffer seen", which makes the object free the open chain and this buffer.
 *
 * Switch amsdu_guard (default on). amsdu_broken_chains counts the chains
 * dropped.
 */
#include "hybrid.h"

#ifdef __KERNEL__
#include <linux/atomic.h>
#include <linux/module.h>
#endif

/* the d11 receive header: the second receive status word (RxStatus2) */
#define RXH_RXSTATUS2		0x12
#define RXS2_AGGTYPE(s2)	(((s2) & 0x0006) >> 1)
#define RXS2_AGG_INTERMEDIATE	0
#define RXS2_AGG_LAST		2
#define RXS2_BUFIDX(s2)		((s2) >> 10)
#define RXS2_BUFIDX_MASK	0x3f

/* the object's A-MSDU module (amsdu_info_t): the state of the reassembly */
#define AMI_DEAGG_STATE		0x68
#define AMI_DEAGG_FIRST		1	/* a first buffer is chained, more to come */
#define AMI_PUB			0x08	/* wlc_pub_t *, also used by the object */
#define PUB_SIH			0x100	/* wlc_pub_t::sih, as in hybrid/stf.c */

bool bcm4360_amsdu_guard = true;

#ifdef __KERNEL__
static atomic_t broken_chains = ATOMIC_INIT(0);

static int amsdu_broken_chains_get(char *buf, const struct kernel_param *kp)
{
	unsigned int count = (unsigned int)atomic_read(&broken_chains);
	struct kernel_param value = { .arg = &count };

	return param_get_uint(buf, &value);
}

/* read only: parse_args() calls .set for a value given at load time unchecked */
static int amsdu_broken_chains_set(const char *val, const struct kernel_param *kp)
{
	return -EPERM;
}

static const struct kernel_param_ops broken_chains_ops = {
	.set = amsdu_broken_chains_set,
	.get = amsdu_broken_chains_get,
};

module_param_named(amsdu_guard, bcm4360_amsdu_guard, bool, 0444);
MODULE_PARM_DESC(amsdu_guard, "drop received A-MSDUs with missing receive buffers instead of joining buffers of different frames (1, default)");
module_param_cb(amsdu_broken_chains, &broken_chains_ops, NULL, 0444);
MODULE_PARM_DESC(amsdu_broken_chains, "received A-MSDUs dropped by amsdu_guard");
#else
/* The freestanding test build does not have kernel atomics. */
unsigned int bcm4360_amsdu_broken_chains;
#endif

HYB_TAKE(void, wlc_recvamsdu, (void *ami, void *rxh, void *p));

void wlc_recvamsdu(void *ami, void *rxh, void *p)
{
	const u8 *h = rxh;
	u16 s2 = (u16)(h[RXH_RXSTATUS2] | h[RXH_RXSTATUS2 + 1] << 8);
	u32 agg = RXS2_AGGTYPE(s2);
	u8 idx = RXS2_BUFIDX(s2);
	u32 *state = (u32 *)((u8 *)ami + AMI_DEAGG_STATE);
	void *pub = *(void **)((u8 *)ami + AMI_PUB);
	void *sih = *(void **)((u8 *)pub + PUB_SIH);
	struct hyb_card *card = hyb_card_of(sih);
	bool follows = card && card->amsdu_owner == ami && card->amsdu_last_valid &&
		idx == ((card->amsdu_last_idx + 1) & RXS2_BUFIDX_MASK);

	if (bcm4360_amsdu_guard && *state == AMI_DEAGG_FIRST &&
	    (agg == RXS2_AGG_INTERMEDIATE || agg == RXS2_AGG_LAST) &&
	    !follows) {
		*state = 0;	/* any state but "first": the object frees the chain and p */
#ifdef __KERNEL__
		atomic_inc(&broken_chains);
#else
		bcm4360_amsdu_broken_chains++;
#endif
	}
	if (card) {
		card->amsdu_owner = ami;
		card->amsdu_last_idx = idx;
		card->amsdu_last_valid = true;
	}
	bcm4360_blob_wlc_recvamsdu(ami, rxh, p);
}
