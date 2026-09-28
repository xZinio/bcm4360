#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
The A/B scenarios: what is compared between Broadcom's object and the open
code.  Run them with ab.py.  A scenario is a function(objects, show) that
returns {'stages', 'ref', 'got', 'differences'}.
"""
import struct

import ab
from chip import BAR0_VA, BAR1_VA, BAR1_SIZE

SCENARIOS = {}


def scenario(fn):
    SCENARIOS[fn.__name__.replace('_', '-')] = fn
    return fn


def all_scenarios():
    return SCENARIOS


def make_hw(s):
    """struct bcm4360_hw (open/include/bcm4360/hw.h) for the modelled card."""
    m = s.m
    hw = m.buf(64)
    m.write(hw, struct.pack('<QQIIQQQQQ', BAR0_VA, BAR1_VA, BAR1_SIZE, 0, s.d.pdev,
                            BAR0_VA, BAR0_VA + 0x3000, BAR0_VA + 0x2000, BAR0_VA + 0x1000))
    return hw


def blob_pi(s):
    """The object's PHY state of the current band (see the analyst guide)."""
    m = s.m
    wlc_hw = m.r64(s.d.wlc + 0x20)
    return m.r64(m.r64(wlc_hw + 0xe8) + 0x28)


def set_channel(s, text):
    """Tune the session of the object to a channel ('36', '36/80', '149/40' ...).

    Channels that the regulatory domain of the session does not allow (the
    driver answers "bad channel": 12 to 14, 52 to 144) are set by calling the
    channel function of the PHY the way wlc_bmac_set_chanspec does.
    """
    from run_chan import chanspec
    d, m = s.d, s.m
    cs = chanspec(m, text)
    r = d.iovar_setint('chanspec', cs)
    if r == 0:
        return cs
    if r != -20:
        raise ab.EmuError('channel %s: error %d' % (text, r))
    pi = blob_pi(s)
    m.call('wlc_suspend_mac_and_wait', d.wlc)
    m.call('wlc_phy_chanspec_radio_set', pi, cs)
    m.call('wlc_phy_chanspec_set', pi, cs)
    m.call('wlc_enable_mac', d.wlc)
    return cs


def raw(trace):
    """Accesses as the bus sees them: nothing decoded, nothing left out."""
    return [(a.op, a.space, a.addr, a.size, a.value) for a in trace
            if a.space in ('d11', 'chipcommon', 'pcie', 'pcicfg', 'delay')
            or a.space.endswith('.wrap')]


def result(stages, ref, got, show):
    n = ab.compare(ref, got, show) if show else sum(
        1 for op in __import__('difflib').SequenceMatcher(a=ref, b=got, autojunk=False)
        .get_opcodes() if op[0] != 'equal')
    return {'stages': stages, 'ref': len(ref), 'got': len(got), 'differences': n}


