/*
 * Up and down of the 802.11 core (wlc_bmac.c): the one-time preparation of
 * the chip after power-up, the steps that bracket wlc_bmac_init at every up
 * (crystal and clocks, PCIe bridge, the radio's hardware switch, the first
 * core reset, interrupts on), the way down again (interrupts off, suspend,
 * reset, core disable, crystal off), the reset of the core with the flush of
 * the queues, and the check of the chip access at attach.
 *
 * Written from docs/re/spec/bmac-init.md, sections C2-C6, C8, C10, C15, D3
 * and the notation of "Data" (C1 and C16 for the callers and the trace), for
 * MAC core revision 42 with an AC-PHY (chip 0x4360 revision 3, PCIe, with a
 * PMU). Everything outside the 802.11 core's registers goes through the
 * services of bcm4360/mac_env.h. A service may change struct bcm4360_mac (the
 * PHY, wl_reset and Broadcom's MAC functions can run MAC code), so no field of
 * *mac is kept in a local variable across a service call.
 */
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>
#include <bcm4360/access.h>

/* Direct 802.11-core registers used here (docs/re/spec/bmac-init.md). */
#define D11_MACCONTROL		0x120	/* MAC control register */
#define D11_REG_0x18c		0x18c	/* written with 0 by D3 step 4 */
#define D11_REG_0x49e		0x49e	/* the coexistence GPIO bits (C10 step 3) */

/* Bits and values of the MAC control register D11(0x120) ("Data", D3). */
#define MCTL_EN_MAC		0x00000001	/* MAC enabled */
#define MCTL_RESET_VALUE	0x04000400	/* WAKE | IHR_EN: the value after a core reset */
#define MCTL_BIT31		BIT(31)		/* D3 step 4: allowed in the reset value */

/* Modes of bcm4360_bmac_clkctl_clk (A13): 0 = fast, 2 = dynamic. */
#define CLK_FAST		0
#define CLK_DYNAMIC		2

/* The `flags` argument of the core reset meaning "the flags of the current band" (A14). */
#define CORERESET_BAND_FLAGS	0xffffffff

/* C3: the result when the hardware switch disables the radio. */
#define ERR_RADIO_HWDISABLED	(-9)

/* The bus (C2, C3, C10, C15); 2 is PCMCIA (not this card). */
#define BUSTYPE_PCI		1

/* PHY types ("Data"; C4 step 5). */
#define PHY_TYPE_HT		7

/* MAC core revisions at which the code branches. */
#define MAC_REV_40		40	/* C2 step 3: si_pmu_res_init on PCI */
#define MAC_REV_SHM_TEST	10	/* D3: above: the shared memory pattern test */

/* Chips (C2 step 7, C15). */
#define CHIP_ID_4352		0x4352
#define CHIP_ID_4360		0x4360
#define CHIP_ID_A9C4		0xa9c4
#define CHIP_ID_AA06		0xaa06

/* si_survive_perst_war of C15 step 4: the mask argument. */
#define SPERST_MASK_0x204	0x204

/* Board flags (wlc_hw+0x8c, +0x90), capabilities (C2 step 9, C15 step 2). */
#define BFL_0x1			0x00000001	/* coexistence hardware */
#define BFL2_0x80		0x00000080
#define BFL2_0x800		0x00000800	/* bit 11: the second LED branch of C2 step 9 */
#define CC_CAPEXT_SECI		BIT(0)		/* ChipCommon extended capabilities: SECI */
#define MCAP_BTCX		BIT(29)		/* MAC capabilities: coexistence */

/* C2 step 9: the LED table and the GPIO pull-up/-down registers. */
#define LED_ENTRIES		32
#define LED_PIN_MASK		31
#define CCREV_GPIOPULL		20	/* ChipCommon revisions from which si_gpiopull is called */

/* C10 step 3: on PCI bit 8 of the coexistence GPIO mask needs si_btcgpiowar. */
#define BTC_GPIO_BIT8		BIT(8)

/* C8 sub_05fefa: the transmit FIFOs with a DMA engine, the receive FIFO, and
 * the range argument of the transmit reclaim ("everything"). */
#define NR_TX_FIFOS		6
#define RX_FIFO			0
#define DMA_RANGE_ALL		1

