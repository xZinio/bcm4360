# PMU and clocks of the BCM4360

Register notation: see [access.md](access.md). Additional notation used here:

* `corereg(reg, mask, val)`: the access pattern of `si_corereg`: if `mask` or
  `val` is not 0: read `reg`, write `(old & ~mask) | val` (`val` is **not**
  limited to `mask`); then in every case read `reg` once more; that last value
  is the result. A pure read (`mask = val = 0`) is a single read.
  ChipCommon and the PCIe core are reached through their fixed windows, no
  window is moved.
* `[CC window]`: the function remembers the current core index, makes
  ChipCommon (core index 0) the current core and restores the remembered index
  before it returns. Each of the two switches writes `PCICFG(0x80)` (core
  address) and `PCICFG(0x70)` (wrapper address), also when the core does not
  change. Registers are then accessed through the movable window.
* `SPINWAIT(cond, us)`: evaluate `cond`; while it is true and less than `us`
  microseconds have been waited: `osl_delay(10)`, evaluate again. At most
  `us/10 + 1` evaluations and `us/10` delays.
* "4360 family": chip ids 0x4360, 0x4352, 0xa9c4 (43460), 0xaa06 (43526). Where
  a function handles only some of them this is stated.

## Scope

All of `hndpmu.c` (.text 0x0108d4..0x01725c) and the clock/power functions of
`siutils.c`, as executed for chip 0x4360 rev 3 on the PCI bus behind a PCIe
Gen2 core, ChipCommon rev 43. Names of `sub_` functions are assigned by the
analyst (see `re-out/analysis/names/pmu.tsv`).

| Function | .text | Size | Effect on the 4360 |
|---|---|---|---|
| si_pmu_init | 0x011993 | 209 | yes |
| si_pmu_chip_init | 0x015543 | 223 | only its two sub-calls |
| si_pmu_otp_chipcontrol | 0x015474 | 207 | yes (variables `chipc%d`) |
| si_pmu_otp_regcontrol | 0x015622 | 217 | yes (variables `reg%d`) |
| si_pmu_otp_pllcontrol | 0x015a1d | 213 | never called for the 4360 |
| si_pmu_sprom_enable, si_pmu_is_sprom_enabled | 0x0116d5, 0x011660 | 142, 117 | window switch only (chip 0x4315 only); the second returns 1 |
| si_pmu_measure_alpclk | 0x01246d | 214 | yes |
| si_pmu_pll_init | 0x016f0e | 846 | yes (not 0xaa06) |
| sub_016dbd = si_set_bb_vcofreq_frac | 0x016dbd | 337 | yes |
| si_pmu_res_init | 0x014cab | 1993 | yes |
| sub_0111b0 = si_pmu_res_masks | 0x0111b0 | 1040 | yes |
| sub_0118f2 = si_pmu_res_deps | 0x0118f2 | 161 | yes |
| si_pmu_swreg_init | 0x0156fb | 802 | only si_pmu_otp_regcontrol |
| si_pmu_chipcontrol, si_pmu_regcontrol, si_pmu_pllcontrol, si_pmu_pllupd | 0x01116b, 0x011126, 0x011032, 0x011015 | 69, 69, 69, 29 | yes |
| si_pmu_alp_clock, si_pmu_ilp_clock | 0x011c28, 0x012d28 | 656, 191 | yes |
| si_pmu_si_clock, si_pmu_cpu_clock, si_pmu_mem_clock | 0x0162b5, 0x0167c8, 0x016639 | 900, 460, 399 | yes |
| sub_011a64 = si_pmu1_cpuclk0, sub_010b09 = si_pmu1_pllfvco0 | 0x011a64, 0x010b09 | 357, 228 | yes |
| sub_011eb8 = si_pmu5_clock | 0x011eb8 | 349 | only through si_pmu_mem_clock |
| si_pmu_get_bb_vcofreq | 0x014b7b | 304 | yes |
| si_pmu_fast_pwrup_delay | 0x015eae | 674 | yes (constant) |
| si_pmu_otp_power, si_pmu_is_otp_powered | 0x012a8d, 0x011763 | 667, 399 | yes |
| si_pmu_waitforclk_on_backplane, si_pmu_force_ilp, si_pmu_enb_ht_req | 0x012543, 0x0122cd, 0x012255 | 147, 119, 120 | yes (not chip specific, no callers) |
| si_pmu_set_ldo_voltage | 0x010d95 | 640 | yes (no caller on the 4360 path) |
| si_pmu_set_switcher_voltage | 0x010cad | 156 | yes (not chip specific, no callers) |
| si_pmu_spuravoid, si_pmu_spuravoid_isdone, sub_013e12 = si_pmu_spuravoid_pllupdate | 0x014a78, 0x01491d, 0x013e12 | 259, 347, 2827 | a read and a write back of CC(0x600) only |
| si_pmu_pll_off_PARR | 0x013cba | 344 | three reads |
| si_pmu_pllreset | 0x016150 | 170 | none |
| siutils.c: si_pmu_rfldo | 0x01e7e8 | 51 | yes (not 0xa9c4) |
| siutils.c: si_clkctl_init, si_clkctl_cc, sub_02105c = _si_clkctl_cc, si_clkctl_fast_pwrup_delay, si_clkctl_xtal | 0x0204b8, 0x0214d8, 0x02105c, 0x020300, 0x01e534 | 355, 102, 802, 440, 391 | yes |
| siutils.c: si_alp_clock, si_ilp_clock, si_clock, si_otp_power, si_is_otp_powered, si_sprom_enable, si_is_sprom_enabled | 0x01f1d5, 0x01f1bb, 0x02061b, 0x01e964, 0x01e987, 0x01e934, 0x01e94d | 63, 26, 329, 35, 23, 25, 23 | wrappers |

