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
u32 bcm4360_bmac_env_gpioouten(struct bcm4360_mac *mac, u32 mask, u32 val, u8 priority);
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

/* --- for up and down (bmac-init.md C2-C6, C8, C10, C15; task bmac-updown) --- */

/* backplane, PCIe bridge and PMU (specs chip, pmu) */
void bcm4360_bmac_env_survive_perst_war(struct bcm4360_mac *mac, bool reset, u32 sperst_mask,
					u32 sperst_val);	/* si_survive_perst_war */
bool bcm4360_bmac_env_ldo_war(struct bcm4360_mac *mac, u32 devid);	/* si_ldo_war */
void bcm4360_bmac_env_pmu_res_init(struct bcm4360_mac *mac);	/* si_pmu_res_init(sih, osh) */
void bcm4360_bmac_env_clkctl_init(struct bcm4360_mac *mac);	/* si_clkctl_init */
void bcm4360_bmac_env_pcie_ltr_war(struct bcm4360_mac *mac);	/* si_pcie_ltr_war */
s32 bcm4360_bmac_env_pci_fixcfg(struct bcm4360_mac *mac);	/* si_pci_fixcfg */
void bcm4360_bmac_env_pci_setup(struct bcm4360_mac *mac, u32 coremask);	/* si_pci_setup */
void bcm4360_bmac_env_pci_up(struct bcm4360_mac *mac);		/* si_pci_up */
void bcm4360_bmac_env_pci_down(struct bcm4360_mac *mac);	/* si_pci_down */
void bcm4360_bmac_env_seci_down(struct bcm4360_mac *mac);	/* si_seci_down */
u32 bcm4360_bmac_env_pmu_res_req_timer_clr(struct bcm4360_mac *mac);
							/* si_pmu_res_req_timer_clr */
void bcm4360_bmac_env_btcgpiowar(struct bcm4360_mac *mac);	/* si_btcgpiowar */
u32 bcm4360_bmac_env_gpioout(struct bcm4360_mac *mac, u32 mask, u32 val, u8 priority);
							/* si_gpioout */
u32 bcm4360_bmac_env_gpioled(struct bcm4360_mac *mac, u32 mask, u32 val);	/* si_gpioled */
u32 bcm4360_bmac_env_gpiopull(struct bcm4360_mac *mac, bool updown, u32 mask, u32 val);
							/* si_gpiopull */

/* the PHY of the current band */
void bcm4360_bmac_env_phy_por_inform(struct bcm4360_mac *mac);	/* wlc_phy_por_inform */
void bcm4360_bmac_env_phy_hw_state_upd(struct bcm4360_mac *mac, bool up);
							/* wlc_phy_hw_state_upd */
s32 bcm4360_bmac_env_phy_down(struct bcm4360_mac *mac);		/* wlc_phy_down */

/* the DMA engine of a FIFO (C8 sub_05fefa; only where the FIFO has an engine) */
void bcm4360_bmac_env_dma_txreclaim(struct bcm4360_mac *mac, u32 fifo, s32 range);
							/* slot 0x60 */
void bcm4360_bmac_env_dma_rxreclaim(struct bcm4360_mac *mac, u32 fifo);	/* slot 0xe0 */

/* MAC functions of Broadcom's that stay Broadcom's in this increment (the
 * platform writes the state back before and reads it again after) */
void bcm4360_bmac_env_4360_pcie2_war(struct bcm4360_mac *mac, u32 freq);
							/* wlc_bmac_4360_pcie2_war (C12) */
void bcm4360_bmac_env_ifsctl_edcrs_set(struct bcm4360_mac *mac, bool on);
							/* wlc_bmac_ifsctl_edcrs_set (E9) */

/* the glue */
void bcm4360_bmac_env_intrson(struct bcm4360_mac *mac);	/* wl_intrson */
/* wl_reset: runs Broadcom's wlc_reset, which resets the core through the MAC
 * layer (C8): the platform writes the state back before and reads it after */
u32 bcm4360_bmac_env_wl_reset(struct bcm4360_mac *mac);

/* the upper layer's state (C1-C8) */
bool bcm4360_bmac_env_pub_hw_up(struct bcm4360_mac *mac);	/* the byte pub+0x40 is not 0 */
void bcm4360_bmac_env_pub_set_hw_up(struct bcm4360_mac *mac);	/* the byte pub+0x40 := 1 */
bool bcm4360_bmac_env_wlc_has_offload(struct bcm4360_mac *mac);	/* the word wlc+0x750 is not 0 */
/* the first 32-bit word of the structure at wlc+0x38 (the MAC's core index, C3) */
u32 bcm4360_bmac_env_wlc_mac_coreidx(struct bcm4360_mac *mac);
/* C8 sub_05fefa: the 16-bit word +0x38 + 2 * fifo of the structure at wlc+0x38 := 0 */
void bcm4360_bmac_env_wlc_clear_txpktpend(struct bcm4360_mac *mac, u32 fifo);
/* C4: the 16-bit PHY type at +0x08 of the upper layer's band (the structure at wlc+0x40) */
u16 bcm4360_bmac_env_wlc_band_phytype(struct bcm4360_mac *mac);
/* C8: the 32-bit counter +0xb4 of the counter block at pub+0xa0 += 1 */
void bcm4360_bmac_env_count_reset(struct bcm4360_mac *mac);
/* C5: the current interrupt mask wlc_hw+0x98 := 0. Not a field of struct
 * bcm4360_mac: the interrupt code writes it at any time. */
