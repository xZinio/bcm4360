# Questions and notes for `phy-init` (initialisation and channel set)

What I had to guess, where a specification was incomplete or contradicted by a
test, and what I did. Saved early; filled in as the work went on.

## 1. The regulator field of `sub_0b018f` step 2: `si_corereg`, not `si_pmu_regcontrol`

- **Spec** (acphy-init section 3 step 2, acphy-radio section 9): the regulator
  field is written with `si_pmu_regcontrol(sih, 0, 0x01f00000, v << 20)`, "the
  access is: read `CC(0x658)`, write `CC(0x658)` = 0, read ... `CC(0x65c)` ...".
  The **task** says to use `bcm4360_pmu_regcontrol()`.
- **Test shows**: at that point the object makes two *service* calls, not direct
  ChipCommon accesses:
  `call chip_corereg(0x0, 0x658, 0xffffffff, 0x0)` and
  `call chip_corereg(0x0, 0x65c, 0x1f00000, 0x0)`.
  So on the 4360 `si_pmu_regcontrol` reaches the PMU regulator register through
  `si_corereg` on ChipCommon, and `si_corereg` is a compared *service* in the
  PHY tests. `bcm4360_pmu_regcontrol()` (open) makes the ChipCommon accesses
  directly instead, which do not match.
- **What I did**: followed the observed behaviour (the guide's rule). I write the
  regulator field with two `bcm4360_chip_corereg(hw, 0, CC_PMU_REGCTL_ADDR/DATA,
  ...)` calls, matching the object's `si_corereg` service calls, rather than
  `bcm4360_pmu_regcontrol()`. The value `v` is as the spec says (r = PHY(0)&0xf;
  in the model PHY(0) reads the revision, so r = 1 and v = 0). `pmu.md`/the
  `pmu-*` scenarios still compare `bcm4360_pmu_regcontrol` directly, so that
  function is right in its own context; only here the object goes through
  `si_corereg`.

## 2. `wlc_phy_tssivisible_thresh_acphy` is a leaf but its return feeds a compared write

- The channel function step 28.4 writes `PHY(0x1641) = 0x7f00 + t` where
  `t = wlc_phy_tssivisible_thresh_acphy(pi)`. The write is in the channel
  function (a compared, non-leaf access), but `wlc_phy_tssivisible_thresh_acphy`
  is on the `LEAVES` list. A stub returning 0 would write `0x7f00` where the
  object writes `0x7f14`.
- The value is fully specified in acphy-chanspec section 5 ("Value of t in step
  28"), so I compute it inline (chip 0x4360, board type 0x137/0x117 -> 0x14)
  instead of stubbing. Only the return value is reproduced; the leaf's own
  (excluded) accesses are not. This is why phy-chanspec/phy-init do not report a
  difference at `PHY(0x1641)`.

## 3. `_band_change` / `_tssi_setup` radio functions are NOT called here

- In the object the radio band change (acphy-radio section 15) and the TSSI
  radio set-up (section 16) run *inside* `sub_09e378` and `sub_0940f8`, both on
  `LEAVES`. The test excludes every access whose call chain contains a leaf, so
  the object's band-change radio accesses are not in the compared trace.
  Calling `bcm4360_radio_band_change` from my (non-leaf) channel function would
  add radio accesses the object's trace has filtered out (and fail for PHY rev
  0). So `sub_09e378` is stubbed whole and those radio functions are left for
  the rxgain task. The radio functions actually called from the compared paths
  are `_chan_entry`, `_tune`, `_rfpll_150khz`, `_vcocal`, `_vcocal_wait`,
  `_afecal`, plus the switch-radio internals (`_pwron_seq`, `_rccal`, `_rcal`,
  `_off`) reached through the already-finished `bcm4360_phy_switch_radio`.

## 4. REMAINING DIFFERENCES: a leaf -> non-leaf state dependency (`sub_09eaf9`)

**Status: phy-init reports 4 differences, phy-chanspec reports 14. Every one of
them is caused by the stubbed leaf `sub_09eaf9` (acphy-rxgain, annex A2), and
none by a non-leaf function of this task.** All other scenarios (access,
radio-*, pmu-*, phy-attach, phy-attach-state) are at 0.

### What the differences are

The differing accesses are *only* these registers (verified by grouping every
`- object` line of both scenarios):

- `PHY(0x140)` bit 11 — read/written by the classifier `wlc_phy_classifier_acphy`
  (H1), which H4 calls when it leaves the carrier search (channel function step
  34);
- `PHY(0x725)` bit 9 and `PHY(0x73a)` bit 7 (and the core-1 mirrors `0x925`,
  `0x93a`) — read and restored by the AFE calibration `sub_096203`
  (`bcm4360_radio_afecal`, channel function step 25).

`sub_09eaf9` (annex A2, step 2 and step 8) is the *only* function in any
specification that writes these bits: `PHY(0x140)` bit 11 = 0x0800 for 20 MHz
(cleared for 40/80 MHz), `PHY(0x725)` bit 9 always, `PHY(0x73a)` bit 7 for PHY
rev 1 (bandwidth-dependent for PHY rev 0). It runs at step 17, *before* the
afecal (step 25) and the classifier leave (step 34).

### Why the middle task cannot fix them

- The comparison excludes the object's leaf accesses from the expected trace
  (`ab.logical(..., services=skip)`, `skip` includes `sub_09eaf9`), but it
  excludes **nothing** from the open trace (`ab.logical(trace, names, spaces)`
  with no `services`). The card is only restored to the object's state at stage
  entry; there is no replay of the object's leaf register writes onto the open
  card during the stage.
- So a non-leaf function (`sub_096203`, the classifier) that reads a register
  the leaf `sub_09eaf9` wrote sees the entry-state value in the open run and the
  leaf-modified value in the object run. On the very first initialisation after
  attach (entry bits clear) and on every bandwidth change (bit 11 flips with the
  bandwidth) the two diverge.
- Reproducing `sub_09eaf9`'s writes to these registers does **not** help: I
  tested it, and it makes things *worse* — the open trace then has extra writes
  the object trace excludes, so the access counts stop matching (phy-chanspec
  went 4095 vs 4255) and the differences rose from 14 to 16. There is no
  non-leaf write of these registers to fold the state into.
- With pure stubs the access counts match exactly (phy-init 16280 = 16280,
  phy-chanspec 4095 = 4095); the differences are read-value divergences only,
  entirely from `sub_09eaf9`'s un-reproduced state.

### What I did / recommendation

Kept the leaves as empty stubs (as the task asks), which is the state with the
fewest and cleanest differences. These differences resolve on their own once the
acphy-rxgain task implements `sub_09eaf9` and it is taken off the `LEAVES` list.
Per the task note I report them here: they are non-leaf *accesses* (classifier,
afecal) but leaf-*caused* state divergences, so the `LEAVES`-based exclusion does
not cover them. Suggested adjustments for the harness, if 0 is required before
rxgain is done: seed the open card with the object's excluded (leaf) register
writes for the stage, or add the specific reads (`sub_096203`'s 0x725/0x73a,
`wlc_phy_classifier_acphy`'s 0x140) to the exclusion.

## 5. Minor

- Step 32 (signal-strength correction) makes no hardware access: `sub_08f086`
  reads the per-core gain stage tables `pi_ac+0x46a/0x4a6/0x650` filled by a
  leaf, so `x` is computed from zeroed tables and `pi+0x212` is not meaningful
  yet. Implemented per sections 8/9 as the task asks; no effect on the trace.
- `pi+0xa8c` (wlc_phy_init step 6) is 0 in the traces and not in the open state;
  I treat it as always 0.
