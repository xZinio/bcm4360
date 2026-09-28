# Bringing the MAC core up: reset, clocks, microcode, MAC state (wlc_bmac.c)

Status: WORK IN PROGRESS. Sections are appended as the analysis proceeds; a
section that is present was read in the decompiler output, checked in the
disassembly and compared with the emulator trace unless it says otherwise.
Register notation: [access.md](access.md).

## Scope

`wlc_bmac.c` (.text 0x05fd0c..0x06aa64) and `wlc_hw.c` for MAC core revision
42 (chip 0x4360, PCIe). The list of functions with offsets and sizes is
completed at the end of the analysis (see "Procedures" for what is present).

## Overview

(to be completed)

## Data

### Notation used in the procedures

* `cflags(mask, value)`: `si_core_cflags(sih, mask, value)` with the 802.11
  core selected: read `WRAP(d11, 0x408)`, write `(old & ~mask) | value`, read
  `WRAP(d11, 0x408)` again (three accesses, the write is made also when
  nothing changes). The inside belongs to the specification `chip`.
* `SPINWAIT(cond, us)`: evaluate `cond`; while it is true and fewer than `us`
  microseconds have been waited: `osl_delay(10)`, evaluate again. At most
  `us/10 + 1` evaluations and `us/10` delays.
* Delays are `osl_delay` in microseconds.

### Bits of the MAC control register `D11(0x120)`

Names as in the open `brcmsmac` driver (same code family); the values are
those the object uses.

| Bit | Mask | Name | Meaning |
|---|---|---|---|
| 0 | 0x00000001 | EN_MAC | MAC enabled (cleared to suspend it) |
| 1 | 0x00000002 | PSM_RUN | microcode processor runs |
| 2 | 0x00000004 | PSM_JMP_0 | microcode processor is held at address 0 |
| 8 | 0x00000100 | SHM_EN | |
| 10 | 0x00000400 | IHR_EN | access to the internal registers enabled |
| 17 | 0x00020000 | INFRA | infrastructure mode |
| 18 | 0x00040000 | AP | access point mode |
| 26 | 0x04000000 | WAKE | microcode must stay awake |
| 30 | 0x40000000 | DISCARD_PMQ | |

### Fields of `wlc_hw` used by the MAC state functions

| Offset | Size | Name | Meaning |
|---|---|---|---|
| 0x164 | 1 | suspended_fifos | bit mask of suspended transmit FIFOs; cleared by the core reset |
| 0x168 | 4 | maccontrol | software copy of the MAC control register **without** the overrides below |
| 0x16c | 4 | mac_suspend_depth | nesting counter of suspend/enable |
| 0x170 | 4 | wake_override | bit mask of reasons to keep the microcode awake: 0x01 clock control, 0x02 PHY register access, 0x04 MAC suspended, 0x08 transmit FIFO, 0x10 fast clock forced |
| 0x174 | 4 | mute_override | not 0: transmitter muted |
| 0x185 | 1 | forcefastclk | fast clock is forced |
| 0x186 | 1 | clk | the core's clock is on, registers may be accessed |
| 0x190 | 1 | phyclk | PHY clock is on (argument of the last `wlc_bmac_core_phy_clk`) |
| 0x192 | 2 | fastpwrup_dly | power up delay of the fast clock in microseconds |

## Procedures

### A. Services used by the PHY and by everybody else

#### A1. sub_0605ad (.text+0x0605ad, 73 bytes, name assigned: wlc_mctrl_write)

Purpose: write the MAC control register from the software copy and the
overrides. Input: `wlc_hw`. Steps:

1. v = `maccontrol` (`wlc_hw+0x168`).
2. If `wake_override` (`wlc_hw+0x170`) != 0: v |= 0x04000000.
3. If `mute_override` (`wlc_hw+0x174`) != 0: v = (v & ~0x00040000) | 0x00020000.
4. `D11(0x120) = v` (32 bit). No read.

#### A2. wlc_bmac_mctrl (.text+0x06066d, 33 bytes, name original)

Inputs: `wlc_hw`, `mask`, `val` (32 bit each). Steps:

1. new = (`maccontrol` & ~mask) | val. (`val` is not limited to `mask`.)
2. If new equals `maccontrol`: return, **no register access**.
3. `maccontrol` = new; A1 (one write of `D11(0x120)`).

Because of step 2 the number of writes depends on the state: a caller that
sets a bit that is already set in the software copy causes no access.

Mask/value pairs used in this area (complete list in the Overview):
`(0xffffffff, 0x04000400)` after a core reset; `(1, 0)` suspend; `(1, 1)`
enable; see the functions below.

#### A3. wlc_ucode_wake_override_set (.text+0x0639c3, 65 bytes, name original)

Inputs: `wlc_hw`, `bit` (one of the `wake_override` bits). Steps:

1. If `wake_override` != 0 or bit 26 (WAKE) of `maccontrol` is set:
   `wake_override` |= bit; return (no access: the microcode is awake already).
2. Else: `wake_override` = bit; A1 (`D11(0x120)` written with WAKE set);
   `wlc_bmac_wait_for_wake` (A5).

`wlapi_bmac_ucode_wake_override_phyreg_set(shim)` calls it with bit 0x02.

#### A4. wlc_ucode_wake_override_clear (.text+0x0605f6, 38 bytes, name original)

Inputs: `wlc_hw`, `bit`. Steps:

1. `wake_override` &= ~bit.
2. If `wake_override` is 0 now and bit 26 of `maccontrol` is clear: A1 (one
   write of `D11(0x120)`, now without WAKE). Otherwise no access. (The write
   is made also when the bit was not set before the call.)

`wlapi_bmac_ucode_wake_override_phyreg_clear(shim)` calls it with bit 0x02.

#### A5. wlc_bmac_wait_for_wake (.text+0x063946, 125 bytes, name original)

Purpose: wait until the microcode has left the sleep state. Steps for MAC
revision 42 (revision 4: only a delay of 5; G-PHY on revision 5: first delay
2000):

1. Delay 40.
2. Read `SHM(0x40)` (state of the microcode, 4 = asleep). While the value is 4
   and the time budget is not used up: delay 10, read again. The budget is
   `fastpwrup_dly` (`wlc_hw+0x192`) microseconds, i.e. at most
   ceil(fastpwrup_dly / 10) delays and one read more than delays. With a
   budget of 0 there is exactly one read.

In the traces the value read is 0 (the model has no microcode), so there is
always exactly one read.

#### A6. wlc_bmac_suspend_mac_and_wait (.text+0x063a04, 279 bytes, name original)

Purpose: stop the MAC (the microcode finishes the frame exchange in progress
and then idles). Calls nest: only the outermost call acts. Input: `wlc_hw`.
Steps:

1. `mac_suspend_depth` (`wlc_hw+0x16c`) += 1. If it is greater than 1 now:
   return (no access).
2. `wlc_ucode_wake_override_set(wlc_hw, 0x04)` (A3).
3. (MAC revisions < 13 with a 3 wire Bluetooth coexistence interface: GPIO
   control; not on this card.)
4. Read `D11(0x120)` (32 bit). If the value is 0xffffffff the card is gone:
   `wl_down(wl)` and return. (The counter stays incremented.)
5. Read `D11(0x128)` (32 bit); 0xffffffff: as in step 4.
6. `wlc_bmac_mctrl(wlc_hw, 1, 0)`: clears EN_MAC (one write of `D11(0x120)`
   if the software copy had the bit set).
7. `SPINWAIT((D11(0x128) & 1) == 0, 83000)`: wait for the interrupt status bit
   0 ("MAC suspended"), 32 bit reads.
8. Read `D11(0x128)` once more (value not used), read `D11(0x120)`; if that is
   0xffffffff: `wl_down(wl)` and return.
9. (Chips 0xd144 and 0x5357 only: `wlc_bmac_mctrl(wlc_hw, 2, 0)`.)

The interrupt status bit is not acknowledged here; `wlc_bmac_enable_mac` does
that. There is no error path when the wait of step 7 runs out.

Trace (`up-trace.txt`, `wlc_rfaware_lifetime_set`): W `D11(0x120)` = 0x44020403,
[A5: `SHM(0x40)`], R `D11(0x120)`, R `D11(0x128)`, W `D11(0x120)` = 0x44020402,
R `D11(0x128)` (bit 0 set: the model sets it at once), R `D11(0x128)`,
R `D11(0x120)`.

#### A7. wlc_bmac_enable_mac (.text+0x060d3d, 216 bytes, name original)

Input: `wlc_hw`. Steps:

1. `mac_suspend_depth` -= 1. If it is not 0 now: return (no access).
2. Read `D11(0x120)` (value not used).
3. `wlc_bmac_mctrl(wlc_hw, 1, 1)` (chips 0xd144, 0x5357: mask and value 3).
4. `D11(0x128) = 1` (32 bit): acknowledges "MAC suspended".
5. Read `D11(0x120)`, read `D11(0x128)` (values not used).
6. `wlc_ucode_wake_override_clear(wlc_hw, 0x04)` (A4).
7. (MAC revisions < 13 with 3 wire coexistence: GPIO control; not here.)

Trace: R `D11(0x120)`, W `D11(0x120)` = 0x44020403, W `D11(0x128)` = 1,
R `D11(0x120)`, R `D11(0x128)`, W `D11(0x120)` = 0x40020403.

#### A8. Shared memory access: wlc_bmac_read_shm, wlc_bmac_write_shm, wlc_bmac_set_shm and their workers

| Function | .text | Size | Name |
|---|---|---|---|
| `wlc_bmac_read_shm(wlc_hw, offset)` -> u16 | 0x061910 | 16 | original |
| `wlc_bmac_write_shm(wlc_hw, offset, value)` | 0x061fb0 | 19 | original |
| `wlc_bmac_set_shm(wlc_hw, offset, value, length)` | 0x061f64 | 76 | original |
| `sub_06188a(wlc_hw, offset, selector)` -> u16 | 0x06188a | 134 | assigned: `wlc_bmac_read_objmem` |
| `sub_061ed5(wlc_hw, offset, value, selector)` | 0x061ed5 | 143 | assigned: `wlc_bmac_write_objmem` |

