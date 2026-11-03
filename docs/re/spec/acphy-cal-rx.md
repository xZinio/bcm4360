# AC-PHY receive calibration, IQ estimation, temperature sense, RSSI

> **Corrections (read first).** Implementing this specification against the
> object's trace showed errors that are not yet folded into the text below.
> Where they disagree, [questions/phy-cal.md](../questions/phy-cal.md) is right
> (its findings are reproduced access for access by `open/phy/phy_cal.c`):
> * tempsense step 6b: the second `RADIO(0x0e)` modification **sets** bit 2 to
>   `B[p]` (it does not clear it) - item 2;
> * the step-7 save/override of `sub_0addfa` is one save loop over all cores,
>   then `PHY(0x401)`, then one override loop; the save order is not ascending
>   and the restore swaps `0x726`/`0x727` - item 8;
> * the gain search `sub_0ad89a`: `paldo` = 0 at 2.4 GHz on this board, the
>   2.4 GHz gain table and `s5` = 10, tone frequency **1000** (not 4000), tones
>   +-1000 - item 9;
> * the 2.4 GHz radio loopback override of `sub_094b1a` - item 10;
> * `sub_09868f` reads `TBL(0x20)[0x32]` as one **48-bit** entry - item 11.
>
> **(phy-calrx5g)** The receive calibration at 40 and 80 MHz, at 5 GHz and with
> non-zero measured powers is completed in the section "Receive calibration at
> every bandwidth and band, with measured powers (phy-calrx5g)" (X1 to X10,
> before "Verification"); it supersedes the passages of the text below that it
> names, in particular: the tone frequencies and amplitudes are 1000 / 2000 /
> 4000 kHz for **20 / 40 / 80 MHz** (the labels were swapped in `sub_0ad89a`,
> in step 10 and in the regression constant K; tempsense step 1 called 0x2000
> "40 MHz" - it is 80 MHz); at 80 MHz there are 6 tones and `pi_ac+0x8c4` is
> set; at 5 GHz the tone estimates isolate the cores (`wlc_phy_rx_iq_est_acphy`
> step 5 *is* exercised); the exact arithmetic of `sub_097809`,
> `wlc_phy_nbits`, `wlc_phy_sqrt_int`, `wlc_phy_cordic`, `wlc_phy_inv_cordic`,
> of the image combination and of the fit; `sub_09107b` with its table; the 5
> GHz gain table of `sub_0ad89a` and its walk; the rev-dependent 5 GHz loopback
> of `sub_094b1a`.

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
| `wlc_phy_nbits` | 0x0b24a4 | 31 | original (wlc_phy_cmn.c); math helper, section X3 (phy-calrx5g) |
| `wlc_phy_sqrt_int` | 0x0b24c3 | 52 | original (wlc_phy_cmn.c); math helper, section X3 (phy-calrx5g) |
| `wlc_phy_inv_cordic` | 0x0b2406 | 97 | original (wlc_phy_cmn.c); math helper, section X3 (phy-calrx5g) |

Helpers described elsewhere and only referenced here: `phy_reg_*`,
`read/write/mod_radio_reg`, `wlc_phy_table_{read,write}_acphy`,
`wlc_phy_write_table_ext` (access.md); `wlc_phy_get_chan_freq_range_acphy`
(acphy-attach.md Data, sub-band index); `wlc_phy_stay_in_carriersearch_acphy`,
`wlc_phy_force_rfseq_acphy` (acphy-init.md); `wlc_phy_tx_tone_acphy`,
`wlc_phy_stopplayback_acphy`, `wlc_phy_txpwr_by_index_acphy` (tx-cal, not yet
specified - their observable effect used here is stated); `wlc_phy_nbits`,
`wlc_phy_sqrt_int`, `wlc_phy_cordic`, `wlc_phy_inv_cordic`, `qm_div16`
(integer maths helpers, wlc_phy_cmn.c; the first four are specified exactly in
section X3, phy-calrx5g).

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
| tempsense | 0x1d0614 = 1902100 | offset added before the final `/0x4000` (rev 0/1). **Corrected 2026-11-03:** the first version gave the decimal as 1901076 (= 0x1d0214, 1/16 degree low); `tools/re/blackbox.py tempsense` bounds the object's constant to exactly 1902100 |
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
   (`pi+0x17e & 0xc000 == 0xc000`). On the BCM4360 2.4 GHz path `img = 1`;
   at 5 GHz `img` = 1 on rev 0 and 0 on rev 1 (phy-calrx5g, X2).
2. `wlc_phy_stay_in_carriersearch_acphy(pi,1)`; `wlc_phy_force_rfseq_acphy(pi,2)`.
3. For each core: `sub_090fcb(pi,2,0,core)` -> zeroes `PHY(c*0x200+0x6a0)` and
   `PHY(c*0x200+0x6a1)` (start from unity/no correction).
4. `sub_09107b(pi,0)` -> `mod(PHY(0x211),1,0)` and clears the per-core
   digital-filter accumulators `pi_ac+0x8c8+4c`.
5. For each active core: `sub_09bf99(pi,0,save,8,core)` (save current RX gain,
   tx-power spec) then `sub_09bf99(pi,1,gains,8,core)` with `gains` read from
   the cal state `*(pi+0xfb8)+0x54+4c` (apply a fixed measurement gain).
6. Compute the bandwidth config word `W` and `bw_index` (`local_21c`) exactly
   as in tempsense step 1 (`bw==0x2000` (80 MHz): `W=0x7f8`, idx 2;
   `bw==0x1800` (40 MHz): `W=0x43e9(+0x201 if pi+0x116a==0)`, idx 1; else
   (20 MHz) `W=0xd5eb`, idx 0; bandwidth names corrected, phy-calrx5g).
