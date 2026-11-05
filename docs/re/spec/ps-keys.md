# Station power save and hardware keys (`ps-keys`)

Status: 2026-11-05, all three sections written: 1 station power save, 2
hardware keys, 3 periodic and event work; verification and open questions
at the end. Each statement says how it was checked: **observed** (direct calls
on Broadcom's object alone in the emulator after attach and up,
`re-out/analysis/ps-keys/verify.py` and `verify2.py`, outputs next to them)
or **read** (from the code only). The model has no microcode: what the
microcode does with the bits the host writes is inferred from how the host
uses them and is marked so.

Notation as in [wlc-hw.md](wlc-hw.md): `SHM(o)` the 16-bit shared-memory
word at byte offset `o`, `D11(o)` a register of the 802.11 core, `AMT[i]`
entry `i` of the address match table (bmac-init B5), host flag `n` bit `b`
= bit `b` of the word `wlc_mhf(wlc, n-1, ..)` changes (bmac-init A9).
`bsscfg` is the station's configuration (`wlc+0x2f8`, the primary one), `pm`
its power-save state (pointer at `bsscfg+0x338`), `pub` the pointer at
`wlc+0x00`, `scb` the station entry of the access point. Procedures of other
specifications are cited by their section (wlc-hw 5.6, frames 4.2, ...).

## Scope

Chip 0x4360, MAC revision 42, a station in an infrastructure BSS. Access
point, IBSS, wake-on-WLAN, offload engine and the TDLS/P2P paths are not
described (one line where they branch off).

| function | .text offset | size | name |
|---|---|---|---|
| `wlc_set_pmstate` | 0x03f924 | 301 | original (wlc.c) |
| `wlc_set_ps_ctrl` | 0x03b62c | 82 | original |
| `wlc_ps_allowed` | 0x03b5b9 | 115 | original |
| `sub_03a14c` | 0x03a14c | 96 | assigned: `wlc_ucode_wake_upd` (as in wlc-hw) |
| `wlc_set_wake_ctrl` | 0x03a1ac | 41 | original |
| `wlc_stay_awake` | 0x024bc7 | 179 | original (wlc-hw 5.6) |
| `wlc_set_pmpending` | 0x03b335 | 53 | original |
| `sub_03b36a` | 0x03b36a | 45 | assigned: `wlc_pmpending_txs_upd` |
| `wlc_update_pmstate` | 0x03b397 | 153 | original |
| `wlc_pm_pending_complete` | 0x03a1d5 | 213 | original |
| `wlc_txs_alias_to_old_fmt` | 0x02546e | 80 | original |
| `wlc_sendnulldata` | 0x03f756 | 167 | original |
| `sub_02d48b` | 0x02d48b | 398 | assigned: `wlc_nulldata_frame` |
| `wlc_sendpspoll` | 0x03eb4a | 287 | original |
| `wlc_set_pspoll`, `wlc_pspoll_timer`, `wlc_pspoll_timer_upd` | 0x03a391, 0x03ec69, 0x02ffaf | 89, 46, 115 | original |
| `sub_03ec97` | 0x03ec97 | 37 | assigned: `wlc_pspoll_more_data` |
| `sub_03a3ea` | 0x03a3ea | 56 | assigned: `wlc_pspoll_txdone` |
| `wlc_tim_process` | 0x03fd93 | 377 | original |
| `wlc_set_pmawakebcn`, `wlc_set_uatbtt`, `wlc_set_pmoverride`, `wlc_update_bcn_info`, `wlc_reset_pmstate` | 0x03a422, 0x03a2aa, 0x03fbd7, 0x03b76f, 0x03b67e | 156, 142, 41, 90, 114 | original |
| `sub_025fb0` | 0x025fb0 | 166 | assigned: `wlc_tbtt_check` |
| `sub_03fc00` | 0x03fc00 | 164 | assigned: `wlc_set_pm_mode` (ioctl `WLC_SET_PM`) |
| `sub_03faf2` | 0x03faf2 | 204 | assigned: `wlc_pm2_sleep_ret_timeout` |
| `wlc_pm2_sleep_ret_timer_start`, `_stop`, `wlc_pm2_ret_upd_last_wake_time` | 0x02ff77, 0x02ff5c, 0x02ff2d | 56, 27, 47 | original |
| `wlc_apsd_trigger_upd`, `wlc_set_apsd_stausp`, `wlc_apsd_trigger_timeout`, `wlc_sendapsdtrigger` | 0x03018e, 0x03a338, 0x03f88f, 0x03f838 | 191, 89, 149, 87 | original |
| `wlc_bss_tbtt`, `wlc_tbtt` | 0x0462fd, 0x046400 | 259, 50 | original |
| `wlc_pretbtt_calc`, `wlc_dtimnoslp_set`, `wlc_bcn_li_upd`, `wlc_gptimer_wake_upd` | 0x024ce2, 0x027eab, 0x027e3c, 0x03b574 | 185, 54, 53, 51 | original |
| `wlc_txfifo` (its power-save part), `sub_04ab93` (the beacon part, wlc-hw), `sub_0450f0` (watchdog, its power-save part) | 0x03e27a, 0x04ab93, 0x0450f0 | | as in frames / wlc-hw |
| `wlc_key_insert` | 0x07c3be | 2145 | original (wlc_key.c) |
| `wlc_key_hw_init` | 0x07acc5 | 1015 | original |
| `wlc_key_hw_init_all` | 0x07b763 | 447 | original (wlc-hw 1.4 step 9) |
| `wlc_key_update`, `wlc_key_delete`, `wlc_key_defkeyflag`, `wlc_key_lookup` | 0x07b6a8, 0x07bd56, 0x07ac3d, 0x07ac04 | 187, 482, 136, 57 | original |
| `wlc_key_iv_init`, `wlc_key_iv_update` | 0x07bb56, 0x07b922 | 234, 564 | original |
| `sub_07c1c1` | 0x07c1c1 | 210 | assigned: `wlc_key_slot_alloc` |
| `wlc_rcmta_add_bssid` | 0x07c293 | 299 | original |
| `wlc_write_amt`, `wlc_read_amt`, `wlc_set_rcmta`, `wlc_write_seckindxblk_secinfo_by_idx`, `wlc_write_amtinfo_by_idx` | 0x027c3c, 0x027bbf, 0x026df0, 0x027d23, 0x027d01 | 157, 100, 70, 31, 34 | original (wlc.c) |
| `wlc_wsec_tx_tkmic_offset`, `wlc_wsec_rx_tkmic_offset` | 0x1717a8, 0x1717ba | 18, 11 | original (wlc_security.c) |
| `sub_026763` | 0x026763 | 132 | assigned: `wlc_txh_key_fill` |
| `sub_03432b`, `sub_03d445` | 0x03432b, 0x03d445 | 1904, 125 | assigned: `wlc_txframe_encap`, `wlc_txh_iv_refresh` (their key parts only) |
| `sub_172296` | 0x172296 | 1809 | assigned: `wlc_wsec_rx_decrypt` |
| `wlc_wsec_recvdata`, `wlc_wsec_miccheck`, `wlc_wsec_rxiv_update` | 0x1729a7, 0x171d08, 0x171768 | 261, 1057, 64 | original (wlc_security.c) |
| `wlc_wsec` | 0x03fa51 | 161 | original (iovar `wsec`) |

## Overview

