# Task `phy-attach`: creating the PHY and its state

Specifications: [`../spec/acphy-attach.md`](../spec/acphy-attach.md) (the
main one), [`../spec/acphy-radio.md`](../spec/acphy-radio.md) sections 1 to 4
(analog core, identification of the radio, switching the radio), register
access: [`../spec/access.md`](../spec/access.md).
Code to use and not to change: `open/hw/access.c`, `open/phy/radio2069.c`,
`open/chip/pmu.c` with their headers.
Scenarios: `phy-attach`, `phy-attach-state` (`python ab.py list`).

Scope: chip 0x4360 on the Apple board (board vendor 0x106b, board type
0x117), AC-PHY revisions 0 and 1, radio 2069 revisions 3 and 4. Where the
specification describes other chips, PHY types or revisions, a comment is
enough.

## The PHY as a part of the driver

This task lays the foundation of `open/phy/`: the state of the PHY, and the
interface through which the MAC layer uses it. Later tasks (initialisation,
channel, receive gain, transmit power, calibrations) add to both.

* The specifications describe the state of the object by places in its
  memory (`pi+0x17e`, `pi_ac+0x342`, `sh+0x64`). These places are of no use
  to you: design `struct bcm4360_phy` yourself, with fields that have names
  and types, grouped by what they are for. What you need from the
  specifications is which values exist, how they are made, and who uses
  them. Keep a field for every value that attach makes, also where the
  consumer is not written yet. Note in a comment at the field which variable
  or which step of the specification it comes from.
* The object has three structures (`sh` shared by the PHYs of a card, `pi`,
  `pi_ac`) because it supports many PHY types and cards with one PHY per
  band. The BCM4360 has one PHY for both bands. One structure is enough.
* The object is given the identification of chip and board by the MAC layer
  and looks up the variables of the board by name. Your code gets the first
  as `struct bcm4360_phy_board` and the second through `hw_getvar()` of
  `hw.h`.
* What the PHY needs from the MAC layer and the chip layer is declared in
  `open/include/bcm4360/phy_env.h`. Read the comment at its top: in the tests
  these functions are recorders, and the calls you make to them are compared
  with those of the object. Where the specification says `wlapi_bmac_corereset`,
  `si_core_sflags`, `si_get_sromctl`, `otp_read_word`, `wlapi_init_timer` ...,
  call the function of `phy_env.h` that stands for it, at the same place,
  with the same arguments.
* Tables of Broadcom (for this task: the tables of the radio,
  `prefregs_2069_rev3`, `prefregs_2069_rev4`, `chan_tuning_2069rev3`,
  `chan_tuning_2069rev4`) come from `hw_fw_data()` of `hw.h`. Get them at
  attach; attach fails if the table of channels is missing.

## What to write

`open/include/bcm4360/phy.h`, `open/phy/phy_attach.c`, and
`open/phy/phy_todo.c` (see "Functions of later tasks").

```c
/* what the platform knows about chip and board before the PHY exists */
struct bcm4360_phy_board {
	u32 chip;		/* chip id, 0x4360 */
	u32 chiprev;
	u32 chippkg;
	u32 corerev;		/* revision of the 802.11 core, 42 */
	u32 sromrev;		/* revision of the SROM, 11 */
	u32 boardtype;
	u32 boardrev;
	u32 boardvendor;
	u32 boardflags;
	u32 boardflags2;
	u32 xtal_hz;		/* frequency of the crystal (ALP clock) in Hz */
	u16 pci_vendor;
	u16 pci_device;
	struct bcm4360_pmu *pmu;	/* the PMU of the chip (pmu.h), for later tasks */
};

struct bcm4360_phy;		/* yours */

/*
 * wlc_phy_shared_attach and the first wlc_phy_attach (2.4 GHz) in one.
 * NULL if it fails. The memory comes from hw_zalloc().
 */
struct bcm4360_phy *bcm4360_phy_attach(struct bcm4360_hw *hw,
				       const struct bcm4360_phy_board *board);
void bcm4360_phy_detach(struct bcm4360_phy *phy);		/* wlc_phy_detach */

/* acphy-attach.md, section 12 */
void bcm4360_phy_machwcap_set(struct bcm4360_phy *phy, u32 caps);
void bcm4360_phy_get_phyversion(struct bcm4360_phy *phy, u16 *phytype, u16 *phyrev,
				u16 *radioid, u16 *radiorev);
u32 bcm4360_phy_get_coreflags(struct bcm4360_phy *phy);
u32 bcm4360_phy_cap_get(struct bcm4360_phy *phy);
void bcm4360_phy_stf_chain_init(struct bcm4360_phy *phy, u8 txchain, u8 rxchain);

/* acphy-radio.md, sections 1, 3 and 4 */
void bcm4360_phy_anacore(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_switch_radio(struct bcm4360_phy *phy, bool on);

/* acphy-chanspec.md, sections 4 (the plain accessors) and 3 (wlc_phy_clk_bwbits, bw_state) */
void bcm4360_phy_chanspec_radio_set(struct bcm4360_phy *phy, u16 chanspec);
u16 bcm4360_phy_chanspec_get(struct bcm4360_phy *phy);
void bcm4360_phy_bw_state_set(struct bcm4360_phy *phy, u16 bw);
u16 bcm4360_phy_bw_state_get(struct bcm4360_phy *phy);
u32 bcm4360_phy_clk_bwbits(struct bcm4360_phy *phy);

/*
 * Three functions that only note something (they are not in a specification
 * yet; this is all they do): "the clock of the core is on, the registers of
 * the PHY may be accessed" (the flag that the specifications call `sh+0x31`),
 * "the driver is up" (`sh+0x30`), and "the chip went through a power-on
 * reset: the next initialisation loads the tables" (`pi+0x185` = 1).
 */
void bcm4360_phy_hw_clk_state_upd(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_hw_state_upd(struct bcm4360_phy *phy, bool up);
void bcm4360_phy_por_inform(struct bcm4360_phy *phy);

/*
 * For tests and debugging: element `index` of the value that the PHY keeps
 * for the variable `name` (0 for a variable that is not an array), after
 * the conversion of the specification. false: no such name or index.
 */
bool bcm4360_phy_get_var(const struct bcm4360_phy *phy, const char *name, u32 index,
			 s32 *value);
```

