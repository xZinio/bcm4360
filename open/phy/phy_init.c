// SPDX-License-Identifier: ISC
/*
 * AC-PHY of the BCM4360: initialisation of the PHY (wlc_phy_init and what it
 * drives) and the small helpers of the initialisation that the channel
 * function and later PHY code reuse.
 *
 * Written from docs/re/spec/acphy-init.md (a "section" or "Hn" in the comments
 * is a section of its "Procedures") and, for the register access, from
 * docs/re/spec/access.md; for chip 0x4360, AC-PHY revision 0 or 1 and radio
 * 2069 revision 3 or 4. The branches of other chips, PHY types and revisions
 * are left out. The radio is switched on and tuned by the finished functions
 * of open/phy/radio2069.c and open/phy/phy_attach.c; the big leaf functions
 * (front end, receive gain/desense, transmit power) are stubs in phy_todo.c.
 */
#include <bcm4360/phy.h>
#include <bcm4360/phy_env.h>

/*
 * PHY registers accessed here, named by their address (their purpose is
 * unknown unless a section of the specification says more). The registers
 * ACPHY_REG_0x408, 0x720, 0x721, 0x725, 0x728, 0x729, 0x739, 0x73a, 0x73e,
 * 0x16b and 0x175 are already in radio2069.h; ACPHY_REG_0x00b is in phy.h.
 */
#define ACPHY_REG_0x000		0x000	/* bits 0..3 read for the regulator field (section 3) */
#define ACPHY_REG_0x001		0x001	/* bit 14: CCA reset (H5) */
#define ACPHY_REG_0x025		0x025
#define ACPHY_REG_0x026		0x026
#define ACPHY_REG_0x042		0x042	/* bit 15: scrambler dynamic bandwidth (H13) */
#define ACPHY_REG_0x072		0x072
#define ACPHY_REG_0x140		0x140	/* bits 0..2: classifier enables (H1) */
#define ACPHY_REG_0x160		0x160	/* bits 0..2: mask of receive cores (H7) */
#define ACPHY_REG_0x16e		0x16e
#define ACPHY_REG_0x16f		0x16f
#define ACPHY_REG_0x170		0x170
#define ACPHY_REG_0x19e		0x19e	/* bits set around table accesses / sequencer triggers */
#define ACPHY_REG_0x1b0		0x1b0	/* bit 6 receive LDPC (H12), bit 15 (section 3) */
#define ACPHY_REG_0x1b1		0x1b1
#define ACPHY_REG_0x1b6		0x1b6
#define ACPHY_REG_0x1ca		0x1ca
#define ACPHY_REG_0x1e6		0x1e6
#define ACPHY_REG_0x1ed		0x1ed
#define ACPHY_REG_0x1f2		0x1f2
#define ACPHY_REG_0x2eb		0x2eb
#define ACPHY_REG_0x2ed		0x2ed	/* bit 4: OFDM carrier sense (H2); bit 5 (section 5) */
#define ACPHY_REG_0x2ef		0x2ef
#define ACPHY_REG_0x2f1		0x2f1
#define ACPHY_REG_0x2f3		0x2f3
#define ACPHY_REG_0x2f5		0x2f5
#define ACPHY_REG_0x2f7		0x2f7
#define ACPHY_REG_0x2f9		0x2f9
#define ACPHY_REG_0x339		0x339	/* 0 inside, 0x0fff outside the carrier search (H4) */
#define ACPHY_REG_0x358		0x358
#define ACPHY_REG_0x3c4		0x3c4
#define ACPHY_REG_0x400		0x400	/* RF sequencer mode (H6, H7) */
#define ACPHY_REG_0x401		0x401	/* RF sequencer core activation (H7) */
#define ACPHY_REG_0x402		0x402	/* RF sequencer trigger (H6) */
#define ACPHY_REG_0x403		0x403	/* RF sequencer status (H6) */
#define ACPHY_REG_0x40f		0x40f
#define ACPHY_REG_0x410		0x410
#define ACPHY_REG_0x645		0x645	/* bits 0..9: idle TSSI of a core (section 4, 6) */
#define ACPHY_REG_0x690		0x690	/* bits 9, 10 (section 5) */
#define ACPHY_REG_0x6d4		0x6d4	/* clip detection, PHY revision != 0 (H3) */
#define ACPHY_REG_0x6da		0x6da	/* clip detection, PHY revision 0 (H3) */

