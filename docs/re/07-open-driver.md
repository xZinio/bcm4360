# The last step: a driver without Broadcom's code

Plan, 2026-11-05 (after 1.1.0). It replaces `OPEN-DRIVER-NOTES.md`, which
judged an open AC-PHY out of reach; the open PHY has run on the card by
default since 1.1.0. [06-roadmap.md](06-roadmap.md) describes how we got
here.

## Where 1.1.0 stands

`bcm4360.ko` still links Broadcom's whole core, but open code takes over
its hardware layer at link time (`hybrid/`). Over a session of bring-up,
channel changes in both bands, watchdog, down and up, 37 % of the Broadcom
code that the core alone executes still runs (73 KB of 200 KB). The open
code does 99 % of all execution, counting repetitions (`hybrid.py share`).
What still runs of Broadcom's is almost all the upper 802.11 layer: the
driver core `wlc.c` and its modules (scan, association, rate selection,
aggregation, keys, power save, iovar handlers). The 73 KB come from 54 source
files (`tools/re/remaining.py`). The largest parts are the channel manager
(`wlc_channel.c`, 9 KB), the driver core (`wlc.c`, 20 KB in 93 functions)
and the setup of LEDs, stations, beamforming, aggregation and power control.
Small rests of the lower layers run too: backplane enumeration, OTP
reading and the PCIe bridge.

Two kinds of Broadcom material stay in any case:

- **Data**: the MAC microcode (`d11ucode42`), its initialisation lists, the
  PHY and radio tables, and the regulatory database. No open microcode exists
  for this MAC revision. This data is loaded, not executed by the host. That
  is the `b43` + `b43-fwcutter` model, and the usual meaning of an open Linux
  Wi-Fi driver.
- **Per-card data** (SROM/OTP calibration) is read from the card itself.

## The target

A Linux **mac80211** driver: no Broadcom code linked, and Broadcom's data cut
out of Broadcom's download at install time, the way `b43-fwcutter` does it
(`tools/re/fwcut.py` already does this), then loaded with `request_firmware`.

Why mac80211 rather than replacing `wlc.c` function by function:

- Linux already has the upper layer. mac80211 and cfg80211 handle joining,
  scanning, rate control (minstrel_ht), aggregation sessions, software
  crypto and power save. wpa_supplicant adds WPA3/SAE, PMF and 802.11r on
  top. Specifying 400 KB of Broadcom's upper layer would re-create code
  that Linux already has, and would keep its limits.
- It lifts the current limitations: WPA3, management frame protection and
  fast roaming come with mac80211, monitor mode becomes possible, and the
  tainted kernel goes away.
- The model is proven: `brcmsmac` (Broadcom's own ISC-licensed mac80211
  driver for older chips of the same family) and `b43` drive Broadcom MACs
  with Broadcom's microcode this way.

What the new driver must provide is the layer below mac80211: what
`brcmsmac`'s `main.c` does for its chips, for MAC revision 42 with the
AC-PHY.

## What exists and what is missing

| Piece | State |
|---|---|
| PCI, backplane, PMU and clocks, SROM/OTP, NVRAM | open (`open/chip`), on the card since 1.1.0 |
| MAC bring-up: core reset, microcode download, initialisation lists, band and channel, up and down, LEDs, coexistence | open (`open/mac`), on the card |
| PHY and radio: init, channels, calibrations, transmit power, interference mitigation, watchdog | open (`open/phy`), on the card |
| DMA engines (rings, descriptors) | open (`open/mac/dma.c`), on the card |
| Shared memory, address match table, templates, retry limits, contention window | open (`open/mac/bmac_mem.c`) |
| Regulatory power limits | open lookup (`open/lib/clm.c`) on Broadcom's database |
| Rests of the lower layers: backplane enumeration (`si_attach`), OTP reading, PCIe bridge setup | open (`open/core/chip_attach.c`, spec [chip-attach](spec/chip-attach.md)) |
| **Interrupts**: status bits, mask, order of handling | open (`open/core/frames.c`, spec [frames](spec/frames.md)) |
| **Receive**: FIFO 0, the receive header of MAC revision 42, mac80211 receive status | open (`open/core/rx.c`, frames.md) |
| **Transmit**: FIFO per access category, the transmit header of revision 42, transmit status | open (`open/core/tx.c`, frames.md); per-rate power field (coverage.md G1) in progress |
| **What the upper layer writes to the hardware** (attach, init, up, join, channel change) | open (`open/core/wlchw.c`, spec [wlc-hw](spec/wlc-hw.md), [channel-power](spec/channel-power.md)); join start (coverage.md G7) in progress |
| Hardware keys, power save | open (`open/core/keys.c`, `ps.c`, spec [ps-keys](spec/ps-keys.md)); spec-checked only; power save opt-in (`powersave=1`), `nohwcrypt=1` for software crypto |
| Aggregation (A-MPDU through the microcode), BlockAckReq | open (`open/core/ampdu.c`, spec [ampdu](spec/ampdu.md)); spec-checked only |
| LEDs, Bluetooth coexistence policy, radio switch / rfkill / MPC, chain changes | specified ([coverage](spec/coverage.md) G2-G5), being implemented |
| Linux side: PCI probe, interrupts, firmware loading, mac80211 callbacks, suspend/resume, restart after a fatal error | open (`drv/`) |
| Firmware file | `scripts/bcm4360-fwcut.py`; made by `open-test.sh` on first use |

