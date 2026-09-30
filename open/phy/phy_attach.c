// SPDX-License-Identifier: ISC
/*
 * The AC-PHY of the BCM4360: attach and detach, the state that attach makes
 * from the variables of the board, the queries of the MAC layer, analog core
 * and radio switch.
 *
 * Written from docs/re/spec/acphy-attach.md ("procedure N" in the comments is
 * a section of its "Procedures"), docs/re/spec/acphy-radio.md, sections 1 to
 * 4, and docs/re/spec/acphy-chanspec.md, sections 3 and 4; for chip 0x4360
 * with an AC-PHY of revision 0 or 1 and a radio 2069 of revision 3 or 4. The
 * branches of other chips, PHY types and revisions are left out.
 *
 * The object makes three structures in two functions, once per band. Here
 * bcm4360_phy_attach() makes one structure, once: it is
 * wlc_phy_shared_attach followed by the wlc_phy_attach for 2.4 GHz.
 */
#include <bcm4360/phy.h>
#include <bcm4360/phy_env.h>

/* docs/re/tasks/phy-attach.md: the layout of the record is a contract with the platform */
_Static_assert(__builtin_offsetof(struct bcm4360_phy_board, pmu) == 48 &&
	       sizeof(struct bcm4360_phy_board) == 56,
	       "struct bcm4360_phy_board is not laid out as the task says");

/* procedure 1, step 4 */
#define WATCHDOG_FAST_TIMER		15
#define WATCHDOG_SLOW_TIMER		60
#define WATCHDOG_GLACIAL_TIMER		120
#define SHARED_INTERFERENCE_MODE	3	/* overwritten by procedure 6, step 29 */

/* procedure 2, step 1: the MAC core revision that has no status flags to read */
#define D11_COREREV_NO_SFLAGS		4
#define D11_COREREV_4_SFLAGS		(D11_SFLAGS_PHY_2G | D11_SFLAGS_PHY_5G)

/* procedure 2, step 4 */
#define WRITE_LIMIT			24	/* a dummy read before every 24th write */
#define WRITE_LIMIT_APPLE_0x093		1	/* a dummy read before every write */
#define TXPWR_PERCENT			100
#define PI_0xf9d_ATTACH			4
#define PI_0xf9f_ATTACH			4

/* procedure 2, step 11 */
#define PI_0xc04_ATTACH			60
#define PI_0xc08_ATTACH			16

/* procedure 2, step 14 */
#define PI_0xf68_ATTACH			10
#define PI_0xf69_ATTACH			3
#define TXPWR_MIN_POWER			1	/* dBm; PHY revision 3: 5 */
#define RXCHAIN_ATTACH			0x03
#define TEMP_CAL_LAST_ATTACH		(-50)
#define TXPWR_USER_TARGET_ATTACH	0x7f
#define RADIOPWR_OVERRIDE_ATTACH	(-1)

/* procedure 2, step 15 */
#define TIMER_PHYCAL_NAME		"phycal"

/* procedure 2, step 21 */
#define PI_0x1144_ATTACH		0xffffffff

/* procedure 5 */
#define TXPWR_INDEX_UNINITTED		0x40	/* PHY revisions 2, 5, 6: 0x3c */
#define PI_0xc36_UNINITTED		0xff
#define PI_0xed2_UNINITTED		0xff

/* procedure 6, step 27: the word of the OTP memory, and its field */
#define OTP_WORD16			16
#define OTP_WORD16_BITS8_12		0x1f00
#define OTP_WORD16_BITS8_12_SHIFT	8
#define OTP_WORD16_RCAL			0x000f	/* procedure 6, step 32 */

/* procedure 12, wlc_phy_ac_caps */
#define PHY_CAPS_REV0			(BCM4360_PHY_CAP_BIT0 | BCM4360_PHY_CAP_STBC | \
					 BCM4360_PHY_CAP_SGI | BCM4360_PHY_CAP_BIT3 | \
					 BCM4360_PHY_CAP_LDPC)
#define PHY_CAPS_REV1			(PHY_CAPS_REV0 | BCM4360_PHY_CAP_VHT_PROP_RATES)

/* acphy-radio.md, "Tables": bytes of an entry of a channel table */
#define CHAN_ENTRY_BYTES		(R2069_CHAN_WORDS * sizeof(u16))

/* procedure 2, step 12: the interference bits the PHY revisions configure */
#define INTERFERENCE_REV1		(BCM4360_INTERFERENCE_DESENSE | \
					 BCM4360_INTERFERENCE_HWACI | BCM4360_INTERFERENCE_W2NB)
#define INTERFERENCE_OTHER		BCM4360_INTERFERENCE_DESENSE

/* procedure 2, step 17: the temperature limits */
#define TEMPTHRESH_DEFAULT		150	/* also used for 0 or 255 */
#define TEMPTHRESH_4360_APPLE		120	/* board 0x117 or 0x111, whatever the variable */
#define TEMPS_HYSTERESIS_DEFAULT	5	/* also used for 0 or 15 */

/* procedure 2, step 20: defaults of the variables read there */
#define TXPWRBCKOF_DEFAULT		6
#define TSSILIMUCOD_DEFAULT		1
#define RSSICORRATTEN_DEFAULT		7
#define RSSICORRPERRG_DEFAULT		(-150)

/* procedure 4 (sub_0be37f): a phycal_tempdelta above this is replaced by the default */
#define PHYCAL_TEMPDELTA_MAX		64
#define PHYCAL_TEMPDELTA_SROM11		40	/* the default set by procedure 7, step 5 */

/* procedure 6, step 3: defaults of the AC-PHY state */
#define ACPHY_0x000_ATTACH		1
#define CRSMINCAL_THRESH_ATTACH		0x36
#define CAL_MODE_PERIODIC		2	/* pi+0xf89 */
#define CAL_PHASE_DELAY_MS		5	/* pi+0xf8a */
#define PI_0xc2b_ATTACH			0xff
#define OLPC_PWROFFSET_ATTACH		1
#define LP_MODE_ATTACH			1	/* pi_ac+0x8e4 */
#define ACPHY_0x8e6_ATTACH		1
#define ACPHY_0x8e7_ATTACH		1
#define CRSMINCAL_NOISE_ATTACH		((s8)0xe2)	/* -30, PHY revision 0/1 */

/* procedure 6, step 6: high RSSI LNA bypass defaults */
#define HIRSSI_0x908_ATTACH		5
#define HIRSSI_COUNT_ATTACH		31
#define HIRSSI_THRESH_A_ATTACH		((s8)0xf3)	/* -13 */
#define HIRSSI_THRESH_B_ATTACH		((s8)0xf1)	/* -15 */
#define HIRSSI_TIMER_OFF		((s16)0xffff)	/* procedure 9: not running */

/* procedure 6, step 8 */
#define ACPHY_0x902_ATTACH		0x404e
#define ACPHY_0x904_ATTACH		0x0fff

/* procedure 6, step 10 */
#define TXPWR_INDEX_SAVED_ATTACH	0x80	/* pi_ac+0x45a */

/* procedure 6, step 30 */
#define ACPHY_0x671_ATTACH		1

/* procedure 11 (wlc_phy_hwaci_init_acphy): the fixed parameters */
#define HWACI_0x672_ATTACH		300
#define HWACI_0x674_ATTACH		1000
#define HWACI_0x676_ATTACH		500
#define HWACI_0x678_ATTACH		1
#define HWACI_0x67a_ATTACH		15
#define HWACI_0x67b_ATTACH		15
#define HWACI_0x67c_ATTACH		1
#define HWACI_0x680_ATTACH		4
#define HWACI_TABLE_ENTRIES		4

/*
 * acphy-attach.md, procedure 2, step 15 (wlapi_init_timer): what the timer
 * "phycal" calls
 */
static void phy_timer_phycal(void *arg)
{
	bcm4360_phy_timer_phycal(arg);
}

/*
 * acphy-attach.md, procedure 3 (bcm_strtoul, base by the prefix): convert a
 * variable value to a 32 bit number. 0x -> hexadecimal, a leading 0 ->
 * octal, else decimal; a leading minus negates modulo 2^32.
 */
static u32 phy_strtoul(const char *s)
{
	bool neg = false;
	u32 base = 10;
	u32 val = 0;

	while (*s == ' ' || *s == '\t')
		s++;
	if (*s == '+' || *s == '-') {
		neg = *s == '-';
		s++;
	}
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	} else if (s[0] == '0' && s[1]) {
		base = 8;
		s++;
	}
	for (;;) {
		char c = *s++;
		u32 d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			d = c - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		val = val * base + d;
	}
	return neg ? (u32)-(s32)val : val;
}

/*
 * acphy-attach.md, procedure 3 (getintvararray / getintvararraysize): the
 * element `index` of a comma separated list. false if the list has fewer
 * elements. Each element is a number in the notation of phy_strtoul.
 */
static bool phy_arr_elem(const char *v, u32 index, u32 *out)
{
	u32 i;

	for (i = 0;; i++) {
		while (*v == ' ' || *v == '\t')
			v++;
		if (i == index) {
			*out = phy_strtoul(v);
			return true;
		}
		while (*v && *v != ',')
			v++;
		if (*v != ',')
			return false;
		v++;
	}
}

/*
 * acphy-attach.md, procedure 3 (phy_getvar / phy_getvar_fabid): does the
 * board have a variable of this name? The fab id is 0 on the BCM4360, so only
 * the plain name is looked up. Every call is a look-up the test compares.
 */
static const char *phy_getvar(const struct bcm4360_phy *phy, const char *name)
{
	return hw_getvar(phy->hw, name);
}

/* acphy-attach.md, procedure 3 (phy_getintvar_default): a scalar variable */
static s32 phy_getint(const struct bcm4360_phy *phy, const char *name, s32 def)
{
	const char *v = phy_getvar(phy, name);

	return v ? (s32)phy_strtoul(v) : def;
}

/* acphy-attach.md, procedure 3 (phy_getintvararray_default): one array element */
static s32 phy_getarr(const struct bcm4360_phy *phy, const char *name, u32 index,
		      s32 def)
{
	const char *v = phy_getvar(phy, name);
	u32 out;

	if (!v || !phy_arr_elem(v, index, &out))
		return def;
	return (s32)out;
}

/*
 * acphy-attach.md, procedure 3: does the variable exist, and if so its value?
 * Used where the object reads a value only when the variable is present.
 */
