# Task `phy-soak`: the open PHY identical in the long session

The open AC-PHY (`open/phy/`, behind the module switch `open_phy`, off by
default) is identical to Broadcom's object in `hybrid.py`'s session. That
session runs only half of its code. `tools/re/phy_soak.py` runs much more -
every channel and bandwidth, an association stand-in with the full
calibration, long watchdog runs with the periodic calibration, RSSI on many
receive headers, the noise interrupt - with the card model giving the
measurement registers deterministic non-zero values, and with board variants
(`--board N`: the SROM variables the PHY reads changed). It reports the steps
that differ. **The goal of this task: `phy_soak.py` reports "the same"**, for
the synthetic board and for `--board 1` to `--board 8`, so that the open PHY
can become the driver's default.

Read `../implementing.md` first (binding), then `tools/re/phy_soak.py`'s
docstring (what each step does).

## What differs today (coordinator's run of 2026-11-02)

1. **RSSI** (`rssi` steps, every call): the object sets `rxh+0x24` = 0 for
   the AC-PHY and in the early-out; the open code leaves it. The
   specification is corrected: `../spec/acphy-api-rx.md`, "The received-frame
   header" and "wlc_phy_rssi_compute".
2. **The calibration of a join** (`joincal`, with non-zero IQ power
   estimates): identical until about 41,160 of 46,430 accesses, then the object
   sets bit 5 and then bit 9 of `PHY(0x729)` (and `PHY(0x929)` for core 1) in
   two read-modify-writes where the open code sets bit 9 only; 118 more
   differences follow, about 1,200 fewer accesses in the open code. With zero
   estimates (the `phy-cal` scenario of `ab.py`) this part was never reached.
   Specifications: `../spec/acphy-cal-rx.md`, `../spec/acphy-cal-tx.md`.
3. **The periodic calibration** (`wait 130` after `149/80`, `unquiet`,
   `joincal`, `interf 1`; and `wait 920`): the object runs its periodic
   calibration in the watchdog; the open code instead reads and writes
   `PHY(0x520)` (= 4), suspends the MAC, ... and does not calibrate (74,359 vs
   18,545 accesses in that step; 242,916 vs 131,288 in `wait 920`).
   Specifications: `../spec/acphy-api-watchdog.md`, `../spec/acphy-cal-tx.md`
   section 3, `../spec/acphy-desense.md` (the interference mode set at the
   join).
4. **Table A1** (boards 1, 4, 5 - `pdgain2g` 30, 20, 23): the open code reads
   past the 19 rows of its table. `../spec/acphy-chanspec.md` "Table A1" now
   gives all 32 rows (what the object reads for every 5-bit value): use them,
   never read beyond.
5. **Single-chain boards** (boards 7 and 8: `txchain` = `rxchain` = 1): 16
   accesses fewer at every channel change, 4 fewer per watchdog tick, more in
   the calibration and noise steps. Find where the open code's handling of the
   chain masks differs from the specification.
6. **The noise path** (`noise` steps, then the CRS thresholds carried into
   later steps such as `up`): its specification is being completed by an
   analyst (`../spec/acphy-api-rx.md`, a new section "Noise, completed").
   **Not part of this task until that section exists**; you will be told.
   Ignore the differences of the `noise` steps and those that only follow
   from them (`PHY(0x324)`, `0x330`, `0x321`, `0x32d`, `0x32a`, `0x336` ...
   values in later steps).

## Rules

* Clean room: `../implementing.md`. Work from the specifications and the
  observed behaviour the soak reports. Where they disagree, the observation
  is right: write the disagreement into `docs/re/questions/phy-soak.md`
  (section, what the soak shows, what you did). Where the specification does
  not say enough to reproduce an observation, write the question and go on
  with the next difference - do not guess values.
* Change only `open/phy/*.c` and, if a prototype must change, `open/phy/*.h`;
  not `open/include/bcm4360/phy_api.h` (the interface `hybrid/phy.c` calls),
  not `hybrid/`, not `tools/`.

## Tests

From `tools/re`, in Docker (`docker run --rm -v "D:\Programs\bcm4360:/repo" -w
/repo/tools/re -e HYBRID_OUT=/repo/re-out/hybrid-physoak bcm4360-emu:latest
...`):

* `python phy_soak.py` and `python phy_soak.py --board N` (N = 1..8): the
  same, apart from point 6. A full run takes several minutes; `--steps
  a,b,c` runs a shorter list of steps (for example `attach,mpc 0,up,roam_off
  1,36/80,assoc,unquiet,joincal` for point 2).
* `python hybrid.py check --on phy`, `check --on phy --off mac`, `python
  hybrid.py srom --on phy`: still the same.
* `python ab.py build` (no warnings) and `python ab.py all`
  (`AB_OUT=/repo/re-out/open-physoak`): still pass.

Finish with a short report: what you changed (file, function, why), the
summary line of each test, and the questions you raised.