**Power save** is shared between the host and the microcode. The host
decides *whether* the station may doze and tells the microcode with two bits
of the MAC control register: bit 25 (0x02000000, brcmsmac name HPS: the
station is in power save, the access point has been or is being told so) and
bit 26 (0x04000000, WAKE: the microcode must not doze now). With HPS set and
WAKE clear the microcode dozes between beacons and wakes itself for the
beacons it must hear, using the TBTT and DTIM state that the join wrote
(wlc-hw 5.2 to 5.5: TSF, `D11(0x188)`, `D11(0x18c)`, PRETBTT, `SHM(0x12)`,
`SHM(0xb6)`). The **host** announces the change of state to the access
point with a null data frame, parses the TIM of every beacon itself (the AID
is never given to the hardware), fetches buffered frames (PS-Poll, or
leaving power save for a while), and holds WAKE whenever it waits for
something (the acknowledgement of the null frame, a PS-Poll answer, a beacon
to re-align the TBTT, an U-APSD service period, a scan).

**Keys** live in a table of 54 entries of 16 bytes in shared memory (base
`2 * SHM(0x56)`), with one 16-bit word per entry in the security index block
`SHM(0x5e0 + 2i)` that gives the hardware cipher and the entry number.
Entries 0..3 are the default (group) keys by key id; entries 4..53 are
per-address (pairwise) keys, each bound to the address in `AMT[i - 4]`. The
host builds the IV/PN in the frame; the core encrypts and appends the
MIC/ICV on transmit, selected by two bytes of the transmit header, and
decrypts on receive, reporting it in `RxStatus1`; the host strips the IV and
MIC/ICV and checks replays. TKIP needs host help in both directions (the
phase-1 key). There is no hardware support for BIP (management frame
protection) or GCMP.

## 1. Station power save

### 1.1 State

Fields of `pm` (`bsscfg+0x338`), by their use:

| offset | size | meaning | set by |
|---|---|---|---|
| +0x00 | 1 | PM mode: 0 off (CAM), 1 `PM_MAX` (PS-Poll), 2 `PM_FAST` (leave power save on traffic, return after a quiet time) | `sub_03fc00` (ioctl `WLC_SET_PM`; the Linux glue sets 2) |
| +0x01 | 1 | PM override: power save forbidden (no TIM in the beacon, roaming, ...) | `wlc_set_pmoverride` |
| +0x04 | 4 | a "PM blocked" count; power save is not re-entered while it is not 0 | other modules; cleared when PM goes off |
| +0x08 | 1 | power save enabled (the station has asked to be in power save) | `wlc_set_pmstate` |
| +0x09 | 1 | stay awake until the next beacon | `wlc_set_pmawakebcn` |
| +0x0a | 1 | PM pending: the change to power save has not yet been acknowledged | `wlc_set_pmpending` |
| +0x0b | 1 | the last transmit status of this configuration had "PM indicated" | `wlc_update_pmstate` |
| +0x0c | 1 | a PS-Poll is outstanding | `wlc_set_pspoll` |
| +0x0d | 1 | update the TBTT at the next beacon | `wlc_set_uatbtt` |
| +0x0e | 2 | PS-Poll period (ms; 0: no periodic PS-Poll) | iovar |
| +0x18 | 4 | U-APSD trigger period (ms) | iovar |
| +0x28 | 1 | an U-APSD service period is running | `wlc_set_apsd_stausp` |
| +0x29 | 1 | (when set, a mode change through `sub_03fc00` does not call `wlc_set_pmstate`) | other modules |
| +0x34 | 4 | PM2 sleep-return time (ms); observed default 200 | iovar `pm2_sleep_ret` |
| +0x38 | 4 | the time the PM2 timer is currently armed for (ms) | `sub_03faf2` |
| +0x3c | 4 | time of the last data activity (high-resolution timer, us) | `wlc_pm2_ret_upd_last_wake_time` |
| +0x58 | 1 | `PM_FAST`: a frame was posted while PM was pending | `wlc_txfifo`; cleared when PM pending is set |
| +0x5c | 4 | a "PM held" count (scan, measurement): no TIM handling, no PS-Poll while not 0 | other modules |

Fields of `wlc` (each requests the WAKE bit through `wlc_stay_awake`,
wlc-hw 5.6): `+0x2ac` update at TBTT, `+0x2ad` PM pending, `+0x2ae` PS-Poll
outstanding, `+0x2af` awake for a beacon, `+0x5c2` U-APSD service period,
`+0x61a` PM2 timer running after a pending change, `+0x6c8` requests of the
general-purpose timer users (`wlc_gptimer_wake_upd`). `wlc+0x830` is a
countdown of the watchdog (wlc-hw 3 step 3) set to 2 when a null frame is
sent.

Fields of `bsscfg`: `+0x22` infrastructure, `+0x0c` associated, `+0x23` the
DTIM period is known and programmed (set by `wlc_update_bcn_info(bsscfg, 1)`
after the first beacon with a TIM, wlc-hw 5.1 step 3.2), `+0x90` the
security word (bits 0..2 cipher enabled: 1 WEP, 2 TKIP, 4 AES; bit 3:
software encryption only), `+0x9a` the WPA authentication mode (0: none),
`+0x9d` the keys are in place (set by `wlc_key_insert` for a group key,
cleared by `wlc_key_delete`), `+0xf0` the BSSID, `+0xf6` the own address,
`+0x112` the AID (as received, with bits 14..15 set).

### 1.2 Power save allowed

`wlc_ps_allowed(bsscfg)` is true only if all hold (read; observed with the
configuration made associated and its DTIM marked programmed: false while
`pm+0x08` = 0, true after `wlc_set_pmstate(bsscfg, 1)`):

1. infrastructure (`+0x22`) and associated (`+0x0c`);
2. power save enabled (`pm+0x08`) and no PM override (`pm+0x01`);
3. the DTIM period is programmed (`bsscfg+0x23`);
4. no WPA (`+0x9a` = 0), or no cipher (`+0x90 & 7` = 0), or the keys are in
   place (`+0x9d`): with WPA the station stays awake until the 4-way
   handshake has installed the group key (section 2.5);
5. no channel switch announcement is being followed (`wlc_csa_state_active`);
6. it is the primary configuration.

### 1.3 The two MAC control bits

* **HPS, bit 25.** `wlc_set_ps_ctrl(bsscfg)`, only while the MAC layer is
  up (`wlc+0x31`): `wlc_bmac_mctrl(hw, 0x02000000, allowed ? 0x02000000 :
  0)` with `allowed` = `wlc_ps_allowed(bsscfg)` (for the primary
  configuration only), then the WAKE update below and `cca_stats_upd(wlc,
  0)` (cca-stf part 1). Observed: entering power save writes `D11(0x120)` =
  0x42020403 (from 0x40020403), leaving writes 0x40020403. Callers: the PM
  state change (1.4), the join (wlc-hw 5.1), `wlc_update_bcn_info`, the TIM
  path (U-APSD), `wlc_init` when re-run while associated, a fatal error's
  restore.
* **WAKE, bit 26.** `sub_03a14c(wlc)`: reads `D11(0x120)` (the value is not
  used), s = `wlc_stay_awake(wlc)` (wlc-hw 5.6: any of the requests listed
  in 1.1, a scan, association in progress, monitor mode, ...),
  `wlc_bmac_mctrl(hw, 0x04000000, s ? 0x04000000 : 0)`, and if s:
  `wlc_bmac_wait_for_wake(hw)` (bmac-init A5: polls `SHM(0x40)` until it is
  not 4 = "asleep"). `wlc_set_wake_ctrl(wlc)` runs it (and
  `cca_stats_upd`) while the driver is up (`pub+0x34`); every setter of a
  wake request calls it on a change. Observed: `D11(0x120)` 0x42020403 ->
  0x46020403 followed by one read of `SHM(0x40)` when PM became pending.

