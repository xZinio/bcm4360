# Task `bmac-runtime`: the MAC core at run time - energy detect, FIFO suspend, CCA, rate offsets, TSF

Specification: [`../spec/bmac-run.md`](../spec/bmac-run.md), sections **R1**
(`wlc_bmac_ifsctl_edcrs_set` with `sub_06212d` and `sub_0622d9`), **R2**
(`wlc_bmac_tx_fifo_suspend`, `_resume`, `_suspended`), **R5**
(`cca_stats_read`, `sub_061ad3`, `wlc_bmac_cca_stats_read`), **R6**
(`wlc_bmac_rate_shm_offset` with the table `rate_info`,
`wlc_bmac_stf_get_rateset_shm_offset`, `_stf_set_rateset_shm_offset`), and
from **R7** only `wlc_bmac_read_tsf`. Its "Data" section (constants, fields)
applies; for the MAC primitives you call: `../spec/bmac-init.md` A3, A4, A6,
A7, A8. Register and shared-memory access: `open/hw/access.c`
(`bcm4360_shm_read`/`_write`, `d11_read*`/`d11_write*`).

Interface: `open/include/bcm4360/mac.h` (given: the prototypes under "At run
time" and the note on the rate set layout) and
`open/include/bcm4360/mac_env.h` (the services, the block "at run time").
Write **`open/mac/bmac_runtime.c`**.

## Why

The eighth MAC increment: the MAC functions Broadcom's core runs constantly -
the energy detect after every channel change, the FIFO suspend/resume of the
mute, the CCA counters at every watchdog tick, the rate offsets at every
channel change (132 calls in the short session), the TSF. With this task they
become open code inside the real driver (hybrid build, switch `open_mac`,
adapter `hybrid/mac_runtime.c`, already written).

## The functions

| Open function | Broadcom's | Section |
|---|---|---|
| `bcm4360_bmac_ifsctl_edcrs_set` | `wlc_bmac_ifsctl_edcrs_set`, with `sub_06212d` and `sub_0622d9` as `static` helpers | R1 |
| `bcm4360_bmac_tx_fifo_suspend`, `_resume`, `_suspended` | the three of R2 | R2 |
| `bcm4360_bmac_cca_stats_read` | `cca_stats_read`, with `sub_061ad3` as a `static` helper | R5 |
| `bcm4360_bmac_cca_stats_get` | `wlc_bmac_cca_stats_read` (the read, result 0) | R5 |
| `bcm4360_bmac_rate_shm_offset` | `wlc_bmac_rate_shm_offset` | R6 |
| `bcm4360_bmac_stf_get_rateset_shm_offset`, `_set_` | the two of R6 | R6 |
| `bcm4360_bmac_read_tsf` | `wlc_bmac_read_tsf` | R7 |

Call the open MAC primitives where the specification calls them:
`wlc_ucode_wake_override_set/clear` -> `bcm4360_bmac_wake_override_set/clear`,
`wlc_bmac_suspend_mac_and_wait` -> `bcm4360_bmac_suspend`,
`wlc_bmac_enable_mac` -> `bcm4360_bmac_enable`, `wlc_bmac_read_shm` /
`wlc_bmac_write_shm` -> the access layer, and your own functions
(`cca_stats_read` uses your `bcm4360_bmac_read_tsf`).

## Rules for this code

* Clean room: `../implementing.md`, binding.
* Outside the 802.11 core's registers and the shared memory: the services of
  `mac_env.h` - `bcm4360_bmac_env_phy_scramb_dyn_bw_en` for R1 step 4, the
  DMA slots 0x20 / 0x28 / 0x30 of R2 (`bcm4360_bmac_env_dma_txsuspend`,
  `_txresume`, `_txsuspended`; the FIFOs that have an engine are the bits of
  `dma_fifos`).
* **The PHY service can change the state** (the platform writes `*mac` back
  before it and reads it after): do not keep fields in locals across it.
* State: `suspended_fifos` (R2) is a field; write it as the specification
  does. `chanspec`, `band_phytype`, `band_phyrev`, `mac_rev`, `dma_fifos` are
  inputs.
* `rate_info` (R6) is a table of facts of the specification: write it as a C
  table, with the values the specification lists for the indices it lists
  (0..127). For a rate above 127 the specification has an open question (Q3):
  treat it as "not in the table" and say so in a comment.
* The rate set and its entries (R6) are the upper layer's: the adapter passes
  the rates and the count, and the entries as bytes in the layout of the
  specification (`mac.h`). Write exactly the bytes R6 writes (byte +1 of an
  entry is never written).
* Card and revision: MAC 42, AC-PHY, chip 0x4360. Branches for other PHY
  types and MAC revisions: a one-line comment, no code - except where the
  specification gives the branch condition on `mac_rev` for this function
  (R1 `sub_0622d9` for revisions below 40, R5 the base 0x6c0 below 40): test
  the condition as written.

## Tests

From `tools/re` (Docker, Linux tools; Docker Desktop is running; the adapter
`hybrid/mac_runtime.c` is linked automatically as soon as
`open/mac/bmac_runtime.c` exists; use `-e
HYBRID_OUT=/repo/re-out/hybrid-bmacruntime`):

* `python ab.py build` (no warnings).
* `python hybrid.py check` must report **the same**; likewise `check --off
  mac`, `check --on phy`, `check --on phy --off mac`, and `srom`.
* `python phy_soak.py --on=` - the long session with Broadcom's PHY and the
  open MAC (`--on=` switches nothing on; the MAC is open by default): it runs
  the energy detect at every bandwidth and sideband, the FIFO suspend and
  resume of the mute, the CCA counters at every tick. It must report **the
  same**, as it does before this task (588,350 accesses, 2026-11-02).
* `python ab.py all` must still pass (`AB_OUT=/repo/re-out/open-bmacruntime`).

`wlc_bmac_tx_fifo_suspended` and the re-read of `sub_061ad3` (high word
changed) are not reached by the sessions: write them from the specification.

Do not edit `hybrid/` or `tools/`, nor any file of `open/` except your own.
Questions into `docs/re/questions/bmac-runtime.md`; finish with a short
report.
