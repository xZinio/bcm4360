# PHY attach for the BCM4360 (AC-PHY) and the state it builds

Status: all procedures below were read in the decompiler output and checked
in the disassembly; what was also checked in the emulator is listed in
"Verification". Field names: names of SROM variables are exact; names marked
(bcm) are the names the corresponding fields have in the open driver
`brcmsmac`, which descends from the same code (they fit by position and use,
but the object itself contains no field names); all other names are mine.

## Scope

How the PHY layer is created for chip 0x4360 (AC-PHY revision 0 or 1), what it
reads from the hardware and from the SROM variables while doing so, and the
layout of the state it keeps (`sh`, `pi`, `pi_ac`).

| Function | .text offset | Size | Name |
|---|---|---|---|
| `wlc_phy_shared_attach` | 0x0b7199 | 267 | original |
| `wlc_phy_attach` | 0x0be426 | 4034 | original |
| `sub_0be37f` | 0x0be37f | 167 | assigned: `wlc_phy_read_tempdelta_settings` |
| `sub_0b7337` | 0x0b7337 | 214 | assigned: `wlc_set_phy_uninitted` |
| `sub_0b56ce` | 0x0b56ce | 261 | assigned: `wlc_phy_timercb_phycal` (only its registration is described) |
| `wlc_phy_attach_acphy` | 0x0a194f | 6189 | original |
| `sub_0a1604` | 0x0a1604 | 843 | assigned: `wlc_phy_srom_read_rssicorrnorm_acphy` |
| `wlc_phy_txpwr_srom11_read` | 0x0bf3e8 | 3719 | original |
| `wlc_phy_hirssi_elnabypass_init_acphy` | 0x092555 | 119 | original |
| `wlc_phy_hirssi_elnabypass_set_ucode_params_acphy` | 0x0923f2 | 270 | original |
| `wlc_phy_hwaci_init_acphy` | 0x0927c8 | 509 | original |
| `phy_getvar`, `phy_getvar_fabid`, `phy_getintvar`, `phy_getintvar_default`, `phy_getintvararray`, `phy_getintvararray_default` | 0x0bcd81, 0x0bceaf, 0x0bd032, 0x0bd008, 0x0bcffb, 0x0bceba | 108, 11, 13, 42, 13, 321 | original |
| `sub_0bcded` | 0x0bcded | 194 | assigned: `phy_getvar_internal` (the worker behind `phy_getvar_fabid`) |
| `getvar`, `getintvar`, `getintvararray`, `getintvararraysize` | 0x00b807, 0x00ba1a, 0x00b9c3, 0x00b978 | 112, 30, 87, 75 | original (bcmutils.c) |
| `si_fabid` | 0x024315 | 183 | original (siutils.c); returns 0 for the 4360 |
| `wlc_phy_get_phyversion`, `wlc_phy_get_encore`, `wlc_phy_get_coreflags`, `wlc_phy_machwcap_set` | 0x0b18cc, 0x0b18fb, 0x0b1907, 0x0b1f6f | 47, 12, 12, 13 | original |
| `wlc_phy_cap_get`, `wlc_phy_ac_caps` | 0x0b799d, 0x08e74c | 92, 46 | original |
| `wlc_phy_stf_chain_init` | 0x0b24f7 | 48 | original (called by `wlc_attach`; fills four fields of `sh`) |
| `wlc_phy_detach` | 0x0baed7 | 213 | original |
| `sub_097e2b`, `sub_092e67`, `sub_099528`, `sub_09737d` | 0x097e2b, 0x092e67, 0x099528, 0x09737d | 31, 89, 131, 126 | assigned: `wlc_phy_detach_acphy`, `wlc_phy_txpower_core_offset_get_acphy`, `wlc_phy_txpower_core_offset_set_acphy`, `wlc_phy_watchdog_acphy` (functions behind slots of the function pointer table) |

Not repeated here, see `acphy-radio.md`: the radio id read (its section 2),
`wlc_phy_anacore` (1), `wlc_phy_switch_radio` (3, 4), the ten saved PHY
registers (19) and the use of OTP word 16 at initialisation (9). Register
access: `access.md`.

## Overview

`wlc_bmac_attach` builds the PHY layer in three steps:

1. `wlc_phy_shared_attach(params)` once: allocates the state shared by the
   PHY objects of both bands (`sh`, 0x100 bytes) and copies the identification
   of chip and board into it. No hardware access.
2. `wlc_phy_attach(sh, regs, bandtype, vars)` once per band, first 2.4 GHz
   (`bandtype` 2), then 5 GHz (`bandtype` 1). The first call allocates the PHY
   state `pi` (0x1170 bytes), resets the 802.11 core, reads the PHY version
   from `D11(0x3e0)`, runs the AC-PHY attach `wlc_phy_attach_acphy` (which
   allocates `pi_ac`, 0x920 bytes, fills the function pointer table and reads
   nearly all SROM variables), switches the analog core on, reads the radio
   id and switches the radio off. The BCM4360 has one PHY for both bands (bit
   3 of the core status flags), so the second call only resets the core again
   and returns the same `pi` with its reference counter incremented.
3. After each `wlc_phy_attach` the MAC layer stores the MAC capabilities in
   `sh` (`wlc_phy_machwcap_set`) and asks for the versions
   (`wlc_phy_get_phyversion`, `wlc_phy_get_encore`, `wlc_phy_get_coreflags`).
   It accepts the AC-PHY if `(0xff >> phy revision) & 1` is set, that is
   revisions 0 to 7.

Later `wlc_attach` calls `wlc_phy_stf_chain_init` (chain masks) and
`wlc_phy_cap_get` (capabilities).

### Hardware accesses of attach, in order

First call of `wlc_phy_attach` (2.4 GHz); sequence numbers of
`re-out\up-trace.txt`, values of the emulated card:

| Seq | Access | Made by | Value |
|---|---|---|---|
| 499 | read `WRAP(d11, 0x500)`, 32 bit | `si_core_sflags` in `wlc_phy_attach` step 1 | 0x0000000c |
| 500..517 | core reset of the 802.11 core: reads of `WRAP(d11, 0x408)`, `WRAP(d11, 0x800)`; reset sequence with `WRAP(d11, 0x408)` = 7, `WRAP(d11, 0x800)` = 1, then 0, `WRAP(d11, 0x408)` = 5 (each write read back, `WRAP(d11, 0x804)` read in between); `D11(0x120)` = 0x04000400; `D11(0x1e0)`: read, write with bit 1 set, read | `wlapi_bmac_corereset` (MAC layer), step 7 | |
| 518 | read `D11(0x3e0)`, 16 bit | `wlc_phy_attach` step 8 | 0x0b01 |
| 520..548 | read `PHY(0x739)`, `PHY(0x73a)`, `PHY(0x725)`, `PHY(0x729)`, `PHY(0x721)`, `PHY(0x728)`, `PHY(0x720)`, `PHY(0x408)`, `PHY(0x417)`, `PHY(0x416)` | `wlc_phy_attach_acphy` step 4 | 0 each |
| 549..552 | the PCI windows are moved to ChipCommon and back (`PCICFG(0x80)`, `PCICFG(0x70)`), no register access | `si_alp_clock`, step 9 | |
| 554 | read `PHY(0x0b)` | step 14 | 0 |
| 558 | read `CC(0x190)` | `si_get_sromctl`, step 27 | 0x00000001 |
| 563 | write `CC(0x190)` = old value \| 0x10 (only if bit 4 was clear) | `si_set_sromctl` | 0x00000011 |
| 566..804 | OTP: `otp_read_word(sih, 16, ..)`: power check (`CC(0x60c)`), initialisation of the OTP layer (done on every call: it reads the OTP status `CC(0x1c)`, `CC(0x10)` and the 16 bits of OTP word 19, bits 0x130..0x13f), then the 16 bits of word 16 (bits 0x100..0x10f), each with `CC(0x14)` = 0, `CC(0xf4)` = 0, read `CC(0x1c)`, `CC(0x18)` = 0x80000800 \| column, read `CC(0x18)` | OTP layer (not part of this specification) | all bits 0 (blank OTP) |
| 807 | write `CC(0x190)` = old value (only if bit 4 was clear) | `si_set_sromctl` | 0x00000001 |
| - | second `otp_read_word(sih, 16, ..)`, only if boardflags3 bit 13: the same 239 accesses as seq 566..804 once more, `CC(0x190)` is not touched | step 32 | not in this trace (own run `otp2`) |
| 810 | write `D11(0x3e6)` = 0, 16 bit | `wlc_phy_anacore(pi, 1)`, `wlc_phy_attach` step 19 | |
| 811..814 | write `D11(0x3d8)` = 0, read `D11(0x3da)`; write `D11(0x3d8)` = 1, read `D11(0x3da)` (16 bit each) | radio id, step 19 | 0x0004, 0x2069 |
| 817..842 | read `D11(0x120)` (32 bit); `PHY(0x173e)` = 0x1c00, `PHY(0x1739)` = 0, `PHY(0x173a)` = 0, `PHY(0x1725)` = 0x1fff, `PHY(0x1729)` = 0, `PHY(0x1721)` = 0xffff, `PHY(0x1728)` = 0, `PHY(0x1720)` = 0x03ff; `mod(PHY(0x408), 0x0002, 0)`; `PHY(0x417)` = 0, `PHY(0x416)` = 1 | `wlc_phy_switch_radio(pi, 0)`, step 19 | |

Every move of the PCI windows around the ChipCommon accesses (`PCICFG(0x80)`,
`PCICFG(0x70)`) is left out of the table except for `si_alp_clock`, where it
is the only thing that happens.

Second call (5 GHz), seq 888..924: read `WRAP(d11, 0x500)`; core reset as
above (because the core is up now and DMA engines exist, the MAC layer
first stops the DMA engines: accesses to `D11(0x200)`..`D11(0x2cc)`). Nothing
else.

No other function of this specification accesses the hardware during
attach. `wlc_phy_hirssi_elnabypass_init_acphy` writes shared memory only when
it is called again by the PHY initialisation.

### Variables read at attach (summary)

All variables the PHY layer reads on the BCM4360 path, in the order they are
read. "Conversion": what is done with the number; every value is first
converted with `bcm_strtoul` (32 bit) and then cut to the size of the
field. "Absent": value used when the variable is not in the list. The column
"Used by" names the consumers found in the object (functions of other
specifications), as a hint to the meaning. Units (dB, dBm) and the meanings
given in words are interpretations of the names and of the arithmetic
(unverified); the conversions and places are exact.

