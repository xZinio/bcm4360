/* SPDX-License-Identifier: ISC */
/*
 * The AC-PHY of the BCM4360: its state, and the interface through which the
 * MAC layer uses it.
 *
 * Specifications: docs/re/spec/acphy-attach.md (the state and how attach
 * makes it), docs/re/spec/acphy-radio.md, sections 1 to 4 (analog core,
 * identification of the radio, switching the radio),
 * docs/re/spec/acphy-chanspec.md, sections 3 and 4 (chanspec of the radio,
 * bandwidth of the PHY clock). The notation D11(o), PHY(a), RADIO(a) in the
 * comments is the one of docs/re/spec/access.md.
 *
 * Scope: chip 0x4360, AC-PHY (PHY type 11) of revision 0 or 1, radio 2069 of
 * revision 3 or 4 (docs/re/tasks/phy-attach.md).
 *
 * The specifications describe the state of Broadcom's object by places in its
 * three structures `sh`, `pi` and `pi_ac`. Here it is one structure, struct
 * bcm4360_phy. The comment of a field names
 *
 *  - the place in the object, so that the field is found from a statement of
 *    a specification ("pi+0xc2e");
 *  - what makes the value: a variable of the board, or a step of a procedure
 *    of acphy-attach.md: "P6.11" stands for procedure 6, step 11.
 *
 * A field whose purpose is not known is named by its place in the object
 * (pi_0xc04, acphy_0x64a for pi_ac+0x64a), not by a guess.
 */
#ifndef BCM4360_PHY_H
#define BCM4360_PHY_H

#include <bcm4360/types.h>
#include <bcm4360/hw.h>
#include <bcm4360/access.h>
#include <bcm4360/radio2069.h>
#include <bcm4360/pmu.h>

/*
 * Registers of the 802.11 core that the PHY code accesses directly, D11(o);
 * the others are in access.h.
 */
#define D11_PHY_ANACORE			0x3e6	/* analog core of the PHY */

/* D11_PHY_VERSION, D11(0x3e0): acphy-attach.md, procedure 2, step 8 */
#define D11_PHY_VERSION_REV		0x000f
#define D11_PHY_VERSION_TYPE		0x0f00
#define D11_PHY_VERSION_TYPE_SHIFT	8
#define D11_PHY_VERSION_ANA_SHIFT	12

/* D11_PHY_ANACORE: acphy-radio.md, section 1 */
#define D11_PHY_ANACORE_ON		0x0000
#define D11_PHY_ANACORE_OFF		0x00f4

/*
 * Status flags of the 802.11 core in its wrapper, WRAP(d11, 0x500):
 * acphy-attach.md, procedure 2, step 1
 */
#define D11_SFLAGS_PHY_2G		0x00000001	/* PHY for 2.4 GHz */
#define D11_SFLAGS_PHY_5G		0x00000002	/* PHY for 5 GHz */
#define D11_SFLAGS_PHY_BOTH		0x00000008	/* one PHY for both bands */

/*
 * Control flags of the 802.11 core in its wrapper, WRAP(d11, 0x408):
 * acphy-chanspec.md, "Constants" and section 3
 */
#define D11_CFLAGS_BW			0x000000c0	/* bandwidth of the PHY clock */
#define D11_CFLAGS_BW_20		0x00000040
#define D11_CFLAGS_BW_40		0x00000080
#define D11_CFLAGS_BW_80		0x000000c0
#define D11_CFLAGS_BAND_2G		0x00002000	/* the band is 2.4 GHz */

/* the SROM control register of ChipCommon, CC(0x190): acphy-attach.md, procedure 6, step 27 */
#define CC_SROMCTL_BIT4			0x00000010	/* set while OTP word 16 is read */

/* PHY type and revisions, from D11_PHY_VERSION */
#define BCM4360_PHY_TYPE_AC		11
#define ACPHY_REV_1			1	/* ACPHY_REV_0: radio2069.h */
#define ACPHY_REV_ANACORE_MAX		18	/* acphy-radio.md, section 1, step 1 */

/* PHY(0x00b): acphy-attach.md, procedure 6, step 14 */
#define ACPHY_REG_0x00b			0x00b
#define ACPHY_0x00b_CORES		0x0007	/* number of cores */

/* identification of the radio: docs/re/spec/access.md, "Radio registers" */
#define R2069_REG_REV			0x000
#define R2069_REG_ID			0x001
#define R2069_REV_REV			0x00ff	/* revision */
#define R2069_REV_MAJOR_SHIFT		4	/* major revision: bits 4..11 */
#define R2069_REV_MAJOR			0x00ff
#define R2069_REV_MINOR			0x000f	/* minor revision */
#define R2069_ID			0x2069

/* boards, acphy-attach.md, procedures 2 and 6 */
#define BCM4360_BOARDVENDOR_APPLE	0x106b
#define BCM4360_BOARDTYPE_0x093		0x093	/* with the vendor above: write limit 1 */
#define BCM4360_BOARDTYPE_0x111		0x111
#define BCM4360_BOARDTYPE_0x117		0x117
#define BCM4360_BOARDTYPE_0x137		0x137

/* boardflags, acphy-attach.md, procedure 6, steps 12 and 17 (bit 1: radio2069.h) */
#define BCM4360_BOARDFLAGS_BIT0		0x00000001
#define BCM4360_BOARDFLAGS_ELNA_2G	0x00001000	/* external LNA for 2.4 GHz */
#define BCM4360_BOARDFLAGS_ELNA_5G	0x10000000	/* external LNA for 5 GHz */
#define BCM4360_BOARDFLAGS_BIT29	0x20000000

/* boardflags2, acphy-attach.md, procedure 6, step 12 */
#define BCM4360_BOARDFLAGS2_BIT1	0x00000002

/* the variable boardflags3, acphy-attach.md, procedure 6, step 11 */
#define BCM4360_BOARDFLAGS3_BITS0_2	0x00000007
#define BCM4360_BOARDFLAGS3_SKIP_RCAL	0x00000008	/* skip RCAL, fixed value */
#define BCM4360_BOARDFLAGS3_BITS4_6	0x00000070
#define BCM4360_BOARDFLAGS3_BITS4_6_SHIFT 4
#define BCM4360_BOARDFLAGS3_BIT7	0x00000080
#define BCM4360_BOARDFLAGS3_SWCTRLMAP	0x00000100	/* switch control maps of the SROM */
#define BCM4360_BOARDFLAGS3_BIT9	0x00000200
#define BCM4360_BOARDFLAGS3_BIT10	0x00000400
#define BCM4360_BOARDFLAGS3_BIT11	0x00000800
#define BCM4360_BOARDFLAGS3_SPURMODE	0x00001000	/* wlc_phy_get_spurmode */
#define BCM4360_BOARDFLAGS3_RCAL_OTP	0x00002000	/* skip RCAL, value from OTP */
#define BCM4360_BOARDFLAGS3_BIT14	0x00004000
#define BCM4360_BOARDFLAGS3_BIT15	0x00008000

/* a chanspec: acphy-chanspec.md, "Chanspec" */
#define BCM4360_CHANSPEC_CHANNEL	0x00ff	/* channel number of the centre frequency */
#define BCM4360_CHANSPEC_CTL_SB		0x0700	/* position of the control channel */
#define BCM4360_CHANSPEC_BW		0x3800	/* bandwidth */
#define BCM4360_CHANSPEC_BW_20		0x1000
#define BCM4360_CHANSPEC_BW_40		0x1800
#define BCM4360_CHANSPEC_BW_80		0x2000
#define BCM4360_CHANSPEC_BAND		0xc000
#define BCM4360_CHANSPEC_BAND_2G	0x0000
#define BCM4360_CHANSPEC_BAND_5G	0xc000

/* the chanspecs a PHY starts with: acphy-attach.md, procedure 2, step 11 */
#define BCM4360_CHANSPEC_FIRST_2G	0x1001	/* channel 1, 20 MHz */
#define BCM4360_CHANSPEC_FIRST_5G	0xd024	/* channel 36, 20 MHz */

/*
 * Interference modes of the AC-PHY, a set of bits: acphy-attach.md, procedure
 * 2, step 12; acphy-init.md, section 1
 */
