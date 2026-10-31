# The PHY as the MAC layer sees it: the 79 entry points

The MAC layer of Broadcom's object (`wlc_bmac.c`, `wlc.c`, `wlc_channel.c`,
`wlc_stf.c`, ...) reaches the PHY only through the 79 functions below: direct
calls, no function pointers (measured: no function of `wlc_phy_cmn.c` or
`wlc_phy_ac.c` has its address taken outside the PHY files), and it never
reads or writes the PHY's state itself (measured in the emulator over attach,
up, channel changes, watchdog and down: no access from outside the PHY files
to the 256 bytes of `sh`, the 4464 of `pi`, the 2336 of `pi_ac`). The handles
`sh` and `pi` are opaque to the MAC.

So the whole PHY can be replaced at these 79 functions. That is what the
hybrid build does with the open PHY (`hybrid/phy.c`, switch `open_phy`): every
entry point is taken over and calls the open function named in the table.

This document fixes the open function for each entry point and says which
specification describes its behaviour. Scope: chip 0x4360, AC-PHY (type 11)
revisions 0 and 1, radio 2069 revisions 3 and 4; for the behaviour of other
PHY types the object has branches that are not described.

## Conventions

* Arguments: `pi` is the PHY handle, `sh` the shared handle. In the open
  functions `phy` is the `struct bcm4360_phy *` of `bcm4360_phy_attach()`.
* Types: the object's `bool` arguments are bytes; the open functions take
  `bool` (the adapter converts). `ppr` arguments are pointers to the MAC's
  per-rate power structures (`ppr_t`), opaque to the PHY and handled through
  the `ppr_*` services ([acphy-api-txpower.md](acphy-api-txpower.md)).
  Results of type `s8`, `u8`, `u16` are returned in the low bits as the object
  does.
* "No effect" means: on the 4360 with an AC-PHY the object's function makes no
  register access, calls no service and changes no state; the open function
  does nothing.

## The entry points

Status: **done** = the open function exists and is compared
(`tools/re/phy_scenarios.py`); **new** = to be written from the named
specification. Seven entry points that `phy_scenarios.py` lists had no open
function: the scenarios skip a stage whose open function is missing, so
they were never compared (`cal_init`, `down`, `watchdog`, `hold_upd`,
`mute_upd`, `antsel_type_set`, `preamble_override_get`); they are marked new.

### Attach, detach, identity

| Entry point (object) | Open function | Status | Specification |
|---|---|---|---|
| `wlc_phy_shared_attach(params)` | - (the adapter keeps the parameters; no hardware access) | adapter | this document, A1 |
| `wlc_phy_shared_detach(sh)` | - | adapter | A1 |
| `wlc_phy_attach(sh, regs, bandtype, vars)`, first call | `bcm4360_phy_attach(hw, board)` | done | acphy-attach |
| `wlc_phy_attach(...)`, call for the second band | `void bcm4360_phy_attach_band(phy, u32 bandtype)` | new | A2 |
| `wlc_phy_detach(pi)` | `bcm4360_phy_detach(phy)` | done | acphy-attach |
| `wlc_phy_get_phyversion(pi, &type, &rev, &radioid, &radiover)` | `bcm4360_phy_get_phyversion` | done | acphy-attach |
| `wlc_phy_get_coreflags(pi)` | `bcm4360_phy_get_coreflags` | done | acphy-attach |
| `wlc_phy_get_encore(pi)` | `bool bcm4360_phy_get_encore(phy)` | new | A3 |
| `wlc_phy_cap_get(pi)` | `bcm4360_phy_cap_get` | done | acphy-attach |
| `wlc_phy_machwcap_set(pi, caps)` | `bcm4360_phy_machwcap_set` | done | acphy-attach |

### Bring-up, radio, channel