| Variable | Read by | Conversion | Absent | Stored in | Used by / meaning |
|---|---|---|---|---|---|
| `phycal_tempdelta` | `sub_0be37f` | 8 bit; above 64: replaced by the default | 0, which is kept (0 is <= 64) | `pi+0xf9c`, `pi+0xf9e` | temperature change that starts a recalibration (`wlc_phy_cal_perical`) |
| `interference` | `wlc_phy_attach` | 32 bit | modes by PHY revision (7 for revision 1, 1 for revision 0) | `sh+0x84`, `sh+0x88`, `sh+0x80` | interference mitigation mode per band |
| `subband5gver` | `wlc_phy_attach_acphy` | 8 bit | 4 | `sh+0x4c` | borders of the 5 GHz sub-bands (`wlc_phy_get_chan_freq_range_acphy`) |
| `extpagain2g`, `extpagain5g` | `wlc_phy_attach_acphy` | 8 bit | 0 | `sh+0xb0`, `sh+0xac` | kind of external power amplifier, read on band change (`sub_09e378`) |
| `femctrl` | `wlc_phy_attach_acphy` | 8 bit | 0 | `pi_ac+0x342` | kind of front end control (`sub_0a602f`, `sub_09e378`, `wlc_phy_tssivisible_thresh_acphy`) |
| `boardflags3` | `wlc_phy_attach_acphy` | 32 bit, split into 12 flags | 0 | `pi_ac+0x343..0x355` | see `pi_ac` table |
| `rpcal2g`, `rpcal5gb0..3` | `wlc_phy_attach_acphy` | 16 bit | 0 | `sh+0xcc..0xd5` | `wlc_phy_populate_recipcoeffs_acphy` |
| `txidxcap2g`, `txidxcap5g` | `wlc_phy_attach_acphy` | 8 bit | 0 | `sh+0xd6`, `sh+0xd7` | upper limit of the tx gain index, read on band change (`sub_09e378`) |
| `pdgain2g`, `pdgain5g` | `wlc_phy_attach_acphy` | 8 bit | 0 | `pi_ac+0x410`, `pi_ac+0x411` | `sub_098949`, `sub_0a4adc`, `sub_09e378` |
| `cckdigfilttype` | `wlc_phy_attach_acphy` | 8 bit | 1 | `pi_ac+0x8fe` | digital filter for CCK (`sub_0a4adc`) |
| `rxgains2gelnagaina<c>`, `rxgains5gelnagaina<c>`, `rxgains5gmelnagaina<c>`, `rxgains5ghelnagaina<c>` | `wlc_phy_attach_acphy` | 2 * v + 6 (8 bit); only read if the band has an external LNA (boardflags bit 12 resp. 28) | 0 (mid/high: copy of the band below) | byte 0 of the receive gain entry | gain of the external LNA in dB (`sub_099658`) |
| `rxgains2gtrisoa<c>`, `rxgains5gtrisoa<c>`, `rxgains5gmtrisoa<c>`, `rxgains5ghtrisoa<c>` | `wlc_phy_attach_acphy` | 2 * v + 8 (8 bit) | 0 (mid/high: copy) | byte 1 of the entry | isolation of the T/R switch in dB |
| `rxgains2gtrelnabypa<c>`, `rxgains5gtrelnabypa<c>`, `rxgains5gmtrelnabypa<c>`, `rxgains5ghtrelnabypa<c>` | `wlc_phy_attach_acphy` | as it is; only read with an external LNA | 0 (mid/high: copy) | byte 2 of the entry | the T/R switch bypasses the external LNA |
| `maxp2ga<c>`, `maxp5ga<c>[k]` | `wlc_phy_txpwr_srom11_read` | 8 bit | 0 | `pi+0xe7c..` | maximum tx power of the board in quarter dBm |
| `pa2ga<c>[0..2]`, `pa5ga<c>[0..11]` | `wlc_phy_txpwr_srom11_read` | 16 bit | 0 | `pi+0xe04..`, `pi+0xe2c..`, `pi+0xe54..` | the three parameters of the power detector curve per band |
| `tssifloor2g`, `tssifloor5g[k]` | `wlc_phy_txpwr_srom11_read` | 16 bit | 0 | `pi+0xea6..` | |
| `cckbw202gpo` .. `sb40and80hr5ghpo` (42 names, see "Tx power offsets") | `wlc_phy_txpwr_srom11_read` | 16 or 32 bit | 0 | `pi+0xc4c..0xd3b` | power offsets per rate group |
| `pdoffset40ma<c>`, `pdoffset80ma<c>` | `wlc_phy_txpwr_srom11_read` | 16 bit | 0 | `pi+0xe90..`, `pi+0xe98..` | |
| `pdoffset2g40ma<c>`, `pdoffset2g40mvalid`, `pdoffsetcckma<c>` | `wlc_phy_txpwr_srom11_read` | 8 bit | 0 | `pi+0xea0..`, `pi+0xea4`, `pi+0xece..` | |
| `tempoffset` | `wlc_phy_txpwr_srom11_read` | 8 bit signed, see procedure 7 | 0 | `pi+0xc35` | cleared again by `wlc_phy_attach` |
| `phycal_tempdelta` (second time) | `sub_0be37f` | as above, default now 40 | 0 | `pi+0xf9c`, `pi+0xf9e` | |
| `rawtempsense` | `wlc_phy_attach_acphy` | 9 bit signed; -1 becomes 255 | 0 | `pi+0x210`, `pi_ac+0x8dc` | reference for the temperature compensation of the gain (`wlc_phy_upd_gain_wrt_temp_phy`) |
| `rxgainerr2ga0`, `rxgainerr5ga0[k]` | `wlc_phy_attach_acphy` | 6 bit signed | 0 | `pi+0x1e3`, `pi+0x1e8+5k` | receive gain error of core 0 |
| `rxgainerr2ga1`, `..a2`, `rxgainerr5ga1[k]`, `..a2[k]` | `wlc_phy_attach_acphy` | 5 bit signed, added to the value of core 0 | 0 | `pi+0x1e4`, `pi+0x1e5`, ... | gain error of core 1 and 2 (the SROM holds the difference to core 0) |
| `noiselvl2ga<c>`, `noiselvl5ga<c>[k]` | `wlc_phy_attach_acphy` | 8 bit, subtracted from -70 | 0 | `pi+0x1fc..0x20f` | noise level per core in dBm |
| `swctrlmap_2g`, `swctrlmapext_2g`, `swctrlmap_5g`, `swctrlmapext_5g` [0..4] | `wlc_phy_attach_acphy`, only with boardflags3 bit 8 | 32 bit | field stays 0 | `pi_ac+0x358..0x3a7` | front end control map (`sub_0a602f`) |
| `rssicorrnorm_c<c>[0..1]`, `rssicorrnorm5g_c<c>[0..11]` | `sub_0a1604` | 8 bit | 0 | `pi_ac+0x3a8..`, `pi_ac+0x3b0..` | RSSI correction (`wlc_phy_rssi_compute_acphy`) |
| `tempthresh` | `wlc_phy_attach` | 8 bit; 0 and 255 become 150; **ignored on the BCM4360 boards 0x117 and 0x111: always 120** | 150 | `pi+0xc2e`, `pi+0xc2f` | temperature limit of the tx chains |
| `temps_hysteresis` | `wlc_phy_attach` | 8 bit; 0 and 15 become 5 | 5 | `pi+0xc30` | |
| `txpwrbckof` | `wlc_phy_attach` | 8 bit | 6 | `pi+0x10a0` | `sub_0b8ca4` (tx power limits) |
| `tssilimucod` | `wlc_phy_attach` | 8 bit | 1 | `pi+0x10b8` | `wlc_phy_tx_pwr_limit_check` |
| `rssicorrnorm`, `rssicorratten` | `wlc_phy_attach` | 8 bit | 0, 7 | `pi+0x10fe`, `pi+0x10ff` | `wlc_phy_rssi_compute` |
| `rssicorrnorm5g[0..2]`, `rssicorratten5g[0..2]` | `wlc_phy_attach` | 8 bit | 0 | `pi+0x1100..`, `pi+0x1103..` | |
| `rssicorrperrg2g[0..4]`, `rssicorrperrg5g[0..4]` | `wlc_phy_attach` | 8 bit | -150, -150, 0, 0, 0 | `pi+0x1106..`, `pi+0x110b..` | |
| `5g_cga[0..23]`, `2g_cga[0..13]` | `wlc_phy_attach` | 8 bit | 0 | `pi+0x1110..`, `pi+0x1128..` | |

Of these, SROM revision 11 defines: `phycal_tempdelta`, `subband5gver`,
`femctrl`, `boardflags3`, `rpcal*`, `txidxcap*`, `pdgain*`, `rxgains*`, `maxp*`,
`pa2ga*`, `pa5ga*`, `tssifloor*`, the power offsets, `pdoffset40ma*`,
`pdoffset80ma*`, `pdoffset2g40m*`, `tempoffset`, `rawtempsense`, `rxgainerr*`,
`noiselvl*`, `tempthresh`, `temps_hysteresis`. The others (`interference`,
`extpagain2g`, `extpagain5g`, `cckdigfilttype`, `pdoffsetcckma*`, `swctrlmap*`,
`rssicorr*`, `txpwrbckof`, `tssilimucod`, `5g_cga`, `2g_cga`) can only come from
a variable list that is not made from the SROM; on this card they take
their defaults.

Variables of SROM revision 11 that no PHY function of the BCM4360 path
reads (searched in the whole object by the name strings): `tssiposslope2g`,
`tssiposslope5g`, `epagain2g`, `epagain5g`, `tworangetssi2g`, `tworangetssi5g`,
`papdcap2g`, `papdcap5g`, `gainctrlsph`, `paparambwver`, `pa2gccka0`,
`pa5gbw40a0`, `pa5gbw80a0`, `pa5gbw4080a0` (no reader at all); `measpower`,
`measpower1`, `measpower2`, `tempsense_slope`, `tempcorrx`, `tempsense_option`
(read only by the attach of other PHY types); `pa5gbw4080a1` (PHY revision 3
only). `temps_period`, `sar2g`, `sar5g`, `xtalfreq`, `aa2g` and the other
antenna and chain variables are read outside the PHY layer.

At the end of `wlc_phy_attach` the pointer to the SROM variables in `pi` is
made unusable (it is set to its own address): **all variables are read at
attach**, nothing can be looked up later. Variables that are not in the list
get defaults; the list is made from the SROM (see the SROM specification),
so names that SROM revision 11 does not define (`interference`, `txpwrbckof`,
`rssicorrnorm_c0`, `swctrlmap_2g`, ...) are absent on this card and always
produce their defaults.

## Data

### Variable list

`vars` is a sequence of strings `name=value`, each ended by a NUL, the list
ended by an empty string. Values are decimal, hexadecimal (`0x`) or octal
(leading `0`) numbers, arrays are numbers separated by commas.

### Argument of wlc_phy_shared_attach

`wlc_bmac_attach` fills a record on its stack and passes its address.

| Record offset | Size | Content (source in `wlc_bmac_attach`) | Copied to |
|---|---|---|---|
| 0x00 | 8 | `osh` | `sh+0x10` |
| 0x08 | 8 | `sih` (`wlc_hw+0xb8`) | `sh+0x18` |
| 0x10 | 8 | PHY shim handle (`wlc_hw+0xd8`) | `sh+0x20` |
| 0x18 | 4 | unit number | `sh+0x08` |
| 0x1c | 4 | MAC core revision (`wlc_hw+0x84`) | `sh+0x28` |
| 0x20 | 4 | bus type (`sih+0x04`) | `sh+0x6c` |
| 0x24 | 4 | bus core revision (`sih+0x0c`) | `sh+0x70` |
| 0x28 | 8 | `vars` | not copied (`wlc_phy_attach` gets it as an argument) |
| 0x30 | 2 | PCI vendor id (`wlc_hw+0x80`) | `sh+0x38` |
| 0x32 | 2 | PCI device id (`wlc_hw+0x82`) | `sh+0x3a` |
| 0x34 | 4 | chip id (`sih+0x3c`) | `sh+0x3c` |
| 0x38 | 4 | chip revision (`sih+0x40`) | `sh+0x40` |
| 0x3c | 4 | chip package (`sih+0x44`) | `sh+0x44` |
| 0x40 | 4 | SROM revision (`wlc_hw+0x88`, a byte, zero extended) | `sh+0x48` |
| 0x44 | 4 | board type (`sih+0x28`) | `sh+0x58` |
| 0x48 | 4 | board revision (`wlc_hw+0x8a`, 16 bit, zero extended) | `sh+0x5c` |
| 0x4c | 4 | board vendor (`sih+0x30`) | `sh+0x60` |
| 0x50 | 4 | boardflags (`wlc_hw+0x8c`) | `sh+0x64` |
| 0x54 | 4 | boardflags2 (`wlc_hw+0x90`) | `sh+0x68` |

### `sh`: shared PHY state (0x100 bytes)

"Set by": S = `wlc_phy_shared_attach`, A = `wlc_phy_attach`, C =
`wlc_phy_attach_acphy`. Values in the column "Emulator" are those after
`wlc_attach` with the synthetic SROM.

