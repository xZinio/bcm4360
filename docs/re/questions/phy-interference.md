# The interference engines with live input (2026-11-03)

Changed: `open/phy/phy_wd.c` (E3 hysteresis), `open/phy/phy_desense.c` (E1
restore of `PHY(0x19e)`), `open/phy/phy_chanspec.c` (step 24 passes the "on"
byte), `open/phy/phy_cal.c` (tempsense constant); tools
`tools/re/phy_soak.py --interference`, `tools/re/blackbox.py`. The session
that made these changes also ran and analysed the card tests; it opened
nothing below `lib/` or `re-out/`. What the object does was settled by
running it with chosen inputs (`blackbox.py`), and the specifications were
corrected in place from that ("Corrections (interference session)" in
`acphy-desense.md`, the constant in `acphy-cal-rx.md`).

## Why: card run 5 (2026-11-03 18:16, `/var/log/bcm4360-card-test-20261103-181604`)

The first full staged run with the open PHY (PSK network 'B-DTU Wireless',
Cisco Meraki APs). Every functional stage passed (load, 2.4 GHz, two radio
switches, link down/up, two reconnects, suspend), but the open PHY joined
2.4 GHz channel 11 (-67 to -71 dBm, 80 % channel load, protection on) and
stayed there; over the 20-minute soak pings failed more and more often, and
at the end there was no traffic at all (DNS failed, 0 Mbit/s, the scan found
no 5 GHz BSS). Broadcom's PHY was soaked on 5 GHz in the same run, so the two
soaks are not directly comparable; its 2-minute 2.4 GHz watch on the same AP
lost pings too (21 s outage; the stock driver at the start of the run as well).

What the MAC counters (`scripts/wl-iovar.py`, the diag files) show:

| Stage | per data frame received: RTS to us / CRS glitches |
|---|---|
| Broadcom's PHY, 2.4 GHz watch, same AP | 0.09 / 1.1 |
| open PHY, before any radio switch, same AP | 0.26 / 3.2 |
| open PHY, after the radio switches | 2.8 / 5 |
| open PHY, last 18 minutes of the soak | 130 to 200 / 190 to 310 |