# ---------------------------------------------------------------- register access
# One list of operations, run through the primitives of the object and through
# those of the open code (docs/re/spec/access.md).
def access_ops():
    ops = []
    # 60 writes in a row: the write pacing must show twice
    for i in range(60):
        ops.append(('phy_write', 0x400 + i, 0x1000 + i))
    ops.append(('phy_read', 0x408))
    for i in range(30):
        ops.append(('radio_write', 0x100 + i, 0x20 + i))
    # reads and modifications reset the pacing
    for i in range(23):
        ops.append(('phy_write', 0x500 + i, i))
    ops.append(('phy_mod', 0x500, 0x00f0, 0xffff))
    for i in range(30):
        ops.append(('phy_write', 0x600 + i, i))
    ops += [('phy_and', 0x601, 0xff00), ('phy_or', 0x602, 0x8001),
            ('radio_read', 0x101), ('radio_mod', 0x102, 0x0ff0, 0x0120),
            ('radio_and', 0x103, 0x000f), ('radio_or', 0x104, 0x8000),
            ('radio_xor', 0x105, 0x00ff)]
    # writes of both kinds share the counter
    for i in range(20):
        ops.append(('phy_write', 0x700 + i, i))
        ops.append(('radio_write', 0x200 + i, i))
    # tables of every width
    ops += [('tbl_write', 0x0a, 8, 0, bytes(range(1, 9))),
            ('tbl_write', 0x40, 16, 5, struct.pack('<6H', 1, 2, 3, 0xffff, 0x8000, 7)),
            ('tbl_write', 0x03, 32, 1, struct.pack('<3I', 0x12345678, 0xffffffff, 1)),
            ('tbl_write', 0x11, 48, 2, struct.pack('<6H', 1, 2, 3, 4, 5, 6)),
            ('tbl_write', 0x30, 60, 0, struct.pack('<4I', 0x11112222, 0x03334444, 5, 6)),
            ('tbl_write', 0x31, 64, 3, struct.pack('<4I', 0x11112222, 0x33334444, 5, 6)),
            ('tbl_read', 0x0a, 8, 0, 8), ('tbl_read', 0x40, 16, 5, 6),
            ('tbl_read', 0x03, 32, 1, 3), ('tbl_read', 0x11, 48, 2, 2),
            ('tbl_read', 0x30, 60, 0, 2), ('tbl_read', 0x31, 64, 3, 2)]
    ops += [('shm_write', 0x8e, 0x1234), ('shm_write', 0x90, 0xabcd), ('shm_read', 0x8e),
            ('shm_read', 0x90), ('shm_write', 0x12, 7), ('shm_read', 0x12)]
    return ops


ENTRY_BYTES = {8: 1, 16: 2, 32: 4, 48: 6, 60: 8, 64: 8}


def run_access(s, ops, names, first):
    """names: operation -> function; first: the state argument per operation class."""
    m, chip = s.m, s.chip
    n0 = len(chip.trace)
    results = []
    for op in ops:
        kind = op[0]
        f = names[kind]
        if kind in ('tbl_write', 'tbl_read'):
            tid, width, off = op[1], op[2], op[3]
            if kind == 'tbl_write':
                data = op[4]
                n = len(data) // ENTRY_BYTES[width]
                p = m.buf(data)
            else:
                n = op[4]
                p = m.buf(n * ENTRY_BYTES[width])
            m.call(f, first(kind), tid, n, off, width, p)
            if kind == 'tbl_read':
                results.append(m.read(p, n * ENTRY_BYTES[width]))
        else:
            r = m.call(f, first(kind), *op[1:])
            if kind.endswith('read'):
                results.append(r & 0xffff)
    return chip.trace[n0:], results


@scenario
def access(objs, show):
    """Register access primitives: PHY, radio, PHY tables, shared memory."""
    ops = access_ops()

    # the object
    s = ab.Session()
    s.d.attach()
    s.d.iovar_setint('mpc', 0)
    s.d.up()
    ab.delays(s.d)
    pi = blob_pi(s)
    wlc_hw = s.m.r64(s.d.wlc + 0x20)
    limit = s.m.r16(pi + 0x228)
    s.m.w16(pi + 0x226, 0)
    state = ab.snapshot(s.chip)
    blob_names = {
        'phy_write': 'phy_reg_write', 'phy_read': 'phy_reg_read', 'phy_mod': 'phy_reg_mod',
        'phy_and': 'phy_reg_and', 'phy_or': 'phy_reg_or', 'radio_write': 'write_radio_reg',
        'radio_read': 'read_radio_reg', 'radio_mod': 'mod_radio_reg',
        'radio_and': 'and_radio_reg', 'radio_or': 'or_radio_reg',
        'radio_xor': 'xor_radio_reg', 'tbl_write': 'wlc_phy_table_write_acphy',
        'tbl_read': 'wlc_phy_table_read_acphy', 'shm_write': 'wlc_bmac_write_shm',
        'shm_read': 'wlc_bmac_read_shm'}
    ref_trace, ref_results = run_access(
        s, ops, blob_names, lambda k: wlc_hw if k.startswith('shm') else pi)

    # the open code, on a card in the same state
    t = ab.Session(objs)
    t.d.attach()
    t.d.iovar_setint('mpc', 0)
    t.d.up()
    ab.delays(t.d)
    ab.restore(t.chip, state)
    hw = make_hw(t)
    io = t.m.buf(256)
    t.m.call(t.sym('bcm4360_phy_io_init'), io, hw, 1, limit)
    open_names = {k: t.sym('bcm4360_' + k) for k in blob_names}
    got_trace, got_results = run_access(
        t, ops, open_names, lambda k: hw if k.startswith('shm') else io)

    ref, got = raw(ref_trace), raw(got_trace)
    res = result(1, ref, got, show)
    if ref_results != got_results:
        res['differences'] += 1
        if show:
            for i, (a, b) in enumerate(zip(ref_results, got_results)):
                if a != b:
                    print('  result %d differs: object %r, open %r' % (i, a, b))
                    break
    return res


