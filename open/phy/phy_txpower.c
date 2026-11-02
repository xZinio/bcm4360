// SPDX-License-Identifier: ISC
/*
 * AC-PHY of the BCM4360: transmit power. The transmit gain set by index, the
 * closed-loop power-control set-up (loop parameters, target powers, the
 * estimated-power tables computed from the PA parameters, the detector-offset
 * table), the idle-TSSI measurement and the transmit-calibration coefficient
 * apply. These are the leaf functions that the channel function
 * (acphy-chanspec.md, section 5), the initialisation (acphy-init.md, sections
 * 6 and 7) and the band-change function (acphy-rxgain.md, sub_09e378) call.
 *
 * Written from docs/re/spec/acphy-txpower.md (a "section" in the comments is a
 * section of its "Procedures"), docs/re/spec/acphy-chanspec.md section 5,
 * docs/re/spec/acphy-init.md sections 6 and 7, and, for the register access,
 * docs/re/spec/access.md; for chip 0x4360, AC-PHY revision 0 or 1 and radio
 * 2069 revision 3 or 4. Measured TSSI reads 0 in the model, so the measurement
 * produces zeros; the branches that use a non-zero reading are written from the
 * specification and not exercised. The notation PHY(a), RADIO(a), TBL(id)[i],
 * D11(o) and mod() in the comments is the one of access.md.
 */
#include <bcm4360/phy.h>
#include <bcm4360/phy_env.h>

/* PHY registers accessed here, named by their address (purpose unknown). */
#define ACPHY_REG_0x012		0x012	/* debug output I (section 25) */
#define ACPHY_REG_0x013		0x013	/* debug output Q / TSSI (section 25) */
#define ACPHY_REG_0x019e	0x19e	/* bit 1: table-access / stall bracket */
#define ACPHY_REG_0x070		0x070	/* power-control command */
#define ACPHY_REG_0x071		0x071	/* power-control timing */
#define ACPHY_REG_0x072		0x072	/* TSSI mode */
#define ACPHY_REG_0x382		0x382	/* sample-play control (acphy-cal-tx tone) */
#define ACPHY_REG_0x392		0x392	/* debug select (section 27) */
#define ACPHY_REG_0x393		0x393	/* debug select (section 27) */
#define ACPHY_REG_0x394		0x394	/* debug select (section 27) */
#define ACPHY_REG_0x400		0x400	/* RF sequencer mode (acphy-cal-tx tone) */
#define ACPHY_REG_0x401		0x401	/* RF sequencer core activation (section 23) */
#define ACPHY_REG_0x403		0x403	/* RF sequencer status (acphy-cal-tx tone) */
#define ACPHY_REG_0x40f		0x40f	/* saved around the ADC read (section 26) */

/* sample-play registers of the test tone (acphy-cal-tx; from the trace) */
#define ACPHY_REG_0x461		0x461
#define ACPHY_REG_0x462		0x462
#define ACPHY_REG_0x463		0x463
#define ACPHY_REG_0x464		0x464
#define ACPHY_REG_0x465		0x465
#define ACPHY_REG_0x466		0x466
#define ACPHY_REG_0x467		0x467
#define ACPHY_REG_0x460		0x460
#define ACPHY_REG_0x471		0x471

/* per-core power-control registers, core 0; core c adds c * ACPHY_CORE_STEP */
#define ACPHY_REG_0x640		0x640	/* status: index the loop is at (bits 8..14) */
#define ACPHY_REG_0x641		0x641	/* TSSI visible threshold */
#define ACPHY_REG_0x642		0x642	/* status: second power estimate (bits 0..7) */
#define ACPHY_REG_0x644		0x644	/* start index of the loop (bits 0..6) */
#define ACPHY_REG_0x645		0x645	/* idle TSSI (bits 0..9) */
#define ACPHY_REG_0x646		0x646	/* target power (bits 0..7) */
#define ACPHY_REG_0x6a0		0x6a0	/* rx IQ compensation a (section 20) */
#define ACPHY_REG_0x6a1		0x6a1	/* rx IQ compensation b (section 20) */
#define ACPHY_REG_0x722		0x722	/* tx-gain override enables (section 24) */
#define ACPHY_REG_0x725		0x725	/* reset-pulse register (section 28) */
#define ACPHY_REG_0x727		0x727	/* TSSI override enable (section 22) */
#define ACPHY_REG_0x732		0x732	/* tx gain, low word (section 24) */
#define ACPHY_REG_0x733		0x733	/* tx gain, high word (section 24) */
#define ACPHY_REG_0x734		0x734	/* filter gain (section 24) */
#define ACPHY_REG_0x739		0x739	/* reset-pulse register (section 28) */
#define ACPHY_REG_0x73a		0x73a	/* reset-pulse register (section 28) */
#define ACPHY_REG_0x73c		0x73c	/* TSSI input select (section 22) */
#define ACPHY_REG_0x747		0x747	/* digital gain (section 24) */

/* the 802.11 core registers touched directly by the debug-output select (section 27) */
#define D11_REG_MACCONTROL	0x120
#define D11_REG_0x49e		0x49e

/* PHY table ids (access.md, acphy-txpower.md "Data") */
#define TBL_TXGAIN		0x20	/* transmit gain table of the band (48 bit) */
#define TBL_GAINCODE		0x07	/* gain code in use when set by index */
#define TBL_BBMULT		0x0c	/* bbmult and tx-cal coefficients */
#define TBL_PDOFF		0x21	/* power-detector offsets (32 bit) */
#define TBL_ESTPWR_CORE0	0x40	/* estimated power core 0 (16 bit); +0x20 per core */

/* TBL(0x07) offsets: gain code of core c when set by index (section 4) */
#define TBL_GAINCODE_LO		0x100	/* [0x100 + core] */
#define TBL_GAINCODE_MID	0x103	/* [0x103 + core] */
#define TBL_GAINCODE_HI		0x106	/* [0x106 + core] */
#define TBL_GAINCODE_RADIO	0x17e	/* [0x17e + 0x10 * core] (section 24 step 5.6) */

/* TBL(0x0c) offsets (sections 2, 20, 21): first + 4 * core */
#define TBL_BBMULT_A		0x63	/* bbmult, OFDM copy */
#define TBL_BBMULT_B		0x73	/* bbmult, 11b copy */
#define TBL_TXIQ_OFDM		0x60	/* tx IQ coefficients a, b (OFDM) */
#define TBL_LOFT_OFDM		0x62	/* digital LO leakage d (OFDM) */

/* radio registers of the LO-leakage compensation (section 20) */
#define R2069_LOFT_I		0x002	/* | core << 9; e, f, I and Q at 0x002..0x005 */
/* radio registers used around the measurement (section 24) */
#define R2069_MEAS_04e		0x04e	/* | core << 9 */
#define R2069_MEAS_166		0x166	/* | core << 9 */

/* PHY(0x70): the three hardware-power-control enable bits (section 7) */
#define ACPHY_0x70_ENABLE	0xe000

/* PHY(0x644): start index of the loop (section 8, 9) */
#define ACPHY_0x644_INDEX	0x007f

/* the valid bits of the two power estimates (section 12) */
#define ACPHY_0x640_VALID	0x8000
#define ACPHY_0x642_VALID	0x0100

/* the estimated-power table has 128 entries (section 13 step 15) */
#define ESTPWR_ENTRIES		128
/* the detector-offset table has 24 entries (section 13 step 16) */
#define PDOFF_ENTRIES		24

/* the transmit-gain "by index" of an uninitialised core saved index (section 7) */
#define TXPWR_INDEX_NONE	0x80

/* MAC capability bit 29 (sh+0x2c): Bluetooth coexistence (section 28); 0 in the model */
#define MACHWCAP_BTCX		0x20000000

/* --------------------------------------------------------------- helpers */

/* sub_099528: the result for a non-zero offset of a core above N (acphy-attach.md) */
#define TXPOWER_CORE_OFFSET_BADCORE	(-2)

/* the number of cores N of the PHY (pi+0x168) */
static u32 phy_cores(const struct bcm4360_phy *phy)
{
	return phy->ver.cores;
}

/* the radio is tuned to a 2.4 GHz channel */
static bool phy_is_2g(const struct bcm4360_phy *phy)
{
	return (phy->radio_chanspec & BCM4360_CHANSPEC_BAND) == BCM4360_CHANSPEC_BAND_2G;
}

/*
 * acphy-init.md, H13 (wlc_phyreg_enter/exit): a nesting counter; the first
 * enter takes the ucode wake override, the last exit releases it.
 */
static void phy_reg_enter(struct bcm4360_phy *phy)
{
	if (phy->phyreg_count++ == 0)
		bcm4360_mac_wake_override_set(phy->hw);
}

static void phy_reg_exit(struct bcm4360_phy *phy)
{
	if (--phy->phyreg_count == 0)
		bcm4360_mac_wake_override_clear(phy->hw);
}

/* per-core register base: core c uses address + c * ACPHY_CORE_STEP */
static u16 core_reg(u16 base, u32 core)
{
	return (u16)(base + core * ACPHY_CORE_STEP);
}

/* docs/re/spec/access.md, "PHY tables": one 16 bit entry */
static void tbl_write16(struct bcm4360_phy_io *io, u32 id, u32 offset, u16 v)
{
	bcm4360_tbl_write(io, id, 1, offset, 16, &v);
}

/* docs/re/spec/access.md, "PHY tables": one 16 bit entry */
static u16 tbl_read16(struct bcm4360_phy_io *io, u32 id, u32 offset)
{
	u16 v = 0;

	bcm4360_tbl_read(io, id, 1, offset, 16, &v);
	return v;
}

/*
 * acphy-txpower.md, "Procedures", "Table access bracket": save bit 1 of
 * PHY(0x19e), set it, and (later) restore it. The brackets nest.
 */
static u16 tbl_bracket_enter(struct bcm4360_phy_io *io)
{
	u16 s = bcm4360_phy_read(io, ACPHY_REG_0x019e);

	bcm4360_phy_mod(io, ACPHY_REG_0x019e, 0x0002, 0x0002);
	return s;
}

static void tbl_bracket_leave(struct bcm4360_phy_io *io, u16 saved)
{
	bcm4360_phy_mod(io, ACPHY_REG_0x019e, 0x0002, saved & 0x0002);
}

/* --------------------------------------------------------- transmit gain */

/*
 * The "transmit gain setting" record (acphy-txpower.md, "Data"): 10 bytes,
 * the pieces of one 48 bit gain-table entry. Fields at the byte offsets the
 * spec names.
 */
struct txgain_rec {
	u16 code_lo;	/* +0: gain code, low part (bits 8..23 of the entry) */
	u16 code_mid;	/* +2: gain code, middle part (bits 24..39) */
	u16 code_hi;	/* +4: gain code, high part (bits 40..47) */
	u16 pad;	/* +6: not written by sub_09868f */
	u16 bbmult;	/* +8: bbmult (bits 0..7) */
};

/*
 * acphy-txpower.md, section 1 (sub_09868f,
 * wlc_phy_get_txgain_settings_by_index_acphy): read one step of the gain table
 * loaded in the PHY and split it into the record.
 */
static void phy_get_txgain_by_index(struct bcm4360_phy *phy, struct txgain_rec *out, s8 index)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 off = (u32)(s32)index;	/* signed 8 bit, sign extended to the offset */
	u16 w[3];
	u16 saved;

	saved = tbl_bracket_enter(io);				/* step 1 */
	bcm4360_tbl_read(io, TBL_TXGAIN, 1, off, 48, w);	/* step 2 */
	tbl_bracket_leave(io, saved);

	/* step 3: w0, w1, w2 (lowest first) split into the record */
	out->bbmult = (u16)(w[0] & 0x00ff);
	out->code_lo = (u16)((w[0] >> 8) | ((w[1] & 0x00ff) << 8));
	out->code_mid = (u16)((w[1] >> 8) | ((w[2] & 0x00ff) << 8));
	out->code_hi = (u16)(w[2] >> 8);
}

