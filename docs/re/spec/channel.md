# Chanspecs and the validity of channels

The chanspec helpers of `bcmwifi_channels.c` and the validity checks of
channels and chanspecs of `wlc_channel.c` (the "channel manager"). With the
open PHY, these are the largest block of Broadcom code the driver still
executes: the upper layer asks "is this chanspec valid here?" for every
channel it considers, at every channel change, scan, power limit update and
rate decision. Measured over the whole hybrid session (`tools/re/hybrid.py`),
the functions below are about 1.7 % of all execution: one third of what is
still Broadcom's once the PHY is open.

Written by the coordinating session from the disassembly (the functions are
short); checked by running the object in the emulator (see "Verification").

## Scope

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wf_chspec_malformed` | 0x00c706 | 202 | original |
| `wf_chspec_ctlchan` | 0x00c94c | 126 | original |
| `wf_chspec_ctlchspec` | 0x00c9ca | 52 | original |
| `wf_chspec_primary40_chspec` | 0x00cad3 | 78 | original |
| `wf_chspec_80` | 0x00cbdb | 79 | original |
| `wf_channel2mhz` | 0x00cba2 | 57 | original |
| `wlc_valid_channel20_in_band` | 0x06eb5d | 47 | original |
| `wlc_valid_channel20` | 0x06eb8c | 56 | original |
| `wlc_valid_channel20_db` | 0x06ebc4 | 97 | original |
| `wlc_valid_40chanspec_in_band` | 0x06ec25 | 45 | original |
| `wlc_valid_80chanspec_in_band` | 0x06ec52 | 46 | original |
| `wlc_valid_chanspec` | 0x0700b8 | 16 | original |
| `wlc_valid_chanspec_db` | 0x06fda7 | 19 | original |
| `sub_06faf3` | 0x06faf3 | 692 | stripped: "valid chanspec, extended" (V6) |
| `sub_06fa51` | 0x06fa51 | 162 | stripped: "the database has limits for the chanspec" (V7) |

None of them accesses the hardware, allocates memory or writes anything
outside its own stack. The two stripped functions are reached only through
`wlc_valid_chanspec` and `wlc_valid_chanspec_db` (and V6 from itself).

Not in scope (other functions of the same files, not run often): the
string conversions `wf_chspec_aton`/`wf_chspec_ntoa`, `wf_chspec_valid`,
`wf_channel2chspec`, `wf_mhz2channel` (no callers in the object), the field
accessors `wlc_channel_locale_flags*`, `wlc_valid_chanvec_get`,
`wlc_quiet_chanvec_get`, and everything that *builds* the channel manager's
state (country setting, locale lookup, power limits).

## The chanspec

A chanspec is a 16-bit value:

| Bits | Mask | Field | Values |
|---|---|---|---|
| 0-7 | 0x00ff | channel | the channel number; for 40, 80 and 160 MHz the *centre* channel; for 80+80 MHz two 4-bit segment indices (bits 0-3 and 4-7) |
| 8-10 | 0x0700 | sideband | position of the 20 MHz control channel inside the wide channel, counted from the lowest 20 MHz part (0 = lowest) |
| 11-13 | 0x3800 | bandwidth | 0x1000 = 20 MHz, 0x1800 = 40, 0x2000 = 80, 0x2800 = 160, 0x3000 = 80+80 MHz (0x0000, 0x0800 and 0x3800 also occur in the tables below) |
| 14-15 | 0xc000 | band | 0x0000 = 2.4 GHz, 0xc000 = 5 GHz (0x4000, 0x8000 are not used) |

Below, `band`, `bw`, `sb` and `ch` are `cs & 0xc000`, `cs & 0x3800`,
`cs & 0x0700` and `cs & 0x00ff`. "5 GHz" means `band == 0xc000`.

## Data

### C1. The 20 MHz channel table

52 entries (channel, flags), used by the 40 MHz check (V6 step 4). The flag
bits carry the names of the same table in the open-source brcmsmac driver:
0x01 `CH_UPPER_SB` (a 40 MHz channel may have this channel as its lower half,
the upper half above it), 0x02 `CH_LOWER_SB` (the other way round), 0x04
`CH_EWA_VALID`.

| Channels | Flags |
|---|---|
| 1, 2, 3, 4 | 0x05 |
| 5, 6, 7, 8, 9 | 0x07 |
| 10, 11 | 0x06 |
| 12, 13, 14 | 0x02 |
| 34 | 0x01 |
| 38, 42, 46 | 0x02 |
| 36, 44, 52, 60, 100, 108, 116, 124, 132, 140, 149, 157 | 0x05 |
| 40, 48, 56, 64, 104, 112, 120, 128, 136, 153, 161 | 0x06 |
| 144, 165 | 0x02 |
| 184, 192, 200, 208 | 0x01 |
| 188, 196, 204, 212, 216 | 0x02 |

In the object the entries are in this order: 1..14, 34, 38, 42, 46, 36, 40,
44, 48, 52, 56, 60, 64, 100, 104, ..., 140, 144, 149, 153, 157, 161, 165,
184, 188, ..., 216. No channel appears twice and channel 0 does not appear.

### C2. The 80 MHz centre channels

42, 58, 106, 122, 138, 155 (the 80 MHz channels of the 5 GHz band that V6
accepts).

### C3. Tables of `wf_chspec_ctlchan`

* **Bandwidth in MHz by bandwidth field** (`bw >> 11`, 0..7): 5, 10, 20, 40,
  80, 160, 160, 0. (Field 7 is not read from a table: the object uses 0.)
* **80+80 centre channels by segment index** (0..15): 42, 58, 106, 122, 138,
  155, 50, 114, then 5, 10, 20, 40, 80, 160, 160, 0. The object indexes a
  table of 8 bytes with a 4-bit index; indices 8..15 read the bytes that
  follow it in the object, which are the bandwidth table above. An open
  implementation reproduces them as the 16 values listed.

### C4. What the validity checks read

The checks take Broadcom's channel manager (`wlc_cm_info`, called `cm` here)
and read, through it, a few fields of the driver's main structure (`wlc`) and
of its public part (`pub`, the `struct wlc_pub` of `src/wl/sys/wlc_pub.h`).
Only these are read:

| Input | Where in Broadcom's structures | Meaning |
|---|---|---|
| `bandunit` | `wlc->band` (`wlc+0x40`, pointer), its `+0x04` (u32) | the current band: 0 = 2.4 GHz, 1 = 5 GHz |
| `bandlocked` | `wlc+0x24e` (u8) | set by `wlc_bandlock`: the driver is held in one band |
| `valid[b]` | `cm+0x29+b*0x30`, 28 bytes | valid 20 MHz channels of band `b`: bit `c & 7` of byte `c >> 3` is channel `c` (channels 0..223) |
| `locale_flags[b]` | `cm+0x28+b*0x30` (u8) | restriction flags of the locale for band `b`; bits 0x04, 0x08, 0x10 are read |
| `bw_cap[b]` | `wlc->bandstate[b]` (`wlc+0x50+b*8`, pointer), its `+0xf1` (u8) | bandwidths band `b` may use: bit 0x02 = 40 MHz, 0x04 = 80 MHz |
| `bw40_capable` | `pub+0x6d` (`phy_bw40_capable`, u8) | |
| `bw80_capable` | `pub+0x6e` (`phy_bw80_capable`, u8) | |
| `vht_enab` | `pub+0x6a` (`_vht_enab`, u8) | |
| `vht_features` | `pub+0xf0` (`vht_features`, u16); the object reads its high byte | bit 0x100 is required for 80 MHz |
| `country` | `cm+0x18` (u32) | the country of the regulatory database (`clm_country_t`) |

`cm+0x08` is `wlc`, `wlc+0x00` is `pub`, and `wlc+0x1a0` is `cm` again (the
driver has one channel manager; some checks reach it through `wlc`, which
gives the same structure).

## Procedures: chanspec helpers (pure functions)

### H1. `wf_chspec_malformed(cs)` -> bool

True when `cs` is not a well-formed chanspec:

1. 2.4 GHz (`band == 0`): `bw` must be 20 or 40 MHz, else malformed. Then the
   sideband test (step 4). The channel number is not checked.
2. 5 GHz (`band == 0xc000`):
   * 80+80 MHz (`bw == 0x3000`): let `lo = cs & 0xf`, `hi = (cs >> 4) & 0xf`.
     Malformed if `hi > 5` or `lo > 5`; otherwise malformed exactly when
     `hi <= lo`. The sideband is not checked; the function ends here.
   * 20, 40, 80 or 160 MHz: malformed if `ch > 224`; otherwise the sideband
     test (step 4).
   * any other bandwidth: malformed.
3. Any other band (0x4000, 0x8000): malformed.
4. Sideband test: 20 MHz: malformed unless `sb == 0`; 40 MHz: malformed if
   `sb > 0x100`; 80 MHz: malformed if `sb > 0x300`; 160 MHz: never malformed.

### H2. `wf_chspec_ctlchan(cs)` -> u8

The control channel.

1. 20 MHz: `ch`.
2. Otherwise let `s = sb >> 8` (0..7).
   * 80+80 MHz: if `s <= 3` the segment index is `cs & 0xf`; else it is
     `(cs >> 4) & 0xf` and `s` becomes `s - 4`. `centre` = C3 80+80 table at
     that index, `W` = 80.
   * any other bandwidth: `centre = ch`, `W` = C3 bandwidth table at
     `bw >> 11`.
3. Result: the low 8 bits of `centre + 4 * s - D`, where `D` is the low 8
   bits of `(W - 20) / 10` computed as an **unsigned 32-bit** division. So
   `D` = 2, 6, 14 for 40, 80, 160 MHz, 6 for 80+80; for the bandwidth fields
   0 and 1 (5, 10 MHz) `D` = 152, for field 7 (`W` = 0) `D` = 151.

### H3. `wf_chspec_ctlchspec(cs)` -> chanspec

The 20 MHz chanspec of the control channel: `cs` itself for 20 MHz,
otherwise `(cs & 0xc000) | 0x1000 | wf_chspec_ctlchan(cs)`.

### H4. `wf_chspec_primary40_chspec(cs)` -> chanspec

For an 80 MHz chanspec, the 40 MHz chanspec that holds its control channel;
any other chanspec is returned unchanged. With `bw == 0x2000`:

| `sb` | centre `c` | sideband `s` |
|---|---|---|
| 0x200 | `ch + 4` | 0x000 |
| 0x300 | `ch + 4` | 0x100 |
| any other | `ch - 4` | `sb` (unchanged) |

Result: the low 16 bits of `s | c | 0xd800` (bitwise OR, `c` computed in 32
bits: `ch - 4` below 0 wraps).

### H5. `wf_chspec_80(centre, primary)` -> chanspec

`centre` and `primary` are 8-bit. For `s` = 0x000, 0x100, 0x200, 0x300 in
this order: `cs = centre | s | 0xe000` (5 GHz, 80 MHz); if
`wf_chspec_ctlchan(cs) == primary`, return `cs`. If none matches: 0x00ff.

### H6. `wf_channel2mhz(channel, start_factor)` -> int

Both arguments 32-bit unsigned.

1. If `start_factor == 4814` (2.4 GHz): `channel` must be 1..14 (else -1);
   channel 14 gives 2484.
2. Otherwise: `channel > 200` gives -1.
3. Result `start_factor / 2 + channel * 5` (unsigned arithmetic, returned
   as int).

## Procedures: validity

The flags results are 0 or 1.

### V1. `wlc_valid_channel20_in_band(cm, b, channel)`

`channel < 224` and bit `channel` of `valid[b]` is set. (`b` is 0 or 1 at
every caller.)

### V2. `wlc_valid_channel20(cm, channel)`

V1 for the current band: `valid20_in_band(bandunit, channel)`.

### V3. `wlc_valid_channel20_db(cm, channel)` ("dual band")

1. If V2 says valid: 1.
2. Else, if `bandlocked`: 0.
3. Else V1 in the other band (band 1 if `bandunit != 1`, else band 0).

### V4. `wlc_valid_40chanspec_in_band(cm, b)`

1 when `locale_flags[b] & 0x18` is 0 and bit 0x02 of `bw_cap[b]` is set.

### V5. `wlc_valid_80chanspec_in_band(cm, b)`

1 when `locale_flags[b] & 0x14` is 0 and bit 0x04 of `bw_cap[b]` is set.

### V6. Valid chanspec: `sub_06faf3(cm, cs, dualband)`

`wlc_valid_chanspec(cm, cs)` is V6 with `dualband` = 0,
`wlc_valid_chanspec_db(cm, cs)` with `dualband` = 1. Below, "valid20(x)"
means V3 for channel `x` when `dualband` is set, V2 otherwise. Note that V2
looks at the *current* band, not at the band of `cs`.

1. If H1 says `cs` is malformed: 0.
2. The band must fit the channel: 5 GHz exactly when `ch > 14`, else 0.
3. 20 MHz: valid20(`ch`).
4. 40 MHz:
   1. 0 unless `bw40_capable`.
   2. 0 unless V4 for the band of `cs` (`b` = 1 for 5 GHz, 0 for 2.4 GHz).
   3. `lo` = `ch - 2` if `ch > 2`, else 0; `hi` = `ch + 2` if `ch < 222`,
      else 0. 0 unless valid20(`lo`) and valid20(`hi`) (in this order; the
      second is not asked when the first fails).
   4. 0 unless V7 for `cs`.
   5. The flags of the entry for channel `lo` in table C1 (0 when there is
      none): 1 when `flags & 0x05 == 0x05`, else 0.
5. 80 MHz:
   1. 0 unless 5 GHz, `bw80_capable`, bit 0x04 of `bw_cap[1]`, `vht_enab`
      and bit 0x100 of `vht_features`.
   2. 0 unless V5 for band 1.
   3. 0 unless `ch` is one of the centre channels of C2.
   4. 0 unless V7 for `cs`.
   5. 1 when V6 says valid for both 40 MHz halves, `(ch - 4) | 0xd800` and
      then `(ch + 4) | 0xd800` (5 GHz, 40 MHz, sideband 0), with the same
      `dualband`; else 0. The second is not asked when the first fails.
6. Any other bandwidth (160, 80+80 MHz): 0.

### V7. The database has limits for the chanspec: `sub_06fa51(cm, cs)`

Asks the regulatory database (clm.md) for the power limits of the chanspec
and reports whether it has them; the limits themselves are thrown away.

1. Read `country`.
2. `clm_limits_params_init(&p)`; if the result is not 0: 0.
3. `clm_country_def(country, &loc)`; if the result is not 0: 0.
4. `p.bw` = 1 for 40 MHz, 2 for 80 MHz, else 0 (the other fields as step 2
   left them).
5. `clm_limits(&loc, band, ch, 0, 0, &p, limits)` with `band` = 1 for 5 GHz,
   0 for 2.4 GHz, antenna gain 0, limits type 0, and a buffer of 336 bytes
   for the limits: 1 when the result is 0, else 0.

## Verification

`tools/re/chan_scenarios.py` (run as `python hybrid.py chan`) runs the
object alone and the hybrid core to the same state (attach, up) and calls
the functions of the scope by their names on both, with the same arguments,
comparing the results:

* H1-H4 for all 65,536 values, H5 for every (centre, primary) pair, H6 for
  every channel 0..255 with the start factors 4814, 10000 and 0;
* V1-V7 (through the named entry points) for every channel 0..255 of both
  bands, every bandwidth and sideband combination that matters, in many
  states: nine countries set with `wlc_set_countrycode` (each with its own
  valid channels and locale flags), both current bands, band lock on and
  off, and variations of the capability fields of C4.

A Python model written from the text of this specification (the
coordinator's check of it, not part of the repository) gives the object's
result for every one of these calls: 2,459,676 calls in 261 states, 0
differences. The hybrid session (`hybrid.py check`) and the long soak
(`phy_soak.py`) also run these functions thousands of times in context.
