# Task `bmac-mhf`: the microcode host flags (MAC bring-up increment A9)

Specification: [`../spec/bmac-init.md`](../spec/bmac-init.md), section **A9**
(`wlc_bmac_mhf` / `wlc_bmac_mhf_get`) and the host-flag table just above it
(the five words and their shared-memory offsets). Register access:
[`../spec/access.md`](../spec/access.md) and `open/hw/access.c`
(`bcm4360_shm_write`; use it, do not change it). Scenario: `mac-mhf`
(`python ab.py run mac-mhf --show 40`).

This continues the MAC bring-up begun in
[`bmac-init.md`](bmac-init.md) (the control primitives A1-A7, already in
`open/mac/bmac_ctrl.c`). Scope here is **only** `wlc_bmac_mhf`. `wlc_bmac_mhf_get`
is a pure getter (no register access, not in the test); implement it if you
like for completeness but it is not required.

## What to write

A new file `open/mac/bmac_mhf.c`, and an extension of the existing
`open/include/bcm4360/mac.h`: add the host-flag state to `struct bcm4360_mac`
**below its "you may add fields below this line" marker**, exactly as:

```c
	/* microcode host flags (bmac-init.md A9) */
	u16 mhf[2][5];		/* copies per band: [0] = 2.4 GHz, [1] = 5 GHz */
	u8  current_band;	/* 0 = 2.4 GHz is the current band, 1 = 5 GHz */
	u8  band_present;	/* bit 0: 2.4 GHz band state exists, bit 1: 5 GHz exists */
```

This layout is the test's ABI contract: `mhf[2][5]` is ten `u16` in row-major
order (the 2.4 GHz row first), then the two `u8`, directly after the existing
`chip_id`. Do not reorder or resize the fields above the marker.

Function (keep the `struct bcm4360_mac *` first argument, as in
`bmac_ctrl.c` — do **not** include `phy_env.h`; its `bcm4360_mac_mhf` is the
PHY's separate `struct bcm4360_hw *`-based service declaration, a different
interface that this task does not implement):

```c
void bcm4360_mac_mhf(struct bcm4360_mac *mac, u8 idx, u16 mask, u16 val, u32 bands);
```

Implement it exactly per spec A9: resolve which band's copy `bands` selects
(1 = 5 GHz, 2 = 2.4 GHz, 0 or 3 = the current band, anything else = do nothing),
update that band's copy word `idx`, and — only when the clock is on, the value
changed, and the selected band is the current band — write the new value to the
shared-memory word for `idx` with `bcm4360_shm_write(mac->hw, word, new)`. The
five `idx -> SHM offset` values are in the spec's host-flag table (`SHM(0x5e)`,
`SHM(0x60)`, `SHM(0x62)`, `SHM(0x78)`, `SHM(0xd4)`); give them a named table.
Handle `bands == 3` (both bands' copies updated) per the spec; note it makes no
register access. A band whose `band_present` bit is clear is skipped.

## What the test does

`mac-mhf` runs the object through attach, up, two channel changes (including a
5 GHz -> 2.4 GHz band change), a watchdog tick and down, and at every call of
`wlc_bmac_mhf` it fills a `struct bcm4360_mac` from the object's state (the per-
band host-flag copies, the current band, the clock flag) and calls your
function with the same `(idx, mask, val, bands)` on a card in the same state.
Compared is the shared-memory access (a single `SHM()` write when the current
band's word actually changes while the clock is on, nothing otherwise). There
are ~35 such calls; several write nothing (clock off early in bring-up, or a
call for the non-current band, or no change) and must stay no-ops.

Finish per [`../implementing.md`](../implementing.md): `0 differences`, no build
warnings, any guesses/contradictions written to
`docs/re/questions/bmac-init.md`, and a short report (files, functions, the
exact final `mac-mhf` line, build-warning status, questions).