# ---------------------------------------------------------------- the 2069 radio
# docs/re/spec/acphy-radio.md, docs/re/tasks/radio.md
RADIO_STAGES = ['sub_09fb72', 'sub_09591e', 'wlc_2069_rfpll_150khz', 'sub_09311b',
                'sub_093e47', 'sub_096203', 'sub_0940f8', 'sub_09e378', 'sub_0a7089',
                'wlc_phy_switch_radio_acphy']
RADIO_CONFIGS = [
    {'radio_rev': 4, 'phy_rev': 1, 'done': True},
    {'radio_rev': 4, 'phy_rev': 1, 'done': False},
    {'radio_rev': 3, 'phy_rev': 0, 'done': True},
]
RADIO_CHANNELS = ['36', '36/80', '4', '6', '149/40', '100/80', '13']
PHY_SPACES = ('phy', 'radio', 'delay')

_radio_cache = {}


def radio_inspect(st, m, args):
    pi = args[0]
    pi_ac = m.r64(pi + 0x138)
    sh = m.r64(pi + 0x20)
    st.extra = {
        'cores': m.r8(pi + 0x168), 'radio_rev': m.r8(pi + 0x16c), 'phy_rev': m.r32(pi + 0x164),
        'chanspec': m.r16(pi + 0x17e), 'boardflags': m.r32(sh + 0x64),
        'skip_rcal': bool(m.r8(pi_ac + 0x345) or m.r8(pi_ac + 0x34e)),
        'bf29': m.r8(pi_ac + 0x34a),
        'rccal': (m.r8(pi_ac + 0x339), m.r8(pi_ac + 0x33a), m.r8(pi_ac + 0x33b)),
    }


def radio_reference(cfg):
    """The object's session for a configuration: (session, stages), cached."""
    key = tuple(sorted(cfg.items()))
    if key in _radio_cache:
        return _radio_cache[key]
    s = ab.Session(phy_rev=cfg['phy_rev'], radio_rev=cfg['radio_rev'])
    s.chip.radio_done = cfg['done']
    ab.delays(s.d)
    stages = ab.watch(s.d, RADIO_STAGES, radio_inspect)
    d = s.d
    d.attach()
    d.iovar_setint('mpc', 0)
    d.up()
    for text in RADIO_CHANNELS:
        set_channel(s, text)
    s.m.call('wlc_down', d.wlc)
    ab.close_stages(d, stages)
    # what the fields are after a stage: taken from the entry of the next one
    for a, b in zip(stages, stages[1:]):
        a.extra['rccal_after'] = b.extra['rccal']
    _radio_cache[key] = (s, stages)
    return s, stages


