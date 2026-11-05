# What the upper layer does to the hardware (wlc-hw)

Status: all six sections written (2026-11-05): 1 attach, up and init; 2
channel change; 3 periodic work; 4 down and detach; 5 joining and the BSS
state (order read from the code, five procedures checked by direct
calls); 6 receive filters and scanning. Each statement says how it was checked: **observed** (seen in
the emulator, session of `tools/re/hybrid.py` on the hybrid core with all
layers open and on Broadcom's object alone; the two make the same accesses)
or **read** (from the code only).

Register notation: [access.md](access.md). `SHM(o)` is the 16-bit word of
the shared memory at byte offset `o`, `SCR(n)` scratch register `n` of the
microcode, `AMT[i]` entry `i` of the address match table (bmac-init B5),
`TPL(o)` the template memory at byte offset `o` (bmac-init F2). Procedures
of other specifications are cited by their section: bmac-init A9 =
[bmac-init.md](bmac-init.md) section A9; R1 = [bmac-run.md](bmac-run.md)
R1; and so on.

## Scope

Broadcom's upper layer (`wlc.c` and its modules) as it runs in the hybrid
build: everything below it (MAC `wlc_bmac_*`, PHY `wlc_phy_*`, DMA,
backplane, PMU, SROM, the per-rate power container, the regulatory lookup,
the rate-set helpers, the channel statistics and the transmit-power offsets
of `wlc_stf.c`) is open code and specified elsewhere. This document says
**which of those entry points the upper layer calls, when, with which
arguments, and what it writes to the hardware by itself** (shared memory
through `wlc_bmac_write_shm`, the MAC control register through
`wlc_bmac_mctrl`, host flags through `wlc_bmac_mhf`, a few registers
directly). The upper layer's own bookkeeping (station lists, BSS
configurations, iovars, events, timers) is left out except where it decides
a value that reaches the hardware.

Not here: the receive and transmit headers, transmit status and interrupt
handling (frames.md); A-MPDU and keys beyond their initialisation (later
specifications); the computation of the regulatory power limits
(`wlc_channel_reg_limits` and its helpers, the OLPC engine:
channel-power.md), of which only the call is given.

Chip 0x4360, MAC revision 42, AC-PHY, PCIe; a station (no AP, no IBSS
beyond one line).

| function | .text offset | size | name |
|---|---|---|---|
| `wlc_attach` | 0x037d10 | 6639 | original (wlc.c) |
| `wlc_up` | 0x03aac3 | 988 | original |
| `wlc_reset` | 0x03fca4 | 239 | original |
| `wlc_init` | 0x03c46f | 2627 | original |
| `wlc_down` | 0x036bae | 646 | original |
| `wlc_detach` | 0x03705c | 1638 | original |
| `wlc_set_chanspec` | 0x03a4be | 635 | original |
| `sub_03649a` | 0x03649a | 116 | assigned: `wlc_set_phy_chanspec` |
| `wlc_channel_set_chanspec` | 0x07419b | 190 | original (wlc_channel.c) |
| `sub_039dd4` | 0x039dd4 | 108 | assigned: `wlc_bsinit` |
| `sub_029597` | 0x029597 | 196 | assigned: `wlc_ucode_mac_upd` |
| `sub_02fd6b` | 0x02fd6b | 157 | assigned: `wlc_ht_vht_cap_upd` (no hardware access) |
| `wlc_set_ratetable` | 0x02cdff | 248 | original |
| `wlc_rate_lookup_init` | 0x0287c9 | 214 | original |
| `wlc_key_hw_init_all` | 0x07b763 | 447 | original (wlc_key.c) |
| `wlc_set_rcmta` | 0x026df0 | 70 | original |
| `wlc_write_seckindxblk_secinfo_by_idx` | 0x027d23 | 31 | original |
| `wlc_txfbf_update_amt_idx` | 0x179237 | 326 | original (wlc_txbf.c) |
| `sub_179ae1` | 0x179ae1 | 545 | assigned: `wlc_txbf_init` (wlc_txbf.c) |
| `wlc_set_mac` | 0x03a0fd | 79 | original |
| `wlc_set_bssid` | 0x039f81 | 295 | original |
| `wlc_ampdu_macaddr_upd` | 0x04f4f0 | 81 | original (wlc_ampdu.c) |
| `wlc_bcn_li_upd` | 0x027e3c | 53 | original |
| `wlc_mac_promisc` | 0x026ffa | 105 | original |
| `wlc_mac_bcn_promisc` | 0x027063 | 41 | original |
| `wlc_enable_probe_req` | 0x026ef0 | 77 | original |
| `sub_03a14c` | 0x03a14c | 96 | assigned: `wlc_ucode_wake_upd` |
| `wlc_stay_awake` | 0x024bc7 | 179 | original |
| `wlc_rfaware_lifetime_set` | 0x027d74 | 200 | original |
| `sub_027e71` | 0x027e71 | 58 | assigned: `wlc_wme_retries_write` |
| `sub_026e83` | 0x026e83 | 55 | assigned: `wlc_mhf2_bit0_upd` |
| `sub_0396ff` | 0x0396ff | 164 | assigned: `wlc_ldpc_set` |
| `wlc_stf_chanspec_upd` | 0x1732f8 | 31 | original (wlc_stf.c) |
| `sub_1731cf` | 0x1731cf | 297 | assigned: `wlc_stf_pwrthrottle_upd` |
| `wlc_stf_duty_cycle_set` | 0x172f21 | 70 | original (wlc_stf.c) |
| `wlc_set_pwrthrottle_config` | 0x172f67 | 616 | original (wlc_stf.c) |
| `wlc_ampdu_shm_upd` | 0x056452 | 147 | original (wlc_ampdu.c) |
| `sub_030b4b` | 0x030b4b | 63 | assigned: `wlc_radio_hwdisable_upd` |
| `wlc_set_home_chanspec` | 0x039e94 | 171 | original |
| `sub_037bf9` | 0x037bf9 | 40 | assigned: `wlc_ht_coex_set` (no access in the session) |
| `wlc_edcf_acp_apply` | 0x02c881 | 180 | original |
| `wlc_mhf`, `wlc_write_shm`, `wlc_read_shm`, `wlc_set_shm`, `wlc_write_template_ram` | 0x026e6a, 0x027cef, 0x027ee1, 0x027cd9, 0x026e48 | 25, 18, 15, 22, 15 | original: pass-throughs to `wlc_bmac_mhf`, `wlc_bmac_write_shm`, `wlc_bmac_read_shm`, `wlc_bmac_set_shm`, `wlc_bmac_write_template_ram` |
| `wlc_intrson`, `wlc_intrsoff`, `wlc_intrsrestore` | 0x07a720, 0x07a6b5, 0x07a684 | 73, 107, 49 | original (wlc_intr.c; their accesses only) |
| `sub_0450f0` | 0x0450f0 | 4621 | assigned: `wlc_watchdog` (section 3) |
| `sub_046432` | 0x046432 | 100 | assigned: `wlc_watchdog_timer` (section 3) |
| `wlc_statsupd` | 0x027107 | 2744 | original (section 3) |
| `wlc_bmac_btc_period_get`, `wlc_bmac_btc_params_get` | 0x0619d4, 0x061995 | 255, 63 | original (wlc_bmac.c, still Broadcom's; section 3) |
| `wlc_stf_tempsense_upd`, `wlc_stf_pwrthrottle_upd` | 0x176728, 0x1752b8 | 287, 443 | original (wlc_stf.c; section 3) |
| `sub_07d1a0` | 0x07d1a0 | 31 | assigned: `wlc_led_watchdog` |
| `wlc_led_down`, `wlc_led_deinit` | 0x07d15a, 0x07d235 | 70, 24 | original (wlc_led.c; section 4) |
| `wlc_tpc_get_local_constraint_qdbm` | 0x178ae3 | 114 | original (wlc_tpc.c; section 2) |
| `wlc_quiet_chanspec` | 0x06e5e0 | 438 | original (wlc_channel.c; section 2) |
| `sub_06f334` | 0x06f334 | 275 | assigned: `wlc_channel_afeoverride_upd` (section 2) |
| `wlc_doiovar` (iovar `chanspec`), `sub_0411e9` (ioctls `WLC_SET_PROMISC`, `WLC_SET_MONITOR`) | 0x0469ed, 0x0411e9 | 16806, 9748 | original; assigned: `wlc_ioctl_handler` (only the cases named) |
| `wlc_assocresp_client`, `wlc_join_complete` | 0x05e017, 0x05e5a6 | 1423, 1176 | original (wlc_assoc.c; section 5) |
| `sub_05cd9c` | 0x05cd9c | 487 | assigned: `wlc_join_bss_done` (wlc_assoc.c) |
| `wlc_sta_assoc_upd`, `wlc_disassociate_client`, `wlc_clear_hw_association` | 0x05ad81, 0x05bf4c, 0x05abe4 | 206, 1241, 305 | original (wlc_assoc.c) |
| `wlc_BSSinit`, `wlc_BSSinit_rateset_filter` | 0x03ae9f, 0x035e22 | 1174, 278 | original |
| `wlc_bsscfg_set_infra_mode` | 0x06aaeb | 36 | original (wlc_bsscfg.c) |
| `wlc_tsf_adopt_bcn`, `wlc_tbtt_align` | 0x02faca, 0x02fa90 | 318, 58 | original |
| `sub_02f996` | 0x02f996 | 250 | assigned: `wlc_tbtt_calc` |
| `wlc_calc_tbtt_offset`, `wlc_compute_bcntsfoff` | 0x17ab79, 0x028323 | 117, 87 | original |
| `wlc_pretbtt_set`, `wlc_pretbtt_calc`, `wlc_dtimnoslp_set` | 0x036e34, 0x024ce2, 0x027eab | 60, 185, 54 | original |
| `wlc_macctrl_init`, `wlc_set_ps_ctrl`, `wlc_ps_allowed`, `wlc_set_pmstate` | 0x026f79, 0x03b62c, 0x03b5b9, 0x03f924 | 129, 82, 115, 301 | original |
| `wlc_set_wake_ctrl`, `wlc_set_uatbtt`, `wlc_set_pmawakebcn` | 0x03a1ac, 0x03a2aa, 0x03a422 | 41, 142, 156 | original |
| `wlc_clear_bssid`, `wlc_switch_shortslot` | 0x03a0a8, 0x03783b | 85, 104 | original |
| `sub_02c636` | 0x02c636 | 587 | assigned: `wlc_edcf_setparams` (section 5.4) |
| `sub_04ab93` | 0x04ab93 | 5547 | assigned: `wlc_recv_bcn` (the beacon part of the receive path; only its hardware-facing calls, 5.3/5.4) |
| `wlc_scan` | 0x16eb4a | 2211 | original (wlc_scan.c; section 6) |
| `sub_16e31e` | 0x16e31e | 1708 | assigned: `wlc_scan_timer` |
| `sub_16e2b7`, `sub_16e03b` | 0x16e2b7, 0x16e03b | 103, 317 | assigned: `wlc_scan_mac_away`, `wlc_scan_mac_home` |
| `wlc_skip_adjtsf` | 0x0797cf | 120 | original (wlc_hw.c) |

## Overview

The upper layer reaches the hardware almost only through the open entry
points. In the session (`hybrid.py` SESSION: attach, mpc 0, up, nine
channels, watchdog, down, up, 36/80, watchdog, down, detach) it makes, from
its own code, 3,845 calls of open functions (most of them the channel
validity checks, the per-rate power container and the regulatory lookup,
which make no access) and 359 card accesses of its own. Of those, all but
37 are the rests of the lower layers that are still Broadcom's (backplane
enumeration, OTP, PCIe bridge); the 37 are: the interrupt mask
`D11(0x12c)` (`wlc_intrsoff`/`wlc_intrson`/`wlc_intrsrestore`), the
transmit FIFO interrupt status `D11(0x20..0x48)` (read by
`wlc_bmac_fifoerrors`, R7), `D11(0x688)`, `D11(0x3dc)`, `D11(0xa00)` and a
read of `D11(0x120)` (observed: `re-out/analysis/wlc-hw/upper.py`,
`upper-session.txt`).

What a driver without the upper layer must reproduce, in short:

* **attach**: `wlc_bmac_attach` (bmac-attach), then software state that
  later decides hardware values: retry limits, the host-flag copies set by
  the module attaches, the PHY's chain masks, the radio switch state;
* **up**: power-up of the MAC layer (`wlc_bmac_hw_up`, `wlc_bmac_up_prep`),
  then `wlc_reset` and `wlc_init`; `wlc_init` runs `wlc_bmac_init` and then
  writes the upper layer's defaults: the key table and its index block, the
  address match table (own address, BSSID, the 50 key entries), the
  beamforming block of the shared memory, the templates of the own address,
  the response-rate table, the channel with its power limits, frame burst
  and A-MPDU words, the MAC control bits for station mode; then enables
  the MAC; `wlc_up` finishes with host flags, the frame lifetime, the
  interrupt mask, the retry words per access category, LDPC and the duty
  cycle words;
* **channel change** (section 2), **watchdog** (section 3), **down/detach**
  (section 4), **join** (section 5), **filters/scan** (section 6).

## Data

### Upper-layer fields that decide hardware values

`wlc` is the upper layer's state, `pub` = the pointer at `wlc+0x00`, `stf`
= the pointer at `wlc+0x550` (cca-stf.md "The stf state"), `band` = the
upper layer's current band state (pointer at `wlc+0x40`; `wlc+0x50` 2.4 GHz,
`wlc+0x58` 5 GHz; layout bmac-attach "Upper-layer state"), `bsscfg` = the
station interface's BSS configuration (pointer at `wlc+0x2f0`; `wlc+0x2f8`
is the primary configuration, the same object for this driver; its bytes
`+0x08` enabled, `+0x0c` associated/up, `+0x22` infrastructure, `+0xf0`
BSSID, `+0xf6` own address), `defbss` = the default
BSS description (pointer at `wlc+0x418`). Values are those of the session.

