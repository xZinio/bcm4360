# The per-rate transmit power container `ppr_t` (`wlc_ppr.c`)

`wlc_ppr.c` is a pure data library: a container of per-rate power values (signed
bytes, quarter dBm) for one channel bandwidth, plus whole-container arithmetic,
queries, element-wise mapping and a serialised form. It makes no hardware
access and keeps no global state; its only imports are `osl_malloc`,
`osl_mfree`, `osl_memcpy` and `osl_memset`. Users: the PHY's transmit power code
(`wlc_phy_cmn.c`, the SROM decoders), `wlc_channel.c` (regulatory limits),
`wlc_stf.c`, `wlc_olpc_engine.c`, `wlc.c` (iovars).

Status marks: **(run)** = confirmed by calling the object's function in the
emulator and comparing with an independent model (section Verification);
**(read)** = from the code only.

## Scope

All 60 functions of the file (.text 0x164690..0x166188, 6,904 bytes): 50 with
symbols, 10 stripped (names marked *assigned* are mine).

| Function | .text offset | Size | Name |
|---|---|---|---|
| `sub_164690` | 0x164690 | 18 | `ppr_get_bw_powers_20` (assigned) |
| `sub_1646a2` | 0x1646a2 | 31 | `ppr_get_bw_powers_40` (assigned) |
| `sub_1646c1` | 0x1646c1 | 60 | `ppr_get_bw_powers_80` (assigned) |
| `sub_1646fd` | 0x1646fd | 49 | `ppr_get_bw_powers` (assigned) |
| `sub_16472e` | 0x16472e | 98 | `ppr_get_mcs_group` (assigned) |
| `ppr_size` | 0x164790 | 25 | original |
| `ppr_ser_size` | 0x1647a9 | 27 | original |
| `ppr_ser_size_by_bw` | 0x1647c4 | 25 | original |
| `ppr_get_ch_bw` | 0x1647dd | 8 | original |
| `ppr_get_max_bw` | 0x1647e5 | 11 | original |
| `ppr_get_vht_mcs_min` | 0x1647f0 | 114 | original |
| `ppr_set_same_dsss` | 0x164862 | 82 | original |
| `ppr_set_same_ofdm` | 0x1648b4 | 123 | original |
| `ppr_set_same_ht_mcs` | 0x16492f | 95 | original |
| `ppr_set_same_vht_mcs` | 0x16498e | 95 | original |
| `ppr_apply_max` | 0x1649ed | 54 | original |
| `sub_164a23` | 0x164a23 | 70 | `ppr_force_disabled_group` (assigned) |
| `ppr_force_disabled` | 0x164a69 | 513 | original |
| `sub_164c6a` | 0x164c6a | 167 | `ppr_apply_constraint_total_tx_set` (assigned) |
| `ppr_apply_constraint_total_tx` | 0x164d11 | 149 | original |
| `ppr_apply_min` | 0x164da6 | 54 | original |
| `ppr_apply_vector_ceiling` | 0x164ddc | 73 | original |
| `ppr_apply_vector_floor` | 0x164e25 | 73 | original |
| `ppr_get_max` | 0x164e6e | 52 | original |
| `ppr_get_min` | 0x164ea2 | 104 | original |
| `ppr_get_max_for_bw` | 0x164f0a | 51 | original |
| `ppr_get_min_for_bw` | 0x164f3d | 51 | original |
| `ppr_map_vec_dsss` | 0x164f70 | 209 | original |
| `ppr_map_vec_ofdm` | 0x165041 | 269 | original |
| `ppr_map_vec_ht_mcs` | 0x16514e | 168 | original |
| `ppr_map_vec_vht_mcs` | 0x1651f6 | 168 | original |
| `ppr_map_vec_all` | 0x16529e | 526 | original |
| `ppr_cmn_val_minus` | 0x1654ac | 59 | original |
| `ppr_minus_cmn_val` | 0x1654e7 | 75 | original |
| `ppr_plus_cmn_val` | 0x165532 | 55 | original |
| `ppr_multiply_percentage` | 0x165569 | 69 | original |
| `ppr_compare_min` | 0x1655ae | 74 | original |
| `ppr_compare_max` | 0x1655f8 | 74 | original |
| `sub_165642` | 0x165642 | 181 | `ppr_deser_set` (assigned) |
| `sub_1656f7` | 0x1656f7 | 137 | `ppr_deser_cpy` (assigned) |
| `sub_165780` | 0x165780 | 212 | `ppr_ser_set` (assigned) |
| `ppr_serialize` | 0x165854 | 430 | original |
| `ppr_copy_struct` | 0x165a02 | 386 | original |
| `ppr_set_vht_mcs` | 0x165b84 | 96 | original |
| `ppr_set_ht_mcs` | 0x165be4 | 96 | original |
| `ppr_set_ofdm` | 0x165c44 | 122 | original |
| `ppr_set_dsss` | 0x165cbe | 85 | original |
| `ppr_set_cmn_val` | 0x165d13 | 39 | original |
| `ppr_deserialize` | 0x165d3a | 138 | original |
| `ppr_get_vht_mcs` | 0x165dc4 | 114 | original |
| `ppr_get_ht_mcs` | 0x165e36 | 114 | original |
| `ppr_get_ofdm` | 0x165ea8 | 141 | original |
| `ppr_get_dsss` | 0x165f35 | 103 | original |
| `ppr_init_ser_mem_by_bw` | 0x165f9c | 76 | original |
| `ppr_init_ser_mem` | 0x165fe8 | 13 | original |
| `ppr_clear` | 0x165ff5 | 40 | original |
| `ppr_init` | 0x16601d | 53 | original |
| `ppr_delete` | 0x166052 | 32 | original |
| `ppr_create` | 0x166072 | 65 | original |
| `ppr_deserialize_create` | 0x1660b3 | 213 | original |

Not in this file although named `ppr_*`: `ppr_mcs_printf`, `ppr_ofdm_printf`,
`ppr_dsss_printf` (wlc_phy_cmn.c, debug printers, no callers).

## Overview

A `ppr_t` is one heap block: a 32-bit bandwidth word followed by 1, 2 or 3
*sets* of 182 power bytes. A 20 MHz container has one set (20 MHz rates); a
40 MHz container has the 40 MHz set and the "20 in 40" set; an 80 MHz
container has the 80 MHz set, the "20 in 80" set and the "40 in 80" set. Each
set has the same internal layout of 21 rate groups (DSSS, OFDM, MCS by number
of spatial streams, transmit chains and transmit mode).

Every group access goes through two selectors:

1. `ppr_get_bw_powers(ppr, bw)` (`sub_1646fd`) turns a `wl_tx_bw_t` value into
   the address of a set (or NULL) according to the container's own bandwidth.
2. Inside the set, the DSSS and OFDM groups are chosen by small switches on
   `chains`/`mode`; the MCS groups by `ppr_get_mcs_group(set, nss, mode,
   chains)` (`sub_16472e`), a lookup in three `.rodata` tables.

The whole-container operations (`ppr_set_cmn_val`, `ppr_*_cmn_val`,
`ppr_apply_*`, `ppr_compare_*`, `ppr_multiply_percentage`, `ppr_get_max/min`)
do not use the selectors: they walk the data bytes linearly, the count taken
from the size table by the container's bandwidth word.

A byte value of **-128 (0x80) means "rate disabled"** (`WL_RATE_DISABLED`).
`ppr_init`/`ppr_clear` fill with it; the unary arithmetic skips such bytes;
the binary min/max operations and `ppr_apply_max/min` do *not* treat it
specially (it simply is the smallest signed byte), with the consequences
listed per function.

