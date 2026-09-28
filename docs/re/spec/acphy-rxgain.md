# AC-PHY of the BCM4360: front end control, analog filters, board dependent tables

Status: WORK IN PROGRESS. Sections are added function by function; what is
not yet written is listed at the end under "Open questions / not yet done".
Register *names* are not known; aliases in parentheses are mine.

## Scope

Register access notation: `access.md`. The channel function `sub_0a7089` and
the three set-up functions `sub_0a4adc`, `sub_09eaf9`, `sub_09e378` that call
the functions below: `acphy-chanspec.md`. SROM variables and the fields they
are stored in: `acphy-attach.md`.

Chip 0x4360 (0x4352 and 43526 = 0xaa06 take the same branches unless said
otherwise), AC-PHY revision 0 or 1, radio 2069 revision 3 or 4 (major revision
0), two cores.

| Function | .text offset | Size | Name |
|---|---|---|---|
| `sub_0a6b0f` | 0x0a6b0f | 1402 | assigned: `wlc_phy_set_tbl_on_reset_acphy` (name of `acphy-init`/`acphy-radio`) |

(more rows follow as the functions are described)

## Overview

(to be written)

## Data

(to be written)

## Procedures

`mod(reg, mask, value)` is read-modify-write and always writes. "For each core
c" means c = 0 .. N-1 in rising order (N = `pi+0x168` = 2).

### 1. sub_0a6b0f (.text+0x0a6b0f, name assigned: wlc_phy_set_tbl_on_reset_acphy)

Purpose: the part of the PHY initialisation that depends on the board and on
the results of the RC calibration: front end control, analog filters, entries
of the RF sequence table. Called once per PHY initialisation by the channel
function (its step 15, only when INIT), directly after `sub_0a0be4`. Input:
`pi`. No result.

State read: `sh+0x68` (boardflags2), `pi+0x164` (PHY revision), `pi+0x16e`
(radio major revision), `pi+0x17e` (chanspec), `pi_ac+0x339`, `pi_ac+0x33a`,
`pi_ac+0x33b` (results of the RC calibration, `acphy-radio.md` section 7;
defaults 0x80, 0x80, 0x0c).

Steps:

1. s = `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`.
2. `sub_0a602f(pi)`: front end control (section 2).
3. `wlc_phy_set_analog_tx_lpf(pi, 0x1ff, -1, -1, -1, pi_ac+0x339, pi_ac+0x33a, -1)`.
4. `wlc_phy_set_tx_afe_dacbuf_cap(pi, 0x1ff, pi_ac+0x33b, -1, -1)`.
5. g = `pi_ac+0x339` (unsigned 8 bit). PHY revision 0 and 1: g20 = (g * 0xdd)
   >> 8, g40 = (g * 0xd7) >> 8 (both 8 bit); (other revisions: g20 = g40 = g).
   Then three calls:
   `wlc_phy_set_analog_rx_lpf(pi, 1, -1, -1, -1, g20, pi_ac+0x33a, -1)`,
   `wlc_phy_set_analog_rx_lpf(pi, 2, -1, -1, -1, g40, pi_ac+0x33a, -1)`,
   `wlc_phy_set_analog_rx_lpf(pi, 4, -1, -1, -1, g40, pi_ac+0x33a, -1)`.
6. `TBL(0x07)` (16 bit entries), one transfer each, in this order:

   | Offset | Entries | Values |
   |---|---|---|
   | 0x20 | 16 | 0x04, 0x03, 0x06, 0x05, 0x02, 0x01, 0x08, 0x2a, 0x2b, 0x0f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f |
   | 0x90 | 16 | 0x0c, 2, 2, 4, 4, 6, 1, 4, 1, 2, 1, 1, 1, 1, 1, 1 |
   | 0x121 | 2 | 0x0aaa, 0x0aaa |
   | 0x131 | 2 | 0x0aaa, 0x0aaa |
   | 0x124 | 2 | 0x0222, 0x0222 |
   | 0x137 | 2 | 0x0222, 0x0222 |
   | 0x00 | 16 | 0x00, 0x01, 0x02, 0x08, 0x05, 0x00, 0x06, 0x03, 0x0f, 0x04, 0x00, 0x35, 0x0f, 0x00, 0x36, 0x1f |
   | 0x3c6 | 1 | 0x0020 |
   | 0x3c7 | 1 | 0x0020 |
   | 0x3d6 | 1 | 0x0020 |
   | 0x3d7 | 1 | 0x0020 |
   | 0x3e6 | 1 | 0x0020 |
   | 0x3e7 | 1 | 0x0020 |

   (PHY revisions 2, 5, 6 write other values at offset 0 and 0x70 of
   `TBL(0x07)`, set bit 15 of `PHY(0x413)` and bit 9 of `PHY(0x40f)` and write
   an entry of `TBL(0x14)` instead of the row "0x00": not described.)
