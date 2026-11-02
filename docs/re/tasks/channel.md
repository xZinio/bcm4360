# Task `channel`: chanspec helpers and channel validity

Specification: [`../spec/channel.md`](../spec/channel.md) (all of it).
Interface: `open/include/bcm4360/channel.h` (given: keep the prototypes and
the structures exactly). Write `open/lib/channel.c`.

## Why

With the open PHY in place, the largest block of Broadcom code the driver
still executes is the upper layer asking "is this channel / chanspec valid
here?" - for every channel it considers, at every channel change, scan and
power limit update. These checks and the chanspec helpers are about 1.7 % of
all execution in the hybrid session, a third of what is still Broadcom's.
With this task they become open code inside the real driver (hybrid build,
switch `open_chan`, adapter `hybrid/chan.c`).

## Rules for this code

* Clean room: `../implementing.md`, binding.
* One function per procedure of the specification (H1-H6, V1-V7); V6 and V7
  are internal to `bcm4360_chan_valid_chanspec` (V6 calls itself for the two
  halves of an 80 MHz channel).
* The tables C1, C2 and C3 are facts of the specification: write them as
  `static const` arrays. No other global state.
* `struct bcm4360_chan` is filled by the caller before every call; read it,
  never write it. The valid-channel vectors are the caller's memory.
* The regulatory database is reached only through the three
  `bcm4360_chan_env_clm_*` services of `channel.h` (the adapter provides
  them). Do not initialise the limits buffer or the parameter structure with
  `= {0}` or a structure assignment (it becomes a `memset` call the emulator
  does not provide); V7 lets `clm_limits_params_init` fill the parameters.
* Reproduce the exact results, including the odd cases the specification
  states (unsigned 32-bit arithmetic in H2, the bitwise OR in H4, channel 0
  and channel 224 boundaries, the check of the *current* band in V2).

## Tests

From `tools/re`; the hybrid tools need Linux (Docker image `bcm4360-emu`):

* `python ab.py build` - your file compiles without warnings.
* `docker run --rm -v "D:\Programs\bcm4360:/repo" -w /repo/tools/re -e
  HYBRID_OUT=/repo/re-out/hybrid-chan bcm4360-emu:latest python hybrid.py
  chan` - the object alone and the hybrid core (with your code in place of
  Broadcom's, through `hybrid/chan.c`) are brought to the same state, and
  every function of the specification is called by its name on both with the
  same arguments: the pure helpers for all 65,536 chanspecs, the validity
  checks for every channel and bandwidth in 261 states (nine countries, both
  bands, band lock, capability variations). Done when it reports
  `the same` (2,459,676 calls). The layer is linked as soon as
  `open/lib/channel.c` exists (`hybrid-new-chan-objs` in `hybrid/objects.mk`);
  a file that does not compile stops every hybrid run, so keep it compiling.
* the same with `--off chan` (Broadcom's functions through the adapter) must
  also report `the same`.
* `python hybrid.py check` (same Docker command, `check` instead of `chan`):
  the whole driver session with your code in place - must report "the same".
* `python ab.py all` must still pass.

Questions into `docs/re/questions/channel.md`; finish with a short report
(files, functions, test results, questions).
