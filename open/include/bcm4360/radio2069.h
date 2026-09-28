/* SPDX-License-Identifier: ISC */
/*
 * The 2069 radio behind the AC-PHY of the BCM4360: power, preferred values,
 * RC calibrations, channel tuning.
 *
 * Specification: docs/re/spec/acphy-radio.md. The notation PHY(a), RADIO(a)
 * and mod() in the comments is the one of docs/re/spec/access.md.
 *
 * Scope: radio major revision 0, radio revisions 3 and 4, behind an AC-PHY of
 * revision 0 or 1. The names of the registers are not known and the purpose
 * of nearly all of them neither: registers are named by their address, fields
 * by their position, values by the procedure that writes them.
 */
#ifndef BCM4360_RADIO2069_H
#define BCM4360_RADIO2069_H

#include <bcm4360/types.h>
#include <bcm4360/access.h>

/* radio revisions (radio major revision 0) that the procedures distinguish */
#define R2069_REV_3			3
#define R2069_REV_4			4

/* PHY revision with radio accesses at the band change, section 15 */
#define ACPHY_REV_0			0

/*
 * Radio register addresses, specification "Radio register addresses": bits
 * 0..8 the register, bits 9..11 the bank. A per core register is named by its
 * address in core 0.
 */
#define R2069_BANK_SHIFT		9
#define R2069_BANK_CORE0		0x000
#define R2069_BANK_CORE1		0x200
#define R2069_BANK_CORE2		0x400	/* holds the shared blocks as well */
#define R2069_BANK_ALL			0x600	/* values that are the same for all cores */
#define R2069_BANK_COMMON		0x800	/* PLL and other common registers */

/* the third core: section 15 names its registers, whatever the number of cores is */
#define R2069_CORE2			2

/* per core registers */
#define R2069_REG_0x017			0x017
#define R2069_REG_0x01a			0x01a
#define R2069_REG_0x01f			0x01f
#define R2069_REG_0x043			0x043
#define R2069_REG_0x059			0x059
#define R2069_REG_0x05c			0x05c
#define R2069_REG_0x061			0x061
#define R2069_REG_0x065			0x065
#define R2069_REG_0x06f			0x06f
#define R2069_REG_0x11d			0x11d
#define R2069_REG_0x121			0x121
#define R2069_REG_0x122			0x122
#define R2069_REG_0x123			0x123
#define R2069_REG_0x126			0x126
#define R2069_REG_0x127			0x127
#define R2069_REG_0x135			0x135
#define R2069_REG_0x143			0x143
#define R2069_REG_0x144			0x144
#define R2069_REG_0x15f			0x15f
#define R2069_REG_0x170			0x170
#define R2069_REG_0x171			0x171

/* shared blocks in the bank of core 2: bandgap, RCAL, RCCAL */
#define R2069_REG_0x407			0x407
#define R2069_REG_0x40b			0x40b
#define R2069_REG_0x40c			0x40c
#define R2069_REG_0x410			0x410
#define R2069_REG_0x411			0x411
#define R2069_REG_0x412			0x412
#define R2069_REG_0x413			0x413
#define R2069_REG_0x414			0x414
#define R2069_REG_0x415			0x415
#define R2069_REG_0x416			0x416
#define R2069_REG_0x548			0x548
#define R2069_REG_0x549			0x549
#define R2069_REG_0x54a			0x54a
#define R2069_REG_0x54b			0x54b
#define R2069_REG_0x54c			0x54c
#define R2069_REG_0x55e			0x55e

/* bank of all cores */
#define R2069_REG_0x645			0x645

/* PLL and other common registers */
#define R2069_REG_0x892			0x892
#define R2069_REG_0x8c7			0x8c7
#define R2069_REG_0x8c8			0x8c8
#define R2069_REG_0x8c9			0x8c9
#define R2069_REG_0x8ca			0x8ca
#define R2069_REG_0x8cc			0x8cc
#define R2069_REG_0x8d0			0x8d0
#define R2069_REG_0x8d6			0x8d6
#define R2069_REG_0x8dc			0x8dc
#define R2069_REG_0x8e5			0x8e5
#define R2069_REG_0x8e8			0x8e8
#define R2069_REG_0x8ea			0x8ea
#define R2069_REG_0x8ec			0x8ec
#define R2069_REG_0x8ed			0x8ed
#define R2069_REG_0x90b			0x90b
#define R2069_REG_0x96b			0x96b
#define R2069_REG_0x96c			0x96c

