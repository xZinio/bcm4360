# Task `bmac-ctrl`: the MAC control primitives (first increment of MAC bring-up)

Specification: [`../spec/bmac-init.md`](../spec/bmac-init.md), sections **A1-A7**
(the "Procedures / A. Services used by the PHY and by everybody else" part, up
to and including A7; also the two reference tables "Bits of the MAC control
register" and "Fields of `wlc_hw` used by the MAC state functions").
Register access: [`../spec/access.md`](../spec/access.md) and your
predecessor's code in `open/hw/access.c` (use it, do not change it).
Scenario: `mac-ctrl` (`python ab.py run mac-ctrl --show 40`).

This is the first of several MAC increments. It covers only the control
primitives every other MAC function builds on; the microcode download, core
reset, attach, up/down and the state setters are later tasks and are **out of
scope** here. Do not implement them.

Scope: chip 0x4360, MAC core revision 42, PCIe. The special cases for chips
0xd144 and 0x5357 (A6 step 9, A7 step 3) and for MAC revisions < 13 with a
3-wire Bluetooth coexistence interface (A6 step 3, A7 step 7) do not occur on
this card: a one-line comment where the specification mentions them is enough,
no code. The microcode "card gone" path (a 32-bit read returns 0xffffffff, A6
steps 4/5/8) cannot happen against the model and the open layer has no handle
to take the interface down; treat a 0xffffffff read as "return early, leaving
`mac_suspend_depth` as it is" and leave a comment that a real driver would call
its platform down here.

## What to write

`open/include/bcm4360/mac.h` and `open/mac/bmac_ctrl.c`.

```c
/* The MAC core's control state (the subset of Broadcom's `wlc_hw` that the
 * control primitives work from). The caller fills in every field; in the
 * tests they come from the object's `wlc_hw` at the entry of the call. */
struct bcm4360_mac {
	struct bcm4360_hw *hw;	/* for D11() register and SHM() access */
	u32 maccontrol;		/* software copy of D11(0x120) WITHOUT the overrides */
	u32 mac_suspend_depth;	/* nesting counter of suspend/enable */
	u32 wake_override;	/* bitmask of reasons to keep the microcode awake */
	u32 mute_override;	/* not 0: transmitter muted */
	u16 fastpwrup_dly;	/* fast-clock power-up delay, microseconds (A5 budget) */
	u8  clk;		/* the core's clock is on */
	u8  mac_rev;		/* MAC core revision (42) */
	u32 chip_id;		/* chip id (0x4360); only for the d144/5357 comments */
	/* you may add fields below this line */
};

void bcm4360_mac_mctrl(struct bcm4360_mac *mac, u32 mask, u32 val);	/* A2 (+A1) */
void bcm4360_mac_wake_override_set(struct bcm4360_mac *mac, u32 bit);	/* A3 (+A5) */
void bcm4360_mac_wake_override_clear(struct bcm4360_mac *mac, u32 bit);	/* A4 */
void bcm4360_mac_suspend(struct bcm4360_mac *mac);			/* A6 */
void bcm4360_mac_enable(struct bcm4360_mac *mac);			/* A7 */
```

The names, argument order and the field layout above the line are the contract
with the test: `struct bcm4360_mac` must have exactly that layout (the test
packs it: `void*`, four `u32`, `u16`, `u8`, `u8`, `u32`, no padding, 32 bytes).
Keep A1 (`wlc_mctrl_write`) and A5 (`wlc_bmac_wait_for_wake`) as `static`
helpers of the `.c` file, named after the procedure they implement.

Direct 802.11-core registers use `d11_read32`/`d11_write32` from
`open/include/bcm4360/hw.h` (`D11(0x120)` is MACCONTROL, `D11(0x128)` is
MACINTSTATUS). Shared memory uses `bcm4360_shm_read(mac->hw, offset)` from
`access.h`. There are no calls to other layers in this increment, so you will
see no `call ...()` lines in the trace.

## What the test does

`mac-ctrl` runs the object through attach, up, two channel changes, a watchdog
tick and down, and at every call of `wlc_bmac_mctrl`,
`wlc_ucode_wake_override_set`/`_clear`, `wlc_bmac_suspend_mac_and_wait` and
`wlc_bmac_enable_mac` it saves the card state and the `wlc_hw` copies, then
calls your matching function with the same arguments on a card in the same
state. Compared are the accesses to `D11()`, `SHM()` and the delays, in order.
There are ~125 such calls; many are no-ops (a nested suspend, an override that
is already awake) and must stay no-ops — reproducing "no access" matters as
much as reproducing the writes.

Finish per [`../implementing.md`](../implementing.md): `0 differences`, no build
warnings, a question written to `docs/re/questions/bmac-init.md` for anything
the specification left unclear, and a short report.
