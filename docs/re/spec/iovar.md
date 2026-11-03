# The iovar dispatcher

`wlc_iovar_op` (`wlc.c`) is the way into every named driver variable
("iovar": `mpc`, `chanspec`, `qtxpower`, ... - 405 of them in 28 modules on
this driver): from the Linux glue (`wl` tool, cfg80211 layer) and from the
driver's own code (`wlc_iovar_getint`/`setint`, the channel and power code
asking for `min_txpower`, `txchain_pwr_offset`). It looks the name up in the
iovar tables of the registered modules and hands the get or set to the
module's handler. The lookup is a linear search with a string compare per
entry, which makes this one small function about 1 % of all execution in
the hybrid session.

Written by the coordinating session from the disassembly; checked by running
the object in the emulator (see "Verification").

## Scope

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_iovar_op` | 0x036153 | 507 | original |

Called with `(wlc, name, params, p_len, arg, len, set, wlcif)`: `name` a
NUL-terminated string, `params`/`p_len` the input buffer of a get (may be
NULL/0), `arg`/`len` the value buffer (the result of a get, the value of a
set), `set` a bool (byte), `wlcif` the interface the request came in on (may
be NULL). `p_len` and `len` are signed 32-bit. Returns a Broadcom error code
(signed 32-bit, 0 = success).

What the function calls, and stays Broadcom's (services):

| Service | What it is |
|---|---|
| the module table | `wlc+0x358` points to an array of 64 module slots of 0x50 bytes; per slot `+0x00` the module's name (char[32]), `+0x20` its iovar table (NULL: empty slot), `+0x28` the module's handle, `+0x38` its iovar handler |
| `wlc_iovar_check(wlc, vi, arg, len, set, wlcif)` | checks a request against the entry's flags (band, up/down state, interface, the lengths via `bcm_iovar_lencheck`); 0 or an error code |
| `wlc_bsscfg_find(wlc, idx, &err)` | the BSS configuration with index `idx`; sets `err` (left alone on success) |
| the BSS configuration's interface | `bsscfg+0x10` |
| the module's handler | `handler(hdl, vi, actionid, name, params, p_len, arg, len, val_size, wlcif)`, result int |

## Data

### The iovar table entry

A module's table is an array of entries of 16 bytes, ended by an entry whose
name is NULL. The layout is the public `bcm_iovar_t` of
`src/include/bcmutils.h`:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 8 | `name` (pointer to a NUL-terminated string) |
| 0x08 | 2 | `varid` |
| 0x0a | 2 | `flags` (only `wlc_iovar_check` reads them) |
| 0x0c | 2 | `type`: 0 = void, 1..7 integers (`IOVT_BOOL` .. `IOVT_UINT32`), 8 = buffer (`IOVT_BUFFER`) |
| 0x0e | 2 | `minlen` (only the length check reads it) |

### Error codes

-14 `BCME_BUFTOOSHORT`, -23 `BCME_UNSUPPORTED` (the values of
`src/include/bcmutils.h`).

### Fields updated after a set

| Field | Size | Meaning (by its use) |
|---|---|---|
| `wlc+0x2a3` | u8 | "transmit header cache valid": `wlc_txc_upd` sets it the same way; the frame path clears it around a header build |
| `wlc+0x2a4` | u8 | "transmit header cache enabled" (1 after attach) |
| `wlc+0x2a8` | u32 | a generation counter of that cache |

## Procedure: `wlc_iovar_op`

`err` below is a local result, 0 at the start.

1. **Lookup.** Let `base` be the part of `name` after its last `':'`, or the
   whole `name` if it has none. For each slot `i` = 0, 1, ..., 63 whose
   iovar table is not NULL: walk the table from its first entry until the
   entry with name NULL; the first entry whose name equals `base` (byte for
   byte, as `strcmp`) is the one. The search stops at the first match: the
   lowest slot, and in it the first matching entry. If no slot has it:
   return -23.
2. With the entry `vi` found in slot `i`:
   1. If `params` is NULL, the input is the value buffer: `params = arg`,
      `p_len = len`.
   2. `val_size`: 0 if `vi->type` is 0, `len` (as passed in, before step 3)
      if it is 8, else 4.
   3. `actionid` = `vi->varid * 2 + 1` for a set, `vi->varid * 2` for a get.
3. **BSS configuration prefix.** If `name` begins with the 7 characters
   `bsscfg:` (compared on the full name, not on `base`):
   1. If `p_len < 4` (signed): return -14.
   2. `idx` = the first 4 bytes of `params` (a little-endian 32-bit value,
      not necessarily aligned).
   3. `bsscfg = wlc_bsscfg_find(wlc, idx, &err)`; if `err` is not 0 now:
      return `err`.
   4. For a set: `arg` += 4, `len` -= 4 (the value follows the index).
   5. `name` += 7, `p_len` -= 4, `params` += 4 (for a get and a set alike),
      and `wlcif` = the interface of `bsscfg`.
4. **Check.** `r = wlc_iovar_check(wlc, vi, arg, len, set, wlcif)` with the
   values of step 3; if `r` is not 0: return `r`.
5. **Dispatch.** `err` = the handler of slot `i`, called with the module's
   handle, `vi`, `actionid`, `name`, `params`, `p_len`, `arg`, `len`,
   `val_size`, `wlcif` (all as left by steps 2-3).
6. **After a set** (whatever `err` is): `wlc+0x2a3` = 1 if `wlc+0x2a4` is 1
   (read now, after the handler), else 0; then `wlc+0x2a8` += 1.
7. Return `err`.

Notes:

* The object computes `base` once per non-empty slot (the same value each
  time); nothing observable depends on how often.
* A name with a colon that does not start with `bsscfg:` (`foo:mpc`) is
  looked up by the part after the colon and handed to the handler unchanged.
* `val_size` for a buffer is taken from `len` before the prefix adjustment
  of step 3.4.

## Module register and unregister

(Coordinator, 2026-11-03, from the disassembly.) The modules of the driver
enter themselves in the module table when they are attached and leave it
when they are detached; the dispatcher above, the watchdog and the up/down
code of `wlc.c` read the table. 29 modules register in the session.

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_module_register` | 0x02b504 | 189 | original |
| `wlc_module_unregister` | 0x035c57 | 109 | original |

