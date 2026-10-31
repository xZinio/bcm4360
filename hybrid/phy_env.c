// SPDX-License-Identifier: ISC
/*
 * Hybrid bridge, layer "phy", the services: what the open PHY needs from the
 * rest of the driver (open/include/bcm4360/phy_env.h, hw_getvar() and
 * hw_fw_data() of hw.h), provided by Broadcom's object - its PHY shim
 * (wlapi_*, wlc_phy_shim.c), its backplane functions (si_*), its OTP reader,
 * the board variables and the data tables in the object.
 *
 * Each service is the call the object's own PHY makes at that point; the
 * pairs are the ones the A/B tests compare (PHY_SERVICES in
 * tools/re/phy_scenarios.py). Integer arguments narrower than 32 bit are
 * passed on as the object's prototypes have them.
 */
#include <bcm4360/phy_env.h>
#include "phy_card.h"

/* the object's functions (wlc_phy_shim.c, siutils.c, bcmotp.c, bcmutils.c) */
void wlapi_suspend_mac_and_wait(void *physhim);
void wlapi_enable_mac(void *physhim);
void wlapi_bmac_corereset(void *physhim, u32 flags);
void wlapi_bmac_bw_set(void *physhim, u16 bw);
void wlapi_bmac_phyclk_fgc(void *physhim, bool clk);
void wlapi_bmac_mhf(void *physhim, u8 idx, u16 mask, u16 val, u32 bands);
void wlapi_bmac_mctrl(void *physhim, u32 mask, u32 val);
u32 wlapi_bmac_btc_mode_get(void *physhim);
void wlapi_bmac_ucode_wake_override_phyreg_set(void *physhim);
void wlapi_bmac_ucode_wake_override_phyreg_clear(void *physhim);
void wlapi_update_bt_chanspec(void *physhim, u16 chanspec, bool scan, bool measure);
void wlapi_high_update_phy_mode(void *physhim, u32 mode);
void wlapi_high_update_txppr_offset(void *physhim, const void *ppr);
void *wlapi_init_timer(void *physhim, void (*fn)(void *arg), void *arg, const char *name);
void wlapi_add_timer(void *physhim, void *t, u32 ms, u32 periodic);
u32 wlapi_del_timer(void *physhim, void *t);
void wlapi_free_timer(void *physhim, void *t);
u32 si_core_sflags(void *sih, u32 mask, u32 val);
u32 si_core_cflags(void *sih, u32 mask, u32 val);
u32 si_corereg(void *sih, u32 coreidx, u32 regoff, u32 mask, u32 val);
u32 si_gpiocontrol(void *sih, u32 mask, u32 val, u8 priority);
u32 si_gpioout(void *sih, u32 mask, u32 val, u8 priority);
u32 si_gpioouten(void *sih, u32 mask, u32 val, u8 priority);
u32 si_get_sromctl(void *sih);
u32 si_set_sromctl(void *sih, u32 value);
s32 otp_read_word(void *sih, u32 wn, u16 *data);
char *getvar(char *vars, const char *name);

#define SHIM(hw)	(hybrid_card(hw)->physhim)
#define SIH(hw)		(hybrid_card(hw)->sih)

/* MAC layer */
void bcm4360_mac_suspend(struct bcm4360_hw *hw)
{
	wlapi_suspend_mac_and_wait(SHIM(hw));
}

void bcm4360_mac_enable(struct bcm4360_hw *hw)
{
	wlapi_enable_mac(SHIM(hw));
}

void bcm4360_mac_corereset(struct bcm4360_hw *hw, u32 flags)
{
	wlapi_bmac_corereset(SHIM(hw), flags);
}

void bcm4360_mac_bw_set(struct bcm4360_hw *hw, u16 bw)
{
	wlapi_bmac_bw_set(SHIM(hw), bw);
}

void bcm4360_mac_phyclk_fgc(struct bcm4360_hw *hw, bool on)
{
	wlapi_bmac_phyclk_fgc(SHIM(hw), on);
}

void bcm4360_mac_mhf(struct bcm4360_hw *hw, u8 idx, u16 mask, u16 val, u32 bands)
{
	wlapi_bmac_mhf(SHIM(hw), idx, mask, val, bands);
}

