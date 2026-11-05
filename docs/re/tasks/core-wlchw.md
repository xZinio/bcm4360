# Task: the upper layer's hardware steps in the core (`open/core/core.c`)

Milestone 1 of [07-open-driver.md](../07-open-driver.md). The core
(`open/core/`, task [core-env](core-env.md), questions in
[questions/core-env.md](../questions/core-env.md)) runs the open layers
without Broadcom's code. What Broadcom's upper layer adds around them is now
specified in [spec/wlc-hw.md](../spec/wlc-hw.md). Implement it:

1. Sections 1-4 (attach, up and init, channel change, periodic work, down
   and detach) in `bcm4360_core_attach/_up/_set_chanspec/_down/_detach` and
   the core's 1 s timer, replacing the `wlc-hw:` / `TODO wlc-hw` places
   and the guessed values of the core-env task where the spec now says
   what they are.
2. Section 5 (joining, BSS state) in `bcm4360_core_set_bss()`,
   `_set_edca()`, `_set_macaddr()`, `_get_tsf()` (section 5.8 says what
   the core has to do).
3. Section 6 (receive filters, scanning) in `bcm4360_core_set_filter()`
   and `_set_scanning()` (section 6.3).

The power limits of a channel come from another spec (channel-power.md,
being written): keep passing NULL limits where the spec says the
regulatory-limits call sits, marked `/* channel-power */`.

Not yours: the frame path (`open/core/frames.c`, `frames.h`, `rx.c`,
`tx.c`: another implementer writes them now, from frames.md). Do not edit
those files; if you need something from the frame path, declare it in your
questions.

## Test

`tools/re/standalone.py check` (see core-env.md): the goal is that every
step is the same as the object's, except the power-limit writes of the
channel steps (channel-power). Report, per step, the accesses of the
object and of the core and what still differs, with the reason.
