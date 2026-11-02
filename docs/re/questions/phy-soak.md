# Questions from task `phy-soak`

Implementer notes for the analysts: what the specification says, what
`tools/re/phy_soak.py` shows, what I did. Code: `open/phy/phy_cal.c`,
`phy_rx.c`, `phy_desense.c`, `phy_rxgain.c`, `phy_txpower.c`, `phy_api.c`,
new `open/phy/phy_noise.h`.

## Status

Second round (after the answers phy-calrx5g and the fixed soak of 160 steps
that re-associates for each channel and injects the IQ estimates): Q1 to Q7
are answered in the specifications and implemented (acphy-cal-rx.md X1-X10:
`phy_cal.c` - the integer helpers, `sub_097809`, the image combination, the
fit, the tone lists and `two`, the isolating estimate, `sub_09107b` with its
table, both gain tables and the exact walk, the loopback by revision;
acphy-cal-tx.md 15 B: the seed source; acphy-desense.md C2 / E1 step 4,
including E1's corrected tail). The join calibrations on 36/80, 6 and 36l are
identical with the injected estimates. After that, `phy_soak.py` (160 steps)
differed only in core 1's LOFT entries `TBL(0x62)[i]` of the calibrations on
149/80 (Q8); Q8 is answered and implemented as well (third round).

(First round, kept for the record: the soak then differed only in the
receive calibration at 5 GHz, 40/80 MHz.)

## Corrections that needed no question

* `wlc_phy_rssi_compute`: `rxh+0x24` = 0 on both paths
  (acphy-api-rx.md, as corrected) - `bcm4360_phy_rssi_compute`.
* Table A1: the 32 rows of acphy-chanspec.md - `pa_table` in `phy_rxgain.c`.
* `sub_0b1227` step 1 (acphy-init.md section 7): the per-core flags
  `cal+0x65+c` are cleared; the open code did not, so the gain schedules of
  `sub_09c91d` were not loaded again in the second and later calibrations
  (soak: `joincal` steps 119, 129).
* `sub_0addfa` step 1: `img` = 0 for PHY revision 1 on 5 GHz (no image
  second estimate); the open code always took it.
* `wlc_phy_cals_acphy` phase 0 step 2: `cal+0xc0` = `sh+0x34` (`phy->now`);
  the open code wrote 0 ("no source"), so the periodic calibration was due
  120 s after up instead of 120 s after the last calibration.
* The multi-phase sequence (acphy-cal-tx sections 1 and 5, `sub_0abc76` with
  `mphase` = 1, the seed from `cal+4` for `sm` = 1, the finishing step F only
  at phase 0xc / 0x10) was not implemented; it is now, as specified.

## Q1. `sub_0addfa` step 7: `PHY(0x729)` bit 5 on 2.4 GHz after a 5 GHz calibration

**Answered (acphy-cal-rx.md X1):** the condition is the current band only; the
soak's later calibrations were on 36/80 because the driver ignored the channel
changes while associated. Implemented as specified (unchanged).

- Spec (acphy-cal-rx.md, step 7 listing): "5 GHz only: if phytype 0xb &
  phyrev != 0 & band 0xc000: `mod(PHY(b+0x729), 0x20, 0x20)`", between
  `mod(PHY(b+0x720), 2, 2)` and `mod(PHY(b+0x729), 0x200, 0x200)`.
- Soak: at 36/80 (first calibration of the session) the object makes this
  mod, as specified. A session whose first calibration is on channel 6
  (`attach,mpc 0,up,roam_off 1,6,assoc,unquiet,joincal`) does not make it
  (identical to the open code). But in the full soak, the calibration on
  channel 6 that follows the one on 36/80 (step 119) **does** make it, and so
  does the one on 149/80. So the condition is not (only) the band of the
  current channel: some state left by the 5 GHz (80 MHz) calibration keeps it
  true on 2.4 GHz. In the same calibration the object reads `PHY(0x211)` = 1
  in step 4 (the digital filter of the previous 80 MHz calibration, see Q5).
- What I did: the mod as specified (5 GHz, PHY revision != 0). What is the
  exact condition? (Candidates I cannot check: a flag such as `pi_ac+0x8d8`
  or `pi_ac+0x8c4` that the 80 MHz calibration sets and nothing clears.)

## Q2. `sub_094b1a`: the 5 GHz radio loopback

**Answered (X10):** rev 0 `0x200, 0x80`, rev 1 `1, 0x200`; the fourth mask is
0x40 and the rev-dependent pair on 0x20/0x21 has masks 0x20 and 4. Implemented.

- Spec (acphy-cal-rx.md, `sub_094b1a`): 5 GHz uses `0x23/0x3d/0x21` with "the
  loopback enable `RADIO(0x23)` mask 0x200 then 0x80/0x80".
- Soak (36/80): after `mod(RADIO(0x21), 4, 4)` the object sets bit 0 of
  `RADIO(0x23)` (0x0000 -> 0x0001), then `mod(0x23, 0x200, 0x200)` (-> 0x0201),
  and there is **no** 0x80 mod; then `mod(0x21, 3, 0)`, `mod(0x23, 6, 0)`.
- What I did: that sequence, with `mod(0x23, 0x1, 0x1)` for the bit-0 write.
  The register is 0 there (all six were zeroed), so the mask of that mod is
  not observable: which is it?

## Q3. Tone frequencies of the receive calibration: bandwidth labels swapped

**Answered (X2, X9):** confirmed; tones {+-2} / {+-4} / {+-8, +-24, +-32} x 500
kHz for 20 / 40 / 80 MHz. Implemented.

- Spec (acphy-cal-rx.md, `sub_0ad89a`): `F` = 4000 / 2000 / 1000 for "20 /
  0x1800 / 40 MHz". questions/phy-cal.md item 9 already found 1000 at 20 MHz.
- Soak (36/80): the gain-search tone has a period of 20 samples at 80 samples
  per period of the 80 MHz table, i.e. `F` = 4000. So `F` = 1000 / 2000 / 4000
  for 20 / 40 / 80 MHz (0x1000 / 0x1800 / 0x2000): the labels of the text are
  the other way round (the document also calls 0x2000 "40 MHz" in tempsense
  step 1, while acphy-cal-tx.md and the chanspec use 0x2000 = 80 MHz).
- The tone loop of step 11 at 80 MHz also plays 4000 kHz (soak, the first
  tone after the gain search), not the 1000 of 20 MHz. Step 10's "`A` = 8 (20
  MHz) / 4 (0x1800) / 2 (else)" with a tone of `A*500` fits the observations
  only with the same swap (2 at 20 MHz -> 1000, 8 at 80 MHz -> 4000).
