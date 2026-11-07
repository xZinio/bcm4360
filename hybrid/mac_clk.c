/*
 * Hybrid build: the clocks, the core reset, the crystal and the core disable
 * of the MAC core in Broadcom's core (wlc_bmac.c, wlc_hw.c) by the open code
 * of open/mac/bmac_clk.c (docs/re/spec/bmac-init.md A10-A17, C7, C9, C11,
 * C13, C14; task docs/re/tasks/bmac-clk.md). Part of the MAC layer: switch
 * open_mac.
 *
 * The state goes in and out as for the other MAC functions (hyb_mac_in /
 * hyb_mac_out, hybrid/mac.c). These functions call other layers - the
 * backplane, the PHY, the DMA engines: those are the services of
 * open/include/bcm4360/mac_env.h, implemented here with Broadcom's functions.
 * The PHY can call back into the MAC (wlapi_*), so around a PHY service the
 * open state is written back first and read again after it.
 *
 * Parameters of type bool of the object are bytes (u8); integer arguments
 * narrower than 32 bit arrive as u32 and are truncated.
 */
#include "hybrid.h"
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>

/* wlc_hw_info_t: docs/re/spec/bmac-init.md, "Data" */
#define WLC_HW_DI		0x020	/* DMA engines of the FIFOs 0..5 */
#define WLC_HW_INTSTATUS	0x094	/* pending interrupt bits (u32) */
#define WLC_HW_SIH		0x0b8	/* si_t * */
#define WLC_HW_BAND		0x0e8	/* current band state */
#define BAND_PI			0x028	/* the PHY of the band */
/* the DMA engine: its first field points to the table of its functions */
#define DI_FN_TXRESET		0x10
#define DI_FN_RXRESET		0xa8

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

HYB_TAKE(void, wlc_bmac_phyclk_fgc, (void *wlc_hw, u32 on));
HYB_TAKE(void, wlc_bmac_macphyclk_set, (void *wlc_hw, u32 on));
HYB_TAKE(void, wlc_bmac_core_phy_clk, (void *wlc_hw, u32 on));
HYB_TAKE(void, wlc_bmac_corereset, (void *wlc_hw, u32 flags));
HYB_TAKE(void, wlc_bmac_phy_reset, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_core_phypll_ctl, (void *wlc_hw, u32 on));
HYB_TAKE(void, wlc_bmac_core_phypll_reset, (void *wlc_hw));
HYB_TAKE(u32, wlc_bmac_btc_mode_get, (void *wlc_hw));
HYB_TAKE(void, wlc_coredisable, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_xtal, (void *wlc_hw, u32 want));
HYB_TAKE(void, wlc_bmac_pllreq, (void *wlc_hw, u32 set, u32 req_bit));
HYB_TAKE(void, wlc_bmac_set_clk, (void *wlc_hw, u32 on));
HYB_TAKE(s32, wlc_bmac_set_ctrl_bt_shd0, (void *wlc_hw, u32 on));
HYB_TAKE(void, wlc_bmac_set_ctrl_SROM, (void *wlc_hw));
HYB_TAKE(u8, wlc_bmac_radio_read_hwdisabled, (void *wlc_hw));
HYB_TAKE(u8, wlc_hw_deviceremoved, (void *wlc_hw));

/* Broadcom's functions the services call (hybrid/takeover.txt: use) */
u32 si_core_cflags(void *sih, u32 mask, u32 val);
u8 si_iscoreup(void *sih);
void si_core_reset(void *sih, u32 bits, u32 resetbits);
void si_core_disable(void *sih, u32 bits);
s32 si_clkctl_xtal(void *sih, u32 what, u32 on);
u32 si_gpiocontrol(void *sih, u32 mask, u32 val, u32 priority);
u32 si_corereg(void *sih, u32 coreidx, u32 regoff, u32 mask, u32 val);
void si_chipcontrl_srom4360(void *sih, u32 on);
u8 si_deviceremoved(void *sih);
void wlc_phy_hw_clk_state_upd(void *pi, u32 newstate);
u16 wlc_phy_clk_bwbits(void *pi);
void wlc_phy_anacore(void *pi, u32 on);
void wlc_phy_switch_radio(void *pi, u32 on);

/* VOID_ADAPT / RET_ADAPT: the open function on the state of wlc_hw, or
 * Broadcom's with open_mac off */
