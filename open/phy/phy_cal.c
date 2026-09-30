// SPDX-License-Identifier: ISC
/*
 * AC-PHY of the BCM4360: the calibrations. The calibration scheduler and phase
 * machine (wlc_phy_cals_acphy), the tone / sample player, the transmit IQ/LO
 * calibration, the receive IQ calibration, IQ power estimation, temperature
 * sense with its gain/throttle consumers, and RSSI compute.
 *
 * Written from docs/re/spec/acphy-cal-tx.md and docs/re/spec/acphy-cal-rx.md (a
 * "section" or "Hn" in the comments is a section of the named specification),
 * docs/re/spec/acphy-txpower.md, docs/re/spec/acphy-radio.md and, for the
 * register access, docs/re/spec/access.md; for chip 0x4360, AC-PHY revision 0
 * or 1 and radio 2069 major revision 0 (revisions 3 or 4), two cores.
 *
 * Measurements read 0 in the model, so the compensation coefficients compute to
 * zero and some polls run to their bounded time-out; that is expected. What the
 * comparison test checks is the deterministic sequence of commands and register
 * writes.
 *
 * The notation PHY(a), RADIO(a), TBL(id)[i], SHM(o), D11(o) and mod() in the
 * comments is the one of access.md.
 *
 * Some small helpers this code needs (set/get bbmult, the cal-coefficient
 * table accessor sub_09bf99 for TBL(0x0c), the gain pulse sub_092ffa, the
 * gpiosel/sample-setup sub_093ebe, the adc-read save/restore sub_09bbe4/
 * sub_09be13, the classifier H1, the RF-sequencer trigger H6 and the phyreg
 * enter/exit H13) are `static` in the finished files phy_txpower.c / phy_init.c
 * and cannot be called from here; they are reimplemented as file-local statics
 * so they make the identical accesses (see docs/re/questions/phy-cal.md).
 */
#include <bcm4360/phy.h>
#include <bcm4360/phy_env.h>

/* --------------------------------------------------------------- registers */

/* PHY registers named by their address (purpose unknown unless noted). The
 * per-core registers 0x720, 0x721, 0x725, 0x728, 0x729, 0x739, 0x73a, 0x73e are
 * in radio2069.h; core c adds c * ACPHY_CORE_STEP. */
#define ACPHY_REG_0x012		0x012	/* debug output I */
#define ACPHY_REG_0x013		0x013	/* debug output Q / ADC readout */
#define ACPHY_REG_0x140		0x140	/* classifier enables (H1) */
#define ACPHY_REG_0x160		0x160	/* mask of receive cores */
#define ACPHY_REG_0x19e		0x19e	/* table-access / sequencer bits */
#define ACPHY_REG_0x210		0x210	/* rx digital-filter mux (rxcal) */
#define ACPHY_REG_0x211		0x211	/* rx digital-filter enable (rxcal) */
#define ACPHY_REG_0x212		0x212	/* rx digital-filter (rxcal) */
#define ACPHY_REG_0x270		0x270	/* rx IQ estimate: start/settle */
#define ACPHY_REG_0x271		0x271	/* rx IQ estimate: wait */
#define ACPHY_REG_0x272		0x272	/* rx IQ estimate: sample count */
#define ACPHY_REG_0x339		0x339	/* carrier-search threshold */
#define ACPHY_REG_0x380		0x380	/* cal engine: command / busy */
#define ACPHY_REG_0x381		0x381	/* cal engine: loopback gain */
#define ACPHY_REG_0x382		0x382	/* cal engine mode / tone start-while-deaf */
#define ACPHY_REG_0x383		0x383	/* cal engine: measurement length */
#define ACPHY_REG_0x392		0x392	/* debug select */
#define ACPHY_REG_0x393		0x393	/* debug select */
#define ACPHY_REG_0x394		0x394	/* debug select */
#define ACPHY_REG_0x400		0x400	/* RF sequencer mode */
#define ACPHY_REG_0x401		0x401	/* RF sequencer core activation */
#define ACPHY_REG_0x402		0x402	/* RF sequencer trigger */
#define ACPHY_REG_0x403		0x403	/* RF sequencer / sample-player status */
#define ACPHY_REG_0x40f		0x40f	/* saved around the ADC read */
#define ACPHY_REG_0x460		0x460	/* sample player start / stop */
#define ACPHY_REG_0x461		0x461	/* sample player loop count */
#define ACPHY_REG_0x462		0x462	/* sample player timing */
#define ACPHY_REG_0x463		0x463	/* sample player last-sample index */
#define ACPHY_REG_0x464		0x464	/* sample player / tone status */
#define ACPHY_REG_0x471		0x471	/* RF-sequencer tone generator */
#define ACPHY_REG_0x678		0x678	/* rxcal save */
#define ACPHY_REG_0x6a0		0x6a0	/* rx IQ compensation a */
#define ACPHY_REG_0x6a1		0x6a1	/* rx IQ compensation b */
#define ACPHY_REG_0x6c0		0x6c0	/* rx IQ estimate: I*Q low */
#define ACPHY_REG_0x6c1		0x6c1	/* rx IQ estimate: I*Q high */
#define ACPHY_REG_0x6c2		0x6c2	/* rx IQ estimate: I^2 low */
#define ACPHY_REG_0x6c3		0x6c3	/* rx IQ estimate: I^2 high */
#define ACPHY_REG_0x6c4		0x6c4	/* rx IQ estimate: Q^2 low */
#define ACPHY_REG_0x6c5		0x6c5	/* rx IQ estimate: Q^2 high */
#define ACPHY_REG_0x6dc		0x6dc	/* rxcal coarse gain seed */
#define ACPHY_REG_0x722		0x722	/* tx-gain override enables */
#define ACPHY_REG_0x723		0x723	/* loopback override */
#define ACPHY_REG_0x724		0x724	/* loopback override */
#define ACPHY_REG_0x727		0x727	/* TSSI / loopback override */
#define ACPHY_REG_0x730		0x730	/* rxcal gain triple */
#define ACPHY_REG_0x731		0x731	/* rxcal gain triple */
#define ACPHY_REG_0x734		0x734	/* filter gain */
#define ACPHY_REG_0x735		0x735	/* loopback bandwidth / gain */
#define ACPHY_REG_0x736		0x736	/* loopback path */
#define ACPHY_REG_0x737		0x737	/* rxcal gain */
#define ACPHY_REG_0x738		0x738	/* loopback bandwidth */
#define ACPHY_REG_0x73c		0x73c	/* TSSI input select / loopback */
#define ACPHY_REG_0x747		0x747	/* digital gain */
#define ACPHY_REG_0x93e		0x93e	/* PA-LDO (tempsense, pi_ac+0x8e1 path) */

/* the 802.11 core registers the debug-output select touches directly */
#define D11_REG_MACCONTROL	0x120
#define D11_REG_0x49e		0x49e

/* radio registers named by their address; core c adds c << R2069_BANK_SHIFT.
 * R2069_REG_0x017, 0x01a, 0x01f, 0x144, 0x15f, 0x170 are in radio2069.h. */
#define R2069_REG_0x002		0x002	/* LO leakage e/f, I and Q at 0x002..0x005 */
#define R2069_REG_0x00e		0x00e	/* tempsense phase */
#define R2069_REG_0x015		0x015	/* tempsense */
#define R2069_REG_0x01b		0x01b	/* txcal radio save */
#define R2069_REG_0x01c		0x01c	/* txcal radio save */
#define R2069_REG_0x01e		0x01e	/* txcal radio band */
#define R2069_REG_0x020		0x020	/* rxcal radio save */
#define R2069_REG_0x021		0x021	/* rxcal radio save */
#define R2069_REG_0x022		0x022	/* rxcal radio save */
#define R2069_REG_0x023		0x023	/* rxcal radio save */
#define R2069_REG_0x024		0x024	/* txcal/tempsense radio save */
#define R2069_REG_0x025		0x025	/* tempsense radio save */
#define R2069_REG_0x03a		0x03a	/* rxcal radio save */
#define R2069_REG_0x03d		0x03d	/* rxcal radio save */
#define R2069_REG_0x161		0x161	/* tempsense */
#define R2069_REG_0x166		0x166	/* tempsense */
#define R2069_REG_0x16e		0x16e	/* tempsense */

/* PHY tables (access.md, "Data") */
#define TBL_SAMPLES		0x0e	/* tone sample table (32 bit) */
#define TBL_BBMULT		0x0c	/* bbmult and cal coefficients (16 bit) */
#define TBL_GAINCODE		0x07	/* gain code in use (16 bit) */
#define TBL_LOFT_CORE0		0x42	/* LO-feedthrough compensation; +0x20 per core */

/* TBL(0x07) offsets */
#define TBL_GAINCODE_LO		0x100	/* [0x100 + core] */
#define TBL_GAINCODE_MID	0x103	/* [0x103 + core] */
#define TBL_GAINCODE_HI		0x106	/* [0x106 + core] */

