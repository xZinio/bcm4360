// SPDX-License-Identifier: ISC
/*
 * The core's frame path: interrupts, receive, transmit, transmit status
 * (docs/re/spec/frames.md). State: struct core_frames (frames.h) in
 * struct bcm4360_core.
 */
#include "core.h"

void bcm4360_core_intrsoff(struct bcm4360_core *c)
{
	(void)core_intrsoff(c);
}

void bcm4360_core_intrson(struct bcm4360_core *c)
{
	core_intrsrestore(c, FIELD(CORE_WLC_HW(c), 0x09c, u32));
}

/* frames.md (pending): acknowledge and mask the MAC's interrupts */
bool bcm4360_core_isr(struct bcm4360_core *c)
{
	u32 st, mask;

	if (!c->up)
		return false;
	st = d11_read32(c->hw, D11_MACINTSTATUS);
	if (!st || st == 0xffffffff)
		return false;
	mask = FIELD(CORE_WLC_HW(c), HW_INTMASK, u32);
	if (!(st & mask))
		return false;
	d11_write32(c->hw, D11_MACINTSTATUS, st & mask);
	FIELD(CORE_WLC_HW(c), HW_INTSTATUS, u32) |= st & mask;
	d11_write32(c->hw, D11_MACINTMASK, 0);
	return true;
}

/* frames.md (pending): receive, transmit status; for now the bits are dropped */
bool bcm4360_core_dpc(struct bcm4360_core *c, u32 budget)
{
	(void)budget;
	FIELD(CORE_WLC_HW(c), HW_INTSTATUS, u32) = 0;
	d11_write32(c->hw, D11_MACINTMASK, FIELD(CORE_WLC_HW(c), HW_INTMASK, u32));
	return false;
}

/* frames.md (pending) */
int bcm4360_core_tx(struct bcm4360_core *c, void *pkt, const struct bcm4360_txparams *p)
{
	(void)c; (void)pkt; (void)p;
	return -1;
}

u32 bcm4360_core_tx_headroom(struct bcm4360_core *c)
{
	(void)c;
	return 0;
}