/* D3: the patterns written to SHM(0) as one 32-bit word (4 bytes, low byte first). */
#define OBJ_SEL_SHM		(D11_OBJ_MEM_SHM << D11_OBJ_MEM_SHIFT)	/* 0x10000 */
#define SHM_TEST_OFFSET		0
#define SHM_TEST_BYTES		4
#define SHM_PATTERN_A		0xaa5555aa
#define SHM_PATTERN_B		0x55aaaa55

/* docs/re/spec/bmac-init.md, D3: a 32-bit word from four bytes, low byte first */
static u32 bytes_le32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* docs/re/spec/bmac-init.md, D3: store a 32-bit word as four bytes, low byte first */
static void store_le32(u8 *p, u32 v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = v >> 24;
}

/* docs/re/spec/bmac-init.md, C2 step 7: the chips of wlc_bmac_4360_pcie2_war */
static bool chip_is_pcie2_war_list(struct bcm4360_mac *mac)
{
	return mac->chip_id == CHIP_ID_A9C4 || mac->chip_id == CHIP_ID_4360 ||
	       mac->chip_id == CHIP_ID_4352;
}

/* docs/re/spec/bmac-init.md, C15: the chips that switch the RF LDO off */
static bool chip_is_rfldo_list(struct bcm4360_mac *mac)
{
	return mac->chip_id == CHIP_ID_4352 || mac->chip_id == CHIP_ID_4360 ||
	       mac->chip_id == CHIP_ID_AA06;
}

/* docs/re/spec/bmac-init.md, C10: the GPIO mask of the coexistence (0 without a state) */
static u32 btc_gpio_mask(struct bcm4360_mac *mac)
{
	return mac->btc_valid ? mac->btc_gpio_mask : 0;
}

/*
 * docs/re/spec/bmac-init.md, C10 (sub_06333d, wlc_bmac_btc_gpio_disable):
 * release the GPIO lines of the Bluetooth coexistence. Without a GPIO mask
 * nothing is released and a crystal switched on here stays on.
 */
static void wlc_bmac_btc_gpio_disable(struct bcm4360_mac *mac)
{
	bool xtal_on = false;
	u16 v;

	/* step 1 */
	if (!mac->sbclk) {
		bcm4360_bmac_xtal(mac, true);
		xtal_on = true;
	}

	/* step 2: the crystal is not switched off again */
	if (btc_gpio_mask(mac) == 0)
		return;

	/* step 3 */
	(void)bcm4360_bmac_env_gpiocontrol(mac, mac->btc_gpio_mask, 0, 0);
	(void)bcm4360_bmac_env_gpioouten(mac, mac->btc_gpio_mask, 0, 0);
	/* chip 0x4313: si_gpiopull here; not this chip */
	(void)bcm4360_bmac_env_gpioout(mac, mac->btc_gpio_out, 0, 0);
	if (mac->clk) {
		v = d11_read16(mac->hw, D11_REG_0x49e);
		d11_write16(mac->hw, D11_REG_0x49e, (u16)(v & ~mac->btc_gpio_out));
	}
	if (mac->bustype == BUSTYPE_PCI && (mac->btc_gpio_mask & BTC_GPIO_BIT8))
		bcm4360_bmac_env_btcgpiowar(mac);

	/* step 4 */
	if (xtal_on)
		bcm4360_bmac_xtal(mac, false);
}

/*
 * docs/re/spec/bmac-init.md, C2 step 9: the GPIO lines of the LEDs to the
 * LEDs, their outputs set from the LED table, and the result kept in the LED
 * state.
 */
static void hw_up_led_gpio(struct bcm4360_mac *mac)
{
	u32 g = mac->led_gpio_mask;
	u32 a = 0, r, pin, i;
	u8 flag;

	(void)bcm4360_bmac_env_gpiocontrol(mac, g, 0, 0);
	(void)bcm4360_bmac_env_gpioled(mac, g, g);

	for (i = 0; i < LED_ENTRIES; i++) {
		bcm4360_bmac_env_led_entry(mac, i, &pin, &flag);
		if (flag == 0)
			a |= BIT(pin & LED_PIN_MASK);
	}

	if (!(mac->boardflags2 & BFL2_0x800)) {
		r = bcm4360_bmac_env_gpioout(mac, g, a & g, 0);
		(void)bcm4360_bmac_env_gpioouten(mac, g, g, 0);
	} else {
		(void)bcm4360_bmac_env_gpioout(mac, g, ~(a & g) & g, 0);
		r = bcm4360_bmac_env_gpioouten(mac, g, 0, 0);
		if (mac->ccrev >= CCREV_GPIOPULL) {
			(void)bcm4360_bmac_env_gpiopull(mac, true, g, 0);
			(void)bcm4360_bmac_env_gpiopull(mac, false, g, 0);
		}
	}

	bcm4360_bmac_env_led_gpio_store(mac, r, g);
}