| field | value | meaning, by its use | set by |
|---|---|---|---|
| `pub+0x14` | 42 | MAC revision | bmac-attach |
| `pub+0x34` | 0 / 1 | the driver is up | `wlc_up`, `wlc_down` |
| `pub+0x35` | 0 | hardware off | glue |
| `pub+0x40` | 0 / 1 | the hardware was powered up once (`hw_up`) | `wlc_up` |
| `pub+0x44` | 2 | number of bands | bmac-attach |
| `pub+0x54` | 1 | WME (QoS) enabled | attach (default) |
| `pub+0x59` | 0 | associated | join (section 5) |
| `pub+0x5e`, `pub+0x5f` | 1, 1 | A-MPDU transmit / receive enabled | attach |
| `pub+0x68` | bits 0..1 set | 802.11n enabled (`& 3`) | attach |
| `pub+0x84` | 0 | radio disabled, bit mask (bit 1: by the hardware switch) | `sub_030b4b`, `wlc_up` |
| `pub+0xd4` | 54 | number of key entries of the hardware key table | `wlc_attach` (rev >= 40: 61, or 54 with beamforming; at most 54) |
| `pub+0xdc` | 0x5e0 | shared-memory byte offset of the security index block | `wlc_attach` (rev >= 40) |
| `pub+0xe0` | 68 | number of words of the security index block | `wlc_attach` (rev >= 40) |
| `pub+0xe4` | 0x2ea | copied into `stf+0x2a` (`shmem_base` of cca-stf.md) | `wlc_attach` (rev >= 40) |
| `pub+0xe8` | 1 | transmit beamforming present | attach |
| `wlc+0x3c0` | 2 * `SHM(0x56)` | byte offset of the hardware key table | `wlc_init` |
| `wlc+0x454`, `wlc+0x474` | `SHM(0x1c0..0x1de)`, `SHM(0x200..0x21e)` | copies of the two rate directories (OFDM, CCK) | `wlc_init` |
| `wlc+0x52a` | 7 | short retry limit | `wlc_info_init` |
| `wlc+0x52c` | 6 | long retry limit (MAC rev >= 40) | `wlc_attach` |
| `wlc+0x2be..0x2c5` | 4 x u16 | retry limits per access category, four 4-bit fields each; bits 8..11 = the long retry limit | `wlc_attach`, `wlc_init` |
| `wlc+0x5f8` | 800 | frame lifetime (`wlc_rfaware_lifetime_set`) | attach |
| `wlc+0x704` | 0 | bit mask of the requesters of probe-request reception (bit 0..2) | `wlc_enable_probe_req` |
| `wlc+0x708` | 0 | decides host flag 2 bit 0 (`sub_026e83`) | iovar |
| `wlc+0x71a` | 1 | MAC rev >= 40: clear bit 0 of `D11(0xa00)` at up | `wlc_attach` |
| `wlc+0x295` | 0 | argument of `wlc_phy_tkip_rifs_war`; selects `SHM(0x82)` | iovar |
| `wlc+0x42a` | 0 | value of `SHM(0x74)` | iovar |
| `wlc+0x2b1`, `wlc+0x2b2` | 0, 0 | beacon listen interval (section 5) | join |
| `wlc+0x28c` | 0 | beacon-promiscuous requesters (24 bits) | section 6 |
| `wlc+0x2a1`, `wlc+0x288` | 0, 0 | promiscuous mode: monitor flag, requesters | section 6 |
| `stf+0x13` | 1 | LDPC enabled | stf attach |
| `stf+0x59` | 1 | 1 when the PHY type is not 7 (set by `wlc_stf_attach`) | stf attach |
| `defbss+0x32` | 0x1001 | default chanspec (the `chanspec` iovar writes it) | attach, iovar |
| `wlc+0x516` | 0x1001 | home chanspec | `wlc_set_home_chanspec` |
| `wlc+0x518` | | chanspec last given to `wlc_channel_set_chanspec` | `sub_03649a` |

### Calls into the hardware layer used below

| call | specification |
|---|---|
| `wlc_bmac_attach`, `wlc_bmac_detach`, `wlc_bmac_si_attach/detach`, `wlc_hw_get_txavail` | bmac-attach |
| `wlc_bmac_mctrl` | bmac-init A2 |
| `wlc_bmac_suspend_mac_and_wait`, `wlc_bmac_enable_mac` | A6, A7 |
| `wlc_bmac_read_shm`, `wlc_bmac_write_shm`, `wlc_bmac_set_shm` | A8 |
| `wlc_bmac_mhf` | A9 |
| `wlc_bmac_init` | B1 |
| `wlc_bmac_write_amt` | B5 |
| `wlc_bmac_hw_up`, `wlc_bmac_up_prep`, `wlc_bmac_up_finish`, `wlc_bmac_down_prep`, `wlc_bmac_down_finish`, `wlc_bmac_reset` | C2, C3, C4, C5, C6, C8 |
| `wlc_bmac_set_ctrl_bt_shd0`, `wlc_bmac_radio_read_hwdisabled`, `wlc_hw_deviceremoved` | C11, C13, C14 |
| `wlc_bmac_set_rcmta`, `wlc_bmac_retrylimit_upd` | E3, E7 |
| `wlc_bmac_write_template_ram` | F2 |
| `wlc_bmac_ifsctl_edcrs_set`, `wlc_bmac_led*`, `wlc_bmac_read_tsf`, `wlc_bmac_watchdog`, `wlc_bmac_state_get`, `wlc_bmac_xmtfifo_sz_get`, `wlc_bmac_ampdu_set`, `wlc_bmac_antsel_type_set`, `wlc_bmac_antsel_set`, `wlc_bmac_txant_set`, `wlc_bmac_seci_upd`, `wlc_bmac_btc_mode_get`, `wlc_bmac_btc_wire_get`, `wlc_bmac_set_ctrl_ePA`, `wlc_bmac_4331_epa_init` | bmac-run R1, R4, R7, R8, R3, R10 |
| `wlc_bmac_set_chanspec`, `wlc_bmac_txbw_update` | acphy-chanspec 1-3, bmac-init B12 |
| `wlc_phy_*` | [acphy-api.md](acphy-api.md) (entry point table; open names there) |
| `wlc_stf_ss_update`, `wlc_stf_ss_algo_channel_get`, `wlc_stf_spatial_policy_set`, `wlc_stf_txcore_shmem_write`, `wlc_update_txppr_offset`, `cca_stats_upd`, `cca_stats_tsf_upd`, `wlc_cca_attach/detach` | [cca-stf.md](cca-stf.md) |
| `wlc_rateset_*`, `wlc_rate_rspec_reference_rate`, `rate_info` | [rate.md](rate.md) |
| `wlc_valid_chanspec*`, `wf_chspec_*` | [channel.md](channel.md) |
| `ppr_*`; `clm_*` | [ppr.md](ppr.md); [clm.md](clm.md) |
| `wlc_channel_reg_limits` and the OLPC engine | channel-power.md (another analyst) |

The interrupt mask: `wlc_intrsoff(wlc)` writes `D11(0x12c) = 0` and reads it
back, keeping the old mask (`wlc_hw+0x98`) for the caller;
`wlc_intrsrestore(wlc, mask)` writes `D11(0x12c) = mask`; `wlc_intrson(wlc)`
writes `D11(0x12c) =` the default mask `wlc_hw+0x9c` = 0xb0e7a860 (set by
bmac-attach step 1). Each only while the core's clock is on (`wlc_hw+0x186`).
The meaning of the bits belongs to frames.md. (Observed: every write of
`D11(0x12c)` in the session is one of these three, with 0 or 0xb0e7a860.)

## 1. Attach, up and init

The session does `wlc_attach`, the iovar `mpc` = 0 and `wlc_up` (the glue's
`wl_up`). "mpc 0" makes no access and no call into the hardware layer
(observed).

### 1.1 wlc_attach (.text+0x037d10)

Hardware-facing steps in order (observed: the calls with these arguments;
the sources of the values read):

1. `wlc_bmac_si_attach(device, osh, regs, bustype, btparam, &vars,
   &varsize)` (bmac-attach "Helpers"; `si_attach`, chip.md).
2. The upper layer's state is allocated and defaults set (no access); among
   them the short retry limit `wlc+0x52a` = 7 (`wlc_info_init`).
3. `wlc_bmac_attach(wlc, 0x14e4, device, unit, piomode 0, osh, regs,
   bustype 1, btparam)` (bmac-attach): the whole MAC/PHY/DMA attach; it
   leaves the chip powered down (core disabled, crystal off). A result other
   than 0 fails the attach.
4. `si_fabid(sih)` (chip.md; kept at `wlc+0x700`); the upper layer's
   current band = the 5 GHz band state for a single-band 5 GHz device, else
   the 2.4 GHz one.
5. `wlc_bmac_state_get(hw, &out)` (R7): the MAC capabilities word is kept
   (`wlc+0x230`); `wlc_phy_preamble_override_get` inside.
6. `wlc_bmac_xmtfifo_sz_get(hw, f, &size)` for f = 0..5 (R8): the sizes
   (9, 0xb7, 0x19, 0x11, 0x11, 8) are kept for the upper layer's transmit
   flow control (no hardware effect).
7. MAC revision >= 40: the constants of the hardware key and security
   tables in `pub` (table above: 54 keys, index block at 0x5e0 with 68
   words); long retry limit `wlc+0x52c` = 6 and
   `wlc_bmac_retrylimit_upd(hw, 7, 6)` (E7: stores; the driver is not up,
   so no access - the values reach `SCR(6)`/`SCR(7)` in `wlc_bmac_init`
   B1 step 27); bits 8..11 of the four per-access-category retry words
   `wlc+0x2be..0x2c5` = 6 (written to `SHM(0x300..0x306)` at up, 1.5 step
   14). (Revisions < 40: other constants, no `retrylimit_upd`; not
   described.)
