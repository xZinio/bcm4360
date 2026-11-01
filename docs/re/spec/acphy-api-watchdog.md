# The PHY watchdog of the BCM4360 (wlc_phy_watchdog)

`wlc_phy_watchdog(pi)` is called once a second by the MAC (from
`wlc_watchdog` through `wlc_bmac_watchdog`). It advances the PHY's one-second
time base, keeps the running glitch/noise statistics the interference
mitigation uses, drives the ACI engines, and triggers the periodic
calibration. This document describes it for chip 0x4360 with an AC-PHY
(`pi+0x160` = 11) of revision 0 or 1; the many branches for other PHY types
are noted only where the AC-PHY path forks around them.

Register notation: [access.md](access.md). It is the `wlc_phy_watchdog` of
[acphy-cal-tx.md](acphy-cal-tx.md) section 11 (the calibration trigger) and
[acphy-desense.md](acphy-desense.md) (the ACI/noise engines), seen as a whole.

## Scope

| Function | .text | Size | Role for the 4360 |
|---|---|---|---|
| `wlc_phy_watchdog` | 0x0bb8ff | 3827 | the watchdog; most of its body is other PHY types |

Open function: `void bcm4360_phy_watchdog(struct bcm4360_phy *phy)`
([acphy-api.md](acphy-api.md)).

The functions it calls are specified elsewhere and are already in `open/`
except where noted:

| Called | Specification / open |
|---|---|
| `wlapi_bmac_read_shm` | service `bcm4360_shm_read` (access.md) |
| `wlapi_suspend_mac_and_wait`, `wlapi_enable_mac` | services (phy_env.h) |
| `wlapi_update_bt_chanspec` | service `bcm4360_mac_update_bt_chanspec` |
| `wlapi_bmac_btc_mode_get`, `wlapi_bmac_btc_period_get` | services (below) |
| `wlc_phy_desense_aci_engine_acphy` | acphy-desense (open) |
| `wlc_phy_hwaci_engine_acphy` | acphy-desense E1 (open) |
| `sub_0baff6` (noise-sample request) | [acphy-api-rx.md](acphy-api-rx.md) |
| `sub_0b72a4` (tx-power recalc tick) | [acphy-api-txpower.md](acphy-api-txpower.md) |
| `wlc_phy_cal_perical` | acphy-cal-tx 3-7, acphy-api.md A9 (open) |
| `wlc_phy_hirssi_elnabypass_engine` | acphy-desense (open) |
| `wlc_phy_stop_bt_toggle_acphy` | acphy-radio (open) |

## State used

All of it is in `struct bcm4360_phy` already, except the statistics histories,
which are internal to the watchdog and make **no** hardware access (they are
counters and small circular buffers in `pi`); an implementation keeps them in
whatever shape is convenient as long as the hardware accesses and service calls
come out the same. The fields that gate behaviour:

| Place | `phy.h` field | Meaning |
|---|---|---|
| `sh+0x34` | `sh.now` (watchdog seconds) | incremented first thing each tick |
| `sh+0x74`, `sh+0x7c` | `fast_timer`, `glacial_timer` | periods in seconds |
| `sh+0x80` | `interference.mode` | the ACI/noise mode in use (7 for rev 1, 1 for rev 0) |
| `pi+0x18c` | `watchdog_override` | 0: the watchdog does nothing (returns at once) |
| `pi+0x19c` | `hold` | the hold flags (scan, not-associated, ...) |
| `pi+0xf89` | `cal.mode` | 0 or 3: no periodic calibration |
| `cal+0xc0`, `cal+0xc8` | `cal.state.last_cal_time`, `.throttle` | calibration gating |
| `pi+0x18f` (399) | (add `u8 bt_scan_active`; name by use) | staleness flag, reset after 6 s |
| `pi+0xc44` | (add `u32 txpwr_tick_time`) | last tx-power recalc tick |
| `pi_ac+0x912` | `cal.` area (add `u8 hirssi_enabled`) | hi-RSSI eLNA-bypass engine armed |

The three SHM counters the statistics read (`SHM(0x10e)`, `SHM(0x10c)`,
`SHM(0x158)`) are MAC-maintained glitch/bad-PLCP counters; the watchdog keeps
their deltas since the last tick and short running averages of them. In the
emulator these reads return 0 (the model does not run the MAC), so the deltas
and averages stay 0 and the mode-change decisions they feed are not taken; a
real card returns live counts.

## Procedure (AC-PHY, revisions 0 and 1)

