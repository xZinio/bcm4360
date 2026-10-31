# Task `phy-api-rx`: the receive and measurement entry points of the PHY API

Part of completing the PHY API so the open PHY can take over Broadcom's inside
the module (`hybrid/README.md`). This group is what the MAC calls on the PHY for
received frames and link measurements: RSSI from a receive header, the noise
average and noise-sample path, interference RSSI, deaf mode, and the receive
chain / antenna-diversity setters and getters.

Specification: [`../spec/acphy-api-rx.md`](../spec/acphy-api-rx.md) (written by
the receive analyst). The entry points and their open names are in the table of
[`../spec/acphy-api.md`](../spec/acphy-api.md), section "Receive, measurements,
chains". Some of the machinery (tempsense, the crsmin noise request) is already
in `open/phy/phy_cal.c` / `phy_desense.c`; the spec says what is new and what to
reuse.

Read first: `open/include/bcm4360/phy_api.h` (keep the prototypes exactly),
`open/include/bcm4360/phy.h` (the `rssi` state and the per-sub-band correction
fields), and `open/include/bcm4360/access.h` (`bcm4360_shm_read` and the PHY/
radio access the RSSI and noise code uses).

## What to write

Add `open/phy/phy_rx.c` (and `phy.h` for new state) implementing from
acphy-api-rx.md: `bcm4360_phy_rssi_compute`, `_noise_avg`,
`_noise_sample_intr`, `_noise_sample_request_external`, `_interf_rssi_update`,
`_set_deaf`, `_stf_chain_set`, `_stf_chain_active_get`, `_ant_rxdiv_set`,
`_ant_rxdiv_get`, `_swdiv_ant_get`, `_swdiv_ant_set`. The receive-header field
offsets the RSSI code reads are in the spec; keep the arithmetic exact
(signedness, rounding, clamping, the combination over chains). Any new service
of the MAC the spec names goes into `open/include/bcm4360/phy_env.h` with a
`bcm4360_*` prototype.

Rules: `docs/re/implementing.md` (clean-room, binding). C11, kernel style, SPDX,
a comment per function naming the spec section. No floating point.

## Tests

From `tools/re`: `python ab.py build`, then `python hybrid.py check --on phy`
(the session reaches the receive path through the watchdog and, for RSSI, a
synthetic receive header the harness can feed — see the spec's verification
section). Keep it identical to the object as far as it runs. Questions into
`docs/re/questions/phy-api-rx.md`; finish with the report.
