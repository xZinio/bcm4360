# Questions from implementing `phy-rxgain`

Implementer notes on where I had to guess, where a specification disagreed with
a test, and what I did. Task `docs/re/tasks/phy-rxgain.md`; code in
`open/phy/phy_rxgain.c` (additions to `open/include/bcm4360/phy.h`).

## 1. `TBL(0x14)[0x33]` top word (annex A2, `sub_09eaf9` step 11) - disagreement

`acphy-chanspec.md` annex A2 step 11 gives the 48-bit value written to
`TBL(0x14)[0x33]` as `0x00000084e800` (20 MHz), `0x000000844800` (40 MHz),
`0x000000860800` (80 MHz), and the note at the end of A2 says the function
"depends on nothing... channel". The 20 and 40 MHz values match the trace. The
**80 MHz** value does not: the top 16-bit word (bits 32..47) is written as
`0x8080` or `0x0000` depending on the channel and on whether the call is an
initialisation:

| Call | Channel (centre) | `get_chan_freq_range` | top word |
|---|---|---|---|
| channel change | 42 (36/80) | 1 | 0x8080 |
| channel change | 106 (100/80) | 3 | 0x0000 |
| channel change | 155 (157/80) | 4 | 0x8080 |
| initialisation | 42 (36/80) | 1 | 0x0000 |

(The low 32 bits are `0x00860800` in every case.) I determined the sub-band
values with a diagnostic that encoded `get_chan_freq_range` into the top word.
A read-modify-write that keeps the old cell was ruled out: reading the entry
first gives the *hardware* top word (0 at the point of the 36/80 change), not
the `0x8080` the object writes, so the `0x8080` comes from the object's own
state, not from the table.

What I did (observed behaviour wins, `implementing.md`): for 80 MHz I write the
top word as `0x8080` when the call is **not** an initialisation
(`pi_ac+0x32c` = 0) **and** `get_chan_freq_range` is 1 or 4 (the outer 5 GHz
sub-bands), else `0x0000`; 20 and 40 MHz keep the spec values.

Open points for the analysts:
- The real source of the top word is unknown (an internal shadow, or a value
  computed in another step). My rule fits the four observed cases but was not
  derived from the object's code.
- The inner sub-band `get_chan_freq_range` = 2 is never a 80 MHz channel in the
  test set, so whether it is `0x8080` or `0x0000` is untested; I treat it as
  `0x0000` (grouping 2 and 3 together, i.e. the DFS band). Please confirm.
- I used "initialisation" (`pi_ac+0x32c`) as the discriminator, but the tested
  init calls are also band changes, so the real condition could be "band
  change" instead of "init"; the two are indistinguishable in the test set.
- The physical reading (outer/non-DFS sub-bands 0x8080, inner/DFS 0x0000) is a
  guess.

## 2. Tables loaded by name with `hw_fw_data()`

The specification names these Broadcom tables; I load them by name:

- `acphy_txv_for_spexp` (section 1 step 8, `TBL(0x10)[0x4c4]`, 243 x 32 bit).
- `rx_farrow_tbl`, `tx_farrow_dac1_tbl` (annex A1 step 2, resampler; 3 blocks of
  123 entries of six 16-bit words). I search block `b` (bandwidth index) for the
  channel and use words 2..5, and the same index in `tx_farrow_dac1_tbl` (chosen
  because `pi_ac+0x000` = 1). This reproduces the "skip if the channel is not in
  the table" behaviour without needing the channel list. All test channels are
  present, so the skip path was not exercised.
- `acphy_txgain_*` (annex A3 step 3, `TBL(0x20)`, 128 x 48 bit), selected by
  band / `extpagain` / radio revision / `boardflags3` bits 4..6.

These names all resolved in the test's `hw_fw_data`; the runs pass, so the names
are correct for the emulated card. `acphy_txgain_epa_2g_2069rev4_id1` (2.4 GHz,
radio rev 4, boardflags3 bits4..6 = 1) is not exercised (the default board is
femctrl 3 with those bits 0).

## 3. `femctrl` values other than 3

Only `femctrl` = 3 is exercised by the channel function in the tests (the
`femctrl` = 5 board variables are used only by the attach scenarios, which do
not run the channel function). I implemented the dispatcher for all values the
specification distinguishes:

- 3 (the emulated card, `sub_09db6d`): full, all four sub-selectors from
  "Table 0a-femctrl3"; verified.
- 0, 1/default (`sub_0a5ffe`), 6, 8, 9: from the data the spec gives; **not**
  exercised.
- 2, 5: the constant blocks the spec gives (`.rodata` 0x2ccb80 / 0x2ccba0 /
  0x2ccbe0 / 0x2ccbc0 / 0x2cd0a0), plus `sub_0a14b6` / `sub_0a13ec`; **not**
  exercised. The `femctrl` 2 constant blocks in the spec's "Constant blocks"
  section seem to be one byte too long when the `00*7` / `00*6` runs are counted
  literally (33 bytes for a 32-byte block); I placed the meaningful bytes as
  best I could without overflowing the 32-byte block, but these blocks are
  unverified.
- 4, 7, 10: not implemented. `femctrl` 4 and 10 need no code on PHY revision 0/1
  (4360); `femctrl` 7 and 10 use large `.rodata` Format-B record tables that the
  spec left as addresses + counts and did not transcribe. These paths are not
  reachable on the 4360, so I left a comment.

## 4. State fields added to `phy.h`

I added, all zero after attach (never set by the current attach code):

- `bt_active` (`pi+0xfa2`): argument of `sub_08f2e9` on the band change. 0 in
  the traces (Bluetooth inactive), so the observed `PHY(0x2d1)`/`PHY(0x2d2)` =
  0x0440 is reproduced.
- `acphy_1169` (`pi+0x1169`): the 40 MHz resampler override of annex A1 step 3
  (an iovar only sets it). 0 by default, so step 3 is never entered.
- `acphy_116a` (`pi+0x116a`): only written by annex A1; read by out-of-scope
  code. Kept for faithfulness.
- `lpf_hpc_ovr_active`/`lpf_hpc_ovr_save` (`pi_ac+0x308`/`0x30a`) and
  `dig_lpf_ovr_save` (`pi+0xf6e`): save areas for the debug overrides of
  sections 11 and 12.

## 5. Debug/iovar overrides (sections 10, 11, 12) - not trace-verified

`wlc_phy_calc_extra_init_gain_acphy`, `wlc_phy_rfctrl_override_rxgain_acphy`,
`wlc_phy_lpf_hpc_override_acphy`, `wlc_phy_dig_lpf_override_acphy` are on the
iovar/debug path and do not appear in the bring-up or channel traces, so they
are implemented from the specification only and are **not** verified against a
trace. In particular `wlc_phy_calc_extra_init_gain_acphy` step 3 (the split of
the extra gain across the stages) is described in the spec as "the exact clamp
order is in the code"; I implemented the gain split from the six output bytes
the spec lists and a plausible clamp order, but the exact order is a guess.

## 6. Annex A1 step 9 (carrier-sense calibration state) skipped

Annex A1 step 9 clears several `pi_ac` byte ranges (`0x014`, `0x02c`, `0x03c`,
`0x03d`, `0x043`) and calls `wlc_phy_crs_min_pwr_cal_acphy` (a desense leaf, not
yet implemented). The byte clears are memory-only (no register/table/service
access) and touch fields of the still-stubbed desense group, so skipping step 9
does not change any compared access. Noted so the desense task adds it.
