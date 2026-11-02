# Receive and measurement entry points of the AC-PHY (BCM4360)

What the MAC layer calls on the PHY for received frames and link
measurements: the RSSI of a received frame, the noise average and the
noise-sample path, the interference-RSSI update, deaf mode, and the receive
chain / antenna-diversity setters and getters. Chip 0x4360, AC-PHY
(`pi+0x160` = 11) revisions 0 and 1, radio 2069 revisions 3 and 4; other PHY
types are noted only where the AC path forks around them.

Register notation: [access.md](access.md). The open names are in
[acphy-api.md](acphy-api.md), section "Receive, measurements, chains".

## Scope

| Function | .text | Size | Open function |
|---|---|---|---|
| `wlc_phy_rssi_compute` | 0x0b97cb | 1624 | `bcm4360_phy_rssi_compute` |
| `wlc_phy_rssi_compute_acphy` | 0x096ee4 | 954 | (its AC body) |
| `wlc_phy_upd_gain_wrt_temp_phy` | 0x0b2bb1 | 259 | (helper, below) |
| `wlc_phy_noise_avg` | 0x0b9e23 | 342 | `bcm4360_phy_noise_avg` |
| `wlc_phy_noise_sample_acphy` | 0x092610 | 94 | (helper) |
| `wlc_phy_noise_read_shmem` | 0x0b7ecc | 400 | (helper) |
| `wlc_phy_noise_sample_intr` | 0x0bb5bc | 789 | `bcm4360_phy_noise_sample_intr` |
| `wlc_phy_noise_sample_request_external` | 0x0bb8e8 | 23 | `bcm4360_phy_noise_sample_request_external` |
| `sub_0baff6` | 0x0baff6 | 1478 | (the common noise-sample request, below) |
| `sub_0b701e` | 0x0b701e | 220 | (helper: per-core dBm and the crs-cal input; "Noise, completed" N2; name assigned `wlc_phy_noise_calc`) |
| `wlc_phy_compute_dB` | 0x0b21e1 | 85 | (helper, N1) |
| `wlc_phy_noise_cb` | 0x0b805c | 168 | (helper: completion of a request, N4) |
| `wlc_phy_noise_sample_request_crsmincal` | 0x0bb8d1 | 23 | (N9; also acphy-desense C4) |
| `wlc_phy_interf_rssi_update` | 0x0b816c | 86 | `bcm4360_phy_interf_rssi_update` |
| `wlc_phy_set_deaf` | 0x0b7dab | 289 | `bcm4360_phy_set_deaf` |
| `wlc_phy_stf_chain_set` | 0x0b5677 | 87 | `bcm4360_phy_stf_chain_set` |
| `wlc_phy_stf_chain_active_get` | 0x0b54e0 | 407 | `bcm4360_phy_stf_chain_active_get` |
| `wlc_phy_ant_rxdiv_set` / `_get` | 0x0b81c2 / 0x0b203f | 1518 / 20 | `bcm4360_phy_ant_rxdiv_set` / `_get` |
| `wlc_phy_swdiv_ant_get` / `_set` | 0x0b5edc / 0x0b5f86 | 170 / 229 | `bcm4360_phy_swdiv_ant_get` / `_set` |

The AC leaf functions `wlc_phy_deaf_acphy`, `wlc_phy_rxcore_setstate_acphy`
and `wlc_phy_crs_min_pwr_cal_acphy` are already in `open/` (acphy-radio,
acphy-desense); `wlc_phy_tempsense_acphy` is `bcm4360_phy_tempsense`
(acphy-cal-rx). `sub_0b701e` (the noise-to-SHM write-back and gain adjust) is
large and shared with other PHYs; its AC path is described below.

## The model and these functions

Almost everything here reads a quantity the hardware measures — received
signal power, the noise counters in shared memory, a temperature. The card
model returns 0 for those reads (it is not a radio). So:

* `wlc_phy_rssi_compute` is described by its arithmetic; with a real receive
  header its output is a real RSSI, in the model it is the arithmetic applied
  to zeros. The algorithm (which registers and header fields it reads, the
  corrections, the combination over chains) is what the spec fixes.
* the noise path reads zeros, so each chain reports the floor (-92 dBm) and
  the aggregates are the floor; the sequence of accesses is exercised, the
  values are not. (Since phy-noise: `tools/re/phy_soak.py` injects non-zero
  noise powers, and "Noise, completed" below describes and verifies the path
  with them.)

A real card is needed for the values, as for every measured quantity
([05-status.md](05-status.md)).

## The received-frame header (`rxh`)

`wlc_phy_rssi_compute(pi, rxh)` reads and writes a MAC receive header
(`wlc_d11rxhdr`, the `d11rxhdr` first). The fields it touches, by byte offset
from `rxh`:

| Offset | Size | Use |
|---|---|---|
| +0x04 | 2 | PHY status word 0: band/bandwidth bits (0x800 = ...), bit 0, bits 12/13 |
| +0x06 | 2 | PHY status word 1: low byte = the raw per-antenna RSSI the AC path starts from |
| +0x08 | 2 | PHY status word 2: gain-index/antenna bits (used by LCN only) |
| +0x09, +0x0a | 1, 2 | per-core raw RSSI bytes for cores 1 and 2 (AC) |
| +0x0b | 1 | flags (bit 2) |
| +0x12 | 2 | received-frame type bits (0x100 = ..., 0x20 = ...) |
| +0x1c | 1 (out) | the final RSSI (signed dBm) |
| +0x1f | 1 (out) | "RSSI valid/invalid" marker |
| +0x20, +0x21 | 1, 1 (out) | per-antenna RSSI and its antenna (N/HT history) |
| +0x24 | 1 (out) | the fractional/quarter-dB remainder |

The AC path uses +0x06/+0x09/+0x0a for the per-core raw values, +0x04 for the
band/bandwidth and +0x1c/+0x1f for the result; the +0x20/+0x21 outputs and the
ring at `pi+0x10b9`/`pi+0x10c9` are written only for N and HT PHYs. **+0x24 is
written by every path** (the common end of `wlc_phy_rssi_compute`, after +0x1c):
for the AC-PHY, and in the early-out for every type, it is set to **0**; only
the LCN types store a remainder there. (Corrected by the coordinator from the
code after `tools/re/phy_soak.py` showed the object clearing it; the first
version said N/HT only.)

