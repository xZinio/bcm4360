#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
A/B scenarios for the MAC core (wlc_bmac.c), docs/re/tasks/bmac-init.md.

The object is run through a session (attach, up, channel changes, a watchdog
tick, down).  Every call of a MAC function under test is a *stage*: at its
entry the card state and the software copies in `wlc_hw` are saved, and the
accesses the function makes (the 802.11 core registers D11(), the shared
memory SHM(), and the delays) are noted.  The open function is then called
with the same arguments on a card in the same state, and the two access
sequences compared.

Increments so far (each a scenario):
  mac-ctrl  bmac-init.md A1-A7: mctrl, microcode wake override, suspend/enable.
  mac-mhf   bmac-init.md A9:    the microcode host flags (per-band copies, the
            current band's copy written to shared memory when the clock is on).

Both touch only D11()/SHM() and delays and call no other layer, so no services
have to be modelled.  Run with ab.py (`python ab.py run mac-ctrl --show 40`).
Run directly (`python mac_scenarios.py`) for a report of the object side only.
"""
import struct

import ab
from emu import STOP_ADDR
from scenarios import scenario, make_hw, set_channel

# The spaces an open MAC function under test may touch.  ab.logical() drops the
# raw address/data mechanics of the indirect accesses (RAW_D11) and keeps the
# decoded shared-memory accesses (space 'shm').
MAC_SPACES = ('d11', 'shm', 'delay')

# control-primitive object function -> (open function, u32 args after wlc_hw)
MAC_FUNCS = {
    'wlc_bmac_mctrl':                ('bcm4360_mac_mctrl', 2),
    'wlc_ucode_wake_override_set':   ('bcm4360_mac_wake_override_set', 1),
    'wlc_ucode_wake_override_clear': ('bcm4360_mac_wake_override_clear', 1),
    'wlc_bmac_suspend_mac_and_wait': ('bcm4360_mac_suspend', 0),
    'wlc_bmac_enable_mac':           ('bcm4360_mac_enable', 0),
}
MHF_FUNC = 'wlc_bmac_mhf'
WATCHED = list(MAC_FUNCS) + [MHF_FUNC]

# What the object is taken through.  up/down, a channel change and a band
# change (5 GHz -> 2.4 GHz) call these functions many times over.
MAC_SESSION = ['up', '36/80', '6', 'watchdog', 'down']

_ref = {}


def mac_reference():
    """Run the object through a session; cached.  Returns (session, stages).

    The window [start:end] of a stage is bounded by the actual entry and
    return of that one invocation (m.at_return), not by ab.close_stages' scan
    of the function's address range: the control functions call one another
    and call the same function twice in a row; a zero-access call immediately
    followed by another call of the same function would otherwise absorb the
    neighbour's accesses (a stage-boundary artifact of the range scan).
    """
    if 'r' in _ref:
        return _ref['r']
    s = ab.Session()
    s.chip.radio_done = True
    d, m, chip = s.d, s.m, s.chip
    ab.delays(d)

    def inspect(st, m, args):
        """Save the `wlc_hw` copies at entry; close the window at the return."""
        hw = args[0]
        pcur, p2g, p5g = (m.r64(hw + off) for off in (0xe8, 0xf0, 0xf8))

        def copies(b):
            return [m.r16(b + 8 + 2 * i) for i in range(5)] if b else [0] * 5

        st.extra = {
            'maccontrol':        m.r32(hw + 0x168),
            'mac_suspend_depth': m.r32(hw + 0x16c),
            'wake_override':     m.r32(hw + 0x170),
            'mute_override':     m.r32(hw + 0x174),
            'fastpwrup_dly':     m.r16(hw + 0x192),
            'clk':               m.r8(hw + 0x186),
            # host-flag copies per band and which band is current (A9)
            'mhf2g':             copies(p2g),
            'mhf5g':             copies(p5g),
            'current_band':      1 if pcur and pcur == p5g else 0,
            'band_present':      (1 if p2g else 0) | (2 if p5g else 0),
        }
        if m.r64(m.mu.reg_read(44)) != STOP_ADDR:   # 44 = UC_X86_REG_RSP
            def done(mm, result, st=st):
                st.end = len(chip.trace)
            m.at_return(done)

    stages = ab.watch(d, WATCHED, inspect)
    d.attach()
    if not d.wlc:
        raise ab.EmuError('the object does not attach (error %d)' % d.err)
    d.iovar_setint('mpc', 0)
    for step in MAC_SESSION:
        if step == 'up':
            if d.up() != 0:
                raise ab.EmuError('the object does not come up')
        elif step == 'down':
            m.call('wlc_down', d.wlc)
        elif step == 'watchdog':
            m.os.run_timers(m, m.os.now_us // 1000 + 2500)
        else:
            set_channel(s, step)
    for st in stages:
        if st.end is None:          # never returned before the session ended
            st.end = st.start
    _ref['r'] = (s, stages)
    return s, stages


# struct bcm4360_mac of docs/re/tasks/bmac-init.md.  The control-primitive
# fields (A1-A7) come first; the host-flag state (A9) is below the "you may add
# fields below this line" marker.  The harness packs the whole struct for every
# stage (a control function simply does not read the host-flag fields).
#   void *hw; u32 maccontrol, mac_suspend_depth, wake_override, mute_override;
#   u16 fastpwrup_dly; u8 clk, mac_rev; u32 chip_id;
#   u16 mhf[2][5];   /* [0] = 2.4 GHz band, [1] = 5 GHz band */
#   u8 current_band; /* 0 = 2.4 GHz is current, 1 = 5 GHz is current */
#   u8 band_present; /* bit 0: 2.4 GHz state exists, bit 1: 5 GHz state exists */
MAC_FMT = '<QIIIIHBBI10HBB'
MAC_REV = 42


def make_mac(t, hw, st):
    x = st.extra
    p = t.m.buf(64)
    t.m.write(p, struct.pack(MAC_FMT, hw, x['maccontrol'], x['mac_suspend_depth'],
                             x['wake_override'], x['mute_override'],
                             x['fastpwrup_dly'], x['clk'] & 0xff, MAC_REV,
                             t.chip.chip_id,
                             *x['mhf2g'], *x['mhf5g'],
                             x['current_band'], x['band_present']))
    return p


def _mac_session_for_open(objs):
    """The open-code session: no attach; state is restored per stage."""
    t = ab.Session(objs)
    t.chip.radio_done = True
    ab.delays(t.d)
    return t, make_hw(t)


def _run(objs, show, want_func, call):
    """Compare every stage of `want_func(st)` : call(t, mac_ptr, st) runs the open code."""
    s, stages = mac_reference()
    t, hw = _mac_session_for_open(objs)
    trace = s.chip.trace
    total_ref = total_got = diffs = count = 0
    for st in stages:
        if not want_func(st):
            continue
        ref = ab.logical(trace[st.start:st.end], s.names, MAC_SPACES)
        ab.restore(t.chip, st.state)
        p = make_mac(t, hw, st)
        n0 = len(t.chip.trace)
        try:
            call(t, p, st)
        except Exception as e:
            return {'stages': count, 'ref': total_ref, 'got': total_got,
                    'differences': diffs + 1,
                    'note': 'open code not callable (%s); built yet?' % e}
        got = ab.logical(t.chip.trace[n0:], t.names, MAC_SPACES)
        count += 1
        total_ref += len(ref)
        total_got += len(got)
        if ref != got:
            diffs += 1
            if show:
                print('stage %s at access %d, args %s:' % (
                    st.func, st.start, ['%#x' % a for a in st.args[1:5]]))
                ab.compare(ref, got, show)
    return {'stages': count, 'ref': total_ref, 'got': total_got, 'differences': diffs}


@scenario
def mac_ctrl(objs, show):
    """MAC control: mctrl, microcode wake override, suspend/enable (bmac-init A1-A7)."""
    def call(t, p, st):
        open_fn, nargs = MAC_FUNCS[st.func]
        args = [st.args[1 + i] & 0xffffffff for i in range(nargs)]
        t.m.call(t.sym(open_fn), p, *args)
    return _run(objs, show, lambda st: st.func in MAC_FUNCS, call)


@scenario
def mac_mhf(objs, show):
    """MAC host flags: per-band copies, current band's copy to shared memory (bmac-init A9)."""
    def call(t, p, st):
        idx = st.args[1] & 0xff
        mask = st.args[2] & 0xffff
        val = st.args[3] & 0xffff
        bands = st.args[4] & 0xffffffff
        t.m.call(t.sym('bcm4360_mac_mhf'), p, idx, mask, val, bands)
    return _run(objs, show, lambda st: st.func == MHF_FUNC, call)


def _report():
    """Object side only: how often each function runs and a sample trace."""
    import collections
    s, stages = mac_reference()
    by = collections.Counter(st.func for st in stages)
    print('stages in the session %s:' % MAC_SESSION)
    for name in WATCHED:
        print('  %-32s %4d' % (name, by[name]))
    print('  %-32s %4d total\n' % ('', len(stages)))
    trace = s.chip.trace
    for name in WATCHED:
        st = next((x for x in stages if x.func == name and x.end > x.start), None)
        if st is None:
            print('%s: no stage with accesses\n' % name)
            continue
        acc = ab.logical(trace[st.start:st.end], s.names, MAC_SPACES)
        print('%s, a call with accesses (args %s), %d accesses:' % (
            name, ['%#x' % a for a in st.args[1:5]], len(acc)))
        for e in acc[:12]:
            print('    ' + ab.fmt(e))
        print()


if __name__ == '__main__':
    _report()
