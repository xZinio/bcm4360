// SPDX-License-Identifier: ISC
/*
 * The services of open/include/bcm4360/phy_env.h and hw.h (hybrid/phy.c with
 * the open functions in place of Broadcom's). The PHY holds `struct
 * bcm4360_hw *`, whose `owner` is the core.
 */
#include "core.h"
#include <bcm4360/phy_env.h>
#include <bcm4360/ratepwr.h>
#include <bcm4360/stf.h>
#include <bcm4360/nvram.h>

#define WAKE_OVERRIDE_PHYREG	2	/* bmac-init.md A18 */

/* --- MAC layer --- */

void bcm4360_mac_suspend(struct bcm4360_hw *hw)
{
	CORE_MAC_CALL(core_of_hw(hw), bcm4360_bmac_suspend(m));
}

void bcm4360_mac_enable(struct bcm4360_hw *hw)
{
	CORE_MAC_CALL(core_of_hw(hw), bcm4360_bmac_enable(m));
}

void bcm4360_mac_corereset(struct bcm4360_hw *hw, u32 flags)
{
	CORE_MAC_CALL(core_of_hw(hw), bcm4360_bmac_corereset(m, flags));
}

void bcm4360_mac_bw_set(struct bcm4360_hw *hw, u16 bw)
{
	CORE_MAC_CALL(core_of_hw(hw), bcm4360_bmac_bw_set(m, bw));
}

void bcm4360_mac_phyclk_fgc(struct bcm4360_hw *hw, bool on)
{
	CORE_MAC_CALL(core_of_hw(hw), bcm4360_bmac_phyclk_fgc(m, on));
}

void bcm4360_mac_mhf(struct bcm4360_hw *hw, u8 idx, u16 mask, u16 val, u32 bands)
{
	CORE_MAC_CALL(core_of_hw(hw), bcm4360_bmac_mhf(m, idx, mask, val, bands));
}

void bcm4360_mac_mctrl(struct bcm4360_hw *hw, u32 mask, u32 val)
{
	CORE_MAC_CALL(core_of_hw(hw), bcm4360_bmac_mctrl(m, mask, val));
}

u32 bcm4360_mac_btc_mode(struct bcm4360_hw *hw)
{
	u32 r = 0;

	CORE_MAC_CALL(core_of_hw(hw), r = bcm4360_bmac_btc_mode_get(m));
	return r;
}

/* acphy-api-watchdog.md "New services": TODO wlc-hw (no spec for wlc_bmac_btc_period_get) */
void bcm4360_mac_btc_period_get(struct bcm4360_hw *hw, u16 *period, u8 *state)
{
	(void)hw;
	*period = 0;
	*state = 0;
}

/* bmac-init.md A18, wlapi_is_eci_coex_enabled */
bool bcm4360_mac_eci_coex_enabled(struct bcm4360_hw *hw)
{
	struct bcm4360_core *c = core_of_hw(hw);

	if (FIELD(CORE_WLC_HW(c), HW_COREREV, u32) < 15 || !(c->sihp.cccaps & BIT(29)) ||
	    (c->sihp.cccaps_ext & 1))
		return false;
	return (FIELD(CORE_WLC_HW(c), 0x0a4, u32) >> 29) & 1;
}

void bcm4360_mac_wake_override_set(struct bcm4360_hw *hw)
{
	CORE_MAC_CALL(core_of_hw(hw), bcm4360_bmac_wake_override_set(m, WAKE_OVERRIDE_PHYREG));
}

void bcm4360_mac_wake_override_clear(struct bcm4360_hw *hw)
{
	CORE_MAC_CALL(core_of_hw(hw), bcm4360_bmac_wake_override_clear(m, WAKE_OVERRIDE_PHYREG));
}

void bcm4360_mac_update_bt_chanspec(struct bcm4360_hw *hw, u16 chanspec, bool scan,
				    bool measure)
{
	CORE_MAC_CALL(core_of_hw(hw), bcm4360_bmac_update_bt_chanspec(m, chanspec, scan, measure));
}