The workers, with `selector` = memory number << 16 (0x10000 for the shared
memory; see section F for the others):

1. `tcm_sem_enter(wlc)` (below).
2. `D11(0x160) = selector | (offset >> 2)` (32 bit), then one read of
   `D11(0x160)` (32 bit, value not used).
3. One 16 bit access to `D11(0x164)` if `offset & 2` is 0, to `D11(0x166)`
   otherwise: read (result) or write of `value`.
4. `tcm_sem_exit(wlc)`.

Bit 0 of `offset` is ignored. `wlc_bmac_read_shm` and `wlc_bmac_write_shm` are
the workers with the selector 0x10000. `wlc_bmac_set_shm` writes the same
`value` to the words at `offset`, `offset + 2`, ... while the distance is less
than `length` (bytes; nothing for `length` <= 0), each with a complete worker
call (address written again every time).

`tcm_sem_enter` / `tcm_sem_exit` (wlc_offloads.c, .text+0x07eb0b, +0x07eb6f):
mutual exclusion with the firmware of the on-chip ARM (offloads). They act only
if the offload state `wlc+0x750` exists, its byte `+0x8c` is not 0 and its
pointer `+0x90` (a structure shared with the ARM) is not 0; then `enter` stores
1 in the 32 bit words `+0x14` and `+0x1c` of that structure and spins (at most
100000 rounds, no delay) while the word `+0x18` is 1 and `+0x1c` is 1; `exit`
stores 0 in `+0x14`. **Without offloads (the normal case and all traces) both
do nothing.** No register of the card is involved.

#### A9. wlc_bmac_mhf (.text+0x062995, 260 bytes), wlc_bmac_mhf_get (.text+0x05fd8b, 63 bytes), names original

Host flags are five 16 bit words that tell the microcode how to behave. The
driver keeps a copy per band and writes the copy of the current band to the
shared memory.

| Index | Shared memory word | brcmsmac name |
|---|---|---|
| 0 | `SHM(0x5e)` | host flags 1 |
| 1 | `SHM(0x60)` | host flags 2 |
| 2 | `SHM(0x62)` | host flags 3 |
| 3 | `SHM(0x78)` | host flags 4 |
| 4 | `SHM(0xd4)` | host flags 5 |

Copy of word `idx` of a band: 16 bit at `band + 8 + 2*idx`.

`wlc_bmac_mhf(wlc_hw, idx (u8), mask (u16), val (u16), bands (int))`:

1. Select the band state: `bands` = 1: the 5 GHz state (`wlc_hw+0xf8`);
   2: the 2.4 GHz state (`wlc_hw+0xf0`); 0 or 3: the current band
   (`wlc_hw+0xe8`); any other value: nothing is done at all.
2. If that band state exists: old = copy; new = (old & ~mask) | val; copy =
   new. If `clk` (`wlc_hw+0x186`) != 0 and new != old and the band state is
   the current band: `wlc_bmac_write_shm(wlc_hw, word of idx, new)`.
3. If `bands` = 3: the copies of both bands are set to (copy & ~mask) | val
   (no further access).

So the shared memory is written only when the value of the *current* band
changes while the clock is on; a call for the other band only changes the
copy. The copies are written to the shared memory as a whole by
`wlc_bmac_bsinit` (section C). `idx` is not checked.

`wlc_bmac_mhf_get(wlc_hw, idx, bands)`: returns the copy; `bands` 0 current
band, 1 5 GHz, 2 2.4 GHz, else 0; 0 if the band state does not exist. No
access.

`wlapi_bmac_mhf(shim, idx, mask, val, bands)` passes all arguments on.

#### A10. wlc_bmac_phyclk_fgc (.text+0x06502b, 65 bytes, name original)

Inputs: `wlc_hw`, `on` (u8). For PHY types 4 (N), 7 (HT) and 11 (AC) only:
`cflags(0x0002, on == 1 ? 0x0002 : 0)`. Three accesses, no delay. Bit 1 of the
wrapper's control register forces the gated clocks of the core on.

#### A11. wlc_bmac_macphyclk_set (.text+0x065006, 37 bytes, name original)

Inputs: `wlc_hw`, `on` (u8). `cflags(0x0010, on == 1 ? 0x0010 : 0)` (bit 4:
clock between MAC and PHY enabled). Three accesses, no delay.

#### A12. wlc_bmac_core_phy_clk (.text+0x06506c, 232 bytes, name original)

Inputs: `wlc_hw`, `on` (u8). `phyclk` (`wlc_hw+0x190`) = `on`. Then:

* `on` = 0: `cflags(0x200a, 0x000a)`; delay 1; `cflags(0x000a, 0x0008)`;
  delay 1. (PHY in reset with the clock forced, band flag cleared; then the
  forcing is removed.)
* `on` != 0, AC-PHY:
  1. MAC revision >= 40: `cflags(0x0300, 0x0100)`.
  2. `cflags(0x000e, 0)`; delay 1. (PHY reset released, PHY clock off, no
     forcing.)
  3. `cflags(0x0004, 0x0004)`; delay 1. (PHY clock on.)

  Other PHY types: step 2 is `cflags(0x000e, 0x0002)`, step 3
  `cflags(0x0006, 0x0004)`.

Bits of `WRAP(d11, 0x408)` used by the MAC code (names of brcmsmac where they
exist): 0x0001 clock enable (written by `si_core_reset`), 0x0002 FGC force
gated clocks, 0x0004 PCLKE PHY clock enable, 0x0008 PRST PHY reset, 0x0010
MPCLKE MAC-PHY clock, 0x00c0 bandwidth of the PHY clock, 0x0300 a two bit
field of MAC revisions >= 40 set to 1 (purpose unknown), 0x2000 band is
2.4 GHz.

#### A13. sub_064887 (.text+0x064887, 449 bytes, name assigned: wlc_bmac_clkctl_clk)

Purpose: force the fast (HT) clock of the MAC core or return to dynamic clock
selection. Inputs: `wlc_hw`, `mode` (0 = fast, anything else, the callers use
2 = dynamic). No result. With a PMU (bit 28 of the ChipCommon capabilities
`sih+0x18`; the case of this card):

1. If `clk` (`wlc_hw+0x186`) is 0: no access; continue with step 4.
2. `mode` = 0: `D11(0x1e0) = D11(0x1e0) | 0x2` (one 32 bit read, one write);
   delay 64; `SPINWAIT((D11(0x1e0) & 0x20000) == 0, 20000)`.
3. `mode` != 0: only if the PMU revision (`sih+0x20`) is 0 (not this card):
   read `D11(0x1e0)` and, if one of the bits 0x12 is set,
   `SPINWAIT((D11(0x1e0) & 0x20000) == 0, 20000)`. Then in every case
   `D11(0x1e0) = D11(0x1e0) & ~0x2` (one read, one write).
4. `forcefastclk` (`wlc_hw+0x185`) = (`mode` == 0).

Without PMU (other chips): `si_clkctl_cc`, host flag 0x400 of word 0 for MAC
revisions < 11, wake override bit 0x10; not described.

Trace (seq 1122): R `D11(0x1e0)` = 0x000f0000, W 0x000f0002, 64 us, R
0x000f0002. At the end of `wlc_up` (`wlc_bmac_up_finish`): R 0x000f0002, W
0x000f0000.

#### A14. wlc_bmac_corereset (.text+0x065e12, 553 bytes, name original)

Purpose: reset the 802.11 core including the PHY and leave it with the clock
forced fast (if it was before) and the microcode processor stopped. Inputs:
`wlc_hw`, `flags` (32 bit; bits for the wrapper's control register; the value
0xffffffff means "the flags of the current band"). Steps:

1. If `flags` = 0xffffffff: `flags` = 0 if the current band has no PHY
   (`band+0x28` = 0), else the 32 bit word `band+0x18` (core flags of the
   band; 0 in all traces).
2. fast = `forcefastclk`. If fast = 0: `wlc_bmac_clkctl_clk(wlc_hw, 0)` (A13;
   no access when `clk` is 0).
3. `si_iscoreup(sih)` (reads `WRAP(d11, 0x408)`, and `WRAP(d11, 0x800)` if the
   clock bit is set). If the core is up:
   1. For FIFO i = 0..5 with a DMA handle (`wlc_hw+0x20 + 8*i`): transmit
      reset of the channel (DMA function slot 0x10; specification
      `bmac-data`).
   2. If FIFO 0 has a handle: `sub_0638a5(wlc_hw, 0)`: receive reset of FIFO
      0 (DMA function slot 0xa8). (MAC revisions < 12 first write
      `D11(0x406)` and wait; revision 4 also resets FIFO 3.)
4. If `noreset` (`wlc_hw+0x184`) != 0: `wlc_hw+0x94` = 0;
   `wlc_bmac_mctrl(wlc_hw, 3, 0)`; **return** (the clock mode of step 2 is
   not restored).
5. `clk` = 0. `si_core_reset(sih, flags | 0x4, 0)` (MAC revisions >= 18; older
   ones pass the PHY clock bit in the third argument or not at all).
   `clk` = 1. If the current band has a PHY: `wlc_phy_hw_clk_state_upd(pi, 1)`.
6. AC-PHY only (current band exists and its PHY type is 11):
   `cflags(0x0300, 0x0100)`, `cflags(0x0006, 0)`, `cflags(0x0004, 0x0004)`
   (no delays between them).
7. Reset of the MAC control state: `maccontrol` = 0, `suspended_fifos` = 0,
   `wake_override` = 0, `mute_override` = 0;
   `wlc_bmac_mctrl(wlc_hw, 0xffffffff, 0x04000400)`: `D11(0x120) = 0x04000400`
   (WAKE and IHR_EN; the microcode processor is stopped).
8. With a PMU: `wlc_bmac_clkctl_clk(wlc_hw, 0)` (the reset cleared the force
   bit).