/* radio control override registers written by section 4, plain core-0 base */
#define ACPHY_REG_0x722		0x722
#define ACPHY_REG_0x723		0x723
#define ACPHY_REG_0x724		0x724
#define ACPHY_REG_0x726		0x726
#define ACPHY_REG_0x727		0x727
#define ACPHY_REG_0x750		0x750

/* shared memory offsets in bytes (access.md, "Object memories of the MAC") */
#define SHM_CHANSPEC		0xa0	/* section 2 */
#define SHM_UCODE_BLOCK		0x92	/* section 1, step 19 */
#define SHM_HIRSSI_A		0x32	/* section 3a */
#define SHM_HIRSSI_B		0x180
#define SHM_HIRSSI_C		0x182
#define SHM_HIRSSI_FLAG		0x184	/* set to 0xdead by the microcode (H11) */

#define SHM_HIRSSI_FLAG_DEAD	0xdead

/* section 3, step 11: the two threshold values of PHY revision 0 and 1 */
#define ACPHY_THRESH_A		0x043f
#define ACPHY_THRESH_B		0x03c0

/* section 3, step 11: the registers that get value A and value B, in order */
static const u16 thresh_regs_a[] = {
	0x33a, 0x33b, 0x33e, 0x33f, 0x342, 0x343, 0x346, 0x347,
};
static const u16 thresh_regs_b[] = {
	0x33c, 0x33d, 0x340, 0x341, 0x344, 0x345, 0x348, 0x349,
};

/* section 4, step 2: the radio-control registers cleared to 0, in order (all cores) */
static const u16 pwron_clear_regs[] = {
	ACPHY_REG_0x73e, ACPHY_REG_0x725, ACPHY_REG_0x722, ACPHY_REG_0x723,
	ACPHY_REG_0x724, ACPHY_REG_0x725, ACPHY_REG_0x726, ACPHY_REG_0x727,
	ACPHY_REG_0x750,
};

/* section 4, "Data": the static table set of PHY revision 0 and 1 (acphytbl_info_rev0) */
struct static_table {
	u32 id;
	u32 entries;
	u32 width;
	const char *name;
};

static const struct static_table static_tables_rev0[] = {
	{ 0x01, 128, 16, "acphy_mcs_tbl_rev0" },
	{ 0x02,  38,  8, "acphy_tx_evm_tbl_rev0" },
	{ 0x04, 256,  8, "acphy_rx_evm_shaping_tbl_rev0" },
	{ 0x03, 256, 32, "acphy_noise_shaping_tbl_rev0" },
	{ 0x05,  22, 32, "acphy_phasetrack_tbl_rev0" },
	{ 0x40, 128, 16, "acphy_est_pwr_lut_core0_rev0" },
	{ 0x60, 128, 16, "acphy_est_pwr_lut_core1_rev0" },
	{ 0x80, 128, 16, "acphy_est_pwr_lut_core2_rev0" },
	{ 0x41, 128, 32, "acphy_iq_lut_core0_rev0" },
	{ 0x61, 128, 32, "acphy_iq_lut_core0_rev0" },	/* core 1 uses core 0's data (how the object is) */
	{ 0x81, 128, 32, "acphy_iq_lut_core2_rev0" },
	{ 0x42, 128, 16, "acphy_loft_lut_core0_rev0" },
	{ 0x62, 128, 16, "acphy_loft_lut_core1_rev0" },
	{ 0x82, 128, 16, "acphy_loft_lut_core2_rev0" },
	{ 0x40, 128, 16, "acphy_papd_comp_rfpwr_tbl_core0_rev0" },	/* overwrite 0x40/0x60/0x80 */
	{ 0x60, 128, 16, "acphy_papd_comp_rfpwr_tbl_core1_rev0" },
	{ 0x80, 128, 16, "acphy_papd_comp_rfpwr_tbl_core2_rev0" },
	{ 0x47,  64, 32, "acphy_papd_comp_epsilon_tbl_core0_rev0" },
	{ 0x67,  64, 32, "acphy_papd_comp_epsilon_tbl_core1_rev0" },
	{ 0x87,  64, 32, "acphy_papd_comp_epsilon_tbl_core2_rev0" },
	{ 0x48,  64, 32, "acphy_papd_cal_scalars_tbl_core0_rev0" },
	{ 0x68,  64, 32, "acphy_papd_cal_scalars_tbl_core1_rev0" },
	{ 0x88,  64, 32, "acphy_papd_cal_scalars_tbl_core2_rev0" },
};