| Entry point (object) | Open function | Status | Specification |
|---|---|---|---|
| `wlc_phy_hw_clk_state_upd(pi, on)` | `bcm4360_phy_hw_clk_state_upd` | done | acphy-radio |
| `wlc_phy_hw_state_upd(pi, up)` | `bcm4360_phy_hw_state_upd` | done | acphy-radio |
| `wlc_phy_por_inform(pi)` | `bcm4360_phy_por_inform` | done | acphy-init |
| `wlc_phy_anacore(pi, on)` | `bcm4360_phy_anacore` | done | acphy-radio |
| `wlc_phy_switch_radio(pi, on)` | `bcm4360_phy_switch_radio` | done | acphy-radio |
| `wlc_phy_cal_init(pi)` | `void bcm4360_phy_cal_init(phy)` | new | acphy-cal-tx section 8 |
| `wlc_phy_init(pi, chanspec)` | `bcm4360_phy_init` | done | acphy-init |
| `wlc_phy_down(pi)` | `u32 bcm4360_phy_down(phy)` | new | A12 |
| `wlc_phy_chanspec_radio_set(pi, chanspec)` | `bcm4360_phy_chanspec_radio_set` | done | acphy-chanspec |
| `wlc_phy_chanspec_get(pi)` | `bcm4360_phy_chanspec_get` | done | acphy-chanspec |
| `wlc_phy_chanspec_set(pi, chanspec)` | `bcm4360_phy_chanspec_set` | done | acphy-chanspec |
| `wlc_phy_bw_state_set(pi, bw)` | `bcm4360_phy_bw_state_set` | done (not compared) | A3 |
| `wlc_phy_clk_bwbits(pi)` | `bcm4360_phy_clk_bwbits` | done | acphy-chanspec |
| `wlc_phy_chanspec_band_firstch(pi, band)` | `u16 bcm4360_phy_chanspec_band_firstch(phy, u32 band)` | new | A4 |
| `wlc_phy_chanspec_band_validch(pi, band, chanvec)` | `void bcm4360_phy_chanspec_band_validch(phy, u32 band, u8 *chanvec)` | new | A4 |
| `wlc_phy_chanspec_ch14_widefilter_set(pi, on)` | `void bcm4360_phy_chanspec_ch14_widefilter_set(phy, bool on)` | new | A3 |
| `wlc_phy_block_bbpll_change(pi, block, going_down)` | `void bcm4360_phy_block_bbpll_change(phy, bool block, bool going_down)` | new | A5 |
| `wlc_acphy_set_scramb_dyn_bw_en(pi, on)` | `bcm4360_phy_set_scramb_dyn_bw_en` | done | acphy-init |
| `wlc_phy_ldpc_override_set(pi, on)` | `bcm4360_phy_ldpc_override_set` | done | acphy-init |
| `wlc_phy_watchdog(pi)` | `void bcm4360_phy_watchdog(phy)` | new | [acphy-api-watchdog.md](acphy-api-watchdog.md) |

### State the MAC sets, flags, work-arounds