/* wlapi_high_update_phy_mode: the upper layer's copy of the PHY mode, TODO wlc-hw */
void bcm4360_mac_update_phy_mode(struct bcm4360_hw *hw, u32 mode)
{
	(void)hw; (void)mode;
}

/* wlc_update_txppr_offset (cca-stf.md part 2) */
void bcm4360_mac_update_txppr_offset(struct bcm4360_hw *hw, const void *offsets)
{
	bcm4360_stf_update_txppr_offset(core_of_hw(hw)->wlc, (void *)offsets);
}

/* no offload firmware: nobody answers */
s8 bcm4360_mac_ol_noise_avg(struct bcm4360_hw *hw)
{
	(void)hw;
	return 0;
}

/* wlapi_noise_cb: an external noise request completed; TODO wlc-hw (scan code) */
void bcm4360_mac_noise_cb(struct bcm4360_hw *hw, u8 chan, s8 v)
{
	(void)hw; (void)chan; (void)v;
}

/* --- timers --- */

void *bcm4360_timer_init(struct bcm4360_hw *hw, void (*fn)(void *arg), void *arg,
			 const char *name)
{
	return bcm4360_plat_timer_init(core_of_hw(hw)->plat, fn, arg, name);
}

void bcm4360_timer_add(struct bcm4360_hw *hw, void *timer, u32 ms, bool periodic)
{
	bcm4360_plat_timer_add(core_of_hw(hw)->plat, timer, ms, periodic);
}

bool bcm4360_timer_del(struct bcm4360_hw *hw, void *timer)
{
	return bcm4360_plat_timer_del(core_of_hw(hw)->plat, timer);
}

void bcm4360_timer_free(struct bcm4360_hw *hw, void *timer)
{
	bcm4360_plat_timer_free(core_of_hw(hw)->plat, timer);
}

/* --- chip layer --- */

u32 bcm4360_chip_core_sflags(struct bcm4360_hw *hw, u32 mask, u32 val)
{
	return bcm4360_si_core_sflags(&core_of_hw(hw)->si, mask, val);
}

u32 bcm4360_chip_core_cflags(struct bcm4360_hw *hw, u32 mask, u32 val)
{
	return bcm4360_si_core_cflags(&core_of_hw(hw)->si, mask, val);
}

u32 bcm4360_chip_corereg(struct bcm4360_hw *hw, u32 coreidx, u32 offset, u32 mask, u32 val)
{
	return bcm4360_si_corereg(&core_of_hw(hw)->si, coreidx, offset, mask, val);
}

u32 bcm4360_chip_gpiocontrol(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority)
{
	return bcm4360_si_gpiocontrol(&core_of_hw(hw)->si, mask, val, priority);
}

u32 bcm4360_chip_gpioout(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority)
{
	return bcm4360_si_gpioout(&core_of_hw(hw)->si, mask, val, priority);
}

u32 bcm4360_chip_gpioouten(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority)
{
	return bcm4360_si_gpioouten(&core_of_hw(hw)->si, mask, val, priority);
}

u32 bcm4360_chip_sromctl_get(struct bcm4360_hw *hw)
{
	return bcm4360_si_get_sromctl(&core_of_hw(hw)->si);
}

void bcm4360_chip_sromctl_set(struct bcm4360_hw *hw, u32 val)
{
	bcm4360_si_set_sromctl(&core_of_hw(hw)->si, val);
}

int bcm4360_otp_read_word(struct bcm4360_hw *hw, u32 word, u16 *data)
{
	return core_otp_read_word(core_of_hw(hw), word, data);
}

const char *hw_getvar(struct bcm4360_hw *hw, const char *name)
{
	return core_getvar(core_of_hw(hw)->vars, name);
}

/* --- the per-rate power container (hybrid/phy.c: ppr_* -> open/lib/ratepwr.c) --- */

typedef struct bcm4360_ratepwr P;

