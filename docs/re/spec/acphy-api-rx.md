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
  values are not.

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
band/bandwidth and +0x1c/+0x1f for the result; the +0x20/+0x21/+0x24 outputs
and the ring at `pi+0x10b9`/`pi+0x10c9` are written only for N and HT PHYs.

## wlc_phy_rssi_compute (AC-PHY)

`wlc_phy_rssi_compute(pi, rxh)` dispatches by PHY type; for type 11 it calls
`wlc_phy_rssi_compute_acphy(pi, rxh)` and stores the result in `rxh+0x1c`,
clears `rxh+0x1f`. (The early-out for `sh+0x28` > 10 with certain `rxh+0x12`
bits, which marks the RSSI invalid and returns, applies to every type.)

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
| noise read uses `SHM(0x8c)` then `SHM(0x308+4c)/(0x30a+4c)` per core | emulator: `wlc_phy_noise_avg` after `up` shows the read-shm carrier at 0x8c, 0x308, 0x30a (zeros -> every core -92) |
| `ant_rxdiv_set` is a store + return for the AC-PHY | read from the code (the body is guarded by other PHY types / chip ids) |
| `swdiv` uses `D11(0x49c)` on the 4360 | read from the code (the `0xa886` branch is not this chip) |
| chain-throttle schedule and thresholds | read from the code; not exercised by the model (tempsense reads 0) |

## Open questions

- The exact `rxh` field semantics (bit meanings of `rxh+4`, `rxh+0x12`) are by
  use; a real captured frame would confirm them.
- The combination divide `qm_div16` for `rssi.mode` 2: the rounding is taken
  from the function; with one active chain (the usual case) the divide is a
  no-op, so the rounding is not exercised by a single-chain capture.
- `sub_0b701e`'s AC write-back and `sub_0baff6`'s AC classifier set-up are
  described from the code; the model's zero noise means neither is fully
  exercised. A real-card noise capture is needed.