| Entry point (object) | Open function | Status | Specification |
|---|---|---|---|
| `wlc_phy_hold_upd(pi, id, on)` | `void bcm4360_phy_hold_upd(phy, u32 id, bool on)` | new | A13 |
| `wlc_phy_mute_upd(pi, mute, flags)` | `void bcm4360_phy_mute_upd(phy, bool mute, u32 flags)` | new | A14 |
| `wlc_phy_stf_chain_init(pi, txchain, rxchain)` | `bcm4360_phy_stf_chain_init` | done | acphy-attach |
| `wlc_phy_antsel_type_set(pi, type)` | `void bcm4360_phy_antsel_type_set(phy, u8 type)` | new | A15 |
| `wlc_phy_preamble_override_get(pi)` | `s8 bcm4360_phy_preamble_override_get(phy)` | new | A3 |
| `wlc_phy_preamble_override_set(pi, val)` | `void bcm4360_phy_preamble_override_set(phy, s8 val)` | new | A3 |
| `wlc_phy_btclock_war(pi, on)` | `void bcm4360_phy_btclock_war(phy, bool on)` | new | A3 |
| `wlc_phy_ofdm_rateset_war(pi, on)` | `void bcm4360_phy_ofdm_rateset_war(phy, bool on)` | new | A3 |
| `wlc_phy_bf_preempt_enable(pi, on)` | `void bcm4360_phy_bf_preempt_enable(phy, bool on)` | new | A3 |
| `wlc_phy_initcal_enable(pi, on)` | `void bcm4360_phy_initcal_enable(phy, bool on)` | new | A3 |
| `wlc_phy_set_filt_war(pi, on)` | `void bcm4360_phy_set_filt_war(phy, bool on)` | new | A3 |
| `wlc_phy_tkip_rifs_war(pi, val)` | `void bcm4360_phy_tkip_rifs_war(phy, u8 val)` | new | A3 |
| `wlc_phy_antsel_init(pi, lut_init)` | `void bcm4360_phy_antsel_init(phy, bool lut_init)` | new | A3 |
| `wlc_phy_freqtrack_start(pi)`, `wlc_phy_freqtrack_end(pi)` | `void bcm4360_phy_freqtrack_start(phy)`, `..._end(phy)` | new | A3 |
| `wlc_phy_interference_set(pi, init)` | `void bcm4360_phy_interference_set(phy, bool init)` | new | A3 |
| `wlc_phy_acimode_noisemode_reset(pi, channel, clear_aci, clear_noise, disassoc)` | `void bcm4360_phy_acimode_noisemode_reset(phy, u32 channel, bool clear_aci, bool clear_noise, bool disassoc)` | new | A3 |
| `wlc_phy_get_femctrl_bt_wlan_ovrd(pi)` | `s8 bcm4360_phy_get_femctrl_bt_wlan_ovrd(phy)` | new | A6 |
| `wlc_phy_set_femctrl_bt_wlan_ovrd(pi, state)` | `void bcm4360_phy_set_femctrl_bt_wlan_ovrd(phy, s8 state)` | new | A6 |
| `wlc_phy_get_ofdm_rate_lookup()` | `const u8 *bcm4360_phy_get_ofdm_rate_lookup(void)` | new | A7 |
| `wlc_phy_BSSinit(pi, bonlyap, noise)` | `void bcm4360_phy_bssinit(phy, bool bonlyap, s8 noise)` | new | A8 |
| `wlc_phy_cal_perical(pi, reason)` | `void bcm4360_phy_cal_perical(phy, u8 reason)` | new | A9 |
| `wlc_phy_ioctl(pi, cmd, len, arg)` | `s32 bcm4360_phy_ioctl(phy, s32 cmd, u32 len, void *arg)` | new | A10 |
| `wlc_phy_iovar_dispatch(pi, id, ...)` | `s32 bcm4360_phy_iovar_dispatch(...)` | new | A11 |

### Transmit power ([acphy-api-txpower.md](acphy-api-txpower.md))

| Entry point (object) | Open function | Status |
|---|---|---|
| `wlc_phy_txpower_limit_set(pi, txpwr)` | `void bcm4360_phy_txpower_limit_set(phy, void *txpwr)` | new |
| `wlc_phy_txpower_set(pi, qdbm, override, reg_pwr)` | `s32 bcm4360_phy_txpower_set(phy, u32 qdbm, bool override, void *reg_pwr)` | new |
| `wlc_phy_txpower_get(pi, &qdbm, &override)` | `s32 bcm4360_phy_txpower_get(phy, u32 *qdbm, u8 *override)` | new |
| `wlc_phy_txpower_sromlimit(pi, chanspec, &min, max_ppr, core)` | `void bcm4360_phy_txpower_sromlimit(phy, u16 chanspec, s8 *min_pwr, void *max_pwr, u8 core)` | new |
| `wlc_phy_txpower_get_target_max(pi)`, `..._min(pi)` | `u8 bcm4360_phy_txpower_get_target_max(phy)`, `..._min(phy)` | new |
| `wlc_phy_txpower_hw_ctrl_get(pi)` | `bool bcm4360_phy_txpower_hw_ctrl_get(phy)` | new |
| `wlc_phy_txpower_ipa_ison(pi)` | `bool bcm4360_phy_txpower_ipa_ison(phy)` | new |
| `wlc_phy_txpwr_percent_set(pi, percent)` | `void bcm4360_phy_txpwr_percent_set(phy, u8 percent)` | new |
| `wlc_phy_txpower_core_offset_set(pi, offsets)` | `s32 bcm4360_phy_txpower_core_offset_set(phy, const void *offsets)` | new |
| `wlc_phy_get_est_pout(pi, ...)` | `void bcm4360_phy_get_est_pout(phy, ...)` (arguments: see there) | new |
| `wlc_phy_get_olpc_pwroffset(pi, bw)` | `s8 bcm4360_phy_get_olpc_pwroffset(phy, u16 bw)` | new |
| `wlc_phy_tssivisible_thresh(pi)` | `s32 bcm4360_phy_tssivisible_thresh(phy)` | new |
| `wlc_phy_clear_tssi(pi)` | `void bcm4360_phy_clear_tssi(phy)` | new |

