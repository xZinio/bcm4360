# Guide for implementers

You write open code for the BCM4360 from specifications. Read this completely
first.

## The one rule

You work **only** from

* the specifications and documents in `docs/re/` (this directory),
* the code and headers in `open/`,
* the results of the comparison tests (`tools/re/ab.py`).

You do **not** look at Broadcom's object or anything derived from its code:
nothing below `lib/` and `re-out/` (except `re-out/open/`, the build output of
your own code), no disassembly, no decompiler output, and you do not run
`tools/re/blob.py`, `index.py`, `fwcut.py` or Ghidra. If a specification is
unclear, incomplete or contradicted by a test, you do not go and look - you
write the question down (see "Questions") and work on with the most plausible
reading. This separation is what makes your code independent; keep it strict.

You do not need the network. Do not use git.

## What a test tells you

`ab.py` runs Broadcom's object and your code in an emulator against the same
model of the card and compares what both do to the hardware: every register
access, its order, its value, the delays in between. A difference is reported
like this:

```
  at access 46 of the object's trace (replace):
      both     W d11         003fc/4 = 102d042d
    - object   R d11         003e0/2 = 0b01
    + open     W d11         003fc/4 = 102f042f
```

That is an observation of behaviour. Use it: it is the purpose of the test.
When the specification and the observed behaviour disagree, the observed
behaviour is right, and the disagreement goes into your questions list.

Where the code under test calls another layer of the driver (the PHY calls
the MAC layer, for example), the test does not run that layer but notes the
call: `call mac_suspend()`, `call chip_core_cflags(0xc0, 0x80)` appear in the
list between the register accesses, for the object and for your code alike
(see `open/include/bcm4360/phy_env.h`).

## Code

* C11 with GNU extensions, Linux kernel coding style (tabs, 80..100 columns,
  `snake_case`, no typedefs for structures).
* Freestanding: no libc, no floating point, no 64 bit division by a variable
  without need. Everything from the environment comes through
  `open/include/bcm4360/hw.h`. Integer types from `bcm4360/types.h`.
  Beware of code the compiler turns into library calls: an initializer of a
  whole structure or a large local array (`struct x v = { ... }`, `= {0}`)
  or a structure assignment may become a call of `memset`/`memcpy`, which
  the emulator of the hybrid build does not provide ("import memset is not
  implemented"). Set fields one by one, or use `hw_memset`/`hw_memcpy`.
* No global mutable state: state lives in structures passed as arguments (a
  machine can have more than one card).
* Every function has a comment that names the specification and section it
  implements, for example `/* docs/re/spec/access.md, "PHY registers" */`.
* Constants get names. A register whose purpose is unknown is named by its
  address (`ACPHY_REG_0x408`), not by a guess.
* SPDX line at the top of every file: `// SPDX-License-Identifier: ISC` in
  `.c` files, `/* SPDX-License-Identifier: ISC */` in headers.
* Keep functions small; mirror the structure of the specification (one
  function per specified procedure) so that a reader can put both side by side.

## Building and testing

From `tools/re`:

```
python ab.py build            # compile everything in open/ (errors and warnings are shown)
python ab.py list             # the scenarios
python ab.py run NAME         # run one scenario and show the differences
python ab.py run NAME --show 50
```

A task is finished when its scenario reports `0 differences`, the build has no
warnings, and the code follows the rules above.

### The PHY inside the driver: `hybrid.py`

The open PHY also runs inside the driver's core in the emulator
(`hybrid/README.md`): `python hybrid.py check --on phy` links Broadcom's
object with the open PHY in its place (all 79 entry points of
[spec/acphy-api.md](spec/acphy-api.md), through `hybrid/phy.c`) and runs a
whole session - attach, up, channel changes, periodic work, down, detach -
on it and on the object alone, and compares every access to the card. As
long as an open function is missing, the session stops where it is first
called ("import bcm4360_phy_... is not implemented") and the accesses until
then are compared. This is a comparison test like `ab.py`: you may run it and
use what it reports. The prototypes of the entry points are in
`open/include/bcm4360/phy_api.h`; keep the names and arguments, they are the
interface `hybrid/phy.c` calls.

`python phy_soak.py` (same place, Docker) is a much longer session for the
open PHY: every channel and bandwidth, an association stand-in, long watchdog
runs with the calibrations, RSSI on many receive headers, the noise
interrupt; the card model gives the measurement registers deterministic
values. It compares the accesses and the results of every step, and reports
the steps that differ. It is a comparison test like the others: run it, use
what it reports.

## Questions

Write everything you had to guess, everything the specification got wrong or
left out, into `docs/re/questions/<task>.md`: what the specification says
(section), what the test shows, what you did. These go back to the analysts.

## Report

Finish with a short report: what you implemented (files, functions), the test
result, the questions you raised.
