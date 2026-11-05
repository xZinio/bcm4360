# Task: power save, hardware keys and the BlockAckReq

Milestones 4 and 5 of [07-open-driver.md](../07-open-driver.md).
Specification: [spec/ps-keys.md](../spec/ps-keys.md) (read its corrections
to frames.md 1.3 and wlc-hw 3/5.6), [spec/ampdu.md](../spec/ampdu.md) 2.4
for the BlockAckReq.

1. **Hardware keys** (ps-keys.md 2, 3): core functions to install and
   remove a key (`open/include/bcm4360/core.h`: e.g.
   `bcm4360_core_set_key(c, idx_or_peer, cipher, key, keylen, ...)`,
   `_del_key`, and the TKIP receive phase-1 update); the key table, the
   security index block, the AMT binding; the transmit header's key bytes
   (0x64/0x65, TKIP 0x6c..0x78) in `open/core/tx.c` for frames mac80211
   marks with `info->control.hw_key`; the receive bits (RxStatus1 3/4,
   RxStatus2 3/4) in `open/core/rx.c` -> `BCM4360_RX_DECRYPTED` and a new
   flag for ICV/MIC failure. Linux (`drv/mac80211.c`): `.set_key` for CCMP
   and TKIP pairwise/group, WEP; -EOPNOTSUPP for everything the hardware
   does not do (BIP/GCMP/MFP: mac80211 does them in software), the IV
   generation left to mac80211 (`IEEE80211_KEY_FLAG_GENERATE_IV` as the spec
   says the host builds the IV), the receive status flags so mac80211 strips
   IV/ICV/MIC and checks replays (`RX_FLAG_DECRYPTED`, `RX_FLAG_IV_STRIPPED`
   only if stripped, `RX_FLAG_MMIC_ERROR`), `.update_tkip_key`.
2. **Power save** (ps-keys.md 1): the HPS and WAKE control, the null-data
   PM announcement and its completion through the transmit status, the
   watchdog resend, PM_FAST with the PM2 timer, beacon tracking (TBTT,
   loss). Map it to mac80211 as ps-keys.md 1.9 proposes:
   `IEEE80211_HW_SUPPORTS_PS` (and `SUPPORTS_DYNAMIC_PS` if the core's
   PM_FAST covers it), `IEEE80211_CONF_PS` in `.config` -> a core call.
   Where ps-keys.md leaves the PM bit question open (its Q1), choose the
   mode that does not depend on it and say which.
3. **BlockAckReq** (ampdu.md 2.4): when an aggregate's status leaves MPDUs
   unacked, send a BlockAckReq with SSN = the last MPDU's sequence number +
   1, as Broadcom does - in Linux with mac80211's `ieee80211_send_bar()`
   from `bcm4360_deliver()` (once per aggregate and TID).

## Test

Extend `tools/re/frames_check.py`: key installation compared with
Broadcom's object where its public entry points allow (ps-keys.md's
Verification shows which functions were called directly), the transmit
header key bytes and receive flags against the spec, the PM transitions
(D11(0x120) values, the null frame, completion on status 0x82) against the
object if reachable through public entry points, else the spec.
`standalone.py check` must stay "pass"; `make open` must link.