static bool phy_getvar_val(const struct bcm4360_phy *phy, const char *name, u32 *out)
{
	const char *v = phy_getvar(phy, name);

	if (!v)
		return false;
	*out = phy_strtoul(v);
	return true;
}

/* the name of a per core variable, "<base><core>" (the core is one digit) */
static const char *core_var(char *buf, const char *base, u32 core)
{
	u32 i = 0;

	while (base[i]) {
		buf[i] = base[i];
		i++;
	}
	buf[i++] = (char)('0' + core);
	buf[i] = '\0';
	return buf;
}

/*
 * acphy-attach.md, procedure 1 (wlc_phy_shared_attach): the part of the state
 * that the object shares between the PHYs of a card. Not copied: unit number,
 * bus type and revision of the bus core (the platform does not hand them
 * over; the card is a PCI card).
 */
static void phy_shared_attach(struct bcm4360_phy *phy, struct bcm4360_hw *hw,
			      const struct bcm4360_phy_board *board)
{
	phy->hw = hw;
	phy->pmu = board->pmu;
	hw_memcpy(&phy->board, board, sizeof(phy->board));

	hw_memset(phy->interference.noise_window, 0, sizeof(phy->interference.noise_window));

	phy->fast_timer = WATCHDOG_FAST_TIMER;
	phy->slow_timer = WATCHDOG_SLOW_TIMER;
	phy->glacial_timer = WATCHDOG_GLACIAL_TIMER;
	phy->interference.mode = SHARED_INTERFERENCE_MODE;
	phy->rssi.mode = 0;
}

/*
 * acphy-attach.md, procedure 2, step 1: the status flags of the 802.11 core
 * D11_SFLAGS_*
 */
static u32 phy_attach_sflags(struct bcm4360_phy *phy)
{
	if (phy->board.corerev == D11_COREREV_NO_SFLAGS)
		return D11_COREREV_4_SFLAGS;
	return bcm4360_chip_core_sflags(phy->hw, 0, 0);
}

/* acphy-attach.md, procedure 2, step 4: the first values, the register access */
static void phy_attach_first(struct bcm4360_phy *phy)
{
	u16 limit = WRITE_LIMIT;

	if (phy->board.boardvendor == BCM4360_BOARDVENDOR_APPLE &&
	    phy->board.boardtype == BCM4360_BOARDTYPE_0x093)
		limit = WRITE_LIMIT_APPLE_0x093;
	bcm4360_phy_io_init(&phy->io, phy->hw, true, limit);
	phy->radio.io = &phy->io;

	phy->init_por = true;
	phy->txpwr.percent = TXPWR_PERCENT;
	phy->cal.pi_0xf9d = PI_0xf9d_ATTACH;
	phy->cal.pi_0xf9f = PI_0xf9f_ATTACH;
	phy->temp.cal_delta_default = 0;
}

/*
 * acphy-attach.md, procedure 2, steps 6 to 9: reset the 802.11 core and read
 * what the PHY is. The PHY that attaches first is the one for 2.4 GHz.
 */
static void phy_attach_identify(struct bcm4360_phy *phy, u32 sflags)
{
	u16 v;

	if (sflags & D11_SFLAGS_PHY_2G)
		phy->ver.coreflags = D11_CFLAGS_BAND_2G;
	bcm4360_mac_corereset(phy->hw, phy->ver.coreflags);

	v = d11_read16(phy->hw, D11_PHY_VERSION);
	phy->fabid = 0;		/* si_fabid: 0 for chip 0x4360, without any access */
	phy->ver.phy_type = (v & D11_PHY_VERSION_TYPE) >> D11_PHY_VERSION_TYPE_SHIFT;
	phy->ver.phy_rev = v & D11_PHY_VERSION_REV;
	phy->ver.ana_rev = v >> D11_PHY_VERSION_ANA_SHIFT;
	phy->ver.cores = 1;
}

/*
 * acphy-attach.md, procedure 2, step 10: the PHY types and revisions that
 * this code is written for; the object accepts more.
 */
static bool phy_attach_supported(const struct bcm4360_phy *phy)
{
	return phy->ver.phy_type == BCM4360_PHY_TYPE_AC && phy->ver.phy_rev <= ACPHY_REV_1;
}

/* acphy-attach.md, procedure 2, step 11: the channel the PHY starts with */
static void phy_attach_chanspec(struct bcm4360_phy *phy)
{
	phy->pi_0xc04 = PI_0xc04_ATTACH;
	phy->pi_0xc08 = PI_0xc08_ATTACH;
	phy->interference.flags = 0;
	phy->bw = BCM4360_CHANSPEC_BW_20;
	phy->radio_chanspec = BCM4360_CHANSPEC_FIRST_2G;
	phy->interference.channel = phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL;
}

/* acphy-attach.md, procedure 2, step 14 */
static void phy_attach_defaults(struct bcm4360_phy *phy)
{
	u32 band;

	phy->cal.pi_0xf68 = PI_0xf68_ATTACH;
	phy->cal.pi_0xf69 = PI_0xf69_ATTACH;
	for (band = 0; band < BCM4360_PHY_BANDS; band++)
		phy->rxgainerr[band].empty = false;
	phy->watchdog_override = true;
	phy->cal.pi_0xf82 = 0;
	phy->txpwr.min_power = TXPWR_MIN_POWER;
	phy->rxchain = RXCHAIN_ATTACH;
	phy->temp.cal_last = TEMP_CAL_LAST_ATTACH;
	phy->cal.pi_0x1080 = 0;
	phy->phynoise_polling = false;
	phy->txpwr.ppr = NULL;
	phy->txpwr.user_target = TXPWR_USER_TARGET_ATTACH;
	phy->txpwr.radiopwr_override = RADIOPWR_OVERRIDE_ATTACH;
	phy->txpwr.user_at_rfport = false;
}

/* acphy-attach.md, procedure 5 (wlc_set_phy_uninitted) */
static void phy_set_uninitted(struct bcm4360_phy *phy)
{
	u32 core;

	phy->cal.init_done = false;
	phy->pi_0xc36 = PI_0xc36_UNINITTED;
	phy->pi_0xed2 = PI_0xed2_UNINITTED;
	for (core = 0; core < BCM4360_PHY_CORE_SLOTS; core++)
		phy->txpwr.index[core] = TXPWR_INDEX_UNINITTED;
}

/* acphy-attach.md, procedure 6, step 4: PHY registers as they are at attach */
static void acphy_attach_save_regs(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_saved_regs *s = &phy->saved;
	struct bcm4360_phy_io *io = &phy->io;

	s->phy_0x739 = bcm4360_phy_read(io, ACPHY_REG_0x739);
	s->phy_0x73a = bcm4360_phy_read(io, ACPHY_REG_0x73a);
	s->phy_0x725 = bcm4360_phy_read(io, ACPHY_REG_0x725);
	s->phy_0x729 = bcm4360_phy_read(io, ACPHY_REG_0x729);
	s->phy_0x721 = bcm4360_phy_read(io, ACPHY_REG_0x721);
	s->phy_0x728 = bcm4360_phy_read(io, ACPHY_REG_0x728);
	s->phy_0x720 = bcm4360_phy_read(io, ACPHY_REG_0x720);
	s->phy_0x408 = bcm4360_phy_read(io, ACPHY_REG_0x408);
	s->phy_0x417 = bcm4360_phy_read(io, ACPHY_REG_0x417);
	s->phy_0x416 = bcm4360_phy_read(io, ACPHY_REG_0x416);
}

/*
 * acphy-attach.md, procedure 6, step 14: the number of cores. The register is
 * read in every case.
 */
static void acphy_attach_cores(struct bcm4360_phy *phy)
{
	const struct bcm4360_phy_board *b = &phy->board;

	phy->ver.cores = bcm4360_phy_read(&phy->io, ACPHY_REG_0x00b) & ACPHY_0x00b_CORES;

	if (b->chip == BCM4360_CHIP_ID_4352 ||
	    (b->chip == BCM4360_CHIP_ID_4360 &&
	     (b->boardtype == BCM4360_BOARDTYPE_0x137 || b->boardtype == BCM4360_BOARDTYPE_0x117)))
		phy->ver.cores = 2;
}

/*
 * acphy-attach.md, procedure 6, step 27: bits 8..12 of OTP word 16. If the
 * read fails the word counts as 0.
 */
static void acphy_attach_otp_word16(struct bcm4360_phy *phy)
{
	struct bcm4360_hw *hw = phy->hw;
	u32 sromctl = bcm4360_chip_sromctl_get(hw);
	u16 word = 0;

	if (!(sromctl & CC_SROMCTL_BIT4))
		bcm4360_chip_sromctl_set(hw, sromctl | CC_SROMCTL_BIT4);
	(void)bcm4360_otp_read_word(hw, OTP_WORD16, &word);
	if (!(sromctl & CC_SROMCTL_BIT4))
		bcm4360_chip_sromctl_set(hw, sromctl);

	phy->otp_word16_bits8_12 = (word & OTP_WORD16_BITS8_12) >> OTP_WORD16_BITS8_12_SHIFT;
}

/*
 * acphy-attach.md, procedure 6, step 32: with bit 13 of boardflags3 the RCAL
 * value is read from OTP word 16, without the SROM control register being
 * touched. If the read fails the value stays what the read left (the radio is
 * not identified yet: its major revision is 0 here. Major revisions 1 and 2
 * would get 10 and 9).
 */
static void acphy_attach_otp_rcal(struct bcm4360_phy *phy)
{
	if (!phy->flags.bf3_rcal_otp)
		return;

	if (bcm4360_otp_read_word(phy->hw, OTP_WORD16, &phy->rcal_otp) == 0)
		phy->rcal_otp &= OTP_WORD16_RCAL;
}

/* the lowest `bits` bits of `v` taken as a signed number */
static s32 sext(u32 v, u32 bits)
{
	u32 mask = (1u << bits) - 1;

	v &= mask;
	if (v & (1u << (bits - 1)))
		v |= ~mask;
	return (s32)v;
}

/*
 * acphy-attach.md, procedure 4 (sub_0be37f,
 * wlc_phy_read_tempdelta_settings): phycal_tempdelta, replaced by the current
 * default when it is above 64. Called twice: while the default is 0
 * (procedure 2, step 5) and after procedure 7 set it to 40.
 */
static void phy_read_tempdelta(struct bcm4360_phy *phy)
{
	phy->temp.cal_delta = (u8)phy_getint(phy, "phycal_tempdelta", 0);
	if (phy->temp.cal_delta > PHYCAL_TEMPDELTA_MAX)
		phy->temp.cal_delta = phy->temp.cal_delta_default;
	else
		phy->temp.cal_delta_default = phy->temp.cal_delta;
}