/*
 * acphy-txpower.md, section 2 (sub_09c4e4, wlc_phy_set_tx_bbmult_acphy): set
 * the digital scaling of the baseband signal of one core.
 */
static void phy_set_tx_bbmult(struct bcm4360_phy *phy, const u16 *m, u32 core)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 saved = tbl_bracket_enter(io);

	tbl_write16(io, TBL_BBMULT, TBL_BBMULT_A + 4 * core, *m);
	tbl_write16(io, TBL_BBMULT, TBL_BBMULT_B + 4 * core, *m);
	tbl_bracket_leave(io, saved);
}

/*
 * acphy-txpower.md, section 3 (sub_098751, wlc_phy_get_tx_bbmult_acphy): read
 * the bbmult of one core.
 */
static void phy_get_tx_bbmult(struct bcm4360_phy *phy, u16 *m, u32 core)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 saved = tbl_bracket_enter(io);

	*m = tbl_read16(io, TBL_BBMULT, TBL_BBMULT_A + 4 * core);
	tbl_bracket_leave(io, saved);
}

/*
 * acphy-txpower.md, section 4 (wlc_phy_txpwr_by_index_acphy): set the transmit
 * gain of the cores in the mask to step `index` of the gain table.
 */
void bcm4360_phy_txpwr_by_index_acphy(struct bcm4360_phy *phy, u8 coremask, s8 index)
{
	struct bcm4360_phy_io *io = &phy->io;
	struct txgain_rec rec;
	u16 saved;
	u32 c;

	saved = tbl_bracket_enter(io);				/* step 1 */
	for (c = 0; c < phy_cores(phy); c++) {			/* step 2 */
		if (!(coremask & (1u << c)))
			continue;
		phy_get_txgain_by_index(phy, &rec, index);	/* step 2.1 */
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_LO + c, rec.code_lo);
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_MID + c, rec.code_mid);
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_HI + c, rec.code_hi);
		phy_set_tx_bbmult(phy, &rec.bbmult, c);		/* step 2.3 */
		phy->txpwr.index[c] = (u8)index;		/* step 2.4 */
	}
	tbl_bracket_leave(io, saved);
}

/* ----------------------------------------------- power-control enable */

/*
 * acphy-txpower.md, section 8 (sub_0906ae,
 * wlc_phy_txpwrctrl_get_cur_index_acphy): the gain-table index the control loop
 * is at, for core 0, 1, 2.
 */
static u8 phy_txpwrctrl_get_cur_index(struct bcm4360_phy *phy, u32 core)
{
	if (core > 2)
		return 0;
	return (u8)((bcm4360_phy_read(&phy->io, core_reg(ACPHY_REG_0x640, core)) & 0x7f00) >> 8);
}

/*
 * acphy-txpower.md, section 9 (sub_08f3d4,
 * wlc_phy_txpwrctrl_set_cur_index_acphy): the index the loop starts from, for
 * core 0, 1, 2.
 */
static void phy_txpwrctrl_set_cur_index(struct bcm4360_phy *phy, u8 index, u32 core)
{
	if (core > 2)
		return;
	bcm4360_phy_mod(&phy->io, core_reg(ACPHY_REG_0x644, core), ACPHY_0x644_INDEX, index);
}

/*
 * acphy-txpower.md, section 7 (wlc_phy_txpwrctrl_enable_acphy): switch the
 * hardware power control on or off.
 */
void bcm4360_phy_txpwrctrl_enable_acphy(struct bcm4360_phy *phy, bool on)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c;

	phy->txpwr.hwpwrctrl = on;				/* step 1 (ctrl < 2) */

	if (!on) {
		u16 v = bcm4360_phy_read(io, ACPHY_REG_0x070);	/* step 2.1 */

		if ((v & ACPHY_0x70_ENABLE) == ACPHY_0x70_ENABLE) {
			for (c = 0; c < phy_cores(phy); c++)
				phy->txpwr.index_saved[c] = phy_txpwrctrl_get_cur_index(phy, c);
		}
		bcm4360_phy_mod(io, ACPHY_REG_0x070, ACPHY_0x70_ENABLE, 0);	/* step 2.2 */
		return;
	}

	bcm4360_phy_mod(io, ACPHY_REG_0x070, ACPHY_0x70_ENABLE, ACPHY_0x70_ENABLE);	/* 3.1 */
	for (c = 0; c < phy_cores(phy); c++) {			/* step 3.2 */
		u8 i = phy->txpwr.index_saved[c];

		if (i != TXPWR_INDEX_NONE)
			phy_txpwrctrl_set_cur_index(phy, i, c);
	}
}

/*
 * acphy-txpower.md, section 12 (wlc_phy_txpwr_est_pwr_acphy): the two power
 * estimates of the hardware per core. `out1` from PHY(0x642 + c * 0x200)
 * (valid: bit 8), `out2` from PHY(0x640 + c * 0x200) (valid: bit 15); a valid
 * register is read a second time and its low byte taken, otherwise 0.
 */
static void phy_txpwr_est_pwr(struct bcm4360_phy *phy, u8 *out1, u8 *out2)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c;

	for (c = 0; c < phy_cores(phy); c++) {
		u16 r642 = core_reg(ACPHY_REG_0x642, c);
		u16 r640 = core_reg(ACPHY_REG_0x640, c);

		out1[c] = 0;						/* step 1 */
		if (bcm4360_phy_read(io, r642) & ACPHY_0x642_VALID)
			out1[c] = (u8)bcm4360_phy_read(io, r642);
		out2[c] = 0;						/* step 2 */
		if (bcm4360_phy_read(io, r640) & ACPHY_0x640_VALID)
			out2[c] = (u8)bcm4360_phy_read(io, r640);
	}
}

/*
 * acphy-txpower.md, section 10 (wlc_phy_txpwrctrl_set_target_acphy): the target
 * power in quarter dBm of core 0, 1, 2.
 */
static void phy_txpwrctrl_set_target(struct bcm4360_phy *phy, u8 power, u32 core)
{
	if (core > 2)
		return;
	bcm4360_phy_mod(&phy->io, core_reg(ACPHY_REG_0x646, core), 0x00ff, power);
}

/* ------------------------------------------------ closed-loop set-up */

/* default PA parameters when the SROM has none (section 13 step 5) */
static const s16 pa_defaults[BCM4360_PHY_CORES_MAX][BCM4360_PHY_PA_PARAMS] = {
	{ -183, 4825, -615 },
	{ -172, 4626, -631 },
	{ -173, 4535, -576 },
};

/*
 * acphy-txpower.md, section 14 (sub_08ef5f, wlc_phy_pdoffset_cal_acphy): OR the
 * 4 bit power-detector offset of core c (selected by the sub-band, sign
 * extended to 8 bit) into the accumulator at byte c.
 */
static u32 phy_pdoffset_cal(u32 acc, u16 v, u8 band, u32 core)
{
	u32 n;

	if (band == 2)
		n = (v >> 4) & 0xf;
	else if (band == 3)
		n = (v >> 8) & 0xf;
	else if (band == 4)
		n = (v >> 12) & 0xf;
	else
		n = v & 0xf;
	if (n >= 8)
		n |= 0xf0;		/* sign extension of a 4 bit number */
	return acc | (n << (8 * core));
}

/*
 * acphy-txpower.md, section 13 step 15: the estimated-power table of one core,
 * 128 entries, power in quarter dBm as a function of the TSSI. Fixed point, no
 * floating point; the divisions are 32 bit and truncate toward zero, exactly as
 * the spec gives the polynomial.
 */
static void phy_est_pwr_table(s16 a1, s16 b0, s16 b1, u16 *entry)
{
	u32 t;

	for (t = 0; t < ESTPWR_ENTRIES; t++) {
		s32 den = 32768 + a1 * (s32)t;
		s32 num = 512 * (s32)b0 + 32 * b1 * (s32)t;
		s32 p = (num + den / 2) / den;

		if (p < -8)
			p = -8;
		if (p > 127)
			p = 127;
		entry[t] = (u16)(p & 0xff);
	}
}

/*
 * acphy-txpower.md, section 13 step 16: the 24-entry detector-offset table.
 * W40/W80/W2G are built with phy_pdoffset_cal over the cores of the mask.
 */
static void phy_pdoffset_table(struct bcm4360_phy *phy, u8 band, bool is2g, u32 *entry)
{
	struct bcm4360_phy_txpwr *tx = &phy->txpwr;
	u32 w40 = 0, w80 = 0, w2g = 0;
	u32 c;

	for (c = 0; c < phy_cores(phy); c++) {
		if (!(phy->hw_rxchain & (1u << c)))
			continue;
		w40 = phy_pdoffset_cal(w40, tx->pdoffset40ma[c], band, c);
		w80 = phy_pdoffset_cal(w80, tx->pdoffset80ma[c], band, c);
		w2g = phy_pdoffset_cal(w2g, tx->pdoffset2g40ma[c], band, c);
	}

	hw_memset(entry, 0, PDOFF_ENTRIES * sizeof(u32));
	if (!is2g) {
		entry[1] = w40 & 0xffffff;
		entry[5] = w40 & 0xffffff;
		entry[6] = w40 & 0xffffff;
		entry[10] = w80 & 0xffffff;
	} else if (tx->pdoffset2g40mvalid != 1) {
		entry[5] = w2g & 0xffffff;
	}
}

/*
 * acphy-txpower.md, section 13 (sub_098949,
 * wlc_phy_txpwrctrl_pwr_setup_acphy): set up the closed-loop power control for
 * the current channel. Leaves bit 15 of PHY(0x70) cleared for the caller.
 *
 * Not reached by the compared PHY-API stages (sub_09949f is called by phy-cmn,
 * after wlc_phy_chanspec_set returns); written from the specification.
 */
