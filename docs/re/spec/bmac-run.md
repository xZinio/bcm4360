# The MAC core at run time: what of `wlc_bmac.c` still runs (bmac-run)

Status: complete for the functions of the Scope (2026-11-02). Every section
was read in the decompiler output, checked in the disassembly, observed in a
session on the object and, for the computations and the branches a session
does not reach, checked by direct calls against a model of this text (see
Verification).
Register notation: [access.md](access.md). Names of `wlc_hw` fields as in
[bmac-init.md](bmac-init.md) "Data"; procedures of that specification are
cited by their section number (A3 = `wlc_ucode_wake_override_set`, A6 =
`wlc_bmac_suspend_mac_and_wait`, A7 = `wlc_bmac_enable_mac`, A9 =
`wlc_bmac_mhf`, ...). DMA function slots are those of
[bmac-data.md](bmac-data.md) D3.

## Scope

The functions of `wlc_bmac.c` (MAC core revision 42, AC-PHY, chip 0x4360 rev 3,
PCIe) that the upper layer (`wlc.c` and its modules) and the PHY call after
bring-up, and that the open MAC layer does not run yet: energy detect,
transmit FIFO suspend/resume, Bluetooth coexistence setters, LEDs, CCA
statistics, rate shared-memory offsets, TSF, FIFO errors, the MAC watchdog and
a few small setters. Attach/detach (`wlc_bmac_attach`, `wlc_bmac_detach`)
belong to bmac-init D2/D4 and are cited only for the calls they make here.

