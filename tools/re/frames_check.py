#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
The core's frame path (open/core/frames.c, rx.c, tx.c) against Broadcom's
object on the same card model (docs/re/tasks/core-frames.md).

    frames_check.py [receive] [transmit] [status] [--show N]

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

The object is used as a black box through its public entry points and the
functions frames.md names (wlc_isr, wlc_dpc, wlc_recv, wlc_txfifo).
"""
import argparse
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
        return standalone.window_free(ab.logical(self.chip.trace[n:], names))

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
            self.txs.append((bytes(m.read(p.data, p.len)), tuple(m.read(ts, 4))))
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
    if v['st1'] & 8 and not v['st1'] & 0x10:
        fl |= F_DEC
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


def txparams(m, fifo, rates, no_ack=0, rts=0, cts=0, seq_by_hw=0):
    """struct bcm4360_txparams in the machine's memory; rates: (enc, rate, nss, bw, flags)."""
    raw = bytearray(30)
    raw[0], raw[1] = fifo, len(rates)
    for i, r in enumerate(rates):
        raw[2 + 6 * i:2 + 6 * i + 6] = bytes(list(r) + [0] * (5 - len(r)) + [1])
    raw[26], raw[27], raw[28], raw[29] = no_ack, rts, cts, seq_by_hw
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
        f['e%d' % i] = (w0, w1 & ~0x1f8, w2, e[6:12].hex(), e[12:14].hex(),
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
                if st != want or data != b.fifo3_frames[k]:
                    bad += 1
                    print('status %-26s core %s, expected %s%s' % (
                        name, st, want, '' if data == b.fifo3_frames[k] else ' (frame bytes differ)'))
        if fatal:
            break
    print('status: %s' % ('same' if not bad else '%d difference(s)' % bad))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('what', nargs='*', default=['receive', 'transmit', 'status'])
    ap.add_argument('--show', type=int, default=10)
    ns = ap.parse_args()
    a, b = bring_up()
    bad = 0
    for w in ns.what:
        bad += globals()[w](a, b, ns.show)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