/* the number of cores N of the PHY (pi+0x168) */
static u32 phy_cores(const struct bcm4360_phy *phy)
{
	return phy->ver.cores;
}

/* the current chanspec is a 2.4 GHz channel (acphy-init.md, "Procedures") */
static bool phy_is_2g(const struct bcm4360_phy *phy)
{
	return (phy->radio_chanspec & BCM4360_CHANSPEC_BAND) == BCM4360_CHANSPEC_BAND_2G;
}

/* the lowest `bits` bits of `v` taken as a signed 16 bit value */
static u16 phy_sext16(u32 v, u32 bits)
{
	u32 mask = (1u << bits) - 1;

	v &= mask;
	if (v & (1u << (bits - 1)))
		v |= ~mask;
	return (u16)v;
}

/* ----------------------------------------------------------------- H1..H13 */

/*
 * acphy-init.md, H1 (wlc_phy_classifier_acphy): set bits of the classifier
 * enable register with a full register write (it counts for the write pacing).
 * Returns the new value.
 */
static u16 phy_classifier(struct bcm4360_phy *phy, u16 mask, u16 value)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 n = (bcm4360_phy_read(io, ACPHY_REG_0x140) & ~mask) | (value & mask);

	bcm4360_phy_write(io, ACPHY_REG_0x140, n);
	return n;
}

/* acphy-init.md, H2 (wlc_phy_ofdm_crs_acphy): OFDM carrier sense of every core */
static void phy_ofdm_crs(struct bcm4360_phy *phy, bool enable)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 e = enable ? 0x0010 : 0;

	bcm4360_phy_mod(io, ACPHY_REG_0x2ed, 0x0010, e);
	bcm4360_phy_mod(io, ACPHY_REG_0x2f1, 0x0010, e);
	bcm4360_phy_mod(io, ACPHY_REG_0x2f5, 0x0010, e);
	bcm4360_phy_mod(io, ACPHY_REG_0x2f9, 0x0010, e);
}

/* acphy-init.md, H3 (sub_0979bc, wlc_phy_clip_det_acphy): clip detection of every core */
static void phy_clip_det(struct bcm4360_phy *phy, bool enable)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c, o;

	for (c = 0; c < phy_cores(phy); c++) {
		o = c * ACPHY_CORE_STEP;
		if (phy->ver.phy_rev == ACPHY_REV_0) {
			bcm4360_phy_write(io, ACPHY_REG_0x6da + o,
					  enable ? phy->acphy_0x902 : 0xffff);
		} else if (enable) {
			bcm4360_phy_and(io, ACPHY_REG_0x6d4 + o, 0xbfff);
		} else {
			bcm4360_phy_or(io, ACPHY_REG_0x6d4 + o, 0x4000);
		}
	}
}

/*
 * acphy-init.md, H5 (wlc_phy_resetcca_acphy): reset of the clear channel
 * assessment. PHY revision 0 and 1.
 */
void bcm4360_phy_resetcca(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 v;

	bcm4360_mac_phyclk_fgc(phy->hw, true);
	v = bcm4360_phy_read(io, ACPHY_REG_0x001);
	bcm4360_phy_write(io, ACPHY_REG_0x001, v | 0x4000);
	hw_udelay(1);
	bcm4360_phy_write(io, ACPHY_REG_0x001, v & 0xbfff);
	bcm4360_mac_phyclk_fgc(phy->hw, false);
	hw_udelay(2);
}

/*
 * acphy-init.md, H6 (wlc_phy_force_rfseq_acphy): trigger a sequence of the RF
 * sequencer and wait until it has run. An unknown command does nothing.
 */
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

	/* SPINWAIT(status still set, 200000 us): at most 20001 reads, 10 us apart */
	v = bcm4360_phy_read(io, ACPHY_REG_0x403);
	for (i = 0; (v & bit) && i < 20000; i++) {
		hw_udelay(10);
		v = bcm4360_phy_read(io, ACPHY_REG_0x403);
	}

	bcm4360_phy_write(io, ACPHY_REG_0x400, s400);
	bcm4360_phy_write(io, ACPHY_REG_0x19e, s19e);
}

