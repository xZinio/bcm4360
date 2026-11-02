# Questions / notes from implementing `bmac-mem` (spec sections A8, B4-B6, B11, E1, E3, E7, F1, F2)

Task: `docs/re/tasks/bmac-mem.md`. File written: `open/mac/bmac_mem.c` (no
change to `mac.h`, `bmac_ctrl.c`, `bmac_mhf.c`, `access.c`). Tests: `python
ab.py build` (no warnings), `hybrid.py check` and `hybrid.py check --off mac`
(both "the same", 41416 accesses each), `python ab.py all`.

## 1. B5: attribute bit 15 is written into the entry (answered by the test)

B5 step 1 says "OR its attribute word into `attributes`" and step 2 puts
`attributes << 16` into word1, but does not say whether bit 15 (the "OR with
the existing entry" request) is cleared before the write. I wrote it as it
reads: bit 15 stays. A probe that cleared it made the hybrid session differ at
entry 63 (`W amt 001fc/4`: object 0x80083ef4, probe 0x00083ef4; also visible in
later reads of the same word, 0x8008 = the unmute attributes of E4). So bit 15
is stored in the table. Suggest adding one sentence to B5 step 2.

## 2. B4 `copyfrom_objmem` with an odd length: is the byte beyond the buffer written?

B4 says of `copyto_objmem` that an odd `length` is rounded up and one byte
beyond the buffer is **read**; of `copyfrom_objmem` only "the same with reads
..., bytes stored low byte first". Read literally, an odd length would also
**store** one byte beyond the caller's buffer. The register accesses are the
same either way (the last 16-bit word is read in full); I store only the bytes
inside `length` so the open code does not write beyond the buffer. The same
applies to the remainder word of the address-match-table path (`length & 3`
not 0). Please confirm what the object does with the extra byte (the session
never passes an odd length, so the test cannot tell).

## 3. B4, address match table with `length & 3` = 3

"If `length & 3` is not 0, one 16 bit word more is written with the worker at
`offset + i`" - for a remainder of 3 bytes only two of them reach the memory
(one 16-bit word). Implemented that way; not exercised in the session (all AMT
transfers are 8 bytes). Is this right, or is a second 16-bit word written?

## 4. F2 with `byte_count` <= 0

F2 does not say whether steps 1 and 2 (`D11(0x130)` write, `D11(0x120)` read)
are made when `byte_count` is 0 or negative. I make them and transfer no word
(the loop runs while i < `byte_count`, i += 4). The four calls of the session
have positive lengths.

## 5. F2: how the word is formed from the buffer

F2 step 3 says "for each 4 bytes: if bit 16 is set, byte-swap the word" without
saying how the word is made from the bytes. I read it as the bytes in rising
order, low byte first (the host order of the object, little endian), swapped
when `D11(0x120)` bit 16 is set. The session's four calls match with bit 16
clear; the swapped path is from the text only.

## 6. B6: the end marker test and the size field

Taken as written: entries of 8 bytes (16-bit offset, 16-bit size, 32-bit
value, little endian), stop at offset 0xffff before doing anything with that
entry, size 2 = 16-bit write of the low half, size 4 = 32-bit write, any other
size skipped. `write_inits` is not reached in the session; not tested.

## 7. Things taken from the spec as written

* The semaphore (`sem_enter`/`sem_exit`, A8 `tcm_sem_enter`/`_exit`) brackets
  every single 16-bit worker access and every single 32-bit AMT word of B4 (so
  `set_shm`, `copyto/from_objmem` take and release it once per word), and is
  not taken by F2 (template RAM) or B6 (initialisation lists), which do not go
  through the object-memory window. Not observable in the tests.
* E3 for MAC revisions below 40 (the old RCMTA table, `copyto_objmem(index << 3,
  addr, 6, 0x40000)`) is implemented as the one-line branch; not reachable on
  this card.
* E7 writes the scratch registers only when `up` is set; B11 writes always.
* `write_ihr`, `set_hw_etheraddr`/`hw_etheraddr` and `write_inits` are not
  reached in the session (as the task says) and are written from the text only.

## Resolution (coordinator, 2026-11-02, from the object's code)

1. Bit 15 stays in the written entry: the attribute word is ORed with the old
   one when bit 15 is set and written as it is (your probe agrees). Added to
   bmac-init.md B5.
2. `copyfrom_objmem` with an odd `len` **stores both bytes** of the last word,
   i.e. one byte beyond `len` (the object writes `buffer[len]`). Your code
   stores only within `len`; the register accesses are identical and no caller
   in the session passes an odd length. Kept as a deliberate difference (it
   avoids a write past the caller's buffer); noted in B4.
3. Yes: for the address match table with `len & 3` = 3 one extra 16-bit word
   is transferred (through the 16-bit worker, with the AMT selector) and the
   third byte is lost. Same in both directions.
4. Yes: F2 always writes `D11(0x130)` and reads `D11(0x120)`; with `len` <= 0
   no word follows.
5. Yes: each word is the 4 buffer bytes loaded little endian, byte-swapped as a
   whole when bit 16 of `D11(0x120)` is set.
6. Yes as written: 8-byte entries, stop at offset 0xffff, size 2 = 16-bit
   write of the low half, size 4 = 32-bit write, other sizes skipped, no reads.
