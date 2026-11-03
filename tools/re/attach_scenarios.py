#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
The attach and detach of the MAC layer (docs/re/spec/bmac-attach.md) in the
hybrid core with the open attach (group mac_attach of the MAC layer) and with
Broadcom's (hybrid.py attach): per case the driver is attached - and, when
that succeeds, brought up, set to a channel, down and detached - and compared:

* the result of wlc_attach and its error code,
* every live allocation of the emulated OS, byte for byte, after the attach
  and again after the detach (the pointers agree because both runs allocate
  in the same order; the only expected difference is the pointer to the
  transmit FIFO size table at wlc_hw+0x150, compared by its contents instead,
  and the backplane's interrupt callbacks, open functions in one run),
* every access to the card.

The cases follow the specification's Verification: the SROM/OTP cards of
hybrid.py srom, identities and board data changed at the entry of
wlc_bmac_attach (arguments, the card's variables, the backplane's fields),
and callee results replaced at their return (allocations, the PHY, the DMA
engines, the chip-access check, the LED state).

The emulator has only the object's static data, so the backplane fields are
those of the model; MAC revisions below 40 are refused by the open attach
(bmac_attach.h), and are left out.
"""
import struct

from unicorn import x86_const as x86

import ab
import hybrid

WLC_HW = 0x20
HW_XMTFIFO_SZ_TBL = 0x150
HW_SIH = 0xb8
PUB_SIH, PUB_VARS, PUB_VARS_SIZE = 0x100, 0x108, 0x110
SIH = {'boardtype': 0x28, 'boardvendor': 0x30, 'chip': 0x3c, 'chiprev': 0x40}
SUCCESS_STEPS = ['up', '36/80', '6', 'down', 'detach']


# --- changes made by a case ---------------------------------------------------

def at_entry(fn):
    """A patch: fn(m, wlc, rsp) at the entry of wlc_bmac_attach."""
    def patch(m):
        m.on_call('wlc_bmac_attach', lambda m, wlc, *a: fn(m, wlc,
                                                        m.mu.reg_read(x86.UC_X86_REG_RSP)))
    return patch


def regs(**values):
    """Arguments of wlc_bmac_attach: vendor, device (registers), bustype (stack)."""
    def fn(m, wlc, rsp):
        if 'vendor' in values:
            m.mu.reg_write(x86.UC_X86_REG_RSI, values['vendor'])
        if 'device' in values:
            m.mu.reg_write(x86.UC_X86_REG_RDX, values['device'])
        if 'bustype' in values:
            m.w32(rsp + 16, values['bustype'])
    return at_entry(fn)


def variables(**changes):
    """The card's variables: name=value (None removes the variable)."""
    def fn(m, wlc, rsp):
        pub = m.r64(wlc)
        vars_, size = m.r64(pub + PUB_VARS), m.r32(pub + PUB_VARS_SIZE)
        raw = m.read(vars_, size)
        entries = []
        for item in raw.split(b'\0'):
            if not item:
                break
            k, _, v = item.partition(b'=')
            entries.append([k.decode(), v])
        names = [k for k, _ in entries]
        for k, v in changes.items():
            if k in names:
                if v is None:
                    entries = [e for e in entries if e[0] != k]
                else:
                    entries[names.index(k)][1] = v.encode()
            elif v is not None:
                entries.insert(0, [k, v.encode()])
            names = [e[0] for e in entries]
        blob = b''.join(k.encode() + b'=' + v + b'\0' for k, v in entries) + b'\0'
        if len(blob) > size:
            # a buffer of its own (wlc_detach frees it with the size at pub+0x110)
            size = (len(blob) + 15) & ~15
            vars_ = m.malloc(size, fill=0)
            m.w64(pub + PUB_VARS, vars_)
            m.w32(pub + PUB_VARS_SIZE, size)
        m.write(vars_, blob + bytes(size - len(blob)))
    return at_entry(fn)


def backplane(**fields):
    def fn(m, wlc, rsp):
        sih = m.r64(m.r64(wlc) + PUB_SIH)
        for k, v in fields.items():
            m.w32(sih + SIH[k], v)
    return at_entry(fn)


def result(name, value, nth=1, when=None):
    """The nth call of name made after the entry of wlc_bmac_attach (and
    matching when(m, args)) returns value instead (an import of the core:
    its handler is not called)."""
    def patch(m):
        state = {'armed': False, 'n': 0}

        def arm(m, *a):
            state['armed'] = True

        def hit(m, args):
            if not state['armed'] or (when and not when(m, args)):
                return False
            state['n'] += 1
            return state['n'] == nth

        m.on_call('wlc_bmac_attach', arm)
        if name in m.blob.imports:
            orig = m.handlers[name]

            def handler(m, *args):
                if hit(m, args):
                    return value
                return orig(m, *args)
            m.handlers[name] = handler
        else:
            def call(m, *args):
                if hit(m, args):
                    m.at_return(lambda m, r: m.mu.reg_write(x86.UC_X86_REG_RAX, value))
            m.on_call(name, call)
    return patch


def cstring(m, addr):
    out = b''
    while len(out) < 64:
        c = m.read(addr + len(out), 1)
        if c == b'\0':
            break
        out += c
    return out.decode(errors='replace')


def both(*patches):
    def patch(m):
        for p in patches:
            p(m)
    return patch


def cases():
    out = [(label, card, None) for label, card in hybrid.srom_images()]
    v, b, r = variables, backplane, result
    out += [
        ('device 0x1234', {}, regs(device=0x1234)),
        ('vendor 0x106b', {}, regs(vendor=0x106b)),
        ('vendor 0x106b, bus type 0', {}, regs(vendor=0x106b, bustype=0)),
        ('vendid=0x106b', {}, v(vendid='0x106b')),
        ('devid=0x43a2 (5 GHz only)', {}, v(devid='0x43a2')),
        ('devid=0x4352 (5 GHz only)', {}, v(devid='0x4352')),
        ('devid=0x43a1 (one 2.4 GHz band)', {}, v(devid='0x43a1')),
        ('devid=0xffff (ignored)', {}, v(devid='0xffff')),
        ('device 0x43a2 (argument)', {}, regs(device=0x43a2)),
        ('MAC revision 6', {}, r('si_corerev', 6)),
        ('MAC revision 35', {}, r('si_corerev', 35)),
        ('MAC revision 41', {}, r('si_corerev', 41)),
        ('MAC revision 44', {}, r('si_corerev', 44)),
        ('chip 0x4311 rev 0', {}, b(chip=0x4311, chiprev=0)),
        ('chip-access check fails', {}, r('wlc_bmac_validate_chip_access', 0)),
        ('Broadcom board 0x40c', {}, b(boardvendor=0x14e4, boardtype=0x40c)),
        ('Broadcom board 0x417 rev 0x3f', {}, both(b(boardvendor=0x14e4, boardtype=0x417),
                                                     v(boardrev='0x3f'))),
        ('Broadcom board 0x417 rev 0x40', {}, both(b(boardvendor=0x14e4, boardtype=0x417),
                                                     v(boardrev='0x40'))),
        ('Broadcom board 0x421 rev 0x50', {}, both(b(boardvendor=0x14e4, boardtype=0x421),
                                                     v(boardrev='0x50'))),
        ('Apple 0x4e rev 0x41', {}, both(b(boardtype=0x4e), v(boardrev='0x41'))),
        ('Apple 0x4e rev 0x40', {}, both(b(boardtype=0x4e), v(boardrev='0x40'))),
        ('Apple 0xe4 rev 0x1500', {}, both(b(boardtype=0xe4), v(boardrev='0x1500'))),
        ('Apple 0xe4 rev 0x1501', {}, both(b(boardtype=0xe4), v(boardrev='0x1501'))),
        ('Apple 0xef rev 0x1201', {}, both(b(boardtype=0xef), v(boardrev='0x1201'))),
        ('boardflags 0x20', {}, v(boardflags='0x20')),
        ('boardflags 0xffffffff', {}, v(boardflags='0xffffffff', boardflags2='0xffffffff')),
        ('host interface 2', {}, r('si_chip_hostif', 2)),
        ('shim allocation fails', {}, r('osl_malloc', 0, when=lambda m, a: a[1] == 0x18)),
        ('shared PHY attach fails', {}, r('wlc_phy_shared_attach', 0)),
        ('PHY attach fails, band 1', {}, r('wlc_phy_attach', 0)),
        ('PHY attach fails, band 2', {}, r('wlc_phy_attach', 0, nth=2)),
        ('DMA resources refused', {}, r('wl_alloc_dma_resources', 0)),
        ('dma_attach of FIFO 2 fails', {}, r('dma_attach', 0, nth=3)),
        ('no macaddr', {}, r('getvar', 0, when=lambda m, a: cstring(m, a[1]) == 'macaddr')),
        ('macaddr 00:00:00:00:00:00', {}, v(macaddr='00:00:00:00:00:00')),
        ('macaddr ff:ff:ff:ff:ff:ff', {}, v(macaddr='ff:ff:ff:ff:ff:ff')),
        ('macaddr FF-FF-FF-FF-FF-FF', {}, v(macaddr='FF-FF-FF-FF-FF-FF')),
        ('macaddr 00:90:4c', {}, v(macaddr='00:90:4c')),
        ('macaddr 1:2:3:4:5:6:7', {}, v(macaddr='1:2:3:4:5:6:7')),
        ('LED state fails', {}, r('wlc_bmac_led_attach', 0)),
    ]
    for k, size in enumerate((0x1d0, 0x20, 0x70, 0x60)):
        out.append(('allocation %d of wlc_hw_attach fails' % (k + 1), {},
                    r('osl_malloc', 0, when=lambda m, a, s=size: a[1] == s)))
    for rev in ('0', '0x3101', '0x0101', '0x1a01', '0x10a1', '0x110a', '0x1001', '0xff',
                '0x1101', '0x2999', '0x40', '0x12345'):
        out.append(('boardrev %s' % rev, {}, v(boardrev=rev)))
    return out


# --- what is compared ---------------------------------------------------------

def heap(m):
    """{address: bytes} of every live allocation, with the expected
    differences taken out."""
    out = {a: bytearray(m.read(a, n)) for a, n in sorted(m.allocs.items())}
    return out


def normalise(r, mem):
    """Replace the pointer to the FIFO size table by its contents, and the
    backplane's callbacks by a mark."""
    m = r.m
    wlc = r.d.wlc
    if not wlc:
        return mem
    hw = m.r64(wlc + WLC_HW)
    if hw in mem:
        p = m.r64(hw + HW_XMTFIFO_SZ_TBL)
        mem[hw][HW_XMTFIFO_SZ_TBL:HW_XMTFIFO_SZ_TBL + 8] = (m.read(p, 8) if p else bytes(8))
        mem[('fifo table', 'rest')] = bytearray(m.read(p + 8, 4) if p else bytes(4))
    # the callbacks: function pointers to wlc_bmac_wlintrsoff/-restore or the
    # open ones; they are found as the two words next to each other equal to
    # the addresses of either pair
    pairs = []
    for names in (('bcm4360_bmac_wlintrsoff', 'bcm4360_bmac_wlintrsrestore'),):
        try:
            pairs.append(tuple(m.resolve(n) for n in names))
        except Exception:
            pass
    pairs += [p for p in getattr(r, 'blob_callbacks', [])]
    for a, block in mem.items():
        if not isinstance(a, int):
            continue
        for i in range(0, len(block) - 15, 8):
            w0, w1 = struct.unpack_from('<QQ', block, i)
            if (w0, w1) in pairs:
                block[i:i + 16] = b'CALLBACK' * 2
    return mem


def blob_callbacks(r):
    """Broadcom's callbacks (sub_061268, sub_061285): their offsets in the
    object's .text, from the specification (bmac-run.md R9)."""
    tb = hybrid.BASES['.text']
    return [(tb + 0x061268, tb + 0x061285)]


def run(core, card, patch, attach_on):
    r = hybrid.Run(core, off=() if attach_on else ('mac_attach',), patch=patch, **card)
    r.blob_callbacks = blob_callbacks(r)
    out = {}
    r.session(['attach'], stop_on_error=False)
    out['attach'] = r.results[0][1] if r.results else None
    out['heap after attach'] = normalise(r, heap(r.m))
    if out['attach'] == 0:
        r.session(SUCCESS_STEPS, stop_on_error=False)
        out['steps'] = [x[1] for x in r.results[1:]]
        out['heap after detach'] = normalise(r, heap(r.m))
    acc = ab.logical(r.s.chip.trace, r.s.chip.func_name)
    return out, acc


def describe(a, b, show):
    lines = []
    for key in a:
        x, y = a[key], b.get(key)
        if x == y:
            continue
        if isinstance(x, dict) and isinstance(y, dict):
            for addr in sorted(set(x) | set(y), key=str):
                p, q = x.get(addr), y.get(addr)
                if p == q:
                    continue
                if p is None or q is None or len(p) != len(q):
                    lines.append('  %s: block %s only in %s (size %s)'
                                 % (key, addr if isinstance(addr, tuple) else hex(addr),
                                    'Broadcom' if q is None else 'open',
                                    len(p or q)))
                    continue
                pos = [i for i in range(len(p)) if p[i] != q[i]]
                lines.append('  %s: block %s (size %#x): offsets %s' % (
                    key, addr if isinstance(addr, tuple) else hex(addr), len(p),
                    ', '.join('%#x %02x/%02x' % (i, p[i], q[i]) for i in pos[:10])))
        else:
            lines.append('  %s: Broadcom %r, open %r' % (key, x, y))
        if len(lines) >= show:
            break
    return lines


def attach_check(off=(), show=12, on=()):
    core = hybrid.build(quiet=True)
    bad = 0
    n = 0
    for label, card, patch in cases():
        n += 1
        want, wacc = run(core, card, patch, False)
        have, hacc = run(core, card, patch, True)
        lines = describe(want, have, show)
        if wacc != hacc:
            lines.append('  accesses differ (%d / %d)' % (len(wacc), len(hacc)))
        if want['attach'] is None or have['attach'] is None:
            lines.append('  the attach did not run to its end (hook or emulation error)')
        status = 'the same' if not lines else 'DIFFERENT'
        print('%-40s attach %-6s %s' % (label, want['attach'], status))
        for line in lines:
            print(line)
        if lines and wacc != hacc:
            ab.compare(wacc, hacc, show)
        bad += bool(lines)
    print('attach check: %d cases, %s' % (n, 'the same' if not bad else
                                          'DIFFERENT in %d' % bad))
    return bad