static void phy_txpwrctrl_pwr_setup(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	struct bcm4360_phy_txpwr *tx = &phy->txpwr;
	bool is2g = phy_is_2g(phy);
	u8 rr = phy->ver.radio_rev;
	u8 band = bcm4360_phy_get_chan_freq_range(phy, 0);
	s16 a1[BCM4360_PHY_CORES_MAX], b0[BCM4360_PHY_CORES_MAX], b1[BCM4360_PHY_CORES_MAX];
	s8 target[BCM4360_PHY_CORES_MAX];
	s8 m = (s8)((u8)(phy->txpwr.min_power << 2));
	u16 saved;
	u16 est[ESTPWR_ENTRIES];
	u32 pdoff[PDOFF_ENTRIES];
	u32 c;
	int cc;

	saved = bcm4360_phy_read(io, ACPHY_REG_0x019e);				/* step 1 */
	bcm4360_phy_mod(io, ACPHY_REG_0x019e, 0x0002, 0x0002);
	bcm4360_phy_mod(io, ACPHY_REG_0x072, 0x0001, 0x0001);			/* step 2 */
	bcm4360_phy_mod(io, ACPHY_REG_0x070, 0x8000, 0);			/* step 3 */
	bcm4360_phy_mod(io, ACPHY_REG_0x070, 0x0100,				/* step 4 */
			(rr == 4 || rr == 8 || phy->ver.phy_rev == 3) ? 0x0100 : 0);

	/* step 5: the PA parameters, replaced by the defaults if a1 is 0 */
	for (c = 0; c < phy_cores(phy); c++) {
		a1[c] = tx->pa[c][band][0];
		b0[c] = tx->pa[c][band][1];
		b1[c] = tx->pa[c][band][2];
		if (a1[c] == 0) {
			a1[c] = pa_defaults[c][0];
			b0[c] = pa_defaults[c][1];
			b1[c] = pa_defaults[c][2];
		}
	}

	/* step 6: the target of each core, at least m = min_power dBm */
	for (c = 0; c < phy_cores(phy); c++) {
		s8 t = phy->txpwr.target_max[c];

		target[c] = (t > m) ? t : m;
	}

	bcm4360_phy_mod(io, ACPHY_REG_0x072, 0x4000, 0);			/* step 7 */
	bcm4360_phy_mod(io, ACPHY_REG_0x072, 0x4000, 0x4000);
	bcm4360_phy_mod(io, ACPHY_REG_0x070, 0x8000, 0);			/* step 8 */
	for (c = 0; c < phy_cores(phy); c++)					/* step 9 */
		bcm4360_phy_mod(io, core_reg(ACPHY_REG_0x644, c), ACPHY_0x644_INDEX,
				(rr == 4 || rr == 8) ? 20 : 50);

	/* step 10: the delay from the start of frame to the TSSI sample */
	{
		u8 g = is2g ? tx->pdgain2g : tx->pdgain5g;
		u16 d = (g > 4) ? 200 : (g == 4) ? 220 : 150;

		bcm4360_phy_mod(io, ACPHY_REG_0x071, 0x00ff, d);
	}
	bcm4360_phy_mod(io, ACPHY_REG_0x071, 0x0700, 0x0400);			/* step 11 */
	bcm4360_phy_mod(io, ACPHY_REG_0x071, 0x0700, 0x0400);
	bcm4360_phy_mod(io, ACPHY_REG_0x070, 0x0800, 0);			/* step 12 */
	bcm4360_phy_mod(io, ACPHY_REG_0x070, 0x0400,				/* step 13 */
			phy->ver.phy_rev == ACPHY_REV_1 ? 0x0400 : 0);

	/* step 14: the targets, in falling core order */
	for (cc = (int)phy_cores(phy) - 1; cc >= 0; cc--)
		phy_txpwrctrl_set_target(phy, (u8)target[cc], (u32)cc);

	/* step 15: the estimated-power table of each core */
	for (c = 0; c < phy_cores(phy); c++) {
		phy_est_pwr_table(a1[c], b0[c], b1[c], est);
		bcm4360_tbl_write(io, TBL_ESTPWR_CORE0 + 0x20 * c, ESTPWR_ENTRIES, 0, 16, est);
	}

	/* step 16: the detector-offset table */
	phy_pdoffset_table(phy, band, is2g, pdoff);
	bcm4360_tbl_write(io, TBL_PDOFF, PDOFF_ENTRIES, 0, 32, pdoff);

	bcm4360_phy_mod(io, ACPHY_REG_0x019e, 0x0002, saved & 0x0002);		/* step 17 */
}

/*
 * acphy-txpower.md, section 16 (wlc_phy_tssivisible_thresh_acphy): the TSSI
 * visible threshold decided by the board; no hardware access. The channel
 * function reproduces the same value inline (acphy-chanspec.md, section 5, step
 * 28); this is the procedure of the specification, for the MAC layer's caller.
 */
u8 bcm4360_phy_tssivisible_thresh_acphy(struct bcm4360_phy *phy)
{
	const struct bcm4360_phy_board *b = &phy->board;
	u8 ch = phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL;

	if (phy->ver.phy_rev != ACPHY_REV_0 && phy->ver.phy_rev != ACPHY_REV_1)
		return 0x80;	/* other revisions: the spec gives 0x1c (rev 3) or 0x80 */
	if (phy->femctrl == 3 && phy->flags.bf3_bits0_2 == 2)
		return 0x26;
	if (b->chip == BCM4360_CHIP_ID_4360 &&
	    (b->boardtype == BCM4360_BOARDTYPE_0x137 || b->boardtype == BCM4360_BOARDTYPE_0x117))
		return 0x14;
	if (b->chip == BCM4360_CHIP_ID_4360 &&
	    (b->boardtype == 0x134 || b->boardtype == 0x112) && ch > 148)
		return 0x16;
	return 0x18;
}

/*
 * acphy-txpower.md, section 17 (sub_09949f,
 * wlc_phy_txpower_recalc_target_acphy): apply new target powers. Called by
 * phy-cmn (sub_0b8ca4) and by sub_099528; not a compared PHY-API stage.
 */
void bcm4360_phy_txpower_recalc_target_acphy(struct bcm4360_phy *phy)
{
	/* step 2: hand the per-rate offsets to the MAC (SHM only, no PHY register) */
	bcm4360_mac_update_txppr_offset(phy->hw, phy->txpwr.ppr);
	phy_txpwrctrl_pwr_setup(phy);					/* step 3 */
	/* step 4: back to the state the control had (the set-up cleared bit 15) */
	bcm4360_phy_txpwrctrl_enable_acphy(phy, phy->txpwr.hwpwrctrl);
}

/*
 * acphy-txpower.md, section 18 (sub_099528,
 * wlc_phy_txpower_core_offset_set_acphy): store the per-core power offsets and
 * recalculate the targets if one changed (acphy-attach.md, "Function pointer
 * table": result 0, or -2 for a non-zero offset of a core above N; the index
 * N itself is accepted). Reached from the MAC through
 * wlc_phy_txpower_core_offset_set (sub_0995ab does nothing on the 4360).
 */
s32 bcm4360_phy_txpower_core_offset_set_acphy(struct bcm4360_phy *phy, const s8 *offsets)
{
	bool changed = false;
	u32 i;

	for (i = 0; i < BCM4360_PHY_CORE_SLOTS; i++) {
		if (offsets[i] != 0 && i > phy_cores(phy))
			return TXPOWER_CORE_OFFSET_BADCORE;
		if (phy->txpwr.core_offset[i] != offsets[i]) {
			phy->txpwr.core_offset[i] = offsets[i];
			changed = true;
		}
	}
	if (changed && phy->clk) {
		bcm4360_mac_suspend(phy->hw);
		bcm4360_phy_txpower_recalc_target_acphy(phy);
		bcm4360_mac_enable(phy->hw);
	}
	return 0;
}

/* ------------------------------------- tx-cal coefficient apply (band change) */

/*
 * acphy-txpower.md, section 21 (sub_09bf99, wlc_phy_cal_txiqlo_coeffs_acphy),
 * for the selections k <= 11 (PHY table TBL(0x0c)). k >= 12 (the calibration
 * state, owned by acphy-cal-tx) is not reached here. mode 0 reads into `data`,
 * anything else writes from `data`.
 */
struct txiqlo_sel {
	u8 n;	/* number of 16 bit values */
	u8 o;	/* first offset */
	u8 d;	/* distance between cores */
};

static const struct txiqlo_sel txiqlo_sels[12] = {
	{ 2, 0x40, 8 }, { 1, 0x43, 8 }, { 1, 0x44, 8 }, { 1, 0x45, 8 },
	{ 2, 0x80, 7 }, { 1, 0x83, 7 }, { 1, 0x84, 7 }, { 1, 0x85, 7 },
	{ 2, 0x60, 4 }, { 1, 0x62, 4 }, { 2, 0x70, 4 }, { 1, 0x72, 4 },
};

static void phy_cal_txiqlo_coeffs(struct bcm4360_phy *phy, u8 mode, u16 *data, u8 k, u32 core)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 saved;

	if (k >= ARRAY_SIZE(txiqlo_sels))
		return;			/* k >= 12: the calibration state (acphy-cal-tx) */

	saved = tbl_bracket_enter(io);				/* step 1 */
	{
		const struct txiqlo_sel *sel = &txiqlo_sels[k];	/* step 2 */
		u32 offset = sel->o + core * sel->d;

		if (mode == 0)
			bcm4360_tbl_read(io, TBL_BBMULT, sel->n, offset, 16, data);
		else
			bcm4360_tbl_write(io, TBL_BBMULT, sel->n, offset, 16, data);
	}
	tbl_bracket_leave(io, saved);				/* step 5 */
}

/*
 * acphy-txpower.md, section 20 (sub_09c161, wlc_phy_txcal_coeffs_apply_acphy):
 * program the transmit-calibration coefficients per core. It does not touch the
 * gain table. `set` = 14 bytes per core; NULL means all zeros (the band
 * change resets the coefficients to 0).
 */
void bcm4360_phy_txcal_coeffs_apply_acphy(struct bcm4360_phy *phy, const u8 *set)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c;

	for (c = 0; c < phy_cores(phy); c++) {
		const u8 *r = set ? set + 14 * c : NULL;
		u16 iq[2];
		u16 loft;

		/* step 1: TBL(0x0c)[0x60 + 4c], [0x61 + 4c] = a, b (OFDM) */
		iq[0] = r ? (u16)(r[0] | (r[1] << 8)) : 0;
		iq[1] = r ? (u16)(r[2] | (r[3] << 8)) : 0;
		phy_cal_txiqlo_coeffs(phy, 1, iq, 8, c);
		/* step 2: TBL(0x0c)[0x62 + 4c] = d (OFDM) */
		loft = r ? (u16)(r[4] | (r[5] << 8)) : 0;
		phy_cal_txiqlo_coeffs(phy, 1, &loft, 9, c);
		/* step 3: the radio LO-leakage compensation, e and f, I and Q */
		bcm4360_radio_write(io, R2069_LOFT_I | (c << R2069_BANK_SHIFT), r ? r[6] : 0);
		bcm4360_radio_write(io, (R2069_LOFT_I + 1) | (c << R2069_BANK_SHIFT), r ? r[7] : 0);
		bcm4360_radio_write(io, (R2069_LOFT_I + 2) | (c << R2069_BANK_SHIFT), r ? r[8] : 0);
		bcm4360_radio_write(io, (R2069_LOFT_I + 3) | (c << R2069_BANK_SHIFT), r ? r[9] : 0);
		/* step 4: the receive IQ compensation a and b */
		bcm4360_phy_write(io, core_reg(ACPHY_REG_0x6a0, c),
				  r ? (u16)(r[10] | (r[11] << 8)) : 0);
		bcm4360_phy_write(io, core_reg(ACPHY_REG_0x6a1, c),
				  r ? (u16)(r[12] | (r[13] << 8)) : 0);
	}
}

/* ------------------------------------------------ TSSI radio-path override */

/*
 * acphy-txpower.md, section 22 (sub_08f9b4, wlc_phy_tssi_phy_setup_acphy):
 * enable the override that selects the input of the TSSI measurement (bit 4 of
 * PHY(0x73c + o) = mode). Called by the band change (mode 0) and by the idle
 * measurement (mode 0).
 */
void bcm4360_phy_tssi_phy_setup_acphy(struct bcm4360_phy *phy, u8 mode)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c;

	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = c * ACPHY_CORE_STEP;

		bcm4360_phy_mod(io, ACPHY_REG_0x727 + o, 0x0004, 0x0004);
		bcm4360_phy_mod(io, ACPHY_REG_0x73c + o, 0x0010, (u16)(mode << 4));
	}
}

/* ---------------------------------------------------- idle-TSSI measurement */

/* saved state of the ADC read (sections 24 and 26) */
struct adc_read_state {
	u16 s1;		/* bit 9 of PHY(0x40f) */
	u16 s2;		/* PHY(0x394) */
	u8 stall;	/* bit 1 of PHY(0x19e) */
};

/*
 * acphy-txpower.md, section 26 (sub_09bbe4, wlc_phy_init_adc_read): prepare the
 * PHY for reading the converter through the debug outputs. The branch that
 * depends on pi_ac+0x8e1 is taken only for PHY revision 0 boards with GPIO
 * front-end control (pi_ac+0x8e1 = 0 on this card).
 */
