#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
The core's frame path (open/core/frames.c, rx.c, tx.c) against Broadcom's
object on the same card model (docs/re/tasks/core-frames.md).

    frames_check.py [receive] [transmit] [status] [ampdu] [keys] [ps] [power] [chains] [--show N]

Both sessions are brought up (attach, up) and get the DMA channel model of
dma_scenarios.DmaHw. Then

  receive   the same receive buffers (header + frame) are put into FIFO 0's
            posted buffers of both and the interrupt path is run (isr, dpc);
            compared: the register accesses, the receive header's host part
            (TSF word, RSSI, per-core RSSI) and the frame bytes at the
            object's wlc_recv and at the core's bcm4360_plat_rx, and the
            decoded status against the values frames.md 1.5, 1.7 give.
  transmit  the probe requests of an active scan of the object are captured
            at wlc_txfifo; the core is given the same frames and rates; the
            posted bytes (prefix, header, frame) are compared. Rates the
            object's scan does not use (HT, VHT, protection) are checked
            against the values of frames.md 3.5, 3.6.
  status    transmit-status words are put into the status registers of both
            for the frames at the head of FIFO 3; compared: the register
            accesses, fatal errors, and the core's decoded status against
            frames.md 4.

  ampdu     (docs/re/tasks/core-ampdu.md) aggregated MPDUs through the core: the
            header bytes (control bits, cache bytes 0x66..0x6b, RTS/CTS, the
            epoch bit in the prefix and when it flips) and aggregate status
            words (bitmap with holes, fallback counts, reason 1 = posted
            again unchanged) against the values of docs/re/spec/ampdu.md 1.4,
            1.6, 2.2, 2.3, 2.6. Checked against the spec only: the object has
            no public entry point that makes it aggregate.

  power     (docs/re/tasks/core-gaps-tx.md, coverage.md G1) wlc_stf_spatial_expansion_get,
            wlc_stf_get_pwrperrate and wlc_acphy_txctl1_calc of the object against the core's for
            random stf states, containers, chanspecs and rate specifications.
  chains    (coverage.md G5) wlc_stf_txchain_set / wlc_stf_rxchain_set of the object against the
            core's: results, stf block, accesses.

The object is used as a black box through its public entry points and the
functions frames.md names (wlc_isr, wlc_dpc, wlc_recv, wlc_txfifo).
"""
import argparse
import os
import random
import struct
import sys

import ab
import hybrid
import standalone
from dma_scenarios import (DmaHw, D11_RX0, R_PTR, R_ADDRLO, R_ADDRHI, ST_IDLE, RXOFFSET)
from emu import EmuError
from run_chan import chanspec

D11_MACINTSTATUS, D11_MACINTMASK = 0x128, 0x12c
MI_DMAINT, MI_TFS = 0x8000, 0x20000000
TXCH = [0x200 + 0x40 * f for f in range(4)]
RXBUF = 2048


def txch(f):
    return TXCH[f]


class ProbedDmaHw(DmaHw):
    """DmaHw whose channels answer the engines' probe as the real card does
    (ring base 0xfffffff0, pointer 0xfff0) also after the model's core reset."""

    def write(self, off, size, value):
        r = super().write(off, size, value)
        if 0x200 <= off < 0x300 and off & 0x1f == R_ADDRLO and value == 0xffffffff:
            self.d11.poke((off & ~0x1f) + R_PTR, 4, 0xfff0)
        return r


class Side:
    """One session (the object or the core) with its DMA model."""

    def __init__(self, name, run=None, core=None):
        self.name, self.run, self.core = name, run, core
        self.s = run.s if run else core.s
        self.m = self.s.m
        self.chip = self.s.chip
        self.d11 = self.chip.core('d11')
        self.rx_log = []        # (rxh+frame bytes as seen, status or None)
        self.fatal = 0

    def trace_from(self, n):
        names = self.s.chip.func_name if self.run else self.s.names
        return standalone.comparable(ab.logical(self.chip.trace[n:], names), [])

    def dma(self):
        self.chip.bar0_win = self.d11.base
        self.hw = ProbedDmaHw(self.d11)



class FCore(standalone.Core):
    """The core with the platform callbacks recorded in full."""

    def __init__(self, objs):
        self.rx = []            # (packet bytes, status tuple, host part, handle)
        self.txs = []           # (packet bytes, status tuple)
        self.wake = []
        self.fatals = 0
        super().__init__(objs)

    def _env(self):
        env = super()._env()

        def on_rx(m, plat, pkt, rs):
            p = m.os.pkt(pkt)
            st = struct.unpack('<QHb3bBBBBB', bytes(m.read(rs, 19)))
            back = bytes(m.read(p.data - 0x80, 0x80))    # what was in front of the frame
            self.rx.append((bytes(m.read(p.data, p.len)), st, back))
            return 0

        def on_txs(m, plat, pkt, ts):
            p = m.os.pkt(pkt)
            self.txs.append((bytes(m.read(p.data, p.len)), tuple(m.read(ts, 6))))
            return 0

        env['bcm4360_plat_rx'] = (3, on_rx)
        env['bcm4360_plat_txstatus'] = (3, on_txs)
        env['bcm4360_plat_tx_wake'] = (2, lambda m, plat, f: self.wake.append(f) or 0)

        def fatal(m, plat):
            self.fatals += 1
            return 0
        env['bcm4360_plat_fatal'] = (1, fatal)
        return env


def bring_up():
    objs = standalone.build(quiet=True)
    ref = hybrid.Run()
    core = FCore(objs)
    a, b = Side('object', run=ref), Side('core', core=core)
    for s in (a, b):
        s.dma()                 # before attach: the engines' probe sees the channel model
    ref.step('attach')
    ref.step('mpc 0')
    ref.step('up')
    cs = ref.d.iovar_getint('chanspec')[1] & 0xffff
    if core.step('attach'):
        raise SystemExit('core: attach fails')
    ref.s.chip.radio_done = True
    if core.step('up', cs):
        raise SystemExit('core: up fails')
    a.cs = b.cs = cs
    for s in (a, b):
        # the channels' current/active descriptors start at the ring base
        # (a core reset of the model leaves them at 0)
        for ch in [D11_RX0] + TXCH:
            base = s.d11.peek(ch + R_ADDRLO) & 0xffff
            st0 = s.d11.peek(ch + 0x10)
            s.d11.poke(ch + 0x10, 4, base | (st0 & 0xf0000000))
            s.d11.poke(ch + 0x14, 4, base)
    # the object: what reaches wlc_recv / wlc_txfifo / wlc_fatal_error
    a.recv, a.txfifo, a.fatal = [], [], 0
    m = ref.m

    def on_recv(m, wlc, p):
        pk = m.os.pkt(p)
        a.recv.append(bytes(m.read(pk.data, pk.len)))

    def on_txfifo(m, wlc, fifo, p, *rest):
        pk = m.os.pkt(p)
        a.txfifo.append((fifo & 0xff, bytes(m.read(pk.data, pk.len))))

    def on_fatal(m, wlc):
        a.fatal += 1

    m.on_call('wlc_recv', on_recv, 2)
    m.on_call('wlc_txfifo', on_txfifo, 4)
    m.on_call('wlc_fatal_error', on_fatal, 1)
    return a, b


def ring(side, ch, n):
    base = (side.d11.peek(ch + R_ADDRHI) & 0x7fffffff) << 32 | side.d11.peek(ch + R_ADDRLO)
    return bytes(side.m.read(base, 16 * n)) if base else b''


def info(side):
    for ch in [D11_RX0] + TXCH:
        print('  %-6s ch %#x base %#x ptr %#x' % (side.name, ch, side.d11.peek(ch + R_ADDRLO),
                                                  side.d11.peek(ch + R_PTR)))



# ----------------------------------------------------------------- receive

F_SGI, F_LDPC, F_STBC, F_SHORT, F_FCS, F_DEC, F_AMPDU = 1, 2, 4, 8, 0x10, 0x20, 0x40
F_DECERR = 0x80
NRXD = 1024


def vec(rng, kind, plcp, phy0, pad=False, st1=0, st2=0, n=None, raw=None):
    """One receive buffer: header + frame, and what to expect."""
    raw = raw or (-55, -60, -70)
    fr = bytes([0x88, 0x01]) + bytes(rng.randrange(256) for _ in range(rng.randrange(26, 120)))
    fcs = bytes(rng.randrange(256) for _ in range(4))
    body = (bytes(2) if pad else b'') + bytes(plcp) + fr + fcs
    if n is not None:
        body = body[:n]
    size = len(body)
    return dict(kind=kind, plcp=bytes(plcp), phy0=phy0, pad=pad, st1=st1 | (4 if pad else 0),
                st2=st2, body=body, size=size, frame=fr + fcs, raw=raw,
                tsft=rng.randrange(0x10000), nlen=len(fr + fcs))


