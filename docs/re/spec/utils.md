# Utilities: board variables, number parsing, bit count, CRC-8

The small generic helpers of `bcmutils.c` that the driver runs most. In a
whole session (attach, up, nine channel changes, watchdog, down) the lookup of
board variables alone (`getvar`) is about a fifth of everything the driver
executes: the PHY's attach reads some 170 variables and every lookup scans the
whole variable list.

Written by the coordinating session from the disassembly (the functions are
short); checked by running the object in the emulator (`tools/re/hybrid.py`,
see "Verification").

## Scope

| Function | .text offset | Size | Name |
|---|---|---|---|
| `getvar` | 0x00b807 | 112 | original |
| `getintvar` | 0x00ba1a | 30 | original |
| `getintvararray` | 0x00b9c3 | 87 | original |
| `getintvararraysize` | 0x00b978 | 75 | original |
| `bcm_strtoul` | 0x00a5f4 | 249 | original |
| `bcm_bitcount` | 0x00ab85 | 38 | original |
| `hndcrc8` | 0x00a9a6 | 30 | original |
| `phy_getvar` | 0x0bcd81 | 108 | original (`wlc_phy_cmn.c`) |

## Overview

The board variables ("vars") are one block of NUL-terminated strings
`name=value`, ended by an empty string (two NULs in a row), made from the SROM
or OTP at attach (`srom_var_init`, srom-otp specification). Values are numbers
in C notation, or lists of numbers separated by commas (`pa2ga0=0xff11,0x1a22,0xfc33`).

`getvar` finds the value of a name; the others build on it and on the number
parser `bcm_strtoul`. When a name is not in the block, `getvar` asks the
driver's global NVRAM (`nvram_get`, `nvram_ro.c`, not part of this
specification).

## Data

### Character classes of `bcm_strtoul`

The parser classifies bytes with a table of 256 class bytes. Only three
properties matter here:

| Property | Bytes |
|---|---|
| white space | 0x09..0x0d, 0x20, 0xa0 |
| decimal digit | 0x30..0x39 (`'0'`..`'9'`) |
| hexadecimal letter, lower case | 0x61..0x66 (`'a'`..`'f'`) |
| hexadecimal letter, upper case | 0x41..0x46 (`'A'`..`'F'`) |

Every other byte (including the letters `g`..`z`, `G`..`Z` and the bytes
0x80..0xff) is none of these.

### CRC-8 of `hndcrc8`

Table driven, one table lookup per byte; the table is the one of the
reflected CRC-8 with polynomial 0xab (the polynomial 0xd5 in most-significant-
bit-first notation): entry `i` is obtained from `c = i` by eight steps of
`c = (c >> 1) ^ 0xab` if `c` is odd, else `c = c >> 1`. Entries 0..7: 0x00,
0xf7, 0xb9, 0x4e, 0x25, 0xd2, 0x9c, 0x6b.

## Procedures

### getvar (.text+0x00b807, name original)

`char *getvar(char *vars, const char *name)`. Result: pointer to the value
(the byte after the `=`) inside `vars`, or what `nvram_get` returns.

1. If `name` is NULL, or its length (`osl_strlen`) is 0: result NULL (no NVRAM
   lookup).
2. Let `n` be the length of `name`. Walk the entries of `vars` from the
   first; stop at the empty string (a NUL where an entry would start). `vars`
   may be NULL: then there are no entries.
3. For each entry `e`: if the first `n` bytes of `e` equal `name`
   (`osl_memcmp(e, name, n) == 0`) and byte `n` of `e` is `=`: result
   `e + n + 1`. Otherwise go on with the byte after the NUL that ends `e`.
4. No entry matched: result `nvram_get(name)`.

So the first matching entry wins, a name only matches a whole name (`pa2ga0`
does not match `pa2ga01=...`), and an entry without `=` never matches.

### getintvar (.text+0x00ba1a, name original)

`int getintvar(char *vars, const char *name)`: `v = getvar(vars, name)`; NULL:
result 0; else result `bcm_strtoul(v, NULL, 0)` (truncated to 32 bit).

### getintvararray (.text+0x00b9c3, name original)

`int getintvararray(char *vars, const char *name, int index)`: element `index`
(counting from 0) of a comma-separated list.

1. `p = getvar(vars, name)`; NULL: result 0.
2. `i = 0`. While the byte at `p` is not NUL:
   a. `v = bcm_strtoul(p, &end, 0)`.
   b. If `i == index`: result `v` (truncated to 32 bit).
   c. `p = end`; if the byte at `p` is `,`, `p` advances by one; `i` += 1.
3. Result 0 (fewer elements than `index + 1`).

Note: if the parser stops without consuming anything (a byte that is neither
part of a number nor a comma), `p` does not move and the loop repeats on the
same position with `i` growing; it ends when `i` reaches `index`, with the
value 0. Keep this behaviour exactly (it decides the result for malformed
values).

