# Calibration control and transmit IQ/LO calibration of the AC-PHY (BCM4360)

Status: work in progress, written function by function. Everything below was
read in the decompiler output and checked in the disassembly; what was also
checked against emulator traces is listed in "Verification". Register and
table *names* are not known; aliases in parentheses are mine.

## Scope

Chip 0x4360 (0x4352 and 43526 = 0xaa06 take the same branches unless said
otherwise), AC-PHY revision 0 or 1, radio 2069 revision 3 or 4 (major revision
0), two chains. Register access notation: `access.md`. Functions already
specified elsewhere that this area calls are referred to, not repeated:
`sub_09c4e4` (set tx bbmult), `sub_098751` (get tx bbmult), `sub_09bf99`
(read/write cal coefficients), `sub_09c161` (apply cal coefficients),
`sub_0affa9` (idle-TSSI), `sub_0b1227` (pre-cal tx gain), `sub_098949`,
`wlc_phy_txpwrctrl_enable_acphy`, `sub_092ffa`, `sub_09311b`/`sub_093e47`
(radio VCO cal) - all in `acphy-txpower.md`, `acphy-init.md`, `acphy-radio.md`.
The receive IQ calibration `sub_0addfa` is owned by `acphy-cal-rx`; here it is
only named as a step of the state machine.

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_phy_cals_acphy` | 0x0b13b5 | 1260 | original |
| `sub_0925cc` | 0x0925cc | 68 | assigned: `wlc_phy_cal_suspend_setup_acphy` |
| `wlc_phy_cal_perical` | 0x0b57d3 | 903 | original (wlc_phy_cmn.c) |
| `wlc_phy_cal_perical_mphase_reset` | 0x0b29c1 | 82 | original (wlc_phy_cmn.c) |
| `wlc_phy_cal_perical_mphase_restart` | 0x0b2488 | 28 | original (wlc_phy_cmn.c) |
| `sub_0b2a90` | 0x0b2a90 | 88 | assigned: `wlc_phy_cal_perical_mphase_schedule` (wlc_phy_cmn.c) |
| `sub_0b56ce` | 0x0b56ce | 261 | assigned: `wlc_phy_cal_perical_mphase_tmr_cb` (wlc_phy_cmn.c) |
| `wlc_phy_cal_init` | 0x0b19f7 | 479 | original (wlc_phy_cmn.c) |
| `wlc_phy_initcal_enable` | 0x0b19aa | 29 | original (wlc_phy_cmn.c) |
| `wlc_phy_cal_mode` | 0x0b2467 | 20 | original (wlc_phy_cmn.c) |
| `wlc_phy_watchdog` | 0x0bb8ff | 3827 | original (wlc_phy_cmn.c); only the calibration trigger |
| `wlc_phy_tx_tone_acphy` | 0x0ab82c | 1098 | original |
| `wlc_phy_stopplayback_acphy` | 0x09c5c3 | 169 | original |
| `wlc_phy_cordic` | 0x0b2317 | 239 | original (wlc_phy_cmn.c); math helper, no access |
| `sub_0abc76` | 0x0abc76 | 7204 | assigned: `wlc_phy_cal_txiqlo_acphy` |
| `sub_09380f` | 0x09380f | 1592 | assigned: `wlc_phy_txcal_radio_setup_acphy` |
| `sub_0948c3` | 0x0948c3 | 599 | assigned: `wlc_phy_txcal_radio_restore_acphy` |
| `sub_097562` | 0x097562 | 679 | assigned: `wlc_phy_txcal_phy_restore_acphy` |
| `sub_09c66c` | 0x09c66c | 420 | assigned: `wlc_phy_txcal_txgain_save_set_acphy` |
| `sub_09c810` | 0x09c810 | 269 | assigned: `wlc_phy_txcal_txgain_restore_acphy` |
| `sub_09c91d` | 0x09c91d | 337 | assigned: `wlc_phy_txcal_gainlut_load_acphy` |
| `wlc_phy_populate_tx_loft_comp_tbl_acphy` | 0x09ca6e | 576 | original |
| `wlc_phy_scanroam_cache_cal_acphy` | 0x09c28b | 601 | original |

## Overview

### What a "calibration run" is

After the driver associates (and periodically afterwards) the transmitter and
receiver are calibrated. `wlc_phy_cals_acphy` is the AC-PHY entry point that
runs, in this order:

1. **Pre-cal transmit gain** (`sub_0b1227`, `acphy-init.md` section 7): pick the
   gain-table index the calibration transmits at and store its gain record.
2. **Idle TSSI** (`sub_0affa9`, `acphy-txpower.md` section 23): only in the
   first calibration after an association (`pi+0xf84`).
3. **Transmit IQ imbalance and LO leakage calibration** (`sub_0abc76`, this
   spec): played as a loopback measurement (tx -> internal receive path); it
   finds compensation coefficients and programs them.
4. **Receive IQ calibration** (`sub_0addfa`, `acphy-cal-rx`).
5. A marker word 0xacdc written to `TBL(0x0c)[0x5f]`, then the results are
   copied into the scan/roam cache (`wlc_phy_scanroam_cache_cal_acphy`).

Around the whole run the driver forces both hardware chains active
(`wlc_phy_rxcore_setstate_acphy`), suspends the MAC, enters PHY-register access
(`wlc_phyreg_enter`), turns the transmit power control off, and restores all of
this at the end.

### Single-shot vs. multi-phase

The same steps run in one of two ways, selected by the perical mode `pi+0xf89`
(2 on the emulated board):

* **Single-shot** (`pi+0xf89` = 1, or any forced full calibration): one call
  `wlc_phy_cals_acphy(pi, 0)` does everything (state-machine phase 0).
* **Multi-phase** (`pi+0xf89` = 2): the steps are spread over a repeating timer
  (`phycal` timer, first tick 5 ms, then `pi+0xf8a` ms). Each tick runs one
  phase and advances a phase counter (`(pi+0xf58)[1]`). The phases are 1
  (pre-cal gain), 2..0xc (transmit cal part A), 0xe..0x10 (transmit cal part
  B), 0x11 (receive cal), 0x12 (VCO cal), 0x13 (idle TSSI, only after an
  association). This spreads the ~1 ms of tone playback per step so the MAC is
  not held off air for the whole run at once.

### Who starts a calibration

* `wlc_phy_watchdog` (once a second) calls `wlc_phy_cal_perical(pi, 2)` when the
  periodic interval `sh+0x7c` has elapsed since the last calibration; inside,
  `wlc_phy_cal_perical` gates on temperature change, elapsed time and channel.
* `wlc_init`/`wlc_full_phy_cal` call `wlc_phy_cal_perical(pi, 4..6)` for a
  forced full calibration (also sets "first cal after association").
* The `phycal` timer callback (`sub_0b56ce`) drives the multi-phase sequence.
* Direct: `wlc_phy_init_test_acphy` (`acphy-init.md` H14).

### The tone / sample player

`wlc_phy_tx_tone_acphy` builds a complex sinusoid with `wlc_phy_cordic`, writes
it as a sample table (`TBL(0x0e)`) and starts the PHY sample player (or the
RF-sequencer tone). `wlc_phy_stopplayback_acphy` stops it. Both are used by the
calibrations and by the idle-TSSI/power measurement (`acphy-txpower.md`
sections 23/24).

## Data

### The calibration state block (`cal`, at `pi+0xf58`)

`pi+0xf58` holds a pointer to a per-PHY calibration state block (allocated at
attach). Fields used here (offsets from the block; s = signed):

| Offset | Size | Content |
|---|---|---|
| 0x00 | u8 | mphase "search mode": 2nd argument passed to `wlc_phy_cals_acphy` from the timer (0 or 1); set to 1 by `wlc_phy_cal_perical` when it arms the sequence |
| 0x01 | u8 | mphase **phase counter** (0 = idle / single-shot; 1..0x13 = the phase being run) |
| 0x02 | u8 | mphase sub-phase, cleared on reset/restart (used inside `sub_0abc76`) |
| 0x04.. | | final result coefficients per core (written through `sub_09bf99` selections 16..19; `acphy-txpower.md` section 21) |
| 0x2c.. | | intermediate result coefficients per core (selections 12..15) |
| 0x54.. | | second copy for `sub_09bf99` mode 2 |
| 0x64 | u8 | "restart-from-previous-coeffs allowed" flag; gates the search mode of `sub_0abc76` |
| 0x65 + c | u8 | per-core flag cleared by `sub_0b1227` |
| 0x92 + 10*c | 10 B | per-core pre-cal transmit gain record (filled by `sub_0b1227`; format in `acphy-txpower.md` "Record transmit gain setting") |
| 0xba | u16 | chanspec the last calibration ran on |
| 0xc0 | u32 | time (`sh+0x34`) of the last calibration |
| 0xc4 | u32 | time of the last temperature reading |
| 0xc8 | u32 | set to `sh+0x7c` when perical decides no calibration is needed (throttle) |
| 0xcc | s16 | last calibration temperature (`wlc_phy_tempsense_acphy`) |

### Fields of `pi` used by the scheduler

| Field | Type | Content |
|---|---|---|
| `pi+0x160` | u32 | PHY type (0xb = AC; 4 = N, 7 = HT) |
| `pi+0x164` | u32 | PHY revision |
| `pi+0x17e` | u16 | current chanspec |
| `pi+0x18d` | u8 | set 1 at entry of `wlc_phy_cal_perical`, cleared when it proceeds (purpose unknown) |
| `pi+0x18e` | u8 | set to (PHY revision > 0x12); false here (purpose unknown) |
| `pi+0x199` | u8 | when non-zero the watchdog does not start a calibration (interpretation: scan/association in progress) |
| `pi+0x19c` | u32 | hold flags: bit 4 (0x10) blocks `wlc_phy_cals_acphy`; bits 0x21e block the timer callback; bit 5 (0x20) blocks a periodic AC calibration |
| `pi+0xf84` | u8 | "first calibration after association": forces the idle-TSSI step; set by `wlc_phy_cal_perical` reasons 4..6, cleared at the end of the run |
| `pi+0xf86` | u8 | per-phase "collect result" mask (bits 0x10, 1, 4, 8 tested by phases; sets `pi_ac+0x44c`); 0 on this board |
| `pi+0xf88` | u8 | radio-on flag (checked elsewhere) |
| `pi+0xf89` | u8 | perical mode: 0 = disabled, 1 = single-phase, 2 = multi-phase, 3 = disabled-2 |
| `pi+0xf8a` | u16 | multi-phase timer interval in ms (5 here) |
| `pi+0xf9c` | u8 | temperature-change threshold that forces a recalibration (0 = never) |
| `pi+0x1088` | ptr | the `phycal` timer handle (0 if none) |
| `pi+0x30` | ptr | function pointer "calibration init" (`sub_08e77a`), called by `wlc_phy_cal_init` |
| `pi+0x130` | ptr | function pointer used by `wlc_phy_cal_mode` (null for AC) |

### Fields of `sh` used by the scheduler

| Field | Size | Content |
|---|---|---|
| `sh+0x34` | u32 | free-running time counter (incremented by the MAC watchdog; unit ~ 1 s) |
| `sh+0x78` | u32 | secondary calibration interval (G-PHY nrssi; not AC) |
| `sh+0x7c` | u32 | periodic calibration interval (0x78 = 120 here) |
| `sh+0x3c` | u32 | chip id (0xa8e5 gets a special timer interval) |
| `sh+0xa4`, `sh+0xa5` | u8 | hardware transmit / receive chain masks (3) |
| `sh+0xa6`, `sh+0xa7` | u8 | current transmit / receive chain masks (saved and forced to the hardware masks around a run) |

### Fields of `pi_ac` used here (beyond those of other specs)

| Field | Size | Content |
|---|---|---|
| `pi_ac+0x02 + 2*c` | u16 | per-core bbmult saved by `wlc_phy_tx_tone_acphy` before it forces the tone bbmult |
| `pi_ac+0x0a` | u8 | "tone bbmult saved" flag (1 while a tone is up) |
| `pi_ac+0x33c` | u8 | CRS-min-power calibration enabled (1); read at the start of a run |
| `pi_ac+0x33d` | u8 | set 1 to request the CRS-min-power calibration (owner `acphy-desense`) |
| `pi_ac+0x382` | u8 | N-PHY only (`wlc_phy_initcal_enable`, mphase reset); not AC |
| `pi_ac+0x412 + 0xe*c` | 14 B | scan/roam calibration-result cache, one record per core (same layout as the `sub_09c161` record, `acphy-txpower.md` section 20) |
| `pi_ac+0x44a` | u16 | cache-valid marker (0xacdc when the cache holds results) |
| `pi_ac+0x44c` | u8 | per-phase "collect result" flag (from `pi+0xf86`) |
| `pi_ac+0x48..0x88 (+2*c)` | u16 | radio-register save area of the transmit cal (`sub_09380f`/`sub_0948c3`) |
| `pi_ac+~0x90..0x110` | u16 | PHY-register save area restored by `sub_097562` |

### The `phycal` shared-memory word

Each phase writes a duration hint to `SHM(0xb8)` through `sub_0925cc` (the
number of microseconds the PHY expects to hold the channel; the microcode uses
it to avoid transmitting during the calibration - interpretation): 29000 for
the single-shot phase, 0x3c (60) for the pre-cal-gain phase, 0x1130 (4400) for
each transmit-cal phase, 0x251c (9500) for the receive-cal phase, 300 for the
VCO phase, 0x60e (1550) for the idle-TSSI phase.

### Constant gain schedules used by `sub_09c91d`

Two 18-entry tables of byte pairs (high, low) at `.rodata+0x2cc930` (table A)
and `.rodata+0x2cc900` (table B):

```
A (.rodata+0x2cc930): (03,00)(04,00)(06,00)(09,00)(0d,00)(12,00)(19,00)
                      (19,01)(19,02)(19,03)(19,04)(19,05)(19,06)(19,07)
                      (23,07)(32,07)(47,07)(64,07)