### Receive, measurements, chains ([acphy-api-rx.md](acphy-api-rx.md))

| Entry point (object) | Open function | Status |
|---|---|---|
| `wlc_phy_rssi_compute(pi, rxh)` | `void bcm4360_phy_rssi_compute(phy, void *rxh)` | new |
| `wlc_phy_noise_avg(pi)` | `s8 bcm4360_phy_noise_avg(phy)` | new |
| `wlc_phy_noise_sample_intr(pi)` | `void bcm4360_phy_noise_sample_intr(phy)` | new |
| `wlc_phy_noise_sample_request_external(pi)` | `void bcm4360_phy_noise_sample_request_external(phy)` | new |
| `wlc_phy_interf_rssi_update(pi, chanspec, rssi)` | `void bcm4360_phy_interf_rssi_update(phy, u16 chanspec, s8 rssi)` | new |
| `wlc_phy_set_deaf(pi, ...)` | `void bcm4360_phy_set_deaf(phy, ...)` (arguments: see there) | new |
| `wlc_phy_stf_chain_set(pi, txchain, rxchain)` | `void bcm4360_phy_stf_chain_set(phy, u8 txchain, u8 rxchain)` | new |
| `wlc_phy_stf_chain_active_get(pi)` | `u8 bcm4360_phy_stf_chain_active_get(phy)` | new |
| `wlc_phy_ant_rxdiv_set(pi, val)` | `void bcm4360_phy_ant_rxdiv_set(phy, u8 val)` | new |
| `wlc_phy_ant_rxdiv_get(pi, &val)` | `bool bcm4360_phy_ant_rxdiv_get(phy, u8 *val)` | new |
| `wlc_phy_swdiv_ant_get(pi, bit, &val)`, `wlc_phy_swdiv_ant_set(pi, bit, on)` | `s32 bcm4360_phy_swdiv_ant_get(phy, u8 bit, u8 *val)`, `s32 bcm4360_phy_swdiv_ant_set(phy, u8 bit, bool on)` | new |

The sections A1 to A11 follow.

## A1. wlc_phy_shared_attach, wlc_phy_shared_detach

`wlc_phy_shared_attach(params)` copies the MAC's parameter block (layout:
[acphy-attach.md](acphy-attach.md), "The parameters of wlc_phy_shared_attach")
into a new `sh` of 0x100 bytes and sets the watchdog periods, the noise window
and the interference mode (acphy-attach procedure 1). It makes no hardware
access and calls no service. The open PHY keeps `sh` inside
`struct bcm4360_phy` (one PHY per card on the 4360), and `bcm4360_phy_attach()`
makes the same state from `struct bcm4360_phy_board`; so there is no open
function: the adapter keeps the parameters and hands them to the first attach.

`wlc_phy_shared_detach(sh)` frees `sh` (0x100 bytes); no other effect.

## A2. wlc_phy_attach, call for the second band

The MAC calls `wlc_phy_attach(sh, regs, bandtype, vars)` once per band:
`bandtype` 2 = 2.4 GHz, 1 = 5 GHz. On the 4360 the 2.4 GHz band comes first
(that call is `bcm4360_phy_attach`, acphy-attach). Every call, the first one
included, begins with the status flags of the 802.11 core: `sflags` =
`si_core_sflags(sih, 0, 0)` (skipped only for core revision 4). For the second
call, `bcm4360_phy_attach_band(phy, bandtype)`:

1. `sflags` = `bcm4360_chip_core_sflags(hw, 0, 0)` (core revision 42: always).
2. If `bandtype` = 1 and `sflags & 0x0a` = 0 (no 5 GHz PHY, no dual-band
   PHY): fail (the object returns NULL; the MAC then has no PHY for the band).
