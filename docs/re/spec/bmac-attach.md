# Attach and detach of the MAC core (bmac-attach)

Status: complete for the functions of the Scope (2026-11-02). Every procedure
was read in the decompiler output and checked in the disassembly; the attach
and the detach were observed on Broadcom's object alone in the emulator
(`hybrid.Run()`, no open code), field by field and call by call; the error
paths were made to happen where the model allows it (see Verification). Each
statement of the Procedures says "observed" when a run showed it and "read"
when it comes from the code only.

This specification replaces section D1, D2 and D4 of
[bmac-init.md](bmac-init.md) (see "Corrections"); D3
(`wlc_bmac_validate_chip_access`) stays there. Register notation:
[access.md](access.md). Field names follow bmac-init.md "Data",
[bmac-run.md](bmac-run.md) "Fields of wlc_hw" and
[bmac-data.md](bmac-data.md) D1/D7/D8.

## Scope

Attach and detach of the hardware MAC layer (`wlc_bmac.c`, `wlc_hw.c`) for
the BCM4360 family (chip 0x4360; 0x4352 and 0xaa06 take the same path except
where said), MAC core revision 42, PCIe, AC-PHY. The functions of other files
that only these two call (the PHY shim, two helpers of `wlc.c`, two of
`wlc_alloc.c`, one of `bcmutils.c`) are specified here too.

| function | .text offset | size | name |
|---|---|---|---|
| `wlc_hw_attach` | 0x0798ff | 241 | original (wlc_hw.c) |
| `wlc_hw_detach` | 0x079884 | 123 | original (wlc_hw.c) |
| `wlc_bmac_attach` | 0x06984f | 4629 | original (wlc_bmac.c) |
| `wlc_bmac_detach` | 0x066cae | 324 | original (wlc_bmac.c) |
| `wlc_hw_set_di` | 0x0796a7 | 21 | original (wlc_hw.c) |
| `wlc_hw_get_txavail` | 0x0796ee | 30 | original (wlc_hw.c) |
| `wlc_hw_get_bandunit` | 0x0796de | 16 | original (wlc_hw.c) |
| `wlc_template_cfg_init` | 0x079724 | 113 | original (wlc_hw.c) |
| `wlc_bmac_si_attach` | 0x066e05 | 23 | original (wlc_bmac.c) |
| `wlc_bmac_si_detach` | 0x066df2 | 19 | original (wlc_bmac.c) |
| `wlc_bmac_info_init` | 0x05fd12 | 85 | original (wlc_bmac.c; no caller, see Data) |
| `wlc_phy_shim_attach` | 0x152b8b | 81 | original (wlc_phy_shim.c) |
| `wlc_phy_shim_detach` | 0x152b6c | 31 | original (wlc_phy_shim.c) |
| `wlc_chipmatch` | 0x0251ca | 42 | original (wlc.c) |
| `wlc_is_singleband_5g` | 0x025eb0 | 136 | original (wlc.c) |
| `wlc_tunables_override` | 0x04e83c | 89 | original (wlc_alloc.c) |
| `wlc_calloc` | 0x04eb8d | 46 | original (wlc_alloc.c) |
| `bcm_ether_atoe` | 0x00a8ac | 79 | original (bcmutils.c) |

Specified elsewhere and only called here (with the arguments given below):
`sub_064887` (A13 of bmac-init), `wlc_bmac_corereset` (A14),
`wlc_bmac_validate_chip_access` (D3), `wlc_bmac_pllreq`, `wlc_bmac_xtal`
(C9), `wlc_coredisable` (C7), `wlc_setxband` (acphy-chanspec.md section 2),
`wlc_bmac_ampdu_set`, `wlc_bmac_set_btswitch`, `wlc_bmac_btc_wire_set`,
`wlc_bmac_btc_mode_set`, `wlc_bmac_led_attach`/`_detach`, `sub_061268`,
`sub_061285` (bmac-run.md R3, R4, R8, R9), `dma_attach`, `dma_addrwidth`,
`sub_069659` (bmac-data.md P1, P2, P54), the `si_*` functions (chip.md,
pmu.md), the PHY's `wlc_phy_*` (acphy-attach.md, acphy-api.md).

## Overview

`wlc_attach` (upper layer, `wlc.c`) first attaches the backplane
(`wlc_bmac_si_attach` = `si_attach`: `sih`, the SROM variables `vars` and
their size; stored in `pub+0x100`, `pub+0x108`, `pub+0x110`), allocates its
own state (`wlc`, `pub`, the tunables, its two band states at `wlc+0x50` /
`wlc+0x58`), and then calls

    err = wlc_bmac_attach(wlc, vendor, device, unit, piomode, osh, regsva, bustype, btparam)

which builds the whole hardware-MAC state and leaves the chip **powered
down** (core disabled, crystal request dropped). In order:

1. `wlc_hw_attach` allocates `wlc_hw` (0x1d0 bytes) and three blocks hanging
   off it: the coexistence state `btc` (0x20), the two band states (0x70 =
   2 x 0x38) and the core-state block (0x60, also `wlc+0x28`). Defaults are
   set (retry limits, interrupt mask, chanspec 0x1001, ...).
2. Identity: vendor/device (overridable by SROM variables), the MAC core is
   selected (`regs`), its revision checked, the tunables adapted to the
   device.
3. First contact with the core: PCIe work-arounds, forced fast clock, core
   reset, chip-access check (D3).
4. Board identity from the SROM variables (`boardrev` validated, `sromrev`,
   `boardflags`, `boardflags2`, antenna switch controls), Apple board fix-ups,
   the band count, copies into `pub`.
5. The PHY: the shim (`wlc_phy_shim_attach`), the shared PHY state
   (`wlc_phy_shared_attach` with a 0x58-byte descriptor), then per band:
   switch to the band (`wlc_setxband`), read the MAC capabilities, attach the
   PHY (`wlc_phy_attach`, which returns the same `pi` for both bands on this
   card), read its version, validate it, copy it into the upper layer's band
   state; on the first band also the DMA engines of FIFOs 0..3; initial host
   flag copies.
6. Coexistence defaults, core disable, PMU radio LDO off, PCIe down, the
   backplane interrupt callbacks, crystal off.
7. MAC address (`macaddr`), LED state, template configuration. Result 0.

Any failure returns an error code at once and leaves everything allocated so
far in place; `wlc_attach` then calls `wlc_detach`, whose `wlc_bmac_detach`
undoes what exists (DMA engines, PHY, shared PHY, shim, LED state, the blocks
of `wlc_hw_attach`).

In the emulated session the attach makes 574 card accesses (observed): 2
in `si_setcore`, 11 `si_pcie_ltr_war`, 3 `si_pcieobffenable`, 17 the core
reset, 50 the chip-access check, 344 + 37 the two `wlc_phy_attach` calls
(each with its own core reset), 1 `dma_addrwidth`, 21 + 15 + 15 + 15 the four
`dma_attach`, 2 reads of `D11(0x15c)`, 34 `wlc_coredisable`, 7
`si_pmu_rfldo`. Everything else is memory and SROM-variable work.

## Data

### Arguments of `wlc_bmac_attach`

| # | name | type | this card | use |
|---|---|---|---|---|
| 1 | `wlc` | pointer | | the upper layer's state |
| 2 | `vendor` | u16 | 0x14e4 | PCI vendor id |
| 3 | `device` | u16 | 0x43a0 | PCI device id |
| 4 | `unit` | u32 | 0 | driver instance |
| 5 | `piomode` | u8 | 0 | stored at `wlc_hw+0x1c`; nothing in this area reads it |
| 6 | `osh` | pointer | | OS handle |
| 7 | `regsva` | pointer | | mapped register window (not used by this function) |
| 8 | `bustype` | u32 | 1 (PCI) | not 0: the ids may be overridden from SROM variables (step 2) |
| 9 | `btparam` | pointer | | passed by `wlc_attach`, not used |

Result (u32): 0 success, else an error code (table at the end of Data).

### Allocations

All through `wlc_calloc(osh, unit, size)` = `osl_malloc(osh, size)` and, if
it succeeds, `osl_memset(p, 0, size)` (no message; `unit` unused).

| block | size | where the pointer goes | error if it fails |
|---|---|---|---|
| `wlc_hw` | 0x1d0 | result of `wlc_hw_attach`; `wlc+0x20` (by `wlc_bmac_attach`) | 0x3f2 |
| `btc` | 0x20 | `wlc_hw+0xb0` | 0x3f3 |
| band states | 0x70 | `wlc_hw+0xf0` (2.4 GHz) = the block, `wlc_hw+0xf8` (5 GHz) = block + 0x38 | 0x3f4 |
| core-state block | 0x60 | `wlc_hw+0x08` and `wlc+0x28` | 0x3f5 |
| shim | 0x18 | `wlc_hw+0xd8` (`osl_malloc` + `osl_memset` in `wlc_phy_shim_attach`) | 0x19 |
| shared PHY state | (PHY) | `wlc_hw+0xe0` (`wlc_phy_shared_attach`) | 0x10 |
| `pi` | (PHY) | band state + 0x28 | 0x11 |
| DMA engines | 0x130 each (bmac-data D1) | `wlc_hw+0x20 + 8*fifo` and core-state block + 8*fifo | 0x13 |
| LED state | 0x328 (bmac-run R4) | `wlc_hw+0x198` | 0x17 |

### `wlc_hw` after a successful attach

Value: what attach leaves there on this card (observed in the dump at the
return of `wlc_bmac_attach`; model values marked). Zero fields that attach
does not write are not listed, except where a reader matters. Step numbers
refer to the procedure of `wlc_bmac_attach` below. Readers outside are
given in "Who reads what attach writes".

