# Aggregation: A-MPDU transmit and status, Block Ack sessions, A-MSDU (`ampdu`)

Status (2026-11-05): complete: sections 1 (transmit aggregation), 2
(A-MPDU transmit status), 3 (session set-up, receive side), 4 (A-MSDU),
with Verification and Open questions. Scope: the 802.11 MAC core revision 42 of the BCM4360 with Broadcom's
microcode `d11ucode42`, AC-PHY, a station. Other MAC revisions: one line
where the object branches on them.

Notation as in [access.md](access.md) and [frames.md](frames.md): `D11(o)`
a register of the 802.11 core, `SHM(o)` a 16-bit word of shared memory,
`TPL(o)` template memory. The transmit header (`txh`, 0x7c bytes after the
4-byte prefix), its fields and the transmit status words `w0..w3`, `v0..v3`
are those of frames.md sections 3 and 4; offsets into the header are
relative to its start (packet offset + 4). "Read from the code only" marks
statements not checked by running the object; the Verification section
lists what was run (`re-out/analysis/ampdu/ampdu_check.py`).

The consumer is a Linux mac80211 driver: mac80211 negotiates the Block Ack
sessions (ADDBA/DELBA), assigns the sequence numbers of QoS data, reorders
received A-MPDUs and decides when to send a BlockAckReq. What Broadcom's
upper layer does in its place (its per-TID queues, its release and BAR
policy) is described where it decides a value the hardware sees, and
otherwise briefly, as a reference for the driver's own policy.

## Scope

| function | .text offset | size | name |
|---|---|---|---|
| `wlc_ampdu_tx_attach` | 0x054be1 | 1090 | original - 1.1 |
| `wlc_ampdu_tx_set` | 0x04f624 | 444 | original - 1.1 |
| `wlc_ampdu_mode_upd` | 0x026cb9 | 129 | original - 1.1 |
| `wlc_bmac_ampdu_set` | 0x061226 | 66 | original - 1.1 (nothing on revision 42) |
| `wlc_ampdu_shm_upd` | 0x056452 | 147 | original - 1.2, 3.3 |
| `wlc_ampdu_macaddr_upd` | 0x04f4f0 | 81 | original - 1.2 |
| `wlc_sendampdu` | 0x05478c | 267 | original - 1.5 |
| `sub_054128` | 0x054128 | 1636 | `wlc_sendampdu_hw` (assigned) - 1.5, 1.6 |
| `sub_0527b5` | 0x0527b5 | 6234 | `wlc_sendampdu_host` (assigned) - other aggregation modes, not reached |
| `sub_0502a4` | 0x0502a4 | 662 | `ampdu_seq_check` (assigned) - 1.7 |
| `wlc_ampdu_chgnsav_epoch` | 0x05400f | 281 | original - 1.6 |
| `wlc_ampdu_change_epoch` | 0x04f26c | 58 | original - 1.6 |
| `wlc_ampdu_was_ampdu` | 0x04f2a6 | 24 | original - 1.6 |
| `wlc_txh_set_epoch` | 0x0252ba | 52 | original - 1.6 |
| `wlc_txh_get_epoch` | 0x0252ee | 19 | original - 1.6 |
| `wlc_txh_get_isAMPDU` | 0x025299 | 33 | original - 1.6 |
| `wlc_send_q` | 0x03e58c | 925 | original - 1.6 (the epoch step only) |
| `sub_031322` | 0x031322 | 12297 | `wlc_d11hdrs_mac` (assigned in frames.md) - 1.4 (the A-MPDU parts) |
| `sub_04f8bb` | 0x04f8bb | 1341 | `ampdu_release` (assigned) - 1.8 |
| `sub_04f090` | 0x04f090 | 390 | `scb_ampdu_update_max` (assigned) - 1.3 |
| `sub_055023` | 0x055023 | 497 | `wlc_ampdu_create_ini` (assigned) - 1.3, 3.1 |
| `wlc_ampdu_upd_pm` | 0x04f33d | 36 | original - 1.8 |
| `sub_055384` | 0x055384 | 817 | `wlc_ampdu_tx_watchdog` (assigned) - 1.8, 3.1 |
| `wlc_ampdu_dotxstatus` | 0x051016 | 5242 | original - 2 (the part for MAC revisions >= 40) |
| `wlc_ampdu_dotxstatus_regmpdu` | 0x052599 | 540 | original - 2.5 |
| `sub_04fdf8` | 0x04fdf8 | 220 | `ampdu_window_update` (assigned) - 2.4 |
| `sub_0500a5` | 0x0500a5 | 511 | `ampdu_send_bar` (assigned) - 2.4 |
| `wlc_send_bar` | 0x055b5a | 274 | original - 2.4 |
| `sub_04fed4` | 0x04fed4 | 465 | `ampdu_bar_complete` (assigned) - 2.4 |
| `wlc_ratesel_upd_txs_ampdu` | 0x16b003 | 790 | original - 2.2 (the meaning of its arguments only) |
| `sub_050e35` | 0x050e35 | 392 | `ampdu_free_status_pkts` (assigned) - 2.3 |
| `wlc_should_retry_suppressed_pkt` | 0x0254be | 11 | original - 2.3 |
| `wlc_ampdu_recv_addba_resp` | 0x05053a | 630 | original - 3.1 |
| `ampdu_cleanup_tid_ini` | 0x0507b0 | 493 | original - 3.2 |
| `wlc_ampdu_rx_attach` | 0x0567c1 | 479 | original - 3.3 |
| `wlc_ampdu_recv_addba_req_resp` | 0x056e99 | 583 | original - 3.3 |
| `wlc_ampdu_recv_bar` | 0x0575c9 | 144 | original - 3.3 |
| `wlc_amsdu_attach` | 0x058fca | 574 | original - 4 |
| `sub_058a36` | 0x058a36 | 1428 | `wlc_amsdu_agg` (assigned) - 4 |

## Overview

