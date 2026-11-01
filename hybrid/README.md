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
| utilities | `open_utils` | on | 7: `getvar`, `getintvar`, `getintvararray`, `getintvararraysize`, `bcm_strtoul`, `bcm_bitcount`, `hndcrc8` (the global NVRAM, `nvram_get`, stays Broadcom's) | `utils.c` -> `open/lib/utils.c` | [utils](../docs/re/spec/utils.md) |

The switches are read when a card is attached (`open_access`, `open_mac` and
`open_utils` at every call; all are read-only once the module is loaded). To
run a layer on Broadcom's code again, e.g. the PMU:

```sh
echo "options bcm4360 open_pmu=0" | sudo tee /etc/modprobe.d/bcm4360-open.conf
sudo rmmod bcm4360; sudo modprobe bcm4360; sudo systemctl restart wpa_supplicant
```

`make HYBRID=0` builds the module without any of this, exactly as 1.0.3.

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
```

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
| the same + the open PHY (`--on phy`, not yet identical, see below) | 123,171 bytes (61.5 %) | 64,378 bytes | 37.5 % |

With the open PHY switched on (emulator only; the PHY layer is not in the
module yet) the whole session now runs: all entry points of the PHY API it
reaches exist in `open/` (control, transmit power, watchdog; the receive
group is still open). The watchdog ticks match the object access for access;
the session as a whole does not yet: from the transmit-power update of a later
channel on, the power offsets the open PHY hands the MAC differ (open
questions in `docs/re/questions/phy-api-txpower.md`), and the MAC's rate table
writes follow from that.

The last column weighs code by how often it runs: the register access
primitives and the variable lookup are small but run constantly (`getvar`
alone was about a fifth of everything the driver executes). `check` gives
"the same" with all layers on and with each switched off; the module builds
against linux-headers 7.2.11-arch1-1 with GCC 16.2.1 without warnings from
`open/` or `hybrid/`, and each of the 46 taken functions of these layers has
exactly one strong definition, the adapter's (the 79 of the PHY stay
Broadcom's until the PHY adapter goes into the module).

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
