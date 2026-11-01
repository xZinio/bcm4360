# Task `clm`: the regulatory database interpreter

Specification: [`../spec/clm.md`](../spec/clm.md) (all of it). Interface:
`open/include/bcm4360/clm.h` (given: keep the prototypes and the two
argument structures exactly; replace the placeholder in `struct bcm4360_clm`
with the state the specification describes, two data-set slots and what
belongs to them). Write `open/lib/clm.c`.

## Why

Broadcom's CLM lookup is the closed code the driver executes most: about 46%
of everything it runs in a session (`clm_limits` walks the locale tables for
every channel and rate). With this task it becomes open code inside the real
driver (hybrid build, switch `open_clm`, `hybrid/clm.c`).

## Rules for this code

* Clean room: `../implementing.md`, binding.
* The database is data handed in by the caller (`hdr`, Broadcom's
  `clm_header`); read it exactly as the specification says, including the
  relocation delta and the format checks. Do not copy any table of the
  database into the code; small constant tables the specification lists
  (rate classes, sub-channel codes, the bandwidth map) are facts and may be
  written as constants.
* No global mutable state: everything in `struct bcm4360_clm`.
* Reproduce the exact results and result codes, including the documented
  odd cases (a failed flags check leaves the half-loaded set active; the
  80+80 `other` argument).

## Tests

From `tools/re`:

* `python ab.py build` (no warnings),
* `python ab.py run clm` - Broadcom's `clm_*` and your `bcm4360_clm_*` called
  with the same arguments on the same database: every country (iteration,
  lookup, definition, advertised code, aggregate lookup), and for every
  distinct locale set the flags and channel vectors of both bands, the
  regulatory limit and the 336 per-rate limits of every valid channel.
  Done when it reports `0 differences`.
* the whole driver session with your CLM in place of Broadcom's (Docker,
  Linux tools): `docker run --rm -v "D:\Programs\bcm4360:/repo" -w
  /repo/tools/re -e HYBRID_OUT=/repo/re-out/hybrid-clmimpl bcm4360-emu:latest
  python hybrid.py check --on clm` - must report "the same" (the power limits
  reach the hardware through the transmit power code).
* `python ab.py all` must still pass.

Questions into `docs/re/questions/clm.md`; finish with a short report.
