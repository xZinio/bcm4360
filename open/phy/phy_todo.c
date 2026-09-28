// SPDX-License-Identifier: ISC
/*
 * Functions of the PHY that belong to tasks which are not done yet. The code
 * of the finished tasks calls them at the places where the specifications
 * say so; here they do nothing. The task that implements one of them takes
 * it out of this file.
 */
#include <bcm4360/phy.h>

/*
 * docs/re/spec/acphy-init.md, section 4 (sub_0a04c2,
 * wlc_phy_set_regtbl_on_pwron_acphy): task of the initialisation
 */
void bcm4360_phy_set_regtbl_on_pwron_acphy(struct bcm4360_phy *phy)
{
}

/*
 * docs/re/spec/acphy-chanspec.md, section 5 (sub_0a7089,
 * wlc_phy_chanspec_set_acphy): task of the channel
 */
void bcm4360_phy_chanspec_set_acphy(struct bcm4360_phy *phy, u16 chanspec)
{
}

/*
 * docs/re/spec/acphy-attach.md, "Scope" (sub_0b56ce,
 * wlc_phy_timercb_phycal): task of the calibrations
 */
void bcm4360_phy_timer_phycal(struct bcm4360_phy *phy)
{
}