7. **Save + override the PHY RX path.** First, once: save `PHY(0x40f)` ->
   `pi_ac+0x24c`, clear its bit 9; save `PHY(0x401)` -> `pi_ac+0x23a`,
   `mod(PHY(0x401),0x7,sh+0xa5)` (rx chain), `mod(PHY(0x401),0x7000,0)`. Then
   per core `c` (base `b=c*0x200`):
   * **Save** these registers into a `pi_ac` scratch area (all restored by
     `sub_09cf53`): `PHY(b+0x720..b+0x73e)` and `PHY(b+0x747)` (26 registers:
     0x720,0x721,0x722,0x723,0x724,0x725,0x726,0x727,0x728,0x729,0x730,0x731,
     0x732,0x733,0x734,0x735,0x736,0x737,0x738,0x739,0x73a,0x73b,0x73c,0x73d,
     0x73e,0x747), plus `PHY(b+0x678)` (saved in the last step), plus the gain
     words `sub_098751(...)` -> `pi_ac+0x21a`, `TBL(7)[c+0x100/0x103/0x106]` ->
     `pi_ac+0x222/0x228/0x22e`, and `pi_ac+0x24e+c` = `pi_ac+0x10+c` (tx index).
   * **Override**, the exact fixed sequence in order (`W` = the config word of
     step 6, `bw_index` = `local_21c`):
     ```
     mod(PHY(b+0x73e),0x10,0);   mod(PHY(b+0x73e),0x20,0);  mod(PHY(b+0x73e),0x1000,0x1000)
     mod(PHY(b+0x721),0x1,0x1);  mod(PHY(b+0x729),0x1,0x1)
     mod(PHY(b+0x73a),0x7,   W & 0x7)
     mod(PHY(b+0x725),0x20,0x20)
     mod(PHY(b+0x739),0x7e,  (W>>2) & 0x7e)
     mod(PHY(b+0x725),0x2,0x2)
     mod(PHY(b+0x73a),0x8,   (W>>6) & 0x8)
     mod(PHY(b+0x725),0x40,0x40)
     mod(PHY(b+0x73a),0x10,  (W>>6) & 0x10)
     mod(PHY(b+0x725),0x80,0x80)
     mod(PHY(b+0x73a),0x60,  (W>>6) & 0x60)
     mod(PHY(b+0x725),0x100,0x100)
     mod(PHY(b+0x729),0x20,0);   mod(PHY(b+0x721),0x20,0x20)
     mod(PHY(b+0x729),0x40,0);   mod(PHY(b+0x721),0x40,0x40)
     mod(PHY(b+0x729),0x800,0);  mod(PHY(b+0x721),0x800,0x800)
     mod(PHY(b+0x729),0x1000,0); mod(PHY(b+0x721),0x1000,0x1000)
     mod(PHY(b+0x729),0xe000,0); mod(PHY(b+0x721),0x2000,0x2000)
     mod(PHY(b+0x728),0x3800,0); mod(PHY(b+0x721),0x4000,0x4000)
     mod(PHY(b+0x728),0x2, R)          # R = 0 if radiorev in {2,3,4,0x12,0x18,0x1a,0x22,8} (our rev4 -> 0), else 2
     mod(PHY(b+0x720),0x2,0x2)
     # (5 GHz only, skipped here: if phytype 0xb & phyrev!=0 & band 0xc000: mod(PHY(b+0x729),0x20,0x20);
     #  radiomajor 1 only: another mod(PHY(b+0x729),0x20,0x20))
     #  (phy-calrx5g, X1: the condition is only the current chanspec's band; no other state)
     mod(PHY(b+0x729),0x200,0x200); mod(PHY(b+0x721),0x200,0x200)
     mod(PHY(b+0x736),0x80,0);   mod(PHY(b+0x724),0x80,0x80)
     mod(PHY(b+0x736),0x4,0);    mod(PHY(b+0x724),0x4,0x4)
     mod(PHY(b+0x736),0x2,0);    mod(PHY(b+0x724),0x2,0x2)
     mod(PHY(b+0x736),0x40,0);   mod(PHY(b+0x724),0x40,0x40)
     mod(PHY(b+0x736),0x100,0);  mod(PHY(b+0x724),0x100,0x100)
     mod(PHY(b+0x736),0x10,0);   mod(PHY(b+0x724),0x10,0x10)
     mod(PHY(b+0x736),0x200,0x200); mod(PHY(b+0x724),0x200,0x200)
     mod(PHY(b+0x736),0x20,0x20);   mod(PHY(b+0x724),0x20,0x20)
     mod(PHY(b+0x736),0x8,0x8);   mod(PHY(b+0x724),0x8,0x8)
     mod(PHY(b+0x736),0x1,0x1);   mod(PHY(b+0x724),0x1,0x1)
     g = TBL(7)[ bw_index==2 ? c*2+0x441 : c*0x10+0x140+bw_index ]   # 16-bit gain code
     mod(PHY(b+0x735),0x700,  (g & 0x7)  << 8);  mod(PHY(b+0x723),0x8,0x8)
     mod(PHY(b+0x735),0x3800, (g & 0x38) << 8);  mod(PHY(b+0x723),0x10,0x10)
     mod(PHY(b+0x737),0xff,   g >> 6);           mod(PHY(b+0x723),0x200,0x200)
     mod(PHY(b+0x735),0x4000,0);  mod(PHY(b+0x723),0x20,0x20)
     mod(PHY(b+0x735),0x1,0x1);   mod(PHY(b+0x723),0x1,0x1)
     mod(PHY(b+0x729),0x100,0x100); mod(PHY(b+0x721),0x100,0x100)
     save PHY(b+0x678) -> pi_ac+0x244+2c;  mod(PHY(b+0x678),0x1,0)
     ```
   The `W`-into-`0x739/0x73a/0x725` distribution is bit-for-bit identical to
   tempsense step 3. `sub_09cf53` (step 13) writes every saved register back.
8. `sub_094b1a(pi)` -> radio loopback setup (below).
9. `sub_0ad89a(pi)` -> RX-gain search / gain selection (below).
10. Build the tone list `local_48` from the bandwidth. **Superseded by X2
    (phy-calrx5g)**: 20 MHz `{+2,-2}`, 40 MHz `{+4,-4}`, 80 MHz
    `{+8,-8,+24,-24,+32,-32}` with `pi_ac+0x8c4` = 1 (only at 80 MHz on rev
    0/1); the earlier "{+8,-8} at 20 MHz, extra tones at 40 MHz, A = 8 (20) / 4
    (0x1800) / 2 (else)" had the bandwidths reversed.
11. **Tone loop**, for each tone index `k` (amplitude `A_k = local_48[k]`;
    exact arguments and the per-core arithmetic in X2, X5, phy-calrx5g):
    a. `wlc_phy_tx_tone_acphy(pi, (s16)(A_k*1000)>>1, 0xb5, 0,0,0)` - plays a
       tone at `A_k*500` kHz with amplitude 0xb5.
    b. `wlc_phy_rx_iq_est_acphy(pi, est0, 0x4000, 0x20, 0, 1)` (0x3000 if
       `pi_ac+0x8c4`; at 5 GHz this isolates the cores, X7).
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
12. **Regression + coefficient build** (after all tones). All quantities are
    32-bit signed. (Exact version, with the corrected K and without the
    remainder term: X6, phy-calrx5g.) Per core `c`, over the `uVar4` tones (`k`):
    ```
    Sxx  = sum_k A_k^2                        # amplitude energy
    Sy   = sum_k angle[k][c]                  # local_98[c]  (angle from step 11e)
    Sxy  = sum_k A_k * angle[k][c]            # local_88[c]
    Sg   = sum_k gainratio[k][c]              # local_a8[c]  (Q10 gain ratio)
    gain = (Sg + uVar4/2) / uVar4             # average gain ratio (rounded)
    q    = ( (Sy>>31 | 1) * (uVar4/2) ) + Sy  # sign-aware rounding numerator
    (w0, w1) = wlc_phy_cordic(q / uVar4, q % uVar4)   # cordic of the average angle
    a = ( ((gain * w0) >> 15) + 1 ) >> 1,  & 0x3ff   -> PHY(c*0x200+0x6a0)
    b = ( ((gain * w1) >> 15) + 1 ) >> 1,  & 0x3ff   -> PHY(c*0x200+0x6a1)
    ```
    `wlc_phy_cordic(theta, out, rem)` returns `out[0]=w0` (the sine-like output,
    0 at theta 0) and `out[1]=w1` (the cosine-like output, ~0x10000 at theta 0),
    so `a` scales the cosine branch and `b` the sine branch; `theta` is the
    integer average angle and `rem` its division remainder (for sub-unit
    precision). Verified: with injected accumulators the predicted `(a,b)` equal
    the emulator's writes exactly for both cores (Verification).
    If `pi_ac+0x8c4` (several tone pairs: **80 MHz** on rev 0/1): also a
    per-core digital-filter coefficient
    `pi_ac+0x8c8+4c = (((-Sxy * K) / Sxx >> 14) + 1) >> 1` with `K = 0x1e`
    (**80 MHz**, 0x2000) / `0xf` (40 MHz) / `8` (20 MHz), and no remainder term
    (corrected, phy-calrx5g X6).
