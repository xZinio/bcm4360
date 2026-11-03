# Task `cca`: the channel statistics module

Specification: [`../spec/cca-stf.md`](../spec/cca-stf.md), "Procedures,
part 1: `wlc_cca.c`" and everything of "Data" it uses (error codes, the
module service, the iovar table, the channel table, the state `cca_info`,
the output of `cca_query_stats`, the value of the iovar, the fields of
`wlc`). Interface: `open/include/bcm4360/cca.h` (given: keep the prototypes
exactly). Write `open/lib/cca.c`.

## Why

Broadcom's channel statistics run at every watchdog tick, at every channel
change and initialisation; the down function alone walks 38 x 60 sample
slots. Together about 0.27 % of everything the driver executes with the open
PHY - one of the largest pieces of Broadcom code left. With this task the
whole of `wlc_cca.c` becomes open code inside the real driver (hybrid
build, switch `open_cca`, adapter `hybrid/cca.c`).

## Rules for this code

* Clean room: `../implementing.md`, binding.
* The state is this file's own: define `struct bcm4360_cca` yourself
  (the layout of "The state `cca_info`" is the natural choice; nobody else
  reads it), but allocate it with exactly the specification's size and
  calls: `osl_malloc(osh, 0x4390)` followed by zeroing (that is what
  `wlc_calloc` does), freed with `osl_mfree(osh, cca, 0x4390)`. The heap of
  the emulator hands out addresses in allocation order and later allocations
  must land at the same places. The iovar handler's buffer likewise
  (`osl_malloc` of the size of step 6, freed with that size).
* The module's three functions are `bcm4360_cca_down`, `bcm4360_cca_watchdog`
  and `bcm4360_cca_doiovar` of the interface; attach registers them through
  `bcm4360_cca_env_module_register` with the module name, an iovar table of
  your own (`struct bcm4360_iovar`, the two entries of "The iovar table of
  the module") and the state as the handle.
* Everything of the driver through the `bcm4360_cca_env_*` services, with the
  `wlc` given to attach (keep it in the state, as the object does). Chanspec
  helpers: `bcm4360_chspec_ctlchspec` and `bcm4360_chspec_malformed` of
  `bcm4360/channel.h` (the open channel layer). The time stamp:
  `osl_sysuptime()` of `bcm4360/hw.h`. Copies and clearing: `hw_memcpy`,
  `hw_memset` or loops.
* Reproduce the quirks the specification spells out: the free marker 0xffff
  that an accumulated busy-own value can reach, the sample `pool[0]` that is
  never handed out, the watchdog's -20 for a channel not in the table (the
  driver's watchdog ends its tick on it), the iovar handler copying the
  **requested** number of records, 32-bit wrapping arithmetic.

## Tests

From `tools/re`; the hybrid tools need Linux (Docker image `bcm4360-emu`):

* `python ab.py build` - your file compiles without warnings.
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-cca bcm4360-emu:latest python hybrid.py
  cca` - the object alone and the hybrid core (with your code in place of
  Broadcom's) drive the module the same way: 2,220 calls of the watchdog,
  down function and iovar handler (through the module table), the update,
  the TSF update, the query and the chanspec lookup, with the microcode's
  counters set by hand, the clock advanced, chanspecs inside and outside the
  table, the power-save flag, the pool exhausted, the free marker hit,
  detach, attach on an old core revision, attach again. Done when it
  reports `the same` (every result and output buffer, the module table, and
  every access to the card). The layer is linked as soon as
  `open/lib/cca.c` exists (`hybrid-new-cca-objs` in `hybrid/objects.mk`);
  a file that does not compile stops every hybrid run, so keep it compiling.
* the same with `--off cca` must also report `the same`.
* `python hybrid.py check` and `python hybrid.py check --on phy` (same Docker
  command): the whole driver session, whose watchdog ticks, channel changes
  and initialisations run your code - must report "the same".
* `python phy_soak.py --on=` (the long session: 1,308 watchdog ticks) must
  report "the same".
* `python ab.py all` must still pass.

Questions into `docs/re/questions/cca.md`; finish with a short report
(files, functions, test results, questions).
