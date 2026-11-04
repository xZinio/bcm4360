#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Read the driver core's state through the glue's private ioctl
(SIOCDEVPRIVATE -> wlc_ioctl; needs root): driver variables (iovars), a few
ioctls and the frame counters. Reads only.

    wl-iovar.py [--dev wlan0] get NAME [SIZE]   one iovar as integer and hex
    wl-iovar.py [--dev wlan0] counters          the counters (wl_cnt_t), non-zero ones
    wl-iovar.py [--dev wlan0] diag              the set scripts/card-test.sh records

The layout of the counters is read from src/include/wlioctl.h.
"""
import argparse
import ctypes
import fcntl
import os
import re
import socket
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WLIOCTL_H = os.path.join(HERE, '..', 'src', 'include', 'wlioctl.h')

SIOCDEVPRIVATE = 0x89f0
WLC_GET_BSSID = 23
WLC_GET_CHANNEL = 29
WLC_GET_RADIO = 37
WLC_GET_COUNTRY = 83
WLC_GET_VAR = 262

# iovars of the diagnosis: the chains, the radio and power save, the channel
DIAG_VARS = ('txchain', 'rxchain', 'hw_txchain', 'hw_rxchain', 'txcore', 'spatial_policy',
             'mpc', 'chanspec', 'bw_cap', 'nmode', 'vhtmode', 'qtxpower', 'curr_txpower',
             'roam_off', 'PM', 'wsec', 'wpa_auth', 'sup_wpa')


def ioctl(dev, cmd, buf):
    """wlc_ioctl(cmd, buf): the buffer afterwards. OSError on an error."""
    b = ctypes.create_string_buffer(bytes(buf), len(buf))
    ioc = struct.pack('<I4xQIB3xII', cmd, ctypes.addressof(b), len(buf), 0, 0, 0)
    iocb = ctypes.create_string_buffer(ioc, len(ioc))
    ifr = bytearray(struct.pack('16sQ', dev.encode(), ctypes.addressof(iocb)) + bytes(16))
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        fcntl.ioctl(s.fileno(), SIOCDEVPRIVATE, ifr)
    return b.raw


def getvar(dev, name, size=4):
    """The value of iovar NAME (the buffer of WLC_GET_VAR: the name, then room
    for the value, which the core writes from the start of the buffer)."""
    n = name.encode() + b'\0'
    return ioctl(dev, WLC_GET_VAR, n + bytes(max(size, 4)))


def getint(dev, name):
    return struct.unpack_from('<i', getvar(dev, name, 4))[0]


def counter_layout():
    """[(name, count)] of the 32-bit fields of wl_cnt_t after version, length."""
    text = open(WLIOCTL_H).read()
    defs = dict((k, int(v)) for k, v in re.findall(r'#define\s+(\w+)\s+(\d+)', text))
    end = text.index('} wl_cnt_t;')
    start = text.rindex('typedef struct {', 0, end)
    out = []
    for typ, name, dim in re.findall(r'(uint16|uint32)\s+(\w+)\s*(?:\[(\w+)\])?\s*;',
                                     text[start:end]):
        n = 1 if not dim else (int(dim) if dim.isdigit() else defs.get(dim, 1))
        if typ == 'uint32':
            out.append((name, n))
    return out


def counters(dev):
    layout = counter_layout()
    size = 4 + 4 * sum(n for _, n in layout)
    raw = getvar(dev, 'counters', size)
    version, length = struct.unpack_from('<HH', raw, 0)
    out = {'version': version, 'length': length}
    off = 4
    for name, n in layout:
        vals = struct.unpack_from('<%dI' % n, raw, off)
        off += 4 * n
        out[name] = vals[0] if n == 1 else list(vals)
    return out


def nonzero(v):
    return any(v) if isinstance(v, list) else v != 0


def diag(dev):
    lines = []
    for name in DIAG_VARS:
        try:
            v = getvar(dev, name, 64)
            lines.append('%-16s %d (%s)' % (name, struct.unpack_from('<i', v)[0],
                                            v[:16].hex()))
        except OSError as e:
            lines.append('%-16s error %s' % (name, e.strerror))
    for label, cmd, size in (('radio', WLC_GET_RADIO, 4), ('channel', WLC_GET_CHANNEL, 12),
                             ('bssid', WLC_GET_BSSID, 6), ('country', WLC_GET_COUNTRY, 12)):
        try:
            v = ioctl(dev, cmd, bytes(size))
            if cmd == WLC_GET_COUNTRY:      # the abbreviation in use (no revision)
                lines.append('%-16s %s' % (label, v.split(b'\0')[0].decode('ascii', 'replace')))
                continue
            lines.append('%-16s %s' % (label, v.hex()))
        except OSError as e:
            lines.append('%-16s error %s' % (label, e.strerror))
    try:
        c = counters(dev)
        lines.append('counters (version %d, length %d): %s' % (
            c.pop('version'), c.pop('length'),
            ' '.join('%s=%s' % (k, v) for k, v in c.items() if nonzero(v))))
    except OSError as e:
        lines.append('counters         error %s' % e.strerror)
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dev', default='wlan0')
    ap.add_argument('cmd', choices=('get', 'counters', 'diag'))
    ap.add_argument('args', nargs='*')
    ns = ap.parse_args()
    try:
        if ns.cmd == 'get':
            size = int(ns.args[1]) if len(ns.args) > 1 else 4
            v = getvar(ns.dev, ns.args[0], size)
            print('%d %s' % (struct.unpack_from('<i', v)[0], v[:size].hex()))
        elif ns.cmd == 'counters':
            for k, v in counters(ns.dev).items():
                if nonzero(v):
                    print('%-20s %s' % (k, v))
        else:
            print(diag(ns.dev))
    except OSError as e:
        sys.exit('%s: %s' % (ns.dev, e.strerror))
    return 0


if __name__ == '__main__':
    sys.exit(main())
