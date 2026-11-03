# hybrid/ - open code inside the shipped driver

Up to version 1.0.3, `bcm4360.ko` ran Broadcom's core (`wlc_hybrid.o_shipped`)
for everything below the Linux glue. The open hardware layer in `open/` was
only ever run in the emulator. The hybrid build changes that: **open code
takes over functions of Broadcom's core inside the module**, so the driver runs
less closed code, and the open code meets real hardware.

## How it works

Broadcom's object calls almost all of its named functions through relocations
(30,197 of 34,040 call sites), so a call can be sent elsewhere at link time
without touching a byte of the object's code:

1. `scripts/mkhybrid.sh` makes a copy of the object in which each function
   named `take` in [`takeover.txt`](takeover.txt) is a *weak* global symbol,
   and adds the alias `bcm4360_blob_NAME` for Broadcom's own body. Code and
   data are copied unchanged; only symbol bindings change. It runs where the
   module is built (DKMS) and needs `nm` and `objcopy`.
2. The adapters here define those functions with the same names and
   arguments. At link time the strong (open) definition wins: every call of
   the object to that function arrives in the adapter.
3. The adapter calls the open code (`open/`). Each layer has a switch (a
   module parameter); switched off, or for a chip the open code does not know,
   the adapter calls `bcm4360_blob_NAME`, Broadcom's own function.

The open code runs on Broadcom's OS layer (`src/shared/linux_osl.c`): the
same `osl_*` functions the object uses. In the emulator the same functions are
provided by `tools/re`, so the code that runs in the module is the code that
was compared there.

## What is taken over

