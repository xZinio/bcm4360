# Chip attach: enumeration, the backplane handle, the PCIe bridge, OTP and the other lower-layer rests

Analyst `chip-attach`, 2026-11-05. Extends [chip.md](chip.md) (which already
describes the accessors, the core switching, the wrapper, the GPIOs and most
PCIe helpers), [pmu.md](pmu.md), [srom-otp.md](srom-otp.md) and
[utils.md](utils.md). Purpose: everything of Broadcom's **lower** layers
that still runs in `tools/re/hybrid.py`'s session (the default hybrid build,
every open layer on), described so that a driver without Broadcom's code can
find the cores, fill the backplane handle, set up the PCIe bridge and read
OTP. Where a function is already fully described in another specification,
this one says so and adds only what was missing or what the real card showed.

Chip 0x4360 revision 3 on PCI (bus type 1), AXI backplane, PCIe Gen2 bus
core (0x83c revision 1), ChipCommon revision 43, PMU revision 17. Other chips,
buses and bus cores: one line each where the code branches.

**Sources.** The object read in the decompilation and disassembly; the
emulator (object alone and hybrid core); and, new here, the **real card**:
the mmiotrace capture of card test 2026-11-03 09:53
(`/var/log/bcm4360-card-test-20261103-095328/capture-broadcom`), which
contains the complete attach of the driver - the chip identity, the whole
enumeration ROM, the ChipCommon capability registers and the OTP reads - and
the configuration space dump of the running card (`peek.txt` of the same
test). Every statement says how it was checked; "(code)" = read from the
code only.

## Scope

Every function of the lower layers (`aiutils.c`, `siutils.c`, `nicpci.c`,
`bcmotp.c`, `hndpmu.c`, `bcmsrom.c`, `bcm_mpool.c`, `bcm_notif.c`,
`bcmutils.c`, `bcmwifi_channels.c`) of which Broadcom's code still executes
in the hybrid session (measured: `re-out/analysis/chip-attach/lower.py`,
coverage per function with callers, for `hybrid.SESSION` and for the longer
`hybrid.REAL` steps with glue, scans, interface down/up, suspend/resume),
plus the lower-layer functions the task named that do not run. "Runs" = the
session steps in which it executed; "Section" = where it is described (A-D
below, or another specification).

