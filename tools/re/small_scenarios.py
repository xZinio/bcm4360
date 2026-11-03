#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
A/B check of three small layers (task docs/re/tasks/small.md):

* the offload semaphore, tcm_sem_enter/tcm_sem_exit (bmac-init.md A8; open
  code open/mac/offload.c, adapter hybrid/mac_sem.c, switch open_mac);
* the global NVRAM of nvram_ro.c (srom-otp.md "nvram_ro.c"; open/chip/nvram.c,
  hybrid/nvram.c, switch open_nvram);
* the module table, wlc_module_register/unregister (iovar.md "Module register
  and unregister"; open/lib/module.c, hybrid/iovar_mod.c, switch open_iovar);
* the PCIe bridge's si_survive_perst_war (chip.md; open/chip/si_pcie.c,
  hybrid/si_perst.c, switch open_si).

The object alone and the hybrid core are brought to the same state (attach,
up), then the functions are called directly on both with the same arguments.
Compared after each call: the result and the memory the function works on
(the shared structure of the semaphore and the driver-wide spin count; the
strings and pointers of the NVRAM, with the use of the heap; the whole module
table), at the end every access to the card.

The semaphore gets a stand-in offload state (the driver has none in the
emulator), the NVRAM blocks made by hand and files "nvram.txt" of the
emulated host.

    python hybrid.py small [--off mac,nvram,iovar,si] [--show N] [--model]