def make_radio(t, hw, st):
    """struct bcm4360_radio (docs/re/tasks/radio.md) for the state of a stage."""
    m = t.m
    x = st.extra
    io = m.buf(256)
    m.call(t.sym('bcm4360_phy_io_init'), io, hw, 1, 24)
    b = m.blob
    rev = x['radio_rev']
    pref = {3: 'prefregs_2069_rev3', 4: 'prefregs_2069_rev4', 8: 'prefregs_2069_rev4'}.get(rev)
    chan = {3: 'chan_tuning_2069rev3', 4: 'chan_tuning_2069rev4',
            8: 'chan_tuning_2069rev4'}.get(rev)
    r = m.buf(512)
    m.write(r, struct.pack('<QQQIIBBBBIBBBBB', io,
                           b.byname[pref][0].addr if pref else 0,
                           b.byname[chan][0].addr if chan else 0,
                           77 if chan else 0, 0,
                           x['cores'], rev, x['phy_rev'], 0, x['boardflags'],
                           1 if x['skip_rcal'] else 0, 1 if x['bf29'] else 0, 0, 0, 0))
    m.call(t.sym('bcm4360_radio_init'), r)
    # the calibration results the object had at this point
    m.write(r + 42, bytes(x['rccal']))
    return r


def radio_scenario(objs, show, func, select, call, after=None, configs=None, when=None):
    """Compare the stages of `func` with what `call` does.

    select(access, owner) picks the accesses of the object that belong to the
    part under test; call(t, r, st) runs the open code for a stage.
    """
    total_ref = total_got = diffs = count = 0
    for cfg in configs or RADIO_CONFIGS:
        s, stages = radio_reference(cfg)
        t = ab.Session(objs, phy_rev=cfg['phy_rev'], radio_rev=cfg['radio_rev'])
        t.chip.radio_done = cfg['done']
        ab.delays(t.d)
        hw = make_hw(t)
        trace = s.chip.trace
        for st in stages:
            if st.func != func or (when is not None and not when(st)):
                continue
            seg = trace[st.start:st.end]
            picked = [a for a in seg if select(a, ab.owner(a, s.names))]
            if not picked:
                continue
            ref = ab.logical(picked, s.names, PHY_SPACES)
            first = seg.index(picked[0])
            ab.restore(t.chip, ab.advance(st.state, seg[:first]))
            r = make_radio(t, hw, st)
            n0 = len(t.chip.trace)
            call(t, r, st)
            got = ab.logical(t.chip.trace[n0:], t.names, PHY_SPACES)
            count += 1
            total_ref += len(ref)
            total_got += len(got)
            if ref != got:
                if show:
                    print('configuration %s, stage at access %d, chanspec %#06x:'
                          % (cfg, st.start, st.extra['chanspec']))
                    ab.compare(ref, got, show)
                diffs += 1
            if after is not None:
                msg = after(t, r, st)
                if msg:
                    diffs += 1
                    if show:
                        print('configuration %s, stage at access %d: %s' % (cfg, st.start, msg))
    return {'stages': count, 'ref': total_ref, 'got': total_got, 'differences': diffs}


def whole(a, own):
    return True


@scenario
def radio_pwron(objs, show):
    """Radio reset, preferred values, fixed modifications (section 5)."""
    return radio_scenario(objs, show, 'sub_09fb72', whole,
                          lambda t, r, st: t.m.call(t.sym('bcm4360_radio_pwron_seq'), r))


@scenario
def radio_off(objs, show):
    """Radio off (section 4, "Off")."""
    return radio_scenario(objs, show, 'wlc_phy_switch_radio_acphy',
                          lambda a, own: own == 'wlc_phy_switch_radio_acphy',
                          lambda t, r, st: t.m.call(t.sym('bcm4360_radio_off'), r),
                          when=lambda st: st.args[1] & 0xff == 0)


