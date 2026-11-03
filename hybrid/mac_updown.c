// SPDX-License-Identifier: ISC
/*
 * Hybrid build: up and down of the MAC core in Broadcom's core (wlc_bmac.c:
 * the one-time preparation after power-up, the steps before and after
 * wlc_bmac_init at every up, the steps before and after the down, the
 * hardware down, the reset with the flush of the DMA queues, the check of the
 * chip access at attach) by the open code of open/mac/bmac_updown.c
 * (docs/re/spec/bmac-init.md C2-C6, C8, C10, C15, D3; task
 * docs/re/tasks/bmac-updown.md). Part of the MAC layer: switch open_mac.
 *
 * As for the other MAC functions the state goes in and out with
 * hyb_mac_in/hyb_mac_out (hybrid/mac.c), and the services of
 * open/include/bcm4360/mac_env.h are implemented here with Broadcom's
 * functions; around those that can run MAC code (the PHY, wl_reset, the MAC
 * functions not open yet) the open state is written back first and read
 * again after.
 *
 * Parameters of type bool of the object are bytes (u8); integer arguments
 * narrower than 32 bit arrive as u32 and are truncated.
 */
#include "hybrid.h"
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>

/* wlc_hw_info_t, wlc_info_t, wlc_pub_t: docs/re/spec/bmac-init.md */
#define WLC_HW_WLC		0x000	/* wlc_info_t * */
#define WLC_HW_OSH		0x010	/* osl_t * */
#define WLC_HW_DI		0x020	/* DMA engines of the FIFOs 0..5 */
#define WLC_HW_INTMASK		0x098	/* the current interrupt mask (u32) */
#define WLC_HW_SIH		0x0b8	/* si_t * */
#define WLC_HW_BAND		0x0e8	/* current band state */
#define WLC_HW_LED		0x198	/* the LED state of wlc_led.c */
#define BAND_PI			0x028	/* the PHY of the band */
#define WLC_PUB			0x000	/* wlc_pub_t * */
#define WLC_WL			0x010	/* the glue's handle */
#define WLC_CORE		0x038	/* the upper layer's core state */
#define WLC_BAND		0x040	/* the upper layer's band */
#define WLC_OFFLOAD		0x750	/* the offload state, or NULL */
#define CORE_COREIDX		0x000	/* u32 */
#define CORE_TXPKTPEND		0x038	/* u16 [6] */
#define WLC_BAND_PHYTYPE	0x008	/* u16 */
#define PUB_HW_UP		0x040	/* u8 */
#define PUB_CNT			0x0a0	/* the counter block */
#define CNT_RESET		0x0b4	/* u32 */
#define LED_TABLE		0x008	/* 32 entries of 24 bytes */
#define LED_ENTRY_SIZE		24
#define LED_ENTRY_PIN		0x00	/* u32 */
#define LED_ENTRY_FLAG		0x04	/* u8 */
#define LED_GPIO_VAL		0x308	/* u32 */
#define LED_GPIO_MASK		0x30c	/* u32 */
/* the DMA engine: its first field points to the table of its functions */
#define DI_FN_TXRECLAIM		0x60
#define DI_FN_RXRECLAIM		0xe0

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

HYB_TAKE(void, wlc_bmac_hw_up, (void *wlc_hw));
HYB_TAKE(s32, wlc_bmac_up_prep, (void *wlc_hw));
HYB_TAKE(s32, wlc_bmac_up_finish, (void *wlc_hw));
HYB_TAKE(s32, wlc_bmac_down_prep, (void *wlc_hw));
HYB_TAKE(s32, wlc_bmac_down_finish, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_hw_down, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_reset, (void *wlc_hw));
HYB_TAKE(u8, wlc_bmac_validate_chip_access, (void *wlc_hw));

/* Broadcom's functions the services call (hybrid/takeover.txt: use, or takes
 * of other layers, which then run the open code of those layers) */
