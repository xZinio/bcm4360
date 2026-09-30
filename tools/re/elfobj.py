#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Load a relocatable x86-64 ELF object (a compiled .o of the open code) into an
emu.Machine, next to Broadcom's object, so that both can run against the same
model of the hardware and be compared.

Undefined symbols are resolved against the imports the machine already
provides (osl_readl, osl_delay, ...), against the symbols of objects loaded
before, and against extra Python handlers given to load().
"""
import struct

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from elftools.elf.sections import SymbolTableSection

import unicorn as uc

from blob import IMPORT_STEP
from chip import STUB_BASE, STUB_SIZE   # import stubs for names Broadcom's object does not import
from emu import ARG_REGS, EmuError

PAGE = 0x1000
OBJ_BASE = 0x06000000          # objects are placed from here on

R_X86_64_64 = 1
R_X86_64_PC32 = 2
R_X86_64_PLT32 = 4
R_X86_64_32 = 10
R_X86_64_32S = 11
R_X86_64_PC64 = 24

SHF_ALLOC = 2


def align_up(v, a):
    return (v + a - 1) & ~(a - 1)


class Object:
    """A loaded object: symbol addresses and sizes."""

    def __init__(self, path):
        self.path = path
        self.symbols = {}       # name -> address
        self.sizes = {}         # name -> size
        self.sections = {}      # name -> (address, size)
        self.functions = []     # (address, size, name), sorted
        self.base = self.end = 0

    def func_at(self, addr):
        for a, size, name in self.functions:
            if a <= addr < a + max(size, 1):
                return name
        return None


TRAP_BASE = 0x05900000         # unresolved externals: trap when executed
TRAP_SIZE = 0x10000


class Loader:
    def __init__(self, machine):
        self.m = machine
        self.next = OBJ_BASE
        self.objects = []
        self.stubs = {}          # name -> address of a stub for a Python handler
        self.stub_at = {}
        self.handlers = {}
        self._stub_mapped = False
        self.traps = {}          # name -> address of a trap for an unresolved symbol
        self.trap_at = {}
        self._trap_mapped = False

    # ---- symbols that no object, import or handler provides: they load, but
    # calling one stops the emulation with a clear message.  A scenario that
    # does not exercise a part of the open code need not provide that part's
    # imports; only a part that is actually run must be complete.
    def _trap(self, name):
        if name in self.traps:
            return self.traps[name]
        m = self.m
        if not self._trap_mapped:
            m.mu.mem_map(TRAP_BASE, TRAP_SIZE)
            m.mu.mem_write(TRAP_BASE, b'\xc3' * TRAP_SIZE)
            m.mu.hook_add(uc.UC_HOOK_CODE, self._on_trap, None, TRAP_BASE,
                          TRAP_BASE + TRAP_SIZE - 1)
            self._trap_mapped = True
        a = TRAP_BASE + IMPORT_STEP * len(self.traps)
        self.traps[name] = a
        self.trap_at[a] = name
        return a

    def _on_trap(self, mu, address, size, user):
        name = self.trap_at.get(address)
        if name is None:
            return
        self.m._pending = ('error', EmuError(
            'the open code calls %s, which nothing provides in this scenario '
            '(an unresolved external of open/)' % name))
        mu.emu_stop()

    # ---- imports that Broadcom's object does not have
    def _stub(self, name):
        if name in self.stubs:
            return self.stubs[name]
        m = self.m
        if not self._stub_mapped:
            m.mu.mem_map(STUB_BASE, STUB_SIZE)
            m.mu.mem_write(STUB_BASE, b'\xc3' * STUB_SIZE)
            m.mu.hook_add(uc.UC_HOOK_CODE, self._on_stub, None, STUB_BASE,
                          STUB_BASE + STUB_SIZE - 1)
            self._stub_mapped = True
        a = STUB_BASE + IMPORT_STEP * len(self.stubs)
        self.stubs[name] = a
        self.stub_at[a] = name
        return a

    def _on_stub(self, mu, address, size, user):
        name = self.stub_at.get(address)
        if name is None:
            return
        fn, nargs = self.handlers[name]
        m = self.m
        try:
            r = fn(m, *[mu.reg_read(reg) for reg in ARG_REGS[:nargs]])
        except EmuError as e:
            m._pending = ('error', e)
            mu.emu_stop()
            return
        mu.reg_write(uc.x86_const.UC_X86_REG_RAX, (r or 0) & 0xffffffffffffffff)

    def define(self, name, nargs, fn):
        """Python implementation of a function the open code imports."""
        self.handlers[name] = (fn, nargs)
        self._stub(name)

    # ---- loading
    def resolve(self, name):
        m = self.m
        if name in self.handlers:
            return self.stubs[name]
        if name in m.blob.imports:
            return m.blob.imports[name]
        for o in reversed(self.objects):
            if name in o.symbols:
                return o.symbols[name]
        raise EmuError('undefined symbol %s' % name)

    def load_all(self, paths):
        """Load several objects that may refer to each other in any order.

        All objects are mapped and their symbols registered first, then the
        relocations are applied, so a reference from one object to a symbol of
        another that is loaded later resolves too (a plain ld would sort this
        out; here the objects come in alphabetical order).
        """
        pending = [self._map(p) for p in paths]
        objs = []
        for obj, relocs in pending:
            self._relocate(obj.path, relocs)
            objs.append(obj)
        return objs

    def load(self, path):
        obj, relocs = self._map(path)
        self._relocate(path, relocs)
        return obj

    def _map(self, path):
        """Place an object in memory and register its symbols; return the
        object and the list of relocations still to apply."""
        m = self.m
        obj = Object(path)
        relocs = []
        with open(path, 'rb') as f:
            elf = ELFFile(f)
            if elf['e_machine'] != 'EM_X86_64' or elf['e_type'] != 'ET_REL':
                raise EmuError('%s is not a relocatable x86-64 object' % path)
            secs = list(elf.iter_sections())
            addr = {}
            a = self.next
            obj.base = a
            for i, s in enumerate(secs):
                if not s['sh_flags'] & SHF_ALLOC or s['sh_size'] == 0:
                    continue
                a = align_up(a, max(s['sh_addralign'], 1))
                addr[i] = a
                obj.sections.setdefault(s.name, (a, s['sh_size']))
                a += s['sh_size']
            obj.end = align_up(a, PAGE)
            m.mu.mem_map(obj.base, max(obj.end - obj.base, PAGE))
            for i, s in enumerate(secs):
                if i in addr and s['sh_type'] != 'SHT_NOBITS':
                    m.mu.mem_write(addr[i], s.data())
            self.next = obj.end + PAGE

            symtab = [s for s in secs if isinstance(s, SymbolTableSection)][0]
            syms = list(symtab.iter_symbols())
            symaddr = []
            for sym in syms:
                shndx = sym['st_shndx']
                kind = sym['st_info']['type']
                if isinstance(shndx, int) and shndx in addr:
                    v = addr[shndx] + sym['st_value']
                    symaddr.append(v)
                    if sym.name and kind in ('STT_FUNC', 'STT_OBJECT', 'STT_NOTYPE'):
                        obj.symbols[sym.name] = v
                        obj.sizes[sym.name] = sym['st_size']
                        if kind == 'STT_FUNC':
                            obj.functions.append((v, sym['st_size'], sym.name))
                elif shndx == 'SHN_ABS':
                    symaddr.append(sym['st_value'])
                else:
                    symaddr.append(None)
            obj.functions.sort()
            self.objects.append(obj)

            for s in secs:
                if not isinstance(s, RelocationSection):
                    continue
                target = s['sh_info']
                if target not in addr:
                    continue
                base = addr[target]
                for r in s.iter_relocations():
                    si = r['r_info_sym']
                    relocs.append((base + r['r_offset'], r['r_info_type'], r['r_addend'],
                                   symaddr[si], syms[si].name))
        return obj, relocs

    def _relocate(self, path, relocs):
        m = self.m
        for P, t, A, S, name in relocs:
            if S is None:
                try:
                    S = self.resolve(name)
                except EmuError:
                    S = self._trap(name)
            if t == R_X86_64_64:
                m.write(P, struct.pack('<Q', (S + A) & 0xffffffffffffffff))
            elif t in (R_X86_64_PC32, R_X86_64_PLT32):
                m.write(P, struct.pack('<i', S + A - P))
            elif t == R_X86_64_32S:
                m.write(P, struct.pack('<i', S + A))
            elif t == R_X86_64_32:
                m.write(P, struct.pack('<I', S + A))
            elif t == R_X86_64_PC64:
                m.write(P, struct.pack('<q', S + A - P))
            else:
                raise EmuError('%s: relocation type %d is not supported '
                               '(compile with -fno-pic -mcmodel=kernel)' % (path, t))