**No effect on the 4360** (chip not handled; at most a `[CC window]` switch,
no other access; all executed in the emulator): si_pmu_radio_enable (0x01308a,
no access at all), si_pmu_paref_ldo_enable (0x010d49, none), si_pmu_rcal
(0x01280d, window only), si_pmu_minresmask_htavail_set (0x0121e7, window only;
chip 0x4313), si_pmu_res_minmax_update (0x0115c0, window only; chips
0x4335/0x4350), si_pmu_is_autoresetphyclk_disabled (0x012015, window only,
result 0), si_pll_minresmask_reset (0x012fdd, window only, result -23),
si_pmu_def_alp_clock (0x010c75, returns 20000000, no access).
si_pmu_gband_spurwar (0x0138ea, chips 0xa99c and 0xa8d6, caller is N-PHY code)
and si_sdiod_drive_strength_init (0x013b27, no callers): only the chip
selection at their start was read, the 4360 is not in it (unverified beyond
that).
**Never reached for the 4360** (their callers select other chips; their bodies
were not analysed): sub_012de7 (si_pmu0_pllinit0), sub_013141
(si_pmu1_pllinit0), si_pmu_set_4330_plldivs (0x011077), sub_015af2, sub_016c8a,
si_pmu_update_pllcontrol (0x016a54), sub_016994 (PLL initialisation of other
chips), sub_011bc9 (si_pmu1_alpclk0),
sub_010924/sub_010a0a (crystal tables; they contain entries for the family but
no caller uses them for it), sub_012344 (si_pmu_res_uptime), sub_0161fa,
sub_0125d6/sub_012697 (PLL on/off: called, but sub_010bed returns the mask 0
for the family, see si_pmu_spuravoid), si_pmu_get_pmutimer,
si_pmu_get_pmutime_diff, si_pmu_wait_for_res_pending,
si_pmu_wait_for_steady_state, the dependency filters sub_0108d4, sub_0108e3,
sub_010903, sub_010912 (the 4360 has no dependency table).

## Overview

The PMU is part of ChipCommon. The driver uses it when bit 28 of the
ChipCommon capabilities `CC(0x04)` is set (all `si_*` wrappers test this bit;
without it they return defaults: ALP clock 20000000, ILP clock 32000, OTP
powered, SPROM enabled).

Order of events (sequence numbers of `re-out/up-trace.txt`):

1. **Attach, `si_doattach`** (sub_022b10):
   1. Bus type PCI: unless the configuration space has a PCI Express
      capability (id 0x10): `si_clkctl_xtal(sih, 3, 1)` (seq 10..15; at this
      time the bus core is not identified yet, so the function acts).
   2. Identification: `CC(0x00)` chip id/rev/package, `CC(0x2c)` chip status
      (ChipCommon rev > 10), `CC(0x04)` capabilities, `CC(0xac)` extended
      capabilities (rev >= 35), and if capabilities bit 28 is set `CC(0x604)`:
      PMU capabilities, PMU revision = bits 0..7.
   3. After the SROM variables are read, only if capabilities bit 28 is set, in
      this order (seq 338..386): `si_pmu_init`, `si_pmu_chip_init`, crystal
      frequency (below), `si_pmu_pll_init`, `si_pmu_res_init`,
      `si_pmu_swreg_init`.
   4. Crystal frequency: `xtalfreq` = integer value of the SROM variable
      `xtalfreq` (kHz; looked up in the variables of the card). For the 4360
      family: if it is 0 (or absent) `xtalfreq = si_pmu_measure_alpclk()`. The
      value is passed to `si_pmu_pll_init`, which ignores it for the 4360
      family (40 MHz is built into the PLL arithmetic).
2. **`wlc_bmac_attach`**: `si_clkctl_init`; later `si_pmu_rfldo(sih, 0)` (seq
   938..944) and `si_clkctl_xtal(sih, 3, 0)` (returns -1 without access, the bus
   core is known now). `wlc_phy_attach_acphy` calls `si_alp_clock` and, through
   the OTP code, `si_is_otp_powered` (seq 547, 552, 557, 562).
3. **First `wlc_up`, `wlc_bmac_hw_up`** (runs once per attach): for bus PCI and
   MAC core rev >= 40 `si_pmu_res_init` a second time (seq 1003..1022), then
   `si_clkctl_xtal(sih, 3, 1)` (no access), `si_clkctl_init`.
   `wlc_bmac_4360_pcie2_war` (wlc_bmac.c, rewrites PMU_PLLCTL[10], [11]) acts
   only for chip rev < 3: not described. `wlc_bmac_up_prep`: `si_clkctl_xtal`,
   `si_clkctl_init` again.