def rx_bytes(v, chan):
    phy2 = (v['raw'][0] & 0xff) << 8
    phy3 = (v['raw'][1] & 0xff) | (v['raw'][2] & 0xff) << 8
    hdr = struct.pack('<12H', v['size'], 0, v['phy0'], 0, phy2, phy3, 0, 0, v['st1'], v['st2'],
                      v['tsft'], chan)
    return hdr + bytes(16) + v['body']


def vectors(rng):
    out = []
    ofs = lambda code: bytes([code | 0x18, 0x11, 0x22, 0, 0, 0])
    for r10, short in ((10, 0), (20, 0), (55, 1), (110, 1)):
        out.append(vec(rng, 'cck %d' % r10, bytes([r10, 0, 0, 0, 0, 0]), 0 | (0x80 if short else 0)))
    for code in range(8):
        out.append(vec(rng, 'ofdm %d' % code, ofs(code), 1))

    def ht(mcs, bw, sgi, ldpc, stbc, agg, **k):
        p = [mcs | bw << 7, 0x12, 0x03, (0x80 if sgi else 0) | (0x40 if ldpc else 0) |
             (0x10 if stbc else 0) | (8 if agg else 0), 0, 0]
        return vec(rng, 'ht mcs%d bw%d%s' % (mcs, bw, ' agg' if agg else ''), p, 2, **k)

    def vht(bw, nsts, stbc, mcs, sgi, ldpc, **k):
        a1 = bw | stbc << 3 | (nsts - 1) << 10 | 1 << 23
        p = [a1 & 255, a1 >> 8 & 255, a1 >> 16, sgi | ldpc << 2 | mcs << 4, 2, 0]
        return vec(rng, 'vht bw%d nsts%d mcs%d' % (bw, nsts, mcs), p, 3, **k)

    out += [ht(0, 0, 0, 0, 0, 0), ht(7, 1, 1, 0, 0, 0), ht(15, 0, 0, 1, 1, 0), ht(23, 1, 1, 1, 0, 0),
            ht(5, 0, 1, 0, 0, 1)]
    out.append(vec(rng, 'ht following mpdu', bytes(6), 2))
    out += [vht(2, 2, 0, 9, 1, 0, st2=0x100), vht(0, 1, 0, 0, 0, 1, st2=0x100),
            vht(1, 4, 1, 5, 0, 0, st2=0x100), vht(2, 1, 0, 7, 0, 0, st2=0),
            vec(rng, 'vht following mpdu', bytes(6), 3, st2=0),
            vec(rng, 'vht last mpdu', bytes(6), 3, st2=0x100)]
    out += [vec(rng, 'pad', ofs(2), 1, pad=True), vec(rng, 'decrypted', ofs(3), 1, st1=8),
            vec(rng, 'decrypt error', ofs(3), 1, st1=0x18, raw=(-30, -128, 5))]
    out += [vec(rng, 'drop 0x10', ofs(2), 1 | 0x10), vec(rng, 'drop 0x100', ofs(2), 1 | 0x100),
            vec(rng, 'drop 0x200', ofs(2), 1 | 0x200), vec(rng, 'drop fcs', ofs(2), 1, st1=1),
            vec(rng, 'drop runt', ofs(2), 1, n=7), vec(rng, 'drop runt pad', ofs(2), 1, pad=True, n=9),
            vec(rng, 'drop amsdu', ofs(2), 1, st2=1), vec(rng, 'no phy status', ofs(2), 1, st2=0)]
    return out


def expect(v, st):
    """frames.md 1.4, 1.5, 1.8 for one vector: None (dropped) or (enc, rate, nss, bw, flags)."""
    if v['phy0'] & 0x310 or v['st1'] & 1 or v['st2'] & 1 or v['size'] < (2 if v['pad'] else 0) + 8:
        return None
    t, p = v['phy0'] & 3, v['plcp']
    fl = 0
    if t == 0:
        enc, rate, nss, bw = 0, p[0] // 5, 1, 0
        fl |= F_SHORT if v['phy0'] & 0x80 else 0
    elif t == 1:
        enc, rate, nss, bw = 0, (96, 48, 24, 12, 108, 72, 36, 18)[p[0] & 7], 1, 0
    elif t == 2:
        enc, rate, bw = 1, p[0] & 0x7f, p[0] >> 7
        nss = rate // 8 + 1
        fl |= (F_SGI if p[3] & 0x80 else 0) | (F_LDPC if p[3] & 0x40 else 0) | \
              (F_STBC if p[3] & 0x30 else 0)
    else:
        enc, rate, bw = 2, p[3] >> 4, min(p[0] & 3, 2)
        a1 = p[0] | p[1] << 8
        nss = ((a1 >> 10) & 7) + 1
        if p[0] & 8:
            fl |= F_STBC
            nss >>= 1
        fl |= (F_SGI if p[3] & 1 else 0) | (F_LDPC if p[3] & 4 else 0)
    if t >= 2:
        zero = not (p[0] | p[1] | p[2])
        if zero:
            if st.get('rate'):
                enc, rate, nss, bw, fl = st['rate']
            amp = True
        else:
            amp = bool(p[3] & 8) if t == 2 else not (v['st2'] & 0x100)
            if amp:
                st['rate'] = (enc, rate, nss, bw, fl)
        fl |= F_AMPDU if amp else 0
    if v['st1'] & 8:
        fl |= F_DECERR if v['st1'] & 0x10 else F_DEC
    return (enc, rate, nss, bw, fl)


def run_irq(side, status, bit):
    """Raise `bit`, run isr and dpc (until nothing is left); returns the accesses."""
    n0 = len(side.chip.trace)
    side.d11.poke(D11_MACINTSTATUS, 4, bit)
    if side.run:
        d, m = side.run.d, side.m
        want, cnt = m.buf(8), m.buf(32)
        m.call('wlc_isr', d.wlc, want)
        more = m.call('wlc_dpc', d.wlc, 1, cnt) & 0xff
        while more:
            m.call('wlc_intrsupd', d.wlc)
            more = m.call('wlc_dpc', d.wlc, 1, cnt) & 0xff
        m.call('wlc_intrson', d.wlc)
    else:
        c = side.core
        c.call('bcm4360_core_isr', c.core)
        while c.call('bcm4360_core_dpc', c.core, 0) & 0xff:
            pass
    return norm_dma(side, [x for x in side.trace_from(n0)
                           if not (x[1] == 'd11' and x[2] == 0x184 and x[0] == 'R')])


def norm_dma(side, acc):
    """The channels' pointer and status values relative to the ring base (the
    two sessions allocate their rings at different addresses)."""
    out = []
    for x in acc:
        if x[1] == 'd11' and 0x200 <= x[2] < 0x300 and x[2] & 0x1f in (4, 0x10, 0x14):
            base = side.d11.peek((x[2] & ~0x1f) + R_ADDRLO)
            if x[2] & 0x1f == 4:
                v = (x[4] - base) & 0xffffffff
            else:
                v = (x[4] & 0xf0000000) | ((x[4] - base) & 0xffff)
            x = x[:4] + (v,)
        out.append(x)
    return out


def deliver(side, bufs):
    ch = D11_RX0
    pos = side.hw.hwpos.get(ch, 0)
    r = ring(side, ch, NRXD)
    for k, data in enumerate(bufs):
        i = (pos + k) % NRXD
        buf = struct.unpack_from('<I', r, 16 * i + 8)[0]
        side.m.write(buf, data)
    side.hw.set_status(ch, (pos + len(bufs)) % NRXD, ST_IDLE)


def receive(a, b, show):
    rng = random.Random(7)
    vs = vectors(rng)
    bad = 0
    state = {}
    for part in (vs[:26], vs[26:], [vec(rng, 'bulk', bytes([2, 0x11, 0x22, 0, 0, 0]), 1)
                                    for _ in range(40)]):
        n_a, n_b = len(a.recv), len(b.core.rx)
        got = []
        for side in (a, b):
            deliver(side, [rx_bytes(v, a.cs) for v in part])
            got.append(run_irq(side, None, MI_DMAINT))
        if got[0] != got[1]:
            bad += 1
            print('receive: register accesses differ (%d object, %d core)' % (len(got[0]), len(got[1])))
            ab.compare(got[0], got[1], show)
        else:
            print('receive: %d frames, %d register accesses: same' % (len(part), len(got[0])))
        objs, mine = a.recv[n_a:], b.core.rx[n_b:]
        if len(objs) != len(part):
            bad += 1
            print('receive: the object saw %d of %d frames at wlc_recv' % (len(objs), len(part)))
            continue
        k = 0
        for v, o in zip(part, objs):
            want = expect(v, state)
            what = None
            if want is not None:
                if k >= len(mine):
                    what = 'not delivered'
                else:
                    data, st, back = mine[k]
                    k += 1
                    pad = 2 if v['pad'] else 0
                    exp_frame = bytes(o[40 + pad + 6:40 + v['size']])
                    host_o, host_m = bytes(o[0x18:0x28]), back[0x80 - 46 - pad + 0x18:0x80 - 46 - pad + 0x28]
                    inv = o[0x1f]
                    sig = 0 if inv else struct.unpack('b', o[0x1c:0x1d])[0]
                    chains = tuple(struct.unpack('b', o[0x20 + i:0x21 + i])[0] if not inv else 0
                                   for i in range(3))
                    if data != exp_frame or data != v['frame']:
                        what = 'frame bytes differ'
                    elif host_o[:4] != host_m[:4] or host_o[4:6] != host_m[4:6] or host_o[7:] != host_m[7:]:
                        what = 'host part of the header differs: %s / %s' % (host_o.hex(), host_m.hex())
                    elif (st[2], tuple(st[3:6])) != (sig, chains):
                        what = 'rssi %s / %s, object %s' % (st[2], st[3:6], (sig, chains))
                    elif (st[6], st[7], st[8], st[9], st[10]) != want:
                        what = 'status %s, expected %s' % ((st[6], st[7], st[8], st[9], st[10]), want)
                    elif st[1] != a.cs:
                        what = 'chanspec %#x' % st[1]
            if what:
                bad += 1
                print('receive: %-22s %s' % (v['kind'], what))
        if k != len(mine):
            bad += 1
            print('receive: the core delivered %d frames, %d expected' % (len(mine), k))
    print('receive: %s' % ('same' if not bad else '%d difference(s)' % bad))
    return bad


