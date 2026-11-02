// SPDX-License-Identifier: ISC
/*
 * The initialisation of the 802.11 core (wlc_bmac.c): wlc_bmac_init with the
 * microcode download and start, the register initialisation lists, the
 * buffer memory controller, the band initialisation, the frequency of the MAC
 * clock, the OFDM rate table of the microcode, the slot time and two helpers
 * of the Bluetooth coexistence.
 *
 * Written from docs/re/spec/bmac-init.md, sections B1-B3, B6-B10, B12, E6 and
 * of E10 sub_06106b and sub_062b79, with the notation of "Data" and the shared
 * memory map of section G, for MAC core revision 42 with an AC-PHY (chip
 * 0x4360 revision 3, PCIe, with a PMU). Broadcom's data (the microcode, the
 * initialisation lists, the table btc_ucode_flags) comes from hw_fw_data().
 * Everything outside the 802.11 core's registers goes through the services of
 * bcm4360/mac_env.h. A service may change struct bcm4360_mac, so no field of
 * *mac is kept in a local variable across a service call.
 */
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>
#include <bcm4360/access.h>
#include <bcm4360/utils.h>

/* Direct 802.11-core registers used here (docs/re/spec/bmac-init.md). */
#define D11_REG_0x24		0x024	/* interrupt mask of DMA channel 0 (B1 step 23) */
#define D11_RCV_LAZY0		0x100	/* receive interrupt control of FIFO 0 (B1 step 21) */
#define D11_MACINTSTATUS	0x128	/* MAC interrupt status register */
#define D11_MACHWCAP		0x15c	/* MAC capabilities (B7 step 1) */
#define D11_REG_0x188		0x188	/* beacon period (B1 step 23) */
#define D11_REG_0x18c		0x18c	/* start of the contention free period (B1 step 23) */
#define D11_CLK_CTL_ST		0x1e0	/* clock control of the MAC core (A13) */
#define D11_REG_0x49c		0x49c	/* antenna selection type 1 sets 0x3000 (B1 step 13) */
#define D11_REG_0x49e		0x49e	/* likewise; the coexistence GPIO bits (E10) */
#define D11_REG_0x520		0x520	/* buffer memory controller (B7) */
#define D11_REG_0x530		0x530	/* B7 step 5: command, 0 when done */
#define D11_REG_0x532		0x532	/* B7 step 5: number of buffers */
#define D11_REG_0x534		0x534	/* B7 step 5: first buffer */
#define D11_REG_0x536		0x536	/* B7 step 5: last buffer */
#define D11_REG_0x540		0x540	/* B7 step 3: control, bit 0 busy */
#define D11_REG_0x542		0x542	/* B7 step 3: total of buffers */
#define D11_REG_0x548		0x548	/* B7 step 4: command */
#define D11_REG_0x54a		0x54a	/* B7 step 4: maximum of buffers */
#define D11_REG_0x54c		0x54c	/* B7 step 4: minimum of buffers */
#define D11_REG_0x54e		0x54e	/* B7 step 4 */
#define D11_REG_0x550		0x550	/* B7 step 4 */
#define D11_MACFREQ_LO		0x62e	/* frequency word of the MAC clock, bits 0..15 (B9) */
#define D11_MACFREQ_HI		0x630	/* ... bits 16..31 (B9) */
#define D11_IFS_SLOT		0x684	/* slot timing (B10 step 8) */
#define D11_REG_0x688		0x688	/* B1 step 29: the upper 4 bits cleared */
#define D11_IFS_AIFSN		0x69c	/* minimum AIFSN (B1 step 29) */
#define D11_FASTPWRUP_DLY	0x6a8	/* fast clock power-up delay (B1 step 25) */

/* Bits of the MAC control register D11(0x120) ("Data"; B1 steps 6, 11, 22; B3). */
#define MCTL_PSM_RUN		0x00000002	/* microcode processor runs */
#define MCTL_PSM_JMP_0		0x00000004	/* microcode processor held at address 0 */
#define MCTL_IHR_EN		0x00000400	/* access to the internal registers enabled */
#define MCTL_GPOUT_SEL		0x0000c000	/* bits 14..15: GPIO outputs selected by the microcode */
#define MCTL_INFRA		0x00020000	/* infrastructure mode */
#define MCTL_AP			0x00040000	/* access point mode */
#define MCTL_WAKE		0x04000000	/* microcode must stay awake */
#define MCTL_DISCARD_PMQ	0x40000000
#define MCTL_ALL		0xffffffff	/* mask: every bit of the copy */

/* Bits of the MAC interrupt status register D11(0x128) (B1 step 23, B3). */
#define MI_MACSSPNDD		0x00000001	/* "MAC suspended" */
#define MI_BIT14		0x00004000	/* acknowledged by B1 step 23 */
#define MI_ALL			0xffffffff

/* Values of B1 step 23. */
#define D11_0x188_DEFAULT	0x80000000	/* beacon period: default */
#define D11_0x18c_DEFAULT	0x02000000	/* start of the contention free period: default */
#define DMA0_INTMASK_RX		0x00010000	/* receive interrupt of DMA channel 0 */

/* B1 step 29 */
#define IFS_0x688_KEEP		0x0fff
#define IFS_AIFSN_MIN		1

/* A reason to keep the microcode awake: "MAC suspended" (B1 step 39, "Data"). */
#define WAKE_REASON_MAC_SUSPENDED	0x04

/* Object memories (sections A8, B2, B4, F). */
#define OBJ_SEL_UCODE		(D11_OBJ_MEM_UCODE << D11_OBJ_MEM_SHIFT)	/* 0x00000 */
#define OBJ_SEL_SHM		(D11_OBJ_MEM_SHM << D11_OBJ_MEM_SHIFT)		/* 0x10000 */
#define OBJ_SEL_SCRATCH		(D11_OBJ_MEM_SCRATCH << D11_OBJ_MEM_SHIFT)	/* 0x20000 */
#define OBJ_ADDR_AUTOINC	0x03000000	/* bits 24, 25: the address advances after a write */
#define OBJ_HALF_BYTES		2
#define OBJ_WORD_BYTES		4

/* Scratch registers of the microcode: register n at byte offset 4n (section G). */
#define OBJ_REG_OFFSET(n)	((u32)(n) << 2)
#define SCR_SRL			6	/* short retry limit (B1 step 27) */
#define SCR_LRL			7	/* long retry limit (B1 step 27) */
#define SCR_0x9			9	/* saved and restored around the list (B1 steps 16, 27) */
#define SCR_0x9_BYTES		4

/* Shared memory words (section G). */
#define SHM_SLOT_TIME		0x10	/* slot duration for the microcode */
#define SHM_MAC_REV		0x16	/* MAC revision */
#define SHM_TXANT_CTL		0x22	/* antenna / coexistence control word (B12 step 2) */
#define SHM_SFBL		0x44	/* short fallback limit */
#define SHM_LFBL		0x46	/* long fallback limit */
#define SHM_PHY_REV		0x50
#define SHM_PHY_TYPE		0x52
#define SHM_ANT_DIV_CNT		0x5c	/* antenna diversity count */
#define SHM_EXTLNA_PWRSAVE	0x64	/* external LNA power save value */
#define SHM_BURST_LIMIT		0x80	/* frame burst limit */
#define SHM_0x8e		0x8e	/* read by B1 step 33 (used by MAC revision 33 only) */
#define SHM_BTC_BASE_HALF	0x92	/* half the base of the coexistence parameters */
#define SHM_SYNTHPU_DLY		0x94	/* synthesizer power-up delay */
#define SHM_MACHWCAP_LO		0xc0	/* MAC capabilities, low half */
#define SHM_MACHWCAP_HI		0xc2	/* MAC capabilities, high half */
#define SHM_RATE_DIR		0x1c0	/* directory of the OFDM rate table */
#define SHM_RATE_PCTL		0x12	/* B12: the PHY control word within a rate's entry */
#define SHM_ETHERADDR		0x78c	/* the interface's MAC address, three words */

