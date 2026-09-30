# Questions from implementing phy-cal

Implementer notes for the analysts. Format: what the spec says, what the test
shows, what I did.

## Status

`python ab.py run phy-cal` is at **0 differences** (141130 accesses, 4 stages:
tempsense and `wlc_phy_cals_acphy` on both models), and `python ab.py all` stays
green. Specifically:

* **`wlc_phy_tempsense_acphy`**: matches access-for-access. (Earlier there was a
  `D11(0x120)` artifact, see 1; it does not appear in the scenario as run because
  tempsense is called with the MAC already suspended.)
* **`wlc_phy_cals_acphy(pi, 0)` phase 0**: the common part, `sub_0925cc`, the
  pre-cal gain, **both `sub_0abc76` (transmit IQ/LO cal) calls**, the `0xacdc`
  marker, `wlc_phy_scanroam_cache_cal_acphy` and the **receive IQ cal
  `sub_0addfa`** (full: save/override, radio loopback, gain search, tone loop,
  regression, restore) all match access-for-access.

So the whole calibration path - transmit cal, receive cal, the tone/sample
player, temperature sense and the scheduler - is complete. Items 8-11 below
record spec corrections found while finishing the receive cal.

## 1. `D11(0x120)` in tempsense's gpiosel cannot match (harness limit)

- Spec: `wlc_phy_tempsense_acphy` runs between `wlapi_suspend_mac_and_wait` +
  `wlc_phyreg_enter` and `wlc_phyreg_exit` + `wlapi_enable_mac`; the sample
  setup `sub_093ebe` clears bits 14..15 of `D11(0x120)`.
- Test: the object reads/writes `D11(0x120) = 0x44020402`; the open code
  reads/writes `0x40020403` (differ in bit 26 "wake" and bit 0). Everything
  before and after matches, and the service calls (`mac_suspend`, the ucode
  wake override, ...) are in the same order on both sides.
- Cause: tempsense **suspends the MAC and takes the wake override itself**
  (inside the compared stage). In the object these services really modify
  `D11(0x120)` in the model; in the open run they are replayed by
  `ab.service_env` as black boxes that only return the recorded result and do
  **not** apply the register effect, and the stage's `ab.restore` does not carry
  it either. So the model's `D11(0x120)` diverges before gpiosel reads it. The
  same `sub_093ebe` gpiosel is exercised by the idle-TSSI inside `wlc_phy_init`
  and matches there, because in that case the suspend/wake happened in the
  caller *before* the compared stage (baked into the restored state). This looks
  like an inherent limit of comparing a function that suspends/wakes the MAC
  itself; the analysts may want `0x120` in the test's `RAW_D11` set, or the
  scenario to call tempsense with the MAC already suspended.

## 2. tempsense `RADIO(0x0e)` phase mods (spec bit assignment is off)

- Spec (`acphy-cal-rx` step 6b): `mod(RADIO(0x0e), 0x2, A[p]*2)` then
  `mod(RADIO(0x0e), 0x4, B[p])`, describing the second as "clears bit 2".
- Test: the second mod actually sets bit 2 to `B[p]`. The object's sequence,
  from a read of `0x0001`, is `A`-mod -> `0x0003` (bit1 = A[p]), `B`-mod ->
  bit2 = B[p]. B = {0,0,1,1} so bit 2 is set in phases 2 and 3, not cleared.
- What I did: `mod(RADIO(0x0e), 0x2, A[p]<<1)`, `mod(RADIO(0x0e), 0x4, B[p]<<2)`.

## 3. tx-cal loopback "core-select" is really the tempsense W distribution

- Spec (`acphy-cal-tx` section 15 A.6): a list of `mod(0x73a/0x725/0x739, ...,
  (c>>k)&m)` "encoding the core number c".
- Test: the object writes the **bandwidth config word W = 0xd5eb** (the
  tempsense step-1 value at 20 MHz) into `0x739/0x73a/0x725`, bit-for-bit the
  same ten mods as tempsense step 3 - not the core number.
- What I did: replaced the "core-select" block with the shared W distribution
  (`phy_distribute_w(io, o, tempsense_w(phy))`). At other bandwidths the tx-cal
  W was not observed; I assume it is the tempsense W there too.

## 4. tx-cal `PHY(0x19e)` bracket (A.5 / A.7)

- Spec: A.5 saves `PHY(0x19e)`, then "Table access bracket (set bit 1) held over
  the loop"; A.7 says "restore `PHY(0x19e)` bit 1" after the ADC pulse.
- Test: A.5 does an extra `PHY(0x19e)` read (the save-for-the-bracket in a full
  `tbl_bracket_enter`, on top of the save into `pi_ac+0x8c`), and A.7 restores
  bit 1 to the **saved** value (clears it), i.e. it leaves the bracket - the
  loop then relies on the per-`sub_09bf99` brackets.
- What I did: A.5 saves `0x19e`/`0x40f`, then `tbl_bracket_enter`; A.7 does
  `tbl_bracket_leave` after the pulse.