On MAC revisions >= 40 the host does not build the aggregate. It posts every
MPDU of an A-MPDU as a frame of its own, with its own transmit header, on
the FIFO of its access category, one descriptor each, exactly like a single
frame (frames.md section 3). Three things make such MPDUs aggregatable:

* `MacTxControlLow` bit 0x0040 (A-MPDU) set and `MacTxControlHigh` bit
  0x0004 clear;
* the A-MPDU fields of the header's cache part (maximum number of MPDUs,
  maximum duration and MPDU spacing, Block Ack window);
* an **epoch** bit in the 4-byte prefix that the host flips whenever the
  next MPDU on that FIFO must not join the aggregate of the MPDUs before it.

The microcode builds the A-MPDUs from consecutive MPDUs of a FIFO, sends
them, collects the Block Ack, retries what was not acknowledged, and
reports one transmit status for a run of MPDUs with the Block Ack bitmap
(section 2). The host never retransmits an unacknowledged MPDU of an
aggregate itself (2.3); it moves the receiver's window on with a
BlockAckReq.

Broadcom calls this aggregation mode 3 (`pub+0xc0` = 3); modes 1 and 2
(host-built aggregates for older MAC revisions, `sub_0527b5`) are not
reached on this core and are not described.

---

## 1. Transmit aggregation

### 1.1 Mode and attach-time values: wlc_ampdu_tx_attach, wlc_ampdu_tx_set, wlc_ampdu_mode_upd

`wlc_ampdu_tx_attach` (called by `wlc_attach`) allocates the module state
(`ampdu`, 0x428 bytes, kept at `wlc+0x180`) and sets, for MAC revisions >=
40 (values observed after `up`, Verification 1):

| What | Value | Where | Use |
|---|---|---|---|
| maximum MPDUs per A-MPDU, by the peer's streams | 1 stream 32, 2 streams 48, 3 streams 64 | tunables `+0xa8`, `+0x14`, `+0x18` (`pub+0x38`) | 1.3 |
| Block Ack window offered in the ADDBA request | 64 | `ampdu+0x17` (also the upper bound `ampdu+0x40b` = 64 for the window a response may grant) | 3.1 |
| Block Ack policy | 1 = immediate | `ampdu+0x16` | 3.1 |
| own minimum MPDU start spacing | 5 (4 us; 6 for MAC revisions 17, 28, 40, 41, 44) | `ampdu+0x2a` | 1.3 |
| maximum MPDUs override | 0xff = none | `ampdu+0x2b` (iovar) | 1.3 |
| maximum A-MPDU duration | 0x1528 = 5416 us | `ampdu+0x2c` | 1.4 |
| release cap | 64 | `ampdu+0x40c` | 1.3 |
| release threshold per TID (MPDUs in flight) | 256 for TID 0, 32 for TIDs 1..7 | `ampdu+0x40e + 2*tid` | 1.8 |
| RTS/CTS before every A-MPDU | 1 = on | `wlc+0x37a` | 1.4 |
| aggregation allowed per TID | 1 for all eight | `ampdu+0x0e + tid` | 3.1 |

`wlc_ampdu_tx_set(ampdu, on)` decides the mode: for MAC revisions >= 40,
with A-MPDU transmission on, 802.11n enabled (`pub+0x68 & 3`) and an HT,
N, AC or LCN-type PHY (types 4, 6, 7, 8, 10, 11), the mode is 3. Then:

* `wlc_mhf(wlc, 2, 0x0040, 0x0040, 3)`: host flag 3 bit 6 set, in both bands
  (wlc-hw 1.1 step 11; cleared again when aggregation is switched off);
* `wlc_ampdu_mode_upd(wlc, 1)`: `wlc_bmac_ampdu_set(hw, 1)` - only MAC
  revisions 26 and 29 copy a parameter block there, nothing on revision 42 -
  and the FIFO sizes are read back (`wlc_bmac_xmtfifo_sz_get`, bmac-run R8)
  into `wlc+0x234 + 2*fifo`, no hardware write.

The frame count limit per FIFO (`pub+0x78`, frames pending in one transmit
FIFO before the upper layer stops posting to it, `wlc_prep_sdu`) is 256 in
mode 3 (`wlc_attach`; observed 0x100). It allows several aggregates to wait
in a FIFO.

A mac80211 driver needs: host flag 3 bit 6 set while it aggregates (read
from the code: it is the only hardware state the transmit side of the
module sets).

### 1.2 Hardware writes at init

Besides host flag 3 bit 6 (1.1), the aggregation modules write (wlc-hw 1.4
steps 13 and 21; observed):

* `wlc_ampdu_macaddr_upd` (from `wlc_set_mac`): `TPL(0x48..0x4f)` = the
  interface's own MAC address followed by two zero bytes. The microcode
  presumably uses it as the transmitter address of the control frames it
  sends for aggregation (Block Ack); nothing else in the object reads it.
  A driver writes it whenever the own address is set.
* `wlc_ampdu_shm_upd` (from `wlc_init`): receive side, 3.3: `SHM(0xba)` =
  0xffff and `SHM(0x3c)` = 10 for the advertised 64 KB A-MPDU receive size.

Nothing else: the aggregation code makes no register or shared-memory
access per session or per aggregate (read from the code: the only accesses
in `wlc_ampdu*.c` for mode 3 are these two and the host flag; the
status-register reads in `wlc_ampdu_dotxstatus` and `sub_050e35` and the
shared-memory reads there belong to MAC revisions below 40).

### 1.3 Per-station values: sub_04f090 and sub_055023

When a session is set up for a station (`sub_055023`, 3.1), the station's
aggregation state (`scb_ampdu`, the station cubby of the module) gets:

* **MPDU spacing** (`scb_ampdu+8`): the larger of the peer's minimum MPDU
  start spacing (bits 2..4 of the A-MPDU parameters of its HT capabilities,
  `scb+0x124`) and the own minimum (5). Codes as in 802.11: 0 none, 1 1/4
  us, 2 1/2, 3 1, 4 2, 5 4, 6 8, 7 16 us. Observed: peer 6 -> 6.
