#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Run code of wlc_hybrid.o_shipped under Unicorn.

The object reaches the outside world only through its 85 imports: the OS
layer (osl_*), the Linux glue (wl_*) and - through osl_read*/osl_write* and
osl_pci_*_config - the hardware.  Here all of them are Python, so any function
of the object can be executed and everything it does to the hardware can be
recorded.  Nothing is patched in the object; it is loaded as the kernel
linker would load it (see blob.py).

    from emu import Machine
    m = Machine()
    print(m.call('bcm_strtoul', m.cstr('0x1234'), 0, 0))

An import handler is a Python function taking the machine and the integer
arguments.  It returns the result, or - when it has to call back into the
object, as the glue's wl_up() calls wlc_up() - it is a generator:

    def wl_up(m, wl):
        err = yield Call('wlc_up', m.r64(wl))
        return err
"""
import struct
import sys

import unicorn as uc
from unicorn import x86_const as x86

from blob import Blob, BASES, IMPORT_BASE, IMPORT_STEP, default_path

PAGE = 0x1000
HEAP_BASE = 0x10000000
HEAP_SIZE = 0x10000000
STACK_TOP = 0x7f100000
STACK_SIZE = 0x100000
# Every call made from Python returns to STOP_ADDR, where the emulation ends.
MAGIC_BASE = 0x05000000
STOP_ADDR = MAGIC_BASE
RET_GADGET = MAGIC_BASE + 0x10

ARG_REGS = (x86.UC_X86_REG_RDI, x86.UC_X86_REG_RSI, x86.UC_X86_REG_RDX,
            x86.UC_X86_REG_RCX, x86.UC_X86_REG_R8, x86.UC_X86_REG_R9)

M64 = 0xffffffffffffffff


def align_up(v, a):
    return (v + a - 1) & ~(a - 1)


class EmuError(Exception):
    pass


class Call:
    """Yielded by an import handler to call back into the object."""

    def __init__(self, target, *args):
        self.target = target
        self.args = args


class Machine:
    def __init__(self, path=None, blob=None):
        self.blob = blob or Blob(path or default_path())
        self.mu = uc.Uc(uc.UC_ARCH_X86, uc.UC_MODE_64)
        self.handlers = {}       # import name -> python callable(machine, *args)
        self.nargs = {}          # import name -> number of integer arguments
        self.import_calls = {}   # statistics
        self.heap_next = HEAP_BASE
        self.allocs = {}         # address -> size
        self._frames = []        # suspended import handlers (generators)
        self._returns = {}       # return address -> [(stack pointer after the return, function)]
        self._pending = None
        self._running = False
        self._map_image()
        self._install_default_handlers()

    # ------------------------------------------------------------ memory
    def _map_image(self):
        mu = self.mu
        b = self.blob
        for name, base in BASES.items():
            size = align_up(max(b.size[name], 1), PAGE)
            mu.mem_map(base, size)
            mu.mem_write(base, bytes(b.data[name]))
        nimp = len(b.imports)
        isize = align_up(IMPORT_STEP * nimp + PAGE, PAGE)
        mu.mem_map(IMPORT_BASE, isize)
        mu.mem_write(IMPORT_BASE, b'\xc3' * isize)          # ret
        mu.mem_map(MAGIC_BASE, PAGE)
        mu.mem_write(MAGIC_BASE, b'\xf4' * PAGE)            # hlt
        mu.mem_write(RET_GADGET, b'\xc3')
        mu.mem_map(HEAP_BASE, HEAP_SIZE)
        mu.mem_map(STACK_TOP - STACK_SIZE, STACK_SIZE)
        # imported data objects
        if 'g_assert_type' in b.imports:
            mu.mem_write(b.imports['g_assert_type'], b'\0' * IMPORT_STEP)
        mu.hook_add(uc.UC_HOOK_CODE, self._on_import, None, IMPORT_BASE,
                    IMPORT_BASE + IMPORT_STEP * nimp - 1)
        mu.hook_add(uc.UC_HOOK_MEM_READ_UNMAPPED | uc.UC_HOOK_MEM_WRITE_UNMAPPED |
                    uc.UC_HOOK_MEM_FETCH_UNMAPPED, self._on_unmapped)

    def malloc(self, size, align=16, fill=None):
        a = align_up(self.heap_next, align)
        self.heap_next = a + max(size, 1)
        if self.heap_next > HEAP_BASE + HEAP_SIZE:
            raise EmuError('emulated heap exhausted')
        self.allocs[a] = size
        if fill is not None and size:
            self.mu.mem_write(a, bytes([fill]) * size)
        return a

    def free(self, addr):
        self.allocs.pop(addr, None)

    def read(self, addr, n):
        return bytes(self.mu.mem_read(addr, n))

    def write(self, addr, data):
        self.mu.mem_write(addr, bytes(data))

    def r8(self, a):
        return self.read(a, 1)[0]

    def r16(self, a):
        return struct.unpack('<H', self.read(a, 2))[0]

    def r32(self, a):
        return struct.unpack('<I', self.read(a, 4))[0]

    def r64(self, a):
        return struct.unpack('<Q', self.read(a, 8))[0]

    def w8(self, a, v):
        self.write(a, struct.pack('<B', v & 0xff))

    def w16(self, a, v):
        self.write(a, struct.pack('<H', v & 0xffff))

    def w32(self, a, v):
        self.write(a, struct.pack('<I', v & 0xffffffff))

    def w64(self, a, v):
        self.write(a, struct.pack('<Q', v & M64))

    def cstr(self, s):
        """Copy a Python string into the emulated heap, return its address."""
        if isinstance(s, str):
            s = s.encode('latin1')
        a = self.malloc(len(s) + 1, 1)
        self.write(a, s + b'\0')
        return a

    def buf(self, data_or_size):
        if isinstance(data_or_size, int):
            a = self.malloc(data_or_size)
            self.write(a, b'\0' * data_or_size)
            return a
        a = self.malloc(len(data_or_size))
        self.write(a, data_or_size)
        return a

    def string(self, addr, limit=4096):
        out = bytearray()
        while len(out) < limit:
            chunk = self.read(addr + len(out), 64)
            i = chunk.find(b'\0')
            if i >= 0:
                out += chunk[:i]
                break
            out += chunk
        return out.decode('latin1')

    # ------------------------------------------------------------ calling
    def resolve(self, f):
        if isinstance(f, int):
            return f
        b = self.blob
        if f in b.byname:
            cands = [s for s in b.byname[f] if s.kind == 'F']
            if cands:
                return cands[0].addr
        if f.startswith('0x'):
            return BASES['.text'] + int(f, 16)
        if f.startswith('sub_'):
            return BASES['.text'] + int(f[4:], 16)
        raise EmuError('unknown function %r' % (f,))

    def _enter(self, target, args):
        """Set up a call frame that returns to STOP_ADDR; gives the entry pc."""
        mu = self.mu
        sp = mu.reg_read(x86.UC_X86_REG_RSP)
        nstack = max(0, len(args) - 6)
        sp -= 8 * nstack
        # keep the ABI's alignment: rsp + 8 is a multiple of 16 at function entry
        sp &= ~0xf
        for i, v in enumerate(args[6:]):
            self.w64(sp + 8 * i, v & M64)
        sp -= 8
        self.w64(sp, STOP_ADDR)
        mu.reg_write(x86.UC_X86_REG_RSP, sp)
        for reg, v in zip(ARG_REGS, args):
            mu.reg_write(reg, v & M64)
        mu.reg_write(x86.UC_X86_REG_RAX, 0)
        return self.resolve(target)

    def call(self, f, *args, max_insns=0, timeout=0):
        """Call a function of the object with integer/pointer arguments."""
        if self._running:
            raise EmuError('Machine.call() used inside an import handler; yield Call() instead')
        mu = self.mu
        mu.reg_write(x86.UC_X86_REG_RSP, STACK_TOP - 0x1000)
        mu.reg_write(x86.UC_X86_REG_RBP, 0)
        for waiting in self._returns.values():
            del waiting[:]      # left over by a call that ended with an error
        pc = self._enter(f, args)
        self._running = True
        try:
            self._run(pc, max_insns, timeout)
        finally:
            self._running = False
            self._frames = []
        return mu.reg_read(x86.UC_X86_REG_RAX)

    def _run(self, pc, max_insns, timeout):
        mu = self.mu
        while True:
            self._pending = None
            try:
                mu.emu_start(pc, STOP_ADDR, timeout=timeout, count=max_insns)
            except uc.UcError as e:
                if self._pending is None or self._pending[0] != 'error':
                    rip = mu.reg_read(x86.UC_X86_REG_RIP)
                    raise EmuError('%s at %s\n%s' % (e, self.where(rip), self.backtrace()))
            rip = mu.reg_read(x86.UC_X86_REG_RIP)
            if self._pending is not None:
                kind, val = self._pending
                if kind == 'error':
                    raise val
                # 'call': an import handler calls back into the object.  rip is at
                # the import stub, rsp at the return address of the import call.
                pc = self._enter(val.target, val.args)
                continue
            if rip != STOP_ADDR:
                raise EmuError('emulation stopped at %s (limit reached?)\n%s'
                               % (self.where(rip), self.backtrace()))
            if not self._frames:
                return
            # a nested call returned: resume the handler that made it
            gen, saved_sp = self._frames[-1]
            mu.reg_write(x86.UC_X86_REG_RSP, saved_sp)
            ret = mu.reg_read(x86.UC_X86_REG_RAX)
            try:
                req = gen.send(ret)
            except StopIteration as e:
                self._frames.pop()
                mu.reg_write(x86.UC_X86_REG_RAX, (e.value or 0) & M64)
                pc = RET_GADGET         # return to the caller of the import
                continue
            pc = self._enter(req.target, req.args)

    # ------------------------------------------------------------ imports
    def _on_import(self, mu, address, size, user):
        name = self.blob.import_at.get(address)
        if name is None:
            return
        self.import_calls[name] = self.import_calls.get(name, 0) + 1
        h = self.handlers.get(name)
        if h is None:
            self._pending = ('error', EmuError('import %s is not implemented (called from %s)'
                                               % (name, self.caller())))
            mu.emu_stop()
            return
        n = self.nargs.get(name, 6)
        args = [mu.reg_read(r) for r in ARG_REGS[:n]]
        try:
            r = h(self, *args)
            if hasattr(r, 'send'):
                # generator: the handler wants to call back into the object
                try:
                    req = next(r)
                except StopIteration as e:
                    r = e.value
                else:
                    self._frames.append((r, mu.reg_read(x86.UC_X86_REG_RSP)))
                    self._pending = ('call', req)
                    mu.emu_stop()
                    return
        except EmuError as e:
            self._pending = ('error', e)
            mu.emu_stop()
            return
        except Exception as e:      # bug in a model: report it with context
            import traceback
            self._pending = ('error', EmuError('handler %s failed: %s\n%s'
                                               % (name, e, traceback.format_exc())))
            mu.emu_stop()
            return
        mu.reg_write(x86.UC_X86_REG_RAX, (r or 0) & M64)

    def record_coverage(self):
        """Collect the basic blocks of .text that get executed from now on."""
        self.blocks = {}
        tb = BASES['.text']

        def hook(mu, address, size, user):
            self.blocks[address] = size

        self.mu.hook_add(uc.UC_HOOK_BLOCK, hook, None, tb, tb + self.blob.size['.text'] - 1)

    def covered_functions(self):
        """Function -> bytes executed, for the blocks seen by record_coverage()."""
        b = self.blob
        if b.funcs is None:
            b.discover_functions()
        out = {}
        for a, size in self.blocks.items():
            f = b.func_at(a)
            if f is not None:
                out[f] = out.get(f, 0) + size
        return out

    def on_call(self, f, fn, nargs=6):
        """Call fn(machine, *args) whenever the function f of the object is entered."""
        addr = self.resolve(f)

        def hook(mu, address, size, user):
            try:
                fn(self, *[mu.reg_read(r) for r in ARG_REGS[:nargs]])
            except Exception as e:
                import traceback
                self._pending = ('error', EmuError('hook on %s failed: %s\n%s'
                                                   % (f, e, traceback.format_exc())))
                mu.emu_stop()

        h = self.mu.hook_add(uc.UC_HOOK_CODE, hook, None, addr, addr)
        # code that was translated before does not know the hook
        self.mu.ctl_remove_cache(addr, addr + 1)
        return h

    def at_return(self, fn):
        """For a hook at the entry of a function (on_call): call
        fn(machine, result) when this call of the function returns.

        Not for a function that was called from Python directly (its return
        ends the emulation).
        """
        mu = self.mu
        rsp = mu.reg_read(x86.UC_X86_REG_RSP)
        ret = self.r64(rsp)
        waiting = self._returns.get(ret)
        if waiting is None:
            waiting = self._returns[ret] = []
            if ret != STOP_ADDR:
                mu.hook_add(uc.UC_HOOK_CODE, self._on_return, None, ret, ret)
                # code that was translated before does not know the hook
                mu.ctl_remove_cache(ret, ret + 1)
        waiting.append((rsp + 8, fn))

    def _on_return(self, mu, address, size, user):
        waiting = self._returns.get(address)
        # the stack pointer tells the return of this call from an execution
        # of the same address further up or down the stack
        if not waiting or waiting[-1][0] != mu.reg_read(x86.UC_X86_REG_RSP):
            return
        fn = waiting.pop()[1]
        try:
            fn(self, mu.reg_read(x86.UC_X86_REG_RAX))
        except Exception as e:
            import traceback
            self._pending = ('error', EmuError('hook at the return to %s failed: %s\n%s'
                                               % (self.where(address), e,
                                                  traceback.format_exc())))
            mu.emu_stop()

    def define(self, name, nargs, fn):
        if name not in self.blob.imports:
            raise EmuError('%s is not an import of the object' % name)
        self.handlers[name] = fn
        self.nargs[name] = nargs

    def _on_unmapped(self, mu, access, address, size, value, user):
        rip = mu.reg_read(x86.UC_X86_REG_RIP)
        kind = {uc.UC_MEM_READ_UNMAPPED: 'read', uc.UC_MEM_WRITE_UNMAPPED: 'write',
                uc.UC_MEM_FETCH_UNMAPPED: 'fetch'}.get(access, str(access))
        self._pending = ('error', EmuError('unmapped %s of %#x (size %d) at %s\n%s'
                                           % (kind, address, size, self.where(rip),
                                              self.backtrace())))
        return False

    # ------------------------------------------------------------ diagnostics
    def where(self, addr):
        b = self.blob
        if b.funcs is None:
            b.discover_functions()
        if BASES['.text'] <= addr < BASES['.text'] + b.size['.text']:
            return '%s (.text+%#x)' % (b.describe(addr), addr - BASES['.text'])
        if addr in b.import_at:
            return 'import ' + b.import_at[addr]
        return '%#x' % addr

    def caller(self):
        sp = self.mu.reg_read(x86.UC_X86_REG_RSP)
        return self.where(self.r64(sp))

    def return_addresses(self, limit=48):
        """Return addresses up the stack, innermost first.

        The object is built with frame pointers, so this is a plain walk of
        the rbp chain.  The frame of the function that is executing right now
        is complete once it has run its prologue, which is the case whenever
        an import is called.
        """
        out = []
        mu = self.mu
        rbp = mu.reg_read(x86.UC_X86_REG_RBP)
        lo, hi = STACK_TOP - STACK_SIZE, STACK_TOP
        while lo <= rbp < hi - 16 and len(out) < limit:
            ret = self.r64(rbp + 8)
            if ret != STOP_ADDR:
                out.append(ret)
            nxt = self.r64(rbp)
            if nxt <= rbp:
                break
            rbp = nxt
        return tuple(out)

    def backtrace(self, limit=32):
        return '\n'.join('    from ' + self.where(a) for a in self.return_addresses(limit))

    # ------------------------------------------------------------ variadic helpers
    def varargs(self, fixed):
        """Iterator over the variadic integer arguments of the current import."""
        mu = self.mu
        regs = [mu.reg_read(r) for r in ARG_REGS]
        sp = mu.reg_read(x86.UC_X86_REG_RSP)
        i = fixed
        while True:
            if i < 6:
                yield regs[i]
            else:
                yield self.r64(sp + 8 + 8 * (i - 6))
            i += 1

    def va_list(self, ap):
        """Iterator over the integer arguments of a va_list."""
        while True:
            gp = self.r32(ap)
            if gp < 48:
                v = self.r64(self.r64(ap + 16) + gp)
                self.w32(ap, gp + 8)
            else:
                ov = self.r64(ap + 8)
                v = self.r64(ov)
                self.w64(ap + 8, ov + 8)
            yield v

    def format(self, fmt, args):
        """printf formatting as the kernel does it (no floating point)."""
        out = []
        i = 0
        n = len(fmt)
        while i < n:
            c = fmt[i]
            if c != '%':
                out.append(c)
                i += 1
                continue
            i += 1
            if i < n and fmt[i] == '%':
                out.append('%')
                i += 1
                continue
            flags = ''
            while i < n and fmt[i] in '-+ #0':
                flags += fmt[i]
                i += 1
            width = ''
            if i < n and fmt[i] == '*':
                width = str(s32(next(args)))
                i += 1
            while i < n and fmt[i].isdigit():
                width += fmt[i]
                i += 1
            prec = None
            if i < n and fmt[i] == '.':
                i += 1
                prec = ''
                if i < n and fmt[i] == '*':
                    prec = str(s32(next(args)))
                    i += 1
                while i < n and fmt[i].isdigit():
                    prec += fmt[i]
                    i += 1
            size = ''
            while i < n and fmt[i] in 'hlLqzjt':
                size += fmt[i]
                i += 1
            if i >= n:
                break
            conv = fmt[i]
            i += 1
            spec = '%' + flags + width + ('.' + prec if prec is not None else '')
            if conv in 'di':
                v = next(args)
                if size in ('l', 'll', 'q', 'z', 'j', 't'):
                    v = s64(v)
                elif size == 'h':
                    v = s16(v)
                elif size == 'hh':
                    v = s8(v)
                else:
                    v = s32(v)
                out.append((spec + 'd') % v)
            elif conv in 'uxXo':
                v = next(args)
                if size in ('l', 'll', 'q', 'z', 'j', 't'):
                    v &= M64
                elif size == 'h':
                    v &= 0xffff
                elif size == 'hh':
                    v &= 0xff
                else:
                    v &= 0xffffffff
                out.append((spec + ('d' if conv == 'u' else conv)) % v)
            elif conv == 'c':
                out.append((spec + 'c') % chr(next(args) & 0xff))
            elif conv == 's':
                p = next(args)
                s = self.string(p) if p else '(null)'
                out.append((spec + 's') % s)
            elif conv == 'p':
                out.append('%#x' % next(args))
            else:
                out.append('%' + conv)
        return ''.join(out)

    # ------------------------------------------------------------ default OS layer
    def _install_default_handlers(self):
        from osl import install
        install(self)


def s8(v):
    v &= 0xff
    return v - 0x100 if v & 0x80 else v


def s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xffffffff
    return v - 0x100000000 if v & 0x80000000 else v


def s64(v):
    v &= M64
    return v - (1 << 64) if v & (1 << 63) else v


if __name__ == '__main__':
    m = Machine()
    checks = [
        ('bcm_strtoul("0x1234")', m.call('bcm_strtoul', m.cstr('0x1234'), 0, 0), 0x1234),
        ('bcm_atoi("4360")', m.call('bcm_atoi', m.cstr('4360')), 4360),
        ('bcm_bitcount({0xff,0x01})', m.call('bcm_bitcount', m.buf(b'\xff\x01'), 2), 9),
        # the start factor is in units of 500 kHz (WF_CHAN_FACTOR_5_G)
        ('wf_channel2mhz(36, 10000)', m.call('wf_channel2mhz', 36, 10000), 5180),
        ('wf_mhz2channel(2437, 0)', s32(m.call('wf_mhz2channel', 2437, 0)), 6),
        ('hndcrc32("123456789")', m.call('hndcrc32', m.cstr('123456789'), 9, 0xffffffff)
         & 0xffffffff, 0xffffffff ^ 0xcbf43926),
    ]
    bad = 0
    for name, got, want in checks:
        ok = got == want
        bad += not ok
        print('%-32s = %#x %s' % (name, got, 'ok' if ok else 'EXPECTED %#x' % want))
    sys.exit(1 if bad else 0)
