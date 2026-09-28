#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Recordings of the real card (Linux mmiotrace, see capture-on-target.sh) and
their replay in the emulator.

Replay: the object runs in the emulator as usual, but every value it reads
from the card is the value the real card returned at the same point of the
recording.  The object is deterministic, so as long as the emulated session
does what the recorded one did, access follows access in the same order; the
first access that differs ends the replay (a "divergence").  What comes out:

* the identity of the real card (chip, cores, PHY, radio, SROM, OTP),
* a register trace like the emulator's, with real values and with the
  function names mmiotrace cannot give,
* the list of registers where the model answers differently from the card.

    mmiotrace.py info CAPTURE_DIR        what the recording contains
    mmiotrace.py replay CAPTURE_DIR [--trace OUT] [--srom OUT.srom]
    mmiotrace.py selftest                record in the emulator, replay, compare
"""
import argparse
import os
import re
import struct
import sys

from emu import EmuError

# R width timestamp map_id physical value pc pid
ACCESS = re.compile(r'^([RW]) (\d) (\d+\.\d+) (\d+) (0x[0-9a-f]+) (0x[0-9a-f]+) ')
MAP = re.compile(r'^MAP (\d+\.\d+) (\d+) (0x[0-9a-f]+) (0x[0-9a-f]+) (0x[0-9a-f]+) ')
MARK = re.compile(r'^MARK (\d+\.\d+) (.*)')
CFG = re.compile(r'^([0-9a-f]{1,2}0): ((?:[0-9a-f]{2} ?){1,16})')


class Recording:
    """The accesses to one PCI device in a mmiotrace file."""

    def __init__(self, bars):
        self.bars = bars            # [(physical base, size)] for BAR0, BAR1
        self.events = []            # (op, bar, offset, size, value)
        self.marks = []             # (index into events, text)
        self.cfg = None             # configuration space at the start

    def bar_of(self, phys):
        for i, (base, size) in enumerate(self.bars):
            if base <= phys < base + size:
                return i, phys - base
        return None, 0

    @classmethod
    def load(cls, directory):
        cfg, bars = parse_pci(os.path.join(directory, 'pci.txt'))
        rec = cls(bars)
        rec.cfg = cfg
        with open(os.path.join(directory, 'mmiotrace.txt'), errors='replace') as f:
            for line in f:
                m = ACCESS.match(line)
                if m:
                    bar, off = rec.bar_of(int(m.group(5), 16))
                    if bar is not None:
                        rec.events.append((m.group(1), bar, off, int(m.group(2)),
                                           int(m.group(6), 16)))
                    continue
                m = MARK.match(line)
                if m:
                    rec.marks.append((len(rec.events), m.group(2).strip()))
        return rec

    def save(self, directory):
        """Write a recording in the format of the capture script (for tests)."""
        os.makedirs(directory, exist_ok=True)
        with open(os.path.join(directory, 'mmiotrace.txt'), 'w') as f:
            f.write('VERSION 20070824\n')
            for i, (base, size) in enumerate(self.bars):
                f.write('MAP 0.000000 %d 0x%x 0xffffc90000000000 0x%x 0x0 0\n'
                        % (i + 1, base, size))
            marks = dict(self.marks)
            for k, (op, bar, off, size, value) in enumerate(self.events):
                if k in marks:
                    f.write('MARK 0.000000 %s\n' % marks[k])
                f.write('%s %d 0.%06d %d 0x%x 0x%x 0x0 0\n'
                        % (op, size, k, bar + 1, self.bars[bar][0] + off, value))
        with open(os.path.join(directory, 'pci.txt'), 'w') as f:
            f.write('--- configuration space\n')
            for i in range(0, len(self.cfg), 16):
                f.write('%02x: %s\n' % (i, ' '.join('%02x' % b for b in self.cfg[i:i + 16])))
            f.write('--- resources\n')
            for base, size in (self.bars[0], (0, 0), self.bars[1]):
                f.write('0x%016x 0x%016x 0x%016x\n' % (base, base + size - 1 if size else 0,
                                                     0x140204 if size else 0))


def parse_pci(path):
    """Configuration space bytes and the BARs from the capture's pci.txt."""
    cfg = bytearray(4096)
    bars = []
    section = None
    with open(path, errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('--- '):
                section = line[4:]
                continue
            if section == 'configuration space':
                m = CFG.match(line)
                if m:
                    off = int(m.group(1), 16)
                    data = bytes(int(x, 16) for x in m.group(2).split())
                    cfg[off:off + len(data)] = data
            elif section == 'resources':
                p = line.split()
                if len(p) == 3:
                    start, end = int(p[0], 16), int(p[1], 16)
                    if end > start:
                        bars.append((start, end - start + 1))
    if len(bars) < 2:
        raise ValueError('%s: the memory resources of the device are missing' % path)
    return cfg, bars[:2]


class Divergence(EmuError):
    pass


class Replay:
    """Feeds a recording to the chip model, see chip.Chip.mmio_read()."""

    def __init__(self, rec):
        self.rec = rec
        self.pos = 0
        self.mismatch = {}          # (space, address) -> (model value, real value, count)
        self.done = False

    def _next(self, chip, op, bar, off, size, value):
        ev = self.rec.events
        if self.pos >= len(ev):
            self.done = True
            raise Divergence('end of the recording after %d accesses' % self.pos)
        e = ev[self.pos]
        if e[:4] != (op, bar, off, size) or (op == 'W' and e[4] != value):
            raise Divergence(
                'divergence at access %d of the recording:\n'
                '  recorded: %s BAR%d+%#x/%d = %#x\n'
                '  emulated: %s BAR%d+%#x/%d%s\n  in %s\n%s'
                % (self.pos, e[0], e[1], e[2], e[3], e[4], op, bar, off, size,
                   ' = %#x' % value if op == 'W' else '', chip.m.caller(), chip.m.backtrace()))
        self.pos += 1
        return e[4]

    def read(self, chip, bar, off, size, model):
        real = self._next(chip, 'R', bar, off, size, None)
        if real != model and chip.trace:
            a = chip.trace[-1]
            k = (a.space, a.addr)
            old = self.mismatch.get(k)
            self.mismatch[k] = (model, real, (old[2] if old else 0) + 1)
        return real

    def write(self, chip, bar, off, size, value):
        self._next(chip, 'W', bar, off, size, value)


def run(directory, trace=None, srom_out=None, quiet=False):
    """Replay attach and up; returns (driver, replay, error or None)."""
    from bcm4360 import Bcm4360
    from chip import cores_from_erom
    from run_attach import Driver

    rec = Recording.load(directory)

    def session(layout, permissive):
        d = Driver(None, echo=not quiet, layout=layout)
        chip = d.chip
        chip.permissive = permissive
        chip.cfg[:len(rec.cfg)] = rec.cfg
        chip.vendor, chip.device = struct.unpack_from('<HH', rec.cfg, 0)
        chip.replay = Replay(rec)
        err = None
        try:
            d.attach()
            if d.wlc:
                d.up()
        except EmuError as e:
            err = e
        return d, err

    # first pass: up to the end of the enumeration, to learn the cores.  The
    # enumeration ROM is what the parser's read function (get_erom_ent,
    # sub_00133f) reads; an entry that did not match is read a second time.
    d, err = session(None, True)
    seen = {}
    for a in d.chip.trace:
        if a.op == 'R' and a.size == 4 and d.chip.func_name(a.pc) == 'sub_00133f':
            seen.setdefault(a.addr, a.value)
    words = [seen[k] for k in sorted(seen)]
    layout = cores_from_erom(words)
    if not layout:
        return d, d.chip.replay, err or EmuError('no enumeration ROM in the recording')
    d, err = session(layout, True)
    chip = d.chip
    if trace:
        chip.dump_trace(trace)
    if srom_out:
        img = {}
        for a in chip.trace:
            if a.space == 'chipcommon' and a.op == 'R' and a.size == 2 and 0x800 <= a.addr < 0xa00:
                img[(a.addr - 0x800) // 2] = a.value
        if img:
            n = max(img) + 1
            with open(srom_out, 'wb') as f:
                f.write(struct.pack('<%dH' % n, *[img.get(i, 0xffff) for i in range(n)]))
    return d, chip.replay, err


def facts(d, rp):
    """What the replay tells about the card."""
    chip, m = d.chip, d.m
    out = []
    cc = chip.core('chipcommon')
    rd = {}
    for a in chip.trace:
        if a.op == 'R':
            rd.setdefault((a.space, a.addr), a.value)
    cid = rd.get(('chipcommon', 0))
    if cid is not None:
        out.append('chip id %#x, revision %d, package %d' % (cid & 0xffff, cid >> 16 & 0xf,
                                                            cid >> 20 & 0xf))
    for c in chip.cores:
        out.append('core %#05x revision %2d at %#x, wrapper %#x' % (c.coreid, c.rev, c.base,
                                                                    c.wrap))
    for name, key in (('ChipCommon capabilities', ('chipcommon', 4)),
                      ('ChipCommon extended capabilities', ('chipcommon', 0xac)),
                      ('chip status', ('chipcommon', 0x2c)),
                      ('PMU capabilities', ('chipcommon', 0x604)),
                      ('OTP layout', ('chipcommon', 0x1c)),
                      ('OTP status', ('chipcommon', 0x10)),
                      ('SROM control', ('chipcommon', 0x190)),
                      ('PHY version register', ('d11', 0x3e0)),
                      ('radio id register 0', ('radio', 0)),
                      ('radio id register 1', ('radio', 1)),
                      ('MAC capabilities', ('d11', 0x15c))):
        if key in rd:
            out.append('%s: %#x' % (name, rd[key]))
    return out


def selftest():
    """Record a session of the model, replay it, compare."""
    import tempfile
    from run_attach import Driver
    d = Driver(echo=False)
    chip = d.chip
    chip.raw = []
    cfg = bytes(chip.cfg[:256])
    d.attach()
    d.up()
    rec = Recording([(0xb0600000, 0x8000), (0xb0400000, 0x200000)])
    rec.events = list(chip.raw)
    rec.cfg = cfg
    reference = [(a.op, a.space, a.addr, a.size, a.value) for a in chip.trace]
    with tempfile.TemporaryDirectory() as tmp:
        rec.save(tmp)
        d2, rp, err = run(tmp, quiet=True)
    got = [(a.op, a.space, a.addr, a.size, a.value) for a in d2.chip.trace]
    ok = True
    if err is not None and not rp.done:
        print('replay failed: %s' % err)
        ok = False
    print('%d accesses recorded, %d replayed' % (len(rec.events), rp.pos))
    if rp.pos != len(rec.events):
        ok = False
    same = sum(1 for a, b in zip(reference, got) if a == b)
    print('%d of %d trace records identical' % (same, len(reference)))
    if got != reference:
        for i, (a, b) in enumerate(zip(reference, got)):
            if a != b:
                print('first difference at record %d: %s / %s' % (i, a, b))
                break
        ok = False
    print('selftest %s' % ('ok' if ok else 'FAILED'))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['info', 'replay', 'selftest'])
    ap.add_argument('dir', nargs='?')
    ap.add_argument('--trace')
    ap.add_argument('--srom')
    ns = ap.parse_args()
    if ns.cmd == 'selftest':
        return selftest()
    if not ns.dir:
        ap.error('CAPTURE_DIR is required')
    if ns.cmd == 'info':
        rec = Recording.load(ns.dir)
        print('BAR0 %#x (%#x bytes), BAR1 %#x (%#x bytes)' % (rec.bars[0] + rec.bars[1]))
        print('%d accesses, %d reads' % (len(rec.events),
                                         sum(1 for e in rec.events if e[0] == 'R')))
        for i, text in rec.marks:
            print('mark at access %d: %s' % (i, text))
        print('PCI id %04x:%04x, subsystem %04x:%04x, revision %d' % (
            struct.unpack_from('<HH', rec.cfg, 0) + struct.unpack_from('<HH', rec.cfg, 0x2c)
            + (rec.cfg[8],)))
        return 0
    d, rp, err = run(ns.dir, ns.trace, ns.srom)
    print('%d of %d recorded accesses replayed' % (rp.pos, len(rp.rec.events)))
    if err is not None:
        print(str(err))
    for line in facts(d, rp):
        print(line)
    if rp.mismatch:
        print('\nregisters where the model differs from the card (model / card, reads):')
        for (space, addr), (model, real, n) in sorted(rp.mismatch.items()):
            print('  %-12s %05x  %08x / %08x  %d' % (space, addr, model, real, n))
    return 0 if err is None or rp.done else 1


if __name__ == '__main__':
    sys.exit(main())