4. **Every `wlc_bmac_init`**: `si_pmu_rfldo(sih, 1)` (seq 1185); the result of
   `si_clkctl_fast_pwrup_delay` is written as 16 bit to `D11(0x6a8)` and kept
   in `wlc_hw + 0x192`; `wlc_bmac_switch_macfreq` calls
   `si_pmu_get_bb_vcofreq(sih, osh, 40)` (seq 25618). The AC-PHY initialisation
   uses `si_pmu_regcontrol(sih, 0, 0x01f00000, 0x00500000)` (in sub_0b018f, seq
   27498) and `si_pmu_regcontrol(sih, 0, 4, 4)` (in sub_0a602f, seq 38361);
   these belong to the PHY specifications.
5. **`wlc_down`, `wlc_bmac_hw_down`**: `si_pmu_rfldo(sih, 0)`, then
   `si_clkctl_xtal(sih, 3, 0)` (no access).
6. Channel changes: the four traces in `re-out/chan` contain no access made by
   a function of this topic (the spur avoidance call in sub_0a7089 is made for
   chip 0x4335 only).

With a PMU the clock mode of the MAC is switched by wlc_bmac.c directly in
`D11(0x1e0)`; `si_clkctl_cc` is not called (its callers use it only without
PMU).

## Data

### Fields of `sih` used

0x04 bus type, 0x08 bus core id, 0x0c bus core rev, 0x10 bus core index, 0x14
ChipCommon rev, 0x18 capabilities, 0x20 PMU rev, 0x24 PMU capabilities, 0x3c
chip id, 0x40 chip rev, 0x48 chip status, 0x58 osh, 0x1c0 current core index.

PMU capabilities `CC(0x604)`: bits 0..7 revision, bits 8..12 number of
resources. Number of indirect registers, PMU rev >= 5 / rev < 5: PLL control
bits 17..21 / 17..20; regulator control bits 22..26 / 21..24; chip control
bits 27..31 / 25..28.

Chip status `CC(0x2c)` of the 4360 family as used here: bit 0: crystal is
40 MHz (ALP clock 40 MHz, else 20 MHz is reported); bit 5: when set the driver
neither programs the PLL registers of `si_pmu_res_init` nor a maximum resource
mask (purpose unknown).

### Static variables

| Where | Size | Content |
|---|---|---|
| .bss+0x2f0 | 1 | 1 after a variable `rmin` was found |
| .bss+0x2f1 | 1 | 1 after a variable `rmax` was found |
| .bss+0x2f4 | 4 | measured ILP clock in Hz, 0 = not measured yet |
| .bss+0x2f8 | 4 | value of `rmin` |
| .bss+0x2fc | 4 | value of `rmax` |

### Variables that override registers

`rmin`, `rmax`, `r<n>t`, `r<n>d`, `chipc<n>`, `reg<n>`, `pll<n>` (n decimal
without leading zeros). They are looked up **without** the variables of the
card (`getvar(NULL, name)`): only the global variable list is searched, which
`nvram_init` fills from an optional file `nvram.txt` (opened through
`osl_os_open_image`; `name=value` items separated by blanks, tabs or line
ends; at most 4096 bytes). Values in the SROM or OTP of the card are therefore
not seen. Values are converted with `bcm_strtoul(value, NULL, 0)` (prefix `0x`
hexadecimal, leading `0` octal, else decimal). Without the file none of these
variables exists; this is the normal case.

### Resource tables of the 4360 (chips 0x4360 and 0x4352 only)

Up/down timer table, entries `{resource number (8 bit), value for CC(0x628)}`:

| Chip rev | Table | Entries |
|---|---|---|
| < 4 | .rodata+0x27e788, 1 entry | {6, 0x00200001} |
| >= 4 | .rodata+0x27e790, 9 entries | {0, 0x00000001} {1, 0x00000001} {2, 0x00000001} {3, 0x00000001} {4, 0x00860002} {5, 0x00000000} {6, 0x00020001} {7, 0x00080001} {8, 0x00000000} |

No dependency table (the dependency masks keep their reset values). Chips
0xa9c4 and 0xaa06: no table at all.

Resource masks (`si_pmu_res_masks`):

| Chip | Rev | Chip status bit 5 | Minimum | Maximum |
|---|---|---|---|---|
| 0x4360, 0x4352 | <= 2 | any | 0 | 0 |
| 0x4360, 0x4352 | 3 | 0 | 0 | 0x1ff |
| 0x4360, 0x4352 | >= 4 | 0 | 0x103 | 0x1ff |
| 0x4360, 0x4352 | >= 4 | 1 | 0x103 | 0 |
| 0x4360, 0x4352 | 3 | 1 | 0 | 0 |
| 0xa9c4, 0xaa06 | >= 3 | 0 | 0 | 0x1ff |
| 0xa9c4, 0xaa06 | other | | 0 | 0 |

A mask of 0 means "leave the register as it is". Meaning of the resource bits
as far as the code shows it: the chip has 9 resources that the driver allows
(bits 0..8). Bit 8 is the power supply of the OTP (`si_pmu_otp_power`,
`si_pmu_is_otp_powered`). Bits 0, 1 and 8 are kept up permanently from chip
rev 4 on. Resource 6 has the longest up time in the rev < 4 table, resource 4
in the rev >= 4 table. Nothing else is revealed: the code has no HT clock
resource mask for the family (sub_010bed returns 0) and takes the power up
delay from constants.