Typical life in a session (`boundary.py wlc_ppr.c` and `watch.py`: 944 calls
into the file over attach, up, four channel changes, watchdog, down/up/down,
detach; 45 containers created and deleted): the PHY's merge `sub_0b8ca4` creates three
containers per run, copies the regulatory limit in, combines them with
`set_cmn_val`/`compare_min`/`minus_cmn_val`/`force_disabled`/`get_max`/
`get_min`/`cmn_val_minus`/`compare_max`, and deletes them; the SROM decoders
fill containers with `ppr_set_dsss/ofdm/vht_mcs`; `wlc_channel_reg_limits`
fills the regulatory container with `ppr_clear` + `ppr_set_*`;
`wlc_update_txppr_offset` (wlc_stf.c) maps the PHY's offsets container into
its own with `ppr_map_vec_all`, then reads groups with `ppr_get_dsss/ofdm`.

## Data

### Enumerations (values as the object uses them; names from `src\shared\bcmwifi\include\bcmwifi_rates.h`)

| `wl_tx_bw_t` | value | | `wl_tx_mode_t` | value |
|---|---|---|---|---|
| `WL_TX_BW_20` | 0 | | `WL_TX_MODE_NONE` (SISO / SDM) | 0 |
| `WL_TX_BW_40` | 1 | | `WL_TX_MODE_STBC` | 1 |
| `WL_TX_BW_80` | 2 | | `WL_TX_MODE_CDD` | 2 |
| `WL_TX_BW_20IN40` | 3 | | `WL_TX_MODE_TXBF` | 3 |
| `WL_TX_BW_20IN80` | 4 | | | |
| `WL_TX_BW_40IN80` | 5 | | | |

`nss` (`wl_tx_nss_t`) is 1..3 and `chains` (`wl_tx_chains_t`) 1..3 as
integers. Values from 6 on of `wl_tx_bw_t` (160 MHz, 80+80, ...) exist in the
header but this object knows nothing about them: no container has a set for
them, and a container bandwidth word >= 3 means "no data" (below).
`ppr_get_max_bw()` returns 2 (80 MHz is the widest).

The object's rate groups are 10 bytes for MCS (VHT 0..9; HT uses the first 8),
and the TXBF groups come **after** all non-TXBF groups. The header in this
repository is a newer 4-chain version (12-byte VHT groups with the
proprietary MCS 10/11, TXBF interleaved per chain count): its `WL_RATE_*`
indices do **not** match the object's byte offsets. Use the table below.

### `ppr_t`

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | `ch_bw`: the container's bandwidth, `wl_tx_bw_t` 0, 1 or 2 (stored as a 32-bit unsigned word) |
| 0x04 | 182 x n | the sets, n = 1 / 2 / 3 for `ch_bw` = 0 / 1 / 2 |

Data size by `ch_bw` (table `.rodata+0x307280`, four 32-bit words
`0xb6, 0x16c, 0x222, 0`; the code indexes it only for `ch_bw` <= 2 and uses 0
otherwise):

| `ch_bw` | data bytes | allocation (`ppr_size`) | serialised maximum (`ppr_ser_size`) |
|---|---|---|---|
| 0 (20 MHz) | 182 (0xb6) | 186 (0xba) | 190 |
| 1 (40 MHz) | 364 (0x16c) | 368 (0x170) | 372 |
| 2 (80 MHz) | 546 (0x222) | 550 (0x226) | 554 |
| >= 3 | 0 | 4 | 8 |

No padding, no alignment requirement beyond what `osl_malloc` gives.

### Sets: which `wl_tx_bw_t` reaches which set

`ppr_get_bw_powers(ppr, bw)` (address relative to the start of the `ppr_t`):

| container `ch_bw` | +0x04 (first set) | +0xba (second set) | +0x170 (third set) | other `bw` |
|---|---|---|---|---|
| 0 (20 MHz) | `bw` = 0 (20) | - | - | NULL |
| 1 (40 MHz) | `bw` = 1 (40) | `bw` = 0 (20) or 3 (20in40) | - | NULL |
| 2 (80 MHz) | `bw` = 2 (80) | `bw` = 0 (20), 3 (20in40) or 4 (20in80) | `bw` = 1 (40) or 5 (40in80) | NULL |
| >= 3 | - | - | - | always NULL |

So in a 40 MHz container "20" and "20in40" are the same set; in an 80 MHz
container "20", "20in40" and "20in80" are one set and "40", "40in80" are one
set. A NULL `ppr` pointer gives NULL. **(run)**

### The 182-byte set

Offsets relative to the start of a set (add 0x04, 0xba or 0x170 for the
offset in the `ppr_t`). "nss x chains" for MCS groups.

| Set offset | Bytes | Group | selected by |
|---|---|---|---|
| 0x00 | 4 | DSSS (1, 2, 5.5, 11 Mbit/s), 1 chain | dsss chains 1 |
| 0x04 | 8 | OFDM (6..54 Mbit/s), 1 chain | ofdm chains 1 (any mode) |
| 0x0c | 10 | MCS 0..9, 1x1 (mode NONE) | nss 1, mode 0, chains 1 |
| 0x16 | 4 | DSSS, 2 chains | dsss chains 2 |
| 0x1a | 8 | OFDM CDD, 2 chains | ofdm chains 2, mode != 3 |
| 0x22 | 10 | MCS 1x2 CDD | nss 1, mode 2, chains 2 |
| 0x2c | 10 | MCS 2x2 STBC | nss 2, mode 1, chains 2 |
| 0x36 | 10 | MCS 2x2 SDM | nss 2, mode 0, chains 2 |
| 0x40 | 4 | DSSS, 3 chains | dsss chains 3 |
| 0x44 | 8 | OFDM CDD, 3 chains | ofdm chains 3, mode != 3 |
| 0x4c | 10 | MCS 1x3 CDD | nss 1, mode 2, chains 3 |
| 0x56 | 10 | MCS 2x3 STBC | nss 2, mode 1, chains 3 |
| 0x60 | 10 | MCS 2x3 SDM | nss 2, mode 0, chains 3 |
| 0x6a | 10 | MCS 3x3 SDM | nss 3, mode 0, chains 3 |
| 0x74 | 8 | OFDM TXBF, 2 chains | ofdm chains 2, mode 3 |
| 0x7c | 10 | MCS 1x2 TXBF | nss 1, mode 3, chains 2 |
| 0x86 | 10 | MCS 2x2 TXBF | nss 2, mode 3, chains 2 |
| 0x90 | 8 | OFDM TXBF, 3 chains | ofdm chains 3, mode 3 |
| 0x98 | 10 | MCS 1x3 TXBF | nss 1, mode 3, chains 3 |
| 0xa2 | 10 | MCS 2x3 TXBF | nss 2, mode 3, chains 3 |
| 0xac | 10 | MCS 3x3 TXBF | nss 3, mode 3, chains 3 |
| 0xb6 | | end | |

Five *chain blocks* are used by the serialiser and by
`ppr_apply_constraint_total_tx`: block A = 0x00..0x15 (22 bytes, 1 chain),
B = 0x16..0x3f (42, 2 chains), C = 0x40..0x73 (52, 3 chains), D = 0x74..0x8f
(28, TXBF 2 chains), E = 0x90..0xb5 (38, TXBF 3 chains).

### `.rodata` tables (facts)

MCS group offsets, 32-bit signed words, -1 = no such group
(`ppr_get_mcs_group`):

