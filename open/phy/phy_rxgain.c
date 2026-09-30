// SPDX-License-Identifier: ISC
/*
 * AC-PHY of the BCM4360: the board- and calibration-dependent set-up around
 * the receive front end. The leaf functions that the channel function and the
 * initialisation call: front end control (femctrl), the analog transmit and
 * receive low-pass filters and the DAC buffer, the channel/bandwidth/band
 * dependent register and table set-up, the reciprocity coefficients and the
 * Bluetooth-gated receive gain, and the debug/iovar override paths.
 *
 * Written from docs/re/spec/acphy-rxgain.md (a "section" in the comments is a
 * section of its "Procedures") and docs/re/spec/acphy-chanspec.md annexes A1,
 * A2, A3 (the channel/bandwidth/band set-up), and, for the register access,
 * from docs/re/spec/access.md; for chip 0x4360, AC-PHY revision 0 or 1 and
 * radio 2069 revision 3 or 4. The radio part of the band change is done by the
 * finished functions of open/phy/radio2069.c; the receive-gain/desense and
 * transmit-power leaves belong to later tasks. The notation PHY(a), RADIO(a),
 * TBL(id)[i], CC(o) and mod() in the comments is the one of access.md.
 */
#include <bcm4360/phy.h>
#include <bcm4360/phy_env.h>

/* PHY registers accessed here, named by their address (purpose unknown). */
#define ACPHY_REG_0x19e		0x19e	/* bit 1: freeze the RF sequencer */
#define ACPHY_REG_0x164		0x164
#define ACPHY_REG_0x197		0x197
#define ACPHY_REG_0x198		0x198
#define ACPHY_REG_0x199		0x199
#define ACPHY_REG_0x19a		0x19a
#define ACPHY_REG_0x19b		0x19b
#define ACPHY_REG_0x19c		0x19c
#define ACPHY_REG_0x1a0		0x1a0
#define ACPHY_REG_0x1a1		0x1a1
#define ACPHY_REG_0x1a2		0x1a2
#define ACPHY_REG_0x1a3		0x1a3
#define ACPHY_REG_0x1ec		0x1ec
#define ACPHY_REG_0x2d1		0x2d1	/* BT-gated initial gain (sub_08f2e9) */
#define ACPHY_REG_0x2d2		0x2d2
#define ACPHY_REG_0x2e4		0x2e4
#define ACPHY_REG_0x30f		0x30f	/* control channel position, bits 14..15 */
#define ACPHY_REG_0x31c		0x31c
#define ACPHY_REG_0x31d		0x31d
#define ACPHY_REG_0x31e		0x31e
#define ACPHY_REG_0x31f		0x31f
#define ACPHY_REG_0x371		0x371	/* first of the six words from the channel entry */
#define ACPHY_REG_0x3a9		0x3a9	/* 11b transmit filter */
#define ACPHY_REG_0x408		0x408
#define ACPHY_REG_0x40a		0x40a	/* bit 8: front end control enable */
#define ACPHY_REG_0x410		0x410
#define ACPHY_REG_0x414		0x414
#define ACPHY_REG_0x418		0x418	/* BT/WLAN femctrl override, RF-control override mode */
#define ACPHY_REG_0x601		0x601
#define ACPHY_REG_0x1601	0x1601
#define ACPHY_REG_0x1602	0x1602
#define ACPHY_REG_0x1603	0x1603
#define ACPHY_REG_0x1606	0x1606
#define ACPHY_REG_0x1607	0x1607
#define ACPHY_REG_0x16d4	0x16d4
#define ACPHY_REG_0x0ec		0x0ec	/* first of the ten 11b digital filter registers */
#define ACPHY_REG_0x6ed		0x6ed
#define ACPHY_REG_0x6ef		0x6ef
#define ACPHY_REG_0x725		0x725	/* per core */
#define ACPHY_REG_0x73a		0x73a	/* per core */
#define ACPHY_REG_0x722		0x722	/* per core: rx gain override (section 10) */
#define ACPHY_REG_0x723		0x723	/* per core: lpf hpc override (section 11) */
#define ACPHY_REG_0x730		0x730
#define ACPHY_REG_0x731		0x731
#define ACPHY_REG_0x734		0x734
#define ACPHY_REG_0x735		0x735
#define ACPHY_REG_0x18b		0x18b	/* dig-LPF override (section 12): 0x18b..0x194 */
#define ACPHY_REG_0x181		0x181	/* the per-bandwidth coefficients: 0x181..0x18a */

/* ChipCommon register modified by the femctrl set-up (purpose unverified). */
#define CC_REG_0x28		0x28

/* PHY table ids, TBL(id). */
#define TBL_RFSEQ		0x07	/* RF sequence table (analog filters, ...) */
#define TBL_FEMCTRL		0x0a	/* front end control LUT */
#define TBL_04			0x04
#define TBL_10			0x10
#define TBL_RECIP		0x11	/* reciprocity coefficients */
#define TBL_14			0x14
#define TBL_15			0x15
#define TBL_TXGAIN		0x20	/* transmit gain table */

/* The RF sequence table has nine "modes" 0..8 (section 4). */
#define RFSEQ_MODES		9

/* Reciprocity (annex A1): the sine table S[n] = round(512 * sin(n * pi / 128)). */
static const s16 recip_sine[64] = {
	0,  13,  25,  38,  50,  63,  75,  88, 100, 112, 124, 137, 149, 161, 172, 184,
	196, 207, 219, 230, 241, 252, 263, 274, 284, 295, 305, 315, 325, 334, 344, 353,
	362, 371, 379, 388, 396, 404, 411, 419, 426, 433, 439, 445, 452, 457, 463, 468,
	473, 478, 482, 486, 490, 493, 497, 500, 502, 504, 506, 508, 510, 511, 511, 512,
};

/* the number of cores N of the PHY (pi+0x168) */
static u32 phy_cores(const struct bcm4360_phy *phy)
{
	return phy->ver.cores;
}

/* the radio is tuned to a 5 GHz channel */
static bool phy_is_5g(const struct bcm4360_phy *phy)
{
	return (phy->radio_chanspec & BCM4360_CHANSPEC_BAND) != BCM4360_CHANSPEC_BAND_2G;
}

/* bandwidth index: 0 = 20 MHz, 1 = 40 MHz, 2 = 80 MHz (anything else) */
static u32 phy_bw_index(const struct bcm4360_phy *phy)
{
	u16 bw = phy->radio_chanspec & BCM4360_CHANSPEC_BW;

	if (bw == BCM4360_CHANSPEC_BW_20)
		return 0;
	if (bw == BCM4360_CHANSPEC_BW_40)
		return 1;
	return 2;
}

/* docs/re/spec/access.md, "PHY tables": one 16 bit entry */
static u16 tbl_read16(struct bcm4360_phy_io *io, u32 id, u32 offset)
{
	u16 v;

	bcm4360_tbl_read(io, id, 1, offset, 16, &v);
	return v;
}

static void tbl_write16(struct bcm4360_phy_io *io, u32 id, u32 offset, u16 v)
{
	bcm4360_tbl_write(io, id, 1, offset, 16, &v);
}

/* one 48 bit entry, given as the low, middle and high 16 bit word */
static void tbl_write48(struct bcm4360_phy_io *io, u32 id, u32 offset, u64 v)
{
	u16 w[3] = { (u16)(v & 0xffff), (u16)((v >> 16) & 0xffff), (u16)((v >> 32) & 0xffff) };

	bcm4360_tbl_write(io, id, 1, offset, 48, w);
}

/*
 * si_pmu_regcontrol(sih, reg, mask, val): select the PMU regulator control
 * register `reg` in CC(0x658), then modify it in CC(0x65c). In the PHY tests
 * si_corereg is a compared service (as in open/phy/phy_init.c).
 */
static void phy_pmu_regcontrol(struct bcm4360_phy *phy, u32 reg, u32 mask, u32 val)
{
	bcm4360_chip_corereg(phy->hw, 0, CC_PMU_REGCTL_ADDR, 0xffffffff, reg);
	bcm4360_chip_corereg(phy->hw, 0, CC_PMU_REGCTL_DATA, mask, val);
}

/* ------------------------------------------------ 4, 5, 6: analog filters */

/*
 * acphy-rxgain.md, section 4 (wlc_phy_set_analog_tx_lpf): change fields of the
 * analog transmit low-pass filter, one 25 bit word per core and per mode
 * (0..8), split over two 16 bit entries of TBL(0x07). A negative argument
 * leaves the field; core = -1 means all cores.
 */
static void phy_set_analog_tx_lpf(struct bcm4360_phy *phy, u16 mode_mask, s32 f0,
				  s32 f6, s32 f3, s32 f9, s32 f17, s32 core)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 s;
	u32 c;

	s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	for (c = 0; c < phy_cores(phy); c++) {
		u32 m;

		if (core >= 0 && (u32)core != c)
			continue;
		for (m = 0; m < RFSEQ_MODES; m++) {
			u32 lo_off = 0x142 + 0x10 * c + m;
			u32 hi_off = 0x362 + 0x10 * c + m;
			u16 lo, hi;
			u32 w;

			if (!(mode_mask & (1u << m)))
				continue;
			lo = tbl_read16(io, TBL_RFSEQ, lo_off);	/* low first, then high */
			hi = tbl_read16(io, TBL_RFSEQ, hi_off);
			w = ((u32)hi << 16) | lo;
			if (f0 >= 0)
				w = (w & 0x1ffffff & ~0x0000007u) | ((u32)f0 << 0);
			if (f6 >= 0)
				w = (w & 0x1ffffff & ~0x00001c0u) | ((u32)f6 << 6);
			if (f3 >= 0)
				w = (w & 0x1ffffff & ~0x0000038u) | ((u32)f3 << 3);
			if (f9 >= 0)
				w = (w & 0x1ffffff & ~0x001fe00u) | ((u32)f9 << 9);
			if (f17 >= 0)
				w = (w & 0x1ffffff & ~0x1fe0000u) | ((u32)f17 << 17);
			tbl_write16(io, TBL_RFSEQ, lo_off, (u16)(w & 0xffff));
			tbl_write16(io, TBL_RFSEQ, hi_off, (u16)((w >> 16) & 0x1ff));
		}
	}

	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);
}

