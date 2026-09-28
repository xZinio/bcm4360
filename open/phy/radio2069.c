// SPDX-License-Identifier: ISC
/*
 * The 2069 radio behind the AC-PHY of the BCM4360: power, preferred values,
 * RC calibrations, channel tuning.
 *
 * Written from docs/re/spec/acphy-radio.md, for radio major revision 0 (radio
 * revisions 3 and 4) behind an AC-PHY of revision 0 or 1. The branches of
 * other revisions are left out. A "section" in the comments is a section of
 * "Procedures" of that specification.
 */
#include <bcm4360/radio2069.h>

/* docs/re/tasks/radio.md: the tests provide this much memory for the state */
#define RADIO_STATE_MAX			512

_Static_assert(sizeof(struct bcm4360_radio) <= RADIO_STATE_MAX,
	       "struct bcm4360_radio is larger than the task allows");

/* section 7, notes: the results of the RC calibration before it has run */
#define RCCAL_GMULT_DEFAULT		0x80
#define RCCAL_DACBUF_DEFAULT		0x0c

/* specification "Constants": how often and how far apart a status is read */
#define RCAL_POLLS			100
#define RCAL_POLL_US			10
#define RCCAL_POLLS			100
#define RCCAL_POLL_US			100
#define VCOCAL_POLLS			100
#define VCOCAL_POLL_US			10
#define AFECAL_POLLS			10
#define AFECAL_POLL_US			10

/* specification "Constants": scale factor of the RCCAL, radio major revision 0 */
#define RCCAL_SCALE			0xc1
#define RCCAL_SCALE_SHIFT		8

/* delays in microseconds */
#define PWRON_PULSE_US			1	/* section 5, step 3 */
#define PWRON_SETTLE_US			100	/* section 5, steps 9 and 10 */
#define RCAL_PHY_US			3	/* section 4, "On", step 3 */
#define RCAL_LOW_US			1	/* section 4, "On", step 4.4 */
#define RCCAL_LOW_US			1	/* section 7, step 2.3 */
#define RCCAL_START_US			35	/* section 7, step 2.3 */
#define VCOCAL_LOW_US			11	/* section 13, start */
#define VCOCAL_HIGH_US			1	/* section 13, start */
#define VCOCAL_SETTLE_US		120	/* section 13, wait */
#define AFECAL_LOW_US			100	/* section 14, step 2.3 */

/* section 6: a table of preferred values */
#define PREFREGS_END			0xffff	/* the address that ends the table */
#define PREFREGS_WORDS			2	/* words of an entry */
#define PREFREGS_ADDR			0
#define PREFREGS_VALUE			1

/* section 11, step 4: the channel with loop filter values of its own */
#define TUNE_CHANNEL_4			4

/* section 14, step 4: number of registers that are complemented, per core */
#define AFECAL_REV3_REGS		14

/* section 15: the cores of the 5 GHz settings that depend on the boardflags */
#define BAND_5G_CORES			2

/* section 5, step 1: what the power-on sequence reads at its start */
struct pwron_saved {
	u16 phy_0x728;
	u16 phy_0x408;		/* the bits that the sequence keeps */
};

/* section 7: the values of a pass; the names are the aliases of the specification */
struct rccal_pass {
	u8 sr;
	u8 sc;
	u8 x1;
	u16 trc;
};

enum rccal_pass_nr {
	RCCAL_PASS_GMULT,	/* result: rccal_gmult and rccal_gmult_rc */
	RCCAL_PASS_RADIO,	/* result: RADIO(0x126) and RADIO(0x043) of every core */
	RCCAL_PASS_DACBUF,	/* result: rccal_dacbuf */
	RCCAL_PASSES
};

static const struct rccal_pass rccal_passes[RCCAL_PASSES] = {
	[RCCAL_PASS_GMULT]  = { .sr = 1, .sc = 0, .x1 = 0x1c, .trc = 0x014a },
	[RCCAL_PASS_RADIO]  = { .sr = 0, .sc = 2, .x1 = 0x70, .trc = 0x0101 },
	[RCCAL_PASS_DACBUF] = { .sr = 0, .sc = 1, .x1 = 0x40, .trc = 0x011a },
};

/*
 * Specification "Tables": the radio registers that the words 2..51 of a
 * channel entry go to, in the order of the words.
 */
static const u16 chan_radio_regs[R2069_CHAN_RADIO_WORDS] = {
	0x8e0, 0x8e1, 0x8dd, 0x8dc, 0x8e6, 0x8e7, 0x8c4, 0x8c5, 0x8e5, 0x8eb,
	0x8d6, 0x113, 0x8db, 0x8da, 0x8d7, 0x885, 0x886, 0x887, 0x8d9, 0x8d8,
	0x8c9, 0x8ca, 0x8cc, 0x8c7, 0x8c8, 0x892, 0x894, 0x895, 0x896, 0x897,
	0x899, 0x89a, 0x89b, 0x89c, 0x112, 0x629, 0x65b, 0x65e, 0x668, 0x11a,
	0x11b, 0x719, 0x630, 0x65c, 0x662, 0x66d, 0x893, 0x145, 0x146, 0x723,
};

