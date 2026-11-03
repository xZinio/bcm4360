// SPDX-License-Identifier: ISC
/*
 * Hybrid build: the channel and band functions of the MAC core in Broadcom's
 * core (wlc_bmac.c: wlc_bmac_set_chanspec with its band switch, wlc_setxband,
 * wlc_bmac_bw_set, wlc_bmac_bw_reset; the transmitter mute) by the open code
 * of open/mac/bmac_chan.c (docs/re/spec/acphy-chanspec.md sections 1-3,
 * docs/re/spec/bmac-init.md E4; task docs/re/tasks/bmac-chan.md). Part of the
 * MAC layer: switch open_mac.
 *
 * As for the other MAC functions the state goes in and out with
 * hyb_mac_in/hyb_mac_out (hybrid/mac.c), and the services of
 * open/include/bcm4360/mac_env.h are implemented here with Broadcom's
 * functions; around those that can run MAC code (the PHY, the MAC functions
 * not open yet) and around the change of the current band the open state is
 * written back first and read again after.
 *
 * Parameters of type bool of the object are bytes (u8); integer arguments
 * narrower than 32 bit arrive as u32 and are truncated.
 */
#include "hybrid.h"
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>

/* wlc_hw_info_t, wlc_info_t: docs/re/spec/bmac-init.md, acphy-chanspec.md */
#define WLC_HW_WLC		0x000	/* wlc_info_t * */
#define WLC_HW_INTSTATUS	0x094	/* pending interrupt bits (u32) */
#define WLC_HW_BAND		0x0e8	/* current band state */
#define WLC_HW_BANDSTATE	0x0f0	/* the band states of the units 0, 1 (8 bytes each) */
#define BAND_PI			0x028	/* the PHY of the band */
#define WLC_BAND		0x040	/* the upper layer's current band */
#define WLC_BANDSTATE		0x050	/* the upper layer's band states of the units 0, 1 */
#define WLC_FLAG_64		0x064	/* u8, bmac-init.md E4 step 1 */
#define NR_BANDS_MAX		2

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

HYB_TAKE(void, wlc_bmac_set_chanspec, (void *wlc_hw, u32 chanspec, u32 mute, void *limits));
HYB_TAKE(void, wlc_setxband, (void *wlc_hw, u32 unit));
HYB_TAKE(void, wlc_bmac_bw_set, (void *wlc_hw, u32 bw));
HYB_TAKE(void, wlc_bmac_bw_reset, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_mute, (void *wlc_hw, u32 on, u32 reason));

/* Broadcom's functions the services call (hybrid/takeover.txt: use, or takes
 * of other layers, which then run the open code of those layers) */
void wlc_phy_chanspec_radio_set(void *pi, u16 chanspec);
void wlc_phy_initcal_enable(void *pi, u8 on);
void wlc_phy_txpower_limit_set(void *pi, void *txpwr, u16 chanspec);
void wlc_phy_chanspec_set(void *pi, u16 chanspec);
u16 wlc_phy_chanspec_get(void *pi);
void wlc_phy_bw_state_set(void *pi, u16 bw);
void wlc_phy_mute_upd(void *pi, u8 mute, u32 flags);
void wlc_bmac_tx_fifo_suspend(void *wlc_hw, u32 fifo);
void wlc_bmac_tx_fifo_resume(void *wlc_hw, u32 fifo);

void wlc_bmac_set_chanspec(void *wlc_hw, u32 chanspec, u32 mute, void *limits)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac, mac0;

	if (!hyb_mac_on(HYB_MAC_CHAN)) {
		bcm4360_blob_wlc_bmac_set_chanspec(wlc_hw, chanspec, mute, limits);
		return;
	}
	hyb_mac_in(wlc_hw, &hw, &mac, &mac0);
	bcm4360_bmac_set_chanspec(&mac, (u16)chanspec, (u8)mute != 0, limits);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

void wlc_setxband(void *wlc_hw, u32 unit)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac, mac0;

	if (!hyb_mac_on(HYB_MAC_CHAN)) {
		bcm4360_blob_wlc_setxband(wlc_hw, unit);
		return;
	}
	hyb_mac_in(wlc_hw, &hw, &mac, &mac0);
	bcm4360_bmac_setxband(&mac, unit);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

void wlc_bmac_bw_set(void *wlc_hw, u32 bw)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac, mac0;

	if (!hyb_mac_on(HYB_MAC_CHAN)) {
		bcm4360_blob_wlc_bmac_bw_set(wlc_hw, bw);
		return;
	}
	hyb_mac_in(wlc_hw, &hw, &mac, &mac0);
	bcm4360_bmac_bw_set(&mac, (u16)bw);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

void wlc_bmac_bw_reset(void *wlc_hw)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac, mac0;

	if (!hyb_mac_on(HYB_MAC_CHAN)) {
		bcm4360_blob_wlc_bmac_bw_reset(wlc_hw);
		return;
	}
	hyb_mac_in(wlc_hw, &hw, &mac, &mac0);
	bcm4360_bmac_bw_reset(&mac);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