/*
 * acphy-init.md, H4 (wlc_phy_stay_in_carriersearch_acphy): make the receiver
 * "deaf" (or let it hear again) through a nesting counter.
 */
void bcm4360_phy_stay_in_carriersearch(struct bcm4360_phy *phy, bool enable)
{
	if (enable) {
		if (phy->csearch_count == 0) {
			phy_classifier(phy, 0x0007, 0x0004);
			phy_ofdm_crs(phy, false);
			phy_clip_det(phy, false);
			bcm4360_phy_write(&phy->io, ACPHY_REG_0x339, 0);
		}
		phy->csearch_count++;
		bcm4360_phy_resetcca(phy);
		return;
	}

	phy->csearch_count--;
	if (phy->csearch_count == 0) {
		phy_classifier(phy, 0x0007, phy_is_2g(phy) ? 0x0007 : 0x0006);
		phy_ofdm_crs(phy, true);
		phy_clip_det(phy, true);
		bcm4360_phy_write(&phy->io, ACPHY_REG_0x339, phy->acphy_0x904);
	}
}

/*
 * acphy-init.md, H7 (wlc_phy_rxcore_setstate_acphy): set the mask of the
 * receive cores in use and drive it into the hardware.
 */
void bcm4360_phy_rxcore_setstate(struct bcm4360_phy *phy, u8 rxmask)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 s401, s400;

	phy->rxchain = rxmask;
	if (!phy->clk)
		return;

	bcm4360_mac_suspend(phy->hw);
	s401 = bcm4360_phy_read(io, ACPHY_REG_0x401);
	s400 = bcm4360_phy_read(io, ACPHY_REG_0x400);
	bcm4360_phy_mod(io, ACPHY_REG_0x160, 0x0007, rxmask);
	bcm4360_phy_mod(io, ACPHY_REG_0x401, 0x0070, rxmask << 4);
	bcm4360_phy_mod(io, ACPHY_REG_0x401, 0x7000, 0x7000);
	bcm4360_phy_mod(io, ACPHY_REG_0x401, 0x0007, 0);
	bcm4360_phy_mod(io, ACPHY_REG_0x400, 0x0001, 0x0001);
	phy_force_rfseq(phy, 0);
	phy_force_rfseq(phy, 1);
	bcm4360_phy_mod(io, ACPHY_REG_0x401, 0x0007, phy->txchain);
	bcm4360_phy_mod(io, ACPHY_REG_0x401, 0x7000, s401 & 0x7000);
	bcm4360_phy_write(io, ACPHY_REG_0x400, s400);
	bcm4360_mac_enable(phy->hw);
}

/*
 * acphy-init.md, H9 (wlc_phy_deaf_acphy): enter or leave the carrier search
 * once, with the MAC suspended. It never nests.
 */
void bcm4360_phy_deaf(struct bcm4360_phy *phy, bool mode)
{
	bcm4360_mac_suspend(phy->hw);
	if (mode && phy->csearch_count == 0)
		bcm4360_phy_stay_in_carriersearch(phy, true);
	else if (!mode && phy->csearch_count != 0)
		bcm4360_phy_stay_in_carriersearch(phy, false);
	bcm4360_mac_enable(phy->hw);
}

/*
 * acphy-init.md, H11 (sub_092500,
 * wlc_phy_hirssi_elnabypass_shmem_read_clear_acphy): read the flag the
 * microcode sets and clear it. Returns whether it was set.
 */
bool bcm4360_phy_hirssi_shmem_read_clear(struct bcm4360_phy *phy)
{
	if (!phy->hirssi.supported)
		return false;
	if (bcm4360_shm_read(phy->hw, SHM_HIRSSI_FLAG) != SHM_HIRSSI_FLAG_DEAD)
		return false;
	bcm4360_shm_write(phy->hw, SHM_HIRSSI_FLAG, 0);
	return true;
}

/*
 * acphy-init.md, H12 (wlc_phy_update_rxldpc_acphy): write the receive LDPC
 * setting, only when it changes.
 */
