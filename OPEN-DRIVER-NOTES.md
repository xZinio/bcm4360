# Can the Broadcom core be replaced with open code?

Short answer: **not realistically, and not by me.** This note records the honest
state so the decision is made with eyes open, not the fact that the blob still
ships next to open glue.

## The boundary (measured from the open source, no disassembly)

`bcm4360.ko` = ~18,800 lines of open glue (`src/`, ISC) linked against one
7,350,128-byte proprietary object, `wlc_hybrid.o_shipped`. The seam between the
two is fully visible in the open headers:

- **The blob exports the entire driver core** that the glue calls into:
  `wlc_attach/detach`, `wlc_init/reset/up/down`, `wlc_ioctl`, `wlc_sendpkt`,
  `wlc_isr`/`wlc_dpc` (interrupt + packet path), plus the `si_*`
  (SiliconBackplane) and `bcm_*` utility layers. `siutils.h` alone declares
  ~196 functions, `bcmutils.h` ~99, `wlc_pub.h` ~34. None of their bodies are
  in the repo.
- **The glue only provides the OS plumbing** the blob calls back for
  (`wl_export.h`: timers, interrupt masking, packet alloc, `wl_sendup`, …).

So the open side is the easy, already-solved part. Everything that actually
drives the radio — MAC control, rate selection, and the **AC-PHY** (the 802.11ac
physical-layer code for this exact chip) — lives in the blob.

## Why replacing it is not on the table here

1. **The AC-PHY is the unsolved problem.** The open `b43` driver has chipped
   away at Broadcom PHYs for over a decade; its AC-PHY support (BCM4360/4352) is
   still marked **BROKEN** in the mainline kernel and crashes on load. A working
   open AC-PHY does not exist anywhere. This is a multi-year, many-person
   research effort with real hardware, not a task with an end date.

2. **Open code alone still would not be self-contained.** Like almost every
   Wi-Fi chip, the BCM4360 needs on-chip firmware and per-device radio
   calibration/NVRAM data to operate. Even a complete open driver would still
   depend on non-source data for the radio, so "nothing outside the repo" is not
   attainable for this hardware even in principle.

3. **The blob's license forbids reverse engineering it.** Broadcom's license
   (`lib/LICENSE.txt`, §2.6) explicitly prohibits reverse engineering,
   decompiling or disassembling the software. Clean-room reimplementation for
   interoperability is a legitimate activity in its own right (and how the open
   Broadcom work was done), but it is done from observed behaviour and public
   documentation by people who have never disassembled the blob — not by
   decompiling this object.

## What a real open effort would build on (prior art)

- **b43 + b43-tools + the specs at bcm-v4.sipsolutions.net** — the open
  reverse-engineering project for Broadcom PHYs (LP/N/HT-PHY working, AC-PHY not).
- **brcmsmac** — Broadcom's *own* open SoftMAC driver, upstream, but only for
  older N-PHY chips; it does not cover the 4360.
- **OpenFWWF** — open firmware for older Broadcom MACs; predates this generation.
- **brcmfmac** — open driver for the FullMAC chips, but those run a large
  proprietary firmware on the card; it does not support the 4360 SoftMAC part.

None of these reach the 4360's AC-PHY. That gap is exactly why Apple/MacBook
BCM4360 machines still rely on the `wl` blob on every OS.

## The honest options

1. **Keep `bcm4360` as built.** Open glue, modern-kernel fixes, working Wi-Fi;
   the RF core stays Broadcom's binary, downloaded at install and kept out of
   the repo. This is what every distro does for this chip.
2. **A genuinely fully-open setup means different hardware.** A USB adapter with
   an in-kernel open driver: Atheros AR9271 (open driver *and* open firmware,
   2.4 GHz only), or MediaTek MT7921/MT7922 (Wi-Fi 6, open driver, proprietary
   firmware on the stick). Either gives a kernel with no proprietary code linked
   into it — the realistic way to get what "fully open" is really after.
3. **Contribute to b43's AC-PHY** if the interest is the research itself. That
   is upstream, lawful, and where the actual open-source gap is — but it is a
   long-horizon project, not something that finishes on a deadline.

There is no path that turns *this* card into a working, in-repo-only open driver.
