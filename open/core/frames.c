// SPDX-License-Identifier: ISC
/*
 * The core's frame path: interrupts (docs/re/spec/frames.md, section 2).
 * Receive is in rx.c, transmit and transmit status in tx.c. State: struct
 * core_frames (frames.h) in struct bcm4360_core.
 */
#include "core.h"
#include <bcm4360/phy_api.h>

#define MI_ATIMWINEND	0x00000020
#define MI_PHYTXERR	0x00000800
#define MI_GP0		0x00002000
#define MI_DMAINT	0x00008000
#define MI_TXSTOP	0x00010000
#define MI_BG_NOISE	0x00040000
#define MI_RFDISABLE	0x10000000
#define MI_BT_PRED	0x00800000	/* the Bluetooth predictor (frames.md 2.2, coverage.md G3) */
#define MI_TFS		0x20000000

#define D11_MACCONTROL		0x120
#define D11_MACCOMMAND		0x124
#define HW_CLK			0x186
#define D11_PHYCTL_STATUS	0x158	/* frames.md 2.2, MI_RFDISABLE */
#define D11_DMA0_INTSTATUS	0x020
#define DMA_INT_RCV		0x00010000
#define HW_DEFINTMASK		0x09c
#define WLC_PENDING_CMD		0x14c

#define RX_BOUND		32	/* tunable word 14 (frames.md 1.1) */
#define TXSTATUS_BOUND		8	/* tunable word 15 (frames.md 4.1) */

void bcm4360_core_intrsoff(struct bcm4360_core *c)
{
	(void)core_intrsoff(c);
}

/* frames.md 2.5, wlc_intrson */
void bcm4360_core_intrson(struct bcm4360_core *c)
{
	core_intrsrestore(c, FIELD(CORE_WLC_HW(c), HW_DEFINTMASK, u32));
}

/* bmac-init.md C14, wlc_hw_deviceremoved: a removed card reads all ones */
static bool core_deviceremoved(struct bcm4360_core *c)
{
	void *hw = CORE_WLC_HW(c);
	u32 v;

	if (!FIELD(hw, HW_CLK, u8)) {
		v = hw_pcicfg_read(c->hw, 0, 4);
		return (v & 0xffff) == 0xffff;
	}
	v = d11_read32(c->hw, D11_MACCONTROL);
	return (v & 0x404) != 0x400;
}

/*
 * frames.md 2.3, wlc_intstatus: read the status, keep the bits of the mask,
 * mask everything, acknowledge. 0xffffffff: the card is gone.
 */
static u32 core_intstatus(struct bcm4360_core *c, bool in_isr)
{
	void *hw = CORE_WLC_HW(c);
	u32 v, mask, s;

	v = d11_read32(c->hw, D11_MACINTSTATUS);
	if (core_deviceremoved(c) || v == 0xffffffff)
		return 0xffffffff;
	mask = FIELD(hw, in_isr ? HW_INTMASK : HW_DEFINTMASK, u32);
	s = mask & v;
	if (!s)
		return 0;
	d11_write32(c->hw, D11_MACINTMASK, 0);
	(void)d11_read32(c->hw, D11_MACINTMASK);
	FIELD(hw, HW_INTMASK, u32) = 0;
	d11_write32(c->hw, D11_MACINTSTATUS, s);
	if (s & MI_DMAINT)
		d11_write32(c->hw, D11_DMA0_INTSTATUS, DMA_INT_RCV);
	return s;
}

/* frames.md 2.3, wlc_isr: true if there is work for the deferred handler */
bool bcm4360_core_isr(struct bcm4360_core *c)
{
	void *hw;
	u32 s;

	if (!c->up || !c->wlc)
		return false;
	hw = CORE_WLC_HW(c);
	if (!FIELD(hw, HW_INTMASK, u32))
		return false;
	s = core_intstatus(c, true);
	if (s == 0xffffffff) {
		FIELD(hw, HW_INTSTATUS, u32) = 0;
		return false;
	}
	if (!s)
		return false;
	FIELD(hw, HW_INTSTATUS, u32) = s;
	return true;
}

/* frames.md 2.2, MI_ATIMWINEND: the MAC command with the pending bits */
static void dpc_atimwinend(struct bcm4360_core *c)
{
	u32 cmd = MI_ATIMWINEND | FIELD(c->wlc, WLC_PENDING_CMD, u32);

	d11_write32(c->hw, D11_MACCOMMAND, cmd);
	FIELD(c->wlc, WLC_PENDING_CMD, u32) = 0;
}

/* frames.md 2.4, wlc_dpc steps 2 to 15 */
static bool dpc_bits(struct bcm4360_core *c, u32 s, u32 bound)
{
	void *hw = CORE_WLC_HW(c);
	bool more, fatal = false;

	if (s & MI_TFS) {
		more = core_txstatus_run(c, TXSTATUS_BOUND, &fatal);
		if (more)
			FIELD(hw, HW_INTSTATUS, u32) |= MI_TFS;
		if (fatal) {
			bcm4360_plat_fatal(c->plat);
			return false;
		}
	}
	if (s & MI_ATIMWINEND)
		dpc_atimwinend(c);
	if (s & MI_PHYTXERR)
		c->fr.txphyerr++;
	if (s & MI_DMAINT) {
		more = core_rx_run(c, bound);
		if (more)
			FIELD(hw, HW_INTSTATUS, u32) |= MI_DMAINT;
	}
	/* MI_TXSTOP: wlc-hw: the scan's and quiet period's "FIFOs suspended" callbacks */
	if ((s & MI_BG_NOISE) && c->phy)
		bcm4360_phy_noise_sample_intr(c->phy);
	/* MI_CCA, MI_TBTT, MI_DTIM_TBTT, MI_TO: no user here */
	if (s & MI_BT_PRED)
		core_btc_predictor(c);		/* coverage.md G3 */
	if (s & MI_GP0)
		bcm4360_plat_fatal(c->plat);
	/* coverage.md G4 (wlc_rfd_intr): D11(0x158) bit 16 at once; a change goes to the
	 * platform (rfkill), which drops the association, and the next watchdog takes the
	 * core down */
	if (s & MI_RFDISABLE)
		(void)core_radio_poll_hw(c);
	return true;
}

/*
 * frames.md 2.4, wlc_dpc and the glue around it: wlc_intrsupd before a
 * rescheduled run, the handlers by bit, wlc_intrson when nothing is left.
 */
bool bcm4360_core_dpc(struct bcm4360_core *c, u32 budget)
{
	void *hw;
	u32 s, t, bound = RX_BOUND;

	if (!c->up || !c->wlc)
		return false;
	hw = CORE_WLC_HW(c);
	if (budget && budget < bound)
		bound = budget;
	if (c->fr.resched) {
		t = core_intstatus(c, false);
		if (t != 0xffffffff)
			FIELD(hw, HW_INTSTATUS, u32) |= t;
	}
	if (core_deviceremoved(c)) {
		bcm4360_plat_fatal(c->plat);	/* the glue's wl_down */
		return false;
	}
	s = FIELD(hw, HW_INTSTATUS, u32);
	FIELD(hw, HW_INTSTATUS, u32) = 0;
	if (!dpc_bits(c, s, bound)) {
		c->fr.resched = false;
		return false;
	}
	if (FIELD(hw, HW_INTSTATUS, u32)) {
		c->fr.resched = true;
		return true;
	}
	c->fr.resched = false;
	bcm4360_core_intrson(c);
	return false;
}
