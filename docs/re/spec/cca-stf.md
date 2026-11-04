# Channel statistics (`wlc_cca.c`) and the run-time part of `wlc_stf.c`

Two small pieces of Broadcom's upper MAC layer (`wlc`) that are still the
object's in the hybrid build with the open PHY switched on:

* **`wlc_cca.c`**, the channel-occupancy ("clear channel assessment")
  statistics: once per watchdog tick it reads the microcode's occupancy
  counters (through `wlc_bmac_cca_stats_read`, open MAC code,
  `bmac-run.md` R5) and accumulates their increments per channel in a ring
  of 60 one-second slots; the iovar `cca_get_stats` reports them.
* **the hot part of `wlc_stf.c`** ("space-time features": transmit chains,
  spatial policy): the update of the per-rate transmit power offsets that the
  PHY asks for after every power computation (`wlc_update_txppr_offset`, with
  its `ppr_map_vec_all` callback), the choice of the transmit cores per rate
  class (`wlc_stf_spatialpolicy_set_complete`) and the shared-memory words
  derived from it, the single-stream mode update and the per-channel
  single-stream algorithm.

Written from the disassembly (the decompiler output was used as an index
only) and checked by running the object in the emulator: see
"Verification".

## Scope

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_cca_attach` | 0x06e309 | 245 | original |
| `wlc_cca_detach` | 0x06e2d2 | 55 | original |
| `sub_06dd9c` | 0x06dd9c | 86 | assigned: `wlc_cca_down` |
| `sub_06e17b` | 0x06e17b | 296 | assigned: `wlc_cca_watchdog` |
| `sub_06e3fe` | 0x06e3fe | 382 | assigned: `wlc_cca_doiovar` |
| `cca_stats_upd` | 0x06df2e | 589 | original |
| `cca_query_stats` | 0x06ddf2 | 316 | original |
| `cca_stats_tsf_upd` | 0x06e2a3 | 47 | original |
| `wlc_cca_get_chanspec` | 0x06dd7c | 32 | original |
| `wlc_update_txppr_offset` | 0x175513 | 1011 | original |
| `sub_172c6c` | 0x172c6c | 27 | assigned: `wlc_stf_txppr_offset_mapfn` |
| `sub_174cd5` | 0x174cd5 | 96 | assigned: `wlc_stf_spatial_mode_upd` |
| `wlc_stf_spatialpolicy_set_complete` | 0x1740c4 | 2886 | original |
| `sub_172af1` | 0x172af1 | 105 | assigned: `wlc_stf_txcore_default` |
| `wlc_stf_txcore_shmem_write` | 0x172e11 | 256 | original |
| `sub_172da9` | 0x172da9 | 104 | assigned: `wlc_stf_spatial_expansion_idx` |
| `wlc_stf_ss_update` | 0x173c97 | 221 | original |
| `wlc_stf_ss_algo_channel_get` | 0x175911 | 365 | original |
| `wlc_stf_spatial_policy_set` | 0x174c0a | 101 | original |

(Sizes as `re-out\index.tsv` gives them: to the next function, padding
included.)

## Overview

### Channel statistics

`wlc_attach` calls `wlc_cca_attach(wlc)` and keeps the result at
`wlc+0x628` (attach fails with error 0x5c if it is NULL). The attach
allocates the module's state (`cca_info`, 0x4390 bytes) and registers the
module `"cca_stats"` with the module service: iovar table with the one entry
`cca_get_stats`, iovar handler `sub_06e3fe`, watchdog `sub_06e17b`, no up
function, down function `sub_06dd9c`. `wlc_detach` calls `wlc_cca_detach`
when `wlc+0x628` is not NULL and clears the field.

The state is a ring of 60 one-second slots per channel for the 38 channels of
a fixed table (2.4 GHz channels 1-14 and the 24 usual 5 GHz channels, all as
20 MHz chanspecs); a slot holds the index of a *sample* in a pool of 450
samples (0 = no sample). Every watchdog tick advances the ring position,
frees that position's samples on all channels, gives the current channel a
fresh sample there and accumulates into it the counter increments since the
last read (`cca_stats_upd(wlc, 1)`). The rest of the driver also calls
`cca_stats_upd`: with 1 before a channel change (`wlc_set_chanspec`: the time
spent on the old channel is booked to it) and in `sub_05c7cc`; with 0 (only
take the counters as the new base) in `wlc_init`, `wlc_roam_complete`,
`wlc_set_ps_ctrl`, `wlc_set_wake_ctrl`. After a channel change
`wlc_set_chanspec` calls `cca_stats_tsf_upd`, which takes the TSF low word
as the new base of the time counter. (`wlc_set_chanspec` skips both calls
under a condition on the state at `wlc+0x2f0`, not described here.)

The iovar `cca_get_stats` (and the upper layer's `sub_079c78` directly,
through `cca_query_stats` and `wlc_cca_get_chanspec`) reads back up to 60
past seconds of one channel, newest first.

Calls out of the file: `wlc_calloc`, `osl_mfree`, `osl_malloc`,
`osl_memset`, `osl_memcpy`, `osl_sysuptime` (allocation and time),
`wlc_module_register`/`wlc_module_unregister` (module service, `wlc.c`),
`wlc_bmac_cca_stats_read` (open MAC, bmac-run R5), `wlc_read_tsf` (`wlc.c`),
`wf_chspec_ctlchspec`, `wf_chspec_malformed` (open, `channel.md` H1/H3).

### Transmit power offsets and spatial policy

The PHY, after every computation of its transmit power targets, calls
`wlapi_high_update_txppr_offset(shim, ppr)`, which calls
`wlc_update_txppr_offset(wlc, ppr)` with the PHY's per-rate power *offsets*
container (`ppr.md`). That function:

1. keeps its own container (`stf+0x30`) of the current channel's bandwidth
   (recreating it when the bandwidth changed),
2. maps the PHY's offsets into it with `ppr_map_vec_all` and the callback
   `sub_172c6c`: each rate's offset halved and limited to a ceiling derived
   from the maximum target power and the `min_txpower` iovar (5,460 callback
   calls in the hybrid session, the hot spot); that iovar does not exist in
   this driver and the object then reads a stack word it never wrote - an
   implementation uses 0 ("The uninitialised stack read"),
3. removes from the transmit chain mask the chains that cannot reach
   `min_txpower` (`wlc_stf_txchain_set`),
4. lets `sub_174cd5` pick the spatial policy of the band/subband
   (`wlc_stf_spatial_policy_set`, which ends in
   `wlc_stf_spatialpolicy_set_complete`: per rate class the transmit core
   mask, the antenna word of the MAC, the shared-memory core words),
5. writes the legacy rates' power offsets into the microcode's rate table in
   shared memory (`wlc_bmac_stf_get/set_rateset_shm_offset`, bmac-run R6).

Who reads the stf state from outside `wlc_stf.c`: see "Data", "The stf
state".

## Data

### Error codes

-14 `BCME_BUFTOOSHORT`, -20 `BCME_BADCHAN`, -23 `BCME_UNSUPPORTED`, -27
`BCME_NOMEM` (`src\include\bcmutils.h`).

### Module service (`wlc.c`, used, not specified here)

`wlc_module_register(pub, iovars, name, hdl, doiovar, watchdog, up, down)`
(eight arguments, the last two on the stack) takes the first free slot of the
module table (`wlc+0x358`, 64 slots of 0x50 bytes, `iovar.md`) and stores:
`+0x00` name (up to 31 characters), `+0x20` iovar table, `+0x28` handle,
`+0x30` watchdog, `+0x38` iovar handler, `+0x40` up, `+0x48` down. It
returns 0, or -22 when all 64 slots are taken. The users of the slots, as far
as they concern this module:

* `wlc_watchdog` (`sub_0450f0`, only while `pub+0x34` "up" is set) calls the
  watchdog of every slot that has one, in slot order, with the slot's handle;
  **if a watchdog returns non-zero, `wlc_watchdog` returns at once** (the
  later slots' watchdogs and the rest of `wlc_watchdog`, e.g.
  `wlc_stf_pwrthrottle_upd`, are skipped for this tick).
* `wlc_down` calls the down function of every slot that has one, with the
  handle, and adds the results to its own result.
* `wlc_iovar_op` calls the iovar handler (`iovar.md`).

`wlc_module_unregister(pub, name, hdl)` frees the slot again.

### The iovar table of the module (`.rodata+0x284880`, 2 entries of 16 bytes)

| name | varid | flags | type | minlen |
|---|---|---|---|---|
| `"cca_get_stats"` | 0 | 0 | 8 (buffer) | 24 (0x18) |
| NULL (end) | | | | |

So the get has action id 0, the set action id 1 (`iovar.md`).

### The channel table (`.rodata+0x2848a0`, 38 u16)

0x1001 .. 0x100e (2.4 GHz channels 1..14, 20 MHz), then 0xd024, 0xd028,
0xd02c, 0xd030, 0xd034, 0xd038, 0xd03c, 0xd040, 0xd064, 0xd068, 0xd06c,
0xd070, 0xd074, 0xd078, 0xd07c, 0xd080, 0xd084, 0xd088, 0xd08c, 0xd095,
0xd099, 0xd09d, 0xd0a1, 0xd0a5 (5 GHz channels 36, 40, 44, 48, 52, 56, 60,
64, 100, 104, ..., 140, 149, 153, 157, 161, 165, 20 MHz). Channel 144 and
all others are not in the table.

### The state `cca_info` (0x4390 bytes, `wlc+0x628`)

The type name `cca_info_t` is declared (only declared) in
`src\wl\sys\wlc_types.h`. Field names are mine.

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0x0000 | 8 | `wlc` | back pointer |
| 0x0008 | 9 x 4 | `last[9]` | the nine counter words of the previous successful `wlc_bmac_cca_stats_read`, same order as its output |
| 0x002c | 38 x 0x7a | `chan[38]` | per channel of the channel table: `+0x00` u16 `chanspec`, `+0x02` u16 `slot[60]` (sample index, 0 = none) |
| 0x1248 | 4 | `cur` | s32, the ring position 0..59 |
| 0x124c | 4 | `nsecs` | s32, the ring length, 60 (set by attach, never changed) |
| 0x1250 | 4 | `nosample` | u32, count of failed sample allocations (pool exhausted); nobody reads it |
| 0x1254 | 450 x 0x1c | `pool[450]` | samples of 7 u32: `+0x00` time, `+0x04` busy-own, `+0x08` busy-other, `+0x0c` interference, `+0x10` timestamp, `+0x14` counter A, `+0x18` counter B (meaning below) |

Address of `chan[c].slot[s]` = state + 0x2e + 0x7a*c + 2*s; of `pool[i]` =
state + 0x1254 + 0x1c*i. The total 0x1254 + 450*0x1c = 0x438c (+4 bytes of
padding).

A pool sample is **free** when its field `+0x04` holds 0xffff. Attach marks
all 450 free, including `pool[0]`, which is never handed out (the search
starts at 1) and never written again: an empty slot (index 0) therefore reads
as the sample {0, 0xffff, 0, 0, 0, 0, 0}. (The marker is also a possible
accumulated value: a sample in use whose `+0x04` reaches exactly 0xffff is
taken as free by the next allocation. An implementation that wants to equal
the object keeps this.)

Meaning of the sample fields, from the arithmetic (words = the output of
`cca_stats_read`, bmac-run R5; "delta" = this read minus `last[]`, 32-bit
wrapping):

| Field | Accumulates | Names (proposed, unverified) |
|---|---|---|
| `+0x00` time | delta of word 5 (TSF low word, microseconds), minus delta of word 6 when power save is enabled (below) | duration |
| `+0x04` busy-own | delta of word 0 + delta of word 1 | congest_ibss (tx + own BSS) |
| `+0x08` busy-other | delta of word 2 + delta of word 3 | congest_obss |
| `+0x0c` interference | delta of word 4 | interference |
| `+0x10` timestamp | (overwritten) `osl_sysuptime()` at the last accumulation, milliseconds | timestamp |
| `+0x14` counter A | delta of word 7 (`SHM(0x10e)`) | purpose unknown |
| `+0x18` counter B | delta of word 8 (`SHM(0x10c)`) | purpose unknown |

"Power save enabled" is the byte at `((wlc+0x2f8)->+0x338)+0x8` (the primary
BSS configuration's power-management state, its "PM enabled" flag -
proposed); read with no NULL check.

**Who reads the state from outside `wlc_cca.c`:** nobody (see
Verification: memory watch over the whole state during a session, and a
static scan). `wlc_attach`/`wlc_detach` only store/clear/test the pointer
`wlc+0x628`; `sub_079c78` uses the state only through `cca_query_stats` and
`wlc_cca_get_chanspec`. The layout is therefore the open code's own; it is
given so that an implementation can reproduce the object's results
(including the quirks) exactly.

### The output of `cca_query_stats` (internal record)

| Offset | Size | Content |
|---|---|---|
| 0x00 | 2 | chanspec |
| 0x02 | 1 | number of seconds reported |
| 0x03 | 1 | not written |
| 0x04 + 0x1c*i | 7 x 4 | second i (0 = newest reported): `(time + 500) / 1000`, `(busy-own + 500) / 1000`, `(busy-other + 500) / 1000`, `(interference + 500) / 1000`, `(timestamp + 500) / 1000`, counter A, counter B (unsigned 32-bit arithmetic: microseconds -> milliseconds, milliseconds -> seconds, rounded) |

### The value of the iovar `cca_get_stats`

Input (`params`): u16 chanspec at +0, u8 number of seconds `n` at +2 (at
least 24 bytes must be passed). Output (`arg`): u16 chanspec, u8 number of
seconds reported, byte +3 not written, then **`n`** records of 20 bytes from
+4: the first five words of the internal record (time, busy-own, busy-other,
interference, timestamp); counters A and B are not passed on.

### The stf state (`wlc+0x550`, 0x120 bytes)

Allocated (zeroed) by `wlc_attach_malloc` (`wlc_calloc(osh, unit, 0x120)`,
error 0x3f9 if it fails), freed by `wlc_detach_mfree`. **It is not private
to `wlc_stf.c`**: some 70 functions of `wlc.c`, `wlc_ampdu.c`,
`wlc_assoc.c`, `wlc_channel.c`, `wlc_lq.c`, `wlc_offloads.c`,
`wlc_olpc_engine.c`, `wlc_rate_sel.c`, `wlc_scb.c`, `wlc_scb_ratesel.c`,
`wlc_txbf.c`, `wlc_vht.c`, `wlc_wowl.c` and the MAC's `wlc_bmac_txant_set`
read or write it directly. The table lists every field the functions of
this specification use, and the other fields found used from outside;
names are proposals (Broadcom style, partly from what the fields do).
"Outside" = functions not in `wlc_stf.c` (from a memory watch of the session
and the soak, and a static scan; see Verification). R read, W write, A
address passed on.

| Offset | Size | Name (proposed) | This spec | Outside `wlc_stf.c` |
|---|---|---|---|---|
| 0x00 | 1 | `hw_txchain` | R `wlc_update_txppr_offset` | R `wlc_attach`, `wlc_watchdog` (`sub_0450f0`: `wlc_stf_txchain_set(wlc, hw_txchain, 0, 4)`) |
| 0x01 | 1 | `txchain` (active transmit cores) | R `sub_172af1`, `..._set_complete` | R `wlc_acphy_txctl0_calc`, `sub_045018`, `sub_0837c5`, `sub_083bfd` (`wlc_olpc_engine.c`), `sub_179ae1`, `wlc_txbf_txchain_upd`; A (`bcm_bitcount`) `sub_031322`, `wlc_txbf_init_link`, `sub_17a812`, `wlc_vht_update_cap` |
| 0x02 | 1 | `txstreams` (`T`) | R nearly all | R `wlc_channel_reg_limits`, `sub_0736a4`, `wlc_set_ratetable`, `wlc_ampdu_tx_attach`, `sub_0527b5`, `wlc_ratesel_attach`, `wlc_vht_init_defaults`, `wlc_vht_update_scbstate`, `wlc_ht_update_scbstate`, `wlc_vht_update_bfr_bfe`, `wlc_set_ratespec_override`, `wlc_parse_rates`, `wlc_get_current_highest_rate`, `sub_02fc08`, `sub_031322`, `wlc_mod_prb_rsp_rate_table`, `wlc_BSSinit_rateset_filter`, `wlc_set_nmode`, `sub_0411e9` (ioctl), `wlc_doiovar`, `sub_04ab93`, `sub_05d4c6`, `wlc_scb_sortrates`, `wlc_scb_reinit`, `wlc_scb_ratesel_init`, `wlc_scb_ratesel_get_primary` |
| 0x03 | 1 | `hw_rxchain` | - | R `wlc_attach`, `wlc_watchdog` |
| 0x04 | 1 | `op_rxstreams` | - | R `wlc_attach`, `wlc_init`, `wlc_default_rateset`, `wlc_ht_build_cap_ie`, `wlc_set_nmode`, `sub_036988`, `wlc_join_BSS` |
| 0x05 | 1 | `rxchain` | - | R `sub_045018`, `wlc_lq_rssi_init`, `wlc_lq_rssi_pktrxh_cal`, `sub_07e4cc`, `wlc_watchdog`; A `wlc_txbf_rxchain_upd`, `sub_179ae1`, `wlc_txbf_init_link`, `wlc_vht_update_cap` |
| 0x06 | 1 | `rxstreams` | - | R `wlc_sendassocreq`, `wlc_get_oper_mode` |
| 0x07, 0x08 | 1, 1 | `ant_rx_ovr`, `txant` | - | W `wlc_info_init` (both), `sub_0411e9` (both); R `sub_0411e9`, `wlc_doiovar` (+0x07) |
| 0x0a | 2 | `phytxant` (antenna word of the transmit header) | W `..._set_complete` | R `wlc_beacon_phytxctl_txant_upd` |
| 0x0c | 1 | `ss_opmode` (single-stream mode: 0 SISO, 1 CDD) | R/W `wlc_stf_ss_update` | R `wlc_rspec_to_rts_rspec_ex`, `wlc_lowest_basic_rspec`, `wlc_scb_ratesel_get_primary`, `sub_031322` (transmit header), `sub_0527b5` (A-MPDU) |
| 0x0d | 1 | `ss_algosel_auto` | R `wlc_stf_ss_update` | R `sub_03649a` |
| 0x0e | 2 | `ss_algo_channel` (bits: 0 SISO, 1 CDD, 2 STBC; 0xffff unknown) | R `wlc_stf_ss_update`; W by `wlc_stf_ss_algo_channel_get` through its pointer | R (bit 2) `sub_031322`, `sub_0527b5`, `wlc_scb_ratesel_get_primary`; A `sub_03649a` (passes it to `wlc_stf_ss_algo_channel_get`) |
| 0x10 | 1 | `no_cddstbc` | R `wlc_stf_ss_update` | W `sub_072da4` (`wlc_channel.c`: 0, or 1 when the locale disables 802.11n) |
| 0x11, 0x12 | 1, 1 | receive-chain restore counter and delay | - | R/W `wlc_watchdog` |
| 0x13, 0x14 | 1, 1 | (purpose unknown) | - | W `wlc_attach` (1, 0xff), `sub_0396ff`; R `wlc_up`, `wlc_doiovar` (+0x14 also W), `sub_031322`, `wlc_get_current_highest_rate`, `wlc_scb_ratesel_get_primary` |
| 0x15 | 1 | column of the txcore table in use | - | - |
| 0x16 + 2i | 1 | `txcore[i].nsts` (i = 0..5) | R `..._set_complete`, `sub_172da9` | R `sub_081696` (`wlc_offloads.c`, copies 0x16..0x21) |
| 0x17 + 2i | 1 | `txcore[i].mask` | W `..._set_complete`; R `wlc_update_txppr_offset` (i = 0, 1), `wlc_stf_txcore_shmem_write`, `sub_172da9` | R `sub_081696` |
| 0x22 + i | 1 | `txcore_override[i]` (i = 0..5; 0 = none) | R `..._set_complete` | W +0x22, +0x23 `sub_083bfd` (`wlc_olpc_engine.c`: both = `txchain`, then `wlc_stf_spatial_policy_set(wlc, spatial_policy)`) |
| 0x28 | 1 | `spatial_policy` | R/W `..._set_complete`; R `wlc_stf_spatial_policy_set` | R `sub_083bfd`; R/W `sub_17e912` (`wlc_wowl.c`: saved and restored around its own policy) |
| 0x29 | 1 | `spatial_policy_pending` | W `wlc_stf_spatial_policy_set`; R/W `..._set_complete` | - |
| 0x2a | 2 | `shmem_base` (word address of the core words) | R `wlc_stf_txcore_shmem_write` | W `wlc_init` (= `pub+0xe4`, when `pub+0x68 & 3`) |
| 0x30 | 8 | `txpwr_ofs` (the stf `ppr_t`) | R/W `wlc_update_txppr_offset`; R `..._set_complete`, `wlc_stf_ss_algo_channel_get` | - |
| 0x44 | 1 | (purpose unknown) | R `wlc_stf_ss_update` | R `wlc_scb_ratesel_init` |
| 0x50, 0x59, 0x76, 0x77, 0x89 | 1 each | (purpose unknown) | - | R `wlc_bcn_prb_body` (+0x50); `wlc_up` (+0x59); `sub_031322` (+0x76, +0x77); `sub_02bdab`, `sub_17a69c` (+0x77); `wlc_ht_update_scbstate`, `wlc_vht_update_scbstate`, `wlc_scb_ratesel_init` (+0x89) |
| 0x48..0x78 | | (purpose unknown) | - | W `wlc_attach` (+0x48, +0x4c, +0x51, +0x52, +0x54, +0x68, +0x6e, +0x70, +0x72, +0x74, +0x78) |
| 0x5f..0x63 | 5 x 1 | `spatial_policy_band[5]` (2.4 GHz; 5 GHz channel <= 51, <= 99, <= 148, above) | R `sub_174cd5` | - (all -1 from `wlc_stf_attach`) |
| 0x64..0x67 | 4 x s8 | `txchain_pwr_offset[4]` (per chain) | R `wlc_update_txppr_offset` | - |
| 0x6c | 2 | `rate_shm_off` (0x0e for MAC rev >= 40, 0x14 below) | R `wlc_update_txppr_offset` | - |
| 0x88 | 1 | `txpwr_ceil` (the ceiling of the offsets, 31 after attach) | W/R `wlc_update_txppr_offset`; R `..._set_complete`, `wlc_stf_ss_algo_channel_get` | - |
| 0x8a | 1 | `core1_first` (one stream prefers core 1) | R `sub_172af1` | W `sub_06f334` (`wlc_channel.c`, every channel set: 1 for a 20 MHz channel when the `phy_afeoverride` value it computed is not 0, else 0) |
| 0x90..0x10f | | software antenna diversity: counters, thresholds, a timer (`+0x108`), enable bytes `+0xf4`, `+0xf5` | - | `wlc_recvdata`, `wlc_dotxstatus`, `wlc_watchdog`, `sub_03067b`, `sub_030702`, `sub_0411e9`, `wlc_attach`, `wlc_timers_init`, `wlc_down`, `wlc_detach`; **`+0xf4` R by `wlc_bmac_txant_set`** (open MAC code, bmac-run R8) |

Consequence for an open implementation of the functions of this
specification: they must work on Broadcom's block in place (or marshal
every field of the column "This spec" in and out around each call), because
other code reads `txchain`, `txstreams`, `phytxant`, `ss_opmode`,
`ss_algo_channel`, `txcore[]`, `spatial_policy` and writes `no_cddstbc`,
`txcore_override[0..1]`, `spatial_policy`, `shmem_base`, `core1_first`
between the calls.

Values on this card after attach and up (object run): `hw_txchain` =
`txchain` = 3, `txstreams` = 2, `hw_rxchain` = 3, `spatial_policy_band[]`
all -1, `txchain_pwr_offset[]` 0, `rate_shm_off` = 0x0e, `shmem_base` =
0x2ea, `txcore[]` nsts 1, 1, 1, 2, 3, 4 with masks 1, 1, 1, 3, 0, 0,
`spatial_policy` -1 (pending 0), `txpwr_ceil` 31, `core1_first` 0 (dump in
`verify.py`'s first line).

### Tables of `wlc_stf.c` used here

* txcore table (`.rodata+0x3083e0`, 6 x 3 bytes {nsts, mask for column 1,
  mask for column 2}): {1, 1, 2}, {1, 1, 2}, {1, 1, 2}, {2, 3, 6}, {3, 7,
  7}, {4, 0x0f, 0x0f} (rate classes CCK, OFDM, 1, 2, 3, 4 streams).
* `cck_ofdm_rates` (`.rodata+0x307910`, a `wlc_rateset_t` of 40 bytes):
  count 12, rates 0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24, 0x30,
  0x48, 0x60, 0x6c, then zeros, bytes 0x26..0x27 = 0xffff.
* `rate_info`: bmac-run R6 (bit 7 marks an OFDM rate).

### Fields of `wlc`, `pub` and the band state read here

| Field | Meaning (as used) |
|---|---|
| `wlc+0x08` | `osh` |
| `wlc+0x20` | `wlc_hw` |
| `wlc+0x31` | `clk` (u8: set by `wlc_up`, cleared by `wlc_down`; proposed name) |
| `wlc+0x38` | the core-state block; `+0x38..0x3e` s16 transmit-pending counts of FIFOs 0..3 |
| `wlc+0x40` | current band state |
| `wlc+0x2a8` | u32 transmit-header-cache generation (`iovar.md`) |
| `wlc+0x2f8` | primary BSS configuration (`+0x338` its power-management state, `+0x8` "PM enabled") |
| `wlc+0x304` | u8 flags; bit 6 = "spatial policy change pending" |
| `wlc+0x518` | the upper layer's current chanspec (read as 16 or 32 bits; only the low 16 bits are the chanspec) |
| `wlc+0x550` | the stf state |
| `wlc+0x584` | u32, the beacon's rate (passed to `wlc_valid_rate` and the beacon functions) |
| `wlc+0x628` | the cca state |
| `wlc+0x690` | the coexistence module's state (`+0x14` u16 written to `SHM(0x5de)`) |
| `wlc+0x750` | the offload engine's state (passed to `wlc_ol_curpwr_upd`) |
| `pub+0x10` | unit; `pub+0x14` MAC core revision; `pub+0x34` up |
| `pub+0x9c` | `boardflags2` (bits 12 and 14 used here, as the bytes `pub+0x9d` bits 4 and 6) |
| `pub+0xe4`, `pub+0xe6` | u16 word addresses of the core words in shared memory: 0x2ea and 0x360 on MAC rev 42 (`wlc_attach`: 0x374 for rev < 40; 0x530 for rev < 30, else 0x360 + 0xc70 for rev < 42) |
| `pub+0x100` | `sih` (`+0x30` board vendor, `+0x3c` chip id) |
| band `+0x00` | u32 band type (1 = 5 GHz, 2 = 2.4 GHz) |
| band `+0x08` | u16 PHY type (0xb AC, 7 HT) |
| band `+0x10` | `pi` |
| band `+0x58` | u8 the band's single-stream mode (`wlc_stf_attach`: 0 for the 2.4 GHz band unless the PHY type is 7 or 4 and its hardware power control is off; 1 for 5 GHz) |

## Procedures, part 1: `wlc_cca.c`

All functions take or reach `wlc`; `cca` = `wlc+0x628` (8 bytes).

### wlc_cca_attach (.text+0x06e309, name original)

`cca_info *wlc_cca_attach(wlc_info *wlc)`

1. If `pub->corerev` (`pub+0x14`, `pub` = `wlc+0x00`) is 14 or less
   (unsigned): return NULL (nothing allocated).
2. `cca = wlc_calloc(osh, unit, 0x4390)` with `osh` = `wlc+0x08`, `unit` =
   `pub+0x10` (32 bit); NULL: return NULL.
3. `cca->wlc = wlc`; `nsecs` = 60; `cur` = 0; `last[0..8]` = 0
   (`osl_memset(cca+8, 0, 0x24)`).
4. For c = 0..37: `chan[c].chanspec` = channel table[c] (the slots stay 0
   from the calloc).
5. For i = 0..449: `pool[i]+0x04` = 0xffff (free).
6. `wlc_module_register(pub, iovar table, "cca_stats", cca, sub_06e3fe,
   sub_06e17b, NULL, sub_06dd9c)`. If it returns non-zero:
   `osl_mfree(osh, cca, 0x4390)`, return NULL.
7. Return `cca`.

On this card `corerev` is 42: the module exists.

### wlc_cca_detach (.text+0x06e2d2, name original)

`void wlc_cca_detach(cca_info *cca)` - no NULL check (the caller checks):

1. `w = cca->wlc`; `wlc_module_unregister(w->pub, "cca_stats", cca)`.
2. `osl_mfree(w+0x08 osh, cca, 0x4390)`.

### sub_06dd9c (.text+0x06dd9c, name assigned: `wlc_cca_down`)

`int down(cca_info *cca)` - the module's down function:

1. If `cca` is NULL: return 0.
2. For s = 0 .. `nsecs`-1 (signed loop), for c = 0..37: if
   `chan[c].slot[s]` = i is not 0: `pool[i]+0x04` = 0xffff, `chan[c].slot[s]`
   = 0.
3. Return 0.

All samples are freed; `cur`, `last[]`, `nosample` and the contents of the
freed samples stay.

### sub_06e17b (.text+0x06e17b, name assigned: `wlc_cca_watchdog`)

`int watchdog(cca_info *cca)` - the module's watchdog, once per tick:

1. If `cca` is NULL: return 0.
2. `w = cca->wlc`; `cs = wf_chspec_ctlchspec(w+0x518)` (the current
   chanspec of the upper layer, u16; its 20 MHz control channel).
3. Advance the ring: `cur` = 0 if `cur` = `nsecs`-1, else `cur` + 1 (stored
   at once).
4. Find c = the first channel with `chan[c].chanspec` = `cs`. None: return
   **-20** (the ring stays advanced; nothing else happens - and
   `wlc_watchdog` ends its tick there, see "Module service").
5. Free position `cur` on all channels: for c' = 0..37, if
   `chan[c'].slot[cur]` = i is not 0: `pool[i]+0x04` = 0xffff,
   `chan[c'].slot[cur]` = 0.
6. Allocate: the first i in 1..449 with `pool[i]+0x04` = 0xffff:
   `osl_memset(&pool[i], 0, 0x1c)`, `chan[c].slot[cur]` = i. None:
   `nosample` += 1.
7. `cca_stats_upd(w, 1)`; return 0.

### cca_stats_upd (.text+0x06df2e, name original)

`void cca_stats_upd(wlc_info *wlc, int calc)`:

1. `cs = wf_chspec_ctlchspec(wlc+0x518)` (called before anything else).
2. If `cca` is NULL: return. Find c = the first channel with
   `chan[c].chanspec` = `cs`; none: return.
3. `r = wlc_bmac_cca_stats_read(wlc+0x20 wlc_hw, v)` with `v` = 9 u32 on the
   stack; if `r` is not 0: return (the MAC's function always returns 0).
4. If `calc` is not 0:
   1. If `chan[c].slot[cur]` is 0: allocate as in the watchdog's step 6
      (first free i from 1, zero it, store; none: `nosample` += 1).
   2. i = `chan[c].slot[cur]`; if i is not 0, with `p` = `pool[i]` and
      `d[k]` = `v[k] - last[k]`:
      * `p.time` += `d[5]`; then, if the power-save flag (Data) is set,
        `p.time` -= `d[6]`;
      * `p.busy_own` += `d[0] + d[1]`;
      * `p.busy_other` += `d[2] + d[3]`;
      * `p.interference` += `d[4]`;
      * `p.counterA` += `d[7]`; `p.counterB` += `d[8]`;
      * `p.timestamp` = `osl_sysuptime()` (the last of these writes).
5. `last[k]` = `v[k]` for k = 0..8 (also when `calc` is 0, and also when the
   allocation failed).

All arithmetic is 32-bit wrapping.

### cca_stats_tsf_upd (.text+0x06e2a3, name original)

`void cca_stats_tsf_upd(wlc_info *wlc)`: if `cca` is not NULL:
`wlc_read_tsf(wlc, &lo, &hi)` (two u32 on the stack), `last[5]` = `lo`.
(The time spent changing the channel is so not booked to the new channel.)

### cca_query_stats (.text+0x06ddf2, name original)

`int cca_query_stats(wlc_info *wlc, chanspec cs, int n, void *out, int size)`
(`cs` compared as 16 bits; `size` used unsigned):

1. If `cca` is NULL: return 0 (`out` not written).
2. `m` = `n` if `n` <= `nsecs` (signed), else `nsecs`; `pos` = `cur`.
3. Find c = the first channel with `chan[c].chanspec` = `cs`. None: `out+2`
   (u8) = 0, `out+0` (u16) = 0, return 0.
4. `out+0` (u16) = `cs`; `room` = `size` - 4 (unsigned); k = 0.
5. While `room` > 27 (unsigned) and k < `m`:
   1. If `pos` is 0: `pos` = `nsecs`; `pos` -= 1.
   2. `room` -= 28; `p` = `pool[chan[c].slot[pos]]` (index 0 gives the
      constant `pool[0]`, Data).
   3. Write record k (Data, "The output of `cca_query_stats`").
   4. k += 1.
6. `out+2` (u8) = k; return 0.

So the newest second reported is the one *before* the current ring position
(`cur`-1, wrapping), then backwards.

### sub_06e3fe (.text+0x06e3fe, name assigned: `wlc_cca_doiovar`)

The module's iovar handler, called by `wlc_iovar_op` as `handler(hdl,
vi, actionid, name, params, p_len, arg, len, val_size, wlcif)` (`iovar.md`);
here `hdl` = `cca`, `p_len` u32, `len` s32 (stack):

1. If `cca` is NULL: return 0.
2. If `p_len` > 3 (unsigned) the object copies the first 4 bytes of `params`
   into a local that is never used (`osl_memcpy`; no observable effect).
3. If `actionid` is not 0: return -23 (a set of `cca_get_stats`).
4. If `len` < 24 (signed) or `p_len` < 24 (unsigned): return -14.
5. If `wf_chspec_malformed(params+0 u16)`: return -20.
6. `cs = wf_chspec_ctlchspec(params+0 u16)`; `n` = `params+2` (u8);
   `size` = 0x20 if `n` = 0, else (`n`-1)*0x1c + 0x20.
7. `buf = osl_malloc(osh, size)` (`osh` = `cca->wlc+0x08`); NULL: return
   -27. `osl_memset(buf, 0, size)`.
8. `r = cca_query_stats(cca->wlc, cs, n, buf, size)` (always 0). If `r` is
   0: `arg+0` (u16) = `buf+0` (u16); `arg+2` (u8) = `buf+2`; for k = 0 ..
   `n`-1 (the **requested** count, not the reported one): the 5 words at
   `buf + 4 + 0x1c*k` (time, busy-own, busy-other, interference, timestamp)
   to `arg + 4 + 0x14*k`. (Records beyond the reported count are zeros from
   the memset.)
9. `osl_mfree(osh, buf, size)`; return `r`.

Note: the handler writes 4 + 20`n` bytes into `arg` but only checks `len` >=
24: with `n` >= 2 and a short value buffer the object writes past it (the
`wl` tool and the glue pass large buffers). Through `wlc_iovar_op` the
length check of `wlc_iovar_check` (minlen 24) presumably rejects a short
value buffer before the handler is reached (unverified; the direct calls
of Verification bypass it).

### wlc_cca_get_chanspec (.text+0x06dd7c, name original)

`chanspec wlc_cca_get_chanspec(wlc_info *wlc, int i)`: 0 if `cca` is NULL,
else `chan[i].chanspec` (i sign-extended, not bounded: an index outside
0..37 reads whatever lies there).

## Procedures, part 2: `wlc_stf.c`

Notation: `stf` = the pointer at `wlc+0x550` (re-read by the object at
almost every use; nothing in these functions changes it); `pub` =
`wlc+0x00`; `band` = the current band state, `wlc+0x40` (`band+0x00` u32
band type: 1 = 5 GHz, 2 = 2.4 GHz; `band+0x08` u16 PHY type, 0xb = AC;
`band+0x10` the PHY handle `pi`); `cs` = the upper layer's current chanspec
`wlc+0x518`; `bw(cs)` = 2 if `cs & 0x3800` = 0x2000 (80 MHz), 1 if = 0x1800
(40 MHz), else 0 (`wl_tx_bw_t` of `ppr.md`); `osh` = `wlc+0x08`; `wlc_hw` =
`wlc+0x20`. `T` = `stf->txstreams` (`stf+0x02`), `txchain` = `stf+0x01`.
Field names of the stf state: Data, "The stf state".

### sub_172c6c (.text+0x172c6c, name assigned: `wlc_stf_txppr_offset_mapfn`)

The callback of `ppr_map_vec_all` (callback type: `ppr.md`, Data), `void
fn(u8 *ceil, u8 *a, u8 *b)`: called for every rate with `a` the PHY's offset
of that rate and `b` the same rate's byte in the stf container:

* if `*a` = 0x80 (rate disabled): `*b` = 0x80;
* else `*b` = the smaller of `*a >> 1` (the byte as **unsigned**, shifted
  logically: 0..127) and `*ceil` (unsigned).

So a negative offset (0x81..0xff) becomes 64..127 and is then limited to the
ceiling (at most 31 here). `*a` and `*ceil` are not written.

### wlc_update_txppr_offset (.text+0x175513, name original)

`void wlc_update_txppr_offset(wlc_info *wlc, ppr_t *ofs)`; called only by
`wlapi_high_update_txppr_offset(shim, ofs)` (the PHY's service into the MAC,
`wlc_phy_shim.c`), `ofs` = the PHY's per-rate offsets container. Observed 11
calls in the hybrid session (`mpc 0` - the first `up` -, channels 1, 6, 11,
36, 36/40, 36/80, 149/80, 157/40, the down, the second up).

1. `min4` = 4 x the value of the iovar `min_txpower`, read with
   `wlc_iovar_getint(wlc, "min_txpower", &v)`. **The driver has no iovar of
   that name: the call always returns -23 and leaves `v` as it was, and the
   object never initialises `v`** (see "The uninitialised stack read" below).
   An implementation uses `v` = 0, so `min4` = 0. (The lookup itself has no
   effect besides its result: `iovar.md`, step 1.)
2. `tmax` = `wlc_phy_txpower_get_target_max(pi)` (s8, quarter dBm).
3. `wlc_ol_curpwr_upd(wlc+0x750, tmax sign-extended, cs as u16)` (the
   offload engine's copy of the power; `wlc_offloads.c`, a service).
4. If `stf->ppr` (`stf+0x30`) is not NULL and `ppr_get_ch_bw(stf->ppr)` !=
   `bw(cs)`: `ppr_delete(osh, stf->ppr)`, `stf->ppr` = NULL.
5. If `stf->ppr` is NULL: `stf->ppr = ppr_create(osh, bw(cs))`; if that is
   NULL: return (nothing else is done).
6. **Ceiling**: `c` = the low byte of `tmax` minus the low byte of `min4`,
   as a signed 8-bit number (wrapping), shifted right by 1 arithmetically;
   if `c` < 0: 0; if `c` > 31: 31. `stf+0x88` = `c`; then
   `ppr_map_vec_all(sub_172c6c, &c, ofs, stf->ppr)` (`c` one byte on the
   stack; the PHY's container first). With `min4` = 0: `c` =
   clamp(`tmax` >> 1, 0, 31).
7. **Chains that cannot reach the minimum**: `tmin` =
   `wlc_phy_txpower_get_target_min(pi)` (s8); `mask` = `stf->hw_txchain`
   (`stf+0x00`); for i = 0..3: if `tmin` + `stf+0x64+i` (s8) < `min4`
   (signed 32-bit): clear bit i of `mask`.
8. `wlc_stf_txchain_set(wlc, mask, 1, 3)` (`mask` as an int 0..255; force
   1; the last argument 3 is the requester id of the call - a service, not
   specified here).
9. `sub_174cd5(wlc, cs as u16)` (the spatial policy of the band, below).
10. Return if `stf->ppr` is NULL (re-read), or if `wlc+0x31` (`clk`, set by
    `wlc_up`, cleared by `wlc_down`) is 0, or if `pub->corerev` <= 21.
11. `rs` = a copy of the rate set `cck_ofdm_rates` (`wlc_rateset_copy`, 40
    bytes: u32 count 12, then the rate bytes 0x82, 0x84, 0x8b, 0x0c, 0x12,
    0x96, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c; i.e. 1, 2, 5.5, 6, 9, 11, 12,
    18, 24, 36, 48, 54 Mbit/s, bit 7 = basic rate).
12. **DSSS powers**: `n` = `bcm_bitcount(&stf->txcore[0].mask, 1)` (the CCK
    core mask, `stf+0x17`); `dbw` = 4 (20in80) if `bw(cs)` = 2, 3 (20in40)
    if 1, else 0. If `n` is 1, 2 or 3: `ppr_get_dsss(stf->ppr, dbw, n,
    dsss)` (4 bytes). Otherwise `dsss` is not written (see below).
13. **OFDM powers**: `n` = `bcm_bitcount(&stf->txcore[1].mask, 1)`
    (`stf+0x19`); `n` = 1: `ppr_get_ofdm(stf->ppr, bw(cs), 0, 1, ofdm)`; 2:
    `(stf->ppr, bw(cs), 2, 2, ofdm)`; 3: `(stf->ppr, bw(cs), 2, 3, ofdm)`
    (mode 2 = CDD); otherwise `ofdm` (8 bytes) not written. Note that the
    OFDM rates use the set of the channel's own bandwidth while the DSSS
    rates use the 20-in-X set.
14. `buf = osl_malloc(osh, 6 * count)` (72 bytes); NULL: return.
    `osl_memset(buf, 0, 6 * count)`.
15. `wlc_bmac_stf_get_rateset_shm_offset(wlc_hw, &rs, stf->rate_shm_off
    (stf+0x6c, u16), buf)` (bmac-run R6: per rate the shared-memory entry
    and its current word at entry + `stf+0x6c`, 0x0e on this card).
16. For k = 0 .. count-1, with two cursors into `dsss` and `ofdm` (both from
    their first byte): `r` = `rs.rates[k] & 0x7f`; if bit 7 of
    `rate_info[r]` is set (an OFDM rate, the table of bmac-run R6): `p` = the
    next byte of `ofdm`, else the next byte of `dsss`. If `p` = 0x80: `p` =
    `stf+0x88` (the ceiling). Then, for `pub->corerev` > 39 (this card):
    `buf[k].word` (u16 at `buf + 6k + 4`) = `((p & 0x3f) << 3) |
    (buf[k].word & 0xfe07)`; older cores: `buf[k].word` = `p`. So the rates
    are taken in the order of `rs`: 1, 2, 5.5 Mbit/s = dsss[0..2], 6, 9 =
    ofdm[0..1], 11 = dsss[3], 12 .. 54 = ofdm[2..7].
17. `wlc_bmac_stf_set_rateset_shm_offset(wlc_hw, &rs, stf+0x6c, buf)`
    (writes each word back), `osl_mfree(osh, buf, 6 * count)`.

#### The uninitialised stack read

Measured with a probe that marks the 8 KB below the entry stack pointer as
unwritten at every entry, marks every byte written by an instruction or by
a memory import, and reports reads of unwritten bytes
(`re-out\analysis\cca-stf\uninit.py`): in all 11 calls of the session
exactly **one** such read happens in the whole call tree - the 4 bytes at
entry rsp - 0x30 (frame offset rbp - 0x28: the `v` of step 1), read by the
multiplication by 4 at `wlc_update_txppr_offset+0x30`, right after
`wlc_iovar_getint` returned -23 without writing it. In the emulated session
the bytes held stale heap addresses (0x100eb14c, ...).

What this does: `min4` enters the ceiling (step 6) and the chain test
(step 7), hence the stf container, the spatial policy (the CCK and OFDM core
masks, written to `SHM(0x5d4)`/`SHM(0x5d6)` by
`wlc_stf_txcore_shmem_write`), the chain mask and the rate words of step 16
(`SHM(0x0e)` in the model). The whole hybrid session run on the object with
the stack below the entry filled with 0xaa instead of 0x00 gives 41,320
instead of 41,416 accesses (`SHM(0x5d4)`, `SHM(0x5d6)` = 3 instead of 1; the
rate words 0 instead of 16 and 32); filled with 0xaa **and** `v` set to 0
before the call it is identical to the zero fill, access for access
(`re-out\analysis\cca-stf\fillcmp.py`). So `v` is the only stack value the
object depends on, and **an implementation equals the object with a zeroed
stack by taking `min_txpower` as 0** (`min4` = 0).

In the real driver (no zeroing) the value is whatever the code before left
on the stack, so the object's ceiling and chain mask there are undefined;
0 is also the value the code's intent suggests (no minimum configured).

Two more buffers can stay unwritten: `dsss` (step 12) when the CCK core mask
has 0 or more than 3 bits, and `ofdm` (step 13) likewise for the OFDM core
mask. That never happened: the probe found no read of them in the session,
and in the soak the CCK and OFDM masks after every call were 1 or 3 (with at
most 3 cores a mask has 0..3 bits; 0 is prevented for these two classes by
step 4.6 of `wlc_stf_spatialpolicy_set_complete` as long as `txchain` is not
0). With a zeroed stack they would read 0: an implementation uses 0 for them
(the rate words then keep only `old & 0xfe07`); `verify.py` checked this on
778 calls with such masks.

### sub_174cd5 (.text+0x174cd5, name assigned: `wlc_stf_spatial_mode_upd`)

`void f(wlc_info *wlc, u16 chanspec)`: picks the spatial policy configured
for the band or 5 GHz subband and applies it:

1. If `chanspec & 0xc000` = 0 (2.4 GHz): `p` = `stf+0x5f`.
2. Else, with `ch` = the low byte of the **current** chanspec `wlc+0x518`
   (the band is taken from the argument, the channel number from the
   field; the two callers pass `wlc+0x518` itself or the chanspec being
   set): `ch` <= 51: `stf+0x60`; <= 99: `stf+0x61`; <= 148: `stf+0x62`;
   else `stf+0x63` (unsigned byte compares).
3. `wlc_stf_spatial_policy_set(wlc, p sign-extended)`; its result is
   dropped.

All five bytes are -1 after attach ("auto"), so on this card every call
asks for policy -1.

### wlc_stf_spatial_policy_set (.text+0x174c0a, name original)

`int wlc_stf_spatial_policy_set(wlc_info *wlc, int policy)` (only the low
byte of `policy` is used):

1. If `band->phytype` (u16) is neither 0xb (AC) nor 7 (HT): return -23.
2. `stf->spatial_policy_pending` (`stf+0x29`) = `policy`.
3. If it equals `stf->spatial_policy` (`stf+0x28`), or if the four
   transmit-pending counters of FIFOs 0..3 are all idle - the sum of the
   s16 at `(wlc+0x38)+0x38, +0x3a, +0x3c, +0x3e` is 0:
   `wlc_stf_spatialpolicy_set_complete(wlc)` now.
4. Else: `wlc+0x304` |= 0x40 (the completion is left to
   `wlc_txfifo_complete`, which calls `wlc_stf_spatialpolicy_set_complete`
   when the FIFOs have drained; not described here).
5. Return 0.

### wlc_stf_spatialpolicy_set_complete (.text+0x1740c4, name original)

`void wlc_stf_spatialpolicy_set_complete(wlc_info *wlc)`: computes per rate
class i = 0..5 (0 CCK, 1 OFDM, 2 one stream, 3 two streams, 4 three, 5
four) the transmit core mask `stf->txcore[i].mask` (`stf+0x17+2i`), sets the
MAC's transmit antenna word and writes the core words to shared memory.

1. `stf->spatial_policy` = `stf->spatial_policy_pending`; then
   `stf->spatial_policy_pending` = 0. Let `pol` = the new policy (byte).
2. Default core masks `def[0..5]`:
   * `pol` = 0: `def[i]` = `sub_172af1(stf, N[i])` with `N` = 1, 1, 1, 2,
     3, 4 (the first column of the txcore table, Data).
   * `pol` = 0xff (-1, "auto"): the power comparison below gives a stream
     count `n[i]` per class; `def[i]` = `sub_172af1(stf, n[i])`.
   * any other `pol`: `def[i]` = `txchain` for all six.
3. **Auto** (`pol` = 0xff). `n[0..5]` = 1, 1, 1, 2, 3, 4. If `stf->ppr` is
   not NULL and `T` >= 2, with `P` = `stf->ppr`, `fix(x)` = `stf+0x88` if
   the byte `x` is 0x80 else `x` (a group the container does not have reads
   as all 0x80, `ppr.md`), `umax(g)` = the largest byte of the group
   `g` compared as **unsigned** 8-bit numbers (after `fix`), and the
   bandwidth `w` = `bw(cs)` (`cs` read as 32 bits here):
   1. CCK: `dw` = 4 if `w` = 2, 3 if `w` = 1, else 0;
      `g1 = ppr_get_dsss(P, dw, 1)`, `g2 = ppr_get_dsss(P, dw, 2)`, and if
      `T` = 3: `g3 = ppr_get_dsss(P, dw, 3)` (4 bytes each; `fix` applied to
      every byte of `g1`, `g2` and, if `T` = 3, `g3`). `A` = -`umax(g1)`,
      `B` = 6 - `umax(g2)`; if `B` > `A`: `n[0]` = 2. If `T` != 2: `C` = 10 -
      `umax(g3)`; if `C` > `A` and `C` > `B`: `n[0]` = 3.
   2. OFDM: the same with `g1 = ppr_get_ofdm(P, w, 0, 1)`, `g2 =
      ppr_get_ofdm(P, w, 2, 2)`, `g3 = ppr_get_ofdm(P, w, 2, 3)` (8 bytes)
      -> `n[1]`.
   3. One stream: the same with `ppr_get_vht_mcs(P, w, nss 1, mode 0,
      chains 1)`, `(P, w, 1, 2, 2)`, `(P, w, 1, 2, 3)` (10 bytes) ->
      `n[2]`. When `T` = 2 the procedure goes to step 4 right after the `B`
      test of this class.
   4. Two streams (only if `T` != 2): `h3 = ppr_get_vht_mcs(P, w, 2, 0, 3)`
      then `h2 = ppr_get_vht_mcs(P, w, 2, 0, 2)` (SDM, 10 bytes, `fix`
      applied); if 4 - `umax(h3)` > -`umax(h2)`: `n[3]` = 3.

   All comparisons are on signed integers. (The offsets of the stf
   container are in 0.5 dB steps - the PHY's quarter-dB offsets halved by
   the callback - so 6 and 10 are the 3 dB and 5 dB array gains of two and
   three chains; interpretation, unverified.) `T` > 3 would read groups that
   were not fetched; not reachable on this chip.
4. **Per class**, i = 0..5 (`d` = `def[i]`):
   1. `m` = `stf+0x22+i` (a per-class override); if it is 0: `m` = `d`.
   2. If i = 0 and bit 12 of `pub->boardflags2` (`pub+0x9d` bit 4) is set
      and `band->phytype` = 0xb: `m` = 1 (CCK on core 0 only).
   3. If `stf->txcore[i].nsts` (`stf+0x16+2i`) > `T` (unsigned): `m` = 0.
   4. If `bcm_bitcount(&m, 1)` > `T`: `m` = 0. (The object then tests the
      same condition a second time on a copy; no effect.)
   5. If `bcm_bitcount(&m, 1)` = `T` and (`m` has a bit outside `txchain`,
      or `m & txchain` = 0): `m` = `txchain`.
   6. If i <= 1 and `m` = 0: if `txchain` is not 0, `stf->txcore[i].mask` =
      `txchain` and `m` = `txchain` (if `txchain` is 0 the mask is not
      written). Otherwise: `stf->txcore[i].mask` = `m`.
   7. If (i = 1 and `band->bandtype` = 1) or (i = 0 and `band->bandtype` =
      2) - the class of the band's basic rates -:
      * `stf->phytxant` (`stf+0x0a`, u16) = `m << 6`;
        `wlc_bmac_txant_set(wlc_hw, stf->phytxant)` (bmac-run R8);
      * if `wlc+0x31` (`clk`) is not 0 and `wlc_valid_rate(wlc,
        wlc+0x584 (u32, the beacon rate), band->bandtype, 0)` returns
        non-zero: `wlc_suspend_mac_and_wait(wlc)`,
        `wlc_beacon_phytxctl_txant_upd(wlc, wlc+0x584)`,
        `wlc_beacon_phytxctl(wlc, wlc+0x584, cs as u16)`,
        `wlc_enable_mac(wlc)`.
   8. `wlc+0x2a8` += 1 (u32; the transmit header cache generation, as in
      `iovar.md` - once per class).
5. `wlc_stf_txcore_shmem_write(wlc, 0)`, `wlc_txc_upd(wlc)`, `wlc+0x2a8`
   += 1.

On this card (`hw_txchain` = `txchain` = 3, `T` = 2): classes 0..2 get 1
(core 0) or 3 (both cores) by the power comparison, class 3 gets 3, classes
4 and 5 get 0 (`nsts` > `T`).

### sub_172af1 (.text+0x172af1, name assigned: `wlc_stf_txcore_default`)

`u8 f(stf *stf, u32 nsts)`: the default core mask for `nsts` streams, from
`txchain`:

* `nsts` > `T` (unsigned): 0.
* `nsts` = 1: if `stf+0x8a` is 0: core 0 (1) if it is in `txchain`, else
  core 1 (2) if it is, else 4. If `stf+0x8a` is not 0: core 1 (2) if in
  `txchain`, else core 2 (4) if in `txchain`, else 1.
* `nsts` = 2: 5 if cores 0 and 2 are both in `txchain`, else 3 if cores 0
  and 1 are, else 6.
* any other `nsts` (0, 3, 4, ... up to `T`): 7.

### wlc_stf_txcore_shmem_write (.text+0x172e11, name original)

`void wlc_stf_txcore_shmem_write(wlc_info *wlc, bool force)`:

1. Return unless `force` or `wlc+0x31` (`clk`) is not 0.
2. Return if `pub->corerev` <= 25, or `band->phytype` is neither 0xb nor 7.
3. `base` = `stf+0x2a` (u16, a word address); return unless it equals
   `pub+0xe4` (u16; 0x2ea for core revisions >= 40) or `pub+0xe6` (u16;
   0x360 for revisions >= 42: the copy used while the WoWL microcode runs;
   both set by `wlc_attach`).
4. For i = 0..4 (five classes; class 5 is not written): `v` =
   `stf->txcore[i].mask & 0x0f`; for an HT PHY (`band->phytype` = 7) `v` |=
   `sub_172da9(wlc, i) << 8`; `wlc_write_shm(wlc, 2*base + 2*i, v)`.
5. If the board vendor (`(pub+0x100)->+0x30`, `sih`) is 0x106b and the chip
   (`sih+0x3c`) is 0x4360 or 0x4331: `wlc_write_shm(wlc, 0x5de, u16 at
   (wlc+0x690)+0x14)` (a value of the coexistence module's state).

On this card: `SHM(0x5d4)`, `0x5d6`, `0x5d8`, `0x5da`, `0x5dc` = the masks
of classes 0..4, then `SHM(0x5de)`. Callers: `wlc_init` (force 1, observed
twice in the session), `wlc_stf_spatialpolicy_set_complete` (force 0),
`wlc_stf_wowl_upd`.

### sub_172da9 (.text+0x172da9, name assigned: `wlc_stf_spatial_expansion_idx`)

`u8 f(wlc_info *wlc, u8 i)`: with `b` = `bcm_bitcount(&stf->txcore[i].mask,
1)` and `s` = `stf->txcore[i].nsts`: 0 if `s` > `T`, `b` = 0 or `b` = `s`;
else 1 for (`b`, `s`) = (2, 1); 2 for (3, 1); 3 for (3, 2); 0 otherwise.
Used only for an HT PHY in `wlc_stf_txcore_shmem_write` and by
`wlc_stf_spatial_expansion_get` (not on the AC path).

### wlc_stf_ss_update (.text+0x173c97, name original)

`int wlc_stf_ss_update(wlc_info *wlc, wlcband *bs)` (`bs` a band state,
`wlc+0x50` or `wlc+0x58`); returns 0:

1. `old` = `stf->ss_opmode` (`stf+0x0c`).
2. If bit 1 of `wlc_phy_cap_get(pi)` is set and `stf+0x0d` (`ss_algosel_auto`)
   is not 0 and `stf+0x0e` (u16 `ss_algo_channel`) is not 0xffff: `new` = 0
   if `stf+0x10` (`no_cddstbc`) is not 0 or `T` = 1, else (bit 0 of
   `stf+0x0e`) XOR 1.
3. Otherwise: if `bs` is not the current band (`wlc+0x40`): **return 0 at
   once** (no further step). Else `new` = 0 if `stf+0x10` is not 0 or `T` =
   1, else `bs+0x58` (u8, the band's single-stream mode).
4. If `new` != `old`: `stf->ss_opmode` = `new`;
   `wlc_bmac_band_stf_ss_set(wlc_hw, new)`.
5. `wlc_mhf(wlc, 3, 0x40, f, 0)` with `f` = 0 if `stf+0x44` is 0 and bit 14
   of `pub->boardflags2` (`pub+0x9d` bit 6) is set, else 0x40 (host flags
   word 3, bit 6; `bands` 0 = the current band).

### wlc_stf_ss_algo_channel_get (.text+0x175911, name original)

`void wlc_stf_ss_algo_channel_get(wlc_info *wlc, u16 *out, u32 chanspec)`;
the caller (`sub_03649a`, the channel set of the upper layer, and
`sub_175a7e`) passes `&stf->ss_algo_channel`:

1. `*out` = 0.
2. If `pub+0x34` (up) is 0: `*out` = 0xffff, return.
3. `w` = `bw(chanspec)`; `a` = byte 0 (MCS 0) of `ppr_get_ht_mcs(stf->ppr, w,
   nss 1, mode 0, chains 1)`; 0x80: `a` = `stf+0x88`.
4. If `T` <= 1: `b` = `c` = 0. Else `b` = byte 0 of `ppr_get_ht_mcs(P, w, 1,
   2, 2)` (CDD), `c` = byte 0 of `ppr_get_ht_mcs(P, w, 2, 1, 2)` (STBC),
   each 0x80 -> `stf+0x88`.
5. Signed bytes: if `T` = 1 or `a` + 6 < `b`: set bit 0 of `*out` (SISO),
   else set bit 1 (CDD). If `T` > 1 and `a` + 6 >= `c`: set bit 2 (STBC).
   (Bit operations on the low byte only.)

## Verification

All scripts are in `re-out\analysis\cca-stf\` and run the object alone in
the emulator (Docker image `bcm4360-emu`, from `tools\re`'s `hybrid.Run()`;
`STACK_ZEROED` as in `hybrid.py` unless said otherwise).

### What runs in a session (`observe.py`)

The `hybrid.py` session (attach, `mpc 0`, up, channels 1, 6, 11, 36, 36/40,
36/80, 149/80, 157/40, 165, watchdog, down, up, 36/80, watchdog, down,
detach), object alone, 154,594 accesses:

| Function | Calls | Observed |
|---|---|---|
| `wlc_cca_attach` / `wlc_cca_detach` | 1 / 1 | attach, detach |
| `sub_06dd9c` (down) | 2 | both downs |
| `sub_06e17b` (watchdog) | 4 | result 0 every time |
| `cca_stats_upd` | 15 | 13 with `calc` 1 (channel changes, watchdog), 2 with 0 (`wlc_init`) |
| `cca_stats_tsf_upd` | 9 | after each channel change |
| `sub_06e3fe`, `cca_query_stats`, `wlc_cca_get_chanspec` | 0 | (direct calls only) |
| `wlc_update_txppr_offset` | 11 | ceiling 31, 29, 27, 25, 21; CCK/OFDM core masks 1 or 3 afterwards; the container re-created on every bandwidth change |
| `sub_172c6c` | 5,460 | 182 per 20 MHz, 546 per 40 MHz, 1,092 per 80 MHz update (as `ppr.md` predicts) |
| `sub_174cd5`, `wlc_stf_spatial_policy_set`, `wlc_stf_spatialpolicy_set_complete` | 12 each | policy -1 eleven times, 0 once (during attach, before `wlc_stf_attach` set the band bytes to -1) |
| `sub_172af1` | 72 | 6 per completion |
| `wlc_stf_txcore_shmem_write` | 14 | force 0 with `clk` 1 x11, force 1 (`wlc_init`) x2, force 0 with `clk` 0 x1 (attach: nothing written) |
| `sub_172da9` | 0 | (HT PHY only; direct calls) |
| `wlc_stf_ss_update` | 15 | `ss_opmode` 1 x11, 0 x4 |
| `wlc_stf_ss_algo_channel_get` | 11 | result 0x6 x7, 0xffff x2 (not up), 0x1 x2 |

The core words appear as `SHM(0x5d4)`..`SHM(0x5de)` writes in the order of
`wlc_stf_txcore_shmem_write` (e.g. trace #52637..#52657: 1, 1, 3, 3, 0, 0).

The long soak (`phy_soak.py`, 160 steps, 1,593,638 accesses, object alone):
watchdog 1,308 calls (all results 0: no watchdog ran on a channel outside
the table), `cca_stats_upd` 1,357 (1,351 with 1), `cca_stats_tsf_upd` 43,
`wlc_update_txppr_offset` 47 (ceilings 21..31), `sub_172c6c` 24,570,
`wlc_stf_spatialpolicy_set_complete` 49 (policy -1 x48, 0 x1),
`wlc_stf_txcore_shmem_write` 51, `wlc_stf_ss_update` 51,
`wlc_stf_ss_algo_channel_get` 45 (0x6 x33, 0x1 x10, 0xffff x2); the iovar
handler, `cca_query_stats`, `wlc_cca_get_chanspec` and `sub_172da9` again
not called.

### Direct calls against a model of this text (`verify.py`, `model.py`)

`model.py` implements the procedures from this specification only (the
`ppr_t` semantics from the ppr analyst's model of `ppr.md`); `verify.py`
calls the object's functions directly after attach + up, with the state and
the services set per case, and compares results, every byte of the state,
the shared-memory writes and the calls made (arguments, order):

| Test | Function(s) | Cases | Differences | Varied |
|---|---|---|---|---|
| cb | `sub_172c6c` | 65,536 | 0 | every (ceiling, offset) byte pair |
| txdef | `sub_172af1` | 41,472 | 0 | `txchain` 0..255, `T` 0..5, `stf+0x8a` 0/1/0x80, `nsts` 0..6, 0x100, 0xffffffff |
| spexp | `sub_172da9` | 3,420 | 0 | masks 0..15 and wider, `nsts` 0..5, `T` 0..4, class 0..5 |
| shmem | `wlc_stf_txcore_shmem_write` | 864 | 0 | force, `clk`, core rev 25/26/42, PHY type 0xb/7/3, base = `pub+0xe4`/`+0xe6`/other, vendor/chip pairs, random masks and coexistence word |
| ssupd | `wlc_stf_ss_update` | 18,432 | 0 | PHY capability (stub of `wlc_phy_cap_get`), all inputs of the text, current and other band; calls of `wlc_bmac_band_stf_ss_set` and `wlc_mhf` |
| ssalgo | `wlc_stf_ss_algo_channel_get` | 3,000 | 0 | random containers (any bandwidth, 30 % disabled bytes), `T` 1..3, ceiling, chanspec bandwidths, up flag |
| modeupd | `sub_174cd5` | 2,048 | 0 | band bits, channel byte 0..255 of `wlc+0x518`, the five band bytes (argument of the stubbed `wlc_stf_spatial_policy_set`) |
| polset | `wlc_stf_spatial_policy_set` | 576 | 0 | PHY type, policy, current policy, pending counts (incl. sums that cancel), `wlc+0x304` |
| complete | `wlc_stf_spatialpolicy_set_complete` | 4,000 | 0 | random stf states (`T` 1..3, `txchain`, overrides, `nsts`, ceiling, policy 0/-1/other, containers of every bandwidth with large or small offsets, NULL container), chanspec bandwidth, band type, PHY type 0xb/7, boardflags2 bit 12, `clk`, `wlc_valid_rate` result; the beacon functions stubbed and recorded; 50 different auto-policy outcomes reached |
| txppr | `wlc_update_txppr_offset` | 3,000 | 0 | `tmax`, `tmin` (stubs), `min_txpower` value written into the unwritten int (0 mostly, also small and huge values), `hw_txchain`, per-chain offsets, CCK/OFDM masks with 0..4 bits (778 cases with an unfilled DSSS/OFDM buffer, read as 0), core rev 21/22/39/40/42, `clk`, bandwidth (1,965 re-creations of the container), random offsets containers; the stf container compared byte by byte, the 12 rate words compared with distinct rate entries set in shared memory (1,932 cases writing them), `wlc_ol_curpwr_upd` / `wlc_stf_txchain_set` / `sub_174cd5` arguments |
| cca | all of `wlc_cca.c` but attach/detach | 20,001 | 0 | random sequences of channel changes (table channels, 40/80 MHz chanspecs whose control channel is in the table, channels outside the table, random words), watchdog, `cca_stats_upd(calc)`, `cca_stats_tsf_upd`, down, `cca_query_stats` (any count and size), the iovar handler (get/set, any lengths, malformed chanspecs), `wlc_cca_get_chanspec`, power-save flag, counter words and uptime (stubbed reads); the whole 0x4390-byte state compared after every operation; a second half with few watchdogs exhausted the pool (1,268 failed allocations); the 0xffff-marker quirk reproduced (two channels share sample 1) |
| attach | `wlc_cca_attach`, `wlc_cca_detach` | 6 | 0 | the initial state byte for byte, the module slot, detach (slot freed, `osl_mfree` size), core rev 14 (NULL), full module table (NULL + `osl_mfree`) |

Total 162,355 cases, 0 differences (the cca test as finally run, with pool
exhaustion, also 0).

### The uninitialised stack read (`uninit.py`, `fillcmp.py`)

See the subsection of `wlc_update_txppr_offset`: one 4-byte read of
unwritten stack in the whole call tree (11 of 11 calls); whole session with
0xaa fill: 41,320 instead of 41,416 accesses; with 0xaa fill and that int set
to 0: identical to the zero fill (0 positions differ). `wlc_iovar_getint`
returned -23 in all 11 calls.

### Who touches the states (`watch.py`, `staticscan.py`)

Memory hooks over the cca and stf blocks from their allocation on (plus the
memory imports), during the `hybrid.py` session and the 160-step soak:

* **cca state: no access at all from outside `wlc_cca.c`** (no instruction,
  no memory import), in both sessions.
* **stf state**: the outside accesses listed in "The stf state" (column
  "Outside"); the soak adds only a read of `+0x58` through `bcm_bitcount`.

`staticscan.py` scans the decompiled C of every function outside
`wlc_stf.c` for accesses through `wlc+0x550` (66 functions); hits through
local variables that are also used for other pointers were checked by hand
for the fields of this specification (`sub_031322`, `sub_0411e9`,
`wlc_recvdata`, `sub_0450f0`, `sub_0527b5`, `sub_083bfd`,
`wlc_scb_ratesel_init`, `wlc_attach`). Paths that did not run are covered
only by this heuristic scan.

### Read but not run in a session

The iovar handler, `cca_query_stats`, `wlc_cca_get_chanspec`, the -20 of the
watchdog, the pool exhaustion, `sub_172da9`, the deferral of
`wlc_stf_spatial_policy_set`, the non-auto policies, `T` = 1 or 3, core
revisions below 40: all by direct calls only (above). Not run at all: the
callers' own branches (`sub_079c78`, `wlc_txfifo_complete`, the WoWL and
OLPC users of the stf state).

## Open questions

1. **`min_txpower`.** The object asks for an iovar that no module of this
   driver registers and uses an uninitialised int. With the zeroed stack
   (and in an open implementation) it is 0. In the real driver the value is
   whatever was on the kernel stack: the real card's CCK/OFDM core choice
   and chain mask could differ from the emulated session (the
   `SHM(0x5d4)`/`SHM(0x5d6)` words, the chain mask passed to
   `wlc_stf_txchain_set`). Cannot be settled without a capture on the card;
   0 is the defined choice.
   **Settled by the card (2026-11-04):** it does differ. In the captures of
   card runs 5, 6 and 7 Broadcom's driver writes the masks 3, 3, 3 (CCK,
   OFDM, one stream) at 77 of 78 channel sets on 2.4 GHz, the open code
   (`v` = 0) 1, 1, 1 at all of them. The recording of run 7 replayed through
   the hybrid core with every switch off (every value read from the card as
   the card returned it; the interrupt handler's interleaved accesses and
   the DMA ring addresses skipped) diverges at that mask write with `v` = 0
   or -1, and runs through `up` without a divergence (28,541 accesses) with
   `v` = 16, 20, 23 or a stale heap address: on the card the stack held a
   value that made the ceiling of step 6 small. The ceiling acts as a floor
   of the transmit power: `c` caps every rate's offset (`sub_172c6c`), so no
   rate goes below `min_txpower` dBm. With a floor near the maximum the
   comparison of set_complete step 3 sees the 1- and 2-chain offsets alike
   and picks two chains; the legacy rate words of step 16 get the capped
   offsets too. Whether an implementation should reproduce this (a floor
   instead of 0) is a choice, see `questions/phy-interference.md`, "Card
   run 7".
2. Meaning of the CCA counter words (bmac-run R5 words 0..8) and of
   counters A/B (`SHM(0x10e)`, `SHM(0x10c)`): the names of the sample fields
   are proposals from the arithmetic.
3. `sub_079c78` (the other user of `cca_query_stats` and
   `wlc_cca_get_chanspec`, 2,044 bytes, `wlc.c`/channel quality) is not
   described; it builds 20 MHz chanspecs from channel numbers (`ch | 0x1000`,
   `| 0xc000` for channels >= 15).
4. Purpose of several stf fields used outside (`+0x13`, `+0x14`, `+0x44`,
   `+0x50`, `+0x59`, `+0x76`, `+0x77`, `+0x89`, the attach-written
   `+0x48..+0x78`).
5. The interpretation of the 6/10/4 constants of the auto policy as 3 dB /
   5 dB / 2 dB array gains in 0.5 dB steps (unverified).
6. With `T` > 3 `wlc_stf_spatialpolicy_set_complete` would compare groups
   it did not fetch (not reachable on this chip, which has 3 cores at most).
7. The static list of outside users of the stf state is heuristic for
   fields reached through reused locals; the watch covers only the paths of
   the two sessions.