static void phy_init_adc_read(struct bcm4360_phy *phy, struct adc_read_state *st)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 v;

	st->stall = (u8)((bcm4360_phy_read(io, ACPHY_REG_0x019e) >> 1) & 1);	/* step 1 */
	v = bcm4360_phy_read(io, ACPHY_REG_0x40f);				/* step 2 */
	st->s1 = (u16)((v >> 9) & 1);
	bcm4360_phy_mod(io, ACPHY_REG_0x40f, 0x0200, 0);
	st->s2 = bcm4360_phy_read(io, ACPHY_REG_0x394);				/* step 3 */
	/* step 4: only if pi_ac+0x8e1 != 0 (0 on this card) */
}

/*
 * acphy-txpower.md, section 26 (sub_09be13, wlc_phy_restore_after_adc_read).
 */
static void phy_restore_after_adc_read(struct bcm4360_phy *phy, const struct adc_read_state *st)
{
	struct bcm4360_phy_io *io = &phy->io;

	bcm4360_phy_mod(io, ACPHY_REG_0x019e, 0x0002, 0x0002);			/* step 1 */
	bcm4360_phy_write(io, ACPHY_REG_0x394, st->s2);				/* step 2 */
	/* step 3: only if pi_ac+0x8e1 != 0 (skipped) */
	bcm4360_phy_mod(io, ACPHY_REG_0x40f, 0x0200, (u16)(st->s1 << 9));	/* step 4 */
	bcm4360_phy_mod(io, ACPHY_REG_0x019e, 0x0002, (u16)(st->stall << 1));	/* step 5 */
}

/*
 * acphy-txpower.md, section 27 (sub_093ebe, wlc_phy_gpiosel_acphy): select what
 * the PHY shows on its debug outputs, read back in PHY(0x012)/PHY(0x013).
 */
static void phy_gpiosel(struct bcm4360_phy *phy, u16 sel, u8 flag)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 v;

	bcm4360_phy_write(io, ACPHY_REG_0x392, 0);				/* step 1 */
	bcm4360_phy_write(io, ACPHY_REG_0x393, 0);
	v = d11_read32(phy->hw, D11_REG_MACCONTROL);				/* step 2 */
	d11_write32(phy->hw, D11_REG_MACCONTROL, v & 0xffff3fff);
	d11_write16(phy->hw, D11_REG_0x49e, 0);					/* step 3 */
	bcm4360_phy_write(io, ACPHY_REG_0x394, (u16)((flag << 8) | sel));	/* step 4 */
	bcm4360_phy_write(io, ACPHY_REG_0x392, 0xffff);				/* step 5 */
	bcm4360_phy_write(io, ACPHY_REG_0x393, 0xffff);
}

/*
 * acphy-txpower.md, section 28 (sub_092ffa, owner acphy-cal-rx): the reset
 * pulse of the converters. For each core: set bits, delay, restore in reverse
 * core order.
 */
static void phy_conv_reset_pulse(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 s739[BCM4360_PHY_CORES_MAX], s73a[BCM4360_PHY_CORES_MAX], s725[BCM4360_PHY_CORES_MAX];
	u32 c;
	int cc;

	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = c * ACPHY_CORE_STEP;

		s739[c] = bcm4360_phy_read(io, ACPHY_REG_0x739 + o);
		bcm4360_phy_write(io, ACPHY_REG_0x739 + o, s739[c] | 0x0080);
		s73a[c] = bcm4360_phy_read(io, ACPHY_REG_0x73a + o);
		bcm4360_phy_write(io, ACPHY_REG_0x73a + o, s73a[c] | 0x0080);
		s725[c] = bcm4360_phy_read(io, ACPHY_REG_0x725 + o);
		bcm4360_phy_write(io, ACPHY_REG_0x725 + o, s725[c] | 0x0204);
	}
	hw_udelay(1);
	for (cc = (int)phy_cores(phy) - 1; cc >= 0; cc--) {
		u32 o = (u32)cc * ACPHY_CORE_STEP;

		bcm4360_phy_write(io, ACPHY_REG_0x725 + o, s725[cc]);
		bcm4360_phy_write(io, ACPHY_REG_0x73a + o, s73a[cc]);
		bcm4360_phy_write(io, ACPHY_REG_0x739 + o, s739[cc]);
	}
	hw_udelay(1);
}

/*
 * The transmitter test tone and stopping playback are now fully specified in
 * docs/re/spec/acphy-cal-tx.md and implemented in open/phy/phy_cal.c as
 * bcm4360_phy_tx_tone_acphy / bcm4360_phy_stopplayback_acphy; the idle-TSSI
 * measurement (phy_poll_samps_war, step 9/12) calls them with amplitude 0.
 */

/*
 * acphy-txpower.md, section 28: the Bluetooth-coexistence overrides. No access
 * unless the MAC has the coexistence capability (sh+0x2c bit 29) and the band
 * is 2.4 GHz; sh+0x2c is 0 in the model.
 */
static void phy_btcx_override_enable(struct bcm4360_phy *phy)
{
	if (!(phy->machwcap & MACHWCAP_BTCX) || !phy_is_2g(phy))
		return;
	/* the coexistence path is out of scope (machwcap bit 29 is 0 here) */
}

static void phy_btcx_override_disable(struct bcm4360_phy *phy)
{
	if (!(phy->machwcap & MACHWCAP_BTCX))
		return;
	/* out of scope (machwcap bit 29 is 0 here) */
}

/*
 * acphy-txpower.md, section 25 (sub_093f74, wlc_phy_poll_samps_acphy): read the
 * measurement converter through the debug outputs and average 2^n readings.
 * tssi = 1 (the idle measurement), the readings are 0 in the model.
 */
static void phy_poll_samps(struct bcm4360_phy *phy, s16 *out, bool tssi, u8 n, bool adc, u32 core)
{
	struct bcm4360_phy_io *io = &phy->io;
	s32 sum = 0;
	u32 rep = 1u << n;
	u32 i;

	phy_conv_reset_pulse(phy);			/* step 1 */
	hw_udelay(100);					/* step 2 */
	for (i = 0; i < rep; i++) {			/* step 3 */
		u16 iq_i, iq_q, x;
		s32 v;

		if (adc)
			phy_gpiosel(phy, (u16)(16 + core), 1);
		/* read PHY(0x013) ("i") then PHY(0x012) ("q"); tssi uses q */
		iq_i = bcm4360_phy_read(io, ACPHY_REG_0x013);
		iq_q = bcm4360_phy_read(io, ACPHY_REG_0x012);
		x = tssi ? iq_q : iq_i;
		v = (u16)x >> 2;
		if (v >= 0x200)
			v -= 0x400;
		sum += v;
	}
	out[core] = (s16)(sum >> n);			/* step 4 */
}

/*
 * acphy-txpower.md, section 24 (sub_0af7f4, wlc_phy_poll_samps_WAR_acphy):
 * measure the power detector of one core while the transmitter plays a tone (or
 * nothing, for the idle TSSI). `idle` = 1: the gain record is not used.
 */
static void phy_poll_samps_war(struct bcm4360_phy *phy, s16 *out, bool idle,
			       const struct txgain_rec *gain, bool fixed8, bool adc, u32 core)
{
	struct bcm4360_phy_io *io = &phy->io;
	struct adc_read_state st = { 0, 0, 0 };
	u16 sv747[BCM4360_PHY_CORE_SLOTS], sv732[BCM4360_PHY_CORE_SLOTS];
	u16 sv733[BCM4360_PHY_CORE_SLOTS], sv734[BCM4360_PHY_CORE_SLOTS];
	u16 sv722[BCM4360_PHY_CORE_SLOTS], svmult[BCM4360_PHY_CORE_SLOTS];
	u16 r1[BCM4360_PHY_CORE_SLOTS], r2[BCM4360_PHY_CORE_SLOTS];
	u16 g1, g2, mult;
	u8 d, l;
	u32 c;
	u8 stall;

	if (adc) {						/* step 1 */
		phy_init_adc_read(phy, &st);
		stall = st.stall;
	} else {
		stall = 0;
	}
	bcm4360_phy_mod(io, ACPHY_REG_0x019e, 0x0002, 0x0002);	/* step 2 */

	/* step 3: save per core in the mask */
	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = c * ACPHY_CORE_STEP;

		if (!(phy->hw_rxchain & (1u << c)))
			continue;
		phy_get_tx_bbmult(phy, &svmult[c], c);
		sv747[c] = bcm4360_phy_read(io, ACPHY_REG_0x747 + o);
		sv732[c] = bcm4360_phy_read(io, ACPHY_REG_0x732 + o);
		sv733[c] = bcm4360_phy_read(io, ACPHY_REG_0x733 + o);
		sv734[c] = bcm4360_phy_read(io, ACPHY_REG_0x734 + o);
		sv722[c] = bcm4360_phy_read(io, ACPHY_REG_0x722 + o);
	}

	/* step 4: the values to apply */
	if (idle) {
		g1 = g2 = mult = 0;
		d = l = 0;
	} else {
		g1 = (u16)((gain->code_mid << 8) | (gain->code_lo >> 8));
		g2 = (u16)((gain->code_mid >> 8) | (gain->code_hi << 8));
		d = (u8)(gain->code_lo & 0x000f);
		l = (u8)((gain->code_lo & 0x00f0) >> 4);
		mult = gain->bbmult;
	}

	/* step 5: apply per core in the mask */
	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = c * ACPHY_CORE_STEP;
		u32 b = c << R2069_BANK_SHIFT;
		u16 v;

		if (!(phy->hw_rxchain & (1u << c)))
			continue;
		bcm4360_phy_write(io, ACPHY_REG_0x732 + o, g1);
		bcm4360_phy_write(io, ACPHY_REG_0x733 + o, g2);
		bcm4360_phy_write(io, ACPHY_REG_0x747 + o, d);
		bcm4360_phy_mod(io, ACPHY_REG_0x734 + o, 0x0038, (u16)((l & 7) << 3));
		bcm4360_phy_mod(io, ACPHY_REG_0x722 + o, 0x0001, 0x0001);
		bcm4360_phy_mod(io, ACPHY_REG_0x722 + o, 0x0008, 0x0008);
		phy_set_tx_bbmult(phy, &mult, c);
		r1[c] = bcm4360_radio_read(io, R2069_MEAS_04e | b);
		r2[c] = bcm4360_radio_read(io, R2069_MEAS_166 | b);
		v = (u16)(tbl_read16(io, TBL_GAINCODE, TBL_GAINCODE_RADIO + 0x10 * c) & 7);
		bcm4360_radio_mod(io, R2069_MEAS_04e | b, 0x0e00, (u16)(v << 9));
		bcm4360_radio_mod(io, R2069_MEAS_166 | b, 0x0002, 0x0002);
	}

	bcm4360_phy_mod(io, ACPHY_REG_0x019e, 0x0002, (u16)(stall << 1));	/* step 6 */
	phy_btcx_override_enable(phy);						/* step 7 */
	hw_udelay(100);								/* step 8 */
	/* the tone / sample player is now fully specified in acphy-cal-tx and
	 * implemented in open/phy/phy_cal.c (idle: amplitude 0, sample player) */
	bcm4360_phy_tx_tone_acphy(phy, 2000, idle ? 0 : 0xb5, 0, 0, 0);		/* step 9 */
	hw_udelay(100);								/* step 10 */
	{
		u8 n = fixed8 ? 3 :						/* step 11 */
			((phy->radio_chanspec & BCM4360_CHANSPEC_BW) == BCM4360_CHANSPEC_BW_80)
			? 8 : 0;

		phy_poll_samps(phy, out, true, n, adc, core);
	}
	bcm4360_phy_stopplayback_acphy(phy);					/* step 12 */
	phy_btcx_override_disable(phy);						/* step 13 */
	bcm4360_phy_mod(io, ACPHY_REG_0x019e, 0x0002, 0x0002);			/* step 14 */

	/* step 15: restore per core in the mask (note the write order) */
	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = c * ACPHY_CORE_STEP;
		u32 b = c << R2069_BANK_SHIFT;

		if (!(phy->hw_rxchain & (1u << c)))
			continue;
		bcm4360_phy_write(io, ACPHY_REG_0x732 + o, sv732[c]);
		bcm4360_phy_write(io, ACPHY_REG_0x733 + o, sv733[c]);
		bcm4360_phy_write(io, ACPHY_REG_0x747 + o, sv747[c]);
		bcm4360_phy_write(io, ACPHY_REG_0x722 + o, sv722[c]);
		bcm4360_phy_write(io, ACPHY_REG_0x734 + o, sv734[c]);
		phy_set_tx_bbmult(phy, &svmult[c], c);
		bcm4360_radio_write(io, R2069_MEAS_04e | b, r1[c]);
		bcm4360_radio_write(io, R2069_MEAS_166 | b, r2[c]);
	}
	bcm4360_phy_mod(io, ACPHY_REG_0x019e, 0x0002, (u16)(stall << 1));	/* step 16 */
	if (adc)								/* step 17 */
		phy_restore_after_adc_read(phy, &st);
}

