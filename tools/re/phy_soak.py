#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
PHY soak: a long session for the open PHY inside the hybrid core.

hybrid.py's session runs about half of the open PHY's code: nine channels, two
watchdog rounds, no association, and the card model measures nothing (every
temperature, IQ power and noise reading is 0). This session runs, on
Broadcom's object alone and on the hybrid core (by default with the open PHY,
--on phy), and compares every access to the card and the result of every step:

  * every channel of both bands in every bandwidth, with band switches;
  * an association stand-in - the upper layer's own wlc_sta_assoc_upd,
    wlc_clr_quiet_chanspec with wlc_mute(0) and wlc_full_phy_cal, as its join
    does (roaming switched off, the model sends no beacons; without the unmute
    the PHY's hold word keeps its mute bit and every calibration returns at
    once) - the full calibration; then long watchdog runs: tempsense, the
    noise requests, the periodic calibration (past 900 s), the transmit power
    recalculation;
  * channel changes and a down/up while associated (the up calibrates);
  * per-frame RSSI computed on many varied receive headers;
  * the noise interrupt and the noise average;
  * transmit power changes and the interference set-up of a join.

The card model gives the measurements values here, the same in both runs and
depending only on the order of the reads (so both see the same values as long
as they read the same registers in the same order): the auxiliary ADC
PHY(0x13) of the temperature sense (temperatures from about 10 to 95 degrees
C), the receive IQ power estimate PHY(c*0x200 + 0x6c0..0x6c5), and the noise
powers SHM(0x308..0x313).

With --board N the card is a variant of the synthetic one: the SROM variables
the PHY reads (power limits and PA coefficients, receive gains, temperature
offset and thresholds, chains and antennas, per-rate power offsets, noise and
gain-error corrections) changed at random within plausible ranges, seed N.

    phy_soak.py [--on L,..] [--off L,..] [--show N] [--steps a,b,..] [--board N]
