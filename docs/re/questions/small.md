# Questions from the implementation of `small` (open/mac/offload.c, open/chip/nvram.c, open/lib/module.c, open/chip/si_pcie.c)

Specifications: `docs/re/spec/bmac-init.md` A8 (tcm_sem_enter / tcm_sem_exit,
"Exactly"), `docs/re/spec/srom-otp.md` "nvram_ro.c: the global NVRAM" and
"Notes for an implementation of nvram_ro.c", `docs/re/spec/iovar.md` "Module
register and unregister", `docs/re/spec/chip.md` "si_survive_perst_war".
No test contradicted the specification; the points below are where I had to
choose.

1. **`nvram_getall` and the end of a node's data** ("nvram_ro.c", nvram_getall
   and "Detail of nvram_getall"): `nvram_get` is described as walking a node
   "while inside the data and the string is not empty", `nvram_getall` only
   "up to the first empty one" - no bound by the node's length is mentioned.
   I implemented `nvram_getall` literally, without the bound: a node whose
   data does not end in an empty string is copied on past its length until an
   empty string is found. `hybrid.py small` reports the same, but its blocks
   may all end in an empty string (or be followed by a zero byte) so that
   both readings give the same result. Does the object's `nvram_getall` stop
   at the end of the data as `nvram_get` does?

2. **`nvram_init` with a negative result of `osl_os_get_image_block`**: the
   specification says the blocks are read "until it returns 0", keeping "the
   length `n` of the last non-zero block". I loop while the result is not 0,
   so a negative result (a read error of the kernel's file read) is kept as
   `n` and the loop goes on. With a negative `n` no byte is converted and
   `buf[n] = 0` writes before the buffer. Does the object loop on `!= 0` or
   on `> 0`, and what does it do with a negative `n`?

3. **`pcie_survive_perst`: `val` masked or not** (chip.md, step 2:
   `mod(PCIE(0x000), mask, val)`): I used `mod` as `access.md` defines it,
   `(old & ~mask) | (val & mask)`. The phrase "R and W only if mask|val != 0"
   resembles `si_corereg`, which does not limit `val` to `mask`
   (`(old & ~mask) | val`). The test reports the same with masks and values;
   is `val` outside `mask` among its cases? Which is the object's?

4. **`si_survive_perst_war` when `si_setcore(0x800)` gives no window**:
   ChipCommon is always core 0, so `si_setcore` always succeeds on PCI; I
   write the PMU watchdog only when the result is not NULL (the delays, the
   restore of the core and of `PCICFG(0x80)` follow in both cases). The
   object presumably writes through the result without a test (address
   0x634, a fault). No caller reaches this.

5. **`nvram_get_global_vars` on an empty list**: reproduced literally, it
   reads through the NULL head as the specification says the object does
   (nothing calls it).

## Resolution (coordinator, 2026-11-03, as relayed to the implementer)

1. The walk of `nvram_getall` is bounded like that of `nvram_get`; a string
   that starts inside the data is copied whole, up to its NUL. Spec
   corrected ("Detail of nvram_getall"); `nvram_copy_node` fixed.
2. The object loops while the result is not 0, as implemented.
3. `val` is not masked: `(old & ~mask) | val`. Spec corrected (chip.md,
   step 2); `pcie_survive_perst` fixed.
4. The object writes without a check; NULL cannot occur there. The check
   stays.
5. Fine as implemented.

After the fixes, with the new cases of `hybrid.py small` (nvram 648, perst
33 calls): `small` and `small --off mac/nvram/iovar/si` the same (20,200
accesses), `check` and `check --on phy` the same (41,416 accesses),
`ab.py build` without warnings.
