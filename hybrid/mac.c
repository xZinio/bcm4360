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
#define WLC_HW_WLC		0x000	/* wlc_info_t * (argument of the offload semaphore) */
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
#define WLC_HW_SRL		0x104	/* short retry limit (u16, E7) */
#define WLC_HW_LRL		0x106	/* long retry limit (u16, E7) */
#define WLC_HW_UP		0x10c	/* the interface is up (u8) */
#define WLC_HW_ETHERADDR	0x178	/* saved hardware MAC address (6 bytes, E1) */
#define WLC_HW_DI		0x020	/* DMA engines of the transmit FIFOs 0..5 (8 bytes each) */
#define WLC_HW_GPIO_SEL		0x0ac	/* u16, C7 step 3 */
#define WLC_HW_BTC		0x0b0	/* coexistence state; its first 32-bit word is the mode */
#define WLC_HW_PLLREQ		0x160	/* PLL request mask (u32, C9) */
#define WLC_HW_SUSPENDED_FIFOS	0x164	/* u8 */
#define WLC_HW_NORESET		0x184	/* u8 */
#define WLC_HW_FORCEFASTCLK	0x185	/* u8 */
#define WLC_HW_SBCLK		0x187	/* u8 */
#define WLC_HW_PHYCLK		0x190	/* u8 */
#define WLC_HW_UCODE_LOADED	0x0ae	/* u8, B2 */
#define WLC_HW_SHORTSLOT	0x102	/* u8, E6 */
#define WLC_HW_SFBL		0x108	/* u16 */
#define WLC_HW_LFBL		0x10a	/* u16 */
#define WLC_HW_BOARDFLAGS	0x08c	/* u32 */
#define WLC_HW_BOARDFLAGS2	0x090	/* u32 */
#define WLC_HW_MACHWCAP		0x0a4	/* u32 */
#define WLC_HW_STF_SS_MODE	0x1a0	/* u8, B12 */
#define WLC_HW_ANTSEL_TYPE	0x1a1	/* u8 */
#define WLC_HW_GPIO_CHIPCTL	0x1a4	/* u32, B1 step 15 */
#define WLC_HW_INTRCVLAZY	0x1ac	/* u32 */
#define WLC_HW_LED_GPIO_MASK	0x180	/* u32, C2 step 9 */
#define WLC_HW_PCIE2_WAR_FREQ	0x1c0	/* u32, C2 step 7 */
#define WLC_HW_DEVID		0x082	/* u16 */
#define WLC_HW_NBANDS		0x118	/* u32 */
#define WLC_HW_CHANSPEC		0x11c	/* u16 */
/* coexistence state (E10) */
#define BTC_MODE		0x00	/* u32 */
#define BTC_WIRE		0x04	/* s32 */
#define BTC_AA2G		0x18	/* u8 */
#define WLC_HW_MACHWCAP_BACKUP	0x0a8	/* u32 */
#define WLC_HW_BTSWITCH		0x1bc	/* s8 */
#define BTC_FLAGS		0x08	/* u16 */
#define BTC_GPIO_MASK		0x0c	/* u32 */
#define BTC_GPIO_OUT		0x10	/* u32 */
#define BTC_SHM_BASE		0x1a	/* u16 */
/* band state: the host flag copies (A9), the contention window (B11), the PHY */
#define BAND_MHFS		0x008	/* u16 [5] */
#define BAND_MHFS_N		5
#define BAND_CWMIN		0x014	/* u16 */
#define BAND_CWMAX		0x016	/* u16 */
#define BAND_COREFLAGS		0x018	/* u32 */
#define BAND_PHYTYPE		0x01c	/* u16 */
#define BAND_PI			0x028	/* the PHY of the band */
#define BAND_TYPE		0x000	/* u32: 1 = 5 GHz, 2 = 2.4 GHz */
#define BAND_UNIT		0x004	/* u32 */
#define BAND_PHYREV		0x01e	/* u16 */

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