## wlc_phy_rssi_compute (AC-PHY)

`wlc_phy_rssi_compute(pi, rxh)` dispatches by PHY type; for type 11 it calls
`wlc_phy_rssi_compute_acphy(pi, rxh)` and stores the result in `rxh+0x1c`,
clears `rxh+0x1f`, and sets `rxh+0x24` = 0. (The early-out for `sh+0x28` > 10
with certain `rxh+0x12` bits applies to every type: it sets `rxh+0x1f` = 1,
then the common end stores 0 in `rxh+0x1c` and in `rxh+0x24`.)

`wlc_phy_rssi_compute_acphy(pi, rxh)` returns a signed dBm:

1. Gather the raw per-core RSSI into `rssi[core]` (`u16`, `core` < `pi+0x168`):
   core 0 from `rxh+0x09` (low byte of `rxh+8`? no — byte at +9), core 1 from
   low byte of `rxh+0x0a`, core 2 from its high byte.
2. If PHY revision is 5, 2 or 6 **and** `rxh+6 & 8`: zero the `rssi[]` and take
   a CCK-specific value from `wlc_phy_11b_rssi_WAR(pi, rxh)` into `rssi[0]`.
   (AC revisions 0/1 skip this.)
3. Sign-extend each `rssi[core]` from 8 bits (values > 0x7f become negative).
4. `wlc_phy_upd_gain_wrt_temp_phy(pi, &tempadj)` (below): a temperature gain
   correction. For each non-zero `rssi[core]`, subtract
   `round((pi+0x212[core-ish] * 2 - tempadj) / 4)` (round half toward zero as
   the code does: positive `(x+2)>>2`, negative `-((2-x)>>2)`).
5. For each core, add a per-core, per-sub-band correction read from `pi_ac`:
   `wlc_phy_get_chan_freq_range_acphy(pi, pi+0x17e & 0xff)` gives a sub-band
   index 0..4; the correction is a byte at `pi_ac + 0x3a8 + 2*core` (index 0,
   2.4 GHz, with a +1 byte for the 40 MHz control-side) or
   `pi_ac + 0x3b0 + 12*core + k` (5 GHz sub-bands, `k` by bandwidth), i.e. the
   `rssicorrnorm`/`rssicorrnorm5g` arrays (`phy.h` `rssi.corrnorm_c`,
   `corrnorm5g_c`). Clamp each result to >= -128.
6. Combine the cores into one value using the receive-chain mask `sh+0xa7`
   (`rxchain`) and the mode `sh+0xa8` (`rssi.mode`): mode 1 = min over active
   cores, mode 0 = max, mode 2 = sum. For mode 2 the sum is divided by the
   number of active cores with `qm_div16` (a rounding 16-bit divide: result
   `(2*num + den) / (2*den)` style — follow `qm_div16` exactly; it returns the
   quotient and a remainder byte).
7. Return the combined value (signed).

`wlc_phy_upd_gain_wrt_temp_phy(pi, &out)`: `out` = 0; for the AC-PHY take the
last tempsense reading `t = pi_ac+0x8dc` (signed), clamp to `[0, 0x69]`;
`out = clamp - pi+0x210` unless `pi+0x210` low byte is 0xff (then 0); the rest
of the function (the per-type gain-table adjust) is for N/HT and is not reached
for the AC-PHY beyond computing `out`.

## Noise: avg, sample, request

**Superseded by "Noise, completed" below** (analysis phy-noise, 2026-11-02).
This first version was written for the model's zero noise; several of its
statements are wrong for a real measurement (the conversion "`power >> 9`",
the -92 floor of a core, the missing channel test of the interrupt). It is
kept only for the history of the questions in
`docs/re/questions/phy-api-rx.md`; implement from the new section.

`wlc_phy_noise_avg(pi)` -> signed dBm:

1. `w = wlapi_get_wlc_info(physhim)` then `r = wlc_ol_noise_avg_offload(w)`;
   if `r & 0xff` is non-zero return `r` (an on-chip-ARM offload answered). The
   open code needs one service for this, `s8 bcm4360_mac_ol_noise_avg(hw)`
   (returns 0 when no offload answers, as on this card).
2. For the AC-PHY: `v = wlc_phy_noise_sample_acphy(pi)`; write `v` into all
   eight bytes of the noise window `sh+0x96..0x9d`.
3. Average the non-zero bytes of the noise window `sh+0x96..0x9d`: sum them,
   count the non-zero, return `sum / count` (signed), or the raw sum if the
   count is 0.

`wlc_phy_noise_sample_acphy(pi)` -> one byte: if the clock is on (`sh+0x31`):
read `SHM(0x8c)` (discarded — a handshake), then
`wlc_phy_noise_read_shmem(pi)` fills the current noise-window slot and returns
the max-core value; store it at `sh+0x96 + sh+0xa0` and advance `sh+0xa0`
(mod 8). Returns the byte (0xa4 = -92 if the clock is off).

