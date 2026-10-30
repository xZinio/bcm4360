#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Which calls cross the boundary of a layer while the object runs a session.

A layer is a set of source files of the object (modmap.txt).  The object is
run through a session and every call from a function outside the layer to a
function inside it ("in": the interface of the layer as it is used), and
from inside to outside ("out": the services it needs from other layers) is
counted, with the callers.

    boundary.py FILE.c [FILE.c ...] [--steps up,36/80,watchdog,down] [--args]

--args lists the arguments of the first calls of each function crossing in.
Used to define the stages and services of the comparison tests (ab.py).
"""
import argparse
import collections
import os

import unicorn as uc
from unicorn import x86_const as x86

from blob import BASES
from emu import STOP_ADDR, ARG_REGS
from modmap import attribute, load_pins

HERE = os.path.dirname(os.path.abspath(__file__))


class Boundary:
    """Counts the calls that cross the boundary of the layer `files`."""

    def __init__(self, m, files):
        self.m = m
        b = m.blob
        if b.funcs is None:
            b.discover_functions()
        where = attribute(b, load_pins(os.path.join(HERE, 'modmap.txt')))
        self.file_of = {f.addr: where[f.addr] for f in b.funcs}
        self.name_of = {f.addr: f.name for f in b.funcs}
        self.files = set(files)
        self.inside = {a for a, w in self.file_of.items() if w in self.files}
        self.calls_in = collections.Counter()       # (function, caller) -> count
        self.calls_out = collections.Counter()
        self.first_args = collections.defaultdict(list)
        self.phase = ''
        self.by_phase = collections.defaultdict(collections.Counter)
        self.log = []                               # (phase, direction, function, caller)
        self.keep_log = False
        tb = BASES['.text']
        m.mu.hook_add(uc.UC_HOOK_BLOCK, self._block, None, tb, tb + b.size['.text'] - 1)

    def _block(self, mu, address, size, user):
        if address not in self.file_of:
            return
        m = self.m
        ret = m.r64(mu.reg_read(x86.UC_X86_REG_RSP))
        if ret == STOP_ADDR:
            caller, cfile = None, '(harness)'
        else:
            f = m.blob.func_at(ret)
            if f is None:
                return                  # called by an import handler (a callback)
            caller, cfile = f.addr, self.file_of.get(f.addr)
            # a jump to the entry of a function from inside the same function
            # (a loop back to its first block) is not a call
            if caller == address:
                return
        into = address in self.inside
        frm = caller in self.inside if caller is not None else False
        if into == frm:
            return
        name = self.name_of[address]
        cname = self.name_of.get(caller, '(harness)')
        if into:
            self.calls_in[(name, cname)] += 1
            self.by_phase[self.phase][('in', name)] += 1
            if len(self.first_args[name]) < 4:
                self.first_args[name].append(tuple(mu.reg_read(r) for r in ARG_REGS))
        else:
            self.calls_out[(name, cname)] += 1
            self.by_phase[self.phase][('out', name)] += 1
        if self.keep_log:
            self.log.append((self.phase, 'in' if into else 'out', name, cname))

    def report(self, args=False):
        for title, table in (('calls INTO the layer', self.calls_in),
                             ('calls OUT of the layer', self.calls_out)):
            print('== %s (%s)' % (title, ' '.join(sorted(self.files))))
            total = collections.Counter()
            who = collections.defaultdict(set)
            for (name, caller), n in table.items():
                total[name] += n
                who[name].add(caller)
            byfile = collections.defaultdict(list)
            addr = {v: k for k, v in self.name_of.items()}
            for name in total:
                byfile[self.file_of[addr[name]]].append(name)
            for fname in sorted(byfile):
                print('  %s' % fname)
                for name in sorted(byfile[fname], key=lambda n: -total[n]):
                    callers = sorted(who[name])
                    print('    %6d  %-40s <- %s' % (total[name], name, ', '.join(callers[:6])
                                                    + (' ...' if len(callers) > 6 else '')))
                    if args and title.startswith('calls INTO'):
                        for a in self.first_args[name]:
                            print('              (%s)' % ', '.join('%#x' % (x & 0xffffffff)
                                                                 for x in a[1:5]))


def run_session(files, steps, keep_log=False, **kw):
    """Run the object through attach and the steps; returns (session, boundary)."""
    import ab
    from scenarios import set_channel
    s = ab.Session(**kw)
    s.chip.radio_done = True
    d, m = s.d, s.m
    bd = Boundary(m, files)
    bd.keep_log = keep_log
    bd.phase = 'attach'
    d.attach()
    if not d.wlc:
        raise SystemExit('the object does not attach (error %d)' % d.err)
    d.iovar_setint('mpc', 0)
    for step in steps:
        bd.phase = step
        if step == 'up':
            d.up()
        elif step == 'down':
            m.call('wlc_down', d.wlc)
        elif step == 'watchdog':
            m.os.run_timers(m, m.os.now_us // 1000 + 2500)
        elif step == 'detach':
            m.call('wlc_detach', d.wlc)
        else:
            set_channel(s, step)
    return s, bd


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='+')
    ap.add_argument('--steps', default='up,36,36/80,6,149/40,watchdog,down,up,down')
    ap.add_argument('--args', action='store_true')
    ns = ap.parse_args()
    s, bd = run_session(ns.files, [x for x in ns.steps.split(',') if x])
    bd.report(ns.args)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