void si_survive_perst_war(void *sih, u32 reset, u32 sperst_mask, u32 sperst_val);
u8 si_ldo_war(void *sih, u32 devid);
void si_pmu_res_init(const void *sih, void *osh);
void si_clkctl_init(void *sih);
void si_pcie_ltr_war(void *sih);
s32 si_pci_fixcfg(void *sih);
void si_pci_setup(void *sih, u32 coremask);
void si_pci_up(void *sih);
void si_pci_down(void *sih);
void si_seci_down(void *sih);
u32 si_pmu_res_req_timer_clr(void *sih);
void si_btcgpiowar(void *sih);
u32 si_gpioout(void *sih, u32 mask, u32 val, u32 priority);
u32 si_gpioled(void *sih, u32 mask, u32 val);
u32 si_gpiopull(void *sih, u32 updown, u32 mask, u32 val);
void wlc_phy_por_inform(void *pi);
void wlc_phy_hw_state_upd(void *pi, u8 up);
u32 wlc_phy_down(void *pi);
void wlc_bmac_4360_pcie2_war(void *wlc_hw, u32 freq);
void wlc_bmac_ifsctl_edcrs_set(void *wlc_hw, u32 on);
/* the glue (src/wl/sys/wl_linux.c) */
u32 wl_reset(void *wl);
void wl_intrson(void *wl);

#define VOID_ADAPT(name, open_name)					\
void name(void *wlc_hw)							\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_mac mac;						\
									\
	if (!hyb_mac_on(HYB_MAC_UPDOWN)) {					\
		bcm4360_blob_##name(wlc_hw);				\
		return;							\
	}								\
	hyb_mac_in(wlc_hw, &hw, &mac);					\
	open_name(&mac);						\
	hyb_mac_out(wlc_hw, &mac);					\
}

#define RET_ADAPT(type, name, open_name)				\
type name(void *wlc_hw)							\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_mac mac;						\
	type r;								\
									\
	if (!hyb_mac_on(HYB_MAC_UPDOWN))						\
		return bcm4360_blob_##name(wlc_hw);			\
	hyb_mac_in(wlc_hw, &hw, &mac);					\
	r = (type)open_name(&mac);					\
	hyb_mac_out(wlc_hw, &mac);					\
	return r;							\
}

VOID_ADAPT(wlc_bmac_hw_up, bcm4360_bmac_hw_up)
RET_ADAPT(s32, wlc_bmac_up_prep, bcm4360_bmac_up_prep)
RET_ADAPT(s32, wlc_bmac_up_finish, bcm4360_bmac_up_finish)
RET_ADAPT(s32, wlc_bmac_down_prep, bcm4360_bmac_down_prep)
RET_ADAPT(s32, wlc_bmac_down_finish, bcm4360_bmac_down_finish)
VOID_ADAPT(wlc_bmac_hw_down, bcm4360_bmac_hw_down)
VOID_ADAPT(wlc_bmac_reset, bcm4360_bmac_reset)
RET_ADAPT(u8, wlc_bmac_validate_chip_access, bcm4360_bmac_validate_chip_access)

/* --- the services of open/include/bcm4360/mac_env.h for up and down --- */

static void *sih_of(struct bcm4360_mac *mac)
{
	return FIELD(mac->env, WLC_HW_SIH, void *);
}

static void *pi_of(struct bcm4360_mac *mac)
{
	void *band = FIELD(mac->env, WLC_HW_BAND, void *);

	return band ? FIELD(band, BAND_PI, void *) : NULL;
}

static void *wlc_of(struct bcm4360_mac *mac)
{
	return FIELD(mac->env, WLC_HW_WLC, void *);
}

static void *pub_of(struct bcm4360_mac *mac)
{
	return FIELD(wlc_of(mac), WLC_PUB, void *);
}

static void *led_of(struct bcm4360_mac *mac)
{
	return FIELD(mac->env, WLC_HW_LED, void *);
}