1. `sh+0x34` += 1.
2. If `pi+0x18c` (`watchdog_override`) is 0: return.
3. The register overrides `pi+0x1136`, `pi+0x113a`, `pi+0x1138`: each, if not
   0, is written to a pair of PHY registers (`pi+0x45a`/`0x45c`,
   `pi+0x44c`/`0x448`, `pi+0x446`/`0x44a`). They are 0 unless set by an iovar,
   so normally nothing is written. (These are `pi` shadow values, not card
   reads; the writes, when they happen, are plain PHY writes.)
4. If `pi+0x19c & 0x206` is not 0 (a scan, PLT or similar hold): skip the
   statistics and the ACI engines, go to step 9.
5. **Statistics.** Read `d = SHM(0x10e)`; `delta0 = d - pi+0x48c`;
   `pi+0x48c = d`. Read `SHM(0x10c)` into `pi+0x300` (delta `delta1` from its
   previous value) and `SHM(0x158)` into `pi+0x3f0` (delta `delta2`); combine
   them with the band (`pi+0x17e & 0xc000`) and the stored `pi+0x302` into the
   four per-tick deltas the histories take (the exact combination: for 2.4 GHz
   the second and third counters are folded into the first and fourth; for
   5 GHz they are taken separately — it only matters for the averages, which
   are 0 in the model). Each non-negative delta is pushed into a two-deep
   circular history (`pi+0x492[pi+0x4a4]` and the running sum `pi+0x490`,
   halved into `pi+0x488`; likewise `pi+0x2ea`/`0x2e4`/`0x2fc`,
   `pi+0x3ea`/`0x3f2`/`0x400`, `pi+0x410`/`0x40a`/`0x418`,
   `pi+0x412`/`0x408`/`0x420`). No hardware access.
6. **ACI/noise engines.** If `sh+0x80 & 1`: `wlc_phy_desense_aci_engine_acphy(pi)`.
   If `sh+0x80 & 6`: `wlc_phy_hwaci_engine_acphy(pi)`. (With the default mode 7,
   both run; with mode 1, only the first.) Both are specified in acphy-desense.
7. (The large block of `wlc_phy_acimode_upd_*` / `wlc_phy_noisemode_upd_*` /
   `wlc_phy_aci_*` calls here is for N/HT/LP/LCN PHYs only; the AC-PHY reaches
   none of it — step 6 is its whole ACI path.)
8. — (fall through to step 9.)
9. If `sh+0x34 % sh+0x74 == 0` (every `fast_timer` seconds, 15): inside a
   `wlc_phyreg_enter`/`_exit` bracket, `wlapi_update_bt_chanspec(physhim,
   pi+0x17e, bit1(pi+0x19c), bit2(pi+0x19c))` (service
   `bcm4360_mac_update_bt_chanspec` with the scan and measure flags from the
   hold word).
10. If `pi+0x19c & 0x20e == 0` (not scanning, radio on, associated):
    `sub_0baff6(pi, 1, pi+0x17e & 0xff)` — the noise-sample request
    ([acphy-api-rx.md](acphy-api-rx.md)).
11. If `pi+0x18f` is set and more than 6 s have passed since `pi+0x190` (400):
    clear `pi+0x18f`.
12. Tx-power recalc tick: if (`pi+0xc44` is 0 or at least `fast_timer` seconds
    since it) and `pi+0x19c & 2` is 0 and `sub_0b72a4(pi)` returns non-zero
    ([acphy-api-txpower.md](acphy-api-txpower.md)): `pi+0xc44 = sh+0x34`.
13. If `pi+0x19c & 0x20f == 0`:
    1. If `pi_ac+0x912` is set: `wlc_phy_hirssi_elnabypass_engine(pi)`.
    2. **Periodic calibration.** If not already calibrating in this tick
       (`bVar21` false — set only by the type-2/4 paths, never for AC),
       `pi+0x199` is 0, `pi+0xf89` is neither 3 nor 0, `cal+0xc8` (throttle) is
       0, and at least `glacial_timer` (`sh+0x7c`) seconds have passed since
       `cal+0xc0`: `wlc_phy_cal_perical(pi, 2)` (acphy-cal-tx; reason 2 =
       periodic). This is the watchdog's calibration trigger.
    3. Decrement `cal+0xc8` if non-zero.
    4. The function pointer `pi+0x100` (a high-level periodic callback): null
       for the AC-PHY on this chip, so nothing.
    5. `n = wlapi_bmac_btc_mode_get(physhim)` (service
       `bcm4360_mac_btc_mode`). If non-zero: `wlapi_bmac_btc_period_get` (a new
       service, below) fills a period and a state byte; if the state byte
       differs from `pi+0xfa2`, call the Bluetooth-coexistence callback
       `pi+0xf8` (for the AC-PHY it updates the front-end timing); store the
       period in `pi+0xfa4` and the state in `pi+0xfa2`.
    6. `wlc_phy_stop_bt_toggle_acphy(pi)` (acphy-radio).