#define BCM4360_INTERFERENCE_DESENSE	0x00000001	/* desense by software */
#define BCM4360_INTERFERENCE_HWACI	0x00000002	/* "hardware ACI" mitigation */
#define BCM4360_INTERFERENCE_W2NB	0x00000004	/* "w2nb" ACI mitigation */

/*
 * Hold flags, pi+0x19c: acphy-radio.md, section 3, step 2; acphy-init.md,
 * section 1, step 6. The names of the bits 1 and 5 are interpretations of that
 * specification.
 */
#define BCM4360_PHY_HOLD_SCAN		0x00000002	/* bit 1: scan in progress */
#define BCM4360_PHY_HOLD_BIT2		0x00000004
#define BCM4360_PHY_HOLD_NOT_ASSOC	0x00000020	/* bit 5: not associated */

/*
 * Capabilities, result of bcm4360_phy_cap_get(): acphy-attach.md, procedure
 * 12. The meanings are those the callers give the bits.
 */
#define BCM4360_PHY_CAP_BIT0		0x00000001	/* by its use: 40 MHz (unverified) */
#define BCM4360_PHY_CAP_STBC		0x00000002
#define BCM4360_PHY_CAP_SGI		0x00000004	/* short guard interval */
#define BCM4360_PHY_CAP_BIT3		0x00000008	/* by its use: 80 MHz (unverified) */
#define BCM4360_PHY_CAP_LDPC		0x00000010
#define BCM4360_PHY_CAP_VHT_PROP_RATES	0x00000020	/* PHY revisions 1, 3 and above 5 */

/* sizes of the state */
#define BCM4360_PHY_CORES_MAX		3	/* cores that the variables of the board describe */
#define BCM4360_PHY_CORE_SLOTS		4	/* per core values of which the object keeps four */
#define BCM4360_PHY_SUBBANDS_5G		4
/* band index: 0 = 2.4 GHz, 1..4 = the sub-bands 0..3 of 5 GHz ("pi", notes) */
#define BCM4360_PHY_BANDS		(1 + BCM4360_PHY_SUBBANDS_5G)
#define BCM4360_PHY_BAND_2G		0
#define BCM4360_PHY_BAND_5G		1	/* the first sub-band of 5 GHz */
#define BCM4360_PHY_PA_PARAMS		3	/* parameters of the power detector curve */
#define BCM4360_PHY_PO_RANGES_5G	3	/* power offsets of 5 GHz: low, mid, high */
#define BCM4360_PHY_PO_GROUPS_5G	12
#define BCM4360_PHY_SWCTRLMAP		5	/* elements of a switch control map */
#define BCM4360_PHY_RXGAIN_STAGES	6
#define BCM4360_PHY_RXGAIN_ENTRIES	10	/* entries of a receive gain stage (acphy-chanspec, 9) */
#define BCM4360_PHY_CRSMIN_SAMPLES	4
#define BCM4360_PHY_HWACI_ENTRIES	4

/* bands of the receive gains: acphy-attach.md, procedure 6, step 18 */
enum bcm4360_phy_rxgain_band {
	BCM4360_PHY_RXGAIN_2G,
	BCM4360_PHY_RXGAIN_5G_LOW,
	BCM4360_PHY_RXGAIN_5G_MID,
	BCM4360_PHY_RXGAIN_5G_HIGH,
	BCM4360_PHY_RXGAIN_BANDS
};

/* what the platform knows about chip and board before the PHY exists */
struct bcm4360_phy_board {
	u32 chip;		/* chip id, 0x4360 */
	u32 chiprev;
	u32 chippkg;
	u32 corerev;		/* revision of the 802.11 core, 42 */
	u32 sromrev;		/* revision of the SROM, 11 */
	u32 boardtype;
	u32 boardrev;
	u32 boardvendor;
	u32 boardflags;
	u32 boardflags2;
	u32 xtal_hz;		/* frequency of the crystal (ALP clock) in Hz */
	u16 pci_vendor;
	u16 pci_device;
	struct bcm4360_pmu *pmu;	/* the PMU of the chip (pmu.h), for later tasks */
};

/*
 * What PHY and radio are (pi+0x160..0x17b): acphy-attach.md, procedure 2,
 * steps 6, 8 and 9, procedure 6, step 14; acphy-radio.md, section 2.
 */
struct bcm4360_phy_version {
	u32 phy_type;		/* pi+0x160: bits 8..11 of D11(0x3e0) */
	u32 phy_rev;		/* pi+0x164: bits 0..3 of D11(0x3e0) */
	u8 cores;		/* pi+0x168: number of cores N; 1 (P2.9), then P6.14 */
	u16 radio_id;		/* pi+0x16a: RADIO(1) */
	u8 radio_rev;		/* pi+0x16c: bits 0..7 of RADIO(0) */
	u8 radio_ver;		/* pi+0x16d: 0 */
	u8 radio_major;		/* pi+0x16e: bits 4..11 of RADIO(0) */
	u8 radio_minor;		/* pi+0x16f: bits 0..3 of RADIO(0) */
	u32 coreflags;		/* pi+0x170: flags for the core reset, P2.6 */
	u32 ana_rev;		/* pi+0x174: bits 12..15 of D11(0x3e0) */
};

/*
 * Flags of the board that the AC-PHY keeps, each as a value of its own:
 * acphy-attach.md, procedure 6, steps 11, 12 and 17.
 */
struct bcm4360_phy_board_flags {
	bool elna_2g;		/* pi_ac+0x340: boardflags bit 12, external LNA for 2.4 GHz */
	bool elna_5g;		/* pi_ac+0x341: boardflags bit 28, external LNA for 5 GHz */
	bool bf_bit0;		/* pi_ac+0x34b: boardflags bit 0 */
	bool bf_bit29;		/* pi_ac+0x34a: boardflags bit 29 */
	bool bf2_bit1;		/* pi_ac+0x344: boardflags2 bit 1 (only written) */
	u8 bf3_bits0_2;		/* pi_ac+0x343: boardflags3 bits 0..2 */
	bool bf3_skip_rcal;	/* pi_ac+0x345: boardflags3 bit 3: skip RCAL, fixed value */
	u8 bf3_bits4_6;		/* pi_ac+0x349: boardflags3 bits 4..6 */
	bool bf3_bit7;		/* pi_ac+0x348: boardflags3 bit 7 */
	bool bf3_swctrlmap;	/* pi_ac+0x34c: boardflags3 bit 8: the maps come from the SROM */
	bool bf3_bit9;		/* pi_ac+0x346: boardflags3 bit 9 */
	bool bf3_bit10;		/* pi_ac+0x347: boardflags3 bit 10 */
	bool bf3_bit11;		/* pi_ac+0x34d: boardflags3 bit 11 */
	bool bf3_spurmode;	/* pi_ac+0x34f: boardflags3 bit 12 (wlc_phy_get_spurmode) */
	bool bf3_rcal_otp;	/* pi_ac+0x34e: boardflags3 bit 13: skip RCAL, value from OTP */
	bool bf3_bit14;		/* pi_ac+0x354: boardflags3 bit 14 */
	bool bf3_bit15;		/* pi_ac+0x355: boardflags3 bit 15 */
};

/*
 * Interference mitigation: acphy-attach.md, procedure 1, step 4, procedure
 * 2, steps 11 to 13, procedure 6, step 29. A mode is a set of bits
 * BCM4360_INTERFERENCE_*.
 */
struct bcm4360_phy_interference {
	u32 mode;		/* sh+0x80: mode in use: 3 (P1.4), variable (P2.13), 0 (P6.29) */
	u32 mode_2g;		/* sh+0x84: configured for 2.4 GHz: P2.12, variable interference */
	u32 mode_5g;		/* sh+0x88: configured for 5 GHz: the same */
	u32 forced_2g;		/* sh+0x8c: used instead of mode_2g while `forced`; not at attach */
	u32 forced_5g;		/* sh+0x90: used instead of mode_5g while `forced`; not at attach */
	bool forced;		/* sh+0x94: 0 (P2.12) */
	u8 channel;		/* pi+0x240: channel the code was last set up for, P2.11 */
	u32 flags;		/* pi+0xc0c: flags of the interference code: 0 (P2.11) */
	u32 mode_applied;	/* pi+0xc3c: mode last applied by wlc_phy_interference (acphy-init) */
	u8 noise_window[8];	/* sh+0x96: `phy_noise_window` (bcm): 0 (P1.3) */
};