13. **Write + restore.** For each core `sub_090fcb(pi,1,ab,core)` writes
    `a`->`PHY(c*0x200+0x6a0)`, `b`->`PHY(c*0x200+0x6a1)`. If `pi_ac+0x8c4`:
    `sub_09107b(pi,1)` writes the digital-filter taps. `sub_095511(pi)` restores
    the radio; `sub_09cf53(pi)` restores the ~26 PHY registers per core and
    finalizes; for each active core `sub_09bf99(pi,1,save,8)` restores the RX
    gain; `wlc_phy_stay_in_carriersearch_acphy(pi,0)`. Return 0.

Verification: ran the whole function in the emulator (58433 accesses); it
completes and (in the base model) writes `PHY(0x6a0)=PHY(0x6a1)=0` for both
cores. This is **not** because the arithmetic is a no-op but because
`wlc_phy_rx_iq_est_acphy` skips the accumulator read while the estimate-done bit
`PHY(0x270)` bit 0 stays set (the synthetic model never clears it), so every
estimate stays 0. When the done bit is modelled *and* the accumulators
`PHY(c*0x200+0x6c0..0x6c5)` are given non-zero, per-core-different values, the
coefficients come out non-zero and per-core-asymmetric, and the step-12 formula
above reproduces the emulator's writes **exactly** (e.g. accumulators
IQ/I2/Q2 = 0x2000/0x14000/0x11000 (c0), 0x3000/0x25000/0x10800 (c1) ->
`PHY(0x6a0)=0x39a, PHY(0x6a1)=0x3aa, PHY(0x8a0)=0x3ad, PHY(0x8a1)=0x2a7`, matched
bit-for-bit by `verify_coeff.py`). So the coefficients are computed from the
measured I/Q powers; their specific object values (e.g. 0x48/0xee) depend on the
real measurement and are only reproducible when the model supplies the done bit
and non-zero accumulators. Everything else (the ~35000 setup/loopback/restore
accesses) is structural and reproducible.

### sub_097809 (.text+0x97809, assigned: wlc_phy_calc_iqcc_acphy)

Purpose: from one IQ-estimate record `{I*Q, I^2, Q^2}` compute the phase angle,
the gain ratio and the quadrature term of the receiver's IQ imbalance, in fixed
point. Inputs `param_1` = `int[3]` = `{IQ, I2, Q2}`; outputs `param_2` =
`int[3]` = `{angle, gainratio (Q10, 0x400 = 1.0), sinterm}`.
**The exact steps are in X4 (phy-calrx5g)**; the outline below is kept for
orientation (its step 1 "denom shifted to IQ's scale" and step 4 "rounded to
even" are made precise there).

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
`PHY(0x211)` bit 0. **Exact accesses and the whole table: X8 (phy-calrx5g).**
Called with mode 1 only when `pi_ac+0x8c4` is set, i.e. after an 80 MHz
calibration on this card.

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
`0x25/0x26/0x27/0x28/0x41/0x44`.) **Exact sequence of both bands: X10
(phy-calrx5g)** - at 5 GHz the "0x200 then 0x80" pair is taken only for PHY
revision 0; revision 1 does `mod(0x23, 1, 1)` then `mod(0x23, 0x200, 0x200)`.

### sub_095511 (.text+0x95511, assigned: wlc_phy_rxcal_radio_restore_acphy)

Inverse of `sub_094b1a`: sets `pi_ac+0x11a = 0` and writes the six saved radio
registers (`0x20,0x21,0x22,0x23,0x3a,0x3d` for the 4360) back from
`pi_ac+0x11c..0x144`, per core. (Phy rev 5/2/6 also toggle `PHY(0x19e)`.)

### sub_0ad89a (.text+0xad89a, assigned: wlc_phy_rxcal_txgain_setup_acphy)

Purpose: the gain selection for the measurement - an iterative per-core search
that sets each core's RX gain so the loopback tone lands in a target power
window. Uses two `.rodata` gain tables `DAT_0054dbe0` (5 GHz) and `DAT_0054db90`
(2.4 GHz), 0x42 bytes each = 11 entries of a `{short s3, short s7, short s5}`
triple (6 bytes) indexed by gain index `gi` (0..10).

Setup: per core `c` read the coarse gain seed `seed[c] = (PHY(c*0x200+0x6dc) &
0x780) >> 7`; `gi[c] = 0` (2.4 GHz) or `4` (5 GHz); mark all cores unsettled
(`done[c]=0`), direction state `dir[c]=0`. `sub_09868f(pi,...,0x32)` primes a
default gain word.

Loop (a do-while that alternates "apply" and "measure"):
(**Exact walk, both gain tables and the corrected tone and `paldo`: X9,
phy-calrx5g.** The paragraphs below are kept for orientation.)

* **Measure** (once all cores in this pass have applied): play one tone
  (`wlc_phy_tx_tone_acphy(pi, F, 0xb5, 0,0,0)`, `F` = 4000 / 2000 / 1000 for
  **80 / 40 / 20 MHz** - corrected, phy-calrx5g; the first version said 20 /
  0x1800 / 40), `wlc_phy_rx_iq_est_acphy(pi, buf, 0x400, 0x20, 0, 0)`
  (0x400 samples), `wlc_phy_stopplayback_acphy`. Per unsettled core compute
  `p = ((I2 + 0x200) >> 10) + ((Q2 + 0x200) >> 10)` from `buf[c]` and step:
  - `dir==1` (climbing): if `p < 0xb58` raise `gi[c]` by 1, else settle;
  - `dir==0` (initial): if `p < 0x169e`: if `p > 0xb56` settle; else set
    `dir=1` and, while `p != 0` and `gi < 8`, double `p` and raise `gi` (fast
    climb); if `p >= 0x169e`: set `dir=2` and, while `gi >= 2`, halve `p` and
    lower `gi` (fast drop);
  - `dir==2` (dropping): if `p >= 0x169e` lower `gi[c]` by 1, else settle.
  If `gi[c] > 10` force-settle. When *every* core is settled the function
  writes back and returns (see below); otherwise restart the pass.
* **Apply** (per core, before each measure): for an unsettled core write its
  current `gi[c]` triple `(s3,s7,s5) = table[gi[c]]`:
  `PHY(c*0x200+0x730) = (seed[c]<<6) | (paldo<<3)`;
  `PHY(c*0x200+0x731) = 0` (2.4 GHz) / `4` (5 GHz);
  `PHY(c*0x200+0x734) = (s3<<3) | s7`; `mod(PHY(c*0x200+0x722),2,2)`,
  `mod(...,4,4)`, `mod(...,8,8)`; then if `s5 == 0xff` write the unity gain
  `TBL(7)[c+0x100/0x103/0x106]`, else `wlc_phy_txpwr_by_index_acphy(pi,
  1<<c, s5)` (`s5` = a tx power index).