/*
 * acphy-rxgain.md, section 5 (wlc_phy_set_analog_rx_lpf): the same for the
 * analog receive low-pass filter, per core and per bandwidth (mode 0 = 20, 1 =
 * 40, 2 = 80 MHz). The word of a mode/core lives in different TBL(0x07)
 * entries than the transmit one.
 */
static void phy_set_analog_rx_lpf(struct bcm4360_phy *phy, u16 mode_mask, s32 f0,
				  s32 f3, s32 f14, s32 f6, s32 f17, s32 core)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 s;
	u32 c;

	s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	for (c = 0; c < phy_cores(phy); c++) {
		u32 m;

		if (core >= 0 && (u32)core != c)
			continue;
		for (m = 0; m < 3; m++) {
			u32 lo_off, hi_off, w;
			u16 lo, hi;

			if (!(mode_mask & (1u << m)))
				continue;
			if (m == 0) {
				lo_off = 0x140 + 0x10 * c;
				hi_off = 0x360 + 0x10 * c;
			} else if (m == 1) {
				lo_off = 0x141 + 0x10 * c;
				hi_off = 0x361 + 0x10 * c;
			} else {
				lo_off = 0x441 + 2 * c;
				hi_off = 0x440 + 2 * c;
			}
			lo = tbl_read16(io, TBL_RFSEQ, lo_off);	/* low first, then high */
			hi = tbl_read16(io, TBL_RFSEQ, hi_off);
			w = ((u32)hi << 16) | lo;
			if (f0 >= 0)
				w = (w & 0x1ffffff & ~0x0000007u) | ((u32)f0 << 0);
			if (f3 >= 0)
				w = (w & 0x1ffffff & ~0x0000038u) | ((u32)f3 << 3);
			if (f14 >= 0)
				w = (w & 0x1ffffff & ~0x001c000u) | ((u32)f14 << 14);
			if (f6 >= 0)
				w = (w & 0x1ffffff & ~0x0003fc0u) | ((u32)f6 << 6);
			if (f17 >= 0)
				w = (w & 0x1ffffff & ~0x1fe0000u) | ((u32)f17 << 17);
			tbl_write16(io, TBL_RFSEQ, lo_off, (u16)(w & 0xffff));
			tbl_write16(io, TBL_RFSEQ, hi_off, (u16)((w >> 16) & 0x1ff));
		}
	}

	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);
}

/*
 * acphy-rxgain.md, section 6 (wlc_phy_set_tx_afe_dacbuf_cap): the capacitor
 * code of the buffer behind the DAC, a 6 bit field per core and mode in
 * TBL(0x07).
 */
static void phy_set_tx_afe_dacbuf_cap(struct bcm4360_phy *phy, u16 mode_mask,
				      s32 cap, s32 fixed, s32 core)
{
	static const u16 base[BCM4360_PHY_CORES_MAX] = { 0x3f0, 0x060, 0x0d0 };
	static const u8 offset[RFSEQ_MODES] = {
		0x0b, 0x0b, 0x0c, 0x0c, 0x0e, 0x0e, 0x0f, 0x0f, 0x0a
	};
	static const u8 shift[RFSEQ_MODES] = { 0, 6, 0, 6, 0, 6, 0, 6, 0 };
	struct bcm4360_phy_io *io = &phy->io;
	u16 s;
	u32 c;

	s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	for (c = 0; c < phy_cores(phy); c++) {
		u32 m;

		if (core >= 0 && (u32)core != c)
			continue;
		for (m = 0; m < RFSEQ_MODES; m++) {
			u32 off = base[c] + offset[m];
			u32 sh = shift[m];
			u16 v, f, nv;

			if (!(mode_mask & (1u << m)))
				continue;
			v = tbl_read16(io, TBL_RFSEQ, off);
			f = (v >> sh) & 0x3f;
			if (cap >= 0)
				f = (f & 0x20) | (u16)cap;
			if (fixed >= 0)
				f = (f & 0x1f) | (u16)(fixed << 5);
			if (sh == 0)
				nv = (u16)((f & 0x3f) | (v & 0x0fc0));
			else
				nv = (u16)((v & 0x003f) | ((f & 0x3f) << 6));
			tbl_write16(io, TBL_RFSEQ, off, nv);
		}
	}

	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);
}

/* --------------------------------------------- 2, 3: front end control table */

/*
 * acphy-rxgain.md, section 2, "Note": the 32-byte default block written by
 * sub_0a5fae (all 0 except a few entries).
 */
static const u8 femctrl_default_block[32] = {
	[2] = 4, [6] = 4, [9] = 1, [0x12] = 4, [0x16] = 4, [0x19] = 2,
};

/*
 * acphy-rxgain.md, "Table 0a-femctrl3": the three 32-byte blocks written by
 * sub_09db6d for femctrl = 3, one set per sub-selector cVar. Blocks of cVar 0
 * and 1 have 16 leading zero bytes; cVar 2 and 3 repeat a 16-byte pattern.
 */
static const u8 femctrl3_blocks[4][3][32] = {
	{	/* cVar 0 (the emulated card): .rodata 0x2ccc40 / 0x2ccc20 / 0x2ccc00 */
		{ [16] = 0x02, 0x04, 0x03, 0x0b, 0x02, 0x04, 0x03, 0x0b,
			 0x02, 0x24, 0x03, 0x2d, 0x02, 0x24, 0x03, 0x2d },
		{ [16] = 0x02, 0x01, 0x06, 0x0e, 0x02, 0x01, 0x06, 0x0e,
			 0x02, 0x21, 0x06, 0x2d, 0x02, 0x21, 0x06, 0x2d },
		{ [16] = 0x04, 0x01, 0x06, 0x0e, 0x04, 0x01, 0x06, 0x0e,
			 0x04, 0x21, 0x06, 0x2b, 0x04, 0x21, 0x06, 0x2b },
	},
	{	/* cVar 1: .rodata 0x2ccca0 / 0x2ccc80 / 0x2ccc60 */
		{ [16] = 0x08, 0x04, 0x03, 0x08, 0x08, 0x04, 0x03, 0x08,
			 0x08, 0x24, 0x03, 0x25, 0x08, 0x24, 0x03, 0x25 },
		{ [16] = 0x08, 0x01, 0x06, 0x08, 0x08, 0x01, 0x06, 0x08,
			 0x08, 0x21, 0x06, 0x25, 0x08, 0x21, 0x06, 0x25 },
		{ [16] = 0x08, 0x01, 0x06, 0x08, 0x08, 0x01, 0x06, 0x08,
			 0x08, 0x21, 0x06, 0x23, 0x08, 0x21, 0x06, 0x23 },
	},
	{	/* cVar 2: .rodata 0x2cccc0 to all three offsets */
		{ 0x02, 0x04, 0x03, 0x02, 0x02, 0x04, 0x03, 0x02,
		  0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23, 0x25,
		  0x02, 0x04, 0x03, 0x02, 0x02, 0x04, 0x03, 0x02,
		  0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23, 0x25 },
		{ 0x02, 0x04, 0x03, 0x02, 0x02, 0x04, 0x03, 0x02,
		  0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23, 0x25,
		  0x02, 0x04, 0x03, 0x02, 0x02, 0x04, 0x03, 0x02,
		  0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23, 0x25 },
		{ 0x02, 0x04, 0x03, 0x02, 0x02, 0x04, 0x03, 0x02,
		  0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23, 0x25,
		  0x02, 0x04, 0x03, 0x02, 0x02, 0x04, 0x03, 0x02,
		  0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23, 0x25 },
	},
	{	/* cVar 3: .rodata 0x2ccce0 / 0x2ccd00 / 0x2ccd20 */
		{ 0x04, 0x01, 0x06, 0x04, 0x04, 0x01, 0x06, 0x04,
		  0x24, 0x21, 0x26, 0x23, 0x24, 0x21, 0x26, 0x23,
		  0x04, 0x01, 0x06, 0x04, 0x04, 0x01, 0x06, 0x04,
		  0x24, 0x21, 0x26, 0x23, 0x24, 0x21, 0x26, 0x23 },
		{ 0x02, 0x01, 0x06, 0x02, 0x02, 0x01, 0x06, 0x02,
		  0x22, 0x21, 0x26, 0x25, 0x22, 0x21, 0x26, 0x25,
		  0x02, 0x01, 0x06, 0x02, 0x02, 0x01, 0x06, 0x02,
		  0x22, 0x21, 0x26, 0x25, 0x22, 0x21, 0x26, 0x25 },
		{ 0x02, 0x04, 0x03, 0x02, 0x02, 0x04, 0x03, 0x02,
		  0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23, 0x25,
		  0x02, 0x04, 0x03, 0x02, 0x02, 0x04, 0x03, 0x02,
		  0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23, 0x25 },
	},
};

