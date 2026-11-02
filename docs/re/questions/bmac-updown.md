# Questions of the implementer: task `bmac-updown`

Specification: [`../spec/bmac-init.md`](../spec/bmac-init.md), sections C2-C6,
C8, C10, C15, D3 (C1, C16 for the callers). Code: `open/mac/bmac_updown.c`;
in `open/mac/bmac_clk.c` the static A13 helper `wlc_bmac_clkctl_clk` became
the exported `bcm4360_bmac_clkctl_clk` (renamed, `static` dropped, its three
callers there updated; nothing else changed, the file-local copy in
`bmac_init.c` left alone).

## Test results

* `python ab.py build`: no warnings.
* `hybrid.py check`, `check --off mac`, `check --on phy`, `check --on phy
  --off mac`: 41416 accesses of the object, 41416 of the hybrid core, the
  same.
* `hybrid.py srom`: all ten cards the same.
* `python ab.py all`: all 25 scenarios ok, 0 differences (no scenario of
  this layer; the others are unchanged).

To make sure the default `check` really runs this file, I ran it once with a
deliberately wrong copy mounted over `open/mac/bmac_updown.c` inside the
container only (D3 step 4 writing 1 instead of 0, C15 step 3 left out), with
its own output directory: differences at the attach (`W d11 0x18c = 1`) and
at both downs (the three `CC(0x28)` accesses missing). With the real file:
the same.

The tests did not contradict the specification anywhere; the first version
was already the same. The points below are what the specification leaves
open or ambiguous, and what I decided. For each: what the specification
says, what the test shows, what I did.

## 1. C15: is the crystal switched off for every chip?

* Specification: "Then, any bus: chips 0x4352, 0x4360, 0xaa06:
  `si_pmu_rfldo(sih, 0)`; `wlc_bmac_xtal(wlc_hw, 0)`." It does not say
  whether the chip list covers both calls or only the first.
* Test: cannot tell (chip 0x4360 is in the list).
* What I did: only `si_pmu_rfldo` depends on the chip; `wlc_bmac_xtal(0)`
  is made on every chip and bus. Correct?

## 2. C8 `sub_05fefa` step 3: receive reclaim without an engine

* Specification: the object calls the receive reclaim of FIFO 0 (slot 0xe0)
  without checking that FIFO 0 has an engine; `mac_env.h` says the DMA
  services of C8 are called "only where the FIFO has an engine".
* Test: FIFO 0 always has an engine; the session is the same.
* What I did: the service is called only if bit 0 of `dma_fifos` is set (as
  `bmac_clk.c` does for the receive reset of A14). Without an engine the
  object would call through a null handle; I took that as not a behaviour to
  copy. Is there a case where FIFO 0 has no engine and the object survives
  the call (a check inside the slot)?

## 3. D3 for MAC revisions <= 10

* Specification: "MAC rev <= 10 uses a different pattern test with
  `D11(0x18c)` and `D11(0x604)`/`D11(0x606)`; not this card." It does not
  say whether step 4 (`D11(0x18c) = 0`, the test of `D11(0x120)`) follows
  that test too.
* Test: not reached (revision 42).
* What I did: for revisions <= 10 a comment only, and step 4 runs for every
  revision. Only matters if older cores are ever supported.

## 4. C2 step 2: the argument of `si_ldo_war`

* Specification: `si_ldo_war(sih, chip id)`; the service's parameter in
  `mac_env.h` is called `devid`.
* Test: the same with the chip id 0x4360.
* What I did: `chip_id` is passed. If the object passes the PCI device id
  (0x43a0) instead, the model shows no difference for it; please confirm
  which one.

## 5. Calls of the PHY without the "current band has a PHY" test

* Specification: C2 step 10 (`wlc_phy_por_inform(pi)`), C4 step 2 / C6 step
  1 (`wlc_phy_hw_state_upd`), C5 step 3 (`wlc_phy_down`), C6 step 2
  (`wlc_phy_hw_clk_state_upd(pi, 0)`) call the PHY unconditionally, where C9
  and A14 say "if the current band has a PHY".
