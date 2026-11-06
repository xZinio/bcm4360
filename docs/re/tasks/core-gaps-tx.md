# Task: the audit's transmit gaps (G1, G5, transmit corrections)

Milestone 5 (parity) of [07-open-driver.md](../07-open-driver.md).
Specification: [spec/coverage.md](../spec/coverage.md) part 2: G1, G5, and
the G9 rows that concern the transmit header, its status and aggregation
(frames.md 3.3, 3.5, 4.2, open question 7; cca-stf.md's
`wlc_stf_spatial_expansion_get`).

1. **G1, per-rate transmit power**: the power field of PHY control word 1
   for every rate entry of the transmit header, as G1 specifies (the
   per-rate value, spatial expansion, the open-loop offset and its clamp
   only when the offset is non-zero). Replace whatever the core writes now.
2. **G5, chain changes**: the core functions that change the transmit and
   receive chains and what they write (the chain words of the shared
   memory included); interface in `open/include/bcm4360/core.h` (e.g.
   `bcm4360_core_set_chains(c, txmask, rxmask)`). Do not wire it into
   mac80211: write in docs/re/questions/core-gaps-tx.md what the Linux side
   must call and when (the coordinator wires drv/).
3. **G9 transmit rows**: check that the core follows each correction; fix
   what does not.

Files: yours are open/core/tx.c, frames.c, frames.h, ampdu.c and new files
you add for G1/G5. Another implementer works at the same time in
open/core/wlchw.c, ps.c, core.c, keys.c, env_*.c, drv/: do not edit those.
Shared: open/core/core.h and open/include/bcm4360/core.h - only small
additions with the Edit tool (never rewrite the file), re-read right before.

## Test

`tools/re/frames_check.py`: extend the transmit part so the power field is
compared with Broadcom's object for many rates/chanspecs/power settings
(G1's verification says how many calls were compared and how); a G5 check
against the object if its entry points allow, else the spec.
`standalone.py check` must stay "pass", `standalone.py undefined` 0,
`make open` links without warnings.