### PMU registers and the bits used

| Register | Bits |
|---|---|
| `CC(0x600)` control | 0x80 ALP request enable, 0x100 HT request enable, 0x200 "no ILP on wait", 0x400 latch the PLL control registers |
| `CC(0x608)` status | 0x100 external low power oscillator available |
| `CC(0x60c)` resource state | 0x100 OTP powered |
| `CC(0x614)` timer | counts ILP clock periods |
| `CC(0x618)`, `CC(0x61c)` | minimum, maximum resource mask |
| `CC(0x620)` | selects the resource for `CC(0x624)` (dependency mask) and `CC(0x628)` (up/down timer) |
| `CC(0x66c)` | bit 31 start measuring, bits 0..12 ALP periods per 4 periods of the 32768 Hz oscillator |
| `CC(0x1e0)` clock control/status (every core has one) | 0x2 force HT, 0x10 HT request, 0x20000 HT available |
| `CC(0x10)` OTP status | 0x1000 OTP ready |
| `PMU_PLLCTL[2]` | bit 0 (set by the driver, purpose unknown), bits 4..6 divider mode (0 integer, 3 fractional), bits 7.. integer divider |
| `PMU_PLLCTL[3]` | fraction of the divider, in 1/2^24 |
| `PMU_PLLCTL[5]` | bits 8..15 divider of the backplane clock |
| `PMU_REGCTL[0]` | bit 1: 1 = radio supply off (si_pmu_rfldo) |
| `PMU_REGCTL[1]` | bits 0..3 voltage of LDO 4 (si_pmu_set_ldo_voltage) |

## Procedures

### si_pmu_init (.text+0x011993, name original)

Inputs: `sih`, `osh`. `[CC window]`.

1. PMU rev 1: `CC(0x600) = CC(0x600) & ~0x200`. PMU rev >= 2:
   `CC(0x600) = CC(0x600) | 0x200` (one read, one write). Rev 0: nothing.
2. (Chip 0x4329 rev 2 only: regulator control 2 and 3; not described.)

### si_pmu_chip_init (.text+0x015543, name original)

1. `si_pmu_otp_chipcontrol(sih, osh)`.
2. `si_pmu_sprom_enable(sih, osh, 0)`: nothing but a `[CC window]` switch.
3. Nothing chip specific for the 4360 (chips 0x4350, 0x4336, 0xa962 only), then
   the current core index is set once more to its own value (one window write
   pair).

Also called by `wlc_bmac_hw_up` for chip 0x4350 only.

### si_pmu_otp_chipcontrol / si_pmu_otp_regcontrol / si_pmu_otp_pllcontrol (.text+0x015474 / 0x015622 / 0x015a1d, names original)

`[CC window]`. For `i` from 0 to count-1 (count from the PMU capabilities, see
Data): if the variable `chipc<i>` / `reg<i>` / `pll<i>` exists: write `i` to
the address register and the value to the data register (`CC(0x650)`/`CC(0x654)`,
`CC(0x658)`/`CC(0x65c)`, `CC(0x660)`/`CC(0x664)`), two plain writes, the whole
register is replaced. `si_pmu_otp_pllcontrol` is called only from the PLL
initialisation of other chips: `pll<i>` has no effect on the 4360.

### si_pmu_measure_alpclk (.text+0x01246d, name original)

Result: crystal frequency in kHz, 0 if it cannot be measured.

1. PMU rev < 10: return 0 without access.
2. `[CC window]`. If `CC(0x608) & 0x100` is 0: return 0.
3. `CC(0x66c) = 0x80000000`; `osl_delay(1000)`; `n = CC(0x66c) & 0x1fff`;
   `CC(0x66c) = 0`.
4. Return `((n * 8192 + 50000) / 100000) * 100` (integer division).

### si_pmu_pll_init (.text+0x016f0e, name original)

Inputs: `sih`, `osh`, `xtalfreq` (kHz, not used for this family). `[CC window]`.
Chips 0x4360, 0x4352, 0xa9c4 with chip rev >= 3:
`si_set_bb_vcofreq_frac(sih, 960, 98)`. Chip rev < 3 and chip 0xaa06: nothing.

### sub_016dbd = si_set_bb_vcofreq_frac (.text+0x016dbd, name assigned)

Inputs: `sih`, `vco` (MHz), `frac` (units of 100 Hz). Sets the VCO of the
baseband PLL to `vco` MHz + `frac` * 100 Hz for a 40 MHz crystal. 4360 family:

1. Make ChipCommon the current core (window writes; not restored here).
2. If `CC(0x1e0) & 0x20000` (HT clock available) is set: return. The PLL is
   only reprogrammed while it is not in use.
3. `f = vco * 10000 + frac`; `rem = f mod 400000`; `mode = 3` if `rem != 0`
   else 0; `n = vco / 40`.
