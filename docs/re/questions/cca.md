# Questions from the implementation of `cca` (open/lib/cca.c)

Specification: `docs/re/spec/cca-stf.md`, "Procedures, part 1" and the Data
it uses. Test results: `hybrid.py cca` "the same" (2,220 calls, 117,624
accesses), the same with `--off cca`; `hybrid.py check` and `check --on phy`
"the same" (41,416 accesses each); `ab.py build` without warnings. No test
contradicted the specification; the points below are where I had to choose.

The implementer of this task was the session that also wrote the card test
(scripts/card-test.sh); it opened nothing below `lib/` or `re-out/` and ran
only the comparison tests named here.

1. **`wlc_calloc(osh, unit, 0x4390)`** (attach, step 2): written as
   `osl_malloc(osh, 0x4390)` and clearing, as the task says; `unit` is not
   used (presumably only for an error message of the object's allocator).
   If `wlc_calloc` prints something when the allocation fails, the open code
   does not.
2. **The 4-byte copy of `params`** (`sub_06e3fe`, step 2): left out, since
   the specification says it has no observable effect.
3. **`params`, `arg` and `out` alignment**: the u16 chanspecs are read and
   written byte by byte (any alignment); the 32-bit words of the records as
   words (x86 allows any alignment). Only the memory counts.
4. **`wlc_cca_get_chanspec` with an index outside 0..37**: the state keeps
   the specification's layout (`chan[]` at 0x2c, 0x7a bytes each, static
   assertions), and the index is applied to that layout without a bound, so
   the same bytes are read as in the object.
5. **The channel table and the iovar table** are constants of the file, so
   the module table holds the open code's iovar table and functions;
   `hybrid.py cca` reports the module table the same.
