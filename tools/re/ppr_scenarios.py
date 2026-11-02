#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
A/B scenario for the per-rate transmit power container (docs/re/spec/ppr.md,
open/lib/ratepwr.c): the container makes no bus access, so the results of
Broadcom's ppr_* and of the open bcm4360_ratepwr_* are compared, called with
the same arguments on the same memory.

Each side is a bare machine (the object mapped, plus the open objects on the
open side). Containers and buffers lie in slots surrounded by a random guard
pattern; after every call the whole slot (container and guard), every output
buffer, the result and - for the map functions - the sequence of callback
visits (element offsets in both containers) are compared. The callback is the
object's own (sub_172c6c of wlc_stf.c, the one wlc_update_txppr_offset passes
to ppr_map_vec_all), so it runs on both sides.

Covered: every function of ppr.md with random contents (disabled bytes and
boundary values over-represented), every bandwidth argument 0..6 and
0xffffffff on containers of bandwidth 0, 1, 2, 3 (and 7), every selector
nss 0..4, chains 0..5, mode 0..7, all pairs of bandwidths for the binary
operations, serialisation with prepared and unprepared buffers and lengths
around every limit. Left out, as ppr.md says the object's result is
undefined there: an MCS selector with nss 1 and chains 0, or nss 1, chains 3
and mode >= 4 (index past the MCS tables), and ppr_get_max of a container
with bandwidth >= 3 (reads past a 4-byte container).

    python ab.py run ppr [--show N]