| Table | Address | Index | Contents |
|---|---|---|---|
| nss 3 | `.rodata+0x307220` | `mode` (chains must be 3) | `0x6a, -1, -1, 0xac` |
| nss 2 | `.rodata+0x307230` | `mode + (chains-2)*4` (chains 2 or 3) | `0x36, 0x2c, -1, 0x86, 0x60, 0x56, -1, 0xa2` |
| nss 1 | `.rodata+0x307250` | `mode + (chains-1)*4` (chains 1..3) | `0x0c, -1, -1, -1, -1, -1, 0x22, 0x7c, -1, -1, 0x4c, 0x98` |
| sizes | `.rodata+0x307280` | `ch_bw` | `0xb6, 0x16c, 0x222, 0` |

The four tables are contiguous, in this order, so they form one array `F` of
28 words starting at `.rodata+0x307220`; the code computes the index without
checking `mode` (see `sub_16472e`).

Set selector table `.rodata+0x3071f0`, three 16-byte entries {64-bit
bandwidth value, function pointer}: {0, `sub_164690`}, {1, `sub_1646a2`},
{2, `sub_1646c1`}. The dispatcher computes the index itself (switch on
`ch_bw`) and uses only the pointer at entry+8; the first word of each entry
is never read (read: from the disassembly of `sub_1646fd`).

### Serialised form (`ppr_serialize`, `ppr_deserialize*`, `ppr_init_ser_mem*`)

