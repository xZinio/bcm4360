/* SPDX-License-Identifier: ISC */
/*
 * Hybrid bridge, layer "phy": what the bridge keeps per card. The open PHY
 * sees only `hw`; the bridge finds the object's handles from it (hw is the
 * first member) when the open PHY asks for a service.
 */
#ifndef HYBRID_PHY_CARD_H
#define HYBRID_PHY_CARD_H

#include <bcm4360/hw.h>

struct bcm4360_phy;

struct hybrid_phy_card {
	struct bcm4360_hw hw;		/* first: the open PHY's view of the card */
	void *physhim;			/* the object's PHY shim (wlapi_*) */
	void *sih;			/* the object's backplane handle (si_*) */
	void *osh;			/* the object's OS handle */
	char *vars;			/* the board variables (getvar) */
	struct bcm4360_phy *phy;	/* the open PHY of this card */
};

static inline struct hybrid_phy_card *hybrid_card(struct bcm4360_hw *hw)
{
	return (struct hybrid_phy_card *)hw;
}

/* hw_fw_data(): the object's data the open code asks for by name (fwdata.c) */
struct hybrid_fw {
	const char *name;
	const void *data;
	u32 size;
};

extern const struct hybrid_fw hybrid_fw_table[];

#endif