/* a field of a radio register and the value it gets */
struct radio_field {
	u16 reg;
	u16 mask;
	u16 val;
};

/* section 16, step 2.2: what is modified for a core, by the number of the core */
static const struct radio_field tssi_core_fields[] = {
	{ R2069_REG_0x54b, R2069_0x54b_BITS8_15, R2069_0x54b_BITS8_15_CORE0 },
	{ R2069_REG_0x54b, R2069_0x54b_BITS0_7, R2069_0x54b_BITS0_7_CORE1 },
	{ R2069_REG_0x54c, R2069_0x54c_BITS8_15, R2069_0x54c_BITS8_15_CORE2 },
};

/* docs/re/spec/acphy-radio.md, "Radio register addresses": the radio register of a core */
static u16 radio_core_reg(u16 reg, u32 core)
{
	return reg | core << R2069_BANK_SHIFT;
}

/* docs/re/spec/acphy-radio.md, "Radio register addresses": the PHY register of a core */
static u16 phy_core_reg(u16 reg, u32 core)
{
	return reg + core * ACPHY_CORE_STEP;
}

/* docs/re/spec/acphy-radio.md, "Procedures": mod(RADIO(reg), mask, val) */
static void radio_mod(struct bcm4360_radio *r, u16 reg, u16 mask, u16 val)
{
	bcm4360_radio_mod(r->io, reg, mask, val);
}

/* docs/re/spec/acphy-radio.md, "Procedures": mod(RADIO(reg), bits, bits) */
static void radio_set(struct bcm4360_radio *r, u16 reg, u16 bits)
{
	bcm4360_radio_mod(r->io, reg, bits, bits);
}

/* docs/re/spec/acphy-radio.md, "Procedures": mod(RADIO(reg), bits, 0) */
static void radio_clear(struct bcm4360_radio *r, u16 reg, u16 bits)
{
	bcm4360_radio_mod(r->io, reg, bits, 0);
}

/*
 * docs/re/spec/acphy-radio.md, "Constants": read a status register of the
 * radio up to `polls` times, `usec` apart. Returns true as soon as `done` is
 * set.
 */
static bool radio_poll(struct bcm4360_radio *r, u16 reg, u16 done, u32 polls, u32 usec)
{
	u32 i;

	for (i = 0; i < polls; i++) {
		hw_udelay(usec);
		if (bcm4360_radio_read(r->io, reg) & done)
			return true;
	}
	return false;
}

/*
 * docs/re/spec/acphy-radio.md, section 4, "On", step 4.3, and section 16,
 * step 1: set bit 0 of RADIO(0x548) and write 0 to RADIO(0x549)..RADIO(0x54c).
 */
static void radio_prepare_0x548_to_0x54c(struct bcm4360_radio *r)
{
	radio_set(r, R2069_REG_0x548, R2069_0x548_BIT0);
	bcm4360_radio_write(r->io, R2069_REG_0x549, 0);
	bcm4360_radio_write(r->io, R2069_REG_0x54a, 0);
	bcm4360_radio_write(r->io, R2069_REG_0x54b, 0);
	bcm4360_radio_write(r->io, R2069_REG_0x54c, 0);
}

/*
 * docs/re/spec/acphy-radio.md, section 7, notes: the results of the RC
 * calibration before it has run (the defaults from attach)
 */
void bcm4360_radio_init(struct bcm4360_radio *r)
{
	r->rccal_gmult = RCCAL_GMULT_DEFAULT;
	r->rccal_gmult_rc = RCCAL_GMULT_DEFAULT;
	r->rccal_dacbuf = RCCAL_DACBUF_DEFAULT;
}

/*
 * docs/re/spec/acphy-radio.md, section 4 (wlc_phy_switch_radio_acphy), "Off",
 * steps 2 to 4
 */
void bcm4360_radio_off(struct bcm4360_radio *r)
{
	struct bcm4360_phy_io *io = r->io;

	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x73e, ACPHY_0x73e_RADIO_OFF);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x739, ACPHY_0x739_RADIO_OFF);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x73a, ACPHY_0x73a_RADIO_OFF);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x725, ACPHY_0x725_RADIO_OFF);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x729, ACPHY_0x729_RADIO_OFF);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x721, ACPHY_0x721_RADIO_OFF);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x728, ACPHY_0x728_RADIO_OFF);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x720, ACPHY_0x720_RADIO_OFF);

	bcm4360_phy_mod(io, ACPHY_REG_0x408, ACPHY_0x408_BIT1, 0);

	bcm4360_phy_write(io, ACPHY_REG_0x417, ACPHY_0x417_RADIO_OFF);
	bcm4360_phy_write(io, ACPHY_REG_0x416, ACPHY_0x416_RADIO_OFF);
}

