# Questions of the implementer: task `bmac-chan`

Specification: [`../spec/acphy-chanspec.md`](../spec/acphy-chanspec.md),
sections 1, 2 and the MAC's part of 3, and
[`../spec/bmac-init.md`](../spec/bmac-init.md), section E4 (with B10, already
open). Code: `open/mac/bmac_chan.c` (`bcm4360_bmac_set_chanspec` with the
static band switch `wlc_bmac_setband`, `bcm4360_bmac_setxband`,
`bcm4360_bmac_bw_set`, `bcm4360_bmac_bw_reset`, `bcm4360_bmac_mute`). The
two allowed changes: in `open/mac/bmac_ctrl.c` the static A1 `wlc_mctrl_write`
became the exported `bcm4360_bmac_mctrl_write` (its three callers there
updated), in `open/mac/bmac_init.c` the static B10 `wlc_bmac_bsinit` became
the exported `bcm4360_bmac_bsinit` (its caller, B1 step 37, updated). Nothing
else changed in those files.

## Test results

Final, after question 1 was fixed in the interface (`chanspec` in `mac.h`,
copied by the platform) and step 2 written as `mac->chanspec = chanspec;`:

* `python ab.py build`: no warnings.
* `hybrid.py check`, `check --off mac`, `check --on phy`, `check --on phy
  --off mac`: 41416 accesses of the object, 41416 of the hybrid core, the
  same.
* `hybrid.py srom`: all ten cards the same.
* `python ab.py all` (Docker, `AB_OUT=/repo/re-out/open-bmacchan`): all 25
  scenarios ok, 0 differences (no scenario of this layer).

First run, before that fix: `check` and `check --on phy` were 41416 / 41411,
DIFFERENT (question 1, the only cause); `check --off mac`, `check --on phy
--off mac`, `srom` and `ab.py all` were as above.

To make sure the session really runs the open functions, I mounted broken
copies of `bmac_chan.c` over the file inside the container only (with an
output directory of their own, removed afterwards). In one run: an inverted band flag in
`setxband` (from the attach on, access 1006, the wrapper's bit 13 is
inverted, e.g. `0x0145` for `0x2145`) and bit 8 added to the mask of
`bw_reset` (at the bandwidth changes bit 8 is cleared too, e.g. `0x2095` for
`0x0195`). In two more runs: attribute bit 0 added to the unmute's address
match entry (AMT `0x1fc` = `0x80093ef4` instead of `0x80083ef4`), and no FIFO
suspend on mute together with step 5 of E4 always made (the four suspends,
`D11(0x200/0x240/0x280/0x2c0)`, disappear; 45 extra `D11(0x120)` writes). Each
showed up. So all five functions run as open code, mute and unmute are both
reached, and E4 step 5 is evaluated and correctly makes no write in the
session.

## 1. Section 1 step 2: `wlc_hw+0x11c` = chanspec cannot be written (blocked the test; resolved)

