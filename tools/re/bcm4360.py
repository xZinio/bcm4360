#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Model of a BCM4360 PCIe card (14e4:43a0) for the emulator, see chip.py.

Core list as enumerated by the kernel's bcma on this chip family:
ChipCommon rev 43, 802.11 MAC rev 42, ARM CR4 rev 2, PCIe Gen2 rev 1,
USB 2.0 device rev 17.
"""
from chip import Chip, Core, SICF_CLOCK_EN
from emu import EmuError

# ---- ChipCommon register offsets
CC_CHIPID = 0x00
CC_CAPABILITIES = 0x04
CC_CORECONTROL = 0x08
CC_OTPSTATUS = 0x10
CC_OTPCONTROL = 0x14
CC_OTPPROG = 0x18
CC_OTPLAYOUT = 0x1c
CC_INTSTATUS = 0x20
CC_INTMASK = 0x24
CC_CHIPCONTROL = 0x28
CC_CHIPSTATUS = 0x2c
CC_GPIOIN = 0x64
CC_WATCHDOG = 0x80
CC_CAPABILITIES_EXT = 0xac
CC_EROMPTR = 0xfc
CC_SROMCONTROL = 0x190
CC_SROMADDRESS = 0x194
CC_SROMDATA = 0x198
CC_CLK_CTL_ST = 0x1e0
CC_PMUCONTROL = 0x600
CC_PMUCAPABILITIES = 0x604
CC_PMUSTATUS = 0x608
CC_RES_STATE = 0x60c
CC_RES_PENDING = 0x610
CC_PMUTIMER = 0x614
CC_MIN_RES_MASK = 0x618
CC_MAX_RES_MASK = 0x61c
CC_RES_TABLE_SEL = 0x620
CC_RES_DEP_MASK = 0x624
CC_RES_UPDN_TIMER = 0x628
CC_CHIPCONTROL_ADDR = 0x650
CC_CHIPCONTROL_DATA = 0x654
CC_REGCONTROL_ADDR = 0x658
CC_REGCONTROL_DATA = 0x65c
CC_PLLCONTROL_ADDR = 0x660
CC_PLLCONTROL_DATA = 0x664
CC_PMU_XTALFREQ = 0x66c
CC_SROM_OTP = 0x800

CC_CAP_PMU = 0x10000000
CC_CAP_SROM = 0x40000000
CC_CAP_OTPSIZE = 0x00380000

# clk_ctl_st, the same layout in every core
CCS_FORCEALP = 0x00000001
CCS_FORCEHT = 0x00000002
CCS_FORCEILP = 0x00000004
CCS_ALPAREQ = 0x00000008
CCS_HTAREQ = 0x00000010
CCS_FORCEHWREQOFF = 0x00000020
CCS_ERSRC_REQ_MASK = 0x00000700
CCS_ALPAVAIL = 0x00010000
CCS_HTAVAIL = 0x00020000
CCS_BP_ON_ALP = 0x00040000
CCS_BP_ON_HT = 0x00080000


def clk_ctl_st(value):
    """Clock status as the hardware reports it for a given request."""
    v = value & 0xffff
    # with the PMU resources up the clocks the core asks for become available
    v |= CCS_ALPAVAIL | CCS_BP_ON_ALP
    if value & (CCS_FORCEHT | CCS_HTAREQ) or not value & CCS_FORCEALP:
        v |= CCS_HTAVAIL | CCS_BP_ON_HT
    return v


class ChipCommon(Core):
    name = 'chipcommon'
    coreid = 0x800
    rev = 43

    def reset(self):
        chip = self.chip
        self.resetctrl = 0
        self.ioctrl = SICF_CLOCK_EN
        self.regs[CC_CHIPID] = (chip.chip_id | chip.chip_rev << 16 | chip.chip_pkg << 20
                                | len(chip.core_classes) << 24 | 1 << 28)
        self.regs[CC_CAPABILITIES] = chip.cc_caps
        self.regs[CC_CAPABILITIES_EXT] = chip.cc_caps_ext
        self.regs[CC_CHIPSTATUS] = chip.chipstatus
        self.regs[CC_EROMPTR] = chip.erom_base
        self.regs[CC_PMUCAPABILITIES] = chip.pmu_caps
        self.regs[CC_MIN_RES_MASK] = chip.pmu_min_res
        self.regs[CC_MAX_RES_MASK] = chip.pmu_max_res
        self.regs[CC_SROMCONTROL] = 1 if chip.srom is not None else 0
        self.regs[CC_OTPLAYOUT] = chip.otp_layout
        self.regs[CC_OTPSTATUS] = chip.otp_status
        self.chipctl = {}
        self.regctl = {}
        self.pllctl = {}
        self.res_updn = {}
        self.res_dep = {}

    def read(self, off, size):
        chip = self.chip
        if off == CC_CLK_CTL_ST:
            return clk_ctl_st(self.peek(off))
        if off in (CC_RES_STATE,):
            # resources follow the requested masks at once
            return (self.peek(CC_MAX_RES_MASK) & chip.pmu_res_up()) | self.peek(CC_MIN_RES_MASK)
        if off == CC_RES_PENDING:
            return 0
        if off == CC_PMUSTATUS:
            return chip.pmu_status
        if off == CC_PMUTIMER:
            # ILP clock, 32.768 kHz
            return (self.chip.m.os.now_us * 32768 // 1000000) & 0xffffffff
        if off == CC_CHIPCONTROL_DATA:
            return self.chipctl.get(self.peek(CC_CHIPCONTROL_ADDR), 0)
        if off == CC_REGCONTROL_DATA:
            return self.regctl.get(self.peek(CC_REGCONTROL_ADDR), 0)
        if off == CC_PLLCONTROL_DATA:
            return self.pllctl.get(self.peek(CC_PLLCONTROL_ADDR), chip.pll_defaults.get(
                self.peek(CC_PLLCONTROL_ADDR), 0))
        if off == CC_RES_UPDN_TIMER:
            return self.res_updn.get(self.peek(CC_RES_TABLE_SEL), 0)
        if off == CC_RES_DEP_MASK:
            return self.res_dep.get(self.peek(CC_RES_TABLE_SEL), 0)
        if off == CC_SROMDATA:
            return self.peek(off)
        if CC_SROM_OTP <= off < CC_SROM_OTP + 0x800:
            return chip.srom_otp_read(off - CC_SROM_OTP, size)
        return self.peek(off, size)

    def write(self, off, size, value):
        chip = self.chip
        if off in (CC_CHIPID, CC_CAPABILITIES, CC_CHIPSTATUS, CC_EROMPTR, CC_PMUCAPABILITIES):
            return
        if off == CC_CHIPCONTROL_DATA:
            self.chipctl[self.peek(CC_CHIPCONTROL_ADDR)] = value
            chip.record('W', 'pmu.chipctl', self.peek(CC_CHIPCONTROL_ADDR), 4, value)
            return
        if off == CC_REGCONTROL_DATA:
            self.regctl[self.peek(CC_REGCONTROL_ADDR)] = value
            chip.record('W', 'pmu.regctl', self.peek(CC_REGCONTROL_ADDR), 4, value)
            return
        if off == CC_PLLCONTROL_DATA:
            self.pllctl[self.peek(CC_PLLCONTROL_ADDR)] = value
            chip.record('W', 'pmu.pllctl', self.peek(CC_PLLCONTROL_ADDR), 4, value)
            return
        if off == CC_RES_UPDN_TIMER:
            self.res_updn[self.peek(CC_RES_TABLE_SEL)] = value
            return
        if off == CC_RES_DEP_MASK:
            self.res_dep[self.peek(CC_RES_TABLE_SEL)] = value
            return
        if off == CC_OTPPROG:
            # bit 31 starts a command (bits 24..27) and reads back 0 when it is
            # done.  Command 0 reads the bit at row (bits 8..15), column (bits
            # 0..7); the bit is returned in bit 29, bit 28 reports an error.
            if value & 0x80000000:
                value &= ~0xb0000000
                if (value >> 24) & 0xf == 0:
                    bit = ((value >> 8) & 0xff) * chip.otp_cols + (value & 0xff)
                    chip.record('R', 'otp.bit', bit, 1, chip.otp_bit(bit))
                    value |= chip.otp_bit(bit) << 29
            self.poke(off, 4, value)
            return
        if off == CC_SROMCONTROL:
            # bit 31 starts an operation, opcode in bits 29..30 (0 = read)
            if value & 0x80000000:
                op = (value >> 29) & 3
                addr = self.peek(CC_SROMADDRESS) & 0xffff
                if op == 0:
                    self.poke(CC_SROMDATA, 4, chip.srom_word(addr // 2))
                value &= ~0x80000000
            self.poke(off, 4, (value & ~1) | (1 if chip.srom is not None else 0))
            return
        if off == CC_WATCHDOG and value:
            raise EmuError('chipcommon watchdog armed (%#x): chip reset requested' % value)
        self.poke(off, size, value)


class D11(Core):
    """802.11 MAC core.  Only what the host driver can observe is modelled."""
    name = 'd11'
    coreid = 0x812
    rev = 42
    nmw = 1
    nsw = 1
    nmp = 1

    MACCONTROL = 0x120
    MACCOMMAND = 0x124
    MACINTSTATUS = 0x128
    MACINTMASK = 0x12c
    OBJADDR = 0x160
    OBJDATA = 0x164
    CLK_CTL_ST = 0x1e0
    PHYVERSION = 0x3e0
    PHYREGADDR = 0x3fc
    PHYREGDATA = 0x3fe
    RADIOREGADDR = 0x3d8
    RADIOREGDATA = 0x3da
    PSM_PHY_HDR_PARAM = 0x492

    MCTL_PSM_RUN = 0x00000002
    MCTL_EN_MAC = 0x00000001
    MI_MACSSPNDD = 0x00000001

    SISF_DB_PHY = 0x0008      # status flags in the wrapper: one PHY for both bands
    SISF_FCLKA = 0x0004       # fast clock available

    def reset(self):
        self.objmem = {}          # (selector, address) -> 16 bit cells
        self.phy = self.chip.phy
        self.phy.d11 = self
        self.iostatus = self.SISF_DB_PHY | self.SISF_FCLKA

    def _obj(self, write, size, value=0, high=False):
        sel = (self.peek(self.OBJADDR) >> 16) & 0x7
        addr = self.peek(self.OBJADDR) & 0xffff
        names = {0: 'ucode', 1: 'shm', 2: 'scr', 3: 'ihr', 4: 'amt'}
        space = names.get(sel, 'obj%d' % sel)
        # shared memory is addressed in 16 bit words through a 32 bit window:
        # address in units of 4 bytes, the upper half is at objdata + 2
        key = (sel, addr * 4 + (2 if high else 0)) if sel in (1, 2, 3, 4) else (sel, addr)
        if write:
            if sel in (1, 2, 3, 4) and size == 4:
                self.objmem[key] = value & 0xffff
                self.objmem[(sel, key[1] + 2)] = value >> 16
            else:
                self.objmem[key] = value
            self.chip.record('W', space, key[1], size, value)
            self.chip.obj_written(space, key[1], size, value)
            return None
        if sel in (1, 2, 3, 4) and size == 4:
            v = self.objmem.get(key, 0) | self.objmem.get((sel, key[1] + 2), 0) << 16
        else:
            v = self.objmem.get(key, 0)
        v = self.chip.obj_read(space, key[1], size, v)
        self.chip.record('R', space, key[1], size, v)
        return v

    def read(self, off, size):
        if off == self.OBJDATA:
            return self._obj(False, size)
        if off == self.OBJDATA + 2:
            return self._obj(False, size, high=True)
        if off == self.CLK_CTL_ST:
            return clk_ctl_st(self.peek(off))
        if off == self.PHYVERSION and size == 2:
            return self.phy.version
        if off == self.PHYREGDATA:
            return self.phy.reg_read(self.peek(self.PHYREGADDR, 2))
        if off == self.RADIOREGDATA:
            return self.phy.radio_read(self.peek(self.RADIOREGADDR, 2))
        if off == self.MACINTSTATUS:
            return self.peek(off)
        return self.peek(off, size)

    def write(self, off, size, value):
        if off == self.OBJDATA:
            return self._obj(True, size, value)
        if off == self.OBJDATA + 2:
            return self._obj(True, size, value, high=True)
        # address and data registers are adjacent 16 bit registers: the driver
        # writes both with one 32 bit access (data in the upper half)
        if off == self.PHYREGADDR and size == 4:
            self.poke(off, 2, value & 0xffff)
            return self.phy.reg_write(value & 0xffff, value >> 16)
        if off == self.RADIOREGADDR and size == 4:
            self.poke(off, 2, value & 0xffff)
            return self.phy.radio_write(value & 0xffff, value >> 16)
        if off == self.PHYREGDATA:
            return self.phy.reg_write(self.peek(self.PHYREGADDR, 2), value)
        if off == self.RADIOREGDATA:
            return self.phy.radio_write(self.peek(self.RADIOREGADDR, 2), value)
        if off == self.MACINTSTATUS:
            # write one to clear
            self.poke(off, 4, self.peek(off) & ~value)
            return
        # command registers of the buffer memory controller (core revisions
        # >= 40): the driver starts a command and polls until the hardware
        # has cleared the register (0x530) or the start bit (0x540)
        if off == 0x530 and size == 2 and value & 0x8000:
            self.poke(off, 2, 0)
            return
        if off == 0x540 and size == 2:
            self.poke(off, 2, value & ~1)
            return
        if off == self.MACCONTROL:
            old = self.peek(off)
            self.poke(off, 4, value)
            if old & self.MCTL_EN_MAC and not value & self.MCTL_EN_MAC:
                # the MAC acknowledges the suspend request
                self.poke(self.MACINTSTATUS, 4, self.peek(self.MACINTSTATUS) | self.MI_MACSSPNDD)
            if value & self.MCTL_PSM_RUN and not old & self.MCTL_PSM_RUN:
                # the microcode starts, initialises and suspends itself
                self.poke(self.MACINTSTATUS, 4, self.peek(self.MACINTSTATUS) | self.MI_MACSSPNDD)
                self.chip.ucode_started()
            return
        self.poke(off, size, value)


class Phy:
    """PHY and radio register files behind the MAC core."""

    def __init__(self, chip, phy_type, phy_rev, radio_id, radio_rev):
        self.chip = chip
        self.version = (phy_type << 8) | phy_rev
        self.radio_id = radio_id
        self.radio_rev = radio_rev
        # register 0 holds the revision (assumed; the driver uses its low 4 bits)
        self.regs = {0: phy_rev}
        # the id code of the radio is in its first two registers
        self.radio = {0: radio_rev, 1: radio_id}
        self.tables = {}        # table id -> {offset: entry}
        self.unknown = set()
        self.tbl_word = 0       # position inside a wide entry
        self.tbl_acc = 0
        self.tbl_hi = 0

    # Table memory behind the registers 0x0d (id), 0x0e (offset), 0x0f (data,
    # low), 0x10 (data, high), 0x11 (data, wide); docs/re/spec/access.md.
    # Entry widths in bits by table id, as the driver uses them (collected from
    # its table transfers during bring-up, channel changes and calibration).
    # The tables of the second and third core have the ids of the first
    # plus 0x20 and 0x40.
    TABLE_WIDTH = {0x01: 16, 0x02: 8, 0x03: 32, 0x04: 8, 0x05: 32, 0x07: 16, 0x0a: 8,
                   0x0b: 8, 0x0c: 16, 0x0e: 32, 0x10: 32, 0x11: 48, 0x14: 48, 0x20: 48,
                   0x21: 32, 0x40: 16, 0x41: 32, 0x42: 16, 0x44: 8, 0x45: 8, 0x47: 32,
                   0x48: 32}
    T_ID, T_OFF, T_LO, T_HI, T_WIDE = 0x0d, 0x0e, 0x0f, 0x10, 0x11

    def table_width(self, tid):
        w = self.TABLE_WIDTH.get(tid)
        if w is None and tid >= 0x60:
            w = self.TABLE_WIDTH.get(0x40 + (tid & 0x1f))
        return w

    def _entry(self):
        return self.tables.get(self.regs.get(self.T_ID, 0), {}).get(
            self.regs.get(self.T_OFF, 0), 0)

    def _store(self, value):
        tid = self.regs.get(self.T_ID, 0)
        self.tables.setdefault(tid, {})[self.regs.get(self.T_OFF, 0)] = value
        self._advance()

    def _advance(self):
        self.regs[self.T_OFF] = (self.regs.get(self.T_OFF, 0) + 1) & 0xffff
        self.tbl_word = 0
        self.tbl_acc = 0

    def _table_read(self, addr):
        width = self.table_width(self.regs.get(self.T_ID, 0))
        if addr == self.T_LO:
            e = self._entry()
            self.tbl_hi = (e >> 16) & 0xffff
            self._advance()
            return e & (0xff if width == 8 else 0xffff)
        if addr == self.T_HI:
            return self.tbl_hi
        words = 4 if width in (60, 64) else 3
        v = (self._entry() >> (16 * self.tbl_word)) & 0xffff
        self.tbl_word += 1
        if self.tbl_word >= words:
            self._advance()
        return v

    def _table_write(self, addr, value):
        width = self.table_width(self.regs.get(self.T_ID, 0))
        if addr == self.T_HI:
            self.tbl_hi = value
            self.tbl_have_hi = True
        elif addr == self.T_LO:
            if width == 32 or (width is None and self.tbl_have_hi):
                value |= self.tbl_hi << 16
            elif width == 8:
                value &= 0xff
            self.tbl_have_hi = False
            self._store(value)
        else:
            self.tbl_acc |= value << (16 * self.tbl_word)
            self.tbl_word += 1
            if self.tbl_word >= (4 if width in (60, 64) else 3):
                self._store(self.tbl_acc)

    tbl_have_hi = False

    def reg_read(self, addr):
        if self.T_LO <= addr <= self.T_WIDE:
            v = self._table_read(addr)
        else:
            v = self.regs.get(addr, 0)
        v = self.chip.phy_read(addr, v)
        self.chip.record('R', 'phy', addr, 2, v)
        return v

    def reg_write(self, addr, value):
        self.chip.record('W', 'phy', addr, 2, value)
        if self.T_LO <= addr <= self.T_WIDE:
            self._table_write(addr, value)
            return
        self.regs[addr] = value
        if addr in (self.T_ID, self.T_OFF):
            self.tbl_word = 0
            self.tbl_acc = 0
        self.chip.phy_written(addr, value)

    def radio_read(self, addr):
        v = self.chip.radio_read(addr, self.radio.get(addr, 0))
        self.chip.record('R', 'radio', addr, 2, v)
        return v

    def radio_write(self, addr, value):
        self.chip.record('W', 'radio', addr, 2, value)
        self.radio[addr] = value


class ArmCR4(Core):
    name = 'armcr4'
    coreid = 0x83e
    rev = 2
    nmw = 1
    nsw = 1
    nmp = 1


class Pcie2(Core):
    name = 'pcie'
    coreid = 0x83c
    rev = 1
    nmw = 1
    nsw = 1
    nmp = 1

    CONFIGADDR = 0x120
    CONFIGDATA = 0x124

    def reset(self):
        self.resetctrl = 0
        self.ioctrl = SICF_CLOCK_EN
        self.cfgind = {}

    def read(self, off, size):
        if off == self.CONFIGDATA:
            a = self.peek(self.CONFIGADDR)
            v = self.cfgind.get(a, 0)
            self.chip.record('R', 'pcie.cfg', a, 4, v)
            return v
        return self.peek(off, size)

    def write(self, off, size, value):
        if off == self.CONFIGDATA:
            a = self.peek(self.CONFIGADDR)
            self.cfgind[a] = value
            self.chip.record('W', 'pcie.cfg', a, 4, value)
            return
        self.poke(off, size, value)


class Usb20d(Core):
    name = 'usb20d'
    coreid = 0x81a
    rev = 17
    nmw = 1
    nsw = 1
    nmp = 1


class Bcm4360(Chip):
    device = 0x43a0
    revision = 0x03
    subvendor = 0x106b        # Apple
    subdevice = 0x0117
    chip_id = 0x4360
    chip_rev = 3
    chip_pkg = 0
    erom_base = 0x180fe000
    cc_caps = CC_CAP_PMU | CC_CAP_SROM | 0x00280000
    cc_caps_ext = 0
    chipstatus = 0x00000001     # bit 0: the crystal is 40 MHz
    pmu_caps = 0x10a22b11 & 0xffffff00 | 17     # PMU rev 17, 11 resources
    pmu_status = 0
    pmu_min_res = 0
    pmu_max_res = 0x1ff
    # PLL control 2: integer divider 24 in bits 7.., integer mode: with the
    # 40 MHz crystal the baseband VCO runs at 960 MHz
    pll_defaults = {2: 24 << 7 | 1}
    otp_layout = 0x00010140     # 40 nm OTP, hardware region at bit 0x140
    otp_status = 0x1000         # OTP ready
    otp_cols = 32
    core_classes = (ChipCommon, D11, ArmCR4, Pcie2, Usb20d)

    def __init__(self, m, srom=None, otp=None, phy_rev=1, radio_rev=4, layout=None):
        self.srom = srom            # list of 16 bit words, or None: no SROM
        self.otp = otp or {}        # word number -> 16 bit value; blank otherwise
        self.phy = Phy(self, 11, phy_rev, 0x2069, radio_rev)
        # cores as (core id, revision, base, wrapper), e.g. from the enumeration
        # ROM of a real card; the default is the list in core_classes
        self.layout = layout
        super().__init__(m)

        m.on_call('wlc_phy_write_table_ext', self._table_write, 2)
        m.on_call('wlc_phy_read_table_ext', self._table_read, 2)

    # PHY tables are written through five registers (id, offset and three data
    # registers of different width).  The functions that do it take a
    # descriptor { data, length, id, offset, width }; logging that is more
    # useful than the register accesses it results in.
    def _table(self, m, info):
        ptr = m.r64(info)
        n, tid, off, width = (m.r32(info + 8 + 4 * i) for i in range(4))
        return ptr, n, tid, off, width

    def _table_write(self, m, pi, info):
        ptr, n, tid, off, width = self._table(m, info)
        size = {8: 1, 16: 2, 32: 4, 48: 6, 60: 8, 64: 8}.get(width)
        if size is None:
            return
        raw = m.read(ptr, n * size)
        for i in range(n):
            v = int.from_bytes(raw[i * size:(i + 1) * size], 'little')
            self.record('W', 'tbl.%02x' % tid, off + i, size, v)

    def _table_read(self, m, pi, info):
        ptr, n, tid, off, width = self._table(m, info)
        self.record('R', 'tbl.%02x' % tid, off, n, width)

    def otp_word(self, i):
        return self.otp.get(i, 0)

    def otp_bit(self, bit):
        return (self.otp_word(bit // 16) >> (bit % 16)) & 1

    def build(self):
        if self.layout:
            known = {cls.coreid: cls for cls in self.core_classes}
            for coreid, rev, base, wrap in self.layout:
                cls = known.get(coreid)
                if cls is None:
                    cls = type('Core%03x' % coreid, (Core,),
                               {'name': 'core%03x' % coreid, 'coreid': coreid})
                c = cls(self, base, wrap if wrap is not None else 0)
                c.rev = rev
                self.cores.append(c)
            return
        base = 0x18000000
        wrap = 0x18100000
        for cls in self.core_classes:
            c = cls(self, base, wrap)
            self.cores.append(c)
            base += 0x1000
            wrap += 0x1000 * (cls.nmw + cls.nsw)

    def pmu_res_up(self):
        return 0xffffffff

    def srom_word(self, i):
        if self.srom is None or i >= len(self.srom):
            return 0xffff
        return self.srom[i]

    def srom_otp_read(self, off, size):
        # window in ChipCommon: shows the SROM if the card has one, else the OTP
        i = off // 2
        word = self.srom_word if self.srom is not None else self.otp_word
        v = word(i)
        if size == 4:
            v |= word(i + 1) << 16
        return v

    # hooks for the indirect spaces; refined as the PHY is understood
    def phy_read(self, addr, value):
        return value

    def phy_written(self, addr, value):
        pass

    # Status of the radio's calibration engines. With radio_done they finish
    # at once, as far as the driver can tell: the "done" bits it polls are set
    # (docs/re/spec/acphy-radio.md) and the result registers hold the values
    # below. Without it the bits stay 0 and the driver's loops time out.
    radio_done = False
    rcal_result = 0
    rccal_result = (0x100, 0x180, 0x1ef)     # RADIO(0x414), (0x415), (0x416)

    def radio_read(self, addr, value):
        if not self.radio_done:
            return value
        if addr == 0x40b:                   # resistor calibration: done = bit 3
            return (value | 8 | self.rcal_result << 4) if value & 1 else value
        if addr == 0x413:                   # RC calibration: done = bit 4
            return value | 0x10
        if 0x414 <= addr <= 0x416:
            return self.rccal_result[addr - 0x414]
        if addr == 0x90b:                   # VCO calibration: done = bit 8
            return value | 0x100
        if addr in (0x144, 0x344, 0x544):   # converter calibration of a core
            return value | 3
        return value

    def obj_read(self, space, addr, size, value):
        return value

    def obj_written(self, space, addr, size, value):
        pass

    def ucode_started(self):
        pass
