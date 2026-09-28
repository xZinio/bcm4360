# Setting the channel on the AC-PHY of the BCM4360

Status: all procedures below were read in the decompiler output and checked in
the disassembly; what was also checked against emulator traces is listed in
"Verification". Register *names* are not known; aliases in parentheses are mine.
Sections 1 to 12 are the assignment `acphy-chanspec`; the annex (A1 to A7)
describes functions of the assignments `acphy-rxgain` and `acphy-desense` that
the channel function calls, as far as they were needed to make the sequence
of a channel change complete.

## Scope

The top level of tuning the card to a channel (band, centre frequency,
bandwidth, position of the control channel): what the MAC layer does around the
PHY's channel function, the channel function of the AC-PHY itself and its
helpers. Chip 0x4360 (0x4352, 43460 = 0xa9c4 and 43526 = 0xaa06 take the same
branches unless said otherwise), MAC core revision 42, AC-PHY revision 0 or 1,
radio 2069 revision 3 or 4 (major revision 0), two chains.

Register access notation: `access.md`. Already specified in `acphy-radio.md` and
only referenced here: the channel table and its lookup `sub_08e9b1` (section
10 there), the radio tuning that is inlined in the channel function (section
11), `wlc_2069_rfpll_150khz` (12), the VCO calibration `sub_09311b` /
`sub_093e47` (13), the AFE calibration `sub_096203` (14).

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_set_chanspec` | 0x03a4be | 635 | original (wlc.c); only the order of its calls |
| `sub_03649a` | 0x03649a | 116 | assigned: `wlc_set_phy_chanspec` |
| `wlc_channel_set_chanspec` | 0x07419b | 190 | original (wlc_channel.c); only the order of its calls |
| `wlc_bmac_set_chanspec` | 0x067918 | 984 | original; contains the inlined band switch (assigned: `wlc_bmac_setband`) |
| `wlc_setxband` | 0x064fb5 | 81 | original |
| `wlc_bmac_bw_set` | 0x06607f | 137 | original |
| `wlc_bmac_bw_reset` | 0x065970 | 62 | original |
| `wlapi_bmac_bw_set` | 0x152a63 | 17 | original |
| `wlc_phy_clk_bwbits` | 0x0b1913 | 86 | original |
| `wlc_phy_chanspec_set` | 0x0b5d48 | 154 | original |
| `wlc_phy_chanspec_shm_set` | 0x0b5cf4 | 84 | original |
| `wlc_phy_chanspec_radio_set`, `wlc_phy_chanspec_get` | 0x0b1c8a, 0x0b1c97 | 13, 13 | original |
| `wlc_phy_bw_state_set`, `wlc_phy_bw_state_get` | 0x0b1c7d, 0x0b1c70 | 13, 13 | original |
| `sub_0a7089` | 0x0a7089 | 14073 | `wlc_phy_chanspec_set_acphy` (guide) |
| `wlc_phy_get_chan_freq_range_acphy` | 0x08edad | 326 | original |
| `wlc_phy_chanspec_bandrange_get` | 0x0b964a | 191 | original (wlc_phy_cmn.c); AC-PHY path |
| `wlc_phy_get_rxgainerr_phy` | 0x0b20bd | 292 | original (wlc_phy_cmn.c) |
| `sub_08f086` | 0x08f086 | 452 | assigned: `wlc_phy_rxgainctrl_encode_gain_acphy` |
| `sub_0995ab` | 0x0995ab | 94 | assigned: `wlc_phy_btc_txpwr_core_offset_acphy`; does nothing on the 4360 |
| `wlc_phy_get_spurmode` | 0x08e780 | 561 | original; not reached on the 4360 |
| `wlc_phy_setup_spurmode` | 0x097e4a | 60 | original; not reached on the 4360 |
| `wlc_phy_set_spurmode` | 0x097e86 | 73 | original; not reached on the 4360 |
| `wlc_phy_block_bbpll_change` | 0x0b2b65 | 76 | original (wlc_phy_cmn.c) |
| `wlc_phy_tssivisible_thresh_acphy` | 0x08f24a | 115 | original; only the value it returns (section 5) |
| `sub_0a4adc` | 0x0a4adc | 5330 | assigned: `wlc_phy_set_regtbl_on_chan_change_acphy`; annex A1 |
| `sub_0a4867` | 0x0a4867 | 629 | assigned: `wlc_phy_set_tx_cck_dig_filt_acphy`; annex A1 |
| `wlc_phy_populate_recipcoeffs_acphy` | 0x09b8e1 | 771 | original; annex A1 |
| `sub_09eaf9` | 0x09eaf9 | 4217 | assigned: `wlc_phy_set_regtbl_on_bw_change_acphy`; annex A2 |
| `sub_09e378` | 0x09e378 | 1921 | `wlc_phy_set_regtbl_on_band_change_acphy` (name by `acphy-radio.md`); annex A3 |
| `sub_092efb`, `sub_0909fd` | 0x092efb, 0x0909fd | 221, 378 | assigned: `wlc_phy_desense_aci_getset_chanidx_acphy`, `wlc_phy_desense_calc_total_acphy`; annex A4 |
| `sub_09af05`, `sub_099658`, `sub_0998cc` | 0x09af05, 0x099658, 0x0998cc | 2125, 628, 225 | assigned: `wlc_phy_rxgainctrl_set_gaintbls_acphy`, `.._set_lna_gaintbl_acphy`, `.._set_lna_gainlimit_acphy`; annex A5 |
| `sub_09a121`, `sub_099f29`, `sub_090493`, `sub_09175a`, `sub_091b6e` | 0x09a121, 0x099f29, 0x090493, 0x09175a, 0x091b6e | 1048, 504, 231, 1044, 744 | assigned: `wlc_phy_rxgainctrl_set_init_clip_gain_acphy`, `.._set_gain_acphy`, `.._calc_clip_pwr_acphy`, `.._nbclip_acphy`, `.._w1clip_acphy`; annex A6 |
| `sub_09a539`, `sub_08f84d`, `sub_090b77` | 0x09a539, 0x08f84d, 0x090b77 | 850, 191, 99 | assigned: `wlc_phy_desense_apply_acphy` and two helpers; annex A7 |

## Overview

### Chanspec

A channel is described by a 16 bit "chanspec":

| Bits | Mask | Meaning |
|---|---|---|
| 0..7 | 0x00ff | channel number of the centre frequency (for 40 and 80 MHz the centre of the whole channel, e.g. 42 for 36/80) |
| 8..10 | 0x0700 | position of the control channel inside the channel (0 = lowest) |
| 11..13 | 0x3800 | bandwidth: 0x1000 = 20 MHz, 0x1800 = 40 MHz, 0x2000 = 80 MHz (0x2800 = 160 MHz in the MAC code, not supported by this PHY) |
| 14..15 | 0xc000 | band: 0 = 2.4 GHz, 0xc000 = 5 GHz |

Examples seen in the traces: channel 1 = 0x1001, 6 = 0x1006, 36 = 0xd024,
36/80 = 0xe02a, 149/40 = 0xd897 (centre 151), 149 = 0xd095.

### One PHY state for both bands

The MAC keeps a band state per band (`wlc_hw+0xf0` 2.4 GHz, `wlc_hw+0xf8`
5 GHz, the current one in `wlc_hw+0xe8`), but both band states point to the
same `pi` (checked in the emulator: the two pointers `band+0x28` are equal). So
`pi+0x17e` (chanspec of the radio), `pi_ac` and all PHY state exist once.

### Order of events of a channel change

`wlc_set_chanspec(wlc, chanspec)` (wlc.c; called by the ioctl/iovar code, scan,
association); in the traces its caller, the iovar handler, suspends the MAC
before and enables it after the call:

1. Statistics (`cca_stats_upd`); nothing is done if the chanspec is not valid
   or equals the current one (`wlc+0x518`).
2. If the band differs from the current band: the upper layer's band pointer
   `wlc+0x40` is switched and, if the driver is up, `sub_039dd4` (assigned:
   `wlc_bsinit`: rate table to shared memory, antenna selection) runs.
3. `sub_03649a(wlc, chanspec)`: `wlc+0x518` = chanspec; the regulatory limits
   of the channel are collected in a power-per-rate object;
   `wlc_bmac_set_chanspec(wlc_hw, chanspec, mute, limits)`, section 1. This is
   where the hardware is touched:
   1. MAC clock forced to fast (`D11(0x1e0)` bit 1).
   2. Only if the band changes: interrupts off, band flag in the core's
      wrapper (`WRAP(d11, 0x408)` bit 13), band specific MAC initialisation
      (`sub_06656c`, **without** a PHY initialisation), interrupts restored.
      The radio stays on.
   3. `wlc_phy_chanspec_set(pi, chanspec)`, section 4: chanspec to `SHM(0xa0)`,
      interference mode of the band selected, then the channel function of the
      AC-PHY `sub_0a7089`, section 5, which does everything else: bandwidth of
      the PHY clock (`WRAP(d11, 0x408)` bits 6..7) if the bandwidth changes,
      radio tuning, band/bandwidth/channel dependent PHY set-up, calibrations.
   4. `wlc_phy_txpower_limit_set(pi, limits, chanspec)`: target powers
      (`sub_09949f`; specification `acphy-txpower`/`phy-cmn`).
   5. `wlc_bmac_mute(wlc_hw, mute, 0)`: transmit FIFOs suspended and address
      match cleared if the channel is a "quiet" one (radar/passive).
   6. MAC clock back to dynamic.
4. `wlc_stf_chanspec_upd`, `wlc_stf_ss_update`, `wlc_bmac_txbw_update` (shared
   memory of the transmit path), `wlc_bmac_ifsctl_edcrs_set`.

The same channel function also runs at the end of the PHY initialisation
(`sub_0b018f`, with the flag "init" `pi_ac+0x32c` set) and when the radio is
switched on while the PHY is initialised (`wlc_phy_switch_radio_acphy`,
`acphy-radio.md` section 4 step 6).

Three conditions steer the channel function:

| Name used below | Meaning |
|---|---|
| INIT | `pi_ac+0x32c` != 0: called by the PHY initialisation |
| BANDCHG | INIT, or the band of the new chanspec differs from `pi_ac+0x330` |
| BWCHG | INIT, or the bandwidth bits of the new chanspec differ from `pi_ac+0x334` |

A change of the channel inside a band with the same bandwidth tunes the radio
and repeats the channel dependent part (resampler, receive gain, transmit
power, VCO calibration); band and bandwidth dependent set-up and the AFE
calibration are skipped.

## Data

### Structure fields

See `re-out\analysis\fields\acphy-chanspec.tsv`. Used below:

| Field | Meaning |
|---|---|
| `pi+0x38` | function pointer: channel function (`sub_0a7089`) |
| `pi+0x168` (u8) | number of cores N (2) |
| `pi+0x17e` (u16) | chanspec the radio is tuned to |
| `pi+0x182` (u16) | bandwidth bits the PHY clock is set to (0x1000, 0x1800, 0x2000) |
| `pi+0x19c` (u32) | hold flags; the channel function tests the mask 0x0206 (unverified: scan, measurement and "scan while radio is normally off" in progress) |
| `pi+0x1e3 + 5*g + c` (s8) | receive gain error of core c from the SROM for channel group g = 0..4 (section 8); `pi+0x1e7 + 5*g` (u8): 1 = the SROM has no values for the group |
| `pi+0x212 + c` (s8) | correction of the signal strength of core c, result of section 5 step 32 |
| `pi+0x240` (u8) | channel number the interference state belongs to ("home" channel) |
| `pi+0xfa0` (u8) | transmit power control is on |
| `pi+0xfa2` (u8) | Bluetooth is active (from the coexistence code, updated by the watchdog) |
| `pi+0x113e` (u8) | changes of the baseband PLL are blocked |
| `pi+0x1140` (u16) | frequency of the spur mode request that arrived while blocked |
| `pi+0x1164` (u8) | spur mode wanted |
| `pi_ac+0x00c` (u16) | nesting counter of "stay in carrier search" |
| `pi_ac+0x010 + c` (s8) | transmit power index of core c last set by `wlc_phy_txpwr_by_index_acphy` |
| `pi_ac+0x32c` (u8) | INIT |
| `pi_ac+0x32f` (u8) | set to 0 by the channel function; not read anywhere in the PHY code (purpose unknown) |
| `pi_ac+0x330` (u8) | band last set up: 1 = 2.4 GHz, 0 = 5 GHz; at attach from `pi+0x17e` |
| `pi_ac+0x334` (u32) | bandwidth bits last set up; at attach from `pi+0x17e` |
| `pi_ac+0x338` (u8) | spur mode last applied (0 at attach) |
| `pi_ac+0x456 .. 0x459` (s8) | transmit power offset per core (four entries), section 10 |
| `pi_ac+0x45f + 3*c` (u8) | loss of the transmit/receive switch of core c in the current band, in dB |
| `pi_ac+0x46a + 0x78*c + 10*s + k` (s8) | gain in dB of entry k of receive gain stage s (6 stages, 10 entries) of core c |
| `pi_ac+0x4a6 + 0x78*c + 10*s + k` (u8) | code of that entry |
| `pi_ac+0x64a + s` (u8) | number of entries of stage s (2, 6, 7, 10, 8, 8) |
| `pi_ac+0x650 + s` (u8) | upper limit of the gain accumulated up to and including stage s |
| `pi_ac+0x670` (u8) | desense flag computed by `sub_0909fd`, argument of `sub_09a539` |
| `pi_ac+0x8a8` (pointer) | interference (desense) state of the channel, result of `sub_092efb`, or 0 |
| `pi_ac+0x908` (u16 of a u32) | high RSSI bypass: duration in watchdog ticks |
| `pi_ac+0x910`, `+0x911` (u8) | high RSSI bypass enabled for 2.4 GHz, for 5 GHz |
| `pi_ac+0x912` (u8) | high RSSI bypass feature present |
| `pi_ac+0x914`, `+0x916` (s16) | high RSSI bypass: remaining ticks for 2.4 GHz, for 5 GHz (negative: not active) |
| `sh+0x20` | handle of the PHY shim; its first field is `wlc_hw` |
| `sh+0x31` (u8) | the clock of the core is on, registers may be accessed |
| `sh+0x4c` (u32) | `subband5gver` from the SROM |
| `sh+0x80` (u32) | interference mode in use |
| `sh+0x84`, `sh+0x88` (u32) | interference mode configured for 2.4 GHz, 5 GHz |
| `sh+0x8c`, `sh+0x90` (u32) | interference mode forced by the user for 2.4 GHz, 5 GHz |
| `sh+0x94` (u8) | 1 = the forced values are used |
| `sh+0xa4`, `sh+0xa7` (u8) | chain masks compared at INIT (section 5 step 30): `hw_phytxchain` and `phyrxchain` (names as in `acphy-attach.md`) |
| `wlc_hw+0x94` (u32) | pending interrupt bits of the MAC |
| `wlc_hw+0x10c` (u8) | driver is up |
| `wlc_hw+0x118` (u32) | number of bands (2) |
| `wlc_hw+0x11c` (u16) | chanspec |
| `wlc_hw+0x184` (u8) | "no reset" |
| `wlc_hw+0x185` (u8) | fast clock is forced |
| `wlc_hw+0x186` (u8) | MAC clock is on |
| `wlc_hw+0x187` (u8) | backplane clock is on |
| `band+0x04` (u32) | band unit: 0 = 2.4 GHz, 1 = 5 GHz (`band` = `wlc_hw+0xe8`) |
| `wlc+0x40`, `wlc+0x50 + 8*unit` | band state of the upper layer: current, per band |
| `wlc+0x518` (u16) | chanspec of the upper layer |

### Constants

| Constant | Value |
|---|---|
| Target gain for the signal strength correction | 0x45 = 69 dB |
| Delay after the bandwidth of the PHY clock was changed | 2 us |
| Width of the radio PLL reset pulse (`PHY(0x728)` bit 8) | 1 us |
| Delay between the CCA reset and the AFE calibration | 1 us |
| Value of the TSSI threshold register | 0x7f00 + threshold |
| Wrapper flags of the 802.11 core, `WRAP(d11, 0x408)` | 0x0002 forced clock gating off (used by the CCA reset), 0x00c0 bandwidth of the PHY clock (0x40 = 20, 0x80 = 40, 0xc0 = 80 MHz), 0x2000 band is 2.4 GHz |

## Procedures

`mod(reg, mask, value)` is read-modify-write and always writes. "For each core
c" means c = 0 .. N-1 in rising order. Delays are `osl_delay` in microseconds.
`cflags(mask, value)` stands for `si_core_cflags(sih, mask, value)` on the
802.11 core: read `WRAP(d11, 0x408)`, write `(old & ~mask) | value`, read it
back (three accesses).

### 1. wlc_bmac_set_chanspec (.text+0x067918, name original)

Inputs: `wlc_hw`, chanspec, `mute` (bool), pointer to the power limits of the
channel. Steps:

1. fast = `wlc_hw+0x185`. If fast is 0: `sub_064887(wlc_hw, 0)`
   (`wlc_bmac_clkctl_clk`, specification `bmac-init`; on this card: read
   `D11(0x1e0)`, write it with bit 1 set, delay 64, poll `D11(0x1e0)` for bit 17
   every 10 us for at most 20 ms).
2. `wlc_hw+0x11c` = chanspec.
3. Band switch, only if `wlc_hw+0x118` > 1 and the band unit of the chanspec
   (1 if `chanspec & 0xc000` = 0xc000, else 0) differs from `band+0x04`:
   * Driver not up (`wlc_hw+0x10c` = 0): only `wlc_setxband(wlc_hw, unit)`.
   * Driver up (inlined function, assigned name `wlc_bmac_setband`):
     1. `wlc_phy_chanspec_radio_set(pi of the new band, chanspec)`.
     2. If the 802.11 core is not up (`si_iscoreup`: `WRAP(d11, 0x408)` and
        `WRAP(d11, 0x800)` are read): `si_core_reset(sih, 0, 0)`, the MAC state
        `wlc_hw+0x164`, `+0x168`, `+0x170`, `+0x174` is cleared and
        `wlc_bmac_mctrl(wlc_hw, 0xffffffff, 0x04000400)`. Not seen in the traces.
     3. `wl_intrsoff` of the glue (result kept): `D11(0x12c)` = 0, read back.
     4. PHY types other than AC (11) and N revision >= 3: radio off and PHY
        clock off. **Not for the AC-PHY: radio and clock stay on.**
     5. `wlc_setxband(wlc_hw, unit)`, section 2.
     6. (N-PHY/A-PHY only: PLL reset loop. Not for the AC-PHY.)
     7. `sub_06656c(wlc_hw, chanspec, 1)` (`wlc_bmac_bsinit`, specification
        `bmac-init`). With the third argument 1 and a MAC revision >= 40 it
        does **not** call `wlc_phy_init`: the PHY is not initialised again on
        a band change, the channel function does the band dependent work.
     8. If `wlc_hw+0x94` != 0: `wlc_hw+0x94` = 0x8000.
     9. `wl_intrsrestore` of the glue with the kept value.
4. `wlc_phy_initcal_enable(pi, mute == 0)`: acts for PHY type 4 only, nothing
   for the AC-PHY.
5. Driver not up: if `wlc_hw+0x186` != 0 `wlc_phy_txpower_limit_set(pi, limits,
   chanspec)`; then `wlc_phy_chanspec_radio_set(pi, chanspec)`; continue with
   step 7.
6. Driver up:
   1. `wlc_phy_chanspec_set(pi, chanspec)` (section 4). For the PCI device ids
      0x43a0 (this card), 0x43a3, 0x43ae and 0x43b1 it is called always, for
      other devices only if the chanspec differs from `wlc_phy_chanspec_get(pi)`.
   2. `wlc_phy_txpower_limit_set(pi, limits, chanspec)`.
   3. `wlc_bmac_mute(wlc_hw, mute, 0)`.
7. If fast was 0 in step 1: `sub_064887(wlc_hw, 2)` (read `D11(0x1e0)`, write
   it with bit 1 cleared).

`pi` is `band+0x28` of the band that is current at that moment.

### 2. wlc_setxband (.text+0x064fb5, name original)

Inputs: `wlc_hw`, band unit (0 = 2.4 GHz, 1 = 5 GHz). Steps:

1. `wlc_hw+0xe8` = `wlc_hw+0xf0 + 8*unit`; `wlc+0x40` = `wlc+0x50 + 8*unit`.
2. If `wlc_hw+0x187` != 0 and `wlc_hw+0x184` = 0: `cflags(0x2000, unit == 0 ?
   0x2000 : 0)`.

Callers: `wlc_bmac_attach`, `wlc_bmac_init`, `wlc_bmac_set_chanspec`.

### 3. wlc_bmac_bw_set, wlapi_bmac_bw_set, wlc_bmac_bw_reset, wlc_phy_clk_bwbits, wlc_phy_bw_state_set/get

`wlapi_bmac_bw_set(shim, bw)` calls `wlc_bmac_bw_set(wlc_hw, bw)` with `wlc_hw`
= first field of the shim. `bw` is the bandwidth field of a chanspec (0x1000,
0x1800, 0x2000).

`wlc_bmac_bw_set(wlc_hw, bw)` for the AC-PHY:

1. `wlc_phy_bw_state_set(pi, bw)`: `pi+0x182` = bw.
2. `wlc_bmac_bw_reset(wlc_hw)`: if there is a current band with a PHY:
   `cflags(0x00c0, wlc_phy_clk_bwbits(pi))`.

(MAC revisions below 17 read `D11(0x120)` in between; PHY types other than AC
are reset and initialised again instead of step 2.)

`wlc_phy_clk_bwbits(pi)`: for PHY types 4, 6, 7, 8, 10 and 11, by `pi+0x182`:
0x1000 -> 0x40, 0x1800 -> 0x80, 0x2000 -> 0xc0, anything else -> 0.

`wlc_phy_bw_state_get(pi)` returns `pi+0x182`; it has no callers in the object.
Callers of `wlapi_bmac_bw_set` for the AC-PHY: the channel function (section 5
step 7, not at INIT) and `wlc_phy_init` (when the bandwidth of its chanspec
differs from `pi+0x182`; `acphy-init.md` section 1). This is why the channel
function does not set the bandwidth when it is called by the initialisation:
`wlc_phy_init` has done it before.

### 4. wlc_phy_chanspec_set, wlc_phy_chanspec_shm_set, wlc_phy_chanspec_radio_set, wlc_phy_chanspec_get

`wlc_phy_chanspec_set(pi, chanspec)`:

1. `wlc_phy_chanspec_shm_set(pi, chanspec)`: MAC revision >= 40: `SHM(0xa0)` =
   chanspec, unchanged. (Older MACs get the channel number, bit 8 for 5 GHz,
   bit 9 for 40 MHz.)
2. AC-PHY only: select the interference mode of the band: `sh+0x80` = if
   `sh+0x94` = 1 then (2.4 GHz: `sh+0x8c`, 5 GHz: `sh+0x90`) else (2.4 GHz:
   `sh+0x84`, 5 GHz: `sh+0x88`).
3. If `pi+0x38` is not null call it: `sub_0a7089(pi, chanspec)`.
4. `wlapi_update_bt_chanspec(shim, chanspec, bit 1 of pi+0x19c, bit 2 of
   pi+0x19c)`: notification for Bluetooth coexistence (specification
   `bmac-init`); no hardware access on this card in the traces.

`wlc_phy_chanspec_radio_set(pi, chanspec)`: `pi+0x17e` = chanspec, nothing
else. `wlc_phy_chanspec_get(pi)`: returns `pi+0x17e`.

### 5. sub_0a7089 (.text+0x0a7089, name by the guide: wlc_phy_chanspec_set_acphy)

Purpose: tune radio and PHY to a chanspec. Inputs: `pi`, chanspec (16 bit).
No result. In the traces the MAC is suspended (channel change) or not yet
started (initialisation) when the function runs; its own code does not suspend
or enable the MAC (`wlc_phy_rxcore_setstate_acphy` in step 30 does).

Notation: ch = chanspec & 0xff; is2g = (chanspec & 0xc000) == 0; bw =
chanspec & 0x3800.

Steps (the numbers in brackets give the condition; no bracket = always):

1. INIT = `pi_ac+0x32c`.
2. Look up the channel: `sub_08e9b1(pi, ch, &freq, &e0, &e1, &e2, &e3)` with
   e0..e3 preset to 0 (`acphy-radio.md` section 10; for this radio e0 becomes
   the address of the channel table entry, e1..e3 stay 0). If it returns 0
   (unknown channel or radio revision without table): **return; nothing has
   been changed or accessed.**
3. (Chip 0x4335 with package 2 only: spur mode, sections 11 and 12. Not
   executed on the 4360.)
4. (INIT and PHY revision 2, 3, 5 or 6: `mod(PHY(0x40a), 0x0200, 0x0200)`.
   Not for revisions 0 and 1.)
5. BANDCHG = INIT or (is2g differs from `pi_ac+0x330`). If BANDCHG:
   1. If `pi_ac+0x912` != 0 and `sub_092500(pi)` returns non-zero (the
      function reads `SHM(0x184)`; if the value is 0xdead it writes 0 to
      `SHM(0x184)` and returns 1; `acphy-init.md` section H11): the high RSSI
      timer of the band that is *left* is started: if `pi_ac+0x330` != 0
      (2.4 GHz) and `pi_ac+0x910` != 0: `pi_ac+0x914` = low 16 bit of
      `pi_ac+0x908`; if `pi_ac+0x330` = 0 and `pi_ac+0x911` != 0:
      `pi_ac+0x916` = low 16 bit of `pi_ac+0x908`.
   2. `pi_ac+0x330` = is2g.
6. s1 = `PHY(0x19e)`; s2 = `PHY(0x19e)` (two reads).
   `mod(PHY(0x19e), 0x0002, 0x0002)`; `mod(PHY(0x19e), 0x0001, 0x0001)`.
   (Aliases, unverified: bit 1 "disable stalls", bit 0 "reset of the sample
   FIFO of the receiver front end".)
7. BWCHG = INIT or (`pi_ac+0x334` differs from bw). If BWCHG:
   1. `pi_ac+0x334` = bw.
   2. If not INIT: `wlapi_bmac_bw_set(sh+0x20, bw)` (section 3: `pi+0x182` =
      bw, `cflags(0x00c0, bits)`). (Chip 0x4335 and 80 MHz only, then:
      `PHY(0x16a)` = 0xfe, CCA reset, `PHY(0x16a)` = 0xff.)
   3. Delay 2.
8. `mod(PHY(0x003), 0x0100, is2g ? 0 : 0x0100)` (band select).
9. `pi_ac+0x00c` = 0; `wlc_phy_stay_in_carriersearch_acphy(pi, 1)`
   (`acphy-init.md` section H4; because the counter was just cleared it always
   takes its "enter" branch: classifier, carrier sense off, then a CCA reset).
10. `pi_ac+0x32f` = 0.
11. `wlc_phy_chanspec_radio_set(pi, chanspec)`: `pi+0x17e` = chanspec.
12. Radio tuning, `acphy-radio.md` section 11 steps 1 to 8 (executed because e0
    is not 0): if BANDCHG or BWCHG the pulse on bit 8 of `PHY(0x728)`
    (`mod(.., 0x0100, 0x0100)`, delay 1, `mod(.., 0x0100, 0)`); the 50 radio
    registers of the channel entry; the patches for radio revision 3 and for
    channel 4; `mod(RADIO(0x645), 0x7000, 0x7000)`; on 5 GHz
    `wlc_2069_rfpll_150khz(pi)`; radio revision >= 4: `RADIO(0x723)` = 0x83e0;
    `sub_09311b(pi)` (start of the VCO calibration).
13. (PHY revision 3 with boardflags2 bit 13 and a 40 MHz crystal only: spur
    canceller entries in `TBL(0x03)` for channel 13. Not for revisions 0, 1.)
14. `mod(PHY(0x19e), 0x0002, s1 & 0x0002)`; `mod(PHY(0x19e), 0x0001, s2 & 0x0001)`.
15. [INIT] `sub_0a0be4(pi)` (PHY registers at reset, `acphy-init.md` section 5;
    summary in `acphy-radio.md` section 17), then `sub_0a6b0f(pi)`
    (specification `acphy-rxgain`).
16. [BANDCHG] `sub_09e378(pi)` (annex A3; radio part in `acphy-radio.md`
    section 15).
17. [BWCHG] `sub_09eaf9(pi)` (annex A2).
18. [`pi_ac+0x912` != 0 and (BANDCHG or BWCHG)]
    `wlc_phy_hirssi_elnabypass_set_ucode_params_acphy(pi)` (`acphy-attach.md`
    section 10; writes `SHM(0x32)`, `SHM(0x180)`, `SHM(0x182)`).
19. `sub_0a4adc(pi, e0, e1, e2, e3)` (annex A1; executed because e0 is
    not 0).
20. Interference state of the channel: `pi_ac+0x8a8` = 0. If
    (`pi+0x19c` & 0x0206) != 0 and `pi+0x240` differs from the channel number
    (low byte of `pi+0x17e`): nothing more (the channel is visited during a
    scan). Otherwise `pi+0x240` = channel number and `pi_ac+0x8a8` =
    `sub_092efb(pi, chanspec, 1)` (annex A4).
21. `sub_0909fd(pi)` (annex A4; no hardware access).
22. `sub_09af05(pi, INIT, BANDCHG, BWCHG)` (annex A5).
23. `sub_09a121(pi)` (annex A6).
24. `sub_09a539(pi, pi_ac+0x670)` (annex A7; the byte as `sub_0909fd` left it).
25. [BWCHG or INIT] `wlc_phy_resetcca_acphy(pi)` (`acphy-init.md` section H5);
    delay 1; `sub_096203(pi)`
    (AFE calibration, `acphy-radio.md` section 14).
26. (PHY revision 2, 3, 5, 6: `mod(PHY(0x40a), 0x0200, 0x0200)`. Not for 0, 1.)
27. For each core c: `wlc_phy_txpwr_by_index_acphy(pi, 1 << c, pi_ac+0x010+c)`
    (`acphy-txpower`): the transmit gain of the index that was last set is
    written again (the gain table changes with the band).
28. Transmit power control:
    1. saved = `pi+0xfa0`.
    2. `wlc_phy_txpwrctrl_enable_acphy(pi, 0)`.
    3. t = `wlc_phy_tssivisible_thresh_acphy(pi)` (8 bit).
    4. `PHY(0x1641)` = 0x7f00 + t (plain write; the address has bit 12 set on
       chips 0x4352, 0x4360, 43460, 43526 and 0x4350, other chips use 0x0641).
    5. `sub_0995ab(pi, pi+0xfa2)`: section 10, nothing on the 4360.
    6. `wlc_phy_txpwrctrl_enable_acphy(pi, saved)`.
29. `sub_093e47(pi, 0)`: wait for the VCO calibration started in step 12
    (`acphy-radio.md` section 13; executed because e0 is not 0).
30. [INIT] v = `PHY(0x00b)`; m = `sh+0xa7`. If m differs from the low byte of
    `(1 << (v & 7)) - 1` or `sh+0xa4` differs from m:
    `wlc_phy_rxcore_setstate_acphy(pi, m)` (`acphy-init.md` section H7).
31. `wlc_phy_resetcca_acphy(pi)`.
32. Signal strength correction: e = `wlc_phy_get_rxgainerr_phy(pi, err[])`
    (section 8). If e = 0: for each core c: x = `sub_08f086(pi, c, 69, 0,
    scratch)` (section 9: the gain in dB the receiver reaches for a wanted gain
    of 69 dB); `pi+0x212+c` = `err[c] + 2 * (69 - x)` truncated to 8 bit (the
    object computes `err[c] - 0x7a - (2*x - 4)` in 8 bit arithmetic, which is
    the same modulo 256). If e != 0: `pi+0x212+c` = 0 for each core.
33. (PHY revision 2, 5, 6: `mod(PHY(0x400), 0x0001, 0)`. Not for 0, 1.)
34. `wlc_phy_stay_in_carriersearch_acphy(pi, 0)`.

Notes:

* The function has no error handling and no result. A timeout of the VCO
  calibration or of the AFE calibration is not noticed.
* Nothing in the function itself depends on the PHY revision being 0 or 1 or
  on the radio revision being 3 or 4 (apart from the radio tuning, see
  `acphy-radio.md`); the functions it calls do.
* Steps 3, 4, 13, 26, 33 and the second half of step 7.2 exist for other
  chips/PHY revisions only.

What runs when:

| Step | Channel change only | Bandwidth change | Band change | INIT |
|---|---|---|---|---|
| 5 high RSSI timer, band stored | - | - | yes | yes |
| 7 `wlapi_bmac_bw_set`, delay 2 | - | yes | - | delay only |
| 12 pulse on `PHY(0x728)` bit 8 | - | yes | yes | yes |
| 12 radio tuning, VCO calibration start | yes | yes | yes | yes |
| 15 `sub_0a0be4`, `sub_0a6b0f` | - | - | - | yes |
| 16 `sub_09e378` | - | - | yes | yes |
| 17 `sub_09eaf9` | - | yes | - | yes |
| 18 high RSSI parameters to shared memory | - | yes | yes | yes |
| 19..24 `sub_0a4adc` .. `sub_09a539` | yes | yes | yes | yes |
| 25 CCA reset, AFE calibration | - | yes | - | yes |
| 27, 28 transmit gain, TSSI threshold | yes | yes | yes | yes |
| 29 wait for the VCO calibration | yes | yes | yes | yes |
| 30 receive cores | - | - | - | yes |
| 31, 32, 34 | yes | yes | yes | yes |

Value of t in step 28 (`wlc_phy_tssivisible_thresh_acphy`, .text+0x08f24a,
name original; specification `acphy-txpower`; given here because the result
depends on the board), PHY revision 0 and 1:

| Condition, tested in this order | t |
|---|---|
| the 16 bit value at `pi_ac+0x342` is 0x0203 (SROM `femctrl` = 3 and boardflags3 bits 0..2 = 2) | 0x26 |
| chip 0x4360 and board type (`sh+0x58`) 0x137 or 0x117 (the Apple boards) | 0x14 |
| chip 0x4360, board type 0x134 or 0x112, channel number above 148 | 0x16 |
| otherwise | 0x18 |

So on the card of this project `PHY(0x1641)` = 0x7f14 for every channel.

What the called functions write depends on (from the comparison of traces of
two chanspecs that differ in one property, `depmatrix.py`; "-" = no access
differs; SROM with different values per sub-band, run `s`):

| Called function | Channel inside a sub-band | Sub-band of 5 GHz | Bandwidth | Band | Position of the control channel |
|---|---|---|---|---|---|
| radio tuning (step 12) | channel table entry | the same | - | 150 kHz loop filter on 5 GHz | - |
| `sub_0a4adc` (A1) | resampler, `PHY(0x371..0x376)` | row of table A1, `rpcal` value (448 entries of `TBL(0x11)`) | resampler block, `PHY(0x31c..0x31f)` | the same, 11b filter for channel 14 | bit 4 of `PHY(0x164)`, bits 14..15 of `PHY(0x30f)`: the only accesses of the whole channel function that depend on it |
| `sub_09af05` | - | channel number below 100 or not (it copies the front end values of the SROM: 2.4 GHz, 5 GHz low for channels below 100, 5 GHz high otherwise; the "mid" values are not used there) | - | yes | - |
| `sub_09a121` | - | as `sub_09af05` | yes | yes | - |
| `sub_09a539` | - | - | yes | - | - |
| `wlc_phy_txpwr_by_index_acphy` | - | - | - | yes (gain table of the band) | - |
| `wlc_phy_txpwrctrl_enable_acphy`, `sub_092efb`, `sub_093e47`, `sub_096203` | - | - | - | - | - |
| `sub_09eaf9` (A2) | not called | not called | everything | four registers | - |
| `sub_09e378` (A3) | not called | not called | not called | everything | - |

### 6. wlc_phy_get_chan_freq_range_acphy (.text+0x08edad, name original)

Purpose: the sub-band of a channel, used to select per sub-band values of the
SROM (PA parameters, power limits, receive gains, ...). Inputs: `pi`, channel
number (0 = the current channel, low byte of `pi+0x17e`). Result (8 bit):

1. f = frequency in MHz of the channel from `sub_08e9b1` (0 if the channel is
   not in the table).
2. If the channel number is 14 or lower: return 0 (2.4 GHz).
3. By `subband5gver` (`sh+0x4c`):

| `subband5gver` | Result 1 | Result 2 | Result 3 | Result 4 |
|---|---|---|---|---|
| 4 | 5170 <= f <= 5249 | 5250 <= f <= 5499 | 5500 <= f <= 5744 | everything else (f >= 5745, and f < 5170: the 4.9 GHz channels and unknown channels) |
| 0 | 5170 <= f <= 5499 | 5500 <= f <= 5744 | everything else | - |
| 1 | 5170 <= f <= 5249 | 5250 <= f <= 5744 | everything else | - |
| other | 4900 <= f <= 5099 | 5100 <= f <= 5499 | everything else | - |

   (The comparisons are unsigned subtractions: `f - 5170 <= 79` and so on.)

No hardware access. Callers: the transmit power code, `sub_0a4adc`,
`wlc_phy_populate_recipcoeffs_acphy`, `wlc_phy_crs_min_pwr_cal_acphy`,
`wlc_phy_rssi_compute_acphy`, `wlc_phy_chanspec_bandrange_get`.

### 7. wlc_phy_chanspec_bandrange_get (.text+0x0b964a, name original), AC-PHY path

Inputs: `pi`, chanspec (only its low byte, the channel number, is used).
Result for the AC-PHY: `wlc_phy_get_chan_freq_range_acphy(pi, channel number)`
(section 6) as 32 bit value. (The table lookup of the frequency at the start
of the function, 67 entries at `.rodata+0x2ce840`, is only used by other PHY
types.) Callers for the AC-PHY: `wlc_phy_get_paparams_for_band`,
`wlc_phy_rssi_compute`, `wlc_phy_noise_avg`, `wlc_phy_iovar_dispatch`.
Note that a channel number of 0 means "the current channel" in section 6.

### 8. wlc_phy_get_rxgainerr_phy (.text+0x0b20bd, name original), AC-PHY path

Inputs: `pi`, array of 16 bit results (one per core). Result: 8 bit, 1 = "the
SROM has no values" (the caller then uses 0).

1. ch = low byte of `pi+0x17e`. Group g: ch < 15: 0; ch < 49: 1; ch < 65: 2;
   ch < 129: 3; else 4. (Note that this grouping is by channel number and is
   not the one of section 6.)
2. For each core c: `err[c]` = `pi+0x1e3 + 5*g + c` (signed 8 bit, extended).
3. Return `pi+0x1e7 + 5*g`.

The fields are filled by `wlc_phy_attach_acphy` from the SROM variables
`rxgainerr2ga0..2` (group 0) and `rxgainerr5ga0..2` (four values each, groups 1
to 4): core 0 is the value taken as signed 6 bit, core 1 and core 2 are the
value taken as signed 5 bit plus the value of core 0. If all three values of a
group are -1 (all bits set) and `rawtempsense` (9 bit) is -1 too, the group is
marked as empty and its values are 0. (Details: specification `acphy-attach`.)

### 9. sub_08f086 (.text+0x08f086, name assigned: wlc_phy_rxgainctrl_encode_gain_acphy)

Purpose: split a wanted receive gain into the gains of the six gain stages of
a core, from the tables in `pi_ac`. Inputs: `pi`, core c, wanted gain in dB (8
bit), flag "include the loss of the T/R switch", pointer to 6 bytes for the
codes of the chosen entries. Result: 8 bit, the gain reached in dB. No hardware
access. Callers: the channel function (wanted gain 69, flag 0, codes not
used) and `sub_099f29` (`acphy-desense`).

Tables (see "Data"): `gain[s][k]`, `code[s][k]`, `len[s]`, `max[s]` for stage
s = 0..5; tr = `pi_ac+0x45f + 3*c` if the flag is set, else 0.

1. need = tr + wanted (16 bit); total = 0 (16 bit); `limit[s]` = `max[s]` + tr
   (8 bit, unsigned); `min[s]` = `gain[s][0]`.
2. For s = 0 .. 5:
   1. If s = 4: if need mod 3 = 2 (need taken as unsigned 16 bit) then need =
      need + 1; if need > 30 (unsigned 16 bit compare) then need = 30.
   2. rest = `min[s+1]` + ... + `min[5]`; room = need - rest, taken as signed 8
      bit.
   3. For k = `len[s]` - 1 down to 0 (k is a signed 8 bit counter): the entry is
      taken if `code[s][k]` = `code[s][0]`, or if (`gain[s][k]` <= room, signed
      8 bit) and (total + `gain[s][k]`, signed 16 bit, <= `limit[s]`). If
      taken: `out[s]` = `code[s][k]`; total = total + `gain[s][k]`; need = need
      - `gain[s][k]`; go on with the next stage. If no entry is taken (only
      possible when `len[s]` is 0) `out[s]` is left as it is.
3. Return (total - tr), low 8 bit.

Because entry 0 of a stage always passes the first test, every stage with at
least one entry contributes.

### 10. sub_0995ab (.text+0x0995ab, name assigned: wlc_phy_btc_txpwr_core_offset_acphy)

Inputs: `pi`, `bt_active` (bool). **If the chip id `sh+0x3c` is not 0x4352 the
function returns at once**: nothing on the 4360.

Chip 0x4352: offsets = {-8, -8, -8, -8} if `bt_active` and the band of
`pi+0x17e` is 2.4 GHz; {0, 0, 0, 0} if not `bt_active` or the band is 5 GHz;
then `sub_099528(pi, offsets)` (`acphy-txpower`; assigned name
`wlc_phy_txpower_core_offset_set_acphy`): for i = 0..3: if `offsets[i]` != 0 and
i > N return the error -2; store `offsets[i]` in `pi_ac+0x456+i` if it differs;
after the loop, if something was changed and `sh+0x31` != 0: MAC suspended,
`sub_09949f(pi)` (target power recalculation), MAC enabled. (With N = 2 and
four offsets of -8 the loop stores three values and leaves with the error at
i = 3, without the recalculation.)

Callers: the channel function (step 28.5) and `wlc_phy_btc_adjust_acphy`.

### 11. wlc_phy_get_spurmode (.text+0x08e780, name original)

Not reached on the 4360: the channel function calls it for chip 0x4335 only,
and the only other caller is `wlc_phy_set_spurmode` (section 12).

Inputs: `pi`, frequency in MHz (16 bit). Result in `pi+0x1164`.

1. If `pi+0x113e` != 0 (blocked): `pi+0x1140` = frequency; return.
2. If `pi_ac+0x34f` != 0: mode 8; return.
3. Table set: 1 ("low power VCO") if `pi_ac+0x8e4` = 2, or `pi_ac+0x8e5` = 1
   and the band of `pi+0x17e` is 2.4 GHz; else 0. Both fields only change for
   PHY revisions 2, 5, 6 (`acphy-radio.md` section 20), so the 4360 would use
   set 0.
4. The frequency is searched in the lists of the modes 1, 2, 3, 4, 5, 6, 8, in
   this order; the first list that contains it gives the mode. If no list
   contains it the mode is 2.

Lists (16 bit frequencies; number of entries per mode from the byte table
`.rodata+0x2ccb30`, 9 bytes per set, index = mode):

| Mode | Set 0: address, entries | Set 1: address, entries |
|---|---|---|
| 1 | `.bss+0x3084`, 0 | `.data+0xd9dee`, 2 |
| 2 | `.data+0xd9eb0`, 48 | `.data+0xd9e00`, 37 |
| 3 | `.data+0xd9f20`, 36 | `.data+0xd9e50`, 37 |
| 4 | `.data+0xd9f10`, 2 | `.data+0xd9e4a`, 3 |
| 5 | `.data+0xd9f68`, 1 | `.data+0xd9e9a`, 3 |
| 6 | `.data+0xd9f6a`, 1 | `.data+0xd9ea0`, 3 |
| 8 | `.data+0xd9f6c`, 1 | `.data+0xd9ea6`, 3 |

(`python blob.py data .data+0xd9eb0 48 2` prints a list. The lists belong to
the 4335's crystal and are of no use for the 4360.)

### 12. wlc_phy_set_spurmode, wlc_phy_setup_spurmode, wlc_phy_block_bbpll_change

`wlc_phy_set_spurmode(pi, frequency)` (.text+0x097e86): `wlc_phy_get_spurmode`;
if `pi+0x1164` differs from `pi_ac+0x338`: `wlc_phy_setup_spurmode(pi)` and
`pi_ac+0x338` = `pi+0x1164`. (The same three lines are inlined in the channel
function for chip 0x4335.)

`wlc_phy_setup_spurmode(pi)` (.text+0x097e4a):

1. `si_pmu_spuravoid(sih, osh, pi+0x1164)`: specification `pmu.md`. On the 4360
   family the mode has no effect: `CC(0x600)` is read and written back
   unchanged, no PLL register changes.
2. `wlapi_switch_macfreq(shim, pi+0x1164)` -> `wlc_bmac_switch_macfreq(wlc_hw,
   mode)` (specification `bmac-init`). On the 4360 family the mode is not
   used: the frequency of the baseband VCO is read from the PMU
   (`si_pmu_get_bb_vcofreq`: `PMU_PLLCTL[2]`, `PMU_PLLCTL[3]`) and the MAC's
   clock ratio is written: `D11(0x62e)` = low 16 bit, `D11(0x630)` = high 16 bit.

`wlc_phy_block_bbpll_change(pi, block, going_down)` (.text+0x0b2b65,
wlc_phy_cmn.c; callers `wlc_up`, `wlc_down`, `sub_0450f0`): if `block`:
`pi+0x113e` = 1. Else: `pi+0x113e` = 0 and, if `pi+0x1140` != 0 and
`going_down` = 0, for the AC-PHY `wlc_phy_set_spurmode(pi, pi+0x1140)`. On the
4360 `pi+0x1140` is never written (section 11 is not reached), so nothing
happens.

## Annex: channel, bandwidth and band dependent set-up in the called functions

The functions of this annex are called by the channel function and belong to
the assignments `acphy-rxgain` (A1, A2, A3) and `acphy-desense` (A4 to A7),
whose specifications did not exist when this was written. They are described
here because they hold everything that depends on the channel (A1), the
bandwidth (A2) and the band (A3) besides the radio tuning, and because A4 to
A7 run on every channel change; where the specifications `acphy-rxgain.md`
and `acphy-desense.md` exist and differ, they are the authoritative ones for
the internals of these functions. All statements are for PHY revision 0 and 1
(the functions have other branches for revisions 2, 3, 5, 6, which are left
out). Still only referenced (not described in any finished specification at
the time of writing): `sub_0a6b0f` (INIT only), `wlc_phy_crs_min_pwr_cal_acphy`
(in A1), `wlc_phy_set_analog_tx_lpf` (in A2), `sub_08f9b4`,
`wlc_phy_hwaci_setup_acphy`, `wlc_phy_aci_w2nb_setup_acphy`, `sub_09c161`,
`sub_08f2e9` (in A3), `sub_08f41b` (in A7), and the transmit power functions
of steps 27 and 28 (`acphy-txpower.md` is being written).

### A1. sub_0a4adc (.text+0x0a4adc, 5330 bytes; name assigned: wlc_phy_set_regtbl_on_chan_change_acphy)

Runs on every call of the channel function (step 19). Inputs: `pi`, the four
pointers of the channel lookup (only e0, the channel table entry, is used
here). State read: `pi+0x17e`, `pi_ac+0x410`/`+0x411` (SROM `pdgain2g`,
`pdgain5g`), `pi_ac+0x8fe` (SROM `cckdigfilttype`, default 1), `sh+0xa5` (mask
of the receive chains of the hardware), `pi+0x1169`.

Notation: ch = low byte of `pi+0x17e`; bw index b = 0 for 20 MHz, 1 for 40 MHz,
2 otherwise; o(c) = 0, 0x200, 0x400 for core 0, 1, 2.

1. `mod(PHY(0x410), 0x0008, 0)`; `mod(PHY(0x410), 0x0380, 0)`; `pi+0x116a` = 0.
   For each core c: `mod(PHY(0x73a+o), 0x0008, 0)`; `mod(PHY(0x725+o), 0x0040,
   0)`; `mod(PHY(0x73a+o), 0x0010, 0)`; `mod(PHY(0x725+o), 0x0080, 0)`;
   `mod(PHY(0x73a+o), 0x0007, 0)`; `mod(PHY(0x725+o), 0x0020, 0)`.
2. Resampler. The entry of channel ch is searched in block b of the table
   `rx_farrow_tbl` (3 blocks of 123 entries of six 16 bit words; word 0
   channel, word 1 frequency). If the channel is not found steps 2 to 4 are
   skipped. With the words w2..w5 of the entry:
   `PHY(0x19a)` = w2, `PHY(0x19b)` = w3, `PHY(0x19c)` = w4, `PHY(0x199)` = w5,
   `PHY(0x1a1)` = w2, `PHY(0x1a2)` = w3, `PHY(0x1a3)` = w4, `PHY(0x1a0)` = w5.
   With the words t2..t5 of the entry with the same index in the same block
   of `tx_farrow_dac1_tbl` (chosen because `pi_ac+0x000` is 1):
   `PHY(0x1603)` = t2, `PHY(0x1602)` = t3, `PHY(0x1607)` = t4, `PHY(0x1606)` = t5.
   (Plain writes; chips other than 0x4352, 0x4360, 43460, 43526, 0x4350 use
   0x0603 and so on.)

   All entries of both tables follow these formulas (checked for all 369
   entries of each table with `farrow.py`), f = centre frequency in MHz, fvco
   = f * 3/2 for 2.4 GHz and f * 2/3 for 5 GHz channels, rounding to nearest:

   | | 20 MHz and 40 MHz (blocks 0, 1) | 80 MHz (block 2) |
   |---|---|---|
   | R | round(fvco * 65536 / 10) | round(fvco * 65536 / 7.5) |
   | w2, w3 | R & 0xffff, (R >> 16) & 0xff | the same |
   | w4 | 0x1400 on 2.4 GHz, 0x0f00 on 5 GHz | 0x0500 on 2.4 GHz, 0x0b40 on 5 GHz |
   | w5 | ((R >> 24) << 7) \| 0x27 | ((R >> 24) << 7) \| 0x04 |
   | T | round(1280 * 2^24 / fvco) | round(1920 * 2^24 / fvco) |
   | t2, t3 | T & 0xffff, T >> 16 | the same |
   | t4, t5 | the same as t2, t3 | the same |

   Channels in the tables (the same for all blocks): 184..228 (even), 32..144
   (even), 145..166, 168..180 (even), 1..14.
3. Only if `pi+0x1169` != 0 (set by an iovar only, 0 by default) and the
   bandwidth is 40 MHz; f' = 5000 + 5 * ch:
   * f' = 5310: `mod(PHY(0x410), 0x0008, 0x0008)`; `mod(PHY(0x410), 0x0380, 0)`;
     for each core c: `mod(PHY(0x73a+o), 0x0008, 0x0008)`; `mod(PHY(0x725+o),
     0x0040, 0x0040)`; `mod(PHY(0x73a+o), 0x0010, 0)`; `mod(PHY(0x725+o), 0x0080,
     0x0080)`; `mod(PHY(0x73a+o), 0x0007, 0x0001)`; `mod(PHY(0x725+o), 0x0020,
     0x0020)`; then `mod(RADIO(0x145), 0x000f, 8)`; `mod(RADIO(0x146), 0x01e0,
     0x0100)`; `mod(RADIO(0x146), 0x000f, 8)`; `pi+0x116a` = 1; values V =
     0x0000, 0x00d8, 0x0b40, 0x0000, 0x00d8, 0x0b40, 0x6c79, 0x0045, 0x6c79,
     0x0045.
   * f' = 5270: `mod(PHY(0x410), 0x0008, 0x0008)`; `mod(PHY(0x410), 0x0380,
     0x0100)`; values V = 0x4bda, 0x0038, 0x10e0, 0x4bda, 0x0038, 0x10e0,
     0xed0e, 0x0068, 0xed0e, 0x0068.
   * In both cases then: `PHY(0x19a)`, `PHY(0x19b)`, `PHY(0x19c)`, `PHY(0x1a1)`,
     `PHY(0x1a2)`, `PHY(0x1a3)`, `PHY(0x1603)`, `PHY(0x1602)`, `PHY(0x1607)`,
     `PHY(0x1606)` = V[0..9] in this order.
4. v = `PHY(0x601)` (read); `PHY(0x1601)` = v.
5. Table entry of the power amplifier per core (purpose by position in the RF
   control table, unverified): p = `pi_ac+0x410` on 2.4 GHz, `pi_ac+0x411` on
   5 GHz; r = `wlc_phy_get_chan_freq_range_acphy(pi, 0)` (0..4; values above 4
   count as 0); row = the 6 bytes at `.rodata+0x2cd480 + 30*p + 6*r`. Then: s =
   `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`; for each core c whose bit is
   set in `sh+0xa5`: read `TBL(0x07)[0x3cd + 0x10*c]` (16 bit, value not used),
   write `TBL(0x07)[0x3cd + 0x10*c]` = (row[c] & 7) | (row[3+c] << 3);
   finally `mod(PHY(0x19e), 0x0002, s & 0x0002)`. The table has 30 bytes per
   value of p (p is not range checked; 19 rows are filled, see "Table A1").
6. `PHY(0x371)` .. `PHY(0x376)` = words 52..57 of the channel table entry e0
   (byte offsets 0x68..0x72), in rising order.
7. Position of the control channel:
   * 80 MHz: `mod(PHY(0x30f), 0xc000, n << 14)` with n = bits 8..10 of the
     chanspec, if n <= 3 (no access for larger n). `PHY(0x164)` is not touched.
   * 40 MHz: if n = 1: `mod(PHY(0x164), 0x0010, 0x0010)`; `mod(PHY(0x30f),
     0xc000, 0x4000)`. Otherwise `mod(PHY(0x164), 0x0010, 0)`; `mod(PHY(0x30f),
     0xc000, 0)`.
   * 20 MHz: `mod(PHY(0x164), 0x0010, 0)`; `mod(PHY(0x30f), 0xc000, 0)`.
8. x = 0x00ff on 2.4 GHz; on 5 GHz 0x0100 for 80 MHz, 0x00bf for 20 and 40 MHz.
   `PHY(0x31c)`, `PHY(0x31d)`, `PHY(0x31e)`, `PHY(0x31f)` = x.
9. State of the carrier sense calibration is cleared: 4 bytes at
   `pi_ac+0x014`, 4 bytes at `pi_ac+0x03d`, 16 bytes at `pi_ac+0x02c`,
   `pi_ac+0x043`, `pi_ac+0x03c`; then `wlc_phy_crs_min_pwr_cal_acphy(pi, 1)`
   (`acphy-desense`).
10. Filter of the 11b transmitter, fc = frequency of the channel table entry
    (word 1): v = `PHY(0x3a9)` (read), t = `pi_ac+0x8fe`.
    * fc = 2484 (channel 14): `mod(PHY(0x3a9), 0x007f, v & 0x3f)`;
      `mod(PHY(0x3a9), 0x0800, 0x0800)`; `sub_0a4867(pi, 0)`.
    * else: `mod(PHY(0x3a9), 0x007f, (v & 0x3f) | ((t & 2) << 5))`;
      `mod(PHY(0x3a9), 0x0800, (t & 4) << 9)`; `sub_0a4867(pi, t & 1)`.
11. `wlc_phy_populate_recipcoeffs_acphy(pi)`.

`sub_0a4867(pi, k)` (.text+0x0a4867, name assigned:
`wlc_phy_set_tx_cck_dig_filt_acphy`), PHY revision 0 and 1: ten plain writes,
`PHY(0x0ec)` .. `PHY(0x0f5)` in rising order:

| k | 0xec | 0xed | 0xee | 0xef | 0xf0 | 0xf1 | 0xf2 | 0xf3 | 0xf4 | 0xf5 |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0x0a94 | 0x0373 | 0x0005 | 0x0a93 | 0x0298 | 0x0004 | 0x0a52 | 0x021d | 0x0004 | 0x0080 |
| 1 | 0x0b54 | 0x0290 | 0x0004 | 0x0a40 | 0x0290 | 0x0005 | 0x0a06 | 0x0240 | 0x0005 | 0x0080 |

(Other values of k: nothing is written.)

`wlc_phy_populate_recipcoeffs_acphy(pi)` (.text+0x09b8e1, name original), PHY
revision 0 and 1. Nothing is done if `sh+0xa4` (mask of the transmit chains of
the hardware) is 0 or 1. Otherwise:

1. r = `wlc_phy_get_chan_freq_range_acphy(pi, 0)`; q = the 16 bit value
   `sh+0xcc + 2*r` for r = 0..4 (SROM `rpcal2g`, `rpcal5gb0` .. `rpcal5gb3`),
   `sh+0xcc` for other r.
2. For i = 0, 1 (always two): byte B = low byte of q for i = 0, high byte for
   i = 1; k = B & 0x3f; quadrant = (B >> 6) & 3; s = S[k], c = S[63 - k] with
   S[n] = round(512 * sin(n * pi / 128)) (table of 64 values, S[0] = 0, S[63] =
   512); (x, y) = (c, -s), (-s, -c), (-c, s), (s, c) for quadrant 0, 1, 2, 3;
   a negative value v is replaced by v + 0x800; V(i) = x | (y << 11) | 0x400000
   (24 bit).
3. E = V(0) | V(1) << 24 (48 bit).
4. s19e = `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`.
5. `TBL(0x11)`, 48 bit entries, each entry with a transfer of its own:
   offsets 0..11 = 0x5b, 0x8250, 0xc338, 0x14527, 0x1a6a1, 0x2081b, 0x28a18,
   0x32c96, 0x38e17, 0x4101b, 0x20, 0x20; offsets 12..459 (448 entries) = E;
   offsets 460..463 = 0.
6. `mod(PHY(0x19e), 0x0002, s19e & 0x0002)`.

With q = 0 (no value in the SROM) E is 0x400200400200.

**Table A1**, bytes at `.rodata+0x2cd480`, one line per value p of `pdgain2g`
/ `pdgain5g`, five groups of six bytes for r = 0..4 (each group: three low
parts for core 0, 1, 2, then three high parts):

```
 p
 0  02 01 02 6b 96 6e  02 02 01 9d 99 a0  02 02 01 9d 99 a1  02 02 00 9d 99 ba  02 02 00 9d 99 bb
 1  01 00 01 9f ae a1  01 00 01 a0 b9 9c  01 00 01 a3 b9 a2  01 00 01 a9 bb a7  01 00 01 98 bc a0
 2  01 01 01 9f a6 a6  02 02 04 8c 97 64  02 02 03 8f 99 74  02 02 02 8f 99 8c  02 02 02 91 a0 9a
 3  01 01 02 82 83 6a  01 01 02 82 83 6a  01 01 02 80 7f 61  00 01 03 9f 89 4b  00 00 03 a4 a2 4c
 4  01 01 01 9c a0 9e  01 01 01 9c a0 9e  01 01 01 9c a0 9e  01 01 01 9c a0 9e  01 01 01 9c a0 9e
 5  02 02 02 68 6c 6a  02 02 02 68 6c 6a  02 02 02 68 6c 6a  02 02 02 68 6c 6a  02 02 02 68 6c 6a
 6  02 00 02 66 aa 68  03 04 03 52 66 52  01 03 01 86 7a 88  01 03 01 86 7c 88  02 03 02 68 7a 6c
 7  00 00 00 b4 b4 b4  00 00 00 b4 b4 b4  00 00 00 b4 b4 b4  00 00 00 b4 b4 b4  00 00 00 b4 b4 b4
 8  02 01 02 66 8a 68  03 05 03 52 64 52  01 04 01 86 74 88  01 03 01 86 88 88  02 03 02 68 88 6c
 9  03 02 03 5a 6a 56  03 01 03 5a 9e 5a  02 01 02 72 9e 70  02 01 01 74 9e 8e  02 01 01 74 9e 8e
