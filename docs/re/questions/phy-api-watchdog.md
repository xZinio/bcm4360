# Questions from task `phy-api-watchdog`

Specification: `docs/re/spec/acphy-api-watchdog.md`, with acphy-desense.md
E1/E3/H1/H2, acphy-rxgain.md section 8, acphy-attach.md "Function pointer
table". Code: `open/phy/phy_wd.c`; small changes in `open/phy/phy_desense.c`,
`open/phy/phy_init.c`, `open/include/bcm4360/phy.h`.

Test state: `hybrid.py check --on phy` now runs the whole session (19 steps,
41416 accesses of the object, 41368 of the hybrid core). The session has four
watchdog ticks, two after each `up` (object accesses 33443-33507, 33538-33569,
41122-41186, 41217-41248, located with temporary marker reads that are
removed again). **All four ticks are identical to the object.** The remaining
74 differences are outside the watchdog: 24548-33058 as before (the known
transmit-power difference and what follows it), and 38914-40722 in the second
`up`, which have the same shape as 24578-33058 (MAC writes and reads of
`SHM(0x0e)`). `ab.py all`: 0 differences in every scenario.

How the results below were obtained: only from the comparison tests,
including temporary experiments (marker reads; steps 13.4 and 13.6 switched
off, then 13.6 alone, which makes the object's `PHY(0x520)` and `PHY(0x418)`
accesses appear as missing). Nothing of these remains in the code.

## Q1. The function pointer `pi+0x100` is not null (step 13.4)

The watchdog spec says `pi+0x100` is null for the AC-PHY. acphy-attach.md
("Function pointer table") says it is `sub_09737d` (`wlc_phy_watchdog_acphy`).
The test agrees with acphy-attach.md: in every tick the object suspends the
MAC and steps the core field of `PHY(0x520)` (0, then 0x0004). Implemented as
acphy-attach.md describes `sub_09737d` (`wd_watchdog_acphy`), including its
crsmincal noise request (Q3).

## Q2. `wlc_phy_stop_bt_toggle_acphy` acts on this card (step 13.6)

acphy-rxgain.md section 8 says: "boardflags = 0x10401001, so bit 22 is clear and
this function does nothing". 0x10401001 contains 0x00400000: bit 22 is **set**.
With `pi_ac+0x32e` = 0xff the function acts (m = 2, so v = -1: release the
override, `PHY(0x418)` bits 1 and 0 cleared, inside phyreg_enter/exit and a MAC
suspend). The test matches only with this.

`pi_ac+0x32e` = 0xff is acphy-init.md section 3, step 8; `open/phy/phy_init.c`
did not set it. Added there (`phy->femctrl_ovrd = -1`; nothing else reads it).

## Q3. The noise-sample request `sub_0baff6` (steps 10, 13.4) - receive group dependency

acphy-api-rx.md says its AC accesses are "the classifier/`wlc_phy_rx_iq_est_acphy`
set-up". The test shows something else: the only accesses are those
acphy-desense.md C4 names - `SHM(0x308..0x312)` = 0 (six words) and
`D11(0x124)` read, written back with bit 4 (0x10) set - and **nothing at all
while a sample is pending**: tick 1 makes one such request, tick 2 none (neither
the watchdog's own request nor the crsmincal request of `sub_09737d`). After the
second `up` tick 3 makes one again, so something clears the pending state in
between - acphy-init.md section 1 step 2 (`pi+0x18f` = 0) does; added to
`open/phy/phy_init.c`.

Since `open/phy/phy_rx.c` does not exist yet, `phy_wd.c` has a static stand-in
(`wd_noise_sample_request`): if `pi+0x18f` (`noise_reason`) is not 0, nothing;
else `pi+0x18f` = reason, `pi+0x190` = `sh+0x34`, the six SHM writes, the
`D11(0x124)` bit. Step 13.4 calls it with reason 4 instead of
`bcm4360_phy_noise_sample_request_crsmincal` (that stub has no pending check).
When the receive group provides `bcm4360_phy_noise_sample_request`, the
stand-in should go, and C4 in `phy_desense.c` (and the extra `D11(0x124)`
write in `phy_cal.c`) should go through it.

Open points for the analysts:

* What exactly the "pending" test is. The calibration at `up` also requests a
  sample (reason 4, `phy_cal.c`); if that set `pi+0x18f`, tick 1 (one second
  later) would have been skipped, but it is not. Either the calibration's
  request does not leave `pi+0x18f` set, or the test is not simply
  `pi+0x18f != 0` (by reason?). The open calibration path does not touch
  `noise_reason`, which makes the session match.
* Which request makes the accesses in tick 1 - the watchdog's (reason 1,
  step 10) or `sub_09737d`'s (reason 4, step 13.4) - cannot be seen in the
  model (no card access between them). Taken: step 10's.

