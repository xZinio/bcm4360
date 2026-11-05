// SPDX-License-Identifier: ISC
/*
 * The services of open/include/bcm4360/mac_env.h, with the open functions in
 * place of Broadcom's. Each comment names the adapter of hybrid/ (mac_clk.c,
 * mac_init.c, mac_updown.c, mac_chan.c, mac_runtime.c, mac_btc.c) it
 * corresponds to. A service that can run MAC code (the PHY, the MAC
 * functions) writes the MAC state back first and reads it after
 * (core_mac_flush / core_mac_reload).
 */
#include "core.h"
#include <bcm4360/mac_env.h>
#include <bcm4360/phy_api.h>
#include <bcm4360/dma.h>
#include <bcm4360/stf.h>

#define SI(mac)		(&core_of_mac(mac)->si)
#define C(mac)		core_of_mac(mac)
#define WLC(mac)	(C(mac)->wlc)
#define PUB(mac)	(C(mac)->pub)

static struct bcm4360_dma *di_of(struct bcm4360_mac *mac, u32 fifo)
{
	return fifo < CORE_NFIFO ? FIELD(mac->env, HW_DI(fifo), struct bcm4360_dma *) : NULL;
}

/* --- backplane --- */

u32 bcm4360_bmac_env_core_cflags(struct bcm4360_mac *mac, u32 mask, u32 val)
{
	return bcm4360_si_core_cflags(SI(mac), mask, val);
}

bool bcm4360_bmac_env_core_iscoreup(struct bcm4360_mac *mac)
{
	return bcm4360_si_iscoreup(SI(mac));
}

void bcm4360_bmac_env_core_reset(struct bcm4360_mac *mac, u32 bits, u32 resetbits)
{
	bcm4360_si_core_reset(SI(mac), bits, resetbits);
}

void bcm4360_bmac_env_core_disable(struct bcm4360_mac *mac, u32 bits)
{
	bcm4360_si_core_disable(SI(mac), bits);
}

s32 bcm4360_bmac_env_clkctl_xtal(struct bcm4360_mac *mac, u32 what, bool on)
{
	return bcm4360_si_clkctl_xtal(SI(mac), what, on);
}

u32 bcm4360_bmac_env_gpiocontrol(struct bcm4360_mac *mac, u32 mask, u32 val, u8 priority)
{
	return bcm4360_si_gpiocontrol(SI(mac), mask, val, priority);
}

u32 bcm4360_bmac_env_corereg(struct bcm4360_mac *mac, u32 coreidx, u32 regoff, u32 mask,
			     u32 val)
{
	return bcm4360_si_corereg(SI(mac), coreidx, regoff, mask, val);
}

/* si_chipcontrl_srom4360 (srom-otp.md): ChipCommon chip control, bits 2, 3, 8, 9, 21 cleared */
void bcm4360_bmac_env_chipcontrol_srom4360(struct bcm4360_mac *mac, bool on)
{
	struct bcm4360_si *si = SI(mac);
	u32 orig = bcm4360_si_coreidx(si);
	void *cc = bcm4360_si_setcore(si, 0x800, 0);
	u32 v = hw_read32(cc, 0x28);

	if (on)
		hw_write32(cc, 0x28, v & 0xffdffcf3);
	bcm4360_si_setcoreidx(si, orig);
}

bool bcm4360_bmac_env_bus_deviceremoved(struct bcm4360_mac *mac)
{
	return bcm4360_si_deviceremoved(SI(mac));
}

u32 bcm4360_bmac_env_core_sflags(struct bcm4360_mac *mac, u32 mask, u32 val)
{
	return bcm4360_si_core_sflags(SI(mac), mask, val);
}

u16 bcm4360_bmac_env_clkctl_fast_pwrup_delay(struct bcm4360_mac *mac)
{
	return core_si_clkctl_fast_pwrup_delay(C(mac));
}

void bcm4360_bmac_env_pmu_rfldo(struct bcm4360_mac *mac, bool on)
{
	bcm4360_pmu_rfldo(&C(mac)->pmu, on);
}

