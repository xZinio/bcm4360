// SPDX-License-Identifier: ISC
/*
 * System suspend: the part of wl_suspend of Broadcom's glue
 * (src/wl/sys/wl_linux.c, ISC; no offload engine) after its wl_down.
 * mac80211 has already stopped the device (bcm4360_core_down) when the PCI
 * suspend runs.
 */
#include "core.h"

void bcm4360_core_suspend(struct bcm4360_core *c)
{
	/* the resume's up powers the hardware up again (wlc_up: pub->hw_up
	 * clear -> wlc_bmac_hw_up, wlchw.c), as after attach */
	FIELD(c->pub, PUB_HW_UP, u8) = 0;
	/* chip.md: si_pci_sleep (no effect on this card's PCIe Gen2 core,
	 * revision 3 and later) */
	core_si_pci_sleep(c);
}