/* TBL(0x0c) offsets (bbmult, sub_09c4e4) */
#define TBL_BBMULT_A		0x63	/* OFDM copy, + 4 * core */
#define TBL_BBMULT_B		0x73	/* 11b copy, + 4 * core */

/* the calibration marker written to TBL(0x0c)[0x5f] (acphy-cal-tx section 1) */
#define CAL_MARKER		0xacdc
#define TBL_BBMULT_MARKER	0x5f

/* the value that arms the calibration engine, PHY(0x382) (section 15) */
#define CAL_ENGINE_ARM		0x8a09

/* --------------------------------------------------------------- helpers */

/* the number of cores N of the PHY (pi+0x168) */
static u32 phy_cores(const struct bcm4360_phy *phy)
{
	return phy->ver.cores;
}

/* per-core register base: core c uses address + c * ACPHY_CORE_STEP */
static u16 core_reg(u16 base, u32 core)
{
	return (u16)(base + core * ACPHY_CORE_STEP);
}

/* the current chanspec bandwidth bits (pi+0x17e & 0x3800) */
static u16 phy_bw(const struct bcm4360_phy *phy)
{
	return phy->radio_chanspec & BCM4360_CHANSPEC_BW;
}

/* bandwidth index 0/1/2 for 20/40/80 MHz */
static u32 phy_bwix(const struct bcm4360_phy *phy)
{
	if (phy_bw(phy) == BCM4360_CHANSPEC_BW_80)
		return 2;
	if (phy_bw(phy) == BCM4360_CHANSPEC_BW_40)
		return 1;
	return 0;
}

/* number of tone samples ns = 20/40/80 for 20/40/80 MHz (section 12) */
static u32 phy_tone_ns(const struct bcm4360_phy *phy)
{
	if (phy_bw(phy) == BCM4360_CHANSPEC_BW_80)
		return 80;
	if (phy_bw(phy) == BCM4360_CHANSPEC_BW_40)
		return 40;
	return 20;
}

/* docs/re/spec/access.md, "PHY tables": one 16 bit entry */
static void tbl_write16(struct bcm4360_phy_io *io, u32 id, u32 offset, u16 v)
{
	bcm4360_tbl_write(io, id, 1, offset, 16, &v);
}

static u16 tbl_read16(struct bcm4360_phy_io *io, u32 id, u32 offset)
{
	u16 v = 0;

	bcm4360_tbl_read(io, id, 1, offset, 16, &v);
	return v;
}

/* acphy-txpower.md "Table access bracket": save PHY(0x19e) bit 1, set it, restore */
static u16 tbl_bracket_enter(struct bcm4360_phy_io *io)
{
	u16 s = bcm4360_phy_read(io, ACPHY_REG_0x19e);

	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);
	return s;
}

static void tbl_bracket_leave(struct bcm4360_phy_io *io, u16 saved)
{
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, saved & 0x0002);
}

/* acphy-init.md H13 (wlc_phyreg_enter/exit): nesting counter; first enter takes
 * the ucode wake override, last exit releases it. */
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

/* acphy-init.md H1 (wlc_phy_classifier_acphy): set bits of the classifier
 * enable register with a full register write; returns the new value. */
static u16 phy_classifier(struct bcm4360_phy *phy, u16 mask, u16 value)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 n = (bcm4360_phy_read(io, ACPHY_REG_0x140) & ~mask) | (value & mask);

	bcm4360_phy_write(io, ACPHY_REG_0x140, n);
	return n;
}

/* acphy-init.md H6 (wlc_phy_force_rfseq_acphy): trigger an RF sequence and wait */
static void phy_force_rfseq(struct bcm4360_phy *phy, u8 cmd)
{
	static const u16 trigger[] = { 0x0001, 0x0002, 0x0020, 0x0004, 0x0008, 0x0010 };
	struct bcm4360_phy_io *io = &phy->io;
	u16 bit, s400, s19e, v;
	u32 i;

	if (cmd >= ARRAY_SIZE(trigger))
		return;
	bit = trigger[cmd];

	s400 = bcm4360_phy_read(io, ACPHY_REG_0x400);
	s19e = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0001, 0x0001);
	bcm4360_phy_or(io, ACPHY_REG_0x400, 0x0003);
	bcm4360_phy_or(io, ACPHY_REG_0x402, bit);

	v = bcm4360_phy_read(io, ACPHY_REG_0x403);
	for (i = 0; (v & bit) && i < 20000; i++) {
		hw_udelay(10);
		v = bcm4360_phy_read(io, ACPHY_REG_0x403);
	}

	bcm4360_phy_write(io, ACPHY_REG_0x400, s400);
	bcm4360_phy_write(io, ACPHY_REG_0x19e, s19e);
}

/* acphy-txpower.md section 2 (sub_09c4e4): set the bbmult of one core */
static void phy_set_bbmult(struct bcm4360_phy *phy, u16 m, u32 core)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 saved = tbl_bracket_enter(io);

	tbl_write16(io, TBL_BBMULT, TBL_BBMULT_A + 4 * core, m);
	tbl_write16(io, TBL_BBMULT, TBL_BBMULT_B + 4 * core, m);
	tbl_bracket_leave(io, saved);
}

/* acphy-txpower.md section 3 (sub_098751): read the bbmult of one core */
static u16 phy_get_bbmult(struct bcm4360_phy *phy, u32 core)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 saved = tbl_bracket_enter(io);
	u16 m = tbl_read16(io, TBL_BBMULT, TBL_BBMULT_A + 4 * core);

	tbl_bracket_leave(io, saved);
	return m;
}

/*
 * acphy-txpower.md section 21 and acphy-cal-tx sections 15/20 (sub_09bf99,
 * wlc_phy_cal_txiqlo_coeffs_acphy): read/write the calibration coefficients.
 * Selections 0..11 are the PHY table TBL(0x0c); 12..19 the in-memory cal state
 * block (intermediate 12..15, final 16..19); mode 0 reads, else writes. mode 2
 * writes a second copy of the IQ coefficients.
 */
struct txiqlo_sel {
	u8 n;	/* number of 16 bit values */
	u8 o;	/* first offset in TBL(0x0c) */
	u8 d;	/* distance between cores */
};

static const struct txiqlo_sel txiqlo_sels[12] = {
	{ 2, 0x40, 8 }, { 1, 0x43, 8 }, { 1, 0x44, 8 }, { 1, 0x45, 8 },
	{ 2, 0x80, 7 }, { 1, 0x83, 7 }, { 1, 0x84, 7 }, { 1, 0x85, 7 },
	{ 2, 0x60, 4 }, { 1, 0x62, 4 }, { 2, 0x70, 4 }, { 1, 0x72, 4 },
};

/* index of the a/b/d/e/f words inside a per-core coefficient array */
static const u8 coef_index[4] = { 0, 2, 3, 4 };	/* sel 12/16 -> a,b; 13/17 -> d; 14/18 -> e; 15/19 -> f */
static const u8 coef_count[4] = { 2, 1, 1, 1 };

static void phy_cal_coeffs(struct bcm4360_phy *phy, u8 mode, u16 *data, u8 sel, u32 core)
{
	struct bcm4360_phy_io *io = &phy->io;
	struct bcm4360_phy_cal_state *st = &phy->cal.state;

	if (sel < 12) {
		const struct txiqlo_sel *s = &txiqlo_sels[sel];
		u32 off = s->o + core * s->d;
		u16 saved = tbl_bracket_enter(io);

		if (mode == 0)
			bcm4360_tbl_read(io, TBL_BBMULT, s->n, off, 16, data);
		else
			bcm4360_tbl_write(io, TBL_BBMULT, s->n, off, 16, data);
		tbl_bracket_leave(io, saved);
		return;
	}

	if (mode == 2) {			/* the second IQ copy (cal+0x54) */
		st->coef_copy[core][0] = data[0];
		st->coef_copy[core][1] = data[1];
		return;
	}

	{
		u16 *p = (sel < 16) ? st->coef_inter[core] : st->coef_final[core];
		u8 k = (sel < 16) ? (sel - 12) : (sel - 16);
		u8 idx = coef_index[k];
		u8 n = coef_count[k];
		u8 i;

		for (i = 0; i < n; i++) {
			if (mode == 0)
				data[i] = p[idx + i];
			else
				p[idx + i] = data[i];
		}
	}
}

/*
 * acphy-cal-rx section (sub_092ffa, wlc_phy_rx_iq_est_gain_pulse_acphy; the same
 * function is sub_092ffa "reset pulse" of acphy-txpower section 28): force a bit
 * pattern into three per-core RX-gain registers, delay, restore in reverse.
 */
static void phy_gain_pulse(struct bcm4360_phy *phy)
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
 * acphy-cal-rx section (sub_093ebe, wlc_phy_tempsense_sample_setup_acphy; the
 * same function is wlc_phy_gpiosel_acphy of acphy-txpower section 27): arm the
 * PHY sample collector for one gain/mux code.
 */
