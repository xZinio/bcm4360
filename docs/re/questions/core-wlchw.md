# Questions of the implementer: task `core-wlchw` (open/core/wlchw.c, chanpow.c)

Result: `standalone.py undefined` 0, `make open` links. `check`: down, 1, 6, 11, 36, 36/80, 149/80, detach same.

## Remaining differences
* attach (827 vs 828): the object reads CC(0x60c) one more time in the OTP power-up
  (open/chip/pmu.c spinwait vs the object); not in wlc-hw.
* DMA ring addresses (W D11 0x208/0x248/0x288/0x2c8/0x228/0x224 at up, 0x224 at
  watchdog): addresses of the harness' allocator, not behaviour.
* 36/40, 157/40, 165: one table write early in the step (tbl.14 entry 0x33, 48 bit:
  object ...8080 0084 4800 vs open 0000 0084 4800; two bytes 0x80 in the object, 0 in the
  core). Depends on state of the previous step; the container of the previous channel
  (channel-power 1.6) is suspected but its groups look as specified. Not found.
## Guesses
* LEDs (wlc_led.c has no spec): mask 7 and pin 0 off / 1, 2 on at up, all off at down are
  hard-coded from the observed session; no ledbh variables in the model. Needs a spec.
* wlc_bmac_wait_for_wake (A5) has no open entry point; set_wake_ctrl omits it when WAKE is set.
* stf: ss_algosel_auto (stf+0x0d) = 1 and ss_algo_channel = 0xffff are needed to match
  the object (second txbw_update at some channels); not in cca-stf.md "Values".
* wlc+0x718 = 1 at attach (bmac-init B1 step 27: first init skips scratch 9); the offload
  state wlc+0x750 is an empty block (the object has one; its presence drives
  survive_perst_war at up and down).
* wlc_intrsoff skips the write when the saved mask is 0; the restore after wl_reset
  at up is read back when 0, at down it is not written (observed; wlc-hw.md 1.2/4.1 do not say).
* wlc_bmac_btc_period_get order: period reads, D11(0x120)/(0x6c2), then params_get (0x4e);
  untested on 2.4 GHz channels (the session only watches 5 GHz).
* Quiet channels = restricted channels of the CLM country (XW: all of 5 GHz).
* set_bss/edca/filter/scan/macaddr are implemented from the spec text, not checked (no join in session).
* FCSFAIL filter: no hardware bit (Q6). TX suspend while scanning uses FIFO 1.
* wlc_stf_txchain_set (stf env): chain mask stored, phy_stf_chain_set + spatial policy; unspecified.
* OLPC engine: TODO (channel-power 4). Per-chain offsets (2.4) left out as specified.
