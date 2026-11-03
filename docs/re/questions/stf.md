# Questions from the implementation of `stf` (open/lib/stf.c)

Specification: `docs/re/spec/cca-stf.md`, "Procedures, part 2":
`wlc_update_txppr_offset`, `sub_172c6c` (its callback), `sub_174cd5` (the
spatial policy of the band). Interface: `open/include/bcm4360/stf.h`;
adapter `hybrid/stf.c` (switch `open_stf`). The implementer was the session
that also ran the card tests; it opened nothing below `lib/` or `re-out/`.

Test results: `hybrid.py check` "the same" with the default layers, with
`--on phy` and with `--off stf` (41,416 accesses each); `phy_soak.py` with
the default layers and with the open PHY (513,322 accesses each) and
`--board 3`; `hybrid.py check --steps real --on phy` (263,920 accesses);
the rfkill/suspend sequence of the 2026-11-03 card test through
`phy_soak.py --steps` (231,072 accesses). `opencov.py`: 69 of the 70 lines
of `open/lib/stf.c` run in the session. The comparison zeroes the stack at the
entry of `wlc_update_txppr_offset` (hybrid.py, STACK_ZEROED), so the object
reads `min_txpower` as 0 there, as the open code always does.

Why this function first: Broadcom's version reads the `min_txpower` value
from a stack word it never writes (spec, "The uninitialised stack read").
In the real driver that word holds whatever earlier code left there - and
open code in the call chain (the open PHY, the open register access and
power container) leaves something else than Broadcom's code did. The value
decides the ceiling of the per-rate offsets, which transmit chains stay on
and the core masks of the spatial policy, after every computation of the
power targets. On the card (2026-11-03) loads with open layers failed
intermittently in a way that looks like frames not going out; the open
function makes this deterministic (0, as the specification says an
implementation must).

1. **`rate_info` for step 16**: written as the eight OFDM codes of
   bmac-run.md R6 (bit 7 of the table); the step only uses the 12 rates of
   `cck_ofdm_rates`.
2. **Older cores in step 16** (`corerev` <= 39: "`buf[k].word` = `p`"): `p`
   is a signed byte; the open code sign-extends it into the 16-bit word. Not
   reachable on this card (revision 42).
3. **Step 3**: the first argument of `wlc_ol_curpwr_upd` is taken as the
   pointer stored at `wlc+0x750` (the offload engine's state), not the
   address of the field.
4. **The callback's context** is a byte of the open function's own stack, as
   in the object; `ppr_map_vec_all` is called through its exported name, so
   it reaches the open or Broadcom's container according to `open_ppr`.

## Part 2: the spatial policy, the core words, the single-stream mode

Added: `wlc_stf_spatial_policy_set`, `wlc_stf_spatialpolicy_set_complete`
(with `sub_172af1`), `wlc_stf_txcore_shmem_write` (with `sub_172da9`),
`wlc_stf_ss_update`, `wlc_stf_ss_algo_channel_get` - the same switch
`open_stf`. Test: `hybrid.py stf` (`tools/re/stf_scenarios.py`): 3,000 direct
calls with the stf block and the fields the functions read set at random
(stream count, chains, overrides, class stream counts, ceiling, policies,
container offsets, PHY and band type, boardflags2, clk, pending FIFO counts,
shared-memory base, core revision, vendor and chip); results, the whole stf
block, the fields written outside it and every access compared.

5. **An uninitialised read in Broadcom's beacon code, not in this
   specification**: the first run of `hybrid.py stf` differed only in the
   second beacon PHY control word, `SHM(0xce)`, written by
   `wlc_beacon_phytxctl` (step 4.7): 0x0008 / 0x0170 from the object,
   0x00d0 / 0x00d8 with the open code, with equal arguments (also when the
   registers beyond them were varied) and equal state of `wlc`, `wlc_hw` and
   the stf block. Zeroing the stack at the entry of `wlc_beacon_phytxctl`
   makes all 3,000 cases identical: that function reads stack it never
   writes, so its word depends on the caller. `hybrid.py` now zeroes the
   stack there too (STACK_ZEROED). A station sends no beacons; the word has no
   effect on this driver, but the analysts may want to note it.
6. **Step 4.7 order**: computing all six masks first and the beacon after
   gave about 200 differences; the specification's per-class order (the
   beacon update inside the loop, at the class of the basic rates) is right.
7. **`T` > 3** (set_complete step 3): treated like 3 (the third group is
   fetched whenever `T` is not 2); not reachable on this chip.
8. **`wlc_stf_ss_update` step 2**: `wlc_phy_cap_get` is asked for the current
   band's PHY (both bands share one PHY on this chip).