### getintvararraysize (.text+0x00b978, name original)

`int getintvararraysize(char *vars, const char *name)`: the number of elements.

1. `p = getvar(vars, name)`; NULL: result 0.
2. `count = 0`. While the byte at `p` is not NUL: `bcm_strtoul(p, &end, 0)`;
   `p = end`; if the byte at `p` is `,`, `p` advances by one; `count` += 1.
3. Result `count`.

(With a byte that the parser does not consume and that is not a comma this
loops for ever, as the object does; the values the driver builds never contain
one.)

### bcm_strtoul (.text+0x00a5f4, name original)

`unsigned long bcm_strtoul(const char *cp, char **endp, unsigned int base)`
(64 bit result).

1. Skip white space (table above).
2. Sign: a `+` is skipped; a `-` is skipped and makes the result negative
   (step 6). Only one sign character is accepted.
3. Base:
   * `base` is 0: if the text starts with `0x` or `0X`, base 16 and both
     characters are skipped; else if it starts with `0`, base 8 and the `0`
     is skipped; else base 10.
   * `base` is 16: a leading `0x` or `0X` is skipped.
   * any other `base`: used as it is, nothing skipped.
4. `result = 0`, `last = 0`. Repeat:
   * Take the byte `c`. Its digit value: `c - '0'` for a decimal digit,
     `c - 'a' + 10` for a lower-case hexadecimal letter, `c - 'A' + 10` for an
     upper-case one; any other byte ends the number.
   * If the digit value is not less than the base, the number ends.
   * `result = result * base + digit` (64 bit, wrapping).
   * If `result < last` (the multiplication or addition wrapped) and the text
     had no `-`: **return all ones (0xffffffffffffffff) at once, without
     storing `endp`**.
   * `last = result`; go to the next byte.
5. (Number ended.)
6. If the text had a `-`, `result = -result` (two's complement).
7. If `endp` is not NULL, `*endp` = the address of the byte that ended the
   number. Result `result`.

Note that with base 8 chosen by a leading `0`, `"0"` alone gives 0 with `endp`
after the `0`, and `"08"` gives 0 with `endp` at the `8`.

### bcm_bitcount (.text+0x00ab85, name original)

`unsigned int bcm_bitcount(const u8 *bitmap, unsigned int length)`: the number
of bits set in the `length` bytes at `bitmap` (32 bit result).

### hndcrc8 (.text+0x00a9a6, name original)

`u8 hndcrc8(const u8 *data, unsigned int nbytes, u8 crc)`: for each of the
`nbytes` bytes in order, `crc = table[crc ^ byte]` (table above). Result
`crc`. No final inversion; the caller passes the initial value (the SROM code
passes 0xff and compares the result with a constant, srom-otp specification).

### phy_getvar (wlc_phy_cmn.c, .text+0x0bcd81, 108 bytes, name original)

`char *phy_getvar(phy_info_t *pi, const char *name)`: the lookup of
Broadcom's PHY in its own copy of the variables pointer, `pi+0x158`
(acphy-attach.md, "Data"). It is `getvar` above with that pointer as `vars`
and **without** step 4: a name that is not found gives NULL, the global NVRAM
is not asked. (Added 2026-11-02 by the coordinating session from the
disassembly.)

Its only caller is a stripped helper of the same file (`sub_0bcded`, the
"fab" variant: when the 16-bit fab id `pi+0x10fc` is not 0 it first looks up
`<name>.fab.<id>` in a buffer it allocates, then `name`), which the PHY's
`phy_getvar_fabid`, `phy_getintvar_default` and `phy_getintvararray_default`
call. With Broadcom's PHY running, every SROM variable the PHY's attach reads
goes through it: measured 2026-11-02, `phy_getvar` alone was 13.7% of all
execution in the default hybrid build (`open_phy` off). (Note: after the PHY's
attach `pi+0x158` holds its own address, acphy-attach.md step 21, so later
lookups scan the bytes of that pointer and find nothing; the open lookup
scans the same bytes.)

## Verification

* All seven procedures were read from the disassembly; the character classes
  and the CRC table were read from the object's data (`bcm_ctype`;
  `.rodata+0x5f60`, which equals the table generated from the polynomial
  above in all 256 entries).
* `getvar` is called thousands of times in a session (by the PHY's attach
  for every SROM variable, by the MAC's attach, the PMU, the LEDs); any
  difference in its result changes register values the driver writes. The
  test of an implementation is the whole-session comparison
  `python tools/re/hybrid.py run utils` (0 differences expected).

## Open questions

* `nvram_get` (the fallback of `getvar`): on this driver the global NVRAM is
  normally empty, so a name that is not in the variables gives NULL. Not
  specified here; it stays the object's in the hybrid build.