/* receive gains of a core in a band (pi_ac+0x3e0 + 12 * band + 3 * core), P6.18 */
struct bcm4360_phy_rxgains {
	u8 elna_gain;		/* gain of the external LNA in dB: 2 * rxgains..elnagaina<c> + 6 */
	u8 triso;		/* isolation of the T/R switch in dB: 2 * rxgains..trisoa<c> + 8 */
	u8 trelnabyp;		/* the T/R switch bypasses the external LNA: rxgains..trelnabypa<c> */
};

/* receive gain errors of a band (pi+0x1e3 + 5 * band), P6.21 and P6.22 */
struct bcm4360_phy_rxgainerr {
	/* core 0: rxgainerr2ga0, rxgainerr5ga0[k]; cores 1 and 2: the sum with core 0 */
	s8 err[BCM4360_PHY_CORES_MAX];
	bool empty;		/* pi+0x1e7 + 5 * band: the SROM has no gain errors */
};

/*
 * Power offsets per rate group of SROM revision 11 (pi+0xc4c..0xd3b):
 * acphy-attach.md, "Tx power offsets" and procedure 7, step 2. The object
 * keeps some offsets several times; what a copy stands for is decided by its
 * consumer (wlc_phy_txpwr_apply_srom11), which is not specified yet. So the
 * copies are kept, in the order of the object. Elements of three: the ranges
 * low, mid and high of 5 GHz.
 */
struct bcm4360_phy_srom_po {
	u16 cckbw202gpo;	/* pi+0xc4c */
	u16 cckbw20ul2gpo;	/* pi+0xc4e */
	/*
	 * pi+0xc50, 0xc54, 0xc58: three times dot11agofdmhrbw202gpo << 16 | n1
	 * << 12 | n1 << 8 | n0 << 4 | n0, n0 and n1 = bits 0..3 and 4..7 of
	 * ofdmlrbw202gpo
	 */
	u32 ofdm_2g[3];
	/* pi+0xc5c..0xc68: mcsbw202gpo, mcsbw202gpo, mcsbw402gpo, not written */
	u32 mcs_2g[4];
	/*
	 * pi+0xc6c + 12 * group + 4 * range. Groups 0, 1, 2, 6, 7, 8:
	 * mcsbw205g?po; groups 3, 4, 9, 10: mcsbw405g?po; groups 5, 11:
	 * mcsbw805g?po.
	 */
	u32 mcs_5g[BCM4360_PHY_PO_GROUPS_5G][BCM4360_PHY_PO_RANGES_5G];
	u16 ofdmlrbw202gpo;	/* pi+0xd14 */
	u16 sb20in40lrpo;	/* pi+0xd16 */
	u16 sb20in40hrpo;	/* pi+0xd18 */
	u16 dot11agduplrpo;	/* pi+0xd1a */
	u16 dot11agduphrpo;	/* pi+0xd1c */
	u16 mcslr5gpo[BCM4360_PHY_PO_RANGES_5G];		/* pi+0xd1e: mcslr5g?po */
	u16 sb20in80and160lr5gpo[BCM4360_PHY_PO_RANGES_5G];	/* pi+0xd24 */
	u16 sb20in80and160hr5gpo[BCM4360_PHY_PO_RANGES_5G];	/* pi+0xd2a */
	u16 sb40and80lr5gpo[BCM4360_PHY_PO_RANGES_5G];		/* pi+0xd30 */
	u16 sb40and80hr5gpo[BCM4360_PHY_PO_RANGES_5G];		/* pi+0xd36 */
};

/*
 * Transmit power: what the board says (acphy-attach.md, procedure 7) and the
 * state of the power control that attach sets.
 */
struct bcm4360_phy_txpwr {
	/* pi+0xe7c + 5 * core + band: maxp2ga<c>, maxp5ga<c>[k], in quarter dBm; P7.1 */
	u8 maxp[BCM4360_PHY_CORES_MAX][BCM4360_PHY_BANDS];
	/*
	 * The three parameters of the power detector curve, P7.1: pa2ga<c>[i],
	 * pa5ga<c>[3 * k + i]. Parameter 0: pi+0xe04 + 10 * core + 2 * band, 1:
	 * pi+0xe2c + .., 2: pi+0xe54 + ..
	 */
	s16 pa[BCM4360_PHY_CORES_MAX][BCM4360_PHY_BANDS][BCM4360_PHY_PA_PARAMS];
	s16 tssifloor[BCM4360_PHY_BANDS];	/* pi+0xea6 + 2 * band: tssifloor2g, ..5g[k]; P7.1 */
	struct bcm4360_phy_srom_po po;		/* P7.2 */
	u16 pdoffset40ma[BCM4360_PHY_CORES_MAX];	/* pi+0xe90 + 2 * core; P7.3 */
	u16 pdoffset80ma[BCM4360_PHY_CORES_MAX];	/* pi+0xe98 + 2 * core; P7.3 */
	u8 pdoffset2g40ma[BCM4360_PHY_CORES_MAX];	/* pi+0xea0 + core; P7.3 */
	u8 pdoffset2g40mvalid;				/* pi+0xea4; P7.3 */
	u8 pdoffsetcckma[BCM4360_PHY_CORES_MAX];	/* pi+0xece + core; P7.3 */
	u8 pdgain2g;		/* pi_ac+0x410: variable, P6.13 */
	u8 pdgain5g;		/* pi_ac+0x411: variable, P6.13 */
	u8 txidxcap2g;		/* sh+0xd6: upper limit of the tx gain index; variable, P6.11 */
	u8 txidxcap5g;		/* sh+0xd7: the same for 5 GHz */
	u8 extpagain2g;		/* sh+0xb0: kind of external power amplifier; variable, P6.11 */
	u8 extpagain5g;		/* sh+0xac: the same for 5 GHz */
	u8 txpwrbckof;		/* pi+0x10a0: variable, default 6; P2.20 */
	u8 tssilimucod;		/* pi+0x10b8: variable, default 1; P2.20 */
	u8 percent;		/* pi+0x184: `txpwr_percent`: 100 (P2.4) */
	bool user_at_rfport;	/* pi+0x140: `user_txpwr_at_rfport` (bcm): 0 (P2.14) */
	u8 user_target;		/* pi+0x1d6: target of the user (wlc_phy_txpower_set): 0x7f (P2.14) */
	u8 min_power;		/* pi+0xc37: lowest target power allowed in dBm: 1 (P2.14) */
	bool hwpwrctrl_capable;	/* pi+0xc40: 1 (P6.15) */
	bool hwpwrctrl;		/* pi+0xfa0: power control by hardware is enabled: 1 (P6.15) */
	s16 radiopwr_override;	/* pi+0x236: (bcm) (unverified): -1 (P2.14) */
	/* pi+0x1c8: object of the power limits per rate, made later: NULL (P2.14) */
	void *ppr;
	/* pi_ac+0x010 + core: gain index last set; 0x40 = "uninitialised" (procedure 5) */
	u8 index[BCM4360_PHY_CORE_SLOTS];
	/*
	 * pi+0x216 + core: highest target power over all rates of the core, in
	 * quarter dBm; computed by sub_0b8ca4 (wlc_phy_cmn.c) before it calls
	 * sub_09949f. Not set on the init/channel path, so 0 here (acphy-txpower.md).
	 */
	s8 target_max[BCM4360_PHY_CORE_SLOTS];
	/* pi_ac+0x44e + core: idle TSSI measured at init (acphy-init.md, section 6): 0 in the model */
	s16 idle_tssi[BCM4360_PHY_CORE_SLOTS];
	/* pi_ac+0x456 + core: power offset of the core (wlc_phy_txpower_core_offset_*): 0 */
	s8 core_offset[BCM4360_PHY_CORE_SLOTS];
	/*
	 * pi_ac+0x45a + core: gain index that the control loop had reached when it
	 * was switched off (acphy-txpower.md); 0x80 = none (P6.10)
	 */
	u8 index_saved[BCM4360_PHY_CORE_SLOTS];
	/* pi_ac+0x8de, 0x8df, 0x8e0: result of wlc_phy_get_olpc_pwroffset for 20, 40, 80 MHz: 1 */
	u8 olpc_pwroffset[3];
};