/*
 * acphy-attach.md, procedure 9 (wlc_phy_hirssi_elnabypass_init_acphy): the
 * state of the "high RSSI: bypass the external LNA" function. At attach the
 * clock is off, so no shared memory is written.
 */
static void phy_hirssi_init(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_hirssi *h = &phy->hirssi;

	h->timer_2g = HIRSSI_TIMER_OFF;
	h->timer_5g = HIRSSI_TIMER_OFF;
	if (!h->supported) {
		h->state_2g = 0;
		h->state_5g = 0;
		return;
	}
	h->state_2g = h->state_first;
	h->state_5g = h->state_first;
}

/*
 * acphy-attach.md, procedure 6, steps 3, 6, 8, 10, 15 and 30: the values the
 * AC-PHY sets before it reads the board. The clock frequency (step 9) is
 * si_alp_clock; the object moves the PCI windows for it but reads no register.
 */
static void acphy_attach_defaults(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_hirssi *h = &phy->hirssi;
	u32 band, core;

	/* step 3 */
	phy->acphy_0x000 = ACPHY_0x000_ATTACH;
	phy->band_2g_last = (phy->radio_chanspec & BCM4360_CHANSPEC_BAND) == 0;
	phy->bw_last = phy->radio_chanspec & BCM4360_CHANSPEC_BW;
	phy->spur_mode = 0;
	phy->crsmincal.thresh = CRSMINCAL_THRESH_ATTACH;
	phy->crsmincal.enable = true;
	phy->crsmincal.state[0] = 0;
	phy->crsmincal.state[1] = 0;
	phy->cal.mode = CAL_MODE_PERIODIC;
	phy->cal.phase_delay_ms = CAL_PHASE_DELAY_MS;
	phy->cal.acphy_0x44a = 0;
	phy->acphy_0x8e1 = 0;
	phy->pi_0xc2b = PI_0xc2b_ATTACH;
	phy->flags.elna_2g = false;
	phy->flags.elna_5g = false;
	phy->txpwr.olpc_pwroffset[0] = OLPC_PWROFFSET_ATTACH;
	phy->txpwr.olpc_pwroffset[1] = OLPC_PWROFFSET_ATTACH;
	phy->txpwr.olpc_pwroffset[2] = OLPC_PWROFFSET_ATTACH;
	phy->lp_mode = LP_MODE_ATTACH;
	phy->acphy_0x8e6 = ACPHY_0x8e6_ATTACH;
	phy->acphy_0x8e7 = ACPHY_0x8e7_ATTACH;
	phy->acphy_0x8e8 = 0;
	bcm4360_radio_init(&phy->radio);	/* pi_ac+0x339, 0x33a, 0x33b */

	/* step 6 */
	phy->rxldpc = true;
	phy->watchdog_core = 0;
	h->state_first = 0;
	h->acphy_0x908 = HIRSSI_0x908_ATTACH;
	h->thresh_a = HIRSSI_THRESH_A_ATTACH;
	h->thresh_b = HIRSSI_THRESH_B_ATTACH;
	h->count_a = HIRSSI_COUNT_ATTACH;
	h->count_b = HIRSSI_COUNT_ATTACH;
	h->supported = phy->ver.phy_rev <= ACPHY_REV_1;

	/* step 7 */
	phy_hirssi_init(phy);

	/* step 8 */
	phy->preamble_override = 0;
	phy->cal.acphy_0x900 = 0;
	phy->cal.acphy_0x901 = 1;
	phy->acphy_0x902 = ACPHY_0x902_ATTACH;
	phy->acphy_0x904 = ACPHY_0x904_ATTACH;

	/* step 9 */
	phy->xtal_hz = bcm4360_pmu_alp_clock(phy->pmu);

	/* step 10 */
	for (core = 0; core < BCM4360_PHY_CORE_SLOTS; core++)
		phy->txpwr.index_saved[core] = TXPWR_INDEX_SAVED_ATTACH;
	phy->rxgain_stage_entries[0] = 2;
	phy->rxgain_stage_entries[1] = 6;
	phy->rxgain_stage_entries[2] = 7;
	phy->rxgain_stage_entries[3] = 10;
	phy->rxgain_stage_entries[4] = 8;
	phy->rxgain_stage_entries[5] = 8;

	/* step 15 */
	phy->txpwr.hwpwrctrl_capable = true;
	phy->txpwr.hwpwrctrl = true;
	phy->pi_0x224 = 1;

	/* step 16: CRS minimum power calibration, PHY revision 0/1 */
	for (band = 0; band < BCM4360_PHY_BANDS; band++) {
		for (core = 0; core < BCM4360_PHY_CORES_MAX; core++)
			phy->crsmincal.noise[band][core] = CRSMINCAL_NOISE_ATTACH;
	}

	/* step 30 */
	phy->acphy_0x671 = ACPHY_0x671_ATTACH;
}

/* acphy-attach.md, procedure 6, step 11: boardflags3, split into flags */
static void acphy_attach_boardflags3(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_board_flags *f = &phy->flags;
	u32 bf3 = (u32)phy_getint(phy, "boardflags3", 0);

	f->bf3_bits0_2 = bf3 & BCM4360_BOARDFLAGS3_BITS0_2;
	f->bf3_skip_rcal = (bf3 & BCM4360_BOARDFLAGS3_SKIP_RCAL) != 0;
	f->bf3_bits4_6 = (bf3 & BCM4360_BOARDFLAGS3_BITS4_6) >> BCM4360_BOARDFLAGS3_BITS4_6_SHIFT;
	f->bf3_bit7 = (bf3 & BCM4360_BOARDFLAGS3_BIT7) != 0;
	f->bf3_swctrlmap = (bf3 & BCM4360_BOARDFLAGS3_SWCTRLMAP) != 0;
	f->bf3_bit9 = (bf3 & BCM4360_BOARDFLAGS3_BIT9) != 0;
	f->bf3_bit10 = (bf3 & BCM4360_BOARDFLAGS3_BIT10) != 0;
	f->bf3_bit11 = (bf3 & BCM4360_BOARDFLAGS3_BIT11) != 0;
	f->bf3_spurmode = (bf3 & BCM4360_BOARDFLAGS3_SPURMODE) != 0;
	f->bf3_rcal_otp = (bf3 & BCM4360_BOARDFLAGS3_RCAL_OTP) != 0;
	f->bf3_bit14 = (bf3 & BCM4360_BOARDFLAGS3_BIT14) != 0;
	f->bf3_bit15 = (bf3 & BCM4360_BOARDFLAGS3_BIT15) != 0;
}

/*
 * acphy-attach.md, procedure 6, steps 11, 12 and 13: the variables of the
 * board that describe the front end, and the flags taken from the boardflags.
 */
static void acphy_attach_vars(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_board_flags *f = &phy->flags;
	const struct bcm4360_phy_board *b = &phy->board;
	char buf[32];
	u32 i;

	/* step 11 */
	phy->subband5gver = (u8)phy_getint(phy, "subband5gver", 4);
	phy->txpwr.extpagain2g = (u8)phy_getint(phy, "extpagain2g", 0);
	phy->txpwr.extpagain5g = (u8)phy_getint(phy, "extpagain5g", 0);
	phy->femctrl = (u8)phy_getint(phy, "femctrl", 0);
	acphy_attach_boardflags3(phy);
	phy->rpcal2g = (u16)phy_getint(phy, "rpcal2g", 0);
	for (i = 0; i < BCM4360_PHY_SUBBANDS_5G; i++)
		phy->rpcal5gb[i] = (u16)phy_getint(phy, core_var(buf, "rpcal5gb", i), 0);
	phy->txpwr.txidxcap2g = (u8)phy_getint(phy, "txidxcap2g", 0);
	phy->txpwr.txidxcap5g = (u8)phy_getint(phy, "txidxcap5g", 0);

	/* step 12 */
	f->bf2_bit1 = (b->boardflags2 & BCM4360_BOARDFLAGS2_BIT1) != 0;
	f->bf_bit0 = (b->boardflags & BCM4360_BOARDFLAGS_BIT0) != 0;
	f->bf_bit29 = (b->boardflags & BCM4360_BOARDFLAGS_BIT29) != 0;

	/* step 13 */
	phy->txpwr.pdgain2g = (u8)phy_getint(phy, "pdgain2g", 0);
	phy->txpwr.pdgain5g = (u8)phy_getint(phy, "pdgain5g", 0);
	phy->cckdigfilttype = (u8)phy_getint(phy, "cckdigfilttype", 1);
}

/*
 * acphy-attach.md, procedure 6, step 18: the receive gains of a band and a
 * core. E[0] gain of the external LNA, E[1] isolation of the T/R switch, E[2]
 * the T/R switch bypasses the external LNA. The two variables that mean an
 * external LNA are only read when the band has one.
 */
static void rxgain_band(struct bcm4360_phy *phy, enum bcm4360_phy_rxgain_band band,
			u32 core, bool have_elna, const char *elna, const char *triso,
			const char *trelnabyp)
{
	struct bcm4360_phy_rxgains *e = &phy->rxgains[band][core];
	char buf[32];
	u32 v;

	if (have_elna) {
		if (phy_getvar_val(phy, core_var(buf, elna, core), &v))
			e->elna_gain = (u8)(2 * v + 6);
		if (phy_getvar_val(phy, core_var(buf, trelnabyp, core), &v))
			e->trelnabyp = (u8)v;
	}
	if (phy_getvar_val(phy, core_var(buf, triso, core), &v))
		e->triso = (u8)(2 * v + 8);
}

/*
 * acphy-attach.md, procedure 6, step 18, mid and high 5 GHz: the values are
 * taken as they are (0 when absent or the band has no external LNA); a blank
 * SROM or all zero makes the entry a copy of the band below.
 */
static void rxgain_band_copy(struct bcm4360_phy *phy, enum bcm4360_phy_rxgain_band band,
			     u32 core, bool have_elna, const char *elna,
			     const char *triso, const char *trelnabyp)
{
	struct bcm4360_phy_rxgains *e = &phy->rxgains[band][core];
	char buf[32];
	u32 g = 0, y = 0, t = 0, v;

	if (have_elna && phy_getvar_val(phy, core_var(buf, elna, core), &v))
		g = v;
	if (have_elna && phy_getvar_val(phy, core_var(buf, trelnabyp, core), &v))
		y = v;
	if (phy_getvar_val(phy, core_var(buf, triso, core), &v))
		t = v;

	if ((g == 0 && y == 0 && t == 0) || (g == 7 && y == 1 && t == 15)) {
		*e = phy->rxgains[band - 1][core];
		return;
	}
	e->elna_gain = (u8)(2 * g + 6);
	e->triso = (u8)(2 * t + 8);
	e->trelnabyp = (u8)y;
}

