# Task `phy-api-control`: the control entry points of the PHY API

The whole AC-PHY is reproduced in `open/` and matches the object for the
bring-up session. To let it take over Broadcom's PHY inside the module
(`hybrid/README.md`), the open PHY must answer all 79 functions through which
the MAC layer reaches the PHY. Most are done; this task writes the **control**
group — the small stores, the getters, and the orchestration functions that do
not belong to the transmit-power, receive or watchdog groups.

Specification: [`../spec/acphy-api.md`](../spec/acphy-api.md), sections A1 to
A15 and the tables of the entry points. Everything you need for this task is in
that one document and in the headers below; the register notation it uses is
[`../spec/access.md`](../spec/access.md), and where it refers to
[`../spec/acphy-cal-tx.md`](../spec/acphy-cal-tx.md) sections 3–8 (for
`cal_perical` and `cal_init`) read those sections too.

Read first: `open/include/bcm4360/phy_api.h` (the prototypes — keep the names
and arguments exactly; `hybrid/phy.c` calls them), `open/include/bcm4360/phy.h`
(the PHY state; most fields these functions touch already exist, their object
place is in the comments), and `open/phy/phy_attach.c` (how the state is made,
for the fields you add).

## What to write

Add `open/phy/phy_api.c` (control group) implementing, from acphy-api.md:

* **A2** `bcm4360_phy_attach_band` — the `wlc_phy_attach` call for the second
  band (shared PHY: core reset + refcount, as the section gives it).
* **A3** the stores and no-ops: `bcm4360_phy_get_encore`,
  `bcm4360_phy_preamble_override_get`/`_set`, `bcm4360_phy_btclock_war`,
  `bcm4360_phy_ofdm_rateset_war`, `bcm4360_phy_chanspec_ch14_widefilter_set`,
  `bcm4360_phy_bf_preempt_enable`, `bcm4360_phy_initcal_enable`,
  `bcm4360_phy_set_filt_war`, `bcm4360_phy_tkip_rifs_war`,
  `bcm4360_phy_antsel_init`, `bcm4360_phy_freqtrack_start`/`_end`,
  `bcm4360_phy_interference_set`, `bcm4360_phy_acimode_noisemode_reset`
  (all no-ops or single stores on the AC-PHY — see the A3 table).
* **A4** `bcm4360_phy_chanspec_band_firstch`, `_band_validch` — the channel
  list (the 67 entries are in A4; put them in one static table).
* **A5** `bcm4360_phy_block_bbpll_change` (acphy-chanspec section 12).
* **A6** `bcm4360_phy_get_femctrl_bt_wlan_ovrd`, `_set_femctrl_bt_wlan_ovrd`.
* **A7** `bcm4360_phy_get_ofdm_rate_lookup` (the 8-byte table in A7).
* **A8** `bcm4360_phy_bssinit`.
* **A9** `bcm4360_phy_cal_perical` (acphy-cal-tx 3–7) and `bcm4360_phy_cal_init`
  (acphy-cal-tx 8). The multi-phase timer uses the services
  `bcm4360_timer_add`/`_del` of `phy_env.h`.
* **A10** `bcm4360_phy_ioctl`, **A11** `bcm4360_phy_iovar_dispatch` (answer the
  three iovars the MAC uses as the table says; the rest return -23 for now —
  this is the one deliberate difference from the object, noted in A11).
* **A12–A15** `bcm4360_phy_down`, `_hold_upd`, `_mute_upd`,
  `_antsel_type_set`.

New state fields A1–A15 name (the A10 override modes `sh+0x8c`/`0x90`, any the
spec says `phy.h` lacks): add them to the right struct in `phy.h` with the
type, initial value and the procedure that sets them, in the style of the
fields already there.

Rules: `docs/re/implementing.md` (the clean-room rules are binding — work from
the specification only). C11, kernel style, SPDX line, a comment on each
function naming the acphy-api.md section. No floating point.

## Tests

From `tools/re`: `python ab.py build` (no warnings), then
`python hybrid.py check --on phy`. As each function lands the session gets
further before it stops at the next missing one; the accesses until the stop
must stay identical to the object. When the txpower and rx and watchdog groups
are also done, `--on phy` runs the whole session with 0 differences.

Write questions into `docs/re/questions/phy-api-control.md` and finish with the
report the guide asks for.
