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

/*
 * acphy-attach.md, procedure 2, step 15 (wlapi_init_timer): what the timer
 * "phycal" calls
 */
static void phy_timer_phycal(void *arg)
{
	bcm4360_phy_timer_phycal(arg);
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

/*
 * acphy-attach.md, procedure 6 (wlc_phy_attach_acphy): the state of the
 * AC-PHY, the description of the board. Not done: the two global variables of
 * the steps 1 and 5 (the chip id is in phy->board), the table of function
 * pointers of step 29 (there is one kind of PHY).
 */
static bool acphy_attach(struct bcm4360_phy *phy)
{
	acphy_attach_save_regs(phy);
	acphy_attach_cores(phy);
	acphy_attach_otp_word16(phy);
	phy->interference.mode = 0;
	acphy_attach_otp_rcal(phy);
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

	sflags = phy_attach_sflags(phy);
	phy_attach_first(phy);
	phy_attach_identify(phy, sflags);
	if (!phy_attach_supported(phy))
		goto fail;
	phy_attach_chanspec(phy);
	phy_attach_defaults(phy);

	phy->timer_phycal = bcm4360_timer_init(hw, phy_timer_phycal, phy, TIMER_PHYCAL_NAME);
	if (!phy->timer_phycal)
		goto fail;
	if (!acphy_attach(phy))
		goto fail;
	phy_set_uninitted(phy);
	if (!phy_attach_radio(phy))
		goto fail;
	phy_attach_finish(phy);
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
