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
 * The receive calibration follows acphy-cal-rx.md X1 to X10 (every bandwidth
 * and band, any measured values); some polls run to their bounded time-out in
 * the model, which is expected.
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
#define ACPHY_REG_0x726		0x726	/* rxcal loopback override */
#define ACPHY_REG_0x732		0x732	/* rxcal save */
#define ACPHY_REG_0x733		0x733	/* rxcal save */
#define ACPHY_REG_0x73b		0x73b	/* rxcal save */
#define ACPHY_REG_0x73d		0x73d	/* rxcal save */
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
	u16 saved = tbl_bracket_enter(io);	/* sub_09bf99 brackets unconditionally */

	if (sel < 12) {
		const struct txiqlo_sel *s = &txiqlo_sels[sel];
		u32 off = s->o + core * s->d;

		if (mode == 0)
			bcm4360_tbl_read(io, TBL_BBMULT, s->n, off, 16, data);
		else
			bcm4360_tbl_write(io, TBL_BBMULT, s->n, off, 16, data);
	} else if (mode == 2) {			/* the second IQ copy (cal+0x54) */
		st->coef_copy[core][0] = data[0];
		st->coef_copy[core][1] = data[1];
	} else {
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
	tbl_bracket_leave(io, saved);
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

/*
 * Distribute the config word W into the ADC-clock/mux fields (tempsense step 3,
 * the same ten mods the tx-cal loopback set-up and the rx-cal use).
 */
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

/* the bandwidth config word W (tempsense step 1; the tx-cal loopback uses it too) */
static u16 tempsense_w(struct bcm4360_phy *phy)
{
	if (phy_bw(phy) == BCM4360_CHANSPEC_BW_80)
		return 0x7f8;
	if (phy_bw(phy) == BCM4360_CHANSPEC_BW_40)
		return (u16)(0x43e9 + (phy->acphy_116a == 0 ? 0x201 : 0));
	return 0xd5eb;
}

/* --------------------------------------------------------------- cordic */

/*
 * acphy-cal-rx.md X3: the integer helpers of wlc_phy_cmn.c. Angles are in
 * 1/65536 degree (0x1680000 = 360 degrees); T[] is the arctangent table of
 * .rodata+0x2ce950, atan(2^-i) in that unit.
 */
#define CORDIC_ITERS		18
#define CORDIC_SEED		0x9b75
#define CORDIC_FIX360		0x1680000
#define CORDIC_FIX180		0x0b40000
#define CORDIC_DEG90		90

static const s32 cordic_atan[CORDIC_ITERS] = {
	0x2d0000, 0x1a90a7, 0x0e0947, 0x072001, 0x03938b, 0x01ca38, 0x00e52a,
	0x007297, 0x00394c, 0x001ca6, 0x000e53, 0x000729, 0x000395, 0x0001ca,
	0x0000e5, 0x000073, 0x000039, 0x00001d,
};

/* a 32-bit left shift of a signed value, by the count modulo 32 (X3, "Conventions") */
static s32 shl32(s32 v, s32 n)
{
	return (s32)((u32)v << (n & 31));
}

/* an arithmetic right shift by the count modulo 32 */
static s32 sar32(s32 v, s32 n)
{
	return v >> (n & 31);
}

/* X3, wlc_phy_nbits: the number of significant bits of |x| (|-2^31| stays negative: 0) */
static s32 phy_nbits(s32 x)
{
	s32 a = (s32)((x < 0) ? (0u - (u32)x) : (u32)x);
	s32 n = 0;

	while ((a >> n) > 0)
		n++;
	return n;
}

/* X3, wlc_phy_sqrt_int: the square root of v rounded to the nearest integer */
static u32 phy_sqrt_int(u32 v)
{
	u32 r = 0, bit = 1u << 30;

	while (bit) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) | bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return v > r ? r + 1 : r;
}

/*
 * X3 / acphy-cal-tx section 13 (wlc_phy_cordic): the sine (*i_out, about
 * 65536 sin theta) and cosine (*q_out) of theta.
 */
static void phy_cordic(s32 theta, s32 *i_out, s32 *q_out)
{
	s32 s = (theta < 0) ? -1 : 1;
	s32 t = ((s32)((u32)theta + (u32)(s * CORDIC_FIX180)) % CORDIC_FIX360)
		- s * CORDIC_FIX180;							/* 1 */
	s32 d, sign = 1, x = CORDIC_SEED, y = 0, z = 0;
	u32 i;

	d = (t >= 0) ? ((t >> 15) + 1) >> 1 : -((((-t) >> 15) + 1) >> 1);		/* 2 */
	if (d > CORDIC_DEG90) {								/* 3 */
		t -= CORDIC_FIX180;
		sign = -1;
	} else if (t < 0 && d < -CORDIC_DEG90) {
		t += CORDIC_FIX180;
		sign = -1;
	}
	for (i = 0; i < CORDIC_ITERS; i++) {						/* 4 */
		s32 xn, yn;

		if (t > z) {
			z += cordic_atan[i];
			xn = x - (y >> i);
			yn = y + (x >> i);
		} else {
			z -= cordic_atan[i];
			xn = x + (y >> i);
			yn = y - (x >> i);
		}
		x = xn;
		y = yn;
	}
	*i_out = sign * y;								/* 5 */
	*q_out = sign * x;
}

/* X3, wlc_phy_inv_cordic: the angle of the vector (x, y), x > 0 */
static s32 phy_inv_cordic(s32 x, s32 y)
{
	s32 z = 0;
	u32 i;

	x = shl32(x, 4);
	y = shl32(y, 4);
	for (i = 0; i < CORDIC_ITERS; i++) {
		s32 xn, yn;

		if (y < 0) {
			z -= cordic_atan[i];
			xn = x - (y >> i);
			yn = y + (x >> i);
		} else {
			z += cordic_atan[i];
			xn = x + (y >> i);
			yn = y - (x >> i);
		}
		x = xn;
		y = yn;
	}
	return z;
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

		/* the gain codes are read then written; then the bbmult (sub_098751 /
		 * sub_09c4e4, each with its own bracket) */
		lo = tbl_read16(io, TBL_GAINCODE, TBL_GAINCODE_LO + c);
		mid = tbl_read16(io, TBL_GAINCODE, TBL_GAINCODE_MID + c);
		hi = tbl_read16(io, TBL_GAINCODE, TBL_GAINCODE_HI + c);
		s[0] = (u8)lo; s[1] = (u8)(lo >> 8);
		s[2] = (u8)mid; s[3] = (u8)(mid >> 8);
		s[4] = (u8)hi; s[5] = (u8)(hi >> 8);
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_LO + c, (u16)(n[0] | (n[1] << 8)));
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_MID + c, (u16)(n[2] | (n[3] << 8)));
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_HI + c, (u16)(n[4] | (n[5] << 8)));
		m = phy_get_bbmult(phy, c);
		s[8] = (u8)m; s[9] = (u8)(m >> 8);
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

	for (i = 0; i < 18; i++) {
		tbl_write16(io, TBL_BBMULT, i,
			    (u16)(((gainlut_a[i][0] * scale / 100) << 8) | gainlut_a[i][1]));
		tbl_write16(io, TBL_BBMULT, 0x20 + i,
			    (u16)(((gainlut_b[i][0] * scale / 100) << 8) | gainlut_b[i][1]));
	}
	tbl_bracket_leave(io, bracket);
}

/*
 * acphy-cal-tx section 19 (wlc_phy_populate_tx_loft_comp_tbl_acphy): the
 * thresholds T, the byte deltas HI / LO by k = 4c + 2 flag + (i <= T[c + 3
 * flag]), and the channels from which a core counts as "high" (L).
 */
#define LOFT_ENTRIES		128
#define LOFT_RADIO_SKIP		0x30b	/* radio id without the tables */
#define LOFT_TBL_STEP		0x20	/* TBL(0x42), 0x62, 0x82 for cores 0, 1, 2 */

static const u8 loft_thresh[6] = { 0x80, 0x80, 0x20, 0x80, 0x1a, 0x1c };
static const u8 loft_hi[12] = { 0, 0, 0, 0, 0, 0, 0xec, 0xf6, 0xf6, 0xf8, 0xf6, 0xfa };
static const u8 loft_lo[12] = { 0, 0, 0, 0, 0, 0, 0, 0xfb, 0xf2, 0xfc, 0xf6, 0xfb };
static const u8 loft_high_chan[BCM4360_PHY_CORES_MAX] = { 200, 149, 100 };