4. `si_pmu_pllcontrol(sih, 2, 0xffffffff, 1 | (mode << 4) | (n << 7))`.
5. If `mode != 0`: `si_pmu_pllcontrol(sih, 3, 0xffffffff, (rem * 2^24) / 400000)`
   (64 bit product, integer division).
6. `si_pmu_pllupd(sih)`.

For (960, 98): `PMU_PLLCTL[2] = 0x00000c31`, `PMU_PLLCTL[3] = 0x0000100e`, then
bit 0x400 of `CC(0x600)`. (Chip 0x4350 uses other field positions: not
described.)

### si_pmu_res_init (.text+0x014cab, name original)

Inputs: `sih`, `osh`. `[CC window]`. `n_res` = PMU capabilities bits 8..12.

1. Variable `rmin` present: .bss+0x2f0 = 1, .bss+0x2f8 = value. Variable `rmax`
   present: .bss+0x2f1 = 1, .bss+0x2fc = value.
2. Up/down timers from the table of the chip (Data), from the last entry to the
   first: `CC(0x620) = resource`, `CC(0x628) = value`. Chip rev 3: only
   `CC(0x620) = 6`, `CC(0x628) = 0x00200001`.
3. For `i` = 0 .. n_res-1: variable `r<i>t` present: `v` = value; if PMU rev
   >= 13 and `v < 0x10000`: `v = ((v >> 8) & 0xff) << 16 | (v & 0xff)`;
   `CC(0x620) = i`, `CC(0x628) = v`.
4. (Dependency table: none for the 4360.)
5. For `i` = 0 .. n_res-1: variable `r<i>d` present: `CC(0x620) = i`,
   `CC(0x624) = value`.
6. `(min, max) = si_pmu_res_masks(sih)`.
7. `min = min | si_pmu_res_deps(sih, osh, cc, min, all = 0)`.
8. Chips 0x4360 and 0x4352, only if chip status bit 5 is 0:
   * chip rev <= 3: `CC(0x660) = 6`, `CC(0x664) = 0x09048562`, `CC(0x660) = 0xe`,
     `CC(0x664) = 0x09048562`, `si_pmu_pllupd(sih)`.
   * chip rev >= 4: `CC(0x650) = 1`, `CC(0x654) = CC(0x654) | 0x800`; then
     `PMU_PLLCTL[6] = 0x080004e2`, `[7] = 0x0000000e`, `[0xe] = 0x080004e2`,
     `[0xf] = 0x0000000e` (each as address write, data write, in this order);
     `si_pmu_pllupd(sih)`.
9. If `max != 0`: `max = max | min`; `CC(0x61c) = CC(0x61c) | max`. Else if
   `min != 0`: `CC(0x61c) = CC(0x61c) | min`.
10. If `min != 0`: `CC(0x618) = min`.
11. If `max != 0`: `CC(0x61c) = max`.
12. Chips 0x4360 and 0x4352 with chip rev <= 3: `v = corereg(reg, 0, 0)`,
    `corereg(reg, 0xffffffff, v | 0x10)` with `reg` = register 0x1e0 of the
    core with index 3 (on this chip the PCIe Gen2 core: `PCIE(0x1e0)`): the HT
    clock is requested for that core.
13. `osl_delay(2000)`.

Chip rev 3, chip status bit 5 = 0, no variables: steps 2, 8, 9, 11, 12, 13; the
minimum resource mask is not written.

### sub_0111b0 = si_pmu_res_masks (.text+0x0111b0, name assigned)

Inputs: `sih`; outputs `min`, `max`. No hardware access for the 4360 family.
Values: table in Data. Afterwards: if .bss+0x2f0 is 1 `min` = .bss+0x2f8; if
.bss+0x2f1 is 1 `max` = .bss+0x2fc.

### sub_0118f2 = si_pmu_res_deps (.text+0x0118f2, name assigned)

Inputs: `sih`, `osh`, `cc`, mask `rsrcs`, flag `all`. Result: the dependencies.
For `i` = 0 .. 30 with bit `i` set in `rsrcs`: `CC(0x620) = i`,
`deps |= CC(0x624)`. If `all` and `deps != 0`:
`deps |= si_pmu_res_deps(.., deps, 1)`.

### si_pmu_swreg_init (.text+0x0156fb, name original)

Nothing chip specific for the 4360; calls `si_pmu_otp_regcontrol(sih, osh)`.

### si_pmu_chipcontrol / si_pmu_regcontrol / si_pmu_pllcontrol (.text+0x01116b / 0x011126 / 0x011032, names original)

Inputs: `sih`, `reg`, `mask`, `val`. `corereg(address register, 0xffffffff, reg)`,
then result = `corereg(data register, mask, val)`. A modification is therefore:
read, write, read of the address register, then read, write, read of the data
register; a query (`mask = val = 0`): the same three accesses of the address
register, one read of the data register.

### si_pmu_pllupd (.text+0x011015, name original)

`corereg(CC(0x600), 0x400, 0x400)`.

### si_pmu_alp_clock (.text+0x011c28, name original)

`[CC window]`, no register access. 4360 family: 40000000 if chip status bit 0
is set, else 20000000. `si_alp_clock(sih)` returns this value.

### si_pmu_ilp_clock (.text+0x012d28, name original)

