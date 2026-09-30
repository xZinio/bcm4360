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
| `sub_0a602f` | 0x0a602f | 2784 | assigned: `wlc_phy_setup_femctrl_acphy` |
| `sub_09db6d` | 0x09db6d | 531 | assigned: `wlc_phy_femctrl_bandsel_map_acphy` |
| `sub_09dd80` | 0x09dd80 | 183 | assigned: `wlc_phy_femctrl_map_swctrl_acphy` |
| `sub_09de37` | 0x09de37 | 183 | assigned: `wlc_phy_femctrl_map_txrx_acphy` |
| `sub_09deee` | 0x09deee | 183 | assigned: `wlc_phy_femctrl_map_gpio_acphy` |
| `sub_09dfa5` | 0x09dfa5 | 191 | assigned: `wlc_phy_femctrl_map_extra_acphy` |
| `sub_09e064` | 0x09e064 | 197 | assigned: `wlc_phy_femctrl_map_b_acphy` |
| `sub_09e129` | 0x09e129 | 197 | assigned: `wlc_phy_femctrl_map_c_acphy` |
| `sub_09e1ee` | 0x09e1ee | 197 | assigned: `wlc_phy_femctrl_map_d_acphy` |
| `sub_09e2b3` | 0x09e2b3 | 197 | assigned: `wlc_phy_femctrl_map_e_acphy` |
| `sub_0a13ec` | 0x0a13ec | 202 | assigned: `wlc_phy_femctrl_write_tbl8_acphy` |
| `sub_0a14b6` | 0x0a14b6 | 334 | assigned: `wlc_phy_femctrl_rfctrl_ovrd_acphy` |
| `sub_0a5fae` | 0x0a5fae | 80 | assigned: `wlc_phy_femctrl_default_entry_acphy` |
| `sub_0a5ffe` | 0x0a5ffe | 49 | assigned: `wlc_phy_femctrl_tbl_stride_acphy` |
| `wlc_phy_set_analog_tx_lpf` | 0x09d93d | 560 | original |
| `wlc_phy_set_analog_rx_lpf` | 0x09d65e | 735 | original |
| `wlc_phy_set_tx_afe_dacbuf_cap` | 0x09d44f | 527 | original |
| `sub_08f2e9` | 0x08f2e9 | 235 | assigned: `wlc_phy_rxgain_bt_wlan_ovrd_acphy` |
| `wlc_phy_populate_recipcoeffs_acphy` | 0x09b8e1 | 771 | original (also `acphy-chanspec.md` annex A1) |
| `wlc_phy_calc_extra_init_gain_acphy` | 0x097f10 | 502 | original |
| `wlc_phy_rfctrl_override_rxgain_acphy` | 0x098373 | 796 | original |
| `wlc_phy_lpf_hpc_override_acphy` | 0x098106 | 621 | original |
| `wlc_phy_dig_lpf_override_acphy` | 0x090c25 | 934 | original |
| `wlc_phy_set_femctrl_bt_wlan_ovrd_acphy` | 0x08f90c | 132 | original |
| `wlc_phy_stop_bt_toggle_acphy` | 0x09091c | 136 | original |

(Offsets and sizes were taken from the function index; names in `assigned:` form
are proposed by this analyst - see `re-out\analysis\names\acphy-rxgain.tsv` for
the confidence of each. `sub_0a6b0f` keeps the name given by `acphy-init` /
`acphy-radio`.)

## Overview

This specification covers the board- and calibration-dependent set-up that the
AC-PHY does around the receive front end, driven from the PHY initialisation
and, for a few functions, from the band change and from debug iovars. It is
reached as follows (INIT = the channel function running with `pi_ac+0x32c` set,
`acphy-chanspec.md` section 5):

* `sub_0a6b0f` (section 1) runs once per PHY init (channel function step 15,
  INIT only), right after the register-reset `sub_0a0be4`. It sets bit 1 of
  `PHY(0x19e)` (freeze the RF sequencer) for the whole body and calls, in
  order: the front-end control set-up `sub_0a602f` (section 2), the analog
  transmit LPF `wlc_phy_set_analog_tx_lpf` (section 4), the DAC-buffer cap
  `wlc_phy_set_tx_afe_dacbuf_cap` (section 6), the analog receive LPF
  `wlc_phy_set_analog_rx_lpf` three times (section 5), a block of static
  `TBL(0x07)` entries and one `TBL(0x10)` array.
