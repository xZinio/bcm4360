# open/ - hardware layer for the BCM4360, written from specifications

Code in this directory is written from the specifications in
[`docs/re/spec/`](../docs/re/spec/) and from nothing else: not from Broadcom's
object, its disassembly or decompilation. It is tested by running it in the
emulator next to Broadcom's object and comparing what both do to the
(modelled) hardware, access by access (`tools/re/ab.py`).

License: ISC, as the rest of the repository.

## State

See [`docs/re/05-status.md`](../docs/re/05-status.md). This is a hardware
layer under construction, not a driver: nothing here is built into
`bcm4360.ko` yet.

## Layout

```
include/bcm4360/types.h    integer types (kernel types when built for Linux)
include/bcm4360/hw.h       the platform interface: register access, delay, memory
hw/                        register access: PHY, radio, PHY tables, shared memory
phy/                       AC-PHY and 2069 radio
chip/                      backplane, PMU, SROM, OTP
mac/                       MAC core
```

## Rules

* C11 with GNU extensions as the kernel uses them; kernel coding style.
* No floating point, no libc. What the code needs from its environment is in
  `hw.h` and nowhere else.
* Every function says in a comment which section of which specification it
  implements.
* A function is done when its scenario in `tools/re/scenarios.py` passes:
  same accesses, same values, same order, same delays as the object.

## Building and testing

```
cd tools/re
python ab.py build          # compiles open/**/*.c with `zig cc` into re-out/open/
python ab.py list
python ab.py run NAME       # differences of one scenario
python ab.py all
```

`zig` is only needed for these tests (any zig release; set `ZIG` to the
executable if it is not on the PATH).
