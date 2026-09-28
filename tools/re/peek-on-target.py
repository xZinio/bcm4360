#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Run on the machine that has the BCM4360, as root:

    sudo python3 peek-on-target.py > bcm4360-peek.txt

Reads the identity of the card: PCI configuration space, chip id and
revision, capabilities, chip status, PMU capabilities, OTP layout, the SROM
contents and the PHY version. It only *reads*, and only registers whose
reading has no side effect, through the two fixed register windows of the
card (ChipCommon and the PCIe core) and - for three registers of the 802.11
core - through the movable window while it points to that core. It works while
the driver is loaded and the interface is up; nothing is written to the card
and the driver is not disturbed.

The output contains the MAC address of the card (inside the SROM contents).
"""
import glob
import mmap
import os
import struct
import sys

VENDOR, DEVICES = 0x14e4, (0x43a0, 0x43a1, 0x43a2, 0x4360)
BAR0_SIZE = 0x8000
CC = 0x3000             # fixed window: ChipCommon
PCIE = 0x2000           # fixed window: PCIe core

CC_REGS = [
    (0x000, 'chip id, revision, package'),
    (0x004, 'capabilities'),
    (0x008, 'core control'),
    (0x010, 'OTP status'),
    (0x014, 'OTP control'),
    (0x01c, 'OTP layout'),
    (0x028, 'chip control'),
    (0x02c, 'chip status'),
    (0x0ac, 'capabilities, extended'),
    (0x0fc, 'address of the enumeration ROM'),
    (0x190, 'SROM control'),
    (0x1e0, 'clock control and status'),
    (0x600, 'PMU control'),
    (0x604, 'PMU capabilities'),
    (0x608, 'PMU status'),
    (0x60c, 'PMU resource state'),
    (0x610, 'PMU resources pending'),
    (0x618, 'PMU minimum resource mask'),
    (0x61c, 'PMU maximum resource mask'),
    (0x66c, 'PMU crystal frequency'),
]
D11_REGS = [
    (0x120, 4, 'MAC control'),
    (0x15c, 4, 'MAC capabilities'),
    (0x3e0, 2, 'PHY version'),
]


def find_device():
    for d in sorted(glob.glob('/sys/bus/pci/devices/*')):
        try:
            vendor = int(open(os.path.join(d, 'vendor')).read(), 16)
            device = int(open(os.path.join(d, 'device')).read(), 16)
        except OSError:
            continue
        if vendor == VENDOR and device in DEVICES:
            return d
    return None


def main():
    dev = find_device()
    if dev is None:
        sys.exit('no BCM4360 found')
    print('device %s' % os.path.basename(dev))
    for name in ('vendor', 'device', 'subsystem_vendor', 'subsystem_device', 'revision'):
        print('%s %s' % (name, open(os.path.join(dev, name)).read().strip()))
    driver = os.path.join(dev, 'driver')
    print('driver %s' % (os.path.basename(os.readlink(driver)) if os.path.islink(driver)
                         else 'none'))

    cfg = open(os.path.join(dev, 'config'), 'rb').read()
    print('\n--- configuration space')
    for i in range(0, len(cfg), 16):
        print('%02x: %s' % (i, ' '.join('%02x' % b for b in cfg[i:i + 16])))
    print('--- resources')
    print(open(os.path.join(dev, 'resource')).read().rstrip())
    win = struct.unpack_from('<I', cfg, 0x80)[0]
    win2 = struct.unpack_from('<I', cfg, 0x70)[0]
    print('\nmovable windows: first %#010x, second %#010x' % (win, win2))

    fd = os.open(os.path.join(dev, 'resource0'), os.O_RDONLY | os.O_SYNC)
    try:
        bar = mmap.mmap(fd, BAR0_SIZE, mmap.MAP_SHARED, mmap.PROT_READ)
    except (OSError, ValueError) as e:
        sys.exit('cannot map BAR0: %s (kernel lockdown? memory space disabled?)' % e)

    def r32(off):
        return struct.unpack_from('<I', bar, off)[0]

    def r16(off):
        return struct.unpack_from('<H', bar, off)[0]

    if r32(CC) == 0xffffffff:
        sys.exit('the card does not answer (register reads give 0xffffffff): powered down?')

    print('\n--- ChipCommon')
    for off, text in CC_REGS:
        print('CC(%#05x) = %#010x   %s' % (off, r32(CC + off), text))
    cid = r32(CC)
    print('chip %#x revision %d package %d' % (cid & 0xffff, cid >> 16 & 0xf, cid >> 20 & 0xf))

    print('\n--- PCIe core')
    for off in (0x000, 0x004, 0x1e0):
        print('PCIE(%#05x) = %#010x' % (off, r32(PCIE + off)))

    # The 802.11 core is expected at 0x18001000 with its wrapper at 0x18101000.
    # Its registers are only read if the driver has left both windows there
    # and the wrapper says that the core is out of reset and has its clock: a
    # core without clock does not answer.
    print('\n--- 802.11 core')
    if win != 0x18001000 or win2 != 0x18101000:
        print('the windows do not point to the 802.11 core and its wrapper: skipped')
    else:
        ioctrl = r32(0x1000 + 0x408)
        iostatus = r32(0x1000 + 0x500)
        resetctrl = r32(0x1000 + 0x800)
        print('wrapper: ioctrl %#x, iostatus %#x, resetctrl %#x' % (ioctrl, iostatus, resetctrl))
        if resetctrl & 1 or not ioctrl & 1:
            print('the core is in reset or has no clock (interface down?): skipped')
        else:
            for off, size, text in D11_REGS:
                v = r32(off) if size == 4 else r16(off)
                print('D11(%#05x) = %#0*x   %s' % (off, 2 + 2 * size, v, text))

    # SROM (or OTP, if the card has no SROM): window of 16 bit words in ChipCommon
    print('\n--- SROM/OTP window, CC(0x800), 16 bit words')
    words = [r16(CC + 0x800 + 2 * i) for i in range(256)]
    for i in range(0, 256, 8):
        print('%03d: %s' % (i, ' '.join('%04x' % w for w in words[i:i + 8])))
    sys.stdout.flush()
    with open('bcm4360-srom-window.bin', 'wb') as f:
        f.write(struct.pack('<256H', *words))
    sys.stderr.write('the 256 words were also written to bcm4360-srom-window.bin\n')


if __name__ == '__main__':
    main()
