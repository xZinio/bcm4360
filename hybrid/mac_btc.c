// SPDX-License-Identifier: ISC
/*
 * Hybrid build: the Bluetooth coexistence functions of the MAC core in
 * Broadcom's core (wlc_bmac.c: mode, wire, the switch, the channel told to
 * the Bluetooth side, the serial coexistence update) by the open code of
 * open/mac/bmac_btc.c (docs/re/spec/bmac-run.md R3; task
 * docs/re/tasks/bmac-btc.md). Part of the MAC layer: switch open_mac.
 *
 * As for the other MAC functions the state goes in and out with
 * hyb_mac_in/hyb_mac_out (hybrid/mac.c); the services of
 * open/include/bcm4360/mac_env.h for these functions are implemented here.
 *
 * Parameters of type bool of the object are bytes (u8); integer arguments
 * narrower than 32 bit arrive as u32 and are truncated.
 */
#include "hybrid.h"
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>

#define WLC_HW_WLC		0x000	/* wlc_info_t * */
#define WLC_HW_SIH		0x0b8	/* si_t * */
#define WLC_HW_BAND		0x0e8	/* current band state */
#define BAND_PI			0x028	/* the PHY of the band */

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

HYB_TAKE(s32, wlc_bmac_btc_mode_set, (void *wlc_hw, s32 mode));
HYB_TAKE(s32, wlc_bmac_btc_wire_set, (void *wlc_hw, s32 wire));
HYB_TAKE(s32, wlc_bmac_btc_wire_get, (void *wlc_hw));
HYB_TAKE(s32, wlc_bmac_set_btswitch, (void *wlc_hw, u32 sw));
HYB_TAKE(void, wlc_bmac_update_bt_chanspec, (void *wlc_hw, u32 chanspec, u32 scanning, u32 p4));
HYB_TAKE(void, wlc_bmac_seci_upd, (void *wlc_hw));

/* Broadcom's functions the services call (hybrid/takeover.txt: use, or takes
 * of other layers) */
void si_eci_notify_bt(void *sih, u32 mask, u32 val, u32 interrupt);
void si_seci_upd(void *sih, u32 enable);
s32 wlc_btc_mode_get(void *wlc);
void wlc_phy_set_femctrl_bt_wlan_ovrd(void *pi, s8 sw);

#define PROLOGUE(blob_call)						\
	struct bcm4360_hw hw;						\
	struct bcm4360_mac mac, mac0;						\
									\
	if (!hyb_mac_on(HYB_MAC_BTC)) {					\
		blob_call;						\
	}								\
	hyb_mac_in(wlc_hw, &hw, &mac, &mac0)

s32 wlc_bmac_btc_mode_set(void *wlc_hw, s32 mode)
{
	s32 r;

	PROLOGUE(return bcm4360_blob_wlc_bmac_btc_mode_set(wlc_hw, mode));
	r = bcm4360_bmac_btc_mode_set(&mac, mode);
	hyb_mac_out(wlc_hw, &mac, &mac0);
	return r;
}

s32 wlc_bmac_btc_wire_set(void *wlc_hw, s32 wire)
{
	s32 r;

	PROLOGUE(return bcm4360_blob_wlc_bmac_btc_wire_set(wlc_hw, wire));
	r = bcm4360_bmac_btc_wire_set(&mac, wire);
	hyb_mac_out(wlc_hw, &mac, &mac0);
	return r;
}

s32 wlc_bmac_btc_wire_get(void *wlc_hw)
{
	s32 r;

	PROLOGUE(return bcm4360_blob_wlc_bmac_btc_wire_get(wlc_hw));
	r = bcm4360_bmac_btc_wire_get(&mac);
	hyb_mac_out(wlc_hw, &mac, &mac0);
	return r;
}

s32 wlc_bmac_set_btswitch(void *wlc_hw, u32 sw)
{
	s32 r;

	PROLOGUE(return bcm4360_blob_wlc_bmac_set_btswitch(wlc_hw, sw));
	r = bcm4360_bmac_set_btswitch(&mac, (s8)sw);
	hyb_mac_out(wlc_hw, &mac, &mac0);
	return r;
}

void wlc_bmac_update_bt_chanspec(void *wlc_hw, u32 chanspec, u32 scanning, u32 p4)
{
	PROLOGUE(bcm4360_blob_wlc_bmac_update_bt_chanspec(wlc_hw, chanspec, scanning, p4);
		 return);
	bcm4360_bmac_update_bt_chanspec(&mac, (u16)chanspec, (u8)scanning != 0, (u8)p4 != 0);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

void wlc_bmac_seci_upd(void *wlc_hw)
{
	PROLOGUE(bcm4360_blob_wlc_bmac_seci_upd(wlc_hw); return);
	bcm4360_bmac_seci_upd(&mac);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

/* --- the services of open/include/bcm4360/mac_env.h for the coexistence --- */

static void *sih_of(struct bcm4360_mac *mac)
{
	return FIELD(mac->env, WLC_HW_SIH, void *);
}

static void *pi_of(struct bcm4360_mac *mac)
{
	void *band = FIELD(mac->env, WLC_HW_BAND, void *);

	return band ? FIELD(band, BAND_PI, void *) : NULL;
}

void bcm4360_bmac_env_eci_notify_bt(struct bcm4360_mac *mac, u32 mask, u32 val, u32 interrupt)
{
	si_eci_notify_bt(sih_of(mac), mask, val, interrupt);
}

void bcm4360_bmac_env_seci_upd(struct bcm4360_mac *mac, bool enable)
{
	si_seci_upd(sih_of(mac), enable ? 1 : 0);
}

s32 bcm4360_bmac_env_wlc_btc_mode(struct bcm4360_mac *mac)
{
	return wlc_btc_mode_get(FIELD(mac->env, WLC_HW_WLC, void *));
}

void bcm4360_bmac_env_phy_set_femctrl_bt_wlan_ovrd(struct bcm4360_mac *mac, s8 sw)
{
	hyb_mac_sync(mac);
	wlc_phy_set_femctrl_bt_wlan_ovrd(pi_of(mac), sw);
	hyb_mac_in(mac->env, mac->hw, mac, mac->env_shadow);
}
