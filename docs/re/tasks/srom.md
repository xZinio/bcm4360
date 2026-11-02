# Task `srom`: the board variables from SROM and OTP

Specification: [`../spec/srom-otp.md`](../spec/srom-otp.md): `srom_var_init`
(bus type 1, PCI) with the conversion and the complete revision-11 table,
`varbuf_append`, `initvars_table`, `sprom_read_pci`, `srom_read`,
`otp_read_pci`, `initvars_flash_pci`, and the notes "for an implementation of
`srom_var_init`". The CRC is `bcm4360_crc8` of `open/lib/utils.c`
(`open/include/bcm4360/utils.h`; spec `utils.md`, `hndcrc8`).

Interface: `open/include/bcm4360/srom.h` (given: keep the prototypes, the
structure and the services exactly). Write **`open/chip/srom.c`**.

## Why

Building the variable list is the largest closed part the driver still runs
when the open PHY is on (about 8% of all execution: every appended variable
scans the whole buffer for a duplicate). With this task the card's SROM (or
the SROM image in its OTP) is turned into the variables by open code inside
the real driver (hybrid build, switch `open_srom`, adapter `hybrid/srom.c`,
already written).

## What to write

* `bcm4360_srom_var_init(card, &vars, &count)`: the procedure
  `srom_var_init` for bus type 1, steps 1 and 3..10: the three SROM read
  attempts (`sprom_read_pci`, with the chip control read and restore around
  each, and `bcm4360_srom_chipcontrol_srom4360` for chip revisions below 3),
  the OTP fall-back (`otp_read_pci` over the service
  `bcm4360_srom_otp_read_region`), the NVRAM fall-back (`initvars_flash_pci`
  over the services), the revision test, the conversion, `initvars_table`.
  The image and work buffers are `osl_malloc(card->osh, ...)`/`osl_mfree` in
  the order and sizes the specification gives (`hw.h`). SROM words are read
  with 16-bit reads (`hw_read16`) from the window the specification computes
  from `card->regs`, `card->ccrev` and `card->cccaps`.
* The conversion for **revision 11** from the specification's complete table
  (rows 1..141, path bases 108/128/148), with the formatting of the walk
  (the special cases: MAC address, `ccode` including a zero byte, `leddc`,
  NOFFS per field width, hexadecimal, signed common scalars, per-path values
  in decimal, the arrays). Every string goes through your `varbuf_append`
  exactly as specified (formatting with `hw_snprintf`, the duplicate scan
  and removal, the size bookkeeping and its overflow rule) - for revision 11
  no name repeats, but the procedure is the contract.
* For a revision other than 11 that the object accepts (bit `rev` of 0xf3e:
  1..5, 8..10), and for any bus type other than 1: free what you allocated
  and return `BCM4360_SROM_UNHANDLED` (the adapter then runs Broadcom's
  function). Unaccepted revisions return -2 as specified.
* `bcm4360_srom_read(card, byteoff, nbytes, buf, check_crc)`: the procedure
  `srom_read` for bus type 1 (other bus types: `BCM4360_SROM_UNHANDLED`).
* The `.bss` statics of the specification are private to the object and
  compared nowhere: keep the array text in your own storage (they are not
  part of the result).

## Rules for this code

* Clean room: `../implementing.md`, binding.
* The services of `srom.h` are the only way to the backplane, the OTP and the
  NVRAM; call them exactly where the specification's procedures call the
  corresponding functions (each has bus accesses that are compared).
* No global mutable state.

## Tests

From `tools/re` (Docker, Linux tools; Docker Desktop is running; the adapter
`hybrid/srom.c` is linked automatically as soon as `open/chip/srom.c`
exists):

* `python ab.py build` (no warnings).
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-srom bcm4360-emu:latest python hybrid.py
  srom` - attaches the driver from ten cards on Broadcom's object alone and on
  the hybrid core: the synthetic SROM, one with unprogrammed words, four
  random revision-11 images, a bad CRC, a blank SROM, no SROM with the image
  in the OTP, no SROM and a blank OTP. Compared: the result and the variable
  buffer of every `srom_var_init`, the result of the attach and every bus
  access. Must report **the same**, also with `--off srom`.
* `... python hybrid.py check` (the whole session) must report **the same**,
  also with `--off srom` and with `--on phy`.
* `python ab.py all` must still pass.

`srom_read` is not reached by these tests (it serves the `srom` iovar):
write it from the specification.

Do not edit `hybrid/` or `tools/`. Questions into `docs/re/questions/srom.md`;
finish with a short report.
