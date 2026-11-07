#!/usr/bin/env python3
"""
bcm4360-fwcut.py: make the firmware file of the open driver bcm4360_open
(/lib/firmware/bcm4360/bcm4360-data.bin) out of Broadcom's download, the way
b43-fwcutter does it for b43.

    bcm4360-fwcut.py OBJECT|TARBALL OUT.bin

OBJECT is lib/wlc_hybrid.o_shipped of Broadcom's 6.30.223.271 release
(SHA-256 checked; any other file is refused); TARBALL is the downloaded
hybrid-v35_64-nodebug-pcoem-6_30_223_271.tar.gz, from which that object is
read (in memory, nothing is unpacked).

What it cuts out (docs/re/03-tools.md, drv/drv.h for the file format):

* every named data object of hybrid/fwdata.h, as it is in the object: the MAC
  microcode d11ucode42, its initialisation lists, the coexistence table, the
  PHY, transmit gain and radio tables (none of them contains a pointer);
* the regulatory database (docs/re/spec/clm.md) as three entries:
  "clm_db" the bytes from clm_header to the end of the last table the
  database reaches through its pointers, each 64-bit pointer stored as an
  offset from the start of the entry; "clm_db.reloc" the offsets of those
  pointers (le32 each); "clm_db.root" the offsets of clm_header and
  clm_data within clm_db (le32 each). The loader adds the address of its
  copy to every listed pointer (drv/fw.c, bcm4360_fw_clm()).

The output is Broadcom's data, for the machine it is made on; this script
contains none of it. Python 3 standard library only.
"""
import bisect
import hashlib
import os
import struct
import sys
import tarfile

OBJECT_SHA256 = '352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743'
TAR_MEMBER = 'lib/wlc_hybrid.o_shipped'
VERSION = 0x061edf0f            # 6.30.223.271, as drv/drv.h names it
MAGIC = b'B4360FW1'
NAME_LEN = 48
ALIGN = 16                      # of every entry's data in the file

# hybrid/fwdata.h: name, size in bytes (tools/re/fwcheck.py compares the two)
FWDATA = [
    ('acphy_est_pwr_lut_core0_rev0', 256),
    ('acphy_est_pwr_lut_core1_rev0', 256),
    ('acphy_est_pwr_lut_core2_rev0', 256),
    ('acphy_iq_lut_core0_rev0', 512),
    ('acphy_iq_lut_core2_rev0', 512),
    ('acphy_loft_lut_core0_rev0', 256),
    ('acphy_loft_lut_core1_rev0', 256),
    ('acphy_loft_lut_core2_rev0', 256),
    ('acphy_mcs_tbl_rev0', 256),
    ('acphy_noise_shaping_tbl_rev0', 1024),
    ('acphy_papd_cal_scalars_tbl_core0_rev0', 256),
    ('acphy_papd_cal_scalars_tbl_core1_rev0', 256),
    ('acphy_papd_cal_scalars_tbl_core2_rev0', 256),
    ('acphy_papd_comp_epsilon_tbl_core0_rev0', 256),
    ('acphy_papd_comp_epsilon_tbl_core1_rev0', 256),
    ('acphy_papd_comp_epsilon_tbl_core2_rev0', 256),
    ('acphy_papd_comp_rfpwr_tbl_core0_rev0', 256),
    ('acphy_papd_comp_rfpwr_tbl_core1_rev0', 256),
    ('acphy_papd_comp_rfpwr_tbl_core2_rev0', 256),
    ('acphy_phasetrack_tbl_rev0', 88),
    ('acphy_rx_evm_shaping_tbl_rev0', 256),
    ('acphy_tx_evm_tbl_rev0', 38),
    ('acphy_txgain_epa_2g_2069rev0', 768),
    ('acphy_txgain_epa_2g_2069rev4', 768),
    ('acphy_txgain_epa_2g_2069rev4_id1', 768),
    ('acphy_txgain_epa_5g_2069rev0', 768),
    ('acphy_txgain_epa_5g_2069rev4', 768),
    ('acphy_txgain_ipa_2g_2069rev0', 768),
    ('acphy_txgain_ipa_5g_2069rev0', 768),
    ('acphy_txv_for_spexp', 7776),
    ('chan_tuning_2069rev3', 8932),
    ('chan_tuning_2069rev4', 8932),
    ('prefregs_2069_rev3', 92),
    ('prefregs_2069_rev4', 100),
    ('rx_farrow_tbl', 4428),
    ('tx_farrow_dac1_tbl', 4428),
    ('btc_ucode_flags', 36),
    ('d11ac1bsinitvals42', 592),
    ('d11ac1initvals42', 4888),
    ('d11ucode42', 43400),
]

# clm.md, "Data": the header (what clm_init() takes) and the root table
CLM_ROOTS = ('clm_header', 'clm_data')
CLM_ENTRY = 'clm_db'