/*
 * acphy-cal-tx section 19 (wlc_phy_populate_tx_loft_comp_tbl_acphy): fill the
 * per-core LO-feedthrough tables with the calibrated LO-comp words d[c]; at
 * 5 GHz cores 1 and 2 get a per-entry delta added byte by byte. Only for radio
 * id != 0x30b (true for the 2069).
 */
static void txcal_populate_loft(struct bcm4360_phy *phy, const u16 *d)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 n = min(phy_cores(phy), (u32)BCM4360_PHY_CORES_MAX);
	u8 ch = (u8)(phy->radio_chanspec & 0xff);
	bool is5g = (phy->radio_chanspec & BCM4360_CHANSPEC_BAND) != BCM4360_CHANSPEC_BAND_2G;
	u8 flag[BCM4360_PHY_CORES_MAX] = { 0 };
	u16 bracket;
	u32 c, i;

	if (phy->ver.radio_id == LOFT_RADIO_SKIP)
		return;

	bracket = tbl_bracket_enter(io);				/* 1 */
	for (c = 0; c < n; c++)						/* 2 */
		flag[c] = loft_high_chan[c] <= ch;
	for (i = 0; i < LOFT_ENTRIES; i++) {				/* 3 */
		for (c = 0; c < n; c++) {
			u16 v = d[c];

			if (c > 0 && is5g) {
				u32 k = 4 * c + 2 * flag[c] +
					(i <= loft_thresh[c + 3 * flag[c]] ? 1 : 0);

				v = (u16)((((v >> 8) + loft_hi[k]) & 0xff) << 8 |
					  (((v & 0xff) + loft_lo[k]) & 0xff));
			}
			tbl_write16(io, TBL_LOFT_CORE0 + c * LOFT_TBL_STEP, i, v);
		}
	}
	tbl_bracket_leave(io, bracket);					/* 4 */
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
 * acphy-cal-tx section 15 (sub_0abc76, wlc_phy_cal_txiqlo_acphy): the whole
 * command list (mphase = 0) or the two commands of the phase cal[1] (mphase =
 * 1). Returns 0, or nonzero if the tone failed.
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
	u16 s19e, s40f, bracket;
	u16 w = tempsense_w(phy);
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
	bracket = tbl_bracket_enter(io);
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
		/*
		 * The "core-select" fields of the spec are actually the same
		 * bandwidth config word W distribution as tempsense step 3 (the
		 * object writes W = 0xd5eb at 20 MHz, not the core number c).
		 */
		phy_distribute_w(io, o, w);
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
	tbl_bracket_leave(io, bracket);

	txcal_txgain_save_set(phy, st->gainrec[0], gsave);	/* A.8 */
	bcm4360_phy_write(io, ACPHY_REG_0x382, CAL_ENGINE_ARM);	/* A.9 */

	/*
	 * B, as corrected (phy-calrx5g): while cal[1] < 3 the intermediate area
	 * cal+0x2c is loaded from zero (sm = 0) or from the final results cal+4
	 * (sm = 1); later phases keep it as it is. In every call the start values
	 * a,b (selection 0) and, for part 0, d, e, f (selections 1, 2, 3) are
	 * written from it.
	 */
	for (c = 0; c < n; c++) {
		u16 *v = st->coef_inter[c];
		u32 k;

		if (st->phase < 3)
			for (k = 0; k < BCM4360_CAL_COEFFS; k++)
				v[k] = sm ? st->coef_final[c][k] : 0;
		phy_cal_coeffs(phy, 1, &v[0], 0, c);
		if (part == 0) {
			phy_cal_coeffs(phy, 1, &v[2], 1, c);
			phy_cal_coeffs(phy, 1, &v[3], 2, c);
			phy_cal_coeffs(phy, 1, &v[4], 3, c);
		}
	}

	/*
	 * C. Choose the command list; single-shot: the whole list, multi-phase:
	 * two commands per phase (lo = cal[1] * 2 - 4 for part 0, - 0x1c for
	 * part 1; the loop is empty when lo > hi)
	 */
	if (sm == 0) {
		list = part ? txcal_cmd_sm0_p1 : txcal_cmd_sm0_p0;
	} else {
		list = part ? txcal_cmd_sm1_p1 : txcal_cmd_sm1_p0;
	}
	cnt = part ? 2 : 6;
	if (!mphase) {
		lo = 0;
		hi = n * cnt - 1;
	} else {
		lo = (u32)st->phase * 2 - (part ? 0x1c : 4);
		hi = min(lo + 1, n * cnt - 1);
	}

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

	/* F. Apply the results, when the list is finished (single-shot, phase 0xc / 0x10) */
	if (part == 0 && (!mphase || st->phase == 0x0c)) {
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
	} else if (part == 1 && (!mphase || st->phase == 0x10)) {
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
	return tone_ok;
}

/* ------------------------------------------------------------ rx IQ cal */

/* the radio is tuned to a 2.4 GHz channel */
static bool phy_is_2g(const struct bcm4360_phy *phy)
{
	return (phy->radio_chanspec & BCM4360_CHANSPEC_BAND) == BCM4360_CHANSPEC_BAND_2G;
}

/* wlc_phy_rx_iq_est_acphy steps 4a/4b: start the estimate and wait for it (SPINWAIT 10 ms) */
static void phy_rx_iq_est_run(struct bcm4360_phy_io *io)
{
	u32 cd = 10000 + 9;

	bcm4360_phy_mod(io, ACPHY_REG_0x270, 0x0001, 0x0001);
	while ((bcm4360_phy_read(io, ACPHY_REG_0x270) & 0x0001) && cd >= 10) {
		hw_udelay(10);
		cd -= 10;
	}
}

/* step 4c: core c's three sums {I*Q, I^2, Q^2}, high words first */
static void phy_rx_iq_est_read(struct bcm4360_phy_io *io, s32 *rec, u32 c)
{
	u32 o = c * ACPHY_CORE_STEP;

	rec[1] = (s32)(((u32)bcm4360_phy_read(io, ACPHY_REG_0x6c3 + o) << 16)
		       | bcm4360_phy_read(io, ACPHY_REG_0x6c2 + o));
	rec[2] = (s32)(((u32)bcm4360_phy_read(io, ACPHY_REG_0x6c5 + o) << 16)
		       | bcm4360_phy_read(io, ACPHY_REG_0x6c4 + o));
	rec[0] = (s32)(((u32)bcm4360_phy_read(io, ACPHY_REG_0x6c1 + o) << 16)
		       | bcm4360_phy_read(io, ACPHY_REG_0x6c0 + o));
}

/* X7 step 1: the four front-end registers of a core that is not measured */
static const u16 iq_est_iso_regs[4] = {
	ACPHY_REG_0x720, ACPHY_REG_0x728, ACPHY_REG_0x721, ACPHY_REG_0x729,
};

/*
 * acphy-cal-rx.md X7 (step 5 of wlc_phy_rx_iq_est_acphy): measure the cores one
 * after the other, the other present cores' front ends forced meanwhile.
 */
static void phy_rx_iq_est_isolated(struct bcm4360_phy *phy, s32 (*est)[3])
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 n = phy_cores(phy), c, d, k;

	for (c = 0; c < n; c++) {
		u16 addr[BCM4360_PHY_CORES_MAX * 4], val[BCM4360_PHY_CORES_MAX * 4];
		u32 saved = 0;

		for (d = 0; d < n; d++) {				/* 1 */
			u16 o = (u16)(d * ACPHY_CORE_STEP);

			if (d == c || !(phy->rxchain & (1u << d)))
				continue;
			for (k = 0; k < 4; k++) {
				addr[saved + k] = iq_est_iso_regs[k] + o;
				val[saved + k] = bcm4360_phy_read(io, addr[saved + k]);
			}
			saved += 4;
			bcm4360_phy_mod(io, ACPHY_REG_0x720 + o, 0x0001, 0x0001);
			bcm4360_phy_mod(io, ACPHY_REG_0x728 + o, 0x0001, 0);
			bcm4360_phy_mod(io, ACPHY_REG_0x721 + o, 0x0004, 0x0004);
			bcm4360_phy_mod(io, ACPHY_REG_0x729 + o, 0x0002, 0);
		}
		hw_udelay(1);						/* 2 */
		phy_rx_iq_est_run(io);
		while (saved--)						/* 3 */
			bcm4360_phy_write(io, addr[saved], val[saved]);
		if (!(bcm4360_phy_read(io, ACPHY_REG_0x270) & 0x0001))	/* 4 */
			phy_rx_iq_est_read(io, est[c], c);
	}
}

