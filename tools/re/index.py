#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Build the cross reference index of the object: for every function its source
file, size, callers, callees, the strings and the named data it refers to.

    index.py build [OUT]       write the index (default re-out/index.tsv)
    index.py show NAME...      print the entries of functions (name or sub_xxxxxx)
    index.py module FILE.c     print the entries of all functions of a source file

Columns of the index, tab separated: offset, size, source file, name,
callers, callees, data (named objects and imports), strings.
"""
import os
import sys

from blob import Blob, BASES, default_path
from modmap import attribute, load_pins

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(HERE, '..', '..', 're-out', 'index.tsv')


def build(path):
    b = Blob(default_path())
    b.discover_functions()
    where = attribute(b, load_pins(os.path.join(HERE, 'modmap.txt')))
    tb = BASES['.text']
    tend = tb + b.size['.text']
    callers = {f.addr: set() for f in b.funcs}
    rows = {}
    for f in b.funcs:
        calls, data = b.refs_of(f)
        callees, objs, strs = [], [], []
        for t in calls:
            if t in b.import_at:
                objs.append(b.import_at[t])
                continue
            g = b.func_at(t) if tb <= t < tend else None
            if g is not None and g.addr != f.addr:
                if g.name not in callees:
                    callees.append(g.name)
                callers[g.addr].add(f.name)
        for t in data:
            sec = b.section_of(t)
            if sec == '.text':
                g = b.func_at(t)
                if g is not None and g.addr != f.addr:
                    # address of a function taken: a callback or a table of handlers
                    if '&' + g.name not in callees:
                        callees.append('&' + g.name)
                    callers[g.addr].add('&' + f.name)
            elif sec == '.rodata.str1.1':
                s = b.cstring(t, 80)
                if s not in strs:
                    strs.append(s)
            elif sec == 'import':
                objs.append(b.import_at[t])
            elif sec is not None:
                s = b.sym_at(t)
                if s is not None and s.kind == 'O':
                    n = s.name if s.addr == t else '%s+%#x' % (s.name, t - s.addr)
                else:
                    n = '%s+%#x' % (sec, t - BASES[sec])
                if n not in objs:
                    objs.append(n)
        rows[f.addr] = (f, callees, sorted(set(objs)), strs)
    with open(path, 'w', encoding='utf8') as o:
        for a in sorted(rows):
            f, callees, objs, strs = rows[a]
            o.write('\t'.join(['%06x' % (a - tb), '%d' % f.size, where[a], f.name,
                               ','.join(sorted(callers[a])), ','.join(callees),
                               ','.join(objs), ' | '.join(repr(s)[1:-1] for s in strs)]) + '\n')
    return len(rows)


def load(path=DEFAULT):
    out = []
    with open(path, encoding='utf8') as f:
        for line in f:
            p = line.rstrip('\n').split('\t')
            out.append(p)
    return out


def show(p):
    print('%s  %s  (.text+0x%s, %s bytes, %s)' % (p[3], '', p[0], p[1], p[2]))
    print('  callers: ' + (p[4] or '-'))
    print('  callees: ' + (p[5] or '-'))
    print('  data:    ' + (p[6] or '-'))
    print('  strings: ' + (p[7] or '-'))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == 'build':
        out = sys.argv[2] if len(sys.argv) > 2 else DEFAULT
        print('%d functions indexed in %s' % (build(out), out))
    elif cmd == 'show':
        idx = {p[3]: p for p in load()}
        for n in sys.argv[2:]:
            if n in idx:
                show(idx[n])
            else:
                print('no such function: ' + n)
    elif cmd == 'module':
        for p in load():
            if p[2] == sys.argv[2]:
                show(p)
    else:
        sys.exit(__doc__)
