# Questions from the implementation of `channel` (open/lib/channel.c)

Specification: `docs/re/spec/channel.md`. Test results: `hybrid.py chan`
"the same" (2,459,676 calls in 261 states, 32 steps, 45,860 accesses), the
same with `--off chan`; `hybrid.py check` "the same" (41,416 accesses);
`ab.py all` all ok; `ab.py build` without warnings. No test contradicted the
specification; the points below are where I had to choose.

1. **Band index outside 0..1** (V1, V4, V5; C4): V1 says "`b` is 0 or 1 at
   every caller", V4 and V5 say nothing. The object would read
   `cm+0x28+b*0x30` for any `b`, beyond the two band records; the open
   structure has only `band[2]`. I return 0 for `b >= 2` in V1, V4 and V5
   (and so in V2/V3 for a `bandunit` above 1). The tests use 0 and 1 only.
   Please confirm that no caller passes anything else.

2. **C3, field 7 of the bandwidth table**: the spec says the object does not
   read field 7 from a table but uses 0. I put 0 at index 7 of the 8-entry
   table, which gives the same `W`; the test of all 65,536 chanspecs agrees.
   Nothing to answer unless the object's 0 has a meaning beyond "no table".

## Resolution (coordinator, 2026-11-03)

1. Every caller of V1, V4 and V5 in the object passes 0 or 1 (the current
   band's unit, the band of a chanspec, or the other band of V3); returning 0
   for anything else is fine and cannot be observed.
2. Right: the object uses 0 for field 7 without reading a table; a table
   entry of 0 gives the same result.
