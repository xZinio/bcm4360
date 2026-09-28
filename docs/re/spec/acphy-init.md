# AC-PHY initialisation of the BCM4360

Status: all procedures below were read in the decompiler output and checked
in the disassembly; what was also compared with emulator traces and
experiments is listed in "Verification" (nearly everything that the BCM4360
path reaches).

Register access notation: see `access.md`. Radio power-up, RCAL, RCCAL, radio
tuning: see `acphy-radio.md`. Register *names* are not known; aliases in
parentheses are mine.

## Scope

AC-PHY (PHY type 11) revision 0 and 1, radio 2069 major revision 0, chip
0x4360 (0x4352 and 0xaa06 take the same branches unless said otherwise), MAC
core revision 42.

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_phy_init` (AC-PHY path) | 0x0babf5 | 738 | original |
| `wlc_phy_chanspec_shm_set` | 0x0b5cf4 | 84 | original |
| `sub_0b018f` | 0x0b018f | 710 | `wlc_phy_init_acphy` (guide) |
| `sub_0a04c2` | 0x0a04c2 | 1826 | assigned: `wlc_phy_set_regtbl_on_pwron_acphy` (name of `acphy-radio`) |
| `sub_0a0be4` | 0x0a0be4 | 2056 | assigned: `wlc_phy_set_reg_on_reset_acphy` (name of `acphy-radio`) |
| `sub_0affa9` | 0x0affa9 | 486 | assigned: `wlc_phy_txpwrctrl_idle_tssi_meas_acphy` |
| `sub_0b1227` | 0x0b1227 | 398 | assigned: `wlc_phy_precal_txgain_acphy` |
| `sub_0b740d` | 0x0b740d | 1207 | assigned: `wlc_phy_interference` |
| `wlc_phy_stay_in_carriersearch_acphy` | 0x097a6d | 203 | original |
| `sub_0979bc` | 0x0979bc | 177 | assigned: `wlc_phy_clip_det_acphy` |
| `wlc_phy_classifier_acphy` | 0x091258 | 78 | original |
| `wlc_phy_ofdm_crs_acphy` | 0x08fa8e | 171 | original |
| `wlc_phy_deaf_acphy` | 0x097b38 | 100 | original |
| `wlc_phy_get_deaf_acphy` | 0x090865 | 183 | original |
| `wlc_phy_resetcca_acphy` | 0x0973fb | 359 | original |
| `wlc_phy_force_rfseq_acphy` | 0x097b9c | 329 | original |
| `wlc_phy_rxcore_setstate_acphy` | 0x097ce5 | 326 | original |
| `wlc_phy_rxcore_getstate_acphy` | 0x0905da | 22 | original |
| `sub_092500` | 0x092500 | 85 | assigned: `wlc_phy_hirssi_elnabypass_shmem_read_clear_acphy` |
| `wlc_phy_update_rxldpc_acphy` | 0x091416 | 80 | original |
| `wlc_acphy_set_scramb_dyn_bw_en` | 0x090bda | 75 | original |
| `wlc_phy_init_test_acphy` | 0x0b18a1 | 43 | original |
| `wlc_phy_hirssi_elnabypass_init_acphy`, `wlc_phy_hirssi_elnabypass_set_ucode_params_acphy` | 0x092555, 0x0923f2 | 119, 270 | original; owned by `acphy-desense`, described in 3a as part of the sequence |
| `sub_08f9b4`, `sub_09868f` | 0x08f9b4, 0x09868f | 147, 194 | assigned: `wlc_phy_tssi_phy_setup_acphy`, `wlc_phy_get_txgain_settings_by_index_acphy`; owned by `acphy-txpower`, described where they are called (sections 6, 7) |
| `wlc_phy_hwaci_setup_acphy`, `wlc_phy_aci_w2nb_setup_acphy`, `wlc_phy_desense_aci_reset_params_acphy` | 0x091e56, 0x0922ef, 0x09ac9f | 1177, 259, 237 | original; owned by `acphy-desense`, described in appendix A as the initialisation calls them |

Not covered (other specifications): the channel function `sub_0a7089` and
everything it calls except `sub_0a0be4` and the helpers H1..H11; the
measurement `sub_0af7f4`; the calibrations.

## Overview

The PHY is initialised once per `wlc_up`: `wlc_init` -> `wlc_bmac_init` ->
`wlc_bmac_bsinit` (`sub_06656c`) -> `wlc_phy_init`. Channel changes and band
changes do not run it again (MAC revision >= 40); everything that depends on
band or bandwidth is done by the channel function `sub_0a7089`, which the
initialisation calls once with a flag "this is the initialisation".

Order of events of one `wlc_phy_init` (sequence numbers of `re-out\up-trace.txt`,
2.4 GHz channel 1, PHY revision 1, radio revision 4):

| Seq | Step | Specified in |
|---|---|---|
| 24920 | `SHM(0xa0)` = chanspec | section 2 |
| 24921 | read of `D11(0x120)` | section 1 |
| 24922 | analog core on: `D11(0x3e6)` = 0 | `acphy-radio` 1 |
| 24923..26655 | radio on: reset, preferred values, RCAL, RCCAL | `acphy-radio` 3..7 |
| 26656..26689 | `sub_0b018f` starts: bit 15 of `PHY(0x1b0)` (rev 1), regulator field of `PMU_REGCTL[0]`, `CC(0x6c)`, shared memory of the LNA bypass | section 3 |
| 26690..26734 | `sub_0a04c2`: radio control overrides released | section 4 |
| 26735..36933 | `sub_0a04c2`: 23 static tables | section 4, "Data" |
| 36934..36941 | `sub_0a04c2`: end | section 4 |
| 36942..49583 | channel function `sub_0a7089` with init flag; in it 37239..37505 `sub_0a0be4` | `acphy-chanspec`; section 5 |
| 49584..49621 | 19 register writes of `sub_0b018f` | section 3 |
| 49622..51153 | `sub_0affa9`: idle TSSI | section 6 |
| 51154..51316 | `sub_0b740d`: interference mode (accesses only for PHY revision 1) | section 8 |
| 51317 | read of `SHM(0x92)` | section 1 |

What the channel function does when it is called by the initialisation
(for orientation only; the call tree of the up-trace, specified in
`acphy-chanspec`, `acphy-radio`, `acphy-rxgain`, `acphy-txpower`,
`acphy-desense`; the functions of this specification in bold):

| Seq | Function | What |
|---|---|---|
| 36942 | **`sub_092500`** | read of `SHM(0x184)` (H11) |
| 36945..36965 | (inline) | bits 1 and 0 of `PHY(0x19e)` set, band bit 8 of `PHY(0x003)` |
| 36966 | **`wlc_phy_stay_in_carriersearch_acphy(pi, 1)`** | after the counter `pi_ac+0x0c` was set to 0 |
| 37016..37186 | (inline) | PLL reset pulse, 50 radio registers of the channel table, patches |
| 37187 | `sub_09311b` | start of the VCO calibration |
| 37229 | (inline) | bits 1 and 0 of `PHY(0x19e)` restored |
| 37239 | **`sub_0a0be4`** | section 5 |
| 37506 | `sub_0a6b0f` | tables that depend on the board (front end control, filters) |
| 40390 | `sub_09e378` | band dependent registers and tables (band changed) |
| 41621 | `sub_09eaf9` | bandwidth dependent registers (bandwidth or band changed) |
| 42221 | `wlc_phy_hirssi_elnabypass_set_ucode_params_acphy` | section 3a |
| 42231 | `sub_0a4adc` | resampler ("farrow") set up, calls **`wlc_phy_force_rfseq_acphy`** |
| 47690, 47693, 48468, 48633 | `sub_092efb`, `sub_09af05`, `sub_09a121`, `sub_09a539` | tx power limits state, receive gain control, desense |
| 48697 | **`wlc_phy_resetcca_acphy`**, delay 1, `sub_096203` | converter calibration (bandwidth changed or init) |
| 48935..49121 | `wlc_phy_txpwr_by_index_acphy` per core, `wlc_phy_txpwrctrl_enable_acphy` | transmit power control |
| 49122 | `sub_093e47` | wait for the VCO calibration |
| 49422 | (inline) read of `PHY(0x00b)`, **`wlc_phy_rxcore_setstate_acphy`** | only at init, see H7 |
| 49533 | **`wlc_phy_resetcca_acphy`** | |
| 49547 | **`wlc_phy_stay_in_carriersearch_acphy(pi, 0)`** | |

Calls into the MAC made by the functions of this specification:
`wlapi_bmac_write_shm`/`wlapi_bmac_read_shm` (`SHM(0xa0)`, `SHM(0x92)`,
`SHM(0x32)`, `SHM(0x180)`, `SHM(0x182)`, `SHM(0x184)`), `wlapi_bmac_bw_set`
(bandwidth bits of the core), `wlapi_bmac_phyclk_fgc` (bit 1 of
`WRAP(d11, 0x408)`), `wlapi_bmac_mhf` (host flags),
`wlapi_suspend_mac_and_wait`/`wlapi_enable_mac` (in H7, H9, H10; during
`wlc_phy_init` the MAC is already suspended and they cause no register
access in the traces), `wlapi_bmac_ucode_wake_override_phyreg_set/clear` (H13);
directly into the backplane code: `si_pmu_regcontrol`, `si_gpiocontrol`,
`si_core_cflags`.

Differences between the two PHY revisions of interest, all in one place:

| Item | PHY revision 0 | PHY revision 1 |
|---|---|---|
| `sub_0b018f` step 1, bit 15 of `PHY(0x1b0)` | not written | set |
| default interference mode (`sh+0x84`, `sh+0x88`) | 1 | 7 |
| `sub_0a0be4` step 19 | `wlc_phy_hwaci_setup_acphy(pi, 0, 0)` | `(pi, 0, 1)` |
| `sub_0b740d` at the end of `wlc_phy_init` | no hardware access | `wlc_phy_hwaci_setup_acphy(pi, 1, 1)`, `wlc_phy_aci_w2nb_setup_acphy(pi, 1)` |
| clip detection off/on in the carrier search (H3) | `PHY(0x6da + c * 0x200)` = 0xffff / 0x404e | bit 14 of `PHY(0x6d4 + c * 0x200)` set / cleared |
| transmit gain index for the calibrations (section 7) | 30 for all cores | 20 for core 0, 30 for core 1 |
| static tables, register values of sections 3, 4, 5 | the same | the same |

The radio revision (3 or 4) makes no difference in any function of this
specification (it does in the radio functions, see `acphy-radio`).

The receiver is made "deaf" around everything that disturbs reception
(channel change, calibration, idle TSSI measurement) with
`wlc_phy_stay_in_carriersearch_acphy` (H4); the calls nest through a counter.

## Data

### Structure fields

The complete list is in `re-out\analysis\fields\acphy-init.tsv`. Used below:

| Field | Meaning |
|---|---|
| `pi+0x28` | function pointer "PHY specific initialisation" = `sub_0b018f` |
| `pi+0x17e` (u16) | chanspec (see `acphy-radio`) |
| `pi+0x182` (u16) | bandwidth bits the core is set to |
| `pi+0x185` (u8) | first initialisation after power-on reset: load the static tables |
| `pi+0x186` (u8) | `wlc_phy_init` is running |
| `pi+0x19c` (u32) | hold flags; bit 1 (scan) and mask 0x021e are tested here, bit 5 is set by `wlc_phy_init` |
| `pi+0x240` (u8) | "home" channel number of the interference code |
| `pi+0xc3c` (u32) | interference mode last applied |
| `pi+0xf58` (pointer) | calibration state; byte +1 phase, bytes +0x65.. per core, +0x92 transmit gain records |
| `pi+0xf84` (u8) | first calibration after association |
| `pi+0xfa6` (u16) | 2 * `SHM(0x92)` |
| `pi_ac+0x0c` (u16) | nesting counter of the carrier search |
| `pi_ac+0x32c`, `+0x32d` (u8) | "initialisation is calling the channel function", "PHY initialised" |
| `pi_ac+0x348` (u8) | bit 7 of `boardflags3` |
| `pi_ac+0x44e` (s16 per core) | idle TSSI |
| `pi_ac+0x8e2` (u16) | bits 8..12 of OTP word 16 |
| `pi_ac+0x8ff` (u8) | receive LDPC setting last written |
| `pi_ac+0x902` (u16) | 0x404e, clip detection value of PHY revision 0 |
| `pi_ac+0x904` (u16) | 0x0fff, value of `PHY(0x339)` outside of the carrier search |
| `pi_ac+0x912` (u8) | PHY revision < 2: high RSSI LNA bypass feature |
| `sh+0x31` (u8) | the PHY may be accessed (interface up, clock on) |
| `sh+0x80` (u32) | interference mode in use |
| `sh+0x84`, `sh+0x88` (u32) | default interference mode for 2.4 GHz, 5 GHz |
| `sh+0x8c`, `sh+0x90` (u32), `sh+0x94` (u8) | interference mode set by the user for 2.4 GHz, 5 GHz; "set by the user" |
| `sh+0xa4`, `sh+0xa5` (u8) | masks of the transmit/receive cores of the hardware (3, 3) |
| `sh+0xa6`, `sh+0xa7` (u8) | masks of the transmit/receive cores in use |

### PHY registers written by the functions of this specification

Purposes are interpretations (mine) unless the text of a section says more.

| Register | Use here |
|---|---|
| 0x001 | bit 14: reset of the clear channel assessment (H5) |
| 0x00d..0x011 | table access (`access.md`) |
| 0x042 | bit 15: "scrambler dynamic bandwidth" enable (H13) |
| 0x140 | bits 0..2: classifier enables (H1) |
| 0x160 | bits 0..2: mask of receive cores (H7) |
| 0x19e | bit 0, bit 1 (set around table accesses and sequencer triggers), bits 2..5, 6..9 (section 5) |
| 0x1b0 | bit 5 (section 5), bit 6 receive LDPC (H12), bit 15 (section 3) |
| 0x2eb, 0x2ef, 0x2f3, 0x2f7 | bits 0..7 = 0x55 (section 5) |
| 0x2ed, 0x2f1, 0x2f5, 0x2f9 | bit 4: OFDM carrier sense enable (H2); bit 5 cleared in section 5 |
| 0x339 | 0 inside, 0x0fff outside of the carrier search (H4) |
| 0x33a..0x349 | 16 thresholds, section 3 step 11 |
| 0x400 | RF sequencer mode: bit 0 "core activation override", bit 1 "trigger override" (H6, H7) |
| 0x401 | RF sequencer core activation: bits 0..2 transmit cores, bits 4..6 receive cores, bits 12..14 (H7, section 6) |
| 0x402, 0x403 | RF sequencer trigger and status (H6) |
| 0x645 + c * 0x200 | bits 0..9: idle TSSI of core c (section 6); written as 0x1645 = 0x025c for all cores in section 4 |
| 0x690 + c * 0x200 | bits 9, 10 set (section 5) |
| 0x6d4 + c * 0x200, 0x6da + c * 0x200 | clip detection (H3) |
| 0x720..0x729, 0x73a, 0x73e, 0x750 (written as 0x17xx) | radio control overrides and their values (section 4; `acphy-radio`) |
| 0x727 + c * 0x200 bit 2, 0x73c + c * 0x200 bit 4 | TSSI related override (section 6 step 3) |

### Static table set of PHY revision 0 and 1: acphytbl_info_rev0

An array of 23 records of 24 bytes {pointer to the data (8), number of
entries (4), table id (4), offset (4), width in bits (4)}; the number of
records is in `acphytbl_info_sz_rev0`. Data and set are in `.rodata`; extract
with `python fwcut.py phytables 0` and `python blob.py data NAME COUNT WIDTH`.
All records start at offset 0. Written in this order:

| # | Id | Entries | Width | Data |
|---|---|---|---|---|
| 0 | 0x01 | 128 | 16 | `acphy_mcs_tbl_rev0` |
| 1 | 0x02 | 38 | 8 | `acphy_tx_evm_tbl_rev0` |
| 2 | 0x04 | 256 | 8 | `acphy_rx_evm_shaping_tbl_rev0` |
| 3 | 0x03 | 256 | 32 | `acphy_noise_shaping_tbl_rev0` |
| 4 | 0x05 | 22 | 32 | `acphy_phasetrack_tbl_rev0` |
| 5, 6, 7 | 0x40, 0x60, 0x80 | 128 | 16 | `acphy_est_pwr_lut_core0_rev0`, `..core1..`, `..core2..` |
| 8, 9, 10 | 0x41, 0x61, 0x81 | 128 | 32 | `acphy_iq_lut_core0_rev0`, `acphy_iq_lut_core0_rev0` (again: the record of core 1 points to the data of core 0), `acphy_iq_lut_core2_rev0` |
| 11, 12, 13 | 0x42, 0x62, 0x82 | 128 | 16 | `acphy_loft_lut_core0_rev0`, `..core1..`, `..core2..` |
| 14, 15, 16 | 0x40, 0x60, 0x80 | 128 | 16 | `acphy_papd_comp_rfpwr_tbl_core0_rev0`, `..core1..`, `..core2..` |
| 17, 18, 19 | 0x47, 0x67, 0x87 | 64 | 32 | `acphy_papd_comp_epsilon_tbl_core0_rev0`, `..core1..`, `..core2..` |
| 20, 21, 22 | 0x48, 0x68, 0x88 | 64 | 32 | `acphy_papd_cal_scalars_tbl_core0_rev0`, `..core1..`, `..core2..` |

Two peculiarities, both are how the object is: records 14..16 have the same
ids, offset and length as records 5..7 and therefore overwrite them (what
stays in tables 0x40/0x60/0x80 is `acphy_papd_comp_rfpwr_tbl_*`); and the
tables of the third core (0x80..0x88) are written although the board has two
chains.

### Constants

| Constant | Value |
|---|---|
| RF sequencer poll (H6) | every 10 us, at most 200 ms (20000 delays, 20001 reads of `PHY(0x403)`) |
| CCA reset (H5) | 1 us with bit 14 of `PHY(0x001)` set, 2 us after the clock gate is released |
| Thresholds of section 3 step 11 | 0x043f and 0x03c0 (PHY revision < 2) |
| Default interference mode | 1 (PHY revision 0), 7 (PHY revision 1) |
| Transmit gain index for the calibrations | 30; PHY revision 1 core 0: 20 |

## Procedures

`mod(reg, mask, value)` is read-modify-write and always writes. "For each core
c" means c = 0 .. N-1 in rising order, N = `pi+0x168` (2 on this board). Delays
are `osl_delay` in microseconds. `shim` = `sh+0x20`, the first argument of all
`wlapi_*` functions. "2.4 GHz" means (chanspec & 0xc000) = 0, "5 GHz" means
(chanspec & 0xc000) = 0xc000, with the chanspec in `pi+0x17e`.

### 1. wlc_phy_init (.text+0x0babf5, name original), AC-PHY path

Purpose: bring the PHY and the radio from reset to operation on a channel.
Inputs: `pi`, `chanspec` (u16). Called by `sub_06656c` (`wlc_bmac_bsinit`) from
`wlc_bmac_init`, that is once per `wlc_up`/`wlc_init`. It is *not* called on
channel or band changes: `wlc_bmac_bsinit(wlc_hw, chanspec, flag)` skips it
when `flag` != 0 and the MAC revision is 40 or higher, and
`wlc_bmac_set_chanspec` passes flag = 1 (the band change is handled by the
channel function alone). The other caller, `wlc_bmac_bw_set`, calls it only for
PHY types other than 11.

Steps:

1. If `pi+0x186` (u8, "initialisation is running") is not 0: return.
2. `pi+0x17e` = chanspec; `pi+0x186` = 1; `pi+0x18f` (u8) = 0 (purpose unknown).
3. `wlc_phy_chanspec_shm_set(pi, chanspec)` (section 2): `SHM(0xa0)` = chanspec.
4. Read 32 bit `D11(0x120)`; the value is not used.
5. `pi+0xc3c` (u32, interference mode in use by the PHY code) = 0.
6. If bit 1 of `pi+0x19c` is clear and `pi+0xa8c` (u8, purpose unknown; 0 in the
   traces) is 0: set bit 5 (0x20) of `pi+0x19c`. (`pi+0x19c` are the "hold"
   flags that keep measurements and calibrations away; bit 1 = scan in
   progress, bit 5 = not associated; interpretation from brcmsmac.)
7. f = the function pointer `pi+0x28`. If it is null: return (`pi+0x186` stays 1).
8. `wlc_phy_anacore(pi, 1)`: `D11(0x3e6)` = 0 (`acphy-radio`, section 1).
9. If (chanspec & 0x3800) differs from `pi+0x182` (u16, bandwidth the MAC is set
   to): `wlapi_bmac_bw_set(shim, chanspec & 0x3800)`. For the AC-PHY this
   stores the value in `pi+0x182` and sets the bandwidth bits of the core:
   `si_core_cflags(sih, mask 0xc0, b)` with b = 0x40 for 20 MHz (0x1000), 0x80
   for 40 MHz (0x1800), 0xc0 for 80 MHz (0x2000), that is a read of
   `WRAP(d11, 0x408)`, a write and a read. Not executed in the bring-up trace
   (`pi+0x182` is 0x1000 after attach and the interface comes up on a 20 MHz
   channel); the PHY is not reset and `wlc_phy_init` is not entered again on
   this path.
10. `pi+0x1091` (u8) = 1 (purpose unknown).
11. `pi_ac+0x32d` = 0 ("PHY initialised" flag). If the chip id `sh+0x3c` is 0x4352
    or 0x4360 (0xaa06 is not in this test): `pi+0xf88` = 0 ("radio is on" flag),
    so that the next step runs the complete switch-on sequence.
12. `wlc_phy_switch_radio(pi, 1)` (`acphy-radio`, sections 3 to 7: read of
    `D11(0x120)`, radio reset, preferred values, RCAL, RCCAL).
13. Call f(pi) = `sub_0b018f` (section 3).
14. `pi+0x185` (u8) = 0. This is the "first initialisation after power-on
    reset" flag; it is set to 1 by `wlc_phy_attach` and by `wlc_phy_por_inform`
    (called by `wlc_bmac_hw_up`, that is at every `wlc_up`). While it is set
    section 4 loads the static tables.
15. (MAC revisions 11 and 12 only: `wlc_phy_do_dummy_tx`. PHY types 0 and 2:
    more. Not reached.)
16. `wlc_phy_ant_rxdiv_set(pi, sh+0x95)`: for the AC-PHY this only stores the
    value in `sh+0x95` again; no hardware access.
17. If bit 1 of `pi+0x19c` is clear (no scan in progress):
    1. Select the interference mode m: if `sh+0x94` (u8, "the user has set a
       mode") is 1: m = `sh+0x8c` on 2.4 GHz; on 5 GHz m = `sh+0x90` if that is 0
       or 1, else 0. Otherwise m = `sh+0x84` on 2.4 GHz, `sh+0x88` on 5 GHz.
    2. `sh+0x80` = m.
    3. `sub_0b740d(pi, m, 0)` (section 8).
18. `pi+0x186` = 0.
19. v = `wlapi_bmac_read_shm(shim, 0x92)`; `pi+0xfa6` (u16) = 2 * v (the byte
    address of a block in the shared memory whose word address the microcode
    publishes in `SHM(0x92)`; not used by the AC-PHY code I read).

Defaults of the interference modes (set in `wlc_phy_attach` for PHY type 11):
`sh+0x94` = 0; `sh+0x84` = `sh+0x88` = 1 for PHY revisions 0, 1, 2, 3, 5, 6 (else
0); PHY revision 1 then sets bits 1 and 2 in both, giving 7. If the SROM has a
variable `interference` its value replaces both. So: **PHY revision 0: m = 1,
PHY revision 1: m = 7**. The mode is a bit mask: bit 0 desense by software,
bit 1 "hardware ACI" mitigation, bit 2 "w2nb" (wide band/narrow band) ACI
mitigation (names from the functions that the bits enable).

### 2. wlc_phy_chanspec_shm_set (.text+0x0b5cf4, name original)

Inputs: `pi`, `chanspec`. MAC revision `sh+0x28` above 39: v = chanspec
unchanged. (Older MACs: v = chanspec & 0xff, bit 8 set for a 5 GHz channel,
bit 9 set for 40 MHz.) Then `wlapi_bmac_write_shm(shim, 0xa0, v)`. Callers:
`wlc_phy_init`, `wlc_phy_chanspec_set`.

### 3. sub_0b018f (.text+0x0b018f, name from the guide: wlc_phy_init_acphy)

Input: `pi`. The function pointer `pi+0x28`; called only by `wlc_phy_init`.
Steps:

1. If the PHY revision is 1 or 3 or above 5: `mod(PHY(0x1b0), 0x8000, 0x8000)`.
   (Not for revision 0.)
2. r = read `PHY(0x000)` & 0xf. If r <= 1: write the regulator field from OTP
   word 16, `si_pmu_regcontrol(sih, 0, mask 0x01f00000, v << 20)`, with v as
   described in `acphy-radio`, section 9 (r = 0: v = `pi_ac+0x8e2`, or 5 if that
   is 0; r = 1: v = `pi_ac+0x8e2` - 3, or 0 if `pi_ac+0x8e2` <= 3). The access is:
   read `CC(0x658)`, write `CC(0x658)` = 0, read `CC(0x658)`, read `CC(0x65c)`,
   write `CC(0x65c)`, read `CC(0x65c)`.
3. (Chip 0x4350 with boardflags bit 25 (`sh+0x64` & 0x02000000) only:
   `si_pmu_regcontrol(sih, 7, 0x100, 0x100)`.)
4. `si_gpiocontrol(sih, mask 0xffff, value 0, priority 0)`: read `CC(0x6c)`,
   write it with bits 0..15 cleared, read it again (all GPIO lines are
   controlled by ChipCommon).
5. `wlc_phy_hirssi_elnabypass_init_acphy(pi)` (specified in `acphy-desense`).
   With the defaults from attach it writes, when `sh+0x31` is set: `SHM(0x32)` =
   0x0032, `SHM(0x180)` = 0x0527, `SHM(0x182)` = 0x01f4, `SHM(0x184)` = 0. Only
   for PHY revisions 0 and 1 (`pi_ac+0x912`).
6. `pi_ac+0x0c` (u16, nesting counter of the carrier search, H4) = 0.
7. `sub_0a04c2(pi)` (section 4).
8. `pi_ac+0x32c` = 1 ("initialisation" flag for the channel function);
   `pi_ac+0x32e` (u8) = 0xff (purpose unknown, belongs to the channel
   function).
9. `sub_0a7089(pi, pi+0x17e)`: the channel function (`acphy-chanspec`). Because
   `pi_ac+0x32c` is set it treats band and bandwidth as changed and calls
   `sub_0a0be4` (section 5) and `sub_0a6b0f`.
10. `pi_ac+0x32c` = 0; `pi_ac+0x32d` = 1.
11. Write 16 PHY registers, in this order: 0x33a, 0x33b, 0x33e, 0x33f, 0x342,
    0x343, 0x346, 0x347 = A; then 0x33c, 0x33d, 0x340, 0x341, 0x344, 0x345,
    0x348, 0x349 = B. PHY revision 0 and 1: A = 0x043f, B = 0x03c0. (PHY
    revision >= 2: A = 0x0415, B = 0x0395.)
12. PHY revision 0 and 1 only: `PHY(0x16e)` = 0x0013, `PHY(0x16f)` = 0x07d0,
    `PHY(0x170)` = 0x07d0.
13. If `pi+0x185` is set (first initialisation after power-on reset):
    `pi+0x240` (u8, the "home" channel of the interference code) = chanspec & 0xff.
14. `sub_0affa9(pi)` (section 6): idle TSSI measurement.

#### 3a. The shared memory writes of step 5 in detail

(`wlc_phy_hirssi_elnabypass_init_acphy` .text+0x092555 and
`wlc_phy_hirssi_elnabypass_set_ucode_params_acphy` .text+0x0923f2, names
original; they belong to `acphy-desense`, given here because they are part of
the initialisation sequence.)

`wlc_phy_hirssi_elnabypass_init_acphy(pi)`:

1. `pi_ac+0x914` (u16) = 0xffff; `pi_ac+0x916` (u16) = 0xffff (state of the bypass
   for 2.4 GHz and 5 GHz; bit 15 set = not engaged).
2. If `pi_ac+0x912` = 0 (PHY revision >= 2): `pi_ac+0x910` = `pi_ac+0x911` = 0; return.
3. `pi_ac+0x910` = `pi_ac+0x911` = `pi_ac+0x906` (u8, the feature switch; 0 after
   attach).
4. If `sh+0x31` != 0: `wlc_phy_hirssi_elnabypass_set_ucode_params_acphy(pi)`, then
   `wlapi_bmac_write_shm(shim, 0x184, 0)`.

`wlc_phy_hirssi_elnabypass_set_ucode_params_acphy(pi)`, nothing if
`pi_ac+0x912` = 0:

1. 2.4 GHz: en = `pi_ac+0x910`, st = `pi_ac+0x914`; 5 GHz: en = `pi_ac+0x911`, st =
   `pi_ac+0x916`.
2. If en = 0: a = 0x0032, b = 0x0527, c = 0x01f4.
   Otherwise: if bit 15 of st is clear (engaged): a = `pi_ac+0x90f` (s8, -15 after
   attach) sign extended to 16 bit, b = 0x0529, n = `pi_ac+0x90c` (u16, 0x1f);
   else a = `pi_ac+0x90e` (s8, -13), b = 0x0527, n = `pi_ac+0x90a` (u16, 0x1f);
   c = n * k (16 bit) with k = 1 for 20 MHz, 2 for 40 MHz, 4 for 80 MHz.
3. `SHM(0x32)` = a; `SHM(0x180)` = b; `SHM(0x182)` = c (three
   `wlapi_bmac_write_shm`).

### 4. sub_0a04c2 (.text+0x0a04c2, name assigned: wlc_phy_set_regtbl_on_pwron_acphy)

Purpose: release the radio control overrides that "radio off" had set and
load the static PHY tables. Input: `pi`. Callers: `sub_0b018f`;
`wlc_phy_switch_radio_acphy` when the radio is switched on while the PHY is
initialised.

Addresses: for the chips 0x4352, 0x4360, 0xa9c4, 0xaa06 and 0x4350 the
registers 0x7xx and 0x645 are written with bit 12 set (0x17xx, 0x1645; see
`acphy-radio`, "Data"); other chips use the plain address. The two *reads*
in step 5 use the plain address (core 0).

Steps for PHY revision 0 and 1:

1. `PHY(0x410)` = 0x0077 (all revisions except 3). (Revisions 2, 5, 6 then
   write `PHY(0x749)` = 3.)
2. Write 0 to `PHY(0x173e)`, `PHY(0x1725)`, `PHY(0x1722)`, `PHY(0x1723)`,
   `PHY(0x1724)`, `PHY(0x1725)` (again), `PHY(0x1726)`, `PHY(0x1727)`,
   `PHY(0x1750)`, in this order.
3. `PHY(0x1728)` = 0x0080; `PHY(0x1720)` = 0x0180. (Revision 3: 0x4080 and 0x0380.)
4. `PHY(0x1729)` = 0; `PHY(0x1721)` = 0x5000.
5. v = read `PHY(0x73a)`; write `PHY(0x173a)` = v | 0x0100.
   v = read `PHY(0x725)`; write `PHY(0x1725)` = v | 0x0400.
6. s = read `PHY(0x19e)`; `mod(PHY(0x19e), 0x0002, 0x0002)`.
7. Only if `pi+0x185` is set: write the static table set of the PHY revision,
   record by record in the order of the set, each record with
   `wlc_phy_write_table_ext` (one table transfer as in `access.md`: id,
   offset, then the entries).

   | PHY revision | Set | Records |
   |---|---|---|
   | 0, 1, 4 and all above 6 | `acphytbl_info_rev0` | 23 (`acphytbl_info_sz_rev0`) |
   | 2, 5 | `acphytbl_info_rev2` | 10 |
   | 3 | `acphytbl_info_rev3` | 6 |
   | 6 | `acphytbl_info_rev6` | 10 |

   No table is skipped or replaced by a condition other than the PHY
   revision, and the same set is used for both bands. The contents of the
   set for revision 0/1 are in "Data".
8. `mod(PHY(0x19e), 0x0002, s & 0x0002)` (restore bit 1).
9. `PHY(0x1645)` = 0x025c.
10. (PHY revisions 2, 3, 5, 6 with `pi_ac+0x348`: `PHY(0x164c)` = 0x025c.)
11. If `pi_ac+0x348` (u8, bit 7 of the SROM variable `boardflags3`) is not 0:
    `wlapi_bmac_mhf(shim, 1, 0x0080, 0x0080, 3)`: set bit 7 of the MAC host
    flag word 2 (index 1) for both bands (written to the shared memory by the
    MAC code; see the MAC specification).

Notes:

* Step 7: bit 1 of `PHY(0x19e)` is set around every table access in the
  AC-PHY code (here, in `sub_09868f`, in `wlc_phy_cals_acphy`); presumably it
  keeps the table clock running (unverified).
* Steps 2 to 4 undo what "radio off" (`acphy-radio`, section 4) wrote to the
  same registers (there 0x1725 = 0x1fff, 0x1721 = 0xffff, 0x1720 = 0x03ff,
  0x173e = 0x1c00). What remains set after this function: bits 7 and 8 of
  0x720, bits 12 and 14 of 0x721, bit 10 of 0x725, bit 7 of 0x728, bit 8 of
  0x73a, in all cores. (Interpretation as in `acphy-radio`: 0x720, 0x721, 0x725
  are override enables, 0x728, 0x729, 0x73a the override values; the meaning
  of the single bits is unknown.)
* Step 9 writes bits 0..9 of register 0x645 of all cores, the field that
  section 6 later fills with the measured idle TSSI of each core: 0x025c is
  the value that stays when the measurement is held off (interpretation).
* The card model treats an address with bit 12 set as a register of its own.
  Values that the traces show for *reads* of 0x6xx/0x7xx registers after such
  writes (for example `PHY(0x725)`, `PHY(0x73a)` in step 5, `PHY(0x645)` in
  section 6) are therefore those of the model, not necessarily those of the
  hardware. The code does not depend on it: it only modifies what it reads.

### 5. sub_0a0be4 (.text+0x0a0be4, name assigned: wlc_phy_set_reg_on_reset_acphy)

Purpose: PHY register settings that are needed once after a reset. Input:
`pi`. Only caller: the channel function `sub_0a7089`, only while `pi_ac+0x32c`
is set (the call made by `sub_0b018f`), after the radio has been tuned and the
VCO calibration has been started, before `sub_0a6b0f`.

Steps for PHY revision 0 and 1 (no radio access):

1. `phy_reg_or(PHY(0x19e), 0x01c0)`.
2. On 2.4 GHz: `PHY(0x3c4)` = 0x0668.
3. `mod(PHY(0x19e), 0x0200, 0x0200)`.
4. `mod(PHY(0x19e), 0x003c, 0x0010)`.
5. `PHY(0x1f2)` = 0x00c8.
6. PHY revision 0 and 1 only: `PHY(0x026)` = 0x0092.
7. `PHY(0x1ed)` = 0x0050; `PHY(0x025)` = 0x0030.
8. `si_core_cflags(sih, mask 0x10, value 0x10)`: read `WRAP(d11, 0x408)`, write
   it with bit 4 set, read it again.
9. `mod(PHY(0x40f), 0x0200, 0)`.
10. `mod(PHY(0x2f1), 0x0020, 0)`; `mod(PHY(0x2ed), 0x0020, 0)`;
    `mod(PHY(0x2f9), 0x0020, 0)`; `mod(PHY(0x2f5), 0x0020, 0)`.
11. `mod(PHY(0x2ef), 0x00ff, 0x0055)`; `mod(PHY(0x2eb), 0x00ff, 0x0055)`;
    `mod(PHY(0x2f7), 0x00ff, 0x0055)`; `mod(PHY(0x2f3), 0x00ff, 0x0055)` (all
    revisions except 3).
12. `PHY(0x400)` = 0.
13. `mod(PHY(0x1ca), 0x1000, 0)`.
14. `wlc_phy_resetcca_acphy(pi)` (H5).
15. `mod(PHY(0x072), 0x0004, 0x0004)`.
16. `mod(PHY(0x1b0), 0x0020, 0)`; `mod(PHY(0x1b1), 0x1000, 0x1000)`;
    `mod(PHY(0x1b6), 0x8000, 0)`.
17. For each core c, r = 0x690 + c * 0x200: `mod(PHY(r), 0x0200, 0x0200)`, then
    `mod(PHY(r), 0x0400, 0x0400)`.
18. `PHY(0x1e6)` = 0x0030.
19. `wlc_phy_hwaci_setup_acphy(pi, 0, x)` with x = 1 if bit 1 of `sh+0x84` or bit
    1 of `sh+0x88` is set (the configured interference modes; PHY revision 1
    by default), else 0 (`acphy-desense`).
20. `PHY(0x358)` = 0xc07f.

Other revisions, not described: 2, 5, 6 differ in steps 4, 9, 13, write more
registers and table 2 between steps 11 and 12, and call `sub_08fb39`; revision
3 calls `sub_09027d`; both set bit 12 in step 13.

### 6. sub_0affa9 (.text+0x0affa9, name assigned: wlc_phy_txpwrctrl_idle_tssi_meas_acphy)

Purpose: measure the idle TSSI (the reading of the power detector without a
transmit signal) of every core and program it as offset. The measurement
itself (`sub_0af7f4`) and the radio set up (`sub_0940f8`) belong to
`acphy-txpower` and `acphy-radio`; here the control flow. Input: `pi`.
Callers: `sub_0b018f` as its last step; `wlc_phy_cals_acphy` (see section 7).

1. If any of the bits 0x021e of `pi+0x19c` is set (hold flags: scan and
   others; bit 5, which `wlc_phy_init` sets, is not among them): return
   without any access.
2. `wlc_phy_stay_in_carriersearch_acphy(pi, 1)` (H4).
3. `sub_08f9b4(pi, 0)`: for each core c, o = c * 0x200:
   `mod(PHY(0x727 + o), 0x0004, 0x0004)`, then `mod(PHY(0x73c + o), 0x0010, 0)`
   (the second argument, 0 here, goes to bit 4).
4. `sub_0940f8(pi, sh+0xa5, 0)`: TSSI radio set up (`acphy-radio`, section 16);
   `sh+0xa5` (u8) is the mask of the cores the hardware has (3).
5. s = read `PHY(0x401)`.
6. `mod(PHY(0x401), 0x0007, sh+0xa5)`; `mod(PHY(0x401), 0x7000, sh+0xa5 << 12)`.
7. For each core c whose bit is set in `sh+0xa5`:
   1. `sub_0af7f4(pi, buf, 1, 0, 0, 1, c)` with buf a local array of 16 bit
      values, one per core, zero at the start; the function stores the
      measured idle TSSI of core c in buf[c].
   2. `pi_ac+0x44e + 2 * c` (s16) = buf[c].
   3. `mod(PHY(0x645 + c * 0x200), 0x03ff, buf[c])`.
   4. (PHY revisions 2, 3, 5, 6 with `pi_ac+0x348`, cores 0 and 1 only:
      `mod(PHY(0x64c + c * 0x200), 0x03ff, buf[c])`.)
8. Write `PHY(0x401)` = s.
9. `wlc_phy_stay_in_carriersearch_acphy(pi, 0)`.

### 7. sub_0b1227 (.text+0x0b1227, name assigned: wlc_phy_precal_txgain_acphy)

Purpose: choose the transmit gain with which the transmit calibrations run.
Inputs: `pi`, `out` = array of one record of 10 bytes per core (the caller
passes the calibration state `pi+0xf58` + 0x92). It is not part of the
initialisation: its only caller is `wlc_phy_cals_acphy`.

1. For each core c: byte (`pi+0xf58`) + 0x65 + c = 0.
2. PHY revisions 2, 3, 5, 6: `sub_0b0455(pi, out)` and return (not described).
3. For each core c choose an index i into the transmit gain table:
   * PHY revision 1: i = 20 for core 0, 30 for the other cores; but if
     `wlc_phy_get_chan_freq_range_acphy(pi, 0)` returns 4 (the highest 5 GHz
     sub-band; with `subband5gver` 4: centre frequency above 5744 MHz) i = 20,
     30, 20 for core 0, 1, 2.
   * PHY revision 0 (and every other revision with this radio): i = 30.
     (Radio major revision 1 with certain radio revisions: 1 on 2.4 GHz; on
     5 GHz 0, 15, 10 for 20, 40, 80 MHz.)
   Then `sub_09868f(pi, out + 10 * c, i)`: save `PHY(0x19e)`, set its bit 1, read
   entry i (48 bit) of `TBL(0x20)`, restore bit 1 of `PHY(0x19e)`; from the three
   16 bit words w0, w1, w2 of the entry (lowest first) the record gets: u16 at
   +0 = (w0 >> 8) | (w1 & 0xff) << 8; u16 at +2 = (w1 >> 8) | (w2 & 0xff) << 8;
   u16 at +4 = w2 >> 8; u16 at +8 = w0 & 0xff (meaning of the fields:
   `acphy-txpower`).
   (Radio major revision 1 on 5 GHz: the u16 at +2 is ORed with 0x00ff.)

Call sites of sections 6 and 7 in `wlc_phy_cals_acphy(pi, x)` (the calibration
state machine, specified by the calibration analysts), by calibration phase
(byte 1 of the state `pi+0xf58`):

| Phase | Calls |
|---|---|
| 0 (everything in one call) | `sub_0affa9` only if `pi+0xf84` (u8, "first calibration after association", set by `wlc_phy_cal_perical`) is set; then always `sub_0b1227`; after the other calibrations `pi+0xf84` = 0 |
| 1 | `sub_0b1227` |
| 0x13 (19) | `sub_0affa9`; `pi+0xf84` = 0. This phase is only reached if `pi+0xf84` was set when phase 0x12 ended (otherwise the state machine finishes after phase 0x12) |

So in both modes the idle TSSI is measured again only in the first
calibration after an association.

### 8. sub_0b740d (.text+0x0b740d, name assigned: wlc_phy_interference)

Purpose: apply an interference mitigation mode. Inputs: `pi`, `mode` (u32),
`init` (bool). Result: always 1. Callers: `wlc_phy_init` (init = 0),
`wlc_phy_ioctl` (set interference mode, init = 1; first with mode 0 if the
new mode is not 0, then with the new mode), `wlc_phy_interference_set`
(which does nothing for the AC-PHY on this chip).

Steps for PHY type 11:

1. If `init`: `pi+0xbf8` (u32) = 0; `pi+0xbfc` (u16) = 0 (state of the
   interference code of other PHY types).
2. If `mode` = 0: `wlc_phy_desense_aci_reset_params_acphy(pi, 1, 1, 1)`;
   `wlc_phy_hwaci_setup_acphy(pi, 0, 0)`; `wlc_phy_aci_w2nb_setup_acphy(pi, 0)`.
3. If bit 1 of `sh+0x80` is set: `wlc_phy_hwaci_setup_acphy(pi, 1, 1)`.
4. If bit 2 of `sh+0x80` is set: `wlc_phy_aci_w2nb_setup_acphy(pi, 1)`.
5. `pi+0xc3c` = mode.

Steps 3 and 4 test the stored mode `sh+0x80`, not the argument (the callers
store the mode before the call). The three functions called are specified in
`acphy-desense`. With the defaults: PHY revision 0 (mode 1): no hardware
access at all; PHY revision 1 (mode 7): steps 3 and 4.

### H1. wlc_phy_classifier_acphy (.text+0x091258, name original)

Inputs: `pi`, `mask` (u16), `value` (u16). Result: the new register value.

1. v = read `PHY(0x140)`.
2. n = (v & ~mask) | (value & mask).
3. Write `PHY(0x140)` = n (a plain register write, not `phy_reg_mod`: the
   write is a full 32 bit address+data write and counts for the write pacing).
4. Return n.

Bits 0..2 of `PHY(0x140)` enable the classification of received frames
(interpretation, from the use: bit 0 CCK, bit 1 OFDM, bit 2 "waited"; mine).
Callers: H4 and the calibration `sub_0abc76` with mask 7; `sub_0baff6`
(`wlc_phy_cmn.c`) first with mask 0 and value 0 to get the current value (the
register is then written back unchanged), then with mask 7 and value 0, later
with mask 7 and the saved value.

### H2. wlc_phy_ofdm_crs_acphy (.text+0x08fa8e, name original)

Inputs: `pi`, `enable` (bool). Steps, in this order:
`mod(PHY(0x2ed), 0x0010, e)`, `mod(PHY(0x2f1), 0x0010, e)`,
`mod(PHY(0x2f5), 0x0010, e)`, `mod(PHY(0x2f9), 0x0010, e)` with e = 0x0010 if
`enable`, else 0. Only caller: H4.

### H3. sub_0979bc (.text+0x0979bc, name assigned: wlc_phy_clip_det_acphy)

Inputs: `pi`, `enable` (bool). Switches the clip detection of every core on or
off (interpretation). For each core c, with o = c * 0x200:

* PHY revision 0: write `PHY(0x6da + o)` = 0xffff if not `enable`; = `pi_ac+0x902`
  (u16; the constant 0x404e set at attach) if `enable`.
* PHY revision not 0: if `enable` `phy_reg_and(PHY(0x6d4 + o), 0xbfff)` (clear
  bit 14), else `phy_reg_or(PHY(0x6d4 + o), 0x4000)` (set bit 14).

Only caller: H4.

### H4. wlc_phy_stay_in_carriersearch_acphy (.text+0x097a6d, name original)

Inputs: `pi`, `enable` (bool). State: `pi_ac+0x0c` (u16), a nesting counter
("deaf count"); it is set to 0 by `sub_0b018f` at every PHY initialisation.

`enable` != 0 (make the receiver deaf):

1. If the counter is 0:
   1. H1 with mask 7, value 4.
   2. H2 with enable = 0.
   3. H3 with enable = 0.
   4. Write `PHY(0x339)` = 0.
2. Counter += 1.
3. `wlc_phy_resetcca_acphy(pi)` (H5). This is done at every call, also when
   the counter was not 0.

`enable` = 0:

1. Counter -= 1 (16 bit; there is no check against underflow).
2. If the counter is now 0:
   1. H1 with mask 7, value 7 if the current channel (`pi+0x17e`) is a 2.4 GHz
      channel ((chanspec & 0xc000) = 0), else value 6.
   2. H2 with enable = 1.
   3. H3 with enable = 1.
   4. Write `PHY(0x339)` = `pi_ac+0x904` (u16; the constant 0x0fff set at attach).
   No CCA reset on this path.

Notes:

* The counter is set to 0 in two places outside of this function: by
  `sub_0b018f` (section 3 step 6) and by the channel function `sub_0a7089`
  immediately before its own call with enable = 1. A channel change therefore
  ends every "deaf" state that was entered before it (for example through
  H9): it enters with the counter at 0 and leaves with its last step.
* Callers: the channel function, the idle TSSI measurement (section 6), the
  calibrations (`sub_0abc76`, `sub_0addfa`, `sub_0b0455`),
  `wlc_phy_tx_tone_acphy`, an iovar handler of `wlc_phy_cmn.c` (`sub_0c0f88`),
  H9. The pairing of the calls was checked for section 6, H9 and the channel
  function only.
* What the four steps do to the receiver (interpretation): no frame type is
  classified except "wait" (classifier value 4), the OFDM carrier sense is
  off, clip detection is off, `PHY(0x339)` = 0 (purpose unknown).

### H5. wlc_phy_resetcca_acphy (.text+0x0973fb, name original)

Input: `pi`. PHY revisions 0, 1 (all revisions except 2, 5, 6):

1. `wlapi_bmac_phyclk_fgc(shim, 1)`: for PHY types 4, 7 and 11 this is
   `si_core_cflags(sih, mask 2, value 2)` on the 802.11 core: read
   `WRAP(d11, 0x408)`, write it with bit 1 set, read it again.
2. v = read `PHY(0x001)`.
3. Write `PHY(0x001)` = v | 0x4000.
4. Delay 1.
5. Write `PHY(0x001)` = v & 0xbfff.
6. `wlapi_bmac_phyclk_fgc(shim, 0)`: the same with bit 1 cleared.
7. Delay 2.

PHY revisions 2, 5, 6 (other chips): a longer sequence that also toggles bits
of `PHY(0x19e)`; not described.

Callers: H4 (every enter), `sub_0a0be4` (section 5 step 14), the channel
function (before the converter calibration when the bandwidth changed or at
initialisation, and always near its end, before it leaves the carrier
search), `sub_097562`,
`wlc_phy_hirssi_elnabypass_apply_acphy`, `wlc_phy_stopplayback_acphy`.

### H6. wlc_phy_force_rfseq_acphy (.text+0x097b9c, name original)

Triggers one of the sequences of the RF sequencer of the PHY and waits until
it has run. Inputs: `pi`, `cmd` (u8).

| cmd | trigger and status bit | meaning (names from the N-PHY of brcmsmac) |
|---|---|---|
| 0 | 0x0001 | RX2TX |
| 1 | 0x0002 | TX2RX |
| 2 | 0x0020 | RESET2RX |
| 3 | 0x0004 | UPDATEGAINH |
| 4 | 0x0008 | UPDATEGAINL |
| 5 | 0x0010 | UPDATEGAINU |

Any other `cmd`: return without any access. Steps:

1. s400 = read `PHY(0x400)`; s19e = read `PHY(0x19e)` (in this order).
2. `mod(PHY(0x19e), 0x0002, 0x0002)`; `mod(PHY(0x19e), 0x0001, 0x0001)`.
3. `phy_reg_or(PHY(0x400), 0x0003)`.
4. `phy_reg_or(PHY(0x402), bit)`: the trigger.
5. Poll: read `PHY(0x403)`; finished when (value & bit) = 0. Otherwise delay 10
   and read again; at most 20000 delays, that is 200 ms (the inlined
   `SPINWAIT(condition, 200000)`: 20001 reads at most). A timeout is not
   reported and changes nothing in what follows.
6. Write `PHY(0x400)` = s400; write `PHY(0x19e)` = s19e (plain writes).

Callers and the commands they use: H7 and `sub_0a4adc`: 0 then 1;
`wlc_phy_tx_tone_acphy`: 0; `wlc_phy_hirssi_elnabypass_apply_acphy`,
`sub_09cf53`, `sub_0addfa`: 2. The commands 3, 4, 5 are not used by any code of
the object.

### H7. wlc_phy_rxcore_setstate_acphy (.text+0x097ce5, name original)

Inputs: `pi`, `rxmask` (u8, bit c = receive core c in use). Steps:

1. `sh+0xa7` = rxmask (always).
2. If `sh+0x31` (u8; "the PHY clock is running/the interface is up", set by
   the MAC side) is 0: return.
3. `wlapi_suspend_mac_and_wait(shim)`.
4. s401 = read `PHY(0x401)`; s400 = read `PHY(0x400)`.
5. `mod(PHY(0x160), 0x0007, rxmask)`.
6. `mod(PHY(0x401), 0x0070, rxmask << 4)`.
7. `mod(PHY(0x401), 0x7000, 0x7000)`.
8. `mod(PHY(0x401), 0x0007, 0)`.
9. `mod(PHY(0x400), 0x0001, 0x0001)`.
10. H6 with cmd 0 (RX2TX), then H6 with cmd 1 (TX2RX).
11. `mod(PHY(0x401), 0x0007, sh+0xa6)` (`sh+0xa6`, u8: the mask of transmit
    cores).
12. `mod(PHY(0x401), 0x7000, s401 & 0x7000)`.
13. Write `PHY(0x400)` = s400.
14. `wlapi_enable_mac(shim)`.

Callers:

* The channel function `sub_0a7089`, only when it is called by the
  initialisation (`pi_ac+0x32c` set), after the wait for the VCO calibration:
  n = read `PHY(0x00b)` & 7 (number of cores of the PHY); if `sh+0xa7` differs
  from (1 << n) - 1 or from `sh+0xa4`: `wlc_phy_rxcore_setstate_acphy(pi,
  sh+0xa7)`. On this board (`sh+0xa7` = 3 = `sh+0xa4`) the call is made unless
  the PHY reports exactly two cores. Whether or not the call was made, H5
  follows.
* `wlc_phy_cals_acphy`: at its start it saves `sh+0xa7` and `sh+0xa6`, replaces
  them by the masks of the hardware `sh+0xa5` and `sh+0xa4` and calls this
  function with `sh+0xa5`; at its end it restores both and calls it with the
  restored `sh+0xa7`.
* `wlc_phy_stf_chain_set(pi, txchain, rxchain)` (`wlc_phy_cmn.c`), when the MAC
  changes the chains: `sh+0xa6` = txchain, then this function with rxchain.

Because of step 11 a change of `sh+0xa6` (transmit cores) reaches the
hardware through this function, too.

### H8. wlc_phy_rxcore_getstate_acphy (.text+0x0905da, name original)

Result: (read `PHY(0x401)` & 0x0070) >> 4. Not referenced by any code of the
object.

### H9. wlc_phy_deaf_acphy (.text+0x097b38, name original)

Inputs: `pi`, `mode` (bool). Steps:

1. `wlapi_suspend_mac_and_wait(shim)`.
2. If `mode` != 0 and the counter `pi_ac+0x0c` is 0: H4 with enable = 1.
   If `mode` = 0 and the counter is not 0: H4 with enable = 0. Otherwise nothing.
3. `wlapi_enable_mac(shim)`.

So the function never nests: it enters once and leaves once (leaving only
takes one level off the counter). Callers: `wlc_phy_set_deaf` (mode 1),
`wlc_phy_clear_deaf` (mode 0) of `wlc_phy_cmn.c`.

### H10. wlc_phy_get_deaf_acphy (.text+0x090865, name original)

Input: `pi`. Result: bool "the receiver is deaf". Not referenced by any code of
the object. Steps:

1. `wlapi_suspend_mac_and_wait(shim)`.
2. v = read `PHY(0x140)`. If (v & 7) != 4 the result is 0 and step 3 is skipped.
3. For each core c (o = c * 0x200), stop at the first failure:
   * PHY revision 0: read `PHY(0x6da + o)`; failure if the value is not 0xffff.
   * other revisions: read `PHY(0x6d4 + o)`; failure if bit 14 is clear.
   The result is 1 if no core failed, else 0.
4. `wlapi_enable_mac(shim)`.

### H11. sub_092500 (.text+0x092500, name assigned: wlc_phy_hirssi_elnabypass_shmem_read_clear_acphy)

Input: `pi`. Result: bool. Steps:

1. If `pi_ac+0x912` (u8; set at attach to 1 if the PHY revision is below 2) is 0:
   return 0 without any access.
2. v = `wlapi_bmac_read_shm(shim, 0x184)`. If v != 0xdead return 0.
3. `wlapi_bmac_write_shm(shim, 0x184, 0)`; return 1.

`SHM(0x184)` is a flag that the microcode sets to 0xdead (meaning: see the
specification of the high RSSI LNA bypass, `acphy-desense`). Callers:
`wlc_phy_hirssi_elnabypass_engine`, and the channel function `sub_0a7089` as
its first hardware access, there only if the band changes (at
initialisation, or the band of the new chanspec differs from `pi_ac+0x330`).
If the result is 1 the channel function stores `pi_ac+0x908` (u16, 5 after
attach) in the bypass state of the band that is being left: in `pi_ac+0x914`
if that band is 2.4 GHz and `pi_ac+0x910` is set, in `pi_ac+0x916` if it is
5 GHz and `pi_ac+0x911` is set.

### H12. wlc_phy_update_rxldpc_acphy (.text+0x091416, name original)

Inputs: `pi`, `ldpc` (u8). If `ldpc` equals `pi_ac+0x8ff` (u8; 1 after attach)
nothing is done. Otherwise: `pi_ac+0x8ff` = ldpc; v = read `PHY(0x1b0)`; write
`PHY(0x1b0)` = v | 0x0040 if ldpc != 0, else v & 0xffbf. Caller:
`wlc_phy_ldpc_override_set`.

### H13. wlc_acphy_set_scramb_dyn_bw_en (.text+0x090bda, name original)

Inputs: `pi`, `enable` (bool). Steps: `wlc_phyreg_enter(pi)`; v = read
`PHY(0x042)`; write `PHY(0x042)` = v | 0x8000 if `enable`, else v & 0x7fff;
`wlc_phyreg_exit(pi)`. Caller: `sub_06212d` (called by
`wlc_bmac_ifsctl_edcrs_set`, after every channel change and at the end of
`wlc_up`, and by an iovar), which passes its own bool argument on, for every
bandwidth, after it has written the MAC registers `D11(0x6c6)`/`D11(0x6c8)`
(MAC specification). In all traces `enable` is 1.

`wlc_phyreg_enter`/`wlc_phyreg_exit` keep a nesting counter in `pi+0x113d` (u8);
the first enter calls `wlapi_bmac_ucode_wake_override_phyreg_set(shim)`, the
last exit `wlapi_bmac_ucode_wake_override_phyreg_clear(shim)`.

### H14. wlc_phy_init_test_acphy (.text+0x0b18a1, name original)

Input: `pi`. Not referenced by any code of the object. Steps:
`wlc_btcx_override_enable(pi)`; `wlc_phy_txpwrctrl_enable_acphy(pi, 0)`;
`wlc_phy_cals_acphy(pi, 0)`.

### A. Appendix: the interference set up functions as the initialisation calls them

These three functions are owned by `acphy-desense`. They are described here
because `sub_0a0be4` and `sub_0b740d` call them and the initialisation is not
complete without them; if the two specifications differ, trust the trace.

Parameters (fields of `pi_ac`, filled at attach; the values are the same for
PHY revision 0 and 1 in the emulator, read from memory after bring-up):

| Field | Value | Goes to |
|---|---|---|
| `+0x672` (u16) | 300 | `PHY(0x558)`, `PHY(0x559)`: t = (value * 10000) >> 3 = 375000 |
| `+0x674` (u16) | 1000 | `PHY(0x554)`, `PHY(0x555)` |
| `+0x676` (u16) | 500 | `PHY(0x556)`, `PHY(0x557)` |
| `+0x678` (u16) | 1 | `PHY(0x552)`, `PHY(0x553)` |
| `+0x67a` (u8) | 0x0f | `PHY(0x550)` bits 4..7 and 8..11 |
| `+0x67b` (u8) | 0x0f | `PHY(0x551)` bits 0..3 and 4..7 |
| `+0x67c`, `+0x67d`, `+0x67e`, `+0x67f` (u8) | 1, 0, 0, 0 | radio registers 0x045, 0x049 of each core |
| `+0x680` (u8) | 4 | bits 12..15 of radio register 0x033 of each core |

`wlc_phy_hwaci_setup_acphy(pi, enable, init)` (.text+0x091e56, name original):

1. For each core c, o = c * 0x200: `mod(PHY(0x728 + o), 0x3800, enable ? 0x0800 : 0)`;
   `mod(PHY(0x721 + o), 0x4000, 0x4000)`.
2. Only if `init`:
   1. For each core c, b = c << 9 (register addresses of the chips 0x4352,
      0x4360, 0xa9c4, 0xaa06): `mod(RADIO(0x045|b), 0x0080, f67c << 7)`; then on
      `RADIO(0x049|b)` six modifications in this order: mask 0xe000 <- 0, mask
      0x1800 <- 0, mask 0x0400 <- 0, mask 0x0300 <- f67d << 8, mask 0x0040 <-
      f67e << 6, mask 0x0080 <- f67f << 7.
   2. With B = 0x550 for PHY revision 0 and 1 (0x5a0 for revisions >= 2):
      v = read `PHY(B)`; write `PHY(B)` = (v & 0xf000) | ((v | 0x000d) & 0x000f) |
      f67a << 4 | f67a << 8 (f67a <= 15).
      v = read `PHY(B+1)`; write `PHY(B+1)` = (v & 0xff00) | (f67b & 0x0f) | f67b << 4.
   3. Write `PHY(B+2)` = `PHY(B+3)` = f678; `PHY(B+4)` = `PHY(B+5)` = f674;
      `PHY(B+6)` = `PHY(B+7)` = f676; `PHY(B+8)` = t & 0xffff; `PHY(B+9)` = t >> 16.
      With the values above: 0x0ffd, 0x00ff, 1, 1, 0x03e8, 0x03e8, 0x01f4,
      0x01f4, 0xb8d8, 0x0005 (the first two if the registers read 0 or
      already hold these values).

`wlc_phy_aci_w2nb_setup_acphy(pi, on)` (.text+0x0922ef, name original): for each
core c, o = c * 0x200: `mod(PHY(0x729 + o), 0x1000, on ? 0x1000 : 0)`;
`mod(PHY(0x721 + o), 0x1000, 0x1000)`; if `on`:
`mod(RADIO(0x033 | c << 9), 0xf000, (f680 & 0xf) << 12)`.

`wlc_phy_desense_aci_reset_params_acphy(pi, apply, a, b)` (.text+0x09ac9f, name
original), as called with (1, 1, 1): clears 0xf0 bytes at `pi_ac+0x6c8` and
0xf0 bytes at `pi_ac+0x7b8` (the statistics of the desense code), sets the
pointer `pi_ac+0x8a8` to null, then calls `sub_09a539(pi, 1)` (application of
the desense values to the receive gain tables; `acphy-desense`/`acphy-rxgain`),
which causes all the hardware accesses of this function.

Calls during initialisation, in order:

1. `sub_0a0be4` step 19: `wlc_phy_hwaci_setup_acphy(pi, 0, x)`.
2. The channel function, because the band counts as changed, through
   `sub_09e378` (`acphy-radio` section 15), after `sub_08f9b4(pi, 0)` and
   `sub_0940f8(pi, sh+0xa5, 0)`: with m = `sh+0x80`:
   `wlc_phy_hwaci_setup_acphy(pi, (m >> 1) & 1, 0)`, then
   `wlc_phy_aci_w2nb_setup_acphy(pi, (m >> 2) & 1)`. At the first
   initialisation after attach m is still 0 (`sh+0x80` is only written in
   `wlc_phy_init` step 17, unless the SROM has the variable `interference`);
   at later initialisations and on band changes it is the mode in use.
3. `sub_0b740d` at the end of `wlc_phy_init` as in section 8.

## Verification

Traces: `re-out\up-trace.txt` (PHY revision 1, radio revision 4, 2.4 GHz
channel 1), `re-out\chan\*.txt`, the PHY revision 0 trace of the radio analyst
`re-out\analysis\acphy-radio\r3-up.txt`, and my own runs in
`re-out\analysis\acphy-init\` made with `myrun.py` (a harness on top of the
unchanged tools; `a1-*`: bring-up with PHY revision 1 followed by direct calls
of single functions). `tblcheck.py` compares table transfers of a trace with
the static table sets of the object.

| Item | Checked how | Result |
|---|---|---|
| `wlc_phy_init` is not run on channel/band changes | no access with `wlc_phy_init` in the call chain in the four `re-out\chan\*.txt`, although `chan-36` is a band change and runs `sub_06656c`; arguments of the two calls of `sub_06656c` read in the disassembly (`wlc_bmac_init`: 0, `wlc_bmac_set_chanspec`: 1) | as specified |
| Order of events of `wlc_phy_init` | up-trace seq 24920..51321 | as in "Overview" |
| `wlc_phy_chanspec_shm_set` | up-trace seq 24920 | `SHM(0xa0)` = 0x1001 |
| `sub_0b018f` steps 1..5 | up-trace seq 26656..26689 | as specified; `PHY(0)` reads 1 in the model, regulator field written with 0 |
| `sub_0b018f` step 1 absent for PHY revision 0 | `r3-up.txt` | no access to `PHY(0x1b0)` by `sub_0b018f` |
| `sub_0b018f` steps 11, 12 | up-trace seq 49584..49621 | 8 x 0x043f, 8 x 0x03c0, then 0x0013, 0x07d0, 0x07d0 |
| `sub_0a04c2` register part | up-trace seq 26690..26734, 36934..36941 | order and values as specified |
| Static tables | `tblcheck.py static ..\..\re-out\up-trace.txt 26735 36934 0` | 23 transfers, ids, offsets, lengths and every entry equal to the data of `acphytbl_info_rev0` in the order of the set; tables 0x40/0x60/0x80 written twice |
| `sub_0a0be4` | up-trace seq 37239..37505 | steps 1..18 in order with the values specified; step 19 with x = 1 (radio accesses of the hardware ACI set up follow); step 20 |
| Interference defaults | fields printed by `myrun.py` after bring-up: `sh+0x84` = `sh+0x88` = `sh+0x80` = 7, `pi+0xc3c` = 7 (PHY revision 1) | as specified |
| `sub_0b740d` | up-trace seq 51154..51316: one call of each of the two set up functions with radio accesses; `r3-up.txt` (PHY revision 0): no access with `sub_0b740d` in the chain | as specified |
| `sub_0affa9` | up-trace seq 49622..51153: carrier search entered, `sub_08f9b4` (0x727, 0x73c, 0x927, 0x93c), `sub_0940f8`, `PHY(0x401)` saved and written back as 0x0033, two measurements, `mod` of 0x645 and 0x845 | as specified (the measured values are 0 in the model) |
| Carrier search enter/leave, classifier, OFDM CRS, clip detection (PHY revision 1) | up-trace seq 49547..49583 (leave, 2.4 GHz: classifier 0x0807), 49622..49671 (enter); `a1-call-01-deaf1.txt`, `a1-call-04-deaf0.txt` | as specified |
| Clip detection, PHY revision 0 | `r3-up.txt` seq 36955, 36957 | `PHY(0x6da)`, `PHY(0x8da)` = 0xffff |
| CCA reset | up-trace seq 49533..49545; time stamps of `a1-call-01-deaf1.txt` (1 us between the two writes of `PHY(0x001)`, 2 us after the last access) | as specified |
| RF sequencer, receive core set up | up-trace seq 49425..49532 | as specified; the status reads 0 in the model, so one poll per trigger |
| RF sequencer with a busy status | runs `a2` (model: the status bit of `PHY(0x403)` reads as set three times after the trigger), direct calls with cmd 0..6 and 255 | triggers 0x01, 0x02, 0x20, 0x04, 0x08, 0x10 for cmd 0..5; four reads of `PHY(0x403)` 10 us apart; cmd 6 and 255: no access |
| RF sequencer timeout | run `a3` (status bit never cleared), `a3-call-00-rfseq1.txt` | 20001 reads of `PHY(0x403)`, 200.000 ms between the first and the last, then the two restoring writes |
| Static tables only after power-on reset | run `a4`: `wlc_phy_init` called again directly: 14755 accesses, no table transfer in `sub_0a04c2`; after `wlc_phy_por_inform`: 24953 accesses, 2620 table entries, `tblcheck.py static` finds the set again | as specified |
| `wlc_phy_init` step 9 (bandwidth) | `a4-call-07-init_36_80.txt` (chanspec 0xe02a while the core is set to 20 MHz) | between the analog core write and the radio: `WRAP(d11, 0x408)` read 0x2155, written 0x21d5, read; `pi+0x182` = 0x2000 afterwards |
| Band dependence | `a4-call-05-init_36.txt` (chanspec 0xd024): classifier written with 6 (0x0806) when the carrier search is left, no write of `PHY(0x3c4)`; `a4-call-09-init_1.txt`: 7 (0x0807), `PHY(0x3c4)` = 0x0668 | as specified |
| Complete list of differences between the PHY revisions | `cmpfunc.py a8-up.txt b0-up.txt FUNCTIONS`: per function of this specification (and of 3a, appendix A) the accesses made inside `wlc_phy_init` in a bring-up with PHY revision 1/radio revision 4 and with PHY revision 0/radio revision 3, compared as sequences of (operation, register, value) | identical except: `sub_0b018f` (bit 15 of `PHY(0x1b0)`; regulator value 0 resp. 5 << 20 because `PHY(0)` reads the revision in the model), `sub_0979bc` (H3), the hardware ACI and w2nb set up (104 resp. 16 and 20 resp. 8 accesses), values read back from `PHY(0x1b0)`, and the dummy reads of the write pacing (`D11(0x3e0)` holds the revision) |
| Radio revision | `cmpfunc.py a8-up.txt a9-up.txt FUNCTIONS` (PHY revision 1 with radio revision 4 and 3) | identical for all functions |
| Conditions of `wlc_phy_init` | run `a6`, direct calls after changing fields: `pi+0x186` = 1: no access; `pi+0x19c` = 2: bit 5 not set, no idle TSSI measurement, no interference step (`pi+0xc3c` stays 0), 13061 accesses instead of 14755; `pi+0xa8c` = 1: bit 5 not set; `sh+0x94` = 1 with `sh+0x8c` = 2, `sh+0x90` = 4: mode 2 on channel 1, mode 0 on channel 36; `sh+0x90` = 1: mode 1 on channel 36 | as specified |
| PHY revision 0, radio revision 3 | run `b0` (`b0-up.txt`): fields after bring-up `sh+0x80` = `sh+0x84` = `sh+0x88` = 1, `pi+0xc3c` = 1; direct calls: deaf/get_deaf (results 0, 1, 0; `PHY(0x6da)`, `PHY(0x8da)`) | as specified |
| `sub_0b740d` with mode 0 | direct calls `a5-call-07-interf0_1.txt` (PHY revision 1, stored mode 7): reset of the desense parameters, hardware ACI set up (0, 0), w2nb set up (0), then hardware ACI set up (1, 1) and w2nb set up (1); `b0-call-06` (PHY revision 0, stored mode 1): only the first three; `b0-call-08` (mode 1, init 0): no access; `pi+0xc3c` = mode afterwards | as specified |
| `sub_0affa9` hold flags | `b0`: direct call with `pi+0x19c` = 0x20: 1520 accesses; with 0x22: none | as specified |
| `sub_0b1227` | direct calls (`b0-call-05`, `b0-call-14`, `a5-call-00/02/04/06`) on channels 1, 36, 144, 149: entries of `TBL(0x20)` read: PHY revision 0: 30, 30; PHY revision 1: 20, 30 on every channel; records of 10 bytes filled as specified (bytes 6, 7 untouched), the third record untouched; bytes +0x65, +0x66 of the calibration state cleared | as specified; the case "core 2 in frequency range 4" cannot be reached with two cores |
| `boardflags3` bit 7 | run `c1` (`boardflags3=0x80`): after `PHY(0x1645)` = 0x025c the host flag write `SHM(0x60)` = 0x0080 | as specified |
| Section 3a | runs `a10`, `a11`, direct calls after changing `pi_ac+0x906`, `+0x914`, `+0x916` and the channel: switch off: `SHM(0x32)`, `SHM(0x180)`, `SHM(0x182)` = 0x0032, 0x0527, 0x01f4 and `SHM(0x184)` = 0; on, not engaged, 20/40/80 MHz: 0xfff3, 0x0527, 0x001f/0x003e/0x007c; engaged: 0xfff1, 0x0529, 0x001f (20 MHz), 0x007c (80 MHz); `wlc_phy_init` sets the state back to 0xffff | as specified |
| `wlc_phy_init` step 9, 40 MHz | `a11-call-01-init_36_40.txt` (chanspec 0xd826) | `WRAP(d11, 0x408)` 0x2155 -> 0x2195 |
| Appendix A, `wlc_phy_hwaci_setup_acphy(pi, 0, 1)` | up-trace seq 37374..37503 | as specified: `PHY(0x728)`, `PHY(0x928)` mask 0x3800 <- 0, bit 14 of `PHY(0x721)`, `PHY(0x921)`, 7 radio modifications per core, ten PHY registers 0x550..0x559 with the values listed |
| Appendix A, calls by the band change function | up-trace seq 41438..41476 (first initialisation: both functions with 0: bits 11 of 0x728/0x928 and 12 of 0x729/0x929 cleared, no radio access); `a4-call-00-init.txt` (second initialisation, stored mode 7): both with 1, radio registers 0x033, 0x233 = 0x4000 | as specified |
| Appendix A, parameters | bytes `pi_ac+0x670..0x681` printed by `myrun.py` after bring-up, runs `a7` (PHY revision 1) and `b1` (PHY revision 0) | the same values for both revisions |
| `wlc_acphy_set_scramb_dyn_bw_en` call sites | up-trace seq 55765 (end of `wlc_up`: `PHY(0x042)` 0 -> 0x8000), one call at the end of each of the four channel changes (0x8000 -> 0x8000) | enable = 1 in all traces |
| `wlc_phy_init_test_acphy`; call sites of sections 6 and 7 in calibration phase 0 | direct calls, runs `a12` and `a13` (call trees kept in `a12-inittest-calls.txt`, `a13-inittest-f84-calls.txt`): transmit power control off, then `wlc_phy_cals_acphy`; in it `sub_0b1227` without `sub_0affa9` when `pi+0xf84` = 0, `sub_0affa9` followed by `sub_0b1227` when `pi+0xf84` = 1; `wlc_phy_rxcore_setstate_acphy` at the start and at the end | as specified |
| `wlc_phy_deaf_acphy`, `wlc_phy_get_deaf_acphy` | direct calls `a1-call-00..06`: result 0, after deaf(1) 1, a second deaf(1) makes no PHY access, deaf(0), result 0, a second deaf(0) makes no PHY access | as specified |
| `wlc_phy_rxcore_getstate_acphy`, `wlc_phy_rxcore_setstate_acphy` | direct calls `a1-call-07..10`: 3, set 1, 1, set 3 | as specified |
| `wlc_phy_update_rxldpc_acphy` | direct calls `a1-call-11..13`: value 1 (unchanged) no access; 0 and 1: read and plain write of `PHY(0x1b0)` | as specified |
| `wlc_acphy_set_scramb_dyn_bw_en` | direct calls `a1-call-14`, `15` | as specified |
| `sub_092500` | direct calls `a1-call-16..18` with `SHM(0x184)` = 0, 0xdead, 0x1234: results 0, 1 (and a write of 0), 0 | as specified |

## Open questions

* The meaning of nearly all PHY registers written here is unknown; the
  interpretations in "Data" are marked as such.
* What `PHY(0x000)` & 0xf reads on the real card (it selects the regulator
  value of `sub_0b018f` step 2; the model returns the PHY revision).
* The RF sequencer status `PHY(0x403)` and the trigger register `PHY(0x402)`
  are not modelled by the card model (the status reads 0, the trigger bits
  stay set). Whether the real sequencer finishes within 200 ms is not known;
  the code does not notice a timeout.
* `pi+0x18f`, `pi+0xa8c`, `pi+0x1091`, `pi_ac+0x32e`: written or tested by the
  functions specified here, purpose unknown.
* What `PHY(0x00b)` & 7 reads on the real card (0 in the model). It decides
  whether the channel function calls `wlc_phy_rxcore_setstate_acphy` at
  initialisation (H7); with any value other than 2 it does.
* Whether a PHY register address with bit 12 set addresses the register of
  all cores (assumed in `acphy-radio`) cannot be decided in the emulator,
  which keeps such addresses as separate registers.
* The block of the shared memory that `SHM(0x92)` points to (`pi+0xfa6`) is
  not used by the functions specified here; its content is unknown.
* `wlc_phy_cals_acphy` and the measurement `sub_0af7f4` were only looked at
  for the call sites of sections 6 and 7; the values measured in the emulator
  (idle TSSI 0) say nothing about the hardware.
* Why the static table set writes tables 0x40/0x60/0x80 twice and uses the
  data of core 0 for table 0x61 is unknown (it is what the object does).
