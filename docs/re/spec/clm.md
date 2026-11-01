# CLM: the regulatory database interpreter (`wlc_clm.c`)

Status: complete (analyst `clm`); all entry points verified against an
independent model, see Verification. Every statement says whether it was
**run** (checked by calling the object's code in the emulator, harness
`re-out\analysis\clm\clmrun.py`) or only **read** (from the code).

## Scope

All 39 functions of `wlc_clm.c` (12,124 bytes). The 16 entry points used by
`wlc_channel.c` / `wlc_cntry.c` are marked E; functions with no caller in the
object are marked U (described briefly).

| function | .text offset | size | name |
|---|---|---|---|
| `clm_init` | 0x0751ce | 27 | original, E |
| `clm_set_inc_data` | 0x0751e9 | 13 | original, E |
| `clm_iter_init` | 0x0751f6 | 24 | original, E |
| `clm_limits_params_init` | 0x07520e | 45 | original, E |
| `clm_country_channels` | 0x07523b | 264 | original, E |
| `clm_country_flags` | 0x075343 | 653 | original, E |
| `clm_limits` | 0x0755d0 | 1996 | original, E |
| `clm_regulatory_limit` | 0x076356 | 188 | original, E |
| `clm_get_base_app_version_string` | 0x076412 | 81 | original, E |
| `clm_get_inc_app_version_string` | 0x076463 | 81 | original, E |
| `clm_agg_country_lookup` | 0x0767f2 | 181 | original, E |
| `clm_agg_country_map_lookup` | 0x076a3f | 189 | original, E |
| `clm_country_lookup` | 0x076ce8 | 192 | original, E |
| `clm_country_iter` | 0x076da8 | 344 | original, E |
| `clm_country_def` | 0x076f00 | 290 | original, E |
| `clm_country_advertised_cc` | 0x077022 | 406 | original, E |
| `sub_07425c` | 0x07425c | 498 | assigned: `clm_get_locale_def` |
| `sub_07444e` | 0x07444e | 135 | assigned: `clm_get_band_locales` |
| `sub_0744d5` | 0x0744d5 | 613 | assigned: `clm_set_chan_combs` |
| `sub_07473a` | 0x07473a | 393 | assigned: `clm_get_channel_set` |
| `sub_0748c3` | 0x0748c3 | 219 | assigned: `clm_channel_in_range` |
| `sub_07499e` | 0x07499e | 1501 | assigned: `clm_set_data` |
| `sub_074f7b` | 0x074f7b | 175 | assigned: `clm_remap_rev` |
| `sub_07502a` | 0x07502a | 99 | assigned: `clm_get_country_ccrev` |
| `sub_07508d` | 0x07508d | 162 | assigned: `clm_get_country_locale_idx` |
| `sub_07512f` | 0x07512f | 159 | assigned: `clm_apply_subchan_rule` |
| `sub_07657e` | 0x07657e | 110 | assigned: `clm_get_table_item` |
| `sub_0765ec` | 0x0765ec | 37 | assigned: `clm_get_country_rec` |
| `sub_076611` | 0x076611 | 247 | assigned: `clm_get_region_ccrev` |
| `sub_076708` | 0x076708 | 121 | assigned: `clm_get_aggregate` |
| `sub_076781` | 0x076781 | 113 | assigned: `clm_find_aggregate` |
| `sub_07699b` | 0x07699b | 164 | assigned: `clm_agg_find_region` |
| `sub_076c6c` | 0x076c6c | 124 | assigned: `clm_find_country` |
| `clm_valid_channels_5g` | 0x075d9c | 656 | original, U |
| `clm_channels_params_init` | 0x07602c | 31 | original, U |
| `clm_valid_channels` | 0x07604b | 779 | original, U |
| `clm_get_string` | 0x0764b4 | 202 | original, U |
| `clm_agg_country_iter` | 0x0768a7 | 244 | original, U |
| `clm_agg_map_iter` | 0x076afc | 368 | original, U |

Data objects of the object that hold the database: `clm_header`
(.rodata+0x284eb0, 120 bytes) and `clm_data` (.rodata+0x284f30, 368 bytes,
the root table); all further tables are anonymous .rodata reached through
the pointers of `clm_data` (.rodata+0x2850a0 .. about +0x2b59c0). The open
code keeps reading the database through these two symbols; it is not
described value by value here.

## Overview

The CLM ("country locale matrix") layer is a pure interpreter of a
read-only database. It makes no hardware access, calls no other file of the
object (no imports, no services: run, the entry points work on a bare
emulator without attach) and keeps all its state in .bss+0xb50..+0xf20,
which no other file references.

* **Data sets.** Two data sets can be loaded: the *base* set (`clm_init`,
  always the built-in `clm_header`) in slot 1 and an optional *incremental*
  set (`clm_set_inc_data`, a CLM blob downloaded at run time through the
  `clm_dload` iovar) in slot 0. Every lookup searches the incremental set
  first and falls back to the base set; records of the incremental set can
  override, delete or inherit records of the base set.
* **Identifiers.** Entities are handed to the caller as 32-bit ids:
  `id = ((set << 30) | (index & 0x3fffffff)) + 1`, set 0 = incremental,
  1 = base. 0 is never a valid id (it is the "start" value of iterators).
  Countries, aggregates and locales use this encoding.
