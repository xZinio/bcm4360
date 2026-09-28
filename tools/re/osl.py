#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Python model of the OS layer the object imports (src/shared/linux_osl.c and
the wl_* callbacks of src/wl/sys/wl_linux.c), for emu.Machine.

Hardware access (osl_read*/osl_write*, osl_pci_*_config, osl_reg_map) is
forwarded to machine.hw, see chip.py; without one, touching hardware raises.
"""
from emu import Call, EmuError, s32

PKT_HEADROOM = 256
PKTTAG_SZ = 32


class Packet:
    __slots__ = ('handle', 'head', 'end', 'data', 'len', 'next', 'link', 'prio', 'tag', 'shared')


class Timer:
    __slots__ = ('handle', 'fn', 'arg', 'name', 'ms', 'periodic', 'due', 'set')


class OS:
    """State of the modelled OS: clock, timers, packets, console."""

    def __init__(self, m):
        self.m = m
        self.now_us = 1000000          # virtual time
        self.console = []              # lines printed through osl_printf
        self.echo = False
        self.malloced = 0
        self.pkts = {}
        self.pktalloced = 0
        self.timers = {}
        self.events = []               # wl_event() notifications
        self.rx = []                   # packets handed to wl_sendup()
        self.txflow = []
        self._line = ''

    # console
    def puts(self, s):
        self._line += s
        while '\n' in self._line:
            line, self._line = self._line.split('\n', 1)
            self.console.append(line)
            if self.echo:
                print('  | ' + line)

    # time
    def advance(self, usec):
        self.now_us += usec

    def due_timers(self):
        now = self.now_us // 1000
        return sorted((t for t in self.timers.values() if t.set and t.due <= now),
                      key=lambda t: t.due)

    def run_timers(self, m, until_ms=None, limit=1000):
        """Fire timers in order of expiry, moving the clock forward to each."""
        fired = 0
        while fired < limit:
            pending = [t for t in self.timers.values() if t.set]
            if not pending:
                break
            t = min(pending, key=lambda t: t.due)
            if until_ms is not None and t.due > until_ms:
                break
            if t.due * 1000 > self.now_us:
                self.now_us = t.due * 1000
            if t.periodic:
                t.due += max(t.ms, 1)
            else:
                t.set = False
            m.call(t.fn, t.arg)
            fired += 1
        if until_ms is not None and until_ms * 1000 > self.now_us:
            self.now_us = until_ms * 1000
        return fired

    # packets
    def pktget(self, m, length):
        p = Packet()
        size = PKT_HEADROOM + length + 64
        p.handle = m.malloc(64)                  # stands for the sk_buff
        m.write(p.handle, b'\0' * 64)
        p.tag = m.malloc(PKTTAG_SZ)
        m.write(p.tag, b'\0' * PKTTAG_SZ)
        p.head = m.malloc(size)
        m.write(p.head, b'\0' * size)
        p.end = p.head + size
        p.data = p.head + PKT_HEADROOM
        p.len = length
        p.next = p.link = 0
        p.prio = 0
        p.shared = False
        self.pkts[p.handle] = p
        self.pktalloced += 1
        return p

    def pkt(self, handle):
        try:
            return self.pkts[handle]
        except KeyError:
            raise EmuError('%#x is not a packet' % handle)

    def pktbytes(self, m, handle):
        p = self.pkt(handle)
        return m.read(p.data, p.len)


def install(m):
    os = OS(m)
    m.os = os
    m.hw = None
    D = m.define

    def hw():
        if m.hw is None:
            raise EmuError('hardware access without a chip model (called from %s)' % m.caller())
        return m.hw

    # ---- memory
    def osl_malloc(m, osh, size):
        size &= 0xffffffff
        os.malloced += size
        return m.malloc(size, fill=0)

    def osl_mfree(m, osh, addr, size):
        os.malloced -= size & 0xffffffff
        m.free(addr)

    D('osl_malloc', 2, osl_malloc)
    D('osl_mfree', 3, osl_mfree)
    D('osl_malloced', 1, lambda m, osh: os.malloced)

    def osl_memcpy(m, d, s, n):
        if n:
            m.write(d, m.read(s, n))
        return d

    def osl_memset(m, d, c, n):
        if n:
            m.write(d, bytes([c & 0xff]) * n)
        return d

    def osl_memcmp(m, a, b, n):
        x, y = m.read(a, n), m.read(b, n)
        return 0 if x == y else (1 if x > y else -1)

    D('osl_memcpy', 3, osl_memcpy)
    D('osl_memmove', 3, osl_memcpy)
    D('osl_memset', 3, osl_memset)
    D('osl_memcmp', 3, osl_memcmp)

    # ---- strings
    def osl_strcmp(m, a, b):
        x, y = m.string(a), m.string(b)
        return 0 if x == y else (1 if x > y else -1)

    def osl_strncmp(m, a, b, n):
        x, y = m.string(a)[:n], m.string(b)[:n]
        return 0 if x == y else (1 if x > y else -1)

    def osl_strcpy(m, d, s):
        m.write(d, m.string(s).encode('latin1') + b'\0')
        return d

    def osl_strncpy(m, d, s, n):
        n &= 0xffffffff
        v = m.string(s).encode('latin1')[:n]
        m.write(d, v + b'\0' * (n - len(v)))
        return d

    def osl_strchr(m, s, c):
        i = m.string(s).find(chr(c & 0xff))
        if c & 0xff == 0:
            return s + len(m.string(s))
        return s + i if i >= 0 else 0

    def osl_strrchr(m, s, c):
        i = m.string(s).rfind(chr(c & 0xff))
        return s + i if i >= 0 else 0

    D('osl_strcmp', 2, osl_strcmp)
    D('osl_strncmp', 3, osl_strncmp)
    D('osl_strlen', 1, lambda m, s: len(m.string(s, 1 << 20)))
    D('osl_strcpy', 2, osl_strcpy)
    D('osl_strncpy', 3, osl_strncpy)
    D('osl_strchr', 2, osl_strchr)
    D('osl_strrchr', 2, osl_strrchr)

    # ---- printing
    def osl_printf(m, fmt):
        s = m.format(m.string(fmt), m.varargs(1))
        os.puts(s)
        return len(s)

    def osl_sprintf(m, buf, fmt):
        s = m.format(m.string(fmt), m.varargs(2))
        m.write(buf, s.encode('latin1') + b'\0')
        return len(s)

    def _bounded(m, buf, n, s):
        n &= 0xffffffff
        if n:
            m.write(buf, s.encode('latin1')[:n - 1] + b'\0')
        return len(s)

    def osl_snprintf(m, buf, n, fmt):
        return _bounded(m, buf, n, m.format(m.string(fmt), m.varargs(3)))

    def osl_vsnprintf(m, buf, n, fmt, ap):
        return _bounded(m, buf, n, m.format(m.string(fmt), m.va_list(ap)))

    D('osl_printf', 1, osl_printf)
    D('osl_sprintf', 2, osl_sprintf)
    D('osl_snprintf', 3, osl_snprintf)
    D('osl_vsnprintf', 4, osl_vsnprintf)

    # ---- time
    def osl_delay(m, usec):
        os.advance(usec & 0xffffffff)
        if m.hw is not None:
            m.hw.tick(os.now_us)

    D('osl_delay', 1, osl_delay)
    D('osl_sysuptime', 0, lambda m: (os.now_us // 1000) & 0xffffffff)

    # ---- hardware
    D('osl_readb', 1, lambda m, r: hw().mmio_read(r, 1))
    D('osl_readw', 1, lambda m, r: hw().mmio_read(r, 2))
    D('osl_readl', 1, lambda m, r: hw().mmio_read(r, 4))
    D('osl_writeb', 2, lambda m, v, r: hw().mmio_write(r, 1, v & 0xff))
    D('osl_writew', 2, lambda m, v, r: hw().mmio_write(r, 2, v & 0xffff))
    D('osl_writel', 2, lambda m, v, r: hw().mmio_write(r, 4, v & 0xffffffff))
    D('osl_pci_read_config', 3,
      lambda m, osh, off, size: hw().pci_read(off & 0xffffffff, size & 0xffffffff))
    D('osl_pci_write_config', 4,
      lambda m, osh, off, size, val: hw().pci_write(off & 0xffffffff, size & 0xffffffff,
                                                    val & 0xffffffff))
    D('osl_pci_bus', 1, lambda m, osh: hw().pci_bus)
    D('osl_pci_slot', 1, lambda m, osh: hw().pci_slot)
    D('osl_reg_map', 2, lambda m, pa, size: hw().reg_map(pa & 0xffffffff, size & 0xffffffff))
    D('osl_reg_unmap', 1, lambda m, va: 0)
    D('osl_uncached', 1, lambda m, va: va)
    D('osl_cached', 1, lambda m, va: va)

    def unsupported(name):
        def f(m, *a):
            raise EmuError('%s called from %s' % (name, m.caller()))
        return f

    D('osl_pcmcia_read_attr', 4, unsupported('osl_pcmcia_read_attr'))
    D('osl_pcmcia_write_attr', 4, unsupported('osl_pcmcia_write_attr'))

    # ---- DMA: "bus addresses" are the emulated addresses themselves
    def osl_dma_alloc_consistent(m, osh, size, align_bits, tot, pap):
        size &= 0xffffffff
        align_bits &= 0xffff
        a = m.malloc(size, 1 << max(align_bits, 12), fill=0)
        m.w32(tot, size)
        m.w64(pap, a)
        return a

    D('osl_dma_alloc_consistent', 5, osl_dma_alloc_consistent)
    D('osl_dma_free_consistent', 4, lambda m, osh, va, size, pa: m.free(va))
    D('osl_dma_map', 6, lambda m, osh, va, size, direction, p, dmah: va & 0xffffffff)
    D('osl_dma_unmap', 4, lambda m, osh, pa, size, direction: 0)

    # ---- files: none exist (the names asked for are kept in os.images)
    os.images = []

    def osl_os_open_image(m, name):
        os.images.append((m.string(name), m.caller()))
        return 0

    D('osl_os_open_image', 1, osl_os_open_image)
    D('osl_os_get_image_block', 3, lambda m, buf, n, image: 0)
    D('osl_os_close_image', 1, lambda m, image: 0)

    # ---- packets
    def osl_pktget(m, osh, length):
        return os.pktget(m, length & 0xffffffff).handle

    def osl_pktdup(m, osh, h):
        p = os.pkt(h)
        q = os.pktget(m, p.len)
        m.write(q.data, m.read(p.data, p.len))
        m.write(q.tag, m.read(p.tag, PKTTAG_SZ))
        q.prio = p.prio
        return q.handle

    def osl_pktfree(m, osh, h, send):
        # PKTFREE frees the whole chain; the tx completion callback registered
        # with PKTFREESETCB lives in osl_pubinfo_t and is called for sent packets
        tx_fn = m.r64(osh + 8) if osh else 0
        tx_ctx = m.r64(osh + 16) if osh else 0
        chain = []
        while h:
            p = os.pkt(h)
            chain.append(h)
            h = p.next
        if (send & 0xff) and tx_fn:
            yield Call(tx_fn, tx_ctx, chain[0], 0)
        for h in chain:
            os.pkts.pop(h, None)
            os.pktalloced -= 1
        return 0

    def osl_pktpush(m, osh, h, n):
        p = os.pkt(h)
        n = s32(n)
        if p.data - n < p.head:
            raise EmuError('pktpush(%d) underruns the headroom (from %s)' % (n, m.caller()))
        p.data -= n
        p.len += n
        return p.data

    def osl_pktpull(m, osh, h, n):
        p = os.pkt(h)
        n = s32(n)
        if n > p.len:
            raise EmuError('pktpull(%d) beyond the packet length %d' % (n, p.len))
        p.data += n
        p.len -= n
        return p.data

    def osl_pktsetlen(m, osh, h, n):
        p = os.pkt(h)
        n &= 0xffffffff
        if p.data + n > p.end:
            raise EmuError('pktsetlen(%d) beyond the buffer' % n)
        p.len = n

    def setter(field):
        def f(m, h, x):
            setattr(os.pkt(h), field, x)
        return f

    D('osl_pktget', 2, osl_pktget)
    D('osl_pktdup', 2, osl_pktdup)
    D('osl_pktfree', 3, osl_pktfree)
    D('osl_pktdata', 2, lambda m, osh, h: os.pkt(h).data)
    D('osl_pktlen', 2, lambda m, osh, h: os.pkt(h).len)
    D('osl_pktheadroom', 2, lambda m, osh, h: os.pkt(h).data - os.pkt(h).head)
    D('osl_pkttailroom', 2, lambda m, osh, h: os.pkt(h).end - os.pkt(h).data - os.pkt(h).len)
    D('osl_pktnext', 2, lambda m, osh, h: os.pkt(h).next)
    D('osl_pktsetnext', 2, setter('next'))
    D('osl_pktsetlen', 3, osl_pktsetlen)
    D('osl_pktpush', 3, osl_pktpush)
    D('osl_pktpull', 3, osl_pktpull)
    D('osl_pkttag', 1, lambda m, h: os.pkt(h).tag)
    D('osl_pktlink', 1, lambda m, h: os.pkt(h).link)
    D('osl_pktsetlink', 2, setter('link'))
    D('osl_pktprio', 1, lambda m, h: os.pkt(h).prio)
    D('osl_pktsetprio', 2, lambda m, h, x: setattr(os.pkt(h), 'prio', x & 0xffffffff))
    D('osl_pktshared', 1, lambda m, h: 1 if os.pkt(h).shared else 0)
    D('osl_pktalloced', 1, lambda m, osh: os.pktalloced)

    # ---- the glue (wl_linux.c).  struct wl_info is opaque to the object; the
    # harness keeps the wlc pointer in its first field.
    def wlc_of(m, wl):
        return m.r64(wl)

    def wl_init(m, wl):
        yield Call('wlc_reset', wlc_of(m, wl))
        yield Call('wlc_init', wlc_of(m, wl))
        return 0

    def wl_reset(m, wl):
        yield Call('wlc_reset', wlc_of(m, wl))
        return 0

    def wl_intrson(m, wl):
        yield Call('wlc_intrson', wlc_of(m, wl))
        return 0

    def wl_intrsoff(m, wl):
        r = yield Call('wlc_intrsoff', wlc_of(m, wl))
        return r & 0xffffffff

    def wl_intrsrestore(m, wl, mask):
        yield Call('wlc_intrsrestore', wlc_of(m, wl), mask & 0xffffffff)
        return 0

    def wl_up(m, wl):
        r = yield Call('wlc_up', wlc_of(m, wl))
        return r

    def wl_down(m, wl):
        yield Call('wlc_down', wlc_of(m, wl))
        return 0

    D('wl_init', 1, wl_init)
    D('wl_reset', 1, wl_reset)
    D('wl_intrson', 1, wl_intrson)
    D('wl_intrsoff', 1, wl_intrsoff)
    D('wl_intrsrestore', 2, wl_intrsrestore)
    D('wl_up', 1, wl_up)
    D('wl_down', 1, wl_down)

    def wl_init_timer(m, wl, fn, arg, name):
        t = Timer()
        t.handle = m.malloc(16, fill=0)
        t.fn, t.arg = fn, arg
        t.name = m.string(name) if name else ''
        t.ms, t.periodic, t.due, t.set = 0, False, 0, False
        os.timers[t.handle] = t
        return t.handle

    def wl_add_timer(m, wl, h, ms, periodic):
        t = os.timers[h]
        t.ms = ms & 0xffffffff
        t.periodic = bool(periodic & 0xffffffff)
        t.due = os.now_us // 1000 + t.ms
        t.set = True

    def wl_del_timer(m, wl, h):
        t = os.timers.get(h)
        if t is None:
            return 1
        t.set = False
        return 1

    def wl_free_timer(m, wl, h):
        os.timers.pop(h, None)

    D('wl_init_timer', 4, wl_init_timer)
    D('wl_add_timer', 4, wl_add_timer)
    D('wl_del_timer', 2, wl_del_timer)
    D('wl_free_timer', 2, wl_free_timer)

    ifname = []

    def wl_ifname(m, wl, wlif):
        if not ifname:
            ifname.append(m.cstr('wlan0'))
        return ifname[0]

    def wl_event(m, wl, name, e):
        # wlc_event_t starts with wl_event_msg_t (big endian on the wire, host order here)
        os.events.append(m.read(e, 72))

    def wl_sendup(m, wl, wlif, p, numpkt):
        os.rx.append(os.pktbytes(m, p))
        return osl_pktfree(m, 0, p, 0)

    D('wl_ifname', 2, wl_ifname)
    D('wl_event', 3, wl_event)
    D('wl_event_sync', 3, wl_event)
    D('wl_sendup', 4, wl_sendup)
    D('wl_txflowcontrol', 4, lambda m, wl, wlif, state, prio:
      os.txflow.append((state & 0xff, s32(prio))))
    D('wl_alloc_dma_resources', 2, lambda m, wl, width: 1)
    D('wl_dump_ver', 2, lambda m, wl, b: 0)
    D('wl_monitor', 3, lambda m, wl, rxsts, p: 0)
    D('wl_set_monitor', 2, lambda m, wl, val: 0)
    D('wl_pcie_bar1', 2, lambda m, wl, addr: hw().bar1(m, addr))
    D('wl_osl_pcie_rc', 3, lambda m, wl, op, param: hw().pcie_rc(op & 0xffffffff, s32(param)))