/* the offload semaphore of Broadcom's wlc_offloads.c (bmac-init.md A8) */
void tcm_sem_enter(void *wlc);
void tcm_sem_exit(void *wlc);

HYB_TAKE(void, wlc_bmac_mctrl, (void *wlc_hw, u32 mask, u32 val));
HYB_TAKE(void, wlc_ucode_wake_override_set, (void *wlc_hw, u32 bit));
HYB_TAKE(void, wlc_ucode_wake_override_clear, (void *wlc_hw, u32 bit));
HYB_TAKE(void, wlc_bmac_suspend_mac_and_wait, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_enable_mac, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_mhf, (void *wlc_hw, u32 idx, u32 mask, u32 val, u32 bands));

void hyb_mac_in(void *wlc_hw, struct bcm4360_hw *hw, struct bcm4360_mac *mac)
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
	mac->up = FIELD(wlc_hw, WLC_HW_UP, u8);
	for (i = 0; i < 6; i++)
		mac->hw_etheraddr[i] = FIELD(wlc_hw, WLC_HW_ETHERADDR + i, u8);
	mac->srl = FIELD(wlc_hw, WLC_HW_SRL, u16);
	mac->lrl = FIELD(wlc_hw, WLC_HW_LRL, u16);
	mac->cwmin = cur ? FIELD(cur, BAND_CWMIN, u16) : 0;
	mac->cwmax = cur ? FIELD(cur, BAND_CWMAX, u16) : 0;
	mac->sem_ctx = FIELD(wlc_hw, WLC_HW_WLC, void *);
	mac->sem_enter = tcm_sem_enter;
	mac->sem_exit = tcm_sem_exit;
	mac->env = wlc_hw;
	mac->noreset = FIELD(wlc_hw, WLC_HW_NORESET, u8);
	mac->forcefastclk = FIELD(wlc_hw, WLC_HW_FORCEFASTCLK, u8);
	mac->sbclk = FIELD(wlc_hw, WLC_HW_SBCLK, u8);
	mac->phyclk = FIELD(wlc_hw, WLC_HW_PHYCLK, u8);
	mac->suspended_fifos = FIELD(wlc_hw, WLC_HW_SUSPENDED_FIFOS, u8);
	for (i = 0; i < 6; i++)
		if (FIELD(wlc_hw, WLC_HW_DI + 8 * i, void *))
			mac->dma_fifos |= 1 << i;
	mac->gpio_sel = FIELD(wlc_hw, WLC_HW_GPIO_SEL, u16);
	mac->pllreq = FIELD(wlc_hw, WLC_HW_PLLREQ, u32);
	if (FIELD(wlc_hw, WLC_HW_BTC, u32 *))
		mac->btc_mode = *FIELD(wlc_hw, WLC_HW_BTC, u32 *);
	if (cur) {
		mac->band_valid = 1;
		mac->band_has_phy = FIELD(cur, BAND_PI, void *) != NULL;
		mac->band_phytype = FIELD(cur, BAND_PHYTYPE, u16);
		mac->band_coreflags = FIELD(cur, BAND_COREFLAGS, u32);
		mac->band_type = FIELD(cur, BAND_TYPE, u32);
		mac->band_phyrev = FIELD(cur, BAND_PHYREV, u16);
		mac->band_unit = FIELD(cur, BAND_UNIT, u32);
	}
	mac->nbands = FIELD(wlc_hw, WLC_HW_NBANDS, u32);
	mac->devid = FIELD(wlc_hw, WLC_HW_DEVID, u16);
	mac->chanspec = FIELD(wlc_hw, WLC_HW_CHANSPEC, u16);
	if (sih) {
		mac->bustype = sih->bustype;
		mac->cccaps = sih->cccaps;
		mac->pmurev = (u32)sih->pmurev;
		mac->chiprev = sih->chiprev;
		mac->cccaps_ext = sih->cccaps_ext;
		mac->boardvendor = sih->boardvendor;
		mac->boardtype = sih->boardtype;
		mac->ccrev = (u32)sih->ccrev;
	}
	mac->led_gpio_mask = FIELD(wlc_hw, WLC_HW_LED_GPIO_MASK, u32);
	mac->pcie2_war_freq = FIELD(wlc_hw, WLC_HW_PCIE2_WAR_FREQ, u32);
	mac->ucode_loaded = FIELD(wlc_hw, WLC_HW_UCODE_LOADED, u8);
	mac->antsel_type = FIELD(wlc_hw, WLC_HW_ANTSEL_TYPE, u8);
	mac->shortslot = FIELD(wlc_hw, WLC_HW_SHORTSLOT, u8);
	mac->stf_ss_mode = FIELD(wlc_hw, WLC_HW_STF_SS_MODE, u8);
	mac->sfbl = FIELD(wlc_hw, WLC_HW_SFBL, u16);
	mac->lfbl = FIELD(wlc_hw, WLC_HW_LFBL, u16);
	mac->boardflags = FIELD(wlc_hw, WLC_HW_BOARDFLAGS, u32);
	mac->boardflags2 = FIELD(wlc_hw, WLC_HW_BOARDFLAGS2, u32);
	mac->machwcap = FIELD(wlc_hw, WLC_HW_MACHWCAP, u32);
	mac->intrcvlazy = FIELD(wlc_hw, WLC_HW_INTRCVLAZY, u32);
	mac->gpio_chipctl = FIELD(wlc_hw, WLC_HW_GPIO_CHIPCTL, u32);
	if (FIELD(wlc_hw, WLC_HW_BTC, void *)) {
		void *btc = FIELD(wlc_hw, WLC_HW_BTC, void *);

		mac->btc_valid = 1;
		mac->btc_flags = FIELD(btc, BTC_FLAGS, u16);
		mac->btc_gpio_mask = FIELD(btc, BTC_GPIO_MASK, u32);
		mac->btc_gpio_out = FIELD(btc, BTC_GPIO_OUT, u32);
		mac->btc_shm_base = FIELD(btc, BTC_SHM_BASE, u16);
		mac->btc_wire = FIELD(btc, BTC_WIRE, s32);
		mac->btc_aa2g = FIELD(btc, BTC_AA2G, u8);
	}
	mac->machwcap_backup = FIELD(wlc_hw, WLC_HW_MACHWCAP_BACKUP, u32);
	mac->btswitch = FIELD(wlc_hw, WLC_HW_BTSWITCH, s8);
}