void *bcm4360_ppr_create(struct bcm4360_hw *hw, u8 bw_sel)
{
	return bcm4360_ratepwr_create(core_of_hw(hw)->osh, bw_sel);
}

void bcm4360_ppr_delete(struct bcm4360_hw *hw, void *ppr)
{
	bcm4360_ratepwr_delete(core_of_hw(hw)->osh, ppr);
}

void bcm4360_ppr_clear(void *ppr) { bcm4360_ratepwr_clear(ppr); }
void bcm4360_ppr_copy(const void *src, void *dst) { bcm4360_ratepwr_copy_struct(src, dst); }
u8 bcm4360_ppr_get_ch_bw(const void *ppr) { return (u8)bcm4360_ratepwr_get_ch_bw(ppr); }
void bcm4360_ppr_set_cmn_val(void *ppr, s8 v) { bcm4360_ratepwr_set_cmn_val(ppr, v); }
void bcm4360_ppr_plus_cmn_val(void *ppr, s8 d) { bcm4360_ratepwr_plus_cmn_val(ppr, d); }
void bcm4360_ppr_minus_cmn_val(void *ppr, s8 d) { bcm4360_ratepwr_minus_cmn_val(ppr, d); }
void bcm4360_ppr_cmn_val_minus(void *ppr, s8 v) { bcm4360_ratepwr_cmn_val_minus(ppr, v); }
void bcm4360_ppr_apply_min(void *ppr, s8 v) { bcm4360_ratepwr_apply_min(ppr, v); }
void bcm4360_ppr_apply_max(void *ppr, s8 v) { bcm4360_ratepwr_apply_max(ppr, v); }
void bcm4360_ppr_compare_min(void *a, void *b) { bcm4360_ratepwr_compare_min(a, b); }
void bcm4360_ppr_compare_max(void *a, void *b) { bcm4360_ratepwr_compare_max(a, b); }
void bcm4360_ppr_multiply_percentage(void *ppr, u8 pct) { bcm4360_ratepwr_multiply_percentage(ppr, pct); }
void bcm4360_ppr_force_disabled(void *ppr, s8 min) { bcm4360_ratepwr_force_disabled(ppr, min); }
s8 bcm4360_ppr_get_max(const void *ppr) { return bcm4360_ratepwr_get_max(ppr); }
s8 bcm4360_ppr_get_min(const void *ppr, s8 floor) { return bcm4360_ratepwr_get_min(ppr, floor); }

void bcm4360_ppr_get_ofdm(const void *ppr, u8 m, u8 rateset, u8 bw, s8 *out)
{
	bcm4360_ratepwr_get_ofdm(ppr, m, rateset, bw, out);
}

void bcm4360_ppr_set_ofdm(void *ppr, u8 m, u8 rateset, u8 bw, const s8 *in)
{
	bcm4360_ratepwr_set_ofdm(ppr, m, rateset, bw, in);
}

void bcm4360_ppr_get_ht_mcs(const void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, s8 *out)
{
	bcm4360_ratepwr_get_ht_mcs(ppr, nss, m, rateset, bw, out);
}

void bcm4360_ppr_set_ht_mcs(void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, const s8 *in)
{
	bcm4360_ratepwr_set_ht_mcs(ppr, nss, m, rateset, bw, in);
}

void bcm4360_ppr_get_vht_mcs(const void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, s8 *out)
{
	bcm4360_ratepwr_get_vht_mcs(ppr, nss, m, rateset, bw, out);
}

void bcm4360_ppr_set_vht_mcs(void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, const s8 *in)
{
	bcm4360_ratepwr_set_vht_mcs(ppr, nss, m, rateset, bw, in);
}

void bcm4360_ppr_get_dsss(const void *ppr, u8 m, u8 rateset, s8 *out)
{
	bcm4360_ratepwr_get_dsss(ppr, m, rateset, out);
}

void bcm4360_ppr_set_dsss(void *ppr, u8 m, u8 rateset, const s8 *in)
{
	bcm4360_ratepwr_set_dsss(ppr, m, rateset, in);
}