| Bytes | Content |
|---|---|
| 0 | version, 1 |
| 1 | `ch_bw` (low byte of the container's bandwidth word) |
| 2..3 | not written by `ppr_serialize` (keep what the buffer held; after `ppr_init_ser_mem*` they are 0xc0, 0xff) |
| 4..7 | flags, 32-bit **big-endian**; `ppr_serialize` always writes 7 (bytes 00 00 00 07) |
| 8.. | the sets in container order (first set at +0x04, then +0xba, then +0x170), each reduced to the chain blocks the flags select |

Flags: bits 0..1 = `c` (number of chains: 0 or 1 = 1, 2, 3), bit 2 = TXBF.
Per set, in this order: block A always; block B if `c` >= 2; block C if
`c` = 3; block D if TXBF; block E if TXBF and `c` = 3. Bytes per set: 22, 64
or 116 without TXBF; +28 (c <= 2) or +66 (c = 3) with TXBF. With flags 7 a
set is 182 bytes, complete.

A buffer *prepared* by `ppr_init_ser_mem*` starts with the magic bytes
`be ef c0 ff` (the 32-bit word 0xffc0efbe little-endian, i.e. 0xbeefc0ff
read big-endian) followed by flags 7 (00 00 00 07), the rest filled with
0x80. `ppr_serialize` takes its flags from bytes 4..7 when the magic is
present, else uses 7; but whatever flags it used, it writes 7 into the
header (see the procedure).

Error codes (Broadcom `BCME_*`): -2 `BCME_BADARG`, -14 `BCME_BUFTOOSHORT`,
-27 `BCME_NOMEM`, -37 `BCME_VERSION`.

### Callback type of the `ppr_map_vec_*` functions

`void fn(void *ctx, int8 *a_elem, int8 *b_elem)`: called once per rate with
the address of the element in the first container (`a`) and the address of
the same rate's element in the second container (`b`). The functions do not
look at the values; the callback may read and write both. (The only
callback in the object, `sub_172c6c` of wlc_stf.c, reads `*a_elem` and writes
`*b_elem`.)

## Procedures

Notation: `S(p, bw)` = `ppr_get_bw_powers(p, bw)`; `N(p)` = data size of `p`
= table[`p->ch_bw`] if `ch_bw` <= 2, else 0; `data(p)` = `p + 4`. All
arithmetic on power bytes is 8-bit two's complement (results wrap) unless
stated; comparisons are signed unless stated.

### Set and group selectors

#### sub_164690 (.text+0x164690, name assigned: `ppr_get_bw_powers_20`)

`int8 *f(ppr_t *p, wl_tx_bw_t bw)`: `p + 4` if `bw` = 0, else NULL. Reached
only through the table at `.rodata+0x3071f0`.

#### sub_1646a2 (.text+0x1646a2, name assigned: `ppr_get_bw_powers_40`)

`bw` = 1 -> `p + 4`; `bw` = 0 or 3 -> `p + 0xba`; else NULL.

#### sub_1646c1 (.text+0x1646c1, name assigned: `ppr_get_bw_powers_80`)

`bw` = 2 -> `p + 4`; `bw` in {0, 3, 4} -> `p + 0xba`; `bw` in {1, 5} ->
`p + 0x170`; else (including `bw` >= 6, compared unsigned) NULL.

#### sub_1646fd (.text+0x1646fd, name assigned: `ppr_get_bw_powers`)

`int8 *ppr_get_bw_powers(ppr_t *p, wl_tx_bw_t bw)`

1. If `p` is NULL: return NULL.
2. By `p->ch_bw`: 0, 1, 2 -> call the 20/40/80 function above with `(p, bw)`
   unchanged and return its result; any other value -> NULL.

Result table: section Data, "Sets". **(run)**

#### sub_16472e (.text+0x16472e, name assigned: `ppr_get_mcs_group`)

`int8 *ppr_get_mcs_group(int8 *set, wl_tx_nss_t nss, wl_tx_mode_t mode, wl_tx_chains_t chains)`

1. `nss` = 1: if `chains` > 3 (unsigned): NULL. Else `o` = nss-1 table
   [`mode + (chains-1)*4`].
2. `nss` = 2: if `chains` is not 2 or 3: NULL. Else `o` = nss-2 table
   [`mode + (chains-2)*4`].
3. `nss` = 3: if `chains` != 3: NULL. Else `o` = nss-3 table [`mode`].
4. Other `nss`: NULL.
5. If `o` = -1: NULL; else `set + o`.

`set` is not checked for NULL (callers check it). Out-of-range inputs the
object does not guard (no caller passes them; an implementation may return
NULL for them, at the price of not reproducing the object's out-of-bounds
reads): `mode` is not range-checked - the index runs on into the following
words of the array `F` (Data); and `nss` = 1 with `chains` = 0 computes the
index with `chains-1` = 0xffffffff, a read far outside `.rodata` (would fault
in the kernel). **(run** for `nss` 0..4, `chains` 0..4, `mode` 0..7 wherever
the index stays below 24, i.e. inside the three MCS tables - out-of-range
modes included; indices 24 and up (e.g. `nss` 1, `chains` 3, `mode` >= 4)
pick up the size words 0xb6, 0x16c, 0x222 as offsets, outside the set: read
only, undefined**)**

The DSSS and OFDM groups are chosen inline by each function (no helper):

* DSSS: `chains` 1 -> set+0x00, 2 -> set+0x16, 3 -> set+0x40, else none.
* OFDM: `chains` 1 -> set+0x04 (mode ignored); `chains` 2 -> set+0x74 if
  `mode` = 3 (TXBF) else set+0x1a; `chains` 3 -> set+0x90 if `mode` = 3 else
  set+0x44; other `chains` -> none. So any `mode` other than 3 (including
  STBC, NONE and out-of-range values) gives the CDD group.

### Sizes and header

#### ppr_size (.text+0x164790, original)

`uint ppr_size(wl_tx_bw_t bw)`: `4 + (bw <= 2 ? table[bw] : 0)` - the number
of bytes `ppr_create` allocates: 0xba, 0x170, 0x226, else 4. **(run)**

#### ppr_ser_size (.text+0x1647a9, original)

`uint ppr_ser_size(const ppr_t *p)`: `8 + N(p)` (the serialised size with
all blocks: 190, 372, 554, else 8). **(run)**

#### ppr_ser_size_by_bw (.text+0x1647c4, original)

`uint ppr_ser_size_by_bw(wl_tx_bw_t bw)`: `8 + (bw <= 2 ? table[bw] : 0)`.
**(run)**

#### ppr_get_ch_bw (.text+0x1647dd, original)

`wl_tx_bw_t ppr_get_ch_bw(const ppr_t *p)`: returns the 32-bit word at
`p+0`. No NULL check. **(run)**

#### ppr_get_max_bw (.text+0x1647e5, original)

`wl_tx_bw_t ppr_get_max_bw(void)`: returns 2. **(run)**

### Life cycle

#### ppr_create (.text+0x166072, original)

`ppr_t *ppr_create(osl_t *osh, wl_tx_bw_t bw)`

1. `n` = `bw` <= 2 ? table[`bw`] : 0.
2. `p` = `osl_malloc(osh, n + 4)`; if NULL return NULL.
3. `ppr_init(p, bw)`; return `p`.

A `bw` >= 3 creates a 4-byte container with no data (every group access then
fails). **(run)**

#### ppr_init (.text+0x16601d, original)

`void ppr_init(ppr_t *p, wl_tx_bw_t bw)`: fill `n + 4` bytes from `p` with
0x80 (`n` as in `ppr_create`, from the argument `bw`, not from `p`), then
store `bw` as a 32-bit word at `p+0`. Result: all rates disabled. **(run)**

#### ppr_delete (.text+0x166052, original)

`void ppr_delete(osl_t *osh, ppr_t *p)`: `osl_mfree(osh, p, N(p) + 4)`. `p`
is dereferenced first: **no NULL check** (every caller checks). **(run**:
size passed to `osl_mfree`**)**

#### ppr_clear (.text+0x165ff5, original)

`void ppr_clear(ppr_t *p)`: fill the `N(p)` data bytes with 0x80; the
header stays. **(run)**

#### ppr_set_cmn_val (.text+0x165d13, original)

`void ppr_set_cmn_val(ppr_t *p, int8 v)`: fill the `N(p)` data bytes with
`v` (disabled bytes included; `v` = -128 is `ppr_clear`). **(run)**

#### ppr_copy_struct (.text+0x165a02, original)

`void ppr_copy_struct(const ppr_t *src, ppr_t *dst)` - note the order:
source first.

1. If `src->ch_bw` = `dst->ch_bw`: copy `N(src)` data bytes from `data(src)`
   to `data(dst)` (0 bytes if the bandwidth is >= 3). The header is not
   written. Return.
2. Different bandwidths: for `b` in the order 0, 1, 3, 2, 4, 5 (`wl_tx_bw_t`):
   if `S(src, b)` and `S(dst, b)` are both non-NULL, copy 182 bytes from
   `S(src, b)` to `S(dst, b)`. Nothing else changes in `dst` (header, sets
   with no counterpart keep their bytes).

Resulting set copies (source set -> destination set):

| src \ dst | 20 MHz | 40 MHz | 80 MHz |
|---|---|---|---|
| 20 MHz | whole | 20 -> 20in40 | 20 -> 20in80 |
| 40 MHz | 20in40 -> 20 | whole | 20in40 -> 20in80, then 40 -> 40in80, then 20in40 -> 20in80 again |
| 80 MHz | 20in80 -> 20 | 20in80 -> 20in40, 40in80 -> 40, 20in80 -> 20in40 again | whole |

**(run**, all 9 pairs and bandwidths >= 3**)**

### Group get / set

All return the number of bytes transferred (4, 8 or 10) or 0 when the group
does not exist (`S` NULL - also for a NULL `p` - or the selector has no
group). **Getters** fill
the output with 0x80 (disabled) on failure; **setters** write nothing on
failure. The arguments `bw`, `mode`, `nss`, `chains` are 32-bit integers
(System V registers; the 8th argument of the MCS map functions is on the
stack).

#### ppr_get_dsss (.text+0x165f35, original)

`int ppr_get_dsss(const ppr_t *p, wl_tx_bw_t bw, wl_tx_chains_t chains, int8 out[4])`:
`s` = `S(p, bw)`; DSSS group by `chains`; copy 4 bytes to `out`, return 4;
failure: `out[0..3]` = 0x80, return 0. **(run)**

#### ppr_get_ofdm (.text+0x165ea8, original)

`int ppr_get_ofdm(const ppr_t *p, wl_tx_bw_t bw, wl_tx_mode_t mode, wl_tx_chains_t chains, int8 out[8])`:
OFDM group by `chains`/`mode` (Data); copy 8 bytes, return 8; failure: 8 x
0x80, return 0. **(run)**

#### ppr_get_ht_mcs (.text+0x165e36, original)

`int ppr_get_ht_mcs(const ppr_t *p, wl_tx_bw_t bw, wl_tx_nss_t nss, wl_tx_mode_t mode, wl_tx_chains_t chains, int8 out[8])`:
group = `ppr_get_mcs_group(S(p, bw), nss, mode, chains)` (only if `S` is
non-NULL); copy its **first 8** bytes (MCS 0..7), return 8; failure: 8 x
0x80, return 0. **(run)**

#### ppr_get_vht_mcs (.text+0x165dc4, original)

As `ppr_get_ht_mcs` with 10 bytes (MCS 0..9), returns 10 / 0 (10 x 0x80 on
failure). **(run)**

#### ppr_set_dsss (.text+0x165cbe, original)

`int ppr_set_dsss(ppr_t *p, wl_tx_bw_t bw, wl_tx_chains_t chains, const int8 in[4])`:
copy 4 bytes into the DSSS group, return 4; else 0. **(run)**

#### ppr_set_ofdm (.text+0x165c44, original)

`int ppr_set_ofdm(ppr_t *p, wl_tx_bw_t bw, wl_tx_mode_t mode, wl_tx_chains_t chains, const int8 in[8])`:
8 bytes into the OFDM group, return 8; else 0. **(run)**

#### ppr_set_ht_mcs (.text+0x165be4, original)

`int ppr_set_ht_mcs(ppr_t *p, wl_tx_bw_t bw, wl_tx_nss_t nss, wl_tx_mode_t mode, wl_tx_chains_t chains, const int8 in[8])`:
8 bytes into the first 8 entries of the MCS group (entries 8 and 9 keep their
value), return 8; else 0. **(run)**

#### ppr_set_vht_mcs (.text+0x165b84, original)

As `ppr_set_ht_mcs` with 10 bytes, returns 10 / 0. **(run)**

#### ppr_set_same_dsss (.text+0x164862, original)

`int ppr_set_same_dsss(ppr_t *p, wl_tx_bw_t bw, wl_tx_chains_t chains, int8 v)`:
the 4 DSSS bytes := `v`, return 4; else 0. **(run)**

#### ppr_set_same_ofdm (.text+0x1648b4, original)

`int ppr_set_same_ofdm(ppr_t *p, wl_tx_bw_t bw, wl_tx_mode_t mode, wl_tx_chains_t chains, int8 v)`:
the 8 OFDM bytes := `v`, return 8; else 0. **(run)**

#### ppr_set_same_ht_mcs (.text+0x16492f, original)

`int ppr_set_same_ht_mcs(ppr_t *p, wl_tx_bw_t bw, wl_tx_nss_t nss, wl_tx_mode_t mode, wl_tx_chains_t chains, int8 v)`:
the first 8 bytes of the MCS group := `v`, return 8; else 0. **(run)**

#### ppr_set_same_vht_mcs (.text+0x16498e, original)

The same with all 10 bytes, return 10; else 0. **(run)**

#### ppr_get_vht_mcs_min (.text+0x1647f0, original)

`int ppr_get_vht_mcs_min(const ppr_t *p, wl_tx_bw_t bw, wl_tx_nss_t nss, wl_tx_mode_t mode, wl_tx_chains_t chains, int8 *min)`

1. `*min` = 127 (written first, also on failure).
2. Group as `ppr_get_vht_mcs`; none: return -1.
3. For each of the 10 bytes that is not -128: `*min` = min(`*min`, byte).
4. Return 0. (All disabled: `*min` stays 127.) **(run)**

### Whole-container arithmetic (linear over the `N(p)` data bytes)

All of these loop `i` = 0 .. `N(p)`-1 over `data(p)[i]`, re-reading the
bandwidth word each iteration; nothing happens for a bandwidth >= 3. None
returns a value unless stated.

#### ppr_apply_max (.text+0x1649ed, original)

`void ppr_apply_max(ppr_t *p, int8 v)`: each byte := min(byte, `v`) - "apply
a maximum", i.e. a ceiling. Disabled bytes stay -128 (no special case
needed). **(run)**