/* docs/re/spec/acphy-radio.md, section 5, steps 1 to 3: reset of the radio */
static void pwron_reset(struct bcm4360_radio *r, struct pwron_saved *s)
{
	struct bcm4360_phy_io *io = r->io;
	u16 phy_0x720;

	s->phy_0x728 = bcm4360_phy_read(io, ACPHY_REG_0x728);
	s->phy_0x408 = bcm4360_phy_read(io, ACPHY_REG_0x408) & ACPHY_0x408_PWRON_KEEP;

	bcm4360_phy_write(io, ACPHY_REG_0x415, ACPHY_0x415_PWRON);
	bcm4360_phy_write(io, ACPHY_REG_0x40e, ACPHY_0x40e_PWRON);
	bcm4360_phy_write(io, ACPHY_REG_0x40c, ACPHY_0x40c_PWRON);
	bcm4360_phy_write(io, ACPHY_REG_0x408, s->phy_0x408);
	bcm4360_phy_write(io, ACPHY_REG_0x417, ACPHY_0x417_RESET);
	bcm4360_phy_write(io, ACPHY_REG_0x416, ACPHY_0x416_PWRON);
	bcm4360_phy_write(io, ACPHY_REG_0x728, s->phy_0x728 &
			  ~(ACPHY_0x728_BIT15 | ACPHY_0x728_BIT8 | ACPHY_0x728_BIT7));

	phy_0x720 = bcm4360_phy_read(io, ACPHY_REG_0x720);
	bcm4360_phy_write(io, ACPHY_REG_0x720, phy_0x720 | ACPHY_0x720_BITS7_8);

	bcm4360_phy_write(io, ACPHY_REG_0x408, s->phy_0x408);
	bcm4360_phy_write(io, ACPHY_REG_0x408, s->phy_0x408 | ACPHY_0x408_BIT0);
	hw_udelay(PWRON_PULSE_US);
	bcm4360_phy_write(io, ACPHY_REG_0x408, s->phy_0x408);
}

/*
 * docs/re/spec/acphy-radio.md, section 6
 * (wlc_phy_init_radio_prefregs_allbands): write a table of preferred values.
 * The first entry is written without looking at its address. Returns the
 * number of entries written.
 */
static u32 radio_prefregs(struct bcm4360_radio *r, const u16 *entry)
{
	u32 n = 0;

	do {
		bcm4360_radio_write(r->io, entry[PREFREGS_ADDR], entry[PREFREGS_VALUE]);
		entry += PREFREGS_WORDS;
		n++;
	} while (entry[PREFREGS_ADDR] != PREFREGS_END);

	return n;
}

/*
 * docs/re/spec/acphy-radio.md, section 5, steps 5 to 7: fixed modifications
 * of common and shared registers
 */
static void pwron_fixed_common(struct bcm4360_radio *r)
{
	if (r->boardflags & BCM4360_BOARDFLAGS_BIT1)
		radio_set(r, R2069_REG_0x8ea, R2069_0x8ea_BIT8);

	radio_set(r, R2069_REG_0x96b, R2069_0x96b_BIT11);
	radio_set(r, R2069_REG_0x96b, R2069_0x96b_BIT14);
	radio_set(r, R2069_REG_0x96c, R2069_0x96c_BIT11);
	radio_set(r, R2069_REG_0x96b, R2069_0x96b_BIT15);
	radio_set(r, R2069_REG_0x96b, R2069_0x96b_BIT12);
	radio_set(r, R2069_REG_0x96b, R2069_0x96b_BIT2);

	radio_set(r, R2069_REG_0x407, R2069_0x407_BIT1);
	radio_set(r, R2069_REG_0x55e, R2069_0x55e_BIT4);
}

/*
 * docs/re/spec/acphy-radio.md, section 5, step 8: fixed modifications of the
 * registers of a core. Left out: what radio revisions 7 and 8 do in addition.
 */
static void pwron_fixed_core(struct bcm4360_radio *r, u32 core)
{
	u16 reg_0x06f = radio_core_reg(R2069_REG_0x06f, core);

	radio_mod(r, radio_core_reg(R2069_REG_0x126, core), R2069_0x126_BITS8_9,
		  R2069_0x126_BITS8_9_PWRON);
	radio_mod(r, radio_core_reg(R2069_REG_0x127, core), R2069_0x127_BITS0_1,
		  R2069_0x127_BITS0_1_PWRON);
	radio_clear(r, reg_0x06f, R2069_0x06f_BIT2);
	radio_clear(r, reg_0x06f, R2069_0x06f_BIT0);
	radio_clear(r, reg_0x06f, R2069_0x06f_BIT1);
	radio_clear(r, radio_core_reg(R2069_REG_0x065, core), R2069_0x065_BIT0);
}

/*
 * docs/re/spec/acphy-radio.md, section 5, steps 9 and 10: the end of the
 * power-on sequence
 */