#define VOID_ADAPT(name, open_name, params, open_args, blob_args)	\
void name params							\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_mac mac, mac0;						\
									\
	if (!hyb_mac_on(HYB_MAC_CLK)) {					\
		bcm4360_blob_##name blob_args;				\
		return;							\
	}								\
	hyb_mac_in(wlc_hw, &hw, &mac, &mac0);					\
	open_name open_args;						\
	hyb_mac_out(wlc_hw, &mac, &mac0);					\
}

#define RET_ADAPT(type, name, open_name, params, open_args, blob_args)	\
type name params							\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_mac mac, mac0;						\
	type r;								\
									\
	if (!hyb_mac_on(HYB_MAC_CLK))						\
		return bcm4360_blob_##name blob_args;			\
	hyb_mac_in(wlc_hw, &hw, &mac, &mac0);					\
	r = open_name open_args;					\
	hyb_mac_out(wlc_hw, &mac, &mac0);					\
	return r;							\
}

VOID_ADAPT(wlc_bmac_phyclk_fgc, bcm4360_bmac_phyclk_fgc, (void *wlc_hw, u32 on),
	   (&mac, (u8)on != 0), (wlc_hw, on))
VOID_ADAPT(wlc_bmac_macphyclk_set, bcm4360_bmac_macphyclk_set, (void *wlc_hw, u32 on),
	   (&mac, (u8)on != 0), (wlc_hw, on))
VOID_ADAPT(wlc_bmac_core_phy_clk, bcm4360_bmac_core_phy_clk, (void *wlc_hw, u32 on),
	   (&mac, (u8)on != 0), (wlc_hw, on))
VOID_ADAPT(wlc_bmac_corereset, bcm4360_bmac_corereset, (void *wlc_hw, u32 flags),
	   (&mac, flags), (wlc_hw, flags))
VOID_ADAPT(wlc_bmac_phy_reset, bcm4360_bmac_phy_reset, (void *wlc_hw), (&mac), (wlc_hw))
VOID_ADAPT(wlc_bmac_core_phypll_ctl, bcm4360_bmac_core_phypll_ctl, (void *wlc_hw, u32 on),
	   (&mac, (u8)on != 0), (wlc_hw, on))
VOID_ADAPT(wlc_bmac_core_phypll_reset, bcm4360_bmac_core_phypll_reset, (void *wlc_hw),
	   (&mac), (wlc_hw))
RET_ADAPT(u32, wlc_bmac_btc_mode_get, bcm4360_bmac_btc_mode_get, (void *wlc_hw), (&mac),
	  (wlc_hw))
VOID_ADAPT(wlc_coredisable, bcm4360_bmac_coredisable, (void *wlc_hw), (&mac), (wlc_hw))
VOID_ADAPT(wlc_bmac_xtal, bcm4360_bmac_xtal, (void *wlc_hw, u32 want),
	   (&mac, (u8)want != 0), (wlc_hw, want))
VOID_ADAPT(wlc_bmac_pllreq, bcm4360_bmac_pllreq, (void *wlc_hw, u32 set, u32 req_bit),
	   (&mac, (u8)set != 0, req_bit), (wlc_hw, set, req_bit))
VOID_ADAPT(wlc_bmac_set_clk, bcm4360_bmac_set_clk, (void *wlc_hw, u32 on),
	   (&mac, (u8)on != 0), (wlc_hw, on))
RET_ADAPT(s32, wlc_bmac_set_ctrl_bt_shd0, bcm4360_bmac_set_ctrl_bt_shd0, (void *wlc_hw, u32 on),
	  (&mac, (u8)on != 0), (wlc_hw, on))
VOID_ADAPT(wlc_bmac_set_ctrl_SROM, bcm4360_bmac_set_ctrl_srom, (void *wlc_hw), (&mac),
	   (wlc_hw))
RET_ADAPT(u8, wlc_bmac_radio_read_hwdisabled, bcm4360_bmac_radio_read_hwdisabled,
	  (void *wlc_hw), (&mac), (wlc_hw))
RET_ADAPT(u8, wlc_hw_deviceremoved, bcm4360_bmac_deviceremoved, (void *wlc_hw), (&mac),
	  (wlc_hw))

/* --- the services of open/include/bcm4360/mac_env.h --- */

