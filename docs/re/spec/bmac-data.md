# DMA engine, FIFOs and interrupts (`bmac-data`)

Status: **incomplete draft** (2026-11-02): the analyst's session was stopped
after the Data section (engine structure, channel registers, the slot table of
`dma64proc`, control flags); the Procedures and Verification sections are
still empty. Not yet a basis for an implementation. Section 1 (the DMA engine
`hnddma.c`) is written first, because the hybrid build takes the engine over at
`dma_attach`. Names of stripped functions marked "(assigned)" were
proposed by this analyst (Broadcom style).

## Scope

The whole of `hnddma.c` (.text 0x00dcf8..0x0108d4) on the 64 bit
descriptor path, the parts of `wlc_bmac.c` that set up the engines
(`dma_attach` calls, the parameter function `sub_069659`), and (later
sections) the FIFOs and interrupts.

The function table `dma64proc` (.rodata+0x27d620, 400 bytes = 49 slots and a
count) is what every caller outside `hnddma.c` reaches; its slots are listed
in "Data" (D3) with the prototype of each.

| function | .text offset | size | name |
|---|---|---|---|
| `dma_attach` | 0x010380 | 1364 | original |
| `dma_addrwidth` | 0x00ffbc | 159 | original |
| `sub_00fecf` | 0x00fecf | 237 | `dma_detach` (assigned), slot 0x000 |
| `sub_00f947` | 0x00f947 | 272 | `dma64_txinit` (assigned), slot 0x008 |
| `sub_00f64a` | 0x00f64a | 198 | `dma64_txreset` (assigned), slot 0x010 |
| `sub_00e448` | 0x00e448 | 29 | `dma64_txenabled` (assigned), slot 0x018 |
| `sub_00e992` | 0x00e992 | 45 | `dma64_txsuspend` (assigned), slot 0x020 |
| `sub_00e965` | 0x00e965 | 45 | `dma64_txresume` (assigned), slot 0x028 |
| `sub_00e428` | 0x00e428 | 32 | `dma64_txsuspended` (assigned), slot 0x030 |
| `sub_00e3e2` | 0x00e3e2 | 70 | `dma64_txsuspendedidle` (assigned), slot 0x038 |
| `sub_00fafd` | 0x00fafd | 465 | `dma64_txfast` (assigned), slot 0x040 |
| `sub_00f3e6` | 0x00f3e6 | 286 | `dma64_txunframed` (assigned), slot 0x048 |
| `sub_00e338` | 0x00e338 | 170 | `dma64_getpos` (assigned), slot 0x050 |
| `sub_00e318` | 0x00e318 | 32 | `dma64_txstopped` (assigned), slot 0x058 |
| `sub_00f098` | 0x00f098 | 68 | `dma64_txreclaim` (assigned), slot 0x060 |
| `sub_00ee0d` | 0x00ee0d | 348 | `dma64_getnexttxp` (assigned), slot 0x068 |
| `sub_00e294` | 0x00e294 | 132 | `dma64_peeknexttxp` (assigned), slot 0x070 |
| `sub_00e184` | 0x00e184 | 272 | `dma64_peekntxp` (assigned), slot 0x078 |
| `sub_00ddcb` | 0x00ddcb | 13 | `dma_txblock` (assigned), slot 0x080 |
| `sub_00ddd8` | 0x00ddd8 | 29 | `dma_txunblock` (assigned), slot 0x088 |
| `sub_00ddf5` | 0x00ddf5 | 24 | `dma_txactive` (assigned), slot 0x090 |
| `sub_00e7c1` | 0x00e7c1 | 420 | `dma64_txrotate` (assigned), slot 0x098 |
| `sub_00f897` | 0x00f897 | 176 | `dma_rxinit` (assigned), slot 0x0a0 |
| `sub_00f5ef` | 0x00f5ef | 91 | `dma64_rxreset` (assigned), slot 0x0a8 |
| `sub_00e140` | 0x00e140 | 68 | `dma64_rxidle` (assigned), slot 0x0b0 |
| `sub_00e120` | 0x00e120 | 32 | `dma64_rxstopped` (assigned), slot 0x0b8 |
| `sub_00e5f0` | 0x00e5f0 | 127 | `dma_rxenable` (assigned), slot 0x0c0 |
| `sub_00e103` | 0x00e103 | 29 | `dma64_rxenabled` (assigned), slot 0x0c8 |
| `sub_00f504` | 0x00f504 | 235 | `dma_rx` (assigned), slot 0x0d0 |
| `sub_00f14d` | 0x00f14d | 665 | `dma_rxfill` (assigned), slot 0x0d8 |
| `sub_00f11a` | 0x00f11a | 51 | `dma_rxreclaim` (assigned), slot 0x0e0 |
| `sub_00ec76` | 0x00ec76 | 407 | `dma_getnextrxp` (assigned), slot 0x0e8 |
| `sub_00e02e` | 0x00e02e | 144 | `dma_peeknextrxp` (assigned), slot 0x0f0 |
| `sub_00ddb3` | 0x00ddb3 | 24 | `dma_rxparam_get` (assigned), slot 0x0f8 |
| `sub_00e5c9` | 0x00e5c9 | 39 | `dma64_fifoloopbackenable` (assigned), slot 0x100 |
| `sub_00ec49` | 0x00ec49 | 45 | `dma_getvar` (assigned), slot 0x108 |
| `sub_00de2e` | 0x00de2e | 27 | `dma_counterreset` (assigned), slot 0x110 |
| `sub_00e551` | 0x00e551 | 120 | `dma_ctrlflags` (assigned), slot 0x118 |
| `sub_00de0d` | 0x00de0d | 33 | `dma_rxactive` (assigned), slot 0x138 |
| `sub_00dfc6` | 0x00dfc6 | 104 | `dma_txpending` (assigned), slot 0x140 |
| `sub_00df72` | 0x00df72 | 84 | `dma_txcommitted` (assigned), slot 0x148 |
| `sub_00de55` | 0x00de55 | 15 | `dma_pktpool_set` (assigned), slot 0x150 |
| `sub_00df44` | 0x00df44 | 46 | `dma_rxtxerror` (assigned), slot 0x158 |
| `sub_00de64` | 0x00de64 | 19 | `dma_burstlen_set` (assigned), slot 0x160 |
| `sub_00de49` | 0x00de49 | 12 | `dma_avoidancecnt` (assigned), slot 0x168 |
| `sub_00de77` | 0x00de77 | 101 | `dma_param_set` (assigned), slot 0x170 |
| `sub_00e4ff` | 0x00e4ff | 82 | `dma_glom_enable` (assigned), slot 0x178 |
| `sub_00dedc` | 0x00dedc | 104 | `dma64_activerxbuf` (assigned), slot 0x180 |
| `sub_00dcf8` | 0x00dcf8 | 187 | `dma64_dd_upd` (assigned), internal: write one descriptor |
| `sub_00e66f` | 0x00e66f | 338 | `dma_ddtable_init` (assigned), internal: ring base registers |
| `sub_00ebf0` | 0x00ebf0 | 89 | `dma64_addrext` (assigned), internal: address-extension probe |
| `sub_01005b` | 0x01005b | 177 | `dma_ringalloc` (assigned), internal |
| `sub_01010c` | 0x01010c | 628 | `dma_alloc` (assigned), internal: allocate one ring |
| `sub_069659` | 0x069659 | 502 | `wlc_bmac_dma_param_init` (assigned), in `wlc_bmac.c` |