"""
import zlib

import ab
import hybrid
from blob import BASES

# the object's .bss comes first in the hybrid core too (hybrid.lds)
MAX_SPINS = BASES['.bss'] + 0x3004
WLC_OL, OL_UP, OL_SHARED = 0x750, 0x8c, 0x90
WLC_HW, WLC_HW_SIH, SIH_BUSTYPE = 0x20, 0xb8, 4
WLC_MODULECB, NSLOTS, SLOT = 0x358, 64, 0x50


class Funcs:
    """The functions under test, called on the machine of a run."""

    def __init__(self, m):
        self.m = m

    def __getattr__(self, name):
        return lambda *a: self.m.call(name, *a)


class ReadHook:
    """Clears a word of the shared structure when it has been read k times
    by emulated code (the ARM letting go of the semaphore)."""

    def __init__(self, m, addr, k):
        import unicorn as uc
        self.m, self.addr, self.k, self.reads = m, addr, k, 0
        self.h = m.mu.hook_add(uc.UC_HOOK_MEM_READ, self._read, None, addr, addr + 3)
        m.mu.ctl_flush_tb()    # code translated before the hook would not call it
        m.read_hooks = getattr(m, 'read_hooks', {})
        m.read_hooks[addr] = self     # for a model that reads with m.r32

    def _read(self, mu, access, address, size, value, user):
        self.touch()

    def touch(self):
        self.reads += 1
        if self.reads >= self.k:
            self.m.mu.mem_write(self.addr, b'\0\0\0\0')

    def done(self):
        self.m.mu.hook_del(self.h)
        self.m.mu.ctl_flush_tb()
        del self.m.read_hooks[self.addr]
        return self.reads


def s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def walk(r, f):
    """Run all the calls on the machine of r with the functions f; returns the
    records and the results of the steps."""
    steps = [(s, r.step(s)) for s in ('attach', 'mpc 0', 'up')]
    m, wlc = r.m, r.d.wlc
    out = []
    out += sem_walk(m, wlc, f)
    out += nvram_walk(m, wlc, f)
    out += module_walk(m, wlc, f)
    out += perst_walk(m, wlc, f)
    return out, steps


# ---------------------------------------------------------------- semaphore
def sem_walk(m, wlc, f):
    out = []
    saved = m.r64(wlc + WLC_OL)
    ol, sh = m.buf(0x100), m.buf(0x40)

    def rec(label, extra=None):
        out.append(('sem', label, m.read(sh, 0x20).hex(), s32(m.r32(MAX_SPINS)), extra))

    def both(label):
        f.tcm_sem_enter(wlc)
        rec(label + ': enter')
        f.tcm_sem_exit(wlc)
        rec(label + ': exit')

    m.w32(MAX_SPINS, 0)
    m.write(sh, bytes(range(0x40)))         # a pattern: what is not written stays
    m.w64(wlc + WLC_OL, 0)
    both('no offload state')
    m.w64(wlc + WLC_OL, ol)
    m.w8(ol + OL_UP, 0)
    m.w64(ol + OL_SHARED, sh)
    both('offloads not up')
    m.w8(ol + OL_UP, 1)
    m.w64(ol + OL_SHARED, 0)
    both('no shared structure')
    m.w8(ol + OL_UP, 0x80)
    m.w64(ol + OL_SHARED, sh)
    for v18 in (0, 2, 0x101, 0xffffffff):
        m.w32(sh + 0x18, v18)
        both('word 0x18 = %#x' % v18)
    for start in (0, 99999, 100000, 100001, -1, -100000, 0x7fffffff):
        m.w32(MAX_SPINS, start)
        m.w32(sh + 0x18, 1)
        both('ARM holds it, max %d' % start)
    for start in (0, 3, 7, 1000):
        for k in (1, 2, 3, 4, 5, 8, 100, 4321):
            m.w32(MAX_SPINS, start)
            m.w32(sh + 0x18, 1)
            h = ReadHook(m, sh + 0x18, k)
            f.tcm_sem_enter(wlc)
            rec('ARM lets go after %d reads of 0x18, max %d' % (k, start), h.done())
            m.w32(sh + 0x18, 1)
            h = ReadHook(m, sh + 0x1c, k)
            f.tcm_sem_enter(wlc)
            rec('0x1c cleared after %d reads, max %d' % (k, start), h.done())
            f.tcm_sem_exit(wlc)
            rec('exit after it')
    m.w64(wlc + WLC_OL, saved)
    m.w32(MAX_SPINS, 0)
    return out


# ---------------------------------------------------------------- NVRAM
BLOCK_A = b'aa=1\0bb=22\0aab=3\0=x\0cc\0dd=\0e=5\0\0zz=9\0'
BLOCK_B = b'aa=override\0b\0bb=\0longname_with_more=v\0'
BLOCK_C = b'single=1'          # no NUL at the end: the walk stops at the length
NAMES = ('aa', 'a', 'aab', 'aa=', 'bb', 'b', 'c', 'cc', 'dd', 'e', 'zz', '', 'x', 'single',
         'single=1', 'longname_with_more', 'longname', 'boardtype', 'boardrev', 'sromrev',
         'ccode', 'devid', 'after_empty', 'first', 'last', 'q', 'k', 'p')
FILES = (
    b'a=1\nb=2\n',
    b'a=1\tb=2 c=3\n\nafter_empty=1\n',
    b'boardtype=0x0111\nboardrev=0x1310\nsromrev=11\nccode=X0\n',
    b'',
    b'x' * 100 + b'=1',
    b'=\n',
    bytes(4096),
    b'k=' + b'v' * 4094,
    b'first=1\n' + b'p' * 4100 + b'\nlast=2\n',
    b'q=1\n' * 2048,
)


def nvram_walk(m, wlc, f):
    out = []
    sih = m.r64(m.r64(wlc + WLC_HW) + WLC_HW_SIH)
    heap0 = m.heap_next
    gbuf = m.buf(0x2000)
    vars_, vlen = m.buf(8), m.buf(4)
    names = {n: m.cstr(n) for n in NAMES}

    def rel(p):
        return None if not p else p - heap0

    def rec(label, rc, extra=None):
        out.append(('nvram', label, rc, m.heap_next - heap0, extra))

    def gets(label):
        for n in NAMES:
            p = f.nvram_get(names[n])
            rec('%s: get %r' % (label, n), rel(p), m.string(p, 64) if p else None)

    def getall(label, counts):
        for c in counts:
            m.write(gbuf, b'\xee' * 0x2000)
            rc = s32(f.nvram_getall(gbuf, c & 0xffffffff))
            rec('%s: getall %d' % (label, c), rc, zlib.crc32(m.read(gbuf, 0x2000)))

    def global_vars(label):
        f.nvram_get_global_vars(vars_, vlen)
        rec(label + ': global vars', rel(m.r64(vars_)), m.r32(vlen))

    gets('after attach')
    getall('after attach', (100, 1, 0, -5))
    blocks = []
    for data in (BLOCK_A, BLOCK_B, BLOCK_C):
        a = m.buf(data)
        blocks.append(a)
        rec('append %d bytes' % len(data), s32(f.nvram_append(sih, a, len(data))))
        gets('%d blocks' % len(blocks))
        global_vars('%d blocks' % len(blocks))
    total = len(b''.join(s + b'\0' for s in
                         [b'single=1', b'aa=override', b'b', b'bb=', b'longname_with_more=v',
                          b'aa=1', b'bb=22', b'aab=3', b'=x', b'cc', b'dd=', b'e=5']))
    getall('3 blocks', (0x2000, total + 1, total, total - 1, 9, 10, 8, 1, 0, -1,
                        len(BLOCK_C) + 1, len(BLOCK_C) + len(BLOCK_B)))
    rec('init with blocks', s32(f.nvram_init(sih)))
    f.nvram_exit(sih)
    rec('exit', None)
    gets('after exit')
    getall('after exit', (16, 1, 0))
    m.os.files['nvram.txt'] = b'a=1\n'
    bus = m.r32(sih + SIH_BUSTYPE)
    for other in (0, 2, 5):
        m.w32(sih + SIH_BUSTYPE, other)
        rec('init on bus type %d' % other, s32(f.nvram_init(sih)))
    m.w32(sih + SIH_BUSTYPE, bus)
    for i, text in enumerate(FILES):
        m.os.files['nvram.txt'] = text
        rec('init file %d (%d bytes)' % (i, len(text)), s32(f.nvram_init(sih)))
        gets('file %d' % i)
        getall('file %d' % i, (0x2000, 1, 0))
        global_vars('file %d' % i)
        rec('init again, file %d' % i, s32(f.nvram_init(sih)))
        f.nvram_exit(sih)
        rec('exit, file %d' % i, None)
    del m.os.files['nvram.txt']
    rec('init without a file', s32(f.nvram_init(sih)))
    gets('without a file')
    f.nvram_exit(sih)
    rec('exit of an empty list', None)
    return out


# ---------------------------------------------------------------- modules
def module_walk(m, wlc, f):
    out = []
    pub = m.call('wlc_pub', wlc)
    table = m.r64(wlc + WLC_MODULECB)
    nopub = m.buf(8)
    names = {}

    def cstr(s):
        if s not in names:
            names[s] = m.cstr(s)
        return names[s]

    def rec(label, rc):
        t = m.read(table, NSLOTS * SLOT)
        free = sum(1 for i in range(NSLOTS) if t[i * SLOT] == 0)
        out.append(('module', label, rc, zlib.crc32(t), free))

    def reg(name, hdl, base=0x5a5a0000):
        rc = s32(f.wlc_module_register(pub, base + 1, cstr(name), hdl, base + 2, base + 3,
                                       base + 4, base + 5))
        rec('register %r hdl %#x' % (name, hdl), rc)
        return rc

    def unreg(name, hdl, p=None):
        rc = s32(f.wlc_module_unregister(pub if p is None else p, cstr(name), hdl))
        rec('unregister %r hdl %#x' % (name, hdl), rc)
        return rc

    rec('as attached', None)
    reg('test', 0x1000)
    reg('test', 0x1001)
    unreg('test', 0x2000)
    unreg('nosuch', 0x1000)
    unreg('test', 0x1001)
    reg('n' * 30, 0x1100)
    reg('n' * 31, 0x1101)
    reg('n' * 32, 0x1102)
    reg('n' * 45, 0x1103)
    unreg('n' * 45, 0x1103)
    unreg('n' * 31, 0x1103)
    unreg('n' * 31, 0x1101)
    reg('', 0x1200, 0x6b6b0000)
    reg('after empty', 0x1201)
    unreg('', 0)
    unreg('', 0x1200)
    unreg('after empty', 0x1201)
    k = 0
    while k < NSLOTS + 2:
        if reg('fill%d' % k, 0x3000 + k) != 0:
            break
        k += 1
    reg('one more', 0x4000)
    unreg('fill3', 0x3003)
    reg('into the hole', 0x4001)
    unreg('into the hole', 0x4001)
    for i in range(k):
        unreg('fill%d' % i, 0x3000 + i)
    for name, hdl in (('test', 0x1000), ('n' * 30, 0x1100), ('n' * 32, 0x1102)):
        unreg(name, hdl)
    unreg('test', 0x1000, nopub)
    rec('at the end', None)
    return out


# ---------------------------------------------------------------- PCIe bridge
SIH_BUSCORETYPE, SIH_CHIP, SIH_CHIPREV = 8, 0x3c, 0x40


def perst_walk(m, wlc, f):
    """si_survive_perst_war(sih, reset, mask, val): what it does is accesses
    to the card (compared at the end) and delays (the emulated time each call
    takes, in the records)."""
    out = []
    sih = m.r64(m.r64(wlc + WLC_HW) + WLC_HW_SIH)

    def call(label, reset, mask, val):
        t0 = m.os.now_us
        f.si_survive_perst_war(sih, reset, mask, val)
        out.append(('perst', label, reset, mask, val, m.os.now_us - t0))   # delays (us)

    for reset, mask, val in ((0, 0, 0), (0, 0x204, 0), (0, 0, 0x204), (0, 0x204, 0x204),
                             (0, 0xffffffff, 5), (0, 4, 4), (1, 0, 0), (1, 0x204, 0x200),
                             (0x100, 0x10, 0x10), (0x101, 0, 0)):
        call('as attached', reset, mask, val)
    for field, values in ((SIH_BUSCORETYPE, (0x820, 0x83b)), (SIH_CHIPREV, (3, 4, 9)),
                          (SIH_CHIP, (0x4352, 0x43a0)), (SIH_BUSTYPE, (0, 2))):
        saved = m.r32(sih + field)
        for v in values:
            m.w32(sih + field, v)
            call('sih+%#x = %#x' % (field, v), 0, 0x204, 0x200)
            call('sih+%#x = %#x, reset' % (field, v), 1, 0x8, 0)
        m.w32(sih + field, saved)
    call('as attached again', 0, 0x204, 0)
    return out


# ---------------------------------------------------------------- check
def compare(ref, got, show, names):
    diffs = 0
    for a, b in zip(ref, got):
        if a != b:
            diffs += 1
            if diffs <= show:
                print('  %s %s:\n    %s %r\n    %s %r' % (a[0], a[1], names[0], a[2:],
                                                      names[1], b[2:]))
    if len(ref) != len(got):
        diffs += 1
        print('  different number of calls: %d, %d' % (len(ref), len(got)))
    return diffs


def small_check(off=(), show=20, on=(), model=False):
    if model:
        return model_check(show)
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
    counts = {}
    for x in ra:
        counts[x[0]] = counts.get(x[0], 0) + 1
    print('small check (%s), switched on: %s, off: %s: %d accesses: %s' % (
        ', '.join('%s %d' % kv for kv in sorted(counts.items())), ', '.join(on) or '-',
        ', '.join(off) or '-', len(ta), 'the same' if not bad else 'DIFFERENT'))
    return bad


def model_check(show):
    """The coordinator's check of the specification: two runs of the object,
    one calling its functions, one a Python model written from the
    specifications (re-out/small_model.py, not part of the repository;
    implementers do not use it)."""
    import importlib.util
    import os
    path = os.path.join(ab.ROOT, 're-out', 'small_model.py')
    spec = importlib.util.spec_from_file_location('small_model', path)
    model = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(model)
    sides = []
    for use_model in (False, True):
        r = hybrid.Run(None)
        out, steps = walk(r, model.Funcs(r.m) if use_model else Funcs(r.m))
        sides.append((out, steps, r.logical()))
    (ra, sa, ta), (rb, sb, tb) = sides
    d = compare(ra, rb, show, ('object', 'model'))
    d += (sa != sb) + (ta != tb)
    print('model check: %d calls, %d accesses: %s' % (
        len(ra), len(ta), '%d differences' % d if d else 'the same'))
    return d