static void pwron_finish(struct bcm4360_radio *r, const struct pwron_saved *s)
{
	struct bcm4360_phy_io *io = r->io;

	radio_clear(r, R2069_REG_0x40c, R2069_0x40c_BIT4);
	bcm4360_phy_write(io, ACPHY_REG_0x408,
			  s->phy_0x408 | ACPHY_0x408_BIT2 | ACPHY_0x408_BIT1);
	hw_udelay(PWRON_SETTLE_US);
	radio_set(r, R2069_REG_0x40c, R2069_0x40c_BIT4);

	bcm4360_phy_write(io, ACPHY_REG_0x417, ACPHY_0x417_PWRON_FIRST);
	bcm4360_phy_write(io, ACPHY_REG_0x408, s->phy_0x408 | ACPHY_0x408_BIT1);
	bcm4360_phy_write(io, ACPHY_REG_0x728,
			  s->phy_0x728 | ACPHY_0x728_BIT8 | ACPHY_0x728_BIT7);
	hw_udelay(PWRON_SETTLE_US);
	bcm4360_phy_write(io, ACPHY_REG_0x417, ACPHY_0x417_PWRON_LAST);
	bcm4360_phy_write(io, ACPHY_REG_0x728, s->phy_0x728 & ~ACPHY_0x728_BIT8);
}

/*
 * docs/re/spec/acphy-radio.md, section 5 (wlc_phy_radio2069_pwron_seq): reset
 * the radio, load the preferred values, apply the fixed modifications.
 * Without a table of preferred values the steps 4 to 8 are left out.
 */
void bcm4360_radio_pwron_seq(struct bcm4360_radio *r)
{
	struct pwron_saved saved;
	u32 core;

	pwron_reset(r, &saved);
	if (r->tbl.prefregs) {
		radio_prefregs(r, r->tbl.prefregs);
		pwron_fixed_common(r);
		for (core = 0; core < r->cores; core++)
			pwron_fixed_core(r, core);
	}
	pwron_finish(r, &saved);
}

/*
 * docs/re/spec/acphy-radio.md, section 4, "On", steps 4.1 to 4.3: before the
 * resistor calibration
 */
static void rcal_prepare(struct bcm4360_radio *r)
{
	radio_set(r, R2069_REG_0x8ea, R2069_0x8ea_BIT6);
	radio_set(r, R2069_REG_0x8ea, R2069_0x8ea_BIT7);
	radio_mod(r, R2069_REG_0x8ed, R2069_0x8ed_BITS9_10, R2069_0x8ed_BITS9_10_RCAL);
	radio_mod(r, R2069_REG_0x8ed, R2069_0x8ed_BITS11_12, R2069_0x8ed_BITS11_12_RCAL);
	radio_prepare_0x548_to_0x54c(r);
}

/*
 * docs/re/spec/acphy-radio.md, section 4, "On", steps 4.4 to 4.6: run the
 * resistor calibration. Its result is read and dropped, a timeout is not
 * noticed.
 */
static void rcal_run(struct bcm4360_radio *r)
{
	radio_clear(r, R2069_REG_0x40b, R2069_0x40b_BIT0);
	hw_udelay(RCAL_LOW_US);
	radio_set(r, R2069_REG_0x40b, R2069_0x40b_BIT0);

	radio_poll(r, R2069_REG_0x40b, R2069_0x40b_DONE, RCAL_POLLS, RCAL_POLL_US);
	(void)bcm4360_radio_read(r->io, R2069_REG_0x40b);
}

/*
 * docs/re/spec/acphy-radio.md, section 4, "On", steps 4.7 to 4.9: after the
 * resistor calibration
 */
static void rcal_finish(struct bcm4360_radio *r)
{
	radio_clear(r, R2069_REG_0x548, R2069_0x548_BIT0);
	radio_clear(r, R2069_REG_0x8ea, R2069_0x8ea_BIT6);
	radio_clear(r, R2069_REG_0x8ea, R2069_0x8ea_BIT7);
	radio_clear(r, R2069_REG_0x40b, R2069_0x40b_BIT0);
}

/*
 * docs/re/spec/acphy-radio.md, section 4 (wlc_phy_switch_radio_acphy), "On",
 * steps 3 and 4: the resistor calibration (RCAL). For this radio nothing is
 * written when the measurement is skipped.
 */
void bcm4360_radio_rcal(struct bcm4360_radio *r)
{
	struct bcm4360_phy_io *io = r->io;

	bcm4360_phy_mod(io, ACPHY_REG_0x16b, ACPHY_0x16b_BIT10, 0);
	hw_udelay(RCAL_PHY_US);
	bcm4360_phy_write(io, ACPHY_REG_0x175, ACPHY_0x175_RADIO_ON);
	hw_udelay(RCAL_PHY_US);

	if (r->skip_rcal)
		return;

	rcal_prepare(r);
	rcal_run(r);
	rcal_finish(r);
}

