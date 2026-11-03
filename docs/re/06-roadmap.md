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
  access-for-access in the emulator: 23/23 scenarios, 0 differences.
- Since the hybrid build (`hybrid/`, see below), open code **does** run inside
  the shipping `bcm4360.ko`, in place of Broadcom's: PMU, register access, MAC
  control and object memories, utilities, regulatory database, power
  container and SROM variables are taken over by default (223 functions),
  each verified identical over a whole session with its fallback too. The
  open AC-PHY takes over all 79 entry points of Broadcom's PHY with the
  session identical; it is in the module behind `open_phy`, off by default
  until it has run on a card.
- It has **never run on a real card.** The card model uses *assumed* identity
  values (chip/PHY/radio revisions, SROM) and fakes calibration results, so the
  hybrid result says the open code does to the *model* exactly what the object
  does, not yet that it drives silicon.
- The MAC layer is specified for bring-up (`bmac-init`) only; the data path is
  not specified or implemented.

## What runs inside the module now (the hybrid build)

Until now `open/` was only ever exercised in the emulator. The hybrid build
(`hybrid/README.md`) links Broadcom's core together with open code that takes
over functions of it at link time — the functions named in `hybrid/takeover.txt`
are made weak in a copy of the object (`scripts/mkhybrid.sh`), so the open
definitions win every call, and Broadcom's own bodies stay reachable as
`bcm4360_blob_*` for the fallback. Each layer has a module parameter that
switches it back to Broadcom's code; `make HYBRID=0` drops all of it and builds
exactly as 1.0.3.

`tools/re/hybrid.py` builds the same core for the emulator and runs the whole
session on it and on the object alone, comparing every access. Measured over
attach, up, nine channel changes, watchdog, down, up, down, detach:

| Build | Broadcom code executed in the session | Open code executed | Share of all execution in open code |
|---|---|---|---|
| object alone (1.0.3) | 200,461 bytes (100 %) | — | 0 % |
| hybrid, PMU taken over | 197,888 bytes (98.7 %) | 2,716 bytes | — |
| hybrid, PMU + register access + MAC control + utilities | 194,769 bytes (97.2 %) | 15,765 bytes | **33.0 %** |
| + regulatory database (2026-11-01) | 187,114 bytes (93.4 %) | 25,831 bytes | 64.7 % |
| + PHY's variable lookup, MAC object memories, power container, SROM variables (2026-11-02) | 179,205 bytes (89.4 %) | 45,101 bytes | **91.2 %** |
| the same + the open AC-PHY (`open_phy=1`) | 107,732 bytes (53.8 %) | 98,356 bytes | **93.3 %** |
| + MAC clocks/reset/initialisation/up and down/channel and band/run time/coexistence, backplane, DMA engines (2026-11-02) | 160,443 bytes (80.1 %) | 74,069 bytes | **92.9 %** |
| the same + the open AC-PHY (`open_phy=1`) | 88,970 bytes (44.4 %) | 127,190 bytes | **95.3 %** |
| + chanspec helpers and channel validity, the iovar dispatcher (2026-11-03) | 158,509 bytes (79.1 %) | 77,249 bytes | **95.0 %** |
| the same + the open AC-PHY (`open_phy=1`) | 87,040 bytes (43.4 %) | 130,446 bytes | **97.8 %** |

The last column counts every execution of a basic block: the register access
primitives and the board-variable lookup (`getvar`, about a fifth of all
execution on its own) are small but run constantly. The regulatory database
lookup, Broadcom's PHY's own variable lookup, the power container and the
SROM variable building were the next largest; with them open, more than nine
tenths of everything the driver does in the session is open code. What
Broadcom code still runs is spread thin (MAC, channel/rate logic of the upper
layer, backplane, DMA). The bytes column is now the more telling one: by
default Broadcom's own AC-PHY still runs (about 70 KB of it), which only the
switch `open_phy` - waiting for a test on the card - removes. These are small layers in
bytes; the number that matters for the static share is the AC-PHY, which is the
bulk of the hardware layer and the part no open driver has. Once its 79 entry
points are all answered by `open/` and the `--on phy` session matches the
object, the executed-closed-code share of a bring-up drops to the MAC/bus/DMA
remainder. The static measure stays as in the table above (the blob is still
linked, so its code is still *present*); what the hybrid build changes is how
much of it *runs*, and it is the first time the open AC-PHY meets anything but
the emulator.

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

**Stage 0.5 — the hybrid build (landed).** The mechanism that lets open code
take over functions of the object inside `bcm4360.ko` at link time, with a
per-layer switch and a fallback to Broadcom's own code (`hybrid/`,
`scripts/mkhybrid.sh`, `tools/re/hybrid.py`). The PMU, register access, MAC
control and utility layers are taken over and verified identical over a full
session (each also with its switch off); the AC-PHY is wired the same way at its
79 entry points and the open side is being completed. This turns every later
"replace layer X" into: write/finish the open layer, list its entry points in
`takeover.txt`, flip its switch, confirm the `hybrid.py` session still matches.
It is independent of Gate 0 (it runs in the emulator), but the *point* of it is
that the same object code then runs on the real card with the open layer in
place, so it is what carries Gate 0's result into the shipping module.

**Stage 1 — MAC core bring-up.** `bmac-init` spec is done. Implement it the same
way the PHY was: the coordinator writes a MAC A/B scenario harness (`wlc_bmac_*`
on a MAC context, `phy_scenarios.py`-style) and a task file; an implementer
writes `open/mac/` from the spec only; compare in the emulator, then take it
over in the hybrid build (Stage 0.5). Open question from the spec: the real MAC
capabilities word (`D11` 0x15c reads 0 in the model) is resolved by Gate 0.

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