| Offset | Size | Field | Set by, value | Emulator |
|---|---|---|---|---|
| 0x00 | 8 | `phy_head` (bcm): first `pi` of the list | A: the new `pi` | |
| 0x08 | 4 | `unit` | S | 0 |
| 0x10 | 8 | `osh` | S | |
| 0x18 | 8 | `sih` | S | |
| 0x20 | 8 | `physhim`: handle of the PHY shim | S | |
| 0x28 | 4 | `corerev`: MAC core revision | S | 42 |
| 0x2c | 4 | `machwcap`: MAC capabilities | `wlc_phy_machwcap_set` | 0 |
| 0x30 | 1 | `up` (bcm): driver is up; tested by the calibration timer and tx power functions | not at attach | 0 |
| 0x31 | 1 | `clk` (bcm): the core's clock is on, registers may be accessed | not at attach | 0 |
| 0x34 | 4 | `now` (bcm): watchdog counter in seconds (unverified) | not at attach | 0 |
| 0x38, 0x3a | 2, 2 | `vid`, `did`: PCI vendor and device id | S | 0x14e4, 0x43a0 |
| 0x3c | 4 | `chip` | S | 0x4360 |
| 0x40 | 4 | `chiprev` | S | 3 |
| 0x44 | 4 | `chippkg` | S | 0 |
| 0x48 | 4 | `sromrev` | S | 11 |
| 0x4c | 4 | `subband5gver` | C: variable, 8 bit; default 4 | 4 |
| 0x58 | 4 | `boardtype` | S | 0x117 |
| 0x5c | 4 | `boardrev` | S | 0x1101 |
| 0x60 | 4 | `boardvendor` | S | 0x106b |
| 0x64 | 4 | `boardflags` | S | 0x10401001 |
| 0x68 | 4 | `boardflags2` | S | 2 |
| 0x6c | 4 | `bustype` | S | 1 |
| 0x70 | 4 | `buscorerev` | S | 1 |
| 0x74 | 4 | `fast_timer` (bcm) | S: 15 | |
| 0x78 | 4 | `slow_timer` (bcm) | S: 60 | |
| 0x7c | 4 | `glacial_timer` (bcm) | S: 120 | |
| 0x80 | 4 | `interference_mode`: mode in use | S: 3; C: 0 | 0 |
| 0x84 | 4 | `interference_mode_2G`: mode configured for 2.4 GHz | A: see procedure | 7 |
| 0x88 | 4 | `interference_mode_5G`: mode configured for 5 GHz | A | 7 |
| 0x8c, 0x90 | 4, 4 | modes used instead of 0x84/0x88 while `sh+0x94` is 1 | not at attach | 0 |
| 0x94 | 1 | selects 0x8c/0x90 (1) or 0x84/0x88 (0) as source of `sh+0x80` | A: 0 | 0 |
| 0x95 | 1 | `rx_antdiv` (bcm): argument of `wlc_phy_ant_rxdiv_set` at init | not at attach | 0 |
| 0x96 | 8 x 1 | `phy_noise_window` (bcm) | S: 0 | |
| 0xa4 | 1 | `hw_phytxchain` | `wlc_phy_stf_chain_init`: tx chain mask | 3 |
| 0xa5 | 1 | `hw_phyrxchain` | `wlc_phy_stf_chain_init`: rx chain mask | 3 |
| 0xa6 | 1 | `phytxchain` | `wlc_phy_stf_chain_init`: tx chain mask | 3 |
| 0xa7 | 1 | `phyrxchain` | A: 3; `wlc_phy_stf_chain_init`: rx chain mask | 3 |
| 0xa8 | 1 | `rssi_mode` (bcm) | S: 0 | |
| 0xac | 4 | `extpagain5g` | C: variable, 8 bit; default 0 | 0 |
| 0xb0 | 4 | `extpagain2g` | C: variable, 8 bit; default 0 | 0 |
| 0xcc | 2 | `rpcal2g` | C: variable, 16 bit; default 0 | 0xffff |
| 0xce, 0xd0, 0xd2, 0xd4 | 2 each | `rpcal5gb0` .. `rpcal5gb3` | C: variables, 16 bit; default 0 | 0xffff |
| 0xd6 | 1 | `txidxcap2g` | C: variable, 8 bit; default 0 | 0 |
| 0xd7 | 1 | `txidxcap5g` | C: variable, 8 bit; default 0 | 0 |
| 0xf2 | 2 | RCAL value: bits 0..3 of OTP word 16 | C: only if boardflags3 bit 13 | 0 |

### `pi`: PHY state (0x1170 bytes)

A = `wlc_phy_attach`, C = `wlc_phy_attach_acphy`, T =
`wlc_phy_txpwr_srom11_read`, U = `sub_0b7337`, D = `sub_0be37f`. "s8" etc.
give the type the consumers use where that is known.

| Offset | Size | Field | Set by, value | Emulator |
|---|---|---|---|---|
| 0x00 | 0x1c | `pubpi_ro` (bcm): copy of `pi+0x160..0x17b` | A, last step | |
| 0x20 | 8 | `sh` | A | |
| 0x28..0x130 | 8 each | function pointer table, see below | C | |
| 0x138 | 8 | `pi_ac` | C | |
| 0x140 | 1 | `user_txpwr_at_rfport` (bcm) | A: 0 | |
| 0x148 | 8 | `regs`: D11 base | A | |
| 0x150 | 8 | `next`: next `pi` in the list of `sh` | A: old `sh+0x00` | 0 |
| 0x158 | 8 | `vars`; after attach the address of this field itself | A | |
| 0x160 | 4 | `phy_type` | A: bits 8..11 of `D11(0x3e0)` | 11 |
| 0x164 | 4 | `phy_rev` | A: bits 0..3 of `D11(0x3e0)` | 1 |
| 0x168 | 1 | `phy_corenum`: number of cores N | A: 1; C: see procedure | 2 |
| 0x16a | 2 | `radioid` | A | 0x2069 |
| 0x16c | 1 | `radiorev` | A | 4 |
| 0x16d | 1 | `radiover` | A: 0 | |
| 0x16e | 1 | radio major revision | A | 0 |
| 0x16f | 1 | radio minor revision | A | 4 |
| 0x170 | 4 | `coreflags`: flags for the core reset | A: 0x2000 for a PHY that only does 2.4 GHz, else 0 | 0 |
| 0x174 | 4 | `ana_rev`: bits 12..15 of `D11(0x3e0)` | A | 0 |
| 0x178 | 1 | `abgphy_encore` (bcm), result of `wlc_phy_get_encore` | never for the AC-PHY: 0 | 0 |
| 0x17e | 2 | `radio_chanspec` | A: 0x1001 (2.4 GHz) or 0xd024 (5 GHz) | 0x1001 |
| 0x182 | 2 | `bw`: bandwidth bits the MAC is set to | A: 0x1000 | |
| 0x184 | 1 | `txpwr_percent` | A: 100 | |
| 0x185 | 1 | `phy_init_por`: first init after power on reset | A: 1 | |
| 0x186 | 1 | PHY init in progress | not at attach | |
| 0x187 | 1 | `initialized`: `wlc_phy_cal_init` done | U: 0 | |
| 0x188 | 4 | `refcnt` | A: +1 per call | 2 |
| 0x18c | 1 | `watchdog_override` (bcm) | A: 1 | |
| 0x198 | 1 | `phynoise_polling` (bcm) | A: 0 for the AC-PHY | |
| 0x1c8 | 8 | pointer to the tx power limit object (`ppr`), created later | A: 0 | |
| 0x1d6 | 1 | user tx power target (written by `wlc_phy_txpower_set`) | A: 0x7f | |
| 0x1e3 | 3 x s8 | `rxgainerr2g[core]` | C, see procedure | 0 |
| 0x1e7 | 1 | 2.4 GHz gain errors are empty | A: 0; C | 1 |
| 0x1e8 + 5k | 3 x s8 | `rxgainerr5g[k][core]`, k = 0..3 (sub-band) | C | 0 |
| 0x1ec + 5k | 1 | 5 GHz gain errors of sub-band k are empty | A: 0; C | 1 |
| 0x1fc | 4 x s8 | `noiselvl2g[core]` | C | 0x9b, 0x9b, 0xe1, 0 |
| 0x200 + 4k | 4 x s8 | `noiselvl5g[k][core]`, k = 0..3 | C | 0x9b, 0x9b, 0xe1, 0 |
| 0x210 | s16 | `rawtempsense` | C | 0x00ff |
| 0x224 | 1 | `phyhang_avoid` (bcm) (read only by N-PHY code) | C: 1 | |
| 0x226 | 2 | counter of register writes in a row | A: 0 after the radio id | |
| 0x228 | 2 | `phy_wreg_limit`: write pacing limit | A: 24 (1 for board vendor 0x106b with board type 0x93) | 24 |
| 0x22a | 1 | preamble override (`wlc_phy_preamble_override_set`/`_get`) | C: 0 | |
| 0x236 | s16 | `radiopwr_override` (bcm) (unverified) | A: 0xffff | |
| 0x240 | 1 | channel number the interference code was last set up for | A: low byte of the first chanspec | 1 |
| 0xc04 | 4 | purpose unknown, read by `wlc_phy_watchdog` | A: 60 (15 for PHY types 4 and 7) | 60 |
| 0xc08 | 4 | purpose unknown | A: 16 | |
| 0xc0c | 4 | flags of the interference code (`sub_0b740d`) | A: 0 | |
| 0xc24 | 4 | crystal frequency in Hz | C: `si_alp_clock(sih)` | 40000000 |
| 0xc2b | 1 | `phy_scraminit` (bcm) (only written) | C: 0xff | |
| 0xc2e | 1 | `tempthresh`: temperature at which tx chains are switched off | A, see procedure | 0x78 |
| 0xc2f | 1 | copy of 0xc2e (upper limit of the threshold) | A | 0x78 |
| 0xc30 | 1 | `temps_hysteresis` | A | 5 |
| 0xc31 | 1 | temperature at which the chains are switched on again | A: 0xc2e - 0xc30 | 0x73 |
| 0xc32 | 1 | chip is heated up | A: 0 | |
| 0xc33 | 1 | chain bitmap: mask of all cores in both nibbles | A | 0x33 |
| 0xc34 | 1 | purpose unknown | A: 0 | |
| 0xc35 | s8 | `tempoffset` | A: 0; T, see procedure | 0 |
| 0xc36 | 1 | purpose unknown (older PHYs) | U: 0xff | |
| 0xc37 | 1 | `min_txpower` | A: 1 (5 for PHY revision 3) | 1 |
| 0xc40 | 1 | `hwpwrctrl_capable` | C: 1 | |
| 0xc4c..0xd3b | | tx power offsets, see below | T | |
| 0xe04 | 20 x s16 | `pa_a[core][band]`: first PA parameter | T | |
| 0xe2c | 20 x s16 | `pa_b[core][band]`: second PA parameter | T | |
| 0xe54 | 20 x s16 | `pa_c[core][band]`: third PA parameter | T | |
| 0xe7c | 15 x u8 | `maxp[core][band]` | T | |
| 0xe90, 0xe92, 0xe94 | 2 each | `pdoffset40ma0`, `..ma1`, `..ma2` | T | 0x1111 |
| 0xe98, 0xe9a, 0xe9c | 2 each | `pdoffset80ma0`, `..ma1`, `..ma2` | T | 0 |
| 0xea0, 0xea1, 0xea2 | 1 each | `pdoffset2g40ma0`, `..ma1`, `..ma2` | T | 15 |
| 0xea4 | 1 | `pdoffset2g40mvalid` | T | 1 |
| 0xea6 | 2 | `tssifloor2g` | T | 0x3ff |
| 0xea8, 0xeaa, 0xeac, 0xeae | 2 each | `tssifloor5g[0..3]` | T | 0x3ff |
| 0xece, 0xecf, 0xed0 | 1 each | `pdoffsetcckma0`, `..ma1`, `..ma2` | T | 0 |
| 0xed2 | 1 | purpose unknown | U: 0xff | |
| 0xf58 | 8 | pointer to the calibration state, always `pi + 0xfb8` | A | |
| 0xf68, 0xf69 | 1, 1 | purpose unknown (changed only by an iovar) | A: 10, 3 | |
| 0xf82 | 1 | purpose unknown (`wlc_phy_dig_lpf_override_acphy`) | A: 0 | |
| 0xf88 | 1 | radio is on | not at attach | 0 |
| 0xf89 | 1 | calibration mode: 2 = periodic calibration in several phases | C: 2 | |
| 0xf8a | 2 | delay between two calibration phases in ms | C: 5 | |
| 0xf9c | 1 | `phycal_tempdelta` | D | 40 |
| 0xf9d | 1 | purpose unknown | A: 4 | |
| 0xf9e | 1 | default of `phycal_tempdelta` | A: 0; T: 40; D | 40 |
| 0xf9f | 1 | purpose unknown | A: 4 | |
| 0xfa0 | 1 | tx power control by hardware is enabled | C: 1 | |
| 0xfb8 | | calibration state (byte 1: phase of the periodic calibration) | zero | |
| 0x1080 | 4 | purpose unknown | A: 0 | |
| 0x1084 | s16 | temperature of the last calibration (unverified) | A: -50 | |
| 0x1088 | 8 | timer "phycal" | A | |
| 0x10a0 | 1 | `txpwrbckof` | A: variable, default 6 | 6 |
| 0x10b8 | 1 | `tssilimucod` | A: variable, default 1 | 1 |
| 0x10fc | 2 | fab id from `si_fabid` | A | 0 |
| 0x10fe | 1 | `rssicorrnorm` | A: variable, default 0 | 0 |
| 0x10ff | 1 | `rssicorratten` | A: variable, default 7 | 7 |
| 0x1100 | 3 x 1 | `rssicorrnorm5g[0..2]` | A: default 0 | 0 |
| 0x1103 | 3 x 1 | `rssicorratten5g[0..2]` | A: default 0 | 0 |
| 0x1106 | 5 x 1 | `rssicorrperrg2g[0..4]` | A: defaults -150 (stored 0x6a), -150, 0, 0, 0 | 0x6a, 0x6a, 0, 0, 0 |
| 0x110b | 5 x 1 | `rssicorrperrg5g[0..4]` | A: the same defaults | 0x6a, 0x6a, 0, 0, 0 |
| 0x1110 | 24 x 1 | `5g_cga[0..23]` | A: default 0 | 0 |
| 0x1128 | 14 x 1 | `2g_cga[0..13]` | A: default 0 | 0 |
| 0x1144 | 8 x 4 | purpose unknown | A: 0xffffffff each | |
| 0x1165 | 1 | purpose unknown (only written) | C: 0 | |

Bands of the per band arrays: index 0 = 2.4 GHz, 1..4 = 5 GHz sub-bands 0..3
(in the order of the elements of the SROM arrays). `pa_a`, `pa_b`, `pa_c` have
room for 4 cores and 5 bands, element address = base + core * 10 + band * 2;
`maxp`: base + core * 5 + band.