* **Maximum MPDUs per A-MPDU** (`scb_ampdu+9`, `sub_04f090`): for a VHT
  peer (`scb+0xd` bit 3) from its VHT MCS map (`scb+0x66`): 64 if it
  supports three streams (bits 4..5 not 3), else 32 if it does not support
  two (bits 2..3 = 3), else 48. For an HT-only peer from its MCS set: 64 if
  it has MCS 16..23 (`scb+0x57` non-zero), 16 if the station flag word
  `scb+8` has bit 8 set and bit 34 clear (meaning not determined), 32 if it
  has no MCS 8..15 (`scb+0x56` zero), else 48. The override `ampdu+0x2b`
  replaces the value when it is not 0xff. Observed: a two-stream VHT peer
  -> 48.
* **Release cap** (`scb_ampdu+10`): the smaller of the maximum MPDUs and 64
  (`ampdu+0x40c`). Observed 48.
* The peer's maximum A-MPDU length, 8192 << (bits 0..1 of the same
  parameters) (`scb_ampdu+0x10`; observed 65536), is kept but not used in
  mode 3: it reaches neither the header nor the hardware (read from the
  code; see Open questions).

`sub_04f090` runs again for all stations when the iovars that change these
limits are set.

### 1.4 The transmit header of an aggregatable MPDU

The header is built by `sub_031322` (frames.md 3.3) and then changed by the
posting procedure (1.5). Differences from a single frame, for an MPDU
released by the aggregation code (packet flag 0x400, 1.8):

1. **Sequence number**: assigned by the host. `MacTxControlLow` bit 0x0800
   stays clear; the header's `Seq` field (0x0e) and the 802.11 sequence
   control field are written with `seq << 4 | fragment` (fragment 0), `seq`
   being the number the release gave the packet. Observed: seq 0x123 ->
   both fields 0x1230.
2. **Aggregatable or not**: the packet is marked aggregatable (internal flag
   0x800) when its first rate entry is an HT or VHT rate and it has no key
   or an AES-CCM key (key algorithm 4); TKIP and WEP frames, and frames at
   a legacy first rate, are sent as single frames even in a session.
3. **RTS/CTS**: when the aggregatable MPDU's first rate entry is HT/VHT and
   `wlc+0x37a` is set (the default), the rate entries get RTS/CTS
   protection (observed with one entry; read for fallback entries): `RtsCtsControl` bit 2 (0x04) plus the protection rate
   fields of frames.md 3.7. Observed: 0x0925 = RTS (0x04), OFDM protection
   rate (0x01), rate code 9 (24 Mb/s), last entry (0x20).
4. **Frame bursting** (`MacTxControlLow` 0x0400): for QoS data at more than
   2 Mb/s when bursting is on (`wlc+0x291`), unless
   `wlc_ampdu_frameburst_override` (`ampdu+0x409`, set by the module's
   watchdog from an iovar flag `ampdu+0x40a` and station flags; 0 here) -
   also for aggregated frames
   whatever the access category's TXOP. (Off in the session: 0x0400 not
   set.)
5. The PLCP of each rate entry is computed as for a single frame
   (frames.md 3.6) with the frame's own length; the HT-SIG aggregation bit
   (byte 3 bit 3) is **not** set by the host (observed: HT 40 MHz MCS 7 ->
   byte 3 = 0x07). The microcode must fill the PPDU's PLCP for the
   aggregate (not visible in the object).

The posting procedure (1.5) then sets, for an MPDU that will be aggregated:

| Field | Value |
|---|---|
| `MacTxControlLow` | bit 0x0040 set (with 0x0080 expect-ack and 0x4000 first fragment as usual: observed 0x40c0) |
| `MacTxControlHigh` | bit 0x0004 cleared (the header builder sets it for VHT rates) |
| `txh+0x66` (1 byte) | maximum MPDUs in the A-MPDU at the primary rate entry = `scb_ampdu+9` (observed 0x30 = 48) |
| `txh+0x67` (1 byte) | the same for the fallback entries (the same value) |
| `txh+0x68` (16 bit) | bits 4..15: the maximum A-MPDU duration in microseconds, `ampdu+0x2c & 0xfff0` (5408); bits 0..3: the MPDU spacing code `scb_ampdu+8` (observed 0x1526) |
| `txh+0x6a` (1 byte) | the Block Ack window size minus 1 (window 64 -> 0x3f) |
| `txh+0x6b` (1 byte) | 0 |
| prefix byte 2 bit 1 (0x02) | the epoch (1.6) |

(The four cache bytes are written only when the header has the long
format, which is always the case on this path.)

For an MPDU of a session that will **not** be aggregated (not marked
aggregatable by 2. above, or the session is being torn down, state 3):
`MacTxControlLow` bit 0x0040 cleared, `MacTxControlHigh` bit 0x0004 set,
cache bytes untouched (0) - read from the code. `MacTxControlHigh` 0x0004
therefore means "sent alone": the header builder sets it for every frame
with a VHT rate entry (a VHT PPDU always carries an A-MPDU, here of one
MPDU), the aggregation path clears it on aggregated MPDUs. **Correction to
frames.md** 3.3, which reads it as "one of the rate entries is VHT": that is
when the header builder sets it, not its meaning to the microcode.

A frame that is not of a session (QoS data without aggregation, management,
non-QoS) has neither bit 0x0040 nor cache fields; its prefix epoch is 0
unless the FIFO's last frame was aggregated (1.6).

### 1.5 Posting: wlc_sendampdu and sub_054128

`wlc_send_q` (frames.md 3.8) hands a packet with flag 0x400 to
`wlc_sendampdu(ampdu, queue, &p, prec)`. It drops the packet (freed, counted)
when its station is not A-MPDU capable (`scb+10` bit 2) or the TID has no
session or one still waiting for the ADDBA response (state 2); returns
"busy" while `wlc+0x304` is set (transmission blocked); in mode 3 calls
`sub_054128`.