SHT_SYMTAB, SHT_RELA, SHT_NOBITS = 2, 4, 8
SHF_ALLOC = 0x2
STT_OBJECT, STT_FUNC, STT_SECTION = 1, 2, 3
R_X86_64_64, R_X86_64_PC32, R_X86_64_PLT32 = 1, 2, 4


class FwcutError(Exception):
    pass


# ------------------------------------------------------------- ELF64 reader

class Elf:
    """The parts of an x86-64 ELF relocatable object used here: sections,
    symbols, RELA relocations. Addresses are (section index, offset)."""

    def __init__(self, data):
        if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
            raise FwcutError('not a little-endian ELF64 file')
        e_type, e_machine = struct.unpack_from('<HH', data, 16)
        if e_type != 1 or e_machine != 62:
            raise FwcutError('not an x86-64 relocatable object')
        shoff, = struct.unpack_from('<Q', data, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3a)
        self.data = data
        self.secs = []
        for i in range(shnum):
            (name, typ, flags, addr, off, size, link, info, align,
             entsize) = struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize)
            self.secs.append({'name_off': name, 'type': typ, 'flags': flags, 'off': off,
                              'size': size, 'link': link, 'info': info, 'entsize': entsize})
        strtab = self.secs[shstrndx]
        for s in self.secs:
            s['name'] = self._str(strtab, s['name_off'])
        self._symbols()
        self._relocations()

    def _str(self, sec, off):
        start = sec['off'] + off
        end = self.data.index(b'\0', start)
        return self.data[start:end].decode('latin1')

    def contents(self, idx):
        s = self.secs[idx]
        if s['type'] == SHT_NOBITS:
            return bytes(s['size'])
        return self.data[s['off']:s['off'] + s['size']]

    def _symbols(self):
        tabs = [i for i, s in enumerate(self.secs) if s['type'] == SHT_SYMTAB]
        if len(tabs) != 1:
            raise FwcutError('expected one symbol table')
        st = self.secs[tabs[0]]
        names = self.secs[st['link']]
        self.symtab = tabs[0]
        self.syms = []          # (name, type, shndx, value, size)
        for i in range(st['size'] // 24):
            name, info, other, shndx, value, size = struct.unpack_from(
                '<IBBHQQ', self.data, st['off'] + 24 * i)
            self.syms.append((self._str(names, name), info & 0xf, shndx, value, size))

    def _relocations(self):
        # target section -> {offset: (type, symbol shndx, symbol value + addend)}
        self.relocs = {}
        for s in self.secs:
            if s['type'] != SHT_RELA:
                continue
            if s['link'] != self.symtab:
                raise FwcutError('relocations against another symbol table')
            out = self.relocs.setdefault(s['info'], {})
            for i in range(s['size'] // 24):
                off, info, addend = struct.unpack_from('<QQq', self.data, s['off'] + 24 * i)
                typ, sym = info & 0xffffffff, info >> 32
                _, _, shndx, value, _ = self.syms[sym]
                out[off] = (typ, shndx, value + addend)

    def objects(self, name):
        """Defined data objects of that name: (section index, offset, size)."""
        return [(shndx, value, size) for n, t, shndx, value, size in self.syms
                if n == name and t == STT_OBJECT and 0 < shndx < len(self.secs)]


# ------------------------------------------------------------- the entries

def find_object(elf, name):
    found = elf.objects(name)
    if len(found) != 1:
        raise FwcutError('%s: %d definitions in the object' % (name, len(found)))
    return found[0]


def named_entry(elf, name, size):
    sec, off, osize = find_object(elf, name)
    if osize != size:
        raise FwcutError('%s: %d bytes in the object, %d expected' % (name, osize, size))
    rel = elf.relocs.get(sec, {})
    if any(off <= p < off + size for p in rel):
        raise FwcutError('%s contains pointers' % name)
    return elf.contents(sec)[off:off + size]


def clm_region(elf):
    """The regulatory database: the byte range of its section that the roots
    reach through 64-bit pointers, transitively. Every table starts at an
    address something points to; it ends where the next one starts (the next
    address any relocation of the object points to, or the next symbol).
    Returns (section, start, end, roots, pointers) with the pointers as
    {offset in section: target offset in section}."""
    roots = [find_object(elf, n) for n in CLM_ROOTS]
    sec = roots[0][0]
    if any(r[0] != sec for r in roots):
        raise FwcutError('the CLM roots are in different sections')
    rel = elf.relocs.get(sec, {})
    # every start of something in the section
    starts = set()
    for tsec, rs in elf.relocs.items():
        for typ, shndx, target in rs.values():
            if shndx == sec:
                # a PC-relative field counts from the end of the instruction
                starts.add(target + 4 if typ in (R_X86_64_PC32, R_X86_64_PLT32) else target)
    for _, t, shndx, value, _ in elf.syms:
        if shndx == sec and t in (STT_OBJECT, STT_FUNC):
            starts.add(value)
    starts = sorted(starts)
    positions = sorted(rel)
    size = elf.secs[sec]['size']

    def table_end(t):
        i = bisect.bisect_right(starts, t)
        return starts[i] if i < len(starts) else size

    reached = {r[1] for r in roots}
    todo = sorted(reached)
    pointers = {}
    while todo:
        t = todo.pop()
        end = table_end(t)
        i = bisect.bisect_left(positions, t)
        while i < len(positions) and positions[i] < end:
            p = positions[i]
            typ, shndx, target = rel[p]
            if typ != R_X86_64_64:
                raise FwcutError('CLM: relocation type %d at %#x' % (typ, p))
            if shndx != sec:
                raise FwcutError('CLM: pointer at %#x leaves the section' % p)
            pointers[p] = target
            if target not in reached:
                reached.add(target)
                todo.append(target)
            i += 1
    start = min(reached)
    end = table_end(max(reached))
    # the range must be the database's alone: what lies in it is reached, and
    # nothing outside points into it except at the roots
    for p in positions:
        if start <= p < end and p not in pointers:
            raise FwcutError('CLM: pointer at %#x not reached from the roots' % p)
    rootoffs = {r[1] for r in roots}
    for tsec, rs in elf.relocs.items():
        for p, (typ, shndx, target) in rs.items():
            if typ in (R_X86_64_PC32, R_X86_64_PLT32):
                target += 4
            if shndx == sec and start <= target < end and target not in rootoffs \
                    and not (tsec == sec and start <= p < end):
                raise FwcutError('CLM: %#x is referred to from outside the database' % target)
    for p, target in pointers.items():
        if not start <= target < end:
            raise FwcutError('CLM: pointer at %#x points out of the range' % p)
    return sec, start, end, [r[1] for r in roots], pointers


def clm_entries(elf):
    sec, start, end, roots, pointers = clm_region(elf)
    db = bytearray(elf.contents(sec)[start:end])
    for p, target in pointers.items():
        struct.pack_into('<Q', db, p - start, target - start)
    reloc = b''.join(struct.pack('<I', p - start) for p in sorted(pointers))
    root = b''.join(struct.pack('<I', r - start) for r in roots)
    return [(CLM_ENTRY, bytes(db)), (CLM_ENTRY + '.reloc', reloc), (CLM_ENTRY + '.root', root)]


def entries(obj):
    elf = Elf(obj)
    out = [(name, named_entry(elf, name, size)) for name, size in FWDATA]
    return out + clm_entries(elf)


def build(ents):
    head = 16 + 56 * len(ents)
    pos = (head + ALIGN - 1) & ~(ALIGN - 1)
    dir_, body = [], bytearray()
    for name, data in ents:
        n = name.encode()
        if len(n) >= NAME_LEN:
            raise FwcutError('%s: name too long' % name)
        dir_.append(struct.pack('<48sII', n, pos + len(body), len(data)))
        body += data
        body += bytes(-len(body) % ALIGN)
    out = MAGIC + struct.pack('<II', len(ents), VERSION) + b''.join(dir_)
    return out + bytes(pos - len(out)) + body


# ------------------------------------------------------------- input, output

def read_object(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'\x7fELF':
        try:
            with tarfile.open(path, 'r:*') as tar:
                m = tar.getmember(TAR_MEMBER)
                if not m.isfile():
                    raise FwcutError('%s: %s is not a file' % (path, TAR_MEMBER))
                data = tar.extractfile(m).read()
        except (tarfile.TarError, KeyError) as e:
            raise FwcutError('%s: neither the object nor the tarball with %s (%s)'
                             % (path, TAR_MEMBER, e))
    digest = hashlib.sha256(data).hexdigest()
    if digest != OBJECT_SHA256:
        raise FwcutError('wlc_hybrid.o_shipped has SHA-256 %s, not %s (Broadcom 6.30.223.271)'
                         % (digest, OBJECT_SHA256))
    return data


def main(argv):
    if len(argv) != 3 or argv[1].startswith('-'):
        sys.stderr.write(__doc__)
        return 2
    try:
        ents = entries(read_object(argv[1]))
    except (OSError, FwcutError) as e:
        sys.stderr.write('bcm4360-fwcut: %s\n' % e)
        return 1
    out = build(ents)
    d = os.path.dirname(os.path.abspath(argv[2]))
    os.makedirs(d, exist_ok=True)
    tmp = argv[2] + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(out)
    os.chmod(tmp, 0o644)
    os.replace(tmp, argv[2])
    db = dict(ents)
    print('%s: %d entries, %d bytes; CLM database %d bytes, %d pointers '
          '(Broadcom\'s data: not for redistribution)'
          % (argv[2], len(ents), len(out), len(db[CLM_ENTRY]), len(db[CLM_ENTRY + '.reloc']) // 4))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
