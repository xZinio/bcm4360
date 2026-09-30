# Status

State of 2026-09-28. This document says what is known, how well, and what is
missing. Read "What the results rest on" before relying on anything.

## Summary

* The object is completely inventoried and decompiled; every function is
  attributed to its source file ([01-anatomy.md](01-anatomy.md)).
* The object runs unmodified in an emulator against a model of the card:
  attach, bring-up, channel changes in both bands and all bandwidths,
  periodic work, calibration entry points, shutdown. Every register access is
  recorded with its call chain ([03-tools.md](03-tools.md)).
* The work of turning that into specifications and into open code has begun
  and is **not finished**. The table below says how far each area is.
* Nothing has been tried on a real card. The repository still needs
  `lib/wlc_hybrid.o_shipped` to produce a working driver.

## Areas

"Specified" means a document in [`spec/`](spec/) exists; "open code" means
code in `open/` written from that document; "compared" means the open code
performs the same register accesses as the object in the emulator, for the
scenarios named.

| Area | Source files of the object | Specified | Open code | Compared |
|---|---|---|---|---|
| Register access: PHY, radio, PHY tables, shared memory | `wlc_phy_cmn.c`, `wlc_bmac.c` | [access](spec/access.md) | `open/hw/access.c` | `access`: 422 accesses, 0 differences |
| 2069 radio: power-up, RCAL, RCCAL, tuning, VCO and converter calibration | `wlc_phy_ac.c` | [acphy-radio](spec/acphy-radio.md) | `open/phy/radio2069.c` | `radio-*` (9 scenarios): 85 calls, 5727 accesses, 0 differences |
| PMU, clocks | `hndpmu.c` | [pmu](spec/pmu.md) | `open/chip/pmu.c` | `pmu-*` (6 scenarios): 83 calls, 2838 accesses, 0 differences |
| PHY attach (state, SROM variables) | `wlc_phy_ac.c`, `wlc_phy_cmn.c` | [acphy-attach](spec/acphy-attach.md) | `open/phy/phy_attach.c` | `phy-attach` (206 acc.), `phy-attach-state` (1260 values): 0 differences |
| PHY initialisation | `wlc_phy_ac.c` | [acphy-init](spec/acphy-init.md) | `open/phy/phy_init.c` | `phy-init`: 47078 accesses, 0 differences (whole session) |
| Setting the channel | `wlc_phy_ac.c` | [acphy-chanspec](spec/acphy-chanspec.md) | `open/phy/phy_chanspec.c` | `phy-chanspec`: 26593 accesses, 0 differences (9 channels, both bands, 20/40/80 MHz) |
| Front end control, analog filters | `wlc_phy_ac.c` | [acphy-rxgain](spec/acphy-rxgain.md) | `open/phy/phy_rxgain.c` | part of `phy-init`/`phy-chanspec`, 0 differences |
| Transmit power control | `wlc_phy_ac.c`, `wlc_ppr.c` | [acphy-txpower](spec/acphy-txpower.md) | `open/phy/phy_txpower.c` | part of `phy-init`/`phy-chanspec`, 0 differences |
| Receive gain control, desense, interference mitigation | `wlc_phy_ac.c` | [acphy-desense](spec/acphy-desense.md) | `open/phy/phy_desense.c` | part of `phy-init`/`phy-chanspec`, 0 differences |
| Calibrations (TX IQ/LO, RX IQ, temperature) | `wlc_phy_ac.c` | not yet | - | (run only after association; not in the session) |
| PHY common layer, interface to the MAC | `wlc_phy_cmn.c` | - | - | - |
| Backplane, cores, PCIe bridge | `siutils.c`, `aiutils.c`, `nicpci.c` | - | - | - |
| SROM, OTP, variables | `bcmsrom.c`, `bcmotp.c` | format only, in `tools/re/srom.py` | - | - |
| MAC core bring-up (microcode, initial values, buffers) | `wlc_bmac.c` | [bmac-init](spec/bmac-init.md) (draft) | - | - |
| DMA, FIFOs, interrupts | `hnddma.c`, `wlc_bmac.c` | - | - | - |
| Frame formats between driver and MAC core | `wlc.c`, `wlc_bmac.c` | - | - | - |
| Aggregation and keys in hardware | `wlc_ampdu.c`, `wlc_key.c` | - | - | - |

How much of the PHY the specifications cover is measured, not estimated:
`python phy_scenarios.py gaps` (in `tools/re`) runs a session of the object
(attach, up, nine channel changes, two periodic ticks, down) and lists the
functions of the PHY files that access the hardware and that no
specification has in its scope. Of the ~98,800 accesses of the PHY in that
session, all but 187 are made by functions a specification now describes; the
187 are the post-association calibrations (`acphy-cal-*`, not yet specified)
and the watchdog top level.

The open PHY is built and tested as a whole session
(`tools/re/phy_scenarios.py`): the object is run through attach, up, nine
channel changes (both bands, 20/40/80 MHz), periodic work and down, and the
open code is taken through the same calls of the MAC layer, compared access
by access. **The whole session now matches the object with nothing excluded**:
`phy-attach` (206 accesses), `phy-attach-state` (1260 state values),
`phy-init` (47,078 accesses) and `phy-chanspec` (26,593 accesses) all report
0 differences, as do the register-access, radio and PMU scenarios. The open
PHY layer is about 280 KB of C in `open/` (attach, init, channel set, radio,
front end, receive gain and desense, transmit power).