/* RADIO(0x017 | core): section 16 */
#define R2069_0x017_BIT1		0x0002

/* RADIO(0x01a | core): section 16 */
#define R2069_0x01a_BIT2		0x0004
#define R2069_0x01a_BITS4_7		0x00f0
#define R2069_0x01a_BITS4_7_MODE0	0x0010	/* mode 0, both bands */
#define R2069_0x01a_BITS4_7_5G		0x0020	/* mode not 0, 5 GHz */
#define R2069_0x01a_BITS4_7_2G		0x0000	/* mode not 0, 2.4 GHz */
#define R2069_0x01a_BITS8_9		0x0300

/* RADIO(0x01f | core): section 16 */
#define R2069_0x01f_BIT2		0x0004	/* gets the mode */
#define R2069_0x01f_BIT2_SHIFT		2

/* RADIO(0x043 | core): section 7 */
#define R2069_0x043_BITS0_4		0x001f	/* result of pass 1 of the RCCAL */

/* RADIO(0x059 | core): section 15 */
#define R2069_0x059_BITS12_13		0x3000
#define R2069_0x059_BITS12_13_2G	0x1000	/* band change to 2.4 GHz */

/* RADIO(0x05c | core) and RADIO(0x061 | core): section 15 */
#define R2069_0x05c_BITS12_15		0xf000
#define R2069_0x05c_BITS12_15_5G	0x6000	/* band change to 5 GHz */
#define R2069_0x061_BITS12_15		0xf000
#define R2069_0x061_BITS12_15_5G	0x6000	/* band change to 5 GHz */

/* RADIO(0x065 | core) and RADIO(0x06f | core): section 5, step 8 */
#define R2069_0x065_BIT0		0x0001
#define R2069_0x06f_BIT0		0x0001
#define R2069_0x06f_BIT1		0x0002
#define R2069_0x06f_BIT2		0x0004

/* RADIO(0x11d | core): section 7, pass 2 */
#define R2069_0x11d_BIT2		0x0004

/* RADIO(0x121 | core), RADIO(0x122 | core), RADIO(0x144 | core): section 14 */
#define R2069_0x121_BIT12		0x1000	/* low for 100 us before the calibration */
#define R2069_0x122_BITS0_3		0x000f	/* set while the calibration runs */
#define R2069_0x144_DONE0		0x0001	/* with DONE1: the calibration is finished */
#define R2069_0x144_DONE1		0x0002

/* RADIO(0x123): section 11, steps 3 and 7 */
#define R2069_0x123_TUNE_REV3		0x03e9	/* radio revision 3, cores 1 and 2 */
#define R2069_0x123_TUNE_REV4		0x83e0	/* radio revision 4, bank of all cores */

/* RADIO(0x126 | core): sections 5 and 7 */
#define R2069_0x126_BITS0_4		0x001f	/* result of pass 1 of the RCCAL */
#define R2069_0x126_BITS8_9		0x0300
#define R2069_0x126_BITS8_9_PWRON	0x0100

/* RADIO(0x127 | core): section 5, step 8 */
#define R2069_0x127_BITS0_1		0x0003
#define R2069_0x127_BITS0_1_PWRON	0x0002

/* RADIO(0x15f | core): section 14, step 4 */
#define R2069_0x15f_BIT4		0x0010
#define R2069_0x15f_BIT5		0x0020

/* RADIO(0x170 | core): section 16 */
#define R2069_0x170_BIT8		0x0100

/* RADIO(0x171 | core): section 7 */
#define R2069_0x171_BIT13		0x2000	/* set while pass 2 of the RCCAL runs */

/* RADIO(0x407): section 5, step 7 */
#define R2069_0x407_BIT1		0x0002

/* RADIO(0x40b): the resistor calibration (RCAL), section 4 */
#define R2069_0x40b_BIT0		0x0001	/* low, then high: the calibration runs */
#define R2069_0x40b_DONE		0x0008	/* the calibration is finished */

