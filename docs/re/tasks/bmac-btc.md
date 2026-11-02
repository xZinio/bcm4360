# Task `bmac-btc`: Bluetooth coexistence of the MAC core

Specification: [`../spec/bmac-run.md`](../spec/bmac-run.md), section **R3**
(`wlc_bmac_btc_mode_set`, `wlc_bmac_btc_wire_set`, `_wire_get`,
`wlc_bmac_set_btswitch`, `wlc_bmac_update_bt_chanspec`, `wlc_bmac_seci_upd`,
and the two helpers `sub_062b79` / `sub_06106b`), with its predicates P_eci,
P_seci, P_cap, BTC_HW and its "Data" section. For the MAC primitives you
call: `../spec/bmac-init.md` A6, A7, A9.

Interface: `open/include/bcm4360/mac.h` (given: the prototypes under
"Bluetooth coexistence" and the fields `btc_wire`, `btc_aa2g`,
`machwcap_backup`, `btswitch`) and `open/include/bcm4360/mac_env.h` (the
services, the block "Bluetooth coexistence"). Write
**`open/mac/bmac_btc.c`**.

## Why

The ninth MAC increment. The coexistence functions run at attach, at every
up (`wlc_bmac_init`, `wlc_up`) and at every channel change (the PHY tells the
Bluetooth side the channel through `wlc_bmac_update_bt_chanspec`). With this
task they become open code inside the real driver (hybrid build, switch
`open_mac`, adapter `hybrid/mac_btc.c`, already written).

## The functions

| Open function | Broadcom's | Section |
|---|---|---|
| `bcm4360_bmac_btc_mode_set` | `wlc_bmac_btc_mode_set` | R3 |
| `bcm4360_bmac_btc_wire_set`, `_wire_get` | `wlc_bmac_btc_wire_set`, `_wire_get` | R3 |
| `bcm4360_bmac_set_btswitch` | `wlc_bmac_set_btswitch` | R3 |
| `bcm4360_bmac_update_bt_chanspec` | `wlc_bmac_update_bt_chanspec` | R3 |
| `bcm4360_bmac_seci_upd` | `wlc_bmac_seci_upd` (no result) | R3 |

**The changes allowed in another file:** `sub_062b79` and `sub_06106b` are
the `static` functions `wlc_bmac_btc_flags_to_mhf` and
`wlc_bmac_btc_gpio_enable` of `open/mac/bmac_init.c`. Make them the exported
`bcm4360_bmac_btc_flags_to_mhf` and `bcm4360_bmac_btc_gpio_enable` declared in
`mac.h`: rename, drop `static`, update their callers in that file. Nothing
else there. Check them against R3 (bmac-run.md says bmac-init's E10 text was
correct for both); if R3 shows a difference, write it into your questions
file - do not change their behaviour in this task.

## Rules for this code

* Clean room: `../implementing.md`, binding.
* Outside the 802.11 core's registers: the services of `mac_env.h` -
  `bcm4360_bmac_env_eci_notify_bt` (`si_eci_notify_bt`),
  `bcm4360_bmac_env_seci_upd` (`si_seci_upd`), `bcm4360_bmac_env_wlc_btc_mode`
  (the upper layer's `wlc_btc_mode_get`),
  `bcm4360_bmac_env_phy_set_femctrl_bt_wlan_ovrd` (the PHY). The PHY service
  can change the state: do not keep fields in locals across it.
* The coexistence state (`btc_valid`, `btc_mode`, `btc_wire`, `btc_flags`,
  `btc_gpio_mask`, `btc_gpio_out`, `btc_aa2g`) and `btswitch`, `ucode_loaded`
  are fields; write them as R3 does, the caller copies them back.
  `machwcap`, `machwcap_backup`, `boardflags`, `boardflags2`, `cccaps`,
  `cccaps_ext`, `ccrev`, `chip_id`, `boardtype`, `boardvendor`, `mac_rev`,
  `up`, `band_phytype` are inputs.
* Card: MAC 42, AC-PHY, chip 0x4360, Apple board. The branches for other
  chips and boards that R3 lists with values (the resolution of the auto mode
  and wire by chip id, the GPIO values of `btc_wire_set`) are tested as
  written; the 0xa9a7/0x4331 switch path of `set_btswitch`: a comment.

## Tests

From `tools/re` (Docker; the adapter `hybrid/mac_btc.c` is linked
automatically as soon as `open/mac/bmac_btc.c` exists; use `-e
HYBRID_OUT=/repo/re-out/hybrid-bmacbtc`):

* `python ab.py build` (no warnings).
* `python hybrid.py check` must report **the same**; likewise `check --off
  mac`, `check --on phy`, `check --on phy --off mac`, and `srom`.
* `python phy_soak.py --on=` must report **the same** (588,350 accesses
  before this task).
* `python ab.py all` must still pass (`AB_OUT=/repo/re-out/open-bmacbtc`).

On the emulated card the coexistence hardware is absent (BTC_HW false): the
resolution for BTC_HW true, the other modes and wires, `update_bt_chanspec`'s
notifications and `seci_upd`'s update are not reached by the sessions - write
them from the specification. On the real card they may be (open question Q1
of bmac-run.md), so they matter.

Do not edit `hybrid/` or `tools/`, nor any file of `open/` except your own and
the change to `bmac_init.c` above. Note: another person is editing
`open/phy/*.c` at the same time; with the PHY off their code is compiled but
not run - if a build fails in `open/phy`, wait and retry, do not edit it.
Questions into `docs/re/questions/bmac-btc.md`; finish with a short report.
