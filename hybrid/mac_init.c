// SPDX-License-Identifier: ISC
/*
 * Hybrid build: the initialisation of the MAC core in Broadcom's core
 * (wlc_bmac.c: wlc_bmac_init with the microcode download and start, the
 * initialisation lists, the buffer memory controller, the band
 * initialisation; the MAC clock frequency; slot time, rate table and
 * coexistence helpers) by the open code of open/mac/bmac_init.c
 * (docs/re/spec/bmac-init.md B1-B3, B6-B10, B12, E6, E10; task
 * docs/re/tasks/bmac-coreinit.md). Part of the MAC layer: switch open_mac.
 *
 * As for the other MAC functions the state goes in and out with
 * hyb_mac_in/hyb_mac_out (hybrid/mac.c), and the services of
 * open/include/bcm4360/mac_env.h are implemented here with Broadcom's
 * functions; around those that can run MAC code (the PHY, the MAC functions
 * not open yet) the open state is written back first and read again after.
 *
 * Broadcom's data the open code loads (microcode, initialisation lists, a
 * coexistence table) is found with hw_fw_data() (hybrid/fwdata.h).
 *
 * Parameters of type bool of the object are bytes (u8); integer arguments
 * narrower than 32 bit arrive as u32 and are truncated.
 */
#include "hybrid.h"
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>

/* wlc_hw_info_t, wlc_info_t: docs/re/spec/bmac-init.md */
#define WLC_HW_WLC		0x000	/* wlc_info_t * */
#define WLC_HW_DI		0x020	/* DMA engines of the FIFOs 0..5 */
#define WLC_HW_SIH		0x0b8	/* si_t * */
#define WLC_HW_VARS		0x0c0	/* the card's variables */
#define WLC_HW_BAND		0x0e8	/* current band state */
#define BAND_PI			0x028	/* the PHY of the band */
#define WLC_PUB			0x000	/* wlc_pub_t * */
#define WLC_WL			0x010	/* the glue's handle */
#define WLC_WORD_68		0x068	/* B1 step 30 */
#define WLC_FLAG_718		0x718	/* B1 step 27 */
#define PUB_ETHERADDR		0x008	/* 6 bytes */
/* the DMA engine: its first field points to the table of its functions */
#define DI_FN_TXINIT		0x08
#define DI_FN_RXINIT		0xa0
#define DI_FN_RXFILL		0xd8

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

HYB_TAKE(void, wlc_bmac_init, (void *wlc_hw, u32 chanspec, u32 mute));
HYB_TAKE(s32, wlc_bmac_wowlucode_start, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_switch_macfreq, (void *wlc_hw, u32 spurmode));
HYB_TAKE(void, wlc_bmac_set_extlna_pwrsave_shmem, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_txbw_update, (void *wlc_hw));
HYB_TAKE(void, wlc_bmac_band_stf_ss_set, (void *wlc_hw, u32 mode));
HYB_TAKE(void, wlc_bmac_set_shortslot, (void *wlc_hw, u32 shortslot));

/* Broadcom's functions the services call (hybrid/takeover.txt: use, or takes
 * of other layers, which then run the open code of those layers) */
u32 si_core_sflags(void *sih, u32 mask, u32 val);
u16 si_clkctl_fast_pwrup_delay(void *sih);
void si_pmu_rfldo(void *sih, u32 on);
u32 si_pmu_get_bb_vcofreq(void *sih, void *osh, s32 xtalfreq);
void *si_osh(void *sih);
void si_seci_init(void *sih, u32 mode);
void si_eci_init(void *sih);
void si_gci_init(void *sih);
u32 si_gpioouten(void *sih, u32 mask, u32 val, u32 priority);
void wlc_phy_chanspec_radio_set(void *pi, u32 chanspec);
void wlc_phy_cal_init(void *pi);
void wlc_phy_antsel_init(void *pi, u32 lut_init);
void wlc_phy_init(void *pi, u32 chanspec);
void wlc_setxband(void *wlc_hw, u32 unit);
s32 wlc_bmac_btc_mode_set(void *wlc_hw, s32 mode);
void wlc_bmac_mute(void *wlc_hw, u32 on, u32 reason);
char *getvar(char *vars, const char *name);
/* the glue (src/wl/sys/wl_linux.c) */
u32 wl_intrsoff(void *wl);
void wl_intrsrestore(void *wl, u32 mask);