3. If `sflags & 0x08` (one PHY for both bands) and the PHY exists:
   `bcm4360_mac_corereset(hw, coreflags)` with the PHY's core flags
   (`pi+0x170`, open `phy->ver.coreflags`: 0x2000 when the PHY has a 2.4 GHz
   part), then the attach count `pi+0x188` (`phy->refcnt`) + 1; the result is
   the same PHY.
4. Otherwise the object would attach a second PHY (not the case on the 4360):
   fail.

Result: true if the PHY serves the band (step 3). `wlc_phy_detach` undoes one
count each (acphy-attach procedure 13).

## A3. Entry points that only store, or have no effect

For the AC-PHY on the 4360:

| Entry point | Effect |
|---|---|
| `wlc_phy_get_encore(pi)` | result `pi+0x178` (u8); only the A/G-PHY attach writes it: 0 |
| `wlc_phy_bw_state_set(pi, bw)` | `pi+0x182` (u16, open `phy->bw`) = `bw`; nothing else |
| `wlc_phy_preamble_override_get(pi)` | result `pi+0x22a` (`phy->preamble_override`, s8) |
| `wlc_phy_preamble_override_set(pi, val)` | if `val` = 0 or the PHY type is not 11: `pi+0x22a` (`phy->preamble_override`) = `val`. For the AC-PHY a value other than 0 is ignored |
| `wlc_phy_btclock_war(pi, on)` | `pi+0xfa1` (u8) = `on` (read only by the LCN-PHY: no further effect) |
| `wlc_phy_ofdm_rateset_war(pi, on)` | `pi+0x17d` (u8) = `on` (read only by the A/G-PHY: no further effect) |
| `wlc_phy_chanspec_ch14_widefilter_set(pi, on)` | PHY types 0 and 2 only: no effect |
| `wlc_phy_bf_preempt_enable(pi, on)` | PHY types 0 and 2 only: no effect |
| `wlc_phy_initcal_enable(pi, on)` | N-PHY only: no effect (acphy-cal-tx section 9) |
| `wlc_phy_set_filt_war(pi, on)` | HT-PHY only: no effect |
| `wlc_phy_tkip_rifs_war(pi, val)` | N-PHY only: no effect |
| `wlc_phy_antsel_init(pi, lut_init)` | N-PHY only: no effect |
| `wlc_phy_freqtrack_start(pi)`, `wlc_phy_freqtrack_end(pi)` | PHY type 2 only: no effect |
| `wlc_phy_interference_set(pi, init)` | only for PHY types 4 and 7 and chip 0xa886: no effect |
| `wlc_phy_acimode_noisemode_reset(pi, ...)` | only for HT-PHYs and N-PHYs of revision 3 and above (the object tests "revision > 2 or HT", then calls the N-PHY code for revision > 2): for AC-PHY revisions 0 and 1 no effect |

The stores are kept so that the state stays the object's; nothing in the AC
code reads `pi+0x17d`, `pi+0xfa1` or `pi+0x178`.

## A4. wlc_phy_chanspec_band_firstch, wlc_phy_chanspec_band_validch

Both walk the PHY's channel list: 67 entries (channel number, centre frequency
in MHz), in this order: 1 to 14; 34, 38, 42, 46; 36 to 64 (36, 40, 44, 48, 52,
54, 56, 60, 62, 64); 100 to 144 (100, 102, 104, 108, 110, 112, 116, 118, 120,
124, 126, 128, 132, 134, 136, 140, 144); 149, 151, 153, 157, 159, 161, 165;
184, 185, 187, 188, 189, 192, 196, 200, 204, 207, 208, 209, 210, 212, 216.
(The frequencies are the usual 5000 + 5 * channel and 2407 + 5 * channel,
2484 for channel 14, 4000 + 5 * channel above 183; the entry for channel 134
has 5660 MHz.) `band` is 2 for 2.4 GHz and 1 for 5 GHz; a channel belongs to
2.4 GHz if its number is below 15.

A flag `pi+0xfac` (u8) excludes channels 149 to 165; only an A/G-PHY function
writes it, so it is 0 for the AC-PHY.

