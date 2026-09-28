#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Attribute every function of the blob to its original source file.

The blob is an `ld -r` of 111 objects in alphabetical file order and its
symbol table lists the files but not which symbol belongs to which.  The
first function of each file is pinned by hand below (from the names of the
functions and the strings they print); everything up to the next pin belongs
to that file.

  modmap.py            summary per file
  modmap.py --funcs    every function with its file
"""
import sys

from blob import Blob, BASES, default_path

# file -> name of the first function (or .text offset) in link order
PINS = []


def load_pins(path):
    pins = []
    with open(path) as f:
        for line in f:
            line = line.split('#')[0].strip()
            if not line:
                continue
            name, first = line.split()
            pins.append((name, first))
    return pins


def attribute(b, pins):
    tb = BASES['.text']
    starts = []
    for name, first in pins:
        if first.startswith('0x'):
            a = tb + int(first, 16)
        else:
            a = b.fbyname[first].addr
        starts.append((a, name))
    starts.sort()
    out = {}
    i = 0
    cur = '?'
    for f in b.funcs:
        while i < len(starts) and starts[i][0] <= f.addr:
            cur = starts[i][1]
            i += 1
        out[f.addr] = cur
    return out


if __name__ == '__main__':
    import os
    b = Blob(default_path())
    b.discover_functions()
    pins = load_pins(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'modmap.txt'))
    where = attribute(b, pins)
    tb = BASES['.text']
    if '--funcs' in sys.argv:
        for f in b.funcs:
            print('%06x %6d %-22s %s' % (f.addr - tb, f.size, where[f.addr], f.name))
    else:
        agg = {}
        order = []
        for f in b.funcs:
            w = where[f.addr]
            if w not in agg:
                agg[w] = [0, 0, 0, f.addr - tb]
                order.append(w)
            agg[w][0] += 1
            agg[w][1] += f.size
            agg[w][2] += 1 if f.kind == 'F' else 0
        print('%-24s %8s %6s %6s %8s' % ('file', 'start', 'funcs', 'named', 'bytes'))
        for w in order:
            n, sz, named, start = agg[w]
            print('%-24s %08x %6d %6d %8d' % (w, start, n, named, sz))