9. If there is a current band: `wlc_bmac_phy_reset(wlc_hw)` (A15).
10. `wlc_bmac_core_phypll_ctl(wlc_hw, 1)` (A16: nothing for revision 42).
11. `wlc_hw+0x94` (pending interrupt bits) = 0.
12. If fast = 0: `wlc_bmac_clkctl_clk(wlc_hw, 2)`.

Accesses of `si_core_reset(sih, bits, 0)` as the traces show them (the inside
belongs to the specification `chip`): R `WRAP(0x804)`; W `WRAP(0x408)` =
bits | 0x3; R `WRAP(0x408)`; R `WRAP(0x804)`; W `WRAP(0x800)` = 1; delay 10;
R `WRAP(0x800)`; R `WRAP(0x804)`; W `WRAP(0x800)` = 0; R `WRAP(0x804)`;
R `WRAP(0x800)`; W `WRAP(0x408)` = bits | 0x1; R `WRAP(0x408)`; delay 1.

Complete trace of a core reset with a PHY, 20 MHz, fast clock already forced
(seq 1099..1137): `si_iscoreup`; `si_core_reset` with bits 0x4 (wrapper
control 0x7, then 0x5); wrapper control 0x105, 0x101, 0x105; `D11(0x120)` =
0x04000400; `D11(0x1e0)` 0xf0000 -> 0xf0002, 64 us, poll; then A15.

`wlapi_bmac_corereset(shim, flags)` passes `flags` on (the PHY attach calls it
with the flags it wants, see `acphy-attach.md`).

#### A15. wlc_bmac_phy_reset (.text+0x0659ae, 1124 bytes, name original)

Purpose: reset the PHY through the wrapper of the 802.11 core. Input:
`wlc_hw`. Nothing is done without a current band or without its `pi`. Steps
for the AC-PHY (almost all of the function is for the N-PHY revisions 3, 4 and
18 and the SSLPN-PHY revisions 2 and 3 with their PLL programming: not
described):

1. bw = `wlc_phy_clk_bwbits(pi)` (0x40, 0x80 or 0xc0 for 20, 40, 80 MHz, see
   `acphy-chanspec.md` section 3).
2. `cflags(0x00ce, bw | 0x000e)`: bandwidth set, PHY reset asserted, PHY clock
   on, gated clocks forced.
3. Delay 2.
4. `wlc_bmac_core_phy_clk(wlc_hw, 1)` (A12): `cflags(0x0300, 0x0100)`,
   `cflags(0x000e, 0)`, delay 1, `cflags(0x0004, 0x0004)`, delay 1.
5. `wlc_phy_anacore(pi, 1)` (`acphy-radio.md` section 1; one write
   `D11(0x3e6) = 0`). (Not called for PHY type 10.)

Trace (seq 1125..1137, 20 MHz): wrapper control 0x105 -> 0x14f, 2 us, 0x14f
(written again), 0x141, 1 us, 0x145, 1 us, `D11(0x3e6)` = 0.

`wlapi_bmac_phy_reset(shim)` calls it.

#### A16. wlc_bmac_core_phypll_ctl (.text+0x063579, 532 bytes), wlc_bmac_core_phypll_reset (.text+0x065278, 192 bytes), names original

`wlc_bmac_core_phypll_ctl(wlc_hw, on)`: returns at once, without any access,
for MAC revisions below 17, 20, 27 and **40 or higher**. (Revisions 17..39:
requests the PHY PLL in `D11(0x1e0)` or `D11(0x4f0)` and waits up to 100 ms.)
On this card it never does anything.

`wlc_bmac_core_phypll_reset(wlc_hw)`: acts for PHY types 4 and 7 only
(`PMU_CHIPCTL[0]` bit 2 pulsed). Nothing for the AC-PHY.

#### A17. wlc_bmac_btc_mode_get (.text+0x06001d, 15 bytes, name original)

Returns the 32 bit word at offset 0 of the coexistence state `wlc_hw+0xb0`
(the Bluetooth coexistence mode: 0 = off). No access. How the mode is set:
section E (`wlc_bmac_btc_mode_set`).

#### A18. The wrappers of wlc_phy_shim.c

The shim is a structure of 0x18 bytes: `+0x00` `wlc_hw`, `+0x08` `wlc`,
`+0x10` `wl` (handle of the glue). Every wrapper replaces the shim by
`wlc_hw` and passes its other arguments on unchanged:

| Wrapper (.text) | Calls |
|---|---|
| `wlapi_suspend_mac_and_wait` (0x152ab0) | `wlc_bmac_suspend_mac_and_wait` |
| `wlapi_enable_mac` (0x152a90) | `wlc_bmac_enable_mac` |
| `wlapi_bmac_mctrl` (0x152a82) | `wlc_bmac_mctrl` |
| `wlapi_bmac_mhf` (0x152acc) | `wlc_bmac_mhf` |
| `wlapi_bmac_read_shm` (0x152ae4), `wlapi_bmac_write_shm` (0x152af2) | `wlc_bmac_read_shm`, `wlc_bmac_write_shm` |
| `wlapi_copyto_objmem` (0x15297c), `wlapi_copyfrom_objmem` (0x15298a) | `wlc_bmac_copyto_objmem`, `wlc_bmac_copyfrom_objmem` |
| `wlapi_bmac_write_template_ram` (0x1529cf) | `wlc_bmac_write_template_ram` |
| `wlapi_bmac_corereset` (0x152abe) | `wlc_bmac_corereset` |
| `wlapi_bmac_phy_reset` (0x152a74) | `wlc_bmac_phy_reset` |
| `wlapi_bmac_phyclk_fgc` (0x152a35) | `wlc_bmac_phyclk_fgc` |
| `wlapi_bmac_macphyclk_set` (0x152a23) | `wlc_bmac_macphyclk_set` |
| `wlapi_bmac_core_phypll_ctl` (0x152a11), `wlapi_bmac_core_phypll_reset` (0x152a03) | the functions of A16 |
| `wlapi_bmac_ucode_wake_override_phyreg_set` (0x1529f0), `..._clear` (0x1529dd) | `wlc_ucode_wake_override_set(wlc_hw, 2)`, `wlc_ucode_wake_override_clear(wlc_hw, 2)` |
| `wlapi_bmac_btc_mode_get` (0x152a47) | `wlc_bmac_btc_mode_get` |
| `wlapi_bmac_get_txant` (0x152a55) | `wlc_bmac_get_txant` |
| `wlapi_bmac_bw_set` (0x152a63) | `wlc_bmac_bw_set` |
| `wlapi_switch_macfreq` (0x152a9e) | `wlc_bmac_switch_macfreq` |
| `wlapi_bmac_rate_shm_offset` (0x1529bd) | `wlc_bmac_rate_shm_offset` |
| `wlapi_update_bt_chanspec` (0x152956) | `wlc_bmac_update_bt_chanspec` |
| `wlapi_intrson`, `wlapi_intrsoff`, `wlapi_intrsrestore` (0x152b21, 0x152b12, 0x152b03) | `wl_intrson(wl)`, `wl_intrsoff(wl)`, `wl_intrsrestore(wl, mask)` of the glue |

`wlapi_is_eci_coex_enabled(shim)`: 0 if the MAC revision is below 15, or bit
29 of the ChipCommon capabilities is clear, or bit 0 of the extended
capabilities (`sih+0x1c`) is set; else bit 29 of the MAC capabilities
(`wlc_hw+0xa4`). No access.

### B. wlc_bmac_init and what it calls

#### B1. wlc_bmac_init (.text+0x06828a, 5071 bytes, name original)

Purpose: from "core reset done" to "microcode running and suspended, MAC and
PHY initialised for a channel". The function contains, inlined, what the
open driver calls `coreinit`, the GPIO initialisation and the FIFO set-up.
Inputs: `wlc_hw`, `chanspec` (u16), `mute` (u8). No result. The caller
(`wlc_init`) has reset the core (`wlc_reset`). Steps for MAC revision 42,
AC-PHY, chip 0x4360 (0x4352 and 0xaa06 take the same branches except where
noted):

1. fast = `forcefastclk`; if 0: `wlc_bmac_clkctl_clk(wlc_hw, 0)` (A13).
2. mask = `wl_intrsoff(wl)` of the glue (`D11(0x12c) = 0`, read back;
   specification `bmac-data`).
3. Chips 0x4352, 0x4360, 0xaa06: `si_pmu_rfldo(sih, 1)` (`pmu.md`).
4. `wlc_setxband(wlc_hw, unit)` with unit = 1 if `chanspec & 0xc000` =
   0xc000, else 0 (`acphy-chanspec.md` section 2: `cflags(0x2000, ...)`).
5. `wlc_phy_chanspec_radio_set(pi, chanspec)`; `wlc_phy_cal_init(pi)`.
6. `wlc_bmac_mctrl(wlc_hw, 0xffffffff, 0x04000404)`: `D11(0x120)` =
   WAKE | IHR_EN | PSM_JMP_0.
7. `wlc_bmac_btc_mode_set(wlc_hw, mode)` with the current coexistence mode
   (first word of `wlc_hw+0xb0`); section E. With mode 0 and the driver not
   up: no access.
8. Bluetooth coexistence hardware, by board flags:
   * `boardflags2 & 0x80` (`wlc_hw+0x90`): chip specific calls for chips
     0x4331, 0x4313, 0xa9a7, 0xa8d1, 0xa87b, 0xa8db, 0xa8dc, 0xa9a4: nothing
     for the 4360 family.
   * If `boardflags & 0x1` (`wlc_hw+0x8c`): with `boardflags2 & 0x80`:
     `si_seci_init(sih, 3)` (not for chip 0x4331). Without it, with C =
     bit 29 of the MAC capabilities (`wlc_hw+0xa4`): if bit 29 of the
     ChipCommon capabilities is set and bit 0 of the extended capabilities
     (`sih+0x1c`) is clear and C: `si_eci_init(sih)`; else if bit 0 of the
     extended capabilities is set and C: `si_seci_init(sih, 1)`; else if bit
     2 of the extended capabilities is set and C: `si_gci_init(sih)`. (In the
     traces C is 0 because the model returns 0 for `D11(0x15c)`: none is
     called. Which one the real card takes is not known.)