/* docs/re/spec/acphy-radio.md, section 7, steps 2.1 and 2.2: the values of a pass */
static void rccal_setup(struct bcm4360_radio *r, enum rccal_pass_nr nr)
{
	const struct rccal_pass *p = &rccal_passes[nr];
	u32 core;

	radio_mod(r, R2069_REG_0x410, R2069_0x410_SR, p->sr << R2069_0x410_SR_SHIFT);
	radio_mod(r, R2069_REG_0x410, R2069_0x410_SC, p->sc << R2069_0x410_SC_SHIFT);
	radio_mod(r, R2069_REG_0x411, R2069_0x411_X1, p->x1 << R2069_0x411_X1_SHIFT);
	bcm4360_radio_write(r->io, R2069_REG_0x412, p->trc);

	if (nr != RCCAL_PASS_DACBUF)
		return;

	for (core = 0; core < r->cores; core++) {
		radio_clear(r, radio_core_reg(R2069_REG_0x11d, core), R2069_0x11d_BIT2);
		radio_set(r, radio_core_reg(R2069_REG_0x171, core), R2069_0x171_BIT13);
	}
}

/*
 * docs/re/spec/acphy-radio.md, section 7, steps 2.3 to 2.5: run a pass.
 * Returns true if it finished in time.
 */
static bool rccal_run(struct bcm4360_radio *r)
{
	bool done;

	radio_clear(r, R2069_REG_0x410, R2069_0x410_BIT0);
	hw_udelay(RCCAL_LOW_US);
	radio_set(r, R2069_REG_0x410, R2069_0x410_BIT0);
	hw_udelay(RCCAL_START_US);
	radio_set(r, R2069_REG_0x411, R2069_0x411_START);

	done = radio_poll(r, R2069_REG_0x413, R2069_0x413_DONE, RCCAL_POLLS, RCCAL_POLL_US);

	radio_clear(r, R2069_REG_0x411, R2069_0x411_START);
	return done;
}

/*
 * docs/re/spec/acphy-radio.md, section 7, step 2.6, pass 0: the result
 * "gmult", computed in 32 bit signed arithmetic; its low byte is kept
 */
static void rccal_result_gmult(struct bcm4360_radio *r)
{
	s32 n0 = bcm4360_radio_read(r->io, R2069_REG_0x414);
	s32 n1 = bcm4360_radio_read(r->io, R2069_REG_0x415);
	s32 g = ((n1 - n0) * RCCAL_SCALE) >> RCCAL_SCALE_SHIFT;

	r->rccal_gmult = g;
	r->rccal_gmult_rc = g;
}

/*
 * docs/re/spec/acphy-radio.md, section 7, step 2.6, pass 1: the result goes
 * into the radio
 */
static void rccal_result_radio(struct bcm4360_radio *r)
{
	u16 v = bcm4360_radio_read(r->io, R2069_REG_0x416) & R2069_0x416_BITS0_4;
	u32 core;

	for (core = 0; core < r->cores; core++) {
		radio_mod(r, radio_core_reg(R2069_REG_0x126, core), R2069_0x126_BITS0_4, v);
		radio_mod(r, radio_core_reg(R2069_REG_0x043, core), R2069_0x043_BITS0_4, v);
	}
}

/*
 * docs/re/spec/acphy-radio.md, section 7, step 2.6, pass 2: the result for
 * the DAC buffer
 */
static void rccal_result_dacbuf(struct bcm4360_radio *r)
{
	u16 v = bcm4360_radio_read(r->io, R2069_REG_0x416);
	u32 core;

	r->rccal_dacbuf = (v & R2069_0x416_BITS5_9) >> R2069_0x416_BITS5_9_SHIFT;

	for (core = 0; core < r->cores; core++)
		radio_clear(r, radio_core_reg(R2069_REG_0x171, core), R2069_0x171_BIT13);
}

/* docs/re/spec/acphy-radio.md, section 7, step 2: one pass of the RC calibration */
static void rccal_pass(struct bcm4360_radio *r, enum rccal_pass_nr nr)
{
	rccal_setup(r, nr);

	if (rccal_run(r)) {
		switch (nr) {
		case RCCAL_PASS_GMULT:
			rccal_result_gmult(r);
			break;
		case RCCAL_PASS_RADIO:
			rccal_result_radio(r);
			break;
		case RCCAL_PASS_DACBUF:
			rccal_result_dacbuf(r);
			break;
		default:
			break;
		}
	}

	radio_clear(r, R2069_REG_0x410, R2069_0x410_BIT0);
}

/*
 * docs/re/spec/acphy-radio.md, section 7 (wlc_phy_radio2069_rccal): RC
 * calibration in three passes. When a pass runs into its timeout its result
 * stays what it was.
 */
void bcm4360_radio_rccal(struct bcm4360_radio *r)
{
	enum rccal_pass_nr nr;

	radio_set(r, R2069_REG_0x8ea, R2069_0x8ea_BIT7);
	radio_mod(r, R2069_REG_0x8ed, R2069_0x8ed_BITS9_10, R2069_0x8ed_BITS9_10_RCCAL);

	for (nr = RCCAL_PASS_GMULT; nr < RCCAL_PASSES; nr++)
		rccal_pass(r, nr);

	radio_clear(r, R2069_REG_0x8ea, R2069_0x8ea_BIT7);
}