#define VOID_ADAPT(name, open_name, params, open_args, blob_args)	\
void name params							\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_mac mac;						\
									\
	if (!bcm4360_open_mac) {					\
		bcm4360_blob_##name blob_args;				\
		return;							\
	}								\
	hyb_mac_in(wlc_hw, &hw, &mac);					\
	open_name open_args;						\
	hyb_mac_out(wlc_hw, &mac);					\
}

VOID_ADAPT(wlc_bmac_init, bcm4360_bmac_init, (void *wlc_hw, u32 chanspec, u32 mute),
	   (&mac, (u16)chanspec, (u8)mute != 0), (wlc_hw, chanspec, mute))
VOID_ADAPT(wlc_bmac_switch_macfreq, bcm4360_bmac_switch_macfreq, (void *wlc_hw, u32 spurmode),
	   (&mac, (u8)spurmode), (wlc_hw, spurmode))
VOID_ADAPT(wlc_bmac_set_extlna_pwrsave_shmem, bcm4360_bmac_set_extlna_pwrsave_shmem,
	   (void *wlc_hw), (&mac), (wlc_hw))
VOID_ADAPT(wlc_bmac_txbw_update, bcm4360_bmac_txbw_update, (void *wlc_hw), (&mac), (wlc_hw))
VOID_ADAPT(wlc_bmac_band_stf_ss_set, bcm4360_bmac_band_stf_ss_set, (void *wlc_hw, u32 mode),
	   (&mac, (u8)mode), (wlc_hw, mode))
VOID_ADAPT(wlc_bmac_set_shortslot, bcm4360_bmac_set_shortslot, (void *wlc_hw, u32 shortslot),
	   (&mac, (u8)shortslot != 0), (wlc_hw, shortslot))

s32 wlc_bmac_wowlucode_start(void *wlc_hw)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac;
	s32 r;

	if (!bcm4360_open_mac)
		return bcm4360_blob_wlc_bmac_wowlucode_start(wlc_hw);
	hyb_mac_in(wlc_hw, &hw, &mac);
	r = bcm4360_bmac_wowlucode_start(&mac);
	hyb_mac_out(wlc_hw, &mac);
	return r;
}

/* --- the services of open/include/bcm4360/mac_env.h for the initialisation --- */

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

static void flush(struct bcm4360_mac *mac)
{
	hyb_mac_out(mac->env, mac);
}

static void reload(struct bcm4360_mac *mac)
{
	hyb_mac_in(mac->env, mac->hw, mac);
}

u32 bcm4360_bmac_env_core_sflags(struct bcm4360_mac *mac, u32 mask, u32 val)
{
	return si_core_sflags(sih_of(mac), mask, val);
}

u16 bcm4360_bmac_env_clkctl_fast_pwrup_delay(struct bcm4360_mac *mac)
{
	return si_clkctl_fast_pwrup_delay(sih_of(mac));
}

void bcm4360_bmac_env_pmu_rfldo(struct bcm4360_mac *mac, bool on)
{
	si_pmu_rfldo(sih_of(mac), on ? 1 : 0);
}

u32 bcm4360_bmac_env_pmu_bb_vcofreq(struct bcm4360_mac *mac, u32 xtalfreq)
{
	void *sih = sih_of(mac);

	return si_pmu_get_bb_vcofreq(sih, si_osh(sih), (s32)xtalfreq);
}

void bcm4360_bmac_env_seci_init(struct bcm4360_mac *mac, u8 mode)
{
	si_seci_init(sih_of(mac), mode);
}

