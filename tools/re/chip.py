#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Hardware model for emu.Machine: a PCIe card with a Broadcom AXI backplane.

This is not a simulator of the radio.  It models what the driver needs to get
through attach/up/init - chip identification, the enumeration ROM, reset and
clock state, PMU resources, SROM/OTP contents, the indirect PHY/radio/table
and shared-memory accesses - and records every access, so that the sequence
of register writes the driver performs can be studied and compared.

Values that come from a real card (SROM contents, chip straps) are taken from
a dump when one is given, see Bcm4360.from_dump(); the built-in defaults are
plausible, not measured.
"""
import struct

from unicorn import x86_const as x86

from emu import EmuError, STOP_ADDR

BAR0_VA = 0x60000000
# where elfobj.py puts the stubs of functions that open code imports from Python
STUB_BASE = 0x05800000
STUB_SIZE = 0x10000
BAR0_SIZE = 0x8000
BAR1_VA = 0x61000000
BAR1_SIZE = 0x200000

# PCI configuration registers of the Broadcom PCI(e) function
PCI_BAR0_WIN = 0x80          # backplane address seen at BAR0 + 0
PCI_BAR1_WIN = 0x84
PCI_SPROM_CONTROL = 0x88
PCI_BAR1_CONTROL = 0x8c
PCI_INT_STATUS = 0x90
PCI_INT_MASK = 0x94
PCI_TO_SB_MB = 0x98
PCI_BACKPLANE_ADDR = 0xa0
PCI_BACKPLANE_DATA = 0xa4
PCI_CLK_CTL_ST = 0xa8
PCI_BAR0_WIN2 = 0xac         # backplane address seen at BAR0 + 0x1000
PCIE2_BAR0_WIN2 = 0x70       # the same, behind a PCIe Gen2 core
PCI_GPIO_IN = 0xb0
PCI_GPIO_OUT = 0xb4
PCI_GPIO_OUTEN = 0xb8

# fixed windows inside BAR0
BAR0_WIN2_OFF = 0x1000
BAR0_PCIE_OFF = 0x2000
BAR0_CC_OFF = 0x3000

# wrapper (DMP) registers of an AXI core
AI_IOCTRL = 0x408
AI_IOSTATUS = 0x500
AI_RESETCTRL = 0x800
AI_RESETSTATUS = 0x804

# clock control and status, the same register in every core
CLK_CTL_ST = 0x1e0
CCS_FORCEALP = 0x00000001
CCS_FORCEHT = 0x00000002
CCS_HTAREQ = 0x00000010
CCS_ALPAVAIL = 0x00010000
CCS_HTAVAIL = 0x00020000
CCS_BP_ON_ALP = 0x00040000
CCS_BP_ON_HT = 0x00080000

SICF_CLOCK_EN = 0x0001
SICF_FGC = 0x0002
AIRC_RESET = 1


class Access:
    """One recorded hardware access."""
    __slots__ = ('seq', 'op', 'space', 'addr', 'size', 'value', 'pc', 'time', 'bt')

    def __init__(self, seq, op, space, addr, size, value, pc, time, bt=()):
        self.seq, self.op, self.space = seq, op, space
        self.addr, self.size, self.value = addr, size, value
        self.pc, self.time, self.bt = pc, time, bt

    def __repr__(self):
        return '%s %-10s %06x/%d = %08x' % (self.op, self.space, self.addr, self.size, self.value)


class Core:
    """A core on the backplane: a bank of registers with optional behaviour."""
    name = 'core'
    coreid = 0
    rev = 0
    mfg = 0x4bf
    cls = 0
    nmw = 0          # master wrappers
    nsw = 1          # slave wrappers
    nmp = 0          # master ports
    size = 0x1000

    def __init__(self, chip, base, wrap):
        self.chip = chip
        self.base = base
        self.wrap = wrap
        self.regs = {}           # offset -> value (32 bit storage, little endian)
        self.ioctrl = 0
        self.iostatus = 0
        self.resetctrl = AIRC_RESET
        self.unknown = set()
        self.reset()

    def reset(self):
        pass

    # --- storage helpers: registers are kept in 32 bit cells
    def peek(self, off, size=4):
        cell = self.regs.get(off & ~3, 0)
        sh = 8 * (off & 3)
        return (cell >> sh) & ((1 << (8 * size)) - 1)

    def poke(self, off, size, value):
        a = off & ~3
        sh = 8 * (off & 3)
        mask = ((1 << (8 * size)) - 1) << sh
        self.regs[a] = (self.regs.get(a, 0) & ~mask) | ((value << sh) & mask)

    @property
    def in_reset(self):
        return bool(self.resetctrl & AIRC_RESET)

    @property
    def clocked(self):
        return bool(self.ioctrl & SICF_CLOCK_EN)

    # --- bus interface
    def read(self, off, size):
        return self.peek(off, size)

    def write(self, off, size, value):
        self.poke(off, size, value)

    def wrap_read(self, off, size):
        if off == AI_IOCTRL:
            return self.ioctrl
        if off == AI_IOSTATUS:
            return self.iostatus
        if off == AI_RESETCTRL:
            return self.resetctrl
        if off == AI_RESETSTATUS:
            return 0
        return 0

    def wrap_write(self, off, size, value):
        if off == AI_IOCTRL:
            self.ioctrl = value
            self.ioctrl_changed()
        elif off == AI_RESETCTRL:
            was = self.in_reset
            self.resetctrl = value
            if value & AIRC_RESET and not was:
                self.regs = {}
                self.reset()

    def ioctrl_changed(self):
        pass

    def tick(self, now_us):
        pass


def cores_from_erom(words):
    """Core list of an enumeration ROM: (core id, revision, base, wrapper)."""
    out = []
    i = 0
    n = len(words)

    def descriptor(i, port, kind):
        """Address descriptor at i for a port and kind: (address, next i) or None."""
        if i >= n:
            return None
        w = words[i]
        if w & 1 == 0 or w & 6 != 4 or (w >> 8) & 0xf != port or w & 0xc0 != kind:
            return None
        i += 1
        if w & 8:
            i += 1                  # upper half of a 64 bit address
        if w & 0x30 == 0x30:
            size = words[i]
            i += 2 if size & 8 else 1
        return w & 0xfffff000, i

    while i + 1 < n:
        cia = words[i]
        if cia == 0xf:
            break
        if cia & 1 == 0 or cia & 0xe != 0:
            i += 1
            continue
        cib = words[i + 1]
        i += 2
        coreid = (cia >> 8) & 0xfff
        nmp = (cib >> 4) & 0x1f
        nsp = (cib >> 9) & 0x1f
        nmw = (cib >> 14) & 0x1f
        nsw = (cib >> 19) & 0x1f
        i += nmp
        base = wrap = None
        for port in range(nsp):
            k = 0
            while True:
                d = descriptor(i, port, 0x00) or (descriptor(i, port, 0x40) if k == 0 else None)
                if d is None:
                    break
                if port == 0 and k == 0:
                    base = d[0]
                i = d[1]
                k += 1
        for port in range(nmw):
            d = descriptor(i, port, 0xc0)
            if d is None:
                break
            if port == 0:
                wrap = d[0]
            i = d[1]
        for k in range(nsw):
            d = descriptor(i, k + (0 if nsp == 1 else 1), 0x80)
            if d is None:
                break
            if k == 0 and nmw == 0:
                wrap = d[0]
            i = d[1]
        if base is not None and (nmw or nsw):
            out.append((coreid, cib >> 24, base, wrap))
    return out


class Erom:
    """Enumeration ROM, built from the core list (format: see aiutils)."""
    ER_VALID = 1
    ER_CI = 0
    ER_MP = 2
    ER_ADD = 4
    ER_END = 0xe
    AD_ST_SLAVE = 0x00
    AD_ST_BRIDGE = 0x40
    AD_ST_SWRAP = 0x80
    AD_ST_MWRAP = 0xc0
    AD_SZ_4K = 0x00

    def __init__(self, cores):
        w = []
        for c in cores:
            nsp = 1
            cia = (c.mfg << 20) | (c.coreid << 8) | (c.cls << 4) | self.ER_CI | self.ER_VALID
            cib = (c.rev << 24) | (c.nsw << 19) | (c.nmw << 14) | (nsp << 9) | (c.nmp << 4) \
                | self.ER_CI | self.ER_VALID
            w += [cia, cib]
            for i in range(c.nmp):
                w.append((i << 8) | self.ER_MP | self.ER_VALID)
            w.append(c.base | (0 << 8) | self.AD_ST_SLAVE | self.AD_SZ_4K | self.ER_ADD
                     | self.ER_VALID)
            # the wrapper the driver uses is the first master wrapper, or the
            # first slave wrapper of a core without master wrappers
            wraps = iter(range(c.wrap, c.wrap + 0x1000 * (c.nmw + c.nsw), 0x1000))
            for i in range(c.nmw):
                w.append(next(wraps) | (i << 8) | self.AD_ST_MWRAP | self.AD_SZ_4K
                         | self.ER_ADD | self.ER_VALID)
            for i in range(c.nsw):
                sp = i + (0 if nsp == 1 else 1)
                w.append(next(wraps) | (sp << 8) | self.AD_ST_SWRAP | self.AD_SZ_4K
                         | self.ER_ADD | self.ER_VALID)
        w.append(self.ER_END | self.ER_VALID)
        self.words = w

    def read(self, off, size):
        i = off // 4
        v = self.words[i] if i < len(self.words) else 0
        return (v >> (8 * (off & 3))) & ((1 << (8 * size)) - 1)


class Chip:
    """PCI function + backplane.  Subclasses add the cores."""
    pci_bus = 3
    pci_slot = 0
    vendor = 0x14e4
    device = 0
    revision = 0
    subvendor = 0
    subdevice = 0
    erom_base = 0x180fe000      # overridden by the ChipCommon model if needed
    pcie_gen2 = True            # the bridge is a PCIe Gen2 core
    MAX_TRACE = 4000000         # accesses after which a session is taken for a runaway
    permissive = False          # accept accesses to addresses without a core

    def __init__(self, m):
        self.m = m
        m.hw = self
        self.cores = []
        self.trace = []
        self.trace_on = True
        self.verbose = False
        self.seq = 0
        self.unknown_reads = {}
        self.cfg = bytearray(4096)
        self.bar0_win = 0x18000000
        self.bar0_win2 = 0x18100000
        m.mu.mem_map(BAR1_VA, BAR1_SIZE)
        self.build()
        self.erom = Erom(self.cores)
        self.init_cfg()

    # ---- to be provided by the chip
    def build(self):
        raise NotImplementedError

    def init_cfg(self):
        c = self.cfg
        struct.pack_into('<HHHHBBBB', c, 0, self.vendor, self.device, 0x0006, 0x0010,
                         self.revision, 0, 0x80, 0x02)
        struct.pack_into('<I', c, 0x10, 0xb0600004)
        struct.pack_into('<I', c, 0x18, 0xb0400004)
        struct.pack_into('<HH', c, 0x2c, self.subvendor, self.subdevice)
        c[0x34] = 0x48
        c[0x3c] = 0x10
        c[0x3d] = 0x01
        if self.pcie_gen2:
            # capability list of a PCIe Gen2 card: power management, MSI,
            # vendor specific, PCI Express (at 0xac, where older bridges have
            # the register of the second window and the GPIO registers)
            struct.pack_into('<BBH', c, 0x48, 0x01, 0x58, 0xc803)
            struct.pack_into('<BBH', c, 0x58, 0x05, 0x68, 0x0080)
            struct.pack_into('<BBB', c, 0x68, 0x09, 0xac, 0x44)
            struct.pack_into('<BBH', c, 0xac, 0x10, 0x00, 0x0002)
            struct.pack_into('<I', c, 0xb0, 0x00008dc0)           # device capabilities
            struct.pack_into('<I', c, 0xb8, 0x0006fc12)           # link capabilities
            struct.pack_into('<HH', c, 0xbc, 0x0040, 0x1012)      # link control, status
            struct.pack_into('<I', c, 0x100, 0x13c10001)          # extended: AER, next 0x13c
            struct.pack_into('<I', c, 0x13c, 0x00000000)

    # ---- recording
    STUCK = 20000       # identical accesses in a row that count as a hang

    def record(self, op, space, addr, size, value):
        self.seq += 1
        # Called from an import (the return address into the calling code is
        # on top of the stack, the frame of the caller is complete) or from a
        # hook at the entry of a function (the function has no frame yet: its
        # caller is known by the return address on top of the stack only).
        rip = self.m.mu.reg_read(x86.UC_X86_REG_RIP)
        ret = self.m.r64(self.m.mu.reg_read(x86.UC_X86_REG_RSP))
        if rip in self.m.blob.import_at or STUB_BASE <= rip < STUB_BASE + STUB_SIZE:
            pc = ret
            first = ()
        else:
            pc = rip
            first = (ret,) if ret != STOP_ADDR else ()
        key = (op, space, addr, value, pc)
        if key == self._last:
            self._repeat += 1
            if self._repeat > self.STUCK:
                self._repeat = 0
                raise EmuError('driver is polling %s %s+%#x (= %#x) forever at %s\n%s'
                               % (op, space, addr, value, self.m.where(pc),
                                  self.m.backtrace()))
        else:
            self._last = key
            self._repeat = 0
        if self.trace_on:
            a = Access(self.seq, op, space, addr, size, value, pc, self.m.os.now_us,
                       first + self.m.return_addresses())
            self.trace.append(a)
            if len(self.trace) > self.MAX_TRACE:
                raise EmuError('more than %d accesses: runaway session, stopped at %s\n%s'
                               % (self.MAX_TRACE, self.m.where(pc), self.m.backtrace()))
            if self.verbose:
                print('%7d %s  <- %s' % (a.seq, a, self.m.where(pc)))

    _last = None
    _repeat = 0

    # ---- backplane
    def core_at(self, addr):
        for c in self.cores:
            if c.base <= addr < c.base + c.size:
                return c, addr - c.base, False
            if c.wrap <= addr < c.wrap + 0x1000:
                return c, addr - c.wrap, True
        return None, 0, False

    def bp_read(self, addr, size):
        if self.erom_base <= addr < self.erom_base + 0x1000:
            v = self.erom.read(addr - self.erom_base, size)
            self.record('R', 'erom', addr - self.erom_base, size, v)
            return v
        c, off, wrap = self.core_at(addr)
        if c is None:
            if self.permissive:
                self.record('R', 'bp', addr, size, 0)
                return 0
            self.record('R', 'bp?', addr, size, 0xffffffff)
            raise EmuError('read of unpopulated backplane address %#x from %s'
                           % (addr, self.m.caller()))
        if wrap:
            v = c.wrap_read(off, size)
            self.record('R', c.name + '.wrap', off, size, v)
        else:
            if off == CLK_CTL_ST and size == 4:
                v = self.clk_status(c.peek(CLK_CTL_ST))
            else:
                v = c.read(off, size) & ((1 << (8 * size)) - 1)
            self.record('R', c.name, off, size, v)
        return v

    def clk_status(self, value):
        """Clock control and status register of a core as it reads back.

        Every core has one at 0x1e0: requests in the low half, status in the
        upper half. The crystal (ALP) clock is always there; the PLL (HT)
        clock is there while a core asks for it.
        """
        v = (value & 0xffff) | CCS_ALPAVAIL | CCS_BP_ON_ALP
        if any(c.peek(CLK_CTL_ST) & (CCS_FORCEHT | CCS_HTAREQ) for c in self.cores):
            v |= CCS_HTAVAIL | CCS_BP_ON_HT
        return v

    def bp_write(self, addr, size, value):
        c, off, wrap = self.core_at(addr)
        if c is None:
            self.record('W', 'bp' if self.permissive else 'bp?', addr, size, value)
            if self.permissive:
                return
            raise EmuError('write to unpopulated backplane address %#x from %s'
                           % (addr, self.m.caller()))
        if wrap:
            self.record('W', c.name + '.wrap', off, size, value)
            c.wrap_write(off, size, value)
        else:
            self.record('W', c.name, off, size, value)
            c.write(off, size, value)

    def core(self, name):
        for c in self.cores:
            if c.name == name:
                return c
        raise KeyError(name)

    # ---- MMIO through BAR0
    def _bp_addr(self, va):
        off = va - BAR0_VA
        if not 0 <= off < BAR0_SIZE:
            raise EmuError('MMIO access outside BAR0: %#x (from %s)' % (va, self.m.caller()))
        if off < BAR0_WIN2_OFF:
            return self.bar0_win + off
        if off < BAR0_PCIE_OFF:
            return self.bar0_win2 + off - BAR0_WIN2_OFF
        if off < BAR0_CC_OFF:
            return self.core('pcie').base + off - BAR0_PCIE_OFF
        if off < 0x4000:
            return self.core('chipcommon').base + off - BAR0_CC_OFF
        raise EmuError('MMIO access to BAR0+%#x (from %s)' % (off, self.m.caller()))

    # Accesses as the bus sees them, (op, bar, offset, size, value): kept when
    # `raw` is a list.  With a `replay` object the values read come from a
    # recording of the real card instead of the model (see mmiotrace.py).
    raw = None
    replay = None

    def mmio_read(self, va, size):
        n0 = len(self.trace)
        if BAR1_VA <= va < BAR1_VA + BAR1_SIZE:
            bar, off = 1, va - BAR1_VA
            v = int.from_bytes(self.m.read(va, size), 'little')
            self.record('R', 'bar1', off, size, v)
        else:
            bar, off = 0, va - BAR0_VA
            v = self.bp_read(self._bp_addr(va), size)
        if self.replay is not None:
            real = self.replay.read(self, bar, off, size, v)
            if real != v:
                # the records this access produced get the value of the real card
                for a in self.trace[n0:]:
                    if a.op == 'R':
                        a.value = real
                v = real
        if self.raw is not None:
            self.raw.append(('R', bar, off, size, v))
        return v

    def mmio_write(self, va, size, value):
        if BAR1_VA <= va < BAR1_VA + BAR1_SIZE:
            bar, off = 1, va - BAR1_VA
        else:
            bar, off = 0, va - BAR0_VA
        if self.replay is not None:
            self.replay.write(self, bar, off, size, value)
        if self.raw is not None:
            self.raw.append(('W', bar, off, size, value))
        if bar == 1:
            self.record('W', 'bar1', off, size, value)
            self.m.write(va, value.to_bytes(size, 'little'))
            return 0
        self.bp_write(self._bp_addr(va), size, value)
        return 0

    def reg_map(self, pa, size):
        raise EmuError('osl_reg_map(%#x, %#x) on a PCI card (from %s)'
                       % (pa, size, self.m.caller()))

    def bar1(self, m, addr_ptr):
        m.w64(addr_ptr, BAR1_VA)
        return BAR1_SIZE

    def pcie_rc(self, op, param):
        return 0

    # ---- PCI configuration space
    def pci_read(self, off, size):
        if off == PCI_BAR0_WIN:
            v = self.bar0_win
        elif off == (PCIE2_BAR0_WIN2 if self.pcie_gen2 else PCI_BAR0_WIN2):
            v = self.bar0_win2
        else:
            v = self.cfg_read(off, size)
        v &= (1 << (8 * size)) - 1
        self.record('R', 'pcicfg', off, size, v)
        return v

    def cfg_read(self, off, size):
        return int.from_bytes(self.cfg[off:off + size], 'little')

    def pci_write(self, off, size, value):
        self.record('W', 'pcicfg', off, size, value)
        if off == PCI_BAR0_WIN:
            self.bar0_win = value & ~0xfff
        elif off == (PCIE2_BAR0_WIN2 if self.pcie_gen2 else PCI_BAR0_WIN2):
            self.bar0_win2 = value & ~0xfff
        elif self.pcie_gen2 and off == PCI_BAR0_WIN2:
            # Until it has identified the bridge the driver writes the window
            # register of older bridges. Behind a PCIe Gen2 core that is the
            # header of the PCI Express capability, which is read only.
            pass
        else:
            self.cfg_write(off, size, value)
        return 0

    def cfg_write(self, off, size, value):
        self.cfg[off:off + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')

    def tick(self, now_us):
        for c in self.cores:
            c.tick(now_us)

    # ---- reports
    def func_name(self, addr):
        b = self.m.blob
        if b.funcs is None:
            b.discover_functions()
        f = b.func_at(addr)
        return f.name if f is not None else '%#x' % addr

    def dump_trace(self, path, start=0):
        """One access per line: sequence, time (ms), access, where, call chain."""
        names = {}

        def name(a):
            if a not in names:
                names[a] = self.func_name(a)
            return names[a]

        with open(path, 'w') as f:
            for a in self.trace[start:]:
                f.write('%7d %9.3f %s  %s | %s\n' % (
                    a.seq, a.time / 1000.0, a, self.m.where(a.pc),
                    ' < '.join(name(x) for x in a.bt)))
