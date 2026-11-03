# Task `rate`: the rate set helpers

Specification: [`../spec/rate.md`](../spec/rate.md) (all of it).
Interface: `open/include/bcm4360/rate.h` (given: keep the prototypes and the
structure exactly). Write `open/lib/rate.c`.

## Why

The driver builds and narrows its sets of rates with these ten helpers at
attach, at every initialisation, at joins and for every station record;
they are among the larger pieces of Broadcom code that still run with the
open PHY. With this task they become open code inside the real driver
(hybrid build, switch `open_rate`, adapter `hybrid/rate.c`).

## Rules for this code

* Clean room: `../implementing.md`, binding.
* The tables of "Data" (`rate_info` with the 19 values after it, the seven
  default rate sets, the reference rates) are constants of your file, from
  the specification's text.
* Copies with `hw_memcpy`, clearing with `hw_memset` (`bcm4360/hw.h`) or
  plain loops - only the resulting memory counts. No global state.
* Write only the fields the specification says are written: the tests fill
  the destination sets with garbage first and compare all 0x28 bytes
  afterwards (rates after `count`, the unused byte at 0x25 and so on must
  stay as they were).
* Byte arguments are `u8`/`bool` in the interface; the adapter truncates.

## Tests

From `tools/re`; the hybrid tools need Linux (Docker image `bcm4360-emu`):

* `python ab.py build` - your file compiles without warnings.
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-rate bcm4360-emu:latest python hybrid.py
  rate` - the object alone and the hybrid core (with your code in place of
  Broadcom's) get the same 20,072 calls: the default sets and random ones
  through every helper, every kind of filter, PHY type, band, bandwidth and
  stream count, rspecs of every kind. Done when it reports `the same`
  (results, all bytes of the sets, every access to the card). The layer is
  linked as soon as `open/lib/rate.c` exists (`hybrid-new-rate-objs` in
  `hybrid/objects.mk`); a file that does not compile stops every hybrid run,
  so keep it compiling.
* the same with `--off rate` must also report `the same`.
* `python hybrid.py check` and `python hybrid.py check --on phy` (same Docker
  command): the whole driver session, which builds its rate sets through
  your code - must report "the same".
* `python ab.py all` must still pass.

Questions into `docs/re/questions/rate.md`; finish with a short report
(files, functions, test results, questions).
