#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Reports on a register trace written by chip.Chip.dump_trace().

    trace.py summary FILE            accesses per register space and per function
    trace.py tree FILE [FROM [TO]]   call tree with the accesses of each function
    trace.py calls FILE [DEPTH]      call tree only, with the number of accesses
    trace.py flat FILE [FROM [TO]]   PHY/radio/table/shared memory level view

FROM and TO are sequence numbers.  In the tree, runs of accesses to
consecutive addresses (microcode, tables, templates) are folded into one line.
"""
import collections
import re
import sys

LINE = re.compile(r'\s*(\d+)\s+([\d.]+) ([RW]) (\S+)\s+([0-9a-f]+)/(\d) = ([0-9a-f]+)\s+'
                  r'(.*?)(?: \| (.*))?$')
FUNC = re.compile(r'(\S+?)(\+0x[0-9a-f]+)? \(')

# register spaces that are reached through other registers: the raw accesses
# to the address/data registers are left out of the higher level views
RAW_D11 = {0x160, 0x164, 0x166, 0x3d8, 0x3da, 0x3fc, 0x3fe}


class Rec:
    __slots__ = ('seq', 't', 'op', 'space', 'addr', 'size', 'val', 'fn', 'chain')


def parse(path, lo=0, hi=1 << 60):
    with open(path) as f:
        for line in f:
            m = LINE.match(line)
            if not m:
                continue
            seq = int(m.group(1))
            if seq < lo:
                continue
            if seq > hi:
                break
            r = Rec()
            r.seq, r.t, r.op, r.space = seq, float(m.group(2)), m.group(3), m.group(4)
            r.addr, r.size, r.val = int(m.group(5), 16), int(m.group(6)), int(m.group(7), 16)
            fm = FUNC.match(m.group(8))
            r.fn = fm.group(1) if fm else m.group(8)
            chain = m.group(9).split(' < ') if m.group(9) else []
            r.chain = [r.fn] + [c for c in chain if c]
            yield r


TABLE_FUNCS = {'wlc_phy_write_table_ext', 'wlc_phy_read_table_ext'}


def is_raw(r):
    if r.space == 'd11' and r.addr in RAW_D11:
        return True
    # register accesses that make up a table access, which is logged as such
    return r.space == 'phy' and len(r.chain) > 1 and (
        r.chain[0] in TABLE_FUNCS or r.chain[1] in TABLE_FUNCS)


def summary(path):
    spaces = collections.Counter()
    funcs = collections.Counter()
    n = 0
    for r in parse(path):
        spaces[(r.space, r.op)] += 1
        funcs[r.fn] += 1
        n += 1
    print('%d accesses' % n)
    print('\nby register space:')
    for s in sorted({s for s, _ in spaces}):
        print('  %-14s %7d reads %7d writes' % (s, spaces[(s, 'R')], spaces[(s, 'W')]))
    print('\nby function (top 60):')
    for fn, c in funcs.most_common(60):
        print('  %7d %s' % (c, fn))


def fold(recs):
    """Merge runs of accesses of one kind to consecutive addresses."""
    run = []

    def flush():
        if not run:
            return
        a = run[0]
        if len(run) < 4:
            for r in run:
                yield '%s %-11s %05x = %0*x' % (r.op, r.space, r.addr, 2 * r.size, r.val)
        else:
            yield '%s %-11s %05x..%05x  %d values: %s ...' % (
                a.op, a.space, a.addr, run[-1].addr, len(run),
                ' '.join('%x' % r.val for r in run[:6]))

    for r in recs:
        if run:
            p = run[-1]
            step = r.addr - p.addr
            if (r.op, r.space) == (p.op, p.space) and step in (0, 1, 2, 4) and (
                    len(run) == 1 or step == run[1].addr - run[0].addr):
                run.append(r)
                continue
            yield from flush()
            run = []
        run.append(r)
    yield from flush()


def tree(path, lo=0, hi=1 << 60, accesses=True, maxdepth=99):
    stack = []
    pending = []
    counts = []

    def emit(depth):
        if accesses and pending:
            for line in fold(pending):
                print('%s    %s' % ('  ' * depth, line))
        del pending[:]

    for r in parse(path, lo, hi):
        if is_raw(r):
            continue
        chain = r.chain[::-1]           # outermost first
        common = 0
        while common < len(stack) and common < len(chain) and stack[common] == chain[common]:
            common += 1
        if common != len(stack) or common != len(chain):
            emit(len(stack))
            del stack[common:]
            for fn in chain[common:]:
                if len(stack) < maxdepth:
                    print('%7d %s%s' % (r.seq, '  ' * len(stack), fn))
                stack.append(fn)
        pending.append(r)
    emit(len(stack))


def flat(path, lo=0, hi=1 << 60):
    for r in parse(path, lo, hi):
        if not is_raw(r):
            print('%7d %s %-11s %05x = %0*x   %s' % (r.seq, r.op, r.space, r.addr, 2 * r.size,
                                                     r.val, ' < '.join(r.chain[:4])))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cmd, path = sys.argv[1], sys.argv[2]
    rest = [int(x, 0) for x in sys.argv[3:]]
    if cmd == 'summary':
        summary(path)
    elif cmd == 'tree':
        tree(path, *rest[:2])
    elif cmd == 'calls':
        tree(path, accesses=False, maxdepth=rest[0] if rest else 99)
    elif cmd == 'flat':
        flat(path, *rest[:2])
    else:
        sys.exit(__doc__)
