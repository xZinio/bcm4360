# Rate sets: the helpers of `wlc_rate.c`

The driver keeps the rates it may use as *rate sets* - a list of legacy
rates (in units of 500 kbit/s, bit 7 = basic rate), the HT MCS bit map and the
VHT MCS map - and builds and narrows them with a handful of helpers of
`wlc_rate.c`: the default set of a PHY type and band, the filter by kind of
rate, the intersection with what the hardware supports. They run at attach,
at every `wlc_init`, at joins and on station records. This document
specifies the ten that run in the sessions of the tools.

Written by the coordinating session from the disassembly; checked by running
the object in the emulator (see "Verification").

## Scope

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_rate_hwrs_filter_sort_validate` | 0x168f50 | 299 | original |
| `wlc_rateset_filter` | 0x168b85 | 241 | original |
| `wlc_rateset_ofdm_fixup` | 0x168765 | 91 | original |
| `wlc_rateset_default` | 0x16907b | 314 | original |
| `wlc_rateset_vhtmcs_upd` | 0x16872e | 55 | original |
| `wlc_rateset_mcs_upd` | 0x168713 | 27 | original |
| `wlc_rateset_mcs_build` | 0x168b47 | 62 | original |
| `wlc_rateset_bw_mcs_filter` | 0x168af0 | 22 | original |
| `wlc_rateset_copy` | 0x168c76 | 25 | original |
| `wlc_rate_rspec_reference_rate` | 0x16868c | 81 | original |

None of them touches the card, the driver state or global variables: they
work on the rate sets they are given and on constant tables (below). Copies
use `osl_memcpy`, clearing `osl_memset`.

Arguments typed `bool`/`u8` below are bytes (only the low 8 bits count);
the others are 32 bit. The `count` of every rate set handed in is at most 16
(the object reads past `rates` otherwise; the driver never does that).

## Data

### The rate set (0x28 bytes, the caller's)

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | `count`: number of legacy rates in `rates` |
| 0x04 | 16 | `rates[16]`: bits 0-6 the rate in 500 kbit/s, bit 7 "basic rate" |
| 0x14 | 1 | `htphy_membership` |
| 0x15 | 16 | `mcs[16]`: HT MCS bit map, bit `k` of byte `j` = MCS `8j + k`; byte 4 bit 0 is MCS 32 |
| 0x25 | 1 | (unused) |
| 0x26 | 2 | `vht_mcsmap`: 2 bits per spatial stream `s` = 0..7 at bits `2s`, `2s+1`: 0 = MCS 0-7, 1 = 0-8, 2 = 0-9, 3 = not supported |

### `rate_info`: per rate code (0..108)

A byte per rate code (the rate in 500 kbit/s, 0..108); 0 = not a rate. Bit 7
set = an OFDM rate. The non-zero entries:

| Code | 2 | 4 | 11 | 12 | 18 | 22 | 24 | 36 | 48 | 72 | 96 | 108 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Value | 0x0a | 0x14 | 0x37 | 0x8b | 0x8f | 0x6e | 0x8a | 0x8e | 0x89 | 0x8d | 0x88 | 0x8c |

The filter of `wlc_rateset_filter` (rates = 1) also looks up codes 109..127
(a rate byte with bit 7 cleared can be anything up to 127): the object then
reads the bytes that follow the table, which are, for codes 109 to 127:
`00 00 00 64 19 00 00 bc 34 00 00 37 1c 00 00 98 3a 00 00` - so of these
codes exactly **116 and 124** count as OFDM there.

### The default rate sets

`count` / `rates` / `mcs` (bytes 0..4; the other mcs bytes are 0) /
`vht_mcsmap`; `htphy_membership` and the unused byte are 0 in all:

| Name | count | rates | mcs[0..4] | vht_mcsmap |
|---|---|---|---|---|
| `cck_rates` | 4 | 82 84 0b 16 | 00 00 00 00 00 | 0xffff |
| `ofdm_rates` | 8 | 8c 12 98 24 b0 48 60 6c | 00 00 00 00 00 | 0xffff |
| `cck_ofdm_rates` | 12 | 82 84 8b 0c 12 96 18 24 30 48 60 6c | 00 00 00 00 00 | 0xffff |
| `ofdm_mimo_rates` | 8 | 8c 12 98 24 b0 48 60 6c | ff ff ff ff 00 | 0x002a |
| `ofdm_40bw_mimo_rates` | 8 | 8c 12 98 24 b0 48 60 6c | ff ff ff ff 01 | 0x002a |
| `cck_ofdm_mimo_rates` | 12 | 82 84 8b 0c 12 96 18 24 30 48 60 6c | ff ff ff ff 00 | 0x002a |
| `cck_ofdm_40bw_mimo_rates` | 12 | 82 84 8b 0c 12 96 18 24 30 48 60 6c | ff ff ff ff 01 | 0x002a |

(The rate bytes are hexadecimal.)

### Reference rates of `wlc_rate_rspec_reference_rate`

Ten 32 bit values, index 0..9: 12, 24, 36, 48, 72, 96, 108, 108, 108, 108.

## Procedures

### `wlc_rateset_copy(src, dst)`

Copy the 0x28 bytes of `src` to `dst` (`osl_memcpy(dst, src, 0x28)`).

### `wlc_rateset_mcs_upd(rs, txstreams)` (txstreams u8)

For `i` = `txstreams`, `txstreams`+1, ... while `i` < 4: `mcs[i]` = 0. (So
nothing if `txstreams` >= 4.)

### `wlc_rateset_mcs_build(rs, txstreams)` (txstreams u8)

Copy the 16 `mcs` bytes of `cck_ofdm_mimo_rates` (ff ff ff ff, then 12
zeros) into `rs->mcs`, then as `wlc_rateset_mcs_upd(rs, txstreams)`.

### `wlc_rateset_vhtmcs_upd(rs, nstreams)` (nstreams u8)

For every stream `s` from `nstreams` to 7 (none if `nstreams` >= 8): set
the two bits of stream `s` in `vht_mcsmap` to 3 (not supported). The other
streams are unchanged.

### `wlc_rateset_bw_mcs_filter(rs, bw)` (bw u8)

If `bw` is 40: set bit 0 of `mcs[4]` (MCS 32); else clear it.

### `wlc_rate_rspec_reference_rate(rspec)` (32 bit) -> 32 bit

By bits 24-25 of `rspec`:

* 2 (VHT): `i` = bits 0-3 of `rspec`; if `i` >= 10, `i` = 0; result the
  reference rate `i`.
* 1 (HT): if the low byte of `rspec` is at most 0x1f, `i` = bits 0-2, else
  `i` = 0; result the reference rate `i`.
* 0 or 3: the low byte of `rspec`.

### `wlc_rateset_ofdm_fixup(rs)`

For each of the first `count` rates: look for the first of the eight rates
of `ofdm_rates` (8c 12 98 24 b0 48 60 6c, in this order) that equals it in
bits 0-6; if there is one and it has bit 7 set, set bit 7 of the rate. (Bit
7 is never cleared.)

### `wlc_rateset_filter(src, dst, basic_only, rates, xmask, mcsflags)`

`basic_only` bool, `rates` u8 (0: CCK and OFDM, 1: CCK only, 2: OFDM only),
`xmask` 32 bit, `mcsflags` u8. `src` and `dst` are different sets.

1. `n` = 0. For each of the first `src->count` rates `r` of `src`, in order:
   skip it if `basic_only` and bit 7 of `r` is clear; if `rates` is 1, skip
   it if `rate_info` of `r & 0x7f` has bit 7 set (see "Data" for codes above
   108); if `rates` is 2, skip it if `r & 0x7f` is 2, 4, 11 or 22; otherwise
   `dst->rates[n] = r & xmask` (low byte), `n` += 1.
2. `dst->count = n`.
3. If `xmask` is exactly 0xff and `rates` is 2: `wlc_rateset_ofdm_fixup(dst)`.
4. `dst->htphy_membership = src->htphy_membership`.
5. If bit 0 of `mcsflags` is clear, or `rates` is 1: the 16 `mcs` bytes of
   `dst` = 0, `dst->vht_mcsmap` = 0xffff. Else: copy the 16 `mcs` bytes of
   `src` to `dst` (`osl_memcpy`); then if bit 1 of `mcsflags` is set,
   `dst->vht_mcsmap = src->vht_mcsmap`, else 0xffff.

### `wlc_rate_hwrs_filter_sort_validate(rs, hw_rs, check_brate)` -> bool

Keeps of `rs` the rates that `hw_rs` also has, in the order of `hw_rs`.
`check_brate` bool. (The object's callers pass a fourth argument, which the
function does not read.)

1. A table `t` of 109 bytes (codes 0..108), all 0 (`osl_memset`).
2. For each of the first `rs->count` rates `r` of `rs`: `c = r & 0x7f`; if
   `c` < 109 and `rate_info[c]` is not 0: `t[c] = r` (a later rate with the
   same code replaces an earlier one).
3. `n` = 0. For each of the first `hw_rs->count` rates `h` of `hw_rs`, in
   order: if `t[h & 0x7f]` is not 0: `rs->rates[n] = t[h & 0x7f]`, `n` += 1.
   (A code of `hw_rs` above 108 reads beyond the table in the object - stack
   left over - and is undefined; the driver's hardware rate sets never have
   one.)
4. `rs->count = n`.
5. `rs->mcs[j] &= hw_rs->mcs[j]` for the 16 bytes.
6. For each stream `s` = 0..7: `a` = the two bits of `s` in
   `rs->vht_mcsmap`, `b` = those of `hw_rs`; if `b` < `a` and `a` is not 3:
   the two bits of `rs` become `b`.
7. Result: false if `n` is 0. Else, if `check_brate`: true if one of the
   first `n` rates of `rs` has bit 7 set, false if none; if not
   `check_brate`: true.

### `wlc_rateset_default(rs_tgt, rs_hw, phy_type, bandtype, cck_only, rate_mask, mcsallow, bw, txstreams)`

`phy_type`, `bandtype`, `rate_mask` 32 bit; `cck_only`, `mcsallow`, `bw`,
`txstreams` u8. `rs_hw` may be NULL.

1. Choose the default set `d`:
   * `phy_type` 4, 6, 7, 8, 10 or 11 (the MIMO PHYs; 11 = AC): if `bandtype`
     is 1 (5 GHz): `ofdm_mimo_rates` if `bw` is 20, else
     `ofdm_40bw_mimo_rates`; other bands: `cck_ofdm_mimo_rates` if `bw` is 20,
     else `cck_ofdm_40bw_mimo_rates`;
   * `phy_type` 5: `ofdm_rates` if `bandtype` is 1, else `cck_ofdm_rates`;
   * `phy_type` 0: `ofdm_rates`; `phy_type` 2: `cck_ofdm_rates`;
   * any other: `cck_rates`.
2. If `rs_hw` is NULL, `rs_hw` = `d`. (So `d` matters only when the caller
   passes no hardware set.)
3. `s` = a copy of `rs_hw` (`wlc_rateset_copy`, into a set of the
   function's own); `wlc_rateset_mcs_upd(s, txstreams)`;
   `wlc_rateset_vhtmcs_upd(s, txstreams)`.
4. `wlc_rateset_filter(s, rs_tgt, false, cck_only != 0 ? 1 : 0, rate_mask,
   mcsallow)`.
5. `wlc_rate_hwrs_filter_sort_validate(rs_tgt, rs_hw, false)` (result not
   used).

An implementation may run the steps of the helpers inline; only the results
count (nothing here is observable but the memory of `rs_tgt`).

## Verification

`tools/re/rate_scenarios.py` (`python hybrid.py rate`) calls the ten
functions on the object alone and on the hybrid core with the same
arguments - the default sets, the sets the driver builds in the session,
and many random sets (counts 0..16, any rate byte, basic bits, MCS maps,
VHT maps, every `rates`/`mcsflags`/`xmask` kind, all PHY types, bands,
bandwidths and stream counts, rspecs of every kind) - and compares the
result and the memory of the sets after each call. A Python model written
from the text of this specification (the coordinator's check of it, not part
of the repository) gives the object's results for all of them (see the
numbers in the commit that added this document).