"""
import random

import emu
from blob import Blob, BASES
from elfobj import Loader
from scenarios import scenario

DATA = {0: 0xb6, 1: 0x16c, 2: 0x222}     # data bytes per container bandwidth
SLOT = 0x300                              # container slot: 0x226 bytes + guard
OUT = 64                                  # output buffer slot
SER = 0x300                               # serialisation buffer slot
CALLBACK = 0x172c6c                       # sub_172c6c (wlc_stf.c), a map callback
BWS = (0, 1, 2, 3, 4, 5, 6, 0xffffffff)   # bandwidth arguments
CBWS = (0, 1, 2, 3, 7)                    # container bandwidth words
OSH = 0x1000                              # an OS handle (osl_malloc ignores it)

_blob = []


def blob():
    if not _blob:
        _blob.append(Blob(emu.default_path()))
    return _blob[0]


class Side:
    """One machine and the way to call its container functions."""

    def __init__(self, objs, open_side):
        self.open = open_side
        self.m = emu.Machine(blob=blob())
        if open_side:
            self.loader = Loader(self.m)
            self.loader.load_all(objs)
        self.slots = [self.m.malloc(SLOT) for _ in range(3)]
        self.out = self.m.malloc(OUT)
        self.ser = self.m.malloc(SER)
        self.ctx = self.m.malloc(16)
        self.optr = self.m.malloc(16)
        self.visits = []
        self.bases = (0, 0)
        self.cb = BASES['.text'] + CALLBACK
        self.m.on_call(self.cb, self._visit, 3)

    def _visit(self, m, ctx, a, b):
        self.visits.append((a - self.bases[0], b - self.bases[1]))

    def call(self, name, *args):
        f = self.loader.resolve('bcm4360_ratepwr_' + name) if self.open else 'ppr_' + name
        return self.m.call(f, *args) & 0xffffffffffffffff


def rand_bytes(rng, n, near=0):
    out = bytearray()
    for _ in range(n):
        r = rng.random()
        if r < 0.45:
            out.append(rng.randrange(256))
        elif r < 0.65:
            out.append(0x80)
        else:
            out.append(rng.choice((0x7f, 0x81, 0, 1, 0xff, near & 0xff,
                                   (near + 1) & 0xff, (near - 1) & 0xff)))
    return bytes(out)


class Case:
    """Memory set-up of one call, the same on both sides."""

    def __init__(self, rng):
        self.rng = rng
        self.guard = bytes(rng.randrange(256) for _ in range(SLOT))
        self.slot_data = {}

    def container(self, i, cbw, near=0):
        n = DATA.get(cbw, 0)
        data = cbw.to_bytes(4, 'little') + rand_bytes(self.rng, n, near)
        self.slot_data[i] = data + self.guard[len(data):]
        return self

    def load(self, side):
        for i, data in self.slot_data.items():
            side.m.write(side.slots[i], data)
        side.m.write(side.out, self.guard[:OUT])
        side.m.write(side.ser, self.guard[:SER])
        side.m.write(side.ctx, self.guard[:16])
        side.m.write(side.optr, bytes(16))
        side.visits = []


def snapshot(side):
    m = side.m
    return (tuple(m.read(a, SLOT) for a in side.slots), m.read(side.out, OUT),
            m.read(side.ser, SER), m.read(side.ctx, 16), tuple(side.visits))


def s8(v):
    v &= 0xff
    return v - 0x100 if v & 0x80 else v


def cases(seed, rounds):
    """Yields (label, case, function, argument builder) for the whole scenario.

    The argument builder gets a side and returns the argument list (addresses
    differ between the machines); `kind` tells how to read the result."""
    rng = random.Random(seed)
    P = lambda i: (lambda s: s.slots[i])
    for bw in BWS:
        yield ('size', None, 'size', lambda s, bw=bw: [bw], 'u32')
        yield ('ser_size_by_bw', None, 'ser_size_by_bw', lambda s, bw=bw: [bw], 'u32')
    yield ('get_max_bw', None, 'get_max_bw', lambda s: [], 'u32')
    for _ in range(rounds):
        for cbw in CBWS:
            c = Case(rng).container(0, cbw)
            for name in ('ser_size', 'get_ch_bw', 'clear'):
                yield (name, c, name, lambda s: [s.slots[0]], 'u32')
            for bw in BWS:
                yield ('init', c, 'init', lambda s, bw=bw: [s.slots[0], bw], 'void')
            v = rng.randrange(256)
            c = Case(rng).container(0, cbw, v)
            for name in ('set_cmn_val', 'apply_max', 'apply_min', 'plus_cmn_val',
                         'minus_cmn_val', 'cmn_val_minus', 'multiply_percentage',
                         'force_disabled', 'apply_constraint_total_tx', 'get_min'):
                yield (name, c, name, lambda s, v=v: [s.slots[0], v], 's8' if name == 'get_min'
                       else 'u32')
            if cbw <= 2:
                yield ('get_max', c, 'get_max', lambda s: [s.slots[0]], 's8')
            for bw in BWS:
                for name in ('get_max_for_bw', 'get_min_for_bw'):
                    yield (name, c, name, lambda s, bw=bw: [s.slots[0], bw], 's8')
        # binary operations, every pair of container bandwidths
        for a in CBWS:
            for b in CBWS:
                c = Case(rng).container(0, a).container(1, b)
                for name in ('copy_struct', 'compare_min', 'compare_max',
                             'apply_vector_ceiling', 'apply_vector_floor'):
                    yield (name, c, name, lambda s: [s.slots[0], s.slots[1]], 'u32')
                yield ('map_vec_all', c, 'map_vec_all',
                       lambda s: [s.cb, s.ctx, s.slots[0], s.slots[1]], 'map')
    # group access, every selector
    for cbw in CBWS:
        for bw in BWS:
            c = Case(rng).container(0, cbw).container(1, cbw)
            for chains in range(6):
                v = rng.randrange(256)
                yield ('get_dsss', c, 'get_dsss',
                       lambda s, bw=bw, ch=chains: [s.slots[0], bw, ch, s.out], 'u32')
                yield ('set_dsss', c, 'set_dsss',
                       lambda s, bw=bw, ch=chains: [s.slots[0], bw, ch, s.out], 'u32')
                yield ('set_same_dsss', c, 'set_same_dsss',
                       lambda s, bw=bw, ch=chains, v=v: [s.slots[0], bw, ch, v], 'u32')
                yield ('map_vec_dsss', c, 'map_vec_dsss',
                       lambda s, bw=bw, ch=chains: [s.cb, s.ctx, s.slots[0], s.slots[1], bw,
                                                    ch], 'map')
                for mode in range(8):
                    yield ('get_ofdm', c, 'get_ofdm',
                           lambda s, bw=bw, md=mode, ch=chains: [s.slots[0], bw, md, ch, s.out],
                           'u32')
                    yield ('set_ofdm', c, 'set_ofdm',
                           lambda s, bw=bw, md=mode, ch=chains: [s.slots[0], bw, md, ch, s.out],
                           'u32')
                    yield ('set_same_ofdm', c, 'set_same_ofdm',
                           lambda s, bw=bw, md=mode, ch=chains, v=v: [s.slots[0], bw, md, ch, v],
                           'u32')
                    yield ('map_vec_ofdm', c, 'map_vec_ofdm',
                           lambda s, bw=bw, md=mode, ch=chains: [s.cb, s.ctx, s.slots[0],
                                                                 s.slots[1], bw, md, ch], 'map')
                    for nss in range(5):
                        if nss == 1 and (chains == 0 or (chains == 3 and mode >= 4)):
                            continue        # undefined in the object (ppr.md)
                        a = lambda s, bw=bw, n=nss, md=mode, ch=chains: [s.slots[0], bw, n, md,
                                                                          ch]
                        for name in ('get_ht_mcs', 'get_vht_mcs', 'set_ht_mcs',
                                     'set_vht_mcs'):
                            yield (name, c, name, lambda s, a=a: a(s) + [s.out], 'u32')
                        for name in ('set_same_ht_mcs', 'set_same_vht_mcs'):
                            yield (name, c, name, lambda s, a=a, v=v: a(s) + [v], 'u32')
                        yield ('get_vht_mcs_min', c, 'get_vht_mcs_min',
                               lambda s, a=a: a(s) + [s.out], 'u32')
                        for name in ('map_vec_ht_mcs', 'map_vec_vht_mcs'):
                            yield (name, c, name,
                                   lambda s, a=a: [s.cb, s.ctx, s.slots[0], s.slots[1]]
                                   + a(s)[1:], 'map')
    # serialisation
    for cbw in CBWS:
        full = 8 + DATA.get(cbw, 0)
        for _ in range(rounds):
            c = Case(rng).container(0, cbw).container(1, cbw)
            for ln in (0, 7, 8, 9, full - 9, full - 8, full - 1, full, full + 1, SER - 16):
                ln = max(ln, 0)
                for bw in BWS:
                    yield ('init_ser_mem_by_bw', c, 'init_ser_mem_by_bw',
                           lambda s, bw=bw, ln=ln: [s.ser, bw, ln], 'u32')
                yield ('init_ser_mem', c, 'init_ser_mem',
                       lambda s, ln=ln: [s.ser, s.slots[0], ln], 'u32')
                yield ('serialize', c, 'serialize',
                       lambda s, ln=ln: [s.slots[0], s.ser, ln, s.optr], 'u32')
            # prepared buffers with every flag value, then the round trip
            for flags in range(8):
                prep = Case(rng).container(0, cbw).container(1, cbw)
                prep.serial = (b'\xbe\xef\xc0\xff' + flags.to_bytes(4, 'big'))
                yield ('serialize-prepared', prep, 'serialize',
                       lambda s: [s.slots[0], s.ser, SER - 16, s.optr], 'ser')
                for ln in (8, 9, full - 1, full, SER - 16):
                    yield ('deserialize', prep, 'deserialize',
                           lambda s, ln=ln: [s.slots[1], s.ser, ln], 'deser')
                    yield ('deserialize_create', prep, 'deserialize_create',
                           lambda s, ln=ln: [OSH, s.ser, ln, s.optr], 'create')


VOID = {'clear', 'init', 'set_cmn_val', 'apply_max', 'apply_min', 'plus_cmn_val',
        'minus_cmn_val', 'cmn_val_minus', 'multiply_percentage', 'copy_struct',
        'compare_min', 'compare_max', 'map_vec_all', 'map_vec_dsss', 'map_vec_ofdm',
        'map_vec_ht_mcs', 'map_vec_vht_mcs'}


def norm(side, args):
    """The arguments with the addresses of the side's buffers replaced by names."""
    names = {side.slots[0]: 'slot0', side.slots[1]: 'slot1', side.slots[2]: 'slot2',
             side.out: 'out', side.ser: 'ser', side.ctx: 'ctx', side.optr: 'optr',
             side.cb: 'callback'}
    return tuple(names.get(a, a) for a in args)


