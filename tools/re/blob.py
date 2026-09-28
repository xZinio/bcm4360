#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Loader for Broadcom's wlc_hybrid.o_shipped (x86-64 ELF relocatable).

Lays the allocated sections out in a flat address space, applies the
relocations and offers symbol lookup, function discovery and annotated
disassembly.  Used by the other scripts in tools/re/.

The object is only read; nothing here modifies it.
"""
import bisect
import struct
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
from elftools.elf.relocation import RelocationSection

try:
    import capstone
    from capstone import x86 as cx86
except ImportError:  # loading/symbols work without capstone
    capstone = None

R_X86_64_64 = 1
R_X86_64_PC32 = 2
R_X86_64_32S = 11

# Flat layout: everything below 2 GiB so that R_X86_64_32S is representable.
BASES = {
    '.text': 0x01000000,
    '.rodata': 0x02000000,
    '.rodata.str1.1': 0x02800000,
    '.data': 0x03000000,
    '.bss': 0x03800000,
}
IMPORT_BASE = 0x04000000
IMPORT_STEP = 0x10


class Sym:
    __slots__ = ('name', 'addr', 'size', 'kind', 'sec', 'glob')

    def __init__(self, name, addr, size, kind, sec, glob):
        self.name, self.addr, self.size = name, addr, size
        self.kind, self.sec, self.glob = kind, sec, glob

    def __repr__(self):
        return 'Sym(%s @%#x +%d %s)' % (self.name, self.addr, self.size, self.kind)


class Blob:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self._load(ELFFile(f))

    # ------------------------------------------------------------ loading
    def _load(self, elf):
        secs = list(elf.iter_sections())
        self.secnames = [s.name for s in secs]
        self.data = {}       # section name -> bytearray (relocated)
        self.raw = {}        # section name -> bytes (as in the file)
        self.size = {}
        for s in secs:
            if s.name in BASES:
                if s['sh_type'] == 'SHT_NOBITS':
                    self.raw[s.name] = bytes(s['sh_size'])
                else:
                    self.raw[s.name] = s.data()
                self.data[s.name] = bytearray(self.raw[s.name])
                self.size[s.name] = s['sh_size']

        symtab = [s for s in secs if isinstance(s, SymbolTableSection)][0]
        self.elfsyms = list(symtab.iter_symbols())
        self.imports = {}    # name -> address of its stub slot
        self.import_at = {}  # address -> name
        self.syms = []       # defined FUNC/OBJECT symbols
        self.files = []
        symaddr = []         # per ELF symbol index: flat address (or None)
        for sym in self.elfsyms:
            kind = sym['st_info']['type']
            shndx = sym['st_shndx']
            if kind == 'STT_FILE':
                self.files.append(sym.name)
                symaddr.append(None)
            elif shndx == 'SHN_UNDEF':
                if sym.name:
                    a = IMPORT_BASE + IMPORT_STEP * len(self.imports)
                    self.imports[sym.name] = a
                    self.import_at[a] = sym.name
                    symaddr.append(a)
                else:
                    symaddr.append(None)
            elif isinstance(shndx, int) and self.secnames[shndx] in BASES:
                sec = self.secnames[shndx]
                a = BASES[sec] + sym['st_value']
                symaddr.append(a)
                if kind in ('STT_FUNC', 'STT_OBJECT'):
                    self.syms.append(Sym(sym.name, a, sym['st_size'],
                                         'F' if kind == 'STT_FUNC' else 'O', sec,
                                         sym['st_info']['bind'] == 'STB_GLOBAL'))
            else:
                symaddr.append(None)
        self.symaddr = symaddr

        # relocations: flat address of the field -> (type, target address)
        self.relocs = {}
        for s in secs:
            if not isinstance(s, RelocationSection):
                continue
            tgt = self.secnames[s['sh_info']]
            if tgt not in BASES:
                continue
            base = BASES[tgt]
            buf = self.data[tgt]
            for r in s.iter_relocations():
                t = r['r_info_type']
                S = symaddr[r['r_info_sym']]
                if S is None:
                    raise ValueError('relocation against unplaced symbol')
                off = r['r_offset']
                P = base + off
                A = r['r_addend']
                if t == R_X86_64_64:
                    struct.pack_into('<Q', buf, off, (S + A) & 0xffffffffffffffff)
                    self.relocs[P] = (t, S + A)
                elif t == R_X86_64_32S:
                    struct.pack_into('<i', buf, off, S + A)
                    self.relocs[P] = (t, S + A)
                elif t == R_X86_64_PC32:
                    struct.pack_into('<i', buf, off, S + A - P)
                    # The displacement counts from the end of the instruction.
                    # Usually the field is its last part (addend -4); if an
                    # immediate follows it, hit() corrects the target.
                    self.relocs[P] = (t, S + A + 4)
                else:
                    raise ValueError('unhandled relocation type %d' % t)

        self.syms.sort(key=lambda s: (s.addr, -s.size))
        self._addrs = [s.addr for s in self.syms]
        self.byname = {}
        for s in self.syms:
            self.byname.setdefault(s.name, []).append(s)
        self.funcs = None

    def hits(self, a, size):
        """Relocations inside the instruction at a: list of (type, target)."""
        out = []
        for p in range(a, a + size):
            r = self.relocs.get(p)
            if r:
                if r[0] == R_X86_64_PC32:
                    # bytes between the field and the end of the instruction
                    r = (r[0], r[1] + (a + size) - (p + 4))
                out.append(r)
        return out

    # ------------------------------------------------------------ lookup
    def section_of(self, addr):
        for name, base in BASES.items():
            if base <= addr < base + max(self.size.get(name, 0), 1):
                return name
        if IMPORT_BASE <= addr < IMPORT_BASE + IMPORT_STEP * len(self.imports):
            return 'import'
        return None

    def read(self, addr, n):
        sec = self.section_of(addr)
        if sec is None or sec == 'import':
            raise ValueError('address %#x not in image' % addr)
        off = addr - BASES[sec]
        return bytes(self.data[sec][off:off + n])

    def cstring(self, addr, limit=200):
        sec = self.section_of(addr)
        off = addr - BASES[sec]
        buf = self.data[sec]
        end = buf.find(b'\0', off, off + limit)
        if end < 0:
            end = off + limit
        return bytes(buf[off:end]).decode('latin1')

    def sym_at(self, addr):
        """Closest defined symbol at or below addr in the same section."""
        i = bisect.bisect_right(self._addrs, addr) - 1
        while i >= 0:
            s = self.syms[i]
            if s.addr + max(s.size, 1) > addr and self.section_of(s.addr) == self.section_of(addr):
                return s
            if addr - s.addr > 0x400000:
                break
            i -= 1
        return None

    def describe(self, addr):
        """Human readable name for a flat address."""
        if addr in self.import_at:
            return self.import_at[addr]
        sec = self.section_of(addr)
        if sec is None:
            return '%#x' % addr
        if sec == '.rodata.str1.1':
            return 'str:"%s"' % self.cstring(addr, 60).replace('\n', '\\n')
        if sec == '.text' and self.funcs is not None:
            f = self.func_at(addr)
            if f is not None:
                d = addr - f.addr
                return f.name if d == 0 else '%s+%#x' % (f.name, d)
        s = self.sym_at(addr)
        if s is not None:
            d = addr - s.addr
            return s.name if d == 0 else '%s+%#x' % (s.name, d)
        return '%s+%#x' % (sec, addr - BASES[sec])

    # ------------------------------------------------------------ code
    def sweep(self):
        """Linear sweep of .text. Returns list of (addr, size, mnem, ops)."""
        if getattr(self, '_insns', None) is not None:
            return self._insns
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        code = bytes(self.data['.text'])
        base = BASES['.text']
        out = []
        pos = 0
        n = len(code)
        while pos < n:
            last = pos
            for a, size, mnem, ops in md.disasm_lite(code[pos:], base + pos):
                out.append((a, size, mnem, ops))
                last = a + size - base
            if last >= n:
                break
            if last == pos or True:
                # undecodable byte: emit it as data and resync
                if last < n and (not out or out[-1][0] + out[-1][1] - base <= last):
                    out.append((base + last, 1, '.byte', '%#x' % code[last]))
                    last += 1
            pos = last
        self._insns = out
        self._insn_addrs = [i[0] for i in out]
        return out

    def discover_functions(self):
        """Named functions plus the stripped static ones found in the gaps."""
        if self.funcs is not None:
            return self.funcs
        insns = self.sweep()
        tbase = BASES['.text']
        tend = tbase + self.size['.text']
        named = {s.addr: s for s in self.syms if s.kind == 'F'}

        # jump tables: indirect jumps through .rodata
        jt_targets = set()
        jt_tables = {}
        rbase = BASES['.rodata']
        rend = rbase + self.size['.rodata']
        rod = self.data['.rodata']
        for a, size, mnem, ops in insns:
            if mnem in ('jmp', 'notrack jmp') and ops.startswith('qword ptr [') and '*8' in ops:
                for p in range(a, a + size):
                    r = self.relocs.get(p)
                    if r and r[0] == R_X86_64_32S and rbase <= r[1] < rend:
                        t = r[1]
                        ents = []
                        while t + 8 <= rend:
                            rr = self.relocs.get(t)
                            if not rr or rr[0] != R_X86_64_64 or not (tbase <= rr[1] < tend):
                                break
                            if ents and t in jt_tables:
                                break
                            ents.append(rr[1])
                            t += 8
                        jt_tables[r[1]] = ents
                        jt_targets.update(ents)
        self.jump_tables = jt_tables

        entries = set(named)
        calls = set()
        jumps = set()
        for a, size, mnem, ops in insns:
            if mnem == 'call' and ops.startswith('0x'):
                t = int(ops, 16)
                if tbase <= t < tend:
                    calls.add(t)
            elif mnem.startswith('j') and ops.startswith('0x'):
                t = int(ops, 16)
                if tbase <= t < tend:
                    jumps.add(t)
        entries |= calls
        # code pointers stored in data or loaded as immediates
        ptrs = set()
        in_tables = set()
        for tstart, ents in jt_tables.items():
            for i in range(len(ents)):
                in_tables.add(tstart + 8 * i)
        for p, (t, target) in self.relocs.items():
            if t in (R_X86_64_64, R_X86_64_32S) and tbase <= target < tend:
                if p in in_tables:
                    continue
                ptrs.add(target)
        entries |= ptrs

        # walk the instructions, splitting after terminators
        labels = jt_targets | jumps
        funcs = []
        cur = None
        maxt = 0
        prev_term = True
        for a, size, mnem, ops in insns:
            if a in entries or (prev_term and cur is not None and a > maxt and a not in labels
                                and mnem not in ('nop', 'int3', '.byte')
                                and not (mnem == 'xchg' and ops == 'ax, ax')):
                if cur is None or a != cur:
                    cur = a
                    funcs.append(a)
                    maxt = 0
            elif cur is None:
                cur = a
                funcs.append(a)
            if mnem.startswith('j') and ops.startswith('0x'):
                t = int(ops, 16)
                if t > maxt and t not in entries:
                    maxt = t
            if mnem == 'jmp' and ops.startswith('qword ptr [') and '*8' in ops:
                for p in range(a, a + size):
                    r = self.relocs.get(p)
                    if r and r[1] in jt_tables:
                        for t in jt_tables[r[1]]:
                            if t > maxt:
                                maxt = t
            prev_term = mnem in ('ret', 'jmp', 'ud2', 'hlt', 'retf', 'iretq')
        funcs = sorted(set(funcs))
        out = []
        for i, a in enumerate(funcs):
            end = funcs[i + 1] if i + 1 < len(funcs) else tend
            if a in named:
                s = named[a]
                out.append(Sym(s.name, a, end - a, 'F', '.text', s.glob))
            else:
                out.append(Sym('sub_%06x' % (a - tbase), a, end - a, 'f', '.text', False))
        self.funcs = out
        self._faddrs = [f.addr for f in out]
        self.fbyname = {f.name: f for f in out}
        self.code_ptrs = ptrs
        self.call_targets = calls
        return out

    def func_at(self, addr):
        i = bisect.bisect_right(self._faddrs, addr) - 1
        if i < 0:
            return None
        f = self.funcs[i]
        return f if addr < f.addr + f.size else None

    def func(self, name_or_addr):
        self.discover_functions()
        if isinstance(name_or_addr, int):
            return self.func_at(name_or_addr)
        if name_or_addr in self.fbyname:
            return self.fbyname[name_or_addr]
        if name_or_addr.startswith('0x'):
            a = int(name_or_addr, 16)
            if a < BASES['.text']:
                a += BASES['.text']
            return self.func_at(a)
        return None

    def insns_of(self, f):
        self.sweep()
        i = bisect.bisect_left(self._insn_addrs, f.addr)
        j = bisect.bisect_left(self._insn_addrs, f.addr + f.size)
        return self._insns[i:j]

    def refs_of(self, f):
        """(calls, data) referenced by a function: sets of flat addresses."""
        calls, data = [], []
        tbase = BASES['.text']
        tend = tbase + self.size['.text']
        for a, size, mnem, ops in self.insns_of(f):
            # an instruction can carry two relocations (displacement and immediate)
            hits = self.hits(a, size)
            if mnem in ('call', 'jmp') and ops.startswith('0x'):
                t = int(ops, 16)
                if not (f.addr <= t < f.addr + f.size):
                    calls.append(t)
            elif mnem.startswith('j'):
                continue
            else:
                for hit in hits:
                    data.append(hit[1])
        return calls, data

    def disasm(self, f, out=sys.stdout):
        tbase = BASES['.text']
        labels = set()
        body = self.insns_of(f)
        for a, size, mnem, ops in body:
            if mnem.startswith('j') and ops.startswith('0x'):
                t = int(ops, 16)
                if f.addr <= t < f.addr + f.size:
                    labels.add(t)
            if mnem == 'jmp' and '*8' in ops:
                for p in range(a, a + size):
                    r = self.relocs.get(p)
                    if r and r[1] in self.jump_tables:
                        labels.update(self.jump_tables[r[1]])
        out.write('; %s  .text+%#x  size %d\n' % (f.name, f.addr - tbase, f.size))
        for a, size, mnem, ops in body:
            if a in labels:
                out.write('L%x:\n' % (a - f.addr))
            note = ''
            found = self.hits(a, size)
            hit = found[0] if found else None
            if mnem in ('call', 'jmp') or mnem.startswith('j'):
                if ops.startswith('0x'):
                    t = int(ops, 16)
                    if f.addr <= t < f.addr + f.size and mnem != 'call':
                        ops = 'L%x' % (t - f.addr)
                    else:
                        ops = self.describe(t)
                elif hit and hit[1] in self.jump_tables:
                    note = ' ; switch: ' + ' '.join(
                        'L%x' % (t - f.addr) for t in self.jump_tables[hit[1]])
                elif hit:
                    note = ' ; ' + self.describe(hit[1])
            elif hit:
                note = ' ; ' + ', '.join(self.describe(h[1]) for h in found)
            out.write('  %5x: %-7s %s%s\n' % (a - f.addr, mnem, ops, note))


def default_path():
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, '..', '..', 'lib', 'wlc_hybrid.o_shipped')


# Where Ghidra puts the sections when it imports the object (it packs them
# from 0x100000 on); used to look up the DAT_/PTR_/LAB_ addresses of its output.
GHIDRA = (('.text', 0x00100000), ('.rodata', 0x00281130), ('.rodata.str1.1', 0x0058a130),
          ('.data', 0x0058eb60), ('.bss', 0x006df380))


def from_ghidra(b, addr):
    """Flat address (as used by this module) of a Ghidra address."""
    for sec, base in GHIDRA:
        if base <= addr < base + max(b.size[sec], 1):
            return BASES[sec] + addr - base
    return None


def resolve_data(b, what):
    """Address of 'symbol', 'symbol+0x10', '.rodata+0x1b00' or a Ghidra address."""
    off = 0
    if '+' in what:
        what, o = what.rsplit('+', 1)
        off = int(o, 0)
    if what in BASES:
        return BASES[what] + off
    if what in b.byname:
        return b.byname[what][0].addr + off
    a = from_ghidra(b, int(what, 16))
    if a is None:
        raise ValueError('%s is neither a symbol nor a Ghidra address' % what)
    return a + off


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--blob', default=default_path())
    ap.add_argument('cmd', choices=['funcs', 'dis', 'callers', 'callees', 'strings', 'stats',
                                    'addr', 'data'])
    ap.add_argument('args', nargs='*')
    ns = ap.parse_args()
    b = Blob(ns.blob)
    tb = BASES['.text']
    if ns.cmd == 'addr':
        # what is at a Ghidra address (DAT_0058eea0 -> 0058eea0)
        for a in ns.args:
            flat = from_ghidra(b, int(a.replace('DAT_', '').replace('PTR_', ''), 16))
            if flat is None:
                print('%s: not in the image' % a)
                continue
            b.discover_functions()
            sec = b.section_of(flat)
            r = b.relocs.get(flat)
            print('%s: %s+%#x  %s%s' % (a, sec, flat - BASES[sec], b.describe(flat),
                                        '  -> ' + b.describe(r[1]) if r else ''))
        sys.exit(0)
    if ns.cmd == 'data':
        # data WHAT [COUNT [WIDTH]]: values at a symbol / section+offset / Ghidra address
        flat = resolve_data(b, ns.args[0])
        count = int(ns.args[1], 0) if len(ns.args) > 1 else 16
        width = int(ns.args[2], 0) if len(ns.args) > 2 else 1
        s = b.sym_at(flat)
        print('; %s (%s+%#x)%s' % (b.describe(flat), b.section_of(flat),
                                   flat - BASES[b.section_of(flat)],
                                   ', symbol size %d' % s.size if s else ''))
        raw = b.read(flat, count * width)
        per = {1: 16, 2: 8, 4: 8, 8: 4}.get(width, 4)
        for i in range(0, count, per):
            vals = []
            for k in range(i, min(i + per, count)):
                p = flat + k * width
                r = b.relocs.get(p)
                if r is not None and width == 8:
                    vals.append(b.describe(r[1]))
                else:
                    vals.append('%0*x' % (2 * width,
                                          int.from_bytes(raw[k * width:(k + 1) * width], 'little')))
            print('%6x: %s' % (i * width, ' '.join(vals)))
        sys.exit(0)
    b.discover_functions()
    if ns.cmd == 'stats':
        named = sum(1 for f in b.funcs if f.kind == 'F')
        print('functions: %d (%d named, %d discovered)' % (len(b.funcs), named, len(b.funcs) - named))
        print('insns: %d, jump tables: %d' % (len(b.sweep()), len(b.jump_tables)))
    elif ns.cmd == 'funcs':
        pat = ns.args[0] if ns.args else ''
        for f in b.funcs:
            if pat in f.name:
                print('%06x %6d %s' % (f.addr - tb, f.size, f.name))
    elif ns.cmd == 'dis':
        for n in ns.args:
            f = b.func(n)
            if f is None:
                print('no such function: %s' % n)
                continue
            b.disasm(f)
            print()
    elif ns.cmd in ('callees', 'strings'):
        for n in ns.args:
            f = b.func(n)
            calls, data = b.refs_of(f)
            if ns.cmd == 'callees':
                for t in sorted(set(calls)):
                    print(b.describe(t))
            else:
                for t in data:
                    if b.section_of(t) == '.rodata.str1.1':
                        print(repr(b.cstring(t)))
    elif ns.cmd == 'callers':
        want = set()
        for n in ns.args:
            if n in b.imports:
                want.add(b.imports[n])
            else:
                want.add(b.func(n).addr)
        for f in b.funcs:
            calls, data = b.refs_of(f)
            if want & set(calls) or want & set(data):
                print(f.name)