/*
 * docs/re/spec/acphy-radio.md, section 10 (wlc_phy_chan2freq_acphy): the
 * entry of a channel in the channel table, the first match wins. Without an
 * entry the frequency is 0.
 */
const u16 *bcm4360_radio_chan_entry(struct bcm4360_radio *r, u8 channel, u16 *freq)
{
	const u16 *entry = r->tbl.chan_tuning;
	u32 i;

	*freq = 0;
	if (!entry)
		return NULL;

	for (i = 0; i < r->tbl.chan_entries; i++, entry += R2069_CHAN_WORDS) {
		if (entry[R2069_CHAN_NUMBER] != channel)
			continue;
		*freq = entry[R2069_CHAN_FREQ];
		return entry;
	}
	return NULL;
}

/*
 * docs/re/spec/acphy-radio.md, section 11, step 2: the words 2..51 of the
 * entry go to the radio
 */
static void tune_write_entry(struct bcm4360_radio *r, const u16 *entry)
{
	u32 i;

	for (i = 0; i < R2069_CHAN_RADIO_WORDS; i++)
		bcm4360_radio_write(r->io, chan_radio_regs[i], entry[R2069_CHAN_RADIO + i]);
}

/*
 * docs/re/spec/acphy-radio.md, section 11, step 3: patch of the radio
 * revisions below 4
 */
static void tune_patch_rev3(struct bcm4360_radio *r)
{
	bcm4360_radio_write(r->io, R2069_BANK_CORE1 | R2069_REG_0x123, R2069_0x123_TUNE_REV3);
	bcm4360_radio_write(r->io, R2069_BANK_CORE2 | R2069_REG_0x123, R2069_0x123_TUNE_REV3);
	radio_mod(r, R2069_REG_0x892, R2069_0x892_BITS8_15, R2069_0x892_BITS8_15_REV3);
}

/* docs/re/spec/acphy-radio.md, section 11, step 4: patch of channel 4 */
static void tune_patch_channel4(struct bcm4360_radio *r)
{
	bcm4360_radio_write(r->io, R2069_REG_0x8d6, R2069_0x8d6_CHANNEL4);
	radio_mod(r, R2069_REG_0x8ec, R2069_0x8ec_BITS4_6, R2069_0x8ec_BITS4_6_CHANNEL4);
}

/*
 * docs/re/spec/acphy-radio.md, section 11
 * (wlc_phy_chanspec_radio2069_setup), steps 2 to 8: write the entry of the
 * channel and the fixed patches, on 5 GHz the loop filter, and start the VCO
 * calibration. Without an entry nothing is changed.
 */
void bcm4360_radio_tune(struct bcm4360_radio *r, const u16 *entry, u8 channel,
			bool is_5g)
{
	if (!entry)
		return;

	tune_write_entry(r, entry);

	if (r->rev < R2069_REV_4)
		tune_patch_rev3(r);
	if (channel == TUNE_CHANNEL_4)
		tune_patch_channel4(r);
	radio_set(r, R2069_REG_0x645, R2069_0x645_BITS12_14);

	if (is_5g)
		bcm4360_radio_rfpll_150khz(r);
	if (r->rev > R2069_REV_3)
		bcm4360_radio_write(r->io, R2069_BANK_ALL | R2069_REG_0x123,
				    R2069_0x123_TUNE_REV4);

	bcm4360_radio_vcocal(r);
}

/*
 * docs/re/spec/acphy-radio.md, section 12 (wlc_2069_rfpll_150khz): loop
 * filter of the PLL for a bandwidth of 150 kHz. Overwrites what the words
 * 22..26 of the channel entry have set.
 */
void bcm4360_radio_rfpll_150khz(struct bcm4360_radio *r)
{
	struct bcm4360_phy_io *io = r->io;

	radio_mod(r, R2069_REG_0x8c9, R2069_0x8c9_BITS8_15, R2069_0x8c9_BITS8_15_150KHZ);
	radio_mod(r, R2069_REG_0x8c9, R2069_0x8c9_BITS0_7, R2069_0x8c9_BITS0_7_150KHZ);
	bcm4360_radio_write(io, R2069_REG_0x8ca, R2069_0x8ca_150KHZ);
	radio_mod(r, R2069_REG_0x8cc, R2069_0x8cc_BITS0_7, R2069_0x8cc_BITS0_7_150KHZ);
	radio_mod(r, R2069_REG_0x8cc, R2069_0x8cc_BITS8_15, R2069_0x8cc_BITS8_15_150KHZ);
	bcm4360_radio_write(io, R2069_REG_0x8c7, R2069_0x8c7_150KHZ);
	bcm4360_radio_write(io, R2069_REG_0x8c8, R2069_0x8c8_150KHZ);
}

/*
 * docs/re/spec/acphy-radio.md, section 13 (wlc_phy_radio2069_vcocal): start
 * the VCO calibration
 */
