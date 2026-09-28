# Transmit power of the AC-PHY of the BCM4360: gain tables, power control, target powers, idle TSSI

Status: work in progress, written function by function. Everything below was
read in the decompiler output and checked in the disassembly; what was also
checked against emulator traces is listed in "Verification". Register and
table *names* are not known; aliases in parentheses are mine.

## Scope

Chip 0x4360 (0x4352 and 43526 = 0xaa06 take the same branches unless said
otherwise), AC-PHY revision 0 or 1, radio 2069 revision 3 or 4 (major revision
0), two chains. Register access notation: `access.md`.

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_phy_txpwr_by_index_acphy` | 0x09cdf8 | 347 | original |
| `wlc_phy_set_txpwr_by_index_acphy` | 0x09ccae | 330 | original (no caller in the object) |
| `sub_09868f` | 0x09868f | 194 | assigned by `acphy-init`: `wlc_phy_get_txgain_settings_by_index_acphy` |
| `sub_09c4e4` | 0x09c4e4 | 223 | assigned: `wlc_phy_set_tx_bbmult_acphy` |
| `sub_098751` | 0x098751 | 167 | assigned: `wlc_phy_get_tx_bbmult_acphy` |
| `wlc_phy_read_txgain_acphy` | 0x0987f8 | 272 | original (no caller in the object) |
| `wlc_phy_txpwrctrl_enable_acphy` | 0x0906e6 | 211 | original |
| `sub_0906ae` | 0x0906ae | 56 | assigned: `wlc_phy_txpwrctrl_get_cur_index_acphy` |
| `sub_08f3d4` | 0x08f3d4 | 71 | assigned: `wlc_phy_txpwrctrl_set_cur_index_acphy` |
| `wlc_phy_txpwr_idx_get_acphy` | 0x0907b9 | 172 | original (no caller in the object) |
| `wlc_phy_txpwr_est_pwr_acphy` | 0x0905f0 | 190 | original |
| `wlc_phy_txpwrctrl_set_target_acphy` | 0x08fa47 | 71 | original |
| `wlc_phy_pwrctrl_shortwindow_upd_acphy` | 0x08f990 | 36 | original (no caller in the object) |
| `wlc_phy_tssivisible_thresh_acphy` | 0x08f24a | 115 | original |
| `sub_098949` | 0x098949 | 2902 | assigned: `wlc_phy_txpwrctrl_pwr_setup_acphy` |
| `sub_08ef5f` | 0x08ef5f | 69 | assigned: `wlc_phy_pdoffset_cal_acphy` |
| `wlc_phy_get_paparams_for_band_acphy` | 0x08eef3 | 108 | original (reached only for PHY revisions 2, 5, 6) |
| `sub_090381` | 0x090381 | 274 | assigned: `wlc_phy_tssifloor_to_minpwr_acphy` (reached only for PHY revisions 2, 5, 6) |
| `sub_09949f` | 0x09949f | 137 | `wlc_phy_txpower_recalc_target_acphy` (guide) |
| `sub_099528` | 0x099528 | 131 | assigned by `acphy-attach`: `wlc_phy_txpower_core_offset_set_acphy` |
| `sub_0995ab` | 0x0995ab | 94 | assigned by `acphy-chanspec`: `wlc_phy_btc_txpwr_core_offset_acphy` |
| `wlc_phy_txpower_sromlimit_get_acphy` | 0x09729e | 223 | original |
| `sub_09c161` | 0x09c161 | 298 | assigned by `acphy-radio`: `wlc_phy_txcal_coeffs_apply_acphy` (called on a band change; it does **not** load the gain table) |
| `sub_09bf99` | 0x09bf99 | 456 | assigned by `acphy-radio`: `wlc_phy_cal_txiqlo_coeffs_acphy` |
| `sub_08f9b4` | 0x08f9b4 | 147 | assigned by `acphy-init`: `wlc_phy_tssi_phy_setup_acphy` |
| `sub_0affa9` | 0x0affa9 | 486 | assigned by `acphy-init`: `wlc_phy_txpwrctrl_idle_tssi_meas_acphy` (flow in `acphy-init.md` section 6) |
| `sub_0af7f4` | 0x0af7f4 | 1973 | assigned: `wlc_phy_poll_samps_WAR_acphy` |
| `sub_093f74` | 0x093f74 | 388 | assigned: `wlc_phy_poll_samps_acphy` |
| `sub_09bbe4` | 0x09bbe4 | 559 | assigned: `wlc_phy_init_adc_read` |
| `sub_09be13` | 0x09be13 | 390 | assigned: `wlc_phy_restore_after_adc_read` |
| `sub_093ebe` | 0x093ebe | 182 | assigned: `wlc_phy_gpiosel_acphy` |
| `sub_0b1227` | 0x0b1227 | 398 | assigned by `acphy-init`: `wlc_phy_precal_txgain_acphy` (specified in `acphy-init.md` section 7) |
| `sub_0b0455` | 0x0b0455 | 3538 | assigned: `wlc_phy_precal_target_tssi_search_acphy` (PHY revisions 2, 3, 5, 6 only; summary only) |
| `sub_092ffa` | 0x092ffa | 289 | owner `acphy-cal-rx`; described in section 28 as the measurement calls it |
| `wlc_btcx_override_enable` | 0x0b31df | 370 | original (wlc_phy_cmn.c); described in section 28 |
| `wlc_phy_btcx_override_disable` | 0x0b2cf2 | 99 | original (wlc_phy_cmn.c); described in section 28 |

(The table grows as the work proceeds.)

## Overview

### Transmit gain

The transmit gain of a core is set by three things: the gain codes of the
radio and the front end (48 bit, kept in three 16 bit entries of `TBL(0x07)`
per core), and the digital scaling of the baseband signal ("bbmult", 8 bit,
kept in two entries of `TBL(0x0c)` per core). The driver has a gain table of
128 steps per band and kind of power amplifier; step 0 is the highest gain.
On a band change the channel function loads the table of the new band into
the PHY (`TBL(0x20)`, 128 entries of 48 bit; `acphy-chanspec.md` A3 step 3).
The PHY uses `TBL(0x20)` by itself when the closed loop power control runs
(the index then comes from the control loop); the driver uses it to set a
fixed gain "by index": it reads entry i of `TBL(0x20)` back from the PHY,
splits it and writes the pieces to `TBL(0x07)` and `TBL(0x0c)`
(`wlc_phy_txpwr_by_index_acphy`).

### Closed loop power control

The PHY hardware measures the output of the power amplifier of each core with
a power detector while it transmits (the detector reading is called TSSI),
converts the reading to a power with a look-up table that the driver
computes, compares it with a target and moves the index into the gain table
`TBL(0x20)` of the core accordingly. The driver's part:

* **Set-up** (`sub_098949`, section 13), run on every recalculation of the
  target powers (after every channel change, `wlc_phy_txpower_limit_set` ->
  `sub_0b8ca4` -> `sub_09949f`): TSSI on, control loop parameters (start index,
  delay between start of frame and sampling, averaging), the target power of
  each core in quarter dBm, the **estimated power tables** `TBL(0x40)`,
  `TBL(0x60)`, `TBL(0x80)` (core 0, 1, 2; 128 entries: power in quarter dBm as a
  function of the TSSI) computed from the three PA parameters of the core and
  sub-band, and the table of power offsets per bandwidth `TBL(0x21)` from the
  `pdoffset*` variables.
* **Enable/disable** (`wlc_phy_txpwrctrl_enable_acphy`, section 7): the three
  enable bits 13..15 of `PHY(0x70)`. When the loop is switched off the index
  it had reached is saved per core, when it is switched on again the loop
  starts from the saved index.
* **Idle TSSI** (`sub_0affa9`/`sub_0af7f4`): the reading of the detector
  without signal, measured at initialisation and programmed as the offset of
  the measurement in `PHY(0x645 + c * 0x200)`.
* **Threshold** `PHY(0x641 + c * 0x200)`, written by the channel function
  (`acphy-chanspec.md` section 5 step 28) with the value of
  `wlc_phy_tssivisible_thresh_acphy`: below this power the detector reading
  is not used (interpretation of the name).

With the power control on, the index registers of `TBL(0x07)`/`TBL(0x0c)`
written by `wlc_phy_txpwr_by_index_acphy` are not what determines the gain
(interpretation; the driver always switches the control off around setting
a gain by index in the calibrations).

## Data

### Transmit gain tables

Objects of 768 bytes in `.data`: 128 entries of three 16 bit words w0, w1, w2
(in this order in memory, little endian; written to the PHY as one 48 bit
entry with w0 as the lowest word). Extract with
`python blob.py data acphy_txgain_epa_2g_2069rev4 384 2`.

| Bits of the 48 bit entry | Word | Content | Goes to (when applied by index) |
|---|---|---|---|
| 0..7 | w0 bits 0..7 | bbmult: digital scaling of the baseband signal | `TBL(0x0c)[0x63 + 4c]` and `TBL(0x0c)[0x73 + 4c]` |
| 8..23 | w0 bits 8..15, w1 bits 0..7 | gain code, low part | `TBL(0x07)[0x100 + c]` |
| 24..39 | w1 bits 8..15, w2 bits 0..7 | gain code, middle part | `TBL(0x07)[0x103 + c]` |
| 40..47 | w2 bits 8..15 | gain code, high part | `TBL(0x07)[0x106 + c]` (upper byte 0) |

c = core. Which bits of the gain code drive which stage of the radio is not
visible in the driver (the PHY hardware passes the code to the radio).

Which table is loaded: see `acphy-chanspec.md` A3 step 3 (confirmed): by band,
by `extpagain2g`/`extpagain5g` = 2 (internal PA: the `ipa` tables) and by the
radio revision (3: `..._2069rev0`, 4: `..._2069rev4`; 2.4 GHz with radio
revision 4 and `pi_ac+0x349` = 1: `acphy_txgain_epa_2g_2069rev4_id1`).

### PHY registers of the power control

Addresses of per core registers: core c uses address + c * 0x200 (0x640,
0x840, 0xa40). Aliases and meanings are mine, derived from how the driver
uses the fields (unverified against a register description).

| Register | Bits | Use |
|---|---|---|
| `PHY(0x70)` (power control command) | 15, 14, 13 | the three enable bits of the hardware power control; always written together (mask 0xe000); bit 15 alone is cleared during the set-up |
| | 11 | cleared by the set-up (purpose unknown; interpretation: "transmit IQ coefficients follow the power index") |
| | 10 | set by the set-up for PHY revision 1, cleared for revision 0 (purpose unknown; interpretation: "LO leakage coefficients follow the power index") |
| | 8 | set by the set-up for radio revision 4 (and 8), cleared for radio revision 3 (purpose unknown) |
| `PHY(0x71)` (power control timing) | 0..7 | delay from the start of the frame to the TSSI sample: 150, 200 or 220 (unit unknown) |
| | 8..10 | 4, written twice by the set-up; `wlc_phy_pwrctrl_shortwindow_upd_acphy` writes 1 or 4 (interpretation: log2 of the number of frames averaged) |
| `PHY(0x72)` (TSSI mode) | 0 | TSSI enable, set by the set-up |
| | 14 | slope of the detector is positive; always 1 after the set-up |
| `PHY(0x640 + c * 0x200)` (status) | 0..7 | estimated power (quarter dBm), valid if bit 15 is set |
| | 8..14 | gain table index the control loop is at |
| | 15 | estimate valid |
| `PHY(0x641 + c * 0x200)` | all | 0x7f00 + TSSI visibility threshold; written as `PHY(0x1641)` by the channel function |
| `PHY(0x642 + c * 0x200)` (status) | 0..7 | second power estimate (interpretation: estimate after the offsets of `TBL(0x21)`), valid if bit 8 is set |
| | 8 | valid |
| `PHY(0x644 + c * 0x200)` | 0..6 | gain table index the control loop starts from |
| `PHY(0x645 + c * 0x200)` | 0..9 | idle TSSI (two's complement, 10 bit) |
| `PHY(0x646 + c * 0x200)` | 0..7 | target power in quarter dBm (signed 8 bit) |

### PHY tables of the power control

| Table | Entries | Width | Content |
|---|---|---|---|
| `TBL(0x20)` | 128 | 48 | transmit gain table of the band (see above) |
| `TBL(0x40)`, `TBL(0x60)`, `TBL(0x80)` | 128 | 16 | estimated power of core 0, 1, 2: entry t = power in quarter dBm (signed 8 bit in the low byte, high byte 0) for the TSSI value t; computed by `sub_098949`. The static table set of the initialisation puts other contents there first (`acphy-init.md`, "Static table set"), which are overwritten by the first target power recalculation. |
| `TBL(0x21)` | 24 | 32 | power detector offsets: byte c of an entry = offset for core c (signed 4 bit value extended to 8 bit); computed by `sub_098949` from `pdoffset40ma<c>`, `pdoffset80ma<c>`, `pdoffset2g40ma<c>` |
| `TBL(0x07)[0x100 + c]`, `[0x103 + c]`, `[0x106 + c]` | 1 each | 16 | gain code of core c in use when the gain is set by index |
| `TBL(0x0c)[0x63 + 4c]`, `[0x73 + 4c]` | 1 each | 16 | bbmult of core c |

### State used

| Field | Type | Content |
|---|---|---|
| `pi+0x216 + c` | s8 | highest target power over all rates of core c in quarter dBm; computed by `sub_0b8ca4` (`phy-cmn`) before it calls `sub_09949f` |
| `pi+0x21a + c` | s8 | lowest target power of core c (not used by the AC-PHY code of this specification) |
| `pi+0x1c8` | pointer | power-per-rate object with the offset of every rate below the highest target, handed to the MAC by `sub_09949f` |
| `pi+0xc37` | u8 | lowest target power allowed in dBm: 1 (PHY revision 3: 5), set at attach |
| `pi+0xfa0` | u8 | power control is enabled (last argument 0 or 1 of `wlc_phy_txpwrctrl_enable_acphy`) |
| `pi+0x1d9`, `pi+0x1de` | u8 | "TSSI slope is positive" for 2.4 GHz and 5 GHz; written only by the SROM readers of SROM revisions 8 and 9 (`wlc_phy_txpwr_srom8_read`, `wlc_phy_txpwr_srom9_read`), never on the path of this card (SROM revision 11): 0 |
| `pi+0xe04 + 10c + 2b`, `pi+0xe2c + ..`, `pi+0xe54 + ..` | s16 | PA parameters a1, b0, b1 of core c and band index b (0 = 2.4 GHz, 1..4 = 5 GHz sub-bands): elements 0, 1, 2 of `pa2ga<c>`, elements 3k, 3k+1, 3k+2 of `pa5ga<c>` (`acphy-attach.md`) |
| `pi+0xe90 + 2c`, `pi+0xe98 + 2c` | u16 | `pdoffset40ma<c>`, `pdoffset80ma<c>`: four fields of 4 bit, one per 5 GHz sub-band (bits 0..3 sub-band index 1, 4..7 index 2, 8..11 index 3, 12..15 index 4) |
| `pi+0xea0 + c` | u8 | `pdoffset2g40ma<c>` (4 bit) |
| `pi+0xea4` | u8 | `pdoffset2g40mvalid` |
| `pi_ac+0x010 + c` | s8 | gain table index last set by `wlc_phy_txpwr_by_index_acphy` (0x40 after attach) |
| `pi_ac+0x410`, `pi_ac+0x411` | u8 | `pdgain2g`, `pdgain5g` |
| `pi_ac+0x45a + c` | u8 | gain table index the control loop had reached when it was last switched off; 0x80 = none (value after attach) |
| `sh+0xa5` | u8 | mask of the cores (`hw_phyrxchain` in `acphy-attach.md`) |

### Record "transmit gain setting" (10 bytes)

Used by `sub_09868f` and its callers (the calibration state has one per core
at `(pi+0xf58) + 0x92 + 10c`).

| Offset | Size | Content |
|---|---|---|
| 0 | u16 | gain code, low part (bits 8..23 of the table entry) |
| 2 | u16 | gain code, middle part (bits 24..39) |
| 4 | u16 | gain code, high part (bits 40..47) |
| 6 | u16 | not written by `sub_09868f` |
| 8 | u16 | bbmult (bits 0..7 of the entry) |

## Procedures

`mod(reg, mask, value)` is read-modify-write and always writes. "For each core
c" means c = 0 .. N-1 in rising order, N = `pi+0x168` (2 on this board).

"Table access bracket": many functions below save bit 1 of `PHY(0x19e)`, set
it, access tables and restore it. Written out: s = read `PHY(0x19e)`;
`mod(PHY(0x19e), 0x0002, 0x0002)`; ...; `mod(PHY(0x19e), 0x0002, s & 0x0002)`.
The brackets nest (every function does its own read and restore).

### 1. sub_09868f (.text+0x09868f, name assigned by `acphy-init`: wlc_phy_get_txgain_settings_by_index_acphy)

Purpose: read one step of the gain table that is loaded in the PHY.
Inputs: `pi`, `out` (record of 10 bytes, see "Data"), index i (signed 8 bit,
sign extended to the table offset). No result.

1. Table access bracket around step 2.
2. Read one entry of 48 bit: `TBL(0x20)[i]` -> words w0, w1, w2 (lowest first).
3. `out+8` = w0 & 0x00ff; `out+0` = (w0 >> 8) | ((w1 & 0xff) << 8); `out+2` =
   (w1 >> 8) | ((w2 & 0xff) << 8); `out+4` = w2 >> 8. `out+6` is not touched.

Callers: `wlc_phy_txpwr_by_index_acphy`, `wlc_phy_set_txpwr_by_index_acphy`,
`sub_0b1227`, `sub_0b0455`, `sub_0ad89a`.

### 2. sub_09c4e4 (.text+0x09c4e4, name assigned: wlc_phy_set_tx_bbmult_acphy)

Purpose: set the digital scaling of the baseband signal of one core.
Inputs: `pi`, pointer to a 16 bit value m, core c (16 bit; 0..3). No result.

1. Table access bracket around steps 2 and 3.
2. `TBL(0x0c)[0x63 + 4c]` = m (one entry of 16 bit).
3. `TBL(0x0c)[0x73 + 4c]` = m.

(The offsets are taken from two arrays {0x63, 0x67, 0x6b, 0x6f} and {0x73,
0x77, 0x7b, 0x7f} indexed by the core.)

### 3. sub_098751 (.text+0x098751, name assigned: wlc_phy_get_tx_bbmult_acphy)

Inputs: `pi`, pointer to a 16 bit result, core c. Table access bracket around:
read one entry of 16 bit `TBL(0x0c)[0x63 + 4c]` into the result.

### 4. wlc_phy_txpwr_by_index_acphy (.text+0x09cdf8, name original)

Purpose: set the transmit gain of the cores selected to step i of the gain
table. Inputs: `pi`, core mask (8 bit), index i (signed 8 bit). No result.

1. Table access bracket around step 2.
2. For each core c whose bit is set in the mask:
   1. `sub_09868f(pi, rec, i)` (section 1; with its own bracket).
   2. `TBL(0x07)[0x100 + c]` = `rec+0`; `TBL(0x07)[0x103 + c]` = `rec+2`;
      `TBL(0x07)[0x106 + c]` = `rec+4` (three transfers of one 16 bit entry).
   3. `sub_09c4e4(pi, &rec+8, c)` (section 2).
   4. `pi_ac+0x010 + c` (s8) = i.

Callers: the channel function (`acphy-chanspec.md` step 27, with the index
stored in `pi_ac+0x010 + c`), `sub_09cf53`, `sub_0ad89a`.

### 5. wlc_phy_set_txpwr_by_index_acphy (.text+0x09ccae, name original)

The same as section 4 without step 2.4 (the index is not stored). No caller
in the object.

### 6. wlc_phy_read_txgain_acphy (.text+0x0987f8, name original)

No caller in the object. Table access bracket around: for each core c: read
`TBL(0x07)[0x100 + c]`, `[0x103 + c]`, `[0x106 + c]` (16 bit each) and the
bbmult with `sub_098751` into a local record per core. The values are not
used (a debug print was compiled out).

### 7. wlc_phy_txpwrctrl_enable_acphy (.text+0x0906e6, name original)

Purpose: switch the hardware power control on or off. Inputs: `pi`, `ctrl`
(8 bit: 0 = off, anything else = on). No result.

1. If `ctrl` < 2 (unsigned): `pi+0xfa0` = `ctrl`.
2. `ctrl` = 0:
   1. v = read `PHY(0x70)`. If (v & 0xe000) = 0xe000 (the control is on): for
      each core c: `pi_ac+0x45a + c` = `sub_0906ae(pi, c)` (the index the loop
      is at, section 8).
   2. `mod(PHY(0x70), 0xe000, 0)`.
3. `ctrl` != 0:
   1. `mod(PHY(0x70), 0xe000, 0xe000)`.
   2. For each core c: i = `pi_ac+0x45a + c`; if i != 0x80:
      `sub_08f3d4(pi, i, c)` (section 9: start index of the loop).

Callers: the channel function (off, then the saved state; `acphy-chanspec.md`
section 5 step 28), `sub_09949f` (section 17), `wlc_phy_cals_acphy`, `sub_0925cc`,
`wlc_phy_init_test_acphy`, `wlc_phy_txpower_set` (wlc_phy_cmn.c).

Note: `pi_ac+0x45a + c` is 0x80 after attach and gets another value only in
step 2.1, that is when the control is switched off while it was on.

### 8. sub_0906ae (.text+0x0906ae, name assigned: wlc_phy_txpwrctrl_get_cur_index_acphy)

Inputs: `pi`, core c (8 bit). Result (8 bit): for c = 0, 1, 2: bits 8..14 of
`PHY(0x640 + c * 0x200)` (one read; (value & 0x7f00) >> 8). Any other c: 0,
no access.

### 9. sub_08f3d4 (.text+0x08f3d4, name assigned: wlc_phy_txpwrctrl_set_cur_index_acphy)

Inputs: `pi`, index i (8 bit), core c (8 bit). For c = 0, 1, 2:
`mod(PHY(0x644 + c * 0x200), 0x007f, i)`. Any other c: nothing.

### 10. wlc_phy_txpwrctrl_set_target_acphy (.text+0x08fa47, name original)

Inputs: `pi`, power p (8 bit, quarter dBm), core c (8 bit). For c = 0, 1, 2:
`mod(PHY(0x646 + c * 0x200), 0x00ff, p)`. Any other c: nothing. Only caller:
`sub_098949`.

### 11. wlc_phy_txpwr_idx_get_acphy (.text+0x0907b9, name original)

Input: `pi`. Result: 32 bit, byte c = gain table index of core c (bytes of
cores that do not exist are 0). No caller in the object.

1. v = read `PHY(0x70)`.
2. If (v & 0xe000) = 0xe000: for each core c: index = `sub_0906ae(pi, c)`.
   Otherwise: index = `pi_ac+0x010 + c` (taken as unsigned 8 bit).

### 12. wlc_phy_txpwr_est_pwr_acphy (.text+0x0905f0, name original)

Purpose: read the power estimates of the hardware. Inputs: `pi`, `out1`,
`out2` (arrays of one byte per core). Caller: `wlc_phy_get_est_pout`
(wlc_phy_cmn.c). For each core c:

1. v = read `PHY(0x642 + c * 0x200)`. If bit 8 of v is set: read the register
   a second time, `out1[c]` = low byte of the second value. Else `out1[c]` = 0.
2. v = read `PHY(0x640 + c * 0x200)`. If bit 15 of v is set: read the register
   a second time, `out2[c]` = low byte of the second value. Else `out2[c]` = 0.

### 13. sub_098949 (.text+0x098949, name assigned: wlc_phy_txpwrctrl_pwr_setup_acphy)

Purpose: set up the closed loop power control for the current channel: loop
parameters, target powers, estimated power tables, detector offsets.
Input: `pi` (chanspec `pi+0x17e`, targets `pi+0x216 + c`). No result. Only
caller: `sub_09949f`. The function leaves bit 15 of `PHY(0x70)` cleared; the
caller switches the control on again.

Notation: is2g = ((`pi+0x17e` & 0xc000) = 0); rr = radio revision `pi+0x16c`;
band = `wlc_phy_get_chan_freq_range_acphy(pi, 0)` (0 = 2.4 GHz, 1..4 = 5 GHz
sub-band, `acphy-chanspec.md` section 6).

1. s = read `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`.
2. `mod(PHY(0x72), 0x0001, 0x0001)`.
3. `mod(PHY(0x70), 0x8000, 0)`.
4. `mod(PHY(0x70), 0x0100, x)` with x = 0x0100 if rr is 4 or 8 (or the PHY
   revision is 3), else 0. So: radio revision 4: bit set; radio revision 3:
   bit cleared.
5. PA parameters, for each core c: a1[c], b0[c], b1[c] = the s16 values at
   `pi+0xe04 + 10c + 2 band`, `pi+0xe2c + 10c + 2 band`, `pi+0xe54 + 10c + 2 band`.
   If a1[c] is 0 (SROM without PA parameters) all three are replaced:

   | Core | a1 | b0 | b1 |
   |---|---|---|---|
   | 0 | -183 (0xff49) | 4825 (0x12d9) | -615 (0xfd99) |
   | 1 | -172 (0xff54) | 4626 (0x1212) | -631 (0xfd89) |
   | 2 | -173 (0xff53) | 4535 (0x11b7) | -576 (0xfdc0) |

   (PHY revisions 2, 3, 5, 6 with `pi_ac+0x348`: parameters of other cores
   are used in addition. Not for revisions 0 and 1.)
6. Targets, for each core c: t[c] = the larger of `pi+0x216 + c` and
   m = `pi+0xc37` << 2 (both as signed 8 bit; m = 4, that is 1 dBm).
   (PHY revisions 2, 5, 6: also limited by `sub_090381`, section 15.)
7. `mod(PHY(0x72), 0x4000, (x << 14) & 0xffff)` with x = `pi+0x1d9` if is2g,
   else `pi+0x1de` (0 on this card); then `mod(PHY(0x72), 0x4000, 0x4000)`.
8. `mod(PHY(0x70), 0x8000, 0)`.
9. Start index: i0 = 20 if rr is 4 or 8, else 50. For each core c:
   `mod(PHY(0x644 + c * 0x200), 0x007f, i0)`.
10. g = `pi_ac+0x410` (`pdgain2g`) if is2g, else `pi_ac+0x411` (`pdgain5g`).
    d = 200 if g > 4; 220 if g = 4; 150 if g < 4. `mod(PHY(0x71), 0x00ff, d)`.
11. `mod(PHY(0x71), 0x0700, 0x0400)`, two times.
12. `mod(PHY(0x70), 0x0800, 0)`.
13. `mod(PHY(0x70), 0x0400, x)` with x = 0x0400 if the PHY revision is 1, else 0.
14. For c = N-1 down to 0 (falling order): `mod(PHY(0x646 + c * 0x200),
    0x00ff, t[c])`.
15. Estimated power tables. For each core c (rising order) compute 128
    values and write them in one transfer of 128 entries of 16 bit, offset
    0, to `TBL(0x40)` (c = 0), `TBL(0x60)` (c = 1), `TBL(0x80)` (c = 2):

    ```
    for t = 0 .. 127:
        den = 32768 + a1[c] * t
        num = 512 * b0[c] + 32 * b1[c] * t
        p   = (num + den / 2) / den        # 32 bit signed, both divisions
                                           # truncate toward zero
        if p < -8:  p = -8
        if p > 127: p = 127
        entry[t] = p & 0xff                # 16 bit entry, high byte 0
    ```

    (The object computes den and num incrementally by adding a1 and 32 * b1;
    that is the same.) Fixed point: t is the table index (7 bit;
    interpretation: the 10 bit TSSI reading after the idle TSSI correction,
    divided by 8); p is the power in quarter dBm, rounded to the nearest. In
    real numbers: P[dBm] = (b0 / 256 + t * b1 / 4096) / (1 + t * a1 / 32768):
    b0 is in units of 1/256 dBm, b1 in 1/4096 dBm per step of t, a1 in 2^-15
    per step of t. Example (synthetic SROM, `pa2ga0` = 0xff26, 0x1a96, 0xfcc0:
    a1 = -218, b0 = 6806, b1 = -832): entry 0 = 0x6a (26.5 dBm), entry 64 =
    0x5f, entry 127 = 0x14 (5 dBm). With the default parameters of core 0:
    entry 0 = 0x4b, entry 64 = 0x3a, entry 127 = 0xfe (-2).
16. Detector offset table: 24 entries of 32 bit, all 0 except:

    | Band | Condition | Entries | Value |
    |---|---|---|---|
    | 5 GHz | always | 1, 5, 6 | W40 & 0xffffff |
    | 5 GHz | always | 10 | W80 & 0xffffff |
    | 2.4 GHz | `pi+0xea4` (`pdoffset2g40mvalid`) is not 1 | 5 | W2G & 0xffffff |
    | 2.4 GHz | `pi+0xea4` is 1 | none | |

    W40 = the OR over all cores c < N whose bit is set in `sh+0xa5` of
    `sub_08ef5f(.., pi+0xe90 + 2c, band, c)` (section 14), that is byte c of
    W40 = the 4 bit field of `pdoffset40ma<c>` for the sub-band, sign extended
    to 8 bit. W80 the same with `pi+0xe98 + 2c` (`pdoffset80ma<c>`), W2G with
    the byte `pi+0xea0 + c` (`pdoffset2g40ma<c>`; band = 0 selects bits 0..3).
    Then one transfer: `TBL(0x21)[0..23]` = the 24 entries.
    (Chip 0x4335 on 2.4 GHz with 20 MHz: other entries from `pdoffsetcckma0`
    or `rpcal2g`. PHY revisions 2, 5, 6 with `pi_ac+0x348`: entry 5 is 0 on
    5 GHz. Not for the 4360.)
17. `mod(PHY(0x19e), 0x0002, s & 0x0002)`.

Notes:

* Nothing guards the division of step 15: a1 <= -259 makes den zero or
  negative for high t (den = 0 raises a divide error in the object: for
  instance a1 = -512 at t = 64). The PA parameters of the defaults and of the
  synthetic SROM give den > 0 for all t.
* Note the inverted sense in step 16: the 2.4 GHz offsets are used when
  `pdoffset2g40mvalid` is **not** 1.
* What the 24 entries of `TBL(0x21)` stand for (which bandwidth and rate
  group each index belongs to) is not visible in the driver.
* The radio revision decides steps 4 and 9; the PHY revision step 13.

### 14. sub_08ef5f (.text+0x08ef5f, name assigned: wlc_phy_pdoffset_cal_acphy)

Inputs: accumulator (32 bit), value v (16 bit), band (8 bit, the sub-band
index), core c (8 bit). Result: 32 bit. No access.

1. n = (v >> 4) & 0xf if band = 2; (v >> 8) & 0xf if band = 3; (v >> 12) & 0xf
   if band = 4; else v & 0xf.
2. If n >= 8: n = n | 0xf0 (sign extension of a 4 bit number to 8 bit).
3. Return accumulator | (n << (8 * c)).

### 15. sub_090381 and wlc_phy_get_paparams_for_band_acphy (.text+0x090381, .text+0x08eef3)

Not reached on the 4360: `sub_090381` (name assigned:
`wlc_phy_tssifloor_to_minpwr_acphy`) is called by `sub_098949` only for PHY
revisions 2, 5, 6 and by `sub_09949f` only for PHY revision 2, and
`wlc_phy_get_paparams_for_band_acphy` only by `sub_090381`.

`wlc_phy_get_paparams_for_band_acphy(pi, &a1, &b0, &b1)`: band =
`wlc_phy_get_chan_freq_range_acphy(pi, 0)`; for each core c, if band < 5: the
three outputs = the s16 at `pi+0xe04 + 10c + 2 band`, `pi+0xe2c + ..`,
`pi+0xe54 + ..` (every core overwrites the outputs: the values of the last
core remain).

`sub_090381(pi)` returns the power (quarter dBm, 8 bit) that belongs to the
lowest usable TSSI: f = `tssifloor` of the band (s16 at `pi+0xea6 + 2 band +
10c`, last core wins); i = `PHY(0x645)` & 0x3ff (idle TSSI of core 0); v = i
+ 0x200 if i < 0x200, else i - 0x1ff; v = v + 4; f = the larger of f and v;
t = f >> 3; p = ((b1 * t + 16 * b0) * 32 + den / 2) / den with den = a1 * t
+ 32768; p is limited to >= -8, and then, compared as an unsigned byte, to
<= 127 (so a negative p becomes 127).

### 16. wlc_phy_pwrctrl_shortwindow_upd_acphy (.text+0x08f990, name original), wlc_phy_tssivisible_thresh_acphy (.text+0x08f24a, name original)

`wlc_phy_pwrctrl_shortwindow_upd_acphy(pi, on)`: `mod(PHY(0x71), 0x0700, on ?
0x0100 : 0x0400)`. No caller in the object.

`wlc_phy_tssivisible_thresh_acphy(pi)`: result 8 bit, no access; the value
for PHY revisions 0 and 1 is given in `acphy-chanspec.md` (table after section
5; confirmed): 0x26 if the 16 bit value at `pi_ac+0x342` is 0x0203, else 0x14
for chip 0x4360 with board type 0x137 or 0x117, else 0x16 for chip 0x4360 with
board type 0x134 or 0x112 and a channel number above 148, else 0x18. (PHY
revision 3: 0x1c; other revisions: 0x80.) Callers: the channel function and
`wlc_phy_tssivisible_thresh` (wlc_phy_cmn.c), which the MAC calls when it
builds the transmit headers.

### 17. sub_09949f (.text+0x09949f, name by the guide: wlc_phy_txpower_recalc_target_acphy)

Purpose: apply new target powers. Input: `pi`. No result. Slot 0x40 of the
function pointer table; called by `sub_0b8ca4` (wlc_phy_cmn.c, at the end of
`wlc_phy_txpower_limit_set`, `wlc_phy_txpower_set` and others), by
`wlc_phy_neg_txpower_set` and by `sub_099528`. The callers have suspended the
MAC.

What it gets from `sub_0b8ca4` (specification `phy-cmn`), all in quarter dBm:

* `pi+0x216 + c` (s8): the highest target power of core c over all rates of
  the current bandwidth: the minimum of the user's target (`pi+0x1d6`), of
  the board limit (section 19) minus `txpwrbckof` (`pi+0x10a0`, 6 = 1.5 dB)
  and of the regulatory limit handed in by the MAC, per rate; then the
  maximum over the rates.
* `pi+0x1c8`: a power-per-rate object (`wlc_ppr.c`) of the current bandwidth
  that holds for every rate the **offset** (highest target of the core minus
  target of the rate, maximum over the cores; >= 0).

Steps:

1. (PHY revision 2 only: `sub_090381` per core with a `tssifloor`; the result
   is not used. Not for revisions 0, 1.)
2. `wlapi_high_update_txppr_offset(sh+0x20, pi+0x1c8)`: the offsets per rate
   are handed to the MAC (`wlc_update_txppr_offset`, specification of the
   MAC): the AC-PHY code writes no per rate power to a PHY register. Seen in
   `wlc_update_txppr_offset` (read only as far as needed to know where the
   data goes): every offset is halved (unit half a dB) and limited to 0..31;
   the offsets of the basic rates are stored in the rate table of the
   shared memory (bits 3..8 of one word per rate). That the offset of the
   other frames travels in the transmit header of each frame is an
   interpretation (unverified). In the traces this call writes
   `SHM(0x5d4..0x5de)` and twelve rate table entries.
3. `sub_098949(pi)` (section 13): loop set-up with the targets `pi+0x216 + c`.
4. `wlc_phy_txpwrctrl_enable_acphy(pi, pi+0xfa0)` (section 7): back to the
   state it had (the set-up has cleared bit 15 of `PHY(0x70)`).

So of the per rate power offsets of the SROM nothing reaches a PHY register
directly: the PHY gets one target per core (`PHY(0x646 + c * 0x200)`), the
rates are told apart by the offset field of the transmit header.

### 18. sub_099528 (.text+0x099528, name assigned by `acphy-attach`: wlc_phy_txpower_core_offset_set_acphy), sub_0995ab (.text+0x0995ab, name assigned by `acphy-chanspec`: wlc_phy_btc_txpwr_core_offset_acphy)

Described in `acphy-attach.md` ("Function pointer table") and
`acphy-chanspec.md` section 10; confirmed. Addition: the offsets stored in
`pi_ac+0x456 + c` are read only by the getter `sub_092e67`; no function of the
object applies them to a target power or a register. So the only effect of
`sub_099528` is the recalculation `sub_09949f` when a value changed. `sub_0995ab`
does nothing unless the chip is 0x4352.

### 19. wlc_phy_txpower_sromlimit_get_acphy (.text+0x09729e, name original)

Purpose: the highest power the board allows for every rate on a channel
(from the SROM). Inputs: `pi`, chanspec (16 bit), power-per-rate object
`out` (made by the caller for the bandwidth of the chanspec), core c (8 bit).
No result, no hardware access. Caller: `wlc_phy_txpower_sromlimit`
(wlc_phy_cmn.c), which before that stores `pi+0xc37` << 2 (the lowest power
allowed, 4 = 1 dBm) in the byte its third argument points to.

1. band = `wlc_phy_get_chan_freq_range_acphy(pi, chanspec & 0xff)`.
2. m = the smallest of the three bytes `pi+0xe7c + band`, `pi+0xe81 + band`,
   `pi+0xe86 + band` (`maxp` of cores 0, 1, 2 for the band; unsigned compare;
   always three cores, so on a board whose SROM has no `maxp` for core 2 m
   is 0).
3. g = band if band < 4, else band - 1 (group of the `*po` variables: 0 =
   2.4 GHz, 1 = 5 GHz low, 2 = mid, 3 = high; sub-band indices 3 and 4 both
   use "high").
4. `wlc_phy_txpwr_apply_srom11(pi, g, chanspec, m, out)` (wlc_phy_cmn.c,
   specification `phy-cmn`): fills `out` with m minus the offset of each rate
   from the `*po` variables.
5. d = (`pi+0xe7c + 5c + band`) - m as signed 8 bit. If d > 0:
   `ppr_plus_cmn_val(out, d)` (adds d to every rate).
6. If band < 5: `ppr_apply_max(out, pi+0xe7c + 5c + band)` (signed 8 bit: no
   rate above `maxp` of the core).

The result per rate is `maxp` of the core minus the offset of the rate, in
quarter dBm.

### 20. sub_09c161 (.text+0x09c161, name assigned by `acphy-radio`: wlc_phy_txcal_coeffs_apply_acphy)

Purpose: program a set of transmit calibration results (IQ and LO leakage
compensation of the transmitter, IQ compensation of the receiver) for every
core. It does not touch the gain table `TBL(0x20)`: that table is written by
`sub_09e378` itself (`acphy-chanspec.md` A3 step 3, selection rule there and
in "Data" above). Inputs: `pi`, `set` = array of one record of 14 bytes per
core. No result. Callers: `sub_09e378` on every band change with a buffer of
56 zero bytes (so **a band change resets the calibration results to 0**; the
calibrations of the new band, specification `acphy-cal-tx`, write the real
values later) and `wlc_phy_scanroam_cache_cal_acphy` (with saved results).

Record per core (14 bytes):

| Offset | Size | Goes to |
|---|---|---|
| 0, 2 | u16, u16 | `TBL(0x0c)[0x60 + 4c]`, `[0x61 + 4c]` (interpretation: transmit IQ compensation coefficients a and b for OFDM) |
| 4 | u16 | `TBL(0x0c)[0x62 + 4c]` (interpretation: digital LO leakage compensation d for OFDM) |
| 6, 7, 8, 9 | u8 each | `RADIO(0x002 \| c << 9)`, `RADIO(0x003 \| c << 9)`, `RADIO(0x004 \| c << 9)`, `RADIO(0x005 \| c << 9)` (interpretation: LO leakage compensation of the radio, e and f, I and Q) |
| 10 | u16 | `PHY(0x6a0 + c * 0x200)` (interpretation: receive IQ compensation a) |
| 12 | u16 | `PHY(0x6a1 + c * 0x200)` (receive IQ compensation b) |

Steps, for each core c (rising order), r = `set + 14c`:

1. `sub_09bf99(pi, 1, copy of the two u16 at r+0 and r+2, 8, c)` (section 21):
   table access bracket, one transfer of two 16 bit entries to
   `TBL(0x0c)[0x60 + 4c]`.
2. `sub_09bf99(pi, 1, r+4, 9, c)`: bracket, one 16 bit entry to
   `TBL(0x0c)[0x62 + 4c]`.
3. Plain writes `RADIO(0x002 | c << 9)` = byte r+6, `RADIO(0x003 | c << 9)` =
   byte r+7, `RADIO(0x004 | c << 9)` = byte r+8, `RADIO(0x005 | c << 9)` = byte
   r+9 (each zero extended to 16 bit).
4. Plain writes `PHY(0x6a0 + c * 0x200)` = u16 at r+10, `PHY(0x6a1 + c * 0x200)`
   = u16 at r+12.

The entries `TBL(0x0c)[0x63 + 4c]` (bbmult, section 2) and the entries for the
11b rates `TBL(0x0c)[0x70 + 4c ..]` are not written by this function.

### 21. sub_09bf99 (.text+0x09bf99, name assigned by `acphy-radio`: wlc_phy_cal_txiqlo_coeffs_acphy)

Purpose: read or write a group of calibration coefficients, in the PHY or in
the driver's calibration state. Inputs: `pi`, mode (8 bit: 0 = read, other =
write, 2 = write to a second place for selections 12..15), `data` (array of
16 bit values), selection k (8 bit), core c (8 bit). No result.

Selections (table of 20 x 3 bytes at `.rodata+0x2cc860`: number of values n,
first offset o, distance between cores d):

| k | n | o | d | Where | Interpretation |
|---|---|---|---|---|---|
| 0 | 2 | 0x40 | 8 | `TBL(0x0c)` | start values of the calibration: a, b |
| 1, 2, 3 | 1 | 0x43, 0x44, 0x45 | 8 | `TBL(0x0c)` | start values: d, e, f |
| 4 | 2 | 0x80 | 7 | `TBL(0x0c)` | best values found: a, b |
| 5, 6, 7 | 1 | 0x83, 0x84, 0x85 | 7 | `TBL(0x0c)` | best values: d, e, f |
| 8 | 2 | 0x60 | 4 | `TBL(0x0c)` | values in use for OFDM: a, b |
| 9 | 1 | 0x62 | 4 | `TBL(0x0c)` | in use for OFDM: d |
| 10 | 2 | 0x70 | 4 | `TBL(0x0c)` | in use for 11b: a, b |
| 11 | 1 | 0x72 | 4 | `TBL(0x0c)` | in use for 11b: d |
| 12 | 2 | 0 | 5 | calibration state | intermediate results: a, b |
| 13, 14, 15 | 1 | 2, 3, 4 | 5 | calibration state | intermediate: d, e, f |
| 16 | 2 | 0 | 5 | calibration state | final results: a, b |
| 17, 18, 19 | 1 | 2, 3, 4 | 5 | calibration state | final: d, e, f |

Steps:

1. s = read `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)` (also when only the
   calibration state is accessed).
2. k <= 11: one transfer of n entries of 16 bit at `TBL(0x0c)[o + c * d]`:
   a read into `data` if mode = 0, else a write from `data`.
3. 12 <= k <= 15, with cal = the pointer `pi+0xf58`, for i = 0 .. n-1:
   mode 0: `data[i]` = u16 at cal + 0x2c + 2 * (o + c * d + i); mode 2: u16 at
   cal + 0x54 + 2 * (c * n + i) = `data[i]`; any other mode: u16 at cal + 0x2c
   + 2 * (o + c * d + i) = `data[i]`.
4. k >= 16: mode 0: `data[i]` = u16 at cal + 0x04 + 2 * (o + c * d + i); else
   that field = `data[i]`.
5. `mod(PHY(0x19e), 0x0002, s & 0x0002)`.

Callers: `sub_09c161`, the transmit calibration (`sub_0abc76`, `sub_0addfa`)
and `wlc_phy_scanroam_cache_cal_acphy`; the use in the calibration belongs to
`acphy-cal-tx`.

### 22. sub_08f9b4 (.text+0x08f9b4, name assigned by `acphy-init`: wlc_phy_tssi_phy_setup_acphy)

Inputs: `pi`, `mode` (8 bit; 0 from the band change function `sub_09e378` and
from `sub_0affa9`, 1 from `sub_0b0455`). For each core c, o = c * 0x200:
`mod(PHY(0x727 + o), 0x0004, 0x0004)`; `mod(PHY(0x73c + o), 0x0010, mode << 4)`.
(Interpretation: bit 2 of `PHY(0x727 + o)` is the override enable for bit 4 of
`PHY(0x73c + o)`, which selects the input of the TSSI measurement: 0 = the
external detector of the front end, 1 = the detector inside the radio. The
radio counterpart is `sub_0940f8`, `acphy-radio.md` section 16, with the same
`mode`.)

### 23. sub_0affa9 (.text+0x0affa9, name assigned by `acphy-init`: wlc_phy_txpwrctrl_idle_tssi_meas_acphy)

The flow is specified in `acphy-init.md` section 6 (confirmed). In short: not
while a hold flag of the mask 0x021e is set; carrier search on; `sub_08f9b4(pi,
0)`; `sub_0940f8(pi, sh+0xa5, 0)`; `PHY(0x401)` saved, its bits 0..2 and 12..14
set to the core mask; per core c of the mask: `sub_0af7f4(pi, buf, 1, 0, 0,
1, c)` (section 24), `pi_ac+0x44e + 2c` = `buf[c]`, `mod(PHY(0x645 + c * 0x200),
0x03ff, buf[c])`; `PHY(0x401)` restored; carrier search off.

`buf[c]` is a signed 16 bit number in the range of a 10 bit two's complement
value (-512..511); bits 0..9 of it go to the register. In the model it is 0.

### 24. sub_0af7f4 (.text+0x0af7f4, name assigned: wlc_phy_poll_samps_WAR_acphy)

Purpose: measure the reading of the power detector of one core while the
transmitter sends a test tone with a given gain (or "nothing": the idle
TSSI). Inputs: `pi`; `out` (array of s16, one per core); `idle` (bool: 1 = idle
measurement, the gain record is not used); `gain` (pointer to a transmit
gain record of 10 bytes, see "Data"); `fixed8` (bool); `adc` (bool: 1 = the
function prepares and restores the read out of the measurement itself);
`core` (16 bit, seventh argument, on the stack). Result in `out[core]`.
Callers: `sub_0affa9` with (1, 0, 0, 1, c); `sub_0b0455` (other PHY revisions).

"Core in the mask" below: c < N and bit c of `sh+0xa5` set. o = c * 0x200, b =
c << 9. The gain and the overrides are applied to **all** cores of the mask,
the measurement is made for `core` only.

1. If `adc`: `sub_09bbe4(pi, state)` (section 26); it also returns the bit 1
   of `PHY(0x19e)` in `stall`. If not `adc`: `stall` = 0.
2. `mod(PHY(0x19e), 0x0002, 0x0002)`.
3. Save, for each core c in the mask: bbmult with `sub_098751(pi, &m[c], c)`
   (section 3); then read `PHY(0x747 + o)`, `PHY(0x732 + o)`, `PHY(0x733 + o)`,
   `PHY(0x734 + o)`, `PHY(0x722 + o)` (in this order).
4. Values to apply. `idle`: all 0. Otherwise from the record: g1 = (u16 at
   +2) << 8 | (u16 at +0) >> 8 (16 bit); g2 = (u16 at +2) >> 8 | (u16 at +4) << 8
   (16 bit); d = (u16 at +0) & 0x000f; l = ((u16 at +0) & 0x00f0) >> 4; mult =
   u16 at +8. In terms of the 48 bit gain table entry: d = bits 8..11, l =
   bits 12..15, g1 = bits 16..31, g2 = bits 32..47, mult = bits 0..7.
5. Apply, for each core c in the mask:
   1. `PHY(0x732 + o)` = g1; `PHY(0x733 + o)` = g2; `PHY(0x747 + o)` = d (plain
      writes).
   2. `mod(PHY(0x734 + o), 0x0038, l << 3)` (the lowest three bits of l).
   3. `mod(PHY(0x722 + o), 0x0001, 0x0001)`; `mod(PHY(0x722 + o), 0x0008, 0x0008)`
      (interpretation: override enables for the transmit gain registers and
      for the filter gain).
   4. `sub_09c4e4(pi, &mult, c)` (section 2).
   5. r1[c] = read `RADIO(0x04e | b)`; r2[c] = read `RADIO(0x166 | b)`.
      (Other chips than 0x4352, 0x4360, 43460, 43526: `RADIO(0x055 | b)` and
      `RADIO(0x179 | b)`; radio major revision other than 0: `RADIO(0x17a | b)`
      in place of the second.)
   6. v = read one 16 bit entry `TBL(0x07)[0x17e + 0x10 * c]`; v = v & 7.
   7. `mod(RADIO(0x04e | b), 0x0e00, v << 9)`; `mod(RADIO(0x166 | b), 0x0002,
      0x0002)` (purpose unknown).
6. `mod(PHY(0x19e), 0x0002, stall << 1)`.
7. `wlc_btcx_override_enable(pi)` (section 28; no access unless the MAC has
   the Bluetooth coexistence capability and the band is 2.4 GHz).
8. Delay 100.
9. `wlc_phy_tx_tone_acphy(pi, 2000, a, 0, 0, 0)` with a = 0 if `idle`, else
   181 (0xb5) (specification `acphy-cal-tx`: tone of 2000 kHz, amplitude a;
   amplitude 0 = transmitter on without signal).
10. Delay 100.
11. n = 3 if `fixed8`; else 8 if the bandwidth (`pi+0x17e` & 0x3800) is 80
    MHz (0x2000), else 0. `sub_093f74(pi, out, 1, n, adc, core)` (section 25):
    average of 2^n readings (8, 256 or 1).
12. `wlc_phy_stopplayback_acphy(pi)` (`acphy-cal-tx`).
13. `wlc_phy_btcx_override_disable(pi)` (section 28).
14. `mod(PHY(0x19e), 0x0002, 0x0002)`.
15. Restore, for each core c in the mask: plain writes `PHY(0x732 + o)`,
    `PHY(0x733 + o)`, `PHY(0x747 + o)`, `PHY(0x722 + o)`, `PHY(0x734 + o)` = the
    saved values (in this order); `sub_09c4e4(pi, &m[c], c)`; `RADIO(0x04e | b)`
    = r1[c]; `RADIO(0x166 | b)` = r2[c].
16. `mod(PHY(0x19e), 0x0002, stall << 1)`.
17. If `adc`: `sub_09be13(pi, state)` (section 26).

### 25. sub_093f74 (.text+0x093f74, name assigned: wlc_phy_poll_samps_acphy)

Purpose: read the measurement converter through the debug outputs of the
PHY and average. Inputs: `pi`, `out` (array of s16 per core), `tssi` (bool),
n (8 bit: 2^n readings), `adc` (bool), `core` (16 bit). Only caller:
`sub_0af7f4` (with `tssi` = 1).

1. `sub_092ffa(pi)` (section 28: reset pulse of the converters).
2. Delay 100.
3. sum = 0. Repeat 2^n times:
   1. If `adc`: `sub_093ebe(pi, 16 + core, 1)` (section 27).
   2. i = read `PHY(0x013)`; q = read `PHY(0x012)` (in this order).
   3. x = q if `tssi`, else i. v = x >> 2 (x taken as unsigned 16 bit); if v >=
      0x200: v = v - 0x400 (10 bit two's complement).
   4. sum = sum + v.
4. `out[core]` = sum >> n (arithmetic shift, 32 bit; stored as 16 bit).

No delay between the readings.

### 26. sub_09bbe4 and sub_09be13 (.text+0x09bbe4, .text+0x09be13; names assigned: wlc_phy_init_adc_read, wlc_phy_restore_after_adc_read)

Purpose: prepare the PHY for reading the converter through the debug
outputs, and undo it. Both take `pi` and eight pointers to the places of the
saved state (the same eight in both calls): `s1` (u16), `s2` (u16), `s3`
(u32), `t1`, `t2`, `t3`, `t4` (u16), `stall` (u8). Callers: `sub_0af7f4`,
`sub_0b0455`, `wlc_phy_tempsense_acphy`.

`sub_09bbe4`:

1. v = read `PHY(0x19e)`; `stall` = bit 1 of v.
2. v = read `PHY(0x40f)`; `s1` = bit 9 of v; `mod(PHY(0x40f), 0x0200, 0)`.
3. `s2` = read `PHY(0x394)`.
4. Only if `pi_ac+0x8e1` != 0 (see below):
   1. `mod(PHY(0x19e), 0x0002, 0x0002)`.
   2. `mod(PHY(0x93e), 0x0010, 0)`; `mod(PHY(0x93e), 0x0020, 0)`;
      `mod(PHY(0x93e), 0x1000, 0x1000)`.
   3. `s3` = read `CC(0x28)`; then `CC(0x28)` = 0x00000100 (through
      `si_corereg(sih, 0, 0x28, mask, value)`: the first call with mask 0
      reads, the second with mask 0xffffffff reads and writes the value).
   4. `t1` = read `TBL(0x0a)[0x29]`, `t2` = read `TBL(0x0a)[0x39]` (16 bit).
   5. `t3` = (`t1` & 0x00f0) << 1; `t4` = `t2` & 0x000f; `TBL(0x0a)[0x29]` = `t3`;
      `TBL(0x0a)[0x39]` = `t4`.
   6. `mod(PHY(0x93e), 0x0010, 0)`; `mod(PHY(0x93e), 0x0020, 0)`;
      `mod(PHY(0x93e), 0x1000, 0)`.
   7. `mod(PHY(0x19e), 0x0002, stall << 1)`.

`sub_09be13`:

1. `mod(PHY(0x19e), 0x0002, 0x0002)`.
2. `PHY(0x394)` = `s2`.
3. Only if `pi_ac+0x8e1` != 0: the three modifications of `PHY(0x93e)` of step
   4.2 above; `TBL(0x0a)[0x29]` = `t1`; `TBL(0x0a)[0x39]` = `t2`; `CC(0x28)` =
   `s3` (`si_corereg` with mask 0xffffffff: read, write); the three
   modifications of step 4.6.
4. `mod(PHY(0x40f), 0x0200, s1 << 9)`.
5. `mod(PHY(0x19e), 0x0002, stall << 1)`.

`pi_ac+0x8e1` is 0 after attach and is set to 1 only by `sub_0a14b6`, which
the front end set-up `sub_0a602f` calls for `femctrl` 2 and 5 and which sets
it only for PHY revision 0 (boards that use the GPIO pins for the front end
control; owner `acphy-rxgain`). With `femctrl` = 3 of the emulated card the
field stays 0 and step 4 is never executed (unverified for the real card,
whose `femctrl` is not known).

### 27. sub_093ebe (.text+0x093ebe, name assigned: wlc_phy_gpiosel_acphy)

Purpose: select what the PHY shows on its debug outputs, which the driver
reads back in `PHY(0x012)`/`PHY(0x013)`. Inputs: `pi`, selection (16 bit),
flag (8 bit). Callers: `sub_093f74`, `sub_0b0455`, `sub_0a14b6`,
`wlc_phy_tempsense_acphy`.

1. `PHY(0x392)` = 0; `PHY(0x393)` = 0.
2. v = read `D11(0x120)` (32 bit); write `D11(0x120)` = v & 0xffff3fff.
3. Write `D11(0x49e)` = 0 (16 bit).
4. `PHY(0x394)` = (flag << 8) | selection.
5. `PHY(0x392)` = 0xffff; `PHY(0x393)` = 0xffff.

The measurement uses selection 16 + core with flag 1: `PHY(0x394)` = 0x0110
for core 0, 0x0111 for core 1.

### 28. Functions of other specifications as the measurement calls them

`sub_092ffa(pi)` (.text+0x092ffa, owner `acphy-cal-rx`): for each core c (o =
c * 0x200): v = read `PHY(0x739 + o)`, write v | 0x0080; v = read
`PHY(0x73a + o)`, write v | 0x0080; v = read `PHY(0x725 + o)`, write v |
0x0204. Delay 1. Then the saved values are written back in the reverse
order (last core first; per core `PHY(0x725 + o)`, `PHY(0x73a + o)`,
`PHY(0x739 + o)`). Delay 1.

`wlc_btcx_override_enable(pi)` (.text+0x0b31df, wlc_phy_cmn.c): only if bit 29
of the MAC capabilities `sh+0x2c` is set and the band is 2.4 GHz (16 bit
accesses): `D11(0x6b4)`: read, write value | 3; `D11(0x6b8)`: read, write
value & 0xff3f; `D11(0x6f0)` = 1; read `D11(0x6f2)`; if
`wlapi_is_eci_coex_enabled(shim)`: `D11(0x6f0)` = 1 and up to 5000 times:
read `D11(0x6f2)`, stop if (value & 0x00f0) = 0, else delay 100;
`D11(0x6b8)`: read, write value | 0x0080; up to 502 times: read `D11(0x6b6)`,
stop if bit 0 is 0 (or after the 502nd read), else delay 100; `D11(0x6b8)`:
read, write value | 0x00c0.

`wlc_phy_btcx_override_disable(pi)` (.text+0x0b2cf2, wlc_phy_cmn.c): only if
bit 29 of `sh+0x2c` is set (any band): `D11(0x6b4)`: read, write value | 3;
`D11(0x6b8)`: read, write value & 0xff3f.

In the traces `sh+0x2c` is 0 (the model's MAC capabilities) and both cause
no access.

### 29. sub_0b1227 (.text+0x0b1227, name assigned by `acphy-init`: wlc_phy_precal_txgain_acphy), sub_0b0455 (.text+0x0b0455, name assigned: wlc_phy_precal_target_tssi_search_acphy)

`sub_0b1227` is specified in `acphy-init.md` section 7 (confirmed): for PHY
revisions 0 and 1 it only reads the gain table entry of a fixed index per
core with `sub_09868f` into the calibration state.

`sub_0b0455(pi, records)` is called by `sub_0b1227` for PHY revisions 2, 3, 5
and 6 only: **not reached on the 4360**. Summary (not specified in detail):
carrier search on, many radio and PHY registers saved, `sub_08f9b4(pi, 1)`,
`sub_0940f8(pi, sh+0xa5, 1)` (detector inside the radio), idle measurement
with `sub_0af7f4(.., 1, 0, 1, 0, core)`, then a search over the gain table
index (entries read with `sub_09868f`, measured with `sub_0af7f4(.., 0, gain,
1, 0, core)`); everything restored at the end. What the search aims at
(it uses the constant tables at `.rodata+0x2cc8a0`, `+0x2cc8c0`, `+0x2cc8e0`)
was not analysed (unverified: a target detector reading per band).

<!-- more procedures are added here -->

## Verification

| Item | Checked how | Result |
|---|---|---|
| `wlc_phy_txpwr_by_index_acphy`, `sub_09868f`, `sub_09c4e4` | up-trace seq 48935..49106 (channel function at init, index 64, table `acphy_txgain_epa_2g_2069rev4`): `TBL(0x20)[0x40]` read as 0x003f, 0xcfff, 0xa707; `TBL(0x07)[0x100]` = 0xff00, `[0x103]` = 0x07cf, `[0x106]` = 0x00a7; `TBL(0x0c)[0x63]` = `[0x73]` = 0x003f | as specified |
| `wlc_phy_txpwrctrl_enable_acphy` | up-trace seq 49107 (off: read of `PHY(0x70)` = 0, no index saved), 49117 (on: 0xe000, no start index written because the saved index is 0x80), 55309 (after the set-up: 0x6500 -> 0xe500) | as specified |
| `sub_098949`, order and values of steps 1..14 | up-trace seq 54309..54390 (2.4 GHz channel 1, radio revision 4, PHY revision 1, `pdgain2g` = 4): `PHY(0x72)` 0x0004 -> 0x0005; `PHY(0x70)` 0xe000 -> 0x6000 -> 0x6100; `PHY(0x72)` -> 0x0005 -> 0x4005; `PHY(0x644)`, `PHY(0x844)` = 0x14; `PHY(0x71)` = 0x00dc -> 0x04dc; `PHY(0x70)` -> 0x6500; `PHY(0x846)` = 0x46, then `PHY(0x646)` = 0x46 | as specified |
| Estimated power tables (step 15) | `estpwr.py` (scratch directory): the formula evaluated in Python for the PA parameters of the synthetic SROM against the 128 entries written to `TBL(0x40)` (seq 54391) and `TBL(0x60)` (seq 54784) | no difference |
| Detector offset table (step 16) | up-trace seq 55177 (2.4 GHz, `pdoffset2g40mvalid` = 1): 24 zeros; chan-36 seq 65523..65546 (5 GHz sub-band 1, `pdoffset40ma0` = `..ma1` = 0x1111): entries 1, 5, 6 = 0x00000101, all others 0 | as specified |
| `sub_09949f`, order of its calls | up-trace seq 54143..55314: `wlapi_high_update_txppr_offset` (shared memory only), `sub_098949`, `wlc_phy_txpwrctrl_enable_acphy` | as specified |
| `sub_08f9b4` | up-trace seq 41310 (band change part of the channel function) and 49672 (idle TSSI): bit 2 of `PHY(0x727)`, `PHY(0x927)` set, bit 4 of `PHY(0x73c)`, `PHY(0x93c)` cleared | as specified |
| `sub_0af7f4`, `sub_093f74`, `sub_09bbe4`, `sub_09be13`, `sub_093ebe`, `sub_092ffa` | up-trace seq 49813..50463 (core 0) and 50464..51114 (core 1), 20 MHz, idle: order of all accesses of sections 24 to 28; one reading (n = 0); `PHY(0x394)` = 0x0110 (core 0), 0x0111 (core 1); delays of 100 us before and after the tone and after the reset pulse | as specified (the readings are 0 in the model) |
| `sub_09c161`, `sub_09bf99` | up-trace seq 41478..41595 (band change part of the channel function at init, all values 0): per core `TBL(0x0c)[0x60 + 4c]`, `[0x61 + 4c]`, then `[0x62 + 4c]`, `RADIO(0x002..0x005 \| c << 9)`, `PHY(0x6a0 + c * 0x200)`, `PHY(0x6a1 + c * 0x200)` | as specified |

## Open questions

* Which bits of the 40 bit gain code control which gain stage of the radio.
