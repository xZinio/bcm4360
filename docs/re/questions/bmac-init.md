# Questions / notes from implementing `bmac-ctrl` (spec sections A1-A7)

Task: `docs/re/tasks/bmac-init.md`. Files written: `open/include/bcm4360/mac.h`,
`open/mac/bmac_ctrl.c`. Test: `python ab.py run mac-ctrl`.

Result: build warning-free; `mac-ctrl` reports **2 differences out of 125
stages** (210 object accesses vs 208 open). Both differences are, with high
confidence, a stage-segmentation artifact of the test harness rather than a
bug in the open code or an error in the spec. Details below.

## 1. `mac-ctrl`: 2 remaining differences are a stage-boundary artifact (not a code or spec bug)

**What the test shows.** Two stages fail, both `wlc_ucode_wake_override_clear`
called with `bit = 0x4`:

```
stage wlc_ucode_wake_override_clear at access 77614, args ['0x4']:
  at access 0 of the object's trace (delete):
    - object   W d11         00120/4 = 40020403
stage wlc_ucode_wake_override_clear at access 77778, args ['0x4']:
    - object   W d11         00120/4 = 40020403
```

i.e. the object's window for each of these stages contains one write
`D11(0x120) = 0x40020403`; the open function makes no access.

**What the spec says (A4).** `wlc_ucode_wake_override_clear`: clear the bit, and
write `D11(0x120)` (via A1) only when `wake_override == 0` afterwards *and* bit
26 (WAKE) of the `maccontrol` copy is clear; otherwise no access.

**Entry state of the two failing stages** (the `wlc_hw` copies the harness
feeds the open function, read with a read-only diagnostic built on `ab.Session`
-- the same object-side machinery `python mac_scenarios.py` uses):

| stage | start idx | bit | wake_override (entry) | after clear | maccontrol | object acc |
|---|---|---|---|---|---|---|
| 97  | 77614 | 0x4 | 0x6 | 0x2 | 0x40020403 | 1 write |
| 98  | 77614 | 0x2 | 0x2 | 0x0 | 0x40020403 | 1 write |
| 111 | 77778 | 0x4 | 0x6 | 0x2 | 0x40020403 | 1 write |
| 112 | 77778 | 0x2 | 0x2 | 0x0 | 0x40020403 | 1 write |

**Why it is an artifact.** Stage 97 (`clear(0x4)`) and stage 98 (`clear(0x2)`)
have the *same* trace start index 77614; likewise 111/112 at 77778. Since
`st.start = len(chip.trace)` at function entry, an identical start for two
consecutive calls proves **zero trace accesses occurred during `clear(0x4)`** --
it is a genuine no-op (after clearing 0x4, `wake_override` is still 0x2, so A4
correctly does not write). The single write `0x40020403` at that index is made
by the *following* `clear(0x2)` call, which takes `wake_override` 0x2 -> 0 with
WAKE clear in the copy and therefore does write.

`ab.close_stages()` ends a stage at the first access whose pc/backtrace is not
"inside" the stage's function. For a zero-access stage immediately followed by
another call of the *same* function, the forward scan reaches the neighbour's
write, whose backtrace still contains a `wlc_ucode_wake_override_clear` frame,
and so includes it. The neighbour's write is thus attributed to *both* stage
windows. The second of each pair (98, 112) passes because the open `clear(0x2)`
reproduces that write; the first of each pair (97, 111) cannot, because in
isolation its `clear(0x4)` is correctly a no-op.

**Why A4's logic is nonetheless confirmed correct** by the clean (non-adjacent)
stages:

* A4 does **not** write when a wake reason remains: stage 57 (`clear(0x4)`, wo
  0xc->0x8) and stage 71 (`clear(0x8)`, wo 0xc->0x4) each make 0 accesses, with
  the same `maccontrol = 0x40020403`.
* The WAKE-in-copy guard is real: stages 15 (`clear(0x2)`, wo 0x2->0,
  `maccontrol = 0x44020402`) and 27 (`clear(0x4)`, wo 0x4->0,
  `maccontrol = 0x44020403`) make 0 accesses because bit 26 is set in the copy.
* A4 does write when wo reaches 0 with WAKE clear: stages 34, 36, 78, 84, 90,
  98, 104, 112, 120 all write `0x40020403`, matched by the open code.

Every `wake_override != 0`-after-clear "write" in the object trace is one of the
two shared-start stages; there is no clean stage exhibiting such a write. So
there is no hidden A4 condition to discover.

**Why no correct code change can reach 0 here.** To match stage 97 the open
`clear(0x4)` (wo 0x6->0x2) would have to emit `W D11(0x120) = 0x40020403`. That
value has WAKE clear, which A1 only produces when `wake_override == 0`; with the
remaining 0x2 reason A1 would instead write `0x44020403` (WAKE re-applied).
Producing `0x40020403` from `clear(0x4)` would mean dropping WAKE while the
microcode must stay awake for the still-pending PHY-register-access reason
(0x02) -- a real bug. The open code therefore keeps the correct behaviour and
does not chase the artifact.

**Suggested harness fix (for the analysts, not done here):** bound a stage by
the call's own return (e.g. using the recorded `st.depth` /
`m.return_addresses()`) instead of scanning forward by pc/backtrace, so a
zero-access stage followed by another call of the same function gets an empty
window.

### Coordinator resolution (2026-10-30)

