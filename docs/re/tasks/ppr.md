# Task `ppr`: the per-rate transmit power container

Specification: [`../spec/ppr.md`](../spec/ppr.md) (all of it). Interface:
`open/include/bcm4360/ratepwr.h` (given: keep the prototypes exactly; they
are the specification's functions with the prefix `bcm4360_ratepwr_`
instead of `ppr_`). Write **`open/lib/ratepwr.c`**.

## Why

After the regulatory database, Broadcom's `wlc_ppr.c` is one of the two
closed parts the driver executes most (about 6% of everything it runs in a
session: the transmit power code builds and combines these containers on
every channel change). It is a pure data library, private to its file, so it
can be taken over as a whole. With this task it becomes open code inside the
real driver (hybrid build, switch `open_ppr`, adapter `hybrid/ppr.c`, already
written), and the open PHY no longer depends on Broadcom's code for its
transmit power arithmetic.

## Rules for this code

* Clean room: `../implementing.md`, binding.
* **Layout**: use the bytes of the specification's "Data" section exactly (a
  32-bit bandwidth word, then 1, 2 or 3 sets of 182 bytes; the group offsets
  of "The 182-byte set"). The specification allows another internal layout,
  but the test compares the container bytes, and `struct bcm4360_ratepwr`
  stays opaque to everybody else.
* **Allocation**: `bcm4360_ratepwr_create` allocates with `osl_malloc(osh,
  size)` and `bcm4360_ratepwr_delete` frees with `osl_mfree(osh, p, size)`
  (declared in `hw.h`), the sizes of the specification - the platform counts
  the bytes per handle. `deserialize_create` goes through `create`.
* The `.rodata` tables the specification lists (the MCS group offsets, the
  sizes, the set selection) are facts: write them as constants. Reproduce the
  documented quirks: 8-bit wrap of the arithmetic, the signed/unsigned
  comparisons as stated (`apply_vector_floor` compares unsigned), disabled
  bytes (-128) treated exactly as each function says, `ppr_serialize` always
  writing flags 7 and its length check without the header, the visiting order
  and repetitions of `map_vec_all`, the set copy order of `copy_struct` for
  mixed bandwidths, an out-of-range `mode` that reads on into the following
  table words (array `F` of "`.rodata` tables") as long as the index stays
  inside the three MCS tables.
* Where the object's result is undefined (an MCS selector with `nss` 1 and
  `chains` 0, or `nss` 1, `chains` 3 and `mode` >= 4; `get_max` of a
  container with bandwidth >= 3), return NULL / no group, or -128: the test
  leaves these out.
* No global mutable state; no `NULL` check the specification does not
  describe is needed (`delete` has none; callers check).

## Tests

From `tools/re`:

* `python ab.py build` (no warnings).
* `python ab.py run ppr --show 20` - Broadcom's `ppr_*` and your
  `bcm4360_ratepwr_*` called with the same arguments on the same memory, about
  92,000 calls: create/delete through the allocator, every function on
  random contents, every bandwidth argument and container bandwidth, every
  selector, all bandwidth pairs, serialisation with prepared and unprepared
  buffers and lengths around every limit. The map functions are called with
  a callback of Broadcom's that runs on both sides; the order of its visits
  is compared. Done when it reports `0 differences`. (`ab.py run ppr` alone
  takes a few seconds.)
* The whole driver session with your container in place of Broadcom's
  (Docker, Linux tools; Docker Desktop is running): `docker run --rm -v
  "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-ppr bcm4360-emu:latest python hybrid.py
  check` must report **the same**, also with `check --off ppr`, `check --on
  phy` and `check --on phy --off ppr`. The adapter `hybrid/ppr.c` is linked
  automatically as soon as `open/lib/ratepwr.c` exists.
* `python ab.py all` must still pass.

Do not edit `hybrid/` or `tools/`. Questions into `docs/re/questions/ppr.md`;
finish with a short report.