| off | size | name | value / source | step |
|---|---|---|---|---|
| 0x00 | 8 | `wlc` | argument 1 | D1 |
| 0x08 | 8 | `corestate` | the 0x60-byte core-state block | D1 |
| 0x10 | 8 | `osh` | argument 6 | D1 |
| 0x18 | 4 | `unit` | argument 4 (0) | D1 |
| 0x1c | 1 | `piomode` | argument 5 (0) | 1 |
| 0x20 + 8 i | 8 | `di[i]`, i = 0..5 | DMA engine of FIFO i: FIFOs 0..3 set, 4 and 5 stay 0 | 13.6 |
| 0x80 | 2 | `vendid` | vendor id (0x14e4) | 2 |
| 0x82 | 2 | `deviceid` | device id (0x43a0) | 2 |
| 0x84 | 4 | `corerev` | `si_corerev(sih)` (42) | 4 |
| 0x88 | 1 | `sromrev` | `getintvar("sromrev")` (11) | 8 |
| 0x8a | 2 | `boardrev` | `getintvar("boardrev")`, 0xff replaced by 1 (0x1101 in the model) | 7 |
| 0x8c | 4 | `boardflags` | `getintvar("boardflags")`, Apple fix-ups (0x10401001 in the model) | 8, 9 |
| 0x90 | 4 | `boardflags2` | `getintvar("boardflags2")`, Apple fix-ups (0x00000002 in the model) | 8, 9 |
| 0x94 | 4 | `macintstatus` | 0 (written by the core resets inside) | 6, 13.4 |
| 0x98 | 4 | `macintmask` | 0 (not written) | - |
| 0x9c | 4 | `defmacintmask` | 0xb0e7a860 | 1 |
| 0xa4 | 4 | `machwcap` | `D11(0x15c)` read per band; bit 31 cleared for some revisions, **not for 42** (0 in the model) | 13.2 |
| 0xa8 | 4 | `machwcap_backup` | copy of `machwcap` | 13.2 |
| 0xae | 1 | `ucode_loaded` | 0 (written by `wlc_bmac_btc_wire_set`) | 14 |
| 0xb0 | 8 | `btc` | the 0x20-byte coexistence block | D1 |
| 0xb8 | 8 | `sih` | `pub+0x100` | 1 |
| 0xc0 | 8 | `vars` | `pub+0x108` | 1 |
| 0xc8 | 4 | `vars_size` (name assigned) | `pub+0x110` (0xe44 in the model) | 1 |
| 0xd0 | 8 | `regs` | `si_setcore(sih, 0x812, 0)`: the 802.11 core's registers | 4 |
| 0xd8 | 8 | `physhim` | `wlc_phy_shim_attach` | 11 |
| 0xe0 | 8 | `phy_sh` | `wlc_phy_shared_attach` | 12 |
| 0xe8 | 8 | `band` | current band state: after attach the **5 GHz** one (`+0xf8`; the last `wlc_setxband` of the loop) | 3, 13.1 |
| 0xf0, 0xf8 | 8, 8 | `bandstate[0]`, `[1]` | the band states (2.4 GHz, 5 GHz) | D1 |
| 0x100 | 2 | `txant` | 0 (not written) | - |
| 0x102 | 1 | `shortslot` | 0 | 1 |
| 0x104 | 2 | `SRL` | 7 | 1 |
| 0x106 | 2 | `LRL` | 4 | 1 |
| 0x108 | 2 | `SFBL` | 3 | 1 |
| 0x10a | 2 | `LFBL` | 2 | 1 |
| 0x118 | 4 | `nbands` | 2 (device-id list) | 10 |
| 0x11c | 2 | `chanspec` | 0x1001 | 1 |
| 0x120 + 8 i | 8 | `txavail[i]` | address of the engine's `txavail` word (`di[i]` + 8) for every FIFO with an engine | 13.6 |
| 0x150 | 8 | `xmtfifo_sz_tbl` | `.data+0x330` (MAC rev >= 40) | 13.3 |
| 0x160 | 4 | `pllreq` | 1 if `boardflags` bit 5 is set (`wlc_bmac_pllreq(hw, 1, 1)`), else 0; with 1 the final `wlc_bmac_xtal(hw, 0)` returns at once (observed with `boardflags` = 0x20) | 10 |
| 0x164 | 1 | `suspended_fifos` | 0 (core reset) | 6 |
| 0x168 | 4 | `maccontrol` | 0x04000400 (core reset) | 6 |
| 0x16c..0x177 | | `mac_suspend_depth`, `wake_override`, `mute_override` | 0 | 6 |
| 0x178 | 6 | `hw_etheraddr` | the `macaddr` variable | 15 |
| 0x185 | 1 | `forcefastclk` | 1 (A13) | 6 |
| 0x186 | 1 | `clk` | 0 (the core reset sets 1, `wlc_coredisable` and `wlc_bmac_xtal(0)` clear it) | 14 |
| 0x187 | 1 | `sbclk` | 0 (`wlc_bmac_xtal(0)`) | 14 |
| 0x198 | 8 | `led` | `wlc_bmac_led_attach` | 15 |
| 0x1ac | 4 | `intrcvlazy` | 0x01000000 | 12 |
| 0x1bc | 1 | `btswitch` | 0xff | 1 |
| 0x1bd | 1 | `antswctl2g` | `getintvar("antswctl2g")` (0) | 8 |
| 0x1be | 1 | `antswctl5g` | `getintvar("antswctl5g")` (0) | 8 |
| 0x1c0 | 4 | `pcie2_war_freq` | 0x1fe | 12 |
| 0x1c4 | 4 | (purpose unknown) | 0, written explicitly; no reader found in the code | 1 |

`wlc_bmac_info_init` (.text+0x05fd12) is a stand-alone copy of the defaults
of step 1 (0xb0e7a860 at +0x9c, 0 at +0x1c4 and +0x102, 3/2/7/4 at
+0x108/+0x10a/+0x104/+0x106, 0x1001 at +0x11c, 0xff at +0x1bc; not the
`piomode` byte). Nothing calls it (the attach has the same stores inline).

### Band state (0x38 bytes; `wlc_hw+0xf0` = 2.4 GHz, `wlc_hw+0xf8` = 5 GHz)

| off | size | name | value after attach (both bands unless said) | written by |
|---|---|---|---|---|
| 0x00 | 4 | `bandtype` | 2 (2.4 GHz) / 1 (5 GHz) | step 13.1 |
| 0x04 | 4 | `bandunit` | 0 / 1 | 13.1 |
| 0x08 | 2 x 5 | `mhfs[0..4]` (host flags 1..5) | zeroed in 13.7, then the attach host-flag bits; after attach: 2.4 GHz `mhfs[4]` = 0x0001, everything else 0 (observed). For a 5 GHz-only device the 2.4 GHz band state is never attached but still gets the writes of step 14 (`wlc_bmac_mhf` with `bands` = 2 updates that copy whatever bands exist: `mhfs[4]` = 0x0001 there too; observed with device id 0x43a2 and 0x4352) | 13.7, 14 (`wlc_bmac_btc_mode_set` -> `wlc_bmac_mhf`) |
| 0x14 | 2 | `CWmin` | 0x000f | 13.5 |
| 0x16 | 2 | `CWmax` | 0x03ff | 13.5 |
| 0x18 | 4 | `core_flags` | `wlc_phy_get_coreflags(pi)` (0) | 13.4 |
| 0x1c | 2 | `phytype` | 11 | 13.4 (`wlc_phy_get_phyversion`) |
| 0x1e | 2 | `phyrev` | PHY revision (1 in the model) | 13.4 |
| 0x20 | 2 | `radioid` | 0x2069 | 13.4 |
| 0x22 | 2 | `radiorev` | radio revision (4 in the model) | 13.4 |
| 0x28 | 8 | `pi` | `wlc_phy_attach` (the same `pi` in both band states) | 13.4 |
| 0x30 | 1 | `encore` (name assigned) | `wlc_phy_get_encore(pi)` (0) | 13.4 |

Bytes 0x24..0x27, 0x2c..0x2f (beyond `pi`), 0x31..0x37 are not written.
Note the 32-bit compares on `+0x1c` in the code: the word `phytype |
phyrev << 16` (e.g. 0x00010002 = type 2 rev 1, 0x0003000a = type 10 rev 3).

### `btc` (0x20 bytes) after attach

Layout: bmac-run.md R3. Attach leaves: `wire` (+0x04) = 2 (from
`wlc_bmac_btc_wire_set(0)` on this card), `aa2g` (+0x18) =
`getintvar("aa2g")` truncated to 8 bits (3 in the model), `mode` (+0x00) =
0 and `flags` (+0x08) = 0 (from `wlc_bmac_btc_mode_set(8)`), everything else
0 (observed).

### Core-state block (0x60 bytes; `wlc_hw+0x08` = `wlc+0x28`)

| off | size | name | value |
|---|---|---|---|
| 0x00 + 8 i | 8 | `di[i]`, i = 0..5 | the same DMA handles as `wlc_hw+0x20+8i` (written by `wlc_hw_set_di`) |
| 0x30..0x5f | | (unknown) | never written or read in any run; zero |

### Shim (0x18 bytes, `wlc_hw+0xd8`)

| off | size | name | value |
|---|---|---|---|
| 0x00 | 8 | `wlc_hw` | `wlc_hw` |
| 0x08 | 8 | `wlc` | `wlc` |
| 0x10 | 8 | `wl` | the glue's handle (`wlc+0x10`) |

### Descriptor passed to `wlc_phy_shared_attach` (0x58 bytes, on the stack)

Built field by field on the stack (no memset; every byte of the 0x58 is
written, there are no gaps) from `wlc_hw` and `sih` after step 10. Observed
contents in brackets.

| off | size | field | source |
|---|---|---|---|
| 0x00 | 8 | `osh` | argument 6 |
| 0x08 | 8 | `sih` | `wlc_hw+0xb8` |
| 0x10 | 8 | `physhim` | `wlc_hw+0xd8` |
| 0x18 | 4 | `unit` | argument 4 [0] |
| 0x1c | 4 | `corerev` | `wlc_hw+0x84` [0x2a] |
| 0x20 | 4 | `bustype` | `sih+0x04` [1] |
| 0x24 | 4 | `buscorerev` | `sih+0x0c` [1 in the model] |
| 0x28 | 8 | `vars` | `wlc_hw+0xc0` |
| 0x30 | 2 | `vid` | `wlc_hw+0x80` [0x14e4] |
| 0x32 | 2 | `did` | `wlc_hw+0x82` [0x43a0] |
| 0x34 | 4 | `chip` | `sih+0x3c` [0x4360] |
| 0x38 | 4 | `chiprev` | `sih+0x40` [3] |
| 0x3c | 4 | `chippkg` | `sih+0x44` [0] |
| 0x40 | 4 | `sromrev` | `wlc_hw+0x88` zero-extended [11] |
| 0x44 | 4 | `boardtype` | `sih+0x28` [0x117] |
| 0x48 | 4 | `boardrev` | `wlc_hw+0x8a` zero-extended [0x1101] |
| 0x4c | 4 | `boardvendor` | `sih+0x30` [0x106b] |
| 0x50 | 4 | `boardflags` | `wlc_hw+0x8c` [0x10401001] |
| 0x54 | 4 | `boardflags2` | `wlc_hw+0x90` [2] |

### Upper-layer state written by the attach

`wlc` (`wlc_info_t`):