static void flush(struct bcm4360_mac *mac)
{
	hyb_mac_out(mac->env, mac);
}

static void reload(struct bcm4360_mac *mac)
{
	hyb_mac_in(mac->env, mac->hw, mac);
}

void bcm4360_bmac_env_survive_perst_war(struct bcm4360_mac *mac, bool reset, u32 sperst_mask,
					u32 sperst_val)
{
	si_survive_perst_war(sih_of(mac), reset ? 1 : 0, sperst_mask, sperst_val);
}

bool bcm4360_bmac_env_ldo_war(struct bcm4360_mac *mac, u32 devid)
{
	return si_ldo_war(sih_of(mac), devid) != 0;
}

void bcm4360_bmac_env_pmu_res_init(struct bcm4360_mac *mac)
{
	si_pmu_res_init(sih_of(mac), FIELD(mac->env, WLC_HW_OSH, void *));
}

void bcm4360_bmac_env_clkctl_init(struct bcm4360_mac *mac)
{
	si_clkctl_init(sih_of(mac));
}

void bcm4360_bmac_env_pcie_ltr_war(struct bcm4360_mac *mac)
{
	si_pcie_ltr_war(sih_of(mac));
}

s32 bcm4360_bmac_env_pci_fixcfg(struct bcm4360_mac *mac)
{
	return si_pci_fixcfg(sih_of(mac));
}

void bcm4360_bmac_env_pci_setup(struct bcm4360_mac *mac, u32 coremask)
{
	si_pci_setup(sih_of(mac), coremask);
}

void bcm4360_bmac_env_pci_up(struct bcm4360_mac *mac)
{
	si_pci_up(sih_of(mac));
}

void bcm4360_bmac_env_pci_down(struct bcm4360_mac *mac)
{
	si_pci_down(sih_of(mac));
}

void bcm4360_bmac_env_seci_down(struct bcm4360_mac *mac)
{
	si_seci_down(sih_of(mac));
}

u32 bcm4360_bmac_env_pmu_res_req_timer_clr(struct bcm4360_mac *mac)
{
	return si_pmu_res_req_timer_clr(sih_of(mac));
}

void bcm4360_bmac_env_btcgpiowar(struct bcm4360_mac *mac)
{
	si_btcgpiowar(sih_of(mac));
}

u32 bcm4360_bmac_env_gpioout(struct bcm4360_mac *mac, u32 mask, u32 val, u8 priority)
{
	return si_gpioout(sih_of(mac), mask, val, priority);
}

u32 bcm4360_bmac_env_gpioled(struct bcm4360_mac *mac, u32 mask, u32 val)
{
	return si_gpioled(sih_of(mac), mask, val);
}

u32 bcm4360_bmac_env_gpiopull(struct bcm4360_mac *mac, bool updown, u32 mask, u32 val)
{
	return si_gpiopull(sih_of(mac), updown ? 1 : 0, mask, val);
}

void bcm4360_bmac_env_phy_por_inform(struct bcm4360_mac *mac)
{
	flush(mac);
	wlc_phy_por_inform(pi_of(mac));
	reload(mac);
}

void bcm4360_bmac_env_phy_hw_state_upd(struct bcm4360_mac *mac, bool up)
{
	flush(mac);
	wlc_phy_hw_state_upd(pi_of(mac), up ? 1 : 0);
	reload(mac);
}

s32 bcm4360_bmac_env_phy_down(struct bcm4360_mac *mac)
{
	s32 r;

	flush(mac);
	r = (s32)wlc_phy_down(pi_of(mac));
	reload(mac);
	return r;
}

typedef void (*dma_fn)(void *di);
typedef void (*dma_range_fn)(void *di, s32 range);

static void *di_of(struct bcm4360_mac *mac, u32 fifo)
{
	return fifo < 6 ? FIELD(mac->env, WLC_HW_DI + 8 * fifo, void *) : NULL;
}