#### ppr_apply_min (.text+0x164da6, original)

`void ppr_apply_min(ppr_t *p, int8 v)`: each byte := max(byte, `v`) - a
floor. **No special case for -128: disabled bytes become `v`** (unless `v`
is -128). **(run)**

#### ppr_plus_cmn_val (.text+0x165532, original)

`void ppr_plus_cmn_val(ppr_t *p, int8 v)`: each byte != -128 := byte + `v`
(8-bit wrap: 100 + 100 = -56; a result may become -128 and is then
"disabled"). **(run)**

#### ppr_minus_cmn_val (.text+0x1654e7, original)

`void ppr_minus_cmn_val(ppr_t *p, int8 v)`: each byte `x` != -128 :=
(`x` > `v`) ? (int8)(`x` - `v`) : 0. So the result never goes below 0 when
the subtraction does not overflow; with a negative `v` it can wrap (`x` =
100, `v` = -100 -> -56). **(run)**

#### ppr_cmn_val_minus (.text+0x1654ac, original)

`void ppr_cmn_val_minus(ppr_t *p, int8 v)`: each byte `x` != -128 :=
(int8)(`v` - `x`) (wraps). **(run)**

#### ppr_multiply_percentage (.text+0x165569, original)

`void ppr_multiply_percentage(ppr_t *p, uint8 pct)`: each byte `x` != -128
:= low byte of ((int)`x` * `pct`) / 100, with C division (truncation toward
zero: -7 * 50 / 100 = -3). `pct` is taken as unsigned 0..255; products above
127 wrap when stored (127 * 255 / 100 = 323 -> 0x43 = 67). **(run)**

#### ppr_compare_min (.text+0x1655ae, original)

`void ppr_compare_min(ppr_t *a, const ppr_t *b)`: if `a->ch_bw` =
`b->ch_bw`: each `a[i]` := min(`a[i]`, `b[i]`) over `N(a)` bytes; else
nothing (no error). A rate disabled in either container is disabled in the
result. **(run)**

#### ppr_compare_max (.text+0x1655f8, original)

As `ppr_compare_min` with max: a rate disabled in `a` takes `b`'s value; the
result is disabled only where both are. **(run)**

#### ppr_apply_vector_ceiling (.text+0x164ddc, original)

`uint ppr_apply_vector_ceiling(ppr_t *a, const ppr_t *b)`: if the bandwidth
words differ: return 0. Else each `a[i]` := min(`a[i]`, `b[i]`) (signed) and
return the number of bytes processed (`N(a)`; 0 for a bandwidth >= 3). The
same effect as `ppr_compare_min`, plus a result. **(run)**

#### ppr_apply_vector_floor (.text+0x164e25, original)

`uint ppr_apply_vector_floor(ppr_t *a, const ppr_t *b)`: like the ceiling,
but each `a[i]` := the larger of `a[i]` and `b[i]` **compared as unsigned
bytes**. So a negative byte (0x80..0xff, including -128 = disabled) counts as
larger than any non-negative one: max(5, -3) = -3, max(5, -128) = -128.
Probably not what was intended, but it is what the object does (no caller in
the object). Returns `N(a)` or 0. **(run)**

#### ppr_force_disabled (.text+0x164a69, original) and sub_164a23 (.text+0x164a23, name assigned: `ppr_force_disabled_group`)

`int ppr_force_disabled(ppr_t *p, int8 min)` - disable the groups whose
power is below `min`.

`ppr_force_disabled_group(int8 *g, int8 min, uint n)` (sub_164a23):

1. For `i` = 0 .. min(`n`, 8) - 1: if `g[i]` < `min` (signed): set all `n`
   bytes of the group to -128 and return.
2. For `i` = 8 .. `n`-1 (only the MCS 8 and 9 entries of a 10-byte group):
   if `g[i]` < `min`: `g[i]` := -128.

(A disabled byte, -128, is below any `min` > -128: a group with a disabled
rate among its first 8 becomes wholly disabled.)

`ppr_force_disabled`: for `b` = 0, 1, 2 (`wl_tx_bw_t` 20, 40, 80): `s` =
`S(p, b)`; if non-NULL, apply `ppr_force_disabled_group(group, min, size)`
to the 21 groups of the set in this order (set offsets): 0x00 (4), 0x04 (8),
0x0c (10), 0x16 (4), 0x1a (8), 0x22, 0x2c, 0x36 (10 each), 0x74 (8), 0x7c,
0x86 (10 each), 0x40 (4), 0x44 (8), 0x4c, 0x56, 0x60, 0x6a (10 each), 0x90
(8), 0x98, 0xa2, 0xac (10 each). Return 0.

Because of the selector table, the loop over `b` = 0, 1, 2 reaches every set
of a 20, 40 or 80 MHz container exactly once (40 MHz: `b` 0 = 20in40, `b`
1 = 40; 80 MHz: 20in80, 40in80, 80). The groups do not overlap, so the order
of visits does not change the result. **(run)**

This corrects `acphy-api-txpower.md`, which describes it as "mark the
proprietary-rate groups disabled".

#### ppr_apply_constraint_total_tx (.text+0x164d11, original) and sub_164c6a (.text+0x164c6a, name assigned: `ppr_apply_constraint_total_tx_set`)

`uint ppr_apply_constraint_total_tx(ppr_t *p, int8 v)` - cap each rate so the
*total* power over all chains does not exceed `v`.

`sub_164c6a(int8 *set, int8 v)`: with `v2` = (int8)(`v` - 12) and `v3` =
(int8)(`v` - 19) (8-bit wrap: 3 dB and 4.75 dB in quarter dB, i.e. 10log10(2)
and 10log10(3)): block A bytes := min(byte, `v`); block B := min(byte, `v2`);
block C := min(byte, `v3`); block D := min(byte, `v2`); block E := min(byte,
`v3`) (blocks: Data). Returns 182.

`ppr_apply_constraint_total_tx`: by `p->ch_bw`: 0 -> the set at +4; 1 -> the
sets at +4 and +0xba; 2 -> +4, +0xba, +0x170; returns 182 x the number of
sets, or 0 for a bandwidth >= 3. Disabled bytes stay disabled. **(run)**

### Queries

#### ppr_get_max (.text+0x164e6e, original)

`int8 ppr_get_max(const ppr_t *p)`: the largest byte (signed) of the `N(p)`
data bytes, disabled bytes included (so: the highest enabled power, or -128
if all are disabled). Starts from `data(p)[0]`. For a bandwidth >= 3 the
object returns the byte at `p+4`, outside a 4-byte container (undefined
value). Only the low byte of the result register is defined. **(run)**

