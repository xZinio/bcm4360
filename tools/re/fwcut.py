#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Cut the data an open driver would have to load as firmware out of
wlc_hybrid.o_shipped: MAC microcode, register initialisation lists, PHY
tables, radio register and channel tuning tables.

Unlike the code, this data has kept its symbol names in the object.

    fwcut.py list [PATTERN]        named data objects (name, section, size)
    fwcut.py phytables [REV]       the PHY table sets (acphytbl_info_rev*)
    fwcut.py initvals NAME         decode a register initialisation list
    fwcut.py extract DIR [CORE]    write the files for a MAC core revision
                                   (default 42, the BCM4360) and a manifest

The extracted files are Broadcom's; they are for local use (the same way
b43-fwcutter is used) and must not be redistributed.
"""
import hashlib
import json
import os
import re
import struct
import sys

from blob import Blob, BASES, default_path

R_X86_64_64 = 1


def objects(b):
    return sorted((s for s in b.syms if s.kind == 'O'), key=lambda s: s.addr)


def find(b, name):
    for s in b.syms:
        if s.kind == 'O' and s.name == name:
            return s
    raise KeyError(name)


def data_of(b, s):
    return b.read(s.addr, s.size)


def raw_of(b, s):
    """Contents as in the file, without relocations applied."""
    off = s.addr - BASES[s.sec]
    return bytes(b.raw[s.sec][off:off + s.size])


def initvals(b, name):
    """List of (register offset, size in bytes, value); ends with offset 0xffff."""
    raw = data_of(b, find(b, name))
    out = []
    for i in range(0, len(raw) - 7, 8):
        addr, size, value = struct.unpack_from('<HHI', raw, i)
        if addr == 0xffff:
            break
        out.append((addr, size, value))
    return out


def phy_tables(b, name):
    """Entries of a table set: (data symbol, entries, id, offset, width in bits)."""
    s = find(b, name)
    out = []
    for p in range(s.addr, s.addr + s.size, 24):
        r = b.relocs.get(p)
        n, tid, off, width = struct.unpack('<IIII', b.read(p + 8, 16))
        if r is None:
            out.append((None, 0, n, tid, off, width))
            continue
        t = b.sym_at(r[1])
        out.append((t.name if t and t.addr == r[1] else '%#x' % r[1], r[1], n, tid, off, width))
    return out


def table_values(b, addr, n, width):
    size = {8: 1, 16: 2, 32: 4, 48: 6, 60: 8, 64: 8}[width]
    raw = b.read(addr, n * size)
    return [int.from_bytes(raw[i * size:(i + 1) * size], 'little') for i in range(n)]


def extract(b, outdir, corerev):
    os.makedirs(outdir, exist_ok=True)
    manifest = {'source': 'wlc_hybrid.o_shipped 6.30.223.271', 'corerev': corerev, 'files': []}
    want = []
    for s in objects(b):
        n = s.name
        if re.fullmatch(r'd11ucode%d|d11ucode_wowl%d|d11aeswakeucode%d' % ((corerev,) * 3), n) \
                or re.fullmatch(r'd11(wake)?ac\d(bs)?initvals%d' % corerev, n):
            want.append(s)
        elif re.match(r'(acphy_|acphytbl_|chan_tuning_2069|prefregs_2069|ovr_regs_2069)', n):
            want.append(s)
    for s in want:
        data = data_of(b, s)
        if s.name.startswith('acphytbl_info_rev'):
            # a list of descriptors with pointers: store it as text
            rows = phy_tables(b, s.name)
            text = ''.join('%-44s id %3d offset %5d entries %5d width %2d\n'
                           % (r[0], r[3], r[4], r[2], r[5]) for r in rows)
            data = text.encode()
            fn = s.name + '.txt'
        else:
            fn = s.name + '.bin'
        with open(os.path.join(outdir, fn), 'wb') as f:
            f.write(data)
        manifest['files'].append({'name': s.name, 'file': fn, 'section': s.sec,
                                  'offset': s.addr - BASES[s.sec], 'size': s.size,
                                  'sha256': hashlib.sha256(data).hexdigest()})
    with open(os.path.join(outdir, 'manifest.json'), 'w') as f:
        json.dump(manifest, f, indent=1)
    return manifest


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    b = Blob(default_path())
    cmd = sys.argv[1]
    if cmd == 'list':
        pat = re.compile(sys.argv[2]) if len(sys.argv) > 2 else None
        for s in objects(b):
            if pat is None or pat.search(s.name):
                print('%-15s %08x %8d %s' % (s.sec, s.addr - BASES[s.sec], s.size, s.name))
    elif cmd == 'phytables':
        revs = [sys.argv[2]] if len(sys.argv) > 2 else ['0', '2', '3', '6']
        for rev in revs:
            print('# acphytbl_info_rev%s' % rev)
            for name, addr, n, tid, off, width in phy_tables(b, 'acphytbl_info_rev' + rev):
                print('  id %3d (0x%02x) offset %5d entries %5d width %2d  %s'
                      % (tid, tid, off, n, width, name))
    elif cmd == 'initvals':
        for addr, size, value in initvals(b, sys.argv[2]):
            print('%04x/%d = %0*x' % (addr, size, 2 * size, value))
    elif cmd == 'extract':
        rev = int(sys.argv[3]) if len(sys.argv) > 3 else 42
        m = extract(b, sys.argv[2], rev)
        total = sum(f['size'] for f in m['files'])
        print('%d files, %d bytes written to %s' % (len(m['files']), total, sys.argv[2]))
    else:
        sys.exit(__doc__)
