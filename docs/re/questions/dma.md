# Questions of the implementer: task `dma`

Specification: [`../spec/bmac-data.md`](../spec/bmac-data.md), section 1.
Code: `open/include/bcm4360/dma.h` (given), `open/mac/dma.c`.

## Test results

* `python ab.py build`: no warnings.
* `hybrid.py check`, `check --off dma`, `check --on phy`, `check --on phy --off
  dma`: 41416 accesses of the object, 41416 of the hybrid core, the same.
* `hybrid.py srom`: all ten cards the same.
* `python ab.py all`: passes (no scenario of this layer; the others are
  unchanged).

To make sure the default `check` really runs this engine, I ran it once with
a deliberately wrong copy of `dma.c` (`txinit` without the parity-disable bit)
mounted over `open/mac/dma.c` inside the container only (the file in the
repository was not touched): differences on every transmit control register
(`R/W d11 0x200/0x240/0x280/0x2c0` = 0x03780041/0x03780043 instead of
0x03780841/0x03780843). Then the real file was touched and `check` run again:
the same.

The tests did not contradict the specification anywhere. The points below
are what the specification leaves open, what the session does not reach, and
what I decided myself. For each: what the specification says, what the test
shows, what I did.

## 1. Index conversion: 16-bit truncation before or after the shift

* Specification: D6 says `index = (((value & mask) - base_low) & mask) >> 4`,
  "kept to 16 bits". P25 (range 3) writes the active descriptor as
  `((((XS1 & xs1_ad_mask) - xmtptrbase) & xs0_cd_mask) & 0xffff) >> 4`
  (truncated *before* the shift); P28 as `(((XS1 & xs1_ad_mask) - xmtptrbase)
  & xs0_cd_mask) >> 4`; P35 with `& xs1_ad_mask` and "(16 bit)".
* Test: not reached (the model's masks are 0xf, every index is 0).
* What I did: the general conversion (status 0, pointer registers, P27, P29,
  P33, P38, P40, P42, P25's status 0) shifts first and then keeps 16 bits;
  the three active-descriptor computations of P25, P28 and P35 are written
  exactly as the text gives them. The forms differ only for a mask with bits
  at or above 16 (possible if the attach probe reads a pointer register with
  high bits, open question 1). Which order does the object use in each place?

## 2. dma64_getnexttxp, range 3: where the status 1 read belongs

* Specification: P25 step 2: "only if `start == xs0cd`: read `XS0` ... Then,
  only for `range` == 3: read `XS1` ...".
* Test: not reached (the session uses range 1 only).
* What I did: the `XS1` part is outside the `start == xs0cd` condition: with
  range 3 every call reads `XS1` (and `XS0` only when the cache says so).
  Correct?

## 3. dma64_txfast when no descriptor is written

* Specification: P23 step 2: if `flags` lacks EOF, ctrl1 of `PREV(out)` =
  `flags | 0x60000000`, with `flags` = 0 when nothing was written; step 3:
  `txp[PREV(out)] = p0`.
* Test: not reached.
* What I did: as specified. Consequence: a frame whose buffers are all empty
  overwrites ctrl1 of the previous frame's last descriptor with 0x60000000
  (losing SOF/EOT there, on an empty fresh ring: descriptor `ntxd-1`) and
  replaces that descriptor's packet pointer by `p0`, so the previous frame's
  packet is never returned by `getnexttxp` (lost), while `p0` is returned
  for it. Is that really the object's behaviour (it is a defect if so)? Also
  as specified: the ring-full test (1.2) comes before the empty-buffer skip
  (1.3), so an empty buffer can still end in "ring full", and the second
  ring-full test after the increment (1.7) is kept although it cannot fire.

## 4. dma_ddtable_init with address extension

* Specification: P11 step 3: "read the control register of that direction
  and write it back with bits 16..17 = `pa >> 30`".
* Test: not reached (`ddoffsetlow` is 0 on this card).
* What I did: bits 16..17 are cleared and then set to `(pa >> 30) & 3` (an
  assignment of the field, not an OR). Correct?

## 5. dma_addrwidth without the 64-bit flag

* Specification: P2: "the 32 bit address-extension probe on `dmaregs`
  fails: 30"; P53: the 32-bit probe is identical to the 64-bit one.
* Test: not reached (the flag is set on this core).
* What I did: P53's sequence (R, W |0x30000, R, R, W &~0x30000) on the
  register `dmaregs` points to (offset 0). Is `dmaregs` the control register
  for the 32-bit engine too?

## 6. The 32-bit engine (D5)

* Task: not needed.
* What I did: attach reads the core flags (`R WRAP(0x500)`, step 3) and, if
  0x1000 is clear, frees the engine and returns NULL (Broadcom's caller then
  sees an attach failure). The object would continue with the 32-bit table.
  Not reached on revision 42.

## 7. Small decisions, no answer needed unless wrong

* `osl_strncpy` is not part of `hw.h`: the name is copied by hand (at most 8
  bytes, stopping at the NUL, byte 7 = 0; the rest is zero from the memset).
  A NULL name gives an empty name (the specification would fault).
* The engine is `sizeof(struct bcm4360_dma)` bytes, not 0x130, allocated with
  `osl_malloc` and freed with the same size in `detach`. The ring bus
  addresses written to `XB`/`RB` are nevertheless the object's (the session
  compares them).
* The per-descriptor map handles (`txp_dmah`, `rxp_dmah`) and the rings' OS
  DMA handles (`tx_dmah`, `rx_dmah`) are not kept: never allocated or used on
  this build (P1 notes, P4). `osl_dma_map` gets NULL as its last argument, as
  the task says.
* The default message-level word lives in the engine (`msg_level_default`).
* `dma64_getpos` (P29) does not update the `xs0cd`/`rs0cd` caches (the text
  does not say it does).
* `dma_ctrlflags` with the parity flag reads the transmit control register
  "always" (P46), also on an engine without a transmit channel (would fault;
  only reachable through slot 0x118 with flag 0x01, which no caller uses on
  this card). Kept as specified.
* `dma_rxfill` (P36): `n = nrxpost - ACTIVE` is unsigned as specified (a huge
  loop if more buffers were posted than `nrxpost`, which cannot happen with
  a constant `nrxpost`); no `nrxd` == 0 check, as specified.
* `dma64_txfast` and `txunframed` do not check `ntxd` == 0 (the text has no
  such check; callers only use engines with a transmit ring).

## 8. Not reached by the session, written from the text only

`txfast`, `txunframed`, `getnexttxp` with ranges 2/3 and with packets,
`txreclaim` with packets, `peeknexttxp`, `peekntxp`, `getpos`, `txrotate`,
`txpending`, `txcommitted`, `rx`, `getnextrxp` with completed buffers,
`peeknextrxp`, `activerxbuf`, `rxidle`, `glom_enable`, `fifoloopbackenable`,
`rxtxerror`, the parity paths (`ctrlflags` probe, descriptor parity bit), the
time-outs of the polling loops (the extra 300 us of `txreset`), the second
allocation of `dma_ringalloc` (boundary crossing), allocation failures in
attach, the address-extension variants (`ddoffsetlow` != 0) and the other bus
types of P1 step 9 / P2.

## Resolution (coordinator, 2026-11-02)

Accepted as implemented; none of them is reached by the session or by this
driver's normal use. 3 (a frame whose buffers are all empty): followed as
specified; the driver never posts such a frame (every frame carries at least
its header), so the odd result cannot occur here - left as a note for a
later check against the object. 1, 2, 4, 5: as you read the specification.
6, 7: fine.