9. Microcode download: B2.
10. Start of the microcode: B3 (result ignored).
11. `wlc_bmac_mctrl(wlc_hw, 0xc000, 0)`: clears the two bits 14..15 (selection
    of the GPIO outputs by the microcode); no access if they are clear in the
    software copy (the case here).
12. If the 32 bit word `+0x10` of the coexistence state is not 0:
    `sub_06106b(wlc_hw)` (section E; 0 in the traces).
13. Antenna selection and GPIO. Let gm = gc = 0. By the antenna selection
    type (byte `wlc_hw+0x1a1`):
    * 2, 3 or 6: `wlc_bmac_mhf(wlc_hw, 2, 1, 1, 3)`, `wlc_bmac_mhf(wlc_hw, 2,
      2, 2, 3)`, `wlc_phy_antsel_init(pi, 0)`.
    * 1: `D11(0x49e) |= 0x3000`, `D11(0x49c) |= 0x3000` (16 bit, read then
      write each), `wlc_bmac_mhf(wlc_hw, 2, 1, 1, 3)`, `wlc_bmac_mhf(wlc_hw,
      2, 2, 0, 3)`, `SHM(0xc2) = 6`, gm = gc = 0x3000.
    * anything else (0 in the traces): nothing.

    If `boardflags & 0x2`: gc |= 0x200, gm |= gc. (A block for chip 0x4322
    follows.) Then `si_gpiocontrol(sih, gm, gc, 0)` (with gm = gc = 0 this
    is one read of `CC(0x6c)`).
14. MAC revisions >= 40: the address match table is cleared: for i = 0..63
    `wlc_bmac_write_amt(wlc_hw, i, 00:00:00:00:00:00, 0)` (B5; 64 entries,
    two 32 bit writes each).
15. (If the 32 bit word `wlc_hw+0x1a4` is not 0: GPIO/chip control for chips
    other than the 4360 family; nothing for the family.)
16. Four bytes are read from the scratch registers of the microcode:
    `wlc_bmac_copyfrom_objmem(wlc_hw, 0x24, buffer, 4, 0x20000)` (two 16 bit
    reads: `D11(0x160)` = 0x00020009 / read back / `D11(0x164)`, then the
    same address and `D11(0x166)`). They are written back in step 27.
17. `si_core_sflags(sih, 0, 0)`: one read of `WRAP(d11, 0x500)`; the value is
    used by other revisions only.
18. Initialisation list: revision 42 with PHY type 11:
    `sub_060f67(wlc_hw, d11ac1initvals42)` (B6; 610 entries). (Revision 43:
    `d11ac3initvals43`; 41, 44..47: `d11ac2initvals41`; 40:
    `d11ac0initvals40`.) No list for another PHY type.
19. `sub_067efd(wlc_hw)` (B7). (MAC revisions below 40 set FIFO sizes in
    `D11(0x520)`, `D11(0x52c)`, `D11(0x540)` and `SHM(0x98..0x9e)` instead.)
20. `SHM(0x80) = 8` (frame burst limit), `SHM(0x5c) = 10` (antenna diversity
    count).
21. `D11(0x100) = ` the 32 bit word `wlc_hw+0x1ac` (receive interrupt
    "lazy" control of FIFO 0; 0x01000000 in the traces = one interrupt per
    frame, no time-out).
22. `wlc_bmac_mctrl(wlc_hw, 0x40060000, 0x40020000)`: INFRA and DISCARD_PMQ
    set, AP cleared (`D11(0x120)` = 0x44020402).
23. `D11(0x188) = 0x80000000`, `D11(0x18c) = 0x02000000` (32 bit; beacon
    period and start time of the contention free period: defaults),
    `D11(0x128) = 0x00004000` (acknowledges interrupt status bit 14),
    `D11(0x24) = 0x00010000` (interrupt mask of DMA channel 0: receive
    interrupt).
24. `wlc_bmac_macphyclk_set(wlc_hw, 1)` (A11).
25. d = `si_clkctl_fast_pwrup_delay(sih)` (`pmu.md`: 1500 us for chip
    revisions < 4, else 3000; 3700 for 0xa9c4/0xaa06);
    `D11(0x6a8) = d` (16 bit). `fastpwrup_dly` = d + `sub_05fdca(wlc_hw)`
    for MAC revisions > 40 (B8: 512 for the AC-PHY, so 2012 or 3512).
26. `SHM(0x16) = 42` (MAC revision); `SHM(0xc0)` = low half,
    `SHM(0xc2)` = high half of the MAC capabilities `wlc_hw+0xa4`.
27. Retry limits to the scratch registers:
    `wlc_bmac_copyto_objmem(wlc_hw, 0x18, &SRL, 2, 0x20000)` and
    `(wlc_hw, 0x1c, &LRL, 2, 0x20000)`: scratch register 6 = short retry
    limit (16 bit at `wlc_hw+0x104`; 7 in the traces), scratch register 7 =
    long retry limit (`wlc_hw+0x106`; 6 in the traces). Each is
    `D11(0x160)` = 0x00020006 resp. 0x00020007, read back, 16 bit write of
    `D11(0x164)`. Then, if the byte `wlc+0x718` is 0, the four bytes
    of step 16 are written back (`wlc_bmac_copyto_objmem(wlc_hw, 0x24,
    buffer, 4, 0x20000)`); if it is not 0 it is set to 0 and nothing is
    written.
28. `SHM(0x44)` = short fallback limit (`wlc_hw+0x108`), `SHM(0x46)` = long
    fallback limit (`wlc_hw+0x10a`).
29. `D11(0x688) = D11(0x688) & 0x0fff` (16 bit read and write),
    `D11(0x69c) = 1` (16 bit; minimum AIFSN).
30. `wlc+0x68` = 0 (32 bit). DMA: for FIFO i = 0..5 with a handle: transmit
    initialisation of the channel (DMA function slot 0x08); then for FIFO 0
    receive initialisation (slot 0xa0) and receive buffer fill (slot 0xd8).
    Specification `bmac-data`.
31. (Chips 0x4748, 0x4716, 0xb83a, 0x4314, 0x4334, 0xa886, 0xa887 write
    `D11(0x62e)`, `D11(0x630)` here; not the 4360 family, see step 36.)
32. Coexistence parameters: p = 2 * `SHM(0x92)` (read); stored as 16 bit in
    the coexistence state `+0x1a`. If p != 0:
    1. For n = 0..118: if a variable `btc_params<n>` (decimal) exists in the
       SROM variables: `SHM(p + 2n)` = its value.
    2. Chips 0x4352 and 0xa8dc only (**not 0x4360**): `SHM(p + 0x02) = 30000`,
       `SHM(p + 0x10) = 20000`, `SHM(p + 0x12) = 30000`, `SHM(p + 0x2c) = 0x753`.
    3. If a variable `btc_flags` exists: coexistence state `+0x08` (16 bit) =
       its value; `sub_062b79(wlc_hw)` (section E).
    4. MAC revisions >= 40: the MAC address of the interface (6 bytes at
       `pub+0x08`) is written to `SHM(0x78c)`, `SHM(0x78e)`, `SHM(0x790)`
       (bytes 0|1 << 8, 2|3 << 8, 4|5 << 8).
33. Read `SHM(0x8e)` (value used by MAC revision 33 only).
34. `mac_suspend_depth` = 1: the MAC counts as suspended once; the
    `wlc_bmac_enable_mac` of `wlc_init` starts it.
35. If `mute`: `wlc_bmac_mute(wlc_hw, 1, 1)` (section E).
36. Chips 0xa9c4, 0x4360, 0xaa06, 0x4352, 0x4350:
    `wlc_bmac_switch_macfreq(wlc_hw, 0)` (B9).
37. `sub_06656c(wlc_hw, chanspec, 0)`: band specific initialisation
    including `wlc_phy_init` (B10).
38. `wl_intrsrestore(wl, mask)`.
39. `wake_override` |= 0x04 (no access: the matching
    `wlc_bmac_enable_mac` clears it).
40. If fast was 0: `wlc_bmac_clkctl_clk(wlc_hw, 2)`.

Notes:

* Order in the trace (seq 1204..51444): 2, 3, 4, 6 (`D11(0x120)` =
  0x04000404), 9 (1217..22918), 10, 13 (one read of `CC(0x6c)`), 14
  (22924..23437), 16, 17, 18 (23447..24394), 19, 20..38.
* The values the driver reads from the shared memory in steps 32 and 33 are
  those the microcode and the initialisation list left there. In the traces
  `SHM(0x92)` reads 0x0a2a, which is an artefact: the model of the card does
  not advance the object address after a write, so every run of the list
  ends up in its first word. The list writes 0x0acc to `SHM(0x92)` (run at
  address 0x24, first data word 0x0acc0bfa), so on hardware p = 0x1598
  unless the microcode changes it (unverified).

<!-- APPEND-B1 -->

#### B2. sub_0607b0 (.text+0x0607b0, 1151 bytes, name by the guide: wlc_ucode_download) and sub_060744 (.text+0x060744, 108 bytes, name assigned: wlc_ucode_write)

Purpose: load the microcode into the memory of the microcode processor. It
is done once per attach. Input: `wlc_hw`. Steps:

1. If the byte `wlc_hw+0xae` (microcode loaded) is not 0: return, no access.
2. Select the image by MAC revision and PHY type of the current band
   (`band+0x1c`). Revision 42 with PHY type 11: `d11ucode42`, length in bytes
   from `d11ucode42sz` (0xa988 = 43400 bytes = 10850 words of 32 bit).
   Revision 42 with another PHY type: no image, nothing is written.
   (Revision 43: `d11ucode43`; 41, 44, 45, 46, 47: `d11ucode41`; 40:
   `d11ucode40`; older revisions: other images, some of them in a byte format
   written by `sub_06068e`; not described.)