`wlc_bmac_mctrl` writes the register only when the software copy changes
(bmac-init A2), and the core's own wake override (bmac-init A3/A4, e.g.
while the MAC is suspended or a FIFO is suspended) ORs WAKE in
independently.

Inference about the microcode (not observable in the model): with HPS set
the microcode puts the PM bit (frame control bit 12) into the frames it
transmits - the host never sets that bit itself: the null frame is built
with frame control 0x0148 (observed) and the transmit status reports back
whether the frame went out with PM set ("PM indicated", frames 4.2 `w0` bit
3). With HPS set and WAKE clear it dozes and wakes for beacons on its own.

### 1.4 Entering and leaving: wlc_set_pmstate(bsscfg, on)

Infrastructure configurations only (else nothing). Steps (read; observed for
on = 1 and 0 with `PM_FAST` and `PM_MAX`):

1. If `on` = 0 and an U-APSD service period runs: `wlc_set_apsd_stausp(
   bsscfg, 0)`.
2. `pm+0x08` = on; **`wlc_set_ps_ctrl(bsscfg)`** (1.3: HPS follows
   `wlc_ps_allowed`, WAKE follows the requests). If `on` = 0: `pm+0x04` = 0.
3. **`wlc_set_pmpending(bsscfg, wlc_ps_allowed(bsscfg))`**: `pm+0x0a` = the
   value; on a change: if now pending, `pm+0x58` = 0 and the request
   `wlc+0x2ad` = 1; then `wlc_set_wake_ctrl` (WAKE set while pending).
4. If the driver is up and the configuration associated: **a null data
   frame** to the BSSID (`wlc_sendnulldata(wlc, bsscfg, BSSID, 0, 0, -1)`)
   unless the BSSID is zero or bit 0 of the byte `wlc+0x304` is set. If no
   frame was sent (one of those, or no station entry for the BSSID, or no
   buffer): `sub_03b36a(bsscfg, pending ? 0x82 : 0x02)` (1.5), which for a
   pending change clears it at once.
5. `wlc_pspoll_timer_upd(bsscfg, on)`: the periodic PS-Poll timer (if the
   period `pm+0x0e` is not 0 and the station is associated with power save
   on, armed periodic with that period; else stopped);
   `wlc_apsd_trigger_upd(bsscfg, on)`: the periodic U-APSD trigger timer
   (only if the AP's station entry has U-APSD (`scb+0x08` bit 6) with
   delivery-enabled categories (`scb+0xed`), period `pm+0x18` / 2 ms).

The **null data frame** (`sub_02d48b`, `wlc_sendnulldata`): a data frame
without body, frame control 0x0048 with To-DS (0x0100) for an
infrastructure BSS, i.e. 0x0148 (observed); addresses: BSSID, own address,
BSSID. With a priority argument >= 0 and a QoS station it would be a QoS
null (0x00c8) with the QoS control field (priority, ack policy); the PM path
passes -1, so it is never QoS here. It is sent through the control-frame
path (`wlc_sendctl`, frames 3) on **FIFO 1** (observed: the transmit
pointer `D11(0x244)` of FIFO 1 advances). The host does not set the PM bit
(1.3). `wlc+0x830` = 2.

In the observed runs, `wlc_set_pmstate(bsscfg, 1)` with the configuration
associated made, in this order: `D11(0x120)` = 0x42020403 (HPS),
`D11(0x120)` = 0x46020403 (WAKE, PM pending), a read of `SHM(0x40)`, the
null frame posted on FIFO 1, and the read of `D11(0x120)` that `wlc_txfifo`
makes after every post while the PM mode is not 0 (frames 3.8).
`wlc_set_pmstate(bsscfg, 0)` made `D11(0x120)` = 0x40020403 (HPS and WAKE
off) and posted the null frame (the microcode sends it with PM clear).

### 1.5 Completion: the transmit status

Every transmit status of a frame that belongs to the station's
configuration (frames 4.3 step 10, also the A-MPDU status path) ends with
`wlc_update_pmstate(bsscfg, s)`, where s is the status in the old 16-bit
format (`wlc_txs_alias_to_old_fmt`): bit 1 acknowledged, bits 2..5 the
suppression reason, bit 6 intermediate, bit 7 **PM indicated** (`w0` bit 3
of frames 4.2), bits 12.. the transmission count.

`wlc_update_pmstate(bsscfg, s)`, for an associated infrastructure
configuration:

1. If PM is pending, a scan is in progress and s has none of bits 1..7: s
   is taken as 0x82 (the null frame of a scan's off-channel step counts as
   done).
2. If bit 7 of s differs from `pm+0x0b`: `sub_03b36a(bsscfg, s)`, then
   `pm+0x0b` = bit 7.
3. If bit 7 is set and PM is pending: `sub_03b36a(bsscfg, s)`.
4. `wlc_pm_pending_complete(wlc)`.

`sub_03b36a(bsscfg, s)`: only while PM is pending and (`pm+0x5c` != 0 or s
bit 1, acknowledged): `wlc_set_pmpending(bsscfg, !(s bit 7))`. So **the
change to power save is complete when a frame that went out with the PM bit
has been acknowledged**; an acknowledged frame without PM keeps it pending.
Observed: status 0x02 left PM pending and WAKE set; status 0x82 cleared it
and `D11(0x120)` went 0x46020403 -> 0x42020403 (the microcode may doze).

`wlc_pm_pending_complete(wlc)`: if the request `wlc+0x2ad` is set and the
station configuration has no PM pending (or is not associated): clears
`wlc+0x2ad` and `wlc+0x830`, `wlc_set_wake_ctrl`, tells the scan and
measurement modules (they wait for it before leaving the channel), stops
the timer `wlc+0x620`; then, if a post-pending awake time `wlc+0x618` (ms) is
configured and the station is associated in `PM_FAST` with power save on:
`wlc+0x61a` = 1 (a WAKE request) and that timer armed for `wlc+0x618` ms
(whose expiry clears the request; not followed).

If the null frame is lost, PM stays pending and WAKE stays set; the watchdog
sends the null frame again every second while PM is pending and power save
is allowed (`sub_0450f0`; read). (wlc-hw section 3 step 11 calls this a
keep-alive; it is this retry.)

Leaving power save needs no completion: PM pending is already 0
(`wlc_ps_allowed` is false with `pm+0x08` = 0), HPS is cleared before the
null frame is queued, and the WAKE bit follows the other requests.

### 1.6 The beacon: TIM, awake-for-beacon, TBTT

The host parses every beacon of its BSS (`sub_04ab93`, wlc-hw 5.3/5.4); the
power-save part, in order (read):