| Layer | Switch (module parameter) | Default | Broadcom functions taken over | Open code | Specification |
|---|---|---|---|---|---|
| cards | - | - | `si_attach`, `si_detach` (wrapped: note the BAR0 window, then Broadcom's) | `card.c` | - |
| PMU, clocks | `open_pmu` | on | 16: `si_pmu_init`, `si_pmu_pll_init`, `si_pmu_res_init`, `si_pmu_measure_alpclk`, the clock queries, OTP power, radio supply, the indirect PMU registers | `pmu.c` -> `open/chip/pmu.c` | [pmu](../docs/re/spec/pmu.md) |
| register access | `open_access` | on | 15: `phy_reg_read/write/mod/and/or`, `read/write/mod/and/or/xor_radio_reg`, `wlc_phy_table_write/read_acphy`, `wlc_bmac_read/write_shm` | `access.c` -> `open/hw/access.c` | [access](../docs/re/spec/access.md) |
| MAC control | `open_mac` | on | 6: `wlc_bmac_mctrl`, `wlc_ucode_wake_override_set/clear`, `wlc_bmac_suspend_mac_and_wait`, `wlc_bmac_enable_mac`, `wlc_bmac_mhf` | `mac.c` -> `open/mac/bmac_ctrl.c`, `bmac_mhf.c` | [bmac-init](../docs/re/spec/bmac-init.md) A1-A9 |
| MAC object memories | `open_mac` | on | 13: `wlc_bmac_set_shm`, `wlc_bmac_copyto/copyfrom_objmem`, `wlc_bmac_write_amt`, `wlc_bmac_set_rcmta`, `wlc_bmac_write_template_ram`, `wlc_bmac_write_ihr`, `wlc_bmac_write_inits`, `wlc_bmac_set_cwmin/cwmax`, `wlc_bmac_retrylimit_upd`, `wlc_bmac_set_hw_etheraddr`, `wlc_bmac_hw_etheraddr` | `mac_mem.c` -> `open/mac/bmac_mem.c` | [bmac-init](../docs/re/spec/bmac-init.md) A8, B4-B6, B11, E1, E3, E7, F1, F2 |
| MAC clocks, core reset, crystal | `open_mac` | on | 16: `wlc_bmac_corereset`, `wlc_bmac_phy_reset`, `wlc_bmac_phyclk_fgc`, `wlc_bmac_macphyclk_set`, `wlc_bmac_core_phy_clk`, `wlc_bmac_core_phypll_ctl/reset`, `wlc_coredisable`, `wlc_bmac_xtal`, `wlc_bmac_pllreq`, `wlc_bmac_set_clk`, `wlc_bmac_set_ctrl_bt_shd0/SROM`, `wlc_bmac_radio_read_hwdisabled`, `wlc_hw_deviceremoved`, `wlc_bmac_btc_mode_get` | `mac_clk.c` -> `open/mac/bmac_clk.c` | [bmac-init](../docs/re/spec/bmac-init.md) A10-A17, C7, C9, C11, C13, C14 |
| MAC initialisation | `open_mac` | on | 7: `wlc_bmac_init` (microcode download and start, initialisation lists, buffer memory controller, band init), `wlc_bmac_wowlucode_start`, `wlc_bmac_switch_macfreq`, `wlc_bmac_set_extlna_pwrsave_shmem`, `wlc_bmac_txbw_update`, `wlc_bmac_band_stf_ss_set`, `wlc_bmac_set_shortslot` (microcode and lists stay Broadcom's data) | `mac_init.c` -> `open/mac/bmac_init.c` | [bmac-init](../docs/re/spec/bmac-init.md) B1-B12, E6, E10 |
| MAC up and down | `open_mac` | on | 8: `wlc_bmac_hw_up` (one-time preparation after power-up, LED GPIOs), `wlc_bmac_up_prep`, `wlc_bmac_up_finish`, `wlc_bmac_down_prep`, `wlc_bmac_down_finish`, `wlc_bmac_hw_down`, `wlc_bmac_reset` (with the flush of the DMA queues), `wlc_bmac_validate_chip_access` (the PCIe bridge, the 4360 PCIe work-around and the IFS energy-detect setting they call stay Broadcom's) | `mac_updown.c` -> `open/mac/bmac_updown.c` | [bmac-init](../docs/re/spec/bmac-init.md) C2-C6, C8, C10, C15, D3 |
| MAC channel and band | `open_mac` | on | 5: `wlc_bmac_set_chanspec` (with the band switch, which now runs the open band initialisation), `wlc_setxband`, `wlc_bmac_bw_set`, `wlc_bmac_bw_reset`, `wlc_bmac_mute` (the transmit FIFO suspend/resume they call stay Broadcom's) | `mac_chan.c` -> `open/mac/bmac_chan.c` | [acphy-chanspec](../docs/re/spec/acphy-chanspec.md) 1-3, [bmac-init](../docs/re/spec/bmac-init.md) E4 |
| MAC at run time | `open_mac` | on | 10: `wlc_bmac_ifsctl_edcrs_set` (energy detect, every bandwidth), `wlc_bmac_tx_fifo_suspend/resume/suspended`, `cca_stats_read`, `wlc_bmac_cca_stats_read`, `wlc_bmac_rate_shm_offset`, `wlc_bmac_stf_get/set_rateset_shm_offset`, `wlc_bmac_read_tsf` | `mac_runtime.c` -> `open/mac/bmac_runtime.c` | [bmac-run](../docs/re/spec/bmac-run.md) R1, R2, R5, R6, R7 |
| MAC Bluetooth coexistence | `open_mac` | on | 6: `wlc_bmac_btc_mode_set`, `wlc_bmac_btc_wire_set/get`, `wlc_bmac_set_btswitch`, `wlc_bmac_update_bt_chanspec` (the channel told to the Bluetooth side at every channel change), `wlc_bmac_seci_upd` | `mac_btc.c` -> `open/mac/bmac_btc.c` | [bmac-run](../docs/re/spec/bmac-run.md) R3 |
| backplane | `open_si` | on | 34 `si_*`: core selection, register access of any core, the AXI wrapper (core flags, reset, disable), GPIO, chip helpers (`si_attach` and the PCIe bridge stay Broadcom's) | `si.c` -> `open/chip/si.c` | [chip](../docs/re/spec/chip.md) |
| DMA engines | `open_dma` | on | `dma_attach` returns an open engine whose function table (49 slots) is the adapter's; `dma_addrwidth` | `dma.c` -> `open/mac/dma.c` | [bmac-data](../docs/re/spec/bmac-data.md) section 1 |
| per-rate power container | `open_ppr` | on | all 50 `ppr_*` functions of `wlc_ppr.c` (the open PHY's transmit power reaches them too) | `ppr.c` -> `open/lib/ratepwr.c` | [ppr](../docs/re/spec/ppr.md) |
| board variables from SROM/OTP | `open_srom` | on | `srom_var_init` (PCI: SROM read and CRC, OTP and NVRAM fall-backs, the revision-11 conversion, the variable buffer), `srom_read`; the backplane, OTP and NVRAM functions they call stay Broadcom's | `srom.c` -> `open/chip/srom.c` | [srom-otp](../docs/re/spec/srom-otp.md) |
| chanspecs, channel validity | `open_chan` | on | 13: the chanspec helpers of `bcmwifi_channels.c` (`wf_chspec_malformed`, `wf_chspec_ctlchan`, `wf_chspec_ctlchspec`, `wf_chspec_primary40_chspec`, `wf_chspec_80`, `wf_channel2mhz`) and the validity checks of `wlc_channel.c` (`wlc_valid_chanspec`, `wlc_valid_chanspec_db`, `wlc_valid_channel20*`, `wlc_valid_40/80chanspec_in_band`); the channel manager's state and the database stay Broadcom's/the CLM layer's | `chan.c` -> `open/lib/channel.c` | [channel](../docs/re/spec/channel.md) |
| driver variables (iovars) | `open_iovar` | on | `wlc_iovar_op`: the lookup of a variable in the 64 modules' tables and the dispatch to the module (the request check, the BSS configurations and the modules' handlers stay Broadcom's) | `iovar.c` -> `open/lib/iovar.c` | [iovar](../docs/re/spec/iovar.md) |
| rate sets | `open_rate` | on | 10 helpers of `wlc_rate.c`: `wlc_rateset_default`, `wlc_rateset_filter`, `wlc_rate_hwrs_filter_sort_validate`, `wlc_rateset_ofdm_fixup`, the MCS and VHT map updates, `wlc_rateset_copy`, `wlc_rate_rspec_reference_rate` | `rate.c` -> `open/lib/rate.c` | [rate](../docs/re/spec/rate.md) |
| channel statistics | `open_cca` | on | the whole of `wlc_cca.c`: attach and detach, the module's watchdog, down function and iovar `cca_get_stats`, `cca_stats_upd`, `cca_stats_tsf_upd`, `cca_query_stats`, `wlc_cca_get_chanspec` (read when the module is attached) | `cca.c` -> `open/lib/cca.c` | [cca-stf](../docs/re/spec/cca-stf.md) part 1 |
| transmit power offsets of the chains | `open_stf` | on | `wlc_update_txppr_offset` (wlc_stf.c: after every computation of the PHY's power targets: the per-rate offsets into the driver's container, the chains that cannot reach the minimum, the spatial policy of the band, the legacy rates' offsets in the microcode's table), with its callback and the band's spatial policy; Broadcom's reads the minimum power from a stack word it never writes, the open code uses 0 | `stf.c` -> `open/lib/stf.c` | [cca-stf](../docs/re/spec/cca-stf.md) part 2 |
| PHY and radio | `open_phy` | **off** | all 79 entry points of the MAC into the PHY (takeover.txt) | `phy.c` -> `open/phy/*` (+ `open/hw/access.c`) | [acphy-api](../docs/re/spec/acphy-api.md) and the `acphy-*` specs |
| regulatory database | `open_clm` | on | 16 `clm_*` entry points of `wlc_clm.c` (the database stays Broadcom's data) | `clm.c` -> `open/lib/clm.c` | [clm](../docs/re/spec/clm.md) |
| utilities | `open_utils` | on | 8: `getvar`, `getintvar`, `getintvararray`, `getintvararraysize`, `bcm_strtoul`, `bcm_bitcount`, `hndcrc8`, and Broadcom's PHY's own lookup `phy_getvar` (the global NVRAM, `nvram_get`, stays Broadcom's) | `utils.c` -> `open/lib/utils.c` | [utils](../docs/re/spec/utils.md) |

The switches are read when a card is attached (`open_access`, `open_mac` and
`open_utils` at every call; all are read-only once the module is loaded). To
run a layer on Broadcom's code again, e.g. the PMU:

```sh
echo "options bcm4360 open_pmu=0" | sudo tee /etc/modprobe.d/bcm4360-open.conf
sudo rmmod bcm4360; sudo modprobe bcm4360; sudo systemctl restart wpa_supplicant
```

`make HYBRID=0` builds the module without any of this, exactly as 1.0.3.

## Testing on the card

Nothing of the open code has run on a real BCM4360 yet. `scripts/layer-test.sh`
takes the module through three configurations, each a run of
`scripts/swap-test.sh` (it loads the module, waits for NetworkManager to
reconnect, checks the link, the gateway, DNS and the kernel log, and goes back
to the previous driver if anything fails):

1. every switch off (`open_pmu=0 ... open_iovar=0`): the hybrid build running
   Broadcom's own functions - tests the build mechanism itself;
2. the defaults: every layer open except the PHY;
3. the defaults with the open PHY (`open_phy=1`).

```sh
sudo systemd-run --unit=bcm4360-layers --collect \
    systemd-inhibit --what=sleep:idle --why="Wi-Fi driver test" \
    "$PWD/scripts/layer-test.sh"        # or: layer-test.sh /path/to/bcm4360.ko
less /var/log/bcm4360-swap-test.log    # each step ends with RESULT: PASS / FAIL
```

The log shows the switches each run really had. It stops at the first failing
step; to find the layer at fault, repeat with one layer switched off at a
time, e.g. `swap-test.sh bcm4360 --params "open_dma=0"`, for each switch in
the table above. `swap-test.sh --params` passes any module parameters.

## How it is tested

`tools/re/hybrid.py` makes the same core for the emulator (`mkhybrid.sh`, the
objects of [`objects.mk`](objects.mk) compiled with zig, `ld -r`) and runs a
whole session on it and on Broadcom's object alone: attach, up, nine channel
changes (both bands, 20/40/80 MHz), periodic work, down, up, down, detach.
Every access to the card is recorded and compared.

```
cd tools/re
python hybrid.py check             # all layers open: the same accesses as the object
python hybrid.py check --off pmu   # the fallback to Broadcom's functions
python hybrid.py share             # how much of the executed code is Broadcom's
python hybrid.py selftest          # a broken open function must be found
python hybrid.py check --steps real   # what the real driver does besides: the glue's
                                      # settings, MPC, scans, radio switch, interface
                                      # down/up, suspend/resume
python hybrid.py dma               # the DMA engines with frames, against channels that
                                   # answer as the real card does
python opencov.py --on phy --soak  # which lines of open/ the sessions never run
```

`hybrid.py dma` exists because the session never moves a frame and the card
model answered the channel probe of `dma_attach` so that every descriptor
index was 0 (masks 0xf); the real card gives masks of 0xffff (2026-11-03).
It drives Broadcom's engine and the open one through the same traffic -
chains with empty buffers, full rings, wrap-arounds, partial completions,
receive frames over several buffers, suspend and error states - and compares
every result, access, descriptor and packet: 7,670 steps, the same.
`opencov.py` builds the core with debug information and maps the executed
blocks to lines: 70.7 % of the open code runs in the session and the soak.

The only accesses left out of the comparison are the moves of the BAR0
windows: Broadcom's PMU code moves the window to ChipCommon and back, the open
code uses ChipCommon's fixed window.

What this proves and what not: the hybrid core does to the *modelled* card
exactly what Broadcom's core does, for this session. The model is not a radio
and its identity values are assumptions (docs/re/05-status.md). The last word
is the real card.

## Results

| Build | Broadcom code executed in the session | Open code executed | Share of all execution (counting repetitions) in open code |
|---|---|---|---|
| object alone (1.0.3) | 200,461 bytes (100 %) | - | 0 % |
| hybrid, PMU open | 197,888 bytes (98.7 %) | 2,716 bytes | - |
| hybrid, PMU, register access, MAC control, utilities open | 194,769 bytes (97.2 %) | 15,765 bytes | **33.0 %** |
| the same + the open PHY (`open_phy=1`) | 123,127 bytes (61.4 %) | 69,061 bytes | 37.6 % |
| default since the CLM layer: PMU, access, MAC, utilities, regulatory database | 187,114 bytes (93.4 %) | 25,831 bytes | **64.7 %** |
| the same + the open PHY (`open_phy=1`) | 115,520 bytes (57.6 %) | 79,127 bytes | **78.6 %** |
| default since 2026-11-02: + `phy_getvar`, MAC object memories, per-rate power container | 181,825 bytes (90.7 %) | 41,093 bytes | **84.4 %** |
| the same + the open PHY (`open_phy=1`) | 110,352 bytes (55.1 %) | 94,348 bytes | **85.2 %** |
| + board variables from SROM/OTP (default) | 179,205 bytes (89.4 %) | 45,101 bytes | **91.2 %** |
| the same + the open PHY (`open_phy=1`) | 107,732 bytes (53.8 %) | 98,356 bytes | **93.3 %** |
| + MAC clocks/reset/initialisation, backplane (default) | 171,321 bytes (85.5 %) | 61,717 bytes | **92.1 %** |
| the same + the open PHY (`open_phy=1`) | 99,848 bytes (49.8 %) | 114,827 bytes | **94.4 %** |
| + DMA engines (default) | 167,241 bytes (83.4 %) | 65,718 bytes | **92.6 %** |
| the same + the open PHY (`open_phy=1`) | 95,768 bytes (47.8 %) | 118,828 bytes | **95.0 %** |
| + MAC up and down (default) | 165,404 bytes (82.5 %) | 68,758 bytes | **92.7 %** |
| the same + the open PHY (`open_phy=1`) | 93,931 bytes (46.9 %) | 121,868 bytes | **95.0 %** |
| + MAC channel and band (default) | 162,929 bytes (81.3 %) | 70,413 bytes | **92.7 %** |
| the same + the open PHY (`open_phy=1`) | 91,456 bytes (45.6 %) | 123,523 bytes | **95.1 %** |
| + MAC at run time (default) | 161,408 bytes (80.5 %) | 72,650 bytes | **92.9 %** |
| the same + the open PHY (`open_phy=1`) | 89,935 bytes (44.9 %) | 125,760 bytes | **95.3 %** |
| + MAC Bluetooth coexistence (default) | 160,443 bytes (80.1 %) | 74,069 bytes | **92.9 %** |
| the same + the open PHY (`open_phy=1`) | 88,970 bytes (44.4 %) | 127,190 bytes | **95.3 %** |
| + chanspecs and channel validity, driver variables (default, 2026-11-03) | 158,509 bytes (79.1 %) | 77,249 bytes | **95.0 %** |
| the same + the open PHY (`open_phy=1`) | 87,040 bytes (43.4 %) | 130,446 bytes | **97.8 %** |
| + rate sets, channel statistics (and the small layers of 2026-11-03: offload semaphore, global NVRAM, module table, PCIe work-around) (default) | 155,080 bytes (77.4 %) | 82,612 bytes | **95.7 %** |
| the same + the open PHY (`open_phy=1`) | 83,611 bytes (41.7 %) | 135,809 bytes | **98.7 %** |
| + transmit power offsets of the chains (`wlc_update_txppr_offset`, default) | 153,989 bytes (76.8 %) | 84,204 bytes | **95.9 %** |
| the same + the open PHY (`open_phy=1`) | 82,520 bytes (41.2 %) | 137,401 bytes | **98.9 %** |

With the open PHY switched on, the whole session is identical to the object
(41,416 accesses: attach, up, nine channel changes in both bands and all
bandwidths, watchdog, down, up, down, detach), also with each of the other
layers switched off. All 79 entry points exist; the receive measurements
(RSSI per frame, noise interrupt) are not reached by the session and are
written from their specification, not compared. The PHY layer is in the
module with `open_phy` off by default until it has run on a card. The
(much larger) static share falls because the whole AC-PHY of Broadcom's
object stops running. (Earlier, the share of execution counting repetitions
was dominated by Broadcom's regulatory database lookup, `wlc_clm.c`, about
46 %; it is open since the CLM layer. With Broadcom's PHY running, its own
variable lookup `phy_getvar` was the next 14 %; it goes through the open
lookup since 2026-11-02, as do the per-rate power container (`wlc_ppr.c`,
about 5 %) and the SROM variable building (`bcmsrom.c`, about 7 %).) What
Broadcom code still runs is spread thin: the MAC (`wlc_bmac.c`), the channel
and rate logic of the upper layer (`wlc_channel.c`, `wlc.c`, `wlc_stf.c`), the
backplane (`siutils.c`, `aiutils.c`) and the DMA engines (`hnddma.c`).

The last column weighs code by how often it runs: the register access
primitives and the variable lookup are small but run constantly (`getvar`
alone was about a fifth of everything the driver executes). `check` gives
"the same" with all layers on and with each switched off; the module builds
against linux-headers 7.2.11-arch1-1 with GCC 16.2.1 without warnings from
`open/` or `hybrid/`, and each of the 309 taken functions has exactly one
strong definition, the adapter's (layers still being written, staged as
`hybrid-new*-objs` in `objects.mk`, keep Broadcom's body until they are
linked).

`python phy_soak.py` (in `tools/re`) is a much longer session: every channel
of both bands in every bandwidth, an association stand-in with the full
calibration, long watchdog runs with the periodic calibration, RSSI on many
receive headers, the noise interrupt, with the card model giving the
measurement registers deterministic values, and `--board N` for board
variants. With Broadcom's PHY and every other layer open (the default
module) it is identical to the object: 513,322 accesses. **With the open PHY
switched on as well it is identical too**, on the synthetic board (513,322
accesses) and on the eight board variants (451,609 to 735,078 accesses
each): every channel and bandwidth, the full calibration on four channels
with measured IQ powers, the periodic multi-phase calibration, measured
temperatures, RSSI and noise. The soak runs 73 % of the open PHY's code
(the rest: front-end overrides of other boards, the debug ioctls and
iovars, entry points of modes the emulator does not use).

Layers that make no access to the card are compared by their results:
`python hybrid.py chan` calls the chanspec helpers for all 65,536 chanspecs
and the validity checks for every channel and bandwidth in 261 states (nine
countries, both bands, band lock, capability variations) on the object alone
and on the hybrid core, 2,459,676 calls; `python hybrid.py iovar` gets and
sets all 405 driver variables (with the `bsscfg:` prefix and odd names too),
3,054 calls, comparing results, value buffers and every access. Both: the
same, also with their switch off.

`python hybrid.py srom` attaches the driver from ten different cards
(SROM images, a blank SROM, the image in the OTP, a blank OTP) on the object
alone and on the hybrid core and compares the variable buffer, the attach
result and every access.

One value of the object is not defined by the object itself: the third
16-bit word of PHY table 0x14, entry 0x33, comes from uninitialized stack in
Broadcom's `sub_09eaf9` (docs/re/questions/phy-rxgain.md); `hybrid.py`
compares that word as undefined.

## Rules for adapters

* An adapter contains interface, not behaviour: the names and arguments of
  Broadcom's functions, the layout of structures the object shares with the
  glue, the mapping to the open API. Behaviour belongs in `open/`, written from
  the specifications (docs/re/implementing.md).
* Declare every taken function with `HYB_TAKE()` (hybrid.h), which also
  declares Broadcom's body `bcm4360_blob_NAME`.
* Parameters of type `bool` of the object are bytes: take them as `u8`.
  Other integer parameters narrower than 32 bit: take them as `u32` and
  truncate. GCC, which built the object, does not extend them; a compiler
  that relies on the extension (clang) would read garbage bits.
* A layer whose state Broadcom's other code reads cannot be taken over in
  part. The PMU keeps no state of its own (one cached clock measurement), so
  its functions can be taken over one by one. The PHY keeps a large state that
  only the PHY reads (measured: no code outside the PHY files touches it), so
  it can be taken over as a whole, at its 79 entry points.
