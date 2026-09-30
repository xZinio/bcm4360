# Task `phy-txpower`: transmit power control (the last PHY leaf)

The channel function and the initialisation call a final group of leaf
functions, still empty stubs in `open/phy/phy_todo.c`: the transmit power
control. The register access, radio, PMU, PHY attach, init, channel function,
front end and desense are all implemented and match the object. This task
implements transmit power; when it is done the **whole PHY session** (attach,
up, channel changes, watchdog, down) reproduces the object's accesses with
nothing excluded.

Specifications:
- [`../spec/acphy-txpower.md`](../spec/acphy-txpower.md): the main one, all 29
  procedures (gain tables, closed-loop control set-up, estimated-power tables
  from the PA parameters, target power per rate, idle-TSSI measurement, the
  tx-cal coefficient apply).
- [`../spec/acphy-chanspec.md`](../spec/acphy-chanspec.md) section 5 (where the
  channel function calls `wlc_phy_txpwr_by_index_acphy`,
  `wlc_phy_txpwrctrl_enable_acphy`, `wlc_phy_tssivisible_thresh_acphy`) and
  [`../spec/acphy-init.md`](../spec/acphy-init.md) sections 6, 7 (the idle-TSSI
  measurement `sub_0affa9` and the pre-cal tx gain `sub_0b1227` in the init
  flow).
- register access: [`../spec/access.md`](../spec/access.md).

Do NOT change any existing `.c` file except by moving the stubs you implement
out of `open/phy/phy_todo.c`; you may add to `open/include/bcm4360/phy.h`.
Read `open/phy/phy_chanspec.c`, `open/phy/phy_init.c`, `open/phy/phy_rxgain.c`,
`open/phy/phy_desense.c`, `open/phy/phy_todo.c` and `open/include/bcm4360/phy.h`
first for the stub names/signatures and the PHY state. Note especially: the
channel function currently computes the TSSI-visible threshold value inline
(for the compared `PHY(0x1641)` write); if you implement
`wlc_phy_tssivisible_thresh_acphy` as a real function, reconcile with that
inline use so the write is unchanged and no double-work appears.

## What to write

`open/phy/phy_txpower.c` (and additions to `phy.h`; move the implemented stubs
out of `phy_todo.c`). Implement the procedures of acphy-txpower.md:

* Transmit gain by index: `wlc_phy_txpwr_by_index_acphy`, `sub_09868f`,
  `sub_09c4e4` (bbmult), `sub_098751`, reading `TBL(0x20)` and writing
  `TBL(0x07)`/`TBL(0x0c)`.
* Closed-loop power control set-up: `sub_098949` (the big one - the
  estimated-power tables computed from the PA parameters, fixed-point;
  compute the polynomial exactly as the spec gives it), `sub_08ef5f`,
  `wlc_phy_txpwrctrl_enable_acphy`, `sub_0906ae`, `sub_08f3d4`,
  `wlc_phy_txpwrctrl_set_target_acphy`, `wlc_phy_tssivisible_thresh_acphy`,
  `sub_08f9b4`.
* Target power per rate: `sub_09949f` and `sub_099528` where reached.
* Idle-TSSI measurement: `sub_0affa9`, `sub_0af7f4`, `sub_093f74`,
  `sub_09bbe4`, `sub_09be13`, `sub_093ebe`, `sub_0b1227`.
* The tx-cal coefficient apply on band change: `sub_09c161`, `sub_09bf99`.

Where the spec gives a formula (the PA polynomial, the estimated-power and
detector-offset tables), compute it in fixed point (no floating point, and
mind 64-bit division - `hw.h` has helpers if needed, else avoid variable
64-bit division). Broadcom tables (`acphy_txgain_*`, the estimated-power LUTs)
come from `hw_fw_data()`. Measured TSSI reads 0 in the model, so the
measurement itself produces zeros - follow the code for what is done with a
reading.

## Tests

From `tools/re`: `python ab.py build`, then `python ab.py run phy-init
--show 10` and `python ab.py run phy-chanspec --show 10`. Iterate until both
are 0 differences with no build warnings; then `python ab.py all` - all 20
scenarios must be 0. Nothing is excluded now: a difference is yours to fix.

Write your questions into `docs/re/questions/phy-txpower.md` (save early) and
finish with the report the guide asks for. When this passes, the open PHY is
complete for the exercised session.
