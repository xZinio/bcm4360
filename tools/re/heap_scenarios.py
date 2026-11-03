#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Memory, not only accesses: run the same steps in the hybrid core twice, with
one layer (or group) on Broadcom's code and with it open, everything else the
same, and compare every live allocation of the emulated OS after each step
(hybrid.py heap --layer L [--steps ...]).

The access comparison of hybrid.py check sees what the code does to the card.
State that only lives in memory - a counter, a snapshot, a flag that a later
step reads - is invisible to it until it changes an access, and it may never
do that in the emulator (the model's counters are 0, for instance). Both runs
allocate in the same order, so the blocks are compared by address. Pointers
to code or static data of one implementation (callbacks, tables) differ by
nature: --ignore takes block:offset pairs found to be such.

    heap_scenarios.py --layer mac --steps real
"""
import argparse
import struct
import sys

import hybrid

PUB_CNT = 0xa0


def labels(r):
    """Names of the blocks the comparison talks about."""
    m, wlc = r.m, r.d.wlc
    out = {}
    if not wlc or wlc not in m.allocs:
        return out          # not attached, or detached (freed)
    try:
        return _labels(m, wlc, out)
    except Exception:
        return out


def _labels(m, wlc, out):
    pub = m.r64(wlc)
    out[wlc] = 'wlc'
    out[pub] = 'pub'
    hw = m.r64(wlc + 0x20)
    if hw:
        out[hw] = 'wlc_hw'
        for off, name in ((0x08, 'corestate'), (0xb0, 'btc'), (0xf0, 'band states'),
                          (0xd8, 'shim'), (0x198, 'led')):
            p = m.r64(hw + off)
            if p:
                out[p] = name
    for off, name in ((0x38, 'wlc->core'), (0x50, 'wlc band 2g'), (0x58, 'wlc band 5g'),
                      (0x550, 'stf')):
        p = m.r64(wlc + off)
        if p:
            out[p] = name
    cnt = m.r64(pub + PUB_CNT)
    if cnt:
        out[cnt] = 'pub->_cnt (wl_cnt_t)'
    return out


def snapshot(m):
    return {a: m.read(a, n) for a, n in sorted(m.allocs.items())}


# Static functions and data of Broadcom's object that have an open
# counterpart: a pointer to one in the Broadcom run and to the other in the
# open run is the same thing (offsets in .text from the specifications).
PAIRS = (('.text', 0x061268, 'bcm4360_bmac_wlintrsoff'),       # bmac-run R9
         ('.text', 0x061285, 'bcm4360_bmac_wlintrsrestore'),
         ('.text', 0x061660, 'bcm4360_bmac_led_timer'),         # bmac-run R4
         ('.data', 0x330, 'xmtfifo_sz_rev40'))                  # bmac-attach Data


def pairs(m):
    out = set()
    for section, off, name in PAIRS:
        hits = [x for x in m.blob.byname.get(name, [])]
        if hits:
            out.add((hybrid.BASES[section] + off, hits[0].addr))
    return out


def diff(a, b, names, ignore, show, same=()):
    lines = []
    for addr in sorted(set(a) | set(b)):
        p, q = a.get(addr), b.get(addr)
        name = names.get(addr, '')
        if p is None or q is None:
            lines.append('  block %#x %s: only in the %s run (size %#x)'
                         % (addr, name, 'Broadcom' if q is None else 'open', len(p or q)))
            continue
        if p == q:
            continue
        skip = set()
        for w in range(0, min(len(p), len(q)) - 7, 4):
            if (struct.unpack_from('<Q', p, w)[0], struct.unpack_from('<Q', q, w)[0]) in same:
                skip.update(range(w, w + 8))
        pos = [i for i in range(min(len(p), len(q))) if p[i] != q[i] and i not in skip
               and (addr, i) not in ignore and (name, i) not in ignore]
        if not pos:
            continue
        # group into words
        words = sorted({i & ~3 for i in pos})
        desc = ', '.join('+%#x %08x/%08x' % (w, struct.unpack_from('<I', p, w)[0],
                                             struct.unpack_from('<I', q, w)[0])
                         for w in words[:8] if w + 4 <= len(p))
        lines.append('  block %#x %s (size %#x): %d bytes: %s'
                     % (addr, name or '?', len(p), len(pos), desc))
        if len(lines) >= show:
            break
    return lines


def heap_check(layer, steps, show=12, ignore=(), on=()):
    core = hybrid.build(quiet=True)
    runs = [hybrid.Run(core, off=(layer,), on=on), hybrid.Run(core, on=on)]
    same = pairs(runs[1].m)
    first = None
    for k, step in enumerate(steps):
        for r in runs:
            r.session([step], stop_on_error=False)
        res = [r.results[-1][1] for r in runs]
        a, b = (snapshot(r.m) for r in runs)
        names = labels(runs[1])
        lines = diff(a, b, names, set(ignore), show, same)
        if res[0] != res[1]:
            lines.insert(0, '  results: Broadcom %r, open %r' % tuple(res))
        print('%3d %-14s %s' % (k, step, 'the same' if not lines else 'DIFFERENT'))
        for line in lines:
            print(line)
        if lines and first is None:
            first = k
    print('heap check, %s off/on, %d steps: %s' % (
        layer, len(steps), 'the same' if first is None else
        'DIFFERENT from step %d (%s)' % (first, steps[first])))
    return first is not None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--layer', default='mac')
    ap.add_argument('--steps', default='real')
    ap.add_argument('--on', default='')
    ap.add_argument('--show', type=int, default=12)
    ns = ap.parse_args()
    steps = (list(hybrid.REAL) if ns.steps == 'real' else
             list(hybrid.SESSION) if ns.steps == 'session' else ns.steps.split(','))
    on = [x for x in ns.on.split(',') if x]
    return 1 if heap_check(ns.layer, steps, ns.show, on=on) else 0


if __name__ == '__main__':
    sys.exit(main())
