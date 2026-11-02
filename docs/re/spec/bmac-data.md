# DMA engine, FIFOs and interrupts (`bmac-data`)

Status (2026-11-02, second analyst): **section 1 (the DMA engine
`hnddma.c`, 64 bit path) is complete**: Data (D1-D8), Procedures P1-P54
(every `dma64proc` slot, `dma_attach`, allocation, internal helpers, the
parameter function of `wlc_bmac.c`), Verification against the traces and
memory hooks. Paths the session cannot reach (transmit reclaim with
hardware progress, receive of frames, rotate, peeks) are verified by
reading only. Sections 2-4 (FIFO use, interrupts, hardware behaviour) are
**not written yet** (see Open questions). The
first analyst wrote D1-D5 and was stopped; the second analyst kept them,
corrected the transmit `range` numbering (D3) and added the rest. Section 1
is written first, because the hybrid build takes the engine over at
`dma_attach`. Names of stripped functions marked "(assigned)" were proposed
by the analysts (Broadcom style).

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
| `dma_txpioloopback` | 0x00ea18 | 36 | original (one line in P21; not called) |

The functions of the 32 bit engine (table at .rodata+0x27d7b0, used only when
the core lacks the "64 bit DMA" status flag) are listed in D5 and not
described further.

## Overview

**One engine per FIFO.** The 802.11 core (rev 42) has six FIFOs; each FIFO
has a transmit DMA channel and a receive DMA channel (register blocks in
D2). `wlc_bmac_attach` creates one engine (`dma_info_t`, D1) per used FIFO
with `dma_attach`: FIFO 0 gets transmit and receive (the only receive path
of this core), FIFOs 1, 2, 3 get transmit only, FIFOs 4 and 5 are not
attached (their handles stay 0). After each `dma_attach` the parameter
function `sub_069659` sets the burst/prefetch fields (P54), and the handle
is stored with `wlc_hw_set_di`. The engine's first field points to the
function table `dma64proc`; every later use from outside `hnddma.c` goes
through that table (D3) except four plain field reads (D8).

