// SPDX-License-Identifier: ISC
/*
 * Hybrid build: the cards, and the switches of the layers.
 *
 * Broadcom's si_attach() makes the handle of a card's backplane (si_t) from
 * the mapped BAR0 window. It is taken over here to note the window: the open
 * code reaches the registers through it (struct bcm4360_hw). The object's own
 * si_attach() then runs as before.
 */
#include "hybrid.h"

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_pmu = true;
bool bcm4360_open_access = true;
bool bcm4360_open_mac = true;
unsigned int bcm4360_open_mac_mask = 0x1ff;
bool bcm4360_open_utils = true;
bool bcm4360_open_clm = true;

#ifdef __KERNEL__
module_param_named(open_pmu, bcm4360_open_pmu, bool, 0444);
MODULE_PARM_DESC(open_pmu, "PMU and clocks: open code (1, default) or Broadcom's (0)");
module_param_named(open_access, bcm4360_open_access, bool, 0444);
MODULE_PARM_DESC(open_access, "PHY/radio/table/shared-memory register access: open code (1, default) or Broadcom's (0)");
module_param_named(open_mac, bcm4360_open_mac, bool, 0444);
MODULE_PARM_DESC(open_mac, "MAC control, host flags, object memories, clocks, core reset, initialisation: open code (1, default) or Broadcom's (0)");
module_param_named(open_mac_mask, bcm4360_open_mac_mask, uint, 0444);
MODULE_PARM_DESC(open_mac_mask, "groups of the MAC layer that run open code (bits: 1 control, 2 object memories, 4 clocks, 8 init, 0x10 up/down, 0x20 channel, 0x40 run time, 0x80 coexistence, 0x100 offload semaphore; default 0x1ff)");
module_param_named(open_utils, bcm4360_open_utils, bool, 0444);
MODULE_PARM_DESC(open_utils, "board variables, number parsing, CRC-8: open code (1, default) or Broadcom's (0)");
module_param_named(open_clm, bcm4360_open_clm, bool, 0444);
MODULE_PARM_DESC(open_clm, "regulatory database lookup: open code (1, default) or Broadcom's (0)");
#endif

#define HYB_CARDS		4
/* a slot whose si_attach() is running: the handle is not known yet */
#define HYB_SIH_PENDING		((const struct hyb_si_pub *)1)

static struct hyb_card hyb_cards[HYB_CARDS];

HYB_TAKE(void *, si_attach, (u32 devid, void *osh, void *regs, u32 bustype, void *sdh,
			     char **vars, u32 *varsz));
HYB_TAKE(void, si_detach, (void *sih));

struct hyb_card *hyb_card_of(const void *sih)
{
	struct hyb_card *pending = NULL;
	int i;

	for (i = 0; i < HYB_CARDS; i++) {
		if (hyb_cards[i].sih == sih)
			return &hyb_cards[i];
		if (hyb_cards[i].sih == HYB_SIH_PENDING)
			pending = &hyb_cards[i];
	}
	/*
	 * si_attach() of the object calls into the layers before it returns the
	 * new handle: the first handle seen while a slot is pending is the new
	 * card's.
	 */
	if (pending && sih)
		pending->sih = sih;
	return pending;
}

void *si_attach(u32 devid, void *osh, void *regs, u32 bustype, void *sdh, char **vars,
		u32 *varsz)
{
	struct hyb_card *c = NULL;
	void *sih;
	int i;

	for (i = 0; i < HYB_CARDS && !c; i++) {
		if (!hyb_cards[i].sih)
			c = &hyb_cards[i];
	}
	if (c) {
		hw_memset(c, 0, sizeof(*c));
		c->sih = HYB_SIH_PENDING;
		c->osh = osh;
		c->hw.bar0 = regs;
		c->hw.pdev = osh;		/* osl_pci_read_config() takes the OS handle */
		c->hw.d11 = regs;		/* the movable window */
		c->hw.wrap = (u8 *)regs + HYB_BAR0_WRAP;
		c->hw.pcie = (u8 *)regs + HYB_BAR0_PCIE;
		c->hw.cc = (u8 *)regs + HYB_BAR0_CC;
	}
	sih = bcm4360_blob_si_attach(devid, osh, regs, bustype, sdh, vars, varsz);
	if (c) {
		if (sih)
			c->sih = sih;
		else
			c->sih = NULL;
	}
	return sih;
}

void si_detach(void *sih)
{
	struct hyb_card *c = hyb_card_of(sih);

	bcm4360_blob_si_detach(sih);
	hyb_pmu_forget(sih);
	if (c)
		c->sih = NULL;
}
