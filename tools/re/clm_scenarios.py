#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
A/B scenario for the regulatory database interpreter (docs/re/spec/clm.md,
open/lib/clm.c): the CLM makes no bus access, so instead of traces the
outputs of Broadcom's clm_* functions and of the open bcm4360_clm_* are
compared, called with the same arguments on the same database (the object's
clm_header, in the memory of both machines).

Covered: every country of the database (iteration, lookup, definition,
advertised code, flags and channel vectors for both bands, aggregate lookup),
and for every distinct set of locales: the regulatory limit and the 336
per-rate limits of every valid 20 MHz channel, for limits type 0 and the
default parameters, plus the four categories on the first channel.

    python ab.py run clm [--show N]
"""
import struct

import ab
from scenarios import scenario

LOC_SIZE = 20          # struct clm_country_locales (18 bytes written)
STATE_SIZE = 1 << 20   # room for struct bcm4360_clm


class Side:
    """One machine and the way to call its CLM."""

    def __init__(self, objs, open_side):
        self.open = open_side
        self.s = ab.Session(objs if open_side else None)
        self.m = self.s.m
        self.hdr = self.m.blob.byname['clm_header'][0].addr
        self.state = self.m.buf(STATE_SIZE) if open_side else None
        self.buf = {k: self.m.buf(n) for k, n in
                    (('iter', 8), ('cc', 8), ('rev', 8), ('id', 8), ('loc', LOC_SIZE),
                     ('flags', 8), ('valid', 32), ('restr', 32), ('lim', 344),
                     ('reg', 8), ('p', 16))}

    def call(self, name, *args):
        if self.open:
            f = self.s.sym('bcm4360_' + name)
            stateless = name in ('clm_iter_init', 'clm_limits_params_init')
            args = args if stateless else (self.state,) + args
            r = self.m.call(f, *args)
        else:
            r = self.m.call(name, *args)
        return ab_s32(r)


def ab_s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def run_side(side, limit_sets=None):
    """All calls of the scenario on one side; returns the list of results."""
    m, b = side.m, side.buf
    out = []
    out.append(('init', side.call('clm_init', side.hdr)))
    m.w32(b['iter'], 0)
    side.call('clm_iter_init', b['iter'])
    countries = []
    while True:
        r = side.call('clm_country_iter', b['iter'], b['cc'], b['rev'])
        cc, rev = m.read(b['cc'], 2), m.r32(b['rev'])
        out.append(('iter', r, cc if r == 0 else b'', rev if r == 0 else 0))
        if r != 0:
            break
        countries.append((cc, rev))
    sets = {}
    for cc, rev in countries:
        m.write(b['cc'], cc)
        r = side.call('clm_country_lookup', b['cc'], rev, b['id'])
        cid = m.r32(b['id'])
        out.append(('lookup', cc, rev, r, cid if r == 0 else 0))
        if r != 0:
            continue
        m.write(b['loc'], b'\xee' * LOC_SIZE)
        r = side.call('clm_country_def', cid, b['loc'])
        loc = m.read(b['loc'], 18)
        out.append(('def', cc, rev, r, loc))
        r = side.call('clm_country_advertised_cc', cid, b['cc'])
        out.append(('adv', cc, rev, r, m.read(b['cc'], 2)))
        m.write(b['cc'], cc)
        r = side.call('clm_agg_country_lookup', b['cc'], rev, b['id'])
        out.append(('agg', cc, rev, r, m.r32(b['id']) if r == 0 else 0))
        sets.setdefault(loc, cc + struct.pack('<I', rev))
    for loc in sorted(sets)[:limit_sets]:
        m.write(b['loc'], loc + b'\0\0')
        for band in (0, 1):
            m.w64(b['flags'], 0)
            r = side.call('clm_country_flags', b['loc'], band, b['flags'])
            out.append(('flags', loc, band, r, m.r64(b['flags'])))
            m.write(b['valid'], bytes(32))
            m.write(b['restr'], bytes(32))
            r = side.call('clm_country_channels', b['loc'], band, b['valid'], b['restr'])
            valid = m.read(b['valid'], 25)
            out.append(('chan', loc, band, r, valid, m.read(b['restr'], 25)))
            chans = [c for c in range(200) if valid[c >> 3] >> (c & 7) & 1]
            for i, ch in enumerate(chans):
                r = side.call('clm_regulatory_limit', b['loc'], band, ch, b['reg'])
                out.append(('reg', loc, band, ch, r, m.r32(b['reg']) if r == 0 else 0))
                for cat in ((0, 1, 2, 3) if i == 0 else (0,)):
                    side.call('clm_limits_params_init', b['p'])
                    m.w32(b['p'] + 8, cat)
                    r = side.call('clm_limits', b['loc'], band, ch, 0, 0, b['p'], b['lim'])
                    out.append(('limits', loc, band, ch, cat, r, m.read(b['lim'], 336)))
    return out


def compare(ref, got, show):
    diffs = 0
    for a, g in zip(ref, got):
        if a != g:
            diffs += 1
            if show and diffs <= show:
                print('  object: %r\n  open:   %r' % (a[:6], g[:6]))
    diffs += abs(len(ref) - len(got))
    return diffs


@scenario
def clm(objs, show, limit_sets=None):
    """Regulatory database: every country, every distinct locale set (clm.md)."""
    ref = run_side(Side(None, False), limit_sets)
    try:
        got = run_side(Side(objs, True), limit_sets)
    except Exception as e:
        return {'stages': len(ref), 'ref': len(ref), 'got': 0, 'differences': len(ref),
                'note': 'open code not callable (%s); built yet?' % str(e).splitlines()[0]}
    return {'stages': len(ref), 'ref': len(ref), 'got': len(got),
            'differences': compare(ref, got, show)}


if __name__ == '__main__':
    import time
    t0 = time.time()
    out = run_side(Side(None, False), 5)
    print('object side: %d results for 5 locale sets in %.1f s' % (len(out), time.time() - t0))