Names and arguments are the contract with the tests.

### bcm4360_phy_get_var

The names are those of the variables in the table "Variables read at attach
(summary)" of the specification, with these details:

* a variable per core carries the number of the core in its name as in the
  specification (`maxp2ga0`, `rxgains5gmtrisoa1`, `rssicorrnorm_c0`);
* arrays: `maxp5ga<c>[k]` k = 0..3; `pa2ga<c>[i]` i = 0..2; `pa5ga<c>[i]` i =
  0..11 in the order of the variable; `rxgainerr5ga<c>[k]`, `noiselvl5ga<c>[k]`,
  `tssifloor5g[k]`: k = 0..3; the others as the specification gives them;
* the value is what the specification says is stored: for `rxgains...elnagaina<c>`
  2 * v + 6 (after the rule for missing values), for `rxgainerr2ga1` the sum
  with the value of core 0, for `noiselvl2ga0` -70 minus the variable, and so
  on. The test compares as many bits as the object keeps (8, 16 or 32) and
  ignores the rest;
* variables of three cores (`maxp`, `pa`, `pdoffset`, `rxgainerr`, `rssicorrnorm`)
  are asked for the cores 0, 1 and 2, the `rxgains` and `noiselvl` variables
  for the cores the PHY has;
* `boardflags3` is not asked.

Names with a dot are not variables:

| Name | Value |
|---|---|
| `.phy_type`, `.phy_rev` | from `D11(0x3e0)` |
| `.cores` | number of cores N |
| `.radio_id`, `.radio_rev` | identification of the radio |
| `.chanspec` | chanspec of the radio |
| `.write_limit` | limit of the write pacing |
| `.xtal_hz` | frequency of the crystal |
| `.interference_2g`, `.interference_5g` | interference modes configured for the bands |
| `.rxgainerr2g_empty`, `.rxgainerr5g_empty[k]` | "the SROM has no gain errors" for 2.4 GHz and for the 5 GHz sub-band k |
| `.otp_word16_bits8_12` | bits 8..12 of OTP word 16 |

### Functions of later tasks

`bcm4360_phy_switch_radio(phy, true)` ends, when the PHY is initialised, with
calls of functions that belong to the tasks of the initialisation and of the
channel (acphy-radio.md, section 4). Declare the functions you need for that
in `phy.h`, with a comment that names specification and section, and define
them in `open/phy/phy_todo.c` as functions that do nothing. The tests of this
task only switch the radio off.

## What the tests do

`phy-attach` runs the attach of the object (for PHY revision 1 with radio
revision 4 and PHY revision 0 with radio revision 3, each with two sets of
variables) and then calls your functions in the order in which the MAC layer
of the object called the PHY during its attach: `bcm4360_phy_attach`,
`bcm4360_phy_machwcap_set`, `bcm4360_phy_get_phyversion`, ...,
`bcm4360_phy_switch_radio(phy, false)`, `bcm4360_phy_anacore(phy, false)`, ...
Before each call the card is put into the state it had when the object was
called. Compared are: the accesses to PHY and radio registers, to the 802.11
core and to the PMU, the delays, the calls of services with their arguments,
the results, and the set of variables that attach looks up (each name that
the object asks for has to be asked for, and no other).

The second call of `wlc_phy_attach` (for the 5 GHz band) has no counterpart:
the MAC layer of the open driver attaches the PHY once.

`phy-attach-state` compares the state after attach through
`bcm4360_phy_get_var()`.
