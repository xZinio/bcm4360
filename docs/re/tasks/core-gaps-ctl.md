# Task: the audit's control gaps (G7, G4, G2, G3) and the REAL steps

Milestone 5 (parity) of [07-open-driver.md](../07-open-driver.md).
Specification: [spec/coverage.md](../spec/coverage.md) part 2: G7, G4, G2,
G3, and the G9 rows for wlc-hw.md (3 step 11, 5.1, 5.6).

Priority order:

1. **G7, the start of a join**: the core function the Linux side calls when
   mac80211 starts authenticating (mac80211 `mgd_prepare_tx`, and
   `mgd_complete_tx` / the association result for the end): channel check,
   rate lookup init, full calibration (reason 4) when channel or BSS are
   new, `AMT[62]` = BSSID with attribute 0x8002, PHY hold 0x1 until the
   association completes or fails, wake control. Wire it in drv/mac80211.c.
2. **G4, radio switch, rfkill and MPC**: the disable bits, `MI_RFDISABLE`,
   the radio monitor, MPC - mapped to mac80211 as G4 says (rfkill:
   `wiphy_rfkill_set_hw_state` / `.rfkill_poll` if the hardware switch is
   readable; MPC: what the core does when mac80211 reports idle, e.g.
   `IEEE80211_CONF_IDLE`, or decide with reasons that it stays off).
3. **REAL steps in the standalone check**: extend `tools/re/standalone.py`
   (`check --steps`) with the steps of hybrid.py's REAL list that the open
   driver has an equivalent for (radio off/on, ifdown/ifup = core down/up,
   suspend/resume, mpc, wait = timers), comparing the core's accesses with
   the object's as for the standard session; where the open driver does
   something different by design (mac80211 software scans instead of
   Broadcom's scan engine), say so and compare what is comparable (the
   channel sets). Make the comparable steps pass.
4. **G2, LEDs** (activity GPIO 0 blink 80/20 ms, GPIOs 1/2 per band, events
   as G2 lists) in the core.
5. **G3, Bluetooth coexistence policy** (the host-flag rule, the watchdog's
   policy) in the core.
6. **G9 wlc-hw rows**: check that the core follows them; fix what does not.

Files: yours are open/core/wlchw.c, ps.c, core.c, keys.c, env_*.c, new files
for G2/G3/G4/G7, drv/mac80211.c, drv/drv.h, drv/timer.c, tools/re/standalone.py.
Another implementer works at the same time in open/core/tx.c, frames.c,
frames.h, ampdu.c: do not edit those. drv/main.c is the coordinator's
(suspend/resume). Shared: open/core/core.h and open/include/bcm4360/core.h -
only small additions with the Edit tool (never rewrite the file), re-read
right before.

## Test

`standalone.py check` (standard steps) must stay "pass", the new REAL steps
pass where comparable; `frames_check.py` stays "same"; `standalone.py
undefined` 0; `make open` links without warnings. Kernel side: read the
mac80211.h comments of every callback/helper you use (context, locks, may it
sleep, what mac80211 writes), never hand mac80211 const or shared data, no
sleeping calls under dev->lock (spinlock, BH off), error paths free what
they allocated.