`wlc_phy_chanspec_band_firstch(pi, band)`: for the AC-PHY, the first entry of
the list whose channel belongs to `band` (and is not excluded by `pi+0xfac`);
result its chanspec: channel | 0x1000 (20 MHz) | 0xc000 for 5 GHz. 0x00ff if
there is none. (For HT/N/LP-PHYs with a 40 MHz bandwidth the object returns a
40 MHz chanspec instead: not described.)

`wlc_phy_chanspec_band_validch(pi, band, chanvec)`: `chanvec` is a bit vector
of 28 bytes (224 channels), first cleared; for every entry of the list whose
channel belongs to `band` and that is not excluded, bit `channel` is set
(byte `channel / 8`, bit `channel % 8`). Excluded besides `pi+0xfac`: channel
144 for PHY types other than 11 (so for the AC-PHY it is valid). No result.

## A5. wlc_phy_block_bbpll_change

[acphy-chanspec.md](acphy-chanspec.md), section 12. `pi+0x113e` (u8) = 1 if
`block`; else `pi+0x113e` = 0 and, if `pi+0x1140` (u16) is not 0 and
`going_down` is false, the spur mode is set for the frequency in `pi+0x1140`
(section 12; `pi+0x1140` is written only by `wlc_phy_get_spurmode` while
blocked, which the 4360 does not reach, so it stays 0).

## A6. wlc_phy_get_femctrl_bt_wlan_ovrd, wlc_phy_set_femctrl_bt_wlan_ovrd

Override of the front end control between Bluetooth and WLAN, bits 0 and 1 of
`PHY(0x418)`. AC-PHY only (other types: get gives -1, set does nothing).

Get: `bcm4360_mac_suspend`; read `PHY(0x418)` twice (two reads); if bit 1 of
the second value is set the result is bit 0 of the first value, else -1;
`bcm4360_mac_enable`.

Set(`state`): `pi_ac+0x32e` (s8) = `state`; `bcm4360_mac_suspend`; then
* `state` = 1: `PHY(0x418)` bit 0 = 1, then bit 1 = 1 (two `phy_reg_mod`);
* `state` = 0: bit 0 = 0, then bit 1 = 1;
* any other value: bit 1 = 0, then bit 0 = 0;
`bcm4360_mac_enable`.

## A7. wlc_phy_get_ofdm_rate_lookup

Returns a pointer to a table of 8 bytes, the rate in units of 500 kbit/s for
each value of the three low bits of the OFDM SIGNAL rate field: 96, 48, 24,
12, 108, 72, 36, 18 (48, 24, 12, 6, 54, 36, 18, 9 Mbit/s). The MAC reads the
table; the open code provides its own constant table.

## A8. wlc_phy_BSSinit

`wlc_phy_BSSinit(pi, bonlyap, noise)`: called when the MAC joins or starts a
BSS. `bonlyap` is not used. For the AC-PHY:

1. The noise window `sh+0x96..0x9d` (8 bytes, open
   `phy->interference.noise_window`) = `noise` (all eight).
2. `sh+0xa0` (u32, the next index into the noise window) = 0.
3. If the interference mode in use `sh+0x80` (`phy->interference.mode`) is 3
   and bit 0 of `pi+0xc0c` (`phy->interference.flags`) is set: `pi+0xc00`
   (u32) = `sh+0x34` (the current time in seconds of the watchdog) + 8.
4. The two arrays `pi+0xed4` and `pi+0xef4` of 16 values (s16 each) = -92.
5. `pi+0xf54` (u8) = 0.

No hardware access, no service.

## A9. wlc_phy_cal_perical

[acphy-cal-tx.md](acphy-cal-tx.md), section 3, with sections 4 to 7 (the
multi-phase schedule, its timer callback, reset and restart), all for the
AC-PHY. The second argument of `wlc_phy_cals_acphy` (open `bcm4360_phy_cals`)
is 0 on every path of `wlc_phy_cal_perical`.

## A10. wlc_phy_ioctl

`wlc_phy_ioctl(pi, cmd, len, arg)`: `arg` points to a 32-bit value. The value
`v` passed in is the first 4 bytes of `arg` if `arg` is not NULL and `len` is
at least 4, else 0. Results are written to `*arg` (32 bit). Error codes:
-23 not supported, -29 out of range, -5 not while up ("not down"), -11 no
clock, -3 failure.

