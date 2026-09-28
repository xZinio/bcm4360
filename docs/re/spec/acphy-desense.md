# Desense, carrier sense thresholds and interference mitigation of the AC-PHY (BCM4360)

Status: work in progress, written function by function. Every procedure below
was read in the decompiler output and checked in the disassembly; what was
also compared with emulator traces and experiments is listed in
"Verification". Register *names* are not known; aliases in parentheses are mine.

Register access notation: `access.md`. The channel function that calls most
of the functions of this area: `acphy-chanspec.md`, section 5 (steps 20 to
24).

**Division of work with `acphy-chanspec.md`.** The annexes A4 to A7 of that
specification describe the receive gain control and the application of the
desense as the channel function runs them: A4 `sub_092efb`, `sub_0909fd`; A5
`sub_09af05`, `sub_099658`, `sub_0998cc`; A6 `sub_09a121`, `sub_099f29`,
`sub_090493`, `sub_09175a`, `sub_091b6e`; A7 `sub_09a539`, `sub_08f84d`,
`sub_090b77`. These descriptions are not repeated here. Section R of
"Procedures" says what of them was checked again for this specification,
and corrects and completes them; where section R and the annex differ,
section R is right. Likewise `acphy-init.md` (section 3a, H11, appendix A) and
`acphy-attach.md` (procedures 9 to 11) describe the set-up functions of this
area; section R completes them.

## Scope

Chip 0x4360 (0x4352, 43460 = 0xa9c4 and 43526 = 0xaa06 take the same
branches unless said otherwise), MAC core revision 42, AC-PHY revision 0 or 1,
radio 2069 revision 3 or 4 (major revision 0), two chains.

| Function | .text offset | Size | Name |
|---|---|---|---|

(The table grows with the text.)

## Overview

(To be completed.)

## Data

### Structure fields

The complete list is in `re-out\analysis\fields\acphy-desense.tsv`.

| Field | Meaning |
|---|---|
| `pi_ac+0x656 .. 0x65e` (9 x u8) | desense values that were applied last (what the hardware is set to), layout below |
| `pi_ac+0x65f .. 0x667` (9 x u8) | desense values used when the channel has no interference record (all 0 after attach) |
| `pi_ac+0x668 .. 0x670` (9 x u8) | desense values wanted ("total"), computed by `sub_0909fd`, layout below |
| `pi_ac+0x8b0` (u32), `pi_ac+0x8b4 .. 0x8bc` (9 x u8) | switch and desense values of the Bluetooth coexistence |

Layout of a set of desense values (9 bytes; the names are mine):

| Byte | Name | Meaning |
|---|---|---|
| 0 | `ofdm_desense` | reduction of the OFDM sensitivity in dB |
| 1 | `bphy_desense` | reduction of the 802.11b sensitivity in dB |
| 2 | `lna1_tbl_desense` | number of entries taken off the top of the gain table of stage 1 (first internal LNA) |
| 3 | `lna2_tbl_desense` | the same for stage 2 (second internal LNA) |
| 4 | `lna1_gainlmt_desense` | number of entries of the gain limit table of stage 1 that are blocked |
| 5 | `lna2_gainlmt_desense` | the same for stage 2 |
| 6 | `elna_bypass` | the external LNA is bypassed |
| 7 | `nf_hit_lna12` | correction in dB added to the clip thresholds (`sub_090493`) |
| 8 | `on` | desense is in force |

In the set of applied values the bytes are written by different functions:
bytes 0 and 1 by `sub_09a539`, bytes 2 and 3 by `sub_099658`, bytes 4 and 5 by
`sub_0998cc`, byte 6 by `sub_09a121`; bytes 7 and 8 are never written.

## Procedures

`mod(reg, mask, value)` is read-modify-write and always writes. "For each core
c" means c = 0 .. N-1 in rising order (N = `pi+0x168` = 2), o = 0x200 * c.
2.4 GHz means (`pi+0x17e` & 0xc000) = 0, everything else is 5 GHz.

### R. Review of the annexes A4 to A7 of `acphy-chanspec.md`

#### R1. A5: sub_09af05, sub_099658, sub_0998cc

Read again in the disassembly and run in the emulator with other inputs (see
"Verification"): **A5 is right as it is written.** Additions:

* `sub_0998cc(pi, stage)` treats every `stage` other than 1 as 2 (there is no
  test for 2). `sub_099658(pi, stage)` returns without doing anything for
  stages other than 1 and 2.
* In `sub_099658` entry 0 gets the gain T[1] and the code 1 also when m = 0;
  then all other entries have the gain T[0] and the code 0 (checked with
  desense values above h).
* "Channel number below 100" in step 3.1 means: low byte of `pi+0x17e` <= 99.
  The three values at `pi_ac+0x3f8 + 3*c` (SROM values for the middle of the
  5 GHz band) are not used by any function of the PHY.
* The values that the four fields "d is remembered in" hold are bytes 2 to 5
  of the set of applied desense values at `pi_ac+0x656` (see "Data").
* What step 3.5 reads at initialisation (the code of entry 0 of stage 0, the
  codes of stage 3, gains and codes of the stages 4 and 5:
  `TBL(0x44 + 0x20*c)[0x60..0x67]`, `[0x70..0x77]` and the same of
  `TBL(0x45 + 0x20*c)`) is never written by the driver before: it is what the
  PHY holds after reset. **These values are not in the object and not known**
  (the model of the card returns 0 for them). They are inputs of `sub_08f086`
  and of everything computed from its result (start gain, clip gains, the
  signal strength correction of the channel function).
* `code[0][1]` (second entry of the external LNA stage) is never written or
  read from the hardware; it stays 0.

## Verification

(To be completed; see the end of the file.)

## Open questions

(To be completed.)
