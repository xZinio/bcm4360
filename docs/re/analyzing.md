# Guide for analysts

You describe, in your own words, what functions of Broadcom's object do to
the hardware and to their state, so that someone who has never seen the
object can write code that does the same. Read this completely first, then
[04-method.md](04-method.md) and one finished specification of a similar kind
(for example [spec/acphy-txpower.md](spec/acphy-txpower.md)).

## What you have

* The object, `lib/wlc_hybrid.o_shipped` (`make fetch`).
* The decompilation: `re-out/ghidra/c/<offset>_<name>.c`, one file per
  function (`tools/re/decompile.sh`, Ghidra 11). Ghidra's `DAT_xxxxxx` /
  `PTR_` addresses are translated with `python tools/re/blob.py addr 0x...`.
* The index: `python tools/re/index.py show NAME` (callers, callees, data,
  strings), `index.py module FILE.c`; `re-out/index.tsv`.
* The disassembler: `python tools/re/blob.py dis NAME`, data:
  `blob.py data NAME [COUNT [WIDTH]]`.
* The emulator (`docs/re/03-tools.md`): any function can be run on the
  modelled card and every register access recorded with its call chain.
  `tools/re/phy_scenarios.py` and `tools/re/hybrid.py` show how a session is
  set up; `tools/re/scenarios.py` has `blob_pi(s)` (the PHY state `pi` of a
  session). Use it to check what you read: run the function, look at the
  trace.
* The existing specifications in `spec/` and the open code in `open/`, whose
  state structure `open/include/bcm4360/phy.h` names, for each field, the
  place in the object (`pi+0xc2e`, `pi_ac+0x64a`, `sh+0x95`).

## Names of the object's structures

* `pi`: the PHY state of the common layer (`wlc_phy_cmn.c`), 0x1170 bytes.
  `pi+0x20` is `sh`, `pi+0x138` is `pi_ac`, `pi+0x148` the registers of the
  802.11 core, `pi+0x160` the PHY type (11 = AC), `pi+0x164` the PHY revision,
  `pi+0x168` the number of cores, `pi+0x17e` the current chanspec.
* `sh`: the state shared by the bands (`wlc_phy_shared_attach`), 0x100
  bytes. `sh+0x10` osh, `sh+0x18` sih, `sh+0x20` physhim (the MAC's handle for
  the `wlapi_*` services), `sh+0x28` the core revision, `sh+0x3c` the chip id.
* `pi_ac`: the state of the AC-PHY (`wlc_phy_ac.c`), 0x920 bytes.

Names of functions without a symbol (`sub_XXXXXX`) may be given an assigned
name in your specification ("assigned: wlc_phy_..."), as the other
specifications do.

## What a specification contains

* **Scope**: a table of the functions it describes (name, `.text` offset,
  size, where the name comes from). Tools read the first column.
* **Interface**: for every entry point, its arguments and result, and the
  open function that is to implement it (names fixed in
  [spec/acphy-api.md](spec/acphy-api.md)).
* **State**: every field read or written, by its place in the object and,
  where `open/include/bcm4360/phy.h` has it, by the field of
  `struct bcm4360_phy`; new fields with type, meaning, initial value and
  which procedure sets them.
* **Procedures**: numbered steps in plain language, with the conditions,
  formulas (integer arithmetic exactly as the object does it: widths,
  signedness, rounding), register accesses (`PHY(a)`, `RADIO(a)`,
  `TBL(id)[off]`, `SHM(o)`, `D11(o)` as in [spec/access.md](spec/access.md)),
  delays, and calls of services of other layers (`wlapi_*`, `si_*`, `ppr_*`)
  with their arguments.
* What is **only** reached for other chips or PHY revisions: say so in one
  line ("PHY types other than 11: not described"). The scope is chip 0x4360,
  AC-PHY revisions 0 and 1, radio 2069 revisions 3 and 4.
* **Verification**: a table of the statements you checked by running the
  object, and how; mark everything else as read from the code only.
* **Open questions**: what you could not settle.

## What a specification must not contain

* No code of the object: no decompiler output, no disassembly, no C that
  mirrors the object's code line by line. Describe the behaviour; structure
  your text after the procedure, not after the object's control flow.
* No tables of the object's data beyond what a short list in prose can say.
  Data that the open code needs as a whole (PHY tables, radio tables) is
  loaded at runtime from the object (`hw_fw_data()`, `hybrid/fwdata.h`): name
  the symbol and describe the format.

Constants needed to talk to the hardware (register addresses, bit positions,
values written) are part of the behaviour and belong in the specification.

## Where your work goes

* The specification: `docs/re/spec/<name>.md`.
* Notes, scripts, traces, names you assigned: `re-out/analysis/<name>/`
  (ignored by git).
* Do not edit `open/` and do not write code for it: that is the implementers'
  part (implementing.md), and they must not see the object's code.
