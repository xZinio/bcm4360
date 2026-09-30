# Task `phy-desense`: receive gain control, desense, interference mitigation, carrier-sense calibration

The channel function and the initialisation call a group of leaf functions
that are still empty stubs in `open/phy/phy_todo.c`. The previous tasks
implemented register access, radio, PMU, attach, init, the channel function
and the front end (`open/phy/`). This task implements the next group: the
receive-gain control, the desense applied to it, the adjacent-channel
interference mitigation and the carrier-sense minimum-power calibration.

Specifications:
- [`../spec/acphy-desense.md`](../spec/acphy-desense.md): the main one
  (receive gain tables, desense, the crs-min-power calibration, the periodic
  engines, the high-RSSI bypass).
- [`../spec/acphy-chanspec.md`](../spec/acphy-chanspec.md) annex A4
  (`sub_092efb`, `sub_0909fd` - the interference state of the channel), A5
  (`sub_09af05` + helpers), A6 (`sub_09a121` + helpers), A7 (`sub_09a539` +
  helpers): these describe several of your functions with formulas and exact
  register lists; use them with acphy-desense.md, which is authoritative
  where they differ.
- register access: [`../spec/access.md`](../spec/access.md).

Do NOT change any existing `.c` file except by moving the stubs you implement
out of `open/phy/phy_todo.c`; you may add to `open/include/bcm4360/phy.h`.
Read `open/phy/phy_chanspec.c`, `open/phy/phy_init.c`, `open/phy/phy_rxgain.c`,
`open/phy/phy_todo.c` and `open/include/bcm4360/phy.h` first to see the exact
names and signatures of the stubs you fill and the PHY state you work with.

## What to write

`open/phy/phy_desense.c` (and additions to `phy.h`; move the implemented
stubs out of `phy_todo.c`). Implement, one function per procedure:

* Receive gain control: `sub_09af05` with `sub_099658`, `sub_0998cc` - the
  gain tables of the receiver (stages, entries, codes, limits), built from the
  SROM `rxgains*` values, written to PHY tables and registers (acphy-desense
  section on receive gain; acphy-chanspec A5).
* Desense applied to the gain: `sub_09a121` with `sub_099f29`, `sub_09175a`,
  `sub_091b6e`, `sub_090493` (A6); `sub_09a539` with `sub_08f84d`, `sub_090b77`
  (A7).
* Interference state of the channel: `sub_092efb`, `sub_0909fd` (A4).
* Interference mitigation set-up: `wlc_phy_hwaci_setup_acphy`,
  `wlc_phy_aci_w2nb_setup_acphy`, `wlc_phy_desense_aci_reset_params_acphy`,
  and the periodic engine `wlc_phy_hwaci_engine_acphy` (from the watchdog).
* Carrier-sense minimum-power calibration: `wlc_phy_crs_min_pwr_cal_acphy`,
  `sub_08f41b`, `wlc_phy_noise_sample_request_crsmincal`,
  `wlc_phy_ed_thres_acphy`.

Where the specification gives a formula, compute it; where it names a
Broadcom table, load it with `hw_fw_data()`. Measured values (noise, RSSI,
energy) read 0 in the model, so branches that depend on a measurement are not
exercised - implement them from the specification, and expect the tests to
compare only the paths that run.

## Still stubbed (leave alone)

The transmit-power group (`sub_098949`, `wlc_phy_txpwr_by_index_acphy`,
`wlc_phy_txpwrctrl_enable_acphy`, the idle-TSSI measurement, ...) is still
empty stubs in `phy_todo.c`; the next task implements it. The tests still
leave its accesses out. If a difference's accesses come from one of those,
it is spurious - note it and move on.

## Tests

From `tools/re`: `python ab.py build`, then `python ab.py run phy-chanspec
--show 10` and `python ab.py run phy-init --show 10`. Iterate until both are
0 differences with no build warnings; then `python ab.py all` (the other 18
scenarios must stay at 0). The comparison now includes your functions and the
front-end group, and still excludes transmit power.

Write your questions into `docs/re/questions/phy-desense.md` (save early) and
finish with the report the guide asks for.