u32 bcm4360_bmac_env_pmu_bb_vcofreq(struct bcm4360_mac *mac, u32 xtalfreq)
{
	return bcm4360_pmu_get_bb_vcofreq(&C(mac)->pmu, xtalfreq);
}

/* TODO chip-attach / bmac-init B1 step 8: the serial/ECI/GCI coexistence interfaces are
 * not specified (this board has none of them in the session) */
void bcm4360_bmac_env_seci_init(struct bcm4360_mac *mac, u8 mode) { (void)mac; (void)mode; }
void bcm4360_bmac_env_eci_init(struct bcm4360_mac *mac) { (void)mac; }
void bcm4360_bmac_env_gci_init(struct bcm4360_mac *mac) { (void)mac; }
void bcm4360_bmac_env_seci_down(struct bcm4360_mac *mac) { (void)mac; }
void bcm4360_bmac_env_seci_upd(struct bcm4360_mac *mac, bool enable) { (void)mac; (void)enable; }
void bcm4360_bmac_env_eci_notify_bt(struct bcm4360_mac *mac, u32 mask, u32 val, u32 interrupt)
{
	(void)mac; (void)mask; (void)val; (void)interrupt;
}
void bcm4360_bmac_env_btcgpiowar(struct bcm4360_mac *mac) { (void)mac; }

u32 bcm4360_bmac_env_gpioouten(struct bcm4360_mac *mac, u32 mask, u32 val, u8 priority)
{
	return bcm4360_si_gpioouten(SI(mac), mask, val, priority);
}

u32 bcm4360_bmac_env_gpioout(struct bcm4360_mac *mac, u32 mask, u32 val, u8 priority)
{
	return bcm4360_si_gpioout(SI(mac), mask, val, priority);
}

u32 bcm4360_bmac_env_gpioled(struct bcm4360_mac *mac, u32 mask, u32 val)
{
	return bcm4360_si_gpioled(SI(mac), mask, val);
}

/* si_gpiopull: only chip 0x4313 uses it (bmac-run.md); nothing for this chip */
u32 bcm4360_bmac_env_gpiopull(struct bcm4360_mac *mac, bool updown, u32 mask, u32 val)
{
	(void)mac; (void)updown; (void)mask; (void)val;
	return 0;
}

void bcm4360_bmac_env_survive_perst_war(struct bcm4360_mac *mac, bool reset, u32 sperst_mask,
					u32 sperst_val)
{
	bcm4360_si_survive_perst_war(SI(mac), reset, sperst_mask, sperst_val);
}

bool bcm4360_bmac_env_ldo_war(struct bcm4360_mac *mac, u32 devid)
{
	return bcm4360_si_ldo_war(SI(mac), devid);
}

void bcm4360_bmac_env_pmu_res_init(struct bcm4360_mac *mac)
{
	bcm4360_pmu_res_init(&C(mac)->pmu);
}

void bcm4360_bmac_env_clkctl_init(struct bcm4360_mac *mac)
{
	core_si_clkctl_init(C(mac));
}

void bcm4360_bmac_env_pcie_ltr_war(struct bcm4360_mac *mac)
{
	core_si_pcie_ltr_war(C(mac));
}

s32 bcm4360_bmac_env_pci_fixcfg(struct bcm4360_mac *mac)
{
	return core_si_pci_fixcfg(C(mac));
}

void bcm4360_bmac_env_pci_setup(struct bcm4360_mac *mac, u32 coremask)
{
	core_si_pci_setup(C(mac), coremask);
}

void bcm4360_bmac_env_pci_up(struct bcm4360_mac *mac) { core_si_pci_up(C(mac)); }
void bcm4360_bmac_env_pci_down(struct bcm4360_mac *mac) { core_si_pci_down(C(mac)); }

u32 bcm4360_bmac_env_pmu_res_req_timer_clr(struct bcm4360_mac *mac)
{
	core_si_pmu_res_req_timer_clr(C(mac));
	return 0;
}

/* --- the PHY of the current band --- */

void bcm4360_bmac_env_phy_hw_clk_state_upd(struct bcm4360_mac *mac, bool newstate)
{
	core_mac_flush(mac);
	bcm4360_phy_hw_clk_state_upd(core_pi_of_mac(mac), newstate);
	core_mac_reload(mac);
}