7. Only if the board has the flag for the band of `pi+0x17e`: bit 20
   (0x00100000) of boardflags2 on 2.4 GHz, bit 21 (0x00200000) on 5 GHz:
   `TBL(0x07)[0x80]` (16 bit) = 0x0078.
8. PHY revision 0 and 1: `TBL(0x10)` (32 bit entries), one transfer of 243
   entries to offset 0x4c4: the array `acphy_txv_for_spexp` of the object
   (972 bytes; see "Data").
9. (Radio major revision other than 0 only: five calls of
   `wlc_phy_set_analog_tx_lpf`. Not executed for the 4360's radio.)
10. `mod(PHY(0x19e), 0x0002, s & 0x0002)`.

### 2. sub_0a602f (front end control)

(to be written)

### 4. wlc_phy_set_analog_tx_lpf (.text+0x09d93d, name original)

Purpose: change fields of the settings of the analog low-pass filter of the
transmitter. The settings are kept in the RF sequence table `TBL(0x07)`: one
word of 25 bit per core and per "mode" (nine modes, 0..8), split over two 16
bit entries.

Inputs: `pi`; `mode_mask` (16 bit; bit m set = mode m is changed); five field
values as signed 32 bit numbers, a negative value (the callers pass -1) means
"leave the field as it is"; `core` (signed 32 bit; -1 = all cores, otherwise
only the core with this index). Order of the arguments: `(pi, mode_mask, f0,
f6, f3, f9, f17, core)`, where the fields are (names in parentheses are my
aliases, by analogy with the receive filter; unverified):

| Argument | Position in the argument list | Bits of the word | Mask |
|---|---|---|---|
| f0 (`bq0_bw`) | 3rd | 0..2 | 0x0000007 |
| f6 (`bq1_bw`) | 4th | 6..8 | 0x00001c0 |
| f3 (`rc_bw`) | 5th | 3..5 | 0x0000038 |
| f9 (`gmult`) | 6th | 9..16 | 0x001fe00 |
| f17 (`gmult_rc`) | 7th (first on the stack) | 17..24 | 0x1fe0000 |

Table entries (16 bit each) of mode m and core c: low part (bits 0..15 of the
word) `TBL(0x07)[0x142 + 0x10*c + m]`, high part (bits 16..24)
`TBL(0x07)[0x362 + 0x10*c + m]`.

Steps:

1. s = `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`.
2. For each core c (skipped if `core` is not -1 and differs from c), for m =
   0 .. 8 in rising order, if bit m of `mode_mask` is set:
   1. Read the low part (a transfer of one entry), then the high part (a
      transfer of one entry); w = high << 16 | low.
   2. For every field whose argument is not negative: w = (w & 0x1ffffff &
      ~mask) | (argument << first bit of the field). The argument is not
      limited to the width of the field.
   3. Write the low part = w & 0xffff, then the high part = (w >> 16) & 0x1ff
      (two transfers of one entry). The write is made even if nothing changed.
3. `mod(PHY(0x19e), 0x0002, s & 0x0002)`.

Callers:

| Caller | `mode_mask` | Fields given |
|---|---|---|
| `sub_0a6b0f` step 3 (initialisation) | 0x1ff | f9 = `pi_ac+0x339`, f17 = `pi_ac+0x33a` (results of the RC calibration; defaults 0x80, 0x80) |
| `sub_09eaf9` step 5 (bandwidth) | 0x100 | f6 = f3 = 3 for 20 MHz, 4 for 40 MHz, 5 for 80 MHz (PHY revisions above 1: 5, 5, 6) |
| `sub_0a6b0f` step 9 (radio major revision other than 0 only) | 0x002, 0x004, 0x010, 0x020: f6 = f3 = 5; 0x080: f6 = f3 = 6 | |

Meaning of the modes (interpretation, unverified; from the use of mode 8 for
the current bandwidth and of the groups of three): 0..2 = 20 MHz (11b, 11n,
11a/g/ac frames), 3..5 = the same for 40 MHz, 6 = 11b in 80 MHz, 7 = all other
frames in 80 MHz, 8 = playback of samples (test tones of the calibrations).

### 5. wlc_phy_set_analog_rx_lpf (.text+0x09d65e, name original)

Purpose: the same for the analog low-pass filter of the receiver: a word of
25 bit per core and per bandwidth.

Inputs: `pi`; `mode_mask` (8 bit; bit 0 = 20 MHz, bit 1 = 40 MHz, bit 2 = 80
MHz; the higher bits are not looked at); five field values, negative = "leave
as it is"; `core` (-1 = all). Order of the arguments: `(pi, mode_mask, f0,
f3, f14, f6, f17, core)`:

| Argument | Position in the argument list | Bits of the word | Mask |
|---|---|---|---|
| f0 (`bq0_bw`) | 3rd | 0..2 | 0x0000007 |
| f3 (`bq1_bw`) | 4th | 3..5 | 0x0000038 |
| f14 (`rc_bw`) | 5th | 14..16 | 0x001c000 |
| f6 (`gmult`) | 6th | 6..13 | 0x0003fc0 |
| f17 (`gmult_rc`) | 7th (first on the stack) | 17..24 | 0x1fe0000 |

Table entries (16 bit) of core c:

| Mode | Low part | High part |
|---|---|---|
| 0 (20 MHz) | `TBL(0x07)[0x140 + 0x10*c]` | `TBL(0x07)[0x360 + 0x10*c]` |
| 1 (40 MHz) | `TBL(0x07)[0x141 + 0x10*c]` | `TBL(0x07)[0x361 + 0x10*c]` |
| 2 (80 MHz) | `TBL(0x07)[0x441 + 2*c]` | `TBL(0x07)[0x440 + 2*c]` |

Steps: exactly as in section 4 (save and set bit 1 of `PHY(0x19e)`; per core
and per mode 0..2 of the mask: read low, read high, replace the fields that
are given, write low, write high = (w >> 16) & 0x1ff; restore bit 1 of
`PHY(0x19e)`).

Only caller: `sub_0a6b0f` step 5, three calls with the masks 1, 2, 4, each
with f6 and f17 given.

### 6. wlc_phy_set_tx_afe_dacbuf_cap (.text+0x09d44f, name original)

Purpose: set the capacitor code of the buffer behind the DAC (6 bit fields in
the RF sequence table, one per core and mode; the same nine modes as in
section 4).

Inputs: `pi`; `mode_mask` (16 bit, bits 0..8); `cap` (signed 32 bit, negative
= leave; bits 0..4 of the field); `fixed` (signed 32 bit, negative = leave;
bit 5 of the field; alias mine); `core` (-1 = all). Order of the arguments:
`(pi, mode_mask, cap, fixed, core)`.

Place of the field of mode m and core c: entry `TBL(0x07)[B(c) + O(m)]` (16
bit), bits S(m) .. S(m)+5, with B = 0x3f0, 0x060, 0x0d0 for core 0, 1, 2 and

| m | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| O(m) | 0x0b | 0x0b | 0x0c | 0x0c | 0x0e | 0x0e | 0x0f | 0x0f | 0x0a |
| S(m) | 0 | 6 | 0 | 6 | 0 | 6 | 0 | 6 | 0 |

Steps:

1. s = `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`.
2. For each core c (skipped if `core` is not -1 and differs from c), for m =
   0 .. 8, if bit m of `mode_mask` is set:
   1. v = the entry (read, one entry); f = (v >> S) & 0x3f.
   2. If `cap` >= 0: f = (f & 0x20) | `cap` (not limited to 5 bit).
      If `fixed` >= 0: f = (f & 0x1f) | (`fixed` << 5).
   3. S = 0: new = f | (v & 0x0fc0). S = 6: new = (v & 0x003f) | (f << 6). (16
      bit. Bits 12..15 of the entry are written as 0 when f fits into 6 bit.)
   4. Write the entry = new (one entry).
3. `mod(PHY(0x19e), 0x0002, s & 0x0002)`.

Because the modes 2k and 2k+1 share an entry, a call with both bits set reads
and writes that entry twice.

Only caller: `sub_0a6b0f` step 4: mask 0x1ff, `cap` = `pi_ac+0x33b` (result of
the RC calibration, default 0x0c), `fixed` = -1, all cores.

## Verification

(to be written)

## Open questions / not yet done

(to be written)