The band index of a channel is computed by
`wlc_phy_get_chan_freq_range_acphy(pi, channel)` (.text+0x08edad, name
original; it belongs to the tx power specification and was read here only
to confirm the index; its callers `wlc_phy_get_paparams_for_band_acphy` and
`wlc_phy_txpower_sromlimit_get_acphy` index `pi+0xe04..` and `pi+0xe7c..` with
it exactly as described above). `channel` = 0 stands for the channel of
`pi+0x17e`. Channels up to 14: index 0. Otherwise with f = centre frequency in
MHz of the channel (from the channel table of the radio, 0 if the channel is
not in it) and v = `sh+0x4c` (`subband5gver`):

| v | Index 1 | Index 2 | Index 3 | Index 4 |
|---|---|---|---|---|
| 4 (default) | 5170 <= f <= 5249 | 5250 <= f <= 5499 | 5500 <= f <= 5744 | all other f |
| 0 | 5170 <= f <= 5499 | 5500 <= f <= 5744 | all other f | - |
| 1 | 5170 <= f <= 5249 | 5250 <= f <= 5744 | all other f | - |
| other | 4900 <= f <= 5099 | 5100 <= f <= 5499 | all other f | - |

**Tx power offsets** (all written only if `sh+0x48` (SROM revision) > 10):

| Offset | Size | Content |
|---|---|---|
| 0xc4c | 2 | `cckbw202gpo` |
| 0xc4e | 2 | `cckbw20ul2gpo` |
| 0xc50 | 4 | OFDM offsets for 2.4 GHz: `dot11agofdmhrbw202gpo << 16 \| n1 << 12 \| n1 << 8 \| n0 << 4 \| n0`, n0 = bits 0..3 and n1 = bits 4..7 of `ofdmlrbw202gpo` |
| 0xc54, 0xc58 | 4, 4 | copies of 0xc50 |
| 0xc5c | 4 | `mcsbw202gpo` |
| 0xc60 | 4 | copy of 0xc5c |
| 0xc64 | 4 | `mcsbw402gpo` |
| 0xc68 | 4 | not written (0) |
| 0xc6c, 0xc70, 0xc74 | 4 each | `mcsbw205glpo`, `mcsbw205gmpo`, `mcsbw205ghpo` |
| 0xc78..0xc80 | 3 x 4 | copy of 0xc6c..0xc74 |
| 0xc84..0xc8c | 3 x 4 | copy of 0xc6c..0xc74 |
| 0xc90..0xc98 | 3 x 4 | copy of 0xcd8..0xce0 (`mcsbw405g?po`) |
| 0xc9c..0xca4 | 3 x 4 | copy of 0xcd8..0xce0 |
| 0xca8..0xcb0 | 3 x 4 | copy of 0xcf0..0xcf8 (`mcsbw805g?po`) |
| 0xcb4, 0xcb8, 0xcbc | 4 each | `mcsbw205glpo`, `mcsbw205gmpo`, `mcsbw205ghpo` |
| 0xcc0..0xcc8 | 3 x 4 | copy of 0xcb4..0xcbc |
| 0xccc..0xcd4 | 3 x 4 | copy of 0xcb4..0xcbc |
| 0xcd8, 0xcdc, 0xce0 | 4 each | `mcsbw405glpo`, `mcsbw405gmpo`, `mcsbw405ghpo` |
| 0xce4..0xcec | 3 x 4 | copy of 0xcd8..0xce0 |
| 0xcf0, 0xcf4, 0xcf8 | 4 each | `mcsbw805glpo`, `mcsbw805gmpo`, `mcsbw805ghpo` |
| 0xd14 | 2 | `ofdmlrbw202gpo` |
| 0xd16, 0xd18 | 2, 2 | `sb20in40lrpo`, `sb20in40hrpo` |
| 0xd1a, 0xd1c | 2, 2 | `dot11agduplrpo`, `dot11agduphrpo` |
| 0xd1e, 0xd20, 0xd22 | 2 each | `mcslr5glpo`, `mcslr5gmpo`, `mcslr5ghpo` |
| 0xd24, 0xd26, 0xd28 | 2 each | `sb20in80and160lr5glpo`, `..lr5gmpo`, `..lr5ghpo` |
| 0xd2a, 0xd2c, 0xd2e | 2 each | `sb20in80and160hr5glpo`, `..hr5gmpo`, `..hr5ghpo` |
| 0xd30, 0xd32, 0xd34 | 2 each | `sb40and80lr5glpo`, `..lr5gmpo`, `..lr5ghpo` |
| 0xd36, 0xd38, 0xd3a | 2 each | `sb40and80hr5glpo`, `..hr5gmpo`, `..hr5ghpo` |

What the copies stand for (which rate group and bandwidth each of the 4 byte
fields belongs to) is decided by the consumer `wlc_phy_txpwr_apply_srom11`,
which is part of the tx power specification.

### `pi_ac`: AC-PHY state (0x920 bytes)

C = `wlc_phy_attach_acphy`, R = `sub_0a1604`, H =
`wlc_phy_hirssi_elnabypass_init_acphy`, W = `wlc_phy_hwaci_init_acphy`, U =
`sub_0b7337`. Everything not listed is zero after attach.

| Offset | Size | Field | Set by, value | Emulator |
|---|---|---|---|---|
| 0x000 | 1 | purpose unknown | C: 1 | |
| 0x010 | 4 x 1 | tx power index per core ("uninitialised" value) | U: 0x40 each (0x3c for PHY revisions 2, 5, 6) | |
| 0x018 | 5 x 4 x s8 | CRS minimum power calibration: noise power the thresholds were last computed for, element = 0x18 + 4 * band + core (band index as for the tx power arrays) | C: cores 0..2 of all five bands 0xe2 (-30) for PHY revision 0/1 | |
| 0x02c | 4 x 4 x s8 | the same calibration: the last four noise samples, element = 0x2c + 4 * sample + core | C: cleared | |
| 0x03c | 1 | the same calibration: index of the next sample (0..3) | C: 0 | |
| 0x03d | 4 x s8 | the same calibration: averaged noise per core of the last run | C: cleared | |
| 0x042 | 1 | the same calibration: threshold in use | C: 0x36 | |
| 0x043 | 1 | the same calibration: counter of runs | C: 0 | |
| 0x044 | 1 | the same calibration: channel number of the last run | C: 0 | |
| 0x32c | 1 | 1 while the PHY init calls the channel function | C: 0 | |
| 0x32d | 1 | PHY init done | zero | |
| 0x330 | 1 | band last set up: 1 = 2.4 GHz | C: from the chanspec | 1 |
| 0x334 | 4 | bandwidth bits last set up | C: chanspec & 0x3800 | 0x1000 |
| 0x338 | 1 | spur mode in use (`wlc_phy_set_spurmode`) | C: 0 | |
| 0x339, 0x33a | 1, 1 | RCCAL results, defaults | C: 0x80 | |
| 0x33b | 1 | RCCAL result for the DAC buffer, default | C: 0x0c | |
| 0x33c | 1 | CRS minimum power calibration enabled | C: 1 | |
| 0x33d, 0x33e | 1, 1 | state of that calibration | C: 0 | |
| 0x340 | 1 | boardflags bit 12: external LNA for 2.4 GHz | C | 1 |
| 0x341 | 1 | boardflags bit 28: external LNA for 5 GHz | C | 1 |
| 0x342 | 1 | `femctrl` | C: variable, default 0 | 3 |
| 0x343 | 1 | boardflags3 bits 0..2 | C | 0 |
| 0x344 | 1 | boardflags2 bit 1 (only written) | C | 1 |
| 0x345 | 1 | boardflags3 bit 3: skip RCAL, fixed value | C | 0 |
| 0x346 | 1 | boardflags3 bit 9 | C | 0 |
| 0x347 | 1 | boardflags3 bit 10 | C | 0 |
| 0x348 | 1 | boardflags3 bit 7 | C | 0 |
| 0x349 | 1 | boardflags3 bits 4..6 | C | 0 |
| 0x34a | 1 | boardflags bit 29 | C | 0 |
| 0x34b | 1 | boardflags bit 0 | C | 1 |
| 0x34c | 1 | boardflags3 bit 8: switch control maps come from the SROM | C | 0 |
| 0x34d | 1 | boardflags3 bit 11 | C | 0 |
| 0x34e | 1 | boardflags3 bit 13: skip RCAL, value from OTP | C | 0 |
| 0x34f | 1 | boardflags3 bit 12 (spur mode, `wlc_phy_get_spurmode`) | C | 0 |
| 0x354 | 1 | boardflags3 bit 14 | C | 0 |
| 0x355 | 1 | boardflags3 bit 15 | C | 0 |
| 0x358 | 5 x 4 | `swctrlmap_2g[0..4]` | C: only if `pi_ac+0x34c` and the variable exists | 0 |
| 0x36c | 5 x 4 | `swctrlmapext_2g[0..4]` | C: the same | 0 |
| 0x380 | 5 x 4 | `swctrlmap_5g[0..4]` | C: the same | 0 |
| 0x394 | 5 x 4 | `swctrlmapext_5g[0..4]` | C: the same | 0 |
| 0x3a8 + 2c | 2 x 1 | `rssicorrnorm_c<c>[0..1]`, c = 0..2 | R: default 0 | 0 |
| 0x3b0 + 12c | 12 x 1 | `rssicorrnorm5g_c<c>[0..11]`, c = 0..2 | R: default 0 | 0 |
| 0x3e0 + 12b + 3c | 3 x 1 | receive gains of band b (0 = 2.4 GHz, 1 = 5 GHz low, 2 = mid, 3 = high) and core c: byte 0 gain of the external LNA, byte 1 isolation of the T/R switch, byte 2 T/R switch is used to bypass the external LNA | C, see procedure | 0x0e 0x16 0x01 (2.4 GHz), 0x0c 0x16 0x01 (5 GHz) |
| 0x410 | 1 | `pdgain2g` | C: variable, default 0 | 4 |
| 0x411 | 1 | `pdgain5g` | C: variable, default 0 | 4 |
| 0x44a | 2 | purpose unknown (used by `wlc_phy_scanroam_cache_cal_acphy` and `wlc_phy_cals_acphy`) | C: 0 | |
| 0x456 | 4 x s8 | tx power offset per core (slots 0xc8/0xd0 of the function table) | zero | |
| 0x45a | 4 x 1 | per core, purpose unknown (used by `wlc_phy_txpwrctrl_enable_acphy`) | C: 0x80 each | |
| 0x64a | 6 x 1 | purpose unknown | C: 2, 6, 7, 10, 8, 8 | |
| 0x656, 0x65f, 0x668 | 9 each | purpose unknown | C: cleared | |
| 0x671 | 1 | purpose unknown (read by `wlc_phy_desense_aci_engine_acphy`) | C: 1 | |
| 0x672..0x6c3 | | parameters of the interference mitigation, see `wlc_phy_hwaci_init_acphy` | W | |
| 0x6c8, 0x7b8 | 0xf0 each | purpose unknown | C: cleared | |
| 0x8a8 | 8 | purpose unknown | C: 0 | |
| 0x8b0 | 4 | purpose unknown | C: 0 | |
| 0x8b4 | 9 | purpose unknown | C: cleared | |
| 0x8dc | s16 | `rawtempsense`, the same value as `pi+0x210` | C | 0x00ff |
| 0x8de, 0x8df, 0x8e0 | 1 each | result of `wlc_phy_get_olpc_pwroffset` for 20, 40 and 80 MHz (power offset of the open loop power control) | C: 1 | |
| 0x8e1 | 1 | purpose unknown | C: 0 | |
| 0x8e2 | 2 | bits 8..12 of OTP word 16 | C | 0 |
| 0x8e4 | 1 | low power mode (used by PHY revisions 2, 5, 6 only) | C: 1 | |
| 0x8e5 | 1 | force low power VCO on 2.4 GHz | C: 1 for PHY revisions 2, 5, 6, else 0 | 0 |
| 0x8e6, 0x8e7 | 1, 1 | purpose unknown (0x8e6 is also written by the channel look-up `sub_08e9b1`, 0x8e7 is read by an iovar) | C: 1 | |
| 0x8e8 | 1 | purpose unknown | C: 0 | |
| 0x8ea..0x8fd | 10 x 2 | saved PHY registers (`acphy-radio.md`, section 19) | C | 0 |
| 0x8fe | 1 | `cckdigfilttype` | C: variable, default 1 | 1 |
| 0x8ff | 1 | LDPC decoding is on (`wlc_phy_update_rxldpc_acphy` keeps bit 6 of `PHY(0x1b0)` in step with it) | C: 1 | |
| 0x900 | 1 | purpose unknown (read by `wlc_phy_rx_iq_est_acphy`) | C: 0 | |
| 0x901 | 1 | purpose unknown (read by `wlc_phy_rx_iq_est_acphy`) | C: 1 | |
| 0x902 | 2 | purpose unknown (read by `sub_0979bc`, which `wlc_phy_stay_in_carriersearch_acphy` calls) | C: 0x404e | |
| 0x904 | 2 | value that `wlc_phy_stay_in_carriersearch_acphy` writes to `PHY(0x339)` when the carrier search ends | C: 0x0fff | |
| 0x906 | 1 | high RSSI LNA bypass: state to start with | C: 0 | |
| 0x908 | 2 | high RSSI LNA bypass: purpose unknown | C: 5 | |
| 0x90a | 2 | high RSSI LNA bypass: count for the first kind of period, 20 MHz | C: 31 | |
| 0x90c | 2 | the same for the second kind of period | C: 31 | |
| 0x90e | s8 | high RSSI LNA bypass: threshold for the first kind of period | C: -13 (0xf3) | |
| 0x90f | s8 | threshold for the second kind | C: -15 (0xf1) | |
| 0x910 | 1 | high RSSI LNA bypass: state on 2.4 GHz | H | 0 |
| 0x911 | 1 | the same on 5 GHz | H | 0 |
| 0x912 | 1 | high RSSI LNA bypass is supported: PHY revision <= 1 | C | 1 |
| 0x914 | s16 | high RSSI LNA bypass: timer on 2.4 GHz, negative = not running | H: 0xffff | |
| 0x916 | s16 | the same on 5 GHz | H: 0xffff | |
| 0x918 | 1 | core index that the watchdog function puts into `PHY(0x520)` | C: 0 | |

