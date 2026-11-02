# Backplane, cores and the PCIe bridge (`siutils.c`, `aiutils.c`, `nicpci.c`)

Analyst `chip`, 2026-11-02. Everything not marked (unverified) was read in the
object; the functions called in the session were also compared with the
object's accesses in the emulator (Verification: 352 calls, 0 differences).

## Scope

Bus type PCI (`sih+0x04` = 1), backplane type AXI (`sih+0x00` = 1), bus core
PCIe Gen2 (core id 0x83c), chip 0x4360 rev 3 (0x4352 and 43526 = 0xaa06 take
the same paths unless said otherwise). The PMU functions (`si_pmu_*`,
`si_clkctl_*`, `si_alp_clock` ...) are in `pmu.md`, SROM/OTP in `srom-otp.md`;
they are only named here where the backplane calls them.

The first column lists every function this specification describes (the name
the tools use). "Session" = called by another layer in the standard session
(`boundary.py siutils.c aiutils.c nicpci.c`).

| Function | .text offset | Size | Name | Session |
|---|---|---|---|---|
| `si_osh` | 0x01dec7 | 10 | original | yes |
| `si_coreid` | 0x01df15 | 19 | original | yes |
| `si_coreidx` | 0x01df28 | 12 | original | yes |
| `si_corerev` | 0x020846 | 35 | original | yes |
| `ai_corerev` | 0x00116f | 108 | original | (inside) |
| `si_backplane64` | 0x01df69 | 15 | original | yes |
| `si_findcoreidx` | 0x01df78 | 49 | original | - |
| `si_corelist` | 0x020817 | 47 | original | - |
| `si_corevendor` | 0x02153e | 35 | original | - |
| `ai_corevendor` | 0x001159 | 22 | original | - |
| `si_register_intr_callback` | 0x01dedb | 44 | original | yes |
| `si_deregister_intr_callback` | 0x01df07 | 14 | original | - |
| `si_setcoreidx` | 0x01f447 | 35 | original | yes |
| `ai_setcoreidx` | 0x001bf6 | 305 | original | (inside) |
| `si_setcore` | 0x01f8ad | 88 | original | yes |
| `si_switch_core` | 0x020764 | 179 | original | (inside) |
| `si_restore_core` | 0x01f83b | 114 | original | (inside) |
| `si_corereg` | 0x01f265 | 39 | original | yes |
| `ai_corereg` | 0x001d27 | 502 | original | (inside) |
| `sub_021d1d` | 0x021d1d | 54 | assigned: `si_cc_corereg` | (inside) |
| `si_ccreg` | 0x02390f | 21 | original | - |
| `write_ccreg` | 0x0238fa | 21 | original | - |
| `si_iscoreup` | 0x01f2a7 | 35 | original | yes |
| `ai_iscoreup` | 0x0011db | 61 | original | (inside) |
| `si_core_cflags` | 0x01f30e | 35 | original | yes |
| `ai_core_cflags` | 0x001553 | 200 | original | (inside) |
| `si_core_cflags_wo` | 0x01f2ed | 33 | original | - |
| `ai_core_cflags_wo` | 0x00161b | 181 | original | - |
| `si_core_sflags` | 0x01f2ca | 35 | original | yes |
| `ai_core_sflags` | 0x00148b | 200 | original | (inside) |
| `si_core_reset` | 0x01f223 | 33 | original | yes |
| `ai_core_reset` | 0x001738 | 396 | original | (inside) |
| `si_core_disable` | 0x01f244 | 33 | original | yes |
| `ai_core_disable` | 0x0018c4 | 236 | original | (inside) |
| `si_wrapperreg` | 0x01f28c | 27 | original | (inside) |
| `ai_wrap_reg` | 0x0016d0 | 104 | original | (inside) |
| `si_gpiocontrol` | 0x023991 | 69 | original | yes |
| `si_gpioouten` | 0x023b65 | 69 | original | yes |
| `si_gpioout` | 0x023baa | 69 | original | yes |
| `si_gpioin` | 0x023bef | 20 | original | - |
| `si_gpioled` | 0x023c83 | 29 | original | yes |
| `si_gpiotimerval` | 0x023ca0 | 29 | original | - |
| `si_clkctl_xtal` | 0x01e534 | 391 | original | yes |
| `si_deviceremoved` | 0x01e50f | 37 | original | yes |
| `si_get_sromctl` | 0x01f4c5 | 68 | original | yes |
| `si_set_sromctl` | 0x020869 | 91 | original | yes |
| `si_chip_hostif` | 0x01e16a | 99 | original | yes |
| `si_fabid` | 0x024315 | 183 | original | yes |
| `si_otp_fabid` | 0x01e8a2 | 146 | original | - |
| `si_pci_war16165` | 0x01e1e1 | 34 | original | yes |
| `si_ldo_war` | 0x01e6bb | 301 | original | yes |
| `si_tcm_size` | 0x0201ba | 326 | original | yes |
| `si_pci_setup` | 0x0215bd | 305 | original | yes |
| `si_pci_up` | 0x0213f8 | 224 | original | yes |
| `si_pci_down` | 0x02137e | 122 | original | yes |
| `si_pci_sleep` | 0x01ecf6 | 28 | original | yes (detach; also the glue) |
| `si_pci_fixcfg` | 0x021e58 | 226 | original | yes |
| `si_pcie_ltr_war` | 0x01f905 | 146 | original | yes |
| `si_pcieltrenable` | 0x01ec6f | 45 | original | (inside) |
| `si_pcieobffenable` | 0x01ec42 | 45 | original | yes |
| `si_survive_perst_war` | 0x01f997 | 282 | original | yes |
| `si_pci_pmeclr` | 0x01ef4c | 18 | original | glue (resume) |
| `si_pci_pmeen` | 0x01ef70 | 18 | original | - |
| `si_pci_pmestat` | 0x01ef5e | 18 | original | - |
| `si_pci_pmestatclr` | 0x01ef3a | 18 | original | - |
| `pcicore_init` | 0x018b68 | 245 | original | (attach) |
| `pcicore_attach` | 0x019154 | 497 | original | (attach) |
| `pcicore_deinit` | 0x018b4c | 28 | original | (detach) |
| `pcicore_up` | 0x0196d0 | 228 | original | (inside) |
| `pcicore_down` | 0x019090 | 65 | original | (inside) |
| `pcicore_sleep` | 0x019015 | 123 | original | (inside) |
| `pcicore_hwup` | 0x019345 | 760 | original | (inside) |
| `pcicore_find_pci_capability` | 0x018156 | 390 | original | (inside) |
| `pcicore_pmecap_fast` | 0x0182dc | 66 | original | yes |
| `pcie_ltrenable` | 0x018060 | 123 | original | (inside) |
| `pcie_obffenable` | 0x017fe1 | 127 | original | (inside) |
| `pcie_survive_perst` | 0x017d13 | 73 | original | (inside) |
| `sub_01963d` | 0x01963d | 147 | assigned: `pcie_devctl_mrrs_set` | (inside) |
| `sub_018c5d` | 0x018c5d | 49 | assigned: `pcie_ctrl_4360c0_war` | (not reached) |
| `sub_01831e` | 0x01831e | 100 | assigned: `pcicore_pmecap` | (inside) |
| `sub_0183bb` | 0x0183bb | 85 | assigned: `pcicore_pmcr_refresh` | (inside) |
| `pcicore_pmeclr` | 0x018410 | 81 | original | (inside) |
| `pcicore_pmeen` | 0x0184b3 | 80 | original | (inside) |
| `pcicore_pmestat` | 0x018382 | 57 | original | (inside) |
| `pcicore_pmestatclr` | 0x018461 | 82 | original | (inside) |
| `si_attach` | 0x023844 | 182 | original | yes |
| `si_detach` | 0x02171d | 197 | original | yes |
| `sub_022b10` | 0x022b10 | 3115 | assigned (guide): `si_doattach` | (attach) |
| `ai_scan` | 0x001f1d | 1491 | original | (attach) |
| `sub_00133f` | 0x00133f | 66 | assigned (guide): `get_erom_ent` | (attach) |
| `sub_001381` | 0x001381 | 266 | assigned (guide): `get_asd` | (attach) |
| `si_getdevpathintvar` | 0x0218b6 | 106 | original | (attach) |
| `si_kattach` | 0x02373b | 265 | original | - (not on PCI) |


## Overview

The backplane utilities keep one structure per card, `si_info_t` (0x7d8
bytes, allocated by `si_attach`; its first 0x58 bytes are the public `si_t`).
Every other layer holds a pointer to it (`sih`) and calls the `si_*`
functions with it. On this card the `si_*` function of a pair is a dispatcher
on the backplane type `sih+0x00` (0 = SB: `sb_*`, not described; 1 or 3 =
AXI: `ai_*`; anything else: return 0 / do nothing); the `ai_*` function does
the work.

**Current core.** `si_info` has a notion of the *current core*: index
`sih+0x1c0`. On PCI the current core is what the two movable windows of BAR0
show:

| BAR0 offset | What it shows | Moved by |
|---|---|---|
| 0x0000..0x0fff | registers of the current core (window 1) | `PCICFG(0x80)` = backplane address |
| 0x1000..0x1fff | AXI wrapper of the current core (window 2) | `PCICFG(0x70)` behind a PCIe Gen2 core (`sih+0x08` = 0x83c), `PCICFG(0xac)` behind other bridges |
| 0x2000..0x2fff | registers of the bus core (PCIe), fixed | - |
| 0x3000..0x3fff | registers of ChipCommon (core index 0), fixed | - |