@scenario
def radio_rcal(objs, show):
    """Resistor calibration (section 4, "On", steps 3 and 4)."""
    return radio_scenario(objs, show, 'wlc_phy_switch_radio_acphy',
                          lambda a, own: own == 'wlc_phy_switch_radio_acphy',
                          lambda t, r, st: t.m.call(t.sym('bcm4360_radio_rcal'), r),
                          when=lambda st: st.args[1] & 0xff != 0)


def rccal_results(t, r, st):
    want = st.extra.get('rccal_after')
    got = tuple(t.m.read(r + 42, 3))
    if want is not None and got != want:
        return 'calibration results %s, the object has %s' % (got, want)


@scenario
def radio_rccal(objs, show):
    """RC calibration and its results (section 7)."""
    return radio_scenario(objs, show, 'sub_09591e', whole,
                          lambda t, r, st: t.m.call(t.sym('bcm4360_radio_rccal'), r),
                          after=rccal_results)


def tune(t, r, st):
    m = t.m
    cs = st.args[1] & 0xffff
    freq = m.buf(8)
    entry = m.call(t.sym('bcm4360_radio_chan_entry'), r, cs & 0xff, freq)
    if entry:
        m.call(t.sym('bcm4360_radio_tune'), r, entry, cs & 0xff, 1 if cs & 0xc000 else 0)


@scenario
def radio_tune(objs, show):
    """Channel tuning: table entry, patches, loop filter, start of the VCO calibration (section 11)."""
    inner = ('wlc_2069_rfpll_150khz', 'sub_09311b')
    return radio_scenario(
        objs, show, 'sub_0a7089',
        lambda a, own: own in inner or (own == 'sub_0a7089' and a.space == 'radio'), tune)


@scenario
def radio_vcocal_wait(objs, show):
    """Wait for the VCO calibration (section 13)."""
    return radio_scenario(objs, show, 'sub_093e47', whole,
                          lambda t, r, st: t.m.call(t.sym('bcm4360_radio_vcocal_wait'), r,
                                                    st.args[1] & 0xff))


@scenario
def radio_afecal(objs, show):
    """Converter calibration (section 14)."""
    return radio_scenario(objs, show, 'sub_096203', whole,
                          lambda t, r, st: t.m.call(t.sym('bcm4360_radio_afecal'), r))


@scenario
def radio_band(objs, show):
    """Radio part of the band change (section 15)."""
    return radio_scenario(
        objs, show, 'sub_09e378',
        lambda a, own: own == 'sub_09e378' and a.space == 'radio',
        lambda t, r, st: t.m.call(t.sym('bcm4360_radio_band_change'), r,
                                  1 if st.extra['chanspec'] & 0xc000 else 0))


@scenario
def radio_tssi(objs, show):
    """Radio set-up of the power detector path (section 16)."""
    return radio_scenario(
        objs, show, 'sub_0940f8', whole,
        lambda t, r, st: t.m.call(t.sym('bcm4360_radio_tssi_setup'), r, st.args[1] & 0xff,
                                  st.args[2] & 0xff, 1 if st.extra['chanspec'] & 0xc000 else 0))


# ---------------------------------------------------------------- the PMU
# docs/re/spec/pmu.md, docs/re/tasks/pmu.md
PMU_SPACES = ('chipcommon', 'pcie', 'delay')
PMU_MODELS = [
    {'chip_rev': 3, 'chipstatus': 0x01},
    {'chip_rev': 3, 'chipstatus': 0x21},
    {'chip_rev': 4, 'chipstatus': 0x01},
    {'chip_rev': 4, 'chipstatus': 0x20},
]


def pmu_session(model, objs=None):
    from bcm4360 import Bcm4360
    cls = type('Bcm4360Variant', (Bcm4360,), dict(model))
    s = ab.Session(objs, chip_class=cls)
    ab.delays(s.d)
    return s


