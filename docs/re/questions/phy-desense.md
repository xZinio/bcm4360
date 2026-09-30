# Questions from implementing `phy-desense`

Implemented `open/phy/phy_desense.c` from `acphy-desense.md`, `acphy-chanspec.md`
annexes A4-A7 and `acphy-init.md` appendix A. All 20 scenarios reach 0
differences. The points below are what I had to guess, where the specs
disagreed, and what the tests could not exercise.

## Had to modify a finished `.c` file (`phy_rxgain.c`)

The rules say to change only `phy_todo.c`. I had to make **one** further change,
to `open/phy/phy_rxgain.c`, and I could not reach 0 differences without it:

* `sub_0a4adc` step 9 (annex A1) calls `wlc_phy_crs_min_pwr_cal_acphy(pi, 1)`.
  In the previous round this call's accesses were excluded (desense was
  stubbed), so `phy_rxgain.c` left it as a comment ("a desense leaf, not yet
  implemented; no register access here"). Now that desense is compared, the
  object's `sub_08f41b` writes (PHY(0x321..0x336) low bytes, PHY(0x910..0x913))
  appear in the channel-change trace, at `sub_0a4adc`'s position (between step 8
  and the 11b filter), so no code I own can produce them there.
* I completed that placeholder: cleared the CRS-cal state bytes (A1 step 9) and
  called `bcm4360_phy_crs_min_pwr_cal_acphy(phy, 1)`. This also fixed a second
  difference: without the CRS cal setting the low byte first, `sub_08f84d` in
  `sub_09a539` wrote e.g. 0x3636 instead of 0x3639 on 80 MHz.

If the intent was to keep `phy_rxgain.c` frozen, then either that placeholder
should have carried the call already, or the CRS cal should be in the
exclusion list. As it stands the channel-change trace requires it.

## `sub_09a539` `on != 0` is reached at init, contradicting acphy-desense.md

`acphy-desense.md` (A7 review, Verification) says `on` is 0 in all traces and
the `on != 0` branch is "code reading only" (reached only by forcing a watchdog
engine). But `acphy-init.md` appendix A has `wlc_phy_desense_aci_reset_params_acphy(pi,1,1,1)`
call `sub_09a539(pi, 1)`, and at init `wlc_phy_interference` runs it (mode 0).
So the `on != 0` branch **does** run in the compared `phy-init` scenario, with
an all-zero desense set. I implemented A7 step 3 fully; the test passes, so the
all-zero path is right, but the two specs disagree on whether `on != 0` is ever
reached outside the watchdog. The measurement-dependent parts of step 3 (a
non-zero `od`/`bd`) are still unexercised.

## `wlc_phy_get_time_usec` (A4)

A4 only names the function and says `sub_092efb` makes "two clock reads". I
implemented it as two 32-bit reads, `D11(0x180)` then `D11(0x184)` (TSF timer
low, high). The trace confirms exactly these two accesses; the returned value
is unused in the model (records are LRU-selected on an all-zero time).

## `pi_ac+0x45e` and `pi_ac+0x460` (front-end values, A5 step 3.1)

A5 step 3.1 copies three bytes (external-LNA gain `e`, T/R loss `t`, bypass `b`)
to `pi_ac+0x45e/+0x45f/+0x460`. `phy.h` had only `rxgain_trloss` (0x45f). I
added `rxgain_bypass` (0x460), read by `sub_090493` (A6). I did **not** add a
field for `e` (0x45e): no function of this area reads it (it is consumed only
through the stage-0 gain table `rxgain_gain[c][0]`), so it is written to the
table but not kept separately. If some out-of-scope consumer (RSSI?) reads
`pi_ac+0x45e`, it would need a field.

## Not exercised by any compared scenario (implemented from the spec only)

The model delivers no noise interrupt and the watchdog is not compared, so
these run from the spec text only and are unverified against a trace:

* `wlc_phy_hwaci_engine_acphy` (E1) - the whole engine. The measured registers
  (PHY(0x7af..), PHY(0x523..)) read 0, so both detectors report no interference
  and only the unconditional 0->1 level step and the E[0..1] write to
  PHY(0x554)/PHY(0x555) would fire. The level tables `pi_ac+0x682/+0x6a2` are
  the synthetic SROM's, so even the write value is a guess.
* `wlc_phy_crs_min_pwr_cal_acphy` calibrate path (`restore = 0`, C1 steps 1 and
  2.1) - reached only from a fresh noise measurement. The per-core input
  `pi_ac+0x14+c` was not located and reads 0; I push 1 into the ring (0 + 1).
  Only the `restore = 1` path is exercised (via A1 step 9).
* `wlc_phy_noise_sample_request_crsmincal` (C4) - its helper `sub_0baff6`
  (`wlc_phy_cmn.c`) is out of scope. I reproduced only the documented effect
  (clearing the per-core result words SHM(0x308..0x312)); the `D11(0x124)` poke
  and the exact SHM range are not reproduced (unknown value, never reached).
* `wlc_phy_ed_thres_acphy` (C5) - iovar only. Implemented from the formula;
  the `d = 0` case matches the spec's worked example (0x97a/0x8fa) by hand.

## `pi_ac+0x8a8` in the scan case

The channel function (`phy_chanspec.c`) discards `sub_092efb`'s result, so I set
the current-record pointer (`pi_ac+0x8a8`) inside `sub_092efb` when `set = 1`.
In the scan case the object sets `pi_ac+0x8a8 = 0` and does not call `sub_092efb`;
my pointer then keeps its previous value. This has no effect on the compared
hardware (every record's desense set is all zero, so `sub_0909fd` produces the
same all-zero total either way), but the memory state differs from the object
in that one case.
