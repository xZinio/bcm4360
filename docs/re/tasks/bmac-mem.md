# Task `bmac-mem`: the MAC core's object memories and the setters on them

Specification: [`../spec/bmac-init.md`](../spec/bmac-init.md), sections
**A8** (shared memory and its workers, `tcm_sem_enter`/`tcm_sem_exit`),
**B4** (copy to/from object memories), **B5** (address match table), **B6**
(initialisation lists), **B11** (contention window), **E1** (hardware MAC
address), **E3** (receive address match), **E7** (retry limits), **F1**
(internal hardware registers), **F2** (template RAM), the introduction of
section **F** (selectors), and the reference tables of "Data". Register
access: [`../spec/access.md`](../spec/access.md) and `open/hw/access.c`
(use it, do not change it).

Interface: `open/include/bcm4360/mac.h` (given: the new prototypes and the
fields under "object memories and the state setters built on them"). Write
**`open/mac/bmac_mem.c`**. Your predecessors' `open/mac/bmac_ctrl.c` and
`bmac_mhf.c` show the style; do not change them.

## Why

This is the third increment of the MAC layer. These functions run constantly
(the address match table is rewritten on every channel change, shared memory
and scratch registers are written through them) and are nearly stateless, so
they can be taken over on their own. With this task they become open code
inside the real driver (hybrid build, switch `open_mac`, adapter
`hybrid/mac_mem.c`, already written).

## The functions

| Open function | Broadcom's | Section |
|---|---|---|
| `bcm4360_bmac_set_shm` | `wlc_bmac_set_shm` | A8 |
| `bcm4360_bmac_copyto_objmem` / `_copyfrom_objmem` | `wlc_bmac_copyto_objmem` / `_copyfrom_objmem` | B4 |
| `bcm4360_bmac_write_amt` | `wlc_bmac_write_amt` (with the read of an entry, `sub_061e23`, as a `static` helper) | B5 |
| `bcm4360_bmac_write_inits` | `wlc_bmac_write_inits` (the worker `sub_060f67` as a `static` helper) | B6 |
| `bcm4360_bmac_set_cwmin` / `_set_cwmax` | `wlc_bmac_set_cwmin` / `_set_cwmax` | B11 |
| `bcm4360_bmac_set_hw_etheraddr` / `_hw_etheraddr` | the same names | E1 |
| `bcm4360_bmac_set_rcmta` | `wlc_bmac_set_rcmta` | E3 |
| `bcm4360_bmac_retrylimit_upd` | `wlc_bmac_retrylimit_upd` | E7 |
| `bcm4360_bmac_write_ihr` | `wlc_bmac_write_ihr` | F1 |
| `bcm4360_bmac_write_template_ram` | `wlc_bmac_write_template_ram` | F2 |

The shared-memory workers of A8 (`sub_06188a`, `sub_061ed5`: one 16-bit word
of any object memory through `D11(0x160)`/`D11(0x164)`/`D11(0x166)`, with the
semaphore around it) are `static` helpers that the others use. Do not use
`bcm4360_shm_read`/`_write` of `open/hw/access.c` for them: the workers take
any selector, and the exact sequence of each word (address write, read back,
data access, with the semaphore around the whole) is what is compared.

## Rules for this code

* Clean room: `../implementing.md`, binding.
* The state is `struct bcm4360_mac`: read the fields the specification says
  the function reads, write the ones it writes (`srl`/`lrl`, `cwmin`/`cwmax`,
  `hw_etheraddr`); the caller copies them from and back to Broadcom's
  structures. `cwmin`/`cwmax` are those of the current band.
* The semaphore: call `mac->sem_enter(mac->sem_ctx)` and
  `mac->sem_exit(mac->sem_ctx)` exactly where A8 and B4 bracket an access with
  `tcm_sem_enter`/`tcm_sem_exit`; skip the call when the pointer is NULL.
  (Without offload firmware both do nothing, so the comparison cannot see a
  missing call: get it right from the text.)
* MAC revision: the card is revision 42 (`mac->mac_rev`). Implement the
  branches for revisions >= 40 that the sections describe; for the other
  revisions (E3's old RCMTA table) a comment is enough, or the branch if it
  is one line.
* Odd cases are part of the contract: an odd `len` in B4 reads one byte
  beyond the buffer; `len` <= 0 does nothing; F2 transfers a last whole word
  for a length that is not a multiple of 4; B5's attribute bit 15 means
  "OR with the existing entry".
* The initialisation list of B6 is Broadcom's data handed in by the caller;
  read its entries as the section says, stop at the end marker.

## Tests

From `tools/re`:

* `python ab.py build` (no warnings).
* The whole driver session with your code in place of Broadcom's (Docker,
  Linux tools; Docker Desktop is running): `docker run --rm -v
  "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-bmacmem bcm4360-emu:latest python hybrid.py
  check` must report **the same**, and so must `... python hybrid.py check
  --off mac` (the fallback). The adapter `hybrid/mac_mem.c` is linked
  automatically as soon as `open/mac/bmac_mem.c` exists. In this session the
  functions are called several hundred times (the address match table about
  240 times, the object-memory copies about 250, `set_shm` twice, the
  template RAM 4 times); `write_inits`, `write_ihr` and `set_hw_etheraddr`
  are not reached - write them from the specification.
* `python ab.py all` must still pass (in particular `mac-ctrl` and `mac-mhf`).

Do not edit `hybrid/` or `tools/`. Questions into
`docs/re/questions/bmac-mem.md`; finish with a short report.
