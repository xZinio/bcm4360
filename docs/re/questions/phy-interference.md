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
