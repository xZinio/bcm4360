# Desense, carrier sense thresholds and interference mitigation of the AC-PHY (BCM4360)

Status: work in progress, written function by function. Every procedure below
was read in the decompiler output and checked in the disassembly; what was
also compared with emulator traces and experiments is listed in
"Verification". Register *names* are not known; aliases in parentheses are mine.

Register access notation: `access.md`. The channel function that calls most
of the functions of this area: `acphy-chanspec.md`, section 5 (steps 20 to
24).

**Division of work with `acphy-chanspec.md`.** The annexes A4 to A7 of that
specification describe the receive gain control and the application of the
desense as the channel function runs them: A4 `sub_092efb`, `sub_0909fd`; A5
`sub_09af05`, `sub_099658`, `sub_0998cc`; A6 `sub_09a121`, `sub_099f29`,
`sub_090493`, `sub_09175a`, `sub_091b6e`; A7 `sub_09a539`, `sub_08f84d`,
`sub_090b77`. These descriptions are not repeated here. Section R of
"Procedures" says what of them was checked again for this specification,
and corrects and completes them; where section R and the annex differ,
section R is right. Likewise `acphy-init.md` (section 3a, H11, appendix A) and
`acphy-attach.md` (procedures 9 to 11) describe the set-up functions of this
area; section R completes them.

**What this specification adds** to the annexes: the *sources* and *consumers*
of the desense state that the annexes only apply. Three machines write the
desense values and the carrier-sense thresholds:

* the **carrier-sense minimum-power calibration** (section C), run from the
  channel change (`sub_0a4adc`) and after each noise measurement, which sets
  the carrier-sense / energy-detect power thresholds;
* the two **periodic interference engines** (section E), run from the watchdog:
  the *hwaci* engine (adjacent-channel interference by hardware measurement,
  writes the gain-limit desense) and the *desense-aci* engine (interference by
  RSSI/energy statistics, writes the OFDM and b-PHY desense);
* the **high-RSSI external-LNA-bypass engine** (section H), run from the
  watchdog, which bypasses the external LNA for a time when the microcode
  signals a strong received signal.

The energy-detect threshold iovar (`wlc_phy_ed_thres_acphy`) and the
Bluetooth-coexistence desense iovar (`wlc_phy_desense_btcoex_acphy`) are
described where they belong (C5, E5).

## Scope

Chip 0x4360 (0x4352, 43460 = 0xa9c4 and 43526 = 0xaa06 take the same
branches unless said otherwise), MAC core revision 42, AC-PHY revision 0 or 1,
radio 2069 revision 3 or 4 (major revision 0), two chains.