/*
 * acphy-cal-rx (wlc_phy_rx_iq_est_acphy): run the RX IQ-estimate accumulator for
 * `samples` samples and read back the per-core power sums {I*Q, I^2, Q^2}; with
 * `gain_change` set and the band's byte pi_ac+0x900 / 0x901 allowing it, the
 * cores one after the other (X7).
 */
static void phy_rx_iq_est(struct bcm4360_phy *phy, s32 (*est)[3], u16 samples,
			  u8 wait, u8 settle, u8 gain_change)
{
	struct bcm4360_phy_io *io = &phy->io;
	u8 g = phy_is_2g(phy) ? phy->cal.acphy_0x900 : phy->cal.acphy_0x901;
	u32 c;

	phy_gain_pulse(phy);					/* step 2 */
	bcm4360_phy_write(io, ACPHY_REG_0x272, samples);	/* step 3 */
	bcm4360_phy_mod(io, ACPHY_REG_0x271, 0x00ff, wait);
	bcm4360_phy_mod(io, ACPHY_REG_0x270, 0x0002, (u16)((settle * 2) & 0x1fe));

	if ((g & gain_change) != 0) {				/* step 5 (X7) */
		phy_rx_iq_est_isolated(phy, est);
		return;
	}
	phy_rx_iq_est_run(io);					/* steps 4a, 4b */
	if (!(bcm4360_phy_read(io, ACPHY_REG_0x270) & 0x0001))	/* step 4c */
		for (c = 0; c < phy_cores(phy); c++)
			phy_rx_iq_est_read(io, est[c], c);
}

/*
 * acphy-cal-rx X10 (sub_094b1a, wlc_phy_rxcal_radio_setup_acphy): radio TX->RX
 * loopback. The 4360 saves 0x20,0x21,0x22,0x23,0x3a,0x3d and writes 0, then
 * applies the overrides via 0x22/0x3a/0x20 (2.4 GHz) or 0x23/0x3d/0x21 (5 GHz).
 */
static const u16 rxcal_radio_regs[6] = {
	R2069_REG_0x020, R2069_REG_0x021, R2069_REG_0x022, R2069_REG_0x023,
	R2069_REG_0x03a, R2069_REG_0x03d,
};

/* X10: the radio revisions that take the first pair of mods on 0x20 / 0x21 */
static bool rxcal_radio_rev_low(u8 rev)
{
	return rev == 2 || rev == 3 || rev == 4 || rev == 8 || rev == 0x12 || rev == 0x18 ||
	       rev == 0x1a || rev == 0x22;
}

static void phy_rxcal_radio_setup(struct bcm4360_phy *phy, u16 save[][6])
{
	struct bcm4360_phy_io *io = &phy->io;
	bool is5g = !phy_is_2g(phy);
	u16 mix = is5g ? R2069_REG_0x023 : R2069_REG_0x022;
	u16 aux = is5g ? R2069_REG_0x03d : R2069_REG_0x03a;
	u16 ctl = is5g ? R2069_REG_0x021 : R2069_REG_0x020;
	u32 c, i;

	phy->cal.rxcal_loopback = 1;
	for (c = 0; c < phy_cores(phy); c++) {
		u32 b = c << R2069_BANK_SHIFT;

		for (i = 0; i < 6; i++)
			save[c][i] = bcm4360_radio_read(io, rxcal_radio_regs[i] | b);
		for (i = 0; i < 6; i++)
			bcm4360_radio_write(io, rxcal_radio_regs[i] | b, 0);
		/*
		 * X10: 2.4 GHz on 0x22 (mixer/loopback), 0x3a, 0x20; 5 GHz the
		 * same on 0x23, 0x3d, 0x21, with the loopback pair by PHY revision
		 */
		bcm4360_radio_mod(io, mix | b, 0x0100, 0);
		bcm4360_radio_mod(io, aux | b, 0x0010, 0);
		bcm4360_radio_mod(io, mix | b, 0x0020, 0);
		bcm4360_radio_mod(io, mix | b, 0x0040, 0);
		if (rxcal_radio_rev_low(phy->ver.radio_rev)) {
			bcm4360_radio_mod(io, ctl | b, 0x0020, 0);
			bcm4360_radio_mod(io, ctl | b, 0x0004, 0x0004);
		} else {
			bcm4360_radio_mod(io, ctl | b, 0x0020, 0x0020);
			bcm4360_radio_mod(io, ctl | b, 0x0004, 0);
		}
		if (is5g && phy->ver.phy_rev != 0) {
			bcm4360_radio_mod(io, mix | b, 0x0001, 0x0001);
			bcm4360_radio_mod(io, mix | b, 0x0200, 0x0200);
		} else {
			bcm4360_radio_mod(io, mix | b, 0x0200, 0x0200);
			bcm4360_radio_mod(io, mix | b, 0x0080, 0x0080);
		}
		bcm4360_radio_mod(io, ctl | b, 0x0003, 0);
		bcm4360_radio_mod(io, mix | b, 0x0006, 0);
	}
}

/* acphy-cal-rx (sub_095511, wlc_phy_rxcal_radio_restore_acphy) */
static void phy_rxcal_radio_restore(struct bcm4360_phy *phy, u16 save[][6])
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c, i;

	phy->cal.rxcal_loopback = 0;
	for (c = 0; c < phy_cores(phy); c++) {
		u32 b = c << R2069_BANK_SHIFT;

		for (i = 0; i < 6; i++)
			bcm4360_radio_write(io, rxcal_radio_regs[i] | b, save[c][i]);
	}
}

/* the angle of a sine term s (X4 step 6, X5 step 2.3): inv_cordic(2 sqrt(1 - (s/2)^2), s) */
static s32 phy_asin_term(s32 s)
{
	s32 h = s >> 1;
	u32 c = phy_sqrt_int(0x40000000u - (u32)h * (u32)h);	/* 32-bit arithmetic */

	return phy_inv_cordic((s32)(2 * c), s);
}

/* X4 steps 7 and 8: the shift of a value for a scale of m bits around 10 */
static s32 phy_scale_m(s32 v, s32 m)
{
	return m > 10 ? sar32(v, m - 10) : shl32(v, 10 - m);
}

/* nbits rounded up to even (X4 step 7, X5 step 2.2) */
static s32 phy_nbits_even(s32 v)
{
	s32 m = phy_nbits(v);

	return m + (m & 1);
}

/*
 * acphy-cal-rx X4 (sub_097809, wlc_phy_calc_iqcc_acphy): from {I*Q, I^2, Q^2}
 * compute {angle, gain ratio (0x400 = 1.0), sine term}. No register access.
 */
static void phy_calc_iqcc(const s32 *in, s32 *out)
{
	s32 iq = in[0], i2 = in[1], q2 = in[2];
	s32 niq = phy_nbits(iq);					/* 1 */
	s32 n = max(phy_nbits(i2), phy_nbits(q2));
	s32 ri, rq, den, num, sn, d, m, dd, ii;

	if (n <= 30) {							/* 2 */
		ri = (s32)phy_sqrt_int((u32)shl32(i2, 30 - n));
		rq = (s32)phy_sqrt_int((u32)shl32(q2, 30 - n));
	} else {
		ri = (s32)phy_sqrt_int((u32)sar32(i2, n - 30));
		rq = (s32)phy_sqrt_int((u32)sar32(q2, n - 30));
	}
	den = (s32)((u32)ri * (u32)rq);					/* 3 */
	if (n > niq + 16)
		den = shl32(den, n - niq - 16);
	else
		den = sar32(den, niq + 16 - n);
	num = shl32((s32)(0u - (u32)iq), 30 - niq);			/* 4 */
	if (num > 0)
		num += den >> 1;
	else
		num -= den >> 1;
	sn = den ? num / den : 0;					/* 5 */
	out[2] = sn;
	out[0] = phy_asin_term(sn);					/* 6 */
	d = (s32)((u32)q2 - (u32)i2);					/* 7 */
	m = phy_nbits_even(d);
	dd = shl32(d, 30 - m);
	ii = phy_scale_m(i2, m);
	if (dd > 0)
		dd += ii >> 1;
	else
		dd -= ii >> 1;
	out[1] = ii ? (s32)phy_sqrt_int((u32)(dd / ii + 0x100000)) : 0x400;	/* 8 */
}

