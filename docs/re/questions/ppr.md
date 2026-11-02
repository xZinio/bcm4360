# Questions from the implementation of `ppr` (open/lib/ratepwr.c)

Specification: `docs/re/spec/ppr.md`. Test results: `ab.py run ppr` 0
differences (94579 stages); `hybrid.py check` "the same" with the defaults,
`--off ppr`, `--on ppr`, `--on phy` and `--on phy --off ppr`. No test
contradicted the specification; the points below are where I had to choose.

1. **Serialisation flags, bits above 2** ("Serialised form"): the spec
   names bits 0..1 (chains) and bit 2 (TXBF) and says nothing of the rest. I
   mask (`flags & 3`, `flags & 4`) in `ppr_ser_set`, the length check of
   `ppr_serialize` and `ppr_deser_set`. The test uses random flags and
   reports no difference, so the object seems to ignore the other bits too.
   Please confirm.

2. **`ppr_get_mcs_group` outside the three MCS tables** (undefined in the
   object): I return no group for `nss` 1 with `chains` 0 and for any table
   index >= 24. I compute the index as an unsigned 64-bit number, so a `mode`
   of 0xffffffff gives no group. If the object sign-extends `mode`, it would
   read the word before the table instead. The test does not use such modes.

3. **`ppr_get_max` with a bandwidth word >= 3**: returns -128, as the task
   says (the object reads the byte after the header, outside the block).

4. **`ppr_serialize` with a bandwidth word above 0xff**: as specified, the
   length check uses the whole 32-bit word (`x 1` for anything other than 1
   and 2), but the sets written depend on its low byte. So a container made by
   `ppr_create(osh, 0x101)` (4 bytes) would have two sets read from beyond its
   end. I reproduced this literally. No caller makes such a container. Is
   reading by the low byte really what the object does (the spec says "by the
   low byte of `p->ch_bw`" in step 5 only)?

5. **Which copies use `osl_memcpy`/`osl_memset`**: the spec lists these
   imports but not which operations call them. I use them for the bulk fills
   and copies (init, clear, set_cmn_val, copy_struct, group get/set,
   force_disabled of a whole group, serialisation) and plain loops elsewhere.
   The tests do not compare these calls.

## Resolution (coordinator, 2026-11-02)

All five are accepted as implemented; none is reached by a caller of the
object. 1: the flags word's bits above 2 select nothing in the object's walk
either (ppr.md, serialised form). 2 and 3: undefined inputs, left out by the
test as the task says. 4: specified so (ppr.md, `ppr_serialize` steps 3 and
5); no container with a bandwidth word above 0xff exists. 5: the
specification does not make the `osl_mem*` calls part of the contract.