#### ppr_get_min (.text+0x164ea2, original)

`int8 ppr_get_min(const ppr_t *p, int8 skip)` - the smallest byte that is
neither -128 nor `skip`.

1. If `p->ch_bw` > 2: return -128.
2. `m` = -128. Walk `i` from 0: while `i` < `N(p)` and (`m` = `skip` or `m` =
   -128): `m` = `data[i]`, `i`++.
3. For the remaining `i` < `N(p)`: if `data[i]` != -128 and `data[i]` !=
   `skip` and `data[i]` < `m`: `m` = `data[i]`.
4. Return `m`.

So if every byte is -128 or `skip`, the result is the **last** data byte
(either `skip` or -128). The PHY passes the SROM minimum as `skip` (the same
value it passed to `ppr_force_disabled`). **(run)**

#### ppr_get_max_for_bw (.text+0x164f0a, original)

`int8 ppr_get_max_for_bw(const ppr_t *p, wl_tx_bw_t bw)`: `s` = `S(p, bw)`;
NULL: -128. Else the largest of the 182 bytes of the set (signed; disabled
included). **(run)**

#### ppr_get_min_for_bw (.text+0x164f3d, original)

`int8 ppr_get_min_for_bw(const ppr_t *p, wl_tx_bw_t bw)`: `s` = `S(p, bw)`;
NULL: -128. Else the smallest of the 182 bytes **including disabled ones**
(any disabled rate in the set gives -128). **(run)**

### Mapping

The callback type is in Data. In all five, the element pointers passed to
the callback advance together, one byte per call, from the first byte of the
group (or set) to the last. If either container lacks the set/group, nothing
is called.

#### ppr_map_vec_dsss (.text+0x164f70, original)

`void ppr_map_vec_dsss(ppr_mapfn_t fn, void *ctx, ppr_t *a, ppr_t *b, wl_tx_bw_t bw, wl_tx_chains_t chains)`:
`sa` = `S(a, bw)`, `sb` = `S(b, bw)`; both must be non-NULL; DSSS group by
`chains` in both; 4 calls `fn(ctx, &ga[k], &gb[k])`, `k` = 0..3. **(run)**

#### ppr_map_vec_ofdm (.text+0x165041, original)

`void ppr_map_vec_ofdm(ppr_mapfn_t fn, void *ctx, ppr_t *a, ppr_t *b, wl_tx_bw_t bw, wl_tx_mode_t mode, wl_tx_chains_t chains)`:
OFDM group (Data) in both; 8 calls. **(run)**

#### ppr_map_vec_ht_mcs (.text+0x16514e, original)

`void ppr_map_vec_ht_mcs(ppr_mapfn_t fn, void *ctx, ppr_t *a, ppr_t *b, wl_tx_bw_t bw, wl_tx_nss_t nss, wl_tx_mode_t mode, wl_tx_chains_t chains)`:
MCS group by `ppr_get_mcs_group` in both; 8 calls (MCS 0..7). **(run)**

#### ppr_map_vec_vht_mcs (.text+0x1651f6, original)

The same with 10 calls (MCS 0..9). **(run)**

#### ppr_map_vec_all (.text+0x16529e, original)

`void ppr_map_vec_all(ppr_mapfn_t fn, void *ctx, ppr_t *a, ppr_t *b)`:

1. If `S(a,0)` and `S(b,0)`: 182 calls over those sets.
2. If `S(a,1)` and `S(b,1)`: 182 calls over them; **then, only in this
   case**, if `S(a,3)` and `S(b,3)`: 182 calls.
3. If `S(a,2)` and `S(b,2)`: 182 calls; then, only in this case, `b` = 4
   (182 calls if both exist) and `b` = 5 (likewise).

Because several `wl_tx_bw_t` values reach the same set, sets are visited
more than once. Same bandwidth: 20 MHz -> 182 calls; 40 MHz -> 546 (20in40,
40, 20in40 again); 80 MHz -> 1092 (20in80, 40in80, 20in80, 80, 20in80,
40in80). A callback that is not idempotent (e.g. `*b += 1`) is applied
several times to those rates; an implementation must keep the exact order.
Mixed bandwidths pair the sets the two containers give for the same `b`
(e.g. `a` 40 / `b` 80: `b`=0: a+0xba with b+0xba; `b`=1: a+4 with b+0x170;
`b`=3: a+0xba with b+0xba; nothing else). **(run**, all 9 pairs with a
recording callback**)**

### Serialisation

#### ppr_init_ser_mem_by_bw (.text+0x165f9c, original)

`int ppr_init_ser_mem_by_bw(uint8 *buf, wl_tx_bw_t bw, uint len)`

1. If `buf` is NULL: return -2.
2. If `len` < 8 + (`bw` <= 2 ? table[`bw`] : 0): return -2.
3. Bytes 0..3 := `be ef c0 ff`; bytes 4..7 := `00 00 00 07`; bytes 8 ..
   `len`-1 := 0x80. Return 0. **(run)**

#### ppr_init_ser_mem (.text+0x165fe8, original)

`int ppr_init_ser_mem(uint8 *buf, const ppr_t *p, uint len)`:
`ppr_init_ser_mem_by_bw(buf, p->ch_bw, len)`. **(run)**

#### sub_165780 (.text+0x165780, name assigned: `ppr_ser_set`)

`uint ppr_ser_set(const int8 *set, uint8 **cursor, uint flags)`: append the
chain blocks selected by `flags` (Data) of one set at `*cursor`, advancing
`*cursor`; returns the number of bytes appended. **(run** through
`ppr_serialize`**)**

#### ppr_serialize (.text+0x165854, original)

`int ppr_serialize(const ppr_t *p, uint8 *buf, uint len, uint *out_len)`

1. If `len` <= 8: return -14.
2. `flags` = 7; if bytes 0..3 of `buf` are `be ef c0 ff`: `flags` = the
   big-endian word at bytes 4..7.
3. `need` = bytes per set for `flags` (22/64/116, +28 or +66 with TXBF),
   times 2 if `p->ch_bw` = 1, times 3 if 2, else times 1. If `len` < `need`:
   return -14. (The 8 header bytes are **not** counted: with an exactly
   sized `len` = `need` the function writes 8 bytes past `len`. A buffer
   from `ppr_ser_size` always has room for flags 7.)
4. Byte 0 := 1; bytes 4..7 := `00 00 00 07` (always 7, whatever `flags`
   was); byte 1 := low byte of `p->ch_bw`. Bytes 2..3 are not touched.
5. From byte 8, by the low byte of `p->ch_bw`: 0 -> `ppr_ser_set(p+4)`; 1 ->
   `p+4` then `p+0xba`; 2 -> `p+4`, `p+0xba`, `p+0x170`; other -> nothing.
6. `*out_len` := 8 + bytes appended; return 0.

Quirk: when the caller had prepared the buffer with flags other than 7, the
data are reduced but the header still says 7, so `ppr_deserialize` of that
buffer would read it wrongly. With the default preparation the round trip
is exact. **(run)**

#### sub_165642 (.text+0x165642, name assigned: `ppr_deser_set`)

`void ppr_deser_set(int8 *set, const uint8 **cursor, uint flags)`: the
inverse of `ppr_ser_set`: copy the selected blocks from `*cursor` into the
set, advancing `*cursor`; blocks not selected are not written. **(run)**

#### sub_1656f7 (.text+0x1656f7, name assigned: `ppr_deser_cpy`)