### Function pointer table in `pi`

Slots that are called somewhere in wlc_phy_cmn.c, and what the AC-PHY puts
there. All other slots of 0x28..0x130 are never set for the AC-PHY (zero);
every caller tests for zero before it calls.

| Slot | AC-PHY function | Called by (wlc_phy_cmn.c) | Arguments | Purpose |
|---|---|---|---|---|
| 0x28 | `sub_0b018f` (`wlc_phy_init_acphy`) | `wlc_phy_init` | pi | initialise the PHY; if the slot is zero `wlc_phy_init` returns early |
| 0x30 | `sub_08e77a` (`wlc_phy_cal_init_acphy`) | `wlc_phy_cal_init` | pi | one time initialisation of the calibration state |
| 0x38 | `sub_0a7089` (`wlc_phy_chanspec_set_acphy`) | `wlc_phy_chanspec_set` | pi, chanspec | tune to a channel |
| 0x40 | `sub_09949f` (`wlc_phy_txpower_recalc_target_acphy`) | `wlc_phy_neg_txpower_set`, `sub_0b8ca4` (the function that computes the power limits, called by `wlc_phy_txpower_set`, `wlc_phy_txpower_limit_set`, `wlc_phy_txpower_get_current`) | pi | apply new tx power targets |
| 0xc0 | `sub_097e2b` | `wlc_phy_detach` | pi | free the PHY specific state |
| 0xc8 | `sub_092e67` | `wlc_phy_txpower_core_offset_get` | pi, address of 4 bytes | read the power offsets of the cores |
| 0xd0 | `sub_099528` | `wlc_phy_txpower_core_offset_set` | pi, address of 4 bytes | set them |
| 0xe8 | 0 | `wlc_phy_txpower_get_current` (PHY type 10 only) | pi | |
| 0xf8 | `wlc_phy_btc_adjust_acphy` | `wlc_phy_watchdog`, when Bluetooth coexistence is on and the Bluetooth period changed | pi, new period (8 bit) | adjust to Bluetooth activity |
| 0x100 | `sub_09737d` | `wlc_phy_watchdog`, every call | pi | PHY specific part of the watchdog |
| 0x108 | 0 | `wlc_phy_tssi_cal` | pi, two pointers | |
| 0x110 | 0 | `wlc_phy_switch_radio` | pi, on | the AC-PHY has its own case there |
| 0x118 | 0 | `wlc_phy_anacore` | pi, on | the AC-PHY writes `D11(0x3e6)` directly |
| 0x130 | 0 | `wlc_phy_cal_mode` | pi, mode | |

The functions with a zero slot return without doing anything for the AC-PHY
(`wlc_phy_txpower_core_offset_get`/`_set` return -23 when their slot is zero).

Short description of the four small AC-PHY functions:

* `sub_097e2b(pi)`: `osl_mfree(sh->osh, pi_ac, 0x920)`. `pi+0x138` is not cleared.
* `sub_092e67(pi, out)`: clear the 4 bytes at `out`; for core c < N: `out[c]` =
  `pi_ac+0x456+c`. Result 0.
* `sub_099528(pi, in)`: for i = 0..3: if `in[i]` is not 0 and i > N: return -2
  (the test lets i = N pass; what was stored for smaller i stays stored and
  no recalculation is made); if `pi_ac+0x456+i` differs from `in[i]` store
  it and remember "changed". If changed and `sh+0x31` (clock on):
  `wlapi_suspend_mac_and_wait`, `sub_09949f(pi)`, `wlapi_enable_mac`. Result 0.
* `sub_09737d(pi)`: if `pi_ac+0x33c` is set call
  `wlc_phy_noise_sample_request_crsmincal(pi)`; then
  `wlapi_suspend_mac_and_wait`, `mod(PHY(0x520), 0x000c, pi_ac+0x918 << 2)`,
  `pi_ac+0x918` = (`pi_ac+0x918` + 1) modulo N, `wlapi_enable_mac`.

## Procedures

### 1. wlc_phy_shared_attach (.text+0x0b7199, name original)

Purpose: create the state shared by all PHY objects of the card.
Input: address of the record described above. Result: address of `sh`, or 0
if the allocation failed. No hardware access.

Steps:

1. Allocate 0x100 bytes with `osl_malloc(record.osh, 0x100)`; on failure
   return 0. Fill with zero.
2. Copy the fields of the record as in the table "Argument of
   wlc_phy_shared_attach".
3. Set the eight bytes `sh+0x96 .. sh+0x9d` to 0.
4. `sh+0x74` = 15, `sh+0x78` = 60, `sh+0x7c` = 120, `sh+0x80` = 3, `sh+0xa8` = 0.

### 2. wlc_phy_attach (.text+0x0be426, name original), AC-PHY path

Purpose: create (or find) the PHY object for a band.
Inputs: `sh`; `regs` (D11 base); `bandtype` (2 = 2.4 GHz, 1 = 5 GHz); `vars`.
Result: `pi`, or 0 on failure.

Steps:

1. flags = 3 if `sh+0x28` (MAC core revision) is 4, else
   `si_core_sflags(sih, 0, 0)`: a read of `WRAP(d11, 0x500)`. Bits: 0 = PHY
   for 2.4 GHz, 1 = PHY for 5 GHz, 3 = one PHY for both bands. The emulated
   card answers 0x0c.
2. If `bandtype` is 1 and flags & 0x0a is 0: return 0 (no 5 GHz).
3. If bit 3 of flags is set and `sh+0x00` is not 0 (a PHY exists already):
   take that `pi`; `wlapi_bmac_corereset(sh+0x20, pi+0x170)`; `pi+0x188` += 1;
   return `pi`. (This is the second call on the BCM4360.)
4. Allocate 0x1170 bytes (`osl_malloc(sh+0x10, 0x1170)`), return 0 on failure;
   fill with zero. Then: `pi+0x148` = `regs`; `pi+0x20` = `sh`; `pi+0x185` = 1;
   `pi+0xf58` = `pi` + 0xfb8; `pi+0x228` = 1 if `sh+0x60` is 0x106b and `sh+0x58`
   is 0x93, else 24; `pi+0x184` = 100; `pi+0xf9d` = 4; `pi+0xf9f` = 4;
   `pi+0xf9e` = 0; `pi+0x158` = `vars`.
5. `sub_0be37f(pi)` (procedure 4). Here it leaves `pi+0xf9c` = the variable
   `phycal_tempdelta` if that is <= 64, else 0.
6. If `bandtype` is 2 and bit 0 of flags is set: `pi+0x170` = 0x2000.
7. `wlapi_bmac_corereset(sh+0x20, pi+0x170)`: reset of the 802.11 core by
   the MAC layer (in the trace: core reset through the wrapper registers,
   `D11(0x120)` = 0x04000400, clock request in `D11(0x1e0)`).
8. v = 16 bit read of `D11(0x3e0)`. `pi+0x10fc` = `si_fabid(sih)`, which is 0
   for chip 0x4360 without any access. `pi+0x160` = (v >> 8) & 0xf; `pi+0x164` =
   v & 0xf; `pi+0x174` = v >> 12. The emulated card answers 0x0b01.
   (Chips 0xa8e2..0xa8e4 and 0xa8e6 force the revision 9; PHY type 9 is
   turned into type 4: other chips.)
9. `pi+0x168` = 1 (3 for PHY type 7, 2 for type 4).
10. The PHY type must be one of 0, 1, 2, 4, 5, 6, 7, 8, 10, 11 and fit the
    band; type 11 is accepted for both bands. Otherwise: failure exit.
11. `pi+0xc04` = 60; `pi+0xc08` = 16; `pi+0xc0c` = 0; `pi+0x182` = 0x1000;
    `pi+0x17e` = 0x1001 if `bandtype` is 2, else 0xd024; `pi+0x240` = low byte of
    that value.
12. Interference modes (PHY type 11): `sh+0x94` = 0; `sh+0x84` = `sh+0x88` = 0;
    if the PHY revision is 0, 1, 2, 3, 5 or 6 both become 1; if the PHY
    revision is 1, bits 1 and 2 are set in both (result 7). `sh+0x80` is not
    written here. (For the AC-PHY the mode is a set of bits; bits 1 and 2
    are the ones the PHY initialisation passes to
    `wlc_phy_hwaci_setup_acphy` and `wlc_phy_aci_w2nb_setup_acphy`, see
    `acphy-radio.md` sections 15, 17 and 20.)
13. If a variable `interference` exists: `sh+0x84` = `sh+0x88` = its value;
    `sh+0x80` = `sh+0x84` if `bandtype` is 2, else `sh+0x88`.
14. `pi+0xf68` = 10; `pi+0xf69` = 3; `pi+0x1e7`, `pi+0x1ec`, `pi+0x1f1`, `pi+0x1f6`,
    `pi+0x1fb` = 0; `pi+0x18c` = 1; `pi+0xf82` = 0; `pi+0xc37` = 5 if the PHY
    revision is 3, else 1; `sh+0xa7` = 3; `pi+0x1084` = 0xffce; `pi+0x1080` = 0;
    `pi+0x198` = 0; `pi+0x1c8` = 0; `pi+0x1d6` = 0x7f; `pi+0x236` = 0xffff;
    `pi+0x140` = 0.
15. `pi+0x1088` = `wlapi_init_timer(sh+0x20, sub_0b56ce, pi, "phycal")`; if
    the result is 0: failure exit.
16. `wlc_phy_attach_acphy(pi)` (procedure 6); if it returns 0: failure exit.
17. Temperature limits. t = low byte of the variable `tempthresh` (0 if
    absent). If t is 0 or 255: t = 150. **If the chip is 0x4360 and the board
    type 0x117 or 0x111: t = 120, whatever the variable says.** `pi+0xc2e` =
    `pi+0xc2f` = t. h = low byte of `temps_hysteresis`; if h is 0 or 15: h = 5.
    `pi+0xc30` = h; `pi+0xc31` = t - h; `pi+0xc32` = `pi+0xc34` = `pi+0xc35` = 0;
    `pi+0xc33` = m | (m << 4) with m = (1 << N) - 1, N = `pi+0x168` as set by
    step 16. (`pi+0xc35` was set by `wlc_phy_txpwr_srom11_read` before and is
    cleared here.)
18. `sub_0b7337(pi)` (procedure 5).
19. `wlc_phy_anacore(pi, 1)`; read the radio id; `pi+0x226` = 0;
    `wlc_phy_switch_radio(pi, 0)`; if the id is neither 0x2069 nor 0x030b:
    failure exit (`acphy-radio.md`, sections 1 to 4).
20. More variables (`phy_getintvar_default` and
    `phy_getintvararray_default`, the low byte of the result is stored):

    | Variable | Default | Stored in |
    |---|---|---|
    | `txpwrbckof` | 6 | `pi+0x10a0` |
    | `tssilimucod` | 1 | `pi+0x10b8` |
    | `rssicorrnorm` | 0 | `pi+0x10fe` |
    | `rssicorratten` | 7 | `pi+0x10ff` |
    | `rssicorrnorm5g[i]`, i = 0..2 | 0 | `pi+0x1100+i` |
    | `rssicorratten5g[i]`, i = 0..2 | 0 | `pi+0x1103+i` |
    | `rssicorrperrg2g[i]`, i = 0, 1 | -150 | `pi+0x1106+i` |
    | `rssicorrperrg5g[i]`, i = 0, 1 | -150 | `pi+0x110b+i` |
    | `rssicorrperrg2g[i]`, i = 2..4 | 0 | `pi+0x1106+i` |
    | `rssicorrperrg5g[i]`, i = 2..4 | 0 | `pi+0x110b+i` |
    | `5g_cga[i]`, i = 0..23 | 0 | `pi+0x1110+i` |
    | `2g_cga[i]`, i = 0..13 | 0 | `pi+0x1128+i` |

