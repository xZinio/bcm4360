# Questions of the implementer: task `chip-core`

Specification: [`../spec/chip.md`](../spec/chip.md). Code:
`open/include/bcm4360/si.h` (given), `open/chip/si.c`.

## Test results

* `python ab.py build`: no warnings.
* `hybrid.py check`, `check --off si`, `check --on phy`, `check --on phy --off
  si`: 41416 accesses of the object, 41416 of the hybrid core, the same.
* `hybrid.py srom`: all ten cards the same.
* `python ab.py all`: passes (no scenario of this layer; the others are
  unchanged).

To make sure the open layer is really used by default, I ran `check` once
with a deliberately wrong copy of `si.c` (`core_cflags` without its write)
mounted over `open/chip/si.c` inside the container only (the file in the
repository was not touched): 93+ differences on `d11.wrap 0x408`. The default
`check` therefore runs this code.

The tests did not contradict the specification anywhere. The points below
are what the specification leaves open, what the session does not reach, and
what I decided myself. For each: what the specification says, what the test
shows, what I did.

## 1. ai_corereg (and si_get/set_sromctl) on a core that does not exist

* Specification: "si_corereg and ai_corereg" and open question 4: an index
  between the number of cores and 31 that is neither ChipCommon nor the bus
  core makes `ai_setcoreidx` return 0, and the access goes to address
  `regoff` (a fault).
* Test: not reached (the session uses cores 0 and 3 only).
* What I did: the register access is left out (result 0); everything else
  happens as specified (callback "off", no window move, the restore when
  `orig` != `idx`, callback "restore"). Likewise `si_get_sromctl` /
  `si_set_sromctl` leave out the register access if `si_setcoreidx(0)` gives
  NULL (only possible with 0 cores).

## 2. si_clkctl_xtal on old PCI bridges (not this card)

* Specification: "si_clkctl_xtal", third bullet.
* Test: not reached (bus core 0x83c returns -1 without access).
* Unclear and decided:
  1. "On: unless the crystal is already on (in & 0x40), for the crystal:
     ...; for the PLL: ...". I read the test as applying to the crystal step
     only: crystal selected and `in & 0x40` = 0 -> crystal step; PLL
     selected -> PLL step (even when the crystal was already on). Or does an
     already running crystal end the whole "on" branch (return 0)?
  2. The crystal step writes 0xb4 and 0xb8, but only the change of `out` is
     given. I set `outen |= ` the same bits as `out` (0x40, and 0x80 if the
     PLL is selected). What is written to 0xb8?
  3. Off: I change only the selected bits (crystal bit cleared if bit 0 of
     `what`, PLL bit set if bit 1). Correct?

## 3. si_chip_hostif for chip 0x4335 (not this card)

* Specification: "from chip status bits 2/0/3 (4 if bit 0, 2 if bit 2, else
  bit 3)".
* What I did: chip status bit 0 -> 4, else bit 2 -> 2, else the value of bit
  3 (1 or 0). The order of the first two tests matters when both bits are
  set ("bits 2/0/3" could mean bit 2 is tested first). Which order, and is
  "else bit 3" = 1 when set, 0 otherwise?

## 4. si_fabid of other chips

* Specification: "si_fabid and si_otp_fabid".
* What I did: 0x4360 (and every chip not named): 0 without access.
  0x4324/0x4334/0x4335/0x4350/0xa8ea/0xa8eb: `CC(0xf8) & 0xf`, read with the
  `sub_021d1d` access pattern (corereg on core 0; the section of `sub_021d1d`
  says it is used by `si_fabid` of other chips). 0x4330: "from `CC(0x28)`"
  does not say which bits; I return 0. The OTP chips (0x4329, 0x5356,
  0xa962) need `si_otp_fabid`, which is not part of this interface; I return
  0. Which bits of `CC(0x28)` for 0x4330?

## 5. si_ldo_war for the old devices (not this card)

* Specification: "si_ldo_war".
* Test: not reached (one configuration read, result 1).
* Decided:
  1. "restores `PCICFG(0x80)`": restores what? I read `PCICFG(0x80)` just
     before moving window 1 and write that value back.
  2. "polls `PCICFG(0xa8)` bit 16 up to 20000 us": I wait until the bit is
     *set*, with the `SPINWAIT` step of 10 us (at most 2000 delays, 2001
     reads).
  3. "(returns 0 on timeout) and restores": I return 0 at once on the
     time-out, without restoring `PCICFG(0x80)`. Is the window restored
     before the `return 0`?
  4. The ChipCommon writes go through window 1 (BAR0 + 0x658, + 0x65c, +
     0x618), as "moves window 1 to 0x18000000" suggests.

## 6. Things not served (as the task allows)

* System bus (bus type 0): `ai_setcoreidx` step 3 (mapping of cores) is not
  done; on any bus other than PCI only `curidx` is set and NULL returned
  (step 4). `ai_corereg` uses no fixed window off PCI.
* The GPIO reservation mask (`.bss+0x334`, system bus only, a global) is not
  kept: on PCI `mask` and `val` pass unchanged, which is what the
  specification says for PCI; `priority` is unused.
* Indices outside the tables: `si_coreid`, `si_corerev`, `si_corevendor`
  give 0 for a current index >= 32 (cannot happen after `setcoreidx`);
  `si_findcoreidx` scans at most 32 entries.

## 7. Notes, no answer needed

* `si_switch_core` step 1: whether `*origidx` = coreid is written also when
  the core is neither ChipCommon nor the bus core makes no difference (step
  2 overwrites it). I write it first under FIXEDWIN, as the text reads.
* The callbacks are tested separately: "off" needs `intr_off` != NULL,
  "restore" needs `intr_restore` != NULL (with `s` = 0 when "off" was not
  called), as specified (`sih+0x78` / `sih+0x80`).
* Not reached by the session, written from the text only: the slow path of
  `corereg`, `switch_core` / `restore_core`, the time-outs of the polling
  loops (core reset: SPINWAIT 5000 each, at most 10 reads of resetctrl;
  disable: 300 then 10000), `core_disable` on a core already in reset, the
  `core_cflags` guards of other chips, `core_cflags_wo`, `ccreg`, `gpioin`,
  `gpiotimerval`, `clkctl_xtal` / `ldo_war` / `chip_hostif` / `fabid` of other
  chips and buses.

## Resolution (coordinator, 2026-11-02)

Accepted as implemented. Questions 2 to 5 concern other chips (old PCI
bridges, 0x4335, 0x4330, the OTP fab id, the `ldo_war` devices): the adapter
`hybrid/si.c` calls the open code only for the 4360 family on an AXI
backplane over PCI, so those paths never run in the open code; Broadcom's
function serves the others. 1 (a core index without a core) does not occur:
the callers pass indices from the scan. 6, 7: as the task allows.
