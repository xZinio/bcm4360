#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
The DMA engine with frames: Broadcom's engine (the object alone) and the
hybrid core's (open/mac/dma.c through hybrid/dma.c) are driven through the
same transmit and receive traffic, against a model of the DMA channels that
answers as the real card does, and every result, register access,
descriptor ring and packet is compared (hybrid.py dma).

Why: hybrid.py's sessions attach, initialise, fill and reclaim the engines
but never move a frame, and the card model answered the channel probe of
dma_attach so that every descriptor index computed to 0 (masks 0xf;
bmac-data.md D7). The real card (run 1 of scripts/card-test.sh, 2026-11-03)
reads back XB = 0xfffffff0 after the probe's 0xffffffff and XP = 0xfff0, so
the masks are 0xffff and the indices are real.

The channel model (DmaHw): the low 4 bits of the ring base registers read 0,
the pointer registers keep 16 bits and read 0xfff0 before the engine writes
them, status 0 and 1 hold the current/active descriptor as the low 16 bits of
its bus address and the state in bits 28..31. The hardware side is moved by
the scenario itself: it completes transmit descriptors, delivers received
frames into the posted buffers (16 bit length at the start of the receive
status, rxoffset bytes before the frame), sets states and errors.
"""
import random
import struct

import ab
import hybrid
from emu import s32

D11_TX0, D11_RX0 = 0x200, 0x220         # FIFO 0 (bmac-data.md D7)
R_CTRL, R_PTR, R_ADDRLO, R_ADDRHI, R_ST0, R_ST1 = 0x00, 0x04, 0x08, 0x0c, 0x10, 0x14
ST_DISABLED, ST_ACTIVE, ST_IDLE, ST_STOPPED, ST_SUSP = 0, 1, 2, 3, 4
RXOFFSET = 40

SLOT = dict(detach=0x000, txinit=0x008, txreset=0x010, txenabled=0x018, txsuspend=0x020,
            txresume=0x028, txsuspended=0x030, txsuspendedidle=0x038, txfast=0x040,
            txunframed=0x048, getpos=0x050, txstopped=0x058, txreclaim=0x060,
            getnexttxp=0x068, peeknexttxp=0x070, peekntxp=0x078, txblock=0x080,
            txunblock=0x088, txactive=0x090, txrotate=0x098, rxinit=0x0a0, rxreset=0x0a8,
            rxidle=0x0b0, rxstopped=0x0b8, rxenable=0x0c0, rxenabled=0x0c8, rx=0x0d0,
            rxfill=0x0d8, rxreclaim=0x0e0, getnextrxp=0x0e8, peeknextrxp=0x0f0,
            rxparam_get=0x0f8, getvar=0x108, counterreset=0x110, ctrlflags=0x118,
            rxactive=0x138, txpending=0x140, txcommitted=0x148, rxtxerror=0x158,
            burstlen_set=0x160, avoidancecnt=0x168, param_set=0x170, activerxbuf=0x180)
BOOL = {'txreset', 'rxreset', 'txenabled', 'txsuspended', 'txsuspendedidle', 'txstopped',
        'rxidle', 'rxstopped', 'rxenabled', 'rxfill', 'rxtxerror'}
VOID = {'detach', 'txinit', 'txsuspend', 'txresume', 'txreclaim', 'txblock', 'txunblock',
        'txrotate', 'rxinit', 'rxenable', 'rxreclaim', 'rxparam_get', 'counterreset',
        'burstlen_set', 'param_set'}
INT = {'txfast', 'txunframed', 'peekntxp'}
U32 = {'txactive', 'rxactive', 'txpending', 'txcommitted', 'ctrlflags', 'avoidancecnt',
       'activerxbuf'}
# 16 bit indices kept by the engines (as wlc reads them, D8) are compared
# through the function results, not through the engine's memory


class DmaHw:
    """The DMA channels of the 802.11 core, as the real card answers."""

    def __init__(self, d11):
        self.d11 = d11
        self.read0, self.write0 = d11.read, d11.write
        d11.read, d11.write = self.read, self.write
        self.hwpos = {}     # channel base -> index the hardware has reached
        for ch in range(0x200, 0x300, 0x20):
            d11.poke(ch + R_PTR, 4, 0xfff0)
            d11.poke(ch + R_ST0, 4, (ST_DISABLED << 28) | 0xfff0)
            d11.poke(ch + R_ST1, 4, 0xfff0)

    def read(self, off, size):
        if 0x200 <= off < 0x300:
            return self.d11.peek(off, size)
        return self.read0(off, size)

    def write(self, off, size, value):
        if not 0x200 <= off < 0x300:
            return self.write0(off, size, value)
        reg = off & 0x1f
        if reg == R_ADDRLO:
            value &= 0xfffffff0
        elif reg == R_PTR:
            value &= 0xffff
        self.d11.poke(off, size, value)
        if reg == R_ADDRLO:
            # a new ring: the hardware starts at its first descriptor
            ch = off & ~0x1f
            self.hwpos[ch] = 0
            st0 = self.d11.peek(ch + R_ST0)
            self.d11.poke(ch + R_ST0, 4, (value & 0xffff) | (st0 & 0xf0000000))
            self.d11.poke(ch + R_ST1, 4, value & 0xffff)
        if reg == R_CTRL:
            ch = off & ~0x1f
            st0 = self.d11.peek(ch + R_ST0)
            state = ST_IDLE if value & 1 else ST_DISABLED
            if value & 1 and value & 2 and not ch & 0x20:
                state = ST_SUSP          # transmit: suspend requested
            self.d11.poke(ch + R_ST0, 4, (st0 & 0xffff) | (state << 28))
        return None

    # --- the hardware side, moved by the scenario
    def base(self, ch):
        return self.d11.peek(ch + R_ADDRLO)

    def pos_value(self, ch, idx):
        return (self.base(ch) + 16 * idx) & 0xffff

    def set_status(self, ch, idx, state=None, err=0, active=None):
        st0 = self.d11.peek(ch + R_ST0)
        if state is None:
            state = st0 >> 28
        self.hwpos[ch] = idx
        self.d11.poke(ch + R_ST0, 4, self.pos_value(ch, idx) | (state << 28))
        a = idx if active is None else active
        self.d11.poke(ch + R_ST1, 4, self.pos_value(ch, a) | (err << 28))

    def posted(self, ch, n):
        """Index of the descriptor after the last one posted (from the pointer)."""
        return ((self.d11.peek(ch + R_PTR) - self.base(ch)) & 0xffff) >> 4 if n else 0


class Engine:
    """One engine of one core: calls through its function table."""

    def __init__(self, run, ntxd, nrxd, nrxpost, rxbufsize=2048):
        self.r, self.m, self.d = run, run.m, run.d
        chip = run.s.chip
        self.d11 = chip.core('d11')
        chip.bar0_win = self.d11.base
        self.hw = DmaHw(self.d11)
        wlc_hw = self.m.r64(self.d.wlc + 0x20)
        osh = self.m.r64(wlc_hw + 0x10)
        sih = self.m.r64(wlc_hw + 0xb8)
        from chip import BAR0_VA
        self.ntxd, self.nrxd, self.rxbufsize = ntxd, nrxd, rxbufsize
        self.osh = osh
        self.di = self.m.call('dma_attach', osh, self.m.cstr('wl9'), sih, BAR0_VA + D11_TX0,
                              BAR0_VA + D11_RX0 if nrxd else 0, ntxd, nrxd, rxbufsize, 0,
                              nrxpost, RXOFFSET, 0)
        self.fn = self.m.r64(self.di) if self.di else 0

    def call(self, name, *args):
        r = self.m.call(self.m.r64(self.fn + SLOT[name]), self.di, *args)
        if name in BOOL:
            return r & 0xff         # a bool: the upper bits of rax are undefined
        if name in VOID:
            return None
        if name in INT:
            return s32(r)
        if name in U32:
            return r & 0xffffffff
        return r                    # a pointer

    def pkt(self, length, fill):
        p = self.m.os.pktget(self.m, length)
        if length:
            self.m.write(p.data, bytes((fill + i) & 0xff for i in range(length)))
        return p.handle

    def chain(self, lens, fill):
        hs = [self.pkt(n, fill + 17 * i) for i, n in enumerate(lens)]
        for a, b in zip(hs, hs[1:]):
            self.m.os.pkt(a).next = b
        return hs[0]

    def ring(self, ch, n):
        """The descriptor ring as the engine wrote it (bus address = address)."""
        base = (self.d11.peek(ch + R_ADDRHI) & 0x7fffffff) << 32 | self.d11.peek(ch + R_ADDRLO)
        if not n or not base:
            return b''
        try:
            return self.m.read(base, 16 * n)
        except Exception:
            return b'unreadable %#x' % base

    def deliver(self, frames, rng):
        """Write received frames into the posted buffers, from the hardware's
        position on; returns how many it could place."""
        ch = D11_RX0
        pos = self.hw.hwpos.get(ch, 0)
        end = self.hw.posted(ch, 1)
        placed = 0
        for n in frames:
            ndesc = (RXOFFSET + n + self.rxbufsize - 1) // self.rxbufsize
            free = (end - pos) % self.nrxd
            if ndesc > free:
                break
            rest = RXOFFSET + n
            for k in range(ndesc):
                i = (pos + k) % self.nrxd
                d = self.ring(ch, self.nrxd)[16 * i:16 * i + 16]
                buf = struct.unpack_from('<I', d, 8)[0]
                part = min(rest, self.rxbufsize)
                data = bytearray(rng.randrange(256) for _ in range(part))
                if k == 0:
                    struct.pack_into('<H', data, 0, n)
                self.m.write(buf, bytes(data))
                rest -= part
            pos = (pos + ndesc) % self.nrxd
            placed += 1
        self.hw.set_status(ch, pos, ST_IDLE)
        return placed

    def describe_pkt(self, h):
        """A packet as the caller sees it: the chain's lengths and data."""
        out = []
        while h:
            p = self.m.os.pkt(h)
            out.append((h, p.len, bytes(self.m.read(p.data, min(p.len, 64)))))
            h = p.next
        return tuple(out)


