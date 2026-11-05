#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
The open driver's core in the emulator (docs/re/07-open-driver.md,
milestone 1): open/core/ and the open layers, without any of Broadcom's
code, run through the same session as Broadcom's object alone, on the same
model of the card; the accesses of every step are compared.

    standalone.py build                 compile open/ and open/core/ (zig)
    standalone.py check [--show N] [--steps a,b,..]
                                        the session on both, compared step by step
    standalone.py undefined             what the open code still needs that nothing defines

The core's interface is open/include/bcm4360/core.h. Broadcom's data
(microcode, tables) is handed to hw_fw_data() from the memory of the object,
as in the hybrid tests; the module loads it from the firmware file instead.

The session (hybrid.py's, without what only Broadcom's upper layer has):
attach, up, channel changes in both bands and all bandwidths, the periodic
work, down, up, down, detach. "mpc 0" of the object's session has no
counterpart: the core never powers the radio down by itself.
"""
import argparse
import glob
import os
import struct
import subprocess
import sys

import ab
import hybrid
from chip import BAR0_VA, BAR0_CC_OFF, BAR0_PCIE_OFF, BAR0_WIN2_OFF
from emu import EmuError, s32

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = ab.ROOT
OUT = os.environ.get('STANDALONE_OUT') or os.path.join(ROOT, 're-out', 'standalone')
# open code that belongs to Broadcom's upper layer and has no place in the core:
# the iovar dispatcher and its module table (the hybrid's), the PHY's to-do file
EXCLUDE = {'open/lib/iovar.c', 'open/lib/module.c', 'open/phy/phy_todo.c'}
STEPS = (['attach', 'up'] + hybrid.CHANNELS +
         ['watchdog', 'down', 'up', '36/80', 'watchdog', 'down', 'detach'])
PCI_VENDOR_BROADCOM = 0x14e4


def sources():
    out = []
    for path in sorted(glob.glob(os.path.join(ROOT, 'open', '*', '*.c'))):
        rel = os.path.relpath(path, ROOT)
        if rel not in EXCLUDE:
            out.append(rel)
    return out


def build(quiet=False):
    """Compile the core and the open layers; returns the objects."""
    zig = ab.find_zig()
    os.makedirs(OUT, exist_ok=True)
    objs = []
    for rel in sources():
        out = os.path.join(OUT, rel[:-2].replace('/', '_') + '.o')
        src = os.path.join(ROOT, rel)
        if (not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(src)
                or any(os.path.getmtime(h) > os.path.getmtime(out)
                       for h in glob.glob(os.path.join(ROOT, 'open', 'include', 'bcm4360', '*.h')))):
            r = subprocess.run([zig, 'cc'] + ab.CFLAGS +
                               ['-I', os.path.join(ROOT, 'open', 'include'), '-o', out, src],
                               capture_output=True, text=True)
            if r.returncode != 0 or (r.stderr.strip() and not quiet):
                sys.stderr.write(r.stderr)
            if r.returncode != 0:
                raise SystemExit('does not compile: ' + rel)
        objs.append(out)
    return objs


class Core:
    """The core on a modelled card (no Broadcom code runs)."""

    def __init__(self, objs, **card):
        self.calls = []                 # (callback, args) of the bcm4360_plat_* callbacks
        self.s = ab.Session(objs=objs, env=self._env(), **card)
        self.s.chip.radio_done = True
        self.m = self.s.m
        self.chip = self.s.chip
        m = self.m
        if 'bcm4360_tbl_write' in [n for o in self.s.objs for n in o.symbols]:
            ab.open_tables(self.s)
        self.osh = self.s.d.osh
        self.hw = m.buf(128)                            # struct bcm4360_hw (72 bytes with owner)
        m.w64(self.hw + 0, BAR0_VA)                     # bar0
        m.w64(self.hw + 24, self.osh)                   # pdev: the OS layer's handle
        m.w64(self.hw + 32, BAR0_VA)                    # d11
        m.w64(self.hw + 40, BAR0_VA + BAR0_CC_OFF)      # cc
        m.w64(self.hw + 48, BAR0_VA + BAR0_PCIE_OFF)    # pcie
        m.w64(self.hw + 56, BAR0_VA + BAR0_WIN2_OFF)    # wrap
        self.perr = m.buf(8)
        self.core = 0
        self.plat = m.buf(16)

    def _env(self):
        calls = self.calls

        def fw_data(m, hw, name, psize):
            n = m.string(name)
            hits = [x for x in m.blob.byname.get(n, []) if x.kind == 'O']
            if not hits:
                return 0
            if psize:
                m.w32(psize, hits[0].size)
            return hits[0].addr

        def record(name):
            def fn(m, *args):
                calls.append((name, args))
                return 0
            return fn

        def timer_init(m, plat, fn, arg, name):
            return m.handlers['wl_init_timer'](m, 0, fn, arg, name)

        def timer_add(m, plat, t, ms, periodic):
            return m.handlers['wl_add_timer'](m, 0, t, ms, periodic)

        def timer_del(m, plat, t):
            tm = m.os.timers.get(t)
            was = bool(tm and tm.set)
            m.handlers['wl_del_timer'](m, 0, t)
            return 1 if was else 0

        def timer_free(m, plat, t):
            return m.handlers['wl_free_timer'](m, 0, t)

        def plat_clm(m, plat):
            n = 'clm_header'
            hits = [x for x in m.blob.byname.get(n, []) if x.kind == 'O']
            return hits[0].addr if hits else 0

        return {'hw_fw_data': (3, fw_data),
                'bcm4360_plat_clm': (1, plat_clm),
                'bcm4360_plat_rx': (3, record('rx')),
                'bcm4360_plat_txstatus': (3, record('txstatus')),
                'bcm4360_plat_tx_wake': (2, record('tx_wake')),
                'bcm4360_plat_fatal': (1, record('fatal')),
                'bcm4360_plat_timer_init': (4, timer_init),
                'bcm4360_plat_timer_add': (4, timer_add),
                'bcm4360_plat_timer_del': (2, timer_del),
                'bcm4360_plat_timer_free': (2, timer_free)}

    def call(self, name, *args):
        return self.m.call(self.s.sym(name), *args)

    def step(self, what, chanspec=None):
        m = self.m
        if what == 'attach':
            self.core = self.call('bcm4360_core_attach', self.hw, self.osh, PCI_VENDOR_BROADCOM,
                                  self.chip.device, self.plat, self.perr)
            return 0 if self.core else s32(m.r32(self.perr)) or -1
        if what == 'up':
            return s32(self.call('bcm4360_core_up', self.core, chanspec))
        if what == 'down':
            self.call('bcm4360_core_down', self.core)
            return 0
        if what == 'detach':
            self.call('bcm4360_core_detach', self.core)
            return 0
        if what == 'watchdog':
            m.os.run_timers(m, m.os.now_us // 1000 + 2500)
            return 0
        return s32(self.call('bcm4360_core_set_chanspec', self.core, chanspec))


def window_free(accesses):
    return [a for a in accesses if not (a[1] == 'pcicfg' and a[2] in hybrid.WINDOW_REGS)]


def check(show=20, steps=STEPS, stop=True):
    from run_chan import chanspec
    objs = build(quiet=True)
    ref = hybrid.Run()
    core = Core(objs)
    bad = 0
    cs_up = None
    for what in steps:
        n0 = len(ref.s.chip.trace)
        if what == 'up':
            ref.step('mpc 0')
            n0 = len(ref.s.chip.trace)
        try:
            r_ref = ref.step(what)
        except EmuError as e:
            print('%-8s the object fails: %s' % (what, str(e).splitlines()[0]))
            return bad + 1
        want = window_free(ab.logical(ref.s.chip.trace[n0:], ref.s.chip.func_name))
        cs = None
        if what == 'up':
            cs = ref.d.iovar_getint('chanspec')[1] & 0xffff
            cs_up = cs
        elif what not in ('attach', 'down', 'detach', 'watchdog'):
            cs = chanspec(ref.m, what)
        n1 = len(core.chip.trace)
        try:
            r_core = core.step(what, cs)
            err = None
        except EmuError as e:
            r_core, err = None, str(e).splitlines()[0]
        got = window_free(ab.logical(core.chip.trace[n1:], core.s.names))
        same = got == want and err is None
        print('%-8s object %5d accesses, core %5d%s  %s' % (
            what, len(want), len(got), '' if cs is None else ' (chanspec %#06x)' % cs,
            'same' if same else ('STOPPED: ' + err if err else 'DIFFERENT')))
        if not same:
            bad += 1
            if not err or got:
                ab.compare(want, got, show)
            if err and stop:
                break
    print('standalone check: %s' % ('pass' if not bad else '%d step(s) differ' % bad))
    return bad


def undefined():
    """Symbols the open code refers to that neither it nor the emulator's
    imports define (what open/core/ still has to provide)."""
    from elftools.elf.elffile import ELFFile
    defined, needed = set(), {}
    for o in build(quiet=True):
        with open(o, 'rb') as f:
            elf = ELFFile(f)
            for sec in elf.iter_sections():
                if sec.header.sh_type != 'SHT_SYMTAB':
                    continue
                for sym in sec.iter_symbols():
                    if not sym.name or sym['st_info']['type'] == 'STT_FILE':
                        continue
                    if sym['st_shndx'] == 'SHN_UNDEF':
                        needed.setdefault(sym.name, set()).add(os.path.basename(o))
                    elif sym['st_info']['bind'] != 'STB_LOCAL':
                        defined.add(sym.name)
    imports = set(hybrid.Blob(hybrid.BLOB).imports)
    provided = set(Core.__dict__) | {'hw_fw_data'}
    missing = {n: f for n, f in needed.items()
               if n not in defined and n not in imports and n not in provided
               and not n.startswith('bcm4360_plat_')}
    for n in sorted(missing):
        print('%-50s %s' % (n, ', '.join(sorted(missing[n]))))
    print('%d undefined' % len(missing))
    return missing


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=('build', 'check', 'undefined'))
    ap.add_argument('--show', type=int, default=20)
    ap.add_argument('--steps', default='')
    ap.add_argument('--go-on', action='store_true', help='check: go on after a step that fails')
    ns = ap.parse_args()
    if ns.cmd == 'build':
        print('\n'.join(build()))
        return 0
    if ns.cmd == 'undefined':
        undefined()
        return 0
    steps = ns.steps.split(',') if ns.steps else STEPS
    return 1 if check(ns.show, steps, stop=not ns.go_on) else 0


if __name__ == '__main__':
    sys.exit(main())
