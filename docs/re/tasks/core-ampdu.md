# Task: A-MPDU transmit in the core and mac80211's Block Ack sessions

Milestone 4 of [07-open-driver.md](../07-open-driver.md). Specification:
[spec/ampdu.md](../spec/ampdu.md) (read its corrections to frames.md 3.3 and
4.2 first: they change what `open/core/tx.c` does for aggregated frames).

1. **Core** (`open/core/tx.c`, new `open/core/ampdu.c`, `frames.h`): the
   per-station/TID session state the transmit header needs (ampdu.md 1:
   the MacTxControl bits, the cache bytes 0x66..0x6b, RTS/CTS default, the
   host-assigned sequence numbers, the epoch bit per FIFO and when to flip
   it, the per-FIFO and per-station limits); the aggregate transmit status
   (ampdu.md 2: NCONS, the bitmap by posting order, per-rate sent/acked
   counts, reason 1 = repost unchanged) reported per MPDU through
   `bcm4360_plat_txstatus()` (`struct bcm4360_txstatus.ampdu` set). The
   receive limits of ampdu.md 3 (`SHM(0xba)`, `SHM(0x3c)`) where the
   spec says they are written.
2. **Interface** (`open/include/bcm4360/core.h`): what the platform must
   pass - add to `struct bcm4360_txparams` the fields an aggregated MPDU
   needs (tid, the session's index or the peer's limits, "part of a
   session"), and functions to start/stop a transmit session
   (`bcm4360_core_ampdu_tx_start(c, peer, tid, ssn, buf_size, ...)` /
   `_stop`). Keep the existing fields; say in the header what each new
   one means.
3. **Linux** (`drv/mac80211.c`): `ieee80211_hw_set(ieee,
   AMPDU_AGGREGATION)`, `.ampdu_action` (TX_START: call the core, then
   `ieee80211_start_tx_ba_cb_irqsafe`; TX_OPERATIONAL; TX_STOP_*:
   `ieee80211_stop_tx_ba_cb_irqsafe`; RX_START/RX_STOP: nothing in
   hardware per ampdu.md 3), the txparams for frames with
   `IEEE80211_TX_CTL_AMPDU`, and the per-MPDU status (`info->flags |=
   IEEE80211_TX_STAT_AMPDU` with `ampdu_len`/`ampdu_ack_len` on the
   reported MPDU, as mac80211's documentation says).

## Test

`tools/re/frames_check.py` (extend it): the header bytes of aggregated
MPDUs (cache bytes, control bits, epoch flips in the situations ampdu.md
lists) against the spec's values, and aggregate status words (with holes,
reason 1) decoded as the spec says. Where you can drive Broadcom's object
through public entry points to the same result, compare with it; else
check against the spec and say so. `standalone.py check` must stay
"pass" and `make open` must link.