/*
 * docs/re/spec/bmac-init.md, C2 (wlc_bmac_hw_up): the one-time preparation of
 * the chip after power-up (bus PCI, chip 0x4360). Nothing if the upper layer
 * has done it already (pub+0x40).
 */
void bcm4360_bmac_hw_up(struct bcm4360_mac *mac)
{
	if (bcm4360_bmac_env_pub_hw_up(mac))
		return;

	/* step 1 */
	if (bcm4360_bmac_env_wlc_has_offload(mac))
		bcm4360_bmac_env_survive_perst_war(mac, true, 0, 0);

	/* step 2 */
	if (mac->bustype == BUSTYPE_PCI)
		(void)bcm4360_bmac_env_ldo_war(mac, mac->chip_id);

	/* step 3 (chip 0xa886 on any bus: the same call; chip 0x4350 on PCI:
	 * si_pmu_chip_init; not this chip) */
	if (mac->mac_rev >= MAC_REV_40 && mac->bustype == BUSTYPE_PCI)
		bcm4360_bmac_env_pmu_res_init(mac);

	/* step 4: `clk` is 0 here, the last call only sets forcefastclk */
	bcm4360_bmac_xtal(mac, true);
	bcm4360_bmac_env_clkctl_init(mac);
	bcm4360_bmac_clkctl_clk(mac, CLK_FAST);

	/* step 5 */
	bcm4360_bmac_env_pcie_ltr_war(mac);

	/* step 6 */
	wlc_bmac_btc_gpio_disable(mac);

	/* step 7 (acts for chip revisions < 3 only) */
	if (mac->bustype == BUSTYPE_PCI && chip_is_pcie2_war_list(mac))
		bcm4360_bmac_env_4360_pcie2_war(mac, mac->pcie2_war_freq);

	/* step 8 (then chips 0xa8d8, 0xa8d9, 0xa99d: si_setcore; chip 0x4313:
	 * si_clk_pmu_htavail_set, si_pmu_synth_pwrsw_4313_war; not this chip) */
	if (mac->bustype == BUSTYPE_PCI)
		(void)bcm4360_bmac_env_pci_fixcfg(mac);

	/* step 9 */
	if (mac->sbclk)
		hw_up_led_gpio(mac);

	/* step 10 */
	bcm4360_bmac_env_phy_por_inform(mac);

	/* step 11 */
	mac->ucode_loaded = 0;
	bcm4360_bmac_env_pub_set_hw_up(mac);

	/* step 12: chips 0x4313 and 0xa8dc: work-arounds; not this chip */
}

/*
 * docs/re/spec/bmac-init.md, C3 (wlc_bmac_up_prep): crystal and fast clock on,
 * the PCIe bridge set up, and a core reset unless the hardware switch disables
 * the radio. Result 0, or -9 if the radio is disabled.
 */
s32 bcm4360_bmac_up_prep(struct bcm4360_mac *mac)
{
	/* step 1: chip 0x4350 revision 0: a chip control bit; not this chip */

	/* step 2 */
	bcm4360_bmac_xtal(mac, true);
	bcm4360_bmac_env_clkctl_init(mac);
	bcm4360_bmac_clkctl_clk(mac, CLK_FAST);

	/* step 3 (bus PCMCIA: si_setcore and si_pcmcia_init; not this card) */
	if (mac->bustype == BUSTYPE_PCI)
		bcm4360_bmac_env_pci_setup(mac, BIT(bcm4360_bmac_env_wlc_mac_coreidx(mac) & 31));

	/* steps 4, 5: the radio is enabled */
	if (!bcm4360_bmac_radio_read_hwdisabled(mac)) {
		if (mac->bustype == BUSTYPE_PCI)
			bcm4360_bmac_env_pci_up(mac);
		bcm4360_bmac_corereset(mac, CORERESET_BAND_FLAGS);
		return 0;
	}

	/* step 6 */
	if (mac->bustype == BUSTYPE_PCI)
		bcm4360_bmac_env_pci_down(mac);
	bcm4360_bmac_xtal(mac, false);
	return ERR_RADIO_HWDISABLED;
}

/*
 * docs/re/spec/bmac-init.md, C4 (wlc_bmac_up_finish): the interface is up,
 * the clock back to dynamic, interrupts on, then the energy detect of E9.
 * Result 0.
 */
