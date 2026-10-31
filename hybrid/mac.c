// SPDX-License-Identifier: ISC
/*
 * Hybrid build: the control primitives of the MAC core in Broadcom's core
 * (wlc_bmac.c: the MAC control register with its overrides, the microcode
 * wake override, suspend/enable, the microcode host flags) by the open code
 * of open/mac/bmac_ctrl.c and open/mac/bmac_mhf.c (docs/re/spec/bmac-init.md
 * A1-A7 and A9).
 *
 * The open functions work on struct bcm4360_mac, a copy of the part of the
 * object's `wlc_hw` they use: it is filled from `wlc_hw` before the call and
 * written back after it, so the object's other functions keep seeing the
 * state they expect. The open functions call no other layer, so nothing else
 * can change that state in between. With the switch open_mac off the
 * object's own function runs (bcm4360_blob_*).
 *
 * Integer arguments narrower than 32 bit arrive as u32 and are truncated.
 */
#include "hybrid.h"
#include <bcm4360/mac.h>

/* wlc_hw_info_t (`wlc_hw`): docs/re/spec/bmac-init.md, "Data" and A9 */
#define WLC_HW_COREREV		0x084	/* MAC core revision */
#define WLC_HW_SIH		0x0b8	/* si_t * */
#define WLC_HW_REGS		0x0d0	/* registers of the 802.11 core */
#define WLC_HW_BAND		0x0e8	/* current band state */
#define WLC_HW_BAND_2G		0x0f0	/* band state of 2.4 GHz */
#define WLC_HW_BAND_5G		0x0f8	/* band state of 5 GHz */
#define WLC_HW_MACCONTROL	0x168	/* copy of D11(0x120) without the overrides (A1) */
#define WLC_HW_SUSPEND_DEPTH	0x16c	/* nesting of suspend/enable (A6, A7) */
#define WLC_HW_WAKE_OVERRIDE	0x170	/* reasons to keep the microcode awake (A3, A4) */
#define WLC_HW_MUTE_OVERRIDE	0x174	/* transmitter muted (A1) */
#define WLC_HW_CLK		0x186	/* the core's clock is on (u8) */
#define WLC_HW_FASTPWRUP_DLY	0x192	/* fast power-up delay, microseconds (u16, A5) */
/* band state: the host flag copies (A9) */
#define BAND_MHFS		0x008	/* u16 [5] */
#define BAND_MHFS_N		5

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

HYB_TAKE(void, wlc_bmac_mctrl, (void *wlc_hw, u32 mask, u32 val));
HYB_TAKE(void, wlc_ucode_wake_override_set, (void *wlc_hw, u32 bit));
HYB_TAKE(void, wlc_ucode_wake_override_clear, (void *wlc_hw, u32 bit));
HYB_TAKE(void, wlc_bmac_suspend_mac_and_wait, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_enable_mac, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_mhf, (void *wlc_hw, u32 idx, u32 mask, u32 val, u32 bands));

static void mac_in(void *wlc_hw, struct bcm4360_hw *hw, struct bcm4360_mac *mac)
{
	void *cur = FIELD(wlc_hw, WLC_HW_BAND, void *);
	void *b2g = FIELD(wlc_hw, WLC_HW_BAND_2G, void *);
	void *b5g = FIELD(wlc_hw, WLC_HW_BAND_5G, void *);
	const struct hyb_si_pub *sih = FIELD(wlc_hw, WLC_HW_SIH, void *);
	int i;

	hw_memset(hw, 0, sizeof(*hw));
	hw->d11 = FIELD(wlc_hw, WLC_HW_REGS, void *);
	hw_memset(mac, 0, sizeof(*mac));
	mac->hw = hw;
	mac->maccontrol = FIELD(wlc_hw, WLC_HW_MACCONTROL, u32);
	mac->mac_suspend_depth = FIELD(wlc_hw, WLC_HW_SUSPEND_DEPTH, u32);
	mac->wake_override = FIELD(wlc_hw, WLC_HW_WAKE_OVERRIDE, u32);
	mac->mute_override = FIELD(wlc_hw, WLC_HW_MUTE_OVERRIDE, u32);
	mac->fastpwrup_dly = FIELD(wlc_hw, WLC_HW_FASTPWRUP_DLY, u16);
	mac->clk = FIELD(wlc_hw, WLC_HW_CLK, u8);
	mac->mac_rev = (u8)FIELD(wlc_hw, WLC_HW_COREREV, u32);
	mac->chip_id = sih ? sih->chip : 0;
	for (i = 0; i < BAND_MHFS_N; i++) {
		mac->mhf[0][i] = b2g ? FIELD(b2g, BAND_MHFS + 2 * i, u16) : 0;
		mac->mhf[1][i] = b5g ? FIELD(b5g, BAND_MHFS + 2 * i, u16) : 0;
	}
	mac->current_band = (cur && cur == b5g) ? 1 : 0;
	mac->band_present = (b2g ? 1 : 0) | (b5g ? 2 : 0);
}

