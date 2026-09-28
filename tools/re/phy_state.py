#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Where the object keeps the state of its PHY layer, for the comparison tests:
the values the object made from the variables of the board are read from its
memory and compared with those of the open code.

The places are those of docs/re/spec/acphy-attach.md ("Data").  This file
describes the memory of the object; it is for the tests, not for
implementers.
"""

PA_BASES = (0xe04, 0xe2c, 0xe54)

POWER_OFFSETS = [
    ('cckbw202gpo', 0xc4c, 2), ('cckbw20ul2gpo', 0xc4e, 2),
    ('mcsbw202gpo', 0xc5c, 4), ('mcsbw402gpo', 0xc64, 4),
    ('mcsbw205glpo', 0xc6c, 4), ('mcsbw205gmpo', 0xc70, 4), ('mcsbw205ghpo', 0xc74, 4),
    ('mcsbw405glpo', 0xcd8, 4), ('mcsbw405gmpo', 0xcdc, 4), ('mcsbw405ghpo', 0xce0, 4),
    ('mcsbw805glpo', 0xcf0, 4), ('mcsbw805gmpo', 0xcf4, 4), ('mcsbw805ghpo', 0xcf8, 4),
    ('ofdmlrbw202gpo', 0xd14, 2),
    ('sb20in40lrpo', 0xd16, 2), ('sb20in40hrpo', 0xd18, 2),
    ('dot11agduplrpo', 0xd1a, 2), ('dot11agduphrpo', 0xd1c, 2),
    ('mcslr5glpo', 0xd1e, 2), ('mcslr5gmpo', 0xd20, 2), ('mcslr5ghpo', 0xd22, 2),
    ('sb20in80and160lr5glpo', 0xd24, 2), ('sb20in80and160lr5gmpo', 0xd26, 2),
    ('sb20in80and160lr5ghpo', 0xd28, 2),
    ('sb20in80and160hr5glpo', 0xd2a, 2), ('sb20in80and160hr5gmpo', 0xd2c, 2),
    ('sb20in80and160hr5ghpo', 0xd2e, 2),
    ('sb40and80lr5glpo', 0xd30, 2), ('sb40and80lr5gmpo', 0xd32, 2),
    ('sb40and80lr5ghpo', 0xd34, 2),
    ('sb40and80hr5glpo', 0xd36, 2), ('sb40and80hr5gmpo', 0xd38, 2),
    ('sb40and80hr5ghpo', 0xd3a, 2),
]


def table(cores=2):
    """name -> (structure, [offset of element 0, 1, ...], size in bytes).

    Names are those of the variables; names that start with a dot are state
    that is not a variable.  Values are compared as the object stores them
    (after the conversion the specification gives).
    """
    t = {}

    def add(name, struct, offsets, size):
        t[name] = (struct, [offsets] if isinstance(offsets, int) else list(offsets), size)

    add('.phy_type', 'pi', 0x160, 4)
    add('.phy_rev', 'pi', 0x164, 4)
    add('.cores', 'pi', 0x168, 1)
    add('.radio_id', 'pi', 0x16a, 2)
    add('.radio_rev', 'pi', 0x16c, 1)
    add('.chanspec', 'pi', 0x17e, 2)
    add('.write_limit', 'pi', 0x228, 2)
    add('.xtal_hz', 'pi', 0xc24, 4)
    add('.interference_2g', 'sh', 0x84, 4)
    add('.interference_5g', 'sh', 0x88, 4)
    add('.rxgainerr2g_empty', 'pi', 0x1e7, 1)
    add('.rxgainerr5g_empty', 'pi', [0x1ec + 5 * k for k in range(4)], 1)
    add('.otp_word16_bits8_12', 'pi_ac', 0x8e2, 2)

    add('phycal_tempdelta', 'pi', 0xf9c, 1)
    add('subband5gver', 'sh', 0x4c, 4)
    add('extpagain2g', 'sh', 0xb0, 4)
    add('extpagain5g', 'sh', 0xac, 4)
    add('femctrl', 'pi_ac', 0x342, 1)
    add('rpcal2g', 'sh', 0xcc, 2)
    for k in range(4):
        add('rpcal5gb%d' % k, 'sh', 0xce + 2 * k, 2)
    add('txidxcap2g', 'sh', 0xd6, 1)
    add('txidxcap5g', 'sh', 0xd7, 1)
    add('pdgain2g', 'pi_ac', 0x410, 1)
    add('pdgain5g', 'pi_ac', 0x411, 1)
    add('cckdigfilttype', 'pi_ac', 0x8fe, 1)
    for c in range(cores):
        for b, band in enumerate(('2g', '5g', '5gm', '5gh')):
            base = 0x3e0 + 12 * b + 3 * c
            add('rxgains%selnagaina%d' % (band, c), 'pi_ac', base, 1)
            add('rxgains%strisoa%d' % (band, c), 'pi_ac', base + 1, 1)
            add('rxgains%strelnabypa%d' % (band, c), 'pi_ac', base + 2, 1)
    for c in range(3):
        add('maxp2ga%d' % c, 'pi', 0xe7c + 5 * c, 1)
        add('maxp5ga%d' % c, 'pi', [0xe7c + 5 * c + 1 + k for k in range(4)], 1)
        add('pa2ga%d' % c, 'pi', [b + 10 * c for b in PA_BASES], 2)
        add('pa5ga%d' % c, 'pi', [PA_BASES[i % 3] + 10 * c + 2 * (i // 3 + 1)
                                  for i in range(12)], 2)
        add('pdoffset40ma%d' % c, 'pi', 0xe90 + 2 * c, 2)
        add('pdoffset80ma%d' % c, 'pi', 0xe98 + 2 * c, 2)
        add('pdoffset2g40ma%d' % c, 'pi', 0xea0 + c, 1)
        add('pdoffsetcckma%d' % c, 'pi', 0xece + c, 1)
        add('rxgainerr2ga%d' % c, 'pi', 0x1e3 + c, 1)
        add('rxgainerr5ga%d' % c, 'pi', [0x1e8 + 5 * k + c for k in range(4)], 1)
        add('rssicorrnorm_c%d' % c, 'pi_ac', [0x3a8 + 2 * c + i for i in range(2)], 1)
        add('rssicorrnorm5g_c%d' % c, 'pi_ac', [0x3b0 + 12 * c + i for i in range(12)], 1)
    for c in range(cores):
        add('noiselvl2ga%d' % c, 'pi', 0x1fc + c, 1)
        add('noiselvl5ga%d' % c, 'pi', [0x200 + 4 * k + c for k in range(4)], 1)
    add('pdoffset2g40mvalid', 'pi', 0xea4, 1)
    add('tssifloor2g', 'pi', 0xea6, 2)
    add('tssifloor5g', 'pi', [0xea8 + 2 * k for k in range(4)], 2)
    for name, off, size in POWER_OFFSETS:
        add(name, 'pi', off, size)
    add('rawtempsense', 'pi', 0x210, 2)
    add('tempthresh', 'pi', 0xc2e, 1)
    add('temps_hysteresis', 'pi', 0xc30, 1)
    add('tempoffset', 'pi', 0xc35, 1)
    add('txpwrbckof', 'pi', 0x10a0, 1)
    add('tssilimucod', 'pi', 0x10b8, 1)
    add('rssicorrnorm', 'pi', 0x10fe, 1)
    add('rssicorratten', 'pi', 0x10ff, 1)
    add('rssicorrnorm5g', 'pi', [0x1100 + i for i in range(3)], 1)
    add('rssicorratten5g', 'pi', [0x1103 + i for i in range(3)], 1)
    add('rssicorrperrg2g', 'pi', [0x1106 + i for i in range(5)], 1)
    add('rssicorrperrg5g', 'pi', [0x110b + i for i in range(5)], 1)
    add('5g_cga', 'pi', [0x1110 + i for i in range(24)], 1)
    add('2g_cga', 'pi', [0x1128 + i for i in range(14)], 1)
    for i, name in enumerate(('swctrlmap_2g', 'swctrlmapext_2g', 'swctrlmap_5g',
                              'swctrlmapext_5g')):
        add(name, 'pi_ac', [0x358 + 20 * i + 4 * k for k in range(5)], 4)
    return t


def structures(m, pi):
    return {'pi': pi, 'pi_ac': m.r64(pi + 0x138), 'sh': m.r64(pi + 0x20)}


def read(m, pi, entry, index):
    """Element `index` of an entry of table(), as an unsigned number."""
    struct, offsets, size = entry
    base = structures(m, pi)[struct]
    return int.from_bytes(m.read(base + offsets[index], size), 'little')


def board(m, pi):
    """What the platform tells the open PHY about chip and board
    (struct bcm4360_phy_board), from the state of the object."""
    sh = m.r64(pi + 0x20)
    return {
        'chip': m.r32(sh + 0x3c), 'chiprev': m.r32(sh + 0x40), 'chippkg': m.r32(sh + 0x44),
        'corerev': m.r32(sh + 0x28), 'sromrev': m.r32(sh + 0x48),
        'boardtype': m.r32(sh + 0x58), 'boardrev': m.r32(sh + 0x5c),
        'boardvendor': m.r32(sh + 0x60), 'boardflags': m.r32(sh + 0x64),
        'boardflags2': m.r32(sh + 0x68), 'xtal_hz': m.r32(pi + 0xc24),
        'pci_vendor': m.r16(sh + 0x38), 'pci_device': m.r16(sh + 0x3a),
    }
