# Task `phy-init`: initialisation of the PHY

Specifications: [`../spec/acphy-init.md`](../spec/acphy-init.md) (all of it),
[`../spec/acphy-radio.md`](../spec/acphy-radio.md) sections 3, 4, 8 and 9
(switching the radio on, what the initialisation does with OTP word 16),
register access: [`../spec/access.md`](../spec/access.md).
Code to build on: `open/phy/phy_attach.c` with `open/include/bcm4360/phy.h`
(the state of the PHY and the interface to the MAC layer, made by the task
`phy-attach`; read them first, and the task file
[`phy-attach.md`](phy-attach.md) for the rules of this part of the driver),
`open/phy/radio2069.c`, `open/chip/pmu.c`, `open/hw/access.c`.
Scenario: `phy-init`; the scenarios `phy-attach` and `phy-attach-state` must
still pass when you are done.

Scope: chip 0x4360, AC-PHY revisions 0 and 1, radio 2069 revisions 3 and 4.

## What to write

`open/phy/phy_init.c`; additions to `open/include/bcm4360/phy.h` (new fields
of the state, prototypes) and to `open/phy/phy_todo.c`; and the part of
`bcm4360_phy_switch_radio(phy, true)` in `open/phy/phy_attach.c` that the
task `phy-attach` left open. You may change `phy.h`, `phy_attach.c` and
`phy_todo.c`; say in your report what you changed in code that was not yours.

```c
/* wlc_phy_init: acphy-init.md, section 1 */
void bcm4360_phy_init(struct bcm4360_phy *phy, u16 chanspec);

/* wlc_acphy_set_scramb_dyn_bw_en: acphy-init.md, H13 */
void bcm4360_phy_set_scramb_dyn_bw_en(struct bcm4360_phy *phy, bool enable);

/*
 * wlc_phy_ldpc_override_set: for the AC-PHY nothing but a call of
 * wlc_phy_update_rxldpc_acphy (acphy-init.md, H12) with the same argument
 */
void bcm4360_phy_ldpc_override_set(struct bcm4360_phy *phy, bool ldpc);
```

One function per procedure of the specification (sections 1 to 8, 3a, the
helpers H1 to H14, appendix A), with names of your choice that say what the
function does; the comment of each names the section. Helpers that other
parts of the PHY will use (carrier search, RF sequencer, CCA reset, receive
core state, classifier ...) are declared in `phy.h`.

### Register access of the PMU

Where the specification says `si_pmu_regcontrol`, call
`bcm4360_pmu_regcontrol()` of `pmu.h` with the PMU that the board record of
attach contains; its accesses are compared. `si_gpiocontrol`, `si_core_cflags`
and the functions of the MAC layer are services (`phy_env.h`).

### Tables

The static tables are data of Broadcom: get each by its name with
`hw_fw_data()` (`acphy_mcs_tbl_rev0` ...; the names and the order are in the
specification, "Static table set"). The list of the records - table id,
offset, width, number of entries, name of the data - is part of your code.
The record of core 1 that points to the data of core 0 is how the object is;
do the same. If a table is missing the initialisation goes on without it.

### Functions of other tasks

The initialisation calls the channel function, which is specified in
`acphy-chanspec.md` and belongs to the next task, and functions of the
transmit power control and of the interference mitigation that the
specification describes only as far as the initialisation needs them. Rules:

* What `acphy-init.md` describes completely (also in its appendix, also where
  it says "owned by" another specification), you implement.
* What it only names, you declare in `phy.h` with a comment that names the
  specification it belongs to, and define in `open/phy/phy_todo.c` as a
  function that does nothing (returning 0 if it returns something). The
  channel function is one of them: `bcm4360_phy_chanspec_set_acphy(phy,
  chanspec)`.
* The test leaves out of the comparison what the object does inside the
  functions that no finished task covers. So a difference that the test
  reports is yours.

## What the tests do

`phy-init` runs two sessions of the object for each of the two pairs of
revisions: attach, up, periodic work, down; and attach, up, channel 36/80,
down, up, channel 149/40, down, up, channel 6, down. The initialisation runs
at every "up", on the channel that was set last. Your code is taken through
the same calls of the MAC layer into the PHY (all functions of the interface
that exist in the open code are called, in the order of the object; before
each call the card is put into the state it had when the object was called).
Compared are the stages of the functions of this task: accesses to PHY and
radio registers, PHY tables, shared memory, the 802.11 core and the PMU, the
delays, and the calls of services with their arguments.
