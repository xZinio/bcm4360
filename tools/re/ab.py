#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
A/B comparison of open code with Broadcom's object, by register trace.

Both run in the same emulator against the same model of the card.  A test
("scenario") names a function of the object - the *stage* - and the function
of the open code that is to do the same job.  The scenario

1. runs the object through a normal session (attach, up, channel changes ...),
   and at each entry of the stage function saves the state of the card model
   and notes where in the trace the stage begins and ends;
2. for each such stage restores the saved state of the card, calls the open
   function, records its accesses;
3. compares the two sequences of accesses.

Compared are the accesses at the level the driver thinks in: PHY registers,
radio registers, PHY tables, shared memory, PMU registers, core registers,
and the delays in between.  The raw accesses to the address/data register
pairs behind them are left out.

    ab.py build [SOURCES...]       compile the open code (needs zig, see ZIG)
    ab.py list                     the scenarios
    ab.py run NAME [--show N]      run one, print the first N differences
    ab.py all                      run all, one line each

The compiler is `zig cc`; set the environment variable ZIG to the zig
executable if it is not on the PATH.
"""
import argparse
import copy
import difflib
import glob
import os
import shutil
import subprocess
import sys

from unicorn import x86_const as x86

from chip import Access
from elfobj import Loader
from emu import EmuError

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
OPEN_SRC = os.path.join(ROOT, 'open')
OPEN_OBJ = os.path.join(ROOT, 're-out', 'open')

CFLAGS = ['-target', 'x86_64-freestanding', '-c', '-O1', '-std=gnu11', '-ffreestanding',
          '-fno-pic', '-fno-pie', '-mcmodel=kernel', '-mno-red-zone', '-mno-sse', '-mno-mmx',
          '-fno-stack-protector', '-fno-builtin', '-fno-omit-frame-pointer',
          '-fno-asynchronous-unwind-tables', '-fno-strict-aliasing', '-Wall', '-Wextra',
          '-Wno-unused-parameter', '-Werror=implicit-function-declaration',
          '-DBCM4360_EMU=1']

# accesses that are the mechanics of an indirect access which is recorded itself
RAW_D11 = {0x160, 0x164, 0x166, 0x3d8, 0x3da, 0x3fc, 0x3fe}
TABLE_REGS = {0x0d, 0x0e, 0x0f, 0x10, 0x11}


def find_zig():
    z = os.environ.get('ZIG') or shutil.which('zig')
    if z:
        return z
    for pat in (os.path.join(ROOT, 're-out', 'tools', 'zig*', 'zig.exe'),
                os.path.join(os.environ.get('TEMP', ''), 'claude', '*', '*', 'scratchpad',
                             'zig*', 'zig.exe')):
        hits = glob.glob(pat)
        if hits:
            return hits[0]
    raise SystemExit('zig not found: install it or set ZIG to the executable')


def build(sources=None, quiet=False, strict=True):
    """Compile the open sources; returns the list of objects.

    Not strict: a file that does not compile is reported and left out, so that
    the scenarios of the other files can still run (several people work in
    open/ at the same time).
    """
    zig = find_zig()
    os.makedirs(OPEN_OBJ, exist_ok=True)
    if not sources:
        sources = sorted(glob.glob(os.path.join(OPEN_SRC, '**', '*.c'), recursive=True))
    objs = []
    failed = []
    inc = ['-I', os.path.join(OPEN_SRC, 'include')]
    for src in sources:
        rel = os.path.relpath(src, OPEN_SRC).replace(os.sep, '_')
        out = os.path.join(OPEN_OBJ, os.path.splitext(rel)[0] + '.o')
        r = subprocess.run([zig, 'cc'] + CFLAGS + inc + ['-o', out, src],
                           capture_output=True, text=True)
        if r.returncode != 0 or (r.stderr.strip() and not quiet):
            sys.stderr.write(r.stderr)
        if r.returncode != 0:
            failed.append(src)
            if os.path.exists(out):
                os.remove(out)
            continue
        objs.append(out)
    if failed:
        msg = 'does not compile: ' + ', '.join(os.path.relpath(f, ROOT) for f in failed)
        if strict:
            raise SystemExit(msg)
        sys.stderr.write(msg + '\n')
    return objs


# ------------------------------------------------------------------ card state
def snapshot(chip):
    """State of the card model that register accesses depend on."""
    s = {'cfg': bytes(chip.cfg), 'win': (chip.bar0_win, chip.bar0_win2), 'cores': []}
    for c in chip.cores:
        d = {k: copy.deepcopy(v) for k, v in vars(c).items()
             if k not in ('chip', 'phy') and not callable(v)}
        s['cores'].append(d)
    phy = getattr(chip, 'phy', None)
    if phy is not None:
        s['phy'] = {k: copy.deepcopy(v) for k, v in vars(phy).items()
                    if k not in ('chip', 'd11')}
    s['time'] = chip.m.os.now_us
    return s


def restore(chip, s):
    chip.cfg[:] = s['cfg']
    chip.bar0_win, chip.bar0_win2 = s['win']
    for c, d in zip(chip.cores, s['cores']):
        for k, v in d.items():
            setattr(c, k, copy.deepcopy(v))
    if 'phy' in s:
        for k, v in s['phy'].items():
            setattr(chip.phy, k, copy.deepcopy(v))
    chip.m.os.now_us = s['time']


# ------------------------------------------------------------------ traces
TABLE_CARRIERS = {'wlc_phy_write_table_ext', 'wlc_phy_read_table_ext', 'phytbl_write',
                  'phytbl_read', 'bcm4360_tbl_write', 'bcm4360_tbl_read'}


def logical(trace, names, spaces=None, services=()):
    """The accesses worth comparing, as tuples (op, space, address, size, value).

    services: names of functions whose own accesses do not count (services of
    another layer; their calls are in the trace as records of the space 'svc',
    see mark_services()).
    """
    out = []
    services = set(services)
    for a in trace:
        if a.space == 'd11' and a.addr in RAW_D11:
            continue
        if a.space == 'phy' and a.addr in TABLE_REGS:
            chain = [names(a.pc)] + [names(x) for x in a.bt[:2]]
            if any(n in TABLE_CARRIERS for n in chain):
                continue
        if spaces is not None and a.space.split('.')[0] not in spaces:
            continue
        if services:
            chain = chain_of(a, names)
            if a.space.split('.')[0] == 'svc':
                chain = chain[1:]       # the service itself
            if any(n in services for n in chain):
                continue
        out.append((a.op, a.space, a.addr, a.size, a.value))
    return out


def fmt(e):
    if e[1] == 'delay':
        return 'delay %d us' % e[4]
    if e[1].startswith('svc.'):
        n = e[3] // 4
        return 'call %s(%s)' % (e[1][4:], ', '.join(
            '%#x' % (e[4] >> (32 * i) & 0xffffffff) for i in range(n)))
    return '%s %-11s %05x/%d = %0*x' % (e[0], e[1], e[2], e[3], 2 * min(e[3], 8), e[4])


def compare(ref, got, show=20):
    """Prints the differences; returns the number of differing blocks."""
    sm = difflib.SequenceMatcher(a=ref, b=got, autojunk=False)
    blocks = [op for op in sm.get_opcodes() if op[0] != 'equal']
    for tag, i1, i2, j1, j2 in blocks[:show]:
        print('  at access %d of the object\'s trace (%s):' % (i1, tag))
        for e in ref[max(i1 - 2, 0):i1]:
            print('      both     ' + fmt(e))
        for e in ref[i1:min(i2, i1 + 12)]:
            print('    - object   ' + fmt(e))
        if i2 - i1 > 12:
            print('    - ... %d more' % (i2 - i1 - 12))
        for e in got[j1:min(j2, j1 + 12)]:
            print('    + open     ' + fmt(e))
        if j2 - j1 > 12:
            print('    + ... %d more' % (j2 - j1 - 12))
    if len(blocks) > show:
        print('  ... %d more differences' % (len(blocks) - show))
    return len(blocks)


class _Quiet:
    """Stands for the card while recorded writes are applied to a saved state."""

    def record(self, *a):
        pass

    def phy_written(self, *a):
        pass

    def phy_read(self, addr, value):
        return value

    def radio_read(self, addr, value):
        return value


def advance(state, accesses):
    """Saved card state after the given accesses (PHY and radio registers, tables)."""
    from bcm4360 import Phy
    s = copy.deepcopy(state)
    if 'phy' not in s:
        return s
    phy = Phy.__new__(Phy)
    for k, v in s['phy'].items():
        setattr(phy, k, v)
    phy.chip = _Quiet()
    for a in accesses:
        if a.space == 'phy':
            if a.op == 'W':
                phy.reg_write(a.addr, a.value)
            elif Phy.T_LO <= a.addr <= Phy.T_WIDE:
                phy.reg_read(a.addr)
        elif a.space == 'radio' and a.op == 'W':
            phy.radio_write(a.addr, a.value)
    s['phy'] = {k: v for k, v in vars(phy).items() if k not in ('chip', 'd11')}
    return s


# Functions that only carry out an access for their caller: the "owner" of an
# access is the innermost function of the call chain that is not one of these.
CARRIERS = {
    'phy_reg_read', 'phy_reg_write', 'phy_reg_mod', 'phy_reg_and', 'phy_reg_or',
    'phy_reg_read_wide', 'phy_reg_write_wide', 'read_radio_reg', 'write_radio_reg',
    'mod_radio_reg', 'and_radio_reg', 'or_radio_reg', 'xor_radio_reg',
    'wlc_phy_write_table_ext', 'wlc_phy_read_table_ext', 'wlc_phy_table_write_acphy',
    'wlc_phy_table_read_acphy', 'ai_corereg', 'si_corereg', 'si_pmu_regcontrol',
    'si_pmu_chipcontrol', 'si_pmu_pllcontrol', 'wlc_bmac_write_shm', 'wlc_bmac_read_shm',
    'wlapi_bmac_write_shm', 'wlapi_bmac_read_shm', 'sub_061ed5', 'sub_06188a',
    'wlc_bmac_copyto_objmem', 'wlc_bmac_copyfrom_objmem',
    # the same in the open code (open/hw/access.c)
    'bcm4360_phy_read', 'bcm4360_phy_write', 'bcm4360_phy_mod', 'bcm4360_phy_and',
    'bcm4360_phy_or', 'bcm4360_radio_read', 'bcm4360_radio_write', 'bcm4360_radio_mod',
    'bcm4360_radio_and', 'bcm4360_radio_or', 'bcm4360_radio_xor', 'bcm4360_tbl_write',
    'bcm4360_tbl_read', 'bcm4360_shm_read', 'bcm4360_shm_write',
}


def chain_of(a, names):
    """Function names of the call chain of an access, innermost first."""
    return [names(a.pc)] + [names(x) for x in a.bt]


def owner(a, names):
    for n in chain_of(a, names):
        if n not in CARRIERS:
            return n
    return None


# ------------------------------------------------------------------ stages
class Stage:
    """One execution of a function of the object inside a session."""

    def __init__(self, func, index, args, state, machine):
        self.func = func
        self.start = index          # position in the trace at entry
        self.end = None
        self.args = args            # the six argument registers at entry
        self.state = state          # card state at entry
        self.depth = None
        self.extra = {}             # whatever the scenario read from the object's memory


def watch(d, funcs, inspect=None):
    """Note every entry of the functions while the session runs."""
    m, chip = d.m, d.chip
    stages = []

    def make(func):
        def on_entry(m, *args):
            st = Stage(func, len(chip.trace), args, snapshot(chip), m)
            st.depth = len(m.return_addresses())
            if inspect is not None:
                inspect(st, m, args)
            stages.append(st)
        return on_entry

    for f in funcs:
        m.on_call(f, make(f))
    return stages


def close_stages(d, stages):
    """Find where each stage ends: the first access made after its return."""
    chip = d.chip
    target = {}
    for st in stages:
        target.setdefault(st.func, d.m.resolve(st.func))
    b = d.m.blob
    for st in stages:
        f = b.func_at(target[st.func])
        lo, hi = f.addr, f.addr + f.size
        end = st.start
        tr = chip.trace
        n = len(tr)
        while end < n:
            a = tr[end]
            inside = lo <= a.pc < hi or any(lo <= x < hi for x in a.bt)
            if not inside:
                break
            end += 1
        st.end = end
    return stages


class Session:
    """A driver instance with the object, and the open code loaded next to it.

    env: functions that the open code imports and the test provides, as
    {name: (number of arguments, function(machine, *arguments))}.  They take
    precedence over functions of the same name in the open code.
    """

    def __init__(self, objs=None, env=None, **kw):
        from run_attach import Driver
        self.d = Driver(echo=False, **kw)
        self.m = self.d.m
        self.chip = self.d.chip
        self.loader = Loader(self.m)
        for name, (nargs, fn) in (env or {}).items():
            self.loader.define(name, nargs, fn)
        self.objs = [self.loader.load(o) for o in (objs or [])]
        self.names = self._names
        self._strings = {}

    def cstring(self, text):
        """Address of a constant string in the memory of the machine."""
        if text not in self._strings:
            self._strings[text] = self.m.buf(text.encode('latin1') + b'\0')
        return self._strings[text]

    def _names(self, addr):
        for o in self.objs:
            if o.base <= addr < o.end:
                return o.func_at(addr) or '%#x' % addr
        return self.chip.func_name(addr)

    def sym(self, name):
        return self.loader.resolve(name)

    def call_open(self, name, *args):
        """Call a function of the open code; returns (result, its accesses)."""
        n = len(self.chip.trace)
        r = self.m.call(self.sym(name), *args)
        return r, self.chip.trace[n:]


ENTRY_BYTES = {8: 1, 16: 2, 32: 4, 48: 6, 60: 8, 64: 8}


def open_tables(s):
    """Record the table transfers of the open code (bcm4360_tbl_write/read of
    open/hw/access.c) the way the card model records those of the object."""
    chip = s.chip

    def on_write(m, io, tid, n, off, width, data):
        tid, n, off, width = (x & 0xffffffff for x in (tid, n, off, width))
        size = ENTRY_BYTES.get(width)
        if size is None:
            return
        raw = m.read(data, n * size)
        for i in range(n):
            v = int.from_bytes(raw[i * size:(i + 1) * size], 'little')
            chip.record('W', 'tbl.%02x' % tid, off + i, size, v)

    def on_read(m, io, tid, n, off, width, data):
        tid, n, off, width = (x & 0xffffffff for x in (tid, n, off, width))
        chip.record('R', 'tbl.%02x' % tid, off, n, width)

    s.m.on_call(s.sym('bcm4360_tbl_write'), on_write, 6)
    s.m.on_call(s.sym('bcm4360_tbl_read'), on_read, 6)


def service_value(args):
    v = 0
    for i, a in enumerate(args):
        v |= (a & 0xffffffff) << (32 * i)
    return v


class Service:
    """A function of another layer that the code under test calls.

    In the tests of a layer the services of the other layers are not
    executed but noted: each call becomes a record in the trace, ('C',
    'svc.<name>', 0, 4 * number of arguments, the arguments as 32 bit values),
    so that the calls, their arguments and their place between the register
    accesses are compared.  What the service of the object did to the
    hardware is left out of the comparison; what it returned is given to the
    open code when that makes the corresponding call.

    func    function of the object
    name    bcm4360_<name> is the function of the open code
    args    the arguments after the first one (the handle): for each its
            size in bytes (1, 2, 4), or 0 for one that is not compared (a
            pointer)
    result  the function returns something
    out     (index of an argument after the handle that is a pointer, size):
            memory the function fills
    """

    def __init__(self, func, name, args=(), result=False, out=None):
        self.func, self.name, self.args = func, name, tuple(args)
        self.nargs = len(self.args)
        self.result, self.out = result, out

    def value(self, args):
        return service_value([a & ((1 << (8 * w)) - 1)
                              for a, w in zip(args[1:1 + self.nargs], self.args)])


class ServiceLog:
    """Calls of services made by the object, in the order of the trace."""

    def __init__(self):
        # [position in the trace, name, result, out, names of the functions
        # of the call chain (innermost first)]
        self.calls = []

    def between(self, start, end, without=()):
        """The calls of a part of the trace, except those made by (or below)
        one of the functions `without`."""
        without = set(without)
        return [c for c in self.calls if start <= c[0] < end
                and not any(n in without for n in c[4])]


def mark_services(d, services):
    """Note the calls of the services in the trace of the object; returns the log.

    A service that is called by another service (or further down) is part of
    that one and is not noted.
    """
    m, chip = d.m, d.chip
    log = ServiceLog()
    funcs = {sv.func for sv in services}
    names = {}

    def name_of(addr):
        if addr not in names:
            names[addr] = chip.func_name(addr)
        return names[addr]

    for sv in services:
        def hook(m, *args, sv=sv):
            ret = m.r64(m.mu.reg_read(x86.UC_X86_REG_RSP))
            chain = [name_of(a) for a in (ret,) + m.return_addresses()]
            if any(n in funcs for n in chain):
                return
            call = [len(chip.trace), sv.name, 0, None, chain]
            chip.record('C', 'svc.' + sv.name, 0, 4 * sv.nargs, sv.value(args))
            log.calls.append(call)
            if sv.result or sv.out:
                def done(m, result, call=call, args=args, sv=sv):
                    call[2] = result
                    if sv.out:
                        p = args[1 + sv.out[0]]
                        call[3] = m.read(p, sv.out[1]) if p else None
                m.at_return(done)
        m.on_call(sv.func, hook, 1 + sv.nargs)
    return log


class Replay:
    """What the services return to the open code: what they returned to the object."""

    def __init__(self):
        self.calls = []
        self.unexpected = []

    def load(self, calls):
        self.calls = list(calls)
        self.unexpected = []

    def take(self, name):
        for i, c in enumerate(self.calls):
            if c[1] == name:
                return self.calls.pop(i)
        self.unexpected.append(name)
        return None


def service_env(session_ref, services, replay):
    """The services for the open code: {name: (nargs, function)} for Session(env=).

    session_ref: a list that will hold the session (it does not exist yet when
    the functions are defined).
    """
    env = {}
    for sv in services:
        def call(m, *args, sv=sv):
            session_ref[0].chip.record('C', 'svc.' + sv.name, 0, 4 * sv.nargs, sv.value(args))
            c = replay.take(sv.name)
            if c is None:
                return 0
            if sv.out and c[3] is not None and args[1 + sv.out[0]]:
                m.write(args[1 + sv.out[0]], c[3])
            return c[2]
        env['bcm4360_' + sv.name] = (1 + sv.nargs, call)
    return env


def variables(s):
    """The variables of the session's card, as the object makes them from the SROM."""
    from srom import Layout
    words = s.chip.srom
    if words is None:
        return {}
    return dict(Layout(s.m.blob).decode(words))


