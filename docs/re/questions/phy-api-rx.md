# Questions from task `phy-api-rx`

Specification: `docs/re/spec/acphy-api-rx.md`, with the AC bodies in
`acphy-cal-rx.md` (`wlc_phy_rssi_compute_acphy`, `wlc_phy_upd_gain_wrt_temp_phy`,
`wlc_phy_stf_chain_temp_throttle_acphy`), `acphy-desense.md` (C3, C4, E4) and
`acphy-init.md` (H7, H9). Code: `open/phy/phy_rx.c`; small changes in
`open/phy/phy_wd.c` (the stand-in noise request replaced),
`open/phy/phy_attach.c` (Q4), `open/include/bcm4360/phy_env.h` (one service),
`open/include/bcm4360/phy.h` (a comment).

Test state: `hybrid.py check --on phy` gives the same output before and after
the change, difference for difference (74 differences from access 24548 on, all
outside the watchdog; the four watchdog ticks identical). **The session calls
none of the 12 entry points of this group**: it ran to the end before
`phy_rx.c` existed (a missing open function stops it), and a temporary
experiment with a marker read at the top of each of the 12 functions (removed
again) showed no marker access. What the session does exercise is the common
noise-sample request `sub_0baff6` (`bcm4360_phy_noise_sample_request`), through
the watchdog (steps 10 and 13.4). Everything else here is written from the
specifications only and is not compared. `ab.py all`: 0 differences in every
scenario (there is no scenario for this group).

## Q1. `wlc_phy_rssi_compute`: the early-out is not specified exactly

acphy-api-rx.md, "wlc_phy_rssi_compute (AC-PHY)": "The early-out for `sh+0x28`
> 10 with certain `rxh+0x12` bits, which marks the RSSI invalid and returns".
The header table lists `rxh+0x12` with "0x100 = ..., 0x20 = ..." and gives no
values for what the early-out writes.

Done: if the MAC core revision (`sh+0x28`, 42) is above 10 and bit 0x100 of
`rxh+0x12` is **clear** (read as "the PHY receive status is not valid"),
`rxh+0x1c` = 0, `rxh+0x1f` = 1, return. Bit 0x20 is not used.

Please give the exact test (which bits, set or clear) and the values written to
`rxh+0x1c` / `rxh+0x1f` on that path.

## Q2. acphy-api-rx.md and acphy-cal-rx.md disagree on the RSSI arithmetic

Where they differ I followed acphy-cal-rx.md, which describes the AC bodies in
more detail and with a verification:

* `wlc_phy_upd_gain_wrt_temp_phy`: acphy-api-rx.md says `out = clamp(T) -
  pi+0x210`; acphy-cal-rx.md says `d = clamp(T) - pi+0x210`, `out = (d * 2 * 7 +
  (d >= 0 ? 25 : -25)) / 50`. Implemented the second.
* The per-core outputs: acphy-cal-rx.md steps 7 and 8 have the AC body write
  `rxh+0x20+c` (the clamped per-core RSSI) and `rxh+0x1f` = 0;
  acphy-api-rx.md says the `+0x20/+0x21/+0x24` outputs are written only for N
  and HT PHYs. Implemented acphy-cal-rx.md (the AC body writes `rxh+0x20+c` for
  c < N); please confirm.
* Rounding of step 4/5: acphy-api-rx.md calls it "round half toward zero", but
  the formula it gives (`x >= 0: (x+2)>>2`, `x < 0: -((2-x)>>2)`) rounds halves
  away from zero (x = 2 gives 1). Implemented the formula.
* The sub-band correction (step 5 / step 6): the bandwidth index taken as 20 ->
  0, 40 -> 1, 80 -> 2 MHz; on 2.4 GHz `rssicorrnorm_c<c>[1]` for 40 MHz, `[0]`
  otherwise (80 MHz on 2.4 GHz is not a valid chanspec; what the object reads
  then is not said); on 5 GHz `rssicorrnorm5g_c<c>[3 * (sb - 1) + k]`. It is
  added to every core c < N, not only to the non-zero ones (the temperature
  correction is for non-zero cores only). Please confirm the order of the three
  per sub-band and that zero cores get the correction too.