Steps 11-13 run only when the radio is on and associated (`pi+0x19c & 0x20f`
= 0); step 4 already gated out the scan/PLT holds.

## New services

| Service (phy_env.h) | Object | Behaviour |
|---|---|---|
| `u32 bcm4360_mac_btc_mode(hw)` | `wlapi_bmac_btc_mode_get` | the Bluetooth-coexistence mode (0 = off); already declared |
| `void bcm4360_mac_btc_period_get(hw, u16 *period, u8 *state)` | `wlapi_bmac_btc_period_get` | the current BT period and a state byte; add this declaration |

## Verification

| Statement | How |
|---|---|
| The three statistics reads are `SHM(0x10e)`, `SHM(0x10c)`, `SHM(0x158)`, in that order | emulator: one AC watchdog tick after `up` records R shm 0x10e, 0x10c, 0x158 through the read-shm carrier |
| The ACI path for the AC-PHY is `desense_aci_engine` then `hwaci_engine`, bracketed by suspend/enable | emulator: the tick shows `wlapi_suspend_mac_and_wait`, then `wlc_phy_hwaci_engine_acphy` accesses (`PHY(0x7af)`, `PHY(0x7ab)`, ... through the table carrier), then enable |
| `sh+0x80` = 7 after `up` on this card (so both engines run) | emulator: read of `sh+0x80` after `up` |
| `pi+0x18c` = 1 after attach (the watchdog is not a no-op) | emulator: read after `up` |
| Step 9/10/12/13 gating by `sh+0x34 % period` and the hold word | read from the code; the emulator exercises the first tick (`sh+0x34` = 1) and the whole-session test (`tools/re/hybrid.py`) runs two ticks |

The whole-session comparison runs two watchdog ticks; with the model's zero
SHM counters the statistics and the mode changes they drive are not exercised,
so the values of a live glitch response still need a real card (as for every
measured quantity, see [05-status.md](05-status.md)).

## Open questions

- The exact fold of the three counter deltas into the four histories (step 5)
  is read from the code; since the model's counters are 0 it is not exercised,
  and the field names (`pi+0x300`, `0x3f0`, `0x302`, the history bases) are by
  position, not by a known meaning. A real-card capture would confirm which
  counter is glitches, which bad-PLCP, etc.
- `pi+0x18f`/`pi+0x190` (step 11) are named by use (a staleness flag reset
  after 6 s); their exact role is not resolved.

## Corrections and answers (analyst, 2026-11-01)

Answers to `docs/re/questions/phy-api-watchdog.md` Q3 and Q5 (and one
correction of step 3 found on the way). Where this section contradicts the
text above, this section wins. Tools: `re-out/analysis/phy-txpower-answers/`
(`wdprobe.py`, `wdstats.py`, logs next to them).

### Q3. The noise-sample request and what "pending" is (read, and run)

`pi+0x18f` is a **bit set of pending noise-sample requests**, one bit per
requester: bit 0 (1) = the watchdog (reason 1), bit 1 (2) = an external
request (`wlc_phy_noise_sample_request_external`, reason 2, from the MAC's
`wlc_lq_noise_sample_request`), bit 2 (4) = the crs-min-power calibration
(`wlc_phy_noise_sample_request_crsmincal`, reason 4; AC-PHY only).
"Pending" is simply `pi+0x18f` ≠ 0, whatever bit. `pi+0x190` (u32) is the
`sh+0x34` second of the request that was accepted; `pi+0x194` (u32) the
channel of a watchdog request. Suggested names: `noise_pending` (bits),
`noise_time`, `noise_chan`; `pi_ac+0x41` = `crsmincal_noise_refused`.

`sub_0baff6(pi, reason, chan)` (proposed name `wlc_phy_noise_sample_request`,
assigned by me), AC-PHY path:

1. `p` = (`pi+0x18f` ≠ 0).
2. `pi_ac+0x41` = 0 (every call, also a refused one).
3. If `reason` = 4: if `p`: `pi_ac+0x41` = 1 and return (no access). Else
   `pi+0x18f` |= 4.
   Other reasons: if `p`: return (no access, `pi_ac+0x41` stays 0). Reason 2:
   `pi+0x18f` |= 2. Reason 1: `pi+0x18f` |= 1, `pi+0x194` = `chan`.