void bcm4360_bmac_env_clear_intmask(struct bcm4360_mac *mac);

/* the LED state of Broadcom's LED code (wlc_hw+0x198; C2 step 9) */
/* entry i (0..31) of the LED table: the first 32-bit word (`pin`) and the byte
 * +4 (`flag`) of the 24-byte entry at +8 + 24 * i */
void bcm4360_bmac_env_led_entry(struct bcm4360_mac *mac, u32 i, u32 *pin, u8 *flag);
/* the 32-bit words +0x308 := val and +0x30c := mask */
void bcm4360_bmac_env_led_gpio_store(struct bcm4360_mac *mac, u32 val, u32 mask);

/* --- for the channel and the band (acphy-chanspec.md 1-3, bmac-init.md E4; task
 * bmac-chan) --- */

/*
 * acphy-chanspec.md section 2 step 1: band `unit` (0 = 2.4 GHz, 1 = 5 GHz)
 * becomes the current band of the MAC and of the upper layer. The platform
 * writes the state back before and reads it again after: every field of the
 * current band (`current_band`, `band_*`, `cwmin`, `cwmax`) then describes
 * the new band.
 */
void bcm4360_bmac_env_band_select(struct bcm4360_mac *mac, u32 unit);
/* wlc_phy_chanspec_radio_set on the PHY of band `unit`, which need not be the
 * current band (section 1 step 3.1) */
void bcm4360_bmac_env_band_phy_chanspec_radio_set(struct bcm4360_mac *mac, u32 unit,
						  u16 chanspec);
/* the PHY of the current band */
void bcm4360_bmac_env_phy_initcal_enable(struct bcm4360_mac *mac, bool on);
							/* wlc_phy_initcal_enable */
void bcm4360_bmac_env_phy_txpower_limit_set(struct bcm4360_mac *mac, void *limits,
					    u16 chanspec);	/* wlc_phy_txpower_limit_set */
void bcm4360_bmac_env_phy_chanspec_set(struct bcm4360_mac *mac, u16 chanspec);
							/* wlc_phy_chanspec_set */
u16 bcm4360_bmac_env_phy_chanspec_get(struct bcm4360_mac *mac);	/* wlc_phy_chanspec_get */
void bcm4360_bmac_env_phy_bw_state_set(struct bcm4360_mac *mac, u16 bw);
							/* wlc_phy_bw_state_set */
void bcm4360_bmac_env_phy_mute_upd(struct bcm4360_mac *mac, bool on, u32 reason);
							/* wlc_phy_mute_upd */
/* MAC functions of Broadcom's not open yet (bmac-data section 2): the platform
 * writes the state back before and reads it again after */
void bcm4360_bmac_env_tx_fifo_suspend(struct bcm4360_mac *mac, u32 fifo);
							/* wlc_bmac_tx_fifo_suspend */
void bcm4360_bmac_env_tx_fifo_resume(struct bcm4360_mac *mac, u32 fifo);
							/* wlc_bmac_tx_fifo_resume */
/* the pending interrupt bits wlc_hw+0x94 (section 1 step 3.8; not a field:
 * the interrupt handler writes them at any time) */
u32 bcm4360_bmac_env_intstatus(struct bcm4360_mac *mac);
void bcm4360_bmac_env_set_intstatus(struct bcm4360_mac *mac, u32 v);
/* E4 step 1: the byte wlc+0x64 is not 0 */
bool bcm4360_bmac_env_wlc_flag_64(struct bcm4360_mac *mac);

/* --- at run time (bmac-run.md R1, R2; task bmac-runtime) --- */

/* the PHY of the current band (R1 step 4) */
void bcm4360_bmac_env_phy_scramb_dyn_bw_en(struct bcm4360_mac *mac, bool on);
							/* wlc_acphy_set_scramb_dyn_bw_en */
/* the DMA engine of a transmit FIFO (R2; only where the FIFO has an engine,
 * the suspended query answers false for one without) */
void bcm4360_bmac_env_dma_txsuspend(struct bcm4360_mac *mac, u32 fifo);	/* slot 0x20 */
void bcm4360_bmac_env_dma_txresume(struct bcm4360_mac *mac, u32 fifo);	/* slot 0x28 */
bool bcm4360_bmac_env_dma_txsuspended(struct bcm4360_mac *mac, u32 fifo);	/* slot 0x30 */

/* --- Bluetooth coexistence (bmac-run.md R3; task bmac-btc) --- */

/* backplane (spec chip) */
void bcm4360_bmac_env_eci_notify_bt(struct bcm4360_mac *mac, u32 mask, u32 val, u32 interrupt);
							/* si_eci_notify_bt */
void bcm4360_bmac_env_seci_upd(struct bcm4360_mac *mac, bool enable);	/* si_seci_upd */
/* the upper layer's coexistence mode (wlc_btc_mode_get(wlc)) */
s32 bcm4360_bmac_env_wlc_btc_mode(struct bcm4360_mac *mac);
/* the PHY of the current band */
void bcm4360_bmac_env_phy_set_femctrl_bt_wlan_ovrd(struct bcm4360_mac *mac, s8 sw);
							/* wlc_phy_set_femctrl_bt_wlan_ovrd */

#endif