At the end the AP sent RTS to us 700,000 times and we answered each with a
CTS (`txctsfrm` = `rxrtsucast`), but almost no data frame followed or was
received: the link was associated and dead. The carrier-sense glitch rate
was about three times Broadcom's from the first minute, on the same AP and
channel. That pointed at the receiver's desense - the one part of the PHY
whose inputs (the MAC's glitch counters) the emulator answered with 0, so
the open and Broadcom's engines had only ever been compared on their early
return.

`txsfovfl` jumps by about 65,536 at a time with the open MAC with either PHY
(also in Broadcom's 5 GHz soak); that is the known 16-bit counter wrap of the
open MAC, unrelated.

## What the interference session found

`phy_soak.py --interference` (53 steps; the glitch counters count by the
emulated clock, the detector registers read seeded values, RSSIs from -95 to
-60 dBm): before the changes the open PHY differed in 10 steps, now in none.

1. **E3, the dwell counter** was 0 after a level change, the object's is 1
   (the spec said "reset" and "+1"; the open code did only the first). Every
   lowering came one tick late.
2. **E3, the upper bound** of a path is set on a lower (to the level lowered
   from); the spec omitted it. Without it every raise took `level + 4`.
3. **E1 step 8** restores only bit 1 of `PHY(0x19e)` (a `mod`, with a read);
   the open code wrote the saved word back.
4. **The channel function, step 24**, called the desense application with 0
   instead of the "on" byte `pi_ac+0x670`. So every channel set - a scan
   channel, the return from it, radio on, `up` - wrote the no-desense values
   (carrier-sense threshold 0x36 instead of e.g. 0x3e, `PHY(0x304..0x30d)`
   0x4e51 instead of 0x5f62) while the channel's record still held its
   desense. The engine re-applies only when a level changes, and on a
   saturated channel the level sits at its clamp: the hardware then stays
   without desense for as long as the interference lasts. This matches the
   card's high glitch rate from the first scan on and the worsening after the
   radio switches; whether it is the whole story the next card run must show.
5. **The temperature sense** added 1901076 before the division by 0x4000;
   the spec's hex value 0x1d0614 is right, its decimal was wrong (0x1d0614 =
   1902100). The open temperature was 1 degree low in about 9 % of readings,
   which shifted the transmit-chain throttle by tens of seconds in the soak.

## Not covered

* The inputs are invented, not a card's: rates, detector readings and RSSIs
  are chosen to cross every threshold, not recorded. A recording of the
  counters and detector registers on a busy channel would make the session
  realistic (`scripts/wl-iovar.py` reads the counters; the detector registers
  need a PHY register read).
* The high-RSSI external-LNA bypass (H1, H2) still never triggers: the model
  never raises the microcode's high-RSSI flag and the board has the feature
  disabled (`pi_ac+0x910/0x911` = 0).
* `wlc_phy_desense_aci_reset_params_acphy(pi, apply, a, b)` is specified only
  for (1, 1, 1); the open code ignores `a` and `b` (clears both bands'
  records) and always applies. The callers with other arguments (H2 with the
  band flags, E5 with (0, 0, 0)) are not reached by any session. Question to
  an analyst: what do `apply`, `a` and `b` select?

## Card run 6 (2026-11-03 22:38, `/var/log/bcm4360-card-test-20261103-223806`)

The same network and APs, with the changes above and a 10-minute 2.4 GHz
watch, locked to the BSSID, for both PHYs (`card-test.sh --watch24`). Every
stage passed; the restore left the installed driver connected.

| 600 s on channel 11, same AP, per data frame received | Broadcom's PHY | open PHY |
|---|---|---|
| pings lost / longest outage | 198 / 36 s | 186 / 35 s |
| RTS to us / CRS glitches | 36.3 / 106 | 39.0 / 108 |
| bad PLCP / bad FCS | 1.05 / 7.7 | 0.93 / 9.6 |
| frames retransmitted / sent | 59 % | 79 % |
| no ACK / transmit failures | 6,490 / 94 | 9,767 / 332 |

The glitch rate gap of run 5 is gone. The channel itself was bad that
evening: Broadcom's whole driver (every switch off) lost 70 % of pings on
it, Broadcom's PHY lost 27 of 42 in its 8-minute soak there. The open PHY's
20-minute soak ran on 5 GHz this time: 0 of 111 pings lost, 176 to 239
Mbit/s; suspend and resume clean (0 pings lost in the minute after).

What is left is on the transmit side (more retransmissions and failures).
The two register captures of the run (load to association, Broadcom's PHY
and the open PHY) go through the same 74 channel sets of the scan; the
desense registers (`PHY(0x304..0x30d)`, the carrier-sense thresholds
`PHY(0x321..0x336)`, `PHY(0x31c..0x31f)`) are written identically in all of
them. Differences:

* **The idle TSSI of core 1**: `PHY(0x845)` = 0x200 (the most negative
  10-bit value, the converter's rail) in the open PHY, 0x25a in Broadcom's;
  core 0 0x264 and 0x266. As a consequence the start index of core 1's power
  loop, `PHY(0x844)`, is 0x14 on every channel (Broadcom's 0x19, 0x1a), and
  core 0's differs by one or two. The cause is in the accesses just before
  each measurement: Broadcom's object takes the radio from the
  Bluetooth-coexistence logic (`D11(0x6b4)`, `D11(0x6b8)`, `D11(0x6f0)`,
  `D11(0x6f2)`, `acphy-txpower.md` section 28) and gives it back after; the
  open code had both functions as stubs, because in the model bit 29 of the
  MAC capabilities is 0. The card has it.
* One procedure (`PHY(0x19e)`, `PHY(0x392..0x394)`, `PHY(0x720..0x73e)` per
  core, radio 0x00e, 0x15f, 0x161, 0x16e) runs 8 times in Broadcom's capture
  and 7 times in the open one, with the same values each time; not followed
  up.
* After its association on 5 GHz the open PHY's session moved to 2.4 GHz
  within the 50 s of the capture (Broadcom's did not); there the desense
  engine raised the thresholds (`PHY(0x304)` 0x5f62, `PHY(0x321)` 0x363e)
  as it should on that channel.

## The Bluetooth-coexistence override (2026-11-04)

`acphy-txpower.md` section 28 was fully specified; the open functions were
empty because the model's MAC capabilities (`D11(0x15c)`) are 0. The card's
are 0xb0518c05 (read in the capture), bit 29 set. Now implemented in
`open/phy/phy_txpower.c`, with the new service
`bcm4360_mac_eci_coex_enabled` (`wlapi_is_eci_coex_enabled`, 0 or 1:
`blackbox.py eci`). The model has an option for the capabilities
(`bcm4360.py` `machwcap`, `cc_caps`; `phy_soak.py --machwcap`, `--cc-caps`).

* `phy_soak.py --machwcap 0xb0518c05`, 16 steps (2.4 GHz channel 11 with a
  join and 40 s of watchdog, then 36/80): the object takes the radio from
  the coexistence 4 times and gives it back 4 times (`D11(0x6b4)`,
  `D11(0x6b8)`, `D11(0x6f0)`, `D11(0x6f2)`, `D11(0x6b6)`); the open PHY the
  same, 117,837 accesses on both sides, the same. With the model's 0 neither
  makes an access.
* With ECI as well (`--cc-caps 0x70280000`, not the card's case) the
  override is the same too, but 2 steps differ: the watchdog's coexistence
  callback `wlc_phy_btc_adjust_acphy`, which has no specification and is
  empty in the open code, runs when the state byte of the Bluetooth period
  turns 1 (see `phy-api-watchdog.md`).

Whether the override cures core 1's idle TSSI and the retransmissions on
the card is for card run 7: `PHY(0x845)` near 0x25a and `PHY(0x844)` varying
by channel in the open capture, and the 2.4 GHz watch's retransmission rate.

## Card run 7 (2026-11-04 01:00, `/var/log/bcm4360-card-test-20261104-010053`)

Build 1.1.0 `F873514CFC033962A7238B3` (the override). The same network and AP
(channel 11), the same plan as run 6. Every stage passed (load, 2.4 GHz,
reconnects, two radio switches, link down/up, suspend, the 20-minute soak,
both captures); the restore left the installed driver connected.

**The override works on the card.** In the open capture core 1's idle TSSI
`PHY(0x845)` is 0x256 (Broadcom's 0x256, 0x257; run 6: 0x200), core 0's
0x264, 0x265 (0x262, 0x264). Both captures take the radio from the
coexistence the same number of times (`D11(0x6b4)` 8 reads and writes,
`D11(0x6b6)` 4 reads, `D11(0x6b8)` 16, `D11(0x6f0)` 4 writes, `D11(0x6f2)` 4
reads).

**Core 1's start index is still 0x14, for another reason.** At each channel
set the PHY reads the power loops' current indices from `PHY(0x640)` and
`PHY(0x840)` (high byte) and writes them back as start indices
(`PHY(0x644)`, `PHY(0x844)`). With Broadcom's driver both move (core 1 0x1a
to 0x1c); with the open driver `PHY(0x840)` reads 0x1400 throughout: core 1's
loop never got a TSSI sample, because core 1 never transmitted. The transmit
core masks of `wlc_stf_txcore_shmem_write` (`SHM(0x5d4)`, `0x5d6`, `0x5d8`:
CCK, OFDM, one stream) are, from the first channel set after the load:

| | 2.4 GHz | 5 GHz |
|---|---|---|
| Broadcom's driver (every switch off) | 3, 3, 3 | 3, 3, 3 (3, 1, 3 on some channels) |
| open driver (defaults and the open PHY) | 1, 1, 1 | 3, 1, 1 or 3, 1, 3 |

So on 2.4 GHz the open driver sends every single-stream frame from core 0
alone, without the second chain's diversity (CDD). The per-rate words of the
rate table (`SHM(0x99a)` .. `SHM(0xa8e)`, stride 0x14) differ as well.

The emulator with the card's own SROM (the run's `bcm4360-srom-window.bin`,
234 words, CRC valid; it contains the MAC address, so it stays local): object
and hybrid are identical with the defaults and with the open PHY (17 steps,
125,347 accesses), and **Broadcom's object itself chooses 1, 1, 1 on
channel 11** and 3, 1, 1 on 36/80. The SROM words both drivers read at the
load on the card are the dump's. Ruled out in the emulator: `tempthresh`
(98 on the card; 255 changes nothing), the user power limit (`qtxpower` 92 as
on the card, or 127), the board identity (the model is 106b:0117 too),
`boardflags2` (0x2: the "CCK on core 0" flag is not set). `regrev` matters
(17 on the card, `ccode` X3; with 0 the object chooses 1, 3, 3; the country
US gives 3, 3, 3), but the country in use is X3 in the model as on the card.

**The input is not the card's: it is Broadcom's uninitialised
`min_txpower`** (`cca-stf.md`, `wlc_update_txppr_offset` step 1 and open
question 1). `tools/re/mmiotrace.py replay` runs the object with every read
answered as the card answered it; through the hybrid core with every switch
off (the module the capture recorded), skipping the interrupt handler's
interleaved accesses (2) and accepting the host's DMA ring addresses, the
replay of run 7's Broadcom capture reaches the first mask write (access
27,649) and writes 1 where the card wrote 3: all hardware inputs equal, the
choice still differs. With a value written into the stack slot that
`wlc_update_txppr_offset` reads unwritten, 0 or -1 give the same divergence;
16, 20, 23 or a stale heap address give 3, 3, 3 and the replay runs through
`up` without a divergence (28,541 accesses). On the card the stack holds
something there; the open code takes 0 as the specification says. Runs 5
and 6 show the same (Broadcom's driver 3, 3, 3 at 77 of 78 channel sets on
2.4 GHz, the open code 1, 1, 1 always).

So the open driver sends single-stream frames from core 0 alone with every
configuration that has `open_stf` on - **also the stage "def" with
Broadcom's PHY**. The 2.4 GHz watch below compares two single-chain
configurations; its difference between the PHYs is a PHY matter, not this
one.

The 2.4 GHz watch, 600 s on the same AP:

| per 600 s | Broadcom's PHY | open PHY |
|---|---|---|
| pings lost / longest outage | 209 / 29 s | 228 / 35 s |
| frames sent / retransmitted | 9,676 / 50 % | 10,121 / 65 % |
| no ACK / transmit failures | 4,936 / 55 | 6,680 / 91 |
| RTS sent, no CTS | 19,062 / 12,086 | 22,247 / 18,084 |
| data frames received / bad FCS | 4,326 / 17,016 | 2,497 / 25,624 |

Broadcom's PHY here is the stage "def" (the open layers with Broadcom's PHY),
so both columns ran with the open spatial policy (masks 1, 1, 1). Both soaks stayed on 2.4 GHz
this time (Broadcom's PHY 15 of 42 pings lost in 8 minutes, the open PHY 51
of 102 in 20); the channel was as bad as in run 6.

The hardware gives both drivers the same inputs up to the first mask write:
aligned, the two captures (27,650 accesses to that point) differ in read
values only in status and timing bits (OTP busy, TSF, clock status), one
microcode scratch register (SCR 9), three PHY reads, and the
coexistence data register `D11(0x6f2)` (0x1300 under Broadcom's driver, 0 under
the open one; read and discarded by the override in both). The PMU writes of
the attach are the same; the open code makes them through BAR0's fixed
ChipCommon window (offset 0x3000 + register), Broadcom's through the switched
window. The glue sets `qtxpower` 92 right after the attach, before `up`; in
the emulator that order changes nothing either.

## The scans with desense on (2026-11-04)

The regression after the override found the interference session different
in its three scans (steps 14, 33, 54; 114 accesses more each): on a scan
channel the open code applied the home channel's desense (`PHY(0x304)` 0x5f62,
`TBL(0x0b)` gain limits 0x7f), the object the baseline (0x4e51). Step 20 of
the channel function (`acphy-chanspec.md`) sets the channel's record pointer
`pi_ac+0x8a8` to 0 before anything else; the open code left it, so on a scan
channel step 21 took the home channel's record. Harmless until run 6's step
24 fix, since step 24 used to pass 0. Fixed in `open/phy/phy_chanspec.c`;
the session (59 steps) is now the same as the object, 545,262 accesses on
both sides.

`blackbox.py restore` matched every R R W R W pattern of `PHY(0x19e)`, also
other functions' accesses; it now checks per call of the hwaci engine (its
first read is the save, its last write the restore): 3 calls, all "only
bit 1".