/* temperature: acphy-attach.md, procedure 2, step 17, procedures 4 and 7, procedure 6, step 20 */
struct bcm4360_phy_temp {
	u8 thresh;		/* pi+0xc2e: the tx chains are switched off; tempthresh, P2.17 */
	u8 thresh_max;		/* pi+0xc2f: copy of thresh (upper limit of the threshold) */
	u8 hysteresis;		/* pi+0xc30: variable temps_hysteresis, P2.17 */
	u8 thresh_on;		/* pi+0xc31: thresh - hysteresis: the chains are switched on again */
	bool heated;		/* pi+0xc32: the chip is heated up: 0 (P2.17) */
	u8 chain_bitmap;	/* pi+0xc33: mask of all cores in both nibbles, P2.17 */
	u8 pi_0xc34;		/* pi+0xc34: 0 (P2.17) */
	s8 offset;		/* pi+0xc35: variable tempoffset (P7.4), cleared by P2.17 */
	/* pi+0x210: variable rawtempsense, the reference of upd_gain_wrt_temp; P6.20 */
	s16 rawtempsense;
	/*
	 * pi_ac+0x8dc: the last measured temperature, written by
	 * wlc_phy_tempsense_acphy (acphy-cal-rx section 3). Init from rawtempsense
	 * at attach; here zero until the first tempsense (the RSSI path that reads
	 * it is not exercised by the tests). Added by the calibrations task.
	 */
	s16 measured_temp;
	u8 cal_delta;		/* pi+0xf9c: variable phycal_tempdelta, procedure 4 */
	u8 cal_delta_default;	/* pi+0xf9e: its default: 0 (P2.4), 40 (P7.5), procedure 4 */
	s16 cal_last;		/* pi+0x1084: temperature of the last calibration (unverified): -50 */
};

/*
 * Calibration of the minimum power of the carrier sense ("CRS minimum power
 * calibration", pi_ac+0x018..0x044, 0x33c..0x33e): acphy-attach.md, procedure
 * 6, steps 3, 15 and 16.
 */
struct bcm4360_phy_crsmincal {
	bool enable;		/* pi_ac+0x33c: 1 (P6.3) */
	u8 state[2];		/* pi_ac+0x33d, 0x33e: state of the calibration: 0 (P6.3) */
	/* pi_ac+0x018 + 4 * band + core: noise power the thresholds were computed for: -30 */
	s8 noise[BCM4360_PHY_BANDS][BCM4360_PHY_CORE_SLOTS];
	/* pi_ac+0x02c + 4 * sample + core: the last four noise samples */
	s8 sample[BCM4360_PHY_CRSMIN_SAMPLES][BCM4360_PHY_CORE_SLOTS];
	u8 next_sample;		/* pi_ac+0x03c: index of the next sample */
	s8 average[BCM4360_PHY_CORE_SLOTS];	/* pi_ac+0x03d: averaged noise of the last run */
	u8 thresh;		/* pi_ac+0x042: threshold in use: 0x36 (P6.3) */
	u8 runs;		/* pi_ac+0x043: counter of runs */
	u8 channel;		/* pi_ac+0x044: channel number of the last run */
};

/*
 * "High RSSI: bypass the external LNA" (pi_ac+0x906..0x916): acphy-attach.md,
 * procedure 6, step 6, procedures 9 and 10.
 */
struct bcm4360_phy_hirssi {
	bool supported;		/* pi_ac+0x912: PHY revision <= 1 */
	u8 state_first;		/* pi_ac+0x906: state to start with: 0 */
	u16 acphy_0x908;	/* pi_ac+0x908: 5; acphy-chanspec.md: duration in watchdog ticks */
	u16 count_a;		/* pi_ac+0x90a: count for the first kind of period, 20 MHz: 31 */
	u16 count_b;		/* pi_ac+0x90c: the same for the second kind of period: 31 */
	s8 thresh_a;		/* pi_ac+0x90e: threshold for the first kind of period: -13 */
	s8 thresh_b;		/* pi_ac+0x90f: threshold for the second kind: -15 */
	u8 state_2g;		/* pi_ac+0x910: state on 2.4 GHz */
	u8 state_5g;		/* pi_ac+0x911: state on 5 GHz */
	s16 timer_2g;		/* pi_ac+0x914: timer on 2.4 GHz, negative: not running */
	s16 timer_5g;		/* pi_ac+0x916: timer on 5 GHz */
};

/* an entry of the two tables of bcm4360_phy_hwaci: acphy-attach.md, procedure 11, step 2 */
struct bcm4360_phy_hwaci_entry {
	u16 w0;			/* +0 */
	u8 b2;			/* +2 */
	u8 b3;			/* +3 */
	u8 b4;			/* +4 */
	u8 b5;			/* +5 */
	u8 b6;			/* +6 */
};

/*
 * Parameters of the interference mitigation (pi_ac+0x672..0x6c3):
 * acphy-attach.md, procedure 11 (wlc_phy_hwaci_init_acphy). Their meaning
 * belongs to the specification of the consumers.
 */
struct bcm4360_phy_hwaci {
	u16 acphy_0x672;	/* 300 */
	u16 acphy_0x674;	/* 1000 */
	u16 acphy_0x676;	/* 500 */
	u16 acphy_0x678;	/* 1 */
	u8 acphy_0x67a;		/* 15 */
	u8 acphy_0x67b;		/* 15 */
	u8 acphy_0x67c;		/* 1 */
	u8 acphy_0x67d;		/* 0 */
	u8 acphy_0x67e;		/* 0 */
	u8 acphy_0x67f;		/* 0 */
	u8 acphy_0x680;		/* 4 */
	struct bcm4360_phy_hwaci_entry table1[BCM4360_PHY_HWACI_ENTRIES];	/* pi_ac+0x682 */
	struct bcm4360_phy_hwaci_entry table2[BCM4360_PHY_HWACI_ENTRIES];	/* pi_ac+0x6a2 */
	u8 table1_entries;	/* pi_ac+0x6c2: 4 */
	u8 table2_entries;	/* pi_ac+0x6c3: 4 */
};

/*
 * The nine-byte desense set: acphy-desense.md, "The 9-byte desense set". A
 * desense set is nine bytes; the code below indexes them by name.
 */
enum bcm4360_phy_desense_byte {
	BCM4360_DESENSE_OFDM,		/* 0: OFDM sensitivity reduction, dB */
	BCM4360_DESENSE_BPHY,		/* 1: 802.11b sensitivity reduction, dB */
	BCM4360_DESENSE_LNA1_TBL,	/* 2: entries off the stage-1 gain table */
	BCM4360_DESENSE_LNA2_TBL,	/* 3: the same for stage 2 */
	BCM4360_DESENSE_LNA1_GAINLMT,	/* 4: stage-1 gain-limit entries blocked */
	BCM4360_DESENSE_LNA2_GAINLMT,	/* 5: the same for stage 2 */
	BCM4360_DESENSE_ELNA_BYPASS,	/* 6: bypass the external LNA */
	BCM4360_DESENSE_NF_HIT,		/* 7: dB added to the clip thresholds */
	BCM4360_DESENSE_ON,		/* 8: desense is in force */
	BCM4360_DESENSE_BYTES,
};

/* number of interference records per band (acphy-desense.md, "Data") */
#define BCM4360_PHY_INTERF_RECORDS	3

/*
 * The interference (desense) record of one channel (0x50 bytes in the object,
 * pi_ac+0x6c8 for 2.4 GHz, pi_ac+0x7b8 for 5 GHz): acphy-desense.md, "Data",
 * and acphy-chanspec.md annex A4. Field comments give the object offset.
 */
struct bcm4360_phy_interf_record {
	u8 channel;		/* +0x00: channel number (0 = free slot) */
	u16 bw;			/* +0x02: bandwidth bits */
	u64 time;		/* +0x08: time of the last use */
	u8 desense[BCM4360_DESENSE_BYTES];	/* +0x10..0x18: this channel's set */
	s8 rssi;		/* +0x19: RSSI-based desense input */
	u32 narrow_ring[BCM4360_PHY_HWACI_ENTRIES];	/* +0x1c: narrow-band metric */
	u8 narrow_lo;		/* +0x2c */
	u8 narrow_hi;		/* +0x2d */
	u8 narrow_dwell;	/* +0x2e */
	u32 wide_ring[BCM4360_PHY_HWACI_ENTRIES];	/* +0x30: wide-band metric */
	u8 wide_lo;		/* +0x40 */
	u8 wide_hi;		/* +0x41 */
	u8 wide_dwell;		/* +0x42 */
	u8 ring_idx;		/* +0x44: write index of the two metric rings */
	u8 settle;		/* +0x45: settle countdown of the desense-aci engine */
	u8 hwaci_level;		/* +0x46: hwaci current level */
	u8 hwaci_min;		/* +0x47: hwaci minimum level seen */
	u8 hwaci_hold;		/* +0x48: hwaci hold-down timer */
	u8 hwaci_settle;	/* +0x49: hwaci settle countdown */
};

