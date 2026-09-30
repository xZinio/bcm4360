#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Scenarios for the PHY as a whole (docs/re/tasks/phy-*.md).

The object runs a session: attach, up, channel changes, periodic work, down.
Every call that the MAC layer makes into the PHY during that session is a
*stage*.  The open code is then taken through the same calls, in the same
order, each time with the card in the state it had when the object was
called, and what both do is compared stage by stage:

* the accesses to PHY and radio registers, PHY tables, shared memory, the
  802.11 core and the PMU, and the delays;
* the calls of services of other layers (bcm4360/phy_env.h) with their
  arguments, at their place between the accesses;
* the results of the calls.

The state of the open PHY is its own: it is made by bcm4360_phy_attach() and
changed by the calls that follow.
"""
import os
import struct

import ab
import phy_state
from emu import STOP_ADDR
from scenarios import make_hw, scenario, set_channel

S = ab.Service
PHY_SERVICES = [
    S('wlapi_suspend_mac_and_wait', 'mac_suspend'),
    S('wlapi_enable_mac', 'mac_enable'),
    S('wlapi_bmac_corereset', 'mac_corereset', (4,)),
    S('wlapi_bmac_bw_set', 'mac_bw_set', (2,)),
    S('wlapi_bmac_phyclk_fgc', 'mac_phyclk_fgc', (1,)),
    S('wlapi_bmac_mhf', 'mac_mhf', (1, 2, 2, 4)),
    S('wlapi_bmac_mctrl', 'mac_mctrl', (4, 4)),
    S('wlapi_bmac_btc_mode_get', 'mac_btc_mode', result=True),
    S('wlapi_bmac_ucode_wake_override_phyreg_set', 'mac_wake_override_set'),
    S('wlapi_bmac_ucode_wake_override_phyreg_clear', 'mac_wake_override_clear'),
    S('wlapi_update_bt_chanspec', 'mac_update_bt_chanspec', (2, 1, 1)),
    S('wlapi_high_update_phy_mode', 'mac_update_phy_mode', (4,)),
    S('wlapi_high_update_txppr_offset', 'mac_update_txppr_offset', (0,)),
    S('wlapi_init_timer', 'timer_init', (0, 0, 0), result=True),
    S('wlapi_add_timer', 'timer_add', (4, 4, 1)),
    S('wlapi_del_timer', 'timer_del', (4,), result=True),
    S('wlapi_free_timer', 'timer_free', (4,)),
    S('si_core_sflags', 'chip_core_sflags', (4, 4), result=True),
    S('si_core_cflags', 'chip_core_cflags', (4, 4), result=True),
    S('si_corereg', 'chip_corereg', (4, 4, 4, 4), result=True),
    S('si_gpiocontrol', 'chip_gpiocontrol', (4, 4, 1), result=True),
    S('si_gpioout', 'chip_gpioout', (4, 4, 1), result=True),
    S('si_gpioouten', 'chip_gpioouten', (4, 4, 1), result=True),
    S('si_get_sromctl', 'chip_sromctl_get', result=True),
    S('si_set_sromctl', 'chip_sromctl_set', (4,)),
    S('otp_read_word', 'otp_read_word', (4, 0), result=True, out=(1, 2)),
]
SERVICE_FUNCS = {sv.func for sv in PHY_SERVICES}

PHY_SPACES = ('phy', 'radio', 'tbl', 'shm', 'svc', 'delay', 'd11', 'pmu', 'chipcommon')


class Api:
    """A function of the interface between the MAC layer and the PHY.

    func    function of the object; its first argument is the PHY state
    name    function of the open code; its first argument is the
            struct bcm4360_phy * that bcm4360_phy_attach() returned
    args    sizes in bytes of the arguments that follow
    result  size in bytes of the result, 0: none
    out     sizes of the objects that the arguments point to, if the
            arguments are pointers to results (then `args` is empty)
    """

    def __init__(self, func, name, args=(), result=0, out=()):
        self.func, self.name = func, 'bcm4360_' + name
        self.args, self.result, self.out = tuple(args), result, tuple(out)


PHY_API = [
    Api('wlc_phy_attach', 'phy_attach'),                # special, see phy_run()
    Api('wlc_phy_machwcap_set', 'phy_machwcap_set', (4,)),
    Api('wlc_phy_get_phyversion', 'phy_get_phyversion', out=(2, 2, 2, 2)),
    Api('wlc_phy_get_coreflags', 'phy_get_coreflags', result=4),
    Api('wlc_phy_cap_get', 'phy_cap_get', result=4),
    Api('wlc_phy_stf_chain_init', 'phy_stf_chain_init', (1, 1)),
    Api('wlc_phy_hw_clk_state_upd', 'phy_hw_clk_state_upd', (1,)),
    Api('wlc_phy_hw_state_upd', 'phy_hw_state_upd', (1,)),
    Api('wlc_phy_por_inform', 'phy_por_inform'),
    Api('wlc_phy_anacore', 'phy_anacore', (1,)),
    Api('wlc_phy_switch_radio', 'phy_switch_radio', (1,)),
    Api('wlc_phy_chanspec_radio_set', 'phy_chanspec_radio_set', (2,)),
    Api('wlc_phy_chanspec_get', 'phy_chanspec_get', result=2),
    Api('wlc_phy_clk_bwbits', 'phy_clk_bwbits', result=4),
    Api('wlc_phy_cal_init', 'phy_cal_init'),
    Api('wlc_phy_init', 'phy_init', (2,)),
    Api('wlc_phy_chanspec_set', 'phy_chanspec_set', (2,)),
    Api('wlc_phy_watchdog', 'phy_watchdog'),
    Api('wlc_phy_down', 'phy_down', result=4),
    Api('wlc_phy_hold_upd', 'phy_hold_upd', (4, 1)),
    Api('wlc_phy_mute_upd', 'phy_mute_upd', (1, 4)),
    Api('wlc_phy_antsel_type_set', 'phy_antsel_type_set', (1,)),
    Api('wlc_phy_preamble_override_get', 'phy_preamble_override_get', result=1),
    Api('wlc_phy_ldpc_override_set', 'phy_ldpc_override_set', (1,)),
    Api('wlc_acphy_set_scramb_dyn_bw_en', 'phy_set_scramb_dyn_bw_en', (1,)),
    Api('sub_0b56ce', 'phy_timer_phycal'),
    Api('wlc_phy_cals_acphy', 'phy_cals', (4,)),            # calibrations (acphy-cal-tx)
    Api('wlc_phy_tempsense_acphy', 'phy_tempsense', result=4),   # acphy-cal-rx
]
API_OF = {a.func: a for a in PHY_API}

PHY_MODELS = [
    {'phy_rev': 1, 'radio_rev': 4},
    {'phy_rev': 0, 'radio_rev': 3},
]
PHY_CHANNELS = ['36', '36/80', '6', '149/40', '100/80', '13', '44/40', '157/80', '11']

# Sets of variables: changes of the default set (data/synthetic-4360-2x2.vars).
PHY_VARIABLES = {
    'default': {},
    'a': {
        'boardflags': '0x00400001',     # no external LNA in either band
        'boardflags3': '0x00002008',
        'femctrl': '5', 'subband5gver': '2', 'pdgain2g': '2', 'pdgain5g': '6',
        'rxgains5gmelnagaina0': '7', 'rxgains5gmtrisoa0': '15', 'rxgains5gmtrelnabypa0': '1',
        'rxgains5ghelnagaina1': '7', 'rxgains5ghtrisoa1': '15', 'rxgains5ghtrelnabypa1': '1',
        'rxgains2gtrisoa0': '3', 'rxgains5gtrisoa1': '9',
        'rxgainerr2ga0': '10', 'rxgainerr2ga1': '3', 'rxgainerr2ga2': '0',
        'rxgainerr5ga0': '5,62,33,0', 'rxgainerr5ga1': '1,30,17,0',
        'rxgainerr5ga2': '0,0,0,0',
        'rawtempsense': '0x1fb',
        'noiselvl2ga0': '3', 'noiselvl2ga1': '7',
        'noiselvl5ga0': '1,2,3,4', 'noiselvl5ga1': '5,6,7,8',
        'tempoffset': '40', 'phycal_tempdelta': '30', 'temps_hysteresis': '3',
        'tempthresh': '100',
        'maxp2ga0': '70', 'maxp2ga1': '74', 'maxp5ga0': '60,62,64,66',
        'maxp5ga1': '61,63,65,67',
        'pa2ga0': '0xff11,0x1a22,0xfc33', 'pa2ga1': '0xff44,0x1a55,0xfc66',
        'pa5ga0': '0xff01,0x1a02,0xfc03,0xff04,0x1a05,0xfc06,0xff07,0x1a08,0xfc09,'
                  '0xff0a,0x1a0b,0xfc0c',
        'pa5ga1': '0xff11,0x1a12,0xfc13,0xff14,0x1a15,0xfc16,0xff17,0x1a18,0xfc19,'
                  '0xff1a,0x1a1b,0xfc1c',
        'cckbw202gpo': '0x1234', 'cckbw20ul2gpo': '0x2345', 'mcsbw202gpo': '0x11223344',
        'mcsbw402gpo': '0x22334455', 'dot11agofdmhrbw202gpo': '0x5678',
        'ofdmlrbw202gpo': '0x00a7',
        'mcsbw205glpo': '0x31323334', 'mcsbw205gmpo': '0x41424344',
        'mcsbw205ghpo': '0x51525354', 'mcsbw405glpo': '0x61626364',
        'mcsbw405gmpo': '0x71727374', 'mcsbw405ghpo': '0x81828384',
        'mcsbw805glpo': '0x91929394', 'mcsbw805gmpo': '0xa1a2a3a4',
        'mcsbw805ghpo': '0xb1b2b3b4',
        'mcslr5glpo': '0x0123', 'mcslr5gmpo': '0x0456', 'mcslr5ghpo': '0x0789',
        'sb20in40lrpo': '0x1111', 'sb20in40hrpo': '0x2222',
        'dot11agduplrpo': '0x3333', 'dot11agduphrpo': '0x4444',
        'sb20in80and160lr5glpo': '0x5551', 'sb20in80and160lr5gmpo': '0x5552',
        'sb20in80and160lr5ghpo': '0x5553', 'sb20in80and160hr5glpo': '0x6661',
        'sb20in80and160hr5gmpo': '0x6662', 'sb20in80and160hr5ghpo': '0x6663',
        'sb40and80lr5glpo': '0x7771', 'sb40and80lr5gmpo': '0x7772',
        'sb40and80lr5ghpo': '0x7773', 'sb40and80hr5glpo': '0x8881',
        'sb40and80hr5gmpo': '0x8882', 'sb40and80hr5ghpo': '0x8883',
        'pdoffset40ma0': '0x1357', 'pdoffset40ma1': '0x2468', 'pdoffset80ma0': '0x0135',
        'pdoffset80ma1': '0x0246',
    },
}


def variables_of(name):
    from run_attach import DEFAULT_SROM
    from srom import load_vars
    v = load_vars(DEFAULT_SROM)
    v.update(PHY_VARIABLES[name])
    return v


_phy_functions = {}


def phy_functions(blob):
    """Names of the functions of the PHY files of the object."""
    if id(blob) not in _phy_functions:
        from modmap import attribute, load_pins
        if blob.funcs is None:
            blob.discover_functions()
        where = attribute(blob, load_pins(os.path.join(ab.HERE, 'modmap.txt')))
        _phy_functions[id(blob)] = {f.name for f in blob.funcs
                                    if where[f.addr] in ('wlc_phy_ac.c', 'wlc_phy_cmn.c')}
    return _phy_functions[id(blob)]


SPEC_DIR = os.path.join(ab.ROOT, 'docs', 're', 'spec')

# Specifications that the open PHY code implements so far.  What the object
# does in functions that none of them describes is left out of the
# comparison, until a task adds the code and the name of its specification
# here.
PHY_SPECS_DONE = ['access', 'acphy-radio', 'acphy-attach']


def scope_of(spec):
    """Functions of the object that a specification describes: the names in
    the first column of the table of its section "Scope"."""
    import re
    names = set()
    inside = False
    with open(os.path.join(SPEC_DIR, spec + '.md'), encoding='utf-8') as f:
        for line in f:
            if line.startswith('## '):
                inside = line.strip() == '## Scope'
            elif inside and line.startswith('|'):
                cells = line.split('|')
                if len(cells) > 2:
                    names.update(re.findall(r'`([A-Za-z_][A-Za-z0-9_]*)`', cells[1]))
    return names


def not_covered(blob, specs=None):
    """Functions of the PHY files that the specifications do not describe."""
    done = set(ab.CARRIERS)     # the access primitives (access.md)
    for spec in specs if specs is not None else PHY_SPECS_DONE:
        done |= scope_of(spec)
    return phy_functions(blob) - done


class Reference:
    """The session of the object."""


_references = {}

# What the object is taken through after its attach.
SESSIONS = {
    'attach': [],
    'cal': ['up', 'tempsense', 'cal-run', 'down'],   # trigger the calibrations directly
    'up': ['up', 'watchdog', 'down'],
    'up-again': ['up', '36/80', 'down', 'up', '149/40', 'down', 'up', '6', 'down'],
    'full': ['up'] + PHY_CHANNELS + ['watchdog', 'down'],
}


def phy_reference(model, variables='default', session='full'):
    """Run the object through a session; cached."""
    if session is True:
        session = 'full'
    elif session is False:
        session = 'attach'
    key = (model['phy_rev'], model['radio_rev'], variables, session)
    if key in _references:
        return _references[key]
    s = ab.Session(srom=variables_of(variables), phy_rev=model['phy_rev'],
                   radio_rev=model['radio_rev'])
    s.chip.radio_done = True
    d, m, chip = s.d, s.m, s.chip
    ab.delays(d)
    r = Reference()
    r.s = s
    r.log = ab.mark_services(d, PHY_SERVICES)
    r.lookups = []
    inside = phy_functions(m.blob)

    def lookup(m, handle, name):
        r.lookups.append((len(chip.trace), m.string(name) if name else ''))

    m.on_call('phy_getvar', lookup, 2)
    m.on_call('getvar', lookup, 2)

    def inspect(st, m, args):
        ret = m.r64(m.mu.reg_read(44))          # UC_X86_REG_RSP
        chain = [chip.func_name(a) for a in (ret,) + m.return_addresses()]
        st.nested = any(n in inside or n in SERVICE_FUNCS for n in chain)
        st.result = None
        st.out = None
        api = API_OF[st.func]

        def done(m, result):
            st.result = result
            if api.out:
                st.out = [m.read(p, n) if p else None
                          for p, n in zip(args[1:], api.out)]

        if ret != STOP_ADDR:
            m.at_return(done)

    r.stages = ab.watch(d, [a.func for a in PHY_API], inspect)
    d.attach()
    if not d.wlc:
        raise ab.EmuError('the object does not attach (error %d)' % d.err)
    r.pi = m.r64(m.r64(m.r64(d.wlc + 0x20) + 0xe8) + 0x28)
    d.iovar_setint('mpc', 0)
    for step in SESSIONS[session]:
        if step == 'up':
            if d.up() != 0:
                raise ab.EmuError('the object does not come up')
        elif step == 'down':
            m.call('wlc_down', d.wlc)
        elif step == 'watchdog':
            m.os.run_timers(m, m.os.now_us // 1000 + 2500)
        elif step == 'cal-run':
            # single-shot calibration (phase 0): runs the whole cal sequence
            m.call('wlc_phy_cals_acphy', r.pi, 0)
        elif step == 'tempsense':
            # tempsense is really called by the cal scheduler with the MAC
            # already suspended; do the same, so the MAC-control write of the
            # (replayed, side-effect-free) suspend is outside the compared stage
            m.call('wlc_suspend_mac_and_wait', d.wlc)
            m.call('wlc_phy_tempsense_acphy', r.pi)
            m.call('wlc_enable_mac', d.wlc)
        else:
            set_channel(s, step)
    ab.close_stages(d, r.stages)
    _references[key] = r
    return r


def make_board(t, r, hw):
    """struct bcm4360_phy_board (docs/re/tasks/phy-attach.md)."""
    from scenarios import make_pmu
    b = phy_state.board(r.s.m, r.pi)
    pmu = make_pmu(t, hw, {'chip_rev': t.chip.chip_rev, 'chipstatus': t.chip.chipstatus})
    p = t.m.buf(64)
    t.m.write(p, struct.pack('<11I2HQ', b['chip'], b['chiprev'], b['chippkg'], b['corerev'],
                             b['sromrev'], b['boardtype'], b['boardrev'], b['boardvendor'],
                             b['boardflags'], b['boardflags2'], b['xtal_hz'],
                             b['pci_vendor'], b['pci_device'], pmu))
    return p


def fw_env(ref):
    """hw_fw_data() of hw.h: the data is handed over from the memory of the object."""
    def hw_fw_data(m, hw, name, psize):
        syms = [x for x in m.blob.byname.get(m.string(name), []) if x.size]
        if not syms:
            return 0
        if psize:
            m.w32(psize, syms[0].size)
        return syms[0].addr
    return {'hw_fw_data': (3, hw_fw_data)}


class OpenPhy:
    """The open code in a session of its own, with the services of the tests."""

    def __init__(self, objs, model, variables):
        ref = []
        self.replay = ab.Replay()
        env = ab.service_env(ref, PHY_SERVICES, self.replay)
        env.update(fw_env(ref))
        getvar = ab.var_env(ref)['hw_getvar'][1]
        self.lookups = []

        def hw_getvar(m, hw, name):
            self.lookups.append(m.string(name) if name else '')
            return getvar(m, hw, name)

        env['hw_getvar'] = (2, hw_getvar)
        self.t = t = ab.Session(objs, env=env, srom=variables_of(variables),
                                phy_rev=model['phy_rev'], radio_rev=model['radio_rev'])
        ref.append(t)
        t.chip.radio_done = True
        ab.delays(t.d)
        ab.open_tables(t)
        self.hw = make_hw(t)
        self.phy = 0

    def has(self, name):
        try:
            self.t.sym(name)
            return True
        except ab.EmuError:
            return False

    def call(self, name, *args):
        t = self.t
        n = len(t.chip.trace)
        r = t.m.call(t.sym(name), *args)
        return r, t.chip.trace[n:]


def phy_run(objs, show, compare, model, variables='default', without=None, session=True):
    """Take the open code through the session of the object.

    compare: names of the functions of the open code whose stages are
    compared (all functions that exist are called).  without: functions of
    the object whose accesses and service calls are left out (parts of the
    PHY that the open code does not have yet); by default those that the
    specifications of PHY_SPECS_DONE do not describe.
    """
    r = phy_reference(model, variables, session)
    s = r.s
    o = OpenPhy(objs, model, variables)
    t = o.t
    board = make_board(t, r, o.hw)
    if without is None:
        without = not_covered(s.m.blob)
    skip = SERVICE_FUNCS | set(without)
    res = {'stages': 0, 'ref': 0, 'got': 0, 'differences': 0, 'missing': set()}
    title = 'PHY rev %d, radio rev %d, variables "%s"' % (model['phy_rev'], model['radio_rev'],
                                                          variables)
    for st in r.stages:
        if st.nested:
            continue
        api = API_OF[st.func]
        if not o.has(api.name):
            res['missing'].add(api.name)
            continue
        attach = st.func == 'wlc_phy_attach'
        if attach and o.phy:
            continue            # the call for the second band: the same PHY
        if not attach and not o.phy:
            continue
        ab.restore(t.chip, st.state)
        o.replay.load(r.log.between(st.start, st.end, without))
        outs = []
        if attach:
            got_result, trace = o.call(api.name, o.hw, board)
            o.phy = got_result
            if not o.phy:
                print('%s: %s returns NULL' % (title, api.name))
                res['differences'] += 1
                break
        elif api.out:
            outs = [t.m.buf(n) for n in api.out]
            got_result, trace = o.call(api.name, o.phy, *outs)
        else:
            args = [a & ((1 << (8 * w)) - 1) for a, w in zip(st.args[1:], api.args)]
            got_result, trace = o.call(api.name, o.phy, *args)
        if api.name not in compare:
            continue
        want = ab.logical(s.chip.trace[st.start:st.end], s.names, PHY_SPACES, services=skip)
        got = ab.logical(trace, t.names, PHY_SPACES)
        res['stages'] += 1
        res['ref'] += len(want)
        res['got'] += len(got)
        bad = []
        if want != got:
            bad.append(None)
        if api.result and st.result is not None:
            mask = (1 << (8 * api.result)) - 1
            if st.result & mask != got_result & mask:
                bad.append('result %#x, the object returns %#x'
                           % (got_result & mask, st.result & mask))
        if api.out and st.out is not None:
            for i, (p, n, w) in enumerate(zip(outs, api.out, st.out)):
                have = t.m.read(p, n)
                if w is not None and have != w:
                    bad.append('result %d: %s, the object gives %s'
                               % (i + 1, have.hex(), w.hex()))
        if o.replay.unexpected:
            bad.append('services called that the object does not call here: %s'
                       % ', '.join(sorted(set(o.replay.unexpected))))
        if bad:
            res['differences'] += 1
            if show:
                print('%s, %s(%s), stage at access %d of the object:'
                      % (title, api.name,
                         ', '.join('%#x' % (a & 0xffffffff)
                                   for a in st.args[1:1 + len(api.args)]), st.start))
                for b in bad:
                    if b is None:
                        ab.compare(want, got, show)
                    else:
                        print('  ' + b)
    if attach_lookups(r, o, show, title) and 'bcm4360_phy_attach' in compare:
        res['differences'] += 1
    res['open'] = o
    res['reference'] = r
    return res


def attach_lookups(r, o, show, title):
    """The variables that the attach looks up: the same names on both sides?"""
    first = [st for st in r.stages if st.func == 'wlc_phy_attach'][0]
    want = {n for pos, n in r.lookups if first.start <= pos <= first.end}
    got = set(o.lookups)
    if not o.phy or want == got:
        return False
    if show:
        print('%s: variables looked up during attach' % title)
        for n in sorted(want - got):
            print('  - object   %s' % n)
        for n in sorted(got - want):
            print('  + open     %s' % n)
    return True


def total(results):
    out = {'stages': 0, 'ref': 0, 'got': 0, 'differences': 0}
    missing = set()
    for res in results:
        for k in out:
            out[k] += res[k]
        missing |= res.get('missing', set())
    out['missing'] = missing
    return out


ATTACH_API = ['bcm4360_phy_attach', 'bcm4360_phy_machwcap_set', 'bcm4360_phy_get_phyversion',
              'bcm4360_phy_get_coreflags', 'bcm4360_phy_cap_get', 'bcm4360_phy_stf_chain_init',
              'bcm4360_phy_hw_clk_state_upd', 'bcm4360_phy_hw_state_upd',
              'bcm4360_phy_por_inform', 'bcm4360_phy_anacore', 'bcm4360_phy_switch_radio',
              'bcm4360_phy_chanspec_get', 'bcm4360_phy_chanspec_radio_set',
              'bcm4360_phy_clk_bwbits']


def need(o, names, show):
    missing = [n for n in names if not o.has(n)]
    if missing and show:
        print('not in the open code: %s' % ', '.join(missing))
    return len(missing)


@scenario
def phy_attach(objs, show):
    """PHY attach: accesses, service calls, variables looked up, results of the queries."""
    results = []
    for model in PHY_MODELS:
        for variables in PHY_VARIABLES:
            res = phy_run(objs, show, ATTACH_API, model, variables, session=False)
            res['differences'] += need(res['open'], ATTACH_API, show and not results)
            results.append(res)
    return total(results)


# The big leaf functions of the channel-set and init paths that belong to
# later tasks (front end and analog filters -> rxgain; receive gain and
# desense -> desense; transmit power -> txpower).  The "middle" task (init and
# the channel function) implements the orchestration and its small helpers and
# leaves these as empty stubs; the tests exclude their accesses until their
# own task implements them.  A name listed here that is actually reached in a
# compared stage has its accesses left out; a name that should be compared but
# is missing here shows up as a difference, so the set is self-correcting.
# The big leaf functions grouped by the task that implements them.  They are
# implemented in the channel function's call order (rxgain, then desense, then
# txpower), each depending only on already-done code; STUBBED lists the areas
# not yet implemented, whose accesses the tests still leave out.
RXGAIN_LEAVES = [
    # front end, analog filters, reciprocity (acphy-rxgain)
    'sub_0a6b0f', 'sub_0a602f', 'sub_0a4adc', 'sub_0a4867', 'sub_09eaf9', 'sub_09e378',
    'sub_09db6d', 'sub_09dd80', 'sub_09de37', 'sub_09deee', 'sub_09dfa5', 'sub_09e064',
    'sub_09e129', 'sub_09e1ee', 'sub_09e2b3', 'sub_0a13ec', 'sub_0a14b6', 'sub_0a5fae',
    'sub_0a5ffe', 'wlc_phy_set_analog_tx_lpf', 'wlc_phy_set_analog_rx_lpf',
    'wlc_phy_set_tx_afe_dacbuf_cap', 'sub_08f2e9', 'wlc_phy_populate_recipcoeffs_acphy',
    'wlc_phy_calc_extra_init_gain_acphy', 'wlc_phy_rfctrl_override_rxgain_acphy',
    'wlc_phy_lpf_hpc_override_acphy', 'wlc_phy_dig_lpf_override_acphy',
]
DESENSE_LEAVES = [
    # receive gain control and desense (acphy-desense)
    'sub_09af05', 'sub_099658', 'sub_0998cc', 'sub_09a121', 'sub_099f29', 'sub_09175a',
    'sub_091b6e', 'sub_090493', 'sub_09a539', 'sub_08f84d', 'sub_090b77', 'sub_092efb',
    'sub_0909fd', 'sub_08f41b', 'wlc_phy_crs_min_pwr_cal_acphy', 'wlc_phy_hwaci_setup_acphy',
    'wlc_phy_aci_w2nb_setup_acphy', 'wlc_phy_hwaci_engine_acphy',
    'wlc_phy_desense_aci_reset_params_acphy', 'wlc_phy_noise_sample_request_crsmincal',
    'wlc_phy_ed_thres_acphy',
]
TXPOWER_LEAVES = [
    # transmit power (acphy-txpower)
    'sub_098949', 'wlc_phy_txpwr_by_index_acphy', 'wlc_phy_txpwrctrl_enable_acphy',
    'wlc_phy_tssivisible_thresh_acphy', 'sub_0affa9', 'sub_0af7f4', 'sub_093f74',
    'sub_0995ab', 'sub_099528', 'sub_09949f', 'sub_08f9b4', 'sub_0b1227', 'sub_09868f',
    'wlc_phy_txpower_sromlimit_get_acphy', 'sub_08ef5f', 'sub_093ebe', 'sub_09bbe4',
    'sub_09be13', 'sub_0b0455', 'wlc_phy_precal_txgain_acphy', 'sub_090381',
    'wlc_phy_get_paparams_for_band_acphy', 'sub_09c161', 'sub_09bf99', 'sub_09c4e4',
    'sub_098751',
]

# Areas not yet implemented (their accesses are excluded from the comparison).
# Shrink this as each leaf task lands: [] means the whole PHY is compared.
STUBBED_AREAS = []     # txpower round: the whole PHY is compared, nothing excluded
LEAVES = [f for area in STUBBED_AREAS for f in area]

INIT_API = ['bcm4360_phy_init', 'bcm4360_phy_set_scramb_dyn_bw_en',
            'bcm4360_phy_ldpc_override_set', 'bcm4360_phy_switch_radio', 'bcm4360_phy_anacore']
CHANSPEC_API = ['bcm4360_phy_chanspec_set']
CAL_API = ['bcm4360_phy_tempsense', 'bcm4360_phy_cals']


@scenario
def phy_init(objs, show):
    """PHY initialisation (the leaf functions of rxgain/desense/txpower stubbed)."""
    results = []
    for model in PHY_MODELS:
        for session in ('up', 'up-again'):
            res = phy_run(objs, show, INIT_API, model, session=session, without=LEAVES)
            res['differences'] += need(res['open'], INIT_API, show and not results)
            results.append(res)
    return total(results)


@scenario
def phy_chanspec(objs, show):
    """Setting the channel (the leaf functions of rxgain/desense/txpower stubbed)."""
    results = []
    for model in PHY_MODELS:
        res = phy_run(objs, show, CHANSPEC_API, model, session='full', without=LEAVES)
        res['differences'] += need(res['open'], CHANSPEC_API, show and not results)
        results.append(res)
    return total(results)


@scenario
def phy_cal(objs, show):
    """Calibrations: temperature sense and the single-shot cal run (nothing stubbed)."""
    results = []
    for model in PHY_MODELS:
        res = phy_run(objs, show, CAL_API, model, session='cal', without=[])
        res['differences'] += need(res['open'], CAL_API, show and not results)
        results.append(res)
    return total(results)


@scenario
def phy_attach_state(objs, show):
    """PHY attach: the state made from the variables of the board."""
    out = {'stages': 0, 'ref': 0, 'got': 0, 'differences': 0}
    for model in PHY_MODELS:
        for variables in PHY_VARIABLES:
            r = phy_reference(model, variables, session=False)
            o = OpenPhy(objs, model, variables)
            t = o.t
            first = [st for st in r.stages if st.func == 'wlc_phy_attach'][0]
            if not o.has('bcm4360_phy_attach') or not o.has('bcm4360_phy_get_var'):
                if show:
                    print('not in the open code: bcm4360_phy_attach, bcm4360_phy_get_var')
                out['differences'] += 1
                return out
            ab.restore(t.chip, first.state)
            o.replay.load(r.log.between(first.start, first.end))
            phy, trace = o.call('bcm4360_phy_attach', o.hw, make_board(t, r, o.hw))
            if not phy:
                out['differences'] += 1
                continue
            value = t.m.buf(8)
            cores = r.s.m.r8(r.pi + 0x168)
            for name, entry in sorted(phy_state.table(cores).items()):
                for index in range(len(entry[1])):
                    want = phy_state.read(r.s.m, r.pi, entry, index)
                    t.m.w32(value, 0xdeadbeef)
                    known, _ = o.call('bcm4360_phy_get_var', phy, t.cstring(name), index, value)
                    have = t.m.r32(value) & ((1 << (8 * entry[2])) - 1)
                    out['stages'] += 1
                    out['ref'] += 1
                    out['got'] += 1 if known & 0xff else 0
                    if not known & 0xff or have != want:
                        out['differences'] += 1
                        if show:
                            print('PHY rev %d, variables "%s": %s[%d]: %s, the object has %#x'
                                  % (model['phy_rev'], variables, name, index,
                                     '%#x' % have if known & 0xff else 'not known', want))
    return out


def gaps():
    """What the specifications do not cover yet: the functions of the PHY
    files that make accesses in a session and are in no "Scope" table."""
    import glob
    specs = sorted(os.path.splitext(os.path.basename(p))[0]
                   for p in glob.glob(os.path.join(SPEC_DIR, '*.md')))
    r = phy_reference(PHY_MODELS[0])
    s = r.s
    inside = phy_functions(s.m.blob)
    missing = not_covered(s.m.blob, specs)
    carriers = ab.CARRIERS | SERVICE_FUNCS
    count = {}
    callers = {}
    total = 0
    for st in r.stages:
        if st.nested:
            continue
        for a in s.chip.trace[st.start:st.end]:
            if a.space.split('.')[0] not in PHY_SPACES:
                continue
            chain = ab.chain_of(a, s.names)
            if any(n in SERVICE_FUNCS for n in chain):
                continue
            total += 1
            own = [n for n in chain if n in inside and n not in carriers]
            if own and own[0] in missing:
                count[own[0]] = count.get(own[0], 0) + 1
                callers.setdefault(own[0], set()).update(own[1:2])
    print('specifications: %s' % ' '.join(specs))
    print('%d accesses of the PHY in the session, %d of them made by functions that no '
          'specification describes:' % (total, sum(count.values())))
    for n, c in sorted(count.items(), key=lambda kv: -kv[1]):
        f = s.m.blob.fbyname[n]
        print('%7d  %-40s .text+%#08x %5d bytes, called by %s'
              % (c, n, f.addr - 0x01000000, f.size, ', '.join(sorted(callers[n]))))


if __name__ == '__main__':
    import sys
    if sys.argv[1:] == ['gaps']:
        gaps()
    else:
        sys.exit('usage: phy_scenarios.py gaps')