* `sub_0a602f` fills the front-end control table `TBL(0x0a)` (the values the RF
  sequencer drives on the board's switch/LNA/PA control lines) according to the
  SROM variable `femctrl`. On the emulated card (`femctrl` = 3) this is
  `sub_09db6d`; the code distinguishes `femctrl` 0..10 and two storage formats
  (section 2). It also programs a few ChipCommon/PMU/GPIO lines.
* The analog LPF and DAC-buffer functions (sections 4-6) each edit fields of
  the RF-sequence table `TBL(0x07)`; they are also called from the bandwidth
  set-up `sub_09eaf9` (`acphy-chanspec.md` annex A2).
* On a band change, `sub_09e378` (`acphy-chanspec.md` annex A3) calls
  `sub_08f2e9` (section 7), which lowers two receive-gain fields while
  Bluetooth is active.
* The remaining functions (sections 8-12) are the Bluetooth/WLAN front-end
  override and the receive-gain / LPF override paths used by debug iovars, not
  by the normal channel/init flow.

The receive **gain control proper** (splitting a wanted gain into the six gain
stages, the clip levels, the desense) is `sub_09af05` and its relatives and
belongs to `acphy-desense.md` (`acphy-chanspec.md` annex A5-A7). The channel
function's own order is in `acphy-chanspec.md` section 5; the encode helper
`sub_08f086` is section 9 there.

## Data

### Structure fields used here (`pi`, `pi_ac`)

All `pi_ac` femctrl/boardflags fields are set at attach (`acphy-attach.md`
section on the SROM); repeated here for convenience:

| Field | Size | Meaning | Emulated card |
|---|---|---|---|
| `pi+0x164` | u32 | PHY revision | 1 |
| `pi_ac+0x342` | u8 | SROM `femctrl` | 3 |
| `pi_ac+0x343` | u8 | boardflags3 bits 0..2 (femctrl sub-selector) | 0 |
| `pi_ac+0x34b` | u8 | boardflags bit 0 (gates the `CC(0x28)` tail) | 1 |
| `pi_ac+0x34c` | u8 | boardflags3 bit 8 (switch maps from SROM) | 0 |
| `pi_ac+0x358..0x3a7` | 4x(5x4) | `swctrlmap_2g/ext_2g/5g/ext_5g[0..4]` (only if `+0x34c`) | 0 |
| `pi_ac+0x8e1` | u8 | set by `sub_0a14b6` (1 on PHY rev 0, else 0); purpose unverified | - |
| `pi_ac+0x339/0x33a/0x33b` | u8 | RC-cal results (`acphy-radio.md` section 7); defaults 0x80, 0x80, 0x0c | as default |

### Table 0a-femctrl3 (front-end LUT for `femctrl` = 3, all sub-selectors)

`TBL(0x0a)`, 8-bit entries, three 32-entry blocks at offsets 0x00 / 0x20 /
0x40. The emulated card uses sub-selector 0. Bytes 0x00..0x0f of every
sub-0/1 block are 0; the meaningful 16 bytes are 0x10..0x1f. Sub-2/3 blocks
have no leading zeros (the pattern repeats every 16 bytes). Bytes shown in hex:

```
sub cVar=0 (boardflags3 bits0..2 = 0; the emulated card):
 off 0x00 (.rodata+0x2ccc40): 00*16  02 04 03 0b 02 04 03 0b 02 24 03 2d 02 24 03 2d
 off 0x20 (.rodata+0x2ccc20): 00*16  02 01 06 0e 02 01 06 0e 02 21 06 2d 02 21 06 2d
 off 0x40 (.rodata+0x2ccc00): 00*16  04 01 06 0e 04 01 06 0e 04 21 06 2b 04 21 06 2b
sub cVar=1:
 off 0x00 (.rodata+0x2ccca0): 00*16  08 04 03 08 08 04 03 08 08 24 03 25 08 24 03 25
 off 0x20 (.rodata+0x2ccc80): 00*16  08 01 06 08 08 01 06 08 08 21 06 25 08 21 06 25
 off 0x40 (.rodata+0x2ccc60): 00*16  08 01 06 08 08 01 06 08 08 21 06 23 08 21 06 23
sub cVar=2 (one block written to all three offsets, .rodata+0x2cccc0):
   02 04 03 02 02 04 03 02 22 24 23 25 22 24 23 25  (repeated to fill 32 bytes)
sub cVar=3:
 off 0x00 (.rodata+0x2ccce0): 04 01 06 04 04 01 06 04 24 21 26 23 24 21 26 23  (repeated)
 off 0x20 (.rodata+0x2ccd00): 02 01 06 02 02 01 06 02 22 21 26 25 22 21 26 25  (repeated)
 off 0x40 (.rodata+0x2ccd20): 02 04 03 02 02 04 03 02 22 24 23 25 22 24 23 25  (repeated)
```

For cVar 0 and 1, `CC(0x28)` is first set with `mod(0xfffffd, 0x10)` (keep bit
1, force value 0x10). Then `sub_0a602f`'s tail adds bit 24 (rev 1), giving
`CC(0x28)` = 0x01000010.

### Constant blocks of other `femctrl` values (32 x 8 bit each)

```
femctrl 2, cVar 0, off 0x20 (.rodata+0x2ccb80): 00 00 50 10 00 00 50 10 00 80 00*7  00 00 06 02 00 00 06 02 00 01 00*6
femctrl 2, cVar 1, off 0x20 (.rodata+0x2ccba0): 00 00 30 20 00 00 30 20 00 80 00*7  40 40 46 42 40 40 46 42 40 41 40*6
femctrl 2, cVar 2, off 0    (.rodata+0x2ccbe0): 06 06 04 06 06 06 06 06 06 07 06 06 06 06 06 06  (repeated 32 bytes)
femctrl 2, cVar 2, off 0x20/0x40 (.rodata+0x2ccbc0): 02 02 00 02 02 02 02 02 02 03 02 02 02 02 02 02  (repeated)
femctrl 5, off 0x20 (.rodata+0x2cd0a0): 00 00 50 40 00 00 50 40 00 20 00*7  80 80 86 82 80 80 86 82 80 81 80*6
```
(In `femctrl` 2/5, offsets 0 and 0x40 that are not listed get the default
block from `sub_0a5fae`, or all-zero for the `femctrl` 5 offset 0x40.)

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

### 2. sub_0a602f (.text+0x0a602f, name assigned: wlc_phy_setup_femctrl_acphy)

Purpose: fill the front-end control table `TBL(0x0a)` with the byte/word
pattern that the RF sequencer drives on the board's switch/LNA/PA control
lines. Which pattern is written is selected by the SROM variable `femctrl`
(`pi_ac+0x342`) and a sub-selector (`pi_ac+0x343` = boardflags3 bits 0..2).
Called once per PHY initialisation from `sub_0a6b0f` step 2. Input: `pi`.
No result.

State read: `pi_ac+0x342` (`femctrl`), `pi_ac+0x343` (boardflags3 bits 0..2),
`pi_ac+0x34b` (boardflags bit 0), `pi_ac+0x34c` (boardflags3 bit 8), `pi+0x164`
(PHY revision), `pi+0x20`->`+0x18` (`sih` for the `si_*` calls).

**The table `TBL(0x0a)`** (my alias "femctrl LUT"; the RF sequencer reads it,
indexed by its internal state, to obtain the control-line values; the exact
bit-to-pin mapping is board wiring and is *not* verified here). Two storage
formats are used, depending on `femctrl`:

* Format A (`femctrl` 1, 2, 3, 5, 6, and the default): **8-bit** entries in
  three consecutive blocks of 32 at offsets 0x00, 0x20 and 0x40 (96 entries).
  Interpretation of the three blocks (per core / per antenna path) is
  unverified; each of `sub_09db6d`, `sub_0a5fae`, `sub_0a13ec` and the inline
  cases write them.
* Format B (`femctrl` 4, 7, 8, 9, 10): **16-bit** entries, one transfer per
  entry, over the whole index range 0..0xff (0..0x13f for `femctrl` 10). The
  code holds a short *sparse list* of `(index, value)` records; while walking
  the index 0,1,2,... it writes the record's value when the running index
  equals the record's index (records must be in ascending index order) and 0
  for every index not in the list. See `sub_09dd80` for the shape.

Steps:

1. s = `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)` (freeze the RF
   sequencer, as in every table-writing helper here).
2. If `pi_ac+0x34c` (boardflags3 bit 8 - "switch control maps come from the
   SROM") is non-zero: the `femctrl` switch is **skipped**. For PHY revision
   2, 5 or 6 a loop then builds `TBL(0x0a)` (128 8-bit entries per core, offsets
   0/0x20/0x40) from the SROM arrays `swctrlmap*`/`swctrlmapext*` at
   `pi_ac+0x358..0x3a7`, gated by `PHY(0x40a)` bit 9 and `PHY(0x419)` bit 1
   (not detailed - other revisions). **For PHY revision 0 and 1 (the 4360)
   this loop is not entered: nothing is written to `TBL(0x0a)` at all**; go to
   step 4. (So a 4360 board that sets boardflags3 bit 8 gets no femctrl table
   from this function.)
3. Otherwise switch on `femctrl` = `pi_ac+0x342`:

   | `femctrl` | Action |
   |---|---|
   | 0 | nothing |
   | 1, or > 10 (default) | `mod(PHY(0x40a), 0x0100, 0x0100)`; `sub_0a5ffe(pi)` (writes the default block to offsets 0, 0x20, 0x40) |
   | 2 | `mod(PHY(0x40a), 0x0100, 0x0100)` is **not** done; see below (uses sub-selector `pi_ac+0x343`) |
   | 3 | `mod(PHY(0x40a), 0x0100, 0)`; `si_pmu_regcontrol(sih, 0, 4, 4)` (`PMU_REGCTL[0]` bit 2 = 1); `sub_09db6d(pi, pi_ac)` (section 3) |
   | 4 | only PHY revision 2/5/6 (see note); **nothing on the 4360** |
   | 5 | `sub_0a5fae(pi, 0)`; `TBL(0x0a)[0x20]` (32 x 8 bit) = `.rodata+0x2cd0a0`; `TBL(0x0a)[0x40]` (32 x 8 bit) = all 0; `sub_0a14b6(pi, 8, 0xc0)` |
   | 6 | `TBL(0x0a)[0]`, `[0x20]`, `[0x40]` (32 x 8 bit each) = a block that is all 0 except entries `[0x12]`=6, `[0x13]`=2, `[0x16]`=6, `[0x17]`=2, `[0x19]`=1 (same block written three times) |
   | 7 | `sub_09dfa5(pi)` (Format B, 28 records) |
   | 8 | Format B fill of `TBL(0x0a)[0..0xff]` (16 bit) from 12 records (indices `.rodata+0x2cd160`, values `.rodata+0x2cd140`): index 0xa1->9, 0xa2->0xc, 0xa3->9, 0xa6->0xc, 0xa7->9, 0xa9->9, 0xb1->0x41, 0xb2->9, 0xb3->0x41, 0xb6->9, 0xb7->0x41, 0xb9->0x41; all other indices 0 |
   | 9 | only if `pi_ac+0x343` = 1: Format B fill of `TBL(0x0a)[0..0xff]` from 8 records: index 2->0x80, 3->0, 9->4, 0x12->0x40, 0x13->0, 0x19->3, 0x82->8, 0xc0->8; all other 0. (`pi_ac+0x343` != 1: nothing) |
   | 10 | by `pi_ac+0x343`: 0 -> `sub_09e064`, 1 -> `sub_09e129`, 2 -> `sub_09e1ee`, 4 -> `sub_09e2b3` (Format B over 0..0x13f; other sub-selector values: nothing) |

   **`femctrl` = 2** (sub-selector cVar = `pi_ac+0x343`):
   * cVar = 0: `sub_0a5fae(pi, 0)`; `TBL(0x0a)[0x20]` (32 x 8 bit) =
     `.rodata+0x2ccb80`; `sub_0a5fae(pi, 0x40)`.
   * cVar = 1: `sub_0a5fae(pi, 0)`; `TBL(0x0a)[0x20]` = `.rodata+0x2ccba0`;
     `sub_0a5fae(pi, 0x40)`.
   * cVar = 2: `sub_0a13ec(pi)` (section 3).
   * Then, always: `sub_0a14b6(pi, cVar == 0 ? 0 : 4, 0xa0)`.
   * Then, only if cVar = 0: `si_gpioout(sih, 0x10, 0, 0)`;
     `si_gpioouten(sih, 0x10, 0x10, 0)`; `si_gpiocontrol(sih, 0x10, 0, 0)`
     (drive GPIO 4 low as an output under CPU control).

4. Tail (reached by every path, including the boardflags3-bit-8 skip): if
   `pi_ac+0x34b` (boardflags bit 0) is non-zero and the PHY revision is 0 or 1:
   `si_corereg(sih, 0, 0x28, mask, val)` with (mask, val) = (0x00000004,
   0x00000004) for revision 0, (0x01000000, 0x01000000) for revision 1. This is
   `mod(CC(0x28), mask, val)`: sets bit 2 (rev 0) or bit 24 (rev 1) of
   ChipCommon register 0x28 (purpose of `CC(0x28)` unverified; the same
   register is touched by `sub_09db6d` and `sub_0a14b6`).
5. `mod(PHY(0x19e), 0x0002, s & 0x0002)` (restore).

Note - the default block written by `sub_0a5fae` (used by the default case and
`femctrl` 2, 5): 32 bytes, all 0 except `[2]`=4, `[6]`=4, `[9]`=1, `[0x12]`=4,
`[0x16]`=4, `[0x19]`=2.

### 3. Helpers of sub_0a602f

All write `TBL(0x0a)` while `PHY(0x19e)` bit 1 is already set by `sub_0a602f`.
Format A helpers write 8-bit entries; Format B helpers write 16-bit entries.

`sub_09db6d(pi, pi_ac)` (.text+0x09db6d, name assigned:
`wlc_phy_femctrl_bandsel_map_acphy`) - the `femctrl` = 3 path. By the
sub-selector cVar = `pi_ac+0x343` it writes three 32-byte blocks (8-bit) to
`TBL(0x0a)` offsets 0, 0x20, 0x40 from constant arrays, and for two sub-cases
sets `CC(0x28)`:

| cVar | `CC(0x28)` first | Offset 0 block | Offset 0x20 block | Offset 0x40 block |
|---|---|---|---|---|
| 0 | `mod(0xfffffd, 0x10)` (= keep bit 1, force 0x10) | `.rodata+0x2ccc40` | `.rodata+0x2ccc20` | `.rodata+0x2ccc00` |
| 1 | `mod(0xfffffd, 0x10)` | `.rodata+0x2ccca0` | `.rodata+0x2ccc80` | `.rodata+0x2ccc60` |
| 2 | - | `.rodata+0x2cccc0` | `.rodata+0x2cccc0` | `.rodata+0x2cccc0` |
| 3 | - | `.rodata+0x2ccce0` | `.rodata+0x2ccd00` | `.rodata+0x2ccd20` |
| other | - | return, nothing written |||

The block contents (each 32 bytes; bytes 0..15 are 0 in every cVar=0/1/2/3
block except the cVar=2/3 blocks which have no leading zeros). Written on the
4360 (cVar=0): see "Data / Table 0a-femctrl3". The `mod(CC(0x28),0xfffffd,0x10)`
keeps only bit 1 of the old value and forces the register to 0x10.

`sub_0a5fae(pi, offset)` (.text+0x0a5fae, assigned
`wlc_phy_femctrl_default_entry_acphy`): writes the 32-byte default block (see
the note above) to `TBL(0x0a)[offset]` (32 x 8 bit).

`sub_0a5ffe(pi)` (.text+0x0a5ffe, assigned `wlc_phy_femctrl_tbl_stride_acphy`):
`sub_0a5fae(pi, 0)`, `sub_0a5fae(pi, 0x20)`, `sub_0a5fae(pi, 0x40)`.

`sub_0a13ec(pi)` (.text+0x0a13ec, assigned `wlc_phy_femctrl_write_tbl8_acphy`;
`femctrl` 2, cVar 2): `TBL(0x0a)[0]` (32 x 8 bit) = `.rodata+0x2ccbe0`;
`TBL(0x0a)[0x20]` = `TBL(0x0a)[0x40]` = `.rodata+0x2ccbc0`; then
`si_gpioout(sih, 8, 8, 0)`, `si_gpioouten(sih, 8, 8, 0)` (drive GPIO 3 high as
an output).

`sub_0a14b6(pi, mode, gpiomask)` (.text+0x0a14b6, assigned
`wlc_phy_femctrl_rfctrl_ovrd_acphy`; called by `femctrl` 2 and 5): sets up the
RF-control override registers then the GPIOs:
1. `mod(PHY(0x40a), 0x0100, 0x0100)`; `PHY(0x414)` = 0x0555 (plain write);
   `mod(CC(0x28), 0x0008, 0x0008)`; `mod(PHY(0x418), 0x003c, (mode << 2) & 0x3fc)`
   (bits 2..5 = `mode` & 0xf).
2. PHY revision 0: `si_gpiocontrol(sih, 0xffff, 0xe0, 0)` (GPIO 5..7 to chip
   control); `sub_093ebe(pi, 0xb, 0)` (`acphy-init`); `pi_ac+0x8e1` = 1;
   `mod(PHY(0x418), 0x0001, 0)`; `mod(PHY(0x418), 0x0002, 0x0002)`.
   PHY revision 1+: `pi_ac+0x8e1` = 0; `mod(PHY(0x40a), 0x0200, 0x0200)`.
3. `si_gpioout(sih, gpiomask, 0, 0)`; `si_gpioouten(sih, gpiomask, gpiomask,
   0)`; `si_gpiocontrol(sih, gpiomask, 0, 0)` (drive the masked GPIOs low as
   CPU-controlled outputs).

`sub_09dd80` (.text+0x09dd80, assigned `wlc_phy_femctrl_map_swctrl_acphy`),
`sub_09de37` (+0x09de37, `..map_txrx..`), `sub_09deee` (+0x09deee,
`..map_gpio..`): Format B over 0..0xff, records are 4 bytes `[index:u8][pad][value:u16]`;
called only by `femctrl` = 4 (PHY revision 2/5/6 only - not reached on the
4360). Record tables and counts: `sub_09dd80` `.rodata+0x2ccd60`, 60 records;
`sub_09de37` `.rodata+0x2cce50`, 72; `sub_09deee` `.rodata+0x2ccf70`, 60.

`sub_09dfa5` (.text+0x09dfa5, assigned `wlc_phy_femctrl_map_extra_acphy`;
`femctrl` = 7): Format B over 0..0xff; 28 records, indices at `.rodata+0x2cd100`
(u16 each), values at `.rodata+0x2cd0c0` (u16 each).

`sub_09e064`, `sub_09e129`, `sub_09e1ee`, `sub_09e2b3` (.text+0x09e064,
+0x09e129, +0x09e1ee, +0x09e2b3; assigned `wlc_phy_femctrl_map_b/c/d/e_acphy`;
`femctrl` = 10 by sub-selector 0/1/2/4): Format B over **0..0x13f** (320
entries); records are separate `u16` index and `u16` value arrays:

| Function | Records | Index array | Value array |
|---|---|---|---|
| `sub_09e064` (sub 0) | 43 | `.rodata+0x2cd1e0` | `.rodata+0x2cd180` |
| `sub_09e129` (sub 1) | 43 | `.rodata+0x2cd2a0` | `.rodata+0x2cd240` |
| `sub_09e1ee` (sub 2) | 43 | `.rodata+0x2cd360` | `.rodata+0x2cd300` |
| `sub_09e2b3` (sub 4) | 45 | `.rodata+0x2cd420` | `.rodata+0x2cd3c0` |

(Contents of the Format B record tables are constant arrays of the object at
the addresses given; they can be dumped with `python blob.py data <addr>`. They
are not written on the emulated card - `femctrl` = 3 - so they were not
transcribed here; they are needed only for boards with `femctrl` 4/7/10.)

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

### 7. sub_08f2e9 (.text+0x08f2e9, name assigned: wlc_phy_rxgain_bt_wlan_ovrd_acphy)

Purpose: set two 4-bit "initial-gain / clip" fields (per antenna path) that are
lowered while Bluetooth is transmitting, so the WLAN receiver does not
overload. Inputs: `pi`, `bt` (bool, `param_2`). No hardware access other than
the four `phy_reg_mod` below. Reads only `pi+0x164` (PHY revision).

PHY revision 0 and 1 (the 4360): with n1 = (bt ? 2 : 4) and n0 = (bt ? 0 : 4):

1. `mod(PHY(0x2d1), 0x00f0, n1 << 4)`.
2. `mod(PHY(0x2d1), 0x0f00, n0 << 8)`.
3. `mod(PHY(0x2d2), 0x00f0, n1 << 4)`.
4. `mod(PHY(0x2d2), 0x0f00, n1 << 8)` (note: the last field uses n1, **not**
   n0 - the decompiler reuses the variable; verified against the trace, see
   Verification).

So for `bt` = 0: `PHY(0x2d1)` and `PHY(0x2d2)` both become 0x0440 (in these
fields); for `bt` = 1: `PHY(0x2d1)` = 0x0020, `PHY(0x2d2)` = 0x0220. (PHY
revision 3: a single different `mod(PHY(0x2d1), ...)` pair; revisions 2, 4, 5,
6: the function returns without doing anything - not the 4360.)

Callers: `sub_09e378` (annex A3 step 10, with `bt` = `pi+0xfa2`), and
`wlc_phy_btc_adjust_acphy` (Bluetooth coexistence). The names of `PHY(0x2d1)`
and `PHY(0x2d2)` are unknown (aliases: "BT-gated initial gain", unverified).

### 8. wlc_phy_set_femctrl_bt_wlan_ovrd_acphy and wlc_phy_stop_bt_toggle_acphy

`wlc_phy_set_femctrl_bt_wlan_ovrd_acphy(pi, v)` (.text+0x08f90c, name original):
forces or releases the BT/WLAN priority override of the front-end control in
`PHY(0x418)` bits 0 (override value) and 1 (override enable). Steps:

1. `wlapi_suspend_mac_and_wait(sh+0x20)`.
2. By `v` (signed byte):
   * `v` = 1: `mod(PHY(0x418), 0x0001, 0x0001)`; `mod(PHY(0x418), 0x0002, 0x0002)`
     (enable the override, value 1).
   * `v` = 0: `mod(PHY(0x418), 0x0001, 0)`; `mod(PHY(0x418), 0x0002, 0x0002)`
     (enable the override, value 0).
   * `v` = anything else (the callers pass -1): `mod(PHY(0x418), 0x0002, 0)`;
     `mod(PHY(0x418), 0x0001, 0)` (release: both bits 0).
3. `wlapi_enable_mac(sh+0x20)`.

Callers: `wlc_phy_set_femctrl_bt_wlan_ovrd` (an iovar wrapper) and
`wlc_phy_stop_bt_toggle_acphy`.

`wlc_phy_stop_bt_toggle_acphy(pi)` (.text+0x09091c, name original): decides
whether to force the override on. It acts **only** if boardflags bit 22
(`sh+0x64` & 0x400000) is set and `pi_ac+0x32e` = 0xff (-1). Then:

1. m = chain mask from boardflags bit 23: `sh+0x64` & 0x800000 set -> 1, else 2.
2. `wlc_phyreg_enter()`.
3. v = -1 if (m & `sh+0xa6`) != 0 or (m & `sh+0xa7`) != 0, else 1 (`sh+0xa7` =
   `phyrxchain`; `sh+0xa6` = the other chain mask compared in `acphy-chanspec`
   section 5 step 30). `wlc_phy_set_femctrl_bt_wlan_ovrd_acphy(pi, v)`.
4. `wlc_phyreg_exit(pi)`.

**On the emulated card boardflags = 0x10401001, so bit 22 is clear and this
function does nothing** (which is why it makes no accesses in the traces).

### 9. wlc_phy_populate_recipcoeffs_acphy (.text+0x09b8e1, name original)

Reciprocity (Tx/Rx phase) coefficients written to `TBL(0x11)`. Fully described
in `acphy-chanspec.md` **annex A1** (the sine table S[], the per-`rpcal`
quadrant formula, the 448-entry fill of `TBL(0x11)`); I read the decompiled
function and confirm annex A1 is correct for PHY revision 0 and 1. Not repeated
here. Called from `sub_0a4adc` (annex A1 step 11). (PHY revision 3 instead
writes 0x40 entries of `TBL(0x1e)`, 32 bit, each = the byte replicated -
`acphy-chanspec` does not cover it; not the 4360.)

### 10. Receive gain override (wlc_phy_calc_extra_init_gain_acphy, wlc_phy_rfctrl_override_rxgain_acphy)

These two implement the "rx gain override" that a debug/iovar path
(`sub_0b7b26`, `sub_0c0f88`) uses to force the analog receive gain of each
core. **They are not on the initialisation/channel/band path and do not appear
in the bring-up or channel traces** (so the values below are from the code, not
verified against a trace). N = `pi+0x168` (2). "For each core c" uses stride
o = 0x200 * c.

`wlc_phy_calc_extra_init_gain_acphy(pi, want, out)` (.text+0x097f10, name
original): distributes an extra receive gain `want` (8 bit) over the gain
stages of each core, from the current initial-gain code words. No register
writes to the card except the table read. Steps:

1. s = `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`; read `TBL(0x07)`
   offset 0xf9, N entries of 16 bit into g[0..N-1]; restore `PHY(0x19e)` bit 1.
2. For each core c: from the word g[c] compute avail = min(4, 4 - ((g>>6)&0xf))
   clamped to >=0, plus 4, plus max(0, (10 - ((g>>10)&7)) - (g>>13)); if avail <
   `want`, set `want` = avail (so `want` is clamped to the smallest headroom
   over the cores).
3. If `want` != 0, for each core c: split `want` across the stages of word g[c]
   and write six bytes to `out + 6*c`: `[0]` = g&7, `[1]` = (g>>3)&7, `[2]` =
   (extra to the mixer stage) + ((g>>6)&0xf), `[3]` = (extra to the next stage)
   + ((g>>10)&7), `[4]` = the running LNA-gain index, `[5]` = min(`want`,4). The
   split takes up to 4 from the first pool, then from the LNA (limited by
   10 - existing), etc. (The exact clamp order is in the code; it produces the
   six per-core gain codes consumed by section 10's second function.)

`wlc_phy_rfctrl_override_rxgain_acphy(pi, mode, codes, save)` (.text+0x098373,
name original): applies or restores the per-core gain override registers.
`codes` = the six-byte-per-core array from the previous function; `save` = an
8-byte-per-core scratch array (4 words per core).

* `mode` = 1 (restore): for each core c: `PHY(0x722+o)` = save[c].w0,
  `PHY(0x730+o)` = save[c].w1, `PHY(0x731+o)` = save[c].w2, `PHY(0x734+o)` =
  save[c].w3 (plain writes).
* `mode` = 0 (apply): s = `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`; for
  each core c:
  1. read `PHY(0x722+o)`, `PHY(0x730+o)`, `PHY(0x731+o)`, `PHY(0x734+o)` into
     save[c] (4 words).
  2. read `TBL(0x15)` offset `0x18*c + (2.4 GHz ? 5 : 0xd)` (1 x 8 bit) into
     e0, and offset `0x18*c + 0x16` (1 x 8 bit) into e1.
  3. `PHY(0x730+o)` = (codes[c][5] & 0x3f) << 10 | codes[c][2] << 6 |
     codes[c][1] << 3 | codes[c][0].
  4. `PHY(0x731+o)` = (e1 >> 3) << 4 | ((e0 >> 3) & 0x0f).
  5. `PHY(0x734+o)` = codes[c][3] | codes[c][4] << 3.
  6. `mod(PHY(0x722+o), 0x0002, 0x0002)`, `mod(.., 0x0004, 0x0004)`,
     `mod(.., 0x0008, 0x0008)` (three writes; enable the override).
  7. read back `PHY(0x730+o)`, `PHY(0x731+o)`, `PHY(0x734+o)`.
  Then restore `PHY(0x19e)` bit 1.

The band is taken from `pi+0x17e` bits 14..15 (step 2). Registers `PHY(0x722)`
(override enable), `PHY(0x730/0x731/0x734)` (gain code words) per core - names
unverified.

### 11. wlc_phy_lpf_hpc_override_acphy (.text+0x098106, name original)

Overrides the receive low-pass-filter "hpc" fields per core. Iovar/debug path
(`sub_0c0f88`); not in the traces. `pi_ac+0x308` = the override-active flag; the
saved values live at `pi_ac + 0x30a + 4*c` (two words per core, my offsets).

* `param_2` = 0 (restore): `pi_ac+0x308` = 0; for each core c: `PHY(0x723+o)` =
  saved word A[c], `PHY(0x735+o)` = saved word B[c] (plain writes).
* `param_2` != 0 (apply): `pi_ac+0x308` = 1; s = `PHY(0x19e)`; `mod(PHY(0x19e),
  0x0002, 0x0002)`; read `TBL(0x07)[0x122]` -> lo, `TBL(0x07)[0x125]` -> hi;
  restore `PHY(0x19e)` bit 1. For each core c:
  1. read `PHY(0x723+o)` and `PHY(0x735+o)` into the save area.
  2. `mod(PHY(0x723+o), 0x0004, 0x0004)`.
  3. `mod(PHY(0x735+o), 0x00e0, ((hi >> (4*c)) & 0x0f) << 5)`.
  4. `mod(PHY(0x723+o), 0x0002, 0x0002)`.
  5. `mod(PHY(0x735+o), 0x001e, ((lo >> (4*c)) & 0x0f) << 1)`.

(Where `TBL(0x07)[0x122]` supplies nibble c for the 0x1e field and
`TBL(0x07)[0x125]` for the 0xe0 field. Registers `PHY(0x723/0x735)` per core -
names unverified.)

### 12. wlc_phy_dig_lpf_override_acphy (.text+0x090c25, name original)

Overrides the ten digital-LPF coefficient registers `PHY(0x18b)` .. `PHY(0x194)`
(0x18b, 0x18c, 0x18d, 0x18e, 0x18f, 0x190, 0x191, 0x192, 0x193, 0x194). Iovar/
debug path (`sub_0c0f88`); not in the traces. Save area `pi+0xf6e` .. `pi+0xf80`
(10 words), flag `pi+0xf82`.

* `param_2` = 0 (restore): if `pi+0xf82` != 0: write the ten registers from the
  save area; `pi+0xf82` = 0.
* `param_2` != 0: if `pi+0xf82` = 0: read the ten registers into the save area,
  `pi+0xf82` = 1. Then:
  * `param_2` = 1: copy the bandwidth filter coefficients into the override
    registers: `PHY(0x181)`->`PHY(0x18b)`, `PHY(0x182)`->`PHY(0x18c)`,
    `PHY(0x183)`->`PHY(0x18d)`, `PHY(0x184)`->`PHY(0x18e)`, `PHY(0x185)`->
    `PHY(0x18f)`, `PHY(0x186)`->`PHY(0x190)`, `PHY(0x187)`->`PHY(0x191)`,
    `PHY(0x188)`->`PHY(0x192)`, `PHY(0x189)`->`PHY(0x193)`, `PHY(0x18a)`->
    `PHY(0x194)` (ten read/write pairs; these are the per-bandwidth
    coefficients that annex A2 step 2 writes to `PHY(0x181..0x18a)`).
  * `param_2` = 2: write `PHY(0x18b)` = 0x02d4, `PHY(0x190)` = 0x02d4, and the
    other eight registers = 0.

## Verification

Checked against the emulator (model: chip 0x4360 rev 3, PHY rev 1, radio 2069
rev 4, board 0x117, `femctrl` = 3, boardflags = 0x10401001, boardflags3 = 0).

* **`sub_0a602f` / `sub_09db6d` (`femctrl` = 3, sub 0)** in the bring-up trace
  `re-out\up-trace.txt`: the switch clears `PHY(0x40a)` bit 8, sets
  `PMU_REGCTL[0]` bit 2 (`CC(0x658)/(0x65c)`), then `sub_09db6d`:
  `mod(CC(0x28), 0xfffffd, 0x10)` = 0x10, then 96 8-bit writes to `TBL(0x0a)`
  offsets 0x00..0x5f matching `.rodata` blocks 0x2ccc40 / 0x2ccc20 / 0x2ccc00
  byte for byte. The tail then does `mod(CC(0x28), 0x1000000, 0x1000000)` =
  0x01000010 (PHY rev 1). All confirmed.
* **`femctrl` = 2** - own run (`--srom` with `femctrl=2`): exactly 96 8-bit
  `TBL(0x0a)` entries; offset 0x20 = `.rodata+0x2ccb80` (00 00 50 10 00 00 50 10
  00 80 ...) as specified; offsets 0 and 0x40 = the `sub_0a5fae` default block.
* **`femctrl` = 8** - own run: exactly 256 16-bit `TBL(0x0a)` entries over
  0x00..0xff, all 0 except the 12 records (0xa1->9, 0xa2->0xc, 0xa3->9, 0xa6->
  0xc, 0xa7->9, 0xa9->9, 0xb1->0x41, 0xb2->9, 0xb3->0x41, 0xb6->9, 0xb7->0x41,
  0xb9->0x41). Confirms Format B and the constant record tables.
* **`sub_08f2e9`** in the bring-up trace (called from the band change with
  `bt` = 0, PHY rev 1): `PHY(0x2d1)` = 0x0440, `PHY(0x2d2)` = 0x0440 - confirms
  the last field uses n1 (=4), not n0.
* `wlc_phy_populate_recipcoeffs_acphy`: read the decompiled function; annex A1
  of `acphy-chanspec.md` is correct for PHY rev 0/1.
* Sections 10-12 (the rx-gain / LPF / dig-LPF overrides) are **not** exercised
  by bring-up or channel changes (iovar/debug only), so they are from the code
  and disassembly, not verified against a trace.

## Open questions / not yet done

* Register/table *purposes* not verified: `PHY(0x2d1)/(0x2d2)` (BT-gated gain),
  `PHY(0x418)` bits 0/1 (BT/WLAN femctrl override), `PHY(0x722/0x730/0x731/
  0x734)`, `PHY(0x723/0x735)`, `PHY(0x18b..0x194)`, and above all `CC(0x28)`
  (ChipCommon register 0x28, set to 0x10/0x01000010 here; likely a chip-level
  front-end/GPIO mux control - the header name was not found in `src\`). The
  bit-to-pin mapping of `TBL(0x0a)` on the board is not known.
* The real card's `femctrl` is unknown (the emulated card uses 3). All values
  0..10 are described; the constant record tables of `femctrl` 4/7/10 are large
  and were left as `.rodata` addresses + record counts + format rather than
  transcribed (see section 3), because the card does not use them; an
  implementer can dump them with `blob.py data`.
* `femctrl` = 4 and the boardflags3-bit-8 (`swctrlmap` from SROM) path are
  effective only on PHY revisions 2/5/6; on the 4360 (rev 0/1) they write
  nothing to `TBL(0x0a)`. This was read from the code (the revision guard) but
  not exercised (the model is rev 1 with `femctrl` = 3).
* `pi_ac+0x8e1` (set by `sub_0a14b6`) and `pi_ac+0x308` (LPF-hpc override active)
  purposes are inferred from context only.
* Radio major revision != 0 branch of `sub_0a6b0f` step 9 (five extra
  `wlc_phy_set_analog_tx_lpf` calls) is not reached on the 4360's radio (major
  rev 0) and was not detailed.
