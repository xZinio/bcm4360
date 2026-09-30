# Task `phy-rxgain`: front end control, analog filters, reciprocity, board-dependent tables

The channel function and the initialisation (implemented in the previous
task, `open/phy/phy_chanspec.c` and `open/phy/phy_init.c`) call a set of leaf
functions that are at present empty stubs in `open/phy/phy_todo.c`. This task
implements the first group of them: everything that sets up the front end
(the board's switches, LNAs and PAs), the analog filters, the reciprocity
coefficients and the board-dependent PHY tables. When you are done these are
real functions and the whole channel-set and initialisation paths reproduce
the object's accesses for this group.

Specifications:
- [`../spec/acphy-rxgain.md`](../spec/acphy-rxgain.md): the main one.
- [`../spec/acphy-chanspec.md`](../spec/acphy-chanspec.md) annex A1
  (`sub_0a4adc`, channel-change set-up: resampler/farrow, PA table,
  reciprocity), A2 (`sub_09eaf9`, bandwidth-change set-up), A3 (`sub_09e378`,
  the PHY part of the band change): these describe several of your functions
  with formulas and register lists - use them together with acphy-rxgain.md.
- [`../spec/acphy-radio.md`](../spec/acphy-radio.md) sections 15, 16: the
  radio part of the band change (`sub_09e378` calls `bcm4360_radio_band_change`
  and `bcm4360_radio_tssi_setup`, which are finished in `radio2069.c`).
- register access: [`../spec/access.md`](../spec/access.md).

Do NOT change any existing `.c` file except by moving the stubs you implement
out of `open/phy/phy_todo.c`; you may add to `open/include/bcm4360/phy.h`.
Read `open/phy/phy_chanspec.c`, `open/phy/phy_init.c`, `open/phy/phy_todo.c`
and `open/include/bcm4360/phy.h` first to see the exact names and signatures
of the stubs you are filling and the PHY state you work with (the previous
implementer chose them; keep them, or adjust `phy.h` and the callers if you
must, reporting what you changed).

## What to write

`open/phy/phy_rxgain.c` (and additions to `phy.h`; move the implemented stubs
out of `phy_todo.c`). Implement, one function per procedure:

* The front end control: `sub_0a602f` (the femctrl dispatcher) and its
  helpers, filling `TBL(0x0a)` from the SROM `femctrl` and the switch-control
  maps (acphy-rxgain.md). The board's `femctrl` is 3 in the test; implement
  the values the code distinguishes as the specification gives them.
* `sub_0a6b0f`: the initialisation-time front-end/table set-up that the
  channel function calls at INIT.
* `sub_0a4adc` (channel-change set-up: resampler/farrow tables, PA table by
  `pdgain` and sub-band, control-channel position, 11b filter, reciprocity;
  acphy-chanspec.md A1), `sub_0a4867` its helper.
* `sub_09eaf9` (bandwidth-change set-up; A2). This one matters early: three
  bits it sets (in `PHY(0x140)`, `PHY(0x725)`, `PHY(0x73a)` and their core-1
  mirrors) are read afterwards by the AFE calibration and the classifier, so
  until it is implemented the init and channel-set tests show a few
  differences there; implementing it removes them.
* `sub_09e378` (band change, PHY part; A3): its own PHY/table work, then it
  calls the finished radio functions `bcm4360_radio_band_change` and
  `bcm4360_radio_tssi_setup` (do not reimplement those).
* The analog filters `wlc_phy_set_analog_tx_lpf`, `wlc_phy_set_analog_rx_lpf`,
  `wlc_phy_set_tx_afe_dacbuf_cap`; `wlc_phy_populate_recipcoeffs_acphy`;
  `sub_08f2e9`; and the iovar/override functions the spec lists
  (`wlc_phy_calc_extra_init_gain_acphy`, `wlc_phy_rfctrl_override_rxgain_acphy`,
  `wlc_phy_lpf_hpc_override_acphy`, `wlc_phy_dig_lpf_override_acphy`).

The static tables these functions write are Broadcom data: get each by name
with `hw_fw_data()` where the specification names a table, or write the
constant values the specification gives as data. Where the specification
gives a formula (the farrow/resampler coefficients, reciprocity), compute it.

## Still stubbed (leave alone)

The receive-gain-control/desense group (`sub_09af05`, `sub_09a121`,
`sub_09a539`, ...) and the transmit-power group (`sub_098949`,
`wlc_phy_txpwr_by_index_acphy`, ...) are still empty stubs in `phy_todo.c`;
later tasks implement them. The tests still leave their accesses out. Do not
implement them; if a difference's accesses come from one of them, it is
spurious - note it and move on.

## Tests

From `tools/re`: `python ab.py build`, then `python ab.py run phy-chanspec
--show 10` and `python ab.py run phy-init --show 10`. Iterate until both
report 0 differences and the build has no warnings; then `python ab.py all`
(access, radio-*, pmu-*, phy-attach, phy-attach-state must stay at 0). The
comparison now includes your functions (rxgain is no longer in the stubbed
set) and still excludes desense and transmit power.

Write your questions into `docs/re/questions/phy-rxgain.md` (save early) and
finish with the report the guide asks for.