void hyb_mac_out(void *wlc_hw, const struct bcm4360_mac *mac)
{
	void *cur = FIELD(wlc_hw, WLC_HW_BAND, void *);
	void *b2g = FIELD(wlc_hw, WLC_HW_BAND_2G, void *);
	void *b5g = FIELD(wlc_hw, WLC_HW_BAND_5G, void *);
	int i;

	for (i = 0; i < 6; i++)
		FIELD(wlc_hw, WLC_HW_ETHERADDR + i, u8) = mac->hw_etheraddr[i];
	FIELD(wlc_hw, WLC_HW_SRL, u16) = mac->srl;
	FIELD(wlc_hw, WLC_HW_LRL, u16) = mac->lrl;
	FIELD(wlc_hw, WLC_HW_UP, u8) = mac->up;
	FIELD(wlc_hw, WLC_HW_CHANSPEC, u16) = mac->chanspec;
	if (cur) {
		FIELD(cur, BAND_CWMIN, u16) = mac->cwmin;
		FIELD(cur, BAND_CWMAX, u16) = mac->cwmax;
	}
	FIELD(wlc_hw, WLC_HW_UCODE_LOADED, u8) = mac->ucode_loaded;
	FIELD(wlc_hw, WLC_HW_SHORTSLOT, u8) = mac->shortslot;
	FIELD(wlc_hw, WLC_HW_STF_SS_MODE, u8) = mac->stf_ss_mode;
	FIELD(wlc_hw, WLC_HW_FASTPWRUP_DLY, u16) = mac->fastpwrup_dly;
	if (FIELD(wlc_hw, WLC_HW_BTC, void *)) {
		void *btc = FIELD(wlc_hw, WLC_HW_BTC, void *);

		FIELD(btc, BTC_FLAGS, u16) = mac->btc_flags;
		FIELD(btc, BTC_SHM_BASE, u16) = mac->btc_shm_base;
		FIELD(btc, BTC_MODE, u32) = mac->btc_mode;
		FIELD(btc, BTC_WIRE, s32) = mac->btc_wire;
		FIELD(btc, BTC_GPIO_MASK, u32) = mac->btc_gpio_mask;
		FIELD(btc, BTC_GPIO_OUT, u32) = mac->btc_gpio_out;
	}
	FIELD(wlc_hw, WLC_HW_BTSWITCH, s8) = mac->btswitch;
	FIELD(wlc_hw, WLC_HW_CLK, u8) = mac->clk;
	FIELD(wlc_hw, WLC_HW_FORCEFASTCLK, u8) = mac->forcefastclk;
	FIELD(wlc_hw, WLC_HW_SBCLK, u8) = mac->sbclk;
	FIELD(wlc_hw, WLC_HW_PHYCLK, u8) = mac->phyclk;
	FIELD(wlc_hw, WLC_HW_SUSPENDED_FIFOS, u8) = mac->suspended_fifos;
	FIELD(wlc_hw, WLC_HW_PLLREQ, u32) = mac->pllreq;
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

	if (!hyb_mac_on(HYB_MAC_CTRL)) {
		bcm4360_blob_wlc_bmac_mctrl(wlc_hw, mask, val);
		return;
	}
	hyb_mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_mctrl(&mac, mask, val);
	hyb_mac_out(wlc_hw, &mac);
}