# ---------------------------------------------------------------- transmit

TXH = 4                         # the prefix; the header follows
ENTRY = 0x14


def txparams(m, fifo, rates, no_ack=0, rts=0, cts=0, seq_by_hw=0, ampdu=0, tid=0, peer=0,
             hw_key=0, p1k=(0, 0, 0, 0, 0)):
    """struct bcm4360_txparams in the machine's memory; rates: (enc, rate, nss, bw, flags)."""
    raw = bytearray(48)
    raw[0], raw[1] = fifo, len(rates)
    for i, r in enumerate(rates):
        raw[2 + 6 * i:2 + 6 * i + 6] = bytes(list(r) + [0] * (5 - len(r)) + [1])
    raw[26], raw[27], raw[28], raw[29] = no_ack, rts, cts, seq_by_hw
    raw[30], raw[31], raw[32] = ampdu, tid, peer
    raw[33] = hw_key
    raw[34:44] = struct.pack('<5H', *p1k)
    return m.buf(bytes(raw))


def post(side, fifo, frame, rates, **kw):
    """Transmit `frame` through the core; returns (result, the posted bytes)."""
    c, m = side.core, side.m
    pk = m.os.pktget(m, len(frame))
    m.write(pk.data, frame)
    p = txparams(m, fifo, rates, **kw)
    r = s32(c.call('bcm4360_core_tx', c.core, pk.handle, p))
    posted = side.posted.setdefault(fifo, [])
    if r < 0:
        return r, None
    ch = txch(fifo)
    d = ring(side, ch, len(posted) + 1)[16 * len(posted):16 * len(posted) + 16]
    ctrl1, ctrl2, lo, hi = struct.unpack('<4I', d)
    data = bytes(m.read(lo, ctrl2 & 0x1fff))
    posted.append(data)
    return r, data


def s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def hdr_fields(raw):
    """The fields of the transmit header that are compared, by name."""
    h = raw[TXH:]
    f = {'prefix': raw[:TXH].hex(), 'low': struct.unpack_from('<H', h, 2)[0],
         'high': struct.unpack_from('<H', h, 4)[0], 'chanspec': struct.unpack_from('<H', h, 6)[0],
         'ivoff': h[8], 'framelen': struct.unpack_from('<H', h, 0x0a)[0],
         'id': struct.unpack_from('<H', h, 0x0c)[0], 'seq': struct.unpack_from('<H', h, 0x0e)[0],
         'rest0': h[0x10:0x14].hex(), 'tail': h[0x64:0x7c].hex(), 'frame': raw[TXH + 0x7c:].hex()}
    for i in range(4):
        e = h[ENTRY + ENTRY * i:ENTRY + ENTRY * i + ENTRY]
        w0, w1, w2 = struct.unpack_from('<3H', e, 0)
        f['e%d' % i] = (w0, w1, w2, e[6:12].hex(), e[12:14].hex(),
                        struct.unpack_from('<H', e, 14)[0], struct.unpack_from('<H', e, 16)[0],
                        e[18:20].hex())
    return f


def diff_fields(want, got, ignore=()):
    out = []
    for k in want:
        if k in ignore:
            continue
        if want[k] != got[k]:
            out.append('%s: object %s, core %s' % (k, want[k], got[k]))
    return out


def ensure_scan(a, b):
    if getattr(a, 'scanned', False):
        return
    a.scanned = True
    b.posted = {}
    a.scan_result = a.run.scan(False)
    print('transmit: the object scanned (%s), %d frames at wlc_txfifo' % (a.scan_result,
                                                                          len(a.txfifo)))
    b.sent = []
    b.tx_bad = 0
    for fifo, raw in a.txfifo:
        h = raw[TXH:]
        low = struct.unpack_from('<H', h, 2)[0]
        rate = struct.unpack_from('<H', h, ENTRY + 14)[0]
        if fifo > 3 or rate > 108:
            continue
        r, data = post(b, fifo, raw[TXH + 0x7c:], [(0, rate, 1, 0, 0)],
                       no_ack=0 if low & 0x80 else 1, seq_by_hw=1 if low & 0x800 else 0)
        b.sent.append((fifo, raw, r, data))


def transmit(a, b, show):
    ensure_scan(a, b)
    bad = 0
    prev_o = prev_m = None
    for k, (fifo, raw, r, data) in enumerate(b.sent):
        if data is None:
            print('transmit: frame %d not taken (%d)' % (k, r))
            bad += 1
            continue
        fo, fm = hdr_fields(raw), hdr_fields(data)
        # the frame ids differ in the counter (the object's has run before); each must count on by one
        ido, idm = fo.pop('id'), fm.pop('id')
        if (ido & 0x1ff) != (idm & 0x1ff):
            bad += 1
            print('transmit: frame %d id %#x / %#x' % (k, ido, idm))
        if prev_o is not None and (((ido - prev_o) >> 9) & 0x3f != 1 or ((idm - prev_m) >> 9) & 0x3f != 1):
            bad += 1
            print('transmit: ids %#x -> %#x (object), %#x -> %#x (core) do not count on by one' %
                  (prev_o, ido, prev_m, idm))
        prev_o, prev_m = ido, idm
        # the scan changes the object's channel; the core stays on its own
        if fm.pop('chanspec') != b.cs:
            bad += 1
            print('transmit: frame %d chanspec %#x, the core is on %#x' % (k, fm['chanspec'], b.cs))
        fo.pop('chanspec')
        d = diff_fields(fo, fm)
        if d:
            bad += 1
            print('transmit: frame %d (fifo %d) differs:' % (k, fifo))
            for x in d:
                print('    ' + x)
        elif k == 0:
            print('transmit: first frame: %s' % {x: fm[x] for x in ('low', 'high', 'ivoff', 'framelen', 'e0')})
    print('transmit: %d frames of the scan compared with the object' % len(b.sent))
    bad += spec_rates(b)
    print('transmit: %s' % ('same' if not bad else '%d difference(s)' % bad))
    return bad


# frames.md 3.5, 3.6 "Verified": rate -> (word 0 without the core bits, word 2, PLCP, TxRate)
SPEC_RATES = [
    ('VHT 80 2ss MCS9 sgi', (2, 9, 2, 2, 1), 0x8007, 0x19, '06048091' + '0200', 1733),
    ('HT 40 MCS7 sgi', (1, 7, 1, 1, 1), 0x4006, 7, '878800' + '87' + '0000', 300),
    ('HT 20 MCS15', (1, 15, 2, 0, 0), 0x0006, 0xf, '0f8800' + '07' + '0000', 0),
    ('OFDM 24', (0, 48, 1, 0, 0), 0x0005, 4, '091100000000', 48),
    ('OFDM 6', (0, 12, 1, 0, 0), 0x0005, 0, '0b1100000000', 12),
    ('CCK 11', (0, 22, 1, 0, 0), 0x0004, 3, '6e046300' + '0000', 22),
    ('CCK 1', (0, 2, 1, 0, 0), 0x0004, 0, '0a044004' + '0000', 2),
]


def spec_rates(b):
    """Rates the object's scan does not use: the values frames.md gives (cores and power field apart)."""
    bad = 0
    fr = bytes([0x88, 0x01]) + bytes(range(2, 132))        # a QoS data frame to the access point
    for name, rate, w0, w2, plcp, txrate in SPEC_RATES:
        r, data = post(b, 2, fr, [rate])
        if data is None:
            print('transmit: %s not taken' % name)
            bad += 1
            continue
        f = hdr_fields(data)['e0']
        got = (f[0] & 0xfe3f, f[2], f[3], f[5])
        cores = (f[0] >> 6) & 7
        want = (w0, w2, plcp, txrate)
        print('transmit: %-20s cores %#x word0 %#06x word2 %#x plcp %s rate %d' %
              (name, cores, f[0], f[2], f[3], f[5]))
        if (got[0] != w0 or got[1] != w2 or got[2] != plcp or (txrate and got[3] != txrate)):
            bad += 1
            print('transmit: %-20s got word0 %#x word2 %#x plcp %s rate %d, spec %s (cores %#x)' %
                  (name, got[0], got[1], got[2], got[3], want, cores))
    return bad


