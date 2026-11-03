#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Black-box checks of specifications against Broadcom's object: the object runs
in the emulator with chosen inputs, its state or result is compared with a
model written from the specification. Nothing here looks at the object's code;
what is compared is what it does (results, the fields the specifications
name, register accesses).

    blackbox.py e3          the desense-aci engine (acphy-desense.md E3): the
                            interference record before and after every call of
                            phy_soak.py's interference session, against the
                            model below (with the corrections of 2026-11-03)
    blackbox.py restore     acphy-desense.md E1 step 8: which bits of PHY(0x19e)
                            the hwaci engine restores (the register reads
                            back random values inside the engine)
    blackbox.py tempsense   acphy-cal-rx.md, the conversion of
                            wlc_phy_tempsense_acphy: the constant added
                            before the division by 0x4000, bounded by search
"""
import argparse
import random
import struct
import sys

import ab
import phy_soak

# ------------------------------------------------------------------ e3

# the statistics histories E3 reads (acphy-api-watchdog.md Q5): array, index
HIST = {'H2': (0x3f6, 0x400), 'H3': (0x3fa, 0x404), 'H4': (0x414, 0x418), 'H5': (0x41c, 0x420)}
RECORD = 0x8a8          # pi_ac: the current interference record (0x50 bytes)
PI_AC = 0x138           # pi: pi_ac
RSSI_CAP = 0x671        # pi_ac: RSSI cap enabled

FIELDS = [('ofdm', 0x10, 'B'), ('bphy', 0x11, 'B'), ('on', 0x18, 'B'), ('rssi', 0x19, 'b'),
          ('nring', 0x1c, '4I'), ('n_lo', 0x2c, 'B'), ('n_hi', 0x2d, 'B'), ('n_dw', 0x2e, 'B'),
          ('wring', 0x30, '4I'), ('w_lo', 0x40, 'B'), ('w_hi', 0x41, 'B'), ('w_dw', 0x42, 'B'),
          ('idx', 0x44, 'B'), ('settle', 0x45, 'B')]


def _latest(h):
    a0, a1, i = h
    return (a0, a1)[i - 1 if i else 1]


def _top2(ring):
    s = sorted(ring, reverse=True)
    return (s[0] + s[1]) // 2


def _path(level, lo, hi, dwell, avg, trigger, low, top):
    """E3 steps 5/6 for one path, with the corrections: a lower sets the
    upper bound to the level lowered from; the dwell counter is reset on a
    change and then counted (1 after a change)."""
    change = False
    if avg >= trigger:
        n = level + 4
        if level < hi:
            n = max(level + 1, (hi + level) // 2)
        lo = level
        change = True
    else:
        thr = 12 if level - lo == 1 else (4 if avg < low else 8)
        if dwell > thr:
            n = max(0, min(level - 1, (lo + level) // 2)) if lo < level else max(0, level - 4)
            hi = level
            change = True
        else:
            n = level
    n = min(n, top)
    dwell = min((0 if change else dwell) + 1, 255)
    return n, lo, hi, dwell


def e3_model(rec, hist, cap, base):
    r = bytearray(rec)
    if r[0x45]:
        r[0x45] -= 1
        return bytes(r)
    i = r[0x44]
    struct.pack_into('<I', r, 0x1c + 4 * i, _latest(hist['H3']) + 2 * _latest(hist['H5']))
    struct.pack_into('<I', r, 0x30 + 4 * i, _latest(hist['H2']) + 2 * _latest(hist['H4']))
    r[0x44] = (i + 1) % 4
    an = _top2(struct.unpack_from('<4I', r, 0x1c))
    aw = _top2(struct.unpack_from('<4I', r, 0x30))
    if an < 301 and r[0x18] == 0 and aw < 601:
        return bytes(r)
    b, r[0x2c], r[0x2d], r[0x2e] = _path(r[0x11], r[0x2c], r[0x2d], r[0x2e], an, 301, 100, 24)
    o, r[0x40], r[0x41], r[0x42] = _path(r[0x10], r[0x40], r[0x41], r[0x42], aw, 601, 300, 48)
    if cap:
        rssi = struct.unpack_from('<b', r, 0x19)[0]
        b = min(b, max(0, rssi + 90))
        o = min(o, max(0, rssi + 85))
    changed = False
    if b != r[0x11]:
        r[0x11] = b
        struct.pack_into('<4I', r, 0x1c, 100, 100, 100, 100)
        changed = True
    if o != r[0x10]:
        r[0x10] = o
        struct.pack_into('<4I', r, 0x30, 300, 300, 300, 300)
        changed = True
    r[0x18] = 1 if bytes(r[0x10:0x18]) != base[:8] else 0
    if changed:
        r[0x45] = 1
    return bytes(r)


def _show(r):
    out = []
    for name, off, f in FIELDS:
        v = struct.unpack_from('<' + f, r, off)
        out.append('%s=%s' % (name, v if len(v) > 1 else v[0]))
    return ' '.join(out)


def e3(show=10):
    run = phy_soak.SoakRun(interference=True)
    m = run.m
    stats = {'calls': 0, 'bad': 0, 'changes': 0, 'rssi': set()}

    def entry(mm, pi, *rest):
        pi_ac = mm.r64(pi + PI_AC)
        recp = mm.r64(pi_ac + RECORD)
        if not recp:
            return
        before = mm.read(recp, 0x50)
        hist = {k: (mm.r16(pi + a), mm.r16(pi + a + 2), mm.r32(pi + i))
                for k, (a, i) in HIST.items()}
        cap, base = mm.r8(pi_ac + RSSI_CAP), mm.read(pi_ac + 0x65f, 9)

        def ret(mm2, result):
            if mm2.r64(pi_ac + RECORD) != recp:
                return
            after = mm2.read(recp, 0x50)
            stats['calls'] += 1
            stats['rssi'].add(struct.unpack_from('<b', before, 0x19)[0])
            stats['changes'] += after[0x10:0x12] != before[0x10:0x12]
            want = e3_model(before, hist, cap, base)
            if want != after:
                stats['bad'] += 1
                if stats['bad'] <= show:
                    print('t=%.1f s' % (mm2.os.now_us / 1e6))
                    print('  before %s\n  object %s\n  model  %s'
                          % (_show(before), _show(after), _show(want)))
        mm.at_return(ret)

    m.on_call('wlc_phy_desense_aci_engine_acphy', entry, 1)
    for s in phy_soak.ISTEPS:
        run.step(s)
    print('E3: %d calls, %d with a level change, RSSI inputs %s: %d differ from the model'
          % (stats['calls'], stats['changes'], sorted(stats['rssi']), stats['bad']))
    return stats['bad']


# ------------------------------------------------------------- restore

def restore():
    """PHY(0x19e) reads random values while the hwaci engine runs; the
    engine's save, set and restore of the register are printed with the
    restored bits."""
    run = phy_soak.SoakRun(interference=True)
    m, chip = run.m, run.s.chip
    inside = [False]

    def entry(mm, *a):
        inside[0] = True
        mm.at_return(lambda mm2, r: inside.__setitem__(0, False))
    m.on_call('wlc_phy_hwaci_engine_acphy', entry, 1)
    rng = random.Random(5)
    read0 = chip.phy_read

    def read(addr, value):
        v = read0(addr, value)
        return rng.randrange(0x10000) if addr == 0x19e and inside[0] else v
    chip.phy_read = read
    n0 = len(chip.trace)
    for s in phy_soak.ISTEPS[:phy_soak.ISTEPS.index('wait 40') + 1]:
        run.step(s)
    acc = [a for a in ab.logical(chip.trace[n0:], chip.func_name)
           if a[1] == 'phy' and a[2] == 0x19e]
    # the pattern of step 8: R (save), R W (set bit 1), ... R W (restore)
    bad = found = 0
    for k in range(len(acc) - 4):
        if [x[0] for x in acc[k:k + 5]] != ['R', 'R', 'W', 'R', 'W']:
            continue
        saved, cur = acc[k][4], acc[k + 3][4]
        if acc[k + 2][4] != acc[k + 1][4] | 2:
            continue
        found += 1
        got = acc[k + 4][4]
        masked = (cur & ~2) | (saved & 2)
        print('saved %04x, read before the restore %04x, written %04x: %s'
              % (saved, cur, got, 'bit 1 restored' if got == masked else
                 'whole word' if got == saved else 'OTHER'))
        bad += got != masked
    print('restore: %d save/restore pairs, %d not "only bit 1"' % (found, bad))
    return bad if found else 1


# ----------------------------------------------------------- tempsense

def tempsense():
    """Bound the constant K of degrees = (sum of the per-core terms + K) /
    0x4000 by inputs that put K + terms on a degree boundary (two cores, the
    other phases 0), as a binary search on the object's result."""
    run = phy_soak.SoakRun()
    for s in ['attach', 'mpc 0', 'up', '11']:
        run.step(s)
    m, chip = run.m, run.s.chip
    pi = run.pi()
    plan, reads = [], []
    read0 = chip.phy_read

    def read(addr, value):
        v = read0(addr, value)
        if addr == phy_soak.AUX_ADC:
            t = plan[len(reads)] if len(reads) < len(plan) else 0
            reads.append(t)
            return (t & 0x3ff) << 2
        return v
    chip.phy_read = read

    def tdiv(a, b):
        q = abs(a) // abs(b)
        return q if (a >= 0) == (b >= 0) else -q

    def terms(a, b):
        return (tdiv(tdiv(a * 4383 * 256, 527), 2) +
                tdiv(tdiv(b * 4383 * 256, 521), 2))

    def call(a, b):
        v = [0] * 64
        for k in range(8):
            v[8 + k] = a            # core 0, phase 1
            v[40 + k] = b           # core 1, phase 1
        plan[:] = v
        del reads[:]
        r = m.call('wlc_phy_tempsense_acphy', pi) & 0xffff
        return r - 0x10000 if r >= 0x8000 else r

    offset = m.r8(pi + 0xc35)
    offset = offset - 256 if offset > 127 else offset
    lo, hi = 1890000, 1910000
    for _ in range(40):
        if hi - lo <= 1:
            break
        mid = (lo + hi) // 2
        # find inputs whose boundary lies nearest to mid
        best = None
        for a in range(-150, 151):
            for b in range(-150, 151):
                x = terms(a, b)
                k = (mid + x) // 16384
                t = 16384 * k - x
                if lo < t < hi and (best is None or abs(t - mid) < abs(best[0] - mid)):
                    best = (t, a, b, k)
        if best is None:
            break
        t, a, b, k = best
        if call(a, b) - offset >= k:
            lo = t
        else:
            hi = t
    print('tempsense: K in [%d, %d) = [%#x, %#x)' % (lo, hi, lo, hi))
    return 0 if hi - lo == 1 and lo == 0x1d0614 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('what', choices=('e3', 'restore', 'tempsense'))
    ns = ap.parse_args()
    return 1 if {'e3': e3, 'restore': restore, 'tempsense': tempsense}[ns.what]() else 0


if __name__ == '__main__':
    sys.exit(main())
