#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Direct calls of the LED functions of the MAC layer (docs/re/spec/bmac-run.md
R4) in the hybrid core with Broadcom's LED code and with the open one (group
mac_led; hybrid.py led): after attach and up, the LED state (polarity, blink
times, time stamps, phases, the timer's period and flags, the GPIO value),
the clock, sbclk and boardflags2 are set at random per case and one function
is called - the blink timer's function (sub_061660 / the open one) most
often, since the session never blinks. The LED state, the timer's settings,
wlc_hw+0x180 and every access to the card are compared.
"""
import random
import struct

import ab
import hybrid

HW_BOARDFLAGS2, HW_SBCLK, HW_LED_MASK, HW_LED = 0x90, 0x187, 0x180, 0x198
L_SIZE, L_GPIO_VAL, L_PERIOD, L_TIMER, L_ARMED, L_ONESHOT = 0x328, 0x308, 0x310, 0x318, 0x320, 0x321
TIMER_BLOB = 0x061660           # sub_061660 in the object's .text


def entry(i):
    return 0x008 + 24 * i


def setup(r, rng):
    m, wlc = r.m, r.d.wlc
    hw = m.r64(wlc + 0x20)
    l = m.r64(hw + HW_LED)
    now = (m.os.now_us // 1000) & 0xffffffff
    for i in range(32):
        e = l + entry(i)
        if rng.randrange(3) == 0:
            on, off = rng.choice(((0, 0), (100, 100), (50, 950), (rng.randrange(0, 3000),
                                                                  rng.randrange(0, 3000))))
        else:
            on, off = 0, 0
        m.w32(e + 0x08, on)
        m.w32(e + 0x0c, off)
        m.w32(e + 0x10, (now - rng.randrange(0, 4000)) & 0xffffffff)
        m.w8(e + 0x04, rng.choice((1, 1, 0)))
        m.w8(e + 0x14, rng.randrange(2))
        m.w8(e + 0x15, rng.choice((0, 0, 1)))
    m.w32(l + L_GPIO_VAL, rng.choice((0, 7, rng.randrange(1 << 32))))
    m.w32(l + L_PERIOD, rng.choice((0, 10, 50, 100, rng.randrange(10, 1000))))
    m.w8(l + L_ARMED, rng.randrange(2))
    m.w8(l + L_ONESHOT, rng.randrange(2))
    m.w8(hw + HW_SBCLK, rng.choice((1, 1, 1, 0)))
    bf2 = m.r32(hw + HW_BOARDFLAGS2)
    m.w32(hw + HW_BOARDFLAGS2, bf2 | 0x800 if rng.randrange(3) == 0 else bf2 & ~0x800)
    m.os.now_us += rng.randrange(0, 2000) * 1000
    return hw, l


def call(r, rng, open_led):
    m, wlc = r.m, r.d.wlc
    hw, l = setup(r, rng)
    pick = rng.randrange(10)
    if pick < 4:
        fn = 'timer'
        m.call('bcm4360_bmac_led_timer' if open_led else hybrid.BASES['.text'] + TIMER_BLOB, wlc)
        res = None
    elif pick == 4:
        fn = 'wlc_bmac_led'
        mask = 1 << rng.randrange(8)
        res = m.call(fn, hw, mask, rng.choice((0, mask, 0xffffffff)), rng.randrange(2))
        res = None
    elif pick == 5:
        fn = 'wlc_bmac_led_blink'
        m.call(fn, hw, rng.randrange(32), rng.choice((0, 100, 500, rng.randrange(0x10000))),
               rng.choice((0, 100, 500, rng.randrange(0x10000))))
        res = None
    elif pick == 6:
        fn = 'wlc_bmac_led_blink_event'
        res = m.call(fn, hw, rng.randrange(2)) & 0xffffffff
    elif pick == 7:
        fn = 'wlc_bmac_blink_sync'
        m.call(fn, hw, rng.randrange(1 << 32))
        res = None
    elif pick == 8:
        fn = 'wlc_bmac_led_set'
        m.call(fn, hw, rng.randrange(32), rng.randrange(2))
        res = None
    else:
        fn = 'wlc_bmac_led_hw_mask_init'
        m.call(fn, hw, rng.randrange(1 << 32))
        res = None
    timers = sorted((t.ms, t.periodic, t.due, t.set) for t in m.os.timers.values()
                    if t.handle == m.r64(l + L_TIMER))
    return fn, res, m.read(l, L_SIZE), m.r32(hw + HW_LED_MASK), timers


def led_check(show=20, cases=3000, seed=5):
    core = hybrid.build(quiet=True)
    results = []
    for open_led in (False, True):
        r = hybrid.Run(core, off=() if open_led else ('mac_led',))
        r.session(['attach', 'mpc 0', 'up', '36/80'])
        n0 = len(r.s.chip.trace)
        rng = random.Random(seed)
        out = [call(r, rng, open_led) for _ in range(cases)]
        acc = ab.logical(r.s.chip.trace[n0:], r.s.chip.func_name)
        results.append((out, acc))
    (want, wacc), (have, hacc) = results
    diff = [i for i, (a, b) in enumerate(zip(want, have)) if a != b]
    for i in diff[:show]:
        a, b = want[i], have[i]
        print('case %d %s: result %r / %r' % (i, a[0], a[1], b[1]))
        if a[2] != b[2]:
            pos = [q for q in range(L_SIZE) if a[2][q] != b[2][q]]
            print('   LED state: %s' % ', '.join('%#x %02x/%02x' % (q, a[2][q], b[2][q])
                                              for q in pos[:10]))
        if a[3] != b[3]:
            print('   led mask %#x / %#x' % (a[3], b[3]))
        if a[4] != b[4]:
            print('   timer %r / %r' % (a[4], b[4]))
    bad = len(diff)
    if wacc != hacc:
        bad += 1
        ab.compare(wacc, hacc, show)
    calls = {}
    for c in want:
        calls[c[0]] = calls.get(c[0], 0) + 1
    print('led check: %d cases (%s), %d accesses of Broadcom\'s LED code, %d of the open: %s'
          % (len(want), ', '.join('%s %d' % kv for kv in sorted(calls.items())), len(wacc),
             len(hacc), 'the same' if not bad else 'DIFFERENT (%d cases)' % len(diff)))
    return bad