/* RADIO(0x40c): section 5, step 9 */
#define R2069_0x40c_BIT4		0x0010

/* RADIO(0x410)..RADIO(0x416): the RC calibration (RCCAL), section 7 */
#define R2069_0x410_BIT0		0x0001	/* low, then high before a pass */
#define R2069_0x410_SC			0x0018	/* "sc" of the pass */
#define R2069_0x410_SC_SHIFT		3
#define R2069_0x410_SR			0x1000	/* "sr" of the pass */
#define R2069_0x410_SR_SHIFT		12
#define R2069_0x411_START		0x0001	/* starts a pass */
#define R2069_0x411_X1			0xff00	/* "x1" of the pass */
#define R2069_0x411_X1_SHIFT		8
#define R2069_0x413_DONE		0x0010	/* the pass is finished */
#define R2069_0x416_BITS0_4		0x001f	/* result of pass 1 */
#define R2069_0x416_BITS5_9		0x03e0	/* result of pass 2 */
#define R2069_0x416_BITS5_9_SHIFT	5

/* RADIO(0x548), RADIO(0x54b), RADIO(0x54c): sections 4 (RCAL) and 16 */
#define R2069_0x548_BIT0		0x0001
#define R2069_0x54b_BITS0_7		0x00ff
#define R2069_0x54b_BITS0_7_CORE1	0x0001	/* section 16: core 1 is set up */
#define R2069_0x54b_BITS8_15		0xff00
#define R2069_0x54b_BITS8_15_CORE0	0x0100	/* section 16: core 0 is set up */
#define R2069_0x54c_BITS8_15		0xff00
#define R2069_0x54c_BITS8_15_CORE2	0x0100	/* section 16: core 2 is set up */

/* RADIO(0x55e): section 5, step 7 */
#define R2069_0x55e_BIT4		0x0010

/* RADIO(0x645): section 11, step 5 */
#define R2069_0x645_BITS12_14		0x7000

/* RADIO(0x892): section 11, step 3 */
#define R2069_0x892_BITS8_15		0xff00
#define R2069_0x892_BITS8_15_REV3	0xa000

/* RADIO(0x8c7)..RADIO(0x8cc): the loop filter of the PLL, section 12 */
#define R2069_0x8c7_150KHZ		0xffff
#define R2069_0x8c8_150KHZ		0xffff
#define R2069_0x8c9_BITS0_7		0x00ff
#define R2069_0x8c9_BITS0_7_150KHZ	0x0002
#define R2069_0x8c9_BITS8_15		0xff00
#define R2069_0x8c9_BITS8_15_150KHZ	0x0000
#define R2069_0x8ca_150KHZ		0x0002
#define R2069_0x8cc_BITS0_7		0x00ff
#define R2069_0x8cc_BITS0_7_150KHZ	0x0002
#define R2069_0x8cc_BITS8_15		0xff00
#define R2069_0x8cc_BITS8_15_150KHZ	0xff00

/* RADIO(0x8d0), RADIO(0x8dc), RADIO(0x8e5), RADIO(0x8e8), RADIO(0x90b): section 13 */
#define R2069_0x8d0_BIT0		0x0001	/* low, then high: the VCO calibration runs */
#define R2069_0x8dc_BIT13		0x2000	/* low, then high, after the two others */
#define R2069_0x8e5_BIT14		0x4000	/* cleared before the VCO calibration */
#define R2069_0x8e8_BIT6		0x0040	/* low, then high, with RADIO(0x8d0) */
#define R2069_0x90b_DONE		0x0100	/* the VCO calibration is finished */

/* RADIO(0x8d6), RADIO(0x8ec): section 11, step 4 */
#define R2069_0x8d6_CHANNEL4		0x0ce4
#define R2069_0x8ec_BITS4_6		0x0070
#define R2069_0x8ec_BITS4_6_CHANNEL4	0x0050

/* RADIO(0x8ea): sections 4, 5, 7 and 14 */
#define R2069_0x8ea_BIT6		0x0040	/* set while the RCAL runs */
#define R2069_0x8ea_BIT7		0x0080	/* set while a calibration runs */
#define R2069_0x8ea_BIT8		0x0100	/* set if bit 1 of the boardflags is set */