/*
 * acphy-txpower.md, section 23 / acphy-init.md, section 6 (sub_0affa9,
 * wlc_phy_txpwrctrl_idle_tssi_meas_acphy): measure the idle TSSI of every core
 * and program it as the offset of the measurement.
 */
void bcm4360_phy_txpwrctrl_idle_tssi_meas_acphy(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	s16 buf[BCM4360_PHY_CORES_MAX];
	u16 s;
	u32 c;

	if (phy->hold & 0x021e)					/* step 1 */
		return;

	bcm4360_phy_stay_in_carriersearch(phy, true);		/* step 2 */
	bcm4360_phy_tssi_phy_setup_acphy(phy, 0);		/* step 3 */
	bcm4360_radio_tssi_setup(&phy->radio, phy->hw_rxchain, 0, !phy_is_2g(phy));	/* step 4 */

	s = bcm4360_phy_read(io, ACPHY_REG_0x401);		/* step 5 */
	bcm4360_phy_mod(io, ACPHY_REG_0x401, 0x0007, phy->hw_rxchain);		/* step 6 */
	bcm4360_phy_mod(io, ACPHY_REG_0x401, 0x7000, (u16)(phy->hw_rxchain << 12));

	for (c = 0; c < BCM4360_PHY_CORES_MAX; c++)
		buf[c] = 0;
	for (c = 0; c < phy_cores(phy); c++) {			/* step 7 */
		if (!(phy->hw_rxchain & (1u << c)))
			continue;
		phy_poll_samps_war(phy, buf, true, NULL, false, true, c);
		phy->txpwr.idle_tssi[c] = buf[c];
		bcm4360_phy_mod(io, core_reg(ACPHY_REG_0x645, c), 0x03ff, (u16)buf[c]);
	}

	bcm4360_phy_write(io, ACPHY_REG_0x401, s);		/* step 8 */
	bcm4360_phy_stay_in_carriersearch(phy, false);		/* step 9 */
}

/*
 * acphy-txpower.md, section 29 / acphy-init.md, section 7 (sub_0b1227,
 * wlc_phy_precal_txgain_acphy): choose the transmit gain with which the
 * transmit calibrations run (PHY revision 0 and 1: read one gain-table entry
 * per core). `records` = one 10 byte record per core. Only caller:
 * wlc_phy_cals_acphy (not on the init/channel path).
 */
void bcm4360_phy_precal_txgain_acphy(struct bcm4360_phy *phy, u8 *records)
{
	u8 band = bcm4360_phy_get_chan_freq_range(phy, 0);
	u32 c;

	/* step 1: the per-core "gain schedule loaded" flags (cal+0x65+c) */
	for (c = 0; c < phy_cores(phy); c++)
		phy->cal.state.core_flag[c] = 0;

	for (c = 0; c < phy_cores(phy); c++) {
		u8 i;

		if (phy->ver.phy_rev == ACPHY_REV_1) {
			if (band == 4)
				i = (c == 1) ? 30 : 20;		/* 20, 30, 20 */
			else
				i = (c == 0) ? 20 : 30;
		} else {
			i = 30;
		}
		phy_get_txgain_by_index(phy, (struct txgain_rec *)(records + 10 * c), (s8)i);
	}
}

/* ===================================================================
 * The MAC-facing transmit-power entry points
 * (docs/re/spec/acphy-api-txpower.md, "Entry points"): the per-rate limit
 * path - the SROM-11 decode, the board limit, the merge - and the small stores
 * and getters. The per-rate power structures (ppr_t) belong to the MAC; the
 * PHY only calls the bcm4360_ppr_* services on them (phy_env.h).
 * =================================================================== */

/* ppr bandwidth selectors (acphy-api-txpower.md, "The ppr structure is a service") */
#define PPR_BW_20		0
#define PPR_BW_40		1
#define PPR_BW_80		2

/* D11(0x120), the MAC control register: bit 0, the MAC is enabled (bmac-init.md) */
#define D11_MACCONTROL_EN_MAC	0x00000001

/* wlc_phy_txpower_set: the result for a qdbm of 0x80 and above */
#define TXPOWER_SET_BADARG	5
#define TXPOWER_QDBM_LIMIT	0x80

/* wlc_phy_txpower_get: the flag of the override form of the result */
#define TXPOWER_GET_OVERRIDE	0x40000000

/* wlc_phy_txpower_get_target_min: the start of the minimum search */
#define TXPOWER_TARGET_MIN_START	0x7f

/* wlc_phy_txpower_set: the hold flag that blocks the recalculation (a scan) */
#define TXPOWER_HOLD_SCAN	BCM4360_PHY_HOLD_SCAN

/* the percentage below which the merge scales the limits */
#define TXPOWER_PERCENT_FULL	100

/*
 * acphy-api-txpower.md, "sub_0b8ca4", step 1: the ppr bandwidth selector of a
 * chanspec (0x2000: 80 MHz, 0x1800: 40 MHz, anything else: 20 MHz).
 */
static u8 ppr_bw_sel(u16 chanspec)
{
	switch (chanspec & BCM4360_CHANSPEC_BW) {
	case BCM4360_CHANSPEC_BW_80:
		return PPR_BW_80;
	case BCM4360_CHANSPEC_BW_40:
		return PPR_BW_40;
	default:
		return PPR_BW_20;
	}
}

/*
 * The rate groups of a ppr (acphy-api-txpower.md, "Corrections and answers",
 * Q2): the selectors the open code passes to the bcm4360_ppr_* services.
 */
#define PPR_DSSS_RATES		4	/* 1, 2, 5.5, 11 Mbit/s */
#define PPR_OFDM_RATES		8	/* 6, 9, 12, 18, 24, 36, 48, 54 Mbit/s */
#define PPR_VHT_RATES		10	/* MCS 0 .. 9 */
#define PPR_RATE_DISABLED	(-128)	/* a disabled rate (0x80) */

/* the bandwidth variant of a rate group (first selector of every group) */
enum ppr_txbw {
	PPR_TXBW_20 = 0,
	PPR_TXBW_40 = 1,
	PPR_TXBW_80 = 2,
	PPR_TXBW_20IN40 = 3,
	PPR_TXBW_20IN80 = 4,
	PPR_TXBW_40IN80 = 5,
};

/* the mode selector of the OFDM and VHT groups */
#define PPR_MODE_NONE		0
#define PPR_MODE_STBC		1
#define PPR_MODE_CDD		2
#define PPR_MODE_TXBF		3

/* one VHT group the decode writes: streams, mode, chains; `no_hi`: MCS 8 and 9 disabled */
struct ppr_vht_grp {
	u8 nss;
	u8 mode;
	u8 chains;
	bool no_hi;
};

/*
 * acphy-api-txpower.md, Q3, sub_0b42df (wlc_phy_ppr_set_vht_all): the VHT
 * groups in the order of the calls; a group is written only if its chain
 * count is at most N. The TXBF groups whose stream count equals the chain
 * count get MCS 8 and 9 disabled.
 */
static const struct ppr_vht_grp srom11_vht_grps[] = {
	{ 1, PPR_MODE_NONE, 1, false },
	{ 1, PPR_MODE_CDD, 2, false },
	{ 2, PPR_MODE_STBC, 2, false },
	{ 2, PPR_MODE_NONE, 2, false },
	{ 1, PPR_MODE_TXBF, 2, false },
	{ 2, PPR_MODE_TXBF, 2, true },
	{ 1, PPR_MODE_CDD, 3, false },
	{ 2, PPR_MODE_STBC, 3, false },
	{ 2, PPR_MODE_NONE, 3, false },
	{ 3, PPR_MODE_NONE, 3, false },
	{ 1, PPR_MODE_TXBF, 3, false },
	{ 2, PPR_MODE_TXBF, 3, false },
	{ 3, PPR_MODE_TXBF, 3, true },
};

/*
 * The SROM-11 words the decode reads (acphy-api-txpower.md, Q3, "The words
 * read"; the copies of struct bcm4360_phy_srom_po). The 5 GHz MCS words exist
 * twice per bandwidth with the same value; OFDM8 is taken from the first copy,
 * VHT10 from the second.
 */
#define SROM11_2G_OFDM		0	/* po.ofdm_2g[0], pi+0xc50 */
#define SROM11_2G_BW20_MCS	0	/* po.mcs_2g[0], pi+0xc5c: mcsbw202gpo */
#define SROM11_2G_BW40_MCS	2	/* po.mcs_2g[2], pi+0xc64: mcsbw402gpo */
#define SROM11_5G_BW20_OFDM	0	/* po.mcs_5g[0][i], pi+0xc6c + 4i: mcsbw205g?po */
#define SROM11_5G_BW40_OFDM	3	/* po.mcs_5g[3][i], pi+0xc90 + 4i: mcsbw405g?po */
#define SROM11_5G_BW80_OFDM	5	/* po.mcs_5g[5][i], pi+0xca8 + 4i: mcsbw805g?po */
#define SROM11_5G_BW20_VHT	6	/* po.mcs_5g[6][i], pi+0xcb4 + 4i: mcsbw205g?po */
#define SROM11_5G_BW40_VHT	9	/* po.mcs_5g[9][i], pi+0xcd8 + 4i: mcsbw405g?po */
#define SROM11_5G_BW80_VHT	11	/* po.mcs_5g[11][i], pi+0xcf0 + 4i: mcsbw805g?po */

/*
 * HB: the two bits of MCS 8 and 9 in pi+0xd36 (2.4 GHz) or in
 * sb40and80hr5g[i] (5 GHz), by their shift; each bit takes 8 dB off.
 */
#define SROM11_HB_2G_BW20	12
#define SROM11_HB_2G_BW40	14
#define SROM11_HB_5G_BW20	4
#define SROM11_HB_5G_BW40	6
#define SROM11_HB_5G_BW80	8
#define SROM11_HB_MCS8		0x1
#define SROM11_HB_MCS9		0x2
#define SROM11_HB_STEP		32	/* 8 dB in quarter dBm */

/* the second adjustment of the outer 20 MHz channels of an 80 MHz block: bits 2..5 */
#define SROM11_SB20IN80_OUTER_SHIFT	2
#define SROM11_NIBBLE			0xf

