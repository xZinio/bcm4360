# AC-PHY receive calibration, IQ estimation, temperature sense, RSSI

Behaviour of the receive-side calibration and measurement functions of the
AC-PHY for chip 0x4360 (AC-PHY revision 0/1, radio 2069 major revision 0,
revisions 3/4, two cores/chains, Apple board 0x106b:0x117). Siblings 0x4352
and 43526 (0xaa06) share the 0x4360 branch of every chip-id switch here
(stated where they differ). Revisions >= 2 (other chips: 2/5/6, and rev 3)
take different arithmetic; the branches that apply to 0/1 are described and
the others noted in one line.

Register-access notation (`PHY(a)`, `RADIO(a)`, `TBL(id)[i]`, `D11(o)`,
`mod(reg,mask,val)`) is defined in `access.md`. The PHY state layout (`pi`,
`pi_ac`, `sh`) and every SROM variable read at attach (`rawtempsense`,
`tempthresh`, `temps_hysteresis`, `tempoffset`, `rxgainerr*`, `rssicorr*`, ...)
are in `acphy-attach.md`; this document refers to those fields by their
`acphy-attach.md` names and does not repeat their derivation.

## Scope

| Function | .text offset | Size | Name |
|---|---|---|---|
| `sub_0addfa` | 0x0addfa | 6650 | assigned: `wlc_phy_rxiqcal_acphy` |
| `sub_0ad89a` | 0x0ad89a | 1376 | assigned: `wlc_phy_rxcal_txgain_setup_acphy` |
| `sub_094b1a` | 0x094b1a | 2551 | assigned: `wlc_phy_rxcal_radio_setup_acphy` |
| `sub_095511` | 0x095511 | 605 | assigned: `wlc_phy_rxcal_radio_restore_acphy` |
| `sub_097809` | 0x097809 | 435 | assigned: `wlc_phy_calc_iqcc_acphy` |
| `sub_09cf53` | 0x09cf53 | 1276 | assigned: `wlc_phy_rxcal_phy_restore_acphy` |
| `sub_090fcb` | 0x090fcb | 176 | assigned: `wlc_phy_rxiqcc_acphy` (RX IQ-coeff accessor) |
| `sub_09107b` | 0x09107b | 477 | assigned: `wlc_phy_rxcal_digfilt_acphy` |
| `wlc_phy_rx_iq_est_acphy` | 0x0932e6 | 1321 | original |
| `sub_092ffa` | 0x092ffa | 289 | assigned: `wlc_phy_rx_iq_est_gain_pulse_acphy` |
| `wlc_phy_tempsense_acphy` | 0x0a317c | 5667 | original |
| `sub_09bbe4` | 0x09bbe4 | 559 | assigned: `wlc_phy_tempsense_paldo_setup_acphy` |
| `sub_093ebe` | 0x093ebe | 182 | assigned: `wlc_phy_tempsense_sample_setup_acphy` |
| `sub_09be13` | 0x09be13 | 390 | assigned: `wlc_phy_tempsense_paldo_restore_acphy` |
| `wlc_phy_stf_chain_temp_throttle_acphy` | 0x0a479f | 200 | original |
| `wlc_phy_upd_gain_wrt_temp_phy` | 0x0b2bb1 | 259 | original (wlc_phy_cmn.c) |
| `wlc_phy_get_tempsense_degree` | 0x0b537d | 38 | original (wlc_phy_cmn.c); not on the AC-PHY path (see Notes) |
| `wlc_phy_rssi_compute_acphy` | 0x096ee4 | 954 | original |
| `wlc_phy_11b_rssi_WAR` | 0x08efa4 | 226 | original; not reached on rev 0/1 (see Notes) |

Helpers described elsewhere and only referenced here: `phy_reg_*`,
`read/write/mod_radio_reg`, `wlc_phy_table_{read,write}_acphy`,
`wlc_phy_write_table_ext` (access.md); `wlc_phy_get_chan_freq_range_acphy`
(acphy-attach.md Data, sub-band index); `wlc_phy_stay_in_carriersearch_acphy`,
`wlc_phy_force_rfseq_acphy` (acphy-init.md); `wlc_phy_tx_tone_acphy`,
`wlc_phy_stopplayback_acphy`, `wlc_phy_txpwr_by_index_acphy` (tx-cal, not yet
specified - their observable effect used here is stated); `wlc_phy_nbits`,
`wlc_phy_sqrt_int`, `wlc_phy_cordic`, `wlc_phy_inv_cordic`, `qm_div16`
(integer maths helpers, wlc_phy_cmn.c).

## Overview

These functions are all *post-bring-up*: none of them runs in the
attach/up trace. They are triggered by the calibration scheduler
(`wlc_phy_cals_acphy` -> `sub_0addfa`), by the periodic-calibration path and
watchdog (`wlc_phy_tempsense_acphy` via `wlc_phy_cal_perical` and
`wlc_phy_stf_chain_temp_throttle_acphy`), and by the receive path
(`wlc_phy_rssi_compute_acphy` for every received frame).

Four independent pieces:

1. **Receive IQ imbalance calibration** (`sub_0addfa`): puts the radio and
   PHY into a TX->RX loopback, plays a single tone through the transmitter,
   measures the received I/Q power with `wlc_phy_rx_iq_est_acphy`, derives the
   compensation coefficients (a, b) per core from the measured powers with
   fixed-point arithmetic, writes them to the receive compensation tables, and
   restores every register it changed.

2. **IQ power estimation** (`wlc_phy_rx_iq_est_acphy` + `sub_092ffa`): a
   generic primitive that runs the PHY's "RX IQ estimate" block for a chosen
   number of samples and reads back the accumulated I^2, Q^2 and I*Q sums per
   core. Used by the calibration above and elsewhere.

3. **Temperature sense** (`wlc_phy_tempsense_acphy` + 3 helpers): routes the
   radio's temperature/PTAT diode to an auxiliary ADC, takes a differential
   reading, converts it to degrees Celsius, and stores the result in
   `pi_ac+0x8dc`. `wlc_phy_stf_chain_temp_throttle_acphy` uses it to switch
   transmit chains off when the chip overheats. `wlc_phy_upd_gain_wrt_temp_phy`
   turns the stored temperature into an RSSI gain correction.

