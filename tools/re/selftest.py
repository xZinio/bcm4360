#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Checks that the tool chain works on this machine and still agrees with the
object: run it after changing a tool.

    selftest.py
"""
import hashlib
import sys
import time

from emu import Machine, s32
from run_attach import DEFAULT_SROM, Driver, load_srom
from run_chan import chanspec
from srom import Layout

EXPECTED_SHA256 = '352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743'

results = []


def check(name, ok, detail=''):
    results.append(ok)
    print('%-52s %s %s' % (name, 'ok' if ok else 'FAILED', detail))


def check_tables(d, trace):
    """The rules for table transfers (docs/re/spec/access.md) against the object.

    A table write is recorded twice: as entries taken from the transfer record
    when wlc_phy_write_table_ext is entered, and as the PHY register writes the
    function then makes.  Both must tell the same story.
    """
    chip = d.chip
    inside = lambda a: 'wlc_phy_write_table_ext' in (
        [chip.func_name(a.pc)] + [chip.func_name(x) for x in a.bt[:2]])
    i, n = 0, len(trace)
    transfers = entries = 0
    bad = None
    while i < n and bad is None:
        a = trace[i]
        if not (a.space.startswith('tbl.') and a.op == 'W'):
            i += 1
            continue
        tid = int(a.space[4:], 16)
        group = []
        while i < n and trace[i].space == a.space and trace[i].op == 'W' and (
                not group or trace[i].addr == group[-1].addr + 1):
            group.append(trace[i])
            i += 1
        want = [(0x0d, tid), (0x0e, group[0].addr)]
        for e in group:
            v, size = e.value, e.size
            if size in (1, 2):
                want.append((0x0f, v))
            elif size == 4:
                want += [(0x10, v >> 16), (0x0f, v & 0xffff)]
            elif size == 6:
                want += [(0x11, v >> (16 * k) & 0xffff) for k in range(3)]
            else:
                want += [(0x11, v >> (16 * k) & 0xffff) for k in range(4)]
        got = []
        while i < n and len(got) < len(want):
            b = trace[i]
            if b.space == 'phy' and b.op == 'W' and inside(b):
                got.append((b.addr, b.value))
            elif b.space.startswith('tbl.'):
                break
            i += 1
        transfers += 1
        entries += len(group)
        if got != want:
            k = next((k for k, (x, y) in enumerate(zip(want, got)) if x != y), len(got))
            bad = 'table %#x offset %d: write %d is %s, expected %s' % (
                tid, group[0].addr, k, got[k] if k < len(got) else None,
                want[k] if k < len(want) else None)
    check('table transfers follow the specified register protocol', bad is None,
          bad or '%d transfers, %d entries' % (transfers, entries))


def main():
    t0 = time.time()
    m = Machine()
    with open(m.blob.path, 'rb') as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    check('object is Broadcom 6.30.223.271', digest == EXPECTED_SHA256, digest[:16])
    m.blob.discover_functions()
    named = sum(1 for f in m.blob.funcs if f.kind == 'F')
    check('function discovery (3922 functions, 2722 named)',
          (len(m.blob.funcs), named) == (3922, 2722), '%d/%d' % (len(m.blob.funcs), named))

    # pure functions of the object, called in the emulator
    check('bcm_strtoul("0x1234")', m.call('bcm_strtoul', m.cstr('0x1234'), 0, 0) == 0x1234)
    check('wf_channel2mhz(36, 10000) = 5180', m.call('wf_channel2mhz', 36, 10000) == 5180)
    check('hndcrc32 check value', m.call('hndcrc32', m.cstr('123456789'), 9, 0xffffffff)
          & 0xffffffff == 0xffffffff ^ 0xcbf43926)
    check('wf_chspec_aton("36/80") = 0xe02a', chanspec(m, '36/80') == 0xe02a)

    # SROM: our encoder and decoder against the object's parser
    d = Driver(echo=False)
    d.chip.trace_on = False
    m = d.m
    words = load_srom(DEFAULT_SROM, m.blob)
    raw = b''.join(w.to_bytes(2, 'little') for w in words)
    check('SROM image passes the object\'s CRC (hndcrc8 = 0x9f)',
          m.call('hndcrc8', m.buf(raw), len(raw), 0xff) & 0xff == 0x9f)
    wlc = d.attach()
    check('wlc_attach() on the modelled BCM4360', bool(wlc) and d.err == 0, 'err %d' % d.err)
    if not wlc:
        return 1
    pub = m.r64(wlc)
    n = m.r32(pub + 0x110)
    theirs = [s.decode('latin1') for s in m.read(m.r64(pub + 0x108), n).split(b'\0') if s]
    ours = ['%s=%s' % kv for kv in Layout(m.blob).decode(words)]
    check('SROM variables: srom.py equals the object (%d variables)' % len(theirs),
          theirs == ours)

    r, rev = d.ioctl(98, size=68)
    f = [int.from_bytes(rev[i:i + 4], 'little') for i in range(0, 68, 4)]
    check('WLC_GET_REVINFO: chip 0x4360, core rev 42, AC-PHY',
          r == 0 and f[11] == 0x4360 and f[4] == 42 and f[12] == 11,
          'chip %#x corerev %d phy %d rev %d' % (f[11], f[4], f[12], f[13]))

    d.iovar_setint('mpc', 0)
    d.chip.trace_on = True
    n0 = len(d.chip.trace)
    check('wlc_up()', d.up() == 0)
    trace = d.chip.trace[n0:]
    ucode = sum(1 for a in trace if a.space == 'ucode' and a.op == 'W')
    check('microcode download (10850 words of d11ucode42)', ucode == 10850, str(ucode))
    phyw = sum(1 for a in trace if a.space == 'phy' and a.op == 'W')
    tblw = sum(1 for a in trace if a.space.startswith('tbl.') and a.op == 'W')
    radw = sum(1 for a in trace if a.space == 'radio' and a.op == 'W')
    check('PHY init wrote registers, tables and the radio',
          phyw > 1000 and tblw > 1000 and radw > 100, 'phy %d, tables %d, radio %d'
          % (phyw, tblw, radw))
    check_tables(d, trace)
    for text in ('36/80', '6'):
        r = d.iovar_setint('chanspec', chanspec(m, text))
        rc, cur = d.iovar_getint('chanspec')
        check('channel %s' % text, r == 0 and cur & 0xffff == chanspec(m, text))
    fired = m.os.run_timers(m, m.os.now_us // 1000 + 2500)
    check('two watchdog ticks', fired == 2, str(fired))
    check('wlc_down()', s32(m.call('wlc_down', d.wlc)) >= 0)

    bad = results.count(False)
    print('\n%d checks, %d failed, %.0f s' % (len(results), bad, time.time() - t0))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