/* the control sidebands of an 80 MHz chanspec that are outer channels (LL, UU) */
#define CHANSPEC_SB_LL		0x0000
#define CHANSPEC_SB_UU		0x0300

/* the rates of an OFDM / VHT group that the low adjustment of ADJ8 / ADJ10 covers */
#define SROM11_ADJ8_LO_RATES	6	/* 6 .. 36 Mbit/s */
#define SROM11_ADJ10_LO_RATES	5	/* MCS 0 .. 4 */

/* acphy-api-txpower.md, Q3, "Notation": n_k(w), bits 4k..4k+3 of w */
static u32 srom11_nib(u32 w, u32 k)
{
	return (w >> (4 * k)) & SROM11_NIBBLE;
}

/* acphy-api-txpower.md, Q3, "Notation": P(x) = maxpwr - 2x, a byte modulo 256 */
static s8 srom11_p(u8 maxpwr, u32 x)
{
	return (s8)(u8)(maxpwr - 2 * x);
}

/* acphy-api-txpower.md, Q3, "Notation": 2 * s4(x), as ((x + 8) & 15) * 2 - 16 */
static s32 srom11_s4x2(u32 x)
{
	return (s32)((x + 8) & SROM11_NIBBLE) * 2 - 16;
}

/* acphy-api-txpower.md, Q3, "Notation": a power byte plus d, modulo 256 */
static s8 srom11_add(s8 v, s32 d)
{
	return (s8)(u8)(v + d);
}

/*
 * acphy-api-txpower.md, Q3, sub_0b2716 (wlc_phy_txpwr_srom11_ofdm_rates):
 * OFDM8(w, x) = [P(n0), P(n0), Q, Q, P(n1), P(n2), P(n3), P(n4)] with
 * Q = P(n0) - 2 * s4(x).
 */
static void srom11_ofdm8(u32 w, u32 x, u8 maxpwr, s8 *r)
{
	s8 p0 = srom11_p(maxpwr, srom11_nib(w, 0));
	s8 q = srom11_add(p0, -srom11_s4x2(x));
	u32 k;

	r[0] = p0;					/* 6 Mbit/s */
	r[1] = p0;					/* 9 */
	r[2] = q;					/* 12 */
	r[3] = q;					/* 18 */
	for (k = 1; k <= 4; k++)			/* 24, 36, 48, 54 */
		r[3 + k] = srom11_p(maxpwr, srom11_nib(w, k));
}

/*
 * acphy-api-txpower.md, Q3, sub_0b2765 (wlc_phy_txpwr_srom11_vht_rates):
 * VHT10(w, x) = [P(n0), Q, Q, P(n1), .., P(n7)] with Q as in OFDM8.
 */
static void srom11_vht10(u32 w, u32 x, u8 maxpwr, s8 *r)
{
	s8 p0 = srom11_p(maxpwr, srom11_nib(w, 0));
	s8 q = srom11_add(p0, -srom11_s4x2(x));
	u32 k;

	r[0] = p0;					/* MCS 0 */
	r[1] = q;					/* MCS 1 */
	r[2] = q;					/* MCS 2 */
	for (k = 1; k <= 7; k++)			/* MCS 3 .. 9 */
		r[2 + k] = srom11_p(maxpwr, srom11_nib(w, k));
}

/*
 * acphy-api-txpower.md, Q3, sub_0b2690 (wlc_phy_txpwr_ofdm_offset_adj):
 * ADJ8(src, lo, hi) into `dst` (may be `src`): rates 0..5 + 2 * s4(lo),
 * rates 6, 7 + 2 * s4(hi).
 */
static void srom11_adj8(const s8 *src, u32 lo, u32 hi, s8 *dst)
{
	u32 k;

	for (k = 0; k < PPR_OFDM_RATES; k++)
		dst[k] = srom11_add(src[k], srom11_s4x2(k < SROM11_ADJ8_LO_RATES ? lo : hi));
}

/*
 * acphy-api-txpower.md, Q3, sub_0b26d3 (wlc_phy_txpwr_vht_offset_adj):
 * ADJ10(src, lo, hi) into `dst` (may be `src`): rates 0..4 + 2 * s4(lo),
 * rates 5..9 + 2 * s4(hi).
 */
static void srom11_adj10(const s8 *src, u32 lo, u32 hi, s8 *dst)
{
	u32 k;

	for (k = 0; k < PPR_VHT_RATES; k++)
		dst[k] = srom11_add(src[k], srom11_s4x2(k < SROM11_ADJ10_LO_RATES ? lo : hi));
}

/*
 * acphy-api-txpower.md, Q3, HB(v, f): with the board flag F (pi_ac+0x34d),
 * MCS 8 of `v` loses 8 dB when bit 0 of `f` is set, MCS 9 when bit 1 is.
 */
static void srom11_hb(s8 *v, u32 f, bool flag)
{
	if (!flag)
		return;
	if (f & SROM11_HB_MCS8)
		v[8] = srom11_add(v[8], -SROM11_HB_STEP);
	if (f & SROM11_HB_MCS9)
		v[9] = srom11_add(v[9], -SROM11_HB_STEP);
}

/* what the decode writes into: the ppr and the number of cores N (pi+0x168) */
struct srom11_out {
	void *ppr;
	u32 cores;
};

/* acphy-api-txpower.md, Q3, SET_DSSS(sel, buf): chains 1 .. N */
static void srom11_set_dsss(const struct srom11_out *o, u8 sel, const s8 *buf)
{
	u32 c;

	for (c = 1; c <= o->cores; c++)
		bcm4360_ppr_set_dsss(o->ppr, sel, (u8)c, buf);
}

/*
 * acphy-api-txpower.md, Q3, sub_0b3369 (wlc_phy_ppr_set_ofdm_all),
 * SET_OFDM(sel, buf): (mode, chains) = (none, 1), then (CDD, c) and (TXBF, c)
 * for c = 2 .. N.
 */
static void srom11_set_ofdm(const struct srom11_out *o, u8 sel, const s8 *buf)
{
	u32 c;

	bcm4360_ppr_set_ofdm(o->ppr, sel, PPR_MODE_NONE, 1, buf);
	for (c = 2; c <= o->cores; c++) {
		bcm4360_ppr_set_ofdm(o->ppr, sel, PPR_MODE_CDD, (u8)c, buf);
		bcm4360_ppr_set_ofdm(o->ppr, sel, PPR_MODE_TXBF, (u8)c, buf);
	}
}

/*
 * acphy-api-txpower.md, Q3, sub_0b42df (wlc_phy_ppr_set_vht_all),
 * SET_VHT(sel, buf): the groups of srom11_vht_grps up to N chains, some
 * with MCS 8 and 9 disabled (a copy; `buf` is left as it was).
 */
static void srom11_set_vht(const struct srom11_out *o, u8 sel, const s8 *buf)
{
	s8 no_hi[PPR_VHT_RATES];
	u32 i;

	for (i = 0; i < PPR_VHT_RATES; i++)
		no_hi[i] = buf[i];
	no_hi[8] = PPR_RATE_DISABLED;
	no_hi[9] = PPR_RATE_DISABLED;

	for (i = 0; i < ARRAY_SIZE(srom11_vht_grps); i++) {
		const struct ppr_vht_grp *g = &srom11_vht_grps[i];

		if (g->chains > o->cores)
			continue;
		bcm4360_ppr_set_vht_mcs(o->ppr, sel, g->nss, g->mode, g->chains,
					g->no_hi ? no_hi : buf);
	}
}

/* acphy-api-txpower.md, Q3: the four DSSS powers [P(n_k(w))], k = 0..3 */
static void srom11_dsss4(u32 w, u8 maxpwr, s8 *r)
{
	u32 k;

	for (k = 0; k < PPR_DSSS_RATES; k++)
		r[k] = srom11_p(maxpwr, srom11_nib(w, k));
}

/*
 * acphy-api-txpower.md, Q3, "2.4 GHz", 0x1800 (40 MHz): the groups of
 * selectors 1 and 3 from the 20 MHz groups OFDM20 and VHT20.
 */
static void srom11_decode_2g_40(struct bcm4360_phy *phy, const struct srom11_out *o, u8 maxpwr,
				const s8 *ofdm20, const s8 *vht20)
{
	const struct bcm4360_phy_srom_po *po = &phy->txpwr.po;
	u32 w = po->mcs_2g[SROM11_2G_BW40_MCS];
	u32 x = srom11_nib(po->ofdmlrbw202gpo, 3);
	u32 lo = srom11_nib(po->sb20in40lrpo, 0);
	u32 hi = srom11_nib(po->sb20in40hrpo, 0);
	s8 dsss40[PPR_DSSS_RATES], vht40[PPR_VHT_RATES], ofdm40[PPR_OFDM_RATES];
	s8 ofdm20in40[PPR_OFDM_RATES], vht20in40[PPR_VHT_RATES];

	srom11_dsss4(po->cckbw20ul2gpo, maxpwr, dsss40);
	srom11_vht10(w, x, maxpwr, vht40);
	srom11_hb(vht40, po->sb40and80hr5gpo[0] >> SROM11_HB_2G_BW40, phy->flags.bf3_bit11);
	srom11_ofdm8(w, x, maxpwr, ofdm40);
	srom11_adj8(ofdm40, srom11_nib(po->dot11agduplrpo, 0), srom11_nib(po->dot11agduphrpo, 0),
		    ofdm40);
	srom11_adj8(ofdm20, lo, hi, ofdm20in40);
	srom11_adj10(vht20, lo, hi, vht20in40);

	srom11_set_ofdm(o, PPR_TXBW_40, ofdm40);
	srom11_set_vht(o, PPR_TXBW_40, vht40);
	srom11_set_dsss(o, PPR_TXBW_20IN40, dsss40);
	srom11_set_ofdm(o, PPR_TXBW_20IN40, ofdm20in40);
	srom11_set_vht(o, PPR_TXBW_20IN40, vht20in40);
}

/* acphy-api-txpower.md, Q3, "2.4 GHz" (chanspec & 0xc000 = 0) */
static void srom11_decode_2g(struct bcm4360_phy *phy, const struct srom11_out *o, u16 chanspec,
			     u8 maxpwr)
{
	const struct bcm4360_phy_srom_po *po = &phy->txpwr.po;
	s8 ofdm20[PPR_OFDM_RATES], vht20[PPR_VHT_RATES], dsss20[PPR_DSSS_RATES];
	u32 k;

	/* always first */
	for (k = 0; k < PPR_OFDM_RATES; k++)
		ofdm20[k] = srom11_p(maxpwr, srom11_nib(po->ofdm_2g[SROM11_2G_OFDM], k));
	srom11_vht10(po->mcs_2g[SROM11_2G_BW20_MCS], srom11_nib(po->ofdmlrbw202gpo, 2), maxpwr,
		     vht20);
	srom11_hb(vht20, po->sb40and80hr5gpo[0] >> SROM11_HB_2G_BW20, phy->flags.bf3_bit11);

	switch (chanspec & BCM4360_CHANSPEC_BW) {
	case BCM4360_CHANSPEC_BW_20:
		srom11_dsss4(po->cckbw202gpo, maxpwr, dsss20);
		srom11_set_dsss(o, PPR_TXBW_20, dsss20);
		srom11_set_ofdm(o, PPR_TXBW_20, ofdm20);
		srom11_set_vht(o, PPR_TXBW_20, vht20);
		break;
	case BCM4360_CHANSPEC_BW_40:
		srom11_decode_2g_40(phy, o, maxpwr, ofdm20, vht20);
		break;
	default:
		break;
	}
}

/*
 * acphy-api-txpower.md, Q3, "5 GHz", 40 MHz: the groups of selectors 1 and 3;
 * `g` = group selects the nibble of the shared words.
 */
