# Questions from implementing phy-txpower

Implementer notes for the analysts. Format: what the spec says, what the test
shows, what I did.

## 1. `wlc_phy_tx_tone_acphy` / `wlc_phy_stopplayback_acphy` are unspecified

- Spec: `acphy-txpower.md` section 24 (`sub_0af7f4`) steps 9 and 12 call
  `wlc_phy_tx_tone_acphy(pi, 2000, a, 0, 0, 0)` (a = 0 for the idle measurement)
  and `wlc_phy_stopplayback_acphy(pi)`, both labelled "specification
  `acphy-cal-tx`". There is no `acphy-cal-tx.md` in `docs/re/spec/`.
- Test: the idle-TSSI measurement runs inside `wlc_phy_init` (compared stage),
  so the tone's accesses are part of the comparison; nothing is excluded now.
- What I did: reconstructed the amplitude-0 tone and stopplayback accesses from
  the object trace shown by the comparison test (the one place I worked from
  observed behaviour, not a specification). For the idle measurement they are:

  `phy_tx_tone` (amplitude 0):
  1. read the bbmult of each core (`sub_098751`);
  2. set the bbmult of each core to 0 (`sub_09c4e4`);
  3. `wlc_phy_resetcca_acphy`;
  4. `PHY(0x471)` = 0 (read+write), `PHY(0x463)` = 0, `PHY(0x461)` = 0xffff,
     `PHY(0x462)` = 0x003c (plain writes);
  5. save `PHY(0x400)`; set bit 0 of `PHY(0x400)`; `PHY(0x460)` read+write
     unchanged; clear bit 0 of `PHY(0x460)`; `PHY(0x382)` read+write unchanged;
     set bit 0 of `PHY(0x460)`; read `PHY(0x403)`; restore `PHY(0x400)`.

  `phy_stopplayback`: read `PHY(0x464)`; `PHY(0x460)` read+write; set each
  core's bbmult to 0 (`sub_09c4e4`); `wlc_phy_resetcca_acphy`.

  The values written to `PHY(0x461)` = 0xffff and `PHY(0x462)` = 0x003c (= 60)
  were the same for every band in the exercised session (2.4 GHz 20 MHz, 5 GHz
  40 and 80 MHz), and the readings are 0 in the model, so no band-dependent
  sample count or sample-buffer table write appeared. This is very likely
  because the tone amplitude is 0; a real `acphy-cal-tx` spec is still needed
  for non-zero amplitudes (the calibrations). The exact register semantics
  (which are read-modify-write vs plain, which bits) are guesses that reproduce
  the observed records; only the trace, not a register description, backs them.

## 2. Band-change wiring of `sub_08f9b4` and `sub_09c161`

- Spec: `acphy-chanspec.md` annex A3 (`sub_09e378`, band change) calls
  `sub_08f9b4(pi, 0)` (step 6) and `sub_09c161` with 56 zero bytes (step 9).
  These belong to `acphy-txpower` (my task).
- Code: `open/phy/phy_rxgain.c`
  (`bcm4360_phy_set_regtbl_on_band_change_acphy`) had these two steps as empty
  "not yet implemented" placeholders. My leaf functions are the only callers on
  the band-change path, so with nothing excluded their accesses would be
  missing there (the band change runs at every INIT and every band change, so
  the accesses are in the compared `wlc_phy_init` / `wlc_phy_chanspec_set`
  windows).
- What I did: wired the two calls at the placeholders in phy_rxgain.c:
  `bcm4360_phy_tssi_phy_setup_acphy(phy, 0)` at step 6 and
  `bcm4360_phy_txcal_coeffs_apply_acphy(phy, NULL)` (NULL = 56 zero bytes) at
  step 9. This is a two-line change to an existing `.c` file, made because the
  task ("nothing excluded, all 20 scenarios 0") cannot be met otherwise and the
  placeholders are explicitly reserved for these functions.

## 3. `sub_09949f` / `sub_098949` are not exercised by the scenarios

- The target-power recalculation `sub_09949f` (and thus the estimated-power
  table set-up `sub_098949`) is called by `sub_0b8ca4`
  (`wlc_phy_txpower_limit_set`, wlc_phy_cmn.c), which runs *after*
  `wlc_phy_chanspec_set` returns (a sibling call of `wlc_bmac_set_chanspec`),
  not inside it, and is not a compared PHY-API stage. So the estimated-power
  tables are implemented from the spec but not validated by `ab.py`.
- What I did: implemented `sub_098949` exactly as section 13 gives it
  (fixed-point polynomial, no floating point, 32-bit divisions), matching the
  worked examples in the spec's "Verification" row.

## 4. State fields added

- `pi+0x216` (highest target power per core, s8) had no field in
  `struct bcm4360_phy`. `sub_098949` step 6 reads it. Added
  `target_max[]` to `struct bcm4360_phy_txpwr`. It is set by out-of-scope
  phy-cmn code (`sub_0b8ca4`), so it stays 0 here.
