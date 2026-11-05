# Questions of the implementer: task `core-env` (open/core/)

Result: `standalone.py undefined` 0; `bcm4360_open.ko` links (modpost: nothing
undefined). `standalone.py check`: the whole session runs (no emulator error);
every step differs, see "Where check differs". Changes outside open/core/:
`hw.h` (field `owner` at the end of struct bcm4360_hw), `standalone.py`
(hw buffer 64 -> 128 bytes, for `owner`).

## Stubs (marked `TODO` / `wlc-hw:` in the code)

* wlc-hw: the rest of wlc_attach (stf defaults in the state at wlc+0x550,
  modules, country, channel manager), wlc_led_init, wlc_init's SHM reads, keys,
  BSSID, rate sets, sub_03a14c, probe-request enable, LEDs at up/down, scan
  and association abort at down; the watchdog runs only the PHY's
  (cca/MAC parts missing); `bcm4360_mac_update_phy_mode`, `_noise_cb`,
  `_btc_period_get` (returns 0, 0), `stf_env_txchain_set`, `_txant_set`,
  `_valid_rate` (true), beacon/txc functions, `core_set_bss/_filter/_edca/
  _scanning/_txpower/_country`.
* The object has an offload state (`wlc+0x750` != NULL): it does
  `si_survive_perst_war(1,0,0)` at up, reads the ARM core (TCM size) at
  attach and the first access of `up` differs. Core: no offload. Needs a spec
  of when/how the offload firmware state is made (wlc-hw).
* si_eci_init / seci_init / gci_init / seci_down / seci_upd / eci_notify_bt /
  btcgpiowar / si_gpiopull: empty (no spec; not reached or no access in the
  session?). si_pmu_chip_init and si_pmu_swreg_init (window switches only):
  not made.
* otp_read_region (SROM image in OTP): returns -1. si_getdevpathintvar and the
  devpath forms of the global NVRAM: always "not found".
* `bcm4360_chan_env_clm_country_def` and `_clm_limits`: no handle of the
  core in their signature (core.h interface question: add a `void *ctx`);
  they return -1. The CLM data is not loaded (no fw name in hw_fw_data list).
* Power limits: `bcm4360_bmac_set_chanspec` is called with `limits` NULL
  (no channel-power spec), so no `tbl.07` writes; the object makes them.
* core.h: `bcm4360_core_tx_headroom` 0, `_tx` -1, `_isr/_dpc` minimal.
* Defaults I guessed: tunables before the override (ntxd/nrxd 256, rxbufsize
  2048, nrxbufpost 32, rxbnd 16; the override sets the real ones for 0x43a0),
  WME enabled at up (mhf 0 bit 0x100), `wl_reset` result 0.

## Where check differs (first differences)

* attach: 782 vs 828 accesses. One more read of CC(0x60c) by the object; at
  the end the object reads PCI config (capability search at 0x0c, 0x04, 0x34,
  0x48) and the ARM CR4 wrapper (tcm size): wlc_attach's offload detection.
* up: no survive_perst_war reset (offload); open writes `D11(0x12c)=0` where
  the object does not (my intrsoff in `wl_reset`/`bmac_init` writes the mask
  even when `HW_INTMASK` is already 0: wlc_intrsoff may skip the write then;
  to be checked in wlc-hw.md); the object's `PHY(0x19e)` access pattern and
  `CC(0x28)`, `PCIE(0)`, `CC(0x644)` accesses at hw_down are missing in the
  core's `down` (si_survive_perst_war(0,0x204,0) and res_req_timer_clr are
  called in the object's `hw_down` which needs the offload/`up` state).
* channel steps: the object suspends the MAC and reads SHM around
  `wlc_set_chanspec` (wlc_suspend_mac_and_wait, SHM 0x768..0x76e) and writes
  the power limit tables; the core does neither.
* watchdog: the object reads counters (D11 0x158, 0x20.., PHY) that belong to
  wlc_watchdog's other parts (statistics, cca).
* detach: the object clears CC(0x64/0x68/0x8c) (GPIO/LED deinit) which
  `bmac_detach` does not; wlc_detach's LED/GPIO release.