- What I did: `F` = 1000 << bandwidth index in the gain search. The tone list
  of step 10 is left at the 20 MHz one (Q5). 40 MHz (2000) is the middle
  value of the text, not observed.

## Q4. `sub_0ad89a`: the 5 GHz gain table `DAT_0054dbe0`

**Answered (X9):** both tables and the exact walk. Implemented.

- Spec: "the 5 GHz table `DAT_0054dbe0` was not observed" (phy-cal.md item 9);
  the open code used the 2.4 GHz table for both bands.
- Soak (36/80, the measurement model's powers): the search starts at `gi` = 4
  and visits 4, 5, ..., 10. The object writes `PHY(0x734)` = 0x04, 0x0c, 0x14,
  0x1c, 0x24, 0x2c, 0x2d for `gi` = 4..10, i.e. `{s3, s7}` = {0,4}, {1,4},
  {2,4}, {3,4}, {4,4}, {5,4}, {5,5}, and calls
  `wlc_phy_txpwr_by_index_acphy` with index 0 (it reads `TBL(0x20)[0]`) for
  all of them (`s5` = 0). (Found with a temporary experiment that was taken
  out again.) Entries 0..3 are never reached with these powers.
- What I did: nothing (the 2.4 GHz table still stands in, so the 36/80
  calibration differs from `gi` = 4 on). Please give the whole table.

## Q5. `sub_0addfa` at 40 and 80 MHz with measured powers: not enough to implement

**Answered (X2-X8):** implemented as specified; with the soak's injected
estimates the calibrations on 36/80, 36l and 6 are identical.

With the gain table of Q4 and the tones of Q3 put in temporarily, the 36/80
calibration still differs in the tone loop and after it. What is missing:

1. Step 10, the tone list for 40 and 80 MHz and `pi_ac+0x8c4`: which tones
   (frequencies, signs, how many: "2 (or 4/6 with `pi_ac+0x8c4`)"), the
   sample count 0x3000, and for which bandwidth `pi_ac+0x8c4` is set (the soak
   shows `PHY(0x211)` = 1 after the 80 MHz calibration, i.e. `sub_09107b`
   mode 1 ran at 80 MHz). Is `pi_ac+0x8c4` ever cleared?
2. `sub_097809` (`wlc_phy_calc_iqcc_acphy`) for non-zero powers: the steps
   name the operations but not their exact shifts and rounding
   (`wlc_phy_nbits`, `wlc_phy_sqrt_int`, `wlc_phy_inv_cordic` are not
   specified either), and the image combination of step 11e ("combine the two
   with nbits/sqrt_int/inv_cordic") is not described. The open code returns
   the zero-power result {0, 0x400, 0} for every input. (At 2.4 GHz / 20 MHz
   the soak is nevertheless identical, coefficients included.)
3. The regression term of the digital filter (`pi_ac+0x8c8`, step 12) and
   `sub_09107b` mode 1: the 0xf2-byte table at `DAT_0054da90` and the exact
   writes to `PHY(0x210)/0x211/0x212` and `PHY(c*0x200+0x6a4..0x6ae)` are not
   given.

So the joincal steps on 36/80 and 149/80 (and, through Q1 and the 0x211 bit,
the later one on channel 6) cannot be made identical from the specification.

## Q6. `sub_0abc76` B: the coefficients are seeded in every phase

**Answered (acphy-cal-tx.md 15 B):** the writes are made in every call; only
the copy into `cal+0x2c` is limited to `cal[1]` < 3. Implemented.

- Spec (acphy-cal-tx.md section 15 B): "only while `cal[1]` < 3, i.e. at the
  first mphase step or single-shot".
- Soak (`wait 130` after `149/80, unquiet, joincal, interf 1`; the periodic
  multi-phase calibration): the object writes the start coefficients
  (`sub_09bf99` mode 1, selections 0..3 per core for part 0, selection 0 for
  part 1: `TBL(0x0c)[0x40/0x41]`, `[0x43]`, ... with their brackets) after
  `PHY(0x382)` = 0x8a09 in **every** phase 2..0xc and 0xe..0x10. The access
  count confirms it: 10 more part-A phases x 50 accesses + 3 part-B phases x
  14 = 542, plus the 1384 of the 80 MHz receive calibration (Q5), is exactly
  the difference of the step (74359 - 72433); `wait 920` has twice that.
- What I did: seed in every call. Is the condition on another byte (for
  example `cal[2]`, the sub-phase, which nothing in the specification
  increments), or is there none?

## Q7. Single-chain boards: C2 and the hwaci wide-band detector use every core

**Answered (acphy-desense.md C2 step 3, E1 step 4):** every core <
`pi+0x168`, no mask; E1's tail corrected as well. Implemented.

- Spec: acphy-desense.md C2 (`sub_08f41b`) step 3 "for each present core c"
  and E1 step 4 "for each present core, read four registers"; "present" =
  bit set in `sh+0xa7` (Procedures, introduction).
- Soak, `--board 7` and `--board 8` (`txchain` = `rxchain` = 1, so `sh+0xa4`
  .. `sh+0xa7` = 1, `pi+0x168` = 2): the object writes core 1's eight mods
  of `PHY(0x910..0x913)` in every C2 call (16 accesses at every channel
  change, in every carrier-sense calibration of the noise steps) and reads
  `PHY(0x9af/0x9ab/0x9b3/0x9b1)` in every hwaci tick (4 per tick). Nothing
  else differs on channel 6 (attach .. watchdog, joincal, wait, noise: the
  same after the change).
- What I did: both loops over every core < `pi+0x168`. Which cores does the
  object really take (all `pi+0x168`, or a mask of the hardware other than
  `sh+0xa5`/`sh+0xa7`)? With `rxchain` = 2 (core 0 absent) the core-0 part of
  C2 would tell; no board of the soak has that. C1 itself (the ring, the
  references, the indices) keeps "present cores": the noise steps are
  identical with it.

## Q8. `wlc_phy_populate_tx_loft_comp_tbl_acphy` at 5 GHz: the per-index deltas

**Answered (acphy-cal-tx.md section 19, completed):** `T[6]`, `HI[12]`,
`LO[12]`, the high-channel limits {200, 149, 100} against the chanspec's
channel byte, k = 4c + 2 flag[c] + (i <= T[c + 3 flag[c]]), bytes added modulo
256, i outer / core inner, inside the `PHY(0x19e)` bracket. Implemented
(`txcal_populate_loft`); the 149/80 join calibration is identical.

- Spec (acphy-cal-tx.md section 19): cores 1, 2 at 5 GHz get "`d[c]` plus a
  small per-index delta (from the constant arrays hi = {..,0xec,0xf6,0xf6,0xf8,
  0xf6,0xfa} and lo = {..,0,0xfb,0xf2,0xfc,0xf6,0xfb}, indexed by the
  high-channel flag and whether i is below the threshold
  {0x80,0x80,0x20,0x80,0x1a,0x1c})". The leading elements are elided and the
  index (which of the six for core c, flag f, entry i) is not given.
- Soak (after the receive calibration became identical, X1-X10): the join
  calibrations on 36/80, 36l and 6 are identical (core 1 at 5 GHz with the
  flag 0 gets `d[1]` unchanged); on 149/80 (core 1's flag = 1, 149 >= 0x95)
  the object writes `TBL(0x62)[i]` = 0xf6fb (with `d[1]` = 0) where the open
  code writes 0, in both calls of the calibration (part 0 and 1: 256
  differences).
- What I did: nothing (the open code writes `d[c]` for every entry, which is
  right where the delta is 0). Please give both arrays in full and the index
  for (core, flag, i), and whether the delta applies at flag 0.

## Notes on the noise path (task point 6)

* Implemented from acphy-api-rx.md "Noise, completed" N1-N9 and the C1
  corrections of acphy-desense.md, as N11 lists. With the join calibration on
  channel 6 (identical there) the noise steps and the thresholds carried into
  `wait` and `up` are identical (`attach .. 6, assoc, unquiet, joincal,
  interf 1, wait 5, noise 1, wait 20, noise 2, qtxpower 60, wait 130, noise 3,
  1, down, up, wait 20, noise 5`: the same).
* `pi_ac+0x14+c` (N2's output, C1's input): the specification suggests a new
  field `crsmincal.noise_in[c]` in `phy.h`; this task may change only
  `open/phy`. Because N3 calls C1 right after N2, and the channel change
  clears the bytes before any later reading writes them again, the values are
  handed over as an argument (`bcm4360_phy_crs_min_pwr_cal_noise`, prototype in
  the new `open/phy/phy_noise.h`). `bcm4360_phy_crs_min_pwr_cal_acphy(phy, 0)`
  has no caller in the open code; it uses 0 as input (the state after a
  channel change). If `pi_ac+0x14..` is ever needed elsewhere, the field
  should be added.
* The third core's noise history (`pi+0xf14`) is not kept (no field, no
  reader on this card).
