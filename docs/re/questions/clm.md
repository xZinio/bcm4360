# Questions from the implementation of `clm` (open/lib/clm.c)

Specification: `docs/re/spec/clm.md`. Test results: `ab.py run clm` 0
differences (145780 stages), `hybrid.py check --on clm` "the same". None of
the points below was contradicted by a test; they are guesses or additions
where the specification is silent or the object would misbehave.

1. **`clm_agg_country_map_lookup`, step 3, `sub_076781(1, &a.id, NULL, &a)`.**
   Taken literally the key (`a.id`) and the output (`a`) alias, so the first
   aggregate read would overwrite the key and always "match" item 0 of the
   base set. The spec's craft.py runs (fallback to the base aggregate,
   0 differences against the model) suggest the object compares against a
   copy of the key. I copy the key before the search. Please confirm (only
   reachable with an incremental set; not covered by `ab.py run clm`).

2. **`clm_agg_country_map_lookup` with an invalid id** (spec open question 5):
   the object uses an uninitialised structure; I return -1 when
   `sub_076708` fails, as the spec suggests.

3. **`sub_07512f` channel argument.** The spec passes `channel` to
   `sub_0748c3`; it also says only the low 8 bits of the channel matter in
   `clm_limits`. I pass `channel & 0xff`. Does the object pass the full
   32-bit value (it would differ only for channel arguments >= 256)?

4. **Relocation of embedded pointers.** For the rule-set item pointer
   (`clm_limits` step 6) and the advertised-cc list pointer, the spec says
   "relocated"; I use `ptr ? ptr + delta : NULL` as for every other pointer.
   `clm_agg_find_region` adds the delta after its own `list == 0` check, as
   specified.

5. **`bw_map` (.bss+0xf18).** The open code uses the constant map directly
   instead of a pointer stored by `clm_init`. Calls before `clm_init` would
   fault in the object; here they work on an empty (zeroed) state. Behaviour
   after `clm_init` is identical.

6. **Locale index width** (`sub_07508d`): I take `full` as 8 bits plus 2 for
   each present extra byte (`loc_hi1_idx`, `loc_hi2_idx`). The layout table
   never gives `loc_hi2` without `loc_hi1`, so the case where only `loc_hi2`
   exists is unspecified. Likewise root flags bits 18..19 = 3: followed the
   table literally (`loc_hi1` only, no `loc_hi2`).

7. **Guards added where the object has no check and would read or write out
   of bounds or loop forever** (no effect on valid data; the database and the
   tests never reach them): a comb triple with step 0; channel numbers >= 200
   in a channel vector (not written); rate indices >= 336 (not applied; class
   treated as HT/VHT in `clm_limits`, skipped in `clm_country_flags`); a NULL
   rate set list (no rates); a NULL extra-region item when `rev_hi_idx >= 0`
   (`sub_076611` step 3: out not written); a NULL header in the app version
   getters (NULL). The locale walk keeps the object's missing bounds check
   (idx is walked as given).

8. **Stale state on reload** (spec open question 6): reproduced as specified -
   fields a load does not write keep their old values, a failed flags check
   leaves the new root/delta active with the old layout. The locale walk
   cache of a slot is dropped whenever that slot's root/delta change (also on
   the failed-flags path and on `NULL`).

9. **Uninitialised `out` in `clm_country_advertised_cc`**: with region format
   > 2 (or a missing extra-region item) `sub_076611` writes nothing and the
   object compares stale stack contents. I keep one `out` per call,
   initialised to zero, carried across the loop.