static void phy_gpiosel(struct bcm4360_phy *phy, u16 sel, u8 flag)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 v;

	bcm4360_phy_write(io, ACPHY_REG_0x392, 0);
	bcm4360_phy_write(io, ACPHY_REG_0x393, 0);
	v = d11_read32(phy->hw, D11_REG_MACCONTROL);
	d11_write32(phy->hw, D11_REG_MACCONTROL, v & 0xffff3fff);
	d11_write16(phy->hw, D11_REG_0x49e, 0);
	bcm4360_phy_write(io, ACPHY_REG_0x394, (u16)((flag << 8) | sel));
	bcm4360_phy_write(io, ACPHY_REG_0x392, 0xffff);
	bcm4360_phy_write(io, ACPHY_REG_0x393, 0xffff);
}

/* state saved by the adc-read save/restore (sub_09bbe4 / sub_09be13) */
struct adc_read_state {
	u16 s1;		/* bit 9 of PHY(0x40f) */
	u16 s2;		/* PHY(0x394) */
	u8 stall;	/* bit 1 of PHY(0x19e) */
};

/* acphy-txpower.md section 26 (sub_09bbe4) / acphy-cal-rx (tempsense paldo
 * setup): the pi_ac+0x8e1 branch is 0 on this board. */
static void phy_adc_read_save(struct bcm4360_phy *phy, struct adc_read_state *st)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 v;

	st->stall = (u8)((bcm4360_phy_read(io, ACPHY_REG_0x19e) >> 1) & 1);
	v = bcm4360_phy_read(io, ACPHY_REG_0x40f);
	st->s1 = (u16)((v >> 9) & 1);
	bcm4360_phy_mod(io, ACPHY_REG_0x40f, 0x0200, 0);
	st->s2 = bcm4360_phy_read(io, ACPHY_REG_0x394);
}

/* acphy-txpower.md section 26 (sub_09be13) / acphy-cal-rx (tempsense paldo restore) */
static void phy_adc_read_restore(struct bcm4360_phy *phy, const struct adc_read_state *st)
{
	struct bcm4360_phy_io *io = &phy->io;

	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);
	bcm4360_phy_write(io, ACPHY_REG_0x394, st->s2);
	bcm4360_phy_mod(io, ACPHY_REG_0x40f, 0x0200, (u16)(st->s1 << 9));
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, (u16)(st->stall << 1));
}

/* --------------------------------------------------------------- cordic */

/*
 * acphy-cal-tx section 13 (wlc_phy_cordic): a cosine/sine pair for an angle in
 * Q16 degrees (0x1680000 per full turn), 18-iteration rotation-mode CORDIC,
 * output magnitude ~2^16 (gain seed 0x9b75 = round(2^16 / K), K the CORDIC
 * gain). The arctan table is atan(2^-i) scaled to the angle unit (derived, not
 * taken from the object). The algorithm is standard; its unspecified details
 * were confirmed against the test (see docs/re/questions/phy-cal.md).
 */
#define CORDIC_ITERS		18
#define CORDIC_SEED		0x9b75
#define CORDIC_FIX360		0x1680000
#define CORDIC_FIX180		0x0b40000
#define CORDIC_FIX90		0x05a0000

static const s32 cordic_atan[CORDIC_ITERS] = {
	2949120, 1740967, 919879, 466945, 234379, 117304,
	58666, 29335, 14668, 7334, 3667, 1833,
	917, 458, 229, 115, 57, 29,
};

static void phy_cordic(s32 theta, s32 *i_out, s32 *q_out)
{
	s32 sign = (theta < 0) ? -1 : 1;
	s32 angle = (((theta % CORDIC_FIX360) + CORDIC_FIX180 * sign) % CORDIC_FIX360)
		    - CORDIC_FIX180 * sign;
	s32 x, y;
	s32 signx = 1;
	u32 k;

	if (angle > CORDIC_FIX90) {
		angle -= CORDIC_FIX180;
		signx = -1;
	} else if (angle < -CORDIC_FIX90) {
		angle += CORDIC_FIX180;
		signx = -1;
	}

	x = CORDIC_SEED;
	y = 0;
	for (k = 0; k < CORDIC_ITERS; k++) {
		s32 xt;

		if (angle >= 0) {
			xt = x - (y >> k);
			y = (x >> k) + y;
			angle -= cordic_atan[k];
		} else {
			xt = x + (y >> k);
			y = -(x >> k) + y;
			angle += cordic_atan[k];
		}
		x = xt;
	}
	/*
	 * The AC-PHY convention (confirmed against the test): the seed sits in the
	 * cosine register (x), the outputs are I = sine (y) and Q = cosine (x), and
	 * the magnitude is ~2^16 (the seed 0x9b75 = round(2^16 / K)).
	 */
	*i_out = y * signx;
	*q_out = x * signx;
}

/* round(amp * v / 2^16), round toward nearest, halves toward zero (section 12) */
static s32 phy_tone_scale(s32 v, u16 amp)
{
	s32 x = (s32)amp * v;
	s32 q = x / 65536;
	s32 r = x - q * 65536;

	if (r > 32768)
		q += 1;
	else if (r < -32768)
		q -= 1;
	return q;
}

/* --------------------------------------------------------- tone / playback */

/*
 * acphy-cal-tx section 12 (wlc_phy_tx_tone_acphy): play a complex tone (or the
 * RF-sequencer tone) on all transmit cores. Result 0, or 0xffffffff on failure
 * (never here). `freq` is in kHz.
 */
u32 bcm4360_phy_tx_tone_acphy(struct bcm4360_phy *phy, s32 freq, u16 amp,
			      u8 dont_deaf, u8 rfseq, u8 set_bbmult)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 ns = phy_tone_ns(phy);
	u32 len = amp ? 2 * ns : 1;
	u32 c;

	if (amp) {						/* step 1: build the samples */
		u32 samples[2 * 80];
		s32 theta = 0;
		s32 step = ((s32)(freq * 36 / (s32)ns) * 65536) / 100;
		u16 saved;
		u32 i;

		for (i = 0; i < len; i++) {
			s32 ci, cq;

			phy_cordic(theta, &ci, &cq);
			{
				s32 si = phy_tone_scale(ci, amp);
				s32 sq = phy_tone_scale(cq, amp);

				samples[i] = (u32)(((sq & 0x3ff) << 10) | (si & 0x3ff));
			}
			theta += step;
		}
		saved = tbl_bracket_enter(io);			/* step 1.2 */
		bcm4360_tbl_write(io, TBL_SAMPLES, len, 0, 32, samples);
		tbl_bracket_leave(io, saved);
	}

	if (!phy->cal.tone_bbmult_saved) {			/* step 2 */
		for (c = 0; c < phy_cores(phy); c++)
			phy->cal.tone_bbmult[c] = phy_get_bbmult(phy, c);
		phy->cal.tone_bbmult_saved = 1;
	}

	/* step 3: force the tone bbmult per core */
	if (amp) {
		if (set_bbmult) {
			for (c = 0; c < phy_cores(phy); c++)
				phy_set_bbmult(phy, 0x40, c);
		}
	} else {
		for (c = 0; c < phy_cores(phy); c++)
			phy_set_bbmult(phy, 0, c);
	}

	if (!dont_deaf)						/* step 4 */
		bcm4360_phy_stay_in_carriersearch(phy, true);

	if (rfseq == 1) {					/* step 5: RF-sequencer tone */
		u16 b = (phy_bw(phy) == BCM4360_CHANSPEC_BW_80) ? 6 :
			(phy_bw(phy) == BCM4360_CHANSPEC_BW_40) ? 4 : 2;

		bcm4360_phy_or(io, ACPHY_REG_0x471, 0x0001);
		bcm4360_phy_or(io, ACPHY_REG_0x471, b);
		phy_force_rfseq(phy, 0);
	} else {						/* step 5: sample player */
		u16 s400;
		u32 i;

		bcm4360_phy_and(io, ACPHY_REG_0x471, 0xfffe);
		bcm4360_phy_write(io, ACPHY_REG_0x463, (u16)(len - 1));
		bcm4360_phy_write(io, ACPHY_REG_0x461, 0xffff);
		bcm4360_phy_write(io, ACPHY_REG_0x462, 0x003c);
		s400 = bcm4360_phy_read(io, ACPHY_REG_0x400);
		bcm4360_phy_or(io, ACPHY_REG_0x400, 0x0001);
		bcm4360_phy_and(io, ACPHY_REG_0x460, 0xfffb);
		bcm4360_phy_and(io, ACPHY_REG_0x460, 0xfffe);
		bcm4360_phy_and(io, ACPHY_REG_0x382, 0x3fff);
		if (!dont_deaf)
			bcm4360_phy_or(io, ACPHY_REG_0x460, 0x0001);
		else
			bcm4360_phy_or(io, ACPHY_REG_0x382, 0x8000);
		for (i = 0; i < 100; i++) {
			if (!(bcm4360_phy_read(io, ACPHY_REG_0x403) & 0x0001))
				break;
			hw_udelay(10);
		}
		bcm4360_phy_write(io, ACPHY_REG_0x400, s400);
	}

	if (!dont_deaf)						/* step 6 */
		bcm4360_phy_stay_in_carriersearch(phy, false);

	return 0;
}

