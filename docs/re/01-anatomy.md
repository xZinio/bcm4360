# Anatomy of `wlc_hybrid.o_shipped`

What the object is, how it was built and what is inside. Everything here can
be reproduced with the tools in `tools/re/` (see [03-tools.md](03-tools.md)).

## Identity

| | |
|---|---|
| File | `lib/wlc_hybrid.o_shipped`, 7,350,128 bytes |
| SHA-256 | `352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743` |
| From | `hybrid-v35_64-nodebug-pcoem-6_30_223_271.tar.gz` (Broadcom, 2015) |
| Version string | `6.30.223.271 (r587334)` |
| Format | ELF64, x86-64, relocatable (`ET_REL`), not stripped of data symbols |
| Compiler | GCC 4.4.7 20120313 (Red Hat 4.4.7-3), one `.comment` entry per source file |
| Code model | kernel (`R_X86_64_32S` absolute references), `-Os` (functions are not aligned), frame pointers kept, no stack protector, no retpolines/IBT |

It is the result of `ld -r` over 111 objects, in alphabetical order of their
source file names. The symbol table still lists the 111 file names.

## Sections

| Section | Size | Contents |
|---|---|---|
| `.text` | 1,577,264 | code, 3,922 functions |
| `.rodata` | 3,182,592 | MAC microcode images, register initialisation lists, PHY tables, regulatory database, SROM layout tables, jump tables |
| `.rodata.str1.1` | 18,992 | 1,462 strings (the build is "nodebug": almost no messages; mostly names of SROM variables and of configuration variables) |
| `.data` | 1,378,336 | two firmware images for the on-chip ARM (`dlarray_4350pci`, `dlarray_4352pci`, 0.89 MB together), radio tables, transmit gain tables |
| `.bss` | 12,484 | |
| relocations | | 31,336 `PC32` and 5,808 `32S` in `.text`; 4,177 `64` in `.rodata`; 30 `64` in `.data` |

## Symbols

* 85 undefined symbols: the imports, see [02-interface.md](02-interface.md).
* 41 global functions: the exports.
* 2,681 local functions with names and 1,284 data objects with names.
* 1,200 functions have **no symbol** (found by following calls, jumps and
  code pointers, see `blob.py`). Everything known about them fits functions
  that were `static` in the source: each is referenced only from inside its
  own source file. The picture is that of a link that discarded local function
  symbols and then made the remaining ones local, except the 41 exports. Data
  objects kept their names.
* The order of the symbol table is scrambled; it does not tell which symbol
  belongs to which source file.

In all tools and documents a function without symbol is called
`sub_<six hex digits of its offset in .text>`.

## Source files

Because functions are not aligned, padding inside `.text` exists only where
the linker aligned the next input object to 4 bytes. 65 file boundaries are
proven by such padding; the others were placed with the cross references (a
`static` function is only referenced from its own file) and the alphabetical
order of the files. The result is `tools/re/modmap.txt`.

19 of the 111 files contribute data only: `bcmevent.c`, the six
`d11ucode_*.c`, `wlc_clm_data.c`, `wlc_dfs.c`, the eight `wlc_phytbl_*.c`,
`wlc_rate_def.c`, `wowlaestbls.c`.

Code per source file (bytes of `.text`; "named" = functions that kept their symbol):