8. PHY capabilities: `wlc_phy_cap_get(pi)` twice (bit 0 kept as STBC
   receive, bit 3 as LDPC; acphy-attach); the duty-cycle variables of the
   SROM (`tx_duty_cycle_ofdm`, `tx_duty_cycle_cck`,
   `tx_duty_cycle_ofdm_40_5g`, `tx_duty_cycle_thresh_40_5g`,
   `tx_duty_cycle_ofdm_80_5g`, `tx_duty_cycle_thresh_80_5g`) into `stf`
   (+0x52, +0x54, +0x6e, +0x70, +0x72, +0x74); for Apple boards of chip
   0x4360 (and the Apple board types 0xf5, 0xd6, 0xef, 0x10f, 0xf4, 0xe4,
   0x10e) `stf+0x78` = 50, `stf+0x51` = 100 and `stf+0x4c` = -1 (used by
   2.3 and section 3 steps 14 and 16; board type 0x56e and Apple 0xe9/0xd1:
   25, 100, -1 and `stf+0x38` = 1; Broadcom's 0x5da: 100, 100, -1).
9. Chains: `wlc_stf_phy_chain_calc` -> `wlc_stf_spatial_policy_set(wlc, 0)`
   (cca-stf.md part 2); `wlc_phy_stf_chain_init(pi, txchain, rxchain)` with
   the chain masks of `stf` (bytes +0 and +3; 3 and 3 in the session;
   acphy-attach). MAC revision >= 40: `wlc+0x71a` = 1 (see 1.5 step 22).
10. `wlc_hw_get_txavail(hw, ...)` (bmac-attach); `wlc_phy_txpower_ipa_ison(pi)`
    kept at `stf+0x48`; `wlc_bmac_hw_etheraddr(hw, wlc+0x248)`: the
    permanent MAC address, copied to `pub+0x08` (the interface address that
    1.4 step 13 and B1 step 32.4 use).
11. Module attaches with a hardware-facing call (observed, in this order):
    * antenna selection: `wlc_bmac_antsel_type_set(hw, 0)` (R8; calls
      `wlc_phy_antsel_type_set(pi, 0)`);
    * A-MPDU transmit: `wlc_mhf(wlc, 2, 0x0040, 0x0040, 3)` - host flag 3
      bit 6 in the copies of both bands (A9; no access, the clock is off;
      the bit is cleared again when A-MPDU transmission is switched off);
      `wlc_bmac_ampdu_set(hw, 1)` and `wlc_bmac_xmtfifo_sz_get` for FIFOs
      0..5 (R8; no effect on revision 42);
    * `wlc_stf_attach`: `wlc_stf_ss_update(wlc, band)` for both bands
      (cca-stf.md; it calls `wlc_mhf(wlc, 3, 0x0040, 0x0040, 0)`: host flag
      4 bit 6 of the band current in the MAC layer - after the attach the
      5 GHz band); `wlc_phy_cap_get`; `stf+0x59` = 1 (PHY type not 7);
    * `wlc_cca_attach` (cca-stf.md part 1);
    * offloads: `si_tcm_size(sih)` (no offload engine on this card: nothing
      further);
    * the channel manager, setting the country of the SROM: per band
      `wlc_stf_ss_update` again, `wlc_phy_chanspec_band_validch(pi, band,
      vector)` for band types 1 and 2 (acphy-api A4) and
      `wlc_phy_chanspec_ch14_widefilter_set(pi, 0)` (A3).
12. `wlc_phy_cap_get` twice more; `wlc_radio_mpc_upd`; `sub_030b4b`: unless
    bit 2 of `pub+0x70` or `pub+0x35` is set,
    `wlc_bmac_radio_read_hwdisabled(hw)` (C13; observed 0) decides bit 1 of
    `pub+0x84` (set: the hardware switch disables the radio, `wlc_up` then
    does not bring the MAC up).
13. `wlc_bmac_antsel_set(hw, v)` (R8, v = 0).

Host-flag copies after the attach (observed): 2.4 GHz band: host flag 3 =
0x0040, host flag 5 = 0x0001 (the latter from bmac-attach step 14); 5 GHz
band: host flag 3 = 0x0040, host flag 4 = 0x0040. They reach the shared
memory at the band initialisation of `wlc_bmac_init` (B10 step 3).

### 1.2 wlc_up (.text+0x03aac3)

1. If `pub+0x35` is set or `wlc_hw_deviceremoved(hw)` (C14) is true: return
   -9.
2. First up after attach or resume (`pub+0x40` = 0): `wlc_led_init`, which
   calls `wlc_bmac_led_hw_mask_init(hw, mask)` with the GPIO mask of the
   LEDs of the SROM (7 in the session; R4); `wlc_bmac_hw_up(hw)` (C2);
   `pub+0x40` = 1.
3. `wlc_bmac_4331_epa_init(hw)` (R10: nothing on the 4360). (Chip 0x4313
   only: one more host flag; not described.)
4. `wlc_bmac_mhf(hw, 4, 0x0080, x, 3)` with x = 0x0080 if `stf+0x59` is
   set (always for the AC-PHY): host flag 5 bit 7, both bands.
5. If `pub+0x84` is 0: `wlc_bmac_up_prep(hw)` (C3). If it returns -9 (the
   switch disables the radio): bit 1 of `pub+0x84` is set and, if the
   primary configuration is enabled and up, it is disabled.
6. If `pub+0x84` is still 0 (otherwise only the radio monitor timer is
   started and the function returns 0):
   1. `wlc_bmac_set_ctrl_ePA(hw)` (R10: nothing).
   2. If the byte `wlc_hw+0x1bc` is 0xff (the default):
      `wlc_bmac_set_ctrl_bt_shd0(hw, 1)` (C11); else
      `wlc_bmac_set_btswitch(hw, that byte)` (R3).
   3. `wlc+0x31` = 1 (the MAC layer is up for the upper layer: gates 1.5
      step 14 and others).
   4. `wlc_bmac_mhf(hw, 0, 0x0100, WME ? 0x0100 : 0, 3)`: host flag 1 bit 8
      = WME enabled (`pub+0x54`; set in the session).
   5. Bus PCI and the byte `wlc+0x60` set (bmac-attach step 10.2; not on
      this card): `wlc_bmac_mhf(hw, 1, 0x0008, 0x0008, 3)`.
   6. `wlc_bmac_seci_upd(hw)` (R3).
   7. The glue's `wl_init`: `wl_intrsoff`, `wlc_reset` (1.3),
      `wl_intrsrestore`, `wlc_init` (1.4). (The emulator's glue calls
      `wlc_reset` and `wlc_init` only.)
   8. `pub+0x34` = 1 (up). Then the rest of `wlc_up`: 1.5.

### 1.3 wlc_reset (.text+0x03fca4)

`wlc+0x2ac` = 0. If associated (`pub+0x59`) and the associated
configuration is enabled: PM state 0 and a TBTT update on it (section 5),
then a delay of 4000 us. Statistics update (reads counters, no write), the
A-MSDU queue is flushed, **`wlc_bmac_reset(hw)`** (C8: core reset, DMA
flush). MAC revision > 40 with a flag of the primary configuration set:
`wlc_bmac_core_phypll_ctl(hw, 0)` (bmac-init A16: nothing on revision 42).
A-MPDU transmit state reset (no access). (Observed: in the session the only
hardware-layer call is `wlc_bmac_reset`.)

### 1.4 wlc_init (.text+0x03c46f), up to the MAC enable

1. **Chanspec.** Not associated (the session): c = the default chanspec
   `defbss+0x32` (the `chanspec` iovar sets it; 0x1001 at the first up),
   corrected: an 80 MHz chanspec becomes its primary 40 MHz chanspec unless
   80 MHz is allowed (the band is 2.4 GHz, or the locale of the 5 GHz band
   forbids it, or bit 2 of `pub+0xf1` is clear, or the chanspec is invalid);
   a 40 MHz chanspec becomes the 20 MHz chanspec of its control channel
   (band 5 GHz if the channel is >= 15) when 802.11n is off (`pub+0x68 & 3`
   = 0) or 40 MHz is not valid in its band; with the band locked
   (`wlc+0x24e`) or one band only, a chanspec of the other band is replaced
   by the band's default chanspec; an invalid chanspec
   (`wlc_valid_chanspec_db`, channel.md) by the default chanspec of its
   band. Associated: c = the home chanspec `wlc+0x516`. (Read; the session
   takes c = 0x1001.)
2. The default BSS's rate set is built for the band of c
   (`wlc_rateset_default`, rate.md; no access).
3. **`wlc_bmac_init(hw, c, 0)`** (B1: microcode, initialisation lists,
   PHY init for c; the MAC counts as suspended once afterwards). (Chip
   0x4335 only: fast power-up delay again; not here.)
4. Reads the two rate directories: `SHM(0x1c0)`, `SHM(0x1c2)`, ...,
   `SHM(0x1de)` (16 words, kept at `wlc+0x454`) and `SHM(0x200)` ...
   `SHM(0x21e)` (kept at `wlc+0x474`). Each word is the word address of a
   rate's entry in the microcode's rate table (R6); the microcode and the
   initialisation list put them there.
5. `wlc_bmac_btc_mode_get(hw)`, `wlc_bmac_btc_wire_get(hw)` (R3; kept by the
   coexistence module, no access).
6. Reads `SHM(0x56)`: the key table starts at byte offset 2 * that value
   (`wlc+0x3c0`). If the MAC revision is > 12 and the capabilities word kept
   in 1.1 step 5 is negative (bit 31): reads `SHM(0xb2)`, 2 * value kept at
   `wlc+0x3c4` (not in the session: the model's capabilities are 0).
7. `wlc_bcn_li_upd(wlc)`: `SHM(0xb6)` = 0 if the byte `wlc+0x2b2` is 1,
   else `wlc+0x2b2` << 8 | `wlc+0x2b1` (the beacon listen interval, section
   5; 0 in the session).
8. If the byte `wlc+0x290` is 0: reads `SHM(0x4c)` and keeps bit 7 of the
   value in `wlc+0x290` (no write).
9. **Key table** (`wlc_key_hw_init_all`, wlc_key.c):
   1. n = 5 if the byte `pub+0x5d` is set, else `pub+0xd4` (54).
      `wlc_bmac_set_shm(hw, wlc+0x3c0, 0, 16 * n)`: the n entries of 16
      bytes of the key table are zeroed (864 bytes, 432 word writes).
   2. For i = 0 .. `pub+0xe0` - 1 (0..67):
      `wlc_bmac_write_shm(hw, pub+0xdc + 2i, 0)`: `SHM(0x5e0..0x666)` = 0
      (the security index block).
   3. MAC revisions other than 41 and 44: for i = 0 .. `pub+0xd4` - 5
      (0..49): `wlc_set_rcmta(wlc, i, 00:00:00:00:00:00)`, which calls
      `wlc_bmac_set_rcmta(hw, i, addr)` (E3: an all-zero address gives
      `wlc_bmac_write_amt(hw, i, 0, 0)`: `AMT[i]` cleared) and, if
      beamforming is present (`pub+0xe8`), `wlc_txfbf_update_amt_idx(txbf,
      i, addr)` (below; nothing for a cleared entry). (Revisions < 40 also
      write `D11(0x43c)` = 0x32; not here.) Then `wlc_mhf(wlc, 0, 0x4000,
      0, 3)`: host flag 1 bit 14 cleared, both bands.
   4. Keys that are configured (none at the first up) are written again
      (`wlc_key_hw_init`; keys are a later specification).
10. **Module initialisation**: the "init" function of every registered
    module, in registration order. Those that reach the hardware in the
    session:
    * the OLPC engine (`sub_08432b` -> `wlc_olpc_eng_hdl_chan_update`):
      `wlc_channel_reg_limits(cm, c, ppr)` with
      `wlc_bmac_filter_war_upd`/`wlc_phy_set_filt_war(pi, 0)` inside,
      `wlc_phy_txpower_sromlimit(pi, c, &min, ppr, 0)`,
      `wlc_phy_tssivisible_thresh(pi)` (channel-power.md; no access);
    * beamforming (`sub_179ae1`, below).
11. **Beamforming block** (`sub_179ae1`, wlc_txbf.c). Let k = the number of
    bits set in the active transmit chain mask (`stf+1`, `txchain` of
    cca-stf.md), r = the number of bits set in the receive chain mask
    (`stf+5`, `rxchain`); 2 and 2 in the session.
    1. b = `SHM(0xb0)` (read; the word address of the microcode's
       beamforming block; kept in the txbf state +0x60). All offsets below
       are byte offsets 2b + x.
    2. `SHM(2b + 0xe0)` = the 16-bit value at txbf state +0x62 (0x186a in
       the session).
    3. k = 1: nothing more here (a flag of the txbf state cleared). k = 2:
       `SHM(2b + 0x104)` = (`stf+1` << 6) | 0x8003 and `SHM(2b + 0xe8)` =
       (`stf+1` << 6) | 0x8002 (0x80c3, 0x80c2 with mask 3).
    4. `SHM(2b + 0xe4)` = r - 1.
    5. `SHM(2b + 0x6e0)` = `SHM(2b + 0x6e0)` with bit 8 cleared (read, then
       write).
    6. Sixteen constant words (a table of the object without a symbol,
       `.rodata+0x308800`, 32 bytes) to `SHM(2b + 0x120)`, `SHM(2b +
       0x122)`, ..., `SHM(2b + 0x13e)` in order: 0x3475, 0x3475, 0x3475,
       0x217c, 0x237b, 0x217c, 0x217c, 0x217c, 0x3276, 0x3276, 0x3475,
       0x187e, 0x217c, 0x167e, 0x1d7d, 0x1f7c (observed; the same in every
       up).
    7. `SHM(2b + 0xf6)` = 0x41c2, `+0xf8` = 0, `+0xfa` = 0x17, `+0xfc` =
       0x24b, `+0xfe` = 0x97, `+0x100` = 0x500, `+0x102` = 0, in this order.
12. If up (`pub+0x34`), associated (`pub+0x59`), beamforming present and an
    associated configuration exists: per station of it the beamforming link
    is initialised (later specification). Not in the session.
13. **Own address and BSSID**, if the station configuration (`wlc+0x2f0`)
    exists (it does from the attach on):
    * `wlc_set_mac(bsscfg)`: for the primary configuration,
      `wlc_bmac_write_amt(hw, 63, bsscfg+0xf6 (its MAC address), 0x8008)`
      (B5: OR into the old attribute word, bit 3); then
      `wlc_ampdu_macaddr_upd`: `wlc_bmac_write_template_ram(hw, 0x48, 8,
      buf)` with buf = the interface address (`pub+0x08`, 6 bytes) and two
      zero bytes: `TPL(0x48..0x4f)`.
    * `wlc_set_bssid(bsscfg)`: for the primary configuration,
      `wlc_bmac_write_amt(hw, 62, bsscfg+0xf0 (the BSSID), attr)` with attr
      = 0 if the BSSID is 00:00:00:00:00:00, else 0x8006 for an
      infrastructure configuration (byte `bsscfg+0x22` set) and 0x8002 for
      an IBSS; then a 20-byte header `wlc_bmac_write_template_ram(hw, 0x30,
      0x14, buf)`: buf = BSSID (6), interface address `pub+0x08` (6),
      BSSID (6), two zero bytes: `TPL(0x30..0x43)`. (Not the primary
      configuration with infrastructure set: `wlc_rcmta_add_bssid`; not
      here.) In the session the BSSID is 0: `AMT[62]` cleared, the template
      words carry zeros and the address. (Observed.)
14. **Response rates per band** (no access): for each band (the band of c
    first, then the other; with the band locked or one band, that band
    twice) the band's default rate set (`wlc_default_rateset` ->
    `wlc_rateset_default` with the default BSS's bandwidth, rate.md) is
    filtered (`wlc_rateset_filter(.., 0, 0, 0x7f, mcsallow)`) into the
    band's hardware rate set (`band+0x5c`), restricted by
    `wlc_rateset_bw_mcs_filter`, and `wlc_rate_lookup_init(wlc, rs)` (1.6)
    fills the band's response-rate map from the basic rates of `rs`.
15. `wlc_set_home_chanspec(wlc, c)`: `wlc+0x516` = c (and the associated
    configuration's chanspecs); only for PHY types 4 (rev >= 3) and 7:
    `wlc_phy_interference_set`, `wlc_phy_acimode_noisemode_reset` - nothing
    for the AC-PHY.
16. **Channel and power**: `sub_03649a(wlc, c)` (section 2.2) - the
    channel of `wlc_bmac_init` is set again through the regular path:
    regulatory limits, `wlc_bmac_set_chanspec(hw, c, quiet, ppr)`, the duty
    cycle words, the spatial policy, `wlc_bmac_txbw_update`.
17. `SHM(0x74)` = `wlc+0x42a` (0). `SHM(0x82)` = 10000 if the byte
    `wlc+0x295` is 0 (the session), else 1504 (frame burst: by the code's
    use the maximum burst time in microseconds; unverified).
    `wlc_phy_tkip_rifs_war(pi, wlc+0x295)` (acphy-api A3).
18. `wlc_set_pwrthrottle_config(wlc)` (wlc_stf.c): decides the board's
    power throttle and writes the duty cycles:
    `wlc_stf_duty_cycle_set(wlc, duty, ofdm, 1)` writes the word
    `SHM(ofdm ? 0xb4 : 0xa4)` = (100 - duty) * 16 / duty (integer division;
    0 for duty 0; duty > 100: nothing, error). For the board of the session
    (Apple, board type 0x117, which is in the function's list of throttle
    boards) with the throttle state `stf+0x50` = 0: OFDM duty = `stf+0x52`
    (`tx_duty_cycle_ofdm`), CCK duty = `stf+0x54` (`tx_duty_cycle_cck`); 0
    in the session, so `SHM(0xb4)` = 0, then `SHM(0xa4)` = 0. (Boards not
    in the lists: no write here. With `stf+0x50` bit 0: only if the board
    is of the second throttle class, with `stf+0x51`. Read.)
19. Board-specific transmit power settings through iovars (no direct
    access): board vendor 0x106b (Apple) with chip 0x4360: `min_txpower` =
    1 and `sar_enable` = 1 (chips 0x4331: 5; 0xaa06, 0xa9c4: 1; 0x4352: 7;
    Broadcom boards 0x5c6/0x527: 5; others: only `sar_enable` = 0). Both
    are handled by the transmit power code (channel-power.md); `min_txpower`
    is the minimum that `wlc_update_txppr_offset` uses (cca-stf.md).
20. `wlc_ol_curpwr_upd` (offloads: nothing).
21. `wlc_ampdu_shm_upd`: if A-MPDU transmit and receive are both enabled
    (`pub+0x5e`, `pub+0x5f`) and the PHY type is 4, 6, 7, 8, 10 or 11:
    `SHM(0xba)` = 0xffff and `SHM(0x3c)` = 10 when both bits 0..1 of the
    byte +0x0f of the A-MPDU state are set (the session), else
    `SHM(0xba)` = 0x8000 and `SHM(0x3c)` = 5.
22. If 802.11n is enabled (`pub+0x68 & 3`): `stf+0x2a` = `pub+0xe4`,
    `wlc_stf_txcore_shmem_write(wlc, 1)` (cca-stf.md part 2:
    `SHM(0x5d4..0x5de)`).
23. **`sub_039dd4`** (the band's MAC settings, also run at every band
    switch, section 2.1):
    1. `sub_02fd6b`: HT/VHT capability fields of the default BSS (no
       access).
    2. `wlc_set_ratetable(wlc)` (1.6): the response-rate table.
    3. `sub_029597(wlc)` (1.7): MAC control for beacons and promiscuous
       mode, probe requests.
    4. `wlc_antsel_init` (nothing on this card).
    5. If the band of the upper layer equals the MAC layer's
       (`wlc_hw_get_bandunit`), the band is 2.4 GHz with PHY type 2 and
       frequency tracking is wanted (`wlc+0x5b8` or `wlc+0x5c1` = 1):
       `wlc_phy_freqtrack_start(pi)` - never for the AC-PHY (type 11).
24. If WME is enabled (`pub+0x54`, the session): `D11(0x688)` |= 0x0004
    (16-bit read and write; observed 0x0f0b -> 0x0f0f). Then, if the
    primary configuration exists and bit 1 of its word `+0x100` is clear,
    `wlc_edcf_acp_apply(wlc, bsscfg, 0)` (section 5.4): the EDCA parameters
    to the shared memory - only when the configuration is associated (byte
    `+0x0c`); in the session it is not, so nothing is written (observed: no
    EDCA write). Unassociated, the EDCA words keep the values of the
    initialisation list.
25. The software queues' priority masks (`wlc+0x2dc..0x2e9`; no access).
26. If the microcode version is not known yet (`wlc+0x22c` = 0): reads
    `SHM(0x00)` (major) and `SHM(0x02)` (minor), kept as major << 16 |
    minor.
27. `wlc_sidechannel_init` (nothing for revision 42).
28. **`wlc_bmac_enable_mac(hw)`** (A7): the MAC starts (the suspend count
    of `wlc_bmac_init` is undone).
29. `sub_03a14c(wlc)`: reads `D11(0x120)` (value not used);
    s = `wlc_stay_awake(wlc)` (section 5); `wlc_bmac_mctrl(hw, 0x04000000,
    s ? 0x04000000 : 0)`: the WAKE bit of the MAC control copy follows s;
    if s: `wlc_bmac_wait_for_wake(hw)` (A5). In the session s = 0: the
    copy loses WAKE (`D11(0x120)` 0x44020403 -> 0x40020403: observed).
30. `wlc_rfaware_lifetime_set(wlc, wlc+0x5f8)`: stores the lifetime (or 0,
    see 1.5 step 11); no access while `pub+0x34` = 0.
31. Flow control of the transmit queues reset; `wlc+0x64` = 0 (the byte
    that bmac-init E4 tests).
32. MAC revision > 9: `D11(0x3dc)` = 10000000 (32 bit; purpose unknown).
33. Reads `SHM(0x300)`, `SHM(0x302)`, `SHM(0x304)`, `SHM(0x306)`: for each
    word i and each of its four 4-bit fields, a field that is 0 in
    `wlc+0x2be + 2i` takes the field of the shared memory (the defaults of
    the microcode/initialisation list). Written back at 1.5 step 14.
34. `sub_037bf9(wlc, pub+0x6b)` (802.11n coexistence setting of the
    configuration; no access in the session); `cca_stats_upd(wlc, 0)`
    (cca-stf.md part 1).
35. Bit 1 of the byte `wlc+0x304` is kept, the others cleared.
36. If up (`pub+0x34`; not at the first up, where `wlc_init` runs before
    it is set; but at a re-init through `wlc_fatal_error`): a periodic
    calibration request `wlc_phy_cal_perical(pi, 6)` if the PHY type is 4, 7
    or 11 and (`wlc+0x300` is set or the byte `wlc+0x302` > 1); and if
    associated: the BSS state is written again (section 5: `D11(0x188)`,
    PM, PRETBTT); offload restart.
37. Association and scan in progress are aborted (`wlc_assoc_abort`,
    `wlc_scan_abort`).

### 1.5 wlc_up after wlc_init

Continuing 1.2 step 6.8:

9. If a band switch is pending (`wlc+0x24f`, set by the `chanspec` iovar
   while down when the band differs): `wlc_bmac_suspend_mac_and_wait`,
   `wlc_set_chanspec(wlc, defbss+0x32)` (section 2), `wlc+0x24f` = 0,
   `wlc_bmac_enable_mac`. (Read; not in the session.)
10. `wlc_enable_probe_req(wlc, 3, wlc+0x704)` (1.8): host flag 5 bit 15.
11. `sub_026e83`: `wlc_bmac_mhf(hw, 1, 0x0001, wlc+0x708 != 0, 3)` (host
    flag 2 bit 0; cleared in the session).
12. `wlc_rfaware_lifetime_set(wlc, wlc+0x5f8)`: the lifetime is kept (800
    in the session) unless one of the words `wlc+0x350`, `+0x34c`,
    `+0x348`, `+0x344` or `+0x5dc` is not 0 (then 0 is kept and the
    function's result is an error for a non-zero request); with the driver
    up: `wlc_bmac_suspend_mac_and_wait(hw)`, `SHM(0x7c)` = the lifetime,
    `wlc_bmac_mhf(hw, 3, 0x0020, lifetime != 0 ? 0x0020 : 0, 3)` (host flag
    4 bit 5), `wlc_bmac_enable_mac(hw)`. (Observed: `SHM(0x7c)` = 0x0320.)
13. **`wlc_bmac_up_finish(hw)`** (C4): dynamic clock, `wlc_phy_hw_state_upd`,
    interrupt mask on (`D11(0x12c)` = 0xb0e7a860), energy detect (R1).
14. `sub_027e71`: if `wlc+0x31` is set: `SHM(0x300 + 2i)` = `wlc+0x2be +
    2i` for i = 0..3 (0x0600 each in the session: long retry limit 6, the
    other fields 0 because the model's shared memory reads 0; on the card
    the fields of step 1.4.33).
15. Station rates validated (no access); the watchdog timer is armed
    (period 1000 ms, periodic; section 3); `wlc+0x2b3` = 1.
16. `wlc_stf_phy_txant_upd` (transmit antenna, R8 `wlc_bmac_txant_set`: no
    access on revision 42).
17. `sub_0396ff(wlc, stf+0x13)`: stores the LDPC setting; with the driver
    up: beacon/probe response templates updated (nothing for a station)
    and `wlc_phy_ldpc_override_set(pi, stf+0x13 != 0)` (acphy-init; 1 in
    the session).
18. `wlc_stf_chanspec_upd(wlc)`: for PHY type 11 with the driver up,
    `sub_1731cf` (2.3): duty cycle words and `SHM(0xd6)`.
19. `wlc_update_brcm_ie`, `wlc_update_beacon`, `wlc_update_probe_resp`:
    no access for a station (observed).
20. LEDs: `wlc_led_up`, `wlc_led_event` -> `wlc_bmac_led(hw, 1 << pin,
    on ? 1 << pin : 0, active_high)` per LED (R4) by the LED behaviours of
    the SROM (session: pin 0 off, pins 1 and 2 on).
21. `wlc+0x719` = 1. If `wlc+0x71a` (MAC revision >= 40): v = `D11(0xa00)`
    (16 bit); if bit 0 of v is set, `D11(0xa00)` = v & 0xfffe; `wlc+0x719`
    = bit 0 of the value then (0). (Observed: read 0, no write.)
22. If `wlc+0x5c1` = 1 and the PHY type is 2: `wlc_phy_freqtrack_start`
    (not the AC-PHY). Chip 0x4335 only: more. Result 0.

### 1.6 The response-rate table: wlc_rate_lookup_init and wlc_set_ratetable

The microcode answers a frame (ACK, CTS, block ACK) at a rate chosen from
the rate of the frame received: the table `SHM(0x1e0..0x1fe)` (OFDM, by the
low 4 bits of the PLCP rate code) and `SHM(0x220..0x23e)` (CCK, by the low
4 bits of the code `rate_info[rate]`, rate.md) holds, for every rate of the
band, the word address of the rate table entry of its response rate (a
word of the directories of 1.4 step 4).

**`wlc_rate_lookup_init(wlc, rs)`** fills a map `m[0..108]` of the current
band (`band+0x84`, 109 bytes, rate in 500 kbit/s units) from the rate set
`rs` (whose rates carry bit 7 = basic):

1. `m` = 0; for every rate of `rs` with bit 7 set (and rate & 0x7f < 109):
   `m[r]` = r (a basic rate answers at itself).
2. Two running values, last basic CCK rate `bc` = 0 and last basic OFDM
   rate `bo` = 0. For each rate r of the band's hardware rate set
   (`band+0x5c` count, `band+0x60` rates, ascending), OFDM meaning bit 7 of
   `rate_info[r & 0x7f]`:
   * `m[r]` != 0 (r is basic): if OFDM, `bo` = r, else `bc` = r.
   * `m[r]` = 0: `m[r]` = `bo` if OFDM, else `bc`; if that is 0: OFDM
     rates take the mandatory rate at or below them, 12 (6 Mbit/s) if r <
     24, 24 (12 Mbit/s) if r < 48, else 48 (24 Mbit/s); CCK rates take r
     itself.

With the default rate sets (rate.md: basic 1, 2, 5.5, 11 Mbit/s in 2.4 GHz,
6, 12, 24 Mbit/s in 5 GHz) this gives: 2.4 GHz 1/2/5.5/11 -> themselves;
6, 9 -> 6; 12, 18 -> 12; 24..54 -> 24; 5 GHz 6, 9 -> 6; 12, 18 -> 12;
24..54 -> 24 (read; the map is not visible in the model because the
directories read 0).

**`wlc_set_ratetable(wlc)`**: rs = the hardware rate set of the current
band (`sub_0256db`) copied and its MCS set reduced to the transmit streams
(`wlc_rateset_mcs_upd(rs, stf+2)`, rate.md). For each legacy rate r of rs
(rate & 0x7f) in order:

1. b = `m[wlc_rate_rspec_reference_rate(r)]` (rate.md); if b = 0, b = the
   first rate of rs.
2. The word written is the directory entry of b: `wlc+0x454 + 2 *
   (rate_info[b] & 0xf)` (copy of `SHM(0x1c0 + ...)`) if b is OFDM, else
   `wlc+0x474 + ...` (copy of `SHM(0x200 + ...)`).
3. `wlc_bmac_write_shm(hw, o, word)` with o = 0x1e0 + 2 * (rate_info[r] &
   0xf) if r is OFDM, else 0x220 + 2 * (rate_info[r] & 0xf).

Observed order and offsets (2.4 GHz, 12 rates 1 .. 54 Mbit/s): 0x234,
0x228, 0x22e, 0x1f6, 0x1fe, 0x23c, 0x1f4, 0x1fc, 0x1f2, 0x1fa, 0x1f0, 0x1f8;
5 GHz (8 OFDM rates): 0x1f6, 0x1fe, 0x1f4, 0x1fc, 0x1f2, 0x1fa, 0x1f0,
0x1f8. The values are 0 in the model (directories read 0).

### 1.7 sub_029597: beacon and promiscuous bits, after a channel or band setting

1. If the home chanspec (`wlc+0x516`) equals the PHY's current chanspec
   (`wlc_phy_chanspec_get(pi)`): an enabled IBSS configuration is enabled
   again (`wlc_ibss_enable`; not for a station); if associated
   (`pub+0x59`) and the PHY type is 4, 6, 7, 8, 10 or 11:
   `SHM(0x1c)` = the 16-bit word `band+0x10e` (section 5). Otherwise an
   enabled IBSS is disabled.
2. `wlc_mac_bcn_promisc(wlc)`: `wlc_bmac_mctrl(hw, 0x00100000, x)` with x =
   0x00100000 if the low 24 bits of `wlc+0x28c` are not 0 (someone wants
   all beacons), else 0 (section 6).
3. `wlc_mac_promisc(wlc)`: section 6 (MAC control bits 22 and 24, and
   probe-request reception).

In the session every call leaves both bits clear (no `D11(0x120)` write:
the copy does not change).

### 1.8 wlc_enable_probe_req(wlc, mask, value)

`wlc+0x704` = (`value` & `mask`) | (`wlc+0x704` & ~`mask`). With the driver
up: `wlc_bmac_mhf(hw, 4, 0x8000, w ? 0 : 0x8000, 3)` with w the new
`wlc+0x704`: host flag 5 bit 15 set = the microcode does **not** pass probe
requests up; cleared while any requester (bit 0..2; bit 2 = promiscuous
mode) wants them. (Observed: 0x8000 at every call of the session.)

### 1.9 State after the first up (observed, session)

* MAC control copy 0x40020403 (`D11(0x120)`): EN_MAC, PSM_RUN, IHR_EN,
  INFRA, DISCARD_PMQ (bit 30); WAKE clear; AP, promiscuous and beacon
  bits clear.
* Host flags (the words of A9; values as the hardware gets them, the copies
  of the current band, 2.4 GHz): host flag 1 = 0x0100 (WME), 2 = 0, 3 =
  0x0040 (A-MPDU), 4 = 0x0060 (bit 6 from `wlc_stf_ss_update`, bit 5 frame
  lifetime), 5 = 0x8081 (bit 0 coexistence flags of the Apple 4360,
  bit 7 1.2 step 4, bit 15 no probe requests). (In the model the key-table
  clear of 1.4 step 9.1 hits offset 0, because `SHM(0x56)` reads 0, and
  zeroes the host-flag words written by `wlc_bmac_init`; on the card the
  key table lies elsewhere.)
* Interrupt mask 0xb0e7a860.
* `AMT[0..49]` cleared, `AMT[62]` cleared (BSSID 0), `AMT[63]` = own
  address with the attribute word 0x8008 OR-ed into the old one (B5).
* `SCR(6)` = 7, `SCR(7)` = 6 (retry limits), `SHM(0x300..0x306)` = retry
  words, `SHM(0x7c)` = 800, `SHM(0x82)` = 10000, `SHM(0xba)` = 0xffff,
  `SHM(0x3c)` = 10, `SHM(0xb6)` = 0, `SHM(0x74)` = 0, duty cycle words 0,
  the response-rate table, the beamforming block, `D11(0x688)` bit 2,
  `D11(0x3dc)` = 10000000.

## 2. Channel change

In the session the channel is changed by the iovar `chanspec` (the glue's
cfg80211 channel setting does the same through `WLC_SET_CHANNEL`/the iovar;
the scan uses `wlc_set_chanspec` directly, section 6).

### 2.1 The iovar handler and wlc_set_chanspec (.text+0x03a4be)

**Iovar `chanspec` (set)** (`wlc_doiovar`, id 0xeb; read, observed):

1. A malformed chanspec (`wf_chspec_malformed`) or one not valid in the
   current country (`wlc_valid_chanspec_db`, channel.md): error -20.
2. Driver down, two bands, band not locked: `wlc+0x24f` = 1 if the band of
   the chanspec differs from the current band (a band switch for the next
   up, 1.5 step 9).
3. `defbss+0x32` = the chanspec (the default chanspec of 1.4 step 1).
4. Only if the driver is up, not associated, and the chanspec differs from
   the PHY's current one (`wlc_phy_chanspec_get(pi)`):
   `wlc_set_home_chanspec(wlc, c)` (1.4 step 15),
   **`wlc_bmac_suspend_mac_and_wait(hw)`**, `wlc_set_chanspec(wlc, c)`,
   **`wlc_bmac_enable_mac(hw)`**. Otherwise nothing touches the hardware
   (observed: the step "1" of the session, the channel of the up, makes no
   call).

**`wlc_set_chanspec(wlc, c)`** (with the MAC suspended by the caller):

1. If c is not valid (`wlc_valid_chanspec_db`) and the scan is not in a
   state that allows it (bit 1 of the byte +9 of the scan state): only the
   OLPC update of step 13.
2. Unless an associated infrastructure configuration is in a particular
   state (the word +8 of its structure `+0x320` = 2): `cca_stats_upd(wlc, 1)`
   (cca-stf.md: the counters of the old channel are closed).
3. If c equals the current chanspec (`wlc+0x518`): return.
4. **Band switch**, when there are two bands and the band of c (unit 1 for
   `c & 0xc000` = 0xc000) differs from the upper layer's band (or a band
   switch is pending, `wlc+0x24f`): with the band locked (`wlc+0x24e`):
   nothing but step 13. Else the upper layer's band becomes the band of c
   (`wlc+0x40` = `wlc+0x50`/`wlc+0x58`), and if the driver is up: if an
   associated configuration exists, `wlc_set_pmawakebcn(cfg, 1)` (section
   5), then `sub_039dd4(wlc)` (1.4 step 23: the response-rate table of the
   new band, `wlc_set_ratetable`; MAC control bits and probe requests,
   `sub_029597`). The MAC layer's band switch happens in step 5, inside
   `wlc_bmac_set_chanspec`. (Observed for 11 -> 36: the eight OFDM entries
   of the table written before the channel.)
5. **`sub_03649a(wlc, c)`** (2.2): channel, power limits, spatial policy.
6. If the bandwidth class of c differs from the old one (5/10, 20, 40, 80,
   160 MHz): `wlc_antsel_init` (nothing here) and
   `wlc_rateset_bw_mcs_filter(band+0x5c, bw)` (rate.md; bw = 0x14 for 20,
   0x28 for 40, 0x50 for 80, 0xa0 for 160 MHz, 10 otherwise).
7. `sub_029597(wlc)` (1.7): beacon/promiscuous MAC control bits, probe
   requests (host flag 5 bit 15), and, if associated on the home channel,
   `SHM(0x1c)`.
8. After a band switch while up with two bands: frequency tracking for PHY
   type 2 only (not the AC-PHY).
9. Unless step 2 was skipped: `cca_stats_tsf_upd(wlc)` (cca-stf.md; reads
   the TSF, `wlc_bmac_read_tsf`).
10. If 802.11n is enabled: a counter `wlc+0x2a8` += 1 (no access).
11. **`wlc_bmac_ifsctl_edcrs_set(hw, band PHY type == 7)`** (R1; the
    argument is 0 for the AC-PHY and has no effect: energy detect for the
    bandwidth of c).
12. (Return.)
13. If the OLPC engine is active (`pub+0x140`):
    `wlc_olpc_eng_hdl_chan_update` (channel-power.md; it calls
    `wlc_channel_reg_limits`, `wlc_phy_txpower_sromlimit`,
    `wlc_phy_tssivisible_thresh`, no access in the session).

### 2.2 sub_03649a (assigned: wlc_set_phy_chanspec) and wlc_channel_set_chanspec

**`sub_03649a(wlc, c)`** (observed in this order at every channel change and
in `wlc_init`):

1. `wlc+0x518` = c.
2. q = `wlc_tpc_get_local_constraint_qdbm(tpc)`: 127 (no local power
   constraint) unless associated with an AP that announced a power
   constraint, c's control channel being the home channel's; section 5.
3. **`wlc_channel_set_chanspec(cm, c, q)`** (below).
4. `wlc_stf_chanspec_upd(wlc)` (2.3).
5. If the automatic single-stream selection is on (`stf+0x0d`):
   `wlc_stf_ss_algo_channel_get(wlc, &stf+0x0e, c)` (cca-stf.md).
6. `wlc_stf_ss_update(wlc, band)` (cca-stf.md: single-stream mode; it calls
   `wlc_mhf(wlc, 3, 0x0040, ...)` - host flag 4 bit 6 - for the current
   band).
7. **`wlc_bmac_txbw_update(hw)`** (bmac-init B12: the OFDM entries' PHY
   control words of the rate table, read and written back).

**`wlc_channel_set_chanspec(cm, c, q)`** (wlc_channel.c):

1. `p = ppr_create(osh, bw)` with bw = 2 for 80 MHz, 1 for 40 MHz, else 0
   (ppr.md). If that fails: nothing.
2. `sub_06f334(cm, c)`: reads the iovar `phy_afeoverride` and writes it
   back with bit 1 cleared, set only for 5 GHz on the board type 0x135 with
   two particular country/revision pairs; `stf+0x8a` = (value != 0) at 20
   MHz, else 0. (The open PHY refuses the iovar, -23: nothing reaches the
   hardware. Observed.)
3. **`wlc_channel_reg_limits(cm, c, p)`**: the regulatory per-rate limits
   of c into `p` (channel-power.md); it calls `wlc_bmac_filter_war_upd(hw,
   0)` -> `wlc_phy_set_filt_war(pi, 0)` (R8, acphy-api A3).
4. `ppr_apply_max(p, q)`: no limit above the local constraint.
5. `sub_0736a4(cm, p)`: further limits (channel-power.md).
6. m = `wlc_quiet_chanspec(cm, c)`: 1 if one of the 20 MHz channels of c
   is in the country's quiet (radar) channel set (a bit vector at `cm+0x88`,
   bit n = channel n; for 80 MHz the channels c-6, c-2, c+2, c+6, for 40 MHz
   c-2, c+2, for 20 MHz c; only with 802.11h/n). In the session every 5 GHz
   channel is quiet (bmac-run Q7).
7. **`wlc_bmac_set_chanspec(hw, c, m, p)`** (acphy-chanspec section 1):
   clock forced, the MAC layer's band switch with the band initialisation
   (bmac-init B10, the host-flag words of the new band, `D11(0x684)` and
   `SHM(0x10)` slot timing, ...), `wlc_phy_chanspec_set`,
   `wlc_phy_txpower_limit_set(pi, p, c)` (the transmit power of the
   channel: acphy-api-txpower; the PHY then calls back
   `wlc_update_txppr_offset`, cca-stf.md, which writes the per-rate offsets
   and `SHM(0x5d4..0x5de)`), and `wlc_bmac_mute(hw, m, 0)` (bmac-init E4:
   a quiet channel suspends the transmit FIFOs and clears `AMT[63]`; a
   normal one resumes them and writes the own address back).
8. `ppr_delete(osh, p)`.

### 2.3 Duty cycle words: wlc_stf_chanspec_upd, sub_1731cf

`wlc_stf_chanspec_upd(wlc)`: only with the driver up and PHY type 11:
`sub_1731cf(wlc)`, which, with t = the throttle state `stf+0x50`:

1. duty = 100; if bit 1 of t is set and `stf+0x78` <= 100: duty =
   `stf+0x78`.
2. If bit 0 of t is clear: w = 0 if t is 0, else 0xffff. Else: w = 0xffff
   unless the MAC revision is >= 40, the board is not Apple's 0xc00/0xc01
   and c is a 5 GHz chanspec of 40 MHz with `stf+0x6e` != 0 (then w =
   `stf+0x70`) or of 80 MHz with `stf+0x72` != 0 (then w = `stf+0x74`);
   and duty = min(duty, `stf+0x51`).
3. With the driver up: `SHM(0xa4)` = d(duty) (CCK), `SHM(0xb4)` = d(duty)
   (OFDM), with d(x) = (100 - x) * 16 / x as in 1.4 step 18 (0 for x = 100);
   MAC revision >= 40: `SHM(0xd6)` = w.

Observed (t = 0 in the session): `SHM(0xa4)` = 0, `SHM(0xb4)` = 0,
`SHM(0xd6)` = 0 at the up and after every channel.

### 2.4 Order of a channel change (observed, channel 11 -> 36, 2.4 -> 5 GHz)

MAC suspended (iovar) -> CCA statistics closed -> response-rate table of
the new band, MAC control/probe-request host flag -> regulatory limits
(`wlc_phy_set_filt_war(pi, 0)`) -> `wlc_bmac_set_chanspec`: interrupts off,
band switch with band initialisation (host flags of the 5 GHz copies,
initialisation list, CW, slot), interrupts on, PHY channel (calibrations
included), transmit power limits (per-rate offsets, core words
`SHM(0x5d4..0x5de)`), mute (quiet channel: FIFOs 1, 3, 0, 2 suspended,
`AMT[63]` cleared) -> duty cycle words -> single-stream mode (host flag 4
bit 6) -> `wlc_bmac_txbw_update` -> MAC control bits again -> TSF read for
the CCA statistics -> energy detect (`D11(0x6c6)`, `SHM(0x5a)` at 20 MHz;
`D11(0x6c8)` at 40/80 MHz, R1) -> OLPC update -> MAC enabled (iovar).
A change within a band has the same order without the rate table and the
band initialisation.

## 3. Periodic work: the watchdog

`wlc_up` arms a periodic timer of 1000 ms (1.5 step 15) whose handler
(`sub_046432`) calls `sub_0450f0` (assigned: `wlc_watchdog`) once per
tick. What it does to the hardware, in order (read; the steps marked were
observed in the session's watchdog steps on 5 GHz and in a run of 35 ticks
on channel 6, `re-out/analysis/wlc-hw/upper-wd.txt`):

1. Nothing if the driver is not up (`pub+0x34`).
2. `wlc_hw_deviceremoved(hw)` (C14; observed): if the card is gone, the
   glue's `wl_down` and return.
3. A pending "stay awake" countdown that ran out (`wlc+0x830` = 1 with
   `wlc+0x2ad`): `wlc_set_wake_ctrl(wlc)` (section 5) and an abort of a
   running scan.
4. Associated: for the BSS channel, the lowest RSSI of the associated
   stations goes to `wlc_phy_interf_rssi_update(pi, chanspec, rssi)`
   (acphy-api-rx; the PHY's interference mitigation). (Not in the session.)
5. Chip 0x4335 only: BB PLL. `pub+0x48` (tick counter) += 1.
6. Radio state: `wlc_radio_mpc_upd` (MPC, no access when MPC is off),
   `sub_030b4b` (`wlc_bmac_radio_read_hwdisabled(hw)`, C13, observed every
   tick; the hardware switch into bit 1 of `pub+0x84`), `wlc_radio_upd`
   (radio switched by the result: down -> `wlc_radio_disable`, back ->
   the glue's `wl_up`). If `pub+0x84` is not 0: return.
7. Bluetooth coexistence, 2.4 GHz band only (observed on channel 6):
   with a coexistence mode other than 0, `wlc_enable_btc_ps_protection`;
   MAC revision > 14: `wlc_bmac_btc_period_get(hw, ...)` and
   `wlc_bmac_btc_params_get(hw, 0x27)` (both still Broadcom's, `wlc_bmac.c`,
   reads only): with `p` = the coexistence base (`btc+0x1a`, bmac-init B1
   step 32; 0x1454 in the model): `SHM(p + 8)` is read, and if it is not 0
   `SHM(p + 0x90)`; the BT period is the first value when the second is >
   4, else 0; then `D11(0x120)` is read and, if PSM_RUN (bit 1) is set,
   `D11(0x6c2)` (16 bit): five ticks in a row with a value other than
   0xffff mark Bluetooth as active (`btc+0x16`), 0xffff clears it.
   `wlc_bmac_btc_params_get(hw, n)` = `SHM(p + 2n)` for n < 0x77 (here
   `SHM(p + 0x4e)`), 0x0bad without a base. The values drive the upper
   layer's coexistence policy (A-MPDU sessions, receive chains on other
   chips; on the 4360 with mode 0: A-MPDU state only, no access). The
   iovar `phy_btc_restage_rxgain` is set from that policy when the
   coexistence state asks for it (mode 0: never).
8. **`wlc_bmac_watchdog(wlc)`** (R7; observed): FIFO error check
   (`D11(0x20)`..`D11(0x48)` read), receive refill of FIFO 0, and
   **`wlc_phy_watchdog(pi)`** (acphy-api-watchdog: calibrations,
   temperature, noise, interference mitigation).
9. Every 30 ticks (`pub+0x48 % 30` = 0): `wlc_statsupd(wlc)` (observed at
   tick 30): with the driver up and `wlc+0x31` set,
   `wlc_bmac_copyfrom_objmem(hw, 0xe0, buf, 0x80, 0x10000)` (bmac-init B4):
   the 64 counter words `SHM(0xe0)`..`SHM(0x15e)` of the microcode. Each is
   a 16-bit wrapping counter; the difference to the previous reading (a
   copy kept by the driver, cleared by `wlc_reset`) is added to a 32-bit
   counter of the driver's statistics (`wl_cnt_t`, `src/include/wlioctl.h`:
   words 0..5 to the six counters from offset 0xc4 on, words 6..11 to those
   from 0xdc, and so on; the mapping of every word was not followed).
   `wlc_reset` (1.3) makes the same read before each core reset when up.
10. Counter history, frequency tracking (PHY type 2 only).
11. Associated, power save allowed, infrastructure, BSSID set: a null data
    frame (`wlc_sendnulldata`, keep-alive; section 5). Beacon-loss
    accounting, `wlc_set_uatbtt` (section 5), roaming, link-loss events
    (no access of their own).
12. IBSS rate probes every 60 ticks (not for a station).
13. The registered modules' watchdog functions, in registration order; in
    the session the LED module (`sub_07d1a0`: `wlc_led_event` ->
    `wlc_bmac_led(hw, 1 << pin, ..)` per LED, R4; observed each tick) and
    the channel statistics (cca-stf.md part 1). A module returning not 0
    ends the tick here.
14. `wlc_stf_pwrthrottle_upd(wlc)` (observed, no access): acts only if the
    board has a throttle input (`stf+0x69`/`stf+0x6a`); with `stf+0x4c` =
    -1 (Apple 4360 boards) and `stf+0x6a` = 0 it reads no GPIO and changes
    nothing; otherwise `si_gpioin` decides the throttle bit 0 of
    `stf+0x50`, and a change re-applies the receive chains and duty cycle
    (2.3).
15. MIMO power-save action frames (associated, HT), and a pending PM
    state change (`wlc+0x6d8`): `sub_03fc00` (section 5).
16. `wlc_stf_tempsense_upd(wlc)` (observed): for PHY types 4, 7, 11 unless
    disabled (`stf+0x38`), every `stf+0x3c` ticks (10, or the SROM
    variable `temps_period` 1..14): a = `wlc_phy_stf_chain_active_get(pi)`
    & 0xf (acphy-api-rx; 0x33 -> 3 in the model). If a has fewer chains
    than `stf+0` (`hw_txchain`), bit 1 of `stf+0x50` is set (else cleared);
    on a change with the driver up: `sub_1731cf` (2.3: the duty cycle
    becomes `stf+0x78`, 50 on Apple 4360 boards: `SHM(0xa4)` =
    `SHM(0xb4)` = 16) and `wlc_stf_txchain_set(wlc, chains, ..)` (the
    transmit chains reduced to a, or restored; cca-stf.md and the PHY's
    `wlc_phy_stf_chain_set`).

The watchdog of the MAC layer itself (R7) and of the PHY
(acphy-api-watchdog) are where nearly all of the hardware work of a tick
happens; the upper layer adds the radio-switch poll, the coexistence reads
(2.4 GHz), the counters every 30 s, the LEDs and the temperature-driven
chain/duty-cycle policy.

## 4. Down and detach

### 4.1 wlc_down (.text+0x036bae)

Nothing if a down is in progress (`wlc+0x252`) or the driver is not up.
Else (observed, both downs of the session):

1. `wlc+0x252` = 1. (Chip 0x4335: BB PLL.)
2. **`wlc_bmac_down_prep(hw)`** (C5: interrupts off, fast clock,
   `wlc_phy_down`).
3. If the card is present (`wlc_hw_deviceremoved`): scan aborted;
   association aborted; an enabled primary configuration is disabled
   (`wlc_bsscfg_disable`, section 5: BSSID cleared, ...) followed by a
   delay of 4000 us (with the byte `pub+0x5d`: `wlc_bsscfg_down` and the
   quiet channels reset instead).
4. The modules' down functions in registration order; with hardware
   calls: the scan module (`wlc_scan_down`: `wlc_phy_hold_upd(pi, 2, 0)`,
   acphy-api A13; observed), the channel statistics (cca-stf.md).
5. Timers deleted (watchdog and two more).
6. `pub+0x34` = 0. **`wlc_phy_mute_upd(pi, 0, 0xffffffff)`** (acphy-api
   A14; observed).
7. `wlc_led_down`: LEDs updated for "down" (`wlc_bmac_led(hw, 1 << pin, 0,
   ..)`: all off in the session; observed).
8. Transmit queues flushed, events flushed (no access).
9. **`wlc_bmac_down_finish(hw)`** (C6: MAC suspended, the glue's
   `wl_reset` -> `wlc_reset` (1.3: no counter read now, the driver is down;
   `wlc_bmac_reset`), core disabled, `wlc_bmac_hw_down`).
10. `wlc+0x31` = 0; stations cleared if no configuration is enabled;
    `wlc+0x252` = 0.

After a down, an up repeats 1.2 without the first-up steps (no
`wlc_bmac_hw_up`, no microcode download: bmac-init C16), and all of 1.3 -
1.5 (observed: the second up of the session makes the same upper-layer
calls as the first, with the same values).

### 4.2 wlc_detach (.text+0x03705c)

The glue downs the interface first (in the session `down` precedes
`detach`). Hardware-facing (observed):

1. `wlc_led_deinit` -> `wlc_bmac_led_hw_deinit(hw, mask)` (R4: LED GPIOs
   released; it switches the crystal on and off around it).
2. The modules' detach functions (`wlc_cca_detach`, ...; the stf state's
   power containers `ppr_delete`): no access.
3. **`wlc_bmac_detach(wlc)`** (bmac-attach: DMA, PHY, shim; no access).
4. `wlc_bmac_si_detach(osh, sih)` -> `si_detach` (chip.md).

## 5. Joining a network and the BSS state

The session does not join; the order of this section is read from the
code (`wlc_assoc.c`, `wlc.c`, `wlc_bsscfg.c`). The procedures
`wlc_tsf_adopt_bcn` (5.3), `wlc_pretbtt_set` (5.2 step 15), `wlc_set_bssid`
with a BSSID (5.2 step 7), `sub_02c636` (5.4) and
`wlc_clear_hw_association` (5.7) were run by direct calls on the object
after attach and up, with the values given there (`verify5.py`). The station's
configuration is `bsscfg` (Data); `bss` = the BSS description the
configuration adopts (`bsscfg+0x318`; +0x00 BSSID, +0x2e beacon interval in
TU, +0x32 chanspec, +0x38 rate set, +0x60 DTIM period, +0x62 capability
word); the target BSS of a join is `bsscfg+0x310`.

### 5.1 The order of a join, hardware-facing

1. Authentication and association requests are frames (frames.md); the
   scan that precedes them is section 6. While joining, the WAKE bit is
   kept (`wlc_stay_awake` is true while the association state machine is
   busy, 5.6).
2. **Association response accepted** (`wlc_assocresp_client`):
   1. `wlc_set_pmoverride(bsscfg, 0)`, `wlc_update_bcn_info(bsscfg, 0)`
      (PS control, 5.6).
   2. **`sub_05cd9c(bsscfg)`** (assigned: `wlc_join_bss_done`):
      `wlc_bmac_suspend_mac_and_wait(hw)`; **`wlc_BSSinit(wlc, target,
      bsscfg, 0)`** (5.2); `wlc_bmac_enable_mac(hw)`;
      `wlc_sta_assoc_upd(bsscfg, 1)` (5.5); with WME and a QoS BSS (bits
      0x102 of `bsscfg+0x100` = 0x100): `wlc_edcf_acp_apply(wlc, bsscfg, 1)`
      (5.4); `wlc_link` (event); the OLPC channel update if active
      (channel-power.md); supplicant set-up; association state 0x0c; LED
      event.
   3. Host flag 4 bit 0: `wlc_mhf(wlc, 3, 0x0001, x, 3)` with x = 1 if bit
      0 of the AP's station entry word +0x0a is clear and its word +0x3c is
      2, else 0 (meaning of both unknown).
   4. If that bit 0 is clear and (MAC revision < 15, or bit 5 of byte +0x1b of
      `sih` is clear, or bit 5 of the byte `wlc+0x233` (MAC capabilities
      kept by 1.1 step 5, bit 29) is clear): `wlc_mhf(wlc, 2, 0x0100, 0, 2)`
      (host flag 3 bit 8 cleared in the 2.4 GHz copy).
   5. PHY type 7, or 4 with MAC revision > 15: `wlc_bmac_ifsctl_edcrs_set`
      (not the AC-PHY).
   6. Beamforming link (later specification), keys (later).
3. **First beacon of the BSS** (`wlc_join_complete`, from the receive path
   when the station waits for it):
   1. **`wlc_tsf_adopt_bcn(bsscfg, .., beacon)`** (5.3): TSF, beacon period,
      next TBTT.
   2. For an infrastructure BSS whose beacon has a TIM element (id 5): the
      DTIM period (byte 3 of the element) is kept (`bss+0x60`) and, for the
      primary configuration, **`SHM(0x12)` = DTIM period**;
      `wlc_update_bcn_info(bsscfg, 1)`. Without TIM: `wlc_set_pmoverride(
      bsscfg, 1)`. (IBSS: the BSS set-up of 2.2 instead; not described.)
   3. Country element adopted (regulatory, channel-power.md).
   4. If power save is allowed (5.6): `wlc_set_pmstate(bsscfg, 1)`; else
      `wlc_set_ps_ctrl(bsscfg)`.

### 5.2 wlc_BSSinit (.text+0x03ae9f) for a station

1. `wlc_bsscfg_set_infra_mode(wlc, bsscfg, target is infrastructure)`:
   `bsscfg+0x22`; for IBSS bits 5..6 of `bsscfg+0x100` set.
2. Beacon-loss counters cleared; TPC state reset unless associated (the
   local power constraint of 2.2 step 2 returns to "none"); frequency
   tracking reset.
3. The target BSS description is copied into `bss`.
4. `wlc_BSSinit_rateset_filter(bsscfg)`: the BSS rate set is filtered to
   the hardware rate set of the band (`wlc_rateset_filter`,
   `wlc_rate_hwrs_filter_sort_validate`, rate.md) and
   **`wlc_rate_lookup_init(wlc, bss rate set)`** (1.6): the response-rate
   map from the **BSS's basic rates**.
5. Channel: if the PHY's chanspec (`wlc_phy_chanspec_get`) is not the BSS's
   (`bss+0x32`) - unless they share the control channel and the BSS's
   bandwidth is narrower than the current one -: `wlc_set_home_chanspec(wlc,
   bss chanspec)`, **`wlc_set_chanspec(wlc, bss chanspec)`** (section 2, with
   the MAC suspended by 5.1 step 2.2). Otherwise the home chanspec becomes
   the PHY's chanspec.
6. Beacon interval 0 in the description is taken as 100 TU. The BSSID is
   copied to `bsscfg+0xf0` and `bsscfg+0x114`.
7. **`wlc_set_bssid(bsscfg)`** (1.4 step 13: `AMT[62]` = BSSID with
   attribute 0x8006 for an infrastructure BSS; `TPL(0x30..0x43)` = BSSID,
   own address, BSSID); **`wlc_set_mac(bsscfg)`** (`AMT[63]`, `TPL(0x48)`).
8. **`wlc_set_ratetable(wlc)`** (1.6): the response-rate table from the map
   of step 4.
9. **Slot time**, if the byte `band+0x19` is set (by its use the 802.11g
   mode of the 2.4 GHz band; unverified): `wlc+0x532` = the
   override `wlc+0x533` (1 = short) if it is not -1, else bit 10 of the
   BSS capability word (short slot time); **`wlc_bmac_set_shortslot(hw,
   wlc+0x532)`** (bmac-init E6: on 2.4 GHz with the driver up,
   `D11(0x684)` = 0x0207 and `SHM(0x10)` = 9 for a short slot, 0x0212 and 20
   for a long one; the 5 GHz band always has the short slot, B10 step 8).
10. ERP and HT protection set-up (`wlc_prot_g_init`, `wlc_prot_n_init`): no
    hardware access; protection decides the transmit headers (RTS/CTS to
    self, frames.md). The short preamble likewise is a transmit-header
    matter. (`wlc_prot_n` may call `wlc_phy_preamble_override_set`; not in
    a station's normal path.)
11. IBSS only: beacon/probe templates, `D11(0x190)` = ATIM window; a TSF
    reset for a new IBSS (`D11(0x180)` = `D11(0x184)` = 0, `D11(0x188)` =
    beacon interval << 16 | ATIM flag, `D11(0x18c)` = beacon interval << 10).
    Not described further.
12. `wlc_phy_BSSinit(pi, 0, -57)` (acphy-api A8; noise reset) and the
    receive signal averages reset.
13. **`wlc_macctrl_init(wlc, bsscfg)`**: `wlc_bmac_mctrl(hw, 0x48020000,
    v)` with v = 0x40020000 (INFRA and bit 30) for an infrastructure
    configuration; for IBSS 0x40000000 (0x40020000 with `wlc+0x300` set and
    `wlc+0x303` clear); bit 27 (0x08000000) is added when bit 6 of
    `bsscfg+0x100` and the byte `bsscfg+0x53` are set (here or on the
    associated configuration). For a station the bits are those of B1 step
    22 already: no access.
14. Unless bit 2 of `bsscfg+0x101`: the BSS's rate set is filtered into the
    band's rate set of the home chanspec's band (no access).
15. **`wlc_pretbtt_set(bsscfg)`**: p = 1024 (us) for an infrastructure
    configuration on the AC-PHY (0x80 on chip 0x4335; other PHY types 100,
    0x200, 0x2ee, 800, 0xfa), 2 for IBSS/AP; `D11(0x612)` = p (16 bit),
    `SHM(0x96)` = p.
16. Admission control on join (CAC; no access).

### 5.3 TSF and TBTT: wlc_tsf_adopt_bcn, wlc_tbtt_align

The beacon body begins with the 8-byte timestamp (little endian) and the
2-byte beacon interval B (TU; 0 is taken as 1024). Let T = the timestamp,
P = B * 1024 (microseconds), o = T mod P (`wlc_calc_tbtt_offset`: computed
from T in units of 1024 us modulo B, times 1024, plus the low 10 bits of T).

**`wlc_tsf_adopt_bcn(bsscfg, .., body)`**, only for the primary
configuration:

1. `wlc_bmac_mctrl(hw, 0x10000000, 0x10000000)`: MAC control bit 28 set
   (holds the TBTT logic while the timer is written; brcmsmac name
   TBTT_HOLD).
2. `D11(0x180)` = low 32 bits of T, `D11(0x184)` = high 32 bits (the TSF).
3. `D11(0x188)` = B << 16 (beacon period; the low half 0).
4. `D11(0x18c)` = n, the next TBTT in TSF microseconds (low 32 bits):
   n = (T_low - o) + P * floor((o + 0x18fff + P) / P), 32-bit unsigned
   arithmetic: the first TBTT (a multiple of P in TSF time) later than
   T + 0x18fff, i.e. more than about 100 TU after the beacon's timestamp.
5. `wlc_bmac_mctrl(hw, 0x10000000, 0)`.

**`wlc_tbtt_align(bsscfg, short_preamble, rate, body)`** (called from the
beacon receive path while associated, when the station must re-align after
power save, `wlc+0x2ac` "update at TBTT"): `D11(0x18c)` = the same formula,
but first, when `wlc+0x2ac` is set, T is reduced by the air time of the
beacon up to its timestamp (`wlc_compute_bcntsfoff`: the preamble, 192 us
for CCK with the long preamble, 96 with the short one, 20 us for OFDM, plus
the time of the frame bytes before the timestamp at the beacon's rate,
`wlc_compute_bcn_payloadtsfoff`), and if the offset in TU (o >> 10) is at least `wlc+0x290`
- 1, n is increased by o. (Read.)

Before a join the beacon period register is the default of B1 step 23
(`D11(0x188)` = 0x80000000); disassociation writes it back (5.7).

### 5.4 EDCA parameters: wlc_edcf_acp_apply, sub_02c636

`wlc_edcf_acp_apply(wlc, cfg, suspend)` takes the four access-category
records of the configuration's WME parameters (`cfg+0x570`, +8: four
records of ACI/AIFSN byte, ECWmin/ECWmax byte, TXOP limit u16, as in the
WME parameter element; when WME is enabled and the station's configuration
is associated and QoS, that configuration's), and for each calls
`sub_02c636` (assigned: `wlc_edcf_setparams`); a non-zero result stops.

`sub_02c636(wlc, cfg, rec, suspend)`: a = (rec[0] >> 5) & 3 (the ACI). The
record is stored in the configuration (and in the associated one if that is
another); software copies (TXOP per AC in microseconds = TXOP << 5, the ACM
bit 4 of rec[0] in a mask). Then, only if the MAC layer is up (`wlc+0x31`)
and the configuration is associated (`cfg+0x0c`):

1. If `suspend`: `wlc_bmac_suspend_mac_and_wait(hw)`.
2. A block of 16 words, all 0, then: w0 = TXOP << 5; aifs = rec[0] & 0xf,
   plus 1 when a = 2 (video), TXOP = 0 and aifs != 15. Only if 1 <= aifs <=
   15: w1 = cwmin = (1 << (rec[1] & 0xf)) - 1; w2 = cwmax = (1 << (rec[1]
   >> 4)) - 1; w3 = cwmin; w4 = aifs; w5 = `D11(0x65a)` & cwmin (16-bit
   read: a random backoff); w6 = w5 + aifs; w7 = `SHM(base + 0x0e)` | 0x100
   (read first); and the 16 words are written to `SHM(base)`,
   `SHM(base + 2)`, ..., `SHM(base + 0x1e)` with base = 0x240 + 0x20 * f and
   f = the transmit FIFO of the access category: BE (a = 0) -> 1, BK (1) ->
   0, VI (2) -> 2, VO (3) -> 3 (Broadcom's 4-byte table at `.rodata+0x2830f4`
   = 01 00 02 03).
3. If `suspend`: `wlc_bmac_enable_mac(hw)`.

Callers: the join (5.1, suspend 1), `wlc_init` (1.4 step 24, suspend 0),
the beacon receive path when the parameter set count of the WME element
changes (suspend 1), the iovar `wme_ac_sta`.

### 5.5 Associated state: wlc_sta_assoc_upd

`wlc_sta_assoc_upd(bsscfg, on)`: on: `wlc_set_pmawakebcn(bsscfg,
wlc+0x2af)`; off: the PM state reset. `bsscfg+0x0c` = on; `pub+0x59`
(associated) = any configuration associated; **`wlc_phy_hold_upd(pi, 0x20,
!associated)`** (acphy-api A13: the PHY's "not associated" hold); the
watchdog's PS bookkeeping; `wlc_enable_btc_ps_protection` (coexistence
mode 0: nothing).

While associated (`pub+0x59`), `sub_029597` (1.7) also writes `SHM(0x1c)`
= the band word `band+0x10e` at every channel/band setting on the home
channel; for a station that word is written only by the IBSS/AP beacon
set-up (the beacon's TSF offset), so it stays 0. (Read.)

`wlc_init` when re-run while associated (re-init after a fatal error, 1.4
step 36): `D11(0x188)` = beacon interval << 16, association state 0x0c,
`wlc_set_ps_ctrl`, and with PM: `wlc_set_pmawakebcn(bsscfg, 1)` and
`wlc_dtimnoslp_set` (`SHM(0x4c)` = (PRETBTT + `wlc+0x290` * 1024) >> 3,
`wlc+0x290` being bit 7.. of the `SHM(0x4c)` read of 1.4 step 8);
`wlc_pretbtt_set`.

The AID is not written to the hardware by the upper layer (TIM bits are
evaluated by the host, `wlc_tim_process`).

The beacon listen interval: `SHM(0xb6)` = (DTIM count << 8 | beacon count)
of `wlc+0x2b2`/`wlc+0x2b1`, 0 when the DTIM count is 1 (`wlc_bcn_li_upd`;
iovars `bcn_li_bcn`, `bcn_li_dtim`; 0 by default).

### 5.6 Power save and the WAKE/PS bits

* **`wlc_set_ps_ctrl(bsscfg)`** (with the MAC layer up, `wlc+0x31`): for the
  primary configuration `wlc_bmac_mctrl(hw, 0x02000000, ps ? 0x02000000 :
  0)` with ps = `wlc_ps_allowed(bsscfg)` (MAC control bit 25, brcmsmac HPS: the host
  allows the microcode to doze); then `sub_03a14c` (WAKE bit, 1.4 step 29)
  and `cca_stats_upd(wlc, 0)`.
* `wlc_ps_allowed(bsscfg)`: infrastructure, associated, PM mode on
  (`pm+0x08`), no PM override (`pm+0x01`), byte `bsscfg+0x23` set, the AID
  word `bsscfg+0x9a` = 0 or the low 3 bits of `bsscfg+0x90` clear or byte
  `bsscfg+0x9d` set, no channel switch in progress, and the primary
  configuration (`pm` = the PM state at `bsscfg+0x338`).
* **`wlc_stay_awake(wlc)`** (the WAKE bit 26 of MAC control follows it):
  true if any of: `wlc+0x2b0`, `wlc+0x2af` (awake for the beacon after
  joining, `wlc_set_pmawakebcn`), a scan in progress (byte +0x0a of the scan
  state), the association state (word +0x0c of `wlc+0x5b0`->+8) other than
  0..3, `wlc+0x590` set, any of the three low bytes of `wlc+0x2ac` (update at
  TBTT), `wlc+0x5c2`, `wlc+0x68`, `wlc+0x61a`, `wlc+0x6c8`, the monitor
  word `wlc+0x288`; otherwise true only if the station configuration is up
  and is not the primary one. Every change of these calls
  `wlc_set_wake_ctrl(wlc)` = `sub_03a14c` and `cca_stats_upd(wlc, 0)` while
  up.
* `wlc_set_pmstate(bsscfg, on)`: PM mode stored, `wlc_set_ps_ctrl`, PM
  pending; associated with a BSSID: a null data frame with the PM bit
  (`wlc_sendnulldata`, frames.md) unless suppressed, else the PM pending
  state completes at once; PS-poll and APSD timers.
* `wlc_set_uatbtt(bsscfg, on)`: `wlc+0x2ac` = on (with other configurations'
  requests), then `wlc_set_wake_ctrl`.

### 5.7 Leaving: wlc_disassociate_client and wlc_clear_hw_association

`wlc_disassociate_client(bsscfg, ..)` (station, infrastructure): admission
control left; if the card is present: `wlc_sta_assoc_upd(bsscfg, 0)` (hold
0x20 set, `pub+0x59` = 0) and **`wlc_clear_hw_association(bsscfg, 1)`**;
the disassociation frame (frames.md); `wlc_bmac_ifsctl_edcrs_set(hw, 1)`
(R1: the same as at every channel); protection reset; link event; LED
event; MPC.

**`wlc_clear_hw_association(bsscfg, mute_quiet)`**:

1. PM override off; `wlc_bss_clear_bssid` (the BSSID cleared in software
   and, through `wlc_clear_bssid`, `AMT[62]` cleared already here, before
   the MAC is suspended: observed); CSA and quiet channels reset.
2. If the card is present: `wlc_bmac_suspend_mac_and_wait(hw)`; if the
   PHY's chanspec is a quiet channel and `mute_quiet` = 1:
   `wlc_bmac_mute(hw, 1, 0)` (E4); **`wlc_clear_bssid(bsscfg)`**: for the
   primary configuration `wlc_bmac_write_amt(hw, 62, 00:00:00:00:00:00,
   0)`; **`D11(0x188)` = 0x80000000** (32 bit); **`wlc_bmac_set_cwmin(hw,
   15)`, `wlc_bmac_set_cwmax(hw, 1023)`** (B11: `SCR(3)`, `SCR(4)`); on the
   2.4 GHz band `wlc_bmac_set_shortslot(hw, wlc+0x532)`; frequency tracking
   reset; `wlc_bmac_enable_mac(hw)`; `wlc_rate_lookup_init(wlc, empty
   set)` (no basic rates: CCK rates answer at themselves, OFDM rates at the
   mandatory 6/12/24 Mbit/s, 1.6) and `wlc_set_ratetable(wlc)`.

`wlc_down` (4.1 step 3) and a new join (`sub_05c425`) also clear the
association; host flag 1 bit 14 is then set by `wlc_key_defkeyflag` (keys,
later).

### 5.8 What the open core's `bcm4360_core_set_bss` has to do

For mac80211's BSS changes, in the object's order: on association (MAC
suspended): channel (section 2) if it differs; response-rate map from the
basic rates and the table (1.6); `AMT[62]` = BSSID (attribute 0x8006),
`TPL(0x30..0x43)`; slot time (E6) on 2.4 GHz; PRETBTT (`D11(0x612)`,
`SHM(0x96)` = 1024); MAC enabled; PHY hold 0x20 off; EDCA per AC (5.4); on
the first beacon (or from mac80211's TSF/beacon interval): MAC control bit
28, TSF, `D11(0x188)` = beacon interval << 16, `D11(0x18c)` = next TBTT,
bit 28 cleared; `SHM(0x12)` = DTIM period; PS bits (5.6) when power save is
used. On leaving: 5.7.

## 6. Receive filters and scanning

### 6.1 Receive filters

Bits of the MAC control register (`D11(0x120)`, through `wlc_bmac_mctrl`,
bmac-init A2; brcmsmac names where they are known):

| bit | mask | set by the upper layer when | brcmsmac name |
|---|---|---|---|
| 20 | 0x00100000 | beacons of other networks wanted: any of the three bytes `wlc+0x28c` (other requesters), `wlc+0x28d` (a scan, 6.2), `wlc+0x28e` (monitor mode) is not 0 (`wlc_mac_bcn_promisc`) | BCNS_PROMISC |
| 22 | 0x00400000 | monitor mode (`wlc+0x288` != 0), together with bit 24 | KEEPCONTROL |
| 24 | 0x01000000 | promiscuous: the `WLC_SET_PROMISC` setting (`pub+0x4c`) unless the byte `wlc+0x2a1` is set; or monitor mode | PROMISC |

`wlc_mac_promisc(wlc)`: v = 0x01000000 if `pub+0x4c` is set and `wlc+0x2a1`
is not, else 0; if `wlc+0x288` != 0, v = 0x01400000;
`wlc_bmac_mctrl(hw, 0x01400000, v)`; then `wlc_enable_probe_req(wlc, 4, v &
0x01000000 ? 4 : 0)` (1.8): in promiscuous/monitor mode probe requests are
passed up (host flag 5 bit 15 cleared).

Probe requests addressed to others: host flag 5 bit 15 (1.8); set (not
passed up) whenever no requester bit of `wlc+0x704` is set.

Monitor mode (`WLC_SET_MONITOR`, ioctl 108): `wlc+0x288` = value,
`wlc+0x28e` = (value != 0), MPC update, and with the MAC layer up:
`wlc_mac_bcn_promisc`, `wlc_mac_promisc`, the glue's `wl_set_monitor`,
`wlc_set_wake_ctrl` (monitor mode keeps the WAKE bit, 5.6). (Read.)

Frames with a bad FCS: the upper layer sets no MAC control bit for them (no
`wlc_bmac_mctrl` call with bit 23 in the object outside the generic
`wlc_mctrl` iovar); whether they are passed up is the receive path's matter
(frames.md).

The address match table entries the upper layer writes: 0..49 (cleared;
keys, 1.4 step 9), 62 (BSSID), 63 (own address); the beamforming index
update of `wlc_txfbf_update_amt_idx` (1.4 step 9.3) acts only on entries
whose attribute has bits 0x8004 set (a station's peer entries; later).

### 6.2 Scanning

Observed: an active broadcast scan (the glue's `WLC_SET_PASSIVE_SCAN` 0 and
`WLC_SCAN`, `hybrid.py` step "scan"), all 2.4 and 5 GHz channels of the
country, 20 MHz, after the first up on channel 1
(`re-out/analysis/wlc-hw/upper-scan.txt`). Read for the associated case.

**Start** (`wlc_scan`): the channel list from the country
(`wlc_phy_chanspec_band_validch` per band); **`wlc_phy_hold_upd(pi, 2,
1)`** (acphy-api A13: scan hold); `wlc_set_wake_ctrl` -> the WAKE bit set
(`wlc_stay_awake` is true while scanning) and `wlc_bmac_wait_for_wake(hw)`
(A5); `cca_stats_upd(wlc, 0)`.

**Leaving the home channel** (the first time the next scan channel differs
from the current chanspec while that is the home chanspec): `sub_16e2b7`
(below); when associated, power save is announced to the AP
(`wlc_set_pmstate(bsscfg, 1)`: null frame with PM set) and the transmit
FIFOs are suspended (`wlc_tx_suspend`, R2) before moving.

**At each channel** (the scan timer, `sub_16e31e`):

1. `wlc_hw_deviceremoved(hw)` (C14).
2. If the channel's control channel differs from the current one
   (`wlc+0x518`): `wlc_bmac_suspend_mac_and_wait(hw)`, `sub_16e2b7`,
   **`wlc_set_chanspec(wlc, c)`** (section 2: a complete channel change with
   power limits, 20 MHz chanspecs), `wlc_bmac_enable_mac(hw)`.
3. `wlc+0x28d` = 1 unless the scan's type byte is 2;
   `wlc_mac_bcn_promisc(wlc)` (bit 20 set: beacons of every network).
4. `wlc_read_tsf` (`wlc_bmac_read_tsf`, R7): the time of arrival on the
   channel.
5. Active scan on a channel that is valid and not quiet: probe requests are
   sent (frames.md; building their transmit header calls
   `wlc_phy_txpower_get_target_max(pi)` and `wlc_phy_tssivisible_thresh(pi)`,
   acphy-api-txpower). Passive scan or quiet (radar) channel: none.

`sub_16e2b7` (assigned: `wlc_scan_mac_away`): an enabled IBSS is disabled
(not here); `wlc_mhf(wlc, 1, 0x0400, 0x0400, 3)` (host flag 2 bit 10);
`wlc_skip_adjtsf(wlc, 1, NULL, 3)`: the requester bit 31 of the 32-bit word
`wlc_hw+0x1b4` is set (bit 0x1c + index; the index of a configuration
otherwise), and `wlc_bmac_mhf(hw, 1, 0x0010, word != 0 ? 0x0010 : 0, 3)`
(host flag 2 bit 4: by the function's name, the microcode does not adjust
its TSF to beacons). Observed at every channel switch: (1, 0x400, 0x400,
3) and (1, 0x10, 0x10, 3); no shared-memory write after the first (the
copies do not change).

**End** (`sub_16e03b`, assigned: `wlc_scan_mac_home`; observed):

1. `wlc_bmac_suspend_mac_and_wait(hw)`; **`wlc_set_chanspec(wlc, home
   chanspec)`** (section 2; with the band switch when the scan ended in the
   other band: observed 5 GHz -> channel 1, response-rate table first).
2. An IBSS is enabled again (not here). `wlc_mhf(wlc, 1, 0x0400, 0, 3)`;
   `wlc_skip_adjtsf(wlc, 0, NULL, 3)` (requester bit cleared; host flag 2
   bit 4 cleared when no requester is left).
3. `wlc+0x28d` = 0; `wlc_mac_bcn_promisc` (bit 20 cleared unless another
   requester).
4. `wlc_bmac_enable_mac(hw)`.
5. **`wlc_phy_clear_tssi(pi)`** (acphy-api-txpower).
6. `wlc_tx_resume(wlc)`: transmit FIFOs resumed (observed
   `wlc_bmac_tx_fifo_resume(hw, 1)`, R2).
7. PM restored: if the scan had set PM on the associated configuration,
   `wlc_set_pmstate(bsscfg, 0)`.
8. `wlc_set_wake_ctrl` (the scan is still marked active: WAKE stays set),
   queued frames sent.

Then (`sub_16e31e`): the scan is marked inactive;
**`wlc_phy_hold_upd(pi, 2, 0)`**; the OLPC channel update when active
(channel-power.md); `wlc_set_wake_ctrl` -> WAKE cleared (observed
`D11(0x120)` 0x44020403 -> 0x40020403); MPC and radio update; scan results
and events.

### 6.3 For the open core

`bcm4360_core_set_scanning(on)` corresponds to: on - phy hold 2 set, WAKE
set, host flag 2 bits 10 and 4 set, MAC control bit 20 set (for an active
or passive scan), transmit FIFOs suspended when associated; each channel
change under mac80211's software scan is section 2 with the MAC suspended;
off - the reverse in the order of the end above, including
`wlc_phy_clear_tssi`. `bcm4360_core_set_filter`: BEACONS -> bit 20,
PROMISC -> bit 24 (and probe requests passed up), CONTROL -> bit 22 (the
object sets it only together with bit 24, for monitor mode), PROBE_RESP ->
nothing beyond bit 24 in the object, FCSFAIL -> no MAC control bit in the
object (open question Q6).

## Verification

Method: every procedure was read in the decompiler output; the calls
into the hardware layer, their arguments and results, and the upper
layer's own accesses were recorded in the emulator with
`re-out/analysis/wlc-hw/upper.py` (a hook on every function taken over by
the hybrid build, logged when called from Broadcom's code, plus every card
access whose instruction lies in Broadcom's `.text`, both with the call
chain), on the hybrid core with all layers open; and the whole trace of the
same session on Broadcom's object alone (`dumpsession.py`,
`obj-session.txt`; the two make the same accesses - `hybrid.py check`).

| run | steps | used for |
|---|---|---|
| `upper-session.txt` (hybrid core) | `hybrid.py` SESSION | sections 1-4: every call and value marked "observed" |
| `obj-session.txt` (object alone) | SESSION | `D11(0x120)`, host flags, interrupt mask, the per-step order |
| `upper-wd.txt` | attach, mpc 0, up, 6, wait 35 | section 3 on 2.4 GHz: coexistence reads, counters at tick 30, temperature every 10 ticks |
| `upper-scan.txt` | attach, mpc 0, up, scan | section 6.2 |
| `verify5.py` -> `verify5-out.txt` (object alone) | attach, mpc 0, up, then direct calls | 5.3: T = 0x123456789a, B = 100: `D11(0x180)` = 0x3456789a, `D11(0x184)` = 0x12, `D11(0x188)` = 0x640000, `D11(0x18c)` = 0x34587000 (as the formula), MAC control 0x50020403 around them; 5.2 step 15: `D11(0x612)` = `SHM(0x96)` = 0x400; 5.2 step 7: `AMT[62]` = 33221100 / 80065544 (attribute 0x8006 OR-ed after a read), template words at 0x30; 5.4: four records (VI with TXOP 94: `SHM(0x280..0x29e)` = 0xbc0, 7, 0xf, 7, 2, 0, 2, 0x100, 0...; BE TXOP 0: base 0x260, cw 0xf/0x3ff, aifs 3; VI TXOP 0: aifs 3; AIFSN 0: no write, but suspend/enable); 5.7 as described |

Observed (with the values given in the text): 1.1 steps 3-13 (calls and
arguments), 1.2-1.5 (all calls, `D11(0xa00)`, `D11(0x688)`, `D11(0x3dc)`,
the shared-memory writes and their order, the key table and AMT clears,
the beamforming block, templates, response-rate table offsets, duty cycle
words, lifetime, retry words, host flags), 1.9; 2.1-2.4 (channels 1, 6, 11,
36, 36/40, 36/80, 149/80, 157/40, 165: within a band and band switches);
3 steps 2, 6-9, 13, 14, 16; 4.1, 4.2; 6.2.

Read only: the branches for other chips and boards, the associated paths of
`wlc_init`/`wlc_reset`/`wlc_watchdog`, the order of section 5 and its
procedures not named in the table above (no join in the emulated
sessions), the IBSS paths, the monitor and promiscuous ioctls
(6.1), the throttle and temperature changes (2.3 with t != 0, 3 step 16's
change), the counter mapping of `wlc_statsupd`.

Limits of the model that shape the observed values: the rate directories
`SHM(0x1c0..)`/`SHM(0x200..)` and `SHM(0x56)` read 0 (no microcode; the
object address does not advance on writes, bmac-init B1 note), so the
response-rate table is written with zeros and the key table clear starts
at offset 0 - which in the model overwrites the host-flag words that
`wlc_bmac_init` wrote; `SHM(0xb0)` reads 0, so the beamforming block lies
at 0 in the trace; `D11(0x15c)` (MAC capabilities) reads 0.

## Open questions

* **Q1.** The real values of `SHM(0x56)` (key table base), `SHM(0xb0)`
  (beamforming block base) and the rate directories: they come from the
  microcode; the model cannot provide them. The procedures use them as
  read, so an implementation that reads them as the object does is correct
  whatever they are; a card capture would confirm the layout.
* **Q2.** The meaning of `D11(0x3dc)` = 10000000, `D11(0xa00)` bit 0,
  `D11(0x688)` bit 2 (WME), `SHM(0x74)`, `SHM(0x82)` (10000 or 1504),
  `SHM(0x3c)`/`SHM(0xba)` (A-MPDU), `SHM(0xd6)`, MAC control bits 27 and 30,
  host flag 2 bits 4 and 10, host flag 4 bits 0, 5, 6, host flag 5 bits 0,
  7, 15 beyond what the code's use says.
* **Q3.** `wlc_statsupd`: the exact mapping of the 64 counter words to the
  `wl_cnt_t` fields was not followed beyond the first twelve; a mac80211
  driver needs only a few (FCS errors, ACK failures).
* **Q4.** The join as a whole was not executed: a join stand-in in the
  emulator driving `wlc_assocresp_client` and `wlc_join_complete` with
  synthetic frames would verify the order of 5.1 (the procedures 5.2 step
  7 and 15, 5.3, 5.4 and 5.7 were checked by direct calls).
* **Q5.** The board-specific duty-cycle and power-throttle lists name board
  types (0x117 among the throttle boards); the MacBook Air card's real board
  type and SROM duty-cycle variables decide whether `SHM(0xa4)`/`SHM(0xb4)`
  are ever non-zero (temperature throttle: 16 with `stf+0x78` = 50).
* **Q6.** Receiving frames with a bad FCS (mac80211's FIF_FCSFAIL) has no
  counterpart in the upper layer's MAC control writes; brcmsmac's bit 23
  name suggests one exists in the hardware (unverified).
* **Q7.** `wlc_skip_adjtsf` passes its fifth argument through unchanged as
  the `bands` argument of `wlc_bmac_mhf` (observed 3 from the scan); other
  callers were not checked.