10  02 02 02 98 9c 9c  02 02 02 98 9c 9c  02 02 02 98 9c 9c  02 02 02 98 9c 9c  02 02 02 98 9c 9c
11  01 01 01 86 86 86  01 01 01 88 88 88  01 01 01 88 88 88  01 01 01 88 88 88  01 01 01 88 88 88
12  03 03 03 5a 5c 56  03 03 03 5a 56 5a  02 03 02 72 56 70  02 02 01 74 6d 8e  02 02 01 74 6e 8e
13  02 02 02 70 72 70  02 02 02 72 72 72  02 02 02 72 72 72  02 02 02 71 72 70  02 02 02 71 72 70
14  01 01 01 86 86 86  00 00 00 a8 a8 a8  00 00 00 a8 a8 a8  00 00 00 a8 a8 a8  00 00 00 a8 a8 a8
15  00 00 00 ac ac ac  00 00 00 a8 a8 a8  00 00 00 a8 a8 a8  00 00 00 a8 a8 a8  00 00 00 a8 a8 a8
16  03 02 03 5a 6a 56  03 00 03 5a ba 5a  02 00 02 72 ba 70  02 00 01 74 ba 8e  02 00 01 74 ba 8e
17  04 04 04 32 2d 32  03 03 03 52 52 52  03 03 03 52 52 52  03 03 03 52 52 52  03 03 03 52 52 52
18  05 05 05 3d 3d 3d  02 02 02 7a 7a 7a  02 02 02 7a 7a 7a  02 02 02 7a 7a 7a  02 02 02 7a 7a 7a
19  (all 0)
```

### A2. sub_09eaf9 (.text+0x09eaf9, 4217 bytes; name assigned: wlc_phy_set_regtbl_on_bw_change_acphy)

Runs when BWCHG (step 17). Input: `pi`; the bandwidth is taken from
`pi+0x17e`, which the channel function has already set. "80" below stands for
"neither 20 nor 40 MHz".

1. s = `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`.
2. Modifications in this order (the mask is the same for all bandwidths):

   | Register | Mask | 20 MHz | 40 MHz | 80 MHz |
   |---|---|---|---|---|
   | `PHY(0x076)` | 0x0007 | 1 | 2 | 3 |
   | `PHY(0x140)` | 0x0800 | 0x0800 | 0 | 0 |
   | `PHY(0x164)` | 0x0010 | 0x0010 | 0 | 0 |
   | `PHY(0x180)` | 0x001f | 0x15 | 0x0b | 0x05 |
   | `PHY(0x181)` | 0x07ff | 0x146 | 0x181 | 0x17a |
   | `PHY(0x182)` | 0x07ff | 0x088 | 0x05a | 0x09e |
   | `PHY(0x183)` | 0x07ff | 0x146 | 0x181 | 0x17a |
   | `PHY(0x184)` | 0x07ff | 0x76e | 0x793 | 0x7ca |
   | `PHY(0x185)` | 0x07ff | 0x1a8 | 0x1b7 | 0x1b2 |
   | `PHY(0x186)` | 0x07ff | 0x0a3 | 0x0c1 | 0x0bd |
   | `PHY(0x187)` | 0x07ff | 0x0f4 | 0x102 | 0x114 |
   | `PHY(0x188)` | 0x07ff | 0x0a3 | 0x0c1 | 0x0bd |
   | `PHY(0x189)` | 0x07ff | 0x684 | 0x6c0 | 0x6d6 |
   | `PHY(0x18a)` | 0x07ff | 0x0ad | 0x0a9 | 0x0a2 |
   | `PHY(0x18b)` | 0x07ff | 0x0e5 | 0x162 | 0x16c |
   | `PHY(0x18c)` | 0x07ff | 0x068 | 0x042 | 0x06f |
   | `PHY(0x18d)` | 0x07ff | 0x0e5 | 0x162 | 0x16c |
   | `PHY(0x18e)` | 0x07ff | 0x6be | 0x75c | 0x793 |
   | `PHY(0x18f)` | 0x07ff | 0x19e | 0x1b3 | 0x1b2 |
   | `PHY(0x190)` | 0x07ff | 0x073 | 0x0b1 | 0x0b6 |
   | `PHY(0x191)` | 0x07ff | 0x0b2 | 0x0ed | 0x0ff |
   | `PHY(0x192)` | 0x07ff | 0x073 | 0x0b1 | 0x0b6 |
   | `PHY(0x193)` | 0x07ff | 0x5fe | 0x692 | 0x6b4 |
   | `PHY(0x194)` | 0x07ff | 0x0cc | 0x0af | 0x0a8 |
   | `PHY(0x1b5)` | 0x00ff | 0x97 | 0x8b | 0x97 |
   | `PHY(0x250)`, 5 GHz only | 0x00ff | 0x19 | 0x32 | 0x32 |
   | `PHY(0x261)`, 5 GHz only | 0x0fff | 0x014 | 0x028 | 0x028 |
   | `PHY(0x262)`, 5 GHz only | 0x0fff | 0x0c8 | 0x190 | 0x190 |
   | `PHY(0x263)`, 5 GHz only | 0x0fff | 0x019 | 0x032 | 0x032 |
   | `PHY(0x312)` | 0x00ff | 0x13 | 0x13 | 0x09 |
   | `PHY(0x313)` | 0xff00 | 0x1300 | 0x1300 | 0x0900 |

   (Chip 0x4335 on 2.4 GHz also modifies `PHY(0x361)`.)
3. For each core c (o = 0x200 * c): `mod(PHY(0x6ed+o), 0x00ff, 0x0a for 20 MHz,
   else 0x14)`; `mod(PHY(0x6ef+o), 0x00ff, 0x17 / 0x2a / 0x54)`;
   `mod(PHY(0x6ef+o), 0xff00, 0x0e00 / 0x1600 / 0x2c00)` (20 / 40 / 80 MHz).
4. For each core c: `mod(PHY(0x6ef+o), 0x00ff, 0x0f / 0x1e / 0x3c)`.
5. `wlc_phy_set_analog_tx_lpf(pi, 0x100, -1, n, n, -1, -1, -1)` with n = 3 for
   20 MHz, 4 for 40 MHz, 5 for 80 MHz (PHY revisions above 1: 5, 5, 6; other
   bandwidths: 0). (`acphy-rxgain`; it reads and writes `TBL(0x07)[0x14a +
   0x10*c]` and `[0x36a + 0x10*c]`.)
6. PHY revision 0 only: `PHY(0x16d4)` = 0x0cc0 for 80 MHz, else 0x0c60.
7. `TBL(0x04)`, 8 bit entries: offsets 1..3 = 8, 6, 4 and offsets 0x3d..0x3f =
   4, 6, 8 for 20 MHz; both = 0, 0, 0 for 40 and 80 MHz (two transfers of three
   entries).
8. For each core c: `mod(PHY(0x73a+o), 0x0080, v)` with v = 0x0080, except for
   PHY revision 0 with 20 or 40 MHz: v = 0; then `mod(PHY(0x725+o), 0x0200,
   0x0200)`.
9. 80 MHz only: `TBL(0x14)` (48 bit entries) offset 0x30 = 0x000000960fd2,
   0x31 = 0x000000860fc2, 0x32 = 0x000000860fd2 (three transfers).
10. `TBL(0x07)` (16 bit entries), six transfers of eight entries in this order:

    | Offset | 20 and 40 MHz | 80 MHz |
    |---|---|---|
    | 0x30 | 0x2a, 0x07, 0x0a, 0x00, 0x08, 0x2b, 0x1f, 0x1f | 0x07, 0x0a, 0x00, 0x08, 0xb0, 0xb1, 0x1f, 0x1f |
    | 0xa0 | 1, 2, 2, 2, 0x10, 1, 1, 1 | 2, 2, 2, 1, 0x0a, 1, 1, 1 |
    | 0x40 | 0x2a, 0x07, 0x08, 0x0c, 0x0e, 0x2b, 0x1f, 0x1f | 0x07, 0x08, 0x0c, 0x0e, 0xb0, 0xb2, 0x1f, 0x1f |
    | 0xb0 | 1, 6, 0x12, 8, 0x10, 1, 1, 1 | 6, 0x12, 8, 1, 0x0a, 1, 1, 1 |
    | 0x50 | 0x2a, 0x07, 0x08, 0x0e, 0x2b, 0x1f, 0x1f, 0x1f | 0x07, 0x08, 0x0e, 0xb0, 0xb1, 0x1f, 0x1f, 0x1f |
    | 0xc0 | 1, 6, 0x1e, 0x1c, 1, 1, 1, 1 | 6, 0x1e, 0x1c, 0x0a, 1, 1, 1, 1 |

11. `TBL(0x14)` offset 0x33 (48 bit) = 0x00000084e800 for 20 MHz,
    0x000000844800 for 40 MHz, 0x000000860800 for 80 MHz.
12. `TBL(0x07)`, two transfers of 16 entries, the same for all bandwidths:
    offset 0x10 = 0xb3, 4, 3, 6, 5, 0, 2, 1, 8, 0x2a, 0x0f, 0, 0x0f, 0x2b,
    0x1f, 0x1f; offset 0x80 = 1, 8, 4, 2, 2, 1, 3, 4, 6, 4, 0x0a, 4, 2, 1, 1, 1.
13. `PHY(0x197)` = 0x14, `PHY(0x198)` = 0x10 for 20 MHz; 0x1e and 0x14 for 40
    and 80 MHz.
14. `mod(PHY(0x19e), 0x0002, s & 0x0002)`.

Nothing in the function depends on the band except the four registers marked
"5 GHz only" (they are not written on 2.4 GHz), and nothing depends on the
channel. Note that step 2 sets bit 4 of `PHY(0x164)` for 20 MHz and A1 step 7,
which runs later in the same call of the channel function, clears it again.

### A3. sub_09e378 (.text+0x09e378, 1921 bytes; name by `acphy-radio.md`: wlc_phy_set_regtbl_on_band_change_acphy)

Runs when BANDCHG (step 16). Input: `pi`; the band is taken from `pi+0x17e`.

1. s = `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`.
2. 2.4 GHz: `PHY(0x1ec)` = 0x0002; `mod(PHY(0x2e4), 0x3f00, 0x0f00)`.
   5 GHz: `PHY(0x1ec)` = 0x9c40 (40000); `mod(PHY(0x2e4), 0x3f00, 0x0800)`.
3. Transmit gain table of the band, 128 entries of 48 bit, one transfer:
   `TBL(0x20)[0..127]` = the table selected by band, kind of power amplifier
   and radio revision (the tables are objects of 768 bytes in `.data`;
   contents and entry format: specification `acphy-txpower`):

   | Band | SROM `extpagain2g` / `extpagain5g` (`sh+0xb0` / `sh+0xac`) | Radio revision | Table |
   |---|---|---|---|
   | 2.4 GHz | 2 (internal PA) | 3, 4 | `acphy_txgain_ipa_2g_2069rev0` |
   | 2.4 GHz | other | 3 | `acphy_txgain_epa_2g_2069rev0` |
   | 2.4 GHz | other | 4 (and 8) | `acphy_txgain_epa_2g_2069rev4`; if `pi_ac+0x349` (boardflags3 bits 4..6) is 1: `acphy_txgain_epa_2g_2069rev4_id1` |
   | 5 GHz | 2 | 3, 4 | `acphy_txgain_ipa_5g_2069rev0` |
   | 5 GHz | other | 3 | `acphy_txgain_epa_5g_2069rev0` |
   | 5 GHz | other | 4 (and 8) | `acphy_txgain_epa_5g_2069rev4` |

4. PHY revision 0 only: radio registers, see `acphy-radio.md` section 15.
5. Only if `pi_ac+0x411` (`pdgain5g`) is 9 or 16: `TBL(0x07)[0x18e]` (16 bit) =
   0x0049 on 5 GHz, 0 on 2.4 GHz.
6. `sub_08f9b4(pi, 0)` (`acphy-txpower`).
7. `sub_0940f8(pi, sh+0xa5, 0)` (`acphy-radio.md` section 16).
8. m = `sh+0x80` (interference mode selected by `wlc_phy_chanspec_set`, section
   4): `wlc_phy_hwaci_setup_acphy(pi, bit 1 of m, 0)`;
   `wlc_phy_aci_w2nb_setup_acphy(pi, bit 2 of m)` (`acphy-desense`).
9. `sub_09c161(pi, buffer of 56 zero bytes)` (transmit calibration
   coefficients, `acphy-cal-tx`).
10. `sub_08f2e9(pi, pi+0xfa2)` (`acphy-rxgain`).
11. `mod(PHY(0x19e), 0x0002, s & 0x0002)`.

(PHY revisions 2, 3, 5, 6 also modify `PHY(0x16c)` and `PHY(0x419)`.)

### A4. sub_092efb and sub_0909fd (interference state of the channel)

Both belong to the assignment `acphy-desense` and have no hardware access
except the clock read of `wlc_phy_get_time_usec`. They are steps 20 and 21 of
the channel function.

`sub_092efb(pi, chanspec, create)` (.text+0x092efb, name assigned:
`wlc_phy_desense_aci_getset_chanidx_acphy`). The driver keeps the interference
history of three channels per band: records of 0x50 bytes, three for 2.4 GHz
at `pi_ac+0x6c8` and three for 5 GHz at `pi_ac+0x7b8`. Fields of a record used
here: +0x00 (u8) channel number, +0x02 (u16) bandwidth bits, +0x08 (u64) time
of the last use, +0x10 (9 bytes) desense values, +0x45 (u8) a state.

1. The three records of the band of the chanspec are searched for one with
   the channel number (low byte of the chanspec) and the bandwidth bits
   (chanspec & 0x3800).
2. If none is found: if `create` = 0 return 0. Otherwise the record with the
   smallest time is taken (on equal times the first), filled with 0, its
   channel number set to the low byte of `pi+0x17e` and its bandwidth to
   `pi+0x182` (both from the state, not from the argument).
3. If `create` != 0: record+0x45 = 2; record+0x08 = `wlc_phy_get_time_usec(pi)`.
4. Return the address of the record.

`sub_0909fd(pi)` (.text+0x0909fd, name assigned:
`wlc_phy_desense_calc_total_acphy`) builds the desense values in use, 9 bytes
at `pi_ac+0x668` (the last one, `pi_ac+0x670`, is the argument of
`sub_09a539`):

1. src = the 9 bytes at record+0x10 if `pi_ac+0x8a8` is not 0, else the 9 bytes
   at `pi_ac+0x65f`.
2. If `pi_ac+0x8b0` (u32) != 0, the band of `pi+0x17e` is not 5 GHz and the
   scan condition of step 20 of the channel function does not hold: for i =
   0..7: `pi_ac+0x668+i` = the larger (unsigned) of `pi_ac+0x8b4+i` and
   src[i]; `pi_ac+0x670` = `pi_ac+0x8bc` | src[8]. Otherwise the 9 bytes are
   copied from src.
3. If the high RSSI timer of the band is running (2.4 GHz: `pi_ac+0x914` >= 0,
   5 GHz: `pi_ac+0x916` >= 0, signed 16 bit): `pi_ac+0x66e` = 1.

(Interpretation, unverified: `pi_ac+0x8b4` are the desense values wanted by
the Bluetooth coexistence, `pi_ac+0x8b0` their switch; byte 6 of the values is
"bypass the external LNA", byte 8 "desense is on".)

### A5. sub_09af05 (.text+0x09af05, 2125 bytes; name assigned: wlc_phy_rxgainctrl_set_gaintbls_acphy) with sub_099658 and sub_0998cc

Step 22 of the channel function, called as `sub_09af05(pi, INIT, BANDCHG,
BWCHG)`; the fourth argument is not used. It belongs to the assignments
`acphy-desense`/`acphy-rxgain`. It writes the gain tables of the receiver
(`TBL(0x44)`: gains in dB, `TBL(0x45)`: codes, for core 0; `TBL(0x64)`/`TBL(0x65)`
for core 1; `TBL(0x84)`/`TBL(0x85)` for core 2; `TBL(0x0b)`: gain limits) and
keeps a copy of them in `pi_ac` (the tables that section 9 uses). All table
entries are 8 bit. Board dependence: `pi_ac+0x340`, `+0x341` (boardflags bit
12 and 28: external LNA on 2.4 GHz, on 5 GHz), the SROM values `rxgains*`
(`pi_ac+0x3e0..`), and the desense values `pi_ac+0x668..` of A4.

1. s19e = `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`; s16c = `PHY(0x16c)`;
   `mod(PHY(0x16c), 0x0040, 0x0040)`.
2. `sub_099658(pi, 1)`; `sub_0998cc(pi, 1)`; `sub_099658(pi, 2)`;
   `sub_0998cc(pi, 2)` (below).
3. For each core c (G = 0x44 + 0x20*c, C = 0x45 + 0x20*c, o = 0x200*c, P =
   `pi_ac+0x46a + 0x78*c`):
   1. Front end values of the band: from `pi_ac+0x3e0 + 3*c` on 2.4 GHz, from
      `pi_ac+0x3ec + 3*c` on 5 GHz if the channel number is below 100, else
      from `pi_ac+0x404 + 3*c`; three bytes (gain of the external LNA e, loss
      of the T/R switch t, bypass b) are copied to `pi_ac+0x45e + 3*c`.
   2. If `pi_ac+0x912` != 0: v = `PHY(0x73e+o)`; `PHY(0x73e+o)` = v & 0xfb3f
      (plain write).
   3. PHY revision 1: `mod(PHY(0x6f9+o), 0x7f00, (t + 2) << 8)`;
      `mod(PHY(0x6f9+o), 0x007f, 2)`. PHY revision 0: only for core 0, and on
      register `PHY(0x289)` instead.
   4. `TBL(G)[0..1]` = e, e; the same two bytes to P (stage 0).
   5. Only if BANDCHG or INIT:
      * 2.4 GHz: `PHY(0x173b)` = 0x001c if `pi_ac+0x66e` != 0, else 0x0018;
        `PHY(0x1726)` = 0x000c; `TBL(G)[0x20..0x29]` = ten times 3;
        `TBL(C)[0x20..0x29]` = ten times 2.
      * 5 GHz: `PHY(0x173b)` = 0x002c; `PHY(0x1726)` = 0x000c;
        `TBL(G)[0x20..0x29]` = ten times 7 and `TBL(C)[0x20..0x29]` = ten times
        2 if `pi_ac+0x341` != 0, else ten times 0x10 and ten times 5.
      * The ten gains are copied to P+0x1e (stage 3).
      * Only if INIT, table reads that fill the copy in `pi_ac`: `TBL(C)[0]` (1
        entry) to `pi_ac+0x4a6 + 0x78*c`; `TBL(C)[0x20..0x29]` to `+0x4c4`;
        `TBL(G)[0x60..0x67]` to P+0x28; `TBL(G)[0x70..0x77]` to P+0x32;
        `TBL(C)[0x60..0x67]` to `pi_ac+0x4ce + 0x78*c`; `TBL(C)[0x70..0x77]` to
        `pi_ac+0x4d8 + 0x78*c` (in this order).
4. `PHY(0x16c)` = s16c (plain write); `mod(PHY(0x19e), 0x0002, s19e & 0x0002)`.

(The number of entries copied for stage 0 and stage 3 are `pi_ac+0x64a` = 2
and `pi_ac+0x64d` = 10.)

`sub_099658(pi, stage)` (.text+0x099658; stage 1 and 2 are the two internal
LNAs; other values: nothing). n = `pi_ac+0x64a + stage` (6 for stage 1, 7 for
stage 2).

| | Stage 1 | Stage 2 |
|---|---|---|
| Table offset | 8 | 0x10 |
| Gains T (signed) | 2.4 GHz: -10, -1, 6, 12, 18, 25; 5 GHz (the object has one list per bandwidth, all three are equal): -7, -2, 4, 10, 16, 23 | 5 GHz: -11, -8, -5, -2, 2, 5, 9; 2.4 GHz: -12, -8, -4, -1, 2, 5, 9, or, if `pi_ac+0x66e` != 0: -10, -6, -2, 1, 4, 7, 11 |
| Desense d | `pi_ac+0x66a` | `pi_ac+0x66b` |
| Highest index h | 5 | 6; but on 2.4 GHz with `pi_ac+0x66e` = 0 and `pi_ac+0x340` != 0: 5 if `pi_ac+0x3e0` (gain of the external LNA of core 0) is below 10, else 4 |
| d is remembered in | `pi_ac+0x658` | `pi_ac+0x659` |

m = h - d, at least 0. Entry k = 0: gain T[1], code 1; entry k = 1..n-1: gain
T[k] and code k if k <= m, else gain T[m] and code m. Then for each core c:
the n gains are copied to `pi_ac+0x46a + 0x78*c + 10*stage` and written to
`TBL(G)[offset ..]`, the n codes are copied to `pi_ac+0x4a6 + 0x78*c +
10*stage` and written to `TBL(C)[offset ..]` (one transfer of n entries each,
in this order).

`sub_0998cc(pi, stage)` (.text+0x0998cc): one transfer to `TBL(0x0b)`.

| | Stage 1 | Stage 2 |
|---|---|---|
| Offset, entries | 8, 6 | 0x10, 7 |
| Values | 0x0b, 0x0c, 0x0e, 0x20, 0x24, 0x28 | 0, 0, 0, 3, 3, 3, 3 |
| Desense d | `pi_ac+0x66c`, remembered in `pi_ac+0x65a` | `pi_ac+0x66d`, remembered in `pi_ac+0x65b` |
| m | 5 - d, at least 0 | 6 - d, at least 0 |

The entries with an index above m are replaced by 0x7f.

### A6. sub_09a121 (.text+0x09a121, 1048 bytes; name assigned: wlc_phy_rxgainctrl_set_init_clip_gain_acphy) with sub_099f29, sub_090493, sub_09175a, sub_091b6e

Step 23 of the channel function; assignment `acphy-desense`. It sets the gain
the receiver starts with and the gains it falls back to when a clip detector
fires, and the thresholds of the two clip detectors of the radio. Signed
values are 8 bit two's complement unless said otherwise. `gain[s][k]`,
`code[s][k]` are the tables of core c in `pi_ac` (section 9); t(c) =
`pi_ac+0x45f + 3*c`, bp(c) = `pi_ac+0x460 + 3*c`; o = 0x200 * c.

`sub_09a121(pi)`:

1. h = bit 0 of `pi_ac+0x910` on 2.4 GHz, of `pi_ac+0x911` on 5 GHz.
2. s19e = `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`.
3. `pi_ac+0x650 .. 0x655` (upper limits of the accumulated gain) = 43, 43, 43,
   52, 52, 100 on 2.4 GHz; 47, 47, 47, 52, 52, 100 on 5 GHz.
4. lna = `pi_ac+0x340` on 2.4 GHz, `pi_ac+0x341` on 5 GHz (external LNA
   present); byp = `pi_ac+0x66e`; `pi_ac+0x65c` = byp; a = lna & byp; b = lna &
   (byp | 1). If b != 0: g3 = g4 = 15. Else: g3 = 15 if lna = 0 (else 30); g4 =
   (g3 + 35) >> 1 (25 for g3 = 15).
5. For each core c whose bit is set in `sh+0xa7`:
   1. Read `PHY(0x6dc+o)` (not used).
   2. `sub_099f29(pi, 0, 69, a, c)`; x1 = `sub_099f29(pi, 1, 48, a, c)`; x2 =
      `sub_099f29(pi, 2, 35, b, c)`; `sub_099f29(pi, 4, g4, b, c)`; x3 =
      `sub_099f29(pi, 3, g3, 1, c)` (in this order).
   3. y1 = `sub_090493(pi, x2, b, c)`.
   4. v = `PHY(0x6dc+o)` (read); e = `gain[0][v & 1]`; d1 = (t(c) - 16 if a != 0,
      else -16) - e; m1 = the smaller of d1 and 23 - x1.
   5. y2 = `sub_090493(pi, x3, 1, c)`.
   6. v = `PHY(0x6dc+o)` (read again); e as before; d2 = (t(c) - 16 if b != 0,
      else -16) - e; m2 = the smaller of d2 and 23 - x2.
   7. With N(m, y) = floor((m + y) / 2) and W(m, y) = -floor((-39 * m - 26 * y)
      / 64) (computed in 32 bit, result truncated to 8 bit): for 20 MHz: r1 =
      N(m1, y1), r2 = W(m2, y2) if h != 0, else N(m2, y2); for 40 and 80 MHz:
      r1 = W(m1, y1), r2 = W(m2, y2).
   8. `sub_09175a(pi, c, r1)`; `sub_091b6e(pi, c, r2)`.
6. `mod(PHY(0x19e), 0x0002, s19e & 0x0002)`.

`sub_099f29(pi, which, wanted, flag, c)` (.text+0x099f29): x =
`sub_08f086(pi, c, wanted, flag, k[0..5])` (section 9). lo = (k[3] << 7) |
(k[2] << 4) | (k[1] << 1); hi = (k[5] << 8) | (k[4] << 4) | (8 if flag != 0,
else 4). Plain writes: `PHY(0x6dc + 2*which + o)` = lo, then `PHY(0x6dd +
2*which + o)` = hi, for which = 0..4 (other values: nothing). Only for which =
0, after the two writes: `TBL(0x07)[0xf9 + c]` (16 bit) = (k[5] << 13) | (k[4]
<< 10) | (k[3] << 6) | (k[2] << 3) | k[1]. Result: x. (The code k[0] of the
external LNA is not written.)

`sub_090493(pi, x, flag, c)` (.text+0x090493): B = -66, -63, -60 and K = 25,
22, 19 for 20, 40, 80 MHz. v = `PHY(0x6dc+o)` (read). acc = B; if flag != 0:
acc = B + t(c) - `gain[0][v & 1]` * bp(c). acc = acc + `pi_ac+0x66f`. Result:
the larger (signed) of acc and -(K + x).

`sub_09175a(pi, c, r)` (.text+0x09175a; threshold of the first clip detector,
"narrow band"):

1. v = `PHY(0x6dc+o)`, w = `PHY(0x6dd+o)`, w2 = `PHY(0x6dd+o)` (three reads).
   i0 = v & 1, i1 = (v >> 1) & 7, i2 = (v >> 4) & 7, i3 = (v >> 7) & 15, i4 =
   (w >> 4) & 7, i5 = (w2 >> 8) & 7.
2. G = r + `gain[0][i0]` + `gain[1][i1]` + `gain[2][i2]` + `gain[3][i3]` +
   `gain[4][i4]` (32 bit; the table entries are signed; i3 can be up to 15
   although a stage has 10 entries: the object then reads the bytes that
   follow, the list of the next stage); if `pi_ac+0x65c` = 1: G = G - t(c).
   P = 10 * G.
3. Thresholds T = -40, -5, 20, 40, 55, 80, 100, 116. If P < -40: n = 0. If P >
   116: n = 7, and before that, if P - 116 > 20 and i4 != 0 and i5 != 7:
   `mod(PHY(0x6dd+o), 0x0070, (i4 - 1) << 4)`; `mod(PHY(0x6dd+o), 0x0700, (i5 +
   1) << 8)`. Otherwise n = index of the threshold nearest to P (the upper
   one if P is in the middle of two).
4. `mod(PHY(0x6ee+o), 0x0003, F[n])` with F = 0, 0, 1, 1, 1, 1, 2, 2.
5. One modification of `RADIO(0x045 | c << 9)` with V = 1, 0, 1, 2, 0, 3, 1, 0:
   n = 0, 1: mask 0x0080, value V[n] << 7; n = 2..5: mask 0x0300, value V[n] <<
   8; n = 6, 7: mask 0x0040, value V[n] << 6.

`sub_091b6e(pi, c, r)` (.text+0x091b6e; threshold of the second clip detector,
"wide band"):

1. v = `PHY(0x6dc+o)` (read); i1 = (v >> 1) & 7. G = r + `gain[0][0]` (taken
   as unsigned) - (`gain[1][5]` - `gain[1][i1]`); if `pi_ac+0x65c` = 1: G = G -
   t(c). P = 10 * G.
2. A = 0, 19, 35, 49, 60, 70, 80, 88, 95, 102, 109, 115; B = 0, 19, 35, 49,
   60, 70, 80, then 92, 105, 120, 130, 140 on 2.4 GHz and 96, 113, 130, 155,
   180 on 5 GHz. Three lists of 12 thresholds: L = A - 340, M = A - 280, H = B -
   220.
3. If P <= L[0]: range 0, n = 0. Else if P >= H[11]: range 2, n = 11. Else:
   range 2 with list H if P > M[11]; range 1 with list M if P >= M[0]; else
   range 0 with list L; n = index of the threshold of that list nearest to P
   (the upper one in the middle of two).
4. `mod(PHY(0x6ee+o), 0x000c, range << 2)`.
5. `mod(RADIO(R | c << 9), 0x00f0, (n + 4) << 4)` with R = 0x02c on 2.4 GHz,
   0x033 on 5 GHz.

With the synthetic SROM the traces show r1 = -41, r2 = -23 on channel 36 and
`PHY(0x6dc)` = 0x016a, `PHY(0x6dd)` = 0x0004, `TBL(0x07)[0xf9]` = 0x00b5.

### A7. sub_09a539 (.text+0x09a539, 850 bytes; name assigned: wlc_phy_desense_apply_acphy) with sub_08f84d, sub_090b77

Step 24 of the channel function, called as `sub_09a539(pi, on)` with `on` =
`pi_ac+0x670`; assignment `acphy-desense`. In all traces `on` is 0.

1. `wlapi_suspend_mac_and_wait(sh+0x20)`; s19e = `PHY(0x19e)`;
   `mod(PHY(0x19e), 0x0002, 0x0002)`.
2. `on` = 0:
   1. `sub_08f84d(pi, 0x36)`: `mod(.., 0xff00, 0x3600)` on `PHY(0x324)`,
      `PHY(0x330)`, `PHY(0x321)`, `PHY(0x32d)`, `PHY(0x32a)`, `PHY(0x336)`,
      `PHY(0x327)`, `PHY(0x333)`, in this order.
   2. `sub_090b77(pi, 0)`: `PHY(0x304)`, `PHY(0x307)`, `PHY(0x30a)`, `PHY(0x30d)`
      = 0x4e51 (plain writes).
   3. 2.4 GHz only: `PHY(0x299)` = 0x4477; `PHY(0x3c1)` = 0x0010.
3. `on` != 0 (code reading only):
   1. `sub_0909fd(pi)` (A4). bd = `pi_ac+0x669`, at most 24; od' =
      `pi_ac+0x668`, at most 48; `pi_ac+0x657` = bd; `pi_ac+0x656` = od'.
   2. `sub_090b77(pi, od' != 0)`: the four registers of 2.2 get 0x5f62 if the
      argument is not 0, else 0x4e51.
   3. od = od' - 1, or 0 if od' = 0. On 5 GHz: i = 0, q = od. On 2.4 GHz: i =
      (bd + 1) >> 1, at most 12; q = Q[i] with Q = 0, 0, 0, 0, 0, 0, 0, 3, 6, 9,
      9, 12, 12. q is limited to 12. u = od - q, limited to 0..30.
   4. The gain tables are written again where the desense values changed
      since they were last written: `sub_099658(pi, 1)` if `pi_ac+0x65c` !=
      `pi_ac+0x66e` or `pi_ac+0x658` != `pi_ac+0x66a`; `sub_099658(pi, 2)` if
      `pi_ac+0x65c` != `pi_ac+0x66e` or `pi_ac+0x659` != `pi_ac+0x66b`;
      `sub_0998cc(pi, 1)` if `pi_ac+0x65a` != `pi_ac+0x66c`; `sub_0998cc(pi, 2)`
      if `pi_ac+0x65b` != `pi_ac+0x66d`; if any of the four was called:
      `sub_09a121(pi)` (A6).
   5. For each core c whose bit is set in `sh+0xa7`: `sub_099f29(pi, 0, 69 -
      q, pi_ac+0x66e, c)` (A6): the start gain reduced by q.
   6. v = ((u * 88) >> 5) + 0x36 (8 bit), at least `pi_ac+0x042` (unsigned; 0x36
      after attach): `sub_08f41b(pi, v, 0, 0)` (carrier sense threshold,
      `acphy-desense`).
   7. w = od - 21, at least 0: `sub_08f84d(pi, ((w * 88) >> 5) + 0x36)`.
   8. 2.4 GHz only: `PHY(0x299)` = 0x4400 | E[i] with E = 0x77, 2, 3, 3, 3, 4,
      4, 4, 4, 4, 4, 4, 4; `PHY(0x3c1)` = F[i] with F = 0x10, 0x60, 0x10, 0x4c,
      0x60, 0x30, 0x40, 0x40, 0x38, 0x2e, 0x40, 0x34, 0x40.
4. `wlc_phy_aci_updsts_acphy(pi)` (no hardware access in the traces);
   `mod(PHY(0x19e), 0x0002, s19e & 0x0002)`; `wlapi_enable_mac(sh+0x20)`.

## Verification

Traces: `re-out\up-trace.txt`, `re-out\chan\chan-36.txt` (from channel 1:
band change), `chan-36_80.txt` (bandwidth change), `chan-6.txt` (band and
bandwidth change), `chan-149_40.txt` (band and bandwidth change); radio
revision 4, PHY revision 1. My own runs in `re-out\analysis\acphy-chanspec\`,
made with `run.py` (a harness on top of the unchanged tools; it writes one
trace per action and prints the state fields): `t1` (channel 40, then 36:
channel change only), `t2` (SROM with receive gain errors), `t3` (comparison of
`sub_08f086` with a model), `t4` (spur mode functions and unknown channels
called directly), `r3` (radio revision 3, PHY revision 0: channels 36, 36/80,
6, 1), `p`, `b`, `c`, `d` (`wlc_phy_chanspec_set` called directly for series
of chanspecs, which also reaches chanspecs the regulatory code of the upper
layer refuses, such as 40 MHz on 2.4 GHz), `q` (flag `pi+0x1169` set), `s` (SROM
with different values per sub-band), `v` (state fields set by the harness),
`w` (chip id faked for `sub_0995ab`), `dn` (driver not up). Tools next to
the traces: `seq.py` (listing and comparison of what a function does in a
trace), `cols.py` (several traces side by side), `depmatrix.py`, `farrow.py`,
`chk_tables.py`, `rxchk.py`. The runs `t2`, `t3` and the state checks print
their results and leave no trace file; the command lines of all runs are in
`NOTES.md` in the same directory. All my runs use the model's option that lets the
calibrations of the radio finish at once (the traces in `re-out\chan` and the
up-trace do not: their polling loops run to the timeout).

| Item | Checked how | Result |
|---|---|---|
| Order of events of `wlc_bmac_set_chanspec` with band switch | chan-36 seq 55930 (clock), 55933 (`si_iscoreup`), 55935 (interrupts off), 55937 (`wlc_setxband`: wrapper 0x2155 -> 0x0155), 55940..56196 (`sub_06656c`, no `wlc_phy_init` inside), 56197 (interrupts restored), 56201 (`wlc_phy_chanspec_set`), 64489 (power limits), 65669 (mute), 65689 (clock) | as specified |
| The same without band switch | chan-36_80 seq 65905 (clock), 65911 (`wlc_phy_chanspec_set`), 73692, 74875, 74880 | as specified: nothing between the clock and the PHY |
| Band flag of the wrapper | chan-6 seq 75246: 0x01d5 -> 0x21d5; chan-149_40 seq 86072: 0x2155 -> 0x0155 | as specified |
| `SHM(0xa0)` = chanspec | chan-36 seq 56201 (0xd024), chan-36_80 (0xe02a), chan-6 (0x1006), chan-149_40 (0xd897) | as specified |
| Both bands share one `pi` | `run.py` prints `band+0x28` of both band states | equal |
| Channel function, band change | chan-36 seq 56204..64488; `r3-0-chan-36` | steps 2, 5 (read of `SHM(0x184)`), 6, 8, 9, 12 (with pulse), 14, 16, 18, 19..24, 27..29, 31, 34 in this order; no step 7.2, 15, 17, 25, 30 |
| Channel function, bandwidth change | chan-36_80 seq 65913..73691; `r3-1-chan-36_80` | step 7.2 at seq 65928 (wrapper 0x0155 -> 0x01d5), 2 us later step 8; pulse; step 17; step 25 (seq 72899, 72913); no step 5.1, 15, 16, 30 |
| Channel function, band and bandwidth change | chan-6 seq 75512..84605 (wrapper 0x21d5 -> 0x2155), chan-149_40 seq 86338..95480 (0x0155 -> 0x0195); `r3-2-chan-6` | steps 5, 7.2, 16, 17, 18, 25 all present, order as specified |
| Channel function, channel change only | `t1-1-chan-36` (40 -> 36) | no pulse on `PHY(0x728)`, no step 5, 7, 15..18, 25, 30; all other steps |
| Channel function, INIT | up-trace seq 36942..49583 | no `wlapi_bmac_bw_set`; steps 15, 16, 17, 18, 25, 30 present (`PHY(0x00b)` reads 0 in the model, so `wlc_phy_rxcore_setstate_acphy` is called) |
| Save/restore of `PHY(0x19e)` | chan-36 seq 56207..56227 (0x03d0 read twice, 0x03d2, 0x03d3 written), 56524 (0x03d1, 0x03d0 written) | as specified |
| Delays | time stamps of the traces: chan-36_80 seq 65930/65931 (2 us), 65990/65992 (1 us), 72908/72912 | as specified (the third includes the 2 us at the end of the CCA reset) |
| TSSI threshold register | chan-36 seq 64120: `PHY(0x1641)` = 0x7f14 between the two calls of `wlc_phy_txpwrctrl_enable_acphy` | as specified |
| Unknown channel | `t4`: `sub_0a7089(pi, 0x100f)` and `(pi, 0xd0ff)` called directly | no hardware access, state unchanged; `wlc_phy_chanspec_set(pi, 0x100f)` writes only `SHM(0xa0)` |
| Signal strength correction | `t2` (rxgainerr2ga0 = 10, rxgainerr2ga1 = 3, rxgainerr5ga0 = 5,6,7,8, rxgainerr5ga1 = 1,2,30,4): `pi+0x212..` read after channel 6 (x = 44): 60, 63; channel 36 (x = 51): 41, 42; channel 149: 44, 48. Default SROM (all bits set): 0, 0 | as specified, including the channel groups and the 8 bit wrap |
| `sub_08f086` | `t3`: a model written from section 9 compared with the object for all wanted gains 0..255, both flags, both cores, on channel 1 and channel 36 (2048 calls) | no difference (result and the six codes) |
| `wlc_phy_get_chan_freq_range_acphy` | `t1`: called for all 77 channels of the table and for channel 0, `subband5gver` = 4 | as specified |
| Spur mode functions on the 4360 | `t4`: `wlc_phy_set_spurmode(pi, 5180)` -> mode 2, `CC(0x600)` read and written back (0x600), `PMU_PLLCTL[2]`, `[3]` read, `D11(0x62e)` = 0x6662, `D11(0x630)` = 6; frequency 5500 -> mode 5, same accesses; repeated call with the same mode: no access; blocked: only `pi+0x1140` is set; unblock: applied | as specified |
| PHY revision 0, radio revision 3 | `r3` runs: list of the calls made by the channel function for channel 36, 36/80, 6, 1 | the same as with revision 1/4 |
| A1, resampler tables | `farrow.py check`: the formulas against all 3 x 123 entries of `rx_farrow_tbl` and `tx_farrow_dac1_tbl` | no difference |
| A1, all steps | `p` runs (`wlc_phy_chanspec_set` called directly for 20 chanspecs: 36, 40, 52, 100, 149, 38/40 lower and upper, 62/40, 42/80 with control channel 0, 1, 3, 155/80, 1, 6, 4, 13, 14, 3/40 lower and upper), listed with `seq.py`/`cols.py` | registers, order and values as specified; between two channels of a sub-band only steps 2 and 6 change (`seq.py diff p-1.. p-2..`) |
| A1 step 3 | `q` run: `pi+0x1169` set to 1 by the harness, chanspecs 0xd836 (5270 MHz) and 0xd83e (5310 MHz) | as specified |
| A1 step 5, table A1 | `chk_tables.py` compares the rows printed in this file with the object; trace: `TBL(0x07)[0x3cd]` = 0x04e1, `[0x3dd]` = 0x0501 with `pdgain` = 4 | as specified |
| A1 step 10, `sub_0a4867` | `p-16` (channel 13) against `p-17` (channel 14) | as specified |
| A1, `wlc_phy_populate_recipcoeffs_acphy` | `p` runs: 12 + 448 + 4 entries of `TBL(0x11)`, E = 0x400200400200 with `rpcal` = 0; `s` run: `rpcal5gb0` = 0x2345 on channel 40: E = 0x73e1457037c1, `rpcal5gb1` = 0x3456 on channel 52: E = 0x70b089727ef9 | as specified (both values computed by hand from step 2) |
| Dependence on the sub-band | `s` run (SROM with different `rxgains5g*`, `rpcal5gb*` per sub-band, `pdgain5g` = 6), channels 36, 40, 52, 100, 149 compared with `seq.py diff` and `depmatrix.py` | table "What the called functions write depends on"; `TBL(0x07)[0x3cd]`, `[0x3dd]` follow table A1 (0x0293, 0x0334 for r = 1; 0x0431, 0x03d3 for r = 2) |
| Position of the control channel | `d` run (38/40 upper, then lower), `p-10`, `p-11` (42/80, position 1 and 3) | only `PHY(0x164)` and `PHY(0x30f)` differ |
| A2 | `b` runs (80 -> 20 -> 40 MHz on 5 GHz, 40 -> 20 MHz on 2.4 GHz) and `p-18`, printed side by side with `cols.py` | all values of the three columns as specified; steps 6 and 8 for PHY revision 0: `r3-1-chan-36_80` (`PHY(0x16d4)` = 0x0cc0), `r3-2-chan-6` (0x0c60, bit 7 of `PHY(0x73a)` cleared) |
| A3 | `p-0` (to 5 GHz), `p-13` (to 2.4 GHz) | as specified (`extpagain` = 0, radio revision 4: epa tables of revision 4) |
| A4 | code reading; `v` run for the scan condition; the two clock reads of `sub_092efb` in every trace | - |
| A5 | `p-0`, `c-2`, `p-1` side by side (`cols.py sub_09af05 ...`); `r3-0-chan-36` for PHY revision 0 (`PHY(0x289)`, core 0 only) | as specified; the desense values were 0 in all runs, so m = h in `sub_099658`/`sub_0998cc` (other values: code reading) |
| A6 | `rxchk.py`: models written from the text of A6 compared with the object for `sub_090493`, `sub_09175a`, `sub_091b6e`, `sub_099f29` (called directly with many arguments) and `sub_09a121` (arguments it passes to the two threshold functions), on the chanspecs 0xd024, 0x1001, 0xe02a, 0x1803, 0xd826, 0xd064, each with six states (desense byte 7, bypass, high RSSI enable, no external LNA) | 60804 comparisons, no difference |
| A7 | `p-2` (5 GHz), `p-14` (2.4 GHz) side by side, `on` = 0 | as specified; the branch `on` != 0: code reading only |
| `wlc_bmac_set_chanspec`, driver not up | `dn` run: the function called directly after attach with 0x1006 and 0xd028 | no hardware access; `wlc_hw+0x11c`, `pi+0x17e` = chanspec, band unit switched |
| Interference mode selection (section 4 step 2) | `v` run: `sh+0x84` = 5, `sh+0x88` = 6 set by the harness: `sh+0x80` = 6 on channel 36; with `sh+0x94` = 1, `sh+0x8c` = 3, `sh+0x90` = 4: 3 on channel 1, 4 on channel 36 | as specified |
| Section 5 step 20 during a scan | `v` run: `pi+0x19c` = 0x22, change from channel 36 to 40 | `pi_ac+0x8a8` = 0, `pi+0x240` stays 36, `sub_092efb` not called; with 0x20 again: pointer set, `pi+0x240` = 40 |
| Section 5 step 5.1 | `v` run: `pi_ac+0x910` = `pi_ac+0x911` = 1 and `SHM(0x184)` = 0xdead set by the harness before a band change | `SHM(0x184)` read, 0 written; leaving 5 GHz: `pi_ac+0x916` = 5; leaving 2.4 GHz: `pi_ac+0x914` = 5 |
| `sub_0995ab` | `w` run: no access and no change with chip id 0x4360; with `sh+0x3c` set to 0x4352 by the harness on channel 1: `bt_active` = 1 stores -8, -8, -8, 0 without recalculation, `bt_active` = 0 stores 0 and recalculates (`sub_09949f`, MAC suspended) | as specified |

Reference listings of everything the channel function and its callees do, one
line per register access, made from the traces with `seq.py list`:
`re-out\analysis\acphy-chanspec\ref-channel-only-40-to-36.txt`,
`ref-bandwidth-36_20-to-38_40.txt`, `ref-band-1-to-36.txt`,
`ref-init-channel-1.txt` (the values are those of the synthetic SROM).

Not checked against a trace (code reading only): the core-in-reset branch of
the band switch in `wlc_bmac_set_chanspec`; PHY table A1 for values of
`pdgain` other than 2, 4 and 6; `wlc_phy_tssivisible_thresh_acphy` for other
boards; everything said about other chips and PHY revisions.

## Open questions

* The meaning of bits 0 and 1 of `PHY(0x19e)`, of bit 8 of `PHY(0x728)` and of
  the value 0x7f00 in `PHY(0x1641)` is an interpretation.
* `pi_ac+0x32f` is written and never read; `pi_ac+0x32e` likewise (set to 0xff
  by the PHY initialisation).
* What `PHY(0x00b)` reads on the real card (step 30) is not known; the model
  returns 0. If its low three bits are 3 (three cores) the comparison value is
  7 and the call is made as in the model, because the board uses two chains.
* With the receive gain tables of the synthetic SROM the receiver reaches only
  44 dB (2.4 GHz) or 51 dB (5 GHz) of the wanted 69 dB; the correction of step
  32 then wraps around in 8 bit. Whether this happens with the values of a real
  card is not known.
* The hold flag mask 0x0206 of step 20 is interpreted from the open driver's
  flag names; which callers set the bits was not followed.
* Three different groupings of the 5 GHz channels are in use and it is not
  known whether this is intended: the sub-band of section 6 (by frequency and
  `subband5gver`), the groups of the receive gain error (section 8, by channel
  number: 48/64/128), and the choice of the front end values in `sub_09af05`
  (channel number below 100 or not; the values of the SROM for the middle of
  the band are read at attach but not used there).
* Table A1 is indexed with the SROM values `pdgain2g`/`pdgain5g` (5 bit)
  without a range check; values above 19 read behind the table. The values of
  the real card are not known (the synthetic SROM has 4), and neither are
  `rpcal*`, `rxgainerr*`, `cckdigfilttype` and `extpagain*`, which decide the
  values written by A1 and A3.
* The purpose of nearly all registers and table entries of the annex is not
  known (A2 step 2 looks like filter coefficients per bandwidth, A2 steps 10
  and 12 are entries of the RF sequence table `TBL(0x07)`): the accesses are
  exact, the meaning is not established.
* Which iovar sets `pi+0x1169` (A1 step 3) and whether the Apple software ever
  uses it was not followed; it is 0 after attach.
* `wlc_bmac_set_chanspec` calls the channel function even if the chanspec is
  unchanged for the device ids 0x43a0, 0x43a3, 0x43ae, 0x43b1; why is not
  known.
* The channel function does not check the bandwidth against the band or the
  radio: a chanspec with 40 MHz on 2.4 GHz or with a position of the control
  channel above 3 is processed as described (checked for 40 MHz on 2.4 GHz).