/* acphy-rxgain.md, "Constant blocks": femctrl 2 (cVar 0, 1) block at offset 0x20 */
static const u8 femctrl2_c0_off20[32] = {
	0x00, 0x00, 0x50, 0x10, 0x00, 0x00, 0x50, 0x10, 0x00, 0x80,
	[16] = 0x00, 0x00, 0x06, 0x02, 0x00, 0x00, 0x06, 0x02, 0x00, 0x01,
};
static const u8 femctrl2_c1_off20[32] = {
	0x00, 0x00, 0x30, 0x20, 0x00, 0x00, 0x30, 0x20, 0x00, 0x80,
	[16] = 0x40, 0x40, 0x46, 0x42, 0x40, 0x40, 0x46, 0x42, 0x40, 0x41,
	[26] = 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
};
/* femctrl 2, cVar 2: two 16-byte blocks (offset 0, then 0x20/0x40) */
static const u8 femctrl2_c2_off00[32] = {
	0x06, 0x06, 0x04, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x04, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
};
static const u8 femctrl2_c2_off20[32] = {
	0x02, 0x02, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	0x02, 0x02, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
};
/* femctrl 5, block at offset 0x20 (.rodata 0x2cd0a0) */
static const u8 femctrl5_off20[32] = {
	0x00, 0x00, 0x50, 0x40, 0x00, 0x00, 0x50, 0x40, 0x00, 0x20,
	[16] = 0x80, 0x80, 0x86, 0x82, 0x80, 0x80, 0x86, 0x82, 0x80, 0x81,
	[26] = 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
};
/* femctrl 6: one block written to all three offsets */
static const u8 femctrl6_block[32] = {
	[0x12] = 6, [0x13] = 2, [0x16] = 6, [0x17] = 2, [0x19] = 1,
};
/* femctrl 5, offset 0x40: all zero */
static const u8 femctrl_zero_block[32] = { 0 };

/* Format B (femctrl 8 and 9): sparse (index, value) records; other indices 0. */
static const u8 femctrl8_idx[] = {
	0xa1, 0xa2, 0xa3, 0xa6, 0xa7, 0xa9, 0xb1, 0xb2, 0xb3, 0xb6, 0xb7, 0xb9
};
static const u16 femctrl8_val[] = {
	9, 0xc, 9, 0xc, 9, 9, 0x41, 9, 0x41, 9, 0x41, 0x41
};
static const u8 femctrl9_idx[] = { 2, 3, 9, 0x12, 0x13, 0x19, 0x82, 0xc0 };
static const u16 femctrl9_val[] = { 0x80, 0, 4, 0x40, 0, 3, 8, 8 };

/* write a 32-byte block (8 bit entries) to TBL(0x0a) at an offset */
static void femctrl_block(struct bcm4360_phy_io *io, u32 offset, const u8 *block)
{
	bcm4360_tbl_write(io, TBL_FEMCTRL, 32, offset, 8, block);
}

/* acphy-rxgain.md, section 3: sub_0a5fae / sub_0a5ffe (the default block) */
static void femctrl_default_entry(struct bcm4360_phy_io *io, u32 offset)
{
	femctrl_block(io, offset, femctrl_default_block);
}

static void femctrl_default_stride(struct bcm4360_phy_io *io)
{
	femctrl_default_entry(io, 0x00);
	femctrl_default_entry(io, 0x20);
	femctrl_default_entry(io, 0x40);
}

/*
 * acphy-rxgain.md, section 2/3, "Format B": walk the index 0..last, write a
 * record's value where the index matches (records in ascending order), 0
 * elsewhere; 16 bit entries, one transfer per index.
 */
static void femctrl_format_b(struct bcm4360_phy_io *io, const u8 *idx, const u16 *val,
			     u32 count, u32 last)
{
	u32 i, rec = 0;

	for (i = 0; i <= last; i++) {
		u16 v = 0;

		if (rec < count && idx[rec] == i) {
			v = val[rec];
			rec++;
		}
		tbl_write16(io, TBL_FEMCTRL, i, v);
	}
}

/*
 * acphy-rxgain.md, section 3 (sub_0a14b6, wlc_phy_femctrl_rfctrl_ovrd_acphy):
 * set up the RF-control override registers, then the GPIOs. Called by femctrl
 * 2 and 5.
 */
static void femctrl_rfctrl_ovrd(struct bcm4360_phy *phy, u32 mode, u32 gpiomask)
{
	struct bcm4360_phy_io *io = &phy->io;

	bcm4360_phy_mod(io, ACPHY_REG_0x40a, 0x0100, 0x0100);
	bcm4360_phy_write(io, ACPHY_REG_0x414, 0x0555);
	bcm4360_chip_corereg(phy->hw, 0, CC_REG_0x28, 0x0008, 0x0008);
	bcm4360_phy_mod(io, ACPHY_REG_0x418, 0x003c, (u16)((mode << 2) & 0x3fc));

	if (phy->ver.phy_rev == ACPHY_REV_0) {
		bcm4360_chip_gpiocontrol(phy->hw, 0xffff, 0xe0, 0);
		/* sub_093ebe(pi, 0xb, 0): acphy-init, a transmit-power leaf (stubbed) */
		phy->acphy_0x8e1 = 1;
		bcm4360_phy_mod(io, ACPHY_REG_0x418, 0x0001, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x418, 0x0002, 0x0002);
	} else {
		phy->acphy_0x8e1 = 0;
		bcm4360_phy_mod(io, ACPHY_REG_0x40a, 0x0200, 0x0200);
	}

	bcm4360_chip_gpioout(phy->hw, gpiomask, 0, 0);
	bcm4360_chip_gpioouten(phy->hw, gpiomask, gpiomask, 0);
	bcm4360_chip_gpiocontrol(phy->hw, gpiomask, 0, 0);
}

/*
 * acphy-rxgain.md, section 3 (sub_0a13ec, wlc_phy_femctrl_write_tbl8_acphy):
 * femctrl 2, cVar 2.
 */
static void femctrl_write_tbl8(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;

	femctrl_block(io, 0x00, femctrl2_c2_off00);
	femctrl_block(io, 0x20, femctrl2_c2_off20);
	femctrl_block(io, 0x40, femctrl2_c2_off20);
	bcm4360_chip_gpioout(phy->hw, 8, 8, 0);
	bcm4360_chip_gpioouten(phy->hw, 8, 8, 0);
}

/*
 * acphy-rxgain.md, section 3 (sub_09db6d, wlc_phy_femctrl_bandsel_map_acphy):
 * the femctrl = 3 path (the emulated card). Three 32-byte blocks by cVar; for
 * cVar 0 and 1, CC(0x28) is first forced to 0x10 (keep bit 1).
 */
static void femctrl_bandsel_map(struct bcm4360_phy *phy, u8 cvar)
{
	struct bcm4360_phy_io *io = &phy->io;

	if (cvar > 3)
		return;
	if (cvar <= 1)
		bcm4360_chip_corereg(phy->hw, 0, CC_REG_0x28, 0xfffffd, 0x10);
	femctrl_block(io, 0x00, femctrl3_blocks[cvar][0]);
	femctrl_block(io, 0x20, femctrl3_blocks[cvar][1]);
	femctrl_block(io, 0x40, femctrl3_blocks[cvar][2]);
}

/*
 * acphy-rxgain.md, section 2 (sub_0a602f, wlc_phy_setup_femctrl_acphy): fill
 * the front end control table TBL(0x0a) by the SROM variable femctrl. The
 * emulated card uses femctrl = 3.
 */
static void phy_setup_femctrl(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u8 cvar = phy->flags.bf3_bits0_2;
	bool rev01 = phy->ver.phy_rev <= ACPHY_REV_1;
	u16 s;

	/* step 1 */
	s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	/* step 2: the switch control maps come from the SROM; nothing on rev 0/1 */
	if (phy->flags.bf3_swctrlmap)
		goto tail;

	/* step 3: by femctrl */
	switch (phy->femctrl) {
	case 0:
		break;
	case 2:
		if (cvar == 0) {
			femctrl_default_entry(io, 0x00);
			femctrl_block(io, 0x20, femctrl2_c0_off20);
			femctrl_default_entry(io, 0x40);
		} else if (cvar == 1) {
			femctrl_default_entry(io, 0x00);
			femctrl_block(io, 0x20, femctrl2_c1_off20);
			femctrl_default_entry(io, 0x40);
		} else if (cvar == 2) {
			femctrl_write_tbl8(phy);
		}
		femctrl_rfctrl_ovrd(phy, cvar == 0 ? 0 : 4, 0xa0);
		if (cvar == 0) {
			bcm4360_chip_gpioout(phy->hw, 0x10, 0, 0);
			bcm4360_chip_gpioouten(phy->hw, 0x10, 0x10, 0);
			bcm4360_chip_gpiocontrol(phy->hw, 0x10, 0, 0);
		}
		break;
	case 3:
		bcm4360_phy_mod(io, ACPHY_REG_0x40a, 0x0100, 0);
		phy_pmu_regcontrol(phy, 0, 0x4, 0x4);
		femctrl_bandsel_map(phy, cvar);
		break;
	case 4:
		/* PHY revision 2/5/6 only: nothing on the 4360 */
		break;
	case 5:
		femctrl_default_entry(io, 0x00);
		femctrl_block(io, 0x20, femctrl5_off20);
		femctrl_block(io, 0x40, femctrl_zero_block);
		femctrl_rfctrl_ovrd(phy, 8, 0xc0);
		break;
	case 6:
		femctrl_block(io, 0x00, femctrl6_block);
		femctrl_block(io, 0x20, femctrl6_block);
		femctrl_block(io, 0x40, femctrl6_block);
		break;
	case 7:
		/* Format B, 28 records (.rodata not transcribed); not on the 4360 */
		break;
	case 8:
		femctrl_format_b(io, femctrl8_idx, femctrl8_val,
				 ARRAY_SIZE(femctrl8_idx), 0xff);
		break;
	case 9:
		if (cvar == 1)
			femctrl_format_b(io, femctrl9_idx, femctrl9_val,
					 ARRAY_SIZE(femctrl9_idx), 0xff);
		break;
	case 10:
		/* Format B over 0..0x13f (.rodata not transcribed); not on the 4360 */
		break;
	case 1:
	default:
		bcm4360_phy_mod(io, ACPHY_REG_0x40a, 0x0100, 0x0100);
		femctrl_default_stride(io);
		break;
	}

tail:
	/* step 4: ChipCommon tail (boardflags bit 0, PHY revision 0/1) */
	if (phy->flags.bf_bit0 && rev01) {
		if (phy->ver.phy_rev == ACPHY_REV_0)
			bcm4360_chip_corereg(phy->hw, 0, CC_REG_0x28, 0x00000004, 0x00000004);
		else
			bcm4360_chip_corereg(phy->hw, 0, CC_REG_0x28, 0x01000000, 0x01000000);
	}

	/* step 5: restore */
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);
}