If .bss+0x2f4 is 0: `[CC window]`; `a = CC(0x614)`; if a second read of
`CC(0x614)` differs, `a` = a third read; `osl_delay(10000)`; `b` the same way;
.bss+0x2f4 = `(b - a) * 100`. Return .bss+0x2f4. Not chip specific.
`si_ilp_clock(sih)` returns this value.

### si_pmu_si_clock, si_pmu_cpu_clock (.text+0x0162b5, 0x0167c8, names original) and sub_011a64 = si_pmu1_cpuclk0

Backplane clock in Hz. `[CC window]`; `CC(0x660) = 5`; `d = (CC(0x664) >> 8) &
0xff`; result `(960000 / d) * 1000` (integer division; `d = 0` is a division
fault). `si_pmu_cpu_clock` returns the same value for the family.
`si_clock(sih)` returns it, framed by the interrupt callbacks of `sih`.
(960000 kHz is the VCO frequency sub_010b09 returns for the family.)

### si_pmu_mem_clock (.text+0x016639, name original)

No callers. PMU rev < 5: as `si_pmu_si_clock`. Else `[CC window]` and
sub_011eb8 (si_pmu5_clock) with register base 12, divider 2: for `k` = 12, 13,
14: `CC(0x660) = k`, read `CC(0x660)`, `c[k] = CC(0x664)`; `alp` =
si_pmu_alp_clock / 1000000; result
`((alp * (c[14] >> 20) * ((c[12] >> 24) & 0xf)) / ((c[12] >> 20) & 0xf) /
((c[13] >> 8) & 0xff)) * 1000000`.

### si_pmu_get_bb_vcofreq (.text+0x014b7b, name original)

Inputs: `sih`, `osh`, `xf` crystal frequency in MHz (the caller passes 40).
Result: VCO frequency in units of 100 Hz, 0 on overflow.

1. `c2 = si_pmu_pllcontrol(sih, 2, 0, 0)`; `mode = (c2 >> 4) & 7`; `n = c2 >> 7`.
2. If `mode != 0`: `fr = si_pmu_pllcontrol(sih, 3, 0, 0)` (all 32 bit), else no
   access and `fr = 0`.
3. `ref = xf * 10000`. `part = (ref * fr + 0x800000) >> 24` (64 bit
   intermediate, result cut to 32 bit) if `mode != 0`, else 0.
4. If `ref > (0xffffffff - part) / n` (signed comparison; `n = 0` is a
   division fault): return 0. Else return `part + n * ref`.

`c2 = 0xc01`: 9600000. `c2 = 0xc31`, `fr = 0x100e`: 9600098.

### si_pmu_fast_pwrup_delay (.text+0x015eae, name original)

`[CC window]`, no register access. Microseconds: chips 0x4360 and 0x4352: 1500
for chip rev < 4, 3000 else; chips 0xa9c4 and 0xaa06: 3700.
`si_clkctl_fast_pwrup_delay(sih)` returns the lower 16 bit, framed by the
interrupt callbacks.

### si_pmu_otp_power (.text+0x012a8d, name original)

Inputs: `sih`, `osh`, `on`.

1. `si_is_otp_disabled(sih)` (always 0 for the family); `[CC window]`.
2. `rsrc = 0x100`; `deps = si_pmu_res_deps(sih, osh, cc, rsrc, all = 1)`;
   `(min, max) = si_pmu_res_masks(sih)`; `deps &= ~min`.
3. `on`: `CC(0x618) = CC(0x618) | rsrc | deps`; `osl_delay(1000)`;
   `SPINWAIT((CC(0x60c) & rsrc) == 0, 20000)`.
   Off: `CC(0x618) = CC(0x618) & ~(rsrc | deps)`.
4. `SPINWAIT((CC(0x10) & 0x1000) != (on ? 0x1000 : 0), 3000)`.

`si_otp_power(sih, on)` calls it and then `osl_delay(1000)`.

### si_pmu_is_otp_powered (.text+0x011763, name original)

`[CC window]`; result bit 8 of `CC(0x60c)`.

### si_pmu_rfldo (.text+0x01e7e8, name original)

Inputs: `sih`, `on`. Chips 0x4360, 0x4352, 0xaa06:
`si_pmu_regcontrol(sih, 0, 2, on ? 0 : 2)`. Other chips: nothing.

### si_pmu_set_ldo_voltage (.text+0x010d95, name original)

Inputs: `sih`, `osh`, `ldo`, `voltage`. 4360 family: `ldo == 4`:
`corereg(CC(0x658), 0xffffffff, 1)`, `corereg(CC(0x65c), 0xf, voltage & 0xf)`.
Other `ldo`: `corereg(CC(0x658), 0xffffffff, 0)` and one read of `CC(0x65c)`.
Its callers are PHY code of other PHY types.

### si_pmu_set_switcher_voltage (.text+0x010cad, name original)

Inputs: `sih`, `osh`, `bb`, `rf`. `[CC window]`. `CC(0x658) = 1`,
`CC(0x65c) = (bb & 0x1f) << 22`, `CC(0x658) = 0`, `CC(0x65c) = (rf & 0x1f) << 14`
(plain writes, the registers are replaced). No callers.

### si_pmu_spuravoid, si_pmu_spuravoid_isdone (.text+0x014a78, 0x01491d, names original)

