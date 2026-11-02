#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
A/B check of the iovar dispatcher (docs/re/spec/iovar.md, open/lib/iovar.c,
hybrid/iovar.c).

The object alone and the hybrid core are brought to the same state (attach,
up) and wlc_iovar_op is called on both with the same arguments - on the
hybrid core it arrives in the adapter and the open code, which hands the
request to Broadcom's own check and module handlers. Compared after each call:
the result, the value buffer, the two fields a set updates (iovar.md, "Fields
updated after a set"); at the end every access to the card.

The calls (names from the module table of the object, in table order):
* a get of every name: without an input buffer; with one; with value
  buffers of 4 and 0 bytes;
* a set of the value just read, for every name whose get succeeded;
* the "bsscfg:" prefix: a get of every name with index 0, with an index that
  does not exist and with an input buffer too short; a set with index 0 for
  the names that could be set;
* other colons, unknown and empty names.

    python hybrid.py iovar [--off iovar] [--show N]
"""
import zlib

import ab
import hybrid

WLC_MODULECB, MODULECB_SIZE, MODULECB_IOVARS = 0x358, 0x50, 0x20
WLC_TXC_VALID, WLC_TXC_GEN = 0x2a3, 0x2a8
BUFSIZE = 4096
IOVT_BUFFER = 8


def table(m, wlc):
    """[(slot, name, type)] of the module table, in lookup order."""
    out = []
    mods = m.r64(wlc + WLC_MODULECB)
    for slot in range(64):
        t = m.r64(mods + slot * MODULECB_SIZE + MODULECB_IOVARS)
        k = 0
        while t:
            p = m.r64(t + 16 * k)
            if not p:
                break
            out.append((slot, m.string(p), m.r16(t + 16 * k + 12)))
            k += 1
    return out


def walk(r, call):
    """Run the calls on the machine of r; call(wlc, name, params, p_len, arg,
    len, set, wlcif) -> raw result of wlc_iovar_op. Returns the records and
    the results of the steps."""
    steps = [(s, r.step(s)) for s in ('attach', 'mpc 0', 'up')]
    m, wlc = r.m, r.d.wlc
    buf, pbuf = m.buf(BUFSIZE), m.buf(64)
    names = {}
    out = []

    def cstr(s):
        if s not in names:
            names[s] = m.cstr(s)
        return names[s]

    def op(label, name, value=b'', length=BUFSIZE, set_=0, params=None):
        m.write(buf, value + bytes(BUFSIZE - len(value)))
        if params is not None:
            m.write(pbuf, params + bytes(64 - len(params)))
        p, plen = (pbuf, len(params)) if params is not None else (0, 0)
        rc = ab_s32(call(wlc, cstr(name), p, plen, buf, length, set_, 0))
        got = m.read(buf, BUFSIZE)
        out.append((label, name, rc, zlib.crc32(got), got[:16].hex(),
                    m.r8(wlc + WLC_TXC_VALID), m.r32(wlc + WLC_TXC_GEN)))
        return rc, got

    tab = table(m, wlc)
    values = {}
    for slot, name, typ in tab:
        rc, got = op('get', name)
        if rc == 0:
            values[name] = got if typ == IOVT_BUFFER else got[:4]
        op('get with input', name, params=b'\x01\0\0\0\x02\0\0\0')
        op('get, 4 bytes', name, length=4)
        op('get, 0 bytes', name, length=0)
    settable = []
    for slot, name, typ in tab:
        if name in values:
            v = values[name]
            rc, _ = op('set back', name, v, len(v), 1)
            if rc == 0:
                settable.append(name)
    for slot, name, typ in tab:
        op('bsscfg 0 get', 'bsscfg:' + name, params=b'\0\0\0\0')
        op('bsscfg 7 get', 'bsscfg:' + name, params=b'\x07\0\0\0')
        op('bsscfg short', 'bsscfg:' + name, params=b'\0\0')
    for name in settable:
        v = values[name][:BUFSIZE - 4]
        op('bsscfg 0 set', 'bsscfg:' + name, b'\0\0\0\0' + v, 4 + len(v), 1)
    for slot, name, typ in tab[:40]:
        op('other colon', 'foo:' + name)
    for name in ('a:b:mpc', 'bsscfg:', 'mpc:', '', 'nosuch', 'MPC', 'mp', 'mpcx',
                 'bsscfg:bsscfg:mpc', 'bsscfg'):
        op('odd name', name)
        op('odd name, set', name, b'\0\0\0\0', 4, 1)
    return out, steps


def ab_s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def compare(ref, got, show, names):
    diffs = 0
    for a, b in zip(ref, got):
        if a != b:
            diffs += 1
            if diffs <= show:
                print('  %s %r: %s result %d, buffer %s, txc %d/%d; %s result %d, buffer %s, '
                      'txc %d/%d' % (a[0], a[1], names[0], a[2], a[4], a[5], a[6],
                                     names[1], b[2], b[4], b[5], b[6]))
    if len(ref) != len(got):
        diffs += 1
        print('  different number of calls: %d, %d' % (len(ref), len(got)))
    return diffs


def iovar_check(off=(), show=20, on=(), model=False):
    if model:
        return model_check(show)
    core = hybrid.build(quiet=True)
    sides = []
    for path, o, n in ((None, (), ()), (core, off, on)):
        r = hybrid.Run(path, off=o, on=n)
        m = r.m
        out, steps = walk(r, lambda *a, m=m: m.call('wlc_iovar_op', *a))
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
    print('iovar check, switched on: %s, off: %s: %d calls, %d accesses: %s' % (
        ', '.join(on) or '-', ', '.join(off) or '-', len(ra), len(ta),
        'the same' if not bad else 'DIFFERENT'))
    return bad


def model_check(show):
    """The coordinator's check of the specification: two runs of the object,
    one calling wlc_iovar_op, one a Python model written from iovar.md
    (re-out/iovar_model.py, not part of the repository; implementers do not
    use it) that calls the object's check, BSS lookup and handlers."""
    import importlib.util
    import os
    path = os.path.join(ab.ROOT, 're-out', 'iovar_model.py')
    spec = importlib.util.spec_from_file_location('iovar_model', path)
    model = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(model)
    sides = []
    for use_model in (False, True):
        r = hybrid.Run(None)
        m = r.m
        model.SCRATCH = m.buf(64)      # on both runs, so that their heaps stay alike
        if use_model:
            out, steps = walk(r, lambda *a, m=m: model.op(m, *a))
        else:
            out, steps = walk(r, lambda *a, m=m: m.call('wlc_iovar_op', *a))
        sides.append((out, steps, r.logical()))
    (ra, sa, ta), (rb, sb, tb) = sides
    d = compare(ra, rb, show, ('object', 'model'))
    d += (sa != sb) + (ta != tb)
    print('model check: %d calls, %d accesses: %s' % (
        len(ra), len(ta), '%d differences' % d if d else 'the same'))
    return d