| function | .text offset | size | name / section |
|---|---|---|---|
| `wlc_bmac_ifsctl_edcrs_set` | 0x062340 | 195 | original - R1 |
| `sub_06212d` | 0x06212d | 428 | `wlc_bmac_ifsctl_ac_edcrs` (assigned) - R1 |
| `sub_0622d9` | 0x0622d9 | 103 | `wlc_bmac_ifs_ctl_edcrs` (bmac-init's name) - R1 |
| `wlc_bmac_tx_fifo_suspend` | 0x063f93 | 256 | original - R2 |
| `wlc_bmac_tx_fifo_resume` | 0x06061c | 81 | original - R2 |
| `wlc_bmac_tx_fifo_suspended` | 0x0604e3 | 73 | original - R2 |
| `wlc_bmac_btc_mode_set` | 0x063b1b | 783 | original - R3 |
| `wlc_bmac_btc_wire_set` | 0x06003c | 350 | original - R3 |
| `wlc_bmac_btc_wire_get` | 0x06002c | 16 | original - R3 |
| `wlc_bmac_btc_mode_get` | 0x06001d | 15 | original - R3 (bmac-init A17) |
| `wlc_bmac_set_btswitch` | 0x066f30 | 294 | original - R3 |
| `wlc_bmac_update_bt_chanspec` | 0x060317 | 312 | original - R3 |
| `wlc_bmac_seci_upd` | 0x0602c2 | 85 | original - R3 |
| `sub_062b79` | 0x062b79 | 325 | `wlc_bmac_btc_flags_to_mhf` (bmac-init's name) - R3 |
| `sub_06106b` | 0x06106b | 168 | `wlc_bmac_btc_gpio_enable` (bmac-init's name) - R3 |
| `wlc_bmac_led` | 0x060e32 | 129 | original - R4 |
| `wlc_bmac_led_attach` | 0x0614e0 | 384 | original - R4 |
| `wlc_bmac_led_detach` | 0x061463 | 125 | original - R4 |
| `wlc_bmac_led_hw_deinit` | 0x0634a5 | 127 | original - R4 |
| `wlc_bmac_led_hw_mask_init` | 0x05ffbc | 12 | original - R4 |
| `wlc_bmac_led_set` | 0x05ffc8 | 24 | original - R4 |
| `wlc_bmac_blink_sync` | 0x05ffe0 | 43 | original - R4 |
| `wlc_bmac_led_blink_event` | 0x0612a0 | 200 | original - R4 |
| `wlc_bmac_led_blink` | 0x061368 | 251 | original - R4 |
| `sub_061660` | 0x061660 | 554 | `wlc_bmac_led_timer` (assigned) - R4 |
| `cca_stats_read` | 0x061b4f | 221 | original - R5 |
| `sub_061ad3` | 0x061ad3 | 124 | `wlc_bmac_read_shm32` (assigned) - R5 |
| `wlc_bmac_cca_stats_read` | 0x061c2c | 13 | original - R5 |
| `wlc_bmac_rate_shm_offset` | 0x061c39 | 52 | original - R6 |
| `wlc_bmac_stf_get_rateset_shm_offset` | 0x061c6d | 135 | original - R6 |
| `wlc_bmac_stf_set_rateset_shm_offset` | 0x0620d7 | 86 | original - R6 |
| `wlc_bmac_read_tsf` | 0x060499 | 74 | original - R7 |
| `wlc_bmac_fifoerrors` | 0x064689 | 240 | original - R7 |
| `wlc_bmac_watchdog` | 0x067cf0 | 91 | original - R7 |
| `wlc_bmac_state_get` | 0x0678e8 | 48 | original - R7 |
| `wlc_bmac_xmtfifo_sz_get` | 0x05fe87 | 33 | original - R8 |
| `wlc_bmac_ampdu_set` | 0x061226 | 66 | original - R8 |
| `wlc_bmac_antsel_type_set` | 0x064779 | 33 | original - R8 |
| `wlc_bmac_antsel_set` | 0x060011 | 12 | original - R8 |
| `wlc_bmac_filter_war_upd` | 0x06452e | 26 | original - R8 |
| `wlc_bmac_txant_set` | 0x0626e8 | 46 | original - R8 (re-check of bmac-init E8) |
| `sub_062684` | 0x062684 | 100 | `wlc_ucode_txant_set` (bmac-init's name) - R8 |
| `sub_061268` | 0x061268 | 29 | `wlc_bmac_wlintrsoff` (assigned) - R9 |
| `sub_061285` | 0x061285 | 27 | `wlc_bmac_wlintrsrestore` (assigned) - R9 |
| `wlc_bmac_4331_epa_init` | 0x060eb3 | 180 | original - R10 |
| `wlc_bmac_set_ctrl_ePA` | 0x064ae5 | 60 | original - R10 |

## Overview

None of these functions is part of the bring-up proper; they are the MAC
layer's run-time services. In a session they appear as follows (counts of
the `hybrid.py` session on the object alone: attach, up, 9 channels,
watchdog, down, up, 36/80, watchdog, down, detach):

* **Attach** (`wlc_bmac_attach`, `wlc_attach`): `wlc_bmac_ampdu_set` (R8, no
  effect on rev 42), `wlc_bmac_set_btswitch(-1)` (R3, refused),
  `wlc_bmac_btc_wire_set(0)` and `wlc_bmac_btc_mode_set(8)` (R3: coexistence
  resolved to "off" on the emulated card), `si_register_intr_callback` with
  `sub_061268`/`sub_061285` (R9), `wlc_bmac_led_attach` (R4),
  `wlc_bmac_state_get`, `wlc_bmac_xmtfifo_sz_get`, `wlc_bmac_antsel_set`,
  `wlc_bmac_antsel_type_set` (R7, R8).
* **Up** (`wlc_up`): `wlc_bmac_led_hw_mask_init` (R4), `wlc_bmac_4331_epa_init`
  and `wlc_bmac_set_ctrl_ePA` (R10, nothing on the 4360), `wlc_bmac_seci_upd`
  (R3), and at the end `wlc_bmac_up_finish` -> `wlc_bmac_ifsctl_edcrs_set`
  (R1: energy detect for the bandwidth). `wlc_bmac_init` calls
  `wlc_bmac_btc_mode_set` again (R3).
* **Every channel change** (`wlc_set_chanspec` and the PHY): energy detect
  (R1, 11 calls), the per-rate power offsets in the microcode's rate table
  (R6: `wlc_update_txppr_offset` reads and writes one word of each of the 12
  legacy rates' entries: 132 directory reads), `wlc_bmac_mute` with the
  transmit FIFO suspend/resume (R2: 5 GHz channels of this regulatory domain
  are muted - FIFOs 1, 3, 0, 2 suspended -, 2.4 GHz unmuted), the channel to
  the Bluetooth side (R3 `wlc_bmac_update_bt_chanspec`, inert here), the TSF
  and CCA counters (R5, R7), `wlc_bmac_filter_war_upd` and
  `wlc_bmac_txant_set` (R8).
* **Watchdog** (once a second): `wlc_bmac_watchdog` (R7: FIFO error check,
  receive refill, PHY watchdog), the CCA counters (R5), the LEDs (R4).
* **Down/detach**: the LEDs off (R4), `wlc_bmac_led_hw_deinit`,
  `wlc_bmac_led_detach`.

The state they share with the open MAC layer is in "Data" below; the last
subsection of Data lists, per function, the fields and the calls out of
`wlc_bmac.c` (the interface).

## Data

### Constants

Chanspec (16 bit, `wlc_hw+0x11c` = `chanspec`, written by
`wlc_bmac_set_chanspec`; layout of the open header
`src/shared/bcmwifi/include/bcmwifi_channels.h`): bits 0..7 channel number,
bits 8..10 (mask 0x0700) the control sideband, bits 11..13 (mask 0x3800)
the bandwidth, bits 14..15 (0xc000) the band (0x0000 2.4 GHz, 0xc000 5
GHz). Bandwidth values used here: 0x1000 = 20 MHz, 0x1800 = 40 MHz, 0x2000
= 80 MHz (0x0000 = 5, 0x0800 = 10, 0x2800 = 160, 0x3000 = 80+80, 0x3800 =
2.5 MHz: no branch of their own). Sideband values: 0x000 the lowest 20 MHz
sub-channel carries the control channel, 0x100 the second lowest, 0x200 the
third, 0x300 the fourth (for 40 MHz only 0x000 "lower" and 0x100 "upper"
occur).

PHY type of a band state (`band+0x1c`, 16 bit; `wlc_hw+0xe8` points to the
current band state): 4 N, 6 LP, 7 HT, 8 SSN, 10 LCN, 11 AC. PHY revision:
`band+0x1e` (16 bit). `pi` of the band: `band+0x28`.

### Fields of `wlc_hw` used by this area

Names of bmac-init.md where it has them; the `struct bcm4360_mac` field of
`open/include/bcm4360/mac.h` that holds it already is in the last column
("-": not in the open state yet).

| Offset | Size | Name | R/W here | by | `bcm4360_mac` |
|---|---|---|---|---|---|
| 0x00 | 8 | `wlc` | R | R4, R7, R8, R9 (for `wlc+0x10` = `wl`, `wlc+0x550`, `wlc+0x690`, `pub`) | - (services) |
| 0x10 | 8 | `osh` | R | R4 (malloc) | - |
| 0x20 + 8 i | 8 | DMA handle of FIFO i | R | R2, R7 | `dma_fifos` (bit mask) |
| 0x84 | 4 | `corerev` | R | R1, R3, R5, R6, R8 | `mac_rev` |
| 0x8c | 4 | `boardflags` | R | R3 | `boardflags` |
| 0x90 | 4 | `boardflags2` | R | R3, R4 (bit 11) | `boardflags2` |
| 0xa4 | 4 | `machwcap` | R | R3, R7 | `machwcap` |
| 0xa8 | 4 | `machwcap_backup` (copy made at attach) | R | R3 (`btc_mode_set` step 3) | - |
| 0xae | 1 | `ucode_loaded` | W (0) | R3 (`btc_wire_set`) | `ucode_loaded` |
| 0xb0 | 8 | `btc` (coexistence state, R3) | R/W | R3 | `btc_valid`, `btc_flags`, `btc_gpio_mask`, `btc_gpio_out`, `btc_mode`; **wire and aa2g: -** |
| 0xb8 | 8 | `sih` | R | R3 (`+0x14`, `+0x18`, `+0x1c`, `+0x28`, `+0x30`, `+0x3c`, `+0x44`) | `ccrev`, `cccaps`, `cccaps_ext`, `boardtype`, `boardvendor`, `chip_id`; package: - |
| 0xc0 | 8 | `vars` | R | R4 (`led_attach`) | - (service `getvar`) |
| 0xd0 | 8 | `regs` | R | R1, R2, R3, R5, R7 | `hw` |
| 0xe8 | 8 | current band | R | R1, R2 (PHY type, rev), R3, R7, R8 (`pi`) | `band_phytype`, `band_phyrev` |
| 0x100 | 2 | `txant` | W | R8 | - |
| 0x10c | 1 | `up` | R | R3, R7, R8, R9 | `up` |
| 0x110 | 4 | `now` (watchdog ticks) | R/W | R7 | - |
| 0x11c | 2 | `chanspec` | R | R1 | `chanspec` |
| 0x150 | 8 | `xmtfifo_sz_tbl` | R | R8 | - |
| 0x164 | 1 | `suspended_fifos` | R/W | R2 | `suspended_fifos` |
| 0x170 | 4 | `wake_override` | (through A3/A4) | R2 | `wake_override` |
| 0x180 | 4 | `led_gpio_mask` | W | R4 | `led_gpio_mask` |
| 0x186 | 1 | `clk` | R | R10 | `clk` |
| 0x187 | 1 | `sbclk` | R | R4 | `sbclk` |
| 0x198 | 8 | `led` (LED state, R4) | R | R4 | - (services `led_entry`, `led_gpio_store` cover only C2) |
| 0x1a1 | 1 | `antsel_type` | W | R8 | `antsel_type` |
| 0x1a4 | 4 | `antsel_avail` (assigned) | W | R8 | - |
| 0x1bc | 1 | `btswitch` | W | R3 | - |

### Interface: calls out of `wlc_bmac.c`, per function

What each function needs beyond the 802.11 core's registers, the shared
memory (A8) and the open MAC primitives (A3, A4, A6, A7, A9 - all open in
`open/mac/`). "env" names an existing service of `mac_env.h`.

| Function | Outside calls | Upper-layer / glue state |
|---|---|---|
| `wlc_bmac_ifsctl_edcrs_set`, `sub_06212d` | `wlc_acphy_set_scramb_dyn_bw_en(pi, on)` | - |
| `sub_0622d9` | - | - |
| `wlc_bmac_tx_fifo_suspend` | DMA slot 0x20 (`dma64_txsuspend`) | - |
| `wlc_bmac_tx_fifo_resume` | DMA slot 0x28 (`dma64_txresume`) | - |
| `wlc_bmac_tx_fifo_suspended` | DMA slot 0x30 (`dma64_txsuspended`) -> bool | - |
| `wlc_bmac_btc_mode_set` | - (A6, A7, A9, `sub_062b79`) | - |
| `wlc_bmac_btc_wire_set`, `_wire_get`, `_mode_get` | - | - |
| `wlc_bmac_set_btswitch` | `wlc_phy_set_femctrl_bt_wlan_ovrd(pi, sw)`; other chips: `si_gpioout`, `si_gpioouten`, C11 | - |
| `wlc_bmac_update_bt_chanspec` | `si_eci_notify_bt(sih, mask, value, 1)` | - |
| `wlc_bmac_seci_upd` | `si_seci_upd(sih, 1)` | `wlc_btc_mode_get(wlc)` (the word `+0x0c` of `wlc+0x690`) |
| `sub_062b79` | - (A9) | - |
| `sub_06106b` | env `gpioouten`, env `gpiocontrol`, (`si_gpiopull`: chip 0x4313) | - |
| `wlc_bmac_led` | env `gpioout`, env `gpioouten` | - |
| `wlc_bmac_led_attach` | `osl_malloc`, `osl_memset`, `osl_malloced`, `osl_printf`, `osl_snprintf`, `osl_sysuptime`, env `getvar`, `bcm_strtoul`, `wl_init_timer(wl, fn, wlc, "led_blink")`, `osl_mfree` | `wlc+0x10` (`wl`) |
| `wlc_bmac_led_detach` | `wl_del_timer`, `wl_free_timer`, `osl_mfree` | `wl` |
| `wlc_bmac_led_hw_deinit` | C9 `wlc_bmac_xtal` (open), env `gpioout`, env `gpioouten`, env `gpioled` | - |
| `wlc_bmac_led_blink_event` | `wl_del_timer`, `wl_add_timer` | `wl` |
| `sub_061660` (timer) | C14 (open), `osl_sysuptime`, `wl_del_timer`, `wl_add_timer`, `wl_down` | `wlc+0x10`, `wlc+0x20` |
| `cca_stats_read`, `sub_061ad3` | - (R7 `read_tsf`) | - |
| `wlc_bmac_rate_shm_offset`, `stf_get/set` | - (the table `rate_info`) | - |
| `wlc_bmac_read_tsf` | - | - |
| `wlc_bmac_fifoerrors` | `wlc_fatal_error(wlc)` | the counters `wl_cnt_t` at the word `pub+0xa0` (+0x34, +0x80, +0xa8, +0xac, +0xb0) |
| `wlc_bmac_watchdog` | DMA slot 0xd8 (`dma_rxfill`) of FIFO 0, `wlc_phy_watchdog(pi)` | argument `wlc`; `wlc+0x20` |
| `wlc_bmac_state_get` | `wlc_phy_preamble_override_get(pi)` | - |
| `wlc_bmac_xmtfifo_sz_get`, `wlc_bmac_ampdu_set` | (`osl_memcpy` for revs 26/29) | - |
| `wlc_bmac_antsel_type_set` | `wlc_phy_antsel_type_set(pi, type)` | - |
| `wlc_bmac_antsel_set` | - | - |
| `wlc_bmac_filter_war_upd` | `wlc_phy_set_filt_war(pi, on)` | - |
| `wlc_bmac_txant_set`, `sub_062684` | - | the byte `+0xf4` of the structure at `wlc+0x550` |
| `sub_061268`, `sub_061285` | env `intrsoff`, env `intrsrestore` | - |
| `wlc_bmac_4331_epa_init`, `wlc_bmac_set_ctrl_ePA` | (other chips: `si_gpiopull`, `si_gpiocontrol`, `si_gpioout`, `si_gpioouten`, `si_chipcontrl_epa4331`) | - |

## Procedures

### R1. Energy detect: wlc_bmac_ifsctl_edcrs_set (.text+0x062340, 195 bytes, name original), sub_06212d (.text+0x06212d, 428 bytes, name assigned: wlc_bmac_ifsctl_ac_edcrs), sub_0622d9 (.text+0x0622d9, 103 bytes, name bmac-init: wlc_bmac_ifs_ctl_edcrs)

Purpose: set the energy-detect / carrier-sense ("EDCRS") control of the IFS
block of the MAC for the current bandwidth. On the AC-PHY the work is in
`sub_06212d`, on N (rev >= 3) and HT PHYs in `sub_0622d9`.

**`wlc_bmac_ifsctl_edcrs_set(wlc_hw, on)`** (`on`: u8, no result).

Reads: `band = wlc_hw+0xe8` (its PHY type `+0x1c`, PHY rev `+0x1e`),
`corerev` (`wlc_hw+0x84`), `chanspec` (`wlc_hw+0x11c`).

1. t = PHY type of the current band. If t = 4 (N) and `corerev` < 16:
   return. If t is not 4, 7 or 11: return.
2. If `on` = 0: `sub_0622d9(wlc_hw, 0x08, 0x08)`. Else, only for t = 4 with
   PHY rev <= 2: `sub_0622d9(wlc_hw, 0x08, 0x00)`; otherwise nothing in this
   step.
3. t is read again from the band state.
   * t = 11 (AC, **this card**): `sub_06212d(wlc_hw, 1)` - the constant 1,
     whatever `on` is. Return.
   * t = 7, or t = 4 with PHY rev >= 3: `sub_0622d9(wlc_hw, 0x38, v)` with
     v = 0x08 if `chanspec & 0x3800` = 0x1000 (20 MHz), else 0x38. Return.
   * otherwise: return.

On the 4360 (rev 42, AC-PHY) the net effect is `sub_06212d(wlc_hw, 1)`
whatever `on` is: step 2 calls `sub_0622d9` only when `on` = 0, and that
does nothing at revision 42.

Callers and their `on`: `wlc_bmac_up_finish` (bmac-init C4) and
`wlc_set_chanspec` (`wlc.c`, after every channel change):
`on` = (PHY type of the upper layer's band, 16 bit at `+0x08` of the
structure `wlc+0x40`, is 7) = 0 here; `wlc_disassociate_client`: 1;
`wlc_assocresp_client`: bit 0 of a 16-bit word at `+0x0a` of the association
response it handles; the iovar dispatcher `wlc_bmac_iovars_dispatch` (see
the end of this section).

**`sub_06212d(wlc_hw, on)`** (`on`: u8): the AC-PHY variant.

Reads `chanspec` (`wlc_hw+0x11c`), `regs` (`wlc_hw+0xd0`), `corerev`, the
current band's `pi`. Let bw = `chanspec & 0x3800`, sb = `chanspec & 0x0700`.

1. v = 0x0f0f if `on` != 0, else 0x000f.
2. By bw:
   * **0x1000 (20 MHz)**: r = `D11(0x6c6)` (16 bit read); n = `(r & 0xf0f0)
     | (v & 0x0f0f)`; `D11(0x6c6) = n` (16 bit); `SHM(0x5a) = n`
     (`wlc_bmac_write_shm`). Go to step 4.
   * **0x1800 (40 MHz)**: v = v & 0x0303. r = `D11(0x6c8)`; `D11(0x6c8) =
     (r & 0xf0f0) | c` with c = 2 if sb = 0x000 (control channel in the
     lower half), else 1. Go to step 3.
   * **0x2000 (80 MHz)**: v = v & 0x0f0f. r = `D11(0x6c8)`; `D11(0x6c8) =
     (r & 0xf0f0) | c` with c = 0x0c if sb is 0x000 or 0x100 (control channel
     in the lower 40 MHz), else 0x03. Go to step 3.
   * any other bw: go to step 4 (no register access).
3. (40 and 80 MHz only.) If `corerev` <= 40: `SHM(0x5a) = v` (not on this
   card, rev 42). Then r = `D11(0x6c6)`; `D11(0x6c6) = (r & 0xf0f0) | v`.
4. `wlc_acphy_set_scramb_dyn_bw_en(pi, on)` (acphy-init.md H13) with the
   `pi` of the current band (`band+0x28`), in every case.

So, with `on` = 1 (the only value of the normal calls), the writes are:

| bandwidth, sideband | `D11(0x6c8)` low nibble | `D11(0x6c6)` | `SHM(0x5a)` (rev 42) |
|---|---|---|---|
| 20 MHz | not touched | `(old & 0xf0f0) \| 0x0f0f` | the same value as `D11(0x6c6)` |
| 40 MHz, sb 0x000 | 2 | `(old & 0xf0f0) \| 0x0303` | not written |
| 40 MHz, sb 0x100 | 1 | `(old & 0xf0f0) \| 0x0303` | not written |
| 80 MHz, sb 0x000/0x100 | 0xc | `(old & 0xf0f0) \| 0x0f0f` | not written |
| 80 MHz, sb 0x200/0x300 | 3 | `(old & 0xf0f0) \| 0x0f0f` | not written |
| other | - | - | - |

`D11(0x6c8)` keeps bits 4..7 and 12..15 of its old value; bits 8..11 become
0. With `on` = 0 (iovar only) the masks are the same and v is 0x000f (20 and
80 MHz) or 0x0003 (40 MHz). Purpose of the bits of `D11(0x6c6)`/`D11(0x6c8)`:
by the code's use, energy-detect enables per 20 MHz sub-channel (0x6c6) and
the position of the primary channel (0x6c8) (unverified interpretation).

**`sub_0622d9(wlc_hw, mask, val)`** (u16 each): only for `corerev` < 40
(nothing at all on rev 42): r = `D11(0x69e)`; n = `(r & ~mask) | val`;
`D11(0x69e) = n`; `SHM(0x5a) = n`.

**The iovar dispatcher** (`wlc_bmac_iovars_dispatch`, belongs to the
upper-layer specification; one line each): id 0x79 ("set") - refused (-23)
unless PHY type 4 with `corerev` >= 16, 7 or 11; value != 0:
`wlc_bmac_ifsctl_edcrs_set(wlc_hw, upper band PHY type == 7)`; value 0:
`sub_06212d(wlc_hw, 0)` for the AC-PHY, `sub_0622d9(wlc_hw, 0x38, 0)` for
the others. Id 0x78 ("get"): same refusal; reads `SHM(0x5a)`; AC: result =
`(SHM(0x5a) & 0x0f00) != 0`; HT: `(& 0x38) != 0`; N: bit 3.

Fields and calls: reads `wlc_hw+0xe8` (band: `+0x1c`, `+0x1e`, `+0x28`),
`+0x84`, `+0x11c`, `+0xd0`; writes no field. Outside calls:
`wlc_acphy_set_scramb_dyn_bw_en` (PHY), `wlc_bmac_write_shm` (A8, open).

Observed (session of `hybrid.py` and soak steps on the object alone, script
`re-out\analysis\bmac-run\observe.py`): 11 calls in the session (2 from
`up_finish`, 9 from `wlc_set_chanspec`), every one `on` = 0 at the outer
function and 1 at `sub_06212d`. 20 MHz (up, 6, 11, 36, 165): R 0x6c6, W 0x6c6
= 0x0f0f, W `SHM(0x5a)` = 0x0f0f. 40 MHz: 36l/36/40 (sb 0) W 0x6c8 = 2, W
0x6c6 = 0x0303; 40u, 161u (sb 0x100) W 0x6c8 = 1; 157/40 (sb 0) 2. 80 MHz:
36/80, 40/80, 149/80 W 0x6c8 = 0xc; 44/80, 48/80, 157/80, 161/80 W 0x6c8 = 3;
W 0x6c6 = 0x0f0f. No `SHM(0x5a)` write at 40/80 MHz. Then the PHY's
`PHY(0x042)` read-modify-write. The `on` = 0 variant and the "other
bandwidth" path: see Verification (direct calls).

### R2. Transmit FIFO suspend and resume: wlc_bmac_tx_fifo_suspend (.text+0x063f93, 256 bytes), wlc_bmac_tx_fifo_resume (.text+0x06061c, 81 bytes), wlc_bmac_tx_fifo_suspended (.text+0x0604e3, 73 bytes), names original

State: `suspended_fifos` (`wlc_hw+0x164`, u8, bit i = transmit FIFO i is
suspended by the driver; cleared by the core reset A14), the DMA handles
`wlc_hw+0x20 + 8*i` (i = 0..5; on this card FIFOs 0..3 have an engine, 4 and
5 are 0), `wake_override` bit 0x08 ("transmit FIFO", A3/A4).

**`wlc_bmac_tx_fifo_suspend(wlc_hw, fifo)`** (`fifo`: u32, no result):

1. bit = `(1 << fifo)` truncated to 8 bits. If `suspended_fifos & bit` =
   bit (already suspended): return, no access.
2. If `suspended_fifos` = 0: `wlc_ucode_wake_override_set(wlc_hw, 0x08)`
   (A3).
3. `suspended_fifos |= bit`.
4. If FIFO `fifo` has no DMA engine: return.
5. e = bit 0 (EN_MAC) of a 32-bit read of `D11(0x120)`.
6. If the current band's PHY type is one of 4, 6, 7, 8, 10, 11 (always on
   this card) and e = 1: `wlc_bmac_suspend_mac_and_wait(wlc_hw)` (A6).
7. DMA slot 0x20 (`dma64_txsuspend`, bmac-data P13) on the engine of the
   FIFO: read its control register, write it back with bit 1 set.
8. The PHY type is read again; if it is in the same list and e = 1:
   `wlc_bmac_enable_mac(wlc_hw)` (A7).

**`wlc_bmac_tx_fifo_resume(wlc_hw, fifo)`** (no result):

1. If FIFO `fifo` has a DMA engine: DMA slot 0x28 (`dma64_txresume`, P14):
   control register read, written back with bit 1 cleared.
2. If `suspended_fifos` != 0: clear bit `fifo` in it (a rotate of 0xfe by
   `fifo`, i.e. only the bit itself is cleared); if `suspended_fifos` is 0
   now: `wlc_ucode_wake_override_clear(wlc_hw, 0x08)` (A4).

Note the order: the engine is resumed before the bookkeeping, and the engine
is resumed even if the bit was not set. Resume does not touch the MAC (no
suspend/enable).

**`wlc_bmac_tx_fifo_suspended(wlc_hw, fifo)`** -> bool:

1. DMA slot 0x30 (`dma64_txsuspended`, P15) on the engine of the FIFO
   **without** checking that it exists (a FIFO without an engine would crash
   the object; callers pass FIFOs that have one). If it returns 0: result 0.
2. Else read `D11(0x150)` (32 bit; brcmsmac's name `chnstatus`); result 1 if
   bit `fifo` of it is **clear** (the channel is no longer busy), else 0.

The register of a FIFO's transmit channel that slots 0x20/0x28/0x30 use is its
control register `D11(0x200 + 0x40 * fifo)` (bmac-data D2): FIFO 0 at 0x200,
1 at 0x240, 2 at 0x280, 3 at 0x2c0.

Callers: `wlc_bmac_mute` (E4: suspend 1, 3, 0, 2 on mute, resume on unmute),
`wlc_tx_suspend`/`wlc_tx_resume` (`wlc.c`), `sub_167df8`; suspended:
`wlc_dpc`, `wlc_tx_suspended`, `wlc_txstop_intr`.

Fields and calls: reads/writes `wlc_hw+0x164`; reads `wlc_hw+0x20..0x48`
(DMA handles), `+0xd0`, `+0xe8` (band `+0x1c`). Calls A3, A4, A6, A7 (open)
and the DMA slots 0x20, 0x28, 0x30 (open DMA layer).

Observed (session): 28 suspend calls, all from `wlc_bmac_mute`, 8 resumes.
The first mute (step 36 and the 36/80 after the second up): for FIFOs 1, 3,
0, 2 in this order R `D11(0x120)` = 0x44020402 (EN_MAC clear: the channel
change has suspended the MAC, so no A6/A7), R/W the control register
0x240/0x2c0/0x200/0x280 = 0x03780841 -> 0x03780843; no `D11(0x120)` write
from A3 (the wake override already had other bits). Later mutes while the
FIFOs were still suspended: no access at all (step 1). Unmute (steps 6, 11):
resume FIFOs 1, 3, 0, 2: R/W 0x240 ... = 0x03780841 (bit 1 cleared).
`wlc_bmac_tx_fifo_suspended`: not called in the session (read only; see
Verification for direct calls).

### R3. Bluetooth coexistence: wlc_bmac_btc_mode_set (.text+0x063b1b, 783 bytes), wlc_bmac_btc_wire_set (.text+0x06003c, 350 bytes), wlc_bmac_btc_wire_get (.text+0x06002c, 16 bytes), wlc_bmac_btc_mode_get (.text+0x06001d, 15 bytes), wlc_bmac_set_btswitch (.text+0x066f30, 294 bytes), wlc_bmac_update_bt_chanspec (.text+0x060317, 312 bytes), wlc_bmac_seci_upd (.text+0x0602c2, 85 bytes), names original; sub_062b79 (.text+0x062b79, 325 bytes, bmac-init's name wlc_bmac_btc_flags_to_mhf), sub_06106b (.text+0x06106b, 168 bytes, bmac-init's name wlc_bmac_btc_gpio_enable)

This section replaces bmac-init.md E10 (see "Corrections").

**State.** `btc` = the 32-byte block whose address is `wlc_hw+0xb0`
(allocated by `wlc_hw_attach`):

| `btc`+ | size | name | meaning |
|---|---|---|---|
| 0x00 | 4 | mode | coexistence mode, resolved (0 = off) |
| 0x04 | 4 | wire | interface type: 2 = 2-wire, 3 = 3-wire, 4 = 4-wire (signed compares) |
| 0x08 | 2 | flags | coexistence flag bits (set by `btc_mode_set`, by `wlc_bmac_init` from the variable `btc_flags`, B1) |
| 0x0c | 4 | gpio_mask | GPIO lines of the coexistence interface |
| 0x10 | 4 | gpio_out | GPIO lines driven by the WLAN side |
| 0x18 | 1 | aa2g | the SROM variable `aa2g` (antennas available on 2.4 GHz), stored by `wlc_bmac_attach` (getintvar, truncated to 8 bits); bmac-init called it `srom_btc_val` |
| 0x1a | 2 | bt_shm_base | 2 * `SHM(0x92)` (B1) |

**Inputs common to several functions.** `corerev` (`wlc_hw+0x84`),
`boardflags` (`wlc_hw+0x8c`), `boardflags2` (`wlc_hw+0x90`), `machwcap`
(`wlc_hw+0xa4`, MAC capabilities, `D11(0x15c)`), `machwcap_backup`
(`wlc_hw+0xa8`, its copy made at attach), `up` (`wlc_hw+0x10c`), and of
`sih` (`wlc_hw+0xb8`): `cccaps` (`sih+0x18`), `cccaps_ext` (`sih+0x1c`),
`ccrev` (`sih+0x14`), board type (`sih+0x28`), board vendor (`sih+0x30`),
chip id (`sih+0x3c`). Predicates used below:

* P_eci = `corerev` >= 15 and `cccaps` bit 29 (0x20000000) set and
  `cccaps_ext` bit 0 clear;
* P_seci = `boardflags` bit 0 set and `boardflags2` bit 7 (0x80) clear and
  `cccaps_ext & 5` != 0;
* P_cap = `machwcap` bit 29 (0x20000000) set;
* BTC_HW = (P_eci or P_seci) and P_cap ("the coexistence hardware is there").

On the emulated card BTC_HW is false (`cccaps` has no bit 29, `cccaps_ext` =
0, `machwcap` reads 0); on the real card it depends on `D11(0x15c)` and the
ChipCommon capabilities (open question Q1).

**`wlc_bmac_btc_mode_set(wlc_hw, mode)`** (`mode`: s32) -> s32:

1. `mode` > 8 (signed): return -2.
2. Resolve: `mode` = 8 ("auto"):
   * If BTC_HW is false and `boardflags2` bit 7 is clear: `mode` = 0 (go to
     step 4; the check of step 3 is skipped).
   * Else (BTC_HW, or `boardflags2` bit 7 set): if `btc.aa2g` <= 2: `mode` =
     1. Else by chip id: 0xa886: `mode` = 1 if board type is 0xe078, else 3;
     0xa8dc: 1; any other chip (the 4360): 5. Go to step 3.
   `mode` = 0: go to step 4. Any other `mode` (also negative): step 3.
3. If `btc.wire` > 2 and `corerev` > 12 and bit 29 of `machwcap_backup` is
   clear: return -3 (nothing changed).
4. v1 = v2 = v3 = 0 (u16 each; host flag values). `btc.flags` = 0. e = 0;
   if `up`: e = bit 0 (EN_MAC) of a 32-bit read of `D11(0x120)`.
5. Build the values:
   * `mode` = 0: nothing more (v1 stays 0).
   * `mode` != 0: v1 = 0x0010. Then:
     * `btc.wire` = 2: if `boardflags` bit 14 (0x4000) is set: v2 = 0x0100,
       `btc.gpio_mask` = 0x30, `btc.gpio_out` = 0x20; else v2 stays 0 and
       the GPIO fields are not touched.
     * `btc.wire` != 2 and `corerev` <= 12: `btc.flags |= 0x000c`,
       `btc.gpio_mask` = 0x870, `btc.gpio_out` = 0x20.
     * `btc.wire` != 2 and `corerev` > 12 (**this card**):
       * by mode: 5: `flags |= 0x0008`; 3: `flags |= 0x0080 | 0x0008`
         (chip 0x4331 with `boardflags` bit 22: `flags |= 0x0100 | 0x0008`
         instead); 4: `flags |= 0x0100 | 0x0008`; any other mode: `flags
         |= 0x0014`.
       * then: if BTC_HW: `flags |= 0x0040`; else if `btc.wire` = 4: v3 =
         0x2000; else `flags |= 0x0001`.
6. `btc.mode` = `mode`.
7. If e and `up` (read again): `wlc_bmac_suspend_mac_and_wait` (A6).
8. `wlc_bmac_mhf(wlc_hw, 0, 0x0010, v1, 2)`, `wlc_bmac_mhf(wlc_hw, 1,
   0x0100, v2, 2)`, `wlc_bmac_mhf(wlc_hw, 2, 0x2000, v3, 2)` (A9: host flags
   1, 2, 3; `bands` = 2 is the 2.4 GHz band only - the shared memory is
   written only if the 2.4 GHz band is current and the clock is on).
9. `sub_062b79(wlc_hw)` (below).
10. If e and `up`: `wlc_bmac_enable_mac` (A7). Return 0.

Callers: `wlc_bmac_attach` (D2 step 14) with 8 for the AC-PHY (1 for an
LP-PHY, 5 for LCN-PHY rev 3), `wlc_bmac_init` (B1 step 7) with `btc.mode`,
the upper layer's `wlc_btc_mode_set`, `sub_07a769`.

**`wlc_bmac_btc_wire_set(wlc_hw, wire)`** (`wire`: s32) -> s32:

1. `wire` > 4 (signed): return -2.
2. `wire` = 0 ("auto"): `btc.wire` = 3 if BTC_HW; else, if `boardflags2`
   bit 7 is set: 3 if `boardflags2` bit 25 (0x02000000) is set, else 4;
   else 2. Then (MAC rev 12 on Apple board types 0x90/0x8b only) 3.
   Any other `wire`: `btc.wire` = `wire`.
3. `ucode_loaded` (`wlc_hw+0xae`) = 0 (always: the next `wlc_bmac_init`
   downloads the microcode again).
4. If `btc.wire` <= 2: return 0.
5. g by chip id: 0xa8d8 and 0xa99d: 0xe0 if `boardflags` bit 22, else
   0x20; 0xa8d9: 0xe0; 0x4312: 0x10; 0x4313: 0x60; **any other (the 4360):
   0**. `btc.gpio_out` = g, `btc.gpio_mask` = g (both are overwritten, so a
   4360 ends with 0 in both). Return 0.

Callers: `wlc_bmac_attach` (D2 step 14, `wire` = 0), the upper layer's
`wlc_btc_wire_set`.

**`wlc_bmac_btc_wire_get(wlc_hw)`** -> `btc.wire`; **`wlc_bmac_btc_mode_get`**
(A17) -> `btc.mode`. No access.

**`wlc_bmac_set_btswitch(wlc_hw, sw)`** (`sw`: s8) -> s32:

* Chips 0xa9a7/0x4331 on board types 0x10e, 0xe4, 0x5c6, 0xef, 0x10f (not this
  card): GPIO 4 (mask 0x10) as the switch, through `wlc_bmac_set_ctrl_bt_shd0`,
  `si_gpioout`, `si_gpioouten`; then as below from "store".
* Otherwise (**this card**): if the current band's PHY type is not 11: return
  -23 (`btswitch` not changed). If `up` = 0: return -4. Else
  `wlc_phy_set_femctrl_bt_wlan_ovrd(pi, sw)` (PHY: stores `sw` in
  `pi_ac+0x32e` and, inside a MAC suspend/enable, sets bits 0/1 of
  `PHY(0x418)`: `sw` = 1: bit 0 = 1 then bit 1 = 1; `sw` = 0: bit 0 = 0, then
  bit 1 = 1; other (-1): bit 1 = 0, then bit 0 = 0 - the "no override" case;
  all read-modify-writes; read from the PHY code, not observed).
* Store: `btswitch` (`wlc_hw+0x1bc`) = `sw`; return 0.

Callers: `wlc_bmac_attach` (D2 step 13.4: with -1 when the band's PHY type
is not 11 - tested right after `wlc_phy_attach`, before
`wlc_phy_get_phyversion` fills the type in, so it is still 0 and the call
is made for both bands and refused with -23 both times: observed twice),
`wlc_up` (C1 step 6.2, when `btswitch` is not 0xff), the iovar dispatcher
(id 0x6f; refused with -4 for the AC-PHY while down).

**`wlc_bmac_update_bt_chanspec(wlc_hw, chanspec, scanning, p4)`**
(`chanspec`: u16, `scanning`, `p4`: u8; no result): tell the Bluetooth side
the WLAN channel through the ECI (`si_eci_notify_bt(sih, mask, value, 1)`,
spec `chip`).

1. Return unless (P_eci or P_seci) and `scanning` = 0 and P_cap and `p4` =
   0. (On the emulated card: always return, no access.)
2. Let H = (`ccrev` >= 35); M = 0x5555f000 if H, else 0x0000000f.
3. By `chanspec`:
   * `chanspec` = 0, or its band bits (0xc000) are neither 0x0000 nor
     0xc000: `si_eci_notify_bt(sih, M, 0, 1)`.
   * band 0x0000 (2.4 GHz): first `si_eci_notify_bt(sih, M, (chanspec &
     0xff) << (H ? 12 : 0), 1)` (the channel number). Then, with b40 =
     (`chanspec & 0x3800` = 0x1800, 40 MHz): `si_eci_notify_bt(sih, H ?
     0x55550e00 : 0x70, H ? (b40 ? 0xa00 : 0x200) : (b40 ? 0x50 : 0x10), 1)`.
   * band 0xc000 (5 GHz): `si_eci_notify_bt(sih, M, H ? 0xf000 : 0xf, 1)`.

The PHY calls it through `wlapi_update_bt_chanspec(shim, chanspec, scanning,
p4)` (bmac-init A18); in the session `scanning` and `p4` were 0 every time.

**`wlc_bmac_seci_upd(wlc_hw)`** (result: none - the register is left as the
callee left it and `wlc_up` ignores it): if `boardflags` bit 0 set,
`boardflags2` bit 7 clear, `cccaps_ext` bit 0 set and P_cap: if the upper
layer's coexistence mode (`wlc_btc_mode_get(wlc)` = the 32-bit word `+0x0c`
of the structure `wlc+0x690`) is not 0: `si_seci_upd(sih, 1)` (spec `chip`).
Caller: `wlc_up` (C1 step 6.5).

**`sub_062b79(wlc_hw)`** (bmac-init E10 is correct; restated): f =
`btc.flags`.

1. `wlc_bmac_mhf(wlc_hw, 1, 0x0200, (f & 1) ? 0x0200 : 0, 2)`.
2. a = OR of the masks of `btc_ucode_flags` entries k = 1..6 for every set
   bit k of f. The table (Broadcom's data, 9 entries of two 16-bit words:
   host flag index, mask; entry k belongs to flag bit k):

   | k | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
   |---|---|---|---|---|---|---|---|---|---|
   | index | 1 | 2 | 2 | 2 | 2 | 2 | 2 | 4 | 4 |
   | mask | 0x0200 | 0x0004 | 0x0008 | 0x0100 | 0x0200 | 0x0400 | 0x1000 | 0x0002 | 0x0004 |

   (The code uses only the masks; entries 0, 7, 8 appear as constants in
   steps 1 and 4.)
3. `wlc_bmac_mhf(wlc_hw, 2, 0x1504, a & 0xfdf7, 2)`; if `(a & 0xedf7)` = 0:
   also `wlc_bmac_mhf(wlc_hw, 2, 0x1000, a & 0x1000, 0)` (`bands` 0 = the
   current band).
4. `wlc_bmac_mhf(wlc_hw, 4, 0x0006, (f bit 7 ? 0x0002 : 0) | (f bit 8 ?
   0x0004 : 0), 2)`.
5. If board vendor = 0x106b and (board type is 0x10f, 0xef, 0xf4 or 0x10e,
   or chip id = 0x4360): `wlc_bmac_mhf(wlc_hw, 4, 0x0001, 0x0001, 2)`.

With f = 0 on the Apple 4360 the calls are: (1, 0x200, 0, 2), (2, 0x1504, 0,
2), (2, 0x1000, 0, 0), (4, 6, 0, 2), (4, 1, 1, 2).

**`sub_06106b(wlc_hw)`** (bmac-init E10 is correct): m = `btc.gpio_mask`;
if m = 0 (always on the 4360): return. Else `D11(0x49e) = D11(0x49e) |
btc.gpio_out` (16 bit read, write), `si_gpioouten(sih, ~btc.gpio_out & m, 0,
0)`, chip 0x4313: `si_gpiopull(sih, 1, m, 0)`, `si_gpiocontrol(sih, m, m,
0)`. Called by `wlc_bmac_bsinit` (B10) only: 3 calls in the session, no
access.

Fields and calls of the section: `btc` +0x00/+0x04/+0x08/+0x0c/+0x10/+0x18;
`wlc_hw` +0x84, +0x8c, +0x90, +0xa4, +0xa8, +0xae (W), +0x10c, +0x1bc (W),
+0xd0, +0xe8 (band PHY type, `pi`), `sih` +0x14, +0x18, +0x1c, +0x28, +0x30,
+0x3c; upper layer `wlc+0x690` (+0x0c). Outside calls: `si_eci_notify_bt`,
`si_seci_upd`, `si_gpioout`/`si_gpioouten`/`si_gpiocontrol`/`si_gpiopull`
(other chips or GPIO mask != 0 only), `wlc_phy_set_femctrl_bt_wlan_ovrd`,
`wlc_btc_mode_get` (upper layer); inside: A6, A7, A9 (open).

Observed (session): `btc_mode_set` 3 calls (attach, `mode` 8 -> resolved 0,
result 0; `wlc_bmac_init` twice with 0), no access (the host-flag writes go to
the 2.4 GHz copies while the clock is off at attach, and in `wlc_bmac_init`
they are part of B1's trace); `btc_wire_set` once (attach, 0 -> 2:
`wlc_bmac_btc_wire_get` returned 2 to `wlc_init` twice); `set_btswitch`
twice from attach, -1 -> -23; `update_bt_chanspec` 15 calls (chanspec 0 at
attach/down, the channel's chanspec otherwise), no access; `seci_upd` twice
from `wlc_up`, no access. The resolutions for BTC_HW true and the other
modes/wires were checked by direct calls (Verification).

### R4. LEDs: wlc_bmac_led (.text+0x060e32, 129 bytes), wlc_bmac_led_attach (.text+0x0614e0, 384 bytes), wlc_bmac_led_detach (.text+0x061463, 125 bytes), wlc_bmac_led_hw_deinit (.text+0x0634a5, 127 bytes), wlc_bmac_led_hw_mask_init (.text+0x05ffbc, 12 bytes), names original; the blink engine: wlc_bmac_led_set (.text+0x05ffc8, 24), wlc_bmac_blink_sync (.text+0x05ffe0, 43), wlc_bmac_led_blink_event (.text+0x0612a0, 200), wlc_bmac_led_blink (.text+0x061368, 251), names original, sub_061660 (.text+0x061660, 554 bytes, name assigned: wlc_bmac_led_timer)

The LED policy (which LED shows what) is in the upper layer `wlc_led.c`; this
layer keeps the per-pin polarity and blink timing and drives the ChipCommon
GPIOs (`si_gpioout`, `si_gpioouten`, `si_gpioled`, spec `chip`).

**The LED state** (0x328 bytes, allocated by `wlc_bmac_led_attach`, its
address in `wlc_hw+0x198`; L below):

| L+ | size | meaning |
|---|---|---|
| 0x000 | 8 | `wlc_hw` (back pointer) |
| 0x008 + 24 i | 24 | entry i, i = 0..31 (one per GPIO pin): |
| ... +0x00 | 4 | pin: the GPIO number (= i, set at attach and never changed) |
| ... +0x04 | 1 | activehi: 1 = the LED is on when the pin is high (default), 0 = active low |
| ... +0x08 | 4 | on time of a blinking LED, ms (0 = not blinking) |
| ... +0x0c | 4 | off time, ms |
| ... +0x10 | 4 | time stamp of the last toggle (`osl_sysuptime`, ms) |
| ... +0x14 | 1 | phase: 1 = in the off phase (or not started), 0 = in the on phase |
| ... +0x15 | 1 | restart: set to force the LED on at the next timer tick |
| 0x308 | 4 | gpio_val: the last value returned by `si_gpioout` / `si_gpioouten` (the register's new value) |
| 0x30c | 4 | gpio_mask: the GPIO lines of the LEDs, stored by `wlc_bmac_hw_up` (bmac-init C2 step 9) |
| 0x310 | 4 | period of the blink timer, ms (>= 10 once `wlc_bmac_led_blink` ran; 0 after attach) |
| 0x318 | 8 | the blink timer (`wl_init_timer`) |
| 0x320 | 1 | the timer is armed |
| 0x321 | 1 | the timer is armed as a one-shot (an early wake-up), not periodic |

Bytes 0x324..0x327 and the gaps are unused (zero from the `memset`).
`wlc_hw+0x180` (`led_gpio_mask`, 32 bit) is the GPIO mask of the LEDs, set by
`wlc_bmac_led_hw_mask_init` and used by `wlc_bmac_hw_up` (C2 step 9).

**`wlc_bmac_led_attach(wlc_hw)`** -> L or NULL. Called by `wlc_bmac_attach`
(D2 step 15; NULL makes the attach fail with 0x17).

1. L = `osl_malloc(osh, 0x328)` (`osh` = `wlc_hw+0x10`). NULL: print "out of
   memory", return NULL. Zero all 0x328 bytes.
2. For i = 0..31: pin = i, activehi = 1, stamp = `osl_sysuptime()`, phase = 1
   (in this order; one `osl_sysuptime` call per entry).
3. For i = 0..31: look up the variable `ledbh<i>` (decimal, `getvar` on
   `wlc_hw+0xc0`); if it does not exist, `wl0gpio<i>`. If one exists: v =
   `bcm_strtoul(value, NULL, 0)`; if `(v & 0x7f)` <= 0x18 (a known LED
   behaviour): pin = i (again) and activehi = 1 - bit 7 of v (bit 7 = "active
   low"). The behaviour number itself is not kept here.
4. L+0 = `wlc_hw`. L+0x318 = `wl_init_timer(wl, sub_061660, wlc, "led_blink")`
   (`wl` = the word `wlc+0x10`, `wlc` = `wlc_hw+0x00`). NULL: print a message,
   free L, return NULL.
5. Return L.

**`wlc_bmac_led_detach(wlc_hw)`** -> bool. L = `wlc_hw+0x198`; if L is NULL:
return 0. If L+0x318 (timer) is not NULL: d = `wl_del_timer(wl, timer)`,
`wl_free_timer(wl, timer)`, L+0x318 = NULL; result = (d = 0). Free L (0x328
bytes). Called by `wlc_bmac_detach` (D4; it does not clear `wlc_hw+0x198`
itself - D4's `wlc_hw_detach` frees `wlc_hw`).

**`wlc_bmac_led_hw_mask_init(wlc_hw, mask)`**: `led_gpio_mask`
(`wlc_hw+0x180`) = `mask` (u32). Called by `wlc_led_init` (from `wlc_up`
before `wlc_bmac_hw_up`): 7 in the session (GPIOs 0, 1, 2).

**`wlc_bmac_led(wlc_hw, mask, val, activehi)`** (`mask`, `val`: u32,
`activehi`: u8; no result - callers ignore `rax`):

1. If `sbclk` (`wlc_hw+0x187`) = 0: return (no access).
2. If `activehi` = 0: val = ~val & mask.
3. If `boardflags2` bit 11 (0x800) is clear (LEDs driven by the output
   value; **the emulated card**): if `((L.gpio_val ^ val) & mask)` = 0:
   return (nothing changes); else r = `si_gpioout(sih, mask, val, 0)`.
4. Else (LEDs driven by the output enable): if `activehi` = 0 and
   `((val ^ L.gpio_val) & mask)` != 0: return; if `activehi` != 0 and it is
   0: return. r = `si_gpioouten(sih, mask, e, 0)` with e = `mask` if the
   **original** `val` (before step 2) equals `mask`, else 0.
5. L.gpio_val (L+0x308) = r.

**`wlc_bmac_led_hw_deinit(wlc_hw, mask)`**: if `sbclk` = 0:
`wlc_bmac_xtal(wlc_hw, 1)` first (C9). If `sih` is not NULL:
`si_gpioout(sih, mask, 0, 0)`, `si_gpioouten(sih, mask, 0, 0)`,
`si_gpioled(sih, mask, 0)`. If it switched the crystal on:
`wlc_bmac_xtal(wlc_hw, 0)`. Called by `wlc_led_deinit` with the LED mask.

**`wlc_bmac_led_set(wlc_hw, i, activehi)`** (`i`: s32, `activehi`: u8):
entry i activehi = `activehi`. Caller: `wlc_led_set`.

**`wlc_bmac_blink_sync(wlc_hw, pins)`** (u32): for i = 0..31 with bit i of
`pins` set: entry i restart (+0x15) = 1.

**`wlc_bmac_led_blink(wlc_hw, i, on_ms, off_ms)`** (`i` s32, `on_ms`,
`off_ms` u16):

1. Entry i: on time (+0x08) = `on_ms`, off time (+0x0c) = `off_ms`.
2. For every entry (0..31): if its on and off times are both 0: restart =
   1; else g_k = gcd(on, off) (Euclid; gcd(x, 0) = x) is collected.
3. p = gcd of all g_k (1000 if no entry blinks); period (L+0x310) = max(p,
   10).
4. If some entry blinks and the timer is armed (L+0x320) and not a one-shot
   (L+0x321 = 0): `wlc_bmac_led_blink_event(wlc_hw, 0)` then
   `wlc_bmac_led_blink_event(wlc_hw, 1)` (restart with the new period).

**`wlc_bmac_led_blink_event(wlc_hw, on)`** (`on` u8) -> s32:

* `on` != 0: `wl_del_timer(wl, timer)`, `wl_add_timer(wl, timer, period, 1)`
  (periodic), armed = 1; return 0.
* `on` = 0: if `wl_del_timer(wl, timer)` returns 0: return 1. Else armed =
  0; for every entry whose on or off time is not 0: `wlc_bmac_led(wlc_hw, 1
  << pin, 0, activehi)` (LED off) and restart = 1. Return 0.

**`sub_061660(wlc)`** (the timer function; name assigned:
`wlc_bmac_led_timer`): `wlc_hw` = `wlc+0x20`.

1. If `wlc_hw_deviceremoved(wlc_hw)` (bmac-init C14): `wl_down(wl)`, return.
2. now = `osl_sysuptime()`; next = 0.
3. For each entry with on or off time != 0: el = now - stamp (32-bit
   unsigned), div(t) = 10 if t <= 1000 else 20.
   * If phase or restart is not 0 (the 16-bit word +0x14 != 0; the LED is
     off or must restart): t = off time. If el > t: turn it on. Else rem = t
     - el; if rem < period: if rem < t / div(t) or rem <= 9: turn it on now
     if (period - rem) > period / 10; otherwise, if rem > 9 and rem >= t /
     div(t): next = rem if next = 0 or rem < next. If the LED was not turned
     on by this and its restart byte is set: turn it on.
     "Turn on" = `wlc_bmac_led(wlc_hw, 1 << pin, 1 << pin, activehi)`, phase
     = 0, stamp = now, restart = 0.
   * Else (on phase): t = on time. If el > t: turn it off =
     `wlc_bmac_led(wlc_hw, 1 << pin, 0, activehi)`, phase = 1, stamp = now.
     Else rem = t - el; if rem < period: if rem < t / div(t) or rem <= 9:
     turn it off if (period - rem) > period / 10; else next = rem if next =
     0, else min(next, rem).
4. If next != 0: `wl_del_timer`, `wl_add_timer(wl, timer, next, 0)`
   (one-shot), armed = 1, one-shot = 1. Else if one-shot is set:
   `wlc_bmac_led_blink_event(wlc_hw, 1)`, armed = 1, one-shot = 0.

(The blink engine is read only: no LED blinks in the session or the soak.)

Fields and calls of the section: `wlc_hw` +0x00 (`wlc`, for `wlc+0x10` =
`wl` and `wlc+0x20`), +0x10 (`osh`), +0x18 (unit, messages), +0x91
(`boardflags2` bit 11), +0xb8 (`sih`), +0xc0 (`vars`), +0x180 (W), +0x187,
+0x198 (the LED state). Outside calls: `si_gpioout`, `si_gpioouten`,
`si_gpioled`, `osl_malloc`/`osl_mfree`/`osl_memset`/`osl_malloced`/
`osl_printf`/`osl_snprintf`/`osl_sysuptime`, `getvar`, `bcm_strtoul`,
`wl_init_timer`, `wl_add_timer`, `wl_del_timer`, `wl_free_timer`, `wl_down`;
inside: C9 (`wlc_bmac_xtal`), C14 (open).

Observed (session): `wlc_bmac_led_hw_mask_init(wlc_hw, 7)` once in the first
up; `wlc_bmac_led` 24 calls from `wlc_led.c` (`sub_07ceb5`, `sub_07cf8d`,
`sub_07d011`; masks 1, 2, 4, `activehi` 1): at up (1, 0) no access (no
change), (2, 2) and (4, 4) each R/W/R of `CC(0x64)` (`si_gpioout`: 0 -> 2 ->
6); at the watchdog no access (no change); at down (2, 0) and (4, 0): `CC(0x64)`
6 -> 4 -> 0. `wlc_bmac_led_hw_deinit(wlc_hw, 7)` at detach: R/W/R `CC(0x64)`
= 0, `CC(0x68)` = 0, `CC(0x8c)` = 0. `led_attach`/`led_detach` once each, no
access.

### R5. CCA statistics: cca_stats_read (.text+0x061b4f, 221 bytes, name original), sub_061ad3 (.text+0x061ad3, 124 bytes, name assigned: wlc_bmac_read_shm32), wlc_bmac_cca_stats_read (.text+0x061c2c, 13 bytes, name original)

Purpose: snapshot of the microcode's channel-occupancy counters for the
upper layer's `wlc_cca.c` (`cca_stats_upd`).

**`sub_061ad3(wlc_hw, base, off)`** -> u32: a 32-bit counter kept by the
microcode as two 16-bit shared-memory words (low word at a = `base + off`,
high word at a + 2), read so that a carry between the halves is not missed:

1. hi = `SHM(a + 2)`, lo = `SHM(a)`, hi2 = `SHM(a + 2)`.
2. If hi != hi2: hi = `SHM(a + 2)`, lo = `SHM(a)` (read again, once).
3. Result `(hi << 16) | lo`.

So 3 reads normally, 5 when the high word changed.

**`cca_stats_read(wlc_hw, out)`** (`out`: 9 u32; no result): with B = 0x768
for `corerev` >= 40 (**this card**), 0x6c0 for older revisions:

| `out` word | source |
|---|---|
| 0 | `sub_061ad3(wlc_hw, B, 0x00)` |
| 1 | `sub_061ad3(wlc_hw, B, 0x04)` |
| 2 | `sub_061ad3(wlc_hw, B, 0x08)` |
| 3 | `sub_061ad3(wlc_hw, B, 0x0c)` |
| 4 | `sub_061ad3(wlc_hw, B, 0x10)` |
| 6 | `sub_061ad3(wlc_hw, B, 0x14)` |
| 7 | `SHM(0x10e)` (16 bit, zero-extended) |
| 8 | `SHM(0x10c)` |
| 5 | the low word of the TSF: `wlc_bmac_read_tsf(wlc_hw, &out[5], &tmp)` (R7; reads `D11(0x180)` and `D11(0x184)`, the high word goes to a local and is dropped) |

in this order (word 5 last). The meaning of the counters is the upper
layer's (`wlc_cca.c`); word 5 is the time stamp in microseconds (purpose of
the others: unknown here; brcmsmac has no such function).

**`wlc_bmac_cca_stats_read(wlc_hw, out)`** -> 0: `cca_stats_read(wlc_hw,
out)`, return 0.

Fields and calls: `wlc_hw+0x84`, `+0xd0`; `wlc_bmac_read_shm` (A8, open), R7.

Observed: 15 calls in the session (from `cca_stats_upd`: at every up,
channel change and watchdog), each 22 accesses in exactly the order above:
R `SHM(0x76a)`, `SHM(0x768)`, `SHM(0x76a)`, ..., `SHM(0x77e)`,
`SHM(0x77c)`, `SHM(0x77e)`, then `SHM(0x10e)`, `SHM(0x10c)`, `D11(0x180)`,
`D11(0x184)` (all read 0 in the model). The re-read of step 2 and the
arithmetic: direct calls, see Verification.

### R6. Rate shared-memory offsets: wlc_bmac_rate_shm_offset (.text+0x061c39, 52 bytes), wlc_bmac_stf_get_rateset_shm_offset (.text+0x061c6d, 135 bytes), wlc_bmac_stf_set_rateset_shm_offset (.text+0x0620d7, 86 bytes), names original

The microcode keeps one entry per legacy rate in shared memory; two
directories give the entry of each rate: `SHM(0x1c0..0x1de)` for the OFDM
rates and `SHM(0x200..0x21e)` for the CCK rates (bmac-init G).

**`wlc_bmac_rate_shm_offset(wlc_hw, rate)`** (`rate`: u8, in 500 kbit/s
units) -> u32:

1. c = `rate_info[rate]` (Broadcom's byte table `rate_info`, below).
2. dir = 0x1c0 if bit 7 of c is set (OFDM), else 0x200 (CCK).
3. Result = 2 * `SHM(dir + 2 * (c & 0x0f))` (the directory word is a word
   address; the result is the byte offset of the rate's entry; the caller
   truncates to 16 bits).

`rate_info` (109 bytes, index = rate; every index not listed is 0):

| rate (index) | 2 | 4 | 11 | 12 | 18 | 22 | 24 | 36 | 48 | 72 | 96 | 108 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Mbit/s | 1 | 2 | 5.5 | 6 | 9 | 11 | 12 | 18 | 24 | 36 | 48 | 54 |
| value | 0x0a | 0x14 | 0x37 | 0x8b | 0x8f | 0x6e | 0x8a | 0x8e | 0x89 | 0x8d | 0x88 | 0x8c |
| directory word | 0x214 | 0x208 | 0x20e | 0x1d6 | 0x1de | 0x21c | 0x1d4 | 0x1dc | 0x1d2 | 0x1da | 0x1d0 | 0x1d8 |

(The values are the PLCP rate codes, bit 7 marking OFDM.) A rate of 0..108
not in the table reads `SHM(0x200)`. The object does not bound the index:
for 109..255 it reads the bytes that follow the table in the object's
`.rodata` (109..111: 0; from 112 the table `mcs_table`: 112 -> 0x64, 113 ->
0x19, 114 -> 0, 115 -> 0, 116 -> 0xbc, 117 -> 0x34, 118, 119 -> 0, 120 ->
0x37, 121 -> 0x1c, 122, 123 -> 0, 124 -> 0x98, 125 -> 0x3a, 126, 127 -> 0);
no caller in a session passes such a rate (the rate-set callers pass the
legacy rates masked to 7 bits); an implementation may treat them as "not
in the table" only if it accepts that difference (open question Q3).

**`wlc_bmac_stf_get_rateset_shm_offset(wlc_hw, rs, off, out)`** (`rs`: a
rate set, a u32 count followed by the rate bytes from `rs+4`; `off`: u16;
`out`: entries of 6 bytes; no result): for i = 0 .. count-1 (u32 compare):

1. r = `rs.rates[i] & 0x7f`; o = `wlc_bmac_rate_shm_offset(wlc_hw, r)`.
2. If `corerev` >= 40: `out[i]+4` (u16) = `SHM((o & 0xffff) + off)` (the
   sum is not truncated to 16 bits).
3. `out[i]+2` (u16) = o (truncated); `out[i]+0` (u8) = r. Byte +1 is not
   written.

**`wlc_bmac_stf_set_rateset_shm_offset(wlc_hw, rs, off, in)`** (no result):
for i = 0 .. count-1: `SHM(off + in[i]+2) = in[i]+4` (`wlc_bmac_write_shm`).

Callers: `wlc_update_txppr_offset` (`wlc.c`: get, change the values, set;
`off` = 0x0e in the session - the per-rate transmit power offset word of an
entry, (unverified name)), `wlc_rate_shm_offset` and
`wlc_mod_prb_rsp_rate_table` (`wlc.c`), the PHY through
`wlapi_bmac_rate_shm_offset(shim, rate)` (bmac-init A18). Fields:
`wlc_hw+0x84`; calls A8 only.

Observed: 132 calls of `wlc_bmac_rate_shm_offset`, all from
`wlc_bmac_stf_get_rateset_shm_offset` (11 calls, at every up and channel
change, for the rate set 2, 4, 11, 12, 18, 22, 24, 36, 48, 72, 96, 108 in
this order): each R directory word (0x214, 0x208, 0x20e, 0x1d6, 0x1de,
0x21c, 0x1d4, 0x1dc, 0x1d2, 0x1da, 0x1d0, 0x1d8 - as in the table), then R
`SHM(o + 0x0e)` (o = 0 in the model, so `SHM(0x0e)`). The arithmetic for
every rate 0..255: direct calls, see Verification.

### R7. TSF, FIFO errors, MAC watchdog, state: wlc_bmac_read_tsf (.text+0x060499, 74 bytes), wlc_bmac_fifoerrors (.text+0x064689, 240 bytes), wlc_bmac_watchdog (.text+0x067cf0, 91 bytes), wlc_bmac_state_get (.text+0x0678e8, 48 bytes), names original

**`wlc_bmac_read_tsf(wlc_hw, lo, hi)`** (`lo`, `hi`: u32 pointers, each may
be NULL; no result): v = `D11(0x180)` (32 bit; read **always**, even if
`lo` is NULL); if `lo`: *lo = v. If `hi`: *hi = `D11(0x184)` (only then
read). Low word first (brcmsmac's names `tsf_timerlow`/`tsf_timerhigh`;
that the hardware latches the high word on the low read is the usual
reason for the order, (unverified)). Callers: `wlc_read_tsf` (`wlc.c`),
`cca_stats_read`, `wlc_bmac_recv`, `wlc_bcn_tsf_later`. Observed 24 calls
(15 from `cca_stats_read`, 9 from `wlc_read_tsf`), each R 0x180, R 0x184.

**`wlc_bmac_fifoerrors(wlc_hw)`** (no result): for FIFO i = 0..5 in order:

1. v = `D11(0x20 + 8 * i)` (32-bit read; the FIFO's interrupt status).
2. If `v & 0xdc00` = 0: next FIFO.
3. Count in the upper layer's counters (`wl_cnt_t` of `wlioctl.h`, at the
   address in the word `pub+0xa0`, `pub` = the word `wlc+0x00`): bit 14
   (0x4000) -> `rxoflo` (+0x80) += 1; bit 10 (0x0400) -> `dmade` (+0xa8);
   bit 11 (0x0800) -> `dmada` (+0xac); bit 12 (0x1000) -> `dmape` (+0xb0);
   bit 15 (0x8000) -> `txuflo` (+0x34). (Checked in this order.)
4. `wlc_fatal_error(wlc)` (upper layer: reinitialises the driver) and
   **return** - the other FIFOs are not looked at.

The object has a write of `D11(0x20 + 8 * i) = v & 0xdc00` (clear the bits)
for the case "some bit of 0xdc00 set but none of the five" - impossible, so
it never runs; the status bits are not cleared here. Callers:
`wlc_bmac_watchdog` and `wlc_fifoerrors` (`wlc.c`, a 15-byte forwarder that
nothing in the object calls).

**`wlc_bmac_watchdog(wlc)`** (argument: the upper layer's `wlc`; no
result): `wlc_hw` = `wlc+0x20`. Nothing if `up` (`wlc_hw+0x10c`) = 0. Else:

1. `now` (`wlc_hw+0x110`, u32) += 1 (watchdog ticks).
2. `wlc_bmac_fifoerrors(wlc_hw)`.
3. DMA slot 0xd8 (`dma_rxfill`, bmac-data P36) on FIFO 0's engine
   (`wlc_hw+0x20`, called without a check). (MAC revision 4 only: also FIFO 3,
   `wlc_hw+0x38`.)
4. `wlc_phy_watchdog(pi)` of the current band (`acphy-api-watchdog.md`).

Caller: the upper layer's watchdog `sub_0450f0` (once per second while up).
Observed: 4 calls (two watchdog steps of each up): R `D11(0x20)`, `0x28`,
`0x30`, `0x38`, `0x40`, `0x48` (all 0), W `D11(0x224)` (receive refill:
the descriptor pointer of FIFO 0's receive channel), then the PHY watchdog.

**`wlc_bmac_state_get(wlc_hw, out)`** -> 0: `out[0]` (u32) = `machwcap`
(`wlc_hw+0xa4`); `out[1]` (s32) = sign-extended byte result of
`wlc_phy_preamble_override_get(pi)` (current band). No access. Caller:
`wlc_attach` (once).

Fields: `wlc_hw` +0xa4, +0xd0, +0xe8, +0x10c, +0x110 (R/W), +0x20 (and
+0x38); `wlc+0x00` (`pub`; `pub+0xa0`). Outside calls: `wlc_fatal_error`,
the DMA slot 0xd8, `wlc_phy_watchdog`, `wlc_phy_preamble_override_get`.

### R8. Small setters: wlc_bmac_xmtfifo_sz_get (.text+0x05fe87, 33 bytes), wlc_bmac_ampdu_set (.text+0x061226, 66 bytes), wlc_bmac_antsel_type_set (.text+0x064779, 33 bytes), wlc_bmac_antsel_set (.text+0x060011, 12 bytes), wlc_bmac_filter_war_upd (.text+0x06452e, 26 bytes), wlc_bmac_txant_set (.text+0x0626e8, 46 bytes), names original; sub_062684 (.text+0x062684, 100 bytes, bmac-init's name wlc_ucode_txant_set)

**`wlc_bmac_xmtfifo_sz_get(wlc_hw, fifo, out)`** -> s32: if `fifo` > 5
(unsigned): return -29. Else `*out` (u32) = the 16-bit word `fifo` of the
table whose address is `wlc_hw+0x150` (`xmtfifo_sz_tbl`, chosen at attach:
for `corerev` >= 40 Broadcom's table at `.data+0x330` = {0x0009, 0x00b7,
0x0019, 0x0011, 0x0011, 0x0008} for FIFOs 0..5); return 0. Callers:
`wlc_attach`, `wlc_ampdu_mode_upd` (observed 12 calls, FIFOs 0..5 twice).

**`wlc_bmac_ampdu_set(wlc_hw, mode)`** (`mode`: u8): only for `corerev` 26
or 29: copy 12 bytes into the table at `wlc_hw+0x150` from `.data+0x350`
({0x62, 0xff, 0xf3, 0x15, 0x11, 0x05}) if `mode` = 2, else from
`.data+0x360` ({0x96, 0xdf, 0xdf, 0x15, 0x11, 0x05}). **Nothing on revision
42** (observed 3 calls, no effect).

**`wlc_bmac_antsel_type_set(wlc_hw, type)`** (u8): `antsel_type`
(`wlc_hw+0x1a1`) = `type`; `wlc_phy_antsel_type_set(pi, type)`. Caller:
`wlc_antsel_attach` (observed once, type 0).

**`wlc_bmac_antsel_set(wlc_hw, v)`** (u32): `wlc_hw+0x1a4` (name assigned:
`antsel_avail`) = v. Read by `wlc_bmac_init` only for other chips (chip
control for antenna selection on chips 0xa8e2..0xa8e4, 0xa8e6, 0x4749,
0x5357, 0xd144). Caller: `wlc_attach` (observed once, v = 0).

**`wlc_bmac_filter_war_upd(wlc_hw, on)`** (u8): `wlc_phy_set_filt_war(pi,
on)`; nothing else. Caller: `wlc_channel_reg_limits` (observed 22 calls, `on`
= 0).

**`wlc_bmac_txant_set(wlc_hw, ant)`** (u16): `txant` (`wlc_hw+0x100`) =
`ant`; if `up` and the byte `+0xf4` of the upper layer's structure whose
address is the word `wlc+0x550` is 0: `sub_062684(wlc_hw)`.
**`sub_062684`**: only for `corerev` < 40 (nothing on this card): `SHM(0x188)
= (SHM(0x188) & 0xfc3f) | txant` and then the same for `SHM(0x22)` (the
32-bit word at `+0x100` is read, only its low half used). bmac-init E8 is
correct. Callers: `wlc_stf_spatialpolicy_set_complete`, `sub_173a21`
(observed 15 calls, `ant` 0x40 or 0xc0, no access); `sub_062684` also from
B10.

Fields: `wlc_hw` +0x84, +0x100 (W), +0x150, +0x1a1 (W), +0x1a4 (W), +0x10c,
+0xe8; `wlc+0x550` (+0xf4). Outside: `wlc_phy_antsel_type_set`,
`wlc_phy_set_filt_war`, `osl_memcpy` (other revisions).

### R9. Interrupt callbacks of the backplane: sub_061268 (.text+0x061268, 29 bytes, name assigned: wlc_bmac_wlintrsoff), sub_061285 (.text+0x061285, 27 bytes, name assigned: wlc_bmac_wlintrsrestore)

`wlc_bmac_attach` registers them (D2 step 14:
`si_register_intr_callback(sih, sub_061268, sub_061285, NULL, wlc_hw)`);
the backplane calls them around the moves of its register window while the
current core is the MAC core (`si_clkctl_fast_pwrup_delay`, `si_tcm_size`,
`si_seci_upd`, ...; spec `chip`), so that the interrupt handler does not
access the core meanwhile.

* **`sub_061268(wlc_hw)`** -> u32: if `up` (`wlc_hw+0x10c`): return
  `wl_intrsoff(wl)` (the interrupt mask before; `wl` = the word `wlc+0x10`,
  `wlc` = `wlc_hw+0x00`). Else return 0.
* **`sub_061285(wlc_hw, mask)`**: if `up`: `wl_intrsrestore(wl, mask)` (the
  second argument is passed through unchanged; the decompiler hides it).
  No result.

Observed: 3 calls each (attach: `si_tcm_size`; the two ups:
`si_clkctl_fast_pwrup_delay`), `up` = 0 every time, so no glue call.

### R10. Other chips: wlc_bmac_4331_epa_init (.text+0x060eb3, 180 bytes), wlc_bmac_set_ctrl_ePA (.text+0x064ae5, 60 bytes), names original

**`wlc_bmac_4331_epa_init(wlc_hw)`** -> s32: only on bus PCI for chips
0xa9a7/0x4331 with chip package (`sih+0x44`) 9 or 0xb: GPIO 5 pull-down
off and pull-up on, GPIO 2 (mask 4) released (`si_gpiopull(sih, 0, 0x20,
0)`, `si_gpiopull(sih, 1, 0x20, 0x20)`, `si_gpiocontrol(sih, 4, 0, 0)`,
`si_gpioout(sih, 4, 0, 0)`, `si_gpioouten(sih, 4, 0, 0)`); return 0.
Otherwise return -1. **On the 4360: returns -1, nothing else.**

**`wlc_bmac_set_ctrl_ePA(wlc_hw)`** -> s32: if `clk` (`wlc_hw+0x186`) = 0:
return -1. Else (bus PCI, chips 0xa9a7/0x4331: `si_chipcontrl_epa4331(sih,
1)`) return 0. **On the 4360: returns 0 (clock on) or -1, nothing else.**

Both are called by `wlc_up` (C1 steps 3 and 6.1), which ignores the result.
Observed: 2 calls each (the two ups): `4331_epa_init` -1, `set_ctrl_ePA` 0,
no access. An open implementation for the 4360 is `return -1` and `return
clk ? 0 : -1`.

## Corrections to bmac-init.md

This specification supersedes these places of bmac-init.md (checked against
the code and, where marked, by direct calls):

* **E8 `wlc_bmac_txant_set`**: correct. Detail added: `sub_062684` reads the
  32-bit word at `wlc_hw+0x100` and uses its low half (R8).
* **E9 (energy detect)**: replaced by R1. The first version described only
  the 20 MHz branch; at 40 and 80 MHz the function writes the low nibble of
  `D11(0x6c8)` (40 MHz: 2 / 1 by the control sideband; 80 MHz: 0xc / 3) and
  then `D11(0x6c6) = (old & 0xf0f0) | 0x0303` (40) or `| 0x0f0f` (80); at
  40/80 MHz `SHM(0x5a)` is written only for MAC revisions <= 40 (not on this
  card), at 20 MHz always. `sub_06212d` is called with the constant 1, not
  with `on` (so `on` has no effect on the 4360); `sub_06212d(.., 0)` (value
  0x000f) comes only from the iovar. Its name in bmac-init
  (`wlc_bmac_ifs_ctl1_edcrs`) is replaced by `wlc_bmac_ifsctl_ac_edcrs`
  (either is a proposal; the tools key on `sub_06212d`).
* **E10 (coexistence)**: replaced by R3. Differences:
  1. `wlc_bmac_btc_mode_set` returns -3 for **every** non-zero mode (not
     only "auto on some chips") when the wire type is > 2, the MAC revision
     > 12 and bit 29 of `machwcap_backup` (`wlc_hw+0xa8`) is clear; it also
     clears `btc.flags` and sets the GPIO fields for the wire-2 and old-MAC
     cases; the auto mode resolves by `btc+0x18`, which is the SROM variable
     **`aa2g`** (bmac-init's `srom_btc_val`; also D2 step 14 "an SROM
     variable"), and by chip id (5 for the 4360 when the coexistence hardware
     or `boardflags2` bit 7 is there and `aa2g` >= 3).
  2. `wlc_bmac_btc_wire_set` "auto" resolves to 2, 3 **or 4** (4: `boardflags2`
     bit 7 set and bit 25 clear); for a wire > 2 it **overwrites**
     `btc.gpio_mask` and `btc.gpio_out` with the per-chip value (0 for the
     4360) - they do not merely "stay 0".
  3. `wlc_bmac_update_bt_chanspec`: the second notification on 2.4 GHz
     depends on **40 MHz** (bandwidth 0x1800), not 80 MHz; a chanspec of 0
     and the bands 0x4000/0x8000 send one notification with value 0; the
     masks/values depend on the ChipCommon revision (>= 35 or not). Verified
     by direct calls.
  4. `sub_06106b` is called by `wlc_bmac_bsinit` (B10), not by B1.
* **C2 step 9 (LED GPIO at `wlc_bmac_hw_up`)**: the byte +4 of an LED entry
  is 1 for an active-**high** LED (the default) and 0 for an active-low one
  (bit 7 of the `ledbh<i>`/`wl0gpio<i>` variable); so the mask `a` of C2
  step 9 (entries whose byte is 0) is the set of active-low LED pins - the
  remark "presumably active high" there is the wrong way round. The words
  `+0x308`/`+0x30c` are `gpio_val`/`gpio_mask` of the LED state (R4).
* **Data, `wlc_hw` fields**: `btc+0x18` = `aa2g` (see above); `wlc_hw+0x110`
  = `now` (watchdog ticks), `wlc_hw+0xa8` = `machwcap_backup`, `wlc_hw+0x1a4`
  = `antsel_avail` (assigned) are new.

## Verification

Method: every function was read in the decompiler output and checked in the
disassembly (`blob.py dis`) where a branch or an argument was unclear (all
of R1, R2, R3, R4 except the five-line setters, R5, R6, R7, R9, R10).
Then two kinds of emulator runs on Broadcom's object alone (no open code;
scripts in `re-out\analysis\bmac-run\`, run with Windows `python` from that
directory):

1. **`observe.py`**: the session of `hybrid.py` (attach, mpc 0, up, 1, 6,
   11, 36, 36/40, 36/80, 149/80, 157/40, 165, watchdog, down, up, 36/80,
   watchdog, down, detach) with an entry/return hook on each function of the
   Scope; per call the caller, the arguments, the result and the accesses
   made inside (output `obs-session.txt`). With `--soak --steps ...` the
   same on `phy_soak.SoakRun` steps (used for the 40/80 MHz sidebands of R1:
   40u, 44/80, 48/80, 40/80, 36l, 161u, 157/80, 161/80). The call counts of
   the assignment are reproduced exactly: energy detect 11, FIFO suspend 28,
   `wlc_bmac_led` 24, CCA 15, TSF 24, rate offsets 132. `boundary.py
   wlc_bmac.c --args` (its shorter default session) shows the same callers
   (`boundary-args.txt`).
2. **`verify.py`**: after attach and up, direct calls of the object's
   functions on states set up in its memory and in the card model, compared
   with models written from this text (output `verify-out.txt`; all 0
   differences):

| test | function(s) | cases | what was varied |
|---|---|---|---|
| edcrs | `sub_06212d`, `wlc_bmac_ifsctl_edcrs_set` | 262 | `on` 0/1 x 8 bandwidths x 8 sidebands x MAC rev 42/40, random old values of 0x6c6/0x6c8; outer function = `sub_06212d(.,1)` |
| fifo | `tx_fifo_suspended`, `_suspend`, `_resume` | 22 | XC bit 1, `D11(0x150)` bit; suspend with EN_MAC set (A3, A6, slot 0x20, A7 order), twice, FIFO without engine, resume bookkeeping and A4 |
| btc | `wlc_bmac_btc_mode_set` | 401 | random capability/board/chip/wire/`aa2g`/mode states: result, stored mode, flags, GPIO fields, every `wlc_bmac_mhf` call (incl. `sub_062b79`); A6/A7 wrap when up and enabled |
| wire | `wlc_bmac_btc_wire_set` | 300 | auto and explicit wires, chips, board types: result, wire, GPIO fields, `ucode_loaded` |
| cca | `sub_061ad3`, `wlc_bmac_cca_stats_read` | 41 | carry between the reads (5 reads), the 9 output words |
| rate | `rate_shm_offset`, `stf_get/set` | 130 | every rate 0..127 with random directories (result and the read), a rate set with masked/unknown rates |
| tsf | `wlc_bmac_read_tsf` | 4 | NULL pointers |
| fifoerr | `wlc_bmac_fifoerrors` | 54 | error bits in each FIFO: reads, counters, `wlc_fatal_error`, stop at the first error |
| btchan | `wlc_bmac_update_bt_chanspec` | 109 | ChipCommon rev 43/34, 10 chanspecs, `scanning`/`p4`, the gate predicates |
| led | `wlc_bmac_led` | 123 | `boardflags2` bit 11, polarity, current value, `sbclk` 0; the attach state |
| blink | `wlc_bmac_led_blink` | 40 | period (gcd), restart flags |
| ledtimer | `sub_061660` | 300 | random entry states and times: LED calls, entry fields, timer calls, flags |
| ledattach | `wlc_bmac_led_attach` | 3 | `ledbh<i>`/`wl0gpio<i>` variables (priority, polarity, the 0x18 limit) |
| seci | `wlc_bmac_seci_upd` | 48 | the four conditions and the upper layer's mode |
| btswitch | `wlc_bmac_set_btswitch` | 4 | 1, 0, -1 (PHY(0x418) accesses shown in the output), down |
| small | xmtfifo, state_get, 4331/ePA, R9, antsel, txant | 15 | out-of-range FIFO, `clk` 0/1, `up` 0/1 |

Read only (not executed in any run): the branches for other chips and MAC
revisions (one line each in the text), the iovar paths of R1 and R3, the
GPIO path of `wlc_bmac_set_btswitch` (chips 0x4331/0xa9a7),
`wlc_bmac_led_blink_event(0)` with a timer that cannot be deleted,
`wlc_bmac_led_detach`'s result.

Observed only in the model's limits: shared-memory counters and directories
read 0 in the sessions (the direct calls fill them); `machwcap` reads 0
(`D11(0x15c)`), so the coexistence paths are inert in every session.

## Open questions

* **Q1 (coexistence on the real card).** Whether the real 4360 board has the
  coexistence hardware as the code tests it (BTC_HW of R3: `machwcap` bit 29
  from `D11(0x15c)`, ChipCommon capabilities bit 29 (ECI), extended
  capabilities bits 0/2 (SECI/GCI), `boardflags` bit 0, `boardflags2` bit 7)
  is unknown: the model reads `D11(0x15c)` = 0 and has no ECI/SECI bits. If
  it does, the auto mode becomes 5 (with `aa2g` = 3), `wlc_bmac_btc_mode_set`
  sets host-flag bits, `wlc_bmac_update_bt_chanspec` notifies the ECI on
  every channel change and `wlc_bmac_seci_upd` may call `si_seci_upd`. To be
  read on the card (`peek-on-target.py`: `D11(0x15c)`, `CC(0x04)`,
  `CC(0xac)`).
* **Q2.** Meaning of the bits of `D11(0x6c6)` and `D11(0x6c8)` (R1): the
  accesses are exact; the interpretation (per-sub-channel energy-detect
  enables, primary-channel position) is a guess.
* **Q3.** `wlc_bmac_rate_shm_offset` with a rate > 108 reads past the end of
  `rate_info` (into `mcs_table`); no caller seen passes one. Whether the PHY
  (`wlapi_bmac_rate_shm_offset`) ever does was not observed (no PHY call in
  the session).
* **Q4.** The meaning of the CCA words 0..4 and 6..8 (R5) belongs to
  `wlc_cca.c`; this layer only copies them.
* **Q5.** The structure at `wlc+0x550` whose byte `+0xf4` gates
  `wlc_bmac_txant_set` (bmac-init E8) is not identified (stf state?).
* **Q6.** `wlc_bmac_seci_upd` returns no defined value (callers ignore it).
* **Q7.** In the sessions every 5 GHz channel of the emulated regulatory
  domain is muted (R2); whether that is the domain's quiet-channel rule or a
  limit of the emulated session (no radar detection, no beacons) was not
  traced further - it only decides which FIFO paths run.