The functions of the 32 bit engine (table at .rodata+0x27d7b0, used only when
the core lacks the "64 bit DMA" status flag) are listed in D5 and not
described further.

## Overview

(to be completed)

## Data

### D1. The engine (`dma_info_t`, 0x130 bytes, allocated by `dma_attach`)

The first 0x20 bytes are the public part (`hnddma_t`) that callers may read;
the rest is private to `hnddma.c` (see "Field use outside hnddma.c" below
for what is really read from outside).

| Offset | Size | Name (assigned) | Meaning |
|---|---|---|---|
| 0x00 | 8 | di_fn | pointer to the function table (`dma64proc` for this core) |
| 0x08 | 4 | txavail | free transmit descriptors (exported by `getvar("&txavail")`) |
| 0x0c | 4 | dmactrlflags | control flags, see D4 |
| 0x10 | 4 | rxgiants | counter: received frames longer than one buffer and dropped |
| 0x14 | 4 | rxnobuf | counter: receive fill could not get a packet |
| 0x18 | 4 | txnobuf | counter: transmit post found the ring full |
| 0x1c | 4 | txnodesc | counter: (32 bit engine only) ring full in txfast |
| 0x20 | 8 | msg_level | pointer to the message level word (`&wl_msg_level`; default a .bss word) |
| 0x28 | 8 | name | 8 bytes, NUL terminated (`"wl0"`) |
| 0x30 | 8 | osh | OS handle |
| 0x38 | 8 | sih | backplane handle |
| 0x40 | 1 | dma64 | 1 = 64 bit descriptors (core status flag 0x1000) |
| 0x41 | 1 | addrext | 1 = the engine supports address extension |
| 0x48 | 8 | txregs | address of the transmit channel registers (0 = none) |
| 0x50 | 8 | rxregs | address of the receive channel registers (0 = none) |
| 0x58 | 8 | txd | transmit descriptor ring (virtual, aligned) |
| 0x60 | 8 | rxd | receive descriptor ring (virtual, aligned) |
| 0x68 | 2 | dmadesc_align | log2 of the ring alignment (4, 0xc or 0xd) |
| 0x6a | 2 | ntxd | transmit ring size (power of 2) |
| 0x6c | 2 | txin | index of the oldest descriptor not yet reclaimed |
| 0x6e | 2 | txout | index of the next free descriptor |
| 0x70 | 8 | txp | array of `ntxd` packet pointers |
| 0x78 | 8 | tx_dmah | OS DMA handle of the transmit ring (filled by the allocator) |
| 0x80 | 8 | txp_dmah | per-descriptor map handles, 0x90 bytes each (never allocated: 0) |
| 0x88 | 8 | txdpa | bus address of the aligned transmit ring |
| 0x90 | 8 | txdpaorig | bus address of the allocation |
| 0x98 | 2 | txdalign | aligned minus original address |
| 0x9c | 4 | txdalloc | bytes allocated |
| 0xa0 | 4 | xmtptrbase | low 32 bits of the transmit ring bus address (when `aligndesc_4k` = 0) |
| 0xa4 | 2 | nrxd | receive ring size |
| 0xa6 | 2 | rxin | index of the next descriptor to be completed |
| 0xa8 | 2 | rxout | index of the next descriptor to be posted |
| 0xb0 | 8 | rxp | array of `nrxd` packet pointers |
| 0xb8 | 8 | rx_dmah | OS DMA handle of the receive ring |
| 0xc0 | 8 | rxp_dmah | per-descriptor map handles (never allocated: 0) |
| 0xc8 | 8 | rxdpa | bus address of the aligned receive ring |
| 0xd0 | 8 | rxdpaorig | bus address of the allocation |
| 0xd8 | 2 | rxdalign | |
| 0xdc | 4 | rxdalloc | |
| 0xe0 | 4 | rcvptrbase | low 32 bits of the receive ring bus address |
| 0xe4 | 2 | rxbufsize | receive buffer size (minus `rxextrahdrroom` when > 0xcc) |
| 0xe8 | 4 | rxextrahdrroom | extra head room of receive buffers (0xcc when the caller passes -1) |
| 0xec | 4 | nrxpost | receive buffers to keep posted |
| 0xf0 | 4 | rxoffset | bytes of receive status the hardware writes before the frame |
| 0xf4 | 4 | ddoffsetlow | added to ring bus addresses (low word) |
| 0xf8 | 4 | ddoffsethigh | value of the high address word of the rings |
| 0xfc | 4 | dataoffsetlow | added to buffer bus addresses (low word) |
| 0x100 | 4 | dataoffsethigh | value of the high address word of buffers |
| 0x104 | 1 | aligndesc_4k | 1 = rings must be aligned to 4/8 KB; 0 = 16 bytes are enough |
| 0x105 | 1 | rxburstlen | receive burst length field (control bits 18..20) |
| 0x106 | 1 | txburstlen | transmit burst length field (control bits 18..20) |
| 0x107 | 1 | txmultioutstdrd | transmit "outstanding reads" field (control bits 6..7) |
| 0x108 | 1 | txprefetchctl | transmit prefetch control (control bits 21..23) |
| 0x109 | 1 | txprefetchthresh | transmit prefetch threshold (control bits 24..25) |
| 0x10a | 1 | rxprefetchctl | receive prefetch control (control bits 21..23) |
| 0x10b | 1 | rxprefetchthresh | receive prefetch threshold (control bits 24..25) |
| 0x110 | 8 | pktpool | packet pool (set by slot 0x150; unused on this path) |
| 0x118 | 4 | dma_avoidance_cnt | counter (returned by slot 0x168; never incremented on this path) |
| 0x11c | 4 | xs0_cd_mask | mask of the "current descriptor" field of transmit status 0 |
| 0x120 | 4 | xs1_ad_mask | mask of the "active descriptor" field of transmit status 1 |
| 0x124 | 4 | rs0_cd_mask | mask of the "current descriptor" field of receive status 0 |
| 0x128 | 2 | rs0cd | last read receive "current descriptor" index (cache) |
| 0x12a | 2 | xs0cd | last read transmit "current descriptor" index (cache) |

