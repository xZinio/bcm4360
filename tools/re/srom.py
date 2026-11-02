#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
SROM images as the driver core reads them.

The layout of the SROM (which variable sits in which word, for which SROM
revision) is a table inside the object; it is read from there, so this file
contains no layout of its own.

    srom.py table [REV]            the layout, optionally for one revision
    srom.py decode IMAGE           variables of a binary image (16 bit LE words)
    srom.py encode VARS IMAGE      build a revision 11 image from name=value lines

An image made with `encode` has a valid signature, revision and CRC, so the
driver accepts it.
"""
import struct
import sys

from blob import Blob, BASES, default_path

# flags of a table entry
SRFL_MORE = 0x001       # value continues in the next entry (next 16 bits)
SRFL_NOFFS = 0x002      # do not write the variable if the word is 0xffff
SRFL_PRHEX = 0x004      # print as hexadecimal
SRFL_PRSIGN = 0x008     # signed
SRFL_CCODE = 0x010      # two letter country code
SRFL_ETHADDR = 0x020    # MAC address in three words
SRFL_LEDDC = 0x040      # LED duty cycle
SRFL_NOVAR = 0x080      # word is read but makes no variable
SRFL_ARRAY = 0x100      # element of an array, entries follow until one without the flag

ENTRY = 24
PCI_SROMVARS = 0x1b00           # offsets in .rodata
PERPATH_SROMVARS = 0x50d0

SROM11_WORDS = 234
SROM11_SIGN_OFF = 64
SROM11_SIGNATURE = 0x0634
SROM11_CRCREV = 233
CRC8_GOOD = 0x9f                # CRC of an image including its CRC byte

# per-path blocks: (revisions, first word, words per path, paths)
PATHS = ((range(11, 32), 108, 20, 3),
         (range(8, 11), 96, 16, 4),
         (range(4, 8), 64, 23, 4))


class Entry:
    __slots__ = ('name', 'revmask', 'flags', 'off', 'mask')

    def __repr__(self):
        return '%-22s rev %08x flags %03x word %3d mask %04x' % (
            self.name, self.revmask, self.flags, self.off, self.mask)


def read_table(b, off):
    out = []
    rb = BASES['.rodata']
    p = rb + off
    while True:
        raw = b.read(p, ENTRY)
        revmask, flags, woff, mask = struct.unpack_from('<IIHH', raw, 8)
        r = b.relocs.get(p)
        if r is None:
            break               # terminator: no name
        e = Entry()
        e.name = b.cstring(r[1])
        e.revmask, e.flags, e.off, e.mask = revmask, flags, woff, mask
        out.append(e)
        p += ENTRY
    return out


def crc8(data, crc=0xff):
    """CRC-8 of the SROM (polynomial x^8 + x^7 + x^6 + x^4 + x^2 + 1, reflected)."""
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xab if crc & 1 else crc >> 1
    return crc


def detect_rev(words):
    """The revision of an image as srom_var_init finds it (srom-otp.md, step 5):
    the first of 234, 220, 64 words whose CRC is good decides, by its
    signature; None if none is good (the driver then tries the OTP, NVRAM)."""
    raw = b''.join((w & 0xffff).to_bytes(2, 'little') for w in words)
    if not words or words[0] == 0xffff:
        return None
    for n in (234, 220, 64):
        if len(words) < n or crc8(raw[:2 * n]) != CRC8_GOOD:
            continue
        if n == 234:
            return words[233] & 0xff if words[64] == SROM11_SIGNATURE else 0
        if n == 220:
            return words[219] & 0xff if 0x5372 in (words[32], words[64]) else 0
        rev = words[63] & 0xff
        return 1 if rev == 0x10 else rev
    return None


def shift_of(mask):
    s = 0
    while mask and not mask & 1:
        mask >>= 1
        s += 1
    return s


class Layout:
    def __init__(self, blob=None):
        b = blob or Blob(default_path())
        self.common = read_table(b, PCI_SROMVARS)
        self.perpath = read_table(b, PERPATH_SROMVARS)

    def decode(self, words, rev=None):
        """Variables of an image, in the order and form the driver makes them
        (docs/re/spec/srom-otp.md, "Conversion of an image into variables").
        rev: the revision; None: found as srom_var_init finds it."""
        if rev is None:
            rev = detect_rev(words)
            if rev is None:
                raise ValueError('no valid image: blank, or no CRC matches for 234, 220 '
                                 'or 64 words (the driver would try the OTP / NVRAM)')
        out = []
        self._room = [0x1000]          # what is left of the driver's work buffer
        self._append(out, 'sromrev', '%d' % rev)
        self._decode(self.common, words, rev, 0, '', out, False)
        for base, p in self.path_bases(rev):
            self._decode(self.perpath, words, rev, base, str(p), out, True)
        return out

    def _append(self, out, name, text):
        """varbuf_append: a repeated name moves to the end; the buffer has
        0x1000 bytes and an append that does not fit ends all appending."""
        n = len(name) + 1 + len(text)
        if self._room[0] <= 1:
            return
        if n > self._room[0] - 2:
            self._room[0] = 0
            return
        for k, (old, val) in enumerate(out):
            if old == name:
                self._room[0] += len(old) + 1 + len(val) + 1
                del out[k]
                break
        out.append((name, text))
        self._room[0] -= n + 1

    @staticmethod
    def path_bases(rev):
        for revs, first, step, count in PATHS:
            if rev in revs:
                return [(first + step * p, p) for p in range(count)]
        return []

    def _decode(self, table, words, rev, base, suffix, out, perpath):
        """One table walk, (A) common or (B) per path, of srom-otp.md."""
        def word(k):
            return words[k] if k < len(words) else 0

        def field(mask):
            if not mask:
                return 0, 0
            lo = shift_of(mask)
            return lo, mask.bit_length() - lo

        mask_rev = 1 << (rev & 31)
        i, n = 0, len(table)
        array = None                    # [name, element texts] while one is built
        while i < n:
            e = table[i]
            i += 1
            if not e.revmask & mask_rev or e.flags & SRFL_NOVAR:
                continue
            if not perpath and e.flags & SRFL_ETHADDR:
                ws = [word(base + e.off + k) for k in range(3)]
                self._append(out, e.name, ':'.join('%02x' % x for w3 in ws
                                                    for x in (w3 >> 8, w3 & 0xff)))
                continue
            if rev > 9 and e.flags & SRFL_ARRAY and array is None:
                array = [e.name, []]
            lo, width = field(e.mask)
            value = (word(base + e.off) & e.mask) >> lo
            last = e
            while not perpath and last.flags & SRFL_MORE and i < n:
                c = table[i]
                i += 1
                if c.off:
                    clo, cw = field(c.mask)
                    value += ((word(c.off) & c.mask) >> clo) << width
                    width += cw
                last = c
            fl = e.flags
            if fl & SRFL_NOFFS and value == ((1 << (width & 31)) - 1) & 0xffffffff:
                continue
            if rev <= 9 and last.flags & SRFL_ARRAY:
                # step over the rest of the array and one entry more; the first
                # element is printed as a scalar under the array's name
                while i < n and table[i].flags & SRFL_ARRAY:
                    i += 1
                i += 2
            elif array is not None:
                if fl & SRFL_PRHEX:
                    array[1].append('0x%x' % value)
                elif fl & SRFL_PRSIGN and width and value >> (width - 1) & 1:
                    array[1].append('%d' % (value - (1 << width)))
                else:
                    array[1].append('%u' % value)
                if last.flags & SRFL_ARRAY:
                    continue
                self._append(out, array[0] + suffix, ','.join(array[1]))
                array = None
                continue
            if perpath:
                # PRSIGN is ignored for per-path scalars
                self._append(out, e.name + suffix,
                             ('0x%x' if fl & SRFL_PRHEX else '%d') % value)
            elif fl & SRFL_CCODE:
                # a zero byte is stored as it is: the driver's list then ends
                # there for getvar (srom-otp.md, open questions)
                self._append(out, 'ccode', '' if value == 0 else
                             chr(value >> 8 & 0xff) + chr(value & 0xff))
            elif fl & SRFL_LEDDC:
                v = ((value & 0xff) << 8) | ((value >> 8) << 24)
                v &= 0xffffffff
                self._append(out, 'leddc', '%d' % (v - (1 << 32) if v >> 31 else v))
            elif fl & SRFL_PRHEX:
                self._append(out, e.name, '0x%x' % value)
            elif fl & SRFL_PRSIGN and width and value >> (width - 1) & 1:
                self._append(out, e.name, '%d' % (value - (1 << width)))
            else:
                self._append(out, e.name, '%u' % value)

    def encode(self, variables, rev=11):
        """Image for a dict of variables; per-path names carry the path digit."""
        if rev != 11:
            raise ValueError('only revision 11 images can be made')
        words = [0xffff] * SROM11_WORDS
        used = {'sromrev'}
        self._encode(self.common, variables, rev, 0, '', words, used)
        for base, p in self.path_bases(rev):
            self._encode(self.perpath, variables, rev, base, str(p), words, used)
        unknown = sorted(set(variables) - used)
        if unknown:
            raise ValueError('not in the SROM layout of revision %d: %s'
                             % (rev, ' '.join(unknown)))
        # word 0 belongs to the PCIe core's own configuration data; the driver
        # only requires that it is not blank (0xffff means "SROM not programmed")
        if words[0] == 0xffff:
            words[0] = 0x2801
        words[SROM11_SIGN_OFF] = SROM11_SIGNATURE
        words[SROM11_CRCREV] = rev
        raw = struct.pack('<%dH' % len(words), *words)
        crc = crc8(raw[:-1]) ^ 0xff
        words[SROM11_CRCREV] = rev | crc << 8
        raw = struct.pack('<%dH' % len(words), *words)
        assert crc8(raw) == CRC8_GOOD
        return words

    def _encode(self, table, variables, rev, base, suffix, words, used):
        i = 0
        n = len(table)
        while i < n:
            e = table[i]
            i += 1
            group = [e]
            while group[-1].flags & SRFL_MORE and i < n:
                group.append(table[i])
                i += 1
            first = group[0]
            if not first.revmask & (1 << rev):
                continue
            # words the driver reads without making a variable (devid) can be set too
            name = first.name + suffix
            if first.flags & SRFL_ARRAY:
                # collect the whole array: entries up to the first without the flag
                elems = [group]
                while elems[-1][0].flags & SRFL_ARRAY and i < n:
                    g = [table[i]]
                    i += 1
                    while g[-1].flags & SRFL_MORE and i < n:
                        g.append(table[i])
                        i += 1
                    elems.append(g)
                if name not in variables:
                    continue
                used.add(name)
                vals = [int(x, 0) for x in str(variables[name]).split(',')]
                if len(vals) != len(elems):
                    raise ValueError('%s needs %d values' % (name, len(elems)))
                for g, v in zip(elems, vals):
                    self._put(g, v, base, words)
                continue
            if name not in variables:
                continue
            used.add(name)
            v = variables[name]
            if first.flags & SRFL_ETHADDR:
                octets = [int(x, 16) for x in str(v).replace('-', ':').split(':')]
                for k in range(3):
                    words[base + first.off + k] = octets[2 * k] << 8 | octets[2 * k + 1]
                continue
            if first.flags & SRFL_CCODE:
                v = 0 if not v else ord(v[0]) << 8 | ord(v[1])
            elif not isinstance(v, int):
                v = int(str(v), 0)
            self._put(group, v, base, words)

    @staticmethod
    def _put(group, v, base, words):
        total = sum(bin(e.mask).count('1') for e in group)
        v &= (1 << total) - 1
        for e in group:
            bits = bin(e.mask).count('1')
            part = v & ((1 << bits) - 1)
            v >>= bits
            w = words[base + e.off]
            words[base + e.off] = (w & ~e.mask) | (part << shift_of(e.mask))


def load_vars(path):
    out = {}
    with open(path) as f:
        for line in f:
            line = line.split('#')[0].strip()
            if line:
                k, v = line.split('=', 1)
                out[k.strip()] = v.strip()
    return out


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    lay = Layout()
    cmd = sys.argv[1]
    if cmd == 'table':
        rev = int(sys.argv[2]) if len(sys.argv) > 2 else None
        for title, t in (('common', lay.common), ('per path', lay.perpath)):
            print('# ' + title)
            more = False            # continuation entries belong to their head
            for e in t:
                if rev is None or e.revmask & (1 << rev) or more:
                    print(e)
                    more = bool(e.flags & SRFL_MORE)
    elif cmd == 'decode':
        raw = open(sys.argv[2], 'rb').read()
        words = list(struct.unpack('<%dH' % (len(raw) // 2), raw[:len(raw) & ~1]))
        for k, v in lay.decode(words):
            if '\0' in v:
                print('%s=%s' % (k, v.split('\0')[0]))
                print('# the driver stores a zero byte in %s here: getvar sees no '
                      'variable after it' % k)
            else:
                print('%s=%s' % (k, v))
    elif cmd == 'encode':
        words = lay.encode(load_vars(sys.argv[2]))
        with open(sys.argv[3], 'wb') as f:
            f.write(struct.pack('<%dH' % len(words), *words))
        print('%d words written to %s' % (len(words), sys.argv[3]))
    else:
        sys.exit(__doc__)