/* ------------------------------------------------------ 1: sub_0a6b0f */

/* acphy-rxgain.md, section 1, step 6: the static TBL(0x07) blocks, in order */
static const u16 reset_tbl7_0x20[16] = {
	0x04, 0x03, 0x06, 0x05, 0x02, 0x01, 0x08, 0x2a,
	0x2b, 0x0f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
};
static const u16 reset_tbl7_0x90[16] = {
	0x0c, 2, 2, 4, 4, 6, 1, 4, 1, 2, 1, 1, 1, 1, 1, 1,
};
static const u16 reset_tbl7_0x00[16] = {
	0x00, 0x01, 0x02, 0x08, 0x05, 0x00, 0x06, 0x03,
	0x0f, 0x04, 0x00, 0x35, 0x0f, 0x00, 0x36, 0x1f,
};
static const u16 reset_tbl7_aaa[2] = { 0x0aaa, 0x0aaa };
static const u16 reset_tbl7_222[2] = { 0x0222, 0x0222 };
static const u16 reset_tbl7_single[6] = { 0x3c6, 0x3c7, 0x3d6, 0x3d7, 0x3e6, 0x3e7 };

/*
 * acphy-rxgain.md, section 1 (sub_0a6b0f, wlc_phy_set_tbl_on_reset_acphy): the
 * board- and calibration-dependent part of the PHY initialisation. Called by
 * the channel function at INIT (step 15). PHY revision 0 and 1.
 */
void bcm4360_phy_set_regtbl_femctrl_acphy(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u8 gmult = phy->radio.rccal_gmult;
	u8 gmult_rc = phy->radio.rccal_gmult_rc;
	u16 s;
	u32 i;

	/* step 1 */
	s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	/* step 2 */
	phy_setup_femctrl(phy);

	/* step 3: analog transmit LPF, gmult from the RC calibration */
	phy_set_analog_tx_lpf(phy, 0x1ff, -1, -1, -1, gmult, gmult_rc, -1);

	/* step 4: DAC buffer cap */
	phy_set_tx_afe_dacbuf_cap(phy, 0x1ff, phy->radio.rccal_dacbuf, -1, -1);

	/* step 5: analog receive LPF, three bandwidths */
	if (phy->ver.phy_rev <= ACPHY_REV_1) {
		u8 g20 = (u8)((gmult * 0xdd) >> 8);
		u8 g40 = (u8)((gmult * 0xd7) >> 8);

		phy_set_analog_rx_lpf(phy, 1, -1, -1, -1, g20, gmult_rc, -1);
		phy_set_analog_rx_lpf(phy, 2, -1, -1, -1, g40, gmult_rc, -1);
		phy_set_analog_rx_lpf(phy, 4, -1, -1, -1, g40, gmult_rc, -1);
	} else {
		phy_set_analog_rx_lpf(phy, 1, -1, -1, -1, gmult, gmult_rc, -1);
		phy_set_analog_rx_lpf(phy, 2, -1, -1, -1, gmult, gmult_rc, -1);
		phy_set_analog_rx_lpf(phy, 4, -1, -1, -1, gmult, gmult_rc, -1);
	}

	/* step 6: a block of static TBL(0x07) entries */
	bcm4360_tbl_write(io, TBL_RFSEQ, 16, 0x20, 16, reset_tbl7_0x20);
	bcm4360_tbl_write(io, TBL_RFSEQ, 16, 0x90, 16, reset_tbl7_0x90);
	bcm4360_tbl_write(io, TBL_RFSEQ, 2, 0x121, 16, reset_tbl7_aaa);
	bcm4360_tbl_write(io, TBL_RFSEQ, 2, 0x131, 16, reset_tbl7_aaa);
	bcm4360_tbl_write(io, TBL_RFSEQ, 2, 0x124, 16, reset_tbl7_222);
	bcm4360_tbl_write(io, TBL_RFSEQ, 2, 0x137, 16, reset_tbl7_222);
	bcm4360_tbl_write(io, TBL_RFSEQ, 16, 0x00, 16, reset_tbl7_0x00);
	for (i = 0; i < ARRAY_SIZE(reset_tbl7_single); i++)
		tbl_write16(io, TBL_RFSEQ, reset_tbl7_single[i], 0x0020);

	/* step 7: one entry, only if the board has the flag for the band */
	{
		u32 flag = phy_is_5g(phy) ? 0x00200000 : 0x00100000;

		if (phy->board.boardflags2 & flag)
			tbl_write16(io, TBL_RFSEQ, 0x80, 0x0078);
	}

	/* step 8: PHY revision 0 and 1: the acphy_txv_for_spexp array */
	if (phy->ver.phy_rev <= ACPHY_REV_1) {
		u32 size = 0;
		const void *data = hw_fw_data(phy->hw, "acphy_txv_for_spexp", &size);

		if (data)
			bcm4360_tbl_write(io, TBL_10, 243, 0x4c4, 32, data);
	}

	/* step 10: restore */
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);
}

/* ------------------------------------------- 7: sub_08f2e9 (BT-gated gain) */

/*
 * acphy-rxgain.md, section 7 (sub_08f2e9, wlc_phy_rxgain_bt_wlan_ovrd_acphy):
 * lower two 4-bit receive-gain fields while Bluetooth transmits. PHY revision
 * 0 and 1; other revisions do nothing here.
 */
static void phy_rxgain_bt_wlan_ovrd(struct bcm4360_phy *phy, bool bt)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 n1 = bt ? 2 : 4;
	u16 n0 = bt ? 0 : 4;

	if (phy->ver.phy_rev > ACPHY_REV_1)
		return;

	bcm4360_phy_mod(io, ACPHY_REG_0x2d1, 0x00f0, n1 << 4);
	bcm4360_phy_mod(io, ACPHY_REG_0x2d1, 0x0f00, n0 << 8);
	bcm4360_phy_mod(io, ACPHY_REG_0x2d2, 0x00f0, n1 << 4);
	bcm4360_phy_mod(io, ACPHY_REG_0x2d2, 0x0f00, n1 << 8);	/* n1, not n0 */
}

/* --------------------------------------------------- A3: sub_09e378 (band) */

/*
 * acphy-chanspec.md, annex A3, step 3: the transmit gain table of the band,
 * chosen by the kind of power amplifier and the radio revision.
 */
static const char *phy_txgain_table(struct bcm4360_phy *phy, bool is5g)
{
	if (!is5g) {
		if (phy->txpwr.extpagain2g == 2)
			return "acphy_txgain_ipa_2g_2069rev0";
		if (phy->radio.rev == R2069_REV_3)
			return "acphy_txgain_epa_2g_2069rev0";
		if (phy->flags.bf3_bits4_6 == 1)
			return "acphy_txgain_epa_2g_2069rev4_id1";
		return "acphy_txgain_epa_2g_2069rev4";
	}
	if (phy->txpwr.extpagain5g == 2)
		return "acphy_txgain_ipa_5g_2069rev0";
	if (phy->radio.rev == R2069_REV_3)
		return "acphy_txgain_epa_5g_2069rev0";
	return "acphy_txgain_epa_5g_2069rev4";
}

/*
 * acphy-chanspec.md, annex A3 (sub_09e378,
 * wlc_phy_set_regtbl_on_band_change_acphy), PHY part: band dependent registers
 * and tables, then the finished radio functions. Called on a band change.
 */
void bcm4360_phy_set_regtbl_on_band_change_acphy(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	bool is5g = phy_is_5g(phy);
	const char *name;
	const void *data;
	u32 size = 0;
	u32 m;
	u16 s;

	/* step 1 */
	s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	/* step 2 */
	if (!is5g) {
		bcm4360_phy_write(io, ACPHY_REG_0x1ec, 0x0002);
		bcm4360_phy_mod(io, ACPHY_REG_0x2e4, 0x3f00, 0x0f00);
	} else {
		bcm4360_phy_write(io, ACPHY_REG_0x1ec, 0x9c40);
		bcm4360_phy_mod(io, ACPHY_REG_0x2e4, 0x3f00, 0x0800);
	}

	/* step 3: the transmit gain table */
	name = phy_txgain_table(phy, is5g);
	data = hw_fw_data(phy->hw, name, &size);
	if (data)
		bcm4360_tbl_write(io, TBL_TXGAIN, 128, 0, 48, data);

	/* step 4: radio registers (only PHY revision 0; self-gated in the radio) */
	bcm4360_radio_band_change(&phy->radio, is5g);

	/* step 5: only if pdgain5g is 9 or 16 */
	if (phy->txpwr.pdgain5g == 9 || phy->txpwr.pdgain5g == 16)
		tbl_write16(io, TBL_RFSEQ, 0x18e, is5g ? 0x0049 : 0);

	/* step 6: sub_08f9b4 (transmit power leaf, acphy-txpower.md section 22) */
	bcm4360_phy_tssi_phy_setup_acphy(phy, 0);

	/* step 7: the radio set-up of the power detector path */
	bcm4360_radio_tssi_setup(&phy->radio, phy->hw_rxchain, 0, is5g);

	/* step 8: interference mitigation (desense leaves) */
	m = phy->interference.mode;
	bcm4360_phy_hwaci_setup_acphy(phy, (m & BCM4360_INTERFERENCE_HWACI) != 0, false);
	bcm4360_phy_aci_w2nb_setup_acphy(phy, (m & BCM4360_INTERFERENCE_W2NB) != 0);

	/* step 9: sub_09c161 (transmit calibration coefficients, acphy-txpower.md section 20;
	 * a band change resets them to 0) */
	bcm4360_phy_txcal_coeffs_apply_acphy(phy, NULL);

	/* step 10: the Bluetooth-gated receive gain */
	phy_rxgain_bt_wlan_ovrd(phy, phy->bt_active != 0);

	/* step 11: restore */
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);
}

