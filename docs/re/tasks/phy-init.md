# Task `phy-init`: initialisation of the PHY and setting the channel

This one task covers two functions of the object that call each other and
share state, so they are written and tested together: the PHY initialisation
(`wlc_phy_init` and what it drives) and the channel-set function
(`sub_0a7089`). The big leaf functions they call - front end and analog
filters, receive-gain and desense, transmit power - belong to later tasks and
are left as empty stubs for now; the tests leave their accesses out of the
comparison until those tasks implement them.

Specifications (read the named parts fully):
- [`../spec/acphy-init.md`](../spec/acphy-init.md): all of it.
- [`../spec/acphy-chanspec.md`](../spec/acphy-chanspec.md): all of it, except
  the annex sections A1, A2, A3, A4, A5, A6, A7 (those describe the leaf
  functions you stub; you may read them for orientation but you do not
  implement them here).
- [`../spec/acphy-radio.md`](../spec/acphy-radio.md): sections 3 to 16 - the
  channel function tunes the radio by calling the functions of
  `open/phy/radio2069.c`, which are finished; you call them, you do not
  rewrite them.
- register access: [`../spec/access.md`](../spec/access.md).

Code to build on (read first, do not change except `phy.h`, `phy_todo.c`):
`open/phy/phy_attach.c` with `open/include/bcm4360/phy.h` (the PHY state and
the interface to the MAC layer, made by the task `phy-attach`; read
[`phy-attach.md`](phy-attach.md) for the rules of this part of the driver),
`open/phy/radio2069.c`, `open/chip/pmu.c`, `open/hw/access.c`.
Scenarios: `phy-init` and `phy-chanspec` must both reach 0 differences; the
scenarios `phy-attach`, `phy-attach-state`, `access`, `radio-*`, `pmu-*` must
stay at 0.

Scope: chip 0x4360, AC-PHY revisions 0 and 1, radio 2069 revisions 3 and 4.

## What to write

`open/phy/phy_init.c` and `open/phy/phy_chanspec.c` (split the two areas as
you like; one file is also fine); additions to `open/include/bcm4360/phy.h`
(new state fields, prototypes) and to `open/phy/phy_todo.c` (the leaf stubs).
Say in your report what you changed in `phy.h`/`phy_todo.c`.

Public functions (names and arguments are the contract with the tests):

```c
/* wlc_phy_init: acphy-init.md, section 1 */
void bcm4360_phy_init(struct bcm4360_phy *phy, u16 chanspec);

/* wlc_phy_chanspec_set: acphy-chanspec.md, section 4 (it calls the channel
 * function sub_0a7089, acphy-chanspec.md section 5) */
void bcm4360_phy_chanspec_set(struct bcm4360_phy *phy, u16 chanspec);

/* wlc_acphy_set_scramb_dyn_bw_en: acphy-init.md, H13 */
void bcm4360_phy_set_scramb_dyn_bw_en(struct bcm4360_phy *phy, bool enable);

/* wlc_phy_ldpc_override_set -> wlc_phy_update_rxldpc_acphy: acphy-init.md H12 */
void bcm4360_phy_ldpc_override_set(struct bcm4360_phy *phy, bool ldpc);
```

Implement, with one function per procedure of the specifications (names of
your choice, the comment naming the section):

* **Initialisation** (acphy-init.md): `wlc_phy_init` (section 1),
  `wlc_phy_chanspec_shm_set` (2), `sub_0b018f` (3), the shared-memory writes
  of the high-RSSI bypass (3a - both functions), `sub_0a04c2` incl. loading
  the static table set (4; get each table by name with `hw_fw_data()`, the
  names and order are in the specification), `sub_0a0be4` (5), and the helpers
  H1 to H14 that the specification describes (carrier search, clip detection,
  classifier, OFDM CRS, deaf, CCA reset, RF sequencer, receive-core state,
  rxldpc, scrambler, hirssi read/clear). Declare the helpers that other parts
  of the PHY will reuse in `phy.h`.