/* RADIO(0x8ed): sections 4 and 7 */
#define R2069_0x8ed_BITS9_10		0x0600
#define R2069_0x8ed_BITS9_10_RCAL	0x0000
#define R2069_0x8ed_BITS9_10_RCCAL	0x0400
#define R2069_0x8ed_BITS11_12		0x1800
#define R2069_0x8ed_BITS11_12_RCAL	0x0000

/* RADIO(0x96b), RADIO(0x96c): section 5, step 6 */
#define R2069_0x96b_BIT2		0x0004
#define R2069_0x96b_BIT11		0x0800
#define R2069_0x96b_BIT12		0x1000
#define R2069_0x96b_BIT14		0x4000
#define R2069_0x96b_BIT15		0x8000
#define R2069_0x96c_BIT11		0x0800

/*
 * PHY registers that drive the power-up signals of the radio. The registers
 * 0x600..0x7ff belong to core 0, those of core 1 and 2 are 0x200 and 0x400
 * higher. Bit 12 of the address is set in some writes (unverified: the write
 * then goes to all cores).
 */
#define ACPHY_CORE_STEP			0x200
#define ACPHY_ALL_CORES			0x1000

#define ACPHY_REG_0x16b			0x16b
#define ACPHY_REG_0x175			0x175
#define ACPHY_REG_0x408			0x408
#define ACPHY_REG_0x40c			0x40c
#define ACPHY_REG_0x40e			0x40e
#define ACPHY_REG_0x415			0x415
#define ACPHY_REG_0x416			0x416
#define ACPHY_REG_0x417			0x417
#define ACPHY_REG_0x720			0x720
#define ACPHY_REG_0x721			0x721
#define ACPHY_REG_0x725			0x725
#define ACPHY_REG_0x728			0x728
#define ACPHY_REG_0x729			0x729
#define ACPHY_REG_0x739			0x739
#define ACPHY_REG_0x73a			0x73a
#define ACPHY_REG_0x73e			0x73e

/* PHY(0x16b), PHY(0x175): section 4, "On", step 3 */
#define ACPHY_0x16b_BIT10		0x0400
#define ACPHY_0x175_RADIO_ON		0x0000

/* PHY(0x408): sections 4 and 5 */
#define ACPHY_0x408_BIT0		0x0001	/* pulsed for 1 us: section 5, step 3 */
#define ACPHY_0x408_BIT1		0x0002	/* set at power-on, cleared by radio off */
#define ACPHY_0x408_BIT2		0x0004	/* set for 100 us: section 5, step 9 */
#define ACPHY_0x408_PWRON_KEEP		0xfc38	/* what the power-on sequence keeps */

/* PHY(0x40c), PHY(0x40e), PHY(0x415): section 5, step 2 */
#define ACPHY_0x40c_PWRON		0x2000
#define ACPHY_0x40e_PWRON		0x0000
#define ACPHY_0x415_PWRON		0x0000

/* PHY(0x416), PHY(0x417): sections 4 and 5 */
#define ACPHY_0x416_PWRON		0x000d
#define ACPHY_0x416_RADIO_OFF		0x0001
#define ACPHY_0x417_RESET		0x0000	/* section 5, step 2 */
#define ACPHY_0x417_PWRON_FIRST		0x000d	/* section 5, step 10, for 100 us */
#define ACPHY_0x417_PWRON_LAST		0x0004	/* section 5, step 10, at the end */
#define ACPHY_0x417_RADIO_OFF		0x0000

/* PHY(0x720), PHY(0x728) of core 0: section 5 */
#define ACPHY_0x720_BITS7_8		0x0180
#define ACPHY_0x728_BIT7		0x0080
#define ACPHY_0x728_BIT8		0x0100
#define ACPHY_0x728_BIT15		0x8000

/* PHY(0x725 + core), PHY(0x739 + core): section 14 */
#define ACPHY_0x725_BIT2		0x0004
#define ACPHY_0x739_BIT7		0x0080