static void mac_out(void *wlc_hw, const struct bcm4360_mac *mac)
{
	void *b2g = FIELD(wlc_hw, WLC_HW_BAND_2G, void *);
	void *b5g = FIELD(wlc_hw, WLC_HW_BAND_5G, void *);
	int i;

	FIELD(wlc_hw, WLC_HW_MACCONTROL, u32) = mac->maccontrol;
	FIELD(wlc_hw, WLC_HW_SUSPEND_DEPTH, u32) = mac->mac_suspend_depth;
	FIELD(wlc_hw, WLC_HW_WAKE_OVERRIDE, u32) = mac->wake_override;
	FIELD(wlc_hw, WLC_HW_MUTE_OVERRIDE, u32) = mac->mute_override;
	for (i = 0; i < BAND_MHFS_N; i++) {
		if (b2g)
			FIELD(b2g, BAND_MHFS + 2 * i, u16) = mac->mhf[0][i];
		if (b5g)
			FIELD(b5g, BAND_MHFS + 2 * i, u16) = mac->mhf[1][i];
	}
}

/* bmac-init.md A2 */
void wlc_bmac_mctrl(void *wlc_hw, u32 mask, u32 val)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	if (!bcm4360_open_mac) {
		bcm4360_blob_wlc_bmac_mctrl(wlc_hw, mask, val);
		return;
	}
	mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_mctrl(&mac, mask, val);
	mac_out(wlc_hw, &mac);
}

/* bmac-init.md A3 */
void wlc_ucode_wake_override_set(void *wlc_hw, u32 bit)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	if (!bcm4360_open_mac) {
		bcm4360_blob_wlc_ucode_wake_override_set(wlc_hw, bit);
		return;
	}
	mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_wake_override_set(&mac, bit);
	mac_out(wlc_hw, &mac);
}

/* bmac-init.md A4 */
void wlc_ucode_wake_override_clear(void *wlc_hw, u32 bit)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	if (!bcm4360_open_mac) {
		bcm4360_blob_wlc_ucode_wake_override_clear(wlc_hw, bit);
		return;
	}
	mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_wake_override_clear(&mac, bit);
	mac_out(wlc_hw, &mac);
}

/* bmac-init.md A6 */
void wlc_bmac_suspend_mac_and_wait(void *wlc_hw)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	if (!bcm4360_open_mac) {
		bcm4360_blob_wlc_bmac_suspend_mac_and_wait(wlc_hw);
		return;
	}
	mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_suspend(&mac);
	mac_out(wlc_hw, &mac);
}

/* bmac-init.md A7 */
void wlc_bmac_enable_mac(void *wlc_hw)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	if (!bcm4360_open_mac) {
		bcm4360_blob_wlc_bmac_enable_mac(wlc_hw);
		return;
	}
	mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_enable(&mac);
	mac_out(wlc_hw, &mac);
}

/* bmac-init.md A9 */
void wlc_bmac_mhf(void *wlc_hw, u32 idx, u32 mask, u32 val, u32 bands)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	if (!bcm4360_open_mac) {
		bcm4360_blob_wlc_bmac_mhf(wlc_hw, idx, mask, val, bands);
		return;
	}
	mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_mhf(&mac, (u8)idx, (u16)mask, (u16)val, bands);
	mac_out(wlc_hw, &mac);
}
