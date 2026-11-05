# Frames through the MAC: receive, interrupts, transmit, transmit status (`frames`)

Status (2026-11-05): complete: sections 1 (receive), 2 (interrupts), 3 (transmit)
and 4 (transmit status), with Verification and Open questions. Scope: the 802.11 MAC core revision 42 of the
BCM4360 with Broadcom's microcode `d11ucode42`, AC-PHY. Other MAC revisions
and PHY types: not described (one line where the object branches on them).

Register notation as in [access.md](access.md): `D11(o)` is a register of
the 802.11 core at byte offset `o` of its register window, `SHM(o)` a word of
the microcode's shared memory. DMA functions are named as in
[bmac-data.md](bmac-data.md) section 1 (D3 slots, P procedures); the open
equivalents are the `bcm4360_dma_*` functions of
`open/include/bcm4360/dma.h`. "Read from the code only" marks statements not
checked by running the object; the Verification section lists what was run.

The consumer is a Linux mac80211 driver (`open/include/bcm4360/core.h`:
`struct bcm4360_rxstatus`, `bcm4360_txparams`, `bcm4360_txstatus`). What
Broadcom's upper layer does with a frame after the hardware-facing steps
(station and BSS look-up, 802.3 conversion, reordering, power-save queues,
rate-selection bookkeeping) is mentioned only where it decides a value read
from or written to the hardware.

## Scope

| function | .text offset | size | name |
|---|---|---|---|
| `wlc_bmac_recv` | 0x067d4b | 434 | original - section 1 |
| `wlc_recv` | 0x04c13e | 4758 | original - section 1 (first part: hardware checks) |
| `wlc_recvdata` | 0x043c6b | 3475 | original - section 1 (header lengths only) |
| `wlc_recvamsdu` | 0x05818a | 372 | original - section 1 (sub-frame types only) |
| `wlc_recv_compute_rspec` | 0x168eac | 164 | original - section 1 |
| `wlc_vht_get_rspec_from_plcp` | 0x168e2b | 129 | original - section 1 |
| `wlc_vht_get_rate_from_plcp` | 0x025415 | 56 | original - section 1 |
| `wlc_phy_get_ofdm_rate_lookup` | 0x0b2598 | 13 | original - section 1 |
| `wlc_recover_tsf32` | 0x0258c9 | 37 | original - section 1 |
| `wlc_recover_tsf64` | 0x025f38 | 120 | original - section 1 |
| `wlc_lq_rssi_pktrxh_cal` | 0x07e22e | 140 | original - section 1 |
| `sub_03ceb2` | 0x03ceb2 | 1349 | `wlc_monitor` (assigned) - section 1 (the monitor-mode decode) |
| `wlc_isr` | 0x07a8e4 | 141 | original - section 2 |
| `sub_07a769` | 0x07a769 | 379 | `wlc_intstatus` (assigned) - section 2 |
| `wlc_intrsoff` | 0x07a6b5 | 107 | original - section 2 |
| `wlc_intrson` | 0x07a720 | 73 | original - section 2 |
| `wlc_intrsrestore` | 0x07a684 | 49 | original - section 2 |
| `wlc_intrsupd` | 0x07a971 | 41 | original - section 2 |
| `wlc_dpc` | 0x07a99a | 618 | original - section 2 |
| `wlc_bmac_set_defmacintmask` | 0x060222 | 24 | original - section 2 |
| `wlc_recover_pkts` | 0x0305c1 | 186 | original - section 2 |
| `wlc_txstop_intr` | 0x0260c1 | 104 | original - section 2 |
| `wlc_bmac_txstatus` | 0x064143 | 1003 | original - section 4 |
| `wlc_dotxstatus` | 0x03b7f4 | 3195 | original - section 4 |
| `wlc_get_txh_info` | 0x030818 | 464 | original - sections 3, 4 |
| `wlc_set_txh_info` | 0x025395 | 128 | original - section 3 |
| `sub_031322` | 0x031322 | 12297 | `wlc_d11hdrs_mac` (assigned) - section 3 (the part for PHY type 11) |
| `wlc_compute_plcp` | 0x028ef6 | 634 | original - section 3 |
| `wlc_acphy_txctl0_calc` | 0x02965b | 206 | original - section 3 |
| `wlc_acphy_txctl1_calc` | 0x03102a | 307 | original - section 3 |
| `wlc_acphy_txctl2_calc` | 0x029170 | 160 | original - section 3 |
| `wlc_stf_txcore_get` | 0x1733a0 | 122 | original - section 3 |
| `wlc_ratespec_nsts` | 0x1684eb | 76 | original - section 3 |
| `wlc_toe_add_hdr` | 0x178fd7 | 106 | original - section 3 |
| `wlc_tso_hdr_length` | 0x178fc0 | 23 | original - section 3 |
| `wlc_pkt_get_vht_hdr` | 0x0307d3 | 69 | original - section 3 |
| `wlc_txfifo` | 0x03e27a | 786 | original - section 3 |
| `wlc_fatal_error` | 0x03b6f0 | 127 | original - section 2 |

## Overview

The core has one receive FIFO (FIFO 0, receive DMA channel at `D11(0x220)`)
and four transmit FIFOs in use (0..3, transmit channels at `D11(0x200)`,
`0x240`, `0x280`, `0x2c0`); bmac-data.md D7 has the ring parameters. The
microcode does the 802.11 timing (acknowledgements, retries, back-off) by
itself; the host:

* keeps receive buffers posted on FIFO 0, and takes each received frame with
  the receive header the core writes in front of it (section 1);
* takes the core's interrupts: the MAC interrupt status `D11(0x128)` with its
  mask `D11(0x12c)`, and the per-FIFO DMA interrupt status registers
  `D11(0x20 + 8*fifo)` (section 2);
* puts a transmit header in front of every frame it posts to a transmit
  FIFO; the header tells the microcode the rates, retries, protection and
  PHY control words (section 3);
* reads one transmit status per frame from the status registers
  `D11(0x170..0x17c)` and matches it to the frame by its frame id (section 4).

---

## 1. Receive

### 1.1 The receive FIFO and its buffers

Parameters of FIFO 0's receive channel (bmac-data.md D7, set by
`wlc_bmac_attach`; the values for this card are fixed by
`wlc_tunables_override`):

| What | Value | Meaning |
|---|---|---|
| ring size `nrxd` | 1024 descriptors | |
| buffers kept posted `nrxpost` | 256 | refills top the ring up to this number |
| buffer size `rxbufsize` | 2048 bytes | each descriptor's byte count (ctrl2) is 2048 |
| receive offset `rxoffset` | 40 bytes | programmed into bits 1..7 of the receive control register `D11(0x220)` (P10): the core writes the frame data from byte 40 of the buffer and its receive status in front of it |
| extra head room | 0 | |
| bound per deferred run (`rxbnd`) | 32 frames | tunable word 14 (`pub+0x38` -> `+0x38`) |
| control flags | 0 | a frame longer than one buffer is dropped (flag 0x04 clear, P37) |

Before posting a buffer the engine writes the 32-bit word 0 at its start
(P36 step 2.4), so that the length field of the status reads 0 until the
core has written it.

Refill policy (read from the code; the watchdog refill seen in the traces,
bmac-run.md R7):

1. after every receive batch, in `wlc_bmac_recv` (step 1.2 below), before
   the frames are processed;
2. once a second in `wlc_bmac_watchdog` (bmac-run.md R7 step 3);
3. after `wlc_recover_pkts` in the deferred handler (section 2, only on a
   path that is never taken on this card).

`dma_rxfill` always rewrites the receive pointer register `D11(0x224)`, also
when nothing new was posted. If no buffer can be allocated at all and the
ring is empty, it returns 1; no caller on this path acts on that result.

### 1.2 The receive procedure of the MAC layer: wlc_bmac_recv (.text+0x067d4b, 434 bytes, name original)

Arguments: `wlc_hw`, the FIFO (0), `bound` (bool: 1 from the deferred
handler), a pointer to a counter of processed frames. Result: 1 when the
bound was reached (more frames may be waiting; the deferred handler then
sets the DMA-interrupt bit 0x8000 in the pending status again, section 2),
else 0.