/* B1 step 20, B1 step 13, B10 steps 8 and 15 */
#define BURST_LIMIT		8
#define ANT_DIV_CNT		10
#define ANTSEL1_SHM_0xc2	6
#define SLOT_SHORT_SHM		9	/* microseconds */
#define SLOT_LONG_SHM		20
#define IFS_SLOT_SHORT		0x0207
#define IFS_SLOT_LONG		0x0212
#define EXTLNA_PWRSAVE		0x0480

/* B12 step 2 */
#define TXANT_CTL_KEEP		0xfc3f
#define TXANT_CTL_BTC_BFL2	0x0080	/* with boardflags2 bit 23 */
#define TXANT_CTL_BTC		0x0040	/* without it */

/* Host flag words: the `idx` of bcm4360_bmac_mhf (A9). */
#define MHF1			0	/* SHM(0x5e) */
#define MHF2			1	/* SHM(0x60) */
#define MHF3			2	/* SHM(0x62) */
#define MHF4			3	/* SHM(0x78) */
#define MHF5			4	/* SHM(0xd4) */
#define MHF_COUNT		5

/* The `bands` argument of bcm4360_bmac_mhf (A9 step 1). */
#define MHF_BANDS_AUTO		0	/* the current band */
#define MHF_BANDS_2G		2	/* the 2.4 GHz band state */
#define MHF_BANDS_ALL		3	/* both band states */

/* Host flag bits written here (B1 step 13, B10 step 12, E10). */
#define MHF2_0x0200		0x0200
#define MHF3_0x0001		0x0001
#define MHF3_0x0002		0x0002
#define MHF3_0x1000		0x1000
#define MHF3_0x1504		0x1504
#define MHF3_BTC_KEEP		0xfdf7	/* E10: bits of the table taken into the 0x1504 field */
#define MHF3_BTC_ONLY_0x1000	0xedf7	/* E10: no other table bit: the 0x1000 call follows */
#define MHF5_0x0001		0x0001
#define MHF5_0x0002		0x0002	/* with coexistence flag bit 7 */
#define MHF5_0x0004		0x0004	/* with coexistence flag bit 8 */
#define MHF5_0x0006		(MHF5_0x0002 | MHF5_0x0004)
#define MHF5_0x0008		0x0008

/* Board flags (wlc_hw+0x8c, +0x90) tested here. */
#define BFL_0x1			0x00000001	/* B1 step 8: coexistence hardware */
#define BFL_0x2			0x00000002	/* B1 step 13: GPIO 0x200 */
#define BFL2_0x80		0x00000080	/* B1 step 8 */
#define BFL2_BIT23		BIT(23)		/* B12 step 2 */

/* B1 step 13 */
#define GPIO_ANTSEL1		0x3000	/* GPIO bits of antenna selection type 1 */
#define GPIO_BFL_0x2		0x0200	/* GPIO bit with boardflags bit 1 */

/* Capabilities (B1 step 8; E10). */
#define CC_CAP_PMU		BIT(28)		/* ChipCommon capabilities: the chip has a PMU */
#define CC_CAP_ECI		BIT(29)		/* ChipCommon capabilities: ECI */
#define CC_CAPEXT_SECI		BIT(0)		/* extended capabilities: SECI */
#define CC_CAPEXT_GCI		BIT(2)		/* extended capabilities: GCI */
#define MCAP_BTCX		BIT(29)		/* MAC capabilities: coexistence */
#define SECI_MODE_1		1
#define SECI_MODE_3		3

/* The coexistence flags word (btc+0x08, E10 sub_062b79). */
#define BTC_FLAG_0		BIT(0)
#define BTC_FLAG_TBL_FIRST	1	/* bits 1..6 select entries of btc_ucode_flags */
#define BTC_FLAG_TBL_LAST	6
#define BTC_FLAGS_TBL		0x007e	/* the bits 1..6 */
#define BTC_FLAG_7		BIT(7)
#define BTC_FLAG_8		BIT(8)
#define BTC_TBL_BASE		6	/* byte offset of the word of flag bit 1 */
#define BTC_TBL_STRIDE		4	/* bytes between the words of two flag bits */
#define BTC_PARAMS_COUNT	119	/* variables btc_params0 .. btc_params118 */

/* Chip-specific coexistence parameters of B1 step 32.2 (chips 0x4352, 0xa8dc). */
#define BTC_PARAM_0x02		0x02
#define BTC_PARAM_0x10		0x10
#define BTC_PARAM_0x12		0x12
#define BTC_PARAM_0x2c		0x2c

/* B1 step 35: the reason passed to wlc_bmac_mute. */
#define MUTE_REASON_INIT	1

/* PHY types ("Data"; B2, B8, B12). */
#define PHY_TYPE_N		4
#define PHY_TYPE_6		6
#define PHY_TYPE_HT		7
#define PHY_TYPE_8		8
#define PHY_TYPE_10		10
#define PHY_TYPE_AC		11

/* Band types (band state +0x00; B10, E6). */
#define BAND_TYPE_5G		1
#define BAND_TYPE_2G		2

/* The chanspec's band bits: 0xc000 is 5 GHz (B1 step 4). */
#define CHSPEC_BAND_MASK	0xc000
#define CHSPEC_BAND_5G		0xc000

/* MAC core revisions. */
#define MAC_REV_40		40	/* address match table, buffer memory controller */
#define MAC_REV_42		42	/* this card */

/* Chips (B1, B8, B9, E10). */
#define CHIP_ID_4331		0x4331
#define CHIP_ID_4350		0x4350
#define CHIP_ID_4352		0x4352
#define CHIP_ID_4360		0x4360
#define CHIP_ID_A8DC		0xa8dc
#define CHIP_ID_A9C4		0xa9c4
#define CHIP_ID_AA06		0xaa06

/* Board vendor and board types of E10 sub_062b79. */
#define BOARDVENDOR_APPLE	0x106b
#define BOARDTYPE_0x10F		0x10f
#define BOARDTYPE_0xEF		0xef
#define BOARDTYPE_0xF4		0xf4
#define BOARDTYPE_0x10E		0x10e

/* B7, the buffer memory controller of MAC revision 42. */
#define BMC_TOTAL_SHIFT		1	/* total = (cap >> 1) & 0xffc */
#define BMC_TOTAL_MASK		0xffc
#define BMC_BASE		0x2a	/* buffers of the template memory (revisions 43, 45..47: 0x7a) */
#define BMC_CTL_INIT		5	/* D11(0x540) */
#define BMC_CTL_BUSY		0x1
#define BMC_TPL_FIFO		7	/* the template memory */
#define BMC_NR_TX_FIFOS		6	/* the transmit FIFOs 0..5 */
#define BMC_FIFO_MINBUFS	0x20	/* minimum of buffers of every FIFO at revision 42 */
#define BMC_FIFO_0x520		0x0b	/* D11(0x520) of a FIFO (revisions 41, 44..47: 6) */
#define BMC_0x54e_LOW_GAP	4	/* D11(0x54e) = ((n - 4) << 8) | n */
#define BMC_0x550		0x740c
#define BMC_CMD_FIFO		0x10	/* D11(0x548) = 0x10 | f */
#define BMC_TPL_CMD		0x8007	/* D11(0x530) = 0x8007 | (i << 4) */
#define BMC_TPL_IDX_SHIFT	4
#define BMC_TPL_SPAN		2	/* the last buffer of a step: min(i + 2, base - 1) */
#define BMC_ALL_BITS		0xffff	/* B7 step 5: the whole value is compared with 0 */