u16 bcm4360_bmac_env_phy_clk_bwbits(struct bcm4360_mac *mac)
{
	u16 r;

	core_mac_flush(mac);
	r = (u16)bcm4360_phy_clk_bwbits(core_pi_of_mac(mac));
	core_mac_reload(mac);
	return r;
}

void bcm4360_bmac_env_phy_anacore(struct bcm4360_mac *mac, bool on)
{
	core_mac_flush(mac);
	bcm4360_phy_anacore(core_pi_of_mac(mac), on);
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_switch_radio(struct bcm4360_mac *mac, bool on)
{
	core_mac_flush(mac);
	bcm4360_phy_switch_radio(core_pi_of_mac(mac), on);
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_chanspec_radio_set(struct bcm4360_mac *mac, u16 chanspec)
{
	core_mac_flush(mac);
	bcm4360_phy_chanspec_radio_set(core_pi_of_mac(mac), chanspec);
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_cal_init(struct bcm4360_mac *mac)
{
	core_mac_flush(mac);
	bcm4360_phy_cal_init(core_pi_of_mac(mac));
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_antsel_init(struct bcm4360_mac *mac, bool lut_init)
{
	core_mac_flush(mac);
	bcm4360_phy_antsel_init(core_pi_of_mac(mac), lut_init);
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_init(struct bcm4360_mac *mac, u16 chanspec)
{
	core_mac_flush(mac);
	bcm4360_phy_init(core_pi_of_mac(mac), chanspec);
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_por_inform(struct bcm4360_mac *mac)
{
	core_mac_flush(mac);
	bcm4360_phy_por_inform(core_pi_of_mac(mac));
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_hw_state_upd(struct bcm4360_mac *mac, bool up)
{
	core_mac_flush(mac);
	bcm4360_phy_hw_state_upd(core_pi_of_mac(mac), up);
	core_mac_reload(mac);
}

s32 bcm4360_bmac_env_phy_down(struct bcm4360_mac *mac)
{
	s32 r;

	core_mac_flush(mac);
	r = (s32)bcm4360_phy_down(core_pi_of_mac(mac));
	core_mac_reload(mac);
	return r;
}

void bcm4360_bmac_env_phy_initcal_enable(struct bcm4360_mac *mac, bool on)
{
	core_mac_flush(mac);
	bcm4360_phy_initcal_enable(core_pi_of_mac(mac), on);
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_txpower_limit_set(struct bcm4360_mac *mac, void *limits, u16 chanspec)
{
	(void)chanspec;
	core_mac_flush(mac);
	bcm4360_phy_txpower_limit_set(core_pi_of_mac(mac), limits);
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_chanspec_set(struct bcm4360_mac *mac, u16 chanspec)
{
	core_mac_flush(mac);
	bcm4360_phy_chanspec_set(core_pi_of_mac(mac), chanspec);
	core_mac_reload(mac);
}

u16 bcm4360_bmac_env_phy_chanspec_get(struct bcm4360_mac *mac)
{
	u16 r;

	core_mac_flush(mac);
	r = bcm4360_phy_chanspec_get(core_pi_of_mac(mac));
	core_mac_reload(mac);
	return r;
}

void bcm4360_bmac_env_phy_bw_state_set(struct bcm4360_mac *mac, u16 bw)
{
	core_mac_flush(mac);
	bcm4360_phy_bw_state_set(core_pi_of_mac(mac), bw);
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_mute_upd(struct bcm4360_mac *mac, bool on, u32 reason)
{
	core_mac_flush(mac);
	bcm4360_phy_mute_upd(core_pi_of_mac(mac), on, reason);
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_scramb_dyn_bw_en(struct bcm4360_mac *mac, bool on)
{
	core_mac_flush(mac);
	bcm4360_phy_set_scramb_dyn_bw_en(core_pi_of_mac(mac), on);
	core_mac_reload(mac);
}

void bcm4360_bmac_env_phy_set_femctrl_bt_wlan_ovrd(struct bcm4360_mac *mac, s8 sw)
{
	core_mac_flush(mac);
	bcm4360_phy_set_femctrl_bt_wlan_ovrd(core_pi_of_mac(mac), sw);
	core_mac_reload(mac);
}

/* --- the DMA engines (the function slots of the table) --- */

void bcm4360_bmac_env_dma_txreset(struct bcm4360_mac *mac, u32 fifo)
{
	struct bcm4360_dma *di = di_of(mac, fifo);

	if (di)
		bcm4360_dma_txreset(di);
}

void bcm4360_bmac_env_dma_rxreset(struct bcm4360_mac *mac, u32 fifo)
{
	struct bcm4360_dma *di = di_of(mac, fifo);

	if (di)
		bcm4360_dma_rxreset(di);
}

void bcm4360_bmac_env_dma_txinit(struct bcm4360_mac *mac, u32 fifo)
{
	struct bcm4360_dma *di = di_of(mac, fifo);

	if (di)
		bcm4360_dma_txinit(di);
}

void bcm4360_bmac_env_dma_rxinit(struct bcm4360_mac *mac, u32 fifo)
{
	struct bcm4360_dma *di = di_of(mac, fifo);

	if (di)
		bcm4360_dma_rxinit(di);
}

void bcm4360_bmac_env_dma_rxfill(struct bcm4360_mac *mac, u32 fifo)
{
	struct bcm4360_dma *di = di_of(mac, fifo);

	if (di)
		bcm4360_dma_rxfill(di);
}

void bcm4360_bmac_env_dma_txreclaim(struct bcm4360_mac *mac, u32 fifo, s32 range)
{
	struct bcm4360_dma *di = di_of(mac, fifo);

	if (di)
		bcm4360_dma_txreclaim(di, range);
}

void bcm4360_bmac_env_dma_rxreclaim(struct bcm4360_mac *mac, u32 fifo)
{
	struct bcm4360_dma *di = di_of(mac, fifo);

	if (di)
		bcm4360_dma_rxreclaim(di);
}

void bcm4360_bmac_env_dma_txsuspend(struct bcm4360_mac *mac, u32 fifo)
{
	struct bcm4360_dma *di = di_of(mac, fifo);

	if (di)
		bcm4360_dma_txsuspend(di);
}

void bcm4360_bmac_env_dma_txresume(struct bcm4360_mac *mac, u32 fifo)
{
	struct bcm4360_dma *di = di_of(mac, fifo);

	if (di)
		bcm4360_dma_txresume(di);
}

bool bcm4360_bmac_env_dma_txsuspended(struct bcm4360_mac *mac, u32 fifo)
{
	struct bcm4360_dma *di = di_of(mac, fifo);

	return di ? bcm4360_dma_txsuspended(di) : false;
}

/* --- interrupt state (wlc_hw+0x94, 0x98) and the interrupt registers --- */

void bcm4360_bmac_env_clear_intstatus(struct bcm4360_mac *mac)
{
	FIELD(mac->env, HW_INTSTATUS, u32) = 0;
}

u32 bcm4360_bmac_env_intstatus(struct bcm4360_mac *mac)
{
	return FIELD(mac->env, HW_INTSTATUS, u32);
}

void bcm4360_bmac_env_set_intstatus(struct bcm4360_mac *mac, u32 v)
{
	FIELD(mac->env, HW_INTSTATUS, u32) = v;
}

void bcm4360_bmac_env_clear_intmask(struct bcm4360_mac *mac)
{
	FIELD(mac->env, HW_INTMASK, u32) = 0;
}

/* wl_intrsoff / wl_intrsrestore of the glue: wlc_intrsoff keeps the mask in
 * wlc_hw+0x98 and writes the register; TODO wlc-hw (spec pending) */
u32 core_intrsoff(struct bcm4360_core *c)
{
	void *hw = CORE_WLC_HW(c);
	u32 old;

	if (!hw || !FIELD(hw, 0x186, u8))	/* the core's clock is off */
		return 0;
	old = FIELD(hw, HW_INTMASK, u32);
	if (!old)	/* observed (wlc-hw.md 1.2): no access when the mask is 0 already */
		return 0;
	d11_write32(c->hw, D11_MACINTMASK, 0);
	(void)d11_read32(c->hw, D11_MACINTMASK);
	FIELD(hw, HW_INTMASK, u32) = 0;
	return old;
}

void core_intrsrestore(struct bcm4360_core *c, u32 mask)
{
	void *hw = CORE_WLC_HW(c);

	if (!hw || !FIELD(hw, 0x186, u8))
		return;
	FIELD(hw, HW_INTMASK, u32) = mask;
	d11_write32(c->hw, D11_MACINTMASK, mask);
}

u32 bcm4360_bmac_env_intrsoff(struct bcm4360_mac *mac)
{
	core_mac_flush(mac);
	return core_intrsoff(C(mac));
}

void bcm4360_bmac_env_intrsrestore(struct bcm4360_mac *mac, u32 mask)
{
	core_mac_flush(mac);
	core_intrsrestore(C(mac), mask);
}

void bcm4360_bmac_env_intrson(struct bcm4360_mac *mac)
{
	struct bcm4360_core *c = C(mac);

	core_mac_flush(mac);
	/* wl_intrson: wlc_intrson restores the default mask (wlc_hw+0x9c) */
	core_intrsrestore(c, FIELD(CORE_WLC_HW(c), 0x09c, u32));
}

/* wl_reset: wl_intrsoff, wlc_reset (-> wlc_bmac_reset, C8), wl_intrsrestore */
u32 bcm4360_bmac_env_wl_reset(struct bcm4360_mac *mac)
{
	struct bcm4360_core *c = C(mac);
	u32 m;

	core_mac_flush(mac);
	m = core_intrsoff(c);
	CORE_MAC_CALL(c, bcm4360_bmac_reset(m));
	if (m)	/* observed (wlc-hw.md 4.1 step 9): no write when the mask was 0 */
		core_intrsrestore(c, m);
	core_mac_reload(mac);
	return 0;
}

/* --- MAC functions that are open but called from inside another one --- */

void bcm4360_bmac_env_4360_pcie2_war(struct bcm4360_mac *mac, u32 freq)
{
	/* bmac-init.md C12: acts for chip revisions < 3 only (TODO core: older revisions) */
	(void)mac; (void)freq;
}

void bcm4360_bmac_env_ifsctl_edcrs_set(struct bcm4360_mac *mac, bool on)
{
	CORE_MAC_NESTED(mac, bcm4360_bmac_ifsctl_edcrs_set(m, on));
}

void bcm4360_bmac_env_setxband(struct bcm4360_mac *mac, u32 unit)
{
	CORE_MAC_NESTED(mac, bcm4360_bmac_setxband(m, unit));
}

void bcm4360_bmac_env_btc_mode_set(struct bcm4360_mac *mac, u32 mode)
{
	CORE_MAC_NESTED(mac, bcm4360_bmac_btc_mode_set(m, (s32)mode));
}

void bcm4360_bmac_env_mute(struct bcm4360_mac *mac, bool on, u32 reason)
{
	CORE_MAC_NESTED(mac, bcm4360_bmac_mute(m, on, reason));
}

void bcm4360_bmac_env_tx_fifo_suspend(struct bcm4360_mac *mac, u32 fifo)
{
	CORE_MAC_NESTED(mac, bcm4360_bmac_tx_fifo_suspend(m, fifo));
}

void bcm4360_bmac_env_tx_fifo_resume(struct bcm4360_mac *mac, u32 fifo)
{
	CORE_MAC_NESTED(mac, bcm4360_bmac_tx_fifo_resume(m, fifo));
}

/* acphy-chanspec.md 2 step 1 (hybrid/mac_chan.c): the current band changes */
void bcm4360_bmac_env_band_select(struct bcm4360_mac *mac, u32 unit)
{
	void *wlc = WLC(mac);

	if (unit >= 2)
		return;
	core_mac_flush(mac);
	FIELD(mac->env, HW_BAND, void *) = FIELD(mac->env, HW_BANDSTATE(unit), void *);
	FIELD(wlc, WLC_BAND, void *) = FIELD(wlc, WLC_BANDSTATE(unit), void *);
	core_mac_reload(mac);
}

void bcm4360_bmac_env_band_phy_chanspec_radio_set(struct bcm4360_mac *mac, u32 unit,
						  u16 chanspec)
{
	void *band;

	if (unit >= 2)
		return;
	band = FIELD(mac->env, HW_BANDSTATE(unit), void *);
	core_mac_flush(mac);
	bcm4360_phy_chanspec_radio_set(band ? FIELD(band, BAND_PI, struct bcm4360_phy *) : NULL,
				       chanspec);
	core_mac_reload(mac);
}

/* --- the upper layer's state --- */

const char *bcm4360_bmac_env_getvar(struct bcm4360_mac *mac, const char *name)
{
	return core_getvar(FIELD(mac->env, HW_VARS, char *), name);
}

void bcm4360_bmac_env_pub_etheraddr(struct bcm4360_mac *mac, u8 *addr)
{
	hw_memcpy(addr, (u8 *)PUB(mac) + PUB_ETHERADDR, 6);
}

bool bcm4360_bmac_env_wlc_take_flag_718(struct bcm4360_mac *mac)
{
	u8 *flag = &FIELD(WLC(mac), WLC_FLAG_718, u8);

	if (!*flag)
		return false;
	*flag = 0;
	return true;
}

void bcm4360_bmac_env_wlc_clear_68(struct bcm4360_mac *mac)
{
	FIELD(WLC(mac), WLC_WORD_68, u32) = 0;
}

bool bcm4360_bmac_env_pub_hw_up(struct bcm4360_mac *mac)
{
	return FIELD(PUB(mac), PUB_HW_UP, u8) != 0;
}

void bcm4360_bmac_env_pub_set_hw_up(struct bcm4360_mac *mac)
{
	FIELD(PUB(mac), PUB_HW_UP, u8) = 1;
}

bool bcm4360_bmac_env_wlc_has_offload(struct bcm4360_mac *mac)
{
	return FIELD(WLC(mac), WLC_OFFLOAD, void *) != NULL;
}

u32 bcm4360_bmac_env_wlc_mac_coreidx(struct bcm4360_mac *mac)
{
	return FIELD(FIELD(WLC(mac), WLC_CORE, void *), CORE_COREIDX, u32);
}

void bcm4360_bmac_env_wlc_clear_txpktpend(struct bcm4360_mac *mac, u32 fifo)
{
	if (fifo < CORE_NFIFO)
		FIELD(FIELD(WLC(mac), WLC_CORE, void *), CORE_TXPKTPEND + 2 * fifo, u16) = 0;
}

u16 bcm4360_bmac_env_wlc_band_phytype(struct bcm4360_mac *mac)
{
	return FIELD(FIELD(WLC(mac), WLC_BAND, void *), 0x08, u16);
}

void bcm4360_bmac_env_count_reset(struct bcm4360_mac *mac)
{
	FIELD(FIELD(PUB(mac), PUB_CNT, void *), CNT_RESET, u32) += 1;
}

bool bcm4360_bmac_env_wlc_flag_64(struct bcm4360_mac *mac)
{
	return FIELD(WLC(mac), WLC_FLAG_64, u8) != 0;
}

s32 bcm4360_bmac_env_wlc_btc_mode(struct bcm4360_mac *mac)
{
	/* wlc_btc_mode_get(wlc): the word +0x0c of the coexistence state at wlc+0x690 */
	return FIELD(FIELD(WLC(mac), WLC_COEX, void *), 0x0c, s32);
}

/* the LED state of wlc_hw+0x198 (C2 step 9) */
void bcm4360_bmac_env_led_entry(struct bcm4360_mac *mac, u32 i, u32 *pin, u8 *flag)
{
	u8 *e = FIELD(mac->env, HW_LED, u8 *) + 8 + 24 * (i & 31);

	*pin = FIELD(e, 0, u32);
	*flag = FIELD(e, 4, u8);
}

void bcm4360_bmac_env_led_gpio_store(struct bcm4360_mac *mac, u32 val, u32 mask)
{
	void *led = FIELD(mac->env, HW_LED, void *);

	FIELD(led, 0x308, u32) = val;
	FIELD(led, 0x30c, u32) = mask;
}
