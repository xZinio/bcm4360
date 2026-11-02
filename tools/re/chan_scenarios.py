#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
A/B check of the chanspec helpers and the channel validity checks
(docs/re/spec/channel.md, open/lib/channel.c, hybrid/chan.c).

These functions make no access to the card, so the hybrid core and
Broadcom's object alone are compared by their results: both are brought to
the same state (attach, up), and the functions of the specification are
called by their names - on the hybrid core they arrive in the adapter and the
open code - with the same arguments:

* the pure chanspec helpers for every chanspec (H1-H4), every (centre,
  primary) pair (H5), channels 0..255 and more with three start factors (H6);
* the validity checks (V1-V7) for every channel of both bands and the
  bandwidth/sideband combinations, in many states: nine countries
  (wlc_set_countrycode), both current bands (a channel change), band lock on
  and off, and variations of the capability fields of channel.md C4 (written
  into Broadcom's structures on both machines alike, and put back).

The channel changes and country settings in between are compared like the
session of hybrid.py: their results and every access to the card.

    python hybrid.py chan [--off chan] [--show N]
"""
import ab
import hybrid

COUNTRIES = ['US', 'JP', 'DE', 'CN', 'ALL', 'KR', '#a', 'Q1', 'XZ']
CURRENT = ['1', '36']                   # channel changes: 2.4 GHz, then 5 GHz current

# (bandwidth, sideband) of the chanspecs given to the validity checks; the
# sideband matters only to wf_chspec_malformed, which is checked on its own
CHANSPEC_KINDS = [(0x1000, 0), (0x1800, 0), (0x1800, 0x100), (0x2000, 0), (0x2000, 0x300),
                  (0x2800, 0), (0x3000, 0)]

# channel.md C4, offsets in Broadcom's structures (as hybrid/chan.c)
CM_COUNTRY, CM_BAND, CM_BAND_SIZE = 0x18, 0x28, 0x30
WLC_PUB, WLC_BAND, WLC_BANDSTATE, WLC_BANDLOCKED, WLC_CM = 0x00, 0x40, 0x50, 0x24e, 0x1a0
BAND_BW_CAP = 0xf1
PUB_VHT_ENAB, PUB_BW40, PUB_BW80, PUB_VHT_FEATURES = 0x6a, 0x6d, 0x6e, 0xf0

BOOL, U8, CHANSPEC, S32 = 0xff, 0xff, 0xffff, None


class Where:
    """The addresses of the fields of C4 on one machine."""

    def __init__(self, r):
        m, wlc = r.m, r.d.wlc
        self.cm = m.r64(wlc + WLC_CM)
        pub = m.r64(wlc + WLC_PUB)
        bs = [m.r64(wlc + WLC_BANDSTATE + 8 * b) for b in (0, 1)]
        # field: (address, width in bytes)
        self.fields = {
            'bandlocked': (wlc + WLC_BANDLOCKED, 1),
            'vht_enab': (pub + PUB_VHT_ENAB, 1),
            'bw40_capable': (pub + PUB_BW40, 1),
            'bw80_capable': (pub + PUB_BW80, 1),
            'vht_features': (pub + PUB_VHT_FEATURES, 2),
            'bw_cap0': (bs[0] + BAND_BW_CAP, 1),
            'bw_cap1': (bs[1] + BAND_BW_CAP, 1),
            'locale_flags0': (self.cm + CM_BAND, 1),
            'locale_flags1': (self.cm + CM_BAND + CM_BAND_SIZE, 1),
        }

    def get(self, m, f):
        a, n = self.fields[f]
        return m.r8(a) if n == 1 else m.r16(a)

    def put(self, m, f, v):
        a, n = self.fields[f]
        if n == 1:
            m.w8(a, v & 0xff)
        else:
            m.write(a, (v & 0xffff).to_bytes(2, 'little'))


# variations of the capability fields: field -> function of the old value
VARIANTS = [
    ('as set', {}),
    ('band lock', {'bandlocked': lambda v: 1}),
    ('no 40 MHz capability', {'bw40_capable': lambda v: 0}),
    ('no 80 MHz capability', {'bw80_capable': lambda v: 0}),
    ('VHT off', {'vht_enab': lambda v: 0}),
    ('VHT feature 0x100 flipped', {'vht_features': lambda v: v ^ 0x100}),
    ('all bandwidths in both bands', {'bw_cap0': lambda v: 7, 'bw_cap1': lambda v: 7}),
    ('20/40 only', {'bw_cap0': lambda v: 3, 'bw_cap1': lambda v: 3}),
    ('no bandwidth', {'bw_cap0': lambda v: 0, 'bw_cap1': lambda v: 0}),
    ('locale bit 0x08 flipped', {'locale_flags0': lambda v: v ^ 0x08,
                                 'locale_flags1': lambda v: v ^ 0x08}),
    ('locale bit 0x04 flipped', {'locale_flags0': lambda v: v ^ 0x04,
                                 'locale_flags1': lambda v: v ^ 0x04}),
    ('locale bit 0x10 set', {'locale_flags0': lambda v: v | 0x10,
                             'locale_flags1': lambda v: v | 0x10}),
    ('band lock, all bandwidths', {'bandlocked': lambda v: 1, 'bw_cap0': lambda v: 7,
                                   'bw_cap1': lambda v: 7}),
]


def pure_batches():
    """(function, argument tuples, result mask) of H1-H6."""
    every = [(cs,) for cs in range(0x10000)]
    yield 'wf_chspec_malformed', every, BOOL
    yield 'wf_chspec_ctlchan', every, U8
    yield 'wf_chspec_ctlchspec', every, CHANSPEC
    yield 'wf_chspec_primary40_chspec', every, CHANSPEC
    yield 'wf_chspec_80', [(c, p) for c in range(256) for p in range(256)], CHANSPEC
    chans = list(range(256)) + [1000, 0x7fffffff, 0xffffffff]
    yield 'wf_channel2mhz', [(c, f) for f in (4814, 10000, 0, 4813) for c in chans], S32


def validity_batches(cm):
    """(function, argument tuples, result mask) of V1-V6 for one state."""
    chans = range(256)
    yield 'wlc_valid_channel20_in_band', [(cm, b, c) for b in (0, 1) for c in chans], BOOL
    yield 'wlc_valid_channel20', [(cm, c) for c in chans], BOOL
    yield 'wlc_valid_channel20_db', [(cm, c) for c in chans], BOOL
    yield 'wlc_valid_40chanspec_in_band', [(cm, 0), (cm, 1)], BOOL
    yield 'wlc_valid_80chanspec_in_band', [(cm, 0), (cm, 1)], BOOL
    specs = [(cm, band | bw | sb | c) for band in (0, 0xc000) for bw, sb in CHANSPEC_KINDS
             for c in chans]
    yield 'wlc_valid_chanspec', specs, BOOL
    yield 'wlc_valid_chanspec_db', specs, BOOL


def walk(r, call):
    """Bring the machine of r through the states and evaluate every batch with
    call(name, args) -> raw result. Returns [(state, name, args, results)] and
    the results of the state changes."""
    m = r.m
    out = []
    steps = []

    def run(state, batches):
        for name, args, mask in batches:
            res = []
            for a in args:
                v = call(name, a)
                res.append(ab_s32(v) if mask is None else v & mask)
            out.append((state, name, args, res))

    for s in ('attach', 'mpc 0', 'up'):
        steps.append((s, r.step(s)))
    w = Where(r)
    run('after up', pure_batches())
    for cc in [None] + COUNTRIES:
        if cc:
            steps.append(('country ' + cc, m.call('wlc_set_countrycode', w.cm, m.cstr(cc))
                          & 0xffffffff))
        for chan in CURRENT:
            steps.append(('channel ' + chan, r.step(chan)))
            for label, change in VARIANTS:
                old = {f: w.get(m, f) for f in change}
                for f, fn in change.items():
                    w.put(m, f, fn(old[f]))
                run('country %s, channel %s, %s' % (cc or 'as attached', chan, label),
                    validity_batches(w.cm))
                for f, v in old.items():
                    w.put(m, f, v)
    return out, steps


def ab_s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def compare(ref, got, show, names=('object', 'open')):
    diffs = 0
    shown = 0
    for (state, name, args, a), (_, _, _, b) in zip(ref, got):
        if a == b:
            continue
        for x, y, arg in zip(a, b, args):
            if x != y:
                diffs += 1
                if shown < show:
                    shown += 1
                    shown_args = arg[1:] if name.startswith('wlc_') else arg   # not the cm pointer
                    print('  %s: %s(%s): %s %s, %s %s' % (
                        state, name, ', '.join(hex(v) for v in shown_args),
                        names[0], x, names[1], y))
    if len(ref) != len(got):
        diffs += 1
        print('  different number of batches: %d, %d' % (len(ref), len(got)))
    return diffs


def chan_check(off=(), show=20, on=(), model=False):
    if model:
        return model_check(show)
    core = hybrid.build(quiet=True)
    sides = []
    for path, o, n in ((None, (), ()), (core, off, on)):
        r = hybrid.Run(path, off=o, on=n)
        m = r.m
        out, steps = walk(r, lambda name, a, m=m: m.call(name, *a))
        sides.append((out, steps, r.logical()))
    (ra, sa, ta), (rb, sb, tb) = sides
    bad = 0
    calls = sum(len(x[2]) for x in ra)
    d = compare(ra, rb, show, ('object', 'hybrid'))
    if d:
        print('%d of %d results differ' % (d, calls))
        bad += 1
    if sa != sb:
        bad += 1
        for (w, x), (_, y) in zip(sa, sb):
            if x != y:
                print('  step %-14s object %s, hybrid %s' % (w, x, y))
    if ta != tb:
        bad += 1
        ab.compare(ta, tb, show)
    print('chan check, switched on: %s, off: %s: %d calls in %d states, %d steps, %d accesses: %s'
          % (', '.join(on) or '-', ', '.join(off) or '-', calls,
             len(set(x[0] for x in ra)), len(sa), len(ta), 'the same' if not bad else 'DIFFERENT'))
    return bad


def model_check(show):
    """The coordinator's check of the specification: the object against a
    Python model written from channel.md (re-out/chan_model.py, not part of
    the repository; implementers do not use it)."""
    import importlib.util
    import os
    path = os.path.join(ab.ROOT, 're-out', 'chan_model.py')
    spec = importlib.util.spec_from_file_location('chan_model', path)
    model = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(model)
    r = hybrid.Run(None)
    m = r.m
    got = []

    def both(name, a):
        v = m.call(name, *a)
        got.append(model.call(m, name, a))
        return v

    ref, _ = walk(r, both)
    it = iter(got)
    mod = [(s, n, a, [ab_s32(next(it)) if mask is None else next(it) & mask for _ in a])
           for (s, n, a, _), mask in zip(ref, masks_of(ref))]
    d = compare(ref, mod, show, ('object', 'model'))
    print('model check: %d calls, %s' % (len(got), '%d differences' % d if d else 'the same'))
    return d


def masks_of(batches):
    kinds = {}
    for name, _, mask in pure_batches():
        kinds[name] = mask
    for name, _, mask in validity_batches(0):
        kinds[name] = mask
    return [kinds[n] for _, n, _, _ in batches]