/* B8: synthesizer power-up delay, microseconds. */
#define SYNTHPU_DLY_AC		512
#define SYNTHPU_DLY_4350	1200

/* B9: floor(0x3a980000000 / vco), vco from si_pmu_get_bb_vcofreq(40). */
#define MACFREQ_XTAL_MHZ	40
#define MACFREQ_NUMERATOR	0x000003a980000000ULL
#define MACFREQ_HALF_SHIFT	16
#define MACFREQ_HALF_MASK	0xffff

/* The transmit FIFOs with a DMA engine and the receive FIFO (B1 step 30). */
#define NR_TX_FIFOS		6
#define RX_FIFO			0

/* Number of entries of the address match table (B1 step 14). */
#define AMT_ENTRIES		64
#define ETHER_ADDR_LEN		6

/* Modes of wlc_bmac_clkctl_clk (A13): 0 = fast, 2 = dynamic. */
#define CLK_FAST		0
#define CLK_DYNAMIC		2

/* Bits of D11(0x1e0) (A13). */
#define CLK_CTL_FORCE_FAST	0x00000002
#define CLK_CTL_0x12		0x00000012
#define CLK_CTL_0x20000		0x00020000

/* Delays and waits, microseconds ("Data", notation; A13, B3, B7). */
#define SPINWAIT_STEP_US	10
#define CLK_FAST_WAIT_US	20000U
#define CLK_FAST_DELAY_US	64
#define UCODE_START_WAIT_US	1000000U
#define BMC_WAIT_US		200U

/* The host flag words in the shared memory, by `idx` (A9; B10 step 3). */
static const u16 mhf_shm_offset[MHF_COUNT] = {
	0x5e,	/* host flags 1 */
	0x60,	/* host flags 2 */
	0x62,	/* host flags 3 */
	0x78,	/* host flags 4 */
	0xd4,	/* host flags 5 */
};

/*
 * B12 step 1: the rate codes of the PLCP header of the OFDM rates 6, 9, 12,
 * 18, 24, 36, 48 and 54 Mbit/s, in this order.
 */
static const u8 ofdm_rate_codes[] = { 11, 15, 10, 14, 9, 13, 8, 12 };

/* B1 step 14: the address the address match table is cleared with. */
static const u8 ether_null[ETHER_ADDR_LEN];

/* docs/re/spec/bmac-init.md, B2/B6/E10: a 16-bit word from two bytes, low byte first */
static u16 bytes_le16(const u8 *p)
{
	return (u16)(p[0] | p[1] << 8);
}

/* docs/re/spec/bmac-init.md, B2: a 32-bit word from four bytes, low byte first */
static u32 bytes_le32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/*
 * docs/re/spec/bmac-init.md, A8 (wlc_bmac_read_shm): one word of the shared
 * memory, through the worker of A8 (copyfrom_objmem of 2 bytes, B4).
 */
static u16 read_shm(struct bcm4360_mac *mac, u32 offset)
{
	u8 b[OBJ_HALF_BYTES];

	bcm4360_bmac_copyfrom_objmem(mac, offset, b, OBJ_HALF_BYTES, OBJ_SEL_SHM);
	return bytes_le16(b);
}

/*
 * docs/re/spec/bmac-init.md, A8 (wlc_bmac_write_shm): one word of the shared
 * memory, through the worker of A8 (set_shm of one word).
 */
static void write_shm(struct bcm4360_mac *mac, u32 offset, u16 v)
{
	bcm4360_bmac_set_shm(mac, offset, v, OBJ_HALF_BYTES);
}

/*
 * docs/re/spec/bmac-init.md, B1 step 27: a 16-bit value to scratch register
 * `n` of the microcode (copyto_objmem of 2 bytes, low byte first).
 */
static void scratch_write(struct bcm4360_mac *mac, u32 n, u16 v)
{
	u8 b[OBJ_HALF_BYTES];

	b[0] = v & 0xff;
	b[1] = v >> 8;
	bcm4360_bmac_copyto_objmem(mac, OBJ_REG_OFFSET(n), b, OBJ_HALF_BYTES, OBJ_SEL_SCRATCH);
}

/* docs/re/spec/bmac-init.md, B10 step 8, B12, E6: the current band is 5 GHz */
static bool band_is_5g(struct bcm4360_mac *mac)
{
	return mac->band_type == BAND_TYPE_5G;
}

/* docs/re/spec/bmac-init.md, B2, B6, B10 step 4: MAC revision 42 with the AC-PHY */
static bool rev42_acphy(struct bcm4360_mac *mac)
{
	return mac->mac_rev == MAC_REV_42 && mac->band_phytype == PHY_TYPE_AC;
}

/*
 * docs/re/spec/bmac-init.md, A13 step 2 (and step 3 at PMU revision 0):
 * SPINWAIT((D11(0x1e0) & 0x20000) == 0, 20000).
 */
static void clk_ctl_wait_0x20000(struct bcm4360_mac *mac)
{
	u32 waited = 0;

	while (!(d11_read32(mac->hw, D11_CLK_CTL_ST) & CLK_CTL_0x20000) &&
	       waited < CLK_FAST_WAIT_US) {
		hw_udelay(SPINWAIT_STEP_US);
		waited += SPINWAIT_STEP_US;
	}
}

/*
 * docs/re/spec/bmac-init.md, A13 (sub_064887, wlc_bmac_clkctl_clk), called by
 * B1 steps 1 and 40: force the fast clock of the MAC core (mode 0) or return
 * to the dynamic clock. A file-local copy of the static helper of bmac_clk.c
 * (see docs/re/questions/bmac-coreinit.md).
 */
static void wlc_bmac_clkctl_clk(struct bcm4360_mac *mac, u32 mode)
{
	u32 v;

	if (!(mac->cccaps & CC_CAP_PMU)) {
		/* without a PMU: si_clkctl_cc and the wake override; not described */
		return;
	}

	/* step 1: nothing to do on the registers without the core's clock */
	if (mac->clk) {
		if (mode == CLK_FAST) {
			/* step 2 */
			v = d11_read32(mac->hw, D11_CLK_CTL_ST);
			d11_write32(mac->hw, D11_CLK_CTL_ST, v | CLK_CTL_FORCE_FAST);
			hw_udelay(CLK_FAST_DELAY_US);
			clk_ctl_wait_0x20000(mac);
		} else {
			/* step 3: the wait only at PMU revision 0 (not this card) */
			if (mac->pmurev == 0 &&
			    (d11_read32(mac->hw, D11_CLK_CTL_ST) & CLK_CTL_0x12))
				clk_ctl_wait_0x20000(mac);
			v = d11_read32(mac->hw, D11_CLK_CTL_ST);
			d11_write32(mac->hw, D11_CLK_CTL_ST, v & ~CLK_CTL_FORCE_FAST);
		}
	}

	/* step 4 */
	mac->forcefastclk = (mode == CLK_FAST);
}

/*
 * docs/re/spec/bmac-init.md, B2, sub_060744 (wlc_ucode_write): the image in
 * 32-bit words to the microcode memory, from address 0 with the address
 * advancing by itself.
 */