21. `pi+0x188` += 1; the eight 32 bit words `pi+0x1144 .. pi+0x1160` =
    0xffffffff; `pi+0x150` = `sh+0x00`; `sh+0x00` = `pi`; `pi+0x158` = address of
    `pi+0x158`; copy the 0x1c bytes at `pi+0x160` to `pi+0x00`. Return `pi`.

Failure exit: if `pi+0x1c8` is not 0 `ppr_delete(sh+0x10, pi+0x1c8)`;
`osl_mfree(sh+0x10, pi, 0x1170)`; return 0. (The timer and `pi_ac` are not
released on this path.) `wlc_bmac_attach` answers a result of 0 with its
error 17.

Notes:

* Where the board type comes from (`si_doattach`, .text+0x022b10, not part of
  this specification, read only for this note): on a PCI card the variable
  `boardtype` of the SROM, or, if that is 0 or absent, the PCI subsystem id
  (upper half of `PCICFG(0x2c)`). For board vendor 0x106b (Apple) the PCI
  subsystem id replaces the SROM value when the SROM says 0x111, 0x112,
  0x129, 0x134 or 0x135. So the case "board type 0x111" of step 17 needs a
  card whose SROM *and* PCI subsystem id are 0x111; a card with 0x111 in the
  SROM and 0x117 as PCI subsystem id is a board 0x117 for the whole driver.
* Write pacing (open question of `access.md`): the limit `pi+0x228` is set
  only here, in step 4: 24 for every board except Apple's board type 0x93
  (limit 1: a dummy read before every write). It is 24 on the BCM4360 boards.
* With core status flags that have bit 3 clear and bits 0 and 1 set (one PHY
  per band, not the BCM4360) both calls run all steps: two `pi` with their
  own `pi_ac`, the first with `pi+0x170` = 0x2000, the second with chanspec
  0xd024 and `pi+0x150` pointing to the first.

### 3. Variable access helpers

`phy_getvar(pi, name)` (.text+0x0bcd81): result 0 if `name` is 0 or empty.
Walk the list at `pi+0x158`: an entry matches if it starts with `name` and the
next character is `=`; result = address of the character after the `=`. Result
0 at the end of the list. Only the list of the PHY is searched.

`sub_0bcded(pi, name)` (.text+0x0bcded; `phy_getvar_fabid` is a jump to it):
the look-up that knows the fab id.

1. If `pi+0x10fc` (fab id) is not 0: allocate a buffer of `strlen(name)` + 5 +
   16 bytes (on failure return 0), print `"<name>.fab.<fab id>"` into it (fab
   id in decimal), look that name up with `phy_getvar`, free the buffer; if
   found return the value.
2. Return `phy_getvar(pi, name)`.

So the fab id variant of a variable is the name followed by `.fab.` and the
decimal fab id, and it takes precedence over the plain name. On the BCM4360
the fab id is 0 and only the plain name is looked up.

`phy_getintvar_default(pi, name, default)` (.text+0x0bd008): v =
`sub_0bcded(pi, name)`; result `default` if not found, else
`bcm_strtoul(v, 0, 0)` (base by prefix; 32 bit result).
`phy_getintvar(pi, name)` (.text+0x0bd032) = the same with default 0.

`phy_getintvararray_default(pi, name, index, default)` (.text+0x0bceba):

1. If `sub_0bcded(pi, name)` finds nothing: result `default`.
2. If the fab id is not 0: build `"<name>.fab.<fab id>"` as above (if the
   allocation fails the result is 0); if `sub_0bcded` finds that name: result
   = element `index` of it if it has more than `index` elements, else
   `default`. If it does not exist go on with step 3.
3. n = `getintvararraysize(vars, name)`; if n > `index` the result is
   `getintvararray(vars, name, index)`, else `default`.

`phy_getintvararray(pi, name, index)` (.text+0x0bcffb) = the same with
default 0.

bcmutils.c:

* `getvar(vars, name)` (.text+0x00b807): 0 if `name` is 0 or empty; search
  the list `vars` as `phy_getvar` does; if `vars` is 0 or the name is not in
  it, the result is `nvram_get(name)`, a search in the global variable
  lists of the driver (.bss+0x300). In the emulator these lists are empty
  after attach (`nvram_get` finds none of the SROM variables), so on this
  card the fallback finds nothing.
* `getintvar(vars, name)` (.text+0x00ba1a): `bcm_strtoul(value, 0, 0)`, or 0 if
  there is no such variable.
* `getintvararraysize(vars, name)` (.text+0x00b978): number of elements: the
  value is parsed number by number, one comma is skipped after each number,
  until the end of the string. 0 if the variable does not exist or is empty.
* `getintvararray(vars, name, index)` (.text+0x00b9c3): the element `index`
  (counted from 0) parsed the same way; 0 if there is no such variable or it
  has fewer elements.

Notes: a value is converted with 32 bit and then cut to the size of the field
it is stored in. `wlc_phy_attach_acphy` reads `rxgainerr5ga*` and
`noiselvl5ga*` with `getintvararray(pi+0x158, ...)` directly, all others
through the `phy_` functions.

### 4. sub_0be37f (.text+0x0be37f, name assigned: wlc_phy_read_tempdelta_settings)

Input: `pi`. `pi+0xf9c` = low byte of the variable `phycal_tempdelta` (0 if
absent). If `pi+0xf9c` > 64: `pi+0xf9c` = `pi+0xf9e`; else `pi+0xf9e` = `pi+0xf9c`.
(For chips 0xa8d8 and 0xa8dc on boards of vendor 0x1028 more is done: other
chips.) Called twice during attach: by `wlc_phy_attach` while `pi+0xf9e` is 0
and by `wlc_phy_txpwr_srom11_read` after it has set `pi+0xf9e` = 40. Result
after attach: if the variable is <= 64 (or absent, which counts as 0) both
fields hold the variable; if it is above 64 (255 in a blank SROM) both
fields hold 40.

### 5. sub_0b7337 (.text+0x0b7337, name assigned: wlc_set_phy_uninitted)

Input: `pi`. `pi+0x187` = 0; `pi+0xc36` = 0xff; `pi+0xed2` = 0xff; for PHY type
11: the four bytes `pi_ac+0x10 .. 0x13` = 0x40 (0x3c for PHY revisions 2, 5,
6). Also called by `wlc_phy_ioctl`.

### 6. wlc_phy_attach_acphy (.text+0x0a194f, name original)

Purpose: create the AC-PHY state, read the board description.
Input: `pi` with `sh`, `regs`, `vars`, PHY type and revision and chanspec set.
Result: 1, or 0 on failure (allocation failed, or
`wlc_phy_txpwr_srom11_read` failed).

Steps (the order of the hardware accesses is the order given here):

1. Set bit 0 of the global variable `phyhal_msg_level` (.data+0xda160).
2. `pi+0x138` = `pi_ac` = `osl_malloc(sh+0x10, 0x920)`; return 0 if that failed;
   fill with zero.
3. Defaults: `pi_ac+0x32c` = 0; `pi_ac+0x330` = 1 if (`pi+0x17e` & 0xc000) is 0,
   else 0; `pi_ac+0x338` = 0; `pi_ac+0x000` = 1; `pi_ac+0x339` = `pi_ac+0x33a` =
   0x80; `pi_ac+0x33b` = 0x0c; `pi_ac+0x334` = `pi+0x17e` & 0x3800; `pi_ac+0x44a` =
   0; `pi+0xf89` = 2; `pi+0xf8a` = 5; `pi_ac+0x8e1` = 0; `pi+0xc2b` = 0xff;
   `pi_ac+0x33c` = 1; `pi_ac+0x33d` = `pi_ac+0x33e` = 0; `pi_ac+0x340` =
   `pi_ac+0x341` = 0; `pi_ac+0x8de` = `pi_ac+0x8df` = `pi_ac+0x8e0` = 1;
   `pi_ac+0x8e4` = 1; `pi_ac+0x8e6` = `pi_ac+0x8e7` = 1; `pi_ac+0x8e8` = 0;
   `pi_ac+0x42` = 0x36.
4. Read ten PHY registers and save them: 0x739 -> `pi_ac+0x8ee`, 0x73a ->
   `+0x8f0`, 0x725 -> `+0x8f2`, 0x729 -> `+0x8ea`, 0x721 -> `+0x8ec`, 0x728 ->
   `+0x8f4`, 0x720 -> `+0x8f6`, 0x408 -> `+0x8f8`, 0x417 -> `+0x8fa`, 0x416 ->
   `+0x8fc` (in this order).
5. The global variable `acphychipid` (.bss+0x3080) = `sh+0x3c` (chip id). The
   AC-PHY code reads the chip id from there when it chooses register
   addresses.
6. `pi_ac+0x8ff` = 1; `pi_ac+0x918` = 0; `pi_ac+0x906` = 0; `pi_ac+0x908` = 5;
   `pi_ac+0x90e` = 0xf3; `pi_ac+0x90f` = 0xf1; `pi_ac+0x90a` = `pi_ac+0x90c` = 31;
   `pi_ac+0x912` = 1 if the PHY revision is 0 or 1, else 0.
7. `wlc_phy_hirssi_elnabypass_init_acphy(pi)` (procedure 9; no hardware
   access at this time).
8. `pi+0x22a` = 0; `pi_ac+0x900` = 0; `pi_ac+0x901` = 1; `pi_ac+0x902` = 0x404e;
   `pi_ac+0x904` = 0x0fff.
9. `pi+0xc24` = `si_alp_clock(sih)` (`pmu.md`: 40000000 if bit 0 of the chip
   status is set, else 20000000; no register access, but the PCI windows are
   moved to ChipCommon and back).
10. `pi_ac+0x45a .. 0x45d` = 0x80; `pi_ac+0x64a .. 0x64f` = 2, 6, 7, 10, 8, 8.
11. Variables. "exists" means `phy_getvar_fabid` finds the name; the value is
    read with `phy_getintvar` and cut to the size of the field:

    | Variable | Size | If absent | Stored in |
    |---|---|---|---|
    | `subband5gver` | 8 bit, zero extended to 32 | 4 | `sh+0x4c` |
    | `extpagain2g` | 8 bit, zero extended to 32 | 0 | `sh+0xb0` |
    | `extpagain5g` | 8 bit, zero extended to 32 | 0 | `sh+0xac` |
    | `femctrl` | 8 bit | 0 | `pi_ac+0x342` |
    | `boardflags3` | 32 bit | 0 | split into flags, see below |
    | `rpcal2g` | 16 bit | 0 | `sh+0xcc` |
    | `rpcal5gb0` .. `rpcal5gb3` | 16 bit | 0 | `sh+0xce`, `sh+0xd0`, `sh+0xd2`, `sh+0xd4` |
    | `txidxcap2g` | 8 bit | 0 | `sh+0xd6` |
    | `txidxcap5g` | 8 bit | 0 | `sh+0xd7` |

    boardflags3: bits 0..2 -> `pi_ac+0x343`; bit 3 -> `+0x345`; bits 4..6 ->
    `+0x349`; bit 7 -> `+0x348`; bit 8 -> `+0x34c`; bit 9 -> `+0x346`; bit 10 ->
    `+0x347`; bit 11 -> `+0x34d`; bit 12 -> `+0x34f`; bit 13 -> `+0x34e`; bit 14
    -> `+0x354`; bit 15 -> `+0x355` (each field one byte holding the bit or
    bit field shifted down to bit 0).
12. From the boardflags in `sh`: `pi_ac+0x344` = bit 1 of boardflags2
    (`sh+0x68`); `pi_ac+0x34b` = bit 0 of boardflags (`sh+0x64`); `pi_ac+0x34a` =
    bit 29 of boardflags.
13. More variables, as in step 11:

    | Variable | Size | If absent | Stored in |
    |---|---|---|---|
    | `pdgain2g` | 8 bit | 0 | `pi_ac+0x410` |
    | `pdgain5g` | 8 bit | 0 | `pi_ac+0x411` |
    | `cckdigfilttype` | 8 bit | 1 | `pi_ac+0x8fe` |

14. Number of cores: read `PHY(0x0b)`; `pi+0x168` = value & 7. Then: if the
    chip is 0x4360 and the board type is 0x137 or 0x117, or if the chip is
    0x4352: `pi+0x168` = 2. (The read is made in every case.)
15. `pi+0xc40` = 1; `pi+0xfa0` = 1; `pi+0x224` = 1; clear the 16 bytes at
    `pi_ac+0x2c` and the 4 bytes at `pi_ac+0x3d`; `pi_ac+0x43` = `pi_ac+0x44` =
    `pi_ac+0x3c` = 0.
