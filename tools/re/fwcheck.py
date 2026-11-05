#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Check a firmware file of bcm4360_open (made by scripts/bcm4360-fwcut.py)
against Broadcom's object as blob.py loads it (sections laid out, relocations
applied), independently of the cutter's own ELF reader.

    fwcheck.py OUT.bin [OBJECT]

* the header, the directory, entries inside the file, names unique;
* the entry list is hybrid/fwdata.h plus clm_db, clm_db.reloc, clm_db.root,
  and the cutter's own list equals hybrid/fwdata.h;
* every fwdata.h entry: size as named there, bytes equal to the object's
  symbol of that name (with relocations applied);
* clm_db: relocated to the address blob.py gives clm_header (every slot of
  clm_db.reloc plus that base), the bytes equal the object's from clm_header
  on; clm_db.root names clm_header and clm_data; the listed slots are exactly
  the object's relocations in that range.

Prints nothing of the data.
"""
import os
import re
import struct
import sys

from blob import BASES, Blob, R_X86_64_64, default_path

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')


def fwdata_h():
    with open(os.path.join(ROOT, 'hybrid', 'fwdata.h')) as f:
        return [(m.group(1), int(m.group(2)))
                for m in re.finditer(r'^HYB_FW\((\w+),\s*(\d+)\)', f.read(), re.M)]


def cutter_list():
    sys.path.insert(0, os.path.join(ROOT, 'scripts'))
    import importlib.util
    spec = importlib.util.spec_from_file_location('fwcut', os.path.join(ROOT, 'scripts',
                                                                        'bcm4360-fwcut.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.FWDATA


def read_fw(path):
    with open(path, 'rb') as f:
        fw = f.read()
    assert fw[:8] == b'B4360FW1', 'magic'
    count, version = struct.unpack_from('<II', fw, 8)
    assert version == 0x061edf0f, 'version %#x' % version
    ents = {}
    order = []
    for i in range(count):
        name, off, size = struct.unpack_from('<48sII', fw, 16 + 56 * i)
        assert b'\0' in name, 'name not terminated'
        name = name.split(b'\0')[0].decode()
        assert name not in ents, 'duplicate ' + name
        assert off + size <= len(fw), name + ' outside the file'
        assert off % 8 == 0, name + ' not aligned'
        ents[name] = fw[off:off + size]
        order.append(name)
    return order, ents


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    b = Blob(argv[2] if len(argv) > 2 else default_path())
    order, ents = read_fw(argv[1])
    want = fwdata_h()
    assert cutter_list() == want, 'the cutter\'s list differs from hybrid/fwdata.h'
    clm = ['clm_db', 'clm_db.reloc', 'clm_db.root']
    assert order == [n for n, _ in want] + clm, 'entry list'

    for name, size in want:
        syms = [s for s in b.byname.get(name, []) if s.kind == 'O']
        assert len(syms) == 1, name
        s = syms[0]
        assert s.size == size == len(ents[name]), name + ' size'
        assert ents[name] == b.read(s.addr, size), name + ' bytes'
        assert not any(p in b.relocs for p in range(s.addr, s.addr + size)), name + ' pointers'

    hdr = b.byname['clm_header'][0].addr
    data = b.byname['clm_data'][0].addr
    db = bytearray(ents['clm_db'])
    slots = struct.unpack('<%dI' % (len(ents['clm_db.reloc']) // 4), ents['clm_db.reloc'])
    roots = struct.unpack('<%dI' % (len(ents['clm_db.root']) // 4), ents['clm_db.root'])
    assert list(roots) == [0, data - hdr], 'clm_db.root'
    assert list(slots) == sorted(set(slots)), 'clm_db.reloc not sorted'
    for off in slots:
        v, = struct.unpack_from('<Q', db, off)
        assert v < len(db), 'pointer out of the database'
        struct.pack_into('<Q', db, off, v + hdr)
    assert bytes(db) == b.read(hdr, len(db)), 'clm_db bytes'
    inrange = sorted(p - hdr for p, (t, _) in b.relocs.items() if hdr <= p < hdr + len(db))
    assert inrange == list(slots), 'clm_db.reloc differs from the object\'s relocations'
    assert all(b.relocs[hdr + p][0] == R_X86_64_64 for p in slots)

    total = sum(len(v) for v in ents.values())
    print('OK: %d entries (%d of hybrid/fwdata.h, 3 of the CLM), %d bytes of data; '
          'clm_db %d bytes at .rodata+%#x, %d pointers'
          % (len(order), len(want), total, len(db), hdr - BASES['.rodata'],
             len(slots)))


if __name__ == '__main__':
    main(sys.argv)