* **Channel set** (acphy-chanspec.md): `wlc_phy_chanspec_set` and
  `wlc_phy_chanspec_shm_set` (section 4), the channel function `sub_0a7089`
  (section 5) with all its own register work and its calls, and the small
  helpers `wlc_phy_get_chan_freq_range_acphy` (6),
  `wlc_phy_chanspec_bandrange_get` (7), `wlc_phy_get_rxgainerr_phy` (8),
  `sub_08f086` (9), `sub_0995ab` (10, a no-op on the 4360). The spur-mode
  functions (11, 12) are not reached on the 4360; a stub that does nothing is
  enough, with a comment.
* **Radio**: where section 5 of acphy-chanspec (and acphy-init section 3 step
  12) tune or switch the radio, call the finished functions of
  `radio2069.h` (`bcm4360_radio_pwron_seq`, `_rccal`, `_chan_entry`, `_tune`,
  `_rfpll_150khz`, `_vcocal`, `_vcocal_wait`, `_afecal`, `_band_change`,
  `_off`, `_rcal`). Build the `struct bcm4360_radio` from your PHY state; the
  tables (`prefregs_2069_rev*`, `chan_tuning_2069rev*`) come from
  `hw_fw_data()`. The PMU regulator field of `sub_0b018f` step 2 uses
  `bcm4360_pmu_regcontrol()` (`pmu.h`) with the PMU from the board record.

### Leaf functions you do NOT implement (stub them in `phy_todo.c`)

The channel function and the initialisation call these; leave each an empty
function (returning 0 if it returns a value), with a comment naming the
specification it will come from. The tests leave their accesses out of the
comparison. Front end / analog filters / reciprocity (acphy-rxgain):
`bcm4360_..._set_regtbl_on_chan_change` (sub_0a4adc), `..._on_bw_change`
(sub_09eaf9), `..._on_band_change`'s PHY part (sub_09e378), the femctrl tables
(sub_0a6b0f, sub_0a602f), the analog LPF functions, `wlc_phy_populate_recipcoeffs`.
Receive gain and desense (acphy-desense): sub_09af05, sub_09a121, sub_09a539,
sub_092efb, sub_0909fd, `wlc_phy_crs_min_pwr_cal`, `wlc_phy_hwaci_setup`,
`wlc_phy_aci_w2nb_setup`. Transmit power (acphy-txpower): sub_098949,
`wlc_phy_txpwr_by_index`, `wlc_phy_txpwrctrl_enable`, `wlc_phy_tssivisible_thresh`,
the idle-TSSI measurement (sub_0affa9). Give the stubs clear names and the
right signatures (from the sections that call them); a later task fills them.
The exact set the tests exclude is the list `LEAVES` in
`tools/re/phy_scenarios.py` - if a test reports a difference whose accesses
come from one of these, it is in the stub set and the difference is spurious;
report it and I will adjust. If a difference comes from a function that is
NOT a leaf, it is yours to fix.

## What the tests do

`phy-init` runs two sessions of the object per revision pair (attach, up,
down; and attach, up on channel 36/80, down, up on 149/40, down, up on 6,
down); the initialisation runs at every "up". `phy-chanspec` runs the full
session (attach, up, nine channel changes, watchdog, down). Your code is
taken through the same calls of the MAC layer into the PHY, in the object's
order, each time on a card put back into the state the object had; your PHY's
own state is what your code built up through the sequence (so the
initialisation must run before the channel changes, which it does). Compared,
for the functions of this task: accesses to PHY and radio registers, PHY
tables, shared memory, the 802.11 core and the PMU; the delays; and the
service calls (`phy_env.h`) with their arguments. The accesses made inside the
stubbed leaf functions are not compared.

Write your questions into `docs/re/questions/phy-init.md` (save early) and
finish with the report the guide asks for.
