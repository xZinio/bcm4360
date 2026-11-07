/*
 * Bluetooth coexistence of the MAC core (wlc_bmac.c): the coexistence mode
 * and the interface type ("wire"), the switch between the Bluetooth and the
 * WLAN front end, the WLAN channel told to the Bluetooth side through the
 * ECI, and the serial coexistence interface (SECI) update.
 *
 * Written from docs/re/spec/bmac-run.md, section R3, with its predicates
 * P_eci, P_seci, P_cap, BTC_HW and its "Data", and the primitives of
 * docs/re/spec/bmac-init.md A6, A7 (open in bmac_ctrl.c) and A9 (bmac_mhf.c),
 * for MAC core revision 42 with an AC-PHY (chip 0x4360, Apple board, PCIe).
 * The two helpers of R3 sub_062b79 and sub_06106b are in bmac_init.c.
 * Everything outside the 802.11 core's registers goes through the services
 * of bcm4360/mac_env.h. A service may change struct bcm4360_mac (the PHY can
 * run MAC code), so no field of *mac is kept in a local variable across a
 * service call.
 */
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>
#include <bcm4360/access.h>

/* Direct 802.11-core registers used here (bmac-init.md "Data"). */
#define D11_MACCONTROL		0x120	/* MAC control register (R3 btc_mode_set step 4) */

/* Bit 0 of D11(0x120): EN_MAC, the MAC is enabled (bmac-init.md "Data"). */
#define MCTL_EN_MAC		0x00000001

/* Results (R3). */
#define BTC_OK			0
#define BTC_E_RANGE		(-2)	/* mode > 8 or wire > 4 */
#define BTC_E_NO_HW		(-3)	/* btc_mode_set step 3: the wire needs hardware not there */
#define BTC_E_NOTUP		(-4)	/* set_btswitch: the interface is down */
#define BTC_E_UNSUPPORTED	(-23)	/* set_btswitch: the band's PHY is not an AC-PHY */

/* Coexistence modes (btc+0x00, R3 btc_mode_set). */
#define BTC_MODE_OFF		0
#define BTC_MODE_1		1
#define BTC_MODE_3		3
#define BTC_MODE_4		4
#define BTC_MODE_5		5
#define BTC_MODE_AUTO		8	/* resolved by step 2; also the largest mode */

/* Interface types (btc+0x04, R3 btc_wire_set; signed compares). */
#define BTC_WIRE_AUTO		0
#define BTC_WIRE_2		2
#define BTC_WIRE_3		3
#define BTC_WIRE_4		4	/* also the largest wire */

/* btc_mode_set step 2: `aa2g` up to this value resolves "auto" to mode 1. */
#define BTC_AA2G_MODE1_MAX	2

/* PHY type of the AC-PHY (bmac-run.md "Constants"). */
#define PHY_TYPE_AC		11

/* MAC core revisions at which R3 branches. */
#define MAC_REV_12		12	/* btc_mode_set steps 3 and 5; btc_wire_set step 2 */
#define MAC_REV_ECI_MIN		15	/* P_eci */

/* ChipCommon revision from which the ECI layout of update_bt_chanspec is the new one. */
#define CC_REV_ECI_NEW		35

/* Capabilities (R3 predicates). */
#define CC_CAP_ECI		BIT(29)		/* cccaps: ECI */
#define CC_CAPEXT_SECI		BIT(0)		/* cccaps_ext: SECI */
#define CC_CAPEXT_SECI_GCI	0x00000005	/* cccaps_ext: bits 0 and 2 (P_seci) */
#define MCAP_BTCX		BIT(29)		/* machwcap and machwcap_backup: coexistence */

/* Board flags (wlc_hw+0x8c, +0x90) tested here. */
#define BFL_0x00000001		0x00000001	/* P_seci, seci_upd */
#define BFL_0x00004000		0x00004000	/* btc_mode_set step 5, wire 2 */
#define BFL_0x00400000		0x00400000	/* btc_mode_set step 5 (0x4331), btc_wire_set step 5 */
#define BFL2_0x00000080		0x00000080	/* P_seci, btc_mode_set step 2, btc_wire_set step 2 */
#define BFL2_0x02000000		0x02000000	/* btc_wire_set step 2 */