3. `sub_060744(wlc_hw, image, length)`:
   1. If the static variable `ucode_chunk` (.bss+0xb34) is 0 (it is never
      written by the object, so always): `D11(0x160) = 0x03000000` (32 bit;
      memory 0 = microcode, address 0, bit 24 and bit 25 set: the address
      advances by itself after every write), then one read of `D11(0x160)`
      (32 bit, not used).
   2. For i = 0 .. length/4 - 1: `D11(0x164) = word i of the image` (32 bit
      writes; the image is an array of 32 bit words in the byte order of the
      host, little endian in the object).
4. `wlc_hw+0xae` = 1 (also when no image was found).

There is **no check afterwards**: nothing is read back, no checksum.
The flag `wlc_hw+0xae` is cleared in two places only: by `wlc_bmac_hw_up`
(section C; it runs when the hardware was powered up) and by
`wlc_bmac_btc_wire_set` (section E). A `wlc_bmac_init` that follows a
`wlc_reset` without them does not load the microcode again.

Trace (seq 1217..22918): `D11(0x160)` = 0x03000000, read back, then 10850
writes of `D11(0x164)`; the first words are 0x0300104e, 0x0001bc60,
0x02f00e25.

#### B3. Start of the microcode (code at the symbol wlc_bmac_wowlucode_start, .text+0x063828, 125 bytes)

The start sequence of the normal initialisation and that of the wake-on-WLAN
microcode are the same code; the linker kept one copy under the name
`wlc_bmac_wowlucode_start`. Input: `wlc_hw`. Result: 0, or -1 if the
microcode did not report itself. Steps:

1. `D11(0x128) = 0xffffffff` (32 bit): all interrupt status bits cleared.
2. `wlc_bmac_mctrl(wlc_hw, 0xffffffff, 0x04020402)`: `D11(0x120)` =
   0x04020402 = WAKE | INFRA | IHR_EN | PSM_RUN. PSM_JMP_0 is removed, the
   processor starts at address 0.
3. `SPINWAIT((D11(0x128) & 1) == 0, 1000000)`: the microcode initialises
   itself and then suspends itself, which sets bit 0 of the interrupt status;
   the driver waits at most 1 s (32 bit reads every 10 us).
4. One more read of `D11(0x128)`; result 0 if bit 0 is set, else -1.

`wlc_bmac_init` ignores the result. The status bit is not cleared here.

Trace (seq 22919..22922): W `D11(0x128)` = 0xffffffff, W `D11(0x120)` =
0x04020402, R `D11(0x128)` = 1, R `D11(0x128)` = 1 (the model answers at once;
how long the real microcode needs is not known).

#### B4. wlc_bmac_copyto_objmem (.text+0x062d48, 333 bytes), wlc_bmac_copyfrom_objmem (.text+0x061cf4, 303 bytes), names original

`wlc_bmac_copyto_objmem(wlc_hw, offset, buffer, length, selector)`: `offset`
and `length` in bytes, `buffer` holds the bytes in the order of rising
addresses, `selector` as in A8.

* `length` <= 0: nothing.
* `selector` without bit 18 (every memory except the address match table):
  for i = 0, 2, 4, ... while i < `length`: the worker `sub_061ed5(wlc_hw,
  offset + i, buffer[i] | buffer[i+1] << 8, selector)`: each 16 bit word with
  its own address write, read back and data write (A8). An odd `length` is
  rounded up (one byte beyond the buffer is read).
* `selector` with bit 18 (0x40000, address match table): 32 bit words. For
  i = 0, 4, ... while i < (`length` & ~3): `tcm_sem_enter`; `D11(0x160) =
  selector | ((offset + i) >> 2)`; read `D11(0x160)`; `D11(0x164) =
  buffer[i] | buffer[i+1] << 8 | buffer[i+2] << 16 | buffer[i+3] << 24`
  (32 bit); `tcm_sem_exit`. If `length & 3` is not 0, one 16 bit word more is
  written with the worker at `offset + i`.

`wlc_bmac_copyfrom_objmem(wlc_hw, offset, buffer, length, selector)`: the
same with reads (16 bit reads with `sub_06188a`; for the address match table
32 bit reads of `D11(0x164)`), bytes stored low byte first.

Selector values seen: 0x10000 shared memory, 0x20000 scratch registers of
the microcode, 0x40000 address match table. Note that for the scratch
registers the address is `offset >> 2` too, so consecutive scratch registers
are 4 apart in `offset` (register n = `offset` 4n, 16 bit each, in
`D11(0x164)`).

#### B5. wlc_bmac_write_amt (.text+0x06316a, 168 bytes, name original), sub_061e23 (.text+0x061e23, 96 bytes, name assigned: wlc_bmac_read_amt)

The address match table of MAC revisions >= 40 has 64 entries of 8 bytes:
a MAC address and a 16 bit attribute word.

`wlc_bmac_write_amt(wlc_hw, index, address (6 bytes), attributes (u16))`:

1. If bit 15 of `attributes` is set: read the entry first (`sub_061e23`: two
   32 bit reads as in B4 at offset 8 * index) and OR its attribute word into
   `attributes`.
2. word0 = address[0] | address[1] << 8 | address[2] << 16 | address[3] << 24;
   word1 = address[4] | address[5] << 8 | attributes << 16.
3. `wlc_bmac_copyto_objmem(wlc_hw, 8 * index, {word0, word1}, 8, 0x40000)`:
   `D11(0x160) = 0x40000 | (2 * index)`, read back, `D11(0x164) = word0`;
   `D11(0x160) = 0x40000 | (2 * index + 1)`, read back, `D11(0x164) = word1`.

`sub_061e23(wlc_hw, index, address out, attributes out)` reads an entry the
same way. `wlc_bmac_amt_dump` reads the entries 63 and 62 and discards them.

#### B6. sub_060f67 (.text+0x060f67, 103 bytes, name assigned: wlc_write_inits)

Inputs: `wlc_hw`, pointer to an initialisation list (format:
`../01-anatomy.md`: entries of 8 bytes: 16 bit register offset, 16 bit size,
32 bit value; end marker offset 0xffff). For every entry in order: size 2:
16 bit write of the low half of the value to `D11(offset)`; size 4: 32 bit
write; any other size: the entry is skipped. No reads, no delays.
`wlc_bmac_write_inits(wlc_hw, list)` (.text+0x060fce) is a wrapper.

#### B7. sub_067efd (.text+0x067efd, 909 bytes, name assigned: wlc_bmac_bmc_init)

Purpose: MAC revisions >= 40 have no fixed transmit FIFO memories; a buffer
memory controller hands out buffers to the FIFOs. This function configures
it. (Register names are not known; the aliases are mine.) Input: `wlc_hw`.
Result 0. All register accesses are 16 bit except the first. Steps for
revision 42:

1. cap = read `D11(0x15c)` (32 bit, MAC capabilities). total =
   `(cap >> 1) & 0xffc`; it is stored in two static 16 bit variables
   (.bss+0xb38 and .data+0x170).
2. (Revisions 43 and 45..47 only: six more writes and a base of 0x7a instead
   of 0x2a; not described.) base = 0x2a (42).
3. `D11(0x542) = total`. `D11(0x540) = 5`.
   `SPINWAIT((D11(0x540) & 1) != 0, 200)`; one more read of `D11(0x540)`.
4. For f in the order 7, 0, 1, 2, 3, 4, 5 (list at .rodata+0x284740; 7 is
   the template memory, 0..5 the transmit FIFOs), six writes each:

   | Register | f = 7 | f = 0..5 |
   |---|---|---|
   | `D11(0x54a)` (maximum of buffers) | base = 0x002a | (total - base) & 0xffff |
   | `D11(0x54c)` (minimum of buffers) | base = 0x002a | 0x0020 (table at .rodata+0x2846e0, 16 bit entries, index 6 * (revision - 40) + f; all six are 0x20 for revision 42) |
   | `D11(0x520)` | base = 0x002a | 0x000b (revisions 41, 44..47: 6) |
   | `D11(0x54e)` | 0x262a | 0x1216 |
   | `D11(0x550)` | 0x740c | 0x740c |
   | `D11(0x548)` (command) | 0x0017 | 0x0010 \| f |

   The value of `D11(0x54e)` is `((n - 4) << 8) | n` with n = base for f = 7
   and n = 2 * 0x0b = 0x16 for the FIFOs.
5. For i = 0 .. 41 (one per buffer of the template memory):
   1. `D11(0x534) = i`.
   2. e = min(i + 2, 41); `D11(0x536) = e`.
   3. `D11(0x532) = e - i + 1` (3 for i <= 39, 2 for i = 40, 1 for i = 41).
   4. `D11(0x530) = 0x8007 | (i << 4)`.
   5. `SPINWAIT(D11(0x530) != 0, 200)` (the whole 16 bit value is compared
      with 0); one more read of `D11(0x530)`.

No error is reported when a wait runs out.

Trace (seq 24395..24697): as above with cap = 0 (the model returns 0 for
`D11(0x15c)`; the value of the real card is not known), so total = 0 and
`D11(0x54a)` = 0xffd6 for the FIFOs. **With the real capability value these
numbers differ.**

#### B8. sub_05fdca (.text+0x05fdca, 168 bytes, name assigned: wlc_bmac_synthpu_dly) and sub_0627c9 (.text+0x0627c9, 38 bytes, name assigned: wlc_bmac_upd_synthpu)

`sub_05fdca(wlc_hw)` returns the time the radio's synthesizer needs to power
up, in microseconds, by PHY type of the current band. AC-PHY: 512 (0x200);
1200 for chip 0x4350. (Other PHY types: 50, 200, 500, 800, 1536, 2288, 3580.)
If the radio of the band is a 2050 of revision 8 the result is at least 2400
(not this card). No access.

`sub_0627c9(wlc_hw)`: `SHM(0x94) = sub_05fdca(wlc_hw)`.

#### B9. wlc_bmac_switch_macfreq (.text+0x064cbf, 758 bytes, name original)