`sub_054128` (assigned `wlc_sendampdu_hw`), for the packet and then for every
following packet at the head of the same queue precedence that also has
flag 0x400 and the same station and TID:

1. FIFO = `prio2fifo[tid]`. Build the header (`wlc_prep_sdu` for 802.3
   packets, `wlc_prep_pdu` for 802.11 ones; frames.md 3); on "no room"
   (-16) the packet goes back to the queue and the procedure ends.
2. If the receiver address of the frame is all zero: drop.
3. Unless the packet carries the "re-sent" flag 0x100 (2.3), which is
   cleared instead: the sequence check of 1.7. If it says drop, the packet
   is freed and the session's in-flight count (`ini+0x54`) decremented.
4. Aggregatable (flag 0x800, 1.4) and the session not stopping: set the
   A-MPDU fields of 1.4 and decide the epoch (1.6). Otherwise: clear 0x0040,
   set 0x0004 and, if the FIFO's last frame was aggregated, flip the epoch
   (reason 2 of 1.6).
5. `wlc_txfifo(wlc, fifo, p, ..., commit = 1, nframes = 1)`: one descriptor,
   the DMA pointer written for every MPDU (observed: six MPDUs, six posts
   with commit 1).

There is no batching on the host side: the release (1.8) decides how many
MPDUs go to the common queue, and from there every MPDU is posted as soon
as `wlc_send_q` reaches it. The per-FIFO limit of 256 pending frames
(1.1) is the only bound on how many wait in the hardware.

### 1.6 The epoch

State per transmit FIFO `f` (in `ampdu`, stride 0x28 from `+0x2a8`): the
current epoch bit (`+0x2a8`), the station and the TID of the last MPDU
posted (`+0x2b0`, `+0x2b8`), the kind of that frame (`+0x2bb`: 0 not
aggregated, 1 HT, 2 VHT - from bits 0..1 of the first entry's
`PhyTxControlWord_0`), the first entry's `FbwInfo` (`+0x2b9`) and PLCP bytes
0 and 3 (`+0x2bc`, `+0x2bd`), for VHT also bytes 1, 2, 4 (`+0x2bf`, `+0x2be`,
`+0x2c0`). All start at 0.

`wlc_ampdu_chgnsav_epoch(ampdu, fifo, reason, scb, tid, txh_info)`: unless
`reason` is -1, flip the FIFO's epoch bit; then save the frame's station,
TID, kind and rate bytes as above (kind 0 when its `MacTxControlLow` bit
0x0040 is clear); return the epoch bit. `wlc_txh_set_epoch` writes the bit
to prefix byte 2 bit 1 (0x02; on MAC revisions below 42 also into the
header's first word).

When the posting procedure decides the epoch for an aggregatable MPDU (step
4 of 1.5), the epoch is flipped (a new aggregate starts) when:

* the frame id has bit 15 set (a rate probe of Broadcom's rate selection;
  not used by a mac80211 driver) - reason 1;
* the station or the TID differs from the FIFO's last MPDU - reason 4;
* the kind differs (the last frame was not aggregated, or HT vs VHT), or
  `FbwInfo` differs, or the rate bytes differ (HT: PLCP bytes 0 and 3 = MCS,
  bandwidth, short GI, LDPC, STBC; VHT: PLCP bytes 0..4) - reason 0;
* everything is equal but the sequence check (1.7) found a gap - reason 5.

Otherwise the epoch is kept (and the FIFO state saved when the last frame
was not aggregated). For the second and later MPDUs taken in the same call
of `sub_054128` the comparison is skipped (the packets of one release share
station, TID and rate) unless a packet asks for it (packet tag byte 6 bit
7); only the gap of 1.7 can flip the epoch then.

For a frame without flag 0x400, `wlc_send_q` flips the epoch of the frame's
FIFO when the FIFO's last frame was aggregated (`wlc_ampdu_was_ampdu`;
reason 3) and writes it into the frame's prefix; else the prefix keeps 0.

Observed (Verification 1): six VHT MPDUs of TID 0 on FIFO 1 all carried
epoch 1 (flipped from 0 by the first, new station); the following
non-aggregated frame on FIFO 1 carried 0; the next six VHT MPDUs 1 (the last
frame was not aggregated); two HT MPDUs after them 0 (kind and rate
changed).

What the microcode does with it is not visible in the object. By the host's
behaviour: consecutive MPDUs of one FIFO with bit 0x0040 and the same epoch
may be sent in one A-MPDU; a change of the epoch bit ends the aggregate.
Read from the code; the microcode side is an inference.

### 1.7 The sequence check: sub_0502a4

Per session (`ini`) the next expected sequence number `ini+0x5e`
(initialised to the session's starting sequence number, 3.1). For an MPDU
with sequence number `s` (from the frame's sequence control):

* `s` = expected: expected = `s + 1` (mod 4096); send.
* else the epoch must change (the gap flag of 1.6). `d = (s - expected) mod
  4096`. If `d` <= 2048 (MPDUs were skipped, e.g. dropped before posting):
  the window start `ini+0x56` = `s`, a BlockAckReq is sent if one is due
  (`sub_0500a5`, 2.4), expected = `s + 1`; send.
* else (`s` is behind the expected number, an old MPDU): drop (counted).

The packets that the status handling re-sends (suppressed, 2.3) skip this
check. A mac80211 driver that posts mac80211's sequence numbers in order
needs only the epoch flip on a gap.

### 1.8 Broadcom's release policy: sub_04f8bb (reference)

Broadcom keeps a queue per station and TID (`scb_ampdu+0x18`, eight
precedences) and moves MPDUs from it to the common transmit queue in
`sub_04f8bb(ampdu, scb_ampdu, ini, force)`; mac80211 replaces this. In mode
3, with `n` = the number queued for the TID, capped at the window
(`ini+0x60`, the granted Block Ack window) and the release cap
(`scb_ampdu+10`):

1. Nothing unless the session is on (state 1) and `n` > 0.
2. `inflight` = (last released sequence number `ini+0x5c` - window start
   `ini+0x5a`) mod 4096, taken as 0 when >= 2048. (The window start follows
   the last acknowledged MPDU, 2.4.)
3. Unless `force`: release only if `inflight` <= the TID's threshold (256 for
   TID 0, 32 for the others; `wlc_ampdu_upd_pm` sets TID 0's to 32 while the
   power-save state is 1 and back to 256) and the common queue has at least
   min(256, its maximum / 2) free places. (An airtime-based variant applies
   when `wlc+0x81c` is set; not on this path.)
4. If `inflight` >= 1 (`ampdu+0x32`) and `n` is less than a full aggregate
   (min(window, release cap)) and not `force`: release nothing (wait for
   more). With nothing in flight, release whatever is queued.
5. Release `n` packets: each gets flag 0x400, the next number of the
   station's per-TID sequence counter (`scb+0xda + 2*tid`, then
   incremented), `ini+0x5c` = that number; it goes to the next transmit
   module (the common queue). `ini+0x54` (MPDUs in flight) += `n`.