* Specification: acphy-chanspec.md section 1 step 2, "`wlc_hw+0x11c` =
  chanspec". bmac-init.md "Data" ("Further fields") names the same field
  `phy_bw_ctl`: "PHY bandwidth control (0x1001 default; bits 0x3800 select
  20/40/80 MHz in E9)". The task lists no field for it, and `struct
  bcm4360_mac` has none (the block "the channel and the band" has `nbands`,
  `band_unit`, `devid`). `mac_env.h` has no service for it either.
* Test: `check` and `check --on phy` differ only in E9
  (`wlc_bmac_ifsctl_edcrs_set`, Broadcom's, called by `wlc_set_chanspec` after
  `wlc_bmac_set_chanspec`), at the five channel changes to 40 or 80 MHz
  (object accesses 26739, 28464, 29952, 31700, 41102 and the ones after them):
  the object reads and writes `D11(0x6c8)` (values 0x0002 for 40 MHz, 0x000c
  for 80 MHz) and writes `D11(0x6c6)` = 0x0303 where needed. The hybrid core
  takes E9's 20 MHz branch every time (`D11(0x6c6)` = 0x0f0f, `SHM(0x5a)` =
  0x0f0f), because `wlc_hw+0x11c` keeps the old bandwidth. In all, 5 more
  accesses on the object's side (41416 vs 41411), 11 places. Nothing else
  differs.
* What I did: step 2 is a comment in `bcm4360_bmac_set_chanspec`. I could
  not fix it inside the rules: adding a field means editing `mac.h`, and
  `hybrid/mac_chan.c` would have to copy it back to `wlc_hw+0x11c`. Neither
  file is mine to edit.
* Proposed fix (interface): a field `u16 chanspec; /* wlc_hw+0x11c */` in
  the block "the channel and the band" of `mac.h`, which `hybrid/mac_chan.c`
  copies in and back like the others (or a service
  `bcm4360_bmac_env_set_chanspec_field`). Step 2 then becomes
  `mac->chanspec = chanspec;`, one line. I expect `check` and `check --on phy`
  to be the same after that, because no other difference is left.
* Also: the two names should agree. The field holds the whole chanspec
  (default 0x1001 = channel 1, 20 MHz), so "chanspec" seems the better name
  for bmac-init.md "Data". Does B1 or the attach write it too?

## 2. E4 step 4: "On unmute, if it was already 0, return here"

* Specification: "`mute_override` = (`on` != 0). On unmute, if it was already
  0, return here."
* Test: mute and unmute are both reached. In the session the copy always has
  INFRA set and AP clear, so step 5 makes no write either way, and the
  reading below cannot be told apart from "unmute always returns".
* What I did: "it" is `mute_override` before step 4 writes it. An unmute
  when not muted returns; an unmute when muted, and every mute, go on to the
  test of step 5. Correct?

## 3. Section 3: is `wlc_bmac_bw_set` PHY-type dependent before step 1?

* Specification: "`wlc_bmac_bw_set(wlc_hw, bw)` for the AC-PHY: 1. ... 2.
  `wlc_bmac_bw_reset` ... (PHY types other than AC are reset and initialised
  again instead of step 2.)"
* Test: the same (AC-PHY only).
* What I did: steps 1 and 2 with no test of the PHY type; the other PHY types
  are a comment, as the task asks. Does step 1 also depend on the PHY type?

## 4. Section 1 step 3.2 (core not up during a band switch): `clk` and the depth

* Specification: `si_core_reset(sih, 0, 0)`; `wlc_hw+0x164`, `+0x168`,
  `+0x170`, `+0x174` cleared; `wlc_bmac_mctrl(0xffffffff, 0x04000400)`.
* Test: not reached.
* What I did: as written. `clk` is not changed and `wlc_phy_hw_clk_state_upd`
  is not called (A14 step 5 does both around its reset), and
  `mac_suspend_depth` (`+0x16c`) is not cleared. Is that right?

## 5. Small decisions, no answer needed unless wrong

* Section 1 step 6.1: the device id is tested first, so
  `wlc_phy_chanspec_get` is called only for devices outside the four. It makes
  no access, so the test cannot see the order.
* Section 1 step 3.8: when any pending bit is set, all of them are replaced
  by 0x8000 (not ORed), as written. The bit is named by its value
  (`INTSTATUS_AFTER_BANDSWITCH`).
* Section 1 step 3.4 ("PHY types other than AC and N revision >= 3: radio
  off and PHY clock off"), step 3.6 and the MAC revisions below 17 (`D11(0x120)`
  reads in steps 3.4 and section 3) are comments only.
* E4 step 2 for MAC revisions below 40 (`wlc_bmac_set_addrmatch(0, ...)`) is
  a comment; the function returns there without writing the address.
* E4: `on` is `bool` in the interface; `mute_override` becomes 1 or 0.
* `bmac_init.c` still calls the services `bcm4360_bmac_env_setxband` (B1 step
  4) and `bcm4360_bmac_env_mute` (B1 step 35), although both functions are
  now open. I left them alone, as the task requires; a later change could call
  `bcm4360_bmac_setxband` / `bcm4360_bmac_mute` there (like the file-local
  A13 copy noted in `bmac-updown.md`).

## Resolution (coordinator, from the object's code, 2026-11-02)

1. Fixed in the interface: `u16 chanspec; /* wlc_hw+0x11c */` in the block
   "the channel and the band" of `mac.h`, copied in and back by the
   platform; bmac-init.md "Data" now calls the field `chanspec` too. Step 2
   is now `mac->chanspec = chanspec;`, and all the tests are the same (see
   "Test results").
2. Right: an unmute when not muted returns; a mute always goes on to step 5.
3. Step 1 (`wlc_phy_bw_state_set`) is made for every PHY type; only step 2
   depends on it (the AC-PHY gets `wlc_bmac_bw_reset`, the others a PHY reset
   and `wlc_phy_init`). Code and comment are right as written.
4. Right: step 3.2 changes no clock field, calls no PHY function and leaves
   `mac_suspend_depth` alone.

Section 5: all as the object does it.

## 6. Not reached by the session, written from the text only

The core not up during a band switch (section 1 step 3.2), devices other
than 0x43a0 in step 6.1, MAC revisions below 40 in E4 step 2, a non-zero
byte `wlc+0x64` (E4 step 1), and an actual write in E4 step 5.