static void *sih_of(struct bcm4360_mac *mac)
{
	return FIELD(mac->env, WLC_HW_SIH, void *);
}

static void *pi_of(struct bcm4360_mac *mac)
{
	void *band = FIELD(mac->env, WLC_HW_BAND, void *);

	return band ? FIELD(band, BAND_PI, void *) : NULL;
}

/* around code that may run the MAC layer again: write the state back, read it after */
static void flush(struct bcm4360_mac *mac)
{
	hyb_mac_sync(mac);
}

static void reload(struct bcm4360_mac *mac)
{
	hyb_mac_in(mac->env, mac->hw, mac, mac->env_shadow);
}

u32 bcm4360_bmac_env_core_cflags(struct bcm4360_mac *mac, u32 mask, u32 val)
{
	return si_core_cflags(sih_of(mac), mask, val);
}

bool bcm4360_bmac_env_core_iscoreup(struct bcm4360_mac *mac)
{
	return si_iscoreup(sih_of(mac)) != 0;
}

void bcm4360_bmac_env_core_reset(struct bcm4360_mac *mac, u32 bits, u32 resetbits)
{
	si_core_reset(sih_of(mac), bits, resetbits);
}

void bcm4360_bmac_env_core_disable(struct bcm4360_mac *mac, u32 bits)
{
	si_core_disable(sih_of(mac), bits);
}

s32 bcm4360_bmac_env_clkctl_xtal(struct bcm4360_mac *mac, u32 what, bool on)
{
	return si_clkctl_xtal(sih_of(mac), what, on ? 1 : 0);
}

u32 bcm4360_bmac_env_gpiocontrol(struct bcm4360_mac *mac, u32 mask, u32 val, u8 priority)
{
	return si_gpiocontrol(sih_of(mac), mask, val, priority);
}

u32 bcm4360_bmac_env_corereg(struct bcm4360_mac *mac, u32 coreidx, u32 regoff, u32 mask,
			     u32 val)
{
	return si_corereg(sih_of(mac), coreidx, regoff, mask, val);
}

void bcm4360_bmac_env_chipcontrol_srom4360(struct bcm4360_mac *mac, bool on)
{
	si_chipcontrl_srom4360(sih_of(mac), on ? 1 : 0);
}

bool bcm4360_bmac_env_bus_deviceremoved(struct bcm4360_mac *mac)
{
	return si_deviceremoved(sih_of(mac)) != 0;
}

void bcm4360_bmac_env_phy_hw_clk_state_upd(struct bcm4360_mac *mac, bool newstate)
{
	flush(mac);
	wlc_phy_hw_clk_state_upd(pi_of(mac), newstate ? 1 : 0);
	reload(mac);
}

u16 bcm4360_bmac_env_phy_clk_bwbits(struct bcm4360_mac *mac)
{
	u16 r;

	flush(mac);
	r = wlc_phy_clk_bwbits(pi_of(mac));
	reload(mac);
	return r;
}

void bcm4360_bmac_env_phy_anacore(struct bcm4360_mac *mac, bool on)
{
	flush(mac);
	wlc_phy_anacore(pi_of(mac), on ? 1 : 0);
	reload(mac);
}

void bcm4360_bmac_env_phy_switch_radio(struct bcm4360_mac *mac, bool on)
{
	flush(mac);
	wlc_phy_switch_radio(pi_of(mac), on ? 1 : 0);
	reload(mac);
}

typedef void (*dma_fn)(void *di);

static void dma_call(struct bcm4360_mac *mac, u32 fifo, u32 slot)
{
	void *di;
	dma_fn fn;

	if (fifo >= 6)
		return;
	di = FIELD(mac->env, WLC_HW_DI + 8 * fifo, void *);
	if (!di)
		return;
	fn = FIELD(FIELD(di, 0, void *), slot, dma_fn);
	fn(di);
}

void bcm4360_bmac_env_dma_txreset(struct bcm4360_mac *mac, u32 fifo)
{
	dma_call(mac, fifo, DI_FN_TXRESET);
}

void bcm4360_bmac_env_dma_rxreset(struct bcm4360_mac *mac, u32 fifo)
{
	dma_call(mac, fifo, DI_FN_RXRESET);
}

void bcm4360_bmac_env_clear_intstatus(struct bcm4360_mac *mac)
{
	FIELD(mac->env, WLC_HW_INTSTATUS, u32) = 0;
}
