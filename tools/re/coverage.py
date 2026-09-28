#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Which code of the object runs for a BCM4360?  Runs attach, up and a set of
channel changes in the emulator and records the executed basic blocks.

    coverage.py [--srom FILE] [--out FILE] [CHANSPEC ...]

Writes one line per function of the object: offset, size, bytes executed,
source file, name (default re-out/coverage.tsv) and prints the sum per source
file.  Code that did not run is not proven dead - other inputs (scan, join,
traffic, other boards) reach more - but code that ran is proven relevant.
"""
import argparse
import collections
import os

from blob import BASES
from emu import EmuError
from modmap import attribute, load_pins
from run_attach import DEFAULT_SROM, Driver
from run_chan import chanspec

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CHANNELS = ['1', '6', '11', '13', '36', '36/40', '36/80', '52/80', '100/80', '149/80',
                    '165']


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('chanspecs', nargs='*', default=DEFAULT_CHANNELS)
    ap.add_argument('--srom', default=DEFAULT_SROM)
    ap.add_argument('--out', default=os.path.join(HERE, '..', '..', 're-out', 'coverage.tsv'))
    ns = ap.parse_args()

    d = Driver(ns.srom, echo=False)
    m = d.m
    d.chip.trace_on = False
    m.record_coverage()
    steps = [('attach', d.attach), ('mpc 0', lambda: d.iovar_setint('mpc', 0)), ('up', d.up)]
    for text in ns.chanspecs:
        steps.append(('chanspec ' + text,
                      lambda t=text: d.iovar_setint('chanspec', chanspec(m, t))))
    steps.append(('watchdog x3', lambda: m.os.run_timers(m, m.os.now_us // 1000 + 3500)))
    steps.append(('down', lambda: m.call('wlc_down', d.wlc)))
    for name, fn in steps:
        try:
            r = fn()
            print('%-16s -> %s' % (name, r if isinstance(r, int) and r < 1 << 32 else hex(r)))
        except EmuError as e:
            print('%-16s FAILED: %s' % (name, str(e).splitlines()[0]))

    b = m.blob
    b.discover_functions()
    where = attribute(b, load_pins(os.path.join(HERE, 'modmap.txt')))
    cov = m.covered_functions()
    tb = BASES['.text']
    per = collections.defaultdict(lambda: [0, 0, 0, 0])
    with open(ns.out, 'w') as o:
        for f in b.funcs:
            n = cov.get(f, 0)
            o.write('%06x\t%d\t%d\t%s\t%s\n' % (f.addr - tb, f.size, n, where[f.addr], f.name))
            p = per[where[f.addr]]
            p[0] += 1
            p[1] += f.size
            if n:
                p[2] += 1
                p[3] += n
    print('\n%-24s %6s %8s %8s %8s' % ('source file', 'funcs', 'reached', 'bytes', 'executed'))
    tot = [0, 0, 0, 0]
    for mod, p in per.items():
        if p[2]:
            print('%-24s %6d %8d %8d %8d' % (mod, p[0], p[2], p[1], p[3]))
        for i in range(4):
            tot[i] += p[i]
    print('%-24s %6d %8d %8d %8d' % ('total', tot[0], tot[2], tot[1], tot[3]))


if __name__ == '__main__':
    main()
