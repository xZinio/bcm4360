# Task `dma`: the DMA engine

Specification: [`../spec/bmac-data.md`](../spec/bmac-data.md), **section 1**
(Overview, Data D1-D8, Procedures P1-P53; P54 is `wlc_bmac.c`'s and stays
Broadcom's). Interface: `open/include/bcm4360/dma.h` (given: keep the
prototypes, `struct bcm4360_dma_pub` and `struct bcm4360_dma_bus` exactly).
Write **`open/mac/dma.c`**.

## Why

The DMA engines move every frame between the driver and the 802.11 core:
the beginning of the data path an open driver needs. Broadcom's code reaches
an engine only through `dma_attach` and the function table in the engine's
first field, so the whole engine can be taken over at one point: with this
task it becomes open code inside the real driver (hybrid build, switch
`open_dma`, adapter `hybrid/dma.c`, already written: it builds the table
with Broadcom's slot layout and points it at your functions).

## Rules for this code

* Clean room: `../implementing.md`, binding (mind its note on struct
  initializers becoming `memset` calls).
* `struct bcm4360_dma` is yours, but it must **begin with `struct
  bcm4360_dma_pub`**: Broadcom's code reads `txavail` (through the pointer
  `getvar("&txavail")` returns) and the three counters directly (D8). Leave
  `fn` alone (the adapter sets it after attach). Allocate the engine with
  `osl_malloc(osh, ...)` and free it in `detach` with the same size.
* Implement the 64-bit engine of the specification (descriptor layout D6,
  registers D2, index/address conversions, the polling loop "Poll" with its
  exact counts, the order of every register access and of the descriptor
  word writes). The 32-bit engine (D5) is not needed: if the core's status
  flag 0x1000 is clear, return NULL from attach with a comment.
* The OS layer for packets and DMA memory is the `osl_pkt*` / `osl_dma_*`
  functions of `hw.h` (the specification's names); `osl_dma_map` gets NULL
  as its last argument (the specification notes the object passes a
  meaningless value there). The backplane functions attach and addrwidth
  call are the services of `dma.h` (`ctx` = `bus->ctx`).
* Edge cases the specification lists are part of the contract (the extra
  300 us of `txreset`, `rxfill` writing the pointer register even when it
  posted nothing, `txcommitted` without the ring base, `addrext` = 1 after
  the probe, the ring allocator freeing with the requested size, 0xdeadbeef
  in reclaimed descriptors, the transmit reclaim ranges 1/3/other).
* No global mutable state (the default message-level word: keep it in the
  engine).

## Tests

From `tools/re` (Docker, Linux tools; Docker Desktop is running; the adapter
`hybrid/dma.c` is linked automatically as soon as `open/mac/dma.c` exists):

* `python ab.py build` (no warnings).
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-dma bcm4360-emu:latest python hybrid.py
  check` must report **the same**; likewise `check --off dma`, `check --on
  phy`, `check --on phy --off dma`, and `srom`. The session attaches four
  engines (FIFO 0 with a receive channel), resets, initialises and fills
  them at every up, suspends/resumes transmit around channel changes,
  reclaims at down and detaches them.
* `python ab.py all` must still pass.

The transmit and receive of frames (txfast, getnexttxp, rx, getnextrxp with
completed descriptors) are not reached by the session (the emulator's DMA
registers do not advance): write them from the specification.

Do not edit `hybrid/` or `tools/`. Questions into `docs/re/questions/dma.md`;
finish with a short report.