/* what radio off writes, to all cores: section 4, "Off", step 2 */
#define ACPHY_0x720_RADIO_OFF		0x03ff
#define ACPHY_0x721_RADIO_OFF		0xffff
#define ACPHY_0x725_RADIO_OFF		0x1fff
#define ACPHY_0x728_RADIO_OFF		0x0000
#define ACPHY_0x729_RADIO_OFF		0x0000
#define ACPHY_0x739_RADIO_OFF		0x0000
#define ACPHY_0x73a_RADIO_OFF		0x0000
#define ACPHY_0x73e_RADIO_OFF		0x1c00

/* bit 1 of the boardflags: section 5, step 5 */
#define BCM4360_BOARDFLAGS_BIT1		0x00000002

/*
 * An entry of the channel tables, specification "Tables": 58 words of 16 bit.
 */
#define R2069_CHAN_WORDS		58
#define R2069_CHAN_NUMBER		0	/* channel number, the search key */
#define R2069_CHAN_FREQ			1	/* centre frequency in MHz */
#define R2069_CHAN_RADIO		2	/* first of the words for radio registers */
#define R2069_CHAN_RADIO_WORDS		50
#define R2069_CHAN_PHY			52	/* first of the words for PHY(0x371).. */
#define R2069_CHAN_PHY_WORDS		6

/*
 * Tables of the radio. They are Broadcom's data and are not part of the open
 * code: the caller provides them (in the tests they are handed over from the
 * object's memory, a driver loads them as firmware).
 */
struct bcm4360_radio_tables {
	const u16 *prefregs;	/* pairs {address, value}, see section 6; may be NULL */
	const u16 *chan_tuning;	/* entries of 58 words, see "Tables" */
	u32 chan_entries;	/* number of entries */
};

struct bcm4360_radio {
	struct bcm4360_phy_io *io;
	struct bcm4360_radio_tables tbl;
	u8 cores;		/* number of cores (chains) of the PHY */
	u8 rev;			/* radio revision (3 or 4) */
	u8 phy_rev;		/* PHY revision (0 or 1) */
	u32 boardflags;
	bool skip_rcal;		/* boardflags3 bit 3 or bit 13 */
	bool bf_bit29;		/* boardflags bit 29 */
	/* results of the RC calibration, section 7 */
	u8 rccal_gmult;
	u8 rccal_gmult_rc;
	u8 rccal_dacbuf;
	/* you may add fields below this line */
};

/* set the defaults of the calibration results; the other fields are the caller's */
void bcm4360_radio_init(struct bcm4360_radio *r);

void bcm4360_radio_off(struct bcm4360_radio *r);		/* section 4, "Off", steps 2..4 */
void bcm4360_radio_pwron_seq(struct bcm4360_radio *r);		/* section 5 */
void bcm4360_radio_rcal(struct bcm4360_radio *r);		/* section 4, "On", steps 3 and 4 */
void bcm4360_radio_rccal(struct bcm4360_radio *r);		/* section 7 */

/* section 10: entry of a channel, NULL if there is none; *freq = MHz */
const u16 *bcm4360_radio_chan_entry(struct bcm4360_radio *r, u8 channel, u16 *freq);

/*
 * section 11, steps 2 to 8: write the entry and the fixed patches, the loop
 * filter on 5 GHz, start the VCO calibration. `channel` is the low byte of
 * the chanspec.
 */
void bcm4360_radio_tune(struct bcm4360_radio *r, const u16 *entry, u8 channel,
			bool is_5g);
void bcm4360_radio_rfpll_150khz(struct bcm4360_radio *r);	/* section 12 */
void bcm4360_radio_vcocal(struct bcm4360_radio *r);		/* section 13, start */
void bcm4360_radio_vcocal_wait(struct bcm4360_radio *r, bool settle);	/* section 13, wait */
void bcm4360_radio_afecal(struct bcm4360_radio *r);		/* section 14 */
/* section 15: the radio part of the band change */
void bcm4360_radio_band_change(struct bcm4360_radio *r, bool is_5g);
/* section 16 */
void bcm4360_radio_tssi_setup(struct bcm4360_radio *r, u8 coremask, u8 mode,
			      bool is_5g);

#endif
