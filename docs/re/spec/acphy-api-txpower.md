# Transmit-power entry points of the AC-PHY (BCM4360)

What the MAC layer calls on the PHY to set and read transmit power: the power
limits per rate, the user target, the per-core offsets, the estimated output,
and the SROM-11 limit decode. Chip 0x4360, AC-PHY (`pi+0x160` = 11) revisions
0 and 1. This is the MAC-facing layer **above** the closed-loop power control,
which is already specified in [acphy-txpower.md](acphy-txpower.md) (the gain
tables, the estimated-power tables, the idle-TSSI measurement, the tx-cal
coefficient apply) and implemented in `open/phy/phy_txpower.c`. Read that first;
this document adds the per-rate limit path that sits on top of it.

Register notation: [access.md](access.md). Open names: [acphy-api.md](acphy-api.md),
section "Transmit power".

## The `ppr` structure is a service, not PHY state

The per-rate power limits travel in the MAC's `ppr_t` structure (source
`wlc_ppr.c`). It is **opaque to the PHY**: the PHY only ever calls `ppr_*`
operations on it. So the open PHY does not reimplement `ppr`; it calls these
as services (`phy_env.h`), which the hybrid module maps to the object's own
`ppr_*` (`hybrid/takeover.txt`, `use`) and the emulator harness provides.

> **The container is now specified in full in [ppr.md](ppr.md), which
> corrects this table** (2026-11-02): `ppr_apply_max` is a ceiling
> (min(rate, v)) and `ppr_apply_min` a floor (max(rate, v), disabled rates
> included) - the two rows below are swapped;
> `ppr_minus_cmn_val` clamps at 0; `ppr_force_disabled(p, min)` disables
> every rate group with a rate below `min`; the second argument of
> `ppr_get_min` is a value to skip, not a floor; the selector arguments are
> `(bw, mode, chains)` and `(bw, nss, mode, chains)`. The open PHY calls the
> services by name and passes its arguments through, so its behaviour does
> not depend on these descriptions.

A `ppr_t` holds, for a bandwidth selector `s` (0 = 20, 1 = 40, 2 = 80 MHz;
the first word), groups of per-rate power bytes in quarter-dBm; a byte of
`-128` (`0x80`) marks a rate **disabled**. The operations observed:

| Service (phy_env.h) | Object | Behaviour |
|---|---|---|
| `void *bcm4360_ppr_create(hw, u8 bw_sel)` | `ppr_create` | allocate a `ppr_t` for the bandwidth selector; NULL on failure |
| `void bcm4360_ppr_delete(hw, void *ppr)` | `ppr_delete` | free it |
| `void bcm4360_ppr_clear(void *ppr)` | `ppr_clear` | all rates disabled |
| `void bcm4360_ppr_copy(const void *src, void *dst)` | `ppr_copy_struct` | copy all rates |
| `u8 bcm4360_ppr_get_ch_bw(const void *ppr)` | `ppr_get_ch_bw` | the bandwidth selector |
| `void bcm4360_ppr_set_cmn_val(void *ppr, s8 v)` | `ppr_set_cmn_val` | every rate := v |
| `void bcm4360_ppr_plus_cmn_val(void *ppr, s8 d)` | `ppr_plus_cmn_val` | every enabled rate += d |
| `void bcm4360_ppr_minus_cmn_val(void *ppr, s8 d)` | `ppr_minus_cmn_val` | every enabled rate -= d |
| `void bcm4360_ppr_cmn_val_minus(void *ppr, s8 v)` | `ppr_cmn_val_minus` | every enabled rate := v - rate |
| `void bcm4360_ppr_apply_min(void *ppr, s8 v)` | `ppr_apply_min` | every enabled rate := min(rate, v) |
| `void bcm4360_ppr_apply_max(void *ppr, s8 v)` | `ppr_apply_max` | every enabled rate := max(rate, v) |
| `void bcm4360_ppr_compare_min(void *a, void *b)` | `ppr_compare_min` | a := element-wise min(a, b) |
| `void bcm4360_ppr_compare_max(void *a, void *b)` | `ppr_compare_max` | a := element-wise max(a, b) |
| `void bcm4360_ppr_multiply_percentage(void *ppr, u8 pct)` | `ppr_multiply_percentage` | every enabled rate scaled by pct/100 |
| `void bcm4360_ppr_force_disabled(void *ppr, s8 min)` | `ppr_force_disabled` | (AC) mark the proprietary-rate groups disabled; `min` is the second argument (see the correction below) |
| `s8 bcm4360_ppr_get_max(const void *ppr)` | `ppr_get_max` | highest enabled rate |
| `s8 bcm4360_ppr_get_min(const void *ppr, s8 floor)` | `ppr_get_min` | lowest enabled rate, or `floor` |
| `void bcm4360_ppr_get_ofdm(const void *ppr, u8 m, u8 rateset, u8 bw, s8 *out)` | `ppr_get_ofdm` | read an OFDM rate group |
| `void bcm4360_ppr_set_ofdm(void *ppr, u8 m, u8 rateset, u8 bw, const s8 *in)` | `ppr_set_ofdm` | write one |
| `void bcm4360_ppr_get_ht_mcs(const void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, s8 *out)` | `ppr_get_ht_mcs` | read an MCS group |
| `void bcm4360_ppr_set_ht_mcs(void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, const s8 *in)` | `ppr_set_ht_mcs` | write one |
| `void bcm4360_ppr_get_dsss(const void *ppr, u8 m, u8 rateset, s8 *out)` | `ppr_get_dsss` | read the DSSS/CCK group |
| `void bcm4360_ppr_set_dsss(void *ppr, u8 m, u8 rateset, const s8 *in)` | `ppr_set_dsss` | write it |

| `void bcm4360_ppr_get_vht_mcs(const void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, s8 *out)` | `ppr_get_vht_mcs` | read a VHT MCS group (as `get_ht_mcs`, groups of 10 rates) |
| `void bcm4360_ppr_set_vht_mcs(void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, const s8 *in)` | `ppr_set_vht_mcs` | write one |

(The `m`/`rateset`/`bw`/`nss` arguments select a rate group; the open code
passes them through unchanged, in the order of this table, from the values
the merge below uses.)