def scenario(run, seed, ntxd, nrxd, nrxpost, frames, chains=False):
    """The traffic; returns the results of every step."""
    rng = random.Random(seed)
    out = []
    e = Engine(run, ntxd, nrxd, nrxpost)
    out.append(('attach', bool(e.di)))
    if not e.di:
        return out, e
    hw, m = e.hw, e.m

    def rec(what, r):
        out.append((what, r, e.ring(D11_TX0, ntxd), e.ring(D11_RX0, nrxd),
                    m.os.pktalloced))

    # initialisation as wlc_bmac_init does it
    for name in ('txreset', 'rxreset', 'txinit', 'rxinit'):
        rec(name, e.call(name))
    if chains:
        # receive frames that span several buffers come back as chains
        rec('ctrlflags 4', e.call('ctrlflags', 4, 4))
    rec('rxfill', e.call('rxfill'))
    rec('getvar', e.call('getvar', m.cstr('&txavail')) != 0)
    sent = 0
    for step in range(frames):
        kind = rng.randrange(10)
        if kind < 4:
            # a frame: one to three buffers, some empty, commit or not
            lens = [rng.choice((0, 14, 60, 300, 1500, 1600)) for _ in range(rng.randrange(1, 4))]
            r = e.call('txfast', e.chain(lens, step), rng.randrange(2))
            rec('txfast %s' % lens, r)
            sent += 1
        elif kind == 4:
            rec('txcommitted', e.call('txcommitted'))
            # what the hardware does with the pointer: completes some of it
            target = hw.posted(D11_TX0, ntxd)
            cur = hw.hwpos.get(D11_TX0, 0)
            span = (target - cur) % ntxd
            hw.set_status(D11_TX0, (cur + rng.randrange(span + 1)) % ntxd,
                          rng.choice((ST_ACTIVE, ST_IDLE)))
            rec('hw tx', 0)
        elif kind == 5:
            rng_range = rng.choice((2, 2, 3))
            got = []
            for _ in range(rng.randrange(1, 6)):
                p = e.call('getnexttxp', rng_range)
                got.append(p)
                if not p:
                    break
                m.os.pkt(p)       # must be a live packet
            rec('getnexttxp %d' % rng_range, tuple(got))
        elif kind == 6:
            placed = e.deliver([rng.choice((10, 100, 1500, 2100, 4000, 7000))
                                for _ in range(rng.randrange(1, 4))], rng)
            got = []
            for _ in range(placed + 1):
                p = e.call('rx')
                if not p:
                    break
                got.append(e.describe_pkt(p))
            rec('rx %d' % placed, tuple(got))
            rec('rxfill', e.call('rxfill'))
        elif kind == 7:
            for name in ('txactive', 'txpending', 'rxactive', 'activerxbuf', 'avoidancecnt'):
                rec(name, e.call(name))
            rec('peeknexttxp', e.call('peeknexttxp'))
            buf = m.buf(8 * 16)
            n = m.buf(4)
            m.w32(n, rng.randrange(1, 9))
            rec('peekntxp', (e.call('peekntxp', n, buf, rng.choice((1, 2, 3))),
                             m.r32(n), m.read(buf, 8 * 8)))
            rec('peeknextrxp', e.call('peeknextrxp'))
            rec('getpos tx', e.call('getpos', 1))
            rec('getpos rx', e.call('getpos', 0))
        elif kind == 8:
            # transmit suspend/resume with the states the hardware reports
            rec('txsuspend', e.call('txsuspend'))
            hw.set_status(D11_TX0, hw.hwpos.get(D11_TX0, 0),
                          rng.choice((ST_ACTIVE, ST_SUSP, ST_IDLE, ST_STOPPED)))
            for name in ('txsuspended', 'txsuspendedidle', 'txstopped', 'txenabled'):
                rec(name, e.call(name))
            rec('txresume', e.call('txresume'))
            hw.set_status(D11_RX0, hw.hwpos.get(D11_RX0, 0),
                          rng.choice((ST_ACTIVE, ST_IDLE, ST_STOPPED)),
                          err=rng.choice((0, 0, 1, 4)))
            for name in ('rxidle', 'rxstopped', 'rxenabled'):
                rec(name, e.call(name))
            rec('rxtxerror tx', e.call('rxtxerror', 1))
            rec('rxtxerror rx', e.call('rxtxerror', 0))
            hw.set_status(D11_RX0, hw.hwpos.get(D11_RX0, 0), ST_IDLE, err=0)
        elif kind == 9 and rng.randrange(3) == 0:
            # a burst until the ring is full (the frame that does not fit is freed)
            got = []
            for k in range(ntxd + 2):
                r = e.call('txfast', e.chain([rng.choice((60, 1500))], step + k), 1)
                got.append(r)
                if r != 0:
                    break
            rec('burst', tuple(got))
        else:
            # occasionally the whole hardware has finished everything
            hw.set_status(D11_TX0, hw.posted(D11_TX0, ntxd), ST_IDLE)
            got = []
            while True:
                p = e.call('getnexttxp', 2)
                if not p:
                    break
                got.append(p)
            rec('drain', tuple(got))
    # the end: reclaim everything as the flush and the down do
    rec('txreclaim 1', e.call('txreclaim', 1))
    rec('rxreclaim', e.call('rxreclaim'))
    rec('ctrlflags', e.call('ctrlflags', 0, 0))
    rec('counterreset', e.call('counterreset'))
    rec('detach', e.call('detach'))
    return out, e