static void wlc_ucode_write(struct bcm4360_mac *mac, const u8 *image, u32 len)
{
	u32 i;

	/* step 3.1 (the static variable ucode_chunk is always 0) */
	d11_write32(mac->hw, D11_OBJ_ADDR, OBJ_SEL_UCODE | OBJ_ADDR_AUTOINC);
	(void)d11_read32(mac->hw, D11_OBJ_ADDR);

	/* step 3.2: words 0 .. len/4 - 1 */
	for (i = 0; i < len / OBJ_WORD_BYTES; i++)
		d11_write32(mac->hw, D11_OBJ_DATA_LO, bytes_le32(image + i * OBJ_WORD_BYTES));
}

/*
 * docs/re/spec/bmac-init.md, B2, sub_0607b0 (wlc_ucode_download): load the
 * microcode once; no check afterwards.
 */
static void wlc_ucode_download(struct bcm4360_mac *mac)
{
	const void *image = NULL;
	u32 size = 0;

	/* step 1 */
	if (mac->ucode_loaded)
		return;

	/* step 2: revision 42 with the AC-PHY: d11ucode42; another PHY type: no
	 * image (revision 43: d11ucode43; 41, 44..47: d11ucode41; 40: d11ucode40;
	 * older revisions: other images; not described) */
	if (rev42_acphy(mac))
		image = hw_fw_data(mac->hw, "d11ucode42", &size);

	/* step 3 */
	if (image)
		wlc_ucode_write(mac, image, size);

	/* step 4: also when no image was found */
	mac->ucode_loaded = 1;
}

/*
 * docs/re/spec/bmac-init.md, B3 (the start sequence at the symbol
 * wlc_bmac_wowlucode_start): start the microcode processor at address 0 and
 * wait until the microcode has suspended itself. Result 0, or -1 if it did
 * not report itself. The status bit is not cleared here.
 */
s32 bcm4360_bmac_wowlucode_start(struct bcm4360_mac *mac)
{
	u32 waited = 0;

	/* step 1 */
	d11_write32(mac->hw, D11_MACINTSTATUS, MI_ALL);

	/* step 2: PSM_JMP_0 removed, PSM_RUN set */
	bcm4360_bmac_mctrl(mac, MCTL_ALL, MCTL_WAKE | MCTL_INFRA | MCTL_IHR_EN | MCTL_PSM_RUN);

	/* step 3 */
	while (!(d11_read32(mac->hw, D11_MACINTSTATUS) & MI_MACSSPNDD) &&
	       waited < UCODE_START_WAIT_US) {
		hw_udelay(SPINWAIT_STEP_US);
		waited += SPINWAIT_STEP_US;
	}

	/* step 4 */
	return (d11_read32(mac->hw, D11_MACINTSTATUS) & MI_MACSSPNDD) ? 0 : -1;
}

/*
 * docs/re/spec/bmac-init.md, B6 (sub_060f67 through wlc_bmac_write_inits):
 * run the initialisation list `name` of Broadcom's data; nothing if there is
 * no such list.
 */
static void write_inits_named(struct bcm4360_mac *mac, const char *name)
{
	const void *list;
	u32 size = 0;

	list = hw_fw_data(mac->hw, name, &size);
	if (list)
		bcm4360_bmac_write_inits(mac, list);
}

/*
 * docs/re/spec/bmac-init.md, B7: SPINWAIT((D11(reg) & mask) != 0, 200) with
 * 16-bit reads, then one more read of the register.
 */
static void bmc_wait_clear(struct bcm4360_mac *mac, u32 reg, u16 mask)
{
	u32 waited = 0;

	while ((d11_read16(mac->hw, reg) & mask) != 0 && waited < BMC_WAIT_US) {
		hw_udelay(SPINWAIT_STEP_US);
		waited += SPINWAIT_STEP_US;
	}
	(void)d11_read16(mac->hw, reg);
}

/*
 * docs/re/spec/bmac-init.md, B7 step 4: the six writes for FIFO `f` (7 is the
 * template memory) with n the value that D11(0x54e) is made of.
 */
static void bmc_fifo_cfg(struct bcm4360_mac *mac, u16 f, u16 maxbufs, u16 minbufs,
			 u16 r520, u16 n)
{
	d11_write16(mac->hw, D11_REG_0x54a, maxbufs);
	d11_write16(mac->hw, D11_REG_0x54c, minbufs);
	d11_write16(mac->hw, D11_REG_0x520, r520);
	d11_write16(mac->hw, D11_REG_0x54e, ((n - BMC_0x54e_LOW_GAP) << 8) | n);
	d11_write16(mac->hw, D11_REG_0x550, BMC_0x550);
	d11_write16(mac->hw, D11_REG_0x548, BMC_CMD_FIFO | f);
}

/*
 * docs/re/spec/bmac-init.md, B7 step 5: hand the buffers of the template
 * memory out, one command per buffer.
 */
static void bmc_tpl_buffers(struct bcm4360_mac *mac, u16 base)
{
	u16 i, e;

	for (i = 0; i < base; i++) {
		e = i + BMC_TPL_SPAN;
		if (e > base - 1)
			e = base - 1;
		d11_write16(mac->hw, D11_REG_0x534, i);
		d11_write16(mac->hw, D11_REG_0x536, e);
		d11_write16(mac->hw, D11_REG_0x532, e - i + 1);
		d11_write16(mac->hw, D11_REG_0x530, BMC_TPL_CMD | (i << BMC_TPL_IDX_SHIFT));
		bmc_wait_clear(mac, D11_REG_0x530, BMC_ALL_BITS);
	}
}

/*
 * docs/re/spec/bmac-init.md, B7 (sub_067efd, wlc_bmac_bmc_init): configure
 * the buffer memory controller of MAC revision 42. No error is reported.
 */
static void wlc_bmac_bmc_init(struct bcm4360_mac *mac)
{
	u16 total, f;

	/* step 1 (the object also keeps `total` in two static variables) */
	total = (d11_read32(mac->hw, D11_MACHWCAP) >> BMC_TOTAL_SHIFT) & BMC_TOTAL_MASK;

	/* step 2: revisions 43 and 45..47: six more writes, base 0x7a; not described */

	/* step 3 */
	d11_write16(mac->hw, D11_REG_0x542, total);
	d11_write16(mac->hw, D11_REG_0x540, BMC_CTL_INIT);
	bmc_wait_clear(mac, D11_REG_0x540, BMC_CTL_BUSY);

	/* step 4: the template memory first, then the transmit FIFOs 0..5 */
	bmc_fifo_cfg(mac, BMC_TPL_FIFO, BMC_BASE, BMC_BASE, BMC_BASE, BMC_BASE);
	for (f = 0; f < BMC_NR_TX_FIFOS; f++)
		bmc_fifo_cfg(mac, f, (total - BMC_BASE) & 0xffff, BMC_FIFO_MINBUFS,
			     BMC_FIFO_0x520, 2 * BMC_FIFO_0x520);

	/* step 5 */
	bmc_tpl_buffers(mac, BMC_BASE);
}

/*
 * docs/re/spec/bmac-init.md, B8 (sub_05fdca, wlc_bmac_synthpu_dly): the time
 * the radio's synthesizer needs to power up, microseconds, by PHY type of the
 * current band. No access.
 */