# ------------------------------------------------------------------ status

class StatusRegs:
    """D11(0x170..0x17c) as a queue of transmit statuses (frames.md 4.1): each is two
    packages of four words, the first read in order of the registers, then the second."""

    def __init__(self, side):
        self.q, self.pkg = [], 0
        self.read0 = side.d11.read
        side.d11.read = self.read

    def read(self, off, size):
        if not 0x170 <= off <= 0x17c:
            return self.read0(off, size)
        if not self.q:
            return 0
        i = (off - 0x170) // 4
        v = self.q[0][self.pkg * 4 + i]
        if i == 3:
            self.pkg += 1
            if self.pkg == 2:
                self.pkg = 0
                self.q.pop(0)
        return v


def status_words(fid, acked=1, reason=0, phyerr=0, counts=(1, 0, 0, 0), ncons=1, rts=0, cts=0):
    w0 = 1 | ncons << 8 | (0x8000 if acked else 0) | reason << 4 | fid << 16
    w1 = 0x1234 | phyerr << 16
    w2 = counts[0] | counts[1] << 16
    w3 = counts[2] | counts[3] << 16
    v0 = 1 | rts << 16 | cts << 24
    return (w0, w1, w2, w3, v0, 0xffff, 0, 0)


def status(a, b, show):
    ensure_scan(a, b)
    bad = 0
    if not hasattr(a, 'sregs'):
        a.sregs, b.sregs = StatusRegs(a), StatusRegs(b)
        a.fifo3 = [h for f, h in a.txfifo if f == 3]
        b.fifo3 = list(b.posted[3][:len(a.fifo3)])
        b.fifo3_frames = [x[TXH + 0x7c:] for x in b.fifo3]
    # (name, kwargs, expected status (acked, rate_idx, tries, ampdu), fatal)
    cases = [('acked', dict(), (1, 0, 1, 0), 0),
             ('not acked, 3 tries', dict(acked=0, counts=(3, 0, 0, 0)), (0, 0, 3, 0), 0),
             ('acked on the second rate', dict(counts=(2, 1, 0, 0)), (1, 1, 3, 0), 0),
             ('acked on the fourth rate', dict(counts=(1, 1, 1, 2)), (1, 3, 5, 0), 0),
             ('rts counts', dict(counts=(2, 0, 0, 0), rts=2, cts=1), (1, 0, 2, 0), 0),
             ('aggregate: ncons 4', dict(acked=0, ncons=4, counts=(1, 0, 0, 0)), (1, 0, 1, 0), 0),
             ('suppressed, lifetime', dict(acked=0, reason=5), (0, 0, 1, 0), 0),
             ('suppressed, flush', dict(acked=0, reason=2), (0, 0, 1, 0), 0),
             ('phy error', dict(acked=0, phyerr=3), (0, 0, 1, 0), 0),
             ('wrong frame id', dict(fid_add=0x200), None, 1)]
    for k, (name, kw, want, fatal) in enumerate(cases):
        kw = dict(kw)
        add = kw.pop('fid_add', 0)
        got = []
        for side in (a, b):
            hdr = side.fifo3[k]
            fid = struct.unpack_from('<H', hdr, TXH + 0x0c)[0]
            side.sregs.q.append(status_words((fid + add) & 0xffff, **kw))
            side.hw.set_status(txch(3), side.hw.posted(txch(3), 1), ST_IDLE)
            f0 = side.fatal if side.run else side.core.fatals
            n_tx = 0 if side.run else len(side.core.txs)
            tr = run_irq(side, None, MI_TFS)
            got.append((tr, (side.fatal if side.run else side.core.fatals) - f0,
                        None if side.run else side.core.txs[n_tx:]))
        (tr_o, f_o, _), (tr_m, f_m, txs) = got
        if fatal:
            tr_o = tr_o[:len(tr_m)]        # the object then re-initialises the core
        if tr_o != tr_m:
            bad += 1
            print('status %-26s register accesses differ' % name)
            ab.compare(tr_o, tr_m, show)
        if f_o != f_m or f_o != fatal:
            bad += 1
            print('status %-26s fatal: object %d, core %d, expected %d' % (name, f_o, f_m, fatal))
        if want is not None:
            if len(txs) != 1:
                bad += 1
                print('status %-26s the core reported %d statuses' % (name, len(txs)))
            else:
                data, st = txs[0]
                if st[:4] != want or data != b.fifo3_frames[k]:
                    bad += 1
                    print('status %-26s core %s, expected %s%s' % (
                        name, st, want, '' if data == b.fifo3_frames[k] else ' (frame bytes differ)'))
        if fatal:
            break
    print('status: %s' % ('same' if not bad else '%d difference(s)' % bad))
    return bad


# -------------------------------------------------------------------- ampdu

VHT_R = (2, 9, 2, 2, 1)         # VHT 80 MHz 2 ss MCS 9, short GI
VHT_R2 = (2, 7, 2, 2, 1)
HT_R = (1, 7, 1, 1, 1)          # HT 40 MHz MCS 7
HT_R2 = (1, 6, 1, 1, 1)
LEG_R = (0, 48, 1, 0, 0)


def qos_frame(seq, tid, n=100):
    """A QoS data frame to the access point with the sequence number `seq`."""
    return (bytes([0x88, 0x01, 0, 0]) + bytes(range(2, 20)) + struct.pack('<H', seq << 4) +
            bytes([tid, 0]) + bytes(range(n)))


def core_call(b, name, *args):
    return b.core.call(name, b.core.core, *args)


def ring_new(b, fifo, start, n):
    """The n descriptors at ring index `start` of the core's FIFO: the bytes posted."""
    r = ring(b, txch(fifo), start + n)
    out = []
    for i in range(start, start + n):
        ctrl1, ctrl2, lo, hi = struct.unpack_from('<4I', r, 16 * i)
        out.append(bytes(b.m.read(lo, ctrl2 & 0x1fff)))
    return out


def ampdu_status(b, fifo, fid, ncons, bitmap, counts=(0, 0, 0, 0), reason=0):
    """Complete everything posted on `fifo` and run one aggregate status; returns the
    statuses the core reported and the number of fatal errors."""
    w0 = 1 | ncons << 8 | reason << 4 | fid << 16
    w2 = counts[0] | counts[1] << 8 | counts[2] << 16 | counts[3] << 24
    b.sregs.q.append((w0, 0x1234, w2, 0, 1, bitmap & 0xffffffff, bitmap >> 32, 0))
    b.hw.set_status(txch(fifo), b.hw.posted(txch(fifo), 1), ST_IDLE)
    f0, n0 = b.core.fatals, len(b.core.txs)
    run_irq(b, None, MI_TFS)
    return b.core.txs[n0:], b.core.fatals - f0