`wlc_phy_noise_read_shmem(pi)`: for each core `c` < `pi+0x168`, read a 32-bit
noise power from `SHM(0x308 + 4c)` (low) and `SHM(0x30a + 4c)` (high);
`power[c]` = the 32-bit value; if it is 0 the core's dBm is -92, else
`power[c] >> 9`. If the total over cores is non-zero: `sub_0b701e(pi, power,
dbm, 0)` (the shared noise-to-SHM write-back; its AC path writes the per-core
noise into the PHY's noise shared words and adjusts the initial-gain), and for
the AC-PHY, when the staleness flag `pi+0x18f` is 4 or `pi_ac+0x41` is set,
`wlc_phy_crs_min_pwr_cal_acphy(pi, 0)` (acphy-desense). The per-core dBm go
into the history ring at `pi + 0x768*... ` (16-entry, `pi+0xf54` is the index)
and the max is stored in the noise window as above. In the model all the SHM
reads are 0, so every core is -92, the total is 0, and `sub_0b701e` and the
crsmin call are not reached.

`sub_0baff6(pi, request, channel)` is the common noise-sample request used by
the watchdog (request = 1), `wlc_phy_noise_sample_intr`,
`wlc_phy_noise_sample_request_external` and
`wlc_phy_noise_sample_request_crsmincal` (already in `open/` via the
calibration task). For the AC-PHY it sets up a classifier read and asks the
MAC for a noise measurement; the bulk of its body is other PHY types. Its AC
accesses are the classifier/`wlc_phy_rx_iq_est_acphy` set-up; describe them
from the code, and reuse the pieces the crsmincal path already implements.

`wlc_phy_noise_sample_intr(pi)`: the interrupt-time handler; for the AC-PHY it
reads the noise result from shared memory (as `noise_read_shmem`) and completes
the pending request. `wlc_phy_noise_sample_request_external(pi)` is a thin
wrapper that calls `sub_0baff6(pi, ...)` for an externally requested sample.

## Noise, completed

Analysis `phy-noise` (2026-11-02). This section replaces "Noise: avg, sample,
request" above and answers `docs/re/questions/phy-api-rx.md` Q5, Q6, Q7 and
Q8. It describes the noise path as it behaves with a real, non-zero
measurement. The carrier-sense calibration the measurement drives is
`wlc_phy_crs_min_pwr_cal_acphy`, [acphy-desense.md](acphy-desense.md) C1 -
that section was corrected at the same time (its "Corrections (phy-noise)").

Marks: **(O)** = observed in the emulator (object alone, `tools/re/phy_soak.py`'s
`SoakRun` with its injected noise powers, or the harness below
`re-out/analysis/phy-noise/`), **(R)** = read from the code (decompiler and
disassembly) only.

### N0. Overview: the life of one noise sample

1. **Request.** `sub_0baff6(pi, reason, chan)` (N8) is asked for a sample by:
   the PHY watchdog every second while associated (reason 1, acphy-api-watchdog
   step 10); the AC watchdog callback `sub_09737d` (`pi+0x100`) when
   `pi_ac+0x33c` is set (reason 4, through
   `wlc_phy_noise_sample_request_crsmincal`); `wlc_phy_cals_acphy` at the start
   of a calibration (reason 4, after setting `pi_ac+0x33d` = 1); and the MAC's
   `wlc_lq_noise_sample_request` (reason 2, through
   `wlc_phy_noise_sample_request_external`). Only one sample is pending at a
   time (`pi+0x18f` != 0 refuses every new request). (O for reasons 1 and 4,
   R for 2.)
2. **Measurement.** An accepted request clears the six result words
   `SHM(0x308..0x312)` and sets bit 4 of `D11(0x124)` (the MAC command
   register). The microcode measures the noise power of each receive core,
   writes it as a 32-bit value per core to `SHM(0x308 + 4c)` (low half) /
   `SHM(0x30a + 4c)` (high half), writes the channel it measured on into the
   low byte of `SHM(0x8c)`, and raises bit 18 (0x40000) of the MAC interrupt
   status. (The SHM/D11 writes: O. What the microcode writes, and when: R -
   inferred from what the driver reads; the card model does not run the
   microcode.)
3. **Completion.** `wlc_dpc` sees bit 0x40000 and calls
   `wlc_phy_noise_sample_intr(pi)` (N5) with the `pi` of the current band
   (`wlc_hw+0xe8` -> `+0x28`) (R). That handler reads the channel byte
   `SHM(0x8c)`, converts the powers to dBm per core and keeps them
   (`wlc_phy_noise_read_shmem`, N3, which on the way calls the conversion
   `sub_0b701e`, N2, and - for a calibration sample - the carrier-sense
   calibration), then completes the request (`wlc_phy_noise_cb`, N4): the
   sample goes into the noise window if it is the watchdog's and was measured
   on the requested channel, the MAC is told if the request was its own, and
   the pending bit is cleared. (O)
4. **Readers.** `wlc_phy_noise_avg(pi)` (N7), asked by `WLC_GET_PHY_NOISE`
   (N10) and by a receive path of the MAC, does **not** just average: it reads
   the result words again (`wlc_phy_noise_sample_acphy`, N6, -> N3) and returns
   that fresh reading. So each `WLC_GET_PHY_NOISE` is itself a read of
   `SHM(0x8c)` and `SHM(0x308..)`, a write-back into `pi_ac+0x14..` and - when
   the gate of N3 is open - a carrier-sense calibration with register writes.
   (O)

If no interrupt comes, the pending bit is dropped by the watchdog after more
than 5 s (acphy-api-watchdog step 11, `sh+0x34 - pi+0x190 > 5`) (O: in a 20 s
wait the bit is set at one tick and cleared six ticks later).

### N0a. Data

Shared memory and registers (all accesses 16 bit through the MAC's SHM
window unless said):

| Location | Writer | Reader | Meaning |
|---|---|---|---|
| `SHM(0x308 + 4c)`, `SHM(0x30a + 4c)`, c = 0..2 | microcode; cleared by N8 | N3 | noise power of core c, low and high 16 bits of a u32 (unit unknown; linear power) |
| `SHM(0x8c)` | microcode | N5, N6 | low byte: the channel number of the measurement (compared with `pi+0x194`); high byte not used on this path. N6 reads it and ignores it |
| `SHM(0x314)` | microcode (unverified) | N2 | read by N2 on every call; its value is used only by other PHY types (not AC). It is the reason the object reads `SHM(0x314)` after the last core's noise words |
| `D11(0x124)` bit 4 | N8 (32-bit read-modify-write) | microcode | start a noise measurement |
| MAC interrupt status bit 18 (0x40000) | microcode | `wlc_dpc` | noise measurement done -> N5 |

State (open names from `open/include/bcm4360/phy.h` where they exist):

| Field | Size | Open name | Meaning |
|---|---|---|---|
| `pi+0x18f` | u8 | `noise_pending` | bit set of the pending request: 1 watchdog, 2 external (MAC), 4 crs-min calibration (AC only). At most one bit is ever set (a request is refused while any is set) |
| `pi+0x190` | u32 | `noise_time` | `sh+0x34` (seconds since up) when the pending request was accepted |
| `pi+0x194` | u32 | `noise_chan` | channel of the last accepted watchdog (reason 1) request |
| `pi+0x198` | u8 | `phynoise_polling` | 1 = measure at once in the PHY instead of asking the microcode; 0 for the AC-PHY (`wlc_phy_attach` sets 1, then clears it for PHY types 4, 5, 7, 8, 10, 11; the only other writers are in the LP and SSLPN PHY files, not reached on this card) (O: 0 at every request) |
| `pi+0xc21` | u8 | (not kept by the open code) | "PHY not usable" short cut of N8: complete every request at once with -95 dBm. 0 in every session (O) |
| `pi+0xed4 + 0x20*c + 2*i`, i = 0..15 | s16 | `bssinit_a` (c=0), `bssinit_b` (c=1); c=2 at `pi+0xf14` | per-core history of the last 16 noise readings in dBm (sign-extended bytes); set to -92 by `wlc_phy_BSSinit` |
| `pi+0xf54` | u8 | `bssinit_f54` | write index of that history, modulo 16, common to all cores |
| `sh+0x31` | u8 | `clk` | the MAC/PHY clock is on (N6, N10 refuse otherwise) |
| `sh+0x34` | u32 | | seconds counter of the watchdog |
| `sh+0x96 .. 0x9d` | 8 x s8 | `interference.noise_window` | the noise window (dBm per sample) |
| `sh+0xa0` | u32 | `interference.noise_index` | its write index, 0..7 |
| `sh+0xa7` | u8 | | receive chain mask (`phyrxchain`); only the crs calibration looks at it - the noise read uses every core < `pi+0x168` |
| `pi+0x168` | u8 | | number of cores (2 on this board, O) |
| `pi_ac+0x14 + c` | s8 | (add: `crsmincal.noise_in[c]`) | the newest per-core noise level for the crs calibration: **dB - 37** (= dBm + 67), written by N2 (answers acphy-desense's open question "the writer of `pi_ac+0x14+c` was not located") |
| `pi_ac+0x41` | u8 | `crsmincal.noise_refused` | the last reason-4 request was refused (N8); opens the gate of N3 |
| `pi_ac+0x33c` | u8 | | crs calibration enabled (set at attach); gates the reason-4 requests of `sub_09737d` and `wlc_phy_cals_acphy` |
| `pi_ac+0x33d` | u8 | `crsmincal.state[0]` | force a reference update at the next calibration; set by `wlc_phy_cals_acphy` |

### N1. wlc_phy_compute_dB (.text+0x0b21e1, name original)

Purpose: a coarse 10 log10 of each of `n` u32 values. Inputs: `in` (array of
u32), `out` (array of bytes), `n`. No access, no state.

For each i < n with `v = in[i]`:

1. `m` = the index of the highest set bit of `v` **among the bits 1..31**
   (bit 0 alone does not count): `m = floor(log2(v))` for `v` >= 2, and `m` = 0
   for `v` = 0 or 1.
2. `b` = bit `m - 1` of `v` if `m` > 0 (the bit below the highest), else 0.
3. `out[i]` = `3*m + 2*b` (byte).

So the result is about 10 log10(v), in steps of 3 dB with a 2 dB half step;
0 and 1 both give 0. (O: (3, 14) -> (5, 11); (14, 48) -> (11, 17); (48, 192)
-> (17, 23); (192, 0) -> (23, 0).)

### N2. sub_0b701e (.text+0x0b701e, 220 bytes, name assigned: `wlc_phy_noise_calc`)

Purpose: turn the per-core noise powers into per-core dBm, and keep the
per-core level the carrier-sense calibration uses. Inputs: `pi`, `pw` (array
of u32 powers, already shifted, one per core), `dbm` (array of s8, out),
`gain` (s8: an extra gain in dB to take off; **0 from every caller on the
BCM4360**). Callers: N3 (always `gain` = 0), N8's immediate branch (not
reached on the AC-PHY), `sub_0c0f88` (an iovar for other PHY types). (R; O for
the N3 calls.)

Steps:

1. Read `SHM(0x314)` (one 16-bit read; the value is kept only for step 3's
   non-AC branch).
2. `wlc_phy_compute_dB(pw, d, pi+0x168)` (N1) into a local `d[]`, for every
   core < `pi+0x168` - also for cores whose power is 0.
3. For each core c < `pi+0x168`, AC-PHY (`pi+0x160` = 11):
   `dbm[c]` = `d[c] - 104 - gain` (byte arithmetic);
   `pi_ac[0x14 + c]` = `dbm[c] + gain + 67` = `d[c] - 37`.
   (Other PHY types: `d[c] - 99 - gain` for PHY revisions < 3, else
   `d[c] - 33 - low byte of SHM(0x314)`; no `pi_ac` write. Not this card.)

Notes: the constants are -37 and 67 in the code (`-37 - (gain + 67)` =
-104 - gain). (O: powers (3, 14) -> dBm (-99, -93) and `pi_ac+0x14..` =
(-32, -26); (192, 0) -> (-81, -104) and (-14, -37).)

### N3. wlc_phy_noise_read_shmem (.text+0x0b7ecc, 400 bytes, name original)

Purpose: read the microcode's result, convert it, keep it, and (for a
calibration sample) run the carrier-sense calibration. Input: `pi`. Result:
the largest per-core dBm, at least -92 (s8). Callers: N5, N6 (and an N-PHY
function, not this card).