/* acphy-cal-tx section 14 (wlc_phy_stopplayback_acphy) */
void bcm4360_phy_stopplayback_acphy(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 v = bcm4360_phy_read(io, ACPHY_REG_0x464);
	u32 c;

	if (v & 0x0001)
		bcm4360_phy_or(io, ACPHY_REG_0x460, 0x0002);
	else if (v & 0x0002)
		bcm4360_phy_and(io, ACPHY_REG_0x382, 0x7fff);

	bcm4360_phy_and(io, ACPHY_REG_0x460, 0xfffb);

	if (phy->cal.tone_bbmult_saved) {
		for (c = 0; c < phy_cores(phy); c++)
			phy_set_bbmult(phy, phy->cal.tone_bbmult[c], c);
		phy->cal.tone_bbmult_saved = 0;
	}

	bcm4360_phy_resetcca(phy);
}

/* ------------------------------------------------------------ tx IQ/LO cal */

/*
 * acphy-cal-tx section 16 (sub_09380f, wlc_phy_txcal_radio_setup_acphy): save
 * the radio front-end registers and set the tx-cal loopback path. `save` holds
 * 7 registers per core in the order 0x1a, 0x1b, 0x1c, 0x1e, 0x1f, 0x24, 0x170.
 */
static const u16 txcal_radio_regs[7] = {
	R2069_REG_0x01a, R2069_REG_0x01b, R2069_REG_0x01c, R2069_REG_0x01e,
	R2069_REG_0x01f, R2069_REG_0x024, R2069_REG_0x170,
};

static void txcal_radio_setup(struct bcm4360_phy *phy, u16 save[][7])
{
	struct bcm4360_phy_io *io = &phy->io;
	bool up5g = (phy->radio_chanspec & BCM4360_CHANSPEC_BAND) == 0xc000;
	u32 c, i;

	for (c = 0; c < phy_cores(phy); c++) {
		u32 b = c << R2069_BANK_SHIFT;

		for (i = 0; i < 7; i++)
			save[c][i] = bcm4360_radio_read(io, txcal_radio_regs[i] | b);

		if (up5g) {
			bcm4360_radio_mod(io, R2069_REG_0x01a | b, 0x00f0, 0x00b0);
			bcm4360_radio_mod(io, R2069_REG_0x01f | b, 0x0004, 0x0004);
			bcm4360_radio_mod(io, R2069_REG_0x170 | b, 0x0100, 0x0100);
			bcm4360_radio_mod(io, R2069_REG_0x170 | b, 0x4000, 0);
			bcm4360_radio_mod(io, R2069_REG_0x01e | b, 0x0004, 0);
		} else {
			bcm4360_radio_mod(io, R2069_REG_0x01a | b, 0x00f0, 0x0080);
			bcm4360_radio_mod(io, R2069_REG_0x01f | b, 0x0004, 0);
			bcm4360_radio_mod(io, R2069_REG_0x170 | b, 0x0100, 0);
			bcm4360_radio_mod(io, R2069_REG_0x170 | b, 0x4000, 0x4000);
			bcm4360_radio_mod(io, R2069_REG_0x01e | b, 0x0004, 0x0004);
		}
		bcm4360_radio_mod(io, R2069_REG_0x01a | b, 0x0300, 0);
	}
}

/* acphy-cal-tx section 17 (sub_0948c3, wlc_phy_txcal_radio_restore_acphy) */
static void txcal_radio_restore(struct bcm4360_phy *phy, u16 save[][7])
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c, i;

	for (c = 0; c < phy_cores(phy); c++) {
		u32 b = c << R2069_BANK_SHIFT;

		for (i = 0; i < 7; i++)
			bcm4360_radio_write(io, txcal_radio_regs[i] | b, save[c][i]);
	}
}

/*
 * acphy-cal-tx section 15 A.6 saves these per-core PHY registers; section 18
 * (sub_097562, wlc_phy_txcal_phy_restore_acphy) restores them. The save order is
 * txcal_phy_save_order; the restore order is txcal_phy_restore_order.
 */
/* restore order and the save-slot each restored register comes from */
static const u16 txcal_phy_restore_reg[16] = {
	ACPHY_REG_0x73e, ACPHY_REG_0x721, ACPHY_REG_0x729, ACPHY_REG_0x720,
	ACPHY_REG_0x728, ACPHY_REG_0x724, ACPHY_REG_0x736, ACPHY_REG_0x723,
	ACPHY_REG_0x735, ACPHY_REG_0x737, ACPHY_REG_0x738, ACPHY_REG_0x727,
	ACPHY_REG_0x73c, ACPHY_REG_0x725, ACPHY_REG_0x739, ACPHY_REG_0x73a,
};
static const u8 txcal_phy_restore_slot[16] = {
	0, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 1, 2, 3,
};

static void txcal_phy_restore(struct bcm4360_phy *phy, u16 save[][16], u16 s19e, u16 s40f)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c, i;

	for (c = 0; c < phy_cores(phy); c++) {
		u32 o = c * ACPHY_CORE_STEP;

		for (i = 0; i < 16; i++)
			bcm4360_phy_write(io, txcal_phy_restore_reg[i] + o,
					  save[c][txcal_phy_restore_slot[i]]);
	}
	bcm4360_phy_write(io, ACPHY_REG_0x19e, s19e);
	bcm4360_phy_write(io, ACPHY_REG_0x40f, s40f);
	bcm4360_phy_resetcca(phy);
}

/*
 * acphy-cal-tx section 19 (sub_09c66c, wlc_phy_txcal_txgain_save_set_acphy):
 * save the current transmit gain of each core into `save` (10 bytes/core) and
 * apply the pre-cal gain record `rec`.
 */
static void txcal_txgain_save_set(struct bcm4360_phy *phy, const u8 *rec, u8 *save)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 bracket = tbl_bracket_enter(io);
	u32 c;

	for (c = 0; c < phy_cores(phy); c++) {
		u8 *s = save + 10 * c;
		const u8 *n = rec + 10 * c;
		u16 lo, mid, hi, m;

		lo = tbl_read16(io, TBL_GAINCODE, TBL_GAINCODE_LO + c);
		mid = tbl_read16(io, TBL_GAINCODE, TBL_GAINCODE_MID + c);
		hi = tbl_read16(io, TBL_GAINCODE, TBL_GAINCODE_HI + c);
		m = phy_get_bbmult(phy, c);
		s[0] = (u8)lo; s[1] = (u8)(lo >> 8);
		s[2] = (u8)mid; s[3] = (u8)(mid >> 8);
		s[4] = (u8)hi; s[5] = (u8)(hi >> 8);
		s[8] = (u8)m; s[9] = (u8)(m >> 8);

		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_LO + c, (u16)(n[0] | (n[1] << 8)));
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_MID + c, (u16)(n[2] | (n[3] << 8)));
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_HI + c, (u16)(n[4] | (n[5] << 8)));
		phy_set_bbmult(phy, (u16)(n[8] | (n[9] << 8)), c);
	}
	tbl_bracket_leave(io, bracket);
}

/* acphy-cal-tx section 19 (sub_09c810, wlc_phy_txcal_txgain_restore_acphy) */
static void txcal_txgain_restore(struct bcm4360_phy *phy, const u8 *rec)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 bracket = tbl_bracket_enter(io);
	u32 c;

	for (c = 0; c < phy_cores(phy); c++) {
		const u8 *r = rec + 10 * c;

		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_LO + c, (u16)(r[0] | (r[1] << 8)));
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_MID + c, (u16)(r[2] | (r[3] << 8)));
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_HI + c, (u16)(r[4] | (r[5] << 8)));
		phy_set_bbmult(phy, (u16)(r[8] | (r[9] << 8)), c);
	}
	tbl_bracket_leave(io, bracket);
}

/*
 * acphy-cal-tx section 19 (sub_09c91d, wlc_phy_txcal_gainlut_load_acphy): write
 * the two 18-entry gain schedules to TBL(0x0c), each entry
 * ((high * scale / 100) << 8) | low.
 */
static const u8 gainlut_a[18][2] = {
	{ 0x03, 0 }, { 0x04, 0 }, { 0x06, 0 }, { 0x09, 0 }, { 0x0d, 0 }, { 0x12, 0 },
	{ 0x19, 0 }, { 0x19, 1 }, { 0x19, 2 }, { 0x19, 3 }, { 0x19, 4 }, { 0x19, 5 },
	{ 0x19, 6 }, { 0x19, 7 }, { 0x23, 7 }, { 0x32, 7 }, { 0x47, 7 }, { 0x64, 7 },
};
static const u8 gainlut_b[18][2] = {
	{ 0x03, 0 }, { 0x04, 0 }, { 0x06, 0 }, { 0x09, 0 }, { 0x0d, 0 }, { 0x12, 0 },
	{ 0x19, 0 }, { 0x23, 0 }, { 0x32, 0 }, { 0x47, 0 }, { 0x64, 0 }, { 0x64, 1 },
	{ 0x64, 2 }, { 0x64, 3 }, { 0x64, 4 }, { 0x64, 5 }, { 0x64, 6 }, { 0x64, 7 },
};