def ampdu(a, b, show):
    ensure_scan(a, b)
    bad = 0

    def fail(msg):
        nonlocal bad
        bad += 1
        print('ampdu: ' + msg)

    if not hasattr(b, 'sregs'):
        b.sregs = StatusRegs(b)
    core_call(b, 'bcm4360_core_intrson')        # a fatal error of the status test leaves them off
    FIFO = 0
    if b.posted.get(FIFO):
        fail('FIFO 0 is not unused, the test needs it')
        return bad
    b.posted[FIFO] = []
    posted = b.posted[FIFO]
    sent = []                       # (data, expected frame bytes)

    def tx(seq, rates, tid=0, peer=1, ampdu=1, fifo=FIFO, **kw):
        fr = qos_frame(seq, tid)
        r, data = post(b, fifo, fr, rates, ampdu=ampdu, tid=tid, peer=peer, **kw)
        if data is None:
            fail('frame %#x not taken (%d)' % (seq, r))
            return None
        if fifo == FIFO:
            sent.append((data, fr))
        return data

    core_call(b, 'bcm4360_core_ampdu_tx_start', 1, 0, 0x123, 48, 6)
    core_call(b, 'bcm4360_core_ampdu_tx_start', 1, 1, 0x40, 48, 6)
    # before operational: not aggregated (a session frame sent alone, ampdu.md 1.4)
    d = tx(0x123, [VHT_R, VHT_R2])
    h = hdr_fields(d)
    if h['low'] & 0x40 or not h['high'] & 4 or d[2] & 2:
        fail('session not yet operational: low %#x high %#x prefix %s' % (h['low'], h['high'], h['prefix']))
    core_call(b, 'bcm4360_core_ampdu_tx_operational', 1, 0, 64)
    core_call(b, 'bcm4360_core_ampdu_tx_operational', 1, 1, 32)

    # (name, frames: [(seq, rates, tid, ampdu)], expected prefix epoch bit per frame)
    # the first is the frame sent alone above; sequence number 0x123 was taken (the check
    # starts at the session's ssn, so the first aggregatable MPDU 0x124 is in order).
    steps = [
        ('6 VHT MPDUs: new station/kind, epoch flips to 1',
         [(0x124 + i, [VHT_R, VHT_R2], 0, 1) for i in range(6)], [1] * 6),
        ('non-aggregated frame after an aggregate: epoch flips',
         [(0x200, [LEG_R], 0, 0)], [0]),
        ('6 more VHT MPDUs: kind changed, epoch flips',
         [(0x12a + i, [VHT_R, VHT_R2], 0, 1) for i in range(6)], [1] * 6),
        ('2 HT MPDUs: kind and rate changed',
         [(0x130 + i, [HT_R, HT_R2], 0, 1) for i in range(2)], [0, 0]),
        ('same HT rate again: no flip', [(0x132, [HT_R, HT_R2], 0, 1)], [0]),
        ('other MCS: flip', [(0x133, [HT_R2, HT_R], 0, 1)], [1]),
        ('other TID: flip', [(0x40, [HT_R2, HT_R], 1, 1)], [0]),
        ('same TID, sequence gap: flip', [(0x42, [HT_R2, HT_R], 1, 1)], [1]),
        ('then in order: no flip', [(0x43, [HT_R2, HT_R], 1, 1)], [1]),
        ('other station (no session): sent alone, flip', [(0x50, [HT_R2, HT_R], 2, 1)], [0]),
        ('legacy first rate in a session: sent alone, no flip', [(0x44, [LEG_R], 1, 1)], [0]),
        ('HT again after it: kind changed', [(0x45, [HT_R2, HT_R], 1, 1)], [1]),
    ]
    for name, frames, epochs in steps:
        for (seq, rates, tid, am), ep in zip(frames, epochs):
            peer = 2 if name.startswith('other station') else 1
            d = tx(seq, rates, tid=tid, peer=peer, ampdu=am)
            if d is None:
                continue
            h = hdr_fields(d)
            agg = am and rates[0][0] >= 1 and not peer == 2
            e0 = h['e0'][6]
            want_seq = seq << 4
            bits = ((d[2] >> 1) & 1)
            errs = []
            if agg:
                if h['low'] != 0x40c0 or h['high'] & 4:
                    errs.append('control low %#x high %#x' % (h['low'], h['high']))
                t = bytes.fromhex(h['tail'])
                win = 0x3f if tid == 0 else 0x1f
                # tail starts at header offset 0x64: bytes 0x66.. are t[2..]
                if t[2] != 48 or t[3] != 48 or struct.unpack_from('<H', t, 4)[0] != 0x1526 \
                        or t[6] != win or t[7] != 0:
                    errs.append('cache bytes %s' % t[2:8].hex())
                if (h['e0'][6] & 0xff) != 0x05 or (h['e1'][6] & 0xff) != 0x25:
                    errs.append('rts/cts words %#x %#x' % (h['e0'][6], h['e1'][6]))
            else:
                if h['low'] & 0x40 or (am and not h['high'] & 4) or bytes.fromhex(h['tail'])[2:8] != bytes(6):
                    errs.append('sent alone: control low %#x high %#x' % (h['low'], h['high']))
            if bits != ep:
                errs.append('epoch bit %d, expected %d' % (bits, ep))
            if h['prefix'] != ('02000%d00' % (2 * ep)):
                errs.append('prefix %s' % h['prefix'])
            if h['seq'] != want_seq & 0xffff or struct.unpack_from('<H', d, TXH + 0x7c + 22)[0] != want_seq:
                errs.append('sequence field %#x' % h['seq'])
            if h['low'] & 0x800:
                errs.append('sequence assigned by the microcode')
            if errs:
                fail('%s, seq %#x: %s' % (name, seq, '; '.join(errs)))
    print('ampdu: %d MPDUs posted, headers checked' % len(sent))

    # another FIFO has its own epoch
    d = tx(0x46, [VHT_R], tid=0, fifo=2)
    if d is not None and d[2] != 2:
        fail('FIFO 2: first MPDU prefix %s' % d[:4].hex())

    # aggregate statuses on FIFO 0, in posting order (sent[]: 0 alone, 1..6 VHT, 7 not
    # aggregated, 8..13 VHT, 14..20 HT (one aggregate each by the epoch), 21, 22 alone, 23 HT)
    def fid_of(i):
        return struct.unpack_from('<H', sent[i][0], TXH + 0x0c)[0]

    def complete(i, ncons, bitmap=None, counts=None):
        bitmap = (1 << ncons) - 1 if bitmap is None else bitmap
        txs, fat = ampdu_status(b, FIFO, fid_of(i), ncons, bitmap, counts or (ncons, ncons, 0, 0))
        if fat or len(txs) != ncons or [x[0] for x in txs] != [sent[i + j][1] for j in range(ncons)]:
            fail('status of %d MPDUs at %d: fatal %d, %d reported' % (ncons, i, fat, len(txs)))
        return txs

    complete(0, 1)
    # 6 MPDUs, bitmap 0b111011, w2 = 6 sent, 5 acked, 1 sent at a fallback rate, 0 acked
    txs = complete(1, 6, 0b111011, (6, 5, 1, 0))
    got = [x[1] for x in txs]
    exp = [(1, 1, 2, 1, 6, 5), (1, 0, 0, 1, 0, 0), (0, 0, 0, 1, 0, 0), (1, 0, 0, 1, 0, 0),
           (1, 0, 0, 1, 0, 0), (1, 0, 0, 1, 0, 0)]
    if got != exp:
        fail('aggregate status (hole): got %s, expected %s' % (got, exp))
    else:
        print('ampdu: aggregate status of 6 MPDUs, bitmap 0b111011: acked %s, ampdu_len/ack_len %d/%d' %
              ([x[0] for x in got], got[0][4], got[0][5]))
    complete(7, 1)
    # reason 1: MPDUs 8..10 are posted again unchanged and nothing is reported
    cnt = len(posted)
    txs, fat = ampdu_status(b, FIFO, fid_of(8), 3, 0, reason=1)
    again = ring_new(b, FIFO, cnt, 3)
    if fat or txs or again != [sent[8 + i][0] for i in range(3)]:
        fail('reason 1: fatal %d, %d statuses, re-posted bytes %s' % (
            fat, len(txs), 'same' if again == [sent[8 + i][0] for i in range(3)] else 'differ'))
    else:
        print('ampdu: reason 1 status of 3 MPDUs: re-posted unchanged, nothing reported')
    posted.extend(again)
    # the copies of 8..10 sit behind everything that was posted before the repost
    complete(11, 3)
    for i, n in ((14, 7), (21, 1), (22, 1), (23, 1)):
        complete(i, n)
    txs = complete(8, 3, 0b111)
    if [x[1] for x in txs] != [(1, 0, 1, 1, 3, 3), (1, 0, 0, 1, 0, 0), (1, 0, 0, 1, 0, 0)]:
        fail('after reason 1: %s' % [x[1] for x in txs])
    # a 40 MPDU status with a hole at MPDU 33: the bitmap's second word
    fr = [tx(0x500 + i, [VHT_R, VHT_R2]) for i in range(40)]
    n0 = len(sent) - 40
    bm = ((1 << 40) - 1) ^ (1 << 33)
    txs = complete(n0, 40, bm, (40, 39, 0, 0))
    if [x[1][0] for x in txs] != [0 if i == 33 else 1 for i in range(40)] or txs[0][1][4:6] != (40, 39):
        fail('40 MPDUs with a hole in the high bitmap word')
    else:
        print('ampdu: 40 MPDUs, hole at 33 (v2 bit 1): 39 acknowledged, ampdu_ack_len 39')
    # a status for more MPDUs than were posted is fatal (ampdu.md 2.3 step 3 reads the ring)
    tx(0x600, [VHT_R, VHT_R2])
    txs, fat = ampdu_status(b, FIFO, fid_of(len(sent) - 1), 3, 7)
    if fat != 1:
        fail('NCONS beyond the posted MPDUs: fatal %d' % fat)
    core_call(b, 'bcm4360_core_intrson')

    # tx_stop: a frame with ampdu is sent alone again
    core_call(b, 'bcm4360_core_ampdu_tx_stop', 1, 0)
    d = tx(0x700, [VHT_R, VHT_R2], fifo=3)
    h = hdr_fields(d)
    if h['low'] & 0x40 or not h['high'] & 4:
        fail('after tx_stop: low %#x high %#x' % (h['low'], h['high']))
    print('ampdu: %s' % ('same' if not bad else '%d difference(s)' % bad))
    return bad


# --------------------------------------------------------------- keys, ps

