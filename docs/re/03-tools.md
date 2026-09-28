# Tools

Everything is in `tools/re/`. Python 3 with `pyelftools`, `capstone` and
`unicorn` (`pip install pyelftools capstone unicorn`); Ghidra 11 or later with
a JDK 21 for the decompilation. The object is expected at
`lib/wlc_hybrid.o_shipped` (`make fetch`).

Nothing here modifies the object. Output that is derived from Broadcom's code
(disassembly, decompilation, extracted microcode and tables, register traces)
goes to `re-out/`, which is ignored by git and must stay out of the repository.

## Static analysis

| Tool | Purpose |
|---|---|
| `blob.py` | Loads the object the way the kernel's module loader would (sections laid out in a flat address space, relocations applied). `stats`, `funcs [PATTERN]`, `dis NAME...` (disassembly with symbols, strings, switch tables), `callers NAME`, `callees NAME`, `strings NAME`, `addr GHIDRA_ADDRESS`, `data WHAT [COUNT [WIDTH]]`. |
| `modmap.py`, `modmap.txt` | Function to source file. `modmap.py` prints the table of [01-anatomy.md](01-anatomy.md), `--funcs` every function. |
| `index.py` | Cross references of every function: callers, callees, data, strings. `build`, `show NAME...`, `module FILE.c`. |
| `fwcut.py` | Microcode, initialisation lists, PHY and radio tables: `list`, `phytables`, `initvals NAME`, `extract DIR [COREREV]`. |
| `srom.py` | SROM images, with the layout read from the object: `table [REV]`, `decode IMAGE`, `encode VARS IMAGE`. |
| `ghidra/ExportDecomp.java` | Ghidra headless script: decompiles every function to `OUT/c/<offset>_<name>.c`. |

Decompiling (about three minutes on a desktop machine): `tools/re/decompile.sh
GHIDRA_DIR`, on Windows `tools\re\decompile.ps1` (looks for Ghidra and a JDK
below `re-out\tools`). It writes `re-out/ghidra/c/<offset>_<name>.c` and the
index `re-out/index.tsv`.

Ghidra packs the sections from 0x100000 on: `.text` 0x100000, `.rodata`
0x281130, `.rodata.str1.1` 0x58a130, `.data` 0x58eb60, `.bss` 0x6df380.
`blob.py addr` translates.

## Dynamic analysis: the emulator

The object only reaches the outside through its imports
([02-interface.md](02-interface.md)), so it can run anywhere if the imports
are provided. `emu.py` runs it under Unicorn:

* `emu.py`: the machine. Maps the object, stack and heap; dispatches imports
  to Python functions; `Machine.call(name, args...)` calls any function of the
  object. Import handlers that have to call back into the object (as the
  glue's `wl_up()` calls `wlc_up()`) are written as generators and `yield
  Call(...)`. `on_call()` hooks the entry of a function, `record_coverage()`
  records executed blocks.
* `osl.py`: the OS side in Python: memory, strings, `printf`, a virtual clock
  (`osl_delay` advances it), timers, packets, the `wl_*` callbacks of the glue.
* `chip.py`: a PCIe card with a Broadcom AXI backplane: PCI configuration
  space with the window registers, enumeration ROM built from the core list,
  wrappers with reset and clock state. Every access is recorded with the call
  chain (walk of the frame pointers).
* `bcm4360.py`: the BCM4360: ChipCommon with PMU, SROM, OTP; the 802.11 core
  with the indirect PHY, radio and shared memory accesses; PHY table writes
  are recorded as such.
* `run_attach.py [--up] [--srom FILE] [--trace OUT]`: `wlc_attach()` and
  `wlc_up()`.
* `run_chan.py CHANSPEC... [--out DIR]`: channel changes, one trace each.
* `coverage.py`: which code runs.
* `trace.py`: views of a trace: `summary`, `calls [DEPTH]`, `tree [FROM [TO]]`,
  `flat [FROM [TO]]`.
* `selftest.py`: checks the tool chain against the object (18 checks).

## Recordings of the real card

* `capture-on-target.sh` runs on the machine with the card: PCI configuration
  space and a kernel `mmiotrace` of the driver loading and coming up.
* `mmiotrace.py replay DIR` runs the object in the emulator with the values
  the real card returned, access by access; it reports the identity of the
  card (chip, cores, PHY, radio), writes the SROM image it saw and a register
  trace with function names, and lists where the model answers differently
  from the card. `mmiotrace.py selftest` checks the mechanism with a
  recording made from the model.

## Comparing open code with the object

* `elfobj.py` loads a compiled relocatable object of the open code into the
  same emulator; its undefined symbols resolve to the same imports.
* `ab.py build` compiles `open/` with `zig cc` (freestanding x86-64, kernel
  code model), `ab.py run NAME` runs a scenario on both and prints the
  differences of the two register traces, `ab.py all` runs all, `ab.py
  selftest` checks the tool itself. See [04-method.md](04-method.md) and
  [implementing.md](implementing.md).
* `scenarios.py`: scenarios for single procedures (register access, radio,
  PMU). A function of the object is called, or cut out of a session by its
  entry and exit, and the corresponding function of the open code is run on a
  card in the same state.
* `phy_scenarios.py`: scenarios for the PHY as a whole. The object runs a
  session (attach, up, channel changes, periodic work, down); every call of
  the MAC layer into the PHY is a stage; the open code is taken through the
  same calls in the same order and compared stage by stage. Its state is its
  own, made by its attach and changed by the calls that follow. Calls into
  other layers (`open/include/bcm4360/phy_env.h`) are not executed but noted
  in both traces with their arguments; the open code is given the results
  the object got. `phy_state.py` tells the tests where the object keeps the
  values that are compared directly.

```
cd tools/re
python emu.py                                   # self test
python run_attach.py --up --trace ../../re-out/up-trace.txt
python trace.py calls ../../re-out/up-trace.txt 4
python run_chan.py 36/80 6 --out ../../re-out/chan
```

### What the model is, and what it is not

The model answers what the driver asks during bring-up: identification,
enumeration, reset and clock status, SROM and OTP contents, read-back of what
was written. It does not simulate the radio. Consequences:

* Register **writes**, their order and their values are those of the real
  driver code, given the same inputs. This is what the traces are for.
* Values **read** from PHY and radio registers are the last value written, or
  0. Status bits that hardware sets by itself (calibration finished, PLL
  locked, measured power) are not modelled: loops that wait for them end by
  timeout, and everything computed from measurements is computed from zeros.
  (Option `radio_done` of the model: the calibrations of the radio report
  "done" at once, with fixed results.)
* The AC-PHY code writes many registers that exist once per core with bit 12
  of the address set (0x1725 for 0x725). The model keeps such an address as a
  register of its own; presumably the hardware writes the register of every
  core. A read of the plain address after such a write may therefore give
  another value on the real card than in the model.
* The regulatory domain of the emulated session does not allow the channels
  12 to 14 and 52 to 144; the scenarios reach them by calling the channel
  function of the PHY directly.
* The identity of the card is modelled after what is known about BCM4360
  cards, not measured on the target machine: chip 0x4360 revision 3, package 0,
  ChipCommon revision 43, 802.11 core revision 42, PCIe Gen2 core revision 1,
  PHY revision 1, radio 0x2069 revision 4; PMU capabilities, PLL defaults and
  OTP layout register are assumptions. The SROM contents are synthetic
  (`data/synthetic-4360-2x2.vars`).

To make the traces those of the real card, the model needs the values of the
real card: see "Data from the target machine" in [README.md](README.md).
