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
    hybrid.py check [--on L,..] [--off L,..]   the session on both cores, compared
    hybrid.py share                    how much of the code that runs is Broadcom's
    hybrid.py selftest                 a broken open function must be found

--on and --off set the switches of layers of the hybrid core (bcm4360_open_<layer>,
hybrid/hybrid.h), whatever their defaults; switched off, a layer falls back to
Broadcom's own functions, which is tested too. The layers: pmu (default on),
phy (default off; the PHY layer of hybrid/objects.mk is linked for the emulator
even while the module leaves it out, so that an open PHY function that is still
missing stops the session with its name).

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
# HYBRID_OUT: a build directory of one's own (several people testing at once)
OUT = os.environ.get('HYBRID_OUT') or os.path.join(ROOT, 're-out', 'hybrid')
BLOB = os.path.join(ROOT, 'lib', 'wlc_hybrid.o_shipped')
CORE = os.path.join(OUT, 'core.o')
LAYERS = ('pmu', 'phy', 'access', 'mac', 'utils')   # the switches bcm4360_open_<layer>

CHANNELS = ['1', '6', '11', '36', '36/40', '36/80', '149/80', '157/40', '165']
SESSION = (['attach', 'mpc 0', 'up'] + CHANNELS +
           ['watchdog', 'down', 'up', '36/80', 'watchdog', 'down', 'detach'])

# accesses that only move the windows of BAR0 (PCI configuration space): the
# object moves them to reach ChipCommon, the open code uses the fixed window
WINDOW_REGS = {0x70, 0x80}

# Bits the object writes without defining them (compared as 0). TBL(0x14)[0x33]
# is one 48-bit entry that sub_09eaf9 (the AC-PHY's update of register tables
# on a bandwidth change) writes from a 32-bit variable on its stack: the third
# 16-bit word is whatever lies on the stack behind it (0x8080 or 0 in the
# object's own session), so it changes with the stack use of any code that
# ran before. Proven by hooking the table write (docs/re/questions/phy-rxgain.md).
UNDEFINED_TBL = {('W', 'tbl.14', 0x33, 6): 0xffffffff}


def objects(variables=('hybrid-objs', 'hybrid-phy-objs')):
    """The objects of the hybrid build: hybrid/objects.mk, with the PHY layer."""
    text = open(os.path.join(ROOT, 'hybrid', 'objects.mk')).read()
    out = []
    for var in variables:
        m = re.search(var + r'\s*:=((?:.*\\\n)*.*)', text)
        out += [o for o in m.group(1).replace('\\\n', ' ').split()
                if o.endswith('.o') and o not in out]
    return out