/*
 * acphy-attach.md, procedure 7, step 1: the maximum power, the parameters of
 * the power detector curve and the tssi floor, for three cores and five bands
 * (band 0 = 2.4 GHz, k = band - 1 the 5 GHz sub-band).
 */
static void txpwr_srom11_paparams(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_txpwr *tx = &phy->txpwr;
	char buf[32];
	u32 band, core, k, i;

	for (band = 0; band < BCM4360_PHY_BANDS; band++) {
		for (core = 0; core < BCM4360_PHY_CORES_MAX; core++) {
			if (band == BCM4360_PHY_BAND_2G) {
				tx->maxp[core][band] = (u8)phy_getint(phy,
					core_var(buf, "maxp2ga", core), 0);
				for (i = 0; i < BCM4360_PHY_PA_PARAMS; i++)
					tx->pa[core][band][i] = (s16)phy_getarr(phy,
						core_var(buf, "pa2ga", core), i, 0);
			} else {
				k = band - 1;
				tx->maxp[core][band] = (u8)phy_getarr(phy,
					core_var(buf, "maxp5ga", core), k, 0);
				for (i = 0; i < BCM4360_PHY_PA_PARAMS; i++)
					tx->pa[core][band][i] = (s16)phy_getarr(phy,
						core_var(buf, "pa5ga", core), 3 * k + i, 0);
			}
		}
		if (band == BCM4360_PHY_BAND_2G)
			tx->tssifloor[band] = (s16)phy_getarr(phy, "tssifloor2g", 0, 0);
		else
			tx->tssifloor[band] = (s16)phy_getarr(phy, "tssifloor5g", band - 1, 0);
	}
}

/* acphy-attach.md, "Tx power offsets": the three ranges of a 5 GHz group */
static void po_range3(u32 dst[BCM4360_PHY_PO_RANGES_5G], const u32 src[BCM4360_PHY_PO_RANGES_5G])
{
	u32 i;

	for (i = 0; i < BCM4360_PHY_PO_RANGES_5G; i++)
		dst[i] = src[i];
}

/*
 * acphy-attach.md, procedure 7, step 2 and "Tx power offsets": the power
 * offsets per rate group. The object keeps some of them several times; the
 * copies are kept here in the order of the object.
 */
static void txpwr_srom11_offsets(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_srom_po *po = &phy->txpwr.po;
	u32 ofdmlr, hrbw, n0, n1;
	u32 mcs20[BCM4360_PHY_PO_RANGES_5G];
	u32 mcs40[BCM4360_PHY_PO_RANGES_5G];
	u32 mcs80[BCM4360_PHY_PO_RANGES_5G];
	u32 i;

	if (phy->board.sromrev <= 10)
		return;

	po->cckbw202gpo = (u16)phy_getint(phy, "cckbw202gpo", 0);
	po->cckbw20ul2gpo = (u16)phy_getint(phy, "cckbw20ul2gpo", 0);

	ofdmlr = (u32)phy_getint(phy, "ofdmlrbw202gpo", 0);
	hrbw = (u32)phy_getint(phy, "dot11agofdmhrbw202gpo", 0);
	n0 = ofdmlr & 0xf;
	n1 = (ofdmlr >> 4) & 0xf;
	po->ofdm_2g[0] = hrbw << 16 | n1 << 12 | n1 << 8 | n0 << 4 | n0;
	po->ofdm_2g[1] = po->ofdm_2g[0];
	po->ofdm_2g[2] = po->ofdm_2g[0];

	po->mcs_2g[0] = (u32)phy_getint(phy, "mcsbw202gpo", 0);
	po->mcs_2g[1] = po->mcs_2g[0];
	po->mcs_2g[2] = (u32)phy_getint(phy, "mcsbw402gpo", 0);
	po->mcs_2g[3] = 0;

	mcs20[0] = (u32)phy_getint(phy, "mcsbw205glpo", 0);
	mcs20[1] = (u32)phy_getint(phy, "mcsbw205gmpo", 0);
	mcs20[2] = (u32)phy_getint(phy, "mcsbw205ghpo", 0);
	mcs40[0] = (u32)phy_getint(phy, "mcsbw405glpo", 0);
	mcs40[1] = (u32)phy_getint(phy, "mcsbw405gmpo", 0);
	mcs40[2] = (u32)phy_getint(phy, "mcsbw405ghpo", 0);
	mcs80[0] = (u32)phy_getint(phy, "mcsbw805glpo", 0);
	mcs80[1] = (u32)phy_getint(phy, "mcsbw805gmpo", 0);
	mcs80[2] = (u32)phy_getint(phy, "mcsbw805ghpo", 0);
	for (i = 0; i < BCM4360_PHY_PO_GROUPS_5G; i++) {
		if (i == 3 || i == 4 || i == 9 || i == 10)
			po_range3(po->mcs_5g[i], mcs40);
		else if (i == 5 || i == 11)
			po_range3(po->mcs_5g[i], mcs80);
		else
			po_range3(po->mcs_5g[i], mcs20);
	}

	po->ofdmlrbw202gpo = (u16)ofdmlr;
	po->sb20in40lrpo = (u16)phy_getint(phy, "sb20in40lrpo", 0);
	po->sb20in40hrpo = (u16)phy_getint(phy, "sb20in40hrpo", 0);
	po->dot11agduplrpo = (u16)phy_getint(phy, "dot11agduplrpo", 0);
	po->dot11agduphrpo = (u16)phy_getint(phy, "dot11agduphrpo", 0);
	po->mcslr5gpo[0] = (u16)phy_getint(phy, "mcslr5glpo", 0);
	po->mcslr5gpo[1] = (u16)phy_getint(phy, "mcslr5gmpo", 0);
	po->mcslr5gpo[2] = (u16)phy_getint(phy, "mcslr5ghpo", 0);
	po->sb20in80and160lr5gpo[0] = (u16)phy_getint(phy, "sb20in80and160lr5glpo", 0);
	po->sb20in80and160lr5gpo[1] = (u16)phy_getint(phy, "sb20in80and160lr5gmpo", 0);
	po->sb20in80and160lr5gpo[2] = (u16)phy_getint(phy, "sb20in80and160lr5ghpo", 0);
	po->sb20in80and160hr5gpo[0] = (u16)phy_getint(phy, "sb20in80and160hr5glpo", 0);
	po->sb20in80and160hr5gpo[1] = (u16)phy_getint(phy, "sb20in80and160hr5gmpo", 0);
	po->sb20in80and160hr5gpo[2] = (u16)phy_getint(phy, "sb20in80and160hr5ghpo", 0);
	po->sb40and80lr5gpo[0] = (u16)phy_getint(phy, "sb40and80lr5glpo", 0);
	po->sb40and80lr5gpo[1] = (u16)phy_getint(phy, "sb40and80lr5gmpo", 0);
	po->sb40and80lr5gpo[2] = (u16)phy_getint(phy, "sb40and80lr5ghpo", 0);
	po->sb40and80hr5gpo[0] = (u16)phy_getint(phy, "sb40and80hr5glpo", 0);
	po->sb40and80hr5gpo[1] = (u16)phy_getint(phy, "sb40and80hr5gmpo", 0);
	po->sb40and80hr5gpo[2] = (u16)phy_getint(phy, "sb40and80hr5ghpo", 0);
}

/* acphy-attach.md, procedure 7, step 3: the power detector offsets */
static void txpwr_srom11_pdoffset(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_txpwr *tx = &phy->txpwr;
	char buf[32];
	u32 c;

	for (c = 0; c < BCM4360_PHY_CORES_MAX; c++) {
		tx->pdoffset40ma[c] = (u16)phy_getint(phy, core_var(buf, "pdoffset40ma", c), 0);
		tx->pdoffset80ma[c] = (u16)phy_getint(phy, core_var(buf, "pdoffset80ma", c), 0);
		tx->pdoffset2g40ma[c] = (u8)phy_getint(phy, core_var(buf, "pdoffset2g40ma", c), 0);
		tx->pdoffsetcckma[c] = (u8)phy_getint(phy, core_var(buf, "pdoffsetcckma", c), 0);
	}
	tx->pdoffset2g40mvalid = (u8)phy_getint(phy, "pdoffset2g40mvalid", 0);
}

/*
 * acphy-attach.md, procedure 7, step 4: tempoffset, mapped to a small signed
 * offset. wlc_phy_attach clears the field again afterwards (procedure 2, step
 * 17), so the variable has no effect; it is read all the same.
 */
static void txpwr_srom11_tempoffset(struct bcm4360_phy *phy)
{
	s32 o = sext((u32)phy_getint(phy, "tempoffset", 0), 8);

	if (o == -1 || o == 0)
		phy->temp.offset = 0;
	else if (o > 48)
		phy->temp.offset = 16;
	else if (o < 16)
		phy->temp.offset = -16;
	else
		phy->temp.offset = (s8)(o - 32);
}

/*
 * acphy-attach.md, procedure 7 (wlc_phy_txpwr_srom11_read): the tx power
 * description of SROM revision 11. No hardware access.
 */
static void phy_txpwr_srom11_read(struct bcm4360_phy *phy)
{
	txpwr_srom11_paparams(phy);
	txpwr_srom11_offsets(phy);
	txpwr_srom11_pdoffset(phy);
	txpwr_srom11_tempoffset(phy);

	phy->temp.cal_delta_default = PHYCAL_TEMPDELTA_SROM11;	/* step 5 */
	phy_read_tempdelta(phy);
}

/*
 * acphy-attach.md, procedure 6, step 20: the reference for the temperature
 * compensation. Returns whether the 9 bit value is -1 (the gain errors need
 * it).
 */
static bool acphy_attach_temp_ref(struct bcm4360_phy *phy)
{
	s32 t = sext((u32)phy_getint(phy, "rawtempsense", 0), 9);

	phy->temp.rawtempsense = (t == -1) ? 255 : (s16)t;
	return t == -1;
}

/*
 * acphy-attach.md, procedure 6, steps 21 and 22: the receive gain errors. The
 * SROM holds the error of core 0 and the difference of cores 1 and 2 to it.
 */