void bcm4360_radio_vcocal(struct bcm4360_radio *r)
{
	radio_clear(r, R2069_REG_0x8e5, R2069_0x8e5_BIT14);
	radio_clear(r, R2069_REG_0x8d0, R2069_0x8d0_BIT0);
	radio_clear(r, R2069_REG_0x8e8, R2069_0x8e8_BIT6);
	radio_clear(r, R2069_REG_0x8dc, R2069_0x8dc_BIT13);
	hw_udelay(VCOCAL_LOW_US);
	radio_set(r, R2069_REG_0x8d0, R2069_0x8d0_BIT0);
	radio_set(r, R2069_REG_0x8e8, R2069_0x8e8_BIT6);
	hw_udelay(VCOCAL_HIGH_US);
	radio_set(r, R2069_REG_0x8dc, R2069_0x8dc_BIT13);
}

/*
 * docs/re/spec/acphy-radio.md, section 13 (wlc_phy_radio2069_vcocal_wait):
 * wait for the VCO calibration. No result: a timeout is not noticed.
 */
void bcm4360_radio_vcocal_wait(struct bcm4360_radio *r, bool settle)
{
	radio_poll(r, R2069_REG_0x90b, R2069_0x90b_DONE, VCOCAL_POLLS, VCOCAL_POLL_US);
	if (settle)
		hw_udelay(VCOCAL_SETTLE_US);
}

/*
 * docs/re/spec/acphy-radio.md, section 14, step 2.5: wait for the calibration
 * of a core. The status is read twice per round, bit 1 counts in the first
 * and bit 0 in the second value.
 */
static void afecal_wait(struct bcm4360_radio *r, u32 core)
{
	u16 reg = radio_core_reg(R2069_REG_0x144, core);
	u16 first, second;
	u32 i;

	for (i = 0; i < AFECAL_POLLS; i++) {
		hw_udelay(AFECAL_POLL_US);
		first = bcm4360_radio_read(r->io, reg);
		second = bcm4360_radio_read(r->io, reg);
		if ((second & R2069_0x144_DONE0) && (first & R2069_0x144_DONE1))
			break;
	}
}

/*
 * docs/re/spec/acphy-radio.md, section 14, steps 2.3 to 2.6: run the
 * calibration of a core
 */
static void afecal_run(struct bcm4360_radio *r, u32 core)
{
	u16 reg_0x121 = radio_core_reg(R2069_REG_0x121, core);
	u16 reg_0x122 = radio_core_reg(R2069_REG_0x122, core);
	u16 v;

	radio_clear(r, reg_0x121, R2069_0x121_BIT12);
	hw_udelay(AFECAL_LOW_US);
	radio_set(r, reg_0x121, R2069_0x121_BIT12);

	v = bcm4360_radio_read(r->io, reg_0x122);
	bcm4360_radio_write(r->io, reg_0x122, v | R2069_0x122_BITS0_3);
	afecal_wait(r, core);
	bcm4360_radio_write(r->io, reg_0x122, v & ~R2069_0x122_BITS0_3);
}

/*
 * docs/re/spec/acphy-radio.md, section 14, step 2: calibrate the converters
 * of a core
 */
static void afecal_core(struct bcm4360_radio *r, u32 core)
{
	struct bcm4360_phy_io *io = r->io;
	u16 reg_0x739 = phy_core_reg(ACPHY_REG_0x739, core);
	u16 reg_0x73a = phy_core_reg(ACPHY_REG_0x73a, core);
	u16 reg_0x725 = phy_core_reg(ACPHY_REG_0x725, core);
	u16 phy_0x739, phy_0x73a, phy_0x725;

	phy_0x739 = bcm4360_phy_read(io, reg_0x739);
	phy_0x73a = bcm4360_phy_read(io, reg_0x73a);
	phy_0x725 = bcm4360_phy_read(io, reg_0x725);
	bcm4360_phy_mod(io, reg_0x739, ACPHY_0x739_BIT7, ACPHY_0x739_BIT7);
	bcm4360_phy_mod(io, reg_0x725, ACPHY_0x725_BIT2, ACPHY_0x725_BIT2);

	afecal_run(r, core);

	bcm4360_phy_write(io, reg_0x725, phy_0x725);
	bcm4360_phy_write(io, reg_0x739, phy_0x739);
	bcm4360_phy_write(io, reg_0x73a, phy_0x73a);
}

/*
 * docs/re/spec/acphy-radio.md, section 14, step 4, radio revisions below 4:
 * 14 registers of a core get the complement of what 14 others hold.
 */
static void afecal_rev3_core(struct bcm4360_radio *r, u32 core)
{
	u16 reg_0x15f = radio_core_reg(R2069_REG_0x15f, core);
	u16 v;
	u32 k;

	radio_set(r, reg_0x15f, R2069_0x15f_BIT5);
	radio_set(r, reg_0x15f, R2069_0x15f_BIT4);

	for (k = 0; k < AFECAL_REV3_REGS; k++) {
		v = bcm4360_radio_read(r->io, radio_core_reg(R2069_REG_0x143 - k, core));
		bcm4360_radio_write(r->io, radio_core_reg(R2069_REG_0x135 - k, core), ~v);
	}
}