static void phy_update_rxldpc(struct bcm4360_phy *phy, bool ldpc)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 v;

	if (ldpc == phy->rxldpc)
		return;
	phy->rxldpc = ldpc;
	v = bcm4360_phy_read(io, ACPHY_REG_0x1b0);
	bcm4360_phy_write(io, ACPHY_REG_0x1b0, ldpc ? (v | 0x0040) : (v & 0xffbf));
}

/* acphy-init.md, H12 (wlc_phy_ldpc_override_set) */
void bcm4360_phy_ldpc_override_set(struct bcm4360_phy *phy, bool ldpc)
{
	phy_update_rxldpc(phy, ldpc);
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

/* acphy-init.md, H13 (wlc_acphy_set_scramb_dyn_bw_en) */
void bcm4360_phy_set_scramb_dyn_bw_en(struct bcm4360_phy *phy, bool enable)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 v;

	phy_reg_enter(phy);
	v = bcm4360_phy_read(io, ACPHY_REG_0x042);
	bcm4360_phy_write(io, ACPHY_REG_0x042, enable ? (v | 0x8000) : (v & 0x7fff));
	phy_reg_exit(phy);
}

/* ----------------------------------------------------- section 4: sub_0a04c2 */

/*
 * acphy-init.md, section 4, "Data": load the static PHY table set of revision
 * 0 and 1, record by record, in order. Broadcom's data comes from the
 * platform (hw_fw_data of hw.h).
 */
static void phy_load_static_tables(struct bcm4360_phy *phy)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(static_tables_rev0); i++) {
		const struct static_table *t = &static_tables_rev0[i];
		u32 size = 0;
		const void *data = hw_fw_data(phy->hw, t->name, &size);

		if (data)
			bcm4360_tbl_write(&phy->io, t->id, t->entries, 0, t->width, data);
	}
}

/*
 * acphy-init.md, section 4 (sub_0a04c2, wlc_phy_set_regtbl_on_pwron_acphy):
 * release the radio-control overrides that "radio off" had set and load the
 * static PHY tables. PHY revision 0 and 1.
 */
void bcm4360_phy_set_regtbl_on_pwron_acphy(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u16 s19e, v;
	u32 i;

	/* step 1 */
	bcm4360_phy_write(io, ACPHY_REG_0x410, 0x0077);

	/* step 2: clear the radio-control registers of all cores (bit 12 set) */
	for (i = 0; i < ARRAY_SIZE(pwron_clear_regs); i++)
		bcm4360_phy_write(io, ACPHY_ALL_CORES | pwron_clear_regs[i], 0);

	/* steps 3, 4 */
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x728, 0x0080);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x720, 0x0180);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x729, 0);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x721, 0x5000);

	/* step 5: the two reads use the plain core-0 address */
	v = bcm4360_phy_read(io, ACPHY_REG_0x73a);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x73a, v | 0x0100);
	v = bcm4360_phy_read(io, ACPHY_REG_0x725);
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x725, v | 0x0400);

	/* step 6: set bit 1 of PHY(0x19e) around the table load */
	s19e = bcm4360_phy_read(io, ACPHY_REG_0x19e);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, 0x0002);

	/* step 7: load the static tables only after a power-on reset */
	if (phy->init_por)
		phy_load_static_tables(phy);

	/* step 8: restore bit 1 of PHY(0x19e) */
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0002, s19e & 0x0002);

	/* step 9: the idle-TSSI field of all cores */
	bcm4360_phy_write(io, ACPHY_ALL_CORES | ACPHY_REG_0x645, 0x025c);

	/* step 11 */
	if (phy->flags.bf3_bit7)
		bcm4360_mac_mhf(phy->hw, 1, 0x0080, 0x0080, 3);
}

/* ----------------------------------------------------- section 5: sub_0a0be4 */

/*
 * acphy-init.md, section 5 (sub_0a0be4, wlc_phy_set_reg_on_reset_acphy): PHY
 * register settings needed once after a reset. PHY revision 0 and 1, no radio
 * access. Called by the channel function while the initialisation runs.
 */