| `cmd` | Name | AC-PHY behaviour |
|---|---|---|
| 0xd3 (211) | get interference mode | `*arg` = `sh+0x80`, with bit 7 set if bit 0 of `pi+0xc0c` is set; result 0 |
| 0xd4 (212) | set interference mode | if `v` < 0 or `v` > 7: -29. If `v` = `sh+0x80`: result 0. Else `sh+0x80` = `v` and "apply" (below) |
| 0x137 (311) | set interference override | if `v` < -1 or `v` > 7: -29. `v` = -1: `sh+0x94` (`forced`) = 0 and the mode becomes `sh+0x84` (2.4 GHz, band bits of `pi+0x17e` = 0) or `sh+0x88` (5 GHz). Else: `sh+0x94` = 1, `sh+0x8c` = `sh+0x90` = `v` (the override for 2.4 and 5 GHz) and the mode becomes `v`. Then `sh+0x80` = the mode and "apply" |
| 0x138 (312) | get interference override | if `sh+0x94` = 0: `*arg` = -1; else `*arg` = `sh+0x80`; result 0 |
| 0x1b (27) | make the PHY uninitialised | if the interface is up (`sh+0x30` != 0): -5. Else the procedure 5 of acphy-attach ("uninitted": `pi+0x187` = 0, `pi+0xc36` = `pi+0xed2` = 0xff, `pi_ac+0x10..0x13` = 0x40 for revisions 0 and 1); result 0 |
| 0x87 (135) | get noise | if the clock is off (`sh+0x31` = 0): -11. Else `*arg` = `wlc_phy_noise_avg(pi)` (sign-extended, [acphy-api-rx.md](acphy-api-rx.md)); result 0 |
| other | | -23 |

"Apply": if the interface is not up (`sh+0x30` = 0): result 0. Else
`bcm4360_mac_suspend`; if `v` is not 0: the interference procedure
(acphy-init section 8, `sub_0b740d`) with mode 0 and `init` = 1; then the same
procedure with the mode `sh+0x80` and `init` = 1; `bcm4360_mac_enable`; result
0 (the procedure always succeeds).

`sh+0x8c` and `sh+0x90` (u32 each, the override modes of 2.4 and 5 GHz) are
not in `phy.h`: zero after attach, written only here.

The glue uses `cmd` 0x87 (`WLC_GET_PHY_NOISE`, statistics and wireless
extensions); the others are reached through the MAC's ioctl from user space.

## A11. wlc_phy_iovar_dispatch

`wlc_phy_iovar_dispatch(pi, actionid, type, params, plen, arg, alen, vsize)`:
the PHY's iovars (table `phy_iovars`, 32 entries). `actionid` is 2 x the
variable id for a get and 2 x id + 1 for a set; the value `v` is the first 4
bytes of `params` if `plen` >= 4, else 0; a get writes 4 bytes to `arg` and
returns 0.

Three of them are used by the MAC itself (by name, through its iovar
functions), and must behave as the object's:

| iovar (id) | Get | Set |
|---|---|---|
| `min_txpower` (0x142) | `*arg` = `pi+0xc37` (u8, minimum transmit power in dBm; also read by `wlc_phy_txpower_sromlimit`, [acphy-api-txpower.md](acphy-api-txpower.md)) | `pi+0xc37` = low byte of `v`; result 0 |
| `phy_rxantsel` (0x29) | N-PHY revision 7 and above only: for the AC-PHY result 0, `arg` not written | result 0, no effect |
| `phy_afeoverride` (0x1af) | `*arg` = `pi+0x1169` (`phy->acphy_1169`) | `pi+0x1169` = low byte of `v`; result 0 |

The other 29 (`phy_muted`, `phy_glitchthrsh`, `phy_noise_up`,
`phy_noise_dwn`, `phy_percal_delay`, `phy_rxiqest`, `edcrs`, `lp_mode`,
`lp_vco_2g`, `radio_pd`, `phynoise_srom`, `num_stream`, `band_range`,
`subband5gver`, `phy_oclscdenable`, `lnldo2`, `phy_crs_war`, `subband_idx`,
`noise_measure_disable`, `pavars2`, `phy_btc_restage_rxgain`, `phy_ed_thresh`,
`phy_sromtempsense` and the six `lpphy_*`) are
reached only from user space (Broadcom's `wl` tool through the private ioctl,
root only); the glue uses none of them. They are **not specified**: the open
function answers -23 (not supported) for them. This is a deliberate
difference from the object, to be closed by a later specification.

