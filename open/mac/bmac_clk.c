/*
 * Clocks, core reset, crystal and core disable of the 802.11 core
 * (wlc_bmac.c, wlc_hw.c): the gated clocks and the PHY clock in the core's
 * wrapper, the fast/dynamic MAC clock, the core reset with the PHY reset, the
 * crystal and the PLL requests, the core disable, the radio's hardware
 * switch and the "card removed" test.
 *
 * Written from docs/re/spec/bmac-init.md, sections A10-A17, C7, C9, C11, C13,
 * C14 and the notation of "Data", for MAC core revision 42 with an AC-PHY
 * (chip 0x4360 revision 3, PCIe, with a PMU). Everything outside the 802.11
 * core's registers goes through the services of bcm4360/mac_env.h. A service
 * may change struct bcm4360_mac (the PHY can call back into the MAC layer), so
 * no field of *mac is kept in a local variable across a service call.
 */
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>
#include <bcm4360/access.h>

/* Direct 802.11-core registers used here (docs/re/spec/bmac-init.md). */
#define D11_MACCONTROL		0x120	/* MAC control register */
#define D11_REG_0x158		0x158	/* bit 16: the radio is disabled by the hardware switch (C13) */
#define D11_CLK_CTL_ST		0x1e0	/* clock control of the MAC core (A13) */

/* Bits of the MAC control register D11(0x120) ("Data", A14 step 7, C14). */
#define MCTL_EN_MAC		0x00000001	/* MAC enabled */
#define MCTL_PSM_RUN		0x00000002	/* microcode processor runs */
#define MCTL_PSM_JMP_0		0x00000004	/* microcode processor held at address 0 */
#define MCTL_IHR_EN		0x00000400	/* access to the internal registers enabled */
#define MCTL_WAKE		0x04000000	/* microcode must stay awake */
#define MCTL_ALL		0xffffffff	/* mask: every bit of the copy */

/* The value of the MAC control register after a core reset (A14 step 7). */
#define MCTL_RESET_VALUE	(MCTL_WAKE | MCTL_IHR_EN)

/* C13: bit 16 of D11(0x158) set: the hardware switch disables the radio. */
#define RADIO_HWDISABLED_SHIFT	16

/* Bits of D11(0x1e0) (A13). */
#define CLK_CTL_FORCE_FAST	0x00000002	/* the fast clock is forced */
#define CLK_CTL_0x12		0x00000012	/* tested before the wait of step 3 */
#define CLK_CTL_0x20000		0x00020000	/* awaited after the fast clock is forced */

/* Modes of wlc_bmac_clkctl_clk (A13): 0 = fast, the callers use 2 = dynamic. */
#define CLK_FAST		0
#define CLK_DYNAMIC		2

/*
 * Bits of the wrapper's control register WRAP(d11, 0x408), changed with
 * cflags() (docs/re/spec/bmac-init.md, A12, the list of bits).
 */
#define CFLAG_FGC		0x0002	/* force the gated clocks on */
#define CFLAG_PCLKE		0x0004	/* PHY clock enable */
#define CFLAG_PRST		0x0008	/* PHY reset */
#define CFLAG_MPCLKE		0x0010	/* clock between MAC and PHY enabled */
#define CFLAG_PHY_BW		0x00c0	/* bandwidth of the PHY clock */
#define CFLAG_0x0300		0x0300	/* two-bit field of MAC revisions >= 40 (purpose unknown) */
#define CFLAG_0x0300_ONE	0x0100	/* ... set to 1 */
#define CFLAG_BAND_2G		0x2000	/* the band is 2.4 GHz */

/* The `flags` argument of the core reset meaning "the flags of the current band" (A14). */
#define CORERESET_BAND_FLAGS	0xffffffff

/* PHY types ("Data"; A10, A12, A14, A15). */
#define PHY_TYPE_N		4
#define PHY_TYPE_HT		7
#define PHY_TYPE_10		10	/* wlc_phy_anacore is not called for it (A15 step 5) */
#define PHY_TYPE_AC		11

/* MAC core revisions at which the code branches. */
#define MAC_REV_RXRESET_PLAIN	12	/* A14 step 3.2: older revisions do more */
#define MAC_REV_PHYPLL_FIRST	17	/* A16: below: no PHY PLL control */
#define MAC_REV_PHYPLL_NONE_A	20	/* A16: no PHY PLL control */
#define MAC_REV_CORERESET_PCLK	18	/* A14 step 5, C13 step 2: PHY clock in the first argument */
#define MAC_REV_PHYPLL_NONE_B	27	/* A16: no PHY PLL control */
#define MAC_REV_40		40	/* A12/A14: CFLAG_0x0300; A16: no PHY PLL control */

