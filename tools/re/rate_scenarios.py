#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
A/B check of the rate set helpers (docs/re/spec/rate.md, open/lib/rate.c,
hybrid/rate.c, switch open_rate).

The object alone and the hybrid core are brought to the same state (attach,
up: the driver builds its rate sets through the helpers), then the ten
functions are called directly on both with the same arguments: the default
rate sets, random sets (counts 0..16, any rate byte, basic bits, MCS and VHT
maps, garbage in the destination), every kind of filter, PHY type, band,
bandwidth and stream count, rspecs of every kind, byte arguments with bits
above the low byte. Compared after each call: the result and the 0x28 bytes
of every set involved; at the end every access to the card.

    python hybrid.py rate [--off rate] [--show N] [--model]
"""
import random

import ab
import hybrid

RS = 0x28
DEFAULTS = ('cck_rates', 'ofdm_rates', 'cck_ofdm_rates', 'ofdm_mimo_rates',
            'ofdm_40bw_mimo_rates', 'cck_ofdm_mimo_rates', 'cck_ofdm_40bw_mimo_rates')
VALID = (2, 4, 11, 12, 18, 22, 24, 36, 48, 72, 96, 108)


class Funcs:
    def __init__(self, m):
        self.m = m

    def __getattr__(self, name):
        return lambda *a: self.m.call(name, *a)


def random_set(g, any_code=True, max_code=127):
    n = g.randint(0, 16)
    rates = []
    for _ in range(16):
        k = g.random()
        if k < 0.6:
            c = g.choice(VALID)
        elif any_code:
            c = g.randint(0, max_code)
        else:
            c = g.choice(VALID)
        rates.append(c | (0x80 if g.random() < 0.4 else 0))
    mcs = bytes(g.choice((0, 0xff, g.randint(0, 255))) for _ in range(16))
    vht = g.choice((0xffff, 0x002a, 0xfffa, 0xffea, g.randint(0, 0xffff)))
    return (n.to_bytes(4, 'little') + bytes(rates) + bytes([g.randint(0, 255)]) + mcs +
            bytes([g.randint(0, 255)]) + vht.to_bytes(2, 'little'))


def walk(r, f, n_random=400):
    steps = [(s, r.step(s)) for s in ('attach', 'mpc 0', 'up')]
    m = r.m
    g = random.Random(4360)
    out = []
    a, b, c = m.buf(RS), m.buf(RS), m.buf(RS)
    defaults = []
    for name in DEFAULTS:
        hits = [x for x in m.blob.byname[name] if x.kind == 'O']
        defaults.append(m.read(hits[0].addr, RS))

    def rec(label, rc, *sets):
        out.append((label, rc) + tuple(m.read(s, RS).hex() for s in sets))

    def sets(n, **kw):
        return defaults + [random_set(g, **kw) for _ in range(n)]

    # one-set helpers
    for s in sets(n_random):
        for fn, args in (('wlc_rateset_mcs_upd', (0, 1, 2, 3, 4, 5, 255, 0x101, 0x203)),
                         ('wlc_rateset_mcs_build', (0, 1, 3, 4, 0x102)),
                         ('wlc_rateset_vhtmcs_upd', (0, 1, 2, 3, 7, 8, 9, 255, 0x103)),
                         ('wlc_rateset_bw_mcs_filter', (20, 40, 80, 0, 0x128, 0x114))):
            for v in args:
                m.write(a, s)
                getattr(f, fn)(a, v)
                rec('%s %#x' % (fn, v), None, a)
        m.write(a, s)
        f.wlc_rateset_ofdm_fixup(a)
        rec('ofdm_fixup', None, a)
        m.write(a, s)
        m.write(b, random_set(g))
        f.wlc_rateset_copy(a, b)
        rec('copy', None, a, b)
    # rspecs
    for kind in range(4):
        for low in range(256):
            for high in (0, 0x00700000, 0x80c00000 ^ (low << 12)):
                rspec = (kind << 24) | high | low
                rec('reference_rate %#x' % rspec, f.wlc_rate_rspec_reference_rate(rspec) &
                    0xffffffff)
    # filter
    for s in sets(n_random):
        for _ in range(6):
            basic = g.choice((0, 1, 0x100, 0x101))
            rates = g.choice((0, 1, 2, 3, 0x101, 0x102))
            xmask = g.choice((0xff, 0x7f, 0x1ff, 0xffffffff, 0, 0x80))
            flags = g.choice((0, 1, 2, 3, 0x101, 0x103))
            m.write(a, s)
            m.write(b, random_set(g))
            f.wlc_rateset_filter(a, b, basic, rates, xmask, flags)
            rec('filter %#x %#x %#x %#x' % (basic, rates, xmask, flags), None, a, b)
    # intersection with the hardware's set (its codes at most 108: rate.md)
    for s in sets(n_random):
        for _ in range(3):
            hw = random_set(g, max_code=108) if g.random() < 0.7 else g.choice(defaults)
            check = g.choice((0, 1, 0x100, 0x101))
            m.write(a, s)
            m.write(c, hw)
            rc = f.wlc_rate_hwrs_filter_sort_validate(a, c, check, g.randint(0, 4)) & 0xff
            rec('hwrs %#x' % check, rc, a, c)
    # default sets
    for phy in list(range(13)) + [0x10b, 0xffffffff]:
        for band in (0, 1, 2, 3):
            for bw in (20, 40, 80, 0x114):
                for _ in range(3):
                    hw_null = g.random() < 0.4
                    hw = random_set(g, max_code=108) if g.random() < 0.6 else g.choice(defaults)
                    args = (phy, band, g.choice((0, 1, 0x100)), g.choice((0xff, 0x7f, 0x1ff)),
                            g.choice((0, 1, 2, 3, 0x101)), bw, g.choice((0, 1, 2, 3, 4, 0x103)))
                    m.write(b, random_set(g))
                    m.write(c, hw)
                    f.wlc_rateset_default(b, 0 if hw_null else c, *args)
                    rec('default hw %s %r' % ('NULL' if hw_null else 'set', args), None, b, c)
    return out, steps


def compare(ref, got, show, names):
    diffs = 0
    for x, y in zip(ref, got):
        if x != y:
            diffs += 1
            if diffs <= show:
                print('  %s:\n    %s %r\n    %s %r' % (x[0], names[0], x[1:], names[1], y[1:]))
    if len(ref) != len(got):
        diffs += 1
        print('  different number of calls: %d, %d' % (len(ref), len(got)))
    return diffs


def rate_check(off=(), show=20, on=(), model=False):
    if model:
        return model_check(show)
    core = hybrid.build(quiet=True)
    sides = []
    for path, o, n in ((None, (), ()), (core, off, on)):
        r = hybrid.Run(path, off=o, on=n)
        out, steps = walk(r, Funcs(r.m))
        sides.append((out, steps, r.logical()))
    (ra, sa, ta), (rb, sb, tb) = sides
    bad = 0
    d = compare(ra, rb, show, ('object', 'hybrid'))
    if d:
        print('%d of %d calls differ' % (d, len(ra)))
        bad += 1
    if sa != sb:
        bad += 1
        print('  steps: object %s, hybrid %s' % (sa, sb))
    if ta != tb:
        bad += 1
        ab.compare(ta, tb, show)
    print('rate check, switched on: %s, off: %s: %d calls, %d accesses: %s' % (
        ', '.join(on) or '-', ', '.join(off) or '-', len(ra), len(ta),
        'the same' if not bad else 'DIFFERENT'))
    return bad


def model_check(show):
    """The coordinator's check of the specification against the object: a
    Python model written from rate.md (re-out/rate_model.py, not part of the
    repository; implementers do not use it)."""
    import importlib.util
    import os
    path = os.path.join(ab.ROOT, 're-out', 'rate_model.py')
    spec = importlib.util.spec_from_file_location('rate_model', path)
    model = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(model)
    sides = []
    for use_model in (False, True):
        r = hybrid.Run(None)
        out, steps = walk(r, model.Funcs(r.m) if use_model else Funcs(r.m))
        sides.append((out, steps, r.logical()))
    (ra, sa, ta), (rb, sb, tb) = sides
    d = compare(ra, rb, show, ('object', 'model'))
    d += (sa != sb) + (ta != tb)
    print('model check: %d calls, %d accesses: %s' % (
        len(ra), len(ta), '%d differences' % d if d else 'the same'))
    return d