void wlc_bmac_mute(void *wlc_hw, u32 on, u32 reason)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac, mac0;

	if (!hyb_mac_on(HYB_MAC_CHAN)) {
		bcm4360_blob_wlc_bmac_mute(wlc_hw, on, reason);
		return;
	}
	hyb_mac_in(wlc_hw, &hw, &mac, &mac0);
	bcm4360_bmac_mute(&mac, (u8)on != 0, reason);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

/* --- the services of open/include/bcm4360/mac_env.h for the channel and band --- */

static void *pi_of(struct bcm4360_mac *mac)
{
	void *band = FIELD(mac->env, WLC_HW_BAND, void *);

	return band ? FIELD(band, BAND_PI, void *) : NULL;
}

static void *wlc_of(struct bcm4360_mac *mac)
{
	return FIELD(mac->env, WLC_HW_WLC, void *);
}

static void flush(struct bcm4360_mac *mac)
{
	hyb_mac_sync(mac);
}

static void reload(struct bcm4360_mac *mac)
{
	hyb_mac_in(mac->env, mac->hw, mac, mac->env_shadow);
}

void bcm4360_bmac_env_band_select(struct bcm4360_mac *mac, u32 unit)
{
	void *wlc = wlc_of(mac);

	if (unit >= NR_BANDS_MAX)
		return;
	flush(mac);
	FIELD(mac->env, WLC_HW_BAND, void *) =
		FIELD(mac->env, WLC_HW_BANDSTATE + 8 * unit, void *);
	FIELD(wlc, WLC_BAND, void *) = FIELD(wlc, WLC_BANDSTATE + 8 * unit, void *);
	reload(mac);
}

void bcm4360_bmac_env_band_phy_chanspec_radio_set(struct bcm4360_mac *mac, u32 unit,
						  u16 chanspec)
{
	void *band;

	if (unit >= NR_BANDS_MAX)
		return;
	band = FIELD(mac->env, WLC_HW_BANDSTATE + 8 * unit, void *);
	flush(mac);
	wlc_phy_chanspec_radio_set(band ? FIELD(band, BAND_PI, void *) : NULL, chanspec);
	reload(mac);
}

void bcm4360_bmac_env_phy_initcal_enable(struct bcm4360_mac *mac, bool on)
{
	flush(mac);
	wlc_phy_initcal_enable(pi_of(mac), on ? 1 : 0);
	reload(mac);
}

void bcm4360_bmac_env_phy_txpower_limit_set(struct bcm4360_mac *mac, void *limits,
					    u16 chanspec)
{
	flush(mac);
	wlc_phy_txpower_limit_set(pi_of(mac), limits, chanspec);
	reload(mac);
}

void bcm4360_bmac_env_phy_chanspec_set(struct bcm4360_mac *mac, u16 chanspec)
{
	flush(mac);
	wlc_phy_chanspec_set(pi_of(mac), chanspec);
	reload(mac);
}

u16 bcm4360_bmac_env_phy_chanspec_get(struct bcm4360_mac *mac)
{
	u16 r;

	flush(mac);
	r = wlc_phy_chanspec_get(pi_of(mac));
	reload(mac);
	return r;
}

void bcm4360_bmac_env_phy_bw_state_set(struct bcm4360_mac *mac, u16 bw)
{
	flush(mac);
	wlc_phy_bw_state_set(pi_of(mac), bw);
	reload(mac);
}

void bcm4360_bmac_env_phy_mute_upd(struct bcm4360_mac *mac, bool on, u32 reason)
{
	flush(mac);
	wlc_phy_mute_upd(pi_of(mac), on ? 1 : 0, reason);
	reload(mac);
}

void bcm4360_bmac_env_tx_fifo_suspend(struct bcm4360_mac *mac, u32 fifo)
{
	flush(mac);
	wlc_bmac_tx_fifo_suspend(mac->env, fifo);
	reload(mac);
}

void bcm4360_bmac_env_tx_fifo_resume(struct bcm4360_mac *mac, u32 fifo)
{
	flush(mac);
	wlc_bmac_tx_fifo_resume(mac->env, fifo);
	reload(mac);
}

u32 bcm4360_bmac_env_intstatus(struct bcm4360_mac *mac)
{
	return FIELD(mac->env, WLC_HW_INTSTATUS, u32);
}

void bcm4360_bmac_env_set_intstatus(struct bcm4360_mac *mac, u32 v)
{
	FIELD(mac->env, WLC_HW_INTSTATUS, u32) = v;
}

bool bcm4360_bmac_env_wlc_flag_64(struct bcm4360_mac *mac)
{
	return FIELD(wlc_of(mac), WLC_FLAG_64, u8) != 0;
}