"""
import argparse
import os
import random
import sys

import ab
import hybrid
from emu import EmuError

CH_2G = [str(c) for c in range(1, 14)] + ['14', '1l', '5l', '9l', '5u', '9u', '13u']
CH_5G_20 = [str(c) for c in list(range(36, 65, 4)) + list(range(100, 145, 4)) +
            list(range(149, 166, 4))]
CH_5G_40 = ['36l', '40u', '44l', '48u', '52l', '56u', '60l', '64u', '100l', '104u', '108l',
            '112u', '116l', '120u', '124l', '128u', '132l', '136u', '140/40', '149l', '153u',
            '157l', '161u']
CH_5G_80 = ['%d/80' % c for c in list(range(36, 65, 4)) + list(range(100, 145, 4)) +
            list(range(149, 162, 4))]

STEPS = (['attach', 'mpc 0', 'up', 'roam_off 1'] + CH_2G + CH_5G_20 + CH_5G_40 + CH_5G_80 +
         ['6', '36/80', '1', '165', 'watchdog', 'rssi 1',
          # associated on 36/80
          '36/80', 'assoc', 'unquiet', 'joincal', 'interf 1', 'wait 5', 'rssi 2', 'noise 1',
          'wait 20', 'rssi 3', 'noise 2', 'qtxpower 60', 'wait 130', 'rssi 4', 'noise 3',
          # channel and band changes while associated
          '6', 'unquiet', 'joincal', 'wait 15', 'rssi 5', 'noise 4', '1l', 'unquiet',
          'wait 15', 'rssi 6', '149/80', 'unquiet', 'joincal', 'interf 1', 'wait 130',
          'rssi 7', 'qtxpower 127', 'wait 20',
          # past 900 s since the last calibration: the periodic (multi-phase) one
          'wait 920', 'rssi 10', 'noise 6',
          '52l', 'wait 15', 'rssi 8',
          # down and up while associated: the up calibrates again
          'down', 'up', 'wait 20', 'rssi 9', 'noise 5', 'interf 0', 'wait 15',
          'down', 'detach'])

WLC_GET_PHY_NOISE = 135             # src/include/wlioctl.h
WLC_CMI = 0x1a0                     # wlc->cmi, the channel manager (as wlc_join_BSS passes it)
AUX_ADC = 0x13                      # PHY: the temperature sense's ADC readout
IQEST = (0x6c0, 0x6c6)              # PHY, per core +0x200: I*Q, I^2, Q^2 (lo, hi)
NOISE_SHM = (0x308, 0x314)          # SHM: noise power per core (lo, hi)
TEMP_AMP = (140, 100, 60, 40, 200)  # ADC swing -> about 40, 64, 85, 96, 12 degrees C
TEMP_SIGN = (1, -1, 1, -1)          # by phase of the measurement


class Measurements:
    """Deterministic readings for the measurement registers of the model."""

    def __init__(self, chip):
        self.chip = chip
        self.n_adc = 0
        self.iq = {}                # core -> number of estimates read
        self.n_noise = {}
        self.phy_read0 = chip.phy_read
        self.obj_read0 = chip.obj_read
        chip.phy_read = self.phy_read
        chip.obj_read = self.obj_read

    def phy_read(self, addr, value):
        value = self.phy_read0(addr, value)
        if addr == AUX_ADC:
            n = self.n_adc
            self.n_adc += 1
            t = TEMP_SIGN[(n // 8) % 4] * TEMP_AMP[(n // 128) % len(TEMP_AMP)] + (n * 7) % 5 - 2
            return (t & 0x3ff) << 2
        core, reg = addr >> 9, addr & 0x1ff
        if core < 3 and IQEST[0] <= reg < IQEST[1]:
            if reg == IQEST[0]:
                self.iq[core] = self.iq.get(core, 0) + 1
            k = self.iq.get(core, 0)
            words = ((3000 + 37 * ((k * 13 + core * 7) % 50) - 900) & 0xffffffff,
                     900000 + 1000 * ((k * 17 + core) % 97),
                     880000 + 1100 * ((k * 11 + core * 3) % 89))
            w = words[(reg - IQEST[0]) // 2]
            return (w >> 16) & 0xffff if (reg - IQEST[0]) & 1 else w & 0xffff
        return value

    def obj_read(self, space, addr, size, value):
        value = self.obj_read0(space, addr, size, value)
        if space == 'shm' and NOISE_SHM[0] <= addr < NOISE_SHM[1]:
            k = self.n_noise.get(addr, 0)
            self.n_noise[addr] = k + 1
            power = (0x40, 0x180, 0x700, 0x1c00, 0x6000, 0x18000)[(k + addr // 4) % 6]
            power += (k * 2311 + addr * 13) % 0x3f
            if size == 4:
                return power & 0xffffffff
            return (power >> 16) & 0xffff if addr & 2 else power & 0xffff
        return value


def board(seed):
    """A board variant: the synthetic card's variables with those the PHY reads
    changed at random within plausible ranges (seeded), as a revision-11 SROM
    image. 0: the synthetic card itself."""
    from blob import Blob
    from run_attach import DEFAULT_SROM
    from srom import Layout
    v = {}
    for line in open(DEFAULT_SROM):
        line = line.strip()
        if line and not line.startswith('#') and '=' in line:
            k, _, val = line.partition('=')
            v[k] = val
    rng = random.Random(seed)

    def arr(name, f):
        v[name] = ','.join(str(f(int(x, 0))) for x in v[name].split(','))

    for p in '01':
        v['maxp2ga' + p] = str(rng.randrange(52, 91))
        arr('maxp5ga' + p, lambda x: rng.randrange(52, 91))
        arr('pa2ga' + p, lambda x: '0x%x' % ((x + rng.randrange(-0x30, 0x31)) & 0xffff))
        arr('pa5ga' + p, lambda x: '0x%x' % ((x + rng.randrange(-0x30, 0x31)) & 0xffff))
        for b in ('2g', '5g', '5gm', '5gh'):
            v['rxgains%selnagaina%s' % (b, p)] = str(rng.randrange(8))
            v['rxgains%strisoa%s' % (b, p)] = str(rng.randrange(16))
            v['rxgains%strelnabypa%s' % (b, p)] = str(rng.randrange(2))
    v['tempoffset'] = str(rng.choice([255, rng.randrange(0, 60)]))
    v['rawtempsense'] = '0x%x' % rng.choice([0x1ff, rng.randrange(20, 80)])
    v['tempthresh'] = str(rng.choice([255, rng.randrange(85, 116)]))
    v['phycal_tempdelta'] = str(rng.choice([255, rng.randrange(15, 41)]))
    for k in ('measpower', 'measpower1', 'measpower2'):
        v[k] = '0x%x' % rng.randrange(0x80)
    v['txchain'] = v['rxchain'] = str(rng.choice([3, 3, 1]))
    v['aa2g'] = v['aa5g'] = str(rng.choice([3, 3, 1]))
    for b in ('2g', '5g'):
        v['epagain' + b] = str(rng.randrange(8))
        v['pdgain' + b] = str(rng.randrange(32))
        v['tworangetssi' + b] = str(rng.randrange(2))
        v['papdcap' + b] = str(rng.randrange(2))
    v['gainctrlsph'] = str(rng.randrange(2))
    for k in list(v):
        if k.endswith('po') and k.startswith(('mcs', 'cck', 'ofdm', 'dot11', 'sb')):
            v[k] = '0x%x' % rng.randrange(1 << (32 if k.startswith('mcs') else 16))
        elif k.startswith(('noiselvl', 'rxgainerr')):
            arr(k, lambda x: rng.randrange(64))
        elif k.startswith('pdoffset'):
            v[k] = '0x%x' % rng.randrange(0x10000)
    v['sar2g'], v['sar5g'] = str(rng.randrange(10, 21)), str(rng.randrange(10, 21))
    return Layout(Blob(hybrid.BLOB)).encode(v)


class SoakRun(hybrid.Run):
    def __init__(self, path=None, off=(), on=(), **card):
        super().__init__(path, off=off, on=on, **card)
        self.s.chip.MAX_TRACE = 40000000
        Measurements(self.s.chip)
        self.marks = []             # length of the trace at the start of each step

    def logical_steps(self):
        """The compared accesses of each step (as Run.logical, step by step)."""
        trace = self.s.chip.trace
        bounds = self.marks + [len(trace)]
        out = []
        for i in range(len(self.marks)):
            part = []
            for a in ab.logical(trace[bounds[i]:bounds[i + 1]], self.s.chip.func_name):
                if a[1] == 'pcicfg' and a[2] in hybrid.WINDOW_REGS:
                    continue
                key = (a[0], a[1], a[2], a[3])
                if key in hybrid.UNDEFINED_TBL and isinstance(a[4], int):
                    a = a[:4] + (a[4] & hybrid.UNDEFINED_TBL[key],) + a[5:]
                part.append(a)
            out.append(part)
        return out

    def pi(self):
        m = self.m
        return m.r64(m.r64(self.d.wlc + 0x40) + 0x10)      # wlc->band->pi

    def step(self, what):
        m, d = self.m, self.d
        self.marks.append(len(self.s.chip.trace))
        word, _, arg = what.partition(' ')
        if word == 'wait':
            m.os.run_timers(m, m.os.now_us // 1000 + int(arg) * 1000)
            r = 0
        elif word in ('roam_off', 'qtxpower'):
            r = d.iovar_setint(word, int(arg))
        elif word == 'assoc':
            cfg = m.call('wlc_bsscfg_primary', d.wlc)
            m.call('wlc_sta_assoc_upd', cfg, 1)
            r = 0
        elif word == 'unquiet':
            # as wlc_join_BSS: the channel is no longer quiet (a beacon was
            # heard), the transmitter is unmuted - without it the PHY's hold
            # word keeps the mute bit and the calibrations return at once
            cs = d.iovar_getint('chanspec')[1] & 0xffff
            m.call('wlc_clr_quiet_chanspec', m.r64(d.wlc + WLC_CMI), cs)
            m.call('wlc_mute', d.wlc, 0, 0)
            r = 0
        elif word == 'joincal':
            cfg = m.call('wlc_bsscfg_primary', d.wlc)
            m.call('wlc_full_phy_cal', d.wlc, cfg, 4)
            r = 0
        elif word == 'interf':
            m.call('wlc_phy_interference_set', self.pi(), int(arg) & 1)     # void (pi, init)
            r = 0
        elif word == 'rssi':
            r = self.rssi(int(arg))
        elif word == 'noise':
            pi = self.pi()
            m.call('wlc_phy_noise_sample_intr', pi)
            r = m.call('wlc_phy_noise_avg', pi) & 0xff
            # the glue's wireless statistics ask the same (WLC_GET_PHY_NOISE)
            rc, data = d.ioctl(WLC_GET_PHY_NOISE, bytes(4))
            r = (r, rc, data)
        else:
            return super().step(what)
        self.results.append((what, r))
        return r

    def rssi(self, seed):
        """wlc_phy_rssi_compute (void; it stores the RSSI in the header) on 32
        receive headers: each header after the call."""
        m = self.m
        rng = random.Random(seed)
        pi = self.pi()
        buf = m.buf(0x40)
        out = []
        for i in range(32):
            h = bytearray(rng.randrange(256) for _ in range(0x30))
            if i % 4:                                       # mostly ordinary frames
                h[0x12] &= ~0x21 & 0xff
                h[0x13] &= ~0x01 & 0xff
            m.write(buf, bytes(h) + bytes(0x10))
            m.call('wlc_phy_rssi_compute', pi, buf)
            out.append(m.read(buf, 0x30))
        return out


def soak(on=('phy',), off=(), show=20, steps=STEPS, board_seed=0):
    core = hybrid.build(quiet=True)
    card = {'srom': board(board_seed)} if board_seed else {}
    ref = SoakRun(**card).session(steps, stop_on_error=False)
    got = SoakRun(core, off=off, on=on, **card).session(steps, stop_on_error=False)
    want, have = ref.logical_steps(), got.logical_steps()
    bad = 0
    if ref.error:
        print('the object itself stopped at step %s (a limit of the model)' % ref.error)
    if got.error and got.error != ref.error:
        bad += 1
        print('the hybrid core stopped at step %s' % got.error)
    n = min(len(ref.results), len(got.results), len(want), len(have))
    shown = 0
    for i in range(n):
        w, a = ref.results[i]
        b = got.results[i][1]
        diff_res = a != b
        diff_acc = want[i] != have[i]
        if not (diff_res or diff_acc):
            continue
        bad += 1
        line = 'step %3d %-12s' % (i, w)
        if diff_res and isinstance(a, list):
            k = sum(1 for x, y in zip(a, b) if x != y)
            first = next(j for j, (x, y) in enumerate(zip(a, b)) if x != y)
            pos = [j for j in range(min(len(a[first]), len(b[first]))) if a[first][j] != b[first][j]]
            line += ' results differ in %d of %d calls (first call %d, bytes %s)' % (
                k, len(a), first, ', '.join('+0x%02x %02x/%02x' % (j, a[first][j], b[first][j])
                                            for j in pos[:6]))
        elif diff_res:
            line += ' result: object %s, hybrid %s' % (a, b)
        if diff_acc:
            line += ' accesses: object %d, hybrid %d' % (len(want[i]), len(have[i]))
        print(line)
        if diff_acc and shown < show:
            shown += 1
            ab.compare(want[i], have[i], 4)
    total_w, total_h = sum(map(len, want)), sum(map(len, have))
    print('PHY soak (%d steps, board %s), switched on: %s, off: %s: %d accesses of the object, '
          '%d of the hybrid core, %s' % (len(steps), board_seed or 'synthetic',
                                         ', '.join(on) or '-', ', '.join(off) or '-',
                                         total_w, total_h, 'the same' if not bad else
                                         'DIFFERENT in %d steps' % bad))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--on', default='phy', help='layers to switch on (default: phy)')
    ap.add_argument('--off', default='', help='layers to switch off')
    ap.add_argument('--show', type=int, default=20)
    ap.add_argument('--steps', default='', help='comma-separated steps instead of the soak')
    ap.add_argument('--board', type=int, default=0,
                    help='a board variant (seed; 0: the synthetic card)')
    ns = ap.parse_args()
    on = tuple(x for x in ns.on.split(',') if x)
    off = tuple(x for x in ns.off.split(',') if x)
    steps = ns.steps.split(',') if ns.steps else STEPS
    try:
        return 1 if soak(on, off, ns.show, steps, ns.board) else 0
    except EmuError as e:
        print('emulation error: %s' % e)
        return 1


if __name__ == '__main__':
    sys.exit(main())