/* The transmit FIFOs with a DMA engine and the receive FIFO (A14 step 3). */
#define NR_TX_FIFOS		6
#define RX_FIFO			0

/* The chip (C11, A13). */
#define BUSTYPE_PCI		1
#define CC_CAP_PMU		BIT(28)		/* ChipCommon capabilities: the chip has a PMU */
#define CHIP_ID_A9C4		0xa9c4
#define CHIP_ID_4360		0x4360
#define CHIP_ID_AA06		0xaa06
#define CHIP_ID_4352		0x4352
#define CHIP_REV_SROM_CTRL	3		/* C11: set_ctrl_SROM acts below this revision */

/* C11: ChipCommon (core index 0) and its chip control register CC(0x28). */
#define CC_CORE_INDEX		0
#define CC_CHIPCONTROL		0x28
#define CC_CHIPCTL_0x01000008	0x01000008	/* cleared by set_ctrl_bt_shd0 */

/* C9: si_clkctl_xtal `what`: bit 0 crystal, bit 1 PLL (docs/re/spec/pmu.md). */
#define XTAL_WHAT_XTAL		0x1
#define XTAL_WHAT_PLL		0x2

/* C9: the PLL request bit that switches the crystal on (bit 2, value 4). */
#define PLLREQ_0x4		0x4

/* C7 step 3: si_gpiocontrol(sih, 0xffffffff, 0, 0). */
#define GPIO_ALL		0xffffffff

/* Delays and waits, microseconds ("Data", notation; A12, A13, A15). */
#define SPINWAIT_STEP_US	10	/* one SPINWAIT round */
#define CLK_FAST_WAIT_US	20000U	/* A13: budget of the wait for D11(0x1e0) bit 17 */
#define CLK_FAST_DELAY_US	64	/* A13 step 2 */
#define PHY_CLK_DELAY_US	1	/* A12 */
#define PHY_RESET_DELAY_US	2	/* A15 step 3 */

/* The current band exists and its PHY type is `type`. */
static bool band_phytype_is(struct bcm4360_mac *mac, u16 type)
{
	return mac->band_valid && mac->band_phytype == type;
}

/* The current band exists and has a PHY (a `pi`). */
static bool band_has_pi(struct bcm4360_mac *mac)
{
	return mac->band_valid && mac->band_has_phy;
}

/*
 * docs/re/spec/bmac-init.md, "Notation", cflags(mask, value): the wrapper's
 * control register of the 802.11 core (si_core_cflags).
 */
static void cflags(struct bcm4360_mac *mac, u32 mask, u32 val)
{
	(void)bcm4360_bmac_env_core_cflags(mac, mask, val);
}

/*
 * docs/re/spec/bmac-init.md, A13 step 2 (and step 3 at PMU revision 0):
 * SPINWAIT((D11(0x1e0) & 0x20000) == 0, 20000).
 */
static void clk_ctl_wait_0x20000(struct bcm4360_mac *mac)
{
	u32 waited = 0;

	while (!(d11_read32(mac->hw, D11_CLK_CTL_ST) & CLK_CTL_0x20000) &&
	       waited < CLK_FAST_WAIT_US) {
		hw_udelay(SPINWAIT_STEP_US);
		waited += SPINWAIT_STEP_US;
	}
}

/*
 * docs/re/spec/bmac-init.md, A13 (sub_064887, wlc_bmac_clkctl_clk): force the
 * fast clock of the MAC core (mode 0) or return to the dynamic clock (any
 * other mode). No register access while the core's clock is off.
 */
void bcm4360_bmac_clkctl_clk(struct bcm4360_mac *mac, u32 mode)
{
	u32 v;

	if (!(mac->cccaps & CC_CAP_PMU)) {
		/* without a PMU: si_clkctl_cc and the wake override; not described */
		return;
	}

	/* step 1: nothing to do on the registers without the core's clock */
	if (mac->clk) {
		if (mode == CLK_FAST) {
			/* step 2 */
			v = d11_read32(mac->hw, D11_CLK_CTL_ST);
			d11_write32(mac->hw, D11_CLK_CTL_ST, v | CLK_CTL_FORCE_FAST);
			hw_udelay(CLK_FAST_DELAY_US);
			clk_ctl_wait_0x20000(mac);
		} else {
			/* step 3: the wait only at PMU revision 0 (not this card) */
			if (mac->pmurev == 0 &&
			    (d11_read32(mac->hw, D11_CLK_CTL_ST) & CLK_CTL_0x12))
				clk_ctl_wait_0x20000(mac);
			v = d11_read32(mac->hw, D11_CLK_CTL_ST);
			d11_write32(mac->hw, D11_CLK_CTL_ST, v & ~CLK_CTL_FORCE_FAST);
		}
	}

	/* step 4 */
	mac->forcefastclk = (mode == CLK_FAST);
}