## 5. `sub_09bf99` brackets unconditionally

- Test: the coefficient accessor enters/leaves the table bracket (`PHY(0x19e)`
  bit 1) for **every** selection, including the in-memory cal-state selections
  12..19 (which make no table access). Bracketing only the table selections
  left the object with extra `0x19e` read/writes per command.
- What I did: `phy_cal_coeffs` always `tbl_bracket_enter` / `tbl_bracket_leave`.

## 6. `sub_09c66c` order; `wlc_phy_cordic`; gain LUT / LOFT tables interleave

- tx-gain save/set (`sub_09c66c`): the object reads the three gain codes, then
  **writes** them, then reads/writes the bbmult (each via
  `sub_098751`/`sub_09c4e4`, which bracket). Reading the bbmult between the gain
  reads and writes diverged.
- `wlc_phy_cordic` (unspecified, `acphy-cal-tx` section 13): the AC-PHY seeds the
  cosine register (`out[1]` = 0x9b75) and returns **I = sine, Q = cosine** with
  magnitude ~2^16; the tone scaling divides by **2^16**, not 2^15 as the section
  12 text says. Confirmed from the tone samples (`sample[0]` = I 0, Q amp;
  `sample[5]` at 90 deg = I amp, Q 0). Derived the 18-entry arctan table from
  `atan(2^-i)` scaled to 0x1680000/turn (not taken from the object).
- The gain LUT (`sub_09c91d`, `TBL(0x0c)[0..0x11]` and `[0x20..0x31]`) and the
  LOFT tables (`wlc_phy_populate_tx_loft_comp_tbl_acphy`, `TBL(0x42)/0x62`) are
  written **interleaved by index** (A[i] then B[i]; core0[i] then core1[i]), not
  as one bulk transfer per table.

## 7. Shared helpers were `static`; scheduler fields

- `sub_09c4e4`/`sub_098751` (bbmult), `sub_09bf99` (0..11), `sub_092ffa`,
  `sub_093ebe`, `sub_09bbe4`/`sub_09be13`, `wlc_phy_classifier_acphy`,
  `wlc_phy_force_rfseq_acphy` and `wlc_phyreg_enter/exit` are all `static` in
  `phy_txpower.c`/`phy_init.c`. The task forbade broader edits to finished
  `.c` files, so I reimplemented the small ones as file-local statics in
  `phy_cal.c` (copied verbatim). Cleaner would be to export them.
- The tone reconciliation (`wlc_phy_tx_tone_acphy`/`wlc_phy_stopplayback_acphy`)
  is done: the full functions live in `phy_cal.c` and `phy_txpower.c`'s
  idle-TSSI (`phy_poll_samps_war`) now calls them; the amplitude-0 reconstruction
  was removed.
- `sub_0baff6` (the crsmincal noise request called by `wlc_phy_cals_acphy`'s
  common part) is out of scope for `acphy-desense` and stubbed there to only
  clear the result words; the object also triggers `D11(0x124) |= 0x10`. I
  reproduce that trigger in `phy_cal.c` after the desense stub returns.
- Added to `struct bcm4360_phy`: the calibration state block
  (`struct bcm4360_phy_cal_state`), the tone bbmult save (`pi_ac+0x02/0x0a`), the
  scan/roam cache, `measured_temp` (`pi_ac+0x8dc`). `pi+0xf86` (per-phase collect
  mask) is 0 here, so `pi_ac+0x44c` is not implemented. `sh+0x34` (free time) has
  no open source and is stored but never read into a compared access.

## 8. Receive IQ calibration `sub_0addfa` - complete; step-7 save/restore order

The receive cal now matches access-for-access. The step-7 save/override
(`sub_09cf53` on restore) is **not** a simple ascending range - both order and
structure matter:

- **Structure**: the object does one *save loop over all cores*, then the
  once-only `PHY(0x401)` setup, then one *override loop over all cores* - not
  save+override interleaved per core. `PHY(0x40f)` (clear bit 9) is saved once
  *before* the save loop; `PHY(0x401)` (`mod 0x7 = sh+0xa5`, `mod 0x7000 = 0`)
  is set *between* the two loops. The whole thing is inside one `PHY(0x19e)`
  bit-1 bracket entered before `0x40f` and left after the override loop.
- **Save order** (26 registers): 25 are read in the order
  `0x720..0x729, 0x732,0x733, 0x730,0x731, 0x734,0x735, 0x737,0x738, 0x736,
  0x739, 0x73a,0x73b, 0x73c,0x73d, 0x747`, then the bbmult (`sub_098751`,
  `TBL(0x0c)[0x63+4c]`, bracketed) and the gain code (`TBL(7)[0x100/0x103/0x106+c]`,
  **unbracketed** - it inherits the outer bracket), and **`0x73e` is read last**,
  after the gain code. The gain code is read via a non-bracketing table accessor
  while the bbmult uses the bracketing one.