B (.rodata+0x2cc900): (03,00)(04,00)(06,00)(09,00)(0d,00)(12,00)(19,00)
                      (23,00)(32,00)(47,00)(64,00)(64,01)(64,02)(64,03)
                      (64,04)(64,05)(64,06)(64,07)
```

`sub_09c91d(pi, scale)` writes A to `TBL(0x0c)[0..0x11]` and B to
`TBL(0x0c)[0x20..0x31]`, one 16-bit entry each = `((high*scale/100) << 8) | low`
(high byte is a digital tone gain scaled by `scale` %, low byte a gain index
0..7). Interpretation: the gain sweep the tone plays at during the transmit
calibration (the loft/iqcal "gain steps").

## Procedures

`mod(reg, mask, value)` is read-modify-write. "For each core c" means
c = 0 .. N-1, N = `pi+0x168` (2). "Table access bracket" is as defined in
`acphy-txpower.md`: read `PHY(0x19e)`, set its bit 1, do the work, restore bit 1.

### 1. wlc_phy_cals_acphy (.text+0x0b13b5, name original)

Purpose: run (or advance) a calibration. Inputs: `pi`; `mode` (u8: the
mphase "search mode", forwarded to the transmit cal). No result. Callers:
`wlc_phy_cal_perical`, `sub_0b56ce` (timer), `wlc_phy_init_test_acphy`.

`cal` = `pi+0xf58`; `phase` = `cal[1]`.

Common part (always):

1. If bit 4 (0x10) of `pi+0x19c` is set: return at once (no access).
2. Save `sh+0xa7`, `sh+0xa6`; set `sh+0xa7` = `sh+0xa5`, `sh+0xa6` = `sh+0xa4`
   (force the hardware chain masks); `wlc_phy_rxcore_setstate_acphy(pi, sh+0xa5)`
   (`acphy-init.md` H7: suspends and re-enables the MAC by itself).
3. If (`phase` = 0x11 or `phase` = 0) and `pi_ac+0x33c` != 0: `pi_ac+0x33d` = 1;
   `wlc_phy_noise_sample_request_crsmincal(pi)` (owner `acphy-desense`).
4. Compute the search mode `sm` passed to the transmit cal: `sm` = `mode` only
   if the chanspec `pi+0x17e` equals `cal+0xba` (same channel as last time) **and**
   `cal+0x64` != 0; otherwise `sm` = 0.
5. If `phase` > 1 and `cal+0xba` != chanspec: `wlc_phy_cal_perical_mphase_restart(pi)`
   (the channel changed mid-sequence - start over).
6. Save `en` = `pi+0xfa0` (the transmit-power-control enable state).

Then a branch on `phase`:

**phase 0 (single-shot, everything):**

1. `sub_0925cc(pi, 29000)` (section 2: `SHM(0xb8)`, suspend MAC, enter phyreg,
   power control off).
2. `cal+0xc0` = `sh+0x34`; `cal+0xba` = chanspec.
3. If `pi+0xf84` != 0: `sub_0affa9(pi)` (idle TSSI).
4. `sub_0b1227(pi, cal+0x92)` (pre-cal tx gain).
5. `sub_0abc76(pi, sm, 0, 0)` then `sub_0abc76(pi, sm, 0, 1)` (transmit cal,
   both halves; section 15).
6. `sub_0addfa(pi)` (receive cal; `acphy-cal-rx`).
7. Table access bracket: `TBL(0x0c)[0x5f]` = 0xacdc (one 16-bit entry).
8. `pi+0xf84` = 0; `pi_ac+0x44a` = 0; `wlc_phy_scanroam_cache_cal_acphy(pi, 1)`
   (section 14: cache the results).
9. Cleanup (below).

**phase 1 (pre-cal gain):** `sub_0925cc(pi, 0x3c)`; `cal+0xc0` = `sh+0x34`;
`cal+0xba` = chanspec; `sub_0b1227(pi, cal+0x92)`; advance (`cal[1] += 1`);
cleanup.

**phases 2..0xc (transmit cal, part A):** if `phase` > 10 and PHY revision != 1:
skip (go to phase 0xd, i.e. just advance and return without cleanup). Otherwise:
`sub_0925cc(pi, 0x1130)`; if bit 4 of `pi+0xf86`: `pi_ac+0x44c` = 1;
r = `sub_0abc76(pi, sm, 1, 0)`; if r != 0: `wlc_phy_cal_perical_mphase_reset(pi)`
and cleanup (abort the sequence); if `phase` = 0xc: `cal[1] += 1` (extra); then
advance and cleanup. (So for PHY revision 1 part A occupies phases 2..0xc; for
PHY revision 0, phases 2..0xa, and 0xb/0xc are skip-advances.)

**phase 0xd (gap):** `cal[1] += 1`; return (no cleanup - nothing was suspended
on this tick).

**phases 0xe..0x10 (transmit cal, part B):** `sub_0925cc(pi, 0x1130)`; if bit 4
of `pi+0xf86`: `pi_ac+0x44c` = 1; r = `sub_0abc76(pi, sm, 1, 1)`; if r != 0:
reset and cleanup; advance and cleanup.

**phase 0x11 (receive cal):** `sub_0925cc(pi, 0x251c)`; if bit 0 of `pi+0xf86`:
`pi_ac+0x44c` = 1; `sub_0addfa(pi)`; `pi_ac+0x44a` = 0;
`wlc_phy_scanroam_cache_cal_acphy(pi, 1)`; advance and cleanup.

**phase 0x12 (VCO cal):** `sub_0925cc(pi, 300)`; if bit 2 of `pi+0xf86`:
`pi_ac+0x44c` = 1; `sub_09311b(pi)` (VCO cal restart, `acphy-radio.md`);
`sub_093e47(pi, 1)` (VCO cal wait); `cal+0xc0` = `sh+0x34`; `cal+0xba` = chanspec;
if `pi+0xf84` = 0: write the 0xacdc marker to `TBL(0x0c)[0x5f]` and
`wlc_phy_cal_perical_mphase_reset(pi)` (sequence finished); else advance to 0x13.

**phase 0x13 (idle TSSI, only if an association forced it):** `sub_0925cc(pi,
0x60e)`; if bit 3 of `pi+0xf86`: `pi_ac+0x44c` = 1; `sub_0affa9(pi)`; `pi+0xf84`
= 0; write the 0xacdc marker to `TBL(0x0c)[0x5f]`; `wlc_phy_cal_perical_mphase_reset(pi)`.

**other phases:** `wlc_phy_cal_perical_mphase_reset(pi)` and return.

Cleanup (the phases that suspended the MAC end here): `wlc_phy_txpwrctrl_enable_acphy(pi, en)`
(restore power control); `wlc_phyreg_exit(pi)`; `wlapi_enable_mac(physhim)`;
restore `sh+0xa7`, `sh+0xa6`; `wlc_phy_rxcore_setstate_acphy(pi, sh+0xa7)`.

Notes:

* The 0xacdc marker in `TBL(0x0c)[0x5f]` is written only when a full run
  finished (phase 0, phase 0x12 without a pending idle-TSSI, phase 0x13).
  Interpretation: "calibration coefficients valid" flag for the microcode.
* The skip-advance phases (0xd, and 0xb/0xc for PHY revision 0) return without
  the cleanup, but they also did not suspend the MAC (`sub_0925cc` was not
  called); the only access they make is the `wlc_phy_rxcore_setstate_acphy` of
  the common part. On this board `sh+0xa7` already equals `sh+0xa5`, so that
  call changes nothing.

### 2. sub_0925cc (.text+0x0925cc, name assigned: wlc_phy_cal_suspend_setup_acphy)

Inputs: `pi`, `dur` (u16). Steps: `wlapi_bmac_write_shm(physhim, 0xb8, dur)`
(write `SHM(0xb8)` = dur); `wlapi_suspend_mac_and_wait(physhim)`;
`wlc_phyreg_enter(pi)`; `wlc_phy_txpwrctrl_enable_acphy(pi, 0)` (power control
off). Called at the start of every working phase; the matching resume is in the
cleanup of `wlc_phy_cals_acphy`.

### 3. wlc_phy_cal_perical (.text+0x0b57d3, name original, wlc_phy_cmn.c)

Purpose: decide whether/how to calibrate. Inputs: `pi`, `reason` (u8). No
result. Callers: `wlc_phy_watchdog` (reason 2), `wlc_init`/`wlc_full_phy_cal`
(reasons 4..6), `wlc_phy_init_nphy`. Only the AC path (`pi+0x160` = 0xb) is
described.

1. `cal+0xc8` = 0; `pi+0x18d` = 1.
2. If the PHY type is not 4, 7 or 0xb: return. If `pi+0xf89` (perical mode) is
   0 or 3: return.
3. `reason` = 2 (periodic):
   1. If bit 5 (0x20) of `pi+0x19c` is set and PHY type = 0xb: return.
   2. `pi+0x18d` = 0; `pi+0x18e` = (PHY revision > 0x12).
   3. If `pi+0xf9c` != 0 (temperature gating on): for AC, read the current time
      `t` = `sh+0x34`; if `t - cal+0xc4` < `sh+0x7c` use the cached temperature
      `cal+0xcc`, else set `cal+0xc4` = `t` and measure
      `temp` = `wlc_phy_tempsense_acphy(pi)`. If |temp - `cal+0xcc`| <
      `pi+0xf9c` **and** `t - cal+0xc0` < 900 **and** `cal+0xba` = chanspec:
      set `cal+0xc8` = `sh+0x7c` and **return** (no calibration - temperature
      stable, recent, same channel). Otherwise `cal+0xcc` = temp.
   4. If `pi+0xf89` = 1 (single-phase): `wlc_phy_cals_acphy(pi, 0)`; return.
   5. If `pi+0xf89` = 2 (multi-phase): if `cal[1]` != 0 return (already running);
      else `cal[0]` = 1 and go to step 6.
4. `reason` = 3: only for mode 2: if `cal[1]` != 0 `wlc_phy_cal_perical_mphase_reset(pi)`;
   `cal[0]` = 0; go to step 6.
5. `reason` = 4, 5 or 6 (forced full): if mode 2 and `cal[1]` != 0,
   `wlc_phy_cal_perical_mphase_reset(pi)`; `pi+0xf84` = 1 (first cal after
   association); if `pi+0xf9c` != 0 measure `wlc_phy_tempsense_acphy` into
   `cal+0xcc`; then `wlc_phy_cals_acphy(pi, 0)` (single-shot) and return.
6. `cal[0]` = 1; `sub_0b2a90(pi, 5)` (arm the multi-phase timer at 5 ms;
   section 4).

Note: the second argument of `wlc_phy_cals_acphy` is 0 on every path here; only
the timer callback passes `cal[0]`.

### 4. sub_0b2a90 (.text+0x0b2a90, name assigned: wlc_phy_cal_perical_mphase_schedule, wlc_phy_cmn.c)

Inputs: `pi`, `delay` (ms). If `pi+0xf89` is 2 or 3: `wlapi_del_timer(physhim,
pi+0x1088)`; `cal[1]` = 1 (start at phase 1); `wlapi_add_timer(physhim,
pi+0x1088, delay, 0)` (one-shot; the callback re-arms). Otherwise nothing.
Also called by `wlc_phy_trigger_cals_for_btc_adjust` (BT-coex recalibration).

### 5. sub_0b56ce (.text+0x0b56ce, name assigned: wlc_phy_cal_perical_mphase_tmr_cb, wlc_phy_cmn.c)

The `phycal` timer callback (registered at `wlc_phy_attach`). Inputs: `pi`.

1. `iv` = `pi+0xf8a` (or 0x28 if chip id `sh+0x3c` = 0xa8e5).
2. If `cal[1]` = 0: return (nothing armed).
3. If `sh+0x30` (interface up) = 0: `wlc_phy_cal_perical_mphase_reset(pi)`; return.
4. If (`pi+0x19c` & 0x21e) = 0: run the current phase - for AC,
   `wlc_phy_cals_acphy(pi, cal[0])`. Otherwise (a hold flag is set): `cal[1]` = 1,
   `cal[2]` = 0, `iv` = 1000 (postpone the whole sequence by 1 s).
5. If `cal[1]` != 0 after the run: `wlapi_add_timer(physhim, pi+0x1088, iv, 0)`
   (re-arm). Else `wlc_phy_cal_perical_mphase_reset(pi)` (finished).

So the sequence is: 5 ms after arming, phase 1 runs; then every `iv` ms
(5 ms here) the next phase runs, until a phase resets the counter to 0.

### 6. wlc_phy_cal_perical_mphase_reset (.text+0x0b29c1, name original, wlc_phy_cmn.c)

Inputs: `pi`. If `pi+0x1088` != 0: `wlapi_del_timer(physhim, pi+0x1088)`.
(N-PHY: `pi_ac+0x382` = 0.) `cal[1]` = 0; `cal[2]` = 0.

### 7. wlc_phy_cal_perical_mphase_restart (.text+0x0b2488, name original, wlc_phy_cmn.c)

Inputs: `pi`. `cal[1]` = 1; `cal[2]` = 0. (Restart the sequence from phase 1
without touching the timer.)

### 8. wlc_phy_cal_init (.text+0x0b19f7, name original, wlc_phy_cmn.c)

Purpose: set the generic calibration search parameters once, and call the
AC-specific calibration-init function pointer. Inputs: `pi`. Caller:
`wlc_bmac_init`. Makes no hardware access itself.

1. If PHY type = 0: `pi+0xc36` = 0x18.
2. If `pi+0x187` != 0 (already initialised): return.
3. `pi+0x488` = `pi+0x48a` = 100; `pi+0x48c` = 0 (u32); `pi+0x490` = 200 (AC/HT
   and N revisions 3..15; 800 otherwise); `pi+0x492..0x4a0` (8 x u16) = 100.
4. A block of u16 search step/threshold fields set to fixed values:
   `pi+0x3ea/0x3ec/0x3ee` = 10, `pi+0x3f0` = 0, `pi+0x3f2/0x3f4` = 0x14,
   `pi+0x3f6/0x3f8/0x3fa/0x3fc` = 10, `pi+0x3e8` (= "pi+1000") = 10,
   `pi+0x414/0x416/0x41c/0x41e` = 10, `pi+0x2e4/0x2e6` = 10,
   `pi+0x2e8` = 0x14 (AC/HT/N rev 3..15; else 0x50), `pi+0x300/0x302` = 0,
   `pi+0x408/0x40a/0x40c/0x40e` = 10, `pi+0x410/0x412` = 0x14,
   `pi+0x418/0x420` = 0 (u32).
5. If `pi+0x30` (calibration-init fn ptr, `sub_08e77a`) != 0: call it.
6. `pi+0x187` = 1.

(These `pi+0x2e4..0x4a0` fields are the per-command iteration counts and step
sizes the transmit cal reads; see section 15. Their meaning per command is not
resolved.)

### 9. wlc_phy_initcal_enable (.text+0x0b19aa, name original, wlc_phy_cmn.c)

Inputs: `pi`, `en` (u8). Only for PHY type 4 (N-PHY): `pi_ac+0x381` = `en`. For
the AC-PHY it does nothing. Caller: `wlc_bmac_set_chanspec`.

### 10. wlc_phy_cal_mode (.text+0x0b2467, name original, wlc_phy_cmn.c)

Inputs: `pi`. If the function pointer `pi+0x130` is not null, call it. It is
null for the AC-PHY, so this does nothing; no caller inside the object.

### 11. wlc_phy_watchdog, calibration trigger only (.text+0x0bb8ff, name original, wlc_phy_cmn.c)

`wlc_phy_watchdog(pi)` (called once a second by `wlc_bmac_watchdog`) contains,
for PHY types 0xb and 7, this trigger:

If no other calibration ran this tick, and `pi+0x199` = 0, and `pi+0xf89`
(perical mode) is neither 0 nor 3, and `cal+0xc8` = 0, and `sh+0x7c` <=
`sh+0x34 - cal+0xc0` (at least `sh+0x7c` time units since the last
calibration): call `wlc_phy_cal_perical(pi, 2)`.

So on this board a periodic calibration is considered every second and actually
starts about every `sh+0x7c` = 120 units, then gated further by temperature,
900-unit recency and channel inside `wlc_phy_cal_perical`.

### 12. wlc_phy_tx_tone_acphy (.text+0x0ab82c, name original)

Purpose: play a complex tone (or start the RF-sequencer tone) on all transmit
cores. Inputs: `pi`; `freq` (int, kHz); `amp` (u16, amplitude 0..0xffff);
`dont_deaf` (u8: if 0 the receiver is put into carrier-search around the
playback); `rfseq` (u8: 1 = use the RF-sequencer tone, else the sample player);
`set_bbmult` (u8). Result: 0, or 0xffffffff on a memory-allocation failure.
`pi_ac` = `pi+0x138`.

Number of samples: `ns` = 20 for 20 MHz, 40 for 40 MHz (chanspec bandwidth
field `pi+0x17e` & 0x3800 = 0x1800), 80 for 80 MHz (= 0x2000). The sample table
length is `L` = 2 * `ns` (40 / 80 / 160). (Default `L` = 1 if `amp` = 0.)

1. If `amp` != 0 (build and load the samples):
   1. For i = 0 .. L-1: `wlc_phy_cordic(theta, &s)` gives s[0] = I, s[1] = Q
      (see section 13); scale each by `amp`: `I = round(amp * I / 2^15)`,
      `Q = round(amp * Q / 2^15)` (round toward nearest, halves toward zero);
      advance `theta += ((freq * 36 / ns) << 16) / 100`. `theta` starts at 0.
   2. Table access bracket. Pack each sample into 20 bits `((Q & 0x3ff) << 10)
      | (I & 0x3ff)` and write them as one transfer of `L` entries of 32 bit to
      `TBL(0x0e)` at offset 0. Restore the bracket.
2. If `pi_ac+0x0a` = 0: for each core save its current bbmult with `sub_098751`
   into `pi_ac+0x02 + 2c`; set `pi_ac+0x0a` = 1.
3. Set the tone bbmult per core (`sub_09c4e4`, `acphy-txpower.md` section 2):
   * `set_bbmult` != 0 and `amp` != 0: bbmult = 0x40.
   * `amp` = 0 (either `set_bbmult`): bbmult = 0.
   * `set_bbmult` = 0 and `amp` != 0: leave the bbmult unchanged (skip).
4. If `dont_deaf` = 0: `wlc_phy_stay_in_carriersearch_acphy(pi, 1)`.
5. Start the playback:
   * `rfseq` = 1 (RF-sequencer tone): `phy_reg_or(PHY(0x471), 1)`; then
     `phy_reg_or(PHY(0x471), b)` with b = 6 (80 MHz), 4 (40 MHz) or 2 (20 MHz);
     `wlc_phy_force_rfseq_acphy(pi, 0)` (RX2TX, `acphy-init.md` H6).
   * `rfseq` != 1 (sample player): `phy_reg_and(PHY(0x471), 0xfffe)`;
     `PHY(0x463)` = L-1; `PHY(0x461)` = 0xffff; `PHY(0x462)` = 0x3c;
     save `s400` = `PHY(0x400)`; `phy_reg_or(PHY(0x400), 1)`;
     `phy_reg_and(PHY(0x460), 0xfffb)`; `phy_reg_and(PHY(0x460), 0xfffe)`;
     `phy_reg_and(PHY(0x382), 0x3fff)`; then the start bit: if `dont_deaf` = 0,
     `phy_reg_or(PHY(0x460), 1)`; else `phy_reg_or(PHY(0x382), 0x8000)`. Poll
     `PHY(0x403)` bit 0 until clear, at most ~100 reads 10 us apart (SPINWAIT
     ~1000 us). Write `PHY(0x400)` = `s400`.
6. If `dont_deaf` = 0: `wlc_phy_stay_in_carriersearch_acphy(pi, 0)`.

Notes: `PHY(0x461)`/`PHY(0x462)`/`PHY(0x463)` are the sample-player loop count,
"?", and last-sample index (interpretation: 0xffff = loop forever, 0x3c a
timing constant, L-1 the number of samples minus one). `PHY(0x460)` bit 0 and
`PHY(0x382)` bit 15 are two alternative "start" bits; bit 15 of `PHY(0x382)` is
the "start while deaf" variant. `PHY(0x471)` bit 0 selects the RF-sequencer
tone generator, bits 1..2 its bandwidth.

### 13. wlc_phy_cordic (.text+0x0b2317, name original, wlc_phy_cmn.c)

Math helper, no hardware access. `wlc_phy_cordic(theta, out)` writes to
`out[0]` = I and `out[1]` = Q a cosine/sine pair for the angle `theta`, scaled
so that a full magnitude is ~2^15 (`out[1]` is seeded with the CORDIC gain
constant 0x9b75). The angle unit is `0x1680000` per full turn, so the tone of
section 12 with `freq` = `sample_rate`/10 (2 MHz at 20 MHz) advances `0x240000`
per sample = one cycle per 10 samples. Also called by `sub_0addfa` and the
other PHYs' tone functions. Not specified further (18-iteration CORDIC over the
constant table at `.rodata+0x2ce950`).

### 14. wlc_phy_stopplayback_acphy (.text+0x09c5c3, name original)

Purpose: stop the sample player / tone and undo the tone bbmult. Inputs: `pi`.
`pi_ac` = `pi+0x138`.

1. v = read `PHY(0x464)`. If bit 0 of v is set: `phy_reg_or(PHY(0x460), 2)`.
   Else if bit 1 of v is set: `phy_reg_and(PHY(0x382), 0x7fff)`.
2. `phy_reg_and(PHY(0x460), 0xfffb)` (clear bit 2).
3. If `pi_ac+0x0a` != 0 (a tone bbmult was forced): for each core restore its
   saved bbmult from `pi_ac+0x02 + 2c` with `sub_09c4e4`; `pi_ac+0x0a` = 0.
4. `wlc_phy_resetcca_acphy(pi)` (`acphy-init.md` H5).

`PHY(0x464)` bit 0 tells the sample player is running (stop it with
`PHY(0x460)` bit 1), bit 1 tells the RF-sequencer tone is running (stop it by
clearing `PHY(0x382)` bit 15) (interpretation).

### 15. sub_0abc76 (.text+0x0abc76, name assigned: wlc_phy_cal_txiqlo_acphy)

Purpose: transmit IQ-imbalance and LO-leakage calibration. It routes the
transmitter output back into the receive path (internal loopback), plays a
tone, drives the PHY's calibration engine through a list of commands (each
command measures and steps one coefficient), reads the resulting compensation
coefficients and programs them. Inputs: `pi`; `sm` (u8: search mode, 0 = full,
1 = start from the previous results); `mphase` (u8: 0 = do the whole command
list now, 1 = do one slice per call); `part` (u8: 0 = main list, 1 = a second
IQ-only pass). Result: 0, or nonzero if the tone could not be built (the run is
then abandoned before the command loop). Only caller: `wlc_phy_cals_acphy`.
`cal` = `pi+0xf58`, `pi_ac` = `pi+0x138`, N = `pi+0x168`, o = c*0x200,
bw3 = 3/4/5 for 20/40/80 MHz (bandwidth field `pi+0x17e` & 0x3800 = 0x1000/
0x1800/0x2000), bwix = 0/1/2 for 20/40/80 MHz.

**A. Set-up.**

1. `wlc_phy_stay_in_carriersearch_acphy(pi, 1)` (make the receiver deaf).
2. Pick the loopback gain bytes and the command lists (radio major revision 0,
   i.e. this card): loopback gain `Ig[bwix]` = {0x76, 0x87, 0x98},
   `Qg[bwix]` = {0x79, 0x79, 0x79}; successive-approximation step counts
   `steps` = {0x3d, 0x1e, 0x0f, 0x07, 0x03, 0x01}. (Radio major revision 1 uses
   band-dependent gains and 7 steps {0x3d,0x2d,0x1d,0x0d,0x07,0x03,0x01} - not
   this card.)
3. Save the classifier: `u8140` = `PHY(0x140)`; `wlc_phy_classifier_acphy(pi, 7,
   4)` (`acphy-init.md` H1: classify "waited" only).
4. `sub_09380f(pi)` (section 16: save the radio registers and set the radio
   loopback path).
5. Save `PHY(0x19e)` -> `pi_ac+0x8c`, `PHY(0x40f)` -> `pi_ac+0x8e`. Table access
   bracket (set `PHY(0x19e)` bit 1) held over the rest of the set-up and the
   command loop. `mod(PHY(0x40f), 0x0200, 0)`.
6. **For each core c** (o = c*0x200), save-and-override the transmit front end
   into loopback. First save `PHY(0x73e+o)` and write it 0, then set it up:
   `mod(0x73e+o, 0x0010, 0)`, `(…,0x0020,0)`, `(…,0x0040,0)`, `(…,0x0080,0)`,
   `(…,0x1000,0x1000)`, `(…,0x0400,0x0400)`. Save the fifteen registers
   `PHY(0x725, 0x739, 0x73a, 0x721, 0x729, 0x720, 0x728, 0x724, 0x736, 0x723,
   0x735, 0x737, 0x738, 0x727, 0x73c)` (+o) into the `pi_ac` save area that
   `sub_097562` restores. Then apply, in this exact order, the overrides:

   | mod(reg+o, mask, value) | | |
   |---|---|---|
   | `0x720`,0x0002,0x0002 | `0x728`,0x0002,0 | `0x721`,0x0040,0x0040 |
   | `0x729`,0x0040,0 | `0x721`,0x0080,0x0080 | `0x729`,0x0080,0 |
   | `0x721`,0x0020,0x0020 | `0x729`,0x0020,0 | `0x721`,0x2000,0x2000 |
   | `0x729`,0xe000,0 | `0x721`,0x0800,0x0800 | `0x729`,0x0800,0 |
   | `0x721`,0x0400,0x0400 | `0x729`,0x0400,0 | `0x721`,0x4000,0x4000 |
   | `0x728`,0x3800,0 | `0x721`,0x1000,0x1000 | `0x729`,0x1000,0 |
   | `0x720`,0x0020,0x0020 | `0x728`,0x0020,0x0020 | `0x720`,0x0040,0x0040 |
   | `0x728`,0x0040,0x0040 | `0x720`,0x0010,0x0010 | `0x728`,0x0010,0x0010 |
   | `0x721`,0x0100,0x0100 | `0x729`,0x0100,0x0100 | `0x727`,0x0004,0x0004 |
   | `0x73c`,0x0010,0x0010 | | |

   Then: `PHY(0x724+o)` = 0x03ff; `PHY(0x736+o)` = 0x0152 (part 0) or 0x022a
   (part 1); and the core-select fields (they encode the core number c into the
   loopback source):
   `mod(0x73a+o, 0x0007, c & 7)`, `mod(0x725+o, 0x0020, 0x0020)`,
   `mod(0x739+o, 0x007e, (c>>2)&0x7e)`, `mod(0x725+o, 0x0002, 0x0002)`,
   `mod(0x73a+o, 0x0008, (c>>6)&8)`, `mod(0x725+o, 0x0040, 0x0040)`,
   `mod(0x73a+o, 0x0010, (c>>6)&0x10)`, `mod(0x725+o, 0x0080, 0x0080)`,
   `mod(0x73a+o, 0x0060, (c>>6)&0x60)`, `mod(0x725+o, 0x0100, 0x0100)`,
   `mod(0x723+o, 0x0008, 0x0008)`, `mod(0x723+o, 0x0010, 0x0010)`,
   `mod(0x723+o, 0x0800, 0x0800)`, `mod(0x735+o, 0x0700, bw3<<8)`.
   Bandwidth into `PHY(0x735)`/`PHY(0x738)` (PHY revision 0/1):
   `mod(0x735+o, 0x3800, bw3<<11)`, `mod(0x738+o, 0x0007, bw3)`. Then
   `mod(0x723+o, 0x0001, 0x0001)`, `mod(0x735+o, 0x0001, 0)`,
   `mod(0x723+o, 0x0020, 0x0020)`, `mod(0x735+o, 0x4000, 0)`,
   `mod(0x723+o, 0x0002, 0x0002)`, `mod(0x735+o, 0x001e, 0x0008)`; and
   (PHY revision != 3) `mod(0x727+o, 0x0002, 0x0002)`, `mod(0x73c+o, 0x000e,
   0x0004)`, `mod(0x727+o, 0x0001, 0x0001)`, `mod(0x73c+o, 0x0001, 0x0001)`.

   (For c = 0 and 1 the `(c>>2)`/`(c>>6)` fields are 0; `0x73a` bits 0..2 hold
   c. On this board bwix = 0, so `PHY(0x735)` ends at 0x1b08, `PHY(0x738)` low
   3 bits = 3.)
7. `mod(PHY(0x19e), 0x0040, 0x0040)`, `(…,0x0080,0x0080)`, `(…,0x0100,0x0100)`;
   `sub_092ffa(pi)` (ADC reset pulse, `acphy-txpower.md` section 28); restore
   `PHY(0x19e)` bit 1.
8. `sub_09c66c(pi, cal+0x92, gsave)` (section 19: save the current transmit gain
   into a local `gsave` and apply the pre-cal gain record `cal+0x92`).
9. `PHY(0x382)` = 0x8a09 (arm the calibration engine).

**B. Seed the coefficients** (only while `cal[1]` < 3, i.e. at the first
mphase step or single-shot): the start values come from zero when `sm` = 0, or
from the previous final results `cal+4` when `sm` = 1; the intermediate area
`cal+0x2c` is loaded (5 u16 per core). Then for each core c write the start
coefficients through `sub_09bf99` mode 1: selection 0 (a,b); if `part` = 0 also
selections 1, 2, 3 (d, e, f). (So a fresh run starts every coefficient at 0.)

**C. Choose the command list** for this (`sm`, `part`, PHY revision):

| sm | part | list (command words) | count |
|---|---|---|---|
| 0 | 0 | {0x434, 0x334, 0x084, 0x267, 0x056, 0x234} | 6 |
| 0 | 1 | {0x084, 0x056} | 2 |
| 1 | 0 | {0x423, 0x334, 0x073, 0x267, 0x045, 0x234} | 6 |
| 1 | 1 | {0x073, 0x045} | 2 |

`cnt` = count. The loop index runs from `lo` to `hi` where, for `mphase` = 0
(single-shot), `lo` = 0 and `hi` = N*cnt - 1 (the whole list, core 0's `cnt`
commands then core 1's); for `mphase` = 1, `lo` = `cal[1]*2 - 4` (part 0) or
`cal[1]*2 - 0x1c` (part 1) and `hi` = min(`lo`+1, N*cnt-1) - so two commands per
timer tick.

**D. The tone.** `wlc_phy_tx_tone_acphy(pi, bwkHz/2, 0xfa, 1, 0, 0)` with
bwkHz = 2000/4000/8000 for 20/40/80 MHz (so 1000 kHz at 20 MHz), amplitude
0xfa, `dont_deaf` = 1, sample player. `osl_delay(5)`. Per core:
`mod(PHY(0x73a+o), 0x0100, 0)`, `mod(PHY(0x725+o), 0x0400, 0x0400)`. If the tone
failed, jump to the restore (F) and return nonzero.

**E. The command loop.** For each index i = `lo` .. `hi`:

1. c = i / cnt (core), `cmd` = list[i mod cnt], `type` = (`cmd` >> 8) & 0xf.
2. If `cal[0x65 + c]` = 0: `sub_09c91d(pi, cal[0x9a + 10c])` (section 19: load
   the gain schedules into `TBL(0x0c)`, scaled by the pre-cal bbmult of core c);
   `cal[0x65 + c]` = 1 (once per core).
3. `PHY(0x381)` = (`Qg[bwix]` << 8) | `Ig[bwix]` (loopback gain; 0x7976 at
   20 MHz).
4. If `type` is 3 or 4: `sub_09bf99(pi, 1, {0}, 1, c)` (reset the d start value).
   If `type` = 4: `sub_09bf99(pi, 1, {0}, 2, c)` (reset e).
5. Successive-approximation inner loop over `steps` = {0x3d,0x1e,0x0f,7,3,1}:
   1. `PHY(0x383)` = the step count.
   2. `PHY(0x380)` = `cmd` | 0x8000 | (c << 12) (start the engine; bit 15 =
      go, bits 12.. = core).
   3. Poll `PHY(0x380)`: while (value & 0xc000) != 0, at most ~2000 reads
      10 us apart (SPINWAIT ~20 ms; bits 14/15 = "busy").
   4. Read `RADIO(0x144 | c<<9)` (the converter/overflow status). If bit 2 is
      0: leave the inner loop (converged). Otherwise `mod(PHY(0x73a+o), 0x0100,
      0x0100)` then `mod(PHY(0x73a+o), 0x0100, 0)` (pulse) and repeat with the
      next, smaller step count.
6. Read the result and propagate it, by `type`:

   | type | meaning | read best (sel) | write start (sel) | write in-use (sel) |
   |---|---|---|---|---|
   | 0 | IQ a, b | 4 | 0 | 12 (cal state a,b) |
   | 2 | LO d | 5 | 1 | 13 |
   | 3 | LO e | 6 | 2 | 14 |
   | 4 | LO f | 7 | 3 | 15 |

   i.e. `sub_09bf99(pi, 0, r, best_sel, c)`; `sub_09bf99(pi, 1, r, start_sel, c)`;
   `sub_09bf99(pi, 1, r, state_sel, c)`. (Copying the best value into both the
   start and the running state carries it into the next command.) If PHY
   revision = 1 and (i mod cnt) > 4, the value `r` is also collected into the
   per-core LOFT array used in step F (the e/f results of the last two commands).

**F. Apply the results and restore.**

* `part` = 0, and (single-shot or `cal[1]` = 0xc, i.e. the list is finished):
  for each core copy the calibration-state coefficients to the final and the
  in-use tables via `sub_09bf99`: a,b (state 12 -> final 16, in-use OFDM 8,
  in-use 11b 10), d (13 -> 17, 9, 11), e (14 -> 18), f (15 -> 19). Set
  `cal+0x64` = 1 and `cal+0xba` = chanspec.
* `part` = 1, and (single-shot or `cal[1]` = 0x10): for each core read the IQ
  state (selection 12) and write it with `sub_09bf99` mode 2 (a second copy at
  `cal+0x54`). Set `cal+0x64` = 1, `cal+0xba` = chanspec.
* `wlc_phy_stopplayback_acphy(pi)`; `PHY(0x382)` = 0.
* If PHY revision = 1: `wlc_phy_populate_tx_loft_comp_tbl_acphy(pi, loft)`
  (section 19: fill the per-core LOFT tables with the collected e/f results).
* `sub_09c810(pi, gsave)` (restore the transmit gain); `sub_097562(pi)` (restore
  the PHY registers); `sub_0948c3(pi)` (restore the radio registers); `PHY(0x140)`
  = `u8140` (restore the classifier); `wlc_phy_stay_in_carriersearch_acphy(pi, 0)`.
* Return the tone result (0).

Notes:

* Command-word encoding (interpretation, mine): the high nibble is the
  coefficient kind (0 = tx IQ a/b, 2 = digital LO d, 3/4 = radio LO e/f), the
  low byte the engine's per-command configuration (number of accumulations /
  window). The `steps` array is a binary search: each pass halves the step until
  the radio no longer reports overflow. `PHY(0x381)` is the loopback (RX) gain,
  `PHY(0x383)` the measurement length, `PHY(0x380)` bit 15 the start/busy bit
  and bits 12.. the core, `PHY(0x382)` = 0x8a09 the engine mode.
* In the emulator `RADIO(0x144)` reads 0, so the inner loop stops after the
  first step and `PHY(0x380)` never clears, so each command spends the full
  ~20 ms poll. The **commands written** (steps E.5.2, the `PHY(0x380)` values)
  are the real ones; the coefficients read back (step E.6) are 0.
* The two `sub_0abc76` calls of phase 0 are `(sm, 0, 0)` then `(sm, 0, 1)`: the
  main six-command calibration, then the two-command IQ-only pass stored
  separately.

### 16. sub_09380f (.text+0x09380f, name assigned: wlc_phy_txcal_radio_setup_acphy)

Purpose: save the radio front-end registers and put the radio into the tx-cal
loopback configuration. Input: `pi`. For each core c (b = c<<9), on chip 0x4360
(registers 0x1a/0x1b/0x1c/0x1e/0x1f/0x24; 0xaa06 uses 0x1f/0x20/0x21/0x23/0x24/
0x29):

1. Save `RADIO(0x1a|b)` -> `pi_ac+0x48+2c`, `RADIO(0x1b|b)` -> `pi_ac+0x70+2c`,
   `RADIO(0x1c|b)` -> `pi_ac+0x78+2c`, `RADIO(0x1e|b)` -> `pi_ac+0x50+2c`,
   `RADIO(0x1f|b)` -> `pi_ac+0x68+2c`, `RADIO(0x24|b)` -> `pi_ac+0x80+2c`.
   Radio major revision 0: `RADIO(0x170|b)` -> `pi_ac+0x58+2c` (else
   `RADIO(0x184|b)` -> `pi_ac+0x60+2c`).
2. Band configuration:
   * 5 GHz upper ((chanspec & 0xc000) = 0xc000): `mod(RADIO(0x1a|b), 0x00f0,
     0x00b0)`; `mod(RADIO(0x1f|b), 4, 4)`; major rev 0: `mod(RADIO(0x170|b),
     0x0100, 0x0100)`, `mod(RADIO(0x170|b), 0x4000, 0)`; then `mod(RADIO(0x1e|b),
     4, 0)`.
   * otherwise (2.4 GHz or 5 GHz lower): `mod(RADIO(0x1a|b), 0x00f0, 0x0080)`;
     `mod(RADIO(0x1f|b), 4, 0)`; major rev 0: `mod(RADIO(0x170|b), 0x0100, 0)`,
     `mod(RADIO(0x170|b), 0x4000, 0x4000)`; then `mod(RADIO(0x1e|b), 4, 4)`.
   * `mod(RADIO(0x1a|b), 0x0300, 0)`.
   * (Radio major revision 1 only: `mod(RADIO(0x24|b), 0x30, 0)`,
     `mod(RADIO(0x24|b), 0x0e, 0)` - not this card.)

### 17. sub_0948c3 (.text+0x0948c3, name assigned: wlc_phy_txcal_radio_restore_acphy)

Restore the radio registers saved by `sub_09380f`. For each core c (b = c<<9):
`RADIO(0x1a|b)` = `pi_ac+0x48+2c`, `RADIO(0x1b|b)` = `pi_ac+0x70+2c`,
`RADIO(0x1c|b)` = `pi_ac+0x78+2c`, `RADIO(0x1e|b)` = `pi_ac+0x50+2c`,
`RADIO(0x1f|b)` = `pi_ac+0x68+2c`, `RADIO(0x24|b)` = `pi_ac+0x80+2c`; major
revision 0: `RADIO(0x170|b)` = `pi_ac+0x58+2c` (else `RADIO(0x184|b)` =
`pi_ac+0x60+2c`).

### 18. sub_097562 (.text+0x097562, name assigned: wlc_phy_txcal_phy_restore_acphy)

Restore the per-core PHY registers saved in step A.6 of `sub_0abc76`. For each
core c (o = c*0x200) write, from the `pi_ac` save area, `PHY(0x73e+o)`,
`PHY(0x721+o)`, `PHY(0x729+o)`, `PHY(0x720+o)`, `PHY(0x728+o)`, `PHY(0x724+o)`,
`PHY(0x736+o)`, `PHY(0x723+o)`, `PHY(0x735+o)`, `PHY(0x737+o)`, `PHY(0x738+o)`,
`PHY(0x727+o)`, `PHY(0x73c+o)`, `PHY(0x725+o)`, `PHY(0x739+o)`, `PHY(0x73a+o)`.
Then `PHY(0x19e)` = `pi_ac+0x8c`, `PHY(0x40f)` = `pi_ac+0x8e`. (PHY revision
2/5/6 also pulse `PHY(0x19e)` - not this card.) Finally `wlc_phy_resetcca_acphy(pi)`
(`acphy-init.md` H5).

### 19. sub_09c66c, sub_09c810, sub_09c91d, wlc_phy_populate_tx_loft_comp_tbl_acphy

**sub_09c66c (.text+0x09c66c, name assigned: wlc_phy_txcal_txgain_save_set_acphy)**:
`(pi, new, save)`. Table access bracket around: for each core c, read the
current gain code `TBL(0x07)[0x100+c]`, `[0x103+c]`, `[0x106+c]` (16 bit) into
`save+10c+0/2/4` and the bbmult (`sub_098751`) into `save+10c+8`, then write the
new record `new+10c` to the same table entries and bbmult (`sub_09c4e4`). (The
10-byte record is the "transmit gain setting" of `acphy-txpower.md`.)

**sub_09c810 (.text+0x09c810, name assigned: wlc_phy_txcal_txgain_restore_acphy)**:
`(pi, rec)`. Table access bracket; for each core c write `rec+10c` to
`TBL(0x07)[0x100+c]`, `[0x103+c]`, `[0x106+c]` and the bbmult (`sub_09c4e4`).
The restore counterpart of `sub_09c66c`.

**sub_09c91d (.text+0x09c91d, name assigned: wlc_phy_txcal_gainlut_load_acphy)**:
`(pi, scale)`. Table access bracket; writes the two 18-entry schedules of "Data"
(table A to `TBL(0x0c)[0..0x11]`, table B to `TBL(0x0c)[0x20..0x31]`), each entry
16 bit = `((high * scale / 100) << 8) | low`. `scale` is the pre-cal bbmult of
the core.

**wlc_phy_populate_tx_loft_comp_tbl_acphy (.text+0x09ca6e, name original)**:
`(pi, d[3])`. Fills the per-core LOFT (LO-feedthrough) compensation tables with
the calibrated LO-comp words `d[0..2]` (one per core, the e/f results collected
in step E.6). Only when the radio id `pi+0x16a` != 0x30b (true for radio 2069).
Table access bracket; per core it computes a "high channel" flag (channel
number >= {200, 0x95, 100} for core 0/1/2) and writes 128 entries:

* core 0: `TBL(0x42)[i]` = `d[0]` for all i.
* cores 1, 2: 2.4 GHz: `TBL(0x62)[i]`/`TBL(0x82)[i]` = `d[c]` for all i.
  5 GHz: `d[c]` plus a small per-index delta (from the constant arrays
  hi = {..,0xec,0xf6,0xf6,0xf8,0xf6,0xfa} and lo = {..,0,0xfb,0xf2,0xfc,0xf6,
  0xfb}, indexed by the high-channel flag and whether i is below the threshold
  {0x80,0x80,0x20,0x80,0x1a,0x1c}). The high byte of `d[c]` gets the hi delta,
  the low byte the lo delta.

These tables are the static `acphy_loft_lut_core0/1/2_rev0` sets of
`acphy-init.md` (Data, offset 0x42/0x62/0x82); the calibration overwrites them
with the measured LO-leakage compensation.



### 20. wlc_phy_scanroam_cache_cal_acphy (.text+0x09c28b, name original)

Purpose: save the calibration results to a cache (for a scan/roam) or restore
them. Inputs: `pi`, `save` (u8: non-zero = save, 0 = restore). Callers:
`wlc_phy_cals_acphy` (save = 1, at the end of a run), `sub_0ba54c` (restore,
scan/roam - owner elsewhere).

`suspend_mac`, `wlc_phyreg_enter`, table access bracket around:

* `save` = 0 (restore): if `pi_ac+0x44a` = 0xacdc, `sub_09c161(pi, pi_ac+0x412)`
  (apply the cached records; `acphy-txpower.md` section 20).
* `save` != 0: for each core c (record at `pi_ac+0x412 + 0xe*c`):
  1. `sub_09bf99(pi, 0, tmp, 8, c)` -> store the two u16 (OFDM IQ a, b) at
     record+0 and record+2.
  2. `sub_09bf99(pi, 0, record+4, 9, c)` (OFDM d).
  3. record+6, +7, +8, +9 = read `RADIO(0x2|c<<9)`, `RADIO(0x3|c<<9)`,
     `RADIO(0x4|c<<9)`, `RADIO(0x5|c<<9)` (radio LO comp bytes).
  4. record+0xa = read `PHY(0x6a0 + c*0x200)`; record+0xc = read
     `PHY(0x6a1 + c*0x200)` (receive IQ a, b).

  Then `pi_ac+0x44a` = 0xacdc (cache valid). The record layout is exactly the
  one `sub_09c161` consumes (`acphy-txpower.md` section 20).

Then restore the bracket, `wlc_phyreg_exit`, `wlapi_enable_mac`.

## Verification

Traces made with `re-out\analysis\acphy-cal-tx\run.py` (a harness on top of the
unchanged tools: bring-up as `run_chan.py`, then direct calls), PHY revision 1,
radio revision 4, 2.4 GHz channel 1 (chanspec 0x1001), 20 MHz, two cores.

| Item | Checked how | Result |
|---|---|---|
| Perical/scheduler defaults | after bring-up: `pi+0xf89` = 2 (multi-phase), `pi+0xf8a` = 5 ms, `pi+0xf84` = 0, `sh+0x7c` = 0x78, `sh+0xa4..a7` = 3, `pi_ac+0x33c` = 1 | as in "Data" |
| `wlc_phy_cals_acphy` phase 0 order | `cals-single.txt`: `wlc_phy_rxcore_setstate_acphy`, `wlc_phy_noise_sample_request_crsmincal`, `sub_0925cc` (`SHM(0xb8)` = 0x7148 = 29000), `sub_0b1227`, `sub_0abc76` x2, `sub_0addfa`, `TBL(0x0c)[0x5f]` = 0xacdc, `wlc_phy_scanroam_cache_cal_acphy`, then power control restore, `wlc_phyreg_exit`, `wlapi_enable_mac`, chain restore | as specified; `pi_ac+0x33d` set 1, `cal+0xba` set to 0x1001 |
| `sub_0925cc` per-phase durations | `SHM(0xb8)` written 29000 (phase 0), 0x3c, 0x1130, 0x251c, 300, 0x60e across the mphase traces | as specified |
| Multi-phase progression | `run.py mphase` (set `cal[1]`=1, call repeatedly): phases 1 -> 2 -> ... with the transmit-cal part A commands landing in phases 2..7 (~16.5k accesses each), part B in phases 14..15, RX cal in 0x11 (58.9k), VCO in 0x12, then phase reset to 0; phase 0x13 skipped because `pi+0xf84` = 0; phase 0xc advances by two (skips 0xd) | as specified; phases 8..0xc and 0x10 are setup/teardown only (loop empty), ~4.1k accesses |
| `sub_0abc76` command list | `cals-single.txt` `PHY(0x380)` writes across both calls: 0x8434 0x8334 0x8084 0x8267 0x8056 0x8234 0x9434 0x9334 0x9084 0x9267 0x9056 0x9234 (part 0, core 0 then core 1) then 0x8084 0x8056 0x9084 0x9056 (part 1) | matches the list {0x434,0x334,0x084,0x267,0x056,0x234} and {0x084,0x056} with bit 15 = go and bit 12 = core |
| `sub_0abc76` engine registers | `PHY(0x381)` = 0x7976 (loopback gain, 20 MHz), `PHY(0x383)` = 0x3d (first step count), `PHY(0x382)` = 0x8a09 then 0 at the end; `PHY(0x380)` polled with mask 0xc000 to timeout (model returns 0x8434 forever) | as specified (poll never clears in the model) |
| `sub_0abc76` loopback set-up | `PHY(0x724)` = 0x3ff, `PHY(0x736)` = 0x152 (part 0), `PHY(0x735)` = 0x300 -> 0x1b00 -> 0x1b08, `PHY(0x738)` = 3 for core 0 at 20 MHz | as specified (bw3 = 3) |
| Tone table | `tone-*.txt` and inside `sub_0abc76`: `TBL(0x0e)` transfer of 40 (= 2*20) 32-bit entries at 20 MHz, packed `(Q&0x3ff)<<10 | (I&0x3ff)`, period 10 samples for the 2 MHz idle tone; sample-player start via `PHY(0x460)`/`PHY(0x463)`=L-1/`PHY(0x461)`=0xffff/`PHY(0x462)`=0x3c; RF-seq path via `PHY(0x471)` | as specified |
| `wlc_phy_stopplayback_acphy` | `tone-*.txt`: `PHY(0x464)` read, `PHY(0x460)` bit-2 clear, bbmult restored, `wlc_phy_resetcca_acphy` | as specified |
| `wlc_phy_scanroam_cache_cal_acphy` | end of phase 0: per core reads selections 8/9 (`sub_09bf99`), `RADIO(0x2..5|c<<9)`, `PHY(0x6a0/0x6a1+c*0x200)` into `pi_ac+0x412+0xe*c`; `pi_ac+0x44a` = 0xacdc | as specified |

The measured coefficients are 0 in the model (no signal, no "done" bits), so
the coefficients written back (`TBL(0x0c)`, radio 0x2..5, `PHY(0x6a0/0x6a1)`)
are 0; the register/table addresses, the command words and the order are the
real ones.

## Open questions

* The exact bit meaning of the calibration-engine registers `PHY(0x380)`
  (command/busy), `PHY(0x381)` (loopback gain), `PHY(0x382)` (= 0x8a09 mode),
  `PHY(0x383)` (measurement length) and of the low byte of each command word is
  not resolved beyond the interpretation given (high nibble = coefficient kind).
* The many per-core loopback overrides of `PHY(0x720..0x73e + c*0x200)` are
  transcribed exactly but their individual purposes (which bit connects which
  node of the transmit/loopback path) are unknown.
* `RADIO(0x144 | c<<9)` bit 2 gates the successive-approximation inner loop; it
  reads 0 in the model, so only the first step (0x3d) is exercised. Whether real
  hardware iterates the full step list {0x3d,0x1e,0x0f,7,3,1} and how the "done"
  is signalled (`PHY(0x380)` bits 14/15) is not observable here.
* `pi+0xf86` (per-phase collect mask) is 0 on this board; what setting its bits
  (and thus `pi_ac+0x44c`) changes was not analysed.
* `pi+0xf9c` (temperature threshold) is 0 in the model, so the temperature
  gating of `wlc_phy_cal_perical` (and `wlc_phy_tempsense_acphy`) never fires in
  the emulator; the 900-unit recency limit and `sh+0x7c` interval are read from
  the code.
* The purpose of the 0xacdc marker in `TBL(0x0c)[0x5f]` and of `cal+0x64`
  ("full cal done on this channel") is interpreted, not confirmed.
* `wlc_phy_cal_mode` (fn ptr `pi+0x130`) is null for the AC-PHY; whether any
  board sets it is unknown (no caller in the object).
* The second-copy IQ result of `part` = 1 (stored at `cal+0x54` via `sub_09bf99`
  mode 2) is written but no function of this area reads it back; its consumer
  (probably the 11b/CCK transmit path) is outside this spec.