### D2. Registers of one channel (64 bit engine)

Each FIFO has a transmit channel at `D11(0x200 + 0x40*fifo)` and a receive
channel 0x20 bytes above it (`D11(0x220 + 0x40*fifo)`); `wlc_bmac_attach`
passes these addresses to `dma_attach` (core revision >= 11; older cores use
other offsets). Offsets inside a channel:

| Offset | Transmit (`XC`, `XP`, ...) | Receive |
|---|---|---|
| +0x00 | control: bit 0 enable, bit 1 suspend request, bit 2 loopback, bits 6..7 outstanding reads, bit 11 parity disable, bits 16..17 address extension, bits 18..20 burst length, bits 21..23 prefetch control, bits 24..25 prefetch threshold | control: bit 0 enable, bits 1..7 receive offset (bytes), bit 10 overflow continue, bit 11 parity disable, bit 14 "glom" enable, bits 16..17 address extension, bits 18..20 burst length, bits 21..23 prefetch control, bits 24..25 prefetch threshold |
| +0x04 | pointer: bus address (low 32 bits) of the descriptor *after* the last one posted | pointer: the same for the receive ring |
| +0x08 | ring base address, low 32 bits | the same |
| +0x0c | ring base address, high 32 bits | the same |
| +0x10 | status 0: bits 0..12 (mask `xs0_cd_mask`) current descriptor (byte offset in the ring, low bits of the bus address); bits 28..31 state: 0 disabled, 1 active, 2 idle, 3 stopped, 4 suspend pending | status 0: current descriptor (mask `rs0_cd_mask`); bits 28..31 state: 0 disabled, 1 active, 2 idle, 3 stopped, 4 suspend pending (unverified for 4) |
| +0x14 | status 1: active descriptor (mask `xs1_ad_mask`); bits 28..31 error code (0 = none) | status 1: bits 28..31 error code |