4360 family: ChipCommon through the fixed window; `CC(0x600) = CC(0x600)` (one
read, one write of the same value, bit 0x400 is not added). No PLL register is
changed; the spur avoidance mode argument has no effect. Not called on the
4360 path.

### si_pmu_pll_off_PARR (.text+0x013cba, name original)

4360 family: stores `CC(0x618)`, `CC(0x61c)`, `CC(0x1e0)` (read in this order)
through the three pointer arguments and returns; the matching
`si_restore_core` is skipped on this path. No callers.
`si_pmu_pllreset` (.text+0x016150): returns at once for the family.

### si_pmu_waitforclk_on_backplane (.text+0x012543, name original)

Inputs: `sih`, `osh`, `clk` (mask), `delay` (us). `[CC window]`; if `delay != 0`:
`SPINWAIT((CC(0x608) & clk) != clk, delay)`; the window is switched back; then
offset 0x608 is read once more **through the movable window**, i.e. from the
core that is current again (the MAC when called from the driver), and
`value & clk` is returned. No callers.

### si_pmu_force_ilp, si_pmu_enb_ht_req (.text+0x0122cd, 0x012255, names original)

`[CC window]`; `old = CC(0x600)`. `si_pmu_force_ilp(force)`: write
`old & ~0x180` if `force`, else `old | 0x180`. `si_pmu_enb_ht_req(enable)`:
write `old | 0x100` if `enable`, else `old & ~0x100`. Both return `old`. No
callers.

### si_clkctl_xtal (.text+0x01e534, name original)

Inputs: `sih`, `what` (bit 0 crystal, bit 1 PLL), `on`. Result 0, or -1.

1. Bus type PCI with bus core id 0x83c or 0x820: return -1, no access. This is
   the case in every call after the core scan of the attach. (Bus type 2:
   return 0; other bus types: return -1; both without access.)
2. Bus type PCI otherwise: `in = PCICFG(0xb0)`, `out = PCICFG(0xb4)`,
   `outen = PCICFG(0xb8)` (32 bit each). If `on` and `in & 0x40`: return 0.
3. `outen |= 0x40` if `what & 1`; `outen |= 0x80` if `what & 2`.
4. `on`:
   * only if `what & 1`: `out |= 0x40`; `out |= 0x80` if `what & 2`;
     `PCICFG(0xb4) = out`; `PCICFG(0xb8) = outen`; `osl_delay(1000)`.
   * then, if `what & 2`: `PCICFG(0xb4) = out & ~0x80`; `osl_delay(2000)`.

   Off: `out &= ~0x40` if `what & 1`; `out |= 0x80` if `what & 2`;
   `PCICFG(0xb4) = out`; `PCICFG(0xb8) = outen`.
5. Return 0.

`what = 3`, `on`, all three registers 0 (seq 10..15): `PCICFG(0xb4) = 0xc0`,
`PCICFG(0xb8) = 0xc0`, 1 ms, `PCICFG(0xb4) = 0x40`, 2 ms.

### si_clkctl_init (.text+0x0204b8, name original)

1. Capabilities bit 18 (0x40000) clear: return, no access.
2. ChipCommon through the fixed window. ChipCommon rev >= 10:
   `CC(0xc0) = (CC(0xc0) & 0xffff) | 0x40000`.
3. `div = 4 * ((CC(0xc0) >> 16) + 1)`; `f = 19800000 / div` (990000).
4. `CC(0xb0) = (f * 150 + 999999) / 1000000` (149);
   `CC(0xb4) = (f * 200 + 999999) / 1000000` (198); `osl_delay(20000)`.

### si_clkctl_cc (.text+0x0214d8) and sub_02105c = _si_clkctl_cc (.text+0x02105c)

Inputs: `sih`, `mode` (0 fast, 2 dynamic). Result: 1 if `mode` is 0, else 0.
ChipCommon rev 43, fixed window:

* mode 0: `CC(0x1e0) = CC(0x1e0) | 2`; `SPINWAIT((CC(0x1e0) & 0x20000) == 0, 20000)`.
* mode 2: `CC(0x1e0) = CC(0x1e0) & ~2`; `SPINWAIT((CC(0x1e0) & 0x20000) != 0, 20000)`.

Not called when the chip has a PMU.

## Verification

Against `re-out/up-trace.txt`: the attach sequence seq 338..386 (si_pmu_init:
`CC(0x600)` 0 -> 0x200; si_pmu_pll_init: only the read of `CC(0x1e0)` = 0xf0000,
the model reports HT available; si_pmu_res_init: steps 2, 8, 9, 11, 12 with the
values above; si_pmu_swreg_init: window switches only), the repetition in
`wlc_bmac_hw_up` (seq 1003..1022), si_pmu_rfldo (seq 938, 1185),
si_pmu_is_otp_powered (seq 547), si_pmu_get_bb_vcofreq (seq 25618, result
9600000), si_clkctl_xtal (seq 10..15). Order of accesses and values agree.

