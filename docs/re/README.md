# Reverse engineering Broadcom's core (`wlc_hybrid.o_shipped`)

`bcm4360.ko` is open glue linked against one proprietary object. This
directory documents that object: what is in it, how it talks to the glue and
to the hardware, and - as specifications - what it does to a BCM4360, so that
it can be replaced by open code.

## Documents

| Document | Contents |
|---|---|
| [01-anatomy.md](01-anatomy.md) | the object: build, sections, source files, what runs for a BCM4360, the firmware-like data in it |
| [02-interface.md](02-interface.md) | imports and exports, the configuration interface the glue uses |
| [03-tools.md](03-tools.md) | the tools in `tools/re/`: loader, disassembler, index, decompiler script, emulator, traces, replay of recordings |
| [04-method.md](04-method.md) | how the work is organised: specifications, independent implementation, comparison by register trace; legal frame |
| [05-status.md](05-status.md) | what is known, what is verified and how, what is missing, what to do next |
| [spec/](spec/) | the specifications, one per area |

## In one paragraph

The object is an unstripped-data, partly symbol-stripped relocatable file of
1.58 MB code from 111 source files. It contains a complete 802.11 station
(MAC management, WPA supplicant, rate control) on top of a hardware layer
(chip bring-up, MAC core, PHYs for many chips). It reaches the kernel and the
hardware only through 85 imported functions, which makes it possible to run it
unmodified in an emulator against a model of the card and to record every
register access with the call chain that caused it. For the BCM4360 about
12 % of the code is exercised by bring-up and channel changes. Those traces
are the reference that specifications and new code are checked against.

## Reproducing

```
make fetch                         # lib/wlc_hybrid.o_shipped
pip install pyelftools capstone unicorn
cd tools/re
python selftest.py                 # 18 checks, a few seconds
python run_attach.py --up --trace ../../re-out/up-trace.txt
python trace.py calls ../../re-out/up-trace.txt 4
```

Everything derived from Broadcom's code is written to `re-out/` (ignored by
git). The documents here describe behaviour in our own words; they contain no
code of the object.