/* acphy-cal-rx (sub_090fcb, wlc_phy_rxiqcc_acphy): the RX IQ coeff accessor */
static void phy_rxiqcc(struct bcm4360_phy *phy, u8 mode, u16 *ab, u32 core)
{
	struct bcm4360_phy_io *io = &phy->io;

	if (mode == 0) {
		ab[0] = bcm4360_phy_read(io, core_reg(ACPHY_REG_0x6a0, core));
		ab[1] = bcm4360_phy_read(io, core_reg(ACPHY_REG_0x6a1, core));
	} else if (mode == 1) {
		bcm4360_phy_write(io, core_reg(ACPHY_REG_0x6a0, core), ab[0]);
		bcm4360_phy_write(io, core_reg(ACPHY_REG_0x6a1, core), ab[1]);
	} else {
		bcm4360_phy_write(io, core_reg(ACPHY_REG_0x6a0, core), 0);
		bcm4360_phy_write(io, core_reg(ACPHY_REG_0x6a1, core), 0);
	}
}

/* X8: the rows R_0 .. R_10 of the 11-tap filter (.rodata+0x2cc960, DAT_0054da90) */
#define DIGFILT_TAPS	11
#define DIGFILT_ROWS	11
#define ACPHY_REG_0x6a4	0x6a4	/* first of the 11 taps, + c * ACPHY_CORE_STEP */

static const u16 digfilt_rows[DIGFILT_ROWS][DIGFILT_TAPS] = {
	{ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0400, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	{ 0xfff4, 0x000f, 0xffec, 0x001e, 0xffc4, 0x0400, 0x003d, 0xffe2, 0x0014, 0xfff1, 0x000c },
	{ 0xffe8, 0x001e, 0xffd8, 0x003c, 0xff88, 0x0400, 0x007a, 0xffc3, 0x0029, 0xffe2, 0x0018 },
	{ 0xffdc, 0x002d, 0xffc4, 0x005b, 0xff4c, 0x0400, 0x00b8, 0xffa4, 0x003d, 0xffd2, 0x0025 },
	{ 0xffd6, 0x0034, 0xffbb, 0x0067, 0xff32, 0x0400, 0x00d3, 0xff97, 0x0046, 0xffcc, 0x002a },
	{ 0xffcc, 0x0041, 0xffaa, 0x0081, 0xff00, 0x0400, 0x0108, 0xff7d, 0x0057, 0xffbf, 0x0034 },
	{ 0xffc2, 0x004e, 0xff99, 0x009b, 0xfecd, 0x03ff, 0x013f, 0xff62, 0x0069, 0xffb2, 0x003f },
	{ 0xffb7, 0x005b, 0xff87, 0x00b4, 0xfe9b, 0x03ff, 0x0175, 0xff48, 0x007a, 0xffa4, 0x0049 },
	{ 0xffad, 0x0068, 0xff76, 0x00ce, 0xfe69, 0x03ff, 0x01ac, 0xff2d, 0x008c, 0xff97, 0x0054 },
	{ 0xffa3, 0x0075, 0xff65, 0x00e7, 0xfe38, 0x03ff, 0x01e3, 0xff12, 0x009e, 0xff8a, 0x005e },
	{ 0xff98, 0x0081, 0xff54, 0x0101, 0xfe06, 0x03fe, 0x021b, 0xfef7, 0x00b0, 0xff7c, 0x0069 },
};

/*
 * acphy-cal-rx X8 (sub_09107b, wlc_phy_rxcal_digfilt_acphy): mode 0 clears
 * PHY(0x211) bit 0 and the per-core slopes; mode 1 (after an 80 MHz
 * calibration) programs the 11 taps of each core from the row of its clamped
 * slope, mirrored for a negative slope, and switches the filter on.
 */
static void phy_rxcal_digfilt(struct bcm4360_phy *phy, u8 mode)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c, i;

	if (mode == 0) {
		bcm4360_phy_mod(io, ACPHY_REG_0x211, 0x0001, 0);
		for (c = 0; c < phy_cores(phy); c++)
			phy->cal.rxcal_digfilt[c] = 0;
		return;
	}
	for (c = 0; c < phy_cores(phy); c++) {
		s32 v = phy->cal.rxcal_digfilt[c];
		s32 a = v * ((v >> 31) | 1);			/* |v| as the object computes it */
		u32 j = (a < 0 || a > 10) ? 10 : (u32)a;
		u16 o = (u16)(c * ACPHY_CORE_STEP);

		bcm4360_phy_mod(io, ACPHY_REG_0x210, 0x0007, 4);
		bcm4360_phy_mod(io, ACPHY_REG_0x211, 0x0004, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x211, 0x0002, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x212, 0x000f, 10);
		for (i = 0; i < DIGFILT_TAPS; i++)
			bcm4360_phy_write(io, ACPHY_REG_0x6a4 + o + i,
					  digfilt_rows[j][v >= 0 ? i : DIGFILT_TAPS - 1 - i]);
	}
	bcm4360_phy_mod(io, ACPHY_REG_0x211, 0x0001, 0x0001);
}

/*
 * Step 7 saves 26 per-core PHY registers in a specific (non-ascending) order;
 * 0x73e is read last (after the bbmult and gain-code words), the other 25 in
 * rxcal_save25. sub_09cf53 writes them back in rxcal_restore25 (0x73e and 0x678
 * first, and 0x726/0x727 are swapped versus the save). Saved values are stored
 * by (reg - 0x720), so both orders index the same slots.
 */
static const u16 rxcal_save25[25] = {
	0x720, 0x721, 0x722, 0x723, 0x724, 0x725, 0x726, 0x727, 0x728, 0x729,
	0x732, 0x733, 0x730, 0x731, 0x734, 0x735, 0x737, 0x738, 0x736, 0x739,
	0x73a, 0x73b, 0x73c, 0x73d, 0x747,
};

static const u16 rxcal_restore25[25] = {
	0x720, 0x721, 0x722, 0x723, 0x724, 0x725, 0x727, 0x726, 0x728, 0x729,
	0x732, 0x733, 0x730, 0x731, 0x734, 0x735, 0x737, 0x738, 0x736, 0x739,
	0x73a, 0x73b, 0x73c, 0x73d, 0x747,
};

#define RXCAL_SLOTS	0x28	/* (reg - 0x720) for 0x720..0x747 */

/*
 * the fixed per-core override mods of step 7 (after the W distribution), in two
 * parts: between them the 5 GHz mod of PHY(0x729) bit 5 (rxcal_override_5g)
 */
static const struct lb_mod rxcal_override_a[] = {
	{ 0x729, 0x0020, 0 }, { 0x721, 0x0020, 0x0020 },
	{ 0x729, 0x0040, 0 }, { 0x721, 0x0040, 0x0040 },
	{ 0x729, 0x0800, 0 }, { 0x721, 0x0800, 0x0800 },
	{ 0x729, 0x1000, 0 }, { 0x721, 0x1000, 0x1000 },
	{ 0x729, 0xe000, 0 }, { 0x721, 0x2000, 0x2000 },
	{ 0x728, 0x3800, 0 }, { 0x721, 0x4000, 0x4000 },
	{ 0x728, 0x0002, 0 },			/* R = 0 for radio rev 4 */
	{ 0x720, 0x0002, 0x0002 },
};

/* step 7: on 5 GHz for PHY revisions other than 0 (radio major revision 0: once) */
static const struct lb_mod rxcal_override_5g = { 0x729, 0x0020, 0x0020 };

static const struct lb_mod rxcal_override[] = {
	{ 0x729, 0x0200, 0x0200 }, { 0x721, 0x0200, 0x0200 },
	{ 0x736, 0x0080, 0 }, { 0x724, 0x0080, 0x0080 },
	{ 0x736, 0x0004, 0 }, { 0x724, 0x0004, 0x0004 },
	{ 0x736, 0x0002, 0 }, { 0x724, 0x0002, 0x0002 },
	{ 0x736, 0x0040, 0 }, { 0x724, 0x0040, 0x0040 },
	{ 0x736, 0x0100, 0 }, { 0x724, 0x0100, 0x0100 },
	{ 0x736, 0x0010, 0 }, { 0x724, 0x0010, 0x0010 },
	{ 0x736, 0x0200, 0x0200 }, { 0x724, 0x0200, 0x0200 },
	{ 0x736, 0x0020, 0x0020 }, { 0x724, 0x0020, 0x0020 },
	{ 0x736, 0x0008, 0x0008 }, { 0x724, 0x0008, 0x0008 },
	{ 0x736, 0x0001, 0x0001 }, { 0x724, 0x0001, 0x0001 },
};