* Test: the same (the current band always has a PHY after attach).
* What I did: called unconditionally, as written. Does the object test `pi`
  anywhere in these functions?

## 6. Small decisions, no answer needed unless wrong

* C2: the test of `pub+0x40` at the top of `wlc_bmac_hw_up` is kept
  although `wlc_up` (C1 step 2) makes it too.
* C2 step 9: an LED entry contributes `1 << (pin & 31)` to `a` when its byte
  `+4` is 0, as written (the specification's "presumably active high" is not
  needed for the code). 32 entries are read through `led_entry`; `r` is the
  result of `si_gpioout` in the first branch and of `si_gpioouten` in the
  second, as written.
* C10: without a coexistence state (`btc_valid` 0) the mask counts as 0. In
  step 3 the 32-bit word `o` is applied to the 16-bit `D11(0x49e)` as
  `v & ~o` cut to 16 bits.
* C10 step 2: as specified, a crystal switched on in step 1 stays on when
  the mask is 0 (only reachable with `sbclk` 0, which neither caller has in
  the session).
* C5 step 1: the interrupt mask returned by `wl_intrsoff` is not used.
* C6: the result of `wl_reset` (32 bit) is returned as the function's
  result, converted to `s32`.
* C3 step 3: `1 << (n & 31)` with n the MAC's core index from
  `wlc_mac_coreidx`.
* C4 step 5: the argument of `wlc_bmac_ifsctl_edcrs_set` is 1 only when the
  upper layer's band PHY type is 7 (HT); 0 for this card, the service is
  called anyway.
* C8 step 1 (`count_reset`) is made before the "card gone" test; step 3
  (`sub_05fefa`) in either case; `wlc_reset_bmac_done` is not called (empty).
* D3: the original 4 bytes of `SHM(0)` are restored only when both patterns
  read back correctly (a failing test returns at once, as specified, and
  leaves the pattern in the shared memory).
* `bmac_init.c` keeps its own copy of A13; now that A13 is exported from
  `bmac_clk.c`, that copy could be replaced by a call in a later change.

## Resolution (coordinator, from the object's code, 2026-11-02)

1. Yes: only `si_pmu_rfldo(sih, 0)` depends on the chip list;
   `wlc_bmac_xtal(wlc_hw, 0)` follows on every chip and bus. Your code is
   right. (C15 reworded.)
2. The object really calls slot 0xe0 of FIFO 0 without a test (and would
   fault without an engine). Every attach that succeeds has an engine for
   FIFO 0, so the test changes nothing reachable; keep it.
3. For revisions <= 10 the object makes its own pattern test and then the
   same step 4 (`D11(0x18c) = 0`, the test of `D11(0x120)`). Your structure
   matches. (D3 reworded.)
4. The object passes the chip id (`sih+0x3c`, 0x4360), not the PCI device
   id; the name `devid` is that of Broadcom's prototype. Right as written.
5. None of these functions tests `pi`: each passes the current band's `pi`
   as it is. Right as written. (C2 step 10 already says so; noted for the
   others in C4-C6.)

Section 6: all as the object does it, including D3 leaving the pattern in
`SHM(0)` on failure and `count_reset` before the "card gone" test.

## 7. Not reached by the session, written from the text only

The card gone (C5 step 1 second branch, C6 step 2, C8 step 2), the radio
disabled by the hardware switch (C3 step 6, result -9), `noreset` set (C5
step 1, C6 step 4), an offload state (C2 step 1, C15 step 4),
`si_seci_down` (C15 step 2), the coexistence GPIO release with a non-zero
mask (C10 step 3, 4), the second LED branch (bit 11 of `boardflags2`, with
`si_gpiopull` for ChipCommon revisions >= 20), the core not up in C6 step 3,
the MAC not enabled in C6 step 3.1, and the failure branches of D3.
