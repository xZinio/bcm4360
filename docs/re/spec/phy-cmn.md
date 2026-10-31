# The PHY API layer (`wlc_phy_cmn.c`, `wlc_phy_iovar.c`) and its contract with the MAC

Status: work in progress (written as the analysis goes). Every statement was
read in the decompiler output; statements also checked in the disassembly or
in the emulator say so ("dis", "run", "trace"). Names of fields marked (bcm)
are the names the corresponding fields have in the open driver `brcmsmac`
(same code lineage, fit by position and use); all other names are mine.

## Scope

The 83 functions of the PHY files that the rest of the object calls (the
"entry points"), what each one does on the BCM4360 (AC-PHY, phy type 11, PHY
revision 0 or 1), and the internal functions of `wlc_phy_cmn.c` they reach on
that path. Entry points that other specifications already describe are only
listed with their prototype and a reference (see the ABI table in "Data").

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_phy_por_inform` | 0x0b1969 | 13 | original |
| `wlc_phy_btclock_war` | 0x0b1976 | 13 | original |
| `wlc_phy_preamble_override_set` | 0x0b1983 | 27 | original |
| `wlc_phy_preamble_override_get` | 0x0b199e | 12 | original |
| `wlc_phy_hw_clk_state_upd` | 0x0b19c7 | 24 | original |
| `wlc_phy_hw_state_upd` | 0x0b19df | 24 | original |
| `wlc_phy_mute_upd` | 0x0b1c2e | 66 | original |
| `wlc_phy_chanspec_ch14_widefilter_set` | 0x0b1cc9 | 35 | original |
| `wlc_phy_chanspec_band_firstch` | 0x0b1d3e | 223 | original |
| `wlc_phy_txpower_get` | 0x0b1e1d | 64 | original |
| `wlc_phy_txpower_get_target_min` | 0x0b1ea7 | 61 | original |
| `wlc_phy_txpower_get_target_max` | 0x0b1ee4 | 61 | original |
| `wlc_phy_txpwr_percent_set` | 0x0b1f62 | 13 | original |
| `wlc_phy_ofdm_rateset_war` | 0x0b1f7c | 13 | original |
| `wlc_phy_bf_preempt_enable` | 0x0b1f89 | 35 | original |
| `wlc_phy_txpower_hw_ctrl_get` | 0x0b1fac | 49 | original |
| `wlc_phy_antsel_type_set` | 0x0b2018 | 27 | original |
| `wlc_phy_ant_rxdiv_get` | 0x0b203f | 20 | original |
| `wlc_phy_BSSinit` | 0x0b224a | 154 | original |
| `wlc_phy_get_ofdm_rate_lookup` | 0x0b2598 | 13 | original |
| `wlc_phy_txpower_core_offset_set` | 0x0b25f7 | 25 | original |
| `wlc_phy_get_olpc_pwroffset` | 0x0b2913 | 86 | original |
| `wlc_phy_tssivisible_thresh` | 0x0b2969 | 28 | original |
| `wlc_phy_get_femctrl_bt_wlan_ovrd` | 0x0b2985 | 22 | original |
| `wlc_phy_set_femctrl_bt_wlan_ovrd` | 0x0b299b | 38 | original |
| `wlc_phy_down` | 0x0b2a13 | 125 | original |
| `wlc_phy_shared_detach` | 0x0b2cb4 | 28 | original |
| `wlc_phy_antsel_init` | 0x0b3351 | 24 | original |
| `wlc_phy_txpower_ipa_ison` | 0x0b5316 | 22 | original |
| `wlc_phy_tkip_rifs_war` | 0x0b532c | 24 | original |
| `wlc_phy_ldpc_override_set` | 0x0b5344 | 57 | original |
| `wlc_phy_stf_chain_active_get` | 0x0b54e0 | 407 | original |
| `wlc_phy_stf_chain_set` | 0x0b5677 | 87 | original |
| `wlc_phy_clear_tssi` | 0x0b5de2 | 184 | original |
| `wlc_phy_swdiv_ant_get` | 0x0b5edc | 170 | original |
| `wlc_phy_swdiv_ant_set` | 0x0b5f86 | 229 | original |
| `wlc_phy_chanspec_band_validch` | 0x0b70fa | 159 | original |
| `wlc_phy_interference_set` | 0x0b78c4 | 217 | original |
| `wlc_phy_set_deaf` | 0x0b7dab | 289 | original |
| `wlc_phy_acimode_noisemode_reset` | 0x0b8104 | 104 | original |
| `wlc_phy_interf_rssi_update` | 0x0b816c | 86 | original |
| `wlc_phy_txpower_sromlimit` | 0x0b8be8 | 188 | original |
| `wlc_phy_txpower_limit_set` | 0x0b94de | 71 | original |
| `wlc_phy_txpower_set` | 0x0b9525 | 293 | original |
| `wlc_phy_set_filt_war` | 0x0ba2ea | 24 | original |
| `wlc_phy_hold_upd` | 0x0ba576 | 57 | original |
| `wlc_phy_noise_sample_request_external` | 0x0bb8e8 | 23 | original |
| `wlc_phy_freqtrack_end` | 0x0bc7f2 | 78 | original |
| `wlc_phy_freqtrack_start` | 0x0bc840 | 58 | original |
| `wlc_phy_iovar_detach` | 0x0e1800 | 21 | original (wlc_phy_iovar.c) |
| `wlc_phy_iovar_attach` | 0x0e1815 | 66 | original (wlc_phy_iovar.c) |
| `wlc_phy_get_tx_power_offset` | 0x152d9e | 8 | original (wlc_phy_ssn.c) |
| `wlc_phy_get_tx_power_offset_by_mcs` | 0x152da6 | 8 | original (wlc_phy_ssn.c) |
| `sub_0b72a4` | 0x0b72a4 | 147 | assigned: `wlc_phy_cal_txpower_recalc_sw` |
| `sub_0b8ca4` | 0x0b8ca4 | 2106 | assigned: `wlc_phy_txpower_recalc_target` |
| `sub_0ba54c` | 0x0ba54c | 42 | assigned: `wlc_phy_scanroam_cache_cal` |
| `wlc_phy_ant_rxdiv_set` | 0x0b81c2 | 1518 | original (AC path only) |
| `wlc_phy_txpwr_apply_srom11` | 0x0b44d7 | 2562 | original |
| `sub_0b2716` | 0x0b2716 | 79 | assigned: `wlc_phy_txpwr_srom11_ofdm_rates` |
| `sub_0b2765` | 0x0b2765 | 79 | assigned: `wlc_phy_txpwr_srom11_vht_rates` |
| `sub_0b2690` | 0x0b2690 | 67 | assigned: `wlc_phy_txpwr_ofdm_offset_adj` |
| `sub_0b26d3` | 0x0b26d3 | 67 | assigned: `wlc_phy_txpwr_vht_offset_adj` |
| `sub_0b3369` | 0x0b3369 | 139 | assigned: `wlc_phy_ppr_set_ofdm_all` |
| `sub_0b42df` | 0x0b42df | 504 | assigned: `wlc_phy_ppr_set_vht_all` |
| `wlc_phy_get_est_pout` | 0x0bc87a | 577 | original (AC path) |
| `wlc_phy_rssi_compute` | 0x0b97cb | 1624 | original (AC path) |
| `sub_0baff6` | 0x0baff6 | 1478 | assigned: `wlc_phy_noise_sample_request` (AC path) |
| `wlc_phy_noise_sample_intr` | 0x0bb5bc | 789 | original (AC path) |
| `wlc_phy_noise_read_shmem` | 0x0b7ecc | 400 | original |
| `sub_0b701e` | 0x0b701e | 220 | assigned: `wlc_phy_noise_calc_dbm` |
| `wlc_phy_compute_dB` | 0x0b21e1 | 85 | original |
| `wlc_phy_noise_cb` | 0x0b805c | 168 | original |
| `wlc_phy_noise_avg` | 0x0b9e23 | 342 | original (AC path) |
| `wlc_phy_ioctl` | 0x0b9f79 | 859 | original |
| `wlc_phy_watchdog` | 0x0bb8ff | 3827 | original (AC path; completes acphy-cal-tx 11) |
| `wlc_phy_get_femctrl_bt_wlan_ovrd_acphy` | 0x09057a | 96 | original |
| `wlc_phy_btc_adjust_acphy` | 0x099609 | 79 | original |
| `wlc_phy_force_lpvco_2G` | 0x08ed84 | 41 | original |
| `wlc_phy_get_SROMnoiselvl_phy` | 0x0b2866 | 173 | original |
| `wlc_phy_iovar_dispatch` | 0x0c1a4e | 3174 | original (AC path; `sub_0c0f88` not described) |
| `sub_0e1857` | 0x0e1857 | 53 | assigned: `wlc_phy_doiovar` |
| `sub_0b7a5f` | 0x0b7a5f | 199 | assigned: `wlc_phy_iovar_oclscd` |
| `sub_0b79f9` | 0x0b79f9 | 102 | assigned: `wlc_phy_iovar_lnldo2` |

(More rows are added as the sections below are written.)

## Overview

The MAC layer (`wlc_bmac.c`, `wlc.c`, `wlc_stf.c`, `wlc_channel.c`, ...)
never touches the PHY's memory (`sh`, `pi`, `pi_ac`): everything crosses as a
call of one of the 83 entry points below, with `pi` (the MAC keeps it at
`band+0x28`) as first argument (exceptions: `wlc_phy_shared_attach`,
`wlc_phy_shared_detach`, `wlc_phy_get_ofdm_rate_lookup`, the two iovar
functions and the two `wlc_phy_ssn.c` stubs). The PHY calls the MAC back only
through the `wlapi_*` functions of `wlc_phy_shim.c` (section "Shim") with the
shim handle `sh+0x20` as first argument.

Most entry points are a switch over the PHY type `pi+0x160`; on the BCM4360
only the case 11 (AC) matters. Many are a single field store or a dispatch to
an AC-PHY function (`*_acphy`) that another specification describes; about a
third do nothing at all for the AC-PHY (they serve N, HT, LCN or A/G PHYs).

## Data

### D1. ABI of the 83 entry points

Types: `pi` = `phy_info_t *`; `sh` = `shared_phy_t *`; `bool` is one byte
(the callee tests only the low byte); `u8`/`s8`/`u16`/`u32` give the width the
callee really uses (dis). `chanspec` is a 16 bit chanspec (see
`acphy-chanspec.md` "Chanspec"). "Callers": the functions outside the PHY
files that call the entry point (from the index; `sub_` names are stripped
functions of the MAC/upper layers). "Run" = called in the standard emulator
session (`boundary.py`), with the arguments seen there. Column "AC-PHY" gives
what happens on the BCM4360; "no-op" = returns without any effect; "spec" =
the specification that describes it.

| # | Entry point (.text) | Prototype | Callers (outside the PHY) | AC-PHY behaviour | Where |
|---|---|---|---|---|---|
| 1 | `wlc_acphy_set_scramb_dyn_bw_en` (0x090bda) | `void (pi, bool en)` | `sub_06212d` (run: en=1) | bit 15 of `PHY(0x42)` = en, inside `wlc_phyreg_enter/exit` | acphy-init H13 |
| 2 | `wlc_phy_get_phyversion` (0x0b18cc) | `bool (pi, u16 *phytype, u16 *phyrev, u16 *radioid, u16 *radiorev)` | `wlc_bmac_attach` | fills the four values, returns 1 | acphy-attach 12 |
| 3 | `wlc_phy_get_encore` (0x0b18fb) | `u8 (pi)` | `wlc_bmac_attach` | returns `pi+0x178` (0) | acphy-attach 12 |
| 4 | `wlc_phy_get_coreflags` (0x0b1907) | `u32 (pi)` | `wlc_bmac_attach` | returns `pi+0x170` (0) | acphy-attach 12 |
| 5 | `wlc_phy_clk_bwbits` (0x0b1913) | `u32 (pi)`; pi may be NULL (result 0) | `wlc_bmac_bw_reset`, `wlc_bmac_phy_reset` | 0x40/0x80/0xc0 for bw 20/40/80 | acphy-chanspec 3 |
| 6 | `wlc_phy_por_inform` (0x0b1969) | `void (pi)` | `wlc_bmac_hw_up` (run) | `pi+0x185` = 1 | P1 |
| 7 | `wlc_phy_btclock_war` (0x0b1976) | `void (pi, bool en)` | `wlc_bmac_init` | `pi+0xfa1` = en | P1 |
| 8 | `wlc_phy_preamble_override_set` (0x0b1983) | `void (pi, s8 val)` | `sub_1679da` | stores only val = 0 (non-zero ignored on the AC-PHY) | P1 |
| 9 | `wlc_phy_preamble_override_get` (0x0b199e) | `s8 (pi)` | `wlc_bmac_state_get` (run) | returns `pi+0x22a` | P1 |
| 10 | `wlc_phy_initcal_enable` (0x0b19aa) | `void (pi, bool en)` | `wlc_bmac_set_chanspec` (run) | no-op (N-PHY only) | acphy-cal-tx 9 |
| 11 | `wlc_phy_hw_clk_state_upd` (0x0b19c7) | `void (pi, bool clk)`; pi may be NULL | `wlc_bmac_corereset`, `wlc_bmac_down_finish`, `wlc_bmac_xtal`, `wlc_coredisable` (run) | `sh+0x31` = clk | P1 |
| 12 | `wlc_phy_hw_state_upd` (0x0b19df) | `void (pi, bool up)`; pi may be NULL | `wlc_bmac_down_finish`, `wlc_bmac_up_finish` (run) | `sh+0x30` = up | P1 |
| 13 | `wlc_phy_cal_init` (0x0b19f7) | `void (pi)` | `wlc_bmac_init` (run) | one-time calibration init (slot 0x30) | acphy-cal-tx 8 |
| 14 | `wlc_phy_mute_upd` (0x0b1c2e) | `void (pi, bool mute, u32 flags)` | `wlc_bmac_mute`, `wlc_down` (run) | bit 4 of `pi+0x19c`; on unmute with flags bit 0: calibration age | P2 |
| 15 | `wlc_phy_bw_state_set` (0x0b1c7d) | `void (pi, u16 bw)` | `wlc_bmac_bw_set` (run) | `pi+0x182` = bw | acphy-chanspec 3 |
| 16 | `wlc_phy_chanspec_radio_set` (0x0b1c8a) | `void (pi, u16 chanspec)` | `wlc_bmac_init`, `wlc_bmac_set_chanspec` (run) | `pi+0x17e` = chanspec | acphy-chanspec 4 |
| 17 | `wlc_phy_chanspec_get` (0x0b1c97) | `u16 (pi)` | 28 functions (`wlc_BSSinit`, `wlc_join_BSS`, `wlc_prep_pdu`, `wlc_doiovar`, `wlc_bmac_set_chanspec`, ...) (run) | returns `pi+0x17e` | acphy-chanspec 4 |
| 18 | `wlc_phy_chanspec_ch14_widefilter_set` (0x0b1cc9) | `void (pi, bool wide)` | `sub_072656` (run: 0) | no-op (A/G PHY only) | P1 |
| 19 | `wlc_phy_chanspec_band_firstch` (0x0b1d3e) | `u16 (pi, u32 band)`; band 2 = 2.4 GHz, 1 = 5 GHz | `wlc_attach`, `wlc_default_chanspec` | first valid 20 MHz chanspec of the band, 0xff if none | P3 |
| 20 | `wlc_phy_txpower_get` (0x0b1e1d) | `int (pi, u32 *qdbm, bool *override)`; override may be NULL | `wlc_doiovar` | user target in quarter dBm; result 0 | P4 |
| 21 | `wlc_phy_txpower_get_target_min` (0x0b1ea7) | `u8 (pi)` | `wlc_update_txppr_offset` (run) | smallest per-core target of the active rx chains | P4 |
| 22 | `wlc_phy_txpower_get_target_max` (0x0b1ee4) | `u8 (pi)` | `wlc_update_txppr_offset` (run), `wlc_acphy_txctl1_calc`, `wlc_bcn_prb_body`, `wlc_check_expected_txbf_system_gain`, `wlc_tpc_rep_build` | largest per-core target of the active rx chains | P4 |
| 23 | `wlc_phy_txpwr_percent_set` (0x0b1f62) | `void (pi, u8 percent)` | `wlc_bmac_set_txpwr_percent` | `pi+0x184` = percent | P1 |
| 24 | `wlc_phy_machwcap_set` (0x0b1f6f) | `void (pi, u32 caps)` | `wlc_bmac_attach` (run) | `sh+0x2c` = caps | acphy-attach 12 |
| 25 | `wlc_phy_ofdm_rateset_war` (0x0b1f7c) | `void (pi, bool war)` | `sub_0411e9` | `pi+0x17d` = war | P1 |
| 26 | `wlc_phy_bf_preempt_enable` (0x0b1f89) | `void (pi, bool en)` | `sub_0411e9` | no-op (A/G PHY only) | P1 |
| 27 | `wlc_phy_txpower_hw_ctrl_get` (0x0b1fac) | `bool (pi)` | `wlc_stf_attach` | returns `pi+0xfa0` (hardware power control enabled) | P1 |
| 28 | `wlc_phy_antsel_type_set` (0x0b2018) | `void (pi, u8 type)` | `wlc_bmac_antsel_type_set` (run: 0) | `pi+0x180` = type; `pi+0x10fa` = (type is 4 or 5) | P1 |
| 29 | `wlc_phy_ant_rxdiv_get` (0x0b203f) | `bool (pi, u8 *val)` | `sub_0411e9` | `*val` = `sh+0x95`; returns 1 | P1 |
| 30 | `wlc_phy_BSSinit` (0x0b224a) | `void (pi, bool bonlyap, s8 rssi)` | `wlc_BSSinit` | resets the noise window and noise state | P2 |
| 31 | `wlc_phy_stf_chain_init` (0x0b24f7) | `void (pi, u8 txchain, u8 rxchain)` | `wlc_attach` (run: 3, 3) | `sh+0xa4..0xa7` | acphy-attach 12 |
| 32 | `wlc_phy_get_ofdm_rate_lookup` (0x0b2598) | `const u8 *(void)` | `wlc_recv_compute_rspec` | address of the table `ofdm_rate_lookup` | P1 |
| 33 | `wlc_phy_txpower_core_offset_set` (0x0b25f7) | `int (pi, s8 offsets[4])` | `sub_175ae6` | slot 0xd0 = `sub_099528` | acphy-attach "Function pointer table" |
| 34 | `wlc_phy_get_olpc_pwroffset` (0x0b2913) | `u8 (pi, u32 bw)` | `wlc_acphy_txctl1_calc` | `pi_ac+0x8de/0x8df/0x8e0` for bw 0x1000/0x1800/0x2000, else 0 | P1 |
| 35 | `wlc_phy_tssivisible_thresh` (0x0b2969) | `int (pi)` (an s8 sign-extended) | `sub_0837c5` (run), `wlc_acphy_txctl1_calc` | `wlc_phy_tssivisible_thresh_acphy(pi)` | acphy-txpower 16 |
| 36 | `wlc_phy_get_femctrl_bt_wlan_ovrd` (0x0b2985) | `u8 (pi)` | `wlc_bmac_iovars_dispatch` | `wlc_phy_get_femctrl_bt_wlan_ovrd_acphy(pi)` | P1 |
| 37 | `wlc_phy_set_femctrl_bt_wlan_ovrd` (0x0b299b) | `void (pi, s8 state)` | `wlc_bmac_set_btswitch` | `pi_ac+0x32e` = state; `wlc_phy_set_femctrl_bt_wlan_ovrd_acphy(pi, state)` | P1, acphy-rxgain 8 |
| 38 | `wlc_phy_down` (0x0b2a13) | `int (pi)` | `wlc_bmac_down_prep` (run) | stops the calibration timer; result 1 if it could not be stopped | P2 |
| 39 | `wlc_phy_block_bbpll_change` (0x0b2b65) | `void (pi, bool block, bool going_down)` | `sub_0450f0`, `wlc_down`, `wlc_up` | spur mode re-apply | acphy-chanspec 12 |
| 40 | `wlc_phy_shared_detach` (0x0b2cb4) | `void (sh)`; sh may be NULL | `wlc_bmac_detach` | frees `sh` (0x100 bytes) | P1 |
| 41 | `wlc_phy_antsel_init` (0x0b3351) | `void (pi, bool lut_init)` | `wlc_bmac_init` | no-op (N-PHY only) | P1 |
| 42 | `wlc_phy_txpower_ipa_ison` (0x0b5316) | `bool (pi)` | `wlc_attach` (run) | returns 0 (N-PHY only) | P1 |
| 43 | `wlc_phy_tkip_rifs_war` (0x0b532c) | `void (pi, u8 rifs)` | `wlc_doiovar`, `wlc_init` (run) | no-op (N-PHY only) | P1 |
| 44 | `wlc_phy_ldpc_override_set` (0x0b5344) | `void (pi, bool ldpc)` | `sub_0396ff` (run: 1) | `wlc_phy_update_rxldpc_acphy(pi, ldpc)` | acphy-init H12 |
| 45 | `wlc_phy_stf_chain_active_get` (0x0b54e0) | `u8 (pi)` | `wlc_stf_tempsense_upd` | temperature throttle, returns the active chain bitmap `pi+0xc33` | P2 |
| 46 | `wlc_phy_stf_chain_set` (0x0b5677) | `void (pi, u8 txchain, u8 rxchain)` | `sub_045018`, `wlc_stf_rxchain_set`, `wlc_stf_txchain_set_complete` | `sh+0xa6` = txchain; `wlc_phy_rxcore_setstate_acphy(pi, rxchain)` | P2 |
| 47 | `wlc_phy_cal_perical` (0x0b57d3) | `void (pi, u8 reason)` | `wlc_full_phy_cal`, `wlc_init` | calibration scheduler | acphy-cal-tx 3 |
| 48 | `wlc_phy_chanspec_set` (0x0b5d48) | `void (pi, u16 chanspec)` | `wlc_bmac_set_chanspec` (run) | channel change | acphy-chanspec 4 |
| 49 | `wlc_phy_clear_tssi` (0x0b5de2) | `void (pi)` | `sub_16e03b` | no-op (types 4, 7, 11) | P1 |
| 50 | `wlc_phy_swdiv_ant_get` (0x0b5edc) | `bool (pi, u8 bit, bool *on)` | `sub_03067b`, `sub_030702` | `*on` = bit of `D11(0x49c)`; returns 1 | P2 |
| 51 | `wlc_phy_swdiv_ant_set` (0x0b5f86) | `bool (pi, u8 bit, bool on)` | `wlc_swdiv_ant_set` | sets/clears a bit of `D11(0x49c)`; returns 1 | P2 |
| 52 | `wlc_phy_chanspec_band_validch` (0x0b70fa) | `void (pi, u32 band, chanvec_t *out)` | `sub_072656` (run), `wlc_11d_scan_complete`, `wlc_get_channels_in_country`, `wlc_get_country_list`, `wlc_scan` | bitmap of the channels of the band (28 bytes) | P3 |
| 53 | `wlc_phy_shared_attach` (0x0b7199) | `sh (shared_phy_params_t *p)` | `wlc_bmac_attach` (run) | allocates `sh` | acphy-attach 1 |
| 54 | `wlc_phy_interference_set` (0x0b78c4) | `void (pi, bool init)` | `wlc_join_BSS`, `wlc_set_home_chanspec` | no-op on the BCM4360 (PHY types 4, 7 or chip 0xa886 only) | P2 |
| 55 | `wlc_phy_cap_get` (0x0b799d) | `u32 (pi)` | 9 functions (`wlc_attach`, `wlc_stf_attach`, `wlc_stf_ss_update`, `wlc_doiovar`, ...) (run) | `wlc_phy_ac_caps(pi)` | acphy-attach 12 |
| 56 | `wlc_phy_set_deaf` (0x0b7dab) | `void (pi, bool user_flag)`; user_flag is not used | `wlc_bmac_set_deaf` | `wlc_phy_deaf_acphy(pi, 1)` | acphy-init H9 |
| 57 | `wlc_phy_acimode_noisemode_reset` (0x0b8104) | `void (pi, u32 channel, bool clear_aci, bool clear_noise, bool disassoc)` | `wlc_disassociate_client`, `wlc_join_BSS`, `wlc_set_home_chanspec` | no-op for PHY revisions 0..2 | P2 |
| 58 | `wlc_phy_interf_rssi_update` (0x0b816c) | `void (pi, u16 chanspec, s8 rssi)` | `sub_0450f0` | `wlc_phy_desense_aci_upd_chan_stats_acphy(pi, chanspec, rssi)` | acphy-desense E4 |
| 59 | `wlc_phy_ant_rxdiv_set` (0x0b81c2) | `void (pi, u8 val)` | `sub_0411e9` | stores `sh+0x95`; AC-PHY part: see P2 | P2 |
| 60 | `wlc_phy_txpower_sromlimit` (0x0b8be8) | `void (pi, u16 chanspec, u8 *min_pwr, ppr_t *max_pwr, u8 core)`; both pointers may be NULL | `sub_0837c5` (run), `sub_06ef19`, `wlc_sendassocreq`, `wlc_write_vht_transmit_power_envelope_ie` | `*min_pwr` = 4 x `pi+0xc37`; `wlc_phy_txpower_sromlimit_get_acphy` | P4 |
| 61 | `wlc_phy_txpower_limit_set` (0x0b94de) | `void (pi, ppr_t *txpwr, u16 chanspec)`; chanspec is not used | `wlc_bmac_set_chanspec` (run), `sub_072656`, `wlc_channel_set_txpower_limit`, `wlc_channel_update_txpwr_limit` | regulatory limits -> new targets | P4 |
| 62 | `wlc_phy_txpower_set` (0x0b9525) | `int (pi, u32 qdbm, bool override, ppr_t *reg_pwr)`; override is not used | `wlc_doiovar` | user target -> new targets; result 0 or 5 | P4 |
| 63 | `wlc_phy_rssi_compute` (0x0b97cb) | `void (pi, d11rxhdr *rxh)` (see P5) | `wlc_bmac_recv` (per received frame) | RSSI of a frame | P5 |
| 64 | `wlc_phy_noise_avg` (0x0b9e23) | `s8 (pi)` | `sub_03ceb2`, `sub_0411e9` | average noise in dBm | P6 |
| 65 | `wlc_phy_ioctl` (0x0b9f79) | `int (pi, int cmd, int len, void *arg, bool *ta_ok)`; the fifth argument is passed but not read | `sub_0411e9` (the MAC's ioctl handler, default case) | 6 ioctls | P7.1 |
| 66 | `wlc_phy_set_filt_war` (0x0ba2ea) | `void (pi, bool war)` | `wlc_bmac_filter_war_upd` (run: 0) | no-op (HT-PHY only) | P1 |
| 67 | `wlc_phy_switch_radio` (0x0ba302) | `void (pi, bool on)` | `wlc_bmac_init`, `wlc_bmac_radio_hw`, `wlc_bmac_set_chanspec`, `wlc_coredisable` (run) | radio on/off | acphy-radio 3, 4 |
| 68 | `wlc_phy_hold_upd` (0x0ba576) | `void (pi, u32 id, bool set)` | `wlc_scan_down` (run), `sub_16c584`, `sub_16e31e`, `wlc_assoc_change_state`, `wlc_scan`, `wlc_scan_terminate`, `wlc_sta_assoc_upd` | bits of `pi+0x19c` | P2 |
| 69 | `wlc_phy_anacore` (0x0baad1) | `void (pi, bool on)` | `wlc_bmac_phy_reset`, `wlc_bmac_radio_hw`, `wlc_coredisable` (run) | `D11(0x3e6)` | acphy-radio 1 |
| 70 | `wlc_phy_init` (0x0babf5) | `void (pi, u16 chanspec)` | `sub_06656c` (`wlc_bmac_bsinit`), `wlc_bmac_bw_set` (run) | full PHY init | acphy-init 1 |
| 71 | `wlc_phy_detach` (0x0baed7) | `void (pi)`; pi may be NULL | `wlc_bmac_detach` | reference count, frees `pi` | acphy-attach 13 |
| 72 | `wlc_phy_noise_sample_intr` (0x0bb5bc) | `void (pi)` | `wlc_dpc` (on the noise interrupt of the microcode) | reads the noise result | P6 |
| 73 | `wlc_phy_noise_sample_request_external` (0x0bb8e8) | `void (pi)` | `wlc_lq_noise_sample_request` | starts a noise measurement | P6 |
| 74 | `wlc_phy_watchdog` (0x0bb8ff) | `void (pi)` | `wlc_bmac_watchdog` (run, once a second) | periodic work | P8, acphy-cal-tx 11 |
| 75 | `wlc_phy_freqtrack_end` (0x0bc7f2) | `void (pi)` | `sub_0450f0`, `wlc_freqtrack_reset` | no-op (A-PHY only) | P1 |
| 76 | `wlc_phy_freqtrack_start` (0x0bc840) | `void (pi)` | `sub_039dd4`, `sub_0450f0`, `wlc_set_chanspec`, `wlc_up` | no-op (A-PHY only) | P1 |
| 77 | `wlc_phy_get_est_pout` (0x0bc87a) | `void (pi, u8 *est_pout, u8 *est_pout_adj, u8 *est_pout_cck)` | `wlc_get_last_txpwr` | estimated output power per core | P4 |
| 78 | `wlc_phy_attach` (0x0be426) | `pi (sh, void *regs, int bandtype, char *vars)` | `wlc_bmac_attach` (run, twice) | creates `pi` | acphy-attach 2 |
| 79 | `wlc_phy_iovar_dispatch` (0x0c1a4e) | `int (pi, u32 actionid, u16 type, void *params, u32 plen, void *arg, int alen, int vsize)` | `wlc_bmac_phy_iovar_dispatch` | PHY iovars | P7 |
| 80 | `wlc_phy_iovar_detach` (0x0e1800) | `void (wlc_pub_t *pub)` | `wlc_detach` | `wlc_module_unregister(pub, "phy", pub->wlc)` | P1 |
| 81 | `wlc_phy_iovar_attach` (0x0e1815) | `int (wlc_pub_t *pub)`; result 0 or -1 (the caller ignores it) | `wlc_attach` (run) | registers the iovar table | P1 |
| 82 | `wlc_phy_get_tx_power_offset` (0x152d9e) | `int (void)`; arguments ignored | `sub_031322` | returns 0 | P1 |
| 83 | `wlc_phy_get_tx_power_offset_by_mcs` (0x152da6) | `int (void)`; arguments ignored | `sub_031322` | returns 0 | P1 |

Notes on the table:

* `wlc_phy_txpower_limit_set` (61) and `wlc_phy_txpower_set` (62) receive
  more arguments than they use: the chanspec (61) and `override` (62) are
  never read (dis).
* `wlc_phy_set_deaf` (56) always passes 1 to the AC function, whatever the
  caller passed.
* Results the decompiler hides: `wlc_phy_txpower_get_target_min/max` return
  the byte in `al` (dis); `wlc_phy_down` returns 0 or 1 in `eax` (dis);
  `wlc_phy_noise_avg` returns a signed byte (its callers sign-extend `al`).
* Error codes are the negative `BCME_*` numbers of the open header
  `src\include\bcmutils.h`: -2 `BCME_BADARG`, -3 `BCME_BADOPTION`, -5
  `BCME_NOTDOWN`, -11 `BCME_NOCLK`, -23 `BCME_UNSUPPORTED`, -29 `BCME_RANGE`.
  `wlc_phy_txpower_set` returns the *positive* number 5 for a target above
  127 (dis: `mov eax, 5`).

### D2. The channel table `chan_info_all` (.rodata+0x2ce840)

67 entries of 4 bytes: channel number (u16), centre frequency in MHz (u16).
Used by `wlc_phy_chanspec_band_firstch` and `wlc_phy_chanspec_band_validch`
(and by the radio's frequency look-ups, other specifications). In order:

| Entries | Channels (frequency MHz) |
|---|---|
| 0..13 | 1..13 (2412 + 5 (ch - 1)), 14 (2484) |
| 14..17 | 34 (5170), 38 (5190), 42 (5210), 46 (5230) |
| 18..29 | 36 (5180), 40, 44, 48, 52, 54 (5270), 56, 60, 62 (5310), 64 (5320) |
| 30..49 | 100 (5500), 102 (5510), 104, 108, 110 (5550), 112, 116, 118 (5590), 120, 124, 126 (5630), 128, 132 (5660), **134 (5660)**, 136 (5680), 140 (5700), 144 (5720), 149 (5745), 151 (5755), 153 (5765) |
| 50..53 | 157 (5785), 159 (5795), 161 (5805), 165 (5825) |
| 54..66 | 184 (4920), 185 (4925), 187 (4935), 188 (4940), 189 (4945), 192 (4960), 196 (4980), 200 (5000), 204 (5020), 207 (5035), 208 (5040), 209 (5045), 210 (5050), 212 (5060), 216 (5080) |

(5 GHz channels whose frequency is not given follow 5000 + 5 ch.) The entry
of channel 134 carries 5660 MHz, the frequency of channel 132 (a slip in the
table; the channel-list functions use only the channel numbers).

### D3. Hold flags `pi+0x19c` (32 bit)

Set and cleared by `wlc_phy_hold_upd(pi, id, set)` (any bits) and
`wlc_phy_mute_upd` (bit 4). Meaning of the bits from their users (names (bcm)
where brcmsmac has them; the MAC passes the constants):

| Bit (mask) | Name | Effect inside the PHY |
|---|---|---|
| 0 (0x001) | `PHY_HOLD_FOR_ASSOC` (bcm) | see the watchdog (P8) |
| 1 (0x002) | `PHY_HOLD_FOR_SCAN` (bcm) | passed to `wlapi_update_bt_chanspec` (first flag); while set `wlc_phy_txpower_set` stores the new target but does not recompute; releasing it (`wlc_phy_hold_upd(pi, 2, 0)`) restores the cached calibration results |
| 2 (0x004) | `PHY_HOLD_FOR_RM` (bcm) | passed to `wlapi_update_bt_chanspec` (second flag) |
| 3 (0x008) | `PHY_HOLD_FOR_PLT` (bcm) | in mask 0x21e |
| 4 (0x010) | `PHY_HOLD_FOR_MUTE` (bcm) | set while the MAC is muted (`wlc_phy_mute_upd`); blocks `wlc_phy_cals_acphy` |
| 5 (0x020) | `PHY_HOLD_FOR_NOT_ASSOC` (bcm) | blocks a periodic AC calibration (acphy-cal-tx); set by `wlc_phy_init` |
| 9 (0x200) | (name unknown) | in masks 0x206 and 0x21e |

Masks tested by other functions: 0x206 (channel function and `sub_0909fd`,
acphy-chanspec: no interference state is looked up while any of these is
set), 0x21e (calibration timer callback, acphy-cal-tx). Which bit the MAC
passes for which situation is the MAC's business; the constants above are
the brcmsmac names and fit the callers' names (`wlc_scan` and `wlc_scan_down`
pass 2, run: `wlc_scan_down` calls `wlc_phy_hold_upd(pi, 2, 0)`).

### D4. `ofdm_rate_lookup` (.rodata+0x2cdbe0, 8 bytes)

`60 30 18 0c 6c 48 24 12`: rates in units of 500 kbit/s (48, 24, 12, 6, 54,
36, 18, 9 Mbit/s). Index = low 3 bits of the rate field of an OFDM PLCP
header as the hardware stores it (codes 0x8..0xf: 0x8 = 48 Mbit/s ... 0xf = 9
Mbit/s; this reading of the index is the brcmsmac one (bcm)). The PHY itself
does not use the table; it only hands out its address.

## Procedures

### P1. Trivial entry points (fields, stubs and no-ops)

All of these were read in the disassembly (dis). None of them accesses the
hardware.

**Field stores and reads** (no other effect):

| Entry point | Effect |
|---|---|
| `wlc_phy_por_inform(pi)` | `pi+0x185` = 1 (`phy_init_por`: the next `wlc_phy_init` loads the static tables again, acphy-init). Called by `wlc_bmac_hw_up` after a power-on reset of the core. |
| `wlc_phy_btclock_war(pi, en)` | `pi+0xfa1` = en (byte). No function of the AC-PHY path reads `pi+0xfa1` (unverified: found no reader in wlc_phy_cmn.c/wlc_phy_ac.c by text search). |
| `wlc_phy_preamble_override_set(pi, v)` | if v = 0 or the PHY type is not 11: `pi+0x22a` = v. So on the AC-PHY only "no override" (0) can be stored; any other value is ignored. |
| `wlc_phy_preamble_override_get(pi)` | returns the byte `pi+0x22a` (0 on the AC-PHY). |
| `wlc_phy_hw_clk_state_upd(pi, clk)` | if pi != NULL and `pi+0x20` (sh) != NULL: `sh+0x31` = clk. The MAC calls it with 1 when the core clock is on (registers may be accessed) and 0 when it goes off. |
| `wlc_phy_hw_state_upd(pi, up)` | if pi != NULL and sh != NULL: `sh+0x30` = up (1 = driver up). |
| `wlc_phy_txpwr_percent_set(pi, p)` | `pi+0x184` = p (byte; 100 at attach). No recalculation is started: the value is used by the next `sub_0b8ca4` (P4.3 step 6.6: every target is scaled by p/100 when p < 100). |
| `wlc_phy_ofdm_rateset_war(pi, war)` | `pi+0x17d` = war. |
| `wlc_phy_txpower_hw_ctrl_get(pi)` | PHY types 11 and 7: returns `pi+0xfa0` (1 = hardware power control on); type 4: `pi+0x221`; others `pi+0x220`. |
| `wlc_phy_antsel_type_set(pi, t)` | `pi+0x180` = t; `pi+0x10fa` = 1 if t is 4 or 5, else 0. |
| `wlc_phy_ant_rxdiv_get(pi, *val)` | `*val` = `sh+0x95`; returns 1. |
| `wlc_phy_get_olpc_pwroffset(pi, bw)` | PHY type 11: bw 0x1000 -> `pi_ac+0x8de`, 0x1800 -> `pi_ac+0x8df`, 0x2000 -> `pi_ac+0x8e0` (each 1 after attach); any other bw or PHY type -> 0. |
| `wlc_phy_get_femctrl_bt_wlan_ovrd(pi)` | PHY type 11: returns `wlc_phy_get_femctrl_bt_wlan_ovrd_acphy(pi)` (u8); else 0xff. |
| `wlc_phy_set_femctrl_bt_wlan_ovrd(pi, s)` | PHY type 11: `pi_ac+0x32e` = s; `wlc_phy_set_femctrl_bt_wlan_ovrd_acphy(pi, s)` with s sign-extended to int (acphy-rxgain section 8). Else nothing. |
| `wlc_phy_tssivisible_thresh(pi)` | PHY type 11: `wlc_phy_tssivisible_thresh_acphy(pi)` sign-extended (acphy-txpower 16); else -128. |
| `wlc_phy_txpower_core_offset_set(pi, offs)` | calls slot `pi+0xd0` (`sub_099528`, acphy-attach "Function pointer table") with (pi, offs) and returns its result; -23 if the slot is empty. |
| `wlc_phy_ldpc_override_set(pi, ldpc)` | type 7: the HT function; type 11: `wlc_phy_update_rxldpc_acphy(pi, ldpc)` (acphy-init H12). |
| `wlc_phy_get_ofdm_rate_lookup()` | returns the address of the 8-byte table `ofdm_rate_lookup` (D11 rate codes of the OFDM rates in the order of the PLCP rate field; see D4). |
| `wlc_phy_shared_detach(sh)` | if sh != NULL: `osl_mfree(sh+0x10 (osh), sh, 0x100)`. |
| `wlc_phy_iovar_attach(pub)` | `wlc_module_register(pub, phy_iovars, "phy", pub->wlc (first field of pub), sub_0e1857, NULL, NULL, NULL)`; returns 0 if that returned 0, else -1. `wlc_attach` ignores the result. |
| `wlc_phy_iovar_detach(pub)` | `wlc_module_unregister(pub, "phy", pub->wlc)`. |
| `wlc_phy_get_tx_power_offset`, `wlc_phy_get_tx_power_offset_by_mcs` (wlc_phy_ssn.c) | return 0 without reading their arguments. |

**No-ops on the AC-PHY** (the function tests the PHY type and does nothing
for type 11; what it does for other types in one line):

| Entry point | Other PHY types |
|---|---|
| `wlc_phy_initcal_enable(pi, en)` | type 4: `pi_ac+0x381` = en |
| `wlc_phy_chanspec_ch14_widefilter_set(pi, w)` | types 0, 2: `pi_ac+0x4c5` = w |
| `wlc_phy_bf_preempt_enable(pi, en)` | types 0, 2: `pi_ac+0x459` = en |
| `wlc_phy_antsel_init(pi, lut)` | type 4: `wlc_phy_antsel_init_nphy` |
| `wlc_phy_txpower_ipa_ison(pi)` | type 4: `wlc_phy_n_txpower_ipa_ison`; type 11 returns 0 |
| `wlc_phy_tkip_rifs_war(pi, r)` | type 4: `wlc_phy_nphy_tkip_rifs_war` |
| `wlc_phy_set_filt_war(pi, w)` | type 7: `wlc_phy_set_filt_war_htphy` |
| `wlc_phy_freqtrack_start(pi)`, `wlc_phy_freqtrack_end(pi)` | type 2: write/restore `PHY(0x18)`, `PHY(0x38)` |
| `wlc_phy_clear_tssi(pi)` | types other than 4, 7, 11: SHM words 0x68, 0x6a (type 0) or 0x58, 0x5a, 0x70, 0x72 = 0x7f7f |
| `wlc_phy_ant_rxdiv_set(pi, v)` | stores `sh+0x95` = v for all types, then returns at once for types 7 and 11; other types program antenna diversity (host flags, PHY registers) |


### P2. State and control entry points

#### P2.1 wlc_phy_mute_upd (.text+0x0b1c2e, name original)

Inputs: `pi`, `mute` (bool), `flags` (u32). No result, no hardware access
(dis).

1. If mute != 0: set bit 4 (0x10) of `pi+0x19c`. Done.
2. Else clear bit 4 of `pi+0x19c`; if bit 0 of flags is set
   (`PHY_MUTE_FOR_PREISM` (bcm)): `cal+0xc0` (time of the last calibration;
   `cal` = the block at `pi+0xf58`, acphy-cal-tx "Data") = `sh+0x34` -
   `sh+0x7c` if `sh+0x34` > `sh+0x7c`, else 0 (unsigned compare). This makes
   the next watchdog consider the periodic calibration due.

Run: `wlc_bmac_mute`/`wlc_down` call it with (1, 8) and (0, 8) in the
standard session.

#### P2.2 wlc_phy_hold_upd (.text+0x0ba576, name original)

Inputs: `pi`, `id` (u32 mask), `set` (bool). No result (dis).

1. If set: `pi+0x19c` |= id; else `pi+0x19c` &= ~id.
2. If bit 1 (0x2, scan) is in id: on the AC-PHY, only when set = 0:
   `sub_0ba54c(pi, 0)` -> `wlc_phy_scanroam_cache_cal_acphy(pi, 0)`
   (acphy-cal-tx 20: restore the cached calibration results if the cache
   holds any (`pi_ac+0x44a` = 0xacdc); suspends the MAC around it). When set
   != 0 nothing more happens on the AC-PHY. (Other PHY types: `sub_0ba54c(pi,
   set)` in both cases; type 7 has its own cache function.)

Run: `wlc_scan_down` calls `wlc_phy_hold_upd(pi, 2, 0)` twice in the standard
session; the cache is empty there, so the restore only suspends and enables
the MAC (no PHY access) (trace: see Verification).

`sub_0ba54c` (.text+0x0ba54c, 42 bytes; name assigned:
`wlc_phy_scanroam_cache_cal`): type 7 -> `wlc_phy_scanroam_cache_cal_htphy(pi,
save)`, type 11 -> `wlc_phy_scanroam_cache_cal_acphy(pi, save)`.

#### P2.3 wlc_phy_BSSinit (.text+0x0b224a, name original)

Inputs: `pi`, `bonlyap` (bool, not used), `noise` (s8: the noise value to
start with, in dBm). Called by `wlc_BSSinit` when a BSS is started or joined.
No result, no hardware access (dis).

1. `sh+0x96 + i` = noise for i = 0..7 (the shared noise ring, read by
   `wlc_phy_noise_avg`, P6).
2. (PHY types 8 and 10 only: the eight bytes = 0xa4 instead.)
3. `sh+0xa0` = 0 (32 bit; index of the next entry of the ring).
4. If `sh+0x80` (interference mode) = 3 and bit 0 of `pi+0xc0c` is set:
   `pi+0xc00` = `sh+0x34` + 8. (Mode 3 is not a mode of the AC-PHY: its modes
   are bit masks of 0..7 set by the ioctl, P7; with mode 3 = bits 0 and 1 this
   step would run if `pi+0xc0c` bit 0 were set - only the N-PHY interference
   code sets that bit (unverified).)
5. `pi+0xed4 + 2i` = -92 and `pi+0xef4 + 2i` = -92 (s16) for i = 0..15 (two
   rings of per-core noise values, P6).
6. `pi+0xf54` = 0 (index into the rings of step 5).

#### P2.4 wlc_phy_down (.text+0x0b2a13, name original)

Input: `pi`. Result: int (dis), the number of timer callbacks that may still
run (0 or 1).

1. r = 0. If the calibration timer `pi+0x1088` exists:
   `wlapi_del_timer(sh+0x20, pi+0x1088)`; if that returns 0 (the timer could
   not be cancelled) r = 1.
2. (PHY type 4: clears two N-PHY fields.)
3. PHY type 11: `pi_ac+0x338` = 0 (spur mode in use) and `pi+0x1164` = 0
   (spur mode wanted), so the next channel change sets the spur mode up again
   (acphy-chanspec 11, 12).
4. Return r.

#### P2.5 wlc_phy_stf_chain_set (.text+0x0b5677, name original)

Inputs: `pi`, `txchain` (u8), `rxchain` (u8). No result.

1. `sh+0xa6` = txchain.
2. PHY type 11: `wlc_phy_rxcore_setstate_acphy(pi, rxchain)` (acphy-init H7:
   stores `sh+0xa7`, and if the clock is on suspends the MAC and programs the
   core registers). (Types 4 and 7: their own functions.)

So the transmit chain mask is only stored here; it reaches the hardware with
the next `wlc_phy_rxcore_setstate_acphy` (H7 writes `sh+0xa6` to bits 0..2 of
`PHY(0x401)` as part of step 2).

#### P2.6 wlc_phy_stf_chain_active_get (.text+0x0b54e0, name original)

Input: `pi`. Result: u8, the active chain bitmap `pi+0xc33` (low nibble:
transmit chains, high nibble: receive chains) (dis).

1. If `pi+0x18c` (`watchdog_override` (bcm), 1 after attach) is 0: return
   `pi+0xc33`.
2. PHY type 11: `wlc_phy_stf_chain_temp_throttle_acphy(pi)` (acphy-cal-rx:
   measures the temperature with the MAC suspended and switches transmit
   chains off/on by rewriting `pi+0xc33`); its own result is ignored.
3. Return `pi+0xc33`.

(Other PHY types measure with `wlc_phy_tempsense_nphy` here; not described.)
Called by `wlc_stf_tempsense_upd` (the MAC's periodic temperature check).

#### P2.7 wlc_phy_swdiv_ant_get, wlc_phy_swdiv_ant_set (.text+0x0b5edc, 0x0b5f86, names original)

Software antenna diversity: one bit per antenna/core in a 16 bit register.
Inputs: `pi`, `bit` (u8: bit number), and `*on` (get, bool pointer) or `on`
(set, bool). Both return 1 (dis).

* For chips 0xa886 and 0xa887 the register is `PHY(0x44d)` (accessed with
  the MAC suspended if it was running). **Not the BCM4360.**
* Otherwise (BCM4360): `D11(0x49c)`, 16 bit.
  * get: `*on` = 1 if bit `bit` of `D11(0x49c)` is set, else 0.
  * set: `D11(0x49c)` = read `D11(0x49c)` with bit `bit` set (on != 0) or
    cleared (on = 0); a plain 16 bit read and write, the MAC is not
    suspended.

Purpose of `D11(0x49c)` unknown (a register of the MAC core's IHR space;
presumably an antenna-select output of the microcode's switch control) (unverified).

#### P2.8 wlc_phy_interference_set (.text+0x0b78c4, name original)

Inputs: `pi`, `init` (bool). Acts only if the PHY type is 7 or 4, or the chip
id (`sh+0x3c`) is 0xa886: **no-op on the BCM4360.** (For those: chooses the
interference mode of the band of `pi+0x17e` from `sh+0x84/0x88` or, when
`sh+0x94` = 1, from `sh+0x8c/0x90` (5 GHz values above 1 count as 0); if
`pi+0x240` differs from the channel of `pi+0x17e`: suspend the MAC,
`sub_0b740d(pi, mode, init)`, `sh+0x80` = mode, enable the MAC.)

On the BCM4360 the interference mode is applied by `wlc_phy_init` and the
channel function (copy of `sh+0x84/0x88` or `sh+0x8c/0x90` into `sh+0x80`,
acphy-init, acphy-chanspec) and by `wlc_phy_ioctl` (P7).

#### P2.9 wlc_phy_acimode_noisemode_reset (.text+0x0b8104, name original)

Inputs: `pi`, `channel` (u32), `clear_aci` (bool), `clear_noise` (bool),
`disassoc` (bool). Acts only if (PHY revision > 2 or PHY type 7) and `sh+0x94`
!= 1 and (disassoc != 0 or channel != `pi+0x240`): **no-op for the BCM4360's
PHY revisions 0 and 1.** (When it acts: revision < 3 and type 7 -> the HT
function; revision >= 3 -> `wlc_phy_aci_noise_reset_nphy`, for every PHY type,
an AC-PHY of revision >= 3 included.)

#### P2.10 wlc_phy_interf_rssi_update (.text+0x0b816c, name original)

Inputs: `pi`, `chanspec` (u16), `rssi` (s8). (N-PHY revision > 5 and HT:
store the RSSI in two fields if the channel of `pi+0x240` matches.) PHY type
11: `wlc_phy_desense_aci_upd_chan_stats_acphy(pi, chanspec (zero-extended),
rssi (sign-extended))` (acphy-desense E4). No other effect (dis).

#### P2.11 wlc_phy_set_deaf (.text+0x0b7dab, name original)

Inputs: `pi`, `user_flag` (not read). PHY type 11:
`wlc_phy_deaf_acphy(pi, 1)` (acphy-init H9). The argument 1 is a constant for
every PHY type (dis). The counterpart `wlc_phy_clear_deaf` (.text+0x0b7cfe)
exists but has no caller in the object, so the MAC can make the PHY deaf but
never calls the PHY to undo it (the AC-PHY leaves the deaf state on its own
when the next init or channel change reprograms the registers - unverified).

### P3. Channel lists

#### P3.1 wlc_phy_chanspec_band_firstch (.text+0x0b1d3e, name original)

Inputs: `pi`, `band` (u32: 2 = 2.4 GHz, 1 = 5 GHz). Result: a chanspec in
the low 16 bits of `eax` (the upper 16 bits are not cleared: callers must use
only the low half) (dis). No access.

For each entry of `chan_info_all` (D2) in table order, with c = its channel
number, on the AC-PHY (the 40 MHz variant applies to PHY types 4, 6, 7 only):

1. cs = c | 0x1000 (20 MHz), plus 0xc000 (5 GHz band) if c >= 15.
2. Skip the entry if 149 <= c <= 165 and `pi+0xfac` != 0 (`pi+0xfac` is set
   only by the A/G PHY's SROM code: always 0 on the BCM4360).
3. If (c < 15 and band = 2) or (c >= 15 and band = 1): return cs.

If no entry matches: return 0xff. Results on the BCM4360: band 2 -> 0x1001
(channel 1), band 1 -> 0xd022 (channel 34, the first 5 GHz entry of the
table, which comes before 36) (read, not run).

#### P3.2 wlc_phy_chanspec_band_validch (.text+0x0b70fa, name original)

Inputs: `pi`, `band` (u32: 2 = 2.4 GHz, 1 = 5 GHz), `out` (pointer to a
28-byte bit vector, `chanvec_t`). No result, no access (dis).

1. Clear the 28 bytes of `out`.
2. For each entry of `chan_info_all` with channel c:
   * skip it if 149 <= c <= 165 and `pi+0xfac` != 0 (never on the BCM4360);
   * skip it if c = 144 and the PHY type is not 11 (so the AC-PHY lists 144);
   * otherwise, if (band = 2 and c < 15) or (band = 1 and c > 14): set bit
     (c & 7) of byte `out[c >> 3]`.

Result on the BCM4360: band 2 -> channels 1..14; band 1 -> every 5 GHz
channel of the table (34, 38, 42, 46, 36..64, 100..165 as listed, 184..216).
The regulatory filtering is done by the MAC afterwards. Run: `sub_072656`
calls it for both bands in the standard session.

### P4. Transmit power at the API level

#### P4.0 Units, objects and the `ppr_*` services

All powers are signed 8 bit numbers in **quarter dBm** (4 = 1 dBm; 127 =
"no limit"). The per-rate container `ppr_t` stays in `wlc_ppr.c` (outside the
PHY); the PHY touches it only through these calls (semantics read in the
`wlc_ppr.c` functions, given here so that the PHY's use can be understood;
they are services, not part of the PHY):

| Service | Semantics as the PHY relies on it |
|---|---|
| `ppr_create(osh, bw)` -> ppr | new object for bandwidth bw (0 = 20 MHz: 182 values, 1 = 40 MHz: 364, 2 = 80 MHz: 546), every value -128 (`WL_RATE_DISABLED`); NULL if out of memory |
| `ppr_delete(osh, p)` | free |
| `ppr_get_ch_bw(p)` | its bandwidth number |
| `ppr_clear(p)` | every value = -128 |
| `ppr_copy_struct(src, dst)` | dst = src (same bandwidth: whole; different: the 20 MHz sub-blocks both have) |
| `ppr_set_cmn_val(p, v)` | every value = v (disabled ones too) |
| `ppr_compare_min(a, b)` | a[i] = min(a[i], b[i]) (only if a and b have the same bandwidth, else nothing) |
| `ppr_compare_max(a, b)` | a[i] = max(a[i], b[i]) (same condition) |
| `ppr_minus_cmn_val(p, v)` | each value x other than -128: x = x - v if x > v, else 0 |
| `ppr_cmn_val_minus(p, v)` | each value x other than -128: x = v - x |
| `ppr_plus_cmn_val(p, v)` | each value x other than -128: x = x + v |
| `ppr_multiply_percentage(p, pct)` | each value x other than -128: x = (x * pct) / 100 (truncating) |
| `ppr_apply_max(p, v)` | every value: x = min(x, v) |
| `ppr_apply_min(p, v)` | every value: x = max(x, v) |
| `ppr_force_disabled(p, t)` | disables rates below t: a group of rates (4 DSSS, 8 OFDM, or the MCS 0..7 of a group of 10) is disabled as a whole if any of its first 8 values is below t; MCS 8 and 9 of a group individually |
| `ppr_get_max(p)` -> s8 | largest value (disabled ones included) |
| `ppr_get_min(p, floor)` -> s8 | smallest value that is neither -128 nor equal to floor |
| `ppr_set_dsss/ofdm/ht_mcs/vht_mcs(p, bw-index, ..., values)` | store one rate group (used by `wlc_phy_txpwr_apply_srom11`, P4.7) |

The PHY keeps one ppr object of its own, `pi+0x1c8` (`tx_power_offset`):
for every rate the amount (>= 0) by which the rate must stay below the
highest per-core target; `sub_09949f` hands it to the MAC
(`wlapi_high_update_txppr_offset`, acphy-txpower 17), which puts it into the
rate table of the shared memory.

Fields written by this level (all per core c = 0..N-1, N = `pi+0x168`):

| Field | Content |
|---|---|
| `pi+0x1d6` | user target (u8, quarter dBm, 127 = none); attach: 127 |
| `pi+0x216 + c` | highest target of core c over all rates (s8) - what `sub_09949f`/`sub_098949` program as the power control target |
| `pi+0x21a + c` | lowest enabled target of core c (s8) |
| `pi+0x22f` | copy of the last `pi+0x21a + c` written (the last core's) |
| `pi+0x234` | 0 (flag "`pi+0x22f` is negative", set only by the dead `wlc_phy_neg_txpower_set`) |
| `pi+0x21e` | 0 (cleared at core 0; purpose unknown) |
| `pi+0x232` | override flag reported by `wlc_phy_txpower_get`; `wlc_phy_txpower_set` clears it |

#### P4.1 wlc_phy_txpower_limit_set (.text+0x0b94de, name original)

Inputs: `pi`, `reg` (ppr object with the regulatory limits per rate, made by
the MAC for the current chanspec; quarter dBm), chanspec (16 bit, **not
read**). No result. Steps (dis; run: up-trace, every channel change):

1. `wlapi_suspend_mac_and_wait(sh+0x20)`.
2. `sub_0b8ca4(pi, reg, NULL)` (P4.3), which ends with the AC recalculation
   `sub_09949f` (the 669..673 hardware accesses seen per call in the
   standard session are all made there, acphy-txpower 17).
3. `sub_0b72a4(pi)`: nothing for the AC-PHY (P4.8).
4. `wlapi_enable_mac(sh+0x20)`.

Note: neither the "driver up" flag `sh+0x30` nor the scan hold bit is
tested here (unlike `wlc_phy_txpower_set`); the MAC calls this from
`wlc_bmac_set_chanspec` after every channel change.

#### P4.2 wlc_phy_txpower_set (.text+0x0b9525, name original)

Inputs: `pi`, `qdbm` (u32, quarter dBm), `override` (bool, **not read**),
`reg` (ppr object of regulatory limits, may be NULL). Result: 5 if qdbm >
127 (nothing changed), else 0 (dis).

1. If qdbm > 127 (unsigned): return 5.
2. `pi+0x1d6` = qdbm; `pi+0x232` = 0.
3. If `sh+0x30` (up) is 0 or bit 1 (scan hold) of `pi+0x19c` is set: return 0
   (the target is used by the next computation).
4. w = bit 0 of `D11(0x120)` (32 bit read: MAC enabled). If w = 1:
   `wlapi_suspend_mac_and_wait(sh+0x20)`.
5. en = `pi+0xfa0`; `wlc_phy_txpwrctrl_enable_acphy(pi, 0)`.
6. `sub_0b8ca4(pi, reg, NULL)`; `sub_0b72a4(pi)`.
7. `wlc_phy_txpwrctrl_enable_acphy(pi, en)`.
8. If w = 1: `wlapi_enable_mac(sh+0x20)`. Return 0.

(Type 6 skips steps 4, 5, 7, 8; type 4 uses its own enable function.) Caller:
`wlc_doiovar` (the `qtxpower` iovar of the MAC layer, presumably; it passes
the regulatory object of the current channel - unverified).

Danger for an implementer: if `reg` is NULL, the regulatory object inside
`sub_0b8ca4` stays all -128, so every rate ends up disabled (P4.3 step 6.3).

#### P4.3 sub_0b8ca4 (.text+0x0b8ca4, 2106 bytes, name assigned: `wlc_phy_txpower_recalc_target`)

Purpose: combine the user target, the board limits (SROM) and the
regulatory limits into the per-core targets and the per-rate offsets, then
let the AC-PHY apply them. Inputs: `pi`, `reg` (ppr, may be NULL), `out`
(ppr or NULL; non-NULL only from `wlc_phy_txpower_get_current`, which has no
caller: on the BCM4360 always NULL). No result. No hardware access of its
own. Callers: `wlc_phy_txpower_limit_set`, `wlc_phy_txpower_set` (and the
dead `wlc_phy_txpower_get_current`).

bw below = 2 if `pi+0x17e & 0x3800` = 0x2000 (80 MHz), 1 if 0x1800 (40 MHz),
else 0. osh = `sh+0x10`. Steps (dis; order of the service calls verified in
the emulator, see Verification):

1. (Board types 0x58b, 0x539 on 5 GHz set a flag used in step 6.12; not the
   BCM4360's board 0x117.)
2. R = `ppr_create(osh, bw)`; if NULL: return.
3. If reg != NULL: `ppr_copy_struct(reg, R)`. (PHY type 4: further
   processing of R; not described.)
4. ctl = `wf_chspec_ctlchan(pi+0x17e)` (used only by PHY type 6).
5. T = `ppr_create(osh, bw)`; if NULL: `ppr_delete(osh, R)`, return.
   S = `ppr_create(osh, bw)`; if NULL: delete R and T, return.
   If `pi+0x1c8` != NULL and `ppr_get_ch_bw(pi+0x1c8)` != bw:
   `ppr_delete(osh, pi+0x1c8)`, `pi+0x1c8` = NULL. If `pi+0x1c8` = NULL:
   `pi+0x1c8` = `ppr_create(osh, bw)`; if that fails: delete R, T, S and
   return (pi+0x1c8 stays NULL). Then `ppr_clear(pi+0x1c8)`.
   (Then a per-channel correction g is looked up for the LCN PHYs: 2.4 GHz:
   `pi+0x1128 + channel - 1` (`2g_cga`); 5 GHz: `pi+0x1110 + i` (`5g_cga`) for
   the first i with frequency <= the i-th of 24 thresholds at
   .rodata+0x2ceb90. g is used only by PHY types 8 and 10: no effect here.)
6. For each core c = 0 .. N-1 (N = `pi+0x168`):
   1. `ppr_set_cmn_val(T, (s8) pi+0x1d6)`: every rate = the user target.
   2. `wlc_phy_txpower_sromlimit(pi, pi+0x17e, &m, S, c)` (P4.4): m = the
      lowest allowed power (4 x `pi+0xc37` = 4, i.e. 1 dBm); S = the board
      limit of every rate for core c. S is not cleared between the cores:
      values that the board-limit function does not write keep those of
      the previous core.
   3. `ppr_compare_min(S, R)`: S = min(board, regulatory) per rate.
   4. `ppr_minus_cmn_val(S, (s8) pi+0x10a0)`: subtract `txpwrbckof` (6 =
      1.5 dB by default; a value that would go to or below 0 becomes 0).
   5. `ppr_compare_min(T, S)`: T = min(user target, S).
   6. If `pi+0x184` (`txpwr_percent`) < 100: `ppr_multiply_percentage(T,
      pi+0x184)`.
   7. PHY type 11: `ppr_force_disabled(T, m)` (rates below 1 dBm are
      disabled; other types raise them to m with `ppr_apply_min`).
   8. mx = `ppr_get_max(T)`; mn = `ppr_get_min(T, m)` (for PHY types 2, 7
      and 11 the floor is m, else -128).
   9. `pi+0x216 + c` = mx; `pi+0x21a + c` = mn; `pi+0x22f` = mn; `pi+0x234`
      = 0.
   10. If c = 0: `pi+0x21e` = 0, and if out != NULL: `ppr_copy_struct(T,
       out)`.
   11. PHY type 11 (and types 4, 7): `ppr_cmn_val_minus(T, mx)`: T becomes
       the offset of each rate below the core's highest target. (Other types
       with `pi+0x220` set subtract mn instead.)
   12. `ppr_compare_max(pi+0x1c8, T)`: keep, per rate, the largest offset over
       the cores.
7. `ppr_delete(osh, R)`, `ppr_delete(osh, T)`, `ppr_delete(osh, S)`.
8. If out != NULL: return. Else if slot `pi+0x40` is set: call it with `pi`
   (AC: `sub_09949f`, acphy-txpower 17).

Example (run, synthetic SROM, 2.4 GHz channel 1, 20 MHz, both cores):
regulatory limits 76 (CCK, OFDM), 64 (MCS 0..7 one stream), 52 (other
groups); board limits 76 with OFDM 48/54 Mbit/s at 72/68 and VHT MCS 6..9 at
72/68/64/60; after the backoff of 6 and the minimum: targets 70 (CCK, OFDM up
to 36 Mbit/s), 66, 62 (48, 54 Mbit/s), 58 (MCS 0..7, 1 stream), 46 (others);
`pi+0x216` = 70, 70; `pi+0x21a` = 46, 46; offsets in `pi+0x1c8`: 0, 4, 8, 12,
24.

#### P4.4 wlc_phy_txpower_sromlimit (.text+0x0b8be8, name original)

Inputs: `pi`, chanspec (u16), `minp` (pointer to s8, may be NULL), `maxp`
(ppr object, may be NULL), core (u8). No result, no access (dis).

1. If minp != NULL: `*minp` = `pi+0xc37` << 2 (`min_txpower` 1 -> 4).
2. If maxp != NULL: PHY type 11:
   `wlc_phy_txpower_sromlimit_get_acphy(pi, chanspec, maxp, core)`
   (acphy-txpower 19). (Other types have their own functions; an unknown type
   sets every rate of maxp to 127.)

The MAC calls it too (`sub_0837c5` before every channel change with core 0;
`wlc_sendassocreq`, `wlc_write_vht_transmit_power_envelope_ie`,
`sub_06ef19`) to learn the board limits.

#### P4.5 wlc_phy_txpower_get, wlc_phy_txpower_get_target_min, wlc_phy_txpower_get_target_max

`wlc_phy_txpower_get(pi, *qdbm, *override)` (.text+0x0b1e1d): `*qdbm` =
`pi+0x1d6` (zero-extended). (If `pi+0x230` is set - only the LCN PHY's
recalculation sets it - `*qdbm` = (s8) `pi+0x22f` instead, as `-v |
0x40000000` (`WL_TXPWR_NEG`) when `pi+0x234` is set.) If override != NULL:
`*override` = `pi+0x232` (0 on the BCM4360). Result 0.

`wlc_phy_txpower_get_target_max(pi)` (.text+0x0b1ee4): the largest
`pi+0x216 + c` (compared unsigned) over the cores c < N whose bit is set in
`sh+0xa7` (the receive chain mask!), starting from 0. Result u8.

`wlc_phy_txpower_get_target_min(pi)` (.text+0x0b1ea7): the smallest
`pi+0x21a + c` (unsigned compare) over the same cores, starting from 0x7f.
Result u8.

Both are called by the MAC's `wlc_update_txppr_offset` while `sub_09949f`
hands over the offsets (run: max 70, min 46 in the example above).

#### P4.8 sub_0b72a4 (.text+0x0b72a4, name assigned: `wlc_phy_cal_txpower_recalc_sw`)

Input: `pi`. For PHY types 4, 6, 7, 8, 10 and 11: returns 0 at once (no
effect). (Type 5: LP-PHY power update; types 0, 2 without hardware power
control: the A/G software recalculation; others: `pi+0x238` = `SHM(0x32)`
when `pi+0x236` = -1.) Called after `sub_0b8ca4` by
`wlc_phy_txpower_limit_set` and `wlc_phy_txpower_set`.

#### P4.7 wlc_phy_txpwr_apply_srom11 (.text+0x0b44d7, 2562 bytes, name original) and its helpers

Purpose: fill a ppr object with the board's power limit of every rate on a
channel, from the per-rate offsets of the SROM (revision 11) that
`wlc_phy_txpwr_srom11_read` stored in `pi+0xc4c..0xd3b` (acphy-attach, "Tx
power offsets"). Inputs: `pi`, g (u8: band group, 0 = 2.4 GHz, 1/2/3 = 5 GHz
low/mid/high, as computed by `wlc_phy_txpower_sromlimit_get_acphy`),
chanspec (u16), m (s8: the board maximum, quarter dBm), `out` (ppr object of
the chanspec's bandwidth). No result, no hardware access. Returns at once if
the PHY type is not 11. Only caller: `wlc_phy_txpower_sromlimit_get_acphy`
(acphy-txpower 19). **Verified exactly in the emulator** (1104 random
settings of all the fields, all band groups, bandwidths, sidebands and both
values of the boardflags3 bit: the sequence of `ppr_set_*` calls and all
values agree with the description below; harness `srom11chk.py`).

Notation: n_k(x) = bits 4k..4k+3 of x; s(n) = n for n < 8, n - 16 for n >= 8
(signed nibble). All arithmetic is 8 bit signed with wrap-around. An SROM
nibble counts half dB, so a rate's limit is m - 2 n. "bf" = `pi_ac+0x34d`
(boardflags3 bit 11; 0 in the emulator, value of the real board unknown).

Helpers (all in wlc_phy_cmn.c, names assigned):

| Helper | Effect |
|---|---|
| `sub_0b2716(po, lr, m, o[8])` (OFDM from a 32 bit word) | o = (A, A, B, B, m-2n_1, m-2n_2, m-2n_3, m-2n_4) with A = m - 2 n_0(po), B = A - 2 s(lr); order of the OFDM rates 6, 9, 12, 18, 24, 36, 48, 54 Mbit/s |
| `sub_0b2765(po, lr, m, v[10])` (VHT MCS 0..9) | v = (A, B, B, m-2n_1, ..., m-2n_7) with A = m - 2 n_0(po), B = A - 2 s(lr) |
| `sub_0b2690(i[8], u, m, o[8])` | o[k] = i[k] + 2 s(n_0(u)) for k = 0..5, i[k] + 2 s(n_1(u)) for k = 6, 7 (m unused; may work in place) |
| `sub_0b26d3(i[10], u, m, o[10])` | o[k] = i[k] + 2 s(n_0(u)) for k = 0..4, i[k] + 2 s(n_1(u)) for k = 5..9 |
| `sub_0b3369(out, bwi, o[8], pi)` | `ppr_set_ofdm(out, bwi, 0, 1, o)`; if N > 1: for c = 2..N: `ppr_set_ofdm(out, bwi, 2, c, o)`, `ppr_set_ofdm(out, bwi, 3, c, o)` |
| `sub_0b42df(out, bwi, v[10], pi)` | `ppr_set_vht_mcs(out, bwi, 1, 0, 1, v)`; if N > 1: `(bwi, 1, 2, 2)`, `(bwi, 2, 1, 2)`, `(bwi, 2, 0, 2)`, `(bwi, 1, 3, 2)`, then with v[8] and v[9] set to -128 for the call (and restored after it) `(bwi, 2, 3, 2)`; N > 2 adds three-chain groups (not this board) |

Arguments of the ppr setters (ppr's convention): bwi 0 = 20 MHz, 1 = 40, 2 =
80, 3 = 20 in 40, 4 = 20 in 80, 5 = 40 in 80; `ppr_set_dsss(out, bwi, chains,
d[4])`; `ppr_set_ofdm(out, bwi, mode, chains, o[8])`; `ppr_set_vht_mcs(out,
bwi, nss, mode, chains, v[10])`; mode 0 = plain, 1 = STBC, 2 = CDD, 3 = TXBF
(names (bcm)).

**2.4 GHz** (chanspec & 0xc000 = 0):

1. O20[k] = m - 2 n_k(`pi+0xc50`), k = 0..7.
2. V20 = like `sub_0b2765` from `pi+0xc5c` (`mcsbw202gpo`) with lr =
   n_2(`pi+0xd14`) (bits 8..11 of `ofdmlrbw202gpo`). If bf: V20[9] -= 32 if
   bit 13 of `pi+0xd36` is set, V20[8] -= 32 if bit 12 is set (8 dB less for
   MCS 9/8).
3. 20 MHz (chanspec & 0x3800 = 0x1000): D[k] = m - 2 n_k(`pi+0xc4c`), k =
   0..3; `ppr_set_dsss(out, 0, c, D)` for c = 1..N; `sub_0b3369(out, 0, O20)`;
   `sub_0b42df(out, 0, V20)`. Done.
4. 40 MHz (0x1800): D40[k] = m - 2 n_k(`pi+0xc4e`); with L = n_3(`pi+0xd14`)
   (bits 12..15): V40 = like `sub_0b2765` from `pi+0xc64` (`mcsbw402gpo`) with
   lr = L (if bf: bits 15 and 14 of `pi+0xd36` for V40[9], V40[8]); O40 = like
   `sub_0b2716` from `pi+0xc64` with lr = L. u = n_0(`pi+0xd18`) << 4 |
   n_0(`pi+0xd16`); O20in40 = `sub_0b2690`(O20, u); V20in40 =
   `sub_0b26d3`(V20, u); Odup = `sub_0b2690`(O40, n_0(`pi+0xd1c`) << 4 |
   n_0(`pi+0xd1a`)). Calls in this order: `sub_0b3369(out, 1, Odup)`,
   `sub_0b42df(out, 1, V40)`, `ppr_set_dsss(out, 3, c, D40)` for c = 1..N,
   `sub_0b3369(out, 3, O20in40)`, `sub_0b42df(out, 3, V20in40)`.
5. Any other bandwidth: nothing is written.

**5 GHz** (chanspec & 0xc000 = 0xc000), j = g - 1 (0..2), lr = `pi+0xd1e +
2j` (`mcslr5g?po`), sb = `pi+0xd36 + 2j` (only with bf):

1. O20 = `sub_0b2716`(`pi+0xc6c + 4j`, n_0(lr)); V20 =
   `sub_0b2765`(`pi+0xcb4 + 4j`, n_0(lr)) (both words hold `mcsbw205g?po`);
   if bf: V20[9] -= 32 if bit 5 of sb, V20[8] -= 32 if bit 4.
2. 20 MHz: `sub_0b3369(out, 0, O20)`, `sub_0b42df(out, 0, V20)`. Done.
3. Otherwise V40 = `sub_0b2765`(`pi+0xcd8 + 4j`, n_1(lr)) (if bf: bits 7 and 6
   of sb); O40 = `sub_0b2716`(`pi+0xc90 + 4j`, n_1(lr)) (both words hold
   `mcsbw405g?po`). With t = 4g (4, 8, 12): dup = ((`pi+0xd1c` >> t) & 0xf)
   << 4 | ((`pi+0xd1a` >> t) & 0xf).
4. 40 MHz: u = ((`pi+0xd18` >> t) & 0xf) << 4 | ((`pi+0xd16` >> t) & 0xf).
   Calls: `sub_0b3369(out, 1, sub_0b2690(O40, dup))`, `sub_0b42df(out, 1,
   V40)`, `sub_0b3369(out, 3, sub_0b2690(O20, u))`, `sub_0b42df(out, 3,
   sub_0b26d3(V20, u))`. Done.
5. 80 MHz: V80 = `sub_0b2765`(`pi+0xcf0 + 4j`, n_2(lr)); O80 =
   `sub_0b2716`(`pi+0xca8 + 4j`, n_2(lr)) (both `mcsbw805g?po`); if bf: bits 9
   and 8 of sb for V80[9], V80[8]. lo = `pi+0xd24 + 2j`
   (`sb20in80and160lr5g?po`), hi = `pi+0xd2a + 2j` (`..hr5g?po`); u =
   n_0(hi) << 4 | n_0(lo); O20in80 = `sub_0b2690`(O20, u); V20in80 =
   `sub_0b26d3`(V20, u). If chanspec & 0x700 is 0 or 0x300 (control channel
   is the lowest or highest 20 MHz of the 80): u2 = ((hi >> 2) & 0xf) << 4 |
   ((lo >> 2) & 0xf) (bits 2..5, not a nibble boundary: dis checked) and
   both arrays are adjusted once more with u2. u3 = n_0(`pi+0xd36 + 2j`) << 4
   | n_0(`pi+0xd30 + 2j`); V40in80 = `sub_0b26d3`(V40, u3). Calls in this
   order: `sub_0b3369(out, 2, sub_0b2690(O80, dup))`, `sub_0b42df(out, 2,
   V80)`, `sub_0b3369(out, 4, O20in80)`, `sub_0b42df(out, 4, V20in80)`,
   `sub_0b42df(out, 5, V40in80)`, `sub_0b3369(out, 5, sub_0b2690(O40, dup))`.
6. Any other bandwidth: nothing.

No DSSS rates are written on 5 GHz, and none for 20-in-80. Rates the
function does not write keep their value in `out` (-128 for a fresh object).

#### P4.6 wlc_phy_get_est_pout (.text+0x0bc87a, name original)

Inputs: `pi`, `est` (u8 array, one per core), `est_adj` (u8 array, one per
core), `est_cck` (pointer to u8). No result. Caller: `wlc_get_last_txpwr`
(the MAC's report of the last transmit power). AC-PHY path (dis):

1. `*est_cck` = 0.
2. `wlapi_suspend_mac_and_wait(sh+0x20)`; `wlc_phyreg_enter(pi)`.
3. `wlc_phy_txpwr_est_pwr_acphy(pi, est, est_adj)` (acphy-txpower 12: per
   core reads `PHY(0x642 + 0x200 c)` and `PHY(0x640 + 0x200 c)`).
4. `wlc_phyreg_exit(pi)`; `wlapi_enable_mac(sh+0x20)`.

So `est[c]` gets the power estimate from `PHY(0x642 + 0x200 c)` and
`est_adj[c]` the one from `PHY(0x640 + 0x200 c)` (each 0 when its valid bit
is clear). (Types 4 and 7 have their own estimate functions; other types read
TSSI registers.)

### P5. wlc_phy_rssi_compute (.text+0x0b97cb, name original): RSSI of a received frame

Called by `wlc_bmac_recv` for every received frame with `pi` and a pointer to
the receive header at the start of the frame's buffer (the MAC stores the
32 bit TSF at header offset 0x18 just before the call). No result (the
caller ignores `rax`). No hardware access on the AC path (run).

Header bytes used on the BCM4360 path (offsets in bytes from the start of
the header; 16 bit fields little-endian):

| Offset | Size | Use |
|---|---|---|
| 0x06 | u16 | low byte loaded as a start value; replaced on the AC path |
| 0x09 | u8 | core 0 receive power (read by `wlc_phy_rssi_compute_acphy`) |
| 0x0a | u16 | low byte: core 1 power; high byte: core 2 power (same) |
| 0x12 | u16 | bit 8: the PHY receive status is valid |
| 0x1c | s8 | **output**: RSSI of the frame in dBm |
| 0x1f | u8 | **output**: 1 = no valid RSSI, 0 = valid (written by the AC function) |
| 0x20 + c | s8 | **output**: RSSI of core c (written by the AC function) |
| 0x24 | u8 | **output**: 0 (fraction in quarter dB, used by LCN PHYs only) |

Steps (MAC core revision `sh+0x28` = 42):

1. If the MAC revision is > 10 and bit 8 of the word at 0x12 is clear:
   header[0x1f] = 1, header[0x1c] = 0, header[0x24] = 0; return. (For MAC
   revision 35 a frame with bit 5 of that word set is also treated so; not
   this chip.)
2. PHY type 11 (radio 0x2069): r = `wlc_phy_rssi_compute_acphy(pi, header)`
   (acphy-cal-rx: per-core values from header 0x09/0x0a with temperature and
   sub-band corrections, stored at 0x20 + c, header[0x1f] = 0, combined over
   the receive chains of `sh+0xa7` by the mode `sh+0xa8`: 0 = maximum, 1 =
   minimum, 2 = average).
3. header[0x1c] = r (low byte); header[0x24] = 0.

(Radio 0x2050/0x2060, LP, SSN, LCN, N and HT PHYs have their own branches; the
N and HT ones also keep a running average in `pi+0x10b9..0x10da`. Not
reached.)

Run (synthetic headers, 2.4 GHz channel 1, defaults): word 0x12 = 0x0100,
core powers 0x20/0x30 -> header[0x1c] = 0x30, [0x20] = 0x20, [0x21] = 0x30,
[0x1f] = 0, [0x24] = 0; core powers 0xf0/0xe8 -> header[0x1c] = 0xf0 (-16);
word 0x12 = 0 -> header[0x1c] = 0, [0x1f] = 1, per-core bytes untouched.

### P6. Noise measurement: the protocol with the microcode

#### P6.0 Overview

The microcode measures the background noise power when the driver sets bit
4 (0x10) of `D11(0x124)` (MAC command register; `MCMD_BG_NOISE` (bcm)); it
writes one 32 bit power per core to `SHM(0x308 + 4c)` (low half) /
`SHM(0x30a + 4c)` (high half), c = 0..2, the channel it measured on to the
low byte of `SHM(0x8c)`, and raises bit 18 (0x40000, `MI_BG_NOISE` (bcm)) of
the MAC interrupt status; `wlc_dpc` then calls `wlc_phy_noise_sample_intr`.
(The model never sets these, so the session does not reach the interrupt;
the protocol below was run by calling the functions and filling the shared
memory by hand.)

State: `pi+0x18f` (`phynoise_state` (bcm)): bit 0 = request of the PHY's
watchdog (reason 1), bit 1 = request of the MAC (reason 2, external), bit 2
= request of the CRS minimum power calibration (reason 4). `pi+0x190` (u32) =
`sh+0x34` (time) of the last request; `pi+0x194` (u32) = channel of the last
reason-1 request. `pi_ac+0x41` = a reason-4 request arrived while another
measurement was pending (the calibration then runs with that measurement).
Results: the shared ring `sh+0x96 + i` (8 x s8, dBm; index `sh+0xa0`), the
per-core rings `pi+0xed4 + 2i` (core 0) and `pi+0xef4 + 2i` (core 1), 16 x
s16 each, index `pi+0xf54`, and `pi_ac+0x14 + c` (noise of core c + 67, the
input of the CRS minimum power calibration, acphy-desense C1).

#### P6.1 sub_0baff6 (.text+0x0baff6, 1478 bytes, name assigned: `wlc_phy_noise_sample_request`)

Inputs: `pi`, reason (u8: 1 watchdog, 2 external, 4 CRS calibration),
channel (u8). No result. Callers: `wlc_phy_noise_sample_request_external`
(reason 2), `wlc_phy_noise_sample_request_crsmincal` (reason 4, acphy-desense
C4), `wlc_phy_watchdog` (reason 1, P8), `wlc_phy_noise_sample_intr` (A/G
PHYs only). AC-PHY path (dis; run):

1. busy = (`pi+0x18f` != 0). `pi_ac+0x41` = 0.
2. If reason = 4: if busy: `pi_ac+0x41` = 1, return. Else `pi+0x18f` |= 4.
   Otherwise: if busy: return. Reason 2: `pi+0x18f` |= 2; reason 1:
   `pi+0x18f` |= 1 and `pi+0x194` = channel. (Any other reason sets no bit
   but continues.)
3. `pi+0x190` = `sh+0x34`.
4. If `pi+0xc21` != 0 (no writer of this byte was found in the object: 0):
   `wlc_phy_noise_cb(pi, channel, -95)` and return.
5. (A polled measurement with `wlc_phy_rx_iq_est_acphy` would run here for
   reasons other than 2 and 4 if `pi+0x198` were set; it is 0 for the
   AC-PHY.)
6. Write 0 to `SHM(0x308)`, `SHM(0x30a)`, `SHM(0x30c)`, `SHM(0x30e)`,
   `SHM(0x310)`, `SHM(0x312)` in this order (`wlapi_bmac_write_shm`).
7. `D11(0x124)` = read `D11(0x124)` | 0x10 (32 bit read, 32 bit write).

Run: reason 2 on channel 1 makes exactly these accesses (6 shared memory
writes, read 0x00000004 / write 0x00000014 of `D11(0x124)`), `pi+0x18f` = 2.

#### P6.2 wlc_phy_noise_sample_request_external (.text+0x0bb8e8, name original)

`sub_0baff6(pi, 2, low byte of pi+0x17e)`. Called by the MAC's
`wlc_lq_noise_sample_request`; the answer comes back through
`wlapi_noise_cb` (P6.5).

#### P6.3 wlc_phy_noise_sample_intr (.text+0x0bb5bc, name original)

Input: `pi`. No result. AC-PHY path (dis; run):

1. ch = low byte of `SHM(0x8c)` (`wlapi_bmac_read_shm`).
2. n = `wlc_phy_noise_read_shmem(pi)` (P6.4).
3. `wlc_phy_noise_cb(pi, ch, n)` (P6.5).

#### P6.4 wlc_phy_noise_read_shmem (.text+0x0b7ecc, name original) and sub_0b701e

Input: `pi`. Result: s8, the noise in dBm (the highest over the cores, at
least -92). Callers: `wlc_phy_noise_sample_intr`,
`wlc_phy_noise_sample_acphy` (from `wlc_phy_noise_avg`), and one MAC function
(`sub_11b2c9`).

1. For each core c < N: v = `SHM(0x30a + 4c)` << 16 | `SHM(0x308 + 4c)`
   (two 16 bit reads, low word first). If v = 0: d[c] = -92 and p[c] = 0;
   else p[c] = v >> 9. Sum the v.
2. If the sum is not 0: `sub_0b701e(pi, p, d, 0)` (overwrites d[c] of every
   core, also of those with v = 0), then, if the PHY type is 11 and
   (`pi+0x18f` = 4 exactly or `pi_ac+0x41` != 0):
   `wlc_phy_crs_min_pwr_cal_acphy(pi, 0)` (acphy-desense C1). (PHY revisions
   16 and > 17 first call an N-PHY correction; not here.)
3. n = -92; for each core c: s16 ring entry of core c at index `pi+0xf54`
   (`pi+0xed4 + 0x20c + 2 * pi+0xf54`) = d[c]; n = max(n, d[c]).
4. `pi+0xf54` = (`pi+0xf54` + 1) & 15.
5. `sh+0x96 + sh+0xa0` = n; `sh+0xa0` = (`sh+0xa0` + 1) & 7.
6. Return n.

`sub_0b701e(pi, p[], d[], adj)` (.text+0x0b701e, name assigned:
`wlc_phy_noise_calc_dbm`): reads `SHM(0x314)` (used only by non-AC PHYs of
revision >= 3); x[c] = `wlc_phy_compute_dB`(p[c]); for the AC-PHY: d[c] =
x[c] - 104 - adj and `pi_ac+0x14 + c` = x[c] - 37 (= d[c] + 67 + adj). (Other
PHY types: revision < 3: x - 99 - adj; else x - 33 - `SHM(0x314)`.) Both
callers pass adj = 0.

`wlc_phy_compute_dB(p[], x[], n)` (.text+0x0b21e1, name original): for each
of the n values: k = index of the highest set bit of p (0 for p = 0 or 1), b
= the bit below it (0 if k = 0); x = 3k + 2b (about 10 log10 p).

Run (channel 1, two cores): `SHM(0x308..0x30e)` = 0x0000, 0x0010, 0x8000,
0x0004 -> p = 2048, 576 -> x = 33, 27 -> d = -71, -77 dBm; result -71;
`pi_ac+0x14` = -4, `pi_ac+0x15` = -10; `pi+0xed4` = -71, `pi+0xef4` = -77,
`pi+0xf54` = 1; `sh+0x96` = -71, `sh+0xa0` = 1.

#### P6.5 wlc_phy_noise_cb (.text+0x0b805c, name original)

Inputs: `pi`, ch (u8), n (s8). No result. If `pi+0x18f` = 0 nothing happens.
Otherwise, in this order:

1. If bit 0: if `pi+0x194` = ch: `sh+0x96 + sh+0xa0` = n and `sh+0xa0` =
   `sh+0xa0` + 1 (0 after 7). Clear bit 0.
2. If bit 1: clear bit 1; `wlapi_noise_cb(sh+0x20, ch, n)` (-> the MAC's
   `wlc_lq_noise_cb`).
3. PHY type 11 and bit 2: clear bit 2.

So a watchdog measurement enters the shared ring twice (once in P6.4 step 5,
once here). Run: after the request of P6.2 and the interrupt,
`wlapi_noise_cb(shim, 1, -71)` was called and `pi+0x18f` went 2 -> 0.

#### P6.6 wlc_phy_noise_avg (.text+0x0b9e23, name original)

Input: `pi`. Result: s8 noise in dBm (returned sign-extended in `eax`).
Callers: `wlc_phy_ioctl` (`WLC_GET_PHY_NOISE`), the MAC's `sub_03ceb2` and
`sub_0411e9`. Steps for the AC-PHY (dis; run):

1. r = `wlc_ol_noise_avg_offload(wlapi_get_wlc_info(sh+0x20))` (MAC side,
   offload engine; 0 in the emulator); if r != 0 (low byte): return r.
2. n = `wlc_phy_noise_sample_acphy(pi)` (acphy-desense C3): if `sh+0x31` = 0
   it is -92 (0xa4) without any access; else it reads `SHM(0x8c)` (value
   unused), n = `wlc_phy_noise_read_shmem(pi)` (P6.4, a fresh conversion of
   the last result words), and stores n once more in the ring (`sh+0x96 +
   sh+0xa0`, index + 1, 0 after 7).
3. `sh+0x96 + i` = n for i = 0..7 (the whole ring).
4. Return the average of the non-zero ring entries (sum / count, signed,
   truncating; 0 if all are 0). After step 3 this is n itself (or 0 if n =
   0).

So on the AC-PHY the "average" is the last measurement of the microcode,
converted now. Run: with the shared memory of P6.4 the result is -71 and
all eight ring entries are -71.

### P7. ioctls and iovars

#### P7.1 wlc_phy_ioctl (.text+0x0b9f79, name original)

Inputs: `pi`, cmd (int), len (int), arg (pointer to the caller's buffer),
and a fifth argument (a `bool *`, passed by the caller, never read). Result:
int (`BCME_*`). Caller: the MAC's ioctl handler `sub_0411e9`, in the default
case of its switch, i.e. for every ioctl number the MAC does not handle
itself (run: 27, 135, 211, 212, 311 and 312 issued through `wlc_ioctl` all
arrive here). If len >= 4 and arg != NULL, v = the first 4 bytes of arg (as
int), else v = 0. Results are written to `*(u32 *)arg`.

| cmd | Name | Effect on the AC-PHY |
|---|---|---|
| 27 (0x1b) | `WLC_RESTART` | if `sh+0x30` (up) is 0: `sub_0b7337(pi)` (`wlc_set_phy_uninitted`, acphy-attach 5: marks the PHY for a complete new initialisation), result 0; else result -5 (`BCME_NOTDOWN`) (run) |
| 135 (0x87) | `WLC_GET_PHY_NOISE` | if `sh+0x31` (clock) is 0: -11; else `*arg` = `wlc_phy_noise_avg(pi)` sign-extended (P6.6), result 0 (run: -71). **Used in normal operation**: the open glue polls it (`wl_linux.c` statistics, `wl_iw.c` wireless statistics) |
| 211 (0xd3) | get interference mode (name not in the open headers) | `*arg` = `sh+0x80`, with bit 7 set if bit 0 of `pi+0xc0c` is set; result 0 (run: 7) |
| 212 (0xd4) | set interference mode | v < 0 or v > 7 (AC-PHY; 4 for others): -29. If v = `sh+0x80`: 0, nothing else. Else `sh+0x80` = v, then the common tail below |
| 311 (0x137) | set interference override | v < -1 or v > 7: -29. v = -1: `sh+0x94` = 0 and `sh+0x80` = `sh+0x84` (2.4 GHz) or `sh+0x88` (5 GHz, chanspec & 0xc000 != 0). Else `sh+0x94` = 1, `sh+0x8c` = `sh+0x90` = v, `sh+0x80` = v. Then the common tail |
| 312 (0x138) | get interference override | `*arg` = -1 if `sh+0x94` = 0, else `sh+0x80`; result 0 (run: -1, then 2) |
| other | | -23 (`BCME_UNSUPPORTED`) |

Common tail of 212 and 311: if `sh+0x30` (up) is 0: return 0 (the mode is
applied by the next init). Else `wlapi_suspend_mac_and_wait`; if v != 0:
`sub_0b740d(pi, 0, 1)` (reset of the mitigation); `sub_0b740d(pi, sh+0x80, 1)`
(acphy-init 8); `wlapi_enable_mac`; result -3 if `sub_0b740d` returned 0
(it always returns 1), else 0. Note: for 311 with v = -1 the test "v != 0"
is true, so the reset runs too (run: `sub_0b740d(pi, 0, 1)` then `(pi, 7,
1)`, 549 accesses).

(The type tests of cmd 311/312 for other PHY types - N-PHY revision >= 3,
HT, LCN on chip 0x4313 - do not matter here; 211..312 do nothing more for
the AC-PHY.)

#### P7.2 wlc_phy_iovar_dispatch (.text+0x0c1a4e, name original) and the table `phy_iovars`

Path: the iovar module "phy" registered by `wlc_phy_iovar_attach` (P1) has
the handler `sub_0e1857` (.text+0x0e1857; name assigned
`wlc_phy_doiovar`), which calls `wlc_bmac_phy_iovar_dispatch(wlc->wlc_hw
(wlc+0x20), actionid, vi->type (u16 at offset 0xc of the table entry),
params, plen, arg, alen, vsize)`, which calls `wlc_phy_iovar_dispatch(pi of
the current band, same arguments)`. actionid = 2 x varid for a get, 2 x
varid + 1 for a set (`IOV_GVAL`/`IOV_SVAL` of `bcmutils.h`). v below = the
first 4 bytes of params (0 if plen < 4); results go to `*(u32 *)arg` unless
said otherwise. Only the 32 names of `phy_iovars` (.rodata+0x2cf960, 16
bytes per entry: name pointer, varid, flags, type, minimum length) can
reach the dispatch; the other action ids it tests (varids 0x4e, 0x15e
(temperature threshold), 0x172, 0x174) are unreachable in this build.
Flags (`IOVF_*` of `wlc_pub.h`): 0x0400 = get only when up, 0x0040 = set
only when up, 0x0008 = manufacturing test.

None of these iovars is used by the open glue or by the object's upper
layers in normal operation (scan, association, data); all are debug or
manufacturing tools. One line each, AC-PHY behaviour:

| Name (varid, type) | Get | Set |
|---|---|---|
| `lpphy_tempsense` (0x22, int32) | calls the LP-PHY function `wlc_phy_tempsense_lpphy(pi)` regardless of the PHY type | -23 |
| `lpphy_cal_delta_temp` (0x0a, int32) | (s8) `pi_ac+0x1a` (an LP-PHY field; on the AC-PHY a byte of the CRS calibration table) | `pi_ac+0x1a` = v |
| `lpphy_vbatsense` (0x2c, int32) | `wlc_phy_vbatsense_lpphy(pi)` regardless of the PHY type | -23 |
| `lpphy_rx_gain_temp_adj_tempsense` (0x20, int8) | (s8) `pi_ac+0x1d0` | `pi_ac+0x1d0` = v |
| `lpphy_rx_gain_temp_adj_thresh` (0x21, uint32) | bytes `pi_ac+0x1d1..0x1d3` packed (s8 each) | stores the three bytes, then `wlc_phy_rx_gain_temp_adj_lpphy(pi)` |
| `lpphy_rx_gain_temp_adj_metric` (0x1f, int16) | `pi_ac+0x1d4`, bit 0 of `pi_ac+0x1d5` as bit 8 | stores them, then `wlc_phy_rx_gain_temp_adj_lpphy(pi)` |
| `phy_muted` (0x143, bool) | bit 4 of `pi+0x19c` (P2.1) | -23 |
| `phy_glitchthrsh` (0x149, uint8) | -23 | `pi+0x1136` = v (16 bit; no AC reader known) |
| `phy_noise_up` (0x14a, uint8) | -23 | `pi+0x1138` = v (16 bit) |
| `phy_noise_dwn` (0x14b, uint8) | -23 | `pi+0x113a` = v (16 bit) |
| `phy_percal_delay` (0x152, uint16) | `pi+0xf8a` (delay between calibration phases, ms) | 1..2000: `pi+0xf8a` = v, else -29 |
| `phy_rxiqest` (0x13d, uint32) | if up: received power estimate per core with the settings below (`sub_0c0f88`, with the MAC suspended and the Bluetooth critical section held; not described), else -4 | packed settings: byte 0 = 0, 1 or 3 (`pi+0xf69`), byte 1 = 10..15 (`pi+0xf68`, log2 of the samples), byte 2 bits 0..3 < 2 (`pi+0xf6a`), bits 4..5 < 2 and bits 6..7 != 3 (`pi+0xf6b`, `pi+0xf6c`), byte 3 bits 0..3 < 3 (`pi+0xf6d`), bits 4..7 (`pi+0xf83`); else -29 |
| `edcrs` (0x1a1, uint8) | 0 if `D11(0x6c6)` = 0x000f, 1 if 0x0f0f (16 bit read), else nothing written | `D11(0x6c6)` = 0x0f0f (v != 0) or 0x000f (16 bit write) |
| `lp_mode` (0x1a2, uint8) | `pi_ac+0x8e7` | 1..3: `wlc_phy_lp_mode(pi, v)`, else nothing |
| `lp_vco_2g` (0x1a4, uint8) | `pi_ac+0x8e5` | 0, 1: `wlc_phy_force_lpvco_2G(pi, v)`, else nothing |
| `radio_pd` (0x1a3, uint8) | -23 | -23 |
| `phynoise_srom` (0x183, uint32) | if up: the per-core SROM noise levels (`wlc_phy_get_SROMnoiselvl_phy`), core 0 in the lowest byte; else -4 | -23 |
| `num_stream` (0x13f, int32) | -1 for the AC-PHY (2 N, 3 HT, 1 A/G/LP/SSN/LCN); vsize bytes copied | -23 |
| `band_range`, `subband_idx` (0x140, int8) | `wlc_phy_chanspec_bandrange_get(pi, pi+0x17e)` (acphy-chanspec 7); vsize bytes copied | -23 |
| `subband5gver` (0x185, int8) | -23 | -23 |
| `min_txpower` (0x142, uint32) | `pi+0xc37` | `pi+0xc37` = v (8 bit; the lowest allowed power in dBm, P4.4) |
| `phy_oclscdenable` (0x28, uint8) | -11 if the clock is off; else reads `PHY(0x245)` and returns -23 | same as get |
| `lnldo2` (0x19b, uint8) | -23 | -23 |
| `phy_rxantsel` (0x29, bool) | nothing written, 0 (N-PHY revision >= 7 only) | 0, nothing |
| `phy_crs_war` (0x188, int8) | -23 (LCN40 only) | -23 |
| `noise_measure_disable` (0x175, int32) | 0 | 0, nothing (LCN only) |
| `pavars2` (0x19e, buffer) | -23 | -23 |
| `phy_btc_restage_rxgain` (0x19f, uint32) | `pi_ac+0x8b0` | v > 6: -29; any of bits 0x206 of `pi+0x19c` set: -25; if v equals `pi_ac+0x8b0`: 0; else suspend, `wlc_phy_desense_btcoex_acphy(pi, v)` (acphy-desense), enable |
| `phy_ed_thresh` (0x1a0, int32) | `wlc_phy_ed_thres_acphy(pi, arg, 0)` (acphy-desense C5) | `wlc_phy_ed_thres_acphy(pi, &v, 1)` |
| `phy_sromtempsense` (0x17b, int16) | (s16) `pi+0x210` | `pi+0x210` = v (16 bit) |
| `phy_afeoverride` (0x1af, uint8) | `pi+0x1169` | `pi+0x1169` = v (8 bit) |

### P9. The shim `wlc_phy_shim.c` (`wlapi_*`): the services the PHY needs from the MAC

`wlc_phy_shim_attach(wlc_hw, wl, wlc)` (.text+0x152b8b, called by
`wlc_bmac_attach`; result stored at `wlc_hw+0xd8` and handed to the PHY as
`sh+0x20`) allocates 0x18 bytes: +0x00 `wlc_hw`, +0x08 `wlc`, +0x10 `wl` (the
Linux glue's handle). `wlc_phy_shim_detach(shim)` (.text+0x152b6c, by
`wlc_bmac_detach`) frees them (with `wlc_hw->osh`). Every `wlapi_*` function
takes the shim as first argument, replaces it by one of the three handles
and passes the other arguments through unchanged (narrowed as shown) (dis).
An open PHY must provide these with the same meaning; on the BCM4360 path
only the rows marked "AC" are called.

| Shim function (.text) | Prototype (PHY's view) | Calls | Used on the AC path by |
|---|---|---|---|
| `wlapi_suspend_mac_and_wait` (0x152ab0) | `void (shim)` | `wlc_bmac_suspend_mac_and_wait(wlc_hw)` | AC: everywhere (bmac-init) |
| `wlapi_enable_mac` (0x152a90) | `void (shim)` | `wlc_bmac_enable_mac(wlc_hw)` | AC: everywhere |
| `wlapi_bmac_read_shm` (0x152ae4) | `u16 (shim, u32 offset)` | `wlc_bmac_read_shm(wlc_hw, offset)` | AC: noise (P6), init, watchdog, `sub_092500` |
| `wlapi_bmac_write_shm` (0x152af2) | `void (shim, u32 offset, u16 v)` | `wlc_bmac_write_shm(wlc_hw, offset, v)` | AC: noise request, `wlc_phy_chanspec_shm_set`, high RSSI bypass, `sub_0925cc` |
| `wlapi_bmac_mhf` (0x152acc) | `void (shim, u8 idx, u16 mask, u16 val, int bands)` | `wlc_bmac_mhf(wlc_hw, idx, mask, val, bands)` | AC: `sub_0a04c2` (init), `wlc_phy_btcx_wlan_critical_enter/exit` (`phy_rxiqest`) |
| `wlapi_bmac_phyclk_fgc` (0x152a35) | `void (shim, bool clk)` | `wlc_bmac_phyclk_fgc(wlc_hw, clk)` | AC: `wlc_phy_resetcca_acphy`, `sub_0abc76` |
| `wlapi_bmac_ucode_wake_override_phyreg_set` (0x1529f0) | `void (shim)` | `wlc_ucode_wake_override_set(wlc_hw, 2)` | AC: `wlc_phyreg_enter` |
| `wlapi_bmac_ucode_wake_override_phyreg_clear` (0x1529dd) | `void (shim)` | `wlc_ucode_wake_override_clear(wlc_hw, 2)` | AC: `wlc_phyreg_exit` |
| `wlapi_bmac_bw_set` (0x152a63) | `void (shim, u16 bw)` | `wlc_bmac_bw_set(wlc_hw, bw)` | AC: channel function, `wlc_phy_init` |
| `wlapi_bmac_corereset` (0x152abe) | `void (shim, u32 flags)` | `wlc_bmac_corereset(wlc_hw, flags)` | AC: `wlc_phy_attach` |
| `wlapi_switch_macfreq` (0x152a9e) | `void (shim, u8 mode)` | `wlc_bmac_switch_macfreq(wlc_hw, mode)` | AC: `wlc_phy_setup_spurmode` (acphy-chanspec 12) |
| `wlapi_bmac_btc_mode_get` (0x152a47) | `int (shim)` | `wlc_bmac_btc_mode_get(wlc_hw)` | AC: `wlc_phy_watchdog` |
| `wlapi_bmac_btc_period_get` (0x1528c6) | `void (shim, u16 *period, u8 *active)` | no call: `*period` = (u16) of the 32 bit word at `btc+0x1c`, `*active` = byte `btc+0x16`, btc = `*(wlc_hw+0xb0)` (the MAC's Bluetooth coexistence state) | AC: `wlc_phy_watchdog` |
| `wlapi_update_bt_chanspec` (0x152956) | `void (shim, u16 chanspec, bool scan, bool roam)` | `wlc_bmac_update_bt_chanspec(wlc_hw, chanspec, scan, roam)` | AC: `wlc_phy_chanspec_set` (with bits 1 and 2 of `pi+0x19c`), `wlc_phy_switch_radio`, `wlc_phy_watchdog` |
| `wlapi_is_eci_coex_enabled` (0x15290c) | `bool (shim)` | no call: 0 if MAC revision (`wlc_hw+0x84`) < 15, or bit 29 of the ChipCommon capabilities (`sih+0x18`) is clear, or bit 0 of `sih+0x1c` is set; else bit 29 of the MAC capabilities `wlc_hw+0xa4` | AC: `wlc_phy_switch_radio`, `wlc_btcx_override_enable` |
| `wlapi_high_update_txppr_offset` (0x15296d) | `void (shim, ppr_t *offsets)` | `wlc_update_txppr_offset(wlc, offsets)` | AC: `sub_09949f` (P4) |
| `wlapi_high_update_phy_mode` (0x1529ae) | `void (shim, u32 mode)` | `wlc_update_phy_mode(wlc, mode)` | AC: `wlc_phy_aci_updsts_acphy` (acphy-desense) |
| `wlapi_noise_cb` (0x152998) | `void (shim, u8 channel, s8 noise)` | `wlc_lq_noise_cb(wlc, channel, noise)` | AC: `wlc_phy_noise_cb` (P6.5) |
| `wlapi_get_wlc_info` (0x1528bc) | `void *(shim)` | no call: returns `wlc` | AC: `wlc_phy_noise_avg` |
| `wlapi_init_timer` (0x152b5d) | `wl_timer *(shim, void (*fn)(void *), void *arg, const char *name)` | `wl_init_timer(wl, fn, arg, name)` (glue) | AC: `wlc_phy_attach` (timer "phycal") |
| `wlapi_add_timer` (0x152b3f) | `void (shim, wl_timer *t, u32 ms, int periodic)` | `wl_add_timer(wl, t, ms, periodic)` | AC: calibration scheduler (`sub_0b2a90`, `sub_0b56ce`, acphy-cal-tx) |
| `wlapi_del_timer` (0x152b30) | `bool (shim, wl_timer *t)` | `wl_del_timer(wl, t)` | AC: `wlc_phy_down`, `sub_0b2a90`, `wlc_phy_cal_perical_mphase_reset` |
| `wlapi_free_timer` (0x152b4e) | `void (shim, wl_timer *t)` | `wl_free_timer(wl, t)` | AC: `wlc_phy_detach` |
| `wlapi_bmac_rate_shm_offset` (0x1529bd) | `u16 (shim, u8 rate)` | `wlc_bmac_rate_shm_offset(wlc_hw, rate)` | `wlc_phy_txpower_update_shm` (not called for the AC-PHY, unverified) |
| `wlapi_bmac_write_template_ram` (0x1529cf) | `void (shim, int offset, int len, void *buf)` | `wlc_bmac_write_template_ram(wlc_hw, ...)` | `wlc_phy_do_dummy_tx` (not on the AC path, unverified) |
| `wlapi_bmac_mctrl` (0x152a82) | `void (shim, u32 mask, u32 val)` | `wlc_bmac_mctrl(wlc_hw, mask, val)` | `wlc_radioreg_enter/exit` (A/G and LP paths) |
| `wlapi_high_txpwr_limit_update_req` (0x152947) | `void (shim)` | `wlc_channel_update_txpwr_limit(wlc)` | not AC |
| `wlapi_copyto_objmem`, `wlapi_copyfrom_objmem` (0x15297c, 0x15298a) | `void (shim, u32 offset, void *buf, int len, u32 sel)` | `wlc_bmac_copyto/copyfrom_objmem(wlc_hw, ...)` | no PHY caller |
| `wlapi_bmac_core_phypll_reset`, `_ctl`, `wlapi_bmac_macphyclk_set`, `wlapi_bmac_phy_reset`, `wlapi_bmac_get_txant` | `(shim[, u8])` | the `wlc_bmac_*` function of the same name with `wlc_hw` | N, HT, LP, LCN only |
| `wlapi_intrsoff`, `wlapi_intrsrestore`, `wlapi_intrson` | `(shim[, u32])` | `wl_intrsoff/restore/on(wl[, mask])` (glue) | LP, SSN only |
| `wlapi_bmac_time_since_bcn_get` | `u8 (shim)` | no call: a byte of the MAC's state | LCN40 only |
| `wlapi_ucode_sample_init`, `wlapi_bmac_pkteng` | `void (...)` | empty | no PHY caller |

### P8. wlc_phy_watchdog (.text+0x0bb8ff, name original): the complete AC-PHY path

acphy-cal-tx section 11 describes only the calibration trigger (step 9.2
below); this is the whole function for PHY type 11. Input: `pi`; result 0
(dis). Called once a second by `wlc_bmac_watchdog`. Steps (dis; run: two
direct ticks after up, see Verification):

1. `sh+0x34` += 1. (This is the only place that advances the time counter:
   one unit = one watchdog tick = 1 s; acphy-attach/acphy-cal-tx attribute it
   to the MAC.)
2. If `pi+0x18c` (`watchdog_override`, 1 after attach) is 0: return 0.
3. Copies of the debug iovars (P7.2): if (u16) `pi+0x1136` != 0: `pi+0x45a` =
   `pi+0x45c` = it; if (u16) `pi+0x113a` != 0: `pi+0x44c` = `pi+0x448` = it;
   if (u16) `pi+0x1138` != 0: `pi+0x446` = `pi+0x44a` = it. (No AC-PHY
   function reads these six fields.)
4. If any of bits 0x206 of `pi+0x19c` (scan, RM, 0x200) is set: go to step 7.
5. Glitch statistics (no AC-PHY function reads the results; only the three
   shared memory reads are visible): a = `SHM(0x10e)` (u16), b = `SHM(0x10c)`
   (s16), c = `SHM(0x158)` (s16), read in this order. Differences to the
   previous tick: d0 = a - `pi+0x48c` (32 bit; `pi+0x48c` = a); d1 = b -
   (s16) `pi+0x300` (`pi+0x300` = b); d2 = c - (s16) `pi+0x3f0` (`pi+0x3f0` =
   c); e = (s16) `pi+0x302`, then `pi+0x302` = 0. Derived: on 2.4 GHz f0 = d0
   - d2 and f1 = d1 + e; on 5 GHz f0 = d0 and f1 = d1. Each of the six values
   v feeds a two-entry moving sum when v >= 0 (d0, d1 only; f0, d2, f1 and -e
   with the band conditions of the code): prev = avg; sum = sum - win[i] + v;
   avg = sum >> 1 (16 bit); win[i] = v; i = (i + 1) mod 2. Fields (avg, prev,
   sum, window, index): d0: 0x488, 0x48a, 0x490, 0x492, 0x4a4; d1: 0x2e4,
   0x2e6, 0x2e8, 0x2ea, 0x2fc; f0: 0x3ea, 0x3ec, 0x3f2, 0x3f6, 0x400; d2:
   0x3e8, 0x3ee, 0x3f4, 0x3fa, 0x404; f1: 0x40a, 0x40c, 0x410, 0x414, 0x418;
   -e: 0x408, 0x40e, 0x412, 0x41c, 0x420 (all in `pi`; `wlc_phy_cal_init`
   gives the start values, acphy-cal-tx 8). Order and conditions: d0 if d0
   >= 0; d1 if d1 >= 0; then f0/d2: on 5 GHz with f0 >= 0: f0, then d2 if d2
   >= 0; on 2.4 GHz with d0 >= 0: f0 and d2 only if d2 >= 0; in all other
   cases d2 alone if d2 >= 0; then f1/-e: on 5 GHz with f1 >= 0: f1, then -e
   if -e >= 0; on 2.4 GHz with d1 >= 0: f1 and -e only if -e >= 0; in all
   other cases -e alone if -e >= 0. (`pi+0x302` has no other writer on this
   path, so e = 0.)
6. If bit 0 of `sh+0x80` (interference mode) is set:
   `wlc_phy_desense_aci_engine_acphy(pi)`; if bit 1 or 2 is set:
   `wlc_phy_hwaci_engine_acphy(pi)` (both acphy-desense).
7. If `sh+0x34` is a multiple of `sh+0x74` (15): `wlc_phyreg_enter(pi)`;
   `wlapi_update_bt_chanspec(sh+0x20, pi+0x17e, bit 1 of pi+0x19c, bit 2 of
   pi+0x19c)`; `wlc_phyreg_exit(pi)`.
8. If none of bits 0x20e of `pi+0x19c` is set: `sub_0baff6(pi, 1, low byte
   of pi+0x17e)` (noise request, P6.1).
9. If `pi+0x18f` != 0 and `sh+0x34` - `pi+0x190` > 5: `pi+0x18f` = 0 (a noise
   request without answer is dropped after 5 s).
10. If (`pi+0xc44` = 0 or `sh+0x34` - `pi+0xc44` >= `sh+0x74`) and bit 1 of
    `pi+0x19c` is clear: `sub_0b72a4(pi)` - 0 for the AC-PHY, so `pi+0xc44`
    is never set.
11. If none of bits 0x20f of `pi+0x19c` is set:
    1. If `pi_ac+0x912` (high RSSI LNA bypass supported, 1 for PHY revision
       <= 1): `wlc_phy_hirssi_elnabypass_engine(pi)`.
    2. Calibration trigger (acphy-cal-tx 11): if `pi+0x199` = 0, `pi+0xf89`
       is neither 0 nor 3, `cal+0xc8` = 0 and `sh+0x34` - `cal+0xc0` >=
       `sh+0x7c`: `wlc_phy_cal_perical(pi, 2)`.
    3. If `cal+0xc8` != 0: `cal+0xc8` -= 1.
    4. Slot `pi+0x100` (`sub_09737d`, acphy-attach "Function pointer table":
       CRS-cal noise request, then `PHY(0x520)` core rotation with the MAC
       suspended).
    5. If `wlapi_bmac_btc_mode_get(sh+0x20)` != 0:
       `wlapi_bmac_btc_period_get(sh+0x20, &period, &active)`; if active !=
       `pi+0xfa2` and slot `pi+0xf8` is set: `wlc_phy_btc_adjust_acphy(pi,
       active)`; `pi+0xfa4` = period (u16); `pi+0xfa2` = active.
    6. `wlc_phy_stop_bt_toggle_acphy(pi)` (acphy-rxgain 8).
12. Return 0.

Run (2.4 GHz channel 1, `sh+0x34` preset to 14): tick 1 made the shared
memory reads 0x10e, 0x10c, 0x158, both engines, the Bluetooth chanspec update
(15th tick), the noise request (6 writes, `D11(0x124)`), `sub_0b72a4`, the LNA
bypass engine, `sub_09737d` (its CRS-cal request found the reason-1 request
pending and only set `pi_ac+0x41`), `wlapi_bmac_btc_mode_get` (0) and
`wlc_phy_stop_bt_toggle_acphy`: 150 accesses. Tick 2: the same without the
Bluetooth update and without shared memory writes (the noise request was
still pending): 61 accesses. No calibration (120 ticks not elapsed).

Completeness of acphy-cal-tx: its section 3 (`wlc_phy_cal_perical`) covers
every AC branch (reasons 2..6; reason 3 only for mode 2; other reasons fall
through to step 6 there); its section 11 covers only step 11.2 above, and
the rest of the watchdog is completed here.

### P10. AC functions behind entry points that no other specification describes

**wlc_phy_get_femctrl_bt_wlan_ovrd_acphy** (.text+0x09057a, name original;
behind `wlc_phy_get_femctrl_bt_wlan_ovrd`): `wlapi_suspend_mac_and_wait`;
a = read `PHY(0x418)`; b = read `PHY(0x418)` (the same register twice); result
= bit 0 of a if bit 1 of b is set, else 0xff; `wlapi_enable_mac`. (Read; the
setter's description, acphy-rxgain section 8, writes these bits: bit 1 =
override on, bit 0 = override value - interpretation.)

**wlc_phy_btc_adjust_acphy** (.text+0x099609, name original; slot `pi+0xf8`,
called by the watchdog, P8 step 11.5): inputs `pi`, active (u8). Only for PHY
revision < 2: `wlapi_suspend_mac_and_wait`; `sub_08f2e9(pi, active)`
(acphy-rxgain 7: receive gain fields of `PHY(0x2d1)`, `PHY(0x2d2)`);
`sub_0995ab(pi, active)` (acphy-chanspec 10: nothing on the BCM4360);
`wlapi_enable_mac`. Not reached in the emulator (`wlapi_bmac_btc_mode_get`
returns 0 there).

**wlc_phy_force_lpvco_2G** (.text+0x08ed84, name original; iovar
`lp_vco_2g`): for PHY revisions 2, 5, 6 only: `pi_ac+0x8e5` = value.
Nothing for revisions 0 and 1.

**wlc_phy_get_SROMnoiselvl_phy** (.text+0x0b2866, name original, wlc_phy_cmn.c;
iovar `phynoise_srom`): inputs `pi`, out (N bytes). c = low byte of
`pi+0x17e`: c < 15: out[core] = `pi+0x1fc + core` (`noiselvl2g`); c <= 48:
`pi+0x200 + core`; c <= 64: `pi+0x204 + core`; c <= 128: `pi+0x208 + core`;
else `pi+0x20c + core` (`noiselvl5g` of sub-bands 0..3, acphy-attach). No
access.

## Verification

All harnesses are in `re-out\analysis\phy-cmn\` and use the tools
unchanged (emulated card: chip 0x4360 rev 3, PHY revision 1, radio revision
4, synthetic SROM `tools\re\data\synthetic-4360-2x2.vars`, brought up to
2.4 GHz channel 1, 20 MHz, two cores). Outputs are kept next to them.

| What | How | Result |
|---|---|---|
| The 83 entry points | computed from `re-out\index.tsv` (functions of `wlc_phy_*.c` except the shim with a caller outside these files) -> `entries.txt`; 39 of them are called in the standard session (`boundary.txt`, `boundary.py wlc_phy_cmn.c wlc_phy_ac.c wlc_phy_iovar.c --args`) | 83, as the assignment says; the arguments of the first calls agree with the prototypes of D1 (e.g. `wlc_phy_txpower_limit_set(pi, ppr, 0x1001)`, `wlc_phy_mute_upd(pi, 1, 8)`, `wlc_phy_hold_upd(pi, 2, 0)`, `wlc_phy_stf_chain_init(pi, 3, 3)`) |
| Widths, hidden results, unused arguments | disassembly (`blob.py dis`) of every entry point marked (dis) | as written in D1 and P1..P10 |
| P1/P2 entry points | `callfn.py` (direct calls), `p12.txt` | preamble override 1 ignored on the AC-PHY; `pi+0xfa1`, `pi+0x180`, `pi+0x10fa` as described; mute/hold bits of `pi+0x19c` (0x20 -> 0x30 -> 0x20, 0x22 -> 0x20); un-mute with flags 1 at `sh+0x34` = 200 writes 80 (200 - 120); hold release: 30 accesses (MAC suspend/enable of the empty cache restore); `wlc_phy_BSSinit(pi, 0, -80)` fills the ring with -80 and the per-core rings with -92; `wlc_phy_stf_chain_set(pi, 1, 3)`: `sh+0xa6` = 1, 126 accesses (H7); swdiv set bit 3: read 0 / write 8 of `D11(0x49c)` (16 bit), get: 1; `wlc_phy_interference_set`, `wlc_phy_acimode_noisemode_reset`, `wlc_phy_interf_rssi_update`: no access; `wlc_phy_set_deaf`: 67 accesses; `wlc_phy_txpower_set(pi, 0x200, ...)` = 5; `wlc_phy_down` = 0 and clears `pi_ac+0x338` |
| Channel lists | `estpout.txt` | firstch(1) = 0xffffd022, firstch(2) = 0x096c1001 (low halves 0xd022, 0x1001; the upper halves are leftovers as described); validch(1) bitmap = all 5 GHz channels of D2 including 144 |
| Tx power order of services | `pprlog.py up` -> `pprlog-up.txt` (first `wlc_phy_txpower_limit_set` of `wlc_up`) | exactly the order of P4.3: create R, copy, `wf_chspec_ctlchan`, create T, S, offsets (first time), clear; per core: set 127, sromlimit, compare_min, minus 6, compare_min, force_disabled(4), get_max, get_min(4), cmn_val_minus, compare_max; delete R, T, S; `sub_09949f`; targets 70/70, minimum 46/46, offsets 0, 4, 8, 12, 24 |
| `wlc_phy_txpower_set` with target 60 and regulatory 76 | `pprlog.py set 60 76` -> `pprlog-set.txt` | suspend, power control off, `sub_0b8ca4` (targets 60/54), `sub_0b72a4`, power control on (saved 1), enable; result 0 |
| `wlc_phy_txpower_limit_set` with every rate limited to 40 | `pprlog.py limit 40` -> `pprlog-limit.txt` | targets 34 (40 - 6) for both cores, offsets 0 |
| `wlc_phy_txpwr_apply_srom11` | `srom11chk.py` (model written from P4.7 vs. the object, seeds 1, 7, 12345) | 1104 cases, 0 differences |
| `wlc_phy_get_est_pout` | `estpout.txt` | suspend, reads `PHY(0x642)`, `PHY(0x640)`, `PHY(0x842)`, `PHY(0x840)`, enable; all outputs 0 (model) |
| `wlc_phy_rssi_compute` | `rssi.txt` (synthetic receive headers) | as listed in P5 |
| Noise protocol | `noise1.txt`, `noise2.txt` (shared memory filled by hand between request and interrupt) | as listed in P6 (-71/-77 dBm, `wlapi_noise_cb(shim, 1, -71)`, rings, flags) |
| ioctls | `ioctl.txt` (issued through `wlc_ioctl`) | all six reach `wlc_phy_ioctl`; results as in P7.1 |
| Watchdog | `wd.txt` (two direct ticks) | as listed in P8 |

Not run (read only): the iovar handlers of P7.2 (except the table layout);
the AC-PHY functions reached only with Bluetooth coexistence active
(`wlc_phy_btc_adjust_acphy`); the error paths of the ppr allocations; the
non-AC branches (not described).

## Open questions

1. `pi+0xfa1` (`wlc_phy_btclock_war`), `pi+0x17d` (`wlc_phy_ofdm_rateset_war`)
   and `pi+0x1136/0x1138/0x113a` (debug iovars) have no reader on the
   AC-PHY path that was found by text search; their purpose is unknown.
2. `pi+0xc21`: read by the noise request only; no writer found (0).
3. `D11(0x49c)` (software antenna diversity) and `D11(0x6c6)` (`edcrs`):
   purpose of the registers unknown.
4. The meaning of the hold bits other than 1 (scan), 4 (mute) and 5 is taken
   from brcmsmac names (unverified); bit 9 (0x200) has no known name.
5. Which ioctl the MAC handles itself before the default case: only the six
   numbers of P7.1 were checked to arrive at `wlc_phy_ioctl`.
6. The real board's boardflags3 bit 11 (`pi_ac+0x34d`), which switches on the
   MCS 8/9 reductions of P4.7, is unknown (0 in the synthetic SROM).
7. The second sideband adjustment of P4.7 (80 MHz, outermost 20 MHz channel)
   uses bits 2..5 of the `sb20in80and160*` words, not a nibble; that is what
   the code does (dis), whether it matches the SROM layout's intent is
   unknown.

### Corrections and additions to other specifications

* acphy-desense ("the writer of `pi_ac+0x14 + c` was not located"; "`pi_ac+0x41`
  = keep calibrating"): the writer is `sub_0b701e` (P6.4): `pi_ac+0x14 + c` =
  noise of core c in dBm + 67, written at every conversion of a noise
  measurement. `pi_ac+0x41` is set by `sub_0baff6` when a CRS-calibration
  noise request arrives while another measurement is pending, and cleared by
  every request (P6.1).
* acphy-cal-tx ("free-running time counter incremented by the MAC
  watchdog"): `sh+0x34` is advanced by `wlc_phy_watchdog` itself (P8 step
  1), which the MAC's watchdog calls once a second.
* acphy-cal-tx section 8 (`wlc_phy_cal_init`): the fields `pi+0x2e4..0x420`
  and `pi+0x488..0x4a4` it initialises are the state of the watchdog's glitch
  statistics (P8 step 5), not calibration step sizes.
* acphy-cal-tx section 11 describes only the calibration trigger of the
  watchdog; P8 gives the whole AC path.
