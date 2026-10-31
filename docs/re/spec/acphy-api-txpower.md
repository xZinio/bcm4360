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
| `void bcm4360_ppr_force_disabled(void *ppr)` | `ppr_force_disabled` | (AC) mark the proprietary-rate groups disabled |
| `s8 bcm4360_ppr_get_max(const void *ppr)` | `ppr_get_max` | highest enabled rate |
| `s8 bcm4360_ppr_get_min(const void *ppr, s8 floor)` | `ppr_get_min` | lowest enabled rate, or `floor` |
| `void bcm4360_ppr_get_ofdm(const void *ppr, u8 m, u8 rateset, u8 bw, s8 *out)` | `ppr_get_ofdm` | read an OFDM rate group |
| `void bcm4360_ppr_set_ofdm(void *ppr, u8 m, u8 rateset, u8 bw, const s8 *in)` | `ppr_set_ofdm` | write one |
| `void bcm4360_ppr_get_ht_mcs(const void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, s8 *out)` | `ppr_get_ht_mcs` | read an MCS group |
| `void bcm4360_ppr_set_ht_mcs(void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, const s8 *in)` | `ppr_set_ht_mcs` | write one |
| `void bcm4360_ppr_get_dsss(const void *ppr, u8 m, u8 rateset, s8 *out)` | `ppr_get_dsss` | read the DSSS/CCK group |
| `void bcm4360_ppr_set_dsss(void *ppr, u8 m, u8 rateset, const s8 *in)` | `ppr_set_dsss` | write it |

(The `m`/`rateset`/`bw`/`nss` arguments select a rate group; the open code
passes them through unchanged from the values the merge below uses.)

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
