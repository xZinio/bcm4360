# The BCM2069 radio of the BCM4360: power, RC calibrations, preferred values, channel tuning

Status: all procedures of the sections "Procedures" 1 to 19 were read in the
decompiler output and checked in the disassembly; what was also checked against
an emulator trace is listed in "Verification". Register *names* are not known;
aliases in parentheses are mine.

## Scope

Radio 2069 with major revision 0 (radio revisions 3 and 4) behind an AC-PHY of
revision 0 or 1, chip 0x4360. Wherever the object selects a register address
by chip id, the chips 0x4352, 0x4360, 0xa9c4 (43460) and 0xaa06 (43526) take the
same branch, so everything below holds for these four; other chips (other radio
major revisions) use other addresses and are not described.

Register access primitives (`phy_reg_*`, `*_radio_reg`, write pacing) are
specified in `access.md` and not repeated here.

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_phy_anacore` (AC-PHY path) | 0x0baad1 | 292 | original |
| `wlc_phy_switch_radio` (AC-PHY path) | 0x0ba302 | 586 | original |
| `wlc_phy_switch_radio_acphy` | 0x0aa782 | 4266 | original; contains the inlined RCAL |
| `sub_09fb72` | 0x09fb72 | 2384 | assigned: `wlc_phy_radio2069_pwron_seq` |
| `wlc_phy_init_radio_prefregs_allbands` | 0x0b623e | 65 | original |
| `sub_09591e` | 0x09591e | 2277 | assigned: `wlc_phy_radio2069_rccal` |
| `si_pmu_rfldo` | 0x01e7e8 | 51 | original |
| `sub_08e9b1` | 0x08e9b1 | 902 | assigned: `wlc_phy_chan2freq_acphy` |
| `sub_0a7089`, radio tuning part only | 0x0a7089 | 14073 | `wlc_phy_chanspec_set_acphy` (guide); the part is an inlined function, assigned: `wlc_phy_chanspec_radio2069_setup` |
| `wlc_2069_rfpll_150khz` | 0x09576e | 432 | original |
| `sub_09311b` | 0x09311b | 459 | assigned: `wlc_phy_radio2069_vcocal` |
| `sub_093e47` | 0x093e47 | 119 | assigned: `wlc_phy_radio2069_vcocal_wait` |
| `sub_096203` | 0x096203 | 2974 | assigned: `wlc_phy_radio2069_afecal` |
| `sub_0940f8` | 0x0940f8 | 1995 | assigned: `wlc_phy_tssi_radio_setup_acphy` |
| `sub_09e378`, radio part only | 0x09e378 | 1921 | assigned: `wlc_phy_set_regtbl_on_band_change_acphy` |
| `sub_0a0be4` | 0x0a0be4 | 2056 | assigned: `wlc_phy_set_reg_on_reset_acphy` (no radio access, see 17) |
| `wlc_phy_radio_override_acphy` | 0x096da1 | 323 | original, not called by anything in the object |
| `wlc_phy_radio2069_pwrup_seq` | 0x091466 | 284 | original, not called by anything in the object |
| `wlc_phy_radio2069_pwrdwn_seq` | 0x091582 | 472 | original, not called by anything in the object |
| `wlc_phy_lp_mode`, `wlc_phy_force_lpvco_2G` | 0x08ed37, 0x08ed84 | 77, 41 | original, no effect on PHY rev 0/1 |
| `sub_08fb39`, `sub_09027d` | 0x08fb39, 0x09027d | 1860, 260 | assigned: `wlc_phy_radio2069_mini_pwron_seq_rev16`, `..._rev32`; not reached on the 4360 |
| parts of `wlc_phy_attach` (radio id), `wlc_phy_attach_acphy` (OTP word 16, flags), `sub_0b018f` (regulator), `wlc_phy_init`, `wlc_bmac_radio_hw`, `wlc_coredisable` | 0x0be426, 0x0a194f, 0x0b018f, 0x0babf5, 0x063e2a, 0x06378d | | only what concerns the radio |

## Overview

Supplies and switches, from the outside in:

1. RF LDO of the PMU: bit 1 of `PMU_REGCTL[0]` (1 = off). `si_pmu_rfldo`.
2. Analog core of the PHY: `D11(0x3e6)`. `wlc_phy_anacore`.
3. Radio power-up signals, which the PHY drives and the driver can override
   through PHY registers 0x72x/0x73x (per core) and 0x408/0x416/0x417.
   `wlc_phy_switch_radio_acphy`.
4. A regulator setting taken from OTP word 16: bits 20..24 of `PMU_REGCTL[0]`.
   Written by the PHY initialisation (`sub_0b018f`).

Order of events as seen in the traces:

* `wlc_attach`: `wlc_phy_attach` reads the PHY version (`D11(0x3e0)`), runs
  `wlc_phy_attach_acphy` (saves ten PHY registers, reads OTP word 16), switches the
  analog core on, reads the radio id, switches the radio off. At the end of
  `wlc_bmac_attach`: `wlc_coredisable` (radio off, analog core off), then RF LDO off.
* `wlc_up` -> `wlc_bmac_init`: RF LDO on; later `wlc_phy_init`: analog core on;
  radio on (section 4: reset pulse, preferred values, fixed modifications, RCAL,
  RCCAL); then the PHY initialisation function, which sets the regulator field
  from the OTP value, loads the PHY tables and calls the channel function for
  the current channel.
* Channel function `sub_0a7089` (at initialisation and on every channel
  change): look up the channel entry; if band or bandwidth changed pulse the PLL
  reset; write 50 radio registers from the entry; fixed patches; for 5 GHz
  channels the 150 kHz loop filter settings; start the VCO calibration; (PHY
  work of other specifications, among it on band change the radio settings of
  sections 15 and 16); if the bandwidth changed or at initialisation the AFE
  calibration; finally wait for the VCO calibration.
* The radio stays on during channel and band changes (the AC-PHY is excluded from
  the radio-off in `wlc_bmac_set_chanspec`).
* `wlc_down`: `wlc_bmac_down_finish` calls `wlc_coredisable` (radio off, then
  analog core off), then `wlc_bmac_hw_down` switches the RF LDO off.

Nothing in the frequency plan is computed at run time for this radio: all PLL
values come from the channel table.

## Data

### Radio register addresses

A radio register address has 12 bits: bits 0..8 register, bits 9..11 bank.

| Bank | Meaning |
|---|---|
| 0x000, 0x200, 0x400 | per core registers of core 0, 1, 2: address = register \| core << 9 |
| 0x400 | also holds the shared blocks the driver uses for the bandgap, RCAL and RCCAL (0x407, 0x40b, 0x40c, 0x410..0x416, 0x548..0x54c, 0x55e); they are accessed even though the board has only two chains |
| 0x600 | used by the tables for values that are the same for all cores (unverified: presumably a write to the register of every core) |
| 0x800 | PLL and other common registers |

PHY registers 0x600..0x7ff belong to core 0, the same registers of core 1 and
2 are 0x200 and 0x400 higher. On the four chips named above the driver sets
bit 12 of the address (0x1000) in some writes to these registers (unverified:
presumably the write then goes to all cores; the emulator treats 0x17xx as
registers of their own).

### Structure fields

See `re-out\analysis\fields\acphy-radio.tsv`. Used below:

| Field | Meaning |
|---|---|
| `pi+0x168` | number of cores N (2 on this board) |
| `pi+0x16c`, `pi+0x16e` | radio revision, radio major revision |
| `pi+0x17e` (u16) | chanspec the radio is tuned to: bits 0..7 channel (centre channel for 40/80 MHz), 0x3800 bandwidth (0x1000 = 20, 0x1800 = 40, 0x2000 = 80 MHz), 0xc000 band (0 = 2.4 GHz, 0xc000 = 5 GHz) |
| `pi+0xf88` (u8) | radio is on |
| `pi_ac+0x32c` (u8) | 1 while the PHY initialisation calls the channel function ("init") |
| `pi_ac+0x32d` (u8) | PHY initialisation done; cleared by `wlc_phy_init` before it switches the radio on |
| `pi_ac+0x330` (u8) | band the PHY was last set up for: 1 = 2.4 GHz |
| `pi_ac+0x334` (u32) | bandwidth bits (chanspec & 0x3800) last set up |
| `pi_ac+0x339`, `+0x33a` (u8) | RCCAL result "gmult" (both get the same value; defaults 0x80) |
| `pi_ac+0x33b` (u8) | RCCAL result for the DAC buffer (default 0x0c) |
| `pi_ac+0x345` (u8) | boardflags3 bit 3: skip RCAL, fixed value |
| `pi_ac+0x34e` (u8) | boardflags3 bit 13: skip RCAL, value from OTP |
| `pi_ac+0x34a` (u8) | boardflags bit 29 |
| `pi_ac+0x8e2` (u16) | bits 8..12 of OTP word 16 |
| `pi_ac+0x8ea..0x8fc` | ten saved PHY registers (section 19) |
| `sh+0x20` | handle of the PHY shim, argument of `wlapi_*` |
| `sh+0x64` | boardflags |
| `sh+0xf2` (u16) | RCAL value from OTP (bits 0..3 of OTP word 16) |

### Tables

All three kinds are in `.data`; extract the contents with
`python blob.py data NAME COUNT 2`.

**Preferred values** `prefregs_2069_rev3` (22 entries), `prefregs_2069_rev4`
(24 entries): array of pairs of 16 bit words {radio register address, value},
ended by an entry with address 0xffff. Selection by radio revision: 3 ->
`prefregs_2069_rev3`; 4 and 8 -> `prefregs_2069_rev4`; 5, 6, 7 and unknown
revisions: no table is written and steps 4 to 8 of section 5 are skipped
(other revisions >= 16: other tables, other chips).

**Override registers** `ovr_regs_2069_rev2`: array of 16 bit radio register
addresses ended by 0xffff, 56 addresses: 18 of core 0 (0x15d, 0x15f..0x169,
0x16d..0x171, 0x173), 16 of core 1 (0x35d, 0x35f..0x363, 0x365..0x369,
0x36e..0x371, 0x373), 17 of core 2 (0x55d..0x563, 0x565..0x569, 0x56e..0x571,
0x573), 5 common (0x95d, 0x96a, 0x96b, 0x96c, 0x972). The lists of the cores
differ, this is how the table is.

**Channel tables** `chan_tuning_2069rev3`, `chan_tuning_2069rev4`: 77 entries of
58 words of 16 bit (116 bytes) each, not sorted. Both tables have the same
channels in the same order: 184, 185, 187, 188, 189, 192, 196, 207, 208, 209,
210, 212, 216, 34..48 (even), 52..64 (even), 100..144 (even), 145..149, 151..161
(odd), 165, 1..14. The two tables differ only in words 37..42, 44..47 and 49..51.

| Word | Byte offset | Goes to |
|---|---|---|
| 0 | 0x00 | channel number (search key) |
| 1 | 0x02 | centre frequency in MHz |
| 2..51 | 0x04..0x66 | radio registers, in this order: 0x8e0, 0x8e1, 0x8dd, 0x8dc, 0x8e6, 0x8e7, 0x8c4, 0x8c5, 0x8e5, 0x8eb, 0x8d6, 0x113, 0x8db, 0x8da, 0x8d7, 0x885, 0x886, 0x887, 0x8d9, 0x8d8, 0x8c9, 0x8ca, 0x8cc, 0x8c7, 0x8c8, 0x892, 0x894, 0x895, 0x896, 0x897, 0x899, 0x89a, 0x89b, 0x89c, 0x112, 0x629, 0x65b, 0x65e, 0x668, 0x11a, 0x11b, 0x719, 0x630, 0x65c, 0x662, 0x66d, 0x893, 0x145, 0x146, 0x723 |
| 52..57 | 0x68..0x72 | `PHY(0x371)`..`PHY(0x376)`, written by `sub_0a4adc` (not part of this specification) |

Table selection by radio revision (major revision 0): revision 3 ->
`chan_tuning_2069rev3`; 4 and 8 -> `chan_tuning_2069rev4`; 5, 6, 7 and
revisions outside 3..8: no table, every lookup fails.

Observations on the contents (the driver does not compute anything; for
information; they hold for all 77 entries of both tables, tested with
`re-out\analysis\acphy-radio\chanobs.py`): with f the centre frequency in MHz
and fvco = f * 3/2 (2.4 GHz) or f * 2/3 (5 GHz): word 5 is the largest odd
integer not above fvco; words 6 (integer part) and 7 (fraction in 1/65536,
truncated) are fvco / 20; words 8 and 9 likewise fvco / 5; words 52..54 are
round(0.4 * x) and words 55..57 are round(2^20 / (0.4 * x)) for x = f + 10, f,
f - 10.

### Constants

| Constant | Value |
|---|---|
| RCAL poll | 100 times, 10 us apart, done = bit 3 of `RADIO(0x40b)` |
| RCCAL poll | 100 times, 100 us apart, done = bit 4 of `RADIO(0x413)`, for each of 3 passes |
| VCO calibration poll | 100 times, 10 us apart, done = bit 8 of `RADIO(0x90b)` |
| AFE calibration poll | 10 times, 10 us apart, per core, done = bits 0 and 1 of `RADIO(0x144 \| core << 9)` |
| RCCAL scale factor | 0xc1 (193) for radio major revision 0 |

## Procedures

`mod(reg, mask, value)` is read-modify-write and always writes. "For each core
c" means c = 0 .. N-1 in rising order. Delays are `osl_delay` in microseconds.

### 1. wlc_phy_anacore (.text+0x0baad1, name original), AC-PHY path

Inputs: `pi`, `on` (bool). Steps:

1. If the PHY revision `pi+0x164` is above 18: return.
2. PHY type 11 has no handler of its own (the function pointer `pi+0x118` is
   null): write 16 bit to `D11(0x3e6)`: 0 if `on`, else 0x00f4.

Callers: `wlc_phy_attach` (on), `wlc_phy_init` (on), `wlc_bmac_phy_reset` (on, at
its end), `wlc_coredisable` (off, after the radio), `wlc_bmac_radio_hw`.

### 2. Reading the radio id (inlined in wlc_phy_attach, .text+0x0be426)

After `wlc_phy_attach_acphy` and `wlc_phy_anacore(pi, 1)`; MAC revision >= 24
and not 27:

1. Write 16 bit 0 to `D11(0x3d8)`, read 16 bit `D11(0x3da)` -> b0.
2. Write 16 bit 1 to `D11(0x3d8)`, read 16 bit `D11(0x3da)` -> b1.
3. `pi+0x16a` (radio id) = b1; `pi+0x16c` (revision) = b0 & 0xff; `pi+0x16e`
   (major revision) = (b0 >> 4) & 0xff; `pi+0x16f` (minor) = b0 & 0xf;
   `pi+0x16d` = 0; write counter `pi+0x226` = 0.
4. Accepted ids for the AC-PHY: 0x2069 and 0x030b.
5. `wlc_phy_switch_radio(pi, 0)`.

The emulated radio answers b0 = 4 (or 3), b1 = 0x2069.

### 3. wlc_phy_switch_radio (.text+0x0ba302, name original), AC-PHY path

Inputs: `pi`, `on`.

1. Read 32 bit `D11(0x120)`; the value is not used.
2. `wlapi_update_bt_chanspec(sh+0x20, on ? pi+0x17e : 0, bit 1 of pi+0x19c, bit 2
   of pi+0x19c)`: Bluetooth coexistence notification, no hardware access in the
   traces (not part of this specification).
3. `wlc_phy_switch_radio_acphy(pi, on)`.

Callers for the AC-PHY: `wlc_phy_attach` (off), `wlc_coredisable` (off),
`wlc_phy_init` (on), `wlc_bmac_radio_hw` (both).

`wlc_phy_init` does before it switches on: `pi_ac+0x32d` = 0 and, for chips
0x4352 and 0x4360, `pi+0xf88` = 0; so the complete switch-on sequence of
section 4 runs at every PHY initialisation, and its step 6 is skipped there.

`wlc_bmac_radio_hw(wlc_hw, enable, keep_anacore)`, only if the MAC core is up:
disable = suspend the MAC, radio off, analog core off unless `keep_anacore`, and
if the chip has a PMU `si_pmu_radio_enable(sih, 0)` (does nothing on the 4360)
and set bit 5 of `D11(0x1e0)`; enable = if PMU: clear bit 5 of `D11(0x1e0)` and
`si_pmu_radio_enable(sih, 1)`; analog core on unless `keep_anacore`; radio on;
enable the MAC.

### 4. wlc_phy_switch_radio_acphy (.text+0x0aa782, name original)

Inputs: `pi`, `on`. State: `pi+0xf88`, `pi_ac+0x32d`, `pi_ac+0x345`, `pi_ac+0x34e`.

**Off** (`on` = 0), always executed:

1. `pi+0xf88` = 0.
2. Write PHY registers in this order (addresses with bit 12 set):
   0x173e = 0x1c00, 0x1739 = 0, 0x173a = 0, 0x1725 = 0x1fff, 0x1729 = 0,
   0x1721 = 0xffff, 0x1728 = 0, 0x1720 = 0x03ff.
3. `mod(PHY(0x408), 0x0002, 0)`.
4. `PHY(0x417)` = 0, `PHY(0x416)` = 1.

(Interpretation, unverified: 0x1725/0x1721/0x1720 switch overrides on, the
value registers 0x1739/0x173a/0x1729/0x1728 force the power-up signals low.)

**On** (`on` != 0): if `pi+0xf88` != 0 nothing is done. Otherwise:

1. `wlapi_suspend_mac_and_wait(sh+0x20)`.
2. `sub_09fb72(pi)` (section 5).
3. `mod(PHY(0x16b), 0x0400, 0)`; delay 3; `PHY(0x175)` = 0; delay 3.
4. RCAL (resistor calibration). If `pi_ac+0x34e` = 1 or `pi_ac+0x345` = 1 the
   measurement is skipped; for this radio (major revision 0) nothing is written
   instead (major revisions 1 and 2 write the OTP value `sh+0xf2` or a constant
   to a radio register). Otherwise:
   1. `mod(RADIO(0x8ea), 0x0040, 0x0040)`; `mod(RADIO(0x8ea), 0x0080, 0x0080)`.
   2. `mod(RADIO(0x8ed), 0x0600, 0)`; `mod(RADIO(0x8ed), 0x1800, 0)`.
   3. `mod(RADIO(0x548), 0x0001, 1)`; write 0 to `RADIO(0x549)`, `RADIO(0x54a)`,
      `RADIO(0x54b)`, `RADIO(0x54c)`.
   4. `mod(RADIO(0x40b), 0x0001, 0)`; delay 1; `mod(RADIO(0x40b), 0x0001, 1)`.
   5. Up to 100 times: delay 10, read `RADIO(0x40b)`, stop when bit 3 is set.
   6. Read `RADIO(0x40b)` once more. The value (the calibration result) is not
      used and not stored.
   7. `mod(RADIO(0x548), 0x0001, 0)`.
   8. `mod(RADIO(0x8ea), 0x0040, 0)`; `mod(RADIO(0x8ea), 0x0080, 0)`.
   9. `mod(RADIO(0x40b), 0x0001, 0)`.
5. `sub_09591e(pi)` (section 7).
6. If `pi_ac+0x32d` != 0 (the radio comes back while the PHY is initialised,
   for example through `wlc_bmac_radio_hw`): `sub_0a04c2(pi)` (PHY register
   and table initialisation, not part of this specification) and
   `sub_0a7089(pi, pi+0x17e)` (channel function).
7. `pi+0xf88` = 1.
8. `wlapi_enable_mac(sh+0x20)`.

Radio major revisions 1 and 2 do more between steps 1 and 2 (start of a
"mini PMU" in the radio, `sub_08fb39`, `sub_09027d`): not reached here. The end of
the function handles chips 0x4335 and 0x4350 only.

### 5. sub_09fb72 (.text+0x09fb72, name assigned: wlc_phy_radio2069_pwron_seq)

Purpose: reset the radio, load the preferred values, apply fixed modifications.
Input: `pi`; reads `sh+0x64` (boardflags). Steps:

1. s728 = `PHY(0x728)`; s408 = `PHY(0x408)` & 0xfc38 (two reads).
2. Write `PHY(0x415)` = 0, `PHY(0x40e)` = 0, `PHY(0x40c)` = 0x2000, `PHY(0x408)` =
   s408, `PHY(0x417)` = 0, `PHY(0x416)` = 0x000d, `PHY(0x728)` = s728 & 0x7e7f.
3. Read `PHY(0x720)`, write it back with bits 0x0180 set (a read and a write
   of their own, not `or`: the write counts for the write pacing). Then
   `PHY(0x408)` = s408; `PHY(0x408)` = s408 | 1; delay 1; `PHY(0x408)` = s408.
4. `wlc_phy_init_radio_prefregs_allbands(pi, table)` with the table of the radio
   revision (section 6).
5. If boardflags bit 1 (0x2) is set: `mod(RADIO(0x8ea), 0x0100, 0x0100)`.
6. `mod(RADIO(0x96b), 0x0800, 0x0800)`; `mod(RADIO(0x96b), 0x4000, 0x4000)`;
   `mod(RADIO(0x96c), 0x0800, 0x0800)`; `mod(RADIO(0x96b), 0x8000, 0x8000)`;
   `mod(RADIO(0x96b), 0x1000, 0x1000)`; `mod(RADIO(0x96b), 0x0004, 0x0004)`.
7. `mod(RADIO(0x407), 0x0002, 0x0002)`; `mod(RADIO(0x55e), 0x0010, 0x0010)`.
8. For each core c, with b = c << 9: `mod(RADIO(0x126|b), 0x0300, 0x0100)`;
   `mod(RADIO(0x127|b), 0x0003, 0x0002)`; `mod(RADIO(0x06f|b), 0x0004, 0)`;
   `mod(RADIO(0x06f|b), 0x0001, 0)`; `mod(RADIO(0x06f|b), 0x0002, 0)`;
   `mod(RADIO(0x065|b), 0x0001, 0)`.
   (Radio revisions 7 and 8 only, then for each core: `mod(RADIO(0x03c|b), 2, 2)`;
   `mod(RADIO(0x16e|b), 8, 8)`.)
9. `mod(RADIO(0x40c), 0x0010, 0)`; `PHY(0x408)` = s408 | 6; delay 100;
   `mod(RADIO(0x40c), 0x0010, 0x0010)`.
10. `PHY(0x417)` = 0x000d; `PHY(0x408)` = s408 | 2; `PHY(0x728)` = s728 | 0x0180;
    delay 100; `PHY(0x417)` = 0x0004; `PHY(0x728)` = s728 & 0xfeff.

All PHY addresses here are the plain ones (core 0 for 0x720/0x728). For radio
revisions without a table the function goes from step 3 directly to step 9.

### 6. wlc_phy_init_radio_prefregs_allbands (.text+0x0b623e, name original)

Inputs: `pi`, pointer to a table of {address, value} pairs. Result: number of
entries written. Steps: write entry 0; then continue with the next entry until
the address of the next entry is 0xffff (the first entry is written without
looking at its address). Each entry is one `write_radio_reg(address, value)`.
Only caller: `sub_09fb72`.

### 7. sub_09591e (.text+0x09591e, name assigned: wlc_phy_radio2069_rccal)

Purpose: RC calibration in three passes; stores two results in `pi_ac` and
writes one into the radio. Input: `pi`. Per pass values (aliases mine):

| Pass | sr | sc | x1 | trc | Result goes to |
|---|---|---|---|---|---|
| 0 | 1 | 0 | 0x1c | 0x014a | `pi_ac+0x339` and `pi_ac+0x33a` |
| 1 | 0 | 2 | 0x70 | 0x0101 | radio registers 0x126 and 0x043 of every core |
| 2 | 0 | 1 | 0x40 | 0x011a | `pi_ac+0x33b` |

(Other major revisions use the factor 0xa0, or with a crystal of 37.4 MHz
the factor 0x9e and trc = 0x22d, 0xf0, 0x10a.)

Steps:

1. `mod(RADIO(0x8ea), 0x0080, 0x0080)`; `mod(RADIO(0x8ed), 0x0600, 0x0400)`.
2. For pass p = 0, 1, 2:
   1. `mod(RADIO(0x410), 0x1000, sr << 12)`; `mod(RADIO(0x410), 0x0018, sc << 3)`;
      `mod(RADIO(0x411), 0xff00, x1 << 8)`; write `RADIO(0x412)` = trc.
   2. Pass 2 only, for each core c (b = c << 9): `mod(RADIO(0x11d|b), 0x0004, 0)`;
      `mod(RADIO(0x171|b), 0x2000, 0x2000)`.
   3. `mod(RADIO(0x410), 0x0001, 0)`; delay 1; `mod(RADIO(0x410), 0x0001, 1)`;
      delay 35; `mod(RADIO(0x411), 0x0001, 1)` (start).
   4. Up to 100 times: delay 100, read `RADIO(0x413)`, stop with "done" when
      bit 4 is set.
   5. `mod(RADIO(0x411), 0x0001, 0)`.
   6. Only if done:
      * pass 0: n0 = `RADIO(0x414)`, n1 = `RADIO(0x415)` (two reads, this order);
        g = ((n1 - n0) * 0xc1) >> 8 computed in 32 bit signed arithmetic,
        the low byte is stored in `pi_ac+0x339` and `pi_ac+0x33a`;
      * pass 1: v = `RADIO(0x416)` & 0x1f; for each core c:
        `mod(RADIO(0x126|b), 0x001f, v)`; `mod(RADIO(0x043|b), 0x001f, v)`;
      * pass 2: v = `RADIO(0x416)`; `pi_ac+0x33b` = (v >> 5) & 0x1f; for each
        core c: `mod(RADIO(0x171|b), 0x2000, 0)`.
   7. `mod(RADIO(0x410), 0x0001, 0)`.
3. `mod(RADIO(0x8ea), 0x0080, 0)`.

Notes: when a pass runs into its timeout the defaults from attach stay (0x80,
0x80, 0x0c) and, in pass 2, bit 13 of `RADIO(0x171|b)` stays set. The results
are used by `sub_0a6b0f` (PHY initialisation, not part of this specification):
transmit low-pass filter with g unchanged, DAC buffer with `pi_ac+0x33b`,
receive low-pass filter with (g * 0xdd) >> 8 for 20 MHz and (g * 0xd7) >> 8 for
40 and 80 MHz when the PHY revision is below 2.

### 8. si_pmu_rfldo (.text+0x01e7e8, name original)

Inputs: `sih`, `on`. For chip ids 0x4360, 0xaa06 and 0x4352 only:
`si_pmu_regcontrol(sih, 0, mask 0x2, on ? 0 : 0x2)`, that is: select regulator
control register 0 through `CC(0x658)` and modify bit 1 in `CC(0x65c)`.
Callers: `wlc_bmac_attach` after `wlc_coredisable` (off), `wlc_bmac_hw_down`
(off), `wlc_bmac_init` before the band is selected (on).

### 9. OTP word 16

In `wlc_phy_attach_acphy`:

1. s = `si_get_sromctl(sih)` (`CC(0x190)`); if bit 4 of s is clear write s | 0x10.
2. `otp_read_word(sih, 16, &w)`; w is 0 if the read fails.
3. If bit 4 of s was clear write s back.
4. `pi_ac+0x8e2` = (w >> 8) & 0x1f.
5. At the end of the function, only if `pi_ac+0x34e` = 1: read OTP word 16
   again (without touching `CC(0x190)`) into `sh+0xf2`; on success `sh+0xf2` &=
   0xf; on failure major revision 2: 9, major revision 1: 10, major revision 0:
   unchanged. Not used on this radio (section 4, step 4).

In `sub_0b018f` (PHY initialisation), after its first step:

1. r = `PHY(0x000)` & 0xf. If r > 1 nothing is written.
2. v = `pi_ac+0x8e2`. If r = 0: if v = 0 then v = 5. If r = 1: v = v - 3 if
   v > 3, else 0.
3. `si_pmu_regcontrol(sih, 0, mask 0x01f00000, v << 20)`.

The purpose of the field is unknown (a regulator output voltage, by its place).

### 10. sub_08e9b1 (.text+0x08e9b1, name assigned: wlc_phy_chan2freq_acphy)

Inputs: `pi`, channel number (chanspec & 0xff), pointers for the results.
Result: 1 if found, with the frequency in MHz and the address of the entry
(major revision 0: fourth argument); 0 and frequency 0 if not. Steps: choose
the table by radio revision (section "Tables"), compare word 0 of every entry
with the channel number, first match wins. No hardware access.

### 11. Radio tuning in sub_0a7089 (.text+0x0a7089; inlined, name assigned: wlc_phy_chanspec_radio2069_setup)

Inputs: `pi`, chanspec. Before the part described here the function has
(specified elsewhere): looked up the entry (if there is none the function
returns at once and nothing is changed); determined "band changed" (init, or
band differs from `pi_ac+0x330`) and "bandwidth changed" (init, or bandwidth
differs from `pi_ac+0x334`); set bits 1 and 0 of `PHY(0x19e)`; set bit 8 of
`PHY(0x003)` to the band; entered carrier search; stored the chanspec in
`pi+0x17e`.

1. If band changed or bandwidth changed: `mod(PHY(0x728), 0x0100, 0x0100)`;
   delay 1; `mod(PHY(0x728), 0x0100, 0)`.
2. Write words 2..51 of the entry to the 50 radio registers of the table in
   section "Tables", in that order, each with `write_radio_reg`.
3. If the radio revision is below 4: write `RADIO(0x323)` = 0x03e9, `RADIO(0x523)` =
   0x03e9; `mod(RADIO(0x892), 0xff00, 0xa000)`.
4. If the channel number (low byte of the chanspec) is 4: write `RADIO(0x8d6)` =
   0x0ce4; `mod(RADIO(0x8ec), 0x0070, 0x0050)`.
5. `mod(RADIO(0x645), 0x7000, 0x7000)`.
6. If the band is 5 GHz: `wlc_2069_rfpll_150khz(pi)` (section 12).
7. If the radio revision is above 3: write `RADIO(0x723)` = 0x83e0.
8. `sub_09311b(pi)` (section 13): start of the VCO calibration.
9. (Rest of the function, specified elsewhere. It contains, in this order:
   restore of bits 1 and 0 of `PHY(0x19e)`; at init `sub_0a0be4`; if band changed
   `sub_09e378`, sections 15 and 16; if bandwidth changed or init:
   `wlc_phy_resetcca_acphy`, delay 1, `sub_096203`, section 14.)
10. Near the end, if an entry was found: `sub_093e47(pi, 0)` (section 13).

### 12. wlc_2069_rfpll_150khz (.text+0x09576e, name original)

Loop filter settings for a PLL bandwidth of 150 kHz (by the name). Steps:
`mod(RADIO(0x8c9), 0xff00, 0)`; `mod(RADIO(0x8c9), 0x00ff, 0x0002)`; write
`RADIO(0x8ca)` = 0x0002; `mod(RADIO(0x8cc), 0x00ff, 0x0002)`;
`mod(RADIO(0x8cc), 0xff00, 0xff00)`; write `RADIO(0x8c7)` = 0xffff; write
`RADIO(0x8c8)` = 0xffff. These overwrite words 22..26 of the channel entry.

### 13. sub_09311b and sub_093e47 (.text+0x09311b, .text+0x093e47; names assigned: wlc_phy_radio2069_vcocal, wlc_phy_radio2069_vcocal_wait)

Start (`sub_09311b(pi)`): `mod(RADIO(0x8e5), 0x4000, 0)`;
`mod(RADIO(0x8d0), 0x0001, 0)`; `mod(RADIO(0x8e8), 0x0040, 0)`;
`mod(RADIO(0x8dc), 0x2000, 0)`; delay 11; `mod(RADIO(0x8d0), 0x0001, 1)`;
`mod(RADIO(0x8e8), 0x0040, 0x0040)`; delay 1; `mod(RADIO(0x8dc), 0x2000, 0x2000)`.

Wait (`sub_093e47(pi, settle)`): up to 100 times: delay 10, read `RADIO(0x90b)`,
stop when bit 8 is set. Then, if `settle` = 1, delay 120. No result, no error
handling: a timeout is not noticed.

Callers: the channel function (start in step 8, wait with settle = 0 in step
10, several milliseconds of other work in between); `wlc_phy_cals_acphy` in
phase 18 of the periodic calibration: start, then wait with settle = 1.

### 14. sub_096203 (.text+0x096203, name assigned: wlc_phy_radio2069_afecal)

Calibration of the converters of each core; no transmit tone. Steps:

1. `mod(RADIO(0x8ea), 0x0080, 0x0080)`.
2. For each core c, with b = c << 9 and o = c * 0x200:
   1. Read `PHY(0x739+o)` -> s1, `PHY(0x73a+o)` -> s2, `PHY(0x725+o)` -> s3.
   2. `mod(PHY(0x739+o), 0x0080, 0x0080)`; `mod(PHY(0x725+o), 0x0004, 0x0004)`.
   3. `mod(RADIO(0x121|b), 0x1000, 0)`; delay 100; `mod(RADIO(0x121|b), 0x1000, 0x1000)`.
   4. v = `RADIO(0x122|b)`; write `RADIO(0x122|b)` = v | 0x000f.
   5. Up to 10 times: delay 10; read `RADIO(0x144|b)` -> a; read it again -> e;
      stop when bit 0 of e and bit 1 of a are set.
   6. Write `RADIO(0x122|b)` = v & 0xfff0.
   7. Write `PHY(0x725+o)` = s3, `PHY(0x739+o)` = s1, `PHY(0x73a+o)` = s2.
3. `mod(RADIO(0x8ea), 0x0080, 0)`.
4. Radio revision below 4 only: `mod(RADIO(0x8ea), 0x0080, 0x0080)`; for each
   core c: `mod(RADIO(0x15f|b), 0x0020, 0x0020)`; `mod(RADIO(0x15f|b), 0x0010,
   0x0010)`; then for k = 0..13: read `RADIO((0x143-k)|b)` and write its
   complement (16 bit) to `RADIO((0x135-k)|b)`; finally
   `mod(RADIO(0x8ea), 0x0080, 0)`.

### 15. sub_09e378, radio part (.text+0x09e378, name assigned: wlc_phy_set_regtbl_on_band_change_acphy)

Runs when the band changed. Between PHY work (transmit gain table and others,
specified elsewhere) it touches the radio only if the PHY revision is 0:

* 2.4 GHz: for each core c: `mod(RADIO(0x059|b), 0x3000, 0x1000)`.
* 5 GHz, two independent steps:
  1. if `pi_ac+0x34a` != 0 (boardflags bit 29): for c = 0 and 1 (fixed):
     `mod(RADIO(0x05c|b), 0xf000, 0x6000)`; `mod(RADIO(0x061|b), 0xf000,
     0x6000)`;
  2. if the radio revision is below 4, whatever `pi_ac+0x34a` is:
     `mod(RADIO(0x45c), 0xf000, 0x6000)`; `mod(RADIO(0x461), 0xf000, 0x6000)`
     (the same two registers of core 2).

  (Confirmed by the comparison test `radio-band`, radio revision 3.)

Later it calls `sub_0940f8(pi, sh+0xa5, 0)` (section 16) and the two
interference functions of section 20.

### 16. sub_0940f8 (.text+0x0940f8, name assigned: wlc_phy_tssi_radio_setup_acphy)

Inputs: `pi`, core mask, mode (0 from the band change and from `sub_0affa9`, 1
from `sub_0b0455`). Steps for major revision 0:

1. `mod(RADIO(0x548), 0x0001, 1)`; write 0 to `RADIO(0x549)`, `RADIO(0x54a)`,
   `RADIO(0x54b)`, `RADIO(0x54c)`; `mod(RADIO(0x40b), 0x0001, 0)`.
2. For each core c whose bit is set in the mask (b = c << 9):
   1. mode 0: `mod(RADIO(0x01a|b), 0x00f0, 0x0010)`, then
      `mod(RADIO(0x01a|b), 0x0004, 0x0004)`. Mode not 0: `mod(RADIO(0x01a|b),
      0x00f0, 0x0020 on 5 GHz, 0 on 2.4 GHz)`, then `mod(RADIO(0x01a|b), 0x0004, 0)`.
   2. c = 0: `mod(RADIO(0x54b), 0xff00, 0x0100)`; c = 1: `mod(RADIO(0x54b),
      0x00ff, 0x0001)`; c = 2: `mod(RADIO(0x54c), 0xff00, 0x0100)`.
   3. `mod(RADIO(0x01a|b), 0x0300, 0)`; `mod(RADIO(0x017|b), 0x0002, 0)`;
      `mod(RADIO(0x01f|b), 0x0004, mode << 2)`; `mod(RADIO(0x170|b), 0x0100, 0x0100)`.

### 17. sub_0a0be4 (.text+0x0a0be4, name assigned: wlc_phy_set_reg_on_reset_acphy)

Named in the assignment as the tuning function; it is not: it has no radio
access and runs only at initialisation (`pi_ac+0x32c` set), after the radio
tuning. PHY revision 0/1 path, in this order: set bits 0x01c0 of `PHY(0x19e)`;
on 2.4 GHz `PHY(0x3c4)` = 0x0668; `mod(PHY(0x19e), 0x0200, 0x0200)`;
`mod(PHY(0x19e), 0x003c, 0x0010)`; `PHY(0x1f2)` = 0x00c8; `PHY(0x026)` = 0x0092;
`PHY(0x1ed)` = 0x0050; `PHY(0x025)` = 0x0030; `si_core_cflags(sih, 0x10, 0x10)`;
`mod(PHY(0x40f), 0x0200, 0)`; clear bit 5 of `PHY(0x2f1)`, `PHY(0x2ed)`,
`PHY(0x2f9)`, `PHY(0x2f5)`; `mod(.., 0x00ff, 0x0055)` on `PHY(0x2ef)`, `PHY(0x2eb)`,
`PHY(0x2f7)`, `PHY(0x2f3)`; `PHY(0x400)` = 0; `mod(PHY(0x1ca), 0x1000, 0)`;
`wlc_phy_resetcca_acphy`; `mod(PHY(0x072), 0x0004, 0x0004)`; `mod(PHY(0x1b0),
0x0020, 0)`; `mod(PHY(0x1b1), 0x1000, 0x1000)`; `mod(PHY(0x1b6), 0x8000, 0)`; for
each core: `mod(PHY(0x690 + c * 0x200), 0x0200, 0x0200)` then `mod(.., 0x0400,
0x0400)`; `PHY(0x1e6)` = 0x0030; `wlc_phy_hwaci_setup_acphy(pi, 0, x)` with x = 1 if
bit 1 of `sh+0x84` or of `sh+0x88` is set; `PHY(0x358)` = 0xc07f. (PHY revisions
2, 3, 5, 6: more, among it `sub_08fb39` and `sub_09027d`.) The complete
specification belongs to the PHY initialisation.

### 18. wlc_phy_radio_override_acphy (.text+0x096da1, name original)

Inputs: `pi`, `on`. Not referenced by any code of the object. Steps for major
revision 0: write to each of the 56 registers of `ovr_regs_2069_rev2`, in table
order, 0xffff if `on`, else 0; `mod(RADIO(0x55e), 0x0002, 0)`;
`mod(RADIO(0x96b), 0x0200, 0)`.

### 19. wlc_phy_radio2069_pwrup_seq, wlc_phy_radio2069_pwrdwn_seq (.text+0x091466, .text+0x091582, names original)

Not referenced by any code of the object. They work on the plain addresses
(core 0) and on the saved values `pi_ac+0x8ea..0x8fc`, which
`wlc_phy_attach_acphy` fills once from the same registers at the start of attach.

| PHY register | 0x739 | 0x73a | 0x725 | 0x729 | 0x721 | 0x728 | 0x720 | 0x408 | 0x417 | 0x416 |
|---|---|---|---|---|---|---|---|---|---|---|
| saved in `pi_ac+` | 0x8ee | 0x8f0 | 0x8f2 | 0x8ea | 0x8ec | 0x8f4 | 0x8f6 | 0x8f8 | 0x8fa | 0x8fc |

Power down: read and save 0x739, 0x73a, 0x725; write 0x739 = 0, 0x73a = 0,
0x725 = 0x1fff; read and save 0x729, 0x721; write 0x729 = 0x0040, 0x721 =
0xffbf; read and save 0x728, 0x720; write 0x728 = 0, 0x720 = 0x03ff; read and
save 0x408, 0x417, 0x416; `mod(PHY(0x408), 0x0002, 0)`; write 0x417 = 0,
0x416 = 1. Power up: write the ten saved values back in the order of the table.

### 20. Low power VCO modes, mini PMU, other functions that touch the radio

* `wlc_phy_lp_mode(pi, mode)` stores mode 1, 2 or 3 in `pi_ac+0x8e4`,
  `wlc_phy_force_lpvco_2G(pi, v)` stores v in `pi_ac+0x8e5`; both only if the PHY
  revision is 2, 5 or 6. The fields select channel tables of radio major
  revision 1. No effect on the 4360.
* `sub_08fb39`, `sub_09027d`: PHY registers 0x830..0x856 only; called for radio
  major revisions 1 and 2 and PHY revisions 2, 3, 5, 6. Not reached.
* Radio accesses of functions that are specified elsewhere (listed so that
  nothing is missed; R = radio register, b = core << 9):
  `wlc_phy_hwaci_setup_acphy(pi, enable, init)`, if init: for each core
  `mod(R(0x045|b), 0x0080, pi_ac+0x67c << 7)`, then on `R(0x049|b)`: mask 0xe000 <-
  0, 0x1800 <- 0, 0x0400 <- 0, 0x0300 <- `pi_ac+0x67d` << 8, 0x0040 <-
  `pi_ac+0x67e` << 6, 0x0080 <- `pi_ac+0x67f` << 7.
  `wlc_phy_aci_w2nb_setup_acphy(pi, on)`, if on: for each core
  `mod(R(0x033|b), 0xf000, (pi_ac+0x680 & 0xf) << 12)`.
  `sub_09175a`, `sub_091b6e` (receive gain control, called by `sub_09a121` for
  each core): one `mod` each, of `R(0x045|b)` (mask 0x0040, 0x0080 or 0x0300) and
  of `R(0x02c|b)` on 2.4 GHz or `R(0x033|b)` on 5 GHz (mask 0x00f0).
  `sub_09c161` (apply transmit calibration coefficients): per core writes
  `R(0x002|b)`..`R(0x005|b)`.
  `sub_0a4adc`: only with PHY revision < 2, `pi+0x1169` set (by an iovar), 40 MHz
  and centre frequency 5310 MHz: `mod(R(0x145), 0x000f, 8)`;
  `mod(R(0x146), 0x01e0, 0x0100)`; `mod(R(0x146), 0x000f, 8)`.
  `sub_0a602f` (front end control): `si_pmu_regcontrol(sih, 0, 0x4, 0x4)` when
  `femctrl` is 3.
  Calibrations with a transmit tone and measurements: `sub_09380f`, `sub_0948c3`,
  `sub_094b1a`, `sub_095511`, `sub_0abc76`, `sub_0af7f4`, `sub_0b0455`,
  `wlc_phy_scanroam_cache_cal_acphy`, `wlc_phy_tempsense_acphy`.

## Verification

Traces: `re-out\up-trace.txt`, `re-out\chan\*.txt` (radio revision 4, PHY
revision 1) and my own runs in `re-out\analysis\acphy-radio\` made with
`myrun.py` (a harness on top of the unchanged tools): `r3-*` (radio revision
3, PHY revision 0, channels 4, 36, 1), `done1-*` (status bits of the
calibrations modelled as "done", OTP word 16 = 0x0a00, `PHY(0)` = 1, boardflags
bit 1 set), `bf3a`, `bf3b` (boardflags3 bits 13 and 3), `otp3`, `otp4`, `x1-call-*`
(direct calls of functions).

| Item | Checked how | Result |
|---|---|---|
| Analog core, radio off | up-trace seq 789..819, 905..931 | as specified |
| Radio id | up-trace seq 791..794 | as specified |
| Power-on sequence, both preferred value tables | up-trace seq 25760..26007; `r3-up` | order and values as specified; 24 resp. 22 table writes |
| boardflags bit 1 | `done1-up` | `RADIO(0x8ea)` gets bit 8 |
| RCAL | up-trace seq 26008..26395 (timeout: 100 polls + 1 read); `done1-up` (1 poll + 1 read) | as specified |
| RCAL skipped by boardflags3 | `bf3a`, `bf3b` | no radio access between sections 5 and 7; `sh+0xf2` = OTP & 0xf |
| RCCAL | up-trace seq 26396..27490 (timeouts); `done1-up` (done paths, n0 = 0x100, n1 = 0x180, 0x416 = 0x1ef) | as specified |
| RF LDO | up-trace seq 938, 1185 | 2 (off), 0 (on) |
| Regulator field | up-trace seq 27498 (5 << 20); `done1` (7 << 20), `otp3` (0), `otp4` (0xc << 20) | as specified |
| Channel entry layout | `chantab.py match`: channels 1, 6, 36, 42 (36/80), 151 (149/40) with rev4; 4, 36, 1 with rev3 | all 50 words in order |
| Patches of radio revision 3, of channel 4 | `r3-chan-4` | as specified |
| 150 kHz, VCO calibration start and wait | chan-36 seq 57281..57357, 64972..65269; `x1-call-vcocal` | as specified |
| AFE calibration | up-trace seq 49546..49768 (timeouts); access counts of `r3-up` (82 reads, 44 writes); `done2-up` seq 47432 (done after one poll) | as specified |
| Order at `wlc_down` | `dn-call-down` | radio off, analog core off, RF LDO off |
| Band change radio part | `r3-up` seq 41167, `r3-chan-36` seq 65710; `x2-chan-36` (boardflags bit 29 set) seq 57666 | as specified |
| TSSI setup, mode 0 and 1, both bands | up-trace seq 42165..42271, chan-36 seq 58309; `x2-call-tssi1*` (direct calls) | as specified |
| `sub_0a0be4` | up-trace seq 38074..38345 | as specified |
| Radio on with initialised PHY | `x1-call-radio_on`, `x1-call-hw_on` | steps 1..8 of section 4 including step 6 |
| Override, power up/down sequences | `x1-call-override1/0`, `x1-call-pwrdwn`, `x1-call-pwrup` | as specified |

Not checked against a trace (code reading only): the steps for radio
revisions 7/8; the radio accesses listed in section 20 for functions specified
elsewhere, except those of `sub_09175a`/`sub_091b6e` and of the two interference
functions, which appear in the up-trace (seq 49367..49461, 38229..38311,
52129..52149).

## Open questions

* The meaning of nearly all registers and of the bank 0x600 and of bit 12 of
  PHY addresses is unknown or an interpretation (marked).
* What `PHY(0x000)` & 0xf reads on the real card decides the regulator value
  (section 9); the emulator reads 0. The content of OTP word 16 of the real
  card is unknown.
* The status bits are not modelled by the card model; the exit conditions are
  from the code. Whether the real radio finishes within the poll limits is
  not known. No calibration has any error handling.
* The RCAL result is read and dropped for this radio; whether the hardware
  applies it by itself is unknown.
* Why channel 4 gets its own loop filter values (section 11 step 4) is unknown.
* `pi+0xc24` (crystal frequency from `si_alp_clock`) is 20000000 in the
  emulator; it is not used on the 4360 path of this specification.
