#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
A/B check of the channel statistics module (docs/re/spec/cca-stf.md part 1,
open/lib/cca.c, hybrid/cca.c, switch open_cca).

The object alone and the hybrid core are brought to the same state (attach,
up: the module is attached and registered), then the module is driven on
both the same way: its watchdog, down function and iovar handler through the
module table (they have no symbols), cca_stats_upd, cca_stats_tsf_upd,
cca_query_stats and wlc_cca_get_chanspec directly, with the microcode's
counters in shared memory set by hand, the clock advanced, the current
chanspec and the power-save flag changed, the sample pool exhausted, the
0xffff marker hit, the module detached and attached again. The state itself
is the open code's own, so what is compared is what can be observed: every
result, every output buffer, the whole module table, and at the end every
access to the card.

    python hybrid.py cca [--off cca] [--show N]
"""
import random
import zlib

import ab
import hybrid

WLC_PUB, WLC_HW, WLC_BSSCFG, WLC_CHANSPEC, WLC_CCA, WLC_MODULECB = 0x0, 0x20, 0x2f8, 0x518, 0x628, 0x358
PUB_COREREV = 0x14
SHM_B = 0x768                                   # cca_stats_read's counters (bmac-run R5)
TABLE = [0x1000 + c for c in range(1, 15)] + [0xd000 + c for c in (
    36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140,
    149, 153, 157, 161, 165)]
OTHER = (0xd090, 0x100f, 0xd026, 0xd826, 0xe02a, 0xe12a, 0x1803, 0x2000, 0xffff, 0)


class Funcs:
    def __init__(self, m):
        self.m = m

    def __getattr__(self, name):
        return lambda *a: self.m.call(name, *a)


def s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def walk(r, f, rounds=1500):
    steps = [(s, r.step(s)) for s in ('attach', 'mpc 0', 'up')]
    m, wlc = r.m, r.d.wlc
    g = random.Random(4360)
    out = []
    hw = m.r64(wlc + WLC_HW)
    pub = m.r64(wlc + WLC_PUB)
    table = m.r64(wlc + WLC_MODULECB)
    pm = m.r64(m.r64(wlc + WLC_BSSCFG) + 0x338) + 8
    qbuf, abuf, pbuf = m.buf(0x800), m.buf(0x1800), m.buf(64)
    name = m.cstr('cca_get_stats')
    ctr = [g.randint(0, 0xffffffff) for _ in range(6)] + [g.randint(0, 0xffff) for _ in range(2)]

    def slot():
        for i in range(64):
            s = table + i * 0x50
            if m.string(s, 32) == 'cca_stats':
                return s
        return None

    def mod():
        s = slot()
        return None if s is None else tuple(m.r64(s + o) for o in (0x20, 0x28, 0x30, 0x38, 0x40, 0x48))

    def rec(label, rc, data=b''):
        out.append((label, rc, zlib.crc32(data), data[:24].hex()))

    def table_rec(label):
        t = m.read(table, 64 * 0x50)
        s = slot()
        # pointers of the module's own slot differ between the sides; compare the rest
        if s is not None:
            k = s - table
            t = t[:k] + m.read(s, 0x20) + t[k + 0x50:]
        rec(label + ': module table', s is not None and (s - table) // 0x50, t)

    def write_ctr():
        for k in range(6):
            a = SHM_B + 4 * k
            m.call('wlc_bmac_write_shm', hw, a, ctr[k] & 0xffff)
            m.call('wlc_bmac_write_shm', hw, a + 2, ctr[k] >> 16)
        m.call('wlc_bmac_write_shm', hw, 0x10e, ctr[6])
        m.call('wlc_bmac_write_shm', hw, 0x10c, ctr[7])

    def bump(big=False):
        for k in range(6):
            ctr[k] = (ctr[k] + (g.randint(0, 0xffffffff) if big else g.randint(0, 50000))) & 0xffffffff
        ctr[6] = (ctr[6] + g.randint(0, 3000)) & 0xffff
        ctr[7] = (ctr[7] + g.randint(0, 3000)) & 0xffff
        write_ctr()

    def advance(us):
        m.os.advance(us)
        if m.hw is not None:
            m.hw.tick(m.os.now_us)

    def set_cs(cs):
        m.w16(wlc + WLC_CHANSPEC, cs)

    def watchdog(label='watchdog'):
        md = mod()
        rc = s32(m.call(md[2], md[1])) if md else None
        rec(label, rc)

    def down():
        md = mod()
        rec('down', s32(m.call(md[5], md[1])) if md else None)

    def query(cs, n, size):
        m.write(qbuf, b'\xee' * 0x800)
        rc = s32(f.cca_query_stats(wlc, cs, n & 0xffffffff, qbuf, size))
        rec('query %#x %d %#x' % (cs, n, size), rc, m.read(qbuf, 0x800))

    def iovar(cs, n, plen, length, action=0):
        md = mod()
        m.write(abuf, b'\xee' * 0x1800)
        m.write(pbuf, bytes([cs & 0xff, cs >> 8, n & 0xff]) + bytes(61))
        rc = s32(m.call(md[3], md[1], md[0], action, name, pbuf, plen, abuf, length,
                        length, 0)) if md else None
        rec('iovar %#x n %d plen %d len %d act %d' % (cs, n, plen, length, action), rc,
            m.read(abuf, 0x1800))

    def snapshot(label):
        for cs in TABLE + list(OTHER[:3]):
            query(cs, 60, 0x800)
        for i in range(38):
            rec('%s: chanspec %d' % (label, i), f.wlc_cca_get_chanspec(wlc, i) & 0xffff)

    table_rec('after up')
    write_ctr()
    snapshot('start')
    cur = TABLE[20]
    for k in range(rounds):
        x = g.random()
        if x < 0.25:
            bump(big=g.random() < 0.1)
            advance(g.randint(1, 2000000))
            if g.random() < 0.3:
                cur = g.choice(TABLE + list(OTHER))
                set_cs(cur)
            watchdog()
        elif x < 0.45:
            bump()
            advance(g.randint(0, 300000))
            rec('upd', f.cca_stats_upd(wlc, g.choice((0, 1, 1, 2, -1))) and None)
        elif x < 0.5:
            advance(g.randint(0, 100000))
            f.cca_stats_tsf_upd(wlc)
            rec('tsf_upd', None)
        elif x < 0.55:
            m.w8(pm, g.choice((0, 1, 0x80)))
            rec('pm', None)
        elif x < 0.7:
            query(g.choice(TABLE + list(OTHER)), g.choice((0, 1, 2, 5, 59, 60, 61, 100, -1)),
                  g.choice((0, 3, 4, 31, 32, 59, 60, 0x100, 0x7ff, 0x800)))
        elif x < 0.85:
            cs = g.choice(TABLE + list(OTHER))
            n = g.choice((0, 1, 2, 3, 10, 60, 61, 255))
            iovar(cs, n, g.choice((24, 23, 0, 3, 4, 64)), g.choice((24, 23, 4 + 20 * n, 0x1800)),
                  g.choice((0, 0, 0, 1)))
        elif x < 0.88:
            # one tick on many channels: fills the pool until it runs out
            for cs in TABLE:
                set_cs(cs)
                bump()
                f.cca_stats_upd(wlc, 1)
            set_cs(cur)
            rec('upd on every channel', None)
        elif x < 0.9:
            # a fresh sample whose busy-own sum is exactly the free marker
            bump()
            advance(1000000)
            f.cca_stats_upd(wlc, 0)
            ctr[0] = (ctr[0] + 0x8000) & 0xffffffff
            ctr[1] = (ctr[1] + 0x7fff) & 0xffffffff
            write_ctr()
            watchdog('watchdog, busy-own 0xffff')
        elif x < 0.91:
            down()
        else:
            advance(g.randint(0, 1000))
            i = g.randint(0, 37)
            rec('get_chanspec %d' % i, f.wlc_cca_get_chanspec(wlc, i) & 0xffff)
        if k % 300 == 299:
            snapshot('round %d' % k)
    # no module state: every entry point must cope
    cca = m.r64(wlc + WLC_CCA)
    m.w64(wlc + WLC_CCA, 0)
    f.cca_stats_upd(wlc, 1)
    f.cca_stats_tsf_upd(wlc)
    query(TABLE[0], 10, 0x800)
    rec('no state: get_chanspec', f.wlc_cca_get_chanspec(wlc, 3) & 0xffff)
    m.w64(wlc + WLC_CCA, cca)
    # detach and attach again
    f.wlc_cca_detach(cca)
    table_rec('after detach')
    rev = m.r32(pub + PUB_COREREV)
    m.w32(pub + PUB_COREREV, 14)
    rec('attach on core rev 14', f.wlc_cca_attach(wlc))
    table_rec('after attach on rev 14')
    m.w32(pub + PUB_COREREV, rev)
    cca2 = f.wlc_cca_attach(wlc)
    rec('attach again', cca2 != 0)
    m.w64(wlc + WLC_CCA, cca2)
    table_rec('after attach again')
    snapshot('fresh')
    for k in range(80):
        bump()
        advance(1000000)
        set_cs(TABLE[k % 38])
        watchdog()
    snapshot('fresh, 80 ticks')
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


def cca_check(off=(), show=20, on=()):
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
    print('cca check, switched on: %s, off: %s: %d calls, %d accesses: %s' % (
        ', '.join(on) or '-', ', '.join(off) or '-', len(ra), len(ta),
        'the same' if not bad else 'DIFFERENT'))
    return bad
