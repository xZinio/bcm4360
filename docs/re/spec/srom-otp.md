# SROM, OTP and the board variables made from them

Topic `srom-otp`: `bcmsrom.c` (.text 0x004508..0x00a1ec), `bcmotp.c`
(0x003244..0x004508), `nvram_ro.c` (0x019830..0x019b94) and the variable
helpers of `siutils.c` that `srom_var_init` calls, for the BCM4360 on PCIe
(ChipCommon rev 43). Purpose: an open `srom_var_init` (and `srom_read`) that
produces **the identical variable buffer** as the object - same variables,
same order, same text, same length - and leaves the same state behind.

Status: complete for the PCIe paths (analyst srom-otp, 2026-11-02): SROM,
OTP and NVRAM fall-back, all verified byte for byte against the object in
the emulator (743 comparisons, 0 differences, see "Verification").
`srom_parsecis` (PCMCIA only) is not described.

## Scope

| Function | .text offset | Size | Name |
|---|---|---|---|
| `srom_var_init` | 0x009704 | 2792 | original (contains the inlined `initvars_srom_pci`) |
| `sub_009142` | 0x009142 | 322 | assigned: `sprom_read_pci` |
| `sub_009284` | 0x009284 | 262 | assigned: `otp_read_pci` (reads the OTP hardware region as an SROM image) |
| `sub_004523` | 0x004523 | 274 | assigned: `varbuf_append` |
| `sub_004635` | 0x004635 | 110 | assigned: `initvars_table` |
| `srom_read` | 0x00938a | 476 | original |
| `si_is_sprom_available` | 0x01f509 | 483 | original (siutils.c) |
| `si_chipcontrl_read` | 0x01fdea | 70 | original (siutils.c) |
| `si_chipcontrl_restore` | 0x01fe30 | 70 | original (siutils.c) |
| `si_chipcontrl_srom4360` | 0x01fcfe | 89 | original (siutils.c) |
| `otp_init` | 0x00329f | 195 | original |
| `otp_read_region` | 0x0033ff | 157 | original |
| `otp_read_word` | 0x003362 | 157 | original |
| `otp_read_bit` | 0x00349c | 84 | original |
| `otp_status` | 0x003279 | 13 | original |
| `otp_size` | 0x003286 | 12 | original |
| `otp_nvread` | 0x003292 | 13 | original |
| `sub_003d6a` | 0x003d6a | 1950 | assigned: `ipxotp_init` |
| `sub_003983` | 0x003983 | 123 | assigned: `ipxotp_otpr` |
| `sub_0038d3` | 0x0038d3 | 176 | assigned: `ipxotp_read_bit` |
| `sub_003a53` | 0x003a53 | 373 | assigned: `ipxotp_read_region` |
| `sub_0039fe` | 0x0039fe | 85 | assigned: `ipxotp_read_word` |
| `sub_00324d` | 0x00324d | 12 | assigned: `ipxotp_size` |
| `sub_003244` | 0x003244 | 9 | assigned: `ipxotp_status` |
| `sub_003259` | 0x003259 | 11 | assigned: `ipxotp_nvread` |
| `si_is_otp_disabled` | 0x01e29b | 180 | original (siutils.c) |
| `si_is_otp_powered` | 0x01e987 | 23 | original (siutils.c) |
| `si_otp_power` | 0x01e964 | 35 | original (siutils.c) |
| `srom_parsecis` | 0x0046a3 | 19103 | original (only its reachability is described) |
| `sub_009566` | 0x009566 | 414 | assigned: `initvars_flash_pci` |
| `si_devpath` | 0x01efe9 | 190 | original (siutils.c) |
| `si_coded_devpathvar` | 0x01f0a7 | 276 | original (siutils.c) |
| `sub_0217e2` | 0x0217e2 | 105 | assigned: `si_devpathvar` |
| `si_getdevpathvar` | 0x02184b | 107 | original (siutils.c) |
| `si_getnvramflvar` | 0x01efdc | 13 | original (siutils.c) |
| `nvram_init` | 0x019a5f | 309 | original (nvram_ro.c) |
| `nvram_append` | 0x019a0d | 82 | original (nvram_ro.c) |
| `nvram_get` | 0x019916 | 135 | original (nvram_ro.c) |
| `nvram_getall` | 0x019869 | 173 | original (nvram_ro.c) |

## Overview

`si_doattach` (`sub_022b10`, siutils.c) calls, after the backplane scan, the
PCIe core set-up and `nvram_init`:

    rc = srom_var_init(sih, bustype, regs, sii->osh, &vars, &varsz);
    if (rc != 0) -> si_doattach fails (returns NULL; PCI: pcicore_deinit) -> attach fails

then reads `regwindowsz` (PCMCIA only), `boardtype`, `subvid`, `boardrev`,
`boardflags`, `xtalfreq`, `leddc`, `muxenab` ... from `vars` with
`getvar`/`getintvar` (utils.md). `si_attach` stores `vars`/`varsz` in
`sii+0xa8` / `sii+0xb0`; `wlc_attach` gets them through
`wlc_bmac_si_attach(..., &vars, &varsz)` and stores them in `pub+0x108`
(`pub->vars`) and `pub+0x110` (length). Every later variable lookup of the
driver (PHY attach, MAC attach, PMU, LEDs) goes to this buffer first (and to
the global NVRAM only when a name is missing, `getvar` in utils.md).

For bus type 1 (PCI) - the only one used by this driver - `srom_var_init`:

1. reads up to 234 words of SROM through the ChipCommon SROM window
   (BAR0 + 0x3800 = ChipCommon core offset 0x800 when the core is in the BAR0
   window), checking the CRC for 234, then 220, then 64 words; or, if the
   chip says no SROM is present, the OTP hardware region as an SROM image;
2. determines the SROM revision from the image;
3. if neither SROM nor OTP gives an image, takes `sromrev` from the device-path
   variables of the global NVRAM and copies all device-path variables of this
   card (flash path, `sub_009566`);
4. otherwise converts the image into `name=value` strings driven by two
   layout tables in `.rodata` (common variables, then per-path variables with
   the path digit appended), appending each string with `varbuf_append`,
   which removes an earlier string of the same name;
5. copies the result into a buffer of exactly the used size (`initvars_table`)
   and returns it in `*vars` / `*varsz`.

## Data

### The variable buffer ("varbuf")

A local of `srom_var_init` (3 fields, on the stack):

| Offset | Size | Field |
|---|---|---|
| 0x00 | 8 | `base`: start of the 0x1000-byte work buffer |
| 0x08 | 8 | `cur`: where the next string is written |
| 0x10 | 4 | `size`: bytes left (starts at 0x1000) |

### Function-static state in .bss (only `srom_var_init` uses it)

No other function of the object reads or writes these addresses (index of all
data references). They keep the text of the last array variable between
calls; an open implementation has to reproduce them only if someone compares
memory - nothing in the object reads them outside `srom_var_init`.

| Address | Size | Content |
|---|---|---|
| `.bss+0x78` | 8 | pointer to the name (a string in `.rodata.str`) of the common-table array being built / built last |
| `.bss+0x80` | 4 | number of characters in `.bss+0x90` of the array being built (including the trailing comma of the last element) |
| `.bss+0x90` | 0x100 | text of the common-table array being built: elements, each followed by `,`; at the end the last `,` is replaced by NUL |
| `.bss+0x190` | 1 | flag "a common-table array is being built" (1 while elements are collected, 0 otherwise) |
| `.bss+0x1a0` | 0x100 | text of the per-path array being built (same format as `.bss+0x90`) |

(The index lists these as `.bss+0x7c` and `.bss+0x18f`: those are the raw
relocation targets of instructions that carry an immediate operand after the
address field - `mov dword [rip+x], 0` and `cmp byte [rip+x], 0` - whose
displacement is counted from the end of the instruction. The addresses used
are `.bss+0x80` and `.bss+0x190`; `blob.py dis` shows them corrected.)

The per-path array uses locals for its flag, position and name.

### SROM image signatures and revision locations

The image is kept as 16-bit words in a 0x300-byte buffer (384 words); word `n`
is at byte `2n`, little endian (as read with `osl_readw`).

| Image | Signature | Revision byte |
|---|---|---|
| 234 words (SROM rev 11; also the OTP image with this signature) | word 64 == 0x0634 | low byte of word 233 |
| 220 words (SROM rev 4, 5: signature at word 32; rev 8, 9, 10: at word 64) | word 32 == 0x5372 or word 64 == 0x5372 | low byte of word 219 |
| 64 words (SROM rev 1..3; "0x10" means rev 1) | none (CRC only) | low byte of word 63 |

The high byte of the last word is the CRC (see `sprom_read_pci`).

The accepted revisions are those with bit `rev` set in 0xf3e: 1, 2, 3, 4, 5,
8, 9, 10, 11 (the shift count is `rev & 31`, so a revision byte of 33..37 or
40..43 would pass this test too and select the bits of rev-32; such images are
not considered further).

## Procedures

### srom_var_init (.text+0x009704, name original)

Prototype (System V, all six in registers):

    int srom_var_init(si_t *sih, uint bustype, void *curmap, osl_t *osh,
                      char **vars, uint *count);

`curmap` is the mapped BAR0 (the register window), `osh` is ignored for
bus types 0, 1 and 4 (replaced by `si_osh(sih)`).

Result: 0 on success; -1 (NULL `curmap`, unknown bus type, no image and no
NVRAM `sromrev`); -2 (allocation failure of the image buffer or the work
buffer, or an unsupported revision); -27 (`BCME_NOMEM`: the final copy in
`initvars_table`, the 64 KB buffer of `initvars_flash_pci`, or - bus types
0/4 only - the work buffer); -14 (`BCME_BUFTOOSHORT`, NVRAM fall-back
overflow). `*vars` / `*count` are only non-zero on success with a non-empty
buffer. Note: NULL `vars` or `count` gives 0 (success) with nothing done.