| off | size | value |
|---|---|---|
| 0x18 | 8 | `regs` (the same as `wlc_hw+0xd0`) |
| 0x20 | 8 | `wlc_hw` |
| 0x28 | 8 | core-state block (by `wlc_hw_attach`; cleared by `wlc_hw_detach`) |
| 0x38 -> +0x00 | 4 | the first word of the structure whose address is `wlc+0x38` (the upper layer's "core" state) = `si_coreidx(sih)` (1), written once per band |
| 0x40 | 8 | current upper-layer band state: `wlc+0x50` (2.4 GHz) or `wlc+0x58` (5 GHz) for a single-band 5 GHz device; then switched by every `wlc_setxband`; after attach `wlc+0x58` |
| 0x60 | 1 | 1 if bus PCI and `si_pci_war16165(sih)` returns true; otherwise not written (0 in the session) |
| 0x228 | 2 | vendor id |
| 0x22a | 2 | device id |
| 0x6bc | 4 | receive offset: 0x28 for MAC rev >= 40, else 0x26 (it held 0x26 before, from `wlc_attach`) |
| 0x72c | 1 | 1 for chip 0x4331 with a PCIe request size > 0x80 (not this card) |
| 0x730..0x743 | 4 x 5 | template configuration (`wlc_template_cfg_init`) |
| 0x818 | 4 | `wlc+0x6bc` rounded to 2 mod 4: 0x2a for rev >= 40 (0x26 otherwise); purpose unknown (name proposed: `rxoffset_align`) |

Each upper-layer band state (`wlc+0x50` -> 2.4 GHz, `wlc+0x58` -> 5 GHz;
the structures are 0x118 bytes apart in the model):

| off | size | value |
|---|---|---|
| 0x00 | 4 | `bandtype` (2 / 1) |
| 0x04 | 4 | `bandunit` (0 / 1) |
| 0x08 | 2 | phy type (11) |
| 0x0a | 2 | phy revision |
| 0x0c | 2 | radio id (0x2069) |
| 0x0e | 2 | radio revision |
| 0x10 | 8 | `pi` |
| 0x18 | 1 | `wlc_phy_get_encore(pi)` (second call) |

`pub` (`wlc+0x00`, `wlc_pub_t`; layout `src/wl/sys/wlc_pub.h`): +0x14
`corerev` (u32), +0x44 `_nbands` (u32), +0x92 `boardrev` (u16), +0x94
`sromrev` (u8), +0x98 `boardflags` (u32), +0x9c `boardflags2` (u32), +0x100
`sih` (rewritten with the same value). The tunables (`pub+0x38` points to
them; `wlc_tunables_override`): word 0 `ntxd`, word 1 `nrxd`, word 3
`nrxbufpost`, word 14 `rxbnd` (values in the procedure).

### Constant tables

**`wlc_chipmatch` device ids** (`.rodata+0x283050`, 82 u16 entries; vendor
must be 0x14e4): 4320 4325 4303 4321 4324 4318 4319 431a 4311 4313 4312 4314
4315 4316 431b 431c 431d 4328 4329 432a 432b 432c 432d 4341 4340 4350 4351
4352 4353 4357 a99d 0576 4354 435f 433f 434f 432f 4334 4335 4336 4337 4338
4339 4716 4748 4727 4343 4360 4346 4347 4348 4331 4332 4333 43aa 4358 4359
435a 43a9 4355 4356 4363 4380 4381 4382 4364 4365 4366 4374 4367 43a0 43a1
43a2 43ae 43af 43b0 43b1 43b2 43b3 43a3 43a4 43a5 (hex). The BCM4360
family's ids 0x4360, 0x43a0, 0x43a1, 0x43a2, 0x4352, 0x43b1..0x43b3 are all
in it.

**Single-band 5 GHz device ids** (`wlc_is_singleband_5g`): 0x4313, 0x4321,
0x432a, 0x431a, 0x431d, 0x4316, 0x4352, 0x432d, 0x4348, 0x435a, 0x43a2,
0x4333, 0x43b3, 0x43b0, 0x434f. Of the family: **0x43a2 and 0x43b3** (and the
device id 0x4352) are 5 GHz only; 0x43a0, 0x43a1, 0x4360, 0x43b1, 0x43b2 are
not.

**Dual-band device ids** (step 10; all others get one band): 0x4319, 0x4324,
0x4312, 0x4328, 0x432b, 0x4314, 0x431b, 0x4350, 0x4353, 0x0576, 0xa99d,
0x4354, 0x4346, 0x435f, 0x4331, 0x4334, 0x4359, 0x4374, 0x4367, 0x4380,
**0x43a0**, 0x43ae, 0x43b1, 0x43a3. Then chips (`sih+0x3c`) 0xa8df, 0xa8d5,
0xa8d9, 0xa8e3, 0xa87b, 0xa8d1, 0xa8db are forced to one band. Note: of the
4360 family only 0x43a0 and 0x43b1 count as dual band; 0x43a1, 0x43b2 and
0x4360 get **one** band (and, not being single-band 5 GHz, that band is the
2.4 GHz one) - read, not observed (unverified whether such cards exist).

**MAC core revisions accepted** (step 5): 4, 5, 7..34, 40..47 (bit masks
0xffffffb0 over revisions 0..31 and 0xff07 over revisions 32..63).

**PHY versions accepted** (step 13.5; bit `phyrev` of the mask, revisions
>= 32 test bit `phyrev & 31`):

| PHY type | mask | revisions |
|---|---|---|
| 0 | 0x1ec | 2, 3, 5, 6, 7, 8 |
| 2 | 0x3c6 | 1, 2, 6, 7, 8, 9 |
| 4 | 0x1f07ff | 0..10, 16..20 |
| 5 | 0x1f | 0..4 |
| 6, 8 | 0xf | 0..3 |
| 7 | 0x3 | 0, 1 |
| 10 | 0x4f | 0..3, 6 |
| **11 (AC)** | **0xff** | **0..7** |
| any other | - | refused (0x12) |

**Transmit FIFO size table** (`wlc_hw+0x150`): MAC rev >= 40: `.data+0x330`
= {0x0009, 0x00b7, 0x0019, 0x0011, 0x0011, 0x0008} (FIFOs 0..5, units of the
microcode; read by `wlc_bmac_xmtfifo_sz_get`, bmac-run R8). Rev < 40:
`.data+0x180 + 12*(corerev - 4)` (one 6-entry table per revision; not
described).

**Template configuration** (`wlc_template_cfg_init(wlc, corerev)`, u32 each):

| `wlc`+ | rev >= 40 | rev < 40 |
|---|---|---|
| 0x730 | 0x6e8 | 0x640 |
| 0x734 | 0x200 | 0x068 |
| 0x738 | 0x200 | 0x100 |
| 0x73c | 1 | 2 |
| 0x740 | 1 | 2 |

Meaning: belongs to the upper layer's template code (`wlc_amsdu_attach`
reads `+0x738`; the others were not seen read); not interpreted here.

### Error codes of `wlc_bmac_attach`

| code | cause | step |
|---|---|---|
| 0x3f2..0x3f5 | an allocation of `wlc_hw_attach` failed (passed through) | 1 |
| 0x0c | `wlc_chipmatch` refused the (possibly overridden) ids | 2 |
| 0x0d | unsupported chip (0x4306/0x4311 cases) or MAC revision | 5 |
| 0x0e | chip-access check failed (D3) | 6 |
| 0x0f | board revision invalid or refused | 7 |
| 0x19 | `wlc_phy_shim_attach` failed | 11 |
| 0x10 | `wlc_phy_shared_attach` failed | 12 |
| 0x11 | `wlc_phy_attach` failed | 13.4 |
| 0x12 | PHY type or revision not supported | 13.5 |
| 0x13 | `wl_alloc_dma_resources` or a `dma_attach` failed | 13.6 |
| 0x15 | no `macaddr` variable | 15 |
| 0x16 | `macaddr` is 00:00:00:00:00:00 or ff:ff:ff:ff:ff:ff | 15 |
| 0x17 | `wlc_bmac_led_attach` failed | 15 |

(0x14 and 0x18 do not occur.)

## Procedures

### wlc_hw_attach (.text+0x0798ff, 241 bytes, name original)

Purpose: allocate `wlc_hw` and its three fixed blocks.
Prototype: `wlc_hw_info_t *wlc_hw_attach(wlc_info_t *wlc, osl_t *osh, uint
unit, uint *err)`. Result: `wlc_hw`, or NULL with `*err` set.

1. `hw = wlc_calloc(osh, unit, 0x1d0)`. NULL: `*err` = 0x3f2, go to 6.
2. `hw+0x00` = `wlc`, `hw+0x10` = `osh`, `hw+0x18` (u32) = `unit`.
3. `hw+0xb0` = `wlc_calloc(osh, unit, 0x20)`. NULL: `*err` = 0x3f3, go to 6.
4. `p = wlc_calloc(osh, unit, 0x70)`; `hw+0xf0` = `p`. NULL: `*err` = 0x3f4,
   go to 6 (`hw+0xf8` stays 0). Else `hw+0xf8` = `p + 0x38`.
5. `hw+0x08` = `wlc_calloc(osh, unit, 0x60)`. NULL: `*err` = 0x3f5, go to 6.
   Else `*err` = 0, `wlc+0x28` = `hw+0x08`, return `hw`.
6. Failure: `wlc_hw_detach(hw)` (also when `hw` is NULL: it does nothing
   then), return NULL.

Observed: four `wlc_calloc` calls of 0x1d0, 0x20, 0x70, 0x60 in this order,
no card access. The failure branches were read only.

### wlc_hw_detach (.text+0x079884, 123 bytes, name original)

Prototype: `void wlc_hw_detach(wlc_hw_info_t *hw)`. If `hw` is NULL: nothing.
Else, with `osh` = `hw+0x10`:

1. `wlc+0x28` = 0 (`wlc` = `hw+0x00`), before anything is freed.
2. If `hw+0xb0` != 0: `osl_mfree(osh, it, 0x20)`.
3. If `hw+0xf0` != 0: `osl_mfree(osh, it, 0x70)` (both band states).
4. If `hw+0x08` != 0: `osl_mfree(osh, it, 0x60)`.
5. `osl_mfree(osh, hw, 0x1d0)`.

The pointers are not cleared (the block is freed). No card access
(observed at the end of the session).

### wlc_bmac_attach (.text+0x06984f, 4629 bytes, name original)

Prototype and result: "Arguments of `wlc_bmac_attach`" above. `vars` below
is `pub+0x108` (read in step 1 and used for every variable lookup up to step
14; step 15 uses `wlc_hw+0xc0`, the same pointer). `getintvar(vars, name)`
returns 0 for a missing variable (utils.md). On every error the function
returns the code at once; nothing is undone (the caller does it, see
`wlc_bmac_detach`).

**Step 1 - state and defaults.** (observed)

1. `hw = wlc_hw_attach(wlc, osh, unit, &e)`; NULL: return `e`.
2. `wlc+0x20` = `hw`.
3. Defaults, in this order: `shortslot` (+0x102, u8) = 0; `SFBL` (+0x108,
   u16) = 3; `LFBL` (+0x10a) = 2; `SRL` (+0x104) = 7; `LRL` (+0x106) = 4;
   `piomode` (+0x1c, u8) = argument 5; `defmacintmask` (+0x9c, u32) =
   0xb0e7a860; +0x1c4 (u32) = 0; `chanspec` (+0x11c, u16) = 0x1001;
   `btswitch` (+0x1bc, u8) = 0xff.
4. From `pub` (`wlc+0x00`): `sih` (+0xb8) = `pub+0x100`; `vars` (+0xc0) =
   `pub+0x108`; +0xc8 (u32) = `pub+0x110` (the size of `vars`).

**Step 2 - identity.** If `bustype` != 0 (read: the "SI bus" 0 skips this;
observed with 1):

1. `v = getvar(vars, "vendid")`; if found: `vendor` = `bcm_strtoul(v, NULL,
   0)` (low 16 bits, no other check).
2. `v = getvar(vars, "devid")`; if found and `bcm_strtoul(v, NULL, 0)`
   truncated to 16 bits is not 0xffff: `device` = that value.
3. If `wlc_chipmatch(vendor, device)` is false: return **0x0c**.

