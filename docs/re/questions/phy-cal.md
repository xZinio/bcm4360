# Questions from implementing phy-cal

Implementer notes for the analysts. Format: what the spec says, what the test
shows, what I did. (Being filled in as I iterate; saved early.)

## 1. Shared helpers are `static` in finished files

- Spec: `acphy-cal-tx.md`/`acphy-cal-rx.md` "Scope" refer to `sub_09c4e4`
  (set bbmult), `sub_098751` (get bbmult), `sub_09bf99` (read/write cal
  coeffs, selections 0..11), `sub_092ffa` (gain pulse), `sub_093ebe`
  (gpiosel/sample-setup), `sub_09bbe4`/`sub_09be13` (adc-read save/restore) as
  "already specified elsewhere". `wlc_phy_classifier_acphy` (H1),
  `wlc_phy_force_rfseq_acphy` (H6) and `wlc_phyreg_enter/exit` (H13) are in
  `acphy-init.md`.
- Code: every one of these is a **`static`** function inside `phy_txpower.c`
  or `phy_init.c`, so it cannot be called from `phy_cal.c`.
- What I did: the task forbids changing existing `.c` files (except moving
  `phy_todo.c` stubs and reconciling the tone). To respect that hard rule I
  reimplemented the small ones as file-local statics in `phy_cal.c`, copied
  verbatim from the finished code so they make the identical accesses. This
  duplicates code; a cleaner design would export the finished versions
  (drop `static`, declare in `phy.h`). `sub_093ebe` appears in both
  `acphy-txpower.md` (as `wlc_phy_gpiosel_acphy`) and `acphy-cal-rx.md` (as
  `wlc_phy_tempsense_sample_setup_acphy`) at the same offset 0x093ebe - it is
  one function; likewise `sub_09bbe4`/`sub_09be13`.

## 2. Tone reconciliation

- Spec: `acphy-cal-tx.md` sections 12/14 fully specify
  `wlc_phy_tx_tone_acphy`/`wlc_phy_stopplayback_acphy`. `phy_txpower.c` had a
  partial amplitude-0 reconstruction (`phy_tx_tone`/`phy_stopplayback`,
  static).
- What I did: implemented the full functions in `phy_cal.c`
  (`bcm4360_phy_tx_tone_acphy`, `bcm4360_phy_stopplayback_acphy`) and redirected
  `phy_txpower.c`'s `phy_poll_samps_war` to call them (deleting the static
  reconstruction). The idle-TSSI call is amplitude 0 with `dont_deaf=0`,
  sample-player, in a nested carrier-search, which reproduces the old
  reconstruction's accesses (resetcca from the nested `stay_in_carriersearch`,
  then the sample-player start).

## 3. `wlc_phy_cordic` is not specified

- Spec: `acphy-cal-tx.md` section 13 gives only: 18 iterations, angle unit
  0x1680000 per full turn (= Q16 degrees), gain seed 0x9b75, output ~2^15,
  "Not specified further".
- What I did: implemented a standard rotation-mode CORDIC (public algorithm)
  with those constants; the arctan table is computed from `atan(2^-i)` scaled
  to the angle unit (derived, not taken from the object). The seed / I-vs-Q
  assignment and rounding of the tone scaling were confirmed against the test.

## 4. rx-cal (`sub_0addfa`) PHY override list is under-specified

- Spec: `acphy-cal-rx.md` step 7 says "The full bit list is a fixed override
  table; see the trace reference in Verification." - the exact per-core
  save/override sequence is not written out.
- What I did: implemented the structure from the spec and used the test diff
  (nothing excluded) to fill in the exact register/bit sequence.

## 5. State added to `struct bcm4360_phy`

- The calibration state block (`pi+0xfb8`, pointer `pi+0xf58`) is zero after
  attach; I added `struct bcm4360_phy_cal_state` (phase machine, coefficients,
  pre-cal gain records, per-run scratch save areas, scanroam cache) plus the
  tone bbmult save (`pi_ac+0x02+2c`, flag `pi_ac+0x0a`) and the measured
  temperature (`pi_ac+0x8dc`). `pi+0xf86` (per-phase collect mask) is 0 on this
  board, so the `pi_ac+0x44c` logic is omitted. `sh+0x34` (free-running time)
  has no source in the open state; it is stored but never read back into a
  compared access.