4. **Signal strength** (`wlc_phy_rssi_compute_acphy` + `wlc_phy_11b_rssi_WAR`):
   turns the per-core power bytes of a received frame's PHY status header into
   a signed dBm value, applying gain-error, per-sub-band and temperature
   corrections and combining the cores.

## Data

### Constants

| Where | Value | Meaning |
|---|---|---|
| `.rodata+0x2cdab8` | bytes `01 00 01 00` | tempsense phase table A: `RADIO(0x0e)` bit 1 per measurement phase |
| `.rodata+0x2cdabc` | bytes `00 00 01 01` | tempsense phase table B: `RADIO(0x0e)` mask-0x4 value per phase (all entries have bit 2 = 0, so bit 2 is cleared every phase) |
| tempsense | 0x223e = 8766 | slope numerator (rev 0/1) |
| tempsense | 0x1d0614 = 1901076 | offset added before the final `/0x4000` (rev 0/1) |
| tempsense | 0x4000 = 16384 | final divisor (all revs) |
| tempsense | `{0x20f,0x209,0x20a}` = `{527,521,522}` | per-core slope divisor (cores 0,1,2), array `local_58` |

With all measured ADC readings equal (the model returns a constant), the
rev 0/1 result is `tempoffset + 0x1d0614/0x4000 = tempoffset + 116` degrees.

### Structure fields identified here

| Struct | Offset | Size | Field |
|---|---|---|---|
| `pi_ac` | 0x8dc | s16 | last measured temperature in degrees C (init from `rawtempsense` at attach; overwritten by `wlc_phy_tempsense_acphy`; read by `wlc_phy_upd_gain_wrt_temp_phy`) |
| `pi_ac` | 0x8e1 | 1 | tempsense: enable PA-LDO save/override path (0 on this board) |
| `pi_ac` | 0x254..0x295 | 2 each | tempsense radio-register save area (per core) |
| `pi_ac` | 0x296 | 2 | tempsense `PHY(0x19e)` save |
| `pi_ac` | 0x298..0x2f7 | 2 each | tempsense PHY-register save area (per core) |
| `pi_ac` | 0x900 | 1 | `wlc_phy_rx_iq_est_acphy` "skip gain wait" flag on 2.4 GHz (0) |
| `pi_ac` | 0x901 | 1 | same, on 5 GHz (1) |
| `pi` | 0x212 | 3 x s8 | per-core RX gain reference used by `wlc_phy_rssi_compute_acphy` (temperature-corrected; purpose otherwise unconfirmed) |

## Procedures

### sub_0addfa (.text+0xaddfa, assigned: wlc_phy_rxiqcal_acphy)

Purpose: the receive IQ-imbalance calibration. Sets up a TX->RX loopback,
plays one or more single tones through the transmitter, measures the received
I/Q powers, fits a gain ratio and phase per core over the tones, and writes the
2x2 correction as an (a, b) pair per core to the RX compensation registers.
Called by `wlc_phy_cals_acphy`.

Inputs: `pi`. Reads `pi+0x164` (phy rev), `pi+0x160` (phy type), `pi+0x17e`
(chanspec: band bits 0xc000, bandwidth bits 0x3800), `pi+0x16c` (radio rev),
`pi+0x16e` (radio major rev), `pi+0x168` (cores), `sh+0xa5/0xa7` (rx chain),
`pi+0x116a`.
Outputs: RX IQ compensation registers `PHY(c*0x200+0x6a0)` (a) and
`PHY(c*0x200+0x6a1)` (b) per core; optional digital-filter coefficients; the
whole PHY/radio register set it touched is restored.

Overall sequence:

1. **Flags.** `img = pi_ac+0x8d8 = 1` (do the image-rejection second estimate),
   except set to 0 for phy rev 5/2/6/3, and for phy rev 1 on 5 GHz
   (`pi+0x17e & 0xc000 == 0xc000`). On the BCM4360 2.4 GHz path `img = 1`.
2. `wlc_phy_stay_in_carriersearch_acphy(pi,1)`; `wlc_phy_force_rfseq_acphy(pi,2)`.
3. For each core: `sub_090fcb(pi,2,0,core)` -> zeroes `PHY(c*0x200+0x6a0)` and
   `PHY(c*0x200+0x6a1)` (start from unity/no correction).
4. `sub_09107b(pi,0)` -> `mod(PHY(0x211),1,0)` and clears the per-core
   digital-filter accumulators `pi_ac+0x8c8+4c`.
5. For each active core: `sub_09bf99(pi,0,save,8,core)` (save current RX gain,
   tx-power spec) then `sub_09bf99(pi,1,gains,8,core)` with `gains` read from
   the cal state `*(pi+0xfb8)+0x54+4c` (apply a fixed measurement gain).
6. Compute the bandwidth config word `W` and `bw_index` (`local_21c`) exactly
   as in tempsense step 1 (`bw==0x2000`: `W=0x7f8`, idx 2; `bw==0x1800`:
   `W=0x43e9(+0x201 if pi+0x116a==0)`, idx 1; else `W=0xd5eb`, idx 0).
7. **Save + override the PHY RX path**, per core `c` (base `b=c*0x200`): read
   and store ~26 registers into a `pi_ac` scratch save area
   (`0x720..0x73e,0x747,0x678`, plus `PHY(0x40f)`->`pi_ac+0x24c`,
   `PHY(0x401)`->`pi_ac+0x23a`, and gain-table words via
   `TBL(7)[c+0x100/0x103/0x106]` and `sub_098751`), then a long fixed sequence
   of `mod`/`write` that: forces the RX front end and ADC on, distributes `W`
   into `PHY(b+0x739)/(b+0x73a)/(b+0x725)` bit-for-bit exactly as tempsense
   step 3, reads a gain code from `TBL(7)[...]` (index depends on `bw_index`:
   `c*2+0x441` for 40 MHz else `c*0x10+0x140+bw_index`) and programs it into
   `PHY(b+0x735)/(b+0x723)/(b+0x737)`, and disables the receiver's own gain
   control. `PHY(0x401)` bits 0..2 set to `sh+0xa5` (rx chain), bits 12..14
   cleared. (The full bit list is a fixed override table; see the trace
   reference in Verification.)