1. **`wlc_tim_process(wlc, bsscfg, scb, ies, len)`**: finds the TIM element
   (id 5). If it is missing, shorter than 3 bytes, or its DTIM period (byte
   3 of the element) is not greater than its DTIM count (byte 2):
   `wlc_set_pmoverride(bsscfg, 1)` (power save forbidden; the change
   re-runs `wlc_set_pmstate` with the stored state) and return. Otherwise,
   only while `pm+0x5c` = 0: with a = the AID (`bsscfg+0x112` & 0x3fff),
   o = (a >> 3) - (bitmap control & 0xfe); the station has buffered traffic
   if o < length - 3 and bit (a & 7) of the partial virtual bitmap byte o
   is set. Then:
   * `PM_FAST` (`pm+0x00` = 2): `wlc_set_pmstate(bsscfg, 0)` (leave power
     save: HPS off, null frame without PM) and
     `wlc_pm2_sleep_ret_timer_start` (1.7). Observed: `D11(0x120)` =
     0x40020403, the null frame on FIFO 1, then `D11(0x18)` = 0x32000 (the
     general-purpose timer, 200 << 10 us).
   * Otherwise, if WME is off (`pub+0x54` = 0), or the AP does not
     advertise U-APSD (byte +0x2b of the configuration's WME state
     `bsscfg+0x570`), or the AP's station entry is given and not all four
     access categories are delivery-enabled (`scb+0xee` != 0x0f): with power
     save on, **`wlc_sendpspoll`**; with power save off, a null data frame.
   * Otherwise (all categories delivery-enabled) and the configuration's
     WME byte +0x29 set: with power save on, the trigger frame
     (`wlc_apsd_trigger_timeout`: a QoS null at the trigger priority) - and
     the station entry's flag bit 27 cleared; then `wlc_set_ps_ctrl`.
   * No traffic: if a PS-Poll is outstanding and no PS-Poll period is set:
     `wlc_set_pspoll(bsscfg, 0)`.
   Returns the DTIM count.
2. If `pm+0x09` (awake for a beacon) is set: `wlc_set_pmawakebcn(bsscfg,
   0)`. The request is set by `wlc_sta_assoc_upd` at association and by
   `wlc_set_chanspec` after a channel change, so the WAKE bit is held from
   the association (or the channel change) until the first beacon has been
   received.
3. If no TBTT update is pending and `sub_025fb0(bsscfg)` says the TBTT
   register is out of range: `wlc_set_uatbtt(bsscfg, 1)` (`wlc+0x2ac`, a
   WAKE request). `sub_025fb0`: for the primary configuration without a
   pending update, with i = 2 x the beacon interval (TU), times the DTIM
   period (`bss+0x60`) for an infrastructure BSS - or, if set, times the
   listen interval in DTIMs (`wlc+0x2b2`) or 2 x beacon interval x the
   listen interval in beacons (`wlc+0x2b1`) -, reads `D11(0x180)` (TSF low)
   and `D11(0x18c)` (next TBTT): out of range when the TBTT is behind the
   TSF or more than i x 1024 us ahead of it.