`paldo` (`bVar20`) = 6 for 5 GHz AC rev!=0, or radio rev in
{2,3,4,0x12,0x18,0x1a,0x22,8}, else 0. (Corrected, phy-calrx5g X9: the radio
rev clause applies only to radio major revision 1; on this card `paldo` = 6
exactly at 5 GHz on rev 1, else 0.) In the model every `rx_iq_est` returns 0,
so `p = 0` each pass and the search simply climbs `gi` to 10 and force-settles
(~11 passes per core at 2.4 GHz, 7 at 5 GHz where it starts at 4).

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
   this board; described for completeness.) **Corrected (phy-calrx5g, X7):**
   it is exercised - the tone loop of `sub_0addfa` passes `param_6` = 1 and
   `pi_ac+0x901` = 1, so every tone estimate at 5 GHz takes it; the exact
   order of its accesses is in X7.

Result: none (void); output is the array at `param_2`.

Verification: called directly in the emulator; produced 3065 accesses. **The
output buffer is left unchanged** because the model never clears `PHY(0x270)`
bit 0: step 4b times out and, since the code itself set bit 0 to 1 to start the
estimate, the re-read in step 4c returns bit 0 = 1, so the whole per-core
accumulator read (4c) is **skipped** and the caller's estimate stays whatever it
was (0). Forcing `PHY(0x6c0..0x6c5)` non-zero has no effect unless the done bit
is also modelled to clear; once it is, 4c runs and returns the accumulators.
This gating is why the RX IQ cal writes zero coefficients in the base model.

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
   * `bw == 0x2000` (80 MHz): `W = 0x7f8`.
   * `bw == 0x1800` (40 MHz): `W = 0x43e9`, plus `0x201` if `pi+0x116a == 0`
     (i.e. `0x45ea` when `pi+0x116a==0`).
   * otherwise (`0x1000`, 20 MHz): `W = 0xd5eb`.
   (Bandwidth names corrected, phy-calrx5g: the first version called 0x2000
   "40 MHz" and 0x3000 "80 MHz"; the chanspec's bandwidth field is 0x1000 /
   0x1800 / 0x2000 for 20 / 40 / 80 MHz, as in acphy-cal-tx.md and
   acphy-chanspec.md.)
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
   acc += 0x1d0614                                # = 1902100 (corrected 2026-11-03)
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

## Receive calibration at every bandwidth and band, with measured powers (phy-calrx5g)

