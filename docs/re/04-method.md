# Method

## Two sources of knowledge

**Static.** The object is disassembled and decompiled (Ghidra). Functions are
attributed to their source files, cross references are indexed. Data objects
kept their names, a third of the functions did not.

**Dynamic.** The object is executed in an emulator against a model of the
card. Every hardware access is recorded with the chain of callers. This gives
what static reading cannot: the actual order of events and the actual values
for a given card, channel and configuration, and a way to test any statement
about the code ("for 80 MHz channels register X is written with Y") by running
it.

The two check each other. A specification is written from the code and
verified against traces; a trace raises questions that the code answers.

## From object to open code: three separate steps

1. **Specification.** Analysts who read the decompiled code describe, in
   their own words, what each function does to the hardware: registers,
   values, order, conditions, formulas, table formats. The result is in
   [`spec/`](spec/). A specification contains no code of the object.
2. **Implementation.** New code is written from the specifications by people
   (or agents) who have not seen the object's code or its decompilation. They
   get the specification, the hardware access interface and test results.
3. **Comparison.** The new code is compiled and run in the same emulator,
   against the same model of the card, with the same inputs as the object
   (`tools/re/elfobj.py` loads a compiled object next to Broadcom's). Both
   produce a register trace; the traces are compared access by access. A
   difference goes back as a question to step 1 ("the object writes 0x13 to
   PHY register 0x408 after the table, the specification does not mention it")
   - not as code.

The comparison is a black box test: it looks at what both programs do to the
hardware, not at how they are written.

### Who did what

The analysis, the specifications and the code in `open/` were made by
instances of an AI model (Claude), each started with a written assignment:

* *Analysts* had the decompiler output, the disassembler and the emulator,
  and wrote specifications (`re-out/analysis/GUIDE.md` is their guide).
* *Implementers* had the specifications, `open/`, and the comparison tool,
  and were told not to open anything derived from the object
  ([implementing.md](implementing.md) is their guide). Their questions to
  the analysts are kept in [`questions/`](questions/).
* The session that coordinated both wrote the tools, the test scenarios and
  the general documents; it has seen decompiler output and therefore wrote no
  code in `open/` except the platform header `hw.h`, which contains no
  behaviour of the object.

Two limits of this separation should be known to whoever relies on it. It is
enforced by instruction, not by technical means. And a language model carries
general knowledge of published code; the open `brcmsmac` driver of the Linux
kernel (ISC licensed), which descends from an older version of the same
Broadcom code, is part of that. Where an implementer noticed such knowledge
coinciding with a choice the specification left open, it is noted in the
questions file of the task.

## What the emulator can and cannot prove

It proves that new code performs the same register accesses as the object for
the tested inputs. It does not prove that those accesses make a radio work:
the model is not a radio, and values the hardware produces (calibration
results, measured power) are not simulated. For that the recordings of the
real card are needed (`tools/re/capture-on-target.sh`, `mmiotrace.py replay`):
replaying a recording runs the object in the emulator with the values the
real card returned, which shows what the object does with real measurements,
and makes the comparison of step 3 possible for them as well. The last
word is the hardware.

## Rules for everything in the repository

* No disassembly, no decompiler output, no microcode, no tables of the object
  in the repository. They live in `re-out/`, which git ignores.
* Specifications describe behaviour and formats. Constants that are needed to
  talk to the hardware (register addresses, bit positions, command values)
  are part of that.
* Data that an open driver has to load (microcode, PHY tables, radio tables)
  stays Broadcom's. It is cut out of the object on the user's machine
  (`tools/re/fwcut.py`), as `b43-fwcutter` does for the `b43` driver.
* Values that come from the model and not from a real card are marked as such.

## Legal frame

This is not legal advice; the owner of the repository decides what to do and
what to publish.

* Broadcom's license (`lib/LICENSE.txt`, section 2.6) says the licensee shall
  not "attempt to reverse engineer, decompile or disassemble any portion of
  the Software".
* In the European Union, Directive 2009/24/EC gives the lawful user of a
  program rights that a contract cannot take away (article 8): to observe,
  study and test the functioning of the program while running it (article
  5(3)), and to decompile it where that is indispensable to obtain the
  information necessary for the interoperability of an independently created
  program (article 6), if the information was not readily available before
  and the work is confined to the parts necessary for interoperability. The
  directive's recitals count the interaction of software with hardware among
  the interfaces meant. Germany implements this in sections 69d, 69e and 69g
  of the Urheberrechtsgesetz.
* Article 6 also limits what may be done with the result: it may not be used
  for anything else than the interoperability of the independent program, not
  be passed on except where interoperability requires it, and not be used to
  make a program substantially similar in its expression. The separation of
  specification and implementation above, and keeping decompiler output out
  of the repository, follow from that.
* Outside the European Union the situation differs from country to country.
* The open `b43` driver for older Broadcom chips was made the same way:
  specifications written from the vendor's driver, code written from the
  specifications.