/*
 * docs/re/spec/bmac-init.md, A10 (wlc_bmac_phyclk_fgc): force the gated clocks
 * of the core on or off; for the N-, HT- and AC-PHY only.
 */
void bcm4360_bmac_phyclk_fgc(struct bcm4360_mac *mac, bool on)
{
	if (!band_phytype_is(mac, PHY_TYPE_N) && !band_phytype_is(mac, PHY_TYPE_HT) &&
	    !band_phytype_is(mac, PHY_TYPE_AC))
		return;
	cflags(mac, CFLAG_FGC, on ? CFLAG_FGC : 0);
}

/*
 * docs/re/spec/bmac-init.md, A11 (wlc_bmac_macphyclk_set): the clock between
 * MAC and PHY on or off.
 */
void bcm4360_bmac_macphyclk_set(struct bcm4360_mac *mac, bool on)
{
	cflags(mac, CFLAG_MPCLKE, on ? CFLAG_MPCLKE : 0);
}

/*
 * docs/re/spec/bmac-init.md, A12 (wlc_bmac_core_phy_clk): the PHY clock on
 * (PHY reset released) or off (PHY held in reset).
 */
void bcm4360_bmac_core_phy_clk(struct bcm4360_mac *mac, bool on)
{
	mac->phyclk = on;

	if (!on) {
		/* PHY in reset with the clock forced, band flag cleared ... */
		cflags(mac, CFLAG_BAND_2G | CFLAG_PRST | CFLAG_FGC, CFLAG_PRST | CFLAG_FGC);
		hw_udelay(PHY_CLK_DELAY_US);
		/* ... then the forcing removed */
		cflags(mac, CFLAG_PRST | CFLAG_FGC, CFLAG_PRST);
		hw_udelay(PHY_CLK_DELAY_US);
		return;
	}

	/* step 1 */
	if (mac->mac_rev >= MAC_REV_40)
		cflags(mac, CFLAG_0x0300, CFLAG_0x0300_ONE);

	if (band_phytype_is(mac, PHY_TYPE_AC)) {
		/* step 2: PHY reset released, PHY clock off, no forcing */
		cflags(mac, CFLAG_PRST | CFLAG_PCLKE | CFLAG_FGC, 0);
		hw_udelay(PHY_CLK_DELAY_US);
		/* step 3: PHY clock on */
		cflags(mac, CFLAG_PCLKE, CFLAG_PCLKE);
		hw_udelay(PHY_CLK_DELAY_US);
	} else {
		/* other PHY types: the gated clocks stay forced in step 2 */
		cflags(mac, CFLAG_PRST | CFLAG_PCLKE | CFLAG_FGC, CFLAG_FGC);
		hw_udelay(PHY_CLK_DELAY_US);
		cflags(mac, CFLAG_PCLKE | CFLAG_FGC, CFLAG_PCLKE);
		hw_udelay(PHY_CLK_DELAY_US);
	}
}

/*
 * docs/re/spec/bmac-init.md, A14 step 7 (also C13 step 2): the MAC control
 * state after a reset of the core: copy and overrides cleared, D11(0x120) =
 * WAKE | IHR_EN (the microcode processor stopped).
 */
static void mctrl_reset(struct bcm4360_mac *mac)
{
	mac->maccontrol = 0;
	mac->suspended_fifos = 0;
	mac->wake_override = 0;
	mac->mute_override = 0;
	bcm4360_bmac_mctrl(mac, MCTL_ALL, MCTL_RESET_VALUE);
}

/*
 * docs/re/spec/bmac-init.md, A14 step 3.2 (sub_0638a5): receive reset of a
 * FIFO through its DMA engine (function slot 0xa8).
 */
static void wlc_bmac_rxfifo_reset(struct bcm4360_mac *mac, u32 fifo)
{
	if (mac->mac_rev < MAC_REV_RXRESET_PLAIN) {
		/* revisions < 12: D11(0x406) and a wait first; not described */
		return;
	}
	bcm4360_bmac_env_dma_rxreset(mac, fifo);
}