| Source file | Functions | Named | Bytes | Purpose |
|---|---:|---:|---:|---|
| `wlc_phy_n.c` | 210 | 110 | 244,044 | N-PHY (other chips) |
| `wlc.c` | 455 | 371 | 166,152 | upper MAC: configuration, transmit/receive path, beacons, power save |
| `wlc_phy_ac.c` | 173 | 79 | 143,744 | **AC-PHY: the PHY of the BCM4360** |
| `wlc_phy_ht.c` | 122 | 53 | 104,708 | HT-PHY (BCM4331) |
| `wlc_phy_lcn.c` | 141 | 63 | 81,932 | LCN-PHY |
| `wlc_phy_lcn40.c` | 123 | 44 | 79,188 | LCN40-PHY |
| `wlc_phy_ssn.c` | 115 | 61 | 72,372 | SSLPN-PHY |
| `wlc_phy_cmn.c` | 232 | 207 | 69,096 | PHY code common to all PHY types |
| `wlc_bmac.c` | 182 | 148 | 44,376 | low level MAC: core reset, clocks, microcode, FIFOs |
| `wlc_phy_abg.c` | 81 | 28 | 41,596 | A/B/G-PHY |
| `wlc_phy_lp.c` | 78 | 37 | 35,912 | LP-PHY |
| `hndpmu.c` | 73 | 49 | 27,016 | power management unit |
| `wlc_ampdu.c` | 55 | 32 | 27,032 | A-MPDU transmit |
| `siutils.c` | 192 | 184 | 26,048 | backplane utilities |
| `wlc_assoc.c` | 56 | 38 | 24,976 | join, roam |
| `bcmsrom.c` | 11 | 6 | 23,780 | SROM/CIS parsing |
| `wlc_channel.c` | 67 | 56 | 23,776 | channels and regulatory limits |
| `wlc_phy_extended_n.c` | 17 | 17 | 22,600 | N-PHY |
| `wlc_phy_radio_n.c` | 14 | 10 | 21,820 | N-PHY radios |
| `wlc_offloads.c` | 67 | 43 | 19,984 | offloads to the on-chip ARM |
| `wlc_wowl.c` | 26 | 13 | 19,448 | wake on WLAN |
| `wlc_stf.c` | 50 | 37 | 15,772 | transmit chains, spatial streams |
| `wlc_rate_sel.c` | 26 | 14 | 12,768 | rate selection |
| `wlc_clm.c` | 39 | 22 | 12,124 | regulatory database access |
| `hnddma.c` | 71 | 3 | 11,228 | DMA engine |
| `bcmutils.c` | 88 | 88 | 9,436 | utilities, packet queues |
| `wlc_sup.c` | 29 | 25 | 8,788 | built-in WPA supplicant |
| `wlc_key.c` | 19 | 18 | 8,616 | keys |
| `wlc_scan.c` | 29 | 19 | 8,060 | scan |
| others (63 files) | 1,081 | 847 | 170,872 | crypto (AES, TKIP, WEP, MD5, SHA-1, RC4), 802.11d/h, protection, block-ack receive, A-MSDU, LEDs, PCIe core, OTP, ... |
| **total (92 files)** | 3,922 | 2,722 | 1,577,264 | |

The PHYs of other chips are 704 kB, 45 % of the code. They are irrelevant for
the BCM4360.

## What runs for a BCM4360

`tools/re/coverage.py` executes the object in the emulator (attach, up, eight
channel changes in both bands with 20/40/80 MHz, three watchdog ticks, down)
and records the executed basic blocks:

* 829 functions are entered, 188,728 bytes of code are executed (12 % of `.text`).
* In `wlc_phy_ac.c` 97 of 173 functions run (55 kB of 144 kB), in
  `wlc_phy_cmn.c` 81 of 232, in `wlc_bmac.c` 109 of 182, in `siutils.c`/
  `aiutils.c`/`hndpmu.c`/`nicpci.c` 93 of 338.

Scanning, joining, traffic and the periodic calibrations are not part of that
run; they add the transmit/receive path, rate selection and the calibration
code of the PHY. What did *not* run is therefore not proven dead, what ran is
proven relevant. The list per function is written to `re-out/coverage.tsv`.

## Data an open driver needs as firmware

The object carries the following for the BCM4360 (MAC core revision 42). All
of it has symbol names. `tools/re/fwcut.py extract` writes it to files.

| Data | Symbols | Size |
|---|---|---:|
| MAC microcode | `d11ucode42` (length in `d11ucode42sz`) | 43,400 |
| Microcode for wake-on-WLAN | `d11ucode_wowl42`, `d11aeswakeucode42` | 35,856 / 36,200 |
| MAC register initialisation | `d11ac1initvals42`, `d11ac1bsinitvals42` (band switch), `d11wakeac1initvals42`, `d11wakeac1bsinitvals42` | 4,888 / 592 / 4,176 / 592 |
| PHY table sets | `acphytbl_info_rev0` (23 tables), `_rev2`, `_rev3`, `_rev6` and the tables they point to | see `fwcut.py phytables` |
| Radio: preferred register values | `prefregs_2069_rev3`, `prefregs_2069_rev4`, `ovr_regs_2069_rev2` | 92 / 100 / 114 |
| Radio: channel tuning | `chan_tuning_2069rev3`, `chan_tuning_2069rev4` | 8,932 each |
| Transmit gain tables | `acphy_txgain_epa_2g_2069rev0`, `..._2069rev4`, `acphy_txgain_epa_5g_2069rev0`, `..._2069rev4`, `acphy_txgain_ipa_*` | 768 each |
| Regulatory database | `wlc_clm_data.c` objects | |
| Firmware for the on-chip ARM (offloads) | `dlarray_4352pci`, `dlarray_4350pci` | 442,233 / 445,717 |

Format of a register initialisation list: entries of 8 bytes, little endian:
16 bit register offset in the MAC core, 16 bit access size in bytes (2 or 4),
32 bit value; the list ends with offset `0xffff`. Format of a PHY table set:
entries of 24 bytes: pointer to the data, number of entries, table id, offset
in the table, width of an entry in bits (8, 16, 32, 48, 60 or 64).

The microcode of other core revisions in the object (`d11ucode40`, `41`, `43`,
`46` and the older ones back to revision 11) belongs to other chips.