## A12. wlc_phy_down

Called by the MAC before it takes the core down. Result: the number of
calibration timer callbacks that could not be cancelled (0 or 1).

1. If the PHY has its calibration timer (`pi+0x1088`, `phy->timer_phycal`):
   `bcm4360_timer_del(hw, timer)`; if that reports the timer was **not**
   pending (false), the result is 1, else 0. Without a timer: 0.
2. For the AC-PHY: the spur mode in use `pi_ac+0x338` (`phy->spur_mode`) = 0
   and the spur mode wanted `pi+0x1164` = 0.

No register access.

## A13. wlc_phy_hold_upd

`wlc_phy_hold_upd(pi, id, on)`: the hold flags `pi+0x19c` (`phy->hold`): set
the bits of `id` if `on`, else clear them. Then, if `id` contains bit 1
(0x2, `BCM4360_PHY_HOLD_SCAN`) and `on` is false (a scan has ended): restore
the calibration results saved for the scan (acphy-cal-tx section 20,
`wlc_phy_scanroam_cache_cal_acphy(pi, 0)`). For the AC-PHY nothing happens
when the scan bit is set.

## A14. wlc_phy_mute_upd

`wlc_phy_mute_upd(pi, mute, flags)`: if `mute`: set bit 4 (0x10) of the hold
flags `pi+0x19c`. Else clear it, and if bit 0 of `flags` is set: the time of
the last calibration in the calibration state block (`cal+0xc0`,
`phy->cal.state.last_cal_time`) = `now - glacial` if the watchdog time `now`
(`sh+0x34`) is greater than the glacial period (`sh+0x7c`,
`phy->glacial_timer`), else 0 (so that the next periodic calibration runs
soon). No register access.

## A15. wlc_phy_antsel_type_set

`wlc_phy_antsel_type_set(pi, type)`: `pi+0x180` (u8) = `type`; `pi+0x10fa`
(bool) = (`type` is 4 or 5). Nothing else on the AC-PHY (the second value is
read by N-PHY code and the `phy_rxantsel` iovar of N-PHYs).

## Verification

| Statement | How |
|---|---|
| The MAC reaches the PHY only through these 79 functions, and never through function pointers | static: every relocation into `wlc_phy_cmn.c`/`wlc_phy_ac.c` from other files (index of the object); no address of a PHY function is taken outside the PHY files |
| The MAC never touches `sh`, `pi`, `pi_ac` | emulator: memory hooks on the three allocations over attach, up, three channel changes, watchdog, down: no access from outside the PHY files |
| A3: no effect / store only | read from the code; the readers of `pi+0x178`, `pi+0x17d`, `pi+0xfa1`, `pi+0xfac` listed with the index (only other PHY types) |
| A12 to A15 | read from the code (all four are small); the session of `tools/re/hybrid.py` runs `wlc_phy_down`, `wlc_phy_hold_upd`, `wlc_phy_mute_upd` and `wlc_phy_antsel_type_set` |
| A4: channel list | data of the object (67 entries) |
| A7: rate table | data of the object (`ofdm_rate_lookup`, 8 bytes) |
| A10: `cmd` 0x87 is the only one the glue uses | `src/wl/sys/wl_linux.c`, `wl_iw.c` |
| A11: iovars used by the MAC | the index: the names `min_txpower`, `phy_rxantsel`, `phy_afeoverride` are referenced by `wlc_init`, `wlc_update_txppr_offset`, `wlc_ol_curpwr_upd`, `wlc_antsel.c`, `wlc_channel.c`; no other name of the table is referenced outside the table |

Everything else in A2, A6, A8, A10 and A11 is read from the code only; the
hybrid test (`tools/re/hybrid.py`) compares the open PHY with the object over a
whole session and probes the entry points the session does not reach.
