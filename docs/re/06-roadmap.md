# From emulator-validated open/ to an open bcm4360.ko

This document fixes an achievable definition of "done" and lays out the path to
it, with the gates that actually block progress. It is a coordinator document
(see [04-method.md](04-method.md)); it contains no object code.

## What "open" can mean for this chip (the honest target)

- **Unreachable:** "no pre-compiled binaries at all." The BCM4360 is a SoftMAC
  part. It needs microcode loaded into the MAC core, PHY/radio init tables, and
  per-device calibration/NVRAM to operate. Those are Broadcom's data and are
  required at runtime no matter how open the driver *code* is. Only a few WiFi
  parts (e.g. Atheros AR9271 with open-ath9k-htc-firmware) are genuinely
  blob-free; this is not one of them.
- **Achievable in principle:** an open-source driver *module* — no proprietary
  object linked into `bcm4360.ko` — with the microcode and tables shipped as a
  separate, redistributable firmware file loaded at runtime. This is the model
  of `b43` + `b43-fwcutter`, and of essentially every mainline WiFi driver. It
  is what "a fully open-source driver" means in the sense Linux uses the phrase.

Everything below targets the second definition.

## The closed-source surface, measured

The blob is 7.35 MB on disk: about 1.54 MB of code (`.text`) and the rest data
(MAC microcode, PHY/radio tables, the CLM regulatory database, MAC init-value
tables, symbols and relocations). `tools/re` + `re-out/index.tsv` let us measure
the code by layer rather than guess (see `surface_map`):

| Layer of the blob's code | Share | An open driver... |
|---|---|---|
| Other chips' PHYs (N, HT, LCN, LP, SSN, ABG) | 44.6% | never needs — dead for the 4360 |
| Upper 802.11 stack (`wlc.c`, assoc, scan, rate, ampdu, ...) | 29.6% | gets from **mac80211** |
| Crypto / generic utility | 2.1% | gets from the kernel |
| **Hardware layer for this chip** | **23.6%** | **must reimplement (open/)** |

So "replace the blob" is not a 1.54 MB problem, let alone 7.35 MB: it is a
**~372 KB hardware-layer problem**. Within that 372 KB:

| Hardware-layer part | Share of hw layer | State |
|---|---|---|
| AC-PHY + 2069 radio + PMU | 46% | **done in `open/`, emulator-validated** |
| MAC (`wlc_bmac.c`/`wlc_hw.c`), backplane, DMA, SROM/OTP, PHY glue | 54% | remaining |

The 46% that is done is the hard, novel part — the AC-PHY that no open driver
has ever had (b43's is `CONFIG_BROKEN`). The remaining 54% is conventional MAC /
bus / DMA / SROM code of the kind b43 and brcmsmac already show how to write.

What stays non-source even when the code is fully open: the **MAC microcode**
loaded into the D11 core (no open version exists for this core revision ~42;
OpenFWWF only ever covered revision 5 — BCM4306/4311/4318), and the **PHY/radio
tables and init-values** (these *can* be open C, as b43 ships them, or extracted
with `fwcut.py`). Per-device SROM/calibration is read from the card, not shipped.

## Current state (measured, not estimated)

- `open/` reimplements the AC-PHY hardware layer (radio 2069, PHY attach/init,
  channel set, rxgain/desense/txpower, calibrations). It reproduces the object
  access-for-access in the emulator: 21/21 scenarios, 0 differences.
- It has **never run on a real card.** The card model uses *assumed* identity
  values (chip/PHY/radio revisions, SROM) and fakes calibration results.
- The default `bcm4360.ko` still links the full 7.35 MB `wlc_hybrid.o_shipped`
  and runs none of `open/`. The experimental HYBRID build (below) runs parts
  of `open/` in place of the object's code.
- The MAC layer is specified for bring-up (`bmac-init`) only; the data path is
  not specified or implemented.

## The hybrid path: replace the object layer by layer, inside the working driver

The stages below end in a new driver. Before that, the open code can already
replace the object's own code in the driver that works today ("strangler"
build, [`hybrid/README.md`](../../hybrid/README.md)). This is the measure of
progress that matters at runtime: how much of what the driver executes is
still Broadcom's code.

- Every call between named functions of the object is a relocation against
  the callee's symbol (no call bypasses it, checked per function), so a weak
  copy of the object plus a bridge that defines the same names redirects all
  of them to the open code. The bridges only marshal the object's structures
  into the open interfaces.
- `tools/re/hybrid.py` runs the object with the bridges in place through a
  whole session and compares every bus access with the object alone, and
  measures how much object code is still executed.