(On this card neither variable exists - observed: both lookups return NULL -
and the ids from the glue are used.) Then, whatever the bus:
`deviceid` (+0x82) = `device`, `vendid` (+0x80) = `vendor`.

**Step 3 - current band pointers.** (observed) `s =
wlc_is_singleband_5g(device)`. `band` (+0xe8) = `s ? hw+0xf8 : hw+0xf0`
(the pointer stored there, i.e. the 5 GHz or 2.4 GHz band state). Then
`wlc_is_singleband_5g(hw+0x82)` again: `wlc+0x40` = the pointer at `s ?
wlc+0x58 : wlc+0x50`.

**Step 4 - the core.** (observed)

1. `regs` (+0xd0) = `si_setcore(sih, 0x812, 0)` (the 802.11 core; 2 accesses
   in the session, chip.md).
2. `corerev` (+0x84) = `si_corerev(sih)` (42).
3. `wlc_tunables_override(pub+0x38 (the pointer), device, corerev)` (helper
   below).
4. `wlc+0x18` = `regs` (the value of 4.1; used again in 13.2 and 13.4).

**Step 5 - refused chips and revisions.** (the session passes; the
refusals for revisions 6 and 35 and chip 0x4311 rev 0 were made to happen,
the 0x4306 case was read only)

1. Chip id (`sih+0x3c`) 0x4306: return **0x0d** if chip revision
   (`sih+0x40`) > 2 and `device` is 0x4324 or 0x4321 and chip package
   (`sih+0x44`) != 1. Chip 0x4311 with chip revision 0: return **0x0d**.
2. If `corerev` is not one of 4, 5, 7..34, 40..47 (Data): return **0x0d**.

**Step 6 - first contact.** (observed)

1. `si_clkctl_init(sih)` (pmu.md/chip.md; no access in the session).
2. `si_pcie_ltr_war(sih)` (chip.md; 11 accesses).
3. Chip 0x4350 with `(sih+0x48 & 0x700000)` = 0x300000 only:
   `si_pcieltrenable(sih, 1, 1)` (not this card).
4. If bus type (`sih+0x04`) = 1 and bus core id (`sih+0x08`) = 0x83c (PCIe
   Gen2) and bus core revision (`sih+0x0c`) <= 4: `si_pcieobffenable(sih, 1,
   0)` (3 accesses; the model's PCIe core has revision 1 - whether the real
   card's core revision is <= 4 is unverified).
5. `sub_064887(hw, 0)` (A13, fast clock): `clk` is 0, so no access; it sets
   `forcefastclk` (+0x185) = 1.
6. `wlc_bmac_corereset(hw, 0xffffffff)` (A14; 17 accesses; sets `clk` = 1,
   `maccontrol` = 0x04000400, clears +0x164/+0x170/+0x174/+0x94).
7. If `wlc_bmac_validate_chip_access(hw)` (D3; 50 accesses) is false: return
   **0x0e**.

**Step 7 - board revision.** (observed for the valid case; 17 other values
and board types made to happen, see Verification)

1. `r = getintvar(vars, "boardrev")`; if `r` = 0xff: `r` = 1. `boardrev`
   (+0x8a, u16) = `r` (truncated to 16 bits). All tests below use the u16.
2. Format check, `ok`:
   * `r` = 0: not ok.
   * 1 <= `r` <= 0xff: ok.
   * `r` > 0xff: ok only if all of: bits 12..15 are 1 or 2; bits 8..11 are
     1..9; bits 4..7 are 0..9; bits 0..3 are 0..9 (a "1.x.y.z"/"2.x.y.z"
     decimal-digit revision).
3. If the board vendor (`sih+0x30`) is 0x14e4 (a Broadcom reference board),
   with board type `t` = `sih+0x28`: `t` = 0x417 or 0x418 and `r` <= 0x3f:
   return **0x0f**; `t` = 0x40c: return **0x0f**; `t` = 0x421 and `r` <= 0x50:
   return **0x0f**.
4. If not `ok`: return **0x0f**.

The Apple card (board vendor 0x106b) only goes through 2 and 4.

**Step 8 - board variables.** (observed) `sromrev` (+0x88, u8) =
`getintvar(vars, "sromrev")`; `boardflags` (+0x8c, u32) = `getintvar(vars,
"boardflags")`; `boardflags2` (+0x90, u32) = `getintvar(vars,
"boardflags2")`; `antswctl2g` (+0x1bd, u8) = `getintvar(vars,
"antswctl2g")`; `antswctl5g` (+0x1be, u8) = `getintvar(vars,
"antswctl5g")`. In this order.

**Step 9 - Apple fix-ups.** (the model's board type 0x117 takes none; the
three cases were made to happen by changing `sih+0x28`, see Verification)
Only if the board vendor (`sih+0x30`) is 0x106b, by board type (`sih+0x28`):

* 0x4e: if `boardrev` > 0x40: `boardflags` |= 0x00000002.
* 0xe4: if `boardrev` <= 0x1500: `boardflags` |= 0x00400000 and
  `boardflags2` = 0.
* 0xef: if `boardrev` <= 0x1201: the same as for 0xe4.
* any other (0x117 of the MacBook Air card): nothing.

**Step 10 - PLL request, band count, copies.** (observed)

1. If `corerev` <= 4 or `boardflags` bit 5 (0x20) is set:
   `wlc_bmac_pllreq(hw, 1, 1)` (C9). (Not on this card.)
2. If bus type = 1 and `si_pci_war16165(sih)` returns true: the byte
   `wlc+0x60` = 1 (otherwise it is not written; false in the session).
3. `nbands` (+0x118) = 2 if `deviceid` is in the dual-band list (Data), else
   1; then, if the chip id is one of the seven of that list, 1.
4. `wlc+0x228` (u16) = `vendid`; `wlc+0x22a` (u16) = `deviceid`.
5. `pub+0x100` = `sih`; `pub+0x14` = `corerev`; `pub+0x94` (u8) = `sromrev`;
   `pub+0x92` (u16) = `boardrev`; `pub+0x98` = `boardflags`; `pub+0x9c` =
   `boardflags2`; `pub+0x44` = `nbands`.

**Step 11 - PHY shim.** (observed) `physhim` (+0xd8) =
`wlc_phy_shim_attach(hw, wlc+0x10 (wl), wlc)`; NULL: return **0x19**.

**Step 12 - shared PHY state.** (observed)

1. Fill the descriptor (Data) and `phy_sh` (+0xe0) =
   `wlc_phy_shared_attach(&descriptor)`; NULL: return **0x10**.
2. `intrcvlazy` (+0x1ac) = 0x01000000; `pcie2_war_freq` (+0x1c0) = 0x1fe.
3. `o` = 0x28 if `corerev` >= 40 else 0x26; `wlc+0x6bc` = `o`; `wlc+0x818` =
   `o` if bit 1 of `o` is set, else `o + 2` (0x2a here).

**Step 13 - per band.** For `i` = 0 while `i` < `nbands` (observed for both
bands):

13.1. If `wlc_is_singleband_5g(deviceid)`: `i` = 1 (so a single-band 5 GHz
  device does only the 5 GHz band). `wlc_setxband(hw, i)` (acphy-chanspec.md
  section 2: switches `hw+0xe8` and `wlc+0x40` to band `i`; no access here
  because `sbclk` is 0). Then `band->bandunit` (+0x04) = `i`;
  `band->bandtype` (+0x00) = 2 if `i` = 0 else 1; the same two words in the
  upper layer's band state (`wlc+0x40`): +0x04 = `i`, +0x00 = the type.
  The first u32 of the structure at `wlc+0x38` = `si_coreidx(sih)`.

13.2. If `corerev` > 12: `c` = 32-bit read of `D11(0x15c)` (at `regs` of
  step 4; observed 0 in the model); `machwcap` (+0xa4) = `c`, and bit 31
  is cleared (`c & 0x7fffffff`) when `corerev` is 0x1d, 0x1e, 0x21, 0x22, 0x28,
  0x29, 0x2b or 0x2c, or 0x1a with chip revision 0 - **not for 42**.
  `machwcap_backup` (+0xa8) = `machwcap`. (Read once per band.)

13.3. `xmtfifo_sz_tbl` (+0x150) = `.data+0x330` if `corerev` >= 40, else
  `.data+0x180 + 12*(corerev-4)`. `wlc_bmac_ampdu_set(hw, 1)` (R8: nothing
  on revision 42).

13.4. PHY:
  1. `p = wlc_phy_attach(phy_sh, regs, band->bandtype, vars)` - the third
     argument is the band **type** (2 then 1), the fourth `vars`;
     `band->pi` (+0x28) = `p`; NULL: return **0x11**. (Observed: 344
     accesses for the 2.4 GHz band, 37 for the 5 GHz one, which gets the
     same `pi` back.)
  2. If `band->phytype` (+0x1c, u16) != 11: `wlc_bmac_set_btswitch(hw, -1)`.
     The type is filled in only by 13.4.4, so this call is made on both
     bands of a fresh attach (and refused with -23: observed twice).
  3. `wlc_phy_machwcap_set(pi, machwcap)`.
  4. `wlc_phy_get_phyversion(pi, &band->phytype, &band->phyrev,
     &band->radioid, &band->radiorev)` (u16 pointers at +0x1c, +0x1e,
     +0x20, +0x22).
  5. `band+0x30` (u8) = `wlc_phy_get_encore(pi)`; then (second call) the
     byte +0x18 of the upper layer's band state = `wlc_phy_get_encore(pi)`.
  6. `band->core_flags` (+0x18, u32) = `wlc_phy_get_coreflags(pi)`.

13.5. Version check: the mask of `band->phytype` (table in Data; unknown type
  -> return **0x12**); if bit (`band->phyrev` & 31) of it is clear: return
  **0x12**. Then the upper layer's band state (`wlc+0x40`): +0x10 = `pi`,
  +0x08 (u16) = phy type, +0x0a = phy revision, +0x0c = radio id, +0x0e =
  radio revision. `band->CWmin` (+0x14) = 0x000f, `band->CWmax` (+0x16) =
  0x03ff.

13.6. DMA, only while `di[0]` (+0x20) is 0 (that is: in the first pass):
  1. `name` = `osl_snprintf(buf, 8, "wl%d", unit)` (an 8-byte buffer on the
     stack: "wl0"). Let `T` = the tunables (`pub+0x38`): `T[0]` ntxd,
     `T[1]` nrxd, `T[2]` rxbufsize, `T[3]` nrxbufpost (u32 words).
  2. `w = dma_addrwidth(sih, regs + 0x200)` (P2; 1 access);
     `wl_alloc_dma_resources(wlc+0x10, w)` (the glue; returns TRUE); false:
     return **0x13**.
  3. FIFO 0: `di = dma_attach(osh, name, sih, regs+0x200, regs+0x220,
     T[0], T[1], T[2], 0, T[3], wlc+0x6bc, &wl_msg_level)` (for `corerev` <
     11 the receive channel is at `regs+0x210`); NULL: return **0x13**.
     Observed arguments: 0x400, 0x400, 0x800, 0, 0x100, 0x28.
  4. If bus type = 1: first store the burst/prefetch globals of bmac-data
     P54 (16-bit words `.data+0x340` = 1, `+0x344` = 3, `+0x342` = 3,
     `+0x346` = 6, `+0x34c` = 3, `+0x34a` = 3, `+0x34e` = 3, in this order),
     then `sub_069659(hw, 1, di)`. Else if bus type = 0:
     `sub_069659(hw, 0, di)`. Other bus types: no call.
  5. `wlc_hw_set_di(hw, 0, di)`.
  6. FIFOs 1 and 2: `di = dma_attach(osh, name, sih, txregs, 0, T[0], 0, 0,
     -1, 0, 0, &wl_msg_level)` with `txregs` = `regs+0x240` / `regs+0x280`
     (`corerev` <= 10: `+0x220` / `+0x240`); NULL: return **0x13**;
     `sub_069659(hw, sih+0x04 (bus type), di)`; `wlc_hw_set_di(hw, fifo, di)`.
  7. FIFO 3: the same with `txregs` = `regs+0x2c0` (`corerev` <= 10:
     `+0x260`); (`corerev` 4 only: `txregs` `+0x260`, `rxregs` `+0x270`,
     nrxd `T[1]`, rxbufsize 0x30, nrxpost `T[3]`); NULL: return **0x13**;
     `sub_069659`, `wlc_hw_set_di(hw, 3, di)`.
  8. For FIFO `f` = 0..5 with `di[f]` != 0: `txavail[f]` (+0x120 + 8 f) =
     the engine's slot 0x108 (`dma_getvar(di, "&txavail")`, the address of
     its `txavail` word).
  (Observed: 21 + 15 + 15 + 15 accesses, the four engines, the seven
  `dma_param_set` calls each; bmac-data D7.)