def make_pmu(t, hw, model):
    """struct bcm4360_pmu (docs/re/tasks/pmu.md) for the modelled chip."""
    chip = t.chip
    p = t.m.buf(256)
    t.m.write(p, struct.pack('<Q9II', hw, chip.chip_id, model['chip_rev'], chip.chip_pkg,
                             model['chipstatus'], 43, chip.cc_caps, chip.cc_caps_ext,
                             chip.pmu_caps & 0xff, chip.pmu_caps, 0))
    return p


def spaces_of(trace, spaces):
    return [(a.op, a.space, a.addr, a.size, a.value) for a in trace if a.space in spaces]


def pmu_scenario(objs, show, steps, models=None, prepare=None):
    """steps: list of (description, call on the object, call on the open code).

    Each call gets (session, handle, ...) and returns the result to compare
    (or None). Every step starts from the state the object had before it.
    """
    diffs = count = nref = ngot = 0
    for model in models or PMU_MODELS:
        s = pmu_session(model)
        try:
            s.d.attach()
        except Exception as e:
            print('model %s: attach failed: %s' % (model, str(e).splitlines()[0]))
            diffs += 1
            continue
        if not s.d.wlc:
            print('model %s: the object does not attach (error %d)' % (model, s.d.err))
            diffs += 1
            continue
        wlc_hw = s.m.r64(s.d.wlc + 0x20)
        sih = s.m.r64(wlc_hw + 0xb8)
        t = pmu_session(model, objs)
        hw = make_hw(t)
        for step in steps:
            text, blob_call, open_call = step[:3]
            mask = step[3] if len(step) > 3 else 0xffffffff
            if prepare is not None:
                prepare(s, text)
            # the object's cache of the measured ILP clock
            s.m.w32(ab_bss(s) + 0x2f4, 0)
            state = ab.snapshot(s.chip)
            n0 = len(s.chip.trace)
            want = blob_call(s, sih)
            ref = spaces_of(s.chip.trace[n0:], PMU_SPACES)
            ab.restore(t.chip, state)
            p = make_pmu(t, hw, model)
            n0 = len(t.chip.trace)
            have = open_call(t, p)
            got = spaces_of(t.chip.trace[n0:], PMU_SPACES)
            count += 1
            nref += len(ref)
            ngot += len(got)
            bad = ref != got
            if want is not None and have is not None and (want & mask) != (have & mask):
                bad = True
                if show:
                    print('%s, chip rev %d, chip status %#x: result %#x, the object returns %#x'
                          % (text, model['chip_rev'], model['chipstatus'],
                             have & mask, want & mask))
            if bad:
                diffs += 1
                if show and ref != got:
                    print('%s, chip rev %d, chip status %#x:' % (text, model['chip_rev'],
                                                                   model['chipstatus']))
                    ab.compare(ref, got, show)
    return {'stages': count, 'ref': nref, 'got': ngot, 'differences': diffs}


def ab_bss(s):
    from blob import BASES
    return BASES['.bss']


def _b(name, *args, result=True):
    """Call of a function of the object: f(sih, osh, args...)."""
    def call(s, sih):
        r = s.m.call(name, sih, s.d.osh, *args)
        return r if result else None
    return call


def _b1(name, *args, result=True):
    """Call of a function of the object that takes sih only: f(sih, args...)."""
    def call(s, sih):
        r = s.m.call(name, sih, *args)
        return r if result else None
    return call


def _o(name, *args, result=True):
    def call(t, p):
        r = t.m.call(t.sym(name), p, *args)
        return r if result else None
    return call


def ht(on):
    """Make the PLL clock available or not before a step (a core asks for it)."""
    def prepare(s, text):
        pcie = s.chip.core('pcie')
        v = pcie.peek(0x1e0)
        pcie.poke(0x1e0, 4, (v | 0x10) if on else (v & ~0x12))
        for c in s.chip.cores:
            if not on:
                c.poke(0x1e0, 4, c.peek(0x1e0) & ~0x12)
    return prepare


