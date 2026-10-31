# Task `phy-api-watchdog`: the PHY watchdog entry point

Part of completing the PHY API so the open PHY can take over Broadcom's inside
the module (`hybrid/README.md`). `wlc_phy_watchdog` is called once a second by
the MAC; it keeps the running averages, drives the ACI/noise mitigation and the
periodic calibration trigger, and does the per-tick housekeeping.

Specification: [`../spec/acphy-api-watchdog.md`](../spec/acphy-api-watchdog.md)
(written by the watchdog analyst). It references the existing
[`../spec/acphy-desense.md`](../spec/acphy-desense.md) (the hwaci engine, E1),
[`../spec/acphy-cal-tx.md`](../spec/acphy-cal-tx.md) section 11 (the calibration
trigger) and [`../spec/acphy-api-rx.md`](../spec/acphy-api-rx.md) (the
noise-sample request); do those groups first or in parallel, since the watchdog
calls into them.

Read first: `open/include/bcm4360/phy_api.h` (the prototype), `phy.h` (the
watchdog and interference state), `open/phy/phy_desense.c` and
`open/phy/phy_cal.c` (the pieces the watchdog calls that already exist).

## What to write

`bcm4360_phy_watchdog` in `open/phy/phy_rx.c` or a small `open/phy/phy_wd.c`
(coordinate with the rx task so the file set stays tidy), plus any new state
fields the spec names in `phy.h`. Follow the spec's numbered steps exactly: the
time counter, the early returns, the SHM counter reads and the moving-average
histories (exact widths and shifts), the mitigation and calibration calls.

Rules: `docs/re/implementing.md` (clean-room, binding). C11, kernel style, SPDX,
a comment naming the spec section. No floating point.

## Tests

From `tools/re`: `python ab.py build`, then `python hybrid.py check --on phy`.
The session runs two watchdog ticks; both must match the object. Questions into
`docs/re/questions/phy-api-watchdog.md`; finish with the report.