/* the per-core save area of the rx cal (step 7); single-shot, so a local */
struct rxcal_save {
	u16 s40f, s401, s19e;			/* globals */
	u16 phy[BCM4360_PHY_CORES_MAX][RXCAL_SLOTS];	/* indexed by reg - 0x720 */
	u16 r678[BCM4360_PHY_CORES_MAX];
	u16 bbmult[BCM4360_PHY_CORES_MAX];
	u16 gaincode[BCM4360_PHY_CORES_MAX][3];	/* TBL(7)[0x100/0x103/0x106] */
	u8 txindex[BCM4360_PHY_CORES_MAX];
};

/*
 * acphy-cal-rx (sub_0addfa step 7): save + override the PHY RX path. The bracket
 * and the whole override are inside the function's table bracket.
 */
static void phy_rxcal_phy_setup(struct bcm4360_phy *phy, struct rxcal_save *sv, u16 w, u32 bwix)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c, i;

	sv->s19e = tbl_bracket_enter(io);
	sv->s40f = bcm4360_phy_read(io, ACPHY_REG_0x40f);
	bcm4360_phy_mod(io, ACPHY_REG_0x40f, 0x0200, 0);

	/* save loop (all cores) */
	for (c = 0; c < phy_cores(phy); c++) {
		u32 b = c * ACPHY_CORE_STEP;

		for (i = 0; i < 25; i++)
			sv->phy[c][rxcal_save25[i] - 0x720] =
				bcm4360_phy_read(io, rxcal_save25[i] + b);
		sv->bbmult[c] = phy_get_bbmult(phy, c);
		sv->gaincode[c][0] = tbl_read16(io, TBL_GAINCODE, TBL_GAINCODE_LO + c);
		sv->gaincode[c][1] = tbl_read16(io, TBL_GAINCODE, TBL_GAINCODE_MID + c);
		sv->gaincode[c][2] = tbl_read16(io, TBL_GAINCODE, TBL_GAINCODE_HI + c);
		sv->phy[c][0x73e - 0x720] = bcm4360_phy_read(io, ACPHY_REG_0x73e + b);
		sv->txindex[c] = phy->txpwr.index[c];
	}

	/* 0x401 setup (once, between the save and override loops) */
	sv->s401 = bcm4360_phy_read(io, ACPHY_REG_0x401);
	bcm4360_phy_mod(io, ACPHY_REG_0x401, 0x0007, phy->hw_rxchain);
	bcm4360_phy_mod(io, ACPHY_REG_0x401, 0x7000, 0);

	/* override loop (all cores) */
	for (c = 0; c < phy_cores(phy); c++) {
		u32 b = c * ACPHY_CORE_STEP;
		u16 g;

		bcm4360_phy_mod(io, ACPHY_REG_0x73e + b, 0x0010, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x73e + b, 0x0020, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x73e + b, 0x1000, 0x1000);
		bcm4360_phy_mod(io, ACPHY_REG_0x721 + b, 0x0001, 0x0001);
		bcm4360_phy_mod(io, ACPHY_REG_0x729 + b, 0x0001, 0x0001);
		phy_distribute_w(io, b, w);
		for (i = 0; i < ARRAY_SIZE(rxcal_override_a); i++)
			bcm4360_phy_mod(io, rxcal_override_a[i].reg + b,
					rxcal_override_a[i].mask, rxcal_override_a[i].val);
		if (!phy_is_2g(phy) && phy->ver.phy_rev != 0)
			bcm4360_phy_mod(io, rxcal_override_5g.reg + b, rxcal_override_5g.mask,
					rxcal_override_5g.val);
		for (i = 0; i < ARRAY_SIZE(rxcal_override); i++)
			bcm4360_phy_mod(io, rxcal_override[i].reg + b,
					rxcal_override[i].mask, rxcal_override[i].val);
		g = tbl_read16(io, TBL_GAINCODE,
			       (bwix == 2) ? (c * 2 + 0x441) : (c * 0x10 + 0x140 + bwix));
		bcm4360_phy_mod(io, ACPHY_REG_0x735 + b, 0x0700, (u16)((g & 0x7) << 8));
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + b, 0x0008, 0x0008);
		bcm4360_phy_mod(io, ACPHY_REG_0x735 + b, 0x3800, (u16)((g & 0x38) << 8));
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + b, 0x0010, 0x0010);
		bcm4360_phy_mod(io, ACPHY_REG_0x737 + b, 0x00ff, (u16)(g >> 6));
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + b, 0x0200, 0x0200);
		bcm4360_phy_mod(io, ACPHY_REG_0x735 + b, 0x4000, 0);
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + b, 0x0020, 0x0020);
		bcm4360_phy_mod(io, ACPHY_REG_0x735 + b, 0x0001, 0x0001);
		bcm4360_phy_mod(io, ACPHY_REG_0x723 + b, 0x0001, 0x0001);
		bcm4360_phy_mod(io, ACPHY_REG_0x729 + b, 0x0100, 0x0100);
		bcm4360_phy_mod(io, ACPHY_REG_0x721 + b, 0x0100, 0x0100);
		sv->r678[c] = bcm4360_phy_read(io, ACPHY_REG_0x678 + b);
		bcm4360_phy_mod(io, ACPHY_REG_0x678 + b, 0x0001, 0);
	}

	tbl_bracket_leave(io, sv->s19e);
}

/* acphy-cal-rx (sub_09cf53, wlc_phy_rxcal_phy_restore_acphy): the step-7 restore */
static void phy_rxcal_phy_restore(struct bcm4360_phy *phy, struct rxcal_save *sv)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 br = tbl_bracket_enter(io);
	u32 c, i;

	bcm4360_phy_write(io, ACPHY_REG_0x401, sv->s401);
	for (c = 0; c < phy_cores(phy); c++) {
		u32 b = c * ACPHY_CORE_STEP;

		/* rewrite the saved gain code (unbracketed, inside the outer bracket) */
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_LO + c, sv->gaincode[c][0]);
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_MID + c, sv->gaincode[c][1]);
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_HI + c, sv->gaincode[c][2]);
		bcm4360_phy_txpwr_by_index_acphy(phy, (u8)(1u << c), (s8)sv->txindex[c]);
		phy_set_bbmult(phy, sv->bbmult[c], c);
		bcm4360_phy_write(io, ACPHY_REG_0x73e + b, sv->phy[c][0x73e - 0x720]);
		bcm4360_phy_write(io, ACPHY_REG_0x678 + b, sv->r678[c]);
		for (i = 0; i < 25; i++)
			bcm4360_phy_write(io, rxcal_restore25[i] + b,
					  sv->phy[c][rxcal_restore25[i] - 0x720]);
	}
	bcm4360_phy_write(io, ACPHY_REG_0x40f, sv->s40f);
	phy_force_rfseq(phy, 2);
	tbl_bracket_leave(io, br);
}

/*
 * acphy-cal-rx X9 (sub_0ad89a, wlc_phy_rxcal_txgain_setup_acphy): the gain
 * tables {s3, s7, s5} by gain index, 5 GHz (.rodata+0x2ccab0) and 2.4 GHz
 * (.rodata+0x2cca60).
 */
#define RXCAL_GAINS		11
#define RXCAL_GI_MAX		10
#define RXCAL_P_CLIMB		0x0b58	/* dir 1: climb while p is below */
#define RXCAL_P_LOW		0x0b56	/* dir 0: in the window above */
#define RXCAL_P_HIGH		0x169e	/* dir 0 / 2: too strong from */
#define RXCAL_S5_UNITY		0xff	/* low byte of s5: write the unity gain */

static const u16 rxcal_gain_5g[RXCAL_GAINS][3] = {
	{ 0, 0, 0 }, { 0, 1, 0 }, { 0, 2, 0 }, { 0, 3, 0 }, { 0, 4, 0 },
	{ 1, 4, 0 }, { 2, 4, 0 }, { 3, 4, 0 }, { 4, 4, 0 }, { 5, 4, 0 },
	{ 5, 5, 0 },
};

static const u16 rxcal_gain_2g[RXCAL_GAINS][3] = {
	{ 0, 0, 10 }, { 0, 1, 10 }, { 0, 2, 10 }, { 0, 3, 10 }, { 0, 4, 10 },
	{ 0, 5, 10 }, { 1, 5, 10 }, { 2, 5, 10 }, { 3, 5, 10 }, { 4, 5, 10 },
	{ 5, 5, 10 },
};

/* the state of the gain search of one core (X9 step 2) */
struct rxcal_gs {
	u16 seed;
	u8 gi;
	u8 dir;
	bool done;
};