/* Chips, board vendor and board types of R3. */
#define CHIP_ID_4312		0x4312
#define CHIP_ID_4313		0x4313
#define CHIP_ID_4331		0x4331
#define CHIP_ID_A886		0xa886
#define CHIP_ID_A8D8		0xa8d8
#define CHIP_ID_A8D9		0xa8d9
#define CHIP_ID_A8DC		0xa8dc
#define CHIP_ID_A99D		0xa99d
#define BOARDVENDOR_APPLE	0x106b
#define BOARDTYPE_0xE078	0xe078	/* chip 0xa886: auto mode 1 */
#define BOARDTYPE_0x90		0x90	/* MAC revision 12: auto wire 3 */
#define BOARDTYPE_0x8B		0x8b	/* likewise */

/* The coexistence flags word (btc+0x08) as btc_mode_set step 5 builds it. */
#define BTC_FLAGS_OLD_MAC	0x000c	/* wire != 2, MAC revision <= 12 */
#define BTC_FLAGS_MODE5		0x0008
#define BTC_FLAGS_MODE3		(0x0080 | 0x0008)
#define BTC_FLAGS_MODE4		(0x0100 | 0x0008)	/* also mode 3 on chip 0x4331 with bit 22 */
#define BTC_FLAGS_MODE_OTHER	0x0014
#define BTC_FLAGS_HW		0x0040	/* BTC_HW */
#define BTC_FLAGS_NO_HW		0x0001	/* neither BTC_HW nor wire 4 */

/* The coexistence GPIO lines (btc+0x0c mask, btc+0x10 out) of btc_mode_set step 5. */
#define BTC_GPIO_MASK_WIRE2	0x30
#define BTC_GPIO_OUT_WIRE2	0x20
#define BTC_GPIO_MASK_OLD_MAC	0x870
#define BTC_GPIO_OUT_OLD_MAC	0x20

/* The GPIO lines of btc_wire_set step 5 by chip (mask and out alike). */
#define BTC_WIRE_GPIO_0xE0	0xe0
#define BTC_WIRE_GPIO_0x20	0x20
#define BTC_WIRE_GPIO_0x10	0x10
#define BTC_WIRE_GPIO_0x60	0x60
#define BTC_WIRE_GPIO_NONE	0	/* any other chip: the 4360 */

/* Host flag words: the `idx` of bcm4360_bmac_mhf (A9), and the bits of step 8. */
#define MHF1			0	/* SHM(0x5e) */
#define MHF2			1	/* SHM(0x60) */
#define MHF3			2	/* SHM(0x62) */
#define MHF1_0x0010		0x0010	/* v1: coexistence on */
#define MHF2_0x0100		0x0100	/* v2: wire 2 with boardflags bit 14 */
#define MHF3_0x2000		0x2000	/* v3: wire 4 without BTC_HW */
#define MHF_BANDS_2G		2	/* the 2.4 GHz band state (A9 step 1) */

/* Chanspec fields (bmac-run.md "Constants"). */
#define CHSPEC_CHAN_MASK	0x00ff
#define CHSPEC_BAND_MASK	0xc000
#define CHSPEC_BAND_2G		0x0000
#define CHSPEC_BAND_5G		0xc000
#define CHSPEC_BW_MASK		0x3800
#define CHSPEC_BW_40		0x1800

/*
 * R3 update_bt_chanspec: the masks and values of si_eci_notify_bt, for a
 * ChipCommon revision >= 35 ("new") and below ("old").
 */
