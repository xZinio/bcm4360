// SPDX-License-Identifier: ISC
/*
 * MAC control primitives of the 802.11 core (wlc_bmac.c): the software copy of
 * the MAC control register with its overrides, the microcode wake override,
 * and suspend/enable of the MAC core.
 *
 * Written from docs/re/spec/bmac-init.md, sections A1-A7, for MAC core
 * revision 42 (chip 0x4360, PCIe).
 */
#include <bcm4360/mac.h>
#include <bcm4360/access.h>

/* Direct 802.11-core registers used here (docs/re/spec/bmac-init.md). */
#define D11_MACCONTROL		0x120	/* MAC control register */
#define D11_MACINTSTATUS	0x128	/* MAC interrupt status register */

/*
 * Bits of the MAC control register D11(0x120)
 * (docs/re/spec/bmac-init.md, "Bits of the MAC control register").
 */
#define MCTL_EN_MAC		0x00000001	/* MAC enabled */
#define MCTL_INFRA		0x00020000	/* infrastructure mode */
#define MCTL_AP			0x00040000	/* access point mode */
#define MCTL_WAKE		0x04000000	/* microcode must stay awake */

/*
 * A reason to keep the microcode awake (docs/re/spec/bmac-init.md, "Fields of
 * wlc_hw ..."): suspend/enable use "MAC suspended".
 */
#define WAKE_REASON_MAC_SUSPENDED	0x04

/* The microcode state in SHM(0x40): 4 means asleep (A5). */
#define SHM_UCODE_STATE		0x40
#define UCODE_STATE_ASLEEP	4

/* Bit 0 of D11(0x128): "MAC suspended" (A6/A7). */
#define MACINTSTATUS_SUSPENDED	0x1

/* A 32-bit register that reads back all ones means the card is gone (A6). */
#define D11_CARD_GONE		0xffffffff

/* The SPINWAIT granularity and the delays of A5/A6, microseconds. */
#define SPINWAIT_STEP_US	10	/* one SPINWAIT round (A5 loop, A6 step 7) */
#define WAIT_FOR_WAKE_US	40	/* A5 step 1 */
#define SUSPEND_WAIT_US		83000U	/* A6 step 7: budget for "MAC suspended" */

/*
 * docs/re/spec/bmac-init.md, A1 (wlc_mctrl_write): write the MAC control
 * register from the software copy and the wake/mute overrides. No read.
 */
void bcm4360_bmac_mctrl_write(struct bcm4360_mac *mac)
{
	u32 v = mac->maccontrol;

	if (mac->wake_override != 0)
		v |= MCTL_WAKE;
	if (mac->mute_override != 0)
		v = (v & ~MCTL_AP) | MCTL_INFRA;
	d11_write32(mac->hw, D11_MACCONTROL, v);
}

/*
 * docs/re/spec/bmac-init.md, A5 (wlc_bmac_wait_for_wake): wait until the
 * microcode has left the sleep state (MAC revision 42 path). A delay of 40,
 * then SPINWAIT on the microcode state in SHM(0x40) with fastpwrup_dly as the
 * budget. With a budget of 0, or once the state is not "asleep", exactly one
 * read is made.
 */
static void wlc_bmac_wait_for_wake(struct bcm4360_mac *mac)
{
	u32 budget = mac->fastpwrup_dly;
	u32 waited = 0;

	hw_udelay(WAIT_FOR_WAKE_US);
	while (bcm4360_shm_read(mac->hw, SHM_UCODE_STATE) == UCODE_STATE_ASLEEP &&
	       waited < budget) {
		hw_udelay(SPINWAIT_STEP_US);
		waited += SPINWAIT_STEP_US;
	}
}

/*
 * docs/re/spec/bmac-init.md, A2 (wlc_bmac_mctrl): change the masked bits of the
 * software copy and write it (A1). No register access when nothing changes;
 * `val` is not limited to `mask`.
 */
void bcm4360_bmac_mctrl(struct bcm4360_mac *mac, u32 mask, u32 val)
{
	u32 new_maccontrol = (mac->maccontrol & ~mask) | val;

	if (new_maccontrol == mac->maccontrol)
		return;
	mac->maccontrol = new_maccontrol;
	bcm4360_bmac_mctrl_write(mac);
}

/*
 * docs/re/spec/bmac-init.md, A3 (wlc_ucode_wake_override_set): keep the
 * microcode awake. If it is awake already (any override set, or WAKE set in the
 * copy), only record the reason; otherwise set WAKE and wait for the wake (A5).
 */
