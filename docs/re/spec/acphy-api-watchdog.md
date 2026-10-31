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
