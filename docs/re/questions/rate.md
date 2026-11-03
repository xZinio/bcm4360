# Questions from the implementation of `rate` (open/lib/rate.c)

Specification: `docs/re/spec/rate.md`. Test results: `hybrid.py rate` "the
same" (20,072 calls, 20,138 accesses), the same with `--off rate`;
`hybrid.py check` and `check --on phy` "the same" (41,416 accesses each);
`ab.py build` without warnings. No test contradicted the specification; the
points below are where I had to choose.

The implementer of this task was the session that also wrote the card test
(scripts/card-test.sh); it opened nothing below `lib/` or `re-out/` and ran
only the comparison tests named here.

1. **A hardware rate with a code above 108** (`wlc_rate_hwrs_filter_sort_validate`,
   step 3): the specification says the object then reads beyond its 109-byte
   table (undefined). The open table covers all 128 codes a rate byte can
   have, cleared, and only codes below 109 are ever entered (step 2), so such
   a rate is never kept. The tests and, per the specification, the driver's
   hardware sets have no such code.
2. **`rate_info` for codes 109..127**: taken as the 19 bytes the
   specification lists after the table; only `wlc_rateset_filter` with
   `rates` = 1 reads them, and only bit 7 matters there (codes 116 and 124).
3. **`rates` values other than 0, 1 and 2** in `wlc_rateset_filter`: no rate
   is skipped by kind (only the basic-rate filter applies), and step 5 treats
   them as "not CCK only". This is the literal reading; the tests pass with it.
