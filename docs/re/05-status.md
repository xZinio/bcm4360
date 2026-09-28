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
| PHY attach (state, SROM variables) | `wlc_phy_ac.c`, `wlc_phy_cmn.c` | [acphy-attach](spec/acphy-attach.md) | in work | scenarios `phy-attach`, `phy-attach-state` |
| PHY initialisation | `wlc_phy_ac.c` | [acphy-init](spec/acphy-init.md) | - | - |
| Setting the channel | `wlc_phy_ac.c` | [acphy-chanspec](spec/acphy-chanspec.md) | - | - |
| Front end control, analog filters | `wlc_phy_ac.c` | in work | - | - |
| Transmit power control | `wlc_phy_ac.c`, `wlc_ppr.c` | in work | - | - |
| Receive gain control, desense, interference mitigation | `wlc_phy_ac.c` | in work | - | - |
| Calibrations (TX IQ/LO, RX IQ, temperature) | `wlc_phy_ac.c` | - | - | - |
| PHY common layer, interface to the MAC | `wlc_phy_cmn.c` | - | - | - |
| Backplane, cores, PCIe bridge | `siutils.c`, `aiutils.c`, `nicpci.c` | - | - | - |
| SROM, OTP, variables | `bcmsrom.c`, `bcmotp.c` | format only, in `tools/re/srom.py` | - | - |
| MAC core bring-up (microcode, initial values, buffers) | `wlc_bmac.c` | in work | - | - |
| DMA, FIFOs, interrupts | `hnddma.c`, `wlc_bmac.c` | - | - | - |
| Frame formats between driver and MAC core | `wlc.c`, `wlc_bmac.c` | - | - | - |
| Aggregation and keys in hardware | `wlc_ampdu.c`, `wlc_key.c` | - | - | - |

The assignments for the areas without specification are written
(`re-out/analysis/assignments/`); they were not started because of the cost
of the analysis (see "Cost").

How much of the PHY the specifications cover is measured, not estimated:
`python phy_scenarios.py gaps` (in `tools/re`) runs a session of the object
(attach, up, nine channel changes, two periodic ticks, down) and lists the
functions of the PHY files that access the hardware and that no
specification has in its scope. At the time of writing 86,000 of the 98,800
accesses of the PHY in that session are made by functions that a
specification describes (87 %). Calibrations that only run after an
association are not part of the session.

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
2. **Finish the PHY**: attach, initialisation, channel, then receive gain and
   transmit power, then the calibrations. This is the part no open driver
   has.
3. **MAC core bring-up and data path**: microcode download, initial values,
   DMA, frame headers, transmit status.
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