def build(quiet=False):
    """Make the hybrid core for the emulator; returns its path."""
    zig = ab.find_zig()
    os.makedirs(os.path.join(OUT, 'obj'), exist_ok=True)
    objs = []
    phy_only = set(objects()) - set(objects(('hybrid-objs',)))
    for o in objects():
        src = os.path.join(ROOT, o[:-2] + '.c')
        if o in phy_only and not os.path.exists(src):
            continue    # a file of the PHY layer that is still to be written
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

    def __init__(self, path=None, off=(), coverage=False, patch=None, on=()):
        self.s = ab.Session(path=path)
        self.s.chip.radio_done = True
        self.m = self.s.m
        self.d = self.s.d
        if patch:
            patch(self.m)
        for layers, value in ((off, 0), (on, 1)):
            for layer in layers:
                sym = 'bcm4360_open_' + layer
                hits = [x for x in self.m.blob.byname.get(sym, []) if x.kind == 'O']
                if not hits:
                    raise SystemExit('%s: no such switch in this core' % sym)
                self.m.w8(hits[0].addr, value)
        if coverage:
            self.m.record_coverage()
            # executions per basic block, for the share counting repetitions
            self.counts = collections.Counter()
            tb = BASES['.text']

            def count(mu, address, size, user):
                self.counts[address] += 1
            import unicorn as uc
            self.m.mu.hook_add(uc.UC_HOOK_BLOCK, count, None, tb,
                               tb + self.m.blob.size['.text'] - 1)
        if path and 'bcm4360_tbl_write' in self.m.blob.byname:
            # the card model records the table transfers of Broadcom's table
            # functions; record those of the open ones (open/hw/access.c) alike
            shim = type('S', (), {})()
            shim.chip, shim.m, shim.sym = self.s.chip, self.m, (lambda name: name)
            ab.open_tables(shim)
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

    def session(self, steps=SESSION, stop_on_error=True):
        """Run the steps. With stop_on_error False an error of the emulation
        (for instance an open function that does not exist yet) ends the
        session early instead: self.error says why."""
        self.error = None
        for what in steps:
            try:
                self.step(what)
            except EmuError as e:
                if stop_on_error:
                    raise
                self.error = '%s: %s' % (what, str(e).splitlines()[0])
                break
        return self

    def logical(self):
        """The trace as tuples, without the moves of the BAR0 windows."""
        out = []
        for a in ab.logical(self.s.chip.trace, self.s.chip.func_name):
            if a[1] == 'pcicfg' and a[2] in WINDOW_REGS:
                continue
            if (a[0], a[1], a[2], a[3]) in UNDEFINED_TBL and isinstance(a[4], int):
                a = a[:4] + (a[4] & UNDEFINED_TBL[(a[0], a[1], a[2], a[3])],) + a[5:]
            out.append(a)
        return out


def check(off=(), show=20, patch=None, on=()):
    core = build(quiet=True)
    ref = Run().session()
    got = Run(core, off=off, patch=patch, on=on).session(stop_on_error=False)
    want, have = ref.logical(), got.logical()
    bad = 0
    if got.error:
        # compare what both did until the hybrid core stopped
        bad += 1
        print('the hybrid core stopped at step %s' % got.error)
        n = len(have)
        if want[:n] == have:
            print('  its %d accesses until then are the same as the object\'s first %d'
                  % (n, n))
        else:
            ab.compare(want[:n], have, show)
        want = want[:n]
    if ref.results[:len(got.results)] != got.results:
        bad += 1
        print('results of the steps differ:')
        for (w, a), (_, b) in zip(ref.results, got.results):
            if a != b:
                print('  %-10s object %s, hybrid %s' % (w, a, b))
    if want != have:
        bad += 1
        ab.compare(want, have, show)
    print('session (%d steps), switched on: %s, off: %s: %d accesses of the object, %d of '
          'the hybrid core, %s' % (len(SESSION), ', '.join(on) or '-', ', '.join(off) or '-',
                                   len(want), len(have), 'the same' if not bad else 'DIFFERENT'))
    return bad


def share():
    """Bytes of code executed in the session: Broadcom's, and the open code's."""
    core = build(quiet=True)
    text_end = BASES['.text'] + Blob(BLOB).size['.text']
    out = {}
    for name, path, off in (('object alone', None, ()), ('hybrid', core, ())):
        r = Run(path, off=off, coverage=True).session()
        closed = opened = dclosed = dopened = 0
        for a, size in r.m.blocks.items():
            n = r.counts.get(a, 0)
            if a < text_end:
                closed += size
                dclosed += size * n
            else:
                opened += size
                dopened += size * n
        out[name] = (closed, opened, dclosed, dopened)
    base = out['object alone'][0]
    for name, (closed, opened, dclosed, dopened) in out.items():
        print('%-14s Broadcom code executed: %7d bytes (%5.1f%%), open code: %6d bytes; '
              'of all execution (counting repetitions) %5.1f%% open'
              % (name, closed, 100.0 * closed / base, opened,
                 100.0 * dopened / max(dclosed + dopened, 1)))
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
    ap.add_argument('--on', default='', help='layers to switch on: ' + ', '.join(LAYERS))
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
    on = [x for x in ns.on.split(',') if x]
    return 1 if check(off, ns.show, on=on) else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except EmuError as e:
        sys.exit('emulation failed: %s' % e)
