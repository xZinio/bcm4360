/*
 * The channel and the band of the 802.11 core (wlc_bmac.c): the channel change
 * of the MAC layer with its band switch, the selection of the current band
 * with the band flag of the core's wrapper, the bandwidth of the PHY clock,
 * and the mute of the transmitter.
 *
 * Written from docs/re/spec/acphy-chanspec.md, sections 1, 2 and the MAC's
 * part of 3 (with its notation), and docs/re/spec/bmac-init.md, section E4
 * (with the notation and the "Data" tables), for MAC core revision 42 with an
 * AC-PHY (chip 0x4360 revision 3, PCI device 0x43a0, PCIe, two bands).
 * Everything outside the 802.11 core's registers goes through the services of
 * bcm4360/mac_env.h. A service may change struct bcm4360_mac (the PHY, the
 * FIFO suspend/resume and the band selection can run MAC code), so no field
 * of *mac is kept in a local variable across a service call.
 */
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>

/* Bits of the MAC control register D11(0x120) (bmac-init.md, "Data"). */
#define MCTL_IHR_EN		0x00000400	/* access to the internal registers enabled */
#define MCTL_INFRA		0x00020000	/* infrastructure mode */
#define MCTL_AP			0x00040000	/* access point mode */
#define MCTL_WAKE		0x04000000	/* microcode must stay awake */
#define MCTL_ALL		0xffffffff	/* mask: every bit of the copy */

/* The value of the MAC control register after a core reset (section 1 step 3.2). */
#define MCTL_RESET_VALUE	(MCTL_WAKE | MCTL_IHR_EN)

/*
 * Bits of the wrapper's control register WRAP(d11, 0x408), changed with
 * cflags() (acphy-chanspec.md, "Constants").
 */
#define CFLAG_PHY_BW		0x00c0	/* bandwidth of the PHY clock */
#define CFLAG_BAND_2G		0x2000	/* the band is 2.4 GHz */

/* The band bits of a chanspec: 0xc000 is 5 GHz (acphy-chanspec.md, "Chanspec"). */
#define CHSPEC_BAND_MASK	0xc000
#define CHSPEC_BAND_5G		0xc000

/* Band units (acphy-chanspec.md section 2). */
#define BAND_UNIT_2G		0
#define BAND_UNIT_5G		1

/* Modes of bcm4360_bmac_clkctl_clk (bmac-init.md A13): 0 = fast, 2 = dynamic. */
#define CLK_FAST		0
#define CLK_DYNAMIC		2

/*
 * Section 1 step 6.1: the PCI device ids for which the PHY's channel function
 * is called also when the chanspec is unchanged.
 */
#define PCI_DEVID_0x43a0	0x43a0	/* this card */
#define PCI_DEVID_0x43a3	0x43a3
#define PCI_DEVID_0x43ae	0x43ae
#define PCI_DEVID_0x43b1	0x43b1

/* Section 1 step 3.8: the pending interrupt bits after a band switch, if any were set. */
#define INTSTATUS_AFTER_BANDSWITCH	0x8000

/* Section 1 step 6.3: the reason passed to wlc_bmac_mute. */
#define MUTE_REASON_CHANSPEC	0

/*
 * E4 step 1: the transmit FIFOs in the order they are suspended and resumed;
 * on unmute FIFO 1 is resumed only if the byte wlc+0x64 is 0.
 */
static const u8 mute_fifo_order[] = { 1, 3, 0, 2 };
#define MUTE_FIFO_GATED		1

/*
 * E4 step 2: entry 63 of the address match table holds the local address
 * (MAC revisions >= 40; bmac-init.md B5). Unmute restores it with the
 * attribute bits 15 (OR into the existing attribute word) and 3.
 */
#define AMT_MIN_MAC_REV		40
#define AMT_IDX_LOCAL		0x3f
#define AMT_ATTR_OR		0x8000
#define AMT_ATTR_BIT3		0x0008
#define ETHER_ADDR_LEN		6

/* E4 step 2: the address entry 63 holds while muted. */
static const u8 ether_null[ETHER_ADDR_LEN];

/*
 * docs/re/spec/acphy-chanspec.md, "Procedures", cflags(mask, value): the
 * wrapper's control register of the 802.11 core (si_core_cflags).
 */
static void cflags(struct bcm4360_mac *mac, u32 mask, u32 val)
{
	(void)bcm4360_bmac_env_core_cflags(mac, mask, val);
}

/*
 * docs/re/spec/acphy-chanspec.md, section 1 step 3: the band unit of a
 * chanspec, 1 (5 GHz) if its band bits are 0xc000, else 0 (2.4 GHz).
 */
static u32 chspec_band_unit(u16 chanspec)
{
	return (chanspec & CHSPEC_BAND_MASK) == CHSPEC_BAND_5G ? BAND_UNIT_5G : BAND_UNIT_2G;
}