Steps:

1. Locals: `pw[4]` (u32) = 0, `dbm[4]` (s8) = 0, `total` (u32) = 0.
2. For each core c = 0 .. `pi+0x168` - 1: `lo` = `SHM(0x308 + 4c)`, then
   `hi` = `SHM(0x30a + 4c)` (two 16-bit reads, in this order);
   `p` = `hi << 16 | lo` (u32). If `p` = 0: `dbm[c]` = -92 (and `pw[c]` stays
   0). Else `pw[c]` = `p >> 9`. `total` += `p` (32-bit, wraps).
3. If `total` != 0 (at least one core measured something - but see Notes):
   1. (`wlc_phy_noisepwr_nphy` for PHY revisions 16 and > 17 - not this card.)
   2. N2: `sub_0b701e(pi, pw, dbm, 0)`. This **overwrites `dbm[c]` of every
      core**, including a core whose `p` was 0: such a core becomes
      `compute_dB(0) - 104` = **-104**, not -92. Likewise a core with
      `p` < 0x400 (`pw` 0 or 1) is -104.
   3. If the PHY is the AC-PHY and (`pi+0x18f` = 4 exactly **or**
      `pi_ac+0x41` != 0): `wlc_phy_crs_min_pwr_cal_acphy(pi, 0)`
      ([acphy-desense.md](acphy-desense.md) C1, calibrate path).