/* --------------------------------------------- A2: sub_09eaf9 (bandwidth) */

/* annex A2, step 2: one modification, with a value per bandwidth */
struct bw_mod {
	u16 reg;
	u16 mask;
	u16 val[3];	/* 20, 40, 80 MHz */
	bool only_5g;
};

static const struct bw_mod bw_mods[] = {
	{ 0x076, 0x0007, { 1, 2, 3 }, false },
	{ 0x140, 0x0800, { 0x0800, 0, 0 }, false },
	{ 0x164, 0x0010, { 0x0010, 0, 0 }, false },
	{ 0x180, 0x001f, { 0x15, 0x0b, 0x05 }, false },
	{ 0x181, 0x07ff, { 0x146, 0x181, 0x17a }, false },
	{ 0x182, 0x07ff, { 0x088, 0x05a, 0x09e }, false },
	{ 0x183, 0x07ff, { 0x146, 0x181, 0x17a }, false },
	{ 0x184, 0x07ff, { 0x76e, 0x793, 0x7ca }, false },
	{ 0x185, 0x07ff, { 0x1a8, 0x1b7, 0x1b2 }, false },
	{ 0x186, 0x07ff, { 0x0a3, 0x0c1, 0x0bd }, false },
	{ 0x187, 0x07ff, { 0x0f4, 0x102, 0x114 }, false },
	{ 0x188, 0x07ff, { 0x0a3, 0x0c1, 0x0bd }, false },
	{ 0x189, 0x07ff, { 0x684, 0x6c0, 0x6d6 }, false },
	{ 0x18a, 0x07ff, { 0x0ad, 0x0a9, 0x0a2 }, false },
	{ 0x18b, 0x07ff, { 0x0e5, 0x162, 0x16c }, false },
	{ 0x18c, 0x07ff, { 0x068, 0x042, 0x06f }, false },
	{ 0x18d, 0x07ff, { 0x0e5, 0x162, 0x16c }, false },
	{ 0x18e, 0x07ff, { 0x6be, 0x75c, 0x793 }, false },
	{ 0x18f, 0x07ff, { 0x19e, 0x1b3, 0x1b2 }, false },
	{ 0x190, 0x07ff, { 0x073, 0x0b1, 0x0b6 }, false },
	{ 0x191, 0x07ff, { 0x0b2, 0x0ed, 0x0ff }, false },
	{ 0x192, 0x07ff, { 0x073, 0x0b1, 0x0b6 }, false },
	{ 0x193, 0x07ff, { 0x5fe, 0x692, 0x6b4 }, false },
	{ 0x194, 0x07ff, { 0x0cc, 0x0af, 0x0a8 }, false },
	{ 0x1b5, 0x00ff, { 0x97, 0x8b, 0x97 }, false },
	{ 0x250, 0x00ff, { 0x19, 0x32, 0x32 }, true },
	{ 0x261, 0x0fff, { 0x014, 0x028, 0x028 }, true },
	{ 0x262, 0x0fff, { 0x0c8, 0x190, 0x190 }, true },
	{ 0x263, 0x0fff, { 0x019, 0x032, 0x032 }, true },
	{ 0x312, 0x00ff, { 0x13, 0x13, 0x09 }, false },
	{ 0x313, 0xff00, { 0x1300, 0x1300, 0x0900 }, false },
};

/* annex A2, step 10: six TBL(0x07) transfers of eight entries (20/40, then 80 MHz) */
static const u16 bw_tbl7_2040[6][8] = {
	{ 0x2a, 0x07, 0x0a, 0x00, 0x08, 0x2b, 0x1f, 0x1f },	/* 0x30 */
	{ 1, 2, 2, 2, 0x10, 1, 1, 1 },				/* 0xa0 */
	{ 0x2a, 0x07, 0x08, 0x0c, 0x0e, 0x2b, 0x1f, 0x1f },	/* 0x40 */
	{ 1, 6, 0x12, 8, 0x10, 1, 1, 1 },			/* 0xb0 */
	{ 0x2a, 0x07, 0x08, 0x0e, 0x2b, 0x1f, 0x1f, 0x1f },	/* 0x50 */
	{ 1, 6, 0x1e, 0x1c, 1, 1, 1, 1 },			/* 0xc0 */
};
static const u16 bw_tbl7_80[6][8] = {
	{ 0x07, 0x0a, 0x00, 0x08, 0xb0, 0xb1, 0x1f, 0x1f },	/* 0x30 */
	{ 2, 2, 2, 1, 0x0a, 1, 1, 1 },				/* 0xa0 */
	{ 0x07, 0x08, 0x0c, 0x0e, 0xb0, 0xb2, 0x1f, 0x1f },	/* 0x40 */
	{ 6, 0x12, 8, 1, 0x0a, 1, 1, 1 },			/* 0xb0 */
	{ 0x07, 0x08, 0x0e, 0xb0, 0xb1, 0x1f, 0x1f, 0x1f },	/* 0x50 */
	{ 6, 0x1e, 0x1c, 0x0a, 1, 1, 1, 1 },			/* 0xc0 */
};
static const u16 bw_tbl7_off[6] = { 0x30, 0xa0, 0x40, 0xb0, 0x50, 0xc0 };

/* annex A2, step 12: two TBL(0x07) transfers of 16 entries (all bandwidths) */
static const u16 bw_tbl7_0x10[16] = {
	0xb3, 4, 3, 6, 5, 0, 2, 1, 8, 0x2a, 0x0f, 0, 0x0f, 0x2b, 0x1f, 0x1f,
};
static const u16 bw_tbl7_0x80[16] = {
	1, 8, 4, 2, 2, 1, 3, 4, 6, 4, 0x0a, 4, 2, 1, 1, 1,
};

/*
 * acphy-chanspec.md, annex A2 (sub_09eaf9,
 * wlc_phy_set_regtbl_on_bw_change_acphy): bandwidth dependent registers and
 * tables. Called on a bandwidth change. PHY revision 0 and 1.
 */
void bcm4360_phy_set_regtbl_on_bw_change_acphy(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 b = phy_bw_index(phy);
	bool is5g = phy_is_5g(phy);
	bool rev0 = phy->ver.phy_rev == ACPHY_REV_0;
	u32 i, c;
	u16 s;

	/* step 1 */
	s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	/* step 2 */
	for (i = 0; i < ARRAY_SIZE(bw_mods); i++) {
		if (bw_mods[i].only_5g && !is5g)
			continue;
		bcm4360_phy_mod(io, bw_mods[i].reg, bw_mods[i].mask, bw_mods[i].val[b]);
	}

	/* step 3 */
	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = ACPHY_CORE_STEP * c;
		u16 v3b = (b == 0) ? 0x17 : (b == 1) ? 0x2a : 0x54;
		u16 v3c = (b == 0) ? 0x0e00 : (b == 1) ? 0x1600 : 0x2c00;

		bcm4360_phy_mod(io, ACPHY_REG_0x6ed + o, 0x00ff, (b == 0) ? 0x0a : 0x14);
		bcm4360_phy_mod(io, ACPHY_REG_0x6ef + o, 0x00ff, v3b);
		bcm4360_phy_mod(io, ACPHY_REG_0x6ef + o, 0xff00, v3c);
	}

	/* step 4 */
	for (c = 0; c < phy_cores(phy); c++) {
		u16 v4 = (b == 0) ? 0x0f : (b == 1) ? 0x1e : 0x3c;

		bcm4360_phy_mod(io, ACPHY_REG_0x6ef + ACPHY_CORE_STEP * c, 0x00ff, v4);
	}

	/* step 5: analog transmit LPF, mode 8 */
	{
		s32 n = (b == 0) ? 3 : (b == 1) ? 4 : 5;

		phy_set_analog_tx_lpf(phy, 0x100, -1, n, n, -1, -1, -1);
	}

	/* step 6: PHY revision 0 only */
	if (rev0)
		bcm4360_phy_write(io, ACPHY_REG_0x16d4, (b == 2) ? 0x0cc0 : 0x0c60);

	/* step 7: TBL(0x04), two transfers of three 8 bit entries */
	{
		static const u8 lo20[3] = { 8, 6, 4 };
		static const u8 hi20[3] = { 4, 6, 8 };
		static const u8 zero3[3] = { 0, 0, 0 };
		const u8 *lo = (b == 0) ? lo20 : zero3;
		const u8 *hi = (b == 0) ? hi20 : zero3;

		bcm4360_tbl_write(io, TBL_04, 3, 1, 8, lo);
		bcm4360_tbl_write(io, TBL_04, 3, 0x3d, 8, hi);
	}

	/* step 8 */
	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = ACPHY_CORE_STEP * c;
		u16 v = 0x0080;

		if (rev0 && (b == 0 || b == 1))
			v = 0;
		bcm4360_phy_mod(io, ACPHY_REG_0x73a + o, 0x0080, v);
		bcm4360_phy_mod(io, ACPHY_REG_0x725 + o, 0x0200, 0x0200);
	}

	/* step 9: 80 MHz only, three TBL(0x14) 48 bit entries */
	if (b == 2) {
		tbl_write48(io, TBL_14, 0x30, 0x000000960fd2ull);
		tbl_write48(io, TBL_14, 0x31, 0x000000860fc2ull);
		tbl_write48(io, TBL_14, 0x32, 0x000000860fd2ull);
	}

	/* step 10: six TBL(0x07) transfers of eight entries */
	for (i = 0; i < 6; i++)
		bcm4360_tbl_write(io, TBL_RFSEQ, 8, bw_tbl7_off[i], 16,
				  (b == 2) ? bw_tbl7_80[i] : bw_tbl7_2040[i]);

	/*
	 * step 11: one TBL(0x14) 48 bit entry. The 20 and 40 MHz values match
	 * annex A2. For 80 MHz the low 32 bits are 0x00860800 as in the spec, but
	 * the top word (bits 32..47) is not the 0 the spec gives: on a channel
	 * change it is 0x8080 in the outer 5 GHz sub-bands (get_chan_freq_range 1
	 * and 4) and 0 in the inner ones, and it is 0 at initialisation. This
	 * channel/context dependence (annex A2 says the function has none) is
	 * observed in the traces; see the questions.
	 */
	if (b == 0) {
		tbl_write48(io, TBL_14, 0x33, 0x00000084e800ull);
	} else if (b == 1) {
		tbl_write48(io, TBL_14, 0x33, 0x000000844800ull);
	} else {
		u8 r = bcm4360_phy_get_chan_freq_range(phy, 0);
		u64 top = (!phy->init_chan && (r == 1 || r == 4)) ? 0x8080ull : 0ull;

		tbl_write48(io, TBL_14, 0x33, (top << 32) | 0x00860800ull);
	}

	/* step 12: two TBL(0x07) transfers of 16 entries */
	bcm4360_tbl_write(io, TBL_RFSEQ, 16, 0x10, 16, bw_tbl7_0x10);
	bcm4360_tbl_write(io, TBL_RFSEQ, 16, 0x80, 16, bw_tbl7_0x80);

	/* step 13 */
	bcm4360_phy_write(io, ACPHY_REG_0x197, (b == 0) ? 0x14 : 0x1e);
	bcm4360_phy_write(io, ACPHY_REG_0x198, (b == 0) ? 0x10 : 0x14);

	/* step 14: restore */
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);
}