static void gainerr_band(struct bcm4360_phy *phy, enum bcm4360_phy_rxgain_band band,
			 s32 a0, s32 a1, s32 a2, bool tneg)
{
	struct bcm4360_phy_rxgainerr *e = &phy->rxgainerr[band];

	if (a0 == -1 && a1 == -1 && a2 == -1 && tneg) {
		e->empty = true;
		a0 = a1 = a2 = 0;
	} else {
		e->empty = false;
	}
	e->err[0] = (s8)a0;
	e->err[1] = (s8)(a0 + a1);
	e->err[2] = (s8)(a0 + a2);
}

/* acphy-attach.md, procedure 6, steps 21 and 22: gain errors of both bands */
static void acphy_attach_rxgainerr(struct bcm4360_phy *phy, bool tneg)
{
	s32 a0, a1, a2;
	u32 k;

	/* step 21: 2.4 GHz */
	a0 = sext((u32)phy_getint(phy, "rxgainerr2ga0", 0), 6);
	a1 = sext((u32)phy_getint(phy, "rxgainerr2ga1", 0), 5);
	a2 = sext((u32)phy_getint(phy, "rxgainerr2ga2", 0), 5);
	gainerr_band(phy, BCM4360_PHY_RXGAIN_2G, a0, a1, a2, tneg);

	/* step 22: the four 5 GHz sub-bands */
	for (k = 0; k < BCM4360_PHY_SUBBANDS_5G; k++) {
		a0 = sext((u32)phy_getarr(phy, "rxgainerr5ga0", k, 0), 6);
		a1 = sext((u32)phy_getarr(phy, "rxgainerr5ga1", k, 0), 5);
		a2 = sext((u32)phy_getarr(phy, "rxgainerr5ga2", k, 0), 5);
		gainerr_band(phy, BCM4360_PHY_BAND_5G + k, a0, a1, a2, tneg);
	}
}

/* acphy-attach.md, procedure 6, steps 23 and 24: the noise level per core in dBm */
static void noiselvl_band(struct bcm4360_phy *phy, enum bcm4360_phy_rxgain_band band,
			  s32 n0, s32 n1, s32 n2)
{
	u32 core;

	for (core = 0; core < phy->ver.cores; core++)
		phy->noiselvl[band][core] = (s8)0xba;	/* -70 */
	phy->noiselvl[band][0] -= (s8)n0;
	phy->noiselvl[band][1] -= (s8)n1;
	phy->noiselvl[band][2] -= (s8)n2;
}

/* acphy-attach.md, procedure 6, steps 23 and 24: the noise levels of both bands */
static void acphy_attach_noiselvl(struct bcm4360_phy *phy)
{
	s32 n0, n1, n2;
	u32 k;

	n0 = (u8)phy_getint(phy, "noiselvl2ga0", 0);
	n1 = (u8)phy_getint(phy, "noiselvl2ga1", 0);
	n2 = (u8)phy_getint(phy, "noiselvl2ga2", 0);
	noiselvl_band(phy, BCM4360_PHY_RXGAIN_2G, n0, n1, n2);

	for (k = 0; k < BCM4360_PHY_SUBBANDS_5G; k++) {
		n0 = (u8)phy_getarr(phy, "noiselvl5ga0", k, 0);
		n1 = (u8)phy_getarr(phy, "noiselvl5ga1", k, 0);
		n2 = (u8)phy_getarr(phy, "noiselvl5ga2", k, 0);
		noiselvl_band(phy, BCM4360_PHY_BAND_5G + k, n0, n1, n2);
	}
}

/*
 * acphy-attach.md, procedure 6, step 25: the switch control maps, only with
 * bit 8 of boardflags3. Each variable that exists gives its elements 0..4.
 */
static void acphy_attach_swctrlmap_one(struct bcm4360_phy *phy, const char *name, u32 *dst)
{
	const char *v = phy_getvar(phy, name);
	u32 out, i;

	if (!v)
		return;
	for (i = 0; i < BCM4360_PHY_SWCTRLMAP; i++)
		dst[i] = phy_arr_elem(v, i, &out) ? out : 0;
}

/* acphy-attach.md, procedure 6, step 25 */
static void acphy_attach_swctrlmap(struct bcm4360_phy *phy)
{
	if (!phy->flags.bf3_swctrlmap)
		return;

	acphy_attach_swctrlmap_one(phy, "swctrlmap_2g", phy->swctrlmap_2g);
	acphy_attach_swctrlmap_one(phy, "swctrlmapext_2g", phy->swctrlmapext_2g);
	acphy_attach_swctrlmap_one(phy, "swctrlmap_5g", phy->swctrlmap_5g);
	acphy_attach_swctrlmap_one(phy, "swctrlmapext_5g", phy->swctrlmapext_5g);
}

/*
 * acphy-attach.md, procedure 8 (sub_0a1604,
 * wlc_phy_srom_read_rssicorrnorm_acphy): the RSSI correction per core. No
 * hardware access.
 */
static void phy_srom_rssicorrnorm(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_rssi *rssi = &phy->rssi;
	char buf[32];
	const char *v;
	u32 out, c, i;

	for (c = 0; c < BCM4360_PHY_CORES_MAX; c++) {
		v = phy_getvar(phy, core_var(buf, "rssicorrnorm_c", c));
		if (v) {
			for (i = 0; i < 2; i++)
				rssi->corrnorm_c[c][i] = phy_arr_elem(v, i, &out) ? (s8)out : 0;
		}
		v = phy_getvar(phy, core_var(buf, "rssicorrnorm5g_c", c));
		if (v) {
			for (i = 0; i < BCM4360_PHY_PO_GROUPS_5G; i++)
				rssi->corrnorm5g_c[c][i] = phy_arr_elem(v, i, &out) ? (s8)out : 0;
		}
	}
}

/*
 * acphy-attach.md, procedure 11 (wlc_phy_hwaci_init_acphy): the defaults of
 * the interference mitigation. No hardware access.
 */
static void phy_hwaci_init(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_hwaci *h = &phy->hwaci;
	static const struct bcm4360_phy_hwaci_entry table1[HWACI_TABLE_ENTRIES] = {
		{ 0xffff, 5, 6, 0, 30, 4 },
		{ 4000,   5, 4, 0, 30, 4 },
		{ 8000,   4, 4, 1, 22, 4 },
		{ 11000,  3, 4, 2, 10, 4 },
	};
	static const struct bcm4360_phy_hwaci_entry table2[HWACI_TABLE_ENTRIES] = {
		{ 0xffff, 5, 6, 0, 30, 4 },
		{ 1000,   5, 4, 0, 30, 4 },
		{ 6000,   4, 4, 1, 25, 4 },
		{ 10000,  3, 4, 2, 15, 4 },
	};

	h->acphy_0x672 = HWACI_0x672_ATTACH;
	h->acphy_0x674 = HWACI_0x674_ATTACH;
	h->acphy_0x676 = HWACI_0x676_ATTACH;
	h->acphy_0x678 = HWACI_0x678_ATTACH;
	h->acphy_0x67a = HWACI_0x67a_ATTACH;
	h->acphy_0x67b = HWACI_0x67b_ATTACH;
	h->acphy_0x67c = HWACI_0x67c_ATTACH;
	h->acphy_0x67d = 0;
	h->acphy_0x67e = 0;
	h->acphy_0x67f = 0;
	h->acphy_0x680 = HWACI_0x680_ATTACH;
	hw_memcpy(h->table1, table1, sizeof(h->table1));
	hw_memcpy(h->table2, table2, sizeof(h->table2));
	h->table1_entries = HWACI_TABLE_ENTRIES;
	h->table2_entries = HWACI_TABLE_ENTRIES;
}

/* acphy-attach.md, procedure 6, steps 17 and 18: the receive gains of every core */
static void acphy_attach_rxgains(struct bcm4360_phy *phy)
{
	bool e2g, e5g;
	u32 core;

	/* step 17 */
	phy->flags.elna_2g = (phy->board.boardflags & BCM4360_BOARDFLAGS_ELNA_2G) != 0;
	phy->flags.elna_5g = (phy->board.boardflags & BCM4360_BOARDFLAGS_ELNA_5G) != 0;
	e2g = phy->flags.elna_2g;
	e5g = phy->flags.elna_5g;

	/* step 18 */
	for (core = 0; core < phy->ver.cores; core++) {
		rxgain_band(phy, BCM4360_PHY_RXGAIN_2G, core, e2g,
			    "rxgains2gelnagaina", "rxgains2gtrisoa", "rxgains2gtrelnabypa");
		rxgain_band(phy, BCM4360_PHY_RXGAIN_5G_LOW, core, e5g,
			    "rxgains5gelnagaina", "rxgains5gtrisoa", "rxgains5gtrelnabypa");
		rxgain_band_copy(phy, BCM4360_PHY_RXGAIN_5G_MID, core, e5g,
				 "rxgains5gmelnagaina", "rxgains5gmtrisoa", "rxgains5gmtrelnabypa");
		rxgain_band_copy(phy, BCM4360_PHY_RXGAIN_5G_HIGH, core, e5g,
				 "rxgains5ghelnagaina", "rxgains5ghtrisoa", "rxgains5ghtrelnabypa");
	}
}

/*
 * acphy-attach.md, procedure 6 (wlc_phy_attach_acphy): create the AC-PHY
 * state, read the board. The hardware accesses are the ten saved PHY
 * registers (step 4), PHY(0x0b) (step 14) and the two OTP reads (steps 27 and
 * 32), in this order; everything else is memory and board variables. Not
 * done: the two global variables of steps 1 and 5 (the chip id is in
 * phy->board), the function pointer table of step 29 (there is one kind of
 * PHY).
 */
static bool acphy_attach(struct bcm4360_phy *phy)
{
	bool tneg;

	acphy_attach_save_regs(phy);		/* step 4 */
	acphy_attach_defaults(phy);		/* steps 3, 6-10, 15, 16, 30 */
	acphy_attach_vars(phy);			/* steps 11, 12, 13 */
	acphy_attach_cores(phy);		/* step 14 */
	acphy_attach_rxgains(phy);		/* steps 17, 18 */
	phy_txpwr_srom11_read(phy);		/* step 19 (procedure 7) */
	tneg = acphy_attach_temp_ref(phy);	/* step 20 */
	acphy_attach_rxgainerr(phy, tneg);	/* steps 21, 22 */
	acphy_attach_noiselvl(phy);		/* steps 23, 24 */
	acphy_attach_swctrlmap(phy);		/* step 25 */
	phy_srom_rssicorrnorm(phy);		/* step 26 (procedure 8) */
	acphy_attach_otp_word16(phy);		/* step 27 */
	phy->lpvco_2g = false;			/* step 28: 0 for PHY revision 0/1 */
	phy->interference.mode = 0;		/* step 29 */
	phy_hwaci_init(phy);			/* step 31 */
	phy->pi_0x1165 = 0;
	acphy_attach_otp_rcal(phy);		/* step 32 */
	return true;
}

