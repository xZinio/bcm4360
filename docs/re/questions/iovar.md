# Questions from the implementation of `iovar` (open/lib/iovar.c)

Specification: `docs/re/spec/iovar.md`. Test results: `hybrid.py iovar`
"the same" (3,054 calls, 21,186 accesses), the same with `--off iovar`;
`hybrid.py check` "the same" (41,416 accesses); `ab.py all` all ok;
`ab.py build` without warnings. No test contradicted the specification; the
points below are where I had to choose.

1. **`wlc_bsscfg_find` returns NULL but leaves `err` at 0** (step 3.3): the
   spec says `err` is set on failure, so I take a 0 `err` to mean a
   configuration was found and ask for its interface (`bsscfg+0x10`) without
   testing the pointer, which would read through NULL, as I assume the object
   does. Can `wlc_bsscfg_find` return NULL without setting `err`?

2. **The index of a `bsscfg:` request when `params` and `arg` are both NULL**
   (steps 2.1, 3.2): `params` becomes `arg` (NULL) and `p_len` becomes `len`;
   with `len >= 4` the index is then read through NULL. I reproduced this
   literally. The tests do not make such a call; presumably no caller does.

## Resolution (coordinator, 2026-11-03)

1. Right as written: the object also reads the interface at `bsscfg+0x10`
   right after `err` is found 0, without testing the pointer
   (`wlc_bsscfg_find` sets `err` whenever it returns NULL).
2. Right as written: the object reads the index through `params` as it is
   then, NULL included; no caller makes such a call.