void bcm4360_phy_set_reg_on_reset_acphy(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 c, r;
	bool x;

	bcm4360_phy_or(io, ACPHY_REG_0x19e, 0x01c0);				/* 1 */
	if (phy_is_2g(phy))							/* 2 */
		bcm4360_phy_write(io, ACPHY_REG_0x3c4, 0x0668);
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x0200, 0x0200);			/* 3 */
	bcm4360_phy_mod(io, ACPHY_REG_0x19e, 0x003c, 0x0010);			/* 4 */
	bcm4360_phy_write(io, ACPHY_REG_0x1f2, 0x00c8);				/* 5 */
	bcm4360_phy_write(io, ACPHY_REG_0x026, 0x0092);				/* 6 */
	bcm4360_phy_write(io, ACPHY_REG_0x1ed, 0x0050);				/* 7 */
	bcm4360_phy_write(io, ACPHY_REG_0x025, 0x0030);
	bcm4360_chip_core_cflags(phy->hw, 0x10, 0x10);				/* 8 */
	bcm4360_phy_mod(io, ACPHY_REG_0x40f, 0x0200, 0);			/* 9 */
	bcm4360_phy_mod(io, ACPHY_REG_0x2f1, 0x0020, 0);			/* 10 */
	bcm4360_phy_mod(io, ACPHY_REG_0x2ed, 0x0020, 0);
	bcm4360_phy_mod(io, ACPHY_REG_0x2f9, 0x0020, 0);
	bcm4360_phy_mod(io, ACPHY_REG_0x2f5, 0x0020, 0);
	bcm4360_phy_mod(io, ACPHY_REG_0x2ef, 0x00ff, 0x0055);			/* 11 */
	bcm4360_phy_mod(io, ACPHY_REG_0x2eb, 0x00ff, 0x0055);
	bcm4360_phy_mod(io, ACPHY_REG_0x2f7, 0x00ff, 0x0055);
	bcm4360_phy_mod(io, ACPHY_REG_0x2f3, 0x00ff, 0x0055);
	bcm4360_phy_write(io, ACPHY_REG_0x400, 0);				/* 12 */
	bcm4360_phy_mod(io, ACPHY_REG_0x1ca, 0x1000, 0);			/* 13 */
	bcm4360_phy_resetcca(phy);						/* 14 */
	bcm4360_phy_mod(io, ACPHY_REG_0x072, 0x0004, 0x0004);			/* 15 */
	bcm4360_phy_mod(io, ACPHY_REG_0x1b0, 0x0020, 0);			/* 16 */
	bcm4360_phy_mod(io, ACPHY_REG_0x1b1, 0x1000, 0x1000);
	bcm4360_phy_mod(io, ACPHY_REG_0x1b6, 0x8000, 0);
	for (c = 0; c < phy_cores(phy); c++) {					/* 17 */
		r = ACPHY_REG_0x690 + c * ACPHY_CORE_STEP;
		bcm4360_phy_mod(io, r, 0x0200, 0x0200);
		bcm4360_phy_mod(io, r, 0x0400, 0x0400);
	}
	bcm4360_phy_write(io, ACPHY_REG_0x1e6, 0x0030);				/* 18 */
	x = (phy->interference.mode_2g & BCM4360_INTERFERENCE_HWACI) ||		/* 19 */
	    (phy->interference.mode_5g & BCM4360_INTERFERENCE_HWACI);
	bcm4360_phy_hwaci_setup_acphy(phy, false, x);
	bcm4360_phy_write(io, ACPHY_REG_0x358, 0xc07f);				/* 20 */
}

/* ------------------------------------------------- section 2, 3, 3a, 8, 1 */

/*
 * acphy-init.md, section 2 (wlc_phy_chanspec_shm_set): the chanspec to shared
 * memory. MAC revision 42 (> 39): the chanspec unchanged.
 */
void bcm4360_phy_chanspec_shm_set(struct bcm4360_phy *phy, u16 chanspec)
{
	bcm4360_shm_write(phy->hw, SHM_CHANSPEC, chanspec);
}

/*
 * acphy-init.md, section 3a
 * (wlc_phy_hirssi_elnabypass_set_ucode_params_acphy): the parameters of the
 * high-RSSI LNA bypass to shared memory, for the current band.
 */