`void ppr_deser_cpy(ppr_t *p, const uint8 *data, uint flags, wl_tx_bw_t bw)`:
`p->ch_bw` := `bw` (32-bit write); then by `bw`: 0 -> set +4; 1 -> +4, +0xba;
2 -> +4, +0xba, +0x170 (one `ppr_deser_set` each, sharing one cursor
starting at `data`); other -> nothing more. **(run)**

#### ppr_deserialize (.text+0x165d3a, original)

`int ppr_deserialize(ppr_t *p, const uint8 *buf, uint len)`

1. `len` < 9: return -14.
2. `buf` NULL: return -2.
3. `buf[0]` != 1: return -37.
4. `buf[1]` != `p->ch_bw`: return -2.
5. `flags` = big-endian word at bytes 4..7; `ppr_set_cmn_val(p, -128)`;
   `ppr_deser_cpy(p, buf+8, flags, buf[1])`; return 0.

`len` is not compared with the data length: the function reads as many bytes
as `flags` and the bandwidth demand. Rates not in the selected blocks end up
disabled. **(run)**

#### ppr_deserialize_create (.text+0x1660b3, original)

`int ppr_deserialize_create(osl_t *osh, const uint8 *buf, uint len, ppr_t **out)`

1. `len` <= 8: `*out` = NULL, return -14.
2. `buf` NULL: `*out` = NULL, return -2. `buf[0]` != 1: `*out` = NULL,
   return -37.
3. `bw` = `buf[1]`; if `len` < 8 + (`bw` <= 2 ? table[`bw`] : 0): `*out` =
   NULL, return -14 (the full size is required whatever the flags).
4. `p` = `ppr_create(osh, bw)`; NULL: `*out` = NULL, return -27.
5. `flags` = big-endian word at bytes 4..7; `ppr_set_cmn_val(p, -128)`;
   `ppr_deser_cpy(p, buf+8, flags, bw)`; `*out` = `p`; return 0. **(run)**

## Who looks inside a `ppr_t` (the contract)

**No code outside `wlc_ppr.c` reads or writes the bytes of a container
directly, and nobody allocates one except `ppr_create`.** The only access to
individual elements from outside is by the two `ppr_map_vec_*` callbacks,
through the element pointers the map functions hand them (that is the
documented interface, not a layout dependency: neither callback depends on
the position of the element):

* `sub_172c6c` (wlc_stf.c, passed to `ppr_map_vec_all` by
  `wlc_update_txppr_offset`): `*b` := -128 if `*a` is -128, else min((uint8)`*a`
  >> 1, `*(uint8 *)ctx`) - halves the PHY's quarter-dB offsets into half-dB
  steps capped by a context byte.
* `sub_06ee40` (wlc_channel.c, passed to `ppr_map_vec_dsss/ofdm/ht_mcs` by
  `sub_0736a4`): reads `*a` and `*b`, writes only its context byte:
  `ctx` := min(`ctx`, `*a` > `*b` ? `*a` - `*b` : 0), compared as unsigned bytes.

Evidence:

1. **Run** (`re-out\analysis\ppr\watch.py`, output `watch.txt`): the session
   up, channels 36, 36/80, 6, 149/40, watchdog, down, up, down, detach. Each
   container returned by `ppr_create` got a Unicorn memory hook over its
   bytes; the `osl_mem*` imports were wrapped; every `ppr_*` entry checked its
   container arguments. Result: 944 calls into the file (45 `ppr_create`, 45
   `ppr_delete`, 0 containers left), every container argument came from
   `ppr_create`, no `osl_malloc` of a container size elsewhere, no `osl_mem*`
   on a container from outside the file; the only outside accesses are
   `sub_172c6c`'s 2,730 reads and 2,730 writes (the map callback).
2. **Read** (`staticscan.py` + reading): all 57 functions outside the file
   that call `ppr_*` (`callers.txt`: wlc.c 2, wlc_channel.c 8,
   wlc_olpc_engine.c 1, wlc_stf.c 7, wlc_phy_cmn.c 18, wlc_phy_ac.c 1, and
   the other PHYs abg/ht/lcn/lcn40/lp/n/ssn 20). No container variable is
   ever dereferenced; containers are held as pointers (`pi+0x1c8`, the stf
   state +0x30, fields +0x50/+0x60 of the current-power report passed to
   `wlc_phy_txpower_get_current`, locals), created, passed on, deleted. The
   ones the session does not reach and that matter for an open driver
   (`wlc_doiovar` case 0x5f, `wlc_sendassocreq`, `sub_072656`, `sub_0736a4`,
   `wlc_channel_band_chain_limit`, `wlc_channel_set_txpower_limit`,
   `wlc_channel_update_txpwr_limit`, `sub_06ef19`, `wlc_phy_txpower_get_current`,
   `wlc_stf_get_pwrperrate`, `sub_175ae6`) were read one by one.

Consequences: the byte layout is private to the file *except* that the
serialised form crosses to user space (`ppr_serialize` from the wlc_stf
iovar handler `sub_175ae6`, into a buffer the iovar caller provides, flags
in it honoured), and that a map callback sees the element order. An open
implementation is free in its internal layout as long as it reproduces the
group selection, the visiting order of the map functions and the serialised
bytes. The open PHY already treats `ppr_t` as opaque (acphy-api-txpower.md).

## Verification