/* X9 step 3: apply the gain of index gs->gi to core c */
static void rxcal_gs_apply(struct bcm4360_phy *phy, const struct rxcal_gs *gs, u32 c,
			   const u16 (*tab)[3], u16 paldo, u16 u)
{
	struct bcm4360_phy_io *io = &phy->io;
	const u16 *t = tab[gs->gi];
	u16 b = (u16)(c * ACPHY_CORE_STEP);

	bcm4360_phy_write(io, ACPHY_REG_0x730 + b, (u16)((gs->seed << 6) | (paldo << 3)));
	bcm4360_phy_write(io, ACPHY_REG_0x731 + b, phy_is_2g(phy) ? 0 : 4);
	bcm4360_phy_write(io, ACPHY_REG_0x734 + b, (u16)((t[0] << 3) | t[1]));
	bcm4360_phy_mod(io, ACPHY_REG_0x722 + b, 0x0002, 0x0002);
	bcm4360_phy_mod(io, ACPHY_REG_0x722 + b, 0x0004, 0x0004);
	bcm4360_phy_mod(io, ACPHY_REG_0x722 + b, 0x0008, 0x0008);
	if ((t[2] & 0xff) == RXCAL_S5_UNITY) {	/* not reached with these tables */
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_LO + c, 0x00ff);
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_MID + c, 0xffff);
		tbl_write16(io, TBL_GAINCODE, TBL_GAINCODE_HI + c, u);
	} else {
		bcm4360_phy_txpwr_by_index_acphy(phy, (u8)(1u << c), (s8)t[2]);
	}
}

/* X9 step 5: one step of a core's walk from its measured power p */
static void rxcal_gs_step(struct rxcal_gs *gs, u32 p)
{
	if (gs->dir == 1) {
		if (p < RXCAL_P_CLIMB)
			gs->gi++;
		else
			gs->done = true;
	} else if (gs->dir == 0) {
		if (p >= RXCAL_P_HIGH) {
			gs->dir = 2;
			do {
				if (gs->gi < 2)
					break;
				p >>= 1;
				gs->gi--;
			} while (p > RXCAL_P_HIGH - 1);
		} else if (p > RXCAL_P_LOW) {
			gs->done = true;
		} else {
			gs->dir = 1;
			if (p != 0) {
				do {
					if (gs->gi > 7)
						break;
					p *= 2;
					gs->gi++;
				} while (p < RXCAL_P_LOW + 1);
			}
		}
	} else {
		if (p < RXCAL_P_HIGH)
			gs->done = true;
		else
			gs->gi--;
	}
	if (gs->gi > RXCAL_GI_MAX)	/* also 255 after a decrement from 0 */
		gs->done = true;
}

/*
 * acphy-cal-rx X9 (sub_0ad89a, wlc_phy_rxcal_txgain_setup_acphy): the gain
 * search - per core the loopback gain index is walked until the tone's power
 * lies in the window.
 */
static void phy_rxcal_gain_search(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 n = phy_cores(phy);
	bool is5g = !phy_is_2g(phy);
	const u16 (*tab)[3] = is5g ? rxcal_gain_5g : rxcal_gain_2g;
	u16 paldo = (is5g && phy->ver.phy_rev != 0) ? 6 : 0;		/* 1 */
	s32 khz = 1000 << phy_bwix(phy);	/* 4: 1000 / 2000 / 4000 for 20 / 40 / 80 MHz */
	struct rxcal_gs gs[BCM4360_PHY_CORES_MAX];
	u16 br, br2, u;
	u32 c;
	bool more;

	br = tbl_bracket_enter(io);					/* 2 */
	for (c = 0; c < n; c++) {
		gs[c].seed = (u16)((bcm4360_phy_read(io, ACPHY_REG_0x6dc + c * ACPHY_CORE_STEP)
				    & 0x0780) >> 7);
		gs[c].gi = is5g ? 4 : 0;
		gs[c].dir = 0;
		gs[c].done = false;
	}
	br2 = tbl_bracket_enter(io);	/* sub_09868f: TBL(0x20)[0x32], one 48-bit entry */
	{
		u8 gainword[6];

		bcm4360_tbl_read(io, 0x20, 1, 0x32, 0x30, gainword);
		u = (u16)(gainword[4] | (gainword[5] << 8));	/* its third 16-bit word */
	}
	tbl_bracket_leave(io, br2);

	do {
		s32 est[BCM4360_PHY_CORES_MAX][3] = { { 0 } };

		for (c = 0; c < n; c++)					/* 3 */
			if (!gs[c].done)
				rxcal_gs_apply(phy, &gs[c], c, tab, paldo, u);

		bcm4360_phy_tx_tone_acphy(phy, khz, 0xb5, 0, 0, 0);	/* 4 */
		phy_rx_iq_est(phy, est, 0x400, 0x20, 0, 0);
		bcm4360_phy_stopplayback_acphy(phy);

		more = false;
		for (c = 0; c < n; c++) {				/* 5 */
			u32 p;

			if (gs[c].done)
				continue;
			p = (((u32)est[c][2] + 0x200) >> 10) + (((u32)est[c][1] + 0x200) >> 10);
			rxcal_gs_step(&gs[c], p);
			if (!gs[c].done)
				more = true;
		}
	} while (more);							/* 6 */

	tbl_bracket_leave(io, br);
}

/* X2: the tone amplitudes A_k by bandwidth (`two` set at 80 MHz, and at 40 MHz on rev 3) */
static const s8 rxcal_tones_20[2] = { 2, -2 };
static const s8 rxcal_tones_40[2] = { 4, -4 };
static const s8 rxcal_tones_80[6] = { 8, -8, 24, -24, 32, -32 };
static const s8 rxcal_tones_40_two[4] = { 4, -4, 15, -15 };	/* rev 3 only */

#define RXCAL_TONES_MAX		6
#define RXCAL_EST_SAMPLES	0x4000
#define RXCAL_EST_SAMPLES_TWO	0x3000
#define RXCAL_TONE_AMP		0xb5
#define RXCAL_IMG_MIN_RATIO	41	/* X5 2.3: below this the image estimate is not used */

/* X2: `two` (pi_ac+0x8c4) and the tone list of the bandwidth */
static u32 rxcal_tone_list(const struct bcm4360_phy *phy, bool *two, const s8 **amp)
{
	u16 bw = phy_bw(phy);

	*two = bw == BCM4360_CHANSPEC_BW_80 ||
	       (bw == BCM4360_CHANSPEC_BW_40 && phy->ver.phy_rev == 3);
	if (bw == BCM4360_CHANSPEC_BW_80) {
		*amp = rxcal_tones_80;
		return ARRAY_SIZE(rxcal_tones_80);
	}
	if (bw == BCM4360_CHANSPEC_BW_40) {
		*amp = *two ? rxcal_tones_40_two : rxcal_tones_40;
		return *two ? ARRAY_SIZE(rxcal_tones_40_two) : ARRAY_SIZE(rxcal_tones_40);
	}
	*amp = rxcal_tones_20;
	return ARRAY_SIZE(rxcal_tones_20);
}

/*
 * acphy-cal-rx X5: the angle of tone k on one core from the loopback estimate
 * e0 and, with img, the image estimate e1; the gain ratio is e0's.
 */
static void rxcal_tone_result(const s32 *e0, const s32 *e1, bool img, s32 *angle, s32 *gain)
{
	s32 r0[3], r1[3], p0, p1, m, p0s;
	u32 r;

	phy_calc_iqcc(e0, r0);						/* 1 */
	*angle = r0[0];
	*gain = r0[1];							/* 4 */
	if (!img)							/* 3 */
		return;
	phy_calc_iqcc(e1, r1);						/* 2.1 */
	p1 = (s32)((u32)e1[1] + (u32)e1[2]);				/* 2.2 */
	p0 = (s32)((u32)e0[1] + (u32)e0[2]);
	m = phy_nbits_even(p1);
	p0s = phy_scale_m(p0, m);
	if (p0s == 0)							/* 2.3 */
		return;
	r = phy_sqrt_int((u32)((s32)((u32)(p0s >> 1) + (u32)shl32(p1, 30 - m)) / p0s));
	if (r <= RXCAL_IMG_MIN_RATIO)
		return;
	*angle = phy_asin_term(r0[2] + ((s32)(r * (u32)(r0[2] - r1[2])) >> 10));
}

/*
 * acphy-cal-rx X6 steps 1 to 4: the coefficients (a, b) of one core from its
 * angles and gains over the tones, and with `two` the slope of the digital
 * filter.
 */