Completeness: [coverage.md](spec/coverage.md) maps every Broadcom function a
client reaches (1,228) to a spec, "no hardware effect" (mac80211 does it) or
"not used on this card"; the only open gap is beamforming (G8), not needed
because the driver does not advertise it.

### Status (2026-11-06)

- Milestone 1 reached 2026-11-05 (`3a5656a`): `standalone.py check` - the
  open core alone makes the same hardware accesses as Broadcom's object for
  the whole standard session.
- First card test (stage rx) 2026-11-05: the chip attach ran on the card,
  then the kernel oopsed in `wiphy_register` (a const bitrate table; fixed in
  `2d2ccd6`). The restore now detects a module that holds the card and says
  that a reboot is needed instead of hanging.
- Next on the card: `scripts/open-test.sh KO --stage all` (receive only,
  then joins with software and hardware crypto).

## Milestones

Each one ends with a test. The hybrid `bcm4360` stays the installed driver
throughout. The new one is a separate module (`bcm4360_open`), and
`swap-test.sh` switches between the two, with a rollback.

1. **Bring-up without Broadcom's code (emulator).** A standalone core,
   `open/` plus a new platform and no object, runs attach, up, a
   channel and the watchdog. It must make the same hardware accesses as
   Broadcom's core alone (`hybrid.py` compares them). What is left over after
   `open/` are exactly the upper layer's hardware writes, specified in
   `wlc-hw`.
2. **Receive (card).** The module loads, brings the card up from the
   firmware file, and receives. A passive scan (`iw dev wlan0 scan
   passive`) lists the access points, and monitor mode shows frames.
3. **Transmit and join (card).** Probe, authentication and association,
   then WPA2 with mac80211's software crypto: DHCP, ping, a 20-minute soak.
4. **Rates and aggregation (card).** minstrel_ht over HT/VHT rates, with
   transmit status feedback, then A-MPDU: throughput close to the hybrid's.
5. **The rest and parity.** Hardware keys, power save, Bluetooth
   coexistence policy, rfkill, suspend and resume, regulatory domains.
   `card-test.sh` stages comparing it with the hybrid. Then the open driver
   becomes an install option, later the default.

## How the missing pieces get written

The same as everything in `open/`. Analysts with Broadcom's code write
behaviour specifications: bmac-data sections 2–3, and a new `wlc-hw` for the
upper layer's hardware writes. Implementers write the code from those
specifications only, and the emulator compares the result with Broadcom's
core, access by access.

Black-box checks back the specifications up without reading any code. The
emulator hands frames to the object's transmit path, and receive buffers to
its receive path, and records what reaches the hardware and the OS.

The Linux side has no counterpart in Broadcom's code. It is written from the
kernel's mac80211 documentation, with `brcmsmac` as the reference.

## Effort and risks (rough)

- **New code**: about 6,000–8,000 lines: the Linux side about 2,000, the
  receive, transmit and interrupt paths about 2,500, aggregation about
  1,000, and the standalone platform about 1,500. Plus the two specification
  pieces.
- **Time**: milestone 1 in about a week of sessions. Milestones 2 and 3 take
  one to two weeks each, and 4 and 5 several weeks: two to three months in
  all.
- **Card time**: from milestone 2 on, every step needs runs on the MacBook,
  started by the user (about an hour each). Expect 15 to 25 of them.
- **Risks**: the microcode interface of revision 42 (its shared-memory
  layout, the transmit header) differs from what `brcmsmac` shows for older
  revisions, so the specifications must be complete before milestone 2 can
  work. The microcode may also expect host behaviour that only shows on the
  card. Milestones 1–3 settle both before the larger work of 4 and 5.