4. `pi+0x190` = `sh+0x34`.
5. If `pi+0xc21` ≠ 0: `wlc_phy_noise_cb(pi, chan, -95)` (which clears the
   bits again, below) and return. (0 in the session.)
6. `pi+0x198` is 0 for the AC-PHY (`wlc_phy_attach` clears it for PHY types
   4, 5, 7, 8, 10, 11), so the immediate-measurement branch (classifier +
   `wlc_phy_rx_iq_est_acphy`, for reasons other than 2 and 4) is never taken.
   Always: `SHM(0x308)`, `0x30a`, `0x30c`, `0x30e`, `0x310`, `0x312` = 0 (six
   `wlapi_bmac_write_shm`, in this order), then `D11(0x124)` |= 0x10 (32-bit
   read, 32-bit write). These are exactly the accesses acphy-desense.md C4
   names; acphy-api-rx.md's "classifier / rx_iq_est set-up" is the
   `pi+0x198` ≠ 0 branch, not taken on this chip.

Who clears the bits: `wlc_phy_noise_cb(pi, chan, value)` - called from the
noise interrupt path (`wlc_phy_noise_sample_intr`, when the microcode has
the result; never in the emulated session) or from step 5 - clears bit 0 (and,
if `chan` = `pi+0x194`, stores `value` in `sh+0x96[sh+0xa0]` and advances
`sh+0xa0` modulo 8), clears bit 1 (and calls `wlapi_noise_cb(physhim, chan,
value)`), clears bit 2 (AC only). Further, the watchdog's step 11 clears the
whole byte when `(u32)(sh+0x34 − pi+0x190)` > 5 (unsigned), and
`wlc_phy_init` sets it to 0.

**What the session does (run, `wdprobe.py` / `wdprobe.txt`, the hybrid
session's steps on the object with every call of `sub_0baff6` and every
write of `pi+0x18f`/`0x190`/`pi_ac+0x41` logged):**

* There is **no** noise request at all before the first watchdog tick - not
  at `up`, not in the nine channel changes. (The calibration's crsmincal
  request is in `wlc_phy_cals_acphy`, which makes no access at all in
  `up-trace.txt` or the four channel traces, i.e. it does not run in this
  session; it runs in the `cal` scenario, where the calibration is triggered
  directly.) So the question's premise "the calibration at
  `up` also requests a sample" does not hold for the object's session.
* Tick 1 (`sh+0x34` = 1): the watchdog's own request (step 10, reason 1,
  channel 165) is accepted: `pi+0x18f` = 1, `pi+0x190` = 1, and the SHM/D11
  accesses are made **by this request**. Then `sub_09737d`'s crsmincal
  request (step 13.4, reason 4) finds it pending: `pi_ac+0x41` = 1, nothing
  else. So the implementer's choice (step 10's request makes the accesses) is
  right.
* Tick 2: both requests refused (`pi+0x18f` still 1; `sh+0x34 − pi+0x190` =
  1, not > 5). The reason-1 call writes only `pi_ac+0x41` = 0, the reason-4
  call `pi_ac+0x41` = 0 then 1.
* Second `up`: `wlc_phy_init` writes `pi+0x18f` = 0 (it was 1). Tick 3
  accepted again (reason 1, channel 42), tick 4 refused, as before.

For the open code: the stand-in in `phy_wd.c` should become the common
request function above, and **`sub_09737d`'s request must go through it with
reason 4** (pending test, `pi_ac+0x41`), not through an unconditional C4
sequence. Likewise the request in `wlc_phy_cals_acphy` (the open
`phy_cal.c` "common 3", which today calls the C4 sequence and adds the
`D11(0x124)` write by hand): with this function it sets bit 2 and
`pi+0x190` when accepted, so a watchdog tick within the next 5 seconds is
refused - that matters in the `cal` scenario, not in the session.

### Q5. The statistics fold of step 5 (read from the disassembly, verified by running)

Replace step 5 by the following. Counters: `c0` = `SHM(0x10e)` (u16),
`c1` = `SHM(0x10c)`, `c2` = `SHM(0x158)`, read in that order.

* `D0` = `c0 − pi+0x48c` (32-bit: the u16 value minus the stored u32);
  `pi+0x48c` = `c0`.
* `D1` = `s16(c1) − s16(pi+0x300)`; `pi+0x300` = `c1`.
* `D2` = `s16(c2) − s16(pi+0x3f0)`; `pi+0x3f0` = `c2`.
* `p` = `s16(pi+0x302)`; `pi+0x302` = 0. On the AC-PHY nothing else ever
  writes `pi+0x302` except `wlc_phy_cal_init` (0), so `p` is always 0 on the
  BCM4360 (it is a correction other PHY types feed in).