/* ------------------------------------------- A1: sub_0a4adc (channel) */

/*
 * acphy-chanspec.md, annex A1 (sub_0a4867, wlc_phy_set_tx_cck_dig_filt_acphy):
 * ten plain writes PHY(0x0ec)..PHY(0x0f5) by the filter type k (0 or 1).
 */
static void phy_set_tx_cck_dig_filt(struct bcm4360_phy *phy, u32 k)
{
	static const u16 filt[2][10] = {
		{ 0x0a94, 0x0373, 0x0005, 0x0a93, 0x0298, 0x0004, 0x0a52, 0x021d, 0x0004, 0x0080 },
		{ 0x0b54, 0x0290, 0x0004, 0x0a40, 0x0290, 0x0005, 0x0a06, 0x0240, 0x0005, 0x0080 },
	};
	struct bcm4360_phy_io *io = &phy->io;
	u32 i;

	if (k > 1)
		return;
	for (i = 0; i < 10; i++)
		bcm4360_phy_write(io, ACPHY_REG_0x0ec + i, filt[k][i]);
}

/* annex A1, "Table A1": the power-amplifier rows at .rodata+0x2cd480 (30 bytes each) */
static const u8 pa_table[20][30] = {
	{ 0x02,0x01,0x02,0x6b,0x96,0x6e, 0x02,0x02,0x01,0x9d,0x99,0xa0, 0x02,0x02,0x01,0x9d,0x99,0xa1, 0x02,0x02,0x00,0x9d,0x99,0xba, 0x02,0x02,0x00,0x9d,0x99,0xbb },
	{ 0x01,0x00,0x01,0x9f,0xae,0xa1, 0x01,0x00,0x01,0xa0,0xb9,0x9c, 0x01,0x00,0x01,0xa3,0xb9,0xa2, 0x01,0x00,0x01,0xa9,0xbb,0xa7, 0x01,0x00,0x01,0x98,0xbc,0xa0 },
	{ 0x01,0x01,0x01,0x9f,0xa6,0xa6, 0x02,0x02,0x04,0x8c,0x97,0x64, 0x02,0x02,0x03,0x8f,0x99,0x74, 0x02,0x02,0x02,0x8f,0x99,0x8c, 0x02,0x02,0x02,0x91,0xa0,0x9a },
	{ 0x01,0x01,0x02,0x82,0x83,0x6a, 0x01,0x01,0x02,0x82,0x83,0x6a, 0x01,0x01,0x02,0x80,0x7f,0x61, 0x00,0x01,0x03,0x9f,0x89,0x4b, 0x00,0x00,0x03,0xa4,0xa2,0x4c },
	{ 0x01,0x01,0x01,0x9c,0xa0,0x9e, 0x01,0x01,0x01,0x9c,0xa0,0x9e, 0x01,0x01,0x01,0x9c,0xa0,0x9e, 0x01,0x01,0x01,0x9c,0xa0,0x9e, 0x01,0x01,0x01,0x9c,0xa0,0x9e },
	{ 0x02,0x02,0x02,0x68,0x6c,0x6a, 0x02,0x02,0x02,0x68,0x6c,0x6a, 0x02,0x02,0x02,0x68,0x6c,0x6a, 0x02,0x02,0x02,0x68,0x6c,0x6a, 0x02,0x02,0x02,0x68,0x6c,0x6a },
	{ 0x02,0x00,0x02,0x66,0xaa,0x68, 0x03,0x04,0x03,0x52,0x66,0x52, 0x01,0x03,0x01,0x86,0x7a,0x88, 0x01,0x03,0x01,0x86,0x7c,0x88, 0x02,0x03,0x02,0x68,0x7a,0x6c },
	{ 0x00,0x00,0x00,0xb4,0xb4,0xb4, 0x00,0x00,0x00,0xb4,0xb4,0xb4, 0x00,0x00,0x00,0xb4,0xb4,0xb4, 0x00,0x00,0x00,0xb4,0xb4,0xb4, 0x00,0x00,0x00,0xb4,0xb4,0xb4 },
	{ 0x02,0x01,0x02,0x66,0x8a,0x68, 0x03,0x05,0x03,0x52,0x64,0x52, 0x01,0x04,0x01,0x86,0x74,0x88, 0x01,0x03,0x01,0x86,0x88,0x88, 0x02,0x03,0x02,0x68,0x88,0x6c },
	{ 0x03,0x02,0x03,0x5a,0x6a,0x56, 0x03,0x01,0x03,0x5a,0x9e,0x5a, 0x02,0x01,0x02,0x72,0x9e,0x70, 0x02,0x01,0x01,0x74,0x9e,0x8e, 0x02,0x01,0x01,0x74,0x9e,0x8e },
	{ 0x02,0x02,0x02,0x98,0x9c,0x9c, 0x02,0x02,0x02,0x98,0x9c,0x9c, 0x02,0x02,0x02,0x98,0x9c,0x9c, 0x02,0x02,0x02,0x98,0x9c,0x9c, 0x02,0x02,0x02,0x98,0x9c,0x9c },
	{ 0x01,0x01,0x01,0x86,0x86,0x86, 0x01,0x01,0x01,0x88,0x88,0x88, 0x01,0x01,0x01,0x88,0x88,0x88, 0x01,0x01,0x01,0x88,0x88,0x88, 0x01,0x01,0x01,0x88,0x88,0x88 },
	{ 0x03,0x03,0x03,0x5a,0x5c,0x56, 0x03,0x03,0x03,0x5a,0x56,0x5a, 0x02,0x03,0x02,0x72,0x56,0x70, 0x02,0x02,0x01,0x74,0x6d,0x8e, 0x02,0x02,0x01,0x74,0x6e,0x8e },
	{ 0x02,0x02,0x02,0x70,0x72,0x70, 0x02,0x02,0x02,0x72,0x72,0x72, 0x02,0x02,0x02,0x72,0x72,0x72, 0x02,0x02,0x02,0x71,0x72,0x70, 0x02,0x02,0x02,0x71,0x72,0x70 },
	{ 0x01,0x01,0x01,0x86,0x86,0x86, 0x00,0x00,0x00,0xa8,0xa8,0xa8, 0x00,0x00,0x00,0xa8,0xa8,0xa8, 0x00,0x00,0x00,0xa8,0xa8,0xa8, 0x00,0x00,0x00,0xa8,0xa8,0xa8 },
	{ 0x00,0x00,0x00,0xac,0xac,0xac, 0x00,0x00,0x00,0xa8,0xa8,0xa8, 0x00,0x00,0x00,0xa8,0xa8,0xa8, 0x00,0x00,0x00,0xa8,0xa8,0xa8, 0x00,0x00,0x00,0xa8,0xa8,0xa8 },
	{ 0x03,0x02,0x03,0x5a,0x6a,0x56, 0x03,0x00,0x03,0x5a,0xba,0x5a, 0x02,0x00,0x02,0x72,0xba,0x70, 0x02,0x00,0x01,0x74,0xba,0x8e, 0x02,0x00,0x01,0x74,0xba,0x8e },
	{ 0x04,0x04,0x04,0x32,0x2d,0x32, 0x03,0x03,0x03,0x52,0x52,0x52, 0x03,0x03,0x03,0x52,0x52,0x52, 0x03,0x03,0x03,0x52,0x52,0x52, 0x03,0x03,0x03,0x52,0x52,0x52 },
	{ 0x05,0x05,0x05,0x3d,0x3d,0x3d, 0x02,0x02,0x02,0x7a,0x7a,0x7a, 0x02,0x02,0x02,0x7a,0x7a,0x7a, 0x02,0x02,0x02,0x7a,0x7a,0x7a, 0x02,0x02,0x02,0x7a,0x7a,0x7a },
	{ 0 },	/* p = 19: all zero */
};

/* annex A1, step 11: the reciprocity coefficients TBL(0x11), 48 bit entries */
static const u32 recip_head[12] = {
	0x5b, 0x8250, 0xc338, 0x14527, 0x1a6a1, 0x2081b,
	0x28a18, 0x32c96, 0x38e17, 0x4101b, 0x20, 0x20,
};