static void srom11_decode_5g_40(struct bcm4360_phy *phy, const struct srom11_out *o, u32 g,
				const s8 *ofdm20, const s8 *vht20, const s8 *ofdm40b,
				const s8 *vht40)
{
	const struct bcm4360_phy_srom_po *po = &phy->txpwr.po;
	u32 lo = srom11_nib(po->sb20in40lrpo, g);
	u32 hi = srom11_nib(po->sb20in40hrpo, g);
	s8 ofdm20in40[PPR_OFDM_RATES], vht20in40[PPR_VHT_RATES], ofdm40[PPR_OFDM_RATES];

	srom11_adj8(ofdm20, lo, hi, ofdm20in40);
	srom11_adj10(vht20, lo, hi, vht20in40);
	srom11_adj8(ofdm40b, srom11_nib(po->dot11agduplrpo, g), srom11_nib(po->dot11agduphrpo, g),
		    ofdm40);

	srom11_set_ofdm(o, PPR_TXBW_40, ofdm40);
	srom11_set_vht(o, PPR_TXBW_40, vht40);
	srom11_set_ofdm(o, PPR_TXBW_20IN40, ofdm20in40);
	srom11_set_vht(o, PPR_TXBW_20IN40, vht20in40);
}

/*
 * acphy-api-txpower.md, Q3, "5 GHz", 80 MHz: the groups of selectors 2, 4
 * and 5; `g` = group, i = g - 1 the low/mid/high word.
 */
static void srom11_decode_5g_80(struct bcm4360_phy *phy, const struct srom11_out *o,
				u16 chanspec, u32 g, u8 maxpwr, const s8 *ofdm20,
				const s8 *vht20, const s8 *ofdm40b, const s8 *vht40)
{
	const struct bcm4360_phy_srom_po *po = &phy->txpwr.po;
	u32 i = g - 1;
	u32 x = srom11_nib(po->mcslr5gpo[i], 2);
	u32 duplo = srom11_nib(po->dot11agduplrpo, g);
	u32 duphi = srom11_nib(po->dot11agduphrpo, g);
	u32 sblo = po->sb20in80and160lr5gpo[i];
	u32 sbhi = po->sb20in80and160hr5gpo[i];
	u16 sb = chanspec & BCM4360_CHANSPEC_CTL_SB;
	s8 vht80[PPR_VHT_RATES], ofdm80[PPR_OFDM_RATES], ofdm40in80[PPR_OFDM_RATES];
	s8 ofdm20in80[PPR_OFDM_RATES], vht20in80[PPR_VHT_RATES], vht40in80[PPR_VHT_RATES];

	srom11_vht10(po->mcs_5g[SROM11_5G_BW80_VHT][i], x, maxpwr, vht80);
	srom11_hb(vht80, po->sb40and80hr5gpo[i] >> SROM11_HB_5G_BW80, phy->flags.bf3_bit11);
	srom11_ofdm8(po->mcs_5g[SROM11_5G_BW80_OFDM][i], x, maxpwr, ofdm80);
	srom11_adj8(ofdm80, duplo, duphi, ofdm80);
	srom11_adj8(ofdm40b, duplo, duphi, ofdm40in80);

	srom11_adj8(ofdm20, srom11_nib(sblo, 0), srom11_nib(sbhi, 0), ofdm20in80);
	srom11_adj10(vht20, srom11_nib(sblo, 0), srom11_nib(sbhi, 0), vht20in80);
	if (sb == CHANSPEC_SB_LL || sb == CHANSPEC_SB_UU) {
		/* the outer 20 MHz channels: again, with bits 2..5 of the two words */
		u32 lo = (sblo >> SROM11_SB20IN80_OUTER_SHIFT) & SROM11_NIBBLE;
		u32 hi = (sbhi >> SROM11_SB20IN80_OUTER_SHIFT) & SROM11_NIBBLE;

		srom11_adj8(ofdm20in80, lo, hi, ofdm20in80);
		srom11_adj10(vht20in80, lo, hi, vht20in80);
	}
	srom11_adj10(vht40, srom11_nib(po->sb40and80lr5gpo[i], 0),
		     srom11_nib(po->sb40and80hr5gpo[i], 0), vht40in80);

	srom11_set_ofdm(o, PPR_TXBW_80, ofdm80);
	srom11_set_vht(o, PPR_TXBW_80, vht80);
	srom11_set_ofdm(o, PPR_TXBW_20IN80, ofdm20in80);
	srom11_set_vht(o, PPR_TXBW_20IN80, vht20in80);
	srom11_set_vht(o, PPR_TXBW_40IN80, vht40in80);
	srom11_set_ofdm(o, PPR_TXBW_40IN80, ofdm40in80);
}

/* acphy-api-txpower.md, Q3, "5 GHz" (chanspec & 0xc000 = 0xc000), group 1..3 */
static void srom11_decode_5g(struct bcm4360_phy *phy, const struct srom11_out *o, u16 chanspec,
			     u32 g, u8 maxpwr)
{
	const struct bcm4360_phy_srom_po *po = &phy->txpwr.po;
	u32 i = g - 1;
	u32 lr5 = po->mcslr5gpo[i];
	u32 hb = po->sb40and80hr5gpo[i];
	bool flag = phy->flags.bf3_bit11;
	u16 bw = chanspec & BCM4360_CHANSPEC_BW;
	s8 ofdm20[PPR_OFDM_RATES], vht20[PPR_VHT_RATES];
	s8 ofdm40b[PPR_OFDM_RATES], vht40[PPR_VHT_RATES];

	/* always first */
	srom11_ofdm8(po->mcs_5g[SROM11_5G_BW20_OFDM][i], srom11_nib(lr5, 0), maxpwr, ofdm20);
	srom11_vht10(po->mcs_5g[SROM11_5G_BW20_VHT][i], srom11_nib(lr5, 0), maxpwr, vht20);
	srom11_hb(vht20, hb >> SROM11_HB_5G_BW20, flag);

	if (bw == BCM4360_CHANSPEC_BW_20) {
		srom11_set_ofdm(o, PPR_TXBW_20, ofdm20);
		srom11_set_vht(o, PPR_TXBW_20, vht20);
		return;
	}
	if (bw != BCM4360_CHANSPEC_BW_40 && bw != BCM4360_CHANSPEC_BW_80)
		return;

	/* 40 or 80 MHz, first */
	srom11_vht10(po->mcs_5g[SROM11_5G_BW40_VHT][i], srom11_nib(lr5, 1), maxpwr, vht40);
	srom11_hb(vht40, hb >> SROM11_HB_5G_BW40, flag);
	srom11_ofdm8(po->mcs_5g[SROM11_5G_BW40_OFDM][i], srom11_nib(lr5, 1), maxpwr, ofdm40b);

	if (bw == BCM4360_CHANSPEC_BW_40)
		srom11_decode_5g_40(phy, o, g, ofdm20, vht20, ofdm40b, vht40);
	else
		srom11_decode_5g_80(phy, o, chanspec, g, maxpwr, ofdm20, vht20, ofdm40b, vht40);
}

/*
 * acphy-api-txpower.md, "Corrections and answers", Q3
 * (wlc_phy_txpwr_apply_srom11): fill `ppr` with the per-rate power limit
 * decoded from the SROM-11 power offsets for `chanspec`, `maxpwr` minus the
 * offsets. Only the selectors of the chanspec's bandwidth and the chain
 * counts up to N are written; every other group keeps what the ppr held.
 * `group` (0 = 2.4 GHz, 1..3 = 5 GHz low, mid, high) selects the 5 GHz word.
 * AC-PHY only (pi+0x160 = 11, always so here).
 */
static void phy_txpwr_apply_srom11(struct bcm4360_phy *phy, u8 group, u16 chanspec, u8 maxpwr,
				   void *ppr)
{
	struct srom11_out o = { ppr, phy_cores(phy) };

	switch (chanspec & BCM4360_CHANSPEC_BAND) {
	case BCM4360_CHANSPEC_BAND_2G:
		srom11_decode_2g(phy, &o, chanspec, maxpwr);
		break;
	case BCM4360_CHANSPEC_BAND_5G:
		if (group >= 1 && group <= BCM4360_PHY_PO_RANGES_5G)
			srom11_decode_5g(phy, &o, chanspec, group, maxpwr);
		break;
	default:
		break;
	}
}

/*
 * acphy-txpower.md, section 19 (wlc_phy_txpower_sromlimit_get_acphy), as
 * corrected in acphy-api-txpower.md, Q3, "The wrapper": the highest power the
 * board allows for every rate on `chanspec`, for core `core`, into `out` (made
 * by the caller for the bandwidth of the chanspec).
 */
static void phy_txpower_sromlimit_get(struct bcm4360_phy *phy, u16 chanspec, void *out, u8 core)
{
	const struct bcm4360_phy_txpwr *tx = &phy->txpwr;
	u8 band, m, g, maxp;
	s8 d;
	u32 c;

	band = bcm4360_phy_get_chan_freq_range(phy, chanspec & BCM4360_CHANSPEC_CHANNEL); /* 1 */
	if (band >= BCM4360_PHY_BANDS)
		return;

	/* step 2: maxpwr, the smallest maxp of cores 0, 1, 2 (unsigned), whatever N is */
	m = tx->maxp[0][band];
	for (c = 1; c < BCM4360_PHY_CORES_MAX; c++)
		if (tx->maxp[c][band] < m)
			m = tx->maxp[c][band];

	g = (band < 4) ? band : band - 1;			/* step 3: r - 1 + (r < 4) */
	phy_txpwr_apply_srom11(phy, g, chanspec, m, out);		/* step 4 */

	maxp = (core < BCM4360_PHY_CORES_MAX) ? tx->maxp[core][band] : 0;
	d = (s8)(maxp - m);						/* step 5 */
	if (d > 0)
		bcm4360_ppr_plus_cmn_val(out, d);
	bcm4360_ppr_apply_max(out, (s8)maxp);				/* step 6: a ceiling */
}

/*
 * acphy-api-txpower.md, "Entry points" (wlc_phy_txpower_sromlimit): the
 * lowest power allowed (quarter dBm) into `min_pwr` and the board limit per
 * rate of core `core` into `max_pwr`; either may be NULL. The merge calls this
 * one directly.
 */
static void phy_txpower_sromlimit(struct bcm4360_phy *phy, u16 chanspec, s8 *min_pwr,
				  void *max_pwr, u8 core)
{
	if (min_pwr)
		*min_pwr = (s8)(u8)(phy->txpwr.min_power << 2);
	if (max_pwr)
		phy_txpower_sromlimit_get(phy, chanspec, max_pwr, core);
}

/* acphy-api-txpower.md, "Entry points": wlc_phy_txpower_sromlimit, for the MAC */
void bcm4360_phy_txpower_sromlimit(struct bcm4360_phy *phy, u16 chanspec, s8 *min_pwr,
				   void *max_pwr, u8 core)
{
	phy_txpower_sromlimit(phy, chanspec, min_pwr, max_pwr, core);
}

/*
 * acphy-api-txpower.md, "Corrections and answers", Q4 (step 3 of the merge):
 * the offsets ppr pi+0x1c8. Deleted if it was made for another bandwidth
 * selector than `bw`, made (again) if there is none, and then cleared on every
 * merge. False if it cannot be made.
 */
static bool phy_txpower_offsets_ppr(struct bcm4360_phy *phy, u8 bw)
{
	struct bcm4360_phy_txpwr *tx = &phy->txpwr;

	if (tx->ppr && bcm4360_ppr_get_ch_bw(tx->ppr) != bw) {
		bcm4360_ppr_delete(phy->hw, tx->ppr);
		tx->ppr = NULL;
	}
	if (!tx->ppr) {
		tx->ppr = bcm4360_ppr_create(phy->hw, bw);
		if (!tx->ppr)
			return false;
	}
	bcm4360_ppr_clear(tx->ppr);
	return true;
}

