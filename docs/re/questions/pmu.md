# Questions of the implementer: task `pmu`

Specification: [`../spec/pmu.md`](../spec/pmu.md). Code:
`open/include/bcm4360/pmu.h`, `open/chip/pmu.c`.

Result of the tests (`python ab.py run NAME`): all scenarios of the task
report 0 differences, the results of the functions that return a value are
equal. The build has no warnings.

| Scenario | Stages | Accesses of the object | Accesses of the open code | Differences |
|---|---|---|---|---|
| `pmu-indirect` | 19 | 99 | 99 | 0 |
| `pmu-init` | 4 | 8 | 8 | 0 |
| `pmu-pll-init` | 16 | 76 | 76 | 0 |
| `pmu-res-init` | 4 | 95 | 95 | 0 |
| `pmu-clocks` | 20 | 60 | 60 | 0 |
| `pmu-power` | 20 | 2500 | 2500 | 0 |

The tests did not contradict the specification anywhere. The points below are
what the specification leaves open, what the tests do not reach, and what I
had to decide myself. For each: what the specification says, what the test
shows, what I did.

How I observed the object: with all functions of `pmu.c` replaced by empty
ones (a temporary change, taken back afterwards) the test lists every access
of the object as a difference, and its results. The values quoted below are
from that run. Nothing else was looked at.

## 1. Division by 0 in si_pmu_si_clock and si_pmu_get_bb_vcofreq

* Specification ("si_pmu_si_clock, si_pmu_cpu_clock", "si_pmu_get_bb_vcofreq"
  step 4): "`d = 0` is a division fault", "`n = 0` is a division fault".
* Test: the scenario sets `PMU_PLLCTL[5] = 0x600` (divider 6, result
  160000000) and `PMU_PLLCTL[2]` reads 0xc31 (divider 24). A divider of 0
  does not occur.
* What I did: no division by 0. `bcm4360_pmu_si_clock()` returns 0 when the
  divider field is 0; `bcm4360_pmu_get_bb_vcofreq()` returns 0 when the
  integer divider is 0 (the value it also returns for an overflow). The
  accesses before that are made as specified.
* Question: can the callers live with 0? What do `PMU_PLLCTL[2]` and `[5]`
  hold after a reset of the real chip?

## 2. si_pmu_get_bb_vcofreq: the division before the signed comparison