- **Restore order** (`sub_09cf53`): `0x73e` first, then `0x678`, then the 25 in
  the same order **except `0x726`/`0x727` are swapped** (`...0x725, 0x727, 0x726,
  0x728...`). Per core it also (a) rewrites the saved gain code directly
  (unbracketed), (b) calls `wlc_phy_txpwr_by_index_acphy(1<<c, saved_index)` (the
  saved tx index, here 0x40), (c) calls `sub_09c4e4` to set the saved bbmult, all
  before the register writes; then `PHY(0x40f)` restore, `force_rfseq(2)`, bracket
  leave.
- **Coefficients ARE 0** in the fixed model. The earlier note that the object
  wrote non-zero `0x48/0xee` was from a *different* model state; with the current
  `bcm4360.py` (done bit cleared, accumulators 0) every `rx_iq_est` returns 0, so
  the step-12 regression gives `angle=0, gain=0x400`, `cordic(0)=(0, ~2^16)`, and
  `a = ((0x400*0)>>15 +1)>>1 = 0`, `b = ((0x400*2^16)>>15 +1)>>1 & 0x3ff = 0`. The
  object writes `PHY(0x6a0)=0x6a1=0x8a0=0x8a1=0` and the open code matches.
  (Verified: `a` uses the sine branch `w0`, `b` the cosine branch `w1`.)

## 9. `sub_0ad89a` gain search: `paldo`, gain table, tone frequency

- **`paldo = 0`** on this board (2.4 GHz, radio rev 4), not 6. The spec's
  "`paldo = 6` for radio rev in {2,3,4,...}" gives the wrong `PHY(0x730)`: the
  object writes `0x730 = (seed<<6)|(paldo<<3) = 0x80` with `seed=2`
  (`(PHY(0x6dc)=0x14a & 0x780)>>7`), which forces `paldo=0`. I use
  `paldo = is5g ? 6 : 0` and flag the radio-rev clause as suspect.
- **2.4 GHz gain table** `DAT_0054db90` (11 x `{s3,s7,s5}`), read from the
  `PHY(0x734)=(s3<<3)|s7` writes and the `txpwr_by_index` argument: `s7` climbs
  0..5 then `s3` climbs 0..5 (`{0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{1,5},{2,5},
  {3,5},{4,5},{5,5}`), and **`s5 = 10` (constant)** - every gain index programs
  `wlc_phy_txpwr_by_index_acphy(1<<c, 10)` (never the `s5==0xff` unity path). The
  5 GHz table `DAT_0054dbe0` was not observed.
- **Tone frequency is 1000**, not 4000. The gain-search tone and the two cal
  tones are `wlc_phy_tx_tone_acphy(pi, +/-1000, 0xb5, ...)`; the spec's
  "F = 4000 for 20 MHz" and tone list "{+8,-8}" do not hold here (hooking the
  object's `wlc_phy_tx_tone_acphy` shows arg1 = 0x3e8 / -0x3e8). Since the model's
  powers are 0 the tone *value* does not affect the coefficients, only the tone
  *frequency* passed to the player.
- The search opens its own `PHY(0x19e)` bracket, reads both cores' seeds
  (`PHY(0x6dc + 0x200c)`) inside it, then `sub_09868f` (see 11), holds the bracket
  through all ~12 passes and their tone measurements, and leaves it at the end.

## 10. `sub_094b1a` radio loopback: exact 2.4 GHz override

The 2.4 GHz override after zeroing `RADIO(0x20..0x3d)` is ten mods, on registers
`0x22`/`0x3a`/`0x20` (5 GHz: `0x23`/`0x3d`/`0x21`), in this order:
`mod(0x22,0x100,0)`, `mod(0x3a,0x10,0)`, `mod(0x22,0x20,0)`, `mod(0x22,0x4,0)`
(mask-4 = 0 for rev 4), `mod(0x20,0x100,0)`, `mod(0x20,0x4,0x4)`,
`mod(0x22,0x200,0x200)` (loopback enable), `mod(0x22,0x80,0x80)`,
`mod(0x20,0x3,0)`, `mod(0x22,0x6,0)`. (The masks that only clear bits from a
zero register are not individually observable; the set/keep ones - `0x4`, `0x200`,
`0x80`, and the `0x3`/`0x6` low-mixer clears - are.)

## 11. `sub_09868f` primes a **48-bit** table entry

- Test: `sub_09868f(pi, ..., 0x32)` reads `TBL(0x20)[0x32]` as **one 48-bit
  entry** (`R tbl.20 00032/1 = 30`), not 16-bit. Reading it as 16-bit was the
  last remaining difference in the whole scenario.
- What I did: `bcm4360_tbl_read(io, 0x20, 1, 0x32, 0x30, buf)` with a 6-byte
  buffer. The value is discarded (it primes a default gain word that the zero-power
  search never uses); only the access width matters for the comparison.
- `TBL(0x0e)` (the receive-cal tone/sample table) uses 32-bit entries as before;
  the tempsense/`sub_098751` gain words are 16-bit. So table entry widths on this
  path are 16 / 32 / 48 bits depending on the table.