/*
 * docs/re/spec/acphy-chanspec.md, section 2 (wlc_setxband): band `unit`
 * becomes the current band; with the backplane clock on and resets allowed
 * the band flag of the wrapper follows it.
 */
void bcm4360_bmac_setxband(struct bcm4360_mac *mac, u32 unit)
{
	/* step 1: the platform's; afterwards the band_* fields describe `unit` */
	bcm4360_bmac_env_band_select(mac, unit);

	/* step 2 */
	if (mac->sbclk && !mac->noreset)
		cflags(mac, CFLAG_BAND_2G, unit == BAND_UNIT_2G ? CFLAG_BAND_2G : 0);
}

/*
 * docs/re/spec/acphy-chanspec.md, section 3 (wlc_bmac_bw_reset): the bandwidth
 * bits of the PHY clock in the wrapper, from the PHY of the current band;
 * nothing without a current band with a PHY.
 */
void bcm4360_bmac_bw_reset(struct bcm4360_mac *mac)
{
	if (!mac->band_valid || !mac->band_has_phy)
		return;
	cflags(mac, CFLAG_PHY_BW, bcm4360_bmac_env_phy_clk_bwbits(mac));
}

/*
 * docs/re/spec/acphy-chanspec.md, section 3 (wlc_bmac_bw_set), AC-PHY: the
 * bandwidth `bw` (bandwidth field of a chanspec) to the PHY's state and to the
 * PHY clock.
 */
void bcm4360_bmac_bw_set(struct bcm4360_mac *mac, u16 bw)
{
	/* step 1 */
	bcm4360_bmac_env_phy_bw_state_set(mac, bw);

	/* MAC revisions below 17 read D11(0x120) here; not this card */

	/* step 2 (PHY types other than AC: PHY reset and initialisation instead; not described) */
	bcm4360_bmac_bw_reset(mac);
}

/*
 * docs/re/spec/bmac-init.md, E4 step 1: suspend (mute) or resume (unmute) the
 * transmit FIFOs 1, 3, 0, 2; FIFO 1 is resumed only if the byte wlc+0x64 is 0.
 */
static void mute_tx_fifos(struct bcm4360_mac *mac, bool on)
{
	u32 i, fifo;

	for (i = 0; i < ARRAY_SIZE(mute_fifo_order); i++) {
		fifo = mute_fifo_order[i];
		if (on)
			bcm4360_bmac_env_tx_fifo_suspend(mac, fifo);
		else if (fifo != MUTE_FIFO_GATED || !bcm4360_bmac_env_wlc_flag_64(mac))
			bcm4360_bmac_env_tx_fifo_resume(mac, fifo);
	}
}

/*
 * docs/re/spec/bmac-init.md, E4 step 2: entry 63 of the address match table
 * cleared (mute: the MAC stops acknowledging) or the saved hardware address
 * restored (unmute).
 */
static void mute_addrmatch(struct bcm4360_mac *mac, bool on)
{
	if (mac->mac_rev < AMT_MIN_MAC_REV) {
		/* MAC revisions below 40: wlc_bmac_set_addrmatch(0, ...); not this card */
		return;
	}
	if (on)
		bcm4360_bmac_write_amt(mac, AMT_IDX_LOCAL, ether_null, 0);
	else
		bcm4360_bmac_write_amt(mac, AMT_IDX_LOCAL, mac->hw_etheraddr,
				       AMT_ATTR_OR | AMT_ATTR_BIT3);
}

/*
 * docs/re/spec/bmac-init.md, E4 (wlc_bmac_mute): mute (`on`) or unmute the
 * transmitter; `reason` is passed on to the PHY.
 */
void bcm4360_bmac_mute(struct bcm4360_mac *mac, bool on, u32 reason)
{
	u32 was;

	/* step 1 */
	mute_tx_fifos(mac, on);

	/* step 2 */
	mute_addrmatch(mac, on);

	/* step 3 */
	bcm4360_bmac_env_phy_mute_upd(mac, on, reason);

	/* step 4: an unmute when not muted ends here */
	was = mac->mute_override;
	mac->mute_override = on ? 1 : 0;
	if (!on && was == 0)
		return;

	/* step 5: A1 applies the override (INFRA on, AP off) unless the copy has it already */
	if ((mac->maccontrol & (MCTL_AP | MCTL_INFRA)) != MCTL_INFRA)
		bcm4360_bmac_mctrl_write(mac);
}

/*
 * docs/re/spec/acphy-chanspec.md, section 1 step 3.2: if the 802.11 core is
 * not up, reset it and start its MAC state afresh (D11(0x120) = WAKE |
 * IHR_EN).
 */