* Specification ("si_pmu_get_bb_vcofreq", step 4): "If `ref > (0xffffffff -
  part) / n` (signed comparison ...)". It does not say whether the division
  is signed or unsigned.
* Test: `xf` = 40, `PMU_PLLCTL[2]` = 0xc31, `[3]` = 0x100e, result 9600098 in
  all four models. Values for which the kinds of division or of comparison
  differ do not occur.
* What I did: unsigned division of 32 bit, then both sides compared as signed
  values of 32 bit.
* Question: is the division unsigned?

## 3. si_pmu_res_deps: the call of itself

* Specification ("sub_0118f2 = si_pmu_res_deps"): "If `all` and `deps != 0`:
  `deps |= si_pmu_res_deps(.., deps, 1)`".
* Test: `CC(0x624)` reads 0 for every resource asked (8 in
  si_pmu_otp_power; 0, 1, 8 in si_pmu_res_init of chip rev 4): the function
  never calls itself in the tests.
* What I did: a loop instead of the recursion (the stack of the kernel is
  small). The dependencies of the resources found are read level by level
  until a level finds none; accesses and their order are those of the
  recursion. The loop ends after 31 levels at the latest, as many as the
  function looks at resources; dependencies without a circle cannot have
  more. With a circle, or with a card that answers 0xffffffff to every read,
  the object would not come to an end.
* Request: the dependency masks of the real chip (`CC(0x624)` for the
  resources 0..8), and a model with some of them not 0, to test this.

## 4. si_pmu_otp_power: switching off takes resource 8 out of the minimum mask

* Specification ("Resource tables of the 4360"): "Bits 0, 1 and 8 are kept up
  permanently from chip rev 4 on". "si_pmu_otp_power", step 3, off:
  `CC(0x618) = CC(0x618) & ~(rsrc | deps)`; only `deps` is reduced by the
  minimum mask of si_pmu_res_masks, `rsrc` is not.
* Test (`pmu-power`, chip rev 4): switching off reads `CC(0x618)` = 0x103 and
  writes 0x003; switching on writes 0x103 again. The open code does the same.
* Both statements of the specification are right for themselves, together
  they are not: after si_pmu_otp_power(off) resource 8 is not kept up.
* Question: is si_pmu_otp_power called with off for this chip at all (the
  order of events in "Overview" does not contain it)? If it is, is the loss
  of bit 8 of the minimum mask intended?

## 5. si_pmu_measure_alpclk: the measurement is not reached

* Specification ("si_pmu_measure_alpclk"): steps 3 and 4.
* Test (`pmu-pll-init`): `CC(0x608)` reads 0 in all models, the object makes
  this one read and returns 0. Steps 3 and 4 are written as specified but not
  compared.
* Request: a preparation of the scenario with bit 0x100 of `CC(0x608)` set
  and a count in `CC(0x66c)`.

## 6. The accessors do not check the register number

* Specification ("Data", PMU capabilities): gives the number of indirect
  registers of each kind; "si_pmu_chipcontrol / ..." does not mention it.
* Test (`pmu-indirect`): chip control and regulator control register 5 are
  accessed although the model has 2 of each; the object makes the accesses.
* What I did: no check of `reg`.

## 7. Chips outside the scope of the task

* Task: "Scope: chips 0x4360 and 0x4352". The specification also describes
  the chips 0xa9c4 and 0xaa06 in places.
* Test: chip 0x4360 only.
* What I did: what the specification says about named chips is done for
  0x4360 and 0x4352 only. For any other chip id these functions make no
  access, those with a result return 0: `bcm4360_pmu_pll_init`,
  `bcm4360_pmu_res_init` (only the delay of step 13 remains),
  `bcm4360_pmu_alp_clock`, `bcm4360_pmu_si_clock`,
  `bcm4360_pmu_get_bb_vcofreq`, `bcm4360_pmu_fast_pwrup_delay`,
  `bcm4360_pmu_otp_power`, `bcm4360_pmu_is_otp_powered`, `bcm4360_pmu_rfldo`.
  Not chip specific, as in the specification: the accessors of the indirect
  registers, `bcm4360_pmu_pllupd`, `bcm4360_pmu_init`,
  `bcm4360_pmu_measure_alpclk`, `bcm4360_pmu_ilp_clock`.
* Question: the specification does not say whether si_pmu_get_bb_vcofreq,
  si_pmu_otp_power and si_pmu_is_otp_powered select the chip. I treated them
  as chip specific, because resource 8 and the layout of `PMU_PLLCTL[2]` are.

## 8. Identification of the chip

* Task: `struct bcm4360_chip_info` is "read at attach (docs/re/spec/pmu.md,
  "Overview", 1.2)"; the tests fill it in, no function of the task reads it.
* Specification ("Overview", 1.2): names the registers, but not the fields of
  `CC(0x00)` (chip id, revision, package) and not where the revision of
  ChipCommon comes from.
* What I did: no function that reads the identification. The registers are
  defined in `pmu.h` (`CC_CAPS`, `CC_CHIP_STATUS`, `CC_CAPS_EXT`,
  `CC_PMU_CAPS`).
* Request: the layout of `CC(0x00)`, if the identification is to be written
  from this specification.

## 9. Not reached by the tests

Written from the specification only, without comparison:

* chip 0x4352; chip revisions below 3 (si_pmu_pll_init does nothing, both
  resource masks 0) and above 4;
* PMU revisions 0 and 1 in si_pmu_init, PMU revisions below 10 in
  si_pmu_measure_alpclk (the model has revision 17);
* `corereg` with a `val` that has bits outside `mask`: in all calls of the
  tests `val` is inside `mask`. Written as specified, `val` is not limited;
* si_pmu_ilp_clock: the third read of `CC(0x614)`. In the tests two reads in
  a row always agree (0x804a, 0x804a, delay, 0x8192, 0x8192; result 32800);
* si_pmu_get_bb_vcofreq: the integer mode (no access to `PMU_PLLCTL[3]`) and
  the overflow;
* si_pmu_otp_power: the time-out of the wait for the resource (20000 us).
  The time-out of the wait for the OTP status is reached when the power is
  switched off (the model keeps bit 0x1000 of `CC(0x10)` set): 301 reads and
  300 delays of 10 us, as "SPINWAIT" says.

## Remarks

* `struct bcm4360_pmu` has 48 bytes (limit of the task: 256); no field was
  added below the line.
* si_set_bb_vcofreq_frac, step 5, divides a value of 64 bit by the constant
  400000. The guide forbids such a division by a variable only, and the test
  build is for x86-64. A build for a machine of 32 bit will need a division
  function from the platform (`hw.h` has none yet).
* Names: registers and bits are named after the purpose the table "PMU
  registers and the bits used" gives. Named by address or bit because the
  purpose is unknown: `CHIPST_4360_BIT_0x20`, `PMU_CHIPCTL_0x1` and
  `PMU_CHIPCTL_0x1_BIT_0x800`, `PMU_PLLCTL_DIV_BIT_0x1`, `PMU_PLLCTL_0x6`,
  `PMU_PLLCTL_0x7`, `PMU_PLLCTL_0xe`, `PMU_PLLCTL_0xf` and the values written
  to them.
* Not implemented, as the task says: the variables that override registers
  (si_pmu_res_init steps 1, 3 and 5, si_pmu_otp_chipcontrol,
  si_pmu_otp_regcontrol, and with them si_pmu_chip_init and
  si_pmu_swreg_init, which do nothing else for these chips), the functions
  without effect or caller, si_clkctl_xtal, si_clkctl_init, si_clkctl_cc.