/*
 * docs/re/spec/bmac-init.md, A14 step 3: if the core is up, reset the
 * transmit DMA channels and the receive channel of FIFO 0.
 */
static void corereset_dma(struct bcm4360_mac *mac)
{
	u32 i;

	if (!bcm4360_bmac_env_core_iscoreup(mac))
		return;
	for (i = 0; i < NR_TX_FIFOS; i++)
		if (mac->dma_fifos & BIT(i))
			bcm4360_bmac_env_dma_txreset(mac, i);
	if (mac->dma_fifos & BIT(RX_FIFO))
		wlc_bmac_rxfifo_reset(mac, RX_FIFO);
}

/*
 * docs/re/spec/bmac-init.md, A14 (wlc_bmac_corereset): reset the 802.11 core
 * including the PHY; leave it with the fast clock forced if it was before and
 * the microcode processor stopped. `flags` are bits for the wrapper's control
 * register; 0xffffffff means "the flags of the current band".
 */
void bcm4360_bmac_corereset(struct bcm4360_mac *mac, u32 flags)
{
	bool fast;

	/* step 1 */
	if (flags == CORERESET_BAND_FLAGS)
		flags = band_has_pi(mac) ? mac->band_coreflags : 0;

	/* step 2: the clock mode at entry (a local by the specification) */
	fast = mac->forcefastclk;
	if (!fast)
		bcm4360_bmac_clkctl_clk(mac, CLK_FAST);

	/* step 3 */
	corereset_dma(mac);

	/* step 4: no reset; the clock mode of step 2 is not restored */
	if (mac->noreset) {
		bcm4360_bmac_env_clear_intstatus(mac);
		bcm4360_bmac_mctrl(mac, MCTL_EN_MAC | MCTL_PSM_RUN, 0);
		return;
	}

	/* step 5 (MAC revisions >= 18; older ones pass the PHY clock bit
	 * differently: not described) */
	mac->clk = 0;
	bcm4360_bmac_env_core_reset(mac, flags | CFLAG_PCLKE, 0);
	mac->clk = 1;
	if (band_has_pi(mac))
		bcm4360_bmac_env_phy_hw_clk_state_upd(mac, true);

	/* step 6 */
	if (band_phytype_is(mac, PHY_TYPE_AC)) {
		cflags(mac, CFLAG_0x0300, CFLAG_0x0300_ONE);
		cflags(mac, CFLAG_PCLKE | CFLAG_FGC, 0);
		cflags(mac, CFLAG_PCLKE, CFLAG_PCLKE);
	}

	/* step 7 */
	mctrl_reset(mac);

	/* step 8: the reset cleared the force bit */
	if (mac->cccaps & CC_CAP_PMU)
		bcm4360_bmac_clkctl_clk(mac, CLK_FAST);

	/* step 9 */
	if (mac->band_valid)
		bcm4360_bmac_phy_reset(mac);

	/* step 10 */
	bcm4360_bmac_core_phypll_ctl(mac, true);

	/* step 11 */
	bcm4360_bmac_env_clear_intstatus(mac);

	/* step 12 */
	if (!fast)
		bcm4360_bmac_clkctl_clk(mac, CLK_DYNAMIC);
}

/*
 * docs/re/spec/bmac-init.md, A15 (wlc_bmac_phy_reset): reset the PHY through
 * the wrapper of the 802.11 core (the AC-PHY path; the PLL programming of the
 * N-PHY revisions 3, 4, 18 and the SSLPN-PHY revisions 2, 3 is not described).
 */
void bcm4360_bmac_phy_reset(struct bcm4360_mac *mac)
{
	u32 bw;

	if (!band_has_pi(mac))
		return;

	/* step 1 */
	bw = bcm4360_bmac_env_phy_clk_bwbits(mac);

	/* step 2: bandwidth, PHY reset asserted, PHY clock on, gated clocks forced */
	cflags(mac, CFLAG_PHY_BW | CFLAG_PRST | CFLAG_PCLKE | CFLAG_FGC,
	       bw | CFLAG_PRST | CFLAG_PCLKE | CFLAG_FGC);

	/* step 3 */
	hw_udelay(PHY_RESET_DELAY_US);

	/* step 4 */
	bcm4360_bmac_core_phy_clk(mac, true);

	/* step 5 */
	if (!band_phytype_is(mac, PHY_TYPE_10))
		bcm4360_bmac_env_phy_anacore(mac, true);
}