/*
 * acphy-radio.md, section 2, steps 1 to 3: read the identification of the
 * radio, with plain accesses to the registers of the 802.11 core
 */
static void phy_attach_radio_id(struct bcm4360_phy *phy)
{
	struct bcm4360_hw *hw = phy->hw;
	u16 rev, id;

	d11_write16(hw, D11_RADIO_ADDR, R2069_REG_REV);
	rev = d11_read16(hw, D11_RADIO_DATA);
	d11_write16(hw, D11_RADIO_ADDR, R2069_REG_ID);
	id = d11_read16(hw, D11_RADIO_DATA);

	phy->ver.radio_id = id;
	phy->ver.radio_rev = rev & R2069_REV_REV;
	phy->ver.radio_major = (rev >> R2069_REV_MAJOR_SHIFT) & R2069_REV_MAJOR;
	phy->ver.radio_minor = rev & R2069_REV_MINOR;
	phy->ver.radio_ver = 0;
	phy->io.write_count = 0;
}

/*
 * acphy-radio.md, "Tables": the tables of the radio by its revision. Broadcom's
 * data comes from the platform (docs/re/tasks/phy-attach.md). Returns false
 * if there is no table of channels: revisions other than 3 and 4.
 */
static bool phy_attach_radio_tables(struct bcm4360_phy *phy)
{
	struct bcm4360_radio_tables *tbl = &phy->radio.tbl;
	const char *prefregs, *chan_tuning;
	u32 size = 0;

	switch (phy->ver.radio_rev) {
	case R2069_REV_3:
		prefregs = "prefregs_2069_rev3";
		chan_tuning = "chan_tuning_2069rev3";
		break;
	case R2069_REV_4:
		prefregs = "prefregs_2069_rev4";
		chan_tuning = "chan_tuning_2069rev4";
		break;
	default:
		return false;
	}

	tbl->prefregs = hw_fw_data(phy->hw, prefregs, &size);
	size = 0;
	tbl->chan_tuning = hw_fw_data(phy->hw, chan_tuning, &size);
	tbl->chan_entries = size / CHAN_ENTRY_BYTES;

	return tbl->chan_tuning && tbl->chan_entries;
}

/* docs/re/tasks/radio.md: what the code of the radio needs to know of the PHY */
static void phy_attach_radio_state(struct bcm4360_phy *phy)
{
	struct bcm4360_radio *r = &phy->radio;

	r->cores = phy->ver.cores;
	r->rev = phy->ver.radio_rev;
	r->phy_rev = phy->ver.phy_rev;
	r->boardflags = phy->board.boardflags;
	r->skip_rcal = phy->flags.bf3_skip_rcal || phy->flags.bf3_rcal_otp;
	r->bf_bit29 = phy->flags.bf_bit29;
}

/*
 * acphy-attach.md, procedure 2, step 19; acphy-radio.md, section 2: analog
 * core on, identify the radio, radio off. The radio is accepted after it was
 * switched off. Left out: the radio with the id 0x030b (other chips).
 */
static bool phy_attach_radio(struct bcm4360_phy *phy)
{
	bcm4360_phy_anacore(phy, true);
	phy_attach_radio_id(phy);
	bcm4360_phy_switch_radio(phy, false);

	if (phy->ver.radio_id != R2069_ID)
		return false;
	if (!phy_attach_radio_tables(phy))
		return false;
	phy_attach_radio_state(phy);
	return true;
}

/*
 * acphy-attach.md, procedure 2, steps 12 and 13: the interference mitigation
 * modes configured for the two bands. The PHY revision sets a default; a
 * variable `interference` overrides it. Procedure 6, step 29 clears the mode
 * in use afterwards.
 */
static void phy_attach_interference(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_interference *in = &phy->interference;
	u32 rev = phy->ver.phy_rev;
	u32 v;

	/* step 12 */
	in->forced = false;
	in->mode_2g = 0;
	in->mode_5g = 0;
	if (rev <= 3 || rev == 5 || rev == 6) {
		in->mode_2g = INTERFERENCE_OTHER;
		in->mode_5g = INTERFERENCE_OTHER;
	}
	if (rev == ACPHY_REV_1) {
		in->mode_2g = INTERFERENCE_REV1;
		in->mode_5g = INTERFERENCE_REV1;
	}

	/* step 13: the first attach is for 2.4 GHz, so the mode in use is mode_2g */
	if (phy_getvar_val(phy, "interference", &v)) {
		in->mode_2g = v;
		in->mode_5g = v;
		in->mode = v;
	}
}

/*
 * acphy-attach.md, procedure 2, step 17: the temperatures at which the tx
 * chains are switched off and on again. On the BCM4360 boards 0x117 and 0x111
 * the threshold is always 120, whatever the variable says. This also clears
 * the tempoffset that procedure 7 computed.
 */
static void phy_attach_temp_limits(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_temp *t = &phy->temp;
	const struct bcm4360_phy_board *b = &phy->board;
	u8 thresh, hyst, m;

	thresh = (u8)phy_getint(phy, "tempthresh", 0);
	if (thresh == 0 || thresh == 255)
		thresh = TEMPTHRESH_DEFAULT;
	if (b->chip == BCM4360_CHIP_ID_4360 &&
	    (b->boardtype == BCM4360_BOARDTYPE_0x117 || b->boardtype == BCM4360_BOARDTYPE_0x111))
		thresh = TEMPTHRESH_4360_APPLE;
	t->thresh = thresh;
	t->thresh_max = thresh;

	hyst = (u8)phy_getint(phy, "temps_hysteresis", 0);
	if (hyst == 0 || hyst == 15)
		hyst = TEMPS_HYSTERESIS_DEFAULT;
	t->hysteresis = hyst;
	t->thresh_on = (u8)(thresh - hyst);
	t->heated = false;
	t->pi_0xc34 = 0;
	t->offset = 0;

	m = (u8)((1u << phy->ver.cores) - 1);
	t->chain_bitmap = (u8)(m | m << 4);
}

/*
 * acphy-attach.md, procedure 2, step 20: the last variables, read with their
 * defaults after the radio has been identified.
 */
static void phy_attach_more_vars(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_rssi *rssi = &phy->rssi;
	struct bcm4360_phy_txpwr *tx = &phy->txpwr;
	u32 i;

	tx->txpwrbckof = (u8)phy_getint(phy, "txpwrbckof", TXPWRBCKOF_DEFAULT);
	tx->tssilimucod = (u8)phy_getint(phy, "tssilimucod", TSSILIMUCOD_DEFAULT);
	rssi->corrnorm = (s8)phy_getint(phy, "rssicorrnorm", 0);
	rssi->corratten = (s8)phy_getint(phy, "rssicorratten", RSSICORRATTEN_DEFAULT);
	for (i = 0; i < BCM4360_PHY_PO_RANGES_5G; i++) {
		rssi->corrnorm5g[i] = (s8)phy_getarr(phy, "rssicorrnorm5g", i, 0);
		rssi->corratten5g[i] = (s8)phy_getarr(phy, "rssicorratten5g", i, 0);
	}
	for (i = 0; i < 5; i++) {
		s32 def = i < 2 ? RSSICORRPERRG_DEFAULT : 0;

		rssi->corrperrg2g[i] = (s8)phy_getarr(phy, "rssicorrperrg2g", i, def);
		rssi->corrperrg5g[i] = (s8)phy_getarr(phy, "rssicorrperrg5g", i, def);
	}
	for (i = 0; i < ARRAY_SIZE(rssi->cga_5g); i++)
		rssi->cga_5g[i] = (s8)phy_getarr(phy, "5g_cga", i, 0);
	for (i = 0; i < ARRAY_SIZE(rssi->cga_2g); i++)
		rssi->cga_2g[i] = (s8)phy_getarr(phy, "2g_cga", i, 0);
}

/* acphy-attach.md, procedure 2, step 21: the PHY is complete */
static void phy_attach_finish(struct bcm4360_phy *phy)
{
	u32 i;

	phy->refcnt++;
	for (i = 0; i < ARRAY_SIZE(phy->pi_0x1144); i++)
		phy->pi_0x1144[i] = PI_0x1144_ATTACH;
	hw_memcpy(&phy->ver_ro, &phy->ver, sizeof(phy->ver_ro));
}

/*
 * acphy-attach.md, procedure 2, failure exit. Other than the object this
 * releases the timer as well.
 */
static void phy_attach_fail(struct bcm4360_phy *phy)
{
	if (phy->timer_phycal)
		bcm4360_timer_free(phy->hw, phy->timer_phycal);
	hw_free(phy, sizeof(*phy));
}

/*
 * acphy-attach.md, procedures 1 and 2 (wlc_phy_shared_attach,
 * wlc_phy_attach): make the PHY of a card. The variables of the board are
 * looked up here and never again.
 */
struct bcm4360_phy *bcm4360_phy_attach(struct bcm4360_hw *hw,
				       const struct bcm4360_phy_board *board)
{
	struct bcm4360_phy *phy;
	u32 sflags;

	phy = hw_zalloc(sizeof(*phy));
	if (!phy)
		return NULL;
	phy_shared_attach(phy, hw, board);

	sflags = phy_attach_sflags(phy);	/* step 1 */
	phy_attach_first(phy);			/* step 4 */
	phy_read_tempdelta(phy);		/* step 5 (procedure 4) */
	phy_attach_identify(phy, sflags);	/* steps 6 to 9 */
	if (!phy_attach_supported(phy))		/* step 10 */
		goto fail;
	phy_attach_chanspec(phy);		/* step 11 */
	phy_attach_interference(phy);		/* steps 12, 13 */
	phy_attach_defaults(phy);		/* step 14 */

	phy->timer_phycal = bcm4360_timer_init(hw, phy_timer_phycal, phy, TIMER_PHYCAL_NAME);
	if (!phy->timer_phycal)			/* step 15 */
		goto fail;
	if (!acphy_attach(phy))			/* step 16 (procedure 6) */
		goto fail;
	phy_attach_temp_limits(phy);		/* step 17 */
	phy_set_uninitted(phy);			/* step 18 (procedure 5) */
	if (!phy_attach_radio(phy))		/* step 19 */
		goto fail;
	phy_attach_more_vars(phy);		/* step 20 */
	phy_attach_finish(phy);			/* step 21 */
	return phy;

fail:
	phy_attach_fail(phy);
	return NULL;
}