static void setband_core_reset(struct bcm4360_mac *mac)
{
	if (bcm4360_bmac_env_core_iscoreup(mac))
		return;
	bcm4360_bmac_env_core_reset(mac, 0, 0);
	mac->suspended_fifos = 0;
	mac->maccontrol = 0;
	mac->wake_override = 0;
	mac->mute_override = 0;
	bcm4360_bmac_mctrl(mac, MCTL_ALL, MCTL_RESET_VALUE);
}

/*
 * docs/re/spec/acphy-chanspec.md, section 1 step 3, driver up (the inlined
 * band switch, assigned name wlc_bmac_setband): switch to band `unit` with
 * the interrupts off and run the band initialisation of B10 without the PHY
 * initialisation. On the AC-PHY radio and PHY clock stay on.
 */
static void wlc_bmac_setband(struct bcm4360_mac *mac, u32 unit, u16 chanspec)
{
	u32 intmask;

	/* step 3.1: the PHY of the new band */
	bcm4360_bmac_env_band_phy_chanspec_radio_set(mac, unit, chanspec);

	/* step 3.2 */
	setband_core_reset(mac);

	/* step 3.3: the result is kept for step 3.9 (a local by the specification) */
	intmask = bcm4360_bmac_env_intrsoff(mac);

	/* step 3.4: radio and PHY clock off for PHY types other than AC and N (rev >= 3) */
	/* MAC revisions below 17 read D11(0x120) here; not this card */

	/* step 3.5 */
	bcm4360_bmac_setxband(mac, unit);

	/* step 3.6: PLL reset loop (PHY types 0, 2), PHY clock on (not AC); not the AC-PHY */

	/* step 3.7: with `bandswitch` and MAC revision >= 40 no wlc_phy_init */
	bcm4360_bmac_bsinit(mac, chanspec, true);

	/* step 3.8 */
	if (bcm4360_bmac_env_intstatus(mac) != 0)
		bcm4360_bmac_env_set_intstatus(mac, INTSTATUS_AFTER_BANDSWITCH);

	/* step 3.9 */
	bcm4360_bmac_env_intrsrestore(mac, intmask);
}

/*
 * docs/re/spec/acphy-chanspec.md, section 1 step 6.1: the PCI devices for
 * which the channel function is called whatever the PHY's chanspec is.
 */
static bool devid_always_chanspec_set(struct bcm4360_mac *mac)
{
	return mac->devid == PCI_DEVID_0x43a0 || mac->devid == PCI_DEVID_0x43a3 ||
	       mac->devid == PCI_DEVID_0x43ae || mac->devid == PCI_DEVID_0x43b1;
}

/*
 * docs/re/spec/acphy-chanspec.md, section 1 step 6 (driver up): the channel to
 * the PHY, the power limits of the channel, the mute.
 */
static void set_chanspec_up(struct bcm4360_mac *mac, u16 chanspec, bool mute, void *limits)
{
	/* step 6.1 */
	if (devid_always_chanspec_set(mac) ||
	    chanspec != bcm4360_bmac_env_phy_chanspec_get(mac))
		bcm4360_bmac_env_phy_chanspec_set(mac, chanspec);

	/* step 6.2 */
	bcm4360_bmac_env_phy_txpower_limit_set(mac, limits, chanspec);

	/* step 6.3 */
	bcm4360_bmac_mute(mac, mute, MUTE_REASON_CHANSPEC);
}

/*
 * docs/re/spec/acphy-chanspec.md, section 1 (wlc_bmac_set_chanspec): tune to
 * `chanspec` with the fast clock forced, switching the band first if the
 * chanspec is in the other band. `mute`: the channel is a quiet one;
 * `limits`: the PHY's handle of the channel's power limits, passed on.
 */
void bcm4360_bmac_set_chanspec(struct bcm4360_mac *mac, u16 chanspec, bool mute,
			       void *limits)
{
	u32 unit = chspec_band_unit(chanspec);
	bool fast;

	/* step 1: the clock mode at entry (a local by the specification) */
	fast = mac->forcefastclk;
	if (!fast)
		bcm4360_bmac_clkctl_clk(mac, CLK_FAST);

	/* step 2 */
	mac->chanspec = chanspec;

	/* step 3 */
	if (mac->nbands > 1 && unit != mac->band_unit) {
		if (!mac->up)
			bcm4360_bmac_setxband(mac, unit);
		else
			wlc_bmac_setband(mac, unit, chanspec);
	}

	/* step 4 (acts for the N-PHY only) */
	bcm4360_bmac_env_phy_initcal_enable(mac, !mute);

	if (!mac->up) {
		/* step 5 */
		if (mac->clk)
			bcm4360_bmac_env_phy_txpower_limit_set(mac, limits, chanspec);
		bcm4360_bmac_env_phy_chanspec_radio_set(mac, chanspec);
	} else {
		/* step 6 */
		set_chanspec_up(mac, chanspec, mute, limits);
	}

	/* step 7 */
	if (!fast)
		bcm4360_bmac_clkctl_clk(mac, CLK_DYNAMIC);
}