#define ECI_CHAN_MASK_NEW	0x5555f000	/* M */
#define ECI_CHAN_MASK_OLD	0x0000000f
#define ECI_CHAN_SHIFT_NEW	12		/* the channel number's position */
#define ECI_CHAN_SHIFT_OLD	0
#define ECI_CHAN_5G_NEW		0xf000		/* the value for any 5 GHz channel */
#define ECI_CHAN_5G_OLD		0x000f
#define ECI_BW_MASK_NEW		0x55550e00	/* the second notification of 2.4 GHz */
#define ECI_BW_MASK_OLD		0x00000070
#define ECI_BW_40_NEW		0x0a00
#define ECI_BW_20_NEW		0x0200
#define ECI_BW_40_OLD		0x0050
#define ECI_BW_20_OLD		0x0010
#define ECI_NOTIFY_INTERRUPT	1		/* the last argument of every notification */

/* ---- the predicates ---- */

/* docs/re/spec/bmac-run.md R3, P_eci: the ECI interface is there */
static bool btc_p_eci(struct bcm4360_mac *mac)
{
	return mac->mac_rev >= MAC_REV_ECI_MIN && (mac->cccaps & CC_CAP_ECI) &&
	       !(mac->cccaps_ext & CC_CAPEXT_SECI);
}

/* docs/re/spec/bmac-run.md R3, P_seci: the serial interface (SECI/GCI) is there */
static bool btc_p_seci(struct bcm4360_mac *mac)
{
	return (mac->boardflags & BFL_0x00000001) && !(mac->boardflags2 & BFL2_0x00000080) &&
	       (mac->cccaps_ext & CC_CAPEXT_SECI_GCI);
}

/* docs/re/spec/bmac-run.md R3, P_cap: the MAC has the coexistence capability */
static bool btc_p_cap(struct bcm4360_mac *mac)
{
	return (mac->machwcap & MCAP_BTCX) != 0;
}

/* docs/re/spec/bmac-run.md R3, BTC_HW: the coexistence hardware is there */
static bool btc_hw_present(struct bcm4360_mac *mac)
{
	return (btc_p_eci(mac) || btc_p_seci(mac)) && btc_p_cap(mac);
}

/* ---- the mode ---- */

/*
 * docs/re/spec/bmac-run.md R3, btc_mode_set step 2: the mode "auto" when the
 * coexistence hardware or boardflags2 bit 7 is there, by `aa2g` and chip.
 */
static s32 btc_mode_auto(struct bcm4360_mac *mac)
{
	if (mac->btc_aa2g <= BTC_AA2G_MODE1_MAX)
		return BTC_MODE_1;
	switch (mac->chip_id) {
	case CHIP_ID_A886:
		return mac->boardtype == BOARDTYPE_0xE078 ? BTC_MODE_1 : BTC_MODE_3;
	case CHIP_ID_A8DC:
		return BTC_MODE_1;
	default:
		return BTC_MODE_5;	/* the 4360 */
	}
}

/*
 * docs/re/spec/bmac-run.md R3, btc_mode_set step 5, wire != 2 and MAC
 * revision > 12: the flag bits of the mode, then those of the hardware.
 * `v3` receives the value of host flag 3.
 */
static void btc_mode_flags(struct bcm4360_mac *mac, s32 mode, u16 *v3)
{
	switch (mode) {
	case BTC_MODE_5:
		mac->btc_flags |= BTC_FLAGS_MODE5;
		break;
	case BTC_MODE_3:
		if (mac->chip_id == CHIP_ID_4331 && (mac->boardflags & BFL_0x00400000))
			mac->btc_flags |= BTC_FLAGS_MODE4;
		else
			mac->btc_flags |= BTC_FLAGS_MODE3;
		break;
	case BTC_MODE_4:
		mac->btc_flags |= BTC_FLAGS_MODE4;
		break;
	default:
		mac->btc_flags |= BTC_FLAGS_MODE_OTHER;
		break;
	}

	if (btc_hw_present(mac))
		mac->btc_flags |= BTC_FLAGS_HW;
	else if (mac->btc_wire == BTC_WIRE_4)
		*v3 = MHF3_0x2000;
	else
		mac->btc_flags |= BTC_FLAGS_NO_HW;
}

