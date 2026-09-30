# Task `phy-cal`: PHY calibrations (the last PHY piece)

Everything the PHY does during bring-up and channel changes is implemented and
matches the object. This task adds the calibrations, which run after an
association (and periodically): temperature sense, transmit IQ/LO calibration,
receive IQ calibration, and the calibration control that schedules them. When
this passes, the open PHY is complete for the whole of `wlc_phy_cals_acphy`
and `wlc_phy_tempsense_acphy` as well.

Specifications:
- [`../spec/acphy-cal-tx.md`](../spec/acphy-cal-tx.md): calibration control
  (`wlc_phy_cals_acphy` phase-0 single shot and the multi-phase machine), tone
  generation and sample playback, transmit IQ/LO calibration (`sub_0abc76` +
  helpers).
- [`../spec/acphy-cal-rx.md`](../spec/acphy-cal-rx.md): receive IQ calibration
  (`sub_0addfa` + helpers), IQ power estimation, temperature sense
  (`wlc_phy_tempsense_acphy`), RSSI computation.
- [`../spec/acphy-txpower.md`](../spec/acphy-txpower.md),
  [`../spec/acphy-radio.md`](../spec/acphy-radio.md): functions the cals call
  that are already implemented (bbmult `sub_09c4e4`, idle-TSSI `sub_0affa9`,
  pre-cal gain `sub_0b1227`, the radio VCO calibration) - call the existing
  open functions, do not reimplement.
- register access: [`../spec/access.md`](../spec/access.md).

Do NOT change existing `.c` files except by moving stubs you implement out of
`open/phy/phy_todo.c`; you may add to `open/include/bcm4360/phy.h`. Read all of
`open/phy/*.c` and `open/include/bcm4360/phy.h` first. **Note on tone
generation**: the transmit-power task earlier put a partial `tx_tone`/
`stopplayback` (amplitude-0 only, reconstructed from a trace) somewhere in the
open code; acphy-cal-tx now specifies these fully. Find that code, implement
the full functions per the spec, and reconcile (one real implementation, no
duplicate symbol).

## What to write

`open/phy/phy_cal.c` (and additions to `phy.h`). The public entry points the
test calls:

```c
/* wlc_phy_cals_acphy(pi, 0): the single-shot calibration (acphy-cal-tx §1) */
void bcm4360_phy_cals(struct bcm4360_phy *phy, u32 phase);

/* wlc_phy_tempsense_acphy (acphy-cal-rx §3); result: temperature in degC */
u32 bcm4360_phy_tempsense(struct bcm4360_phy *phy);
```

and one function per procedure of the two specifications: the cal scheduler
and phase machine, the tx tone/playback, the transmit IQ/LO calibration and
its loopback set-up and cal-engine command sequence, the receive IQ
calibration, the IQ power estimation, temperature sense and the gain/throttle
functions it feeds, and RSSI compute. Follow the specifications exactly:
the cal-engine command words, the loopback override register lists, the
poll/step counts, and the fixed-point coefficient math.

Measurements read 0 in the model and the "done" bits are not all set, so the
computed coefficients come out as zeros and some loops run to their bounded
time-out - that is expected. **What the test compares is the sequence of
commands and register writes, which are deterministic**; get those exactly
right. (The radio's own calibrations report done in the model, so the radio
parts finish normally.)

## Tests

From `tools/re`: `python ab.py build`, then `python ab.py run phy-cal
--show 10`. The scenario brings the driver up, then triggers
`wlc_phy_tempsense_acphy` and `wlc_phy_cals_acphy(pi, 0)` and compares your
`bcm4360_phy_tempsense` / `bcm4360_phy_cals` against them, access by access,
with nothing excluded. Iterate until 0 differences with no build warnings.
Then `python ab.py all`: all other scenarios (now 20) must stay at 0.

Write your questions into `docs/re/questions/phy-cal.md` (save early) and
finish with the report the guide asks for. When this passes, the AC-PHY is
completely reimplemented for everything the emulator exercises.