It runs when packets are queued for the TID, after every A-MPDU status, and
from the module's watchdog once a second with `force` (`sub_055384`).
Observed: six MPDUs with nothing in flight were released (sequence numbers
0x123..0x128); six more while six were in flight were held; after the
status of the first six (window moved on) they were released.

---

## 2. A-MPDU transmit status

### 2.1 Dispatch

A status is read and matched as in frames.md 4.1-4.3: two packages of four
words, the frame at the head of FIFO `frameid & 7` (the oldest posted frame
whose descriptors the DMA engine has completed), its frame id compared with
the status'. `wlc_dotxstatus` then, for a packet with the release flag 0x400
(1.8):

* `MacTxControlLow` bit 0x0040 set: `wlc_ampdu_dotxstatus(ampdu, scb, p,
  status, txh_info)` handles the status completely (2.3) and the result is
  its result (always 0: an aggregate status is never fatal);
* bit 0x0040 clear (a single MPDU of a session): `wlc_ampdu_dotxstatus_regmpdu`
  (2.5), then the ordinary handling of frames.md 4.3 continues.

### 2.2 The status words of an aggregate

What the A-MPDU code reads (MAC revisions >= 40; by the object's use):

| Word | Bits | Meaning |
|---|---|---|
| `w0` | 8..14 | `NCONS`: the number of MPDUs this status completes - the head frame and the `NCONS - 1` frames posted after it on the same FIFO |
| `w0` | 4..7 | suppression reason (frames.md 4.2), 2.3 step 2 |
| `w0` | 3 | the receiver was indicated in power save (to the station's power-save state, `wlc_update_pmstate`) |
| `w1` | 16..23 | PHY transmit error (non-zero: no rate-selection update) |
| `w2` | 0..7 | MPDUs sent at the primary rate entry |
| `w2` | 8..15 | of these, acknowledged |
| `w2` | 16..23 | MPDUs sent at the fallback rate entries |
| `w2` | 24..31 | of these, acknowledged |
| `v1` | 0..31 | Block Ack bitmap, MPDUs 0..31 of the status |
| `v2` | 0..31 | Block Ack bitmap, MPDUs 32..63 |

Bit `i` of the bitmap belongs to the `i`-th MPDU of the status in posting
order (0 = the head frame whose id is in `w0`); it is set when that MPDU was
acknowledged. The bitmap is **not** indexed by sequence number.

The meaning of the four `w2` bytes follows from the rate selection that
receives them (`wlc_ratesel_upd_txs_ampdu`: it caps byte 1 at byte 0 and
byte 3 at byte 2 and takes the success ratio of each pair). This differs
from a single frame's status, where frames.md 4.2 reads `w2`/`w3` as
transmission counts per rate entry; **for an aggregate, frames.md's "acked"
(`w0` bit 15 or `NCONS` > 1) and "transmissions in all" (sum of the count
bytes) do not apply**. `w0` bit 15, `w1` bits 0..15 (sequence), `w3` and
`v0` are not read by the aggregate path. (For a head frame with a fixed
rate, `MacTxControlHigh` bit 1, the object would read `w2` as two 16-bit
halves, sent and acknowledged; it reaches that only for frames whose rate
came from its rate selection, which never have the fixed bit. Read from the
code only.)

### 2.3 wlc_ampdu_dotxstatus (.text+0x051016, the part for MAC revisions >= 40)

Arguments: `ampdu`, the station, the head packet, the decoded status, the
head's header information.

1. If there is no station, it is not A-MPDU capable, or its session for the
   packet's TID is missing or still waiting for the ADDBA response:
   `sub_050e35` takes `NCONS` packets (the head, then the next completed
   packets of the FIFO, `dma64_getnexttxp(range 2)`), and for each calls
   `wlc_txfifo_complete(fifo, 1)` and frees it (counted as not sent). Done.
2. Suppression reason `r`:
   * 0: if the PHY error byte is non-zero it is counted and the rate
     selection is not updated; else the rate selection is updated when the
     head's rate came from it (step 6).
   * 1 (power-save queue): **re-send** (`wlc_should_retry_suppressed_pkt` is
     true for reason 1 only).
   * 3, 4 (channel mismatch), 5 (lifetime expired: also Broadcom's expiry
     accounting), 2, 6: counted where named; no re-send, no rate update.
3. For each MPDU `i` = 0 .. `NCONS - 1` (the head first, then the next
   completed packet of the FIFO's ring - their frame ids are not checked):
   * unless re-sending: the session's in-flight count `ini+0x54` - 1;
   * bitmap bit `i` set: the MPDU's sequence number becomes the last
     acknowledged (`ini+0x58`); the station's last-activity time is set; the
     packet's callback reports success; freed;
   * clear and re-sending: the packet gets flag 0x100 and goes back to the
     station's transmit path at precedence `prio2prec[tid] + 1` (at most 15),
     ahead of newer frames; it is posted again without the sequence check
     (1.7) and stays in flight;
   * clear otherwise: its sequence number is remembered as the last
     unacknowledged one; freed as failed. The host does not retransmit it.
   * `wlc_txfifo_complete(wlc, fifo, 1)` (frames.md 4.3 step 10) for every
     MPDU, re-sent or not.
4. If an MPDU was left unacknowledged (not re-sent): the BlockAckReq
   decision of 2.4.
5. `sub_04fdf8` (window update and release, 2.4); the power-save state of
   the station from `w0` bit 3.
6. Rate selection (only when the head's rate came from it, packet tag bit
   27): `wlc_scb_ratesel_upd_txs_ampdu(rs, scb, frameid, sent0, acked0,
   sent1, acked1, 0, mcs, antenna)` with the four `w2` bytes (each capped at
   255); when bytes 0 and 2 are both 0, `sent0` = `NCONS` and the others 0.
   `mcs` is taken from the head's first rate entry: HT `PhyTxControlWord_2 &
   0x3f`; VHT `0x80 | (streams << 4) | MCS` (observed 0xa9 for 2 streams
   MCS 9).
7. `sub_04f8bb` (release more, 1.8); result 0.

Observed (Verification 1): a status with `NCONS` 6, bitmap 0b111011 and
`w2` = 6, 5, 1, 0 for six MPDUs 0x123..0x128: six `wlc_txfifo_complete`,
all six packets freed, last acknowledged 0x128, the rate selection called
with 6, 5, 1, 0, a BlockAckReq with starting sequence number 0x129. A status
with reason 1 for six MPDUs: none freed, all six posted again at once, the
in-flight count unchanged.

So a failed MPDU of an aggregate is final when its status arrives: the
microcode has already retried it (the fallback counts of `w2` show the
retransmissions). Read from the code; how often and at which rate the
microcode retries is not visible (presumably the retry limits of bmac-init
B1 step 27).

### 2.4 Window and BlockAckReq: sub_04fdf8, sub_0500a5, wlc_send_bar

Window update (`sub_04fdf8`, mode 3): if the last acknowledged number
`ini+0x58` is ahead of the window start `ini+0x5a` (by 1..2047), the start
becomes last acknowledged + 1; then the release (1.8).

BlockAckReq decision after an aggregate with an unacknowledged MPDU (`u` =
the last unacknowledged sequence number, `w` = the window): if (last
released `ini+0x5c` - `u`) mod 4096 < `w`, or (`u` - last acknowledged) mod
4096 >= 2`w`, and no BlockAckReq is outstanding or (`u` + 1 - the last BAR's
number `ini+0x56`) mod 4096 < 2048: `ini+0x56` = the sequence number of the
**last MPDU of the status** + 1, then `sub_0500a5`.

`sub_0500a5(ampdu, ini, start)` sends one if none is outstanding
(`ini+0xea` = 0), the session is on, and fewer than 50 were sent in a row
(`ini+0xee`; at 25 the sending is postponed once). Its starting sequence number is
`ini+0x56` (the window start `ini+0x5a` when called with `start`, at the
session's begin). `wlc_send_bar(wlc, scb, tid, ssn, 0, 1, &err)` builds a
20-byte control frame: frame control 0x0084 (BlockAckReq), duration 0, RA =
the station, TA = the own address, BAR control = `tid << 12 | 0x0004`
(compressed bitmap; bit 0, no acknowledgement policy, 0), starting
sequence control = `ssn << 4`; it is sent with `wlc_sendctl` (FIFO 3,
observed) at the same 802.1D priority as the TID. `ini+0x56` and
`ini+0x62` hold its number. A callback on its status (`sub_04fed4`) clears
the outstanding flag (observed); if the BAR was acknowledged, its number
becomes the last acknowledged one (when it lies between the window start
and the last released number + 1) and the window moves on, and if no newer
BAR was asked for meanwhile the request is dropped; then `sub_0500a5` runs
again, so an unacknowledged BAR, or one asked for while the previous was
outstanding, is sent at once (read from the code).

Observed: BlockAckReq frames `84 00 ...` with control 0x0004 and SSN field
0x1290 after the hole of 2.3, and one with the session's starting number
right after the ADDBA response (3.1).

### 2.5 A single MPDU of a session: wlc_ampdu_dotxstatus_regmpdu

For a packet with flag 0x400 sent without bit 0x0040 (not aggregatable,
1.4): the in-flight count - 1; acknowledged: last acknowledged = its
sequence number; not acknowledged and within the window ((last released -
`s`) mod 4096 < `w`): `ini+0x56` = `s` + 1 and a BlockAckReq (2.4), unless
one is outstanding for an older number. Then the window update and
release. Read from the code only.

### 2.6 For mac80211

Per aggregate status: the MPDUs are the `NCONS` oldest completed frames of
FIFO `frameid & 7`, in posting order, the first with the status' frame id;
MPDU `i` was acknowledged if bitmap bit `i` is set; `NCONS` and the number
of set bits among bits 0..`NCONS-1` are the `ampdu_len` and `ampdu_ack_len`
of mac80211's A-MPDU status; `w2` bytes 0..3 give the attempts and successes
at the primary and the fallback rate. An unacknowledged MPDU is lost for
good; the receiver's reorder window has to be moved past it (Broadcom: a
BlockAckReq with the number after the status' last MPDU). A status with
suppression reason 1 means the MPDUs were not sent (receiver asleep): they
can be posted again unchanged (same sequence numbers).

---

## 3. Session set-up and the receive side

### 3.1 Transmit session (originator): sub_055023, wlc_ampdu_recv_addba_resp

Neither the set-up nor the tear-down of a Block Ack session writes to the
hardware (read from the code: no register, shared-memory, template or host
flag access in these procedures or their callees; in the emulator session
the set-up only posted the two frames named below). What a session changes
is what the host puts into the headers of its MPDUs (1.4) and how it reads
their statuses (2).

`sub_055023(ampdu, scb_ampdu, tid, force)` (assigned
`wlc_ampdu_create_ini`): unless aggregation is off for the TID
(`ampdu+0x0e + tid`), computes the station's values (1.3), creates the
per-TID state `ini` in state 2 (waiting) and sends an ADDBA request
(`wlc_send_addba_req`): Block Ack window 64, immediate policy, timeout 0,
starting sequence number = the station's per-TID counter. Observed request
body `03 00 01 03 10 00 00 30 12`: parameter set 0x1003 (A-MSDU bit set,
immediate, TID 0, buffer 64), timeout 0, starting sequence control 0x1230.
The module's watchdog repeats the request once a second up to four times,
then gives up (the queued frames go on unaggregated).

`wlc_ampdu_recv_addba_resp(ampdu, scb, body)` accepts a response for a
waiting TID if its status is 0, its policy is immediate and its buffer
size is at most 64 (`ampdu+0x40b`); otherwise the session is dropped
(state 0, queued frames passed on unaggregated). On acceptance:

* state 1 (on); window = the response's buffer size (`ini+1`, `ini+0x60`);
  the header's `txh+0x6a` is this minus 1;
* A-MSDUs inside A-MPDUs allowed for the station when the response's
  parameter bit 0 is set (station flag `scb+0xc` bit 29);
* window start, next expected number = the station's per-TID counter now;
  last released = start - 1 (observed 0x123, 0x123, 0x122);
* a BlockAckReq with the starting number is sent at once (`sub_0500a5` with
  `start`; observed on FIFO 3 before any data);
* the TID's queued frames are released (1.8).

For mac80211 (`ampdu_action`): `IEEE80211_AMPDU_TX_START` and
`TX_OPERATIONAL` need no hardware access; keep per station and TID the
window (`buf_size`, at most 64 for the header byte), and per station the
maximum MPDUs (from the peer's streams, 1.3) and the spacing (the larger of
the peer's `ampdu_density` and 5). `TX_STOP_*`: MPDUs already posted
complete with their statuses as usual.

### 3.2 Tear-down: ampdu_cleanup_tid_ini

Called for a DELBA received or sent, a disassociation, a reset
(`wlc_ampdu_tx_reset` from `wlc_reset`), a failed BlockAckReq series (the
watchdog sends a DELBA with reason 39 after 50 BARs in a row, or when the
window makes no progress while MPDUs are in flight; the number of seconds
was not settled): state 3; when no BlockAckReq is
outstanding, the last BAR's number becomes the last acknowledged one and
the window moves on; the TID's queued frames are dropped; the state is
freed when nothing is in flight (or when forced; with a BAR outstanding it
is kept until the BAR's status). MPDUs already in a hardware FIFO stay
there and complete normally; MPDUs still in the common queue are posted as
single frames (1.4). No hardware access (read from the code only).

### 3.3 Receive side: wlc_ampdu_rx_attach, wlc_ampdu_shm_upd, wlc_ampdu_recv_addba_req_resp

What the receive module sets (observed after `up`):

* advertised A-MPDU parameters of the HT capabilities (`wlc+0x383`): 0x17 =
  maximum A-MPDU length exponent 3 (64 KB, `ampdu_rx+0x0f`; 2 for PHY type 4
  revisions 0..1) and minimum MPDU start spacing 5 (4 us, `ampdu_rx+0x10`;
  6 for MAC revisions 17 and 28);
* window offered in ADDBA responses: at most 64 (`ampdu_rx+0x0d`); policy:
  immediate only (`ampdu_rx+0x0c` = 1);
* `wlc_ampdu_shm_upd`, at every `wlc_init` when A-MPDU transmit and
  receive are both on (`pub+0x5e`, `pub+0x5f`) and the PHY type is 4, 6, 7,
  8, 10 or 11: if the advertised length exponent is 3, `SHM(0xba)` = 0xffff
  and `SHM(0x3c)` = 10, else `SHM(0xba)` = 0x8000 and `SHM(0x3c)` = 5
  (observed 0xffff and 10). By `brcmsmac`'s names (`M_MIMO_MAXSYM`,
  `M_WATCHDOG_8TU`), which agree with this use: the longest received frame
  the microcode accepts, in symbols, and its receive watchdog in units of 8
  TU; both raised for 64 KB A-MPDUs. A driver advertising 64 KB writes
  0xffff and 10.

`wlc_ampdu_recv_addba_req_resp` (an ADDBA request from the peer): accepted
when receive aggregation is on for the TID and the policy is immediate;
window = the request's buffer size, capped at 64 (64 when the request says
0); timeout 0; the A-MSDU bit of the request is kept in the response only
when `wlc+0x293` is set (MAC revisions >= 40 with `pub+0x60` and enough
receive buffers; observed 1). Otherwise a response with status 37
(declined) or 38 (invalid parameters). No hardware access: the microcode
holds no per-session receive state that the host sets.

So the microcode acknowledges received A-MPDUs with Block Acks, and
BlockAckReq frames with Block Acks, on its own: nothing in the upper layer
builds a BlockAck frame (frame control subtype 9), a received BlockAckReq
only moves the software reorder window (`wlc_ampdu_recv_bar`), and a
received BlockAck that reaches the host is only counted
(`wlc_ampdu_recv_ba`). The transmitter address it uses is presumably the
template copy of 1.2. Read from the code; the microcode side is an
inference. Whether it answers with a compressed bitmap for every TID, and
whether it needs host flag 3 bit 6 for that, is not visible.

Received MPDUs of an A-MPDU come to the host one by one (frames.md 1.5,
1.8: first/following MPDU by the PLCP bytes and `RxStatus2` bit 8). A
mac80211 driver marks them as A-MPDU subframes (same reference number per
A-MPDU, last-subframe flag when `RxStatus2` bit 8 is set) and lets mac80211
reorder. Control frames reach Broadcom's `wlc_recv` (it hands BlockAckReq
and BlockAck to the aggregation code) without the "pass control frames"
filter of wlc-hw section 6, so the core passes these two subtypes to the
host by itself (inference from the code; not checked).

---

## 4. A-MSDU

Transmit: Broadcom builds A-MSDUs in software, in a transmit module before
the A-MPDU module (`sub_058a36`): it concatenates frames of one station and
TID into one MPDU with A-MSDU subframe headers and the QoS control's
A-MSDU-present bit. The core sees an ordinary MPDU; there is no header bit
or prefix flag for A-MSDUs on this path, and the A-MPDU fields of 1.4 are
the same (read from the code only). Limits set by `wlc_amsdu_attach`: per
FIFO the smaller of a value from the FIFO's size (`wlc+0x234 + 2*fifo`
times `wlc+0x738`) and 3835 bytes for the AC-PHY (7935 otherwise); an
A-MSDU goes into an A-MPDU only if the peer allowed it in its ADDBA response
(3.1). A mac80211 driver can let mac80211 build A-MSDUs; the hardware needs
nothing for them.

Receive: the core splits received A-MSDUs into sub-frames itself (frames.md
1.3, `RxStatus2` bits 0..2). The advertised maximum A-MSDU length is 7935
when the receive FIFO's size (`wlc_rcvfifo_limit_get`) less `wlc+0x6bc` less
100 exceeds 7934, else 3835; for the AC-PHY up to 11398 when that room
allows (read from the code only).

---

## Verification

Script and output in `re-out/analysis/ampdu/` (`ampdu_check.py`,
`ampdu_check.txt`; it reuses the transmit-status queue and the DMA model of
`re-out/analysis/frames/tx_check.py`). Object alone (`hybrid.Run()`), no
change to `tools/re`.

| # | What | How | Result |
|---|---|---|---|
| 1 | Attach values (1.1, 3.3), the mode, `pub+0x78`, `SHM(0xba)`/`SHM(0x3c)`; a whole transmit session: station values (1.3), ADDBA request, response handling (3.1), release policy (1.8), sequence numbers, the header and cache fields, RTS bits and PLCP of aggregated MPDUs (1.4), posting (1.5), the epoch in four situations (1.6); an A-MPDU status with a hole: bitmap mapping, freeing, window, BlockAckReq contents, rate-selection arguments (2.2-2.4); the BAR's own status; a suppressed (reason 1) aggregate status | attach, up, channel 36/80; a station from `wlc_scblookup` (its BSS pointer set by hand) marked A-MPDU, HT and VHT capable (2 streams, spacing 6), `wlc_scb_ampdu_enable`; `sub_055023` for TID 0; a hand-made ADDBA response to `wlc_ampdu_recv_addba_resp`; 802.11 QoS frames (VHT 80 MHz 2x2 MCS 9, HT 40 MHz MCS 7, OFDM 24) queued on the TID and released by `sub_04f8bb`, then `wlc_send_q`; posted frames captured at `wlc_txfifo`; statuses in the modelled `D11(0x170..0x17c)` with `MI_TFS`, `wlc_isr` + `wlc_dpc`; the rate selection of the station replaced by a stub that records its arguments | as described in the text |

Read from the code only: the mode decision for other configurations, the
station values for HT-only peers, frame bursting, the sequence-gap case of
1.7, the non-aggregated MPDU of a session (header bits and its status,
2.5), suppression reasons other than 1, the PHY-error case, the BAR
callback beyond clearing the outstanding flag, tear-down (3.2), the
receive-side ADDBA handling, A-MSDU (4). The microcode's side (what the
epoch and the cache fields do, retries, Block Ack generation) is inferred
from the host's behaviour and not visible in the object.

## Open questions

1. **Epoch and cache fields in the microcode**: which MPDU's cache fields
   (the first of the aggregate?) limit the aggregate, and whether the
   microcode also ends an aggregate at the Block Ack window by itself
   (`txh+0x6a`) or relies on the host never posting beyond it (Broadcom
   holds at most a window's worth in flight only through its release
   policy, 1.8). A capture of aggregates on the card would settle it.
2. **Peer's maximum A-MPDU length**: mode 3 passes it nowhere (`txh+0x6b`
   stays 0). Either the microcode bounds aggregates only by MPDU count and
   duration, or `txh+0x6b` is a length field that Broadcom leaves 0. Matters
   for peers advertising less than 64 KB.
3. **HT-SIG aggregation bit**: the host leaves it 0 in every PLCP; assumed
   set by the microcode for aggregates.
4. **`MacTxControlHigh` 0x0004 for non-VHT single frames**: the A-MPDU path
   sets it also at HT or legacy rates (1.4); whether the microcode looks at
   it for non-VHT rates is not visible.
5. **Retries inside the microcode**: limits and rate stepping for MPDUs of
   an aggregate, and whether `NCONS` can be smaller than the number of
   MPDUs that went out (MPDUs left for a later status).
6. **Block Ack generation**: whether the microcode needs host flag 3 bit 6
   (or anything else) to answer received A-MPDUs and BlockAckReqs, and which
   transmitter address it uses (`TPL(0x48)` presumed).
7. **Control frames to the host**: whether BlockAckReq frames reach the
   host without the control-frame filter (3.3), which mac80211 needs to
   move its reorder window.
8. The station flag (`scb+8` bit 8, 1.3) that limits HT-only peers to 16
   MPDUs.

## Corrections to other documents

* [frames.md](frames.md) 3.3: `MacTxControlHigh` bit 0x0004 is "sent alone"
  (a single MPDU, for VHT an A-MPDU of one MPDU); the header builder sets it
  for VHT rates, the aggregation path clears it on aggregated MPDUs (1.4).
  The prefix's byte 2 bit 1 is the epoch (1.6), not always 0. The cache
  fields `txh+0x66..0x6b` and `MacTxControlLow` 0x0040 are given in 1.4.
* [frames.md](frames.md) 4.2: for an aggregate, `w2` holds sent/acknowledged
  MPDU counts at the primary and fallback rates, not transmissions per rate
  entry; "acked" and the transmission total do not apply (2.2).
* [wlc-hw.md](wlc-hw.md) Q2: `SHM(0xba)`/`SHM(0x3c)` are the receive limits
  of 3.3.