* **Countries** are keyed by a two-character code and a 16-bit revision
  (`cc/rev`, e.g. `US/0`, `XW/0`). A country record names four *locales*
  (2.4 GHz base, 5 GHz base, 2.4 GHz HT, 5 GHz HT) and a flags byte.
  **Aggregates** (also keyed `cc/rev`) map a list of countries to the
  revision to use for each (used by the driver's ccode/regrev logic).
  **Advertised-cc** tables map countries to the code to advertise (802.11d).
* **Locales** hold the regulatory content: the set of valid channels, the
  set of "restricted" channels, regulatory (country-wide) power limits per
  channel range, DFS type and other flags (base locales), and the per-rate
  transmit power limits, grouped by bandwidth (base and HT locales).
* The driver (in `wlc_channel.c`) uses the layer like this (run, emulated
  session, country `XW/0`, see Verification): `clm_init(&clm_header)` at
  attach; `clm_country_lookup` / `clm_agg_country_lookup` when a country is
  set (and `clm_agg_country_map_lookup` when an aggregate exists - not the
  case for the session's "ww"/0); `clm_country_advertised_cc`;
  `clm_country_def(id, &locales)` to turn the country id into a locale set;
  `clm_country_flags`, `clm_country_channels` per band;
  `clm_limits_params_init` + `clm_limits` (once per channel and
  bandwidth/sub-channel kind) for the tx power limits of a channel.
  `clm_regulatory_limit` (regulatory maximum of a channel, from
  `wlc_get_reg_max_power_for_channel`), `clm_country_iter`,
  `clm_set_inc_data` and the version getters are not reached in the
  session (called directly in the harness).
* Hot spot: every query that needs a locale walks the locale table from its
  start up to the wanted index (`sub_07425c`), because locale records have
  variable length and there is no index. `clm_limits` does this twice per
  call (base and HT locale). Caching the result per locale id is the obvious
  optimisation for an open implementation and is behaviour-neutral.

Result codes (all `int` entry points): 0 = OK, 1 = error (bad argument,
null pointer, bad data or id), -1 = not found / not available. Names used
below: `CLM_OK`, `CLM_ERR`, `CLM_NOT_FOUND` (assigned).

## Data

### Entry point prototypes (as the object uses them)

Types: `uint8`, `int8`, `uint16`, `uint32`, `uint64` as usual; `bool` results
are 0/1 in eax. Argument registers per SysV; narrow arguments are taken from
the low bits of the register as noted. Read; all 16 called with these
arguments from the harness (run).

```c
int  clm_init(const struct clm_header *hdr);           /* load base set (slot 1) */
int  clm_set_inc_data(const struct clm_header *hdr);   /* load (hdr!=NULL) or drop (NULL) incremental set (slot 0) */
int  clm_iter_init(uint32 *iter);                      /* *iter = 0; result 0, or 1 if iter == NULL */
int  clm_limits_params_init(struct clm_limits_params *p);   /* defaults; 0, or 1 if p == NULL */
int  clm_country_channels(const struct clm_country_locales *loc, uint32 band,
                          uint8 valid[25], uint8 restricted[25]);
int  clm_country_flags(const struct clm_country_locales *loc, uint32 band, uint64 *flags);
int  clm_limits(const struct clm_country_locales *loc, uint32 band, uint32 channel,
                int ant_gain /* low 8 bits, signed */, uint32 limits_type,
                const struct clm_limits_params *p, int8 limits[336]);
int  clm_regulatory_limit(const struct clm_country_locales *loc, uint32 band,
                          uint32 channel, uint32 *limit);
const char *clm_get_base_app_version_string(void);
const char *clm_get_inc_app_version_string(void);
int  clm_agg_country_lookup(const char cc[2], uint32 rev /* low 16 bits */, uint32 *agg_id);
int  clm_agg_country_map_lookup(uint32 agg_id, const char cc[2], uint32 *rev);
int  clm_country_lookup(const char cc[2], uint32 rev /* low 16 bits */, uint32 *country_id);
int  clm_country_iter(uint32 *iter, char cc[2], uint32 *rev);
int  clm_country_def(uint32 country_id, struct clm_country_locales *loc);
int  clm_country_advertised_cc(uint32 country_id, char cc[2]);
```

`band`: 0 = 2.4 GHz, 1 = 5 GHz (any value >= 2 is an error).
`channel`: a 20/40/80/160 MHz channel number (centre channel for wide
channels); only its low 8 bits matter in `clm_limits`.
A code of two NUL characters passed as `cc` means `"ww"` (all lookups).

### struct clm_country_locales (filled by `clm_country_def`, 18 bytes written, 20 with padding)

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0x00 | 4 | `locale[0]` | locale id, 2.4 GHz base locale |
| 0x04 | 4 | `locale[1]` | locale id, 5 GHz base locale |
| 0x08 | 4 | `locale[2]` | locale id, 2.4 GHz HT locale |
| 0x0c | 4 | `locale[3]` | locale id, 5 GHz HT locale |
| 0x10 | 1 | `flags` | the country's flags byte (see "Country record"), 0 if the format has none |
| 0x11 | 1 | `set` | data set the country record came from (0 incremental, 1 base) |

Bytes 0x12..0x13 are not written (run: they keep their old contents).
A locale id is `((set << 30) | index) + 1`; the index value 0xfff means
"no locale" (valid, empty), 0xffe "invalid" (lookups fail).

"Locale type" t = index into `locale[]`: 0 = base 2.4 GHz, 1 = base 5 GHz,
2 = HT 2.4 GHz, 3 = HT 5 GHz. Base locales carry channel sets, regulatory
limits, flags and power records; HT locales carry only power records (for
the MCS/VHT rates).

### struct clm_limits_params (input of `clm_limits`, 16 bytes)

| Offset | Size | Field | Default (`clm_limits_params_init`) | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | `bw` | 0 | channel bandwidth: 0 = 20 MHz, 1 = 40, 2 = 80, 3 = 160, 4 = 80+80 (>= 5: error) |
| 0x04 | 4 | `sar` | 0x7f | signed upper bound applied to every limit that is not "disabled" |
| 0x08 | 4 | `category` | 0 | power column selector 0..3 (>= 4: error); selects byte 0, 3, 4 or 5 of a power record (purpose of the columns unknown; name assigned) |
| 0x0c | 4 | `other_80_80` | 0 | the other 80 MHz segment of an 80+80 channel: passed as `other` to `sub_0748c3` for the main channel matches of `clm_limits`; matters only where a comb is empty (5 GHz 80+80 in this object), where a range is a literal (channel, other) pair (run: US/150, bw 4, channel 42 with other 106 -> limits; with other 0 or 58 -> -1) |

`clm_limits_params_init` writes, in this order: +0x00 = 0, +0x08 = 0,
+0x04 = 0x7f, +0x0c = 0 (read).

### limits_type (argument of `clm_limits`)

Selects which part of a wide channel the limits are for. Table
.rodata+0x284cf0 (15 bytes) gives a code per type: high nibble = depth d
(number of halvings from the channel's bandwidth), low bits = the path
(bit d-1 for the first halving ... bit 0 for the last; 1 = upper half,
0 = lower half).

| type | code | meaning (assigned names) |
|---|---|---|
| 0 | 0x00 | the whole channel (`CHANNEL`) |
| 1, 2 | 0x10, 0x11 | lower / upper half (`SUBCHAN_L`, `SUBCHAN_U`) |
| 3..6 | 0x20..0x23 | quarters `LL`, `LU`, `UL`, `UU` |
| 7..14 | 0x30..0x37 | eighths `LLL` .. `UUU` |

### limits[] (output of `clm_limits`)

336 signed bytes (`WL_NUMRATES` of `bcmwifi_rates.h`), indexed by the rate
index of `clm_rates_t` in `src\shared\bcmwifi\include\bcmwifi_rates.h`.
Values are power limits in the database's unit (quarter dBm in Broadcom's
convention - unverified), 0x80 (-128, `WL_RATE_DISABLED`) = rate not
allowed. (Read; run for XW/0.)

### flags (output of `clm_country_flags`, 64 bit)

Starts as 0xe6000000; bits are then set or cleared (read, run). Names are
assigned from what sets the bit:

| Bit(s) | Value | Set / cleared when | assigned name |
|---|---|---|---|
| 0..1 | 1, 2, 3 | base locale flags byte bits 0..1 (copied) | DFS type (1, 2, 3; 0 = none) |
| 2 | 0x4 | base locale flags byte bit 2 | purpose unknown (the caller `wlc_channel_reg_limits` calls `wlc_bmac_filter_war_upd` near its use: a filter workaround - unverified) |
| 3 | 0x8 | country flags bit 3 | purpose unknown |
| 4 | 0x10 | any power group of base or HT locale has records wider than 3 bytes | per-category powers present |
| 5 | 0x20 | country flags bit 4 | purpose unknown |
| 6 | 0x40 | country flags bit 5 | purpose unknown |
| 25 | 0x02000000 | initially set; cleared if an 80+80 MHz group has a record with power != 0x80 | `NO_80_80MHZ` |
| 26 | 0x04000000 | initially set; cleared likewise for 160 MHz | `NO_160MHZ` |
| 27 | 0x08000000 | set if an OFDM rate occurs in an enabled record (power != 0x80) of a group with flag "EIRP" (group flag bit 1) | `HAS_OFDM_EIRP` (name unverified) |
| 28 | 0x10000000 | set if a DSSS rate occurs in an enabled record of such a group | `HAS_DSSS_EIRP` (name unverified) |
| 29 | 0x20000000 | initially set; cleared if an MCS/VHT rate occurs in an enabled record | `NO_MIMO` / no HT rates |
| 30 | 0x40000000 | initially set; cleared likewise for 40 MHz | `NO_40MHZ` |
| 31 | 0x80000000 | initially set; cleared likewise for 80 MHz | `NO_80MHZ` |

### State of the layer (.bss)

Two data-set slots of 0x1b8 bytes: **slot 0 at .bss+0xb50 = incremental
set**, **slot 1 at .bss+0xd08 = base set** (run: after `clm_init` only
slot 1 is filled). Plus two globals:

| Address | Size | Name (assigned) | Meaning |
|---|---|---|---|
| .bss+0xec0 | 0x54 | `rate_class[]` | 2 bits per rate index 0..335: 0 = DSSS, 1 = OFDM, 2 = HT/VHT (MCS); rebuilt by every load |
| .bss+0xf18 | 8 | `bw_map` | pointer to the group-bandwidth map (.rodata+0x284e60), set by `clm_init` |

Slot layout (offsets from the slot start; `R` = the set's root table,
`D` = the set's relocation delta; every pointer copied from the database is
stored already relocated, i.e. `p ? p + D : NULL`):

| Offset | Size | Field (assigned) | Set by `clm_set_data` (sub_07499e) / `clm_set_chan_combs` (sub_0744d5) |
|---|---|---|---|
| 0x000 | 8 | `root` | `hdr->root + D` (header +0x38), NULL when not loaded |
| 0x008 | 4 | `delta` | D = (uint32)(address of header) - (uint32)(header +0x30) |
| 0x010 | 0xa0 | `comb[2][5]` | per band (0x50 each) and bandwidth (0x10 each): `{uint32 count; pad; ptr triples}` = the valid channel grid (see "Channel combs") |
| 0x0b0 | 4 | `has_comb_tables` | 1 if new format and root flags bit 7 |
| 0x0b4 | 4 | `rev_hi_idx` | extra-byte index of the high byte of the country revision, -1 if none |
| 0x0b8 | 4 | `country_size` | size in bytes of a country record |
| 0x0c0 | 8 | `header` | the header pointer as passed (not relocated) |
| 0x0c8 | 4 | `has_160_rules` | 1 if new format and root flags bit 3 |
| 0x0d0 | 0x28 | `ranges[5]` | channel range table per bandwidth 0..4 |
| 0x0f8 | 0x50 | `ratesets[2][5]` | rate set list per band and bandwidth |
| 0x148 | 4 | `loc_hi1_idx` | extra-byte index of locale index bits 8..9, -1 if none |
| 0x14c | 4 | `loc_hi2_idx` | extra-byte index of locale index bits 10..11, -1 if none |
| 0x150 | 4 | `flags_idx` | extra-byte index of the country flags byte, -1 if none |
| 0x154 | 4 | `region_fmt` | encoding of aggregate / advertised-cc region lists: 0 inline 3 bytes, 1 one-byte country index, 2 two-byte country index |
| 0x158 | 0x50 | `ext_ratesets[2][5]` | rate set list for "extended" groups (rate index +104) per band and bandwidth |
| 0x1a8 | 4 | `flag24` | root flags bit 24 (new format), else 0; not read by any function of the object |
| 0x1b0 | 8 | `rev_remap` | pointer to the revision remap table (root +0x168) if new format and root flags bit 25, else unchanged |

(The extra-byte indices count from byte 7 of a country record.)

The remaining fields are described with the data format below and with
`sub_07499e`.

### Constant tables in .rodata (facts)

| Address | Size | Content |
|---|---|---|
| .rodata+0x284cd8 | 4 | `00 00 'w' 'w'`: the string "ww" (at +0x284cda) used for an empty country code |
| .rodata+0x284ce0 | 4 x u32 | power column offset per `params.category`: {0, 3, 4, 5} |
| .rodata+0x284cf0 | 15 x u8 | limits_type codes {0x00, 0x10, 0x11, 0x20, 0x21, 0x22, 0x23, 0x30..0x37} |
| .rodata+0x284d00 | 4 x (u32, u32) | (output offset, locale type) for `clm_country_def`: (0,0) (4,1) (8,2) (12,3) |
| .rodata+0x284d20 | 7 x (u32 band, u32 bw, u32 root offset) | extended rate set lists: (0,0,0xf0) (0,1,0xf8) (1,0,0x100) (1,1,0x108) (1,2,0x110) (1,4,0x110) (1,3,0x118) |
| .rodata+0x284d74 | 4 | default comb, 2.4 GHz 40 MHz: one triple (3, 11, 1) |
| .rodata+0x284d78 | 9 | default comb, 5 GHz 40 MHz: (38, 62, 8) (102, 142, 8) (151, 159, 8) |
| .rodata+0x284d88 | 9 | default comb, 5 GHz 80 MHz: (42, 58, 16) (106, 138, 16) (155, 155, 16) |
| .rodata.str1.1 "22 rr " | 6 | default comb, 5 GHz 160 MHz: the bytes of the string constant "22 rr " = (50, 50, 32) (114, 114, 32) (the compiler merged the table with an equal string) |
| .rodata+0x284da0 | 4 x (u32 band, u32 bw, u32 root offset) | comb tables of wide channels: (0,1,0xc8) (1,1,0xd0) (1,2,0xd8) (1,3,0xe0) |
| .rodata+0x284dd0 | 12 x (u32 start, u32 count, u32 class) | rate class runs, applied in order: (0,336,2) (0,4,0) (24,4,0) (104,4,0) (4,8,1) (28,8,1) (108,8,1) (72,8,1) (164,8,1) (208,4,0) (212,8,1) (280,8,1) |
| .rodata+0x284e60 | 0x50 x u8 | group bandwidth map, index = group flags & 0x49: [0x00]=0 (20 MHz), [0x01]=1 (40), [0x08]=2 (80), [0x09]=3 (160), [0x48]=4 (80+80); all other entries 0 |

Rate classes after the runs: class 0 (DSSS) for rate indices 0..3, 24..27,
104..107, 208..211; class 1 (OFDM) for 4..11, 28..35, 72..79, 108..115,
164..171, 212..219, 280..287; class 2 (HT/VHT) for all others (read; the
.bss table matched this model in every comparison run).

The two words named in the assignment: **.bss+0xb50 = `S[0].root`** and
**.bss+0xb58 = `S[0].delta`** (the incremental set's root pointer and
delta), written only by `sub_07499e` (from `clm_set_inc_data`) and read by
nearly every function (the slot 1 fields are reached as .bss+0xb50 +
0x1b8 = .bss+0xd08 by indexing, which is why the index lists only
.bss+0xb50/0xb58 for most functions).

### Combining the base and incremental sets (summary)

| Entity | Rule (functions) |
|---|---|
| country lookup | set 0 searched first, then set 1; a found record whose type-0 locale is 0xffd counts as "not found" (`clm_country_lookup`) |
| country iteration | set 0 records (not deleted), then set 1 records that have no record with the same cc/rev in set 0 (`clm_country_iter`) |
| locale of a country | per locale type: 0xffe = take the base set's record of the same cc/rev (0xfff if none) (`clm_country_def`) |
| locales themselves | a locale id carries its set; records are read from that set's tables, with that set's range tables, rate sets and combs (`sub_07425c`) |
| sub-channel rules | from the set of the country record (`clm_limits`) |
| aggregates | set 0 first, then set 1; n = -1 = deleted (`clm_agg_country_lookup`); per-country mapping: set 0 aggregate first, then the set 1 aggregate of the same cc/rev; rev 0xffff = excluded (`clm_agg_country_map_lookup`) |
| advertised cc | set 0 items, then set 1 items whose code has no item in set 0; n = -1 = deleted (`clm_country_advertised_cc`) |
| combs | an empty comb of set 0 is copied from set 1, and an empty comb of set 1 from set 0 (`sub_0744d5`) |

### Notes for an implementation

* The 16 entry points are the whole interface (callers in `wlc_channel.c`
  and `wlc_cntry.c` only; the U functions have no callers).
  `wlc_channel_mgr_attach` passes `&clm_header` to `clm_init`.
  `wlc_handle_clm_dload` (read in its disassembly, not run) passes a
  downloaded blob either to `clm_set_inc_data` (on failure it calls
  `clm_set_inc_data(NULL)`) or to `clm_init` (on failure, or when no blob is
  kept, it calls `clm_init(&clm_header)` again), then re-resolves the
  current country with `clm_country_lookup`. So both slots can hold
  run-time data, and `clm_init` can be called more than once.
* Exactness that callers can observe: return codes 0/1/-1; outputs
  untouched on argument errors; `clm_limits` fills 0x81 before the locale
  lookup (so a failed lookup leaves 0x81) and normalises to 0x80 only on
  success; `clm_country_flags` writes 0xe6000000 before the lookup;
  `clm_country_def` does not write bytes 0x12..0x13; `*iter` values are ids.
* The locale walk can be cached (per set, type and index) without changing
  any result; the cache must be dropped when a set is (re)loaded.

All multi-byte fields are little endian. "ptr" = 64-bit pointer stored in
the data; the code uses `ptr ? ptr + delta : NULL` (delta of the data set,
see the slot table). A "{count, ptr} table" is 16 bytes: u32 count at +0,
4 bytes padding, ptr at +8 to `count` items. (All read; the formats were
checked by the model of `compare.py`, which decodes the database
independently and reproduces the object's results - see Verification.)

### Header (`clm_header`, 120 bytes in this object)

| Offset | Size | Content (this object's values) |
|---|---|---|
| 0x00 | 9 | magic "CLM DATA" with its NUL; compared byte by byte up to the first NUL of either string, so the header must contain "CLM DATA\0" |
| 0x08 | 2 | not read (0) |
| 0x0a | 2 | format major, signed (16) |
| 0x0c | 2 | format minor (0) |
| 0x0e | 20 | string, CLM version ("8.1.0"), read only by `clm_get_string` |
| 0x22 | 30 | string ("1.27.6"), read only by `clm_get_string` (customer/data version - unverified) |
| 0x30 | 8 | the header's own address as the data was linked (self pointer) |
| 0x38 | 8 | ptr to the root table |
| 0x40 | 30 | string ("ClmImport: 1.29.5", the generator version), read only by `clm_get_string` |
| 0x5e | 26 | string, application version ("Broadcom-0.0"), see the app version getters |

Accepted formats: major/minor exactly 5/1 ("old format"), or major 7..16
with any minor ("new format"). Anything else is rejected.

Relocation: `delta = (uint32)hdr - (uint32)*(uint64*)(hdr+0x30)`, kept as an
unsigned 32-bit value and added zero-extended to 64-bit pointers. For the
built-in data, the kernel's relocation of the self pointer makes delta 0
(run: delta 0 in the emulator, which relocates the object like the kernel).
For downloaded data the pointers are relative to the address the data
generator assumed.

### Root table (`clm_data`, 46 qwords)

| Offset | Content | Used by |
|---|---|---|
| 0x00 | ptr to comb {count, ptr} of 2.4 GHz 20 MHz channels | `sub_0744d5` |
| 0x08 | ptr to comb of 5 GHz 20 MHz channels | `sub_0744d5` |
| 0x10 | ptr to channel range table, 20 MHz | slot.ranges |
| 0x18 | ptr to the restricted channel set list | `clm_country_channels` |
| 0x20 | ptr to the valid channel set list | `clm_country_channels` |
| 0x28 | ptr to rate set list, 5 GHz 20 MHz (also used for 2.4 GHz unless flags bit 5, and for everything in the old format) | slot.ratesets |
| 0x30, 0x38, 0x40, 0x48 | ptr to the locale tables of type 0 (base 2.4), 1 (base 5), 2 (HT 2.4), 3 (HT 5) | `sub_07425c` |
| 0x50 | ptr to {count, ptr} country table | `sub_0765ec` |
| 0x58 | ptr to {count, ptr} advertised-cc table (16-byte items) | `clm_country_advertised_cc` |
| 0x60 | ptr to {count, ptr} aggregate table (16-byte items) | `sub_076708` |
| 0x68 | u32 flags (this object: 0x01e950ff), see below | `sub_07499e` |
| 0x70 | ptr to {count, ptr} 80 MHz sub-channel rule sets | `clm_limits` |
| 0x78 | ptr to {count, ptr} 160 MHz sub-channel rule sets | `clm_limits` |
| 0x80, 0x88, 0x90 | ptr to channel range tables, 40, 80, 160 MHz | slot.ranges |
| 0x98, 0xa0, 0xa8 | ptr to rate set lists, 5 GHz 40, 80, 160 MHz | slot.ratesets |
| 0xb0, 0xb8 | ptr to rate set lists, 2.4 GHz 20, 40 MHz (if flags bit 5) | slot.ratesets |
| 0xc0 | ptr to a string (this object: "2015-09-18 15:46:07", the generation date) | `clm_get_string` |
| 0xc8, 0xd0, 0xd8, 0xe0 | ptr to combs: 2.4 GHz 40 MHz; 5 GHz 40, 80, 160 MHz (if flags bit 7) | `sub_0744d5` |
| 0xe8 | ptr to {count, ptr} table of extra region codes (3 or 4-byte items; empty in this object) | `sub_076611` |
| 0xf0 .. 0x118 | ptr to extended rate set lists (see .rodata+0x284d20; if flags bit 23) | slot.ext_ratesets |
| 0x120 .. 0x160 | not read by this version of the code (this object: 0 or pointers to empty tables) | - |
| 0x168 | ptr to the revision remap table (if flags bit 25; 0 in this object) | `sub_074f7b` |

Root flags (u32 at +0x68), new format only (the old format behaves as if
all were 0):

| Bit | Meaning (as used) |
|---|---|
| 0 | (only if bit 16 is clear) country records have one extra byte (locale bits 8..9) and a flags byte: size 9 |
| 1 | the header's application version string (+0x5e) is valid |
| 3 | the 160 MHz sub-channel rule sets (root+0x78) exist |
| 4 | per-bandwidth range tables and rate set lists exist (root+0x80..0xb8); else every bandwidth uses the 20 MHz range table and the root+0x28 rate set list |
| 5 | 2.4 GHz has its own 20/40 MHz rate set lists (root+0xb0/0xb8) |
| 6 | the date string (root+0xc0) exists |
| 7 | the wide-channel comb tables exist (root+0xc8..0xe0); else built-in defaults are used |
| 8..15 | must be 0x50 or 0, else the data is rejected |
| 16 | country record layout given by bits 18..19 and 22 (see "Country record") |
| 18..19 | number of extra bytes holding high bits of the locale indices (0, 1, 2) |
| 21 | region lists use 2-byte country indices (else 1-byte) |
| 22 | country records have a high revision byte |
| 23 | extended rate set lists exist (root+0xf0..0x118) |
| 24 | copied to slot+0x1a8 (not used) |
| 25 | revision remap table exists (root+0x168) |

### Country record (root+0x50 table, `slot.country_size` bytes each)

| Byte | Content |
|---|---|
| 0, 1 | country code (two characters, e.g. 'X','W') |
| 2 | revision, low 8 bits |
| 3, 4, 5, 6 | locale index of type 0, 1, 2, 3, low 8 bits |
| 7 + `rev_hi_idx` | revision, high 8 bits (if `rev_hi_idx` >= 0) |
| 7 + `loc_hi1_idx` | locale index bits 8..9: type t in bits 2t..2t+1 (if >= 0) |
| 7 + `loc_hi2_idx` | locale index bits 10..11: type t in bits 2t..2t+1 (if >= 0) |
| 7 + `flags_idx` | country flags byte (if >= 0) |

Layout parameters (computed by `sub_07499e` from the root flags):

| Case | rev_hi_idx | loc_hi1_idx | loc_hi2_idx | flags_idx | size | region_fmt |
|---|---|---|---|---|---|---|
| new, bit 16 set | b22 ? 0 : -1 | n>=1 ? next : -1 | n==2 ? next : -1 | next | flags_idx + 8 | b21 ? 2 : 1 |
| new, bit 16 clear, bit 0 set | -1 | 0 | -1 | 1 | 9 | 0 |
| otherwise (incl. old format) | -1 | -1 | -1 | -1 | 7 | 0 |

("next" = the next free extra-byte index, counting from rev_hi_idx + 1;
n = bits 18..19.) This object: rev_hi 0, loc_hi1 1, loc_hi2 2, flags 3,
size 11, region_fmt 2 (run: 5388 country records, the first is
"#a"/0).

Revision of a record (`sub_07502a`): with a high byte: `hi << 8 | lo`.
Without: lo, except lo = 0xff gives 0xffff, and with a remap table the
8-bit value is mapped (`sub_074f7b`).

Locale index of type t (`sub_07508d`): `v = byte[3+t]`; with loc_hi1:
`v |= ((byte[7+loc_hi1] >> 2t) & 3) << 8`; with loc_hi2:
`v |= ((byte[7+loc_hi2] >> 2t) & 3) << 10`. Let `full` = 0xff, 0x3ff or
0xfff (8, 10 or 12 bits available). Then: `v == full` gives 0xfff ("no
locale"), `v == full & 0xffe` gives 0xffe ("same as the base set's record
of this cc/rev"), `v == full & 0xffd` gives 0xffd ("country deleted"),
other values are the index.

Country flags byte: bits 0..2 and 6 select the sub-channel rule set
(`index = ((f & 7) | ((f >> 3) & 8)) - 1`, none if `f & 0x47 == 0`); bits 3,
4, 5 become `clm_country_flags` bits 3, 5, 6 (XW/0: 0x22, rule set 1).

### Locale tables (root+0x30 + 8*type)

A locale table is a plain concatenation of variable-length locale records
without an index; locale n is found by skipping n records from the start.

Base locale record (types 0 and 1):

| Byte | Content |
|---|---|
| 0 | valid channel set index (into the list at root+0x20; 0xff = none) |
| 1 | locale flags: bits 0..1 DFS type, bit 2 copied to `clm_country_flags` bit 2; other bits not read |
| 2 | restricted channel set index (into the list at root+0x18; 0xff = none) |
| 3 | n = number of regulatory limit pairs |
| 4 .. 3+2n | n pairs (limit, 20 MHz range index); limit = unsigned byte (dBm - unverified unit) |
| 4+2n .. | power groups (below) |

HT locale record (types 2 and 3): power groups only.

Power groups: a sequence of groups; each group:

| Byte | Content |
|---|---|
| 0 | group flags F: bit 7 = a second flags byte follows; bits 4..5 = w - 3 where w is the record width (3..6); bit 2 = another group follows this one; bit 1 = the powers are reduced by the antenna gain (`ant_gain` of `clm_limits`; "EIRP" group - name unverified); bits 0, 3, 6 = bandwidth code (index `F & 0x49` into the bandwidth map: 0x00 20 MHz, 0x01 40, 0x08 80, 0x09 160, 0x48 80+80) |
| (1) | if F bit 7: second flags byte F2: bit 1 = the rate set indices refer to the *extended* rate set list and rate indices are offset by 104 (0x68); other bits not read |
| next | N = number of records |
| next | N records of w bytes: [0] power for category 0 (signed byte; 0x80 = disabled), [1] range index (into the range table of the group's bandwidth), [2] rate set index (into the rate set list of the band and group bandwidth, normal or extended), [3], [4], [5] powers for categories 1, 2, 3 (present if w > 3, > 4, > 5) |

Skipping a group: `p += (F & 0x80 ? 2 : 1); p += 1 + N*w`; stop after a
group with F bit 2 clear.

### Channel range tables

Per bandwidth (20 / 40 / 80 / 160, and 80+80 uses the 80 MHz table): an array
of 2-byte entries (first channel, last channel). A range does not list the
channels; it covers the channels of the band's *comb* for that bandwidth
lying between first and last.

### Channel combs (`slot.comb[band][bw]`)

A comb is a list of triples (first, last, step) describing the channel
numbers that exist for a band and bandwidth, e.g. (this object, run) 2.4 GHz
20 MHz: (1, 14, 1); 5 GHz 20 MHz: (34, 46, 4) (36, 64, 4) (100, 144, 4)
(149, 165, 4); 2.4 GHz 40 MHz: (3, 11, 1); 5 GHz 40 MHz: (38, 62, 8)
(102, 142, 8) (151, 159, 8); 5 GHz 80 MHz: (42, 58, 16) (106, 138, 16)
(155, 155, 16); 5 GHz 160 MHz: (50, 50, 32) (114, 114, 32); none for
2.4 GHz 80/160/80+80 and 5 GHz 80+80. Triples are matched in list order;
"the next triple" after a triple t is the triple with the smallest first
channel greater than t.last.

### Channel set lists (root+0x20 valid, root+0x18 restricted)

A list of length-prefixed byte strings: set k = the k-th string (skip k
strings of `1 + len` bytes). Each byte of a set is a 20 MHz range index,
or 0xff = every channel of the band's 20 MHz comb.

### Rate set lists

A list of length-prefixed byte strings: rate set k = the k-th string; its
bytes are rate indices (`clm_rates_t`), to which 104 is added for groups
using the extended list. E.g. (this object, 2.4 GHz 20 MHz list) set 0 =
rates 12..21, set 2 = rates 0..11 (DSSS and OFDM, one chain).

### Sub-channel rule sets (root+0x70 for 80 MHz, root+0x78 for 160 MHz)

{count, ptr} table of rule sets, 16 bytes each: u32 n at +0, ptr at +8 to n
rules. A rule is 7 bytes (80 MHz) or 15 bytes (160 MHz): byte 0 = range
index into the country's set's range table of that bandwidth; byte k
(1 <= k <= 6 or 14) = for limits_type k, a bit mask over the bandwidth
levels 0..4 (bit i set: the limits of the sub-channel at level i apply,
see `clm_limits`). The rule set is chosen by the country flags byte.

### Aggregate table (root+0x60, 16-byte items)

| Offset | Content |
|---|---|
| 0x00 | if region_fmt != 0: cc[2] and u16 rev; if region_fmt == 0: a 3-byte inline region (cc[2], rev8) decoded like a list element |
| 0x04 | s32 n = number of regions; -1 = aggregate deleted |
| 0x08 | ptr to the region list (n elements) |

### Advertised-cc table (root+0x58, 16-byte items)

| Offset | Content |
|---|---|
| 0x00 | cc[2] to advertise |
| 0x04 | s32 n (-1 = entry deleted) |
| 0x08 | ptr to a region list of the countries that advertise this code |

### Region lists (`sub_076611`)

Element i, by `slot.region_fmt`:
* 0: 3 bytes at `3*i`: cc[2], rev8 (0xff -> 0xffff; remapped if a remap
  table exists).
* 1 or 2: country index = byte i (fmt 1) or u16 i (fmt 2). If the index is
  below the country count of the set, the cc/rev of that country record
  (`sub_07502a`). Otherwise item `index - count` of the extra region table
  (root+0xe8): 4 bytes (cc[2], u16 rev) if the set has `rev_hi_idx` >= 0,
  else 3 bytes decoded as in format 0.
* other values: nothing is written.

### Revision remap table (root+0x168, `sub_074f7b`)

24 bytes: u32 n at +0, ptr at +8 to n+1 country entries of 4 bytes (cc[2],
u16 first), ptr at +16 to 3-byte entries (rev high, rev low, rev8). The
entries of a country are `first(this) .. first(next)-1`; an entry whose
rev8 equals the 8-bit revision gives the 16-bit revision
`high << 8 | low`. Not present in this object (read only).

## Procedures

Pseudo-code conventions: `S[k]` = slot k (0 incremental, 1 base);
`rptr(k, off)` = the relocated pointer at `S[k].root + off`, NULL if
`S[k].root` is NULL or the stored pointer is 0; `split(id)` gives
`set = ((id-1) >> 30) & 1`, `idx = (id-1) & 0x3fffffff`;
`mkid(set, idx) = ((set << 30) | (idx & 0x3fffffff)) + 1`.

### Loading

#### clm_init (.text+0x0751ce, name original)

`int clm_init(const struct clm_header *hdr)`. Stores the address of the
bandwidth map (.rodata+0x284e60) in .bss+0xf18, then returns
`sub_07499e(hdr, 1)` (loads the base set). Called by
`wlc_channel_mgr_attach` with `&clm_header` (read in its disassembly; run:
result 0) and by `wlc_handle_clm_dload` with a downloaded blob or again
with `&clm_header` (read). Must be called before any other entry point (the
power and flag functions read the map through .bss+0xf18).

#### clm_set_inc_data (.text+0x0751e9, name original)

`int clm_set_inc_data(const struct clm_header *hdr)` = `sub_07499e(hdr, 0)`:
load (or with NULL: drop) the incremental set. Caller:
`wlc_handle_clm_dload` (not reached in the emulated session). Run: loading
`&clm_header` a second time as incremental returns 0 and every lookup then
finds its records in set 0 (see Verification).

#### sub_07499e (.text+0x07499e, name assigned: `clm_set_data`)

`int clm_set_data(const struct clm_header *hdr, int k)`; k = slot.

1. If hdr is NULL: `S[k].delta = 0`, `S[k].root = NULL`; go to step 9.
2. Compare the string at hdr with "CLM DATA" (byte by byte until a NUL in
   either or a difference); if they differ return 1.
3. `new = true`; if the u16 pair at hdr+0x0a/0x0c is not exactly (5, 1):
   if `(int16)hdr[0x0a] - 7` as unsigned is > 9 (major not in 7..16) return
   1. If it is (5, 1): `new = false`.
4. `S[k].delta = (uint32)hdr - (uint32)hdr->self`; `S[k].root = hdr->root ?
   hdr->root + delta : NULL`.
5. If new: `F = root->flags (u32 at +0x68)`; if `(F >> 8) & 0xff` is
   neither 0x50 nor 0, return 1 (note: delta and root of the slot are
   already overwritten at this point, and the slot stays in use: run, a
   following `clm_country_lookup` finds its countries in that set with the
   layout fields of the previous load). If F bit 25: `S[k].rev_remap =
   rptr(k, 0x168)` (otherwise the field keeps its old value).
6. `S[k].header = hdr`. Country layout fields (rev_hi_idx, loc_hi1_idx,
   loc_hi2_idx, flags_idx, country_size, region_fmt) as in the table under
   "Country record" (old format and new without bits 16/0: all -1, size 7,
   region_fmt 0). `S[k].has_160_rules = new && F bit 3`.
7. If new and F bit 4:
   * `ranges[0..4] = rptr(0x10), rptr(0x80), rptr(0x88), rptr(0x90),
     rptr(0x88)`;
   * `ratesets[1][0..4] = rptr(0x28), rptr(0x98), rptr(0xa0), rptr(0xa8),
     rptr(0xa0)`;
   * if F bit 5: `ratesets[0][0] = rptr(0xb0)`, `ratesets[0][1] =
     rptr(0xb8)`; else they are copies of `ratesets[1][0]`, `[1][1]`;
     `ratesets[0][2..4]` are not written;
   * if F bit 23: for each (band, bw, off) of .rodata+0x284d20:
     `ext_ratesets[band][bw] = rptr(off)`.

   Otherwise (old format, or no bit 4): for bw 0..4: `ranges[bw] =
   rptr(0x10)`, `ratesets[0][bw] = ratesets[1][bw] = rptr(0x28)`
   (ext_ratesets not written).
8. `S[k].has_comb_tables = new && F bit 7`; `S[k].flag24 = new ? F bit 24
   : 0`.
9. `sub_0744d5(k)`; if k == 1 also `sub_0744d5(0)` (the incremental set
   inherits combs from the base set, so it is rebuilt after a base load).
10. Rebuild the rate class table .bss+0xec0: for each run (start, count,
    class) of .rodata+0x284dd0 in order, set the 2-bit field of every rate
    index in `start .. start+count-1` (rate r: byte `r >> 2`, bits
    `2*(r & 3) .. 2*(r & 3)+1`) to class.
11. Return 0.

Notes: fields not mentioned for a case keep their previous values (static
storage, zero at module load). This object's data (run): new format 16.0,
flags 0x01e950ff, delta 0.

#### sub_0744d5 (.text+0x0744d5, name assigned: `clm_set_chan_combs`)

`void clm_set_chan_combs(int k)`: fills `S[k].comb[2][5]` (each entry
`{u32 count; ptr triples}`, 16 bytes).

1. If `S[k].root` is NULL: set the count of all ten entries to 0 (pointers
   untouched) and return.
2. Source descriptors `src[2][5]`, all NULL; `src[0][0] = rptr(k, 0x00)`,
   `src[1][0] = rptr(k, 0x08)`; if `S[k].has_comb_tables`: for each (band,
   bw, off) of .rodata+0x284da0 - (0,1,0xc8) (1,1,0xd0) (1,2,0xd8)
   (1,3,0xe0) - `src[band][bw] = rptr(k, off)`.
3. For band 0..1, bw 0..4, with `B = S[1].comb[band][bw]`:
   * if `src` is NULL or its count is 0: if k == 0, copy B into
     `S[0].comb[band][bw]` (inherit from the base); if k == 1 leave the
     entry unchanged;
   * else: `S[k].comb[band][bw] = {src.count, relocated src.ptr}`; and if
     k == 0 and B's count is 0, also copy it into B (the base set inherits
     combs that only the incremental set defines).
4. If not `S[k].has_comb_tables`: overwrite with the built-in defaults:
   `comb[0][1] = {1, .rodata+0x284d74}`, `comb[1][1] = {3,
   .rodata+0x284d78}`, `comb[1][2] = {3, .rodata+0x284d88}`, `comb[1][3] =
   {2, "22 rr "}` (values in "Constant tables").

Run: after `clm_init` slot 1 holds exactly the combs listed under "Channel
combs"; slot 0 is all zero.

### Locale access

#### sub_07425c (.text+0x07425c, name assigned: `clm_get_locale_def`)

`bool clm_get_locale_def(const struct clm_country_locales *loc, int type,
struct clm_locale_def *d)`. The hot function of the layer (37.9 % of all
execution of a session).

Output: `struct clm_locale_def` (assigned name), 15 pointers = 0x78
bytes:

| Index | Offset | Content |
|---|---|---|
| 0 | 0x00 | address of the locale record (NULL = no locale) |
| 1 | 0x08 | `&S[set].ranges[0]` (array of 5 range table pointers) |
| 2 | 0x10 | `&S[set].ratesets[band][0]` |
| 3 | 0x18 | valid channel set list, `rptr(set, 0x20)` |
| 4 | 0x20 | restricted channel set list, `rptr(set, 0x18)` |
| 5..9 | 0x28..0x48 | `&S[set].comb[band][bw]` for bw 0..4 |
| 10..13 | 0x50..0x68 | not written (0) |
| 14 | 0x70 | `&S[set].ext_ratesets[band][0]` |

Steps:
1. Zero the 0x78 bytes of d.
2. type 0: `id = loc->locale[0]`, band 0, base; type 1: `locale[1]`, band
   1, base; type 2: `locale[2]`, band 0, HT; type 3: `locale[3]`, band 1,
   HT; any other type: return false.
3. `(set, idx) = split(id)`. If idx == 0xfff return true (d stays all
   zero: "no locale"). If idx == 0xffe return false.
4. `p = rptr(set, 0x30 + 8*type)`; repeat idx times: if base, `p += 4 +
   2*p[3]` (skip the base header); then skip power groups until a group
   without the "more" flag (format above).
5. Fill d as in the table (with `d[0] = p`; note that d[0] points to the
   start of the record, including a base header), return true.

Notes: no bounds check on idx or on a missing set (a set without data and
idx > 0 would dereference NULL). Cost is proportional to the sum of the
sizes of all preceding records; an implementation may cache the record
address per (set, type, idx) - the result only changes when a set is
loaded. Run: every power/flag/channel comparison below goes through it.

#### sub_07444e (.text+0x07444e, name assigned: `clm_get_band_locales`)

`bool clm_get_band_locales(const struct clm_country_locales *loc,
uint32 band, struct clm_locale_def d[2], uint8 *base_flags)`.

1. If base_flags is not NULL: `*base_flags = 0`.
2. `ok = sub_07425c(loc, band != 0 ? 1 : 0, &d[0])`; if !ok return false.
3. If `d[0].record` is not NULL: `*base_flags = record[1]` (if
   base_flags is not NULL), then advance `d[0].record` past the base header
   (`+= 4 + 2*record[3]`) so that it points at the power groups.
4. Return `sub_07425c(loc, band == 0 ? 2 : 3, &d[1])`.

#### sub_0748c3 (.text+0x0748c3, name assigned: `clm_channel_in_range`)

`bool clm_channel_in_range(uint32 chan, const uint8 range[2], const comb
*c, uint32 other)`; range = (first, last).

1. If `c->count == 0`: return `chan == first && other == last` (with no
   comb a range is a literal channel pair; `other` is the second 80 MHz
   segment of an 80+80 query, 0 from every other caller).
2. If not `first <= chan <= last` (signed int compares): return false.
3. t = the first triple (in list order) with `t.first <= first <= t.last`
   and `(first - t.first) % t.step == 0`; none: return false.
4. While `chan > t.last`: t = the triple with the smallest `first` greater
   than `t.last`; none: return false.
5. Return `chan >= t.first && (chan - t.first) % t.step == 0`.

Callers pass `other` = `params->other_80_80` (`clm_limits`, read in the
disassembly: the decompiler omits this argument) or 0 (all others).

#### sub_07473a (.text+0x07473a, name assigned: `clm_get_channel_set`)

`void clm_get_channel_set(uint8 vec[25], const uint8 *set_list, uint8 n,
const uint8 *ranges, const comb *c)`: channel bit vector (bit `ch & 7` of
byte `ch >> 3`, channels 0..199).

1. If vec is NULL return. Zero the 25 bytes.
2. If set_list is NULL or n == 0xff return. Skip n length-prefixed sets;
   S = the set reached.
3. For each byte r of S:
   * r == 0xff: for each triple of c: set every channel `t.first,
     t.first+t.step, ... <= t.last`.
   * else: (first, last) = `ranges[2r], ranges[2r+1]`; t = first triple
     (list order) containing first on its grid (as in `sub_0748c3`); none:
     next r. Then `ch = first` and loop: set bit ch; stop if `ch >= last`;
     if `ch < t.last`: `ch += t.step`; else t = the triple with the
     smallest `first > ch`, stop if none or `t.first > last`, `ch =
     t.first`.

(A range whose last channel is off the grid can set one channel above
`last`; the data has none - run: all channel vectors matched the model.)

### Power limits

#### clm_limits (.text+0x0755d0, name original)

`int clm_limits(const struct clm_country_locales *loc, uint32 band,
uint32 channel, int ant_gain, uint32 limits_type, const struct
clm_limits_params *p, int8 limits[336])`.

Purpose: the transmit power limit of every rate for one channel (or one
sub-channel of a wide channel) of the country whose locales are `loc`.

Steps:

1. Argument check, before anything is written: return 1 if limits, loc or
   p is NULL, band >= 2, limits_type >= 15, `p->bw` >= 5 or
   `p->category` >= 4.
2. **Channel numbers per bandwidth level.** `code` = limits_type code
   (.rodata+0x284cf0); `lev` = `p->bw`, but 2 if `p->bw` == 4 (80+80 is
   handled as 80); `chans[5] = {0}` (bytes); `half = 2 << lev`;
   `m = 1 << (code >> 4)`; `c = channel`. Loop:
   * `m >>= 1`; `chans[lev] = c` (low 8 bits);
   * if m == 0: leave the loop;
   * `lev -= 1`; if lev == -1: return 1 (the type asks for a deeper split
     than the bandwidth allows, e.g. a sub-channel of a 20 MHz channel);
   * `half >>= 1`; `c += (m & code) ? half : -half`.

   So `chans[l]` is the channel number at level l (0 = 20 MHz, 1 = 40,
   2 = 80, 3 = 160) on the path selected by the type. Example (computed by
   the model that reproduces the object): 80 MHz channel 42, type 4
   (`LU`): chans[2] = 42, chans[1] = 38, chans[0] = 40.
3. `dsss_chan = chans[0]` if band == 0 and `p->bw` != 0 and chans[0] != 0,
   else 0. (2.4 GHz sub-channel queries of a 40 MHz channel: the DSSS rates
   then take the limits of the 20 MHz sub-channel.)
4. Fill limits[0..335] with 0x81 (-127, "not set").
5. `sub_07444e(loc, band, d[2], NULL)`; if it fails return 1 (limits stay
   0x81).
6. **Sub-channel rule sets** of the country's set `cs = loc->set & 1`:
   `rules80 = rules160 = {0, NULL}`. If `f = loc->flags & 0x47` is not 0:
   `i = ((f & 7) | ((f >> 3) & 8)) - 1`;
   * `T = rptr(cs, 0x70)`; if T and `i < T.count` (unsigned): item = the
     16-byte item i of T: `rules80 = {item.n (u32 at +0), relocated item
     ptr (+8)}`;
   * if `S[cs].has_160_rules`: the same with `rptr(cs, 0x78)` for
     `rules160`.
7. `found = false`. For each of the two locale definitions L = d[0] (base,
   pointing at its power groups) and d[1] (HT); skip L if `L.record` is
   NULL:
   1. `lc[5] = {0}` (channel to match per bandwidth code of a group),
      `ch = channel & 0xff`:
      * limits_type == 0: `lc[p->bw] = ch` (for bw 4 that is `lc[4]`);
      * else by `p->bw`: 1: `lc[1] = ch` (a sub-channel of a 40 MHz channel
        uses the 40 MHz records of the channel itself); 2: `sub_07512f(lc,
        chans, limits_type, channel, S[cs].ranges[2], &S[cs].comb[band][2],
        rules80, stride 7)`; 4: the same, then `lc[4] = lc[2]`,
        `lc[2] = 0`; 3: `sub_07512f(..., S[cs].ranges[3],
        &S[cs].comb[band][3], rules160, stride 15)`.
   2. For each power group of L (flags F, optional F2, record width w,
      N records):
      * `b` = bandwidth map[F & 0x49]; `comb = L.comb[b]`; `lch = lc[b]`;
        `ranges = L.ranges[b]`;
      * power column `col = (w == 3) ? 0 : {0,3,4,5}[p->category]`;
      * rate set list: if F2 bit 1: `L.ext_ratesets[b]` with rate offset
        104; else `L.ratesets[b]`, offset 0;
      * for each record r (bytes r[0..w-1]); `rng = ranges + 2*r[1]`:
        * if `col < w`:
          - `v = r[col]`; if v != 0x80: if F bit 1, `v = (v - ant_gain) &
            0xff` (8-bit arithmetic); v as signed byte; if `p->sar < v`
            (signed int compare) `v = (int8)p->sar`. If v == 0x80: v = -128
            (no gain, no clamp);
          - if `lch != 0` and `sub_0748c3(lch, rng, comb, p->other_80_80)`:
            for each rate x of rate set r[2]: `i = x + offset`; if
            `dsss_chan == 0` or `rate_class[i] != 0`: **apply** v to
            limits[i]. Then if v != -128: `found = true`;
          - if b == 0 and `dsss_chan != 0` and `sub_0748c3(dsss_chan, rng,
            L.comb[0], 0)`: for each rate x of the set with
            `rate_class[x + offset] == 0` (DSSS): apply v.
        * else (the record has no power for this category): if !found and
          `lch != 0`: if `sub_0748c3(lch, rng, comb, p->other_80_80)` then
          `found = (r[0] != 0x80)`.

   "Apply v to limits[i]": `cur = limits[i]`; if `cur == -127` or
   (`v < cur` and `cur != -128`): `limits[i] = v`. So the result is the
   minimum of all matching records, and -128 (disabled) once set can not be
   raised again.
8. If found: replace every remaining 0x81 (and 0x80) by 0x80 and return 0.
   Else return -1 (the channel is not valid for the country; limits keep
   0x81 except entries set to -128).

Notes:
* The base and HT locales of a band contribute together (the base locale
  usually covers DSSS/OFDM, the HT locale MCS/VHT rates); a rate covered by
  both gets the minimum.
* Units: the database's (quarter dBm in Broadcom's convention:
  unverified); `ant_gain` and `p->sar` must be in the same unit.
* In the emulated session (run, `re-out\analysis\clm\sesslog.txt`:
  attach, up, channels 36, 36/80, 6, 149/40): country XW/0, 70 calls, all
  with `ant_gain` 0 and params {bw, 127, 0, 0}. At attach a sweep over the
  2.4 GHz channels 3..9 (bw 1) and the 5 GHz 40/80 MHz channels (38 .. 163
  with bw 1 and 2; several answer -1, e.g. channel 42 with bw 1 and channel
  58 with bw 2 for XW); at up and at each channel change the channel itself
  (type 0), for 36/80 also channel 42 bw 2 with types 1 (`SUBCHAN_L`) and 3
  (`LL`) and the 40 MHz channels 38/46, for 149/40 also type 1. All
  results (return code and the 336 values) of these calls are reproduced
  by the model (`replay.py`).
* 80+80 (bw 4): the groups with bandwidth code 4 are matched with the 80 MHz
  range table and `comb[band][4]`, which is empty in this object, so such a
  record matches only if its range is exactly (channel, `other_80_80`)
  (read). The database's 80+80 records indeed use pair ranges such as
  (42, 106), (106, 42), (58, 106) (6 groups, e.g. in US/150 and "#a"/0).
  Run: US/150, bw 4, type 0: channel 42 with other 106, 106 with other 42,
  58 with other 106 give limits (166 enabled rates, identical to the
  model); 42 with other 0 or 58 gives -1. US/0 and XW/0 have no 80+80
  records (bw 4 type 0 always -1), while their sub-channel types (1, 3)
  of bw 4 return limits through the 80 MHz rules.
* Cost: two `sub_07425c` walks plus a linear pass over both locales'
  groups for each call; the driver calls it once per (channel, bandwidth)
  when it computes a channel's power targets.

#### sub_07512f (.text+0x07512f, name assigned: `clm_apply_subchan_rule`)

`void clm_apply_subchan_rule(uint8 lc[5], const uint8 chans[5], uint32
limits_type, uint32 channel, const uint8 *ranges, const comb *c, const uint8
*rules, uint32 n, uint32 stride)`.

For each of the n rules (stride bytes each) in order: if
`sub_0748c3(channel, ranges + 2*rule[0], c, 0)`: `mask = rule[limits_type]`;
for i in 0..4: if mask bit i: `lc[i] = chans[i]`; return. No matching rule:
lc unchanged (all 0: the query then finds no limits and `clm_limits`
returns -1).

So a rule says, for one range of 80 (or 160) MHz channels and for each
sub-channel type, which bandwidth levels' power records limit that
sub-channel: e.g. a mask 0x3 for type `LU` of channel 42 means "the 20 MHz
records of channel 40 and the 40 MHz records of channel 38 apply". (Read;
the mask values of the session's rule set were exercised by the 80/160 MHz
cases of the comparison runs.)

#### clm_regulatory_limit (.text+0x076356, name original)

`int clm_regulatory_limit(const struct clm_country_locales *loc, uint32
band, uint32 channel, uint32 *limit)`.

1. Return 1 if band >= 2, loc NULL or limit NULL.
2. `sub_07425c(loc, band, &d)` (base locale of the band); return 1 if it
   fails or `d.record` is NULL (no locale).
3. For each of the n = record[3] pairs (limit, range) at record+4: if
   `sub_0748c3(channel, d.ranges[0] + 2*range, d.comb[0], 0)` (20 MHz
   ranges and comb): `*limit = limit byte` (zero-extended), return 0.
4. Return -1 (channel not in any regulatory range; *limit untouched).

Caller: `wlc_get_reg_max_power_for_channel` (not reached in the emulated
session). Run: 2.4 GHz channels 0..15 and 200, 5 GHz 20 MHz channels, for
every distinct locale set (see Verification).

#### clm_limits_params_init (.text+0x07520e, name original)

`int clm_limits_params_init(struct clm_limits_params *p)`: if p is not
NULL write `bw = 0`, `category = 0`, `sar = 0x7f`, `other_80_80 = 0` (in
this order); return 1 if p is NULL, else 0.

### Countries

#### sub_07657e (.text+0x07657e, name assigned: `clm_get_table_item`)

`void *clm_get_table_item(int k, uint32 root_off, int stride, int i, int
*count)`. `T = rptr(k, root_off)` (a {count, ptr} table). If T is NULL:
`*count = -1` (if count is not NULL), return NULL. Else `*count = T.count`
(if not NULL); if `i < T.count` (signed) return `relocated T.ptr +
i*stride` (NULL + i*stride if T.ptr is 0), else NULL.

#### sub_0765ec (.text+0x0765ec, name assigned: `clm_get_country_rec`)

`const uint8 *clm_get_country_rec(int k, int i, int *count)` =
`sub_07657e(k, 0x50, S[k].country_size, i, count)`.

#### sub_07502a (.text+0x07502a, name assigned: `clm_get_country_ccrev`)

`void clm_get_country_ccrev(int k, const uint8 *rec, struct clm_ccrev
*out)`; `struct clm_ccrev { char cc[2]; uint16 rev; }` (4 bytes, assigned).
`out->cc = rec[0..1]`; `out->rev = rec[2]`; then if `S[k].rev_hi_idx < 0`:
if rec[2] == 0xff `out->rev = 0xffff`, else if `S[k].rev_remap` is not
NULL `sub_074f7b(k, out)`; otherwise `out->rev = rec[7 + rev_hi_idx] << 8 |
rec[2]`.

#### sub_074f7b (.text+0x074f7b, name assigned: `clm_remap_rev`)

`void clm_remap_rev(int k, struct clm_ccrev *ccrev)`: with the remap table
R = `S[k].rev_remap` (format above): find the first of the R.n country
entries whose two characters equal `ccrev->cc`; none: return. Scan its
3-byte entries `first(this) .. first(next)-1`: the first entry whose byte 2
equals `ccrev->rev` (the 8-bit revision, compared as 16-bit values) sets
`ccrev->rev = byte0 << 8 | byte1`; none: unchanged. (Read only: no remap
table in this object.)

#### sub_07508d (.text+0x07508d, name assigned: `clm_get_country_locale_idx`)

`uint32 clm_get_country_locale_idx(int k, const uint8 *rec, int t)`: the
locale index of type t with the special values, exactly as given under
"Country record" (8, 10 or 12 bits depending on `loc_hi1_idx` /
`loc_hi2_idx` of set k).

#### sub_076c6c (.text+0x076c6c, name assigned: `clm_find_country`)

`const uint8 *clm_find_country(int k, const struct clm_ccrev *key, int
*index)`: for i = 0, 1, ...: `rec = sub_0765ec(k, i, NULL)`; NULL: return
NULL; decode it (`sub_07502a`); if cc and rev both equal the key: store i
in *index (if not NULL) and return rec. Linear search; records with a
deleted locale are found too.

#### clm_country_lookup (.text+0x076ce8, name original)

`int clm_country_lookup(const char cc[2], uint32 rev, uint32 *country_id)`.

1. Return 1 if cc or country_id is NULL.
2. If cc[0] and cc[1] are both 0, use "ww". key = {cc[0], cc[1], rev &
   0xffff}.
3. `rec = sub_076c6c(0, key, &i)`, k = 0; if NULL: `rec = sub_076c6c(1,
   key, &i)`, k = 1; if still NULL return -1.
4. If `sub_07508d(k, rec, 0) == 0xffd` (deleted) return -1.
5. `*country_id = mkid(k, i)`; return 0.

Run: all 5388 records of the database (via `clm_country_iter`), "ww" and
"" (both -1 in this object), "ZZ" (-1); XW/0 gives 0x40001495, US/0
0x40001225.

#### clm_country_def (.text+0x076f00, name original)

`int clm_country_def(uint32 country_id, struct clm_country_locales *loc)`.

1. Return 1 if loc is NULL.
2. `(k, i) = split(country_id)`; `rec = sub_0765ec(k, i, NULL)`; NULL:
   return -1.
3. `base = NULL`. For each (offset, t) of .rodata+0x284d00 - t = 0..3,
   offset 4t:
   * `v = sub_07508d(k, rec, t)`, `vset = k`;
   * if v == 0xffe: if base is NULL: `base = sub_076c6c(1, ccrev of rec,
     NULL)` (the same cc/rev in the base set); `v = base ? sub_07508d(1,
     base, t) : 0xfff`; `vset = 1`;
   * `loc->locale[t] = mkid(vset, v)` (so "no locale" is stored as index
     0xfff).
4. `loc->flags = S[k].flags_idx >= 0 ? rec[7 + flags_idx] : 0`;
   `loc->set = k`. Bytes 0x12..0x13 are not written. Return 0.

Run: all 5388 countries; XW/0 (session) gives locales {0x400002f8,
0x40000364, 0x40000344, 0x40000385}, flags 0x22, set 1.

#### clm_country_iter (.text+0x076da8, name original)

`int clm_country_iter(uint32 *iter, char cc[2], uint32 *rev)`; *iter must
start at 0 (`clm_iter_init`).

1. Return 1 if any argument is NULL.
2. Position: if *iter == 0: (k, i) = (0, 0); else `(k, i) = split(*iter)`
   and i += 1 (continue after the last returned country).
3. Loop:
   * `rec = sub_0765ec(k, i, &n)`; if NULL: if k == 0: (k, i) = (1, 0),
     continue; else: `*iter = mkid(1, n >= 0 ? n : 0)` (n = the base
     set's country count, -1 if it has no country table), return -1;
   * decode cc/rev of rec; if `sub_07508d(k, rec, 0) == 0xffd`: skip;
   * if k == 1 and `S[0].root` is not NULL: search set 0 for the same
     cc/rev (linear, `sub_0765ec` + `sub_07502a`); found: skip (the
     incremental record replaces or deletes it);
   * else (not skipped): `cc = rec cc`, `*rev = rev`, `*iter = mkid(k, i)`,
     return 0;
   * skip: i += 1.

Callers: `wlc_get_country_list`, `sub_07765c` (not reached in the session).
Run: the object's iteration over this database yields 5388 countries then
-1 with *iter = 0x4000150d, identical to the model; also with the same data
loaded as incremental set (all from set 0, base entries skipped).

#### clm_country_advertised_cc (.text+0x077022, name original)

`int clm_country_advertised_cc(uint32 country_id, char cc[2])`.

1. Return 1 if cc is NULL, or if the country record `split(country_id)`
   does not exist.
2. key = cc/rev of the record (`sub_07502a`).
3. For k = 0, 1: for each item j of the advertised-cc table
   `sub_07657e(k, 0x58, 16, j, NULL)` (until NULL): with n = s32 at +4 and
   list = ptr at +8 relocated by S[k]'s delta:
   * skip the item if n == -1;
   * if k == 1 and `S[0].root` is not NULL: skip the item if set 0's
     advertised-cc table has an item with the same two characters;
   * for r = 0 .. n-1: `sub_076611(k, out, list, r)`; if out's cc and rev
     equal key: `cc = item's cc[0..1]`, return 0.
4. `cc = key.cc` (the country advertises its own code); return 0.

Caller: `wlc_set_countrycode_rev` (run in the session: XW/0 -> "XW").
Run: all 5388 countries identical to the model.

#### clm_country_flags (.text+0x075343, name original)

`int clm_country_flags(const struct clm_country_locales *loc, uint32 band,
uint64 *flags)`.

1. Return 1 if flags or loc is NULL or band >= 2.
2. `*flags = 0xe6000000` (64-bit store).
3. `sub_07444e(loc, band, d[2], &bf)`; if it fails return 1 (flags keep
   0xe6000000).
4. `*flags |= bf & 3` (DFS type 1, 2 or 3); if bf bit 2: `*flags |= 4`.
5. For L = d[0], d[1] with a non-NULL record, for each power group (F, F2,
   w, N records): `b` = bandwidth map[F & 0x49]; `bwbit` = 0x40000000 for
   b = 1, 0x80000000 for b = 2, 0x04000000 for b = 3, 0x02000000 for b = 4,
   0 for b = 0; if w != 3: `*flags |= 0x10`; rate set list and offset as in
   `clm_limits` (F2 bit 1: extended, +104); for each record r (in order):
   * if `r[0] != 0x80` (category-0 power): if bwbit: `*flags &= ~bwbit`;
     then for each rate x of rate set r[2], class c = `rate_class[x +
     offset]`: c == 1 and F bit 1: `*flags |= 0x08000000`; c == 2:
     `*flags &= ~0x20000000`; c == 0 and F bit 1: `*flags |= 0x10000000`;
     then bwbit = 0 (the bandwidth bit is cleared at most once per group,
     which makes no difference to the result).
   * records with power 0x80 contribute nothing.
6. From the country flags byte `loc->flags`: bit 3 -> `*flags |= 0x08`;
   bit 4 -> `|= 0x20`; bit 5 -> `|= 0x40`.
7. Return 0.

Run: session XW/0: band 0 -> 0x9e000040, band 1 -> 0x06000040 (both bands
for every distinct locale set identical to the model).

#### clm_country_channels (.text+0x07523b, name original)

`int clm_country_channels(const struct clm_country_locales *loc, uint32
band, uint8 valid[25], uint8 restricted[25])`.

1. Return 1 if band >= 2 or loc is NULL. If both vectors are NULL return 0
   without doing anything. If only `valid` is NULL, a local 32-byte buffer
   is used in its place.
2. `sub_07425c(loc, band, &d)` (base locale of the band); fails: return 1.
3. If `d.record` is NULL: zero both vectors (each only if not NULL), return
   0.
4. `sub_07473a(valid, d.valid_sets, record[0], d.ranges[0], d.comb[0])`;
   `sub_07473a(restricted, d.restricted_sets, record[2], d.ranges[0],
   d.comb[0])`; if restricted is not NULL: `restricted[j] &= valid[j]` for
   j = 0..24.
5. Return 0.

Run: session XW/0: 2.4 GHz valid = channels 1..11, restricted = none;
5 GHz valid = 36, 40, 44, 48, 149, 153, 157, 161, 165, restricted = the same
nine channels; every distinct locale set identical to the model.

### Aggregates and region lists

#### sub_076611 (.text+0x076611, name assigned: `clm_get_region_ccrev`)

`void clm_get_region_ccrev(int k, struct clm_ccrev *out, const uint8 *list,
int i)`: element i of a region list of set k (formats under "Region
lists"):

1. fmt = `S[k].region_fmt`. fmt 0: `e = list + 3*i`, go to step 4. fmt > 2:
   return (out not written).
2. `x = (fmt == 1) ? list[i] : u16 at list + 2*i`; `rec =
   sub_0765ec(k, x, &n)`; if rec: `sub_07502a(k, rec, out)`, return.
3. `e = sub_07657e(k, 0xe8, S[k].rev_hi_idx >= 0 ? 4 : 3, x - n, NULL)`;
   if `rev_hi_idx >= 0`: copy the 4 bytes at e into out (cc[2], u16 rev),
   return (no NULL check on e in this case).
4. If e is NULL return. `out->cc = e[0..1]`, `out->rev = e[2]`; if e[2] ==
   0xff `out->rev = 0xffff`; else if `S[k].rev_remap`: `sub_074f7b(k,
   out)`.

#### sub_076708 (.text+0x076708, name assigned: `clm_get_aggregate`)

`bool clm_get_aggregate(int k, int i, struct clm_agg *out, int *count)`;
`struct clm_agg { struct clm_ccrev id; int32 n; uint64 list; }` (16 bytes,
assigned; list NOT relocated). `e = sub_07657e(k, 0x60, 16, i, count)`;
NULL: return false. If `S[k].region_fmt == 0`: `sub_076611(k, &out->id, e,
0)` (the first 3 bytes are an inline region); else `out->id` = the u32 at e.
`out->n` = s32 at e+4, `out->list` = u64 at e+8. Return true.

#### sub_076781 (.text+0x076781, name assigned: `clm_find_aggregate`)

`bool clm_find_aggregate(int k, const struct clm_ccrev *key, int *index,
struct clm_agg *out)`: for i = 0, 1, ...: `sub_076708(k, i, out, NULL)`;
false: return false; if out's cc (as one u16) and rev equal key's: store i
in *index (if not NULL), return true.

#### sub_07699b (.text+0x07699b, name assigned: `clm_agg_find_region`)

`bool clm_agg_find_region(int k, const struct clm_agg *agg, const char
cc[2], struct clm_ccrev *out)`: return false if agg is NULL or agg->list is
0. `list = agg->list + S[k].delta`. If out is NULL a local buffer is used.
For r = 0 .. agg->n - 1: `sub_076611(k, out, list, r)`; if out's two
characters equal cc: return true. Return false.

#### clm_agg_country_lookup (.text+0x0767f2, name original)

`int clm_agg_country_lookup(const char cc[2], uint32 rev, uint32 *agg_id)`.

1. Return 1 if agg_id or cc is NULL. Empty code -> "ww". key = {cc, rev &
   0xffff}.
2. `sub_076781(0, key, &i, &a)` (k = 0); not found: `sub_076781(1, key,
   &i, &a)` (k = 1); not found: return -1.
3. If `a.n == -1` (deleted) return -1.
4. `*agg_id = mkid(k, i)`; return 0.

Run: the session calls it twice with "ww"/0 (no aggregate: -1). Every
aggregate's own cc/rev looked up (compare.py aggs) identical to the model.

#### clm_agg_country_map_lookup (.text+0x076a3f, name original)

`int clm_agg_country_map_lookup(uint32 agg_id, const char cc[2], uint32
*rev)`: the revision that aggregate `agg_id` prescribes for country cc.

1. Return 1 if rev or cc is NULL. Empty code -> "ww".
2. `(k, i) = split(agg_id)`; `sub_076708(k, i, &a, NULL)` (result not
   checked: an invalid id leaves `a` undefined).
3. Loop:
   * if `sub_07699b(k, &a, cc, &r)`: if `r.rev != 0xffff`: `*rev = r.rev`,
     return 0; else return -1 (the country is explicitly excluded);
   * if k == 1: return -1;
   * k = 1; `sub_076781(1, &a.id, NULL, &a)` (the aggregate with the same
     cc/rev in the base set); not found: return -1; repeat.

So an incremental aggregate lists only changed countries; others are taken
from the base aggregate of the same name.

Caller: `sub_06f468` (wlc_channel.c, country code + regrev resolution; not
reached with a result in the session because "ww"/0 has no aggregate).

#### clm_get_base_app_version_string, clm_get_inc_app_version_string (.text+0x076412, +0x076463, names original)

`const char *clm_get_base_app_version_string(void)` (set 1) and
`clm_get_inc_app_version_string(void)` (set 0): with `F = S[k].root ?
root->flags : 0`: if F bit 1 is set and the string at `S[k].header + 0x5e`
differs from "Broadcom-0.0" (byte compare up to the first NUL), return that
string; else NULL. Caller: `wlc_dump_clmver`. Run: base -> NULL (this
object's string is "Broadcom-0.0"); incremental (no set) -> NULL.

### Functions without callers in the object (U)

Described briefly; an open implementation does not need them.

* `clm_get_string` (.text+0x0764b4): `const char *clm_get_string(int
  which, int src)`; src 0 = base, 1 = incremental, 2 = incremental if loaded
  else base, >= 3: NULL; with `h = S[k].header` and the set loaded: which 0
  -> h+0x0e, 1 -> h+0x22, 2 -> h+0x40, 3 -> h+0x5e (only if root flags bit
  1, and NULL if it equals "Broadcom-0.0"), 4 -> `rptr(k, 0xc0)` (only if
  root flags bit 6); other values NULL. For 0..2 and 4 an empty string gives
  NULL. (Read.)
* `clm_channels_params_init` (.text+0x07602c): `int (uint32 p[2])`: p[0] =
  p[1] = 0; result 1 if p is NULL.
* `clm_valid_channels` (.text+0x07604b): `int (loc, band, const uint32
  p[2] /* bw, this_80_80 */, uint8 vec[25])`: checks band < 2, p[0] < 5 and
  `(p[0] == 4) == (p[1] != 0)`; zeroes vec; for every group of both band
  locales whose bandwidth code equals p[0] and every record with power !=
  0x80: if p[1] == 0, sets every comb channel of that bandwidth inside the
  record's range (`sub_0748c3`); else, if the range's first channel equals
  p[1], sets the range's last channel. Returns 0 if any bit was set, else
  -1; 1 on bad arguments.
* `clm_valid_channels_5g` (.text+0x075d9c): `int (loc, uint8 v20[25],
  uint8 v40plus[25])` on band 1: like the above for all bandwidths, 20 MHz
  groups into the first vector, 40/80/160 into the second (80+80 groups
  skipped), channel 0 never set; result 0, or 1 on bad arguments.
* `clm_agg_country_iter` (.text+0x0768a7): iterator over aggregates of
  both sets like `clm_country_iter` (skips deleted ones and base aggregates
  present in set 0); outputs cc and rev.
* `clm_agg_map_iter` (.text+0x076afc): iterator over the regions of one
  aggregate (incremental entries first, then base entries not overridden);
  outputs cc and rev, skips rev 0xffff.

Sanity runs of these (`uncalled.py`): `clm_get_string` gives "8.1.0",
"1.27.6", "ClmImport: 1.29.5", NULL (app version = default), "2015-09-18
15:46:07" for which = 0..4 with src 0 or 2, NULL for src 1 (no incremental
set) and src 3; `clm_valid_channels` for XW/0: 2.4 GHz 20 MHz 1..11, 40 MHz
3..9, 5 GHz 20 MHz 36..48 + 149..165, 40 MHz 38, 46, 151, 159, 80 MHz 42,
155, 160 MHz none (-1), bw 4 with other 0 -> 1; `clm_valid_channels_5g`:
{36..48, 149..165} and {38, 42, 46, 151, 155, 159}; `clm_agg_country_iter`
returns the 152 aggregates (first CN/1, DE/1, DE/2, DE/3);
`clm_agg_map_iter` of DE/1: AU/2, ID/5, JP/1, US/1, each equal to
`clm_agg_country_map_lookup`.

## Verification

All checks run on a bare emulator `Machine()` (the CLM needs no card
model): `clm_init(&clm_header)`, then direct calls of the entry points.
Scripts in `re-out\analysis\clm\`: `clmrun.py` (call wrappers), `model.py`
(an independent Python implementation written from this specification,
reading the database bytes), `compare.py`, `craft.py`, `replay.py`,
`edge.py`, `sesslog.py` (call log of a session). Results:

| Check | What | Result |
|---|---|---|
| sesslog.py 36 36/80 6 149/40 | every CLM call of attach, up and four channel changes with arguments and results (`sesslog.txt`): 1 `clm_init`, 3 `clm_country_lookup`, 2 `clm_agg_country_lookup`, 1 `clm_country_advertised_cc`, 67 `clm_country_def`, 13 `clm_country_flags`, 4 `clm_country_channels`, 64 `clm_limits_params_init`, 70 `clm_limits` | session country XW/0 (empty `ccode` -> lookups of "", US/0, then XW/0) |
| replay.py | the 70 `clm_limits` calls of the session, object vs model, full 336-byte arrays | 0 differences; results equal the logged ones |
| compare.py iter | `clm_country_iter` over the whole database; for each of the 5388 countries `clm_country_lookup`, `clm_country_def`, `clm_country_advertised_cc` | 0 differences |
| compare.py one XW 0 | XW/0: `clm_country_flags` and `clm_country_channels` (both bands), `clm_regulatory_limit` (all channels + 0, 15, 200), `clm_limits` for 2.4 GHz 20 and 40 MHz (types 0..2), 5 GHz 20/40/80/80+80/160 MHz with every valid type, each with three parameter sets (ant_gain 0/8/-6, sar 127/60, category 0/1/3) | 0 differences in 220 x 3 cases |
| compare.py locales | the same for every distinct locale set of the database (1934 sets used by the 5388 countries): flags, channels and regulatory limits always; `clm_limits` with the full case list for the first 25 sets and for XW, US, XA, JP, DE, CN, KR at revision 0, a random sample of 40 cases (x 3 parameter sets) for the others | 0 differences in all 1934 sets (82,940 `clm_limits` cases x 3 parameter sets, plus flags, channel vectors and regulatory limits of both bands) |
| compare.py aggs | `clm_agg_country_lookup` for all 152 aggregates and `clm_agg_country_map_lookup` of each with 234 country codes | 0 differences (35,568 map lookups) |
| compare.py ... --inc | the database loaded a second time as incremental set (`clm_set_inc_data(&clm_header)`): iteration, lookups, defs, advertised cc, 304 aggregates / 71,136 map lookups, XW/0 limits | 0 differences |
| craft.py 0..3 | crafted incremental sets in the emulator heap: overriding country, locale 0xffe (inherit from base, also without a base record), 0xffd (deleted), 0xfff (no locale), 16-bit revision, aggregate override / deletion / fallback to the base aggregate, advertised-cc override and deletion; variant 1 without comb tables and 160 MHz rules (built-in default combs); variant 2 with root flags bit 16 clear + bit 0 (9-byte records, inline regions); variant 3 old format 5.1 (7-byte records); variant 4 a hand-made locale with 4- and 6-byte records (see below) | 0 differences (also the slot comb tables) |
| edge.py | argument checks of all entry points (outputs untouched), `clm_limits` with invalid/absent locale and invalid channel, failed loads, relocation delta | as described in the procedures |

Coverage of this database (scan of the 4357 locales used by the 5388
countries with the model): every power record is 3 bytes wide (so for
categories 1..3 `clm_limits` always takes the "no column" path: the
channel is valid but all rates are disabled - run with category 1 and 3);
group flags present: bandwidth codes 0x00, 0x01, 0x08, 0x09 (3 groups),
0x48 (3), with and without bit 1 (antenna gain) and bit 7 (F2); F2 is 0 or
2 (1942 groups use the extended rate sets); 530 of 46,181 records are
disabled (0x80); base locale flags bytes 0, 1, 2, 4; 1 to 10 groups per
locale. So all branches of `clm_limits` except w > 3 with a present column
are exercised by the database itself; that one is covered by `craft.py 4`
(a hand-made 5 GHz HT locale with a 4-byte group chained to a 6-byte
"EIRP" group, disabled entries in category columns; 9 channels x 4
categories x 4 (ant_gain, sar) pairs = 144 cases, 0 differences; e.g.
channel 36, category 1, ant_gain 10: OFDM 44 from the 4-byte group's
column 3, MCS 66 - 10 = 56 from the 6-byte group; channel 52, category 2:
result 0 with every rate disabled, because the 4-byte group has no column 4
but its category-0 power is enabled; `clm_country_flags` 0xce000010, equal
to the model).

Not run: the revision remap table (no data with root flags bit 25), the
extra region table (root+0xe8, empty in this object), formats 8..15, the
functions without callers (U). `clm_regulatory_limit` and
`clm_get_*_app_version_string` are not reached in the emulated session but
were called directly.

The guide's whole-session `hybrid.py check` was not needed for this layer:
the CLM makes no bus access, so its correctness shows only in its results,
which the comparisons above check directly.

## Open questions

1. Units: the power bytes and `ant_gain`/`sar` are treated as plain signed
   numbers by the code; quarter dBm (and dBm for the regulatory limits:
   XW/0 gives 20 on 2.4 GHz channel 1 and 17 on 5 GHz channel 36) is
   Broadcom's convention but not proven here.
2. Meaning of the four power columns (`params.category`): the code only
   selects byte 0, 3, 4 or 5 of a record. The session always uses 0.
3. Names of the flag bits of `clm_country_flags` and of the country flags
   bits 4/5 (copied to 0x20/0x40): derived from what sets them; the
   driver-side meaning is outside this layer.
4. Root flags bit 24 (slot+0x1a8) and the root fields 0x120..0x160 are not
   used by this version of the code.
5. `clm_agg_country_map_lookup` with an invalid aggregate id uses an
   uninitialised stack structure (the result of `sub_076708` is not
   checked); an implementation should return -1.
6. Stale state on reload: `sub_0744d5` with k == 1 leaves comb entries
   without data unchanged instead of clearing them, `sub_07499e` leaves
   `ratesets[0][2..4]`, `ext_ratesets` and `rev_remap` unchanged when the
   new data lacks them, and the base set inherits combs from an incremental
   set. After `wlc_handle_clm_dload` reloads a set (`clm_init` can be called
   again with a downloaded blob or `&clm_header`), values of the previous
   data can survive. Reproduce this exactly or document the difference; not
   exercised (the emulated session loads once).
