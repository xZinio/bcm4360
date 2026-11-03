# Task `small`: the offload semaphore, the global NVRAM, the module table, a PCIe workaround

Four small pieces of Broadcom's code that still run very often (or for very
long) in the hybrid session, each with its own specification section,
interface and adapter. Write four files:

| Write | Specification | Interface (given: keep prototypes and structures exactly) | Adapter (given) |
|---|---|---|---|
| `open/mac/offload.c` | [`../spec/bmac-init.md`](../spec/bmac-init.md) A8, the paragraphs on `tcm_sem_enter` / `tcm_sem_exit` including "Exactly" | `open/include/bcm4360/offload.h` | `hybrid/mac_sem.c` (switch `open_mac`) |
| `open/chip/nvram.c` | [`../spec/srom-otp.md`](../spec/srom-otp.md), "nvram_ro.c: the global NVRAM" (all of it, with the details after the list) and "Notes for an implementation of nvram_ro.c" | `open/include/bcm4360/nvram.h` | `hybrid/nvram.c` (switch `open_nvram`) |
| `open/lib/module.c` | [`../spec/iovar.md`](../spec/iovar.md), "Module register and unregister" | `open/include/bcm4360/iovar.h` (the part after "Module register and unregister") | `hybrid/iovar_mod.c` (switch `open_iovar`) |
| `open/chip/si_pcie.c` | [`../spec/chip.md`](../spec/chip.md), "si_survive_perst_war" (with `pcie_survive_perst`) | `open/include/bcm4360/si.h` (`bcm4360_si_survive_perst_war`, and the rest of the open backplane's functions, which you call: `bcm4360_si_setcore`, `bcm4360_si_setcoreidx`) | `hybrid/si_perst.c` (switch `open_si`) |

## Why

The semaphore is called around every access of the shared memory (2,619
times in the session), the NVRAM lookup is the fallback of every board
variable that the SROM does not have (526 times), and the 29 modules of the
driver enter themselves in the module table at attach; the PCIe workaround
waits 20 ms in 2,000 rounds at every bring-up. Together about 0.5 % of
everything the driver executes with the open PHY - the largest remaining
pieces of Broadcom code that are this small. With this task they become open
code inside the real driver (hybrid build).

## Rules for this code

* Clean room: `../implementing.md`, binding.
* Semaphore: the words of the shared structure are read and written through
  the `volatile u32 *` of the interface, each read done again in every round,
  in the order of the specification. The spin count variable is the
  caller's (`max_spins`).
* NVRAM: the list is this file's own (a static variable, empty at load); no
  other global state. Allocate and free exactly as the specification says
  (sizes and order: the tests compare the use of the heap), with
  `osl_malloc`/`osl_mfree` and the `osh` you are given; the file through
  `osl_os_open_image`/`osl_os_get_image_block`/`osl_os_close_image`; string
  helpers `hw_strlen`, `hw_memcmp`, `hw_memcpy` of `bcm4360/hw.h`.
* Module table: the slots are the caller's (`struct bcm4360_module`, 64 of
  them); copy the name with `hw_strncpy` (the OS layer's, not a loop of your
  own: it differs between kernels and must stay the same as Broadcom's);
  compare with `hw_strcmp`.
* PCIe workaround: the PCI configuration space through `hw_pcicfg_read`/
  `hw_pcicfg_write` of `si->hw`, the core selection through the open
  backplane (`bcm4360_si_setcore`, `bcm4360_si_setcoreidx`: the window moves
  must be the same), the PCIe core's register 0 through the fixed window
  `si->hw->pcie`, the delays with `hw_udelay` (the same number and length as
  the specification: the tests compare the time spent).
* Reproduce the exact results, including the cases the specification spells
  out (an empty name, a full table, a missing file, a file longer than one
  block, a string without its NUL at the end of a block; the chip, revision
  and bus checks of the workaround).

## Tests

From `tools/re`; the hybrid tools need Linux (Docker image `bcm4360-emu`):

* `python ab.py build` - your files compile without warnings.
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-small bcm4360-emu:latest python hybrid.py
  small` - the object alone and the hybrid core (with your code in place of
  Broadcom's) get the same 897 calls: the semaphore with a stand-in offload
  state (spinning until the limit, the ARM letting go after some reads, the
  largest count kept), the NVRAM with blocks made by hand and with files
  `nvram.txt` of the emulated host, the module table up to full and back,
  the PCIe workaround with and without the reset, with masks and values, on
  other chips, revisions and buses. Done when it reports `the same`
  (results, the memory each function works on, the use of the heap, the time
  spent in delays, and every access to the card). Each of the four layers is
  linked as soon as its open file exists (`hybrid-new-olsem-objs`,
  `hybrid-new-nvram-objs`, `hybrid-new-module-objs`, `hybrid-new-perst-objs`
  in `hybrid/objects.mk`); a file that does not compile stops every hybrid
  run, so keep them compiling.
* the same with `--off mac`, `--off nvram`, `--off iovar`, `--off si` must
  also report `the same`.
* `python hybrid.py check` and `python hybrid.py check --on phy` (same Docker
  command): the whole driver session, in which your code runs at every
  shared-memory access, every board variable lookup, every module attach and
  every bring-up and shutdown - must report "the same". Also `python
  hybrid.py srom` (the board variables of ten cards).
* `python ab.py all` must still pass.

Questions into `docs/re/questions/small.md`; finish with a short report
(files, functions, test results, questions).
