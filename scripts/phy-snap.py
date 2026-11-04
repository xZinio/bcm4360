#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
The card's PHY registers, radio registers, PHY tables and shared memory, read
through the module's /proc/bcm4360_snap<unit> (hybrid/snap.c): the hardware
state both PHYs share, to compare Broadcom's PHY and the open one on the card
in the same situation (docs/re/03-tools.md).

    phy-snap.py list CAPTURE_DIR... > LIST
        what to read: every PHY and radio register and PHY table entry the
        drivers touched in the mmiotrace captures of card-test.sh (BAR0 of
        16 KB; PHY through D11 0x3fc/0x3fe, radio 0x3d8/0x3da, tables through
        PHY 0x0d..0x11), and the shared memory up to the highest word they
        touched
    phy-snap.py take [--list LIST] [--proc FILE] -o OUT.json      (root)
        one snapshot; the list defaults to phy-snap.list next to this script
    phy-snap.py diff A.json B.json [--same A2.json [--same B2.json]] [--all]
        what differs between A and B; a value that differs between A and A2
        (the first --same) or B and B2 (the second), snapshots of the same
        configuration moments apart, moves by itself and is left out (listed
        with --all)

A LIST is the request of hybrid/snap.c, one item per line:
"phy ADDR COUNT", "radio ADDR COUNT", "tbl ID OFFSET COUNT WIDTH",
"shm OFFSET COUNT". The module reads it with the driver's lock held, the
microcode kept awake, and (for tables) the MAC suspended; nothing is written
to the PHY but the table pointer.
"""
import argparse
import glob
import json
import os
import re
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LIST = os.path.join(HERE, 'phy-snap.list')

# D11 registers of the indirect accesses (docs/re/spec/access.md)
D11_OBJ_ADDR, D11_OBJ_LO, D11_OBJ_HI = 0x160, 0x164, 0x166
D11_RADIO_ADDR, D11_RADIO_DATA = 0x3d8, 0x3da
D11_PHY_ADDR, D11_PHY_DATA = 0x3fc, 0x3fe
# PHY registers of the table port
T_ID, T_OFF, T_LO, T_HI, T_WIDE = 0x0d, 0x0e, 0x0f, 0x10, 0x11
OBJ_SHM = 1
MAX_COUNT = 4096
SHM_ROUND = 0x100


def ranges(keys, gap=0):
    """Sorted runs [start, end) of the integers in keys; runs closer than gap
    are joined (the holes are read too)."""
    out = []
    for k in sorted(keys):
        if out and k <= out[-1][1] + gap:
            out[-1][1] = max(out[-1][1], k + 1)
        else:
            out.append([k, k + 1])
    return out


def split(start, end):
    while start < end:
        n = min(MAX_COUNT, end - start)
        yield start, n
        start += n


class Decoder:
    """The PHY, radio, table and shared-memory accesses of one capture."""

    def __init__(self):
        self.phy = set()
        self.radio = set()
        self.tables = {}        # id -> {entry: words per entry}
        self.shm_max = 0
        self.phy_addr = None
        self.radio_addr = None
        self.tbl_id = 0
        self.tbl_off = 0
        self.wide_words = 0
        self.obj = None

    def phy_access(self, rw, addr, value=None, set_addr=True):
        if addr is None:
            return
        if addr in (T_ID, T_OFF) and rw == 'W':
            if addr == T_ID:
                self.tbl_id = value
            else:
                self.tbl_off = value
            self.wide_words = 0
            return
        if addr == T_LO:
            # one entry per access of the low half (8, 16 and 32 bit); a 32-bit
            # write puts the high half first, a read reads it after
            ent = self.tables.setdefault(self.tbl_id, {})
            ent[self.tbl_off] = max(ent.get(self.tbl_off, 1), 1)
            self.tbl_off += 1
            return
        if addr == T_HI:
            # the entry the low half goes (write) or went (read) to
            ent = self.tables.setdefault(self.tbl_id, {})
            e = self.tbl_off if rw == 'W' else self.tbl_off - 1
            ent[e] = max(ent.get(e, 1), 2)
            return
        if addr == T_WIDE:
            # the first word of a wide entry goes through the register (an
            # address write), the others straight to the data register
            if set_addr:
                if self.wide_words:
                    self.tbl_off += 1
                self.wide_words = 0
            self.wide_words += 1
            ent = self.tables.setdefault(self.tbl_id, {})
            ent[self.tbl_off] = max(ent.get(self.tbl_off, 1), 3 if self.wide_words <= 3 else 4)
            return
        if self.wide_words:
            self.tbl_off += 1
            self.wide_words = 0
        self.phy.add(addr)

    def access(self, rw, size, off, value):
        if off == D11_PHY_ADDR and rw == 'W':
            if size == 4:
                self.phy_addr = value & 0xffff
                self.phy_access('W', self.phy_addr, value >> 16)
            else:
                self.phy_addr = value & 0xffff
                self.pending_addr = True
            return
        if off == D11_PHY_DATA:
            fresh = getattr(self, 'pending_addr', False)
            self.pending_addr = False
            self.phy_access(rw, self.phy_addr, value, set_addr=fresh)
            return
        if off == D11_RADIO_ADDR and rw == 'W':
            self.radio_addr = value & 0xffff
            return
        if off == D11_RADIO_DATA and self.radio_addr is not None:
            self.radio.add(self.radio_addr)
            return
        if off == D11_OBJ_ADDR and rw == 'W' and size == 4:
            self.obj = value
            return
        if off in (D11_OBJ_LO, D11_OBJ_HI) and self.obj is not None:
            if (self.obj >> 16) & 7 == OBJ_SHM:
                byte = (self.obj & 0xffff) * 4 + (2 if off == D11_OBJ_HI or size == 4 else 0)
                self.shm_max = max(self.shm_max, byte)

    def capture(self, path):
        bar0 = set()
        for line in open(path, errors='replace'):
            p = line.split()
            if not p:
                continue
            if p[0] == 'MAP' and len(p) > 5 and int(p[5], 16) == 0x4000:
                bar0.add(int(p[3], 16))
                continue
            if p[0] not in ('R', 'W') or len(p) < 6:
                continue
            a = int(p[4], 16)
            for b in bar0:
                if b <= a < b + 0x4000:
                    self.access(p[0], int(p[1]), a - b, int(p[5], 16))
                    break

    def request(self):
        lines = []
        phy = self.phy - {T_ID, T_OFF, T_LO, T_HI, T_WIDE}
        for s, e in ranges(phy):
            lines += ['phy 0x%x %d' % (a, n) for a, n in split(s, e)]
        for s, e in ranges(self.radio):
            lines += ['radio 0x%x %d' % (a, n) for a, n in split(s, e)]
        for tid in sorted(self.tables):
            ent = self.tables[tid]
            words = max(ent.values())
            width = {1: 16, 2: 32, 3: 48, 4: 64}[words]
            for s, e in ranges(ent, gap=8):
                lines += ['tbl 0x%x 0x%x %d %d' % (tid, a, n, width) for a, n in split(s, e)]
        top = (self.shm_max + 2 + SHM_ROUND - 1) // SHM_ROUND * SHM_ROUND
        lines += ['shm 0x%x %d' % (a * 2, n) for a, n in split(0, top // 2)]
        return lines


def cmd_list(args):
    d = Decoder()
    for c in args.captures:
        path = os.path.join(c, 'mmiotrace.txt') if os.path.isdir(c) else c
        d.capture(path)
    lines = d.request()
    words = 0
    for line in lines:
        words += item_words(parse_item(line))
    print('# phy-snap.py list of %d captures: %d PHY registers, %d radio registers, %d tables, '
          'shared memory to 0x%x; %d words' % (len(args.captures), len(d.phy), len(d.radio),
                                                len(d.tables), d.shm_max, words))
    print('\n'.join(lines))


def parse_item(line):
    p = line.split()
    nums = [int(x, 0) for x in p[1:]]
    if p[0] == 'tbl':
        return ('tbl', nums[0], nums[1], nums[2], nums[3])
    return (p[0], None, nums[0], nums[1], None)


def item_words(it):
    space, _, _, count, width = it
    if space != 'tbl':
        return count
    return count * {8: 1, 16: 1, 32: 2, 48: 3, 60: 4, 64: 4}[width]


def read_list(path):
    items = []
    for line in open(path):
        line = line.strip()
        if line and not line.startswith('#'):
            items.append(parse_item(line))
    return items


def decode(items, data):
    """{'phy': {addr: v}, 'radio': {...}, 'shm': {byte: v}, 'tbl': {'id': {entry: [words]}}}"""
    words = struct.unpack('<%dH' % (len(data) // 2), data)
    out = {'phy': {}, 'radio': {}, 'shm': {}, 'tbl': {}}
    i = 0
    for space, tid, start, count, width in items:
        if space == 'tbl':
            per = item_words((space, tid, start, 1, width))
            t = out['tbl'].setdefault('0x%x' % tid, {})
            for k in range(count):
                t['0x%x' % (start + k)] = list(words[i:i + per])
                i += per
        elif space == 'shm':
            for k in range(count):
                out['shm']['0x%x' % (start + 2 * k)] = words[i]
                i += 1
        else:
            for k in range(count):
                out[space]['0x%x' % (start + k)] = words[i]
                i += 1
    if i != len(words):
        raise SystemExit('the module returned %d words, the list asks for %d' % (len(words), i))
    return out


def module_state():
    st = {}
    for name in ('srcversion', 'version'):
        try:
            st[name] = open('/sys/module/bcm4360/' + name).read().strip()
        except OSError:
            pass
    params = {}
    for p in glob.glob('/sys/module/bcm4360/parameters/*'):
        try:
            params[os.path.basename(p)] = open(p).read().strip()
        except OSError:
            pass
    st['parameters'] = params
    return st


def cmd_take(args):
    proc = args.proc or (sorted(glob.glob('/proc/bcm4360_snap*')) or [None])[0]
    if not proc:
        raise SystemExit('no /proc/bcm4360_snap*: the module has no snapshot (HYBRID build '
                         'with hybrid/snap.c needed)')
    text = ''.join(line for line in open(args.list) if not line.startswith('#'))
    items = read_list(args.list)
    req = text.encode()
    t0 = time.time()
    fd = os.open(proc, os.O_RDWR)
    try:
        n = os.write(fd, req)
        if n != len(req):
            raise SystemExit('short write to %s: %d of %d' % (proc, n, len(req)))
        os.lseek(fd, 0, os.SEEK_SET)
        chunks = []
        while True:
            b = os.read(fd, 1 << 16)
            if not b:
                break
            chunks.append(b)
    except OSError as e:
        raise SystemExit('%s: %s' % (proc, e.strerror))
    finally:
        os.close(fd)
    snap = decode(items, b''.join(chunks))
    snap['meta'] = {'time': time.strftime('%Y-%m-%d %H:%M:%S'), 'seconds': round(time.time() - t0, 3),
                    'proc': proc, 'list': os.path.abspath(args.list), 'module': module_state(),
                    'label': args.label}
    with open(args.out, 'w') as f:
        json.dump(snap, f, indent=0, sort_keys=True)
    total = sum(len(snap[s]) for s in ('phy', 'radio', 'shm')) + \
        sum(len(t) for t in snap['tbl'].values())
    print('%s: %d values in %.2f s' % (args.out, total, snap['meta']['seconds']))


def flat(snap):
    out = {}
    for s in ('phy', 'radio', 'shm'):
        for k, v in snap.get(s, {}).items():
            out[(s, int(k, 16))] = v
    for tid, t in snap.get('tbl', {}).items():
        for k, v in t.items():
            out[('tbl.' + tid[2:].rjust(2, '0'), int(k, 16))] = tuple(v)
    return out


def fmt(v):
    if isinstance(v, tuple):
        return ':'.join('%04x' % w for w in reversed(v))
    return '%04x' % v


def cmd_diff(args):
    a, b = flat(json.load(open(args.a))), flat(json.load(open(args.b)))
    moving = set()
    for ref, same in zip((a, b), args.same or []):
        s = flat(json.load(open(same)))
        moving |= {k for k in s if k in ref and s[k] != ref[k]}
    keys = sorted(set(a) & set(b))
    diff = [k for k in keys if a[k] != b[k]]
    shown = [k for k in diff if k not in moving]
    by = {}
    for k in shown:
        by.setdefault(k[0], []).append(k)
    print('%s vs %s: %d values compared, %d differ; left out as moving by themselves '
          '(--same): %d; left: %d' % (args.a, args.b, len(keys), len(diff), len(diff) - len(shown),
                                       len(shown)))
    for space in sorted(by):
        print('== %s: %d' % (space, len(by[space])))
        for k in by[space]:
            print('  %-7s 0x%04x  %s -> %s' % (space, k[1], fmt(a[k]), fmt(b[k])))
    if args.all and moving:
        print('== moving by themselves: %d' % len(moving))
        for k in sorted(moving):
            print('  %-7s 0x%04x  %s / %s' % (k[0], k[1], fmt(a.get(k, -1)), fmt(b.get(k, -1))))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('list')
    p.add_argument('captures', nargs='+')
    p = sub.add_parser('take')
    p.add_argument('--list', default=DEFAULT_LIST)
    p.add_argument('--proc')
    p.add_argument('--label', default='')
    p.add_argument('-o', '--out', required=True)
    p = sub.add_parser('diff')
    p.add_argument('a')
    p.add_argument('b')
    p.add_argument('--same', action='append')
    p.add_argument('--all', action='store_true')
    args = ap.parse_args()
    {'list': cmd_list, 'take': cmd_take, 'diff': cmd_diff}[args.cmd](args)


if __name__ == '__main__':
    main()