void bcm4360_bmac_env_eci_init(struct bcm4360_mac *mac)
{
	si_eci_init(sih_of(mac));
}

void bcm4360_bmac_env_gci_init(struct bcm4360_mac *mac)
{
	si_gci_init(sih_of(mac));
}

u32 bcm4360_bmac_env_gpioouten(struct bcm4360_mac *mac, u32 mask, u32 val, u8 priority)
{
	return si_gpioouten(sih_of(mac), mask, val, priority);
}

void bcm4360_bmac_env_phy_chanspec_radio_set(struct bcm4360_mac *mac, u16 chanspec)
{
	flush(mac);
	wlc_phy_chanspec_radio_set(pi_of(mac), chanspec);
	reload(mac);
}

void bcm4360_bmac_env_phy_cal_init(struct bcm4360_mac *mac)
{
	flush(mac);
	wlc_phy_cal_init(pi_of(mac));
	reload(mac);
}

void bcm4360_bmac_env_phy_antsel_init(struct bcm4360_mac *mac, bool lut_init)
{
	flush(mac);
	wlc_phy_antsel_init(pi_of(mac), lut_init ? 1 : 0);
	reload(mac);
}

void bcm4360_bmac_env_phy_init(struct bcm4360_mac *mac, u16 chanspec)
{
	flush(mac);
	wlc_phy_init(pi_of(mac), chanspec);
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

void bcm4360_bmac_env_dma_txinit(struct bcm4360_mac *mac, u32 fifo)
{
	dma_call(mac, fifo, DI_FN_TXINIT);
}

void bcm4360_bmac_env_dma_rxinit(struct bcm4360_mac *mac, u32 fifo)
{
	dma_call(mac, fifo, DI_FN_RXINIT);
}

void bcm4360_bmac_env_dma_rxfill(struct bcm4360_mac *mac, u32 fifo)
{
	dma_call(mac, fifo, DI_FN_RXFILL);
}

void bcm4360_bmac_env_setxband(struct bcm4360_mac *mac, u32 unit)
{
	flush(mac);
	wlc_setxband(mac->env, unit);
	reload(mac);
}

void bcm4360_bmac_env_btc_mode_set(struct bcm4360_mac *mac, u32 mode)
{
	flush(mac);
	wlc_bmac_btc_mode_set(mac->env, (s32)mode);
	reload(mac);
}

void bcm4360_bmac_env_mute(struct bcm4360_mac *mac, bool on, u32 reason)
{
	flush(mac);
	wlc_bmac_mute(mac->env, on ? 1 : 0, reason);
	reload(mac);
}

u32 bcm4360_bmac_env_intrsoff(struct bcm4360_mac *mac)
{
	return wl_intrsoff(FIELD(wlc_of(mac), WLC_WL, void *));
}

void bcm4360_bmac_env_intrsrestore(struct bcm4360_mac *mac, u32 mask)
{
	wl_intrsrestore(FIELD(wlc_of(mac), WLC_WL, void *), mask);
}

const char *bcm4360_bmac_env_getvar(struct bcm4360_mac *mac, const char *name)
{
	return getvar(FIELD(mac->env, WLC_HW_VARS, char *), name);
}

void bcm4360_bmac_env_pub_etheraddr(struct bcm4360_mac *mac, u8 *addr)
{
	const u8 *src = (const u8 *)FIELD(wlc_of(mac), WLC_PUB, void *) + PUB_ETHERADDR;
	int i;

	for (i = 0; i < 6; i++)
		addr[i] = src[i];
}

bool bcm4360_bmac_env_wlc_take_flag_718(struct bcm4360_mac *mac)
{
	u8 *flag = &FIELD(wlc_of(mac), WLC_FLAG_718, u8);

	if (!*flag)
		return false;
	*flag = 0;
	return true;
}

void bcm4360_bmac_env_wlc_clear_68(struct bcm4360_mac *mac)
{
	FIELD(wlc_of(mac), WLC_WORD_68, u32) = 0;
}