/*
 * docs/re/spec/acphy-radio.md, section 14 (wlc_phy_radio2069_afecal):
 * calibration of the converters of each core. No result: a timeout is not
 * noticed.
 */
void bcm4360_radio_afecal(struct bcm4360_radio *r)
{
	u32 core;

	radio_set(r, R2069_REG_0x8ea, R2069_0x8ea_BIT7);
	for (core = 0; core < r->cores; core++)
		afecal_core(r, core);
	radio_clear(r, R2069_REG_0x8ea, R2069_0x8ea_BIT7);

	if (r->rev >= R2069_REV_4)
		return;

	radio_set(r, R2069_REG_0x8ea, R2069_0x8ea_BIT7);
	for (core = 0; core < r->cores; core++)
		afecal_rev3_core(r, core);
	radio_clear(r, R2069_REG_0x8ea, R2069_0x8ea_BIT7);
}

/* docs/re/spec/acphy-radio.md, section 15, 5 GHz: the settings of a core */
static void band_5g_core(struct bcm4360_radio *r, u32 core)
{
	radio_mod(r, radio_core_reg(R2069_REG_0x05c, core), R2069_0x05c_BITS12_15,
		  R2069_0x05c_BITS12_15_5G);
	radio_mod(r, radio_core_reg(R2069_REG_0x061, core), R2069_0x061_BITS12_15,
		  R2069_0x061_BITS12_15_5G);
}

/* docs/re/spec/acphy-radio.md, section 15, 2.4 GHz: every core of the PHY */
static void band_2g(struct bcm4360_radio *r)
{
	u32 core;

	for (core = 0; core < r->cores; core++)
		radio_mod(r, radio_core_reg(R2069_REG_0x059, core), R2069_0x059_BITS12_13,
			  R2069_0x059_BITS12_13_2G);
}

/*
 * docs/re/spec/acphy-radio.md, section 15, 5 GHz: cores 0 and 1 only with bit
 * 29 of the boardflags; the registers of core 2 with radio revisions below 4,
 * whatever the boardflags are (seen in the test, scenario radio-band).
 */
static void band_5g(struct bcm4360_radio *r)
{
	u32 core;

	if (r->bf_bit29) {
		for (core = 0; core < BAND_5G_CORES; core++)
			band_5g_core(r, core);
	}
	if (r->rev < R2069_REV_4)
		band_5g_core(r, R2069_CORE2);
}

/*
 * docs/re/spec/acphy-radio.md, section 15
 * (wlc_phy_set_regtbl_on_band_change_acphy), radio part: only with PHY
 * revision 0.
 */
void bcm4360_radio_band_change(struct bcm4360_radio *r, bool is_5g)
{
	if (r->phy_rev != ACPHY_REV_0)
		return;

	if (is_5g)
		band_5g(r);
	else
		band_2g(r);
}

/* docs/re/spec/acphy-radio.md, section 16, step 2: the set-up of a core */
static void tssi_setup_core(struct bcm4360_radio *r, u32 core, u8 mode, bool is_5g)
{
	u16 reg_0x01a = radio_core_reg(R2069_REG_0x01a, core);
	const struct radio_field *f;

	if (mode == 0) {
		radio_mod(r, reg_0x01a, R2069_0x01a_BITS4_7, R2069_0x01a_BITS4_7_MODE0);
		radio_set(r, reg_0x01a, R2069_0x01a_BIT2);
	} else {
		radio_mod(r, reg_0x01a, R2069_0x01a_BITS4_7,
			  is_5g ? R2069_0x01a_BITS4_7_5G : R2069_0x01a_BITS4_7_2G);
		radio_clear(r, reg_0x01a, R2069_0x01a_BIT2);
	}

	if (core < ARRAY_SIZE(tssi_core_fields)) {
		f = &tssi_core_fields[core];
		radio_mod(r, f->reg, f->mask, f->val);
	}

	radio_clear(r, reg_0x01a, R2069_0x01a_BITS8_9);
	radio_clear(r, radio_core_reg(R2069_REG_0x017, core), R2069_0x017_BIT1);
	radio_mod(r, radio_core_reg(R2069_REG_0x01f, core), R2069_0x01f_BIT2,
		  mode << R2069_0x01f_BIT2_SHIFT);
	radio_set(r, radio_core_reg(R2069_REG_0x170, core), R2069_0x170_BIT8);
}

/*
 * docs/re/spec/acphy-radio.md, section 16 (wlc_phy_tssi_radio_setup_acphy):
 * radio set-up of the power detector path of the cores in `coremask`.
 */
void bcm4360_radio_tssi_setup(struct bcm4360_radio *r, u8 coremask, u8 mode,
			      bool is_5g)
{
	u32 core;

	radio_prepare_0x548_to_0x54c(r);
	radio_clear(r, R2069_REG_0x40b, R2069_0x40b_BIT0);

	for (core = 0; core < r->cores; core++) {
		if (coremask & BIT(core))
			tssi_setup_core(r, core, mode, is_5g);
	}
}