13.7. Host-flag copies of the band (`mhfs` at `band+0x08`, five u16):
  `osl_memset(band+0x08, 0, 10)`, then:
  * phy type 2 rev 1 (the u32 at `band+0x1c` = 0x00010002): `mhfs[0]` |= 0x0020;
  * phy type 2, `boardflags` bit 1 and the u16 `hw+0xac` != 0: `mhfs[0]` |=
    0x0040;
  * phy type 2 with phy rev <= 2: `mhfs[0]` |= 0x0008 unless bit 0x0040 is
    set; any other PHY: `mhfs[0]` |= 0x0008 if the chip is 0x4331 or 0xa9a7
    and the chip package is 9 or 0xb;
  * `boardflags` bit 5 (0x20): `mhfs[0]` |= 0x0400;
  * radio 0x2050 with radio rev <= 5: `mhfs[1]` |= 0x0004;
  * phy type 4 rev <= 1: `mhfs[1]` |= 0x0800 and `mhfs[0]` |= 0x0200;
  * phy type 4 and `sromrev` > 8: `mhfs[1]` |= 0x0080;
  * `corerev` >= 40 and `si_chip_hostif(sih)` = 2: `mhfs[2]` |= 0x0010.
  For the BCM4360 only the 0x0400 (`boardflags` bit 5) and the `mhfs[2]`
  0x0010 (host interface 2) rules can apply; on the emulated card neither
  does (boardflags 0x10401001, `si_chip_hostif` = 1: observed), so the copies
  stay 0 until step 14.

  `i` += 1, next band.

**Step 14 - after the bands.** (observed)

1. Chip 0x4331 only, if `si_pcie_get_request_size(sih)` > 0x80: for every
   FIFO with an engine `dma_ctrlflags(di, 0x20, 0x20)` (slot 0x118), and the
   byte `wlc+0x72c` = 1.
2. `wlc_bmac_btc_wire_set(hw, 0)` (R3: `btc.wire` = 2 on this card,
   `ucode_loaded` = 0).
3. `btc.aa2g` (`btc+0x18`, u8) = `getintvar(vars, "aa2g")` (3 in the model).
4. `m` = 1 if the current band's (the last one of the loop: 5 GHz) phy type
   is 6; else 5 if the u32 at `band+0x1c` is 0x0003000a (type 10 rev 3);
   else 8. `wlc_bmac_btc_mode_set(hw, m)` (R3; 8 = auto, resolved to 0 on
   the emulated card; it writes the host-flag copies through
   `wlc_bmac_mhf`, which leaves `mhfs[4]` = 0x0001 in the 2.4 GHz band
   state).
5. `wlc_coredisable(hw)` (C7; 34 accesses: radio off, analog core off, core
   disable; `clk` = 0).
6. Chip 0x4352, 0x4360 or 0xaa06: `si_pmu_rfldo(sih, 0)` (pmu.md; 7
   accesses).
7. Bus type = 1: `si_pci_down(sih)` (chip.md; no access in the session).
8. `si_register_intr_callback(sih, sub_061268, sub_061285, NULL, hw)` (R9).
9. `wlc_bmac_xtal(hw, 0)` (C9: `pllreq` is 0, so the crystal request is
   dropped: `si_clkctl_xtal(sih, 3, 0)` - no access on PCIe -, `sbclk` = 0,
   `clk` = 0, `wlc_phy_hw_clk_state_upd(pi, 0)`). With `boardflags` bit 5
   (step 10.1) `pllreq` is 1 and the call returns at once; `sbclk` and
   `clk` are 0 anyway (never set / cleared by 14.5).

**Step 15 - MAC address and the rest.** (observed for the success; 0x15,
0x16 and 0x17 made to happen, see Verification)

1. `v = getvar(hw+0xc0, "macaddr")`; NULL: return **0x15**.
2. `bcm_ether_atoe(v, hw+0x178)` (helper below; its result is ignored).
3. If all six bytes are 0xff, or all six are 0: return **0x16**.
4. `led` (+0x198) = `wlc_bmac_led_attach(hw)` (R4); NULL: return **0x17**.
5. `wlc_template_cfg_init(wlc, corerev)`; return 0.

### wlc_bmac_detach (.text+0x066cae, 324 bytes, name original)

Prototype: `int wlc_bmac_detach(wlc_info_t *wlc)`. Result: what
`wlc_bmac_led_detach` returned (1 if the LED timer could not be deleted), or
0. `wlc_detach` adds it to its count of callbacks still pending.

`hw` = `wlc+0x20`; if NULL: return 0. Else:

1. If `sih` (+0xb8) != 0: `si_deregister_intr_callback(sih)`; and if its bus
   type is 1: `si_pci_sleep(sih)`. (Both also when the attach failed before
   registering anything.)
2. For FIFO `f` = 0..5: if `di[f]` (+0x20 + 8 f) != 0: `dma_detach(di)`
   (slot 0x000 of its table, P5) and `wlc_hw_set_di(hw, f, NULL)`.
3. PHYs: `b` = `hw+0xe8` (the current band state); for `k` = 0 while `k` <
   `nbands`: if `wlc_is_singleband_5g(deviceid)`: `k` = 1; if `b->pi` != 0:
   `wlc_phy_detach(pi)` and `b->pi` = 0; then `b` = `bandstate[u]` with
   `u` = 0 if the **upper layer's** current band (`wlc+0x40`, its +0x04
   `bandunit`) is 1, else 1; `k` += 1. So the second pass takes the band
   other than the upper layer's current one. (Observed: two passes, the same
   `pi` passed to `wlc_phy_detach` twice - each band state holds it; the
   PHY counts its users.) The local `b` is not stored back.
4. `wlc_phy_shared_detach(phy_sh)` (+0xe0), `wlc_phy_shim_detach(physhim)`
   (+0xd8) (both called even if NULL; both accept NULL).
5. `vars` (+0xc0) = 0, `sih` (+0xb8) = 0 (they belong to `wlc_attach`,
   which frees them after this through `wlc_bmac_si_detach` and
   `osl_mfree`).
6. If `led` (+0x198) != 0: result = `wlc_bmac_led_detach(hw)` (R4), `led` =
   0.
7. `wlc_hw_detach(hw)`; `wlc+0x20` = 0.

No card access (observed: the detach step of the session makes none inside
this function).

### Helpers

**`wlc_hw_set_di(hw, fifo, di)`** (.text+0x0796a7, 21 bytes, name original;
`fifo` u32, no range check): `hw+0x20 + 8*fifo` = `di` and
`*(hw+0x08) + 8*fifo` = `di` (the same handle in `wlc_hw` and in the
core-state block, which is `wlc+0x28` for the upper layer). Callers: attach
(13.6, FIFOs 0..3), detach (with NULL). Observed.

**`wlc_hw_get_txavail(hw, out)`** (.text+0x0796ee, 30 bytes, name original):
copies the six 64-bit words `hw+0x120 .. hw+0x148` (`txavail[0..5]`, NULL
for FIFOs without an engine) to `out[0..5]`. Caller: `wlc_attach`, right
after the attach, with `out` = (the structure at `wlc+0x38`) + 8: this is
the writer of the `&txavail` copies that bmac-data D8 found at
`wlc->core+0x08+8*fifo` and left unidentified (the call was read in
`wlc_attach`, which passes its `wlc_hw` and the structure at `wlc+0x38`
plus 8; observed as the only reader of `+0x120..+0x148`).

**`wlc_hw_get_bandunit(hw)`** (.text+0x0796de, 16 bytes, name original):
returns the u32 `bandunit` (+0x04) of the current band state (`hw+0xe8`).
Caller: `sub_039dd4` (wlc.c). Listed because it reads attach state from the
upper layer.

**`wlc_template_cfg_init(wlc, corerev)`** (.text+0x079724, 113 bytes, name
original): writes the five u32 of the table "Template configuration" (Data)
by `corerev` >= 40 (0x28) or not, in the order 0x730, 0x734, 0x738, 0x73c,
0x740. Observed (rev 42 values).

**`wlc_bmac_si_attach(device, osh, regs, bustype, btparam, &vars, &varsz)`**
(.text+0x066e05, 23 bytes, name original): calls `si_attach` with the same
seven arguments (the seventh, on the stack, is copied to the new frame) and
returns its result (`sih`). Caller: `wlc_attach`, before the upper layer's
allocations. (chip.md specifies `si_attach`; it is taken in the hybrid
build.)

**`wlc_bmac_si_detach(osh, sih)`** (.text+0x066df2, 19 bytes, name original):
if `sih` != NULL: `si_detach(sih)`. Callers: `wlc_attach` (failure after
the backplane attach), `wlc_detach` (after `wlc_bmac_detach`, with
`pub+0x100`; it then clears `pub+0x100` and frees `vars` with the size
`pub+0x110`).

**`wlc_phy_shim_attach(hw, wl, wlc)`** (.text+0x152b8b, 81 bytes, name
original, `wlc_phy_shim.c`): `s = osl_malloc(hw+0x10 (osh), 0x18)`; if not
NULL: `osl_memset(s, 0, 0x18)`, `s+0x00` = `hw`, `s+0x08` = `wlc`, `s+0x10`
= `wl`. Returns `s` (NULL on failure). Observed.

**`wlc_phy_shim_detach(s)`** (.text+0x152b6c, 31 bytes, name original): if
`s` != NULL: `osl_mfree(*(s+0x00)+0x10 (osh of the `wlc_hw` it points to),
s, 0x18)`. Note: it reads `wlc_hw->osh` through the shim, so it must run
before `wlc_hw` is freed (it does: step 4 of the detach).

