# Questions of the implementer: task `core-ampdu` (open/core/ampdu.c, tx.c, drv/mac80211.c)

Result: `frames_check.py` (receive, transmit, status, ampdu): same; `standalone.py check`: pass;
`standalone.py undefined`: 0; `make open` links without warnings. Everything in `ampdu` is checked
against the spec's values only (the object has no public entry that makes it aggregate).
Outside the task's files: `drv/drv.h` (`peer_map`), `frames_check.py` (`standalone.window_free`
no longer exists; replaced by `standalone.comparable(..., [])`).

## Guesses and gaps

1. Protection rate (ampdu.md 1.4 step 3): the observed `RtsCtsControl` is 0x0925 (protection rate
   code 9) but the core uses the 6 Mb/s OFDM code 0xb it uses for all RTS (frames.md 3.7, "basic rate
   policy"). The test masks bits 8..11. Which rate is `wlc_rspec_to_rts_rspec` of an HT/VHT entry?
2. Sequence check (1.7): a stale MPDU (d > 2048) is not dropped; it goes out with an epoch flip and
   `next_seq` unchanged (mac80211 does not resend, and a silent drop would leak the skb's status).
3. The epoch of a frame that is not aggregated: written only when it flipped the FIFO's epoch
   (1.6 "else the prefix keeps 0"); the FIFO's epoch bit itself can be 1 while the frame carries 0.
   Whether the microcode wants the FIFO's current bit on every frame is unknown.
4. `FbwInfo` (1.6) is 0 in our headers (frames.md 3.3 leaves the field unset), so it never differs.
   The kind is the encoding of the first rate entry; for the PLCP comparison the bytes of the entry
   as we build them (they contain the bandwidth/GI/LDPC/STBC bits; HT bytes 0 and 3).
5. Maximum duration: constant 0x1528 & 0xfff0 (ampdu+0x2c); spacing = max(peer density, 5); the
   peer's maximum A-MPDU length (peer HT cap) is unused, as the spec says. Whether the microcode
   respects a 5.4 ms limit at low rates (HT MCS 0) is not visible; mac80211 does not know it.
6. Aggregate status (2.6): mac80211 wants one status with `ampdu_len`/`ampdu_ack_len`; the core reports
   every MPDU, those values on the first, `STAT_AMPDU` on all. Rate/tries of the first: 1 try at rate
   0, and 1 more at rate 1 when `w2` byte 2 (sent at fallback) is non-zero (which fallback entry is
   not known; entries 1..3 all map to index 1). `w0` bit 15 and the PHY error are not used (2.2).
7. Reason 1 (suppressed, power save): re-post unchanged to the same FIFO (behind newer frames, unlike
   Broadcom's precedence). At most 64 consecutive re-posts, then the MPDUs are reported lost: a hot
   loop if the receiver sleeps long. Is the "PM indication" (w0 bit 3) to be used to hold them?
   Reasons 2..6 are reported as not acknowledged (no re-send).
8. A status with NCONS > 64 or with fewer completed frames than NCONS is fatal.
9. No BlockAckReq is sent (2.4): after a hole the receiver's window is moved by mac80211 (it sends BAR
   on `IEEE80211_TX_STAT_AMPDU_NO_BACK` only; the driver never sets it). If the peer's reorder
   buffer stalls after losses, the BAR policy (2.4) must be added; the control frame path (FIFO 3) is
   available.
10. Host flag 3 bit 6 and the `TPL(0x48)` own address are already written by wlchw.c (steps 11, 13),
    `SHM(0xba)`, `SHM(0x3c)` by step 21; no change was needed there.
11. Peer parameters (`max_mpdus`, `density`) are per peer, set by every `tx_start` (same values
    for all TIDs of a peer). The window is per session, from `tx_operational`.