static void txcal_gainlut_load(struct bcm4360_phy *phy, u16 scale)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 bracket = tbl_bracket_enter(io);
	u32 i;

	for (i = 0; i < 18; i++)
		tbl_write16(io, TBL_BBMULT, i,
			    (u16)(((gainlut_a[i][0] * scale / 100) << 8) | gainlut_a[i][1]));
	for (i = 0; i < 18; i++)
		tbl_write16(io, TBL_BBMULT, 0x20 + i,
			    (u16)(((gainlut_b[i][0] * scale / 100) << 8) | gainlut_b[i][1]));
	tbl_bracket_leave(io, bracket);
}

/*
 * acphy-cal-tx section 19 (wlc_phy_populate_tx_loft_comp_tbl_acphy): fill the
 * per-core LO-feedthrough tables with the calibrated LO-comp words. 2.4 GHz: the
 * whole table is the word; only for radio id != 0x30b (true for the 2069).
 */
static void txcal_populate_loft(struct bcm4360_phy *phy, const u16 *d)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 bracket;
	u32 c, i;

	if (phy->ver.radio_id == 0x30b)
		return;

	bracket = tbl_bracket_enter(io);
	for (c = 0; c < phy_cores(phy); c++) {
		u16 buf[128];

		for (i = 0; i < 128; i++)
			buf[i] = d[c];
		bcm4360_tbl_write(io, TBL_LOFT_CORE0 + c * 0x20, 128, 0, 16, buf);
	}
	tbl_bracket_leave(io, bracket);
}

/* the per-core loopback overrides applied in section 15 A.6 (mod reg, mask, val) */
struct lb_mod {
	u16 reg;
	u16 mask;
	u16 val;
};

static const struct lb_mod txcal_lb_overrides[] = {
	{ 0x720, 0x0002, 0x0002 }, { 0x728, 0x0002, 0 }, { 0x721, 0x0040, 0x0040 },
	{ 0x729, 0x0040, 0 }, { 0x721, 0x0080, 0x0080 }, { 0x729, 0x0080, 0 },
	{ 0x721, 0x0020, 0x0020 }, { 0x729, 0x0020, 0 }, { 0x721, 0x2000, 0x2000 },
	{ 0x729, 0xe000, 0 }, { 0x721, 0x0800, 0x0800 }, { 0x729, 0x0800, 0 },
	{ 0x721, 0x0400, 0x0400 }, { 0x729, 0x0400, 0 }, { 0x721, 0x4000, 0x4000 },
	{ 0x728, 0x3800, 0 }, { 0x721, 0x1000, 0x1000 }, { 0x729, 0x1000, 0 },
	{ 0x720, 0x0020, 0x0020 }, { 0x728, 0x0020, 0x0020 }, { 0x720, 0x0040, 0x0040 },
	{ 0x728, 0x0040, 0x0040 }, { 0x720, 0x0010, 0x0010 }, { 0x728, 0x0010, 0x0010 },
	{ 0x721, 0x0100, 0x0100 }, { 0x729, 0x0100, 0x0100 }, { 0x727, 0x0004, 0x0004 },
	{ 0x73c, 0x0010, 0x0010 },
};

/* registers saved in A.6 (15 of them, after PHY(0x73e) which is slot 0) */
static const u16 txcal_phy_save15[15] = {
	ACPHY_REG_0x725, ACPHY_REG_0x739, ACPHY_REG_0x73a, ACPHY_REG_0x721,
	ACPHY_REG_0x729, ACPHY_REG_0x720, ACPHY_REG_0x728, ACPHY_REG_0x724,
	ACPHY_REG_0x736, ACPHY_REG_0x723, ACPHY_REG_0x735, ACPHY_REG_0x737,
	ACPHY_REG_0x738, ACPHY_REG_0x727, ACPHY_REG_0x73c,
};

/* command lists, indexed by (sm, part); each command word drives PHY(0x380) */
static const u16 txcal_cmd_sm0_p0[6] = { 0x434, 0x334, 0x084, 0x267, 0x056, 0x234 };
static const u16 txcal_cmd_sm0_p1[2] = { 0x084, 0x056 };
static const u16 txcal_cmd_sm1_p0[6] = { 0x423, 0x334, 0x073, 0x267, 0x045, 0x234 };
static const u16 txcal_cmd_sm1_p1[2] = { 0x073, 0x045 };

/* the successive-approximation step counts (radio major revision 0) */
static const u16 txcal_steps[6] = { 0x3d, 0x1e, 0x0f, 0x07, 0x03, 0x01 };

/* result propagation per command type: {read best, write start, write state} */
static const u8 txcal_sel_best[5] = { 4, 0, 5, 6, 7 };
static const u8 txcal_sel_start[5] = { 0, 0, 1, 2, 3 };
static const u8 txcal_sel_state[5] = { 12, 0, 13, 14, 15 };

/*
 * acphy-cal-tx section 15 (sub_0abc76, wlc_phy_cal_txiqlo_acphy). Single-shot
 * only is exercised (mphase = 0). Returns 0, or nonzero if the tone failed.
 */