static void phy_rxcal_coeffs(struct bcm4360_phy *phy, const s32 *angle, const s32 *gain,
			     const s8 *amp, u32 cnt, bool two, u32 core, u16 *ab)
{
	s32 sxx = 0, sy = 0, sxy = 0, sg = 0, half = (s32)(cnt >> 1), g, q, w0, w1;
	u32 k;

	for (k = 0; k < cnt; k++) {
		sxx += amp[k] * amp[k];
		sy += angle[k];
		sxy += amp[k] * angle[k];
		sg += gain[k];
	}
	g = (sg + half) / (s32)cnt;					/* 1 */
	q = sy >= 0 ? sy + half : sy - half;				/* 2 */
	phy_cordic(q / (s32)cnt, &w0, &w1);
	ab[0] = (u16)(((((g * w0) >> 15) + 1) >> 1) & 0x3ff);		/* 3 */
	ab[1] = (u16)(((((g * w1) >> 15) + 1) >> 1) & 0x3ff);
	if (two) {							/* 4 */
		u16 bw = phy_bw(phy);
		s32 kk = bw == BCM4360_CHANSPEC_BW_80 ? 30 : bw == BCM4360_CHANSPEC_BW_40 ? 15 : 8;

		phy->cal.rxcal_digfilt[core] = ((((-sxy * kk) / sxx) >> 14) + 1) >> 1;
	}
}

/*
 * acphy-cal-rx (sub_0addfa, wlc_phy_rxiqcal_acphy) with X1 to X10: the receive
 * IQ-imbalance calibration at every bandwidth and band.
 */
