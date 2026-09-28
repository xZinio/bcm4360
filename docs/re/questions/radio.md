# Questions of the implementer: task `radio`

Specification: [`../spec/acphy-radio.md`](../spec/acphy-radio.md) (sections in
parentheses are its "Procedures"), register access:
[`../spec/access.md`](../spec/access.md). Code:
`open/include/bcm4360/radio2069.h`, `open/phy/radio2069.c`.

Result of the tests (`python ab.py run NAME`):

| Scenario | Stages | Accesses of the object | of the open code | Differences |
|---|---|---|---|---|
| `radio-pwron` | 3 | 271 | 271 | 0 |
| `radio-off` | 9 | 108 | 108 | 0 |
| `radio-rcal` | 3 | 303 | 303 | 0 |
| `radio-rccal` | 3 | 857 | 857 | 0 |
| `radio-tune` | 18 | 1368 | 1368 | 0 |
| `radio-vcocal-wait` | 18 | 1224 | 1224 | 0 |
| `radio-afecal` | 12 | 1040 | 1040 | 0 |
| `radio-band` | 4 | 16 | 16 | 0 |
| `radio-tssi` | 15 | 540 | 540 | 0 |
| `access` | 1 | 422 | 422 | 0 |

In `radio-rccal` the three results of the calibration are equal to those of
the object as well. `radio2069.c` compiles without warnings; the complete
build (`python ab.py build`) has none at the end of my work.

The test contradicted my reading of the specification in one place (point
1); everything else was as specified. The other points are what the
specification leaves open, what I had to decide myself, and what the tests do
not reach. For each: what the specification says, what the test shows, what I
did.

How I observed: three times I changed my code for one test run (a marker
delay or another primitive) to see accesses and values of the object in the
list of differences, as my predecessor did for the pacing. These changes are
removed; the results above are from the final code.

## 1. Band change on 5 GHz: the registers of core 2 do not depend on boardflags bit 29

* Specification (section 15): "5 GHz: if `pi_ac+0x34a` != 0, for c = 0 and 1
  (fixed): `mod(RADIO(0x05c|b), 0xf000, 0x6000)`; `mod(RADIO(0x061|b), 0xf000,
  0x6000)`. Then, if the radio revision is below 4: `mod(RADIO(0x45c), 0xf000,
  0x6000)`; `mod(RADIO(0x461), 0xf000, 0x6000)`." The sentence can be read with
  the second part inside or outside the condition on bit 29.
* Test (`radio-band`, radio revision 3, PHY revision 0, chanspec 0xd024 and
  0xd897): the object modifies `RADIO(0x45c)` and `RADIO(0x461)` and nothing
  else, so bit 29 is clear there and the second part is done all the same.
* What I did: cores 0 and 1 only with bit 29; core 2 with radio revision
  below 4, whatever bit 29 is. My first reading (all inside the condition)
  was wrong and gave 2 differences.
* Request: say it in the specification. Not tested: bit 29 set (point 4).

## 2. Power-on sequence, step 3: which primitive writes `PHY(0x720)`

* Specification (section 5, step 3): "Read `PHY(0x720)`, write it back with
  bits 0x0180 set." It does not say `mod`, as it does elsewhere.
* Test: I tried both, a `phy_reg_read` followed by a `phy_reg_write`, and
  `phy_reg_or`: `radio-pwron` reports 0 differences for both. At the level of
  PHY registers they are the same; they differ in the raw accesses and in
  the write pacing, which the radio scenarios do not compare (point 3).
* What I did: a read followed by a `phy_reg_write` (the literal reading). It
  is a guess.
* Question: which primitive does the object use here? The same question for
  every place where the specification says "write" of a PHY register after a
  read of it: section 14, step 2.7 (I use `phy_reg_write`).

## 3. The write pacing is not compared in the radio scenarios

* Specification (`access.md`, "PHY registers"): a dummy read before every
  24th write in a row, one counter for PHY and radio writes.
* Test: the radio scenarios compare the spaces `phy`, `radio` and `delay`
  only; the dummy reads of `D11(0x3e0)` and `D11(0x120)` are left out. The
  test also gives every stage a new `struct bcm4360_phy_io` with the counter
  at 0, while the counter of the object has the value that the accesses
  before the stage left.
* What I did: all accesses go through the functions of `open/hw/access.c`,
  so writes are counted and paced as `access.md` says. Where the dummy reads
  fall in the rows of writes of this task (22 or 24 preferred values, 50 words
  of a channel entry, the eight writes of radio off) is not verified.
