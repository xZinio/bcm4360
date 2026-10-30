#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Measure the closed-source surface of the object by layer.

The question "how much closed code does an open driver still have to replace"
is not the size of the 7.35 MB object: most of its code is either for other
chips, or is the 802.11 stack that the kernel (mac80211) provides, or is crypto
the kernel provides. This tool sums the `.text` of the object's functions per
source file (from `re-out/index.tsv`, column 3 is the file) and groups the
files into layers, so the part an open BCM4360 driver actually needs, and how
much of it `open/` already reproduces, can be read off and tracked as each
increment lands.

    python surface_map.py [INDEX.tsv]      (default: ../../re-out/index.tsv)

`index.tsv` is built by `index.py build` and lives under `re-out/` (ignored);
this is a coordinator tool, not part of the open code.
"""
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_INDEX = os.path.join(HERE, '..', '..', 're-out', 'index.tsv')

# Files already reproduced in open/ (whole-file or dominant). Grow this as
# increments land; wlc_bmac.c / wlc_phy_cmn.c stay "partial" until their
# non-access parts are done too.
OPEN_DONE = {
    'wlc_phy_ac.c',      # AC-PHY: radio 2069, attach/init/chanspec/rxgain/desense/txpower/cal
    'hndpmu.c',          # PMU / clocks
}
PARTIAL = {
    'wlc_phy_cmn.c',     # register access + attach helpers done; common/iovar remainder open
    'wlc_bmac.c',        # SHM access done; control primitives in progress; bring-up/data open
}
# Hardware-layer code an open (mac80211) driver still needs from the blob.
HW_REMAINING = {
    'wlc_hw.c',
    'siutils.c', 'aiutils.c', 'sbutils.c', 'nicpci.c', 'sflash.c',   # backplane / bus
    'hnddma.c',                                                      # DMA engines
    'bcmsrom.c', 'bcmotp.c', 'nvram_ro.c',                          # SROM / OTP / NVRAM
    'wlc_phy_shim.c', 'wlc_phy_iovar.c',                            # PHY glue to the MAC
}
# Other chips' PHYs - dead code for the 4360, never needed.
OTHER_PHY = {
    'wlc_phy_n.c', 'wlc_phy_ht.c', 'wlc_phy_lcn.c', 'wlc_phy_lcn40.c',
    'wlc_phy_lp.c', 'wlc_phy_abg.c', 'wlc_phy_ssn.c', 'wlc_phy_radio_n.c',
    'wlc_phy_extended_n.c',
}
# Crypto / generic utility - supplied by the kernel (mac80211, crypto API).
KERNEL_PROVIDED = {
    'aes.c', 'aeskeywrap.c', 'hmac.c', 'md5.c', 'passhash.c', 'prf.c', 'rc4.c',
    'rijndael-alg-fst.c', 'sha1.c', 'tkhash.c', 'tkmic.c', 'wep.c', 'bcmwpa.c',
    'qmath.c', 'bcmutils.c', 'wlc_utils.c', 'bcm_mpool.c', 'bcm_notif.c',
}

LAYERS = [
    ('1 open/ (done)', OPEN_DONE),
    ('2 partial (access done)', PARTIAL),
    ('3 hw-layer REMAINING', HW_REMAINING),
    ('4 other-chip PHY (dead for 4360)', OTHER_PHY),
    ('5 kernel-provided (mac80211/crypto)', KERNEL_PROVIDED),
]


def layer_of(f):
    for name, files in LAYERS:
        if f in files:
            return name
    return '6 upper 802.11 stack (mac80211 replaces)'


def main(index=DEFAULT_INDEX):
    by_file = collections.Counter()
    with open(index, encoding='utf8') as fh:
        for line in fh:
            p = line.rstrip('\n').split('\t')
            if len(p) < 4:
                continue
            try:
                by_file[p[2]] += int(p[1])
            except ValueError:
                continue

    by_layer = collections.Counter()
    for f, sz in by_file.items():
        by_layer[layer_of(f)] += sz
    total = sum(by_layer.values()) or 1

    print('TOTAL .text of code functions: %d bytes (%.0f KB)\n' % (total, total / 1024))
    for lay in sorted(by_layer):
        print('%-42s %8d  %5.1f%%' % (lay, by_layer[lay], 100 * by_layer[lay] / total))

    done = by_layer['1 open/ (done)']
    partial = by_layer['2 partial (access done)']
    remaining = by_layer['3 hw-layer REMAINING']
    hw_total = (done + partial + remaining) or 1
    print('\n== Hardware layer an open driver must get from the blob ==')
    print('  done (open/):            %7d  %5.1f%%' % (done, 100 * done / hw_total))
    print('  partial (access slice):  %7d  %5.1f%%' % (partial, 100 * partial / hw_total))
    print('  remaining:               %7d  %5.1f%%' % (remaining, 100 * remaining / hw_total))
    print('  hw-layer total:          %7d' % hw_total)

    print('\n== REMAINING hw-layer files, largest first ==')
    for f, sz in sorted(by_file.items(), key=lambda x: -x[1]):
        if layer_of(f).startswith('3'):
            print('  %-18s %7d' % (f, sz))
    return 0


if __name__ == '__main__':
    raise SystemExit(main(*(sys.argv[1:2] or [DEFAULT_INDEX])))