PEER = bytes([0x00, 0x11, 0x22, 0x33, 0x44, 0x55])
OWN_PM = 0x10


def shm_writes(side, n0):
    """The shared-memory writes since trace position n0, as {offset: value} in order."""
    return [(x[2], x[4]) for x in side.trace_from(n0) if x[0] == 'W' and x[1] == 'shm']


def amt_writes(side, n0):
    return [(x[2], x[4]) for x in side.trace_from(n0) if x[0] == 'W' and x[1] == 'amt']


def key_words(key, base=0):
    """The 16 key bytes as the 8 shared-memory words at `base` (ps-keys.md 2.1)."""
    k = (bytes(key) + bytes(16))[:16]
    return [(base + 2 * i, k[2 * i] | k[2 * i + 1] << 8) for i in range(8)]


def set_key(b, cipher, key_id, addr, key, p1k=None, iv32=0):
    m, c = b.m, b.core
    ap = m.buf(addr) if addr else 0
    kp = m.buf(bytes(key))
    pp = m.buf(struct.pack('<5H', *p1k)) if p1k else 0
    return s32(c.call('bcm4360_core_set_key', c.core, cipher, key_id, ap, kp, len(key), pp, iv32))


def keys(a, b, show):
    """ps-keys.md 2.1 to 2.5: the key table, the index block, the AMT binding, the transmit
    header's key bytes and the receive flags. Checked against the spec only (the object's key
    code cannot be called from outside)."""
    bad = 0
    c = b.core
    if not hasattr(b, 'posted'):
        b.posted = {}

    def fail(msg):
        nonlocal bad
        bad += 1
        print('keys: ' + msg)

    # CCMP pairwise (2.7): entry 4, index word 0x45, AMT[0] = peer, attribute 0x8004
    ck = bytes(range(0x10, 0x20))
    n0 = len(b.chip.trace)
    r = set_key(b, 5, 0, PEER, ck)
    want = [(0x5e0 + 8, 0x45)] + key_words(ck, 16 * 4)
    if r != 4 or shm_writes(b, n0) != want or amt_writes(b, n0) != [(0, 0), (4, 0),
                                                                    (0, 0x33221100), (4, 0x80045544)]:
        fail('CCMP pairwise: entry %d, shm %s, amt %s' % (r, shm_writes(b, n0), amt_writes(b, n0)))
    # CCMP group key id 1: entry 1, 0x15, no AMT write
    n0 = len(b.chip.trace)
    r = set_key(b, 5, 1, None, ck)
    if r != 1 or shm_writes(b, n0)[0] != (0x5e0 + 2, 0x15) or amt_writes(b, n0):
        fail('CCMP group: entry %d, shm %s, amt %s' % (r, shm_writes(b, n0)[:1], amt_writes(b, n0)))
    # TKIP group: not in hardware; bad lengths and ciphers
    tk = bytes(range(0x30, 0x50))
    for name, got in (('TKIP group', set_key(b, 2, 2, None, tk)),
                      ('short CCMP key', set_key(b, 5, 0, PEER, ck[:8])),
                      ('cipher 4', set_key(b, 4, 0, PEER, ck))):
        if got not in (-95, -22):
            fail('%s: %d' % (name, got))
    # TKIP pairwise replaces the CCMP key of the same peer: entry 4 again, 0x42, phase-1 block
    p1k = (0x1111, 0x2222, 0x3333, 0x4444, 0x5555)
    n0 = len(b.chip.trace)
    r = set_key(b, 2, 0, PEER, tk, p1k, 0x00030002)
    sw = shm_writes(b, n0)
    want = [(0x5e0 + 8, 0x42)] + key_words(tk, 16 * 4) + \
        [(0x2e0 + 14 * 4 + 2 * i, p1k[i]) for i in range(5)] + \
        [(0x2e0 + 14 * 4 + 10, 2), (0x2e0 + 14 * 4 + 12, 3)]
    if r != 4 or sw != want:
        fail('TKIP pairwise: entry %d, shm %s' % (r, sw))
    # a second peer gets entry 5
    if set_key(b, 5, 0, bytes([2, 3, 4, 5, 6, 7]), ck) != 5:
        fail('second peer is not entry 5')
    # update of the receive phase-1 key: the seven words only
    n0 = len(b.chip.trace)
    c.call('bcm4360_core_tkip_rx_update', c.core, 4, 0x10005, b.m.buf(struct.pack('<5H', 1, 2, 3, 4, 5)))
    want = [(0x2e0 + 14 * 4 + 2 * i, i + 1) for i in range(5)] + \
        [(0x2e0 + 14 * 4 + 10, 5), (0x2e0 + 14 * 4 + 12, 1)]
    if shm_writes(b, n0) != want or amt_writes(b, n0):
        fail('tkip_rx_update: %s' % shm_writes(b, n0))

    # transmit header (2.4): CCMP entry 5, TKIP entry 4 with phase-1 key and IV bytes 0..2
    def hdr(fifo, key_entry, cipher_iv, **kw):
        fc = bytes([0x88, 0x41, 0, 0]) + PEER + bytes(range(6, 18)) + bytes([0x10, 0, 0, 0]) + cipher_iv
        fr = fc + bytes(range(40))
        r, d = post(b, fifo, fr, [LEG_R], hw_key=key_entry + 1, **kw)
        return r, d, fr

    b.posted.setdefault(2, [])
    ccmp_iv = bytes([1, 0, 0, 0x20, 0, 0, 0, 0])
    r, d, fr = hdr(2, 5, ccmp_iv)
    if r < 0:
        fail('CCMP frame not taken (%d)' % r)
    else:
        h = d[TXH:]
        flen = struct.unpack_from('<H', h, 0x0a)[0]
        if h[0x64] != 0x50 or h[0x65] != 5 or flen != len(fr) + 4 + 8 or h[0x66:0x7c] != bytes(22):
            fail('CCMP header: %02x %02x len %d %s' % (h[0x64], h[0x65], flen, h[0x66:0x7c].hex()))
    iv = bytes([0x00, 0x20, 0x02, 0x20, 0, 0, 0, 0])
    r, d, fr = hdr(2, 4, iv, p1k=p1k)
    if r < 0:
        fail('TKIP frame not taken (%d)' % r)
    else:
        h = d[TXH:]
        flen = struct.unpack_from('<H', h, 0x0a)[0]
        if (h[0x64], h[0x65], flen) != (0x20, 4, len(fr) + 4 + 4) or \
                h[0x6c:0x76] != struct.pack('<5H', *p1k) or h[0x76:0x79] != iv[:3]:
            fail('TKIP header: %02x %02x len %d %s' % (h[0x64], h[0x65], flen, h[0x6c:0x79].hex()))
    # refused: unknown entry, frame without the Protected bit, frame too short for its IV
    r, _ = post(b, 2, bytes([0x88, 0x01, 0, 0]) + PEER + bytes(30), [LEG_R], hw_key=5)
    r2, _ = post(b, 2, bytes([0x88, 0x41, 0, 0]) + PEER + bytes(30), [LEG_R], hw_key=40)
    r3, _ = post(b, 2, bytes([0x88, 0x41, 0, 0]) + PEER + bytes(14), [LEG_R], hw_key=5)
    if (r, r2, r3) != (-1, -1, -1):
        fail('refused frames returned %s' % ((r, r2, r3),))
    # without a key: bytes 0x64.. stay 0
    r, d = post(b, 2, qos_frame(5, 0), [LEG_R])
    if r < 0 or d[TXH + 0x64:TXH + 0x7c] != bytes(0x18):
        fail('plain frame has key bytes')

    # delete: the entry empty, AMT cleared keeping the attribute 0x0004 (2.3 step 2)
    n0 = len(b.chip.trace)
    c.call('bcm4360_core_del_key', c.core, 5)
    want = [(0x5e0 + 10, 0x50)] + key_words(b'', 16 * 5)
    if shm_writes(b, n0) != want or amt_writes(b, n0) != [(8, 0), (12, 0x00040000)]:
        fail('del_key: shm %s, amt %s' % (shm_writes(b, n0)[:2], amt_writes(b, n0)))
    for e in (4, 1):
        c.call('bcm4360_core_del_key', c.core, e)
    print('keys: %s' % ('same' if not bad else '%d difference(s)' % bad))
    return bad


def beacon_vec(rng, bssid):
    v = vec(rng, 'beacon', bytes([2, 0x11, 0x22, 0, 0, 0]), 1)
    fr = bytes([0x80, 0, 0, 0]) + bytes([0xff] * 6) + bssid + bssid + bytes(2) + \
        struct.pack('<QHH', 0x123456, 100, 0x11) + bytes(range(16)) + bytes(4)
    body = bytes([2, 0x11, 0x22, 0, 0, 0]) + fr
    v.update(frame=fr, body=body, size=len(body), nlen=len(fr))
    return v