8. `sub_094b1a(pi)` -> radio loopback setup (below).
9. `sub_0ad89a(pi)` -> RX-gain search / gain selection (below).
10. Build the tone list `local_48` from the bandwidth: 20 MHz -> `{+8,-8}`
    (2 tones); 40 MHz -> extra tones and `pi_ac+0x8c4=1` (two-tone / filter
    fit); the amplitude byte `A` = 8 (20 MHz) / 4 (0x1800) / 2 (else); tone
    count `uVar4` = 2 (or 4/6 with `pi_ac+0x8c4`).
11. **Tone loop**, for each tone index `k` (amplitude `A_k = local_48[k]`):
    a. `wlc_phy_tx_tone_acphy(pi, (s16)(A_k*1000)>>1, 0xb5, 0,0,0)` - plays a
       tone at `A_k*500` (freq units) with amplitude 0xb5.
    b. `wlc_phy_rx_iq_est_acphy(pi, est0, 0x4000, ...)` (0x3000 if
       `pi_ac+0x8c4`).
    c. If `img == 1`: `sub_095511(pi)` (radio restore), a second
       `wlc_phy_rx_iq_est_acphy(pi, est1, 0x4000)`, `sub_094b1a(pi)` (radio
       setup again) - the pair with/without the loopback path gives the image
       component.
    d. `wlc_phy_stopplayback_acphy(pi)`.
    e. For each core `c`: take `{I*Q, I^2, Q^2} = est0[c]`; `sub_097809` (below)
       -> per-tone phase angle and gain ratio. If `img==1`, also `sub_097809`
       on `est1[c]` and combine the two with `wlc_phy_nbits`/`wlc_phy_sqrt_int`/
       `wlc_phy_inv_cordic` (image rejection) into the final angle. Store the
       angle in `aiStack_1f4[k*9+c]` and the gain ratio (`local_98[1]`) in
       `aiStack_1e4[k*9+c]`.
12. **Regression + coefficient build** (after all tones). Per core `c`, over
    the `uVar4` tones:
    ```
    Sxx  = sum_k A_k^2
    Sy   = sum_k angle[k][c]                 # local_98[c]
    Sxy  = sum_k A_k * angle[k][c]           # local_88[c]
    Sg   = sum_k gainratio[k][c]             # local_a8[c]
    gain = (Sg + uVar4/2) / uVar4            # average gain ratio (rounded)
    ang  = round(Sy / uVar4)                 # average angle (sign-aware rounding)
    (cos,sin) = wlc_phy_cordic(ang)          # fixed point
    a = ((gain * cos >> 15) + 1) >> 1,  masked to 10 bits   -> PHY(c*0x200+0x6a0)
    b = ((gain * sin >> 15) + 1) >> 1,  masked to 10 bits   -> PHY(c*0x200+0x6a1)
    ```
    If `pi_ac+0x8c4` (two-tone): also a per-core digital-filter coefficient
    `pi_ac+0x8c8+4c = ((-Sxy * K) / Sxx >> 14 + 1) >> 1` with `K = 0x1e`
    (20 MHz) / `8` (0x2000) / `0xf` (0x1800), plus a remainder term.
13. **Write + restore.** For each core `sub_090fcb(pi,1,ab,core)` writes
    `a`->`PHY(c*0x200+0x6a0)`, `b`->`PHY(c*0x200+0x6a1)`. If `pi_ac+0x8c4`:
    `sub_09107b(pi,1)` writes the digital-filter taps. `sub_095511(pi)` restores
    the radio; `sub_09cf53(pi)` restores the ~26 PHY registers per core and
    finalizes; for each active core `sub_09bf99(pi,1,save,8)` restores the RX
    gain; `wlc_phy_stay_in_carriersearch_acphy(pi,0)`. Return 0.

Verification: ran the whole function in the emulator (58433 accesses); it
completes and writes `PHY(0x6a0)=PHY(0x6a1)=0` for both cores (the model returns
0 for every I/Q power register, so `gain`/`angle` degenerate to a zero
correction). Because measured powers are 0, only the *structure* and the
register targets are confirmed here, not the non-zero coefficient scaling.

### sub_097809 (.text+0x97809, assigned: wlc_phy_calc_iqcc_acphy)

Purpose: from one IQ-estimate record `{I*Q, I^2, Q^2}` compute the phase angle,
the gain ratio and the quadrature term of the receiver's IQ imbalance, in fixed
point. Inputs `param_1` = `int[3]` = `{IQ, I2, Q2}`; outputs `param_2` =
`int[3]` = `{angle, gainratio (Q10, 0x400 = 1.0), sinterm}`.

Steps (all shifts are the exact ones in the code; `nbits(x)` = position of the
highest set bit, `sqrt_int` = integer square root, `inv_cordic` = atan2):
1. `n = max(nbits(I2), nbits(Q2))`; normalise so `sqrt(I2)` and `sqrt(Q2)` are
   computed at a common scale: `rI = sqrt_int(I2 << (0x1e-n))`,
   `rQ = sqrt_int(Q2 << (0x1e-n))` (or `>> (n-0x1e)` when `n>=0x1f`);
   `denom = rI*rQ` shifted to `IQ`'s scale using `nbits(IQ)`.
2. `sinterm = -IQ (scaled) / denom` with round-to-nearest (`+/- denom/2`),
   0 if `denom == 0`; `param_2[2] = sinterm`.
3. `cos = sqrt_int(0x40000000 - (sinterm>>1)^2)`; `angle =
   inv_cordic(cos*2, sinterm)`; `param_2[0] = angle`.
4. Gain ratio: `d = Q2 - I2`, scaled by `nbits(d)` rounded to even; if
   `I2 == 0` -> `param_2[1] = 0x400` (unity), else `param_2[1] =
   sqrt_int(d_scaled / I2 + 0x100000, ...)` - i.e. `sqrt(1 + (Q2-I2)/I2)` in
   Q10, the RX amplitude imbalance.

### sub_090fcb (.text+0x90fcb, assigned: wlc_phy_rxiqcc_acphy)