static u16 wlc_bmac_synthpu_dly(struct bcm4360_mac *mac)
{
	if (mac->band_phytype != PHY_TYPE_AC) {
		/* other PHY types: 50 .. 3580, not assigned to types in the
		 * specification; a 2050 radio of revision 8: at least 2400 */
		return 0;
	}
	return mac->chip_id == CHIP_ID_4350 ? SYNTHPU_DLY_4350 : SYNTHPU_DLY_AC;
}

/* docs/re/spec/bmac-init.md, B8 (sub_0627c9, wlc_bmac_upd_synthpu) */
static void wlc_bmac_upd_synthpu(struct bcm4360_mac *mac)
{
	write_shm(mac, SHM_SYNTHPU_DLY, wlc_bmac_synthpu_dly(mac));
}

/*
 * docs/re/spec/bmac-init.md, B9: floor(n / d) by shift and subtract (64-bit
 * numerator, 32-bit divisor, the low 32 bits of the quotient).
 */
static u32 div64_32(u64 n, u32 d)
{
	u64 rem = 0, q = 0;
	s32 bit;

	for (bit = 63; bit >= 0; bit--) {
		rem = (rem << 1) | ((n >> bit) & 1);
		q <<= 1;
		if (rem >= d) {
			rem -= d;
			q |= 1;
		}
	}
	return (u32)q;
}

/* docs/re/spec/bmac-init.md, B9: the chips that compute the frequency word */
static bool chip_is_macfreq_vco(struct bcm4360_mac *mac)
{
	if (mac->chip_id == CHIP_ID_4350)
		return mac->bustype != 0;
	return mac->chip_id == CHIP_ID_4360 || mac->chip_id == CHIP_ID_4352 ||
	       mac->chip_id == CHIP_ID_A9C4 || mac->chip_id == CHIP_ID_AA06;
}

/*
 * docs/re/spec/bmac-init.md, B9 (wlc_bmac_switch_macfreq): tell the MAC the
 * frequency of its clock. `spurmode` is not used by the chips of this family.
 */
void bcm4360_bmac_switch_macfreq(struct bcm4360_mac *mac, u8 spurmode)
{
	u32 vco, q;

	(void)spurmode;

	/* chip 0x4335 and the N-PHY chips: tables selected by spurmode; chips in
	 * no list without an LCN-PHY: nothing; not described */
	if (!chip_is_macfreq_vco(mac))
		return;

	/* step 1 */
	vco = bcm4360_bmac_env_pmu_bb_vcofreq(mac, MACFREQ_XTAL_MHZ);
	if (vco == 0)
		return;		/* no division by 0 (see the questions) */

	/* step 2 */
	q = div64_32(MACFREQ_NUMERATOR, vco);

	/* step 3 */
	d11_write16(mac->hw, D11_MACFREQ_LO, q & MACFREQ_HALF_MASK);
	d11_write16(mac->hw, D11_MACFREQ_HI, q >> MACFREQ_HALF_SHIFT);
}

/*
 * docs/re/spec/bmac-init.md, B10 step 3 (sub_062766, wlc_write_mhf): the five
 * host flag copies of the current band to the shared memory.
 */
static void wlc_write_mhf(struct bcm4360_mac *mac)
{
	u32 i;

	for (i = 0; i < MHF_COUNT; i++)
		write_shm(mac, mhf_shm_offset[i], mac->mhf[mac->current_band][i]);
}

/*
 * docs/re/spec/bmac-init.md, B10 step 6 (sub_062684, wlc_ucode_txant_set):
 * nothing for MAC revisions >= 40.
 */
static void wlc_ucode_txant_set(struct bcm4360_mac *mac)
{
	if (mac->mac_rev >= MAC_REV_40)
		return;
	/* older revisions: the antenna bits 6..9 of SHM(0x188) and SHM(0x22); not described */
}

/*
 * docs/re/spec/bmac-init.md, B10 step 8 (sub_062716,
 * wlc_bmac_update_slot_timing): the slot time to the IFS block and to the
 * microcode.
 */
static void wlc_bmac_update_slot_timing(struct bcm4360_mac *mac, bool shortslot)
{
	if (shortslot) {
		d11_write16(mac->hw, D11_IFS_SLOT, IFS_SLOT_SHORT);
		write_shm(mac, SHM_SLOT_TIME, SLOT_SHORT_SHM);
	} else {
		d11_write16(mac->hw, D11_IFS_SLOT, IFS_SLOT_LONG);
		write_shm(mac, SHM_SLOT_TIME, SLOT_LONG_SHM);
	}
}

/* docs/re/spec/bmac-init.md, B12: the PHY types whose rate table is updated */
static bool phytype_has_ofdm_pctl1(struct bcm4360_mac *mac)
{
	switch (mac->band_phytype) {
	case PHY_TYPE_N:
	case PHY_TYPE_6:
	case PHY_TYPE_HT:
	case PHY_TYPE_8:
	case PHY_TYPE_10:
	case PHY_TYPE_AC:
		return true;
	default:
		return false;
	}
}

/*
 * docs/re/spec/bmac-init.md, B12 (sub_062403, wlc_upd_ofdm_pctl1_table): the
 * PHY control words of the eight OFDM rates of the microcode's rate table,
 * read and written back unchanged for the AC-PHY; then the coexistence bits
 * of SHM(0x22).
 */
static void wlc_upd_ofdm_pctl1_table(struct bcm4360_mac *mac)
{
	u32 i, a;
	u16 t, v;

	if (!phytype_has_ofdm_pctl1(mac))
		return;

	/* step 1 */
	for (i = 0; i < ARRAY_SIZE(ofdm_rate_codes); i++) {
		t = read_shm(mac, SHM_RATE_DIR + 2 * ofdm_rate_codes[i]);
		a = ((2 * (u32)t) & 0xffff) + SHM_RATE_PCTL;
		v = read_shm(mac, a);
		/* step 1.3: revision 31 with the N-PHY, revision 29 with the
		 * HT-PHY, the N- and LCN-PHY modify v; not the AC-PHY */
		write_shm(mac, a, v);
	}

	/* step 2 */
	if (mac->btc_mode != 0) {
		v = read_shm(mac, SHM_TXANT_CTL);
		v = (v & TXANT_CTL_KEEP) |
		    ((mac->boardflags2 & BFL2_BIT23) ? TXANT_CTL_BTC_BFL2 : TXANT_CTL_BTC);
		write_shm(mac, SHM_TXANT_CTL, v);
	}
}

/*
 * docs/re/spec/bmac-init.md, B12 (wlc_bmac_txbw_update): the rate table
 * update of B12 if the core's clock is on.
 */
void bcm4360_bmac_txbw_update(struct bcm4360_mac *mac)
{
	if (mac->clk)
		wlc_upd_ofdm_pctl1_table(mac);
}

/*
 * docs/re/spec/bmac-init.md, B12 (wlc_bmac_band_stf_ss_set): keep `mode` (used
 * by the N-PHY branch of B12 step 1.3 only) and update the rate table if the
 * core's clock is on.
 */
void bcm4360_bmac_band_stf_ss_set(struct bcm4360_mac *mac, u8 mode)
{
	mac->stf_ss_mode = mode;
	if (mac->clk)
		wlc_upd_ofdm_pctl1_table(mac);
}

/*
 * docs/re/spec/bmac-init.md, B10 step 15 (wlc_bmac_set_extlna_pwrsave_shmem):
 * the power save value of the external LNA (chip 0x4331 under board
 * conditions: 0x04c0; not this chip).
 */
void bcm4360_bmac_set_extlna_pwrsave_shmem(struct bcm4360_mac *mac)
{
	write_shm(mac, SHM_EXTLNA_PWRSAVE, EXTLNA_PWRSAVE);
}

