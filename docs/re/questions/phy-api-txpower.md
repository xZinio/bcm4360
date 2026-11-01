# Questions from task `phy-api-txpower`

Specification: `docs/re/spec/acphy-api-txpower.md` (with acphy-txpower.md
sections 17 to 19). Code: `open/phy/phy_txpower.c` (from "The MAC-facing
transmit-power entry points" on), `open/include/bcm4360/phy.h`.

Test state: `hybrid.py check --on phy` runs the whole session up to the first
watchdog tick (`import bcm4360_phy_watchdog is not implemented`, 33347
accesses). Every access is identical to the object up to access 21177. The
first transmit-power update (attach/up, channel 1, `wlc_phy_txpower_limit_set`
-> merge -> `wlc_update_txppr_offset`) is identical, including the per-rate
offsets the MAC derives from the SROM decode (48 and 54 Mbit/s). From the
second update on (access 21178, channel 6) the MAC writes different values to
`SHM(0x5d4..0x5de)` and to the rate table (Q1). `ab.py all`: 0 differences.

How the results below were obtained: only from the comparison tests. Some
runs used temporary instrumentation in the open code (extra SHM writes of
values, extra service calls on a scratch ppr, deliberately changed results)
so that the difference listing showed values; all of it is removed again.

## Q1 (most important). `bcm4360_mac_update_txppr_offset` sees an undefined value

The MAC's reaction to `wlapi_high_update_txppr_offset` depends on something the
open code does not pass. Observed:

* With the open code exactly as it is, the session differs from the second
  update on (channel 6): `SHM(0x5d4)`, `SHM(0x5d6)` = 3 instead of 1, and the
  rate-table offsets of 48/54 Mbit/s 0 instead of 2/4 (half dB).
* The result changes with code layout alone: two builds with the same
  semantics (the merge calling the exported `bcm4360_phy_txpower_sromlimit`
  instead of the internal function) gave different MAC writes.
* Filling the stack with a non-zero byte (0x01, 0x7f, 0xff) immediately
  before the call of `bcm4360_mac_update_txppr_offset` in
  `bcm4360_phy_txpower_recalc_target_acphy` makes **all three 2.4 GHz
  channels identical** (first difference then at access 24548, the first
  5 GHz channel); filling it with 0 gives the failing result again. Moving the
  fill earlier (before `ppr_force_disabled`, before `ppr_get_max`) has the
  same effect; moving it after the call has none.

So the MAC (or the mapping of the service) reads a value that is not an
argument of the open prototype - a third argument of
`wlapi_high_update_txppr_offset` / `wlc_update_txppr_offset` (a flag that
`sub_09949f` passes non-zero?), or an argument the hybrid mapping does not
forward. acphy-txpower.md section 17 step 2 gives only `(sh+0x20, pi+0x1c8)`.
Please check the object's call in `sub_09949f` and the service mapping. I did
not change the service (it belongs to the earlier task and the mapping is the
coordinator's).

## Q2. The ppr selectors (the spec gives names but no values)

`acphy-api-txpower.md` lists `ppr_set_dsss/ofdm/vht_mcs(ppr, m, rateset, bw,
...)` but not the values the decode passes. Found with the services
themselves (scratch ppr objects, sentinel buffers):

* Group sizes: DSSS 4 bytes, OFDM 8, VHT 10 (a 24 byte buffer: exactly that
  many bytes written by the getters).
* First selector = bandwidth variant: a 20 MHz ppr accepts only 0; a 40 MHz
  ppr 0, 1, 3; an 80 MHz ppr 0..5. Read as 0 = 20, 1 = 40, 2 = 80, 3 = 20in40,
  4 = 20in80, 5 = 40in80 (the spec's step 1 selector order).
* DSSS: (bw, chains 1..3).
* OFDM: (bw, mode, chains 1..3); modes 0, 1, 2, 4 all address the same group,
  mode 3 its own (TXBF): groups SISO, CDD2, CDD3, TXBF2, TXBF3.
* VHT: (bw, nss, mode, chains); valid: nss 1 {mode 0/1 chain, mode 2 (CDD)
  2/3 chains, mode 3 (TXBF) 2/3}; nss 2 {mode 0 2/3, mode 1 (STBC) 2/3, mode
  3 2/3}; nss 3 {mode 0 3, mode 3 3}. Out-of-range nss/mode values crash
  `ppr_get_vht_mcs` (unmapped read in sub_16472e).
* A new ppr is all disabled (-128). `ppr_compare_min(a, b)` with `b` disabled
  disables `a`; with `a` disabled and `b` enabled it takes `b`.

The open code names these selectors (`enum ppr_txbw`, `PPR_MODE_*`). Please
confirm them against the object's calls in `apply_srom11` and its helpers.

## Q3. `wlc_phy_txpwr_apply_srom11`: what I implemented and what is guessed

The spec fixes only the step (maxpwr - 2 * nibble) and the inputs. Implemented:

* For every bandwidth variant the ppr holds (20; 40: 20, 40, 20in40; 80: all
  six), every DSSS/OFDM/MCS group (all chain counts and modes) gets the same
  powers. **Guess**: no per-chain or per-mode reduction. Observed: writing only
  the SISO groups or all groups, with or without a -12 (3 dB) reduction for
  two chains, gives the same session (Q1 masks everything after channel 1).
* 2.4 GHz: DSSS from `cckbw202gpo` (20) and `cckbw20ul2gpo` (20in40), 4
  nibbles; OFDM from the words at `pi+0xc50/0xc54/0xc58` (20, 20in40, 40), 8
  nibbles, nibble k = OFDM rate k. **Confirmed** for 20 MHz SISO OFDM by the
  test: the MAC's rate-table offsets of 48/54 Mbit/s (nibbles 2 and 4 of
  `dot11agofdmhrbw202gpo` = 0x4200) match only with this.
* MCS from `pi+0xc5c/0xc60/0xc64` (20, 20in40, 40), nibble k = MCS k.
* 5 GHz: OFDM from copy groups 0..5 and MCS from groups 6..11 of
  `pi+0xc6c + 12 * group + 4 * range` in the order 20, 20in40, 20in80, 40,
  40in80, 80 (acphy-attach.md's copy table read that way). **Doubtful**: with
  the non-zero fill of Q1 the first 5 GHz channel (36, 20 MHz) shows the
  object's OFDM basic rates all at offset 0 while the open code still has the
  2.4 GHz offsets of 48/54 Mbit/s in the offsets ppr (see Q4).
* MCS 8 and 9 get the power of MCS 7 (**guess**; the words have 8 nibbles).
* Not implemented: the sub-band corrections (`sb20in40*`, `sb20in80and160*`,
  `sb40and80*`, `mcslr5g*`, `dot11agdup*`, the `pdoffset` words, gated by
  `pi_ac+0x34d`). They are 0 in the model's SROM and the flag is 0, so the
  test cannot show them, and the spec does not say how they apply.

The model's SROM (read back through the test): `cckbw202gpo` = 0,
`cckbw20ul2gpo` = 0, `dot11agofdmhrbw202gpo` = 0x4200, `ofdmlrbw202gpo` = 0,
`mcsbw202gpo` = `mcsbw402gpo` = 0x86420000, all `mcsbw?05g?po` = 0xba768600,
all sub-band words 0, `maxp2ga0/1` = 0x4c, `maxp5ga0/1` = 0x48, core 2: 0xff.

## Q4. Is the offsets ppr `pi+0x1c8` cleared between merges?

Spec step 3 recreates it only when the bandwidth changes; step 10 accumulates
with `ppr_compare_max`, so it never decreases. With the Q1 fill, at channel 36
(20 MHz, same bandwidth as the 2.4 GHz channels) the open code's rate-table
offsets still show the 2.4 GHz values of 48/54 Mbit/s; the object's are 0.
Clearing it (`ppr_clear`) at step 3 makes `SHM(0x5d4)` match there but other
values not. The spec lists `ppr_clear` as an observed operation but uses it
nowhere. Where does the object call it?

## Q5. `sub_0b72a4` (the recalculation tick) is not specified

`limit_set` and `txpower_set` call it after the merge, the watchdog (step 12)
uses its result. No specification describes it. Written as: no access, no
service, result false (the session shows no access at the first call). I
declared it in phy.h (`bcm4360_phy_txpower_recalc_tick`) for the watchdog task.

## Q6. Smaller points

* `get_target_max`/`_min`: the spec says "the cores in sh+0xa7 (txchain)", but
  sh+0xa7 is `rxchain` in acphy-attach.md (`txchain` is sh+0xa6). I used
  sh+0xa7 (`phy->rxchain`).
* `wlc_phy_get_est_pout`: which of the two estimates (section 12: `out1` from
  `PHY(0x642)`, `out2` from `PHY(0x640)`) goes to `est_pout` and which to
  `est_pout_adj`, and what `est_pout_cck` gets, is not specified. Written:
  `est_pout` = `out2`, `est_pout_adj` = `out1`, `est_pout_cck` = 0. Not called
  in the session.
* `wlc_phy_get_olpc_pwroffset(pi, bw)`: is `bw` the chanspec bandwidth bits or
  a 0/1/2 (or 20/40/80) value? Written for the chanspec bits; anything else
  gives 0. Not called in the session.
* `wlc_phy_txpower_set`: "if the MAC is enabled" was written as a read of
  `D11(0x120)` bit 0 (as other PHY code does). Not called in the session.
* `sub_099528` (`core_offset_set_acphy`) now returns 0 or -2 and accepts index
  N (acphy-attach.md, "Function pointer table"); the earlier code rejected
  index N and returned nothing.

## Round 2 (2026-11-01): the analyst's corrections applied

Code changed: `open/phy/phy_txpower.c` (SROM-11 decode, wrapper, merge,
`get_est_pout`, Q5 comment), `open/include/bcm4360/phy.h`,
`open/include/bcm4360/phy_env.h` (comments of the `ppr` services only, no new
service); for the watchdog answers `open/phy/phy_wd.c`, `open/phy/phy_rx.c`,
`open/phy/phy_desense.c` (C4 through the common request),
`open/phy/phy_cal.c` (the hand-made `D11(0x124)` write removed, it is now in
the request), `open/phy/phy_init.c` (field rename), `open/phy/phy_api.c`
(`wlc_phy_cal_init` sets the start values of the histories).

Test state: `hybrid.py check --on phy` 41416 / 41416 accesses, **the same**
(was different from access 24548 on); `hybrid.py check` the same; `ab.py all`
0 differences in every scenario; build without warnings.

Remaining points (none of them changes the tests):

* **R1. `est_pout_cck` is one byte behind a `u16 *`.** Q6 says the object
  writes one byte to `*est_pout_cck`; the open prototype (phy_api.h, called
  by `hybrid/phy.c`) has `u16 *`. Written as one byte through the pointer
  (`*(u8 *)est_pout_cck = 0`), prototype kept. Is the MAC's variable really
  16 bits (then its high byte stays as it was), or should the open prototype
  become `u8 *`? `get_est_pout` is not called in the session.
* **R2. Which copy of the 5 GHz MCS words feeds which group.** Q3 lists both
  copies per bandwidth as read (`0xc6c+4i` and `0xcb4+4i`, `0xc90`/`0xcd8`,
  `0xca8`/`0xcf0`) but not which one OFDM8 and which one VHT10 takes. They
  hold the same value after attach, so it does not matter for the result;
  written as OFDM8 from the first, VHT10 from the second.
* **R3. `sub_0baff6` step 5 (`pi+0xc21` set).** Not implemented: `pi+0xc21` is
  not kept in `phy.h`, nothing in `open/` sets it, and `wlc_phy_noise_cb`
  would need `wlapi_noise_cb` (bit 1), which is not a service in
  `phy_env.h`. What sets `pi+0xc21` (an iovar?), and should
  `wlapi_noise_cb(physhim, chan, value)` become a service?
* **R4. `wlc_phy_noise_sample_intr` and `wlc_phy_noise_cb`.** Q3 of the
  watchdog answers says the callback stores the value in `sh+0x96[sh+0xa0]`
  and advances `sh+0xa0` only if `chan` = `pi+0x194` (bit 0), and calls
  `wlapi_noise_cb` for bit 1. The receive group's `bcm4360_phy_noise_sample_intr`
  reads the result, always advances the window and now clears bits 0..2;
  the channel test and the MAC callback are not there (`acphy-api-rx.md` does
  not describe them). Not reached in the session (no noise interrupt).
* **R5. `pi_ac+0x41`.** acphy-desense.md "Data" calls it "run the calibration
  on every noise sample", the watchdog answers "the last crsmincal request
  was refused". Both describe the same use in C3; the field is now
  `crsmincal.noise_refused`, set and cleared only by the request.
* **R6. `wlc_phy_cal_init` step 5.** The start values of the six histories
  (acphy-cal-tx.md section 8, steps 3 and 4) are now set; the
  calibration-init callback `pi+0x30` (`sub_08e77a`) is still not called
  (unchanged; no access is visible for it).
