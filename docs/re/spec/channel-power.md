# Transmit power of a channel: regulatory limits, user cap, OLPC (channel-power)

What the upper layer computes and hands to the hardware layer for transmit
power, at attach, at up and at every channel change: the per-rate
regulatory limit container (from the regulatory database, `clm.md`, into the
per-rate container, `ppr.md`), the caps applied to it (the user's `qtxpower`,
the 802.11h local power constraint), what the channel manager
(`wlc_channel.c`) does besides that touches the hardware or feeds the PHY,
and the open-loop power control engine (`wlc_olpc_engine.c`). What the PHY
does with the container is in [acphy-api-txpower.md](acphy-api-txpower.md)
(the merge `sub_0b8ca4`) and [acphy-txpower.md](acphy-txpower.md); what the
MAC does with the PHY's offsets is in [cca-stf.md](cca-stf.md) part 2.
Where these calls sit in a channel change and in `wlc_init` is in
[wlc-hw.md](wlc-hw.md) section 2.2 (not repeated here).

Written from the decompiled code and the disassembly; checked by running the
object in the emulator (section "Verification"). Every procedure says what
was **run** (observed in the emulator or checked by a rebuild) and what is
**read** from the code only. Scripts: `re-out/analysis/channel-power/`.

## Scope

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_channel_mgr_attach` | 0x073078 | 1209 | original |
| `wlc_country_lookup` | 0x06f622 | 33 | original |
| `sub_06f468` | 0x06f468 | 442 | assigned: `wlc_country_lookup_rev` |
| `wlc_set_countrycode` | 0x07306a | 14 | original |
| `wlc_set_countrycode_rev` | 0x072f6a | 256 | original |
| `sub_072da4` | 0x072da4 | 454 | assigned: `wlc_set_country_common` |
| `sub_072656` | 0x072656 | 1870 | assigned: `wlc_channels_commit` |
| `wlc_get_flags` | 0x0703e2 | 157 | original |
| `wlc_locale_get_channels` | 0x071e08 | 79 | original |
| `wlc_quiet_channels_reset` | 0x06f124 | 132 | original |
| `wlc_channel_reg_limits` | 0x07047f | 6413 | original |
| `wlc_channel_set_chanspec` | 0x07419b | 190 | original |
| `wlc_channel_update_txpwr_limit` | 0x071d8c | 124 | original |
| `wlc_channel_set_txpower_limit` | 0x0740ed | 174 | original |
| `wlc_channel_band_chain_limit` | 0x074004 | 233 | original (no caller) |
| `sub_0736a4` | 0x0736a4 | 2400 | assigned: `wlc_channel_txchain_pwr_offset_upd` |
| `sub_06ef19` | 0x06ef19 | 144 | assigned: `wlc_channel_min_srom_ppr` |
| `sub_06ee40` | 0x06ee40 | 33 | assigned: `wlc_channel_min_margin_mapfn` |
| `sub_06f334` | 0x06f334 | 275 | assigned: `wlc_channel_afeoverride_upd` |
| `wlc_next_chanspec`, `wlc_default_chanspec` | 0x07014f, 0x0702a0 | 337, 92 | original |
| `wlc_get_regclass` (and `sub_06efa9`) | 0x06f1a8, 0x06efa9 | 267, 123 | original; assigned: `wlc_us_code` |
| `wlc_tpc_get_local_constraint_qdbm` | 0x178ae3 | 114 | original (wlc_tpc.c) |
| `wlc_tpc_set_local_constraint`, `wlc_tpc_set_local_max`, `wlc_tpc_reset_all` | 0x178b55, 0x178ab8, 0x178aaa | 83, 10, 14 | original (wlc_tpc.c) |
| `wlc_cntry_adopt_country_ie` | 0x0772ba | 302 | original (wlc_cntry.c) |
| the `qtxpower` iovar (in `wlc_doiovar`, ids 0x5e get / 0x5f set) | 0x0469ed | - | original (wlc.c) |
| `wlc_olpc_eng_attach` | 0x083b18 | 229 | original |
| `sub_08432b` | 0x08432b | 60 | assigned: `wlc_olpc_eng_up` |
| `sub_083767` | 0x083767 | 94 | assigned: `wlc_olpc_eng_down` |
| `wlc_olpc_eng_reset` | 0x084367 | 45 | original |
| `wlc_olpc_eng_hdl_chan_update` | 0x084310 | 27 | original |
| `wlc_olpc_eng_hdl_txchain_update` | 0x08449c | 52 | original |
| `wlc_olpc_eng_recal` | 0x0842f9 | 23 | original (no caller) |
| `sub_0837c5` | 0x0837c5 | 615 | assigned: `wlc_olpc_chan_needs_cal` |
| `sub_083bfd` | 0x083bfd | 1572 | assigned: `wlc_olpc_eng_chan_update` |
| `sub_083a2c` | 0x083a2c | 162 | assigned: `wlc_olpc_find_chan` |
| `sub_084221` | 0x084221 | 216 | assigned: `wlc_olpc_bsscfg_updown` |
| `wlc_olpc_eng_pkt_complete` | 0x084394 | 264 | original |

Not described: `wlc_get_valid_chanspecs`, `wlc_get_country_list`,
`wlc_get_channels_in_country`, `wlc_channel_get_chanvec`,
`wlc_get_reg_max_power_for_channel` (country IE of an AP), the radar and
restricted chanspec queries, `wlc_rclass_*`, `wlc_handle_clm_dload`: they
answer ioctls, scans or build information elements and give no value to the
hardware layer. The validity checks are in [channel.md](channel.md).

## Overview

The data path of transmit power, at every channel change (run, the
`hybrid.py` session; the same with the open layers, which reproduce it):

1. **Regulatory container.** `wlc_channel_reg_limits(cm, chanspec, p)`
   asks the regulatory database for the per-rate limits of the channel (one
   `clm_limits` call for the whole channel, one for each kind of sub-channel
   of a 40/80 MHz channel) and copies them, group by group, into a `ppr_t`
   of the channel's bandwidth (section 1.6).
2. **Local constraint.** `ppr_apply_max(p, q)`: q = the 802.11h local power
   constraint, 127 (no cap) unless associated with an AP that announced one
   (section 2.2).
3. `sub_0736a4`: per-chain power offsets; no effect in this driver
   (section 2.4).
4. `wlc_bmac_set_chanspec` -> `wlc_phy_txpower_limit_set(pi, p, chanspec)`:
   the PHY combines the container with the user target `qtxpower`
   (`pi+0x1d6`, 92 = 23 dBm from the Linux glue), the board limits from the
   SROM and the back-off, and programs one target per core; it hands the
   per-rate offsets back to the MAC (`wlc_update_txppr_offset`).

The container reaches the PHY by three more ways: when a country is set
while up (section 1.4, cap 127), from the `qtxpower` iovar (section 2.1,
through `wlc_phy_txpower_set`), and from the OLPC engine when a calibration
round ends (section 4, without any cap) or the 802.11h constraint changes
(section 2.2).

All power values are signed bytes in quarter dBm; -128 (0x80) is "rate
disabled" (`ppr.md`).

## Data

### The channel manager `cm` (`wlc+0x1a0`, 0x138 bytes)

Allocated and zeroed by `wlc_channel_mgr_attach`. Fields used here (names
proposed; `b` = band unit 0 = 2.4 GHz, 1 = 5 GHz):

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0x00 | 8 | `pub` | |
| 0x08 | 8 | `wlc` | |
| 0x10 | 4 | `srom_ccode` | country code from the SROM/NVRAM (up to 3 chars, NUL-terminated) |
| 0x14 | 4 | `srom_regrev` | regulatory revision from the SROM (`regrev`), after the board fix-ups |
| 0x18 | 4 | `country` | the CLM country id in use (`clm.md`) |
| 0x1c | 4 | `ccode` | the code that was looked up (after the aggregate mapping) |
| 0x20 | 4 | `regrev` | its revision |
| 0x24 | 4 | `country_abbrev` | the code the driver advertises (CLM advertised code, else `ccode`) |
| 0x28 + 0x30b | 1 | `locale_flags[b]` | the flag byte of `wlc_get_flags` for band b (channel.md C4) |
| 0x29 + 0x30b | 28 | `valid[b]` | valid 20 MHz channels of band b (bit c = channel c) |
| 0x48 + 0x30b | 8 | `radar[b]` | pointer to the radar channel vector (both bands point to `cm+0xcc`) |
| 0x50 + 0x30b | 4 | `chain_limit[b][4]` | per-chain power limits, s8; 0x7f = none (set to 0x7f at attach; only `wlc_channel_band_chain_limit`, which has no caller, changes them) |
| 0x88 | 28 | `quiet` | quiet channel vector (channels on which the station may not transmit first) |
| 0xcc | 28 | `radar_vec` | radar (DFS) channels of the country |
| 0xe8 | 28 | `restricted` | restricted channels of the country (CLM "restricted" set, both bands ORed) |
| 0x104 | 1 | `has_restricted` | some channel is restricted |
| 0x108, 0x110, 0x118 | 8 each | regulatory class tables | pointers into .rodata, chosen by the country (information elements only) |
| 0x120 | 1 | `filter_war` | set at attach to (chip id = 0x4331): 0 on this chip |
| 0x121 | 1 | `sar_enable` | SAR limit enabled (iovar `sar_enable`); 0 after attach |
| 0x122 | 17 | `regclass_vec` | bit vector of the regulatory classes in use (information elements only) |

### Fields of other structures

| Field | Meaning (as used here) |
|---|---|
| `pub+0x18` | `osh` |
| `pub+0x34` | up |
| `pub+0x44` | number of bands (2) |
| `pub+0x59` | 802.11h spectrum management enabled (0 in the session) |
| `pub+0x68` bits 0..1 | 40 MHz (N-mode) capability |
| `pub+0x6a` | VHT enabled |
| `pub+0x84` | radio-disable bits; bit 3 (0x08) = "no valid channel in this country" |
| `pub+0x94` | SROM revision (11 on this card) |
| `pub+0xc8` | 802.11h (radar) handling enabled |
| `pub+0xca` | regulatory limits of an invalid chanspec from the auto-country default (0 in the session) |
| `pub+0xe8` | TXBF capable (1 on this card) |
| `pub+0x100` | `sih`: `+0x28` board type, `+0x30` board vendor (0x106b = Apple), `+0x3c` chip id |
| `pub+0x108` | the variable buffer (`getvar`) |
| `pub+0x140` | OLPC enabled (gate of `wlc_olpc_eng_reset`) |
| `wlc+0x40`, `wlc+0x50 + 8b` | current band state, band state of unit b |
| band `+0x00` | band type: 1 = 5 GHz, 2 = 2.4 GHz |
| band `+0x04` | unit |
| band `+0x10` | `pi` |
| band `+0xf1` | bandwidth capability bits (0x02 40 MHz, 0x04 80 MHz) |
| band `+0x108` | antenna gain, s8, quarter dB (0 on this card) |
| band `+0x109` | copy of `+0x110`, written by every `wlc_channel_reg_limits` |
| band `+0x110` | SAR limit of the band, s8 quarter dBm (0x7f on this card) |
| `wlc+0x1b8` | the scan state; its byte `+0x0a` = a scan is in progress |
| `wlc+0x604` | u8 transmit power percentage (ioctl 220/221) |
| `wlc+0x1d0` | the TPC state `tpc`: `+0x0a` s8 local maximum (dBm), `+0x0b` u8 local constraint (dB) |
| `wlc+0x1f0`, `wlc+0x1f8` | the 802.11d and country modules (auto-country default, default country) |
| `wlc+0x516` | the home chanspec (of the BSS the station is associated with) |
| `wlc+0x518` | the current chanspec |
| `wlc+0x550` | the stf state (cca-stf.md): `+0x01` txchain, `+0x02` txstreams `T`, `+0x10` no_cddstbc, `+0x22/+0x23` txcore overrides, `+0x28` spatial policy, `+0x8a` core1_first |
| `wlc+0x820` | the OLPC engine (section 4) |

Values of this card in the emulated session (run, `probe.py`): SROM
revision 11, `srom_ccode` empty, `srom_regrev` 0, country XW/0
(`cm+0x18` = 0x40001495, `ccode` = `country_abbrev` = "XW"),
`locale_flags` 0x05 (2.4 GHz) and 0x00 (5 GHz), valid channels 1..11 and
36..48, 149..165, all of the 5 GHz channels restricted and quiet, no radar
channels, chain limits all 0x7f, antenna gains 0, SAR limits 0x7f,
`sar_enable` 0, `filter_war` 0, `T` = 2, TXBF capable.

## 1. The regulatory limits

### 1.1 Country state at attach: `wlc_channel_mgr_attach(wlc)`

(Read; the session's path run.)

1. Allocate `cm` (0x138 bytes, zeroed); `cm+0` = `pub`, `cm+8` = `wlc`,
   `wlc+0x1a0` = `cm`. `chain_limit[0][0..3]` and `chain_limit[1][0..3]` =
   0x7f.
2. SROM country code: if the SROM revision (`pub+0x94`) is below 2: n =
   `getintvar("cc")`; if n < 10 the code is entry n of the list AU, TH,
   IL, JO, CN, JP, US, DE, US, JP, else empty. From revision 2 on: the
   string `getvar("ccode")` (empty if absent). At most 3 characters are
   copied to `srom_ccode`.
3. `srom_regrev` = `getintvar("regrev")` (0 if absent). Board fix-ups, all
   only for board vendor 0x106b (Apple): board type 0x90 or 0x8b with
   regrev 2 and code "KR": regrev 3; board type 0x93 with code "JP" and
   regrev 4: regrev 6; board type 0x135 with code "X0", "X2" or "X3":
   regrev 19.
4. `clm_init(&clm_header)` (clm.md).
5. Look the SROM code up (`wlc_country_lookup`, 1.2). If that fails, the
   code becomes "US" ("XV" for board vendor 0x1028 with chip 0x4352) and is
   looked up again (result ignored).
6. `wlc_cntry_set_default(wlc+0x1f8, code)`; auto-country default
   `wlc_11d_set_autocountry_default(wlc+0x1f0, d)` with d = the code if it
   is one of XY, XA, XB, X0, X1, X2, X3, XS, XV, else "XV".
7. `filter_war` = (chip id = 0x4331). Chip ids other than the supported
   list (0x4360 is in it) end the attach with an error.
8. Unless the board vendor is 0x103c (HP) and the code is US, JP or IL: a
   code whose first character is not 'X' is replaced by **"XW"**.
9. `wlc_11d_set_autocountry_default(wlc+0x1f0, code)` again, then
   `wlc_set_countrycode(cm, code)` (1.3).

So on this card (empty `ccode`) the lookup of "" fails, the code becomes
"US", step 8 turns it into "XW", and the driver runs on XW/0 (run: the
CLM calls of the session, clm.md "sesslog"). A card whose SROM carries a
country that does not start with 'X' gets XW as well; only the X codes
(Broadcom's worldwide aggregates) and HP's three codes survive. The Linux
glue does not set a country in this driver; the country changes only
through the `country` ioctl/iovar, 802.11d (`wlc_11d_adopt_country`, from a
received country element when 802.11d is on) and the CLM download
(`wlc_handle_clm_dload`), all through `wlc_set_countrycode[_rev]`.

### 1.2 Country lookup: `sub_06f468(cm, code, mapped, &rev, &id)`

`wlc_country_lookup(wlc, code, &id)` is this with its own buffers. (Read;
run for "", "US", "XW".)

1. If the code is 4 or more characters long: result 1 (failure).
2. `mapped` = code, `rev` = 0.
3. Buffalo boards (vendor 0x1154) with MAC core revision > 5 asking for
   "JP": try "J10"/0 first; done if found.
4. If code = `srom_ccode`: `rev` = `srom_regrev`. Otherwise look for an
   aggregate of the SROM's code (`clm_agg_country_lookup(srom_ccode or
   "ww" if empty, srom_regrev & 0xff)`); if one exists,
   `clm_agg_country_map_lookup(agg, code, &rev)` gives the revision of
   `code` in it ("mapped by the aggregate").
5. If the code is "JP", it was not mapped by an aggregate, `rev` = 0, the
   MAC core revision is > 5, and (`srom_ccode` is not "JP" or the SROM
   revision is below 3): `rev` = 1.
6. `clm_country_lookup(mapped, rev, &id)`; if that fails and `rev` != 0:
   `rev` = 0 and look up again. The result is the CLM result (0 = found).

### 1.3 Setting a country: `wlc_set_countrycode_rev(cm, code, rev)`

`wlc_set_countrycode(cm, code)` = this with `rev` = -1. (Read; run once per
attach.)

1. `rev` = -1: the lookup of 1.2. Otherwise `clm_country_lookup(code, rev)`
   directly (`mapped` = code).
2. Not found: result -2, nothing changes.
3. `abbrev` = `clm_country_advertised_cc(id)` if that succeeds, else the
   code.
4. `sub_072da4(cm, abbrev, mapped, rev, id)`:
   1. `country` = id, `country_abbrev` = abbrev (the old one kept), `ccode`
      = mapped, `regrev` = rev.
   2. `clm_country_def(id, &loc)`; f = `wlc_get_flags(&loc, 2.4 GHz)`
      (1.5).
   3. If TXBF capable (`pub+0xe8`): `wlc_stf_set_txbf(wlc, f & 0x40 != 0)`.
   4. If f has 0x10 (no HT rates on 2.4 GHz) and the device has two bands
      or is 5 GHz only: f = `wlc_get_flags(&loc, 5 GHz)`.
   5. f & 0x10: `wlc_set_nmode(wlc, 0)` and `stf+0x10` (no_cddstbc) = 1.
      Else `stf+0x10` = 0 and `wlc_prot_n_mode_reset(prot_n, 0)`.
   6. `wlc_stf_ss_update(wlc, band)` for both band states.
   7. `sub_072656(cm, id)`: section 1.4.
   8. If the old abbrev had 2 or more characters and the new one differs:
      `sub_06f334(cm, wlc+0x518)` (3.1) and the event
      `WLC_E_COUNTRY_CODE_CHANGED` (0x2f) with the new abbrev.
5. `wlc_ol_curpwr_upd(wlc+0x750, 0, 0)` (offload engine, a service); if
   OLPC is enabled (`pub+0x140`): `wlc_olpc_eng_reset(wlc+0x820)` (4.2).
   Result 0.

### 1.4 Committing the country's channels: `sub_072656(cm, id)`

(Read; the state it leaves was compared after attach, run.) For the band
state of the current band first, then the other (`pub+0x44` bands):

1. Before the loop: `restricted` and `radar_vec` cleared.
2. Per band b: `clm_country_def(id)`; `locale_flags[b]` = `wlc_get_flags`
   of the band; `wlc_locale_get_channels(loc, band, valid[b], tmp)`
   (`clm_country_channels`: the valid and the restricted set); `tmp` ORed
   into `restricted`; `radar[b]` = `&radar_vec`; for 5 GHz with flag 0x02
   (a DFS type) `radar_vec` = `valid[1]` AND the channels 52..64 and
   100..140; then `valid[b]` is fetched again and ANDed with the PHY's
   channel list (`wlc_phy_chanspec_band_validch(pi, bandtype)`,
   acphy-api A4).
3. `has_restricted` = any bit of `restricted`.
4. `wlc_quiet_channels_reset(cm)`: `quiet` = `restricted`; if 802.11h is
   on (`pub+0xc8`), the radar vector is ORed in (once per band).
5. The first channel 0..223 valid in either band (`wlc_valid_channel20_db`,
   channel.md V3): none: `pub+0x84` |= 0x08 (the radio stays off: no valid
   channel); else that bit is cleared.
6. With two bands or the current band 2.4 GHz:
   `wlc_phy_chanspec_ch14_widefilter_set(pi, abbrev is "JP" or "J1".."J9")`
   (no effect on the AC-PHY, acphy-api A3).
7. **If a valid channel exists and the driver is up** (`pub+0x34`): p =
   `ppr_create(bw of wlc+0x518)`, `wlc_channel_reg_limits(cm, wlc+0x518,
   p)`, `ppr_apply_max(p, 127)`, **`wlc_phy_txpower_limit_set(pi, p,
   wlc+0x518)`**, `ppr_delete`. So a country change while up re-limits the
   current channel at once, without the local constraint and without a
   channel change.
8. Regulatory classes (information elements only): the class tables
   `cm+0x108/0x110/0x118` by the abbrev (US/Q1/Q2/ALL; Japan; others) and
   `regclass_vec` from `wlc_get_regclass` of every valid 20, 40 and 80 MHz
   chanspec, with the bandwidth capability bits `band+0xf1` 0x02/0x04 set
   for the duration and restored after.

### 1.5 The flag byte: `wlc_get_flags(loc, band, &f)`

`clm_country_flags(loc, band, &F)` (clm.md, 64 bits); result = its result;
f = 0 and then, if it succeeded (read; run: f = 0x05 for XW 2.4 GHz, 0x00
for XW 5 GHz):

| F | f |
|---|---|
| bits 0..1 (DFS type) = 1 | 0x22 |
| bits 0..1 = 2 | 0x02 |
| bit 2 | 0x80 (the filter workaround of 1.6 step 6) |
| bit 3 | 0x40 (TXBF allowed, 1.3 step 4.3) |
| bit 29 (no HT/VHT rates) | 0x10, and then bits 30/31 are not looked at |
| bit 29 clear: bit 30 (no 40 MHz) | 0x08 |
| bit 29 clear: bit 31 (no 80 MHz) | 0x04 |
| 2.4 GHz: bit 28; 5 GHz: bit 27 (an EIRP group with DSSS / OFDM rates) | 0x01 |

These are the `locale_flags` the validity checks read (channel.md V4, V5).

### 1.6 The regulatory container: `wlc_channel_reg_limits(cm, c, p)`

Input: chanspec `c`, a `ppr_t` `p` that the caller created for the
bandwidth of `c` (0 = 20, 1 = 40, 2 = 80 MHz). Output: `p`. No result. The
procedure (run: rebuilt from the recorded `clm_limits` outputs, see
Verification):

1. `ppr_clear(p)` (every rate disabled). `clm_limits_params_init(&par)`;
   failure: return.
2. Country: if `c` is valid (`wlc_valid_chanspec_db`, channel.md V6 with
   `dualband`), the country id is `cm->country`. Otherwise: if
   `pub+0xca` is 0, return (**`p` stays all disabled**); else the country
   of the auto-country default (`wlc_11d_get_autocountry_default` looked
   up with `wlc_country_lookup`); return if that fails.
3. `bs` = the band state of `c`'s band (`wlc+0x50 + 8 * (c is 5 GHz)`);
   CLM band = 1 if `bs+0x00` = 1 (5 GHz), else 0. Antenna gain g =
   `bs+0x108` (s8). `bs+0x109` = `bs+0x110`. `par.sar` = 0x7f, or the
   signed byte `bs+0x110` when `sar_enable` (`cm+0x121`) is set.
4. `clm_country_def(country, &loc)`; failure: return.
5. f = `wlc_get_flags(&loc, CLM band)`; failure: return.
6. Filter workaround: `wlc_bmac_filter_war_upd(wlc_hw, w)` ->
   `wlc_phy_set_filt_war(pi, w)` with w = 1 only if `filter_war`
   (`cm+0x120`) is set, f has 0x80 and the channel byte of `c` is 1 or 13;
   else 0. Called on every pass through this step (no effect on the
   AC-PHY, acphy-api A3; run: always 0).
7. The CLM queries, all with `ant_gain` = g, `par.sar` from step 3,
   `par.category` 0, the channel byte of `c` (the centre channel):

   | `c` bandwidth | `par.bw` | query W (whole channel) | query S (sub-channel) | query H (40 MHz half) |
   |---|---|---|---|---|
   | 20 MHz | 0 | type 0 | - | - |
   | 40 MHz | 1 | type 0 | type 2 (upper) if the sideband is 0x100, else 1 (lower) | - |
   | 80 MHz | 2 | type 0 | type 3, 4, 5, 6 for the sidebands 0x000, 0x100, 0x200, 0x300 (the 20 MHz part holding the control channel: LL, LU, UL, UU) | type 2 (upper half) for the sidebands 0x200, 0x300, else 1 |

   Order: W, then H (80 MHz), then S. If any query does not return 0, the
   function returns at once: `p` keeps what was copied before, which is
   nothing (the copying starts after the last query), so **`p` stays all
   disabled**.
8. Copy into `p`. The CLM output is indexed by the CLM rate index
   (clm.md; groups of 12 entries for MCS, of which the first 10 are VHT
   MCS 0..9); each copied group goes to the `ppr_t` group named by
   (selector, chains, mode, nss), `ppr.md`. The groups, with the CLM index
   of their first rate, and when each is copied (`T` = `stf+0x02`
   txstreams; "TXBF" = `pub+0xe8` set):

   | CLM index | ppr group | copied when |
   |---|---|---|
   | 0 (4) | DSSS, 1 chain | always (see below) |
   | 4 (8) | OFDM, 1 chain | always |
   | 12 (10) | VHT MCS 0..9, 1 stream, mode none, 1 chain | always |
   | 24 (4) | DSSS, 2 chains | T >= 2 |
   | 28 (8) | OFDM CDD, 2 chains | T >= 2 |
   | 36 (10) | VHT 1 stream CDD, 2 chains | T >= 2 |
   | 48 (10) | VHT 2 streams STBC, 2 chains | T >= 2 |
   | 60 (10) | VHT 2 streams SDM, 2 chains | T >= 2 |
   | 104 (4) | DSSS, 3 chains | T >= 3 |
   | 108 (8) | OFDM CDD, 3 chains | T >= 3 |
   | 116 (10) | VHT 1 stream CDD, 3 chains | T >= 3 |
   | 128 (10) | VHT 2 streams STBC, 3 chains | T >= 3 |
   | 140 (10) | VHT 2 streams SDM, 3 chains | T >= 3 |
   | 152 (10) | VHT 3 streams SDM, 3 chains | T >= 3 |
   | 72 (8) | OFDM TXBF, 2 chains | TXBF and T >= 2 |
   | 80 (10) | VHT 1 stream TXBF, 2 chains | TXBF and T >= 2 |
   | 92 (**8**) | HT MCS 0..7, 2 streams TXBF, 2 chains (`ppr_set_ht_mcs`: the group's MCS 8, 9 stay disabled) | TXBF and T >= 2 |
   | 164 (8) | OFDM TXBF, 3 chains | TXBF and T >= 3 |
   | 172 (10) | VHT 1 stream TXBF, 3 chains | TXBF and T >= 3 |
   | 184 (10) | VHT 2 streams TXBF, 3 chains | TXBF and T >= 3 |
   | 196 (**8**) | HT MCS 0..7, 3 streams TXBF, 3 chains (MCS 8, 9 stay disabled) | TXBF and T >= 3 |

   Which output goes to which selector of `p` (selector numbers of
   `ppr.md`: 0 = 20, 1 = 40, 2 = 80, 3 = 20in40, 4 = 20in80, 5 = 40in80):

   | `c` bandwidth | W | H | S |
   |---|---|---|---|
   | 20 MHz | selector 0, all groups | - | - |
   | 40 MHz | selector 1, all groups **except the DSSS ones** | - | selectors 0 and 3 (the same set of a 40 MHz container), all groups |
   | 80 MHz | selector 2, all groups except DSSS | selector 5, all groups except DSSS | selectors 0 and 4 (the same set), all groups |

   So the DSSS rates of a wide container are those of the 20 MHz
   sub-channel; the DSSS groups of the 40 and 80 MHz sets and of the
   40in80 set stay disabled. No arithmetic is done: the bytes are copied
   as `clm_limits` gave them (the antenna gain and SAR limit are applied
   inside `clm_limits`, clm.md step 7). Rates of no copied group stay -128.
   (The object writes the 1-chain OFDM group of the W set twice;
   the second write has no effect.)

### 1.7 Where the container goes

| Caller | When | What it does with `p` before the PHY |
|---|---|---|
| `wlc_channel_set_chanspec(cm, c, q)` | every channel change and `wlc_init` (wlc-hw 2.2) | `ppr_apply_max(p, q)` (q: 2.2), `sub_0736a4`, then `wlc_bmac_set_chanspec(..., p)` -> `wlc_phy_txpower_limit_set` |
| `sub_072656` | country set while up (1.4 step 7) | `ppr_apply_max(p, 127)`, `wlc_phy_txpower_limit_set` |
| `qtxpower` set (2.1) | iovar | `ppr_apply_max(p, 127)`, `wlc_phy_txpower_set(pi, v, ovr, p)` |
| `wlc_channel_set_txpower_limit(cm, q)` | the local constraint changed (2.2), `wlc_cntry_adopt_country_ie` at a join, the ioctl `WLC_SET_PWROUT_PERCENTAGE` (2.3) | `ppr_apply_constraint_total_tx(p, q)` (ppr.md: per-chain-count caps q, q-12, q-19), `sub_0736a4`, `wlc_phy_txpower_limit_set(pi, p, wlc+0x518)`; result 0, or -1 if the container could not be created |
| `wlc_channel_update_txpwr_limit(wlc)` | end of an OLPC round (4.5); the LCN40-PHY's request service (not the AC-PHY) | no cap: `wlc_phy_txpower_limit_set(pi, p, wlc+0x518)` |
| `sub_0837c5` | OLPC decision (4.3) | not handed to the PHY |
| `wlc_channel_band_chain_limit` | no caller in the object | - |

Each caller creates `p` for the bandwidth of the chanspec it uses
(`wlc+0x518` or `c`: 2 for 80 MHz, 1 for 40 MHz, else 0) and deletes it
after; nothing is kept between calls. (Read; the first row and the
`qtxpower` row run.)

## 2. The user and platform caps

### 2.1 `qtxpower`: the user's target

The iovar `qtxpower` (`wlc_doiovar`, id 0x2f get / set as action ids 0x5e
/ 0x5f; read, the set run):

* **Set** with a 32-bit value `v`: `q` = `v & 0x7fffffff`, at most 255;
  `override` = bit 31 of `v`. The regulatory container of the current
  chanspec `wlc+0x518` is built (1.6) and capped with `ppr_apply_max(p,
  127)`, then `wlc_phy_txpower_set(pi, q & 0xff, override, p)`; the result
  of the iovar is the PHY's result (acphy-api-txpower: `q` >= 0x80 is
  refused there and nothing changes). The PHY stores `q` as its user
  target `pi+0x1d6` and, only if the interface is up and no scan holds the
  PHY, runs its merge at once with `p` as the regulatory input. The
  `override` bit is not used by the AC-PHY.
* **Get**: `wlc_phy_txpower_get(pi, &q, &ovr)`; the value is `q`, with bit
  31 set if `ovr`.

The Linux glue sets `qtxpower` to 92 (23 dBm) once in `wl_attach`, before
the interface is up (run: `wlc_phy_txpower_set(pi, 92, 0, p)` with the
2.4 GHz channel 1 container of XW; the PHY only stores 92). The cfg80211
glue sets it again from `set_tx_power`: `min(dBm * 4, 127) | 0x80000000`
for a limited or fixed power, 127 for "automatic" (no user cap: every
regulatory limit of the database is below 127).

### 2.2 The 802.11h local power constraint

`q` = `wlc_tpc_get_local_constraint_qdbm(tpc)` (read; run: 127 at every
channel change of the session):

1. If the local maximum `tpc+0x0a` is -128, or spectrum management is off
   (`pub+0x59` = 0), or the control channel of the current chanspec
   (`wlc+0x518`) differs from that of the home chanspec (`wlc+0x516`):
   127.
2. Else d = 4 * (local maximum) - 4 * (local constraint `tpc+0x0b`, u8),
   as a 16-bit number: 1..126 -> d; negative -> 0; 0 or 127 and above ->
   127.

The two bytes: `wlc_tpc_reset_all` (at `wlc_BSSinit`) sets 127 and 0;
`wlc_cntry_adopt_country_ie` (at the end of a join, `wlc_join_complete`)
parses the AP's country element (`wlc_cntry_parse_country_ie`), adopts the
country through 802.11d (`wlc_11d_adopt_country`), and, with 802.11h on
(`pub+0xc8`) and spectrum management on (`pub+0x59`), if the home control
channel is among the element's channels, sets the local maximum to the
element's maximum power for that channel (`wlc_tpc_set_local_max`) and
calls `wlc_channel_set_txpower_limit(cm, q)`;
`wlc_tpc_set_local_constraint(tpc, ie)` (from a received power-constraint
element, `ie[1]` length, `ie[2]` the constraint in dB) stores a changed
constraint and, when up, with spectrum management on and the current
chanspec equal to the home chanspec, calls
`wlc_channel_set_txpower_limit(cm, q)`.

Two different caps are made of the same `q`: at a channel change
`ppr_apply_max(p, q)` (every rate at most `q`), in
`wlc_channel_set_txpower_limit` `ppr_apply_constraint_total_tx(p, q)`
(1-chain groups at most `q`, 2-chain groups at most `q` - 12, 3-chain
groups at most `q` - 19: the total over the chains at most `q`; ppr.md).
So after a constraint arrives the limits differ until the next channel
change, which applies the per-rate cap instead. (Read.)

### 2.3 The output percentage

The ioctl `WLC_SET_PWROUT_PERCENTAGE` (221; 220 reads it back): a value
above 100 is refused; else `wlc+0x604` = value,
`wlc_bmac_set_txpwr_percent(wlc_hw, value)` (-> `wlc_phy_txpwr_percent_set`,
`pi+0x184`, applied by the PHY's merge as `ppr_multiply_percentage`), and,
when up and no scan is in progress (`wlc+0x1b8` byte `+0x0a`): MAC
suspended, `wlc_channel_set_txpower_limit(cm, q)` with the current local
constraint, MAC enabled. (Read; the glue does not use it.)

### 2.4 Per-chain offsets: `sub_0736a4(cm, p)` - no effect in this driver

Called after the cap by `wlc_channel_set_chanspec` and
`wlc_channel_set_txpower_limit`. (Read; run: the iovar result below at
every call of the session.)

1. Only for PHY types 11 and 7 (else result -23).
2. o[0..3] = 0. If any `chain_limit[b][i]` of the current band is not
   0x7f (never, see Data): with a container S filled with the SROM's
   minimum power (`sub_06ef19`: `wlc_phy_txpower_sromlimit(pi, c, &min,
   NULL, 0)`, every rate = min), the margin m = the smallest unsigned
   difference `p[r] - S[r]` over all rates (callback `sub_06ee40`, start
   0xff), `cap` = m - 6 if m > 5 else 0; then S = the SROM limits of core
   0 for the channel, S = min(S, p) per rate, `M` = the largest value of
   the channel's own bandwidth set of S; for each chain i with
   `chain_limit[b][i]` < `M`: o[i] = -min(`M` - `chain_limit[b][i]`,
   `cap`).
3. `wlc_iovar_op(wlc, "txchain_pwr_offset", get)`; if that succeeds and
   the value differs from o: set it to o. **No module of this driver
   registers `txchain_pwr_offset`**: the get returns -23 (unsupported) and
   nothing is set. The function makes no other change; `p` is not
   modified.

An implementation leaves it out.

### 2.5 How the caps combine (summary)

For a channel `c`, after `wlc_phy_txpower_limit_set(pi, p, c)` the PHY's
merge (acphy-api-txpower.md, "Further corrections") computes for every
rate r and core k:

    A_k[r] = min( qtxpower,
                  max0( min(SROM_k[r], REG[r]) - txpwrbckof ) )

with `REG` = the container of 1.6 after the cap of section 2.2 (or 127),
`max0(x)` = x - backoff floored at 0 for enabled rates, then the
percentage and `ppr_force_disabled(A_k, minpower)`; the target of core k
is the largest `A_k[r]`, the per-rate offsets handed back to the MAC are
`target - A_k[r]` (largest over the cores). So the user cap and the
regulatory limits act per rate as a minimum, and the user's `qtxpower`
never raises a rate above its regulatory or board limit. A rate disabled
in `REG` is disabled in the PHY too (`ppr_compare_min` with -128).

## 3. What else of the channel manager reaches the hardware

### 3.1 `sub_06f334(cm, c)`: the AFE override and `core1_first`

Called by `wlc_channel_set_chanspec` (every channel change, before the
regulatory limits) and by `sub_072da4` when the advertised country changes
(1.3 step 4.8). (Read; run: in every call of the session both iovar calls
return -23 and `stf+0x8a` stays 0.)

1. v = `wlc_iovar_getint(wlc, "phy_afeoverride")`. No module of the
   driver's iovar dispatcher (iovar.md) has this name, so the call returns
   -23 (run) and **leaves v unwritten** (an int on the stack the object never
   initialises; the same pattern as `min_txpower`, cca-stf.md "The
   uninitialised stack read").
2. v &= ~0x02; v |= 0x02 if `c` is a 5 GHz chanspec, the board type
   (`sih+0x28`) is 0x135 and the first two characters of
   `country_abbrev` and the revision `regrev` (`cm+0x20`) are "CN" with 40
   or "X2" with 19.
3. `wlc_iovar_setint(wlc, "phy_afeoverride", v)`: -23 again, nothing
   reaches the PHY (`pi+0x1169` keeps 0, acphy-api A11).
4. `stf+0x8a` (`core1_first`) = (v != 0) for a 20 MHz `c`, else 0. This
   byte is read by `sub_172af1` (cca-stf.md): a one-stream rate class then
   prefers transmit core 1, which reaches `SHM(0x5d4..)` through the
   spatial policy.

An implementation takes the unwritten int as 0: then `core1_first` = 1
exactly for 20 MHz 5 GHz channels on an Apple board of type 0x135 with the
country X2/19 or CN/40 (attach makes X0/X2/X3 regrev 19 on that board
type, 1.1 step 3), else 0. With the object the byte depends on what the
kernel stack held.

### 3.2 Valid, restricted, quiet and radar channels

Built by `sub_072656` (1.4). What of them reaches the hardware:

* **Mute on quiet channels.** `wlc_channel_set_chanspec` asks
  `wlc_quiet_chanspec(cm, c)` (a 20 MHz part of `c` in `quiet`) and hands
  the answer to `wlc_bmac_set_chanspec` as `mute` (wlc-hw.md 2.2 step 6,
  bmac-init E4: transmit FIFOs suspended). `quiet` starts as the country's
  restricted set (plus, with 802.11h on, its radar channels) after every
  country commit and on `wlc_down`/`wlc_clear_hw_association`/
  `wlc_11h_set_spect` (`wlc_quiet_channels_reset`); a channel's bit is
  cleared when a beacon is heard there (`wlc_clr_quiet_chanspec`, join
  path) and set again by `wlc_set_quiet_chanspec` (channel switch, AP
  lost). In the session (XW) every 5 GHz channel is restricted, so every
  5 GHz channel is entered muted. For mac80211 this is the
  "no initiating radiation" (passive) flag of a channel.
* **No valid channel**: `pub+0x84` bit 3; the driver keeps the radio off.
* The validity checks that use `valid[b]` and `locale_flags[b]` decide
  which chanspecs the driver accepts (channel.md); `wlc_set_chanspec`
  refuses others (-20: run, channels 13, 100 and 120/40 under XW).

### 3.3 The initial channel: `wlc_default_chanspec`, `wlc_next_chanspec`

`wlc_next_chanspec(cm, cs, kind, dualband)` (read; run as part of attach
and `wlc_init`): starting after the channel number of `cs` and wrapping at
224, the first channel n (in the band, bandwidth and sideband of `cs`)
whose chanspec is valid (`wlc_valid_chanspec`, channel.md V6) and, for
kind 1, not quiet, for kind 2, quiet (kind 0: any); if none and `dualband`
is set, the same over both bands with `wlc_valid_chanspec_db` (a channel
above 14 gets the 5 GHz band bits); none at all: 0xff.
`wlc_default_chanspec(cm, flag)` = `wlc_next_chanspec(cm,
wlc_create_chspec(wlc, 0), 0, 0)`, and if that finds nothing and `flag`
is set: `wlc_phy_chanspec_band_firstch(pi, bandtype)`. With XW the first
chanspec is 2.4 GHz channel 1 (0x1001), the channel of the first `up`
(run).

### 3.4 Calls into the PHY that have no effect on this chip

`wlc_phy_set_filt_war(pi, w)` (1.6 step 6; w is always 0 here: the flag
`cm+0x120` is set for chip 0x4331 only) and
`wlc_phy_chanspec_ch14_widefilter_set` (1.4 step 6) do nothing on the
AC-PHY (acphy-api A3). `wlc_phy_chanspec_band_validch` only answers.

### 3.5 Not hardware

`wlc_get_regclass`, the regulatory class vector and tables, the locale
flag `0x22`/`0x02` DFS bits beyond `radar_vec`, the country abbreviation
and the country-changed event serve information elements and user space.

## 4. The OLPC engine (`wlc_olpc_engine.c`)

"Open-loop power control" calibration: on a channel where some rates must
be sent at a power too low for the PHY's power detector (TSSI) to measure,
the engine sends a burst of frames on each transmit core at full power
(offset 0), so that the PHY's closed loop settles on that channel, and then
hands the power limits to the PHY again. It acts only while associated on
that channel. In the emulated session (never associated) it decides on
every channel but sends nothing and makes no hardware access (run).

### 4.1 State

The engine (`wlc+0x820`, 0x30 bytes, `osl_malloc`ed and zeroed by
`wlc_olpc_eng_attach`, which registers the module "olpc" with the up
function `sub_08432b` and the down function `sub_083767`, and the BSS
up/down callback `sub_084221`):

| Offset | Size | Meaning (names proposed) |
|---|---|---|
| 0x00 | 8 | `wlc` |
| 0x08 | 8 | list of channel records (singly linked) |
| 0x10 | 1 | `up` |
| 0x11 | 1 | set while a calibration frame is being prepared |
| 0x12 | 1 | attached (1) |
| 0x13 | 1 | `npkt`, frames per core: 16 |
| 0x14 | 1 | `ncores`: bits of `stf+0x00` (`hw_txchain`) at up |
| 0x15 | 1 | the stf per-rate state is saved |
| 0x16 | | the saved stf per-rate state (`wlc_stf_txchain_get_perrate_state`) |
| 0x18 | 1 | cached `txchain` of the last decision (0xff after attach) |
| 0x1a | 2 | cached chanspec of the last decision (0xff after attach) |
| 0x1c | 1 | cached decision |
| 0x20 | 8 | the record of the channel being calibrated, or NULL |

A channel record (0x18 bytes, `osl_malloc`): `+0x00` next, `+0x08`
chanspec, `+0x0a..0x0c` frames outstanding per core, `+0x0d` mask of
calibrated cores, `+0x0e` mask of cores in progress, `+0x0f` mask of
cores completed in the current round, `+0x10` frames outstanding in all.
`sub_083a2c(wlc, cs, &err, create)` finds the record of `cs`, or, with
`create`, puts a new zeroed one at the head of the list (err = -27 when
the allocation fails).

### 4.2 Up, down, reset

* **Up** (`sub_08432b`, the module's up function, at every `wlc_up`): if
  not up: `up` = 1, `ncores` = bit count of `hw_txchain`, then the channel
  update (4.4) with force 0.
* **Down** (`sub_083767`): if up: `up` = 0, cached decision 0, cached
  chanspec 0xff, every record freed, current record NULL.
* **Reset** (`wlc_olpc_eng_reset`, from `wlc_set_countrycode_rev` when
  `pub+0x140` is set): if up, down then up.
* **BSS up/down** (`sub_084221`): a BSS coming up runs the channel update
  with force 1. A BSS going down frees the record of its BSS's channel
  (clearing the current-record pointer if it was that one) unless the
  primary BSS is still associated on the current channel.
* **Channel changes**: `wlc_olpc_eng_hdl_chan_update` (the channel update
  with `npkt` and force 0) is called by `wlc_set_chanspec` after every
  channel change (wlc-hw.md 2.4, "OLPC update"), by the scan and radio
  measurement start and end, by `wlc_olpc_eng_hdl_txchain_update` (after
  a change of the transmit chains; it first forgets the saved per-rate
  state if `wlc_stf_saved_state_is_consistent` says it no longer fits)
  and by the frame completion (4.5).

(Read; up, the channel updates and their results run.)

### 4.3 Does a channel need it: `sub_0837c5(eng, cs)`

Result 1 = calibration needed. (Read; run on seven channels with the
inputs observed.)

1. No engine, no band state or no `pi`: 0.
2. If `cs` and `stf+0x01` (`txchain`) equal the cached ones: the cached
   decision.
3. R = the regulatory container of `cs` (`wlc_channel_reg_limits`, 1.6,
   no cap); S = the SROM limits of core 0 for `cs`
   (`wlc_phy_txpower_sromlimit(pi, cs, &min, S, 0)`, which also gives
   `min` = 4 x the PHY's minimum power in dBm); R = min(R, S) per rate
   (`ppr_apply_vector_ceiling`); R = max(R, min) per rate
   (`ppr_apply_min`: **a disabled rate becomes `min`**). Either container
   failing to be created: decision 0.
4. n = bit count of `txchain`. m = 0xffff. If n > 1: m = the smallest
   enabled value of the VHT group "2 streams, mode none (SDM), 2 chains"
   of R at the bandwidth of `cs` (`ppr_get_vht_mcs_min`); if that fails
   or gives -128: m = 0xffff. If n is not 2 (3 cores): the same for the
   group "3 streams SDM, 3 chains", taking it if smaller.
5. Decision = (m - 6 < `wlc_phy_tssivisible_thresh(pi)`) (signed int; m
   still -128 can not occur after step 3). Cache `cs`, `txchain` and the
   decision.

So a channel needs OLPC when its lowest two-stream power (in quarter dB,
less 1.5 dB) is below the lowest power the detector can see. With one
transmit chain it never does. Run (XW, this card's SROM, threshold 20 =
5 dBm): the 2.4 GHz channels 1, 6, 11 give m = 4 (a rate of the group is
disabled there and counts as `min` = 4) and need it; 36, 165, 36/40,
36/80 give m = 40..50 and do not.

### 4.4 The channel update: `sub_083bfd(wlc, npkt, force)`

(Read; in the session it always returned at step 4 or 5.)

1. No engine or not up: -1.
2. "Busy" = a scan is in progress (`wlc+0x1b8` byte `+0x0a`) or the state
   word at (`wlc+0x5b0`)`->+0x08`, offset `+0x0c` (u32), is 4 or above
   (another module's state machine; name not settled).
3. If there is a current record and (its chanspec is not `wlc+0x518` or
   busy) and it has cores in progress: restore the stf per-rate state if
   it was saved (`wlc_stf_txchain_restore_perrate_state`, `+0x15` = 0);
   the record's per-core counts = 0, in-progress = 0, calibrated &= ~
   completed-this-round, completed = 0 (the round is abandoned).
4. If busy: current record = NULL, return 0. If the decision (4.3) for
   `wlc+0x518` is 0: if there is a current record, restore the per-rate
   state if it was saved and set the current record to NULL; return 0.
5. rec = the record of `wlc+0x518` (created if needed; failure: -27);
   current record = rec. If `force`: rec's calibrated mask = 0. If every
   core of `txchain` is calibrated or in progress: return 0. If not
   `force`: return 0 unless the primary BSS (`wlc+0x2f0`) is associated
   (`+0x08`) and its BSS (`+0x318`) has the chanspec (`+0x32`) =
   `wlc+0x518`.
6. k = the cores i < `ncores` of `txchain` neither calibrated nor in
   progress. If `npkt` x `ncores` < k x `npkt` + rec's outstanding count:
   return 0.
7. If the transmit core overrides of the CCK and OFDM classes
   (`stf+0x22`, `stf+0x23`) are not both `txchain`: save the stf per-rate
   state (once, `+0x15` = 1), set both to `txchain` and
   `wlc_stf_spatial_policy_set(wlc, stf+0x28)` (cca-stf.md: the core words
   `SHM(0x5d4..)` follow).
8. For each such core i: `npkt` frames, each: a 31-byte frame (frame
   control 0x0008 = data, duration 0, the three addresses = the own MAC
   address `pub+0x08`, sequence control 0, the rest 0), prepared by
   `wlc_prep80211_raw` (which picks the FIFO), marked "no acknowledgement"
   (`wlc_pkt_set_ack(.., 0)`), transmit core i only (`wlc_pkt_set_core`),
   power offset 0 (`wlc_pkt_set_txpwr_offset(.., 0)`), the low 4 bits of
   the packet tag's flag word replaced by 8; rec's count of core i and its
   total count incremented; `wlc_txfifo(wlc, fifo, frame, ..., 1, 1)`.
   Then core i is marked in progress. A failed allocation ends with -27.
9. Result 0 (or -27).

### 4.5 A calibration frame completes: `wlc_olpc_eng_pkt_complete(wlc, frame)`

Reached from the transmit status of such a frame (through a pointer; no
direct caller). (Read only.)

1. The record of the frame's chanspec (from its transmit header); none:
   return.
2. Total outstanding -1 (not below 0).
3. If the record has cores in progress: the frame's core mask = bits 6..9
   of a 16-bit word of the transmit header information (`wlc_get_txh_info`, the word at offset 10; the core bits of the PHY control); index = 2 for
   mask 4, else mask - 1. That core's count -1 (not below 0); at 0 the
   core is added to calibrated and to completed-this-round and removed
   from in-progress. When no core is in progress any more: restore the
   saved stf per-rate state (if saved), **`wlc_channel_update_txpwr_limit
   (wlc)`** (the regulatory container of `wlc+0x518`, no cap, to
   `wlc_phy_txpower_limit_set`: the PHY's merge and target set-up run
   again), completed = 0.
4. If the total outstanding count is 0: the channel update (4.4) again.

What the engine does to the hardware, in all: the calibration frames
(through the normal transmit path, with a fixed core and power offset 0),
the transmit core overrides with the spatial policy (shared memory core
words) and their restore, and a renewed `wlc_phy_txpower_limit_set` at the
end of a round. It writes no register itself.

## Verification

Object alone in the emulator (`hybrid.Run()` without a core), scripts in
`re-out/analysis/channel-power/`:

| Check | How | Result |
|---|---|---|
| The session's whole power path | `probe.py`: attach, glue, `mpc 0`, up, channels 1, 6, 11, 13, 36, 36/40, 40/40, 36/80, 40/80, 44/80, 48/80, 149/80, 157/40, 165, 100, 120/40, watchdog, down, up, 36/80, watchdog, down; hooks on `clm_limits` (arguments, 336 outputs), `wlc_channel_reg_limits` (output container), `wlc_phy_txpower_limit_set` / `wlc_phy_txpower_set` (container, caller), `ppr_apply_max`, `wlc_channel_set_chanspec` (cap), `wlc_tpc_get_local_constraint_qdbm`, the `txchain_pwr_offset` and `phy_afeoverride` iovar calls, `wlc_phy_set_filt_war`, the OLPC functions, `wlc_set_countrycode_rev`; the channel manager dumped after attach | `probe.json`, `summary.txt` |
| 1.6: the mapping CLM -> container | `rebuild.py`: every container of `wlc_channel_reg_limits` rebuilt from the `clm_limits` outputs of the same call with the tables of 1.6, compared byte by byte | 31 of 31 identical (20, 40 MHz both sidebands, 80 MHz all four sidebands, both bands; callers: channel set, `qtxpower`, OLPC) |
| 1.6 with other states | `variants.py`: direct calls of `wlc_channel_reg_limits` after up with `T` = 1, 2, 3, TXBF on/off, SAR enabled with a band limit of 40, antenna gain 8 on either band, two chanspecs the country does not allow (with and without `pub+0xca`), the filter-war flag set | all 28 rebuilt containers identical; `T` = 1 leaves the 2-chain groups disabled (20 instead of 52 rates on channel 1); SAR: `clm_limits` gets `sar` 40 and every rate is <= 40; gain: `clm_limits` gets 8 (2.4 GHz maximum 76 -> 68); invalid chanspec: container all disabled, no CLM call without `pub+0xca`, a failing one (-1) with it; `filt_war` always 0 under XW |
| 1.7: what reaches the PHY | probe | at every channel change the container the PHY receives equals the regulatory one (cap 127); the glue's `qtxpower` reaches `wlc_phy_txpower_set(pi, 92, 0, p)` |
| 1.1, 1.3: country at attach | probe | one `wlc_set_countrycode_rev("XW", -1)`; `cm` fields as listed in Data |
| 2.2 | probe | the local constraint is 127 at every channel change |
| 2.4 | probe | `txchain_pwr_offset` get: -23 at every call, never set |
| 3.1 | probe | `phy_afeoverride` set: -23 at every call (v = 0 in the emulator) |
| 4.3 | `variants.py`: the decision recomputed on 7 chanspecs with the cache cleared, hooks on `ppr_get_vht_mcs_min` and `wlc_phy_tssivisible_thresh` | m = 4 on 1, 6, 11 (decision 1), 40, 50, 50, 48 on 36, 165, 36/80, 36/40 (decision 0); threshold 20 |
| 4.2, 4.4 | probe | the channel update runs at up and after every channel change, result 0, no frame sent (not associated) |

Read from the code only: the CLM lookup branches of 1.2 other than the
session's ("", "US", "XW"), the board fix-ups of 1.1, the auto-country
replacement (1.1 step 8) for other codes, 1.4 steps 5 to 8, the
3-chain and TXBF groups of 1.6 (the XW database gives -128 for every one
of them, so the rebuild could not tell copied from not copied: their
gating is read only), the `qtxpower` get, 2.2 beyond the value 127, 2.3,
2.4 step 2, 3.1 step 2, 3.3, and the OLPC engine beyond the decision and
the early returns (4.4 steps 5 to 9, 4.5).

## Open questions

1. **The real card's country.** The emulated card has no `ccode` and
   becomes XW/0. The real card's SROM `ccode`/`regrev` and board type
   decide 1.1 steps 3 and 8 (an Apple board 0x135 with X0/X2/X3 gets
   regrev 19, and then `core1_first` of 3.1 on 5 GHz 20 MHz channels).
   They are in the SROM image (`srom-otp.md`, words 75/76); a dump of the
   card's variables settles it.
2. **Unwritten `phy_afeoverride` int** (3.1): as with `min_txpower`, the
   object reads an int it never wrote; 0 is the defined choice, but the
   object on the card may have set `core1_first` on 20 MHz channels from
   stack contents. A capture of `SHM(0x5d4..)` on a 20 MHz 5 GHz channel
   would show it.
3. The 3-chain and TXBF rows of the table in 1.6 could not be checked
   against the object with this database (all disabled under XW); a
   country whose locale enables TXBF rates (CLM flags) would.
4. The second "busy" condition of 4.4 step 2 (the module at `wlc+0x5b0`)
   is not identified.
5. For the mac80211 driver the regulatory input will come from cfg80211;
   whether to keep Broadcom's database and the XW locale (as this
   specification describes) or to derive the container from cfg80211's
   rules is a design decision outside this specification.
