# Task: the core's frame path (`open/core/frames.c`)

Milestones 2 and 3 of [07-open-driver.md](../07-open-driver.md): receive
and transmit. The specification is [spec/frames.md](../spec/frames.md)
(receive, interrupts, transmit, transmit status), with
[spec/bmac-data.md](../spec/bmac-data.md) for the DMA engine (open:
`open/mac/dma.c`, `open/include/bcm4360/dma.h`) and the PHY's receive
functions ([spec/acphy-api-rx.md](../spec/acphy-api-rx.md), open in
`open/phy/`, with the correction frames.md records).

Implement, in `open/core/frames.c`, `frames.h` (state: `struct
core_frames`, embedded in the core as `fr`) and, if you like, new files
`open/core/rx.c`, `tx.c`, `intr.c`:

1. **Interrupts** (frames.md 2): `bcm4360_core_isr()` (interrupt context:
   read, acknowledge, mask; true if there is work), `bcm4360_core_dpc()`
   (receive, transmit status, the other bits the default mask enables,
   fatal errors -> `bcm4360_plat_fatal()`; unmask when done or return true
   when the budget ran out), `_intrson()`, `_intrsoff()`.
2. **Receive** (frames.md 1): FIFO 0 refill, the receive header, the frame
   handed to `bcm4360_plat_rx()` with its data starting at the 802.11
   header (pad and PLCP removed; the FCS stays, the platform will tell
   mac80211 the frames include it) and `struct bcm4360_rxstatus` filled
   per frames.md 1.8; frames the spec drops are freed (`osl_pktfree`).
3. **Transmit** (frames.md 3): `bcm4360_core_tx()` builds the prefix and
   the transmit header in the packet's headroom from `struct
   bcm4360_txparams` (open/include/bcm4360/core.h) and the frame, posts it
   to the FIFO's DMA engine; `_tx_headroom()` returns what it needs; the
   return values of core.h (0, 1 when the FIFO is now full, -1).
4. **Transmit status** (frames.md 4): read, match, reclaim from the DMA
   ring, `struct bcm4360_txstatus` to `bcm4360_plat_txstatus()`, and
   `bcm4360_plat_tx_wake()` when a stopped FIFO has room again.

Where frames.md leaves something open (its Open questions), take the most
plausible reading, mark it, and list it in
`docs/re/questions/core-frames.md`. The power field of PHY control word 1
(`wlc_stf_get_pwrperrate`, not specified yet): use 0 and mark it.

## Test

A comparison harness `tools/re/frames_check.py` (yours to write, a test
tool like `standalone.py`, ISC): on the same card model, (a) hand both the
object and the core the same receive buffers (receive header + frame in
FIFO 0's posted buffers, as frames.md's Verification describes) and
compare what reaches the OS (the frame bytes and the decoded status); (b)
transmit the same frames with the same rates through both and compare the
bytes posted to the DMA ring (prefix, header, frame); (c) feed the same
transmit-status words to both and compare the result. You may use the
public entry points of Broadcom's object in the emulator as black-box
references (calling them, reading what they write), as ab.py and
standalone.py do; you do not read its code.

Not yours: `open/core/core.c`, `core.h` (another implementer changes them
now from wlc-hw.md; if you need something there, write it in your
questions and use what is there).
