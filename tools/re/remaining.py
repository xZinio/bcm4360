#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Which of Broadcom's code still runs in the hybrid core: hybrid.py's session
with coverage, the executed blocks of the object's .text attributed to their
functions and source files (modmap.txt).

    remaining.py [--on L,..] [--off L,..] [--top N]

Prints, per source file and for the N largest functions, the bytes of
Broadcom's code executed (each block once) and the share of all execution
counting repetitions (as `hybrid.py share` counts it). Function names and
sizes only: nothing of the code itself.
"""
import argparse
import collections
import os

import hybrid
import modmap
from blob import BASES, Blob

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--on', default='')
    ap.add_argument('--off', default='')
    ap.add_argument('--top', type=int, default=40)
    ns = ap.parse_args()
    on = [x for x in ns.on.split(',') if x]
    off = [x for x in ns.off.split(',') if x]

    core = hybrid.build(quiet=True)
    b = Blob(hybrid.BLOB)
    b.discover_functions()
    where = modmap.attribute(b, modmap.load_pins(os.path.join(HERE, 'modmap.txt')))
    text_end = BASES['.text'] + b.size['.text']

    r = hybrid.Run(core, off=off, on=on, coverage=True).session()
    per_func = collections.defaultdict(lambda: [0, 0])     # addr -> [bytes, bytes*count]
    total_closed = total_open = open_bytes = 0
    for a, size in r.m.blocks.items():
        n = r.counts.get(a, 0)
        if a >= text_end:
            open_bytes += size
        if a < text_end:
            f = b.func_at(a)
            key = f.addr if f is not None else a
            per_func[key][0] += size
            per_func[key][1] += size * n
            total_closed += size * n
        else:
            total_open += size * n
    total = max(total_closed + total_open, 1)

    per_file = collections.defaultdict(lambda: [0, 0, 0])
    for addr, (sz, dyn) in per_func.items():
        w = where.get(addr, '?')
        per_file[w][0] += sz
        per_file[w][1] += dyn
        per_file[w][2] += 1
    print('session with on=%s off=%s: Broadcom %.2f%% of all execution, %d bytes of its code '
          'ran (open code: %d bytes)'
          % (','.join(on) or '-', ','.join(off) or '-', 100.0 * total_closed / total,
             sum(v[0] for v in per_func.values()), open_bytes))
    print('\n%-22s %6s %8s %8s' % ('file', 'funcs', 'bytes', 'exec %'))
    for w, (sz, dyn, n) in sorted(per_file.items(), key=lambda kv: -kv[1][1]):
        print('%-22s %6d %8d %8.3f' % (w, n, sz, 100.0 * dyn / total))
    print('\n%-44s %-20s %7s %8s' % ('function', 'file', 'bytes', 'exec %'))
    for addr, (sz, dyn) in sorted(per_func.items(), key=lambda kv: -kv[1][1])[:ns.top]:
        f = b.func_at(addr)
        name = f.name if f is not None else '%#x' % (addr - BASES['.text'])
        print('%-44s %-20s %7d %8.3f' % (name, where.get(addr, '?'), sz, 100.0 * dyn / total))


if __name__ == '__main__':
    main()