static u32 phy_cal_txiqlo(struct bcm4360_phy *phy, u8 sm, u8 mphase, u8 part)
{
	struct bcm4360_phy_io *io = &phy->io;
	struct bcm4360_phy_cal_state *st = &phy->cal.state;
	u32 n = phy_cores(phy);
	u32 bwix = phy_bwix(phy);
	u32 bw3 = 3 + bwix;
	u16 chanspec = phy->radio_chanspec;
	static const u16 ig[3] = { 0x76, 0x87, 0x98 };
	static const u16 qg[3] = { 0x79, 0x79, 0x79 };
	u16 u8140;
	u16 s19e, s40f;
	u16 radio_save[BCM4360_PHY_CORES_MAX][7];
	u16 phy_save[BCM4360_PHY_CORES_MAX][16];
	u8 gsave[BCM4360_PHY_CORES_MAX * 10];
	u16 loft[BCM4360_PHY_CORES_MAX] = { 0 };
	const u16 *list;
	u32 cnt, lo, hi, i, c;
	u32 tone_ok;

	/* A. Set-up */
	bcm4360_phy_stay_in_carriersearch(phy, true);		/* A.1 */
	u8140 = bcm4360_phy_read(io, ACPHY_REG_0x140);		/* A.3 */
	phy_classifier(phy, 0x0007, 0x0004);
	txcal_radio_setup(phy, radio_save);			/* A.4 */

	s19e = bcm4360_phy_read(io, ACPHY_REG_0x19e);		/* A.5 */
	s40f = bcm4360_phy_read(io, ACPHY_REG_0x40f);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);
	bcm4360_phy_mod(io, ACPHY_REG_0x40f, 0x0200, 0);

	for (c = 0; c < n; c++) {				/* A.6 */
		u32 o = c * ACPHY_CORE_STEP;
		u32 j;

		phy_save[c][0] = bcm4360_phy_read(io, ACPHY_REG_0x73e + o);
		bcm4360_phy_write(io, ACPHY_REG_0x73e + o, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x73e + o, 0x0010, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x73e + o, 0x0020, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x73e + o, 0x0040, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x73e + o, 0x0080, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x73e + o, 0x1000, 0x1000);
		bcm4360_phy_mod(io, ACPHY_REG_0x73e + o, 0x0400, 0x0400);
		for (j = 0; j < 15; j++)
			phy_save[c][j + 1] = bcm4360_phy_read(io, txcal_phy_save15[j] + o);
		for (j = 0; j < ARRAY_SIZE(txcal_lb_overrides); j++)
			bcm4360_phy_mod(io, txcal_lb_overrides[j].reg + o,
					txcal_lb_overrides[j].mask, txcal_lb_overrides[j].val);
		bcm4360_phy_write(io, ACPHY_REG_0x724 + o, 0x03ff);
		bcm4360_phy_write(io, ACPHY_REG_0x736 + o, part ? 0x022a : 0x0152);
		bcm4360_phy_mod(io, ACPHY_REG_0x73a + o, 0x0007, (u16)(c & 7));
		bcm4360_phy_mod(io, ACPHY_REG_0x725 + o, 0x0020, 0x0020);
		bcm4360_phy_mod(io, ACPHY_REG_0x739 + o, 0x007e, (u16)((c >> 2) & 0x7e));
		bcm4360_phy_mod(io, ACPHY_REG_0x725 + o, 0x0002, 0x0002);
		bcm4360_phy_mod(io, ACPHY_REG_0x73a + o, 0x0008, (u16)((c >> 6) & 8));
		bcm4360_phy_mod(io, ACPHY_REG_0x725 + o, 0x0040, 0x0040);
		bcm4360_phy_mod(io, ACPHY_REG_0x73a + o, 0x0010, (u16)((c >> 6) & 0x10));
		bcm4360_phy_mod(io, ACPHY_REG_0x725 + o, 0x0080, 0x0080);
		bcm4360_phy_mod(io, ACPHY_REG_0x73a + o, 0x0060, (u16)((c >> 6) & 0x60));
		bcm4360_phy_mod(io, ACPHY_REG_0x725 + o, 0x0100, 0x0100);
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + o, 0x0008, 0x0008);
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + o, 0x0010, 0x0010);
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + o, 0x0800, 0x0800);
		bcm4360_phy_mod(io, ACPHY_REG_0x735 + o, 0x0700, (u16)(bw3 << 8));
		bcm4360_phy_mod(io, ACPHY_REG_0x735 + o, 0x3800, (u16)(bw3 << 11));
		bcm4360_phy_mod(io, ACPHY_REG_0x738 + o, 0x0007, (u16)bw3);
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + o, 0x0001, 0x0001);
		bcm4360_phy_mod(io, ACPHY_REG_0x735 + o, 0x0001, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + o, 0x0020, 0x0020);
		bcm4360_phy_mod(io, ACPHY_REG_0x735 + o, 0x4000, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + o, 0x0002, 0x0002);
		bcm4360_phy_mod(io, ACPHY_REG_0x735 + o, 0x001e, 0x0008);
		if (phy->ver.phy_rev != 3) {
			bcm4360_phy_mod(io, ACPHY_REG_0x727 + o, 0x0002, 0x0002);
			bcm4360_phy_mod(io, ACPHY_REG_0x73c + o, 0x000e, 0x0004);
			bcm4360_phy_mod(io, ACPHY_REG_0x727 + o, 0x0001, 0x0001);
			bcm4360_phy_mod(io, ACPHY_REG_0x73c + o, 0x0001, 0x0001);
		}
	}

	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0040, 0x0040);	/* A.7 */
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0080, 0x0080);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0100, 0x0100);
	phy_gain_pulse(phy);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	txcal_txgain_save_set(phy, st->gainrec[0], gsave);	/* A.8 */
	bcm4360_phy_write(io, ACPHY_REG_0x382, CAL_ENGINE_ARM);	/* A.9 */

	/* B. Seed the coefficients (single-shot / first mphase step) */
	if (st->phase < 3) {
		for (c = 0; c < n; c++) {
			u16 z[2] = { 0, 0 };

			phy_cal_coeffs(phy, 1, z, 0, c);
			if (part == 0) {
				phy_cal_coeffs(phy, 1, z, 1, c);
				phy_cal_coeffs(phy, 1, z, 2, c);
				phy_cal_coeffs(phy, 1, z, 3, c);
			}
		}
	}

	/* C. Choose the command list */
	if (sm == 0) {
		list = part ? txcal_cmd_sm0_p1 : txcal_cmd_sm0_p0;
	} else {
		list = part ? txcal_cmd_sm1_p1 : txcal_cmd_sm1_p0;
	}
	cnt = part ? 2 : 6;
	lo = 0;
	hi = n * cnt - 1;

	/* D. The tone */
	{
		s32 khz = (phy_bw(phy) == BCM4360_CHANSPEC_BW_80) ? 8000 :
			  (phy_bw(phy) == BCM4360_CHANSPEC_BW_40) ? 4000 : 2000;

		tone_ok = bcm4360_phy_tx_tone_acphy(phy, khz / 2, 0xfa, 1, 0, 0);
	}
	hw_udelay(5);
	for (c = 0; c < n; c++) {
		u32 o = c * ACPHY_CORE_STEP;

		bcm4360_phy_mod(io, ACPHY_REG_0x73a + o, 0x0100, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x725 + o, 0x0400, 0x0400);
	}
	if (tone_ok)
		goto restore;

	/* E. The command loop */
	for (i = lo; i <= hi; i++) {
		u32 core = i / cnt;
		u32 o = core * ACPHY_CORE_STEP;
		u16 cmd = list[i % cnt];
		u8 type = (u8)((cmd >> 8) & 0xf);
		u16 r[2] = { 0, 0 };
		u32 s;

		if (!st->core_flag[core]) {			/* E.2 */
			u16 scale = (u16)(st->gainrec[core][8] | (st->gainrec[core][9] << 8));

			txcal_gainlut_load(phy, scale);
			st->core_flag[core] = 1;
		}
		bcm4360_phy_write(io, ACPHY_REG_0x381,		/* E.3 */
				  (u16)((qg[bwix] << 8) | ig[bwix]));
		if (type == 3 || type == 4) {			/* E.4 */
			u16 z[2] = { 0, 0 };

			phy_cal_coeffs(phy, 1, z, 1, core);
			if (type == 4)
				phy_cal_coeffs(phy, 1, z, 2, core);
		}
		for (s = 0; s < ARRAY_SIZE(txcal_steps); s++) {	/* E.5 */
			u16 r144;
			u32 cd;

			bcm4360_phy_write(io, ACPHY_REG_0x383, txcal_steps[s]);
			bcm4360_phy_write(io, ACPHY_REG_0x380,
					  (u16)(cmd | 0x8000 | (core << 12)));
			cd = 20000 + 9;
			while ((bcm4360_phy_read(io, ACPHY_REG_0x380) & 0xc000) && cd >= 10) {
				hw_udelay(10);
				cd -= 10;
			}
			r144 = bcm4360_radio_read(io, R2069_REG_0x144 | (core << R2069_BANK_SHIFT));
			if (!(r144 & 0x0004))
				break;
			bcm4360_phy_mod(io, ACPHY_REG_0x73a + o, 0x0100, 0x0100);
			bcm4360_phy_mod(io, ACPHY_REG_0x73a + o, 0x0100, 0);
		}
		/* E.6: read the best value and propagate it */
		phy_cal_coeffs(phy, 0, r, txcal_sel_best[type], core);
		phy_cal_coeffs(phy, 1, r, txcal_sel_start[type], core);
		phy_cal_coeffs(phy, 1, r, txcal_sel_state[type], core);
		if (phy->ver.phy_rev == ACPHY_REV_1 && (i % cnt) > 4)
			loft[core] = r[0];
	}

	/* F. Apply the results */
	if (part == 0) {
		for (c = 0; c < n; c++) {
			u16 ab[2], d[1], e[1], f[1];

			phy_cal_coeffs(phy, 0, ab, 12, c);
			phy_cal_coeffs(phy, 1, ab, 16, c);
			phy_cal_coeffs(phy, 1, ab, 8, c);
			phy_cal_coeffs(phy, 1, ab, 10, c);
			phy_cal_coeffs(phy, 0, d, 13, c);
			phy_cal_coeffs(phy, 1, d, 17, c);
			phy_cal_coeffs(phy, 1, d, 9, c);
			phy_cal_coeffs(phy, 1, d, 11, c);
			phy_cal_coeffs(phy, 0, e, 14, c);
			phy_cal_coeffs(phy, 1, e, 18, c);
			phy_cal_coeffs(phy, 0, f, 15, c);
			phy_cal_coeffs(phy, 1, f, 19, c);
		}
		st->restart_ok = 1;
		st->last_chanspec = chanspec;
	} else {
		for (c = 0; c < n; c++) {
			u16 ab[2];

			phy_cal_coeffs(phy, 0, ab, 12, c);
			phy_cal_coeffs(phy, 2, ab, 12, c);
		}
		st->restart_ok = 1;
		st->last_chanspec = chanspec;
	}

restore:
	bcm4360_phy_stopplayback_acphy(phy);
	bcm4360_phy_write(io, ACPHY_REG_0x382, 0);
	if (phy->ver.phy_rev == ACPHY_REV_1)
		txcal_populate_loft(phy, loft);
	txcal_txgain_restore(phy, gsave);
	txcal_phy_restore(phy, phy_save, s19e, s40f);
	txcal_radio_restore(phy, radio_save);
	bcm4360_phy_write(io, ACPHY_REG_0x140, u8140);
	bcm4360_phy_stay_in_carriersearch(phy, false);
	(void)mphase;
	return tone_ok;
}

/* ------------------------------------------------------------ rx IQ cal */

/*
 * acphy-cal-rx (sub_0addfa, wlc_phy_rxiqcal_acphy): the receive IQ-imbalance
 * calibration. On this board (2.4 GHz, 20 MHz, two cores) the tone list is
 * {+8,-8} and img = 1; the measured powers are 0, so both coefficients are 0.
 *
 * Work in progress: the structure is from the spec, the exact per-core PHY
 * override sequence of step 7 is a fixed table the spec leaves to the trace and
 * is filled in from the comparison test.
 */