Prototypes (public, `src/wl/sys/wlc_pub.h`):
`int wlc_module_register(wlc_pub_t *pub, const bcm_iovar_t *iovars, const
char *name, void *hdl, iovar_fn_t iovar_fn, watchdog_fn_t watchdog_fn, up_fn_t
up_fn, down_fn_t down_fn)` and `int wlc_module_unregister(wlc_pub_t *pub,
const char *name, void *hdl)`. The first word of `pub` is `wlc`.

### The module slot

The table (`wlc+0x358` points to it) has 64 slots of 0x50 bytes:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 32 | name (NUL-terminated; first byte 0 = free slot) |
| 0x20 | 8 | `iovars` (the iovar table, may be NULL) |
| 0x28 | 8 | `hdl` (the module's handle) |
| 0x30 | 8 | `watchdog_fn` |
| 0x38 | 8 | `iovar_fn` |
| 0x40 | 8 | `up_fn` |
| 0x48 | 8 | `down_fn` |

### Procedure: `wlc_module_register`

1. `wlc = *pub` (not checked).
2. For each slot 0..63 in order: if the first byte of its name is 0, it is
   the free slot: copy the name into it with the OS layer's
   `osl_strncpy(slot name, name, 31)` (`strncpy`: at most 31 bytes; if the
   name is shorter, the rest of the 31 bytes is filled with zeros; byte 31 is
   not written - the emulator's semantics; the Linux glue implements it with
   `strscpy_pad` from kernel 7.2 on, which keeps at most 30 characters and a
   NUL; an implementation calls the same OS function), then store `iovars`,
   `hdl`, `iovar_fn`, `watchdog_fn`, `up_fn`, `down_fn` in their fields;
   result 0.
3. No free slot: result -22 (`BCME_NORESOURCE`), nothing changed.

Nothing checks whether the name is already registered. A module registered
with an empty name ("") leaves its slot looking free: the next register
takes the same slot.

### Procedure: `wlc_module_unregister`

1. `wlc = *pub`; if it is NULL: result -30 (`BCME_NOTFOUND`).
2. For each slot 0..63 in order (free slots too): if `strcmp(slot name,
   name)` is 0 and the slot's `hdl` equals `hdl`: zero the whole slot (0x50
   bytes); result 0.
3. None: result -30.

## Verification

`tools/re/iovar_scenarios.py` (run as `python hybrid.py iovar`) brings the
object alone and the hybrid core to the same state (attach, up) and calls
`wlc_iovar_op` on both with the same arguments, comparing the result, the
value buffer and the fields of "Fields updated after a set" after each
call, and every access to the card:

* a get of every one of the 405 names (with and without an input buffer),
* a set of the value just read for every name whose get succeeds,
* the `bsscfg:` prefix: every name with index 0, an index that does not
  exist, an input buffer too short, gets and sets,
* names with other colons, unknown and empty names, value buffers of
  length 0, 2 and 4.

A Python model written from the text of this specification (the
coordinator's check of it, not part of the repository; it calls the
object's own check, BSS lookup and handlers) gives the object's results for
all of these calls: 3,054 calls, the same results, value buffers and fields,
and the same 21,186 accesses to the card.