**Rings.** Each channel has a ring of 16-byte descriptors (D6) in coherent
memory, allocated in `dma_attach`. The driver owns the descriptors between
`txin` and `txout` (transmit) or `rxin` and `rxout` (receive) only in the
sense of bookkeeping: it writes a descriptor, then moves the hardware's
"pointer" register (`+0x04`, the bus address of the descriptor after the
last valid one) to hand it over; the hardware reports its progress in the
"current descriptor" field of status 0 (and, for transmit, the "active
descriptor" field of status 1). Indices are converted to and from bus
addresses with the ring's base (`xmtptrbase`, `rcvptrbase`) and the masks
found at attach (`xs0_cd_mask`, ...).

**Life cycle in a session** (all of it seen in the traces, see
Verification):

1. attach: `dma_attach` x4 (register probes, ring allocation), parameters
   (`sub_069659`), `getvar("&txavail")` x4.
2. every core reset (`wlc_bmac_corereset`, bmac-init A14): transmit reset
   (slot 0x10) of FIFOs 0..3, receive reset (slot 0xa8, via `sub_0638a5`) of
   FIFO 0.
3. `wlc_bmac_init` (bmac-init B1 step 30): transmit init (slot 0x08) of
   FIFOs 0..3, receive init (slot 0xa0) and receive fill (slot 0xd8) of
   FIFO 0.
4. running: transmit post (slot 0x40, from `wlc_txfifo`/`wlc_bmac_txfifo`);
   transmit status processing reclaims with slot 0x68; the receive path
   (`wlc_bmac_recv`) takes frames with slot 0xd0 and refills with slot 0xd8;
   the watchdog refills FIFO 0 (slot 0xd8); FIFO suspend/resume (slots 0x20,
   0x28) around channel changes and scans.
5. down/flush (`sub_05fefa`): transmit reclaim of everything (slot 0x60,
   range 1) of FIFOs 0..3, receive reclaim (slot 0xe0) of FIFO 0; then the
   core reset of step 2.
6. detach (`wlc_bmac_detach`): slot 0x00 for each handle.

**What the engine needs from the OS layer**: `osl_malloc/mfree/memset/
strncpy/strcmp`, `osl_dma_alloc_consistent/free_consistent` (rings),
`osl_dma_map/unmap` (buffers), `osl_pktget/pktfree/pktdata/pktlen/pktnext/
pktsetnext/pktsetlen/pktpull` (packets), `osl_readl/writel`, `osl_delay`;
from the backplane layer `si_core_sflags`, `si_coreid`, `si_corerev`,
`si_osh`, `si_backplane64`. Nothing else: the engine has no global state of
its own (the default message-level word at .bss+0x2d0 is only stored, never
read on this path).

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
argument. `range` (transmit reclaim) is 1 = everything posted
("HNDDMA_RANGE_ALL"), 3 = everything the hardware has fetched
("HNDDMA_RANGE_TRANSFERED", also looks at the active-descriptor field of
status 1), any other value (the callers use 2, "HNDDMA_RANGE_TRANSMITTED") =
only descriptors the hardware has finished (current-descriptor field of
status 0); see `dma64_getnexttxp` (P25). (Corrected by the second analyst:
the first draft numbered them 0/1/2.) `pkt` is an OS packet handle
(Linux: `struct sk_buff *`, used only through `osl_pkt*`).

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
| 0x180 | `sub_00dedc` | dma64_activerxbuf | `uint (di)` | posted receive descriptors the hardware has not reached (index of `RP` minus index of `RS0`) |
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

### D6. The 64 bit descriptor (16 bytes, little-endian 32 bit words)

| Word (byte offset) | Name | Contents |
|---|---|---|
| 0 (+0x0) | ctrl1 | bit 31 SOF (first descriptor of a frame), bit 30 EOF (last descriptor of a frame), bit 29 IOC (interrupt on completion), bit 28 EOT (this is the last descriptor of the ring: the hardware wraps to the base after it). Transmit: the flags only. Receive: only EOT (0x10000000 on index `nrxd-1`, else 0). |
| 1 (+0x4) | ctrl2 | bits 0..14 buffer byte count (`len & 0x7fff`); bits 16..17 address extension (bits 31..30 of the bus address, only when `dataoffsetlow` != 0 and the address uses them - never on this card); bit 18 parity (only with control flag 0x01, never on this card) |
| 2 (+0x8) | addrlow | bus address of the buffer + `dataoffsetlow` (= the bus address: `dataoffsetlow` is 0) |
| 3 (+0xc) | addrhigh | `dataoffsethigh` = 0x80000000 on this card (PCIe core 0x83c: bit 31 of the high word selects the host's 64 bit PCIe address space) |

Writing a descriptor (`dma64_dd_upd`, P52) stores, in this order,
addrlow, addrhigh, ctrl1, ctrl2 (then possibly ctrl2 again with the
parity bit). A descriptor that has been reclaimed gets 0xdeadbeef in
addrlow and addrhigh (ctrl1/ctrl2 are left as they were). `txinit`/`rxinit`
clear the whole ring to 0.

Indices: `NEXT(i) = (i + 1) & (n - 1)`, `PREV(i) = (i - 1) & (n - 1)`,
`ACTIVE(in, out) = (out - in) & (n - 1)` with `n` = `ntxd` or `nrxd` (a power
of two; the code does not check). One descriptor is always left free: the
ring is full when `NEXT(txout) == txin`, and `txavail` = `ntxd - 1 -
ACTIVE(txin, txout)`.

Index <-> register value: the pointer register is written with
`base_low + 16 * index` (`base_low` = `xmtptrbase` / `rcvptrbase`, the low 32
bits of the ring's bus address when `aligndesc_4k` = 0; 0 otherwise). A
status field is converted back with `index = (((value & mask) - base_low) &
mask) >> 4` (mask = `xs0_cd_mask`, `xs1_ad_mask` or `rs0_cd_mask`), kept to 16
bits. Exceptions are listed with each procedure (`dma_txcommitted` uses
`(XP >> 4) & 0xffff` without subtracting the base).

### D7. Parameters used by the driver on this card

`dma_attach` arguments as made by `wlc_bmac_attach` (values seen in the
session, `attach_dump.txt`):

| FIFO | txregs | rxregs | ntxd | nrxd | rxbufsize | rxextheadroom | nrxpost | rxoffset |
|---|---|---|---|---|---|---|---|---|
| 0 | D11+0x200 | D11+0x220 | 1024 | 1024 | 2048 | 0 | 256 | 40 |
| 1 | D11+0x240 | 0 | 1024 | 0 | 0 | -1 | 0 | 0 |
| 2 | D11+0x280 | 0 | 1024 | 0 | 0 | -1 | 0 | 0 |
| 3 | D11+0x2c0 | 0 | 1024 | 0 | 0 | -1 | 0 | 0 |

`osh` = `wlc_hw+0x10`, `name` = `"wl%d"` with the unit (`"wl0"`), `sih` =
`wlc_hw+0xb8`, `msg_level` = `&wl_msg_level`. `ntxd`, `nrxd`, `rxbufsize`,
`nrxpost` are words 0..3 of the tunables (`pub+0x38`); `wlc_tunables_override`
sets ntxd = nrxd = 0x400, nrxbufpost = 0x100 and word 14 (`rxbnd`, the receive
bound per DPC run) = 0x20 for device ids 0x43a0..0x43a2, 0x43a3..0x43a5,
0x43b1..0x43b3 when the MAC revision is > 41 and != 44 (so for this card);
`rxbufsize` 2048 is the default. `rxoffset` = `wlc+0x6bc` (40 for MAC
revision >= 40: the receive status header the core writes in front of each
frame; its contents belong to `mac-frames`). MAC revisions < 11 (not this
card) use other register offsets (+0x210 for the FIFO-0 receive channel,
+0x220/+0x240/+0x260 for FIFOs 1..3); revision 4 also gives FIFO 3 a receive
channel. **Correction to bmac-init.md D2 step 13.6**, which lists
+0x200/+0x220/+0x240/+0x260: for revision 42 the transmit channels are at
+0x200, +0x240, +0x280, +0x2c0 and the only receive channel at +0x220.

Burst/prefetch fields after `sub_069659` (PCI bus, chip 0x4360/0x4352/0x4350/
0xa9c4, MAC rev >= 32), the same for all four engines: txmultioutstdrd 1,
txprefetchctl 3, txprefetchthresh 3, txburstlen 6, rxprefetchctl 3,
rxprefetchthresh 3, rxburstlen 3. Resulting control registers after init:
transmit `0x03780841`, receive `0x036c0851`.

What `dma_attach` detects on this card (model values; the real card may
differ, see Open questions): `dma64` = 1, `addrext` = 1, `aligndesc_4k` = 0,
`dmadesc_align` = 4 (16 bytes), masks 0xf in the model (0x1fff or `ptr |
0xf` on hardware), `ddoffsetlow` = `dataoffsetlow` = 0, `ddoffsethigh` =
`dataoffsethigh` = 0x80000000, all control flags 0.

### D8. Field use outside hnddma.c

Checked dynamically (memory hook over every engine's 0x130 bytes for a whole
session: attach, up, 4 channel changes, watchdog, scan with 22 transmitted
probe requests, watchdog, down, up, down, detach; `watch.py`, result
`watch.txt`) and statically (every call through a function table with a slot
offset in all decompiled functions, `staticscan.py`; each DMA user read by
hand). The only accesses to engine memory by code outside `hnddma.c`:

| Offset | Size | Field | Reader | How |
|---|---|---|---|---|
| 0x00 | 8 | `di_fn` | every caller of a slot | load of the table pointer before each call |
| 0x08 | 4 | `txavail` | `wlc_prep_pdu` (wlc.c) | through the pointer returned by slot 0x108 (`getvar("&txavail")`): `wlc_bmac_attach` stores it at `wlc_hw+0x120+8*fifo`; the same pointers are found at `wlc->core+0x08+8*fifo` (`wlc+0x38`) after attach (copied by the upper layer; the writer was not identified); `wlc_prep_pdu` compares `*txavail` with the number of segments of the frame (doubled when `wlc+0x72c` is set) and refuses the frame when it is not greater |
| 0x10 | 4 | `rxgiants` | `wlc_statsupd` (wlc.c) | read directly, added to `pub->_cnt` (`pub+0xa0`) +0x68 (`rxgiant`) |
| 0x14 | 4 | `rxnobuf` | `wlc_statsupd` | added to `_cnt`+0x50 (`rxnobuf`) |
| 0x18 | 4 | `txnobuf` | `wlc_statsupd` | added to `_cnt`+0x20 (`txnobuf`), then slot 0x110 clears the three counters |

(`wlc_statsupd` is not reached in the emulated session; its reads were found
statically.) Nobody outside `hnddma.c` writes engine memory. No other field
is read: an open engine must keep these five fields at these offsets (the
public part `hnddma_t`), everything from 0x1c on is private. The handles
themselves are kept in `wlc_hw+0x20+8*fifo` and in the array at
`*(wlc_hw+0x08)` (= `wlc+0x28`, `wlc->hw->di[]`), both written by
`wlc_hw_set_di`.

Callers of slots outside `hnddma.c` (static scan, the session's counts in
brackets): `wlc_bmac_attach` 0x108 [4], 0x118 (only chip 0x4331);
`sub_069659` 0x160 (other chips), 0x170 [28]; `wlc_bmac_corereset` 0x10
[20]; `sub_0638a5` 0xa8 [5] (0xb0 only MAC rev < 12); `wlc_bmac_init` 0x08
[8], 0xa0 [2], 0xd8 [2]; `sub_05fefa` 0x60 [16], 0xe0 [4];
`wlc_bmac_tx_fifo_suspend` 0x20 [12]; `wlc_bmac_tx_fifo_resume` 0x28 [48];
`wlc_bmac_tx_fifo_suspended` 0x30; `wlc_bmac_txfifo` 0x40; `wlc_txfifo`
0x40 [22]; `wlc_bmac_recv` 0xd0, 0xd8; `wlc_bmac_watchdog` 0xd8 [8] (FIFO 3
also for MAC rev 4); `sub_064093` 0xd0, 0xd8 (FIFO 3 receive, MAC rev 4
only); `wlc_dpc` 0xd8; `wlc_dotxstatus` 0x68 (0x68 with range 1, 0x80, 0x90, 0x10,
0x08 only on its recovery path for MAC revs 5 and 6);
`wlc_ampdu_dotxstatus` 0x68; `sub_050e35` 0x68; `wlc_recover_pkts` 0x68,
0xd0; `wlc_statsupd` 0x110; `wlc_bmac_iovars_dispatch` 0x168;
`wlc_bmac_detach` 0x00 [4]. Slots no caller uses on this path: 0x18, 0x38,
0x48, 0x50, 0x58, 0x70, 0x78, 0x88, 0x98, 0xb8, 0xc8, 0xf0, 0xf8, 0x100,
0x138, 0x140, 0x148, 0x150, 0x158, 0x178, 0x180 (they are still described,
for completeness of the table). Inside `hnddma.c` the engine calls its own
functions directly, except `dma_attach`, which calls `dma_ctrlflags` through
the table (slot 0x118).

## Procedures

Order: attach and allocation (P1-P5), channel control and state machine
(P6-P22), transmit (P23-P35), receive (P36-P43), accessors (P44-P51),
internal helpers (P52-P53), the parameter function of `wlc_bmac.c` (P54). Conventions: `XC`, `XP`, `XB` (`+0x08` low,
`+0x0c` high), `XS0`, `XS1` = the transmit channel registers at `txregs+0x00,
+0x04, +0x08/+0x0c, +0x10, +0x14`; `RC`, `RP`, `RB`, `RS0`, `RS1` = the same at
`rxregs`. All register accesses are 32 bit (`osl_readl`/`osl_writel`).
"Poll(cond)" = the inlined SPINWAIT used by the engine: a counter starts at
10009; loop { read; if cond or counter == 9: stop; counter -= 10;
`osl_delay(10)` } - at most 1001 reads and 1000 delays of 10 us.

### P1. dma_attach (.text+0x010380, name original)

Purpose: create and probe one engine.

Prototype: `dma_info_t *dma_attach(osh, const char *name, si_t *sih, void
*txregs, void *rxregs, uint ntxd, uint nrxd, uint rxbufsize, int
rxextheadroom, uint nrxpost (16 bit used), uint rxoffset (8 bit used),
uint *msg_level)` - arguments 7..12 on the stack. Result: the engine, or
NULL (nothing left allocated).

Steps:

1. `di = osl_malloc(osh, 0x130)`; NULL -> return NULL. `osl_memset(di, 0,
   0x130)`.
2. `msg_level` (+0x20) = the argument, or the address of a default word
   (.bss+0x2d0) when it is NULL.
3. `flags = si_core_sflags(sih, 0, 0)` (one read of `WRAP(d11, 0x500)` with
   the D11 core current, spec chip); `dma64` (+0x40) = bit 12 of it. Store
   `rxregs` (+0x50), the table (+0x00: `dma64proc` if `dma64`, else the 32 bit
   table, D5) and `txregs` (+0x48).
4. Through the table: `ctrlflags(di, mask 3, value 0)` (slot 0x118, P46): the
   flags stay 0; no register access since the parity bit is not requested.
5. `osl_strncpy(di+0x28, name, 8)`; byte +0x2f = 0. `sih` (+0x38), `ntxd`
   (+0x6a, 16 bit), `nrxd` (+0xa4, 16 bit), `osh` (+0x30).
6. `rxextrahdrroom` (+0xe8) = 0xcc if `rxextheadroom` == -1, else the
   argument. `rxbufsize` (+0xe4, 16 bit) = `rxbufsize - rxextrahdrroom` if
   `rxbufsize` > 0xcc (unsigned), else `rxbufsize`. `nrxpost` (+0xec) = the
   argument's low 16 bits; `rxoffset` (+0xf0) = its low 8 bits.
7. If `rxregs` != 0:
   1. if `dma64`: write 0xffffffff to `RB` low (`rxregs+0x08`), read it
      back; if `(value & 0xfff) == 0` then mask = 0x1fff, else mask = read
      of `RP` (`rxregs+0x04`) OR 0xf. `rs0_cd_mask` (+0x124) = mask.
   2. three reads of `RC`: `rxburstlen` (+0x105) = bits 18..20 of the
      first, `rxprefetchctl` (+0x10a) = bits 21..23 of the second,
      `rxprefetchthresh` (+0x10b) = bits 24..25 of the third.
8. If `txregs` != 0:
   1. if `dma64`: the same probe on `XB` low / `XP`; the mask goes to both
      `xs0_cd_mask` (+0x11c) and `xs1_ad_mask` (+0x120).
   2. four reads of `XC`: `txburstlen` (+0x106) bits 18..20,
      `txmultioutstdrd` (+0x107) bits 6..7, `txprefetchctl` (+0x108) bits
      21..23, `txprefetchthresh` (+0x109) bits 24..25.
9. Address offsets: `ddoffsetlow` (+0xf4) = `dataoffsetlow` (+0xfc) = 0. If the
   bus type (`sih+0x04`) is 1 (PCI):
   * bus core id (`sih+0x08`) 0x83c or 0x820 (PCIe) and `dma64`:
     `ddoffsetlow` = 0, `ddoffsethigh` (+0xf8) = 0x80000000. **This card.**
   * otherwise: `ddoffsetlow` = 0x80000000 for chip ids 0x10f6, 0x4322,
     0xa8d5, 0xa8df, 0xa867, 0xa868, 0xa8d6, else 0x40000000;
     `ddoffsethigh` = 0.
   then `dataoffsetlow` = `ddoffsetlow`, `dataoffsethigh` (+0x100) =
   `ddoffsethigh`. (Other bus types: all four stay 0.)
10. Address extension (+0x41): 0 if the current core (`si_coreid`) is 0x829
    with `si_corerev` 1 or 2, or 0x834 with revision 0 or 1 (not this card:
    the current core is the D11, 0x812, so `si_coreid` is called twice and
    `si_corerev` never). Otherwise, if neither `txregs` nor `rxregs` is
    given: 0. Otherwise, with `dma64`: run the probe `dma64_addrext` (P53) on
    the control register of `txregs` (of `rxregs` when `txregs` is 0) and
    set `addrext` = 1 **whatever the probe returns**; without `dma64`: the
    32 bit probe's result.
11. Ring alignment: with `dma64` and at least one channel: write 0xff0 to
    `XB` low (or `RB` low when there is no transmit channel), read it back;
    `aligndesc_4k` (+0x104) = 1 if it reads 0, else 0. Without `dma64` or
    without channels: 1. Then `dmadesc_align` (+0x68) = 4 if `aligndesc_4k`
    = 0; else 0xd if `dma64` and (`nrxd` > 255 or `ntxd` > 255), else 0xc.
12. If `ntxd` != 0: `txp` (+0x70) = `osl_malloc(osh, 8*ntxd)`, zeroed
    (`osl_memset`); failure -> step 15. The same for `rxp` (+0xb0) with
    `nrxd`.
13. If `ntxd` != 0: `dma_alloc(di, 1)` (P3); failure -> step 15. If `nrxd`
    != 0: `dma_alloc(di, 2)`; failure -> step 15.
14. Return `di` if `ddoffsetlow` == 0 (this card) or `addrext` = 1; else
    only if `txdpa` (+0x88) and `rxdpa` (+0xc8) are both <= 0x40000000
    (a 1 GB PCI window), otherwise step 15.
15. Failure: `dma_detach(di)` (P5, frees whatever was allocated), return
    NULL.

Register accesses of one call on this card (FIFO 0, with both channels;
FIFOs 1..3 have only the transmit part): `R WRAP(0x500)`; `W RB.lo
0xffffffff`, `R RB.lo`, [`R RP`], `R RC` x3; `W XB.lo 0xffffffff`, `R XB.lo`,
[`R XP`], `R XC` x4; `R XC`, `W XC |0x30000`, `R XC`, `R XC`, `W XC
&~0x30000`; `W XB.lo 0xff0`, `R XB.lo`. Notes: the probes leave `XB.lo` =
0xff0 and `RB.lo` = 0xffffffff (or whatever the hardware kept) until the
rings are programmed by `txinit`/`rxinit`. `txp_dmah`/`rxp_dmah` (+0x80,
+0xc0) are never allocated on this build, and `tx_dmah`/`rx_dmah` (+0x78,
+0xb8) stay 0 (P4).

### P2. dma_addrwidth (.text+0x00ffbc, name original)

Purpose: the DMA address width the core supports, for the OS layer.
Prototype: `uint dma_addrwidth(si_t *sih, void *dmaregs)`. Called once by
`wlc_bmac_attach` with the FIFO-0 transmit registers (D11+0x200); the
result goes to `wl_alloc_dma_resources(wl, width)`, which ignores it in the
open glue (returns TRUE).

Steps: `osh = si_osh(sih)`; `flags = si_core_sflags(sih, 0, 0)` (`R
WRAP(0x500)`).

* flag 0x1000 set (64 bit DMA): return 64 if `si_backplane64(sih)` (bit
  27 of the ChipCommon capabilities) and (bus type 0 or (bus type 1 and bus
  core 0x83c or 0x820)); else 32. **This card: 32 in the model (the
  capability bit is 0 there), 64 if the real chip has a 64 bit backplane
  (unverified).**
* flag clear: if the bus type is not 0 and not (PCI with a PCIe core) and the
  32 bit address-extension probe on `dmaregs` fails: 30; else 32.

### P3. sub_01010c = dma_alloc (assigned)

Purpose: allocate one ring. Prototype: `bool dma_alloc(di, uint direction)`
(1 = transmit, 2 = receive). Result 1 = done, 0 = no memory.

Steps (64 bit; `n` = `ntxd` or `nrxd`, `size` = 16*n):

1. `alignbits` (local 16 bit) = `dmadesc_align`; `alloced` (local) = 0.
2. `boundary` = 0x2000 if the channel's mask (`xs0_cd_mask` for transmit,
   `rs0_cd_mask` for receive) == 0x1fff, else 0x10000.
3. `va = dma_ringalloc(osh, boundary, size, &alignbits, &alloced,
   &dpaorig, &dmah)` (P4) with `dpaorig` = +0x90 / +0xd0 and `dmah` = +0x78
   / +0xb8. NULL -> return 0.
4. `align = 1 << alignbits` (alignbits as possibly changed by P4);
   `dpa` (+0x88 / +0xc8) = `dpaorig` rounded up to a multiple of `align`
   (64 bit); `dalign` (+0x98 / +0xd8, 16 bit) = `dpa - dpaorig`; ring
   virtual address (+0x58 / +0x60) = `va + dalign`; `dalloc` (+0x9c /
   +0xdc) = `alloced`. Return 1.

(32 bit engine: boundary 0x1000, size 8*n, the virtual address is rounded
and the bus address follows it - not used on this card.)

### P4. sub_01005b = dma_ringalloc (assigned)

Purpose: get coherent memory for a ring that does not cross a `boundary`.
Prototype: `void *dma_ringalloc(osh, uint boundary, uint size, u16
*alignbits, uint *alloced, ulong *pa, void **dmah)` (`dmah` is not used).

Steps:

1. `va = osl_dma_alloc_consistent(osh, size, *alignbits, alloced, pa)`;
   NULL -> return NULL.
2. `start` = the low 32 bits of `*pa` rounded up to `1 << *alignbits` (32
   bit arithmetic). If `((start + size - 1) ^ start) & boundary` is 0:
   return `va`.
3. Otherwise the ring would straddle a `boundary`-aligned line: `*alignbits`
   = floor(log2(`size`)) (the number of right shifts until `size` becomes 0,
   minus one), `osl_dma_free_consistent(osh, va, size, *pa)` (note: `size`,
   not `*alloced`), and return `osl_dma_alloc_consistent(osh, size,
   *alignbits, alloced, pa)` (may be NULL).

(The open OSL's `osl_dma_alloc_consistent` adds `1 << align_bits` to the size
when the page size is not a multiple of it, and writes the size into
`*alloced`.)

### P5. sub_00fecf = dma_detach (assigned, slot 0x000)

Purpose: free an engine. Prototype: `void dma_detach(di)`. No register
access.

Steps: if `txd` != 0: `osl_dma_free_consistent(osh, txd - txdalign,
txdalloc, txdpaorig)`; if `rxd` != 0: the same with `rxd`, `rxdalign`,
`rxdalloc`, `rxdpaorig`; if `txp` != 0: `osl_mfree(osh, txp, 8*ntxd)`; if
`rxp` != 0: `osl_mfree(osh, rxp, 8*nrxd)`; if `txp_dmah` != 0:
`osl_mfree(osh, txp_dmah, 0x90*ntxd)`; if `rxp_dmah` != 0: the same with
`nrxd`; finally `osl_mfree(osh, di, 0x130)`. Packets still referenced by
`txp`/`rxp` are not freed (the driver reclaims them before, see
`sub_05fefa`).

### P6. sub_00f947 = dma64_txinit (assigned, slot 0x008)

Purpose: (re)start the transmit channel with an empty ring. Prototype:
`void (di)`. Nothing if `ntxd` == 0.

Steps:

1. `xs0cd` (+0x12a) = 0, `txout` = 0, `txin` = 0; `txavail` = `ntxd - 1`;
   `osl_memset(txd, 0, 16*ntxd)`.
2. Read `XC`; write `XC` = `(old & 0xffe3ff3f) | txburstlen << 18 |
   txmultioutstdrd << 6`, then with bits 21..23 replaced by `txprefetchctl`
   and bits 24..25 replaced by `txprefetchthresh` (one write; the fields are
   ORed after clearing, the fields are not masked to their width).
3. If `aligndesc_4k` = 0: `dma_ddtable_init(di, 1, txdpa)` (P11) now.
4. Read `XC`; write `XC | 1 | (0x800 unless control flag 0x01)` (enable;
   bit 11 = parity disable).
5. If `aligndesc_4k` = 1: `dma_ddtable_init(di, 1, txdpa)` now (after the
   enable).

On this card: `R XC`, `W XC 0x03780040` (from 0), `W XB.lo txdpa`, `W XB.hi
0x80000000`, `R XC`, `W XC 0x03780841`. The pointer register is not written:
`XP` keeps its old value (0 after a reset) until the first post.

### P7. sub_00f64a = dma64_txreset (assigned, slot 0x010)

Purpose: stop the transmit channel. Prototype: `bool (di)`; result 1 = the
channel reports "disabled". `ntxd` == 0 -> return 1, no access.

Steps:

1. Write `XC` = 2 (suspend request; enable and all other fields cleared).
2. Poll(`XS0` bits 28..31 is 0 (disabled), 2 (idle) or 3 (stopped)).
3. Write `XC` = 0.
4. Poll(`XS0` bits 28..31 == 0); if this poll ends by the counter (state
   still not 0): `osl_delay(300)`.
5. Return (last state read == 0).

On this card (model: status reads 0): `W XC 2`, `R XS0`, `W XC 0`, `R XS0`.

### P8. sub_00f897 = dma_rxinit (assigned, slot 0x0a0)

Purpose: (re)start the receive channel with an empty ring. Prototype: `void
(di)`. Nothing if `nrxd` == 0.

Steps (64 bit): `rs0cd` (+0x128) = 0, `rxout` = 0, `rxin` = 0;
`osl_memset(rxd, 0, 16*nrxd)`; if `aligndesc_4k` = 0:
`dma_ddtable_init(di, 2, rxdpa)` then `dma_rxenable(di)` (P10); if
`aligndesc_4k` = 1: `dma_rxenable` first, then `dma_ddtable_init`. (32 bit:
memset of 8*nrxd, `dma_rxenable`, then `dma_ddtable_init`.) No buffers are
posted here: the caller follows with `dma_rxfill`.

On this card: `W RB.lo rxdpa`, `W RB.hi 0x80000000`, `R RC`, `W RC
0x036c0851`.

### P9. sub_00f5ef = dma64_rxreset (assigned, slot 0x0a8)

Purpose: stop the receive channel. Prototype: `bool (di)`; `nrxd` == 0 ->
return 1. Steps: write `RC` = 0; Poll(`RS0` bits 28..31 == 0); return (last
state == 0). No extra delay on timeout. On this card: `W RC 0`, `R RS0`.

### P10. sub_00e5f0 = dma_rxenable (assigned, slot 0x0c0)

Purpose: program and enable the receive control register. Prototype: `void
(di)`.

Steps: read `RC`; keep only its bits 16..17 (address extension); `v = kept
| 1`, `| 0x800` unless control flag 0x01 (parity disable), `| 0x400` if
control flag 0x02 (overflow continue); `v |= rxburstlen << 18`; replace
bits 21..23 by `rxprefetchctl`; clear bits 24..25; write `v | rxprefetchthresh
<< 24 | rxoffset << 1` (bits 1..7: the receive offset in bytes, not masked).
With rxoffset 40 and the fields of D7: `0x036c0851`.

### P11. sub_00e66f = dma_ddtable_init (assigned)

Purpose: give the hardware the ring's base address. Prototype: `void
(di, uint direction, ulong pa)` (1 = transmit, else receive; only the low
32 bits of `pa` are used). Registers `B` = `XB`/`RB` of that direction.

Steps (64 bit):

1. If `aligndesc_4k` = 0: `xmtptrbase` (+0xa0) or `rcvptrbase` (+0xe0) =
   `pa` (low 32 bits).
2. If `ddoffsetlow` == 0 or bits 30..31 of `pa` are 0 (always on this card):
   write `B.lo` = `pa + ddoffsetlow`, then `B.hi` = `ddoffsethigh`. Done.
3. Otherwise: write `B.lo` = `(pa & 0x3fffffff) + ddoffsetlow`, `B.hi` =
   `ddoffsethigh`, then read the control register of that direction and
   write it back with bits 16..17 = `pa >> 30`.

(32 bit: one write of the ring address to `+0x04`, with the same
address-extension variant.)

### P12. sub_00e448 = dma64_txenabled (assigned, slot 0x018)

Read `XC`; return 0 if it reads 0xffffffff (card gone), else bit 0.

### P13. sub_00e992 = dma64_txsuspend (assigned, slot 0x020)

If `ntxd` != 0: read `XC`, write it back with bit 1 set (suspend request).
Called by `wlc_bmac_tx_fifo_suspend` (section 2).

### P14. sub_00e965 = dma64_txresume (assigned, slot 0x028)

If `ntxd` != 0: read `XC`, write it back with bit 1 cleared. (The decompiler
shows a garbage return value; the function returns nothing useful.)

### P15. sub_00e428 = dma64_txsuspended (assigned, slot 0x030)

Return 1 if `ntxd` == 0 (no access); else read `XC` and return bit 1. Note:
this is the *request* bit, not the hardware state; `wlc_bmac_tx_fifo_suspended`
combines it with `D11(0x150)` (section 2).

### P16. sub_00e3e2 = dma64_txsuspendedidle (assigned, slot 0x038)

Return 1 if `ntxd` == 0. Else read `XC`; if bit 1 is clear return 0; else read
`XS0` and return (bits 28..31 == 2, idle). (The 32 bit variant reads the
status twice with a 2 us delay; the 64 bit one reads once.)

### P17. sub_00e318 = dma64_txstopped (assigned, slot 0x058)

Read `XS0`; return (bits 28..31 == 3, stopped). No `ntxd` check.

### P18. sub_00e140 = dma64_rxidle (assigned, slot 0x0b0)

Return 1 if `nrxd` == 0. Else read `RS0`, then `RP`; return 1 if `(RS0 ^ RP)
& rs0_cd_mask` == 0 (the hardware's current descriptor has reached the end of
the posted buffers). Also used by `dma_rxfill` (P36).

### P19. sub_00e120 = dma64_rxstopped (assigned, slot 0x0b8)

Read `RS0`; return (bits 28..31 == 3).

### P20. sub_00e103 = dma64_rxenabled (assigned, slot 0x0c8)

Read `RC`; return 0 if it reads 0xffffffff, else bit 0.

### P21. sub_00e5c9 = dma64_fifoloopbackenable (assigned, slot 0x100)

Read `XC`, write it back with bit 2 set (loopback). (The named function
`dma_txpioloopback` at .text+0x00ea18 does the same on a register address
given as its second argument; nobody calls it on this path.)

### P22. sub_00e4ff = dma_glom_enable (assigned, slot 0x178)

Prototype: `bool (di, uint enable)`. `enable` == 0: read `RC`, write it back
with bit 14 cleared, return 1. Otherwise: read `RC`, write it back with bit 14
set, read `RC` again and return its bit 14 (whether the hardware has the
"glom" feature). Not called on this path.

### P23. sub_00fafd = dma64_txfast (assigned, slot 0x040)

Purpose: post one frame (a chain of packet buffers linked by `next`) for
transmission. Prototype: `int (di, void *p0, bool commit)`. Result 0, or -1
when the ring is full (the frame is then freed).

Steps (`out` = a local copy of `txout`; `flags` = a local, initially 0):

1. For each buffer `p` of the chain, starting with `p0`:
   1. `data = osl_pktdata(osh, p)`, `len = osl_pktlen(osh, p)`, `next =
      osl_pktnext(osh, p)` (in this order).
   2. If `NEXT(out) == txin`: ring full -> step 4.
   3. If `len` == 0: skip this buffer (no descriptor).
   4. `pa = osl_dma_map(osh, data, len, 1 /* to device */, p, txp_dmah +
      0x90*out)` (`txp_dmah` is 0: the last argument is a meaningless
      address; the Linux OSL ignores it).
   5. `flags` = 0x80000000 (SOF) if `p == p0`, else 0; `| 0x60000000`
      (EOF|IOC) if `next` == NULL; `| 0x10000000` (EOT) if `out == ntxd-1`.
   6. `dma64_dd_upd(di, txd, pa, out, &flags, len)` (P52).
   7. `out = NEXT(out)`. (The code then checks `out == txin` -> step 4; this
      cannot happen after the check of 1.2.)
2. After the chain: if `flags` lacks EOF (0x40000000) - the last buffer of
   the chain was empty, or there was no non-empty buffer at all - write
   ctrl1 of descriptor `PREV(out)` = `flags | 0x60000000` (with `flags` = 0
   when nothing was written this call: then it overwrites ctrl1 of the
   previous frame's last descriptor; parity is not recomputed).
3. `txp[PREV(out)] = p0` (the packet pointer is kept at the frame's last
   descriptor); `txout = out`; if `commit`: write `XP = 16*txout +
   xmtptrbase`. `txavail = ntxd - 1 - ACTIVE(txin, txout)`. Return 0.
4. Ring full: `osl_pktfree(osh, p0, 1)` (the whole chain, "sent" = 1 so the
   OS transmit-done callback runs), `txnobuf` (+0x18) += 1, `txavail` = 0,
   return -1. Descriptors already written for this frame stay in the ring
   but `txout` is not advanced, so the hardware never sees them.

In the scan the 22 probe requests are single-buffer frames: one descriptor
each with ctrl1 = 0xe0000000 (0xf0000000 at index 1023), ctrl2 = the frame
length, addrlow = the buffer's bus address, addrhigh = 0x80000000, then one
write of `XP` (D11+0x2c4 for FIFO 3).

### P24. sub_00f3e6 = dma64_txunframed (assigned, slot 0x048)

Purpose: post one raw buffer (not a packet) as one frame. Prototype: `int
(di, void *buf, uint len, bool commit)`.

Steps: if `NEXT(txout) == txin`: `txnobuf` += 1, `txavail` = 0, return -1.
If `len` == 0: return 0 (nothing posted). `pa = osl_dma_map(osh, buf, len,
1, NULL, txp_dmah + 0x90*txout)`; `flags` = 0xe0000000 (SOF|EOF|IOC), 0xf0000000
at index `ntxd-1`; `dma64_dd_upd(di, txd, pa, txout, &flags, len)`; `txp[txout]
= buf`; `txout = NEXT(txout)`; if `commit`: write `XP = 16*txout +
xmtptrbase`; `txavail` updated; return 0. Not called on this path.

### P25. sub_00ee0d = dma64_getnexttxp (assigned, slot 0x068)

Purpose: reclaim the descriptors of one transmitted frame and return its
packet. Prototype: `void *(di, int range)` (D3). Result: the packet, or
NULL.

Steps:

1. `ntxd` == 0 -> return NULL. `start = txin`.
2. `end`:
   * `range` == 1: `end = txout` (no register access).
   * otherwise: `end = xs0cd` (+0x12a, the cached current descriptor); only
     if `start == xs0cd`: read `XS0`, `end = index(XS0, xs0_cd_mask)` (D6),
     store it in `xs0cd`. Then, only for `range` == 3: read `XS1`, `ad =
     ((((XS1 & xs1_ad_mask) - xmtptrbase) & xs0_cd_mask) & 0xffff) >> 4`; if
     `ad != end`: `end = PREV(ad)` (the descriptor before the one the
     hardware is fetching).
3. If `start == 0` and `end > txout`: return NULL (no state change; guards
   against a stale status after a reset).
4. `p` = NULL; `i = start`; while `p` == NULL and `i != end`:
   1. read the descriptor's addrlow and ctrl2; write 0xdeadbeef to its
      addrlow and addrhigh;
   2. `p = txp[i]`; `txp[i] = NULL`;
   3. `osl_dma_unmap(osh, addrlow - dataoffsetlow, ctrl2 & 0x7fff, 1)`;
   4. `i = NEXT(i)`.
5. `txin = i`; `txavail = ntxd - 1 - ACTIVE(txin, txout)`; return `p`.

So one call frees the descriptors from `txin` up to and including the first
one that carries a packet (the last descriptor of a frame, P23); if none of
them carries one, all descriptors up to `end` are freed and NULL is
returned. `wlc_dotxstatus` uses range 2 (only what the hardware has
finished); the flush uses range 1 through `dma64_txreclaim`.

### P26. sub_00f098 = dma64_txreclaim (assigned, slot 0x060)

Prototype: `void (di, int range)`. If `txin != txout` (16 bit compare):
repeat `p = dma64_getnexttxp(di, range)` until it returns NULL, and free each
`p` with `osl_pktfree(osh, p, 1)` unless control flag 0x08 is set. (With
range 1 and an empty ring nothing happens; with range 1 every posted frame is
freed whatever the hardware did.) No register access with range 1.

### P27. sub_00e294 = dma64_peeknexttxp (assigned, slot 0x070)

Return NULL if `ntxd` == 0. Read `XS0`, `end = index(XS0, xs0_cd_mask)`, store
it in `xs0cd`; return the first non-NULL `txp[i]` for `i` from `txin` up to
(not including) `end`, else NULL. Nothing is changed besides `xs0cd`.

### P28. sub_00e184 = dma64_peekntxp (assigned, slot 0x078)

Prototype: `int (di, int *n, void *pkts[], int range)`. List up to `*n`
posted packets without reclaiming them.

Steps: if `ntxd` == 0: `*n` = 0, return -1. `max = *n`; `*n = 0`. `end`: for
`range` == 1 `txout`; otherwise read `XS0` and `XS1` (both, every time),
`cd = index(XS0, xs0_cd_mask)` (stored in `xs0cd`), `ad = (((XS1 & xs1_ad_mask)
- xmtptrbase) & xs0_cd_mask) >> 4`, `end = cd` if `ad == cd` (16 bit compare)
else `PREV(ad)` (unlike P25 this is done for every range other than 1). If
`txin == 0` and `end > txout`: return -1 (`*n` stays 0). Walk `i` from `txin`
to `end`: for each non-NULL `txp[i]`: stop if `count >= max`, else
`pkts[count++] = txp[i]`. `*n = count`; return 0.

### P29. sub_00e338 = dma64_getpos (assigned, slot 0x050)

Prototype: `void *(di, bool tx)`. `tx` == 1: read `XS0`, `idx = index(XS0,
xs0_cd_mask)`, return `txp[idx]` if `ACTIVE(txin, txout)` != 0, else NULL.
Otherwise the same with `RS0`, `rs0_cd_mask`, `rcvptrbase`, `rxp` and
`ACTIVE(rxin, rxout)`. (The array element is read in both cases.)

### P30. sub_00ddcb = dma_txblock (assigned, slot 0x080)

`txavail` = 0 (the upper layer then sees the FIFO as full).

### P31. sub_00ddd8 = dma_txunblock (assigned, slot 0x088)

`txavail = ntxd - 1 - ACTIVE(txin, txout)`.

### P32. sub_00ddf5 = dma_txactive (assigned, slot 0x090)

Return `ACTIVE(txin, txout)` (descriptors posted and not reclaimed).

### P33. sub_00dfc6 = dma_txpending (assigned, slot 0x140)

Read `XS0`; `cd = index(XS0, xs0_cd_mask)`, stored in `xs0cd`; return
`(txout - cd) & (ntxd - 1)` (posted descriptors the hardware has not
reached).

### P34. sub_00df72 = dma_txcommitted (assigned, slot 0x148)

If `txin == txout` return 0 (no access). Else read `XP` and return `(((XP >>
4) & 0xffff) - txin) & (ntxd - 1)`. Note: the base is **not** subtracted
(correct only when the ring is aligned so that the base's bits 4..15 are 0,
i.e. with `aligndesc_4k` = 1, or by chance).

### P35. sub_00e7c1 = dma64_txrotate (assigned, slot 0x098)

Purpose: after the hardware was stopped in the middle of the ring, move the
pending descriptors so that they start at the hardware's active position.
Prototype: `void (di)`.

Steps:

1. `nactive = ACTIVE(txin, txout)`; read `XS1`; `ad = (((XS1 & xs1_ad_mask)
   - xmtptrbase) & xs1_ad_mask) >> 4` (16 bit); `rot = (ad - txin) & (ntxd -
   1)`. If `rot >= ntxd - nactive`: return (ring too full; nothing done).
2. For `old` from `PREV(txout)` down to `txin` (stop when `old ==
   PREV(txin)`): `new = (old + rot) & (ntxd - 1)`; ctrl1 of `new` = ctrl1 of
   `old` without EOT, with EOT if `new == ntxd - 1`; copy ctrl2, addrlow,
   addrhigh of `old` to `new`; write 0xdeadbeef to addrlow and addrhigh of
   `old`; `txp[new] = txp[old]`; `txp[old] = NULL`.
3. `txin = ad`; `txout = (txout + rot) & (ntxd - 1)`; `txavail = ntxd - 1 -
   ACTIVE(txin, txout)`; write `XP = 16*txout + xmtptrbase`.

Not called on this path.

### P36. sub_00f14d = dma_rxfill (assigned, slot 0x0d8)

Purpose: post receive buffers until `nrxpost` are outstanding. Prototype:
`bool (di)`. Result: 1 only when no buffer could be allocated at all *and*
the ring is empty (the hardware has nothing to receive into); else 0. There
is no `nrxd` == 0 check (callers only use it on FIFO 0).

Steps:

1. `out = rxout` (local); `n = nrxpost - ACTIVE(rxin, rxout)` (computed once,
   unsigned); `align = 16` if control flag 0x10, else 1; `extra =
   rxextrahdrroom` if `rxbufsize` > 0xcc, else 0 (on this card: 0).
2. For `i` = 0 .. `n-1`:
   1. `p = osl_pktget(osh, rxbufsize + align - 1 + extra)` (2048 on this
      card).
   2. If `p` == NULL: result = 1 if `i` == 0 and `dma64_rxidle(di)` (P18:
      reads `RS0`, `RP`) returns 1, else 0; `rxnobuf` (+0x14) += 1; go to
      step 3.
   3. `shift = 0`; if flag 0x10: `shift = (align - osl_pktdata(osh, p)) &
      (align - 1)` (bytes to the next 16-byte boundary). If `shift + extra`
      != 0: `osl_pktpull(osh, p, shift + extra)`.
   4. Write the 32 bit word 0 at `osl_pktdata(osh, p)` (so that the length
      field of the receive status reads 0 until the hardware writes it).
   5. `pa = osl_dma_map(osh, osl_pktdata(osh, p), rxbufsize, 2 /* from
      device */, p, rxp_dmah + 0x90*out)` (`osl_pktdata` is called again
      here: three calls per buffer when flag 0x10 is set, two otherwise).
   6. `rxp[out] = p`.
   7. `flags` = 0x10000000 (EOT) if `out == nrxd - 1`, else 0;
      `dma64_dd_upd(di, rxd, pa, out, &flags, rxbufsize)` (P52).
   8. `out = NEXT(out)`.
   After the loop (all `n` posted): result = 0.
3. `rxout = out`; write `RP = 16*rxout + rcvptrbase` - **always**, also when
   nothing was posted or the allocation failed. Return the result.

On this card at `wlc_bmac_init`: 256 buffers, descriptors 0..255 = {0,
0x800, buffer bus address, 0x80000000}, then `W RP rcvptrbase + 0x1000`.
At each watchdog with nothing received: `n` = 0, only `W RP` (same value).

### P37. sub_00f504 = dma_rx (assigned, slot 0x0d0)

Purpose: return the next received frame. Prototype: `void *(di)`. Result: a
packet (a chain when the frame spans several buffers and control flag 0x04
is set), or NULL when nothing is completed.

Steps:

1. `head = dma_getnextrxp(di, 0)` (P38); NULL -> return NULL.
2. `len` = the 16 bit little-endian word at `osl_pktdata(osh, head)` (the
   first field of the receive status the hardware wrote; it counts the
   frame bytes after the `rxoffset` bytes of status).
3. `osl_pktsetlen(osh, head, min(rxoffset + len, rxbufsize))`.
4. `resid = len + rxoffset - rxbufsize` (signed). If `resid` <= 0: return
   `head`.
5. `tail = head`; while `resid` > 0: `p = dma_getnextrxp(di, 0)`; NULL ->
   leave the loop; `osl_pktsetnext(tail, p)` (two arguments: packet, next);
   `osl_pktsetlen(osh, p, min(resid, rxbufsize))`; `resid -= rxbufsize`;
   `tail = p`.
6. If control flag 0x04: return `head` (the chain). Otherwise
   `osl_pktfree(osh, head, 0)` (the chain), `rxgiants` (+0x10) += 1, and
   start again at step 1 (look for the next frame).

The caller (`wlc_bmac_recv`, section 2) strips the `rxoffset` bytes
itself; this function does not pull them.

### P38. sub_00ec76 = dma_getnextrxp (assigned, slot 0x0e8)

Purpose: take one buffer back from the ring. Prototype: `void *(di, bool
forceall)`. Result: the packet, or NULL.

Steps (64 bit):

1. `nrxd` == 0 -> NULL. `i = rxin`. If `i == rxout`: NULL (nothing posted).
2. If `i == rs0cd` (+0x128, the cached current descriptor): read `RS0`,
   `rs0cd = index(RS0, rs0_cd_mask)` (D6 with `rcvptrbase`); if `i` equals
   the new value and `forceall` == 0: return NULL (the hardware has not
   finished this buffer).
3. `p = rxp[i]`; `rxp[i] = NULL`; `osl_dma_unmap(osh, addrlow(i) -
   dataoffsetlow, rxbufsize, 2)`; write 0xdeadbeef to addrlow and addrhigh of
   descriptor `i`.
4. `rxin = NEXT(i)`; return `p`.

(The register is read only when the cache says the hardware might not have
passed `rxin`; while the driver walks behind a known hardware position it
reads nothing.)

### P39. sub_00f11a = dma_rxreclaim (assigned, slot 0x0e0)

Prototype: `void (di)`. Repeat `p = dma_getnextrxp(di, 1)` until NULL, and
`osl_pktfree(osh, p, 0)` each. With `forceall` every posted buffer comes
back; one `RS0` read happens when `rxin` equals `rs0cd` (the first call after
`rxinit`, where both are 0, and whenever the walk reaches the cached
value). In the session's down step: 256 buffers freed, 257 calls of P38, one
`R RS0`.

### P40. sub_00e02e = dma_peeknextrxp (assigned, slot 0x0f0)

Return NULL if `nrxd` == 0. Read `RS0`, `end = index(RS0, rs0_cd_mask)`, store
it in `rs0cd`; return the first non-NULL `rxp[i]` for `i` from `rxin` up to
(not including) `end`, else NULL.

### P41. sub_00de0d = dma_rxactive (assigned, slot 0x138)

Return `ACTIVE(rxin, rxout)`.

### P42. sub_00dedc = dma64_activerxbuf (assigned, slot 0x180)

Read `RS0`, then `RP`; return `(index(RP, rs0_cd_mask) - index(RS0,
rs0_cd_mask)) & (nrxd - 1)` (buffers posted that the hardware has not yet
reached). (The decompiler's naming suggests `RS1`; the code reads `rxregs +
0x10` and `rxregs + 0x04`.)

### P43. sub_00ddb3 = dma_rxparam_get (assigned, slot 0x0f8)

Prototype: `void (di, u16 *rxoffset, u16 *rxbufsize)`: store the low 16 bits
of `rxoffset` (+0xf0) and `rxbufsize` (+0xe4).

### P44. sub_00ec49 = dma_getvar (assigned, slot 0x108)

Prototype: `void *(di, const char *name)`. `osl_strcmp(name, "&txavail")`;
equal -> return `di + 8` (the address of `txavail`), else NULL.

### P45. sub_00de2e = dma_counterreset (assigned, slot 0x110)

`rxgiants` = `rxnobuf` = `txnobuf` = 0 (+0x10, +0x14, +0x18; `txnodesc` is not
cleared).

### P46. sub_00e551 = dma_ctrlflags (assigned, slot 0x118)

Prototype: `uint (di, uint mask, uint flags)`. Result: the new flags (0 if
`di` is NULL).

Steps: `new = (dmactrlflags & ~mask) | flags`. If `new` has bit 0 (parity):
read `XC` (the transmit channel's control register, always), write `XC |
0x800`, read `XC`; if bit 11 did not stick: clear bit 0 of `new` (the engine
has no parity control); else write the original `XC` value back. Store `new`
in `dmactrlflags` (+0x0c) and return it. `dma_attach` calls it with (3, 0);
`wlc_bmac_attach` calls it only for chip 0x4331 (flag 0x20); so on this card
the flags stay 0 and the parity probe never runs.

### P47. sub_00de55 = dma_pktpool_set (assigned, slot 0x150)

Store the pointer in `pktpool` (+0x110); return 0. Not used.

### P48. sub_00df44 = dma_rxtxerror (assigned, slot 0x158)

Prototype: `bool (di, bool istx)`. Return 0 if `dma64` = 0. Else read status
1 of the transmit channel (`istx` != 0) or of the receive channel; return
(bits 28..31 != 0).

### P49. sub_00de64 = dma_burstlen_set (assigned, slot 0x160)

Prototype: `void (di, u8 rxburstlen, u8 txburstlen)`: store them at +0x105
and +0x106. Takes effect at the next `txinit`/`rxenable`. Used by
`sub_069659` for other chips only.

### P50. sub_00de49 = dma_avoidancecnt (assigned, slot 0x168)

Return `dma_avoidance_cnt` (+0x118; nothing on this path increments it, so 0).
Used by `wlc_bmac_iovars_dispatch`.

### P51. sub_00de77 = dma_param_set (assigned, slot 0x170)

Prototype: `void (di, u16 id, u16 value)`; stores the low byte of `value`:

| id | field |
|---|---|
| 0 | `txmultioutstdrd` (+0x107) |
| 1 | `txprefetchctl` (+0x108) |
| 2 | `txprefetchthresh` (+0x109) |
| 3 | `txburstlen` (+0x106) |
| 0x100 | `rxprefetchctl` (+0x10a) |
| 0x101 | `rxprefetchthresh` (+0x10b) |
| 0x102 | `rxburstlen` (+0x105) |
| other | nothing |

No register access; the values are used at the next `txinit`/`rxenable`.

### P52. sub_00dcf8 = dma64_dd_upd (assigned)

Purpose: write one descriptor. Prototype: `void (di, dma64dd_t *ring, uint
pa, uint index, uint *flags, uint bufcount)`.

Steps: `ctrl2 = bufcount & 0x7fff`. If `dataoffsetlow` == 0 or `pa & 0xc0000000`
== 0: `addrlow = pa + dataoffsetlow`; else `ctrl2 |= (pa >> 30) << 16` and
`addrlow = (pa & 0x3fffffff) + dataoffsetlow`. Store into `ring[index]`, in
this order: word 2 = `addrlow`, word 3 = `dataoffsethigh`, word 0 = `*flags`,
word 1 = `ctrl2`. If control flag 0x01 (parity) is set: if the XOR of the four
words has odd parity (bit count), store word 1 again with bit 18 (0x40000)
set. (Only memory is written; no register access.)

### P53. sub_00ebf0 = dma64_addrext (assigned)

Prototype: `bool (osh, uint32 *ctrlreg)`. Read the register, write it back
with bits 16..17 set, read it (`w`), read it again, write that value with bits
16..17 cleared; return (`w & 0x30000) == 0x30000`. Five accesses (R, W, R, R,
W). The 32 bit engine's `sub_00e9bf` is identical.

### P54. sub_069659 = wlc_bmac_dma_param_init (assigned, wlc_bmac.c)

Purpose: set the burst/prefetch fields of a new engine. Prototype: `void
(wlc_hw, int bustype, di)`; called by `wlc_bmac_attach` after each
`dma_attach` with the bus type (`sih+0x04`).

Steps:

* bus type 1 (PCI): nothing if the MAC revision (`wlc_hw+0x84`) < 32. Chip id
  (`sih+0x3c`) 0xa9c4, 0x4360, 0x4350 or 0x4352: `dma_param_set` (slot 0x170)
  with ids 0, 1, 2, 3, 0x100, 0x101, 0x102 and the 16 bit words at
  .data+0x340 (Ghidra 0x58eea0), +0x342, +0x344, +0x346, +0x34a, +0x34c,
  +0x34e. Before the first call `wlc_bmac_attach` **writes those words**
  (global data): 1, 3, 3, 6, (0x348 untouched), 3, 3, 3. So: txmultioutstdrd
  1, txprefetchctl 3, txprefetchthresh 3, txburstlen 6, rxprefetchctl 3,
  rxprefetchthresh 3, rxburstlen 3. Other chips: `dma_burstlen_set(di, 3, 3)`
  (slot 0x160).
* bus type 0 (backplane, not this card): MAC revision >= 32 and chip 0xaa06,
  0x4360, 0xa9c4, 0x4352, 0x4350 or 0x4335: ids 0, 1, 2 = 0; id 3 = 1 (chip
  0x4335) or 2; ids 0x100, 0x101 = 0; id 0x102 = 2.
* other bus types: nothing.

(`wlc_bmac_attach` writes the globals only when the bus type is 1 and calls
this function for FIFO 0 with the constant 1; for FIFOs 1..3 it passes the
bus type read again from `sih`.) Seen in the session: 4 engines x 7 calls
with the values above.

## Verification

What was checked, and how (scripts and outputs in
`re-out\analysis\bmac-data\`):

1. **Attach probes and register order** (P1): `re-out\up-trace.txt` seq
   844-909 (four `dma_attach` calls) shows exactly the order and values of
   P1's access list: per engine `R WRAP(0x500)`; for FIFO 0 the receive probe
   first (`W 0x228 ffffffff`, `R 0x228`, `R 0x224`, `R 0x220` x3), then the
   transmit probe (`W 0x208 ffffffff`, `R 0x208`, `R 0x204`, `R 0x200` x4),
   the address-extension probe on `XC` (R, W 0x30000, R, R, W 0), and the
   alignment probe (`W 0x208 0xff0`, `R 0x208`). FIFOs 1..3 the same without
   the receive part. No difference.
2. **Engine contents after attach and after up** (`attach_dump.py` ->
   `attach_dump.txt`): all fields as derived in P1/P3/P6/P8/P36 (masks 0xf
   from the model's plain-memory registers, `aligndesc_4k` 0, `dmadesc_align`
   4, offsets 0/0x80000000, burst fields 6/1/3/3 and 3/3/3, `txavail` 0x3ff
   after init, `rxout` 0x100 after the fill, `xmtptrbase`/`rcvptrbase` = the
   ring bus addresses, `rs0_cd_mask` 0 for transmit-only engines, `txp_dmah`
   etc. 0). The four `dma_attach` argument lists of D7 were read from the
   same run (stack arguments included). The first receive descriptors are
   {0, 0x800, buffer bus address, 0x80000000}, descriptor 256 and above are
   0, and each receive buffer's data starts 256 bytes into its allocation
   (the emulated OSL's head room; no `osl_pktpull` because flag 0x10 and
   `extra` are 0).
3. **Core reset, init, fill** (P6-P10, P36): up-trace seq 914-931 and
   1171-1188 (`W XC 2`, `R XS0`, `W XC 0`, `R XS0` per FIFO; `W RC 0`, `R
   RS0`), seq 24770-24798 (`XC` 0x03780040 -> 0x03780841, `XB` = ring
   address / 0x80000000, `RB`, `RC` 0x036c0851, `RP` = base + 0x1000). These
   values were computed from P6/P10/P36 with the D7 fields before looking
   at the trace lines; they agree. `boundary.py hnddma.c --args` confirms
   the call counts and the `dma_param_set` arguments (ids/values of P54).
4. **Who touches the engine** (D8): `watch.py` -> `watch.txt` (memory hooks
   on all four engines over attach, up, 36, 36/80, 6, 149/40, watchdog, scan,
   watchdog, down, up, down, detach): only offset 0x00 (all slot callers)
   and offset 0x08 (`wlc_prep_pdu`, 22 reads during the scan); no write; no
   `osl_mem*` on an engine from outside. Slot call counts per caller and
   step are in the same file (e.g. 22 `txfast` from `wlc_txfifo` in the scan,
   23 `getnexttxp` and 514 `getnextrxp` inside the two down steps' reclaims).
   `staticscan.py` -> `staticscan.txt`: every table call with a slot offset
   in all decompiled functions, read by hand (adds `wlc_statsupd`'s direct
   counter reads and the data-path callers not reached by the session).
5. Every procedure was read in the decompiled C **and** checked against the
   disassembly where the decompiler was unclear (`dma_attach` stack
   arguments and the third control-register field, `dma_rxfill`'s
   `osl_pktpull` argument and alignment arithmetic, `dma_rx`'s
   `osl_pktsetnext` arguments, `dma_ringalloc`/`dma_alloc` argument lists,
   the timeout-only `osl_delay(300)` of `txreset`, `getnexttxp`/`peekntxp`
   range handling, `txrotate` masks, `glom_enable` result).

Not done: a direct-call comparison of every slot against an independent
model with chosen states (status registers preset, chains, full rings,
allocation failures). The transmit reclaim with hardware progress, the
receive path (`dma_rx` with completed buffers), `txrotate`, `peek*`,
`getpos`, `txunframed` and the parity paths are therefore verified by
reading only, not by execution. The session cannot exercise them
meaningfully anyway: the model's DMA registers are plain memory (status
never advances) and the attach probe yields masks of 0xf, which turn every
status index into 0 (see Open questions).

## Open questions

1. **Masks on the real chip.** The attach probe writes 0xffffffff to the
   ring base low register and reads it back; if the low 12 bits are kept the
   mask becomes `(pointer register) | 0xf`. What the real rev 42 core
   returns in the pointer register at that moment is unknown (an engine with
   16-bit pointers would give 0xffff; the plain-memory model gives 0 -> mask
   0xf, which makes every index computation 0 and breaks the status-based
   paths in the emulator). Same question for the alignment probe (0xff0
   read back -> `aligndesc_4k` 0) and the address-extension probe. A peek on
   the target should read `XB.lo`/`XP` after such a write (unverified).
2. `dma_addrwidth` on the real card depends on ChipCommon capabilities bit
   27 (64-bit backplane); the open glue ignores the result, so it only
   matters for the call itself.
3. The model of the card needs, for traffic (section 4, still to be
   written): status 0 state/current-descriptor fields that advance as
   descriptors are consumed, status 1 active descriptor and error fields,
   the receive status word (length) written at the start of each buffer,
   and per-FIFO interrupt bits. Not specified yet.
4. Sections 2 (FIFO use in `wlc_bmac.c`/`wlc.c` beyond the slot calls listed
   in D8 and P54), 3 (interrupts) and 4 (what the hardware does) are not yet
   written.
5. `wlc+0x68` is cleared by `wlc_bmac_init` step 30 and by `sub_05fefa`;
   purpose unknown (it is not part of the engine).
6. Who copies the `&txavail` pointers to `wlc->core+0x08` (D8) was not
   identified.

## Contradictions with other documents

* `bmac-init.md` D2 step 13.6: ring registers listed as +0x200, +0x220,
  +0x240, +0x260; for MAC rev 42 they are transmit +0x200/+0x240/+0x280/
  +0x2c0 and receive +0x220 (D7).
* The first draft of this file numbered the transmit reclaim ranges 0/1/2;
  the code uses 1 (all), 3 (transferred), other (transmitted) (D3, P25).