The RX IQ compensation-coefficient accessor, per core `c` (base `c*0x200`):
* mode 0 (save): read `PHY(c*0x200+0x6a0)` and `PHY(c*0x200+0x6a1)` into the
  buffer.
* mode 1 (write): `PHY(c*0x200+0x6a0) = buf[0]` (a), `PHY(c*0x200+0x6a1) =
  buf[1]` (b).
* any other mode (2): write 0 to both (disable correction).

### sub_09107b (.text+0x9107b, assigned: wlc_phy_rxcal_digfilt_acphy)

RX digital notch/filter-coefficient setup, using an 0xf2-byte constant table at
`.rodata` (`DAT_0054da90`). mode 0 clears `PHY(0x211)` bit 0 and the per-core
accumulators `pi_ac+0x8c8+4c`. mode 1 (after the cal): per core it clamps the
computed slope `pi_ac+0x8c8+4c` to +/-10, programs `PHY(0x210)/0x211/0x212`
(mux/enable), and writes 11 taps to `PHY(c*0x200+0x6a4..0x6ae)` from the table
(indexed by the clamped slope, forwards or reversed by its sign), then sets
`PHY(0x211)` bit 0.

### sub_094b1a (.text+0x94b1a, assigned: wlc_phy_rxcal_radio_setup_acphy)

Radio TX->RX loopback setup. Sets `pi_ac+0x11a = 1`. Per core `c` (radio stride
`c<<9`), for the 4360 it saves radio registers `0x20,0x21,0x22,0x23,0x3a,0x3d`
into `pi_ac+0x11c/0x124/0x12c/0x134/0x13c/0x144 (+2c)`, writes 0 to all six,
then applies band-dependent overrides (2.4 GHz uses `0x22/0x3a/0x20`, 5 GHz uses
`0x23/0x3d/0x21`): clear a "PA on" bit (mask 0x100), clear mask 0x10 and 0x20,
set/clear mask 4 by radio rev (rev in {2,3,4,0x12,0x18,0x1a,0x22,8} -> 0, our
rev 4 -> 0), set the loopback enable (2.4 GHz `RADIO(0x22)` mask 0x200; 5 GHz AC
`RADIO(0x23)` mask 0x200 then 0x80/0x80), and clear the low mixer bits (mask 3
then mask 6). (Siblings 0x4352/0xa9c4/0xaa06 identical; other chips use radio
`0x25/0x26/0x27/0x28/0x41/0x44`.)

### sub_095511 (.text+0x95511, assigned: wlc_phy_rxcal_radio_restore_acphy)

Inverse of `sub_094b1a`: sets `pi_ac+0x11a = 0` and writes the six saved radio
registers (`0x20,0x21,0x22,0x23,0x3a,0x3d` for the 4360) back from
`pi_ac+0x11c..0x144`, per core. (Phy rev 5/2/6 also toggle `PHY(0x19e)`.)

### sub_0ad89a (.text+0xad89a, assigned: wlc_phy_rxcal_txgain_setup_acphy)

Purpose: the gain selection for the measurement - an iterative search that sets
each core's RX gain so the loopback tone lands in a target power window. Uses
two 0x42-byte gain tables in `.rodata` (`DAT_0054dbe0` for 5 GHz, `DAT_0054db90`
for 2.4 GHz), each a list of `{value0, value1, value2}` triples per gain index.
Steps: read a coarse gain seed from `PHY(c*0x200+0x6dc)` bits 7..10 per core;
then loop: play a fixed tone (`wlc_phy_tx_tone_acphy`, freq 4000/2000/1000 by
bandwidth, amplitude 0xb5), `wlc_phy_rx_iq_est_acphy(pi,buf,0x400,0x20,0,0)`,
`wlc_phy_stopplayback_acphy`; per core compute `p = (I2+0x200>>10) +
(Q2+0x200>>10)` and step the gain index up/down (bounds `0xb58` low, `0x169e`
high) until `p` is in-window or clamped; when settled, write the chosen table
triple to `PHY(c*0x200+0x730)` (with the seed), `PHY(c*0x200+0x731)`,
`PHY(c*0x200+0x734)` and either `wlc_phy_txpwr_by_index_acphy(...)` or, when the
triple's third field is 0xff, `TBL(7)[c+0x100/0x103/0x106]`. Returns when every
core is settled (`local_58[c] != 0`).

### sub_09cf53 (.text+0x9cf53, assigned: wlc_phy_rxcal_phy_restore_acphy)