Functions I only refer to (they are described in the annexes A4 to A7 of
`acphy-chanspec.md` or in `acphy-init.md`/`acphy-attach.md`) are **not** in this
table: `sub_09af05`, `sub_099658`, `sub_0998cc`, `sub_09a121`, `sub_099f29`,
`sub_090493`, `sub_09175a`, `sub_091b6e`, `sub_09a539`, `sub_08f84d`,
`sub_090b77`, `sub_092efb`, `sub_0909fd`, `sub_092500`,
`wlc_phy_hirssi_elnabypass_set_ucode_params_acphy`,
`wlc_phy_desense_aci_reset_params_acphy`, `wlc_phy_hwaci_setup_acphy`,
`wlc_phy_hwaci_init_acphy`, `wlc_phy_force_rfseq_acphy`,
`wlc_phy_resetcca_acphy`.

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_phy_crs_min_pwr_cal_acphy` | 0x0929c5 | 1186 | original |
| `sub_08f41b` | 0x08f41b | 1074 | assigned: `wlc_phy_set_crs_thresh_acphy` |
| `wlc_phy_noise_sample_acphy` | 0x092610 | 94 | original |
| `wlc_phy_noise_read_shmem` | 0x0b7ecc | 400 | original (wlc_phy_cmn.c); only the CRS-cal trigger |
| `wlc_phy_noise_sample_request_crsmincal` | 0x0bb8d1 | 23 | original (wlc_phy_cmn.c) |
| `wlc_phy_ed_thres_acphy` | 0x0912a6 | 368 | original |
| `wlc_phy_hwaci_engine_acphy` | 0x0999ad | 1404 | original |
| `wlc_phy_aci_updsts_acphy` | 0x092ec0 | 59 | original |
| `wlc_phy_desense_aci_engine_acphy` | 0x09a88b | 1044 | original |
| `wlc_phy_desense_aci_upd_chan_stats_acphy` | 0x092fd8 | 34 | original |
| `wlc_phy_desense_btcoex_acphy` | 0x09ad8c | 377 | original |
| `wlc_phy_hirssi_elnabypass_engine` | 0x09b7f3 | 238 | original |
| `wlc_phy_hirssi_elnabypass_apply_acphy` | 0x09b752 | 161 | original |
| `wlc_phy_hirssi_elnabypass_status_acphy` | 0x0909a4 | 89 | original |

## Overview

The desense state is a set of 9 bytes (layout in "Data"). Three copies live in
`pi_ac`: the values wanted ("total", `+0x668`), the values applied last
(`+0x656`), and the values used when the channel has no interference record
(`+0x65f`, all 0 after attach). Per channel there is also an *interference
record* of 0x50 bytes (`sub_092efb`, A4) that holds its own 9-byte desense set
(`record+0x10`), the running interference statistics and the hwaci level.

The flow of a change is always the same:

1. A source writes the wanted desense somewhere: the hwaci engine into
   `record+0x14/+0x15` (gain-limit desense), the desense-aci engine into
   `record+0x10/+0x11` (OFDM/b-PHY desense), the Bluetooth iovar into
   `pi_ac+0x8b4..` (the BT desense set).
2. `sub_0909fd` (A4) folds the record's set (or the `+0x65f` default) and the BT
   set into `pi_ac+0x668` (total).
3. `sub_09a539(pi, on)` (A7) applies the total: the OFDM/b-PHY registers, and,
   where a byte changed since last time, it re-runs the gain-table builders
   `sub_099658`/`sub_0998cc` (A5) and the init-clip-gain setter `sub_09a121`
   (A6), and it programs the carrier-sense threshold via `sub_08f41b` (C2).

The carrier-sense minimum-power calibration (section C) is a separate path: it
does not touch the desense bytes but writes the carrier-sense / energy-detect
power-threshold registers directly (through `sub_08f41b`), unless desense is in
force, in which case it defers to `sub_09a539`.

The two interference engines and the high-RSSI engine run only from the PHY
watchdog (`wlc_phy_watchdog`, `.text+0x0bb8ff`, once per second). In the
model's synthetic card every measured quantity (received power, RSSI, energy,
noise) reads 0, so the engines run but their decisions are degenerate: the
desense-aci engine returns below its thresholds without changing anything, the
hwaci engine raises its level from 0 to 1 on the first tick (an unconditional
step, not a measurement result), and the high-RSSI engine returns at once
because the feature is disabled per band in the synthetic SROM. The register
addresses, the order and the table values are nonetheless exact; the decisions
that depend on a measured value are marked "(code reading)".

## Data

### Structure fields

The complete list is in `re-out\analysis\fields\acphy-desense.tsv`.

#### The 9-byte desense set

| Byte | Name | Meaning |
|---|---|---|
| 0 | `ofdm_desense` | reduction of the OFDM sensitivity in dB |
| 1 | `bphy_desense` | reduction of the 802.11b sensitivity in dB |
| 2 | `lna1_tbl_desense` | entries taken off the top of the stage-1 gain table |
| 3 | `lna2_tbl_desense` | the same for stage 2 |
| 4 | `lna1_gainlmt_desense` | entries of the stage-1 gain-limit table blocked |
| 5 | `lna2_gainlmt_desense` | the same for stage 2 |
| 6 | `elna_bypass` | bypass the external LNA |
| 7 | `nf_hit_lna12` | dB added to the clip thresholds (`sub_090493`) |
| 8 | `on` | desense is in force |

Copies of the set:

| Field | Meaning |
|---|---|
| `pi_ac+0x656 .. 0x65e` | desense values applied last (what the hardware is set to) |
| `pi_ac+0x65f .. 0x667` | values used when the channel has no interference record (all 0 after attach; the "no interference" baseline) |
| `pi_ac+0x668 .. 0x670` | values wanted ("total"), built by `sub_0909fd`; `+0x670` = byte 8 is the argument of `sub_09a539` |
| `pi_ac+0x671` (u8) | RSSI-based desense cap enabled (observed = 1 after up); consumed by the desense-aci engine |
| `pi_ac+0x8b0` (u32) | Bluetooth-coexistence desense profile id (0 = off); the switch tested by `sub_0909fd` |
| `pi_ac+0x8b4 .. 0x8bc` | the Bluetooth-coexistence desense set (9 bytes) |

#### Carrier-sense minimum-power calibration (`pi_ac`)

| Field | Meaning |
|---|---|
| `pi_ac+0x14 + c` (s8) | per-core measured input of the calibration (interpretation: a recent per-core received/noise level; reads 0 in the model) |
| `pi_ac+0x18 + 4*sb + c` (s8) | per-sub-band, per-core reference level; `sb` = `wlc_phy_get_chan_freq_range_acphy` (0..4) |
| `pi_ac+0x2c .. 0x3b` | 4-deep ring buffer (4 slots of 4 bytes), each slot per-core (`pi_ac+0x14+c` + 1) |
| `pi_ac+0x3c` (u8) | ring write index (modulo 4) |
| `pi_ac+0x3d + c` (s8) | per-core averaged value of the last calibration |
| `pi_ac+0x41` (u8) | run the calibration on every noise sample (a "keep calibrating" enable) |
| `pi_ac+0x42` (u8) | applied minimum power (max over cores); 0x36 after attach; the floor of `v` in A7 step 3.6 |
| `pi_ac+0x43` (u8) | count of calibrations done |
| `pi_ac+0x44` (u8) | channel number of the last calibration |
| `pi_ac+0x33d` (u8) | "force reference update" flag; cleared by a calibration |
| `pi_ac+0x33e` (u8) | state: 1 = calibrated and applied, 2 = deferred because desense is on |

#### hwaci engine (`pi_ac` and the per-level tables)

| Field | Meaning |
|---|---|
| `pi_ac+0x682 + 8*k` | 2.4 GHz hwaci level table: 8 bytes per level `k` (below); set by `wlc_phy_hwaci_setup_acphy` (`acphy-init` appendix A) |
| `pi_ac+0x6a2 + 8*k` | the same for 5 GHz |
| `pi_ac+0x6c2` (u8) | number of hwaci levels for 2.4 GHz |
| `pi_ac+0x6c3` (u8) | number of hwaci levels for 5 GHz |

Layout of one 8-byte hwaci level entry (names mine, from the way the engine
reads them):

| Byte | Use in the engine |
|---|---|
| 0..1 | u16 written to `PHY(0x554)`/`PHY(0x555)` (rev>=2: `PHY(0x5a4)`/`PHY(0x5a5)`) when the level is entered |
| 2 | `lna1_gainlmt_desense` produced as `max(0, 5 - byte2)` |
| 3 | `lna2_gainlmt_desense` produced as `max(0, 6 - byte3)` |
| 4 | comparison mode of the hwaci detector (0, 1 or 2) |
| 5, 6 | thresholds of the hwaci detector |

#### The interference record (0x50 bytes; base `pi_ac+0x8a8`)

Three per band: 2.4 GHz at `pi_ac+0x6c8`, 5 GHz at `pi_ac+0x7b8` (A4). Fields
beyond those A4 lists:

| Field | Meaning |
|---|---|
| `record+0x10 .. 0x18` | this channel's 9-byte desense set (the "src" of `sub_0909fd`); `+0x10` = OFDM, `+0x11` = b-PHY, `+0x14` = lna1 gain-limit, `+0x15` = lna2 gain-limit, `+0x18` = byte 8 "on"/"differs from default" |
| `record+0x19` (s8) | RSSI-based desense input (set by `wlc_phy_desense_aci_upd_chan_stats_acphy`) |
| `record+0x1c .. 0x2b` | 4-deep u32 ring of the "narrow-band" interference metric |
| `record+0x2c, +0x2d, +0x2e` (u8) | narrow-band hysteresis: lower bound, upper bound, dwell counter |
| `record+0x30 .. 0x3f` | 4-deep u32 ring of the "wide-band" interference metric |
| `record+0x40, +0x41, +0x42` (u8) | wide-band hysteresis: lower bound, upper bound, dwell counter |
| `record+0x44` (u8) | ring write index of the two metrics (modulo 4) |
| `record+0x45` (u8) | settle countdown; 2 when the record is created, 1 after the desense-aci engine applies; decremented each tick |
| `record+0x46` (u8) | hwaci current level |
| `record+0x47` (u8) | hwaci minimum level seen |
| `record+0x48` (u8) | hwaci hold-down timer (reset to 8 on a level change) |
| `record+0x49` (u8) | hwaci settle countdown (2 on a level change); decremented each tick |

#### High-RSSI external-LNA bypass (`pi_ac`)

The chanspec spec (its Data table) already lists `pi_ac+0x908`, `+0x910`,
`+0x911`, `+0x912`, `+0x914`, `+0x916`. Used here as: `+0x908` low 16 bits =
bypass duration in watchdog ticks; `+0x910`/`+0x911` = feature enabled for
2.4 GHz / 5 GHz; `+0x912` = feature present; `+0x914`/`+0x916` (s16) = remaining
ticks for 2.4 GHz / 5 GHz (negative = bypass not active).

#### Interference statistics read by the desense-aci engine (`pi`)

All are measured, and read 0 in the model.

| Field | Meaning (interpretation) |
|---|---|
| `pi+0x3f6 + 2*i`, `pi+0x3fa + 2*i` (u16) | wide-band and narrow-band interference histograms |
| `pi+0x414 + 2*i`, `pi+0x41c + 2*i` (u16) | two more histograms added in at weight 2 |
| `pi+0x400`, `pi+0x404`, `pi+0x418`, `pi+0x420` (u32) | index selectors for the four histograms above (`sel = value ? value-1 : 1`) |
| `pi+0x18f` (byte, = 399 decimal) | reason of the pending noise sample; 4 = "for the CRS min-pwr cal" |

### Constants

| Constant | Value |
|---|---|
| Default carrier-sense minimum power (`pi_ac+0x42`, and `local_58[0]`) | 0x36 = 54 |
| CRS min-power reference change threshold `cVar16` | 3 (PHY rev 0/1), 2 (rev >= 2) |
| CRS 20/40/80 MHz index bias added to the level | +0x22, +0x21, +0x1e |
| CRS lookup table index range | clamped to 0 .. 14 |
| desense-aci narrow / wide trigger | metric average >= 301 / >= 601 |
| desense-aci level clamp | b-PHY (narrow) <= 24, OFDM (wide) <= 48 |
| desense-aci ring reset on change | 100 (narrow), 300 (wide) |
| ED-threshold register scale | `reg = (dBm*640000 + 73045696) / 30103` (first eight registers); `+69205696` instead (second eight, 6.0 dB lower) |
| CRS reference default (`pi_ac+0x18`) | 0xe2 = -30 (set at attach), which maps back to the 0x36 minimum power |

CRS minimum-power lookup tables, 15 bytes, index 0..14 (`.rodata`):

| Bandwidth | Address | Bytes (decimal) |
|---|---|---|
| 20 MHz | `.rodata+0x2ccb18` | 45 48 51 53 54 57 60 63 66 68 70 72 75 78 80 |
| 40 MHz | `.rodata+0x2ccb08` | 44 46 48 50 52 54 56 58 60 63 66 69 71 74 76 |
| 80 MHz | `.rodata+0x2ccaf8` | 45 47 49 51 53 54 55 57 58 59 60 61 63 65 67 |

## Procedures

`mod(reg, mask, value)` is read-modify-write and always writes. "For each core
c" means c = 0 .. N-1 in rising order (N = `pi+0x168` = 2), o = 0x200 * c.
2.4 GHz means (`pi+0x17e` & 0xc000) = 0, everything else is 5 GHz; bw =
`pi+0x17e` & 0x3800 (0x1000 = 20, 0x1800 = 40, 0x2000 = 80 MHz). "Present cores"
are those whose bit is set in `sh+0xa7` (`phyrxchain`).

### R. Review of the annexes A4 to A7 of `acphy-chanspec.md`

#### R1. A5: sub_09af05, sub_099658, sub_0998cc

Read again in the disassembly and run in the emulator with other inputs (see
"Verification"): **A5 is right as it is written.** Additions:

* `sub_0998cc(pi, stage)` treats every `stage` other than 1 as 2 (there is no
  test for 2). `sub_099658(pi, stage)` returns without doing anything for
  stages other than 1 and 2.
* In `sub_099658` entry 0 gets the gain T[1] and the code 1 also when m = 0;
  then all other entries have the gain T[0] and the code 0 (checked with
  desense values above h).
* "Channel number below 100" in step 3.1 means: low byte of `pi+0x17e` <= 99.
  The three values at `pi_ac+0x3f8 + 3*c` (SROM values for the middle of the
  5 GHz band) are not used by any function of the PHY.
* The values that the four fields "d is remembered in" hold are bytes 2 to 5
  of the set of applied desense values at `pi_ac+0x656` (see "Data").
* What step 3.5 reads at initialisation (the code of entry 0 of stage 0, the
  codes of stage 3, gains and codes of the stages 4 and 5:
  `TBL(0x44 + 0x20*c)[0x60..0x67]`, `[0x70..0x77]` and the same of
  `TBL(0x45 + 0x20*c)`) is never written by the driver before: it is what the
  PHY holds after reset. **These values are not in the object and not known**
  (the model of the card returns 0 for them). They are inputs of `sub_08f086`
  and of everything computed from its result (start gain, clip gains, the
  signal strength correction of the channel function).
* `code[0][1]` (second entry of the external LNA stage) is never written or
  read from the hardware; it stays 0.

#### R2. A4: sub_092efb, sub_0909fd - confirmations

The A4 "interpretation, unverified" note is now confirmed by the writers of
those fields (section E5, `wlc_phy_desense_btcoex_acphy`):

* `pi_ac+0x8b4` **is** the Bluetooth-coexistence desense set (9 bytes) and
  `pi_ac+0x8b0` **is** its switch (a profile id 1..6, 0 = off). `sub_0909fd`
  folds it in only on 2.4 GHz and only outside a scan, taking the larger
  (unsigned) of the BT byte and the source byte for bytes 0..7 and OR-ing byte
  8. `pi_ac+0x8bc` (byte 8, "on") is 1 whenever the profile id is > 0.
* The 9-byte "src" that `sub_0909fd` reads at `record+0x10` when
  `pi_ac+0x8a8 != 0` is exactly the record's own desense set that the two
  watchdog engines write (section E). When there is no record it reads the
  `pi_ac+0x65f` baseline (all 0), which is why on the model's synthetic card the
  wanted desense is always all-0.
* `pi_ac+0x66e` that step 3 may set to 1 is byte 6 (`elna_bypass`) of the total
  set (`pi_ac+0x668 + 6`); step 3 sets it when the high-RSSI bypass timer of the
  band is running.

#### R3. A6/A7: sub_09a539, sub_08f84d, sub_090b77 - the `on != 0` branch

A7 says the `on != 0` branch was "code reading only" there. It was exercised
here in the emulator by making a watchdog engine change the desense (section E
verification). The branch is as A7 describes it. One thing A7 leaves implicit:
its step 3.6 calls `sub_08f41b(pi, v, 0, 0)` with the third and fourth arguments
0, which is why on a desense change only core 0's carrier-sense low bytes move
and cores 1/2 are set to 0 offset (C2). The base `pi_ac+0x42` that step 3.6
floors `v` to is the value the carrier-sense calibration left (C1), 0x36 after
attach.

### C. Carrier-sense minimum-power calibration

#### C1. wlc_phy_crs_min_pwr_cal_acphy (.text+0x0929c5, name original)

Purpose: keep, per sub-band and per core, a reference of the received/noise
level, and from it set the carrier-sense minimum-power thresholds. Inputs:
`pi`, `restore` (byte). No result register used.

`restore = 0` is the calibrate path (from a fresh noise measurement, C3);
`restore != 0` is the re-apply path (from the channel change `sub_0a4adc`,
which passes 1: reprogram from the stored references without a new measurement).

Local tables: copy the three CRS lookup tables (20/40/80 MHz, 15 bytes each,
see "Data") to the stack. `local_48[c]` = per-core level, `local_58[c]` =
per-core minimum power; `local_58[0]` starts at 0x36, the rest at 0. `sb` =
`wlc_phy_get_chan_freq_range_acphy(pi, 0)` (the current channel's sub-band,
0..4); the per-sub-band reference row is `pi_ac+0x18 + 4*sb`.

Steps:

1. **Only if `restore = 0`**, push the newest measurement into the ring: for
   each present core c, `pi_ac[0x2c + c + 4*pi_ac[0x3c]] = pi_ac[0x14+c] + 1`;
   then advance the ring index `pi_ac[0x3c] = (pi_ac[0x3c] + 1) mod 4`.
2. For each present core c (in rising order, counting processed cores in a
   variable used as the "any core done" flag):
   1. **`restore = 0`:** `d = pi_ac[0x2c+c]` (newest ring slot). If `d = 0`
      the ring is empty for this core: **return** (nothing calibrated yet).
      Average up to the four ring slots `pi_ac[0x2c+c]`, `[0x30+c]`, `[0x34+c]`,
      `[0x38+c]` that are non-zero (stop at the first zero), integer division by
      the count. `local_48[c]` = this average.
      Compute the bandwidth-adjusted reference `r = pi_ac[0x18+4*sb+c]` for
      20 MHz, `+3` for 40 MHz, `+7` for 80 MHz. If `|r - average| >= cVar16`
      (3 for PHY rev 0/1, 2 for rev >= 2) **or** `pi_ac[0x33d] != 0`: mark a
      change, and store the new reference `pi_ac[0x18+4*sb+c]` = `average`
      (20 MHz), `average - 3` (40 MHz) or `average - 7` (80 MHz).
   2. **`restore != 0`:** `local_48[c]` = `pi_ac[0x18+4*sb+c]` adjusted for the
      bandwidth exactly as `r` above (read back, no measurement, no ring, no
      change mark).
   3. Common: `idx = local_48[c] + 0x22` (20 MHz), `+0x21` (40 MHz) or `+0x1e`
      (80 MHz); clamp `idx` to 0..14 (values above 14 become 14 if positive, 0
      if negative as a signed byte). `local_58[c]` = table[idx] from the 20/40/80
      MHz table. If c = 1: `off1 = local_58[1] - local_58[0]`; if c = 2:
      `off2 = local_58[2] - local_58[0]`.
3. Finalise:
   1. If no core was processed and `restore = 0`: **return**. If a change was
      marked and `restore = 0`: `pi_ac[0x33d] = 0` (clear the force flag).
   2. `pi_ac[0x33e] = 1`.
   3. `local_58[1] = max(local_58[1], local_58[2])`;
      `pi_ac[0x42] = max(local_58[0], local_58[1])` (the applied minimum power).
   4. If `pi_ac[0x670] = 0` (desense not in force): `pi_ac[0x43]++`;
      `pi_ac[0x44]` = channel number (low byte of `pi+0x17e`); for each present
      core c: `pi_ac[0x3d+c] = local_48[c]`; then suspend the MAC,
      `wlc_phyreg_enter(pi)`, `sub_08f41b(pi, local_58[0], off1, off2)` (C2),
      `wlc_phyreg_exit(pi)`, enable the MAC. Otherwise (desense in force):
      `pi_ac[0x33e] = 2` and do **not** program the thresholds - the desense
      apply (`sub_09a539`, A7) owns them then.

Notes: the reference table `pi_ac+0x18` overlaps nothing (5 sub-bands x up to 3
cores fit in `0x18..0x2b`, below the ring at `0x2c`). It is initialised to 0xe2
(-30) per entry at attach, which the tables map back to the 0x36 default minimum
power (index 4 of the 20 MHz table = 54). On the **restore** path
(`sub_0a4adc` on a channel not yet calibrated) that default is read back and
`sub_08f41b(pi, 0x36, 0, 0)` is applied (verified: registers stay 0x36).
On the **calibrate** path the model returns 0 for `pi_ac+0x14`, so every ring
slot is 1, the average is 1, the reference is pulled from -30 to 1, `idx` is
clamped to 14, and `local_58[c]` = the last table entry (80/76/67 for 20/40/80
MHz); the emulator confirms `pi_ac+0x42 = 0x50` and `sub_08f41b(pi, 0x50, 0, 0)`
after a calibrate on channel 1. The writer of `pi_ac+0x14` was not located; it
is the per-core input this calibration averages, and with a realistic value it
would land on a lower table index than the model's degenerate 0.

#### C2. sub_08f41b (.text+0x08f41b, name assigned: wlc_phy_set_crs_thresh_acphy)

Purpose: write the per-core carrier-sense / energy-detect minimum-power
threshold registers. Inputs: `pi`, `pwr` (byte), `off1` (byte), `off2` (byte).
Callers: `sub_09a539` (A7 step 3.6, as `sub_08f41b(pi, v, 0, 0)`) and
`wlc_phy_crs_min_pwr_cal_acphy` (C1, `sub_08f41b(pi, local_58[0], off1, off2)`).
No result.

1. Compute the two byte values that core 0 uses, `a` and `b`:
   * If `pwr = 0` (never happens from the two callers, which pass `pwr >= 0x36`):
     PHY rev 0/1 use `a = b = 0x36` and also write `pi_ac[0x42] = 0x36`; PHY
     revisions 2/3/5/6 pick band/bandwidth dependent defaults (0x3a/0x3c/0x43,
     not this card).
   * If `pwr != 0`: `a = pwr`. `b = pwr` for PHY rev 0/1. (PHY rev 2/3/5/6 scale
     `b` by bandwidth: `b = pwr*1.01+2`, `pwr*1.09-2` or `pwr*1.7-33` for
     20/40/80 MHz; not this card.) `pi_ac[0x42]` is **not** written.
2. `p3 = off1`. (PHY rev 3 only, and core 1 on 5 GHz, rescales `p3` by
   bandwidth; not this card, so `p3 = off1` throughout.)
3. For each present core c (0, then 1; core 2 not on this board):
   * **core 0** - low byte only (`mod(reg, 0x00ff, value)`), in this order:
     `PHY(0x324)=a`, `PHY(0x330)=b`, `PHY(0x321)=a`, `PHY(0x32d)=b`,
     `PHY(0x32a)=a`, `PHY(0x336)=b`, `PHY(0x327)=a`, `PHY(0x333)=b`. (These are
     the same eight registers whose *high* byte `sub_08f84d`, A7, writes.)
   * **core 1** - both bytes of four registers to the value `off1`:
     `mod(PHY(0x910),0xff00, off1<<8)`, `mod(PHY(0x910),0x00ff, off1)`,
     `mod(PHY(0x912),0xff00, off1<<8)`, `mod(PHY(0x912),0x00ff, off1)`,
     `mod(PHY(0x911),0x00ff, off1)`, `mod(PHY(0x911),0xff00, off1<<8)`,
     `mod(PHY(0x913),0x00ff, off1)`, `mod(PHY(0x913),0xff00, off1<<8)`.
   * **core 2** (not reached with N=2): the same shape on `PHY(0xb10..0xb13)`
     with the value `off2`.

So core 0 gets the absolute minimum power and the other cores get the offset of
their minimum power relative to core 0. When called from `sub_09a539` (desense)
the offsets are 0, so cores 1/2 registers are set to 0. Verified: on channel 1,
`sub_08f41b(pi, 0x36, 0, 0)` sets the low byte of `PHY(0x321..0x336)` to 0x36
and leaves the high byte (0x36 from `sub_08f84d`), and sets `PHY(0x910..0x913)`
to 0.

#### C3. wlc_phy_noise_sample_acphy (.text+0x092610) and the trigger in wlc_phy_noise_read_shmem (.text+0x0b7ecc)

`wlc_phy_noise_sample_acphy(pi)` returns 0xa4 (a noise floor of -92 dBm) if the
core clock is off (`sh+0x31 = 0`). Otherwise it reads `SHM(0x8c)` (a handshake,
value ignored), calls `wlc_phy_noise_read_shmem(pi)`, stores the result byte in
the shared noise ring `sh[0x96 + sh[0xa0]]`, advances `sh[0xa0]` modulo 8, and
returns it. Caller: `wlc_phy_noise_avg`.

`wlc_phy_noise_read_shmem(pi)` (only the part that concerns this area): for each
core it reads the 32-bit measured noise power from `SHM(0x308 + 4*c)` (low) and
`SHM(0x30a + 4*c)` (high); converts it to a per-core dBm (via `sub_0b701e`,
`wlc_phy_cmn.c`; a zero measurement becomes -92 dBm). **If the PHY type is AC
(`pi+0x160` = 0xb) and (`pi+0x18f` = 4 or `pi_ac+0x41 != 0`): it calls
`wlc_phy_crs_min_pwr_cal_acphy(pi, 0)` (C1).** That is the only path that runs
the calibration with a fresh measurement: either the sample was requested for
the calibration (reason 4, C4) or the "keep calibrating" flag `pi_ac+0x41` is
set. The rest of the function stores the per-core noise into the `pi`-level
noise ring and is `wlc_phy_cmn.c`'s business.

The model never delivers the "noise measurement done" interrupt, so this
trigger is not reached in a traced watchdog run; C1 was verified by calling it
directly (see C1).

#### C4. wlc_phy_noise_sample_request_crsmincal (.text+0x0bb8d1, name original)

`wlc_phy_noise_sample_request_crsmincal(pi)` = `sub_0baff6(pi, 4, low byte of
pi+0x17e)`: request a noise sample with reason 4 (the reason that C3 turns into
a calibration) for the current channel. `sub_0baff6` (`wlc_phy_cmn.c`, the
watchdog's noise-request helper, out of scope here) clears the per-core result
words `SHM(0x308..0x312)` and pokes `D11(0x124)` to start the measurement (seen
in the watchdog trace). Callers of the request: `wlc_phy_cals_acphy` and
`sub_09737d` (the periodic calibration path).

#### C5. wlc_phy_ed_thres_acphy (.text+0x0912a6, name original)

The energy-detect threshold iovar handler. Inputs: `pi`, `int *val`, `set`
(byte). Caller: `wlc_phy_iovar_dispatch`.

* `set = 1`: with `d = *val` (a threshold in dBm), compute
  `hi = ((d*640000 + 73045696) / 30103) & 0xffff` and
  `lo = ((d*640000 + 69205696) / 30103) & 0xffff`. Write `hi` to
  `PHY(0x33a, 0x33b, 0x33e, 0x33f, 0x342, 0x343, 0x346, 0x347)` and `lo` to
  `PHY(0x33c, 0x33d, 0x340, 0x341, 0x344, 0x345, 0x348, 0x349)` (plain writes,
  16 registers in all: the assert and de-assert thresholds of the four
  energy-detect pairs). The two numerators differ by 3,840,000 = exactly 6.0 dB.
  Verified: `d = 0` writes `hi = 0x97a`, `lo = 0x8fa`.
* `set = 0` (get): `*val = (PHY(0x33a) * 30103 - 73045696) / 640000` (the
  inverse; dBm). 30103 = 100000 x log10(2), so about 0.047 dB per register LSB.

These sixteen registers are distinct from the carrier-sense registers C2 writes.
Not reached in the traced sessions (iovar only).

### E. Periodic interference engines (from the watchdog)

Both engines are called once per watchdog tick from `wlc_phy_watchdog`
(`.text+0x0bb8ff`) after the noise-sample request; each first makes sure the
current channel's interference record exists (`sub_092efb(pi, pi+0x17e, 1)`,
A4) and stores it in `pi_ac+0x8a8`.

#### E1. wlc_phy_hwaci_engine_acphy (.text+0x0999ad, name original)

Purpose: adjacent-channel-interference mitigation by hardware measurement. It
runs a per-channel level; a higher level means more gain-limit desense. Input:
`pi`. Runs only when the interference mode `sh+0x80` has bit 1 or bit 2 set
(bit 1 = the "wide-band" detector, bit 2 = the "hwaci" detector); in the model
`sh+0x80 = 7`, so both detectors run.

Band-dependent selection: on 2.4 GHz `nlevels = pi_ac[0x6c2]`, level table
`pi_ac+0x682`; on 5 GHz `nlevels = pi_ac[0x6c3]`, table `pi_ac+0x6a2`. Bandwidth
shift `s`: 0 for 20 MHz, 1 for 40 MHz, 2 for 80 MHz.

1. If neither detector bit is set: return.
2. `rec` = the record (created if absent). If `rec[0x49] != 0` (settling):
   `rec[0x49]--` and return.
3. `level = rec[0x46]`; decrement the hold-down timer `rec[0x48] = max(0,
   rec[0x48]-1)`. Suspend the MAC.
4. **Wide-band detector** (bit 1 of `sh+0x80`): `detW = false`. For each present
   core, read four registers and accumulate: core 0 reads
   `PHY(0x7af)`, `PHY(0x7ab)`, `PHY(0x7b3)`, `PHY(0x7b1)`; core 1 reads
   `PHY(0x9af)`, `PHY(0x9ab)`, `PHY(0x9b3)`, `PHY(0x9b1)` (core 2 would use
   `PHY(0xbaf, 0xbab, 0xbb3, 0xbb1)`). Call the four running sums A, B, C, D in
   that read order. (PHY revisions >= 2 read four addresses 5/6 lower; not this
   card.) Divide each sum by the number of cores. If D < 200 and B > 199: D = B.
   Then C = B if B > 199 else 0. `detW = (C != 0 and D < 4*C)`. **(code reading;
   the registers read 0 in the model, so C = 0 and `detW = false`.)**
5. **hwaci detector** (bit 2 of `sh+0x80`): read `w=PHY(0x523)`, `x=PHY(0x529)`,
   `y=PHY(0x528)`, `z=PHY(0x527)`. Let `E` = the level table entry of the
   current level (`table + 8*level`) and `f(v) = (v & 0x7f) >> s`. `detH =
   (f(w) <= E[6])`, then refined by `E[4]`: if `E[4]=0`, AND with `((f(x) < E[5]
   and f(y)=0) ? f(z)!=0 : true)`; if `E[4]=1`, AND with `(E[5] <= f(y) or
   f(z)!=0)`; else AND with `(E[5] <= f(z))`. **(code reading; the registers
   read 0 in the model.)**
6. Update the level (`bVar2` below = the old `rec[0x46]`, `bVar7` = old
   `rec[0x47]`):
   * If old level = 0: `rec[0x46] = 1` (an unconditional first step).
   * else if `detW or detH`: `rec[0x48] = 8`; `rec[0x46]++`; `rec[0x47]` = old
     level (raise, remember the previous as the minimum).
   * else if `rec[0x48] = 0` (hold-down expired): `rec[0x46] = max(0, level-1)`;
     if that is below `rec[0x47]`, `rec[0x47]` = it (lower).
   Then clamp: `rec[0x46] = min(rec[0x46], nlevels-1)`, at least 1.
7. If the level changed (`old != rec[0x46]`): `rec[0x48] = 8`, `rec[0x49] = 2`;
   if the wide-band bit is set, write the level's `E[0..1]` as a 16-bit value to
   `PHY(0x554)` and `PHY(0x555)` (PHY rev >= 2: `PHY(0x5a4)`/`PHY(0x5a5)`).
8. If the minimum level changed (`bVar7 != rec[0x47]`): let `Emin` = the entry
   of level `rec[0x47]`. `rec[0x14] = max(0, 5 - Emin[2])`,
   `rec[0x15] = max(0, 6 - Emin[3])` (the lna1/lna2 gain-limit desense bytes of
   this record's desense set). Then save `PHY(0x19e)`, `mod(PHY(0x19e), 2, 2)`,
   `sub_0909fd(pi)` (rebuild the total desense, A4), `sub_0998cc(pi, 1)` and
   `sub_0998cc(pi, 2)` (rewrite the gain-limit tables `TBL(0x0b)`, A5), restore
   `PHY(0x19e)`, then `wlc_phy_aci_updsts_acphy(pi)` (E2).
9. Enable the MAC.

If `rec[0x49] != 0` at entry (step 2) the whole body is skipped for that tick.
In the model the first tick takes the "old level = 0" branch, clamps to 1, and
because 0 != 1 writes `PHY(0x554)=PHY(0x555)=E[1..0]` (= 0x0fa0 for the
synthetic level table); the minimum level does not change, so step 8 is not run.

#### E2. wlc_phy_aci_updsts_acphy (.text+0x092ec0, name original)

Tell the MAC whether interference mitigation is active. Input: `pi`. `active` =
`rec != 0 and (rec[0x18] != 0 or rec[0x47] != 0)` - i.e. software desense is on
(`rec[0x18]`) or the hwaci minimum level is above 0. Calls
`wlapi_high_update_phy_mode(sh+0x20, active)` (a MAC-side notification, no PHY
access). Callers: E1 and `sub_09a539` (A7 step 4).

#### E3. wlc_phy_desense_aci_engine_acphy (.text+0x09a88b, name original)

Purpose: interference mitigation by RSSI/energy statistics; sets the OFDM and
b-PHY desense of the channel. Input: `pi`. All inputs are measured and read 0 in
the model, so this whole procedure is **code reading**; the engine returns at
step 4 in every model run and changes nothing.

1. `rec` = the record (created if absent). If `rec[0x45] != 0` (settling):
   `rec[0x45]--` and return.
2. Form two metrics from the `pi` histograms (see "Data" for the selectors):
   `M_narrow = pi[0x3fa + 2*sel(0x404)] + 2*pi[0x41c + 2*sel(0x420)]`;
   `M_wide = pi[0x3f6 + 2*sel(0x400)] + 2*pi[0x414 + 2*sel(0x418)]`, where
   `sel(f) = pi[f] ? pi[f]-1 : 1`. Store `M_narrow` in the narrow ring
   `rec[0x1c + 4*rec[0x44]]`, `M_wide` in the wide ring `rec[0x30 + 4*rec[0x44]]`;
   advance `rec[0x44] = (rec[0x44]+1) mod 4`.
3. For each ring compute the average of its two largest of four entries:
   `avgN` (narrow), `avgW` (wide).
4. If `avgN < 301` **and** `rec[0x18] = 0` **and** `avgW < 601`: return.
5. Narrow path (drives the b-PHY desense `rec[0x11]`, hysteresis state
   `rec[0x2c]` lower bound, `rec[0x2d]` upper bound, `rec[0x2e]` dwell counter):
   * If `avgN >= 301` (interference): raise. `new = level + 4`, but if `level <
     rec[0x2d]` then `new = max(level+1, (rec[0x2d]+level)/2)`; set the lower
     bound `rec[0x2c] = level`, reset the counter.
   * else (`avgN < 301`): `thr = (level - rec[0x2c] == 1) ? 12 : (avgN < 100 ?
     4 : 8)`. If the counter `rec[0x2e] > thr`: lower - if `rec[0x2c] < level`,
     `new = min(level-1, (rec[0x2c]+level)/2)` (>=0); else `new = max(0,
     level-4)`; reset the counter. Otherwise `new = level` (no change).
   * Clamp `new` to <= 24. Save `rec[0x2c]`, `rec[0x2d]`, and the counter
     (`+1`, capped at 255).
6. Wide path (drives the OFDM desense `rec[0x10]`, hysteresis `rec[0x40]`,
   `rec[0x41]`, `rec[0x42]`): the same shape with `avgW`, threshold 601, the
   sub-threshold constant 300 instead of 100, and clamp <= 48.
7. If `pi_ac[0x671] != 0` (RSSI cap enabled): cap the b-PHY result to
   `max(0, rec[0x19] + 90)` and the OFDM result to `max(0, rec[0x19] + 85)`,
   where `rec[0x19]` is the RSSI-based value from E4 (so at low RSSI the desense
   is forced toward 0).
8. If the b-PHY result differs from `rec[0x11]`: store it and reset the narrow
   ring to 100,100,100,100. If the OFDM result differs from `rec[0x10]`: store
   it and reset the wide ring to 300,300,300,300.
9. `rec[0x18] = (the 9 bytes at rec[0x10] differ from the pi_ac+0x65f baseline)`
   (a memcmp; 1 = desense is active on this channel).
10. If either the OFDM or the b-PHY level changed in step 8:
    `sub_09a539(pi, 1)` (apply the desense, A7) and `rec[0x45] = 1` (settle one
    tick).

#### E4. wlc_phy_desense_aci_upd_chan_stats_acphy (.text+0x092fd8, name original)

`wlc_phy_desense_aci_upd_chan_stats_acphy(pi, chanspec, val)`: find the
interference record of `chanspec` (`sub_092efb(pi, chanspec, 0)`, **without**
creating it); if it exists, `record+0x19 = val`. This is the RSSI-based input
that E3 step 7 uses to cap the desense. Caller: `wlc_phy_interf_rssi_update`
(the RSSI-update path). No hardware access.

#### E5. wlc_phy_desense_btcoex_acphy (.text+0x09ad8c, name original)

The Bluetooth-coexistence desense iovar. Inputs: `pi`, `profile` (int, 0..6).
Caller: `wlc_phy_iovar_dispatch`.

1. Remember the old profile `pi_ac[0x8b0]`; clear the 9-byte BT desense set
   `pi_ac+0x8b4`; `pi_ac[0x8b0] = profile`; byte 8 `pi_ac[0x8bc] = (profile >
   0)`.
2. By `profile`, set bytes 4, 5, 6, 7 of the BT set (`pi_ac+0x8b8..0x8bb` =
   lna1 gain-limit, lna2 gain-limit, `elna_bypass`, `nf_hit_lna12`):

   | profile | lna1 gl | lna2 gl | elna_bypass | nf_hit |
   |---|---|---|---|---|
   | 1 | 1 | 3 | 0 | 0 |
   | 2 | 0 | 0 | 1 | 0 |
   | 3 | 0 | 2 | 1 | 2 |
   | 4 | 1 | 2 | 1 | 3 |
   | 5 | 3 | 0 | 1 | 13 |
   | 6 | 3 | 4 | 1 | 24 |

3. If 2.4 GHz and not scanning (`pi+0x19c & 0x206 = 0`): if `profile > 0` and it
   differs from the old profile, `wlc_phy_desense_aci_reset_params_acphy(pi, 0,
   0, 0)` (`acphy-init` appendix A); then `sub_09a539(pi, 1)` (apply, A7). On
   5 GHz or during a scan nothing is applied (the set is stored for
   `sub_0909fd`, which folds it in only on 2.4 GHz anyway).

### H. High-RSSI external-LNA bypass

The microcode raises `SHM(0x184) = 0xdead` when the received signal is strong;
`sub_092500` (`acphy-init` H11) reads it, clears it, and returns 1 on the event.
When the event fires the driver bypasses the external LNA for `pi_ac+0x908` low
16 bits watchdog ticks. The feature is enabled per band by `pi_ac+0x910`
(2.4 GHz) / `pi_ac+0x911` (5 GHz) and present iff `pi_ac+0x912 != 0`
(`wlc_phy_hirssi_elnabypass_init_acphy`, `acphy-attach`). In the synthetic SROM
the per-band enables are 0, so the engine returns without acting; the logic
below is from the code, with the early return verified.

#### H1. wlc_phy_hirssi_elnabypass_engine (.text+0x09b7f3, name original)

Input: `pi`. Called each watchdog tick. Let the current band be the band of
`pi+0x17e`.

1. `rem` = the current band's remaining ticks (`pi_ac+0x914` for 2.4 GHz,
   `pi_ac+0x916` for 5 GHz, s16); `en` = the current band's enable
   (`pi_ac+0x910` / `pi_ac+0x911`). Decrement the *other* band's remaining
   ticks toward -1 and store it back.
2. If `en = 0`: return.
3. Decide whether a transition happens (`apply`):
   * If `rem < 0` (bypass currently off): poll `sub_092500(pi)`. If it returns
     non-zero (event): `rem = pi_ac+0x908` low 16 bits (reload the duration),
     `apply = true`. Else `apply = false`.
   * If `rem >= 0` (bypass currently on, counting down): `rem--`. If it reached
     -1 (expired): `apply = true`, then poll `sub_092500`; if the event is still
     pending reload `rem = pi_ac+0x908`, otherwise leave `rem = -1` (bypass will
     turn off). If it did not reach -1: `apply = false`.
4. Store `rem` back into the current band's remaining-ticks field.
5. If `apply`: `wlc_phy_hirssi_elnabypass_set_ucode_params_acphy(pi)`
   (`acphy-attach`; `SHM(0x32)`, `SHM(0x180)`, `SHM(0x182)`) and
   `wlc_phy_hirssi_elnabypass_apply_acphy(pi)` (H2).

#### H2. wlc_phy_hirssi_elnabypass_apply_acphy (.text+0x09b752, name original)

Input: `pi`. Does nothing if the core clock is off (`sh+0x31 = 0`). Otherwise,
with the MAC suspended:

1. `wlc_phy_desense_aci_reset_params_acphy(pi, 0, is2g, is5g)` (`acphy-init`
   appendix A: reset the interference/desense parameters, with the two band
   flags of the current chanspec).
2. `sub_0909fd(pi)` (rebuild the total desense, A4; `sub_0909fd` sets the
   `elna_bypass` byte from the running bypass timer, so the bypass takes effect
   here).
3. `sub_09a539(pi, 0)` (apply the desense, A7).
4. `sub_09af05(pi, 1, 1, 1)` (rebuild the receive gain tables from scratch -
   init, band-change and bw-change all forced, A5) and `sub_09a121(pi)`
   (reprogram the init clip gains, A6): both are needed because bypassing the
   external LNA changes the front-end gain constants.
5. `wlc_phy_resetcca_acphy(pi)` (`acphy-init` H5), `wlc_phy_force_rfseq_acphy(pi,
   2)` (force the RF sequencer). Enable the MAC.

#### H3. wlc_phy_hirssi_elnabypass_status_acphy (.text+0x0909a4, name original)

Input: `pi`. Result (uint): whether the high-RSSI bypass is active/pending for
the current band. No callers in the object (an iovar/debug hook reached through
a table). Reads the current band's remaining-ticks field
(`pi_ac+0x914`/`+0x916`); the default result is "the timer is not negative"
(bit 15 of the inverted value). If the core clock is on and the high byte of the
timer indicates a pending state, it reads `SHM(0x184)` and reports whether it
equals 0xdead. No state change.

## Verification

All runs use the model's `--done` option (calibrations finish at once) and the
scratch harness `re-out\analysis\acphy-desense\run.py` (built on the unchanged
tools; brings the card up, then runs direct calls / pokes / watchdog ticks and
prints the accesses of each action). PHY rev 1, radio rev 4, chip 0x4360, two
cores, synthetic 2x2 SROM.

| Item | Checked how | Result |
|---|---|---|
| C1 calibrate path | `run crs` : `wlc_phy_crs_min_pwr_cal_acphy(pi, 0)` on channel 1 | MAC suspended; `sub_08f41b(pi, 0x50, 0, 0)` called; `pi_ac+0x42 = 0x50`; reference `pi_ac+0x18/+0x19 = 1`; `pi_ac+0x3d/+0x3e = 1`; `pi_ac+0x43` counted up; `pi_ac+0x44 = 1` (channel); `pi_ac+0x14 = 0` (measured) - matches C1 with the 20 MHz table clamped to index 14 (0x50) |
| C2 register set | same run: the `sub_08f41b` calls of C1, and a direct `sub_08f41b(pi, 0x36, 0, 0)` | core-0 low byte of `PHY(0x321,0x324,0x327,0x32a,0x32d,0x330,0x333,0x336)` written (0x3636->0x3650, then ->0x3636), high byte preserved; `PHY(0x910..0x913)` both bytes = the offset (0) - as C2 |
| C1 restore path | `run ver` : `wlc_phy_crs_min_pwr_cal_acphy(pi, 1)` on a fresh card | no ring push; reads reference `pi_ac+0x18 = 0xe2` (-30), applies `sub_08f41b(pi, 0x36, 0, 0)` (registers 0x36->0x36) - as C1 restore branch |
| C5 ED threshold | `run ver` : `wlc_phy_ed_thres_acphy(pi, {0}, 1)` | `PHY(0x33a,0x33b,0x33e,0x33f,0x342,0x343,0x346,0x347) = 0x97a`; `PHY(0x33c,0x33d,0x340,0x341,0x344,0x345,0x348,0x349) = 0x8fa` - matches the formula for d = 0 |
| Interference mode | `run ver` : dump `sh+0x80` | 7 (bits 0,1,2) - E1's two detectors both run |
| E1 addresses and level | `run wd0` : one watchdog tick (3.5 s) | `wlc_phy_hwaci_engine_acphy` reads `PHY(0x7af,0x7ab,0x7b3,0x7b1)` (core 0), `PHY(0x9af,0x9ab,0x9b3,0x9b1)` (core 1), `PHY(0x523,0x529,0x528,0x527)`; level 0->1 writes `PHY(0x554)=PHY(0x555)=0x0fa0`; MAC suspended/enabled around it - as E1 |
| E3 early return | `run wd2` : watchdog tick, `des` before and after | applied desense `pi_ac+0x656` stays all 0; the desense-aci engine makes no PHY access (returns at step 4, measured stats 0); `pi_ac+0x671 = 1` observed |
| H early return | `run wd2` : `des` prints the high-RSSI state | `pi_ac+0x910 = pi_ac+0x911 = 0` (disabled), `pi_ac+0x912 = 1` (present), timers -1/-1; the engine makes no PHY access |
| Noise-sample request | `run wd0` : `sub_0baff6` in the tick | writes `SHM(0x308..0x312)` (the per-core result words C3 reads) and `D11(0x124)`; the CRS cal is not reached because the model delivers no "noise done" interrupt |
| R3 desense-apply `on != 0` | reached by forcing a desense change through E1's minimum-level branch (`sub_0909fd`/`sub_0998cc` re-run observed) | `sub_09a539`'s `on != 0` steps run as A7 describes |

## Open questions

* The writer of `pi_ac+0x14+c` (the per-core input the CRS calibration averages)
  was not located; it reads 0 in the model. With a real, non-zero measurement
  the calibration would pick a lower index of the CRS tables and a lower
  minimum power than the 0x50/0x4c/0x43 the model produces.
* The meaning of the carrier-sense registers `PHY(0x321..0x336)` (four
  low-byte/high-byte pairs written by `sub_08f41b`/`sub_08f84d`) and
  `PHY(0x910..0x913)`, and of the energy-detect registers `PHY(0x33a..0x349)`,
  is an interpretation from the calling context (carrier sense / energy detect
  minimum power). The accesses and the scaling formula (C5) are exact.
* The hwaci detector logic (E1 steps 4-5) and the whole desense-aci engine
  decision (E3) were read from the code only: every measured input reads 0 in
  the model, so only the register addresses, the constants and the fact that
  the engines run were verified against the object. The comparison against a
  real card's measurements is not possible here.
* The interference mode `sh+0x80` is 7 in the model (bits 0, 1, 2), so both
  detector branches of E1 run. Which SROM/iovar values map to which mode, and
  what bit 0 selects, is `acphy-attach`/`acphy-chanspec` (section 4) territory.
* The hwaci level tables `pi_ac+0x682`/`+0x6a2` and the counts `pi_ac+0x6c2/3`
  are filled by `wlc_phy_hwaci_setup_acphy` (`acphy-init` appendix A); their
  values on a real Apple board are the SROM's and are not known (the synthetic
  level-1 entry begins 0x0fa0).
* PHY revisions 0 and 1 are identical throughout this area by construction: the
  revision-dependent selects test `rev < 2` (C2, E1: `PHY(0x554)/0x555` vs
  `0x5a4/0x5a5`, the four E1 measurement addresses) or `rev in {2,3,5,6}` (C2,
  the `sub_08f84d` band defaults), and both 0 and 1 fall on the same side of
  each. So the rev-0 path was not re-run separately; the emulator figures above
  are rev 1.