void bcm4360_phy_hirssi_set_ucode_params(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_hirssi *h = &phy->hirssi;
	u16 a, b, c, n, st;
	u8 en;

	if (!h->supported)
		return;

	if (phy_is_2g(phy)) {
		en = h->state_2g;
		st = (u16)h->timer_2g;
	} else {
		en = h->state_5g;
		st = (u16)h->timer_5g;
	}

	if (en == 0) {
		a = 0x0032;
		b = 0x0527;
		c = 0x01f4;
	} else {
		if (!(st & 0x8000)) {		/* the bypass is engaged */
			a = phy_sext16(h->thresh_b, 8);
			b = 0x0529;
			n = h->count_b;
		} else {
			a = phy_sext16(h->thresh_a, 8);
			b = 0x0527;
			n = h->count_a;
		}
		switch (phy->radio_chanspec & BCM4360_CHANSPEC_BW) {
		case BCM4360_CHANSPEC_BW_40:
			c = n * 2;
			break;
		case BCM4360_CHANSPEC_BW_80:
			c = n * 4;
			break;
		default:
			c = n;
			break;
		}
	}

	bcm4360_shm_write(phy->hw, SHM_HIRSSI_A, a);
	bcm4360_shm_write(phy->hw, SHM_HIRSSI_B, b);
	bcm4360_shm_write(phy->hw, SHM_HIRSSI_C, c);
}

/*
 * acphy-init.md, section 3a (wlc_phy_hirssi_elnabypass_init_acphy): the state
 * of the high-RSSI bypass at initialisation. Unlike the attach-time version
 * the clock is on here, so the shared-memory parameters are written.
 */
static void phy_hirssi_elnabypass_init(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_hirssi *h = &phy->hirssi;

	h->timer_2g = (s16)0xffff;
	h->timer_5g = (s16)0xffff;
	if (!h->supported) {
		h->state_2g = 0;
		h->state_5g = 0;
		return;
	}
	h->state_2g = h->state_first;
	h->state_5g = h->state_first;
	if (phy->clk) {
		bcm4360_phy_hirssi_set_ucode_params(phy);
		bcm4360_shm_write(phy->hw, SHM_HIRSSI_FLAG, 0);
	}
}

/*
 * acphy-init.md, section 8 (sub_0b740d, wlc_phy_interference): apply an
 * interference mitigation mode. Called by wlc_phy_init with init = false.
 */
static void phy_interference(struct bcm4360_phy *phy, u32 mode, bool init)
{
	if (mode == 0) {
		bcm4360_phy_desense_aci_reset_params_acphy(phy, true, true, true);
		bcm4360_phy_hwaci_setup_acphy(phy, false, false);
		bcm4360_phy_aci_w2nb_setup_acphy(phy, false);
	}
	if (phy->interference.mode & BCM4360_INTERFERENCE_HWACI)
		bcm4360_phy_hwaci_setup_acphy(phy, true, true);
	if (phy->interference.mode & BCM4360_INTERFERENCE_W2NB)
		bcm4360_phy_aci_w2nb_setup_acphy(phy, true);
	phy->interference.mode_applied = mode;
	(void)init;
}

/*
 * acphy-init.md, section 3, step 2, and acphy-radio.md, section 9: the
 * regulator field from OTP word 16, written only when PHY(0x000) & 0xf <= 1.
 */
static void phy_init_regulator(struct bcm4360_phy *phy)
{
	u16 r = bcm4360_phy_read(&phy->io, ACPHY_REG_0x000) & 0x000f;
	u32 v = phy->otp_word16_bits8_12;

	if (r > 1)
		return;
	if (r == 0)
		v = v ? v : 5;
	else
		v = (v > 3) ? (v - 3) : 0;
	/*
	 * si_pmu_regcontrol reaches the PMU regulator control register 0 through
	 * si_corereg on ChipCommon: select the register in CC(0x658), modify bits
	 * 20..24 in CC(0x65c). In the PHY tests si_corereg is a compared service.
	 */
	bcm4360_chip_corereg(phy->hw, 0, CC_PMU_REGCTL_ADDR, 0xffffffff, PMU_REGCTL_RFLDO);
	bcm4360_chip_corereg(phy->hw, 0, CC_PMU_REGCTL_DATA, 0x01f00000, v << 20);
}

/*
 * acphy-init.md, section 3 (sub_0b018f, wlc_phy_init_acphy): the PHY specific
 * part of the initialisation. The function pointer pi+0x28 of wlc_phy_init.
 */
