# Coverage: what of Broadcom's code still runs, what a client needs, and the gaps (`coverage`)

Status (2026-11-06): part 1 (the audit) and part 2 (the gaps G1-G7,
the corrections G9) written; G8 left open. Goal: the open mac80211 driver (no Broadcom code
linked) must reproduce everything of Broadcom's code that matters for this
card. This document lists every Broadcom function that still executes in the
hybrid core, and every function a client station reaches in normal operation
that the emulator sessions do not execute, and says for each where its
hardware-relevant behaviour is specified - or that it has none (and what
mac80211 does instead), or that a client on this card does not use it, or
that it is a gap. Part 2 specifies the gaps.

Chip 0x4360, MAC revision 42, AC-PHY, PCIe; a station (no AP, no IBSS).
Statements are marked **verified** (how) or **read** (from the code only).

## Scope

Functions specified in part 2 (gap sections). Everything else of the audit
is specified elsewhere (column "verdict" of part 1).

| function | .text offset | size | name |
|---|---|---|---|
| `wlc_stf_get_pwrperrate` | 0x1734be | 1379 | original (wlc_stf.c) - G1 |
| `wlc_stf_spatial_expansion_get` | 0x173317 | 137 | original (wlc_stf.c) - G1 |
| `wlc_stf_txchain_get` | 0x17349e | 32 | original (wlc_stf.c) - G1 |
| `wlc_acphy_txctl1_calc` | 0x03102a | 307 | original (wlc.c) - G1 (the power field; the rest: frames 3.5) |
| `wlc_ratespec_nsts`, `wlc_ratespec_nss` | 0x1684eb, 0x1684af | 76, 60 | original (wlc_rate.c) - G1 |
| `wlc_led_attach`, `wlc_led_init`, `wlc_led_up`, `wlc_led_down`, `wlc_led_deinit` | 0x07d339, 0x07d24d, 0x07cdb2, 0x07d15a, 0x07d235 | 962, 98, 29, 70, 24 | original (wlc_led.c) - G2 |
| `wlc_led_event`, `wlc_led_start_activity_timer`, `wlc_led_stop_activity_timer` | 0x07d095, 0x07ce13, 0x07cdcf | 197, 79, 68 | original - G2 |
| `sub_07d6fb` | 0x07d6fb | 88 | assigned: `wlc_led_activity_timer` - G2 |
| `sub_07ce62`, `sub_07cf8d`, `sub_07d011`, `sub_07df67`, `sub_07ceb5` | 0x07ce62, 0x07cf8d, 0x07d011, 0x07df67, 0x07ceb5 | 83, 132, 132, 83, 35 | assigned: `wlc_led_radio`, `wlc_led_bradio`, `wlc_led_aradio`, `wlc_led_activity`, `wlc_led_off` - G2 |
| `sub_07daf9`, `sub_07d753` | 0x07daf9, 0x07d753 | 126, 85 | assigned: `wlc_led_blink_set`, `wlc_led_blink_stop` - G2 |
| `wlc_btc_mode_set`, `wlc_btc_wire_get`, `wlc_enable_btc_ps_protection` | 0x026b9a, 0x025e4b, 0x02b0e6 | 59, 16, 406 | original (wlc.c) - G3 |
| `wlc_bmac_btc_flags_get`, `wlc_bmac_btc_rssi_threshold_get`, `wlc_bmac_btc_update_predictor` | 0x06019a, 0x061920, 0x062004 | 17, 117, 211 | original (wlc_bmac.c, still Broadcom's) - G3 |
| `wlc_radio_upd`, `wlc_radio_disable`, `wlc_radio_monitor_stop`, `wlc_radio_mpc_upd`, `wlc_ismpc`, `wlc_down_for_mpc`, `wlc_rfd_intr` | 0x036e70, 0x036428, 0x030022, 0x036f4c, 0x025086, 0x0250ac, 0x026056 | 151, 114, 66, 216, 30, 38, 87 | original (wlc.c) - G4 |
| `sub_026c55`, `sub_037024`, `sub_025002` | 0x026c55, 0x037024, 0x025002 | 100, 56, 132 | assigned: `wlc_radio_monitor_start`, `wlc_radio_timer`, `wlc_mpc_allowed` - G4 |
| `wlc_stf_txchain_set`, `wlc_stf_txchain_set_complete`, `wlc_stf_rxchain_set` | 0x1750f4, 0x174f29, 0x173f47 | 452, 459, 381 | original (wlc_stf.c) - G5 |
| `sub_173b37`, `sub_172b5a` | 0x173b37, 0x172b5a | 151, 138 | assigned: `wlc_stf_stbc_tx_upd`, `wlc_stf_txcore_table_upd` - G5 |
| `sub_045018` | 0x045018 | 216 | assigned: `wlc_mimops_complete` - G5 |
| `wlc_txbf_txchain_upd`, `wlc_txbf_rxchain_upd` | 0x179877, 0x1799ec | 373, 78 | original (wlc_txbf.c) - G5 |
| `wlc_join_BSS`, `wlc_assoc_change_state`, `wlc_roam_complete` | 0x05ae4f, 0x05c614, 0x05cca9 | 2188, 405, 243 | original (wlc_assoc.c) - G7 |
| `wlc_full_phy_cal`, `wlc_bsscfg_up` | 0x039e40, 0x06c233 | 84, 127 | original (wlc.c, wlc_bsscfg.c) - G7 |

## Overview

* **Audit (part 1).** 1,228 Broadcom functions: 571 that execute in the
  hybrid core in the standard session and in the "real" session (scans,
  radio off/on, interface down/up, suspend/resume, MPC), and 657 that a
  client station reaches in normal operation but the sessions do not run
  (association, keys, power save, aggregation, beacons, rfkill, roaming
  scans, the frame paths). Of them 480 are covered (435 by the existing specifications, 45 only by part 2 here), 706 have no hardware effect, 40 are not used by a client on this card, 2 remain gaps.
* **Gaps (part 2).** G1 per-rate transmit power in the transmit header, G2 LEDs, G3 Bluetooth coexistence policy, G4 radio switch/rfkill/MPC, G5 chain changes, G6 the spectrum-management, measurement and interference modules (no client hardware writes needed), G7 the start of a join; G8 (beamforming links) is left open.
* **Corrections** that newer specifications make to older ones are
  collected in G9, so that an implementer reading an older section finds
  them.

## Part 1. Audit

### 1.1 How the lists were made

**What runs.** `tools/re/remaining.py` (extended by this work with
`--steps` and `--tsv`) builds the hybrid core with all layers at their
defaults (open PHY included), runs a session on it with block coverage, and
attributes every executed block of Broadcom's `.text` to its function and
source file (`modmap.txt`):

* `--steps session` (`hybrid.SESSION`: attach, `mpc 0`, up, nine channels
  in both bands and all bandwidths, watchdog, down, up, 36/80, watchdog,
  down, detach): 456 functions, 73,332 bytes of Broadcom's code executed
  (each block counted once), 0.89 % of all execution counting repetitions;
  151,075 bytes of open code.
* `--steps real` (`hybrid.REAL`: attach, the glue's settings, up, active
  and passive scans, radio off/on, `ifdown`/`ifup`, suspend/resume, `mpc
  0`, 36/80, watchdog, `mpc 1` and 30 s idle, scan, down, detach; the steps
  of `hybrid.Run.step`): 571 functions, 91,288 bytes, 2.19 %; it contains
  every function of the standard session.

`python3 remaining.py --steps real --top 0 --tsv real.tsv` lists every
function that ran (`re-out/analysis/coverage/session.tsv`, `real.tsv`,
`session.txt`, `real.txt`). Both sessions ran to the end.

**What a client reaches without the sessions running it.** The callee
closure (index `re-out/index.tsv`; `re-out/analysis/coverage/reach2.py`) of
the entry points the specifications name for a station: association
(`wlc_join`, `wlc_join_BSS`, `wlc_reassoc`, `wlc_roam_scan`,
`wlc_assocresp_client`, `wlc_join_complete`, `wlc_roam_complete`,
`wlc_disassociate_client`, `wlc_assoc_change_state`), keys
(`wlc_key_insert`, `wlc_key_update`, `wlc_key_delete`, `wlc_key_hw_init`,
`wlc_wpa_plumb_gtk`, `wlc_sup_eapol`), power save (`wlc_set_pmstate`,
`wlc_set_pmawakebcn`, `wlc_set_uatbtt`, `wlc_pm_pending_complete`),
aggregation (`wlc_sendampdu`, `wlc_ampdu_dotxstatus`,
`wlc_ampdu_recv_addba_req_resp`, `wlc_ampdu_recv_addba_resp`,
`wlc_ampdu_recv_ctl`, `wlc_send_bar`, `wlc_recvamsdu`), beacons
(`sub_04ab93`, `wlc_tsf_adopt_bcn`), the radio switch (`wlc_radio_upd`,
`sub_030b4b`, `wlc_radio_mpc_upd`), the frame paths and interrupts
(`wlc_isr`, `wlc_dpc`, `wlc_sendpkt`, `wlc_send_q`, `wlc_recv`,
`wlc_recvdata`, `wlc_dotxstatus`, `wlc_bmac_txstatus`, `wlc_bmac_recv`,
`wlc_txfifo`), the watchdog (`sub_0450f0`) and scans (`wlc_scan`). The
closure stops at functions the open code takes over (`hybrid/takeover.txt`)
and does not enter modules a station does not use (AP, IBSS, WDS, TDLS,
P2P, the offload engine, wake-on-WLAN, debug dumps): 887 functions, 230 of
which ran, 657 not. Two functions of Broadcom's PHY files that are not
taken over are called directly by the upper layer: `wlc_phy_get_tx_power_offset`
and `wlc_phy_get_tx_power_offset_by_mcs` (`wlc_phy_ssn.c`, 8 bytes each),
from the header builder's branch for PHY type 6 only - not reached on this
card (read).

**Classification** (`re-out/analysis/coverage/audit.py`, `gen.py`,
`verdicts.tsv`):

1. A function in the Scope table of a specification is **covered** by it;
   the section is taken from the Scope row or the heading that names it.
2. The 77 other functions that call the hardware layer directly (a function
   of `takeover.txt` other than the pure helpers, or one of the upper
   layer's pass-throughs `wlc_write_shm`, `wlc_mhf`, `wlc_mctrl`,
   `wlc_suspend_mac_and_wait`, ...) and every function not matched by a rule
   were decided by hand, from the specifications' text, the index and,
   where needed, the code.
3. The rest by source file and name: the upper layer's bookkeeping
   (station table, BSS configurations, events, timers, rate selection,
   supplicant and software crypto, information elements, management
   frames, regulatory information elements, protection state, Block Ack
   session bookkeeping) has **no hardware effect**; the verdict names what
   mac80211/cfg80211 does instead. The rule results of every file that
   touches the hardware were reviewed by hand and corrected where a
   specification names the function.

Verdicts: **C** covered (specification and section; "coverage Gn" = part 2
of this document); **N** no hardware effect (what mac80211 does instead);
**not used** - writes the hardware, but not for a station on this card
(AP/IBSS templates, other PHY types or MAC revisions, the offload engine,
software antenna diversity); **GAP** - hardware behaviour a client may need
that no specification gives. Column "ran": S standard session, R real
session, "-" reached, not run.

Limits. Calls through function pointers are not in the index: the DMA
engine's slots (open), the modules' registered callbacks (watchdog, up,
down, iovar: those of the sessions are in the "ran" lists), the LED
behaviour table (read for G2). Frame contents reach the hardware as data:
the header builder and the receive parser are covered by frames.md and
ampdu.md; the information elements and management frames they carry are
mac80211's. The ioctl and iovar dispatchers (`sub_0411e9`, `wlc_doiovar`)
are covered only for the cases the specifications name (channel, promisc,
monitor, power); their other cases are configuration (nl80211).

### 1.2 Summary

| source file | functions | ran (S/R) | not run, reached | covered | no hardware effect | not used | gap |
|---|---|---|---|---|---|---|---|
| wlc.c | 337 | 153 | 184 | 161 | 157 | 19 | 0 |
| wlc_assoc.c | 46 | 4 | 42 | 11 | 35 | 0 | 0 |
| wlc_ampdu.c | 41 | 13 | 28 | 26 | 15 | 0 | 0 |
| wlc_bsscfg.c | 40 | 26 | 14 | 4 | 35 | 1 | 0 |
| wlc_scb.c | 39 | 18 | 21 | 0 | 39 | 0 | 0 |
| wlc_channel.c | 37 | 22 | 15 | 25 | 12 | 0 | 0 |
| wlc_stf.c | 32 | 22 | 10 | 29 | 3 | 0 | 0 |
| siutils.c | 29 | 29 | 0 | 29 | 0 | 0 | 0 |
| wlc_bmac.c | 29 | 15 | 14 | 25 | 1 | 3 | 0 |
| wlc_scan.c | 26 | 16 | 10 | 7 | 19 | 0 | 0 |
| wlc_rate_sel.c | 25 | 5 | 20 | 1 | 24 | 0 | 0 |
| wlc_ampdu_rx.c | 22 | 8 | 14 | 4 | 18 | 0 | 0 |
| wlc_csa.c | 21 | 7 | 14 | 0 | 21 | 0 | 0 |
| wlc_sup.c | 21 | 3 | 18 | 0 | 21 | 0 | 0 |
| bcmutils.c | 20 | 7 | 13 | 7 | 13 | 0 | 0 |
| wlc_vht.c | 20 | 2 | 18 | 0 | 20 | 0 | 0 |
| wlc_txbf.c | 19 | 9 | 10 | 4 | 13 | 0 | 2 |
| wlc_amsdu.c | 18 | 8 | 10 | 2 | 16 | 0 | 0 |
| bcmwpa.c | 17 | 0 | 17 | 0 | 17 | 0 | 0 |
| wlc_cac.c | 17 | 6 | 11 | 0 | 17 | 0 | 0 |
| wlc_prot_n.c | 17 | 9 | 8 | 1 | 16 | 0 | 0 |
| wlc_scb_ratesel.c | 17 | 5 | 12 | 0 | 17 | 0 | 0 |
| wlc_led.c | 15 | 14 | 1 | 15 | 0 | 0 | 0 |
| wlc_rate.c | 15 | 3 | 12 | 5 | 9 | 1 | 0 |
| wlc_hrt.c | 14 | 10 | 4 | 3 | 11 | 0 | 0 |
| wlc_key.c | 14 | 1 | 13 | 14 | 0 | 0 | 0 |
| wlc_phy_shim.c | 14 | 14 | 0 | 14 | 0 | 0 | 0 |
| wlc_prot_g.c | 14 | 7 | 7 | 0 | 14 | 0 | 0 |
| nicpci.c | 12 | 12 | 0 | 12 | 0 | 0 | 0 |
| wlc_ampdu_cmn.c | 12 | 2 | 10 | 1 | 11 | 0 | 0 |
| wlc_antsel.c | 12 | 4 | 8 | 3 | 8 | 1 | 0 |
| wlc_olpc_engine.c | 12 | 10 | 2 | 12 | 0 | 0 | 0 |
| wlc_tpc.c | 12 | 4 | 8 | 4 | 8 | 0 | 0 |
| wlc_event.c | 11 | 11 | 0 | 0 | 11 | 0 | 0 |
| wlc_quiet.c | 11 | 4 | 7 | 0 | 11 | 0 | 0 |
| bcm_mpool.c | 10 | 10 | 0 | 10 | 0 | 0 | 0 |
| wlc_11h.c | 10 | 4 | 6 | 0 | 10 | 0 | 0 |
| wlc_rm.c | 10 | 4 | 6 | 0 | 10 | 0 | 0 |
| wlc_security.c | 10 | 0 | 10 | 6 | 4 | 0 | 0 |
| wlc_offloads.c | 9 | 9 | 0 | 0 | 0 | 9 | 0 |
| bcm_notif.c | 8 | 7 | 1 | 7 | 1 | 0 | 0 |
| wlc_11d.c | 8 | 3 | 5 | 2 | 6 | 0 | 0 |
| wlc_lq.c | 8 | 2 | 6 | 2 | 6 | 0 | 0 |
| bcmotp.c | 6 | 6 | 0 | 6 | 0 | 0 | 0 |
| wlc_airtime.c | 6 | 0 | 6 | 0 | 6 | 0 | 0 |
| wlc_cntry.c | 6 | 3 | 3 | 3 | 3 | 0 | 0 |
| wlc_intr.c | 6 | 3 | 3 | 6 | 0 | 0 | 0 |
| wlc_prot.c | 6 | 3 | 3 | 0 | 6 | 0 | 0 |
| aes.c | 5 | 0 | 5 | 0 | 5 | 0 | 0 |
| hndpmu.c | 5 | 5 | 0 | 5 | 0 | 0 | 0 |
| wlc_utils.c | 5 | 0 | 5 | 1 | 4 | 0 | 0 |
| aiutils.c | 4 | 4 | 0 | 4 | 0 | 0 | 0 |
| md5.c | 4 | 0 | 4 | 0 | 4 | 0 | 0 |
| rijndael-alg-fst.c | 4 | 0 | 4 | 0 | 4 | 0 | 0 |
| sha1.c | 4 | 0 | 4 | 0 | 4 | 0 | 0 |
| wlc_wowl.c | 4 | 4 | 0 | 0 | 0 | 4 | 0 |
| bcmwifi_channels.c | 3 | 3 | 0 | 3 | 0 | 0 | 0 |
| wlc_alloc.c | 3 | 3 | 0 | 0 | 3 | 0 | 0 |
| wlc_interfere.c | 3 | 3 | 0 | 0 | 3 | 0 | 0 |
| bcm_ol_msg.c | 2 | 2 | 0 | 0 | 0 | 2 | 0 |
| prf.c | 2 | 0 | 2 | 0 | 2 | 0 | 0 |
| rc4.c | 2 | 0 | 2 | 0 | 2 | 0 | 0 |
| tkhash.c | 2 | 0 | 2 | 0 | 2 | 0 | 0 |
| tkmic.c | 2 | 0 | 2 | 0 | 2 | 0 | 0 |
| wlc_phy_cmn.c | 2 | 2 | 0 | 2 | 0 | 0 | 0 |
| wlc_phy_iovar.c | 2 | 2 | 0 | 0 | 2 | 0 | 0 |
| wlc_tso.c | 2 | 2 | 0 | 2 | 0 | 0 | 0 |
| wlc_vndr_ie_list.c | 2 | 2 | 0 | 0 | 2 | 0 | 0 |
| aeskeywrap.c | 1 | 0 | 1 | 0 | 1 | 0 | 0 |
| bcmsrom.c | 1 | 1 | 0 | 1 | 0 | 0 | 0 |
| hmac.c | 1 | 0 | 1 | 0 | 1 | 0 | 0 |
| passhash.c | 1 | 0 | 1 | 0 | 1 | 0 | 0 |
| wlc_hw.c | 1 | 1 | 0 | 1 | 0 | 0 | 0 |
| **all** | 1228 | 571 | 657 | 480 | 706 | 40 | 2 |

Of the 1,228 functions: **480 covered** - 435 by the existing
specifications (some of them completed by part 2) and 45 only by part 2 of
this document -, **706 without hardware effect** (mac80211, cfg80211 or the
kernel do their work), **40 not used** by a client on this card, and **2
gaps left open** (G8: the beamforming links of `wlc_txbf.c`, needed only if
the driver advertises VHT beamforming capabilities).

The gaps the audit found, by section of part 2: G1 the per-rate power field
of the transmit header (`wlc_stf_get_pwrperrate`,
`wlc_stf_spatial_expansion_get`); G2 the LED policy of `wlc_led.c`; G3 the
upper layer's Bluetooth coexistence policy (`wlc_enable_btc_ps_protection`,
`wlc_btc_mode_set`, the `wlc_bmac_btc_*` helpers still Broadcom's); G4 the
radio switch, rfkill and MPC (`wlc_rfd_intr`, `wlc_radio_upd`,
`wlc_radio_disable`, `wlc_radio_mpc_upd`); G5 changes of the transmit and
receive chains (`wlc_stf_txchain_set`, `wlc_stf_rxchain_set`); G6 the
spectrum-management, measurement and interference modules, checked for
client hardware writes (none needed); G7 the start of a join
(`wlc_join_BSS`, the association-state PHY hold, the full calibration);
G8 the beamforming links (left open). G9 collects the corrections.

### 1.3 Tables per source file

The tables list every function of the audit, per source file (wlc.c first),
those that ran first. Where a file has a common "no hardware effect"
verdict it is given once under the heading and the rows say "N".

<!-- generated by re-out/analysis/coverage/gen.py md -->
#### wlc.c

N = no hardware effect: transmit queues and per-packet callbacks; mac80211 queues and tx status.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_025002` | 0x25002 | 132 | S R | C: coverage G4 (MPC: may the radio go off) |
| `sub_0256db` | 0x256db | 131 | S R | C: wlc-hw 1.6 (rate-table iterator) |
| `sub_0262cc` | 0x262cc | 149 | R | N: per-packet completion callbacks; mac80211 tx status |
| `sub_026361` | 0x26361 | 50 | R | N: per-packet completion callbacks; mac80211 tx status |
| `sub_026629` | 0x26629 | 61 | S R | N: per-configuration data; mac80211 vif private data |
| `sub_026c55` | 0x26c55 | 100 | R | C: coverage G4 (radio monitor: PLL request while the radio is off) |
| `sub_026e83` | 0x26e83 | 55 | S R | C: wlc-hw 1.5 |
| `sub_027e71` | 0x27e71 | 58 | S R | C: wlc-hw 1.5 |
| `sub_0283f7` | 0x283f7 | 95 | R | N: packet-tag flag; none |
| `sub_029597` | 0x29597 | 196 | S R | C: wlc-hw 1.7 |
| `sub_02afa2` | 0x2afa2 | 87 | S R | N: per-configuration data; mac80211 vif private data |
| `sub_02b3f5` | 0x2b3f5 | 100 | R | N: event record; cfg80211 notifications |
| `sub_02c636` | 0x2c636 | 587 | S R | C: wlc-hw 5.4 |
| `sub_02d080` | 0x2d080 | 353 | R | N: configuration interface; nl80211/cfg80211 |
| `sub_02d688` | 0x2d688 | 118 | S R | N: station count; none |
| `sub_02fd6b` | 0x2fd6b | 157 | S R | C: wlc-hw 1.4 |
| `sub_030b4b` | 0x30b4b | 63 | S R | C: wlc-hw 1.1 |
| `sub_031322` | 0x31322 | 12297 | R | C: ampdu 1.4; frames 3 |
| `sub_03649a` | 0x3649a | 116 | S R | C: acphy-chanspec; wlc-hw 2.2 |
| `sub_037024` | 0x37024 | 56 | R | C: coverage G4 (radio-switch timer) |
| `sub_0376c2` | 0x376c2 | 117 | R | N: event queue to the glue; cfg80211 notifications |
| `sub_037bf9` | 0x37bf9 | 40 | S R | C: wlc-hw 1.4 |
| `sub_037c21` | 0x37c21 | 117 | S R | N: 20/40 MHz coexistence setting; mac80211/cfg80211 |
| `sub_037cb9` | 0x37cb9 | 87 | S R | not used: beacon/probe-response update (AP/IBSS) |
| `sub_0396ff` | 0x396ff | 164 | S R | C: wlc-hw 1.5 |
| `sub_039996` | 0x39996 | 974 | R | N: scan request ioctl; nl80211 scan |
| `sub_039dd4` | 0x39dd4 | 108 | S R | C: wlc-hw 1.4 |
| `sub_03a14c` | 0x3a14c | 96 | S R | C: ps-keys 1.3; wlc-hw 1.4 |
| `sub_03d429` | 0x3d429 | 28 | R | N: A-MPDU state; mac80211 aggregation sessions |
| `sub_03fc00` | 0x3fc00 | 164 | R | C: ps-keys 1.1 |
| `sub_0411e9` | 0x411e9 | 9748 | R | C: wlc-hw |
| `sub_0450f0` | 0x450f0 | 4621 | S R | C: ps-keys 1.5; wlc-hw 3 |
| `sub_046432` | 0x46432 | 100 | S R | C: wlc-hw 3 |
| `wlc_acphy_txctl0_calc` | 0x2965b | 206 | R | C: frames 3 |
| `wlc_acphy_txctl1_calc` | 0x3102a | 307 | R | C: frames 3 |
| `wlc_acphy_txctl2_calc` | 0x29170 | 160 | R | C: frames 3 |
| `wlc_ampdu_mode_upd` | 0x26cb9 | 129 | S R | C: ampdu 1.1 |
| `wlc_attach` | 0x37d10 | 6639 | S R | C: wlc-hw 1.1 |
| `wlc_bcn_li_upd` | 0x27e3c | 53 | S R | C: ps-keys 1.6; wlc-hw 1.4 |
| `wlc_bss_assocscb_getcnt` | 0x2d6fe | 18 | S R | N: station count; none |
| `wlc_bss_list_free` | 0x26567 | 109 | S R | N: scan results; cfg80211 BSS list |
| `wlc_bss_list_xfer` | 0x2889f | 80 | R | N: scan results; cfg80211 BSS list |
| `wlc_bss_mac_event` | 0x2bb1c | 46 | R | N: event to the glue; cfg80211/mac80211 notifications |
| `wlc_bss_mac_rxframe_event` | 0x2ba1b | 257 | R | N: event to the glue; cfg80211/mac80211 notifications |
| `wlc_bss_update_brcm_ie` | 0x2d710 | 125 | S R | N: information elements; mac80211 builds and parses them |
| `wlc_bsscfg_reprate_init` | 0x28727 | 35 | S R | N: rate sets; mac80211 rate tables (hardware rate table: wlc-hw 1.6) |
| `wlc_compute_plcp` | 0x28ef6 | 634 | R | C: frames 3 |
| `wlc_copy_info_elt` | 0x2692a | 35 | R | N: information elements; mac80211 builds and parses them |
| `wlc_copy_info_elt_safe` | 0x267e7 | 51 | R | N: information elements; mac80211 |
| `wlc_custom_scan_complete` | 0x3024d | 330 | R | N: scan results; cfg80211 BSS list |
| `wlc_default_rateset` | 0x2bd01 | 170 | S R | N: rate sets (wlc-hw 1.4); mac80211 rate tables |
| `wlc_detach` | 0x3705c | 1638 | S R | C: wlc-hw 4.2 |
| `wlc_doiovar` | 0x469ed | 16806 | S R | C: channel-power 2.1; wlc-hw 2.1 |
| `wlc_down` | 0x36bae | 646 | S R | C: bmac-init C1; wlc-hw 4.1 |
| `wlc_down_for_mpc` | 0x250ac | 38 | S R | C: coverage G4 (also G2: LEDs keep their state while MPC holds the radio off) |
| `wlc_dump_register` | 0x35ba9 | 174 | S R | N: driver bookkeeping; the open driver's probe |
| `wlc_edcf_acp_apply` | 0x2c881 | 180 | S R | C: wlc-hw 5.4 |
| `wlc_enable_mac` | 0x26f3d | 15 | R | C: bmac-init A7 (pass-through) |
| `wlc_enable_probe_req` | 0x26ef0 | 77 | S R | C: wlc-hw 1.8 |
| `wlc_event_if` | 0x2b459 | 171 | R | N: event to the glue; cfg80211/mac80211 notifications |
| `wlc_frame_get_mgmt` | 0x2d461 | 42 | R | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_frame_get_mgmt_ex` | 0x2d32b | 268 | R | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_get_mcsallow` | 0x25bef | 176 | S R | N: rate sets; mac80211 rate tables (hardware rate table: wlc-hw 1.6) |
| `wlc_get_sup_ext_rates` | 0x2cd6a | 149 | R | N: information elements; mac80211 |
| `wlc_get_txh_info` | 0x30818 | 464 | R | C: frames 3, 4 |
| `wlc_ht_build_cap_ie` | 0x2a139 | 308 | R | N: information elements; mac80211 builds and parses them |
| `wlc_ht_coex_enab` | 0x37b63 | 150 | S R | N: 20/40 coexistence information elements; mac80211 |
| `wlc_ht_obss_scanparam_init` | 0x25cfc | 47 | S R | N: OBSS scan parameters; mac80211 |
| `wlc_info_init` | 0x24d9b | 549 | S R | N: default settings of the driver state (wlc-hw 1.1); the open driver's probe |
| `wlc_init` | 0x3c46f | 2627 | S R | C: bmac-init C1; wlc-hw 1.4 |
| `wlc_ioctl` | 0x2d1e1 | 11 | R | N: configuration interface; nl80211/cfg80211 |
| `wlc_iovar_check` | 0x35f38 | 539 | S R | N: iovar request check (iovar.md); nl80211 |
| `wlc_iovar_getint` | 0x363fc | 44 | S R | N: iovar access (cca-stf part 2 step 1, channel-power 3.1); nl80211 at most |
| `wlc_iovar_setint` | 0x363cc | 48 | S R | N: iovar access (channel-power 3.1); nl80211 at most |
| `wlc_ismpc` | 0x25086 | 30 | S R | N: association bookkeeping; mac80211 MLME |
| `wlc_mac_bcn_promisc` | 0x27063 | 41 | S R | C: wlc-hw 1.7 |
| `wlc_mac_event` | 0x2bb4a | 48 | R | N: event to the glue; cfg80211/mac80211 notifications |
| `wlc_mac_promisc` | 0x26ffa | 105 | S R | C: wlc-hw 1.7 |
| `wlc_mhf` | 0x26e6a | 25 | S R | C: wlc-hw 1.1 |
| `wlc_module_add_ioctl_fn` | 0x2af08 | 154 | S R | N: configuration interface; nl80211/cfg80211 |
| `wlc_module_remove_ioctl_fn` | 0x265d4 | 85 | S R | N: configuration interface; nl80211/cfg80211 |
| `wlc_pkt_callback` | 0x26129 | 137 | R | N |
| `wlc_pkt_get_vht_hdr` | 0x307d3 | 69 | R | C: frames 3 |
| `wlc_pm2_sleep_ret_timer_start` | 0x2ff77 | 56 | R | C: ps-keys 1.6 |
| `wlc_pm_pending_complete` | 0x3a1d5 | 213 | R | C: ps-keys 1.5 |
| `wlc_prec_enq` | 0x355fe | 14 | R | N |
| `wlc_prec_enq_head` | 0x35451 | 429 | R | N |
| `wlc_prep_pdu` | 0x34b61 | 426 | R | C: ampdu 1.5; frames 3.3 (header through sub_031322) |
| `wlc_process_event` | 0x2b5c1 | 50 | R | N: event to the glue; cfg80211/mac80211 notifications |
| `wlc_ps_allowed` | 0x3b5b9 | 115 | R | C: ps-keys 1.2; wlc-hw 5.6 |
| `wlc_pub` | 0x24fc0 | 9 | S R | N: driver bookkeeping; the open driver's probe |
| `wlc_queue_80211_frag` | 0x3ff0c | 332 | R | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_radio_disable` | 0x36428 | 114 | R | C: coverage G4 |
| `wlc_radio_monitor_stop` | 0x30022 | 66 | S R | C: coverage G4 |
| `wlc_radio_mpc_upd` | 0x36f4c | 216 | S R | C: coverage G4 |
| `wlc_radio_upd` | 0x36e70 | 151 | S R | C: coverage G4; wlc-hw 3 step 6 |
| `wlc_rate_lookup_init` | 0x287c9 | 214 | S R | C: wlc-hw 1.6 |
| `wlc_rateset_elts` | 0x28528 | 511 | R | N: rate sets; mac80211 rate tables (hardware rate table: wlc-hw 1.6) |
| `wlc_rcvfifo_limit_get` | 0x25db7 | 75 | S R | C: ampdu 4 |
| `wlc_read_shm` | 0x27ee1 | 15 | S R | C: wlc-hw |
| `wlc_read_tsf` | 0x26dce | 15 | S R | C: bmac-run R7 (pass-through) |
| `wlc_reprate_init` | 0x2874a | 23 | S R | N: rate sets; mac80211 rate tables (hardware rate table: wlc-hw 1.6) |
| `wlc_reset` | 0x3fca4 | 239 | S R | C: bmac-init C1; wlc-hw 1.3 |
| `wlc_rfaware_lifetime_set` | 0x27d74 | 200 | S R | C: wlc-hw 1.4 |
| `wlc_rminprog` | 0x24b9c | 43 | R | N: association bookkeeping; mac80211 MLME |
| `wlc_scan_request_ex` | 0x39804 | 402 | R | N: scan results; cfg80211 BSS list |
| `wlc_send_active_q` | 0x465bd | 25 | R | N: transmit queue restart; mac80211 queues |
| `wlc_send_q` | 0x3e58c | 925 | R | C: ampdu 1.6 |
| `wlc_sendmgmt` | 0x4104f | 46 | R | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_sendprobe` | 0x449fe | 1058 | R | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_set_bssid` | 0x39f81 | 295 | S R | C: wlc-hw 1.4 |
| `wlc_set_chanspec` | 0x3a4be | 635 | S R | C: acphy-chanspec; wlc-hw 2.1 |
| `wlc_set_gmode` | 0x3650e | 601 | S R | N: 802.11g mode configuration; mac80211 band capabilities (slot time: wlc-hw 5.2) |
| `wlc_set_home_chanspec` | 0x39e94 | 171 | S R | C: wlc-hw 1.4 |
| `wlc_set_mac` | 0x3a0fd | 79 | S R | C: wlc-hw 1.4 |
| `wlc_set_nmode` | 0x36767 | 545 | S R | N: 802.11n mode configuration; mac80211 HT capabilities |
| `wlc_set_pmpending` | 0x3b335 | 53 | R | C: ps-keys 1.1 |
| `wlc_set_pmstate` | 0x3f924 | 301 | R | C: ps-keys 1.4; wlc-hw 5.1 |
| `wlc_set_ps_ctrl` | 0x3b62c | 82 | R | C: ps-keys 1.3; wlc-hw 5.1 |
| `wlc_set_ratetable` | 0x2cdff | 248 | S R | C: wlc-hw 1.6 |
| `wlc_set_rcmta` | 0x26df0 | 70 | S R | C: ps-keys 2.3; wlc-hw 1.4 |
| `wlc_set_shm` | 0x27cd9 | 22 | S R | C: wlc-hw |
| `wlc_set_wake_ctrl` | 0x3a1ac | 41 | R | C: ps-keys 1.3; wlc-hw 3 |
| `wlc_statsupd` | 0x27107 | 2744 | S R | C: wlc-hw 3 |
| `wlc_stay_awake` | 0x24bc7 | 179 | S R | C: ps-keys 1.1; wlc-hw 1.4 |
| `wlc_suspend_mac_and_wait` | 0x26f4c | 15 | R | C: bmac-init A6 (pass-through) |
| `wlc_timers_init` | 0x37737 | 260 | S R | N: driver bookkeeping; the open driver's probe |
| `wlc_tx_resume` | 0x30b8a | 109 | R | C: bmac-run R2 (pass-through); wlc-hw 6.2 |
| `wlc_txc_invalidate` | 0x25e08 | 51 | S R | N: transmit-header cache (not used by the open driver) |
| `wlc_txc_upd` | 0x25285 | 20 | S R | N: transmit-header cache (iovar.md); not used by the open driver |
| `wlc_txfifo` | 0x3e27a | 786 | R | C: frames 3; ps-keys 1.1 |
| `wlc_txflowcontrol_reset_qi` | 0x26a08 | 45 | S R | N |
| `wlc_txmod_config` | 0x2a2e2 | 126 | S R | N |
| `wlc_txmod_fn_register` | 0x25c9f | 87 | S R | N |
| `wlc_txq_alloc` | 0x266b5 | 120 | S R | N |
| `wlc_txq_free` | 0x264cf | 76 | S R | N |
| `wlc_txq_scb_free_notify` | 0x2a734 | 184 | S R | N |
| `wlc_txq_scb_init` | 0x2a722 | 18 | S R | N |
| `wlc_up` | 0x3aac3 | 988 | S R | C: bmac-init C1; wlc-hw 1.2 |
| `wlc_update_beacon` | 0x37c96 | 35 | S R | not used: beacon template (AP/IBSS); called at up without effect for a station (wlc-hw 1.5 step 19) |
| `wlc_update_brcm_ie` | 0x2d78d | 25 | S R | N: Broadcom vendor IE (wlc-hw 1.5 step 19); none |
| `wlc_update_phy_mode` | 0x26c2d | 21 | S R | N: interference mode notice to the rate selection; minstrel_ht |
| `wlc_update_probe_resp` | 0x3596d | 41 | S R | not used: probe-response template (AP/IBSS); wlc-hw 1.5 step 19 |
| `wlc_valid_rate` | 0x257a1 | 176 | S R | N: rate sets; mac80211 rate tables (hardware rate table: wlc-hw 1.6) |
| `wlc_watchdog_upd` | 0x30064 | 298 | R | N: watchdog period while in power save; kernel timer |
| `wlc_wlcif_alloc` | 0x26666 | 79 | S R | N: driver bookkeeping; the open driver's probe |
| `wlc_wlcif_free` | 0x2651b | 76 | S R | N: driver bookkeeping; the open driver's probe |
| `wlc_wme_initparams_sta` | 0x26980 | 26 | S R | N: default WME parameters (applied: wlc-hw 5.4); mac80211 defaults |
| `wlc_write_brcm_ht_cap_ie` | 0x26883 | 105 | R | N: information elements; mac80211 builds and parses them |
| `wlc_write_info_elt` | 0x2694d | 51 | R | N: information elements; mac80211 builds and parses them |
| `wlc_write_seckindxblk_secinfo_by_idx` | 0x27d23 | 31 | S R | C: ps-keys; wlc-hw |
| `wlc_write_shm` | 0x27cef | 18 | S R | C: wlc-hw |
| `wlc_write_template_ram` | 0x26e48 | 15 | S R | C: wlc-hw |
| `sub_024acc` | 0x24acc | 208 | - | C: frames 3.5 bit 4, 3.6 (helper of the header builder: the preamble type of an HT/VHT frame) |
| `sub_02521c` | 0x2521c | 105 | - | N: running average of the BSS's RSSI; mac80211 signal averaging |
| `sub_0255c5` | 0x255c5 | 169 | - | N: control channel of a received beacon's chanspec; cfg80211 BSS channel |
| `sub_025851` | 0x25851 | 12 | - | N: count helper; none |
| `sub_0258ee` | 0x258ee | 147 | - | N: association request fields; mac80211 builds the frame |
| `sub_025fb0` | 0x25fb0 | 166 | - | C: ps-keys 1.6 |
| `sub_026763` | 0x26763 | 132 | - | C: ps-keys 2.4 |
| `sub_02699a` | 0x2699a | 110 | - | N: flow control towards the glue; mac80211 queue stop/wake |
| `sub_028233` | 0x28233 | 190 | - | C: wlc-hw 5.3 (air time of the beacon bytes before the timestamp) |
| `sub_0288ef` | 0x288ef | 349 | - | C: frames 3.3 (helper of the header builder sub_031322; the AC header's fields are given there) |
| `sub_028bed` | 0x28bed | 66 | - | C: frames 3.7 (durations of protection/ack frames; a mac80211 driver sends mac80211's duration) |
| `sub_028c92` | 0x28c92 | 108 | - | C: frames 3.7 (durations; mac80211 computes them) |
| `sub_028cfe` | 0x28cfe | 85 | - | C: frames 3.7 (durations; mac80211 computes them) |
| `sub_029210` | 0x29210 | 903 | - | N: admission-control air-time accounting per frame; none |
| `sub_029950` | 0x29950 | 591 | - | N: scan result processing; cfg80211 BSS list |
| `sub_02c473` | 0x2c473 | 215 | - | N: rate history statistics; minstrel_ht |
| `sub_02cef7` | 0x2cef7 | 393 | - | N: segment count of a packet (frames 3.2 says what is posted); mac80211 skb |
| `sub_02d48b` | 0x2d48b | 398 | - | C: ps-keys 1.4 |
| `sub_02eac4` | 0x2eac4 | 268 | - | C: channel-power 3.2 (beacon heard on a quiet channel: mute lifted; for mac80211 the NO_IR flag); coverage G6 |
| `sub_02f263` | 0x2f263 | 1843 | - | N: scan result from a beacon/probe response; cfg80211 BSS list |
| `sub_02f996` | 0x2f996 | 250 | - | C: wlc-hw |
| `sub_03067b` | 0x3067b | 135 | - | not used: software antenna diversity (stf+0xf4/0xf5 enable bytes 0 on this card: coverage verification); not used |
| `sub_030e2f` | 0x30e2f | 413 | - | N: packet length bookkeeping; none |
| `sub_03432b` | 0x3432b | 1904 | - | C: ps-keys |
| `sub_03795d` | 0x3795d | 501 | - | not used: beacon template and its PHY control words (AP/IBSS) |
| `sub_03b36a` | 0x3b36a | 45 | - | C: ps-keys 1.4 |
| `sub_03b7c9` | 0x3b7c9 | 43 | - | C: ps-keys 1.5 (PS control after a transmit status) |
| `sub_03ceb2` | 0x3ceb2 | 1349 | - | C: frames 1 |
| `sub_03d3f7` | 0x3d3f7 | 50 | - | N: A-MPDU state; mac80211 aggregation sessions |
| `sub_03d445` | 0x3d445 | 125 | - | C: ps-keys 2.4 |
| `sub_03ec97` | 0x3ec97 | 37 | - | C: ps-keys 1.7 |
| `sub_03faf2` | 0x3faf2 | 204 | - | C: ps-keys 1.1 |
| `sub_04388f` | 0x4388f | 988 | - | N: deauthentication/disassociation frames received; mac80211 MLME |
| `sub_045018` | 0x45018 | 216 | - | C: coverage G5 (receive chains for SM power save) |
| `sub_04ab93` | 0x4ab93 | 5547 | - | C: ps-keys 1.6; wlc-hw |
| `wlc_8023_etherhdr` | 0x2aff9 | 237 | - | N: 802.3/802.11 header conversion; mac80211 |
| `wlc_BSSinit` | 0x3ae9f | 1174 | - | C: wlc-hw 5.2 |
| `wlc_BSSinit_rateset_filter` | 0x35e22 | 278 | - | C: wlc-hw 5.2 |
| `wlc_apsd_trigger_timeout` | 0x3f88f | 149 | - | C: ps-keys 1.6 |
| `wlc_apsd_trigger_upd` | 0x3018e | 191 | - | C: ps-keys 1.4 |
| `wlc_assoc_capability` | 0x25981 | 169 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_bcn_prb_body` | 0x2d81c | 4154 | - | not used: beacon/probe-response body (AP/IBSS templates) |
| `wlc_bcn_prb_template` | 0x2e856 | 382 | - | not used: beacon/probe-response template (AP/IBSS) |
| `wlc_bcn_tsf_later` | 0x30397 | 284 | - | not used: compares a beacon's timestamp with the TSF (IBSS coalescing) |
| `wlc_beacon_phytxctl` | 0x34e00 | 485 | - | not used: PHY control words of the microcode's beacon (AP/IBSS; not called while the beacon rate is 0: cca-stf part 2 step 4.7) |
| `wlc_beacon_phytxctl_txant_upd` | 0x378a3 | 186 | - | not used: antenna word of the beacon (AP/IBSS) |
| `wlc_bss_clear_bssid` | 0x3b430 | 119 | - | C: wlc-hw 5.7 |
| `wlc_bss_connected` | 0x254d9 | 64 | - | N: association bookkeeping; mac80211 MLME |
| `wlc_bss_eapol_event` | 0x2b743 | 214 | - | N: event to the glue; cfg80211/mac80211 notifications |
| `wlc_bss_eapol_wai_event_indicate` | 0x2b819 | 134 | - | N: event to the glue; cfg80211/mac80211 notifications |
| `wlc_bss_tbtt` | 0x462fd | 259 | - | C: ps-keys 1.6 |
| `wlc_bss_update_beacon` | 0x37b52 | 17 | - | not used: beacon template update (AP/IBSS) |
| `wlc_bss_update_probe_resp` | 0x35862 | 267 | - | not used: probe-response template and rate table (AP/IBSS) |
| `wlc_btc_mode_set` | 0x26b9a | 59 | - | C: coverage G3; bmac-run R3 |
| `wlc_btc_wire_get` | 0x25e4b | 16 | - | C: coverage G3 |
| `wlc_calc_frame_time` | 0x28a4c | 417 | - | C: frames 3.7 (air time of a frame; mac80211 computes durations) |
| `wlc_calc_lsig_len` | 0x28e0a | 236 | - | not used: L-SIG length for the header format of other PHY types (sub_031322 before its AC part) |
| `wlc_clear_bssid` | 0x3a0a8 | 85 | - | C: wlc-hw 5.7 |
| `wlc_clear_mac` | 0x39f3f | 66 | - | N: 802.11h quiet period: own address removed from the AMT (coverage G6); mac80211 does not honour Quiet elements |
| `wlc_compute_bcn_payloadtsfoff` | 0x282f1 | 50 | - | C: wlc-hw 5.3 |
| `wlc_compute_bcntsfoff` | 0x28323 | 87 | - | C: wlc-hw 5.3 |
| `wlc_compute_rtscts_dur` | 0x28d53 | 183 | - | C: frames 3.7 (RTS/CTS duration; mac80211's) |
| `wlc_copyto_shm` | 0x27c23 | 25 | - | C: bmac-init B4 (pass-through) |
| `wlc_do_chanswitch` | 0x3a739 | 407 | - | N: channel switch on a CSA (coverage G6); mac80211's channel switch, then wlc-hw 2 |
| `wlc_dotxstatus` | 0x3b7f4 | 3195 | - | C: frames 4 |
| `wlc_dump_ucode_fatal` | 0x35996 | 310 | - | N: driver bookkeeping; the open driver's probe |
| `wlc_enable_btc_ps_protection` | 0x2b0e6 | 406 | - | C: coverage G3 |
| `wlc_erp_find` | 0x2a0f2 | 71 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_ether_8023hdr` | 0x28456 | 210 | - | N: 802.3/802.11 header conversion; mac80211 |
| `wlc_exptime_check_end` | 0x26bd5 | 54 | - | N: lifetime accounting of transmit status; none |
| `wlc_exptime_start` | 0x26c0b | 34 | - | N: lifetime accounting; none |
| `wlc_fatal_error` | 0x3b6f0 | 127 | - | C: frames 2 |
| `wlc_find_nominal_req_pwr` | 0x2837a | 71 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_find_vendor_ie` | 0x2cbfa | 178 | - | N: information elements; mac80211 builds and parses them |
| `wlc_find_wme_ie` | 0x2ebf0 | 56 | - | N: information elements; mac80211 builds and parses them |
| `wlc_frame_get_ctl` | 0x2d619 | 111 | - | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_freqtrack_reset` | 0x2aec4 | 68 | - | not used: frequency tracking: PHY type 2 only (wlc-hw 3 step 10) |
| `wlc_full_phy_cal` | 0x39e40 | 84 | - | C: coverage G7 (full calibration at a join); acphy-cal-tx "Who starts a calibration" |
| `wlc_get_accum_pmdur` | 0x27f11 | 137 | - | N: power-save duration statistic (iovar); none |
| `wlc_get_current_highest_rate` | 0x2bf7f | 612 | - | N: rate sets; mac80211 rate tables (hardware rate table: wlc-hw 1.6) |
| `wlc_get_rspec_history` | 0x2c1e3 | 656 | - | N: rate history (iovar); none |
| `wlc_getrand` | 0x2c5cb | 107 | - | N: random numbers; kernel |
| `wlc_handle_ap_lost` | 0x3b4a7 | 205 | - | N: association bookkeeping; mac80211 MLME |
| `wlc_hdr_proc` | 0x34fe5 | 544 | - | N: 802.3/802.11 header conversion; mac80211 |
| `wlc_ht_chanspec` | 0x25d4a | 109 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_ht_mimops_cap_update` | 0x2566e | 32 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_ht_update_scbstate` | 0x2a360 | 962 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_ibss_disable` | 0x270bc | 50 | - | not used: IBSS beaconing off (not reached for a station) |
| `wlc_ibss_enable` | 0x2708c | 48 | - | not used: IBSS beaconing (wlc-hw 1.7: not for a station) |
| `wlc_lifetime_set` | 0x2647e | 81 | - | N: per-frame lifetime (frames 3.3 bit 0x1000); mac80211 sets none |
| `wlc_link` | 0x2b89f | 380 | - | N: link event (wlc-hw 5.1); cfg80211 notifications |
| `wlc_lowest_basic_rspec` | 0x2bee9 | 150 | - | N: rate sets; mac80211 rate tables (hardware rate table: wlc-hw 1.6) |
| `wlc_macctrl_init` | 0x26f79 | 129 | - | C: wlc-hw 5.2 |
| `wlc_max_valid_bw_chanspec` | 0x2fe08 | 293 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_mimops_action_ht_send` | 0x44f93 | 133 | - | N: SM power save action frame; mac80211 (the chain change itself: coverage G5) |
| `wlc_mod_prb_rsp_rate_table` | 0x35705 | 349 | - | not used: probe-response rate table (AP/IBSS; bmac-run R6) |
| `wlc_mute` | 0x26ddd | 19 | - | C: bmac-init E4 (pass-through); channel-power 3.2 |
| `wlc_nonerp_find` | 0x2a08f | 99 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_parse_rates` | 0x2bbb1 | 336 | - | N: rate sets; mac80211 rate tables (hardware rate table: wlc-hw 1.6) |
| `wlc_parse_wpa2_ie` | 0x2c95c | 636 | - | N: information elements; mac80211 builds and parses them |
| `wlc_phytxctl1_calc` | 0x3115d | 453 | - | not used: PHY control word 1 of the N/HT PHYs; the AC-PHY uses wlc_acphy_txctl1_calc (frames 3.5, coverage G1) |
| `wlc_pkt_callback_register` | 0x261b2 | 142 | - | N |
| `wlc_pkt_get_mcl` | 0x30b11 | 58 | - | N: packet-tag accessor; none |
| `wlc_pkt_get_seq` | 0x309e8 | 66 | - | N: packet-tag accessor; none |
| `wlc_pkt_set_ack` | 0x30ab6 | 91 | - | C: channel-power 4.4 (OLPC calibration frames) |
| `wlc_pkt_set_core` | 0x30a6b | 75 | - | C: channel-power 4.4 (OLPC calibration frames) |
| `wlc_pkt_set_txpwr_offset` | 0x30a2a | 65 | - | C: channel-power 4.4 (OLPC calibration frames) |
| `wlc_pkttag_info_move` | 0x26393 | 235 | - | N |
| `wlc_pm2_ret_upd_last_wake_time` | 0x2ff2d | 47 | - | C: ps-keys 1.1 |
| `wlc_pm2_sleep_ret_timer_stop` | 0x2ff5c | 27 | - | C: ps-keys 1.7 |
| `wlc_pmkid_event` | 0x2b5f3 | 336 | - | N: event to the glue; cfg80211/mac80211 notifications |
| `wlc_prep80211_raw` | 0x34d0b | 245 | - | C: frames 3.1 (FIFO of raw frames), 3.3; channel-power 4.4 |
| `wlc_prep_sdu` | 0x3d4c2 | 2815 | - | C: frames 3.2-3.3 (the data frame's header through sub_031322); 802.3 conversion, fragmentation, software keys: mac80211 |
| `wlc_prep_sdu_fast` | 0x3dfc1 | 697 | - | C: frames 3.2-3.3 (as wlc_prep_sdu) |
| `wlc_pretbtt_calc` | 0x24ce2 | 185 | - | C: ps-keys 1.6; wlc-hw |
| `wlc_pretbtt_set` | 0x36e34 | 60 | - | C: wlc-hw 5 |
| `wlc_process_brcm_ie` | 0x2585d | 108 | - | N: information elements; mac80211 builds and parses them |
| `wlc_process_extcap_ie` | 0x29f79 | 158 | - | N: information elements; mac80211 builds and parses them |
| `wlc_pspoll_timer_upd` | 0x2ffaf | 115 | - | C: ps-keys 1.4 |
| `wlc_qosinfo_update` | 0x25587 | 62 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_rateprobe` | 0x3f7fd | 59 | - | N: rate probe with a null frame; minstrel_ht |
| `wlc_read_amt` | 0x27bbf | 100 | - | C: ps-keys |
| `wlc_read_ht_add_ie` | 0x2a03f | 40 | - | N: information elements; mac80211 builds and parses them |
| `wlc_read_ht_add_ies` | 0x2ccac | 95 | - | N: information elements; mac80211 builds and parses them |
| `wlc_read_ht_cap_ie` | 0x2a067 | 40 | - | N: information elements; mac80211 builds and parses them |
| `wlc_read_ht_cap_ies` | 0x2cd0b | 95 | - | N: information elements; mac80211 builds and parses them |
| `wlc_recover_pkts` | 0x305c1 | 186 | - | C: frames 2 |
| `wlc_recv` | 0x4c13e | 4758 | - | C: frames 1 |
| `wlc_recv_parse_bcn_prb` | 0x2ec28 | 1595 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_recv_prep_event_rx_frame_data` | 0x2e9d4 | 240 | - | N: event to the glue; cfg80211/mac80211 notifications |
| `wlc_recvdata` | 0x43c6b | 3475 | - | C: frames 1 |
| `wlc_recvdata_ordered` | 0x3ecbc | 2714 | - | C: ps-keys 1.7 (more-data handling); delivery: mac80211 rx |
| `wlc_recvdata_sendup` | 0x30c0f | 544 | - | N: delivery of received frames; mac80211 rx |
| `wlc_remove_wpa2_pmkid` | 0x25a2a | 197 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_reset_pmstate` | 0x3b67e | 114 | - | C: ps-keys 1.8 |
| `wlc_rfd_intr` | 0x26056 | 87 | - | C: frames 2.2 (MI_RFDISABLE); coverage G4 |
| `wlc_rspec_to_rts_rspec` | 0x280f6 | 58 | - | C: frames 3.7 (protection rate) |
| `wlc_rspec_to_rts_rspec_ex` | 0x27ff0 | 262 | - | C: frames 3.7 (protection rate) |
| `wlc_scb_disassoc_cleanup` | 0x397a3 | 97 | - | N: association bookkeeping; mac80211 MLME |
| `wlc_send_action_err` | 0x44e20 | 154 | - | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_send_action_ht_mimops` | 0x44eba | 217 | - | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_sendassocreq` | 0x40058 | 3498 | - | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_sendauth` | 0x40e02 | 589 | - | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_sendctl` | 0x3ea2a | 288 | - | C: ampdu 2.4; ps-keys 1.4 (control frames: BlockAckReq, PS-poll); mac80211 builds them |
| `wlc_senddeauth` | 0x4107d | 146 | - | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_senddisassoc` | 0x437fd | 146 | - | N: management/control frame building; mac80211 builds the frames (headers: frames 3) |
| `wlc_sendnulldata` | 0x3f756 | 167 | - | C: ps-keys 1.4 |
| `wlc_sendpkt` | 0x465d6 | 941 | - | C: frames 3 (data path entry: header 3.3, posting 3.8); queueing: mac80211 |
| `wlc_sendpspoll` | 0x3eb4a | 287 | - | C: ps-keys 1.6 |
| `wlc_set_apsd_stausp` | 0x3a338 | 89 | - | C: ps-keys 1.1 |
| `wlc_set_pmawakebcn` | 0x3a422 | 156 | - | C: ps-keys 1.1; wlc-hw 2.1 |
| `wlc_set_pmoverride` | 0x3fbd7 | 41 | - | C: ps-keys 1.1 |
| `wlc_set_pspoll` | 0x3a391 | 89 | - | C: ps-keys 1.1 |
| `wlc_set_uatbtt` | 0x3a2aa | 142 | - | C: ps-keys 1.1; wlc-hw 3 |
| `wlc_shm_ssid_upd` | 0x28761 | 104 | - | not used: SSID of the probe-response template (AP/IBSS) |
| `wlc_should_retry_suppressed_pkt` | 0x254be | 11 | - | C: ampdu 2.3 |
| `wlc_stas_connected` | 0x25519 | 75 | - | N: association bookkeeping; mac80211 MLME |
| `wlc_switch_shortslot` | 0x3783b | 104 | - | C: wlc-hw |
| `wlc_tbtt` | 0x46400 | 50 | - | C: ps-keys 1.6 |
| `wlc_tbtt_align` | 0x2fa90 | 58 | - | C: wlc-hw 5.3 |
| `wlc_tim_process` | 0x3fd93 | 377 | - | C: ps-keys 1.6 |
| `wlc_tsf_adopt_bcn` | 0x2faca | 318 | - | C: wlc-hw 5.3 |
| `wlc_tx_suspend` | 0x30bf7 | 24 | - | C: bmac-run R2 (pass-through); wlc-hw 6.2 |
| `wlc_tx_suspended` | 0x260ad | 20 | - | C: bmac-run R2 (pass-through) |
| `wlc_txfifo_complete` | 0x304b3 | 270 | - | C: frames 4.3 step 10; ampdu 2.3 |
| `wlc_txflowcontrol` | 0x26ad0 | 83 | - | N |
| `wlc_txflowcontrol_override` | 0x26a35 | 155 | - | N |
| `wlc_txh_get_isAMPDU` | 0x25299 | 33 | - | C: ampdu 1.6 |
| `wlc_txh_info_get_mcs` | 0x2544d | 33 | - | N: transmit status statistics; mac80211 tx status |
| `wlc_txh_set_epoch` | 0x252ba | 52 | - | C: ampdu 1.6 |
| `wlc_txmod_get_pkts_pending` | 0x25150 | 36 | - | N |
| `wlc_txmod_unconfig` | 0x2a26d | 117 | - | N |
| `wlc_txpktcnt` | 0x25174 | 80 | - | N: pending-frame count (ps-keys 1.7); mac80211 queues |
| `wlc_txs_alias_to_old_fmt` | 0x2546e | 80 | - | C: ps-keys 1.5 |
| `wlc_txstop_intr` | 0x260c1 | 104 | - | C: frames 2 |
| `wlc_update_bcn_info` | 0x3b76f | 90 | - | C: ps-keys 1.1 |
| `wlc_update_pmstate` | 0x3b397 | 153 | - | C: ps-keys 1.1 |
| `wlc_vht_get_rate_from_plcp` | 0x25415 | 56 | - | C: frames 1 |
| `wlc_vht_update_scbstate` | 0x29d10 | 617 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_wme_downgrade_fifo` | 0x28130 | 153 | - | N |
| `wlc_wme_wmmac_check_fixup` | 0x281c9 | 106 | - | N: admission-control downgrade of the access category; mac80211 ACM handling |
| `wlc_wpa_mcast_cipher` | 0x2568e | 77 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_write_amt` | 0x27c3c | 157 | - | C: ps-keys |
| `wlc_write_info_elt_safe` | 0x268ec | 62 | - | N: information elements; mac80211 |

#### aes.c

N = no hardware effect: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `aes_cmac` | 0x294 | 393 | - | N |
| `aes_cmac_calc` | 0x41d | 109 | - | N |
| `aes_cmac_gen_subkeys` | 0x1f1 | 163 | - | N |
| `aes_encrypt` | 0x195 | 92 | - | N |
| `sub_000000` | 0x | 90 | - | N |

#### aeskeywrap.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `aes_unwrap` | 0xd00 | 516 | - | N: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto |

#### aiutils.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `ai_corereg` | 0x1d27 | 502 | S R | C: chip-attach; chip "si_corereg (.text+0x01f265) and " |
| `ai_scan` | 0x1f1d | 1491 | S R | C: chip-attach A4; chip "ai_scan (.text+0x001f1d, name or" |
| `sub_00133f` | 0x133f | 66 | S R | C: chip-attach A4; chip "ai_scan (.text+0x001f1d, name or" |
| `sub_001381` | 0x1381 | 266 | S R | C: chip-attach A4; chip "ai_scan (.text+0x001f1d, name or" |

#### bcm_mpool.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `bcm_mp_alloc` | 0x2775 | 107 | S R | C: chip-attach D2 |
| `bcm_mp_free` | 0x260b | 86 | S R | C: chip-attach |
| `bcm_mpm_create_heap_pool` | 0x2970 | 37 | S R | C: chip-attach |
| `bcm_mpm_create_prealloc_pool` | 0x2995 | 119 | S R | C: chip-attach |
| `bcm_mpm_deinit` | 0x270d | 104 | S R | C: chip-attach |
| `bcm_mpm_delete_heap_pool` | 0x26f7 | 11 | S R | C: chip-attach |
| `bcm_mpm_delete_prealloc_pool` | 0x2702 | 11 | S R | C: chip-attach |
| `bcm_mpm_init` | 0x2a0c | 272 | S R | C: chip-attach D2 |
| `sub_002661` | 0x2661 | 150 | S R | C: chip-attach |
| `sub_0027e0` | 0x27e0 | 400 | S R | C: chip-attach |

#### bcm_notif.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `bcm_notif_add_interest` | 0x2c9d | 129 | S R | C: chip-attach |
| `bcm_notif_attach` | 0x2dbd | 179 | S R | C: chip-attach |
| `bcm_notif_create_list` | 0x2d1e | 71 | S R | C: chip-attach |
| `bcm_notif_delete_list` | 0x2c16 | 135 | S R | C: chip-attach |
| `bcm_notif_detach` | 0x2db2 | 11 | S R | C: chip-attach |
| `bcm_notif_remove_interest` | 0x2b78 | 158 | S R | C: chip-attach |
| `sub_002d65` | 0x2d65 | 77 | S R | C: chip-attach |
| `bcm_notif_signal` | 0x2b1c | 84 | - | N: notifier lists; kernel |

#### bcm_ol_msg.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `bcm_olmsg_create` | 0x2e70 | 105 | S R | not used: offload engine / wake-on-WLAN, not used by a mac80211 driver |
| `bcm_olmsg_init` | 0x2ed9 | 73 | S R | not used: offload engine / wake-on-WLAN, not used by a mac80211 driver |

#### bcmotp.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `otp_init` | 0x329f | 195 | S R | C: chip-attach C; srom-otp "otp_init (.text+0x00329f, name o" |
| `otp_read_word` | 0x3362 | 157 | S R | C: chip-attach C; srom-otp "otp_read_region (.text+0x0033ff," |
| `sub_0038d3` | 0x38d3 | 176 | S R | C: chip-attach; srom-otp "ipxotp_read_bit (sub_0038d3, .te" |
| `sub_003983` | 0x3983 | 123 | S R | C: chip-attach; srom-otp "ipxotp_otpr (sub_003983, .text+0" |
| `sub_0039fe` | 0x39fe | 85 | S R | C: chip-attach; srom-otp "ipxotp_read_word (sub_0039fe), i" |
| `sub_003d6a` | 0x3d6a | 1950 | S R | C: chip-attach; srom-otp "ipxotp_init (sub_003d6a, .text+0" |

#### bcmsrom.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `srom_var_deinit` | 0x4510 | 13 | S R | C: chip-attach A6 |

#### bcmutils.c

N = no hardware effect: packet queues/TLV helpers; kernel skb queues and mac80211.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `bcm_iovar_lencheck` | 0xa957 | 79 | S R | C: chip-attach D3 |
| `pktq_flush` | 0xc0b1 | 94 | S R | C: chip-attach D3 |
| `pktq_init` | 0xb520 | 83 | S R | C: chip-attach D3 |
| `pktq_mdeq` | 0xba5c | 213 | R | C: chip-attach D3 |
| `pktq_penq` | 0xbf48 | 107 | R | C: chip-attach D3 |
| `pktsegcnt` | 0xc1fd | 39 | R | C: chip-attach D3 |
| `pkttotlen` | 0xc259 | 69 | R | C: chip-attach D3 |
| `bcm_next_tlv` | 0xaa17 | 56 | - | N |
| `bcm_parse_tlvs` | 0xaa4f | 61 | - | N |
| `hndcrc32` | 0xa9ec | 43 | - | N |
| `pktcopy` | 0xc47b | 251 | - | N |
| `pktlast` | 0xc224 | 53 | - | N |
| `pktq_pdeq` | 0xbe80 | 95 | - | N |
| `pktq_pdeq_tail` | 0xbd7a | 142 | - | N |
| `pktq_peek_tail` | 0xa512 | 61 | - | N |
| `pktq_penq_head` | 0xbedf | 105 | - | N |
| `pktq_pflush` | 0xbfb3 | 254 | - | N |
| `pktsegcnt_war` | 0xc17a | 131 | - | N |
| `pktsetprio` | 0xb665 | 287 | - | N |
| `prhex` | 0xb209 | 259 | - | N |

#### bcmwifi_channels.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_00c6c8` | 0xc6c8 | 62 | S R | C: chip-attach |
| `sub_00cc2a` | 0xcc2a | 72 | S R | C: chip-attach |
| `wf_chspec_aton` | 0xcc72 | 950 | S R | C: chip-attach D4 |

#### bcmwpa.c

N = no hardware effect: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `bcm_find_wmeie` | 0xd534 | 157 | - | N |
| `bcm_find_wpaie` | 0xd5d1 | 110 | - | N |
| `bcm_has_ie` | 0xd316 | 115 | - | N: information elements; mac80211 builds and parses them |
| `bcmwpa_akm2WPAauth` | 0xd285 | 145 | - | N |
| `bcmwpa_cipher2wsec` | 0xd24a | 59 | - | N |
| `sub_00d3a7` | 0xd3a7 | 116 | - | N |
| `wpa2_cipher` | 0xd41b | 21 | - | N |
| `wpa_array_cmp` | 0xd1e0 | 74 | - | N |
| `wpa_calc_ptk` | 0xd7e4 | 311 | - | N |
| `wpa_check_mic` | 0xd6a1 | 133 | - | N |
| `wpa_cipher` | 0xd430 | 21 | - | N |
| `wpa_decr_gtk` | 0xda24 | 26 | - | N |
| `wpa_decr_key_data` | 0xd91b | 265 | - | N |
| `wpa_find_gtk_encap` | 0xd4b6 | 16 | - | N |
| `wpa_find_kde` | 0xd445 | 113 | - | N |
| `wpa_incr_array` | 0xd22a | 32 | - | N |
| `wpa_make_mic` | 0xd63f | 98 | - | N |

#### hmac.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `hmac_md5` | 0xdba8 | 336 | - | N: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto |

#### hndpmu.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `si_pmu_chip_init` | 0x15543 | 223 | S R | C: chip-attach A3 |
| `si_pmu_otp_chipcontrol` | 0x15474 | 207 | S R | C: chip-attach |
| `si_pmu_otp_regcontrol` | 0x15622 | 217 | S R | C: chip-attach |
| `si_pmu_sprom_enable` | 0x116d5 | 142 | S R | C: chip-attach |
| `si_pmu_swreg_init` | 0x156fb | 802 | S R | C: chip-attach A3 |

#### md5.c

N = no hardware effect: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `MD5Final` | 0x17bbe | 218 | - | N |
| `MD5Init` | 0x1725c | 47 | - | N |
| `MD5Update` | 0x17b00 | 190 | - | N |
| `sub_01728b` | 0x1728b | 2165 | - | N |

#### nicpci.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `pcicore_attach` | 0x19154 | 497 | S R | C: chip-attach B3; chip "pcicore_attach (.text+0x019154, " |
| `pcicore_deinit` | 0x18b4c | 28 | S R | C: chip-attach B11; chip "pcicore_init (.text+0x018b68), p" |
| `pcicore_down` | 0x19090 | 65 | S R | C: chip-attach B9; chip "pcicore_down, pcicore_sleep, pci" |
| `pcicore_find_pci_capability` | 0x18156 | 390 | S R | C: chip-attach A3; chip "pcicore_find_pci_capability (.te" |
| `pcicore_hwup` | 0x19345 | 760 | S R | C: chip-attach B4; chip "pcicore_down, pcicore_sleep, pci" |
| `pcicore_init` | 0x18b68 | 245 | S R | C: chip-attach B2; chip "pcicore_init (.text+0x018b68), p" |
| `pcicore_pmecap_fast` | 0x182dc | 66 | S R | C: chip-attach B12; chip "pcicore_pmecap_fast (.text+0x018" |
| `pcicore_sleep` | 0x19015 | 123 | S R | C: chip-attach B10; chip "pcicore_down, pcicore_sleep, pci" |
| `pcicore_up` | 0x196d0 | 228 | S R | C: chip-attach B8; chip "pcicore_up (.text+0x0196d0, name" |
| `pcie_ltrenable` | 0x18060 | 123 | S R | C: chip-attach B6; chip "si_pcieltrenable (.text+0x01ec6f" |
| `pcie_obffenable` | 0x17fe1 | 127 | S R | C: chip-attach B5; chip "si_pcieobffenable (.text+0x01ec4" |
| `sub_01963d` | 0x1963d | 147 | S R | C: chip-attach B8; chip "pcicore_up (.text+0x0196d0, name" |

#### passhash.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `init_passhash` | 0x19d66 | 115 | - | N: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto |

#### prf.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `fPRF` | 0x19f80 | 587 | - | N: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto |
| `hmac_sha1` | 0x1a1cb | 345 | - | N: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto |

#### rc4.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `prepare_key` | 0x1abc8 | 111 | - | N: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto |
| `rc4` | 0x1ac37 | 101 | - | N: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto |

#### rijndael-alg-fst.c

N = no hardware effect: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `rijndaelDecrypt` | 0x1b7f6 | 1302 | - | N |
| `rijndaelEncrypt` | 0x1b2e1 | 1301 | - | N |
| `rijndaelKeySetupDec` | 0x1b075 | 620 | - | N |
| `rijndaelKeySetupEnc` | 0x1ac9c | 985 | - | N |

#### sha1.c

N = no hardware effect: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `SHA1Input` | 0x1dc53 | 175 | - | N |
| `SHA1Reset` | 0x1da20 | 86 | - | N |
| `SHA1Result` | 0x1dd02 | 334 | - | N |
| `sub_01da76` | 0x1da76 | 477 | - | N |

#### siutils.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `si_alp_clock` | 0x1f1d5 | 63 | S R | C: chip-attach |
| `si_attach` | 0x23844 | 182 | S R | C: chip-attach A2; chip "si_attach (.text+0x023844, name " |
| `si_chipcontrl_read` | 0x1fdea | 70 | S R | C: chip-attach; srom-otp |
| `si_chipcontrl_restore` | 0x1fe30 | 70 | S R | C: chip-attach; srom-otp |
| `si_clkctl_fast_pwrup_delay` | 0x20300 | 440 | S R | C: chip-attach |
| `si_clkctl_init` | 0x204b8 | 355 | S R | C: chip-attach |
| `si_coded_devpathvar` | 0x1f0a7 | 276 | S R | C: chip-attach A5; srom-otp |
| `si_deregister_intr_callback` | 0x1df07 | 14 | S R | C: chip-attach; chip "si_deregister_intr_callback (.te" |
| `si_detach` | 0x2171d | 197 | S R | C: chip-attach A6; chip "si_detach (.text+0x02171d, name " |
| `si_devpath` | 0x1efe9 | 190 | S R | C: chip-attach; srom-otp |
| `si_getdevpathintvar` | 0x218b6 | 106 | S R | C: chip-attach A5; chip "si_getdevpathintvar (.text+0x021" |
| `si_is_otp_disabled` | 0x1e29b | 180 | S R | C: chip-attach C; srom-otp |
| `si_is_otp_powered` | 0x1e987 | 23 | S R | C: chip-attach C; srom-otp |
| `si_is_sprom_available` | 0x1f509 | 483 | S R | C: chip-attach C; srom-otp "si_is_sprom_available (.text+0x0" |
| `si_ldo_war` | 0x1e6bb | 301 | S R | C: chip-attach A3; chip "si_ldo_war (.text+0x01e6bb, name" |
| `si_osh` | 0x1dec7 | 10 | S R | C: chip-attach; chip "si_osh (.text+0x01dec7, name ori" |
| `si_pci_down` | 0x2137e | 122 | S R | C: chip-attach B9; chip "si_pci_down (.text+0x02137e, nam" |
| `si_pci_fixcfg` | 0x21e58 | 226 | S R | C: chip-attach B4; chip "si_pci_fixcfg (.text+0x021e58, n" |
| `si_pci_setup` | 0x215bd | 305 | S R | C: chip-attach B7; chip "si_pci_setup (.text+0x0215bd, na" |
| `si_pci_sleep` | 0x1ecf6 | 28 | S R | C: chip-attach B10; chip "si_pci_sleep (.text+0x01ecf6, na" |
| `si_pci_up` | 0x213f8 | 224 | S R | C: chip-attach B8; chip "si_pci_up (.text+0x0213f8, name " |
| `si_pcie_ltr_war` | 0x1f905 | 146 | S R | C: chip-attach B6; chip "si_pcie_ltr_war (.text+0x01f905," |
| `si_pcieltrenable` | 0x1ec6f | 45 | S R | C: chip-attach B6; chip "si_pcieltrenable (.text+0x01ec6f" |
| `si_pcieobffenable` | 0x1ec42 | 45 | S R | C: chip-attach B5; chip "si_pcieobffenable (.text+0x01ec4" |
| `si_pmu_res_req_timer_clr` | 0x243cc | 68 | S R | C: chip-attach D1 |
| `si_register_intr_callback` | 0x1dedb | 44 | S R | C: chip-attach; chip "si_register_intr_callback (.text" |
| `sub_0217e2` | 0x217e2 | 105 | S R | C: chip-attach; srom-otp |
| `sub_021d1d` | 0x21d1d | 54 | S R | C: chip-attach A3, D1; chip "sub_021d1d (.text+0x021d1d, name" |
| `sub_022b10` | 0x22b10 | 3115 | S R | C: chip-attach A3; chip "sub_022b10 (.text+0x022b10, name" |

#### tkhash.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `tkhash_phase1` | 0x24410 | 493 | - | N: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto |
| `tkhash_phase2` | 0x245fd | 591 | - | N: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto |

#### tkmic.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `tkip_mic` | 0x2484c | 226 | - | N: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto |
| `tkip_mic_eom` | 0x2492e | 66 | - | N: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto |

#### wlc_11d.c

N = no hardware effect: country information element; cfg80211 regulatory (limits: channel-power 1).

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_11d_attach` | 0x4d97d | 168 | S R | N |
| `wlc_11d_detach` | 0x4d935 | 72 | S R | N |
| `wlc_11d_set_autocountry_default` | 0x4d3f1 | 20 | S R | C: channel-power 1.1 |
| `sub_04d405` | 0x4d405 | 298 | - | N |
| `wlc_11d_adopt_country` | 0x4d52f | 210 | - | C: channel-power 1.1, 2.2 (country of the AP adopted; cfg80211 regulatory hints instead) |
| `wlc_11d_autocountry_adopted` | 0x4d3de | 9 | - | N |
| `wlc_11d_get_autocountry_default` | 0x4d3e7 | 10 | - | N |
| `wlc_11d_scan_complete` | 0x4d601 | 820 | - | N |

#### wlc_11h.c

N = no hardware effect: spectrum management frames and CSA state; mac80211 CSA handling.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_04dc60` | 0x4dc60 | 8 | S R | N |
| `sub_04dc68` | 0x4dc68 | 6 | S R | N |
| `wlc_11h_attach` | 0x4e28d | 223 | S R | N |
| `wlc_11h_detach` | 0x4e245 | 72 | S R | N |
| `wlc_11h_get_spect` | 0x4dc74 | 9 | - | N |
| `wlc_11h_process_beacon` | 0x4e1af | 150 | - | N |
| `wlc_11h_set_spect_state` | 0x4dc7d | 21 | - | N |
| `wlc_11h_write_sup_chan_ie` | 0x4e010 | 415 | - | N: information elements; mac80211 builds and parses them |
| `wlc_recv_frameaction_specmgmt` | 0x4dd07 | 777 | - | N |
| `wlc_validate_measure_req` | 0x4dcd7 | 48 | - | N |

#### wlc_airtime.c

N = no hardware effect: air-time arithmetic; mac80211 duration computation (frames 3.7).

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_04e61c` | 0x4e61c | 116 | - | N |
| `sub_04e690` | 0x4e690 | 28 | - | N |
| `wlc_airtime_dot11hdrsize` | 0x4e6b7 | 31 | - | N |
| `wlc_airtime_payload_time_us` | 0x4e7d4 | 40 | - | N |
| `wlc_airtime_pkt_overhead_us` | 0x4e6d6 | 254 | - | N |
| `wlc_airtime_plcp_time_us` | 0x4e6ac | 11 | - | N |

#### wlc_alloc.c

N = no hardware effect: allocation of the driver state; the open driver's probe.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_04e895` | 0x4e895 | 138 | S R | N |
| `wlc_attach_malloc` | 0x4ebbb | 1237 | S R | N |
| `wlc_detach_mfree` | 0x4e91f | 622 | S R | N |

#### wlc_ampdu.c

N = no hardware effect: Block Ack session and reorder bookkeeping; mac80211 aggregation sessions and reorder buffer.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_054a3f` | 0x54a3f | 199 | S R | N |
| `sub_054b06` | 0x54b06 | 74 | S R | N |
| `sub_055384` | 0x55384 | 817 | S R | C: ampdu 1.8, 3.1 |
| `sub_05597e` | 0x5597e | 170 | S R | N |
| `wlc_ampdu_agg_state_update_tx` | 0x54897 | 302 | R | N |
| `wlc_ampdu_macaddr_upd` | 0x4f4f0 | 81 | S R | C: ampdu 1.2; wlc-hw 1.4 |
| `wlc_ampdu_tx_attach` | 0x54be1 | 1090 | S R | C: ampdu 1.1 |
| `wlc_ampdu_tx_detach` | 0x54b50 | 145 | S R | N |
| `wlc_ampdu_tx_reset` | 0x52490 | 162 | S R | C: ampdu 3.2 |
| `wlc_ampdu_tx_set` | 0x4f624 | 444 | S R | C: ampdu 1.1 |
| `wlc_ampdu_upd_pm` | 0x4f33d | 36 | R | C: ampdu 1.8 |
| `wlc_ampdu_was_ampdu` | 0x4f2a6 | 24 | R | C: ampdu 1.6 |
| `wlc_sidechannel_init` | 0x4f307 | 54 | S R | C: wlc-hw 1.4 step 27 (nothing on revision 42) |
| `ampdu_cleanup_tid_ini` | 0x507b0 | 493 | - | C: ampdu 3.2 |
| `scb_ampdu_tx_flush` | 0x549dc | 88 | - | N |
| `sub_04f090` | 0x4f090 | 390 | - | C: ampdu 1.3 |
| `sub_04f382` | 0x4f382 | 105 | - | N |
| `sub_04f7e0` | 0x4f7e0 | 137 | - | N |
| `sub_04f8bb` | 0x4f8bb | 1341 | - | C: ampdu 1.8 |
| `sub_04fdf8` | 0x4fdf8 | 220 | - | C: ampdu 2.4 |
| `sub_04fed4` | 0x4fed4 | 465 | - | C: ampdu 2.4 |
| `sub_0500a5` | 0x500a5 | 511 | - | C: ampdu 2.4 |
| `sub_0502a4` | 0x502a4 | 662 | - | C: ampdu 1.7 |
| `sub_050a36` | 0x50a36 | 114 | - | N |
| `sub_050e35` | 0x50e35 | 392 | - | C: ampdu 2.3 |
| `sub_050fbd` | 0x50fbd | 89 | - | N |
| `sub_0527b5` | 0x527b5 | 6234 | - | C: ampdu |
| `sub_054128` | 0x54128 | 1636 | - | C: ampdu 1.5, 1.6 |
| `wlc_ampdu_change_epoch` | 0x4f26c | 58 | - | C: ampdu 1.6 |
| `wlc_ampdu_chgnsav_epoch` | 0x5400f | 281 | - | C: ampdu 1.6 |
| `wlc_ampdu_dotxstatus` | 0x51016 | 5242 | - | C: ampdu 2 |
| `wlc_ampdu_dotxstatus_regmpdu` | 0x52599 | 540 | - | C: ampdu 2.5 |
| `wlc_ampdu_frameburst_override` | 0x4f216 | 19 | - | C: ampdu 1.4 |
| `wlc_ampdu_null_delim_cnt` | 0x4f541 | 227 | - | N |
| `wlc_ampdu_recv_addba_req_ini` | 0x4f869 | 82 | - | N |
| `wlc_ampdu_recv_addba_resp` | 0x5053a | 630 | - | C: ampdu 3.1 |
| `wlc_ampdu_recv_ba` | 0x4f2be | 16 | - | C: ampdu 3.3 |
| `wlc_ampdu_tx_recv_delba` | 0x5099d | 59 | - | N |
| `wlc_ampdu_tx_send_delba` | 0x509d8 | 94 | - | N |
| `wlc_dump_aggfifo` | 0x50aa8 | 909 | - | N: driver bookkeeping; the open driver's probe |
| `wlc_sendampdu` | 0x5478c | 267 | - | C: ampdu 1.5 |

#### wlc_ampdu_cmn.c

N = no hardware effect: Block Ack session and reorder bookkeeping; mac80211 aggregation sessions and reorder buffer.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_ampdu_deinit` | 0x55ffb | 29 | S R | N |
| `wlc_ampdu_init` | 0x56018 | 90 | S R | N |
| `scb_ampdu_cleanup` | 0x55ac9 | 72 | - | N |
| `scb_ampdu_cleanup_all` | 0x55a5a | 111 | - | N |
| `wlc_ampdu_recv_addba_req` | 0x55eba | 37 | - | N |
| `wlc_ampdu_recv_ctl` | 0x55edf | 84 | - | N |
| `wlc_ampdu_recv_delba` | 0x55b11 | 73 | - | N |
| `wlc_frameaction_ampdu` | 0x55f8d | 110 | - | N |
| `wlc_scb_ampdu_disable` | 0x55f33 | 44 | - | N |
| `wlc_scb_ampdu_enable` | 0x55f5f | 46 | - | N |
| `wlc_send_bar` | 0x55b5a | 274 | - | C: ampdu 2.4 |
| `wlc_send_delba` | 0x55c6c | 210 | - | N |

#### wlc_ampdu_rx.c

N = no hardware effect: Block Ack session and reorder bookkeeping; mac80211 aggregation sessions and reorder buffer.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_056565` | 0x56565 | 102 | S R | N |
| `sub_056d9e` | 0x56d9e | 206 | S R | N |
| `sub_05725f` | 0x5725f | 89 | S R | N |
| `sub_0572b8` | 0x572b8 | 75 | S R | N |
| `wlc_ampdu_rx_attach` | 0x567c1 | 479 | S R | C: ampdu 3.3 |
| `wlc_ampdu_rx_detach` | 0x56736 | 139 | S R | N |
| `wlc_ampdu_rx_set` | 0x564e5 | 128 | S R | N |
| `wlc_ampdu_shm_upd` | 0x56452 | 147 | S R | C: ampdu 1.2, 3.3; wlc-hw 1.4 |
| `ampdu_cleanup_tid_resp` | 0x56cb1 | 152 | - | N |
| `scb_ampdu_rx_flush` | 0x5722d | 50 | - | N |
| `sub_0569a0` | 0x569a0 | 435 | - | N |
| `sub_0570e0` | 0x570e0 | 186 | - | N |
| `wlc_ampdu_agg_state_update_rx` | 0x571fc | 49 | - | N |
| `wlc_ampdu_recv_addba_req_resp` | 0x56e99 | 583 | - | C: ampdu 3.3 |
| `wlc_ampdu_recv_bar` | 0x575c9 | 144 | - | C: ampdu 3.3 |
| `wlc_ampdu_recvdata` | 0x57659 | 699 | - | N |
| `wlc_ampdu_release_n_ordered` | 0x56b53 | 350 | - | N |
| `wlc_ampdu_release_ordered` | 0x5748d | 316 | - | N |
| `wlc_ampdu_rx_recv_delba` | 0x56e6c | 45 | - | N |
| `wlc_ampdu_rx_send_delba` | 0x56d49 | 85 | - | N |
| `wlc_ampdu_update_rxcounters` | 0x5621c | 158 | - | N |
| `wlc_send_addba_resp` | 0x56360 | 242 | - | N |

#### wlc_amsdu.c

N = no hardware effect: A-MSDU aggregation/deaggregation; mac80211 A-MSDU.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_057946` | 0x57946 | 93 | S R | N |
| `sub_057bae` | 0x57bae | 73 | S R | N |
| `sub_057c08` | 0x57c08 | 13 | S R | N |
| `sub_057c8d` | 0x57c8d | 84 | S R | N |
| `wlc_amsdu_agg_flush` | 0x582fe | 248 | S R | N |
| `wlc_amsdu_attach` | 0x58fca | 574 | S R | C: ampdu 4 |
| `wlc_amsdu_detach` | 0x589d5 | 97 | S R | N |
| `wlc_amsdurx_cap` | 0x57932 | 20 | S R | N |
| `sub_057ce1` | 0x57ce1 | 228 | - | N |
| `sub_05800c` | 0x5800c | 382 | - | N |
| `wlc_amsdu_deagg_hw` | 0x57dc5 | 583 | - | N |
| `wlc_amsdu_deagg_sw` | 0x57a33 | 379 | - | N |
| `wlc_amsdu_dotxstatus` | 0x58611 | 107 | - | N |
| `wlc_amsdu_flush` | 0x57bf7 | 17 | - | N |
| `wlc_amsdu_scb_ht_agglimit_upd` | 0x579dd | 51 | - | N |
| `wlc_amsdu_scb_vht_agglimit_upd` | 0x579a9 | 52 | - | N |
| `wlc_amsdu_txop_upd` | 0x579a3 | 6 | - | N |
| `wlc_recvamsdu` | 0x5818a | 372 | - | C: frames 1 |

#### wlc_antsel.c

N = no hardware effect: antenna-selection state; not used on this board.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_059348` | 0x59348 | 113 | S R | N |
| `wlc_antsel_attach` | 0x5970a | 610 | S R | C: bmac-run R8 (antenna-selection type 0 on this card) |
| `wlc_antsel_detach` | 0x596cd | 61 | S R | N |
| `wlc_antsel_init` | 0x596a7 | 38 | S R | C: wlc-hw 1.4 (nothing on this card) |
| `sub_059510` | 0x59510 | 116 | - | N |
| `sub_0595b8` | 0x595b8 | 101 | - | not used: antenna-selection word for boards with an antenna switch (wlc_antsel_init does nothing on this card: wlc-hw 1.4) |
| `wlc_antsel_antcfg_get` | 0x593dc | 201 | - | N |
| `wlc_antsel_antsel2id` | 0x594a5 | 107 | - | N |
| `wlc_antsel_buildtxh` | 0x59584 | 52 | - | C: frames 3.3 (antenna-selection word for the header of other formats; 0 on this card) |
| `wlc_antsel_ratesel` | 0x593b9 | 25 | - | N |
| `wlc_antsel_set_unicast` | 0x593d2 | 10 | - | N |
| `wlc_antsel_upd_dflt` | 0x5961d | 138 | - | N |

#### wlc_assoc.c

N = no hardware effect: join/roam state machine; mac80211 MLME and wpa_supplicant.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_05a349` | 0x5a349 | 46 | S R | N |
| `wlc_assoc_abort` | 0x5bab2 | 425 | S R | C: wlc-hw 1.4 step 37 |
| `wlc_mac_request_entry` | 0x5bc5b | 753 | R | N |
| `wlc_roam_defaults` | 0x59f25 | 155 | S R | N |
| `sub_059b7c` | 0x59b7c | 251 | - | N |
| `sub_059cf9` | 0x59cf9 | 178 | - | N |
| `sub_05a260` | 0x5a260 | 233 | - | N |
| `sub_05a50d` | 0x5a50d | 838 | - | N |
| `sub_05ab9c` | 0x5ab9c | 72 | - | N |
| `sub_05ad15` | 0x5ad15 | 108 | - | N |
| `sub_05b6db` | 0x5b6db | 194 | - | N |
| `sub_05b79d` | 0x5b79d | 789 | - | N |
| `sub_05c425` | 0x5c425 | 495 | - | C: wlc-hw 5.7 (association cleared before a new join; host flag by wlc_key_defkeyflag: ps-keys 2) |
| `sub_05c7cc` | 0x5c7cc | 869 | - | N: join start (scan for the target); mac80211 MLME (cca_stats_upd: cca-stf part 1) |
| `sub_05cb31` | 0x5cb31 | 80 | - | N |
| `sub_05cd9c` | 0x5cd9c | 487 | - | C: wlc-hw 5.1 |
| `sub_05d33f` | 0x5d33f | 329 | - | N |
| `sub_05d488` | 0x5d488 | 62 | - | N |
| `sub_05d4c6` | 0x5d4c6 | 2420 | - | N |
| `sub_05de3a` | 0x5de3a | 336 | - | N |
| `sub_05f235` | 0x5f235 | 1550 | - | N |
| `wlc_assoc_change_state` | 0x5c614 | 405 | - | C: coverage G7 (PHY hold 1 while joining; wake bit: ps-keys 1.3) |
| `wlc_assocreq_complete` | 0x5df8a | 141 | - | N |
| `wlc_assocresp_client` | 0x5e017 | 1423 | - | C: wlc-hw 5 |
| `wlc_auth_tx_complete` | 0x5f017 | 136 | - | N |
| `wlc_authresp_client` | 0x5ee28 | 495 | - | N |
| `wlc_build_roam_cache` | 0x5a377 | 406 | - | N |
| `wlc_clear_hw_association` | 0x5abe4 | 305 | - | C: wlc-hw 5.7 |
| `wlc_deauth_complete` | 0x5a0b2 | 127 | - | N |
| `wlc_deauth_ind_complete` | 0x59fc0 | 121 | - | N |
| `wlc_disassoc_complete` | 0x5a131 | 125 | - | N |
| `wlc_disassoc_ind_complete` | 0x5a039 | 121 | - | N |
| `wlc_disassociate_client` | 0x5bf4c | 1241 | - | C: wlc-hw 5.7 |
| `wlc_join` | 0x5fa85 | 277 | - | N |
| `wlc_join_BSS` | 0x5ae4f | 2188 | - | C: coverage G7 |
| `wlc_join_bss_prep` | 0x5c7a9 | 35 | - | N |
| `wlc_join_complete` | 0x5e5a6 | 1176 | - | C: wlc-hw 5 |
| `wlc_pmkid_build_cand_list` | 0x5f09f | 406 | - | N |
| `wlc_reassoc` | 0x5cc0c | 157 | - | N |
| `wlc_roam_bcns_lost` | 0x5eda1 | 135 | - | N: beacon loss (ps-keys 1.8); mac80211 beacon-loss detection |
| `wlc_roam_complete` | 0x5cca9 | 243 | - | C: coverage G7 (MAC suspended around the BSS update as in wlc-hw 5.1; cca-stf part 1) |
| `wlc_roam_motion_detect` | 0x59dab | 312 | - | N |
| `wlc_roam_scan` | 0x5cb81 | 139 | - | N |
| `wlc_roamscan_start` | 0x5ea3e | 552 | - | N |
| `wlc_sta_assoc_upd` | 0x5ad81 | 206 | - | C: wlc-hw 5.5 |
| `wlc_txrate_roam` | 0x5ec66 | 315 | - | N |

#### wlc_bmac.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_062684` | 0x62684 | 100 | S R | C: bmac-init B10; bmac-run R8 |
| `wlc_bmac_4331_epa_init` | 0x60eb3 | 180 | S R | C: bmac-run R10 |
| `wlc_bmac_4360_pcie2_war` | 0x65338 | 798 | S R | C: bmac-init C12 |
| `wlc_bmac_antsel_set` | 0x60011 | 12 | S R | C: bmac-run R8 |
| `wlc_bmac_antsel_type_set` | 0x64779 | 33 | S R | C: bmac-run R8 |
| `wlc_bmac_btc_params_get` | 0x61995 | 63 | R | C: wlc-hw 3 |
| `wlc_bmac_btc_period_get` | 0x619d4 | 255 | R | C: wlc-hw 3 |
| `wlc_bmac_fifoerrors` | 0x64689 | 240 | S R | C: bmac-run R7 |
| `wlc_bmac_filter_war_upd` | 0x6452e | 26 | S R | C: bmac-run R8 |
| `wlc_bmac_iovars_dispatch` | 0x67056 | 2194 | R | N: variables of the MAC layer (GPIO, SROM, coexistence debug); nl80211 at most |
| `wlc_bmac_set_ctrl_ePA` | 0x64ae5 | 60 | S R | C: bmac-run R10 |
| `wlc_bmac_state_get` | 0x678e8 | 48 | S R | C: bmac-run R7 |
| `wlc_bmac_txant_set` | 0x626e8 | 46 | S R | C: bmac-init E8; bmac-run R8 |
| `wlc_bmac_wait_for_wake` | 0x63946 | 125 | R | C: bmac-init A5 |
| `wlc_bmac_watchdog` | 0x67cf0 | 91 | S R | C: bmac-run R7 |
| `sub_062842` | 0x62842 | 117 | - | C: bmac-init F3 |
| `sub_0628b7` | 0x628b7 | 114 | - | C: bmac-init F3 |
| `sub_064093` | 0x64093 | 176 | - | not used: transmit status of old MAC revisions (bmac-data D8); not revision 42 |
| `wlc_bmac_btc_flags_get` | 0x6019a | 17 | - | C: coverage G3 |
| `wlc_bmac_btc_rssi_threshold_get` | 0x61920 | 117 | - | C: coverage G3 |
| `wlc_bmac_btc_stuck_war50943` | 0x62a99 | 73 | - | not used: coexistence reset for MAC revisions < 15 only (frames 2.2 bit 0x00400000) |
| `wlc_bmac_btc_update_predictor` | 0x62004 | 211 | - | C: coverage G3; frames 2.2 |
| `wlc_bmac_recv` | 0x67d4b | 434 | - | C: frames 1 |
| `wlc_bmac_rm_cca_int` | 0x63268 | 108 | - | C: frames 2.2 (MI_CCA); coverage G6 (radio measurement, not used) |
| `wlc_bmac_set_addrmatch` | 0x60fdb | 144 | - | C: bmac-init E2 |
| `wlc_bmac_set_defmacintmask` | 0x60222 | 24 | - | C: frames 2 |
| `wlc_bmac_txstatus` | 0x64143 | 1003 | - | C: frames 4 |
| `wlc_bmac_write_hw_bcntemplates` | 0x62929 | 108 | - | C: bmac-init F3 |
| `wlc_swdiv_ant_set` | 0x6479a | 237 | - | not used: software antenna diversity (off on this card) |

#### wlc_bsscfg.c

N = no hardware effect: BSS-configuration state; mac80211 vif and bss_conf.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_06ae8c` | 0x6ae8c | 603 | S R | N |
| `sub_06b446` | 0x6b446 | 472 | S R | N |
| `sub_06b61e` | 0x6b61e | 92 | S R | N |
| `sub_06b67a` | 0x6b67a | 492 | S R | N |
| `sub_06b866` | 0x6b866 | 551 | S R | N |
| `sub_06bd92` | 0x6bd92 | 111 | S R | N |
| `sub_06c725` | 0x6c725 | 83 | S R | N |
| `wlc_bsscfg_assoc_params_reset` | 0x6b3f2 | 20 | S R | N |
| `wlc_bsscfg_assoc_params_set` | 0x6b363 | 143 | S R | N |
| `wlc_bsscfg_attach` | 0x6c65b | 202 | S R | N |
| `wlc_bsscfg_cubby_reserve` | 0x6aa64 | 86 | S R | N |
| `wlc_bsscfg_deinit` | 0x6bb37 | 524 | S R | N |
| `wlc_bsscfg_detach` | 0x6c5fb | 96 | S R | N |
| `wlc_bsscfg_ena_cnt` | 0x6aaba | 29 | S R | N |
| `wlc_bsscfg_find_by_wlcif` | 0x6ab77 | 32 | S R | N |
| `wlc_bsscfg_free` | 0x6be01 | 199 | S R | N |
| `wlc_bsscfg_primary_init` | 0x6bec8 | 112 | S R | N |
| `wlc_bsscfg_scan_params_reset` | 0x6b351 | 18 | S R | N |
| `wlc_bsscfg_scan_params_set` | 0x6b2cf | 130 | S R | N |
| `wlc_bsscfg_scbclear` | 0x6bd43 | 79 | S R | N |
| `wlc_bsscfg_set_ext_cap` | 0x6ad4e | 121 | S R | N |
| `wlc_bsscfg_updown_register` | 0x6c5e5 | 22 | S R | N |
| `wlc_bsscfg_updown_unregister` | 0x6c5cf | 22 | S R | N |
| `wlc_bsscfg_wsec_key_buf_free` | 0x6ae55 | 55 | S R | N |
| `wlc_vndr_ie_getlen_ext` | 0x6b25c | 29 | R | N |
| `wlc_vndr_ie_write_ext` | 0x6b234 | 40 | R | N |
| `wlc_bsscfg_SSID_set` | 0x6b279 | 86 | - | N |
| `wlc_bsscfg_disable` | 0x6c177 | 188 | - | C: wlc-hw 4.1 step 3, 5.7 |
| `wlc_bsscfg_down` | 0x6c077 | 256 | - | C: wlc-hw 4.1 step 3 |
| `wlc_bsscfg_enable` | 0x6aadf | 12 | - | N |
| `wlc_bsscfg_find` | 0x6ab44 | 38 | - | N |
| `wlc_bsscfg_find_by_ID` | 0x6ad21 | 34 | - | N |
| `wlc_bsscfg_find_by_bssid` | 0x6abff | 95 | - | N |
| `wlc_bsscfg_find_by_hwaddr` | 0x6ab97 | 104 | - | N |
| `wlc_bsscfg_find_by_ssid` | 0x6b406 | 64 | - | N |
| `wlc_bsscfg_find_by_target_bssid` | 0x6acc2 | 95 | - | N |
| `wlc_bsscfg_set_infra_mode` | 0x6aaeb | 36 | - | C: wlc-hw 5.2 |
| `wlc_bsscfg_start_ibss` | 0x6c2b2 | 797 | - | not used: IBSS start |
| `wlc_bsscfg_up` | 0x6c233 | 127 | - | C: coverage G7 (host flags of a configuration going up) |
| `wlc_bsscfg_wsec_session_reset` | 0x6ae2c | 41 | - | N |

#### wlc_cac.c

N = no hardware effect: admission control (TSPEC); mac80211 WMM ACM handling.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_06c7ef` | 0x6c7ef | 126 | S R | N |
| `sub_06c86d` | 0x6c86d | 6 | S R | N |
| `sub_06c963` | 0x6c963 | 40 | S R | N |
| `wlc_cac_attach` | 0x6cfbe | 310 | S R | N |
| `wlc_cac_detach` | 0x6cf37 | 135 | S R | N |
| `wlc_cac_watchdog` | 0x6ca3b | 204 | S R | N |
| `sub_06c98b` | 0x6c98b | 176 | - | N |
| `sub_06cb07` | 0x6cb07 | 319 | - | N |
| `sub_06cc46` | 0x6cc46 | 252 | - | N |
| `sub_06cd72` | 0x6cd72 | 223 | - | N |
| `sub_06d72b` | 0x6d72b | 99 | - | N |
| `wlc_cac_action_frame` | 0x6d78e | 1430 | - | N |
| `wlc_cac_is_traffic_admitted` | 0x6c879 | 90 | - | N |
| `wlc_cac_on_join_bss` | 0x6c92b | 56 | - | N |
| `wlc_cac_on_leave_bss` | 0x6c8f2 | 57 | - | N |
| `wlc_cac_update_used_time` | 0x6c778 | 92 | - | N |
| `wlc_frameaction_cac` | 0x6dd24 | 88 | - | N |

#### wlc_channel.c

N = no hardware effect: channel-manager bookkeeping (country, restricted/radar lists); cfg80211 regulatory.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_06efa9` | 0x6efa9 | 123 | S R | C: channel-power |
| `sub_06f334` | 0x6f334 | 275 | S R | C: channel-power 3.1; wlc-hw 2 |
| `sub_06f468` | 0x6f468 | 442 | S R | C: channel-power 1.2 |
| `sub_072656` | 0x72656 | 1870 | S R | C: channel-power 1.4 |
| `sub_072da4` | 0x72da4 | 454 | S R | C: channel-power 1.3 |
| `sub_0736a4` | 0x736a4 | 2400 | S R | C: channel-power 2.4 |
| `wlc_channel_country_abbrev` | 0x6e57c | 10 | S R | N |
| `wlc_channel_locale_flags` | 0x6e599 | 25 | R | N |
| `wlc_channel_locale_flags_in_band` | 0x6e5b2 | 16 | R | N |
| `wlc_channel_mgr_attach` | 0x73078 | 1209 | S R | C: channel-power 1.1 |
| `wlc_channel_mgr_detach` | 0x6f6ff | 102 | S R | N |
| `wlc_channel_reg_limits` | 0x7047f | 6413 | S R | C: channel-power 1.6 |
| `wlc_channel_set_chanspec` | 0x7419b | 193 | S R | C: acphy-chanspec; channel-power 1.7; wlc-hw 2.2 |
| `wlc_country_lookup` | 0x6f622 | 33 | S R | C: channel-power 1.1 |
| `wlc_get_flags` | 0x703e2 | 157 | S R | C: channel-power 1.5 |
| `wlc_get_regclass` | 0x6f1a8 | 267 | S R | C: channel-power 1.4 |
| `wlc_locale_get_channels` | 0x71e08 | 79 | S R | C: channel-power 1.4 |
| `wlc_next_chanspec` | 0x7014f | 337 | S R | C: channel-power 3.3 |
| `wlc_quiet_channels_reset` | 0x6f124 | 132 | S R | C: channel-power 1.4 |
| `wlc_quiet_chanspec` | 0x6e5e0 | 438 | S R | C: wlc-hw 2 |
| `wlc_set_countrycode` | 0x7306a | 14 | S R | C: channel-power 1.1 |
| `wlc_set_countrycode_rev` | 0x72f6a | 256 | S R | C: channel-power 1.3 |
| `sub_06ee40` | 0x6ee40 | 33 | - | C: channel-power 2.4 |
| `sub_06ef19` | 0x6ef19 | 144 | - | C: channel-power 2.4 |
| `wlc_channel_get_chanvec` | 0x73531 | 99 | - | N |
| `wlc_channel_set_txpower_limit` | 0x740ed | 174 | - | C: channel-power 1.7 |
| `wlc_clr_quiet_chanspec` | 0x6e97e | 479 | - | C: channel-power 3.2 |
| `wlc_clr_restricted_chanspec` | 0x6ed4c | 232 | - | N |
| `wlc_country_lookup_ext` | 0x6f643 | 188 | - | N |
| `wlc_get_locale` | 0x6fa46 | 11 | - | N |
| `wlc_get_reg_max_power_for_channel` | 0x702fc | 230 | - | N: regulatory maximum for an IE (clm.md lookup); cfg80211 regulatory |
| `wlc_has_restricted_chanspec` | 0x6ee34 | 12 | - | N |
| `wlc_japan` | 0x6ec9c | 43 | - | N |
| `wlc_radar_chanspec` | 0x6ff8c | 300 | - | C: channel-power 3.2 |
| `wlc_rclass_extch_get` | 0x6ecc7 | 117 | - | N |
| `wlc_restricted_chanspec` | 0x6fdba | 466 | - | C: channel-power 3.2 |
| `wlc_set_quiet_chanspec` | 0x6e796 | 488 | - | C: channel-power 3.2 |

#### wlc_cntry.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_cntry_attach` | 0x775d7 | 133 | S R | N: country information element; cfg80211 regulatory (limits: channel-power 1) |
| `wlc_cntry_detach` | 0x775a0 | 55 | S R | N: country information element; cfg80211 regulatory (limits: channel-power 1) |
| `wlc_cntry_set_default` | 0x771b8 | 20 | S R | C: channel-power 1.1 |
| `wlc_cntry_adopt_country_ie` | 0x772ba | 302 | - | C: channel-power 1.7 |
| `wlc_cntry_parse_country_ie` | 0x771e5 | 213 | - | C: channel-power 2.2 (local power constraint; cfg80211/mac80211 pass it as bss_conf power) |
| `wlc_cntry_write_country_ie` | 0x773e8 | 440 | - | N: information elements; mac80211 builds and parses them |

#### wlc_csa.c

N = no hardware effect: spectrum management frames and CSA state; mac80211 CSA handling.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_0787a5` | 0x787a5 | 47 | S R | N |
| `sub_07881d` | 0x7881d | 53 | S R | N |
| `sub_0789b6` | 0x789b6 | 83 | S R | N |
| `sub_078a09` | 0x78a09 | 91 | S R | N |
| `sub_078a64` | 0x78a64 | 138 | S R | N |
| `wlc_csa_attach` | 0x78852 | 252 | S R | N |
| `wlc_csa_detach` | 0x787d4 | 73 | S R | N |
| `sub_078271` | 0x78271 | 708 | - | N |
| `wlc_csa_get_csa_count` | 0x778f8 | 17 | - | N |
| `wlc_csa_quiet_mode` | 0x77f62 | 51 | - | N |
| `wlc_csa_recv_process_beacon` | 0x78535 | 415 | - | N |
| `wlc_csa_reset_all` | 0x77909 | 53 | - | N |
| `wlc_csa_state_active` | 0x77830 | 13 | - | N: CSA in progress (ps-keys 1.2); mac80211 CSA state |
| `wlc_csa_trans` | 0x7811a | 343 | - | N |
| `wlc_csa_write_chan_switch_wrapper_ie` | 0x7804a | 208 | - | N: information elements; mac80211 builds and parses them |
| `wlc_csa_write_csa_ie` | 0x779f3 | 72 | - | N: information elements; mac80211 builds and parses them |
| `wlc_csa_write_ext_csa_ie` | 0x77c05 | 88 | - | N: information elements; mac80211 builds and parses them |
| `wlc_csa_write_extch_ie` | 0x7789e | 84 | - | N: information elements; mac80211 builds and parses them |
| `wlc_recv_csa_action` | 0x77f95 | 181 | - | N |
| `wlc_recv_ext_csa_action` | 0x778f2 | 6 | - | N |
| `wlc_recv_public_csa_action` | 0x786d4 | 209 | - | N |

#### wlc_event.c

N = no hardware effect: event to the glue; cfg80211/mac80211 notifications.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_078ce7` | 0x78ce7 | 43 | R | N: event queue to the glue; cfg80211/mac80211 notifications |
| `wlc_event_alloc` | 0x791b0 | 48 | R | N |
| `wlc_event_free` | 0x791a1 | 15 | R | N |
| `wlc_eventq_attach` | 0x79251 | 195 | S R | N |
| `wlc_eventq_deq` | 0x78c60 | 43 | R | N |
| `wlc_eventq_detach` | 0x791e0 | 113 | S R | N |
| `wlc_eventq_down` | 0x78f8c | 82 | S R | N |
| `wlc_eventq_enq` | 0x7915e | 67 | R | N |
| `wlc_eventq_flush` | 0x78f51 | 59 | S R | N |
| `wlc_eventq_handle_ind` | 0x78fde | 384 | R | N |
| `wlc_eventq_test_ind` | 0x78cc3 | 36 | R | N |

#### wlc_hrt.c

N = no hardware effect: software timer list; kernel timers (the gptimer register: frames 2.1/2.4).

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_0793c9` | 0x793c9 | 78 | R | N |
| `sub_07955e` | 0x7955e | 22 | R | N |
| `sub_0795a1` | 0x795a1 | 24 | R | N |
| `wlc_hrt_add_timeout` | 0x79417 | 155 | R | N |
| `wlc_hrt_alloc_timeout` | 0x795f6 | 48 | S R | N |
| `wlc_hrt_attach` | 0x79626 | 110 | S R | N |
| `wlc_hrt_del_timeout` | 0x7933f | 62 | S R | N |
| `wlc_hrt_detach` | 0x795d7 | 31 | S R | N |
| `wlc_hrt_free_timeout` | 0x795b9 | 30 | S R | N |
| `wlc_hrt_getdelta` | 0x7938d | 60 | R | N |
| `wlc_hrt_gettime` | 0x7937d | 16 | - | N |
| `wlc_hrt_gptimer_get` | 0x7954b | 19 | - | C: frames 2.1 (D11(0x18) gptimer) |
| `wlc_hrt_gptimer_set` | 0x79574 | 24 | - | C: frames 2.1 (D11(0x18) gptimer) |
| `wlc_hrt_isr` | 0x794b2 | 141 | - | C: frames 2.2/2.4 (MI_TO); the timers themselves: kernel timers |

#### wlc_hw.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_skip_adjtsf` | 0x797cf | 120 | R | C: wlc-hw 6.2 |

#### wlc_interfere.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_079c78` | 0x79c78 | 2044 | S R | N: channel-interference assessment, off by default (coverage G6); none in mac80211 |
| `wlc_itfr_attach` | 0x79a27 | 275 | S R | N: interference module set-up (coverage G6) |
| `wlc_itfr_detach` | 0x799f0 | 55 | S R | N: interference module (coverage G6) |

#### wlc_intr.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_intrsoff` | 0x7a6b5 | 107 | S R | C: frames 2; wlc-hw |
| `wlc_intrson` | 0x7a720 | 73 | S R | C: frames 2; wlc-hw |
| `wlc_intrsrestore` | 0x7a684 | 49 | S R | C: frames 2; wlc-hw |
| `sub_07a769` | 0x7a769 | 379 | - | C: frames 2 |
| `wlc_dpc` | 0x7a99a | 618 | - | C: frames 2 |
| `wlc_isr` | 0x7a8e4 | 141 | - | C: frames 2 |

#### wlc_key.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_key_hw_init_all` | 0x7b763 | 447 | S R | C: ps-keys 2.3; wlc-hw 1.4 |
| `sub_07c1c1` | 0x7c1c1 | 210 | - | C: ps-keys 2.2 |
| `wlc_key_defkeyflag` | 0x7ac3d | 136 | - | C: ps-keys |
| `wlc_key_delete` | 0x7bd56 | 482 | - | C: ps-keys 1.1 |
| `wlc_key_hw_init` | 0x7acc5 | 1015 | - | C: ps-keys 2.3 |
| `wlc_key_hw_reallocate` | 0x7bc40 | 278 | - | C: ps-keys 2.2 (slot allocation; with mac80211 the driver allocates slots in set_key) |
| `wlc_key_insert` | 0x7c3be | 2145 | - | C: ps-keys 1.1 |
| `wlc_key_iv_init` | 0x7bb56 | 234 | - | C: ps-keys 2.2 |
| `wlc_key_iv_update` | 0x7b922 | 564 | - | C: ps-keys 2.4 |
| `wlc_key_lookup` | 0x7ac04 | 57 | - | C: ps-keys |
| `wlc_key_scb_delete` | 0x7c136 | 139 | - | C: ps-keys 2.2-2.3 (a station's keys removed; mac80211 calls set_key DISABLE_KEY) |
| `wlc_key_update` | 0x7b6a8 | 187 | - | C: ps-keys 2.2 |
| `wlc_rcmta_add_bssid` | 0x7c293 | 299 | - | C: ps-keys |
| `wlc_rcmta_del_bssid` | 0x7c0c7 | 111 | - | C: ps-keys 2.2 (the key-peer entry of the BSSID removed) |

#### wlc_led.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_07ceb5` | 0x7ceb5 | 35 | S R | C: coverage G2; bmac-run R4 |
| `sub_07cf8d` | 0x7cf8d | 132 | S R | C: coverage G2; bmac-run R4 |
| `sub_07d011` | 0x7d011 | 132 | S R | C: coverage G2; bmac-run R4 |
| `sub_07d1a0` | 0x7d1a0 | 31 | S R | C: coverage G2; wlc-hw 3 step 13 |
| `sub_07d753` | 0x7d753 | 85 | S R | C: coverage G2 |
| `sub_07df67` | 0x7df67 | 83 | S R | C: coverage G2 |
| `wlc_led_attach` | 0x7d339 | 962 | S R | C: coverage G2 |
| `wlc_led_deinit` | 0x7d235 | 24 | S R | C: coverage G2; wlc-hw 4.2 |
| `wlc_led_detach` | 0x7d2af | 138 | S R | C: coverage G2 |
| `wlc_led_down` | 0x7d15a | 70 | S R | C: coverage G2; wlc-hw 4.1 step 7 |
| `wlc_led_event` | 0x7d095 | 197 | S R | C: coverage G2 |
| `wlc_led_init` | 0x7d24d | 98 | S R | C: coverage G2; bmac-run R4 |
| `wlc_led_stop_activity_timer` | 0x7cdcf | 68 | S R | C: coverage G2 |
| `wlc_led_up` | 0x7cdb2 | 29 | S R | C: coverage G2 |
| `wlc_led_start_activity_timer` | 0x7ce13 | 79 | - | C: coverage G2 |

#### wlc_lq.c

N = no hardware effect: link-quality averages; mac80211 signal averaging.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_lq_attach` | 0x7e487 | 69 | S R | N |
| `wlc_lq_detach` | 0x7e46f | 24 | S R | N |
| `wlc_lq_noise_sample_request` | 0x7e2cb | 87 | - | C: acphy-api-rx N0 (noise sample request; coverage G6) |
| `wlc_lq_rssi_event_update` | 0x7e3a5 | 202 | - | N: event to the glue; cfg80211/mac80211 notifications |
| `wlc_lq_rssi_init` | 0x7e128 | 101 | - | N |
| `wlc_lq_rssi_pktrxh_cal` | 0x7e22e | 140 | - | C: frames 1 |
| `wlc_lq_rssi_reset_ma` | 0x7e18d | 58 | - | N |
| `wlc_lq_rssi_update_ma` | 0x7e1c7 | 103 | - | N |

#### wlc_offloads.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_0834d0` | 0x834d0 | 424 | S R | not used: offload engine / wake-on-WLAN, not used by a mac80211 driver |
| `wlc_ol_attach` | 0x813e8 | 621 | S R | not used: offload engine (ARM CR4) set-up: off (pub+0x4f = 0), only the capability probe runs |
| `wlc_ol_cap` | 0x8138b | 93 | S R | not used: offload engine / wake-on-WLAN, not used by a mac80211 driver |
| `wlc_ol_curpwr_upd` | 0x7ee5a | 275 | S R | not used: offload engine's copy of the power (cca-stf part 2 step 3); offload off |
| `wlc_ol_detach` | 0x812c2 | 201 | S R | not used: offload engine / wake-on-WLAN, not used by a mac80211 driver |
| `wlc_ol_down` | 0x81247 | 123 | S R | not used: offload engine / wake-on-WLAN, not used by a mac80211 driver |
| `wlc_ol_enable_intrs` | 0x7f679 | 69 | S R | not used: offload engine / wake-on-WLAN, not used by a mac80211 driver |
| `wlc_ol_rx_deferral` | 0x7fe12 | 225 | R | not used: offload engine: off (returns at once) |
| `wlc_ol_up` | 0x80e20 | 785 | S R | not used: offload engine: off (pub+0x4f = 0: returns at once; with it on it would halt and load the ARM CR4) |

#### wlc_olpc_engine.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_083767` | 0x83767 | 94 | S R | C: channel-power 4.1 |
| `sub_0837c5` | 0x837c5 | 615 | S R | C: channel-power 4.3 |
| `sub_083a2c` | 0x83a2c | 162 | S R | C: channel-power 4.1 |
| `sub_083bfd` | 0x83bfd | 1572 | S R | C: channel-power 4.4 |
| `sub_08432b` | 0x8432b | 60 | S R | C: channel-power 4.1 |
| `wlc_olpc_eng_attach` | 0x83b18 | 229 | S R | C: channel-power 4.1 |
| `wlc_olpc_eng_detach` | 0x836dc | 139 | S R | C: channel-power 4.1 |
| `wlc_olpc_eng_hdl_chan_update` | 0x84310 | 27 | S R | C: channel-power 4.2 |
| `wlc_olpc_eng_reset` | 0x84367 | 45 | S R | C: channel-power 1.3 |
| `wlc_olpc_eng_tx_cal_pkts` | 0x836bb | 22 | R | C: channel-power 4.4 (asked by the header builder: is this an OLPC calibration frame) |
| `sub_083678` | 0x83678 | 22 | - | C: channel-power 4.4 |
| `wlc_olpc_eng_hdl_txchain_update` | 0x8449c | 52 | - | C: channel-power 4.2 |

#### wlc_phy_cmn.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_phy_shared_attach` | 0xb7199 | 267 | S R | C: acphy-attach "Argument of wlc_phy_shared_attac" |
| `wlc_phy_shared_detach` | 0xb2cb4 | 28 | S R | C: acphy-api A1 |

#### wlc_phy_iovar.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_phy_iovar_attach` | 0xe1815 | 66 | S R | N: configuration interface; nl80211/cfg80211 |
| `wlc_phy_iovar_detach` | 0xe1800 | 21 | S R | N: configuration interface; nl80211/cfg80211 |

#### wlc_phy_shim.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlapi_bmac_btc_mode_get` | 0x152a47 | 14 | S R | C: acphy-api-watchdog |
| `wlapi_bmac_bw_set` | 0x152a63 | 17 | S R | C: acphy-chanspec 3 |
| `wlapi_bmac_corereset` | 0x152abe | 14 | S R | C: bmac-init A18, A14 |
| `wlapi_bmac_phyclk_fgc` | 0x152a35 | 18 | S R | C: bmac-init A18, A10 |
| `wlapi_bmac_ucode_wake_override_phyreg_clear` | 0x1529dd | 19 | S R | C: bmac-init A18, A4 |
| `wlapi_bmac_ucode_wake_override_phyreg_set` | 0x1529f0 | 19 | S R | C: bmac-init A18, A3 |
| `wlapi_del_timer` | 0x152b30 | 15 | S R | C: bmac-init A18 (service of the PHY: timers) |
| `wlapi_enable_mac` | 0x152a90 | 14 | S R | C: acphy-api-watchdog |
| `wlapi_free_timer` | 0x152b4e | 15 | S R | C: bmac-init A18 |
| `wlapi_high_update_phy_mode` | 0x1529ae | 15 | S R | C: bmac-init A18; acphy-desense (notice to the MAC side, no hardware access) |
| `wlapi_high_update_txppr_offset` | 0x15296d | 15 | S R | C: bmac-init A18; cca-stf part 2 |
| `wlapi_init_timer` | 0x152b5d | 15 | S R | C: bmac-init A18 |
| `wlapi_suspend_mac_and_wait` | 0x152ab0 | 14 | S R | C: acphy-api-watchdog |
| `wlapi_update_bt_chanspec` | 0x152956 | 23 | S R | C: acphy-api-watchdog |

#### wlc_prot.c

N = no hardware effect: ERP/HT protection state; mac80211 bss_conf (use_cts_prot, ht_operation_mode) decides RTS/CTS in the transmit header (frames 3.7).

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_166188` | 0x166188 | 37 | S R | N |
| `wlc_prot_attach` | 0x16630f | 158 | S R | N |
| `wlc_prot_detach` | 0x1662d2 | 61 | S R | N |
| `wlc_prot_cfg_init` | 0x1661dd | 13 | - | N |
| `wlc_prot_cfg_upd` | 0x1661ad | 48 | - | N |
| `wlc_prot_init` | 0x1661ea | 35 | - | N |

#### wlc_prot_g.c

N = no hardware effect: ERP/HT protection state; mac80211 bss_conf (use_cts_prot, ht_operation_mode) decides RTS/CTS in the transmit header (frames 3.7).

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_166474` | 0x166474 | 76 | S R | N |
| `sub_166919` | 0x166919 | 130 | S R | N |
| `sub_166b42` | 0x166b42 | 63 | S R | N |
| `sub_166b81` | 0x166b81 | 190 | S R | N |
| `wlc_prot_g_attach` | 0x166c3f | 321 | S R | N |
| `wlc_prot_g_cfg_set` | 0x16645c | 24 | S R | N |
| `wlc_prot_g_detach` | 0x166af2 | 80 | S R | N |
| `wlc_prot_g_cfg_init` | 0x1669b9 | 105 | - | N |
| `wlc_prot_g_cfg_track` | 0x16654d | 221 | - | N |
| `wlc_prot_g_init` | 0x166a22 | 208 | - | N: ERP protection set-up (wlc-hw 5.2 step 10); mac80211 bss_conf.use_cts_prot |
| `wlc_prot_g_mode_upd` | 0x16662a | 529 | - | N |
| `wlc_prot_g_ovlp_upd` | 0x16688c | 141 | - | N |
| `wlc_prot_g_to_upd` | 0x16683b | 81 | - | N |
| `wlc_prot_g_write_erp_ie` | 0x1664c0 | 141 | - | N: information elements; mac80211 builds and parses them |

#### wlc_prot_n.c

N = no hardware effect: ERP/HT protection state; mac80211 bss_conf (use_cts_prot, ht_operation_mode) decides RTS/CTS in the transmit header (frames 3.7).

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_166efd` | 0x166efd | 67 | S R | N |
| `sub_16755b` | 0x16755b | 141 | S R | N |
| `sub_167792` | 0x167792 | 63 | S R | N |
| `sub_1677d1` | 0x1677d1 | 167 | S R | N |
| `wlc_prot_n_attach` | 0x167878 | 354 | S R | N |
| `wlc_prot_n_cap_upd` | 0x167029 | 27 | R | N |
| `wlc_prot_n_cfg_set` | 0x166ed8 | 37 | S R | N |
| `wlc_prot_n_detach` | 0x16774b | 71 | S R | N |
| `wlc_prot_n_mode_reset` | 0x167717 | 52 | S R | C: channel-power 1.3 |
| `sub_166f40` | 0x166f40 | 54 | - | N |
| `wlc_prot_n_build_add_ie` | 0x167065 | 455 | - | N: information elements; mac80211 builds and parses them |
| `wlc_prot_n_cfg_init` | 0x1675e8 | 169 | - | N |
| `wlc_prot_n_cfg_track` | 0x166f76 | 179 | - | N |
| `wlc_prot_n_init` | 0x167691 | 134 | - | N: HT protection set-up (wlc-hw 5.2 step 10); mac80211 bss_conf.ht_operation_mode |
| `wlc_prot_n_mode_upd` | 0x16722c | 523 | - | N |
| `wlc_prot_n_ovlp_upd` | 0x1674b3 | 168 | - | N |
| `wlc_prot_n_to_upd` | 0x167437 | 124 | - | N |

#### wlc_quiet.c

N = no hardware effect: 802.11h quiet periods; mac80211 has none for a station.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_168311` | 0x168311 | 92 | S R | N |
| `sub_16836d` | 0x16836d | 137 | S R | N |
| `wlc_quiet_attach` | 0x1681ac | 224 | S R | N |
| `wlc_quiet_detach` | 0x168101 | 90 | S R | N |
| `sub_167cc7` | 0x167cc7 | 123 | - | N |
| `sub_167df8` | 0x167df8 | 157 | - | N: 802.11h quiet period: FIFOs suspended (coverage G6); mac80211 does not honour Quiet elements |
| `sub_167e95` | 0x167e95 | 190 | - | N |
| `wlc_11h_quiet` | 0x167f53 | 430 | - | N |
| `wlc_quiet_get_quiet_state` | 0x167c3d | 16 | - | N |
| `wlc_quiet_txfifo_suspend_complete` | 0x167d42 | 182 | - | N |
| `wlc_quiet_write_ie` | 0x167be4 | 71 | - | N: information elements; mac80211 builds and parses them |

#### wlc_rate.c

N = no hardware effect: rate-set/ratespec arithmetic; mac80211 rate tables.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_rate_rspec2rate` | 0x1685fd | 143 | R | C: frames 3.3 (TxRate) |
| `wlc_ratespec_nss` | 0x1684af | 60 | R | C: frames 3.5; coverage G1 |
| `wlc_ratespec_nsts` | 0x1684eb | 76 | R | C: frames 3.5; coverage G1 |
| `wlc_bss_membership_filter` | 0x169269 | 119 | - | N |
| `wlc_get_highest_rate` | 0x16885f | 635 | - | N |
| `wlc_get_valid_vht_mcsmap` | 0x1687ed | 114 | - | N |
| `wlc_rate_ht2vhtmcs` | 0x168b06 | 39 | - | N |
| `wlc_rate_legacy_phyctl` | 0x1687c0 | 45 | - | not used: legacy-rate PHY control of the N/HT PHYs (only from wlc_phytxctl1_calc) |
| `wlc_rate_mcs2rate` | 0x168551 | 172 | - | N |
| `wlc_rateset_mcs_clear` | 0x168ada | 22 | - | N: rate sets; mac80211 rate tables (hardware rate table: wlc-hw 1.6) |
| `wlc_rateset_vhtmcs_build` | 0x168b2d | 26 | - | N: rate sets; mac80211 rate tables (hardware rate table: wlc-hw 1.6) |
| `wlc_ratespec_mcs` | 0x168478 | 55 | - | N |
| `wlc_ratespec_ntx` | 0x168537 | 26 | - | N |
| `wlc_recv_compute_rspec` | 0x168eac | 164 | - | C: frames 1 |
| `wlc_vht_get_rspec_from_plcp` | 0x168e2b | 129 | - | C: frames 1 |

#### wlc_rate_sel.c

N = no hardware effect: rate selection; minstrel_ht (chosen rates reach the transmit header: frames 3.3).

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_ratesel_aci_change` | 0x169472 | 35 | S R | N |
| `wlc_ratesel_attach` | 0x16b963 | 225 | S R | N |
| `wlc_ratesel_detach` | 0x16b926 | 61 | S R | N |
| `wlc_ratesel_rcb_sz` | 0x169545 | 11 | S R | N |
| `wlc_ratesel_rssi_attach` | 0x1692e0 | 27 | S R | N |
| `sub_169306` | 0x169306 | 326 | - | N |
| `sub_169495` | 0x169495 | 176 | - | N |
| `sub_1697c3` | 0x1697c3 | 639 | - | N |
| `sub_169a42` | 0x169a42 | 743 | - | N |
| `sub_169d29` | 0x169d29 | 142 | - | N |
| `sub_169db7` | 0x169db7 | 172 | - | N |
| `sub_169f6d` | 0x169f6d | 206 | - | N |
| `sub_16a164` | 0x16a164 | 90 | - | N |
| `sub_16a1be` | 0x16a1be | 89 | - | N |
| `sub_16a217` | 0x16a217 | 1212 | - | N |
| `sub_16a6d3` | 0x16a6d3 | 2352 | - | N |
| `wlc_ratesel_getmcsfbr` | 0x16a03b | 219 | - | N |
| `wlc_ratesel_gettxrate` | 0x169550 | 627 | - | N |
| `wlc_ratesel_init` | 0x16ba44 | 2684 | - | N |
| `wlc_ratesel_minrate` | 0x16944c | 38 | - | N |
| `wlc_ratesel_probe_ready` | 0x16a116 | 78 | - | N |
| `wlc_ratesel_upd_rxstats` | 0x169e63 | 266 | - | N |
| `wlc_ratesel_upd_txs_ampdu` | 0x16b003 | 790 | - | C: ampdu 2.2 |
| `wlc_ratesel_upd_txs_blockack` | 0x16b319 | 762 | - | N |
| `wlc_ratesel_upd_txstatus_normalack` | 0x16b613 | 787 | - | N |

#### wlc_rm.c

N = no hardware effect: radio measurement requests; not offered by mac80211 for a station.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_16d201` | 0x16d201 | 78 | S R | N |
| `wlc_rm_abort` | 0x16d130 | 198 | S R | N: radio measurement abort (coverage G6); not used (early return in the sessions) |
| `wlc_rm_attach` | 0x16d63d | 295 | S R | N |
| `wlc_rm_detach` | 0x16d59a | 163 | S R | N |
| `sub_16c584` | 0x16c584 | 126 | - | N: radio measurement: PHY hold (coverage G6); not used |
| `sub_16c669` | 0x16c669 | 121 | - | N |
| `wlc_rm_cca_complete` | 0x16c602 | 103 | - | N |
| `wlc_rm_pm_pending_complete` | 0x16c556 | 46 | - | N |
| `wlc_rm_stop` | 0x16d1f6 | 11 | - | N |
| `wlc_rm_terminate` | 0x16c75b | 159 | - | N: radio measurement end (coverage G6); not used |

#### wlc_scan.c

N = no hardware effect: scan state machine; mac80211 software scan (hardware side: wlc-hw 6.2).

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_16d781` | 0x16d781 | 84 | R | N |
| `sub_16d7ef` | 0x16d7ef | 8 | S R | N |
| `sub_16d8de` | 0x16d8de | 294 | R | N |
| `sub_16dd2b` | 0x16dd2b | 343 | R | N |
| `sub_16e03b` | 0x16e03b | 317 | R | C: wlc-hw 6.2 |
| `sub_16e178` | 0x16e178 | 74 | S R | N |
| `sub_16e2b7` | 0x16e2b7 | 103 | R | C: wlc-hw 6.2 |
| `sub_16e31e` | 0x16e31e | 1708 | R | C: wlc-hw 6.2 |
| `sub_16f3ed` | 0x16f3ed | 181 | R | N |
| `wlc_scan` | 0x16eb4a | 2211 | R | C: wlc-hw 6 |
| `wlc_scan_abort` | 0x16eaf6 | 59 | S R | C: wlc-hw 1.4 step 37 |
| `wlc_scan_attach` | 0x16f527 | 441 | S R | N |
| `wlc_scan_default_channels` | 0x16de82 | 150 | R | N |
| `wlc_scan_detach` | 0x16f4a2 | 133 | S R | N |
| `wlc_scan_down` | 0x16e1c2 | 99 | S R | C: wlc-hw 4.1 step 4 |
| `wlc_scan_ioctl` | 0x16da04 | 580 | R | N: configuration interface; nl80211/cfg80211 |
| `sub_16e27a` | 0x16e27a | 61 | - | N |
| `wlc_scan_bsscfg` | 0x16d7f7 | 16 | - | N |
| `wlc_scan_fifo_suspend_complete` | 0x16dc93 | 87 | - | N |
| `wlc_scan_in_scan_chanspec_list` | 0x16e225 | 85 | - | N |
| `wlc_scan_inprog` | 0x16d764 | 29 | - | N |
| `wlc_scan_pm_pending_complete` | 0x16dc48 | 75 | - | N |
| `wlc_scan_radar_clear` | 0x16df18 | 291 | - | N |
| `wlc_scan_recv_parse_bcn_prb` | 0x16d7e7 | 8 | - | N |
| `wlc_scan_ssid_match` | 0x16d807 | 215 | - | N |
| `wlc_scan_terminate` | 0x16e9ca | 300 | - | C: wlc-hw 6.2 (scan end: PHY hold 2 released) |

#### wlc_scb.c

N = no hardware effect: station table; mac80211 sta entries.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_16f80b` | 0x16f80b | 152 | S R | N |
| `sub_17015f` | 0x17015f | 63 | S R | N |
| `sub_1701c7` | 0x1701c7 | 171 | S R | N |
| `sub_1702ee` | 0x1702ee | 146 | S R | N |
| `sub_170b8a` | 0x170b8a | 63 | S R | N |
| `wlc_internalscb_alloc` | 0x170c3a | 119 | S R | N |
| `wlc_internalscb_free` | 0x170c2b | 15 | S R | N |
| `wlc_scb_attach` | 0x170cb1 | 163 | S R | N |
| `wlc_scb_bsscfg_scbclear` | 0x170b21 | 105 | S R | N |
| `wlc_scb_cubby_reserve` | 0x16f7a0 | 87 | S R | N |
| `wlc_scb_detach` | 0x17019e | 41 | S R | N |
| `wlc_scb_iterinit` | 0x16f6e0 | 67 | S R | N |
| `wlc_scb_iternext` | 0x16f723 | 125 | S R | N |
| `wlc_scb_set_bsscfg` | 0x1704e2 | 461 | S R | N |
| `wlc_scb_txmod_activate` | 0x16fd40 | 345 | S R | N |
| `wlc_scbclear` | 0x170bc9 | 98 | S R | N |
| `wlc_scbfree` | 0x1706af | 280 | S R | N |
| `wlc_scblist_validaterates` | 0x1707c7 | 102 | S R | N |
| `sub_16f8a3` | 0x16f8a3 | 111 | - | N |
| `sub_16f995` | 0x16f995 | 117 | - | N |
| `sub_16fb10` | 0x16fb10 | 91 | - | N |
| `sub_16fb6b` | 0x16fb6b | 133 | - | N |
| `sub_170949` | 0x170949 | 443 | - | N |
| `wlc_scb_clearstatebit` | 0x16f930 | 12 | - | N |
| `wlc_scb_reinit` | 0x170380 | 354 | - | N |
| `wlc_scb_resetstate` | 0x170272 | 36 | - | N |
| `wlc_scb_rssi` | 0x16fc51 | 48 | - | N |
| `wlc_scb_setstatebit` | 0x16f912 | 30 | - | N |
| `wlc_scb_switch_band` | 0x16fbf0 | 97 | - | N |
| `wlc_scb_txmod_deactivate` | 0x16fe99 | 331 | - | N |
| `wlc_scb_update_band_for_cfg` | 0x17082d | 284 | - | N |
| `wlc_scb_update_oper_mode` | 0x16ffe4 | 45 | - | N |
| `wlc_scbband` | 0x16f7f7 | 20 | - | N |
| `wlc_scbbssfindband` | 0x16faa9 | 103 | - | N |
| `wlc_scbfind` | 0x16fa0a | 18 | - | N |
| `wlc_scbfindband` | 0x16fa1c | 11 | - | N |
| `wlc_scbibssfindband` | 0x16fa67 | 66 | - | N |
| `wlc_scblookup` | 0x170b0f | 18 | - | N |
| `wlc_scblookupband` | 0x170b04 | 11 | - | N |

#### wlc_scb_ratesel.c

N = no hardware effect: rate selection; minstrel_ht (chosen rates reach the transmit header: frames 3.3).

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_1715fc` | 0x1715fc | 67 | S R | N |
| `sub_17163f` | 0x17163f | 96 | S R | N |
| `wlc_scb_ratesel_aci_change` | 0x171229 | 19 | S R | N |
| `wlc_scb_ratesel_attach` | 0x17169f | 201 | S R | N |
| `wlc_scb_ratesel_detach` | 0x1715c9 | 51 | S R | N |
| `wlc_scb_ratesel_get_primary` | 0x1712e5 | 740 | - | N |
| `wlc_scb_ratesel_getmcsfbr` | 0x17120c | 29 | - | N |
| `wlc_scb_ratesel_gettxrate` | 0x1712b9 | 44 | - | N |
| `wlc_scb_ratesel_init` | 0x170d80 | 825 | - | N |
| `wlc_scb_ratesel_init_all` | 0x171107 | 77 | - | N |
| `wlc_scb_ratesel_init_bss` | 0x1710b9 | 78 | - | N |
| `wlc_scb_ratesel_minrate` | 0x171154 | 22 | - | N |
| `wlc_scb_ratesel_probe_ready` | 0x171293 | 38 | - | N |
| `wlc_scb_ratesel_upd_rxstats` | 0x171281 | 18 | - | N |
| `wlc_scb_ratesel_upd_txs_ampdu` | 0x1711be | 78 | - | N: rate selection update from A-MPDU status (ampdu 2.3); minstrel_ht |
| `wlc_scb_ratesel_upd_txs_blockack` | 0x17116a | 84 | - | N |
| `wlc_scb_ratesel_upd_txstatus_normalack` | 0x17123c | 69 | - | N |

#### wlc_security.c

N = no hardware effect: software encryption/decryption; mac80211 software crypto.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_172296` | 0x172296 | 1809 | - | C: ps-keys 1.7 |
| `wlc_decrypt_error` | 0x171c72 | 150 | - | N |
| `wlc_dofrag_tkip` | 0x1717c5 | 1110 | - | N: software TKIP fragmentation (ps-keys 2.4); mac80211 crypto |
| `wlc_wsec_miccheck` | 0x171d08 | 1057 | - | C: ps-keys |
| `wlc_wsec_recvdata` | 0x1729a7 | 261 | - | C: ps-keys |
| `wlc_wsec_rx_tkmic_offset` | 0x1717ba | 11 | - | C: ps-keys |
| `wlc_wsec_rxiv_update` | 0x171768 | 64 | - | C: ps-keys 2.5 |
| `wlc_wsec_sw_encrypt_data` | 0x172129 | 365 | - | N |
| `wlc_wsec_tkip_nextfrag_len` | 0x171c1b | 87 | - | N |
| `wlc_wsec_tx_tkmic_offset` | 0x1717a8 | 18 | - | C: ps-keys |

#### wlc_stf.c

N = no hardware effect: spatial (chain) bookkeeping; mac80211 antenna/chain settings.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_172b5a` | 0x172b5a | 138 | S R | C: coverage G5 |
| `sub_172da9` | 0x172da9 | 104 | R | C: cca-stf "sub_172da9 (.text+0x172da9, name" |
| `sub_1731cf` | 0x1731cf | 297 | S R | C: wlc-hw 2.3 |
| `sub_17341a` | 0x17341a | 74 | R | C: frames 3.5 (core selection helper of the header builder) |
| `sub_173a21` | 0x173a21 | 208 | S R | C: bmac-run R8 (antenna word given to wlc_bmac_txant_set) |
| `sub_173bce` | 0x173bce | 132 | S R | N |
| `sub_174cd5` | 0x174cd5 | 96 | S R | C: cca-stf "sub_174cd5 (.text+0x174cd5, name" |
| `wlc_set_pwrthrottle_config` | 0x172f67 | 616 | S R | C: wlc-hw 1.4 |
| `wlc_stf_attach` | 0x173d74 | 467 | S R | C: cca-stf "The stf state" (attach values); wlc-hw 1.1 |
| `wlc_stf_chanspec_upd` | 0x1732f8 | 31 | S R | C: wlc-hw 2.3 |
| `wlc_stf_d11hdrs_phyctl_txant` | 0x173464 | 47 | R | C: frames 3.3/3.5 (helper of the header builder; the AC header's antenna/core bits are those of PhyTxControlWord_0) |
| `wlc_stf_detach` | 0x173afc | 59 | S R | N |
| `wlc_stf_duty_cycle_set` | 0x172f21 | 70 | S R | C: wlc-hw 1.4 |
| `wlc_stf_get_pwrperrate` | 0x1734be | 1379 | R | C: coverage G1 |
| `wlc_stf_phy_chain_calc` | 0x174d35 | 500 | S R | C: wlc-hw 1.1 step 9 |
| `wlc_stf_phy_txant_upd` | 0x173af1 | 11 | S R | C: wlc-hw 1.5 step 16; bmac-run R8 |
| `wlc_stf_pwrthrottle_upd` | 0x1752b8 | 443 | S R | C: wlc-hw 3 |
| `wlc_stf_set_txbf` | 0x172d01 | 29 | S R | C: channel-power 1.3 |
| `wlc_stf_spatial_expansion_get` | 0x173317 | 137 | R | C: coverage G1 |
| `wlc_stf_tempsense_upd` | 0x176728 | 288 | S R | C: wlc-hw 3 |
| `wlc_stf_txchain_set` | 0x1750f4 | 452 | S R | C: coverage G5 |
| `wlc_stf_txcore_get` | 0x1733a0 | 122 | R | C: frames 3 |
| `sub_172af1` | 0x172af1 | 105 | - | C: cca-stf "sub_172af1 (.text+0x172af1, name" |
| `sub_172d1e` | 0x172d1e | 139 | - | N |
| `sub_173b37` | 0x173b37 | 151 | - | C: coverage G5 |
| `wlc_stf_phytxchain_sel` | 0x173493 | 11 | - | C: frames 3.5 (core selection helper) |
| `wlc_stf_rxchain_set` | 0x173f47 | 381 | - | C: coverage G5 |
| `wlc_stf_saved_state_is_consistent` | 0x172cc2 | 63 | - | C: channel-power 4.2 |
| `wlc_stf_txchain_get` | 0x17349e | 32 | - | C: coverage G1 |
| `wlc_stf_txchain_get_perrate_state` | 0x172c87 | 59 | - | C: channel-power 4.1 |
| `wlc_stf_txchain_restore_perrate_state` | 0x174c6f | 102 | - | C: channel-power 4.4 |
| `wlc_stf_txchain_set_complete` | 0x174f29 | 459 | - | C: coverage G5 |

#### wlc_sup.c

N = no hardware effect: in-driver supplicant and software crypto; wpa_supplicant and mac80211 crypto.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_sup_attach` | 0x176b6c | 263 | S R | N |
| `wlc_sup_detach` | 0x176aea | 130 | S R | N |
| `wlc_wpapsk_free` | 0x176a46 | 164 | S R | N |
| `sub_1779a4` | 0x1779a4 | 966 | - | N |
| `sub_177f42` | 0x177f42 | 54 | - | N |
| `wlc_eapol_pktget` | 0x177893 | 273 | - | N |
| `wlc_find_sup_auth_ies` | 0x176848 | 133 | - | N: information elements; mac80211 builds and parses them |
| `wlc_set_sup` | 0x177444 | 189 | - | N |
| `wlc_sup_clear_pmkid_store` | 0x1768cd | 18 | - | N |
| `wlc_sup_down` | 0x176d0d | 41 | - | N |
| `wlc_sup_eapol` | 0x177f78 | 2798 | - | N |
| `wlc_sup_get_auth_status_extended` | 0x176942 | 133 | - | N |
| `wlc_sup_pmkid_cache_req` | 0x176d36 | 231 | - | N |
| `wlc_sup_set_ea` | 0x1769c7 | 20 | - | N |
| `wlc_sup_set_ssid` | 0x1770f2 | 58 | - | N |
| `wlc_wpa_cobble_pmk` | 0x176fbf | 307 | - | N |
| `wlc_wpa_plumb_gtk` | 0x176e1d | 316 | - | N |
| `wlc_wpa_plumb_tk` | 0x17712c | 207 | - | N |
| `wlc_wpa_send_sup_status` | 0x177842 | 81 | - | N |
| `wlc_wpa_senddeauth` | 0x176f59 | 102 | - | N |
| `wlc_wpapsk_start` | 0x1771fb | 585 | - | N |

#### wlc_tpc.c

N = no hardware effect: TPC report/request frames; mac80211/cfg80211 (power constraint: channel-power 2.2).

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_178a9c` | 0x178a9c | 8 | S R | N |
| `wlc_tpc_attach` | 0x178e39 | 160 | S R | N |
| `wlc_tpc_detach` | 0x178df1 | 72 | S R | N |
| `wlc_tpc_get_local_constraint_qdbm` | 0x178ae3 | 114 | S R | C: channel-power 2.2; wlc-hw 2.2 |
| `wlc_recv_tpc_report` | 0x178db7 | 58 | - | N |
| `wlc_recv_tpc_request` | 0x178cca | 36 | - | N |
| `wlc_send_tpc_report` | 0x178c2e | 156 | - | N |
| `wlc_tpc_rep_build` | 0x178ba8 | 134 | - | N: TPC report frame (coverage G6); mac80211 |
| `wlc_tpc_reset_all` | 0x178aaa | 14 | - | C: channel-power 2.2 |
| `wlc_tpc_set_local_constraint` | 0x178b55 | 83 | - | C: channel-power 2.2 |
| `wlc_tpc_set_local_max` | 0x178ab8 | 10 | - | C: channel-power 2.2 |
| `wlc_tpc_write_constraint_ie` | 0x178ac2 | 33 | - | N: information elements; mac80211 builds and parses them |

#### wlc_tso.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_toe_add_hdr` | 0x178fd7 | 109 | R | C: frames 3 |
| `wlc_tso_hdr_length` | 0x178fc0 | 23 | R | C: frames 3 |

#### wlc_txbf.c

N = no hardware effect: beamforming bookkeeping; mac80211 VHT capabilities.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_179079` | 0x179079 | 47 | S R | N |
| `sub_179ae1` | 0x179ae1 | 545 | S R | C: wlc-hw 1.4 |
| `sub_17a438` | 0x17a438 | 80 | S R | N |
| `sub_17a488` | 0x17a488 | 87 | S R | N |
| `wlc_txbf_attach` | 0x17a4df | 445 | S R | N |
| `wlc_txbf_delete_link` | 0x179d02 | 319 | S R | **GAP**: coverage G8 (beamforming link removed: AMT entry and SHM block) |
| `wlc_txbf_detach` | 0x17a3f4 | 68 | S R | N |
| `wlc_txbf_init` | 0x179a3a | 167 | S R | N |
| `wlc_txfbf_update_amt_idx` | 0x179237 | 326 | S R | C: wlc-hw 1.4 |
| `wlc_check_expected_txbf_system_gain` | 0x1794e3 | 916 | - | N |
| `wlc_txbf_check` | 0x17937d | 358 | - | N |
| `wlc_txbf_clear_init_pending` | 0x179044 | 18 | - | N |
| `wlc_txbf_init_link` | 0x179e41 | 1459 | - | **GAP**: coverage G8 (beamforming link to the AP) |
| `wlc_txbf_is_init_pending` | 0x179068 | 17 | - | N |
| `wlc_txbf_rxchain_upd` | 0x1799ec | 78 | - | C: coverage G5, G8 |
| `wlc_txbf_set_init_pending` | 0x179056 | 18 | - | N |
| `wlc_txbf_txchain_upd` | 0x179877 | 373 | - | C: coverage G5, G8 |
| `wlc_txbf_update_ht_cap` | 0x1790be | 57 | - | N |
| `wlc_txbf_update_vht_cap` | 0x1790a8 | 22 | - | N |

#### wlc_utils.c

N = no hardware effect: utility; kernel library.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_calc_tbtt_offset` | 0x17ab79 | 117 | - | C: wlc-hw 5.3 |
| `wlc_rsn_ucast_lookup` | 0x17acfc | 40 | - | N |
| `wlc_uint64_add` | 0x17ab38 | 16 | - | N |
| `wlc_uint64_lt` | 0x17ab5b | 30 | - | N |
| `wlc_uint64_sub` | 0x17ab48 | 19 | - | N |

#### wlc_vht.c

N = no hardware effect: VHT information elements and peer capabilities; mac80211.

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_vht_init_defaults` | 0x17bb82 | 310 | S R | N |
| `wlc_vht_update_cap` | 0x17ba93 | 239 | S R | N |
| `sub_17ad24` | 0x17ad24 | 165 | - | N |
| `sub_17adc9` | 0x17adc9 | 125 | - | N |
| `sub_17b359` | 0x17b359 | 503 | - | N |
| `wlc_frameaction_vht` | 0x17affa | 52 | - | N |
| `wlc_get_oper_mode` | 0x17b02e | 139 | - | N |
| `wlc_read_ext_cap_ie` | 0x17b879 | 122 | - | N: information elements; mac80211 builds and parses them |
| `wlc_read_oper_mode_notif_ie` | 0x17b7a3 | 82 | - | N: information elements; mac80211 builds and parses them |
| `wlc_read_vht_cap_ie` | 0x17b837 | 66 | - | N: information elements; mac80211 builds and parses them |
| `wlc_read_vht_features_ie` | 0x17b8f3 | 93 | - | N: information elements; mac80211 builds and parses them |
| `wlc_read_vht_op_ie` | 0x17b7f5 | 66 | - | N: information elements; mac80211 builds and parses them |
| `wlc_vht_chanspec` | 0x17b18a | 53 | - | N: peer capabilities from frames; mac80211 sta/bss_conf |
| `wlc_vht_ie_len` | 0x17ae46 | 274 | - | N |
| `wlc_vht_parse_bcn_prb` | 0x17b950 | 323 | - | N |
| `wlc_vht_upd_rate_mcsmap` | 0x17af58 | 162 | - | N |
| `wlc_vht_write_vht_brcm_ie` | 0x17b550 | 290 | - | N: information elements; mac80211 builds and parses them |
| `wlc_vht_write_vht_ies` | 0x17b672 | 26 | - | N: information elements; mac80211 builds and parses them |
| `wlc_write_oper_mode_notif_ie` | 0x17b173 | 23 | - | N: information elements; mac80211 builds and parses them |
| `wlc_write_vht_transmit_power_envelope_ie` | 0x17b27f | 218 | - | N: information elements; mac80211 builds and parses them |

#### wlc_vndr_ie_list.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `wlc_vndr_ie_list_getlen_ext` | 0x17bcb8 | 120 | R | N: vendor information elements; mac80211/wpa_supplicant |
| `wlc_vndr_ie_list_write_ext` | 0x17c48a | 174 | R | N: vendor information elements; mac80211/wpa_supplicant |

#### wlc_wowl.c

| function | .text | size | ran | verdict |
|---|---|---|---|---|
| `sub_17e8a9` | 0x17e8a9 | 105 | S R | not used: offload engine / wake-on-WLAN, not used by a mac80211 driver |
| `wlc_wowl_attach` | 0x17e6eb | 446 | S R | not used: offload engine / wake-on-WLAN, not used by a mac80211 driver |
| `wlc_wowl_cap` | 0x17dd54 | 154 | S R | not used: offload engine / wake-on-WLAN, not used by a mac80211 driver |
| `wlc_wowl_detach` | 0x17e634 | 183 | S R | not used: offload engine / wake-on-WLAN, not used by a mac80211 driver |


## Part 2. The gaps

Sections G1-G8 specify what part 1 found missing; G9 collects the
corrections the newer specifications make to older ones.

### G1. The per-rate power of `PhyTxControlWord_1` (frames.md open question 7)

frames.md 3.5 gives the layout of the word; this section gives the power
value. Three functions of `wlc_stf.c` and one of `wlc.c`:

* `u8 wlc_stf_spatial_expansion_get(wlc, u32 rspec)` (.text+0x173317);
* `s8 wlc_stf_get_pwrperrate(wlc, u32 rspec, u16 expansion)` (.text+0x1734be);
* `u8 wlc_stf_txchain_get(wlc, u32 rspec)` (.text+0x17349e): the number of
  set bits of `wlc_stf_txcore_get(wlc, rspec)`, the core mask of the rate's
  class (frames.md 3.5, cca-stf.md `txcore[]`);
* `u16 wlc_acphy_txctl1_calc(wlc, u32 rspec)` (.text+0x03102a): the word.

The open driver computes this per rate entry when it builds the transmit
header (frames.md 3.3/3.5).

**Rate specification fields used** (as frames.md 1.5 and 3.5): encoding
bits 24..25 (0 legacy, 1 HT, 2 VHT); bandwidth bits 16..18 (1 = 20, 2 = 40,
3 = 80 MHz); STBC bit 20 (0x00100000); beamforming bit 21 (0x00200000);
legacy: the rate in 500 kb/s in bits 0..6; HT: the MCS in bits 0..6; VHT:
MCS bits 0..3, spatial streams bits 4..7. Derived:

* `nss` = VHT: bits 4..7; HT: (MCS >> 3) + 1, but 1 for MCS 32; legacy 1.
* `nsts` = VHT: `nss`, doubled with STBC; HT: (MCS >> 3) + 1, plus 1 more
  with STBC, but 1 for MCS 32; legacy 1.
* CCK = a legacy rate of 2, 4, 11 or 22 (1, 2, 5.5, 11 Mb/s); OFDM = a
  legacy rate whose `rate_info` byte has bit 7 set (12, 18, 24, 36, 48, 72,
  96, 108).

#### G1.1 `wlc_stf_spatial_expansion_get(wlc, rspec)`

1. The rate class `i`: beamforming bit set: `i` = `txstreams` (`stf+0x02`)
   + 1; else CCK: 0; else OFDM: 1; else (HT, VHT, or a legacy value that is
   neither): `nsts` + 1. `i` is capped at 5.
2. Result: `sub_172da9(wlc, i)` (cca-stf.md, `wlc_stf_spatial_expansion_idx`):
   with `b` = the number of bits of `txcore[i].mask` and `s` =
   `txcore[i].nsts`: 0 if `s` > `txstreams`, `b` = 0 or `b` = `s`; 1 for
   (`b`, `s`) = (2, 1); 2 for (3, 1); 3 for (3, 2); 0 otherwise.

So on this card (two transmit streams, cores 0 and 1) a single-stream class
whose core mask is 3 gets 1 (cyclic delay over two chains), one with mask 1
gets 0, and two-stream rates get 0.

#### G1.2 `wlc_stf_get_pwrperrate(wlc, rspec, e)`

`P` = the stf container `stf+0x30` (cca-stf.md part 2: the PHY's per-rate
offsets, halved and limited to the ceiling `stf+0x88`, rewritten after every
transmit power computation; 0x80 = rate disabled). The group getters are
those of ppr.md ("Group get / set": a missing group reads as all 0x80).

1. If `P` is NULL: return 0.
2. Bandwidth selector `w`: rate bandwidth 80 MHz -> 2, 40 -> 1, anything
   else -> 0. For CCK rates instead: 80 -> 4 (20-in-80), 40 -> 3
   (20-in-40), else 0.
3. Chains from the expansion: `c(e)` = 2 for `e` = 1, 3 for `e` = 2, else 1.
4. "Beamforming allowed" = the TXBF capability byte `pub+0xe8` is not 0,
   the rate's beamforming bit is set and `txstreams` >= 2. Then the chains
   are `n` = `wlc_stf_txchain_get(wlc, rspec)`.
5. Legacy rates:
   * CCK: group `ppr_get_dsss(P, w, c(e))` (4 bytes); index 0, 1, 2, 3 for
     1, 2, 5.5, 11 Mb/s.
   * OFDM: beamforming allowed: `ppr_get_ofdm(P, w, mode 3, n)`; else
     `ppr_get_ofdm(P, w, mode, c(e))` with mode 2 (CDD) for `e` = 1 or 2,
     else 0. Index 0..7 for 6, 9, 12, 18, 24, 36, 48, 54 Mb/s.
   * A legacy value that is neither reads no group (the object then
     indexes an uninitialised buffer); it does not occur - an
     implementation returns 0.
6. HT and VHT: group `ppr_get_vht_mcs(P, w, nss', mode, chains)` (10 bytes;
   HT uses the same groups), index = the MCS within its stream (VHT: bits
   0..3; HT: bits 0..2):
   * STBC bit set (checked first): `nss'` 2, mode 1 (STBC), chains 3 if
     `e` = 3, else 2 - whatever the number of streams;
   * else beamforming allowed: `nss'` = `nss`, mode 3, chains `n`;
   * else by `nsts`: 1 -> `nss'` 1, mode 2 if `e` is 1 or 2 (else 0),
     chains `c(e)`; 2 -> `nss'` 2, mode 0, chains 3 if `e` = 3, else 2; 3 ->
     `nss'` 3, mode 0, chains 3; any other `nsts` reads no group (does not
     occur; return 0).
7. Result: the byte of the group at the index, as a signed 8-bit value; if
   that byte is 0x80, the ceiling `stf+0x88` (s8) instead.

Units: half-dB steps of back-off below the target maximum power (the
container's values are 0..`stf+0x88`, at most 31).

#### G1.3 The power field: `wlc_acphy_txctl1_calc(wlc, rspec)`

1. `bw` = rate bits 16..18 (1, 2, 3); `cs` = the current chanspec
   (`wlc+0x518`).
2. `e` = G1.1; `p` = G1.2 with `e`, taken as an unsigned byte.
3. Chip id 0x4360 only (`sih+0x3c`; other chips: `v` = `p` & 0x3f, go to
   step 6): `t` = `wlc_phy_txpower_get_target_max(pi)` as an unsigned byte
   (quarter dBm), `th` = `wlc_phy_tssivisible_thresh(pi)` (signed 32-bit;
   both acphy-api-txpower). If `t` - 2 * `p` (signed 32-bit) < `th`: `o` =
   `wlc_phy_get_olpc_pwroffset(pi, k)` (s8; the PHY's open-loop offsets
   `pi_ac+0x8de/0x8df/0x8e0`) with `k` = 0x1000 for `bw` 1, 0x1800 for 2,
   0x2000 for 3; any other `bw`: `o` = 0. Otherwise `o` = 0.
4. `o6` = the low 6 bits of `o`, sign-extended. If `o6` = 0: `v` = `p` &
   0x3f.
5. Else: `s` = `o6` + (the low 6 bits of `p`, sign-extended), in 8-bit
   wrapping arithmetic read as signed; clamped to -32..31; `v` = `s` & 0x3f.
6. Word = `v` << 3 | ((`cs` >> 8) & 7) >> ((`bw` - 1) & 31).

So the correction by the OLPC offset is applied only when the rate's target
(the maximum less twice the back-off, in quarter dB) falls below the PHY's
TSSI visibility threshold, and only then is the sum clamped.

Values in the model (object, after up): ceiling `stf+0x88` 29 on 36/80, 21
on 36/40, 25 on 36/20; target maximum 58, 42, 50 (quarter dBm); threshold
20; OLPC offsets 1, 1, 1 on 36/80 and 0 elsewhere. A driver that leaves
the field 0 sends every rate at the target maximum (no per-rate back-off).

**Verified** (`re-out/analysis/coverage/verify_pwr.py`, object alone:
attach, up, then channels 36/80, 36/40 and 36/20): the stf container
filled with random values 0..31 and 8 % disabled entries; G1.2 and G1.1
written as a model from this text and compared with the object's
functions for every legacy rate, HT MCS 0..15 (with STBC and beamforming
bits), VHT 1..3 streams MCS 0..9 (with STBC and beamforming), the three
bandwidths, expansions 0..3 and the TXBF capability off and on; G1.3
compared with `wlc_acphy_txctl1_calc` with the OLPC offsets (0, 0, 0), (5,
-3, 40), (-20, 31, -32) and (63, 1, -1): 16,884 comparisons, 0
differences.

### G2. LEDs: the policy of `wlc_led.c`

bmac-run R4 describes the MAC layer's LED engine (polarity, blink timer,
GPIO writes); this section is the upper layer's policy: which pin shows
what, and which events update it. Functions: `wlc_led_attach`
(.text+0x07d339), `wlc_led_detach`, `wlc_led_init` (0x07d24d), `wlc_led_up`
(0x07cdb2), `wlc_led_down` (0x07d15a), `wlc_led_deinit` (0x07d235),
`wlc_led_event` (0x07d095), `wlc_led_start_activity_timer` (0x07ce13),
`wlc_led_stop_activity_timer` (0x07cdcf), the module's watchdog
`sub_07d1a0`, the activity timer `sub_07d6fb`, the behaviour functions
(`sub_07ce62`, `sub_07cf8d`, `sub_07d011`, `sub_07df67`, `sub_07ceb5`, ...)
and the blink helpers `sub_07daf9`, `sub_07d753` (names assigned below).

**State** (`led` = `wlc+0x170`, 0x250 bytes):

| `led`+ | size | meaning |
|---|---|---|
| 0x00 | 8 | `wlc` |
| 0x08 | 8 | `pub` |
| 0x10 + 16 i | 16 | entry i = 0..31: +0 pin (u32), +4 behaviour (u32, 0..24), +8 active high (u8), +0x0a on time (u16, ms; 0 = not blinking), +0x0c off time (u16), +0x0e blink-synchronised (u8, 1 after attach) |
| 0x214 | 4 | GPIO mask of the LEDs |
| 0x218 + 2 k | 2 | on time of blink set k = 0..3: 500, 80, 40, 200 ms |
| 0x220 + 2 k | 2 | off time of blink set k: 500, 20, 40, 100 ms |
| 0x228 | 1 | activity seen since the last activity tick |
| 0x229 | 1 | some entry has behaviour 2 (activity) |
| 0x230 | 8 | the activity timer (`wl_init_timer`, `sub_07d6fb`) |
| 0x238 | 1 | activity timer armed |
| 0x239 | 1 | blink engine started (`wlc_bmac_led_blink_event(hw, 1)` done) |
| 0x23a | 1 | LEDs "up" |
| 0x244 | 4 | countdown of behaviour 20 (association with security) |
| 0x248 | 4 | mask of pins blinking in synchrony |

**Which pin shows what** (`wlc_led_attach`):

1. Defaults: entry 0 = pin 0, behaviour 2 (activity); entry 1 = pin 1,
   behaviour 5 (2.4 GHz radio); entry 2 = pin 2, behaviour 4 (5 GHz radio);
   entries 3..31 = pin i, behaviour 0 (none); all active high; every
   entry's blink-synchronised byte = 1. (Board vendor 0x0e11: entry 0 gets
   behaviour 3. Chips 0xa99d/0xa8d8 at revision 0 have another table: not
   this card.)
2. For i = 0..31, the variable `ledbh<i>`, else `wl0gpio<i>`: if present
   and (v & 0x7f) <= 0x18: entry i = pin i, behaviour v & 0x7f, active high
   unless bit 7 of v is set. On this card the SROM words of `ledbh0..3`
   (SROM words 77 and 78) are 0xffff in the capture of the real card
   (`re-out/captures/run1-broadcom.srom`), so the variables do not exist
   (srom-otp: "NOFFS") and **the defaults apply: GPIO 0 activity, GPIO 1
   2.4 GHz radio, GPIO 2 5 GHz radio, active high** (verified: the object
   after attach in the model, which has no LED variables either).
3. Blink sets as in the table; `led+0x229` = 1 (entry 0 has behaviour 2).
4. Registered as module "led" with a watchdog (`sub_07d1a0`) and an iovar
   handler (blink times, per-pin behaviour and synchronisation; not needed).

`wlc_led_init` (from `wlc_up` before the MAC layer's `wlc_bmac_hw_up`):
mask = the OR of `1 << pin` of every entry whose behaviour is neither 0 nor
11 (7 here); `wlc_bmac_led_hw_mask_init(hw, mask)` (R4). `wlc_led_deinit`
(detach): `wlc_bmac_led_hw_deinit(hw, mask)`.

**"On" state used by the behaviours**: `up` = `led+0x23a` or
`wlc_down_for_mpc(wlc)` (the driver is down only because MPC turned the
radio off: `pub+0x34` = 0, `wlc+0x251` = 0 and `pub+0x84` = 4 exactly).
`wlc_led_up` sets `led+0x23a` = 1 (end of `wlc_up`); `wlc_led_down` (from
`wlc_down`) stops the activity timer, clears it and runs `wlc_led_event`.

**Behaviours used on this card** (each gets the entry; "set(x)" =
`wlc_bmac_led(hw, 1 << pin, x ? 1 << pin : 0, activehi)`, R4):

* 5 (2.4 GHz radio, `sub_07cf8d`): x = (the driver has two bands, `pub+0x44`
  != 1, and is not band-locked, `wlc+0x24e` != 1) or the current band is
  2.4 GHz (`wlc+0x40`->bandtype = 2); x = 0 unless `up`. set(x).
* 4 (5 GHz radio, `sub_07d011`): the same with bandtype 1. On this card
  (two bands, no lock) both radio LEDs are on whenever `up`.
* 2 (activity, `sub_07df67`): if not `led+0x23a`: `led+0x228` = 0. If
  `led+0x228` = 0: stop blinking (`sub_07d753`: if the entry's on or off
  time is not 0, both = 0, its pin removed from `led+0x248`,
  `wlc_bmac_led_blink(hw, pin, 0, 0)`) and set(0). Else blink with set 1
  (`sub_07daf9`): if the entry is synchronised, its pin joins `led+0x248`;
  if the entry's times differ from 80/20: store them,
  `wlc_bmac_led_blink(hw, pin, 80, 20)` and, if `led+0x248` != 0,
  `wlc_bmac_blink_sync(hw, led+0x248)`.

Other behaviours (only through `ledbh` variables): 1 and 3 on while `up`;
21 always off; 0 and 11 nothing; 6..10, 12..20, 22..24 association and
blink patterns (sets 0..3). Not described further.

**`wlc_led_event(led)`**: run the behaviour of every entry whose behaviour
is not 0, in entry order. Then, if `up` and some entry has a non-zero on or
off time: if `led+0x239` = 0: `wlc_bmac_led_blink_event(hw, 1)` (start the
blink timer, R4), `led+0x239` = 1. Otherwise (not up, or nothing blinks):
if `led+0x239`: `wlc_bmac_led_blink_event(hw, 0)` (all blinking LEDs off),
`led+0x239` = 0.

**Events** (callers of `wlc_led_event`): the end of `wlc_up`; the
watchdog, every second (`sub_07d1a0`: `led+0x244` counted down, then the
event); `wlc_led_down`; the radio switched off while down (`wlc_radio_disable`,
G4); a join completed (`sub_05cd9c`) or started (`sub_05f235`); a
disassociation; a band lock; the activity timer.

**Activity**: the transmit path (`wlc_prep_sdu`, `wlc_prep_sdu_fast`) and
the receive delivery (`wlc_recvdata_sendup`) call
`wlc_led_start_activity_timer(led)`: if `led+0x229`: `led+0x228` = 1, and
if the timer is not armed, `wl_add_timer(timer, 200 ms, periodic)` and armed
= 1. The timer (`sub_07d6fb`), only while the driver is up: if the card is
gone, the glue's `wl_down`; else `wlc_led_event(led)`, then if `led+0x228`
= 0 `wlc_led_stop_activity_timer` (delete, armed = 0, `led+0x228` = 0), then
`led+0x228` = 0. So the activity LED blinks 80 ms on / 20 ms off while
frames pass, and goes off at the first 200 ms tick without frames.

**For the open driver**: GPIO 0 = mac80211's throughput/activity trigger
blinking 80/20 ms; GPIOs 1 and 2 = "radio on" (on while the interface is up
or MPC holds the radio off, off when down); the pins and polarity from the
`ledbh0..3` variables when present.

Verified (`re-out/analysis/coverage/verify_misc.py`, object alone): the
entry table after attach (pins 0, 1, 2 with behaviours 2, 5, 4, active high,
synchronised), the blink sets, mask 7; at up `wlc_bmac_led` (1, 0), (2, 2),
(4, 4); after `wlc_led_start_activity_timer` and 250 ms:
`wlc_bmac_led_blink(hw, 0, 80, 20)`, blink engine started, pin 0 on; 450 ms
idle: `wlc_bmac_led_blink(hw, 0, 0, 0)`, pin 0 off, blink engine stopped;
at down all three off. The other behaviours: read.

### G3. Bluetooth coexistence: the upper layer's policy

bmac-run R3 gives the MAC layer's coexistence functions (mode, wire, host
flags, ECI/SECI notices) and wlc-hw 3 step 7 the watchdog's reads. Left:
`wlc_btc_mode_set` (.text+0x026b9a), `wlc_btc_wire_get` (0x025e4b),
`wlc_enable_btc_ps_protection` (0x02b0e6) and the still-Broadcom helpers
`wlc_bmac_btc_flags_get` (0x06019a), `wlc_bmac_btc_rssi_threshold_get`
(0x061920), `wlc_bmac_btc_update_predictor` (0x062004),
`wlc_bmac_btc_stuck_war50943` (0x062a99), and what the watchdog does with
the coexistence state.

The upper layer keeps a block `B` = `wlc+0x690`: +0x02 Bluetooth active
(set by the watchdog's five-tick test, wlc-hw 3 step 7), +0x03, +0x06,
+0x07 RSSI thresholds (below), +0x08 the PM mode to use while Bluetooth is
active (3 = do not change), +0x0c the resolved mode, +0x10 the wire type.
`wlc_init` copies the MAC layer's mode and wire into +0x0c and +0x10
(`wlc_bmac_btc_mode_get`, `wlc_bmac_btc_wire_get`) after `wlc_bmac_init`.

**Whether any of this runs on this card**: the mode resolves to 0 (off)
unless the coexistence hardware is present (R3: BTC_HW, needs MAC
capabilities bit 29) or `boardflags2` bit 7; card run 6 found bit 29 set on
the real card (hybrid/README "Rules for adapters"), so the auto mode there
is 5 when `aa2g` >= 3, else 1 (R3 step 2), and the wire type 3. The model
has neither: in the sessions mode = 0, wire = 2 and nothing below runs.

* `wlc_btc_mode_set(wlc, mode)`: r = `wlc_bmac_btc_mode_set(hw, mode)`
  (R3); `B+0x0c` = `wlc_bmac_btc_mode_get(hw)`; return r. (Iovar
  `btc_mode`; the watchdog uses it on chip 0xa886 only.)
* `wlc_btc_wire_get(wlc)` = `B+0x10`. `wlc_bmac_btc_flags_get(hw)` = the
  MAC layer's `btc.flags` (R3, u16).
* `wlc_bmac_btc_rssi_threshold_get(hw, &a, &b, &c)`: with p =
  `btc.bt_shm_base` (R3): if p != 0: a = `SHM(p + 0x92)`, b = `SHM(p +
  0xc8)`, c = `SHM(p + 0xca)`, each truncated to 8 bits; p = 0: unchanged.

**`wlc_enable_btc_ps_protection(wlc, cfg, on)`** (the third argument is not
used). Callers: `wlc_sta_assoc_upd` (wlc-hw 5.5; after it sets `wlc+0x301`
= the number of connected configurations, `wlc_stas_connected`),
`wlc_bss_clear_bssid` (likewise), the watchdog (2.4 GHz band, mode != 0),
`wlc_key_delete`, the iovars.

1. Nothing if `B+0x10` <= 2 (wire) or `B+0x0c` = 0 (mode).
2. f = `wlc_bmac_btc_flags_get(hw)`; `pc` = the primary configuration
   (`wlc+0x2f8`).
3. Nothing if `pub+0x84` != 0 (radio disabled, G4), the driver is down
   (`pub+0x34` = 0), or MPC is on (`wlc+0x253`) and `wlc_ismpc(wlc)`.
4. `wlc_bmac_btc_rssi_threshold_get(hw, B+3, B+6, B+7)`.
5. c = 8 if (`B+3` != 0 and -r > `B+3`, with r = the primary
   configuration's RSSI, s32 at (`pc+0x330`)->+0x14, compared signed with
   the unsigned byte), or f bit 2 (4) is set; else 0. a = 0x0200 if f bit
   4 (0x10) is set, else 0.
6. "Protected link": (`pc+0x318`)->+0x34 (the BSS is infrastructure) and
   `pc+0x0c` (associated) and (`pc+0x22` = 0, or the BSSID `pc+0xf0..0xf5`
   not all zero). If it holds and `wlc+0x301` = 1: value = a | c, except 0
   when the configuration uses WPA (`pc+0x9a` != 0 and `pc+0x90` & 7 != 0)
   and its keys are not in place yet (`pc+0x9d` = 0). Otherwise: value = c
   if `wlc+0x301` != 0, else 0.
7. `wlc_bmac_mhf(hw, 2, 0x0208, value, 2)` (bmac-init A9: host flags word
   2, bits 0x0008 and 0x0200, the 2.4 GHz band's copy).

**The watchdog with mode != 0, on the 2.4 GHz band** (read; besides wlc-hw
3 step 7): `wlc_enable_btc_ps_protection` every tick; then, unless `B+8` =
3: Bluetooth not active and `B+8` differs from the primary configuration's
PM mode (`(pc+0x338)`->+0): set that PM mode (`sub_03fc00`, ps-keys 1.7);
Bluetooth active, `B+8` = 0 and the PM mode is not 2: PM mode 2. Then, MAC
revision > 14: A-MPDU policy - modes 0 and 4: aggregation allowed
(`sub_03d429`); other modes, while associated, on the BSS channel, not
scanning, with A-MPDU on: if the Bluetooth period (`B+0`, u16) is 1..9999 and
the mode is 1 or 2, aggregation off for transmit and receive
(`sub_03d3f7`), otherwise allowed. So for mode 5 (this card with BTC_HW)
aggregation stays allowed. The receive-chain and chain-mask changes of the
same block are for chips 0x4331, 0x4352, 0xa8d9, 0xa8dc only.

**`wlc_bmac_btc_update_predictor(hw)`** (from `wlc_dpc` on interrupt bit
0x00800000, frames.md 2.2): p = `btc.bt_shm_base`; nothing if p = 0. P =
`SHM(p + 8)` (the Bluetooth period); nothing if 0. now = `D11(0x180)` (32
bit, the TSF's low word). T = `SHM(p + 0x18)` | `SHM(p + 0x1a)` << 16, read
as: low, high, low again, repeated until both low reads agree. `SHM(p +
0x1c)` = the low 16 bits of ((now - T) / P + 1) * P + T (32-bit unsigned
arithmetic, division truncating): the next Bluetooth slot after now.

**`wlc_bmac_btc_stuck_war50943`**: only for MAC revisions < 15 (interrupt
bit 0x00400000, frames.md 2.2): not this card.

**For the open driver**: with the card's coexistence hardware, keep the
MAC layer's mode (R3) and do steps 1-7 of `wlc_enable_btc_ps_protection` at
association changes and every second on 2.4 GHz; switch to power save (PM)
while Bluetooth is active only if the driver supports it (mac80211's
dynamic power save is the nearest equivalent); answer interrupt bit
0x00800000 with the predictor update.

Verified (`verify_misc.py`, object alone, state forced: wire 3, mode 5,
`bt_shm_base` 0x1454 as the model has it): six cases of
`wlc_enable_btc_ps_protection` - flags 4 not associated -> value 8; flags
0x14 associated, open -> 0x208; the same with WPA, keys not in place -> 0;
threshold 70, RSSI -80, two connected configurations -> 8; threshold 70,
RSSI -60 -> 0; flags 0x10, nothing connected -> 0; each as one
`wlc_bmac_mhf(hw, 2, 0x208, value, 2)`. The rest read.

### G4. Radio switch, rfkill and MPC

**State**: `pub+0x84`, the radio-disable bits: bit 0 (1) software
(`WLC_SET_RADIO`: the glue's rfkill and `set_tx_power`), bit 1 (2) the
hardware switch, bit 2 (4) MPC, bit 3 (8) no valid channel
(channel-power 3.2). `pub+0x88` = the previous value. `wlc+0x250` radio
monitor running, `wlc+0x280` its timer, `wlc+0x253` MPC enabled (1 after
attach), `wlc+0x25c`/`wlc+0x25a` MPC hold-off counters, `wlc+0x788` time
the radio monitor started.

* **Hardware switch poll** (`sub_030b4b`, wlc-hw 3 step 6; unless
  `pub+0x70` bit 2 or `pub+0x35`): `wlc_bmac_radio_read_hwdisabled(hw)`
  (bmac-init C13) sets or clears bit 1.
* **`wlc_radio_upd(wlc)`**: if `pub+0x84` = 0: if the driver is down, the
  card present (C14) and `wlc+0x251` = 0: the glue's `wl_up`. Else
  `wlc_radio_disable(wlc)`. Then `pub+0x88` = `pub+0x84` if they differ, and
  the event `WLC_E_RADIO` (40) to the glue (which updates its rfkill state).
* **`wlc_radio_disable(wlc)`**: driver down: if a primary configuration
  exists, `wlc_bmac_pllreq(hw, 1, 4)`, `wlc_led_event` (G2: LEDs off),
  `wlc_bmac_pllreq(hw, 0, 4)`. Driver up and no down in progress
  (`wlc+0x252`): start the radio monitor (`sub_026c55`: time stamp
  `wlc+0x788`; unless it runs already or `pub+0x70` bit 2:
  `wlc+0x250` = 1, `wlc_bmac_pllreq(hw, 1, 2)` - keep the backplane clock
  requested so the switch can be read while down -, the timer `wlc+0x280`
  every 2000 ms, periodic), then the glue's `wl_down`.
* **The monitor timer** (`sub_037024`): card gone: `wl_down`; else the
  switch poll and `wlc_radio_upd` (so the driver comes back up by itself
  when every disable bit is clear).
* **`wlc_radio_monitor_stop(wlc)`** (from `wlc_up`, `wlc_detach`,
  `wlc_radio_mpc_upd`): if running: `wlc+0x250` = 0,
  `wlc_bmac_pllreq(hw, 0, 2)`, delete the timer.
* **`MI_RFDISABLE`** (frames.md 2.2, `wlc_rfd_intr`): reads `D11(0x158)`
  (32 bit), counts; if bit 16 is set (the switch is off) and the primary
  configuration is an infrastructure station that is up (`cfg+0x22`,
  `cfg+0x09`): `wlc_bsscfg_disable` (the association is dropped; the radio
  itself goes off at the next poll).
* **MPC** (`wlc_radio_mpc_upd`, from the watchdog and from scan, join and
  ioctl paths): MPC off: clear bit 2; if bits remain clear, `wlc_radio_upd`
  and, if all clear, stop the monitor. MPC on: if `wlc_ismpc(wlc)` (the
  hold-off `wlc+0x25c` is 0 and `sub_025002`: not associated (`pub+0x59`),
  no scan in progress, no monitor mode (`wlc+0x288`), `wlc+0x254`,
  `wlc+0x258`, `wlc+0x748`, `pub+0x4d` all 0, and the primary
  configuration idle (no association state machine running, `cfg+0xd4` and
  `cfg+0xd8` 0)): set bit 2 (the watchdog's `wlc_radio_upd` then takes the
  driver down); else, if bit 2 was set: clear it, `wlc_radio_upd` (up
  again), `wlc+0x25a` = 10 if the radio was off for less than 3 s, and
  account the time; then, unless `sub_025002` holds, `wlc+0x25c` =
  `wlc+0x25a`.

**For the open driver** (mac80211): the hardware switch is reported with
`wiphy_rfkill_set_hw_state()`: poll C13 every second while up (watchdog)
and from `rfkill_poll` while down (with the backplane clock requested as
the monitor does: `wlc_bmac_pllreq(hw, 1, 2)`), and on `MI_RFDISABLE` read
`D11(0x158)` bit 16 at once. The software disable is mac80211's
stop/start. MPC is replaced by mac80211's idle state
(`IEEE80211_CONF_IDLE`): the driver may power the radio down while idle;
Broadcom's conditions above are the equivalent of "not associated and not
scanning".

Verified (`verify_misc.py`, object alone): `WLC_SET_RADIO` off -> `pub+0x84`
= 1, driver down, `wlc_bmac_pllreq(hw, 1, 2)`, LEDs off, event 40; every 2 s
while off `wlc_bmac_pllreq(1, 4)`, LEDs off, `wlc_bmac_pllreq(0, 4)`; on ->
`pub+0x84` = 0, up, `wlc_bmac_pllreq(hw, 0, 2)`, LEDs 1/2 on, event 40. The
`MI_RFDISABLE` and MPC paths: read.

### G5. Changes of the transmit and receive chains

The PHY's `wlc_phy_stf_chain_set(pi, txchain, rxchain)` (acphy-api) is the
hardware step; cca-stf.md part 2 gives the spatial policy and the core
table. Left: who changes the chain masks and what is recomputed. All read.

**`wlc_stf_txchain_set(wlc, mask, force, id)`** (.text+0x1750f4): every
requester `id` (0..4: 3 = `wlc_update_txppr_offset`, 4 = the watchdog's
temperature and coexistence code, others iovars) has a byte
`stf+0x5a+id`; the effective mask is `hw_txchain` (`stf+0`) AND the five
bytes.

1. Store `mask` in the requester's byte (remember the old one), compute
   the effective mask `n`.
2. If `n` = `txchain` (`stf+1`): return 0 (the byte keeps `mask`).
3. If `n` has a bit outside `hw_txchain` or is 0: restore the requester's
   old byte, return -29 (refused).
4. If the change goes from more than one chain to exactly one: for each
   band whose fixed rate overrides (`band+0x50`, `band+0x54`) use more than
   one transmit chain: unless `force`, restore the requester's byte; clear
   those overrides; if the current band's STBC transmit byte (`band+0x59`)
   is 1, clear it in both bands.
5. `stf+0x58` (pending mask) = `n`. If `n` contains all of `txchain`, or no
   frame is pending in the four access-category FIFOs (the four signed
   16-bit counters at +0x38..+0x3e of the structure `wlc+0x38` points to,
   sum 0): clear `wlc+0x304` bit 5 and complete now
   (below). Otherwise set `wlc+0x304` bit 5: `wlc_txfifo_complete`
   completes when the FIFOs have drained (frames 4.3). Return 0.

**`wlc_stf_txchain_set_complete(wlc)`** (.text+0x174f29):

1. `txchain` = `stf+0x58`; `txstreams` (`stf+2`) = its number of bits;
   `stf+0x58` = 0.
2. If `txchain` is not 0: a per-class override `txcore_override[1]`
   (`stf+0x23`) or `[0]` (`stf+0x22`) that uses a chain outside `txchain`
   is cleared (and the callback `stf+0x80` called if set).
3. STBC transmit (`sub_173b37`, with the current band's byte `band+0x59`):
   allowed only with more than one stream and the PHY capability bit 1
   (`wlc_phy_cap_get`); it sets or clears bit 7 of `wlc+0x381` and of
   `wlc+0x71b` (the HT/VHT capability copies) and writes `band+0x59` of both
   bands.
4. `wlc_stf_ss_update` for both bands (cca-stf.md); `stf+8` (`txant`) = 3
   on the AC-PHY; the antenna word (`sub_173a21`, bmac-run R8); the core
   table `txcore[]` rebuilt from the table of cca-stf.md "Tables" with the
   column `stf+0x15` = 2 if `txchain` = 6 else 1 (or `sub_172af1` per class
   when `core1_first`); the spatial policy (`sub_174cd5`, cca-stf.md).
5. VHT capabilities (`wlc_vht_update_cap`, with VHT on), the rate
   selection re-initialised, the header cache generation `wlc+0x2a8` += 1.
6. **`wlc_phy_stf_chain_set(pi, txchain, rxchain)`**.
7. TXBF capable (`pub+0xe8`) and `txstreams` > 1: `wlc_txbf_txchain_upd`
   (G8). If `txchain` changed and the OLPC engine is enabled:
   `wlc_olpc_eng_hdl_txchain_update` (channel-power 4).

**`wlc_stf_rxchain_set(wlc, mask)`** (.text+0x173f47): nothing if `mask`
= `rxchain` (`stf+5`), or `mask` has a bit outside `hw_rxchain` (`stf+3`) or
is 0. Else `rxchain` = `mask`, `rxstreams` (`stf+6`) = its bits; VHT
capabilities updated; TXBF capable: `wlc_txbf_rxchain_upd` (with the
beamforming block on and the clock on: `SHM(2 * base + 0xe4)` = streams - 1,
`base` = the block's word address, wlc-hw 1.4). If the number of streams
did not change: `wlc_phy_stf_chain_set` when the mask changed. If it
changed to or from one stream: the SM power-save mode (0 = static for one
stream, 3 = off) is stored in the primary configuration (`+0x568`); not
associated: `wlc_phy_stf_chain_set` and the HT capability's SM power-save
field; associated: when leaving one-stream mode the PHY first, then the
SM power-save action frame to the AP (`wlc_mimops_action_ht_send`), whose
completion (`sub_045018`) sets the PHY's chains (`wlc_phy_stf_chain_set`)
for the reduction.

**When it happens on this card**: `wlc_update_txppr_offset` asks for
`hw_txchain` (no change, cca-stf.md step 8, with `min_txpower` 0);
`wlc_stf_tempsense_upd` (wlc-hw 3 step 16) reduces the transmit chains to
the PHY's active-chain report and restores them; the iovars `txchain` and
`rxchain`. For mac80211, SMPS changes (`IEEE80211_CONF_CHANGE_SMPS`) map to
`wlc_stf_rxchain_set` without the action frame (mac80211 sends it), and the
temperature reduction to `wlc_stf_txchain_set` with id 4.

### G6. Spectrum management, measurement and interference modules

Checked for hardware writes a client on this card needs (read; the
sessions ran their attach, watchdog and down functions):

* **`wlc_interfere.c`** (module "interference"): channel-interference
  assessment. Off by default: the attach leaves the enable word `+0x24`,
  the mode byte `+0x0c` and `+0x14` at 0; its watchdog (`sub_079c78`)
  returns before 60 ticks and then at once while they are 0. Enabled (iovar
  only) it reads the channel statistics (`cca_query_stats`, cca-stf.md) of
  every valid channel and may request scans; it writes no hardware of its
  own (`WLC_SET` 0xd4 only for chip 0x4312). **Not needed.**
* **`wlc_lq.c`**: RSSI moving averages and RSSI events (no hardware);
  `wlc_lq_rssi_pktrxh_cal` is frames.md 1.6; `wlc_lq_noise_sample_request`
  asks the PHY for a noise sample (acphy-api-rx N0; the PHY's watchdog
  does the same). mac80211 averages the signal itself.
* **`wlc_rm.c`** (radio measurement requested by an AP or the `measure`
  iovar): around a measurement the PHY hold 4 (`sub_16c584`: set while the
  association state machine is busy), at the end host flag 1 bit 0x0400
  cleared (`wlc_mhf(wlc, 1, 0x400, 0, 3)`), `wlc_skip_adjtsf` (wlc-hw 6.2),
  the PM state restored and the wake bit updated; CCA results come through
  `MI_CCA` (frames.md 2.2). mac80211 offers no radio measurement for a
  station: **not needed.**
* **`wlc_tpc.c`**: the local power constraint is channel-power.md 2.2; the
  rest is TPC request/report frames (the report: transmit power = (target
  maximum + the band's offset `band+0x108`) / 4 dBm, link margin from the
  request's rate). **No hardware.**
* **`wlc_11h.c`**: spectrum-management state, measurement-request frames,
  the supported-channels element; the power constraint goes to
  channel-power.md 2.2. **No hardware.**
* **`wlc_csa.c`** with `wlc_do_chanswitch` (wlc.c): on a channel switch
  announcement the transmit is muted (`wlc_mute`, bmac-init E4) when the AP
  asks for silence, and at the switch time the MAC is suspended,
  `wlc_set_chanspec` (wlc-hw 2) runs, the new channel is marked quiet if it
  is a radar channel, the MAC is enabled. mac80211 handles CSA itself
  (stops the queues, then changes the channel through the driver: wlc-hw 2).
* **`wlc_quiet.c`** (the AP's Quiet element): at the start of a quiet
  period transmit FIFOs 1 and 3 are suspended (`sub_167df8`, bmac-run R2)
  and, once suspended, the own address is removed from the address match
  table (`wlc_clear_mac`: `AMT[63]` cleared); at the end resumed and
  restored. mac80211 does not honour Quiet elements for a station: **not
  needed.**
* **`wlc_cac.c`**: admission control (TSPEC frames, used-time accounting
  per frame). **No hardware.**
* **Quiet (radar/passive) channels**: channel-power.md 3.2 mutes the
  transmit on a quiet channel until a beacon is heard there (`sub_02eac4`
  in the beacon path and `wlc_join_BSS` clear the channel's quiet bit and
  unmute). With mac80211 the channel's NO_IR flag does this job; the open
  driver can pass `mute` = 0 to the channel set and let mac80211 decide
  when to transmit.

### G7. The start of a join

wlc-hw 5.1 starts at the association response; before it the station
prepares the hardware in `wlc_join_BSS` (.text+0x05ae4f), and the
association state machine holds the PHY. Read.

**`wlc_join_BSS(cfg, bss)`** (the target BSS chosen, before
authentication):

1. Target chanspec `c` = the BSS's (`bss+0x32`), checked against the band;
   `wlc+0x304` bit 2 set.
2. If the PHY's chanspec is `c` - or has the same control channel and a
   wider bandwidth than `c` -: if `c` is a quiet channel: clear its quiet
   bit and `wlc_mute(wlc, 0, 0)` (unmute, bmac-init E4). Otherwise: clear
   the quiet bit of `c`, `wlc_bmac_suspend_mac_and_wait`, `wlc_set_chanspec(
   wlc, c)` (wlc-hw 2), `wlc_bmac_enable_mac`.
3. The BSS's rate set filtered to the hardware's (rate.md) and
   `wlc_rate_lookup_init(wlc, rates)` (wlc-hw 1.6).
4. AC-PHY: **full calibration** `wlc_full_phy_cal(wlc, cfg, 4)` unless the
   configuration is an infrastructure one already associated with a
   reassociation flag (`cfg+0x22`, the join type 3 and bit 2 of the join
   flags). `wlc_full_phy_cal`: unless another configuration (the primary
   one, if it is not `cfg`) is associated on the same chanspec,
   `wlc_phy_cal_perical(pi, 4)` (acphy-cal-tx "Who starts a calibration").
5. `wlc_bsscfg_set_infra_mode` (wlc-hw 5.2 step 1).
6. Infrastructure target: **`AMT[62]` = the BSSID with attribute 0x8002**
   (`wlc_bmac_write_amt(hw, 0x3e, bssid, 0x8002)`, bmac-init B5; MAC
   revisions below 40 use `wlc_bmac_set_addrmatch`), so that the AP's
   frames pass the BSSID filter during authentication; association state
   0x0c is then entered (`wlc_assoc_change_state`), and
   `wlc_bsscfg_up(wlc, cfg)`: `cfg+8` = 1 and, with the driver up,
   `wlc_mhf(wlc, 0, 0x4000, wlc_key_defkeyflag(wlc), 3)` (host flag 1 bit
   14, ps-keys 2). (wlc-hw 5.2 step 7 later writes `AMT[62]` again with
   0x8006.)

**`wlc_assoc_change_state(cfg, state)`**: when the association state
machine leaves its idle states (0, 0x0c for an infrastructure
configuration, 0x0d, 0x10), **PHY hold bit 0x1** is set
(`wlc_phy_hold_upd(pi, 1, 1)`, acphy-api A13); when it returns to one of
them and no other configuration is in the middle of a join, it is cleared
(`wlc_phy_hold_upd(pi, 1, 0)`) and the wake bit updated
(`wlc_set_wake_ctrl`, ps-keys 1.3: `wlc_stay_awake` is true while the state
machine is busy). The PS control follows (`wlc_set_ps_ctrl`).

**`wlc_roam_complete(cfg, status)`**: association state 0, the channel
statistics updated (`cca_stats_upd(wlc, 0)`, cca-stf.md); on success, if
the PHY's chanspec is not the new BSS's: MAC suspended, `wlc_set_chanspec`,
MAC enabled; `wlc_rate_lookup_init` with the BSS's rates.

**For the open driver** (mac80211 `mgd_prepare_tx` / the authentication
step): the channel (mac80211's channel context), the response-rate map from
the BSS's basic rates, a full calibration (reason 4) when the channel or BSS
is new, `AMT[62]` = BSSID with attribute 0x8002, PHY hold 0x1 while
authenticating and associating (released when the association completes or
fails); then wlc-hw 5.1/5.8.

### G8. Beamforming links (`wlc_txbf.c`) - open

`wlc_txbf_init_link` (.text+0x179e41, 1,459 bytes, called
from the association response, the beacon path and `wlc_init`) and
`wlc_txbf_delete_link` (.text+0x179d02) set up and remove a per-peer beamforming entry: an
address-match entry and a block of the beamforming area of the shared
memory (wlc-hw 1.4 sets up the area itself: `sub_179ae1`), with the MAC
suspended; `wlc_txbf_txchain_upd`/`wlc_txbf_rxchain_upd` (G5) keep the
area's chain words. They matter only when the station takes part in
explicit beamforming (VHT SU beamformee/beamformer capability advertised).
**Not specified**: an open driver that does not advertise beamforming
capabilities in its VHT capabilities does not need it.

### G9. Corrections recorded by newer specifications

Where an older document is superseded; the newer text wins.

| Older place | Corrected by | In short |
|---|---|---|
| frames.md 3.3 (`MacTxControlHigh` 0x0004, the prefix, cache fields) | ampdu.md "Corrections" | 0x0004 = "sent alone" (single MPDU; the aggregation path clears it); prefix byte 2 bit 1 is the epoch; cache fields `txh+0x66..0x6b` and `MacTxControlLow` 0x0040 given in ampdu 1.4 |
| frames.md 4.2 (transmit status words) | ampdu.md "Corrections" | for an aggregate `w2` holds sent/acknowledged MPDU counts at the primary and fallback rates, not transmissions per rate entry |
| wlc-hw.md open question 2 | ampdu.md "Corrections" | `SHM(0xba)`/`SHM(0x3c)` are the receive limits of ampdu 3.3 |
| frames.md 1.3 (`RxStatus2` bits 3, 4) | ps-keys.md "Corrections" | the core's TKIP MIC check (ps-keys 2.5) |
| wlc-hw.md 3 step 11 (null frame "keep-alive") | ps-keys.md "Corrections" | sent only while a change to power save is pending and allowed: a retry of the PM announcement (ps-keys 1.5) |
| wlc-hw.md 5.6 (`wlc_ps_allowed` fields) | ps-keys.md "Corrections" | `bsscfg+0x9a` = WPA authentication mode, `+0x90` low bits = enabled ciphers, `+0x9d` = keys in place, `+0x23` = DTIM period programmed |
| acphy-api-rx.md 1.6 (RSSI written per core) | frames.md "Corrections" | the AC-PHY RSSI body writes the corrected per-core values to `rxh+0x20+core` |
| bmac-data.md open question 6 | frames.md "Corrections" | the `&txavail` pointers are copied by `wlc_hw_get_txavail`, called from `wlc_attach` |
| bmac-init.md D1, D2, D4 | bmac-attach.md "Corrections" | D2 step 13.2 wrong (bit 31 of the MAC capabilities), D4's result and order; bmac-attach supersedes D1, D2, D4 |
| bmac-init.md E8, E9, E10 | bmac-run.md "Corrections" | E9 (energy detect) and E10 (coexistence) replaced by R1 and R3; E8 detail |
| bmac-init.md C4 step 5 | bmac-init.md "Corrections found while verifying" | `wlc_bmac_ifsctl_edcrs_set` does act on the AC-PHY (R1) |
| acphy-api-txpower.md (merge `sub_0b8ca4`, ppr service table) | acphy-api-txpower.md "Corrections and answers", ppr.md "Corrections" | the offsets ppr is cleared on every merge; order of the percentage and force-disabled steps; `ppr_apply_max/min`, `ppr_minus_cmn_val` semantics |
| acphy-api-watchdog.md step 3 | its own "Corrections and answers" | the pending noise-sample bit set |
| acphy-desense.md C1, C3, C4, E3 | its "Corrections" (2026-11-02, 2026-11-03) | noise-calibration early return, ring test; dwell counter and hysteresis of the interference engines |
| frames.md 3.5 ("the sum clamped to -32..31") | this document, G1.3 | the sum with the OLPC offset is formed and clamped only when the offset is not 0; otherwise the per-rate value is used unclamped (low 6 bits) |
| frames.md open question 7 | this document, G1 | answered |
| cca-stf.md `sub_172da9` ("`wlc_stf_spatial_expansion_get` ... not on the AC path") | this document, G1.1 | it is on the AC path: `wlc_acphy_txctl1_calc` calls it for every rate entry |
| wlc-hw.md 5.1 (join begins at the association response) | this document, G7 | the join starts in `wlc_join_BSS`: channel, calibration, `AMT[62]` with 0x8002, PHY hold 1 |
| frames.md 2.2 bit 0x00400000 | (consistent) | `wlc_bmac_btc_stuck_war50943` is the reset meant there; MAC revisions < 15 only |

## Verification

| Statement | How |
|---|---|
| Part 1: the lists of functions that run | `remaining.py --steps session` and `--steps real` on the hybrid core (all layers at their defaults), block coverage; both sessions ran to the end |
| Part 1: the reached-not-run list | index closure (`reach2.py`), read |
| G1 (per-rate power, expansion, control word 1) | `verify_pwr.py`: 16,884 calls of the object compared with a model written from G1, 0 differences |
| G2 (LED table, mask, up/activity/idle/down GPIO calls) | `verify_misc.py` on the object; other behaviours read |
| G3 (`wlc_enable_btc_ps_protection`, six states) | `verify_misc.py` (state forced); watchdog policy and predictor read |
| G4 (radio off/on, monitor timer, LED and PLL requests, event 40) | `verify_misc.py`; `MI_RFDISABLE` and MPC read |
| G2/G4 attach values: `stf+0xf4/0xf5` = 0 (no software antenna diversity), `pub+0x4f` = 0 (offload engine off), `pub+0xe8` = 1, `pub+0x44` = 2, `wlc+0x24e` = 0, MPC on | `verify_misc.py`, object after attach in the model |
| G5-G8 | read from the code only |

Scripts and outputs: `re-out/analysis/coverage/` (`remaining.py` outputs
`session.tsv`/`real.tsv`, `reach2.py`, `audit.py`, `gen.py`, `verdicts.tsv`,
`verify_pwr.py`, `verify_misc.py`).

## Open questions

1. **Beamforming links** (G8): not specified; needed only with beamforming
   capabilities advertised.
2. **Coexistence on the real card** (G3, bmac-run Q1): the resolved mode
   (5 or 1) depends on `aa2g` and the ECI/SECI capabilities of the real card;
   with mode 5 the upper-layer policy of G3 runs (host flag 3 bits 0x0008 and
   0x0200, PM 2 while Bluetooth is active). A read of `D11(0x15c)`,
   `CC(0x04)`, `CC(0xac)` and the SROM's `aa2g` on the card would settle it.
3. **The radio-switch register** `D11(0x158)` bit 16 (G4) is read by the
   interrupt path; whether the MacBook has a switch wired at all (the
   `MI_RFDISABLE` interrupt never seen) is unknown.
4. The meaning of host flag 3 bits 0x0008 and 0x0200 (G3) and host flag 1
   bit 0x0400 (G6) is the microcode's.
5. Not every function that reaches the hardware only through data or
   function pointers can be found from the index (1.1 "Limits"); the
   sessions' module callbacks are complete, the closure's are not.
