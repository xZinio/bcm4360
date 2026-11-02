# Task `chip-core`: core selection, register access, wrapper, GPIO

Specification: [`../spec/chip.md`](../spec/chip.md), sections **Overview**,
**Data** (`si_info_t`, the AXI wrapper registers, the ChipCommon registers,
polling loops), and the procedures under **Accessors without hardware
access**, **Selecting the current core**, **Register access of any core**,
**AXI wrapper of the current core**, **GPIO**, **Crystal, device presence,
SROM control, host interface** (up to and including `si_tcm_size`). Not the
PCIe bridge and not the attach (later tasks).

Interface: `open/include/bcm4360/si.h` (given: keep the prototypes and the
structure exactly). Write **`open/chip/si.c`**.

## Why

The backplane functions are called by every other layer (the MAC's core
flags alone 136 times in a session). With this task they become open code
inside the real driver (hybrid build, switch `open_si`, adapter
`hybrid/si.c`, already written); Broadcom's `si_attach` keeps building the
backplane structure, whose fields the adapter hands you in place.

## Rules for this code

* Clean room: `../implementing.md`, binding (mind its note on struct
  initializers becoming `memset` calls).
* Implement the AXI / PCI path the sections describe, for chips of the 4360
  family (the adapter calls you only for those; for the SB backplane or other
  bus types a comment is enough). The specification's `ai_*` function of a
  pair is your implementation of the `si_*` one (the dispatch on the
  backplane type is the adapter's).
* Windows: window 1 is `si->hw->d11` (BAR0), window 2 (the current core's
  wrapper, `sih+0x548`) is `si->hw->wrap`, ChipCommon's fixed window
  `si->hw->cc`, the bus core's `si->hw->pcie`. "FIXEDWIN" holds on this card
  (`buscoreid` 0x83c). Configuration registers: `hw_pcicfg_read` /
  `hw_pcicfg_write` (32 bit). A register pointer returned by `setcoreidx`,
  `setcore`, `switch_core` is `si->hw->bar0` (the specification's `sih+0xb8`)
  or an offset into it.
* `*si->curidx` is the current core: read it and write it where the
  specification reads and writes `sih+0x1c0`; never keep it in a local across
  a call that can change it.
* The interrupt callbacks: call `si->intr_off(si->intr_arg)` and
  `si->intr_restore(si->intr_arg, state)` exactly where the specification
  does (only when the pointer is not NULL and the current core's id equals
  `intr_coreid`).
* Polling loops, delays (`hw_udelay`) and the order of every access as
  specified; the tests compare them access by access.

## Tests

From `tools/re` (Docker, Linux tools; Docker Desktop is running; the adapter
`hybrid/si.c` is linked automatically as soon as `open/chip/si.c` exists):

* `python ab.py build` (no warnings).
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-si bcm4360-emu:latest python hybrid.py
  check` must report **the same**; likewise `check --off si`, `check --on
  phy`, `check --on phy --off si`, and `srom` (attach from ten cards: the
  attach selects cores and resets the 802.11 core through these functions).
  In the session the core flags are read/written 136 times, `corereg` 45
  times, `setcoreidx` 36, the core reset 12, `iscoreup` 12.
* `python ab.py all` must still pass.

The slow path of `corereg` (a core other than ChipCommon and the bus core),
`switch_core`/`restore_core`, the time-outs of the polling loops and
`ldo_war` for the old devices are not reached by the session: write them from
the specification.

Do not edit `hybrid/` or `tools/`. Questions into
`docs/re/questions/chip-core.md`; finish with a short report.
