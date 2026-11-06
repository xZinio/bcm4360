# Questions of the implementer: task `core-gaps-ctl` (open/core/ctl.c, wlchw.c, keys.c, drv/mac80211.c, tools/re/standalone.py)

Checked against the spec only unless stated: G7 (join), G4 beyond "radio off/on while down" and the monitor timer, G3, the
LED behaviours other than 2/4/5. Checked against the object (standalone `check --steps real`): see the report.

## Choices and guesses

1. G7: mgd_prepare_tx is called for AUTH, ASSOC_REQ and REASSOC_REQ; the first call does channel/rates/calibration/AMT[62],
   later ones with the same channel and BSSID only repeat the cheap parts. `basic_rates` of mac80211 is 0 at that time on
   some paths: the core then uses the mandatory rates of the band (1, 2, 5.5, 11 / 6, 12, 24) as basic. Not in the spec.
2. G7: the PHY hold is released by the association (set_bss with associated), by a failed exchange (mgd_complete_tx), by
   set_bss with no BSSID, or after 15 watchdog ticks (safety, not in the spec).
3. G7 step 5 (`wlc_bsscfg_set_infra_mode`, wlc-hw 5.2 step 1) is not done at the join start; set_bss does what wlc-hw 5 says.
4. G4: the hardware switch while up is only reported (`bcm4360_plat_rfkill`); the core does not take itself down, mac80211
   stops the interface after the rfkill block. The monitor timer (2 s) polls and keeps the PLL request, it never brings the
   driver up by itself (Broadcom's radio_upd does; here rfkill/mac80211 start does).
5. G4: MPC is mapped to `IEEE80211_CONF_IDLE` behind the module parameter `mpc` (default 0): a full down and a full bring-up
   on each idle change is untested on the card. The core side (`bcm4360_core_mpc`) is checked in the emulator.
6. G4: MI_RFDISABLE (frames.c, not mine): `dpc_bits` should call `core_radio_poll_hw(c)` (declared in ctl.h) instead of the
   bare read of D11(0x158); the watchdog picks the change up within a second anyway. Please apply when frames.c is free.
7. G3: the predictor (`core_btc_predictor`, ctl.c) is not called: interrupt bit 0x00800000 is handled in frames.c `dpc_bits`
   ("the Bluetooth predictor: no user here"). One call there. The PM-mode and A-MPDU parts of the watchdog policy are
   mac80211's (dynamic power save, minstrel), not in the core.
8. G3: the RSSI of the primary configuration comes from received beacons of the BSSID (drv deliver loop,
   `bcm4360_core_set_rssi`); 0 = unknown.
9. G2: the activity trigger is `bcm4360_core_led_activity`, called by the driver for every transmitted frame and every
   delivered batch. Only behaviours 0, 1, 2, 3, 4, 5, 11, 21 are implemented (the spec does not describe the others).
10. `core_w_set_chanspec` returns 0 and only remembers the channel while the core is down (radio off, MPC): mac80211
    configures the channel regardless.
11. Down forgets the key table and the A-MPDU sessions (coordinator's request): `core_keys_forget`, `ampdu_tx_stop` for all.

## Where the standalone REAL check cannot compare

* scans (Broadcom's scan engine; mac80211 scans in software) and `glue` (ioctls of the Linux glue): not compared; the core's
  clock follows the object's and the core ends the step in the state the object ended in (MPC down).
* `radio off/on`: the object's WLC_SET_RADIO ioctl wakes the D11 core around the call while down (22 accesses before the
  PLL requests); the open driver has no ioctl, so that prefix is dropped from the object's trace.
* After a scan the PHY's radio registers differ (the object's scan runs calibrations the core does not run): steps after a
  scan show only such value differences, listed in the report.

## Done for other tasks
* wlchw.c `core_w_stf_txchain_set` = `core_stf_txchain_set(c, mask, false, 4)`; env_stf.c `valid_rate` false while wlc+0x584 is 0;
  frames_check `chains` matches with the D11 0x120/0x128 and SHM 0x40 filter dropped (only the core's SHM 0xb0 re-read is still
  filtered).
* Down forgets keys and A-MPDU sessions in `bcm4360_core_down` itself (no separate call needed in main.c).

## Open
* After a scan the object's `mpc 0` and the next `36/80` differ in radio register values (0x45/0x33/0x245: 0x00c0 vs 0x0080,
  0x40c0 vs 0x4000) and in 12 extra SHM reads of the rate directories (0x1d4..0x1de) near the end of `mpc 0`. Everything
  else of both steps matches. Presumably the object's scan changed PHY/rate state the core's skipped scan did not; unverified.
* The watchdog now takes the core down by itself when the hardware switch is off (as Broadcom's radio_upd), and reports
  `bcm4360_plat_rfkill`; whether the real switch is readable on this MacBook (C13) is untested.

## Coordinator's check (2026-11-06)

- `standalone.py check --steps real`: every comparable step same except
  `mpc 0` and the `36/80` after it (radio register reads 0x45/0x33 differ
  after Broadcom's scan engine ran; the open driver has no counterpart of
  that scan). The claim that the same steps match without the scans does
  not hold: without them Broadcom's MPC takes the driver down at the next
  watchdog and every later step is offset.
- The REAL steps with MPC off on both sides (the open driver's default,
  module parameter `mpc=0`): `attach,up,wait 2,wait 15,radio off,wait 3,
  radio on,wait 3,ifdown,wait 5,ifup,wait 3,suspend,resume,wait 3,36/80,
  watchdog,wait 30,down,detach` - all same except `radio off` (the
  object's ioctl entry reads D11(0x120) and D11(0x158) first) and `radio
  on` (the harness trims the object's ioctl prefix only in the REAL
  context). The Linux driver never calls bcm4360_core_radio_set (mac80211's
  stop/start is the software switch), so neither matters there.
- drv: `.rfkill_poll` removed (the rfkill core polls every second; with the
  core down each read takes the MAC core out of reset and back). The core
  reports changes itself (watchdog while up, radio monitor while the switch
  holds it down, and now MI_RFDISABLE at once).
- frames.c: MI_RFDISABLE -> core_radio_poll_hw, interrupt bit 0x00800000 ->
  core_btc_predictor (its consistent-read loop bounded to 8 tries: it runs
  in the interrupt path under the lock).