/*
 * docs/re/spec/bmac-init.md, E10, sub_06106b (wlc_bmac_btc_gpio_enable): hand
 * the GPIO lines of the coexistence to it; nothing without a GPIO mask.
 */
static void wlc_bmac_btc_gpio_enable(struct bcm4360_mac *mac)
{
	u16 v;

	if (!mac->btc_valid || mac->btc_gpio_mask == 0)
		return;

	v = d11_read16(mac->hw, D11_REG_0x49e);
	d11_write16(mac->hw, D11_REG_0x49e, v | (u16)mac->btc_gpio_out);
	bcm4360_bmac_env_gpioouten(mac, ~mac->btc_gpio_out & mac->btc_gpio_mask, 0, 0);
	/* chip 0x4313: si_gpiopull here; not this chip */
	(void)bcm4360_bmac_env_gpiocontrol(mac, mac->btc_gpio_mask, mac->btc_gpio_mask, 0);
}

/*
 * docs/re/spec/bmac-init.md, E10, sub_062b79: the OR of the words of the
 * table btc_ucode_flags that the coexistence flag bits 1..6 select.
 */
static u16 btc_ucode_flags_or(struct bcm4360_mac *mac)
{
	const u8 *tbl;
	u32 size = 0, k, off;
	u16 a = 0;

	tbl = hw_fw_data(mac->hw, "btc_ucode_flags", &size);
	if (!tbl)
		return 0;
	for (k = BTC_FLAG_TBL_FIRST; k <= BTC_FLAG_TBL_LAST; k++) {
		off = BTC_TBL_BASE + BTC_TBL_STRIDE * (k - BTC_FLAG_TBL_FIRST);
		if ((mac->btc_flags & BIT(k)) && off + OBJ_HALF_BYTES <= size)
			a |= bytes_le16(tbl + off);
	}
	return a;
}

/* docs/re/spec/bmac-init.md, E10, sub_062b79: the board case of host flag 5 bit 0 */
static bool btc_apple_board(struct bcm4360_mac *mac)
{
	if (mac->boardvendor != BOARDVENDOR_APPLE)
		return false;
	return mac->boardtype == BOARDTYPE_0x10F || mac->boardtype == BOARDTYPE_0xEF ||
	       mac->boardtype == BOARDTYPE_0xF4 || mac->boardtype == BOARDTYPE_0x10E ||
	       mac->chip_id == CHIP_ID_4360;
}

/*
 * docs/re/spec/bmac-init.md, E10, sub_062b79 (wlc_bmac_btc_flags_to_mhf):
 * the coexistence flags word to host flag bits, all of the 2.4 GHz band
 * except one.
 */
static void wlc_bmac_btc_flags_to_mhf(struct bcm4360_mac *mac)
{
	u16 a = 0, v;

	bcm4360_bmac_mhf(mac, MHF2, MHF2_0x0200,
			 (mac->btc_flags & BTC_FLAG_0) ? MHF2_0x0200 : 0, MHF_BANDS_2G);

	if (mac->btc_flags & BTC_FLAGS_TBL)
		a = btc_ucode_flags_or(mac);
	bcm4360_bmac_mhf(mac, MHF3, MHF3_0x1504, a & MHF3_BTC_KEEP, MHF_BANDS_2G);
	if ((a & MHF3_BTC_ONLY_0x1000) == 0)
		bcm4360_bmac_mhf(mac, MHF3, MHF3_0x1000, a & MHF3_0x1000, MHF_BANDS_AUTO);

	v = ((mac->btc_flags & BTC_FLAG_7) ? MHF5_0x0002 : 0) |
	    ((mac->btc_flags & BTC_FLAG_8) ? MHF5_0x0004 : 0);
	bcm4360_bmac_mhf(mac, MHF5, MHF5_0x0006, v, MHF_BANDS_2G);

	if (btc_apple_board(mac))
		bcm4360_bmac_mhf(mac, MHF5, MHF5_0x0001, MHF5_0x0001, MHF_BANDS_2G);
}

/*
 * docs/re/spec/bmac-init.md, B10 (sub_06656c, wlc_bmac_bsinit): the part of
 * the initialisation that depends on the band. `bandswitch`: called by the
 * band switch of wlc_bmac_set_chanspec (not by wlc_bmac_init).
 */
void bcm4360_bmac_bsinit(struct bcm4360_mac *mac, u16 chanspec, bool bandswitch)
{
	/* step 1: chips 0xa9a7 and 0x4331: si_seci_upd; not this chip */

	/* step 2 */
	(void)d11_read16(mac->hw, D11_PHY_VERSION);

	/* step 3 */
	wlc_write_mhf(mac);

	/* step 4: revision 42 with the AC-PHY (43: d11ac3bsinitvals43; 41,
	 * 44..47: d11ac2bsinitvals41; 40: d11ac0bsinitvals40) */
	if (rev42_acphy(mac))
		write_inits_named(mac, "d11ac1bsinitvals42");

	/* step 5 */
	if (!bandswitch || mac->mac_rev < MAC_REV_40)
		bcm4360_bmac_env_phy_init(mac, chanspec);

	/* step 6 */
	wlc_ucode_txant_set(mac);

	/* step 7 */
	bcm4360_bmac_set_cwmin(mac, mac->cwmin);
	bcm4360_bmac_set_cwmax(mac, mac->cwmax);

	/* step 8: the 5 GHz band always has the short slot */
	wlc_bmac_update_slot_timing(mac, band_is_5g(mac) ? true : mac->shortslot);

	/* step 9 */
	write_shm(mac, SHM_PHY_TYPE, mac->band_phytype);
	write_shm(mac, SHM_PHY_REV, mac->band_phyrev);

	/* step 10 */
	wlc_upd_ofdm_pctl1_table(mac);

	/* step 11 */
	wlc_bmac_upd_synthpu(mac);

	/* step 12 */
	bcm4360_bmac_mhf(mac, MHF5, MHF5_0x0008, band_is_5g(mac) ? MHF5_0x0008 : 0,
			 MHF_BANDS_ALL);

	/* step 13 */
	wlc_bmac_btc_gpio_enable(mac);

	/* step 14: chips 0xa9a7/0x4331 in 5 GHz with package 9 or 11; not this chip */

	/* step 15 */
	bcm4360_bmac_set_extlna_pwrsave_shmem(mac);
}

/* docs/re/spec/bmac-init.md, B1 step 3: the chips that switch the RF LDO on */
static bool chip_is_rfldo_list(struct bcm4360_mac *mac)
{
	return mac->chip_id == CHIP_ID_4352 || mac->chip_id == CHIP_ID_4360 ||
	       mac->chip_id == CHIP_ID_AA06;
}

/* docs/re/spec/bmac-init.md, B1 step 36: the chips that set the MAC frequency */
static bool chip_is_macfreq_list(struct bcm4360_mac *mac)
{
	return mac->chip_id == CHIP_ID_A9C4 || mac->chip_id == CHIP_ID_4360 ||
	       mac->chip_id == CHIP_ID_AA06 || mac->chip_id == CHIP_ID_4352 ||
	       mac->chip_id == CHIP_ID_4350;
}

/*
 * docs/re/spec/bmac-init.md, B1 step 8: the Bluetooth coexistence hardware,
 * by board flags. (With boardflags2 bit 7 the chips 0x4331, 0x4313, 0xa9a7,
 * 0xa8d1, 0xa87b, 0xa8db, 0xa8dc and 0xa9a4 make chip specific calls first;
 * nothing for the 4360 family.)
 */