4. `best` = -92. For each core c < `pi+0x168`, in rising order: history
   `pi[0xed4 + 0x20*c + 2*pi[0xf54]]` = `dbm[c]` (sign-extended to 16 bits);
   `best` = max(`best`, `dbm[c]`) (signed).
5. `pi+0xf54` = (`pi+0xf54` + 1) & 15.
6. Noise window: `sh[0x96 + sh[0xa0]]` = `best`; `sh+0xa0` = (`sh+0xa0` + 1) & 7.
7. Return `best`.

So the per-core conversion is: **`p` = 0 and all cores 0: -92; otherwise
`compute_dB(p >> 9) - 104`.** Because `best` starts at -92, the result is
never below -92 although a core can be -104. Range of a core when `total`
!= 0: -104 .. -36 (`p >> 9` < 2^23, so `compute_dB` is at most 3*22 + 2 =
68). (O, see Verification.)

Notes:
* `total` is the 32-bit sum of the raw (unshifted) powers: two cores whose
  powers add up to 2^32 make `total` 0 and the reading is taken as "nothing
  measured" (all -92, no N2, no calibration). (R; edge case.)
* The gate of step 3.3 is open for a reason-4 sample (`pi+0x18f` = 4) and
  whenever the last reason-4 request was refused (`pi_ac+0x41`), which is the
  normal state while associated: every watchdog tick makes a reason-1 request
  (which sets `pi+0x18f` = 1) and then `sub_09737d`'s reason-4 request, which
  is refused and sets `pi_ac+0x41` = 1. So in practice every reading -
  interrupt and `wlc_phy_noise_avg` alike - runs the calibration, which then
  reprograms the thresholds only if a core's reference moved (acphy-desense
  C1 step 3.1) (O: all nine readings of the `noise 1` / `noise 2` / `noise 3`
  steps of `probe1.py` ran it).

### N4. wlc_phy_noise_cb (.text+0x0b805c, 168 bytes, name original)

Purpose: complete the pending request with a value. Inputs: `pi`, `chan`
(u8), `v` (s8). Callers: N5, N8's short cuts (N-PHY/LCN callers not this
card). If `pi+0x18f` = 0 it does nothing. Otherwise, in this order:

1. If bit 0 (watchdog) is set: if `pi+0x194` = `chan`: `sh[0x96 + sh[0xa0]]`
   = `v` and `sh+0xa0` = 0 if it was 7, else +1. Clear bit 0 (whether or not
   the channel matched).
