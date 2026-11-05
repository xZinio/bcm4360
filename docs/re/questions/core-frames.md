# Questions of the implementer: task `core-frames` (open/core/frames.c, rx.c, tx.c)

Result: `tools/re/frames_check.py` (receive, transmit, status): same.
`standalone.py undefined` 0; `make open` links without warnings.
Changes outside the frame files: `hw.h` (prototype `osl_pktpush`, as in
Broadcom's linux_osl.c), `drv/mac80211.c` (`RX_INCLUDES_FCS`).

## For core.c / core.h (not mine)

* `bcm4360_core_set_filter()` must store its argument in `c->fr.rx_filter`
  (rx.c delivers frames with a wrong FCS only if `BCM4360_FILTER_FCSFAIL` is set).
* `c->fr` (txid, stopped[], resched, rate_valid) must be zero after up (clear it with hw_memset).
* `c->chanspec` must be the current chanspec at all times (the transmit header's
  Chanspec field reads it).
* `c->info.rxchain` (receive chain mask) selects the `chain_rssi[]` that are reported.

## Where the spec was incomplete or I had to guess (marked in the code)

1. `wlc_hw_deviceremoved` is read in `wlc_intstatus` AFTER the status read (frames.md
   2.3 step 1 does not say which comes first) and at the start of `wlc_dpc` after the
   `intrsupd` of a rescheduled run; the test shows this order. With the clock on it is
   a read of `D11(0x120)` (bmac-init.md C14), which frames.md 2.3 omits.
2. 64-bit TSF: frames.md 1.2 reads only `D11(0x180)` per batch. The core also reads
   `D11(0x184)` once per batch (for `struct bcm4360_rxstatus.tsf`). This one extra read
   is the only difference in the register accesses and the harness filters it.
3. A-MSDU sub-frames (`RxStatus2` bit 0) are dropped: A-MSDU reception is off (the
   object's `wlc+0x292`). mac80211 would need the sub-frames joined; to do later.
4. `MacTxControlHigh` bit 1 (fixed rate) is set only when `nrates` is 1 (frames.md 3.3
   says "unless the rates came from Broadcom's rate selection"). A frame with a retry
   chain from minstrel_ht is a rate-selection frame; unverified on the card.
5. The power field of `PhyTxControlWord_1` is 0 (task: `wlc_stf_get_pwrperrate`
   not specified); the harness masks bits 3..8.
6. PHY control word 0: the core mask comes from the stf state (`stf+0x17+2*i`, override
   `stf+0x22+i`), `i` = 0 CCK, 1 OFDM, else nsts+1 (capped 5), with nsts = streams, +1
   (HT) or x2 (VHT) with STBC. frames.md does not say how `wlc_stf_txcore_get` treats
   the override; I let it win. Against the object's probe requests (CCK, mask 1 in the
   model) it agrees. frames.md 3.5 lists 0xc0 (mask 3) for CCK/OFDM from another
   model state; the HT/VHT values agree with the spec (mask 3).
7. Protection (3.7, read from the code only): RTS / CTS-to-self set bits 2 / 3 of
   `RtsCtsControl` in every entry; the protection rate is 6 Mb/s OFDM (code 0xb) for
   OFDM/HT/VHT entries and 1 Mb/s CCK (code 0xa) for CCK entries. The real rule is
   `wlc_rspec_to_rts_rspec` with the BSS's basic rates. Untested.
8. `TxRate` for HT/VHT is computed from the standard's tables (symbol time 3.6/4 us); the
   spec's two values (300, 1733) agree. HT MCS 32 gives 12 (6 Mb/s) by assumption.
9. Short preamble: `PhyTxControlWord_0` bit 4 for CCK rates other than 1 Mb/s; the PLCP
   SERVICE byte is not changed. Greenfield, beamforming, sounding: not set.
10. Receive: `RxStatus2` bit 8 is clear for the first MPDU of a VHT A-MPDU (frames.md 1.5
    reading); an HT first MPDU is recognised by `plcp[3]` bit 3. MPDUs with zero PLCP
    take the remembered rate even when no first MPDU was seen yet (then the PLCP-less
    decode: enc 0, rate 0).
11. Non-HT duplicate bandwidth (`PhyRxStatus_5`) is not used: legacy frames report 20 MHz.
12. Transmit status: `acked` is 0 when the suppression reason or the PHY error is
    non-zero even if bit 15 is set (frames.md 4.4: "count as not sent"); `rate_idx`
    is the last rate entry with a non-zero count. Flush (reason 2) frames are handed to
    the platform like the others (the object re-queues them).
13. Flow control: the FIFO counts as full below 4 free descriptors (`bcm4360_core_tx`
    returns 1); `bcm4360_plat_tx_wake` at 32. Arbitrary; not exercised by the harness.
    All AC queues stop together on the Linux side (`ieee80211_stop_queues`), so the wake
    is issued for the FIFO that was stopped.
14. The not-yet-done interrupt bits (frames.md 2.2): `MI_TXSTOP` (scan/quiet callbacks),
    `MI_CCA`, `MI_TBTT`, `MI_TO`, the Bluetooth predictor, and the rfkill decision of
    `MI_RFDISABLE` (only the `D11(0x158)` read is made) do nothing. `MI_GP0` calls
    `bcm4360_plat_fatal()` without the microcode state dump.
15. `bcm4360_core_tx()` with a full ring: `bcm4360_dma_txfast` frees the packet itself on
    failure (bmac-data P23 step 4); `core_tx` checks `txavail` first so that the -1 path
    leaves the packet to the platform, and returns -1 after a failed `txfast` too (then
    the packet is already freed: a platform that frees on -1 would free it twice; with
    `txavail` >= 2 this cannot happen).

## What the harness (frames_check.py) does not cover

* HT/VHT receive and transmit rates come from vectors made by hand from frames.md
  (the decode is checked against the spec's formulas, the frame bytes, host fields and
  RSSI against the object); the object's own monitor-mode decode is not used.
* Hardware keys, A-MPDU, fragments, and the transmit-queue flow control.