The fixed windows are used only when the bus core is PCIe Gen2 (0x83c), PCIe
(0x820) or PCI (0x804) with revision > 12 (call this condition
**FIXEDWIN** below; it holds on this card).

`ai_setcoreidx` moves both movable windows (two configuration writes, always
both, even when the index does not change) and updates `sih+0x1c0`.
`sih+0xb8` (virtual address of BAR0) and `sih+0x548` (virtual address of
window 2 = BAR0 + 0x1000, set once by `ai_scan`) do not change on PCI. So all
functions that act on "the current core's wrapper" (`ai_core_cflags`,
`ai_core_reset`, `ai_iscoreup`, ...) access BAR0+0x1000+offset, and the
caller must have selected the core before (`si_setcore` /
`si_setcoreidx`).

**Register access without switching.** `si_corereg(sih, idx, off, mask,
val)` reaches ChipCommon and the PCIe core through the fixed windows, without
touching the current core; any other core costs a switch there and back
(four configuration writes when the current core differs) bracketed by the
interrupt callbacks the MAC registered.

**Interrupt callbacks.** `wlc_bmac_attach` registers three functions and an
argument (`si_register_intr_callback`); `ai_corereg`, `si_switch_core` and
`si_restore_core` call "interrupts off" before and "restore" after a core
switch, but only when the *current* core (before the switch) is the core
that was current at registration (`sih+0x68`, the 802.11 core). The
callbacks are the MAC's; on this card they mask/restore the MAC's interrupt
mask (not described here).

**Services of the open MAC layer.** `open/include/bcm4360/mac_env.h`
(`open/mac/bmac_clk.c`) calls these backplane functions as services; their
exact behaviour on this card is in the sections below: `si_core_cflags`,
`si_iscoreup`, `si_core_reset`, `si_core_disable` (wrapper of the current
core, which the MAC keeps on the 802.11 core), `si_clkctl_xtal` (returns -1,
no access, on this PCIe Gen2 card), `si_gpiocontrol` and `si_corereg`
(ChipCommon and PCIe through the fixed windows, other cores with a switch),
`si_deviceremoved` (one read of `PCICFG(0x00)`), `si_chipcontrl_srom4360`
(`srom-otp.md`).

## Data

### `si_info_t` (`sih`)

Offsets from the start of the structure (`si_attach` allocates 0x7d8 bytes).
"Writer" = the functions that write it after attach; all others are written
only by `si_doattach` / `ai_scan`.

| Offset | Size | Field | Writer / readers |
|---|---|---|---|
| 0x00 | 4 | backplane type: 0 SB, 1 AXI, 3 (also AXI) | read by every dispatcher |
| 0x04 | 4 | bus type: 0 SI (system bus), 1 PCI, 2 PCMCIA/SDIO-like (only tested by `si_clkctl_xtal`) | read widely |
| 0x08 | 4 | bus core id (0x83c) | |
| 0x0c | 4 | bus core revision | |
| 0x10 | 4 | bus core index | `ai_corereg` |
| 0x14 | 4 | ChipCommon revision (43) | `si_gpioled`, `si_gpiotimerval` |
| 0x18 | 4 | ChipCommon capabilities (`CC(0x04)`); bit 27 = 64-bit backplane, bit 28 = PMU | `si_backplane64` |
| 0x1c | 4 | ChipCommon extended capabilities | |
| 0x20, 0x24 | 4, 4 | PMU revision, PMU capabilities | |
| 0x28..0x34 | 4 each | board type, board revision, board vendor, boardflags | |
| 0x3c | 4 | chip id | many |
| 0x40 | 4 | chip revision | `si_survive_perst_war` |
| 0x44 | 4 | chip package | `si_chip_hostif` |
| 0x48 | 4 | chip status (`CC(0x2c)`) | `si_chip_hostif` |
| 0x58 | 8 | `osh` | `si_osh`, every configuration access |
| 0x60 | 8 | bus device handle (the `sdh` argument of `si_attach`) | |
| 0x68 | 4 | core id that was current when the interrupt callbacks were registered | `si_register_intr_callback` (W) |
| 0x70 | 8 | argument of the interrupt callbacks | `si_register_intr_callback` (W) |
| 0x78 | 8 | callback "interrupts off" (returns a saved state, u32) | `si_register_intr_callback` (W), cleared by `si_deregister_intr_callback` |
| 0x80 | 8 | callback "interrupts restore" (argument, saved state) | `si_register_intr_callback` (W) |
| 0x88 | 8 | callback "interrupts enabled?" (not called by the functions here) | `si_register_intr_callback` (W) |
| 0x90 | 8 | `pcicore_info` handle (`nicpci.c`), 0 until `pcicore_init` | |
| 0xa8 | 8 | variable buffer (SROM variables) | `si_attach` |
| 0xb0 | 4 | its size | `si_attach` |
| 0xb8 | 8 | PCI: virtual address of BAR0; `ai_setcoreidx` rewrites it with the same value | `ai_setcoreidx` (W, same value) |
| 0xc0 | 8 x 32 | SI bus only: mapped register address per core index | not on PCI |
| 0x248 | 4 x 32 | backplane address of the registers of core *i* (slave port 0, first region) | read by `ai_setcoreidx` |
| 0x3c8 | 4 x 32 | backplane address of a second 4 KB register region of core *i* (0 if none) | `ai_addrspace` |
| 0x448 | 4 x 32 | size of the first region of core *i* | `ai_addrspacesize` |
| 0x4c8 | 4 x 32 | size of the second region of core *i* | `ai_addrspacesize` |
| 0x548 | 8 | virtual address of the current core's wrapper. PCI: BAR0 + 0x1000, fixed | read by all wrapper functions |
| 0x550 | 8 x 32 | SI bus only: mapped wrapper address per core | not on PCI |
| 0x650 | 4 x 32 | backplane address of the wrapper of core *i* | read by `ai_setcoreidx` |
| 0x6d0 | 4 x 32 | enumeration word "CIA" of core *i* (bits 20..31 vendor, 8..19 core id, 4..7 class) | `ai_corevendor` |
| 0x750 | 4 x 32 | enumeration word "CIB" of core *i* (bits 24..31 revision, 19..23 slave wrappers, 14..18 master wrappers, 9..13 slave ports, 4..8 master ports) | `ai_corerev` |
| 0x1c0 | 4 | index of the current core | `ai_setcoreidx` (W) |
| 0x1c4 | 4 | number of cores | |
| 0x1c8 | 4 x 32 | core id of core *i* | |
| 0x7d0 | 4 | backplane address found for core id 0x367 (OOB router) | `ai_scan` |

Core indices in the model (and, presumably, on the card: the enumeration ROM
of the real card is not known): 0 ChipCommon (0x800), 1 802.11 (0x812), 2 ARM
CR4 (0x83e), 3 PCIe Gen2 (0x83c), 4 USB 2.0 device (0x81a).

### AXI wrapper registers (offsets in window 2)

| Offset | Name (Broadcom style) | Use |
|---|---|---|
| 0x408 | ioctrl | core control flags ("cflags"): bit 0 clock enable, bit 1 force gated clocks (FGC), bits 2..15 core specific |
| 0x500 | iostatus | core status flags ("sflags"), core specific |
| 0x800 | resetctrl | bit 0 = core in reset |
| 0x804 | resetstatus | not 0 while backplane transactions are pending |

### ChipCommon registers used here

`CC(0x60)` gpioin, `CC(0x64)` gpioout, `CC(0x68)` gpioouten, `CC(0x6c)`
gpiocontrol, `CC(0x88)` gpiotimerval, `CC(0x8c)` gpiotimeroutmask,
`CC(0x190)` sromcontrol, `CC(0x634)` PMU watchdog (`si_survive_perst_war`).

### Polling loops

All waits are the usual `SPINWAIT(cond, T)`: test the condition (a register
read); if it still holds and the remaining budget allows, wait 10 us and test
again. Concretely: at most T/10 delays of 10 us and T/10 + 1 reads. If the
condition is false at the first read there is no delay.

## Procedures

### Accessors without hardware access

#### si_osh (.text+0x01dec7, name original)
Returns `sih+0x58`.

#### si_coreidx (.text+0x01df28, name original)
Returns the current core index `sih+0x1c0` (u32).

#### si_coreid (.text+0x01df15, name original)
Returns `sih+0x1c8 + 4 * sih+0x1c0` (core id of the current core).

#### si_corerev (.text+0x020846) and ai_corerev (.text+0x00116f), names original
Dispatcher (backplane type 1/3 -> `ai_corerev`, 0 -> `sb_corerev`, else 0).
`ai_corerev`: revision = byte 3 of CIB of the current core, i.e.
`(sih+0x750 + 4 * curidx) >> 24`. Chip 0x5300 only (not this card) maps the
revision through small tables for cores 0x800/0x82d/0x80e. No access.

#### si_corevendor (.text+0x02153e) and ai_corevendor (.text+0x001159), names original
`CIA(curidx) >> 20`. No access.

#### si_backplane64 (.text+0x01df69, name original)
Returns bit 27 of `sih+0x18` (ChipCommon capabilities: 64-bit backplane).
Called by `dma_addrwidth`. In the model the bit is 0.

#### si_findcoreidx (.text+0x01df78, name original)
`si_findcoreidx(sih, coreid, unit)`: index of the `unit`-th core (counting
from 0) whose id `sih+0x1c8+4i` equals `coreid`, scanning `i` = 0 ..
`sih+0x1c4`-1; 0x21 (33, "bad index") if there is none.

#### si_corelist (.text+0x020817, name original)
Copies `4 * numcores` bytes of the core id array (`sih+0x1c8`) to the
caller's buffer (`osl_memcpy`) and returns `numcores`.