/*
 * docs/re/spec/bmac-init.md, A16 (wlc_bmac_core_phypll_ctl): nothing for MAC
 * revisions below 17, 20, 27 and 40 or higher.
 */
void bcm4360_bmac_core_phypll_ctl(struct bcm4360_mac *mac, bool on)
{
	(void)on;

	if (mac->mac_rev < MAC_REV_PHYPLL_FIRST || mac->mac_rev == MAC_REV_PHYPLL_NONE_A ||
	    mac->mac_rev == MAC_REV_PHYPLL_NONE_B || mac->mac_rev >= MAC_REV_40)
		return;
	/* revisions 17..39: the PHY PLL request in D11(0x1e0) / D11(0x4f0); not described */
}

/*
 * docs/re/spec/bmac-init.md, A16 (wlc_bmac_core_phypll_reset): nothing for the
 * AC-PHY.
 */
void bcm4360_bmac_core_phypll_reset(struct bcm4360_mac *mac)
{
	if (!band_phytype_is(mac, PHY_TYPE_N) && !band_phytype_is(mac, PHY_TYPE_HT))
		return;
	/* N- and HT-PHY: PMU_CHIPCTL[0] bit 2 pulsed; not described */
}

/*
 * docs/re/spec/bmac-init.md, A17 (wlc_bmac_btc_mode_get): the Bluetooth
 * coexistence mode (0 = off). No access.
 */
u32 bcm4360_bmac_btc_mode_get(struct bcm4360_mac *mac)
{
	return mac->btc_mode;
}

/*
 * docs/re/spec/bmac-init.md, C7 (wlc_coredisable): switch the radio and the
 * analog core off and disable the 802.11 core. Nothing if the card is gone or
 * `noreset` is set.
 */
void bcm4360_bmac_coredisable(struct bcm4360_mac *mac)
{
	if (bcm4360_bmac_deviceremoved(mac) || mac->noreset)
		return;

	/* step 1 */
	bcm4360_bmac_env_phy_switch_radio(mac, false);
	bcm4360_bmac_env_phy_anacore(mac, false);

	/* step 2 */
	bcm4360_bmac_core_phypll_ctl(mac, false);

	/* step 3 */
	if (mac->gpio_sel != 0)
		(void)bcm4360_bmac_env_gpiocontrol(mac, GPIO_ALL, 0, 0);

	/* step 4 */
	mac->clk = 0;
	bcm4360_bmac_env_core_disable(mac, 0);

	/* step 5 */
	bcm4360_bmac_env_phy_hw_clk_state_upd(mac, false);
}

/*
 * docs/re/spec/bmac-init.md, C9 (wlc_bmac_xtal): the crystal and the PLL on or
 * off. Not switched off while a PLL request is pending.
 */
void bcm4360_bmac_xtal(struct bcm4360_mac *mac, bool want)
{
	/* step 1 */
	if (!want && mac->pllreq != 0)
		return;

	/* step 2 */
	(void)bcm4360_bmac_env_clkctl_xtal(mac, XTAL_WHAT_XTAL | XTAL_WHAT_PLL, want);

	/* step 3 */
	mac->sbclk = want;
	if (!want) {
		mac->clk = 0;
		if (band_has_pi(mac))
			bcm4360_bmac_env_phy_hw_clk_state_upd(mac, false);
	}
}

/*
 * docs/re/spec/bmac-init.md, C9 (wlc_bmac_pllreq): add or remove a requester
 * of the PLL. Clearing a request never switches the crystal off (the mask is
 * not 0 when bcm4360_bmac_xtal is called).
 */
void bcm4360_bmac_pllreq(struct bcm4360_mac *mac, bool set, u32 req_bit)
{
	if (set) {
		if (mac->pllreq & req_bit)
			return;
		mac->pllreq |= req_bit;
		if ((mac->pllreq & PLLREQ_0x4) && !mac->sbclk)
			bcm4360_bmac_xtal(mac, true);
	} else {
		if (!(mac->pllreq & req_bit))
			return;
		mac->pllreq &= ~req_bit;
		if ((mac->pllreq & PLLREQ_0x4) && mac->sbclk)
			bcm4360_bmac_xtal(mac, false);
	}
}

/*
 * docs/re/spec/bmac-init.md, C9 (wlc_bmac_set_clk): on: crystal on and a core
 * reset; off: core disable (if the clock is on) and crystal off.
 */