static void phy_init_acphy(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_io *io = &phy->io;
	u32 i;

	/* step 1: PHY revision 1 (and 3, > 5) */
	if (phy->ver.phy_rev == ACPHY_REV_1)
		bcm4360_phy_mod(io, ACPHY_REG_0x1b0, 0x8000, 0x8000);

	phy_init_regulator(phy);				/* step 2 */
	bcm4360_chip_gpiocontrol(phy->hw, 0xffff, 0, 0);	/* step 4 */
	phy_hirssi_elnabypass_init(phy);			/* step 5 */
	phy->csearch_count = 0;					/* step 6 */
	bcm4360_phy_set_regtbl_on_pwron_acphy(phy);		/* step 7 */

	/* steps 8, 9: run the channel function with the "init" flag set */
	phy->init_chan = true;
	bcm4360_phy_chanspec_set_acphy(phy, phy->radio_chanspec);
	phy->init_chan = false;					/* step 10 */
	phy->init_done = true;

	/* step 11: sixteen threshold registers */
	for (i = 0; i < ARRAY_SIZE(thresh_regs_a); i++)
		bcm4360_phy_write(io, thresh_regs_a[i], ACPHY_THRESH_A);
	for (i = 0; i < ARRAY_SIZE(thresh_regs_b); i++)
		bcm4360_phy_write(io, thresh_regs_b[i], ACPHY_THRESH_B);

	/* step 12: PHY revision 0 and 1 */
	bcm4360_phy_write(io, ACPHY_REG_0x16e, 0x0013);
	bcm4360_phy_write(io, ACPHY_REG_0x16f, 0x07d0);
	bcm4360_phy_write(io, ACPHY_REG_0x170, 0x07d0);

	/* step 13 */
	if (phy->init_por)
		phy->interference.channel = phy->radio_chanspec & BCM4360_CHANSPEC_CHANNEL;

	/* step 14: idle TSSI measurement (leaf) */
	bcm4360_phy_txpwrctrl_idle_tssi_meas_acphy(phy);
}

/*
 * acphy-init.md, section 1, step 17: choose the interference mode for the
 * current band and apply it.
 */
static void phy_init_interference(struct bcm4360_phy *phy)
{
	struct bcm4360_phy_interference *in = &phy->interference;
	bool is2g = phy_is_2g(phy);
	u32 m;

	if (phy->hold & BCM4360_PHY_HOLD_SCAN)
		return;

	if (in->forced) {
		if (is2g)
			m = in->forced_2g;
		else
			m = (in->forced_5g == 0 || in->forced_5g == 1) ? in->forced_5g : 0;
	} else {
		m = is2g ? in->mode_2g : in->mode_5g;
	}
	in->mode = m;
	phy_interference(phy, m, false);
}

/*
 * acphy-init.md, section 1 (wlc_phy_init), AC-PHY path: bring the PHY and the
 * radio from reset to operation on a channel.
 */
void bcm4360_phy_init(struct bcm4360_phy *phy, u16 chanspec)
{
	if (phy->init_running)					/* step 1 */
		return;

	phy->radio_chanspec = chanspec;				/* step 2 */
	phy->init_running = true;

	bcm4360_phy_chanspec_shm_set(phy, chanspec);		/* step 3 */
	(void)d11_read32(phy->hw, D11_REG_0x120);		/* step 4 */
	phy->interference.mode_applied = 0;			/* step 5 */

	/* step 6: not associated (pi+0xa8c is 0 in the traces) */
	if (!(phy->hold & BCM4360_PHY_HOLD_SCAN))
		phy->hold |= BCM4360_PHY_HOLD_NOT_ASSOC;

	bcm4360_phy_anacore(phy, true);				/* step 8 */

	/* step 9: the bandwidth of the PHY clock (wlapi_bmac_bw_set keeps pi+0x182) */
	if ((chanspec & BCM4360_CHANSPEC_BW) != phy->bw) {
		phy->bw = chanspec & BCM4360_CHANSPEC_BW;
		bcm4360_mac_bw_set(phy->hw, chanspec & BCM4360_CHANSPEC_BW);
	}

	/* step 11: run the whole switch-on sequence of the radio at every init */
	phy->init_done = false;
	phy->radio_on = false;
	bcm4360_phy_switch_radio(phy, true);			/* step 12 */

	phy_init_acphy(phy);					/* step 13 */
	phy->init_por = false;					/* step 14 */
	phy_init_interference(phy);				/* step 17 */
	phy->init_running = false;				/* step 18 */
	(void)bcm4360_shm_read(phy->hw, SHM_UCODE_BLOCK);	/* step 19 */
}