The PHY restore + finalize after the cal (the counterpart to step 7's save). `mod(PHY(0x19e),2,2)`; `pi_ac+0x14c=0`;
`PHY(0x401) = pi_ac+0x23a` (saved). Per core: rewrite the gain tables
`TBL(7)[c+0x100/0x103/0x106]` from the saved words, restore the tx index via
`wlc_phy_txpwr_by_index_acphy`, `sub_09c4e4`, and write back all ~26 saved PHY
registers (`0x678,0x720..0x73e,0x747`). Then `PHY(0x40f) = pi_ac+0x24c`
(saved), `wlc_phy_force_rfseq_acphy(pi,2)`, `mod(PHY(0x19e),2,old)`.

### wlc_phy_rx_iq_est_acphy (.text+0x932e6, name original)

Purpose: run the PHY receive IQ-estimate accumulator for a number of samples
and read back the per-core power sums.

Inputs:
* `pi`.
* `param_2`: address of an output array of `N` records of 12 bytes (N = number
  of cores, `pi+0x168`); each record = three 32-bit words `{I*Q, I^2, Q^2}`.
* `param_3` (u16): sample count, written to `PHY(0x272)`.
* `param_4` (u8): written to bits 0..7 of `PHY(0x271)` (wait/decimation).
* `param_5` (byte, low byte used): `(param_5*2) & 0x1fe` written to bits 1..8
  of `PHY(0x270)` (gain settle count).
* `param_6` (byte): "gain change allowed" flag.

State read: `pi+0x168` (cores), `pi+0x17e` (chanspec, to pick `pi_ac+0x900`
for 2.4 GHz or `pi_ac+0x901` for 5 GHz into `bVar12`), `sh+0xa7` (rx chain
mask, only in the gain-change branch).

Steps:
1. Choose `g = pi_ac+0x900` if the chanspec band bits (`pi+0x17e & 0xc000`)
   are 0 (2.4 GHz), else `g = pi_ac+0x901`.
2. `sub_092ffa(pi)` (gain pulse, below).
3. `PHY(0x272) = param_3`; `mod(PHY(0x271), 0xff, param_4)`;
   `mod(PHY(0x270), 0x2, (param_5*2) & 0x1fe)`.
4. If `(g & param_6) == 0` (the common case, gain change not required):
   a. `mod(PHY(0x270), 0x1, 1)` to start the estimate.
   b. Poll `PHY(0x270)` bit 0, every 10 us, up to 1000 iterations (loop
      counter 0x2719 down by 10 to 9); stop when bit 0 clears. (Model note:
      bit 0 never clears, so the loop always times out.)
   c. Re-read `PHY(0x270)`; if bit 0 is now clear, for each core `c` in
      `0..N-1`: read the three power sums (16-bit high word first) and pack:
      `rec[c].word1 = PHY(c*0x200+0x6c3)<<16 | PHY(c*0x200+0x6c2)` (= I^2),
      `rec[c].word2 = PHY(c*0x200+0x6c5)<<16 | PHY(c*0x200+0x6c4)` (= Q^2),
      `rec[c].word0 = PHY(c*0x200+0x6c1)<<16 | PHY(c*0x200+0x6c0)` (= I*Q).
5. Else (gain change allowed): for each core `c` it first forces, on every
   *other* active core `d != c` (rx-chain bit set in `sh+0xa7`), the RX front
   end into a fixed state - `mod(PHY(d*0x200+0x720),1,1)`,
   `mod(PHY(d*0x200+0x728),1,0)`, `mod(PHY(d*0x200+0x721),4,4)`,
   `mod(PHY(d*0x200+0x729),2,0)` - saving each register's previous value on a
   stack; then `osl_delay(1)`, starts and polls the estimate as in 4a/4b,
   restores the saved registers in reverse order, and reads the three power
   sums for core `c` as in 4c. (This branch is not exercised by the callers on
   this board; described for completeness.)

Result: none (void); output is the array at `param_2`.

Verification: called directly in the emulator; produced 3065 accesses, output
words all 0 (the model returns 0 for the power registers and never sets the
"done" bit, so step 4b times out and step 4c still runs because bit 0 reads 0).

### sub_092ffa (.text+0x92ffa, assigned: wlc_phy_rx_iq_est_gain_pulse_acphy)

Purpose: momentarily force a bit pattern into three per-core RX-gain registers,
then restore them - a settling pulse before an IQ estimate/tempsense run.

Steps: for each core `c` in `0..N-1`, save and OR-in:
`PHY(c*0x200+0x739) |= 0x80`, `PHY(c*0x200+0x73a) |= 0x80`,
`PHY(c*0x200+0x725) |= 0x204` (saving the pre-write value of each on a stack).
Then `osl_delay(1)`; write every saved value back in reverse order (restoring
the three registers per core); `osl_delay(1)`.

Notes: the writes use `phy_reg_write` (full 16-bit writes of `old|mask`), so
after the function the three registers hold exactly their entry values again.

### wlc_phy_tempsense_acphy (.text+0xa317c, name original)

Purpose: measure the die temperature via the radio's PTAT/temperature diode and
an auxiliary ADC, convert to degrees C, store in `pi_ac+0x8dc`, return it.

Inputs: `pi`. Reads `tempoffset` (`pi+0xc35`), `pi+0x164` (phy rev),
`pi+0x16e` (radio major rev, 0 here), `pi+0x168` (cores), `sh+0xa7` (rx chain
mask), `pi+0x17e & 0x3800` (bandwidth), `pi+0x116a`.
Outputs: `*(s16)(pi_ac+0x8dc)` = measured temperature; returns the same value
(`tempoffset + degrees`).

The whole body runs between `wlapi_suspend_mac_and_wait` + `wlc_phyreg_enter`
and `wlc_phyreg_exit` + `wlapi_enable_mac`.

Steps (BCM4360, radio major rev 0, phy rev 0/1):

1. Compute a bandwidth-dependent config word `W` (`uVar20`) from the chanspec
   bandwidth bits `bw = pi+0x17e & 0x3800`:
   * `bw == 0x2000` (40 MHz): `W = 0x7f8`.
   * `bw == 0x1800`: `W = 0x43e9`, plus `0x201` if `pi+0x116a == 0`
     (i.e. `0x45ea` when `pi+0x116a==0`).
   * otherwise (incl. `0x1000` 20 MHz, `0x3000` 80 MHz): `W = 0xd5eb`.
2. Save `PHY(0x19e)` into `pi_ac+0x296`; clear its bits 6, 7, 8
   (`mod(0x19e,0x40,0)`, `mod(0x19e,0x80,0)`, `mod(0x19e,0x100,0)`).
3. For each active core `c` (bit `c` set in `sh+0xa7`): base `b = c*0x200`.
   Save 14 PHY registers into the `pi_ac` save area
   (`0x73e,0x727,0x73c,0x721,0x729,0x720,0x728,0x724,0x736,0x725,0x739,0x73a,
   0x722,0x734`, each at `b`), and immediately write `PHY(b+0x73e)=0x440`.
   Then program the front end for the temperature read:
   * `mod(PHY(b+0x727),0x2,0x2)`, `mod(PHY(b+0x73c),0xe,0x2)`,
     `mod(PHY(b+0x727),0x1,0x1)`, `mod(PHY(b+0x73c),0x1,0x1)`.
   * `mod(PHY(b+0x721),0x100,0x100)`, `mod(PHY(b+0x729),0x100,0)`.
   * `mod(PHY(b+0x720),0x20,0x20)`, `mod(PHY(b+0x728),0x20,0x20)`,
     `mod(PHY(b+0x720),0x40,0x40)`, `mod(PHY(b+0x728),0x40,0)`,
     `mod(PHY(b+0x720),0x10,0x10)`, `mod(PHY(b+0x728),0x10,0x10)`.
   * `PHY(b+0x736)=0x154`, `PHY(b+0x724)=0x3ff`.
   * Distribute `W` into the ADC-clock/mux fields:
     `mod(PHY(b+0x73a),0x7,W&7)`, `mod(PHY(b+0x725),0x20,0x20)`,
     `mod(PHY(b+0x739),0x7e,(W>>2)&0x7e)`, `mod(PHY(b+0x725),0x2,0x2)`,
     `mod(PHY(b+0x73a),0x8,(W>>6)&0x8)`, `mod(PHY(b+0x725),0x40,0x40)`,
     `mod(PHY(b+0x73a),0x10,(W>>6)&0x10)`, `mod(PHY(b+0x725),0x80,0x80)`,
     `mod(PHY(b+0x73a),0x60,(W>>6)&0x60)`, `mod(PHY(b+0x725),0x100,0x100)`.
   * `mod(PHY(b+0x734),0x7,0)`, `mod(PHY(b+0x722),0x4,0x4)`.
4. For each active core `c` (radio per-core stride `c<<9`): save 7 radio
   registers into `pi_ac` and override them. For the 4360 the radio addresses
   are `0x16e` (save->`pi_ac+0x254`), `0xe` (->`0x274`), `0x161` (->`0x264`),
   `0x17` (->`0x27c`), `0x15f` (->`0x26c`), `0x24` (->`0x284`),
   `0x25` (->`0x28c`) (siblings 0x4352/0xa9c4/0xaa06 identical; other chips use
   `0x181/0x13/0x174/0x1c/0x172/0x29/0x2a`). Overrides:
   `mod(RADIO(0x161),0x4000,0x4000)`, `mod(RADIO(0xe),0x1,0x1)`,
   `mod(RADIO(0x161),0x1000,0x1000)`, `mod(RADIO(0x17),0x1,0x1)`,
   `mod(RADIO(0x17),0x2,0)`, `mod(RADIO(0x15f),0x2000,0x2000)`,
   `mod(RADIO(0x25),0x3ff,0x91)`, `mod(RADIO(0x15f),0x4000,0x4000)`,
   `mod(RADIO(0x24),0x700,0x300)` (all `| c<<9`). (Radio major rev 1 only:
   two extra `mod(RADIO(0x24), 0x30,0)` and `mod(RADIO(0x24),0xe,2)` - not this
   board.)
5. Clear the 16-word accumulator array; `sub_09bbe4(pi,...)` (PA-LDO / DAC
   save+setup, below - a no-op on this board beyond saving `PHY(0x40f)` bit 9
   and `PHY(0x394)`); `sub_092ffa(pi)` (gain pulse).
6. **Measurement**, per active core `c` (counting `n` = number processed):
   a. `sub_093ebe(pi, 0x10+c, 1)` arms the sample collector (below).
   b. Four phases `p = 0..3`, reading tables A/B (`.rodata+0x2cdab8/2cdabc`):
      * `mod(RADIO(0x16e|c<<9), 0x2, 0x2)`.
      * `mod(RADIO(0x0e|c<<9), 0x2, A[p]*2)` - sets bit 1 to `A[p]` (=1,0,1,0).
      * `mod(RADIO(0x16e|c<<9), 0x1, 0x1)`.
      * `mod(RADIO(0x0e|c<<9), 0x4, B[p])` - clears bit 2 (B[p] & 4 = 0).
      * `osl_delay(10)`.
      * Read `PHY(0x13)` eight times; each reading `v` contributes
        `t(v) = (v>>2) - (0x400 if (v>>2) >= 0x200 else 0)` (a 10-bit signed
        ADC value); accumulate the eight and store `sum>>3` (the average) into
        `m[c][p]` (`local_78[c + 3*p]`).
7. **Convert** (phy rev 0/1, i.e. not 3, not 5/2/6):
   ```
   acc = 0
   for each active core c (n of them):
       diff = m[c][3] + m[c][1] - m[c][2] - m[c][0]
       term = (((diff * 8766) / 2) << 8) / DIV[c]     # DIV = {527,521,522}
       acc += term / n                                 # 32-bit signed / n
   acc += 1901076
   degrees = acc / 16384
   temperature = tempoffset + degrees
   ```
   (Rev 3 / 5 / 2 / 6 use only core 0's four samples and a different
   slope/offset, and do NOT add `tempoffset`; not this board.)
8. Restore `PHY(0x19e)` from `pi_ac+0x296`; restore the 14 PHY registers and
   7 radio registers of each active core; `sub_09be13(pi,...)` (PA-LDO / DAC
   restore); `wlc_phyreg_exit`; `wlapi_enable_mac`.
9. `*(s16)(pi_ac+0x8dc) = temperature`; return `temperature`.

Notes:
* The differential `m3+m1-m2-m0` cancels any constant ADC offset: it is the
  PTAT reading with `RADIO(0x0e)` bit 1 toggled on (phases 0,2) versus off
  (phases 1,3). Because the model returns a constant, `diff = 0` and the result
  is exactly `tempoffset + 116` for every register setting.
* `PHY(0x13)` is the shared auxiliary-ADC readout register (10-bit signed in
  bits 2..11).
* SROM variables: on the BCM4360 the AC-PHY tempsense uses only `tempoffset`
  (`pi+0xc35`, added in step 7) and, indirectly, `rawtempsense` (`pi+0x210`,
  the reference in `wlc_phy_upd_gain_wrt_temp_phy`). The slope constant is the
  fixed 8766 above - `tempsense_slope`, `tempcorrx`, `tempsense_option` and
  `temps_period` are **not** read on this path (per acphy-attach.md they are
  read only by other PHY types' attach, or outside the PHY layer). `tempthresh`
  and `temps_hysteresis` are used by the throttle function, not by tempsense
  itself. `phycal_tempdelta` gates when the calibration scheduler re-runs
  `sub_0addfa` (calibration spec, not here).

### sub_093ebe (.text+0x93ebe, assigned: wlc_phy_tempsense_sample_setup_acphy)

Arms the PHY "sample collect" block for one gain/mux code. Steps:
`PHY(0x392)=0`, `PHY(0x393)=0`; clear bits 14..15 of `D11(0x120)`
(`osl_readl`/`osl_writel` of the MAC-control register); `D11(0x49e)=0` (16-bit);
`PHY(0x394) = (param_3<<8) | param_2` (called with `param_2 = 0x10+c`,
`param_3 = 1`, so `0x0110|c`); `PHY(0x392)=0xffff`, `PHY(0x393)=0xffff`.

### sub_09bbe4 / sub_09be13 (.text+0x9bbe4 / 0x9be13, assigned: wlc_phy_tempsense_paldo_setup/restore_acphy)

Save/restore pair around the measurement. `sub_09bbe4` reads `PHY(0x19e)` bit 1
(into `*param_9`), reads `PHY(0x40f)` bit 9 (into `*param_2`) and clears it,
saves `PHY(0x394)` (into `*param_3`). Only if `pi_ac+0x8e1 != 0` (false on this
board) it additionally sets `PHY(0x19e)` bit1, clears/sets `PHY(0x93e)` bits
4/5/12, saves and overrides the PA-LDO regulator through
`si_corereg(sih,0,0x28,...)` and reads/rewrites `TBL(0x0a)[0x29]` and
`TBL(0x0a)[0x39]` (16-bit). `sub_09be13` is the inverse: `mod(PHY(0x19e),2,2)`,
`PHY(0x394)=saved`, (the `pi_ac+0x8e1` block if set), then
`mod(PHY(0x40f),0x200,(saved&0x7f)<<9)` and `mod(PHY(0x19e),2,saved_bit1*2)`.

### wlc_phy_stf_chain_temp_throttle_acphy (.text+0xa479f, name original)

Purpose: switch transmit chains off when the die is too hot, on again when it
has cooled, by editing the active-chain bitmap `pi+0xc33`.

Inputs: `pi`; reads `sh+0xa7` (rx chain mask, kept unchanged as the high
nibble), `sh+0xa6` (tx chain mask), `sh+0xa4` (full chain bitmap), `pi+0xc32`
(over-temperature latch), `pi+0xc2e` (`tempthresh`), `pi+0xc31` (on-again
threshold = `tempthresh - temps_hysteresis`).

Steps:
1. `wlapi_suspend_mac_and_wait`; `t = wlc_phy_tempsense_acphy(pi)`;
   `wlapi_enable_mac`.
2. If not currently throttled (`pi+0xc32 == 0`):
   * if `t < tempthresh` (`pi+0xc2e`): return, nothing changes.
   * else set the latch `pi+0xc32 = 1` and reduce the tx chain mask via the
     table `{1,1,2,1,4,1,2,1}` indexed by the current tx mask `sh+0xa6`
     (mask 1->1, 2->1, 3->1 keep low bit... i.e. drop to a single chain: index
     `m` gives `tbl[m]`, e.g. `3 -> tbl[3] = 1`), new tx mask = `tbl[sh+0xa6]`.
3. If currently throttled (`pi+0xc32 == 1`):
   * if `t > pi+0xc31` (on-again threshold): return.
   * else clear the latch and restore the tx mask to `sh+0xa4` (full bitmap).
4. `pi+0xc33 = (sh+0xa7 << 4) | new_tx_mask` (rx mask in the high nibble, the
   possibly-reduced tx mask in the low nibble).

Verification: called directly; returned 120 (= `tempthresh`), `pi+0xc32`
stayed 0, `pi+0xc33` stayed 0x33 (t=116 < 120, so no throttle).

### wlc_phy_upd_gain_wrt_temp_phy (.text+0xb2bb1, name original, wlc_phy_cmn.c)

Purpose: compute an RSSI gain correction from the temperature drift since the
SROM reference. Writes a signed 16-bit correction to `*param_2`.

For the AC-PHY (`pi+0x160 == 0xb`): `T = (s16)(pi_ac+0x8dc)` (current
temperature). Clamp `T` to `[0, 0x69]` (0..105). `d = T_clamped - rawtempsense`
(`pi+0x210`); if `rawtempsense == 0xff` force `d = 0`. Slope factor `s = 7`
for the AC-PHY. Then
`*param_2 = ( d*2*s + (25 if d>=0 else -25) ) / 50` (rounded to nearest,
`0x19 = 25`, `0x32 = 50`).
(Other PHY types: type 4 measures via `wlc_phy_tempsense_nphy` with slope 6;
type 7 reads `pi_ac+0x300` with slope 8; anything else returns `*param_2 = 0`.)

### wlc_phy_rssi_compute_acphy (.text+0x96ee4, name original)

Purpose: convert a received frame's per-core power bytes into a signed dBm
RSSI, apply corrections, combine cores, and write the result back into the PHY
status header.

Inputs: `pi`; `param_2` = the RX status/PHY header the MAC prepends to a frame.
Header fields used:

| Offset | Meaning |
|---|---|
| +6 | u16 PHY flags; bit 3 = 11b/CCK frame |
| +9 | u8 core-0 raw power |
| +10 | u16: low byte core-1 power, high byte core-2 power |
| +0xc | u16 (11b WAR only) |
| +0x1f | u8 output: written 0 |
| +0x20 + c | u8 output: signed per-core RSSI |

Steps:
1. Load `rssi[0] = hdr[9]`; for `N>1` `rssi[1] = hdr[10] & 0xff`; for `N>2`
   `rssi[2] = hdr[10] >> 8`.
2. **11b WAR** (only if `phy_rev in {2,5,6}` AND `hdr[6] & 8`): zero all
   `rssi[]` and set `rssi[0] = wlc_phy_11b_rssi_WAR(pi, hdr)`. On the BCM4360
   (`phy_rev` 0/1) this branch never runs.
3. Sign-extend each `rssi[c]` from 8 bits (subtract 0x100 if > 0x7f).
4. `wlc_phy_upd_gain_wrt_temp_phy(pi, &g)` (temperature gain correction `g`).
5. For each core `c` with `rssi[c] != 0`:
   `x = (s8)(pi+0x212+c) * 2 - g`; round `x/4` toward nearest with sign
   (`x>=0: (x+2)>>2`; `x<0: -((2-x)>>2)`); `rssi[c] -= round(x/4)`.
6. Per-core sub-band correction: `sb = wlc_phy_get_chan_freq_range_acphy(pi,
   low byte of pi+0x17e)`; if `sb < 5`, add a byte from `pi_ac` selected by
   `sb` and the chanspec bandwidth bits (`pi+0x17e & 0x3800`, comparing to
   0x2000 / 0x1800 / else). The bytes are the `rssicorrnorm_c<c>` (2.4 GHz,
   `sb==0`, at `pi_ac+0x3a8/0x3a9 + 2c`) and `rssicorrnorm5g_c<c>` (5 GHz,
   `sb` 1..4, at `pi_ac+0x3b0.. + 0xc*c`, three per sub-band selected by the
   bandwidth) - see acphy-attach.md for these arrays.
7. Clamp each `rssi[c]` to a floor of `-128` (0xff80): values `< -0x80` become
   `-0x80`; write `hdr[0x20+c] = (s8)rssi[c]`.
8. `hdr[0x1f] = 0`.
9. Combine cores per the antenna-combine mode `sh+0xa8` over the cores with
   the rx-chain bit set (`sh+0xa7`): mode 1 = minimum, mode 0 = maximum,
   mode 2 = sum then `qm_div16(sum, count)` (average). Return the combined
   signed RSSI.

Verification: called with a synthetic header (`hdr[9]=0x20`, `hdr[10]=0x1030`);
returned 48 with no register access (all corrections 0 in the model, cores
combined by max). `phy_rev`=1 confirmed the 11b WAR is skipped.

### wlc_phy_11b_rssi_WAR (.text+0x8efa4, name original)

Purpose: an alternative core-0 RSSI computation for CCK frames on PHY revs
2/5/6. **Not reached on the BCM4360** (rev 0/1); described for completeness.
It reads `hdr[9]`, `hdr[10]`, `hdr[0xc]`, and per-index correction bytes at
`pi_ac+0x474 + (hdr[10]&7)`, `pi_ac+0x47e`, `pi_ac+0x488`, `pi_ac+0x492`,
`pi_ac+0x49c`, and (when `pi_ac+0x460 == 1`) an eLNA-bypass term from
`pi_ac+0x45e/0x45f`, and returns a single signed byte:
`rssi0 = -(nibble*3) - corr(0x474) + 2 - corr(0x47e) - corr(0x488) -
corr(0x492) - corr(0x49c) - elna`, where `nibble = ((hdr word)>>2) & 0xf`.
(Exact index derivation from `hdr[9..0xd]` is in the code; unverified because
the path is dead on this board.)

### wlc_phy_get_tempsense_degree (.text+0xb537d, name original, wlc_phy_cmn.c)

Not on the AC-PHY path: it simply tail-calls `wlc_lcnphy_tempsense_degree`
(LCN-PHY) and has no callers in the object. Listed because the assignment names
it; the AC-PHY's degree conversion is the arithmetic inside
`wlc_phy_tempsense_acphy` (step 7 above).

## Verification

Everything was run in the Unicorn model after a full bring-up (`run.py`,
`verify_formula.py` in `re-out\analysis\acphy-cal-rx\`):
* `wlc_phy_tempsense_acphy(pi)` returns 116 with the model's constant ADC, and
  stores 116 in `pi_ac+0x8dc` - matching `tempoffset + 0x1d0614/0x4000`.
* Injecting a per-phase ADC sequence via a custom `PHY(0x13)` read
  (`verify_formula.py`) reproduced the rev-0/1 formula exactly (predicted 118,
  emulator 118), confirming the slope constant 8766, the `<<8`, the per-core
  divisor 527, the `/n` and the `/0x4000`.
* `wlc_phy_stf_chain_temp_throttle_acphy(pi)` returned 120 and left the chain
  bitmap 0x33 (116 < `tempthresh` 120: no throttle), confirming the threshold
  comparison.
* `wlc_phy_rx_iq_est_acphy` and `wlc_phy_rssi_compute_acphy` were called; the
  register sequences and the combine result (48) match the reading above. All
  measured quantities read 0/constant in the model, so only the offset (not the
  slope) of tempsense and only the corrections' structure (not their non-zero
  values) of RSSI are numerically confirmed.
* `sub_0addfa` (full RX IQ cal) ran to completion (58433 accesses, `iqcal.py`):
  it entered/left carrier search, ran the radio+PHY loopback save/override,
  `sub_0ad89a` gain search, one tone pair (`+8`/`-8`, `img=1` so two estimates
  each) and wrote `PHY(0x6a0)=PHY(0x6a1)=0` for both cores, then restored via
  `sub_095511`/`sub_09cf53`. `*(pi+0xf58) == pi+0xfb8` was confirmed; the
  save/restore scratch area is `pi_ac` (the function reassigns its base after
  the early cal-state reads). The coefficient targets and the save/restore
  register lists are confirmed; the non-zero coefficient arithmetic is from the
  code only (measured powers are 0).

## Open questions

* `pi+0x212` (per-core byte, RSSI gain reference): exact writer/meaning not yet
  traced; the access in `wlc_phy_rssi_compute_acphy` is exact.
* The purpose of the tempsense config word `W` fields and of `pi+0x116a` (its
  0x1800-bandwidth conditional) is unconfirmed; the register writes are exact.
* The slope arithmetic of tempsense could only be verified by injection, not
  against real hardware; the constants are read from the code.
* RX IQ cal: the exact fixed-point scale of the `wlc_phy_cordic` outputs (and
  hence the final `a`/`b` numeric values) is not verified - the model returns
  0 for all I/Q powers, so both coefficients compute to 0. The operation order,
  shifts, masks and register targets are exact; the cos/sin word assignment
  (low word -> `a`/PHY(0x6a0), high word -> `b`/PHY(0x6a1)) is inferred from the
  code and the way `sub_090fcb` writes them.
* The two-tone digital-filter coefficient (`pi_ac+0x8c8`, `sub_09107b`) only
  runs on 40 MHz; not exercised on the 20 MHz emulator channel.
* `wlc_phy_tx_tone_acphy`, `wlc_phy_stopplayback_acphy`,
  `wlc_phy_txpwr_by_index_acphy`, `sub_09bf99`, `sub_098751`, `sub_09c4e4`,
  `sub_09868f` are treated as opaque here (tx-cal / tx-power topics); only their
  role in the cal is stated.