/* bmac-init.md A3 */
void wlc_ucode_wake_override_set(void *wlc_hw, u32 bit)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	if (!hyb_mac_on(HYB_MAC_CTRL)) {
		bcm4360_blob_wlc_ucode_wake_override_set(wlc_hw, bit);
		return;
	}
	hyb_mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_wake_override_set(&mac, bit);
	hyb_mac_out(wlc_hw, &mac);
}

/* bmac-init.md A4 */
void wlc_ucode_wake_override_clear(void *wlc_hw, u32 bit)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	if (!hyb_mac_on(HYB_MAC_CTRL)) {
		bcm4360_blob_wlc_ucode_wake_override_clear(wlc_hw, bit);
		return;
	}
	hyb_mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_wake_override_clear(&mac, bit);
	hyb_mac_out(wlc_hw, &mac);
}

/* bmac-init.md A6 */
void wlc_bmac_suspend_mac_and_wait(void *wlc_hw)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	if (!hyb_mac_on(HYB_MAC_CTRL)) {
		bcm4360_blob_wlc_bmac_suspend_mac_and_wait(wlc_hw);
		return;
	}
	hyb_mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_suspend(&mac);
	hyb_mac_out(wlc_hw, &mac);
}

/* bmac-init.md A7 */
void wlc_bmac_enable_mac(void *wlc_hw)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	if (!hyb_mac_on(HYB_MAC_CTRL)) {
		bcm4360_blob_wlc_bmac_enable_mac(wlc_hw);
		return;
	}
	hyb_mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_enable(&mac);
	hyb_mac_out(wlc_hw, &mac);
}

/* bmac-init.md A9 */
void wlc_bmac_mhf(void *wlc_hw, u32 idx, u32 mask, u32 val, u32 bands)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;

	if (!hyb_mac_on(HYB_MAC_CTRL)) {
		bcm4360_blob_wlc_bmac_mhf(wlc_hw, idx, mask, val, bands);
		return;
	}
	hyb_mac_in(wlc_hw, &hw, &mac);
	bcm4360_bmac_mhf(&mac, (u8)idx, (u16)mask, (u16)val, bands);
	hyb_mac_out(wlc_hw, &mac);
}