static void bmac_init_btc_hw(struct bcm4360_mac *mac)
{
	bool btcx;

	if (!(mac->boardflags & BFL_0x1))
		return;

	if (mac->boardflags2 & BFL2_0x80) {
		if (mac->chip_id != CHIP_ID_4331)
			bcm4360_bmac_env_seci_init(mac, SECI_MODE_3);
		return;
	}

	btcx = (mac->machwcap & MCAP_BTCX) != 0;
	if (!btcx)
		return;
	if ((mac->cccaps & CC_CAP_ECI) && !(mac->cccaps_ext & CC_CAPEXT_SECI))
		bcm4360_bmac_env_eci_init(mac);
	else if (mac->cccaps_ext & CC_CAPEXT_SECI)
		bcm4360_bmac_env_seci_init(mac, SECI_MODE_1);
	else if (mac->cccaps_ext & CC_CAPEXT_GCI)
		bcm4360_bmac_env_gci_init(mac);
}

/*
 * docs/re/spec/bmac-init.md, B1 step 13: antenna selection and the GPIO lines
 * the core drives.
 */
static void bmac_init_antsel_gpio(struct bcm4360_mac *mac)
{
	u32 gm = 0, gc = 0;
	u16 v;

	switch (mac->antsel_type) {
	case 2:
	case 3:
	case 6:
		bcm4360_bmac_mhf(mac, MHF3, MHF3_0x0001, MHF3_0x0001, MHF_BANDS_ALL);
		bcm4360_bmac_mhf(mac, MHF3, MHF3_0x0002, MHF3_0x0002, MHF_BANDS_ALL);
		bcm4360_bmac_env_phy_antsel_init(mac, false);
		break;
	case 1:
		v = d11_read16(mac->hw, D11_REG_0x49e);
		d11_write16(mac->hw, D11_REG_0x49e, v | GPIO_ANTSEL1);
		v = d11_read16(mac->hw, D11_REG_0x49c);
		d11_write16(mac->hw, D11_REG_0x49c, v | GPIO_ANTSEL1);
		bcm4360_bmac_mhf(mac, MHF3, MHF3_0x0001, MHF3_0x0001, MHF_BANDS_ALL);
		bcm4360_bmac_mhf(mac, MHF3, MHF3_0x0002, 0, MHF_BANDS_ALL);
		write_shm(mac, SHM_MACHWCAP_HI, ANTSEL1_SHM_0xc2);
		gm = GPIO_ANTSEL1;
		gc = GPIO_ANTSEL1;
		break;
	default:
		break;
	}

	if (mac->boardflags & BFL_0x2) {
		gc |= GPIO_BFL_0x2;
		gm |= gc;
	}
	/* chip 0x4322: a block follows here; not this chip */
	(void)bcm4360_bmac_env_gpiocontrol(mac, gm, gc, 0);
}

/* docs/re/spec/bmac-init.md, B1 step 14: clear the address match table */
static void bmac_init_amt_clear(struct bcm4360_mac *mac)
{
	s32 i;

	for (i = 0; i < AMT_ENTRIES; i++)
		bcm4360_bmac_write_amt(mac, i, ether_null, 0);
}

/*
 * docs/re/spec/bmac-init.md, B1 steps 20-29: shared memory, interrupt and
 * IFS registers, the fast power-up delay, the retry limits; `scratch9` holds
 * the four bytes of step 16.
 */
static void bmac_init_core_regs(struct bcm4360_mac *mac, const u8 *scratch9)
{
	u16 d, v;

	/* step 20 */
	write_shm(mac, SHM_BURST_LIMIT, BURST_LIMIT);
	write_shm(mac, SHM_ANT_DIV_CNT, ANT_DIV_CNT);

	/* step 21 */
	d11_write32(mac->hw, D11_RCV_LAZY0, mac->intrcvlazy);

	/* step 22: INFRA and DISCARD_PMQ set, AP cleared */
	bcm4360_bmac_mctrl(mac, MCTL_DISCARD_PMQ | MCTL_AP | MCTL_INFRA,
			   MCTL_DISCARD_PMQ | MCTL_INFRA);

	/* step 23 */
	d11_write32(mac->hw, D11_REG_0x188, D11_0x188_DEFAULT);
	d11_write32(mac->hw, D11_REG_0x18c, D11_0x18c_DEFAULT);
	d11_write32(mac->hw, D11_MACINTSTATUS, MI_BIT14);
	d11_write32(mac->hw, D11_REG_0x24, DMA0_INTMASK_RX);

	/* step 24 */
	bcm4360_bmac_macphyclk_set(mac, true);

	/* step 25 */
	d = bcm4360_bmac_env_clkctl_fast_pwrup_delay(mac);
	d11_write16(mac->hw, D11_FASTPWRUP_DLY, d);
	mac->fastpwrup_dly = d;
	if (mac->mac_rev > MAC_REV_40)
		mac->fastpwrup_dly = d + wlc_bmac_synthpu_dly(mac);

	/* step 26 */
	write_shm(mac, SHM_MAC_REV, mac->mac_rev);
	write_shm(mac, SHM_MACHWCAP_LO, mac->machwcap & 0xffff);
	write_shm(mac, SHM_MACHWCAP_HI, mac->machwcap >> 16);

	/* step 27: the four bytes of step 16 only if the byte wlc+0x718 was 0 */
	scratch_write(mac, SCR_SRL, mac->srl);
	scratch_write(mac, SCR_LRL, mac->lrl);
	if (!bcm4360_bmac_env_wlc_take_flag_718(mac))
		bcm4360_bmac_copyto_objmem(mac, OBJ_REG_OFFSET(SCR_0x9), scratch9,
					   SCR_0x9_BYTES, OBJ_SEL_SCRATCH);

	/* step 28 */
	write_shm(mac, SHM_SFBL, mac->sfbl);
	write_shm(mac, SHM_LFBL, mac->lfbl);

	/* step 29 */
	v = d11_read16(mac->hw, D11_REG_0x688);
	d11_write16(mac->hw, D11_REG_0x688, v & IFS_0x688_KEEP);
	d11_write16(mac->hw, D11_IFS_AIFSN, IFS_AIFSN_MIN);
}

/*
 * docs/re/spec/bmac-init.md, B1 step 30: the DMA engines of the FIFOs that
 * have one: transmit initialisation, then receive initialisation and buffer
 * fill of FIFO 0.
 */
static void bmac_init_dma(struct bcm4360_mac *mac)
{
	u32 i;

	bcm4360_bmac_env_wlc_clear_68(mac);
	for (i = 0; i < NR_TX_FIFOS; i++)
		if (mac->dma_fifos & BIT(i))
			bcm4360_bmac_env_dma_txinit(mac, i);
	if (mac->dma_fifos & BIT(RX_FIFO)) {
		bcm4360_bmac_env_dma_rxinit(mac, RX_FIFO);
		bcm4360_bmac_env_dma_rxfill(mac, RX_FIFO);
	}
}

/*
 * docs/re/spec/bmac-init.md, B1 step 32.1: the variables btc_params<n> to the
 * coexistence parameters at SHM(p + 2n).
 */
static void bmac_init_btc_params_vars(struct bcm4360_mac *mac, u16 p)
{
	char name[16];
	const char *v;
	u32 n;

	for (n = 0; n < BTC_PARAMS_COUNT; n++) {
		hw_snprintf(name, sizeof(name), "btc_params%u", n);
		v = bcm4360_bmac_env_getvar(mac, name);
		if (v)
			write_shm(mac, (u32)p + 2 * n, (u16)bcm4360_strtoul(v, NULL, 0));
	}
}