- Done (0 differences): `access`, `pmu`, `mac` (37 functions; 8.9% of the
  execution is open code; 36 functions of the object no longer run).
- Next, by size of the object code a session runs:
  1. **The PHY** (`wlc_phy_ac.c` + `wlc_phy_cmn.c`: 72.7 KB, 38% of the object
     code a session runs). Measured: no code outside the PHY touches the
     PHY's memory and the PHY touches none of the MAC's, so it can be
     replaced as a whole at its 83 entry points. 23 of them are specified;
     `phy-cmn` (in progress) specifies the other 60.
  2. **The rest of `wlc_bmac.c`** (20 KB, 10.6%): the bring-up is specified
     (`bmac-init.md`), implementation continues increment by increment.
  3. Backplane (`siutils.c`, `aiutils.c`, `nicpci.c`: 9.4 KB, 5%), DMA
     (`hnddma.c`, 2%), SROM/OTP (`bcmsrom.c`, `bcmotp.c`, 2.1%): need specs.
- The upper 802.11 layers (`wlc.c`, `wlc_channel.c`, CLM, scan, association:
  most of the remainder) stay the object's in the hybrid; the new driver gets
  them from mac80211 (stage 4).

## The critical path, with its honest gates

**Gate 0 — hardware validation of the open PHY (blocks everything downstream).**
Until the open PHY is shown to drive a *real* 4360, the 0-difference emulator
result is necessary but not sufficient: the model's assumed identity values and
faked measurements could be hiding real divergence. Steps (need the MacBook, the
card, the present driver loaded):
  1. `peek-on-target.py`: read the real chip/PHY/radio revisions and SROM/OTP.
  2. Correct `tools/re/bcm4360.py` with the measured identity; re-run scenarios.
  3. `capture-on-target.sh`: record the real driver's register trace; replay it
     (`mmiotrace.py replay`) so the object runs against the values the real card
     returned, and re-compare. This is the first point at which "it matches"
     says something about silicon rather than about the model.
Only the user can do this, and it is the highest-value next step by a wide margin.

**Stage 1 — MAC core bring-up.** `bmac-init` spec is done. Implement it the same
way the PHY was: the coordinator writes a MAC A/B scenario harness (`wlc_bmac_*`
on a MAC context, `phy_scenarios.py`-style) and a task file; an implementer
writes `open/mac/` from the spec only; compare in the emulator. Open question
from the spec: the real MAC capabilities word (`D11` 0x15c reads 0 in the model)
is resolved by Gate 0.

**Stage 2 — MAC data path (`bmac-data`).** DMA / FIFOs / interrupts, the TX/RX
path. Needs an analyst spec first, and the emulator extended to simulate packet
DMA and interrupts (substantial). This is where the work stops being a bring-up
demonstration and starts moving frames.

**Stage 3 — firmware as external data.** `fwcut.py` already cuts the microcode,
initial values and PHY/radio tables out of the object on the user's machine.
These become a redistributable firmware file loaded at runtime — not in this
repo, not linked into the `.ko`. This is the step that makes the *code* fully
open while being honest that the *data* stays Broadcom's.

**Stage 4 — driver frame.** A new mac80211 driver on the kernel's `bcma` bus (as
`brcmsmac`/`b43` are built), with `open/` as its hardware layer; or add the
AC-PHY to `b43` if its MAC layer handles core revision 42. Decidable only after
Gate 0, with the card measured.

## Who can legitimately do what

- **Coordinator** (this session): tooling, A/B harnesses, task files, spec
  cleanups, roadmap and status docs; spawns analyst/implementer subagents. Has
  seen decompiler output, so writes no code in `open/`.
- **Analysts** (subagents): read the decompilation, write behavioral specs.
- **Implementers** (fresh subagents): write `open/` from specs only.
- **The user:** runs anything on real hardware (Gate 0, final load tests); owns
  the decision to run the reverse engineering and the legal risk of it.

## Honest cost, and the realistic near-term deliverable

A *working* open driver for this chip is community-scale, multi-year work with
real hardware — the AC-PHY is the exact problem open Broadcom efforts have not
solved in a decade. The realistic near-term deliverable from this project is
narrower and still worth having: an open AC-PHY hardware layer **validated on a
real 4360** (Gate 0), plus the specifications behind it. That is a usable seed
for `b43`'s missing AC-PHY and the first open description of how this PHY is
driven — and it is the point at which the work stops being a simulator result
and becomes new open capability.