void bcm4360_mac_mctrl(struct bcm4360_hw *hw, u32 mask, u32 val)
{
	wlapi_bmac_mctrl(SHIM(hw), mask, val);
}

u32 bcm4360_mac_btc_mode(struct bcm4360_hw *hw)
{
	return wlapi_bmac_btc_mode_get(SHIM(hw));
}

void bcm4360_mac_wake_override_set(struct bcm4360_hw *hw)
{
	wlapi_bmac_ucode_wake_override_phyreg_set(SHIM(hw));
}

void bcm4360_mac_wake_override_clear(struct bcm4360_hw *hw)
{
	wlapi_bmac_ucode_wake_override_phyreg_clear(SHIM(hw));
}

void bcm4360_mac_update_bt_chanspec(struct bcm4360_hw *hw, u16 chanspec, bool scan,
				    bool measure)
{
	wlapi_update_bt_chanspec(SHIM(hw), chanspec, scan, measure);
}

void bcm4360_mac_update_phy_mode(struct bcm4360_hw *hw, u32 mode)
{
	wlapi_high_update_phy_mode(SHIM(hw), mode);
}

void bcm4360_mac_update_txppr_offset(struct bcm4360_hw *hw, const void *offsets)
{
	wlapi_high_update_txppr_offset(SHIM(hw), offsets);
}

/* timers */
void *bcm4360_timer_init(struct bcm4360_hw *hw, void (*fn)(void *arg), void *arg,
			 const char *name)
{
	return wlapi_init_timer(SHIM(hw), fn, arg, name);
}

void bcm4360_timer_add(struct bcm4360_hw *hw, void *timer, u32 ms, bool periodic)
{
	wlapi_add_timer(SHIM(hw), timer, ms, periodic);
}

bool bcm4360_timer_del(struct bcm4360_hw *hw, void *timer)
{
	return wlapi_del_timer(SHIM(hw), timer) & 0xff;
}

void bcm4360_timer_free(struct bcm4360_hw *hw, void *timer)
{
	wlapi_free_timer(SHIM(hw), timer);
}

/* chip layer */
u32 bcm4360_chip_core_sflags(struct bcm4360_hw *hw, u32 mask, u32 val)
{
	return si_core_sflags(SIH(hw), mask, val);
}

u32 bcm4360_chip_core_cflags(struct bcm4360_hw *hw, u32 mask, u32 val)
{
	return si_core_cflags(SIH(hw), mask, val);
}

u32 bcm4360_chip_corereg(struct bcm4360_hw *hw, u32 coreidx, u32 offset, u32 mask, u32 val)
{
	return si_corereg(SIH(hw), coreidx, offset, mask, val);
}

u32 bcm4360_chip_gpiocontrol(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority)
{
	return si_gpiocontrol(SIH(hw), mask, val, priority);
}

u32 bcm4360_chip_gpioout(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority)
{
	return si_gpioout(SIH(hw), mask, val, priority);
}

u32 bcm4360_chip_gpioouten(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority)
{
	return si_gpioouten(SIH(hw), mask, val, priority);
}

u32 bcm4360_chip_sromctl_get(struct bcm4360_hw *hw)
{
	return si_get_sromctl(SIH(hw));
}

void bcm4360_chip_sromctl_set(struct bcm4360_hw *hw, u32 val)
{
	si_set_sromctl(SIH(hw), val);
}

int bcm4360_otp_read_word(struct bcm4360_hw *hw, u32 word, u16 *data)
{
	return otp_read_word(SIH(hw), word, data);
}

/* hw.h: the board variables and the object's data */
const char *hw_getvar(struct bcm4360_hw *hw, const char *name)
{
	return getvar(hybrid_card(hw)->vars, name);
}

const void *hw_fw_data(struct bcm4360_hw *hw, const char *name, u32 *size)
{
	const struct hybrid_fw *f;

	for (f = hybrid_fw_table; f->name; f++) {
		if (!hw_strcmp(f->name, name)) {
			if (size)
				*size = f->size;
			return f->data;
		}
	}
	return NULL;
}