2. If bit 1 (external) is set: clear it, then
   `wlapi_noise_cb(physhim, chan, v)` (the MAC's `wlc_lq_noise_cb(wlc, chan,
   v)`: open service needed, see N11).
3. If the PHY is AC and bit 2 is set: clear it.

(O: `wlc_phy_noise_cb(ch 0, -92)` with `pi+0x18f` = 1 and `pi+0x194` = 42
cleared bit 0 and stored nothing; R for bits 1, 2.)

### N5. wlc_phy_noise_sample_intr (.text+0x0bb5bc, 789 bytes, name original)

Purpose: the "noise measurement done" interrupt. Input: `pi`. AC-PHY path
(types 4 and 7, N and HT, take it too):

1. `chan` = low byte of `SHM(0x8c)`.
2. `v` = N3 (`wlc_phy_noise_read_shmem(pi)`).
3. N4: `wlc_phy_noise_cb(pi, chan, v)`.

No clock test, no "is a request pending" test (with nothing pending N3 still
reads, converts, writes the window and the history, and may calibrate; N4
then does nothing). The rest of the body (types 0, 2, 5, 6, 8, 10: a- and
g-PHY JSSI conversion, LP-PHY, LCN) is not this card. (O: the access order
in the soak is `SHM(0x8c)`, `SHM(0x308)`, `0x30a`, `0x30c`, `0x30e`,
`SHM(0x314)`, then - when the calibration applies - the MAC suspend, the
sixteen threshold writes of acphy-desense C2, the MAC enable.)

So the object's two extra reads against the open code are explained: `SHM(0x8c)`
is the channel of the measurement (step 1), `SHM(0x314)` is N2's step 1.

### N6. wlc_phy_noise_sample_acphy (.text+0x092610, 94 bytes, name original)

Input: `pi`. Result: s8.

1. If the clock is off (`sh+0x31` = 0): return -92 (0xa4); nothing else.
2. Read `SHM(0x8c)` (value not used).
3. `v` = N3 (which already wrote `v` into the window slot `sh+0xa0` and
   advanced the index).
4. Write `v` again into the (new) slot `sh[0x96 + sh[0xa0]]` and advance
   `sh+0xa0` once more (0 after 7).
5. Return `v`.

(O: window and index after N6 inside N7, see N7.)

### N7. wlc_phy_noise_avg (.text+0x0b9e23, 342 bytes, name original)

Input: `pi`. Result: s32 (a sign-extended s8 in practice).

1. `r = wlc_ol_noise_avg_offload(wlapi_get_wlc_info(physhim))`; if the low
   byte of `r` is not 0, return `r` (an ARM offload's answer; 0 on this card,
   whose offload is not running) (O: the soak's `noise` steps continue to
   step 2).
2. AC-PHY: `v` = N6; write `v` into all eight bytes `sh+0x96 .. 0x9d`.
3. Average: over the eight bytes as s8, `sum` of the non-zero ones and their
   `count`; return `sum / count` (signed, truncating) - or `sum` (= 0) if
   `count` = 0.

Since step 2 fills the window with one value, the result is `v` itself (N3's
`best`, or -92 with the clock off): **`wlc_phy_noise_avg` returns the fresh
reading of the result words, not an average of earlier samples.** The window
ends up all `v`, its index advanced by two (N3 and N6). (O: soak `noise 1`:
the interrupt's reading -92, then `wlc_phy_noise_avg` = -87 (169 as a byte),
then `WLC_GET_PHY_NOISE` = -81; window after the first `noise_avg` all -87,
`sh+0xa0` 1 -> 3.) The earlier text's "-92 when the clock is off" holds; "the
average of the non-zero bytes" holds but is degenerate.

### N8. sub_0baff6 (.text+0x0baff6, 1478 bytes, name assigned: `wlc_phy_noise_sample_request`)

The request; acphy-api-watchdog.md "Corrections and answers" Q3 has it, and
it is right (O again here). Summary for the AC-PHY, `pi`, `reason`, `chan`:

1. `pending` = (`pi+0x18f` != 0). `pi_ac+0x41` = 0 (every call).
2. Reason 4: if `pending`, `pi_ac+0x41` = 1 and return; else `pi+0x18f` |= 4.
   Other reasons: if `pending` return; reason 2: `pi+0x18f` |= 2; reason 1:
   `pi+0x18f` |= 1 and `pi+0x194` = `chan`; any other reason (0, from the
   a-PHY interrupt): no bit.
3. `pi+0x190` = `sh+0x34`.
4. If `pi+0xc21` != 0: N4 with (`chan`, -95) and return.
5. If `pi+0x198` != 0 and `reason` is neither 2 nor 4 (never on the AC-PHY,
   see N0a): measure at once - MAC suspend; `s = wlc_phy_classifier_acphy(pi,
   0, 0)`; `wlc_phy_classifier_acphy(pi, 7, 0)`;
   `wlc_phy_rx_iq_est_acphy(pi, est, 0x400, 0x20, 0, 0)`;
   `wlc_phy_classifier_acphy(pi, 7, s)`; MAC enable; per core
   `pw[c]` = (I² + Q² power words of `est[c]`) >> 10; N2 with `gain` 0; the
   history, `best` and window steps 4-6 of N3; then N4 with (`chan`, `best`)
   and return. (R only.)
6. Otherwise: `SHM(0x308)`, `0x30a`, `0x30c`, `0x30e`, `0x310`, `0x312` = 0
   (in this order, six 16-bit writes, also for two cores), then `D11(0x124)`
   |= 0x10 (32-bit read, 32-bit write). (O)

The `chan` argument is stored only for reason 1 (and passed to N4 in the
short cuts). Answer to questions/phy-api-rx.md Q8: there is no clock test.

### N9. wlc_phy_noise_sample_request_external / _crsmincal (.text+0x0bb8e8 / 0x0bb8d1, 23 bytes each, names original)

`wlc_phy_noise_sample_request_external(pi)` = N8 with reason **2** and `chan`
= low byte of `pi+0x17e`; `wlc_phy_noise_sample_request_crsmincal(pi)` = N8
with reason **4** and the same `chan` (R, from the disassembly; O for reason 4).
Answer to Q6: the open code's choice (reason 2, current channel) is right.

### N10. WLC_GET_PHY_NOISE (ioctl 135) and the other readers

* The glue asks `WLC_GET_PHY_NOISE` from its statistics (`wl_linux.c`, the
  `wlc_get(..., WLC_GET_PHY_NOISE, &phy_noise)` of the per-second counters
  update, and `wl_iw.c`'s wireless statistics). In the MAC's ioctl handler
  (`sub_0411e9`, `wlc.c`) command 0x87 shares a case with 0x1b, 0xd3, 0xd4,
  0x137, 0x138: after the handler's common checks (the device-removed test,
  the 4-byte argument copy) it calls `wlc_phy_ioctl(wlc->band->pi, cmd, len,
  arg)` and returns its result. `wlc_phy_ioctl` (acphy-api.md A10): clock off
  (`sh+0x31` = 0) -> -11 (BCME_NOCLK); else `*arg` = (s32)(s8)
  `wlc_phy_noise_avg(pi)`, result 0. (O: the soak's ioctl returned 0 and
  -81 = `af ff ff ff`.)
* A receive path of the MAC (`sub_03ceb2`, `wlc.c`, called from `wlc_recv`;
  it passes frames to `wl_monitor`) calls `wlc_phy_noise_avg` for each frame
  it handles, on the `pi` of the frame's band. (R; when `wlc_recv` takes that
  path - presumably monitor mode - is not verified.)
* `wlc_lq_noise_sample_request(wlc, reason, chan)` (`wlc_lq.c`): if its own
  pending byte `wlc+0x5f0` is 0, it notes the reason (2 or 4, or for 1: bit 0,
  the channel in `wlc+0x5f4` and -92 in `wlc+0x6c + chan`) and calls N9
  external. Its completion `wlc_lq_noise_cb` stores the value for a matching
  channel in `wlc+0x6c + chan`. That is the upper layer; the PHY only needs
  the service `wlapi_noise_cb` (N4 step 2). (R)

### N11. What the open code must change (open/phy/phy_rx.c, phy_desense.c)

Against `open/phy/phy_rx.c` and `open/phy/phy_desense.c` as of 2026-11-02:

1. `noise_read_shmem`: per-core value is `compute_dB(p >> 9) - 104` through
   N2 (not `p >> 9`); every core is overwritten by N2 when `total` != 0 (a
   zero core becomes -104); N2 reads `SHM(0x314)` after the last core's
   words and writes `pi_ac+0x14 + c` = `d[c] - 37`; the gate is `pi+0x18f == 4`
   (the whole byte, the open code's test is the same) or `pi_ac+0x41`;
   `best` starts at -92 for every core (the open code starts with core 0's
   value, so it can return -104 where the object returns -92); the history
   is written for every core < `pi+0x168` (third core at `pi+0xf14`).
2. `bcm4360_phy_noise_sample_intr`: read `SHM(0x8c)` first; the completion is
   N4 (bit 0 stores the value into the window only for a channel match and
   advances `sh+0xa0` once more; bit 1 calls the MAC; bit 2 cleared), not
   "advance the window and clear the bits". The open code advances the
   window once more after `noise_read_shmem` unconditionally - wrong.
3. `noise_sample_acphy` and `bcm4360_phy_noise_avg` are right except that
   they inherit 1.
4. A service for N4 step 2: `bcm4360_mac_noise_cb(hw, u8 chan, s8 v)` for
   `wlapi_noise_cb` (new).
5. `bcm4360_phy_crs_min_pwr_cal_acphy`: the ring push takes
   `pi_ac[0x14 + c] + 1` (not the constant 1), and the early return of the
   calibrate path is "no reference changed", not "no core processed" - see
   acphy-desense.md C1 corrections.

Also corrected from the first version: `sub_0b701e` does **not** "write the
per-core noise into the PHY's noise shared words" nor "adjust the initial
gain" - on the AC-PHY it makes no hardware access but the one `SHM(0x314)`
read, and its only state write is `pi_ac+0x14..` (R and O: no write access
inside it in any traced call). `sh+0x9e`/`0x9f` are not used by this path.

### N12. Worked example (O, soak step `noise 1`, channel 36/80, sub-band 1)

The interrupt reads `SHM(0x8c)` = 0 (the model; on a card: the channel), core
0 `0x0708`/`0x0000`, core 1 `0x1c3c`/`0x0000`, then `SHM(0x314)`.

| Quantity | core 0 | core 1 |
|---|---|---|
| raw power `p` | 0x708 | 0x1c3c |
| `p >> 9` | 3 | 14 |
| `compute_dB` | 3*1 + 2*1 = 5 | 3*3 + 2*1 = 11 |
| dBm (N2) | -99 | -93 |
| `pi_ac+0x14+c` | -32 | -26 |
| pushed into ring slot 0 (ring was empty) | -31 | -25 |
| average | -31 | -25 |
| reference row 1 + 7 (80 MHz) | -30 + 7 = -23 | -23 |
| \|r - avg\| >= 3? | 8: yes, new reference -31 - 7 = -38 | 2: no |
| index = avg + 0x1e, clamped | -1 -> 0 | 5 |
| 80 MHz table | 45 | 54 |

Result: `best` = max(-92, -99, -93) = -92 (the floor), window slot 0 = -92;
the calibration applies (core 0's reference changed): MAC suspended, low byte
of `PHY(0x324, 0x330, 0x321, 0x32d, 0x32a, 0x336, 0x327, 0x333)` = 45 (0x3639
-> 0x362d), both bytes of `PHY(0x910..0x913)` = 54 - 45 = 9 (0x0909), MAC
enabled; `pi_ac+0x42` = 54, `+0x43` + 1, `+0x3d/+0x3e` = -31/-25. Then
`wlc_phy_noise_cb(pi, 0, -92)`: bit 0 pending but `pi+0x194` = 42 != 0, so
only the bit is cleared. The open PHY of today writes 0x3643 instead (its ring
receives the constant 1, so the index clamps to 14 = 67) and returns 48 from
the following `wlc_phy_noise_avg` (core 1's `p >> 9` of that second reading,
0x6028 >> 9, taken as a dBm; the object gives -87 for it).

### N13. Verification (phy-noise)

Harness below `re-out/analysis/phy-noise/` (the tools of `tools/re`
imported, unchanged); object alone.

| What | How | Result |
|---|---|---|
| Order of the accesses and the gate state of a noise reading | `probe1.py` (the soak's steps `attach .. 36/80, assoc, joincal, wait 5, noise 1, wait 20, noise 2, noise 3` with the soak's injected powers; every noise function hooked) -> `probe1.txt` | as N3-N7: `SHM(0x8c)`, per core `0x308+4c`/`0x30a+4c`, `SHM(0x314)`; the calibration ran at all 9 readings (3 per `noise` step: interrupt, `noise_avg`, the ioctl's `noise_avg`; gate `pi_ac+0x41` = 1, in `noise 2` also `pi+0x18f` = 4) and programmed each time (`sub_08f41b` with 45/9, 49/9, 54/7, 58/-1, 57/-4, 53/-6, 47/-2, 45/6, 51/7); results 169, 164, 169 (-87, -92, -87); `pi+0x18f` 0 -> 1 (tick) -> 0 (> 5 s) -> 4 (reason 4 accepted) |
| Conversion, write-back, history, window, completion, `noise_avg`, and the crs calibration with noise input (C1 restore 0 and 1, C2) | `cmp.py`: the independent model `model.py` (written from this text and acphy-desense C1/C2) against the object on random cases - random pending bits/channels, noise window and index, history index, ring/index/references/force/desense flags, rx chain mask 1..7, 2 or 3 cores (`pi+0x168` poked to 3 for a quarter of the cases), 25 chanspecs (all five sub-bands, 20/40/80 MHz, poked into `pi+0x17e`), random contents of the 24 threshold registers, raw powers 0, < 0x400, powers of two and their neighbours, log-uniform up to 2^32 - 1, a sum wrapping to 2^32, the level -1 that pushes a 0; modes interrupt / `wlc_phy_noise_avg` / `wlc_phy_ioctl(pi, 135, 4, arg)` / C1(0) / C1(1), the clock (`sh+0x31`) off in 15 % of the `noise_avg` and ioctl cases. Compared per case: `pi+0x18f`, window, `sh+0xa0`, `pi+0xf54`, history of every core, `pi_ac+0x14..`, ring, `+0x3c`, references of all sub-bands, `+0x3d..0x3f`, `+0x42/43/44`, `+0x33d/33e`, the final values of the 24 registers, the order of the SHM reads, the number of MAC suspends, the call of `wlc_lq_noise_cb`, the result (for the ioctl: its return code and `*arg`) | **36,300 cases, 0 differences** (seeds 1, 2, 3, 11-14, 21, 22); the C1 exits "applied", "deferred", "no change", "slot 0 empty" each several hundred times, in all modes and for 2 and 3 cores |
| The harness finds errors | the same with the model switched to the first version's statements (`NOISE_MODEL=old_conv`: `power >> 9` as dBm, -92 per zero core; `old_push`: ring push 1; `old_exit`: C1 returns only if no core processed), 1,000 cases each | 883, 571 and 90 cases differ |
| The model on the soak's own sequence | `soakcheck.py [N]`: all 150 steps of `tools/re/phy_soak.py`'s soak (as of 2026-11-02, with `unquiet`, `wait 920`, `noise 6`), its injected powers, every call of `wlc_phy_noise_read_shmem`, `wlc_phy_noise_sample_acphy` and C1(restore 1) checked against the model at its return; the synthetic board and the soak's board variants 1-5, 7, 8 (`--board N`; 7 and 8 have one receive chain, `sh+0xa7` = 1) | per board 6 + 12 + 42 calls; 8 boards, 480 calls, 0 differences |
| The present gap | `phy_soak.py --steps attach,mpc 0,up,roam_off 1,36/80,assoc,joincal,wait 5,noise 1,wait 20,noise 2` in Docker (`HYBRID_OUT=/repo/re-out/hybrid-phynoise`), open PHY of today | differs only in the two `noise` steps (missing `SHM(0x8c)` and `SHM(0x314)` reads, thresholds 0x3643 instead of 0x362d/0x363a, results 48/0 instead of 169/164) - what N11 lists |

### N14. Open questions (phy-noise)

- **What the microcode writes**: the unit of the noise power words, whether
  it writes all three cores' words on a two-chain board, the high byte of
  `SHM(0x8c)` and `SHM(0x314)` (read by N2 for other PHY types only) are not
  known; the card model does not run the microcode. Whether the microcode
  writes the channel into `SHM(0x8c)` before raising the interrupt is
  inferred from the comparison in N4 (the model returns 0, so the watchdog's
  sample never enters the window there). A noise capture on the real card
  (SHM words + the interrupt) would settle it.
- **The once-a-second `WLC_GET_PHY_NOISE` of the glue** (`wl_linux_watchdog`,
  a module watchdog of `wlc_watchdog`) runs, as far as read, *after*
  `wlc_bmac_watchdog` -> `wlc_phy_watchdog` in the same tick (the module loop
  is an indirect call after `wlc_bmac_watchdog` in `wlc_watchdog`,
  `sub_0450f0`; unverified: the emulator does not run the open glue). If so,
  in a tick whose noise request was accepted it reads the words right after
  the request cleared them (unless the microcode is already done): total 0,
  -92, no calibration; in the other ticks it re-reads the last completed
  result and - with `pi_ac+0x41` set - runs the calibration again on it.
  The real timing needs the card.
- The condition under which `wlc_recv` passes a frame to `sub_03ceb2` (and so
  calls `wlc_phy_noise_avg` per frame) was not determined.
- `pi+0xc21` (N8 step 4) is 0 in every session; its writer was not located
  in this analysis.

## wlc_phy_interf_rssi_update

`wlc_phy_interf_rssi_update(pi, chanspec, rssi)`: for the AC-PHY it calls
`wlc_phy_desense_aci_upd_chan_stats_acphy(pi, chanspec, rssi)` (which updates
the ACI channel statistics `sub_092efb`); the N/HT branch at the top
(`pi+0x138+0x28e` and `pi+0x2d0`) is not taken for the AC-PHY. Specified with
the desense code.

## Deaf, chains, antenna diversity

`wlc_phy_set_deaf(pi, on)`: for the AC-PHY, `wlc_phy_deaf_acphy(pi, 1)` — it
always sets deaf **on**; the `on` argument the MAC passes
(`wlc_bmac_set_deaf`) is ignored by the object, so the open function may take
it for signature parity but ignores it. `wlc_phy_deaf_acphy` is in `open/`.

`wlc_phy_stf_chain_set(pi, txchain, rxchain)`: store `rxchain` in `sh+0xa6`,
then for the AC-PHY `wlc_phy_rxcore_setstate_acphy(pi, rxchain)` (in `open/`).

`wlc_phy_stf_chain_active_get(pi)`: for the AC-PHY, if the throttle flag
`pi+0x18c` path is set it calls `wlc_phy_stf_chain_temp_throttle_acphy`, which
suspends the MAC, reads the temperature (`wlc_phy_tempsense_acphy` =
`bcm4360_phy_tempsense`), and lowers or restores the active-chain mask against
the thresholds `pi+0xc2e`/`pi+0xc31` with the schedule `{1,1,2,1,4,1,2,1}`;
otherwise it returns the stored active mask `pi+0xc33`. Describe the schedule
and thresholds from the code; the result is `pi+0xc33`.

`wlc_phy_ant_rxdiv_set(pi, val)`: stores `val` in `sh+0x95`; for the AC-PHY
(`pi+0x160` = 11) it returns immediately with no further effect (the large body
is for the LP/LCN/N/G PHYs and specific chips). `wlc_phy_ant_rxdiv_get(pi,
&val)`: `*val = sh+0x95`; returns 1.

`wlc_phy_swdiv_ant_get(pi, bit, &val)` / `_set(pi, bit, on)`: software
antenna-diversity. For the chips 0xa886/0xa887 they use `PHY(0x44d)` under a
MAC suspend; for every other card (the 4360 included) they use the direct
register `osl_readw/writew(regs + 0x49c)`: get reads bit `bit` of `D11(0x49c)`
into `*val`; set clears or sets it. On the 4360 this is the plain path (no
suspend).

## Verification

| Statement | How |
|---|---|
| RSSI dispatch: AC type calls the AC body and stores at `rxh+0x1c` | read from the code; emulator: call `wlc_phy_rssi_compute` on a zeroed `rxh` after `up`, observe the write to `rxh+0x1c` and no card access beyond the temp read |
| noise read uses `SHM(0x8c)` then `SHM(0x308+4c)/(0x30a+4c)` per core | emulator: `wlc_phy_noise_avg` after `up` shows the read-shm carrier at 0x8c, 0x308, 0x30a (zeros -> every core -92). With non-zero noise: see "Noise, completed" N13 (adds `SHM(0x314)`, the conversion and the calibration) |
| `ant_rxdiv_set` is a store + return for the AC-PHY | read from the code (the body is guarded by other PHY types / chip ids) |
| `swdiv` uses `D11(0x49c)` on the 4360 | read from the code (the `0xa886` branch is not this chip) |
| chain-throttle schedule and thresholds | read from the code; not exercised by the model (tempsense reads 0) |

## Open questions

- The exact `rxh` field semantics (bit meanings of `rxh+4`, `rxh+0x12`) are by
  use; a real captured frame would confirm them.
- The combination divide `qm_div16` for `rssi.mode` 2: the rounding is taken
  from the function; with one active chain (the usual case) the divide is a
  no-op, so the rounding is not exercised by a single-chain capture.
- (Answered by "Noise, completed": `sub_0b701e` is N2 - no write-back to the
  hardware, it computes the per-core dBm and `pi_ac+0x14..`, verified with
  non-zero noise; `sub_0baff6`'s classifier set-up is the `pi+0x198` branch,
  never taken on the AC-PHY, N8. The open questions left are in N14.)
