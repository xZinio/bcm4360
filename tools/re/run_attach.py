#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Run the driver core's attach (and optionally up) against the modelled
BCM4360 and record what it does to the hardware.

    run_attach.py [--srom FILE] [--up] [--trace FILE] [--verbose]

FILE is a binary SROM image or a list of name=value lines (see srom.py); the
default is the synthetic card in data/.
"""
import argparse
import os
import struct
import sys
import time

from bcm4360 import Bcm4360
from chip import BAR0_VA
from emu import EmuError, Machine, s32
from srom import Layout, load_vars

PCI_BUS = 1
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SROM = os.path.join(HERE, 'data', 'synthetic-4360-2x2.vars')


def load_srom(path, blob):
    raw = open(path, 'rb').read()
    if b'=' in raw[:4096] and raw[:1] in b'#abcdefghijklmnopqrstuvwxyz\r\n':
        return Layout(blob).encode(load_vars(path))
    return list(struct.unpack('<%dH' % (len(raw) // 2), raw[:len(raw) & ~1]))


class Driver:
    """The object attached to a modelled card, as the Linux glue would do it."""

    def __init__(self, srom=DEFAULT_SROM, echo=True, verbose=False, chip_class=Bcm4360, **kw):
        m = self.m = Machine()
        m.os.echo = echo
        if isinstance(srom, dict):              # variables
            words = Layout(m.blob).encode(srom)
        elif isinstance(srom, (list, tuple)):   # image
            words = list(srom)
        else:
            words = load_srom(srom, m.blob) if srom else None
        self.chip = chip_class(m, srom=words, **kw)
        self.chip.verbose = verbose
        self.osh = m.buf(0x100)       # osl_pubinfo_t and private part
        m.w8(self.osh, 1)             # pkttag
        m.w8(self.osh + 1, 1)         # mmbus
        self.wl = m.buf(0x400)        # struct wl_info: the harness keeps the wlc first
        self.pdev = m.buf(0x100)
        self.perr = m.buf(8)
        self.wlc = 0

    def attach(self):
        m, chip = self.m, self.chip
        self.wlc = m.call('wlc_attach', self.wl, chip.vendor, chip.device, 0, 0, self.osh,
                          BAR0_VA, PCI_BUS, self.pdev, self.perr)
        if self.wlc:
            m.w64(self.wl, self.wlc)
            self.pub = m.call('wlc_pub', self.wlc)
        return self.wlc

    @property
    def err(self):
        return s32(self.m.r32(self.perr))

    def up(self):
        return s32(self.m.call('wlc_up', self.wlc))

    def iovar_setint(self, name, val):
        return s32(self.m.call('wlc_iovar_setint', self.wlc, self.m.cstr(name), val))

    def iovar_getint(self, name):
        p = self.m.buf(8)
        r = s32(self.m.call('wlc_iovar_getint', self.wlc, self.m.cstr(name), p))
        return r, s32(self.m.r32(p))

    def ioctl(self, cmd, data=b'', size=None):
        n = size if size is not None else len(data)
        p = self.m.buf(max(n, 8))
        if data:
            self.m.write(p, data)
        r = s32(self.m.call('wlc_ioctl', self.wlc, cmd, p, n, 0))
        return r, self.m.read(p, n)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--srom', default=DEFAULT_SROM)
    ap.add_argument('--no-srom', action='store_true', help='card without SROM (OTP only)')
    ap.add_argument('--up', action='store_true', help='also run wlc_up()')
    ap.add_argument('--trace', help='write the register trace to this file')
    ap.add_argument('--verbose', action='store_true', help='print accesses as they happen')
    ns = ap.parse_args()

    d = Driver(None if ns.no_srom else ns.srom, verbose=ns.verbose)
    chip = d.chip
    t0 = time.time()
    rc = 0
    try:
        d.attach()
        print('wlc_attach -> %#x, err %d' % (d.wlc, d.err))
    except EmuError as e:
        print('wlc_attach FAILED: %s' % e)
        rc = 1
    print('%.1fs, %d hardware accesses' % (time.time() - t0, len(chip.trace)))
    if d.wlc and ns.up:
        n = len(chip.trace)
        try:
            print('wlc_up -> %d, %d hardware accesses' % (d.up(), len(chip.trace) - n))
        except EmuError as e:
            print('wlc_up FAILED: %s' % e)
            rc = 1
    if ns.trace:
        chip.dump_trace(ns.trace)
    return rc


if __name__ == '__main__':
    sys.exit(main())