* acphy-api-rx.md step 1 ("low byte of `rxh+8`? no — byte at +9") and the
  table row "+0x06 ... low byte = the raw per-antenna RSSI the AC path starts
  from" contradict each other; implemented core 0 = byte `rxh+9` (both specs'
  steps), core 1 = low byte, core 2 = high byte of the 16-bit `rxh+0x0a`. The
  16-bit fields are read as little endian.

## Q3. `qm_div16` and the combination of the cores

acphy-api-rx.md step 6 asks to "follow `qm_div16` exactly", but no document
describes `qm_div16` (acphy-cal-rx.md only names it among the maths helpers).
Implemented for `rssi.mode` 2: the sum of the active cores divided by their
number, rounded to the nearest with halves away from zero (`(2|s| + n) / (2n)`
with the sign of the sum). Also not specified, chosen:

* min/max start from the first active core;
* no active core (`sh+0xa7` & ((1 << N) - 1) = 0): result 0;
* a mode other than 0, 1, 2: treated as 0 (maximum).

Please describe `qm_div16` (its result is a quotient and a "remainder byte":
which one is used) and these three cases.

## Q4. `pi_ac+0x8dc` was not initialised at attach (fixed)

acphy-attach.md procedure 6, step 20: `pi+0x210` = `pi_ac+0x8dc` = the
temperature reference. `open/phy/phy_attach.c` set only `pi+0x210`
(`temp.rawtempsense`); `temp.measured_temp` (`pi_ac+0x8dc`) stayed 0 until the
first tempsense, which matters for the RSSI temperature correction when
`rawtempsense` is not 255. Added the one line to `acphy_attach_temp_ref`. No
other code reads it before a tempsense; no test changes.

## Q5. `sub_0b701e` is not specified

acphy-api-rx.md, "Scope": "`sub_0b701e` ... its AC path is described below";
"Noise" only says it "writes the per-core noise into the PHY's noise shared
words and adjusts the initial-gain". No addresses, no arithmetic. Not
implemented: `wlc_phy_noise_read_shmem` calls nothing at that place (it is
reached only with a non-zero measurement, never in the model). Please specify
it. A hint from acphy-desense.md C1: the per-core input `pi_ac+0x14+c` that
the carrier-sense calibration averages "was not located"; with the
calibration called right after `sub_0b701e` on the same path, `sub_0b701e` may
be its writer.

Related: the per-core dBm is given as `power >> 9` (a positive number) or -92
for a zero measurement; desense C3 says the conversion to dBm is done "via
`sub_0b701e`". Implemented literally (`power >> 9` as a 16-bit value, the
largest of the cores returned as a byte). Please confirm the conversion and
the type of the per-core values.

## Q6. The reason of `wlc_phy_noise_sample_request_external`

"a thin wrapper that calls `sub_0baff6(pi, ...)`" - the arguments are not
given. The known reasons are 1 (watchdog) and 4 (crsmincal, whose wrapper is
also 23 bytes and passes the low byte of `pi+0x17e` as the channel). Used
reason **2** and the current channel. Please give the reason.

## Q7. `wlc_phy_noise_sample_intr` is described in one sentence

"reads the noise result from shared memory (as `noise_read_shmem`) and
completes the pending request" (789 bytes in the object). Implemented:
`wlc_phy_noise_read_shmem` (the per-core reads, the history, the current slot
of the noise window, and the carrier-sense calibration when the measurement is
non-zero and the pending reason is 4 or `pi_ac+0x41` is set), then the window
index `sh+0xa0` advanced (mod 8), then `pi+0x18f` = 0. No clock test, no
"is a request pending" test, no `SHM(0x8c)` handshake. Please describe it.

Also open in `wlc_phy_noise_read_shmem`:

* the history: phy.h keeps two 16-entry rings (`pi+0xed4`, `pi+0xef4`, the
  arrays wlc_phy_BSSinit sets to -92) with the index `pi+0xf54`; written for
  cores 0 and 1, index advanced by one (mod 16) per call. Where a third core
  goes, and whether the index advances in this function, are not said.
* "fills the current noise-window slot" (read_shmem) and "store it at
  `sh+0x96 + sh+0xa0` and advance" (noise_sample_acphy) both write the same
  slot; implemented both as written.
* The order "if the total is non-zero: `sub_0b701e`, then the crsmin
  calibration" (acphy-api-rx.md) is used; desense C3 does not mention the
  total.

## Q8. `sub_0baff6`, the common request

Implemented exactly as the watchdog's stand-in that the test produced
(docs/re/questions/phy-api-watchdog.md, Q3): nothing while `pi+0x18f` is set;
else `pi+0x18f` = reason, `pi+0x190` = `sh+0x34`, `SHM(0x308..0x312)` = 0 (six
words, whatever the number of cores), `D11(0x124)` |= 0x10 (32 bit). The
"classifier/`wlc_phy_rx_iq_est_acphy` set-up" of acphy-api-rx.md does not
appear in the test. The `channel` argument is not used (please say what the
object does with it), and there is no clock test.

`phy_wd.c` now calls this function for both of its requests (reason 1, step
10; reason 4, step 13.4); the watchdog ticks stay identical.

Not changed: `wlc_phy_cals_acphy` (`phy_cal.c`) still calls
`bcm4360_phy_noise_sample_request_crsmincal` (phy_desense.c C4, which only
clears the result words) plus its own `D11(0x124)` write, and does not set
`pi+0x18f`. Sending it through the common request would set `pi+0x18f` = 4 at
the calibration of `up` and suppress the watchdog's first request, which the
object makes. So the open point of phy-api-watchdog.md Q3 stays: what the
object's pending test really is, or what clears `pi+0x18f` between the
calibration and the first tick.

## Q9. `wlc_phy_swdiv_ant_get` / `_set`: the results

The results (`s32`) are not specified; both return 0. `bit` is taken as a bit
number, `*val` as 0 or 1, the register as 16 bits (`osl_readw/writew`); set is
read-modify-write. The `0xa886`/`0xa887` branch (`PHY(0x44d)` under a MAC
suspend) is not implemented (other chips).

## Q10. `wlc_phy_stf_chain_set`: `sh+0xa6` takes the tx mask

acphy-api-rx.md: "store `rxchain` in `sh+0xa6`". `sh+0xa6` is the tx mask
(acphy-attach.md, acphy-init.md), and acphy-init.md H7 ("Callers") says
`sh+0xa6` = txchain, then `wlc_phy_rxcore_setstate_acphy(pi, rxchain)`.
Implemented H7's version.

## Q11. `wlc_phy_stf_chain_active_get`

acphy-api-rx.md: "if the throttle flag `pi+0x18c` path is set it calls
`wlc_phy_stf_chain_temp_throttle_acphy`", and leaves the throttle to "describe
from the code"; it is described in acphy-cal-rx.md, which I followed.
`pi+0x18c` is `watchdog_override` in phy.h (1 after attach), so the throttle
runs on every call. Chosen where not said: no clock test (the tempsense
accesses happen whenever the MAC calls this); the temperature compared as a
signed value with the unsigned thresholds `pi+0xc2e` / `pi+0xc31`; the
schedule `{1,1,2,1,4,1,2,1}` indexed with `sh+0xa6 & 7`; `sh+0xa6` itself is
not changed. Please confirm the gate (is it really `pi+0x18c`?) and whether
the object tests the clock first.

## Q12. New service

`s8 bcm4360_mac_ol_noise_avg(hw)` (`phy_env.h`) for
`wlc_ol_noise_avg_offload(wlapi_get_wlc_info(physhim))`, as acphy-api-rx.md
names it; `wlc_phy_noise_avg` returns its answer when it is not 0.