/* annex A1: one 24 bit reciprocity word from a byte of the rpcal value */
static u32 recip_word(u8 b)
{
	u32 k = b & 0x3f;
	u32 quad = (b >> 6) & 3;
	s32 sn = recip_sine[k];
	s32 cs = recip_sine[63 - k];
	s32 x, y;

	switch (quad) {
	case 0:  x = cs;  y = -sn; break;
	case 1:  x = -sn; y = -cs; break;
	case 2:  x = -cs; y = sn;  break;
	default: x = sn;  y = cs;  break;
	}
	if (x < 0)
		x += 0x800;
	if (y < 0)
		y += 0x800;
	return (u32)x | ((u32)y << 11) | 0x400000u;
}

/*
 * acphy-chanspec.md, annex A1 (wlc_phy_populate_recipcoeffs_acphy): the
 * reciprocity (Tx/Rx phase) coefficients written to TBL(0x11). PHY revision 0
 * and 1; nothing if the hardware has fewer than two transmit chains.
 */
static void phy_populate_recipcoeffs(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u8 r = bcm4360_phy_get_chan_freq_range(phy, 0);
	u16 q;
	u64 e;
	u16 s;
	u32 i;

	if (phy->hw_txchain == 0 || phy->hw_txchain == 1)
		return;

	if (r >= 1 && r <= 4)
		q = phy->rpcal5gb[r - 1];
	else
		q = phy->rpcal2g;

	e = (u64)recip_word((u8)(q & 0xff)) | ((u64)recip_word((u8)((q >> 8) & 0xff)) << 24);

	s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	for (i = 0; i < 12; i++)
		tbl_write48(io, TBL_RECIP, i, recip_head[i]);
	for (i = 12; i < 460; i++)
		tbl_write48(io, TBL_RECIP, i, e);
	for (i = 460; i < 464; i++)
		tbl_write48(io, TBL_RECIP, i, 0);

	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);
}

/* annex A1, step 2: the resampler/farrow tables (searched by channel) */
#define FARROW_BLOCK_ENTRIES	123
#define FARROW_ENTRY_WORDS	6

/* annex A1, step 2: find the entry index of channel `ch` in block `b`, -1 if none */
static s32 farrow_index(const u16 *tbl, u32 words, u32 b, u8 ch)
{
	u32 i;

	if (!tbl)
		return -1;
	if ((b + 1) * FARROW_BLOCK_ENTRIES * FARROW_ENTRY_WORDS > words)
		return -1;
	for (i = 0; i < FARROW_BLOCK_ENTRIES; i++) {
		const u16 *e = tbl + (b * FARROW_BLOCK_ENTRIES + i) * FARROW_ENTRY_WORDS;

		if (e[0] == ch)
			return (s32)(b * FARROW_BLOCK_ENTRIES + i);
	}
	return -1;
}

/*
 * acphy-chanspec.md, annex A1 (sub_0a4adc,
 * wlc_phy_set_regtbl_on_chan_change_acphy): channel dependent set-up -
 * resampler/farrow tables, the power-amplifier table, control-channel
 * position, the 11b filter and the reciprocity coefficients. Runs on every
 * channel change. PHY revision 0 and 1. `entry` is the channel table entry e0.
 */
void bcm4360_phy_set_regtbl_on_chan_change_acphy(struct bcm4360_phy *phy,
						 const u16 *entry)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 chanspec = phy->radio_chanspec;
	u8 ch = chanspec & BCM4360_CHANSPEC_CHANNEL;
	u32 b = phy_bw_index(phy);
	bool is5g = phy_is_5g(phy);
	u32 rx_size = 0, tx_size = 0;
	const u16 *rx_farrow = hw_fw_data(phy->hw, "rx_farrow_tbl", &rx_size);
	const u16 *tx_farrow = hw_fw_data(phy->hw, "tx_farrow_dac1_tbl", &tx_size);
	s32 fidx;
	u32 c;
	u16 s;

	/* step 1: clear a set of bits */
	bcm4360_phy_mod(io, ACPHY_REG_0x410, 0x0008, 0);
	bcm4360_phy_mod(io, ACPHY_REG_0x410, 0x0380, 0);
	phy->acphy_116a = 0;
	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = ACPHY_CORE_STEP * c;

		bcm4360_phy_mod(io, ACPHY_REG_0x73a + o, 0x0008, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x725 + o, 0x0040, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x73a + o, 0x0010, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x725 + o, 0x0080, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x73a + o, 0x0007, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x725 + o, 0x0020, 0);
	}

	/* step 2: resampler, only if the channel is in the farrow table */
	fidx = farrow_index(rx_farrow, rx_size / (u32)sizeof(u16), b, ch);
	if (fidx >= 0) {
		const u16 *rxe = rx_farrow + (u32)fidx * FARROW_ENTRY_WORDS;

		bcm4360_phy_write(io, ACPHY_REG_0x19a, rxe[2]);
		bcm4360_phy_write(io, ACPHY_REG_0x19b, rxe[3]);
		bcm4360_phy_write(io, ACPHY_REG_0x19c, rxe[4]);
		bcm4360_phy_write(io, ACPHY_REG_0x199, rxe[5]);
		bcm4360_phy_write(io, ACPHY_REG_0x1a1, rxe[2]);
		bcm4360_phy_write(io, ACPHY_REG_0x1a2, rxe[3]);
		bcm4360_phy_write(io, ACPHY_REG_0x1a3, rxe[4]);
		bcm4360_phy_write(io, ACPHY_REG_0x1a0, rxe[5]);

		if (tx_farrow) {
			const u16 *txe = tx_farrow + (u32)fidx * FARROW_ENTRY_WORDS;

			bcm4360_phy_write(io, ACPHY_REG_0x1603, txe[2]);
			bcm4360_phy_write(io, ACPHY_REG_0x1602, txe[3]);
			bcm4360_phy_write(io, ACPHY_REG_0x1607, txe[4]);
			bcm4360_phy_write(io, ACPHY_REG_0x1606, txe[5]);
		}

		/* step 3: 40 MHz resampler override (iovar only, acphy_1169 = 0) */

		/* step 4 */
		bcm4360_phy_write(io, ACPHY_REG_0x1601,
				  bcm4360_phy_read(io, ACPHY_REG_0x601));
	}

	/* step 5: the power-amplifier table per core */
	{
		u8 p = is5g ? phy->txpwr.pdgain5g : phy->txpwr.pdgain2g;
		u8 r = bcm4360_phy_get_chan_freq_range(phy, 0);
		const u8 *row;

		if (r > 4)
			r = 0;
		if (p >= ARRAY_SIZE(pa_table))
			p = 0;
		row = &pa_table[p][6 * r];

		s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
		bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);
		for (c = 0; c < phy_cores(phy); c++) {
			if (!(phy->hw_rxchain & (1u << c)))
				continue;
			(void)tbl_read16(io, TBL_RFSEQ, 0x3cd + 0x10 * c);
			tbl_write16(io, TBL_RFSEQ, 0x3cd + 0x10 * c,
				    (u16)((row[c] & 7) | (row[3 + c] << 3)));
		}
		bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);
	}

	/* step 6: the six words from the channel table entry */
	for (c = 0; c < R2069_CHAN_PHY_WORDS; c++)
		bcm4360_phy_write(io, ACPHY_REG_0x371 + c, entry[R2069_CHAN_PHY + c]);

	/* step 7: position of the control channel */
	{
		u16 n = (chanspec & BCM4360_CHANSPEC_CTL_SB) >> 8;

		if (b == 2) {
			if (n <= 3)
				bcm4360_phy_mod(io, ACPHY_REG_0x30f, 0xc000, n << 14);
		} else if (b == 1) {
			if (n == 1) {
				bcm4360_phy_mod(io, ACPHY_REG_0x164, 0x0010, 0x0010);
				bcm4360_phy_mod(io, ACPHY_REG_0x30f, 0xc000, 0x4000);
			} else {
				bcm4360_phy_mod(io, ACPHY_REG_0x164, 0x0010, 0);
				bcm4360_phy_mod(io, ACPHY_REG_0x30f, 0xc000, 0);
			}
		} else {
			bcm4360_phy_mod(io, ACPHY_REG_0x164, 0x0010, 0);
			bcm4360_phy_mod(io, ACPHY_REG_0x30f, 0xc000, 0);
		}
	}

	/* step 8 */
	{
		u16 x = !is5g ? 0x00ff : (b == 2) ? 0x0100 : 0x00bf;

		bcm4360_phy_write(io, ACPHY_REG_0x31c, x);
		bcm4360_phy_write(io, ACPHY_REG_0x31d, x);
		bcm4360_phy_write(io, ACPHY_REG_0x31e, x);
		bcm4360_phy_write(io, ACPHY_REG_0x31f, x);
	}

	/*
	 * step 9: clear the carrier-sense calibration state, then re-apply the
	 * carrier-sense thresholds from the stored references
	 * (wlc_phy_crs_min_pwr_cal_acphy with restore = 1; acphy-desense.md C1,
	 * now implemented in open/phy/phy_desense.c). This is the one call the
	 * desense task wires into the channel-change set-up; the accesses it makes
	 * (sub_08f41b on PHY(0x321..0x336) low bytes, PHY(0x910..0x913)) are part
	 * of the compared channel-change trace.
	 */
	hw_memset(phy->crsmincal.sample, 0, sizeof(phy->crsmincal.sample));
	hw_memset(phy->crsmincal.average, 0, sizeof(phy->crsmincal.average));
	phy->crsmincal.next_sample = 0;
	phy->crsmincal.runs = 0;
	bcm4360_phy_crs_min_pwr_cal_acphy(phy, 1);

	/* step 10: the 11b transmit filter */
	{
		u16 fc = entry[R2069_CHAN_FREQ];
		u8 t = phy->cckdigfilttype;
		u16 v = bcm4360_phy_read(io, ACPHY_REG_0x3a9);

		if (fc == 2484) {
			bcm4360_phy_mod(io, ACPHY_REG_0x3a9, 0x007f, v & 0x3f);
			bcm4360_phy_mod(io, ACPHY_REG_0x3a9, 0x0800, 0x0800);
			phy_set_tx_cck_dig_filt(phy, 0);
		} else {
			bcm4360_phy_mod(io, ACPHY_REG_0x3a9, 0x007f,
					(u16)((v & 0x3f) | ((t & 2) << 5)));
			bcm4360_phy_mod(io, ACPHY_REG_0x3a9, 0x0800, (u16)((t & 4) << 9));
			phy_set_tx_cck_dig_filt(phy, t & 1);
		}
	}

	/* step 11: reciprocity coefficients */
	phy_populate_recipcoeffs(phy);
}