A descriptor index `i` corresponds to the pointer value `base_low + 16*i`;
the engine converts a status/pointer value back to an index as
`((value & mask) - base_low) & mask) >> 4` (base_low = `xmtptrbase` /
`rcvptrbase`).

### D3. The function table `dma64proc` (.rodata+0x27d620)

`di` is the engine returned by `dma_attach`; all slots take it as the first
argument. `range` (transmit reclaim) is 0 = only descriptors the hardware
has finished ("DMA_RANGE_TRANSMITTED"), 1 = everything posted
("DMA_RANGE_ALL"), 2 = everything the hardware has fetched
("DMA_RANGE_TRANSFERED"), as far as the code distinguishes them (see
`dma64_getnexttxp`). `pkt` is an OS packet handle (`osl_pkt*`).

| Slot | Function | Name (assigned) | Prototype | What it does |
|---|---|---|---|---|
| 0x000 | `sub_00fecf` | dma_detach | `void (di)` | free rings, arrays and the engine |
| 0x008 | `sub_00f947` | dma64_txinit | `void (di)` | empty the transmit ring, program control and ring base, enable |
| 0x010 | `sub_00f64a` | dma64_txreset | `bool (di)` | suspend, wait, disable, wait for "disabled"; 1 = disabled |
| 0x018 | `sub_00e448` | dma64_txenabled | `bool (di)` | transmit control bit 0 (0 if the register reads all ones) |
| 0x020 | `sub_00e992` | dma64_txsuspend | `void (di)` | set transmit control bit 1 |
| 0x028 | `sub_00e965` | dma64_txresume | `void (di)` | clear transmit control bit 1 |
| 0x030 | `sub_00e428` | dma64_txsuspended | `bool (di)` | transmit control bit 1 (1 if `ntxd` = 0) |
| 0x038 | `sub_00e3e2` | dma64_txsuspendedidle | `bool (di)` | suspend requested and state = idle |
| 0x040 | `sub_00fafd` | dma64_txfast | `int (di, pkt, bool commit)` | post a packet chain; 0 or -1 (ring full, packet freed) |
| 0x048 | `sub_00f3e6` | dma64_txunframed | `int (di, void *buf, uint len, bool commit)` | post one raw buffer as one frame |
| 0x050 | `sub_00e338` | dma64_getpos | `void *(di, bool direction_tx)` | packet at the hardware's current descriptor |
| 0x058 | `sub_00e318` | dma64_txstopped | `bool (di)` | transmit state = stopped |
| 0x060 | `sub_00f098` | dma64_txreclaim | `void (di, int range)` | reclaim and free all packets of `range` |
| 0x068 | `sub_00ee0d` | dma64_getnexttxp | `void *(di, int range)` | reclaim one transmitted packet |
| 0x070 | `sub_00e294` | dma64_peeknexttxp | `void *(di)` | first transmitted packet, without reclaiming |
| 0x078 | `sub_00e184` | dma64_peekntxp | `int (di, int *n, void *pkts[], int range)` | list up to `*n` posted packets |
| 0x080 | `sub_00ddcb` | dma_txblock | `void (di)` | `txavail` = 0 |
| 0x088 | `sub_00ddd8` | dma_txunblock | `void (di)` | recompute `txavail` |
| 0x090 | `sub_00ddf5` | dma_txactive | `uint (di)` | descriptors posted and not reclaimed |
| 0x098 | `sub_00e7c1` | dma64_txrotate | `void (di)` | move pending descriptors to the hardware's position |
| 0x0a0 | `sub_00f897` | dma_rxinit | `void (di)` | empty the receive ring, enable, program ring base |
| 0x0a8 | `sub_00f5ef` | dma64_rxreset | `bool (di)` | disable, wait for "disabled"; 1 = disabled |
| 0x0b0 | `sub_00e140` | dma64_rxidle | `bool (di)` | receive current descriptor = pointer (nothing posted is pending) |
| 0x0b8 | `sub_00e120` | dma64_rxstopped | `bool (di)` | receive state = stopped |
| 0x0c0 | `sub_00e5f0` | dma_rxenable | `void (di)` | program receive control (enable, offset, burst, prefetch) |
| 0x0c8 | `sub_00e103` | dma64_rxenabled | `bool (di)` | receive control bit 0 |
| 0x0d0 | `sub_00f504` | dma_rx | `void *(di)` | next received frame (chain), or NULL |
| 0x0d8 | `sub_00f14d` | dma_rxfill | `bool (di)` | post receive buffers up to `nrxpost`; 1 = ring empty and no memory |
| 0x0e0 | `sub_00f11a` | dma_rxreclaim | `void (di)` | free all posted receive buffers |
| 0x0e8 | `sub_00ec76` | dma_getnextrxp | `void *(di, bool forceall)` | take one completed (or, forced, any posted) receive buffer |
| 0x0f0 | `sub_00e02e` | dma_peeknextrxp | `void *(di)` | first completed receive buffer, without taking it |
| 0x0f8 | `sub_00ddb3` | dma_rxparam_get | `void (di, u16 *rxoffset, u16 *rxbufsize)` | |
| 0x100 | `sub_00e5c9` | dma64_fifoloopbackenable | `void (di)` | set transmit control bit 2 |
| 0x108 | `sub_00ec49` | dma_getvar | `void *(di, const char *name)` | `"&txavail"` -> `&di->txavail`, else NULL |
| 0x110 | `sub_00de2e` | dma_counterreset | `void (di)` | clear `rxgiants`, `rxnobuf`, `txnobuf` |
| 0x118 | `sub_00e551` | dma_ctrlflags | `uint (di, uint mask, uint flags)` | change `dmactrlflags` (parity probe) |
| 0x120 | 0 | (dump) | | absent in this build |
| 0x128 | 0 | (dumptx) | | absent |
| 0x130 | 0 | (dumprx) | | absent |
| 0x138 | `sub_00de0d` | dma_rxactive | `uint (di)` | receive descriptors posted and not taken |
| 0x140 | `sub_00dfc6` | dma_txpending | `uint (di)` | posted descriptors the hardware has not reached |
| 0x148 | `sub_00df72` | dma_txcommitted | `uint (di)` | descriptors committed to the hardware and not finished |
| 0x150 | `sub_00de55` | dma_pktpool_set | `int (di, void *pool)` | store pool, return 0 |
| 0x158 | `sub_00df44` | dma_rxtxerror | `bool (di, bool istx)` | error field of status 1 non-zero |
| 0x160 | `sub_00de64` | dma_burstlen_set | `void (di, u8 rxburstlen, u8 txburstlen)` | |
| 0x168 | `sub_00de49` | dma_avoidancecnt | `uint (di)` | return `dma_avoidance_cnt` |
| 0x170 | `sub_00de77` | dma_param_set | `void (di, u16 id, u16 value)` | set one burst/prefetch field |
| 0x178 | `sub_00e4ff` | dma_glom_enable | `bool (di, uint enable)` | receive control bit 14 |
| 0x180 | `sub_00dedc` | dma64_activerxbuf | `uint (di)` | receive descriptors between the hardware's current and active positions |
| 0x188 | (the number 0x28 = 40) | | | count stored after the table |

