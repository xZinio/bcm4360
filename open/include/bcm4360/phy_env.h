/* SPDX-License-Identifier: ISC */
/*
 * What the PHY code needs from the other layers of the driver: services of
 * the MAC layer, of the chip layer (backplane, OTP) and timers.
 *
 * The PHY code calls these functions; it does not implement them. In a driver
 * the MAC and chip layers do. In the comparison tests (tools/re/ab.py) they
 * are replaced by recorders: a call is noted with its arguments at its place
 * between the register accesses and compared with the call the object makes
 * at that place; results are those the object got. So for the tests the
 * calls, their arguments and their order are part of the behaviour of the
 * PHY code, and what happens inside a service is not.
 *
 * The specifications name the functions of the object; the comment of each
 * declaration says which one it stands for.
 */
#ifndef BCM4360_PHY_ENV_H
#define BCM4360_PHY_ENV_H

#include <bcm4360/types.h>
#include <bcm4360/hw.h>

/*
 * MAC layer
 */

/* wlapi_suspend_mac_and_wait, wlapi_enable_mac: the calls nest */
void bcm4360_mac_suspend(struct bcm4360_hw *hw);
void bcm4360_mac_enable(struct bcm4360_hw *hw);

/* wlapi_bmac_corereset: reset of the 802.11 core with the given core flags */
void bcm4360_mac_corereset(struct bcm4360_hw *hw, u32 flags);

/*
 * wlapi_bmac_bw_set: bandwidth of the PHY clock, bw = the bandwidth bits of a
 * chanspec. The MAC layer tells the PHY the new bandwidth before it changes
 * the clock; in the tests nobody does, the PHY code has to keep its own
 * record of the bandwidth before it calls this.
 */
void bcm4360_mac_bw_set(struct bcm4360_hw *hw, u16 bw);

/* wlapi_bmac_phyclk_fgc: force the clock of the PHY on (gating off) */
void bcm4360_mac_phyclk_fgc(struct bcm4360_hw *hw, bool on);

/* wlapi_bmac_mhf: change bits of a host flag word; bands: 1, 2 or 3 (both) */
void bcm4360_mac_mhf(struct bcm4360_hw *hw, u8 idx, u16 mask, u16 val, u32 bands);

/* wlapi_bmac_mctrl: change bits of the MAC control register */
void bcm4360_mac_mctrl(struct bcm4360_hw *hw, u32 mask, u32 val);

/* wlapi_bmac_btc_mode_get: mode of the Bluetooth coexistence */
u32 bcm4360_mac_btc_mode(struct bcm4360_hw *hw);

/* wlapi_bmac_ucode_wake_override_phyreg_set, .._clear */
void bcm4360_mac_wake_override_set(struct bcm4360_hw *hw);
void bcm4360_mac_wake_override_clear(struct bcm4360_hw *hw);

/* wlapi_update_bt_chanspec */
void bcm4360_mac_update_bt_chanspec(struct bcm4360_hw *hw, u16 chanspec, bool scan,
				    bool measure);

/* wlapi_high_update_phy_mode */
void bcm4360_mac_update_phy_mode(struct bcm4360_hw *hw, u32 mode);

/* wlapi_high_update_txppr_offset: the power offsets per rate have changed */
void bcm4360_mac_update_txppr_offset(struct bcm4360_hw *hw, const void *offsets);

/*
 * Timers: wlapi_init_timer, wlapi_add_timer, wlapi_del_timer,
 * wlapi_free_timer. A timer calls fn(arg) in a context in which the hardware
 * may be accessed.
 */
void *bcm4360_timer_init(struct bcm4360_hw *hw, void (*fn)(void *arg), void *arg,
			 const char *name);
void bcm4360_timer_add(struct bcm4360_hw *hw, void *timer, u32 ms, bool periodic);
bool bcm4360_timer_del(struct bcm4360_hw *hw, void *timer);	/* true: it was pending */
void bcm4360_timer_free(struct bcm4360_hw *hw, void *timer);

/*
 * Chip layer
 */

/*
 * si_core_sflags, si_core_cflags: status and control flags of the 802.11
 * core in its wrapper: new = (old & ~mask) | val, written only if mask or
 * val is not 0; result: the flags after that.
 */
u32 bcm4360_chip_core_sflags(struct bcm4360_hw *hw, u32 mask, u32 val);
u32 bcm4360_chip_core_cflags(struct bcm4360_hw *hw, u32 mask, u32 val);

/*
 * si_corereg: a register of any core by the index of the core and the
 * offset of the register: new = (old & ~mask) | val, written only if mask or
 * val is not 0; result: the register read after that.
 */
u32 bcm4360_chip_corereg(struct bcm4360_hw *hw, u32 coreidx, u32 offset, u32 mask, u32 val);

/* si_gpiocontrol, si_gpioout, si_gpioouten */
u32 bcm4360_chip_gpiocontrol(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority);
u32 bcm4360_chip_gpioout(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority);
u32 bcm4360_chip_gpioouten(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority);

/* si_get_sromctl, si_set_sromctl: the SROM control register of ChipCommon */
u32 bcm4360_chip_sromctl_get(struct bcm4360_hw *hw);
void bcm4360_chip_sromctl_set(struct bcm4360_hw *hw, u32 val);

/* otp_read_word: word `word` of the OTP memory; result 0, or an error below 0 */
int bcm4360_otp_read_word(struct bcm4360_hw *hw, u32 word, u16 *data);

#endif