static void phy_rxiqcal(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_cal_state *st = &phy->cal.state;
	u32 n = phy_cores(phy);
	u32 bwix = phy_bwix(phy);
	u16 w = tempsense_w(phy);
	struct rxcal_save sv;
	u16 rxgain[BCM4360_PHY_CORES_MAX][2];
	u16 radio_save[BCM4360_PHY_CORES_MAX][6];
	s32 angle[BCM4360_PHY_CORES_MAX][RXCAL_TONES_MAX];
	s32 gain[BCM4360_PHY_CORES_MAX][RXCAL_TONES_MAX];
	u16 ab[BCM4360_PHY_CORES_MAX][2];
	u32 rev = phy->ver.phy_rev, cnt, k, c;
	const s8 *amp;
	bool two;
	/* step 1 (X2): the image second estimate, not for rev 2, 3, 5, 6, nor rev 1 at 5 GHz */
	bool img = !(rev == 2 || rev == 3 || rev == 5 || rev == 6 ||
		     (rev == ACPHY_REV_1 && !phy_is_2g(phy)));

	bcm4360_phy_stay_in_carriersearch(phy, true);		/* step 2 */
	phy_force_rfseq(phy, 2);

	for (c = 0; c < n; c++)					/* step 3 */
		phy_rxiqcc(phy, 2, NULL, c);

	phy_rxcal_digfilt(phy, 0);				/* step 4 */

	for (c = 0; c < n; c++) {				/* step 5 */
		u16 gains[2];

		if (!(phy->rxchain & (1u << c)))
			continue;
		phy_cal_coeffs(phy, 0, rxgain[c], 8, c);
		gains[0] = st->coef_copy[c][0];
		gains[1] = st->coef_copy[c][1];
		phy_cal_coeffs(phy, 1, gains, 8, c);
	}

	phy_rxcal_phy_setup(phy, &sv, w, bwix);			/* steps 6, 7 */
	phy_rxcal_radio_setup(phy, radio_save);			/* step 8 */
	phy_rxcal_gain_search(phy);				/* step 9 */

	cnt = rxcal_tone_list(phy, &two, &amp);			/* step 10 (X2) */

	for (k = 0; k < cnt; k++) {				/* step 11 */
		s32 est0[BCM4360_PHY_CORES_MAX][3] = { { 0 } };
		s32 est1[BCM4360_PHY_CORES_MAX][3] = { { 0 } };
		s32 f = (s16)(amp[k] * 1000) >> 1;		/* A_k * 500 kHz */

		bcm4360_phy_tx_tone_acphy(phy, f, RXCAL_TONE_AMP, 0, 0, 0);
		phy_rx_iq_est(phy, est0, two ? RXCAL_EST_SAMPLES_TWO : RXCAL_EST_SAMPLES,
			      0x20, 0, 1);
		if (img) {
			phy_rxcal_radio_restore(phy, radio_save);
			phy_rx_iq_est(phy, est1, RXCAL_EST_SAMPLES, 0x20, 0, 1);
			phy_rxcal_radio_setup(phy, radio_save);
		}
		bcm4360_phy_stopplayback_acphy(phy);
		for (c = 0; c < n; c++)				/* step 11e (X5) */
			rxcal_tone_result(est0[c], est1[c], img, &angle[c][k], &gain[c][k]);
	}

	for (c = 0; c < n; c++)					/* step 12 (X6) */
		phy_rxcal_coeffs(phy, angle[c], gain[c], amp, cnt, two, c, ab[c]);
	for (c = 0; c < n; c++)					/* step 13 */
		phy_rxiqcc(phy, 1, ab[c], c);
	if (two)
		phy_rxcal_digfilt(phy, 1);

	phy_rxcal_radio_restore(phy, radio_save);
	phy_rxcal_phy_restore(phy, &sv);
	for (c = 0; c < n; c++) {
		if (!(phy->rxchain & (1u << c)))
			continue;
		phy_cal_coeffs(phy, 1, rxgain[c], 8, c);
	}
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
					  (u16)(tempsense_a[p] << 1));
			bcm4360_radio_mod(io, R2069_REG_0x16e | cb, 0x0001, 0x0001);
			bcm4360_radio_mod(io, R2069_REG_0x00e | cb, 0x0004,
					  (u16)(tempsense_b[p] << 2));
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
	acc += 0x1d0614;					/* = 1902100 (acphy-cal-rx.md, corrections) */
	degrees = acc / 16384;
	temp = (s32)phy->temp.offset + degrees;

	bcm4360_phy_write(io, ACPHY_REG_0x19e, s19e);		/* step 8 */
	for (c = 0; c < phy_cores(phy); c++) {			/* all cores' PHY regs first */
		u32 b = c * ACPHY_CORE_STEP;

		if (!(phy->rxchain & (1u << c)))
			continue;
		for (i = 0; i < 14; i++)
			bcm4360_phy_write(io, tempsense_phy_regs[i] + b, phy_save[c][i]);
	}
	for (c = 0; c < phy_cores(phy); c++) {			/* then all cores' radio regs */
		u32 cb = c << R2069_BANK_SHIFT;

		if (!(phy->rxchain & (1u << c)))
			continue;
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
 * acphy-cal-tx section 6 (wlc_phy_cal_perical_mphase_reset): stop the
 * multi-phase sequence (the same as the static one of phy_api.c, which this
 * file cannot call)
 */
static void phy_cal_mphase_reset(struct bcm4360_phy *phy)
{
	if (phy->timer_phycal)
		bcm4360_timer_del(phy->hw, phy->timer_phycal);
	phy->cal.state.phase = 0;
	phy->cal.state.subphase = 0;
}

/* acphy-cal-tx section 7 (wlc_phy_cal_perical_mphase_restart): from phase 1 again */
static void phy_cal_mphase_restart(struct bcm4360_phy *phy)
{
	phy->cal.state.phase = 1;
	phy->cal.state.subphase = 0;
}

/* acphy-cal-tx section 1, phase 0 step 7: the "calibration done" marker */
static void phy_cal_marker(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 bracket = tbl_bracket_enter(io);

	tbl_write16(io, TBL_BBMULT, TBL_BBMULT_MARKER, CAL_MARKER);
	tbl_bracket_leave(io, bracket);
}

/* the durations of sub_0925cc per phase (acphy-cal-tx, "The phycal shared-memory word") */
#define CAL_DUR_SINGLE		29000
#define CAL_DUR_PRECAL		0x3c
#define CAL_DUR_TXCAL		0x1130
#define CAL_DUR_RXCAL		0x251c
#define CAL_DUR_VCO		300
#define CAL_DUR_IDLETSSI	0x60e

/* the phases of the multi-phase sequence (cal[1], acphy-cal-tx section 1) */
#define CAL_PHASE_PRECAL	0x01
#define CAL_PHASE_TXA_FIRST	0x02
#define CAL_PHASE_TXA_REV0_LAST	0x0a	/* PHY revision 0: 0x0b, 0x0c only advance */
#define CAL_PHASE_TXA_LAST	0x0c
#define CAL_PHASE_GAP		0x0d
#define CAL_PHASE_TXB_FIRST	0x0e
#define CAL_PHASE_TXB_LAST	0x10
#define CAL_PHASE_RXCAL		0x11
#define CAL_PHASE_VCO		0x12
#define CAL_PHASE_IDLETSSI	0x13

/* acphy-cal-tx section 1, phase 0: everything in one call */
static void phy_cals_single(struct bcm4360_phy *phy, u8 sm)
{
	struct bcm4360_phy_cal_state *st = &phy->cal.state;

	phy_cal_suspend_setup(phy, CAL_DUR_SINGLE);		/* 1 */
	st->last_cal_time = phy->now;				/* 2: sh+0x34 */
	st->last_chanspec = phy->radio_chanspec;
	if (st->first_cal)					/* 3 */
		bcm4360_phy_txpwrctrl_idle_tssi_meas_acphy(phy);
	bcm4360_phy_precal_txgain_acphy(phy, st->gainrec[0]);	/* 4 */
	phy_cal_txiqlo(phy, sm, 0, 0);				/* 5 */
	phy_cal_txiqlo(phy, sm, 0, 1);
	phy_rxiqcal(phy);					/* 6 */
	phy_cal_marker(phy);					/* 7 */
	st->first_cal = 0;					/* 8 */
	phy->cal.acphy_0x44a = 0;
	phy_scanroam_cache(phy, 1);
}

/*
 * acphy-cal-tx section 1, phases 2 to 0xc and 0xe to 0x10: one slice of the
 * transmit calibration (part A or B). False if the tone could not be built
 * (the sequence is then reset).
 */
static bool phy_cals_txcal_phase(struct bcm4360_phy *phy, u8 sm, u8 part)
{
	phy_cal_suspend_setup(phy, CAL_DUR_TXCAL);
	/* pi+0xf86 bit 4 / bit 0 (pi_ac+0x44c): 0 on this board */
	if (phy_cal_txiqlo(phy, sm, 1, part)) {
		phy_cal_mphase_reset(phy);
		return false;
	}
	return true;
}

/*
 * acphy-cal-tx section 1, the branch on the phase of the multi-phase sequence
 * (phases 1 to 0x13). True if the phase suspended the MAC (the cleanup
 * follows), false if it only advanced or reset the sequence.
 */
static bool phy_cals_mphase(struct bcm4360_phy *phy, u8 phase, u8 sm)
{
	struct bcm4360_phy_cal_state *st = &phy->cal.state;

	if (phase == CAL_PHASE_PRECAL) {
		phy_cal_suspend_setup(phy, CAL_DUR_PRECAL);
		st->last_cal_time = phy->now;
		st->last_chanspec = phy->radio_chanspec;
		bcm4360_phy_precal_txgain_acphy(phy, st->gainrec[0]);
		st->phase++;
		return true;
	}
	if (phase >= CAL_PHASE_TXA_FIRST && phase <= CAL_PHASE_TXA_LAST) {
		if (phase > CAL_PHASE_TXA_REV0_LAST && phy->ver.phy_rev != ACPHY_REV_1) {
			st->phase++;			/* as phase 0xd: advance only */
			return false;
		}
		if (!phy_cals_txcal_phase(phy, sm, 0))
			return true;
		if (phase == CAL_PHASE_TXA_LAST)
			st->phase++;			/* skips the gap 0xd */
		st->phase++;
		return true;
	}
	if (phase == CAL_PHASE_GAP) {
		st->phase++;
		return false;
	}
	if (phase >= CAL_PHASE_TXB_FIRST && phase <= CAL_PHASE_TXB_LAST) {
		if (!phy_cals_txcal_phase(phy, sm, 1))
			return true;
		st->phase++;
		return true;
	}
	if (phase == CAL_PHASE_RXCAL) {
		phy_cal_suspend_setup(phy, CAL_DUR_RXCAL);
		phy_rxiqcal(phy);
		phy->cal.acphy_0x44a = 0;
		phy_scanroam_cache(phy, 1);
		st->phase++;
		return true;
	}
	if (phase == CAL_PHASE_VCO) {
		phy_cal_suspend_setup(phy, CAL_DUR_VCO);
		bcm4360_radio_vcocal(&phy->radio);		/* sub_09311b */
		bcm4360_radio_vcocal_wait(&phy->radio, true);	/* sub_093e47(pi, 1) */
		st->last_cal_time = phy->now;
		st->last_chanspec = phy->radio_chanspec;
		if (!st->first_cal) {
			phy_cal_marker(phy);
			phy_cal_mphase_reset(phy);		/* the sequence is finished */
		} else {
			st->phase++;
		}
		return true;
	}
	if (phase == CAL_PHASE_IDLETSSI) {
		phy_cal_suspend_setup(phy, CAL_DUR_IDLETSSI);
		bcm4360_phy_txpwrctrl_idle_tssi_meas_acphy(phy);
		st->first_cal = 0;
		phy_cal_marker(phy);
		phy_cal_mphase_reset(phy);
		return true;
	}
	phy_cal_mphase_reset(phy);				/* other phases */
	return false;
}

/*
 * acphy-cal-tx section 1 (wlc_phy_cals_acphy): run or advance a calibration.
 * `phase_mode` is the mphase search mode forwarded to the transmit cal.
 */
void bcm4360_phy_cals(struct bcm4360_phy *phy, u32 phase_mode)
{
	struct bcm4360_phy_cal_state *st = &phy->cal.state;
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

	if ((phase == CAL_PHASE_RXCAL || phase == 0) && phy->crsmincal.enable) { /* common 3 */
		phy->crsmincal.state[0] = 1;
		/*
		 * the common request sub_0baff6 (phy_rx.c) clears the result words
		 * and starts the measurement with D11(0x124) bit 4; refused while a
		 * sample is pending (acphy-api-watchdog.md, "Corrections and
		 * answers", Q3)
		 */
		bcm4360_phy_noise_sample_request_crsmincal(phy);
	}

	sm = (chanspec == st->last_chanspec && st->restart_ok) ? mode : 0;	/* common 4 */

	if (phase > 1 && st->last_chanspec != chanspec) {	/* common 5 */
		phy_cal_mphase_restart(phy);
		phase = st->phase;	/* start over: the branch runs phase 1 */
	}

	en = phy->txpwr.hwpwrctrl;				/* common 6 */

	if (phase == 0) {
		phy_cals_single(phy, sm);
	} else if (!phy_cals_mphase(phy, phase, sm)) {
		return;			/* nothing was suspended: no cleanup */
	}
	phy_cals_cleanup(phy, en, rxsave, txsave);		/* cleanup */
}

/* sub_0b56ce: the repeat interval of the chip 0xa8e5 */
#define CAL_TIMER_CHIP_A8E5	0xa8e5
#define CAL_TIMER_A8E5_MS	0x28
/* sub_0b56ce step 4: a hold flag postpones the whole sequence by 1 s */
#define CAL_TIMER_HOLD_MASK	0x021e
#define CAL_TIMER_HOLD_MS	1000

/*
 * acphy-cal-tx section 5 (sub_0b56ce, wlc_phy_cal_perical_mphase_tmr_cb): the
 * phycal timer callback; runs one phase of the multi-phase sequence and arms
 * the timer again for the next one.
 */
void bcm4360_phy_timer_phycal(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_cal_state *st = &phy->cal.state;
	u32 iv = phy->cal.phase_delay_ms;			/* 1 */

	if (phy->board.chip == CAL_TIMER_CHIP_A8E5)
		iv = CAL_TIMER_A8E5_MS;
	if (st->phase == 0)					/* 2 */
		return;
	if (!phy->up) {						/* 3 */
		phy_cal_mphase_reset(phy);
		return;
	}
	if (!(phy->hold & CAL_TIMER_HOLD_MASK)) {		/* 4 */
		bcm4360_phy_cals(phy, st->search_mode);
	} else {
		st->phase = 1;
		st->subphase = 0;
		iv = CAL_TIMER_HOLD_MS;
	}
	if (st->phase != 0)					/* 5 */
		bcm4360_timer_add(phy->hw, phy->timer_phycal, iv, false);
	else
		phy_cal_mphase_reset(phy);
}

/* acphy-api.md, A13: restore the scan/roam calibration cache (hold_upd) */
void bcm4360_phy_scanroam_restore(struct bcm4360_phy *phy)
{
	phy_scanroam_cache(phy, 0);
}