## Card run 8: the power floor on the card (2026-11-04 21:48, `/var/log/bcm4360-card-test-20261104-214816`)

Build 1.1.0 `9C3F9FC2620995660FC1FBC` (module parameter `stf_min_txpower`,
commit 3ffe820). `card-test.sh --ab24 ... --watch24 600`: every configuration
loaded, measured, then watched for 600 s on the same 2.4 GHz AP (B-DTU
Wireless, `a8:46:9d:29:65:d7`, channel 11, -64 to -72 dBm), in this order,
with `def0` repeated at the end against drift. The deltas are the core's
counters from the start to the end of each watch.

| per 600 s | def0 | def16 | stock | phy16 | def0b |
|---|---|---|---|---|---|
| watch start | 21:56 | 22:17 | 22:37 | 22:57 | 23:23 |
| floor (dBm) | 0 | 16 | Broadcom's stack | 16 | 0 |
| pings lost / longest outage | 411 / 144 s | 351 / 125 s | 292 / 34 s | 368 / 46 s | 114 / 44 s |
| frames sent | 12,560 | 21,141 | 10,246 | 11,315 | 5,757 |
| retransmissions per frame | 1.23 | 0.94 | 0.68 | 0.85 | 0.43 |
| transmit failures | 10.2 % | 9.9 % | 0.6 % | 2.0 % | 0.7 % |
| RTS from the AP to us | 273,669 | 166,936 | 182,239 | 209,811 | 69,041 |
| data frames received | 2,402 | 6,639 | 2,652 | 2,362 | 2,254 |