Slots used in the session (`boundary.py hnddma.c`): 0x008, 0x010, 0x020,
0x028, 0x060, 0x0a0, 0x0a8, 0x0d8, 0x0e0, 0x108, 0x170 (and `dma_attach`,
`dma_addrwidth`). The others are reached by the data path (transmit,
receive, status), which the emulated session exercises only partly.

### D4. Control flags (`dmactrlflags`, `+0x0c`)

| Bit | Meaning on this path |
|---|---|
| 0x01 | parity: descriptors carry a parity bit and the parity-disable bit of the control registers is not set. Set only if the hardware keeps the parity-disable bit writable (`dma_ctrlflags`). |
| 0x02 | receive overflow continue: receive control bit 10 is set by `dma_rxenable` |
| 0x04 | receive frames spanning several buffers are passed up as a chain (`dma_rx`); otherwise they are dropped and counted in `rxgiants` |
| 0x08 | transmit reclaim (`dma64_txreclaim`) does not free the packets |
| 0x10 | receive buffers are aligned to 16 bytes (`dma_rxfill`) (purpose of the name unknown) |
| 0x20 | set by `wlc_bmac_attach` only for chip 0x4331 with a large PCIe request size; no effect inside the 64 bit engine of this build (unverified) |

`dma_attach` clears bits 0 and 1 (`ctrlflags(di, 3, 0)`), so all flags are 0
on this card unless a caller sets them (see "Field use outside hnddma.c").

### D5. The 32 bit engine (one line)

`dma_attach` picks the table at .rodata+0x27d7b0 instead of `dma64proc` when
the core's status flag 0x1000 is clear; it uses 8 byte descriptors, a
pointer register at +0x08 and a status register at +0x0c (`sub_00fa57`
txinit, `sub_00f7d2` txreset, `sub_00e4e2` txenabled, `sub_00ebc3`
txsuspend, `sub_00eb96` txresume, `sub_00e4c2` txsuspended, `sub_00f76b`
txsuspendedidle, `sub_00fcce` txfast, `sub_00e4a2` txstopped, `sub_00f0dc`
txreclaim, `sub_00ef69` getnexttxp, `sub_00ea3c` txrotate, `sub_00f710`
rxreset, `sub_00e0be` rxidle, `sub_00e482` rxstopped, `sub_00e465`
rxenabled, `sub_00e9bf` addrext; the other slots are shared with the 64 bit
table, txunframed/getpos/dump/glom/activerxbuf are 0). A revision 42 core
has the flag, so this engine is not used (the old traces made without the
flag in the model used it).

## Procedures

(being written)

## Verification

(to be written)

## Open questions

(to be written)