Two small notes on the clean-room separation, kept honest:
* Two functions the idle-TSSI measurement calls, `wlc_phy_tx_tone_acphy` and
  `wlc_phy_stopplayback_acphy`, have no specification yet (they belong to the
  transmit calibration, not written). Their amplitude-0 accesses were
  reconstructed by the implementer from the comparison test's observed
  behaviour, which `implementing.md` permits, not from a specification. When
  the calibration is specified this should be revisited.
* What is verified is that the open code makes the same hardware accesses as
  the object **in the emulator**, whose card model uses assumed identity
  values (below). It is not yet run on a real card.

Not planned: the 802.11 station above the hardware layer (management,
scanning, WPA, rate control - `wlc.c`, `wlc_assoc.c`, `wlc_scan.c`,
`wlc_sup.c`, `wlc_rate*.c` ...). An open driver gets that from the kernel
(mac80211); only what these files tell the MAC core and the microcode is of
interest.

## What the results rest on

**Verified by running the object** (repeatable: `python selftest.py`, 18
checks; `python mmiotrace.py selftest`; `python ab.py all`):

* The object attaches to the model, comes up, changes channels, goes down
  without errors.
* The SROM codec of `tools/re/srom.py` produces the same 169 variables from
  an image as the object's own parser.
* The protocol of PHY table accesses, for the widths 8, 16, 32 and 48 bit
  (655 transfers, 4211 entries of the bring-up).
* Recording and replay: a recording made from a run of the emulator, replayed,
  gives the identical trace.
* Statements in the specifications that carry the mark "verified" were
  checked against traces by their authors; the verification tables at the end
  of each specification list them.

**Read from the code only, not run:** everything marked so in the
specifications - above all the paths for other chips, other radio revisions
and error handling.

**Assumed, not measured - the identity of the card.** The model answers with
values that were chosen to be plausible, not read from a BCM4360:

| Value | Model | Source |
|---|---|---|
| chip id 0x4360, revision 3, package 0 | assumed | PCI revision 03 of the card |
| chip status `CC(0x2c)` = 0x1 | assumed | makes the object choose a 40 MHz crystal |
| PMU capabilities 0x10a22b11 (revision 17) | assumed | - |
| PHY revision 1, radio 2069 revision 4 | assumed | tables for these exist in the object |
| SROM contents | synthetic | `tools/re/data/synthetic-4360-2x2.vars`, written to pass the object's checks |
| calibration results (RCAL, RCCAL, VCO, IQ) | fixed values | the model is not a radio |

Every trace, and every scenario of the comparison, inherits these
assumptions. Code paths that depend on them (chip revision, PHY revision,
radio revision, board flags from the SROM, front end type) may be different
ones on the real card. Replacing the assumptions by measurements is the most
valuable next step and needs the MacBook (below).

## Cost

The analysis is done by instances of a language model
([04-method.md](04-method.md), "Who did what"). One area of the table costs
an analyst about 1 to 1.5 million tokens, an implementation about one tenth
of that. The usage limit of the account, not the method, decides how fast
the table fills.

## Next steps

1. **Measure the card** (on the MacBook, with the present driver loaded):
   * `sudo python3 tools/re/peek-on-target.py > bcm4360-peek.txt` - reads the
     identity registers and the SROM; reads only, a few seconds.
   * `sudo tools/re/capture-on-target.sh` - records every register access of
     the driver from loading to an associated interface (`mmiotrace`);
     unloads and reloads the driver, the network is interrupted.

   Both are untested on real hardware. The outputs contain the MAC address
   of the card; they are for local use (`re-out/`), not for the repository.
   With them: correct the model (`tools/re/bcm4360.py`), replay the recording
   (`python mmiotrace.py replay`), re-run all scenarios.
2. **The PHY is done for the exercised session** (bring-up and channel
   changes). What is left of the PHY: the calibrations that run only after an
   association (transmit IQ/LO, receive IQ), and re-running the whole session
   against a *real-card* recording once the model is corrected (step 1).
3. **MAC core bring-up and data path**: bring-up is specified
   ([bmac-init](spec/bmac-init.md)) and can be implemented next the same way;
   still to specify are the data path (DMA, FIFOs, interrupts), the frame
   formats and the aggregation/key handling in hardware.
4. **Decide the frame of the open driver.** See below.
5. **First test on hardware** of the open hardware layer: bring-up to the
   point where the PHY receives (a scan that sees beacons).

## The frame of an open driver: a proposal

The object is a complete driver with its own 802.11 stack; the glue in `src/`
only adapts it to the kernel. Replacing the object function by function
would mean writing that stack again. The kernel has one: mac80211. The
proposal is therefore a new mac80211 driver on top of the kernel's `bcma`
bus driver, which already enumerates the cores of this card, with `open/` as
its hardware layer - the way `brcmsmac` and `b43` are built for older chips.

What such a driver needs from this work is the hardware layer of the table
above, and the data cut out of the object (`tools/re/fwcut.py`): microcode,
initial values, PHY and radio tables.

To be checked before deciding (not done here): how much of the MAC core
revision 42 the kernel's `b43` driver already handles. `b43` knows the card
and has a PHY type "AC" without implementation; if its MAC layer is usable
for this core revision, adding the PHY to `b43` is the shorter way.
