// SPDX-License-Identifier: ISC
/*
 * Functions of the PHY that belong to tasks which are not done yet. The code
 * of the finished tasks calls them at the places where the specifications
 * say so; here they do nothing. The task that implements one of them takes
 * it out of this file.
 *
 * The tests leave the accesses of these leaf functions out of the comparison
 * (the list LEAVES in tools/re/phy_scenarios.py), so an empty body is enough
 * for the initialisation and the channel function to reach 0 differences.
 */
#include <bcm4360/phy.h>

/*
 * docs/re/spec/acphy-attach.md, "Scope" (sub_0b56ce,
 * wlc_phy_timercb_phycal): the phycal timer callback is now implemented in
 * open/phy/phy_cal.c.
 */

/*
 * docs/re/spec/acphy-rxgain.md: front end control, analog filters and
 * reciprocity (sub_0a6b0f, sub_09e378, sub_09eaf9, sub_0a4adc) are now
 * implemented in open/phy/phy_rxgain.c.
 */

/*
 * docs/re/spec/acphy-desense.md: receive gain control and desense are now
 * implemented in open/phy/phy_desense.c.
 */

/*
 * docs/re/spec/acphy-txpower.md: transmit power (transmit gain by index, the
 * closed-loop power-control set-up, the idle-TSSI measurement, the
 * transmit-calibration coefficient apply) is now implemented in
 * open/phy/phy_txpower.c.
 */