/*
 * acphy-attach.md, procedure 13 (wlc_phy_detach). There is one PHY and one
 * structure: nothing is taken out of a list, no state of the AC-PHY is freed
 * separately.
 */
void bcm4360_phy_detach(struct bcm4360_phy *phy)
{
	if (!phy)
		return;
	if (--phy->refcnt)
		return;

	if (phy->timer_phycal) {
		bcm4360_timer_free(phy->hw, phy->timer_phycal);
		phy->timer_phycal = NULL;
	}
	hw_free(phy, sizeof(*phy));
}

/* acphy-attach.md, procedure 12 (wlc_phy_machwcap_set) */
void bcm4360_phy_machwcap_set(struct bcm4360_phy *phy, u32 caps)
{
	phy->machwcap = caps;
}

/* acphy-attach.md, procedure 12 (wlc_phy_get_phyversion) */
void bcm4360_phy_get_phyversion(struct bcm4360_phy *phy, u16 *phytype, u16 *phyrev,
				u16 *radioid, u16 *radiorev)
{
	*phytype = phy->ver.phy_type;
	*phyrev = phy->ver.phy_rev;
	*radioid = phy->ver.radio_id;
	*radiorev = phy->ver.radio_rev;
}

/* acphy-attach.md, procedure 12 (wlc_phy_get_coreflags) */
u32 bcm4360_phy_get_coreflags(struct bcm4360_phy *phy)
{
	return phy->ver.coreflags;
}

/*
 * acphy-attach.md, procedure 12 (wlc_phy_cap_get, wlc_phy_ac_caps). Of the
 * revisions of this code revision 1 has the bit that depends on the revision
 * (the object: revisions 1, 3 and above 5).
 */
u32 bcm4360_phy_cap_get(struct bcm4360_phy *phy)
{
	return phy->ver.phy_rev == ACPHY_REV_1 ? PHY_CAPS_REV1 : PHY_CAPS_REV0;
}

/* acphy-attach.md, procedure 12 (wlc_phy_stf_chain_init) */
void bcm4360_phy_stf_chain_init(struct bcm4360_phy *phy, u8 txchain, u8 rxchain)
{
	phy->hw_txchain = txchain;
	phy->txchain = txchain;
	phy->hw_rxchain = rxchain;
	phy->rxchain = rxchain;
}

/* acphy-radio.md, section 1 (wlc_phy_anacore): the analog core of the PHY */
void bcm4360_phy_anacore(struct bcm4360_phy *phy, bool on)
{
	if (phy->ver.phy_rev > ACPHY_REV_ANACORE_MAX)
		return;
	d11_write16(phy->hw, D11_PHY_ANACORE, on ? D11_PHY_ANACORE_ON : D11_PHY_ANACORE_OFF);
}

/* acphy-radio.md, section 4 (wlc_phy_switch_radio_acphy), "Off" */
static void acphy_switch_radio_off(struct bcm4360_phy *phy)
{
	phy->radio_on = false;
	bcm4360_radio_off(&phy->radio);
}

/*
 * acphy-radio.md, section 4 (wlc_phy_switch_radio_acphy), "On". When the PHY
 * is initialised the registers and tables of the PHY are set up again and the
 * radio is tuned to its channel.
 */
static void acphy_switch_radio_on(struct bcm4360_phy *phy)
{
	if (phy->radio_on)
		return;

	bcm4360_mac_suspend(phy->hw);
	bcm4360_radio_pwron_seq(&phy->radio);
	bcm4360_radio_rcal(&phy->radio);
	bcm4360_radio_rccal(&phy->radio);
	if (phy->init_done) {
		bcm4360_phy_set_regtbl_on_pwron_acphy(phy);
		bcm4360_phy_chanspec_set_acphy(phy, phy->radio_chanspec);
	}
	phy->radio_on = true;
	bcm4360_mac_enable(phy->hw);
}

/* acphy-radio.md, section 3 (wlc_phy_switch_radio) */
void bcm4360_phy_switch_radio(struct bcm4360_phy *phy, bool on)
{
	(void)d11_read32(phy->hw, D11_REG_0x120);
	bcm4360_mac_update_bt_chanspec(phy->hw, on ? phy->radio_chanspec : 0,
				       phy->hold & BCM4360_PHY_HOLD_SCAN,
				       phy->hold & BCM4360_PHY_HOLD_BIT2);
	if (on)
		acphy_switch_radio_on(phy);
	else
		acphy_switch_radio_off(phy);
}

/* acphy-chanspec.md, section 4 (wlc_phy_chanspec_radio_set) */
void bcm4360_phy_chanspec_radio_set(struct bcm4360_phy *phy, u16 chanspec)
{
	phy->radio_chanspec = chanspec;
}

/* acphy-chanspec.md, section 4 (wlc_phy_chanspec_get) */
u16 bcm4360_phy_chanspec_get(struct bcm4360_phy *phy)
{
	return phy->radio_chanspec;
}

/* acphy-chanspec.md, section 3 (wlc_phy_bw_state_set) */
void bcm4360_phy_bw_state_set(struct bcm4360_phy *phy, u16 bw)
{
	phy->bw = bw;
}

/* acphy-chanspec.md, section 3 (wlc_phy_bw_state_get) */
u16 bcm4360_phy_bw_state_get(struct bcm4360_phy *phy)
{
	return phy->bw;
}

/*
 * acphy-chanspec.md, section 3 (wlc_phy_clk_bwbits): the bandwidth of the PHY
 * clock as bits of the control flags of the core
 */
u32 bcm4360_phy_clk_bwbits(struct bcm4360_phy *phy)
{
	switch (phy->bw) {
	case BCM4360_CHANSPEC_BW_20:
		return D11_CFLAGS_BW_20;
	case BCM4360_CHANSPEC_BW_40:
		return D11_CFLAGS_BW_40;
	case BCM4360_CHANSPEC_BW_80:
		return D11_CFLAGS_BW_80;
	default:
		return 0;
	}
}

/*
 * docs/re/tasks/phy-attach.md (wlc_phy_hw_clk_state_upd): the clock of the
 * core is on, the registers of the PHY may be accessed
 */
void bcm4360_phy_hw_clk_state_upd(struct bcm4360_phy *phy, bool on)
{
	phy->clk = on;
}

/* docs/re/tasks/phy-attach.md (wlc_phy_hw_state_upd): the driver is up */
void bcm4360_phy_hw_state_upd(struct bcm4360_phy *phy, bool up)
{
	phy->up = up;
}

/*
 * docs/re/tasks/phy-attach.md (wlc_phy_por_inform): the chip went through a
 * power-on reset, the next initialisation loads the tables
 */
void bcm4360_phy_por_inform(struct bcm4360_phy *phy)
{
	phy->init_por = true;
}

/*
 * docs/re/tasks/phy-attach.md, bcm4360_phy_get_var: match "<base><core>"
 * where the core is a single digit below `maxcore`.
 */
static bool match_core(const char *name, const char *base, u32 maxcore, u32 *core)
{
	u32 i = 0;
	u32 c;

	while (base[i]) {
		if (name[i] != base[i])
			return false;
		i++;
	}
	if (name[i] < '0' || name[i] > '9' || name[i + 1])
		return false;
	c = (u32)(name[i] - '0');
	if (c >= maxcore)
		return false;
	*core = c;
	return true;
}

/* one of the three bytes of a receive gain entry (0 LNA, 1 T/R, 2 bypass) */
static u8 rxgain_field(const struct bcm4360_phy_rxgains *e, u32 field)
{
	if (field == 0)
		return e->elna_gain;
	if (field == 1)
		return e->triso;
	return e->trelnabyp;
}

/*
 * docs/re/tasks/phy-attach.md (bcm4360_phy_get_var): read back the value the
 * PHY keeps for a board variable, after the conversion of the specification.
 * The test compares as many bits as the object keeps; the rest is ignored.
 */
