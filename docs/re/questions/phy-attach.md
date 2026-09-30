# Questions of the implementer: task `phy-attach`

Specification: [`../spec/acphy-attach.md`](../spec/acphy-attach.md) ("procedure
N" is a section of its "Procedures"), with
[`../spec/acphy-radio.md`](../spec/acphy-radio.md) sections 1 to 4,
[`../spec/acphy-chanspec.md`](../spec/acphy-chanspec.md) sections 3 and 4 and
[`../spec/access.md`](../spec/access.md). Code:
`open/include/bcm4360/phy.h`, `open/phy/phy_attach.c`, `open/phy/phy_todo.c`.

Result of the tests (`python ab.py run NAME`):

| Scenario | Stages | Accesses of the object | of the open code | Differences |
|---|---|---|---|---|
| `phy-attach` | 88 | 206 | 206 | 0 |
| `phy-attach-state` | 1260 | 1260 | 1260 | 0 |
| `access` | 1 | 422 | 422 | 0 |
| all `radio-*`, all `pmu-*` | | | | 0 each |

`python ab.py build` has no warnings. `phy-init` in `python ab.py all` still
DIFFERs: it needs `bcm4360_phy_init`, `bcm4360_phy_set_scramb_dyn_bw_en` and
`bcm4360_phy_ldpc_override_set`, which belong to the initialisation task and
are not written yet; that scenario is not part of this task.

The specification agreed with the tests everywhere: no observed behaviour
contradicted my reading. The points below are what I had to decide where the
task file or the specification leaves a choice, and the branches the tests do
not reach. I continued the partial files of a colleague: the design of `struct
bcm4360_phy` in `phy.h` and the register/service flow of `phy_attach.c` were
already there; I filled in procedures 4, 6, 7, 8 and 11, the missing steps of
procedure 2 (5, 12, 13, 17, 20) and `bcm4360_phy_get_var()`.

## 1. `bcm4360_phy_get_var`: which names the test asks for

* Task file: "The names are those of the variables in the table 'Variables
  read at attach (summary)'" plus the names with a dot, and "`boardflags3` is
  not asked".
* Test (`phy-attach-state`): it also asks for the four `swctrlmap*` variables
  (`swctrlmap_2g`, `swctrlmapext_2g`, `swctrlmap_5g`, `swctrlmapext_5g`, index
  0..4), which the task text does not name explicitly; the object has 0 for
  every element on both variable sets (boardflags3 bit 8 is clear). All other
  asked names were the ones of the summary table.
* What I did: `get_var` returns the stored value for every summary-table
  variable, every dot-name and the four `swctrlmap*` arrays. Names it does not
  know give `false`. I keep a handler for the plain `interference` as well
  (returning the 2.4 GHz mode); the test asks only for `.interference_2g` and
  `.interference_5g`, so that handler is never reached and its choice is not
  verified.
* Request: list the `swctrlmap*` names (and, if it is asked anywhere, the plain
  `interference`) in the task file.

## 2. `get_var` of the tx power offsets: which of the several copies is read

* Specification (procedure 7, step 2 and "Tx power offsets"): the object keeps
  the MCS offsets of 5 GHz in twelve groups, most of them copies of one
  another (`pi+0xc6c` etc.), and the OFDM and 2.4 GHz MCS offsets more than
  once too. A variable such as `mcsbw205glpo` is therefore stored in six
  places.
* Test: `phy-attach-state` reads `mcsbw205glpo` (and the others) and my value
  matches, so the test reads the *first* place of each variable
  (`mcsbw205g?po` at group 0, `mcsbw405g?po` at group 3, `mcsbw805g?po` at
  group 5; `mcsbw202gpo` at `pi+0xc5c`, `mcsbw402gpo` at `pi+0xc64`,
  `ofdmlrbw202gpo` at `pi+0xd14`). `dot11agofdmhrbw202gpo` is not asked (it is
  not stored on its own, only folded into `pi+0xc50`).
* What I did: `get_var` returns the primary storage location of each offset
  variable; the field `struct bcm4360_phy_srom_po` keeps every copy in the
  order of the object (`ofdm_2g[3]`, `mcs_2g[4]`, `mcs_5g[12][3]`), because the
  meaning of the copies belongs to `wlc_phy_txpwr_apply_srom11` of the tx power
  task.
* Question: is the choice of the first location the intended contract, or does
  `phy_state.py` read a fixed `pi` offset that happens to be the first one?

## 3. `si_alp_clock` makes no register access

* Specification (procedure 6, step 9): `pi+0xc24 = si_alp_clock(sih)`,
  "40000000 if bit 0 of the chip status is set, else 20000000; no register
  access, but the PCI windows are moved to ChipCommon and back".
* Test: the attach trace has only the window moves (seq 549..552), no
  ChipCommon read, inside this step.
* What I did: `phy->xtal_hz = bcm4360_pmu_alp_clock(phy->pmu)`, which reads the
  cached chip status of `struct bcm4360_pmu` and makes no access. `get_var`
  (`.xtal_hz`) returns it; the test has 40000000. The window moves are not in
  the compared spaces, so nothing is lost by leaving them out.

## 4. Receive gains, mid and high 5 GHz: the elna variables are read only with an external LNA

* Specification (procedure 6, step 18.4/18.5): "g = `rxgains5gmelnagaina<c>`
  and y = `rxgains5gmtrelnabypa<c>` if `pi_ac+0x341` is set and the variable
  exists, else 0; t = `rxgains5gmtrisoa<c>` if it exists".
* Test: variable set `a` has no external LNA in either band (boardflags bits 12
  and 28 clear) but *does* contain `rxgains5gmelnagaina0` and the like. The
  look-up comparison of `phy-attach` passes, so the object does **not** look
  those names up when the band has no external LNA; it does look up the
  `...trisoa<c>` names in every case.
* What I did: the elna and trelnabyp names are read only inside `if (elna_5g)`
  (a C short circuit, so no `hw_getvar` when the flag is clear); the triso name
  always. The same for the 2.4 GHz and 5 GHz-low bands (step 18.2/18.3). This
  is what made the look-up set match; a reading that always looks the names up
  would have added names the object does not ask for.

## 5. `tempoffset` is read and then discarded

* Specification (procedure 7, step 4 and procedure 2, step 17; "Open
  questions"): `wlc_phy_txpwr_srom11_read` computes `pi+0xc35` from
  `tempoffset`, and `wlc_phy_attach` clears it to 0 right after. "the variable
  has no effect in this version of the driver ... an implementation that wants
  the same behaviour must end with 0."
* Test: `get_var("tempoffset")` has 0 in every run.
* What I did: procedure 7 computes `temp.offset` as specified (so the variable
  is looked up, which the set needs), and procedure 2, step 17
  (`phy_attach_temp_limits`) sets it back to 0. `get_var` returns 0.

## 6. Number of cores: 2 on this board, `PHY(0x0b)` read all the same

* Specification (procedure 6, step 14): read `PHY(0x0b)`, then force N to 2 for
  chip 0x4360 with board type 0x137 or 0x117.
* Test: the model answers `PHY(0x0b)` = 0; N is forced to 2. The rxgains and
  noiselvl variables are looked up for cores 0 and 1 only; `maxp`, `pa`,
  `pdoffset`, `rxgainerr`, `rssicorrnorm` for cores 0, 1 and 2.
* What I did: the read is made in every case; `get_var` uses `phy->ver.cores`
  for the rxgains and noiselvl names and `BCM4360_PHY_CORES_MAX` (3) for the
  others, as the task file says. A board with N = 3 (the `PHY(0x0b)` value or
  another board type) is not reached by the tests.

## 7. Branches the tests do not reach

The code of these is written from the specification alone; the two variable
sets and two PHY/radio revisions of `phy-attach`/`phy-attach-state` do not
exercise them.

| Branch | Section | Why it is not reached |
|---|---|---|
| `interference` variable present | procedure 2, step 13 | neither variable set defines it; the modes come from the PHY revision (1 for rev 0, 7 for rev 1) |
| `boardflags3` bit 8: `swctrlmap*` read from the SROM | procedure 6, step 25 | bit 8 is clear on both sets; the fields stay 0 |
| `boardflags3` bit 13: second OTP read of word 16 into `sh+0xf2` | procedure 6, step 32 | bit 13 clear; `phy->rcal_otp` stays 0 (the object's `otp2` run is not in these scenarios) |
| `tempthresh` = 0x111 board, or a non-4360 board | procedure 2, step 17 | the board is 0x117, so the threshold is forced to 120 |
| write pacing limit 1 (Apple board 0x093) | procedure 2, step 4 | the board is 0x117: limit 24 |
| radio id 0x030b, second band (5 GHz) attach | procedure 2, steps 2, 3, 19 | the open MAC attaches the PHY once, for 2.4 GHz; the id is 0x2069 |
| the failure exits of `wlc_phy_attach` | procedure 2 | attach succeeds in every run |

* Request: variable sets with `interference`, with boardflags3 bit 8 and bit
  13, and a board that keeps the SROM `tempthresh`.

## 8. `bcm4360_phy_switch_radio(phy, true)` (the radio-on path)

* Task file / specification (acphy-radio.md section 4, "On"): the on path
  suspends the MAC, runs the power-on sequence, RCAL and RCCAL, and when the
  PHY is already initialised calls `sub_0a04c2` and the channel function, then
  enables the MAC.
* Test: this task switches the radio only off (`phy-attach`); the on path is
  not compared here (it is reached by `phy-init`, a later task).
* What I did: the on path is implemented in `phy_attach.c`
  (`acphy_switch_radio_on`) with the power-on sequence, RCAL and RCCAL of
  `radio2069.c`; the two functions of later tasks it ends with,
  `bcm4360_phy_set_regtbl_on_pwron_acphy` and `bcm4360_phy_chanspec_set_acphy`,
  are declared in `phy.h` and defined empty in `phy_todo.c`, as the task asks.
  Their placement and arguments are from the specification and are not verified
  by a test in this task.

## 9. Names and state kept but not read back

* The specification names the object's state by memory offsets. Every value is
  a named field of `struct bcm4360_phy` (the colleague's design), with a
  comment giving the offset and the step. Fields of unknown purpose keep the
  offset in the name (`pi_0xc04`, `acphy_0x671`).
* Attach fills state that no consumer of this task reads and that `get_var`
  does not expose: the CRS-minimum-power calibration (`crsmincal`), the
  interference-mitigation parameters (`hwaci`, procedure 11), the high-RSSI
  bypass defaults (`hirssi`), the ten saved PHY registers, the RCCAL defaults
  and many single fields. I set them all as the specification says, so the
  structure is complete for the later tasks, but only the register/service
  trace and the `get_var` values are checked. Their correctness beyond "as
  written" is therefore not verified here.
* `struct bcm4360_phy` is large (one structure for a card, as the task wants);
  it is allocated with `hw_zalloc`, so every field the procedures do not touch
  is 0, which matches the "everything not listed is zero" of the tables.

## 10. Look-up set: how it was confirmed

* `phy-attach` compares the set of variable names the object asks for during
  the first `wlc_phy_attach` with the names the open code passes to
  `hw_getvar()`. My helpers (`phy_getint`, `phy_getarr`, `phy_getvar_val`) call
  `hw_getvar` once per name; arrays are read element by element (as the object
  does with `getintvararray`), which asks the same name several times, but the
  comparison is of the set, so that does not matter. The set matched on the
  first passing build for both variable sets and both revisions, which is the
  evidence that the conditional look-ups (external LNA, boardflags3 bit 8) are
  gated the way the object gates them.