16. PHY revision 0 or 1: for b = 0..4 the three bytes `pi_ac+0x18+4b`,
    `+0x19+4b`, `+0x1a+4b` = 0xe2. (PHY revisions 2, 3, 5, 6: other values;
    revisions 4 and above 6: nothing.)
17. `pi_ac+0x340` = bit 12 of boardflags; `pi_ac+0x341` = bit 28 of boardflags.
18. Receive gains, for each core c = 0 .. N-1 (N from step 14). The variable
    names end in the core number (`rxgains2gelnagaina0`, ...). E(b) stands
    for the three bytes at `pi_ac + 0x3e0 + 12 * b + 3 * c`, b = 0 (2.4 GHz), 1
    (5 GHz low), 2 (5 GHz mid), 3 (5 GHz high).
    1. Clear E(0) .. E(3).
    2. 2.4 GHz: if `pi_ac+0x340` is set: if `rxgains2gelnagaina<c>` exists
       E(0)[0] = 2 * value + 6; if `rxgains2gtrelnabypa<c>` exists E(0)[2] =
       value. In every case: if `rxgains2gtrisoa<c>` exists E(0)[1] = 2 *
       value + 8.
    3. 5 GHz low: the same with `pi_ac+0x341`, `rxgains5gelnagaina<c>`,
       `rxgains5gtrelnabypa<c>`, `rxgains5gtrisoa<c>` and E(1).
    4. 5 GHz mid: g = `rxgains5gmelnagaina<c>` and y = `rxgains5gmtrelnabypa<c>`
       if `pi_ac+0x341` is set and the variable exists, else 0; t =
       `rxgains5gmtrisoa<c>` if it exists, else 0 (all three as 8 bit values).
       If g, y and t are all 0, or if g = 7, y = 1 and t = 15 (the fields of a
       blank SROM): E(2) = E(1) (all three bytes). Otherwise E(2)[0] = 2 * g
       + 6, E(2)[1] = 2 * t + 8, E(2)[2] = y.
    5. 5 GHz high: the same with `rxgains5ghelnagaina<c>`,
       `rxgains5ghtrelnabypa<c>`, `rxgains5ghtrisoa<c>`; the fallback is E(3) =
       E(2).

    All arithmetic is 8 bit.
19. `wlc_phy_txpwr_srom11_read(pi)` (procedure 7); return 0 if it returns 0.
20. Temperature reference: r = `phy_getintvar("rawtempsense")`; t = the
    lowest 9 bits of r taken as a signed number (-256 .. 255). `pi+0x210` =
    `pi_ac+0x8dc` = 255 if t is -1, else t (16 bit).
21. Receive gain errors for 2.4 GHz: a0 = the lowest 6 bits of
    `rxgainerr2ga0` as a signed number (-32 .. 31), a1 and a2 = the lowest 5
    bits of `rxgainerr2ga1` and `rxgainerr2ga2` as signed numbers (-16 .. 15)
    (`phy_getintvar`, 0 if absent). If a0, a1 and a2 are all -1 and t of step
    20 is -1 too: `pi+0x1e7` = 1 and a0 = a1 = a2 = 0; else `pi+0x1e7` = 0. Then
    `pi+0x1e3` = a0; `pi+0x1e4` = a0 + a1; `pi+0x1e5` = a0 + a2 (8 bit).
22. The same for the four 5 GHz sub-bands k = 0..3 with element k of
    `rxgainerr5ga0`, `rxgainerr5ga1`, `rxgainerr5ga2` (`getintvararray`, 0 if
    absent): flag `pi+0x1ec+5k`, values `pi+0x1e8+5k`, `pi+0x1e9+5k`, `pi+0x1ea+5k`.
23. Noise levels for 2.4 GHz: for c = 0 .. N-1: `pi+0x1fc+c` = 0xba (-70).
    Then, for all three of them whatever N is: `pi+0x1fc` -= `noiselvl2ga0`;
    `pi+0x1fd` -= `noiselvl2ga1`; `pi+0x1fe` -= `noiselvl2ga2` (8 bit; the
    third one starts from 0 on a board with two cores).
24. The same for the 5 GHz sub-bands k = 0..3 with `pi+0x200+4k+c` and
    element k of `noiselvl5ga0`, `noiselvl5ga1`, `noiselvl5ga2`.
25. If `pi_ac+0x34c` (boardflags3 bit 8) is set: for each of the four
    variables `swctrlmap_2g`, `swctrlmapext_2g`, `swctrlmap_5g`,
    `swctrlmapext_5g` that exists: its elements 0..4 (32 bit) go to
    `pi_ac+0x358`, `pi_ac+0x36c`, `pi_ac+0x380`, `pi_ac+0x394` (+ 4 * index).
26. `sub_0a1604(pi)` (procedure 8).
27. OTP word 16: s = `si_get_sromctl(sih)` (read of `CC(0x190)`); if bit 4 of s
    is 0: `si_set_sromctl(sih, s | 0x10)`. w = 0; `otp_read_word(sih, 16, &w)`
    (w stays 0 if the read fails; the result of the call is not examined).
    If bit 4 of s was 0: `si_set_sromctl(sih, s)`. `pi_ac+0x8e2` = (w >> 8) &
    0x1f (16 bit field).
28. `pi_ac+0x8e5` = 1 if the PHY revision is 2, 5 or 6, else 0.
29. Function pointers: see the table in "Data". `sh+0x80` = 0.
30. Clear 9 bytes each at `pi_ac+0x656`, `pi_ac+0x65f`, `pi_ac+0x668`,
    `pi_ac+0x8b4`; `pi_ac+0x671` = 1; clear 0xf0 bytes each at `pi_ac+0x6c8` and
    `pi_ac+0x7b8`; `pi_ac+0x8a8` (64 bit) = 0; `pi_ac+0x8b0` (32 bit) = 0.
31. `wlc_phy_hwaci_init_acphy(pi)` (procedure 11). `pi+0x1165` = 0.
32. Only if `pi_ac+0x34e` is 1 (boardflags3 bit 13): read OTP word 16 a second
    time, now directly into `sh+0xf2` and without touching `CC(0x190)`:
    `otp_read_word(sih, 16, sh+0xf2)`. If the result is 0 (success): `sh+0xf2`
    &= 0x000f. If it failed: `sh+0xf2` = 9 if `pi+0x16e` is 2, 10 if it is 1.
    (`pi+0x16e`, the radio major revision, is still 0 at this time, the radio
    id is read after this function. So after a failure the field keeps what
    `otp_read_word` left in it.)
33. Return 1.

Use of the two parts of OTP word 16: bits 8..12 (`pi_ac+0x8e2`) become a field
of `PMU_REGCTL[0]` at PHY initialisation; bits 0..3 (`sh+0xf2`) are a
resistor calibration value that radios of major revision 1 and 2 use when
boardflags3 bit 13 tells them to skip the measurement (`acphy-radio.md`,
sections 4 and 9). Bits 4..7 and 13..15 are not used.

### 7. wlc_phy_txpwr_srom11_read (.text+0x0bf3e8, name original)

Purpose: read the tx power description of SROM revision 11.
Input: `pi`. Result: 1; 0 if the PHY type is not 11. No hardware access.

Steps:

1. For band = 0..4 (0 = 2.4 GHz, k = band - 1 the 5 GHz sub-band), for core c
   = 0, 1, 2 (three cores, whatever N is), everything read with
   `phy_getintvar`/`phy_getintvararray` (0 if absent or too short):

   | Field | 2.4 GHz | 5 GHz sub-band k |
   |---|---|---|
   | `pi + 0xe7c + 5c + band` (8 bit) | `maxp2ga<c>` | `maxp5ga<c>[k]` |
   | `pi + 0xe04 + 10c + 2 band` (16 bit) | `pa2ga<c>[0]` | `pa5ga<c>[3k]` |
   | `pi + 0xe2c + 10c + 2 band` (16 bit) | `pa2ga<c>[1]` | `pa5ga<c>[3k + 1]` |
   | `pi + 0xe54 + 10c + 2 band` (16 bit) | `pa2ga<c>[2]` | `pa5ga<c>[3k + 2]` |
   | `pi + 0xea6 + 2 band` (16 bit) | `tssifloor2g[0]` | `tssifloor5g[k]` |

   (PHY revision 3 with `pi_ac+0x348` set also reads `pa5gbw4080a1` into the
   places of a fourth core: not the 4360.)
2. If `sh+0x48` (SROM revision) > 10: the tx power offsets of the table "Tx
   power offsets" in "Data": each named variable is read with
   `phy_getintvar` and stored with the size given there; the copies are
   made from the fields named there. When `pi+0xc50` is computed `pi+0xd16` is
   still 0.
3. `pi+0xe90`, `pi+0xe92`, `pi+0xe94` = `pdoffset40ma0`, `..1`, `..2`; `pi+0xe98`,
   `pi+0xe9a`, `pi+0xe9c` = `pdoffset80ma0`, `..1`, `..2` (16 bit);
   `pi+0xea0`, `pi+0xea1`, `pi+0xea2` = `pdoffset2g40ma0`, `..1`, `..2`;
   `pi+0xea4` = `pdoffset2g40mvalid`; `pi+0xece`, `pi+0xecf`, `pi+0xed0` =
   `pdoffsetcckma0`, `..1`, `..2` (8 bit).
4. o = low byte of `tempoffset` taken as a signed 8 bit number. `pi+0xc35` =
   0 if o is -1 or 0; else 16 if o > 48; else -16 (0xf0) if o < 16; else o -
   32. (`wlc_phy_attach` clears the field again afterwards, step 17 there:
   the value is lost. See "Open questions".)
5. `pi+0xf9e` = 40; `sub_0be37f(pi)` (procedure 4).
6. Return 1.

### 8. sub_0a1604 (.text+0x0a1604, name assigned: wlc_phy_srom_read_rssicorrnorm_acphy)

Input: `pi`. For core c = 0, 1, 2:

* if the variable `rssicorrnorm_c<c>` exists: `pi_ac+0x3a8+2c+i` = element i
  of it, i = 0, 1; else both bytes = 0;
* if the variable `rssicorrnorm5g_c<c>` exists: `pi_ac+0x3b0+12c+i` = element
  i of it, i = 0..11 (read sub-band by sub-band: 4 groups of 3); else the 12
  bytes = 0.

Elements are read with `phy_getintvararray` (0 if the array is shorter) and
cut to 8 bit. No hardware access.

### 9. wlc_phy_hirssi_elnabypass_init_acphy (.text+0x092555, name original)

Purpose: reset the state of the "high RSSI: bypass the external LNA"
function. Input: `pi`. Callers: `wlc_phy_attach_acphy`, `sub_0b018f` (PHY
initialisation).

1. `pi_ac+0x914` = 0xffff; `pi_ac+0x916` = 0xffff.
2. If `pi_ac+0x912` is 0 (PHY revision > 1): `pi_ac+0x910` = `pi_ac+0x911` = 0;
   return.
3. `pi_ac+0x910` = `pi_ac+0x911` = `pi_ac+0x906`.
4. If `sh+0x31` (clock on) is not 0:
   `wlc_phy_hirssi_elnabypass_set_ucode_params_acphy(pi)` (procedure 10),
   then `SHM(0x184)` = 0. At attach `sh+0x31` is 0: no access.

### 10. wlc_phy_hirssi_elnabypass_set_ucode_params_acphy (.text+0x0923f2, name original)

Input: `pi`. Nothing is done if `pi_ac+0x912` is 0. Otherwise three words of
the shared memory are written, in this order: `SHM(0x032)` = a, `SHM(0x180)` =
b, `SHM(0x182)` = n.

* state = `pi_ac+0x910` and timer = `pi_ac+0x914` on 2.4 GHz (`pi+0x17e` &
  0xc000 = 0), `pi_ac+0x911` and `pi_ac+0x916` on 5 GHz.
* If state is 0: a = 0x0032, b = 0x0527, n = 0x01f4.
* Else if the timer is not negative (bit 15 clear): a = `pi_ac+0x90f` sign
  extended to 16 bit, b = 0x0529, count = `pi_ac+0x90c`; if it is negative: a
  = `pi_ac+0x90e` sign extended, b = 0x0527, count = `pi_ac+0x90a`. n = count *
  f (16 bit), f = 1 for 20 MHz (`pi+0x17e` & 0x3800 = 0x1000), 2 for 40 MHz
  (0x1800), 4 otherwise.

Callers: procedure 9, the channel function `sub_0a7089`,
`wlc_phy_hirssi_elnabypass_engine`.

### 11. wlc_phy_hwaci_init_acphy (.text+0x0927c8, name original)

Purpose: defaults of the interference mitigation (the consumers are
`wlc_phy_hwaci_setup_acphy` and the functions around it; the meaning of the
values belongs to that specification). Input: `pi`. No hardware access.