This section completes `sub_0addfa` and its helpers so that the receive
calibration can be implemented for 20, 40 and 80 MHz on both bands, for PHY
revision 0 and 1, and for **any** values of the IQ-estimate registers. Where it
disagrees with the text above or with `questions/phy-cal.md` items 8 to 10,
this section is right. "Observed" = seen in the emulator (the object alone; see
"Verification (phy-calrx5g)"); "read" = from the code and the disassembly only.
`bw` = `pi+0x17e & 0x3800` (0x1000 = 20 MHz, 0x1800 = 40 MHz, 0x2000 =
80 MHz); "5 GHz" = `pi+0x17e & 0xc000` = 0xc000; `rev` = `pi+0x164`; N =
`pi+0x168`; all arithmetic is 32-bit two's complement, divisions truncate
toward zero, `>>` is arithmetic, and a shift by a computed count uses the count
modulo 32 (the processor's rule; it matters only at the extremes noted).

### X1. Which state carries the 5 GHz behaviour (answers Q1 of questions/phy-soak.md)

* Every band or bandwidth decision of the calibration is taken from the current
  chanspec `pi+0x17e` and from `rev` (read). Step 7's
  `mod(PHY(b+0x729), 0x20, 0x20)` is made exactly when the PHY type `pi+0x160`
  is 0xb, `rev` != 0 and the chanspec is 5 GHz (read; observed made at 36/80 on
  rev 1, not made on channel 6). (The second such mod, for radio major
  revision 1, does not apply to this card.)
* `pi_ac+0x8d8` (`img`) and `pi_ac+0x8c4` (`two`, X2) are written by every
  call before they are read, and `pi_ac+0x8c8+4c` is cleared by step 4; on the
  AC-PHY path nothing else uses them (read: no other AC function names these
  offsets). So no flag carries one calibration into the next. What does survive
  is hardware state: the digital filter of `sub_09107b` mode 1 (X8) - bit 0 of
  `PHY(0x211)` and the taps `PHY(c*0x200+0x6a4..0x6ae)` - stays programmed
  until the next calibration's step 4 clears `PHY(0x211)` bit 0; a calibration
  at 20 or 40 MHz leaves the filter off. No other function of the AC-PHY names
  `PHY(0x210..0x212)` or `0x6a4..0x6ae` (read).
* What the soak showed (observed): in `tools/re/phy_soak.py` the channel steps
  taken while associated (`6` = step 117, `1l` = 123, `149/80` = 127) return 0
  from the `chanspec` iovar but make **no access**, and `pi+0x17e` stays 0xe02a
  (36/80): the upper layer stays on the associated channel. `sub_0addfa` in the
  join calibrations of steps 119 and 129 and in the periodic calibrations of
  `wait 130` and `wait 920` sees chanspec 0xe02a, rev 1: they are 80 MHz
  calibrations at 5 GHz, the same as step 105. That is why they make the mod
  and read `PHY(0x211)` = 1 (left by the previous 80 MHz calibration) in step 4.
  The band test of the current chanspec is the whole condition.

### X2. Flags, tones and estimate parameters (Q3, Q5.1)

* `img` (step 1) = 1, except 0 for `rev` 2, 3, 5, 6 and for `rev` 1 at 5 GHz.
  This card: rev 0 -> 1 on both bands; rev 1 -> 1 at 2.4 GHz, 0 at 5 GHz (read;
  observed on both revisions and bands).
* `two` = `pi_ac+0x8c4`, written once per call right after the gain search
  (step 9): 1 if `bw` = 0x2000, or if `bw` = 0x1800 and `rev` = 3; else 0. On
  rev 0/1 it is 1 exactly at 80 MHz and **cleared** at 20 and 40 MHz (read;
  observed 0 at 20/40 and 1 at 80 MHz).
* Tone amplitudes `A_k` (signed bytes) and their number `cnt` (step 10):

  | Bandwidth | `two` | `A_0 .. A_cnt-1` | `cnt` |
  |---|---|---|---|
  | 20 MHz | 0 | +2, -2 | 2 |
  | 40 MHz (rev 0/1) | 0 | +4, -4 | 2 |
  | 80 MHz | 1 | +8, -8, +24, -24, +32, -32 | 6 |
  | 40 MHz, rev 3 only (other chips) | 1 | +4, -4, +15, -15 | 4 |

  (A bandwidth outside the three with `two` set would get +2, -2, +7, -7;
  unreachable.)
* Tone k is `wlc_phy_tx_tone_acphy(pi, f_k, 0xb5, 0, 0, 0)` with `f_k` =
  `A_k * 1000` taken as a 16-bit signed product, shifted right by 1
  (arithmetic) = `A_k * 500` kHz: **20 MHz +-1000, 40 MHz +-2000, 80 MHz
  +-4000, +-12000, +-16000** in this order (observed at all three). The
  `{+8,-8}` and "`A` = 8 (20 MHz) / 4 (0x1800) / 2 (else)" of step 10 above had
  the bandwidths the wrong way round; 8 belongs to 80 MHz.
* For each tone k (step 11): the loopback estimate
  `wlc_phy_rx_iq_est_acphy(pi, est0, n, 0x20, 0, 1)` with `n` = 0x3000 if `two`
  else 0x4000; then, if `img`: `sub_095511(pi)`,
  `wlc_phy_rx_iq_est_acphy(pi, est1, 0x4000, 0x20, 0, 1)`, `sub_094b1a(pi)`;
  then `wlc_phy_stopplayback_acphy(pi)` (observed, all arguments). The last
  argument 1 asks the estimator to isolate the cores (X7); it takes effect
  only where the band's byte allows it: `pi_ac+0x900` (2.4 GHz) = 0 and
  `pi_ac+0x901` (5 GHz) = 1 on this card (observed). So **at 5 GHz every
  tone estimate measures the cores one after the other**, at 2.4 GHz all at
  once. (The gain search's estimates pass 0 there and always measure all cores
  at once.)

### X3. The integer helpers of wlc_phy_cmn.c (exact)

Angles are in units of 1/65536 degree (0x1680000 = 360 degrees, 0xb40000 =
180). `T[0..17]` = the arctangent table at `.rodata+0x2ce950` (32-bit
entries; `atan(2^-i)` in that unit):
`0x2d0000, 0x1a90a7, 0x0e0947, 0x072001, 0x03938b, 0x01ca38, 0x00e52a,
0x007297, 0x00394c, 0x001ca6, 0x000e53, 0x000729, 0x000395, 0x0001ca, 0x0000e5,
0x000073, 0x000039, 0x00001d`. (questions/phy-cal.md item 6 derived it; these
are the object's values.)

* **`wlc_phy_nbits(x)`** (`.text+0x0b24a4`, 31 bytes), x signed 32-bit, result
  a byte: let `a` = |x| computed in 32 bits (so |-2^31| stays -2^31); the
  result is the smallest n >= 0 with `a >> n` <= 0 - the number of significant
  bits of |x|: 0 for 0, 1 for +-1, 31 for magnitudes >= 2^30, and 0 for -2^31.
* **`wlc_phy_sqrt_int(v)`** (`.text+0x0b24c3`, 52 bytes), v unsigned 32-bit:
  digit-by-digit square root. `r` = 0; for `bit` = 2^30, 2^28, ..., 2^2, 2^0: if
  `v >= r + bit` then `v -= r + bit` and `r = (r >> 1) | bit`, else
  `r = r >> 1`. Result `r + 1` if the remainder `v > r`, else `r`: the square
  root rounded to the nearest integer (0..65536). Only one argument (the extra
  arguments the decompiler shows at its call sites are leftovers in registers).
* **`wlc_phy_cordic(theta, out)`** (`.text+0x0b2317`, 239 bytes; also in
  acphy-cal-tx.md section 13): only two arguments (the third the decompiler
  shows in `sub_0addfa` is the remainder of a division left in a register).
  1. `s` = -1 if theta < 0 else +1; `t` = ((theta + s*0xb40000) rem 0x1680000)
     - s*0xb40000 (rem = remainder of a truncating division) - theta folded
     into -180..+180 degrees.
  2. `d` = t in whole degrees, rounded: `((t >> 15) + 1) >> 1` for t >= 0,
     `-((((-t) >> 15) + 1) >> 1)` for t < 0.
  3. If d > 90: `t -= 0xb40000`, sign = -1. Else if t < 0 and d < -90:
     `t += 0xb40000`, sign = -1. Else sign = +1.
  4. `x` = 0x9b75, `y` = 0, `z` = 0. For i = 0..17: if t > z: `z += T[i]`,
     `(x, y) = (x - (y >> i), y + (x >> i))`; else: `z -= T[i]`,
     `(x, y) = (x + (y >> i), y - (x >> i))` (both new values from the old ones).
  5. `out[0]` = sign*y (the sine, about 65536*sin theta), `out[1]` = sign*x
     (the cosine; 65541 at theta 0).
* **`wlc_phy_inv_cordic(xy, out)`** (`.text+0x0b2406`, 97 bytes): one 64-bit
  argument with `x` in its high 32 bits and `y` in its low 32 bits, and an
  `int *out`. `x <<= 4`, `y <<= 4` (32 bits); `z` = 0; for i = 0..17: if y < 0:
  `z -= T[i]`, `(x, y) = (x - (y >> i), y + (x >> i))`; else `z += T[i]`,
  `(x, y) = (x + (y >> i), y - (x >> i))`. `*out = z`: the angle of (x, y) for
  x > 0.

### X4. sub_097809 (wlc_phy_calc_iqcc_acphy) exact

Input `{IQ, I2, Q2}` (the three words of one estimate record), output
`{angle, gain, sin}` (replaces "sub_097809" above, whose steps 1 to 4 were only
an outline):

1. `nIQ` = nbits(IQ); `n` = max(nbits(I2), nbits(Q2)).
2. If `n` <= 30: `rI` = sqrt_int(I2 << (30-n)), `rQ` = sqrt_int(Q2 << (30-n));
   else `rI` = sqrt_int(I2 >> (n-30)), `rQ` = sqrt_int(Q2 >> (n-30)) (the
   shifted words passed as unsigned).
3. `den` = rI*rQ (32-bit product); if `n > nIQ + 16`: `den <<= n - nIQ - 16`,
   else `den >>= nIQ + 16 - n` (count modulo 32).
4. `num` = (-IQ) << (30 - nIQ) (count modulo 32: nIQ = 31 shifts by 31). If
   `num` > 0: `num += den >> 1`, else `num -= den >> 1` (rounding).
5. `sin` = num / den, or 0 if den = 0. `out[2]` = sin. (About
   -IQ/sqrt(I2*Q2) * 65536.)
6. `h` = sin >> 1; `c` = sqrt_int(0x40000000 - h*h); `out[0]` =
   inv_cordic(x = 2c, y = sin) (about asin of the sine term, in 1/65536 degree).
7. `d` = Q2 - I2; `m` = nbits(d) rounded up to even (`m += m & 1`);
   `dd` = d << (30 - m) (count modulo 32); `ii` = I2 >> (m - 10) if m > 10,
   else I2 << (10 - m); if `dd` > 0: `dd += ii >> 1`, else `dd -= ii >> 1`.
8. `out[1]` = 0x400 if `ii` = 0, else sqrt_int(dd / ii + 0x100000) (about
   sqrt(Q2/I2) * 1024).

(The processor would fault on -2^31 / -1 in steps 5 and 8; the estimator's
records do not produce that - unverified on hardware.)

### X5. Per tone and core: the angle and the gain (step 11e)

For tone k and core c (`est0[c]`, `est1[c]` = the records of the two estimates):

1. `(a0, g0, s0)` = sub_097809(est0[c]).
2. If `img` = 1:
   1. `(a1, g1, s1)` = sub_097809(est1[c]).
   2. `P1` = est1.I2 + est1.Q2, `P0` = est0.I2 + est0.Q2 (32-bit sums).
      `m` = nbits(P1) rounded up to even; `P0'` = P0 >> (m - 10) if m > 10,
      else P0 << (10 - m).
   3. If `P0'` = 0: angle = a0. Otherwise `r` = sqrt_int(((P0' >> 1) +
      (P1 << (30 - m))) / P0') (about sqrt(P1/P0) * 1024). If `r` <= 41:
      angle = a0. Otherwise `s` = s0 + ((r * (s0 - s1)) >> 10); `h` = s >> 1;
      `c` = sqrt_int(0x40000000 - h*h); angle = inv_cordic(2c, s).
3. Else angle = a0.
4. Keep `angle[k][c]` = angle and `gain[k][c]` = g0 (the loopback estimate's
   gain; the image estimate's gain g1 is not used).

### X6. The fit and the coefficients (steps 12 and 13, exact)

Over the `cnt` tones: `Sxx` = sum of A_k^2 (one value for all cores); per core
c: `Sy` = sum of angle[k][c], `Sxy` = sum of A_k * angle[k][c], `Sg` = sum of
gain[k][c]; `half` = cnt >> 1. Then per core:

1. `g` = (Sg + half) / cnt.
2. `q` = Sy + half if Sy >= 0, else Sy - half; `theta` = q / cnt;
   `(w0, w1)` = wlc_phy_cordic(theta) (sine, cosine).
3. `a` = (((g * w0) >> 15) + 1) >> 1, bits 0..9 kept; `b` = (((g * w1) >> 15)
   + 1) >> 1, bits 0..9 kept (32-bit products). `a` -> `PHY(c*0x200+0x6a0)`,
   `b` -> `PHY(c*0x200+0x6a1)` (written in step 13 by `sub_090fcb` mode 1, core
   by core, after all cores are computed). So `a` is the sine branch and `b`
   the cosine branch (as phy-cal.md item 8 found); with no imbalance `b` =
   0x400, which the 10-bit field keeps as 0 (interpretation: the field holds b
   relative to 1.0).
4. Only if `two`: `K` = 30 at 80 MHz (15 at 40 MHz, reached only on rev 3;
   8 at 20 MHz, unreachable); `pi_ac+0x8c8+4c` = ((((-Sxy * K) / Sxx) >> 14) + 1) >> 1. There
   is no remainder term (the "remainder" of the text above and the extra
   argument the decompiler passes to `sub_090fcb`/`sub_09107b` are dead values
   in registers). The text above had K's 20 and 80 MHz labels swapped.
5. Step 13: the writes of item 3 for every core; then, if `two`,
   `sub_09107b(pi, 1)` (X8); then `sub_095511`, `sub_09cf53`, the `sub_09bf99`
   restores and carrier search off, as above.

The calibration makes the same accesses for any measured values: the values
change only the written words (`0x6a0/0x6a1`, the filter taps) and the gain
search's path (X9).

### X7. wlc_phy_rx_iq_est_acphy with core isolation (exercised at 5 GHz)

Correction of its step 5 ("not exercised ... on this board"): the tone loop of
the receive calibration takes this branch at 5 GHz (X2). Exactly (read; the
access sequence observed in the 80 MHz calibration): for each core c = 0..N-1
(the core being measured):

1. For each core d = 0..N-1 with d != c and bit d set in `sh+0xa7`: read
   `PHY(d*0x200+0x720)`, `+0x728`, `+0x721`, `+0x729` (in this order), then
   `mod(+0x720, 1, 1)`, `mod(+0x728, 1, 0)`, `mod(+0x721, 4, 4)`,
   `mod(+0x729, 2, 0)`; remember the four (address, value read) pairs in that
   order.
2. `osl_delay(1)`; `mod(PHY(0x270), 1, 1)`; poll `PHY(0x270)` bit 0 as in step
   4b (up to 1000 waits of 10 us).
3. Write the remembered values back in the reverse order (last core's 0x729
   first, first core's 0x720 last; plain writes).
4. Read `PHY(0x270)`; if bit 0 is clear, read core c's six sums as in step 4c
   into record c.

With one present core (`sh+0xa7` = 1, the single-chain boards) the measured
core 0 isolates nobody and core 1 isolates core 0; both are still measured
(the outer loop is over all N cores).

### X8. sub_09107b (wlc_phy_rxcal_digfilt_acphy) exact

* Mode 0: `mod(PHY(0x211), 1, 0)`; `pi_ac+0x8c8+4c` = 0 for c < N (observed).
* Mode 1, for each core c = 0..N-1: `v` = `pi_ac+0x8c8+4c`; `j` = min(|v|, 10);
  `mod(PHY(0x210), 7, 4)`, `mod(PHY(0x211), 4, 0)`, `mod(PHY(0x211), 2, 0)`,
  `mod(PHY(0x212), 0xf, 10)` (repeated for every core); then 11 plain writes
  `PHY(c*0x200+0x6a4+i)` for i = 0..10 in rising address order, of `R_j[i]` if
  v >= 0 and of `R_j[10-i]` (the row reversed) if v < 0. After all cores:
  `mod(PHY(0x211), 1, 1)`.
* The rows `R_0 .. R_10` (the 0xf2 bytes at `.rodata+0x2cc960`,
  `DAT_0054da90`; 16-bit entries, i = 0..10):

  | j | R_j[0..10] |
  |---|---|
  | 0 | 0000 0000 0000 0000 0000 0400 0000 0000 0000 0000 0000 |
  | 1 | fff4 000f ffec 001e ffc4 0400 003d ffe2 0014 fff1 000c |
  | 2 | ffe8 001e ffd8 003c ff88 0400 007a ffc3 0029 ffe2 0018 |
  | 3 | ffdc 002d ffc4 005b ff4c 0400 00b8 ffa4 003d ffd2 0025 |
  | 4 | ffd6 0034 ffbb 0067 ff32 0400 00d3 ff97 0046 ffcc 002a |
  | 5 | ffcc 0041 ffaa 0081 ff00 0400 0108 ff7d 0057 ffbf 0034 |
  | 6 | ffc2 004e ff99 009b fecd 03ff 013f ff62 0069 ffb2 003f |
  | 7 | ffb7 005b ff87 00b4 fe9b 03ff 0175 ff48 007a ffa4 0049 |
  | 8 | ffad 0068 ff76 00ce fe69 03ff 01ac ff2d 008c ff97 0054 |
  | 9 | ffa3 0075 ff65 00e7 fe38 03ff 01e3 ff12 009e ff8a 005e |
  | 10 | ff98 0081 ff54 0101 fe06 03fe 021b fef7 00b0 ff7c 0069 |

  (Interpretation: an 11-tap filter with centre tap 1.0 = 0x400 whose
  asymmetry grows with j; reversing it mirrors the correction. Row 0 is the
  identity, so v = 0 programs a pass-through filter and still sets
  `PHY(0x211)` bit 0.) `|v|` is taken as `v * ((v >> 31) | 1)`, so v = -2^31
  would index outside the table (not reachable: |v| < 2^17 from X6).

### X9. sub_0ad89a: the gain search, exact (Q4)

Gain tables, entries (`s3`, `s7`, `s5`) of three 16-bit words, index `gi` =
0..10 (read: the `.rodata` bytes; observed: the writes):

| gi | 5 GHz `.rodata+0x2ccab0` (`DAT_0054dbe0`) | 2.4 GHz `.rodata+0x2cca60` (`DAT_0054db90`) |
|---|---|---|
| 0 | 0, 0, 0 | 0, 0, 10 |
| 1 | 0, 1, 0 | 0, 1, 10 |
| 2 | 0, 2, 0 | 0, 2, 10 |
| 3 | 0, 3, 0 | 0, 3, 10 |
| 4 | 0, 4, 0 | 0, 4, 10 |
| 5 | 1, 4, 0 | 0, 5, 10 |
| 6 | 2, 4, 0 | 1, 5, 10 |
| 7 | 3, 4, 0 | 2, 5, 10 |
| 8 | 4, 4, 0 | 3, 5, 10 |
| 9 | 5, 4, 0 | 4, 5, 10 |
| 10 | 5, 5, 0 | 5, 5, 10 |

1. `paldo` = 6 if the chanspec is 5 GHz, the PHY type is 0xb and `rev` != 0;
   else 0. (Also 6 for radio major revision 1 with radio rev in {2, 3, 4, 8,
   0x12, 0x18, 0x1a, 0x22} - not this card; the "or radio rev in {...}" of the
   text above belongs to that radio only.) Table = the 5 GHz one at 5 GHz,
   else the 2.4 GHz one.
2. Read `PHY(0x19e)`, `mod(PHY(0x19e), 2, 2)`. For each core c: `seed[c]` =
   (`PHY(c*0x200+0x6dc)` & 0x780) >> 7; `gi[c]` = 4 at 5 GHz, 0 at 2.4 GHz;
   `dir[c]` = 0; `done[c]` = 0. Then `sub_09868f(pi, buf, 0x32)` (item 11 of
   phy-cal.md); its third 16-bit word is kept as `u` (used only by the unity
   path below).
3. **Apply** (for c = 0..N-1 with `done[c]` = 0), `(s3, s7, s5)` =
   table[gi[c]]: `PHY(c*0x200+0x730)` = (seed[c] << 6) | (paldo << 3);
   `PHY(c*0x200+0x731)` = 0 at 2.4 GHz, 4 at 5 GHz; `PHY(c*0x200+0x734)` =
   (s3 << 3) | s7; `mod(PHY(c*0x200+0x722), 2, 2)`, `(..., 4, 4)`,
   `(..., 8, 8)`; then, if the low byte of s5 is 0xff, `TBL(7)[c+0x100]` =
   0xff, `TBL(7)[c+0x103]` = 0xffff, `TBL(7)[c+0x106]` = u (16-bit, one entry
   each; not reached with these tables), else
   `wlc_phy_txpwr_by_index_acphy(pi, 1 << c, s5)` (index 0 at 5 GHz, 10 at
   2.4 GHz).
4. **Measure**: `wlc_phy_tx_tone_acphy(pi, F, 0xb5, 0, 0, 0)` with F = 4000
   at 80 MHz, 2000 at 40 MHz, 1000 at 20 MHz (the text above had 20 and 80
   swapped); `wlc_phy_rx_iq_est_acphy(pi, buf, 0x400, 0x20, 0, 0)`;
   `wlc_phy_stopplayback_acphy(pi)`.
5. **Step** each core with `done[c]` = 0, from its record (unsigned 32-bit):
   `p` = ((Q2 + 0x200) >> 10) + ((I2 + 0x200) >> 10) (logical shifts):
   * `dir` = 1: if p < 0xb58: gi += 1; else done.
   * `dir` = 0: if p >= 0x169e: `dir` = 2, then repeat { stop if gi < 2;
     p >>= 1; gi -= 1 } while p > 0x169d. Else if p > 0xb56: done. Else
     `dir` = 1 and, if p != 0, repeat { stop if gi > 7; p *= 2; gi += 1 }
     while p < 0xb57.
   * `dir` = 2: if p < 0x169e: done; else gi -= 1.
   * Then, `gi` being a byte: if gi > 10 as an unsigned byte (so also after a
     decrement from 0, which gives 255): done.
6. If any core stepped in 5 is still not done, go to 3 (only the cores not done
   are applied again); else `mod(PHY(0x19e), 2, old & 2)` and return. A core
   that is done keeps the gain applied last (an index that went past 10 or
   below 0 is never applied).

The first pass applies the start index to every core before the first
measurement. With zero powers (the base model and the soak, see
Verification) `p` = 0: `dir` becomes 1 with no climb, then one step per pass,
4 -> 10 at 5 GHz (7 measurements, then gi = 11 -> done), 0 -> 10 at 2.4 GHz.

### X10. sub_094b1a: the radio loopback, exact (Q2)

Per core c (radio registers `| c << 9`; chip 0x4360, 0x4352, 0xa9c4 and 0xaa06
alike), after `pi_ac+0x11a` = 1: read and save `RADIO(0x20)`, `0x21`, `0x22`,
`0x23`, `0x3a`, `0x3d` (into `pi_ac+0x11c/0x124/0x12c/0x134/0x13c/0x144 + 2c`),
write 0 to the same six in the same order, then (read; observed at 2.4 GHz and
at 5 GHz on rev 1):

| 2.4 GHz | 5 GHz |
|---|---|
| `mod(0x22, 0x100, 0)` | `mod(0x23, 0x100, 0)` |
| `mod(0x3a, 0x10, 0)` | `mod(0x3d, 0x10, 0)` |
| `mod(0x22, 0x20, 0)` | `mod(0x23, 0x20, 0)` |
| `mod(0x22, 0x40, 0)` | `mod(0x23, 0x40, 0)` |
| radio rev in {2,3,4,8,0x12,0x18,0x1a,0x22}: `mod(0x20, 0x20, 0)`, `mod(0x20, 4, 4)`; else `mod(0x20, 0x20, 0x20)`, `mod(0x20, 4, 0)` | the same on `0x21` |
| `mod(0x22, 0x200, 0x200)`, `mod(0x22, 0x80, 0x80)` | **rev 0:** `mod(0x23, 0x200, 0x200)`, `mod(0x23, 0x80, 0x80)`; **rev 1:** `mod(0x23, 1, 1)`, `mod(0x23, 0x200, 0x200)` |
| `mod(0x20, 3, 0)` | `mod(0x21, 3, 0)` |
| `mod(0x22, 6, 0)` | `mod(0x23, 6, 0)` |

The 5 GHz loopback pair depends on one 8-byte comparison of `pi+0x160` (the PHY
type and, above it, the PHY revision) with 0xb: it is true only for PHY type
0xb **and** revision 0. (So the text above, "`RADIO(0x23)` mask 0x200 then
0x80/0x80", is the rev-0 branch; the soak's rev-1 card takes the other.) The
fourth mask is 0x40 (not 4 as questions/phy-cal.md item 10 has it; the
registers are 0 there, so the mask is not observable) and the rev-dependent
pair is on `0x20`/`0x21` with masks 0x20 and 4.

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
  each; phy-calrx5g: at 20 MHz the pair is +2/-2, played at +-1000 kHz, X2)
  and, in the base model, wrote `PHY(0x6a0)=PHY(0x6a1)=0` for both cores,
  then restored via `sub_095511`/`sub_09cf53`. `*(pi+0xf58) == pi+0xfb8` was
  confirmed; the save/restore scratch area is `pi_ac` (the function reassigns
  its base after the early cal-state reads).
* Coefficient arithmetic **numerically confirmed** (`verify_coeff.py`): with the
  `PHY(0x270)` done bit modelled and the accumulators
  `PHY(c*0x200+0x6c0..0x6c5)` forced to per-core-different non-zero values, the
  emulator wrote `PHY(0x6a0)=0x39a, 0x6a1=0x3aa, 0x8a0=0x3ad, 0x8a1=0x2a7`; a
  Python re-implementation of the step-12 formula (calling the object's own
  `sub_097809`/`wlc_phy_cordic`) reproduced all four bit-for-bit. This proves
  the coefficients are the `sub_097809`+regression+cordic result of the measured
  I/Q powers, and pins down `a`=cosine branch / `b`=sine branch of the cordic
  output. (phy-calrx5g: the reverse - `a` is computed from `out[0]`, the sine,
  and `b` from `out[1]`, the cosine, as `verify_coeff.py` itself does; X6.)
  `investigate.py`/`test_done.py` showed the base model writes 0 only
  because `rx_iq_est` never reads the accumulators (done bit never clears).

### Verification (phy-calrx5g)

Scripts and outputs in `re-out\analysis\phy-calrx5g\` (the object alone in the
emulator; `model.py` holds the independent models written from X3 to X9):

* `verify_math.py 20000`: `wlc_phy_nbits`, `wlc_phy_sqrt_int`,
  `wlc_phy_inv_cordic`, `wlc_phy_cordic` and `sub_097809` called directly on
  20,000 inputs each (random 32-bit words, small values, powers of two and
  their neighbours, the extremes, realistic estimate records): **100,000
  comparisons, 0 different** (`vm-20000.txt`).
* `verify_cal.py`: a full bring-up, a channel, then `sub_0addfa` called
  directly, the IQ-estimate registers giving a fresh random record for every
  estimate and core (powers from 2^6 to 2^31, imbalances up to +-60 %, phase
  terms up to +-0.45, some zero and some arbitrary records). Captured: every
  estimate the object read, the tones, the gain-search writes and power
  indices, the coefficients passed to `sub_090fcb` mode 1, `pi_ac+0x8c8` and
  every access of `sub_09107b`; compared with the models fed with the captured
  estimates. 192 calls (rev 0 and rev 1; 36/80, 149/80, 36l, 161u, 36, 6 and
  channel 1): **384 coefficient pairs, 48 filter value pairs and 48
  `sub_09107b` access sequences, 192 gain searches (730 measurements, every
  write of 0x730/0x731/0x734 and every power index), 192 tone lists, the
  arguments of 730 gain-search, 576 tone and 360 image estimates, 192 `img`
  and 192 `two` flags: 0 different**
  (`vc-full.txt`). All branches of the gain-search walk were taken (fast climb,
  in-window, fast drop, climb, drop, settle, forced at 11 and at 255;
  `vc-cov.txt`).
* `verify_digfilt.py`: `sub_09107b(pi, 1)` with 571 chosen value pairs
  (-22..22, the clamp edges, random up to +-2^16, +-2^31-1): 0 different; mode
  0 observed (`vdf.txt`).
* `probe1.py`, `probe2.py soak` (the soak's card): chanspec 0xe02a in every
  calibration after step 105 (X1); `pi_ac+0x900` = 0, `+0x901` = 1; the tone
  loop arguments of X2 (`probe1-*.txt`, `probe2-soak.txt`).
* `probe3.py`: the radio accesses of `sub_094b1a` at 5 GHz on rev 0
  (`RADIO(0x23)` 0 -> 0x200 -> 0x280) and rev 1 (0 -> 1 -> 0x201), and the
  isolating estimate of X7 (`probe3-*.txt`).
* **The soak's IQ powers are zero** (observed, `probe2-soak.txt`: 14 of 14
  estimates of the 36/80 join calibration are 0): `Measurements.phy_read` in
  `tools/re/phy_soak.py` splits the address as `addr >> 9`, `addr & 0x1ff` and
  tests `0x6c0 <= reg < 0x6c6`, which a value below 0x200 never is (the
  per-core registers are `0x6c0 + 0x200*c`, i.e. `addr >> 9` = 3, 4, 5). The
  soak's gain search therefore climbs 4..10 with p = 0 and `pi_ac+0x8c8` is 0;
  the soak does not test the arithmetic of X3 to X6. (Not changed: tools are
  not the analyst's to edit; reported.)

## Open questions

* `pi+0x212` (per-core byte, RSSI gain reference): exact writer/meaning not yet
  traced; the access in `wlc_phy_rssi_compute_acphy` is exact.
* The purpose of the tempsense config word `W` fields and of `pi+0x116a` (its
  0x1800-bandwidth conditional) is unconfirmed; the register writes are exact.
* The slope arithmetic of tempsense could only be verified by injection, not
  against real hardware; the constants are read from the code.
* RX IQ cal coefficients: the arithmetic is now numerically confirmed
  (Verification). The only irreducible point against the synthetic model is the
  *input*: the base `bcm4360.py` never clears `PHY(0x270)` bit 0 and returns 0
  for `PHY(0x6c0..0x6c5)`, so the object writes zero coefficients. To reproduce
  the object's non-zero writes the model must (a) clear `PHY(0x270)` bit 0 after
  an estimate and (b) return the real per-core I/Q powers - genuinely a
  measurement the synthetic card cannot invent. Those two writes per core are
  therefore a documented model limit; the ~35000 structural accesses are not.
* The two-tone digital-filter coefficient (`pi_ac+0x8c8`, `sub_09107b`) only
  runs on 40 MHz; not exercised on the 20 MHz emulator channel. Its numerator
  constant `K` and the remainder term are from the code, not verified.
  **Answered (phy-calrx5g):** it runs at 80 MHz (rev 0/1), K = 30, there is no
  remainder term; verified (X6, X8, "Verification (phy-calrx5g)").
* (phy-calrx5g) The previous item's "model limit" no longer holds: the model
  clears the done bit, and with X3 to X6 the coefficients follow for any
  register values. What stays open is only what real hardware returns.
* (phy-calrx5g) Interpretation only: the meaning of the 10-bit `a`/`b` fields
  (b relative to 1.0?) and of the 11-tap filter rows; the purpose of
  `PHY(0x210)/0x212` fields. The accesses are exact.
* (phy-calrx5g) Not reachable on this card and not described further: rev 3's
  40 MHz two-tone path (K = 15, tones +-15), radio major revision 1's `paldo`
  and second `0x729` mod, the `s5` = 0xff unity-gain path of `sub_0ad89a`.
* `wlc_phy_tx_tone_acphy`, `wlc_phy_stopplayback_acphy`,
  `wlc_phy_txpwr_by_index_acphy`, `sub_09bf99`, `sub_098751`, `sub_09c4e4`,
  `sub_09868f` are treated as opaque here (tx-cal / tx-power topics); only their
  role in the cal is stated.
