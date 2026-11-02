# Questions of the implementer: task `bmac-runtime`

Specification: [`../spec/bmac-run.md`](../spec/bmac-run.md), sections R1, R2,
R5, R6 and R7 (`wlc_bmac_read_tsf`), with "Data", and
[`../spec/bmac-init.md`](../spec/bmac-init.md) A3, A4, A6, A7, A8. Code:
`open/mac/bmac_runtime.c` (`bcm4360_bmac_ifsctl_edcrs_set` with the static
`wlc_bmac_ifsctl_ac_edcrs` (`sub_06212d`) and `wlc_bmac_ifs_ctl_edcrs`
(`sub_0622d9`); `bcm4360_bmac_tx_fifo_suspend`, `_resume`, `_suspended`;
`bcm4360_bmac_cca_stats_read` with the static `wlc_bmac_read_shm32`
(`sub_061ad3`) and `bcm4360_bmac_cca_stats_get`;
`bcm4360_bmac_rate_shm_offset` with the table `rate_info`,
`bcm4360_bmac_stf_get_rateset_shm_offset`, `_set_`;
`bcm4360_bmac_read_tsf`). No other file changed.

## Test results

All in Docker, `HYBRID_OUT=/repo/re-out/hybrid-bmacruntime`,
`AB_OUT=/repo/re-out/open-bmacruntime`:

* `python ab.py build`: no warnings.
* `hybrid.py check`, `check --off mac`, `check --on phy`, `check --on phy
  --off mac`: 41416 accesses of the object, 41416 of the hybrid core, the
  same.
* `hybrid.py srom`: all ten cards the same.
* `phy_soak.py --on=`: 150 steps, 588350 accesses of the object, 588350 of
  the hybrid core, the same.
* `python ab.py all`: all 25 scenarios ok, 0 differences (no scenario of
  this layer).

To make sure the session really runs the open functions, I mounted two
broken copies of `bmac_runtime.c` over the file inside the container only
(output directories of their own, removed afterwards). Copy A: 40 MHz
lower-sideband primary 6 instead of 2, bit 12 added to the 20 MHz `SHM(0x5a)`
write, no DMA suspend of FIFO 3, no DMA resume of FIFO 0. Copy B: `rate_info[2]`
= 0x0b, the `stf_set` values xored with 1, CCA word 8 from `SHM(0x10a)`, the
TSF low word from `D11(0x188)`. Each change showed up: the `SHM(0x5a)`
write at all six 20 MHz calls, `D11(0x6c8)` at the two 40 MHz channels with
sideband 0 (and the 80 MHz read after the first of them), the suspends of the
mutes and the resumes of the unmutes, the directory word 0x216 for 0x214,
the `SHM(0x0e)` writes of `stf_set`, the CCA and TSF reads. So R1 (20 and
40 MHz; 80 MHz is reached as well), R2 suspend and resume, R5, R6 get and set
and R7 all run as open code in the session. Not reached by it: `tx_fifo_suspended`, the re-read of
`sub_061ad3`, `on` = 0 of R1 and the revisions below 40 (written from the
text).

## 1. R6: the sums of `stf_get` and `stf_set` and the 16-bit offset of the access layer

* Specification: R6 `stf_get` step 2, "`out[i]+4` (u16) = `SHM((o & 0xffff)
  + off)` (the sum is not truncated to 16 bits)". `stf_set`: "`SHM(off +
  in[i]+2) = in[i]+4`" - nothing said about truncation. The task points to
  the access layer for the shared memory, whose `bcm4360_shm_read`/`_write`
  take a **16-bit** offset, so a sum above 0xffff would be cut there, while
  the A8 worker writes `0x10000 | (offset >> 2)` to `D11(0x160)` with the
  whole 32-bit offset (a sum of 0x10010 selects word 0x4004, a cut one word
  0x0004).
* Test: not visible - in the sessions the directories read 0, so the sums
  are 0x0e.
* What I did: these two accesses go through the open A8/B4 primitives that
  take a 32-bit offset - `bcm4360_bmac_copyfrom_objmem(mac, sum, b, 2,
  0x10000)` (one A8 read) and `bcm4360_bmac_set_shm(mac, sum, v, 2)` (one A8
  write) - so the sum is passed whole, as the specification says for `get`.
  For `set` I assumed the same (two 16-bit values added in 32 bits). Every
  other shared-memory access of the file uses `bcm4360_shm_read`/`_write`
  (their offsets fit in 16 bits). Is the `set` sum also untruncated? And
  should the access layer get a 32-bit offset instead (then the two helpers
  become plain calls)?

## 2. R1 `sub_06212d` step 3: the condition `corerev <= 40`

* Specification: step 3, "If `corerev` <= 40: `SHM(0x5a) = v` (not on this
  card, rev 42)". The task says to write branches for other MAC revisions as
  a comment, "except where the specification gives the branch condition on
  `mac_rev` for this function (R1 `sub_0622d9` for revisions below 40, R5 the
  base 0x6c0 below 40)".
* What I did: I read the exception as covering every condition on `mac_rev`
  the specification gives for these functions, so step 3 tests `mac_rev <=
  40` as written (as do `sub_0622d9` (< 40), R5 (>= 40) and R6 `stf_get`
  step 2 (>= 40)). Not reached at revision 42.

## 3. R1: the steps for the N- and HT-PHY

* Specification: step 1 returns for an N-PHY below MAC revision 16 and for
  types other than 4, 7, 11; steps 2 and 3 have branches for the N-PHY (by
  PHY revision) and the HT-PHY.
* What I did: as the task asks for other PHY types, no code: the function
  returns at once unless the current band's PHY type is 11 (comments name
  the N/HT steps). For the AC-PHY, step 2 (`on` = 0: `sub_0622d9(0x08,
  0x08)`, nothing at revision 42) and step 3 (`sub_06212d(.., 1)`) are
  written. Step 3 "t is read again" is not repeated: nothing between step 1
  and step 3 can change it (no service is called).

## 4. Small decisions, no answer needed unless wrong

* R2 suspend step 1: "(1 << `fifo`) truncated to 8 bits" is 0 for `fifo` >=
  8 (written so, to avoid a shift by 32 or more in C), and then step 1
  returns (`x & 0` = 0). The FIFO's engine test uses the same bit, so a
  FIFO >= 8 has no engine.
* R2 resume step 2: "a rotate of 0xfe by `fifo`" is written as an 8-bit
  rotate by `fifo` mod 8, which for 0..7 clears just bit `fifo`.
* R2 `suspended`: the service is called without the engine test, as the
  specification says; `mac_env.h` says it answers false for a FIFO without
  one (the object would crash).
* R2 suspend: EN_MAC (`e`) is kept in a local across the DMA service; it is
  a register value, not a field. The PHY type of step 8 is read from the
  state again after the service.
* R6 `rate_info`: indices 0..127 with the values R6 lists, including the
  bytes it lists for 109..127 (the start of `mcs_table` in the object). A
  rate above 127 is "not in the table" (value 0, the CCK directory's first
  word `SHM(0x200)`), as the task asks for Q3; no caller passes one (the
  rate-set callers mask to 7 bits).
* R5: the counters are read with `bcm4360_shm_read` (16-bit offsets; `base
  + off + 2` fits). The high word of the TSF goes to a local and is
  dropped.
* The semaphore of A8 (`tcm_sem_enter`/`_exit`): the two R6 helpers of
  question 1 call it (through the bmac_mem.c primitives), the access layer
  does not; without offloads both do nothing.
