# Task `bmac-updown`: up and down of the MAC core

Specification: [`../spec/bmac-init.md`](../spec/bmac-init.md), sections
**C2** (`wlc_bmac_hw_up`), **C3** (`wlc_bmac_up_prep`), **C4**
(`wlc_bmac_up_finish`), **C5** (`wlc_bmac_down_prep`), **C6**
(`wlc_bmac_down_finish`), **C8** (`wlc_bmac_reset` and `sub_05fefa`, the
flush of the queues), **C10** (`sub_06333d`, the coexistence GPIOs
released), **C15** (`wlc_bmac_hw_down`) and **D3**
(`wlc_bmac_validate_chip_access`). C1 and C16 tell how the upper layer calls
these functions and what the whole down looks like in the trace (read them;
nothing to implement there). The notation section and the reference tables
of "Data" apply. Register access: `open/hw/access.c`.

Interface: `open/include/bcm4360/mac.h` (given: the prototypes under "up and
down", the fields under "up and down", and the prototype of
`bcm4360_bmac_clkctl_clk`) and `open/include/bcm4360/mac_env.h` (the
services, the block "for up and down"). Write **`open/mac/bmac_updown.c`**.

## Why

The sixth MAC increment. These functions run at every up and down of the
interface: they prepare the chip after power-up, bracket `wlc_bmac_init`
(clocks, PCIe bridge, the radio's hardware switch, the first core reset,
interrupts on), and take the core down again (interrupts off, suspend, reset,
core disable, crystal off). With this task they become open code inside the
real driver (hybrid build, switch `open_mac`, adapter `hybrid/mac_updown.c`,
already written).

## The functions

| Open function | Broadcom's | Section |
|---|---|---|
| `bcm4360_bmac_hw_up` | `wlc_bmac_hw_up`, with C10 (`sub_06333d`) as a `static` helper | C2 |
| `bcm4360_bmac_up_prep` | `wlc_bmac_up_prep` | C3 |
| `bcm4360_bmac_up_finish` | `wlc_bmac_up_finish` | C4 |
| `bcm4360_bmac_down_prep` | `wlc_bmac_down_prep` (also uses C10) | C5 |
| `bcm4360_bmac_down_finish` | `wlc_bmac_down_finish` | C6 |
| `bcm4360_bmac_hw_down` | `wlc_bmac_hw_down` | C15 |
| `bcm4360_bmac_reset` | `wlc_bmac_reset`, with `sub_05fefa` as a `static` helper (C6 uses it too) | C8 |
| `bcm4360_bmac_validate_chip_access` | `wlc_bmac_validate_chip_access` | D3 |

Where the specification calls a MAC function that is already open, call the
open one: `wlc_bmac_xtal` -> `bcm4360_bmac_xtal`, `wlc_bmac_corereset` ->
`bcm4360_bmac_corereset`, `wlc_bmac_radio_read_hwdisabled`,
`wlc_hw_deviceremoved` -> `bcm4360_bmac_deviceremoved`,
`wlc_bmac_suspend_mac_and_wait` -> `bcm4360_bmac_suspend`,
`wlc_coredisable` -> `bcm4360_bmac_coredisable`, `wlc_bmac_set_ctrl_SROM` /
`_bt_shd0`, `wlc_bmac_copyto_objmem` / `copyfrom_objmem`, and
`wlc_bmac_clkctl_clk` (A13, `sub_064887`) -> `bcm4360_bmac_clkctl_clk`.

**The one change allowed in another file:** A13 is a `static` function of
`open/mac/bmac_clk.c` (`wlc_bmac_clkctl_clk`). Make it the exported
`bcm4360_bmac_clkctl_clk` declared in `mac.h`: rename it, drop `static`,
update its callers in `bmac_clk.c`. Nothing else in that file. Leave the
file-local copy in `bmac_init.c` alone.

## Rules for this code

