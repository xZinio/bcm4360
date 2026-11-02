# Task `bmac-clk`: clocks, core reset, crystal and core disable of the MAC

Specification: [`../spec/bmac-init.md`](../spec/bmac-init.md), sections
**A10** to **A17** (gated clocks, MAC-PHY clock, PHY clock, the fast/dynamic
clock `sub_064887`, the core reset, the PHY reset, the PHY PLL, the
coexistence mode), **C7** (core disable), **C9** (crystal, PLL requests,
`set_clk`), **C11** (`set_ctrl_bt_shd0`, `set_ctrl_SROM`), **C13** (the
radio's hardware switch), **C14** (card removed), the notation section
("cflags", "SPINWAIT") and the reference tables of "Data". Register access:
`open/hw/access.c` (use it, do not change it).

Interface: `open/include/bcm4360/mac.h` (given: the new prototypes and the
fields under "clocks, core reset, crystal, core disable") and
**`open/include/bcm4360/mac_env.h`** (the services: backplane, PHY, DMA).
Write **`open/mac/bmac_clk.c`**. Your predecessors' `bmac_ctrl.c`,
`bmac_mhf.c` and `bmac_mem.c` are the style; call their functions where the
specification calls `wlc_bmac_mctrl`, `wlc_ucode_wake_override_set/clear`,
`wlc_bmac_write_ihr` and so on (do not change those files).

## Why

The fourth increment of the MAC layer. These functions bring the 802.11
core's clocks up and down and reset the core and the PHY; every bring-up,
channel change and shutdown goes through them. With this task they become
open code inside the real driver (hybrid build, switch `open_mac`, adapter
`hybrid/mac_clk.c`, already written). It is also the first MAC increment
that calls other layers, through `mac_env.h`.

## The functions

| Open function | Broadcom's | Section |
|---|---|---|
| `bcm4360_bmac_phyclk_fgc` | `wlc_bmac_phyclk_fgc` | A10 |
| `bcm4360_bmac_macphyclk_set` | `wlc_bmac_macphyclk_set` | A11 |
| `bcm4360_bmac_core_phy_clk` | `wlc_bmac_core_phy_clk` | A12 |
| `bcm4360_bmac_corereset` | `wlc_bmac_corereset`, with A13 (`sub_064887`) and the receive reset of FIFO 0 (`sub_0638a5`) as `static` helpers | A13, A14 |
| `bcm4360_bmac_phy_reset` | `wlc_bmac_phy_reset` | A15 |
| `bcm4360_bmac_core_phypll_ctl` / `_core_phypll_reset` | the two of A16 | A16 |
| `bcm4360_bmac_btc_mode_get` | `wlc_bmac_btc_mode_get` (the field `btc_mode`) | A17 |
| `bcm4360_bmac_coredisable` | `wlc_coredisable` | C7 |
| `bcm4360_bmac_xtal`, `_pllreq`, `_set_clk` | `wlc_bmac_xtal`, `_pllreq`, `_set_clk` | C9 |
| `bcm4360_bmac_set_ctrl_bt_shd0`, `_set_ctrl_srom` | `wlc_bmac_set_ctrl_bt_shd0`, `_set_ctrl_SROM` | C11 |
| `bcm4360_bmac_radio_read_hwdisabled` | `wlc_bmac_radio_read_hwdisabled` | C13 |
| `bcm4360_bmac_deviceremoved` | `wlc_hw_deviceremoved` | C14 |

## Rules for this code

* Clean room: `../implementing.md`, binding.
* Everything outside the 802.11 core's registers goes through the services of
  `mac_env.h`, called exactly where the specification calls the
  corresponding function: `cflags(mask, value)` of the notation is
  `bcm4360_bmac_env_core_cflags`; `si_core_reset`, `si_core_disable`,
  `si_iscoreup`, `si_clkctl_xtal`, `si_gpiocontrol`, `si_corereg`,
  `si_chipcontrl_srom4360`, `si_deviceremoved` and the PHY functions
  likewise; the DMA engine's function slots 0x10 / 0xa8 of A14 are
  `bcm4360_bmac_env_dma_txreset` / `_rxreset` (only for a FIFO whose bit is
  set in `dma_fifos`); `wlc_hw+0x94` = 0 is
  `bcm4360_bmac_env_clear_intstatus`.
* **A service may change the state**: the platform writes `struct
  bcm4360_mac` back before a PHY service and reads it again after it (the PHY
  can call back into the MAC). Never keep a field of `*mac` in a local
  variable across a service call; read it again from `*mac`. (A14 step 2's
  "fast" is a value taken at entry by the specification itself: that one is
  meant to be a local.)
* The state is `struct bcm4360_mac`: write the fields the specification
  writes (`clk`, `forcefastclk`, `sbclk`, `phyclk`, `pllreq`,
  `suspended_fifos`, `maccontrol`, `wake_override`, `mute_override`); the
  caller copies them back. `noreset`, `band_*`, `dma_fifos`, `gpio_sel`, the
  chip fields and `btc_mode` are inputs.
* Card and revision: MAC core revision 42, AC-PHY (type 11), chip 0x4360
  revision 3, PCIe, with a PMU. Implement the branches the sections describe
  for this card; the paths "not described" for other revisions, chips or
  PHY types get a one-line comment, no code. Where a section names a chip
  list (C11: 0xa9c4, 0x4360, 0xaa06, 0x4352), test `chip_id` as written.

## Tests

From `tools/re` (Docker, Linux tools; Docker Desktop is running; the adapter
`hybrid/mac_clk.c` is linked automatically as soon as
`open/mac/bmac_clk.c` exists):

* `python ab.py build` (no warnings).
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-bmacclk bcm4360-emu:latest python
  hybrid.py check` must report **the same**; likewise `check --off mac`,
  `check --on phy` and `check --on phy --off mac`. In the session the core
  is reset 9 times, the PHY reset 9 times, `phyclk_fgc` runs 82 times, the
  crystal is switched 10 times, `deviceremoved` 19 times, the core disabled 3
  times, the hardware radio switch read 7 times.
* `python hybrid.py srom` must still report the same (attach goes through
  the core reset).
* `python ab.py all` must still pass (`mac-ctrl`, `mac-mhf`).

`pllreq`, `set_clk` and `core_phypll_reset` are not reached by the session:
write them from the specification.

Do not edit `hybrid/` or `tools/`. Questions into
`docs/re/questions/bmac-clk.md`; finish with a short report.