def mctrl_bits(side, n0):
    """(HPS, WAKE) in D11(0x120) after the steps since n0 (suspend / enable of the MAC toggle
    WAKE inside a step, so only the end state is compared)."""
    v = side.d11.peek(0x120)
    return (v >> 25 & 1, v >> 26 & 1)


def ps(a, b, show):
    """ps-keys.md 1.3 to 1.5, 1.8, 1.9: the HPS and WAKE bits through the join, a beacon, the
    null frame with the PM bit, its status, and the null frame without it. Spec only."""
    bad = 0
    c, m = b.core, b.m
    rng = random.Random(3)
    if not hasattr(b, 'posted'):
        b.posted = {}

    def fail(msg):
        nonlocal bad
        bad += 1
        print('ps: ' + msg)

    def bits(fn):
        n0 = len(b.chip.trace)
        fn()
        return mctrl_bits(b, n0)

    def null(pm):
        fr = bytes([0x48, 0x01 | (OWN_PM if pm else 0), 0, 0]) + PEER + bytes(range(6, 12)) + PEER + bytes(2)
        r, d = post(b, 1, fr, [LEG_R])
        return r, d

    def done(d, w0low):
        fid = struct.unpack_from('<H', d, TXH + 0x0c)[0]
        b.sregs.q.append((w0low | 1 << 8 | fid << 16, 0, 1, 0, 1, 0, 0, 0))
        b.hw.set_status(txch(1), b.hw.posted(txch(1), 1), ST_IDLE)
        run_irq(b, None, MI_TFS)

    if not hasattr(b, 'sregs'):
        b.sregs = StatusRegs(b)
    core_call(b, 'bcm4360_core_intrson')
    b.posted.setdefault(1, [])
    bss = bytearray(32)
    bss[0:6] = PEER
    bss[6] = 1
    struct.pack_into('<HH', bss, 8, 1, 100)
    bss[12] = 2
    bp = m.buf(bytes(bss))
    # the join holds WAKE until the first beacon (1.8); no HPS
    got = bits(lambda: c.call('bcm4360_core_set_bss', c.core, bp))
    if got != (0, 1):
        fail('join: %s' % (got,))
    got = bits(lambda: (deliver(b, [rx_bytes(beacon_vec(rng, bytes([9] * 6)), a.cs)]),
                        run_irq(b, None, MI_DMAINT)))
    if got != (0, 1):
        fail('foreign beacon: %s' % (got,))
    got = bits(lambda: (deliver(b, [rx_bytes(beacon_vec(rng, PEER), a.cs)]),
                        run_irq(b, None, MI_DMAINT)))
    if got != (0, 0):
        fail('beacon of the BSS: %s' % (got,))
    # no key was set: power save is allowed. A null frame with PM: HPS, then WAKE (pending)
    res = {}
    got = bits(lambda: res.update(r=null(True)))
    if got != (1, 1) or res['r'][0] < 0:
        fail('null with PM: %s' % (got,))
    got = bits(lambda: done(res['r'][1], 1 | 0x8000))      # acknowledged, "PM indicated" missing
    if got != (1, 1):
        fail('ack without PM indicated: %s' % (got,))
    got = bits(lambda: (res.update(r=null(True)), done(res['r'][1], 1 | 0x8000 | 8)))
    if got != (1, 0):
        fail('ack with PM indicated: %s' % (got,))
    # configuration says power save off: HPS off, WAKE held for the frame that ends it
    got = bits(lambda: c.call('bcm4360_core_set_ps', c.core, 0))
    if got != (0, 1):
        fail('set_ps(0): %s' % (got,))
    got = bits(lambda: res.update(r=null(False)))
    if got != (0, 1) or res['r'][0] < 0:
        fail('null without PM: %s' % (got,))
    got = bits(lambda: done(res['r'][1], 1 | 0x8000))
    if got != (0, 0):
        fail('status of the leave frame: %s' % (got,))
    # a group key must exist first once a key was set (1.2 step 4)
    c.call('bcm4360_core_key_state', c.core, 1, 0)
    got = bits(lambda: c.call('bcm4360_core_set_ps', c.core, 1))
    if got != (0, 0):
        fail('set_ps with a pairwise key only: %s' % (got,))
    got = bits(lambda: c.call('bcm4360_core_key_state', c.core, 1, 1))
    if got != (1, 1):
        fail('group key arrives: %s' % (got,))
    got = bits(lambda: c.call('bcm4360_core_set_bss', c.core, m.buf(bytes(32))))
    if got != (0, 0):
        fail('disassociation: %s' % (got,))
    print('ps: %s' % ('same' if not bad else '%d difference(s)' % bad))
    return bad


# ------------------------------------------------------------ power, chains

def rspec_vectors(rng, n):
    """Rate specifications (coverage.md G1): legacy (CCK, OFDM, other), HT MCS 0..32,
    VHT 1..3 streams MCS 0..9, bandwidths 0..4, STBC and beamforming bits."""
    cck, ofdm = (2, 4, 11, 22), (12, 18, 24, 36, 48, 72, 96, 108)
    for _ in range(n):
        enc = rng.choice((0, 0, 1, 1, 2, 2))
        bw = rng.choice((1, 1, 2, 2, 3, 3, 0, 4))
        v = enc << 24 | bw << 16
        if enc == 0:
            v |= rng.choice(cck + ofdm)
        elif enc == 1:
            v |= rng.choice(list(range(24)) + [32])
        else:
            v |= rng.randrange(0, 10) | rng.randrange(1, 4) << 4
        if rng.random() < 0.3:
            v |= 0x100000
        if rng.random() < 0.3:
            v |= 0x200000
        yield v


def fill_stf(rng, side_stf, side_m, ppr_bytes, state):
    """The same random stf state (cca-stf.md "The stf state") and container in a block."""
    for off, val in state.items():
        side_m.write(side_stf + off, bytes([val]))
    p = side_m.r64(side_stf + 0x30)
    side_m.write(p + 4, ppr_bytes)


def power(a, b, show):
    """coverage.md G1: the object's wlc_stf_spatial_expansion_get, wlc_stf_get_pwrperrate and
    wlc_acphy_txctl1_calc against the core's, for random stf states, containers, chanspecs
    and rate specifications."""
    rng = random.Random(7)
    om, cm = a.m, b.m
    wo = a.run.d.wlc
    wc = b.core.call('core_test_block', b.core.core, 0)
    so, sc = om.r64(wo + 0x550), b.core.call('core_test_block', b.core.core, 1)
    po, pc = om.r64(wo), b.core.call('core_test_block', b.core.core, 2)
    ptr_o, ptr_c = om.r64(so + 0x30), cm.r64(sc + 0x30)
    if not ptr_o or not ptr_c:
        print('power: no stf container (object %#x, core %#x)' % (ptr_o, ptr_c))
        return 1
    bwo, bwc = om.r32(ptr_o), cm.r32(ptr_c)
    if bwo != bwc or bwo > 2:
        print('power: containers of different bandwidth (%d / %d)' % (bwo, bwc))
        return 1
    size = 182 * (bwo + 1)
    save = [(om, so, bytes(om.read(so, 0x90))), (cm, sc, bytes(cm.read(sc, 0x90))),
            (om, po + 0xe8, bytes(om.read(po + 0xe8, 1))), (cm, pc + 0xe8, bytes(cm.read(pc + 0xe8, 1))),
            (om, wo + 0x518, bytes(om.read(wo + 0x518, 2))), (cm, wc + 0x518, bytes(cm.read(wc + 0x518, 2)))]
    save_ppr = [(om, ptr_o + 4, bytes(om.read(ptr_o + 4, size))), (cm, ptr_c + 4, bytes(cm.read(ptr_c + 4, size)))]
    n = bad = 0
    stats = {'skipped': 0, 'olpc': 0, 'exp': set(), 'p': set()}
    for it in range(100):
        t = rng.choice((1, 2, 2, 2, 3))
        state = {0x00: 3, 0x01: rng.choice((1, 2, 3, 3, 6)), 0x02: t, 0x88: rng.choice((0, 5, 21, 25, 29, 31))}
        for i in range(6):
            state[0x16 + 2 * i] = rng.choice((1, 1, 2, 3, 4))
            state[0x17 + 2 * i] = rng.choice((0, 1, 2, 3, 3, 5, 6, 7))
            state[0x22 + i] = rng.choice((0, 0, 0, 1, 3, 7))
        ppr = bytes(0x80 if rng.random() < 0.08 else (rng.randrange(0, 32) if rng.random() < 0.9
                                                       else rng.randrange(216, 256))
                    for _ in range(size))
        cap = rng.choice((0, 1))
        cs = (rng.randrange(0, 8) << 8) | rng.choice((0xe024, 0xe02a, 0x1001, 0xd0a5, 0xe0a4))
        for m, st in ((om, so), (cm, sc)):
            fill_stf(rng, st, m, ppr, state)
        om.write(po + 0xe8, bytes([cap])); cm.write(pc + 0xe8, bytes([cap]))
        om.write(wo + 0x518, struct.pack('<H', cs)); cm.write(wc + 0x518, struct.pack('<H', cs))
        for rs in rspec_vectors(rng, 50):
            eo = om.call('wlc_stf_spatial_expansion_get', wo, rs) & 0xff
            ec = b.core.call('core_stf_spatial_expansion_get', b.core.core, rs) & 0xff
            e = eo
            to_ = om.call('wlc_stf_txchain_get', wo, rs) & 0xff
            tc_ = b.core.call('core_stf_txchain_get', b.core.core, rs) & 0xff
            if to_ != tc_:
                bad += 1
                if bad <= show:
                    print('power: txchain_get of rspec %#010x (t=%d, txchain %#x, masks %s, ovr %s): object %d, core %d'
                          % (rs, t, state[1], [state[0x17 + 2 * i] for i in range(6)],
                             [state[0x22 + i] for i in range(6)], to_, tc_))
            try:
                po_ = om.call('wlc_stf_get_pwrperrate', wo, rs, e) & 0xff
            except EmuError:
                stats['skipped'] = stats.get('skipped', 0) + 1     # a group the object's table lacks
                continue
            pc_ = b.core.call('core_stf_get_pwrperrate', b.core.core, rs, e) & 0xff
            wo_ = om.call('wlc_acphy_txctl1_calc', wo, rs) & 0xffff
            wc_ = b.core.call('core_txctl1_calc', b.core.core, rs) & 0xffff
            n += 3
            stats['exp'].add(eo)
            stats['p'].add(po_)
            if wo_ >> 3 & 0x3f != po_ & 0x3f:
                stats['olpc'] += 1
            for what, x, y in (('expansion', eo, ec), ('pwrperrate', po_, pc_), ('txctl1', wo_, wc_)):
                if x != y:
                    bad += 1
                    if bad <= show:
                        print('power: %s of rspec %#010x (t=%d cs=%#06x cap=%d): object %#x, core %#x'
                              % (what, rs, t, cs, cap, x, y))
    for m, off, raw in save + save_ppr:
        if off in (so, sc):
            m.write(off, raw[:0x30]); m.write(off + 0x38, raw[0x38:])
        else:
            m.write(off, raw)
    print('power: %d calls compared with the object (expansions seen %s, %d words changed by the '
          'open-loop offset, %d different power values, %d rates skipped: no group in the object)'
          % (n, sorted(stats['exp']), stats['olpc'], len(stats['p']), stats['skipped']))
    print('power: %s' % ('same' if not bad else '%d difference(s)' % bad))
    return bad