* Clean room: `../implementing.md`, binding.
* Everything outside the 802.11 core's registers goes through `mac_env.h`,
  called exactly where the specification calls the corresponding function:
  the backplane and PCIe calls (`si_survive_perst_war`, `si_ldo_war`,
  `si_pmu_res_init`, `si_clkctl_init`, `si_pcie_ltr_war`, `si_pci_fixcfg`,
  `si_pci_setup`, `si_pci_up`, `si_pci_down`, `si_seci_down`,
  `si_pmu_res_req_timer_clr`, `si_pmu_rfldo`, `si_btcgpiowar`, the GPIO
  functions, `si_iscoreup`), the PHY (`wlc_phy_por_inform`,
  `wlc_phy_hw_state_upd`, `wlc_phy_hw_clk_state_upd`, `wlc_phy_down`), the
  DMA engine slots 0x60 / 0xe0 of `sub_05fefa`, the glue (`wl_intrsoff`,
  `wl_intrson`, `wl_reset`), and the words of the upper layer (`pub+0x40`,
  `wlc+0x750`, the core index and the pending counters of `wlc+0x38`, the
  band's PHY type at `wlc+0x40`, the reset counter, `wlc+0x68`,
  `wlc_hw+0x98`), and the LED table of C2 step 9 (`led_entry`,
  `led_gpio_store`).
* Two Broadcom functions stay Broadcom's in this increment and are services:
  `wlc_bmac_4360_pcie2_war` (C12; C2 step 7 calls it with
  `pcie2_war_freq`) and `wlc_bmac_ifsctl_edcrs_set` (E9; C4 step 5 calls it
  with the argument computed from `bcm4360_bmac_env_wlc_band_phytype`). Do not
  implement C12 or E9.
* **Services can change the state** (`mac_env.h`): after any service that
  can run MAC code (the PHY, `wl_reset`, the two Broadcom functions above)
  the platform has reloaded `*mac`; never keep a field in a local across
  one. In particular `wl_reset` (C6 step 3.2) runs the upper layer's reset,
  which calls your own `bcm4360_bmac_reset` through the platform: the core is
  reset and the clock fields change under you.
* Write the fields the specification writes (`up`, `clk`, `sbclk`,
  `ucode_loaded`, and those of the functions you call); the caller copies
  them back. `noreset`, the chip and board fields, `machwcap`, `cccaps_ext`,
  `ccrev`, `led_gpio_mask`, `pcie2_war_freq`, `btc_gpio_mask/out`,
  `dma_fifos`, `mac_rev` and `bustype` are inputs.
* D3 compares 32-bit words written and read back through the object memory
  functions as 4 bytes; save and restore the original 4 bytes of `SHM(0)`
  as the section says.
* Card and revision: MAC 42, AC-PHY (type 11), chip 0x4360 revision 3, PCIe,
  PMU present. Branches for other chips, buses or revisions (0x4350, 0xa886,
  0x4313, 0xa8dc, 0xa8d8/0xa8d9/0xa99d, 0x4331, PCMCIA, MAC revision <= 10
  in D3, MAC revision 4 in `sub_05fefa`): a one-line comment, no code.

## Tests

From `tools/re` (Docker, Linux tools; Docker Desktop is running; the adapter
`hybrid/mac_updown.c` is linked automatically as soon as
`open/mac/bmac_updown.c` exists):

* `python ab.py build` (no warnings).
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-bmacupdown bcm4360-emu:latest python
  hybrid.py check` must report **the same**; likewise `check --off mac`,
  `check --on phy`, `check --on phy --off mac`, and `srom` (it attaches ten
  cards: D3 runs at each attach). The session has two ups (the one-time
  preparation of C2 only at the first) and two downs, and the upper layer's
  reset runs inside each up and each down.
* `python ab.py all` must still pass (`AB_OUT=/repo/re-out/open-bmacupdown`
  if you run it in Docker).

The paths of a card that is gone (C5, C6 step 2, C8 step 2), the radio
disabled by the hardware switch (C3 step 6), `noreset` set, an offload state
present, `si_seci_down`, the second LED branch (bit 11 of `boardflags2`) and
the failure branches of D3 are not reached by the session: write them from
the specification.

Do not edit `hybrid/` or `tools/`, nor any file of `open/` except your own
and the one change to `bmac_clk.c` above. Questions into
`docs/re/questions/bmac-updown.md`; finish with a short report.