**Corrections by the coordinator (2026-11-01, from the object's call sites):**

* `ppr_force_disabled` takes a second argument, a signed byte: in the merge
  (`sub_0b8ca4`) it is the minimum power that `wlc_phy_txpower_sromlimit`
  returned through its third argument (a byte initialised to 0 before the
  call) - the same value step 8 passes as the floor of `ppr_get_min`. So step
  6 is `ppr_force_disabled(A, min)`.
* The SROM-11 decoding writes the VHT rate groups with `ppr_set_vht_mcs`
  (180 calls in a session, from `sub_0b42df`), not `ppr_set_ht_mcs`; both
  have the argument layout of the table. The two VHT services were added
  above.
* `ppr_create` and `ppr_delete` take the OS handle as their first argument
  (`sh+0x10`); `hw` stands for it in the open prototypes.

## Entry points

| Function | Behaviour (AC-PHY) |
|---|---|
| `wlc_phy_txpower_limit_set(pi, ppr, chanspec)` | suspend MAC; `sub_0b8ca4(pi, ppr, 0)` (the merge, below); `sub_0b72a4(pi)` (the recalc tick, [acphy-txpower.md](acphy-txpower.md) §17 flow); enable MAC. The `chanspec` the MAC passes is not used by the AC path. |
| `wlc_phy_txpower_set(pi, qdbm, override, reg_ppr)` | if `qdbm` >= 0x80: return 5. Else `pi+0x1d6` (`txpwr.user`) = `qdbm`; `pi+0x232` = 0; if the interface is up and the scan hold is clear: suspend if the MAC is enabled, disable the hardware power control (`wlc_phy_txpwrctrl_enable_acphy(pi, 0)` saving `pi+0xfa0`), `sub_0b8ca4(pi, reg_ppr, 0)`, `sub_0b72a4(pi)`, restore the control, enable if it was suspended. Return 0. |
| `wlc_phy_txpower_get(pi, &qdbm, &override)` | `*qdbm` = `pi+0x1d6`; if `pi+0x230`: `*qdbm` = (override active) `-pi+0x22f \| 0x40000000`; `*override` (if non-NULL) = `pi+0x232`. Return 0. |
| `wlc_phy_txpower_sromlimit(pi, chanspec, &min, max_ppr, core)` | if `&min`: `*min` = `pi+0xc37 << 2` (the min-power iovar, quarter-dBm). If `max_ppr`: for the AC-PHY `wlc_phy_txpower_sromlimit_get_acphy(pi, chanspec, max_ppr, core)` ([acphy-txpower.md](acphy-txpower.md) §19). |
| `wlc_phy_txpower_get_target_max(pi)` | max of `pi+0x216[core]` over the cores in `sh+0xa7` (`txchain`). |
| `wlc_phy_txpower_get_target_min(pi)` | min of `pi+0x21a[core]` over those cores, from a 0x7f start. |
| `wlc_phy_txpower_hw_ctrl_get(pi)` | for the AC-PHY, `pi+0xfa0` (u8, the hardware-power-control enable). |
| `wlc_phy_txpower_ipa_ison(pi)` | for the AC-PHY, 0 (the internal-PA test is N-PHY only). |
| `wlc_phy_txpwr_percent_set(pi, pct)` | `pi+0x184` (`txpwr.percent`) = `pct`. |
| `wlc_phy_txpower_core_offset_set(pi, offsets)` | `wlc_phy_txpower_core_offset_set_acphy` (already in `open/`, [acphy-txpower.md](acphy-txpower.md) §18) through the function pointer `pi+0xd0`; returns -23 if the pointer is null (not for AC). |
| `wlc_phy_get_est_pout(pi, est, est_adj, est_cck)` | suspend MAC; read the estimated output power per core from the control status (the AC path reads `PHY(0x640 + 0x200 c)` etc., see [acphy-txpower.md](acphy-txpower.md) for the status layout); enable MAC. In the model the status reads 0. |
| `wlc_phy_get_olpc_pwroffset(pi, bw)` | for the AC-PHY, a byte from `pi_ac`: `+0x8de`/`0x8df`/`0x8e0` for bw 20/40/80. |
| `wlc_phy_tssivisible_thresh(pi)` | for the AC-PHY, `wlc_phy_tssivisible_thresh_acphy(pi)` (already in `open/`). |
| `wlc_phy_clear_tssi(pi)` | for the AC-PHY (`pi+0x160` = 11) the function returns without writing (the SHM writes are for the other PHY types); no effect. |

State fields used (all in `phy.h` `txpwr` already, except add the two the
object keeps): `pi+0x1d6` user target, `pi+0x184` percent, `pi+0x216[core]`
/ `pi+0x21a[core]` the per-core max/min targets, `pi+0x22f`/`0x230`/`0x232`
the override state, `pi+0xc37` the min-power iovar, `pi+0xfa0` the
hardware-control enable (add `u8 hwpwrctrl` if not present), `pi+0x1c8` the
offsets `ppr` (`txpwr.ppr`).

## sub_0b8ca4: the per-rate limit merge

`sub_0b8ca4(pi, in_ppr, reg_ppr)` (wlc_phy_cmn.c) builds the per-rate power
limits the PHY will enforce and the per-core targets. It is called by
`wlc_phy_txpower_limit_set`, `_set` and `wlc_phy_txpower_get_current`. The AC
path:

1. Pick the bandwidth selector `s` from `pi+0x17e` bandwidth bits (0x2000 ->
   2 = 80, 0x1800 -> 1 = 40, else 0 = 20). `ppr_create` a working limit `L`
   for `s`; if `in_ppr` is given, `ppr_copy` it into `L` (the regulatory/board
   limit from the MAC).
2. `ctl` = `wf_chspec_ctlchan(pi+0x17e)` (the control-channel number; the open
   code computes it — see acphy-chanspec for `wf_chspec_ctlchan`). Create two
   more ppr objects `A` and `B` for `s`.
3. If the offsets ppr `pi+0x1c8` does not match the current bandwidth, delete
   and recreate it for `s` (it caches the per-rate offsets handed to the MAC).
4. Read the board/sub-band constant from the channel: index the PHY channel
   table for `pi+0x17e`'s channel; take a per-sub-band value `cR` from
   `pi+0x1110` (5 GHz) or `pi+0x1128` (2.4 GHz) — the `cga` arrays.
5. For each core `c` in `pi+0x168`:
   1. `ppr_set_cmn_val(A, pi+0x1d6)` (the user target as the ceiling).
   2. `wlc_phy_txpower_sromlimit(pi, pi+0x17e, &min, B, c)` -> `B` holds the
      SROM per-rate limit for this core (via `..._get_acphy` and
      `apply_srom11`, below); `min` the SROM min.
   3. `ppr_compare_min(B, L)` (clamp the SROM limit to the regulatory `L`).
   4. `ppr_minus_cmn_val(B, pi+0x10a0)` (an open-loop/path-loss back-off byte).
   5. `ppr_compare_min(A, B)` (A := min(user ceiling, adjusted SROM limit)).
   6. for the AC-PHY `ppr_force_disabled(A)` (disable the proprietary-rate
      groups the AC-PHY does not send), instead of the N/HT `ppr_apply_min`.
   7. if `pi+0x184` (percent) < 100: `ppr_multiply_percentage(A, pi+0x184)`.
   8. `pi+0x216[c]` = `ppr_get_max(A)`; `pi+0x21a[c]` = `ppr_get_min(A, min)`;
      `pi+0x22f` = that min; `pi+0x234` = 0.
   9. on core 0, if `reg_ppr` is given, `ppr_copy(A, reg_ppr)` (hand the
      regulatory-limited set back to the MAC).
   10. build the offsets: `ppr_cmn_val_minus(A, pi+0x216[c])` (unless an
       override state says otherwise) so `A` becomes target-minus-limit per
       rate; `ppr_compare_max(pi+0x1c8, A)` accumulate into the offsets ppr.
6. `ppr_delete` `L`, `A`, `B`. If `reg_ppr` was not given and the callback
   `pi+0x40` (recalc, = `sub_09949f` = `wlc_phy_txpower_recalc_target_acphy`,
   already in `open/`) is set, call it.

The arithmetic is all signed quarter-dBm; `-128` entries stay disabled through
every op (the `ppr_*` services honour that). The two new `pi` scalars
(`pi+0x10a0` path-loss byte, the override bytes) are SROM/iovar-derived; give
them names in `phy.h` and set them where attach/iovar does.

## wlc_phy_txpwr_apply_srom11: the SROM-11 per-rate decode

`wlc_phy_txpwr_apply_srom11(pi, group, chanspec, maxpwr, out_ppr)` fills
`out_ppr` with the per-rate power **limit** for one core, decoded from the
SROM-11 power-offset variables (held unpacked in `pi` as packed 32-bit words).
It is AC-PHY only. Structure:

* Inputs in `pi` (filled at attach from the SROM, see
  [acphy-attach.md](acphy-attach.md); the variable names are
  `cckbw202gpo`, `cckbw20ul2gpo`, `mcsbw202gpo`/`mcsbw402gpo`,
  `mcsbw205g[lmh]po`/`mcsbw405g[lmh]po`/`mcsbw805g[lmh]po`,
  `dot11agofdmhrbw202gpo`, `ofdmlrbw202gpo`, `mcslr5g[lmh]po`, the
  `sb20in40`/`sb20in80and160`/`sb40and80` sub-band offsets, and the
  `pdoffset` words): each is a packed vector of 4-bit per-rate offsets. The
  base words are at `pi+0xc50`, `pi+0xc5c`, `pi+0xd14`, `pi+0xd36`, and the
  neighbouring offsets (the decompiled code reads them by these fixed `pi`
  offsets).
* The decode unpacks each nibble `n` to a power byte `maxpwr + (-2) * n`
  (quarter-dBm steps of -0.5 dB below `maxpwr`), with per-rate and per-sub-band
  corrections (the `local_a8[1]`/`local_9f`/`local_a0` adjustments from the
  `pdoffset`/`sb*` words, gated by the board flag `pi_ac+0x34d`), and fills the
  DSSS, OFDM and MCS rate groups of `out_ppr` by bandwidth (20/40/80) and, for
  5 GHz, by sub-band (low/mid/high), using the `ppr_set_dsss`/`_set_ofdm`/
  `_set_ht_mcs` services.
* Helpers: `sub_0b3369` (unpack one packed word to a byte array),
  `sub_0b2690`/`sub_0b26d3`/`sub_0b2716`/`sub_0b2765` (copy a decoded group
  into the ppr for a given rate class), `sub_0b42df` (the MCS/sub-band
  expansion). Each is small and mechanical.

**Scope note.** The nibble-by-nibble mapping from each SROM word to each rate
group is intricate (it is the one piece of the PHY the bring-up session does
not exercise — the merge above runs only after an association, MAC-driven).
This document fixes its inputs (the SROM variables and `pi` words), its outputs
(the ppr groups per bandwidth and sub-band), the step size (-0.5 dB per nibble
from `maxpwr`) and the helper roles; the exact per-variable nibble order is to
be finalised against the object with the A/B test
(`tools/re/hybrid.py --on phy` with a `wlc_phy_txpower_limit_set` driven after
`up`, which runs `sub_0b8ca4` -> `sromlimit` -> `apply_srom11`). A difference
there names the exact rate group that is off, which closes the mapping without
guessing. This is the honest boundary of a from-decompilation spec for a
256-nibble table; the comparison test is its arbiter, as for the receive
calibration in [acphy-cal-rx.md](acphy-cal-rx.md).

## Verification

| Statement | How |
|---|---|
| `txpower_get`/`get_target_max`/`_min`/`hw_ctrl_get`/`ipa_ison`/`percent_set`/`clear_tssi` behaviours | read from the code (all small, AC paths isolated) |
| `limit_set` -> `sub_0b8ca4` -> `sub_0b72a4`, and `sub_0b8ca4` -> recalc `sub_09949f` | the existing [acphy-txpower.md](acphy-txpower.md) §17 verified the recalc order against the up-trace |
| the `ppr_*` semantics (disabled = -128, the per-op effect) | read from the `ppr_*` bodies; confirm each in the emulator by calling it on a ppr the harness creates |
| `apply_srom11` inputs/outputs and step size | read from the code; the full mapping is to be confirmed by the A/B (above) |

## Open questions

- The exact nibble order of `apply_srom11` per SROM variable (the scope note):
  to be closed by the A/B, not guessed here.
- `pi+0x10a0` (the back-off byte subtracted in the merge) and the override
  bytes `pi+0x22f`/`0x230`/`0x232`/`0x234`: named by use; their SROM/iovar
  source should be confirmed against the attach.
- `wlc_phy_get_est_pout`'s exact per-core status read for the AC-PHY is
  described from the control-status layout; the model returns 0, so the values
  are not exercised.

## Corrections and answers (analyst, 2026-11-01)

Answers to `docs/re/questions/phy-api-txpower.md`. Each answer says whether
it was checked by running code (emulator / hybrid comparison) or only read
from the code. Where this section contradicts the text above, this section
wins.

### Q5. `sub_0b72a4` (the recalculation tick): read from the code

Name: `wlc_phy_cal_txpower_recalc_sw` (`.text+0x0b72a4`, 147 bytes; the
name the phy-cmn analyst assigned in `re-out/analysis/names/phy-cmn.tsv` -
the open declaration `bcm4360_phy_txpower_recalc_tick` is fine as an open
name). Arguments: `pi`. Result: a `bool` in the low
byte of the return register.

1. Look at the PHY type `pi+0x160`. For the types 7 (HT), 4 (N), **11 (AC)**,
   6, 10 and 8 (the LCN families): return 0 (false) at once. No access, no
   service call, no state change.
2. (Other PHY types, not the BCM4360: type 5 (LP) runs the LP-PHY power
   control update and returns true; types 0/2 (A/G) with `pi+0x220` clear
   return the result of the software tx-power recalculation; with
   `pi+0x220` set and `pi+0x236` = 0xffff it reads `SHM(0x32)` into
   `pi+0x238`; all these return true.)

So for the BCM4360 the function is the constant `false`, and the watchdog's
step 12 never sets `pi+0xc44`. The open stand-in ("no access, no service,
result false") is exactly right. Callers (`wlc_phy_txpower_limit_set`,
`wlc_phy_txpower_set`, `wlc_phy_watchdog`) ignore or test the result as the
text above says.

### Q4. The lifetime of the offsets ppr `pi+0x1c8`: read from the disassembly

The merge **clears the offsets ppr on every run**. Its complete life:

| Where | What happens to `pi+0x1c8` |
|---|---|
| `wlc_phy_attach` | set to NULL (no ppr yet); on an attach failure it is deleted if not NULL |
| `sub_0b8ca4` (merge), after creating `L`, `A`, `B` | if not NULL and `ppr_get_ch_bw(pi+0x1c8)` differs from the selector of `pi+0x17e`: `ppr_delete` it, set NULL. If NULL: `ppr_create(sh+0x10, s)`; if that fails: delete `L`, `A`, `B` and return (no callback). **Then, unconditionally: `ppr_clear(pi+0x1c8)`** (every rate byte := -128, disabled) |
| merge, per core (step 5.10) | `ppr_compare_max(pi+0x1c8, A)`: element-wise maximum, so after the loop each rate holds the largest (target − limit) over the cores |
| `sub_09949f` (recalc, via `pi+0x40` at the end of the merge when `reg_ppr` is NULL, and wherever else the recalc runs) | passed as the second argument of `wlapi_high_update_txppr_offset(sh+0x20, pi+0x1c8)`; not modified |
| `wlc_phy_get_est_pout` | read only: `ppr_get_ofdm(pi+0x1c8, bw 0, mode 0, chains 1, buf)` (see Q6) |
| `wlc_phy_detach` | `ppr_delete` if not NULL |

No other AC-PHY code reads or writes it (the other references in the object
are in N/HT/LCN/SSN PHY code). So between two merges it holds exactly what
the last merge computed: for every rate, the maximum over the cores of
`pi+0x216[c] − A_c[rate]` (enabled rates), or -128 for rates disabled in every
core's `A`. Nothing accumulates across merges. The text's step 3 is therefore:

> 3. If `pi+0x1c8` exists but was made for another bandwidth selector, delete
>    it. If it does not exist (now), create it for `s` (failure: free `L`, `A`,
>    `B`, return). **Then `ppr_clear(pi+0x1c8)`.**

This is exactly the experiment of the question ("`ppr_clear` at step 3 makes
`SHM(0x5d4)` match"); the values that still differed after it come from the
decode (Q3), not from the lifetime.

### Further corrections to "sub_0b8ca4: the per-rate limit merge" (read from the disassembly)

The complete order of service calls in the AC path (`N` = `pi+0x168`, `s` the
selector of `pi+0x17e`: 2 if bits 0x3800 = 0x2000, 1 if 0x1800, else 0):

1. `L = ppr_create(sh+0x10, s)`; NULL: return. If `in_ppr`: `ppr_copy_struct(in_ppr, L)`
   (the N-PHY-only HT/OFDM fix-up loop that follows is skipped for type 11).
2. `ctl = wf_chspec_ctlchan(pi+0x17e)` (used only by the SSN-PHY test, no effect
   on AC). `A = ppr_create(sh+0x10, s)` (NULL: delete `L`, return);
   `B = ppr_create(sh+0x10, s)` (NULL: delete `L`, `A`, return).
3. The offsets ppr as in Q4, ending with `ppr_clear(pi+0x1c8)`.
4. The channel-table lookup of `cR` (`pi+0x1110`/`pi+0x1128`) is computed but
   **used only by the LCN PHY types (8, 10)**: no effect on the AC-PHY. An
   implementation can omit it (it makes no access).
5. `min` is one byte, set to 0 **once** before the loop (not per core); each
   `wlc_phy_txpower_sromlimit` call overwrites it with `pi+0xc37 << 2`.
   For core `c` = 0 .. N-1:
   1. `ppr_set_cmn_val(A, (s8)pi+0x1d6)`.
   2. `wlc_phy_txpower_sromlimit(pi, chanspec, &min, B, c)`, `chanspec` =
      `pi+0x17e` as read in step 2.
   3. `ppr_compare_min(B, L)`.
   4. `ppr_minus_cmn_val(B, (s8)pi+0x10a0)`.
   5. `ppr_compare_min(A, B)`.
   6. **If `pi+0x184` < 100: `ppr_multiply_percentage(A, pi+0x184)`** - this
      comes *before* the next item (the text has them the other way round).
   7. `ppr_force_disabled(A, (s8)min)`.
   8. `pi+0x216[c]` = `ppr_get_max(A)`; `m` = `ppr_get_min(A, (s8)min)`;
      `pi+0x21a[c]` = `m`; `pi+0x22f` = `m`; `pi+0x234` = 0.
   9. If `c` = 0: **`pi+0x21e` = 0** (missing in the text), then if `reg_ppr`:
      `ppr_copy_struct(A, reg_ppr)`.
   10. `ppr_cmn_val_minus(A, (s8)pi+0x216[c])` - **always** for the AC-PHY
       (the alternative `ppr_minus_cmn_val(A, pi+0x21a[c])` is taken only when
       `pi+0x220` is set and the PHY type is not 4, 7 or 11). There is no
       override condition on this path for the BCM4360.
   11. `ppr_compare_max(pi+0x1c8, A)`.
6. `ppr_delete(L)`, `ppr_delete(A)`, `ppr_delete(B)` (in that order). If
   `reg_ppr` is not NULL: return. If `pi+0x40` is not NULL: call it with `pi`.

### Corrections to the `ppr` service table (read from the object's `ppr_*` bodies)

The table at the top describes some operations wrongly. As the object does
them (`v` a signed byte; "all" = every rate byte of the ppr, disabled or not):

| Service | Behaviour |
|---|---|
| `ppr_clear` | every rate byte := -128 (0x80) |
| `ppr_apply_max(p, v)` | **all** rates := min(rate, v) (a ceiling; -128 stays -128). The text says max: wrong |
| `ppr_apply_min(p, v)` | **all** rates := max(rate, v) (a floor; this also turns disabled -128 into `v`). The text says min: wrong. Not used on the AC path |
| `ppr_minus_cmn_val(p, d)` | each enabled rate (≠ -128): rate − d if rate > d, **else 0** (it floors at 0, it does not go negative) |
| `ppr_plus_cmn_val(p, d)` | each enabled rate += d (no saturation) |
| `ppr_cmn_val_minus(p, v)` | each enabled rate := v − rate |
| `ppr_compare_min(a, b)` / `ppr_compare_max(a, b)` | if both were made for the same selector: a[i] := min / max(a[i], b[i]) over all rate bytes (so -128 in `b` disables `a[i]` under min, and is ignored under max); different selectors: nothing |
| `ppr_force_disabled(p, v)` | for every rate group of every bandwidth block (DSSS 4, OFDM 8, VHT 10, all chain counts and modes - not only "proprietary" groups): if any of the group's first 8 rates is < v, the **whole group** := -128; for the 9th and 10th rates of a VHT group (MCS 8, 9), each one separately := -128 if < v |

Sizes (from the object's size table): a ppr made for selector 0, 1, 2 holds
182, 364 or 546 rate bytes (1, 2 or 3 blocks of 182 bytes; a block is 3
DSSS groups of 4, 5 OFDM groups of 8 and 13 VHT groups of 10). How the
selectors of Q2 map onto the blocks is internal to the service (not needed by
the PHY).

### Q3. The SROM-11 decode `wlc_phy_txpwr_apply_srom11`: verified by running the object

This replaces the section "wlc_phy_txpwr_apply_srom11" and its scope note.
The mapping below was read from the disassembly and then **checked by
running the object**: `re-out/analysis/phy-txpower-answers/srom11probe.py`
fills `pi+0xc4c..0xd3b` with random bytes, calls the object's
`wlc_phy_txpwr_apply_srom11` on a fresh ppr for 19 chanspecs (2.4 GHz 20/40
both sidebands, 5 GHz 20/40/80 in all three offset groups, all four 80 MHz
sidebands), with 2 and 3 cores, with `pi_ac+0x34d` = 0 and 1, and with random
`maxpwr`, reads every rate group back with `ppr_get_dsss/_ofdm/_vht_mcs`,
and compares it with a Python model written from this text: **18,648 groups
compared, 0 differences** (`probe-r3.txt`).

**Notation.** All values are signed bytes in quarter-dBm, arithmetic modulo
256 (no saturation anywhere). For a word `w`: `n_k(w)` = bits 4k..4k+3.
`s4(x)` = the nibble `x` taken as a signed 4-bit number (x for 0..7, x − 16
for 8..15); the object computes it as `((x + 8) & 15) * 2 − 16` for `2*s4(x)`.
`P(x)` = `maxpwr − 2x` (half a dB per step below `maxpwr`).
OFDM groups have 8 rates in the order 6, 9, 12, 18, 24, 36, 48, 54 Mbit/s;
VHT groups 10 rates, MCS 0..9; DSSS groups 4 rates, 1, 2, 5.5, 11 Mbit/s.

**Arguments.** `wlc_phy_txpwr_apply_srom11(pi, group, chanspec, maxpwr, ppr)`;
returns at once if `pi+0x160` ≠ 11. `group` is computed by the caller
(`wlc_phy_txpower_sromlimit_get_acphy`) from `r` =
`wlc_phy_get_chan_freq_range_acphy(pi, chanspec & 0xff)`: `group` = `r − 1 +
(r < 4)`, i.e. 0 for 2.4 GHz, 1, 2, 3 for r = 1, 2, 3 and **3 also for r = 4**.
For 5 GHz, `i` = `group − 1` selects the low/mid/high word (0, 1, 2) and
`g` = `group` selects a nibble of the shared words. The flag `F` =
`pi_ac+0x34d` (boardflags3 bit 11, acphy-attach.md) is read once at entry.

**The building blocks** (names as assigned by the phy-cmn analyst, `names/phy-cmn.tsv`):

| Function | Name | What it makes |
|---|---|---|
| `sub_0b2716` (.text+0x0b2716, 79 B) | `wlc_phy_txpwr_srom11_ofdm_rates` | OFDM8(w, x) = [P(n0), P(n0), Q, Q, P(n1), P(n2), P(n3), P(n4)] with Q = P(n0) − 2·s4(x), w a 32-bit MCS word |
| `sub_0b2765` (.text+0x0b2765, 79 B) | `wlc_phy_txpwr_srom11_vht_rates` | VHT10(w, x) = [P(n0), Q, Q, P(n1), P(n2), P(n3), P(n4), P(n5), P(n6), P(n7)] with Q as above |
| `sub_0b2690` (.text+0x0b2690, 67 B) | `wlc_phy_txpwr_ofdm_offset_adj` | ADJ8(src, lo, hi): rates 0..5 + 2·s4(lo), rates 6, 7 + 2·s4(hi) (**added**; the caller passes `hi << 4 \| lo` as one byte) |
| `sub_0b26d3` (.text+0x0b26d3, 67 B) | `wlc_phy_txpwr_vht_offset_adj` | ADJ10(src, lo, hi): rates 0..4 + 2·s4(lo), rates 5..9 + 2·s4(hi) |
| `sub_0b3369` (.text+0x0b3369, 139 B) | `wlc_phy_ppr_set_ofdm_all` | SET_OFDM(sel, buf): `ppr_set_ofdm(ppr, sel, 0, 1, buf)`; then for c = 2 .. N: `ppr_set_ofdm(ppr, sel, 2, c, buf)`, `ppr_set_ofdm(ppr, sel, 3, c, buf)` |
| `sub_0b42df` (.text+0x0b42df, 504 B) | `wlc_phy_ppr_set_vht_all` | SET_VHT(sel, buf), below |
| (inline) | | SET_DSSS(sel, buf): for c = 1 .. N: `ppr_set_dsss(ppr, sel, c, buf)` |
| (inline) | | HB(v, f): if F: MCS 8 of `v` −= 32 when bit 0 of `f` is set, MCS 9 −= 32 when bit 1 is set (8 dB each) |

`N` = `pi+0x168` (2 on the BCM4360). SET_VHT calls `ppr_set_vht_mcs(ppr, sel,
nss, mode, chains, buf)` with (nss, mode, chains) in this order:
(1, 0, 1); if N > 1: (1, 2, 2), (2, 1, 2), (2, 0, 2), (1, 3, 2), and
(2, 3, 2) with MCS 8 and 9 of `buf` temporarily set to -128 (restored after
the call); if N > 2: (1, 2, 3), (2, 1, 3), (2, 0, 3), (3, 0, 3), (1, 3, 3),
(2, 3, 3), and (3, 3, 3) with MCS 8/9 again -128. So the TXBF groups whose
stream count equals the chain count never get MCS 8/9. Every group gets the
same powers otherwise: **there is no per-chain or per-mode reduction** in the
decode.

**The words read** (names from acphy-attach.md "Tx power offsets"):
`pi+0xc4c` cckbw202gpo, `0xc4e` cckbw20ul2gpo, `0xc50` the 2.4 GHz OFDM word,
`0xc5c` mcsbw202gpo, `0xc64` mcsbw402gpo, `0xd14` ofdmlrbw202gpo (`LR2`),
`0xd16`/`0xd18` sb20in40lrpo/hrpo, `0xd1a`/`0xd1c` dot11agduplrpo/hrpo,
and per 5 GHz word `i`: `0xc6c+4i` and `0xcb4+4i` (both mcsbw205g[lmh]po),
`0xc90+4i` and `0xcd8+4i` (both mcsbw405g[lmh]po), `0xca8+4i` and `0xcf0+4i`
(both mcsbw805g[lmh]po), `0xd1e+2i` mcslr5g[lmh]po (`LR5`), `0xd24+2i`/
`0xd2a+2i` sb20in80and160lr/hr5g[lmh]po, `0xd30+2i`/`0xd36+2i`
sb40and80lr/hr5g[lmh]po. **Never read by the decode:** `0xc54`, `0xc58`,
`0xc60`, `0xc68`, `0xc78..0xc8c`, `0xc9c..0xca4`, `0xcc0..0xcd4`,
`0xce4..0xcec` (the other copies made at attach). The `pdoffset*` words
(`pi+0xe90..0xed0`) are **not** used by the decode at all.

#### 2.4 GHz (`chanspec & 0xc000` = 0)

Always first:

* OFDM20 = [P(n_k(pi+0xc50))] for k = 0..7 (the attach packs this word so
  that rates 6/9 come from nibble 0 of ofdmlrbw202gpo, 12/18 from its nibble
  1, and 24..54 from the four nibbles of dot11agofdmhrbw202gpo).
* VHT20 = VHT10(mcsbw202gpo, n2(LR2)); HB(VHT20, bits 12..13 of `pi+0xd36`).

Then by bandwidth (`chanspec & 0x3800`):

* 0x1000 (20 MHz): SET_DSSS(0, [P(n_k(cckbw202gpo))], k = 0..3); SET_OFDM(0,
  OFDM20); SET_VHT(0, VHT20).
* 0x1800 (40 MHz):
  * DSSS40 = [P(n_k(cckbw20ul2gpo))], k = 0..3.
  * VHT40 = VHT10(mcsbw402gpo, n3(LR2)); HB(VHT40, bits 14..15 of `pi+0xd36`).
  * OFDM40 = ADJ8(OFDM8(mcsbw402gpo, n3(LR2)), n0(dot11agduplrpo), n0(dot11agduphrpo)).
  * OFDM20in40 = ADJ8(OFDM20, n0(sb20in40lrpo), n0(sb20in40hrpo));
    VHT20in40 = ADJ10(VHT20, same two nibbles).
  * Calls, in this order: SET_OFDM(1, OFDM40); SET_VHT(1, VHT40);
    SET_DSSS(3, DSSS40); SET_OFDM(3, OFDM20in40); SET_VHT(3, VHT20in40).
    (No DSSS for selector 1; nothing for selector 0.)
* any other bandwidth: nothing.

#### 5 GHz (`chanspec & 0xc000` = 0xc000)

Always first, with `i` = group − 1:

* OFDM20 = OFDM8(mcsbw205g[i], n0(LR5[i])); VHT20 = VHT10(mcsbw205g[i],
  n0(LR5[i])); HB(VHT20, bits 4..5 of sb40and80hr5g[i]).

Then:

* 20 MHz: SET_OFDM(0, OFDM20); SET_VHT(0, VHT20). Done (no DSSS on 5 GHz).
* 40 or 80 MHz, first: VHT40 = VHT10(mcsbw405g[i], n1(LR5[i])); HB(VHT40,
  bits 6..7 of sb40and80hr5g[i]); OFDM40b = OFDM8(mcsbw405g[i], n1(LR5[i])).
* 40 MHz:
  * OFDM20in40 = ADJ8(OFDM20, n_g(sb20in40lrpo), n_g(sb20in40hrpo));
    VHT20in40 = ADJ10(VHT20, same nibbles).
  * OFDM40 = ADJ8(OFDM40b, n_g(dot11agduplrpo), n_g(dot11agduphrpo)).
  * Calls: SET_OFDM(1, OFDM40); SET_VHT(1, VHT40); SET_OFDM(3, OFDM20in40);
    SET_VHT(3, VHT20in40).
* 80 MHz:
  * VHT80 = VHT10(mcsbw805g[i], n2(LR5[i])); HB(VHT80, bits 8..9 of
    sb40and80hr5g[i]); OFDM80b = OFDM8(mcsbw805g[i], n2(LR5[i])).
  * dup = (n_g(dot11agduplrpo), n_g(dot11agduphrpo)): OFDM80 = ADJ8(OFDM80b,
    dup); OFDM40in80 = ADJ8(OFDM40b, dup).
  * OFDM20in80 = ADJ8(OFDM20, n0(sb20in80and160lr5g[i]),
    n0(sb20in80and160hr5g[i])); VHT20in80 = ADJ10(VHT20, same nibbles).
    If the control sideband (`chanspec & 0x700`) is 0 or 0x300 (the two outer
    20 MHz channels of the 80 MHz block), both are adjusted **a second time**
    in place with lo = bits **2..5** of sb20in80and160lr5g[i] and hi = bits
    2..5 of sb20in80and160hr5g[i] (the object shifts the words right by 2,
    not by 4; verified).
  * VHT40in80 = ADJ10(VHT40, n0(sb40and80lr5g[i]), n0(sb40and80hr5g[i])).
  * Calls: SET_OFDM(2, OFDM80); SET_VHT(2, VHT80); SET_OFDM(4, OFDM20in80);
    SET_VHT(4, VHT20in80); SET_VHT(5, VHT40in80); SET_OFDM(5, OFDM40in80).
    (Nothing for selectors 0, 1, 3.)
* any other bandwidth: nothing (after the "first" computations).

Note on `n_g` for the shared words: `g` = 1, 2, 3 (nibbles 1..3); the
2.4 GHz path uses nibble 0 of the same words (sb20in40*, dot11agdup*).

#### The wrapper `wlc_phy_txpower_sromlimit_get_acphy` (verified as well)

`maxpwr` = the **unsigned** minimum of `pi+0xe7c+r`, `pi+0xe81+r`,
`pi+0xe86+r` (maxp of cores 0, 1, 2 for range `r`, whatever N is; in the
model core 2's is 0xff, so it never wins). After the decode: `d` =
`maxp[core][r] − maxpwr` (signed byte); if `d` > 0: `ppr_plus_cmn_val(ppr,
d)`; then, if `r` < 5, `ppr_apply_max(ppr, maxp[core][r])` (a ceiling, see the
`ppr` corrections). The probe's part C (two cores, six channels, three sets of
maxp values including an absent core 2) shows 0 differences.

#### What this changes in the open implementation (Q3's list)

* **MCS nibble layout**: MCS 0, 1, 2 come from nibble 0 (MCS 1 and 2 with
  the low-rate offset), MCS 3..9 from nibbles 1..7. "MCS k = nibble k, MCS 8
  and 9 = MCS 7" is wrong. Model SROM, channel 1: VHT20 = 76, 76, 76, 76, 76,
  76, 72, 68, 64, 60 (the guess gave 76, 76, 76, 76, 72, 68, 64, 60, 60, 60).
* **5 GHz OFDM comes from the MCS word** of the same bandwidth through
  OFDM8 (rates 6/9 = nibble 0, 12/18 = nibble 0 with the low-rate offset,
  24/36/48/54 = nibbles 1..4); there are no separate 5 GHz OFDM words. Model
  SROM, channel 36/20 (`maxpwr` 72): OFDM20 = 72, 72, 72, 72, 72, 60, 56, 60
  - so the basic rates 6/12/24 are all at offset 0, which is what the
  question observed in the object; VHT20 = 72, 72, 72, 72, 60, 56, 60, 58, 52,
  50.
* **2.4 GHz 40 MHz**: OFDM40 is derived from mcsbw402gpo (not from a copy of
  the OFDM word); OFDM20in40 and VHT20in40 are the 20 MHz groups plus the
  sb20in40 offsets (not copies).
* **Only the selectors of the chanspec's bandwidth are written** (20 MHz: 0;
  40 MHz: 1 and 3; 80 MHz: 2, 4, 5) and only for chain counts up to N; all
  other groups keep what the ppr held (all -128 for the fresh `B` of the
  merge). The service aliases the others when read (see Q2).
* The sub-band corrections are exactly the ADJ8/ADJ10/HB steps above; the
  low-rate words (`ofdmlrbw202gpo` nibbles 2, 3; `mcslr5g*` nibbles 0..2)
  act on rates 12/18 of OFDM and MCS 1/2 of VHT, subtracting; the
  sb*/dup offsets add.

Things not exercised by the probe: none of the decode paths; the probe covers
every branch of the function (the alias reads are a property of the `ppr`
service, not of the decode).

### Q4 (continued): verified by running the object

See `re-out/analysis/phy-txpower-answers/q4probe.py`,
log `q4probe.txt`: attach, up, channels 36, 36/80, 6, 149/40 with hooks on the
merge, `ppr_create`/`_delete`/`_clear`/`_compare_max` and
`wlapi_high_update_txppr_offset`. In every merge the object calls
`ppr_clear(pi+0x1c8)` once, before the two `ppr_compare_max(pi+0x1c8, A)` of
the two cores; at 36 -> 36/80, 36/80 -> 6 and 6 -> 149/40 (bandwidth changes)
it first deletes the old object and makes a new one; at 1 -> 36 (both 20 MHz)
it keeps the same object and only clears it. Offsets handed to the MAC (first
OFDM rates of block 0, SISO): channel 1 `0 0 0 0 0 0 4 8`, channel 36 all 0
(the 2.4 GHz 4/8 at 48/54 Mbit/s do **not** survive into the 5 GHz channel),
channel 6 `0 0 0 0 0 0 4 8` again.

### Q2. The ppr selectors: confirmed from the object's calls (read) and the probe (run)

The decode's calls fix the argument meaning (`ppr` first in all):

* `ppr_set_dsss(ppr, sel, chains, buf[4])`, `ppr_set_ofdm(ppr, sel, mode,
  chains, buf[8])`, `ppr_set_vht_mcs(ppr, sel, nss, mode, chains, buf[10])`;
  the getters take the same arguments with an output buffer of the same size.
  Group sizes 4 / 8 / 10: confirmed.
* `sel` (the "bandwidth variant"): 0 = 20, 1 = 40, 2 = 80, 3 = 20in40,
  4 = 20in80, 5 = 40in80: confirmed - a 20 MHz chanspec writes only 0, a
  40 MHz one 1 (with the "dup" offsets) and 3 (with the sb20in40 offsets), an
  80 MHz one 2, 4 (sb20in80and160 offsets) and 5 (sb40and80 offsets).
  `ppr_create(osh, s)` takes 0/1/2 = 20/40/80 (the merge's `s`).
* Reading a selector the ppr was not written for returns another block
  (service-internal aliasing, seen in the probe): on a 40 MHz ppr selector 0
  shows the 20in40 block (3); on an 80 MHz ppr selectors 0 and 3 show 20in80
  (4) and selector 1 shows 40in80 (5).
* `mode`: 0 = none (one chain, or as many streams as chains), 1 = STBC,
  2 = CDD, 3 = TXBF. The decode uses OFDM (mode, chains) = (0, 1), (2, c),
  (3, c) for c = 2..N, and the 13 VHT (nss, mode, chains) groups listed under
  SET_VHT in Q3 - exactly the valid set the question found. Other
  combinations are never used by the PHY.
* `chains` 1..3; the decode writes only chain counts up to `pi+0x168`.
* A fresh ppr is all -128 (probe: an 80 MHz `ppr_create` read back through
  all 6 selectors and all groups). `ppr_compare_min`/`_max`: see the `ppr`
  corrections above (element-wise over the whole ppr; nothing if the two were
  made for different selectors).

The open names (`enum ppr_txbw`, `PPR_MODE_*`) with these values are right.

### Q6. The smaller points (read from the code)

* **`get_target_max` / `get_target_min`: `sh+0xa7` is right; the text's
  "(txchain)" is wrong.** Both loop over c = 0 .. `pi+0x168` − 1 and take core
  `c` only if bit `c` of the byte `sh+0xa7` is set. `wlc_phy_stf_chain_init`
  writes its txchain argument to `sh+0xa4` and `sh+0xa6` and its rxchain
  argument to `sh+0xa5` and `sh+0xa7`; `wlc_phy_stf_chain_set` updates only
  `sh+0xa6`. So the mask is the **rx** chain mask, as acphy-attach.md names
  it. Both compare **unsigned** bytes: max starts at 0 and keeps
  `pi+0x216[c]` if it is above (unsigned), min starts at 0x7f and keeps
  `pi+0x21a[c]` if below (unsigned) - so a negative target (≥ 0x80) counts as
  large in both. The result is that byte.
* **`wlc_phy_get_est_pout(pi, est_pout, est_pout_adj, est_pout_cck)`**:
  first `*est_pout_cck` = 0 (one byte, for every PHY type). AC-PHY: MAC
  suspend; `wlc_phyreg_enter`; `wlc_phy_txpwr_est_pwr_acphy(pi, est_pout,
  est_pout_adj)`; `wlc_phyreg_exit`; MAC enable. With acphy-txpower.md
  section 12 that means **`est_pout[c]` ← `PHY(0x642 + 0x200c)`** (`out1`,
  bit 8 test) and **`est_pout_adj[c]` ← `PHY(0x640 + 0x200c)`** (`out2`,
  bit 15 test). The open code has them the other way round. (The read of the
  offsets ppr `pi+0x1c8` in this function belongs to PHY type 2 only.)
* **`wlc_phy_get_olpc_pwroffset(pi, bw)`**: `bw` is the chanspec bandwidth
  field value: 0x1000 -> byte `pi_ac+0x8de`, 0x1800 -> `pi_ac+0x8df`, 0x2000 ->
  `pi_ac+0x8e0`; any other value, or a PHY type other than 11: 0. The open
  reading is right.
* **`wlc_phy_txpower_set`**: "the MAC is enabled" is a 32-bit read of
  `D11(0x120)`, bit 0 (one `osl_readl`), as written. Full AC order: if `qdbm`
  ≥ 0x80 return 5; `pi+0x1d6` = `qdbm`; `pi+0x232` = 0; if `sh+0x30` (up) is
  set and bit 1 of `pi+0x19c` is clear: read `D11(0x120)`; if bit 0 set,
  `wlapi_suspend_mac_and_wait`; save `pi+0xfa0`;
  `wlc_phy_txpwrctrl_enable_acphy(pi, 0)`; `sub_0b8ca4(pi, reg_ppr, NULL)`;
  `sub_0b72a4(pi)`; `wlc_phy_txpwrctrl_enable_acphy(pi, saved)`; if it was
  suspended here, `wlapi_enable_mac`. Return 0. The `override` argument is not
  used.
* **`sub_099528` (`wlc_phy_txpower_core_offset_set_acphy`)**, argument a
  4-byte array `off[0..3]`: for i = 0..3 in order: if `off[i]` ≠ 0 and
  i > `pi+0x168` (strictly greater, so index N itself is accepted): return -2
  at once (the entries before i stay stored, no recalculation). Else if
  `pi_ac+0x456+i` ≠ `off[i]`: store it and note a change. After the loop: if
  something changed and `sh+0x31` (core clock on) is set: `wlapi_suspend_mac_and_wait`,
  `sub_09949f(pi)` (the recalc), `wlapi_enable_mac`. Return 0. The open code's
  "returns 0 or -2 and accepts index N" is right.

### End-to-end check of the merge as now specified: run

`re-out/analysis/phy-txpower-answers/q4merge.py` (log `q4merge.txt`) runs the
object's session (attach, up, channels 36, 36/80, 6, 149/40) and, by direct
calls of `wlc_phy_chanspec_set` + `wlc_phy_txpower_limit_set` with a stand-in
regulatory ppr (all rates 0x50), six more channels (0xd03c, 0xd064, 0xe06a,
0xe39b, 0xd93e, 0x1903: 5 GHz mid and high, 80 MHz outer sidebands LL and UU,
40 MHz upper sideband, 2.4 GHz 40 MHz). For each of these 11 merges it
snapshots the regulatory ppr the merge receives and the offsets ppr
`pi+0x1c8` and targets `pi+0x216/0x21a` when the object calls
`wlapi_high_update_txppr_offset`, then replays the merge **as described in
this section** (decode of Q3, the clear of Q4, the corrected step order),
driving the object's own `ppr_*` services from Python. Result: **all 11
merges identical** - every byte of the offsets ppr (182/364/546) and both
cores' max/min targets (e.g. channel 1: 70/46, channel 36: 50/34, 36/80:
58/34, 149/40: 54/44). An implementation that follows this section should
therefore hand the MAC the same offsets as the object on every channel of the
hybrid session (with the stack zeroing of Q1 in place). The hybrid comparison
itself was not re-run with corrected open code (I may not change `open/`).

### Summary for the implementer (what to change in `open/phy/phy_txpower.c`)

1. `phy_txpwr_apply_srom11`: replace the copy-table decode by the Q3 decode
   (OFDM8/VHT10/ADJ8/ADJ10/HB, per band and bandwidth, only the chanspec's
   selectors, chain counts up to N). The words `pi+0xc54/0xc58/0xc60` and the
   5 GHz copy groups beyond those named in Q3 are not needed.
2. The merge: `ppr_clear(pi+0x1c8)` after the (re)create, every time;
   `ppr_multiply_percentage` before `ppr_force_disabled`; `pi+0x21e` = 0 on
   core 0; always `ppr_cmn_val_minus(A, pi+0x216[c])` on the AC-PHY; the `cR`
   channel-table lookup can go (no effect on AC).
3. `wlc_phy_get_est_pout`: swap the two outputs (`est_pout` from
   `PHY(0x642+0x200c)`, `est_pout_adj` from `PHY(0x640+0x200c)`), wrapped in
   `wlc_phyreg_enter/exit` inside the MAC suspend.
4. `sub_0b72a4` = constant false (already so).

With 1 and 2 the replay of all 11 merges (`q4merge.py`) gives the object's
offsets byte for byte; the remaining hybrid differences from access 24548 on
(`SHM(0x5d4)` 3 vs 1 at the first 5 GHz channel, then the `SHM(0x0e)` rate
table) are the consequences of exactly these two points (baseline run of
`hybrid.py check --on phy` on the current open code, 2026-11-01:
`re-out/analysis/phy-txpower-answers/hybrid-check.txt`).