* Request: compare the space `d11` without the raw register pairs in these
  scenarios, and hand over the counter of the object at the entry of a stage.

## 4. Branches that the tests do not reach

What I conclude from the numbers of accesses and from marker runs. The code
of these branches is written from the specification alone.

| Branch | Section | How I know that it is not reached |
|---|---|---|
| boardflags bit 1: `mod(RADIO(0x8ea), 0x0100, 0x0100)` | 5, step 5 | 271 accesses = 3 x 43 + (24 + 24 + 22) table writes + 3 x 2 cores x 12: nothing is left for the two accesses of the step |
| RCAL skipped (`skip_rcal`) | 4, "On", step 4 | 303 accesses = 35 + 233 + 35: all three stages measure |
| no table of preferred values (`prefregs` = NULL) | 5, 6 | radio revisions 3 and 4 have a table |
| boardflags bit 29: cores 0 and 1 on 5 GHz | 15 | point 1 |
| PHY revision 1 at the band change | 15 | see below |
| mode not 0, both bands | 16 | a marker delay in this branch gave no difference in `radio-tssi` |
| core 2 of the power detector set-up (`RADIO(0x54c)`) | 16, step 2.2 | 36 accesses per stage = 8 + 2 cores x 14 |
| wait with `settle` = 1 (delay 120) | 13 | 1224 accesses = 12 x 2 + 6 x 200: no stage has the delay |
| a PHY with three cores | all | all stages have two cores |
| RCCAL: `n1` below `n0`, a result above 0xff | 7, step 2.6 | see point 5 |
| a channel without entry | 10 | the test calls `bcm4360_radio_tune()` only with an entry |

PHY revision 1 at the band change: the scenario leaves out the stages in
which the object makes no radio access, so my function is not called for
them. That the object makes none with PHY revision 1 follows from the number
of stages (4, all of the configuration with PHY revision 0); that my code
makes none is not tested.

* Request: configurations with boardflags bit 1, bit 29 and boardflags3 bit 3
  or 13 set, a call with mode 1 for both bands, a wait with `settle` = 1.

## 5. RC calibration: what the values of the test cover

* Specification (section 7, step 2.6): g = ((n1 - n0) * 0xc1) >> 8 in 32 bit
  signed arithmetic, the low byte is stored.
* Test: in both configurations with `done` the card answers n0 = 0x0100,
  n1 = 0x0180 and `RADIO(0x416)` = 0x01ef in both passes (seen with a marker
  delay after the reads). The results are 0x60, 0x60 and 0x0f, and 0x0f goes
  into the radio; equal to the object's. In the configuration without `done`
  the three results keep the values they had.
* What I did: as specified; `n0` and `n1` are taken as 16 bit values without
  sign, the difference is signed, the shift is arithmetic (the compiler's
  behaviour for signed values, as the kernel relies on it), the result is
  cut to 8 bit.
* Not tested: n1 < n0 (negative product, where an arithmetic and a logical
  shift differ) and a quotient above 0xff (the cut). Question: are the two
  counters really read as unsigned 16 bit values before the subtraction?

## 6. AFE calibration: which of the two reads gives which bit

* Specification (section 14, step 2.5): "read `RADIO(0x144|b)` -> a; read it
  again -> e; stop when bit 0 of e and bit 1 of a are set."
* Test: the card answers the same to both reads (both bits in the
  configurations with `done`: one round of 3 accesses per core; none
  otherwise: 10 rounds). A swap of the two values or of the two bits would
  not show.
* What I did: as specified, bit 1 from the first and bit 0 from the second
  value.

## 7. AFE calibration, step 4: what is inside the loop over the cores

* Specification (section 14, step 4): "`mod(RADIO(0x8ea), 0x0080, 0x0080)`; for
  each core c: `mod(RADIO(0x15f|b), ...)`; `mod(RADIO(0x15f|b), ...)`; then for
  k = 0..13: read ... write ...; finally `mod(RADIO(0x8ea), 0x0080, 0)`." It
  does not say where the loop over the cores ends.
* Test (radio revision 3): per core the two modifications of `RADIO(0x15f|b)`
  and then the 14 pairs of read and write of the same core; bit 7 of
  `RADIO(0x8ea)` once before the first and once after the last core.
* What I did: that. Request: number the sub-steps as in step 2.

## 8. Power detector set-up: bits of the mask and values of the mode that the specification does not name

* Specification (section 16): "For each core c whose bit is set in the mask";
  "`mod(RADIO(0x01f|b), 0x0004, mode << 2)`"; modes are 0 and 1.