1. `pi_ac+0x672` = 300, `pi_ac+0x674` = 1000, `pi_ac+0x676` = 500, `pi_ac+0x678` =
   1 (16 bit each); `pi_ac+0x67a` = 15, `pi_ac+0x67b` = 15, `pi_ac+0x67c` = 1,
   `pi_ac+0x67d` = `pi_ac+0x67e` = `pi_ac+0x67f` = 0, `pi_ac+0x680` = 4 (8 bit
   each); `pi_ac+0x6c2` = 4, `pi_ac+0x6c3` = 4 (number of entries of the two
   tables that follow).
2. Two tables of four entries of 8 bytes, the first at `pi_ac+0x682`, the
   second at `pi_ac+0x6a2`; both areas (32 bytes) are cleared first. Entry
   layout: 16 bit word at +0, bytes at +2, +3, +4, +5, +6; +7 unused.

   | Table | Entry | +0 (16 bit) | +2 | +3 | +4 | +5 | +6 |
   |---|---|---|---|---|---|---|---|
   | 1 (0x682) | 0 | 0xffff | 5 | 6 | 0 | 30 | 4 |
   | | 1 | 4000 | 5 | 4 | 0 | 30 | 4 |
   | | 2 | 8000 | 4 | 4 | 1 | 22 | 4 |
   | | 3 | 11000 | 3 | 4 | 2 | 10 | 4 |
   | 2 (0x6a2) | 0 | 0xffff | 5 | 6 | 0 | 30 | 4 |
   | | 1 | 1000 | 5 | 4 | 0 | 30 | 4 |
   | | 2 | 6000 | 4 | 4 | 1 | 25 | 4 |
   | | 3 | 10000 | 3 | 4 | 2 | 15 | 4 |

### 12. Queries of the MAC layer

* `wlc_phy_get_phyversion(pi, &phytype, &phyrev, &radioid, &radiorev)`
  (.text+0x0b18cc): stores, as 16 bit each, `pi+0x160`, `pi+0x164`, `pi+0x16a`
  and the byte `pi+0x16c`; result 1. `wlc_bmac_attach` puts them into the band
  state at +0x1c, +0x1e, +0x20, +0x22.
* `wlc_phy_get_encore(pi)` (.text+0x0b18fb): the byte `pi+0x178` (0).
* `wlc_phy_get_coreflags(pi)` (.text+0x0b1907): `pi+0x170` (32 bit; 0).
* `wlc_phy_machwcap_set(pi, caps)` (.text+0x0b1f6f): `sh+0x2c` = caps (the
  value `wlc_bmac_attach` read from `D11(0x15c)`).
* `wlc_phy_stf_chain_init(pi, txchain, rxchain)` (.text+0x0b24f7): `sh+0xa4` =
  `sh+0xa6` = txchain; `sh+0xa5` = `sh+0xa7` = rxchain (8 bit masks).
* `wlc_phy_cap_get(pi)` (.text+0x0b799d): capability bits by PHY type; type
  11: `wlc_phy_ac_caps(pi)`; types 5 and 9 and unknown types: 0.
* `wlc_phy_ac_caps(pi)` (.text+0x08e74c): 0x3f if the PHY revision is 1, 3
  or above 5; else 0x1f. So revision 0: 0x1f, revision 1: 0x3f. Meaning of
  the bits, from the callers: bit 5 (the one that depends on the revision)
  is printed as "vht-prop-rates" and bit 1 as "stbc-tx"/"stbc-rx-1ss" by the
  capability iovar in `wlc_doiovar`; bit 2 enables the short guard interval
  settings and bit 4 the LDPC setting in `wlc_attach`; bit 0 and bit 3 are
  stored by `wlc_attach` in `pub+0x6d` and `pub+0x6e` and tested by
  `wlc_set_ratespec_override` (by their use: 40 MHz and 80 MHz capable;
  unverified).

None of them accesses the hardware.

### 13. wlc_phy_detach (.text+0x0baed7, name original)

Input: `pi` (may be 0: nothing happens). No hardware access.

1. `pi+0x188` -= 1; if it is not 0 now: return. (On the BCM4360
   `wlc_bmac_detach` calls the function once per band with the same `pi`;
   the second call releases.)
2. If `pi+0x1088` is not 0: `wlapi_free_timer(sh+0x20, pi+0x1088)`; `pi+0x1088`
   = 0.
3. Take `pi` out of the list: if `sh+0x00` is `pi`: `sh+0x00` = `pi+0x150`; else
   if the `next` field of the first element is `pi`: that field = 0.
4. If slot 0xc0 of the function table is not 0 call it with `pi`: for the
   AC-PHY `sub_097e2b`, which frees `pi_ac` (0x920 bytes).
5. If `pi+0x1c8` is not 0: `ppr_delete(sh+0x10, pi+0x1c8)`.
6. `osl_mfree(sh+0x10, pi, 0x1170)`.

`sh` itself is freed by `wlc_phy_shared_detach` (not examined).

## Verification

Two kinds of checks were made in the emulator, both with the harness
`re-out\analysis\acphy-attach\attrun.py` (on top of the unchanged tools):

* order and values of the hardware accesses: the shared trace
  `re-out\up-trace.txt` and the traces of my own runs (`<name>-trace.txt`);
* the state: after `wlc_attach` the memory of `pi`, `pi_ac` and `sh` is
  written to files and compared between runs with `memdiff.py`; runs differ
  in the SROM contents (`--set`), in variables put into the list that
  `wlc_phy_attach` receives (`--var`, `--del`), in revisions, OTP contents,
  PCI subsystem id and core status flags. Some functions were called
  directly.

| Item | Checked how | Result |
|---|---|---|
| Order of the hardware accesses of attach | up-trace seq 499..842 and 888..924 | as in "Hardware accesses of attach" |
| Every field of the three tables in "Data" that has a value in the column "Emulator" | run `base`: dump of all bytes that are not zero | as listed; no byte is set that the tables do not explain, except the fields filled by the radio id read (`acphy-radio.md`) |
| Record of `wlc_phy_shared_attach` | `base`: `sh` against the known identification of the emulated card | as specified; the fields marked (unverified) in the guide (`sh+0x40`, `sh+0x44`, `sh+0x70`) are chip revision, chip package, bus core revision |
| PHY revision 0, radio revision 3 | run `r0` | only differences: revisions, `sh+0x84` = `sh+0x88` = 1 instead of 7 |
| Second call returns the same `pi` | `base`: both band states of `wlc_hw` hold the same `pi`, `pi+0x188` = 2 | as specified |
| Core status flags | runs `sf7` (flags 7), `sf5`, `sf4` | 7: two `pi`, first with `pi+0x170` = 0x2000, second with chanspec 0xd024, `pi_ac+0x330` = 0, `pi+0x150` = first; 5 and 4: second call fails, `wlc_attach` error 17 |
| `tempthresh`, `temps_hysteresis` | runs `v1` (100, 3; board 0x117), `v2b` (board 0x621: 100 is used), `v2c` (board 0x137, variable 255: 150), `v2d` (board 0x111: 120), `v5` (absent) | as specified |
| Number of cores | `v2b`, `v2d` (`PHY(0x0b)` = 3: N = 3), `v2c` (board 0x137: N = 2), `base` (`PHY(0x0b)` = 0: N = 2) | as specified; with N = 3 the third core gets its receive gains and the -70 of the noise levels |
| Board type 0x111 replaced by the PCI subsystem id | run `v2` (SROM 0x111, subsystem id 0x117: `sh+0x58` = 0x117), `v2d`, `v2e` | as in the note of procedure 2 |
| `phycal_tempdelta` | values 0, 30, 64, 65, 255, absent | 0, 30, 64, 40, 40, 0 in both fields |
| `tempoffset` | values 0, 10, 15, 16, 40, 48, 49, 60, 127, 128, 200, 255; read at the entry of `sub_0a1604` | 0, -16, -16, -16, 8, 16, 16, 16, 16, -16, -16, 0; after attach `pi+0xc35` is 0 in every run |
| `rawtempsense`, `rxgainerr*`, `noiselvl*` | `base` (blank values: flags 1, errors 0), `v1` (values chosen to test the sign extension and the sums), `v5` (absent) | as specified |
| Receive gains | `base`, `v3` (no external LNA, 5 GHz mid all zero, high changed), `v3b`, `v5`, `v2b` (blank third core: mid and high are copies) | as specified |
| Variables that are not in the SROM | run `v4`: `interference`, `txpwrbckof`, `tssilimucod`, `rssicorr*`, `5g_cga`, `2g_cga`, `cckdigfilttype`, `extpagain*`, `rssicorrnorm_c*`, `rssicorrnorm5g_c*`, `swctrlmap*` with boardflags3 bit 8, `pdoffsetcckma*`; arrays shorter and longer than the field | as specified |
| Defaults of absent variables | run `v5` (28 variables removed) | as specified |
| boardflags, boardflags2, boardflags3 bits | `base`, `v3`, `v3b` (bits 0, 12, 28, 29 of boardflags; bit 1 of boardflags2); `bf3`, `bf3b` (boardflags3 = 0xd6f5 and its complement 0x290a: all twelve fields) | as specified |
| Tx power tables of `wlc_phy_txpwr_srom11_read` | run `po`: a different value in each of the 40 offset and `pdoffset` variables (`ofdmlrbw202gpo` = 0x4321, `dot11agofdmhrbw202gpo` = 0x8765: `pi+0xc50` = 0x87652211); `base` for `pa2ga*`, `pa5ga*` (the synthetic SROM has a different value in every element); run `mx` for `maxp*`, `tssifloor*`, `pdoffset2g40m*` (different value per core and sub-band); `v5` (removed variables give 0 in the field and in its copies) | as specified, every field and every copy |
| OTP word 16 | runs `otp1` (word 0xf5ca: `pi_ac+0x8e2` = 0x15), `otp2` (with boardflags3 bit 13: `sh+0xf2` = 0x0a, second read in the trace, `CC(0x190)` untouched) | as specified |
| Radio revision unknown inside `wlc_phy_attach_acphy` | hook at the entry of `sub_0a1604`: `pi+0x16a` = 0, `pi+0x16e` = 0 | as specified |
| Variable helpers | direct calls with a variable list of my own and fab id 0, 3, 4 (`attrun.py --helpers`) | as specified: fab variant wins; a fab variant array that is too short gives the default, not the element of the plain array; prefix of a name does not match; empty value gives 0 |
| `wlc_phy_get_phyversion`, `_get_encore`, `_get_coreflags`, `wlc_phy_cap_get` | direct calls (`--caps`), PHY revisions 0..7 | 0x1f for revisions 0, 2, 4, 5; 0x3f for 1, 3, 6, 7 |
| `sub_092e67`, `sub_099528` | direct calls through `wlc_phy_txpower_core_offset_get`/`_set` | as specified, including the accepted index N and the result -2 |
| `wlc_phy_detach` | two direct calls (`--detach`) | first call frees nothing; second frees 0x1a90 bytes (0x1170 + 0x920), `sh+0x00` = 0; no hardware access |
| High RSSI functions | up-trace seq 26673..26688 (from the PHY initialisation); direct calls after `wlc_up` with 8 combinations of chanspec, state and timer (`--up --hirssi`) | as specified |
| `sub_09737d` | three direct calls after `wlc_up` | `PHY(0x520)` bits 2..3 = 0, 1, 0; index alternates |

Not checked in the emulator (code reading only): the paths of other PHY
revisions and chips mentioned in one line; `wlc_phy_hwaci_init_acphy` was
checked only through the memory dump of `base` (all values as in procedure
11); the failure exits of `wlc_phy_attach` apart from the one of the second
band.

## Open questions

* The purpose of many fields that attach only initialises is unknown (marked
  in the tables); their consumers belong to other specifications. The field
  names marked (bcm) are analogies.
* `tempoffset`: the value computed by `wlc_phy_txpwr_srom11_read` is
  overwritten with 0 by `wlc_phy_attach` right afterwards, so the variable
  has no effect in this version of the driver. Whether that is intended is
  unknown; an implementation that wants the same behaviour must end with 0.
* `PHY(0x0b)` (number of cores) reads 0 in the emulator; what the real card
  answers is unknown. It does not matter on board 0x117 (N is forced to 2).
* The core status flags of the real card (`WRAP(d11, 0x500)`; the model
  answers 0x0c) and the value of `D11(0x3e0)` (PHY revision 0 or 1) are
  assumptions of the model.
* The second read of OTP word 16 is made without setting bit 4 of `CC(0x190)`
  first. Whether the OTP can be read in that state on the real card is
  unknown (the model does not care); if not, `otp_read_word` fails and
  `sh+0xf2` keeps what the OTP layer left there. Only relevant with
  boardflags3 bit 13, and the value is not used by this radio.
* Meaning of bits 0 and 3 of the result of `wlc_phy_cap_get` and of the
  interference mode values (1, 7): defined by the consumers, not examined
  further.
* `wlc_phy_shared_detach` and the release of `sh` were not examined.
* What the two groups of values of `wlc_phy_hwaci_init_acphy` and the
  fields `pi_ac+0x64a..0x64f` (2, 6, 7, 10, 8, 8) mean.