def lifecycle(side):
    """create / delete for every bandwidth, through the platform's allocator."""
    out = []
    m = side.m
    for bw in BWS:
        before = m.os.malloced
        p = side.call('create', OSH, bw)
        size = m.os.malloced - before
        data = m.read(p, size) if p else b''
        side.call('delete', OSH, p)
        out.append(('create/delete', (bw,), size, data, m.os.malloced - before))
    return out


def one(side, case, name, args, kind):
    """One call on one side; the comparable result."""
    m = side.m
    if case is not None:
        case.load(side)
        serial = getattr(case, 'serial', None)
        if serial is not None:
            m.write(side.ser, serial)
            if kind in ('deser', 'create'):
                # a serialised container to read back, made by the side's own
                # ppr_serialize (compared by the 'serialize-prepared' case first)
                side.call('serialize', side.slots[0], side.ser, SER - 16, side.optr)
    a = args(side)
    side.bases = (a[2], a[3]) if kind == 'map' else (0, 0)
    r = side.call(name, *a)
    if name in VOID or kind == 'map':
        r = None
    elif kind == 's8':
        r = s8(r)
    else:
        r &= 0xffffffff
    extra = ()
    if kind == 'create':
        p = m.r64(side.optr)
        n = 4 + DATA.get(m.r32(p), 0) if p else 0
        extra = (bool(p), m.read(p, n) if p else b'')
        m.write(side.optr, bytes(8))
        if p:
            side.call('delete', OSH, p)
    elif name == 'serialize':
        extra = (m.r32(side.optr),)
    return (norm(side, a), r, extra, snapshot(side))


