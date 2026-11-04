#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
The hardware snapshot of hybrid/snap.c (the module's /proc/bcm4360_snap<unit>,
scripts/phy-snap.py) in the emulator, on the hybrid core:

    hybrid.py snap [--on phy] [--off LAYERS]

1. Requests: a malformed one is refused, the list scripts/phy-snap.list is
   parsed into the items phy-snap.py makes of it.
2. Before the interface is up every item is refused (the core has no clock),
   without a single access to the card.
3. Up on channel 11, after the watchdog: every value read equals the card
   model's state (PHY and radio registers, table entries by the table's width,
   shared memory).
4. Its own accesses are reads, besides the address registers, the table
   pointer, the MAC control (wake override, suspend) and the acknowledgement of
   the suspend.
5. It changes nothing: the session going on after it (36/80, watchdog, 6,
   watchdog, down) makes the same accesses as without it.
6. With Broadcom's PHY and with the open PHY the snapshot is the same (the
   hardware state of both sessions is).
"""
import os
import struct
import sys

import hybrid
from emu import s32

HERE = os.path.dirname(os.path.abspath(__file__))
LIST = os.path.join(hybrid.ROOT, 'scripts', 'phy-snap.list')
sys.path.insert(0, os.path.join(hybrid.ROOT, 'scripts'))
import importlib.util
_spec = importlib.util.spec_from_file_location('phy_snap', os.path.join(hybrid.ROOT, 'scripts',
                                                                        'phy-snap.py'))
phy_snap = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(phy_snap)

SPACES = {'phy': 1, 'radio': 2, 'tbl': 3, 'shm': 4}
EINVAL, EAGAIN = 1, 2
PRE = ['attach', 'mpc 0', 'up', '11', 'watchdog']
POST = ['36/80', 'watchdog', '6', 'watchdog', 'down']
# what the snapshot may write (d11 offsets; the PHY's table pointer)
D11_WRITES = {0x120: 'maccontrol', 0x128: 'macintstatus', 0x160: 'objaddr', 0x3d8: 'radio address',
              0x3fc: 'PHY address'}
PHY_WRITES = {0x0d, 0x0e}


def items_of(path):
    return phy_snap.read_list(path)


def pack(it):
    space, tid, start, count, width = it
    return struct.pack('<BBHII', SPACES[space], width or 0, tid or 0, start, count)


class Snap:
    def __init__(self, run):
        self.run = run
        self.m = run.m
        self.wlc = run.d.wlc

    def parse(self, text):
        m = self.m
        b = text.encode()
        req = m.malloc(len(b) + 1, fill=0)
        m.write(req, b)
        items = m.malloc(12 * 4096)
        return s32(m.call('hyb_snap_parse', req, len(b), items, 4096))

    def read(self, items):
        """The words of every item, or (item, error)."""
        m = self.m
        it_buf = m.malloc(12)
        total = sum(phy_snap.item_words(it) for it in items)
        out = m.malloc(2 * total + 2)
        words = []
        for it in items:
            m.write(it_buf, pack(it))
            n = phy_snap.item_words(it)
            r = s32(m.call('hyb_snap_read', self.wlc, it_buf, out, n))
            if r < 0:
                return it, r
            if r != n:
                return it, 'returned %d words, %d expected' % (r, n)
            words += struct.unpack('<%dH' % n, m.read(out, 2 * n))
        return words


def expected(chip, items):
    """The words the card model holds for the items."""
    phy, d11 = chip.phy, chip.core('d11')
    out = []
    for space, tid, start, count, width in items:
        for k in range(count):
            if space == 'phy':
                out.append(chip.phy_read(start + k, phy.regs.get(start + k, 0)) & 0xffff)
            elif space == 'radio':
                out.append(chip.radio_read(start + k, phy.radio.get(start + k, 0)) & 0xffff)
            elif space == 'shm':
                off = start + 2 * k
                out.append(chip.obj_read('shm', off, 2, d11.objmem.get((1, off), 0)) & 0xffff)
            else:
                e = phy.tables.get(tid, {}).get(start + k, 0)
                mw = phy.table_width(tid)
                per = phy_snap.item_words((space, tid, start, 1, width))
                if per == 1:
                    out.append(e & (0xff if mw == 8 else 0xffff))
                else:
                    out += [(e >> (16 * w)) & 0xffff for w in range(per)]
    return out


def label(items, i):
    for it in items:
        n = phy_snap.item_words(it)
        if i < n:
            space, tid, start, count, width = it
            if space == 'tbl':
                per = n // count
                return 'tbl.%02x[0x%x] word %d' % (tid, start + i // per, i % per)
            return '%s 0x%x' % (space, start + (2 if space == 'shm' else 1) * i)
        i -= n
    return '?'


def snap_check(off=(), show=20, on=()):
    core = hybrid.build(quiet=True)
    items = items_of(LIST)
    bad = 0

    # 1, 2: requests; before up
    run = hybrid.Run(core, off=off, on=on)
    run.session(['attach', 'mpc 0'])
    s = Snap(run)
    for text, want in (('phy 0x10\n', -EINVAL), ('foo 1 2\n', -EINVAL), ('phy 0x1fff 2\n', -EINVAL),
                       ('tbl 0x0b 0 4 12\n', -EINVAL), ('shm 1 1\n', -EINVAL),
                       ('# a comment\n\nphy 0x10 4\n  radio 0x40b 1  \n', 2)):
        got = s.parse(text)
        if got != want:
            bad += 1
            print('parse %r: %d, expected %d' % (text, got, want))
    text = ''.join(line for line in open(LIST) if not line.startswith('#'))
    n = s.parse(text)
    if n != len(items):
        bad += 1
        print('the list parses into %d items, phy-snap.py makes %d' % (n, len(items)))
    t0 = len(run.s.chip.trace)
    r = s.read(items[:1])
    if r != (items[0], -EAGAIN) or len(run.s.chip.trace) != t0:
        bad += 1
        print('before up: %s, %d accesses (expected -%d, none)'
              % (r, len(run.s.chip.trace) - t0, EAGAIN))

    # 3, 4: up on channel 11
    runs = {}
    for name, sw_on in (('default', tuple(on)), ('open phy', tuple(set(on) | {'phy'}))):
        if name == 'open phy' and 'phy' in on:
            continue
        run = hybrid.Run(core, off=off, on=sw_on).session(PRE)
        s = Snap(run)
        t0 = len(run.s.chip.trace)
        words = s.read(items)
        own = run.s.chip.trace[t0:]
        if isinstance(words, tuple):
            bad += 1
            print('%s: item %s refused: %s' % (name, words[0], words[1]))
            continue
        want = expected(run.s.chip, items)
        diffs = [i for i, (a, b) in enumerate(zip(words, want)) if a != b]
        if diffs:
            bad += 1
            print('%s: %d of %d words differ from the model:' % (name, len(diffs), len(words)))
            for i in diffs[:show]:
                print('  %-24s snapshot %04x, model %04x' % (label(items, i), words[i], want[i]))
        writes = [a for a in own if a.op == 'W' and not (
            (a.space == 'd11' and a.addr in D11_WRITES) or
            (a.space == 'phy' and a.addr in PHY_WRITES))]
        if writes:
            bad += 1
            print('%s: %d writes besides address registers and MAC control, first: %s'
                  % (name, len(writes), writes[:3]))
        reads = sum(1 for a in own if a.op == 'R')
        print('%s: %d items, %d words, the same as the model: %s; %d accesses of its own '
              '(%d reads), writes only to %s' % (
                  name, len(items), len(words), 'yes' if not diffs else 'NO', len(own), reads,
                  ', '.join(sorted({D11_WRITES.get(a.addr, 'PHY table pointer')
                                    for a in own if a.op == 'W'})) or '-'))
        runs[name] = (run, words, t0, len(own))

    # 5: nothing changes for the session going on
    for name, (run, words, t0, n_own) in runs.items():
        sw_on = tuple(on) if name == 'default' else tuple(set(on) | {'phy'})
        ref = hybrid.Run(core, off=off, on=sw_on).session(PRE)
        mark = len(ref.s.chip.trace)
        ref.session(POST)
        run.session(POST)
        names = run.s.chip.func_name
        after = hybrid.ab.logical(run.s.chip.trace[t0 + n_own:], names)
        want = hybrid.ab.logical(ref.s.chip.trace[mark:], ref.s.chip.func_name)
        if after != want or ref.results[len(PRE):] != run.results[len(PRE):]:
            bad += 1
            print('%s: the session after the snapshot is DIFFERENT:' % name)
            hybrid.ab.compare(want, after, show)
        else:
            print('%s: the session after the snapshot (%s): %d accesses, the same as without it'
                  % (name, ', '.join(POST), len(after)))

    # 6: Broadcom's PHY and the open PHY
    if len(runs) == 2:
        a, b = runs['default'][1], runs['open phy'][1]
        d = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
        if d:
            bad += 1
            print('Broadcom\'s PHY and the open PHY: %d words differ' % len(d))
            for i in d[:show]:
                print('  %-24s %04x / %04x' % (label(items, i), a[i], b[i]))
        else:
            print('Broadcom\'s PHY and the open PHY: the same snapshot (%d words)' % len(a))
    print('snap check: %s' % ('pass' if not bad else '%d FAILED' % bad))
    return bad