def stf_bytes(side, wlc_stf):
    raw = bytes(side.m.read(wlc_stf, 0x90))
    return raw[:0x30] + raw[0x38:]


def chains(a, b, show):
    """coverage.md G5: wlc_stf_txchain_set / wlc_stf_rxchain_set of the object against the
    core's: results, the stf block and the accesses to the card."""
    om, cm = a.m, b.m
    wo = a.run.d.wlc
    so = om.r64(wo + 0x550)
    sc = b.core.call('core_test_block', b.core.core, 1)
    bad = 0
    # the same starting state: attach-time fields that are not the subject (stf+0x7, 0x4d.., 0x68..)
    raw = bytes(om.read(so, 0x90))
    cm.write(sc, raw[:0x30])
    cm.write(sc + 0x38, raw[0x38:])
    # no frame pending in either (the earlier checks leave frames in the rings)
    co, cc = om.r64(wo + 0x38), cm.r64(b.core.call('core_test_block', b.core.core, 0) + 0x38)
    for m, blk in ((om, co), (cm, cc)):
        m.write(blk + 0x38, bytes(8))
    # (what, mask, force, id)
    steps = [('tx', 1, 0, 0), ('tx', 3, 0, 0), ('tx', 1, 0, 4), ('tx', 2, 1, 3), ('tx', 3, 0, 4),
             ('tx', 4, 0, 0), ('tx', 3, 0, 3), ('rx', 1, 0, 0), ('rx', 3, 0, 0), ('rx', 7, 0, 0),
             ('rx', 2, 0, 0), ('rx', 3, 0, 0), ('tx', 3, 0, 0)]
    for k, (w, mask, force, rid) in enumerate(steps):
        na, nb = len(a.chip.trace), len(b.chip.trace)
        if w == 'tx':
            ro = s32(om.call('wlc_stf_txchain_set', wo, mask, force, rid))
            rc = s32(b.core.call('core_stf_txchain_set', b.core.core, mask, force, rid))
        else:
            ro = s32(om.call('wlc_stf_rxchain_set', wo, mask))
            rc = s32(b.core.call('core_stf_rxchain_set', b.core.core, mask))
        ta, tb = a.trace_from(na), b.trace_from(nb)
        # not compared: the core's re-read of the beamforming base (SHM 0xb0)
        ta, tb = ([x for x in t if x[:3] != ('R', 'shm', 176)] for t in (ta, tb))
        if os.environ.get("CHAINS_DUMP") and k == int(os.environ["CHAINS_DUMP"]):
            for x in ta:
                print('  object', x)
            for x in tb:
                print('  core  ', x)
        fo, fc = stf_bytes(a, so), stf_bytes(b, sc)
        msg = []
        if ro != rc:
            msg.append('result: object %d, core %d' % (ro, rc))
        for off in range(len(fo)):
            if fo[off] != fc[off]:
                real = off + (8 if off >= 0x30 else 0)
                msg.append('stf+%#x: object %#x, core %#x' % (real, fo[off], fc[off]))
        if ta != tb:
            for i, (x, y) in enumerate(zip(ta, tb)):
                if x != y:
                    msg.append('access %d: object %s, core %s' % (i, x, y))
                    break
            else:
                msg.append('%d accesses of the object, %d of the core' % (len(ta), len(tb)))
        print('chains: %s mask %d force %d id %d -> %d (%d accesses)%s' % (w, mask, force, rid, ro, len(ta),
                                                                           '' if not msg else ' DIFFERS'))
        for x in msg[:show]:
            print('    ' + x)
        bad += 1 if msg else 0
    # G5 step 5: a chain change that removes a chain waits while a frame is pending, and
    # completes when the FIFO has drained (wlc_txfifo_complete / core_txfifo_complete)
    wc = b.core.call('core_test_block', b.core.core, 0)
    for m, blk in ((om, co), (cm, cc)):
        m.write(blk + 0x38, struct.pack('<h', 1))
    na, nb = len(a.chip.trace), len(b.chip.trace)
    ro = s32(om.call('wlc_stf_txchain_set', wo, 1, 0, 0))
    rc = s32(b.core.call('core_stf_txchain_set', b.core.core, 1, 0, 0))
    msg = []
    if (ro, rc) != (0, 0) or len(a.chip.trace) != na or len(b.chip.trace) != nb:
        msg.append('the change was not deferred (results %d / %d)' % (ro, rc))
    fl = (om.read(wo + 0x304, 1)[0], cm.read(wc + 0x304, 1)[0])
    if fl[0] & 0x20 == 0 or fl[1] & 0x20 == 0 or stf_bytes(a, so) != stf_bytes(b, sc):
        msg.append('flag bit 5 %#x / %#x or stf block differs while pending' % fl)
    na, nb = len(a.chip.trace), len(b.chip.trace)
    om.call('wlc_txfifo_complete', wo, 0, 1)
    cm.write(cc + 0x38, struct.pack('<h', 0))
    b.core.call('core_txfifo_complete', b.core.core)
    ta, tb = a.trace_from(na), b.trace_from(nb)
    ta, tb = ([x for x in t if not (x[1] == 'd11' and x[2] in (288, 296)) and
               x[:3] not in (('R', 'shm', 64), ('R', 'shm', 176))] for t in (ta, tb))
    if ta != tb or stf_bytes(a, so) != stf_bytes(b, sc) or om.read(so + 1, 1)[0] != 1:
        msg.append('after the drain: %d / %d accesses, chains %#x / %#x' % (
            len(ta), len(tb), om.read(so + 1, 1)[0], cm.read(sc + 1, 1)[0]))
    print('chains: deferred while a frame is pending, completed at the drain%s' %
          ('' if not msg else ' DIFFERS'))
    for x in msg:
        print('    ' + x)
    bad += 1 if msg else 0
    for m, blk in ((om, co), (cm, cc)):
        m.write(blk + 0x38, bytes(8))
    om.call('wlc_stf_txchain_set', wo, 3, 0, 0)
    b.core.call('core_stf_txchain_set', b.core.core, 3, 0, 0)
    print('chains: %s' % ('same' if not bad else '%d difference(s)' % bad))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('what', nargs='*', default=['receive', 'transmit', 'status', 'ampdu', 'power', 'chains', 'keys', 'ps'])
    ap.add_argument('--show', type=int, default=10)
    ns = ap.parse_args()
    a, b = bring_up()
    bad = 0
    for w in ns.what:
        bad += globals()[w](a, b, ns.show)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
