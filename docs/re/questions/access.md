# Questions of the implementer: task `access`

Specification: [`../spec/access.md`](../spec/access.md). Code:
`open/include/bcm4360/access.h`, `open/hw/access.c`.

Result of the test: `python ab.py run access`: 1 stage, 422 accesses of the
object, 422 of the open code, 0 differences; the values returned by the read
operations are equal. The build has no warnings.

The test did not contradict the specification anywhere. The points below are
what the specification leaves open and what I had to decide myself. For each:
what the specification says, what the test shows, what I did.

How I observed the pacing of the object: with the pacing switched off in my
code (a temporary change) the test lists the dummy reads of the object as
differences. They are at the accesses 23, 48, 110, 142, 175, 240, 274 and 303
of the object's trace.

## 1. Write pacing: how the counter is compared with the limit

* Specification ("PHY registers", write pacing): "Before the write the counter
  is incremented; when it reaches the limit it is set to 0 and a dummy read is
  made first".
* Test: the limit the test takes from the object is 24. The eight dummy reads
  of the object are each before a 24th counted write, the counter runs on
  from PHY to radio writes and into the table transfers, and the writes made
  directly to `D11(0x3fe)` are not counted (the read at access 303 follows the
  two direct writes of a 48 bit entry and precedes `PHY(0x0d)` of the next
  transfer). All as specified. With a constant limit of 24 the test cannot
  tell "equal" from "greater or equal".
* What I did: increment, then dummy read and counter = 0 if the counter is
  greater than or equal to the limit.
* Question: which comparison does the object make? Can the limit be 0, or
  change while the counter is above the new value? With a limit of 0 my code
  makes a dummy read before every write (like a limit of 1); a comparison for
  equality would make none for the next 65535 writes.

## 2. Reading table entries of 60 and 64 bit

* Specification ("PHY tables", step 2): for 60 and 64 bit only the writing is
  described.
* Test: two entries of 60 bit (table 0x30) and two of 64 bit (table 0x31) are
  read; no difference in the accesses and in the values returned.
* What I did, by analogy to 48 bit: the first word with a normal read of
  `PHY(0x11)`, three more words by reading `D11(0x3fe)`; first value = word 0
  | word 1 << 16, second value = word 2 | word 3 << 16.
* Request: add the reading to the specification.

## 3. Entries of 60 bit: the four bits above

* Specification ("PHY tables"): 60 and 64 bit are described alike, nothing is
  said about the bits 60..63.
* Test: the second value of the 60 bit entry in the scenario is 0x03334444,
  its upper four bits are 0. If the object masked them when writing or
  reading, the test would not show it.
* What I did: all 16 bits of the highest word are transferred, in both
  directions, for 60 as for 64 bit.
* Question: does the object mask the highest word of a 60 bit entry? An entry
  like 0xf3334444 in the scenario would answer it.

## 4. Transfers with a width that no table has

* Specification ("PHY tables"): the widths are 8, 16, 32, 48, 60 and 64.
* Test: no other width occurs.
* What I did: `bcm4360_tbl_write()` and `bcm4360_tbl_read()` make no access at
  all for another width (id and offset are not written either).
* Question: what does the object do? Only of interest if a caller depends on
  it.

## 5. Entries of 8 bit, ids and offsets of more than 16 bit

* Specification ("PHY tables"): 8 bit entries go through the 16 bit register
  `PHY(0x0f)`; nothing about the upper byte when reading. Id and offset are
  32 bit arguments in the interface of the task, the registers `PHY(0x0d)`
  and `PHY(0x0e)` take 16 bit.
* Test: the 8 bit values are 1..8, ids and offsets are small.
* What I did: writing: the byte, upper byte 0. Reading: the lower byte of
  what was read is stored, the upper byte is dropped. Of id and offset the
  lower 16 bit are written.

## 6. Shared memory: width of the flush

* Specification ("Object memories of the MAC"): "After writing the address
  the driver reads `D11(0x160)` back once (a flush)", without the width.
* Test: a read of 32 bit is right.
* Request: name the width in the specification.

## 7. Bus type

* Specification ("PHY registers"): "The pacing only exists for bus type PCI";
  the scope is a PCIe card.
* Test: it calls `bcm4360_phy_io_init()` with `pci` = 1 and the object paces
  its writes: for the object the PCIe card has bus type PCI.
* What I did: with `pci` = false writes are not counted and no dummy read is
  made. Reads set the counter to 0 on every bus type (no difference in
  behaviour). Not tested: `pci` = false.

## 8. Names of `D11(0x3e0)` and `D11(0x120)`

* Specification: `access.md` gives no name or purpose for the two registers
  of the dummy reads. `acphy-radio.md` ("Overview") calls `D11(0x3e0)` the PHY
  version; for `D11(0x120)` no document gives a purpose (`acphy-radio.md`,
  section 3, also only reads it and drops the value).
* What I did: `D11_PHY_VERSION` for 0x3e0, `D11_REG_0x120` for 0x120 (rule
  "named by its address").
* Question: what is `D11(0x120)`?

## 9. The guide: SPDX line of headers

* Guide ("Code"): "`// SPDX-License-Identifier: ISC` at the top of every
  file". The headers that exist in `open/include/bcm4360/` (`hw.h`,
  `types.h`) begin with `/* SPDX-License-Identifier: ISC */`, which is what
  the kernel wants for headers.
* What I did: `access.h` like the existing headers, `access.c` with `//`.
* Question: which form is wanted for headers?

## Remarks

* `docs/re/README.md` and `open/README.md` refer to `docs/re/05-status.md`;
  there is no such file in `docs/re/`.
* `struct bcm4360_phy_io` has 16 bytes (limit of the task: 256).
* The register addresses and the table registers are defined in `access.h`
  (`D11_PHY_ADDR`, `D11_RADIO_ADDR`, `D11_OBJ_ADDR`, `ACPHY_TBL_ID` and so
  on), because the radio id is read at attach with plain accesses to
  `D11(0x3d8)`/`D11(0x3da)` and the MAC will need the object memories. If they
  are to live in a header of the MAC core, they can move.