## Q4. The engines look the record up; they do not "create" it (step 6)

acphy-desense.md E: "each first makes sure the current channel's interference
record exists (`sub_092efb(pi, pi+0x17e, 1)`)". With `create` = 1, A4 step 3
reads the clock (`D11(0x180)`, `D11(0x184)`) and sets record+0x45 = 2. The test
shows **no clock read** in any tick (the open code had four per tick: E3 and
E1). Implemented `bcm4360_phy_desense_chan_record` (phy_desense.c): look the
record up (`create` = 0), create it only if absent; E1 (phy_desense.c) and E3
(phy_wd.c) use it. Whether the object instead uses `pi_ac+0x8a8` when it is set
cannot be told from the session (the record always exists there).

This also resolves a contradiction in E3: with `create` = 1 every tick, its
step 1 would always find record+0x45 = 2 and return, never reaching step 4,
while the verification table says it "returns at step 4".

## Q5. Step 5: the statistics fold (guessed, no effect in the model)

The text lists five history triples (`pi+0x492`/`0x490`/`0x488`, `0x2ea`/`0x2e4`/
`0x2fc`, `0x3ea`/`0x3f2`/`0x400`, `0x410`/`0x40a`/`0x418`, `0x412`/`0x408`/`0x420`)
for "four per-tick deltas", leaves the fold open ("for 2.4 GHz the second and
third counters are folded into the first and fourth"), and names a stored
`pi+0x302` without saying what it is. E3 reads histograms at `pi+0x3f6`,
`0x3fa`, `0x414`, `0x41c` with selectors `0x400`, `0x404`, `0x418`, `0x420`,
which do not match these bases. What I did (`wd_statistics`):

* history 0 (`pi+0x492`) takes delta0 (`SHM(0x10e)`);
* 2.4 GHz: history 1 takes delta0 + delta1, histories 2 and 3 take 0,
  history 4 takes delta2; 5 GHz: histories 1, 2, 3 take delta0, delta1, delta2,
  history 4 takes 0; negative deltas are not pushed; `pi+0x302` is not used;
* E3's inputs, matched by their selector fields: `pi+0x3f6` (sel `0x400`) =
  history 2, `pi+0x414` (sel `0x418`) = history 3, `pi+0x41c` (sel `0x420`) =
  history 4, `pi+0x3fa` (sel `0x404`) = history 1 (the one left over).

Please give the exact fold, the meaning of `pi+0x302`, and which history feeds
which E3 input.

## Q6. Smaller points

* Step 3: "written to a pair of PHY registers (`pi+0x45a`/`0x45c`, ...)". Taken
  as PHY register addresses 0x45a/0x45c, 0x44c/0x448, 0x446/0x44a, written
  with the 16-bit override value. Are they addresses in `pi` instead? Iovar
  only; not reached.
* "Steps 11-13 run only when the radio is on and associated (`pi+0x19c &
  0x20f` = 0)": 0x20f does not contain the not-associated bit 0x20 (set by
  acphy-init.md section 1 step 6), so step 13 runs while not associated; the
  session shows it. Steps 11 and 12 implemented with their own conditions.
* Step 13.5: the callback `pi+0xf8` (`wlc_phy_btc_adjust_acphy`) has no
  specification; acphy-attach.md gives its arguments as "pi, new period (8
  bit)", the watchdog spec calls it when the *state* byte changes. Implemented
  as an empty function, called with the state; period and state are stored.
  Whether `wlapi_bmac_btc_mode_get` returns non-zero in the session is not
  visible (the services make no card access).
* The spec asks for a new field `bt_scan_active` for `pi+0x18f`; phy.h already
  had `noise_reason` / `noise_time` for `pi+0x18f` / `pi+0x190` (the receive
  group's names). Used those.
* E3 (not exercised, all inputs 0): the upper bounds record+0x2d / +0x41 are
  never written in the description; "reset the counter" and "save the counter
  (+1, capped at 255)" read as: 0 on a reset, else +1; "average of its two
  largest" as (a + b) / 2, truncated.
* H1 step 1, "decrement the other band's remaining ticks toward -1": written
  as "if above -1, minus 1". H1/H2 not exercised (per-band enables 0).
* `sub_0b72a4` (step 12) is still unspecified (phy-api-txpower Q5); it returns
  false, so `pi+0xc44` is never set.