| Function | .text offset | Size | Name | Runs | Section |
|---|---|---|---|---|---|
| `si_attach` | 0x023844 | 182 | original | attach | A2 (hybrid's `card.c` wraps it) |
| `sub_022b10` | 0x022b10 | 3115 | assigned: `si_doattach` | attach | A3 |
| `ai_scan` | 0x001f1d | 1491 | original | attach | A4 |
| `sub_00133f` | 0x00133f | 66 | assigned: `get_erom_ent` | attach | A4 |
| `sub_001381` | 0x001381 | 266 | assigned: `get_asd` | attach | A4 |
| `si_getdevpathintvar` | 0x0218b6 | 106 | original | attach | A5 |
| `si_coded_devpathvar` | 0x01f0a7 | 276 | original | attach | A5, srom-otp.md |
| `sub_0217e2` | 0x0217e2 | 105 | assigned: `si_devpathvar` | attach | A5, srom-otp.md |
| `si_devpath` | 0x01efe9 | 190 | original | attach | A5, srom-otp.md |
| `si_ldo_war` | 0x01e6bb | 301 | original | attach | A3 step 3, chip.md |
| `si_detach` | 0x02171d | 197 | original | detach | A6 (hybrid's `card.c` wraps it) |
| `srom_var_deinit` | 0x004510 | 13 | original | detach | A6 |
| `si_osh` | 0x01dec7 | 10 | original | attach, up, detach | chip.md |
| `si_register_intr_callback` | 0x01dedb | 44 | original | attach | chip.md |
| `si_deregister_intr_callback` | 0x01df07 | 14 | original | detach | chip.md |
| `sub_021d1d` | 0x021d1d | 54 | assigned: `si_cc_corereg` | attach, down | chip.md; A3, D1 |
| `ai_corereg` | 0x001d27 | 502 | original | attach, down (through `sub_021d1d` only) | chip.md |
| `pcicore_find_pci_capability` | 0x018156 | 390 | original | attach | B2, chip.md |
| `pcicore_init` | 0x018b68 | 245 | original | attach | B2 |
| `pcicore_attach` | 0x019154 | 497 | original | attach | B3 |
| `si_pci_fixcfg` | 0x021e58 | 226 | original | attach, up | B4 |
| `pcicore_hwup` | 0x019345 | 760 | original | attach, up | B4 |
| `si_pcieobffenable` | 0x01ec42 | 45 | original | attach | B5 |
| `pcie_obffenable` | 0x017fe1 | 127 | original | attach | B5 |
| `si_pcie_ltr_war` | 0x01f905 | 146 | original | attach, up | B6 |
| `si_pcieltrenable` | 0x01ec6f | 45 | original | attach, up | B6 |
| `pcie_ltrenable` | 0x018060 | 123 | original | attach, up | B6 |
| `si_pci_setup` | 0x0215bd | 305 | original | up | B7 |
| `si_pci_up` | 0x0213f8 | 224 | original | up | B8 |
| `pcicore_up` | 0x0196d0 | 228 | original | up | B8 |
| `sub_01963d` | 0x01963d | 147 | assigned: `pcie_devctl_mrrs_set` | up | B8 |
| `si_pci_down` | 0x02137e | 122 | original | attach, down | B9 |
| `pcicore_down` | 0x019090 | 65 | original | attach, down, scan | B9 |
| `si_pci_sleep` | 0x01ecf6 | 28 | original | detach, suspend | B10 |
| `pcicore_sleep` | 0x019015 | 123 | original | detach, suspend | B10 |
| `pcicore_deinit` | 0x018b4c | 28 | original | detach | B11 |
| `pcicore_pmecap_fast` | 0x0182dc | 66 | original | attach | B12 |
| `si_survive_perst_war` | 0x01f997 | 282 | original | (open: `hybrid/si_perst.c`) | B13 |
| `si_pcie_get_request_size` | 0x01edf6 | 37 | original | not reached | B14 |
| `pcie_get_request_size` | 0x017ca7 | 49 | original | not reached | B14 |
| `si_pci_pmeclr` | 0x01ef4c | 18 | original | glue, resume with wake-on-WLAN only | B15, chip.md |
| `pcie_disable_TL_clk_gating`, `pcie_set_L1_entry_time` | 0x0186e1, 0x018a68 | 54, 118 | original | not reached (chip rev < 3) | B16 |
| `otp_read_word` | 0x003362 | 157 | original | attach (PHY attach) | C, srom-otp.md |
| `otp_init` | 0x00329f | 195 | original | attach | C, srom-otp.md |
| `sub_003d6a` | 0x003d6a | 1950 | assigned: `ipxotp_init` | attach | C, srom-otp.md |
| `sub_003983` | 0x003983 | 123 | assigned: `ipxotp_otpr` | attach | C, srom-otp.md |
| `sub_0038d3` | 0x0038d3 | 176 | assigned: `ipxotp_read_bit` | attach | C, srom-otp.md |
| `sub_0039fe` | 0x0039fe | 85 | assigned: `ipxotp_read_word` | attach | C, srom-otp.md |
| `si_is_otp_disabled` | 0x01e29b | 180 | original | attach | C, srom-otp.md |
| `si_is_otp_powered` | 0x01e987 | 23 | original | attach | C, srom-otp.md |
| `si_is_sprom_available` | 0x01f509 | 483 | original | attach | C, srom-otp.md |
| `si_chipcontrl_read`, `si_chipcontrl_restore` | 0x01fdea, 0x01fe30 | 70, 70 | original | attach | srom-otp.md |
| `si_pmu_chip_init` | 0x015543 | 223 | original | attach | pmu.md |
| `si_pmu_otp_chipcontrol` | 0x015474 | 207 | original | attach | pmu.md |
| `si_pmu_sprom_enable` | 0x0116d5 | 142 | original | attach | pmu.md |
| `si_pmu_swreg_init` | 0x0156fb | 802 | original | attach | pmu.md |
| `si_pmu_otp_regcontrol` | 0x015622 | 217 | original | attach | pmu.md |
| `si_clkctl_init` | 0x0204b8 | 355 | original | attach, up | pmu.md |
| `si_clkctl_fast_pwrup_delay` | 0x020300 | 440 | original | up | pmu.md |
| `si_alp_clock` | 0x01f1d5 | 63 | original | attach (PHY attach) | pmu.md |
| `si_pmu_res_req_timer_clr` | 0x0243cc | 68 | original | down | D1 |
| `bcm_mpm_init`, `bcm_mpm_deinit` | 0x002a0c, 0x00270d | 272, 104 | original | attach, detach | D2 |
| `bcm_mpm_create_prealloc_pool`, `bcm_mpm_create_heap_pool` | 0x002995, 0x002970 | 119, 37 | original | attach | D2 |
| `bcm_mpm_delete_prealloc_pool`, `bcm_mpm_delete_heap_pool` | 0x002702, 0x0026f7 | 11, 11 | original | detach | D2 |
| `sub_0027e0`, `sub_002661` | 0x0027e0, 0x002661 | 400, 150 | - (static pool create / delete) | attach, detach | D2 |
| `bcm_mp_alloc`, `bcm_mp_free` | 0x002775, 0x00260b | 107, 86 | original | attach, detach | D2 |
| `bcm_notif_attach`, `bcm_notif_detach`, `sub_002d65` | 0x002dbd, 0x002db2, 0x002d65 | 179, 11, 77 | original / - | attach, detach | D2 |
| `bcm_notif_create_list`, `bcm_notif_delete_list` | 0x002d1e, 0x002c16 | 71, 135 | original | attach, detach | D2 |
| `bcm_notif_add_interest`, `bcm_notif_remove_interest` | 0x002c9d, 0x002b78 | 129, 158 | original | attach, detach | D2 |
| `pktq_init`, `pktq_flush` (REAL also `pktq_penq`, `pktq_mdeq`, `pktsegcnt`, `pkttotlen`) | 0x00b520, 0x00c0b1 | 83, 94 | original | attach, down | D3 |
| `bcm_iovar_lencheck` | 0x00a957 | 79 | original | iovar sets | D3 |
| `wf_chspec_aton` | 0x00cc72 | 950 | original | channel steps (called by the test harness only) | D4 |
| `sub_00cc2a`, `sub_00c6c8` | 0x00cc2a, 0x00c6c8 | 72, 62 | - | (inside `wf_chspec_aton`) | D4 |

Not in the list because the open code has taken them over (hybrid
`takeover.txt`): the accessors and core switching of chip.md, `si_ldo_war`
and `si_pci_war16165` outside the attach, the PMU functions of pmu.md,
`srom_var_init`, the NVRAM functions, the chanspec helpers. `si_ldo_war`
appears above because `si_doattach` runs before the hybrid knows the new
handle, so its adapter falls back to Broadcom's body there.

## Interface

The open functions for this part do not exist yet. Names follow
`open/include/bcm4360/si.h` (prefix `bcm4360_si_` instead of `si_`); the
implementers fix them:

| Entry point | Arguments, result | Open function (proposed) |
|---|---|---|
| `si_attach(devid, osh, regs, bustype, sdh, &vars, &varsz)` | device id, OS handle, mapped BAR0, bus type (1), bus handle, out: variable buffer and its length; result: the handle or NULL | `bcm4360_si_attach` (fills `struct bcm4360_si`, `struct bcm4360_chip_info`, `struct bcm4360_srom_card`) |
| `si_detach(sih)` | - | `bcm4360_si_detach` |
| `si_pci_fixcfg(sih)` | result 0 or -1 | `bcm4360_si_pci_fixcfg` |
| `si_pci_setup(sih, coremask)` | - | `bcm4360_si_pci_setup` |
| `si_pci_up(sih)`, `si_pci_down(sih)`, `si_pci_sleep(sih)` | - | `bcm4360_si_pci_up/down/sleep` |
| `si_pcie_ltr_war(sih)` | - | `bcm4360_si_pcie_ltr_war` |
| `si_pcieobffenable(sih, mask, val)`, `si_pcieltrenable(sih, mask, val)` | result: the field as read after the optional write | `bcm4360_si_pcieobffenable/ltrenable` |
| `si_pcie_get_request_size(sih)` | result bytes or 0 | `bcm4360_si_pcie_get_request_size` |
| `si_pmu_res_req_timer_clr(sih)` | - | `bcm4360_pmu_res_req_timer_clr` |
| `otp_read_word(sih, wn, *data)` | result 0 or < 0 | `bcm4360_otp_read_word` (already in `phy_env.h`) |

The MAC layer reaches the PCIe functions through `mac_env.h`
(`bcm4360_bmac_env_pci_fixcfg`, `_pci_setup`, `_pci_up`, `_pci_down`,
`_pcie_ltr_war`, `_survive_perst_war`, `_ldo_war`,
`_pmu_res_req_timer_clr`); `bmac_attach.h` adds
`bcm4360_attach_env_si_pcie_get_request_size`.

## A. Attach: the backplane handle and the enumeration ROM

### A1. The handle (`si_info_t`, 0x7d8 bytes)

chip.md ("Data", "Who uses `si_info_t`") lists every field and shows by
measurement that no code outside the three backplane files writes the
structure and none reads beyond offset 0x48. An open driver needs:

* the **public part** (offsets 0x00..0x4c), which the other layers read and
  which the open layers take as `struct bcm4360_si` (`si.h`),
  `struct bcm4360_chip_info` (`pmu.h`) and `struct bcm4360_srom_card`
  (`srom.h`), and
* the **core tables** of the private part, which only the backplane
  functions use: number of cores, per core its id, the two enumeration words
  CIA/CIB, the register base, the wrapper base (and two region fields no
  session code reads), the current core index, the interrupt callbacks and
  the PCIe bridge state.

How each public field gets its value, with the value of the real card
(verified: the capture's register reads; the object's `si_attach` in the
emulator on a card model with the real enumeration ROM and the real identity
registers, `re-out/analysis/chip-attach/real_erom.py`, gave exactly these
fields; boardrev/boardflags there come from the model's SROM):

| Offset | Field (`hybrid.h` name) | Source | Real card |
|---|---|---|---|
| 0x00 | `socitype`, backplane type | bits 28..31 of the chip id register `CC(0x00)` | 1 (AXI) |
| 0x04 | `bustype` | the `bustype` argument | 1 (PCI) |
| 0x08 | `buscoretype` | the bus core found in A3 step 7 | 0x83c |
| 0x0c | `buscorerev` | that core's revision (CIB bits 24..31) | 1 |
| 0x10 | `buscoreidx` | that core's index | 3 |
| 0x14 | `ccrev` | revision of core 0 (CIB bits 24..31) | 43 |
| 0x18 | `cccaps` | `CC(0x04)` | 0x58680001 |
| 0x1c | `cccaps_ext` | `CC(0xac)`, read only if ccrev > 34 | 0x00000003 |
| 0x20 | `pmurev` | bits 0..7 of `CC(0x604)`, read only if `cccaps` bit 28 | 17 |
| 0x24 | `pmucaps` | `CC(0x604)` (same condition) | 0x10a22b11 |
| 0x28 | `boardtype` | A3 step 12 | 0x117 |
| 0x2c | `boardrev` | `getintvar(vars, "boardrev")` | from the SROM |
| 0x30 | `boardvendor` | A3 step 12 | 0x106b |
| 0x34 | `boardflags` | `getintvar(vars, "boardflags")` | from the SROM |
| 0x38 | `boardflags2` | **never written on this path: 0** | 0 |
| 0x3c | `chip` | bits 0..15 of `CC(0x00)` | 0x4360 |
| 0x40 | `chiprev` | bits 16..19 of `CC(0x00)` | 3 |
| 0x44 | `chippkg` | bits 20..23 of `CC(0x00)` | 0 |
| 0x48 | `chipst` | `CC(0x2c)`, read only if ccrev > 10 | 0x00000a4d |
| 0x4c | byte: package is 14 or 15 | from `chippkg` | 0 |

The real card's chip id register reads **0x15034360** (chip 0x4360, revision
3, package 0, AXI): this settles that the card is revision 3, as the model
assumed, so every branch "chip revision < 3" (B16, `wlc_bmac_4360_pcie2_war`)
and "chip revision >= 4" (`pcie_ctrl_4360c0_war`) is not taken on it.

**Where the real card differs from the card model** (`tools/re/bcm4360.py`;
values of the model in brackets) - all of them are read by other layers,
so a session on the model does not run the code that depends on them:

* `cccaps` 0x58680001 [0x50280000]: on the card bits 0, 22 and **27** are
  set as well. Bit 27 is what `si_backplane64` returns (chip.md: "64-bit
  backplane"), which `dma_attach`'s address-width decision reads.
  Bits 19..21 (the OTP size code, 5) and 28 (PMU), 30 are the same.
* `cccaps_ext` 3 [0]: bit 0 is one of the conditions of
  `wlapi_is_eci_coex_enabled` (`phy_env.h`), which requires it **clear**.
* `chipst` 0xa4d [0x1]: bit 0 (40 MHz crystal) is the same, bit 5 (pmu.md:
  no PLL programming / no maximum resource mask when set) is clear on both;
  bits 2, 3, 6, 9, 11 are set only on the card (no reader known in the
  session).
* The enumeration ROM: the same five cores at the same register addresses,
  but other wrapper addresses for cores 2..4 and many more entries (A4).

### A2. si_attach (.text+0x023844, name original)

1. Allocate 0x7d8 bytes for the handle (failure: result NULL).
2. Run `si_doattach` (A3) on it with all arguments. If it fails: free the
   0x7d8 bytes, result NULL.
3. Store the variable buffer pointer `*vars` (0 if `vars` is NULL) at
   `sih+0xa8` and its length `*varsz` (0 if NULL) at `sih+0xb0`. Result: the
   handle.

No hardware access of its own (code; emulator: the accesses are all
`si_doattach`'s).

The hybrid's `card.c` takes `si_attach` over only to note the BAR0 mapping
for the open code (window 1 = BAR0, wrapper window BAR0+0x1000, PCIe
BAR0+0x2000, ChipCommon BAR0+0x3000) and then calls Broadcom's body; its
`si_detach` calls Broadcom's body and forgets the card. An open
implementation replaces both.

### A3. si_doattach (sub_022b10, .text+0x022b10, name assigned)

Arguments: the new handle, device id, OS handle, `regs` (BAR0), bus type,
bus handle, `&vars`, `&varsz`. Result: the handle or NULL. All configuration
accesses are 32 bit; "W1" is the movable window at BAR0+0x000.

1. Zero the handle. `buscoreidx` = 0x21 (none yet); store `regs`
   (`sih+0xb8`), the bus handle (`+0x60`) and the OS handle (`+0x58`).
2. Bus type PCI:
   1. Read `PCICFG(0x88)`. If it reads 0xffffffff, the device is taken as a
      system-bus core instead (bus type 0; not this card).
   2. Read `PCICFG(0x80)` (the window's current backplane address): keep it
      as `win0` if it lies in 0x18000000..0x1801ffff and is 4 KB aligned,
      else `win0` = 0x18000000. Write `PCICFG(0x80)` = 0x18000000 (W1 shows
      ChipCommon). If `regs` is NULL: fail (result NULL, nothing to undo).
3. Store the bus type (`sih+0x04`). Bus type PCI: `si_ldo_war(sih, devid)`
   (chip.md: on this card one read of `PCICFG(0x08)`, result 1; result 0
   would fail the attach). Then look for the PCI Express capability
   (`pcicore_find_pci_capability(osh, 0x10)`, B2); only if there is none:
   `si_clkctl_xtal(sih, 3, 1)` (not this card: it has the capability, at
   0xac).
4. Read the chip id register, W1+0x000 = `CC(0x00)`, and set `chip`,
   `chiprev`, `chippkg`, `socitype` as in A1. (Chip 0x4329 with revision
   0 and a non-zero package: package := 1.) The byte `sih+0x4c` := 1 if the
   package is 14 or 15.
5. Backplane type 1 or 3: `ai_scan(sih, regs, devid)` (A4). Type 0:
   `sb_scan` (not described). Type 2: no scan. Other: fail. If the scan
   found no core (`numcores` = 0): fail.
6. ChipCommon identity: select core 0 (`si_setcoreidx`, chip.md: writes
   `PCICFG(0x80)` = core 0's base and the wrapper window, see the note
   below). `ccrev` := revision of core 0 from its CIB. Then through W1:
   if ccrev > 10, `chipst` := R `CC(0x2c)`; `cccaps` := R `CC(0x04)`; if
   ccrev > 34, `cccaps_ext` := R `CC(0xac)`; if `cccaps` bit 28 (PMU):
   `pmucaps` := R `CC(0x604)`, `pmurev` := its bits 0..7.
7. **Find the bus core.** `buscoretype` := 0x700, `buscorerev` :=
   0xffffffff, `buscoreidx` := 0x21. For each core index `i` from 0 to
   `numcores` - 1: select it (`si_setcoreidx`: two configuration writes),
   take its id and revision (from the tables, no access), and on PCI note:
   a PCI core (id 0x804: index and revision of the last one seen), a PCIe
   core (id 0x83c or 0x820: index and revision of the last one seen, and
   whether any 0x83c was seen). Independently, `home` := `i` if `win0`
   (step 2) is not 0 and equals core `i`'s register base (on the system
   bus: if core `i`'s mapped address equals `regs`). `home` starts at 0.
   After the loop:
   * PCIe core seen and PCI core seen: if the bus is PCI and the PCI Express
     capability exists (`pcicore_find_pci_capability` again), the PCIe core
     is the bus core, else the PCI core.
   * Only a PCIe core seen: it is the bus core.
   * Only a PCI core: the PCI core.
   * Neither: the fields keep 0x700 / 0xffffffff / 0x21.
   The PCIe bus core's type is 0x83c if any 0x83c core was seen, else
   0x820; its index and revision are those of the last PCIe core of the
   loop. (Bus type 2 instead looks for core 0x80d; not this card.)
   On this card: 0x83c, revision 1, index 3; the loop leaves core 4 (the
   last one) current.
8. Bus type PCI: if the bus core is 0x83c or 0x820 (or 0x804 with revision
   > 12) and there is no PCIe bridge state yet (`sih+0x90`): `sih+0x90` :=
   `pcicore_init(sih, osh, regs + 0x2000)` (B2); NULL: fail with clean-up.
   Then, for every PCI bus core, `si_pci_fixcfg(sih)` (B4); a result other
   than 0: fail with clean-up.
9. Select core `home` (two configuration writes).
10. Chip-specific steps of other chips (0x4322 with chip status bits 6..7 =
    3: fail; 0xa8e2..0xa8e4, 0xa8e6 with ccrev 37 and revision < 3: a
    ChipCommon write and 10 us; 0xa9a7/0x4331: an ePA chip control and 20
    ms). For chips 0xa9c4, 0x4360, 0x4352 with **chip revision < 3**:
    `pcie_disable_TL_clk_gating` and `pcie_set_L1_entry_time(0x40)` (B16;
    **not on this card**, revision 3).
11. `nvram_init(sih)` (srom-otp.md; open). `srom_var_init(sih, bustype, regs,
    osh, &vars, &varsz)` (srom-otp.md; open); a result other than 0: fail
    with clean-up. `vars` := `*vars` from now on (NULL if `&vars` is NULL).
12. **Board identity** (bus type PCI):
    1. `ss` := R `PCICFG(0x2c)` (subsystem vendor in bits 0..15, subsystem id
       in bits 16..31; real card 0x0117106b).
    2. `boardvendor` := the **low 16 bits** of
       `si_getdevpathintvar(sih, "boardvendor")` (A5); if that is 0: `ss &
       0xffff`.
    3. `boardtype` := the low 16 bits of `si_getdevpathintvar(sih,
       "boardtype")`; if 0: `getintvar(vars, "boardtype")` (the card's
       variables, full 32 bits); if 0: `ss >> 16`.
    4. Apple: if `boardvendor` is 0x106b and `boardtype` is 0x111, 0x112,
       0x129, 0x134 or 0x135, `boardtype` := `ss >> 16`. (This card: the
       SROM gives 0x117, kept.)
    (Chips 0xa9a7/0x4331 with package 9 build `ss` from the variables
    `boardtype` and `subvid`. Bus types 0, 2, 4 take vendor 0x14e4 or the
    `manfid`/`prodid` variables; not this card.)
13. `boardrev` := `getintvar(vars, "boardrev")`, `boardflags` :=
    `getintvar(vars, "boardflags")`. (0xa9a7/0x4331: `extpagain2g/5g`
    rewritten for some boards.)
14. If ccrev > 19: select ChipCommon (`si_setcore(sih, 0x800, 0)`), write
    W1+0x58 = 0 and W1+0x5c = 0 (`CC(0x58)`, `CC(0x5c)`; chips 0xa886/0x4314
    write 0x402e0 and 0x20500), select core `home` again.
15. If `cccaps` bit 28 (PMU): `si_pmu_init`, `si_pmu_chip_init`; crystal
    frequency `x` := `getintvar(vars, "xtalfreq")` (chip 0xa887: 20000;
    0x4350 and 0xa8ea/0xa8eb: constants), and if `x` is 0:
    `si_pmu_measure_alpclk`; `si_pmu_pll_init(x)`, `si_pmu_res_init`,
    `si_pmu_swreg_init` (all pmu.md).
16. If ccrev > 15: `l` := `getintvar(vars, "leddc")`, 0 -> 0xa005a;
    `CC(0x88)` (gpiotimerval) := `l` (`sub_021d1d` with mask 0xffffffff:
    read, write, read through the fixed window).
17. (Bus core 0x820/0x804 with chips 0x4311/0x4321/0x4716/0x4748: crystal
    on; not this card.) Bus core 0x83c or 0x820: `pcicore_attach(sih+0x90,
    vars, 1)` (B3). (0x4311 rev 2 / 0x4312: an initiator time-out; 0xa99d/
    0xa8d8: chip controls; not this card.)
18. If the variable `muxenab` exists in `vars`: `si_muxenab(sih,
    getintvar(vars, "muxenab"))` (absent on this card's SROM; not described).
19. If ccrev > 20: `v` := R `CC(0x08)` (one read), then `sub_021d1d(sih,
    0x08, v | 4, v | 4)`: R, W `(old & ~(v | 4)) | (v | 4)`, R - i.e. bit 2
    of `CC(0x08)` set (purpose unknown).
20. (0x4313: a PMU chip control; 0x4350/0x4335: GCI overrides.) Result: the
    handle.

**Clean-up on failure after step 8:** if the bus is PCI and the PCIe bridge
state exists, `pcicore_deinit` (B11); `sih+0x90` := 0; result NULL. (Before
step 8 nothing is undone; `si_attach` frees the handle.)

**The core current after attach** is `home`: the core whose register base
was in the window when the driver was loaded. On a freshly powered card
that is ChipCommon (0x18000000), but after a previous driver left the
window on the 802.11 core (0x18001000, as the configuration dump of the
running card shows) it is core 1. Verified in the emulator with the real ROM
and the window preset to 0x18001000: the attach ends with core index 1
current and `PCICFG(0x80)` = 0x18001000, `PCICFG(0x70)` = 0x18101000. The
MAC selects the 802.11 core itself afterwards (`si_setcore(sih, 0x812, 0)`
in `wlc_bmac_attach`), so this changes only configuration writes.

**The wrapper window during attach.** `si_setcoreidx` writes the wrapper
address to `PCICFG(0x70)` only once `buscoretype` is 0x83c; until step 7 has
set it, it writes `PCICFG(0xac)` instead (chip.md). On this card 0xac is the
header of the PCI Express capability: the write goes to its read-only
capability register (the dump of the running card shows 0xac unchanged:
0x00020010). An open driver need not imitate those writes; nothing in the
attach uses the wrapper window before step 8.

**Access sequence** (verified: object in the emulator with the real ROM
against the real capture, MMIO reads 1..94 identical in order, address,
width and value except the one model value noted in B4; configuration
accesses from the emulator, the capture does not record them):

* configuration: R 0x88, R 0x80, W 0x80 = 0x18000000, R 0x08, the
  capability search (R 0x0c, 0x04, 0x34, then for each capability in the
  list two reads of its first dword - 0x48, 0x58, 0x68, and one of 0xac);
* MMIO: R `CC(0x00)` = 0x15034360, then the scan (A4: R `CC(0xfc)` =
  0x1810a000, configuration W 0x80 = 0x1810a000, 87 ROM reads);
* configuration: the pair W 0x80 / W 0xac for core 0 (step 6), MMIO R
  `CC(0x2c)`, `CC(0x04)`, `CC(0xac)`, `CC(0x604)` through W1;
* configuration: the pairs W 0x80 / W 0xac for cores 0..4 (step 7), the
  capability search again (inside `pcicore_init`), W 0x80 / W 0x70 for the
  PCIe core and back to core 4 around the 16-bit read of the PCIe core's
  register 0x800 (`si_pci_fixcfg`), then core `home`;
* then the SROM/OTP and NVRAM steps (srom-otp.md), R `PCICFG(0x2c)`, the
  ChipCommon writes of step 14, the PMU (pmu.md), the rest above.

### A4. ai_scan (.text+0x001f1d), get_erom_ent (sub_00133f) and get_asd (sub_001381)

#### The enumeration ROM

ChipCommon register 0xfc holds the backplane address of the enumeration ROM
(EROM), a list of 32-bit words. Bit 0 of a word marks it valid; bits 1..3 are
its tag in the descriptor words (0 component, 1 master port, 2 address with
bit 3 meaning "64-bit"; the end marker is the whole word 0x0000000f).

* **Component**, two words. CIA: bits 4..7 class, 8..19 core id, 20..31
  manufacturer (0x4bf Broadcom, 0x43b ARM). CIB (follows directly): bits 4..8
  number of master ports, 9..13 slave ports, 14..18 master wrappers, 19..23
  slave wrappers, 24..31 revision.
* **Master port** descriptor: tag 1 (`(w & 0xe) == 2`).
* **Address** descriptor: `(w & 6) == 4`; bit 3: a second word with the high
  32 bits of the address follows; bits 4..5 size code (0, 1, 2: 4 KB, 8 KB,
  16 KB; 3: a size word follows - its bits 12..31 are the size, and if its
  bit 3 is set a further word with the high 32 bits of the size follows);
  bits 6..7 type (0 slave, 1 bridge, 2 slave wrapper, 3 master wrapper);
  bits 8..11 port number; bits 12..31 the address.

#### get_erom_ent(&ptr, mask, match)

Read the word at `ptr` (32-bit MMIO), advance `ptr` by 4. With `mask` = 0:
return it, whatever it is. Otherwise skip words whose bit 0 is clear; a word
equal to 0xf is returned (end); a valid word with `(w & mask) == match` is
returned; any other valid word is skipped too.

#### get_asd(&ptr, port, index, type) -> word, address, size

1. `w` := `get_erom_ent(&ptr, 1, 1)` (the next valid word).
2. If `w` is not an address descriptor, or its port is not `port`, or its
   type is not `type`: move `ptr` back by 4 (that word will be read again)
   and return 0 (outputs unchanged). `index` is not compared.
3. Else: address low := `w & 0xfffff000`; high := the next word (read with
   mask 0) if bit 3, else 0; size high := 0; size code 3: the next word `s`,
   size low := `s & 0xfffff000`, and if `s` bit 3 the size high is the next
   word; else size low := 0x1000 << code. Return `w`.

#### ai_scan(sih, regs, devid)

1. `erom` := R W1+0xfc (`CC(0xfc)`, ChipCommon is in the window). Bus type
   PCI: the wrapper address used by every wrapper function becomes BAR0 +
   0x1000 (`sih+0x548`, fixed from now on); W `PCICFG(0x80)` := `erom`; the
   ROM is then read through W1 from BAR0+0. (System bus: the ROM is mapped;
   other bus types: return with no cores.)
2. Repeat while the read position is below ROM offset 0xe00 (reaching it:
   `numcores` := 0, i.e. the attach fails):
   1. `cia` := `get_erom_ent(0xe, 0)` - the next component word. If it is
      0xf (end of the ROM): return; the cores found so far stand.
   2. `cib` := the next word (mask 0). If `cib & 0xe` is not 0: error
      (`numcores` := 0, return).
   3. Skip this component (go to 2) if it is ARM's default component
      (manufacturer 0x43b with core id 0xfff) or has no slave ports.
   4. If it has neither master nor slave wrappers: core id 0x367 (the OOB
      router): read its first slave address descriptor of port 0 and, if
      there is one, store its address at `sih+0x7d0`; skip the component.
      Core ids 0x50b and 0x5dc are kept; any other such component is
      skipped.
   5. Let `i` := `numcores`. Store `cib` at CIB[i], `cia` at CIA[i], the core
      id at COREID[i] (chip 0x5300 maps ids through a table; not this
      card).
   6. For each master port: `w` := `get_erom_ent(1, 1)`; if `(w & 0xe)` is
      not 2: error.
   7. The core's registers: `get_asd(port 0, index 0, slave)`. If there is
      none, the component is a **bridge**: consume every following bridge
      descriptor of port 0 (`get_asd(0, 0, bridge)` until it fails); if at
      least one was found, continue with the values of the last one (the
      component will not be counted, step 12). (If none was found either,
      the object checks the values left from the previous descriptor read:
      not "size 0x1000 with both high words 0" is an error, otherwise it
      tries again without progress - an endless loop; unreachable with a
      well-formed ROM.) Store the address at REGBASE[i] (`sih+0x248+4i`) and
      the size at `sih+0x448+4i`.
   8. Further slave descriptors of port 0 (index 1, 2, ...): read until one
      fails. Only the one at index 1 is used, and only if its size is
      0x1000: its address goes to `sih+0x3c8+4i`, 0x1000 to `sih+0x4c8+4i`.
   9. Slave ports 1 .. n-1: each must have at least one slave descriptor
      (else error); all of them are consumed.
   10. Master wrappers, port 0 .. m-1: `get_asd(port, 0, master wrapper)`
       must succeed with size low 0x1000 and size high 0 (the address high
       word is not checked), else error. Port 0's address goes to
       WRAPBASE[i] (`sih+0x650+4i`).
   11. Slave wrappers, k = 0 .. s-1, port `k` if the component has exactly
       one slave port, else `k + 1`: same checks. If the component has no
       master wrapper, the address of k = 0 goes to WRAPBASE[i].
   12. Unless the component was a bridge: `numcores` += 1. (A bridge's table
       entries at index `i` stay written but are not counted, so the next
       counted core overwrites them.)

A component that fails a check ends the scan with **no cores** (the attach
fails), whatever was found before.

#### The real card's ROM

Base 0x1810a000 (the model uses 0x180fe000). 76 words up to the end marker
at offset 0x12c; the scan makes 87 reads (a word that a failing `get_asd`
gives back is read again; one word, at 0xb4, three times). Components in
order:

| # | Component (manufacturer, id, revision) | Ports and wrappers | Registers, wrapper | Counted as |
|---|---|---|---|---|
| 1 | 0x4bf 0x800 ChipCommon rev 43 | 1 MP, 2 SP, 1 MW, 1 SW | 0x18000000 (4 KB); port 1: 0x1c000000, 16 MB; MW 0x18100000; SW (port 1) 0x18105000 | core 0 |
| 2 | 0x4bf 0x812 802.11 rev 42 | 1 MP, 1 SP, 1 MW | 0x18001000; MW 0x18101000 | core 1 |
| 3 | 0x4bf 0x83e ARM CR4 rev 2 | 1 MP, 2 SP, 1 MW, 1 SW | 0x18002000; port 0 index 1: 0x18005000, 8 KB (not recorded: not 4 KB); port 1: 0x00000000 640 KB and 0x000e0000 32 KB; MW 0x18102000; SW 0x18106000 | core 2 |
| 4 | 0x4bf 0x83c PCIe Gen2 rev 1 | 1 MP, 2 SP, 1 MW, 1 SW | 0x18003000; port 1: 0x08000000 128 MB and a 64-bit region at 0x80000000_00000000, size 0x80000000_00000000; MW 0x18103000; SW 0x18107000 | core 3 |
| 5 | 0x4bf 0x81a USB 2.0 device rev 17 | 1 MP, 1 SP, 1 MW | 0x18004000; MW 0x18104000 | core 4 |
| 6 | 0x43b 0x135 rev 0 | 1 SP, 1 SW, no slave descriptor: a bridge at 0x18000000, 32 KB; SW 0x18108000 | - | bridge: entries at index 5 written, not counted |
| 7 | 0x43b 0x367 (OOB router) | 1 SP, no wrappers | 0x18109000 | `sih+0x7d0` = 0x18109000 |
| 8 | 0x43b 0x366 | 1 SP, no wrappers | 0x1810a000 (the ROM itself) | skipped |
| 9 | 0x43b 0x301 | 1 SP, no wrappers | 0x18200000, 1 MB | skipped |
| 10 | 0x43b 0xfff (ARM default component) | 1 SP, 1 SW | eight regions, one with a 64-bit size word | skipped; its words are passed over while looking for the next component |

Result: five cores, indices 0..4 in the order of the model, with register
bases 0x18000000 + 0x1000·i and wrappers **0x18100000, 0x18101000,
0x18102000, 0x18103000, 0x18104000** (the model has 0x18100000,
0x18101000, 0x18103000, 0x18105000, 0x18107000: it builds a wrapper per
master *and* slave wrapper in sequence). No core has a second 4 KB region
(`sih+0x3c8..` all 0); every first region is 4 KB. The PCIe core's revision
1 settles the condition of `si_pcie_ltr_war` (B6) for the card.

The card model's synthetic ROM (`tools/re/chip.py`, `Erom`) has one slave
port per core, no size words, no 64-bit fields, no bridge, no components
without wrappers and no ARM default component: none of the branches of steps
4, 7 (bridge), 8, 9, 3 (0xfff) or the size word/high word paths of `get_asd`
are exercised by the standard session. They are by the real ROM (below).

Verified: (1) the object's `ai_scan` run in the emulator on the real ROM
(`real_erom.py`) makes the same 87 reads, at the same offsets, as the card
in the capture; (2) an independent decoder written from the text above
gives the same `numcores`, the same CIA, CIB, core id, register base, size,
second region and wrapper of every core, and the same `sih+0x7d0`, as the
object (0 differences); (3) the object leaves the bridge's entries at index
5 (id 0x135, base 0x18000000, size 0x8000, wrapper 0x18108000) with
`numcores` = 5. The error paths (steps 2.2, 6, 9-11 failing, the 0xe00
limit) were read from the code only.

### A5. si_getdevpathintvar (.text+0x0218b6, name original)

`(sih, name)`: integer value of the device-path variable of this card in the
**global NVRAM only** (not the card's variables): first
`getintvar(NULL, "pci/<bus>/<slot>/<name>")` (the name built by
`si_devpathvar`, srom-otp.md); if that gives 0 and `si_coded_devpathvar`
finds a `devpath<n>` NVRAM entry naming this device,
`getintvar(NULL, "<n>:<name>")`. Result 32 bit; `si_doattach` keeps only
its low 16 bits. No hardware access. On this driver the global NVRAM is
normally empty: both lookups give 0 (emulator: two calls in the attach,
for `boardvendor` and `boardtype`, each with one `si_coded_devpathvar` that
searches `devpath0` .. `devpath31` and finds none).

### A6. si_detach (.text+0x02171d) and srom_var_deinit (.text+0x004510), names original

`si_detach(sih)`, NULL: nothing.

1. System bus only: unmap the 32 mapped core addresses.
2. `srom_var_deinit(sih)`: clears one byte of `bcmsrom.c`'s static state
   (`.bss+0x71`, no reader in the session); no access.
3. `nvram_exit(sih)` (open, srom-otp.md).
4. Bus PCI: `pcicore_deinit(sih+0x90)` if the bridge state exists (B11);
   `sih+0x90` := 0.
5. Free the 0x7d8 bytes (unless the handle is the static one of
   `si_kattach`, the system-bus attach).

No hardware access (code; emulator: none in the detach step from these).
`si_detach` does not free the variable buffer (`sih+0xa8`); who does was not
followed (code).

## B. The PCIe bridge (`nicpci.c` and the `si_pci*` functions)

chip.md ("PCIe bridge") describes all of these and compared the session's
calls with the object (0 differences). This section gives the order in which
the driver uses them, what each does **on this card**, and what the real
card showed; for every function the full text of chip.md stays valid.

### B1. When the driver calls what

| When | Call | Effect on this card |
|---|---|---|
| attach, `si_doattach` step 3 | `pcicore_find_pci_capability(0x10)` | configuration reads only |
| attach, step 8 | `pcicore_init(regs + 0x2000)`, `si_pci_fixcfg` | bridge state; the SROM-shadow index check |
| attach, step 17 | `pcicore_attach(vars, 1)` | MRRS code 3 stored, no access |
| `wlc_bmac_attach` | `si_pcie_ltr_war`, `si_pcieobffenable(1, 0)`, `si_pci_down` | wrapper writes; OBFF off; nothing |
| `wlc_wowl_cap` (attach) | `pcicore_pmecap_fast` | configuration reads |
| first `wlc_up`, `wlc_bmac_hw_up` | `si_survive_perst_war(1, 0, 0)`, then (after the PMU and clock set-up) `si_pcie_ltr_war`, `si_pci_fixcfg` | chip reset by watchdog, 20 ms; wrapper writes again; index check |
| every up, `wlc_bmac_up_prep` | `si_pci_setup(2)`, `si_pci_up` | interrupt mask bit; MRRS 1024 |
| `wlc_bmac_hw_down` | `si_pmu_res_req_timer_clr` (D1), `si_pci_down`, `si_survive_perst_war(0, 0x204, 0)` | PMU timer; nothing; PCIe register 0 bits 2, 9 cleared |
| scan steps of the upper layer | `si_pci_down` (from the MAC's power-down paths) | nothing |
| `wlc_bmac_detach` | `si_pci_sleep` | flag cleared only |
| `si_detach` | `pcicore_deinit` | free |
| glue suspend / resume | `si_pci_sleep`; `si_pci_pmeclr` only with wake-on-WLAN offload active | flag only; not reached |

(Emulator: the call sites and steps are those of `lower.py`'s coverage with
callers, session and REAL steps; the hw_up order also matches the real
capture, accesses 861..884.)

### B2. Bridge state, pcicore_init (.text+0x018b68) and the capability search (.text+0x018156)

`pcicore_init(sih, osh, regs)` allocates and zeroes the 0x138-byte bridge
state (chip.md lists its fields), stores the PCIe core's register window
(BAR0 + 0x2000), `sih` and `osh`, and for bus core 0x83c/0x820 looks up the
PCI Express capability (id 0x10) and records the configuration offsets of
Device Control (capability + 8), Link Control (+ 0x10) and Device Control 2
(+ 0x28) (and + 0x798/0x79c/0x7a0 for LTR helpers that are not reached).
Result: the state, or NULL if the allocation failed. Bus core 0x820 only:
one more flag. No MMIO.

The capability search walks the standard list (header type 0, status bit 4,
pointer at 0x34, each capability's id and next pointer read with separate
32-bit configuration reads of the same dword). **Real card** (configuration
dump of the running card): the list is 0x48 power management (PMC
0xfe03: PME supported from all states), 0x58 MSI, 0x68 vendor specific,
0xac PCI Express (version 2, end of list) - the same list, at the same
offsets, as the card model. So on the card Device Control is at 0xb4, Link
Control at 0xbc and Device Control 2 at 0xd4, as chip.md assumed (its open
question 2, second half, is settled).

An open driver on Linux can take these offsets from the PCI core
(`pci_pcie_cap`, `pci_find_capability`) instead of walking the list; the
reads are configuration reads without side effects.

### B3. pcicore_attach (.text+0x019154, name original)

On this card (bus core 0x83c, board vendor 0x106b, board type 0x117, chip
0x4360 revision 3): only the Max Read Request Size code 3 (1024 bytes) is
stored for B8; no access. Board types 0x129 and 0x135 of Apple would first
write a SerDes register (0x830, block 0x10, mask 0x3ff, value 0x39f;
not described); chip revision >= 4 would do the `PCIE(0x000)` modification
of chip.md's `sub_018c5d`. Bus core 0x820: ASPM/CLKREQ policy work-arounds
(not described). (code; emulator: no access in the attach from it.)

### B4. si_pci_fixcfg (.text+0x021e58) and pcicore_hwup (.text+0x019345), names original

1. (Chip 0x4321 rev < 2: a ChipCommon chip-control write; not this card.)
2. Remember the current core. Bus core 0x83c/0x820/0x804: select the bus core
   (`si_setcore`: W `PCICFG(0x80)` = 0x18003000, W `PCICFG(0x70)` =
   0x18103000); other bus cores: result -1, nothing else.
3. 16-bit read of the PCIe core's register 0x800 through window 1 (the
   first word of its SROM shadow). If bits 12..15 differ from the bus core's
   index (the now current index), 16-bit write: the low 12 bits kept, bits
   12..15 := the index.
4. Select the remembered core again; `pcicore_hwup` (on this card: nothing;
   board types 0x129/0x135 of Apple: the SerDes write of B3; bus core 0x820:
   gen-1 work-arounds); result 0.

**Real card:** the word reads **0x3801** at attach and again at up (bits
12..15 = 3 = the PCIe core's index): **no write**. It is the same value as
the first word of the SROM read through ChipCommon right after (`CC(0x800)`
= 0x3801): the PCIe core's register 0x800 shadows SROM word 0. The card
model returns 0 there and so takes the write branch (0x3000) at attach: the
only access of the attach in which the object on the model and the object on
the card differ (MMIO access 94 of the comparison in A3). Model value, not a
behaviour difference.

### B5. si_pcieobffenable (.text+0x01ec42) and pcie_obffenable (.text+0x017fe1), names original

Bus PCI with bus core 0x83c/0x820, else result 0. If Device Control 2's
offset is unknown, result 0. Else read Device Control 2 (32 bit); if `mask`
is not 0, write it with bits 13..14 (OBFF enable) := `val & 3` and read it
again; result bits 13..14 of the value read last. `wlc_bmac_attach` calls it
with mask 1, val 0: **OBFF off** (R, W, R of `PCICFG(0xd4)`). Real card:
Device Control 2 reads 0x00000400 with the driver running (OBFF field 0).

### B6. si_pcie_ltr_war (.text+0x01f905), si_pcieltrenable (.text+0x01ec6f), pcie_ltrenable (.text+0x018060), names original

Only for bus PCI, bus core 0x83c **revision 1** (the real card's revision,
A4):

1. `on` := `si_pcieltrenable(sih, 0, 0)`: bit 10 (LTR mechanism enable) of
   Device Control 2 (one configuration read; 0 if the offset is unknown).
2. Remember the current core; select the 802.11 core (0x812).
3. Wrapper register 0x160 of the 802.11 core := **0x02838280 if `on`**,
   else 0x02848180; wrapper register 0x164 := 3 (each R, W, R through the
   wrapper window, BAR0+0x1160 / +0x1164).
4. Select the remembered core.

`pcie_ltrenable(mask, val)` in general: read Device Control 2; if `mask`:
bit 10 := (`val` != 0), write, read again; result bit 10 of the value read
last.

**Real card:** Device Control 2 has LTR enabled (0x00000400, presumably by
the Linux PCI core, which enables LTR when the device and its upstream port
support it), so the card takes the **`on` branch** and writes 0x02838280.
The card model's Device Control 2 is 0, so the session on the model takes
the other branch (0x02848180): the open code's LTR branch is not covered by
the session. On the card, at attach the registers already read 0x02838280
and 3; at the first up, after the watchdog reset of `si_survive_perst_war`,
they read **0x82848180 and 0x83** and are written 0x02838280 and 3 again
(capture accesses 360..365 and 878..883). So the reset returns them to their
power-on values (bit 31 and bit 7 read back set, not written), which is why
the work-around runs again after it. Purpose of the registers unknown.

### B7. si_pci_setup (.text+0x0215bd, name original)

Bus PCI with bus core 0x83c/0x820 (or 0x804 rev > 5): `PCICFG(0x94)` |=
`coremask << 8` (R, W). `wlc_bmac_up_prep` passes 2 (core 1, the 802.11
core): bit 9, the 802.11 core's interrupt routed to the PCIe function.
(Bus core 0x804: `si_setint`, the core flag, and PCI core register
work-arounds; not this card.) Real card: `PCICFG(0x94)` reads 0x00000200
with the driver running.

### B8. si_pci_up (.text+0x0213f8), pcicore_up (.text+0x0196d0), sub_01963d (`pcie_devctl_mrrs_set`)

Bus PCI only. (Chips 0x4311/0x4321/0x4716/0x4748: crystal on first.) Bus
core 0x83c/0x820: `pcicore_up`, which for 0x83c sets the Max Read Request
Size: read Device Control (`PCICFG(0xb4)`), write it with bits 12..14 := 3
(1024 bytes; other bits kept), read it again. (Chip 0x4360 revision > 3:
then `sub_018c5d`, not this card. Bus core 0x820: gen-1 work-arounds.) Real
card: Device Control reads 0x3c1f with the driver running (MRRS 1024, Max
Payload 128).

### B9. si_pci_down (.text+0x02137e), pcicore_down (.text+0x019090)

Bus PCI only: (crystal off for chips 0x4311/0x4321/0x4716/0x4748), then
`pcicore_down`, which acts only for bus core 0x820. **No access on this
card** (code; chip.md's comparison of the session calls).

### B10. si_pci_sleep (.text+0x01ecf6), pcicore_sleep (.text+0x019015)

Clear the global flag `do_4360_pcie2_war` (32 bit; read by
`wlc_bmac_4360_pcie2_war`, which acts only for chip revision < 3), then
`pcicore_sleep`, which acts only for bus core 0x820 (a gen-1 helper, and for
revisions 3..5 bit 1 of Link Control, ASPM L1, cleared). **No access on this card.** Called by
`wlc_bmac_detach` and by the glue's suspend handler.

### B11. pcicore_deinit (.text+0x018b4c)

Free the 0x138 bytes. No access.

### B12. pcicore_pmecap_fast (.text+0x0182dc)

Find the power management capability; result 1 if its PMC bits 27..31 (PME
support) are not all 0. Called by `wlc_wowl_cap` at attach. Real card: PMC
0xfe03 at 0x48: result 1 (as on the model).

### B13. si_survive_perst_war (.text+0x01f997)

Already open (`hybrid/si_perst.c`, `open/chip`; chip.md). Real card
confirms the reset branch at the first up: W `CC(0x634)` = 2 through window
1 (ChipCommon selected), then 20.7 ms until the next access (the 2000 x 10 us
wait); after it the PMU is programmed again and the PCIe core's clock
control register `PCIE(0x1e0)` reads 0x10040 where it read 0x30050 before
the reset (the HT request bit 4 gone): the watchdog reset reaches the PCIe
core's registers while the link survives (capture accesses 861..877). The
mask branch at down (PCIe register 0 bits 2 and 9 cleared) is not in this
capture (the capture ends with the driver up).

### B14. si_pcie_get_request_size (.text+0x01edf6) and pcie_get_request_size (.text+0x017ca7)

Result 0 unless bus PCI with bus core **0x820**: then the size for the
stored MRRS code (0: 128, 1: 256, 2: 512 bytes, other codes 0). On this card
always 0, no access. Its only caller (`wlc_bmac_attach`) uses it for chip
0x4331 only; the open attach does the same through
`bcm4360_attach_env_si_pcie_get_request_size`, so the call is not even
made on this card (emulator: not executed). (code)

### B15. si_pci_pmeclr (.text+0x01ef4c)

chip.md. The glue (`wl_linux.c`, `wl_resume`) calls it only when the
offload engine is enabled and wake-on-WLAN was active; not on this driver's
normal path. (code)

### B16. pcie_disable_TL_clk_gating (.text+0x0186e1) and pcie_set_L1_entry_time (.text+0x018a68)

Called by `si_doattach` only for chips 0xa9c4/0x4360/0x4352 with chip
revision < 3. The real card is revision 3 (A1): **not reached**. Not
described.

## C. OTP reading

[srom-otp.md](srom-otp.md) ("OTP access (bcmotp.c)") describes every
function that runs - `otp_read_word`, `otp_init`, `ipxotp_init`,
`ipxotp_otpr`, `ipxotp_read_bit`, `ipxotp_read_word`, `si_is_otp_powered`,
`si_is_otp_disabled`, `si_is_sprom_available` - completely for the 4360 on
PCIe; nothing is missing for the session. The only caller in the session is
the PHY's attach (acphy-attach.md step 27: OTP word 16, around a set of bit
4 of `CC(0x190)`; the open PHY calls it through `phy_env.h`'s
`bcm4360_otp_read_word`). The SROM path's OTP fall-back (`otp_read_pci`)
does not run on this card: it has an SROM.

**Checked on the real card** (capture accesses 464..637): the sequence is
exactly srom-otp.md's - four reads of the PMU resource state `CC(0x60c)`
(0x1fb: the OTP is powered, no power-up), the OTP layout `CC(0x1c)` three
times, the status `CC(0x10)`, `CC(0x190)` (the SROM-present check before
each word: bit-serial reads), the 16 bit reads of general-use word 3, the
fuse register `CC(0x4c)`, `CC(0x190)` again, and the 16 bit reads of word
16, each bit as `CC(0x14)` = 0, `CC(0xf4)` = 0, R `CC(0x1c)`, W `CC(0x18)` =
0x80000000 | row << 8 | column, R `CC(0x18)` (bit 31 already clear at the
first read). With the card's own register values the formulas of
`ipxotp_init` give:

| | Real card | Card model |
|---|---|---|
| `cccaps` OTP size code (bits 19..21) | 5: 192 rows x 32 columns, 384 words | 5 |
| layout `CC(0x1c)` | 0x00115200: type 1, general-use base ((0x200 >> 4) - 4) = **28** | 0x00010140: base 16 |
| status `CC(0x10)` | 0x9000 (no region-programmed bit 8..11) | 0x1000 |
| general-use word 3 (region flags), read as | word 31 = bits 496..511: row 15, columns 16..31; reads 0 | word 19: row 9 |
| fuse register `CC(0x4c)` | 0x318 (ignored: the 4360 uses 99) | (ignored as well) |
| **OTP word 16** (row 8, columns 0..15) | **0x0500** (bits 8 and 10 set) | 0 (blank) |

So the card's OTP has no programmed regions (its board data is in the SROM)
but word 16 is not blank. Word 16's bits 8..12 are the PHY's value at
`pi_ac+0x8e2` (acphy-attach.md): **5 on the card, 0 on the model**, and bits
0..3 its RCAL value (0 on both; read only with boardflags3 bit 13). Any
session on the model therefore runs the PHY with the blank value; the code
that depends on `pi_ac+0x8e2` belongs to the PHY specifications.

## D. The other rests

### D1. si_pmu_res_req_timer_clr (.text+0x0243cc, name original)

Called by `wlc_bmac_hw_down` (every down, before `si_pci_down`). Two
ChipCommon register operations through the fixed window (`sub_021d1d`):

1. `CC(0x644)`: clear bits 24 and 25 (mask 0x03000000, value 0; chip 0x4328:
   mask 0x00000c00): R, W `old & ~0x03000000`, R.
2. `CC(0x644)` read once more (mask and value 0).

Accesses: R, W, R, R of BAR0+0x3644. No result. By its name, register 0x644
is the PMU's resource request timer; bits 24..25 its enables (not verified
on the card: the capture ends before a down). (code; emulator: two calls in
the session, at the two downs.)

### D2. bcm_mpool.c and bcm_notif.c

A memory-pool manager (`bcm_mpm_init`/`deinit`, pools created from
pre-allocated or heap memory, `bcm_mp_alloc`/`free`) and notifier lists
(`bcm_notif_*`: a list per event, interested parties added and removed)
used by the upper layer: `wlc_attach` creates the manager, the event queue
takes a heap pool, the BSS configurations register for up/down
notifications. Host memory only: their imports are `osl_malloc`,
`osl_mfree`, `osl_memset` and `osl_strncpy` (index), **no hardware
access**. A mac80211 driver has no use for them: not described.

### D3. bcmutils.c: packet queues and the iovar length check

`pktq_init`, `pktq_flush` (and in the REAL steps `pktq_penq`, `pktq_mdeq`,
`pktsegcnt`, `pkttotlen`) are the upper layer's packet queues;
`bcm_iovar_lencheck` checks a variable's buffer length for `wlc_iovar_check`.
Host memory only, no hardware access; not needed below mac80211. Not
described.

### D4. wf_chspec_aton (.text+0x00cc72, name original) and its helpers

No caller in the object (index): it converts a chanspec text ("36/80") into
a chanspec. In the session it is called by the **test harness**
(`tools/re/run_chan.py`, `chanspec()`) to turn the channel steps into
chanspecs, not by the driver; it does not belong to the driver's behaviour.
No hardware access. Not described.

## Verification

| Statement | How |
|---|---|
| The list of lower-layer functions that run (Scope) | `re-out/analysis/chip-attach/lower.py`: coverage of the hybrid core per function, entry counts, callers and session steps, for `hybrid.SESSION` and `hybrid.REAL` (outputs `lower-session.txt`, `lower-real.txt`) |
| Chip id/rev/package, EROM pointer, the whole ROM, `chipst`, `cccaps`, `cccaps_ext`, `pmucaps` of the real card | read in the capture of card test 2026-11-03 09:53 (`real-attach-head.txt`) |
| `ai_scan` on the real ROM: reads, tables, `numcores`, OOB router address, the uncounted bridge entry (A4) | object in the emulator on a model with the real ROM, identity registers and core layout (`real_erom.py`): its first 94 MMIO accesses equal the capture's except the model's PCIe shadow word (B4); an independent decoder from A4's text gives identical tables (0 differences) |
| Public fields of the handle (A1 table) | the same run: every field read back from the object's handle |
| `home`: the core current after attach follows the window found at entry (A3) | the same run with the window preset to 0x18001000: core 1 current, windows at 0x18001000 / 0x18101000 |
| Board vendor/type truncated to 16 bits (A3 step 12) | disassembly (zero-extension of the 16-bit result) |
| Step 14, 16, 19 of A3 (`CC(0x58)`, `CC(0x5c)` = 0; `CC(0x88)` = 0xa005a; `CC(0x08)` 0x38 -> 0x3c) | capture accesses 332, 333, 353..359 |
| Capability list, Device Control 0x3c1f (MRRS 1024), Device Control 2 0x400 (LTR on, OBFF off), `PCICFG(0x94)` = 0x200 (B2, B5-B8) | configuration dump of the running card (`peek.txt`) |
| PCIe shadow word 0x3801, no fixcfg write; the LTR work-around's `on` branch and the wrapper values before and after the watchdog reset; the 20 ms wait; `PCIE(0x1e0)` changed by the reset (B4, B6, B13) | capture accesses 94, 360..365, 861..884 |
| OTP geometry and access pattern; word 16 = 0x0500 (C) | capture accesses 464..637, against srom-otp.md's formulas |
| Software-only rests (D2-D4) | index: their imports are memory functions only; `wf_chspec_aton` has no caller in the object, the harness calls it (`run_chan.py`) |

Read from the code only: the error paths of `si_doattach` and `ai_scan`,
the branches for other chips, buses and bus cores, the endless-loop case of
A4 step 7, `si_detach`, `si_pmu_res_req_timer_clr` on the card (D1),
`si_pcie_get_request_size`, `si_pci_pmeclr`. The session functions of B
were compared with the object by chip.md (352 calls, 0 differences); this
specification did not repeat that run.

## Open questions

1. The 802.11 wrapper registers 0x160/0x164 of `si_pcie_ltr_war`: the card
   shows their reset values (0x82848180, 0x83; bits 31 and 7 set on read)
   and that the work-around rewrites them after every watchdog reset; their
   meaning is still unknown (chip.md question 1).
2. `cccaps` bit 27 is set on the card (0 in the model): `si_backplane64`
   returns 1 there, and `dma_attach`'s address-width choice reads it. Whether
   the DMA engines' 64-bit mode depends on it on this card belongs to
   bmac-data; the session on the model never runs that branch.
3. OTP word 16 = 0x0500 on the card (0 in the model) feeds `pi_ac+0x8e2`
   of the PHY: the model sessions run the PHY with 0 there.
4. `cccaps_ext` bit 0 is set on the card (0 in the model);
   `wlapi_is_eci_coex_enabled` requires it clear, so on the card ECI
   coexistence is reported off whatever the MAC capabilities say (code of
   bmac-init.md A18, not re-read here).
5. The writes of `CC(0x58)`, `CC(0x5c)` and bit 2 of `CC(0x08)` at attach
   (chip.md question 6): on the card `CC(0x08)` reads 0x38 before and 0x3c
   after; meaning unknown.
6. `si_pmu_res_req_timer_clr` at down and the mask branch of
   `si_survive_perst_war` were not seen on the card (the capture ends while
   the driver is up).