static void phy_rxiqcal(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 n = phy_cores(phy);
	u32 c;

	bcm4360_phy_stay_in_carriersearch(phy, true);		/* step 2 */
	phy_force_rfseq(phy, 2);

	for (c = 0; c < n; c++) {				/* step 3 */
		bcm4360_phy_write(io, core_reg(ACPHY_REG_0x6a0, c), 0);
		bcm4360_phy_write(io, core_reg(ACPHY_REG_0x6a1, c), 0);
	}

	/* the remaining steps (radio/PHY loopback save+override, gain search, the
	 * tone loop, the regression and the restore) are filled in against the
	 * test; the coefficients compute to zero. */
	bcm4360_phy_stay_in_carriersearch(phy, false);
}

/* --------------------------------------------------------- temperature */

/* per-core divisors of the tempsense slope (cores 0,1,2) */
static const u32 tempsense_div[3] = { 527, 521, 522 };
/* tempsense phase tables A (RADIO(0x0e) bit 1) and B (RADIO(0x0e) bit 2 value) */
static const u8 tempsense_a[4] = { 1, 0, 1, 0 };
static const u8 tempsense_b[4] = { 0, 0, 1, 1 };

/* the 14 PHY registers saved per core (tempsense step 3) */
static const u16 tempsense_phy_regs[14] = {
	ACPHY_REG_0x73e, ACPHY_REG_0x727, ACPHY_REG_0x73c, ACPHY_REG_0x721,
	ACPHY_REG_0x729, ACPHY_REG_0x720, ACPHY_REG_0x728, ACPHY_REG_0x724,
	ACPHY_REG_0x736, ACPHY_REG_0x725, ACPHY_REG_0x739, ACPHY_REG_0x73a,
	ACPHY_REG_0x722, ACPHY_REG_0x734,
};
/* the 7 radio registers saved per core (tempsense step 4) */
static const u16 tempsense_radio_regs[7] = {
	R2069_REG_0x16e, R2069_REG_0x00e, R2069_REG_0x161, R2069_REG_0x017,
	R2069_REG_0x15f, R2069_REG_0x024, R2069_REG_0x025,
};

/* distribute the config word W into the ADC-clock/mux fields (step 3 / rxcal) */
static void phy_distribute_w(struct bcm4360_phy_io *io, u32 b, u16 w)
{
	bcm4360_phy_mod(io, ACPHY_REG_0x73a + b, 0x0007, w & 7);
	bcm4360_phy_mod(io, ACPHY_REG_0x725 + b, 0x0020, 0x0020);
	bcm4360_phy_mod(io, ACPHY_REG_0x739 + b, 0x007e, (u16)((w >> 2) & 0x7e));
	bcm4360_phy_mod(io, ACPHY_REG_0x725 + b, 0x0002, 0x0002);
	bcm4360_phy_mod(io, ACPHY_REG_0x73a + b, 0x0008, (u16)((w >> 6) & 0x8));
	bcm4360_phy_mod(io, ACPHY_REG_0x725 + b, 0x0040, 0x0040);
	bcm4360_phy_mod(io, ACPHY_REG_0x73a + b, 0x0010, (u16)((w >> 6) & 0x10));
	bcm4360_phy_mod(io, ACPHY_REG_0x725 + b, 0x0080, 0x0080);
	bcm4360_phy_mod(io, ACPHY_REG_0x73a + b, 0x0060, (u16)((w >> 6) & 0x60));
	bcm4360_phy_mod(io, ACPHY_REG_0x725 + b, 0x0100, 0x0100);
}

/* tempsense config word W from the bandwidth (step 1) */
static u16 tempsense_w(struct bcm4360_phy *phy)
{
	if (phy_bw(phy) == BCM4360_CHANSPEC_BW_80)
		return 0x7f8;
	if (phy_bw(phy) == BCM4360_CHANSPEC_BW_40)
		return (u16)(0x43e9 + (phy->acphy_116a == 0 ? 0x201 : 0));
	return 0xd5eb;
}

/*
 * acphy-cal-rx section 3 (wlc_phy_tempsense_acphy): measure the die temperature.
 */
u32 bcm4360_phy_tempsense(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 w = tempsense_w(phy);
	u16 s19e;
	u16 phy_save[BCM4360_PHY_CORES_MAX][14];
	u16 radio_save[BCM4360_PHY_CORES_MAX][7];
	s32 m[BCM4360_PHY_CORES_MAX][4];
	struct adc_read_state adc;
	s32 acc, degrees, temp;
	u32 c, i, p, k, ncores = 0;

	bcm4360_mac_suspend(phy->hw);
	phy_reg_enter(phy);

	s19e = bcm4360_phy_read(io, ACPHY_REG_0x19e);		/* step 2 */
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0040, 0);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0080, 0);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0100, 0);

	for (c = 0; c < phy_cores(phy); c++) {			/* step 3 */
		u32 b = c * ACPHY_CORE_STEP;

		if (!(phy->rxchain & (1u << c)))
			continue;
		phy_save[c][0] = bcm4360_phy_read(io, ACPHY_REG_0x73e + b);
		bcm4360_phy_write(io, ACPHY_REG_0x73e + b, 0x0440);
		for (i = 1; i < 14; i++)
			phy_save[c][i] = bcm4360_phy_read(io, tempsense_phy_regs[i] + b);
		bcm4360_phy_mod(io, ACPHY_REG_0x727 + b, 0x0002, 0x0002);
		bcm4360_phy_mod(io, ACPHY_REG_0x73c + b, 0x000e, 0x0002);
		bcm4360_phy_mod(io, ACPHY_REG_0x727 + b, 0x0001, 0x0001);
		bcm4360_phy_mod(io, ACPHY_REG_0x73c + b, 0x0001, 0x0001);
		bcm4360_phy_mod(io, ACPHY_REG_0x721 + b, 0x0100, 0x0100);
		bcm4360_phy_mod(io, ACPHY_REG_0x729 + b, 0x0100, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x720 + b, 0x0020, 0x0020);
		bcm4360_phy_mod(io, ACPHY_REG_0x728 + b, 0x0020, 0x0020);
		bcm4360_phy_mod(io, ACPHY_REG_0x720 + b, 0x0040, 0x0040);
		bcm4360_phy_mod(io, ACPHY_REG_0x728 + b, 0x0040, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x720 + b, 0x0010, 0x0010);
		bcm4360_phy_mod(io, ACPHY_REG_0x728 + b, 0x0010, 0x0010);
		bcm4360_phy_write(io, ACPHY_REG_0x736 + b, 0x0154);
		bcm4360_phy_write(io, ACPHY_REG_0x724 + b, 0x03ff);
		phy_distribute_w(io, b, w);
		bcm4360_phy_mod(io, ACPHY_REG_0x734 + b, 0x0007, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x722 + b, 0x0004, 0x0004);
	}

	for (c = 0; c < phy_cores(phy); c++) {			/* step 4 */
		u32 cb = c << R2069_BANK_SHIFT;

		if (!(phy->rxchain & (1u << c)))
			continue;
		for (i = 0; i < 7; i++)
			radio_save[c][i] = bcm4360_radio_read(io, tempsense_radio_regs[i] | cb);
		bcm4360_radio_mod(io, R2069_REG_0x161 | cb, 0x4000, 0x4000);
		bcm4360_radio_mod(io, R2069_REG_0x00e | cb, 0x0001, 0x0001);
		bcm4360_radio_mod(io, R2069_REG_0x161 | cb, 0x1000, 0x1000);
		bcm4360_radio_mod(io, R2069_REG_0x017 | cb, 0x0001, 0x0001);
		bcm4360_radio_mod(io, R2069_REG_0x017 | cb, 0x0002, 0);
		bcm4360_radio_mod(io, R2069_REG_0x15f | cb, 0x2000, 0x2000);
		bcm4360_radio_mod(io, R2069_REG_0x025 | cb, 0x03ff, 0x0091);
		bcm4360_radio_mod(io, R2069_REG_0x15f | cb, 0x4000, 0x4000);
		bcm4360_radio_mod(io, R2069_REG_0x024 | cb, 0x0700, 0x0300);
	}

	phy_adc_read_save(phy, &adc);				/* step 5 */
	phy_gain_pulse(phy);

	for (c = 0; c < phy_cores(phy); c++) {			/* step 6 */
		u32 cb = c << R2069_BANK_SHIFT;

		if (!(phy->rxchain & (1u << c)))
			continue;
		phy_gpiosel(phy, (u16)(0x10 + c), 1);
		for (p = 0; p < 4; p++) {
			s32 sum = 0;

			bcm4360_radio_mod(io, R2069_REG_0x16e | cb, 0x0002, 0x0002);
			bcm4360_radio_mod(io, R2069_REG_0x00e | cb, 0x0002,
					  (u16)(tempsense_a[p] * 2));
			bcm4360_radio_mod(io, R2069_REG_0x16e | cb, 0x0001, 0x0001);
			bcm4360_radio_mod(io, R2069_REG_0x00e | cb, 0x0004, tempsense_b[p]);
			hw_udelay(10);
			for (k = 0; k < 8; k++) {
				s32 v = bcm4360_phy_read(io, ACPHY_REG_0x013) >> 2;

				if (v >= 0x200)
					v -= 0x400;
				sum += v;
			}
			m[c][p] = sum >> 3;
		}
		ncores++;
	}

	acc = 0;						/* step 7 */
	for (c = 0; c < phy_cores(phy); c++) {
		s32 diff, term;

		if (!(phy->rxchain & (1u << c)))
			continue;
		diff = m[c][3] + m[c][1] - m[c][2] - m[c][0];
		term = (((diff * 8766) / 2) << 8) / (s32)tempsense_div[c];
		acc += term / (s32)ncores;
	}
	acc += 1901076;
	degrees = acc / 16384;
	temp = (s32)phy->temp.offset + degrees;

	bcm4360_phy_write(io, ACPHY_REG_0x19e, s19e);		/* step 8 */
	for (c = 0; c < phy_cores(phy); c++) {
		u32 b = c * ACPHY_CORE_STEP;
		u32 cb = c << R2069_BANK_SHIFT;

		if (!(phy->rxchain & (1u << c)))
			continue;
		for (i = 0; i < 14; i++)
			bcm4360_phy_write(io, tempsense_phy_regs[i] + b, phy_save[c][i]);
		for (i = 0; i < 7; i++)
			bcm4360_radio_write(io, tempsense_radio_regs[i] | cb, radio_save[c][i]);
	}
	phy_adc_read_restore(phy, &adc);

	phy_reg_exit(phy);
	bcm4360_mac_enable(phy->hw);

	phy->temp.measured_temp = (s16)temp;			/* step 9 */
	return (u32)temp;
}