`phy0` (open PHY, floor 0) has no watch: its load joined another AP of the
network (`a8:46:9d:29:66:00`, 2.4 GHz, -83 dBm; the scans of the run show
that AP anywhere between -65 and -88 dBm), DNS timed out and swap-test rolled
back. The kernel log stayed clean.

**The channel dominates, the floor does not show.** The same configuration
`def0` lost 411 pings with 10.2 % transmit failures at 21:56 and 114 with
0.7 % at 23:23; in between, the AP's RTS to us fell from 274,000 to 69,000
per 10 minutes (each answered with a CTS, `txctsfrm`; most of the data
after them never arrives, the run 5 RTS loop, and the AP tries again). `def0` and
`def16`, next to each other in time, differ less than `def0` and `def0b`.
The earlier A/B on a clean AP (eduroam, channel 6, -47 dBm,
`/var/log/bcm4360-card-test-20261104-141647`, 480 s each, the same four open
configurations) lost no ping in any of them, with at most 0.1 % transmit
failures. Neither run measures an effect of the floor.

The open PHY next to Broadcom's code (`stock` 22:37, `phy16` 22:57): 2.0 %
against 0.6 % transmit failures, 368 against 292 pings lost - the same
direction as runs 6 and 7 (2.7 % against 0.8 %, 0.9 % against 0.6 %, there
against the stage "def"), still within the drift of this channel.