Steps for bus type 1 (PCI):

1. If `count` or `vars` is NULL: return 0 (nothing else happens). Else set
   `*vars = NULL`, `*count = 0`.
2. Bus type dispatch: 1 -> step 3. 0 (SI bus) or 4: flash path only (see
   `sub_009566`, below: malloc 0x1000, `sub_009566`, `initvars_table`, free).
   2 (PCMCIA): CIS path (`srom_read` of 0x300 bytes or attribute memory,
   `srom_parsecis`), not described. Anything else: return -1.
3. If `curmap` is NULL: return -1. `osh = si_osh(sih)`. `img =
   osl_malloc(osh, 0x300)`; NULL: return -2. (The buffer is not cleared.)
4. SROM window: if ChipCommon rev (`sih+0x14`) <= 31: `curmap + 0x1000`.
   Otherwise (the 4360: rev 43): `curmap + 0x3800` if bit 30 of the
   ChipCommon capabilities (`sih+0x18`, tested as bit 6 of byte `sih+0x1b`)
   is set, else NULL.
5. `si_is_sprom_available(sih)` true (SROM present):
   1. `r = sprom_read_pci(osh, sih, window, 0, img, 234, check_crc=1)`.
      `r == 0`: if word 64 == 0x0634, `rev` = low byte of word 233, else
      `rev = 0` (which fails in step 7). Go to step 7.
   2. Else `r = sprom_read_pci(..., 220, 1)`. `r == 0`: if word 32 == 0x5372
      or word 64 == 0x5372, `rev` = low byte of word 219, else `rev = 0`.
      Go to step 7.
   3. Else `r = sprom_read_pci(..., 64, 1)`. `r == 0`: `rev` = low byte of
      word 63; if that is 0x10, `rev = 1`. Go to step 7.
   4. Else go to step 6.

   `si_is_sprom_available` false (no SROM): `r = otp_read_pci(osh, sih, img,
   0x300)` (`sub_009284`). `r != 0`: step 6. `r == 0`: if word 64 == 0x0634,
   `rev` = low byte of word 233; else `rev` = low byte of word 219, and if
   word 219 is 0x5372, `rev` = low byte of word 229 instead. Go to step 7.

   (Each `sprom_read_pci` reads its words from the window again, from word 0,
   into the same buffer: a 220-word attempt leaves words 220..233 of the
   failed 234-word attempt in place, and a 64-word attempt leaves words
   64..233. Revision 10 uses common words up to 228, which therefore come
   from the first read; the hardware returns the same words, so the effect is
   that of a 234-word image. Words never read are whatever `osl_malloc`
   returned - zero in the emulator; no accepted revision uses them.)
6. No image: `v = si_getdevpathvar(sih, "sromrev")`. If found: `rev =
   bcm_strtoul(v, NULL, 0)` (low 8 bits), `flash = 1`, go to step 7.
   Not found: `v = si_getnvramflvar(sih, "sromrev")`; free `img` (0x300) and
   return 0 if found (with `*vars` NULL and `*count` 0), -1 if not.
7. If bit `rev & 31` of 0xf3e is clear: free `img`, return -2.
8. `buf = osl_malloc(osh, 0x1000)`; NULL: free `img`, return -2.
9. If `flash`: `end = buf`; `r = sub_009566(sih, osh, &end)`; if `r == 0`:
   `r = initvars_table(osh, buf, end, vars, count)`. Free `buf` (0x1000) and
   `img` (0x300), return `r`.
10. Otherwise convert the image (next section, "Conversion"), giving the end
    pointer `end` (one past the final extra NUL). `r = initvars_table(osh, buf,
    end, vars, count)`. Free `buf` (0x1000), free `img` (0x300), return `r`.

Allocation order and sizes (they matter if allocations are compared): 0x300
(image), [inside `otp_read_pci`: 0x300, freed again], 0x1000 (work buffer),
[flash path: 0x10000 inside `initvars_flash_pci`, freed again],
[`initvars_table`: exactly the used length], then free 0x1000, free 0x300.
(All verified, see Verification.)

#### Conversion of an image into variables (steps of 10)

    vb = { base = buf, cur = buf, size = 0x1000 }
    varbuf_append(&vb, "sromrev=%d", rev)
    walk the common table (.rodata+0x1b00), word base 0           (A)
    if rev > 3: for each path p: walk the per-path table (.rodata+0x50d0)
                with the word base of path p, path digit p          (B)
    *vb.cur = 0 ; end = vb.cur + 1          (the empty string that ends the list)

The tables are arrays of 24-byte entries ending with an entry whose name
pointer is NULL:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 8 | `name` (pointer to a NUL-terminated string; continuation entries point to `""`) |
| 0x08 | 4 | `revmask`: bit `r` set = the entry applies to SROM revision `r` |
| 0x0c | 4 | `flags` (below) |
| 0x10 | 2 | `off`: word number in the image (per-path table: relative to the path base) |
| 0x12 | 2 | `mask`: bits of the word that hold the value |
| 0x14 | 4 | padding (0 in all entries) |

| Flag | Name (Broadcom style) | Meaning in this walk |
|---|---|---|
| 0x001 | MORE | (common table only) the value continues in the next entry: the next entry's bits are the next more significant bits |
| 0x002 | NOFFS | skip the variable if its value is all ones (over its full width) |
| 0x004 | PRHEX | print in hexadecimal (`0x%x`) |
| 0x008 | PRSIGN | print as signed (two's complement over the value's width); common table and array elements only |
| 0x010 | CCODE | two-character country code |
| 0x020 | ETHADDR | MAC address in three words |
| 0x040 | LEDDC | LED duty cycle (special packing) |
| 0x080 | NOVAR | read nothing, make no variable |
| 0x100 | ARRAY | element of an array; the array continues with the next entry and ends with the first entry without this flag (rev >= 10 only) |

Common table: 573 entries (terminator at .rodata+0x50b8). Per-path table: 72
entries (terminator at .rodata+0x5790). Masks are all contiguous runs of bits.

Value of an entry: `lo` = number of the lowest set bit of `mask`, `width` =
(highest set bit) - `lo` + 1 (both 0 if `mask` is 0); `value = (word[off] &
mask) >> lo`.

**(A) common table walk.** `mask_rev = 1 << (rev & 31)`. For each entry `e` in
order:

1. If `e.revmask & mask_rev` is 0: next entry. (Continuation entries of
   variables not in this revision either have revmask 0 or the same revmask as
   their head, so they are skipped here too.)
2. If `e.flags` has NOVAR: next entry.
3. If ETHADDR: the six bytes are, for `k` = 0, 1, 2: high byte of
   `word[e.off + k]`, then its low byte. Text: `bcm_ether_ntoa` - six
   two-digit lower-case hexadecimal numbers separated by `:` (17 characters).
   Append `"<name>=<text>"`. Next entry. (No NOFFS test, no MORE handling.)
4. If `rev > 9`, `e.flags` has ARRAY and no common array is being built:
   start one - `.bss+0x80 = 0`, `.bss+0x78 = e.name`, clear the 256 bytes at
   `.bss+0x90`, `.bss+0x190 = 1`.
