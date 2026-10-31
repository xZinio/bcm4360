# Task `utils`: board variables, number parsing, bit count, CRC-8

Specification: [`../spec/utils.md`](../spec/utils.md) (all of it: seven small
procedures). Interface: `open/include/bcm4360/utils.h` (given; implement it,
do not change the prototypes). Test: `python hybrid.py run utils` in
`tools/re`.

## Why

These helpers are tiny, but the driver runs them constantly: the lookup of a
board variable alone is about a fifth of everything the driver executes in a
session. With this task they become open code in the hybrid build of the
driver (`hybrid/README.md`), which replaces the object's functions with the
open ones inside the real module.

## What to write

`open/lib/utils.c` implementing every function of `utils.h`, one function per
procedure of the specification, in the style of `../implementing.md`:

* `bcm4360_getvar`, `bcm4360_getintvar`, `bcm4360_getintvararray`,
  `bcm4360_getintvararraysize`: the `fallback` argument is what the
  specification calls `nvram_get` (call it exactly where the specification
  does; it may be NULL, then a name that is not in the block has no value).
* `bcm4360_strtoul`: exactly as specified, including the overflow return
  without storing `endp` and the character classes of the specification's
  "Data" section (do not use the C library or the kernel's ctype).
* `bcm4360_bitcount`, `bcm4360_crc8`: compute the CRC from the polynomial
  given in the specification (a table you compute in the code or a bitwise
  loop, your choice); do not copy a table from anywhere.
* Freestanding: no libc; `hw_strlen`/`hw_memcmp` of `hw.h` are available if
  you want them (the specification's `osl_strlen`/`osl_memcmp`).

## What the test does

`hybrid.py run utils` runs Broadcom's object through a whole session (attach,
up, nine channel changes, watchdog, down) twice: once alone, once with its
`getvar`, `getintvar`, `getintvararray`, `getintvararraysize`, `bcm_strtoul`,
`bcm_bitcount` and `hndcrc8` replaced by your functions (through the bridge
`hybrid/utils.c`, which you may read: it only passes the arguments on). Every
bus access of the two runs is compared. The object looks up hundreds of board
variables during attach and programs the hardware with them, so a wrong
lookup or parse shows up as different register values. The task is done when
it reports `0 differences`.

`python ab.py build` checks that `open/` compiles without warnings (your file
must too); `python ab.py all` must still pass (your file is not used there).

Finish per [`../implementing.md`](../implementing.md): `0 differences`, no
build warnings, questions (anything you had to guess) in
`docs/re/questions/utils.md`, and a short report.