Purpose: tell the MAC the frequency of its clock (it derives the TSF
microseconds from it). Inputs: `wlc_hw`, `spurmode` (u8). For the chips
0x4360, 0x4352, 0xa9c4, 0xaa06 the argument `spurmode` is **not used**:

1. vco = `si_pmu_get_bb_vcofreq(sih, osh, 40)` (`pmu.md`: reads
   `PMU_PLLCTL[2]` and, in fractional mode, `PMU_PLLCTL[3]`; result in units
   of 100 Hz).
2. q = floor(0x000003a980000000 / vco) (64 bit numerator = 937.5 * 2^32;
   `bcm_uint64_divide`; 32 bit result). This is 2^26 divided by the clock in
   MHz, the clock being vco / 6.
3. `D11(0x62e) = q & 0xffff`, `D11(0x630) = q >> 16` (16 bit each).

(Chip 0x4350 does the same if the bus is not 0; chip 0x4335 and the N-PHY
chips use tables of constants selected by `spurmode`; chips that are in no
list and have no LCN-PHY: nothing.)

Trace (seq 24779..24788): `PMU_PLLCTL[2]` = 0xc31, `PMU_PLLCTL[3]` = 0x100e,
vco = 9600098, q = 0x66662: `D11(0x62e)` = 0x6662, `D11(0x630)` = 0x0006.
With an integer PLL setting (vco = 9600000) q = 0x66666.

`wlapi_switch_macfreq(shim, spurmode)` calls it; the AC-PHY code calls that
with the spur mode of the channel for other chips (see
`acphy-chanspec.md`).

#### B10. sub_06656c (.text+0x06656c, 1490 bytes, name by the guide: wlc_bmac_bsinit)

Purpose: the part of the initialisation that depends on the band. Inputs:
`wlc_hw`, `chanspec` (u16), `bandswitch` (u8: 0 when called by
`wlc_bmac_init`, 1 when called by the band switch of
`wlc_bmac_set_chanspec`). `band` is the current band state `wlc_hw+0xe8`.
Steps:

1. (Chips 0xa9a7 and 0x4331 only: `si_seci_upd`.)
2. One 16 bit read of `D11(0x3e0)` (PHY version; the value is not used).
3. `sub_062766(wlc_hw, band + 8)` (name assigned: `wlc_write_mhf`): the five
   host flag copies of the band are written to the shared memory, in the
   order `SHM(0x5e)`, `SHM(0x60)`, `SHM(0x62)`, `SHM(0x78)`, `SHM(0xd4)`
   (always five writes).
4. Band specific initialisation list: revision 42 with PHY type 11:
   `sub_060f67(wlc_hw, d11ac1bsinitvals42)` (73 entries; the same list for
   both bands). (43: `d11ac3bsinitvals43`; 41, 44..47: `d11ac2bsinitvals41`;
   40: `d11ac0bsinitvals40`.)
5. If `bandswitch` is 0 (or the MAC revision is below 40):
   `wlc_phy_init(pi, chanspec)` (`acphy-init.md` section 1).
6. `sub_062684(wlc_hw)` (name assigned: `wlc_ucode_txant_set`): does nothing
   for MAC revisions >= 40. (Older: the transmit antenna bits 6..9 of
   `SHM(0x188)` and `SHM(0x22)` are replaced by `wlc_hw+0x100`.)
7. `wlc_bmac_set_cwmin(wlc_hw, band+0x14)`, `wlc_bmac_set_cwmax(wlc_hw,
   band+0x16)` (B11) with the values stored in the band state (15 and 1023
   in the traces).
8. `sub_062716(wlc_hw, shortslot)` (name assigned:
   `wlc_bmac_update_slot_timing`) with shortslot = 1 if the band is 5 GHz
   (first word of the band state, the band type, = 1), else the byte
   `wlc_hw+0x102`:
   * shortslot: `D11(0x684) = 0x0207` (16 bit), `SHM(0x10) = 9`;
   * else: `D11(0x684) = 0x0212`, `SHM(0x10) = 20`.
9. `SHM(0x52)` = PHY type (11), `SHM(0x50)` = PHY revision.
10. `sub_062403(wlc_hw)` (B12).
11. `sub_0627c9(wlc_hw)`: `SHM(0x94) = 512` (B8).
12. `wlc_bmac_mhf(wlc_hw, 4, 0x0008, band is 5 GHz ? 0x0008 : 0, 3)`.
13. `sub_06106b(wlc_hw)` (section E; nothing unless the coexistence state
    has a GPIO mask).
14. (Chips 0xa9a7/0x4331 in 5 GHz with package 9 or 11: a host flag is
    cleared and step 3 repeated.)
15. `wlc_bmac_set_extlna_pwrsave_shmem(wlc_hw)` (.text+0x0627ef, 83 bytes,
    name original): `SHM(0x64) = 0x0480`. (0x04c0 for chip 0x4331 under
    board conditions.)

Trace (seq 24789..51443): 2, 3 (values 0x0100, 0, 0x0040, 0, 0x0081), 4, 5
(`wlc_phy_init`, 24920..51321), 7 (scratch 3 = 0x000f, scratch 4 = 0x03ff),
8 (`D11(0x684)` = 0x0212, `SHM(0x10)` = 0x14), 9 (`SHM(0x52)` = 0x0b,
`SHM(0x50)` = 1), 10, 11 (`SHM(0x94)` = 0x200), 15 (`SHM(0x64)` = 0x480).
Step 12 made no access (flag unchanged).

#### B11. wlc_bmac_set_cwmin (.text+0x063138, 50 bytes), wlc_bmac_set_cwmax (.text+0x063106, 50 bytes), names original

`wlc_bmac_set_cwmin(wlc_hw, value)`: `band+0x14` = value (16 bit);
`wlc_bmac_copyto_objmem(wlc_hw, 0x0c, &value, 2, 0x20000)`: scratch register
3 = value (`D11(0x160)` = 0x00020003, read back, 16 bit write of
`D11(0x164)`).

`wlc_bmac_set_cwmax(wlc_hw, value)`: `band+0x16` = value; scratch register 4
(`D11(0x160)` = 0x00020004) = value.

#### B12. sub_062403 (.text+0x062403, 594 bytes, name assigned: wlc_upd_ofdm_pctl1_table)

Purpose: update the PHY control word of the eight OFDM rates in the rate
table of the microcode. Acts for PHY types 4, 6, 7, 8, 10 and 11. For the
AC-PHY on revision 42 the words are read and written back **unchanged**.
Steps:

