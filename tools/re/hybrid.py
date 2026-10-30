#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
The hybrid build in the emulator (hybrid/README.md).

bcm4360.ko links Broadcom's core together with open code that takes over
functions of it.  This tool makes the same core for the emulator, the way the
Makefile does - scripts/mkhybrid.sh on the object, the objects of
hybrid/objects.mk (compiled with zig), `ld -r` - and runs whole sessions on it
and on Broadcom's object alone.  Every access to the card is recorded; the two
traces have to be the same.

    hybrid.py build                    make re-out/hybrid/core.o
    hybrid.py check [--off LAYER,..]   the session on both cores, compared
    hybrid.py share                    how much of the code that runs is Broadcom's
    hybrid.py selftest                 a broken open function must be found

--off switches layers of the hybrid core off (their switches, hybrid/hybrid.h),
so that the fallback to Broadcom's own functions is tested too.

The session: attach, up, channel changes in both bands and all bandwidths,
periodic work, down, up again, down, detach.
"""
import argparse
import collections
import os
import re
import subprocess
import sys

import ab
from blob import BASES, Blob
from emu import EmuError

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = ab.ROOT
OUT = os.path.join(ROOT, 're-out', 'hybrid')
BLOB = os.path.join(ROOT, 'lib', 'wlc_hybrid.o_shipped')
CORE = os.path.join(OUT, 'core.o')
LAYERS = ('pmu',)                   # the switches bcm4360_open_<layer>

CHANNELS = ['1', '6', '11', '36', '36/40', '36/80', '149/80', '157/40', '165']
SESSION = (['attach', 'mpc 0', 'up'] + CHANNELS +
           ['watchdog', 'down', 'up', '36/80', 'watchdog', 'down', 'detach'])

# accesses that only move the windows of BAR0 (PCI configuration space): the
# object moves them to reach ChipCommon, the open code uses the fixed window
WINDOW_REGS = {0x70, 0x80}


def objects():
    """The objects of the hybrid build: hybrid/objects.mk."""
    text = open(os.path.join(ROOT, 'hybrid', 'objects.mk')).read()
    m = re.search(r'hybrid-objs\s*:=((?:.*\\\n)*.*)', text)
    return [o for o in m.group(1).replace('\\\n', ' ').split() if o.endswith('.o')]


def build(quiet=False):
    """Make the hybrid core for the emulator; returns its path."""
    zig = ab.find_zig()
    os.makedirs(os.path.join(OUT, 'obj'), exist_ok=True)
    objs = []
    for o in objects():
        src = os.path.join(ROOT, o[:-2] + '.c')
        out = os.path.join(OUT, 'obj', o.replace('/', '_'))
        r = subprocess.run([zig, 'cc'] + ab.CFLAGS +
                           ['-DBCM4360_HYBRID=1', '-I', os.path.join(ROOT, 'open', 'include'),
                            '-o', out, src], capture_output=True, text=True)
        if r.returncode != 0 or (r.stderr.strip() and not quiet):
            sys.stderr.write(r.stderr)
        if r.returncode != 0:
            raise SystemExit('does not compile: ' + o[:-2] + '.c')
        objs.append(out)
    weak = os.path.join(OUT, 'wlc_hybrid_open.o')
    r = subprocess.run(['sh', os.path.join(ROOT, 'scripts', 'mkhybrid.sh'), BLOB, weak,
                        os.path.join(ROOT, 'hybrid', 'takeover.txt')],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(r.stderr)
    if not quiet:
        sys.stdout.write(r.stdout)
    r = subprocess.run(['ld', '-r', '-T', os.path.join(HERE, 'hybrid.lds'), '-o', CORE, weak]
                       + objs, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(r.stderr)
    return CORE


class Run:
    """A session on one build of the core."""

    def __init__(self, path=None, off=(), coverage=False, patch=None):
        self.s = ab.Session(path=path)
        self.s.chip.radio_done = True
        self.m = self.s.m
        self.d = self.s.d
        if patch:
            patch(self.m)
        for layer in off:
            sym = 'bcm4360_open_' + layer
            hits = [x for x in self.m.blob.byname.get(sym, []) if x.kind == 'O']
            if not hits:
                raise SystemExit('%s: no such switch in this core' % sym)
            self.m.w8(hits[0].addr, 0)
        if coverage:
            self.m.record_coverage()
        self.results = []

    def step(self, what):
        from run_chan import chanspec
        d, m = self.d, self.m
        if what == 'attach':
            r = 0 if d.attach() else d.err or -1
        elif what == 'mpc 0':
            r = d.iovar_setint('mpc', 0)
        elif what == 'up':
            r = d.up()
        elif what == 'down':
            r = m.call('wlc_down', d.wlc) & 0xffffffff
        elif what == 'watchdog':
            m.os.run_timers(m, m.os.now_us // 1000 + 2500)
            r = 0
        elif what == 'detach':
            r = m.call('wlc_detach', d.wlc) & 0xffffffff
        else:
            r = d.iovar_setint('chanspec', chanspec(m, what))
        self.results.append((what, r))
        return r

    def session(self, steps=SESSION):
        for what in steps:
            self.step(what)
        return self

    def logical(self):
        """The trace as tuples, without the moves of the BAR0 windows."""
        out = []
        for a in ab.logical(self.s.chip.trace, self.s.chip.func_name):
            if a[1] == 'pcicfg' and a[2] in WINDOW_REGS:
                continue
            out.append(a)
        return out


def check(off=(), show=20, patch=None):
    core = build(quiet=True)
    ref = Run().session()
    got = Run(core, off=off, patch=patch).session()
    want, have = ref.logical(), got.logical()
    bad = 0
    if ref.results != got.results:
        bad += 1
        print('results of the steps differ:')
        for (w, a), (_, b) in zip(ref.results, got.results):
            if a != b:
                print('  %-10s object %s, hybrid %s' % (w, a, b))
    if want != have:
        bad += 1
        ab.compare(want, have, show)
    print('session (%d steps), switches off: %s: %d accesses of the object, %d of the hybrid '
          'core, %s' % (len(SESSION), ', '.join(off) or 'none', len(want), len(have),
                        'the same' if not bad else 'DIFFERENT'))
    return bad


def share():
    """Bytes of code executed in the session: Broadcom's, and the open code's."""
    core = build(quiet=True)
    text_end = BASES['.text'] + Blob(BLOB).size['.text']
    out = {}
    for name, path, off in (('object alone', None, ()), ('hybrid', core, ())):
        r = Run(path, off=off, coverage=True).session()
        closed = opened = 0
        for a, size in r.m.blocks.items():
            if a < text_end:
                closed += size
            else:
                opened += size
        out[name] = (closed, opened)
    base = out['object alone'][0]
    for name, (closed, opened) in out.items():
        print('%-14s Broadcom code executed: %7d bytes (%5.1f%%), open code: %6d bytes'
              % (name, closed, 100.0 * closed / base, opened))
    return out


def selftest():
    """The check must see it when an open function does not do its job: here
    the open radio supply switch (bcm4360_pmu_rfldo) returns at once."""
    def broken(m):
        f = [x for x in m.blob.byname['bcm4360_pmu_rfldo'] if x.kind == 'F'][0]
        m.write(f.addr, b'\xc3')                  # ret
    print('a hybrid core whose bcm4360_pmu_rfldo does nothing:')
    bad = check(show=3, patch=broken)
    print('selftest %s' % ('ok' if bad else 'FAILED: the difference was not found'))
    return 0 if bad else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=('build', 'check', 'share', 'selftest'))
    ap.add_argument('--off', default='', help='layers to switch off: ' + ', '.join(LAYERS))
    ap.add_argument('--show', type=int, default=20)
    ns = ap.parse_args()
    if ns.cmd == 'build':
        print(build())
        return 0
    if ns.cmd == 'share':
        share()
        return 0
    if ns.cmd == 'selftest':
        return selftest()
    off = [x for x in ns.off.split(',') if x]
    return 1 if check(off, ns.show) else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except EmuError as e:
        sys.exit('emulation failed: %s' % e)