/*
 * acphy-api-txpower.md, "Further corrections to sub_0b8ca4", step 5: the
 * limits and targets of core `core` on `chanspec`. `l` is the regulatory
 * limit, `a` and `b` the two working objects, `min` the minimum power byte
 * (set to 0 once by the caller, overwritten by every sromlimit call).
 */
static void phy_txpower_merge_core(struct bcm4360_phy *phy, u16 chanspec, u32 core, void *l,
				   void *a, void *b, void *reg_ppr, s8 *min)
{
	struct bcm4360_phy_txpwr *tx = &phy->txpwr;
	s8 m;

	bcm4360_ppr_set_cmn_val(a, (s8)tx->user_target);			/* 5.1 */
	phy_txpower_sromlimit(phy, chanspec, min, b, (u8)core);			/* 5.2 */
	bcm4360_ppr_compare_min(b, l);						/* 5.3 */
	bcm4360_ppr_minus_cmn_val(b, (s8)tx->txpwrbckof);			/* 5.4 */
	bcm4360_ppr_compare_min(a, b);						/* 5.5 */
	if (tx->percent < TXPOWER_PERCENT_FULL)					/* 5.6 */
		bcm4360_ppr_multiply_percentage(a, tx->percent);
	bcm4360_ppr_force_disabled(a, *min);					/* 5.7 */

	tx->target_max[core] = bcm4360_ppr_get_max(a);				/* 5.8 */
	m = bcm4360_ppr_get_min(a, *min);
	tx->target_min[core] = m;
	tx->ovr_value = m;
	tx->ovr_neg = 0;

	if (core == 0) {							/* 5.9 */
		tx->pi_0x21e = 0;
		if (reg_ppr)
			bcm4360_ppr_copy(a, reg_ppr);
	}

	bcm4360_ppr_cmn_val_minus(a, tx->target_max[core]);			/* 5.10 */
	bcm4360_ppr_compare_max(tx->ppr, a);					/* 5.11 */
}

/*
 * acphy-api-txpower.md, "sub_0b8ca4: the per-rate limit merge", in the order
 * of "Further corrections to sub_0b8ca4": the per-rate limits the PHY
 * enforces and the targets of every core, from the regulatory limit `in_ppr`
 * (may be NULL) and the board limit. `reg_ppr` (may be NULL) gets the limited
 * set of core 0; without it the targets are applied (the callback pi+0x40,
 * wlc_phy_txpower_recalc_target_acphy).
 */
static void phy_txpower_merge(struct bcm4360_phy *phy, void *in_ppr, void *reg_ppr)
{
	u16 chanspec = phy->radio_chanspec;
	u8 bw = ppr_bw_sel(chanspec);
	void *l, *a, *b;
	s8 min = 0;
	u32 c;

	l = bcm4360_ppr_create(phy->hw, bw);					/* step 1 */
	if (!l)
		return;
	if (in_ppr)
		bcm4360_ppr_copy(in_ppr, l);

	a = bcm4360_ppr_create(phy->hw, bw);					/* step 2 */
	if (!a) {
		bcm4360_ppr_delete(phy->hw, l);
		return;
	}
	b = bcm4360_ppr_create(phy->hw, bw);
	if (!b) {
		bcm4360_ppr_delete(phy->hw, l);
		bcm4360_ppr_delete(phy->hw, a);
		return;
	}

	if (!phy_txpower_offsets_ppr(phy, bw)) {				/* step 3 */
		bcm4360_ppr_delete(phy->hw, l);
		bcm4360_ppr_delete(phy->hw, a);
		bcm4360_ppr_delete(phy->hw, b);
		return;
	}

	/* step 4: the channel-table value cR is used by the LCN PHYs only */

	for (c = 0; c < phy_cores(phy); c++)					/* step 5 */
		phy_txpower_merge_core(phy, chanspec, c, l, a, b, reg_ppr, &min);

	bcm4360_ppr_delete(phy->hw, l);						/* step 6 */
	bcm4360_ppr_delete(phy->hw, a);
	bcm4360_ppr_delete(phy->hw, b);
	if (reg_ppr)
		return;
	bcm4360_phy_txpower_recalc_target_acphy(phy);
}

/*
 * acphy-api-txpower.md, "Corrections and answers", Q5 (sub_0b72a4,
 * wlc_phy_cal_txpower_recalc_sw, the transmit-power recalculation tick called
 * by wlc_phy_txpower_limit_set, wlc_phy_txpower_set and the watchdog step 12):
 * for the AC-PHY (pi+0x160 = 11) it returns false at once, without an access,
 * a service call or a change of state.
 */
bool bcm4360_phy_txpower_recalc_tick(struct bcm4360_phy *phy)
{
	(void)phy;
	return false;
}

/* acphy-api-txpower.md, "Entry points": wlc_phy_txpower_limit_set */
void bcm4360_phy_txpower_limit_set(struct bcm4360_phy *phy, void *txpwr)
{
	bcm4360_mac_suspend(phy->hw);
	phy_txpower_merge(phy, txpwr, NULL);
	bcm4360_phy_txpower_recalc_tick(phy);
	bcm4360_mac_enable(phy->hw);
}

/* acphy-api-txpower.md, "Entry points": wlc_phy_txpower_set */
s32 bcm4360_phy_txpower_set(struct bcm4360_phy *phy, u32 qdbm, bool override, void *reg_pwr)
{
	struct bcm4360_phy_txpwr *tx = &phy->txpwr;
	bool suspend, hwctrl;

	(void)override;
	if (qdbm >= TXPOWER_QDBM_LIMIT)
		return TXPOWER_SET_BADARG;

	tx->user_target = (u8)qdbm;
	tx->ovr_got = 0;
	if (!phy->up || (phy->hold & TXPOWER_HOLD_SCAN))
		return 0;

	suspend = d11_read32(phy->hw, D11_REG_MACCONTROL) & D11_MACCONTROL_EN_MAC;
	if (suspend)
		bcm4360_mac_suspend(phy->hw);
	hwctrl = tx->hwpwrctrl;
	bcm4360_phy_txpwrctrl_enable_acphy(phy, false);
	phy_txpower_merge(phy, reg_pwr, NULL);
	bcm4360_phy_txpower_recalc_tick(phy);
	bcm4360_phy_txpwrctrl_enable_acphy(phy, hwctrl);
	if (suspend)
		bcm4360_mac_enable(phy->hw);
	return 0;
}

/* acphy-api-txpower.md, "Entry points": wlc_phy_txpower_get */
s32 bcm4360_phy_txpower_get(struct bcm4360_phy *phy, u32 *qdbm, u8 *override)
{
	const struct bcm4360_phy_txpwr *tx = &phy->txpwr;

	*qdbm = tx->user_target;
	if (tx->ovr_active)
		*qdbm = (u32)(-(s32)tx->ovr_value) | TXPOWER_GET_OVERRIDE;
	if (override)
		*override = tx->ovr_got;
	return 0;
}

/*
 * acphy-api-txpower.md, "Entry points": wlc_phy_txpower_get_target_max, the
 * highest target over the cores of the mask sh+0xa7.
 */
u8 bcm4360_phy_txpower_get_target_max(struct bcm4360_phy *phy)
{
	u8 m = 0;
	u32 c;

	for (c = 0; c < phy_cores(phy); c++)
		if ((phy->rxchain >> c) & 1)
			if ((u8)phy->txpwr.target_max[c] > m)
				m = (u8)phy->txpwr.target_max[c];
	return m;
}

/*
 * acphy-api-txpower.md, "Entry points": wlc_phy_txpower_get_target_min, the
 * lowest target over the cores of the mask sh+0xa7, from 0x7f.
 */
u8 bcm4360_phy_txpower_get_target_min(struct bcm4360_phy *phy)
{
	u8 m = TXPOWER_TARGET_MIN_START;
	u32 c;

	for (c = 0; c < phy_cores(phy); c++)
		if ((phy->rxchain >> c) & 1)
			if ((u8)phy->txpwr.target_min[c] < m)
				m = (u8)phy->txpwr.target_min[c];
	return m;
}

/* acphy-api-txpower.md, "Entry points": wlc_phy_txpower_hw_ctrl_get, pi+0xfa0 */
bool bcm4360_phy_txpower_hw_ctrl_get(struct bcm4360_phy *phy)
{
	return phy->txpwr.hwpwrctrl;
}

/* acphy-api-txpower.md, "Entry points": wlc_phy_txpower_ipa_ison, 0 for the AC-PHY */
bool bcm4360_phy_txpower_ipa_ison(struct bcm4360_phy *phy)
{
	(void)phy;
	return false;
}

/* acphy-api-txpower.md, "Entry points": wlc_phy_txpwr_percent_set, pi+0x184 */
void bcm4360_phy_txpwr_percent_set(struct bcm4360_phy *phy, u8 percent)
{
	phy->txpwr.percent = percent;
}

/*
 * acphy-api-txpower.md, "Entry points": wlc_phy_txpower_core_offset_set, the
 * AC function through its slot (acphy-txpower.md, section 18).
 */
s32 bcm4360_phy_txpower_core_offset_set(struct bcm4360_phy *phy, const void *offsets)
{
	return bcm4360_phy_txpower_core_offset_set_acphy(phy, offsets);
}

/*
 * acphy-api-txpower.md, Q6 (wlc_phy_get_est_pout): the estimated output power
 * per core from the control status (acphy-txpower.md, section 12): est_pout
 * from PHY(0x642 + 0x200c), est_pout_adj from PHY(0x640 + 0x200c), with the
 * MAC suspended and inside the phyreg bracket. est_pout_cck is cleared first;
 * the object writes one byte there.
 */
void bcm4360_phy_get_est_pout(struct bcm4360_phy *phy, u8 *est_pout, u8 *est_pout_adj,
			      u16 *est_pout_cck)
{
	if (est_pout_cck)
		*(u8 *)est_pout_cck = 0;
	bcm4360_mac_suspend(phy->hw);
	phy_reg_enter(phy);
	phy_txpwr_est_pwr(phy, est_pout, est_pout_adj);
	phy_reg_exit(phy);
	bcm4360_mac_enable(phy->hw);
}

/*
 * acphy-api-txpower.md, "Entry points": wlc_phy_get_olpc_pwroffset, the
 * byte pi_ac+0x8de, 0x8df, 0x8e0 for the bandwidth 20, 40, 80 MHz.
 */
s8 bcm4360_phy_get_olpc_pwroffset(struct bcm4360_phy *phy, u16 bw)
{
	switch (bw) {
	case BCM4360_CHANSPEC_BW_20:
		return (s8)phy->txpwr.olpc_pwroffset[0];
	case BCM4360_CHANSPEC_BW_40:
		return (s8)phy->txpwr.olpc_pwroffset[1];
	case BCM4360_CHANSPEC_BW_80:
		return (s8)phy->txpwr.olpc_pwroffset[2];
	default:
		return 0;
	}
}

/*
 * acphy-api-txpower.md, "Entry points": wlc_phy_tssivisible_thresh, the AC
 * function (acphy-txpower.md, section 16).
 */
s32 bcm4360_phy_tssivisible_thresh(struct bcm4360_phy *phy)
{
	return bcm4360_phy_tssivisible_thresh_acphy(phy);
}

/* acphy-api-txpower.md, "Entry points": wlc_phy_clear_tssi, no effect on the AC-PHY */
void bcm4360_phy_clear_tssi(struct bcm4360_phy *phy)
{
	(void)phy;
}