So `stf_min_txpower` stays 0 by default, the value of the specification: the
floor of Broadcom's driver comes from a stack word nobody wrote, whose value
depends on the calls before it, and two chains for single-stream frames on
2.4 GHz gave nothing measurable on either AP. The parameter stays, for
comparisons with captures of Broadcom's driver (16 reproduces its mask
writes).

## Card run 9: both PHYs interleaved, with hardware snapshots (2026-11-05 02:16, `/var/log/bcm4360-card-test-20261105-021653`)

Build 1.1.0 `FE92A2D485A6826B3D4AFC4` (commit 1d3435c: `/proc/bcm4360_snap`,
`scripts/phy-snap.py`). `card-test.sh --ab24 'def1:;phy1:open_phy=1;def2:;
phy2:open_phy=1;def3:;phy3:open_phy=1' --watch24 300`: Broadcom's PHY and the
open PHY in turn, three loads each, each watched 300 s on the same 2.4 GHz AP
as run 8 (B-DTU Wireless `a8:46:9d:29:65:d7`, channel 11, -67 to -72 dBm),
with a hardware snapshot (10,383 words: PHY and radio registers, PHY tables,
shared memory) after the join and two, 2 s apart, at the end of each watch.
Every load passed; the kernel log stayed clean but for one "join failed
(status 1)" of `phy1` when the harness switched to the BSSID-locked profile
(joined 7 s later). The first snapshot (`def1` after the join) was lost to
an lseek in `phy-snap.py` (fixed in 0d7d036); all 17 others took 0.08 s.