@scenario
def pmu_indirect(objs, show):
    """Indirect registers: chip control, regulator control, PLL control, PLL update."""
    steps = []
    for reg, mask, val in ((0, 0, 0), (0, 0x2, 0x2), (0, 0x01f00000, 0x00500000),
                           (1, 0xf, 0x7), (2, 0xffffffff, 0xc31), (5, 0, 0)):
        for name in ('chipcontrol', 'regcontrol', 'pllcontrol'):
            steps.append(('%s(%d, %#x, %#x)' % (name, reg, mask, val),
                          _b1('si_pmu_' + name, reg, mask, val),
                          _o('bcm4360_pmu_' + name, reg, mask, val)))
    steps.append(('pllupd', _b1('si_pmu_pllupd', result=False),
                  _o('bcm4360_pmu_pllupd', result=False)))
    return pmu_scenario(objs, show, steps, models=PMU_MODELS[:1])


@scenario
def pmu_init(objs, show):
    """si_pmu_init."""
    return pmu_scenario(objs, show, [('init', _b('si_pmu_init', result=False),
                                      _o('bcm4360_pmu_init', result=False))])


@scenario
def pmu_pll_init(objs, show):
    """si_pmu_pll_init with the PLL clock in use and not in use; crystal measurement."""
    steps = [('pll_init', _b('si_pmu_pll_init', 40000, result=False),
              _o('bcm4360_pmu_pll_init', 40000, result=False)),
             ('measure_alpclk', _b('si_pmu_measure_alpclk'), _o('bcm4360_pmu_measure_alpclk'))]
    a = pmu_scenario(objs, show, steps, prepare=ht(False))
    b = pmu_scenario(objs, show, steps, prepare=ht(True))
    return {k: a[k] + b[k] for k in a}


@scenario
def pmu_res_init(objs, show):
    """si_pmu_res_init: resource timers, PLL registers, resource masks, clock request."""
    return pmu_scenario(objs, show, [('res_init', _b('si_pmu_res_init', result=False),
                                      _o('bcm4360_pmu_res_init', result=False))])


@scenario
def pmu_clocks(objs, show):
    """Clock queries: ALP, ILP, backplane, VCO, power up delay."""
    def pll5(s, text):
        # a divider for the backplane clock (PLL control 5, bits 8..15)
        s.chip.core('chipcommon').pllctl[5] = 0x00000600
    steps = [('alp_clock', _b('si_pmu_alp_clock'), _o('bcm4360_pmu_alp_clock')),
             ('ilp_clock', _b('si_pmu_ilp_clock'), _o('bcm4360_pmu_ilp_clock')),
             ('si_clock', _b('si_pmu_si_clock'), _o('bcm4360_pmu_si_clock')),
             ('get_bb_vcofreq', _b('si_pmu_get_bb_vcofreq', 40),
              _o('bcm4360_pmu_get_bb_vcofreq', 40)),
             ('fast_pwrup_delay', _b('si_pmu_fast_pwrup_delay'),
              _o('bcm4360_pmu_fast_pwrup_delay'))]
    return pmu_scenario(objs, show, steps, prepare=pll5)


@scenario
def pmu_power(objs, show):
    """OTP power, radio supply."""
    steps = [('is_otp_powered', _b('si_pmu_is_otp_powered'), _o('bcm4360_pmu_is_otp_powered'),
              0xff),
             ('otp_power(1)', _b('si_pmu_otp_power', 1, result=False),
              _o('bcm4360_pmu_otp_power', 1, result=False)),
             ('otp_power(0)', _b('si_pmu_otp_power', 0, result=False),
              _o('bcm4360_pmu_otp_power', 0, result=False)),
             ('rfldo(0)', _b1('si_pmu_rfldo', 0, result=False),
              _o('bcm4360_pmu_rfldo', 0, result=False)),
             ('rfldo(1)', _b1('si_pmu_rfldo', 1, result=False),
              _o('bcm4360_pmu_rfldo', 1, result=False))]
    return pmu_scenario(objs, show, steps)
