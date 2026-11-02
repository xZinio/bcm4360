/* SPDX-License-Identifier: ISC */
/*
 * What the MAC layer needs from its environment besides the registers of the
 * 802.11 core: the backplane (the core's wrapper, the crystal, GPIOs, other
 * cores' registers), the PHY, and the DMA engines. The platform implements
 * these; in the hybrid build they call Broadcom's functions (hybrid/mac_clk.c).
 *
 * Specification: docs/re/spec/bmac-init.md (where its procedures call
 * si_core_cflags, si_core_reset, wlc_phy_*, the DMA engines ...); the name of
 * the function each service stands for is in its comment.
 *
 * A service can let other code of the platform run (the PHY may call back
 * into the MAC layer), which may read and change the MAC state: the platform
 * writes struct bcm4360_mac back before such a service and reads it again
 * after it. So the caller must not keep copies of fields of struct
 * bcm4360_mac in local variables across a service call; read the field again.
 */
#ifndef BCM4360_MAC_ENV_H
#define BCM4360_MAC_ENV_H

#include <bcm4360/types.h>

struct bcm4360_mac;

/* backplane: the 802.11 core and its wrapper (spec `chip`) */
u32 bcm4360_bmac_env_core_cflags(struct bcm4360_mac *mac, u32 mask, u32 val);
							/* si_core_cflags */
bool bcm4360_bmac_env_core_iscoreup(struct bcm4360_mac *mac);	/* si_iscoreup */
void bcm4360_bmac_env_core_reset(struct bcm4360_mac *mac, u32 bits, u32 resetbits);
							/* si_core_reset */
void bcm4360_bmac_env_core_disable(struct bcm4360_mac *mac, u32 bits);	/* si_core_disable */
s32 bcm4360_bmac_env_clkctl_xtal(struct bcm4360_mac *mac, u32 what, bool on);
							/* si_clkctl_xtal */
u32 bcm4360_bmac_env_gpiocontrol(struct bcm4360_mac *mac, u32 mask, u32 val, u8 priority);
							/* si_gpiocontrol */
u32 bcm4360_bmac_env_corereg(struct bcm4360_mac *mac, u32 coreidx, u32 regoff, u32 mask,
			     u32 val);				/* si_corereg */
void bcm4360_bmac_env_chipcontrol_srom4360(struct bcm4360_mac *mac, bool on);
							/* si_chipcontrl_srom4360 */
bool bcm4360_bmac_env_bus_deviceremoved(struct bcm4360_mac *mac);	/* si_deviceremoved */

/* the PHY of the current band (its `pi`) */
void bcm4360_bmac_env_phy_hw_clk_state_upd(struct bcm4360_mac *mac, bool newstate);
							/* wlc_phy_hw_clk_state_upd */
u16 bcm4360_bmac_env_phy_clk_bwbits(struct bcm4360_mac *mac);	/* wlc_phy_clk_bwbits */
void bcm4360_bmac_env_phy_anacore(struct bcm4360_mac *mac, bool on);	/* wlc_phy_anacore */
void bcm4360_bmac_env_phy_switch_radio(struct bcm4360_mac *mac, bool on);
							/* wlc_phy_switch_radio */

/* the DMA engine of a FIFO (spec bmac-data; the function slots of A14) */
void bcm4360_bmac_env_dma_txreset(struct bcm4360_mac *mac, u32 fifo);	/* slot 0x10 */
void bcm4360_bmac_env_dma_rxreset(struct bcm4360_mac *mac, u32 fifo);	/* slot 0xa8 */

/*
 * The pending interrupt bits (wlc_hw+0x94) := 0 (A14 steps 4 and 11). Not a
 * field of struct bcm4360_mac: the interrupt handler writes it at any time.
 */
void bcm4360_bmac_env_clear_intstatus(struct bcm4360_mac *mac);

#endif
