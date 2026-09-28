#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Bring the modelled card up and tune it to a channel; record what the driver
core does to the hardware for the channel change.

    run_chan.py CHANSPEC [CHANSPEC ...] [--srom FILE] [--out DIR]

CHANSPEC as the driver prints it: 1, 6, 36, 36/40 (or 36l, 40u), 36/80, ...
One trace file per channel is written to DIR (default re-out/chan).
"""
import argparse
import os
import sys

from emu import EmuError
from run_attach import DEFAULT_SROM, Driver


def chanspec(m, text):
    """Let the driver's own parser turn the text into a chanspec."""
    v = m.call('wf_chspec_aton', m.cstr(text)) & 0xffff
    if v == 0:
        raise ValueError('bad chanspec %r' % text)
    return v


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('chanspecs', nargs='+')
    ap.add_argument('--srom', default=DEFAULT_SROM)
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                  '..', '..', 're-out', 'chan'))
    ns = ap.parse_args()
    os.makedirs(ns.out, exist_ok=True)

    d = Driver(ns.srom, echo=True)
    d.attach()
    # keep the radio on although the interface is idle
    print('mpc 0 ->', d.iovar_setint('mpc', 0))
    print('up ->', d.up())
    chip = d.chip
    for text in ns.chanspecs:
        cs = chanspec(d.m, text)
        n = len(chip.trace)
        try:
            r = d.iovar_setint('chanspec', cs)
        except EmuError as e:
            print('%s: FAILED %s' % (text, e))
            r = None
        path = os.path.join(ns.out, 'chan-%s.txt' % text.replace('/', '_'))
        chip.dump_trace(path, n)
        print('%-8s chanspec %#06x -> %s, %d accesses, %s'
              % (text, cs, r, len(chip.trace) - n, os.path.relpath(path)))
        rc, cur = d.iovar_getint('chanspec')
        print('         driver reports chanspec %#06x' % (cur & 0xffff))
    return 0


if __name__ == '__main__':
    sys.exit(main())
