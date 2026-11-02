# Questions of the implementer: task `bmac-btc`

Specification: [`../spec/bmac-run.md`](../spec/bmac-run.md), section R3, with
its predicates P_eci, P_seci, P_cap, BTC_HW and "Data", and
[`../spec/bmac-init.md`](../spec/bmac-init.md) A6, A7, A9. Code:
`open/mac/bmac_btc.c` (`bcm4360_bmac_btc_mode_set`, `_btc_wire_set`,
`_btc_wire_get`, `_set_btswitch`, `_update_bt_chanspec`, `_seci_upd`, with
static helpers for the predicates, the auto mode and wire, the flag bits of
step 5 and the 2.4 GHz notifications). In `open/mac/bmac_init.c` the two
static helpers became the exported `bcm4360_bmac_btc_flags_to_mhf`
(`sub_062b79`) and `bcm4360_bmac_btc_gpio_enable` (`sub_06106b`). I renamed
them, dropped `static`, updated the three callers and added "bmac-run.md R3"
to their comments. Their behaviour is unchanged.

## Test results

All in Docker, `HYBRID_OUT=/repo/re-out/hybrid-bmacbtc`,
`AB_OUT=/repo/re-out/open-bmacbtc`:

* `python ab.py build`: no warnings.
* `hybrid.py check`, `check --off mac`, `check --on phy`, `check --on phy
  --off mac`: 41416 accesses of the object, 41416 of the hybrid core, the
  same.
* `hybrid.py srom`: all ten cards the same.
* `phy_soak.py --on=`: 150 steps, 588350 accesses of the object, 588350 of
  the hybrid core, the same.
* `python ab.py all`: all 25 scenarios ok, 0 differences (no scenario of
  this layer).

Not done: a check with deliberately broken copies of `bmac_btc.c` to prove
that the session runs the open functions (`bmac-runtime` did one). Docker
Desktop stopped after the required tests. In the session only the "off" paths
are reached anyway (BTC_HW false). Those are `btc_wire_set(0)` -> 2,
`btc_mode_set(8)` -> 0 with its host-flag copies, `set_btswitch(-1)` -> -23,
and the early returns of `update_bt_chanspec` and `seci_upd`. Everything else
is written from the text.

## 1. B1 step 12 calls `sub_06106b`; R3 says only B10 does

* Specification: bmac-init.md B1 step 12: "If the 32 bit word `+0x10` of the
  coexistence state is not 0: `sub_06106b(wlc_hw)`". bmac-run.md R3:
  "Called by `wlc_bmac_bsinit` (B10) only", and "Corrections" item 4:
  "`sub_06106b` is called by `wlc_bmac_bsinit` (B10), not by B1".
* Test: not visible. `btc.gpio_out` is 0 in every session.
* What I did: nothing, as the task asks (do not change behaviour).
  `bcm4360_bmac_init` still calls it in step 12 when `btc_gpio_out` != 0,
  and `bcm4360_bmac_bsinit` calls it in step 13. Which is right? Is the B1
  step 12 call a different function, or the same one inlined?

## 2. `sub_06106b` in bmac_init.c compared with R3

* R3: "m = `btc.gpio_mask`; if m = 0: return. Else ... chip 0x4313:
  `si_gpiopull(sih, 1, m, 0)`, `si_gpiocontrol(sih, m, m, 0)`".
* The open function also returns when `btc_valid` is 0. R3 has no such test,
  because the block always exists after `wlc_hw_attach`. It writes the chip
  0x4313 `si_gpiopull` only as a comment, although `mac_env.h` now has
  `bcm4360_bmac_env_gpiopull`. Neither changes anything on the 4360 with a
  coexistence state. I left both unchanged (task). If chip-specific
  branches with values are wanted here as in R3's other functions, the
  0x4313 call is one line.
* `sub_062b79`: R3 shows no difference from the open code. One remark: the
  open code ORs the masks from Broadcom's table `btc_ucode_flags`
  (`hw_fw_data`). R3 now gives the six masks (0x0004, 0x0008, 0x0100,
  0x0200, 0x0400, 0x1000 for bits 1..6), so they could become constants and
  the code would no longer need the data. I did not change it.

## 3. `btc_wire_set` step 2: the MAC revision 12 case

* Specification: "Then (MAC rev 12 on Apple board types 0x90/0x8b only) 3."
* What I did: inside the "auto" branch only, after the resolution: if
  `mac_rev` = 12 and `boardvendor` = 0x106b and `boardtype` is 0x90 or 0x8b,
  the wire is 3. Two things are guessed. First, that the board vendor is
  tested; the text says "Apple board types" but does not name the vendor
  test as `sub_062b79` does. Second, that an explicit wire is not affected.
  Not reached at revision 42.

## 4. Small decisions, no answer needed unless wrong

* `set_btswitch`: the GPIO path of chips 0xa9a7/0x4331 (board types 0x10e,
  0xe4, 0x5c6, 0xef, 0x10f) is a comment, as the task asks. Every chip takes
  the AC-PHY path: -23 unless the band's PHY type is 11, -4 when down, else
  the PHY service, then `btswitch` = `sw`.
* The other chip and board branches with values are code, as the task asks.
  These are the auto mode by `aa2g` and chip 0xa886 (board 0xe078)/0xa8dc,
  mode 3 on chip 0x4331 with `boardflags` bit 22, the GPIO values of
  `btc_wire_set` for 0xa8d8/0xa99d/0xa8d9/0x4312/0x4313, and the MAC
  revision <= 12 branch of `btc_mode_set` step 5.
* `btc_mode_set` step 3 runs for every resolved mode other than 0, including
  negative ones, as the text says. A negative mode then takes step 5's
  "any other mode" flags (0x0014) and is stored in `btc_mode` (u32) as its
  32-bit pattern.
* No function tests `btc_valid`. R3 has no such test, and the object would
  use the block unconditionally.
* `update_bt_chanspec`: H (ChipCommon revision >= 35) is computed again from
  `ccrev` after the first 2.4 GHz notification, so no field is kept across a
  service (`mac_env.h`). Unless a service changes `ccrev`, the value is the
  same.
* `seci_upd` tests `cccaps_ext` bit 0 only (not `& 5` as P_seci does), as R3
  writes it. The upper layer's mode is asked only when the four conditions
  hold.
* Results are named by their meaning in R3 (`BTC_E_RANGE` -2, `BTC_E_NO_HW`
  -3, `BTC_E_NOTUP` -4, `BTC_E_UNSUPPORTED` -23), not by guessed Broadcom
  names.
* B1 step 7 in bmac_init.c still calls the service
  `bcm4360_bmac_env_btc_mode_set` (Broadcom's `wlc_bmac_btc_mode_set`). Now
  that the open function exists, it could call `bcm4360_bmac_btc_mode_set`
  directly. I left it, because the task allows no other change there.
