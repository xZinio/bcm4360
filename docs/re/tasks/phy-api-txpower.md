# Task `phy-api-txpower`: the transmit-power entry points of the PHY API

Part of completing the PHY API so the open PHY can take over Broadcom's inside
the module (`hybrid/README.md`). This group is the transmit-power functions the
MAC calls on the PHY: the power limits and targets, the per-core offsets, the
estimated output, and the SROM-11 limit decoding.

Specification: [`../spec/acphy-api-txpower.md`](../spec/acphy-api-txpower.md)
(written by the transmit-power analyst). It builds on the existing
[`../spec/acphy-txpower.md`](../spec/acphy-txpower.md) (the closed-loop control,
gain tables, idle-TSSI — already implemented in `open/phy/phy_txpower.c`), so
read that first for what the open code already has. The entry points and their
open names are in the table of [`../spec/acphy-api.md`](../spec/acphy-api.md),
section "Transmit power".

The `ppr_*` functions (the MAC's per-rate power library, `wlc_ppr.c`) are **not**
part of the PHY: the spec lists them as services with `bcm4360_ppr_*` names and
prototypes. Add their declarations to `open/include/bcm4360/phy_env.h` (the
other-layer interface); `hybrid/phy.c` will map them to the object's `ppr_*`
(a follow-up adds that mapping — for the emulator test the harness provides
them, as it does the other services).

Read first: `open/include/bcm4360/phy_api.h` (keep the prototypes exactly),
`open/phy/phy_txpower.c` and `open/include/bcm4360/phy.h` (the `txpwr` state).

## What to write

Add to `open/phy/phy_txpower.c` (and `phy.h` for new state, `phy_env.h` for the
`ppr` services), implementing from acphy-api-txpower.md:
`bcm4360_phy_txpower_limit_set`, `_set`, `_get`, `_sromlimit`,
`_get_target_max`/`_min`, `_hw_ctrl_get`, `_ipa_ison`, `bcm4360_phy_txpwr_percent_set`,
`_txpower_core_offset_set`, `bcm4360_phy_get_est_pout`, `_get_olpc_pwroffset`,
`_tssivisible_thresh`, `_clear_tssi`. The SROM-11 per-rate decoding must follow
the spec exactly (widths, signedness, rounding).

Rules: `docs/re/implementing.md` (clean-room, binding). C11, kernel style, SPDX,
a comment per function naming the spec section. No floating point; the PA
polynomial and power tables are fixed point (see acphy-txpower.md for how the
existing code does it).

## Tests

From `tools/re`: `python ab.py build`, then `python hybrid.py check --on phy`.
The session must stay identical to the object as far as it runs. When this and
the control, rx and watchdog groups are done, `--on phy` is 0 differences for
the whole session. Questions into `docs/re/questions/phy-api-txpower.md`; finish
with the report.