Method (scripts in `re-out\analysis\ppr\`): `model.py` is a Python model
written from this text; `compare.py` creates containers with random content
(45 % uniform bytes, 20 % -128, the rest boundary values and values near the
operation's argument), places each in a buffer surrounded by a random guard
pattern, calls the object's function in the emulator (`emu.Machine`, bare,
no session), and compares **the whole guarded buffer**, every output buffer
(also guarded) and the return value with the model. The group functions are
run for every combination of bandwidth argument (0..6, 0xffffffff),
`chains` 0..5, `mode` 0..5 (0..7 for MCS), `nss` 0..4, on containers of
`ch_bw` 0, 1, 2, 3; `ppr_copy_struct` and the binary operations for all
pairs of bandwidths (including 3 and 7); the map functions with a recording
callback and with a mutating one (`*b += *a + 1; *a ^= 0x55`) to check order
and repetition. Run `python compare.py 400 7` (`cmp-400-7.txt`):

| Function | comparisons | differences |
|---|---|---|
| `ppr_size`, `ppr_ser_size`, `ppr_ser_size_by_bw`, `ppr_get_ch_bw` | 13 each | 0 |
| `ppr_get_max_bw` | 1 | 0 |
| `ppr_create`, `ppr_init`, `ppr_delete` (incl. malloc/mfree sizes) | 9 each | 0 |
| `ppr_clear`, `ppr_set_cmn_val` | 400 each | 0 |
| `ppr_copy_struct` (25 bandwidth pairs) | 400 | 0 |
| `ppr_get_dsss`, `ppr_set_dsss`, `ppr_set_same_dsss` | 192 each | 0 |
| `ppr_get_ofdm`, `ppr_set_ofdm`, `ppr_set_same_ofdm` | 1,152 each | 0 |
| `ppr_get_ht_mcs`, `ppr_set_ht_mcs`, `ppr_set_same_ht_mcs` | 6,016 each | 0 |
| `ppr_get_vht_mcs`, `ppr_set_vht_mcs`, `ppr_set_same_vht_mcs` | 6,016 each | 0 |
| `ppr_get_vht_mcs_min` | 6,416 | 0 |
| `ppr_apply_max`, `ppr_apply_min`, `ppr_plus_cmn_val`, `ppr_minus_cmn_val`, `ppr_cmn_val_minus`, `ppr_multiply_percentage` | 400 each | 0 |
| `ppr_compare_min`, `ppr_compare_max`, `ppr_apply_vector_ceiling`, `ppr_apply_vector_floor` | 400 each | 0 |
| `ppr_force_disabled`, `ppr_apply_constraint_total_tx` | 400 each | 0 |
| `ppr_get_max`, `ppr_get_min`, `ppr_get_max_for_bw`, `ppr_get_min_for_bw` | 400 each | 0 |
| `ppr_map_vec_dsss` / `_ofdm` / `_ht_mcs` / `_vht_mcs` / `_all` | 44 / 42 / 46 / 48 / 85 | 0 |
| `ppr_init_ser_mem_by_bw`, `ppr_init_ser_mem` | 49 / 48 | 0 |
| `ppr_serialize` (prepared/unprepared buffers, flags 0..7 and random, lengths at the limits) | 400 | 0 |
| `ppr_deserialize`, `ppr_deserialize_create` (round trips, wrong version, wrong bandwidth, short lengths, NULL) | 460 / 459 | 0 |
| **total, 50 functions** | **55,905** | **0** |

The 10 stripped helpers run inside these calls (the selectors in every group
function, `sub_164a23` in `ppr_force_disabled`, `sub_164c6a` in
`ppr_apply_constraint_total_tx`, `sub_165642`/`sub_1656f7`/`sub_165780` in
the serialisation).

The first run (20 cases per family) showed 128 differences, all with `nss`
= 1, `chains` = 3, `mode` 4..7: there the table index runs past the MCS
tables into the size table, i.e. the offsets 0xb6, 0x16c, 0x222 point
outside the set - undefined input, now excluded (the model raises an
"undefined" for indices >= 24; with indices < 24 out-of-range modes were
compared and matched).

**Replay of the session** (`replay.py`, `replay.txt`): every call into the
file from outside during up, 36, 36/80, 6, 149/40, watchdog, down, up, down,
detach was replayed through the model (containers and input buffers read at
entry, containers, output buffers and result compared at the return; the map
callback `sub_172c6c` modelled as in "Who looks inside"): 854 calls of 25
functions (`ppr_set_vht_mcs` 310, `ppr_set_ofdm` 174, `ppr_set_dsss` 48,
`ppr_create` 45, `ppr_set_ht_mcs` 26, `ppr_apply_max` 24, `ppr_compare_min`
24, ..., `ppr_map_vec_all` 6), **0 differences**. (Not replayed: the 45
`ppr_delete`, checked by size in `compare.py`, and the 45 `ppr_init` nested
in `ppr_create`.)

**NULL container** (`nullcheck.py`): the getters return 0 and fill -128,
`ppr_get_vht_mcs_min` returns -1 with 127, the setters return 0,
`ppr_get_max/min_for_bw` return -128, the map functions call nothing,
`ppr_force_disabled` returns 0 - all because the dispatcher returns NULL.
The other functions dereference the pointer (no NULL check).

What was **read only**: the out-of-bounds behaviour for `nss` = 1 /
`chains` = 0 and for table indices >= 24; `ppr_get_max` on a container with
`ch_bw` >= 3 was run, but its result is whatever byte follows the header.

Session facts (`boundary.txt`, `watch.txt`): the functions called during
the standard session are `ppr_create/init/delete/clear/set_cmn_val/
copy_struct/get_ch_bw`, `ppr_set_dsss/ofdm/ht_mcs/vht_mcs`,
`ppr_get_dsss/ofdm/ht_mcs/vht_mcs/vht_mcs_min`, `ppr_apply_max/min`,
`ppr_apply_vector_ceiling`, `ppr_compare_min/max`, `ppr_minus_cmn_val`,
`ppr_cmn_val_minus`, `ppr_force_disabled`, `ppr_get_max/min`,
`ppr_map_vec_all` (+ `ppr_get_max_bw` at attach). Never called anywhere in
the object (no caller outside the file): `ppr_set_same_ht_mcs`,
`ppr_set_same_vht_mcs`, `ppr_map_vec_vht_mcs`, `ppr_apply_vector_floor`,
`ppr_get_min_for_bw`, `ppr_size`, `ppr_ser_size`, `ppr_ser_size_by_bw`,
`ppr_init_ser_mem`, `ppr_init_ser_mem_by_bw`, `ppr_deserialize`,
`ppr_deserialize_create`.

### Corrections to the `ppr` table of acphy-api-txpower.md

| Entry | acphy-api-txpower.md says | the object does (run) |
|---|---|---|
| `ppr_apply_max(p, v)` | every enabled rate := max(rate, v) | every byte := **min**(byte, v) (a ceiling); disabled stays disabled |
| `ppr_apply_min(p, v)` | every enabled rate := min(rate, v) | every byte := **max**(byte, v) (a floor); **disabled bytes become v** |
| `ppr_minus_cmn_val(p, d)` | every enabled rate -= d | enabled `x` := `x` > `d` ? `x` - `d` : **0** (clamped at 0) |
| `ppr_force_disabled(p, min)` | mark the proprietary-rate groups disabled | disable each rate group that has a rate below `min` among its first 8; in 10-rate groups also single MCS 8/9 below `min` |
| `ppr_get_min(p, floor)` | lowest enabled rate, or `floor` | lowest byte that is neither -128 nor the argument (the argument is *excluded*, not a floor); if none, the last data byte |
| `ppr_get_max(p)` | highest enabled rate | the same in effect (max over all bytes, -128 if all disabled) |
| `ppr_get_ofdm/set_ofdm(p, m, rateset, bw, ...)` | argument names | the arguments are `(p, bw, mode, chains, buf)` (`wl_tx_bw_t`, `wl_tx_mode_t`, `wl_tx_chains_t`); MCS: `(p, bw, nss, mode, chains, buf)`; DSSS: `(p, bw, chains, buf)` |
| `ppr_copy_struct` | copy all rates | `(src, dst)`; same bandwidth: all data; different: set by set as in the table above |
| `ppr_multiply_percentage` | scaled by pct/100 | `x` * pct / 100 truncated toward zero, stored as a byte (wraps above 127) |
| `ppr_get_ch_bw`, `ppr_create(osh, bw)`, `ppr_delete(osh, p)`, `ppr_clear`, `ppr_set_cmn_val`, `ppr_plus_cmn_val`, `ppr_cmn_val_minus`, `ppr_compare_min/max`, the get/set group functions | | as stated there (with the argument order above) |

The open PHY's service calls are thin pass-throughs to the object's
functions in the hybrid build, so these corrections change no behaviour
there; they matter for anyone implementing `ppr` or reasoning about the
merge `sub_0b8ca4` from that table (e.g. the merge's step 6 disables whole
groups below the SROM minimum, it does not touch "proprietary" rates; and
in `sub_0837c5` (olpc) `ppr_apply_min(lim, srom_min)` raises disabled
regulatory rates to the SROM minimum).

## Open questions

1. Whether any user-space tool prepares a serialisation buffer with flags
   other than 7 for the wlc_stf iovar (`sub_175ae6`); the object then
   writes reduced data under a header that says 7 (quirk documented in
   `ppr_serialize`). Not reachable from the open driver's own code.
2. `ppr_apply_vector_floor` compares unsigned; no caller exists in this
   object, so it is unknown whether Broadcom ever relied on it. Reproduce it
   as described if bit-exactness is wanted.
3. The 8-byte overrun allowed by `ppr_serialize`'s length check (header not
   counted) is real in the object; whether the iovar handler's buffer always
   has the slack was not checked.