1. `limit` = the tunable `rxbnd` (32) if `bound`, else unlimited. Take
   frames with `dma_rx` (P37) until it returns none or `limit` frames were
   taken, and chain them in arrival order (through the packets' link
   field). `dma_rx` has already set each packet's length to `40 +
   RxFrameSize` (the 16-bit length the core wrote at the start of the
   buffer, section 1.3), capped at the buffer size; frames longer than one
   buffer were dropped inside `dma_rx` and counted in `rxgiants`.
2. `dma_rxfill` (P36) on FIFO 0.
3. Read the TSF low word once: `D11(0x180)` (32 bit; `wlc_bmac_read_tsf`
   with no high pointer, so `D11(0x184)` is not read). Call it `tsf_now`.
4. For each frame in arrival order, with `rxh` = the start of its buffer
   (the receive header):
   1. store `tsf_now` at `rxh+0x18` (32 bit, the host part of the header,
      1.3);
   2. `wlc_phy_rssi_compute(pi, rxh)` with `pi` of the current band (open:
      `bcm4360_phy_rssi_compute`, [acphy-api-rx.md](acphy-api-rx.md)): it
      reads the PHY status words and `RxStatus2` of the header and writes
      the signal strength to `rxh+0x1c`, the "invalid" flag to `rxh+0x1f`
      and 0 to `rxh+0x24` (1.6);
   3. (MAC revisions below 40 convert `RxChan` into a chanspec here; not
      this core.) (Band PHY types 0, 2, 5 force bit 0 of `PhyRxStatus_0`;
      not the AC-PHY.)
   4. hand the frame to the upper layer: `wlc_recv(wlc, p)` (1.4).
5. Add the number of frames taken to the caller's counter; return
   (taken >= limit).

The receive header is still in front of the frame when `wlc_recv` gets it;
the upper layer strips it (1.4).

### 1.3 The receive header (40 bytes in front of every frame)

The first 24 bytes are written by the core (microcode) with each frame; bytes
24..39 are the host's: the core does not fill them (the frame data starts at
byte 40 because of the receive offset), the host writes the fields it uses
(step 1.2.4). All fields little-endian.

| Offset | Size | Name (brcmsmac style) | Contents for MAC revision 42 |
|---|---|---|---|
| 0x00 | 2 | `RxFrameSize` | number of bytes the core wrote after the 40-byte header: the 2-byte pad if present, the 6-byte PLCP header, the 802.11 frame and its 4-byte FCS (1.4) |
| 0x02 | 2 | (pad) | not used |
| 0x04 | 2 | `PhyRxStatus_0` | bits 0..1 frame type: 0 CCK (DSSS), 1 OFDM (legacy), 2 HT, 3 VHT; bit 4 (0x10) unsupported rate; bit 5 (0x20) antenna / upper sub-band (the monitor decode reports it as the antenna); bit 7 (0x80) short preamble (CCK); bit 8 (0x100) PLCP format violation; bit 9 (0x200) PLCP header check failed; bits 11..13: used by the RSSI code of other PHY types only |
| 0x06 | 2 | `PhyRxStatus_1` | low byte: raw RSSI (other PHY types; for the AC-PHY see `PhyRxStatus_2`); bit 3: CCK flag for the RSSI work-around of PHY revisions 2, 5, 6 (not 0/1); high byte (`rxh+0x07`): signal quality - the monitor decode passes it as is; the upper layer's SNR for one other PHY type converts it as `(b+2)>>2` (CCK) or `(b*0x6e17+0x20000)>>18` (OFDM) |
| 0x08 | 2 | `PhyRxStatus_2` | low nibble: with `rxh+0x07`, tells HT mixed mode from greenfield (1.5); high byte (`rxh+0x09`): raw RSSI of receive core 0 (signed dBm) |
| 0x0a | 2 | `PhyRxStatus_3` | low byte: raw RSSI of core 1; high byte (`rxh+0x0b`): raw RSSI of core 2 |
| 0x0c | 2 | `PhyRxStatus_4` | not read by the object for this core |
| 0x0e | 2 | `PhyRxStatus_5` | bits 0..1: bandwidth signalled in a non-HT duplicate frame (RTS/CTS with dynamic bandwidth): 0 = 20, 1 = 40, 2 = 80, 3 = 160 MHz; bit 2: dynamic-bandwidth flag of that signalling (both used only by the monitor decode, for MAC revisions >= 40) |
| 0x10 | 2 | `RxStatus1` | bit 0 FCS error; bit 1: the frame was a response the core transmitted for (not read); bit 2 (0x04) **pad present**: two pad bytes precede the PLCP header; bit 3 (0x08) the core attempted decryption; bit 4 (0x10) decryption error (ICV/MIC check failed); bits 5..10: key index of the decryption (not read on the paths described here); bit 15: beacon sent (not read) |
| 0x12 | 2 | `RxStatus2` | bit 0 (0x01): the frame is a sub-frame of an A-MSDU the core split up; bits 1..2: its position: 1 first, 0 middle, 2 last, 3 the only one; bit 8 (0x100): the PHY status words (`PhyRxStatus_*`) are valid for this frame (clear for MPDUs of an A-MPDU that carry no PHY status) |
| 0x14 | 2 | `RxTSFTime` | low 16 bits of the TSF at the start of the frame |
| 0x16 | 2 | `RxChan` | the chanspec the frame was received on (MAC revisions >= 40: a chanspec in the format of bmac-run.md "Constants", used without conversion) |
| 0x18 | 4 | `tsf_l` (host) | TSF low word read after the batch was taken (1.2 step 3) |
| 0x1c | 1 | `rssi` (host) | signed dBm from `wlc_phy_rssi_compute`; 0 = unknown |
| 0x1d, 0x1e | 1, 1 | (host) | not written for the AC-PHY |
| 0x1f | 1 | `rssi_invalid` / `do_rssi_ma` (host) | 1 = no valid RSSI (or already sampled), 0 = valid |
| 0x20..0x23 | 4 | `rxpwr[4]` (host) | per-core RSSI, signed dBm: for the AC-PHY written by `wlc_phy_rssi_compute` for each core `i` < the PHY's number of cores (`pi+0x168`) at `0x20+i`, after the per-core corrections (1.6); read by `wlc_lq_rssi_pktrxh_cal` |
| 0x24 | 1 | `rssi_qdb` (host) | quarter-dB remainder; 0 for the AC-PHY |
| 0x25..0x27 | 3 | (host) | not used |

The meanings of bits named above come from how the object uses them (which
check drops the frame, which flag of the monitor status a bit becomes); the
names follow `brcmsmac`'s `d11.h` where the use agrees. Bits not named are not
read by the object for this core. Read from the code; the offsets and the
decode of the rate fields were checked by running the object (Verification
1).

### 1.4 Frame layout in the buffer, and the checks before the upper layer

The bytes after the 40-byte header, in order:

1. 2 pad bytes, only if `RxStatus1` bit 2 is set (the core inserts them to
   align the frame body);
2. the PLCP header, 6 bytes (contents by frame type in 1.5);
3. the 802.11 frame (frame control first);
4. the FCS, 4 bytes, included.

So with `n = RxFrameSize`, the 802.11 frame starts at byte `40 + 6 + (pad ?
2 : 0)` of the buffer and is `n - 6 - 4 - (pad ? 2 : 0)` bytes long without
the FCS. The core does not strip the PLCP header or the FCS.

What `wlc_recv` does before Broadcom's frame handling proper, in this order
(read from the code; the pulls and the drop of frames with PHY errors were
seen in the emulator, Verification 1):

1. Remove the 40 header bytes (`osl_pktpull(40)`; the header stays readable
   through the saved pointer `rxh`).
2. If `RxStatus1` bit 2 (pad): if fewer than 2 bytes are left, drop (counted
   as a runt frame); else remove 2 more bytes.
3. Drop the frame if `PhyRxStatus_0 & 0x310` (unsupported rate, PLCP format
   violation, PLCP header check failure). (For band PHY types 8 and 10 bit
   0x100 is cleared first; not the AC-PHY.)
4. Monitor mode (`wlc+0x288` != 0): a copy of the frame (PLCP header, frame,
   FCS) is passed to the OS layer's `wl_monitor` with a status decoded from
   the header (1.5, 1.7). A-MSDU sub-frames (`RxStatus2` bit 0) are joined
   first: type 1 or 3 starts a new chain (a held one is dropped), 3 is passed
   at once, 1 is held, 0 and 2 are appended to the held chain, 2 passes it.
5. Drop the frame if `RxStatus1` bit 0 (FCS error). (Monitor mode saw it in
   step 4.)
6. Drop the frame if fewer than 8 bytes remain (PLCP header plus frame
   control; counted as a runt).
7. If `RxStatus2` bit 0 (A-MSDU sub-frame): to Broadcom's A-MSDU reassembly
   (`wlc_recvamsdu`) when A-MSDU reception is enabled (`wlc+0x292`), else
   drop. The reassembly expects the types of step 4 in order (first, middle
   ..., last; or one "only" frame); middle sub-frames shorter than 15 bytes
   and last ones shorter than 18 bytes are dropped as runts; a complete
   chain goes to the data path with the first sub-frame's header.
8. Frame type from the frame control at offset 6 (after the PLCP header):
   data frames go to `wlc_recvdata`; for management and control frames the
   PLCP header (6 bytes) is removed and the length set to `len - 10` (minus
   PLCP header and FCS) before Broadcom's handling. `wlc_recvdata` does the
   same: it removes the 6 PLCP bytes and sets the length of the last buffer
   to its length minus 4 (the FCS). (An HT-control field is assumed present,
   4 more header bytes, when the frame type is HT and the frame control's
   order bit (bit 15) is set.)

The rest (`wlc_recv` from step 8 on, `wlc_recvdata`) is Broadcom's upper
layer (address filters, station look-up, software decryption, reordering,
conversion to 802.3, `wl_sendup`), which mac80211 replaces. Its drops are
software policy: frames whose transmitter address is zero or group (data
and management), frames to other addresses when not promiscuous, frames for
no known BSS. Not described further.

### 1.5 Rate and encoding: wlc_recv_compute_rspec (.text+0x168eac, 164 bytes) and the VHT helpers (names original)

Inputs: `rxh` and the 6-byte PLCP header `plcp[0..5]` (the bytes after the
pad). The frame type is `PhyRxStatus_0 & 3`. The result is Broadcom's
32-bit rate specification; its fields here are given as what the mac80211
status needs.

* **Type 0, CCK**: `plcp[0]` is the SIGNAL field (rate in 100 kb/s:
  10, 20, 55, 110); the rate in 500 kb/s units is `plcp[0] / 5` (integer
  division): 2, 4, 11, 22. Short preamble: `PhyRxStatus_0` bit 7 (the
  monitor decode reports "short" when it is set, "long" when clear). 20 MHz.
* **Type 1, OFDM**: rate from the low 3 bits of `plcp[0]` (the L-SIG rate
  field, bits R1..R3), in 500 kb/s units: 0 -> 96 (48 Mb/s), 1 -> 48, 2 ->
  24, 3 -> 12, 4 -> 108, 5 -> 72, 6 -> 36, 7 -> 18 (the table
  `ofdm_rate_lookup`; the standard L-SIG coding). 20 MHz (the object does
  not look at a non-HT duplicate; `PhyRxStatus_5` bits 0..1 tell it, see
  1.3).
* **Type 2, HT**: `plcp[0..2]` is HT-SIG1, `plcp[3..5]` HT-SIG2.
  MCS = `plcp[0] & 0x7f`; bandwidth 40 MHz if `plcp[0]` bit 7, else 20;
  short GI if `plcp[3]` bit 7; LDPC if `plcp[3]` bit 6; STBC if `plcp[3]`
  bits 4..5 are not 0. Spatial streams follow from the MCS (MCS / 8 + 1 for
  MCS 0..31). Mixed mode or greenfield (monitor decode only): mixed if
  `PhyRxStatus_2 & 0xf` != 0 or `rxh+0x07` != 0, else greenfield.
* **Type 3, VHT**: `plcp[0..2]` is VHT-SIG-A1, `plcp[3..5]` VHT-SIG-A2.
  Let `a1 = plcp[0] | plcp[1] << 8`. Bandwidth: `(plcp[0] & 3)`: 0 -> 20,
  1 -> 40, 2 -> 80 (3 -> 160) MHz. STBC: `plcp[0]` bit 3. Spatial streams:
  `((a1 >> 10) & 7) + 1` (the NSTS field plus one), halved (shift right by
  one) when STBC is set. MCS: `plcp[3] >> 4`. Short GI: `plcp[3]` bit 0.
  LDPC: `plcp[3]` bit 2. (Monitor decode, besides: group id `(a1 >> 4) &
  0x3f`, partial AID `((plcp[1] << 8 | plcp[2] << 16) >> 13) & 0x1ff`,
  TXOP-PS-not-allowed `plcp[2]` bit 6, short-GI symbol disambiguation
  `plcp[3]` bit 1, LDPC extra symbol `plcp[3]` bit 3, beamformed `plcp[4]`
  bit 0.)

**A-MPDU (receive).** The core delivers each MPDU of an A-MPDU as a frame of
its own. Only the first MPDU carries the PLCP header; the PLCP bytes of the
following MPDUs are zero. The object recognises them as follows (monitor
decode `sub_03ceb2`, for QoS-data and QoS-null frames, i.e. frame control
subtype 8 or 12; the data path uses the same "PLCP bytes 0..2 all zero" test
to skip rate statistics for such MPDUs):

* HT: HT-SIG2 aggregation bit (`plcp[3]` bit 3) set -> first MPDU of an
  A-MPDU: its rate is remembered (`wlc+0x70c`); PLCP bytes 0..2 zero ->
  a following MPDU: the remembered rate is used.
* VHT: PLCP bytes 0..2 non-zero and `RxStatus2` bit 8 clear -> first MPDU
  of an A-MPDU (rate remembered); non-zero and bit 8 set -> a single
  MPDU (Broadcom marks it "not aggregated"); zero and bit 8 clear -> a
  following MPDU; zero and bit 8 set -> the last MPDU (the one that carries
  the PHY status; no flag); a counter of A-MPDUs (`wlc+0x784`) is
  incremented for each non-zero PLCP. For **every** VHT QoS MPDU the monitor
  decode then reports the remembered rate - also for the single MPDU with
  its own non-zero PLCP, which therefore gets the rate of the previous
  A-MPDU (seen in Verification 1: a single VHT MPDU at MCS 9 was reported
  with the HT rate remembered from the frame before). That looks like a
  defect of the object; a driver should take the rate from the frame's own
  PLCP whenever its bytes 0..2 are not all zero.

So a driver must keep the rate of the last first-MPDU and apply it to the
MPDUs whose PLCP is zero; the RSSI of MPDUs without valid PHY status
(`RxStatus2` bit 8 clear) is reported invalid by the PHY (1.6).

**Short preamble, encoding, bandwidth for mac80211** (mapping of the
monitor decode): CCK rates are "CCK" with the preamble of `PhyRxStatus_0`
bit 7; OFDM rates are "OFDM"; HT and VHT as above. The bandwidth of the
frame relative to the channel (`RxChan` bits 11..13: 0x1000 20, 0x1800 40,
0x2000 80 MHz) and its control sideband (`RxChan` bits 8..10) give which 20
or 40 MHz part of the channel a narrower frame used (the monitor decode
reports the radiotap VHT bandwidth index: on an 80 MHz channel a 20 MHz frame
is 7..10 by sideband 0..3, a 40 MHz frame 5 or 6, an 80 MHz frame 4; on a
40 MHz channel a 20 MHz frame 2 (sideband 0, lower) or 3, a 40 MHz frame 1;
else 0). Note: for a 40 MHz frame on an 80 MHz channel the object gives 6
only for sideband value 0x100 and 5 for all others, which looks like a
defect; the frame's 40 MHz half follows from the sideband (0x000/0x100
lower, 0x200/0x300 upper). In the monitor status the frame length is
`RxFrameSize - 6` also when a pad is present (2 bytes too many).

### 1.6 Signal strength

* Entry point: `wlc_phy_rssi_compute(pi, rxh)` of the current band's PHY,
  called once per frame by `wlc_bmac_recv` after `tsf_l` is stored (1.2).
  Open: `bcm4360_phy_rssi_compute` ([acphy-api-rx.md](acphy-api-rx.md)).
* Inputs it reads from the header: `RxStatus2` bit 8 (PHY status valid; if
  clear the result is "invalid": `rxh+0x1c` = 0, `rxh+0x1f` = 1), the raw
  per-core values `rxh+0x09` (core 0), `rxh+0x0a` (core 1), `rxh+0x0b`
  (core 2) for the AC-PHY (signed 8 bit), and `PhyRxStatus_1` bit 3 only for
  PHY revisions 2, 5, 6. Besides the header it uses the PHY's state (number
  of cores, receive chain mask, temperature correction, per-core per-band
  corrections, combination mode).
* Outputs: `rxh+0x1c` = signed dBm (0 = unknown), `rxh+0x1f` = 0 valid / 1
  invalid, `rxh+0x24` = 0, and for the AC-PHY with valid status the
  corrected value of each core `i` (before the cores are combined; clamped to
  >= -128) at `rxh+0x20+i`, for `i` below the PHY's number of cores. These
  are the `chain_rssi[]` of `struct bcm4360_rxstatus` (only meaningful when
  `rxh+0x1f` is 0 and the receive chain mask has core `i`). Verified
  (Verification 1): with raw core values -50/-52 the result was -50 (the
  maximum, combination mode 0 of the model's state) and `rxh+0x20`, `+0x21`
  held -50, -52. **Correction to [acphy-api-rx.md](acphy-api-rx.md)**, which
  says `+0x20/+0x21` are written only for N- and HT-PHYs: the AC body writes
  them too (the common code's ring at `pi+0x10b9` is the N/HT-only part).
* The upper layer reads the value through `wlc_lq_rssi_pktrxh_cal(wlc,
  rxh)`: it returns `rxh+0x1c`. As a side effect, if `rxh+0x1f` is 0 and the
  RSSI is not 0, it copies `rxh+0x20..0x22` (3 bytes with the AC- or HT-PHY)
  into a per-antenna history of the upper layer and sets `rxh+0x1f` = 1 (so a
  frame is sampled once). No hardware access.
* Noise (monitor status only): `wlc_phy_noise_avg(pi)` of the band the
  frame's chanspec belongs to (`RxChan & 0xc000` = 0xc000: 5 GHz).

### 1.7 Time stamp (TSF) of the frame

The header has the low 16 bits of the TSF at the frame's start
(`RxTSFTime`), the host adds the full low word read after the batch
(`tsf_l`, 1.2 step 3; always later than the frame). Reconstruction:

* 32-bit (`wlc_recover_tsf32`, also used by the monitor decode for its
  `mactime`): `t = (tsf_l & 0xffff0000) | RxTSFTime`; if `(tsf_l & 0xffff) <
  RxTSFTime` (the low half wrapped between the frame and the read), `t -=
  0x10000` (32-bit arithmetic).
* 64-bit (`wlc_recover_tsf64`): read `lo = D11(0x180)` then `hi = D11(0x184)`
  now; compute `t` as above; if `lo < t` (unsigned: the low word wrapped
  since), `hi -= 1`; the TSF is `hi << 32 | t`. (Note that this reads the
  TSF again at the time of the call, not at the batch's read; the frame must
  be less than 2^32 us old for this to hold.)

### 1.8 Receive status for mac80211 (summary)

Per frame, after `wlc_bmac_recv`'s steps (TSF word, RSSI) - what fills
`struct bcm4360_rxstatus` and the frame bounds:

| mac80211 | from |
|---|---|
| frame | buffer bytes `46 + pad` .. `40 + RxFrameSize - 4` (pad = 2 if `RxStatus1` bit 2) |
| FCS | the 4 bytes after it (included by the core) |
| `chanspec`, band | `RxChan` (band: bits 14..15, 0xc000 = 5 GHz) |
| `tsf` | 1.7 |
| `rssi` | `rxh+0x1c` (0 when `rxh+0x1f` was set by the PHY) |
| `chain_rssi[i]` | `rxh+0x20+i` for the PHY's cores (1.6) |
| `enc`, `rate`, `nss`, `bw`, SGI, LDPC, STBC, short preamble | 1.5 (A-MPDU: rate of the first MPDU) |
| `FCS_FAIL` | `RxStatus1` bit 0 (the object drops such frames unless in monitor mode) |
| `DECRYPTED` | `RxStatus1` bit 3 set and bit 4 clear (the IV and ICV/MIC are still in the frame; the object strips them itself); bit 4 set: decryption failed |
| `AMPDU` | HT: `plcp[3]` bit 3 or zero PLCP; VHT: `RxStatus2` bit 8 clear or zero PLCP (1.5) |
| drop | PHY errors (`PhyRxStatus_0 & 0x310`), FCS error (unless monitor), runts (< 8 bytes after the pad), A-MSDU sub-frames when A-MSDU reception is off, frames longer than one buffer (in `dma_rx`) |

Whether the core decrypts at all depends on the keys the driver installs
(excluded here); with mac80211's software crypto no key is installed and bit
3 stays clear (read from the code: the software decryption path sets bit 3
and bit 4 itself after its own ICV check).

## 2. Interrupts

### 2.1 Registers

| Register | Name (brcmsmac) | Use |
|---|---|---|
| `D11(0x128)` | `macintstatus` | MAC interrupt status, 32 bit, write 1 to clear |
| `D11(0x12c)` | `macintmask` | MAC interrupt mask: the core raises the PCIe interrupt while `macintstatus & macintmask` != 0 |
| `D11(0x20 + 8*f)` | `intstatus` of DMA FIFO `f` (0..5) | per-FIFO DMA interrupt status, write 1 to clear (the object clears bit 16 of FIFO 0 this way; the clear of the error bits in `wlc_bmac_fifoerrors` exists but is unreachable, bmac-run.md R7) |
| `D11(0x24 + 8*f)` | `intmask` of FIFO `f` | per-FIFO DMA interrupt mask: `wlc_bmac_init` writes `D11(0x24)` = 0x00010000 (FIFO 0: receive interrupt) and no other FIFO mask (bmac-init.md B1 step 23) |
| `D11(0x100)` | `intrcvlazy[0]` | receive interrupt coalescing of FIFO 0: bits 24..31 = number of frames, bits 0..23 = time-out; written once by `wlc_bmac_init` with `wlc_hw+0x1ac` = 0x01000000 (one interrupt per frame, no time-out; bmac-init.md B1 step 21). Nothing changes it later on this path. |
| `D11(0x18)` | `gptimer` | general-purpose timer of the upper layer's high-resolution timers (`wlc_hrt_*`: written with the time to the next timer, read back); its expiry raises bit 31 |

Per-FIFO status bits the object reads (`D11(0x20 + 8*f)`): bit 16 (0x10000)
receive interrupt (frame completed, FIFO 0); bit 10 (0x400) descriptor
error, bit 11 (0x800) data error, bit 12 (0x1000) descriptor protocol error,
bit 14 (0x4000) receive overflow (no posted buffer), bit 15 (0x8000)
transmit underflow (bmac-run.md R7). Only bit 16 of FIFO 0 is enabled in
its mask, so the per-FIFO errors raise no interrupt; they are polled by the
watchdog (2.6).

### 2.2 The default mask and the bits handled

State in `wlc_hw`: `defmacintmask` (`+0x9c`), the mask written whenever
interrupts are switched on; `macintmask` (`+0x98`), the mask currently in
the register (0 while off); `macintstatus` (`+0x94`), the status bits taken
from the hardware and not yet handled; `+0x1c4`, a mask of bits always
taken (0 at attach, never set on this path).

`defmacintmask` is 0xb0e7a860 from `wlc_bmac_attach` on (bmac-init.md D2;
checked: Verification 1 reads it and `macintmask` after `up`). It changes
only through `wlc_bmac_set_defmacintmask(wlc_hw, mask, val)` (`defmask =
(defmask & ~mask) | (val & mask)`, no register access), which only the
beacon code of an access point / IBSS and the offload engine call: not a
station. Its bits and what the deferred handler does with each (2.4 gives
the order):

| Bit | Name (brcmsmac) | In the default mask | What the object does |
|---|---|---|---|
| 0x00000004 | `MI_TBTT` | no | with 0x80000: `wlc_tbtt` (the upper layer's beacon-time work) |
| 0x00000020 | `MI_ATIMWINEND` | yes | `D11(0x124)` (MAC command) = its value OR the pending command bits the upper layer collected in `wlc+0x14c`; then `wlc+0x14c` = 0 |
| 0x00000040 | `MI_PMQ` | yes | nothing (acknowledged only; the power-save queue is an access-point matter) |
| 0x00000800 | `MI_PHYTXERR` | yes | count (`txphyerr`); consecutive counts are kept in `wlc_hw+0x114` (reset to 0 by any status without this bit); only band PHY type 6 turns a count over the threshold into a fatal error: not the AC-PHY |
| 0x00002000 | `MI_GP0` | yes | microcode fatal error: the upper layer dumps microcode state (debug), counts, `wlc_fatal_error` (2.7) |
| 0x00008000 | `MI_DMAINT` | yes | a FIFO's DMA interrupt (FIFO 0 receive): the receive procedure (1.2) |
| 0x00010000 | `MI_TXSTOP` | yes | transmit FIFO suspend completed: if `wlc_bmac_tx_fifo_suspended(wlc_hw, 1)` (FIFO 1's suspend request set and `D11(0x150)` bit 1 clear, bmac-run.md R2): `wlc_txstop_intr` (the scan's and quiet period's "FIFOs are suspended" callbacks) |
| 0x00020000 | `MI_CCA` | yes | `wlc_bmac_rm_cca_int`: reads two 16-bit words of the scratch object memory (`0x4d8`, `0x4dc`, selector 0x30000) and hands the 32-bit value shifted right by 3 to the radio-measurement code |
| 0x00040000 | `MI_BG_NOISE` | yes | `wlc_phy_noise_sample_intr(pi)`: the PHY's noise measurement is done (open PHY: [acphy-api-rx.md](acphy-api-rx.md), "Noise, completed") |
| 0x00080000 | `MI_DTIM_TBTT` | no | `wlc_tbtt` |
| 0x00200000 | `MI_PWRUP` | yes | nothing in the handler (acknowledged only) |
| 0x00400000 | (Bluetooth activity stuck) | yes | in the interrupt handler, only for MAC revisions < 15: coexistence reset. Nothing for revision 42 (acknowledged only) |
| 0x00800000 | (Bluetooth predictor request) | yes | `wlc_bmac_btc_update_predictor` (coexistence; shared-memory words) |
| 0x10000000 | `MI_RFDISABLE` | yes | `wlc_rfd_intr`: reads `D11(0x158)`; if bit 16 (radio switched off) and the station is associated: the BSS is disabled (rfkill) |
| 0x20000000 | `MI_TFS` | yes | transmit status available: `wlc_bmac_txstatus` (section 4) |
| 0x80000000 | `MI_TO` | yes | general-purpose timer expired: `wlc_hrt_isr` runs the upper layer's due high-resolution timers and reprograms `D11(0x18)` |

Bit 0 (`MI_MACSSPNDD`, MAC suspended) is not in the mask; the suspend and
enable procedures poll and clear it themselves (bmac-init.md A6/A7). A
mac80211 driver needs `MI_DMAINT`, `MI_TFS`, `MI_TXSTOP` (if it suspends
FIFOs and waits for the interrupt), `MI_BG_NOISE` (if it uses the PHY's
noise measurement), `MI_GP0` and `MI_RFDISABLE`; the others serve
Broadcom's upper layer and may be left out of the mask (the microcode does
not need them to be enabled, read from the code only: the object never
checks the mask on the microcode side).

### 2.3 Interrupt handler: wlc_isr (.text+0x07a8e4, 141 bytes) and sub_07a769 = wlc_intstatus (assigned, 379 bytes)

The glue's interrupt handler calls `wlc_isr(wlc, &wantdpc)` under its
interrupt lock and schedules the deferred handler when `wantdpc` is set; the
interrupt is "ours" when the result is 1.

`wlc_isr`:

1. `wantdpc` = 0. If the MAC is not up (`wlc_hw+0x10c` = 0) or `macintmask`
   = 0 (interrupts off): result 0 (not ours), no register access.
2. `s = wlc_intstatus(wlc, in_isr = 1)`.
3. `s` = 0xffffffff (card gone): `macintstatus` = 0; result 1, no deferred
   work.
4. `s` = 0: result 0.
5. Else: `wantdpc` = 1, `macintstatus` = `s` (replaces it); result 1.

(With an offload engine attached (`wlc+0x750`), not this driver: its own
status is consulted when `s` = 0.)

`wlc_intstatus(wlc, in_isr)` (the status read and acknowledgement):

1. Read `D11(0x128)`. If the card is gone (`wlc_hw_deviceremoved`, a read of
   PCI configuration space; open: bmac-init.md C14) or the value is
   0xffffffff: return 0xffffffff.
2. `mask` = `macintmask` when called from the interrupt handler, else
   `defmacintmask`; `s = (mask | wlc_hw+0x1c4) & value`.
3. `s` = 0: return 0 (no write: the status bits outside the mask stay set in
   the register).
4. Mask everything: `D11(0x12c)` = 0, read `D11(0x12c)` back (flush);
   `macintmask` = 0.
5. (Bit 0x400000 with MAC revision < 15: coexistence reset; not revision 42.)
6. Acknowledge: `D11(0x128)` = `s`.
7. If `s` has bit 15 (`MI_DMAINT`): `D11(0x20)` = 0x00010000 (acknowledge the
   receive interrupt of FIFO 0). (MAC revision 4 instead reads and clears
   bit 16 of FIFO 0 and FIFO 3 separately; not this core.)
8. Return `s`.

Order of the accesses for a receive interrupt (read from the code; the
values left in the registers were seen in Verification 1): R `D11(0x128)`,
W `D11(0x12c)` = 0, R `D11(0x12c)`, W `D11(0x128)` = 0x8000, W `D11(0x20)` =
0x10000.

### 2.4 Deferred handler: wlc_dpc (.text+0x07a99a, 618 bytes)

Arguments: `wlc`, `bounded` (the glue passes 1), a counter structure
(`processed`, filled by the receive procedure). Result: 1 if work is left
(the glue reschedules the deferred handler without switching interrupts on),
0 when done (the glue then calls `wlc_intrson`).

The glue, before a rescheduled run (its previous result was 1), calls
`wlc_intrsupd(wlc)` under the interrupt lock: `t = wlc_intstatus(wlc,
in_isr = 0)` (the status read with the **default** mask: catches bits that
came in while the mask was 0) and, unless `t` = 0xffffffff, `macintstatus |=
t`.

`wlc_dpc` steps:

1. If the card is gone (`wlc_hw_deviceremoved`): the glue's `wl_down`;
   result 0.
2. `s` = `macintstatus`; `macintstatus` = 0.
3. Bit 0x800000: coexistence predictor (2.2).
4. Bits 0x80004 (any of them): `wlc_tbtt`.
5. Bit 0x20000000 (`MI_TFS`): `more = wlc_bmac_txstatus(wlc_hw, bounded,
   &fatal)` (section 4); if `more`: set bit 0x20000000 in `macintstatus`
   again. If `fatal`: `wlc_fatal_error(wlc)` (2.7) and go to step 17 (the
   rest of this run is skipped).
6. Bit 0x20 (`MI_ATIMWINEND`): the MAC command write of 2.2.
7. Bit 0x800 (`MI_PHYTXERR`): counters (2.2); without it `wlc_hw+0x114` = 0.
8. (Band PHY type 6 only: fatal error when the PHY transmit error count
   reaches the global threshold.)
9. Bit 0x8000 (`MI_DMAINT`): normally `more = wlc_bmac_recv(wlc_hw, 0,
   bounded, &processed)` (1.2); if `more`: set bit 0x8000 in `macintstatus`
   again. (If the bus is PCI and the flag `wlc_hw+0x191` is set,
   `wlc_recover_pkts(wlc, 1)` and a receive refill instead: the flag is
   never set by anything in the object, so this path is dead.)
10. Bit 0x10000 (`MI_TXSTOP`): 2.2.
11. Bit 0x40000 (`MI_BG_NOISE`): `wlc_phy_noise_sample_intr(pi)`.
12. Bit 0x20000 (`MI_CCA`): `wlc_bmac_rm_cca_int`.
13. Bit 0x2000 (`MI_GP0`): microcode fatal error, `wlc_fatal_error`. (The
    handler does not stop here; the following steps still run.)
14. Bit 0x80000000 (`MI_TO`): `wlc_hrt_isr`.
15. Bit 0x10000000 (`MI_RFDISABLE`): `wlc_rfd_intr`.
16. If the upper layer's transmit queue is not empty: `wlc_send_q` (posts
    queued frames now that descriptors may be free, section 3).
17. Result: `macintstatus` != 0.

So the receive and transmit-status work is bounded per run (receive: 32
frames; transmit status: tunable word 15, `pub+0x38` -> `+0x3c`, see section
4); what is left keeps its bit in `macintstatus` and the glue reschedules.
Interrupts stay masked (`D11(0x12c)` = 0) from the interrupt handler until
the run that leaves nothing pending.

### 2.5 Switching interrupts on and off

* `wlc_intrson(wlc)` (.text+0x07a720, 73 bytes): `macintmask` =
  `defmacintmask`; `D11(0x12c)` = `defmacintmask`. (Offload engine: also its
  interrupts.) The glue calls it after a deferred run that returned 0, and
  `wlc_bmac_up_finish` through the glue's `wl_intrson` (bmac-init.md C4).
* `wlc_intrsoff(wlc)` (.text+0x07a6b5, 107 bytes): if the core clock is on
  (`wlc_hw+0x186`): remember `macintmask`, `D11(0x12c)` = 0, read
  `D11(0x12c)`, `osl_delay(1)`, `macintmask` = 0; return the remembered mask
  if no status is pending (`macintstatus` = 0), else 0. Clock off: return 0
  without access. (Returning 0 when work is pending makes the matching
  restore keep the interrupts off until the deferred handler is done.)
* `wlc_intrsrestore(wlc, mask)` (.text+0x07a684, 49 bytes): if the core
  clock is on: `macintmask` = `mask`, `D11(0x12c)` = `mask`.

The MAC layer uses off/restore around its register sequences that must not
be interrupted (bmac-init.md B1 step 2 and others).

### 2.6 Receive interrupt coalescing

`D11(0x100)` = 0x01000000 for the whole session: an interrupt for every
received frame, without a timer (2.1). The bound of 32 frames per deferred
run (1.1) is the only other limit. Nothing in the object changes the value
after `wlc_bmac_init` (read from the code; the only write in the session is
the one of `wlc_bmac_init`).

### 2.7 Fatal errors

Sources:

* `MI_GP0` (microcode fatal error), in the deferred handler;
* a transmit status that does not match the frame at the head of its FIFO
  (section 4), in the deferred handler;
* per-FIFO DMA errors, found by the watchdog (`wlc_bmac_fifoerrors`, once a
  second, bmac-run.md R7): the first FIFO whose status `D11(0x20 + 8*f)` has
  any of the bits 0xdc00 (descriptor error, data error, descriptor protocol
  error, receive overflow, transmit underflow) - counted, then fatal; the
  bits are not cleared (the reset clears them).

`wlc_fatal_error(wlc)` (.text+0x03b6f0, 127 bytes): counts, remembers the
general-purpose timer (`D11(0x18)`), saves and adjusts the power-save
control state of the associated BSS, calls the glue's `wl_init(wl)` - which
is `wlc_reset` followed by `wlc_init`: the whole core is reset and
initialised again (microcode, FIFOs, rings: every posted frame is reclaimed
and freed, bmac-data.md "Life cycle" step 5) - then writes the remembered
timer back to `D11(0x18)` and restores the power-save state. For a mac80211
driver the equivalent is a restart of the hardware (`ieee80211_restart_hw`):
`bcm4360_plat_fatal()`.

## 3. Transmit

### 3.1 FIFOs

| FIFO | DMA channel | Use by the object |
|---|---|---|
| 0 | `D11(0x200)` | access category background (AC_BK) |
| 1 | `D11(0x240)` | best effort (AC_BE) |
| 2 | `D11(0x280)` | video (AC_VI) |
| 3 | `D11(0x2c0)` | voice (AC_VO), and every management/control frame and every frame sent while WME is off ("control FIFO") |
| 4, 5 | (no engine) | broadcast/multicast after DTIM and ATIM (access point / IBSS); not attached on this core |

The mapping priority -> FIFO is the table `prio2fifo` (802.1D priority 0..7
-> FIFO 1, 0, 0, 1, 2, 2, 3, 3, i.e. the usual WMM mapping); frames of the
raw 802.11 path (`wlc_prep80211_raw`) go to FIFO 3 unless WME is on and the
station is a QoS station. Every FIFO has 1024 descriptors (bmac-data.md D7).
Verified: the 22 probe requests of a scan and four raw frames were posted on
FIFO 3 (Verification 2).

### 3.2 What is posted: layout of one frame

The object pushes, in front of the 802.11 frame (which starts with its frame
control; no FCS - the core appends it):

1. a 4-byte prefix (the "TSO header"), then
2. the 124-byte (0x7c) transmit header of MAC revision >= 40,
3. the 802.11 frame.

The whole is one packet (one DMA descriptor for a linear buffer:
`dma64_txfast`, ctrl1 SOF|EOF|IOC = 0xe0000000, byte count = 4 + 124 +
frame length). Head room needed: 128 bytes. Verified (Verification 2):
every posted frame was 128 bytes longer than its 802.11 frame, the
descriptor's byte count was the packet length.

**Prefix.** Byte 0 = 0x02, bytes 1..3 = 0. It is added by `wlc_toe_add_hdr`
for every frame because `wlc_attach` sets `wlc+0x71a` = 1 (and `+0x719` = 0)
for MAC revisions >= 40. Its length is given by bit 0 of byte 0: 0 -> 4
bytes, 1 -> 16 bytes (`wlc_tso_hdr_length`); the object always uses the
4-byte form. The meaning of bit 1 is not visible in the object (the
microcode reads it; by Broadcom's naming a "TCP segmentation offload"
header with offload switched off). A driver must send it unchanged.

### 3.3 The transmit header (0x7c bytes, after the prefix)

Built by `sub_031322` (assigned `wlc_d11hdrs_mac`, .text+0x031322, 12297
bytes; the part for band PHY type 11 starts where it pushes 0x7c bytes);
every field is little-endian. Offsets relative to the header start (packet
offset + 4).

**Per-packet part (0x00..0x13):**

| Offset | Size | Name | Contents |
|---|---|---|---|
| 0x00 | 2 | (`PktInfo`) | 0 |
| 0x02 | 2 | `MacTxControlLow` | bits below |
| 0x04 | 2 | `MacTxControlHigh` | bit 1 (0x0002): the frame has one fixed rate entry (set unless the rates came from Broadcom's rate selection); bit 2 (0x0004): one of the rate entries is VHT; other bits 0 |
| 0x06 | 2 | `Chanspec` | the current chanspec (`wlc+0x518`) at the time the header is built |
| 0x08 | 1 | `IVOffset` | length of the 802.11 header: management 24, control 16, data 24 (3 addresses) or 30 (4 addresses), +2 for QoS data |
| 0x09 | 1 | `PktCacheLen` | 0 |
| 0x0a | 2 | `FrameLen` | 802.11 frame length + 4 (FCS) + the ICV length of the key used (0 without hardware encryption) + 8 if the core computes the TKIP MIC |
| 0x0c | 2 | `TxFrameID` | frame id (3.4) |
| 0x0e | 2 | `Seq` | copy of the 802.11 sequence control field after the host has set it (0 when the microcode assigns it) |
| 0x10 | 2 | `Tstamp` | only with bit 0x1000 of `MacTxControlLow`: the frame's lifetime expiry, the packet's expiry time (us) shifted right by 8 |
| 0x12 | 2 | `TxStatus` | 0 |

`MacTxControlLow` bits set by the object (names after Broadcom's
`D11AC_TXC_*` where the use agrees):

| Bit | Set when |
|---|---|
| 0x0001 | never (0 = this long header format; 1 would be the 20-byte short format of the header cache) |
| 0x0040 | A-MPDU: the frame is part of an aggregate (set by the A-MPDU code; excluded) |
| 0x0080 | expect an immediate acknowledgement: the receiver address is unicast and the frame is not marked "no ack" (and, for QoS data, the ack policy is normal) |
| 0x0200 | beacons only (frame control subtype 0x80) |
| 0x0400 | frame bursting allowed (HT data at a rate above 2 Mb/s when bursting is on and not overridden) |
| 0x0800 | the microcode assigns the sequence number: every frame that is not control and not QoS data with a host-assigned sequence (A-MPDU / HT QoS data to a station with aggregation); the host leaves the sequence field 0 |
| 0x1000 | lifetime: `Tstamp` is valid (also set again by `wlc_txfifo` when the packet has a lifetime) |
| 0x2000 | the core computes the TKIP MIC (hardware TKIP key) |
| 0x4000 | first fragment of an MSDU (fragment number 0) |
| 0x8000 | RIFS (with 0x0400; HT greenfield bursting, not used on this path) |

**Per-rate entries (0x14 + 0x14*i, i = 0..3).** One entry per rate the
microcode may use, in order of use (primary rate, then fallbacks). The
object fills 1 entry for a fixed rate (management frames, explicit rates)
and 2 (or 4 when `wlc+0x759` is set) from its rate selection; unused
entries are 0.

| Offset in entry | Size | Name | Contents |
|---|---|---|---|
| 0x00 | 2 | `PhyTxControlWord_0` | 3.5 |
| 0x02 | 2 | `PhyTxControlWord_1` | 3.5 |
| 0x04 | 2 | `PhyTxControlWord_2` | 3.5 |
| 0x06 | 6 | `plcp` | the PLCP header for this rate and the frame length (3.6) |
| 0x0c | 2 | `FbwInfo` | 0 |
| 0x0e | 2 | `TxRate` | the rate in 500 kb/s units (`wlc_rate_rspec2rate / 500`): 2..108 legacy, e.g. 300 for HT MCS 7 40 MHz short GI, 1733 for VHT 80 MHz 2 streams MCS 9 short GI |
| 0x10 | 2 | `RtsCtsControl` | bit 0x0020: this is the last valid entry; protection bits, 3.7 |
| 0x12 | 2 | `Bfm0` | 0 (beamforming only) |

**Cache part (0x64..0x7b):** 0 unless a hardware key is used: 0x64 = the key
algorithm << 4, 0x65 = the key index, and the rest (TKIP phase-1 key, packet
number) is written by the key code (excluded). A-MPDU fields (maximum MPDUs,
A-MPDU duration, block-ack window) are set by the aggregation code
(excluded).

Verified (Verification 2): probe requests (CCK 1 Mb/s, broadcast): control
low 0x4800, high 0x0002, IVOffset 24, length 130 for a 126-byte frame, one
entry `PhyTxControlWord_0` 0x0044, PLCP `0a 04 10 04 00 00`, rate 2,
`RtsCtsControl` 0x0020; unicast QoS data: control low 0x4880, IVOffset 26;
broadcast data: 0x4800.

### 3.4 Frame id

`TxFrameID` (16 bit), assigned per frame when the header is built:

* bits 0..2: the transmit FIFO;
* bits 5..8: the fragment number;
* bits 9..14: the low 6 bits of a driver-wide 16-bit counter (`wlc+0x422`),
  incremented after the last fragment of each frame (so all fragments of an
  MSDU share it);
* bits 3, 4, 15: 0.

(FIFO 4 uses a separate counter at `wlc+0x424` in bits 5..14.) The function
returns the id. The transmit status carries it back (section 4); the status
is matched to the oldest frame in that FIFO's ring, and the ids must be
equal. Verified: consecutive probe requests got 0x0003, 0x0203, ...,
0x2a03; the counter advanced by one per frame (Verification 2).

### 3.5 PHY control words

`rspec` below is the frame's rate for that entry (encoding legacy/HT/VHT,
rate or MCS, streams, bandwidth, short GI, LDPC, STBC as in 1.5).

`PhyTxControlWord_0` (`wlc_acphy_txctl0_calc`):

* bits 0..1: frame type: 0 CCK (1, 2, 5.5, 11 Mb/s), 1 OFDM, 2 HT, 3 VHT;
* bit 2: always 1;
* bit 3: beamformed (TXBF rate only; then bits 6.. = the active transmit
  chains);
* bit 4: short preamble (CCK short preamble or HT greenfield requested);
* bits 6..: the transmit core mask << 6 (`wlc_stf_txcore_get`: the
  spatial-stream module's core mask for the rate's class - index 0 CCK, 1
  OFDM, nsts+1 for HT/VHT, capped at 5; [cca-stf.md](cca-stf.md) `txcore[]`);
* bits 14..15: bandwidth of the frame: 0 20 MHz, 1 (0x4000) 40, 2 (0x8000)
  80, 3 (0xc000) 160.

`PhyTxControlWord_1` (`wlc_acphy_txctl1_calc`): bits 0..2: the position of
the frame's sub-channel within the channel: the chanspec's control sideband
(bits 8..10 of `wlc+0x518`) shifted right by (frame bandwidth code - 1)
with code 1 = 20, 2 = 40, 3 = 80 MHz (so a 20 MHz frame on an 80 MHz channel
gives the sideband 0..3, a 40 MHz frame 0..1, an 80 MHz frame 0); bits
3..8: the per-rate transmit power offset (6 bits) from
`wlc_stf_get_pwrperrate(wlc, rspec, spatial expansion)` (the power-per-rate
table of the spatial-stream module, half-dB units of back-off), corrected on
chip 0x4360 by the PHY's open-loop power offset for the bandwidth
(`wlc_phy_get_olpc_pwroffset`) when the target maximum power minus twice
that value is below the PHY's TSSI visibility threshold (the sum clamped to
-32..31).

`PhyTxControlWord_2` (`wlc_acphy_txctl2_calc`): HT: the MCS; VHT: (streams -
1) << 4 | MCS; legacy: the index of the rate in its table (CCK 1, 2, 5.5, 11
-> 0..3; OFDM 6, 9, 12, 18, 24, 36, 48, 54 -> 0..7); bit 6 (0x40): STBC.

Verified values (Verification 2, channel 36/80, rates given to the builder):
VHT 80 MHz 2 streams MCS 9 short GI: 0x80c7, 0x0038, 0x0019; HT 40 MHz
MCS 7: 0x40c6, 0x0040, 0x0007; HT 20 MHz MCS 15: 0x00c6, 0x0060, 0x000f;
OFDM 24 Mb/s: 0x00c5, -, 0x0004; OFDM 6: index 0; CCK 11: 0x00c4, -, 3; CCK
1: index 0. (The power fields are the model's power table; the core masks
the model's spatial-stream state.)

### 3.6 PLCP header (`wlc_compute_plcp`, .text+0x028ef6)

The host builds the PLCP for every rate entry; `len` = `FrameLen` (frame +
FCS + ICV/MIC). The CRC/tail bits are left 0 (the hardware fills them).

* CCK: byte 0 = rate in 100 kb/s (10, 20, 55, 110); byte 1 (SERVICE) =
  0x04, plus 0x80 (length extension) for 11 Mb/s when `ceil(8*len/11)*11 -
  8*len >= 8`; bytes 2..3 = duration in us: 1 Mb/s `8*len`, 2 Mb/s `4*len`,
  5.5 Mb/s `ceil(16*len/11)`, 11 Mb/s `ceil(8*len/11)`; bytes 4..5 = 0.
* OFDM: 24-bit L-SIG in bytes 0..2: bits 0..3 the rate code (6 -> 0xb, 9
  -> 0xf, 12 -> 0xa, 18 -> 0xe, 24 -> 0x9, 36 -> 0xd, 48 -> 0x8, 54 ->
  0xc), bits 5..16 the length (12 bits); bytes 3..5 = 0.
* HT: byte 0 = MCS | 0x80 for 40 MHz (also MCS 32); bytes 1..2 = length;
  byte 3 = 0x07 (smoothing, not sounding, reserved bit) | 0x10 STBC (one
  extra stream) | 0x40 LDPC | 0x80 short GI; bytes 4..5 = 0. (The
  aggregation bit is set by the A-MPDU code.)
* VHT: SIG-A1 in bytes 0..2: bits 0..1 bandwidth (0 20, 1 40, 2 80, 3 160),
  bit 2 = 1, bit 3 STBC, bits 4..9 group id = 0 if the frame goes to the
  access point (frame control To-DS set, From-DS clear) else 63, bits 10..12
  NSTS - 1 (streams, doubled with STBC), partial AID bits 13..21 = 0, bit 23
  = 1; SIG-A2: byte 3 = short GI (bit 0) | LDPC (bit 2) | MCS << 4; byte 4 =
  0x02 | beamformed (bit 0); byte 5 = 0. The length is not in the VHT PLCP.

Verified (Verification 2): VHT 80 MHz 2 streams MCS 9 short GI to the access
point: `06 04 80 91 02 00`; HT 40 MHz MCS 7 short GI, length 136: `87 88 00
87 00 00`; OFDM 24 and 6 Mb/s: `09 11 00`, `0b 11 00`; CCK 11 / 1 Mb/s
with 136 bytes: `6e 04 63 00`, `0a 04 40 04`; CCK 1 Mb/s, 130 bytes: `0a 04
10 04`.

### 3.7 Protection, retries, duration, sequence numbers

* **RTS/CTS and CTS-to-self**, per rate entry, in `RtsCtsControl`: bit 2
  (0x04) send RTS/CTS, bit 3 (0x08) send CTS-to-self; bit 0: the protection
  frame's rate is OFDM (0 for CCK); bit 4: short preamble for a CCK
  protection frame other than 1 Mb/s when the BSS allows short preambles;
  bits 8..11: the protection rate's code (OFDM: the L-SIG rate code of 3.6;
  CCK: rate in 100 kb/s & 0xf); bits 12..15: beamforming (0). The
  protection rate is `wlc_rspec_to_rts_rspec` of the entry's rate (the
  BSS's basic rate policy of the upper layer). RTS is chosen when the frame
  is longer than the RTS threshold (`wlc+0x528`) or marked for RTS and
  unicast; CTS-to-self for OFDM/HT/VHT rates when the BSS uses ERP/HT
  protection. Read from the code only (the session sent no protected
  frame).
* **Retries**: the header carries no per-frame retry count. The microcode
  uses the short and long retry limits and fallback limits the MAC layer
  wrote to its scratch registers at init (bmac-init.md B1 step 27: 7 / 4,
  fallback 3 / 2), switching from one rate entry to the next. The upper
  layer keeps per access category a 16-bit word (`wlc+0x2be + 2*ac`, AC
  numbering BE 0, BK 1, VI 2, VO 3; `wme_fifo2ac` maps FIFO 0..3 to 1, 0, 2,
  3): bits 0..3 the limit the status handling compares with the RTS count,
  bits 8..11 the one compared with the data transmission count, bits 4..7
  and 12..15 passed to its rate selection. The mac80211
  `tries` per rate can therefore only be approximated (read from the code
  only).
* **Duration**: the host leaves the 802.11 duration field as the frame has
  it for unfragmented frames; it writes it only for fragments (time of the
  next fragment and its acknowledgement) and in the RIFS case. A driver
  should send the duration mac80211 computed (read from the code; the
  frames of Verification 2 kept 0).
* **Sequence numbers**: with `MacTxControlLow` 0x0800 the microcode assigns
  them (and the host leaves the field 0); the host assigns them itself only
  for QoS data that may be aggregated (per-TID counters). A mac80211 driver
  can use 0x0800 for all non-QoS frames and assign QoS sequence numbers
  itself (then without 0x0800, field written).

### 3.8 Posting, flow control, suspend

* `wlc_txfifo(wlc, fifo, p, info, commit, nframes)` posts: if the packet
  has a lifetime, `MacTxControlLow |= 0x1000`; for FIFO 4 `SHM(0xa8)` = the
  frame id (not a station); adds `nframes` to the FIFO's pending count
  (`wlc->core+0x38+2*fifo`) when `commit`; `dma64_txfast(di, p, commit)`
  (bmac-data P23; `commit` = 0 lets the A-MPDU code post several frames and
  write the pointer register once); if posting failed, the count is undone.
  Chips 0x4352/0x4331/0x4360, while the BSS is in power save: one read of
  `D11(0x120)` afterwards. No other per-frame register or shared-memory
  write.
* **Flow control**: before building a frame the upper layer checks the
  FIFO's free descriptors through the `&txavail` pointer (bmac-data.md D8:
  refused unless `txavail` > the number of buffers of the frame); when a
  FIFO fills, it stops the OS queue (`wl_txflowcontrol`) and restarts it
  from the transmit-status path (`wlc_txfifo_complete` decrements the
  pending count). The pointers are copied at `wlc_attach` by
  `wlc_hw_get_txavail` (answers bmac-data.md open question 6).
* **Suspend and flush**: around channel changes the MAC layer mutes and
  suspends FIFOs (bmac-run.md R2: `D11(0x200+0x40*f)` bit 1, completion
  shown by `MI_TXSTOP` and `D11(0x150)`), and resumes them afterwards;
  posted frames stay in the rings. On down or a fatal error all rings are
  reclaimed without status (bmac-data.md P26, range 1). A driver changing
  channels with frames queued should suspend the four FIFOs, wait for
  `MI_TXSTOP`/`D11(0x150)`, change the channel and resume (read from the
  code; the session's channel changes had empty rings).

## 4. Transmit status

### 4.1 Reading: wlc_bmac_txstatus (.text+0x064143, 1003 bytes, name original)

Called by the deferred handler for `MI_TFS` (2.4) with `bounded` = 1.
Arguments: `wlc_hw`, `bounded`, a pointer to the `fatal` flag. Result: 1
when the bound was reached (the handler sets `MI_TFS` in the pending status
again), else 0.

For MAC revisions >= 40 one status is **two packages of four 32-bit
words**, read from the same four registers:

1. `limit` = tunable word 15 (`pub+0x38` -> `+0x3c`; 8 on this card,
   Verification 2) if `bounded`, else unlimited. Read the TSF low word
   `D11(0x180)` once (the time stamp given to every status of this call).
2. Loop while `fatal` is 0 and fewer than `limit` statuses were handled:
   1. `w0 = D11(0x170)`. If bit 0 is clear: no more status, leave the loop.
      If `w0` = 0xffffffff (card gone): return 0 at once.
   2. `w1 = D11(0x174)`, `w2 = D11(0x178)`, `w3 = D11(0x17c)`.
   3. `v0 = D11(0x170)`, `v1 = D11(0x174)`, `v2 = D11(0x178)`, then a read of
      `D11(0x17c)` whose value is not used. If bit 0 of `v0` is clear:
      return 0 (the status is dropped; no fatal error).
   4. Decode (4.2) and `fatal = wlc_dotxstatus(wlc, status)` (4.3).
3. If `fatal`: return 0 (the deferred handler then calls `wlc_fatal_error`).
   Else result = (count >= `limit`); if the upper layer's transmit queue is
   not empty: `wlc_send_q` (post more frames). Return the result.

(MAC revisions below 40 read a two-word status; revision 4 uses a status
FIFO in shared memory; not described.)

The hardware presents the next status in `D11(0x170..0x17c)` after the
four reads of a package (by the reading order of the object; the register
semantics themselves are not visible in the object).

### 4.2 Format of the two packages

| Word | Bits | Meaning (by the object's use) |
|---|---|---|
| `w0` | 0 | valid |
| | 1 | not read |
| | 2 | "intermediate" status (stored; used only for MAC revisions < 40) |
| | 3 | the receiver was indicated to be in power save |
| | 4..7 | suppression reason (4.3): 0 none, 1 power-save queue, 2 flush, 3 fragment / TBTT, 4 channel mismatch, 5 lifetime expired, 6 underflow |
| | 8..14 | number of MPDUs the status covers (`NCONS`); 0 is an invalid status (4.3) |
| | 15 | acknowledgement received |
| | 16..31 | frame id (3.4) |
| `w1` | 0..15 | sequence number field (stored as the status' sequence) |
| | 16..23 | PHY transmit error; non-zero: the frame failed |
| `w2` | 0..7 | transmissions at rate entry 0 |
| | 16..23 | transmissions at rate entry 1 |
| `w3` | 0..7 | transmissions at rate entry 2 |
| | 16..23 | transmissions at rate entry 3 |
| `v0` | 0 | valid (second package) |
| | 16..23 | RTS transmissions |
| | 24..31 | CTS received (stored; by Broadcom's naming) |
| `v1`, `v2` | 0..31 | A-MPDU block-ack bitmap: MPDUs 0..31 in `v1`, 32..63 in `v2` (read by the A-MPDU status code) |
| `v3` | | not used |

Derived values: **acked** = bit 15 of `w0` or `NCONS` > 1; **transmissions
in all** = the sum of the four count bytes of `w2` and `w3`. The assignment
of the count bytes to the rate entries follows from their layout and the
A-MPDU and rate-selection code that read `w2`/`w3` (read from the code; the
object itself only adds them up on this path).

Verified (Verification 2): status words written to the modelled registers
came out in the object's status as frame id, suppression reason, acked
(bit 15 -> 1, none -> 0), transmission total (sum of `w2`/`w3` bytes),
RTS count (`v0` bits 16..23) and CTS count (`v0` bits 24..31), raw low half
of `w0`; the two packages are read in the order above.

### 4.3 Matching and handling: wlc_dotxstatus (.text+0x03b7f4, 3195 bytes, name original)

Result 1 = fatal (the caller stops and the core is re-initialised), 0 =
handled.

1. `fifo` = frame id & 7. FIFO > 5, or a FIFO without a DMA engine: result
   1.
2. `p` = `dma64_getnexttxp(fifo's engine, range 2)` (bmac-data P25): the
   **oldest** frame of that FIFO whose descriptors the DMA engine has
   finished (status 0's current descriptor has passed them). The transmit
   status itself does not move the ring. If there is none: result 1 (for MAC
   revisions > 6).
3. Read the frame's header (`wlc_get_txh_info`, after the 4-byte prefix). If
   its `TxFrameID` differs from the status' frame id, or `NCONS` is 0: free
   `p` (as sent) and return 1. So statuses must come in the order the frames
   were posted, per FIFO; an out-of-order status is a fatal error.
4. A-MPDU frame (packet flag of the A-MPDU code and `MacTxControlLow` bit
   0x40): `wlc_ampdu_dotxstatus` (excluded), which uses `NCONS`, the
   block-ack bitmap and the counts.
5. Suppressed (reason != 0):
   * 1 (power-save queue): the reason is cleared (also bits 4..7 of the
     stored raw word) and the frame is handled as not sent; freed.
   * 2 (flush): the frame is **not freed** here: it is handed back to the
     upper layer's suppression handling (re-queued).
   * 3: freed as not sent. 4 (channel mismatch): counted, freed. 5 (lifetime
     expired): counted per access category, freed. 6 (underflow): freed.
6. Not suppressed, PHY error (`w1` bits 16..23) != 0: counted
   (`txphyerr`), freed as failed.
7. Acked: success; the retries (transmissions - 1) are counted.
8. Not acked: failure. If the retry limit of the access category (3.7) was
   reached by the RTS count or the transmission count, the frame counts as
   "retry limit exceeded" (an event to the upper layer for unicast frames);
   otherwise as a status error.
9. For frames of the rate selection: its update with the status (rate
   entries, counts, acked).
10. `wlc_txfifo_complete(wlc, fifo, 1)` (pending count - 1, re-open the OS
    queue when the FIFO has room again), the packet's callback, and
    `osl_pktfree(p, 1)` (except reason 2, flush). Result 0.

Verified (Verification 2): statuses for the frame at the head of FIFO 3
(not acked; suppressed with reason 5) were handled without a fatal error,
`wlc_txfifo_complete` ran and the frame was freed; a status whose frame id
was not the head frame's (once because 25 earlier frames were still in the
ring, once with a changed id) led to `wlc_fatal_error`, after which the
re-initialisation freed the frames.

### 4.4 For mac80211

Per status: the frame is the oldest posted on FIFO `frameid & 7`, after
the DMA engine has completed it; check the frame id. `acked` as in 4.2
(only meaningful for frames sent with `MacTxControlLow` 0x0080);
`tries` = the sum of the four count bytes; `rate_idx` = the last rate entry
with a non-zero count; frames with a suppression reason other than 0 or a
PHY error count as not sent (reason 2 frames may be re-sent); `ampdu` =
`MacTxControlLow` bit 0x40 of the frame, with `NCONS` and the bitmap for
the aggregate (excluded here).

## Verification

Scripts and outputs in `re-out/analysis/frames/` (they use the DMA channel
model `DmaHw` of `tools/re/dma_scenarios.py`; the engines' descriptor masks
are set to 0xffff after `up`, as the real card's probe gives them, because
the model's core reset at attach clears the registers `DmaHw` presets; the
transmit status registers are a queue modelled in `tx_check.py`). No change
to `tools/re`.

| # | What | How | Result |
|---|---|---|---|
| 1 | Receive (section 1, 2.3, 2.4): header fields, pad, PLCP and FCS bounds, rate/encoding decode for CCK, OFDM, HT (40 MHz, short GI, LDPC, STBC), VHT (80 MHz, 2 streams, MCS 9; 20 MHz frame on an 80 MHz channel), A-MPDU first/following MPDUs, PHY-status-valid bit and RSSI, per-core RSSI bytes, TSF reconstruction, drops for PLCP errors and FCS errors, A-MSDU sub-frame flag, decryption error flag; the interrupt path for `MI_DMAINT` | `rx_check.py` (`rx_check.txt`): object alone, attach + up; ten frames with hand-made headers put into FIFO 0's posted buffers, `MI_DMAINT` set, `wlc_isr` + `wlc_dpc` + `wlc_intrson` called; monitor mode on so that the object's own decode reaches `wl_monitor`; hooks at `wlc_recv` (header after `wlc_bmac_recv`) and at the return of `wlc_recv_compute_rspec` | as described; the stale-rate defect for single VHT MPDUs (1.5) and the per-core RSSI bytes (correction of acphy-api-rx.md, 1.6) were found this way |
| 2 | Transmit (section 3) and transmit status (section 4), plus `defmacintmask`, the tunables | `tx_check.py` (`tx_check.txt`): object alone, attach + up, a scan (22 probe requests captured at `wlc_txfifo` and in the ring), channel 36/80, `sub_031322` called directly with seven rates, four frames through `wlc_send80211_raw`, then status words in the modelled `D11(0x170..0x17c)` with `MI_TFS` and `wlc_isr` + `wlc_dpc` | as described: prefix, header fields, frame ids, PHY control words, PLCP bytes, descriptor flags, status decode, matching to the oldest frame, fatal error on a mismatch |

Read from the code only: everything marked so in the text, in particular
the protection bits (3.7), the hardware-key and A-MPDU fields, the
suppression reasons other than 5 and the flush case, the PHY-error path,
the handlers of interrupt bits other than `MI_DMAINT` and `MI_TFS`, the
fatal-error re-initialisation beyond its being called, `wlc_recover_tsf64`.
The values a real card writes (PHY status words, transmit status words) are
the model's or hand-made; their meaning is taken from the object's use.

## Open questions

1. **Meaning of the prefix** (3.2): the object always sends `02 00 00 00`;
   what the microcode does with bit 1, and whether a 16-byte form is ever
   needed, is not visible in the object.
2. **Single VHT MPDUs** (1.5): the object's monitor decode gives them the
   remembered rate of the previous A-MPDU. Whether the core really delivers
   a non-zero PLCP only on the first MPDU (and the PHY status only on the
   last) should be confirmed with captured frames on the card.
3. **Acknowledgement of an A-MPDU status**: "acked = bit 15 or NCONS > 1" is
   the object's formula; the exact meaning of `NCONS` for non-aggregated
   frames (always 1?) needs a card capture.
4. **Per-rate count bytes** (4.2): their assignment to rate entries 0..3
   is from the layout and the aggregation code; a capture with fallback
   transmissions would settle it.
5. **Duration field**: whether the microcode overwrites the 802.11 duration
   of unfragmented frames (the host leaves it) - matters for a driver that
   passes mac80211's value.
6. **`RxStatus1` bits 5..10** (key index) and bit 1, `PhyRxStatus_4`: not
   read by the object on these paths; meaning unknown.
7. **The power field of `PhyTxControlWord_1`** depends on
   `wlc_stf_get_pwrperrate` (the spatial-stream module's per-rate power
   table), which no specification describes yet; a driver can start with 0
   (no back-off).

## Corrections to other documents

* [acphy-api-rx.md](acphy-api-rx.md): the AC-PHY RSSI body writes the
  corrected per-core values to `rxh+0x20+core` (1.6); the spec says
  `+0x20/+0x21` are written only for N/HT PHYs.
* [bmac-data.md](bmac-data.md) open question 6: the `&txavail` pointers are
  copied by `wlc_hw_get_txavail`, called from `wlc_attach` (3.8).
