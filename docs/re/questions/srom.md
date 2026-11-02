# Questions from the implementation of `srom` (open/chip/srom.c)

Specification: `docs/re/spec/srom-otp.md`. Test results: `hybrid.py srom`
"the same" (ten cards, also `--off srom`); `hybrid.py check`, `check --off
srom`, `check --on phy` "the same". None of the points below was contradicted
by a test. They are guesses, or additions where the specification says
nothing or the object would misbehave. `srom_read` and the NVRAM fall-back
past the `sromrev` lookup (`initvars_flash_pci`) are not reached by these
tests. They are written from the text only.

1. **Chips 0x4331 / 0xa9a7** (`sprom_read_pci`, step 2,
   `si_chipcontrl_epa4331`): `srom.h` has no service for this function, so
   both entry points return `BCM4360_SROM_UNHANDLED` for these chips, before
   any service call or bus access. Should `srom.h` get an `epa4331` service,
   or is UNHANDLED right for them?

2. **Revision bytes with `rev & 31 == 11` other than 11** (43, 75, 107, ...).
   Step 7 shifts by `rev & 31`, and (B) says "11 (and anything > 10 that
   passed step 7)". So for these bytes the walk is the revision-11 walk:
   revmask bit 11, arrays (`rev > 9`), 3 paths at 108. Only the first string
   differs (`sromrev=43`). The open code converts them with the revision-11
   table instead of returning UNHANDLED. The other accepted bytes (33..37,
   40..42, and 1..5, 8..10) return UNHANDLED. No test image has such a byte.
   Please confirm.

3. **`varbuf_append`, step 3**: the specification checks for `n == -1`. The
   open code treats every negative `n` as an overflow (`size = 0`, return 0).
   The kernel's `snprintf` gives no other negative value, so this changes
   nothing.

4. **Array text bounds** ((A) 8, (B) 5). The object's array text is 0x100
   bytes. Once `pos` reaches 0x100, the object would call `snprintf` with
   size 0 (then `pos` grows past 0x100) or with a "negative" size (the
   kernel refuses sizes > INT_MAX and returns 0). When the array completes,
   it would write the NUL at `.bss+0x90 + pos - 1`, which can be outside the
   buffer. The open code does two things here. It treats `pos >= 0x100` as
   the "n <= 0" case. It also writes the terminating NUL at most at byte
   0xff. The longest revision-11 array text is 84 characters (12 x `0xffff,`),
   so this case cannot occur.

5. **"n <= 0" skip** ((A) 8): "skip entries while they have ARRAY (as in
   step 7 ...)". The open code starts the scan at `last` itself. When `last`
   has ARRAY, this is the same as step 7: the next entry visited is the
   first entry without ARRAY + 2. The specification does not say which entry
   is next when `last` has no ARRAY (the array's final element). This case
   cannot occur (see 4).

6. **`initvars_flash_pci`, step 3**: the specification clears `coded` but
   says nothing about `devpath`. The open code sets `devpath[0] = 0` before
   `si_devpath`, which changes nothing when `si_devpath` succeeds. Both
   results (`si_devpath`, `si_coded_devpathvar`) are ignored, as specified.

7. **`srom_read`**: the step-1 argument test runs before the bus-type test,
   as specified. After that test, other bus types return
   `BCM4360_SROM_UNHANDLED`. (For those bus types the object returns 1
   except PCMCIA, so the adapter's fall-back gives the same result.)

8. **(A) step 7** (revisions below 10, an array read as a scalar) is
   implemented as written but cannot be reached: the open walk runs only for
   `rev & 31 == 11`.

9. **No `vsnprintf` in `hw.h`.** `varbuf_append` is variadic in the object.
   Here it is a macro: the call site formats with `hw_snprintf(b->cur,
   b->size, fmt, ...)` (step 2), and a function does steps 1 and 3..5. If
   later code needs a real variadic append, `hw.h` would need an
   `osl_vsnprintf`.

10. **For other implementers: compiler-generated `memset`.** A
    whole-structure initializer (`struct x c = { .a = ... }` for a structure
    with two 256-byte arrays) built without warnings with `ab.py build`
    (zig). In the hybrid build, gcc turned it into a call to `memset`, which
    the hybrid core does not provide: "import memset is not implemented".
    The open code now sets the fields one by one. It may be worth a line in
    `implementing.md`.

11. **CRC byte order**: `hndcrc8` runs over the image's bytes. The open code
    feeds each word explicitly as low byte, then high byte ("word n is at
    byte 2n, little endian"). On x86 this is the same as the object's byte
    pointer over the buffer.

12. **Dedicated `.bss` statics**: not reproduced. Each conversion keeps its
    array state in a local structure that starts empty, as the task allows.
    The object keeps the common array flag in `.bss` across calls. If a
    common array were ever left open at the end of the table, the object's
    next call would continue it. That cannot happen with the revision-11
    table.

## Resolution (coordinator, 2026-11-02)

* 1 (0x4331/0xa9a7): leave them UNHANDLED; this driver's chips are the
  0x4360 family, and Broadcom's function serves the others.
* 2 (`rev & 31` = 11 with rev 43, 75, ...): as you did - srom-otp.md step 7
  tests bit `rev & 31` and (B) takes "anything > 10 that passed step 7" as
  revision 11; `sromrev=` prints the byte itself. No such image exists.
* 3, 4: accepted as implemented (none is reachable with the revision-11
  table; the services and `hw_snprintf` are as the interface gives them).
* 10 (a struct initializer became a call of `memset`, which the emulator
  does not provide): noted in docs/re/implementing.md.
