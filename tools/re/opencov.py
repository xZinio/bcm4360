#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Line coverage of the open code (open/, hybrid/) in the hybrid core: which
lines the emulator's sessions run, and which they never reach. The lines
never reached are the open code no comparison with Broadcom's object has
checked - where a real card can take it and the emulator has not.

    opencov.py [--on L,..] [--off L,..] [--session] [--soak] [--files PATTERN]
               [--list PATTERN]

--session runs hybrid.py's session (the default), --soak phy_soak.py's; both
together are united. --files limits the summary to files whose path contains
PATTERN; --list prints the lines never run of the files matching PATTERN,
with their source text.

The core is built with -g into its own directory (HYBRID_OUT, default
re-out/hybrid-cov); executed blocks are mapped to lines through each object's
DWARF line table (objdump --dwarf=decodedline).
"""
import argparse
import collections
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
os.environ.setdefault('HYBRID_OUT', os.path.join(HERE, '..', '..', 're-out', 'hybrid-cov'))

import ab        # noqa: E402
import hybrid    # noqa: E402
from blob import Blob    # noqa: E402

ROOT = ab.ROOT


def object_texts(core_blob):
    """[(object path, source path, base address of its .text, size)]."""
    out = []
    objdir = os.path.join(hybrid.OUT, 'obj')
    for o in hybrid.objects():
        path = os.path.join(objdir, o.replace('/', '_'))
        if not os.path.exists(path):
            continue
        nm = subprocess.run(['nm', '--defined-only', path], capture_output=True, text=True).stdout
        base = None
        for line in nm.splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[1] == 'T' and parts[2] in core_blob.byname:
                hits = [s for s in core_blob.byname[parts[2]] if s.kind == 'F']
                if hits:
                    base = hits[0].addr - int(parts[0], 16)
                    break
        if base is None:
            continue
        hdr = subprocess.run(['objdump', '-h', path], capture_output=True, text=True).stdout
        m = re.search(r'^\s*\d+\s+\.text\s+([0-9a-f]+)', hdr, re.M)
        size = int(m.group(1), 16) if m else 0
        out.append((path, os.path.join(ROOT, o[:-2] + '.c'), base, size))
    return out


def line_table(obj):
    """[(address, file, line)] of the object's .text, sorted, plus end markers
    (address, None, None)."""
    txt = subprocess.run(['objdump', '--dwarf=decodedline', obj], capture_output=True,
                         text=True).stdout
    rows = []
    for line in txt.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1].isdigit():
            try:
                addr = int(parts[2], 0)
            except ValueError:
                continue
            rows.append((addr, parts[0], int(parts[1])))
        elif len(parts) >= 3 and parts[1] == '-' and parts[0].endswith('.c'):
            # end of a sequence: "file - 0xADDR"
            try:
                rows.append((int(parts[2], 0), None, None))
            except ValueError:
                pass
    return rows


def run(on, off, session, soak):
    ab.CFLAGS.append('-g')
    core = hybrid.build(quiet=True)
    executed = collections.defaultdict(int)     # block address -> size
    if session:
        r = hybrid.Run(core, on=on, off=off, coverage=True).session()
        for a, size in r.m.blocks.items():
            executed[a] = max(executed[a], size)
    if soak:
        import phy_soak
        r = phy_soak.SoakRun(core, off=off, on=on, coverage=True).session(
            phy_soak.STEPS, stop_on_error=False)
        for a, size in r.m.blocks.items():
            executed[a] = max(executed[a], size)
    return core, executed


def coverage(core, executed):
    """{source file: (covered lines set, all code lines set)}."""
    cb = Blob(core)
    result = {}
    for obj, src, base, size in object_texts(cb):
        ranges = sorted((a - base, a - base + n) for a, n in executed.items()
                        if base <= a < base + size)
        rows = line_table(obj)
        lines_all, lines_run = set(), set()
        name = os.path.basename(src)
        for i, (addr, f, ln) in enumerate(rows):
            if f is None or i + 1 >= len(rows):
                continue
            end = rows[i + 1][0]
            if end <= addr:
                continue
            if os.path.basename(f) != name or ln == 0:
                continue        # lines of headers (inline functions) are left out
            lines_all.add(ln)
            for s, e in ranges:
                if s < end and addr < e:
                    lines_run.add(ln)
                    break
        result[src] = (lines_run, lines_all)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--on', default='')
    ap.add_argument('--off', default='')
    ap.add_argument('--session', action='store_true')
    ap.add_argument('--soak', action='store_true')
    ap.add_argument('--files', default='')
    ap.add_argument('--list', default='')
    ns = ap.parse_args()
    if not ns.session and not ns.soak:
        ns.session = True
    on = [x for x in ns.on.split(',') if x]
    off = [x for x in ns.off.split(',') if x]
    core, executed = run(on, off, ns.session, ns.soak)
    cov = coverage(core, executed)
    total_run = total_all = 0
    print('%-40s %6s %6s %6s' % ('file', 'lines', 'run', '%'))
    for src in sorted(cov):
        rel = os.path.relpath(src, ROOT)
        run_l, all_l = cov[src]
        total_run += len(run_l & all_l)
        total_all += len(all_l)
        if ns.files and ns.files not in rel:
            continue
        print('%-40s %6d %6d %5.1f%%' % (rel, len(all_l), len(run_l & all_l),
                                         100.0 * len(run_l & all_l) / max(len(all_l), 1)))
    print('%-40s %6d %6d %5.1f%%' % ('all', total_all, total_run,
                                     100.0 * total_run / max(total_all, 1)))
    if ns.list:
        for src in sorted(cov):
            rel = os.path.relpath(src, ROOT)
            if ns.list not in rel:
                continue
            run_l, all_l = cov[src]
            never = sorted(all_l - run_l)
            text = open(src).read().splitlines()
            print('\n===== %s: %d lines never run' % (rel, len(never)))
            prev = None
            for ln in never:
                if prev is not None and ln != prev + 1:
                    print('   ...')
                print('%5d  %s' % (ln, text[ln - 1] if ln <= len(text) else ''))
                prev = ln
    return 0


if __name__ == '__main__':
    sys.exit(main())
