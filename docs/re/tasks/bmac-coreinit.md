# Task `bmac-coreinit`: the initialisation of the MAC core

Specification: [`../spec/bmac-init.md`](../spec/bmac-init.md), sections
**B1** (`wlc_bmac_init`, all 40 steps), **B2** (microcode download), **B3**
(microcode start), **B6** (initialisation lists), **B7** (buffer memory
controller), **B8** (synthesizer power-up delay), **B9** (MAC clock
frequency), **B10** (band initialisation), **B12** (OFDM rate table and its
two wrappers), **E6** (`wlc_bmac_set_shortslot`), and of **E10** the two
coexistence helpers `sub_06106b` and `sub_062b79` (not the others). The
notation section, the reference tables of "Data" and section G (shared
memory map) apply. Register access: `open/hw/access.c`.

Interface: `open/include/bcm4360/mac.h` (given: the new prototypes and the
fields under "the initialisation") and `open/include/bcm4360/mac_env.h`
(the services, including the new block "for the initialisation"). Write
**`open/mac/bmac_init.c`**. Call the functions of your predecessors'
`bmac_ctrl.c`, `bmac_mhf.c`, `bmac_mem.c`, `bmac_clk.c` wherever the
specification calls the corresponding Broadcom function (`wlc_bmac_mctrl`,
`wlc_bmac_mhf`, `wlc_bmac_write_amt`, `wlc_bmac_copyto/copyfrom_objmem`,
`wlc_bmac_write_inits`, `wlc_bmac_set_cwmin/cwmax`, `sub_064887` = the
fast/dynamic clock of A13 inside `bmac_clk.c`, `wlc_bmac_macphyclk_set`,
...); where a helper you need is `static` in one of their files, ask in your
questions file and, for now, implement it file-local in yours. Do not
change their files.

## Why

The fifth MAC increment, and the largest closed piece of the MAC that still
runs: it downloads the microcode (the busiest loop of the MAC layer: 10,850
writes), runs the initialisation lists, sets up the buffer memory controller
and the band. With this task it becomes open code inside the real driver
(hybrid build, switch `open_mac`, adapter `hybrid/mac_init.c`, already
written).

## The functions

| Open function | Broadcom's | Section |
|---|---|---|
| `bcm4360_bmac_init` | `wlc_bmac_init`, with B2 (`sub_0607b0`/`sub_060744`), B7 (`sub_067efd`), B8 (`sub_05fdca`, `sub_0627c9`), B10 (`sub_06656c` with `sub_062766`, `sub_062684`, `sub_062716`), B12 (`sub_062403`), E10 `sub_06106b`/`sub_062b79` as `static` helpers | B1 ... |
| `bcm4360_bmac_wowlucode_start` | the start sequence at `wlc_bmac_wowlucode_start` | B3 |
| `bcm4360_bmac_switch_macfreq` | `wlc_bmac_switch_macfreq` | B9 |
| `bcm4360_bmac_set_extlna_pwrsave_shmem` | the same name | B10 step 15 |
| `bcm4360_bmac_txbw_update`, `_band_stf_ss_set` | the two wrappers of B12 | B12 |
| `bcm4360_bmac_set_shortslot` | `wlc_bmac_set_shortslot` | E6 |

## Rules for this code

* Clean room: `../implementing.md`, binding (mind its note on struct
  initializers becoming `memset` calls).
* **Broadcom's data** - the microcode `d11ucode42` (43,400 bytes: 10,850
  32-bit words), the lists `d11ac1initvals42` and `d11ac1bsinitvals42`
  (8-byte entries as B6 says, ended by offset 0xffff), the table
  `btc_ucode_flags` (36 bytes, E10) - comes from `hw_fw_data(mac->hw, name,
  &size)` (`hw.h`). Never copy any of it into the code. For other MAC
  revisions / PHY types the specification names other images: a comment.
* Everything outside the 802.11 core's registers goes through `mac_env.h`,
  called exactly where the specification calls the corresponding function:
  `wl_intrsoff`/`wl_intrsrestore` of the glue, `si_pmu_rfldo`,
  `wlc_setxband`, the PHY functions, `wlc_bmac_btc_mode_set`,
  `si_seci/eci/gci_init`, `si_gpiocontrol`, `si_core_sflags`,
  `si_clkctl_fast_pwrup_delay`, the DMA engine slots 0x08/0xa0/0xd8 of step
  30 (only for FIFOs set in `dma_fifos`), `wlc+0x68`, `wlc+0x718`, the
  variables `btc_params<n>`/`btc_flags` (parse values with
  `bcm4360_strtoul` of `utils.h`), the interface address at `pub+0x08`,
  `wlc_bmac_mute`, `si_pmu_get_bb_vcofreq`, `si_gpioouten`.
* **Services can change the state** (`mac_env.h`): after any service that
  can run MAC code (the PHY, `setxband`, `btc_mode_set`, `mute`) the platform
  has reloaded `*mac`; never keep a field in a local across one. In
  particular `wlc_setxband` (step 4) changes the current band, so the
  `band_*` fields are only valid after it.
* Write the fields the specification writes (`ucode_loaded`,
  `fastpwrup_dly`, `mac_suspend_depth`, `wake_override`, `shortslot`,
  `stf_ss_mode`, `btc_flags`, `btc_shm_base`, and those of the functions you
  call); the caller copies them back.
* B9 needs a 64-bit division by a variable (step 2): write it with a
  shift-and-subtract loop (the kernel build has no 64-bit `/` on all
  architectures); it must give the floor of 0x3a980000000 / vco exactly.
* Card and revision: MAC 42, AC-PHY (type 11), chip 0x4360 revision 3,
  PCIe, PMU present. Branches for other chips/revisions: a one-line comment.

## Tests

From `tools/re` (Docker, Linux tools; Docker Desktop is running; the adapter
`hybrid/mac_init.c` is linked automatically as soon as
`open/mac/bmac_init.c` exists):

* `python ab.py build` (no warnings).
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-bmacinit bcm4360-emu:latest python
  hybrid.py check` must report **the same**; likewise `check --off mac`,
  `check --on phy`, `check --on phy --off mac`, and `srom`. The session runs
  `wlc_bmac_init` twice (two ups: the second one without microcode download,
  `ucode_loaded` set), the band initialisation, `switch_macfreq`, the slot
  time.
* `python ab.py all` must still pass.

`set_shortslot` with the interface up in 2.4 GHz, the antenna selection
types 1/2/3/6, the coexistence branches and `mute` = 1 are not reached by the
session: write them from the specification.

Do not edit `hybrid/` or `tools/`. Questions into
`docs/re/questions/bmac-coreinit.md`; finish with a short report.