/*
 * docs/re/spec/bmac-init.md, B1 step 32: the coexistence parameters, the
 * coexistence flags and the interface's MAC address in the shared memory.
 */
static void bmac_init_btc_params(struct bcm4360_mac *mac)
{
	u8 addr[ETHER_ADDR_LEN];
	const char *v;
	u16 p;

	p = 2 * read_shm(mac, SHM_BTC_BASE_HALF);
	mac->btc_shm_base = p;
	if (p == 0)
		return;

	/* step 32.1 */
	bmac_init_btc_params_vars(mac, p);

	/* step 32.2: chips 0x4352 and 0xa8dc only */
	if (mac->chip_id == CHIP_ID_4352 || mac->chip_id == CHIP_ID_A8DC) {
		write_shm(mac, (u32)p + BTC_PARAM_0x02, 30000);
		write_shm(mac, (u32)p + BTC_PARAM_0x10, 20000);
		write_shm(mac, (u32)p + BTC_PARAM_0x12, 30000);
		write_shm(mac, (u32)p + BTC_PARAM_0x2c, 0x753);
	}

	/* step 32.3 */
	v = bcm4360_bmac_env_getvar(mac, "btc_flags");
	if (v) {
		mac->btc_flags = (u16)bcm4360_strtoul(v, NULL, 0);
		wlc_bmac_btc_flags_to_mhf(mac);
	}

	/* step 32.4 */
	if (mac->mac_rev >= MAC_REV_40) {
		bcm4360_bmac_env_pub_etheraddr(mac, addr);
		write_shm(mac, SHM_ETHERADDR, bytes_le16(addr));
		write_shm(mac, SHM_ETHERADDR + 2, bytes_le16(addr + 2));
		write_shm(mac, SHM_ETHERADDR + 4, bytes_le16(addr + 4));
	}
}

/*
 * docs/re/spec/bmac-init.md, B1 steps 9-19: microcode download and start,
 * antenna selection and GPIO, address match table, the initialisation list
 * and the buffer memory controller; `scratch9` receives the four bytes of
 * step 16.
 */
static void bmac_init_ucode_and_lists(struct bcm4360_mac *mac, u8 *scratch9)
{
	/* step 9 */
	wlc_ucode_download(mac);

	/* step 10 (the result is ignored) */
	(void)bcm4360_bmac_wowlucode_start(mac);

	/* step 11 */
	bcm4360_bmac_mctrl(mac, MCTL_GPOUT_SEL, 0);

	/* step 12 */
	if (mac->btc_valid && mac->btc_gpio_out != 0)
		wlc_bmac_btc_gpio_enable(mac);

	/* step 13 */
	bmac_init_antsel_gpio(mac);

	/* step 14 */
	if (mac->mac_rev >= MAC_REV_40)
		bmac_init_amt_clear(mac);

	/* step 15: if gpio_chipctl is not 0: GPIO/chip control for chips other
	 * than the 4360 family; nothing for the family */

	/* step 16 */
	bcm4360_bmac_copyfrom_objmem(mac, OBJ_REG_OFFSET(SCR_0x9), scratch9, SCR_0x9_BYTES,
				     OBJ_SEL_SCRATCH);

	/* step 17 (the value is used by other revisions only) */
	(void)bcm4360_bmac_env_core_sflags(mac, 0, 0);

	/* step 18: revision 42 with the AC-PHY (43: d11ac3initvals43; 41,
	 * 44..47: d11ac2initvals41; 40: d11ac0initvals40); no list for another
	 * PHY type */
	if (rev42_acphy(mac))
		write_inits_named(mac, "d11ac1initvals42");

	/* step 19 (revisions below 40 set fixed FIFO sizes instead; not described) */
	if (mac->mac_rev >= MAC_REV_40)
		wlc_bmac_bmc_init(mac);
}

/*
 * docs/re/spec/bmac-init.md, B1 (wlc_bmac_init): from "core reset done" to
 * "microcode running and suspended, MAC and PHY initialised for `chanspec`".
 * `mute`: the transmitter is muted at the end.
 */
void bcm4360_bmac_init(struct bcm4360_mac *mac, u16 chanspec, bool mute)
{
	u8 scratch9[SCR_0x9_BYTES];
	bool fast;
	u32 mask;

	/* step 1 */
	fast = mac->forcefastclk;
	if (!fast)
		wlc_bmac_clkctl_clk(mac, CLK_FAST);

	/* step 2 */
	mask = bcm4360_bmac_env_intrsoff(mac);

	/* step 3 */
	if (chip_is_rfldo_list(mac))
		bcm4360_bmac_env_pmu_rfldo(mac, true);

	/* step 4: the band_* fields are valid from here on */
	bcm4360_bmac_env_setxband(mac, (chanspec & CHSPEC_BAND_MASK) == CHSPEC_BAND_5G ? 1 : 0);

	/* step 5 */
	bcm4360_bmac_env_phy_chanspec_radio_set(mac, chanspec);
	bcm4360_bmac_env_phy_cal_init(mac);

	/* step 6: WAKE | IHR_EN | PSM_JMP_0 */
	bcm4360_bmac_mctrl(mac, MCTL_ALL, MCTL_WAKE | MCTL_IHR_EN | MCTL_PSM_JMP_0);

	/* step 7 */
	bcm4360_bmac_env_btc_mode_set(mac, mac->btc_mode);

	/* step 8 */
	bmac_init_btc_hw(mac);

	/* steps 9-19 */
	bmac_init_ucode_and_lists(mac, scratch9);

	/* steps 20-29 */
	bmac_init_core_regs(mac, scratch9);

	/* step 30 */
	bmac_init_dma(mac);

	/* step 31: chips 0x4748, 0x4716, 0xb83a, 0x4314, 0x4334, 0xa886, 0xa887
	 * write D11(0x62e), D11(0x630) here; not this chip (see step 36) */

	/* step 32 */
	bmac_init_btc_params(mac);

	/* step 33 (the value is used by MAC revision 33 only) */
	(void)read_shm(mac, SHM_0x8e);

	/* step 34: the MAC counts as suspended once */
	mac->mac_suspend_depth = 1;

	/* step 35 */
	if (mute)
		bcm4360_bmac_env_mute(mac, true, MUTE_REASON_INIT);

	/* step 36 */
	if (chip_is_macfreq_list(mac))
		bcm4360_bmac_switch_macfreq(mac, 0);

	/* step 37 */
	bcm4360_bmac_bsinit(mac, chanspec, false);

	/* step 38 */
	bcm4360_bmac_env_intrsrestore(mac, mask);

	/* step 39: no access; the matching enable clears it */
	mac->wake_override |= WAKE_REASON_MAC_SUSPENDED;

	/* step 40 */
	if (!fast)
		wlc_bmac_clkctl_clk(mac, CLK_DYNAMIC);
}

/*
 * docs/re/spec/bmac-init.md, E6 (wlc_bmac_set_shortslot): keep the slot time
 * request; in the 2.4 GHz band with the interface up apply it with the MAC
 * suspended.
 */
void bcm4360_bmac_set_shortslot(struct bcm4360_mac *mac, bool shortslot)
{
	mac->shortslot = shortslot;
	if (mac->band_type != BAND_TYPE_2G || !mac->up)
		return;
	bcm4360_bmac_suspend(mac);
	wlc_bmac_update_slot_timing(mac, shortslot);
	bcm4360_bmac_enable(mac);
}