bool bcm4360_phy_get_var(const struct bcm4360_phy *phy, const char *name, u32 index,
			 s32 *value)
{
	static const struct {
		const char *base;
		u8 band;
		u8 field;
	} rxgain_vars[] = {
		{ "rxgains2gelnagaina", BCM4360_PHY_RXGAIN_2G, 0 },
		{ "rxgains2gtrisoa", BCM4360_PHY_RXGAIN_2G, 1 },
		{ "rxgains2gtrelnabypa", BCM4360_PHY_RXGAIN_2G, 2 },
		{ "rxgains5gelnagaina", BCM4360_PHY_RXGAIN_5G_LOW, 0 },
		{ "rxgains5gtrisoa", BCM4360_PHY_RXGAIN_5G_LOW, 1 },
		{ "rxgains5gtrelnabypa", BCM4360_PHY_RXGAIN_5G_LOW, 2 },
		{ "rxgains5gmelnagaina", BCM4360_PHY_RXGAIN_5G_MID, 0 },
		{ "rxgains5gmtrisoa", BCM4360_PHY_RXGAIN_5G_MID, 1 },
		{ "rxgains5gmtrelnabypa", BCM4360_PHY_RXGAIN_5G_MID, 2 },
		{ "rxgains5ghelnagaina", BCM4360_PHY_RXGAIN_5G_HIGH, 0 },
		{ "rxgains5ghtrisoa", BCM4360_PHY_RXGAIN_5G_HIGH, 1 },
		{ "rxgains5ghtrelnabypa", BCM4360_PHY_RXGAIN_5G_HIGH, 2 },
	};
	static const struct {
		const char *name;
		u8 group;
		u8 range;
	} po5g_vars[] = {
		{ "mcsbw205glpo", 0, 0 }, { "mcsbw205gmpo", 0, 1 }, { "mcsbw205ghpo", 0, 2 },
		{ "mcsbw405glpo", 3, 0 }, { "mcsbw405gmpo", 3, 1 }, { "mcsbw405ghpo", 3, 2 },
		{ "mcsbw805glpo", 5, 0 }, { "mcsbw805gmpo", 5, 1 }, { "mcsbw805ghpo", 5, 2 },
	};
	const struct bcm4360_phy_txpwr *tx = &phy->txpwr;
	const struct bcm4360_phy_rssi *rssi = &phy->rssi;
	u32 c, i;

#define RET(v)	do { *value = (s32)(v); return true; } while (0)
#define EQ(s)	(hw_strcmp(name, (s)) == 0)

	/* names with a dot: not board variables */
	if (EQ(".phy_type"))
		RET(phy->ver.phy_type);
	if (EQ(".phy_rev"))
		RET(phy->ver.phy_rev);
	if (EQ(".cores"))
		RET(phy->ver.cores);
	if (EQ(".radio_id"))
		RET(phy->ver.radio_id);
	if (EQ(".radio_rev"))
		RET(phy->ver.radio_rev);
	if (EQ(".chanspec"))
		RET(phy->radio_chanspec);
	if (EQ(".write_limit"))
		RET(phy->io.write_limit);
	if (EQ(".xtal_hz"))
		RET(phy->xtal_hz);
	if (EQ(".interference_2g"))
		RET(phy->interference.mode_2g);
	if (EQ(".interference_5g"))
		RET(phy->interference.mode_5g);
	if (EQ(".rxgainerr2g_empty"))
		RET(phy->rxgainerr[BCM4360_PHY_BAND_2G].empty);
	if (EQ(".rxgainerr5g_empty"))
		RET(phy->rxgainerr[BCM4360_PHY_BAND_5G + index].empty);
	if (EQ(".otp_word16_bits8_12"))
		RET(phy->otp_word16_bits8_12);

	/* variables of three cores (0, 1, 2) */
	if (match_core(name, "maxp2ga", BCM4360_PHY_CORES_MAX, &c))
		RET(tx->maxp[c][BCM4360_PHY_BAND_2G]);
	if (match_core(name, "maxp5ga", BCM4360_PHY_CORES_MAX, &c))
		RET(tx->maxp[c][BCM4360_PHY_BAND_5G + index]);
	if (match_core(name, "pa2ga", BCM4360_PHY_CORES_MAX, &c))
		RET(tx->pa[c][BCM4360_PHY_BAND_2G][index]);
	if (match_core(name, "pa5ga", BCM4360_PHY_CORES_MAX, &c))
		RET(tx->pa[c][BCM4360_PHY_BAND_5G + index / BCM4360_PHY_PA_PARAMS]
		       [index % BCM4360_PHY_PA_PARAMS]);
	if (match_core(name, "pdoffset40ma", BCM4360_PHY_CORES_MAX, &c))
		RET(tx->pdoffset40ma[c]);
	if (match_core(name, "pdoffset80ma", BCM4360_PHY_CORES_MAX, &c))
		RET(tx->pdoffset80ma[c]);
	if (match_core(name, "pdoffset2g40ma", BCM4360_PHY_CORES_MAX, &c))
		RET(tx->pdoffset2g40ma[c]);
	if (match_core(name, "pdoffsetcckma", BCM4360_PHY_CORES_MAX, &c))
		RET(tx->pdoffsetcckma[c]);
	if (match_core(name, "rxgainerr2ga", BCM4360_PHY_CORES_MAX, &c))
		RET(phy->rxgainerr[BCM4360_PHY_BAND_2G].err[c]);
	if (match_core(name, "rxgainerr5ga", BCM4360_PHY_CORES_MAX, &c))
		RET(phy->rxgainerr[BCM4360_PHY_BAND_5G + index].err[c]);
	if (match_core(name, "rssicorrnorm5g_c", BCM4360_PHY_CORES_MAX, &c))
		RET(rssi->corrnorm5g_c[c][index]);
	if (match_core(name, "rssicorrnorm_c", BCM4360_PHY_CORES_MAX, &c))
		RET(rssi->corrnorm_c[c][index]);
	if (match_core(name, "rpcal5gb", BCM4360_PHY_SUBBANDS_5G, &c))
		RET(phy->rpcal5gb[c]);

	/* variables of the cores the PHY has */
	for (i = 0; i < ARRAY_SIZE(rxgain_vars); i++) {
		if (match_core(name, rxgain_vars[i].base, phy->ver.cores, &c))
			RET(rxgain_field(&phy->rxgains[rxgain_vars[i].band][c],
					 rxgain_vars[i].field));
	}
	if (match_core(name, "noiselvl2ga", phy->ver.cores, &c))
		RET(phy->noiselvl[BCM4360_PHY_BAND_2G][c]);
	if (match_core(name, "noiselvl5ga", phy->ver.cores, &c))
		RET(phy->noiselvl[BCM4360_PHY_BAND_5G + index][c]);

	/* scalars and simple arrays */
	if (EQ("phycal_tempdelta"))
		RET(phy->temp.cal_delta);
	if (EQ("interference"))
		RET(phy->interference.mode_2g);
	if (EQ("subband5gver"))
		RET(phy->subband5gver);
	if (EQ("extpagain2g"))
		RET(tx->extpagain2g);
	if (EQ("extpagain5g"))
		RET(tx->extpagain5g);
	if (EQ("femctrl"))
		RET(phy->femctrl);
	if (EQ("rpcal2g"))
		RET(phy->rpcal2g);
	if (EQ("txidxcap2g"))
		RET(tx->txidxcap2g);
	if (EQ("txidxcap5g"))
		RET(tx->txidxcap5g);
	if (EQ("pdgain2g"))
		RET(tx->pdgain2g);
	if (EQ("pdgain5g"))
		RET(tx->pdgain5g);
	if (EQ("cckdigfilttype"))
		RET(phy->cckdigfilttype);
	if (EQ("pdoffset2g40mvalid"))
		RET(tx->pdoffset2g40mvalid);
	if (EQ("tssifloor2g"))
		RET(tx->tssifloor[BCM4360_PHY_BAND_2G]);
	if (EQ("tssifloor5g"))
		RET(tx->tssifloor[BCM4360_PHY_BAND_5G + index]);
	if (EQ("tempoffset"))
		RET(phy->temp.offset);
	if (EQ("rawtempsense"))
		RET(phy->temp.rawtempsense);
	if (EQ("tempthresh"))
		RET(phy->temp.thresh);
	if (EQ("temps_hysteresis"))
		RET(phy->temp.hysteresis);
	if (EQ("txpwrbckof"))
		RET(tx->txpwrbckof);
	if (EQ("tssilimucod"))
		RET(tx->tssilimucod);
	if (EQ("rssicorrnorm"))
		RET(rssi->corrnorm);
	if (EQ("rssicorratten"))
		RET(rssi->corratten);
	if (EQ("rssicorrnorm5g"))
		RET(rssi->corrnorm5g[index]);
	if (EQ("rssicorratten5g"))
		RET(rssi->corratten5g[index]);
	if (EQ("rssicorrperrg2g"))
		RET(rssi->corrperrg2g[index]);
	if (EQ("rssicorrperrg5g"))
		RET(rssi->corrperrg5g[index]);
	if (EQ("5g_cga"))
		RET(rssi->cga_5g[index]);
	if (EQ("2g_cga"))
		RET(rssi->cga_2g[index]);
	if (EQ("swctrlmap_2g"))
		RET(phy->swctrlmap_2g[index]);
	if (EQ("swctrlmapext_2g"))
		RET(phy->swctrlmapext_2g[index]);
	if (EQ("swctrlmap_5g"))
		RET(phy->swctrlmap_5g[index]);
	if (EQ("swctrlmapext_5g"))
		RET(phy->swctrlmapext_5g[index]);

	/* tx power offsets (procedure 7, step 2) */
	if (EQ("cckbw202gpo"))
		RET(tx->po.cckbw202gpo);
	if (EQ("cckbw20ul2gpo"))
		RET(tx->po.cckbw20ul2gpo);
	if (EQ("mcsbw202gpo"))
		RET(tx->po.mcs_2g[0]);
	if (EQ("mcsbw402gpo"))
		RET(tx->po.mcs_2g[2]);
	if (EQ("ofdmlrbw202gpo"))
		RET(tx->po.ofdmlrbw202gpo);
	if (EQ("sb20in40lrpo"))
		RET(tx->po.sb20in40lrpo);
	if (EQ("sb20in40hrpo"))
		RET(tx->po.sb20in40hrpo);
	if (EQ("dot11agduplrpo"))
		RET(tx->po.dot11agduplrpo);
	if (EQ("dot11agduphrpo"))
		RET(tx->po.dot11agduphrpo);
	if (EQ("mcslr5glpo"))
		RET(tx->po.mcslr5gpo[0]);
	if (EQ("mcslr5gmpo"))
		RET(tx->po.mcslr5gpo[1]);
	if (EQ("mcslr5ghpo"))
		RET(tx->po.mcslr5gpo[2]);
	if (EQ("sb20in80and160lr5glpo"))
		RET(tx->po.sb20in80and160lr5gpo[0]);
	if (EQ("sb20in80and160lr5gmpo"))
		RET(tx->po.sb20in80and160lr5gpo[1]);
	if (EQ("sb20in80and160lr5ghpo"))
		RET(tx->po.sb20in80and160lr5gpo[2]);
	if (EQ("sb20in80and160hr5glpo"))
		RET(tx->po.sb20in80and160hr5gpo[0]);
	if (EQ("sb20in80and160hr5gmpo"))
		RET(tx->po.sb20in80and160hr5gpo[1]);
	if (EQ("sb20in80and160hr5ghpo"))
		RET(tx->po.sb20in80and160hr5gpo[2]);
	if (EQ("sb40and80lr5glpo"))
		RET(tx->po.sb40and80lr5gpo[0]);
	if (EQ("sb40and80lr5gmpo"))
		RET(tx->po.sb40and80lr5gpo[1]);
	if (EQ("sb40and80lr5ghpo"))
		RET(tx->po.sb40and80lr5gpo[2]);
	if (EQ("sb40and80hr5glpo"))
		RET(tx->po.sb40and80hr5gpo[0]);
	if (EQ("sb40and80hr5gmpo"))
		RET(tx->po.sb40and80hr5gpo[1]);
	if (EQ("sb40and80hr5ghpo"))
		RET(tx->po.sb40and80hr5gpo[2]);
	for (i = 0; i < ARRAY_SIZE(po5g_vars); i++) {
		if (EQ(po5g_vars[i].name))
			RET(tx->po.mcs_5g[po5g_vars[i].group][po5g_vars[i].range]);
	}

	return false;
#undef RET
#undef EQ
}