def run(sides, seed, rounds, show=0):
    """All cases on each side in lockstep; returns (results, differences)."""
    count = diffs = 0
    life = [lifecycle(s) for s in sides]
    if len(sides) == 2:
        for x, y in zip(*life):
            if x != y:
                diffs += 1
                if show and diffs <= show:
                    print('  create/delete bw %#x: object %d bytes, open %d bytes%s' % (
                        x[1][0], x[2], y[2], '' if x[3:] == y[3:] else ', contents differ'))
    count += len(life[0])
    for label, case, name, args, kind in cases(seed, rounds):
        res = [one(s, case, name, args, kind) for s in sides]
        count += 1
        if len(res) == 2 and res[0] != res[1]:
            diffs += 1
            if show and diffs <= show:
                a, b = res
                what = [k for k, i in (('result', 1), ('outputs', 2), ('memory', 3))
                        if a[i] != b[i]]
                if a[3][4] != b[3][4]:
                    what.append('callback visits')
                print('  %s %s: %s differ (object result %r, open result %r)' % (
                    label, [hex(x) if isinstance(x, int) else x for x in a[0]],
                    ', '.join(what), a[1], b[1]))
    return count, diffs


@scenario
def ppr(objs, show, seed=4360, rounds=3):
    """Per-rate power container: every function, selector and bandwidth pair (ppr.md)."""
    ref = Side(None, False)
    try:
        got = Side(objs, True)
        got.loader.resolve('bcm4360_ratepwr_create')
    except Exception as e:
        return {'stages': 0, 'ref': 0, 'got': 0, 'differences': 1,
                'note': 'open code not callable (%s); built yet?' % str(e).splitlines()[0]}
    try:
        count, diffs = run([ref, got], seed, rounds, show)
    except Exception as e:
        return {'stages': 0, 'ref': 0, 'got': 0, 'differences': 1,
                'note': 'a call failed: %s' % str(e).splitlines()[0]}
    return {'stages': count, 'ref': count, 'got': count, 'differences': diffs}

if __name__ == '__main__':
    import time
    t0 = time.time()
    count, _ = run([Side(None, False)], 4360, 1)
    print('object side: %d calls in %.1f s' % (count, time.time() - t0))