CASES = [
    # seed, ntxd, nrxd, nrxpost, steps: small rings wrap many times; the
    # card's 1024 descriptors give offsets beyond 0x1fff
    (1, 64, 64, 32, 400),
    (2, 64, 64, 48, 400),
    (3, 256, 256, 128, 300),
    (4, 1024, 1024, 256, 300),
    (5, 32, 32, 31, 300),
]
CHAIN_CASES = [(6, 64, 64, 32, 400), (7, 1024, 1024, 256, 300)]


def dma_check(off=(), show=20, on=()):
    core = hybrid.build(quiet=True)
    bad = 0
    total = 0
    for seed, ntxd, nrxd, nrxpost, steps in CASES + CHAIN_CASES:
        res = []
        for path, o, f in ((None, (), ()), (core, on, off)):
            r = hybrid.Run(path, on=o, off=f)
            r.step('attach')
            n0 = len(r.s.chip.trace)
            out, e = scenario(r, seed, ntxd, nrxd, nrxpost, steps,
                              chains=(seed, ntxd, nrxd, nrxpost, steps) in CHAIN_CASES)
            acc = ab.logical(r.s.chip.trace[n0:], r.s.chip.func_name)
            res.append((out, acc))
        (want, wacc), (have, hacc) = res
        total += len(want)
        diff = [i for i, (a, b) in enumerate(zip(want, have)) if a != b]
        if len(want) != len(have):
            diff.append(min(len(want), len(have)))
        print('case seed %d ntxd %d nrxd %d nrxpost %d: %d steps, %d accesses of the object, '
              '%d of the hybrid core: %s' % (seed, ntxd, nrxd, nrxpost, len(want), len(wacc),
                                            len(hacc), 'the same' if not diff and wacc == hacc
                                            else 'DIFFERENT'))
        if diff:
            bad += 1
            for i in diff[:show]:
                a = want[i] if i < len(want) else None
                b = have[i] if i < len(have) else None
                print('  step %d %s' % (i, a[0] if a else b[0]))
                for k, (x, y) in enumerate(zip(a or (), b or ())):
                    if x != y:
                        if isinstance(x, bytes) and isinstance(y, bytes) and len(x) == len(y):
                            j = next(j for j in range(len(x)) if x[j] != y[j])
                            print('    field %d differs from byte %#x: object %s, hybrid %s'
                                  % (k, j, x[j:j + 16].hex(), y[j:j + 16].hex()))
                        else:
                            print('    field %d: object %r, hybrid %r'
                                  % (k, x if not isinstance(x, bytes) else x[:32],
                                     y if not isinstance(y, bytes) else y[:32]))
        elif wacc != hacc:
            bad += 1
            ab.compare(wacc, hacc, show)
    print('dma check, switched on: %s, off: %s: %d steps: %s'
          % (', '.join(on) or '-', ', '.join(off) or '-', total,
             'the same' if not bad else 'DIFFERENT in %d cases' % bad))
    return bad