/* ------------------------------------------- 10, 11, 12: debug/iovar overrides */

/*
 * acphy-rxgain.md, section 10 (wlc_phy_calc_extra_init_gain_acphy): distribute
 * an extra receive gain over the gain stages of each core, from the current
 * initial-gain code words. Iovar/debug path, not in the traces (from the
 * specification only; the exact split order is not fully specified there).
 */
void bcm4360_phy_calc_extra_init_gain_acphy(struct bcm4360_phy *phy, u8 want, u8 *out)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 g[BCM4360_PHY_CORES_MAX];
	u32 n = phy_cores(phy);
	u16 s;
	u32 c;

	s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);
	bcm4360_tbl_read(io, TBL_RFSEQ, n, 0xf9, 16, g);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);

	for (c = 0; c < n; c++) {
		s16 a1 = (s16)(4 - ((g[c] >> 6) & 0xf));
		s16 a2 = (s16)((10 - ((g[c] >> 10) & 7)) - (g[c] >> 13));
		s16 avail;

		if (a1 > 4)
			a1 = 4;
		if (a1 < 0)
			a1 = 0;
		if (a2 < 0)
			a2 = 0;
		avail = a1 + 4 + a2;
		if (avail < (s16)want)
			want = (u8)avail;
	}

	if (want == 0)
		return;

	for (c = 0; c < n; c++) {
		u16 gc = g[c];
		u8 mixer0 = (gc >> 6) & 0xf;
		u8 lna0 = (gc >> 10) & 7;
		s16 mixer_room = (s16)(4 - mixer0);
		s16 lna_room = (s16)(10 - lna0);
		u8 rem = want;
		u8 to_mixer, to_lna;

		if (mixer_room < 0)
			mixer_room = 0;
		if (mixer_room > 4)
			mixer_room = 4;
		if (lna_room < 0)
			lna_room = 0;
		to_mixer = min(rem, (u8)mixer_room);
		rem = (u8)(rem - to_mixer);
		to_lna = min(rem, (u8)lna_room);

		out[6 * c + 0] = gc & 7;
		out[6 * c + 1] = (gc >> 3) & 7;
		out[6 * c + 2] = (u8)(to_mixer + mixer0);
		out[6 * c + 3] = (u8)(to_lna + lna0);
		out[6 * c + 4] = lna0;
		out[6 * c + 5] = min(want, (u8)4);
	}
}

/*
 * acphy-rxgain.md, section 10 (wlc_phy_rfctrl_override_rxgain_acphy): apply or
 * restore the per-core receive-gain override registers. `codes` = the six
 * bytes per core of the previous function; `save` = four words per core.
 * Iovar/debug path, not in the traces.
 */
void bcm4360_phy_rfctrl_override_rxgain_acphy(struct bcm4360_phy *phy, u8 mode,
					      const u8 *codes, u16 *save)
{
	struct bcm4360_phy_io *io = &phy->io;
	bool is2g = (phy->radio_chanspec & BCM4360_CHANSPEC_BAND) == BCM4360_CHANSPEC_BAND_2G;
	u32 n = phy_cores(phy);
	u16 s;
	u32 c;

	if (mode == 1) {
		for (c = 0; c < n; c++) {
			u32 o = ACPHY_CORE_STEP * c;

			bcm4360_phy_write(io, ACPHY_REG_0x722 + o, save[4 * c + 0]);
			bcm4360_phy_write(io, ACPHY_REG_0x730 + o, save[4 * c + 1]);
			bcm4360_phy_write(io, ACPHY_REG_0x731 + o, save[4 * c + 2]);
			bcm4360_phy_write(io, ACPHY_REG_0x734 + o, save[4 * c + 3]);
		}
		return;
	}

	s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	for (c = 0; c < n; c++) {
		u32 o = ACPHY_CORE_STEP * c;
		const u8 *cd = &codes[6 * c];
		u8 e0, e1;

		save[4 * c + 0] = bcm4360_phy_read(io, ACPHY_REG_0x722 + o);
		save[4 * c + 1] = bcm4360_phy_read(io, ACPHY_REG_0x730 + o);
		save[4 * c + 2] = bcm4360_phy_read(io, ACPHY_REG_0x731 + o);
		save[4 * c + 3] = bcm4360_phy_read(io, ACPHY_REG_0x734 + o);

		bcm4360_tbl_read(io, TBL_15, 1, 0x18 * c + (is2g ? 5 : 0xd), 8, &e0);
		bcm4360_tbl_read(io, TBL_15, 1, 0x18 * c + 0x16, 8, &e1);

		bcm4360_phy_write(io, ACPHY_REG_0x730 + o,
				  (u16)(((cd[5] & 0x3f) << 10) | (cd[2] << 6) |
					(cd[1] << 3) | cd[0]));
		bcm4360_phy_write(io, ACPHY_REG_0x731 + o,
				  (u16)(((e1 >> 3) << 4) | ((e0 >> 3) & 0x0f)));
		bcm4360_phy_write(io, ACPHY_REG_0x734 + o, (u16)(cd[3] | (cd[4] << 3)));

		bcm4360_phy_mod(io, ACPHY_REG_0x722 + o, 0x0002, 0x0002);
		bcm4360_phy_mod(io, ACPHY_REG_0x722 + o, 0x0004, 0x0004);
		bcm4360_phy_mod(io, ACPHY_REG_0x722 + o, 0x0008, 0x0008);

		(void)bcm4360_phy_read(io, ACPHY_REG_0x730 + o);
		(void)bcm4360_phy_read(io, ACPHY_REG_0x731 + o);
		(void)bcm4360_phy_read(io, ACPHY_REG_0x734 + o);
	}

	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);
}

/*
 * acphy-rxgain.md, section 11 (wlc_phy_lpf_hpc_override_acphy): override the
 * receive low-pass-filter "hpc" fields per core. Iovar/debug path, not in the
 * traces. The saved values live in phy->lpf_hpc_ovr_save.
 */
void bcm4360_phy_lpf_hpc_override_acphy(struct bcm4360_phy *phy, bool apply)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 n = phy_cores(phy);
	u16 lo, hi, s;
	u32 c;

	if (!apply) {
		phy->lpf_hpc_ovr_active = 0;
		for (c = 0; c < n; c++) {
			u32 o = ACPHY_CORE_STEP * c;

			bcm4360_phy_write(io, ACPHY_REG_0x723 + o, phy->lpf_hpc_ovr_save[c][0]);
			bcm4360_phy_write(io, ACPHY_REG_0x735 + o, phy->lpf_hpc_ovr_save[c][1]);
		}
		return;
	}

	phy->lpf_hpc_ovr_active = 1;
	s = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);
	lo = tbl_read16(io, TBL_RFSEQ, 0x122);
	hi = tbl_read16(io, TBL_RFSEQ, 0x125);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s & 0x0002);

	for (c = 0; c < n; c++) {
		u32 o = ACPHY_CORE_STEP * c;

		phy->lpf_hpc_ovr_save[c][0] = bcm4360_phy_read(io, ACPHY_REG_0x723 + o);
		phy->lpf_hpc_ovr_save[c][1] = bcm4360_phy_read(io, ACPHY_REG_0x735 + o);
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + o, 0x0004, 0x0004);
		bcm4360_phy_mod(io, ACPHY_REG_0x735 + o, 0x00e0,
				(u16)(((hi >> (4 * c)) & 0x0f) << 5));
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + o, 0x0002, 0x0002);
		bcm4360_phy_mod(io, ACPHY_REG_0x735 + o, 0x001e,
				(u16)(((lo >> (4 * c)) & 0x0f) << 1));
	}
}

/*
 * acphy-rxgain.md, section 12 (wlc_phy_dig_lpf_override_acphy): override the
 * ten digital-LPF coefficient registers PHY(0x18b)..PHY(0x194). Iovar/debug
 * path, not in the traces. Save area phy->dig_lpf_ovr_save, flag cal.pi_0xf82.
 */
void bcm4360_phy_dig_lpf_override_acphy(struct bcm4360_phy *phy, u8 mode)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 i;

	if (mode == 0) {
		if (phy->cal.pi_0xf82) {
			for (i = 0; i < 10; i++)
				bcm4360_phy_write(io, ACPHY_REG_0x18b + i,
						  phy->dig_lpf_ovr_save[i]);
			phy->cal.pi_0xf82 = 0;
		}
		return;
	}

	if (phy->cal.pi_0xf82 == 0) {
		for (i = 0; i < 10; i++)
			phy->dig_lpf_ovr_save[i] = bcm4360_phy_read(io, ACPHY_REG_0x18b + i);
		phy->cal.pi_0xf82 = 1;
	}

	if (mode == 1) {
		for (i = 0; i < 10; i++)
			bcm4360_phy_write(io, ACPHY_REG_0x18b + i,
					  bcm4360_phy_read(io, ACPHY_REG_0x181 + i));
	} else if (mode == 2) {
		for (i = 0; i < 10; i++)
			bcm4360_phy_write(io, ACPHY_REG_0x18b + i,
					  (i == 0 || i == 5) ? 0x02d4 : 0);
	}
}