/*
 * The desense state (acphy-desense.md, "Overview" and "Data"): the wanted,
 * applied and baseline desense sets, the per-band interference records and the
 * Bluetooth-coexistence set. All zero after attach.
 */
struct bcm4360_phy_desense {
	u8 applied[BCM4360_DESENSE_BYTES];	/* pi_ac+0x656: applied last */
	u8 base[BCM4360_DESENSE_BYTES];		/* pi_ac+0x65f: "no interference" baseline */
	u8 total[BCM4360_DESENSE_BYTES];	/* pi_ac+0x668: wanted; [8] = pi_ac+0x670 */
	struct bcm4360_phy_interf_record record[2][BCM4360_PHY_INTERF_RECORDS];	/* pi_ac+0x6c8 / 0x7b8 */
	struct bcm4360_phy_interf_record *cur;	/* pi_ac+0x8a8: record of the channel */
	u32 bt_profile;		/* pi_ac+0x8b0: Bluetooth-coexistence profile id (0 = off) */
	u8 bt_desense[BCM4360_DESENSE_BYTES];	/* pi_ac+0x8b4: Bluetooth-coexistence set */
};

/*
 * PHY registers as they were at attach (pi_ac+0x8ea..0x8fd): acphy-attach.md,
 * procedure 6, step 4; acphy-radio.md, section 19. In the order in which they
 * are read.
 */
struct bcm4360_phy_saved_regs {
	u16 phy_0x739;		/* pi_ac+0x8ee */
	u16 phy_0x73a;		/* pi_ac+0x8f0 */
	u16 phy_0x725;		/* pi_ac+0x8f2 */
	u16 phy_0x729;		/* pi_ac+0x8ea */
	u16 phy_0x721;		/* pi_ac+0x8ec */
	u16 phy_0x728;		/* pi_ac+0x8f4 */
	u16 phy_0x720;		/* pi_ac+0x8f6 */
	u16 phy_0x408;		/* pi_ac+0x8f8 */
	u16 phy_0x417;		/* pi_ac+0x8fa */
	u16 phy_0x416;		/* pi_ac+0x8fc */
};

/* corrections of the signal strength: acphy-attach.md, procedure 2, step 20, procedure 8 */
struct bcm4360_phy_rssi {
	s8 corrnorm;		/* pi+0x10fe: variable rssicorrnorm, default 0 */
	s8 corratten;		/* pi+0x10ff: variable rssicorratten, default 7 */
	s8 corrnorm5g[3];	/* pi+0x1100: variable rssicorrnorm5g */
	s8 corratten5g[3];	/* pi+0x1103: variable rssicorratten5g */
	s8 corrperrg2g[5];	/* pi+0x1106: variable rssicorrperrg2g, defaults -150, -150, 0, 0, 0 */
	s8 corrperrg5g[5];	/* pi+0x110b: variable rssicorrperrg5g, the same defaults */
	s8 cga_5g[24];		/* pi+0x1110: variable 5g_cga */
	s8 cga_2g[14];		/* pi+0x1128: variable 2g_cga */
	/* pi_ac+0x3a8 + 2 * core: variable rssicorrnorm_c<c>; procedure 8 */
	s8 corrnorm_c[BCM4360_PHY_CORES_MAX][2];
	/* pi_ac+0x3b0 + 12 * core: variable rssicorrnorm5g_c<c>; procedure 8 */
	s8 corrnorm5g_c[BCM4360_PHY_CORES_MAX][12];
	u8 mode;		/* sh+0xa8: `rssi_mode` (bcm): 0 (P1.4) */
};

/*
 * The calibration state block (acphy-cal-tx.md "The calibration state block",
 * pi+0xfb8, pointer pi+0xf58). Zero after attach; owned by the calibrations
 * task. Per core the coefficient arrays hold the five 16-bit words a, b, d, e,
 * f in this order (the sub_09bf99 selections 12..15 = intermediate,
 * 16..19 = final). Field comments give the object offset in the block.
 */
#define BCM4360_CAL_COEFFS	5	/* a, b, d, e, f per core */
#define BCM4360_CAL_GAINREC	10	/* pre-cal transmit gain record, bytes */

struct bcm4360_phy_cal_state {
	u8 search_mode;		/* +0x00: cal[0], mphase search mode */
	u8 phase;		/* +0x01: cal[1], phase counter (0 = idle/single-shot) */
	u8 subphase;		/* +0x02: cal[2] */
	u16 coef_final[BCM4360_PHY_CORES_MAX][BCM4360_CAL_COEFFS];	/* +0x04: sel 16..19 */
	u16 coef_inter[BCM4360_PHY_CORES_MAX][BCM4360_CAL_COEFFS];	/* +0x2c: sel 12..15 */
	u16 coef_copy[BCM4360_PHY_CORES_MAX][2];			/* +0x54: mode 2 (a, b) */
	u8 restart_ok;		/* +0x64: restart-from-previous-coeffs allowed */
	u8 core_flag[BCM4360_PHY_CORES_MAX];	/* +0x65 + c: gainlut loaded for the core */
	u8 gainrec[BCM4360_PHY_CORES_MAX][BCM4360_CAL_GAINREC];	/* +0x92: pre-cal gain record */
	u16 last_chanspec;	/* +0xba: chanspec the last calibration ran on */
	u32 last_cal_time;	/* +0xc0: time of the last calibration (sh+0x34) */
	u32 last_temp_time;	/* +0xc4: time of the last temperature reading */
	u32 throttle;		/* +0xc8: set when perical decides not to calibrate */
	s16 last_temp;		/* +0xcc: last calibration temperature */
	u8 first_cal;		/* pi+0xf84: first calibration after an association */
};

/* calibrations: acphy-attach.md, procedure 2, steps 4 and 14, procedure 5, procedure 6, step 3 */
struct bcm4360_phy_cal {
	bool init_done;		/* pi+0x187: `initialized`, wlc_phy_cal_init done: 0 (procedure 5) */
	u8 mode;		/* pi+0xf89: 2 = periodic calibration in several phases (P6.3) */
	u16 phase_delay_ms;	/* pi+0xf8a: delay between two phases: 5 (P6.3) */
	u8 pi_0xf68;		/* pi+0xf68: 10 (P2.14); changed only by an iovar */
	u8 pi_0xf69;		/* pi+0xf69: 3 (P2.14); changed only by an iovar */
	u8 pi_0xf82;		/* pi+0xf82: 0 (P2.14); wlc_phy_dig_lpf_override_acphy */
	u8 pi_0xf9d;		/* pi+0xf9d: 4 (P2.4) */
	u8 pi_0xf9f;		/* pi+0xf9f: 4 (P2.4) */
	u32 pi_0x1080;		/* pi+0x1080: 0 (P2.14) */
	u16 acphy_0x44a;	/* pi_ac+0x44a: 0 (P6.3); wlc_phy_cals_acphy */
	u8 acphy_0x900;		/* pi_ac+0x900: 0 (P6.8); wlc_phy_rx_iq_est_acphy */
	u8 acphy_0x901;		/* pi_ac+0x901: 1 (P6.8); wlc_phy_rx_iq_est_acphy */
	/*
	 * The calibration state block (pi+0xfb8, pointer pi+0xf58): all zero
	 * after attach, added by the calibrations task.
	 */
	struct bcm4360_phy_cal_state state;
	/* the tone / sample player (acphy-cal-tx sections 12/14) */
	u16 tone_bbmult[BCM4360_PHY_CORES_MAX];	/* pi_ac+0x02 + 2c: bbmult saved by the tone */
	u8 tone_bbmult_saved;			/* pi_ac+0x0a: a tone bbmult is forced */
	/* the scan/roam calibration-result cache (acphy-cal-tx section 20) */
	u8 scanroam_cache[BCM4360_PHY_CORES_MAX][14];	/* pi_ac+0x412 + 0xe*c */
	/* rx IQ calibration scratch (acphy-cal-rx) */
	u8 rxcal_loopback;			/* pi_ac+0x11a: radio loopback engaged */
	s32 rxcal_digfilt[BCM4360_PHY_CORES_MAX];	/* pi_ac+0x8c8 + 4c: digital-filter slope */
};