#### si_register_intr_callback (.text+0x01dedb, name original)
`si_register_intr_callback(sih, intrsoff_fn, intrsrestore_fn,
intrsenabled_fn, intr_arg)`: stores `sih+0x70` = intr_arg, `+0x78` =
intrsoff_fn, `+0x80` = intrsrestore_fn, `+0x88` = intrsenabled_fn, and
`sih+0x68` = core id of the current core (`sih+0x1c8 + 4*curidx`). No result.
`wlc_bmac_attach` calls it while the current core is the 802.11 core
(0x812), so the callbacks are used only for switches *away from* the 802.11
core.

#### si_deregister_intr_callback (.text+0x01df07, name original)
Sets `sih+0x78` = 0 (the other fields stay).

### Selecting the current core

#### si_setcoreidx (.text+0x01f447) and ai_setcoreidx (.text+0x001bf6), names original
`si_setcoreidx(sih, idx)` dispatches (1/3 -> `ai_setcoreidx`, 0 ->
`sb_setcoreidx`, else return 0). Result: pointer (virtual address of the
selected core's registers) or 0.

`ai_setcoreidx(sih, idx)`:
1. If `idx >= min(sih+0x1c4, 32)`: return 0, nothing changes, no access.
2. Bus type PCI (`sih+0x04` = 1):
   1. `PCICFG(0x80)` = `sih+0x248 + 4*idx` (32-bit configuration write):
      window 1 shows the core's registers.
   2. `PCICFG(0x70)` = `sih+0x650 + 4*idx` if the bus core id `sih+0x08`
      is 0x83c, else `PCICFG(0xac)` = the same: window 2 shows the core's
      wrapper.
   3. `sih+0xb8` stays as it is (written back with its own value),
      `sih+0x1c0` = idx; return `sih+0xb8` (BAR0).
3. Bus type SI (0, not this card): maps the core's registers and wrapper on
   first use (`osl_reg_map(base, 0x1000)` into `sih+0xc0+8*idx` and
   `sih+0x550+8*idx`), sets `sih+0xb8` and `sih+0x548` to them.
4. Other bus types: `sih+0xb8` = 0, `sih+0x1c0` = idx, return 0.

Both configuration writes happen on every call, also when `idx` equals the
current index. No delay, no read.

#### si_setcore (.text+0x01f8ad, name original)
`si_setcore(sih, coreid, unit)`: finds the index as `si_findcoreidx` does
(the `unit`-th core with that id); if there is none or the index is > 31,
returns 0 without access. Else dispatches like `si_setcoreidx` (returns the
result of `ai_setcoreidx(sih, idx)`).

#### si_switch_core (.text+0x020764, name original)
`si_switch_core(sih, coreid, *origidx, *intr_val)`; returns a register
pointer.
1. If PCI and FIXEDWIN: `*origidx` = coreid; if coreid is 0x800 return
   `sih+0xb8 + 0x3000`; if coreid equals the bus core id `sih+0x08` return
   `sih+0xb8 + 0x2000` (no access, no callback).
2. Otherwise: if the callback `sih+0x78` is set and the current core's id
   equals `sih+0x68`, `*intr_val` = intrsoff(`sih+0x70`). `*origidx` =
   `sih+0x1c0`; return `si_setcore(sih, coreid, 0)`.

Note: in case 2, `*intr_val` is left unchanged when the callback is not
called; callers initialise it.

#### si_restore_core (.text+0x01f83b, name original)
`si_restore_core(sih, origidx, intr_val)`: if PCI, FIXEDWIN and `origidx` is
0x800 or the bus core id (i.e. `si_switch_core` took a fixed window), do
nothing. Else `si_setcoreidx(sih, origidx)`, then, if `sih+0x80` is set and
the (now) current core's id equals `sih+0x68`, call
intrsrestore(`sih+0x70`, intr_val).

### Register access of any core

#### si_corereg (.text+0x01f265) and ai_corereg (.text+0x001d27), names original
`si_corereg(sih, idx, regoff, mask, val)` dispatches (1/3 -> `ai_corereg`,
0 -> `sb_corereg`, else return 0). Result: the register's value (u32) after
the optional write.

`ai_corereg(sih, idx, regoff, mask, val)`:
1. `idx > 31`: return 0, no access.
2. Choose the address (PCI):
   * core `idx` is ChipCommon (`sih+0x1c8+4*idx` = 0x800) and FIXEDWIN:
     address = BAR0 + 0x3000 + regoff ("fast").
   * else `idx` = bus core index `sih+0x10`: with FIXEDWIN address = BAR0 +
     0x2000 + regoff ("fast"); (old PCI cores without FIXEDWIN: BAR0 +
     0x1800 + regoff for regoff < 0xf00, else BAR0 + 0x1000 + regoff - not
     this card).
   * else ("slow"):
     1. If `sih+0x78` is set and the id of the *current* core equals
        `sih+0x68`: `s` = intrsoff(`sih+0x70`); else `s` = 0.
     2. `orig` = `sih+0x1c0`.
     3. `ai_setcoreidx(sih, idx)` (two configuration writes, see above);
        address = BAR0 + regoff.
3. If `mask` != 0 or `val` != 0: read the 32-bit register, write
   `(old & ~mask) | val`. Note: `val` is *not* masked with `mask`.
4. Read the register again (32 bit): this is the result. The read happens
   always, also after a write.
5. Slow path only: if `orig` != `idx`, `ai_setcoreidx(sih, orig)`; then if
   `sih+0x80` is set and the id of the (now) current core equals `sih+0x68`,
   intrsrestore(`sih+0x70`, `s`).

Fields read: 0x00 (dispatcher), 0x04, 0x08, 0x0c, 0x10, 0x1c8.., 0x1c0,
0x248.., 0x650.., 0x68, 0x70, 0x78, 0x80, 0xb8. Written: `sih+0x1c0` (slow
path only; restored to `orig` at the end).

Bus accesses, fast path: [R, W] (if mask|val), R, all 32-bit MMIO at the
fixed window. Slow path, current core different from idx: W PCICFG(0x80), W
PCICFG(0x70), [R, W], R, W PCICFG(0x80), W PCICFG(0x70) (plus whatever the
callbacks do). Slow path with idx = current core: the first two
configuration writes only (no restore).

#### sub_021d1d (.text+0x021d1d, name assigned: `si_cc_corereg`)
`sub_021d1d(sih, regoff, mask, val)` = the `si_corereg` dispatch on core
index 0 (ChipCommon): `ai_corereg(sih, 0, regoff, mask, val)` for
backplane type 1/3 (on this card: always the fixed window at BAR0+0x3000),
`sb_corereg` for 0, else 0. Used by the GPIO functions and `si_fabid` of
other chips.

#### si_ccreg (.text+0x02390f) and write_ccreg (.text+0x0238fa), names original
`(sih, regoff, mask, val)`: if regoff < 0xe09, `sub_021d1d(sih, regoff,
mask, val)` and return its result, else return 0. Not called in the session.

### AXI wrapper of the current core

All of these act on `w` = `sih+0x548` (PCI: BAR0+0x1000 = window 2, i.e. the
wrapper of the core selected by the last `ai_setcoreidx`). All accesses are
32-bit MMIO. The `si_*` function of each pair is the dispatcher on
`sih+0x00` (1/3 -> `ai_*`, 0 -> `sb_*`, else return 0 / nothing).

#### si_iscoreup (.text+0x01f2a7) and ai_iscoreup (.text+0x0011db), names original
1. `v` = R `w+0x408` (ioctrl).
2. If `(v & 3)` != 1 (clock enable without force-gated-clocks): return 0
   (no second read).
3. Else `r` = R `w+0x800` (resetctrl); return 1 if bit 0 of `r` is 0, else
   0.

Result is a bool (0/1 in eax). Reads `sih+0x548` only.

#### si_core_cflags (.text+0x01f30e) and ai_core_cflags (.text+0x001553), names original
`ai_core_cflags(sih, mask, val)`:
1. Chip guards (none applies to 0x4360: chip 0xb83a rev 0 with current core
   0x82c, chips 0x4749/0x5357 package 11 with current core 0x819, chips
   0xcf1a/0xcf12 with current core 0x50b: return 0 without access).
2. If `mask` != 0 or `val` != 0: R `w+0x408`, W `w+0x408` = `(old & ~mask)
   | val` (val not masked).
3. Return R `w+0x408` (always read).

Reads `sih+0x3c`, (`+0x40`, `+0x44`, `+0x1c0`, `+0x1c8..` only for the
guards of other chips), `+0x548`.

#### si_core_cflags_wo (.text+0x01f2ed) and ai_core_cflags_wo (.text+0x00161b), names original
As `ai_core_cflags` without the final read and without result: nothing at
all when `mask` = `val` = 0, else R, W of `w+0x408`.

#### si_core_sflags (.text+0x01f2ca) and ai_core_sflags (.text+0x00148b), names original
As `ai_core_cflags` on `w+0x500` (iostatus): same guards, R/W only when
mask|val != 0, then always R `w+0x500` = result.

#### si_core_reset (.text+0x01f223) and ai_core_reset (.text+0x001738), names original
`ai_core_reset(sih, bits, resetbits)` (no result):
1. `SPINWAIT(R w+0x804 != 0, 5000)`.
2. W `w+0x408` = `bits | resetbits | 3` (core flags with clock enable and
   force gated clocks).
3. R `w+0x408` (value discarded).
4. `SPINWAIT(R w+0x804 != 0, 5000)`.
5. W `w+0x800` = 1 (put the core into reset), delay 10 us.
6. Up to 10 times: `r` = R `w+0x800`; stop if `r` = 0; stop also at the
   10th read (the body below runs at most 9 times). Body:
   `SPINWAIT(R w+0x804 != 0, 5000)`; W `w+0x800` = 0;
   `SPINWAIT(R w+0x804 != 0, 5000)`.
7. W `w+0x408` = `bits | 1` (clock on, force gated clocks off).
8. R `w+0x408` (discarded); delay 1 us.

When resetstatus reads 0 and the reset bit clears at once (the model, and
presumably the card), the accesses are: R 804, W 408, R 408, R 804, W 800=1,
delay 10, R 800 (=1), R 804, W 800=0, R 804, R 800 (=0), W 408, R 408, delay 1.

Note: the object puts the core into reset *after* setting the flags (unlike
newer Broadcom code); there is no separate "disable" step inside the reset.
The MAC calls `si_core_disable` before when it wants that (`bmac-init.md`
A14).

#### si_core_disable (.text+0x01f244) and ai_core_disable (.text+0x0018c4), names original
`ai_core_disable(sih, bits)` (no result):
1. `r` = R `w+0x800`; if bit 0 is set (already in reset): return.
2. `SPINWAIT(R w+0x804 != 0, 300)`; if it timed out (the last read was not
   0), `SPINWAIT(R w+0x804 != 0, 10000)`, starting with an immediate read.
3. W `w+0x800` = 1; R `w+0x800` (discarded); delay 1 us.
4. W `w+0x408` = `bits`; R `w+0x408` (discarded); delay 10 us.

Normal case (resetstatus 0, core out of reset): R 800, R 804, W 800=1, R 800,
delay 1, W 408=bits, R 408, delay 10.

#### si_wrapperreg (.text+0x01f28c) and ai_wrap_reg (.text+0x0016d0), names original
`si_wrapperreg(sih, offset, mask, val)`: backplane type 1/3 ->
`ai_wrap_reg`, else 0 (no SB variant). `ai_wrap_reg`: if mask|val != 0, R
then W `w + (offset & ~3)` = `(old & ~mask) | val`; then R `w + (offset &
~3)` = result.

### GPIO (ChipCommon)

#### si_gpiocontrol, si_gpioouten, si_gpioout (.text+0x023991, 0x023b65, 0x023baa, names original)
`si_gpioX(sih, mask, val, priority)` (priority is a byte):
* Only on bus type SI (0) and priority != 2 ("high"), mask and val are first
  limited by the global reservation mask (`.bss+0x334`, written by
  `si_gpioreserve` / `si_gpiorelease`): priority 0 ("driver") clears the
  reserved bits from mask, any other priority keeps only reserved bits;
  then val &= mask (mask = val = 0 stays 0).
* On PCI (this card) mask and val pass unchanged.
* Then `sub_021d1d(sih, R, mask, val)` with R = 0x6c (gpiocontrol), 0x68
  (gpioouten), 0x64 (gpioout): i.e. on this card [R, W `CC(R)` =
  `(old & ~mask) | val`] if mask|val != 0, then R `CC(R)`, all at BAR0+0x3000.
  The result of `ai_corereg` (the register after the write) is returned
  (the decompiler shows void; the callers use the value - unverified which
  ones).

Fields read: `sih+0x04`, and through `ai_corereg` 0x00, 0x08, 0x0c, 0x1c8,
0xb8.

#### si_gpioin (.text+0x023bef, name original)
`sub_021d1d(sih, 0x60, 0, 0)`: one read of `CC(0x60)`, returned.

#### si_gpioled, si_gpiotimerval (.text+0x023c83, 0x023ca0, names original)
`(sih, mask, val)`: if the ChipCommon revision `sih+0x14` >= 16:
`sub_021d1d(sih, 0x8c (gpioled = gpiotimeroutmask) / 0x88 (gpiotimerval),
mask, val)` and return its result; else return 0xffffffff without access.

### Crystal, device presence, SROM control, host interface

#### si_clkctl_xtal (.text+0x01e534, name original)
`si_clkctl_xtal(sih, what, on)`: crystal/PLL power through the PCI GPIO
configuration registers of old PCI bridges.
* PCI with bus core 0x83c or 0x820 (this card): return 0xffffffff (-1)
  at once, **no access**.
* Bus type other than PCI and not 2: return 0xffffffff; bus type 2: return 0.
* PCI with another bridge (not this card): reads `PCICFG(0xb0)` (in),
  `0xb4` (out), `0xb8` (outen); bit 0 of `what` = crystal (GPIO bit 6),
  bit 1 = PLL (GPIO bit 7). Off (`on` = 0): outen |= the selected bits, out:
  crystal bit cleared, PLL bit set; writes 0xb4 then 0xb8. On: unless the
  crystal is already on (in & 0x40), for the crystal: out |= 0x40 (and 0x80
  if PLL), write 0xb4, 0xb8, delay 1000 us; for the PLL: write 0xb4 = out &
  ~0x80, delay 2000 us. Return 0.

Fields read: `sih+0x04`, `sih+0x08`.

#### si_deviceremoved (.text+0x01e50f, name original)
PCI: one 32-bit read of `PCICFG(0x00)`; returns 1 if its low 16 bits
(vendor id) are not 0x14e4, else 0. Other buses: 0 without access. Reads
`sih+0x04`, `sih+0x58`.

#### si_get_sromctl (.text+0x01f4c5, name original)
1. `orig` = `sih+0x1c0`.
2. `p` = `si_setcoreidx(sih, 0)` (W PCICFG(0x80), W PCICFG(0x70)).
3. `v` = R `p + 0x190` (32 bit, through window 1: `CC(0x190)` sromcontrol).
4. `si_setcoreidx(sih, orig)` (two configuration writes).
5. Return `v`.

#### si_set_sromctl (.text+0x020869, name original)
`si_set_sromctl(sih, value)`:
1. `orig` = `sih+0x1c0`; `p` = `si_setcoreidx(sih, 0)` (two configuration
   writes).
2. If `si_corerev(sih)` (ChipCommon's revision from CIB, no access) < 32:
   return -23 (0xffffffe9) **without restoring the core** (not this card).
3. W `p + 0x190` = value (32 bit, window 1); `si_setcoreidx(sih, orig)`;
   return 0.

#### si_chip_hostif (.text+0x01e16a, name original)
No access. Chip 0x4360, 0xa9c4 or 0xaa06: return 1 (PCIe host interface) if
bit 0 of the package `sih+0x44` is 0 or bit 8 of the chip status
`sih+0x48` is 0, else 2. Chip 0x4335: from chip status bits 2/0/3 (4 if bit
0, 2 if bit 2, else bit 3). Other chips: 0. On this card (package 0): 1.

#### si_fabid (.text+0x024315, name original) and si_otp_fabid (.text+0x01e8a2, name original)
`si_fabid(sih)` returns a u16 fab id. Chip 0x4360: **0 without access**
(`acphy-attach.md` agrees). For other chips: 0x4330 from `CC(0x28)`,
0x4324/0x4334/0x4335/0x4350/0xa8ea/0xa8eb from `CC(0xf8)` & 0xf, 0x4329,
0x5356, 0xa962 from `si_otp_fabid` (OTP word 8: (word & 0x3c or 0x7c) >> 2;
0x4329 rev < 3: word 0, mask 0).

#### si_pci_war16165 (.text+0x01e1e1, name original)
Returns 1 for PCI with bus core 0x804 revision < 11, else 0. No access. 0 on
this card.

#### si_ldo_war (.text+0x01e6bb, name original)
`si_ldo_war(sih, devid)`: reads `PCICFG(0x08)` (32 bit: revision id and
class). Only for devid 0x10f6, 0x4322, 0x432b, 0x432c, 0x432d with PCI
revision byte 0 (not this card) it moves window 1 to 0x18000000
(ChipCommon), writes `CC(0x658)` = 0, `CC(0x65c)` = 0x3001, waits 5000 us,
writes `CC(0x618)` = 0xd, polls `PCICFG(0xa8)` bit 16 up to 20000 us
(returns 0 on timeout) and restores `PCICFG(0x80)`. Returns 1 (byte). On
this card: one configuration read, result 1.

#### si_tcm_size (.text+0x0201ba, name original)
Size in bytes of the ARM CR4's tightly coupled memory (called by
`wlc_ol_attach`). Result u32.
1. `s` = intrsoff(`sih+0x70`) if `sih+0x78` is set and the current core's id
   equals `sih+0x68` (on this card the current core is the 802.11 core: the
   callback runs), else 0.
2. `orig` = `sih+0x1c0`; `p` = `si_setcore(sih, 0x83e, 0)` (ARM CR4; two
   configuration writes). If `p` = 0 (no such core) skip to step 7 with size
   0.
3. `up` = `si_iscoreup(sih)`; if not up: `si_core_reset(sih, 0x20, 0x20)`
   (ioctrl 0x23, then 0x21; the reset sequence above).
4. `cap` = R `p+0x04` (window 1: ARM CR4 register 4). Number of banks =
   bits 4..7 + bits 0..3 of `cap`.
5. For each bank `i` from 0: W `p+0x40` = i; `info` = R `p+0x44`; size +=
   `((info & 0x3f) + 1) * 0x2000`.
6. If it was not up: `si_core_disable(sih, 0)`. `si_setcoreidx(sih, orig)`.
7. If `sih+0x80` is set and the current core's id equals `sih+0x68`:
   intrsrestore(`sih+0x70`, `s`). Return the size.

In the model the ARM core is down and register 4 reads 0: size 0, with a
reset and a disable of the ARM core (32 accesses).

### PCIe bridge (`nicpci.c`, reached through `siutils.c`)

`pcicore_info` (0x138 bytes, `osl_malloc` in `pcicore_init`, pointer in
`sih+0x90`):

| Offset | Size | Field |
|---|---|---|
| 0x00 | 8 | virtual address of the PCIe core's registers (the `regs` argument of `pcicore_init`: BAR0 + 0x2000) |
| 0x08 | 8 | `sih` |
| 0x10 | 8 | `osh` |
| 0x18 | 1 | configuration offset of the PCIe Link Control register (PCIe capability + 0x10) |
| 0x19 | 1 | configuration offset of the Device Control register (capability + 8) |
| 0x1a | 1 | (cleared by `pcicore_sleep` on PCIe gen1 cores) |
| 0x1b | 1 | gen1 cores only (ASPM/clkreq state from `pcie_readreg(0x204)`) |
| 0x1c | 1 | gen1 cores only (a work-around policy from the board variables) |
| 0x1d | 1 | configuration offset of the power management capability, 0 until first needed |
| 0x1e | 1 | 1 if that capability announces PME support (PMC bits 27..31 not 0) |
| 0x1f | 1 | 1 for bus core 0x820 only |
| 0x20 | 2 | copy of the 16-bit PCIe core register 0x868 (read when the PM capability is first looked up; purpose unknown) |
| 0x22 | 2 | Max Read Request Size code for Device Control bits 12..14: 3 (1024 bytes) for chips 0x4360, 0x4352, 0x4350, 0xa9c4 (`pcicore_attach`) |
| 0x26 | 1 | configuration offset of Device Control 2 (capability + 0x28) |
| 0x28, 0x2c, 0x30 | 4 each | capability + 0x798, + 0x79c, + 0x7a0 (used by the LTR register helpers, not reached) |
| 0x34 | 0x100 | gen1 cores only (cleared by `pcicore_attach`) |

In the model's configuration space the PCI Express capability is at 0xac, so
0x18 = 0xbc, 0x19 = 0xb4, 0x26 = 0xd4; the PM capability is at 0x48. The
real card's capability list is not known (assumption of the model).

#### pcicore_find_pci_capability (.text+0x018156, name original)
`(osh, capid, buf, *buflen)`: returns the configuration offset (byte) of the
capability `capid`, 0 if there is none. All configuration reads are 32 bit.
1. R `PCICFG(0x0c)`; if bits 16..22 (header type) are not 0: return 0.
2. R `PCICFG(0x04)`; if bit 20 (capabilities list) is 0: return 0.
3. `ptr` = low byte of R `PCICFG(0x34)`; 0: return 0.
4. Loop: the id is byte `ptr & 3` of R `PCICFG(ptr & ~3)`; equal to `capid`:
   done. Else the next pointer is byte `(ptr+1) & 3` of R
   `PCICFG((ptr+1) & ~3)`; 0: return 0; continue with it.
5. If `buf` and `buflen` are given and `*buflen` != 0: copy `*buflen` bytes
   (limited to the end of the 256-byte space, `*buflen` updated) from
   `ptr + 2` on, one 32-bit configuration read per byte. Not used on this
   card.

#### pcicore_init (.text+0x018b68), pcicore_deinit (.text+0x018b4c), names original
`pcicore_init(sih, osh, regs)`: allocates 0x138 bytes, zeroes them, sets
0x00 = regs, 0x08 = sih, 0x10 = osh. Bus core 0x83c or 0x820: finds the PCI
Express capability (`pcicore_find_pci_capability(osh, 0x10, 0, 0)`) and sets
0x19 = cap + 8, 0x18 = cap + 0x10, 0x26 = cap + 0x28, 0x28 = cap + 0x798,
0x30 = cap + 0x7a0, 0x2c = cap + 0x79c (0x1f = 1 for 0x820 only). Returns
the pointer (0 if the allocation failed). `pcicore_deinit(pi)` frees the 0x138
bytes (`osl_mfree(pi+0x10, pi, 0x138)`).

#### pcicore_attach (.text+0x019154, name original)
`(pi, vars, state)`. On this card: Apple board types 0x129/0x135 only (not
0x117) program a SerDes register first; bus core is not 0x820, so: chips
0xa9c4, 0x4360, 0x4350, 0x4352 set `pi+0x22` = 3; chip 0x4360 with chip
revision >= 4 also does `sub_018c5d` (below); revision 3: nothing more. No
access on this card. (Bus core 0x820: ASPM/clkreq/SerDes work-arounds, not
described.)

#### pcicore_up (.text+0x0196d0, name original) and sub_01963d (.text+0x01963d, name assigned: `pcie_devctl_mrrs_set`)
`pcicore_up(pi, state)`: with `pi` = 0 nothing. Bus core 0x83c:
`sub_01963d(pi, pi+0x22)`, then for chip 0x4360 with chip revision > 3
`sub_018c5d(pi)`. (Other cores: gen1 work-arounds, not described.)

`sub_01963d(pi, code)`: if `pi+0x19` is 0, nothing. Else `v` = R
`PCICFG(pi+0x19)` (Device Control); (bus core 0x820 rev < 0x12: code = 0);
W `PCICFG(pi+0x19)` = `(v & 0xffff8fff) | ((code & 7) << 12)`; R
`PCICFG(pi+0x19)` again. On this card: Max Read Request Size = 1024 bytes.

#### sub_018c5d (.text+0x018c5d, name assigned: `pcie_ctrl_4360c0_war`)
`mod(PCIE(0x000), 0x300, 0x200)` through `pi+0x00` (R, W, R; 32 bit).
Reached only for chip 0x4360 revision >= 4 (not the modelled revision 3).

#### pcicore_down, pcicore_sleep, pcicore_hwup (.text+0x019090, 0x019015, 0x019345, names original)
Nothing on this card: `pcicore_down` and `pcicore_sleep` act only for bus
core 0x820; `pcicore_hwup` acts for board type 0x135/0x129 (Apple; SerDes
register 0x830/0x10) and bus core 0x820. No access with board type 0x117.

#### si_pci_setup (.text+0x0215bd, name original)
`si_pci_setup(sih, coremask)`, PCI only. Bus core 0x83c/0x820 (or 0x804 rev >
5): R `PCICFG(0x94)`, W `PCICFG(0x94)` = old | (coremask << 8) (the
backplane interrupt mask of the bridge; `wlc_bmac_up_prep` passes 2 = core 1,
the 802.11 core). (Old PCI core 0x804: `si_setint` and PCI core register
work-arounds, not this card.)

#### si_pci_up (.text+0x0213f8, name original)
PCI only. Chips 0x4311/0x4321/0x4716/0x4748 first turn the crystal on
(`sub_02105c`, `pmu.md`), not this card. Bus core 0x83c/0x820:
`pcicore_up(sih+0x90, 3)` (on this card: the Device Control read, write of
MRRS 3, read back). Chips 0x4311 rev 2 / 0x4312: a SB initiator timeout, not
this card.

#### si_pci_down (.text+0x02137e, name original)
PCI only: (the crystal-off step for chips 0x4311/0x4321/0x4716/0x4748, not
this card), then `pcicore_down(sih+0x90, 2)`: nothing on this card. No access.

#### si_pci_sleep (.text+0x01ecf6, name original)
Clears the global `do_4360_pcie2_war` (32 bit, used by
`wlc_bmac_4360_pcie2_war`, `bmac-init.md` C12), then `pcicore_sleep(sih+0x90)`:
nothing else on this card. Called by `wlc_bmac_detach` (and by the glue,
`wl_linux.c`, on suspend).

#### si_pci_fixcfg (.text+0x021e58, name original)
Makes the PCIe core's SROM shadow word 0 name the bus core's index.
1. (Chip 0x4321 rev < 2: a ChipCommon chip-control write, not this card.)
2. `orig` = `sih+0x1c0`. Bus core 0x83c/0x820/0x804: `p` =
   `si_setcore(sih, bus core id, 0)` + 0x800 (two configuration writes;
   window 1 now shows the PCIe core: `p` is its register 0x800). Other
   bridges: return 0xffffffff.
3. `v` = 16-bit R `p`; if `v >> 12` differs from the bus core index (the now
   current index): 16-bit W `p` = `(v & 0xfff) | ((index & 0xf) << 12)`.
4. `si_setcoreidx(sih, orig)`; `pcicore_hwup(sih+0x90)` (nothing here);
   return 0.

In the model the word reads 0x3000 (index 3): no write.

#### si_pcieltrenable (.text+0x01ec6f), pcie_ltrenable (.text+0x018060), names original
`si_pcieltrenable(sih, mask, val)`: PCI with bus core 0x83c/0x820 ->
`pcie_ltrenable(sih+0x90, mask, val)`, else 0. `pcie_ltrenable`: if
`pi+0x26` = 0 return 0; `v` = R `PCICFG(pi+0x26)` (Device Control 2); if
mask != 0: bit 10 (LTR mechanism enable) set if val != 0 else cleared, W, R
again; return bit 10 of `v`.

#### si_pcieobffenable (.text+0x01ec42), pcie_obffenable (.text+0x017fe1), names original
Same gate. `pcie_obffenable(pi, mask, val)`: `v` = R `PCICFG(pi+0x26)`; if
mask != 0: W `(v & ~0x6000) | ((val & 3) << 13)` (OBFF enable field), R
again; return bits 13..14 of `v`. `wlc_bmac_attach` calls it with (1, 0):
OBFF off (R, W, R of `PCICFG(0xd4)` in the model).

#### si_pcie_ltr_war (.text+0x01f905, name original)
Only PCI, bus core 0x83c, bus core revision 1 (the model's revision; the
card's is not confirmed):
1. `on` = `si_pcieltrenable(sih, 0, 0)` (one read of Device Control 2).
2. `orig` = `sih+0x1c0`; `si_setcore(sih, 0x812, 0)` (two configuration
   writes: 802.11 core).
3. `si_wrapperreg(sih, 0x160, 0xffffffff, on ? 0x02838280 : 0x02848180)`
   and `si_wrapperreg(sih, 0x164, 0xffffffff, 3)`: R, W, R of the 802.11
   core's wrapper registers 0x160 and 0x164 (purpose unknown).
4. `si_setcoreidx(sih, orig)`. No result.

Called by `wlc_bmac_attach` and `wlc_bmac_hw_up`.

#### si_survive_perst_war (.text+0x01f997, name original)
`si_survive_perst_war(sih, reset, mask, val)` (reset is a byte). Only PCI,
chip 0x4352 or 0x4360, chip revision < 4:
1. If `reset` != 0: `win` = R `PCICFG(0x80)`; `orig` = `sih+0x1c0`; `p` =
   `si_setcore(sih, 0x800, 0)` (two configuration writes); W `p + 0x634` = 2
   (through window 1: ChipCommon's PMU watchdog register; presumably resets
   the chip after two ILP ticks while the PCIe core survives); then 2000
   times delay 10 us (20 ms); `si_setcoreidx(sih, orig)`; R `PCICFG(0x80)`;
   if it differs from `win`, W `PCICFG(0x80)` = `win`.
2. If `mask` != 0 and the bus core is 0x83c/0x820: `pcie_survive_perst(
   sih+0x90, mask, val)`: `mod(PCIE(0x000), mask, val)` through `pi+0x00`
   (R and W only if mask|val != 0), then R `PCIE(0x000)` (32 bit; the fixed
   window BAR0+0x2000).

`wlc_bmac_hw_up` calls (1, 0, 0) (the watchdog reset, 2007 accesses incl.
delays), `wlc_bmac_hw_down` calls (0, 0x204, 0) (clears bits 2 and 9 of
the PCIe core's register 0).

#### si_pci_pmeclr, si_pci_pmeen, si_pci_pmestat, si_pci_pmestatclr (names original) and pcicore_pme* (names original)
Each passes `sih+0x90` to `pcicore_pmeclr/pmeen/pmestat/pmestatclr`. All
first call `sub_01831e` (name assigned `pcicore_pmecap`): if `pi+0x1d` is 0,
find the PM capability (`pcicore_find_pci_capability(osh, 1)`; none: result
0), store it in `pi+0x1d`, store the 16-bit R `PCIE(0x868)` (through
`pi+0x00`) in `pi+0x20`, and `pi+0x1e` = (R `PCICFG(cap)` & 0xf8000000) != 0.
Returns `pi+0x1e`. If it is 0 the pme function does nothing (pmestat: 0).
Then (`sub_0183bb`, name assigned `pcicore_pmcr_refresh`, acts only for bus
core revisions 0x12/0x13: not this card) and on the PM control/status
register `PCICFG(cap+4)`: pmeclr: R, W with bit 8 (PME enable) cleared;
pmeen: R, W with bit 8 set; pmestat: R, result bit 15 (PME status);
pmestatclr: R, and W of the same value if bit 15 is set (write 1 to clear).
Not reached in the emulated session (the glue calls `si_pci_pmeclr` on
resume).

#### pcicore_pmecap_fast (.text+0x0182dc, name original)
`(osh)`: `cap` = `pcicore_find_pci_capability(osh, 1, 0, 0)`; 0: return 0;
else return (R `PCICFG(cap)` & 0xf8000000) != 0 (bool). Called by
`wlc_wowl_cap`. In the model: reads 0x0c, 0x04, 0x34, 0x48, 0x48; result 1.

### Attach and detach

#### si_attach (.text+0x023844, name original)
`si_attach(devid, osh, regs, bustype, sdh, &vars, &varsz)`: allocates 0x7d8
bytes (`osl_malloc(osh, 0x7d8)`; 0 -> return 0), calls `sub_022b10`
(`si_doattach`) with the new structure and all arguments; on failure frees
the 0x7d8 bytes and returns 0. On success stores `sih+0xa8` = `*vars` (0 if
the pointer is NULL) and `sih+0xb0` = `*varsz`, returns the structure.

#### si_detach (.text+0x02171d, name original)
`si_detach(sih)`: (NULL: nothing.) Bus SI only: unmaps the mapped registers
(`sih+0xc0..`). Then `srom_var_deinit(sih)`, `nvram_exit(sih)`; bus PCI:
`pcicore_deinit(sih+0x90)` if set, `sih+0x90` = 0. Frees the 0x7d8 bytes
(`osl_mfree(osh, sih, 0x7d8)`) unless `sih` is the static structure of
`si_kattach` (`.bss+0x340`). No hardware access.

#### sub_022b10 (.text+0x022b10, name assigned in the guide: `si_doattach`)
`si_doattach(sii, devid, osh, regs, bustype, sdh, &vars, &varsz)`; returns
`sii` or 0. Steps for this card (bus type 1, PCI; all configuration accesses
32 bit; "W1" = window 1 at BAR0+0, "CC" = the fixed window BAR0+0x3000):

1. Zero the 0x7d8 bytes. `sih+0x10` = 0x21 (no bus core index yet),
   `sih+0xb8` = regs, `sih+0x60` = sdh, `sih+0x58` = osh.
2. R `PCICFG(0x88)`; if it reads 0xffffffff the bus type is changed to 0 (SI)
   and the registers are mapped (not this card). R `PCICFG(0x80)` = `win0`:
   kept only if it lies in 0x18000000..0x1801ffff and is 4 KB aligned, else
   taken as 0x18000000. W `PCICFG(0x80)` = 0x18000000 (ChipCommon). regs = 0:
   return 0.
3. `sih+0x04` = bustype. `si_ldo_war(sih, devid)` (one read of
   `PCICFG(0x08)`; 0 -> return 0). If no PCI Express capability is found
   (`pcicore_find_pci_capability(osh, 0x10)`, reads 0x0c, 0x04, 0x34 and the
   list) `si_clkctl_xtal(sih, 3, 1)` (not this card: it has the capability).
4. `id` = R W1+0x000 (ChipCommon chip id): `sih+0x3c` = id & 0xffff, `sih+0x40`
   = bits 16..19, `sih+0x44` = bits 20..23, `sih+0x00` = id >> 28 (backplane
   type; chip 0x4329 special case for the package). Byte `sih+0x4c` = 1 if
   the package is 14 or 15 (purpose unknown, not used here).
5. Backplane type 1 or 3: `ai_scan(sih, regs, devid)` (below); 0: `sb_scan`;
   2: nothing; other: return 0. Number of cores 0: return 0.
6. `p` = `si_setcoreidx(sih, 0)`. `sih+0x14` = `si_corerev` (ChipCommon
   revision, from CIB). Rev > 10: `sih+0x48` = R `p+0x2c` (chip status).
   `sih+0x18` = R `p+0x04` (capabilities). Rev > 34: `sih+0x1c` = R `p+0xac`.
   If capabilities bit 28 (PMU): `sih+0x24` = R `p+0x604`, `sih+0x20` = its
   low byte.
7. Find the bus core: `sih+0x08` = 0x700, `+0x0c` = 0xffffffff, `+0x10` =
   0x21; for every index `i` < number of cores: `si_setcoreidx(sih, i)` (two
   configuration writes; until the bus core is known the second goes to
   `PCICFG(0xac)`, see below), `si_corerev`. Bus PCI: core 0x804 = a PCI core,
   0x83c = PCIe Gen2, 0x820 = PCIe (remember index and revision). Also
   remember the index whose register base equals `win0` (step 2) (or whose
   mapped address equals regs): `home`. After the loop: a PCIe core (0x83c or
   0x820) is the bus core if there is one and either there is no PCI core or
   the PCI Express capability exists; else the PCI core. `sih+0x08` = id,
   `+0x0c` = revision, `+0x10` = index. Model: 0x83c, rev 1, index 3; `home`
   = 0.
8. PCI with FIXEDWIN and no `sih+0x90` yet: `sih+0x90` =
   `pcicore_init(sih, osh, regs + 0x2000)` (0 -> fail: step 17). Then
   `si_pci_fixcfg(sih)` (not 0 -> fail): in the model the shadow word reads 0
   and is written 0x3000 (index 3) here.
9. `si_setcoreidx(sih, home)`.
10. (Chip 0x4322 with chip status bits 6..7 = 3: return 0. Chips 0xa8e2..
    0xa8e6 with CC rev 37: a work-around. Chips 0xa9a7/0x4331: ePA chip
    control. Not this card.) Chips 0xa9c4, 0x4360, 0x4352 with **chip
    revision < 3**: `pcie_disable_TL_clk_gating(sih+0x90)` and
    `pcie_set_L1_entry_time(sih+0x90, 0x40)` (not reached with the modelled
    revision 3; not described further).
11. `nvram_init(sih)`; `srom_var_init(sih, bustype, regs, osh, &vars,
    &varsz)` (`srom-otp.md`); not 0 -> fail (step 17). `vars` = `*vars`.
12. Board identity (PCI): `ss` = R `PCICFG(0x2c)` (subsystem vendor in bits
    0..15, subsystem id in 16..31). `sih+0x30` (board vendor) =
    `si_getdevpathintvar(sih, "boardvendor")`, if 0: `ss & 0xffff`.
    `sih+0x28` (board type) = `si_getdevpathintvar(sih, "boardtype")`, if 0
    `getintvar(vars, "boardtype")`, if 0 `ss >> 16`. **Apple:** if the board
    vendor is 0x106b and the board type is 0x111, 0x112, 0x129, 0x134 or
    0x135, the board type is replaced by `ss >> 16` (the subsystem id). Board
    type 0x117 (this card) keeps the value from the variables. (Chips
    0xa9a7/0x4331 with package 9 use boardtype/subvid variables for `ss`.)
13. `sih+0x2c` = `getintvar(vars, "boardrev")`, `sih+0x34` =
    `getintvar(vars, "boardflags")`. (0xa9a7/0x4331: extpagain fix-ups.)
14. ChipCommon rev > 19: `p` = `si_setcore(sih, 0x800, 0)`; W `p+0x58` = 0,
    W `p+0x5c` = 0 (W1; chips 0xa886/0x4314 write 0x402e0/0x20500;
    purpose unknown); `si_setcoreidx(sih, home)`.
15. PMU present: `si_pmu_init`, `si_pmu_chip_init`; `x` =
    `getintvar(vars, "xtalfreq")` (0: `si_pmu_measure_alpclk`; chip
    specific constants for 0xa887, 0x4350, 0xa8ea/0xa8eb);
    `si_pmu_pll_init(sih, osh, x)`, `si_pmu_res_init`, `si_pmu_swreg_init`
    (`pmu.md`).
16. ChipCommon rev > 15: `l` = `getintvar(vars, "leddc")`, 0 -> 0xa005a;
    `sub_021d1d(sih, 0x88, 0xffffffff, l)` (CC(0x88) gpiotimerval: R, W,
    R). (Chips 0x4311/0x4321/0x4716/0x4748: crystal on.) Bus core
    0x83c/0x820: `pcicore_attach(sih+0x90, vars, 1)` (nothing on this card).
    (0xa99d/0xa8d8: chip controls.) If the variable `muxenab` exists:
    `si_muxenab(sih, getintvar(vars, "muxenab"))` (absent in the synthetic
    SROM; not described). ChipCommon rev > 20: `v` = `sub_021d1d(sih, 0x08,
    0, 0)` (one read of CC(0x08)); `sub_021d1d(sih, 0x08, v | 4, v | 4)`
    (R, W, R: sets bit 2 of CC(0x08); purpose unknown). (0x4313, 0x4350,
    0x4335: more, not this card.) Return `sih`.
17. Failure after step 8: PCI: `pcicore_deinit(sih+0x90)`, `sih+0x90` = 0;
    return 0.

Board variables read here: `boardvendor`, `boardtype` (devpath forms, then
plain), `boardrev`, `boardflags`, `xtalfreq`, `leddc`, `muxenab` (plus
`sromrev`, `boardflags2`, `subvid`, `extpagain2g/5g`, `regwindowsz`,
`manfid`, `prodid` for other chips/buses). The current core at the end of
attach is ChipCommon (index `home` = 0); `wlc_bmac_attach` selects the
802.11 core with `si_setcore(sih, 0x812, 0)`.

#### si_getdevpathintvar (.text+0x0218b6, name original)
`(sih, name)`: builds the device-path name of the variable (`sub_0217e2`,
`srom-otp.md`), returns `getintvar(NULL, it)` (the global NVRAM); if 0 and
`si_coded_devpathvar` gives a coded name, `getintvar(NULL, coded)`. No
hardware access. On this card the global NVRAM has no such variables: 0.

#### ai_scan (.text+0x001f1d, name original), sub_00133f (`get_erom_ent`), sub_001381 (`get_asd`)
`ai_scan(sih, regs, devid)` reads the enumeration ROM and fills the core
tables.

1. `erom` = R `regs+0xfc` (ChipCommon's EROM pointer, W1). PCI: `sih+0x548`
   = regs + 0x1000 (fixed from now on); W `PCICFG(0x80)` = `erom`; the ROM
   is read through W1 from offset 0 (32-bit reads). Bus SI: mapped. Other:
   return.
2. Loop over components while the read offset is below 0xe00 (reaching it:
   `sih+0x1c4` = 0, return):
   1. `cia` = `get_erom_ent(mask 0xe, match 0)`: the next valid word that is
      a component descriptor (reads skip words with bit 0 clear). If it is
      the end marker 0xf: return (normal end).
   2. `cib` = next word; if `cib & 0xe` != 0: error (numcores = 0, return).
   3. id = bits 8..19 of `cia`, vendor = bits 20..31. Skip (next component)
      if id = 0xfff with vendor 0x43b, or if the number of slave ports
      (`cib` bits 9..13) is 0.
   4. Master wrappers `nmw` = `cib` bits 14..18, slave wrappers `nsw` =
      bits 19..23. If both are 0: id 0x367 -> record the base of its first
      slave address descriptor in `sih+0x7d0` and skip; ids 0x50b/0x5dc are
      kept; all others skipped.
   5. `i` = `sih+0x1c4`: `sih+0x750+4i` = cib, `sih+0x6d0+4i` = cia,
      `sih+0x1c8+4i` = id (chip 0x5300 maps ids through a table).
   6. Master port descriptors: `cib` bits 4..8 times `get_erom_ent(1, 1)`;
      each must have `& 0xe` = 2, else error.
   7. Slave port 0, address 0: `get_asd(port 0, index 0, type slave 0x00)`.
      If there is none: bridge descriptors (type 0x40) are consumed; such a
      component is not counted (its table entries are overwritten by the
      next one). `sih+0x248+4i` = base, `sih+0x448+4i` = size.
   8. Further addresses of port 0 (index 1, 2, ...): consumed; index 1 with
      size 0x1000 is recorded in `sih+0x3c8+4i` / `sih+0x4c8+4i` = 0x1000.
   9. Ports 1 .. nsp-1: each must have at least one slave descriptor (else
      error); consumed.
   10. Master wrappers (type 0xc0) port 0..nmw-1: each must exist with size
       0x1000 and no 64-bit size; port 0's base -> `sih+0x650+4i`.
   11. Slave wrappers (type 0x80) port `k + (nsp != 1)` for k < nsw: same
       checks; if there are no master wrappers, k = 0's base ->
       `sih+0x650+4i`.
   12. `sih+0x1c4` += 1 (except for the bridge case of 7).

`get_erom_ent(sih, &ptr, mask, match)`: read the word at `ptr`, advance by
4; with mask 0 return it at once; else skip words with bit 0 clear, stop at
the end marker 0xf (returned) or at a word with `(w & mask)` = match.

`get_asd(sih, &ptr, port, index, type, &addrl, &addrh, &sizel, &sizeh)`:
reads one entry (`get_erom_ent(1, 1)`); it matches if `(w & 6)` = 4
(address descriptor), bits 8..11 = port and bits 6..7 = type; else `ptr`
goes back by 4 (the word will be read again) and 0 is returned. On a match:
`addrl` = w & 0xfffff000; bit 3 set: `addrh` = next word; size code bits
4..5: 0..2 -> size 0x1000 << code; 3 -> the next word & 0xfffff000 (and a
high word if its bit 3 is set). (The `index` argument is not compared.)

Enumeration ROM word format (as read): component descriptor A: bit 0 valid,
bits 1..3 = 0 (tag "CI"), bits 4..7 class, 8..19 core id, 20..31
manufacturer; B: bit 0 valid, bits 1..3 = 0, 4..8 master ports, 9..13 slave
ports, 14..18 master wrappers, 19..23 slave wrappers, 24..31 revision. Master
port descriptor: bits 1..3 = 1 (tag "MP"). Address descriptor: bits 1..2 =
2 (tag "ADD"), bit 3 64-bit address, bits 4..5 size code, bits 6..7 type
(0 slave, 1 bridge, 2 slave wrapper, 3 master wrapper), bits 8..11 port,
bits 12..31 address. End: 0x0000000f.

In the model (`re-out/analysis/chip/attach.txt`): five components,
ChipCommon (0x800 rev 43, one slave wrapper), 802.11 (0x812 rev 42), ARM CR4
(0x83e rev 2), PCIe Gen2 (0x83c rev 1), USB 2.0 device (0x81a rev 17), each
of the last four with one master port, one master and one slave wrapper; 29
ROM words read (some twice, after a `get_asd` push-back).

**Second window during attach.** `ai_setcoreidx` writes the wrapper window
to `PCICFG(0x70)` only when `sih+0x08` is 0x83c. Before step 7 has set the
bus core id, it writes `PCICFG(0xac)` (the register of older bridges; behind
a PCIe Gen2 core that is inside the PCI Express capability, read only).
Nothing in attach uses window 2 before that.

#### si_kattach (.text+0x02373b, name original)
Attach of the chip the code runs on (static `si_info` at `.bss+0x340`), for
the system bus; not used on PCI.
## Who uses `si_info_t`

Measured over the whole standard session (attach, up, nine channel changes,
watchdog, down, up, a channel, watchdog, down, detach) with a Unicorn memory
hook over the 0x7d8 bytes of the card's `si_info` from its allocation in
`si_attach` on (`re-out/analysis/chip/calls.py`), and a check of the memory
imports (`osl_memcpy/memmove/memset/memcmp`) called from outside:

* **No code outside `siutils.c`, `aiutils.c`, `nicpci.c` writes any byte of
  `si_info`, and none reads beyond offset 0x48.** Fields read from outside:
  0x04 (bus type), 0x08 and 0x0c (bus core id/rev: `dma_attach`,
  `wlc_bmac_attach`), 0x14, 0x18, 0x1b (byte 3 of the ChipCommon
  capabilities), 0x1c, 0x20, 0x24 (ChipCommon/PMU revisions and
  capabilities: OTP, PMU, BT-coex code), 0x28, 0x30 (board type/vendor),
  0x3c, 0x40, 0x44, 0x48 (chip id/rev/package/status). `osh` (0x58) is
  obtained through `si_osh`. No memory import touches the structure from
  outside.
* The glue (`src/wl/sys/wl_linux.c`, not emulated) reads `sih->bustype` and
  calls `si_pci_sleep` and `si_pci_pmeclr`.

So the private part (0x58..0x7d8: current core, windows, core tables,
callbacks, `pcicore_info`) belongs to the backplane alone; an open
implementation of the `si_*` functions only has to keep the public fields
0x00..0x48 at their offsets for the object's other layers.

Fields each session function reads / writes (beyond the dispatch on 0x00):

| Function | Reads | Writes |
|---|---|---|
| `si_osh` | 0x58 | - |
| `si_coreidx`, `si_coreid`, `si_corerev` | 0x1c0, 0x1c8.., 0x750.. | - |
| `si_backplane64` | 0x18 | - |
| `si_register_intr_callback` | 0x1c0, 0x1c8.. | 0x68, 0x70, 0x78, 0x80, 0x88 |
| `si_deregister_intr_callback` | - | 0x78 |
| `si_setcoreidx` | 0x04, 0x08, 0x58, 0x1c4, 0x248.., 0x650.., 0xb8 | 0x1c0 (0xb8 unchanged) |
| `si_setcore` | 0x1c4, 0x1c8.. + as `si_setcoreidx` | 0x1c0 |
| `si_corereg` | 0x04, 0x08, 0x0c, 0x10, 0x1c8.., 0xb8; slow path also 0x1c0, 0x68, 0x70, 0x78, 0x80, 0x248.., 0x650.. | slow path: 0x1c0 (restored) |
| `si_iscoreup`, `si_core_reset`, `si_core_disable`, `si_wrapperreg` | 0x548 | - |
| `si_core_cflags`, `si_core_sflags`, `si_core_cflags_wo` | 0x3c, 0x548 (others only for other chips) | - |
| `si_gpio*` | 0x04, 0x14 (`gpioled`, `gpiotimerval`) + as `si_corereg` on core 0 | - |
| `si_clkctl_xtal` | 0x04, 0x08 | - |
| `si_deviceremoved` | 0x04, 0x58 | - |
| `si_get_sromctl`, `si_set_sromctl` | 0x1c0 + as `si_setcoreidx`, 0x750.. | 0x1c0 (restored) |
| `si_chip_hostif` | 0x3c, 0x44, 0x48 | - |
| `si_fabid`, `si_pci_war16165` | 0x3c / 0x04, 0x08, 0x0c | - |
| `si_ldo_war` | 0x58, 0xb8 | - |
| `si_tcm_size` | 0x68..0x80, 0x1c0, 0x1c8.. + as `si_setcore`, `si_iscoreup`, ... | 0x1c0 (restored) |
| `si_pci_setup`, `si_pci_up`, `si_pci_down`, `si_pci_sleep` | 0x04, 0x08, 0x0c, 0x3c, 0x40, 0x58, 0x90 | (`pcicore_info` read only) |
| `si_pci_fixcfg` | 0x08, 0x3c, 0x1c0, 0x90 + as `si_setcore` | 0x1c0 (restored) |
| `si_pcie_ltr_war`, `si_pcieltrenable`, `si_pcieobffenable` | 0x04, 0x08, 0x0c, 0x90, 0x1c0 + as `si_setcore` | 0x1c0 (restored) |
| `si_survive_perst_war` | 0x04, 0x08, 0x3c, 0x40, 0x58, 0x90, 0x1c0 + as `si_setcore` | 0x1c0 (restored) |
| `si_pci_pme*` | 0x90 | `pcicore_info` 0x1d, 0x1e, 0x20 (first use) |

## Verification

`re-out/analysis/chip/calls.py` runs the standard session (the steps of
`hybrid.py`) on the object alone and records every call into the three files
from another layer: arguments, result, every bus access in order (MMIO with
its BAR0 offset, so the window used is visible; PCI configuration reads and
writes; delays; the entry/return of the MAC's interrupt callbacks), and the
`si_info` and `pcicore_info` bytes before and after. `model.py` is an
independent model written from this text; for each recorded call it
produces the accesses (taking read values from the object's access at the
same position when operation, address and width match), the result and the
structure bytes after the call, and compares them.

Result: **352 calls of 37 functions, 3,012 accesses, 0 differences** (all
session functions of this layer except those of other specifications:
`si_pmu_*`, `si_clkctl_init`, `si_clkctl_fast_pwrup_delay`, `si_alp_clock`,
`si_is_otp_*`, `si_is_sprom_available`, `si_chipcontrl_*`, and except
`si_attach`/`si_detach`). Covered there: `si_core_cflags` (136 calls),
`si_corereg` (45, all on core 0 or 3: fixed windows), `si_setcoreidx` (36),
`si_core_reset` (12), `si_iscoreup` (12, up and down), `si_core_disable`
(6), `si_survive_perst_war` (reset branch and mask branch), `si_tcm_size`
(with the interrupt callbacks, a reset and a disable of the ARM core),
`si_pcie_ltr_war`, `si_pci_*`, the GPIO functions, `si_get/set_sromctl`.

Not exercised by the session (described from the code, model written but not
compared): the slow path of `ai_corereg` (other cores than ChipCommon and
PCIe), the time-outs of the polling loops, `ai_core_disable` on a core
already in reset, `si_switch_core`/`si_restore_core`, the pme functions,
`si_pcieltrenable` with mask != 0, `si_pci_fixcfg` with a wrong index.

## Open questions

1. Purpose of the 802.11 wrapper registers 0x160/0x164 written by
   `si_pcie_ltr_war` (values 0x02848180 / 0x02838280 and 3); the model reads
   them as 0.
2. The bus core revision of the real card (the LTR work-around runs only
   for revision 1) and the real configuration space capability list (the
   offsets 0xb4/0xbc/0xd4/0x48 follow from the model's list).
3. `si_survive_perst_war(1, ...)` writes the PMU watchdog: on the card this
   presumably resets the chip (except the PCIe core); the model does nothing.
   What the driver expects afterwards is not modelled.
4. `ai_corereg` does not check `idx` against the number of cores: an index
   between the number of cores and 31 that is not ChipCommon or the bus core
   makes `ai_setcoreidx` return 0 and the access goes to address `regoff`
   (a fault). No caller does this on this card.
5. The ARM CR4 capability register (bank counts) and bank info registers
   read 0 in the model; `si_tcm_size` on the card will loop over the real
   banks (access count not known).
6. `CC(0x58)`/`CC(0x5c)` written 0 and bit 2 of `CC(0x08)` set at attach:
   purpose unknown.

## Compared with bcma

**From memory of the Linux kernel's `bcma` driver; not verified against its
source. A pointer for later work, not a specification.**

Probably the same in `bcma`:
* Core enumeration from the EROM (`scan.c`): EROM pointer at ChipCommon
  0xfc, the same component/master-port/address descriptor format, skipping
  of ARM default components (vendor 0x43b, id 0xfff), first slave address and
  first wrapper of each core.
* Core switching on PCI (`host_pci.c`): `PCICFG(0x80)` = core base,
  `PCICFG(0x70)` = wrapper base, registers at BAR0+0, wrapper at BAR0+0x1000,
  with a cache of the mapped core. As far as remembered `bcma` does not use
  the fixed ChipCommon/PCIe windows at BAR0+0x3000/0x2000 as the object does
  (`ai_corereg`), so its access pattern for ChipCommon and the PCIe core
  differs (more window moves).
* `bcma_core_is_enabled` = `ai_iscoreup` (clock on without FGC, not in
  reset).
* GPIO helpers of ChipCommon (out, outen, control, timer) with mask/value.
* PMU and resource tables (`driver_chipcommon_pmu.c`), partly chip
  specific; whether the 4360 is covered is not known.

Where the object differs or does more for this chip:
* Core reset: `ai_core_reset` sets ioctrl first, then pulses resetctrl
  (1, then 0, with resetstatus waits), with no disable step inside;
  `bcma_core_enable` disables first. `ai_core_disable` writes resetctrl
  before ioctrl and waits for resetstatus (300 + 10000 us).
* PCIe Gen2 work-arounds of this layer: `si_pcie_ltr_war` (802.11 wrapper
  registers 0x160/0x164, bus core revision 1), `si_survive_perst_war`
  (PMU watchdog reset with a 20 ms wait at up, clearing bits 2 and 9 of
  PCIe register 0 at down; chips 0x4360/0x4352 rev < 4),
  `si_pcieobffenable(1, 0)` (OBFF off), MRRS 1024 bytes (`pcicore_up`), the
  SROM shadow index (`si_pci_fixcfg`), the PCI interrupt mask
  (`si_pci_setup`), `pcie_disable_TL_clk_gating` /
  `pcie_set_L1_entry_time(0x40)` for chip revisions < 3. `bcma`'s
  `driver_pcie2.c` has its own LTR / PERST work-arounds for other core
  revisions (unverified which).
* ChipCommon writes at attach: 0x58/0x5c = 0, gpiotimerval = `leddc`
  (0xa005a), bit 2 of 0x08; Apple board-type override from the PCI
  subsystem id.
* `si_tcm_size` (resets/disables the ARM CR4 to read its bank sizes) and the
  interrupt-callback bracketing of core switches have no direct `bcma`
  counterpart that is needed here.