Confirmed and fixed. The analysis above is correct: `close_stages()` bounded a
stage by a forward scan over the function's address range, so a zero-access
call immediately followed by another call of the *same* function absorbed the
neighbour's write. `mac_scenarios.py` now bounds each stage by that
invocation's own return (`m.at_return`, as `phy_reference` already does), not by
`close_stages`. With that change `mac-ctrl` reports **125 stages, 208/208
accesses, 0 differences**, and the full suite is unchanged (22/22, 0 diffs).
The open code was correct as written; no code change was needed. The shared
`ab.close_stages()` has the same latent issue but is left as is (the radio/PHY
scenarios that use it do not call the same function twice in a row through a
zero-access stage); a later consolidation could move all harnesses to
return-based bounding.

## 2. Things taken from the spec as written (exercised and matching, no open question)

* **A1 mute transform** `v = (v & ~AP) | INFRA` (AP = 0x00040000,
  INFRA = 0x00020000): exercised by stages 55/56 (`enable`/`mctrl`) and 58/60
  (`suspend`/`mctrl`) with `mute_override = 1`; the open code reproduces the
  object's `0x44020403` / `0x44020402` writes exactly.
* **A5 SPINWAIT**: implemented per the spec's SPINWAIT definition (delay 40,
  then read `SHM(0x40)`, loop with delay 10 while the state is 4 and
  `fastpwrup_dly` budget remains). The model returns 0, so exactly one read is
  made, matching the trace. The budget arithmetic for a non-zero/"asleep" state
  is therefore not exercised by the test.
* **Out-of-scope special cases** (d144/0x5357 in A6 step 9 / A7 step 3;
  MAC rev < 13 3-wire BT coex in A6 step 3 / A7 step 7; the 0xffffffff
  "card gone" early-return in A6 steps 4/5/8) are left as one-line comments with
  an early `return` for the card-gone path, per the task scope. None occur on
  this card, so none are exercised by `mac-ctrl`.

## 3. Method note (clean-room)

The per-stage entry state and object-side access counts above were obtained with
a small read-only script that imports `ab`/`mac_scenarios` and calls
`mac_reference()` -- the same emulator-driven object run the test file itself
invokes for its `python mac_scenarios.py` report. Only `wlc_hw` field values
(which the harness already packs into the open function's `struct bcm4360_mac`
argument) and the logical register trace (the "observation of behaviour" the
implementer guide tells us to use) were read. No disassembly, decompiler output,
program counters, or object bytes were inspected; `blob.py`/`index.py`/`emu.py`
etc. were not run directly.

# Questions / notes from implementing `bmac-mhf` (spec section A9)

Task: `docs/re/tasks/bmac-mhf.md`. Files written: `open/include/bcm4360/mac.h`
(host-flag fields + the `bcm4360_mac_mhf` declaration, below the marker),
`open/mac/bmac_mhf.c`. Test: `python ab.py run mac-mhf`.

Result: build warning-free; `mac-mhf` reports **0 differences out of 35 stages**
(6 object accesses, 6 open); `mac-ctrl` re-run stays at **0 differences** (the
`mac.h` change did not disturb it).

## 1. `bands == 3` DOES write the shared memory (task note is wrong; spec A9 and the test agree)

The task (`docs/re/tasks/bmac-mhf.md`) says of `bands == 3`: "note it makes no
register access." That is **incorrect**, and I did not follow it. Spec A9, read
literally, has step 2 (update the selected band's copy; write `SHM(word)` when
`clk != 0`, the value changed, and the selected band is the current band) run
*before* step 3 (set both bands' copies). For `bands == 3` step 1 selects the
**current** band, so step 2's "selected band is the current band" condition is
satisfied and the conditional write fires exactly as for `bands == 0`.

The test confirms the spec, not the task note. My first version followed the
task note (update both copies first, so the current band's old == new and the
change-test never fires) and `mac-mhf` reported 5 differences -- all of the form
"object wrote, open did not" on `bands == 3` calls whose current-band value
changed with the clock on:

```
stage wlc_bmac_mhf at access 3194,  args ['0x0','0x100','0x100','0x3']: object W shm 0005e/2 = 0100
stage wlc_bmac_mhf at access 56307, args ['0x4','0x8000','0x8000','0x3']: object W shm 000d4/2 = 8081
stage wlc_bmac_mhf at access 56327, args ['0x3','0x20','0x20','0x3']:     object W shm 00078/2 = 0060
stage wlc_bmac_mhf at access 56780, args ['0x4','0x8','0x8','0x3']:       object W shm 000d4/2 = 8088
stage wlc_bmac_mhf at access 67396, args ['0x4','0x8','0x0','0x3']:       object W shm 000d4/2 = 8081
```

Reordering to the literal spec (step 2 for the current band first -- which emits
that write -- then step 3 setting both copies) gives 0 differences. So the five
`bands == 3` antenna/BT-coex host-flag updates during bring-up each do write the
current band's word. Per the implementer guide, the observed behaviour wins; the
task note's parenthetical should be read as applying only to step 3 itself ("no
further access"), which is how spec A9 phrases it.

## 2. Things taken from the spec as written (exercised and matching, no open question)

* The write condition is the full A9 triple: `clk != 0` **and** value changed
  **and** selected band == current band. A `bands == 1`/`2` call for the
  non-current band only updates that band's copy (no access); exercised and
  matching.
* `idx` is not range-checked (spec A9: "`idx` is not checked"); the five-entry
  `idx -> SHM offset` table (0x5e, 0x60, 0x62, 0x78, 0xd4) is indexed directly.
  The test never passes `idx > 4`.
* A band whose `band_present` bit is clear is skipped (both in the selected-band
  step and in the `bands == 3` both-copies step).
* `wlc_bmac_mhf_get` (pure getter, no access) was not implemented -- out of
  scope per the task and not in the test.
