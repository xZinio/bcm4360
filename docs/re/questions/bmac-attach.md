# Questions from the implementation of `bmac-attach` (open/mac/bmac_attach.c)

Specification: `docs/re/spec/bmac-attach.md` (all functions of its Scope),
with `bmac-data.md` P54 (`sub_069659`, the DMA parameters) and
`bmac-run.md` R8 (`wlc_bmac_ampdu_set`, `wlc_bmac_xmtfifo_sz_get`) and R9
(`sub_061268`, `sub_061285`, the backplane's interrupt callbacks).
Interface: `open/include/bcm4360/bmac_attach.h`; adapter
`hybrid/mac_attach.c` (switch `open_mac`, group 0x200 of `open_mac_mask`,
`hybrid.py --off mac_attach`). The implementer was the session that also ran
the card tests; it opened nothing below `lib/` or `re-out/`.

Test: `hybrid.py attach` (`tools/re/attach_scenarios.py`) attaches the
driver in the hybrid core twice per case - Broadcom's attach and the open
one, everything else the same - and compares the result, every live
allocation of the emulated OS byte for byte (after the attach, and for a
successful attach again after up, two channels, down and detach) and every
access to the card. The cases are the ten SROM/OTP cards of `hybrid.py
srom` and those of the specification's Verification that the emulator can
make: arguments, card variables and backplane fields changed at the entry
of `wlc_bmac_attach`, callee results replaced at their return.

Results (2026-11-03): `hybrid.py attach` 66 cases, the same (results 0,
0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x13, 0x15, 0x16, 0x17, 0x19, 0x3f2..0x3f5
as the specification's table says; single-band devices refuse the other
band's channels afterwards); `hybrid.py check` with the default layers,
`--on phy`, `--off mac_attach`, `--steps real --on phy`; `phy_soak.py
--on phy`; `hybrid.py srom`; `hybrid.py dma`: the same. `heap_scenarios.py
--layer mac --steps real`: memory after every step of the real driver's
sequence identical with the open and Broadcom's MAC layer, except the
pointers to static functions and data that have open counterparts (the R9
callbacks, the R4 timer function, the FIFO size table).

## The LEDs (bmac-run.md R4, open/mac/bmac_led.c, group 0x400)

`hybrid.py led` (`tools/re/led_scenarios.py`): 3,000 direct calls with the
LED state, clock, `sbclk` and `boardflags2` set at random (1,169 of the blink
timer's function, which the session never reaches), LED state, timer settings,
`wlc_hw+0x180` and accesses: the same. Readings: in the timer, "otherwise"
(the next wake-up) applies only when the remaining time is below the period,
like the branch it is the alternative of; the restart byte turns an LED on
only in its off phase.

## Choices the specification left open

1. **Revisions below 40.** The transmit FIFO size tables of MAC revisions
   below 40 (`.data+0x180 + 12*(corerev-4)`) are not described. The open
   attach refuses those revisions with 0x0d, after the checks of step 5, and
   keeps the table of revisions >= 40 as its own (writable: R8's
   `wlc_bmac_ampdu_set` writes into the table of revisions 26 and 29). As a
   consequence the code for revisions <= 10 and 4 (register offsets of
   13.6) is not there. The driver binds to the BCM4360 family only
   (revision 42).
2. **"Bus type".** Steps 6.4, 10.2, 13.6.4, 14.7 and the detach say "bus
   type"; step 6.4 names `sih+0x04`, step 2 the argument `bustype`. The open
   code reads `sih+0x04` everywhere except step 2. On this card both are 1.
   *Question:* is any of the other places the argument?
3. **Step 13.7, "any other PHY".** Read as the else branch of "phy type 2
   with phy rev <= 2" (so a type 2 PHY with revision > 2 also gets the
   0x4331/0xa9a7 rule). Irrelevant for the AC-PHY. *Question:* or is it
   "phy type != 2"?
4. **Step 7.1.** `r = 0xff` is tested on the full result of `getintvar`,
   before the truncation to 16 bits.
5. **A13 (`sub_064887`).** Broadcom's attach calls its own static copy; the
   open attach calls the open A13 (`bcm4360_bmac_clkctl_clk`, the clock
   group's code) through the view of the MAC state. With the clock group
   switched off the open A13 still runs here.
6. **The burst and prefetch words (P54).** Broadcom's attach writes seven
   16-bit globals in `.data` that `sub_069659` then reads. The open code
   keeps its own copies and does not write Broadcom's. *Question:* does
   anything else read `.data+0x340 .. +0x34e`?
7. **`dma_attach`'s rxoffset.** "wlc+0x6bc" in step 13.6.3 is passed as the
   value of the word (the observed 0x28), not its address.
8. **`osl_snprintf(buf, 8, "wl%d", unit)`** is done by the open code itself
   (the platform interface has no formatted printing).
9. **`bcm_ether_atoe`'s result** for a string with more than six fields
   ("1:2:3:4:5:6:7"): true (six bytes were stored).

## What the comparison cannot reach

`D11(0x15c)` other than the model's 0 (Q1 of the specification), PHY
revisions other than the model's, `si_chip_hostif` = 2 together with a real
microcode, and the chips of other families (0x4306, 0x4331, 0x4350, the
0xa8xx single-band list) beyond the checks of step 5.
