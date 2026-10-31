#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
The hybrid driver in the emulator: Broadcom's object with layers of it done
by the open code, compared with the object alone.

Every call from one part of the object into another is a relocation against
the callee's symbol (checked by `hybrid.py redirect`: no call bypasses it).
A bridge in hybrid/ defines functions under the object's names, with the
object's arguments, and passes the calls on to the open code in open/. Here
the object's relocations against those names are pointed at the bridge -
what the linker does in the HYBRID build of the module once the names are
made weak in a copy of the object (see hybrid/README.md). Then the session
the PHY scenarios use (attach, up, nine channel changes, watchdog, down) is
run, and every access of the bus is compared with the same session of the
object alone: the open layer must leave the card's view of the driver
unchanged.

    hybrid.py list                          the layers that can be swapped
    hybrid.py run LAYER... [--show N]       hybrid session vs the object alone
    hybrid.py metric [LAYER...]             object code executed, with and without the layers
    hybrid.py redirect LAYER...             how the object's references reach the bridge
    hybrid.py symbols LAYER...              symbol lists for the HYBRID build (weaken / globalize)

This is a coordinator tool; the bridges contain no behaviour of the object,
only the marshalling between its structures and the open interfaces.
"""
import argparse
import collections
import difflib
import os
import struct
import subprocess
import sys
import time

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from elftools.elf.sections import SymbolTableSection

import unicorn as uc

import ab
from blob import BASES
from elfobj import Loader, R_X86_64_64, R_X86_64_PC32, R_X86_64_PLT32, R_X86_64_32S
from emu import EmuError

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
BRIDGE_SRC = os.path.join(ROOT, 'hybrid')
HYBRID_OBJ = os.path.join(ROOT, 're-out', 'hybrid')

# A layer: the bridge files (hybrid/) and the open files (open/) it needs.
LAYERS = {
    'access': {
        'doc': 'register access primitives: PHY and radio registers, PHY tables, '
               'shared memory (open/hw/access.c)',
        'bridge': ['access.c'],
        'open': ['hw/access.c'],
    },
    'pmu': {
        'doc': 'power management unit and clocks: indirect registers, PLL, resources, '
               'clock queries, OTP and radio supply (open/chip/pmu.c)',
        'bridge': ['pmu.c'],
        'open': ['chip/pmu.c'],
    },
    'mac': {
        'doc': 'MAC core control primitives: MAC control register, microcode wake '
               'override, suspend/enable, host flags (open/mac/bmac_ctrl.c, bmac_mhf.c)',
        'bridge': ['mac.c'],
        'open': ['mac/bmac_ctrl.c', 'mac/bmac_mhf.c', 'hw/access.c'],
    },
    'utils': {
        'doc': 'board variables, number parsing, bit count, CRC-8 (open/lib/utils.c)',
        'bridge': ['utils.c'],
        'open': ['lib/utils.c'],
    },
}

DEFAULT_STEPS = None     # filled from phy_scenarios.SESSIONS['full'] on first use

# The data of the object the open PHY asks for by name (hw_fw_data() of hw.h):
# every name the open sources can pass (string literals in open/phy/*.c).
# `hybrid.py fwdata --write` makes hybrid/fwdata.c from it: declarations and
# sizes only; the data itself is linked from the object.
FW_NAMES = '''
acphy_est_pwr_lut_core0_rev0 acphy_est_pwr_lut_core1_rev0 acphy_est_pwr_lut_core2_rev0
acphy_iq_lut_core0_rev0 acphy_iq_lut_core2_rev0
acphy_loft_lut_core0_rev0 acphy_loft_lut_core1_rev0 acphy_loft_lut_core2_rev0
acphy_mcs_tbl_rev0 acphy_noise_shaping_tbl_rev0
acphy_papd_cal_scalars_tbl_core0_rev0 acphy_papd_cal_scalars_tbl_core1_rev0
acphy_papd_cal_scalars_tbl_core2_rev0
acphy_papd_comp_epsilon_tbl_core0_rev0 acphy_papd_comp_epsilon_tbl_core1_rev0
acphy_papd_comp_epsilon_tbl_core2_rev0
acphy_papd_comp_rfpwr_tbl_core0_rev0 acphy_papd_comp_rfpwr_tbl_core1_rev0
acphy_papd_comp_rfpwr_tbl_core2_rev0
acphy_phasetrack_tbl_rev0 acphy_rx_evm_shaping_tbl_rev0 acphy_tx_evm_tbl_rev0
acphy_txgain_epa_2g_2069rev0 acphy_txgain_epa_2g_2069rev4 acphy_txgain_epa_2g_2069rev4_id1
acphy_txgain_epa_5g_2069rev0 acphy_txgain_epa_5g_2069rev4
acphy_txgain_ipa_2g_2069rev0 acphy_txgain_ipa_5g_2069rev0
acphy_txv_for_spexp chan_tuning_2069rev3 chan_tuning_2069rev4
prefregs_2069_rev3 prefregs_2069_rev4 rx_farrow_tbl tx_farrow_dac1_tbl
'''.split()


# ------------------------------------------------------------------ build
def build(layers):
    """Compile the bridges and the open files of the layers; returns
    (bridge objects, open objects)."""
    zig = ab.find_zig()
    os.makedirs(HYBRID_OBJ, exist_ok=True)
    inc = ['-I', os.path.join(ROOT, 'open', 'include'), '-I', BRIDGE_SRC]
    bridge, opened = [], []
    seen = set()
    for name in layers:
        lay = LAYERS[name]
        for kind, files, base, out in (('bridge', lay['bridge'], BRIDGE_SRC, bridge),
                                       ('open', lay['open'], os.path.join(ROOT, 'open'), opened)):
            for f in files:
                src = os.path.join(base, f)
                if src in seen:
                    continue
                seen.add(src)
                obj = os.path.join(HYBRID_OBJ, kind + '_' + f.replace('/', '_')[:-2] + '.o')
                r = subprocess.run([zig, 'cc'] + ab.CFLAGS + inc + ['-o', obj, src],
                                   capture_output=True, text=True)
                if r.returncode != 0 or r.stderr.strip():
                    sys.stderr.write(r.stderr)
                if r.returncode != 0:
                    raise SystemExit('does not compile: ' + os.path.relpath(src, ROOT))
                out.append(obj)
    return bridge, opened


# ------------------------------------------------------------------ loading
class HybridLoader(Loader):
    """The open objects may also call functions of the object itself."""

    def resolve(self, name):
        try:
            return super().resolve(name)
        except EmuError:
            syms = self.m.blob.byname.get(name)
            if syms and len(syms) == 1:
                return syms[0].addr
            raise


def blob_relocations(blob):
    """(field address, type, addend, symbol name) of every relocation of the object."""
    out = []
    with open(blob.path, 'rb') as f:
        elf = ELFFile(f)
        secs = list(elf.iter_sections())
        syms = list([s for s in secs if isinstance(s, SymbolTableSection)][0].iter_symbols())
        for s in secs:
            if not isinstance(s, RelocationSection):
                continue
            tgt = secs[s['sh_info']].name
            if tgt not in BASES:
                continue
            for r in s.iter_relocations():
                out.append((BASES[tgt] + r['r_offset'], r['r_info_type'], r['r_addend'],
                            syms[r['r_info_sym']].name, r['r_info_sym']))
    return out


def overrides_of(blob, bridge_objs):
    """Names the bridges define that are functions of the object: name -> address."""
    funcs = {s.name for s in blob.syms if s.kind == 'F'}
    out = {}
    for o in bridge_objs:
        for name, addr in o.symbols.items():
            if name in funcs and o.sizes.get(name):
                out[name] = addr
    return out


def redirect(m, new):
    """Point the object's relocations against the names in `new` at new[name]."""
    count = collections.Counter()
    for P, t, A, name, _ in blob_relocations(m.blob):
        S = new.get(name)
        if S is None:
            continue
        if t in (R_X86_64_PC32, R_X86_64_PLT32):
            v = S + A - P
            if not -2**31 <= v < 2**31:
                raise EmuError('%s: displacement out of range' % name)
            m.write(P, struct.pack('<i', v))
        elif t == R_X86_64_64:
            m.write(P, struct.pack('<Q', (S + A) & 0xffffffffffffffff))
        elif t == R_X86_64_32S:
            m.write(P, struct.pack('<i', S + A))
        else:
            raise EmuError('%s: relocation type %d' % (name, t))
        count[name] += 1
    return count


