/* SPDX-License-Identifier: ISC */
/*
 * The entry points of the PHY that docs/re/spec/acphy-api.md adds to the
 * interface of phy.h: with them the open PHY answers all 79 functions through
 * which the MAC layer reaches the PHY (hybrid/phy.c). Declarations only: the
 * table of that specification, transcribed. Each function is implemented from
 * the specification its comment names.
 */
#ifndef BCM4360_PHY_API_H
#define BCM4360_PHY_API_H

#include <bcm4360/types.h>

struct bcm4360_phy;

/* acphy-api.md, A2: wlc_phy_attach for the second band; true: the same PHY serves it */
bool bcm4360_phy_attach_band(struct bcm4360_phy *phy, u32 bandtype);

/* acphy-api.md, A3: stores and entry points without effect on the AC-PHY */
bool bcm4360_phy_get_encore(struct bcm4360_phy *phy);
s8 bcm4360_phy_preamble_override_get(struct bcm4360_phy *phy);
void bcm4360_phy_preamble_override_set(struct bcm4360_phy *phy, s8 val);
void bcm4360_phy_btclock_war(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_ofdm_rateset_war(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_chanspec_ch14_widefilter_set(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_bf_preempt_enable(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_initcal_enable(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_set_filt_war(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_tkip_rifs_war(struct bcm4360_phy *phy, u8 val);
void bcm4360_phy_antsel_init(struct bcm4360_phy *phy, bool lut_init);
void bcm4360_phy_freqtrack_start(struct bcm4360_phy *phy);
void bcm4360_phy_freqtrack_end(struct bcm4360_phy *phy);
void bcm4360_phy_interference_set(struct bcm4360_phy *phy, bool init);
void bcm4360_phy_acimode_noisemode_reset(struct bcm4360_phy *phy, u32 channel, bool clear_aci,
					 bool clear_noise, bool disassoc);

/* acphy-api.md, A4: the channel list */
u16 bcm4360_phy_chanspec_band_firstch(struct bcm4360_phy *phy, u32 band);
void bcm4360_phy_chanspec_band_validch(struct bcm4360_phy *phy, u32 band, u8 *chanvec);

/* acphy-api.md, A5 (acphy-chanspec.md, section 12) */
void bcm4360_phy_block_bbpll_change(struct bcm4360_phy *phy, bool block, bool going_down);

/* acphy-api.md, A6 */
s8 bcm4360_phy_get_femctrl_bt_wlan_ovrd(struct bcm4360_phy *phy);
void bcm4360_phy_set_femctrl_bt_wlan_ovrd(struct bcm4360_phy *phy, s8 state);

/* acphy-api.md, A7: 8 bytes */
const u8 *bcm4360_phy_get_ofdm_rate_lookup(void);

/* acphy-api.md, A8 */
void bcm4360_phy_bssinit(struct bcm4360_phy *phy, bool bonlyap, s8 noise);

/* acphy-api.md, A9 (acphy-cal-tx.md, sections 3 to 7) and acphy-cal-tx.md, section 8 */
void bcm4360_phy_cal_perical(struct bcm4360_phy *phy, u8 reason);
void bcm4360_phy_cal_init(struct bcm4360_phy *phy);

/* acphy-api.md, A10 and A11 */
s32 bcm4360_phy_ioctl(struct bcm4360_phy *phy, s32 cmd, u32 len, void *arg);
s32 bcm4360_phy_iovar_dispatch(struct bcm4360_phy *phy, u32 actionid, u16 type, void *params,
			       u32 plen, void *arg, s32 alen, s32 vsize);

/* acphy-api.md, A12 to A15 */
u32 bcm4360_phy_down(struct bcm4360_phy *phy);
void bcm4360_phy_hold_upd(struct bcm4360_phy *phy, u32 id, bool on);
void bcm4360_phy_mute_upd(struct bcm4360_phy *phy, bool mute, u32 flags);
void bcm4360_phy_antsel_type_set(struct bcm4360_phy *phy, u8 type);

/* acphy-api-watchdog.md */
void bcm4360_phy_watchdog(struct bcm4360_phy *phy);

/* acphy-api-txpower.md; `void *` arguments are the MAC's per-rate power structures */
void bcm4360_phy_txpower_limit_set(struct bcm4360_phy *phy, void *txpwr);
s32 bcm4360_phy_txpower_set(struct bcm4360_phy *phy, u32 qdbm, bool override, void *reg_pwr);
s32 bcm4360_phy_txpower_get(struct bcm4360_phy *phy, u32 *qdbm, u8 *override);
void bcm4360_phy_txpower_sromlimit(struct bcm4360_phy *phy, u16 chanspec, s8 *min_pwr,
				   void *max_pwr, u8 core);
u8 bcm4360_phy_txpower_get_target_max(struct bcm4360_phy *phy);
u8 bcm4360_phy_txpower_get_target_min(struct bcm4360_phy *phy);
bool bcm4360_phy_txpower_hw_ctrl_get(struct bcm4360_phy *phy);
bool bcm4360_phy_txpower_ipa_ison(struct bcm4360_phy *phy);
void bcm4360_phy_txpwr_percent_set(struct bcm4360_phy *phy, u8 percent);
s32 bcm4360_phy_txpower_core_offset_set(struct bcm4360_phy *phy, const void *offsets);
void bcm4360_phy_get_est_pout(struct bcm4360_phy *phy, u8 *est_pout, u8 *est_pout_adj,
			      u16 *est_pout_cck);
s8 bcm4360_phy_get_olpc_pwroffset(struct bcm4360_phy *phy, u16 bw);
s32 bcm4360_phy_tssivisible_thresh(struct bcm4360_phy *phy);
void bcm4360_phy_clear_tssi(struct bcm4360_phy *phy);

/* acphy-api-rx.md; `rxh`: the MAC's receive header of a frame */
void bcm4360_phy_rssi_compute(struct bcm4360_phy *phy, void *rxh);
s8 bcm4360_phy_noise_avg(struct bcm4360_phy *phy);
void bcm4360_phy_noise_sample_intr(struct bcm4360_phy *phy);
void bcm4360_phy_noise_sample_request_external(struct bcm4360_phy *phy);
void bcm4360_phy_interf_rssi_update(struct bcm4360_phy *phy, u16 chanspec, s8 rssi);
void bcm4360_phy_set_deaf(struct bcm4360_phy *phy, bool user);
void bcm4360_phy_stf_chain_set(struct bcm4360_phy *phy, u8 txchain, u8 rxchain);
u8 bcm4360_phy_stf_chain_active_get(struct bcm4360_phy *phy);
void bcm4360_phy_ant_rxdiv_set(struct bcm4360_phy *phy, u8 val);
bool bcm4360_phy_ant_rxdiv_get(struct bcm4360_phy *phy, u8 *val);
s32 bcm4360_phy_swdiv_ant_get(struct bcm4360_phy *phy, u8 bit, u8 *val);
s32 bcm4360_phy_swdiv_ant_set(struct bcm4360_phy *phy, u8 bit, bool on);

#endif