| per 300 s | def1 | phy1 | def2 | phy2 | def3 | phy3 |
|---|---|---|---|---|---|---|
| watch start | 02:19 | 02:29 | 02:39 | 02:49 | 02:58 | 03:08 |
| pings lost / longest outage | 26 / 24 s | 49 / 31 s | 62 / 35 s | 0 | 0 | 0 |
| frames sent | 3,051 | 2,905 | 5,335 | 861 | 764 | 771 |
| retransmissions per frame | 0.25 | 0.30 | 0.35 | 0.01 | 0.00 | 0.00 |
| transmit failures | 0.2 % | 0.1 % | 0.5 % | 0 | 0 | 0 |
| RTS from the AP to us | 20,896 | 23,936 | 43,462 | 825 | 705 | 694 |

The AP's RTS loop of run 8 was on until about 02:50 and then stopped; the
three loads before it and the three after it show no difference between the
PHYs (Broadcom's PHY 88 pings lost in all, the open PHY 49).

**The hardware state is the same.** `phy-snap.py` snapshots compared over
the three loads of each PHY (values that move within two seconds left out:
254 - counters, IQ estimates, power-loop indices): no value is the same in
all loads of one PHY and different in the other's. 274 values vary from
load to load in both groups alike: the desense (on or off, as the glitches
of the moment had it), the calibration results (RC calibration
`RADIO(0x414)`/`(0x415)`, the analog filter trims of `TBL(0x07)` derived
from it, the coefficients of `TBL(0x0c)` and `TBL(0x10)`), and words the
microcode writes itself (`SHM(0xae8)`..`(0xb28)`: no host access in the
captures). With two loads each, ten values had looked systematic (the RC
calibration 1-2 counts lower, three coefficients of `TBL(0x0c)` 0 instead of
-1, five of those microcode words); the third pair dissolved them all.

With runs 6 to 8 the open PHY has now run every stage of `card-test.sh` on
the card (load, both bands, reconnects, radio switch, down/up, suspend, the
20-minute soak) and shows neither a different hardware state nor a
difference in the link beyond the channel's drift. `open_phy` is on by
default since this run.