* Test: every stage sets up both cores of a PHY with two cores (the value of
  the mask itself I do not see), mode 0 only (point 4).
* What I did: cores 0 .. N-1 are looked at, bits of the mask above are
  ignored. For a mode above 1 the branch "not 0" is taken and `mode << 2` is
  cut by the mask like in the object's arithmetic (mode 2 clears the bit).
  For a core above 2 step 2.2 is left out.
* Question: what does the object do with a bit of the mask for a core that
  the PHY does not have?

## 9. Tuning: `RADIO(0x123)` of the three cores

* Specification (section 11, steps 2, 3 and 7): the last word of the entry
  goes to `RADIO(0x723)`; radio revision 3 then writes 0x03e9 to `RADIO(0x323)`
  and `RADIO(0x523)`; radio revision 4 writes 0x83e0 to `RADIO(0x723)`.
* Test: as specified, for both revisions, on 2.4 and 5 GHz and for channel 4.
* What I did: as specified. Observation, no difference in behaviour: with
  revision 4 word 51 of the entry is overwritten at once, and with revision 3
  the register of core 0 (`RADIO(0x123)`) is not written by the patch. If the
  bank 0x600 means "all cores", core 0 keeps the value of the table and the
  two others get 0x03e9.
* Question: is that so in the object, or is there a write to `RADIO(0x123)`
  in a branch that the traces do not reach?

## 10. Interface of the task: what the specification does not cover

* `bcm4360_radio_chan_entry()`: without a table (`chan_tuning` = NULL) and
  for a channel without entry the result is NULL and `*freq` is 0 (section 10:
  "0 and frequency 0 if not"). `freq` must not be NULL.
* `bcm4360_radio_tune()` with `entry` = NULL makes no access. Section 11 says
  this of the channel function as a whole ("if there is none the function
  returns at once and nothing is changed"); the rest of that function is the
  caller's.
* `bcm4360_radio_pwron_seq()` decides by `tbl.prefregs` (NULL or not), not by
  the radio revision, whether steps 4 to 8 of section 5 are made: the caller
  selects the table by revision (specification "Tables").
* A table of preferred values whose first address is 0xffff: the first entry
  is written all the same (section 6), also in my code. The caller must not
  hand over an empty table.
* `bcm4360_radio_init()` sets the three results to 0x80, 0x80 and 0x0c
  (section 7, notes) and touches nothing else.

## 11. Names

* Specification: no register has a name. Guide ("Code"): registers of unknown
  purpose are named by their address, constants get names.
* What I did: registers `R2069_REG_0x8ea`, `ACPHY_REG_0x408`; fields by their
  position (`R2069_0x8ea_BIT7`, `R2069_0x8ed_BITS9_10`); values by the
  procedure that writes them (`ACPHY_0x416_RADIO_OFF`,
  `R2069_0x8ed_BITS9_10_RCCAL`). A purpose is named only where the
  specification states it: the bits "done" of the four calibrations, the
  start bit and the fields "sr", "sc" and "x1" of the RCCAL (aliases of the
  specification).
* The 50 addresses of the channel entry are numbers in one table
  (`chan_radio_regs[]`), in the order of the specification; a name for each
  would repeat the number. Question: is that acceptable under the rule?
* The PHY registers of this task are defined in `radio2069.h`, because there
  is no header of the PHY yet. They should move there when it exists.

## Remarks

* `radio-tune`, `radio-vcocal-wait`: 6 stages per configuration. The numbers
  of accesses fit the initialisation and the channels 36, 36/80, 4, 6 and
  149/40 (three stages on 5 GHz, one on channel 4, two others on 2.4 GHz; four
  AFE calibrations per configuration; four band changes in the configuration
  with PHY revision 0). The last two channel changes of `RADIO_CHANNELS` in
  `scenarios.py`, 100/80 and 13, do not seem to show up as stages. This is a
  conclusion from the numbers, I cannot see the stages themselves.
* The specification refers to `re-out\analysis\fields\acphy-radio.tsv`,
  `chanobs.py` and `blob.py` ("Structure fields", "Tables"). I may not read
  them and did not need them: the tables are handed over by the caller.
* `struct bcm4360_radio` has 48 bytes (limit of the task: 512); I added no
  field. A static assertion in `radio2069.c` checks the limit.
* At my first build `open/chip/pmu.c` (not part of this task, I did not touch
  it) gave two warnings about unused tables; at the end it gave none.