void bcm4360_bmac_wake_override_set(struct bcm4360_mac *mac, u32 bit)
{
	if (mac->wake_override != 0 || (mac->maccontrol & MCTL_WAKE)) {
		mac->wake_override |= bit;
		return;
	}
	mac->wake_override = bit;
	bcm4360_bmac_mctrl_write(mac);
	wlc_bmac_wait_for_wake(mac);
}

/*
 * docs/re/spec/bmac-init.md, A4 (wlc_ucode_wake_override_clear): drop a reason.
 * When no reason is left and the copy does not want WAKE, write WAKE away (this
 * write is made even when `bit` was not set before the call).
 */
void bcm4360_bmac_wake_override_clear(struct bcm4360_mac *mac, u32 bit)
{
	mac->wake_override &= ~bit;
	if (mac->wake_override == 0 && !(mac->maccontrol & MCTL_WAKE))
		bcm4360_bmac_mctrl_write(mac);
}

/*
 * docs/re/spec/bmac-init.md, A6 (wlc_bmac_suspend_mac_and_wait): stop the MAC.
 * Calls nest: only the outermost call acts. The interrupt status bit is not
 * acknowledged here (A7 does that).
 */
void bcm4360_bmac_suspend(struct bcm4360_mac *mac)
{
	u32 waited = 0;

	/* step 1: nesting counter; only the outermost call acts */
	mac->mac_suspend_depth++;
	if (mac->mac_suspend_depth > 1)
		return;

	/* step 2: keep the microcode awake while the MAC is suspended */
	bcm4360_bmac_wake_override_set(mac, WAKE_REASON_MAC_SUSPENDED);

	/* step 3: MAC revisions < 13 with a 3-wire Bluetooth coexistence
	 * interface drive a GPIO here; MAC revision 42 does not. */

	/* step 4: a 32-bit read of all ones means the card is gone */
	if (d11_read32(mac->hw, D11_MACCONTROL) == D11_CARD_GONE)
		return;		/* a real driver would call wl_down() here */

	/* step 5 */
	if (d11_read32(mac->hw, D11_MACINTSTATUS) == D11_CARD_GONE)
		return;		/* a real driver would call wl_down() here */

	/* step 6: clear EN_MAC (one write if the copy had the bit) */
	bcm4360_bmac_mctrl(mac, MCTL_EN_MAC, 0);

	/* step 7: wait for the "MAC suspended" interrupt status bit */
	while ((d11_read32(mac->hw, D11_MACINTSTATUS) & MACINTSTATUS_SUSPENDED) == 0 &&
	       waited < SUSPEND_WAIT_US) {
		hw_udelay(SPINWAIT_STEP_US);
		waited += SPINWAIT_STEP_US;
	}

	/* step 8: a last read of each; the card-gone test is on D11(0x120) */
	(void)d11_read32(mac->hw, D11_MACINTSTATUS);
	if (d11_read32(mac->hw, D11_MACCONTROL) == D11_CARD_GONE)
		return;		/* a real driver would call wl_down() here */

	/* step 9: chips 0xd144 and 0x5357 also clear PSM_RUN here (mctrl(2, 0));
	 * chip 0x4360 does not. */
}

/*
 * docs/re/spec/bmac-init.md, A7 (wlc_bmac_enable_mac): start the MAC again.
 * Only the call that brings the nesting counter to 0 acts; it sets EN_MAC and
 * acknowledges "MAC suspended".
 */
void bcm4360_bmac_enable(struct bcm4360_mac *mac)
{
	/* step 1: nesting counter; only the call reaching 0 acts */
	mac->mac_suspend_depth--;
	if (mac->mac_suspend_depth != 0)
		return;

	/* step 2 */
	(void)d11_read32(mac->hw, D11_MACCONTROL);

	/* step 3: set EN_MAC (chips 0xd144 and 0x5357 use mask and value 3) */
	bcm4360_bmac_mctrl(mac, MCTL_EN_MAC, MCTL_EN_MAC);

	/* step 4: acknowledge "MAC suspended" */
	d11_write32(mac->hw, D11_MACINTSTATUS, MACINTSTATUS_SUSPENDED);

	/* step 5 */
	(void)d11_read32(mac->hw, D11_MACCONTROL);
	(void)d11_read32(mac->hw, D11_MACINTSTATUS);

	/* step 6 */
	bcm4360_bmac_wake_override_clear(mac, WAKE_REASON_MAC_SUSPENDED);

	/* step 7: MAC revisions < 13 with a 3-wire coexistence interface drive a
	 * GPIO here; MAC revision 42 does not. */
}