/* the state of the PHY of one card */
struct bcm4360_phy {
	/*
	 * The card and the other layers. `hw` stands for osh, sih, the PHY
	 * shim and the registers (sh+0x10, 0x18, 0x20, pi+0x148).
	 */
	struct bcm4360_hw *hw;
	struct bcm4360_pmu *pmu;		/* board.pmu: for the initialisation */
	/* sh+0x28, 0x38..0x48, 0x58..0x68: identification of chip and board, P1.2 */
	struct bcm4360_phy_board board;
	/* register access; write pacing: pi+0x226 (counter), pi+0x228 (limit, P2.4) */
	struct bcm4360_phy_io io;
	/* the radio; pi_ac+0x339, 0x33a, 0x33b: results of the RCCAL, P6.3 */
	struct bcm4360_radio radio;
	void *timer_phycal;			/* pi+0x1088: timer "phycal", P2.15 */
	u32 refcnt;				/* pi+0x188: P2.21, procedure 13 */
	u16 fabid;				/* pi+0x10fc: si_fabid: 0 for this chip, P2.8 */

	/* what PHY and radio are */
	struct bcm4360_phy_version ver;
	struct bcm4360_phy_version ver_ro;	/* pi+0x00: `pubpi_ro` (bcm): copy of ver, P2.21 */
	u32 xtal_hz;				/* pi+0xc24: frequency of the crystal, P6.9 */
	u16 otp_word16_bits8_12;		/* pi_ac+0x8e2: regulator field, P6.27 */
	u16 rcal_otp;				/* sh+0xf2: bits 0..3 of OTP word 16, P6.32 */

	/* what the MAC layer tells the PHY */
	u32 machwcap;		/* sh+0x2c: capabilities of the MAC, procedure 12 */
	bool up;		/* sh+0x30: the driver is up */
	bool clk;		/* sh+0x31: the clock of the core is on, the PHY may be accessed */
	bool init_por;		/* pi+0x185: the next initialisation loads the tables: 1 (P2.4) */
	u32 hold;		/* pi+0x19c: hold flags BCM4360_PHY_HOLD_*; not at attach */
	u8 hw_txchain;		/* sh+0xa4: mask of the tx chains of the hardware, procedure 12 */
	u8 hw_rxchain;		/* sh+0xa5: mask of the rx chains of the hardware, procedure 12 */
	u8 txchain;		/* sh+0xa6: mask of the tx chains in use, procedure 12 */
	u8 rxchain;		/* sh+0xa7: mask of the rx chains in use: 3 (P2.14), procedure 12 */

	/* channel and radio */
	u16 radio_chanspec;	/* pi+0x17e: chanspec of the radio, P2.11 */
	u16 bw;			/* pi+0x182: bandwidth bits the PHY clock is set to: 0x1000 (P2.11) */
	bool radio_on;		/* pi+0xf88: the radio is on; acphy-radio.md, section 4 */
	bool init_running;	/* pi+0x186: wlc_phy_init is running (acphy-init, section 1) */
	u16 csearch_count;	/* pi_ac+0x0c: nesting counter of the carrier search (acphy-init, H4) */
	u8 phyreg_count;	/* pi+0x113d: nesting counter of wlc_phyreg_enter (acphy-init, H13) */
	bool init_chan;		/* pi_ac+0x32c: the initialisation calls the channel function */
	bool init_done;		/* pi_ac+0x32d: the PHY is initialised */
	bool band_2g_last;	/* pi_ac+0x330: band last set up is 2.4 GHz, P6.3 */
	u32 bw_last;		/* pi_ac+0x334: bandwidth bits last set up, P6.3 */
	u8 spur_mode;		/* pi_ac+0x338: spur mode in use: 0 (P6.3) */
	u8 subband5gver;	/* sh+0x4c: borders of the sub-bands of 5 GHz; variable, P6.11 */
	u8 lp_mode;		/* pi_ac+0x8e4: low power mode (PHY revisions 2, 5, 6): 1 (P6.3) */
	bool lpvco_2g;		/* pi_ac+0x8e5: force low power VCO on 2.4 GHz: 0 (P6.28) */
	struct bcm4360_phy_saved_regs saved;

	/* periods of the watchdog (bcm): P1.4 */
	u32 fast_timer;		/* sh+0x74: 15 */
	u32 slow_timer;		/* sh+0x78: 60 */
	u32 glacial_timer;	/* sh+0x7c: 120 */
	bool watchdog_override;	/* pi+0x18c: (bcm): 1 (P2.14) */
	u8 watchdog_core;	/* pi_ac+0x918: core that the watchdog puts into PHY(0x520): 0 */

	/* the front end and the receiver */
	struct bcm4360_phy_board_flags flags;
	u8 femctrl;		/* pi_ac+0x342: kind of front end control; variable, P6.11 */
	u8 bt_active;		/* pi+0xfa2: Bluetooth is active (coexistence); 0 by default */
	u8 acphy_1169;		/* pi+0x1169: 40 MHz resampler override (iovar only); 0 by default */
	u8 acphy_116a;		/* pi+0x116a: set by sub_0a4adc; read by out-of-scope code */
	u16 rpcal2g;		/* sh+0xcc: variable, P6.11 */
	u16 rpcal5gb[BCM4360_PHY_SUBBANDS_5G];	/* sh+0xce..0xd4: rpcal5gb0..3, P6.11 */
	u8 cckdigfilttype;	/* pi_ac+0x8fe: digital filter for CCK; variable, default 1, P6.13 */
	bool rxldpc;		/* pi_ac+0x8ff: LDPC decoding is on: 1 (P6.6) */
	u8 preamble_override;	/* pi+0x22a: wlc_phy_preamble_override_set: 0 (P6.8) */
	bool phynoise_polling;	/* pi+0x198: (bcm): 0 (P2.14) */
	/* switch control maps, only with flags.bf3_swctrlmap: P6.25 */
	u32 swctrlmap_2g[BCM4360_PHY_SWCTRLMAP];	/* pi_ac+0x358 */
	u32 swctrlmapext_2g[BCM4360_PHY_SWCTRLMAP];	/* pi_ac+0x36c */
	u32 swctrlmap_5g[BCM4360_PHY_SWCTRLMAP];	/* pi_ac+0x380 */
	u32 swctrlmapext_5g[BCM4360_PHY_SWCTRLMAP];	/* pi_ac+0x394 */
	struct bcm4360_phy_rxgains rxgains[BCM4360_PHY_RXGAIN_BANDS][BCM4360_PHY_CORES_MAX];
	/* pi_ac+0x64a: number of entries of the receive gain stages: 2, 6, 7, 10, 8, 8 (P6.10) */
	u8 rxgain_stage_entries[BCM4360_PHY_RXGAIN_STAGES];
	/*
	 * Receive gain stage tables (acphy-chanspec.md, 9): gain in dB and code of
	 * entry k of stage s of core c, and the upper gain limit up to a stage.
	 * Filled by the receive gain code (acphy-rxgain); zero until then.
	 */
	s8 rxgain_gain[BCM4360_PHY_CORES_MAX][BCM4360_PHY_RXGAIN_STAGES][BCM4360_PHY_RXGAIN_ENTRIES];	/* pi_ac+0x46a */
	u8 rxgain_code[BCM4360_PHY_CORES_MAX][BCM4360_PHY_RXGAIN_STAGES][BCM4360_PHY_RXGAIN_ENTRIES];	/* pi_ac+0x4a6 */
	u8 rxgain_max[BCM4360_PHY_RXGAIN_STAGES];	/* pi_ac+0x650 */
	u8 rxgain_trloss[BCM4360_PHY_CORES_MAX];	/* pi_ac+0x45f + 3*core: T/R switch loss in dB */
	u8 rxgain_bypass[BCM4360_PHY_CORES_MAX];	/* pi_ac+0x460 + 3*core: T/R switch bypasses the ext LNA (acphy-desense A5/A6) */
	s8 rssi_gain_corr[BCM4360_PHY_CORES_MAX];	/* pi+0x212: rx gain error correction (acphy-chanspec, 5 step 32) */
	struct bcm4360_phy_rxgainerr rxgainerr[BCM4360_PHY_BANDS];
	/* pi+0x1fc + 4 * band + core: noise level in dBm: -70 - noiselvl..a<c>; P6.23, P6.24 */
	s8 noiselvl[BCM4360_PHY_BANDS][BCM4360_PHY_CORES_MAX];
	u16 acphy_0x902;	/* pi_ac+0x902: 0x404e (P6.8); acphy-init.md: clip detection */
	u16 acphy_0x904;	/* pi_ac+0x904: PHY(0x339) outside of the carrier search: 0x0fff */
	struct bcm4360_phy_rssi rssi;

