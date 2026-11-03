#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Direct calls of the spatial-policy functions of wlc_stf.c (cca-stf.md part 2)
on the object alone and on the hybrid core (hybrid.py stf): after attach, up
and a channel, the stf block and the fields the functions read are set at
random per case - stream count, chains, overrides, stream counts of the
classes, ceiling, policies, the container's offsets, PHY and band type,
boardflags2, clk, the FIFOs' pending counts, the shared-memory base, core
revision, vendor and chip - and one of the functions is called. Results, the
whole stf block, the fields written outside it and every access to the card
are compared.

The sessions only reach the auto policy with two streams; this covers the
rest of the specification's text.
"""
import random
import struct

import ab
import hybrid
from emu import s32

WLC_PUB, WLC_CLK, WLC_CORESTATE, WLC_BAND = 0x00, 0x31, 0x38, 0x40
WLC_TXC_GEN, WLC_FLAGS_304, WLC_CHANSPEC, WLC_STF, WLC_BCN_RATE = 0x2a8, 0x304, 0x518, 0x550, 0x584
PUB_COREREV, PUB_UP, PUB_BFL2_HI, PUB_E4, PUB_SIH = 0x14, 0x34, 0x9d, 0xe4, 0x100
SIH_VENDOR, SIH_CHIP = 0x30, 0x3c
BAND_TYPE, BAND_PHYTYPE, BAND_SS = 0x00, 0x08, 0x58
STF_SIZE = 0x120
FUNCS = ('wlc_stf_spatial_policy_set', 'wlc_stf_spatialpolicy_set_complete',
         'wlc_stf_txcore_shmem_write', 'wlc_stf_ss_update', 'wlc_stf_ss_algo_channel_get')
PPR_SIZE = {0: 0xba, 1: 0x170, 2: 0x226}       # ppr.md: data bytes by ch_bw


class Fields:
    """Remember fields outside the stf block before a case, put them back after."""

    def __init__(self, m):
        self.m, self.saved = m, []

    def set(self, addr, size, value):
        self.saved.append((addr, size, self.m.read(addr, size)))
        self.m.write(addr, int(value).to_bytes(size, 'little', signed=value < 0))

    def restore(self):
        for addr, size, old in reversed(self.saved):
            self.m.write(addr, old)
        self.saved = []


def case(r, rng, k):
    m, wlc = r.m, r.d.wlc
    stf = m.r64(wlc + WLC_STF)
    pub = m.r64(wlc + WLC_PUB)
    band = m.r64(wlc + WLC_BAND)
    sih = m.r64(pub + PUB_SIH)
    cs_block = m.r64(wlc + WLC_CORESTATE)
    f = Fields(m)
    # the stf block: what the functions read
    w8 = lambda off, v: m.write(stf + off, bytes([v & 0xff]))
    w8(0x01, rng.choice((1, 2, 3, 3, 3, 4, 5, 6, 7, 0)))           # txchain
    w8(0x02, rng.choice((1, 2, 2, 2, 3)))                          # T
    for i in range(6):
        w8(0x22 + i, rng.choice((0, 0, 0, 0, rng.randrange(1, 8))))  # overrides
        if rng.randrange(8) == 0:
            w8(0x16 + 2 * i, rng.randrange(0, 5))                  # nsts of the class
    w8(0x88, rng.randrange(32))                                    # ceiling
    w8(0x8a, rng.choice((0, 0, 1)))                                # core1_first
    w8(0x28, rng.choice((0xff, 0xff, 0, 1, 2, 3)))                 # spatial_policy
    w8(0x29, rng.choice((0xff, 0, 5)))                             # pending
    w8(0x0c, rng.randrange(2)); w8(0x0d, rng.randrange(2)); w8(0x10, rng.choice((0, 0, 1)))
    w8(0x44, rng.randrange(2))
    m.write(stf + 0x0e, struct.pack('<H', rng.choice((0xffff, 0, 1, 2, 3, 6))))
    base = m.read(pub + PUB_E4, 4)
    m.write(stf + 0x2a, rng.choice((base[0:2], base[2:4], b'\x00\x01')))
    # the container's offsets
    p = m.r64(stf + 0x30)
    if p and rng.randrange(4):
        bw = m.r32(p)
        n = PPR_SIZE.get(bw, 0)
        if n:
            m.write(p + 4, bytes(rng.choice((0x80, rng.randrange(0, 40), rng.randrange(256)))
                                 for _ in range(n)))
    # fields outside the block
    f.set(band + BAND_PHYTYPE, 2, rng.choice((0xb, 0xb, 0xb, 7, 3)))
    f.set(band + BAND_TYPE, 4, rng.choice((1, 2)))
    f.set(band + BAND_SS, 1, rng.randrange(2))
    f.set(pub + PUB_BFL2_HI, 1, rng.choice((0, 0x10, 0x40, 0x50)))
    f.set(pub + PUB_COREREV, 4, rng.choice((42, 42, 42, 25, 26, 40)))
    f.set(pub + PUB_UP, 1, rng.choice((1, 1, 0)))
    f.set(wlc + WLC_CLK, 1, rng.choice((1, 1, 0)))
    f.set(sih + SIH_VENDOR, 4, rng.choice((0x106b, 0x106b, 0x14e4)))
    f.set(sih + SIH_CHIP, 4, rng.choice((0x4360, 0x4360, 0x4331, 0x4352)))
    f.set(wlc + WLC_BCN_RATE, 4, rng.choice((2, 12, 0x02000002, 0x01000005, 11)))
    for i in range(4):
        f.set(cs_block + 0x38 + 2 * i, 2, rng.choice((0, 0, 0, 1, -1, 3)))
    fn = rng.choice(FUNCS)
    if fn == 'wlc_stf_spatial_policy_set':
        res = s32(m.call(fn, wlc, rng.choice((-1, -1, 0, 1, 2, 0x1ff))))
    elif fn == 'wlc_stf_spatialpolicy_set_complete':
        m.call(fn, wlc)
        res = None                       # void
    elif fn == 'wlc_stf_txcore_shmem_write':
        m.call(fn, wlc, rng.randrange(2))
        res = None                       # void
    elif fn == 'wlc_stf_ss_update':
        bs = rng.choice((band, m.r64(wlc + 0x50), m.r64(wlc + 0x58)))
        res = s32(m.call(fn, wlc, bs))
    else:
        out = m.buf(4)
        m.write(out, b'\x55\x55\x55\x55')
        cs = rng.choice((0xd024, 0xd826, 0xe02a, 0x1001, 0x1803))
        m.call(fn, wlc, out, cs)
        res = m.read(out, 4)
    state = (m.read(stf, STF_SIZE), m.r32(wlc + WLC_TXC_GEN), m.read(wlc + WLC_FLAGS_304, 1))
    f.restore()
    return (k, fn, res, state)


def stf_check(off=(), show=20, on=(), cases=3000, seed=7):
    core = hybrid.build(quiet=True)
    results = []
    for path, o, x in ((None, (), ()), (core, on, off)):
        r = hybrid.Run(path, on=o, off=x)
        r.session(['attach', 'mpc 0', 'up', '36/80', 'watchdog'])
        n0 = len(r.s.chip.trace)
        rng = random.Random(seed)
        out = [case(r, rng, k) for k in range(cases)]
        acc = ab.logical(r.s.chip.trace[n0:], r.s.chip.func_name)
        results.append((out, acc))
    (want, wacc), (have, hacc) = results
    diff = [i for i, (a, b) in enumerate(zip(want, have)) if a != b]
    for i in diff[:show]:
        a, b = want[i], have[i]
        print('case %d %s: result object %r, hybrid %r' % (a[0], a[1], a[2], b[2]))
        for j, (x, y) in enumerate(zip(a[3], b[3])):
            if x != y:
                if isinstance(x, bytes):
                    pos = [q for q in range(len(x)) if x[q] != y[q]]
                    print('   state %d: offsets %s' % (j, ', '.join(
                        '%#x %02x/%02x' % (q, x[q], y[q]) for q in pos[:8])))
                else:
                    print('   state %d: object %r, hybrid %r' % (j, x, y))
    bad = len(diff)
    if wacc != hacc:
        bad += 1
        ab.compare(wacc, hacc, show)
    calls = {}
    for c in want:
        calls[c[1]] = calls.get(c[1], 0) + 1
    print('stf check, switched on: %s, off: %s: %d cases (%s), %d accesses of the object, '
          '%d of the hybrid core: %s'
          % (', '.join(on) or '-', ', '.join(off) or '-', len(want),
             ', '.join('%s %d' % (k[8:], v) for k, v in sorted(calls.items())), len(wacc),
             len(hacc), 'the same' if not bad else 'DIFFERENT (%d cases)' % len(diff)))
    return bad
