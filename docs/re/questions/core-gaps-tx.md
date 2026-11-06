# Questions of the implementer: task `core-gaps-tx` (G1, G5, G9 transmit rows)

Files: `open/core/txpwr.c` (G1), `open/core/chains.c` (G5), `tx.c` (power field, pending counts,
drain hook), `core.h` / `include/bcm4360/core.h` (declarations, `bcm4360_core_set_chains`),
`tools/re/frames_check.py` (`power`, `chains`; transmit part unmasked).
Results: `frames_check.py` all same (`power`: 14,991 calls of the object's
`wlc_stf_spatial_expansion_get`, `wlc_stf_get_pwrperrate`, `wlc_acphy_txctl1_calc`,
`wlc_stf_txchain_get` compared, 0 differences; the 22 scan frames now compare the power field too;
`chains`: 13 steps + the deferral against `wlc_stf_txchain_set` / `wlc_stf_rxchain_set` /
`wlc_txfifo_complete`: results, stf block, every register access, same);
`standalone.py check` pass, `undefined` 0, `make open` links without warnings.

## What the Linux side must call (G5)

`bcm4360_core_set_chains(c, txmask, rxmask)` (0 = leave that side), while up (-1 otherwise),
under the usual lock; returns 0 or -29 (refused: empty or outside the hardware's chains).
* `IEEE80211_CONF_CHANGE_SMPS` in `config`: static SM power save -> `rxmask` 1, off/dynamic -> the full
  `info.rxchain`. mac80211 sends the SMPS action frame itself; the order the spec describes for the
  association case (action frame first, PHY after its acknowledgement when reducing) is NOT
  reproduced: the core changes the PHY at once. `txmask` 0.
* `set_antenna` (cfg80211, optional): both masks.
* Temperature reduction (wlc-hw.md 3 step 16, `wlchw.c` `stf_tempsense_upd`) and the watchdog's
  restore must use requester 4: `core_stf_txchain_set(c, mask, false, 4)`; `env_stf.c`'s
  `bcm4360_stf_env_txchain_set` requester 3, force: `core_stf_txchain_set(c, mask, true, 3)`.
  The platform's own calls use requester 0.
* Not done by the core: `wlc_vht_update_cap` (G5 step 5) and the rate-selection re-initialisation: the
  platform re-advertises the HT/VHT capabilities itself (a change of the mask changes
  `hw->wiphy` stream counts; mac80211 reads them at registration only), minstrel_ht adapts by itself.
* Frames in flight: the core now keeps the FIFOs' pending counts (`wlc->core+0x38+2*fifo`: +1 at
  `bcm4360_core_tx`, -1 per reported status); a change that removes a chain waits (flag `wlc+0x304`
  bit 5, as the spec) and completes in the status path when the sum is 0
  (`core_txfifo_complete`, also completes the deferred spatial policy, bit 6, which nothing did before).

## For wlchw.c / env_stf.c / core.c (not mine)

1. `core_w_stf_txchain_set` (wlchw.c) is a shortcut; replace its body by
   `core_stf_txchain_set(c, mask, false, 4)` (chains.c implements G5 including the completion).
   Same bytes and accesses as the object were verified.
2. The requester bytes `stf+0x5a..0x5e` must be 0xff after attach (the object has 0xff, 3 for some
   after use); chains.c treats a 0 byte as 0xff, so it works either way.
3. `env_stf.c` `bcm4360_stf_env_valid_rate` returns true; the object's `wlc_valid_rate(beacon rate 0)` is false in
   the model, so every spatial-policy completion of the core also does a MAC suspend / enable pair
   (`D11(0x120)` write, `D11(0x128)` poll) around two stubs. The chains check filters those accesses
   (D11 0x120/0x128, SHM 0x40) from both sides. Return false while `wlc+0x584` is 0.
4. `wlc+0x518` (the chanspec of `txctl1`) is read by `core_txctl1_calc` as the spec says; it is the
   word `bcm4360_stf_env_chanspec` reads too. It must be current (set_chanspec).

## Guesses and gaps

1. G1 and the txcore override: the spec text (G1 "the core mask of the rate's class") and
   `frames.md` 3.5 leave it open whether `wlc_stf_txcore_get` looks at `txcore_override`. The test
   shows it does NOT (object: `txchain_get` ignores the override bytes `stf+0x22..`; the policy
   has folded them into `txcore[].mask`). `tx.c`'s `tx_corecount_mask` (core mask of PHY control
   word 0) followed my earlier reading "the override wins" (core-frames Q6); changed to the mask
   alone. Not distinguishable in the card model (overrides 0 there); the object's function agrees now
   (via `txchain_get`; word 0 itself not compared for non-zero overrides).
2. A legacy rate that is neither CCK nor OFDM with the STBC bit: the object's expansion class is
   nsts + 1 with nsts 2 (it adds the STBC stream also to legacy rates); the spec says nsts is 1 for
   legacy. Does not occur (no such rate is sent); the test leaves these out. pwrperrate returns 0
   for them as the spec says.
3. `nss` = 4 HT rates (MCS 24..31) and a BF/non-STBC rate of 4 streams: no group; the object crashes
   in `ppr_get_vht_mcs`, the core returns 0. Test limited to MCS 0..23 and 32. Three rates (BF with
   an invalid chain count in a random mask) made the object read outside its table; skipped in the test.
4. Beamforming (`RSPEC_BF`): `bcm4360_txrate` has no beamforming flag; the header never sets it
   (`pub+0xe8` is 1 in the model but nothing is advertised). The BF paths of G1 are compared with the
   object only.
5. G5 step 4: "unless `force`, restore the requester's byte; clear those overrides ..." does not
   say what is returned in the not-forced case. Implemented: restore the byte and refuse (-29).
   The fixed-rate override words `band+0x50`, `band+0x54` are zero in the core (the platform chooses
   rates), so this branch never runs; untested.
6. G5 step 3 (`sub_173b37`): which value is written to `band+0x59` when STBC is allowed is not stated; I keep the current band's byte
   (clear it when not allowed) and set / clear bit 7 of `wlc+0x381` and `wlc+0x71b`. Not compared
   (the object's `band+0x59` is not read by the core elsewhere, only `wlc_up` reads `stf+0x59`).
   Nothing in the open core reads these capability copies; the platform's STBC capability is mac80211's.
7. G5 step 4 "the antenna word (`sub_173a21`, bmac-run R8)" is not specified beyond its name: the
   core relies on the spatial policy's step 4.7 (`phytxant` for the basic class and
   `wlc_bmac_txant_set`); the stf bytes and accesses agree with the object, so nothing is missing in
   the model state. `stf+0x08` (txant) = 3 is set as written.
8. `wlc_txbf_txchain_upd` / `wlc_txbf_rxchain_upd` (G8, "not specified"): `rxchain_upd` writes
   `SHM(2b+0xe4)` = streams - 1 (as the spec says; same accesses as the object, with a re-read of
   `SHM(0xb0)` first: the object keeps the base from `wlc_init`). `txchain_upd` writes nothing in the
   model (capability on, no links): I first wrote the two chain words of wlc-hw.md 1.4 step 11 and the
   object does not; removed. What it does for links is G8's.
9. `wlc_stf_rxchain_set`: the one-stream transition (SM power save mode stored in the primary
   configuration `+0x568`, the HT capability's SM power-save field, the action frame and the
   delayed PHY change of `sub_045018`) is not reproduced; PHY and block words only. In the model
   the object's accesses for 1<->2 streams are equal to the core's without it.
10. The MAC-suspend pair around the beacon update (see 3 above) is the only register-access
    difference in the policy completion; everything else is identical.
