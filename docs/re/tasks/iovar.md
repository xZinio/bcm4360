# Task `iovar`: the iovar dispatcher

Specification: [`../spec/iovar.md`](../spec/iovar.md) (all of it).
Interface: `open/include/bcm4360/iovar.h` (given: keep the prototypes and the
structures exactly). Write `open/lib/iovar.c`.

## Why

Every named driver variable goes through this one function, from the Linux
glue and from the driver itself, and its linear search over the modules'
tables makes it about 1 % of all execution in the hybrid session - after
the channel checks, the largest single piece of Broadcom code that still
runs. With this task it becomes open code inside the real driver (hybrid
build, switch `open_iovar`, adapter `hybrid/iovar.c`).

## Rules for this code

* Clean room: `../implementing.md`, binding.
* Follow the numbered steps of the procedure; the module table, the request
  check, the BSS configuration lookup and the handlers are reached only
  through the `bcm4360_iovar_env_*` services of `iovar.h`. The tables'
  entries are the caller's data (`struct bcm4360_iovar`): read them, never
  write them.
* The fields of "Fields updated after a set" are reached through the
  pointers of `struct bcm4360_iovar_wlc`.
* String helpers: `hw_strcmp`, `hw_strncmp`, `hw_memcpy` of
  `bcm4360/hw.h`; finding the last `':'` is a loop of your own. No global
  state.
* Reproduce the exact results and argument values handed to the services,
  including the cases the specification spells out (`params` NULL, the
  `bsscfg:` prefix for gets and sets, `val_size` from the length before the
  prefix adjustment, the update after a set whatever the handler returned).

## Tests

From `tools/re`; the hybrid tools need Linux (Docker image `bcm4360-emu`):

* `python ab.py build` - your file compiles without warnings.
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-iovar bcm4360-emu:latest python hybrid.py
  iovar` - the object alone and the hybrid core (with your code in place of
  Broadcom's dispatcher) get the same 3,054 calls of `wlc_iovar_op`: gets
  and sets of all 405 driver variables, the `bsscfg:` prefix, odd names.
  Done when it reports `the same` (results, value buffers, the fields after
  a set, and every access to the card). The layer is linked as soon as
  `open/lib/iovar.c` exists (`hybrid-new-iovar-objs` in `hybrid/objects.mk`);
  a file that does not compile stops every hybrid run, so keep it compiling.
* the same with `--off iovar` must also report `the same`.
* `python hybrid.py check` (same Docker command): the whole driver session,
  which sets its variables through your code - must report "the same".
* `python ab.py all` must still pass.

Questions into `docs/re/questions/iovar.md`; finish with a short report
(files, functions, test results, questions).