	struct bcm4360_phy_txpwr txpwr;
	struct bcm4360_phy_temp temp;
	struct bcm4360_phy_cal cal;
	struct bcm4360_phy_interference interference;
	struct bcm4360_phy_crsmincal crsmincal;
	struct bcm4360_phy_hirssi hirssi;
	struct bcm4360_phy_hwaci hwaci;
	struct bcm4360_phy_desense desense;

	/*
	 * Values of unknown purpose that attach sets. (Left out: the areas of
	 * unknown purpose that attach only clears, P6.30; see
	 * docs/re/questions/phy-attach.md.)
	 */
	u32 pi_0xc04;		/* pi+0xc04: 60 (P2.11); read by wlc_phy_watchdog */
	u32 pi_0xc08;		/* pi+0xc08: 16 (P2.11) */
	u8 pi_0xc2b;		/* pi+0xc2b: `phy_scraminit` (bcm), only written: 0xff (P6.3) */
	u8 pi_0xc36;		/* pi+0xc36: 0xff (procedure 5); older PHYs */
	u8 pi_0xed2;		/* pi+0xed2: 0xff (procedure 5) */
	u8 pi_0x224;		/* pi+0x224: `phyhang_avoid` (bcm), read by N-PHY code: 1 (P6.15) */
	u32 pi_0x1144[8];	/* pi+0x1144: 0xffffffff each (P2.21) */
	u8 pi_0x1165;		/* pi+0x1165: 0 (P6.31), only written */
	u8 acphy_0x000;		/* pi_ac+0x000: 1 (P6.3) */
	u8 acphy_0x671;		/* pi_ac+0x671: 1 (P6.30); wlc_phy_desense_aci_engine_acphy */
	u8 acphy_0x8e1;		/* pi_ac+0x8e1: 0 (P6.3) */
	u8 acphy_0x8e6;		/* pi_ac+0x8e6: 1 (P6.3); written by the channel look-up */
	u8 acphy_0x8e7;		/* pi_ac+0x8e7: 1 (P6.3); read by an iovar */
	u8 acphy_0x8e8;		/* pi_ac+0x8e8: 0 (P6.3) */

	/*
	 * State of the debug/iovar override functions of acphy-rxgain (sections
	 * 11 and 12); not on the init/channel/band path. Zero after attach.
	 */
	u8 lpf_hpc_ovr_active;				/* pi_ac+0x308 */
	u16 lpf_hpc_ovr_save[BCM4360_PHY_CORES_MAX][2];	/* pi_ac+0x30a */
	u16 dig_lpf_ovr_save[10];			/* pi+0xf6e */
};

/*
 * wlc_phy_shared_attach and the first wlc_phy_attach (2.4 GHz) in one.
 * NULL if it fails. The memory comes from hw_zalloc().
 */
struct bcm4360_phy *bcm4360_phy_attach(struct bcm4360_hw *hw,
				       const struct bcm4360_phy_board *board);
void bcm4360_phy_detach(struct bcm4360_phy *phy);		/* wlc_phy_detach */

/* acphy-attach.md, section 12 */
void bcm4360_phy_machwcap_set(struct bcm4360_phy *phy, u32 caps);
void bcm4360_phy_get_phyversion(struct bcm4360_phy *phy, u16 *phytype, u16 *phyrev,
				u16 *radioid, u16 *radiorev);
u32 bcm4360_phy_get_coreflags(struct bcm4360_phy *phy);
u32 bcm4360_phy_cap_get(struct bcm4360_phy *phy);
void bcm4360_phy_stf_chain_init(struct bcm4360_phy *phy, u8 txchain, u8 rxchain);