/*
 * docs/re/spec/bmac-run.md R3, btc_mode_set step 5: the host flag values
 * `v1`..`v3` (all 0 on entry), the flags word and the GPIO fields of a mode
 * that is not 0.
 */
static void btc_mode_values(struct bcm4360_mac *mac, s32 mode, u16 *v1, u16 *v2, u16 *v3)
{
	*v1 = MHF1_0x0010;

	if (mac->btc_wire == BTC_WIRE_2) {
		if (mac->boardflags & BFL_0x00004000) {
			*v2 = MHF2_0x0100;
			mac->btc_gpio_mask = BTC_GPIO_MASK_WIRE2;
			mac->btc_gpio_out = BTC_GPIO_OUT_WIRE2;
		}
	} else if (mac->mac_rev <= MAC_REV_12) {
		mac->btc_flags |= BTC_FLAGS_OLD_MAC;
		mac->btc_gpio_mask = BTC_GPIO_MASK_OLD_MAC;
		mac->btc_gpio_out = BTC_GPIO_OUT_OLD_MAC;
	} else {
		btc_mode_flags(mac, mode, v3);		/* this card */
	}
}

/*
 * docs/re/spec/bmac-run.md R3 (wlc_bmac_btc_mode_set): set the coexistence
 * mode (8: resolve it), build the host flags of the 2.4 GHz band and the
 * coexistence flags from it, with the MAC suspended around the host flag
 * writes if it is enabled and up.
 */
s32 bcm4360_bmac_btc_mode_set(struct bcm4360_mac *mac, s32 mode)
{
	u16 v1 = 0, v2 = 0, v3 = 0;
	bool en = false;

	/* step 1 */
	if (mode > BTC_MODE_AUTO)
		return BTC_E_RANGE;

	/* step 2: resolve "auto"; mode 0 (also resolved) skips step 3 */
	if (mode == BTC_MODE_AUTO) {
		if (!btc_hw_present(mac) && !(mac->boardflags2 & BFL2_0x00000080))
			mode = BTC_MODE_OFF;
		else
			mode = btc_mode_auto(mac);
	}

	/* step 3: any mode but 0 (also a negative one) */
	if (mode != BTC_MODE_OFF && mac->btc_wire > BTC_WIRE_2 && mac->mac_rev > MAC_REV_12 &&
	    !(mac->machwcap_backup & MCAP_BTCX))
		return BTC_E_NO_HW;

	/* step 4 */
	mac->btc_flags = 0;
	if (mac->up)
		en = (d11_read32(mac->hw, D11_MACCONTROL) & MCTL_EN_MAC) != 0;

	/* step 5: mode 0 leaves v1..v3 at 0 */
	if (mode != BTC_MODE_OFF)
		btc_mode_values(mac, mode, &v1, &v2, &v3);

	/* step 6 */
	mac->btc_mode = (u32)mode;

	/* step 7 */
	if (en && mac->up)
		bcm4360_bmac_suspend(mac);

	/* step 8: host flags 1, 2, 3 of the 2.4 GHz band */
	bcm4360_bmac_mhf(mac, MHF1, MHF1_0x0010, v1, MHF_BANDS_2G);
	bcm4360_bmac_mhf(mac, MHF2, MHF2_0x0100, v2, MHF_BANDS_2G);
	bcm4360_bmac_mhf(mac, MHF3, MHF3_0x2000, v3, MHF_BANDS_2G);

	/* step 9: sub_062b79 */
	bcm4360_bmac_btc_flags_to_mhf(mac);

	/* step 10 */
	if (en && mac->up)
		bcm4360_bmac_enable(mac);
	return BTC_OK;
}

/* ---- the wire ---- */

/*
 * docs/re/spec/bmac-run.md R3, btc_wire_set step 2: the interface type of
 * "auto", from the hardware and the board flags.
 */