s32 bcm4360_bmac_up_finish(struct bcm4360_mac *mac)
{
	/* step 1 */
	mac->up = 1;

	/* step 2 */
	bcm4360_bmac_env_phy_hw_state_upd(mac, true);

	/* step 3 */
	bcm4360_bmac_clkctl_clk(mac, CLK_DYNAMIC);

	/* step 4 */
	bcm4360_bmac_env_intrson(mac);

	/* step 5: the argument is 1 only for the HT-PHY (0 here, the call acts anyway) */
	bcm4360_bmac_env_ifsctl_edcrs_set(mac,
					  bcm4360_bmac_env_wlc_band_phytype(mac) == PHY_TYPE_HT);

	return 0;
}

/*
 * docs/re/spec/bmac-init.md, C5 (wlc_bmac_down_prep): interrupts off, the fast
 * clock forced and the coexistence GPIOs released (if the card is there),
 * then the PHY down. Nothing, result 0, if the interface is not up.
 */
s32 bcm4360_bmac_down_prep(struct bcm4360_mac *mac)
{
	if (!mac->up)
		return 0;

	/* step 1 */
	if (!bcm4360_bmac_deviceremoved(mac)) {
		(void)bcm4360_bmac_env_intrsoff(mac);
		bcm4360_bmac_clkctl_clk(mac, CLK_FAST);
		if (!mac->noreset)
			wlc_bmac_btc_gpio_disable(mac);
	} else {
		bcm4360_bmac_env_clear_intmask(mac);
	}

	/* step 2: chip 0x4331: SHM(0x64) = 0x480; not this chip */

	/* step 3 */
	return bcm4360_bmac_env_phy_down(mac);
}

/*
 * docs/re/spec/bmac-init.md, C8, sub_05fefa (wlc_flushqueues): reclaim
 * everything the DMA engines hold and clear the pending counters of the
 * upper layer.
 */
static void wlc_flushqueues(struct bcm4360_mac *mac)
{
	u32 i;

	/* step 1 */
	bcm4360_bmac_env_wlc_clear_68(mac);

	/* step 2 */
	for (i = 0; i < NR_TX_FIFOS; i++) {
		if (!(mac->dma_fifos & BIT(i)))
			continue;
		bcm4360_bmac_env_dma_txreclaim(mac, i, DMA_RANGE_ALL);
		bcm4360_bmac_env_wlc_clear_txpktpend(mac, i);
	}

	/* step 3 (the object does not check for the engine of FIFO 0; the
	 * service is called only where there is one). MAC revision 4: also the
	 * receive reclaim of FIFO 3; not this card. */
	if (mac->dma_fifos & BIT(RX_FIFO))
		bcm4360_bmac_env_dma_rxreclaim(mac, RX_FIFO);
}

/*
 * docs/re/spec/bmac-init.md, C6 (wlc_bmac_down_finish): the interface is down;
 * suspend, the upper layer's reset and the core disable if the core is up,
 * then the chip down. Result: that of wl_reset if it ran, else 0. Nothing,
 * result 0, if the interface is not up.
 */
s32 bcm4360_bmac_down_finish(struct bcm4360_mac *mac)
{
	s32 result = 0;

	if (!mac->up)
		return 0;

	/* step 1 */
	mac->up = 0;
	bcm4360_bmac_env_phy_hw_state_upd(mac, false);

	/* step 2: the card is gone: steps 3 and 4 are skipped */
	if (bcm4360_bmac_deviceremoved(mac)) {
		mac->sbclk = 0;
		mac->clk = 0;
		bcm4360_bmac_env_phy_hw_clk_state_upd(mac, false);
		wlc_flushqueues(mac);
		return 0;
	}

	/* step 3: wl_reset runs the upper layer's reset, which calls
	 * bcm4360_bmac_reset: the core is reset under us */
	if (bcm4360_bmac_env_core_iscoreup(mac)) {
		if (d11_read32(mac->hw, D11_MACCONTROL) & MCTL_EN_MAC)
			bcm4360_bmac_suspend(mac);
		result = (s32)bcm4360_bmac_env_wl_reset(mac);
		bcm4360_bmac_coredisable(mac);
	}

	/* step 4 */
	if (!mac->noreset)
		bcm4360_bmac_hw_down(mac);

	return result;
}

