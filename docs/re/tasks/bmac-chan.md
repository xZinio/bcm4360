# Task `bmac-chan`: the channel and the band of the MAC core, the transmitter mute

Specification: [`../spec/acphy-chanspec.md`](../spec/acphy-chanspec.md),
sections **1** (`wlc_bmac_set_chanspec` with its band switch), **2**
(`wlc_setxband`) and **3** (only the MAC's part: `wlc_bmac_bw_set` and
`wlc_bmac_bw_reset`; `wlc_phy_bw_state_set` and `wlc_phy_clk_bwbits` are the
PHY's and are services), and [`../spec/bmac-init.md`](../spec/bmac-init.md)
section **E4** (`wlc_bmac_mute`). The notation of both ("cflags", `mod`) and
the "Data" tables of `bmac-init.md` apply. Register access:
`open/hw/access.c`.

Interface: `open/include/bcm4360/mac.h` (given: the prototypes and fields
under "the channel and the band", and the prototypes of
`bcm4360_bmac_mctrl_write` and `bcm4360_bmac_bsinit`) and
`open/include/bcm4360/mac_env.h` (the services, the block "for the channel
and the band"). Write **`open/mac/bmac_chan.c`**.

## Why

The seventh MAC increment. `wlc_bmac_set_chanspec` runs at every channel
change: it forces the fast clock, switches the band when the channel is in
the other band (the band initialisation of B10 again, without the PHY
initialisation), hands the channel to the PHY and sets the mute. While it is
Broadcom's, Broadcom's own copy of the band initialisation (B10 with B6,
B8, B12 and the E10 helpers) keeps running from it, although
`open/mac/bmac_init.c` already implements all of it. With this task the
channel path becomes open code inside the real driver (hybrid build, switch
`open_mac`, adapter `hybrid/mac_chan.c`, already written).

## The functions

| Open function | Broadcom's | Section |
|---|---|---|
| `bcm4360_bmac_set_chanspec` | `wlc_bmac_set_chanspec`, with the inlined band switch (`wlc_bmac_setband`) as a `static` helper | acphy-chanspec 1 |
| `bcm4360_bmac_setxband` | `wlc_setxband` | acphy-chanspec 2 |
| `bcm4360_bmac_bw_set` | `wlc_bmac_bw_set` | acphy-chanspec 3 |
| `bcm4360_bmac_bw_reset` | `wlc_bmac_bw_reset` | acphy-chanspec 3 |
| `bcm4360_bmac_mute` | `wlc_bmac_mute` | bmac-init E4 |

Where the specification calls a MAC function that is already open, call the
open one: `sub_064887` -> `bcm4360_bmac_clkctl_clk`, `wlc_bmac_mctrl`,
`wlc_bmac_write_amt`, `sub_06656c` (B10) -> `bcm4360_bmac_bsinit`,
`sub_0605ad` (A1) -> `bcm4360_bmac_mctrl_write`, and your own
`bcm4360_bmac_setxband`, `bcm4360_bmac_bw_reset`, `bcm4360_bmac_mute`.

**The two changes allowed in other files** (as the previous task did for
A13): B10 is the `static` function `wlc_bmac_bsinit` of
`open/mac/bmac_init.c`, A1 the `static` function `wlc_mctrl_write` of
`open/mac/bmac_ctrl.c`. Make them the exported `bcm4360_bmac_bsinit` and
`bcm4360_bmac_mctrl_write` declared in `mac.h`: rename, drop `static`,
update their callers in those files. Nothing else in those files.

## Rules for this code

* Clean room: `../implementing.md`, binding.
* Everything outside the 802.11 core's registers goes through `mac_env.h`,
  called exactly where the specification calls the corresponding function:
  the PHY (`wlc_phy_chanspec_radio_set` - of the **new** band in section 1
  step 3.1, `bcm4360_bmac_env_band_phy_chanspec_radio_set`; of the current
  band in step 5 -, `wlc_phy_initcal_enable`, `wlc_phy_txpower_limit_set`,
  `wlc_phy_chanspec_set`, `wlc_phy_chanspec_get`, `wlc_phy_bw_state_set`,
  `wlc_phy_clk_bwbits`, `wlc_phy_mute_upd`), the backplane (`si_iscoreup`,
  `si_core_reset`, `cflags`), the glue (`wl_intrsoff`, `wl_intrsrestore`),
  the pending interrupt bits `wlc_hw+0x94`, the byte `wlc+0x64` of E4, and
  `wlc_bmac_tx_fifo_suspend` / `_resume` (they stay Broadcom's: the FIFO part
  of the DMA specification is not written yet).
* **The current band is the platform's.** Section 2 step 1 is the service
  `bcm4360_bmac_env_band_select(mac, unit)`; after it every field of the
  current band in `*mac` (`current_band`, `band_*`, `cwmin`, `cwmax`)
  describes the new band. "`pi` of the band that is current at that moment"
  is what the PHY services use.
* **Services can change the state**: after any service that can run MAC
  code (the PHY, the FIFO suspend/resume, the band selection) the platform
  has reloaded `*mac`; never keep a field in a local across one. Section 1
  step 1's "fast" is a value taken at entry by the specification itself: that
  one is meant to be a local; so is the kept result of `wl_intrsoff`.
* `limits` (section 1) is the PHY's handle of the channel's power limits:
  pass it on unchanged, never look into it.
* Write the fields the specification writes (`suspended_fifos`,
  `maccontrol`, `wake_override`, `mute_override` in section 1 step 3.2 and
  E4, and those of the functions you call). `nbands`, `band_unit`, `devid`,
  `up`, `clk`, `sbclk`, `noreset`, `forcefastclk`, `mac_rev`,
  `hw_etheraddr` are inputs (of these only `forcefastclk` and `clk` change,
  through the functions you call).
* The chanspec, the bandwidth and the band unit: section 1 step 3 gives the
  unit of a chanspec (1 if `chanspec & 0xc000` = 0xc000, else 0).
* Card and revision: MAC 42, AC-PHY (type 11), chip 0x4360 revision 3, PCI
  device 0x43a0, PCIe, two bands. Branches for other PHY types, MAC
  revisions (below 17, below 40) and devices: a one-line comment, no code -
  except the device list of section 1 step 6.1, which is tested as written.

## Tests

From `tools/re` (Docker, Linux tools; Docker Desktop is running; the adapter
`hybrid/mac_chan.c` is linked automatically as soon as `open/mac/bmac_chan.c`
exists):

* `python ab.py build` (no warnings).
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-bmacchan bcm4360-emu:latest python
  hybrid.py check` must report **the same**; likewise `check --off mac`,
  `check --on phy`, `check --on phy --off mac`, and `srom`. In the session
  `wlc_bmac_set_chanspec` runs 11 times (bring-up and nine channel changes
  in both bands and all bandwidths, one of them a band switch while up),
  `wlc_setxband` and `wlc_bmac_bw_set` 5 times each, `wlc_bmac_mute` 9
  times.
* `python ab.py all` must still pass (`AB_OUT=/repo/re-out/open-bmacchan` if
  you run it in Docker).

The core not up during a band switch (section 1 step 3.2), devices other
than the four of step 6.1, MAC revisions below 40 in E4 and a non-zero
`wlc+0x64` are not reached by the session: write them from the
specification.

Do not edit `hybrid/` or `tools/`, nor any file of `open/` except your own
and the two changes above. Questions into `docs/re/questions/bmac-chan.md`;
finish with a short report.