void bcm4360_bmac_env_dma_txreclaim(struct bcm4360_mac *mac, u32 fifo, s32 range)
{
	void *di = di_of(mac, fifo);

	if (di)
		FIELD(FIELD(di, 0, void *), DI_FN_TXRECLAIM, dma_range_fn)(di, range);
}

void bcm4360_bmac_env_dma_rxreclaim(struct bcm4360_mac *mac, u32 fifo)
{
	void *di = di_of(mac, fifo);

	if (di)
		FIELD(FIELD(di, 0, void *), DI_FN_RXRECLAIM, dma_fn)(di);
}

void bcm4360_bmac_env_4360_pcie2_war(struct bcm4360_mac *mac, u32 freq)
{
	flush(mac);
	wlc_bmac_4360_pcie2_war(mac->env, freq);
	reload(mac);
}

void bcm4360_bmac_env_ifsctl_edcrs_set(struct bcm4360_mac *mac, bool on)
{
	flush(mac);
	wlc_bmac_ifsctl_edcrs_set(mac->env, on ? 1 : 0);
	reload(mac);
}

void bcm4360_bmac_env_intrson(struct bcm4360_mac *mac)
{
	wl_intrson(FIELD(wlc_of(mac), WLC_WL, void *));
}

u32 bcm4360_bmac_env_wl_reset(struct bcm4360_mac *mac)
{
	u32 r;

	flush(mac);
	r = wl_reset(FIELD(wlc_of(mac), WLC_WL, void *));
	reload(mac);
	return r;
}

bool bcm4360_bmac_env_pub_hw_up(struct bcm4360_mac *mac)
{
	return FIELD(pub_of(mac), PUB_HW_UP, u8) != 0;
}

void bcm4360_bmac_env_pub_set_hw_up(struct bcm4360_mac *mac)
{
	FIELD(pub_of(mac), PUB_HW_UP, u8) = 1;
}

bool bcm4360_bmac_env_wlc_has_offload(struct bcm4360_mac *mac)
{
	return FIELD(wlc_of(mac), WLC_OFFLOAD, void *) != NULL;
}

u32 bcm4360_bmac_env_wlc_mac_coreidx(struct bcm4360_mac *mac)
{
	return FIELD(FIELD(wlc_of(mac), WLC_CORE, void *), CORE_COREIDX, u32);
}

void bcm4360_bmac_env_wlc_clear_txpktpend(struct bcm4360_mac *mac, u32 fifo)
{
	if (fifo < 6)
		FIELD(FIELD(wlc_of(mac), WLC_CORE, void *), CORE_TXPKTPEND + 2 * fifo, u16) = 0;
}

u16 bcm4360_bmac_env_wlc_band_phytype(struct bcm4360_mac *mac)
{
	return FIELD(FIELD(wlc_of(mac), WLC_BAND, void *), WLC_BAND_PHYTYPE, u16);
}

void bcm4360_bmac_env_count_reset(struct bcm4360_mac *mac)
{
	FIELD(FIELD(pub_of(mac), PUB_CNT, void *), CNT_RESET, u32) += 1;
}

void bcm4360_bmac_env_clear_intmask(struct bcm4360_mac *mac)
{
	FIELD(mac->env, WLC_HW_INTMASK, u32) = 0;
}

void bcm4360_bmac_env_led_entry(struct bcm4360_mac *mac, u32 i, u32 *pin, u8 *flag)
{
	u8 *e = (u8 *)led_of(mac) + LED_TABLE + LED_ENTRY_SIZE * (i & 31);

	*pin = FIELD(e, LED_ENTRY_PIN, u32);
	*flag = FIELD(e, LED_ENTRY_FLAG, u8);
}

void bcm4360_bmac_env_led_gpio_store(struct bcm4360_mac *mac, u32 val, u32 mask)
{
	FIELD(led_of(mac), LED_GPIO_VAL, u32) = val;
	FIELD(led_of(mac), LED_GPIO_MASK, u32) = mask;
}