5. `value`, `width` of `e` as above. Then, while the *current* entry has
   MORE: step to the next entry `c`; if `c.off` is not 0: `value +=
   ((word[c.off] & c.mask) >> lo(c)) << width; width += width(c)`. (A
   continuation entry with `off` 0 would contribute nothing and not add to the
   width - none exists in the table. Continuation entries' revmask and flags
   other than MORE/ARRAY are not looked at.) Let `last` be the entry the loop
   stopped at (`e` itself if it has no MORE). `F = e.flags` (the head's flags)
   is used for the format; `last.flags` for the array test.
6. If `F` has NOFFS and `value == (1 << width) - 1` (all ones; 32-bit
   arithmetic, shift count taken modulo 32 - no NOFFS entry is 32 bits wide):
   next entry (after `last`). This applies to array elements too: such an
   element is left out of the array text (no array element has NOFFS in the
   table).
7. If `rev <= 9` and `last` has ARRAY: skip forward over the array - step
   past `last` and past every following entry up to and including the first
   one without ARRAY, then one entry further: the entry after that first
   non-ARRAY entry is not visited at all. Then go on with step 9 (print the
   first element's value as a scalar under the array's name). (Revisions 8 and
   9 contain no ARRAY entries in either table, so this has no effect for them;
   it is described for completeness.)
8. If a common array is being built (`.bss+0x190` = 1):
   * element text: if `F` has PRHEX: `"0x%x,"` of `value`; else if `F` has
     PRSIGN and bit `width-1` of `value` is set: `"%d,"` of `value | (-1 <<
     width)` (negative); else `"%u,"` of `value`;
   * `n = snprintf(.bss+0x90 + pos, 0x100 - pos, text...)` with `pos` =
     `.bss+0x80`;
   * if `n > 0`: `pos += n` (stored back to `.bss+0x80`). If `last` has ARRAY:
     next entry. Otherwise the array is complete: `.bss+0x190 = 0`, the
     character at `.bss+0x90 + pos - 1` (the last comma) becomes NUL, and
     `"<.bss+0x78 name>=<.bss+0x90 text>"` is appended. Next entry.
   * if `n <= 0` (does not happen with the kernel's `snprintf`): skip entries
     while they have ARRAY (as in step 7, the entry after the first non-ARRAY
     one is skipped as well), `.bss+0x190 = 0`, nothing is appended.
9. Not in an array:
   * CCODE: value 0 -> append `"ccode="`; else `"ccode=%c%c"` of
     `value >> 8` and `value & 0xff`. (The name is the literal `ccode`, not
     taken from the entry - it is the same.)
   * LEDDC: append `"leddc=%d"` of `((value & 0xff) << 8) | ((value >> 8) <<
     24)` as a signed 32-bit number: for word 0xHHLL the number 0xHH00LL00.
     (Literal name `leddc`.)
   * PRHEX: `"<name>=0x%x"` of `value` (lower case, no leading zeros).
   * PRSIGN and bit `width-1` of `value` set: `"<name>=%d"` of `value | (-1
     << width)`.
   * otherwise `"<name>=%u"` of `value`.
10. Next entry (the one after `last`).

Each "append" is one `varbuf_append(&vb, format, ...)` call.

**(B) per-path walk** (only if `rev > 3`). Path layout:

| Revision | Paths | Base of path 0 (word) | Words per path |
|---|---|---|---|
| 11 (and anything > 10 that passed step 7) | 3 | 108 | 20 |
| 8, 9, 10 | 4 | 96 | 16 |
| 4, 5 | 4 | 64 | 23 |

For `p` = 0 .. paths-1 (`base` = base of path 0 + `p` * words per path), for
each entry `e` of the per-path table in order:

1. If `e.revmask & mask_rev` is 0, or `e.flags` has NOVAR: next entry.
2. If `rev > 9`, no per-path array is being built and `e.flags` has ARRAY:
   start one: clear the 256 bytes at `.bss+0x1a0`, flag on, position 0,
   array name = `e.name`.
3. `value = (word[base + e.off] & e.mask) >> lo`, `width` as above. (No MORE
   handling in this table; no entry has MORE.)
4. NOFFS and `value == (1 << width) - 1`: next entry.
5. Array being built: element text as in (A) 8 (PRHEX `"0x%x,"`, PRSIGN with
   sign extension `"%d,"`, else `"%u,"`), `snprintf` into `.bss+0x1a0 + pos`
   with room `0x100 - pos`; if `n > 0`: `pos += n`; if `e` has ARRAY: next
   entry; else replace the last comma by NUL and append
   `"%s%d=%s"` of (array name, `p`, `.bss+0x1a0`), flag off. (`n <= 0`:
   skip as in (A) 8, flag off.)
6. Not in an array: PRHEX: append `"%s%d=0x%x"` of (`e.name`, `p`, `value`);
   otherwise `"%s%d=%d"` of (`e.name`, `p`, `value`). **PRSIGN is ignored for
   per-path scalars**: the unsigned field value is printed in decimal.

The per-path array flag, position and name are not reset between paths (an
array never spans the end of the table, so this has no effect).

#### Revisions other than 11

Revisions 1..3 (64-word images): common table entries for those revisions,
no per-path walk. Revisions 4, 5: common table plus 4 paths at word 64, 23
words apart. Revisions 8, 9: other common entries (revmask bits 8/9), 4 paths
at word 96, 16 apart; no arrays. Revision 10: as 8/9 plus arrays (including
`swctrlmap_2g`, an array whose elements are 32-bit MORE pairs). Revisions 12
and 13 are rejected by step 7 (-2).

#### The conversion table for SROM revision 11 (complete)

For revision 11 the walk above produces exactly these strings, in this order:
`sromrev=11`, then rows 1..125 (common table), then rows 126..141 for path 0
(word base 108), then rows 126..141 for path 1 (base 128), then for path 2
(base 148), each per-path name followed by the path digit (`maxp2ga0`,
`pa2ga0`, ..., `pa5ga0`, `maxp2ga1`, ...). No name occurs twice, so the
de-duplication of `varbuf_append` never acts for revision 11. A row is left out
only where "NOFFS" is given and the field is all ones (0xff for an 8-bit
field, 0xf for 4 bits, 0xffff for 16 bits).

"Word" is the word number in the image (per-path rows: added to the path
base). "Mask" selects the field; the value is the field shifted down to bit 0.
Formats: *unsigned* `name=%u`; *decimal* `name<p>=%d` of the (non-negative)
field value; *hex* `name=0x%x` (lower case, no leading zeros, `0x0` for 0);
*32 bit*: the first word gives bits 0..15, the second bits 16..31; *array of
n*: `name=e1,e2,...,en` with each element in the given format (hex arrays
`0x%x`, unsigned arrays `%u`), no spaces; *MAC address*: `macaddr=` + six
bytes `%02x` separated by `:`, word 72 high byte first, then word 72 low byte,
word 73 high, low, word 74 high, low; *country code*: `ccode=` followed by the
character `word >> 8` and the character `word & 0xff` (`ccode=` alone when the
word is 0; see the note on zero bytes in "Open questions"); *LED duty cycle*:
`leddc=%d` of the signed 32-bit number `(w & 0xff) << 8 | (w >> 8) << 24`.
The "Table entries" column gives the entry numbers (0-based) in the object's
table, for cross-checking.

| # | Variable | Word(s) | Mask | Format | Table entries |
|---|---|---|---|---|---|
| - | (devid: read, no variable) | 48 | 0xffff | NOVAR | 0 |
| 1 | boardrev | 65 | 0xffff | hex | 3 |
| 2 | boardflags | 66 (low), 67 (high) | 0xffff each | hex, 32 bit | 13..14 |
| 3 | boardflags2 | 68 (low), 69 (high) | 0xffff each | hex, 32 bit | 19..20 |
| 4 | boardtype | 2 | 0xffff | hex | 21 |
| 5 | subvid | 3 | 0xffff | hex | 22 |
| 6 | boardflags3 | 70 (low), 71 (high) | 0xffff each | hex, 32 bit | 374..375 |
| 7 | boardnum | 74 | 0xffff | unsigned | 376 |
| 8 | macaddr | 72, 73, 74 | 0xffff | MAC address | 377 |
| 9 | ccode | 75 | 0xffff | country code | 378 |
| 10 | regrev | 76 | 0x00ff | unsigned | 379 |
| 11 | ledbh0 | 77 | 0x00ff | unsigned, NOFFS | 380 |
| 12 | ledbh1 | 77 | 0xff00 | unsigned, NOFFS | 381 |
| 13 | ledbh2 | 78 | 0x00ff | unsigned, NOFFS | 382 |
| 14 | ledbh3 | 78 | 0xff00 | unsigned, NOFFS | 383 |
| 15 | leddc | 79 | 0xffff | LED duty cycle, NOFFS | 384 |
| 16 | aa2g | 80 | 0x00ff | unsigned | 385 |
| 17 | aa5g | 80 | 0xff00 | unsigned | 386 |
| 18 | agbg0 | 81 | 0xff00 | unsigned | 387 |
| 19 | agbg1 | 81 | 0x00ff | unsigned | 388 |
| 20 | agbg2 | 82 | 0xff00 | unsigned | 389 |
| 21 | aga0 | 82 | 0x00ff | unsigned | 390 |
| 22 | aga1 | 83 | 0xff00 | unsigned | 391 |
| 23 | aga2 | 83 | 0x00ff | unsigned | 392 |
| 24 | txchain | 84 | 0x000f | unsigned, NOFFS | 393 |
| 25 | rxchain | 84 | 0x00f0 | unsigned, NOFFS | 394 |
| 26 | antswitch | 84 | 0xff00 | unsigned, NOFFS | 395 |
| 27 | tssiposslope2g | 85 | 0x0001 | unsigned | 396 |
| 28 | epagain2g | 85 | 0x000e | unsigned | 397 |
| 29 | pdgain2g | 85 | 0x01f0 | unsigned | 398 |
| 30 | tworangetssi2g | 85 | 0x0200 | unsigned | 399 |
| 31 | papdcap2g | 85 | 0x0400 | unsigned | 400 |
| 32 | femctrl | 85 | 0xf800 | unsigned | 401 |
| 33 | tssiposslope5g | 86 | 0x0001 | unsigned | 402 |
| 34 | epagain5g | 86 | 0x000e | unsigned | 403 |
| 35 | pdgain5g | 86 | 0x01f0 | unsigned | 404 |
| 36 | tworangetssi5g | 86 | 0x0200 | unsigned | 405 |
| 37 | papdcap5g | 86 | 0x0400 | unsigned | 406 |
| 38 | gainctrlsph | 86 | 0xf800 | unsigned | 407 |
| 39 | tempthresh | 87 | 0xff00 | unsigned | 408 |
| 40 | tempoffset | 87 | 0x00ff | unsigned | 409 |
| 41 | rawtempsense | 88 | 0x01ff | hex | 410 |
| 42 | measpower | 88 | 0xfe00 | hex | 411 |
| 43 | tempsense_slope | 89 | 0x00ff | hex | 412 |
| 44 | tempcorrx | 89 | 0xfc00 | hex | 413 |
| 45 | tempsense_option | 89 | 0x0300 | hex | 414 |
| 46 | xtalfreq | 90 | 0xffff | unsigned | 415 |
| 47 | pa5gbw4080a1 | 91, 94, 103, 106, 148, 149, 150, 151, 152, 153, 154, 155 | 0xffff | array of 12, hex | 416..427 |
| 48 | phycal_tempdelta | 92 | 0x00ff | unsigned | 428 |
| 49 | temps_period | 92 | 0x0f00 | unsigned | 429 |
| 50 | temps_hysteresis | 92 | 0xf000 | unsigned | 430 |
| 51 | measpower1 | 93 | 0x007f | hex | 431 |
| 52 | measpower2 | 93 | 0x3f80 | hex | 432 |
| 53 | tssifloor2g | 95 | 0x03ff | hex | 433 |
| 54 | tssifloor5g | 96, 97, 98, 99 | 0x03ff | array of 4, hex | 434..437 |
| 55 | pdoffset2g40ma0 | 100 | 0x000f | unsigned | 438 |
| 56 | pdoffset2g40ma1 | 100 | 0x00f0 | unsigned | 439 |
| 57 | pdoffset2g40ma2 | 100 | 0x0f00 | unsigned | 440 |
| 58 | pdoffset2g40mvalid | 100 | 0x8000 | unsigned | 441 |
| 59 | pdoffset40ma0 | 101 | 0xffff | unsigned | 442 |
| 60 | pdoffset40ma1 | 102 | 0xffff | unsigned | 443 |
| 61 | pdoffset40ma2 | 103 | 0xffff | unsigned | 444 |
| 62 | pdoffset80ma0 | 104 | 0xffff | unsigned | 445 |
| 63 | pdoffset80ma1 | 105 | 0xffff | unsigned | 446 |
| 64 | pdoffset80ma2 | 106 | 0xffff | unsigned | 447 |
| 65 | subband5gver | 107 | 0xffff | hex | 448 |
| 66 | paparambwver | 200 | 0xf000 | unsigned | 449 |
| 67 | pa5gbw4080a0 | 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167 | 0xffff | array of 12, hex | 450..461 |
| 68 | pa5gbw40a0 | 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147 | 0xffff | array of 12, hex | 462..473 |
| 69 | pa5gbw80a0 | 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167 | 0xffff | array of 12, hex | 474..485 |
| 70 | pa2gccka0 | 129, 130, 131 | 0xffff | array of 3, hex | 486..488 |
| 71 | cckbw202gpo | 168 | 0xffff | unsigned | 489 |
| 72 | cckbw20ul2gpo | 169 | 0xffff | unsigned | 490 |
| 73 | mcsbw202gpo | 170 (low), 171 (high) | 0xffff each | unsigned, 32 bit | 491..492 |
| 74 | mcsbw402gpo | 172 (low), 173 (high) | 0xffff each | unsigned, 32 bit | 493..494 |
| 75 | dot11agofdmhrbw202gpo | 174 | 0xffff | unsigned | 495 |
| 76 | ofdmlrbw202gpo | 175 | 0xffff | unsigned | 496 |
| 77 | mcsbw205glpo | 176 (low), 177 (high) | 0xffff each | unsigned, 32 bit | 497..498 |
| 78 | mcsbw405glpo | 178 (low), 179 (high) | 0xffff each | unsigned, 32 bit | 499..500 |
| 79 | mcsbw805glpo | 180 (low), 181 (high) | 0xffff each | unsigned, 32 bit | 501..502 |
| 80 | mcsbw205gmpo | 184 (low), 185 (high) | 0xffff each | unsigned, 32 bit | 503..504 |
| 81 | mcsbw405gmpo | 186 (low), 187 (high) | 0xffff each | unsigned, 32 bit | 505..506 |
| 82 | mcsbw805gmpo | 188 (low), 189 (high) | 0xffff each | unsigned, 32 bit | 507..508 |
| 83 | mcsbw205ghpo | 192 (low), 193 (high) | 0xffff each | unsigned, 32 bit | 509..510 |
| 84 | mcsbw405ghpo | 194 (low), 195 (high) | 0xffff each | unsigned, 32 bit | 511..512 |
| 85 | mcsbw805ghpo | 196 (low), 197 (high) | 0xffff each | unsigned, 32 bit | 513..514 |
| 86 | mcslr5glpo | 200 | 0x0fff | unsigned | 515 |
| 87 | mcslr5gmpo | 201 | 0xffff | unsigned | 516 |
| 88 | mcslr5ghpo | 202 | 0xffff | unsigned | 517 |
| 89 | sb20in40hrpo | 203 | 0xffff | unsigned | 518 |
| 90 | sb20in80and160hr5glpo | 204 | 0xffff | unsigned | 519 |
| 91 | sb40and80hr5glpo | 205 | 0xffff | unsigned | 520 |
| 92 | sb20in80and160hr5gmpo | 206 | 0xffff | unsigned | 521 |
| 93 | sb40and80hr5gmpo | 207 | 0xffff | unsigned | 522 |
| 94 | sb20in80and160hr5ghpo | 208 | 0xffff | unsigned | 523 |
| 95 | sb40and80hr5ghpo | 209 | 0xffff | unsigned | 524 |
| 96 | sb20in40lrpo | 210 | 0xffff | unsigned | 525 |
| 97 | sb20in80and160lr5glpo | 211 | 0xffff | unsigned | 526 |
| 98 | sb40and80lr5glpo | 212 | 0xffff | unsigned | 527 |
| 99 | sb20in80and160lr5gmpo | 213 | 0xffff | unsigned | 528 |
| 100 | sb40and80lr5gmpo | 214 | 0xffff | unsigned | 529 |
| 101 | sb20in80and160lr5ghpo | 215 | 0xffff | unsigned | 530 |
| 102 | sb40and80lr5ghpo | 216 | 0xffff | unsigned | 531 |
| 103 | dot11agduphrpo | 217 | 0xffff | unsigned | 532 |
| 104 | dot11agduplrpo | 218 | 0xffff | unsigned | 533 |
| 105 | sar2g | 221 | 0x00ff | unsigned | 534 |
| 106 | sar5g | 221 | 0xff00 | unsigned | 535 |
| 107 | noiselvl2ga0 | 222 | 0x001f | unsigned | 536 |
| 108 | noiselvl2ga1 | 222 | 0x03e0 | unsigned | 537 |
| 109 | noiselvl2ga2 | 222 | 0x7c00 | unsigned | 538 |
| 110 | noiselvl5ga0 | 223, 224, 225, 226 | 0x001f | array of 4, unsigned | 539..542 |
| 111 | noiselvl5ga1 | 223, 224, 225, 226 | 0x03e0 | array of 4, unsigned | 543..546 |
| 112 | noiselvl5ga2 | 223, 224, 225, 226 | 0x7c00 | array of 4, unsigned | 547..550 |
| 113 | rxgainerr2ga0 | 227 | 0x003f | unsigned | 551 |
| 114 | rxgainerr2ga1 | 227 | 0x07c0 | unsigned | 552 |
| 115 | rxgainerr2ga2 | 227 | 0xf800 | unsigned | 553 |
| 116 | rxgainerr5ga0 | 228, 229, 230, 231 | 0x003f | array of 4, unsigned | 554..557 |
| 117 | rxgainerr5ga1 | 228, 229, 230, 231 | 0x07c0 | array of 4, unsigned | 558..561 |
| 118 | rxgainerr5ga2 | 228, 229, 230, 231 | 0xf800 | array of 4, unsigned | 562..565 |
| 119 | rpcal2g | 182 | 0xffff | unsigned | 566 |
| 120 | rpcal5gb0 | 183 | 0xffff | unsigned | 567 |
| 121 | rpcal5gb1 | 190 | 0xffff | unsigned | 568 |
| 122 | rpcal5gb2 | 191 | 0xffff | unsigned | 569 |
| 123 | rpcal5gb3 | 198 | 0xffff | unsigned | 570 |
| 124 | txidxcap2g | 212 | 0x0ff0 | unsigned | 571 |
| 125 | txidxcap5g | 214 | 0x0ff0 | unsigned | 572 |

Per-path rows (path `p` = 0, 1, 2; word = 108 + 20 `p` + the given number;
the variable name gets the digit `p` appended):

| # | Variable | Word(s), relative to the path base | Mask | Format | Table entries |
|---|---|---|---|---|---|
| 126 | maxp2ga | 0 | 0x00ff | decimal | 40 |
| 127 | pa2ga | 1, 2, 3 | 0xffff | array of 3, hex | 41..43 |
| 128 | rxgains5gmelnagaina | 4 | 0x0007 | decimal | 44 |
| 129 | rxgains5gmtrisoa | 4 | 0x0078 | decimal | 45 |
| 130 | rxgains5gmtrelnabypa | 4 | 0x0080 | decimal | 46 |
| 131 | rxgains5ghelnagaina | 4 | 0x0700 | decimal | 47 |
| 132 | rxgains5ghtrisoa | 4 | 0x7800 | decimal | 48 |
| 133 | rxgains5ghtrelnabypa | 4 | 0x8000 | decimal | 49 |
| 134 | rxgains2gelnagaina | 5 | 0x0007 | decimal | 50 |
| 135 | rxgains2gtrisoa | 5 | 0x0078 | decimal | 51 |
| 136 | rxgains2gtrelnabypa | 5 | 0x0080 | decimal | 52 |
| 137 | rxgains5gelnagaina | 5 | 0x0700 | decimal | 53 |
| 138 | rxgains5gtrisoa | 5 | 0x7800 | decimal | 54 |
| 139 | rxgains5gtrelnabypa | 5 | 0x8000 | decimal | 55 |
| 140 | maxp5ga | 6, 6, 7, 7 | 0x00ff, 0xff00, 0x00ff, 0xff00 | array of 4, unsigned | 56..59 |
| 141 | pa5ga | 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 | 0xffff | array of 12, hex | 60..71 |

Notes on the table: `boardnum` is the low word of the MAC address (word 74
serves both); `pa5gbw4080a0` and `pa5gbw80a0` read the same twelve words;
`paparambwver` and `mcslr5glpo` share word 200, `sb40and80lr5glpo` and
`txidxcap2g` word 212, `sb40and80lr5gmpo` and `txidxcap5g` word 214; the
common arrays `pa5gbw4080a1`, `pa2gccka0`, `pa5gbw40a0` and `pa5gbw4080a0` /
`pa5gbw80a0` read words inside the per-path blocks (108..167) as well. Words
0, 1, 4..64, 199, 219, 220, 232 and 233 make no variable for revision 11
(word 48 = `devid` is read as NOVAR and ignored; word 64 is the signature,
word 233 revision and CRC).

### varbuf_append (sub_004523, .text+0x004523, name assigned)

`int varbuf_append(varbuf_t *b, const char *fmt, ...)`: format a string at
the end of the variable buffer and append it, replacing an earlier string with
the same name. Result: the number of bytes appended (string length + 1), or 0.

1. If `b->size <= 1` (unsigned): return 0, nothing written.
2. `n = vsnprintf(b->cur, b->size, fmt, args)` (kernel semantics: at most
   `size - 1` characters and a NUL are written; `n` is the length the full
   text would have).
3. If `n == -1` or `n > b->size - 2` (signed comparison): `b->size = 0`
   (every later append is a no-op) and return 0. The truncated text stays in
   memory at `cur` but `cur` does not move (the final NUL of step 10 of
   `srom_var_init` overwrites its first byte).
4. `eq = strchr(b->cur, '=')`. If there is one, let `len = eq - b->cur` (the
   name length). Walk the strings from `b->base` while the walk pointer `s` is
   below `b->cur`: if the first `len` bytes of `s` equal those of the new text
   (`memcmp`) and `s[len]` is `=`: remove `s` - `k = strlen(s) + 1`;
   move the bytes from `s + k` up to and including the NUL of the new text
   (`b->cur + n`) down to `s` (`memmove`); `b->cur -= k`; `b->size += k`; stop
   the walk (only the first match is removed). Otherwise `s` advances past the
   NUL of its string.
5. `b->cur += n + 1`, `b->size -= n + 1`. Return `n + 1`.

So a repeated name moves to the end of the buffer with its new value. Cost:
every append scans the whole buffer (this is 7.6% of all execution in a
hybrid session).

Note on `%c` with a zero character: the kernel's `vsnprintf` stores the zero
byte and counts it in `n`; the appended "string" then contains an embedded
NUL (see `ccode` in Open questions).

### initvars_table (sub_004635, .text+0x004635, name assigned)

`int initvars_table(osl_t *osh, char *start, char *end, char **vars, uint
*count)`:

1. `c = end - start` (32-bit signed).
2. If `c <= 1`: `*vars = NULL`, `*count = 0`, return 0.
3. `v = osl_malloc(osh, c)`; NULL: return -27 (`*vars`/`*count` untouched).
4. Copy `c` bytes from `start` to `v`; `*vars = v`; `*count = c`; return 0.

The result is therefore `"sromrev=11\0...\0last=value\0\0"`, `*count` counting
both final NULs.

### sprom_read_pci (sub_009142, .text+0x009142, name assigned)

`int sprom_read_pci(osl_t *osh, si_t *sih, void *window, uint wordoff, u16 *buf,
uint nwords, bool check_crc)`. Result 0 = read (and CRC good if checked),
1 = blank or bad CRC.

1. If the chip id (`sih+0x3c`) is 0x4331, 0xa9a7, 0x4360, 0xa9c4 or 0x4352:
   `saved = si_chipcontrl_read(sih)` (see below). (Otherwise `saved = 0`.)
2. If the chip is 0x4331/0xa9a7: `si_chipcontrl_epa4331(sih, 0)` (other chips).
   If the chip is 0xa9c4, 0x4360 or 0x4352 and its revision (`sih+0x40`) is
   below 3: `si_chipcontrl_srom4360(sih, 1)`. **The BCM4360 rev 3 does
   neither** - the chip control register is read and later written back with
   the same value.
3. For `i` = 0 .. `nwords`-1: `buf[i] = osl_readw(window + 2 (wordoff + i))`
   (16-bit reads, ascending addresses).
4. If `check_crc`: result 1 if `buf[0] == 0xffff`; else `hndcrc8(buf, 2
   nwords, 0xff)` must be 0x9f (the CRC over the whole image including its CRC
   byte - the high byte of the last word - gives the fixed residue 0x9f):
   0x9f -> 0, else 1. Without `check_crc`: 0.
5. For the chips of step 1: `si_chipcontrl_restore(sih, saved)`.

Helpers (siutils.c), all keep the current core: save `sih+0x1c0` (current
core index), `si_setcore(sih, 0x800 /* ChipCommon */, 0)`, the access, then
`si_setcoreidx(sih, saved index)`:

* `si_chipcontrl_read`: returns `CC(0x28)` (32 bit; chip control).
* `si_chipcontrl_restore(sih, v)`: `CC(0x28) = v`.
* `si_chipcontrl_srom4360(sih, on)`: reads `CC(0x28)`; if `on`, writes it back
  with bits 2, 3, 8, 9 and 21 cleared (`& 0xffdffcf3`); if not `on`, no write.

Bus accesses of one 234-word read on the 4360 (from `re-out\up-trace.txt`,
seq 91..334; the window moves are those of `si_setcore`/`si_setcoreidx`,
backplane specification):

    BAR0 window -> ChipCommon; R CC(0x28) -> saved; window back
    234 x R16 BAR0+0x3800+2i (= CC(0x800 + 2i)), i = 0..233
    BAR0 window -> ChipCommon; W CC(0x28) = saved; window back

Because the CRC of a 234-word image normally does not match over 220 or 64
words, a card whose rev-11 SROM is valid needs one read; a card whose SROM is
blank or corrupt is read three times (234 + 220 + 64 words), each with its own
chip-control read/restore.

### si_is_sprom_available (.text+0x01f509, name original, siutils.c)

`bool si_is_sprom_available(si_t *sih)`. For ChipCommon rev >= 31 (the 4360:
43): if bit 30 of the capabilities (`sih+0x18`, "SROM present" capability) is
clear: false. Otherwise save the current core index, `si_setcoreidx(sih, 0)`
(ChipCommon), read `CC(0x190)` (SROM control), restore the core index; result
= bit 0 of that register (1 = an SROM is attached). (For ChipCommon rev < 31 the
answer comes from chip-specific bits of the chip status `sih+0x48`; other
chips, not described.)

Trace (seq 86..90): window -> ChipCommon, `R CC(0x190)` (= 1 in the model:
SROM present), window back.

### srom_read (.text+0x00938a, name original)

`int srom_read(si_t *sih, uint bustype, void *curmap, osl_t *osh, uint byteoff,
uint nbytes, u16 *buf, bool check_crc)`. Result 0 = success, 1 = failure (the
function returns a truth value "failed").

Callers: `srom_var_init` (PCMCIA only) and `wlc_bmac_iovars_dispatch` (the
`srom` get iovar, id 0x44: `srom_read(sih, sih->bustype, regs, osh, arg[0],
arg[1], &arg[2], 0)`, only when `si_is_sprom_available`; the iovar enables
the SROM interface with `si_sprom_enable(sih, 1)` around the call if it was
disabled, and returns 0 or -1).

1. If `byteoff` or `nbytes` is odd, or `byteoff + nbytes > 0x300` (32-bit
   sum): return 1.
2. `wordoff = byteoff / 2`, `nwords = nbytes / 2`.
3. Bus type 1 (PCI):
   * `curmap` NULL: return 1.
   * `si_is_sprom_available(sih)` false: `r = otp_read_pci(osh, sih, buf,
     nbytes)` - note that the OTP image is always taken from its start,
     `byteoff` is ignored - and return `r != 0`.
   * else window = `curmap + 0x1000` for ChipCommon rev <= 31, otherwise
     `curmap + 0x3800`, but return 1 if capability bit 30 is clear. Return
     `sprom_read_pci(osh, sih, window, wordoff, buf, nwords, check_crc) != 0`.
4. Bus type 2 (PCMCIA): word by word through the attribute-memory registers
   0x39f/0x39e (address), 0x39b (command 2, poll bit 3, up to 1000 times),
   0x39d/0x39c (data); not used on PCIe, not described further.
5. Any other bus type: return 1.


### otp_read_pci (sub_009284, .text+0x009284, name assigned)

`int otp_read_pci(osl_t *osh, si_t *sih, u16 *buf, uint nbytes)`: the SROM
image of a card without SROM, taken from the OTP's hardware region. Result 0
= image with a good CRC; 1 = blank or bad CRC; -1 = no memory, or the result
of `otp_read_region` when that fails (-1, -14, -25, -30, ...).

1. `tmp = osl_malloc(osh, 0x300)`; NULL: return -1. Clear it (0x300 bytes).
2. `wlen = 0x180` (words); `r = otp_read_region(sih, 1 /* hardware region */,
   tmp, &wlen)`. If `r != 0`: free `tmp`, return `r`.
3. Copy `nbytes` bytes from `tmp` to `buf` (always, before any check; words
   beyond the region stay 0).
4. If `tmp[0] == 0xffff`: free, return 1.
5. If `hndcrc8(tmp, 0x1b8, 0xff)` (220 words), or else over 0x1cc bytes (230
   words), or else over 0x1d4 bytes (234 words) gives 0x9f: result 0; if none
   does: result 1. Free `tmp` (0x300), return the result.

So the OTP image may be an SROM image of 220 words (revisions 8..10), 230
words, or 234 words (revision 11), as long as the CRC byte is the high byte
of its last word. `srom_var_init` then determines the revision as described
there (signature 0x0634 at word 64 -> low byte of word 233; otherwise low byte
of word 219, or of word 229 if word 219 is 0x5372).

### OTP access (bcmotp.c)

Two implementations exist; `otp_init` picks one by ChipCommon revision:
`ipxotp` for revision 21 and revisions above 22 - **the BCM4360 (rev 43)
uses ipxotp** - and `hndotp` for revision 22 and revisions up to 20 (not
described). Each is a table of function pointers (ipxotp at `.data+0x60`,
hndotp at `.data+0xd0`; 14 slots of 8 bytes):

| Slot (offset) | ipxotp | Meaning |
|---|---|---|
| 0 (0x00) | `sub_00324d` | size in bytes (`2 * wsize`) |
| 1 (0x08) | `sub_0038d3` | read one bit |
| 2 (0x10) | NULL | |
| 3 (0x18) | `sub_003244` | status (`otpinfo.status`) |
| 4 (0x20) | `sub_003d6a` | init |
| 5 (0x28) | `sub_003a53` | read a region |
| 6 (0x30) | `sub_003259` | NVRAM read: returns -23 (`BCME_UNSUPPORTED`) |
| 7..11 | NULL | (write, lock, ... not built) |
| 12 (0x60) | `sub_0039fe` | read one word |

The generic entry points `otp_size`, `otp_status`, `otp_nvread` (no callers
in the object), `otp_read_bit` (called by `si_eci_init` only),
`otp_read_word` (PHY attach, `si_otp_fabid`) and `otp_read_region`
(`otp_read_pci` only) dispatch through this table.

State: one static `otpinfo` structure at `.bss+0x00` (0x70 bytes), cleared and
filled again by **every** `otp_init` call - and `otp_read_word` /
`otp_read_region` call `otp_init` every time, so the OTP geometry and regions
are re-read from the hardware for every word or region read.

| Offset | Size | Field (names assigned, Broadcom style) |
|---|---|---|
| 0x00 | 4 | `ccrev`: ChipCommon revision |
| 0x08 | 8 | `fn`: the function table |
| 0x10 | 8 | `sih` |
| 0x18 | 8 | `osh` (`si_osh(sih)`) |
| 0x20 | 2 | `wsize`: OTP size in 16-bit words |
| 0x22 | 2 | `rows` |
| 0x24 | 2 | `cols` (bits per row) |
| 0x28 | 4 | `status`: `CC(0x10)` with the region-programmed bits 8..11 or-ed in |
| 0x2c | 2 | `hwbase`: first word of the hardware region (the SROM image) |
| 0x2e | 2 | `hwlim`: end (exclusive) of the hardware region |
| 0x30 | 2 | `swbase`: first word of the software region |
| 0x32 | 2 | `swlim`: end of the software region |
| 0x34 | 2 | `fbase`: first word of the fuse region |
| 0x36 | 2 | `flim`: end of the fuse region (= `wsize`) |
| 0x38 | 4 | `otpgu_base`: first word of the general-use area (signed int) |
| 0x3c | 2 | `fusebits`: size of the fuse area as used in the `swlim` formula (99 for the 4360) |
| 0x3e | 1 | `buotp`: "unified OTP" - regions end at `flim` (chips 0x4335, 0xa887, 0x4319 only) |
| 0x40 | 4 | cleared by init, purpose unknown (USB manufacturer-id offset in Broadcom's naming, unverified) |

Status bits (in `status`): bit 8 hardware region programmed, bit 9 software
region programmed, bit 10 chip-id region programmed, bit 11 fuse region
programmed; bit 12 (from `CC(0x10)`) is "OTP ready" in the card model
(purpose not used by this code).

#### otp_init (.text+0x00329f, name original)

`void *otp_init(si_t *sih)`: result the `otpinfo` pointer, or NULL.

1. Clear the 0x70 bytes at `.bss+0x00`. `ccrev = sih->ccrev` (`sih+0x14`).
2. `fn` = ipxotp table if `ccrev == 21` or `ccrev > 22`; hndotp table if
   `ccrev == 22` or `ccrev <= 20`.
3. `sih`, `osh = si_osh(sih)` stored.
4. `was_on = si_is_otp_powered(sih)`; if not: `si_otp_power(sih, 1)`.
5. `r = fn->init(sih)` (ipxotp: `ipxotp_init`).
6. If not `was_on`: `si_otp_power(sih, 0)`. Return `r`.

`si_is_otp_powered(sih)`: true if the chip has no PMU (capability bit 28,
tested as bit 4 of byte `sih+0x1b`), else `si_pmu_is_otp_powered(sih, osh)`
(pmu.md: reads `CC(0x60c)` resource state bit 8 for the 4360).
`si_otp_power(sih, on)`: `si_pmu_otp_power(sih, osh, on)` if the chip has a
PMU, then always `osl_delay(1000)`. `si_is_otp_disabled(sih)`: false for the
4360 (chip-status bits only for other chips).

#### otp_read_region (.text+0x0033ff, name original) / otp_read_word (.text+0x003362, name original)

`int otp_read_region(si_t *sih, int region, u16 *data, uint *wlen)`,
`int otp_read_word(si_t *sih, uint wn, u16 *data)`:

1. `was_on = si_is_otp_powered(sih)`; if not: `si_otp_power(sih, 1)`.
2. If the OTP is (still) not powered, or `si_is_otp_disabled(sih)`: result
   -25 (`BCME_NOTREADY`).
3. Else `oi = otp_init(sih)`; NULL: result -1; else result
   `oi->fn->read_region(oi, region, data, wlen)` (slot 5) /
   `oi->fn->read_word(oi, wn, data)` (slot 12; -23 if the slot is NULL).
4. If not `was_on`: `si_otp_power(sih, 0)`. Return the result.

#### ipxotp_init (sub_003d6a, .text+0x003d6a, name assigned)

`otpinfo_t *ipxotp_init(si_t *sih)` (uses the `otpinfo` at `.bss+0x00`, just
cleared by `otp_init`). Result: `&otpinfo`, or NULL.

1. NULL if ChipCommon rev < 23 and != 21, or `si_is_otp_disabled(sih)`, or
   not `si_is_otp_powered(sih)`.
2. Save the current core index; `cc = si_setcoreidx(sih, 0)` (ChipCommon
   registers). Every return below restores the core index.
3. Size code `z = (sih->cccaps >> 19) & 7` (capabilities `sih+0x18`, bits
   19..21). `z == 0` (no OTP): return NULL.
4. Geometry from `L = (CC(0x1c) >> 16) & 7` (OTP layout register):
   * `L == 1`: `rows` = 0x40, 0x60, 0x80, 0xa0, 0xc0, 0x100 for `z` = 1, 2, 3,
     4, 5, 7 and `cols = 0x20`; for `z == 6` neither is set (both stay 0).
     Then `wsize = rows * cols / 16`.
     (The 4360 in the model: caps size code 5 -> 192 rows of 32 bits, 384
     words = 768 bytes.)
   * `L == 0` (older OTP): `z` = 1: rows 0x20, cols 0x40, wsize 0x80; 2:
     0x40, 0x40, 0x100; 5: 0x60, 0x40, 0x180; 7: 0x10, 0x40, 0x40; other
     codes: nothing set.
   * other `L`: nothing set.
5. `otpgu_base`: for ChipCommon rev >= 40: `((CC(0x1c) & 0xfff) >> 4) - 4`
   (the model: layout 0x00010140 -> 16). (rev 21, 24, 27: 12; rev 36, 39: 12
   if `wsize < 0x80` else 20; rev 23 and 25..35, 37, 38: 20.)
6. If `(CC(0x1c) & 0x70000) != 0x10000` (`L != 1`): `CC(0x18) = 0x84000000`,
   then read `CC(0x18)` until bit 31 is clear, at most 10,000,000 reads; on
   time-out return `&otpinfo` at once (regions left 0).
7. `status = CC(0x10)`. For the chips 0x4360, 0x4352, 0xaa06 (43526),
   0xa9c4, 0x4350, 0x4335, 0x4331, 0xa9a7, 0x4324, 0xa887 and a list of other
   ids: `w = otpr(otpgu_base + 3)`; `status |= (w >> 12) << 8` (the top four
   bits of general-use word 3 are the region-programmed flags).
8. `buotp = 0`; the 32-bit field at 0x40 = 0. (`buotp = 1` only if `(status &
   0x300) == 0x300` and the chip is 0x4335, 0xa887 or 0x4319.)
9. `hwbase = otpgu_base + 4`; `hwlim = wsize`; `flim = wsize`.
10. Chip-specific fixed limits (not the 4360, whose pair is (0, 0)): 0x4335
    (0x15b, 0x11b), 0x4324/0xa8ea/0xa8eb (0xa5, 0x95), 0x4314/0xa886 (0x136,
    0x106), 0x4330 (0x76, 0x6e), 0x4334 (0xad, 0x9d), 0x4350 (0x152, 0x112):
    if the two numbers differ, `hwlim = swlim =` second, `fbase =` first.
11. If `status` bit 8: `hwlim = otpr(otpgu_base) >> 4` and `swbase = hwlim`;
    else `swbase = hwbase`.
12. If the pair of step 10 differed: return `&otpinfo` (other chips).
13. Save the core index, `si_setcoreidx(sih, 0)`; if ChipCommon rev > 34:
    `fusebits = ((CC(0x4c) & 0x3ff) + 7) >> 3`; restore the core index.
14. Chip-specific `fusebits`: **0x4360 and 0xa9c4: 99**, replacing the
    `CC(0x4c)` value (the register is still read). Other chips have other
    constants. 0x4352 and 0xaa06 (43526) are not in the list and keep the
    `CC(0x4c)` value.
15. `swlim = (2 * wsize - fusebits - 2) / 2` (C integer division; chip 0x4315:
    0xb2 / 2). For the 4360 with 384 words: 333.
16. If `status` bit 9: `swlim = otpr(otpgu_base + 1) >> 4` and `fbase =
    swlim`; else `fbase = swbase`.
17. Return `&otpinfo`.

Layout of the general-use area (words relative to `otpgu_base`): word 0 bits
4..15 = end of the hardware region (exclusive, absolute word number); word 1
bits 4..15 = end of the software region; word 3 bits 12..15 = region flags
(hw, sw, chip id, fuse); words 2..3 = the chip-id region (2 words, read by
region 4); word 4 = first word of the hardware region = the SROM image.

#### ipxotp_otpr (sub_003983, .text+0x003983, name assigned)

`u16 otpr(otpinfo_t *oi, chipcregs_t *cc, uint wn)`: one OTP word.

* If `si_is_sprom_available(oi->sih)` (an SROM is attached, so the
  ChipCommon window at 0x800 shows the SROM): bit-serial - for `b` = 0..15:
  `bit = ipxotp_read_bit(oi, cc, 16 wn + b)`; if `bit == 0xffff` (error)
  return 0xffff at once; else `word |= bit << b`. Return `word`.
* Otherwise: `osl_readw(cc + 0x800 + 2 wn)` - a 16-bit read of the
  ChipCommon SROM/OTP window, which on a card without SROM shows the OTP.
  (Through BAR0 this is BAR0 + 0x3800 + 2 wn when ChipCommon is the core in
  the window; `cc` is the address `si_setcoreidx(sih, 0)` returned.)

Note: `si_is_sprom_available` is called for every word (one `CC(0x190)` read
with its window moves each time).

#### ipxotp_read_bit (sub_0038d3, .text+0x0038d3, name assigned)

`int read_bit(otpinfo_t *oi, chipcregs_t *cc, uint off)`:

1. `CC(0x14) = 0` (OTP control); `CC(0xf4) = 0` (purpose unknown); read
   `CC(0x1c)` (value unused).
2. `row = off / cols`, `col = off % cols`. `CC(0x18) = 0x80000000 | ((row <<
   8) & 0xffff) | (col & 0xff)` (bit 31 start, command 0 = read, row in bits
   8..15, column in bits 0..7).
3. Read `CC(0x18)` until bit 31 is clear, at most 10,000,000 reads; time-out:
   return -1 (0xffff in the caller's 16 bits).
4. If bit 28 is set (error): return -1. Else return bit 29 (the data bit).

Trace (`re-out\up-trace.txt` seq 597..602, word 19 = general-use word 3 of the
model's OTP, read while the SROM is present): W CC(0x14)=0, W CC(0xf4)=0, R
CC(0x1c), W CC(0x18)=0x80000910 (row 9, column 16 = bit 0x130), R CC(0x18).

#### ipxotp_read_region (sub_003a53, .text+0x003a53, name assigned)

`int read_region(otpinfo_t *oi, int region, u16 *data, uint *wlen)`:

| region | start | size | needs `status` bit |
|---|---|---|---|
| 1 hardware | `hwbase` | (`buotp` ? `flim` : `hwlim`) - `hwbase` | 8 |
| 2 software | `swbase` | (`buotp` ? `flim` : `swlim`) - `swbase` | 9 |
| 4 chip id | `otpgu_base + 2` | 2 | 10 |
| 8 fuse | `fbase` | `flim - fbase` | 11 |
| 15 all | `hwbase` | `flim - hwbase` | 8 or 9 |
| other | | | result -2 (`BCME_BADARG`) |

(The size is a 32-bit unsigned difference: an end below the start gives a
huge size.) If the status bit is missing: `*wlen = size`, result -30
(`BCME_NOTFOUND`). If `*wlen < size` (for region 4: `*wlen <= 1`): `*wlen =
size`, result -14 (`BCME_BUFTOOSHORT`). Otherwise: save the core index, `cc =
si_setcoreidx(sih, 0)`, `data[i] = otpr(oi, cc, start + i)` for `i` = 0 ..
size-1, restore the core index, `*wlen = size`, result 0.

#### ipxotp_read_word (sub_0039fe), ipxotp_size (sub_00324d), ipxotp_status (sub_003244), ipxotp_nvread (sub_003259)

`read_word(oi, wn, *data)`: save the core index, `cc = si_setcoreidx(sih,
0)`, `*data = otpr(oi, cc, wn)`, restore, result 0. `size(oi)`: `2 *
oi->wsize`. `status(oi)`: `oi->status`. `nvread`: -23.

#### Building an OTP that `srom_var_init` accepts (4360, ChipCommon rev 43)

For a card without SROM (`CC(0x190)` bit 0 clear), the OTP must hold, with
`gu = ((CC(0x1c) & 0xfff) >> 4) - 4` (16 in the model):

* word `gu + 3`: bit 12 set (hardware region programmed) - or `CC(0x10)`
  bit 8 set;
* word `gu`: `(gu + 4 + n) << 4` with `n` = number of image words (234 for
  revision 11; at most 384 so the region fits `otp_read_pci`'s buffer);
* words `gu + 4 ..`: the SROM image (word 0 not 0xffff, CRC as in the SROM).

The emulator attaches from such an OTP (no SROM) and produces the same
3652-byte variable buffer as from the SROM (see Verification).

### srom_parsecis (.text+0x0046a3, name original) - not reached on PCIe

Called only from the PCMCIA branch (bus type 2) of `srom_var_init`. On PCI
the OTP is always taken as an SROM image (hardware region), never parsed as
CIS tuples, so a CIS in the OTP's software region has no effect on the
variables of this driver. Not described further.

### initvars_flash_pci (sub_009566, .text+0x009566, name assigned)

The fall-back when the card has neither a valid SROM nor a valid OTP image but
the global NVRAM (`nvram.txt`, `nvram_init` below) holds a `sromrev` for this
device. `int initvars_flash_pci(si_t *sih, osl_t *osh, char **cur)`: appends
this card's NVRAM variables, without their device prefix, at `*cur` (the start
of `srom_var_init`'s 0x1000-byte buffer) and advances `*cur` past the final
extra NUL. Result 0, -27 (no memory), -14 (`BCME_BUFTOOSHORT`) or the result
of `nvram_getall`.

1. `coded[16]` cleared. `flash = osl_malloc(osh, 0x10000)`; NULL: return -27.
2. `r = nvram_getall(flash, 0x10000)`; `r != 0`: free `flash`, return `r`.
3. `si_devpath(sih, devpath, 16)` -> `devpath` = `"pci/<bus>/<slot>/"`;
   `dlen = strlen(devpath)`.
4. `si_coded_devpathvar(sih, coded, 16, "devid")` -> `"<i>:devid"` if some
   NVRAM variable `devpath<i>` names this device (below), else `coded`
   stays empty. If `strlen(coded) > 5` (`strlen("devid")`): `plen =
   strlen(coded) - 5` and `coded[plen] = 0` (leaving `"<i>:"`); else `plen =
   0`.
5. `room = 0x1000`. For each string `s` of `flash` up to the first empty
   string: if the first `dlen` bytes of `s` equal `devpath`, `skip = dlen`;
   else if `plen != 0` and the first `plen` bytes equal `coded`, `skip =
   plen`; else ignore `s`. For a match: `n = strlen(s) + 1 - skip`; if `room <
   n`: result -14 (stop); copy the `n` bytes from `s + skip` (the rest of the
   string with its NUL, `strncpy`) to the write pointer (starting at `*cur`),
   `room -= n`, write pointer `+= n`.
6. After the last string: if `room == 0`: -14. Else write one NUL, `*cur` =
   one past it, result 0.
7. Free `flash` (0x10000).

The strings keep their NVRAM order and text (no de-duplication, no
reformatting: `sromrev=0xb` stays as written). `srom_var_init` then copies
the used part with `initvars_table`.

### Device-path helpers (siutils.c)

* `si_devpath(sih, path, size)` (.text+0x01efe9, original): if `size <= 0` or
  `path` NULL: -1. Bus type (`sih+4`) 1 (PCI): `snprintf(path, size,
  "pci/%u/%u/", osl_pci_bus(osh), osl_pci_slot(osh))` (`osh` = `sih+0x58`;
  `osl_pci_slot` is called before `osl_pci_bus`); 0 or 4: `"sb/%u/"` of the
  current core index; 2: `"pc/1/1/"`; other bus types: -1. If the
  `snprintf` result is negative or `>= size`: `path[0] = 0`, -1; else 0.
  (`osl_pci_bus` = the PCI bus number, `osl_pci_slot` = `PCI_SLOT(devfn)`,
  `src\shared\linux_osl.c`.)
* `si_coded_devpathvar(sih, out, size, name)` (.text+0x01f0a7, original):
  `si_devpath(sih, dp, 16)`; failure: NULL. Drop a trailing `/` of `dp`. For
  `i` = 0..31: `v = getvar(NULL, "devpath<i>")` (that is `nvram_get`); if `v`
  without a trailing `/` has the same length as `dp` and the same bytes:
  `snprintf(out, size, "%d:%s", i, name)`, return `out`. None: NULL.
* `si_devpathvar` (`sub_0217e2`, .text+0x0217e2, name assigned):
  `(sih, var, name)`: if `var` is NULL return it; `si_devpath(sih, var, 0x30)`
  (failure: return `var`); `plen = strlen(var)`; if `strlen(name) + 1 > 0x30 -
  plen`: `var[0] = 0`; else `strncpy(var + plen, name, 0x30 - plen - 1)`.
  Return `var` (`"pci/3/0/sromrev"`).
* `si_getdevpathvar(sih, name)` (.text+0x02184b, original): `buf[0x30]`;
  `si_devpathvar(sih, buf, name)`; `v = getvar(NULL, buf)` (NVRAM only); if
  NULL and `si_coded_devpathvar(sih, buf, 0x30, name)` succeeds: `v =
  getvar(NULL, buf)` (`"<i>:name"`). Return `v`.
* `si_getnvramflvar(sih, name)` (.text+0x01efdc, original): `getvar(NULL,
  name)`, i.e. `nvram_get(name)`.

### nvram_ro.c: the global NVRAM

A list of blocks, head at `.bss+0x300` (8 bytes), each block a node of 0x18
bytes from `osl_malloc`:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 8 | next node |
| 0x08 | 4 | 0x18 (header size) |
| 0x0c | 4 | length of the data in bytes |
| 0x10 | 8 | data: NUL-separated `name=value` strings |

* `nvram_init(sih)` (.text+0x019a5f, original; called by `si_doattach` just
  before `srom_var_init`, result ignored there): if the list is not empty, or
  the bus type is not 1: return 0. `buf = osl_malloc(0x1000)` (NULL: -1).
  `img = osl_os_open_image("nvram.txt")` (`filp_open` of that relative path
  in the Linux glue; normally absent -> result -1, NVRAM stays empty). If it
  opened: call `osl_os_get_image_block(buf, 0x1000, img)` until it returns 0,
  keeping the length `n` of the last non-zero block - **each block is read to
  the start of `buf`, so only the last block survives** (a file longer than
  4096 bytes loses its beginning). Then every tab, space, LF or NUL among the
  `n` bytes becomes NUL, `buf[n] = 0` (one byte past the buffer when `n` is
  0x1000), `n += 1`, close the image; `copy = osl_malloc(n)`, copy `n`
  bytes, `nvram_append(sih, copy, n)`. Free `buf` (0x1000). Result -1 if
  `nvram_append` failed, else 0.
* `nvram_append(sih, data, len)` (.text+0x019a0d, original): node =
  `osl_malloc(0x18)` (NULL: -27); fill as in the table; insert at the head.
* `nvram_get(name)` (.text+0x019916, original): for each node from the head,
  walk its strings while inside the data and the string is not empty: if the
  first `strlen(name)` bytes match and the next byte is `=`: return the value.
  None: NULL.
* `nvram_getall(buf, count)` (.text+0x019869, original): for each node from
  the head, copy its strings (each with its NUL) up to the first empty one;
  if the running total of a node exceeds the space left: -14. At the end, if
  no space is left (`count <= 0`): -14; else one more NUL, 0.
* `nvram_exit` (`si_detach`), `nvram_get_global_vars`, `nvram_set`/`unset`/
  `reset`/`commit` (stubs): not used by the attach path.

Consequence of the whitespace rule: an empty line or two blanks in a row in
`nvram.txt` produce an empty string, and both `nvram_get` and `nvram_getall`
stop there - everything after it is invisible.

## Notes for an implementation of `srom_var_init` (hybrid take-over)

* Contract: same prototype and results; `*vars` must be a fresh
  `osl_malloc` of exactly `*count` bytes holding the strings in the order of
  the walk; the image buffer (0x300) and work buffer (0x1000) are allocated
  and freed in the order given in `srom_var_init`, step 10 (only matters if
  allocations are compared; `osl_malloced` would show a leak otherwise).
* Bus accesses: keep calling `si_is_sprom_available`,
  `si_chipcontrl_read` / `_restore` (and `si_chipcontrl_srom4360` for chip rev
  < 3), `otp_read_region` (or reimplement them with the same accesses: window
  moves by `si_setcore`/`si_setcoreidx`, `CC(0x190)`, `CC(0x28)`, the 16-bit
  window reads in ascending order). The three SROM read attempts must stay
  (a blank SROM is read 234 + 220 + 64 words).
* The conversion can be a fixed list for revision 11 (the table above): no
  name repeats, so the result equals the table walk; but revisions 1..5 and
  8..10 are accepted by the object too, so an implementation that wants to be
  identical for any image needs the object's two tables (573 + 72 entries,
  which are facts and can be extracted with
  `re-out\analysis\srom-otp\tables.py dump REV`) and the walk as specified.
* The `.bss` statics are private; reproduce `.bss+0x78/0x80/0x90/0x190/0x1a0`
  only if memory is compared. For revision 11 after a successful call:
  `.bss+0x78` = the name `"rxgainerr5ga2"`, `.bss+0x80` = length of its text
  with the trailing comma, `.bss+0x90` = e.g. `"31,31,31,31"` (NUL-padded to
  256 bytes), `.bss+0x190` = 0, `.bss+0x1a0` = the last `pa5ga2` text, e.g.
  `"0xffff,...,0xffff"` (12 elements).
* For revision 11 the work buffer cannot overflow: the longest buffer seen in
  all comparisons and in pattern probes of the model was 3,877 bytes (of
  4,094 usable). (Upper bound not proven formally.)

## Verification

All checks run the unmodified object in the emulator (`tools\re`), calling
`srom_var_init` directly (after one `wlc_attach`, with the `sih` of the
attached card) or observing it during `wlc_attach`, and compare with an
independent Python model written from this text
(`re-out\analysis\srom-otp\model.py`; it reads the layout tables itself with
`tables.py`, not with `tools\re\srom.py`). Compared: the result code, the
variable buffer byte for byte (length = `*count`), the sizes and order of all
`osl_malloc`/`osl_mfree` calls inside `srom_var_init`, and the `.bss`
statics.

| Script (re-out\analysis\srom-otp\) | Cases | Differences | What |
|---|---|---|---|
| `compare.py --random 3 --seed 1` | 280 | 0 | SROM path: the synthetic card; all fields 0xffff; all fields 0; random images with valid CRC for every accepted revision (1, 2, 3 incl. "0x10" = rev 1, 4, 5, 8, 9, 10, 11); images biased to 0x00/0xff bytes, sign bits, country codes with zero bytes; 232 single-word edge variants of the synthetic image; bad CRC (three reads, then NVRAM fall-back, -1); valid CRC but wrong signature (-2); revisions 0, 6, 7, 12 (-2); word 0 = 0xffff; blank SROM; 64-word image |
| `compare.py --random 10 --seed 5` | 379 | 0 | the same with other random contents |
| `otp_compare.py --random 4` | 28 | 0 | OTP path (no SROM): image in the hardware region; region flag in OTP word or in `CC(0x10)`; software region present; blank OTP; bad CRC; region lengths 220, 230, 384, 385, negative; 220-word rev 8/9/10 images; signature 0x5372 at word 219; general-use base from layout values 0x100/0x140/0x200; OTP size codes 0..7. Also compared: the `otpinfo` fields (`.bss+0x20..0x3e`) |
| `srom_read_check.py` | 45 | 0 | `srom_read` called directly: random offsets/lengths (odd, beyond 0x300 -> 1, buffer untouched), the words read, CRC check over the whole image (0) and over a partial range (1); without SROM: the OTP image from its start whatever `byteoff` is |
| `flash_compare.py` | 11 | 0 | NVRAM fall-back with an emulated `nvram.txt`: `pci/3/0/` prefix, `devpath<i>` coded prefix (with and without trailing `/`), both, empty line, tabs/spaces, unsupported revision, hex revision, global `sromrev` only (result 0, no variables), no `sromrev` (-1), two file blocks (only the last kept) |

Further checks:

* `harness.py`: the direct call with the synthetic SROM returns the same
  3652 bytes that `wlc_attach` stored in `pub->vars`; allocations 0x300,
  0x1000, 3652, free 0x1000, free 0x300.
* Attaching **without SROM** from an OTP that holds the synthetic image as
  described in "Building an OTP" gives the identical 3652-byte buffer
  (`otp_compare.py`, first line).
* Bus accesses of the SROM read were checked against `re-out\up-trace.txt`
  (seq 86..334) and of the OTP bit read against seq 592..692.
* The model was shown to detect differences: `srompy_diff.py` runs
  `tools\re\srom.py`'s decoder on the same images and reports the
  differences listed below.

### Differences between `tools\re\srom.py` and the object

**Fixed 2026-11-02** (coordinator): `srom.py decode` now follows the walk of
this specification (revision detection by CRC and signature, NOFFS per field
width, `leddc`, `ccode` with a zero byte, signed formatting only for common
scalars and array elements, arrays only from revision 10, the duplicate rule
and size limit of `varbuf_append`) and gives the object's buffer byte for byte
on 37 revision-11 images (the cards of `hybrid.py srom` and 30 random ones);
`srom.py table REV` lists the continuation entries of a variable. The list
below is kept as the record of what was wrong.

Found by reading, confirmed by `srompy_diff.py` (238 revision-11 images):

1. **`leddc`**: the object prints `(w & 0xff) << 8 | (w >> 8) << 24` as a
   signed decimal (word 0x2a70 -> 0x2a007000 -> `leddc=704671744`; word
   0x1234 -> `leddc=302003200`, checked in the emulator); `srom.py` prints the
   word itself.
2. **NOFFS**: the object leaves a variable out when its *field* is all ones
   (`ledbh0`..`ledbh3`, `txchain`, `rxchain`, `antswitch`, `leddc`), e.g.
   `ledbh0` is missing whenever the low byte of word 77 is 0xff; `srom.py`
   only when the whole word(s) are 0xffff.
3. **`ccode` with a zero byte**: the object's `%c` of a zero character puts
   a NUL into the buffer (`ccode=X` followed by an empty string - which ends
   the variable list for `getvar`, see Open questions); `srom.py` produces
   `X\x00` as the value.
4. Per-path scalar values are never printed signed by the object (`srom.py`
   would sign them if a per-path entry had PRSIGN; none has, so no effect
   today). An entry with both PRHEX and PRSIGN would be printed unsigned hex by
   the object (none exists).
5. `srom.py decode` takes the revision from the last word of the file; the
   object uses the signature/size rules of `srom_var_init` step 5.
6. `srom.py table 11` hides the continuation entries whose revmask is 0
   (the high words of `boardflags`, `boardflags2`, `boardflags3`); its
   decoder handles them correctly.
7. `srom.py` handles arrays for every revision; the object only for revision
   >= 10 (no array entries exist for revisions below 10, so no effect).

`srom.py encode` produces images the object accepts (synthetic card: the
buffer decodes back to the input values except for the points above).

## Open questions

* **Which form the Apple card uses** (SROM or OTP) is still unknown; both
  paths are specified and verified in the emulator. The model's OTP
  geometry (`CC(0x1c)` = 0x00010140, size code 5 in the capabilities,
  `CC(0x10)` = 0x1000) is an assumption; the real values decide `otpgu_base`,
  `hwbase` and the region flags.
* `ccode` with exactly one zero byte (e.g. word 0x5500): the buffer gets
  `"ccode=U"` followed by an empty string (the kernel's `vsnprintf` writes
  the zero character); `getvar` stops at the empty string, so every variable
  after `ccode` (rows 10 onward of the revision-11 table and all per-path
  variables) becomes invisible.
  Word 0x0055 gives `"ccode="` followed by a string `"U"` without `=`. This
  is the object's behaviour (reproduced in the emulator); a real SROM with
  such a word is unlikely.
* The purpose of `CC(0xf4)` (written 0 before every OTP bit read) and of the
  32-bit `otpinfo` field at 0x40 is unknown.
* `srom_parsecis` (19 kB) is not described: it is only reachable for
  PCMCIA. If a PCIe card stored its data as CIS tuples in the OTP's
  software region, this driver would not see them.
* Revisions 1..5 and 8..10 are covered only by the generic walk and the
  random-image comparisons, not by a listed table.