static s32 btc_wire_auto(struct bcm4360_mac *mac)
{
	s32 wire;

	if (btc_hw_present(mac))
		wire = BTC_WIRE_3;
	else if (mac->boardflags2 & BFL2_0x00000080)
		wire = (mac->boardflags2 & BFL2_0x02000000) ? BTC_WIRE_3 : BTC_WIRE_4;
	else
		wire = BTC_WIRE_2;

	/* MAC revision 12 on the Apple board types 0x90 and 0x8b */
	if (mac->mac_rev == MAC_REV_12 && mac->boardvendor == BOARDVENDOR_APPLE &&
	    (mac->boardtype == BOARDTYPE_0x90 || mac->boardtype == BOARDTYPE_0x8B))
		wire = BTC_WIRE_3;
	return wire;
}

/* docs/re/spec/bmac-run.md R3, btc_wire_set step 5: the coexistence GPIO lines by chip */
static u32 btc_wire_gpio(struct bcm4360_mac *mac)
{
	switch (mac->chip_id) {
	case CHIP_ID_A8D8:
	case CHIP_ID_A99D:
		return (mac->boardflags & BFL_0x00400000) ? BTC_WIRE_GPIO_0xE0 :
							      BTC_WIRE_GPIO_0x20;
	case CHIP_ID_A8D9:
		return BTC_WIRE_GPIO_0xE0;
	case CHIP_ID_4312:
		return BTC_WIRE_GPIO_0x10;
	case CHIP_ID_4313:
		return BTC_WIRE_GPIO_0x60;
	default:
		return BTC_WIRE_GPIO_NONE;	/* the 4360 */
	}
}

/*
 * docs/re/spec/bmac-run.md R3 (wlc_bmac_btc_wire_set): set the interface type
 * (0: resolve it). The microcode is downloaded again at the next
 * initialisation; a 3- or 4-wire interface gets the chip's GPIO lines.
 */
s32 bcm4360_bmac_btc_wire_set(struct bcm4360_mac *mac, s32 wire)
{
	u32 g;

	/* step 1 */
	if (wire > BTC_WIRE_4)
		return BTC_E_RANGE;

	/* step 2 */
	mac->btc_wire = wire == BTC_WIRE_AUTO ? btc_wire_auto(mac) : wire;

	/* step 3: always */
	mac->ucode_loaded = 0;

	/* step 4 */
	if (mac->btc_wire <= BTC_WIRE_2)
		return BTC_OK;

	/* step 5: both fields overwritten (0 on the 4360) */
	g = btc_wire_gpio(mac);
	mac->btc_gpio_out = g;
	mac->btc_gpio_mask = g;
	return BTC_OK;
}

/* docs/re/spec/bmac-run.md R3 (wlc_bmac_btc_wire_get): the interface type, no access */
s32 bcm4360_bmac_btc_wire_get(struct bcm4360_mac *mac)
{
	return mac->btc_wire;
}

/* ---- the switch ---- */

/*
 * docs/re/spec/bmac-run.md R3 (wlc_bmac_set_btswitch): the override of the
 * front-end control between Bluetooth and WLAN (`sw` 1, 0, or -1 for none),
 * set in the PHY of the current band and stored.
 */
s32 bcm4360_bmac_set_btswitch(struct bcm4360_mac *mac, s8 sw)
{
	/*
	 * Chips 0xa9a7 and 0x4331 on the board types 0x10e, 0xe4, 0x5c6, 0xef
	 * and 0x10f use GPIO 4 (mask 0x10) as the switch instead (through
	 * wlc_bmac_set_ctrl_bt_shd0, si_gpioout and si_gpioouten) and then store
	 * it as below; not this chip.
	 */

	if (mac->band_phytype != PHY_TYPE_AC)
		return BTC_E_UNSUPPORTED;	/* btswitch not changed */
	if (!mac->up)
		return BTC_E_NOTUP;
	bcm4360_bmac_env_phy_set_femctrl_bt_wlan_ovrd(mac, sw);

	/* store */
	mac->btswitch = sw;
	return BTC_OK;
}

/* ---- the channel told to the Bluetooth side ---- */