void bcm4360_bmac_set_clk(struct bcm4360_mac *mac, bool on)
{
	if (on) {
		bcm4360_bmac_xtal(mac, true);
		bcm4360_bmac_corereset(mac, CORERESET_BAND_FLAGS);
		return;
	}
	if (mac->clk)
		bcm4360_bmac_coredisable(mac);
	bcm4360_bmac_xtal(mac, false);
}

/* docs/re/spec/bmac-init.md, C11: the chip is one of the list of set_ctrl_bt_shd0 */
static bool chip_is_bt_shd0_list(struct bcm4360_mac *mac)
{
	return mac->chip_id == CHIP_ID_A9C4 || mac->chip_id == CHIP_ID_4360 ||
	       mac->chip_id == CHIP_ID_AA06 || mac->chip_id == CHIP_ID_4352;
}

/* docs/re/spec/bmac-init.md, C11: the chip is one of the list of set_ctrl_SROM */
static bool chip_is_srom_list(struct bcm4360_mac *mac)
{
	return mac->chip_id == CHIP_ID_A9C4 || mac->chip_id == CHIP_ID_4360 ||
	       mac->chip_id == CHIP_ID_4352;
}

/*
 * docs/re/spec/bmac-init.md, C11 (wlc_bmac_set_ctrl_bt_shd0): on PCI and the
 * chips 0xa9c4, 0x4360, 0xaa06, 0x4352 clear the bits 0x01000008 of
 * ChipCommon's chip control register, whatever `on` is. Result 0.
 */
s32 bcm4360_bmac_set_ctrl_bt_shd0(struct bcm4360_mac *mac, bool on)
{
	(void)on;

	if (mac->bustype != BUSTYPE_PCI)
		return 0;
	if (chip_is_bt_shd0_list(mac))
		(void)bcm4360_bmac_env_corereg(mac, CC_CORE_INDEX, CC_CHIPCONTROL,
					       CC_CHIPCTL_0x01000008, 0);
	/* chips 0xa9a7/0x4331: si_chipcontrl_btshd0_4331; not described */
	return 0;
}

/*
 * docs/re/spec/bmac-init.md, C11 (wlc_bmac_set_ctrl_SROM): on PCI and the
 * chips 0xa9c4, 0x4360, 0x4352 below revision 3: si_chipcontrl_srom4360(1).
 */
void bcm4360_bmac_set_ctrl_srom(struct bcm4360_mac *mac)
{
	if (mac->bustype != BUSTYPE_PCI)
		return;
	if (chip_is_srom_list(mac) && mac->chiprev < CHIP_REV_SROM_CTRL)
		bcm4360_bmac_env_chipcontrol_srom4360(mac, true);
}

/*
 * docs/re/spec/bmac-init.md, C13 (wlc_bmac_radio_read_hwdisabled): whether the
 * hardware switch disables the radio. Crystal and core are brought up for the
 * read if needed and put back afterwards; `clk` is not changed.
 */
bool bcm4360_bmac_radio_read_hwdisabled(struct bcm4360_mac *mac)
{
	bool xtal_on = false, core_on = false;
	u32 v;

	/* step 1 */
	if (!mac->sbclk) {
		bcm4360_bmac_xtal(mac, true);
		xtal_on = true;
	}

	/* step 2 (MAC revisions >= 18; older ones: not described) */
	if (!mac->clk) {
		bcm4360_bmac_env_core_reset(mac, CFLAG_PCLKE, 0);
		mctrl_reset(mac);
		core_on = true;
	}

	/* step 3 */
	v = d11_read32(mac->hw, D11_REG_0x158);

	/* step 4 */
	if (core_on)
		bcm4360_bmac_env_core_disable(mac, 0);

	/* step 5 */
	if (xtal_on)
		bcm4360_bmac_xtal(mac, false);

	return (v >> RADIO_HWDISABLED_SHIFT) & 1;
}

/*
 * docs/re/spec/bmac-init.md, C14 (wlc_hw_deviceremoved): whether the card is
 * gone. With the core's clock off the bus is asked; else a working MAC has
 * IHR_EN set and PSM_JMP_0 clear in D11(0x120) (a removed card reads all ones).
 */
bool bcm4360_bmac_deviceremoved(struct bcm4360_mac *mac)
{
	u32 v;

	if (!mac->clk)
		return bcm4360_bmac_env_bus_deviceremoved(mac);
	v = d11_read32(mac->hw, D11_MACCONTROL);
	return (v & (MCTL_IHR_EN | MCTL_PSM_JMP_0)) != MCTL_IHR_EN;
}