**`wlc_chipmatch(vendor, device)`** (.text+0x0251ca, 42 bytes, name original,
wlc.c; u16 arguments) -> bool: true iff `vendor` = 0x14e4 and `device` is in
the 82-entry list (Data). (Vendor 0x106b is not accepted: an Apple-branded
card must present Broadcom's vendor id, which the BCM4360 does; observed.)

**`wlc_is_singleband_5g(device)`** (.text+0x025eb0, 136 bytes, name original,
wlc.c; the u32 argument compared as is) -> bool: true iff `device` is one of
the 15 ids of the list in Data. Observed false for 0x43a0 (six calls per
session: two in step 3, one per band in step 13, one per band in the detach).

**`wlc_tunables_override(T, device, corerev)`** (.text+0x04e83c, 89 bytes,
name original, wlc_alloc.c): if `device` is 0x43a0..0x43a2, 0x43b1..0x43b3,
0x43a3, 0x43a4 or 0x43a5, and `corerev` > 41 and `corerev` != 44: `T[0]`
(ntxd) = 0x400, `T[1]` (nrxd) = 0x400, `T[14]` (rxbnd) = 0x20, `T[3]`
(nrxbufpost) = 0x100 (u32 words, in this order). Otherwise nothing.
Observed (the defaults before were 0x200, 0x100, 8, 0x40).

**`wlc_calloc(osh, unit, size)`** (.text+0x04eb8d, 46 bytes, name original,
wlc_alloc.c): `p = osl_malloc(osh, size)`; if `p`: `osl_memset(p, 0, size)`.
Returns `p`. `unit` is not used.

**`bcm_ether_atoe(s, ea)`** (.text+0x00a8ac, 79 bytes, name original,
bcmutils.c) -> bool: up to six times: byte = `bcm_strtoul(s, &end, 16)`
(low 8 bits) stored to `ea[k]`; stop after storing if `*end` is NUL; else
`s` = `end + 1` (one separator character of any kind skipped) and `k` += 1.
Result: true iff six bytes were stored. A short string leaves the remaining
bytes of `ea` as they were (zero in a fresh `wlc_hw`). Observed:
"00:90:4c:0d:f4:3e" of the model -> 00 90 4c 0d f4 3e.

**`wlc_bmac_info_init`** (.text+0x05fd12, 85 bytes, name original): see Data
("`wlc_hw` after a successful attach"); dead code.

## Who reads what attach writes

Method (observed): a memory hook over every block the attach creates
(`wlc_hw`, `btc`, the band states, the core-state block, the shim, the LED
state) and over every upper-layer field it writes, from the return of
`wlc_bmac_attach` to the end of a session on the object alone (attach,
mpc 0, up, channel 1, a **scan** (22 probe requests: the transmit path),
watchdog, 6, 11, 36, 36/40, 36/80, 149/80, 157/40, 165, watchdog, down, up,
36/80, watchdog, down, detach); every access is attributed to the function
and source file of the instruction (`re-out\analysis\bmac-attach\readers.py`,
`readers-all.txt`, `fieldmap.txt`). Readers are grouped:

* **U** - the upper layer and other files (stays Broadcom's as long as the
  upper layer does);
* **B** - functions of `wlc_bmac.c`/`wlc_hw.c` whose Broadcom body still runs
  in the hybrid build (not taken in `hybrid\takeover.txt`, called from
  Broadcom code);
* **O** - functions open in the hybrid build (taken ones, and Broadcom's
  static helpers that only taken functions call - those run as open code).

Accesses inside the attach itself are not counted (it is the writer).

### `wlc_hw`

| off | field | U | B | O |
|---|---|---|---|---|
| 0x00 | `wlc` | - | `wlc_bmac_led_detach`, `wlc_bmac_txant_set`, `wlc_hw_detach` | many (up/down, init, mute, objmem, chanspec) |
| 0x08 | `corestate` | - | `wlc_hw_detach` | - |
| 0x10 | `osh` | `wlc_phy_shim_detach` | `wlc_bmac_led_detach`, `wlc_hw_detach` | `wlc_bmac_hw_up`, `wlc_bmac_switch_macfreq` |
| 0x20..0x38 | `di[0..3]` | - | `wlc_bmac_detach`, `wlc_bmac_watchdog` (FIFO 0) | `sub_05fefa`, `wlc_bmac_init`, `wlc_bmac_tx_fifo_suspend/resume` |
| 0x40, 0x48 | `di[4..5]` (0) | - | `wlc_bmac_detach` | `sub_05fefa`, `wlc_bmac_init` |
| 0x82 | `deviceid` | - | `wlc_bmac_detach` | `wlc_bmac_set_chanspec` |
| 0x84 | `corerev` | - | `sub_062684`, `wlc_bmac_wait_for_wake`, `wlc_bmac_watchdog` | many |
| 0x8c | `boardflags` | - | `wlc_bmac_seci_upd` | `wlc_bmac_hw_up/hw_down/init` |
| 0x90 | `boardflags2` | - | `wlc_bmac_seci_upd`, `wlc_bmac_led` (byte +0x91) | `wlc_bmac_hw_up/hw_down/init` |
| 0x94 | `macintstatus` | `wlc_intrsoff` | - | `wlc_bmac_corereset` (W), `wlc_bmac_set_chanspec` |
| 0x98 | `macintmask` | `wlc_intrsoff` (R/W), `wlc_intrson` (W), `wlc_intrsrestore` (W) | - | - |
| 0x9c | `defmacintmask` | `wlc_intrson` | - | - |
| 0xa4 | `machwcap` | - | `wlc_bmac_state_get` | `wlc_bmac_init` (also its byte +0xa6) |
| 0xae | `ucode_loaded` | - | - | `sub_0607b0`, `wlc_bmac_hw_up` (W) |
| 0xb0 | `btc` | - | `wlc_bmac_btc_params_get`, `_period_get`, `_wire_get`, `wlc_hw_detach` | `wlc_bmac_btc_mode_get`, `wlc_bmac_init`, A6/A7, `sub_06106b`, `sub_06333d` |
| 0xb8 | `sih` | `wlc_attach` | `wlc_bmac_4331_epa_init`, `wlc_bmac_4360_pcie2_war`, `wlc_bmac_detach`, `wlc_bmac_led`, `wlc_bmac_led_hw_deinit`, `wlc_bmac_seci_upd`, `wlc_bmac_set_ctrl_ePA` | many |
| 0xc0 | `vars` | - | `wlc_bmac_detach` (W) | `wlc_bmac_init` |
| 0xd0 | `regs` | `wlc_intrsoff`, `wlc_intrson`, `wlc_intrsrestore` | `sub_064887`, `wlc_bmac_btc_period_get`, `wlc_bmac_fifoerrors` | many |
| 0xd8 | `physhim` | - | `wlc_bmac_detach` | - |
| 0xe0 | `phy_sh` | - | `wlc_bmac_detach` | - |
| 0xe8 | `band` | - | `wlc_bmac_antsel_type_set`, `_detach`, `_filter_war_upd`, `_state_get`, `_wait_for_wake`, `_watchdog`, `wlc_hw_get_bandunit` | many; `wlc_setxband` (W) |
| 0xf0, 0xf8 | `bandstate[]` | - | `wlc_bmac_detach`, `wlc_hw_detach` | `wlc_bmac_mhf`, `wlc_bmac_set_chanspec` |
| 0x100 | `txant` | - | `wlc_bmac_txant_set` (W), `sub_062684` | - |
| 0x102 | `shortslot` | - | - | `sub_06656c` |
| 0x104, 0x106 | `SRL`, `LRL` | - | - | `wlc_bmac_retrylimit_upd` (W) |
| 0x108, 0x10a | `SFBL`, `LFBL` | - | - | `wlc_bmac_init` |
| 0x10c | `up` | - | `sub_061268`, `sub_061285`, `wlc_bmac_txant_set`, `wlc_bmac_watchdog` | up/down, retry limits, chanspec |
| 0x110 | `now` | - | `wlc_bmac_watchdog` (R/W) | - |
| 0x118 | `nbands` | - | `wlc_bmac_detach` | `wlc_bmac_set_chanspec` |
| 0x11c | `chanspec` | - | - | `sub_06212d`, `wlc_bmac_set_chanspec` (W) |
| 0x120..0x148 | `txavail[]` | - | `wlc_hw_get_txavail` (for `wlc_attach`) | - |
| 0x150 | `xmtfifo_sz_tbl` | - | `wlc_bmac_xmtfifo_sz_get` (for `wlc_attach`, `wlc_ampdu_mode_upd`) | - |
| 0x164..0x177 | MAC control state | - | - | A1-A7, A14 |
| 0x178 | `hw_etheraddr` | - | - | `wlc_bmac_hw_etheraddr` (copy), `wlc_bmac_write_amt` |
| 0x180 | `led_gpio_mask` | - | `wlc_bmac_led_hw_mask_init` (W) | `wlc_bmac_hw_up` |
| 0x184, 0x185 | `noreset`, `forcefastclk` | - | `sub_064887` (W, at attach) | clocks, init, chanspec |
| 0x186 | `clk` | `wlc_intrsoff`, `wlc_intrsrestore` | `wlc_bmac_set_ctrl_ePA` | many |
| 0x187 | `sbclk` | - | `wlc_bmac_led`, `wlc_bmac_led_hw_deinit` | `sub_06333d`, `wlc_bmac_hw_up`, C9, C13 |
| 0x192 | `fastpwrup_dly` | - | `wlc_bmac_wait_for_wake` | `wlc_bmac_init` (R/W) |
| 0x198 | `led` | - | `wlc_bmac_detach`, `wlc_bmac_led`, `wlc_bmac_led_detach` | `wlc_bmac_hw_up` |
| 0x1a1, 0x1a4 | `antsel_type`, `antsel_avail` | - | `wlc_bmac_antsel_type_set` (W), `wlc_bmac_antsel_set` (W) | `wlc_bmac_init` |
| 0x1ac | `intrcvlazy` | - | - | `wlc_bmac_init` |
| 0x1b4, 0x1b7 | (not written by attach) | - | `wlc_skip_adjtsf` | - |
| 0x1bc | `btswitch` | `wlc_up` | - | - |
| 0x1c0 | `pcie2_war_freq` | - | - | `wlc_bmac_hw_up` |

No reader at all after the attach in this session: `unit` (+0x18),
`piomode` (+0x1c), `vendid` (+0x80), `sromrev` (+0x88), `boardrev` (+0x8a),
`machwcap_backup` (+0xa8: read by `wlc_bmac_btc_mode_set` only for a mode
other than 0 with a wire type > 2), `vars_size` (+0xc8),
`antswctl2g`/`antswctl5g` (+0x1bd/+0x1be: read only by
`wlc_bmac_set_extlna_pwrsave_shmem` (B10, open), and only for chip 0x4331 -
a static search of the decompiled code), +0x1c4 (no reader anywhere in the
decompiled code: written only here and by the dead `wlc_bmac_info_init`).

### Band states, `btc`, core-state block, shim, LED state

* **Band states**: no U reader. B: `wlc_hw_get_bandunit` (+0x04, for
  `sub_039dd4` of `wlc.c`), `wlc_bmac_wait_for_wake` (+0x1c, also called
  from `sub_03a14c` of `wlc.c`), `wlc_bmac_detach`, `_filter_war_upd`,
  `_watchdog`, `_state_get`, `_antsel_type_set` (+0x28 `pi`). Everything
  else (host-flag copies, CW, PHY identity, core flags) is read by open code
  only.
* **`btc`**: no U reader. B: `wlc_bmac_btc_mode_set` (W +0x00, +0x08),
  `_wire_get` (+0x04), `_period_get` (+0x16, +0x17, +0x1a, +0x1c - fields
  attach does not write), `_params_get` (+0x1a).
* **Core-state block** (`wlc+0x28`): **U**: `wlc_txfifo` (`wlc.c`) reads
  `di[3]` to post a frame (observed during the scan; it takes `di[fifo]` of
  the FIFO it transmits on). B: `wlc_hw_set_di` (W). Nothing reads
  +0x30..+0x5f.
* **Shim**: U only (`wlc_phy_shim.c`): +0x00 by every `wlapi_bmac_*`
  wrapper (the `wlc_hw` passed on to the MAC function), `wlapi_enable_mac`,
  `wlapi_suspend_mac_and_wait` and `wlc_phy_shim_detach`; +0x08 by
  `wlapi_high_update_phy_mode`, `wlapi_high_update_txppr_offset`; +0x10 by
  `wlapi_del_timer`, `wlapi_free_timer`.
* **LED state**: B (`wlc_bmac_led`, `_led_detach`) and O (`wlc_bmac_hw_up`,
  the active-high bytes and +0x308/+0x30c) only.

### Upper-layer fields

Read by the upper layer, so an open attach must write them exactly as
listed in Data: `wlc+0x18` (`wlc_init`, `wlc_up`, `sub_03a14c`,
`sub_0411e9`, `wlc_ol_attach`, `wlc_ol_up`), `wlc+0x20` (about 40 functions
of `wlc.c`, `wlc_channel.c`, `wlc_stf.c`, `wlc_intr.c`, `wlc_led.c`,
`wlc_cca.c`, `wlc_antsel.c`), `wlc+0x28` (`wlc_txfifo`), `wlc+0x38`
(the pointer; its first word, the core index, is read only by
`wlc_bmac_up_prep`), `wlc+0x40` (about 50 functions), `wlc+0x60`
(`wlc_up`), `wlc+0x22a` (`wlc_attach`, `sub_06b67a`, `wlc_prot_g_attach`,
`wlc_prot_n_attach`; +0x228 is not read), `wlc+0x6bc` and `wlc+0x738`
(`wlc_amsdu_attach`), the `pub` fields (+0x14 about 25 functions; +0x44
about 15; +0x94 `wlc_attach`, `wlc_antsel_attach`, `wlc_channel_mgr_attach`;
+0x98/+0x9c `wlc_vht_init_defaults`, `wlc_antsel_attach`, `wlc_stf_*`;
+0x100 about 20; +0x92 is not read), and the upper layer's band states
(+0x00, +0x04, +0x08 (also as one u32 with +0x0a), +0x0c, +0x10 by many;
+0x18 is not read). `wlc+0x72c` (read by `wlc_prep_pdu`, bmac-data D8) and
`wlc+0x818`, `wlc+0x730/+0x734/+0x73c/+0x740` were not seen read.

### Consequence for an open attach

* The upper layer reads `wlc_hw` directly at +0x94, +0x98, +0x9c, +0xd0,
  +0x186 (interrupt mask handling of `wlc_intr.c`), +0xb8 (`wlc_attach`) and
  +0x1bc (`wlc_up`), and the core-state block's `di[]`; the PHY shim (still
  Broadcom's) reads the shim and passes its `wlc_hw` to the `wlc_bmac_*`
  functions. These must be in Broadcom's layout as long as those readers
  are Broadcom's.
* The hybrid adapters (`hybrid\mac*.c`) take the open MAC state from
  `wlc_hw` at every call and write it back; and the B functions read many
  fields directly. Until all of these are open, **an open attach has to
  fill Broadcom's `wlc_hw`, band states, `btc`, core-state block and shim
  completely** (every field of the tables in Data, with Broadcom's offsets).
* A private open state becomes possible for the fields with no U and no B
  reader (for instance `SRL`/`LRL`/`SFBL`/`LFBL`, `chanspec`, the MAC control
  state, `hw_etheraddr`, `intrcvlazy`, `pcie2_war_freq`, the host-flag copies
  and CW of the band states) once the adapters take them from the open state
  instead.

## Calls of the attach and detach in the hybrid build

`take`: open in the hybrid build; `use`: Broadcom's function, called by open
code as a service; Broadcom: neither (`hybrid\takeover.txt` of 2026-11-02).

| call (step) | status |
|---|---|
| `wlc_hw_attach` (1), `wlc_calloc` | Broadcom |
| `getvar`, `getintvar`, `bcm_strtoul` (2, 7, 8, 14, 15) | take (utils) |
| `wlc_chipmatch`, `wlc_is_singleband_5g` (2, 3, 13, detach) | Broadcom (wlc.c) |
| `si_setcore`, `si_corerev`, `si_coreidx` (4, 13.1) | take (si) |
| `wlc_tunables_override` (4) | Broadcom (wlc_alloc.c) |
| `si_clkctl_init`, `si_pcie_ltr_war` (6) | use |
| `si_pcieltrenable`, `si_pcieobffenable` (6) | Broadcom |
| `sub_064887` (6) | Broadcom's static copy (the open A13 exists as `bcm4360_bmac_clkctl_clk`, but the object's attach calls its own) |
| `wlc_bmac_corereset`, `wlc_bmac_validate_chip_access` (6) | take (mac) |
| `wlc_bmac_pllreq`, `si_pci_war16165` (10) | take |
| `wlc_phy_shim_attach` (11) | Broadcom (wlc_phy_shim.c) |
| `wlc_phy_shared_attach`, `wlc_phy_attach`, `wlc_phy_machwcap_set`, `wlc_phy_get_phyversion`, `wlc_phy_get_encore`, `wlc_phy_get_coreflags` (12, 13.4) | take (phy; open only with `open_phy`) |
| `wlc_setxband` (13.1) | take (mac) |
| `wlc_bmac_ampdu_set`, `wlc_bmac_set_btswitch` (13.3, 13.4) | Broadcom |
| `dma_addrwidth`, `dma_attach` (13.6) | take (dma) |
| `wl_alloc_dma_resources`, `osl_*` | the open glue |
| `sub_069659`, `wlc_hw_set_di` (13.6) | Broadcom |
| `si_chip_hostif` (13.7) | take (si) |
| `si_pcie_get_request_size` (14.1, not on this card) | Broadcom |
| `wlc_bmac_btc_wire_set` (14.2) | Broadcom |
| `wlc_bmac_btc_mode_set` (14.4) | use (Broadcom's body; it calls the open `wlc_bmac_mhf`) |
| `wlc_coredisable`, `wlc_bmac_xtal` (14.5, 14.9) | take (mac) |
| `si_pmu_rfldo` (14.6) | take (pmu) |
| `si_pci_down` (14.7) | use |
| `si_register_intr_callback` with `sub_061268`, `sub_061285` (14.8) | Broadcom (callbacks Broadcom's, R9) |
| `bcm_ether_atoe` (15) | Broadcom (bcmutils.c) |
| `wlc_bmac_led_attach` (15) | Broadcom (R4) |
| `wlc_template_cfg_init` (15) | Broadcom |
| detach: `si_deregister_intr_callback`, `si_pci_sleep` | Broadcom |
| detach: `wlc_phy_detach`, `wlc_phy_shared_detach` | take (phy) |
| detach: `wlc_phy_shim_detach`, `wlc_bmac_led_detach`, `wlc_hw_detach`, `wlc_hw_set_di` | Broadcom |
| detach: `dma_detach` (slot 0x000) | the engine's table (open with `open_dma`) |

## Corrections to bmac-init.md D1, D2 and D4

Checked against the code and the observations above; this specification
supersedes those sections.

**D1** (`wlc_hw_attach`/`wlc_hw_detach`): correct in substance. Additions:
`*err` is set to 0 on success; `wlc_hw+0xf8` is set only when the band-state
allocation succeeded; `wlc_hw_detach` is also called (and returns at once)
when the very first allocation failed; it clears `wlc+0x28` before freeing;
`wlc_calloc` is `osl_malloc` + zeroing (no message).

**D2** (`wlc_bmac_attach`):

1. Arguments: the fifth is `piomode` (stored in the byte `+0x1c`), not a
   "band unit"; the "flag (read the ids from the SROM when set)" is the bus
   type (eighth argument; not 0 = not the SI bus), and the ninth
   (`btparam`) is unused. `+0xc8` is the size of `vars` (`pub+0x110`).
2. Step 1: `+0x1c4` = 0 is also written; `wlc+0x20` is set before the
   defaults.
3. Step 2: `vendid` is taken as is; `devid` only if it is not 0xffff;
   `wlc_chipmatch` is called only for a bus type other than 0.
4. Step 5: exact conditions for chip 0x4306 (chip rev > 2, device 0x4324 or
   0x4321, package != 1) and 0x4311 (rev 0); the revision list of D2 is
   right (4, 5, 7..34, 40..47).
5. Step 6: missing the chip-0x4350 `si_pcieltrenable(sih, 1, 1)`; `si_clkctl_init`
   takes `sih`; "rev < 5" is right (<= 4).
6. Step 8 (board revision): the vague "certain chip ids and `boardrev`
   ranges" is **board vendor 0x14e4 with board type 0x417/0x418 (`boardrev`
   <= 0x3f), 0x40c (always) or 0x421 (`boardrev` <= 0x50)**, plus the format
   check (0 invalid; above 0xff the four hex digits must be 1-2, 1-9, 0-9,
   0-9), which applies to every board; the check comes **before** `sromrev`
   and the other variables are read.
7. Step 10: `wlc+0x60` is set to 1 only when the work-around applies; it is
   not "stored" otherwise.
8. Step 11: the device-id list and the chip ids forced to one band are in
   Data; besides `pub` the attach writes `wlc+0x228`/`wlc+0x22a`. Note that
   0x43a1, 0x43b2 and 0x4360 get one band.
9. Step 12: also `wlc+0x6bc` (0x28) and `wlc+0x818` (0x2a); the descriptor
   layout is in Data.
10. Step 13.1: also writes the first word of the structure at `wlc+0x38`
    (`si_coreidx`); a single-band 5 GHz device starts the loop at band 1.
11. **Step 13.2: wrong.** Bit 31 of the MAC capabilities is cleared only for
    revisions 0x1d, 0x1e, 0x21, 0x22, 0x28, 0x29, 0x2b, 0x2c (and 0x1a with
    chip rev 0) - **not for 0x2a, the 4360's revision** (observed with
    `D11(0x15c)` = 0xa0000001: `machwcap` keeps bit 31). And nothing is
    copied to `pub`.
12. Step 13.3: the table for revisions < 40 is `.data+0x180 + 12*(rev-4)`;
    the one for >= 40 is `.data+0x330` (`DAT_0058ee90` of D2 is the same
    address).
13. **Step 13.4: the third argument of `wlc_phy_attach` is the band type** (2
    for 2.4 GHz, then 1 for 5 GHz), not the band unit; the fourth is
    `vars`. `wlc_phy_get_encore` is called twice (band state +0x30 and the
    upper layer's band state +0x18), `wlc_phy_get_coreflags` fills
    `band+0x18`; `wlc_bmac_set_btswitch(-1)` is called on both bands of a
    fresh attach (the type is not known yet).
14. Step 13.5: the per-type masks are in Data.
15. Step 13.6: DMA argument list completed (name "wl%d", `dma_addrwidth`,
    `wl_alloc_dma_resources` failing also gives 0x13, the globals of P54
    written only on bus type 1, `sub_069659` with the constant 1 for FIFO 0
    and the bus type for the others); the `&txavail` pointers go to
    `wlc_hw+0x120 + 8*fifo`.
16. Step 13.7: the host-flag rules are in step 13.7 above; for the AC-PHY
    D2's two rules are right.
17. Step 14: the "SROM variable" is `aa2g`; the coexistence mode is 1 for PHY
    type 6, 5 for type 10 rev 3, else 8, taken from the band current after
    the loop (5 GHz).
18. Error codes: the codes 0x3f2..0x3f5 of `wlc_hw_attach` are passed
    through; nothing is undone on an error - `wlc_attach` calls `wlc_detach`.
19. "Register-touching steps in attach are only 7, 13.2 and 13.4/13.6": also
    `si_setcore`, `si_pcie_ltr_war`, `si_pcieobffenable`, `dma_addrwidth`,
    `wlc_coredisable` and `si_pmu_rfldo` access the card (574 accesses in
    all, Overview).

**D4** (`wlc_bmac_detach`): the order is right; corrections: the result is
`wlc_bmac_led_detach`'s (0 without LED state), not void; the PHY loop takes
the current band first and then the band other than the **upper layer's**
current band (`wlc+0x40`), so on this card it calls `wlc_phy_detach` twice
with the same `pi`; the DMA detach passes only the engine; `wlc_hw+0x198` is
cleared after the LED detach; `si_deregister_intr_callback` is called on
every bus, `si_pci_sleep` only on PCI. "No register access ... except the DMA
detach": there is none at all (`dma_detach` makes none either; observed 0).

## Verification

Reading: every function of the Scope was read in the decompiler output
(`re-out\ghidra\c\`) and checked in the disassembly (`blob.py dis`):
`wlc_bmac_attach` completely (argument registers and stack arguments, the
boardrev compares - unsigned 16-bit -, the MAC-revision and PHY-version bit
masks, the `machwcap` revision list, the `wlc+0x6bc`/`+0x818` arithmetic,
the order of the DMA arguments, the host-flag rules, the `macaddr` test),
`wlc_bmac_detach` (hidden arguments of `si_deregister_intr_callback`,
`si_pci_sleep`, the DMA slot 0 call, the band loop), `wlc_hw_attach`,
`wlc_bmac_si_attach`/`_si_detach`, `wlc_phy_shim_attach`,
`wlc_hw_get_txavail`, `bcm_ether_atoe`; the data tables with `blob.py data`.

Emulator runs, all on Broadcom's object alone (`hybrid.Run()` without a
hybrid core, Windows `python`, the default synthetic card unless said;
scripts and outputs in `re-out\analysis\bmac-attach\`):

| run | what | result |
|---|---|---|
| `observe.py` -> `obs-attach.txt` | every call made by `wlc_bmac_attach`/`wlc_hw_attach` with arguments, result and accesses; every write into the new blocks and into `wlc`/`pub`/the upper band states with the writing function; dumps at the return | the call order, arguments and results of the Procedures; 574 accesses (Overview); the field values of Data |
| `observe.py --steps attach,mpc 0,up,down,detach` -> `obs-detach.txt` | the same for `wlc_bmac_detach` | detach calls as specified; `wlc_phy_detach` twice on one `pi` |
| `detach_acc.py` | `wlc_bmac_detach` after attach, up, channel 36, down | 0 accesses, result 0, `wlc+0x20` and `wlc+0x28` cleared |
| `readers.py` (+ `--all`, with a scan) -> `readers*.txt`, `fieldmap.txt` | memory hooks on every block and field the attach creates, the whole session | "Who reads what attach writes" |
| `errors.py` -> `errors.txt` | 60 attaches: the error paths and branches (below), and the ten cards of `hybrid.py srom` | every result as specified |
| `expect.py` -> `expect.txt` | a model written from Data predicts 131..137 values per card (all of `wlc_hw`'s attach fields, band states, `btc`, core-state block, shim, `wlc`, `pub`, tunables, upper band states, the pointer relations) from the inputs; compared with the object at the return of `wlc_bmac_attach` | 14 cards, 1,900 values, **0 differences** |

The 14 cards of `expect.py`: the synthetic SROM; the SROM with unprogrammed
words (`boardflags` = `boardflags2` = 0xffffffff: `pllreq`, host flag
0x0400, coexistence wire 3 and the mode refused with -3); the image in the
OTP; device ids 0x43a2, 0x4352 (5 GHz only) and 0x43a1 (one 2.4 GHz band)
through the `devid` variable; Apple board types 0x4e (`boardrev` 0x41) and
0xef (0x1201); `boardflags` 0x20; `si_chip_hostif` = 2; `D11(0x15c)` =
0xa0000001 and 0x20000000; PHY revision 0; `macaddr` "00:90:4c".

`errors.py` cases and results (how: `arg` = argument register changed at
the entry, `ret` = a callee's result replaced, `mem` = `sih` fields changed,
`model` = card model, SROM image or allocation failure):

| case | result |
|---|---|
| device 0x1234 (arg); vendor 0x106b (arg); `vendid` = 0x106b (ret) | 0x0c |
| vendor 0x106b with bus type 0 (arg) | 0 (no lookup, no `wlc_chipmatch`) |
| `devid` = 0x43a2 / 0x43a1 / 0xffff (ret) | 0, one band 5 GHz / one band 2.4 GHz / ignored |
| `si_corerev` = 6 or 35 (ret); chip 0x4311 rev 0 (mem) | 0x0d |
| `wlc_bmac_validate_chip_access` = 0 (ret) | 0x0e |
| `boardrev` 0, 0x3101, 0x0101, 0x1a01, 0x10a1, 0x110a, 0x1001 (ret) | 0x0f |
| `boardrev` 0xff (stored as 1), 0x1101, 0x2999, 0x40, 0x12345 (stored 0x2345) (ret) | 0 |
| board vendor 0x14e4 with type 0x40c; 0x417 with 0x3f; 0x421 with 0x50 (mem) | 0x0f |
| 0x14e4 type 0x417 with `boardrev` 0x40 (mem) | 0 |
| Apple 0x4e/0x41 (bf |= 2), 0x4e/0x40 (no change), 0xe4/0x1500 and 0xef/0x1201 (`boardflags2` = 0), 0xe4/0x1501 (no change) (mem) | 0, as specified |
| `D11(0x15c)` = 0xa0000001 (model) | `machwcap` = `machwcap_backup` = 0xa0000001 (bit 31 kept) |
| PHY revision 8 in the card model; revision reported 8 (ret) | 0x12; revision 7 and 0: 0 |
| shim allocation fails; `wlc_phy_shared_attach`, `wlc_phy_attach` (first or second band) NULL | 0x19, 0x10, 0x11, 0x11 |
| `dma_attach` of FIFO 2 NULL; `wl_alloc_dma_resources` FALSE | 0x13, 0x13 |
| `macaddr` all 0, all ff, "FF-FF-FF-FF-FF-FF" | 0x16 |
| `macaddr` "00:90:4c", "1:2:3:4:5:6:7" | 0 (00 90 4c 00 00 00; 01..06) |
| `macaddr` missing (ret, `errors2.py`) | 0x15 |
| LED state allocation fails | 0x17 |
| allocation 1..4 of `wlc_hw_attach` fails | 0x3f2, 0x3f3, 0x3f4, 0x3f5 |
| `hybrid.py srom` cards: synthetic, unprogrammed words, OTP image | 0 |
| the four random revision-11 SROMs | 0x0f (their `boardrev` is invalid) |
| bad CRC, blank SROM, no SROM with blank OTP | `wlc_bmac_attach` not reached (`wlc_attach` error 1 from the backplane attach) |

Note on `hybrid.py srom`: it prints the attach error in **decimal** ("attach
15" = 0x0f), so the failure of its four random cards is the board-revision
check of step 7, not 0x15 "no MAC address" (observed: their `boardrev`
words are invalid; the attach stops before `macaddr` is looked up). 0x15
itself does not occur with these images (the unprogrammed-words card keeps a
partly valid `macaddr`); it was made to happen by `getvar(.., "macaddr")`
returning NULL (`errors2.py`): result 0x15 (`wlc_attach` error 21),
`hw_etheraddr` untouched (0), the detach clean. In every failure case `wlc_attach` called `wlc_detach`
and `wlc_bmac_detach` returned 0 with no card access, also for a partly
built state (no `pi`, DMA engines of FIFOs 0..1 only, no shim, no
`wlc_hw` blocks).

Read only (not executed): bus types other than 1 and 0 for the
`sub_069659` choice; MAC revisions <= 12 (no `D11(0x15c)` read) and the
revision-dependent register offsets of 13.6; the host-flag rules for PHY
types 2 and 4 and radio 0x2050; chip 0x4331 (14.1) and 0x4350 (6.3); the
board type 0xa8xx single-band list; `wlc_hw_detach`'s own failure branches
(observed only through the allocation failures above).

Limits of the model: `D11(0x15c)` reads 0 unless forced (the real
capabilities word is unknown, see bmac-run Q1); `si_chip_hostif` returns 1;
the PCIe core revision is 1; the SROM is synthetic (no `vendid`/`devid`
variables).

## Open questions

* **Q1.** The real card's `D11(0x15c)` (MAC capabilities): it decides
  `machwcap` (kept with bit 31 on revision 42), the coexistence resolution
  of step 14 (bmac-run Q1) and bmac-init's buffer-memory sizes. To be read
  on the card.
* **Q2.** The real PCIe Gen2 core revision (`sih+0x0c`): step 6.4 calls
  `si_pcieobffenable` only for revisions <= 4 (the model has 1).
* **Q3.** Whether `si_chip_hostif` can return 2 on a BCM4360 PCIe card
  (13.7 would then set host flag 3 bit 0x10 in both bands); the model
  returns 1.
* **Q4.** Purpose of `wlc+0x818` (receive offset rounded to 2 mod 4: 0x2a)
  and `wlc_hw+0x1c4` (written 0, never read): no reader found.
* **Q5.** Purpose of the core-state block's bytes 0x30..0x5f (never written
  or read in the session) and of the template configuration words
  `wlc+0x730..0x740` (upper layer).
* **Q6.** Device ids 0x43a1, 0x43b2 and 0x4360 get one 2.4 GHz band (read in
  the code, observed for 0x43a1 through the `devid` variable). Whether cards
  with these ids exist and are meant to be single band was not checked.