* 2.4 GHz (`pi+0x17e & 0xc000` = 0): `X2` = `D0 − D2`, `X4` = `D1 + p`;
  5 GHz (= 0xc000): `X2` = `D0`, `X4` = `D1`. `X5` = `−p`.

There are **six** two-deep histories, each with a 2-entry u16 array, a u32
index, a u16 running sum, a u16 average and a u16 "previous average":

| History | Array | Index | Sum | Average | Previous | Pushed value |
|---|---|---|---|---|---|---|
| H0 | `pi+0x492` | `pi+0x4a4` | `pi+0x490` | `pi+0x488` | `pi+0x48a` | `D0` |
| H1 | `pi+0x2ea` | `pi+0x2fc` | `pi+0x2e8` | `pi+0x2e4` | `pi+0x2e6` | `D1` |
| H2 | `pi+0x3f6` | `pi+0x400` | `pi+0x3f2` | `pi+0x3ea` | `pi+0x3ec` | `X2` |
| H3 | `pi+0x3fa` | `pi+0x404` | `pi+0x3f4` | `pi+0x3e8` | `pi+0x3ee` | `D2` |
| H4 | `pi+0x414` | `pi+0x418` | `pi+0x410` | `pi+0x40a` | `pi+0x40c` | `X4` |
| H5 | `pi+0x41c` | `pi+0x420` | `pi+0x412` | `pi+0x408` | `pi+0x40e` | `X5` |

Push of `v` into a history: `old` = `array[index]`; previous := average;
sum := (sum − old + v) mod 2^16; average := sum >> 1 (logical, 16 bit);
`array[index]` = `v` (low 16 bits); index := index + 1, or 0 if that is 2 or
more. (For the AC-PHY all six are two deep with `>> 1`; the eight-deep `>> 3`
form is for the N-PHY.) Initial values: acphy-cal-tx.md section 8
(`wlc_phy_cal_init`).

Which pushes happen, in this order:

1. H0 ← `D0` if `D0` ≥ 0.
2. H1 ← `D1` if `D1` ≥ 0.
3. H2/H3: 5 GHz: H2 ← `D0` if `D0` ≥ 0; then H3 ← `D2` if `D2` ≥ 0.
   2.4 GHz: if `D0` ≥ 0 and `D2` ≥ 0: H2 ← `D0 − D2` (**even if that is
   negative**), then H3 ← `D2`; if `D0` ≥ 0 and `D2` < 0: neither; if `D0` < 0:
   H3 ← `D2` if `D2` ≥ 0 (H2 not).
4. H4/H5: 5 GHz: H4 ← `D1` if `D1` ≥ 0; then H5 ← `−p` if `−p` ≥ 0.
   2.4 GHz: if `D1` ≥ 0: if `−p` ≥ 0, H4 ← `D1 + p` then H5 ← `−p`, else
   neither; if `D1` < 0: H5 ← `−p` if `−p` ≥ 0 (H4 not).

With `p` = 0 (always on this chip) that simplifies to: H4 ← `D1` when
`D1` ≥ 0, H5 ← 0 every tick.

The E3 inputs of the question are these arrays: `pi+0x3f6` (index `0x400`)
is H2, `pi+0x3fa` (index `0x404`) is H3 (= `D2`, the `SHM(0x158)` delta),
`pi+0x414` (index `0x418`) is H4, `pi+0x41c` (index `0x420`) is H5. The
implementer's mapping (H2 = "history 2" fed by D0 / D0+D1, `0x3fa` = "history
1") was off; use the table.

Verified by running: `wdstats.py` (log `wdstats.txt`) writes chosen values
into the three SHM counters (steps 0, small, large, negative, wrapping) and
random `pi+0x302` values (0, 1, 3, −1, −16) before each tick, calls the
object's `wlc_phy_watchdog`, and compares the six histories (arrays,
indices, sums, averages, previous averages) and the stored counters with a
model of this text: **80 ticks (40 on channel 1, 40 on channel 36), 0
differences.** No hardware access is involved (the three SHM reads aside).

### Correction of step 3 (read)

The three overrides are copied into **`pi` fields**, not written to PHY
registers: if `pi+0x1136` (s16) ≠ 0 it is stored to `pi+0x45a` and
`pi+0x45c`; if `pi+0x113a` ≠ 0, to `pi+0x44c` and `pi+0x448`; if the low 16
bits of `pi+0x1138` ≠ 0, to `pi+0x446` and `pi+0x44a`. No access (this
answers the first point of the question's Q6).
