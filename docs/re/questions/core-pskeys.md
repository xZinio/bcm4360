# Questions of the implementer: task `core-pskeys` (open/core/keys.c, ps.c, tx.c, rx.c, wlchw.c, drv/mac80211.c)

Result: `frames_check.py` (receive, transmit, status, ampdu, keys, ps): same; `standalone.py check`: pass;
`standalone.py undefined`: 0; `make open` links without warnings. Keys and power save are checked against the
spec only (the object's key and PM code cannot be called from outside). The receive test's expectation for
"decrypt error" changed: RxStatus1 bits 3+4 now give `BCM4360_RX_DECRYPT_ERR` (0x80) instead of no flag.

## Choices and guesses

1. PM bit (ps-keys.md Q1): mac80211 runs in `PS_NULLFUNC_STACK` mode (it sends the null frames and parses the TIM;
   `SUPPORTS_DYNAMIC_PS` is NOT set, so mac80211's own timer does the dynamic power save and PM_FAST / the PM2 timer /
   PS-Poll are not in the core). mac80211 sends the PM=1 null before it reports CONF_PS; the core therefore sets HPS
   when a null data frame with PM=1 to the BSSID is posted (object order: HPS, then the frame), so the frame is
   right whether or not the microcode overrides the host's PM bit. The core never sets the PM bit.
2. The watchdog's repeat of the null frame (1.5) is replaced: a change that is not completed after 5 ticks is given up
   (HPS and WAKE off). Cannot resend mac80211's frame. Unverified on the card.
3. `ps.c` beacon part: TBTT check (sub_025fb0) and alignment run only while HPS is set. The rule "n += o when
   (o >> 10) >= wlc+0x290 - 1" (wlc-hw 5.3, "Read") is implemented as written with `dtimnoslp`; with 0 it always
   applies. Air time before the timestamp: 192/96/20 us + 24 header bytes at the rate (HT/VHT: 6 Mb/s). Guess.
4. Beacon loss (1.8): timeout = max(2 s, 10 beacon intervals x DTIM) is a guess (the spec says only "at least 2000 ms");
   WAKE is held from half of it to the full time.
5. `wlc_ps_allowed` condition 4 (WPA without group key): the core cannot see the security word, so the driver reports
   "a key was set since the association" and "a group key was set" (`bcm4360_core_key_state`), including keys mac80211
   keeps in software.
6. Keys: TKIP group keys, WEP, BIP, GCMP, CCMP-256 and every key with `IEEE80211_KEY_FLAG_RX_MGMT` go to software
   (-EOPNOTSUPP). The core itself accepts WEP (entries 0..3, host flag 1 bit 14) but the driver does not use it.
7. TKIP: mac80211 does the Michael MIC (`GENERATE_MMIC`); the core's MIC engine and the MIC key words are not used
   (ps-keys.md Q3: capability bit 31 unknown), so `MacTxControlLow` 0x2000 is never set, RxStatus2 bits 3/4 are
   ignored (`pk.hw_mic` false) and FrameLen adds 4 (ICV) only. The MIC keys are kept in the table for later; the order
   would be transmit = mac80211 key[16..23], receive = key[24..31] (the spec's "bytes 24..31 / 16..23" is Broadcom's
   layout; wpa_supplicant swaps for a station).
8. TKIP receive phase-1: set_key gives the key for IV32 0 (`ieee80211_get_tkip_rx_p1k`); after a decrypted frame
   with IV16 0xffff the driver installs the key for IV32 + 1 (`bcm4360_tkip_rx_next`), because mac80211 calls
   `update_tkip_key` only for frames it decrypted itself. The update writes the seven words only, not the whole entry
   with the AMT clear/rewrite (ps-keys.md 3). A frame under a newer IV32 before the update fails in the core and is
   dropped.
9. FrameLen and the PLCP length of a hardware-keyed frame include the ICV/MIC (4 or 8): frames.md 3.6 says the PLCP
   uses FrameLen. Not checked against a real encrypted frame.
10. A frame with `hw_key` that has no key entry, no Protected bit or no room for its IV is refused (-1, the platform
    drops it): never sent in the clear.
11. Wait for wake (bmac-init A5) added to `set_wake_ctrl` (40 us, then SHM(0x40) while 4, at most fastpwrup_dly): the
    TODO of wlchw.c. Not exercised by STEPS.
12. BlockAckReq (ampdu.md 2.4): the driver sends one per aggregate that has an unacknowledged MPDU (SSN = last MPDU
    + 1), without the window/outstanding bookkeeping of sub_04fdf8/sub_0500a5 and without the "50 in a row" limit.
13. Protected management frames: never marked decrypted by the core (key flag RX_MGMT excludes them from the
    hardware, ps-keys.md Q4 open).

## Not covered by tests

Driver code (drv/) is compile-checked only. The TBTT alignment numbers, the beacon-loss timing, the PS abort, and
`ieee80211_send_bar` need the card.