/* docs/re/spec/bmac-init.md, C15 step 2: the conditions of si_seci_down */
static bool hw_down_seci(struct bcm4360_mac *mac)
{
	return (mac->boardflags & BFL_0x1) && !(mac->boardflags2 & BFL2_0x80) &&
	       (mac->cccaps_ext & CC_CAPEXT_SECI) && (mac->machwcap & MCAP_BTCX);
}

/*
 * docs/re/spec/bmac-init.md, C15 (wlc_bmac_hw_down): the chip down: the chip
 * control bits, the PCIe bridge, the RF LDO and the crystal off.
 */
void bcm4360_bmac_hw_down(struct bcm4360_mac *mac)
{
	if (mac->bustype == BUSTYPE_PCI) {
		/* step 1 */
		bcm4360_bmac_set_ctrl_srom(mac);

		/* step 2 */
		if (hw_down_seci(mac))
			bcm4360_bmac_env_seci_down(mac);

		/* step 3 */
		(void)bcm4360_bmac_set_ctrl_bt_shd0(mac, false);

		/* step 4 */
		if (bcm4360_bmac_env_wlc_has_offload(mac)) {
			bcm4360_bmac_env_survive_perst_war(mac, false, SPERST_MASK_0x204, 0);
			(void)bcm4360_bmac_env_pmu_res_req_timer_clr(mac);
		}

		/* step 5 */
		bcm4360_bmac_env_pci_down(mac);
	}

	/* any bus */
	if (chip_is_rfldo_list(mac))
		bcm4360_bmac_env_pmu_rfldo(mac, false);
	bcm4360_bmac_xtal(mac, false);

	/* chip 0x4350 revision 0: chip control; not this chip */
}

/*
 * docs/re/spec/bmac-init.md, C8 (wlc_bmac_reset): a core reset without a
 * microcode download (unless the card is gone), then the queues flushed.
 */
void bcm4360_bmac_reset(struct bcm4360_mac *mac)
{
	/* step 1 */
	bcm4360_bmac_env_count_reset(mac);

	/* step 2 */
	if (!bcm4360_bmac_deviceremoved(mac))
		bcm4360_bmac_corereset(mac, CORERESET_BAND_FLAGS);

	/* step 3 (then wlc_reset_bmac_done: an empty function of wlc.c) */
	wlc_flushqueues(mac);
}

/*
 * docs/re/spec/bmac-init.md, D3 step 2: write `pattern` to SHM(0) as one
 * 32-bit word and read it back. Result: it reads back unchanged.
 */
static bool shm_pattern_ok(struct bcm4360_mac *mac, u32 pattern)
{
	u8 b[SHM_TEST_BYTES];

	store_le32(b, pattern);
	bcm4360_bmac_copyto_objmem(mac, SHM_TEST_OFFSET, b, SHM_TEST_BYTES, OBJ_SEL_SHM);
	bcm4360_bmac_copyfrom_objmem(mac, SHM_TEST_OFFSET, b, SHM_TEST_BYTES, OBJ_SEL_SHM);
	return bytes_le32(b) == pattern;
}

/*
 * docs/re/spec/bmac-init.md, D3 (wlc_bmac_validate_chip_access): the MAC core
 * answers: the shared memory holds what is written, and the MAC control
 * register has its reset value (with or without bit 31).
 */
bool bcm4360_bmac_validate_chip_access(struct bcm4360_mac *mac)
{
	u8 saved[SHM_TEST_BYTES];
	u32 v;

	/* MAC revisions <= 10: another test (D11(0x18c), D11(0x604/0x606)); not described */
	if (mac->mac_rev > MAC_REV_SHM_TEST) {
		/* step 1 */
		bcm4360_bmac_copyfrom_objmem(mac, SHM_TEST_OFFSET, saved, SHM_TEST_BYTES,
					     OBJ_SEL_SHM);

		/* step 2 */
		if (!shm_pattern_ok(mac, SHM_PATTERN_A) || !shm_pattern_ok(mac, SHM_PATTERN_B))
			return false;

		/* step 3 */
		bcm4360_bmac_copyto_objmem(mac, SHM_TEST_OFFSET, saved, SHM_TEST_BYTES,
					   OBJ_SEL_SHM);
	}

	/* step 4 */
	d11_write32(mac->hw, D11_REG_0x18c, 0);
	v = d11_read32(mac->hw, D11_MACCONTROL);
	return v == MCTL_RESET_VALUE || v == (MCTL_RESET_VALUE | MCTL_BIT31);
}
