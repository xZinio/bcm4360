# Task: the core's platform for the open layers (`open/core/`)

Milestone 1 of [07-open-driver.md](../07-open-driver.md): the open layers run
without any of Broadcom's code. In the hybrid module every service an open
layer needs from its environment (the `*_env_*` functions of
`open/include/bcm4360/mac_env.h`, `phy_env.h`, `bmac_attach.h`, `stf.h`,
`cca.h`, `bmac_led.h`, `dma.h`, `channel.h`, and the PHY's `bcm4360_mac_*`,
`bcm4360_chip_*`, `bcm4360_ppr_*`, `bcm4360_srom_*`, `bcm4360_timer_*`,
`bcm4360_otp_read_word`, `hw_getvar`) is provided by the adapters in
`hybrid/`, which call Broadcom's function of that name. Most of those
Broadcom functions are themselves open code by now (`hybrid/takeover.txt`
and `hybrid/README.md` say which open function replaces each). The core
provides the same services by calling the open functions directly, and keeps
the state that Broadcom's structures held.

## What to write

`open/core/` (new directory; C, the rules of
[implementing.md](../implementing.md)):

1. **The core's state**: `struct bcm4360_core` (private header
   `open/core/core.h`) holding everything: the `struct bcm4360_hw *`, the
   MAC state `struct bcm4360_mac`, the PHY, the backplane state of
   `open/chip/si.c`, the DMA engines of the FIFOs, the SROM variables, the
   power containers, the timers, the band state, and the platform's `plat`
   handle. A way from every handle an env function receives (`struct
   bcm4360_hw *`, `struct bcm4360_mac *`, `void *wlc`, `void *sih`, the PHY's
   handles) back to the core: you may add a field `void *owner` at the end of
   `struct bcm4360_hw` in `open/include/bcm4360/hw.h` (the hybrid ignores it).
2. **Every env function** the open layers need (`tools/re/standalone.py
   undefined` lists them; 274 at the start), in files by layer
   (`open/core/env_mac.c`, `env_phy.c`, `env_attach.c`, `env_stf.c`, ...).
   Each one does what the hybrid's adapter does, with the open function in
   place of Broadcom's; the comment names the adapter it corresponds to.
   Where the adapter reads or writes a field of Broadcom's upper layer
   (`wlc+0x...`, `pub+0x...`, `wlc_hw+0x...` that only the upper layer uses),
   the core keeps that value in its state; where the value comes from
   Broadcom's upper layer and no spec describes it yet, return the value the
   hybrid sees in the emulator session if that is known from its comments
   or tests, otherwise a stub that does nothing, marked `/* TODO wlc-hw */`
   and listed in your questions file.
3. **The interface of `open/include/bcm4360/core.h`**, as far as the open
   layers already do the work. `bcm4360_core_attach()`: the MAC's attach
   (`open/mac/bmac_attach.c`, spec bmac-attach.md), which attaches the
   backplane, PHY, DMA, LEDs through its env functions;
   `bcm4360_core_up()`, `_down()`, `_set_chanspec()`, `_detach()` calling
   the open MAC/PHY functions in the order the hybrid's session reaches them
   (bmac-init.md, acphy-chanspec.md, `hybrid/mac_updown.c`,
   `hybrid/mac_chan.c`). The steps that Broadcom's upper layer adds around
   them come from specs being written now (`docs/re/spec/wlc-hw.md`,
   `chip-attach.md` for `si_attach` and the PCIe bridge, `frames.md` for
   interrupts and frames): leave clearly marked places (`/* wlc-hw: ... */`)
   for them. The frame functions (`_tx`, `_isr`, `_dpc`, filters, BSS) are
   stubs for now.
4. **Timers**: `bcm4360_timer_*` of phy_env.h on top of
   `bcm4360_plat_timer_*` of core.h. The core arms its own 1 s periodic
   timer for the periodic work at up (what it runs comes from wlc-hw.md;
   for now the PHY's watchdog entry point and the MAC's, as the hybrid's
   session calls them).

Not in this task: `open/lib/iovar.c` and `open/lib/module.c` (Broadcom's
iovar dispatcher, no place in the core: `standalone.py` leaves them out) -
if other open code calls into them, say so in the questions.

## Test

From `tools/re` (ZIG set as in implementing.md):

```
python standalone.py undefined      # must reach 0 (except what is marked TODO and listed)
python standalone.py check          # the session, step by step, against the object alone
```

`check` runs Broadcom's object alone and the core on the same card model
and compares the accesses of each step: attach, up, channel changes,
periodic work, down, up, down, detach. As long as `si_attach` and the upper
layer's steps are missing it stops early or differs; get as far as the open
layers allow and report where it stops and why. The harness itself is
`tools/re/standalone.py`; you may fix it (it is a test tool, ISC) and say
what you changed.

Rules: [implementing.md](../implementing.md). You may read `hybrid/` (open
code of this project: the adapters) and everything in `open/`, `docs/`,
`drv/`, `tools/re/*.py` as tools; never `lib/`, `re-out/` (except your own
build output) or anything derived from Broadcom's object. Questions go to
`docs/re/questions/core-env.md`.
