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

/* --- for the initialisation (bmac-init.md B1-B12; task bmac-init) --- */

/* backplane and PMU */
u32 bcm4360_bmac_env_core_sflags(struct bcm4360_mac *mac, u32 mask, u32 val);
							/* si_core_sflags */
u16 bcm4360_bmac_env_clkctl_fast_pwrup_delay(struct bcm4360_mac *mac);
							/* si_clkctl_fast_pwrup_delay */
void bcm4360_bmac_env_pmu_rfldo(struct bcm4360_mac *mac, bool on);	/* si_pmu_rfldo */
u32 bcm4360_bmac_env_pmu_bb_vcofreq(struct bcm4360_mac *mac, u32 xtalfreq);
						/* si_pmu_get_bb_vcofreq(sih, osh, xtalfreq) */
void bcm4360_bmac_env_seci_init(struct bcm4360_mac *mac, u8 mode);	/* si_seci_init */
void bcm4360_bmac_env_eci_init(struct bcm4360_mac *mac);		/* si_eci_init */
void bcm4360_bmac_env_gci_init(struct bcm4360_mac *mac);		/* si_gci_init */
void bcm4360_bmac_env_gpioouten(struct bcm4360_mac *mac, u32 mask, u32 val, u8 priority);
							/* si_gpioouten */

/* the PHY of the current band */
void bcm4360_bmac_env_phy_chanspec_radio_set(struct bcm4360_mac *mac, u16 chanspec);
							/* wlc_phy_chanspec_radio_set */
void bcm4360_bmac_env_phy_cal_init(struct bcm4360_mac *mac);	/* wlc_phy_cal_init */
void bcm4360_bmac_env_phy_antsel_init(struct bcm4360_mac *mac, bool lut_init);
							/* wlc_phy_antsel_init */
void bcm4360_bmac_env_phy_init(struct bcm4360_mac *mac, u16 chanspec);	/* wlc_phy_init */

/* the DMA engine of a FIFO (the function slots of B1 step 30) */
void bcm4360_bmac_env_dma_txinit(struct bcm4360_mac *mac, u32 fifo);	/* slot 0x08 */
void bcm4360_bmac_env_dma_rxinit(struct bcm4360_mac *mac, u32 fifo);	/* slot 0xa0 */
void bcm4360_bmac_env_dma_rxfill(struct bcm4360_mac *mac, u32 fifo);	/* slot 0xd8 */

/* MAC functions of Broadcom's that are not open yet (they change the state:
 * the platform writes it back before and reads it again after) */
void bcm4360_bmac_env_setxband(struct bcm4360_mac *mac, u32 unit);	/* wlc_setxband */
void bcm4360_bmac_env_btc_mode_set(struct bcm4360_mac *mac, u32 mode);
							/* wlc_bmac_btc_mode_set */
void bcm4360_bmac_env_mute(struct bcm4360_mac *mac, bool on, u32 reason);	/* wlc_bmac_mute */

/* the glue and the upper layer */
u32 bcm4360_bmac_env_intrsoff(struct bcm4360_mac *mac);		/* wl_intrsoff */
void bcm4360_bmac_env_intrsrestore(struct bcm4360_mac *mac, u32 mask);	/* wl_intrsrestore */
/* the board variables (getvar on the card's variables, utils.md) */
const char *bcm4360_bmac_env_getvar(struct bcm4360_mac *mac, const char *name);
/* the interface's MAC address (B1 step 32.4: the 6 bytes at pub+0x08) */
void bcm4360_bmac_env_pub_etheraddr(struct bcm4360_mac *mac, u8 *addr);
/* B1 step 27: the byte wlc+0x718; if it is not 0 it is set to 0. Result: it
 * was not 0 */
bool bcm4360_bmac_env_wlc_take_flag_718(struct bcm4360_mac *mac);
/* B1 step 30: the 32-bit word wlc+0x68 := 0 */
void bcm4360_bmac_env_wlc_clear_68(struct bcm4360_mac *mac);

#endif