1. For the rates 6, 9, 12, 18, 24, 36, 48, 54 Mbit/s in this order, with the
   rate code c of the PLCP header (11, 15, 10, 14, 9, 13, 8, 12):
   1. t = read `SHM(0x1c0 + 2*c)` (directory of the rate table: offset of
      the rate's entry in words).
   2. a = ((2 * t) & 0xffff) + 0x12; v = read `SHM(a)`.
   3. (Revision 31 with N-PHY, revision 29 with HT-PHY, and the N-PHY and
      LCN-PHY in general modify v; not the AC-PHY.)
   4. `SHM(a) = v`.
2. If the coexistence mode (first word of `wlc_hw+0xb0`) is not 0: v = read
   `SHM(0x22)`; `SHM(0x22) = (v & 0xfc3f) | x` with x = 0x0080 if bit 23 of
   `boardflags2` (`wlc_hw+0x90`) is set, else 0x0040.

The directory addresses are, in the order of step 1: 0x1d6, 0x1de, 0x1d4,
0x1dc, 0x1d2, 0x1da, 0x1d0, 0x1d8. In the traces all directory entries read
0 (model, see the note of B1), so all eight read/write pairs hit `SHM(0x12)`.

`wlc_bmac_txbw_update(wlc_hw)` (.text+0x062655, 20 bytes, name original):
calls `sub_062403` if `clk` is not 0. `wlc_bmac_band_stf_ss_set(wlc_hw,
mode)` (.text+0x062669, 27 bytes, name original): stores `mode` in the byte
`wlc_hw+0x1a0` (used by the N-PHY branch of step 1.3 only) and calls
`sub_062403` if `clk` is not 0.

<!-- APPEND-B -->

### C. Up, down, reset, clocks

`pub` is the public state `wlc+0x00` (layout: `src/wl/sys/wlc_pub.h`):
`pub+0x34` `up`, `pub+0x35` `hw_off`, `pub+0x40` `hw_up`, `pub+0x84`
`radio_disabled` (bit mask, bit 1 = disabled by the hardware switch).

#### C1. How wlc.c calls the functions of this area

`wlc_up(wlc)` (.text+0x03aac3, name original; only the calls that matter
here, in order):

1. If `pub+0x35` (`hw_off`) is set or `wlc_hw_deviceremoved(wlc_hw)`: return
   -9.
2. If `pub+0x40` (`hw_up`) is 0: `wlc_led_init`, `wlc_bmac_hw_up(wlc_hw)` (C2),
   `hw_up` = 1. (So C2 runs at the first up after attach only.)
3. `wlc_bmac_4331_epa_init` (nothing for the 4360: chips 0xa9a7/0x4331 only).
4. `wlc_bmac_mhf(wlc_hw, 4, 0x0080, x, 3)` with x = 0x0080 if the byte
   `+0x59` of the structure `wlc+0x550` is not 0, else 0.
5. If `radio_disabled` is 0: `wlc_bmac_up_prep(wlc_hw)` (C3); if it returns
   -9 (radio switched off by hardware): bit 1 of `radio_disabled` is set.
6. If `radio_disabled` is still 0:
   1. `wlc_bmac_set_ctrl_ePA(wlc_hw)` (nothing for the 4360).
   2. If the byte `wlc_hw+0x1bc` is 0xff (-1, the default):
      `wlc_bmac_set_ctrl_bt_shd0(wlc_hw, 1)` (C11), else
      `wlc_bmac_set_btswitch(wlc_hw, that byte)`.
   3. `wlc_bmac_mhf(wlc_hw, 0, 0x0100, WME enabled (pub+0x54) ? 0x0100 : 0, 3)`.
   4. Bus PCI and byte `wlc+0x60` not 0: `wlc_bmac_mhf(wlc_hw, 1, 0x0008,
      0x0008, 3)`.
   5. `wlc_bmac_seci_upd(wlc_hw)` (nothing unless the board has the serial
      coexistence interface, see B1 step 8).
   6. `wl_init(wl)` of the glue: `wl_reset(wl)` (= `wl_intrsoff`,
      `wlc_reset(wlc)`, `wl_intrsrestore`), then `wlc_init(wlc)`
      (`src/wl/sys/wl_linux.c`). The glue of the emulator calls `wlc_reset`
      and `wlc_init` only, so the two interrupt mask accesses around
      `wlc_reset` are missing in the traces.
   7. `pub+0x34` (`up`) = 1; ...; `wlc_enable_probe_req`,
      `wlc_rfaware_lifetime_set` (each may suspend and enable the MAC, A6/A7);
      `wlc_bmac_up_finish(wlc_hw)` (C4); watchdog timer armed (1 s); LEDs.
7. Else (radio disabled): `sub_026c55` (radio monitor), nothing of this area.

`wlc_reset(wlc)` (.text+0x03fca4): statistics; if associated a delay of
4000; `wlc_bmac_reset(wlc_hw)` (C8); for MAC revisions > 40 and only if a
flag of the default BSS configuration is set:
`wlc_bmac_core_phypll_ctl(wlc_hw, 0)` (A16: nothing); A-MPDU reset.

`wlc_init(wlc)` (.text+0x03c46f): chooses the chanspec (default or home
channel), then
`wlc_bmac_init(wlc_hw, chanspec, 0)` (B1); reads the rate table directories
`SHM(0x1c0..0x1de)` and `SHM(0x200..0x21e)` (16 words each, kept at
`wlc+0x454` and `wlc+0x474`), `wlc_bmac_btc_mode_get`, reads `SHM(0x56)`
(kept doubled in `wlc+0x3c0`), `SHM(0xb2)` (only if `wlc+0x230` is
negative), `wlc_bcn_li_upd`, `SHM(0x4c)`, keys (`wlc_key_hw_init_all`),
`wlc_set_mac`, `wlc_set_bssid` (section E), rate sets, `sub_03649a`
(`wlc_set_phy_chanspec`, `acphy-chanspec.md`), `SHM(0x74)`, `SHM(0x82)`, ...,
reads `SHM(0x00)`, `SHM(0x02)` (version of the microcode) if not known yet,
**`wlc_bmac_enable_mac(wlc_hw)`** (A7: this starts the MAC; the counter was
set to 1 by `wlc_bmac_init`), `sub_03a14c` (MAC control bits for the
operating mode), `wlc_rfaware_lifetime_set`, `D11(0x3dc) = 10000000` (32
bit), reads `SHM(0x300..0x306)`.

`wlc_down(wlc)` (.text+0x036bae): `wlc_bmac_down_prep(wlc_hw)` (C5); aborts
scan and association; the down functions of the modules; timers deleted;
`pub+0x34` = 0; `wlc_phy_mute_upd(pi, 0, 0xffffffff)`; LEDs; queues flushed;
`wlc_bmac_down_finish(wlc_hw)` (C6).

#### C2. wlc_bmac_hw_up (.text+0x065656, 794 bytes, name original)

Purpose: one time preparation of the chip after power up. Input: `wlc_hw`.
Nothing is done if `pub+0x40` (`hw_up`) is already set. Steps (bus PCI, chip
0x4360):

1. If `wlc+0x750` (offload state) exists: `si_survive_perst_war(sih, 1, 0,
   0)` (specification `chip`).
2. `si_ldo_war(sih, chip id)` (bus PCI).
3. MAC revision >= 40 on PCI: `si_pmu_res_init(sih, osh)` (`pmu.md`).
4. `wlc_bmac_xtal(wlc_hw, 1)` (C9), `si_clkctl_init(sih)`,
   `wlc_bmac_clkctl_clk(wlc_hw, 0)`. (At this time `clk` is 0: the last has
   no access, it only sets `forcefastclk` = 1.)
5. `si_pcie_ltr_war(sih)`.
6. `sub_06333d(wlc_hw)` (C10).
7. Chips 0xa9c4, 0x4360, 0x4352: `wlc_bmac_4360_pcie2_war(wlc_hw, v)` with v
   = the 32 bit word `wlc_hw+0x1c0` (C12: acts for chip revisions < 3 only).
8. `si_pci_fixcfg(sih)`.
9. LEDs/GPIO, only if `sbclk` (`wlc_hw+0x187`) is set: with g = the 32 bit
   word `wlc_hw+0x180` (GPIO mask of the LEDs; 7 in the traces) and the LED
   table `wlc_hw+0x198`: `si_gpiocontrol(sih, g, 0, 0)`, `si_gpioled(sih, g,
   g)`; a = OR of `1 << (pin & 31)` over those of the 32 LED entries (24
   bytes each from table+8: pin in the first word) whose byte +4 is 0
   (presumably "active high" (unverified)); if bit 11 of `boardflags2` is
   clear: `si_gpioout(sih, g,
   a & g, 0)`, `si_gpioouten(sih, g, g, 0)`; else: `si_gpioout(sih, g,
   ~(a & g) & g, 0)`, `si_gpioouten(sih, g, 0, 0)` and for ChipCommon
   revisions >= 20 `si_gpiopull(sih, 1, g, 0)`, `si_gpiopull(sih, 0, g, 0)`.
   (Belongs to the LED code; listed for the order.)
10. `wlc_phy_por_inform(pi)`.
11. `ucode_loaded` (`wlc_hw+0xae`) = 0; `pub+0x40` = 1.
12. (Chip 0x4313 and 0xa8dc work-arounds: not here.)

#### C3. wlc_bmac_up_prep (.text+0x06646a, 258 bytes, name original)

Input: `wlc_hw`. Result: 0, or -9 if the radio is disabled by the hardware
switch. Steps:

1. (Chip 0x4350 rev 0: a chip control bit.)
2. `wlc_bmac_xtal(wlc_hw, 1)`, `si_clkctl_init(sih)`,
   `wlc_bmac_clkctl_clk(wlc_hw, 0)`.
3. Bus PCI: `si_pci_setup(sih, 1 << n)` with n = the core index of the MAC
   (first word of the structure `wlc+0x38`).
4. `wlc_bmac_radio_read_hwdisabled(wlc_hw)` (C13).
5. If the radio is enabled: `si_pci_up(sih)` (bus PCI),
   `wlc_bmac_corereset(wlc_hw, 0xffffffff)` (A14); return 0.
6. Else: `si_pci_down(sih)`, `wlc_bmac_xtal(wlc_hw, 0)`; return -9.

#### C4. wlc_bmac_up_finish (.text+0x066409, 97 bytes, name original)

1. `up` (`wlc_hw+0x10c`) = 1.
2. `wlc_phy_hw_state_upd(pi, 1)`.
3. `wlc_bmac_clkctl_clk(wlc_hw, 2)`: back to dynamic clock (read
   `D11(0x1e0)`, write with bit 1 cleared).
4. `wl_intrson(wl)`: `D11(0x12c)` = the interrupt mask (specification
   `bmac-data`; 0xb0e7a860 in the traces).
5. `wlc_bmac_ifsctl_edcrs_set(wlc_hw, x)` (E) with x = 1 if the PHY type of
   the upper layer's band (`wlc+0x40`, 16 bit at `+0x08`) is 7; for the
   AC-PHY the argument is 0 and has no effect.

Result 0.

#### C5. wlc_bmac_down_prep (.text+0x066108, 144 bytes, name original)

Nothing (result 0) if `up` is 0. Else:

1. `wlc_hw_deviceremoved(wlc_hw)` (C14). If the card is there:
   `wl_intrsoff(wl)`, `wlc_bmac_clkctl_clk(wlc_hw, 0)`, and if `noreset` is 0
   `sub_06333d(wlc_hw)` (C10). If it is gone: `wlc_hw+0x98` = 0.
2. (Chip 0x4331: `SHM(0x64) = 0x480`.)
3. Result: `wlc_phy_down(pi)`.

#### C6. wlc_bmac_down_finish (.text+0x066338, 209 bytes, name original)

Nothing (result 0) if `up` is 0. Else:

1. `up` = 0; `wlc_phy_hw_state_upd(pi, 0)`.
2. If the card is gone (`wlc_hw_deviceremoved`): `sbclk` = 0, `clk` = 0,
   `wlc_phy_hw_clk_state_upd(pi, 0)`, `sub_05fefa(wlc_hw)` (C8); result 0.
3. If the core is up (`si_iscoreup`):
   1. Read `D11(0x120)`; if bit 0 is set:
      `wlc_bmac_suspend_mac_and_wait(wlc_hw)`.
   2. result = `wl_reset(wl)` of the glue = `wlc_reset(wlc)` (C1, C8: core
      reset).
   3. `wlc_coredisable(wlc_hw)` (C7).
4. If `noreset` is 0: `wlc_bmac_hw_down(wlc_hw)` (C15).

#### C7. wlc_coredisable (.text+0x06378d, 155 bytes, name original)

Nothing if the card is gone or `noreset` is set. Else:

1. `wlc_phy_switch_radio(pi, 0)`, `wlc_phy_anacore(pi, 0)`
   (`acphy-radio.md`).
2. `wlc_bmac_core_phypll_ctl(wlc_hw, 0)` (A16: nothing).
3. (If the 16 bit word `wlc_hw+0xac` is not 0: `si_gpiocontrol(sih,
   0xffffffff, 0, 0)`. 0 in the traces.)
4. `clk` = 0; `si_core_disable(sih, 0)` (traces: R `WRAP(0x800)`,
   R `WRAP(0x804)`, W `WRAP(0x800)` = 1, R `WRAP(0x800)`, delay 1,
   W `WRAP(0x408)` = 0, R `WRAP(0x408)`, delay 10; specification `chip`).
5. `wlc_phy_hw_clk_state_upd(pi, 0)`.

#### C8. wlc_bmac_reset (.text+0x066b3e, 79 bytes, name original), sub_05fefa (.text+0x05fefa, 122 bytes, name assigned: wlc_flushqueues)

`wlc_bmac_reset(wlc_hw)`: a counter is incremented (32 bit at `+0xb4` of
the counter block `pub+0xa0`); unless the card is gone:
`wlc_bmac_corereset(wlc_hw, 0xffffffff)`; `sub_05fefa(wlc_hw)`;
`wlc_reset_bmac_done(wlc)` (empty).

`sub_05fefa(wlc_hw)`: `wlc+0x68` = 0; for FIFO i = 0..5 with a DMA handle:
transmit reclaim of everything (DMA function slot 0x60 with argument 1) and
the 16 bit counter `+0x38 + 2*i` of the structure `wlc+0x38` = 0; then
receive reclaim of FIFO 0 (slot 0xe0). In the traces this reads
`D11(0x22c)` once.

#### C9. wlc_bmac_xtal (.text+0x0632d4, 105 bytes), wlc_bmac_pllreq (.text+0x063524, 85 bytes), wlc_bmac_set_clk (.text+0x06603b, 68 bytes), names original

`wlc_bmac_xtal(wlc_hw, want)`:

1. If `want` is 0 and the PLL request mask `wlc_hw+0x160` is not 0: return.
2. If `sih` exists: `si_clkctl_xtal(sih, 3, want)` (`pmu.md`: on a PCIe card
   no access, result -1).
3. `sbclk` (`wlc_hw+0x187`) = `want`. If `want` is 0: `clk` = 0 and, if the
   current band has a PHY, `wlc_phy_hw_clk_state_upd(pi, 0)`.

`wlc_bmac_pllreq(wlc_hw, set, bit)`: keeps a mask of requesters in
`wlc_hw+0x160`. `set`: if the bit is already in the mask return; add it; if
the new mask contains bit 2 (value 4) and `sbclk` is 0:
`wlc_bmac_xtal(wlc_hw, 1)`. Clear: if the bit is not in the mask return;
remove it; if the new mask **still** contains bit 2 and `sbclk` is set:
`wlc_bmac_xtal(wlc_hw, 0)` (which returns at once because the mask is not
0, so clearing a request never switches anything off). The open brcmsmac
driver has the same logic. No caller of `wlc_bmac_pllreq` was seen in the
traces.

`wlc_bmac_set_clk(wlc_hw, on)`: on: `wlc_bmac_xtal(wlc_hw, 1)`,
`wlc_bmac_corereset(wlc_hw, 0xffffffff)`. Off: if `clk` is set
`wlc_coredisable(wlc_hw)`; `wlc_bmac_xtal(wlc_hw, 0)`.

#### C10. sub_06333d (.text+0x06333d, 263 bytes, name assigned: wlc_bmac_btc_gpio_disable)

Releases the GPIO lines of the Bluetooth coexistence. With m = the GPIO mask
(32 bit `+0x0c` of the coexistence state) and o = `+0x10`:

1. If `sbclk` is 0: `wlc_bmac_xtal(wlc_hw, 1)` first.
2. If m is 0: return (the case in all traces: no access). (Note: the crystal
   is then **not** switched off again, the request of step 1 stays.)
3. `si_gpiocontrol(sih, m, 0, 0)`, `si_gpioouten(sih, m, 0, 0)`, (chip 0x4313:
   pull), `si_gpioout(sih, o, 0, 0)`; if `clk` is set: `D11(0x49e) &= ~o`
   (16 bit read and write); bus PCI and bit 8 of m: `si_btcgpiowar(sih)`.
4. If step 1 switched the crystal on: `wlc_bmac_xtal(wlc_hw, 0)`.

Its counterpart `sub_06106b` (enable) is in section E.

#### C11. wlc_bmac_set_ctrl_bt_shd0 (.text+0x066198, 167 bytes), wlc_bmac_set_ctrl_SROM (.text+0x064c6b, 84 bytes), names original

`wlc_bmac_set_ctrl_bt_shd0(wlc_hw, on)`, bus PCI: chips 0xa9c4, 0x4360,
0xaa06, 0x4352: `si_corereg(sih, 0, 0x28, 0x01000008, 0)`, that is
`CC(0x28) = CC(0x28) & ~0x01000008` (read, write, read back; ChipCommon's
chip control register), **whatever `on` is**. (Chips 0xa9a7/0x4331 on some
boards: `si_chipcontrl_btshd0_4331(sih, on)`.) Result 0.

`wlc_bmac_set_ctrl_SROM(wlc_hw)`, bus PCI: chips 0xa9c4, 0x4360, 0x4352 with
chip revision < 3: `si_chipcontrl_srom4360(sih, 1)`; nothing for revision 3.

#### C12. wlc_bmac_4360_pcie2_war (.text+0x065338, 798 bytes, name original)

Inputs: `wlc_hw`, a frequency value v (32 bit word `wlc_hw+0x1c0`). Acts
only for chips 0xa9c4, 0x4360, 0x4352 with **chip revision < 3** on bus PCI,
once per load of the driver (static flag `do_4360_pcie2_war`), and only if
`wl_osl_pcie_rc(wl, 0, 0)` of the glue does not return 1. For chip revision
3 it returns at once; verified in the emulator (no access). For revisions
< 3 (not executed, read from the code only, (unverified)):

1. `PCIE(0x120) = 0xbc`, c = read `PCIE(0x124)` (indirect configuration
   register 0xbc). If bits 16..19 of c are 2: nothing more.
2. `si_pcie_configspace_cache(sih)`.
3. `PMU_PLLCTL[10]` = `((v / 20) << 7) | 2 | (v % 20 != 0 ? 0x30 : 0)`; if
   v % 20 != 0: `PMU_PLLCTL[11]` = `((v % 20) << 24) / 20`;
   `si_pmu_pllupd(sih)`.
4. `si_watchdog(sih, 2)` (chip reset in 2 ticks); delay 2000;
   `wl_osl_pcie_rc(wl, 1, 0)`; delay 50000; `si_pcie_configspace_restore(sih)`.
5. Indirect configuration register 0x4dc: low 4 bits set to 2 (address
   written to `PCIE(0x120)` before every access of `PCIE(0x124)`); register
   0x1800: low 4 bits set to 2, then to 0; delay 1000; register 0xbc read.

#### C13. wlc_bmac_radio_read_hwdisabled (.text+0x065154, 292 bytes, name original)

Result: 1 if the hardware switch disables the radio, else 0. Steps:

1. If `sbclk` is 0: `wlc_bmac_xtal(wlc_hw, 1)` (remember that).
2. If `clk` is 0 (remember that): `si_core_reset(sih, 0x4, 0)` (MAC revisions
   >= 18), then the reset of the MAC control state as in A14 step 7
   (`D11(0x120) = 0x04000400`). `clk` itself is not changed.
3. v = read `D11(0x158)` (32 bit); result = bit 16 of v.
4. If step 2 was done: `si_core_disable(sih, 0)`.
5. If step 1 was done: `wlc_bmac_xtal(wlc_hw, 0)`.

Trace (seq 1076..1095, in `wlc_bmac_up_prep`): core reset, W `D11(0x120)` =
0x04000400, R `D11(0x158)` = 0, core disable.

#### C14. wlc_hw_deviceremoved (wlc_hw.c, .text+0x079847, 61 bytes, name original)

If `clk` is 0: `si_deviceremoved(sih)` (reads `PCICFG(0)`). Else: v = read
`D11(0x120)` (32 bit); removed if `(v & 0x404) != 0x400` (a removed card
reads 0xffffffff; a working MAC has IHR_EN set and PSM_JMP_0 clear).

#### C15. wlc_bmac_hw_down (.text+0x06623f, 249 bytes, name original)

Bus PCI:

1. `wlc_bmac_set_ctrl_SROM(wlc_hw)` (C11).
2. (`si_seci_down(sih)` under the conditions of `wlc_bmac_seci_upd`.)
3. `wlc_bmac_set_ctrl_bt_shd0(wlc_hw, 0)` (C11).
4. If the offload state `wlc+0x750` exists: `si_survive_perst_war(sih, 0,
   0x204, 0)`, `si_pmu_res_req_timer_clr(sih)`.
5. `si_pci_down(sih)`.

Then, any bus: chips 0x4352, 0x4360, 0xaa06: `si_pmu_rfldo(sih, 0)`;
`wlc_bmac_xtal(wlc_hw, 0)`. (Chip 0x4350 rev 0: chip control.)

#### C16. The complete down sequence in the trace

`re-out\analysis\bmac-init\life-down1.txt` (169 accesses): R `D11(0x120)`;
`D11(0x12c)` = 0 and read back; fast clock forced (`D11(0x1e0)`);
[`wlc_phy_down`, `wlc_scan_down` with a suspend/enable pair of the PHY];
LED GPIO; R `D11(0x120)`; `si_iscoreup`; R `D11(0x120)`; suspend (A6); core
reset with DMA resets (A14, A15); R `D11(0x22c)`; radio off
(`wlc_phy_switch_radio_acphy`), `D11(0x3e6)` = 0x00f4; core disable;
`CC(0x28)`; PCIe and PMU calls of `wlc_bmac_hw_down`; `PMU_REGCTL[0]` bit 1
set. State afterwards: `up` 0, `clk` 0, `sbclk` 0, `forcefastclk` 1,
`maccontrol` 0x04000400, `mac_suspend_depth` 1, `ucode_loaded` 1.

The second up (`life-up2.txt`) therefore differs from the first: no
`wlc_bmac_hw_up`, **no microcode download** (22,766 instead of 54,790
accesses); the start sequence B3 and everything else is repeated.

<!-- APPEND-C -->

<!-- APPEND-A -->

## Verification

(to be completed)

## Open questions

(to be completed)