def var_env(session_ref, override=None):
    """hw_getvar() of hw.h for the open code, for Session(env=)."""
    cache = {}

    def hw_getvar(m, hw, name):
        s = session_ref[0]
        if 'vars' not in cache:
            cache['vars'] = variables(s)
            cache['vars'].update(override or {})
        v = cache['vars'].get(m.string(name))
        return s.cstring(v) if v is not None else 0

    return {'hw_getvar': (2, hw_getvar)}


def delays(d):
    """Make the session record delays as accesses of the space 'delay'."""
    m, chip = d.m, d.chip
    inner = m.handlers['osl_delay']

    def osl_delay(m, usec):
        chip.record('D', 'delay', 0, 4, usec & 0xffffffff)
        return inner(m, usec)

    m.handlers['osl_delay'] = osl_delay


def selftest():
    """The tool's own features, with data/harness-test.c in the place of open code."""
    objs = build([os.path.join(OPEN_SRC, 'hw', 'access.c'),
                  os.path.join(HERE, 'data', 'harness-test.c')], quiet=True)
    services = [Service('wlapi_bmac_phyclk_fgc', 'test_service', (1, 2)),
                Service('si_core_sflags', 'test_query', (4, 4), result=True)]
    ref = []
    replay = Replay()
    env = service_env(ref, services, replay)
    env.update(var_env(ref))
    s = Session(objs, env=env)
    ref.append(s)
    delays(s.d)
    log = mark_services(s.d, services)
    s.d.attach()
    # the object asked for the status flags of the 802.11 core during attach
    asked = [c for c in log.calls if c[1] == 'test_query']
    bad = 0
    if not asked or asked[0][2] & 0xffffffff != s.chip.core('d11').iostatus:
        bad += 1
        print('results of the object\'s services: %r' % asked[:3])
    replay.load([[0, 'test_query', 5, None, []]])
    open_tables(s)
    hw = s.m.buf(64)
    from chip import BAR0_VA
    import struct
    s.m.write(hw, struct.pack('<QQIIQQQQQ', BAR0_VA, 0, 0, 0, s.d.pdev, BAR0_VA,
                              BAR0_VA + 0x3000, BAR0_VA + 0x2000, BAR0_VA + 0x1000))
    io = s.m.buf(64)
    s.m.call(s.sym('bcm4360_phy_io_init'), io, hw, 1, 24)
    r, trace = s.call_open('harness_test', io)
    got = logical(trace, s.names, ('phy', 'tbl', 'svc', 'delay'))
    want = [('C', 'svc.test_service', 0, 8, 0x3456 << 32 | 0x12),
            ('W', 'phy', 0x400, 2, 0x55aa),
            ('W', 'tbl.40', 7, 2, 0x1111), ('W', 'tbl.40', 8, 2, 0x2222),
            ('W', 'tbl.40', 9, 2, 0x3333), ('R', 'tbl.40', 7, 3, 16),
            ('C', 'svc.test_query', 0, 8, 0),
            ('D', 'delay', 0, 4, 10)]
    if got != want:
        bad += 1
        print('trace of the test function:')
        compare(want, got)
    boardtype = variables(s).get('boardtype', '')
    expect = ord(boardtype[0]) | 0x2222 << 16 | 5 << 12
    if r & 0xffffffff != expect:
        bad += 1
        print('result %#x, expected %#x' % (r & 0xffffffff, expect))
    owners = [owner(a, s.names) for a in trace if a.space.startswith('tbl')]
    if set(owners) != {'harness_test'}:
        bad += 1
        print('owner of the table transfers: %s' % sorted(set(map(str, owners))))
    # the same attribution for the object: a transfer is owned by the function
    # that asked for it
    d = Session().d
    d.attach()
    d.iovar_setint('mpc', 0)
    d.up()
    own = {owner(a, d.chip.func_name) for a in d.chip.trace if a.space.startswith('tbl')}
    if not own or None in own or own & CARRIERS:
        bad += 1
        print('owners of the object\'s table transfers: %s' % sorted(map(str, own)))
    print('selftest %s' % ('FAILED' if bad else 'ok'))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['build', 'list', 'run', 'all', 'selftest'])
    ap.add_argument('args', nargs='*')
    ap.add_argument('--show', type=int, default=20)
    ns = ap.parse_args()
    if ns.cmd == 'build':
        for o in build(ns.args):
            print(os.path.relpath(o, ROOT))
        return 0
    if ns.cmd == 'selftest':
        return selftest()
    import scenarios
    import phy_scenarios    # noqa: F401 (adds its scenarios to the table)
    table = scenarios.all_scenarios()
    if ns.cmd == 'list':
        for name, sc in table.items():
            print('%-28s %s' % (name, sc.__doc__.strip().splitlines()[0] if sc.__doc__ else ''))
        return 0
    objs = build(quiet=True, strict=False)
    names = ns.args if ns.cmd == 'run' else list(table)
    bad = 0
    for name in names:
        if name not in table:
            raise SystemExit('no scenario %s' % name)
        try:
            res = table[name](objs, ns.show if ns.cmd == 'run' else 0)
        except EmuError as e:
            print('%-28s ERROR %s' % (name, str(e).splitlines()[0]))
            if ns.cmd == 'run':
                print(e)
            bad += 1
            continue
        ok = res['differences'] == 0
        bad += not ok
        print('%-28s %s  %d stages, %d accesses of the object, %d of the open code, '
              '%d differences' % (name, 'ok    ' if ok else 'DIFFER', res['stages'],
                                  res['ref'], res['got'], res['differences']))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