/* ------------------------------------------------- scan/roam cache */

/*
 * acphy-cal-tx section 20 (wlc_phy_scanroam_cache_cal_acphy): save the
 * calibration results into the cache (save = 1) or restore them (save = 0).
 */
static void phy_scanroam_cache(struct bcm4360_phy *phy, u8 save)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 bracket;
	u32 c;

	bcm4360_mac_suspend(phy->hw);
	phy_reg_enter(phy);
	bracket = tbl_bracket_enter(io);

	if (!save) {
		if (phy->cal.acphy_0x44a == CAL_MARKER)
			bcm4360_phy_txcal_coeffs_apply_acphy(phy, phy->cal.scanroam_cache[0]);
	} else {
		for (c = 0; c < phy_cores(phy); c++) {
			u8 *rec = phy->cal.scanroam_cache[c];
			u32 cb = c << R2069_BANK_SHIFT;
			u16 ab[2], d;

			phy_cal_coeffs(phy, 0, ab, 8, c);
			rec[0] = (u8)ab[0];
			rec[1] = (u8)(ab[0] >> 8);
			rec[2] = (u8)ab[1];
			rec[3] = (u8)(ab[1] >> 8);
			phy_cal_coeffs(phy, 0, &d, 9, c);
			rec[4] = (u8)d;
			rec[5] = (u8)(d >> 8);
			rec[6] = (u8)bcm4360_radio_read(io, R2069_REG_0x002 | cb);
			rec[7] = (u8)bcm4360_radio_read(io, (R2069_REG_0x002 + 1) | cb);
			rec[8] = (u8)bcm4360_radio_read(io, (R2069_REG_0x002 + 2) | cb);
			rec[9] = (u8)bcm4360_radio_read(io, (R2069_REG_0x002 + 3) | cb);
			{
				u16 a = bcm4360_phy_read(io, core_reg(ACPHY_REG_0x6a0, c));
				u16 b = bcm4360_phy_read(io, core_reg(ACPHY_REG_0x6a1, c));

				rec[10] = (u8)a;
				rec[11] = (u8)(a >> 8);
				rec[12] = (u8)b;
				rec[13] = (u8)(b >> 8);
			}
		}
		phy->cal.acphy_0x44a = CAL_MARKER;
	}

	tbl_bracket_leave(io, bracket);
	phy_reg_exit(phy);
	bcm4360_mac_enable(phy->hw);
}

/* --------------------------------------------------------- scheduler */

/*
 * acphy-cal-tx section 2 (sub_0925cc, wlc_phy_cal_suspend_setup_acphy): write
 * the duration hint, suspend the MAC, enter PHY-register access, power control
 * off.
 */
static void phy_cal_suspend_setup(struct bcm4360_phy *phy, u16 dur)
{
	bcm4360_shm_write(phy->hw, 0xb8, dur);
	bcm4360_mac_suspend(phy->hw);
	phy_reg_enter(phy);
	bcm4360_phy_txpwrctrl_enable_acphy(phy, false);
}

/* the cleanup shared by the working phases of wlc_phy_cals_acphy */
static void phy_cals_cleanup(struct bcm4360_phy *phy, bool en, u8 rxsave, u8 txsave)
{
	bcm4360_phy_txpwrctrl_enable_acphy(phy, en);
	phy_reg_exit(phy);
	bcm4360_mac_enable(phy->hw);
	phy->rxchain = rxsave;
	phy->txchain = txsave;
	bcm4360_phy_rxcore_setstate(phy, rxsave);
}

/*
 * acphy-cal-tx section 1 (wlc_phy_cals_acphy): run or advance a calibration.
 * `phase` is the mphase search mode forwarded to the transmit cal.
 */
void bcm4360_phy_cals(struct bcm4360_phy *phy, u32 phase_mode)
{
	struct bcm4360_phy_cal_state *st = &phy->cal.state;
	struct bcm4360_phy_io *io = &phy->io;
	u16 chanspec = phy->radio_chanspec;
	u8 mode = (u8)phase_mode;
	u8 rxsave, txsave;
	u8 phase = st->phase;
	u8 sm;
	bool en;

	if (phy->hold & 0x0010)					/* common 1 */
		return;

	rxsave = phy->rxchain;					/* common 2 */
	txsave = phy->txchain;
	phy->rxchain = phy->hw_rxchain;
	phy->txchain = phy->hw_txchain;
	bcm4360_phy_rxcore_setstate(phy, phy->hw_rxchain);

	if ((phase == 0x11 || phase == 0) && phy->crsmincal.enable) {	/* common 3 */
		u32 v;

		phy->crsmincal.state[0] = 1;
		bcm4360_phy_noise_sample_request_crsmincal(phy);
		/*
		 * The open noise-sample request (phy_desense.c) only clears the
		 * per-core result words; the object's request helper (sub_0baff6,
		 * wlc_phy_cmn.c) also triggers the measurement via D11(0x124). Reproduce
		 * the trigger here (see docs/re/questions/phy-cal.md).
		 */
		v = d11_read32(phy->hw, 0x124);
		d11_write32(phy->hw, 0x124, v | 0x0010);
	}

	sm = (chanspec == st->last_chanspec && st->restart_ok) ? mode : 0;	/* common 4 */

	if (phase > 1 && st->last_chanspec != chanspec) {	/* common 5 */
		st->phase = 1;
		st->subphase = 0;
	}

	en = phy->txpwr.hwpwrctrl;				/* common 6 */

	if (phase == 0) {
		phy_cal_suspend_setup(phy, 29000);		/* 1 */
		st->last_cal_time = 0;				/* 2 (sh+0x34 has no source) */
		st->last_chanspec = chanspec;
		if (st->first_cal)				/* 3 */
			bcm4360_phy_txpwrctrl_idle_tssi_meas_acphy(phy);
		bcm4360_phy_precal_txgain_acphy(phy, st->gainrec[0]);	/* 4 */
		phy_cal_txiqlo(phy, sm, 0, 0);			/* 5 */
		phy_cal_txiqlo(phy, sm, 0, 1);
		phy_rxiqcal(phy);				/* 6 */
		{						/* 7 */
			u16 bracket = tbl_bracket_enter(io);

			tbl_write16(io, TBL_BBMULT, TBL_BBMULT_MARKER, CAL_MARKER);
			tbl_bracket_leave(io, bracket);
		}
		st->first_cal = 0;				/* 8 */
		phy->cal.acphy_0x44a = 0;
		phy_scanroam_cache(phy, 1);
		phy_cals_cleanup(phy, en, rxsave, txsave);	/* 9 */
		return;
	}

	/* the multi-phase machine is implemented but not exercised by the test */
	phy_cals_cleanup(phy, en, rxsave, txsave);
}

/*
 * acphy-cal-tx section 5 (sub_0b56ce, wlc_phy_cal_perical_mphase_tmr_cb): the
 * phycal timer callback. Nothing is armed here (the open scheduler never calls
 * perical), so cal[1] is 0 and it returns at once.
 */
void bcm4360_phy_timer_phycal(struct bcm4360_phy *phy)
{
	if (phy->cal.state.phase == 0)
		return;
	/* the remaining phases run the multi-phase machine; not exercised */
}