/* docs/re/spec/bmac-run.md R3, update_bt_chanspec step 2: H (ChipCommon revision >= 35) */
static bool eci_layout_new(struct bcm4360_mac *mac)
{
	return mac->ccrev >= CC_REV_ECI_NEW;
}

/* docs/re/spec/bmac-run.md R3, update_bt_chanspec step 2: the mask M */
static u32 eci_chan_mask(struct bcm4360_mac *mac)
{
	return eci_layout_new(mac) ? ECI_CHAN_MASK_NEW : ECI_CHAN_MASK_OLD;
}

/*
 * docs/re/spec/bmac-run.md R3, update_bt_chanspec step 3, 2.4 GHz: the
 * channel number, then the bandwidth (40 MHz or not).
 */
static void bt_chanspec_2g(struct bcm4360_mac *mac, u16 chanspec)
{
	bool b40 = (chanspec & CHSPEC_BW_MASK) == CHSPEC_BW_40;
	u32 shift = eci_layout_new(mac) ? ECI_CHAN_SHIFT_NEW : ECI_CHAN_SHIFT_OLD;

	bcm4360_bmac_env_eci_notify_bt(mac, eci_chan_mask(mac),
				       (u32)(chanspec & CHSPEC_CHAN_MASK) << shift,
				       ECI_NOTIFY_INTERRUPT);

	/* the state may have changed in the service: H is read again */
	if (eci_layout_new(mac))
		bcm4360_bmac_env_eci_notify_bt(mac, ECI_BW_MASK_NEW,
					       b40 ? ECI_BW_40_NEW : ECI_BW_20_NEW,
					       ECI_NOTIFY_INTERRUPT);
	else
		bcm4360_bmac_env_eci_notify_bt(mac, ECI_BW_MASK_OLD,
					       b40 ? ECI_BW_40_OLD : ECI_BW_20_OLD,
					       ECI_NOTIFY_INTERRUPT);
}

/*
 * docs/re/spec/bmac-run.md R3 (wlc_bmac_update_bt_chanspec): tell the
 * Bluetooth side the WLAN channel through the ECI, unless a scan is running
 * or there is no coexistence hardware (then: no access).
 */
void bcm4360_bmac_update_bt_chanspec(struct bcm4360_mac *mac, u16 chanspec, bool scanning,
				     bool p4)
{
	u16 band = chanspec & CHSPEC_BAND_MASK;

	/* step 1 */
	if (!((btc_p_eci(mac) || btc_p_seci(mac)) && !scanning && btc_p_cap(mac) && !p4))
		return;

	/* step 3 (step 2: eci_layout_new, eci_chan_mask) */
	if (chanspec == 0 || (band != CHSPEC_BAND_2G && band != CHSPEC_BAND_5G))
		bcm4360_bmac_env_eci_notify_bt(mac, eci_chan_mask(mac), 0, ECI_NOTIFY_INTERRUPT);
	else if (band == CHSPEC_BAND_2G)
		bt_chanspec_2g(mac, chanspec);
	else
		bcm4360_bmac_env_eci_notify_bt(mac, eci_chan_mask(mac),
					       eci_layout_new(mac) ? ECI_CHAN_5G_NEW :
								     ECI_CHAN_5G_OLD,
					       ECI_NOTIFY_INTERRUPT);
}

/* ---- the serial coexistence interface ---- */

/*
 * docs/re/spec/bmac-run.md R3 (wlc_bmac_seci_upd): update the SECI when the
 * board and the chip have it and the upper layer's coexistence mode is not 0.
 * No result (Q6).
 */
void bcm4360_bmac_seci_upd(struct bcm4360_mac *mac)
{
	if (!(mac->boardflags & BFL_0x00000001) || (mac->boardflags2 & BFL2_0x00000080) ||
	    !(mac->cccaps_ext & CC_CAPEXT_SECI) || !btc_p_cap(mac))
		return;
	if (bcm4360_bmac_env_wlc_btc_mode(mac) != 0)
		bcm4360_bmac_env_seci_upd(mac, true);
}