4. If a TBTT update is pending: `wlc_tbtt_align(bsscfg, short preamble,
   rate, body)` (wlc-hw 5.3: `D11(0x18c)` = the next TBTT from this
   beacon's timestamp, corrected by its air time) and `wlc_set_uatbtt(
   bsscfg, 0)`.

At the join (wlc-hw 5.1 step 3), the first beacon gives the DTIM period:
`SHM(0x12)` = DTIM period, `wlc_update_bcn_info(bsscfg, 1)` (`bsscfg+0x23`
= 1, then `wlc_set_ps_ctrl`), and the PM state is applied
(`wlc_set_pmstate(bsscfg, pm+0x08)`).

**The words the microcode uses for waking** (written by the join and init,
wlc-hw): `D11(0x180/0x184)` TSF, `D11(0x188)` = beacon interval << 16,
`D11(0x18c)` next TBTT, `D11(0x612)` = `SHM(0x96)` = PRETBTT
(`wlc_pretbtt_calc`: 1024 us for an infrastructure BSS on the AC-PHY; the
microcode wakes this long before the TBTT), `SHM(0x12)` DTIM period,
`SHM(0xb6)` the listen interval (`wlc_bcn_li_upd`: DTIM count << 8 | beacon
count from the iovars `bcn_li_dtim`/`bcn_li_bcn`, 0 when the DTIM count is
1; 0 by default - by the name the microcode then wakes for every beacon it
is told to by `SHM(0x12)`; not checked), `SHM(0x4c)` "DTIM no-sleep"
(`wlc_dtimnoslp_set` = (PRETBTT + `wlc+0x290` x 1024) >> 3, written only
by `wlc_init` when re-run while associated; otherwise the initialisation
list's value), `SHM(0x40)` the microcode's state (4 = asleep, read by A5).
The upper layer writes no other shared-memory word for power save (read:
no other `wlc_write_shm` in the functions of this section).

The TBTT interrupts (`MI_TBTT` 0x4, `MI_DTIM_TBTT` 0x80000) are not in the
default interrupt mask (frames 2.2), so `wlc_tbtt`/`wlc_bss_tbtt` (count;
in power save, run the watchdog from the TBTT at most once per second and
re-arm the watchdog timer to max(2000, 2 x beacon interval x DTIM period
(or listen interval)) ms) never run for a station on this core. A mac80211
driver needs neither.

### 1.7 PM modes

* **`PM_MAX` (1)**: the station stays in power save. When the TIM shows
  traffic it sends a **PS-Poll** (`wlc_sendpspoll`): only if the AP's
  station entry has no PS-Poll outstanding (`scb+0x08` bit 23); a control
  frame of 16 bytes, frame control 0x00a4, duration/ID = the AID word
  `bsscfg+0x112` as stored (bits 14..15 set), BSSID, own address;
  `wlc_set_pspoll(bsscfg, 1)` (WAKE request `wlc+0x2ae`); sent with
  `wlc_sendctl` (FIFO argument 3 - by frames 3.1 the voice FIFO; read) at a
  packet priority with the low bits 2; `scb+0x08` |= bit 23. Its transmit
  completion (`sub_03a3ea`) clears bit 23 and, if the status shows neither
  an acknowledgement nor a suppression, ends the PS-Poll state. Each
  received unicast frame from the AP while a PS-Poll is outstanding
  (`wlc_recv`, `wlc_recvdata_ordered` -> `sub_03ec97`): frame control bit
  13 (More Data) set -> another PS-Poll; clear -> `wlc_set_pspoll(bsscfg,
  0)` (WAKE may drop). A periodic PS-Poll (`wlc_pspoll_timer`, period
  `pm+0x0e`) is sent when nothing is held, pending or scanning. (Read; the
  emulated TIM call passed no station entry and so took the U-APSD branch,
  which did nothing: no PS-Poll was observed.)
* **`PM_FAST` (2)** - what the Linux glue selects:
  1. TIM bit set -> leave power save (1.6 step 1).
  2. **A frame is posted** (`wlc_txfifo`, for every frame not marked by
     packet tag bit 0x04 of byte 7) while power save is on, in an
     infrastructure BSS, `PM_FAST`, `pm+0x5c` = 0: if PM is not pending or
     `pm+0x58` is set, `wlc_set_pmstate(bsscfg, 0)` and the PM2 timer
     started; else `pm+0x58` = 1 (the second frame during a pending change
     leaves power save). Leaving costs one null frame; the data frame
     itself goes out with PM clear because HPS is already off.
  3. Activity: the transmit status of a data or QoS data frame (frames 4.3,
     when acknowledged) and received data frames
     (`wlc_recvdata_ordered`, also a replayed frame in `sub_172296`)
     record the time (`wlc_pm2_ret_upd_last_wake_time`, `pm+0x3c`).
  4. **The PM2 timer** (`wlc_pm2_sleep_ret_timer_start`: a high-resolution
     timer of `pm+0x38` << 10 us, i.e. the general-purpose timer
     `D11(0x18)`, interrupt `MI_TO`; observed 0x32000 for 200 ms). Its
     handler `sub_03faf2`, `PM_FAST` only: e = the time since the last
     activity in units of 1024 us. If e < `pm+0x38` - 5: re-armed for
     `pm+0x34` - e. Otherwise `pm+0x38` = `pm+0x34`, and if no scan is in
     progress, no measurement (`wlc+0x590`), no frames are queued
     (`wlc_txpktcnt` = 0), the configuration is associated and `pm+0x04` =
     0: `wlc_set_pmstate(bsscfg, 1)` (back to power save with the null frame
     with PM); then the timer is stopped if PM is pending or `pm+0x04` != 0,
     else re-armed.
* **Mode change** (`sub_03fc00(wlc, mode, bsscfg)`, mode 0..2, the ioctl
  `WLC_SET_PM`): stores the mode; `wlc_bmac_core_phypll_ctl(hw, mode = 0)`
  for MAC revisions > 40 (bmac-init A16: no access on this core); stops the
  PM2 timer while up; `wlc_set_pmstate(bsscfg, mode != 0)` (for mode 0
  always, otherwise unless `pm+0x29`); the watchdog and A-MPDU modules are
  told (no access).

**U-APSD** (read only, briefly): when a QoS data frame of a
trigger-enabled access category (bit of `scb+0xed` for the category of the
frame's FIFO) is posted in power save with no PM pending, the service
period starts (`wlc_set_apsd_stausp(bsscfg, 1)`: WAKE request `wlc+0x5c2`)
and the station entry's flag bit 27 is set; a received QoS frame with the
EOSP bit ends it (`wlc_recvdata_ordered`). The periodic trigger timer sends
QoS null frames (`wlc_apsd_trigger_timeout`). Nothing of U-APSD reaches the
hardware except the WAKE bit and the frames.

### 1.8 Beacon loss and other events

* **Beacon loss** (watchdog, once per second, read): the roam state counts
  seconds since the last beacon (byte +6 of `bsscfg+0x328`, reset by each
  beacon). With power save on and a BSSID, once that time reaches half the
  beacon timeout (`pub+0x8e` ms, at least 2000; computed against the
  beacon or DTIM interval): `wlc_set_uatbtt(bsscfg, 1)` - **WAKE is held**
  so that the microcode listens continuously and the TBTT is re-aligned on
  the next beacon. Past the timeout: `wlc_set_uatbtt(bsscfg, 0)` and
  `wlc_roam_bcns_lost` (roam scan or link loss: software).
* **Traffic arriving**: the TIM (1.6), the PS-Poll chain (1.7), `PM_FAST`
  leaving on transmit (1.7).
* **Association, disassociation**: `wlc_sta_assoc_upd(bsscfg, 1)` sets the
  awake-for-beacon request; on leaving, `wlc_reset_pmstate(bsscfg)`:
  awake-for-beacon off, a pending change completed as with status 0x82,
  PS-Poll, U-APSD and TBTT requests off, `pm+0x08` = (PM mode != 0), then
  `wlc_set_ps_ctrl` (HPS off because not associated).
* **Channel change and scan**: `wlc_set_chanspec` sets the awake-for-beacon
  request; the scan holds WAKE through `wlc_stay_awake` while it runs, and
  waits for `wlc_pm_pending_complete` when it has to announce power save to
  the AP before leaving the channel (wlc-hw 6.2).
* **Fatal error / re-init**: `wlc_init` while associated re-applies
  `wlc_set_ps_ctrl`, the awake-for-beacon request and `SHM(0x4c)` (wlc-hw
  1.4 step 36).
* After every frame posted while the PM mode is not 0, the object reads
  `D11(0x120)` once (chips 0x4352, 0x4331, 0x4360; frames 3.8; observed).
  The value is not used.

### 1.9 For a mac80211 driver

What the core has to provide is the object's order, with mac80211 deciding
when (`IEEE80211_CONF_PS` from the `config` callback, dynamic power save
with `PM_FAST`-like behaviour done by mac80211's timers):

1. **Enter**: set HPS (`D11(0x120)` bit 25) and hold WAKE (bit 26); queue a
   null data frame to the BSSID (frame control 0x0148, FIFO 1, no PM bit
   from the host); when its transmit status shows "PM indicated" (frames
   4.2 `w0` bit 3) and acknowledged, clear WAKE unless something else needs
   the core awake (scan, PS-Poll outstanding, a beacon to re-align the
   TBTT, the first beacon after association or a channel change). If the
   status shows no acknowledgement, keep WAKE and send the null frame again
   (the object retries once per second).
2. **Leave**: clear HPS, set WAKE and wait for `SHM(0x40)` != 4 (bmac-init
   A5), then queue the null frame (it goes out with PM clear).
3. **TIM and buffered frames**: in software, as the object does (1.6, 1.7);
   mac80211 parses the TIM itself and sends PS-Polls or wakes up.
4. Keep TSF, beacon interval, TBTT, PRETBTT and `SHM(0x12)` = DTIM period
   programmed as at the join (wlc-hw 5.8); re-align `D11(0x18c)` from a
   beacon when it drifts (1.6 steps 3 and 4) and hold WAKE while beacons
   are being missed (1.8).

This maps onto `IEEE80211_HW_SUPPORTS_PS` without
`IEEE80211_HW_PS_NULLFUNC_STACK` (the driver sends the null frames, as
here). With `PS_NULLFUNC_STACK` mac80211 sends the null frame with the PM
bit already set *before* it reports `IEEE80211_CONF_PS`, i.e. while HPS is
still clear; whether the microcode transmits a host-set PM bit unchanged
when HPS is clear is not known (open question 1), so that variant needs a
test on the card.

Proposed open entry point (names not yet in `open/`):
`bcm4360_core_set_ps(core, bool ps, bool awake)` - HPS = `ps`, WAKE =
`awake` or any internal request; the null frame is an ordinary frame of
`bcm4360_core_tx`.

## 2. Hardware keys

### 2.1 Layout for MAC revision 42

Constants set by `wlc_attach` for MAC revisions >= 40: 54 key entries
(`pub+0xd4`: 61, reduced to 54 when transmit beamforming is present, which
it is on this card), the security index block at `SHM(0x5e0)` with 68 words
(`pub+0xdc`, `pub+0xe0`), 64 address-match entries usable for keys
(`pub+0xd8`).

* **Key table**: byte offset B = 2 x `SHM(0x56)` (read once per init,
  `wlc+0x3c0`, wlc-hw 1.4 step 6). Entry i (0..53) is the 16 bytes at
  `SHM(B + 16i)` .. `SHM(B + 16i + 14)`, the key bytes in order, two per
  word (byte 2k in the low half of word k), zero-padded: WEP-40 5 bytes,
  WEP-104 13 bytes, CCMP the 16-byte key, TKIP the 16-byte temporal key
  (the MIC keys go elsewhere). An empty entry is 16 zero bytes. (Observed
  with B set to 0x1000: entry 4 at `SHM(0x1040..0x104e)`, entry 1 at
  `SHM(0x1010..)`, entry 0 at `SHM(0x1000..)`; key bytes 0x10, 0x11, ...
  gave the words 0x1110, 0x1312, ...)
* **Security index block**: `SHM(0x5e0 + 2i)` for entry i: bits 0..3 the
  hardware cipher of the entry, bits 4..9 the entry number i (so the word is
  never 0 for i > 0 even when empty), bits 10..13 for other configurations
  (access point / IBSS) the cipher of the group key; 0 for a station.
  Hardware cipher values of revisions >= 40: 0 none, 1 WEP-40, 2 TKIP, 3
  WEP-104, 5 AES-CCM, 4 AES-CCM when the station entry's byte +0x09 has bit
  2 (a variant whose meaning was not followed; not for a normal WPA2 AP).
  (Observed: 0x45 for CCMP in entry 4, 0x15 for CCMP in entry 1, 0x42 for
  TKIP in entry 4, 0x03 for WEP-104 in entry 0, **0x20 for a TKIP group key
  in entry 2** - see the rule below.) Revisions below 40 number the ciphers
  differently (3 AES, 4 WEP-104, 5 the variant); not here.
* **Binding to the peer**: entry i >= 4 belongs to the address in
  `AMT[i - 4]`, attribute bit 2 (0x0004) set (`wlc_bmac_set_rcmta`,
  bmac-init E3: the attribute is OR-ed in with bit 15 = 0x8004, so the
  entry reads back 0x8004 plus whatever attribute bits were there).
  Observed: entry 4 for 00:11:22:33:44:55 -> `AMT[0]` = 0x33221100 /
  0x80045544. Entries 0..3 have no address: they are the **default keys**
  by key id (the key id of a received frame's IV selects the entry; by the
  host's look-up rule, read). `AMT[62]` (BSSID) and `AMT[63]` (own address)
  are not key entries.
* **TKIP receive phase-1 block**: for a TKIP key in entry i >= 4, seven
  words at `SHM(0x2e0 + 14i)`: words 0..4 the phase-1 key (TTAK, five 16-bit
  values) for the transmitter address of the peer and the current receive
  IV32, word 5 the low and word 6 the high half of that IV32. (Observed for
  entry 4: `SHM(0x318..0x320)` = the phase-1 key, `SHM(0x322)` =
  `SHM(0x324)` = 0 for IV32 = 0.) Entries 4..53 occupy `SHM(0x318)` ..
  `SHM(0x5d2)`.
* **TKIP MIC keys**: only if the MAC capabilities word kept at attach
  (`wlc+0x230`, wlc-hw 1.1 step 5) has bit 31 set, at byte offset M = 2 x
  `SHM(0xb2)` (`wlc+0x3c4`, read by `wlc_init` only in that case): for
  entries 4..11 only, the transmit MIC key at `SHM(M + 16i)` (8 bytes) and
  the receive MIC key at `SHM(M + 16i + 8)`. For a station (infrastructure)
  the transmit MIC key is bytes 24..31 of the 32-byte TKIP key and the
  receive MIC key bytes 16..23 (other modes: transmit 16..23). (Observed
  with the capability bit and M forced: `SHM(M + 0x40..0x46)` = key bytes
  24..31, `SHM(M + 0x48..0x4e)` = bytes 16..23.) Whether the card sets the
  capability bit is unknown (open question 3).
* **Host flag 1 bit 14** (0x4000, both bands): set when the configuration
  uses no WPA (`bsscfg+0x9a` = 0), a WEP key (cipher 1 or 3) is among the
  default keys 0..3 and no key entry from 54 on exists (none can on this
  core); else cleared. Written after a default-key entry changes. By its use
  it lets the microcode decrypt unicast frames with the default (WEP) keys.
  (Observed: WEP-104 in entry 0 -> host-flag word `SHM(0x5e)` = 0x4100;
  CCMP and TKIP keys -> bit 14 cleared.)

**TKIP group keys are not enabled in the hardware.** For a TKIP key without
an address (a group key) the security index word gets cipher 0 although
the key bytes are written; the host decrypts such frames itself (2.5). The
same happens for every key when software encryption is selected (the
global byte `wlc+0x340`, or bit 3 of `bsscfg+0x90`): `wlc_key_hw_init`
then writes the entry as empty.

### 2.2 Which entry a key gets (station)

`wlc_key_insert(wlc, bsscfg, length, key id, flags, algorithm, data,
address, initial IV, &key)` (read; observed for the five cases of the
Verification):

1. Only lengths 5, 13, 16 and 32 are accepted, and key ids 0..3. The
   length decides the cipher: 5 WEP-40 (IV 4, ICV 4 bytes), 13 WEP-104 (IV
   4, ICV 4), 32 TKIP (IV 8, ICV 4; MIC 8 more), 16 AES-CCM (IV 8, MIC 8)
   - or one of two OCB modes when the algorithm argument is 5 or 6 (not
   described). No other cipher exists in the object (2.6).
2. **Group key** (address 00:00:00:00:00:00) of the station's configuration:
   entry = key id (0..3). If `flags` bit 1 is set, it becomes the
   configuration's transmit key (`bsscfg+0xa8` = key id, the key's flag
   word bit 1).
3. **Pairwise key** (the AP's address): the station entry of that address
   is found or created; its key keeps the entry it had, else
   `sub_07c1c1` takes the first free entry from 4 on (4..53 for an
   infrastructure configuration, 4..19 otherwise). Observed: a TKIP key
   replacing a CCMP key of the same peer reused entry 4.
4. The software key record (0xbc bytes) is cleared and filled (address, key
   id, cipher, IV/ICV lengths, the key bytes), then `wlc_key_iv_init`
   (only while up): TKIP and CCMP: transmit packet number 0 (or the
   initial IV given), the receive counters likewise; TKIP also the receive
   IV32 of the phase-1 block; WEP: a random 24-bit transmit IV from two
   reads of `D11(0x65a)` (observed) - by its use the core's random number
   register.
5. TKIP: the two phase-1 keys are computed by the host (`tkhash_phase1`):
   transmit with the own address and the transmit IV32, receive with the
   peer's address (the BSSID for a group key) and the receive IV32.
6. `wlc_key_update(wlc, entry, bsscfg)`: for entries < 54 with hardware
   encryption, while up: **`wlc_key_hw_init`** (2.3).
7. A group key marks the keys as in place (`bsscfg+0x9d` = 1); if that
   makes power save allowed (1.2), `wlc_set_pmstate(bsscfg, pm+0x08)`
   (section 1: power save starts only after the group key of the 4-way
   handshake; observed: no access from it in the run because power save
   was not enabled).

`wlc_key_delete(wlc, bsscfg, key)`: the configuration's or station's
reference is dropped, the keys-in-place byte cleared (power save
re-evaluated as in step 7), the record cleared (an entry >= 4 freed), then
`wlc_key_update`: the hardware entry is written as empty (2.3). The keys of
a station entry are deleted when it is freed (disassociation).

### 2.3 Writing an entry: wlc_key_hw_init(wlc, i, bsscfg)

In this order (observed for entries 0, 1, 2 and 4; read for the rest):

1. The record k at entry i is taken as absent if hardware encryption is off
   (2.1). c = k's hardware cipher, or 0 if k is absent or is a TKIP key
   without an address.
2. Entries 4..67 (bounded by `pub+0xd8` + 4): read `AMT[i - 4]`; if its
   attribute has bit 15 set, write the entry back with the address
   00:00:00:00:00:00 and the attribute without bit 15; otherwise clear it
   (`wlc_set_rcmta(.., zero)`: address and attribute 0). (Observed: a fresh
   entry was written 0/0; an entry holding 0x8004 was written 0 /
   0x00040000, i.e. attribute 0x0004 kept.)
3. `SHM(0x5e0 + 2i)` = c | i << 4 (for i < 68).
4. Key bytes: absent or cipher byte 0 -> 16 zero bytes at `SHM(B + 16i)`;
   else the first 16 key bytes. For TKIP with the capability bit and an
   entry 4..11 (and hardware encryption): the two MIC keys (2.1).
5. TKIP, i >= 4: the seven phase-1 words at `SHM(0x2e0 + 14i)`.
6. i >= 4 and k present: `AMT[i - 4]` = k's address with attribute 0x8004
   OR-ed (read first), and the beamforming index update when beamforming
   is present (wlc-hw 1.4 step 9.3).
7. i < 4: host flag 1 bit 14 as in 2.1.

The MAC is not suspended around these writes. `wlc_key_hw_init_all` at
every init clears the table and the index block and writes every configured
key again (wlc-hw 1.4 step 9).

### 2.4 Transmit: who builds what

For a frame sent with a key (read; IV formats and the header bytes
observed by direct calls of `wlc_key_iv_update` and `sub_026763`):

1. **The host** sets the Protected bit (frame control 0x4000), leaves
   `iv length` bytes after the 802.11 header (and QoS control) and writes
   the IV there (`wlc_key_iv_update`, advancing the counter once per MPDU):
   * CCMP: the 48-bit packet number + 1; IV = PN0, PN1, 0, key id << 6 |
     0x20, PN2, PN3, PN4, PN5 (observed: PN 1 and 2 -> `01 00 00 20 00 00 00
     00`, `02 00 00 20 ...`; key id 1 -> byte 3 = 0x60).
   * TKIP: the 48-bit TSC + 1 (on a wrap of the low 16 bits the high 32
     bits advance and the transmit phase-1 key is recomputed); phase 2 is
     computed by the host; IV = TSC1, (TSC1 | 0x20) & 0x7f, TSC0, key id <<
     6 | 0x20, TSC2..TSC5 (observed: TSC 1 -> `00 20 01 20 00 00 00 00`).
   * WEP: a 24-bit IV counter that skips the weak-IV classes, byte 3 = key
     id << 6.
   The hardware never assigns or advances a packet number.
2. **The transmit header** (frames 3.3), when the key's entry is < 54 and
   hardware encryption is on: byte 0x64 = hardware cipher << 4, byte 0x65 =
   the entry number; `FrameLen` additionally counts the ICV/MIC length (4
   WEP/TKIP, 8 CCMP). TKIP only: bytes 0x6c..0x75 = the transmit phase-1
   key (five 16-bit words, little-endian) for the IV32 of this frame, bytes
   0x76..0x78 = IV bytes 0..2 (observed: `e0 f7 79 d7 85 52 b5 9b b2 fb` and
   `00 20 02` for TSC 2). CCMP and WEP: nothing more (observed: the header
   bytes stay 0).
3. **The core** encrypts and appends the ICV or MIC; the host neither
   reserves nor writes them.
4. **TKIP Michael MIC**: computed by the core (MacTxControlLow bit 0x2000,
   `FrameLen` + 8) only if the MAC revision is > 12, the capability bit 31
   is set, the key is in entry 4..11 and the MSDU is sent in one fragment;
   otherwise the host appends it to the MSDU before fragmenting
   (`wlc_dofrag_tkip`).
5. Keys without a hardware entry (entry >= 54, software encryption): the
   host encrypts the whole frame and leaves header bytes 0x64.. at 0.

For A-MPDU and retransmitted headers the object refreshes the IV and the
TKIP header bytes in the stored header (`sub_03d445`).

### 2.5 Receive: what the core reports and the host does

Receive header (frames 1.3): `RxStatus1` bit 3 (0x08) the core decrypted
the frame, bit 4 (0x10) the ICV/MIC check failed, bits 5..10 the key entry;
`RxStatus2` bit 3 (0x08) the core checked the TKIP Michael MIC, bit 4
(0x10) that MIC was wrong (read: used by the host's MIC check for MAC
revisions >= 13, for frames that are not fragments). The frame keeps its IV
and its ICV/MIC.

The host, for a frame with the Protected bit (read):

1. Key: a unicast frame uses the AP's pairwise key if there is one, else
   (and for group frames) the default key of the key id in IV byte 3 bits
   6..7. No key -> dropped.
2. TKIP and CCMP: the replay check against the key's counter for the
   frame's priority class (the PN as in 2.4: low 16 bits from IV bytes 0..1
   for CCMP, bytes 2 and 0 for TKIP; high 32 bits from bytes 4..7). A PN not
   above the last accepted one -> dropped.
3. Software decryption (WEP and TKIP with RC4 and a CRC check, which sets
   or clears bit 4 itself) when the core did not decrypt (bit 3 clear),
   when software encryption is on, or for a TKIP group key (always, 2.1).
   A CCMP frame the core did not decrypt is dropped (no software CCMP in
   the object).
4. Bits 3 and 4 both set -> decryption error, dropped.
5. The IV (`iv length` bytes after the header) and the ICV/MIC (`icv
   length` at the end) are removed; the PN is kept and, after reordering,
   becomes the key's counter (`wlc_wsec_rxiv_update`).
6. TKIP: the Michael MIC (the last 8 bytes now) is taken from `RxStatus2`
   when the core checked it, else computed by the host; removed. A failure
   starts the countermeasures (software). A pass may update the receive
   phase-1 block (section 3).

For mac80211: `RX_FLAG_DECRYPTED` when bit 3 is set and bit 4 clear, IV and
MIC left in the frame (mac80211 strips them and checks the PN itself); bit
4 set: drop (or `RX_FLAG_MMIC_ERROR` for a TKIP frame with `RxStatus2` bits
3 and 4). A protected frame with bit 3 clear goes up without the flag and
mac80211 decrypts it in software (it keeps every key).

### 2.6 Management frame protection

The object has no hardware support for it: `wlc_key_insert` accepts no BIP
(AES-CMAC) or GCMP key, there is no hardware cipher value for them, and the
transmit path builds key fields only for data frames. BIP must be done by
mac80211 in software (return `-EOPNOTSUPP` from `set_key` for
`WLAN_CIPHER_SUITE_AES_CMAC`), and robust management frames under a CCMP
pairwise key are best left to software as well
(`IEEE80211_KEY_FLAG_SW_MGMT_TX`), since whether the microcode encrypts or
decrypts management frames was not seen in the host code (open question 4).

### 2.7 For a mac80211 driver

`set_key(SET_KEY)`:

* CCMP pairwise: entry i from 4 on, kept per station; key bytes at `SHM(B
  + 16i)`, `SHM(0x5e0 + 2i)` = 5 | i << 4, `AMT[i - 4]` = peer address with
  attribute 0x0004 (bit 15 = OR); `hw_key_idx` = i;
  `IEEE80211_KEY_FLAG_GENERATE_IV` (the host builds the CCMP header, 2.4).
* CCMP group: entry = key index (1..3), `SHM(0x5e0 + 2i)` = 5 | i << 4, no
  AMT entry.
* TKIP pairwise: as CCMP with cipher 2, plus the receive phase-1 block
  (from `update_tkip_key`, section 3) and, per frame, the transmit phase-1
  key and IV bytes 0..2 in the transmit header
  (`ieee80211_get_tkip_p1k`); `IEEE80211_KEY_FLAG_GENERATE_MMIC` unless the
  core computes the MIC (2.4 step 4).
* TKIP group keys and WEP: software (`-EOPNOTSUPP`) is the simple choice;
  the object itself decrypts TKIP group frames in software.

`set_key(DISABLE_KEY)`: the entry written as empty (2.3: index word i << 4,
16 zero bytes, `AMT[i - 4]` cleared). Order of the writes as in 2.3; no MAC
suspend. After a core re-init all keys are written again.

Proposed open entry points (not yet in `open/`): `bcm4360_core_set_key(core,
idx, cipher, key, len, addr)`, `bcm4360_core_clear_key(core, idx)`,
`bcm4360_core_set_tkip_rx(core, idx, iv32, p1k[5])`; the transmit header
fields are filled by `bcm4360_core_tx` from a per-frame key argument.

## 3. Periodic and event work for power save and keys

Keys (read):

* **TKIP receive IV32 advance.** The core decrypts TKIP with the phase-1
  key the host put into the block of 2.1, valid for one IV32. After a
  frame whose IV16 (TSC0/TSC1) is 0xffff passes the MIC check, the host
  computes the phase-1 key for IV32 + 1 (peer address, temporal key),
  stores it with that IV32 and the frame's priority, and calls
  `wlc_key_update` - which rewrites the **whole** entry (2.3 steps 2..6,
  including the AMT clear and rewrite), not only the seven words. Likewise
  when the host had to decrypt a frame in software because its IV32 was
  newer than the stored one and the MIC then passed: the newly computed
  phase-1 key and IV32 are stored and written. Frames that arrive under a
  newer IV32 before that update fail in the core (bit 4) and are dropped.
  mac80211 equivalent: the `update_tkip_key(hw, vif, conf, sta, iv32,
  phase1key)` callback -> write `SHM(0x2e0 + 14i)` .. `+12` (five phase-1
  words, IV32 low, IV32 high).
* **TKIP transmit phase-1.** Recomputed by the host when the transmit TSC's
  low 16 bits wrap (2.4); it travels in every transmit header, so no shared
  memory is written.
* **Packet numbers.** Kept by the host only, in both directions; a new key
  starts at 0 (or the given initial value). The hardware has no PN state.
* **Rekeying.** A new group key goes into the entry of its key id; the old
  entry stays until it is replaced or deleted, so frames under the old key
  id still decrypt. A new pairwise key reuses the peer's entry (written in
  place, 2.3). Each change re-evaluates power save through the keys-in-place
  byte (2.2 step 7, `wlc_key_delete`).
* **Init / re-init** (up, fatal error): `wlc_key_hw_init_all` (wlc-hw 1.4
  step 9) clears the table, the index block and `AMT[0..49]`, then writes
  every configured key. A change of the software-encryption bit of the
  security word while up (`wlc_wsec`) rewrites all entries, and re-applies
  the PM state.

Power save (section 1 for the details):

* every transmit status: `wlc_update_pmstate` (PM pending completion);
* every beacon: TIM, awake-for-beacon, TBTT check and re-alignment;
* every watchdog tick (1 s): the null frame again while PM is pending and
  allowed; beacon-loss accounting with WAKE held from half the timeout on;
* `PM_FAST`: leave on transmit or TIM, the PM2 timer (`D11(0x18)`,
  `MI_TO`) returns to power save after `pm+0x34` ms (200) without data;
* association, channel change: WAKE held until the next beacon.

## Verification

Method: direct calls on Broadcom's object alone (`hybrid.Run()` default
build) after attach, `mpc 0` and up, recording every access of the card
model with the call chain; the configuration was made associated by
writing its bytes (`+0x0c`, `+0x23`, AID `+0x112` = 0xc001, BSSID
00:11:22:33:44:55), and for the key runs the key table base `wlc+0x3c0` and
the MIC base `wlc+0x3c4` were set to 0x1000 and 0x1800 and capability bit
31 forced, because the model's shared memory reads 0 (no microcode).

| run | calls | statements checked |
|---|---|---|
| `verify.py` -> `verify-out.txt` | `wlc_key_insert` CCMP pairwise (AP address), CCMP group id 1 (transmit key), TKIP pairwise, TKIP group id 2, WEP-104 id 0; `wlc_key_iv_update` x 2 and `sub_026763` on the TKIP key; `wlc_set_pmstate` 1/0 (`PM_FAST`, `PM_MAX`), `wlc_update_pmstate` 0x02/0x82, `wlc_tim_process` (TIM bit set, clear) | 2.1 (entry layout, index words incl. 0x20 for the TKIP group key, AMT binding 0x8004 and the pre-clear, phase-1 block offsets, MIC key offsets and order, host flag 1 bit 14 with WEP), 2.2 steps 1-6 (entry numbers, reuse of entry 4, `D11(0x65a)` reads), 2.3 order, 2.4 TKIP IV and header bytes 0x6c..0x78; 1.3, 1.4 (register values, order, null frame 0x0148 on FIFO 1, `SHM(0x40)` read, `D11(0x120)` read after the post), 1.5 (0x02 keeps PM pending, 0x82 completes it and drops WAKE), 1.6 step 1 for `PM_FAST` (leave, timer `D11(0x18)` = 0x32000) |
| `verify2.py` -> `verify2-out.txt` | CCMP keys, `wlc_key_iv_update` x 2 per key, `sub_026763`; `PM_MAX` enter/complete; TIM with no station entry | 2.4 CCMP IV format (key id 0 and 1), no header bytes for CCMP; 1.4/1.5 for `PM_MAX` |

Read from the code only: everything about the microcode's behaviour
(dozing, waking, the PM bit in frames, decryption itself); the PS-Poll
frame and its chain, U-APSD, the PM2 timer handler, the watchdog parts,
beacon processing steps 2-4 of 1.6, `sub_025fb0`; the transmit and
receive paths of 2.4 steps 1, 4, 5 and 2.5 (no frame was sent or received
with a key); section 3; the transmit header bytes 0x64/0x65 (written by
`sub_031322`, not run with a key).

## Open questions

1. **The PM bit.** Does the microcode set (or clear) frame control bit 12
   in every transmitted frame according to HPS, overriding the host? The
   object relies on it (it never sets the bit); mac80211's
   `PS_NULLFUNC_STACK` would set it itself while HPS is still clear. A card
   capture of a null frame sent with HPS clear but PM set in the frame
   would settle it.
2. **What the microcode does with `SHM(0xb6)` = 0 and `SHM(0x4c)`** (wake
   for every beacon or every DTIM; time awake after a DTIM beacon for
   group traffic). Only the host side is described.
3. **MAC capabilities bit 31** (hardware TKIP MIC) and the real values of
   `SHM(0x56)` (key table base) and `SHM(0xb2)` (MIC key base) on the card:
   the model reads 0. A read of `D11(0x15c)` and of the two words on the
   card would settle it (wlc-hw open question 1 asks the same for
   `SHM(0x56)`).
4. **Management frames.** Whether the core encrypts or decrypts protected
   unicast management frames with the pairwise key (the host code has no
   key path for them); mac80211 can do them in software either way.
5. **Receive counters per priority.** The software key record has four
   receive counters; the index the host uses for a frame (`+0x80` of the
   receive state) was not followed to the TID/AC mapping. Irrelevant for
   mac80211, which keeps its own per-TID counters.
6. **The meaning of the AMT attribute bits** beyond the use: 0x0004 for key
   peers, 0x0002/0x0006 for the BSSID, 0x0008 for the own address, bit 15
   kept in the entry (bmac-init B5). What the microcode does when a pairwise
   key's peer is also `AMT[62]` (the BSSID) - both entries hold the AP's
   address - was not seen.
7. **Hardware cipher 4** (CCMP variant chosen by station flag bit 2 of byte
   +0x09) - its meaning was not followed.

## Corrections to other documents

* wlc-hw section 3 step 11 calls the watchdog's null frame a keep-alive:
  it is sent only while a change to power save is pending (`pm+0x0a`) and
  power save is allowed - a retry of the PM announcement (1.5).
* wlc-hw 5.6, `wlc_ps_allowed`: "the AID word `bsscfg+0x9a`" is the WPA
  authentication mode, "the low 3 bits of `bsscfg+0x90`" the enabled
  ciphers, and `bsscfg+0x9d` the keys-in-place byte; `bsscfg+0x23` is "the
  DTIM period is programmed" (1.1, 1.2).
* frames 1.3: `RxStatus2` bits 3 and 4 are the TKIP MIC check of the core
  (2.5).
