// SPDX-License-Identifier: ISC
/*
 * Hybrid bridge, layer "mac": the control primitives of the MAC core in
 * Broadcom's object (wlc_bmac.c: the MAC control register with its
 * overrides, the microcode wake override, suspend/enable, the microcode host
 * flags) are done by the open code (open/mac/bmac_ctrl.c, open/mac/bmac_mhf.c,
 * written from docs/re/spec/bmac-init.md A1-A7 and A9).
 *
 * The open functions work on struct bcm4360_mac, a copy of the part of the
 * object's `wlc_hw` they use. The bridge fills it from `wlc_hw` before the
 * call and writes it back after, so the object's other functions keep seeing
 * the state they expect. The open functions call no other layer, so no other
 * state can change in between.
 *
 * Integer arguments are taken as u32 and truncated (see access.c).
 */
#include <bcm4360/mac.h>
#include "blob_abi.h"

static void mac_in(void *wlc_hw, struct bcm4360_hw *hw, struct bcm4360_mac *mac)
{
	void *cur = BLOB_PTR(wlc_hw, WLC_HW_BAND);
	void *b2g = BLOB_PTR(wlc_hw, WLC_HW_BAND_2G);
	void *b5g = BLOB_PTR(wlc_hw, WLC_HW_BAND_5G);
	void *sih = BLOB_PTR(wlc_hw, WLC_HW_SIH);
	int i;

	hw_memset(hw, 0, sizeof(*hw));
	hw->d11 = BLOB_PTR(wlc_hw, WLC_HW_REGS);
	hw_memset(mac, 0, sizeof(*mac));
	mac->hw = hw;
	mac->maccontrol = BLOB_U32(wlc_hw, WLC_HW_MACCONTROL);
	mac->mac_suspend_depth = BLOB_U32(wlc_hw, WLC_HW_SUSPEND_DEPTH);
	mac->wake_override = BLOB_U32(wlc_hw, WLC_HW_WAKE_OVERRIDE);
	mac->mute_override = BLOB_U32(wlc_hw, WLC_HW_MUTE_OVERRIDE);
	mac->fastpwrup_dly = BLOB_U16(wlc_hw, WLC_HW_FASTPWRUP_DLY);
	mac->clk = BLOB_FIELD(wlc_hw, WLC_HW_CLK, u8);
	mac->mac_rev = (u8)BLOB_U32(wlc_hw, WLC_HW_COREREV);
	mac->chip_id = sih ? BLOB_U32(sih, SIH_CHIP) : 0;
	for (i = 0; i < BAND_MHFS_N; i++) {
		mac->mhf[0][i] = b2g ? BLOB_U16(b2g, BAND_MHFS + 2 * i) : 0;
		mac->mhf[1][i] = b5g ? BLOB_U16(b5g, BAND_MHFS + 2 * i) : 0;
	}
	mac->current_band = (cur && cur == b5g) ? 1 : 0;
	mac->band_present = (b2g ? 1 : 0) | (b5g ? 2 : 0);
}

static void mac_out(void *wlc_hw, const struct bcm4360_mac *mac)
{
	void *b2g = BLOB_PTR(wlc_hw, WLC_HW_BAND_2G);
	void *b5g = BLOB_PTR(wlc_hw, WLC_HW_BAND_5G);
	int i;

	BLOB_U32(wlc_hw, WLC_HW_MACCONTROL) = mac->maccontrol;
	BLOB_U32(wlc_hw, WLC_HW_SUSPEND_DEPTH) = mac->mac_suspend_depth;
	BLOB_U32(wlc_hw, WLC_HW_WAKE_OVERRIDE) = mac->wake_override;
	BLOB_U32(wlc_hw, WLC_HW_MUTE_OVERRIDE) = mac->mute_override;
	for (i = 0; i < BAND_MHFS_N; i++) {
		if (b2g)
			BLOB_U16(b2g, BAND_MHFS + 2 * i) = mac->mhf[0][i];
		if (b5g)
			BLOB_U16(b5g, BAND_MHFS + 2 * i) = mac->mhf[1][i];
	}
}

void wlc_bmac_mctrl(void *wlc_hw, u32 mask, u32 val);
void wlc_ucode_wake_override_set(void *wlc_hw, u32 bit);
void wlc_ucode_wake_override_clear(void *wlc_hw, u32 bit);
void wlc_bmac_suspend_mac_and_wait(void *wlc_hw);
void wlc_bmac_enable_mac(void *wlc_hw);
void wlc_bmac_mhf(void *wlc_hw, u32 idx, u32 mask, u32 val, u32 bands);

/* bmac-init.md A2 */
void wlc_bmac_mctrl(void *wlc_hw, u32 mask, u32 val)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	mac_in(wlc_hw, &hw, &mac);
	bcm4360_mac_mctrl(&mac, mask, val);
	mac_out(wlc_hw, &mac);
}

/* bmac-init.md A3 */
void wlc_ucode_wake_override_set(void *wlc_hw, u32 bit)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	mac_in(wlc_hw, &hw, &mac);
	bcm4360_mac_wake_override_set(&mac, bit);
	mac_out(wlc_hw, &mac);
}

/* bmac-init.md A4 */
void wlc_ucode_wake_override_clear(void *wlc_hw, u32 bit)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	mac_in(wlc_hw, &hw, &mac);
	bcm4360_mac_wake_override_clear(&mac, bit);
	mac_out(wlc_hw, &mac);
}

/* bmac-init.md A6 */
void wlc_bmac_suspend_mac_and_wait(void *wlc_hw)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	mac_in(wlc_hw, &hw, &mac);
	bcm4360_mac_suspend(&mac);
	mac_out(wlc_hw, &mac);
}

/* bmac-init.md A7 */
void wlc_bmac_enable_mac(void *wlc_hw)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	mac_in(wlc_hw, &hw, &mac);
	bcm4360_mac_enable(&mac);
	mac_out(wlc_hw, &mac);
}

/* bmac-init.md A9 */
void wlc_bmac_mhf(void *wlc_hw, u32 idx, u32 mask, u32 val, u32 bands)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	mac_in(wlc_hw, &hw, &mac);
	bcm4360_mac_mhf(&mac, (u8)idx, (u16)mask, (u16)val, bands);
	mac_out(wlc_hw, &mac);
}