/* acphy-radio.md, sections 1, 3 and 4 */
void bcm4360_phy_anacore(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_switch_radio(struct bcm4360_phy *phy, bool on);

/* acphy-chanspec.md, sections 4 (the plain accessors) and 3 (wlc_phy_clk_bwbits, bw_state) */
void bcm4360_phy_chanspec_radio_set(struct bcm4360_phy *phy, u16 chanspec);
u16 bcm4360_phy_chanspec_get(struct bcm4360_phy *phy);
void bcm4360_phy_bw_state_set(struct bcm4360_phy *phy, u16 bw);
u16 bcm4360_phy_bw_state_get(struct bcm4360_phy *phy);
u32 bcm4360_phy_clk_bwbits(struct bcm4360_phy *phy);

/*
 * Three functions that only note something (they are not in a specification
 * yet; this is all they do): "the clock of the core is on, the registers of
 * the PHY may be accessed" (the flag that the specifications call `sh+0x31`),
 * "the driver is up" (`sh+0x30`), and "the chip went through a power-on
 * reset: the next initialisation loads the tables" (`pi+0x185` = 1).
 */
void bcm4360_phy_hw_clk_state_upd(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_hw_state_upd(struct bcm4360_phy *phy, bool up);
void bcm4360_phy_por_inform(struct bcm4360_phy *phy);

/*
 * For tests and debugging: element `index` of the value that the PHY keeps
 * for the variable `name` (0 for a variable that is not an array), after
 * the conversion of the specification. false: no such name or index.
 */
bool bcm4360_phy_get_var(const struct bcm4360_phy *phy, const char *name, u32 index,
			 s32 *value);

/*
 * Initialisation and channel set (open/phy/phy_init.c, open/phy/phy_chanspec.c).
 * The public entry points (names and arguments are the contract with the tests).
 */

/* acphy-init.md, section 1 (wlc_phy_init) */
void bcm4360_phy_init(struct bcm4360_phy *phy, u16 chanspec);

/* acphy-chanspec.md, section 4 (wlc_phy_chanspec_set) */
void bcm4360_phy_chanspec_set(struct bcm4360_phy *phy, u16 chanspec);

/* acphy-init.md, H13 (wlc_acphy_set_scramb_dyn_bw_en) */
void bcm4360_phy_set_scramb_dyn_bw_en(struct bcm4360_phy *phy, bool enable);

/* acphy-init.md, H12 (wlc_phy_ldpc_override_set -> wlc_phy_update_rxldpc_acphy) */
void bcm4360_phy_ldpc_override_set(struct bcm4360_phy *phy, bool ldpc);

/*
 * acphy-init.md, section 4 (sub_0a04c2, wlc_phy_set_regtbl_on_pwron_acphy):
 * release the overrides of the radio control, load the static tables. Called
 * by the initialisation and by acphy-radio.md, section 4, "On", step 6.
 */
void bcm4360_phy_set_regtbl_on_pwron_acphy(struct bcm4360_phy *phy);

/*
 * acphy-chanspec.md, section 5 (sub_0a7089, wlc_phy_chanspec_set_acphy): the
 * channel function. Called by the initialisation, by wlc_phy_chanspec_set and
 * by acphy-radio.md, section 4, "On", step 6.
 */
void bcm4360_phy_chanspec_set_acphy(struct bcm4360_phy *phy, u16 chanspec);

/*
 * Helpers of the initialisation that the channel function and later PHY code
 * reuse (open/phy/phy_init.c). The comment names the section of acphy-init.md.
 */
void bcm4360_phy_chanspec_shm_set(struct bcm4360_phy *phy, u16 chanspec);	/* section 2 */
void bcm4360_phy_set_reg_on_reset_acphy(struct bcm4360_phy *phy);		/* section 5 (sub_0a0be4) */
void bcm4360_phy_hirssi_set_ucode_params(struct bcm4360_phy *phy);		/* section 3a */
void bcm4360_phy_stay_in_carriersearch(struct bcm4360_phy *phy, bool enable);	/* H4 */
void bcm4360_phy_resetcca(struct bcm4360_phy *phy);				/* H5 */
void bcm4360_phy_rxcore_setstate(struct bcm4360_phy *phy, u8 rxmask);		/* H7 */
void bcm4360_phy_deaf(struct bcm4360_phy *phy, bool mode);			/* H9 */
bool bcm4360_phy_hirssi_shmem_read_clear(struct bcm4360_phy *phy);		/* H11 (sub_092500) */

/*
 * Helpers of the channel function that later PHY code reuses
 * (open/phy/phy_chanspec.c). The comment names the section of acphy-chanspec.md.
 */
u8 bcm4360_phy_get_chan_freq_range(struct bcm4360_phy *phy, u8 channel);	/* section 6 */
u32 bcm4360_phy_chanspec_bandrange_get(struct bcm4360_phy *phy, u16 chanspec);	/* section 7 */
u8 bcm4360_phy_get_rxgainerr(struct bcm4360_phy *phy, s16 *err);		/* section 8 */
/* section 9 (sub_08f086): split a wanted gain into the six stage codes; result: gain reached */
u8 bcm4360_phy_rxgainctrl_encode_gain(struct bcm4360_phy *phy, u8 core, u8 wanted,
				      bool include_tr, u8 *codes);

/*
 * Functions of later tasks that the code of this task calls. Until their
 * tasks are done they are defined in open/phy/phy_todo.c and do nothing.
 */

/*
 * acphy-attach.md, "Scope" (sub_0b56ce, wlc_phy_timercb_phycal): what the
 * timer "phycal" does; attach (procedure 2, step 15) only registers it.
 */
void bcm4360_phy_timer_phycal(struct bcm4360_phy *phy);

/* acphy-rxgain: front end, analog filters, reciprocity (leaves of the channel function) */
void bcm4360_phy_set_regtbl_femctrl_acphy(struct bcm4360_phy *phy);		/* sub_0a6b0f (INIT) */
void bcm4360_phy_set_regtbl_on_band_change_acphy(struct bcm4360_phy *phy);	/* sub_09e378 (BANDCHG) */
void bcm4360_phy_set_regtbl_on_bw_change_acphy(struct bcm4360_phy *phy);	/* sub_09eaf9 (BWCHG) */
void bcm4360_phy_set_regtbl_on_chan_change_acphy(struct bcm4360_phy *phy,
						 const u16 *entry);		/* sub_0a4adc */

/*
 * acphy-rxgain, sections 10 to 12: the receive-gain / LPF / dig-LPF debug
 * overrides (iovar path; not on the init/channel/band path, not in the traces).
 */
void bcm4360_phy_calc_extra_init_gain_acphy(struct bcm4360_phy *phy, u8 want, u8 *out);
void bcm4360_phy_rfctrl_override_rxgain_acphy(struct bcm4360_phy *phy, u8 mode,
					      const u8 *codes, u16 *save);
void bcm4360_phy_lpf_hpc_override_acphy(struct bcm4360_phy *phy, bool apply);
void bcm4360_phy_dig_lpf_override_acphy(struct bcm4360_phy *phy, u8 mode);

/* acphy-desense: receive gain control and desense (leaves of the channel function) */
void *bcm4360_phy_desense_getset_chanidx_acphy(struct bcm4360_phy *phy, u16 chanspec,
					       bool set);			/* sub_092efb */
void bcm4360_phy_desense_calc_total_acphy(struct bcm4360_phy *phy);		/* sub_0909fd */
void bcm4360_phy_rxgainctrl_set_gaintbls_acphy(struct bcm4360_phy *phy, bool init,
					       bool bandchg, bool bwchg);	/* sub_09af05 */
void bcm4360_phy_rxgainctrl_set_init_clip_gain_acphy(struct bcm4360_phy *phy);	/* sub_09a121 */
void bcm4360_phy_desense_apply_acphy(struct bcm4360_phy *phy, u8 flag);		/* sub_09a539 */
void bcm4360_phy_hwaci_setup_acphy(struct bcm4360_phy *phy, bool enable, bool init);
void bcm4360_phy_aci_w2nb_setup_acphy(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_desense_aci_reset_params_acphy(struct bcm4360_phy *phy, bool apply,
						bool a, bool b);

/*
 * acphy-desense: carrier-sense minimum-power calibration and the periodic
 * hwaci engine (open/phy/phy_desense.c). wlc_phy_crs_min_pwr_cal_acphy is
 * called by the channel-change set-up (sub_0a4adc, acphy-chanspec.md A1 step 9,
 * with restore = 1) and by the noise-sample path (restore = 0).
 */
void bcm4360_phy_crs_min_pwr_cal_acphy(struct bcm4360_phy *phy, u8 restore);	/* C1 */
void bcm4360_phy_noise_sample_request_crsmincal(struct bcm4360_phy *phy);	/* C4 */
void bcm4360_phy_ed_thres_acphy(struct bcm4360_phy *phy, s32 *val, bool set);	/* C5 */
void bcm4360_phy_hwaci_engine_acphy(struct bcm4360_phy *phy);			/* E1 */

/* acphy-txpower: transmit power (leaves of the channel function and of section 6) */
void bcm4360_phy_txpwr_by_index_acphy(struct bcm4360_phy *phy, u8 coremask, s8 index);
void bcm4360_phy_txpwrctrl_enable_acphy(struct bcm4360_phy *phy, bool on);
void bcm4360_phy_txpwrctrl_idle_tssi_meas_acphy(struct bcm4360_phy *phy);	/* sub_0affa9 */

/*
 * acphy-txpower: leaves called by the band-change function of acphy-rxgain
 * (sub_09e378, open/phy/phy_rxgain.c) - the TSSI radio-path override enable and
 * the transmit-calibration coefficient apply.
 */
void bcm4360_phy_tssi_phy_setup_acphy(struct bcm4360_phy *phy, u8 mode);		/* sub_08f9b4 */
/* sub_09c161: `set` = 14 bytes per core, or NULL for all zeros (the band change) */
void bcm4360_phy_txcal_coeffs_apply_acphy(struct bcm4360_phy *phy, const u8 *set);

/*
 * acphy-txpower: the closed-loop set-up and the pure/getter functions. These
 * are reached only through phy-cmn or the calibrations (not in the compared
 * PHY-API stages); they are provided for completeness and to keep the leaves
 * they call referenced.
 */
void bcm4360_phy_txpower_recalc_target_acphy(struct bcm4360_phy *phy);		/* sub_09949f */
void bcm4360_phy_txpower_core_offset_set_acphy(struct bcm4360_phy *phy,
					       const s8 *offsets);		/* sub_099528 */
u8 bcm4360_phy_tssivisible_thresh_acphy(struct bcm4360_phy *phy);		/* section 16 */
void bcm4360_phy_precal_txgain_acphy(struct bcm4360_phy *phy, u8 *records);	/* sub_0b1227 */

/*
 * Calibrations (open/phy/phy_cal.c): the public entry points the tests call
 * and the tone / sample player that the transmit-power idle measurement shares.
 * docs/re/spec/acphy-cal-tx.md, docs/re/spec/acphy-cal-rx.md.
 */

/* wlc_phy_cals_acphy(pi, phase): run or advance a calibration (acphy-cal-tx section 1) */
void bcm4360_phy_cals(struct bcm4360_phy *phy, u32 phase);

/* wlc_phy_tempsense_acphy: measure the die temperature in degC (acphy-cal-rx section 3) */
u32 bcm4360_phy_tempsense(struct bcm4360_phy *phy);

/* wlc_phy_tx_tone_acphy (acphy-cal-tx section 12); result 0, or nonzero on failure */
u32 bcm4360_phy_tx_tone_acphy(struct bcm4360_phy *phy, s32 freq, u16 amp,
			      u8 dont_deaf, u8 rfseq, u8 set_bbmult);
/* wlc_phy_stopplayback_acphy (acphy-cal-tx section 14) */
void bcm4360_phy_stopplayback_acphy(struct bcm4360_phy *phy);

#endif