def direct_calls(blob, targets):
    """Direct calls (e8 rel32) to the addresses in `targets` that carry no
    relocation: references the linker could not redirect."""
    fields = {P for P, *_ in blob_relocations(blob)}
    text, tb = blob.data['.text'], BASES['.text']
    hits = collections.Counter()
    for i in range(len(text) - 5):
        if text[i] == 0xE8:
            T = tb + i + 5 + struct.unpack_from('<i', text, i + 1)[0]
            if T in targets and tb + i + 1 not in fields:
                hits[targets[T]] += 1
    return hits


# ------------------------------------------------------------------ session
class Hybrid:
    """One driver instance: the object, with the layers' bridges in place if any."""

    def __init__(self, layers=(), coverage=False):
        from run_attach import Driver
        self.d = Driver(echo=False)
        self.m, self.chip = self.d.m, self.d.chip
        self.layers = list(layers)
        self.bridge = self.opened = []
        self.redirected = {}
        self.objects = []
        if layers:
            bridge_paths, open_paths = build(layers)
            self.loader = HybridLoader(self.m)
            self.objects = self.loader.load_all(bridge_paths + open_paths)
            self.bridge = self.objects[:len(bridge_paths)]
            new = overrides_of(self.m.blob, self.bridge)
            self.redirected = redirect(self.m, new)
            self.new = new
        if coverage:
            # basic block -> [size, executions]; the object's .text and the open objects
            self.blocks = {}
            tb = BASES['.text']
            self.m.mu.hook_add(uc.UC_HOOK_BLOCK, self._block, None, tb,
                               tb + self.m.blob.size['.text'] - 1)
            for o in self.objects:
                self.m.mu.hook_add(uc.UC_HOOK_BLOCK, self._block, None, o.base, o.end - 1)
        ab.delays(self.d)

    def _block(self, mu, address, size, user):
        b = self.blocks.get(address)
        if b is None:
            self.blocks[address] = [size, 1]
        else:
            b[1] += 1

    def run(self, steps):
        from phy_scenarios import SESSIONS
        from scenarios import set_channel, blob_pi
        d, m = self.d, self.m
        d.attach()
        if not d.wlc:
            raise EmuError('attach fails (error %d)' % d.err)
        d.iovar_setint('mpc', 0)
        for step in steps:
            if step == 'up':
                if d.up() != 0:
                    raise EmuError('up fails')
            elif step == 'down':
                m.call('wlc_down', d.wlc)
            elif step == 'watchdog':
                m.os.run_timers(m, m.os.now_us // 1000 + 2500)
            elif step == 'cal-run':
                m.call('wlc_phy_cals_acphy', blob_pi(self), 0)
            elif step == 'tempsense':
                m.call('wlc_suspend_mac_and_wait', d.wlc)
                m.call('wlc_phy_tempsense_acphy', blob_pi(self))
                m.call('wlc_enable_mac', d.wlc)
            else:
                set_channel(self, step)
        return self.chip.trace

    def coverage(self):
        """What ran: {'blob': {function: [static bytes, dynamic bytes]},
        'open': {object file: [static, dynamic]}, 'files': {source file of the
        object: [static, dynamic]}}. Static: distinct bytes of code executed;
        dynamic: bytes executed counting every execution (a measure of the
        instructions executed)."""
        from modmap import attribute, load_pins
        b = self.m.blob
        if b.funcs is None:
            b.discover_functions()
        where = attribute(b, load_pins(os.path.join(HERE, 'modmap.txt')))
        tb, te = BASES['.text'], BASES['.text'] + b.size['.text']
        blob = collections.defaultdict(lambda: [0, 0])
        files = collections.defaultdict(lambda: [0, 0])
        opened = collections.defaultdict(lambda: [0, 0])
        for a, (size, n) in self.blocks.items():
            if tb <= a < te:
                f = b.func_at(a)
                name = f.name if f is not None else '?'
                fl = where.get(f.addr, '?') if f is not None else '?'
                for d in (blob[name], files[fl]):
                    d[0] += size
                    d[1] += size * n
            else:
                for o in self.objects:
                    if o.base <= a < o.end:
                        d = opened[os.path.basename(o.path)]
                        d[0] += size
                        d[1] += size * n
        return {'blob': dict(blob), 'open': dict(opened), 'files': dict(files)}


def steps_default():
    from phy_scenarios import SESSIONS
    return SESSIONS['full']


def bus(trace):
    from scenarios import raw
    return undefined_masked(raw(trace))


# The registers of PCI configuration space that move the two windows of BAR0
# (access.md, "Address spaces"). Code that reaches ChipCommon or the PCIe core
# through their fixed windows (as the open code does) needs no window moves
# where the object moves the window there and back. Whether the windows point
# to the right core whenever it matters is still checked: the card model
# names every access by the core it reaches at that moment, so a window left
# elsewhere shows as an access of the wrong core.
WINDOW_REGS = {0x70, 0x80, 0xac}


def logical(acc):
    """The bus accesses without the moves of the windows."""
    return [a for a in acc if not (a[1] == 'pcicfg' and a[2] in WINDOW_REGS)]


# Values the object writes that are not defined by its own code, so the two
# sessions may legitimately differ there. Found with this tool:
#
# * TBL(0x14)[0x33], third 16 bit word: sub_09eaf9 (the bandwidth-change
#   table update of the AC-PHY) passes the address of a 32 bit variable on
#   its stack to a transfer of one 48 bit entry, so the third word is
#   whatever lies on the stack behind the variable (0x8080 in some calls of
#   the object's session, 0 in others; the bridges use the stack differently).
#   Checked by hooking wlc_phy_table_write_acphy: data on the stack, bytes 4..7
#   alternate between 80808080 and 00000000 at the same address.
UNDEFINED_TBL_WORDS = {(0x14, 0x33, 2)}


def undefined_masked(acc):
    """Replace the values of UNDEFINED_TBL_WORDS by '?' (bus accesses as raw())."""
    out = list(acc)
    tid = toff = None
    word = None
    for i, (op, space, addr, size, value) in enumerate(out):
        if space != 'd11' or op != 'W':
            continue
        if addr == 0x3fc and size == 4:
            reg, val = value & 0xffff, value >> 16
            if reg == 0x0d:
                tid, word = val, None
            elif reg == 0x0e:
                toff, word = val, None
            elif reg == 0x11:
                word = 0                    # first word of a wide entry
            else:
                word = None
        elif addr == 0x3fe and size == 2 and word is not None:
            word += 1
            if (tid, toff, word) in UNDEFINED_TBL_WORDS:
                out[i] = (op, space, addr, size, '?')
    return out


def compare(ref, got, show):
    if ref == got:
        return 0
    n = min(len(ref), len(got))
    first = next((i for i in range(n) if ref[i] != got[i]), n)
    if show:
        print('  first difference at bus access %d of %d (object) / %d (hybrid):'
              % (first, len(ref), len(got)))
        for i in range(max(0, first - 3), min(max(len(ref), len(got)), first + show)):
            a = ref[i] if i < len(ref) else None
            b = got[i] if i < len(got) else None
            mark = ' ' if a == b else '!'
            print('   %s %6d  object %-40s hybrid %s' % (mark, i, a, b))
    sm = difflib.SequenceMatcher(a=ref, b=got, autojunk=False)
    return sum(1 for op in sm.get_opcodes() if op[0] != 'equal')


# ------------------------------------------------------------------ commands
def cmd_list(ns):
    for name, lay in LAYERS.items():
        print('%-10s %s' % (name, lay['doc']))
        print('           bridge: %s; open: %s' % (', '.join(lay['bridge']), ', '.join(lay['open'])))


_reference = {}


def reference(steps):
    key = tuple(steps)
    if key not in _reference:
        h = Hybrid()
        _reference[key] = bus(h.run(steps))
    return _reference[key]


def cmd_run(ns):
    steps = ns.steps.split(',') if ns.steps else steps_default()
    t0 = time.time()
    ref = reference(steps)
    h = Hybrid(ns.layers)
    got = bus(h.run(steps))
    strict = compare(ref, got, 0)
    n = compare(logical(ref), logical(got), ns.show)
    calls = sum(h.redirected.values())
    print('%s: %d functions of the object redirected (%d references); session of %d steps: '
          '%d bus accesses of the object, %d of the hybrid; %d differences '
          '(%d counting the moves of the BAR0 windows) (%.0f s)'
          % ('+'.join(ns.layers), len(h.new), calls, len(steps), len(ref), len(got), n,
             strict, time.time() - t0))
    return 1 if n else 0


def metric(layers, steps):
    """Coverage of the object alone and of the hybrid; returns both."""
    base = Hybrid(coverage=True)
    base.run(steps)
    c0 = base.coverage()
    c1 = None
    if layers:
        h = Hybrid(layers, coverage=True)
        h.run(steps)
        c1 = h.coverage()
    return c0, c1


def totals(c):
    s = [sum(v[0] for v in c['blob'].values()), sum(v[1] for v in c['blob'].values())]
    o = [sum(v[0] for v in c['open'].values()), sum(v[1] for v in c['open'].values())]
    return s, o


def cmd_metric(ns):
    steps = ns.steps.split(',') if ns.steps else steps_default()
    c0, c1 = metric(ns.layers, steps)
    (s0, d0), _ = totals(c0)
    print('object alone: %d bytes of the object\'s code run (%d functions); %d bytes '
          'executed counting repetitions' % (s0, len(c0['blob']), d0))
    if c1 is None:
        for f, v in sorted(c0['files'].items(), key=lambda x: -x[1][0])[:30]:
            print('  %-24s %7d bytes  %10d executed' % (f, v[0], v[1]))
        return 0
    (s1, d1), (os_, od) = totals(c1)
    print('hybrid (%s): %d bytes of the object\'s code run (%d functions); %d bytes of '
          'open code run' % ('+'.join(ns.layers), s1, len(c1['blob']), os_))
    print('  object code no longer run:  %7d bytes, %.1f%% of what the session runs'
          % (s0 - s1, 100.0 * (s0 - s1) / max(s0, 1)))
    print('  share of execution in open code: %.1f%% (%d of %d bytes executed, '
          'counting repetitions)' % (100.0 * od / max(od + d1, 1), od, od + d1))
    gone = sorted(set(c0['blob']) - set(c1['blob']), key=lambda f: -c0['blob'][f][0])
    print('  functions of the object no longer run: %d' % len(gone))
    for f in gone[:40]:
        print('    %-36s %6d bytes' % (f, c0['blob'][f][0]))
    return 0


def cmd_redirect(ns):
    h = Hybrid(ns.layers)
    b = h.m.blob
    targets = {}
    for name in h.new:
        syms = b.byname.get(name, [])
        if len(syms) != 1:
            print('  %s: %d definitions in the object - cannot be redirected by name'
                  % (name, len(syms)))
        for s in syms:
            targets[s.addr] = name
    bypass = direct_calls(b, targets)
    for name in sorted(h.new):
        print('  %-30s %5d references redirected, %d direct calls without relocation'
              % (name, h.redirected.get(name, 0), bypass.get(name, 0)))
    return 1 if bypass else 0


def layer_symbols(blob, layer):
    """For one layer: (weaken, globalize, imports, unresolved) as sorted lists.

    weaken: functions of the object the bridge defines (all callers of the
    object must reach the bridge); globalize: functions of the object the
    bridge or the open code call and that are local in the object; imports:
    imports of the object the open code uses too (provided by the driver's
    OS layer); unresolved: names nothing provides."""
    bridge_paths, open_paths = build([layer])
    defined, undefined = set(), set()
    for p in bridge_paths + open_paths:
        with open(p, 'rb') as f:
            elf = ELFFile(f)
            st = [s for s in elf.iter_sections() if isinstance(s, SymbolTableSection)][0]
            for s in st.iter_symbols():
                if not s.name or s['st_info']['bind'] == 'STB_LOCAL':
                    continue
                (undefined if s['st_shndx'] == 'SHN_UNDEF' else defined).add(s.name)
    funcs = {s.name: s for s in blob.syms}
    weaken = sorted(n for n in defined if n in funcs and funcs[n].kind == 'F')
    needed = sorted(n for n in undefined - defined if n in funcs)
    imports = sorted(n for n in undefined - defined if n in blob.imports)
    unknown = sorted(n for n in undefined - defined if n not in funcs and n not in blob.imports)
    for n in weaken + needed:
        if len(blob.byname.get(n, [])) != 1:
            raise SystemExit('%s is defined %d times in the object' % (n, len(blob.byname.get(n, []))))
    # weakened names are made global first as well: they are local in the object
    globalize = sorted(set(n for n in needed if not funcs[n].glob))
    return weaken, globalize, imports, unknown


def cmd_symbols(ns):
    """What the HYBRID build has to do to a copy of the object."""
    from blob import Blob, default_path
    b = Blob(default_path())
    bad = 0
    for layer in ns.layers:
        weaken, globalize, imports, unknown = layer_symbols(b, layer)
        print('== %s' % layer)
        print('# weaken (the bridge defines them): %d: %s' % (len(weaken), ' '.join(weaken)))
        print('# globalize (called by the bridge or the open code, local in the object): '
              '%d: %s' % (len(globalize), ' '.join(globalize)))
        print('# imports of the object the open code uses too: ' + ', '.join(imports))
        if unknown:
            print('# UNRESOLVED (nothing provides them): ' + ', '.join(unknown))
            bad = 1
        if ns.write:
            head = ('# generated by tools/re/hybrid.py symbols --write %s; do not edit\n'
                    % layer)
            for ext, names in (('weaken', weaken), ('globalize', globalize)):
                with open(os.path.join(BRIDGE_SRC, '%s.%s' % (layer, ext)), 'w',
                          newline='\n') as f:
                    f.write(head + ''.join(n + '\n' for n in names))
    return bad


def cmd_isolation(ns):
    """Can a set of source files be replaced as a whole?  Over the session:
    which code outside the files touches the memory they allocate, and which
    memory of others (heap, globals) their code touches."""
    import bisect
    from unicorn import x86_const as x86
    from emu import HEAP_BASE, HEAP_SIZE
    from modmap import attribute, load_pins
    layer = set(ns.files)
    steps = ns.steps.split(',') if ns.steps else steps_default()

    def setup():
        h = Hybrid()
        m = h.m
        b = m.blob
        if b.funcs is None:
            b.discover_functions()
        where = attribute(b, load_pins(os.path.join(HERE, 'modmap.txt')))
        funcs = sorted((f.addr, f.addr + max(f.size, 1), where.get(f.addr, '?'), f.name)
                       for f in b.funcs)
        starts = [f[0] for f in funcs]

        def info(pc):
            i = bisect.bisect_right(starts, pc) - 1
            if i >= 0 and funcs[i][0] <= pc < funcs[i][1]:
                return funcs[i][2], funcs[i][3]
            return None, None
        return h, m, info

    def chain(m):
        return (m.r64(m.mu.reg_read(x86.UC_X86_REG_RSP)),) + m.return_addresses()

    # run 1: every allocation, with its owner (layer or not)
    h, m, info = setup()
    allocs = []
    orig = m.handlers['osl_malloc']

    def malloc(mm, osh, size):
        # the memory belongs to the function that asked for it (a container
        # like ppr_create allocating for a caller keeps its own objects)
        a = orig(mm, osh, size)
        c = [info(r) for r in chain(mm)]
        first = c[0] if c else (None, '?')
        allocs.append((a, size, first[1] if first[0] in layer else None, first[1]))
        return a
    m.handlers['osl_malloc'] = malloc
    h.run(steps)
    mine = [(a, s, w) for a, s, w, _ in allocs if w and s]
    print('allocations by the layer: %d (%d bytes)' % (len(mine), sum(s for _, s, _ in mine)))
    allocs.sort()
    astarts = [a for a, *_ in allocs]

    def owner(addr):
        i = bisect.bisect_right(astarts, addr) - 1
        if i >= 0 and allocs[i][0] <= addr < allocs[i][0] + max(allocs[i][1], 1):
            return allocs[i]
        return None

    # run 2: watch the heap and the object's data (deterministic heap)
    h, m, info = setup()
    b = m.blob
    into = collections.Counter()     # outside code -> layer memory
    out = collections.Counter()      # layer code -> others' memory
    util = {'bcmutils.c', 'qmath.c', 'wlc_utils.c', 'bcm_mpool.c'}

    def who(mu):
        pc = mu.reg_read(x86.UC_X86_REG_RIP)
        f, n = info(pc)
        if f in util or f is None:
            for r in chain(m):
                ff, nn = info(r)
                if ff and ff not in util:
                    return ff, nn
        return f, n

    def on_mem(mu, access, address, size, value, user):
        f, n = who(mu)
        rw = 'W' if access == uc.UC_MEM_WRITE else 'R'
        o = owner(address) if user == 'heap' else None
        if user == 'heap' and o is None:
            return
        if user == 'heap':
            lay = o[2] is not None
            if (f in layer) == lay:
                return
            key = ('%s+%#x' % (o[2] or o[3], (address - o[0]) & ~3), rw)
            (into if lay else out)[(n, f) + key] += 1
        else:
            if f not in layer or rw == 'R':
                return
            s = b.sym_at(address)
            out[(n, f, 'global ' + (s.name if s else hex(address)), rw)] += 1

    m.mu.hook_add(uc.UC_HOOK_MEM_READ | uc.UC_HOOK_MEM_WRITE, on_mem, 'heap',
                  HEAP_BASE, HEAP_BASE + HEAP_SIZE - 1)
    m.mu.hook_add(uc.UC_HOOK_MEM_WRITE, on_mem, 'data', BASES['.data'],
                  BASES['.bss'] + b.size['.bss'] - 1)
    h.run(steps)
    def report(title, d):
        for rw in ('W', 'R'):
            items = [(k, c) for k, c in d.items() if k[3] == rw]
            print('%s, %s: %d accesses' % (title, 'writes' if rw == 'W' else 'reads',
                                         sum(c for _, c in items)))
            # aggregate by accessor and allocation (offsets listed compactly)
            agg = collections.defaultdict(lambda: [0, set()])
            for (n, f, where_, _), c in items:
                base, _, off = where_.partition('+')
                a = agg[(n, f, base)]
                a[0] += c
                a[1].add(off)
            for (n, f, base), (c, offs) in sorted(agg.items(), key=lambda x: -x[1][0])[:ns.show]:
                o = sorted(offs, key=lambda x: int(x, 16) if x.startswith('0x') else 0)
                print('  %8d %-32s %-18s %s %s' % (c, n, f, base,
                                                  ','.join(o[:8]) + (' ...' if len(o) > 8 else '')))

    report('outside code touching the layer\'s memory', into)
    report('layer code touching memory of others (heap; globals: writes only)', out)
    return 0


def cmd_fwdata(ns):
    """hybrid/fwdata.c: the table behind hw_fw_data() in the HYBRID build."""
    import re
    from blob import Blob, default_path
    b = Blob(default_path())
    # every literal name in open/ that hw_fw_data can be asked for must be listed
    src = ''
    for root, _, files in os.walk(os.path.join(ROOT, 'open')):
        for f in files:
            if f.endswith('.c'):
                src += open(os.path.join(root, f), encoding='utf8').read()
    missing = []
    rows = []
    for name in FW_NAMES:
        syms = [s for s in b.byname.get(name, []) if s.kind == 'O' and s.size]
        if len(syms) != 1:
            missing.append(name)
            continue
        if '"%s"' % name not in src:
            print('note: %s is not named in open/ (still listed)' % name)
        rows.append((name, syms[0].size))
    if missing:
        raise SystemExit('not one sized data object in the object: ' + ', '.join(missing))
    lines = ['// SPDX-License-Identifier: ISC',
             '// generated by tools/re/hybrid.py fwdata --write; do not edit',
             '/*',
             ' * The data of Broadcom\'s object that the open PHY asks for by name',
             ' * (hw_fw_data()). Only the names and the sizes are here; the data is',
             ' * linked from the object (its symbols are made global in the HYBRID build).',
             ' */',
             '#include "phy_card.h"', '']
    lines += ['extern const u8 %s[];' % n for n, _ in rows]
    lines += ['', 'const struct hybrid_fw hybrid_fw_table[] = {']
    lines += ['\t{ "%s", %s, %d },' % (n, n, size) for n, size in rows]
    lines += ['\t{ NULL, NULL, 0 }', '};', '']
    text = '\n'.join(lines)
    if ns.write:
        with open(os.path.join(BRIDGE_SRC, 'fwdata.c'), 'w', newline='\n') as f:
            f.write(text)
        print('hybrid/fwdata.c: %d tables, %d bytes of data referenced'
              % (len(rows), sum(s for _, s in rows)))
    else:
        print(text)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('list')
    p = sub.add_parser('fwdata')
    p.add_argument('--write', action='store_true')
    p = sub.add_parser('isolation')
    p.add_argument('files', nargs='+', help='source files of the object (modmap names)')
    p.add_argument('--steps')
    p.add_argument('--show', type=int, default=30)
    for name in ('run', 'metric', 'redirect', 'symbols'):
        p = sub.add_parser(name)
        p.add_argument('layers', nargs='*' if name == 'metric' else '+', choices=list(LAYERS))
        p.add_argument('--steps', help='comma separated session steps (default: the PHY session)')
        p.add_argument('--show', type=int, default=12)
        if name == 'symbols':
            p.add_argument('--write', action='store_true',
                           help='write hybrid/LAYER.weaken and hybrid/LAYER.globalize')
    ns = ap.parse_args()
    return {'list': cmd_list, 'run': cmd_run, 'metric': cmd_metric,
            'redirect': cmd_redirect, 'symbols': cmd_symbols,
            'fwdata': cmd_fwdata, 'isolation': cmd_isolation}[ns.cmd](ns)


if __name__ == '__main__':
    sys.exit(main())