With the analyst's harness `re-out/analysis/pmu/harness.py` (the unmodified
object in the emulator, single calls after an attach, variants of the model):
every procedure above was called and its accesses compared with the text,
including the branches the traces do not reach: HT clock not available
(`PMU_PLLCTL[2] = 0xc31`, `[3] = 0x100e`, PLL update), `xtalfreq = 0`
(measurement), a file `nvram.txt` with `chipc0`, `reg1`, `rmin`, `rmax`, `r6t`,
`r7t`, `r2d`, `pll2` (all applied as described, `pll2` and `chipc3` not),
polling time-outs (2001 and 301 reads), chip rev 4 for the power up delay,
capabilities with bit 18 for si_clkctl_init. Call log of attach, up, down, up:
`re-out/analysis/pmu/life-default.txt`.

si_pmu_res_init was also executed for chip rev 4 (table written from resource
8 down to 0, dependencies of resources 0, 1, 8 read, `PMU_CHIPCTL[1]`,
`PMU_PLLCTL[6]`, `[7]`, `[0xe]`, `[0xf]`, minimum mask 0x103, no access to
`PCIE(0x1e0)`) and with chip status bit 5 set (rev 3: only step 2, 12, 13; rev
4: steps 2, 7, `CC(0x61c) |= 0x103`, `CC(0x618) = 0x103`, 13).

Not verified by execution, read from the disassembly only: the chips 0x4352,
0xa9c4, 0xaa06; si_clkctl_xtal switching off; chip rev < 3.

Two runs with a model whose configuration space has a PCI Express capability
did not finish and were stopped by the system (memory); they were not
repeated. That si_clkctl_xtal is skipped at the start of the attach of a real
card is therefore a statement from the code of si_doattach only.

Differences between model and hardware that matter here: the model reports
chip status 0 (so the ALP clock is reported as 20 MHz although the PLL code
assumes a 40 MHz crystal), PMU capabilities 0x10a22b11 (rev 17, 11 resources,
2 chip control, 2 regulator control registers), capabilities without bit 18,
and a configuration space without PCI Express capability (this is why
si_clkctl_xtal acts at seq 10; a PCIe card has the capability, then the call
is skipped).

Note added after this verification: the model was corrected on the points of
the last paragraph (chip status 1, configuration space with PCI Express
capability, PLL clock available only while a core requests it), the traces
were made again, and the sequence numbers given above belong to the older
trace. The statements were checked again by the comparison tests of the open
implementation (`pmu-*` scenarios of `tools/re/scenarios.py`, chip revisions 3
and 4, chip status 0x01, 0x21, 0x20, PLL clock in use and not in use): 16 to
20 calls per scenario, no difference.

## Answers to the questions of the implementation

Questions: [`../questions/pmu.md`](../questions/pmu.md).

1. *Division by 0* (si_pmu_si_clock, si_pmu_get_bb_vcofreq): the object does
   not test the divider; with a divider field of 0 the processor raises a
   division fault. Returning 0 instead is a deviation that the hardware cannot
   see. What the registers hold after a reset of the real chip is not known
   (see "Open questions").
2. *si_pmu_get_bb_vcofreq, step 4*: both divisions are unsigned divisions of
   32 bit; the comparison that follows is a signed one. (Checked in the
   disassembly.)
3. *si_pmu_res_deps*: a loop in the place of the recursion is equivalent as
   long as the accesses are the same; the recursion of the object has no
   limit of its own.
4. *si_pmu_otp_power(off) and resource 8*: the only callers are the OTP
   functions (`otp_init`, `otp_read_word`, `otp_read_region`, through
   `si_otp_power`) and `si_eci_init`. The OTP functions switch the power on
   only if si_pmu_is_otp_powered says it is off, and switch it off again at
   their end only in that case: they restore what they found. Where resource
   8 is in the minimum mask (chip rev 4 after si_pmu_res_init) the OTP is
   reported as powered and neither call is made. In the traces the function
   is never executed (the model reports the OTP as powered). The sentence
   "kept up permanently" describes si_pmu_res_init, not an invariant.
5. *si_pmu_measure_alpclk*: steps 3 and 4 are reached only with bit 0x100 of
   `CC(0x608)` set; no scenario does that yet.
6. *Register number of the accessors*: not checked by the object either.
7. *Chip specific or not*: si_pmu_get_bb_vcofreq, si_pmu_otp_power and
   si_pmu_is_otp_powered all select by chip id (a list of about 25 ids each);
   the descriptions above are those of the branch for 0x4360, which 0x4352,
   0xa9c4 and 0xaa06 share. (Checked in the disassembly.)
8. *Identification*: the layout of `CC(0x00)` and the revision of ChipCommon
   belong to the specification of the backplane (`chip`, not written yet).
   `CC(0x00)`: bits 0..15 chip id, 16..19 chip revision, 20..23 package.

## Open questions

* Chip status, PMU revision and capabilities, ChipCommon capabilities of the
  real card are unknown; they select branches above (ALP clock, r<n>t format,
  number of indirect registers, si_clkctl_init).
* Meaning of the values 0x09048562 (`PMU_PLLCTL[6]`, `[0xe]`), of the fields of
  `PMU_PLLCTL[2]` bit 0 and of chip status bit 5: unknown.
* Names and functions of the resources 0..7: unknown.
* Whether bit 0x400 of `CC(0x600)` clears itself in the hardware: unknown; the
  driver never clears it.
