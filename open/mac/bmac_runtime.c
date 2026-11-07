/*
 * The MAC core at run time (wlc_bmac.c): the energy detect of the IFS block
 * for the current bandwidth, the suspend and resume of the transmit FIFOs,
 * the microcode's CCA counters, the per-rate entries of the microcode's rate
 * table in shared memory, and the TSF read.
 *
 * Written from docs/re/spec/bmac-run.md, sections R1, R2, R5, R6 and R7
 * (wlc_bmac_read_tsf only), with its "Data", and the primitives of
 * docs/re/spec/bmac-init.md A3, A4, A6, A7 (open in bmac_ctrl.c) and A8 (the
 * access layer and bmac_mem.c), for MAC core revision 42 with an AC-PHY
 * (chip 0x4360, PCIe). Everything outside the 802.11 core's registers and the
 * shared memory goes through the services of bcm4360/mac_env.h. A service may
 * change struct bcm4360_mac (the PHY and the DMA layer can run MAC code), so
 * no field of *mac is kept in a local variable across a service call.
 */
#include <bcm4360/mac.h>
#include <bcm4360/mac_env.h>
#include <bcm4360/access.h>

/* Direct 802.11-core registers used here (bmac-run.md, bmac-init.md "Data"). */
#define D11_MACCONTROL		0x120	/* MAC control register (R2 step 5) */
#define D11_CHNSTATUS		0x150	/* bit i: transmit channel i busy (brcmsmac: chnstatus; R2) */
#define D11_TSF_TIMERLOW	0x180	/* TSF, low 32 bits (R7) */
#define D11_TSF_TIMERHIGH	0x184	/* TSF, high 32 bits (R7) */
#define D11_REG_0x69e		0x69e	/* EDCRS control of MAC revisions below 40 (R1 sub_0622d9) */
#define D11_REG_0x6c6		0x6c6	/* energy detect, per 20 MHz sub-channel (R1; unverified) */
#define D11_REG_0x6c8		0x6c8	/* position of the primary channel (R1; unverified) */

/* Bit 0 of D11(0x120): EN_MAC, the MAC is enabled (bmac-init.md "Data"). */
#define MCTL_EN_MAC		0x00000001

/* The reason "transmit FIFO" of the microcode wake override (A3/A4; R2). */
#define WAKE_REASON_TX_FIFO	0x08

/* PHY types of a band state (bmac-run.md "Constants"). */
#define PHY_TYPE_N		4
#define PHY_TYPE_LP		6
#define PHY_TYPE_HT		7
#define PHY_TYPE_SSN		8
#define PHY_TYPE_LCN		10
#define PHY_TYPE_AC		11

/* MAC core revisions at which the code of this file branches. */
#define MAC_REV_40		40	/* R1: sub_0622d9 below, SHM(0x5a) up to; R5, R6 */

/* Chanspec fields (bmac-run.md "Constants"). */
#define CHSPEC_BW_MASK		0x3800
#define CHSPEC_BW_20		0x1000
#define CHSPEC_BW_40		0x1800
#define CHSPEC_BW_80		0x2000
#define CHSPEC_SB_MASK		0x0700
#define CHSPEC_SB_LL		0x0000	/* the lowest 20 MHz sub-channel is the control channel */
#define CHSPEC_SB_LU		0x0100	/* the second lowest */

/*
 * R1: the shared-memory copy of the EDCRS control, the values and the masks
 * of sub_06212d and the arguments of sub_0622d9.
 */
#define SHM_0x5a		0x5a
#define EDCRS_ON		0x0f0f	/* sub_06212d step 1, `on` != 0 */
#define EDCRS_OFF		0x000f	/* sub_06212d step 1, `on` = 0 */
#define EDCRS_KEEP		0xf0f0	/* the bits of 0x6c6/0x6c8 that are kept */
#define EDCRS_SUBCH_MASK	0x0f0f	/* sub_06212d: the bits of `v` at 20 and 80 MHz */
#define EDCRS_SUBCH_MASK_40	0x0303	/* ... at 40 MHz */
#define EDCRS_PRIM_40_LOWER	0x2	/* 40 MHz, control channel in the lower half */
#define EDCRS_PRIM_40_UPPER	0x1	/* 40 MHz, control channel in the upper half */
#define EDCRS_PRIM_80_LOWER	0xc	/* 80 MHz, control channel in the lower 40 MHz */
#define EDCRS_PRIM_80_UPPER	0x3	/* 80 MHz, control channel in the upper 40 MHz */
#define IFS_EDCRS_0x08		0x08	/* step 2: mask and value of sub_0622d9 */

/* R5: the CCA counters, 32 bits each as two 16-bit shared-memory words. */
#define SHM_CCA_BASE		0x768	/* MAC revisions >= 40 */
#define SHM_CCA_BASE_OLD	0x6c0	/* older revisions */
#define SHM_CCA_HI		2	/* the high word follows the low word */
#define SHM_0x10e		0x10e	/* CCA word 7 */
#define SHM_0x10c		0x10c	/* CCA word 8 */
#define CCA_TSF_WORD		5	/* CCA word 5: the low word of the TSF */

/*
 * R5 cca_stats_read: the offsets from the base of the 32-bit counters, in the
 * order they are read, and the output word each goes to.
 */
static const u8 cca_counter_off[] = { 0x00, 0x04, 0x08, 0x0c, 0x10, 0x14 };
static const u8 cca_counter_word[] = { 0, 1, 2, 3, 4, 6 };

/* R6: the two directories of the microcode's rate table (bmac-init.md G). */
#define SHM_RATE_DIR_OFDM	0x1c0
#define SHM_RATE_DIR_CCK	0x200
#define RATE_INFO_OFDM		0x80	/* bit 7 of a rate_info value: an OFDM rate */
#define RATE_INFO_IDX		0x0f	/* the word of the rate in its directory */
#define RATE_MASK		0x7f	/* stf_get step 1: the rate without bit 7 */

/* R6: an entry of a rate set's offset list, 6 bytes, little-endian. */
#define RS_ENTRY_BYTES		6
#define RS_ENTRY_RATE		0	/* u8 */
#define RS_ENTRY_OFFSET		2	/* u16 */
#define RS_ENTRY_VALUE		4	/* u16 */

/* The A8 selector of the shared memory (memory 1 << 16). */
#define OBJ_SEL_SHM		(D11_OBJ_MEM_SHM << D11_OBJ_MEM_SHIFT)
#define SHM_WORD_BYTES		2

/* Transmit FIFOs: bit i of `suspended_fifos` and `dma_fifos` (R2). */
#define FIFO_BITS		8

/*
 * docs/re/spec/bmac-run.md R6: Broadcom's byte table `rate_info`, index = the
 * rate in 500 kbit/s units, value = the PLCP rate code (bit 7: OFDM; bits
 * 0..3: the word in the directory). Every index not listed is 0. The indices
 * 109..127 hold the bytes R6 lists for them (in the object they follow the
 * table, the start of `mcs_table`). A rate above 127 is treated as "not in
 * the table" (value 0), see bmac-run.md Q3.
 */
#define RATE_INFO_SIZE		128
static const u8 rate_info[RATE_INFO_SIZE] = {
	[2] = 0x0a,	/* 1 Mbit/s */
	[4] = 0x14,	/* 2 Mbit/s */
	[11] = 0x37,	/* 5.5 Mbit/s */
	[12] = 0x8b,	/* 6 Mbit/s */
	[18] = 0x8f,	/* 9 Mbit/s */
	[22] = 0x6e,	/* 11 Mbit/s */
	[24] = 0x8a,	/* 12 Mbit/s */
	[36] = 0x8e,	/* 18 Mbit/s */
	[48] = 0x89,	/* 24 Mbit/s */
	[72] = 0x8d,	/* 36 Mbit/s */
	[96] = 0x88,	/* 48 Mbit/s */
	[108] = 0x8c,	/* 54 Mbit/s */
	/* beyond the table (R6, the bytes that follow it) */
	[112] = 0x64,
	[113] = 0x19,
	[116] = 0xbc,
	[117] = 0x34,
	[120] = 0x37,
	[121] = 0x1c,
	[124] = 0x98,
	[125] = 0x3a,
};

/* docs/re/spec/bmac-run.md R6: a 16-bit entry field, low byte first */
static u16 entry_le16(const u8 *p)
{
	return (u16)(p[0] | p[1] << 8);
}

/* docs/re/spec/bmac-run.md R6: store a 16-bit entry field, low byte first */
static void entry_store_le16(u8 *p, u16 v)
{
	p[0] = v & 0xff;
	p[1] = v >> 8;
}

/*
 * docs/re/spec/bmac-init.md A8 (wlc_bmac_read_shm) with the 32-bit offset the
 * object passes: R6 reads SHM((o & 0xffff) + off) with the sum not truncated
 * to 16 bits, which the access layer's 16-bit offset cannot express. One
 * worker call (B4 with 2 bytes is one A8 read).
 */
static u16 shm_read_wide(struct bcm4360_mac *mac, u32 offset)
{
	u8 b[SHM_WORD_BYTES];

	bcm4360_bmac_copyfrom_objmem(mac, offset, b, SHM_WORD_BYTES, OBJ_SEL_SHM);
	return entry_le16(b);
}

/*
 * docs/re/spec/bmac-init.md A8 (wlc_bmac_write_shm) with a 32-bit offset (R6
 * stf_set: the sum of two 16-bit values): wlc_bmac_set_shm of one word is
 * one worker call.
 */
static void shm_write_wide(struct bcm4360_mac *mac, u32 offset, u16 v)
{
	bcm4360_bmac_set_shm(mac, offset, v, SHM_WORD_BYTES);
}

/* ---- R1: energy detect ---- */

/*
 * docs/re/spec/bmac-run.md R1, sub_0622d9 (wlc_bmac_ifs_ctl_edcrs): the EDCRS
 * control of MAC revisions below 40 and its shared-memory copy. Nothing at
 * all from revision 40 on (this card).
 */
static void wlc_bmac_ifs_ctl_edcrs(struct bcm4360_mac *mac, u16 mask, u16 val)
{
	u16 n;

	if (mac->mac_rev >= MAC_REV_40)
		return;
	n = (d11_read16(mac->hw, D11_REG_0x69e) & ~mask) | val;
	d11_write16(mac->hw, D11_REG_0x69e, n);
	bcm4360_shm_write(mac->hw, SHM_0x5a, n);
}

/*
 * docs/re/spec/bmac-run.md R1, sub_06212d step 2, 40 and 80 MHz: the low
 * nibble of D11(0x6c8) from the position of the control channel `prim`.
 */
static void edcrs_set_primary(struct bcm4360_mac *mac, u16 prim)
{
	u16 r = d11_read16(mac->hw, D11_REG_0x6c8);

	d11_write16(mac->hw, D11_REG_0x6c8, (r & EDCRS_KEEP) | prim);
}

/*
 * docs/re/spec/bmac-run.md R1, sub_06212d step 3 (40 and 80 MHz only): the
 * shared-memory copy (MAC revisions up to 40), then the sub-channel enables
 * `v` into D11(0x6c6).
 */
static void edcrs_set_subchannels(struct bcm4360_mac *mac, u16 v)
{
	u16 r;

	if (mac->mac_rev <= MAC_REV_40)
		bcm4360_shm_write(mac->hw, SHM_0x5a, v);
	r = d11_read16(mac->hw, D11_REG_0x6c6);
	d11_write16(mac->hw, D11_REG_0x6c6, (r & EDCRS_KEEP) | v);
}

/*
 * docs/re/spec/bmac-run.md R1, sub_06212d (wlc_bmac_ifsctl_ac_edcrs): the
 * AC-PHY variant. The energy-detect enables of the sub-channels for the
 * current bandwidth (and the primary channel's position at 40/80 MHz), then
 * the PHY's dynamic-bandwidth scrambler setting.
 */
static void wlc_bmac_ifsctl_ac_edcrs(struct bcm4360_mac *mac, bool on)
{
	u16 bw = mac->chanspec & CHSPEC_BW_MASK;
	u16 sb = mac->chanspec & CHSPEC_SB_MASK;
	u16 v, n;

	/* step 1 */
	v = on ? EDCRS_ON : EDCRS_OFF;

	/* step 2, with step 3 for 40 and 80 MHz */
	switch (bw) {
	case CHSPEC_BW_20:
		n = (d11_read16(mac->hw, D11_REG_0x6c6) & EDCRS_KEEP) | (v & EDCRS_SUBCH_MASK);
		d11_write16(mac->hw, D11_REG_0x6c6, n);
		bcm4360_shm_write(mac->hw, SHM_0x5a, n);
		break;
	case CHSPEC_BW_40:
		v &= EDCRS_SUBCH_MASK_40;
		edcrs_set_primary(mac, sb == CHSPEC_SB_LL ? EDCRS_PRIM_40_LOWER :
							    EDCRS_PRIM_40_UPPER);
		edcrs_set_subchannels(mac, v);
		break;
	case CHSPEC_BW_80:
		v &= EDCRS_SUBCH_MASK;
		edcrs_set_primary(mac, (sb == CHSPEC_SB_LL || sb == CHSPEC_SB_LU) ?
				       EDCRS_PRIM_80_LOWER : EDCRS_PRIM_80_UPPER);
		edcrs_set_subchannels(mac, v);
		break;
	default:
		break;		/* any other bandwidth: no register access */
	}

	/* step 4: the PHY of the current band, in every case */
	bcm4360_bmac_env_phy_scramb_dyn_bw_en(mac, on);
}

/*
 * docs/re/spec/bmac-run.md R1 (wlc_bmac_ifsctl_edcrs_set): the energy detect
 * of the IFS block for the current bandwidth. On the AC-PHY the net effect is
 * sub_06212d(.., 1) whatever `on` is.
 */
void bcm4360_bmac_ifsctl_edcrs_set(struct bcm4360_mac *mac, bool on)
{
	/* step 1: the N-PHY (type 4) from MAC revision 16 and the HT-PHY (7)
	 * have their own steps below; they are not this card. Any other type:
	 * return. */
	if (mac->band_phytype != PHY_TYPE_AC)
		return;

	/* step 2 (N-PHY revisions <= 2 with `on`: sub_0622d9(0x08, 0); not this card) */
	if (!on)
		wlc_bmac_ifs_ctl_edcrs(mac, IFS_EDCRS_0x08, IFS_EDCRS_0x08);

	/* step 3: t = 11 (AC): the constant 1. (HT-PHY and N-PHY revisions >= 3:
	 * sub_0622d9(0x38, 20 MHz ? 0x08 : 0x38); not this card.) */
	wlc_bmac_ifsctl_ac_edcrs(mac, true);
}

/* ---- R2: transmit FIFO suspend and resume ---- */

/* docs/re/spec/bmac-run.md R2, suspend step 1: (1 << fifo) truncated to 8 bits */
static u8 fifo_bit(u32 fifo)
{
	return fifo < FIFO_BITS ? (u8)BIT(fifo) : 0;
}

/* docs/re/spec/bmac-run.md R2: transmit FIFO `fifo` has a DMA engine */
static bool fifo_has_dma(struct bcm4360_mac *mac, u32 fifo)
{
	return (mac->dma_fifos & fifo_bit(fifo)) != 0;
}

/*
 * docs/re/spec/bmac-run.md R2, suspend steps 6 and 8: the PHY types for which
 * the MAC is suspended around the DMA suspend (always on this card).
 */
static bool phytype_suspends_mac(u16 t)
{
	return t == PHY_TYPE_N || t == PHY_TYPE_LP || t == PHY_TYPE_HT ||
	       t == PHY_TYPE_SSN || t == PHY_TYPE_LCN || t == PHY_TYPE_AC;
}

/* docs/re/spec/bmac-run.md R2, resume step 2: an 8-bit rotate left of `v` by `n` */
static u8 fifo_rotl8(u8 v, u32 n)
{
	n &= FIFO_BITS - 1;
	return (u8)((v << n) | (v >> (FIFO_BITS - n)));
}

/*
 * docs/re/spec/bmac-run.md R2 (wlc_bmac_tx_fifo_suspend): mark transmit FIFO
 * `fifo` suspended (the first one keeps the microcode awake) and suspend its
 * DMA engine, with the MAC suspended around it if it was enabled.
 */
void bcm4360_bmac_tx_fifo_suspend(struct bcm4360_mac *mac, u32 fifo)
{
	u8 bit = fifo_bit(fifo);
	bool en;

	/* step 1: already suspended - no access */
	if ((mac->suspended_fifos & bit) == bit)
		return;

	/* step 2 */
	if (mac->suspended_fifos == 0)
		bcm4360_bmac_wake_override_set(mac, WAKE_REASON_TX_FIFO);

	/* step 3 */
	mac->suspended_fifos |= bit;

	/* step 4 */
	if (!fifo_has_dma(mac, fifo))
		return;

	/* step 5 */
	en = (d11_read32(mac->hw, D11_MACCONTROL) & MCTL_EN_MAC) != 0;

	/* step 6 */
	if (phytype_suspends_mac(mac->band_phytype) && en)
		bcm4360_bmac_suspend(mac);

	/* step 7: slot 0x20 */
	bcm4360_bmac_env_dma_txsuspend(mac, fifo);

	/* step 8: the PHY type read again */
	if (phytype_suspends_mac(mac->band_phytype) && en)
		bcm4360_bmac_enable(mac);
}

/*
 * docs/re/spec/bmac-run.md R2 (wlc_bmac_tx_fifo_resume): resume the DMA engine
 * of `fifo` (even if it was not marked), then the bookkeeping; the last one
 * lets the microcode sleep again. The MAC is not touched.
 */
void bcm4360_bmac_tx_fifo_resume(struct bcm4360_mac *mac, u32 fifo)
{
	/* step 1: slot 0x28 */
	if (fifo_has_dma(mac, fifo))
		bcm4360_bmac_env_dma_txresume(mac, fifo);

	/* step 2: only the bit itself is cleared (0xfe rotated by `fifo`) */
	if (mac->suspended_fifos == 0)
		return;
	mac->suspended_fifos &= fifo_rotl8(0xfe, fifo);
	if (mac->suspended_fifos == 0)
		bcm4360_bmac_wake_override_clear(mac, WAKE_REASON_TX_FIFO);
}

/*
 * docs/re/spec/bmac-run.md R2 (wlc_bmac_tx_fifo_suspended): the DMA engine of
 * `fifo` is suspended and its transmit channel is no longer busy. The engine
 * is asked without checking that it exists (callers pass a FIFO with one;
 * the service answers false for one without).
 */
bool bcm4360_bmac_tx_fifo_suspended(struct bcm4360_mac *mac, u32 fifo)
{
	/* step 1: slot 0x30 */
	if (!bcm4360_bmac_env_dma_txsuspended(mac, fifo))
		return false;

	/* step 2: bit `fifo` of D11(0x150) clear */
	return (d11_read32(mac->hw, D11_CHNSTATUS) & BIT(fifo)) == 0;
}

/* ---- R5: CCA statistics ---- */

/*
 * docs/re/spec/bmac-run.md R5, sub_061ad3 (wlc_bmac_read_shm32): a 32-bit
 * counter of the microcode as two 16-bit shared-memory words at base + off
 * (low) and + 2 (high), read again once if the high word changed in between.
 */
static u32 wlc_bmac_read_shm32(struct bcm4360_mac *mac, u16 base, u16 off)
{
	u16 a = base + off;
	u16 hi, lo, hi2;

	/* step 1 */
	hi = bcm4360_shm_read(mac->hw, a + SHM_CCA_HI);
	lo = bcm4360_shm_read(mac->hw, a);
	hi2 = bcm4360_shm_read(mac->hw, a + SHM_CCA_HI);

	/* step 2 */
	if (hi != hi2) {
		hi = bcm4360_shm_read(mac->hw, a + SHM_CCA_HI);
		lo = bcm4360_shm_read(mac->hw, a);
	}

	/* step 3 */
	return (u32)hi << 16 | lo;
}

/*
 * docs/re/spec/bmac-run.md R5 (cca_stats_read): the snapshot of the
 * microcode's channel-occupancy counters, 9 words, in the order of the
 * specification's table (word 5, the low word of the TSF, last).
 */
void bcm4360_bmac_cca_stats_read(struct bcm4360_mac *mac, u32 out[9])
{
	u16 base = mac->mac_rev >= MAC_REV_40 ? SHM_CCA_BASE : SHM_CCA_BASE_OLD;
	u32 tsf_hi;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(cca_counter_off); i++)
		out[cca_counter_word[i]] = wlc_bmac_read_shm32(mac, base, cca_counter_off[i]);
	out[7] = bcm4360_shm_read(mac->hw, SHM_0x10e);
	out[8] = bcm4360_shm_read(mac->hw, SHM_0x10c);

	/* word 5: the high word of the TSF is read and dropped */
	bcm4360_bmac_read_tsf(mac, &out[CCA_TSF_WORD], &tsf_hi);
}

/* docs/re/spec/bmac-run.md R5 (wlc_bmac_cca_stats_read): cca_stats_read, result 0 */
s32 bcm4360_bmac_cca_stats_get(struct bcm4360_mac *mac, u32 out[9])
{
	bcm4360_bmac_cca_stats_read(mac, out);
	return 0;
}

/* ---- R6: rate shared-memory offsets ---- */

/*
 * docs/re/spec/bmac-run.md R6 (wlc_bmac_rate_shm_offset): the byte offset of
 * the microcode's rate-table entry of `rate` (500 kbit/s units), from the
 * OFDM or the CCK directory. A rate not in the table (also above 127, Q3)
 * reads the first word of the CCK directory.
 */
u32 bcm4360_bmac_rate_shm_offset(struct bcm4360_mac *mac, u8 rate)
{
	u8 c = rate < RATE_INFO_SIZE ? rate_info[rate] : 0;	/* step 1 */
	u16 dir = (c & RATE_INFO_OFDM) ? SHM_RATE_DIR_OFDM : SHM_RATE_DIR_CCK;	/* step 2 */

	/* step 3: the directory word is a word address */
	return 2 * (u32)bcm4360_shm_read(mac->hw, dir + 2 * (c & RATE_INFO_IDX));
}

/*
 * docs/re/spec/bmac-run.md R6 (wlc_bmac_stf_get_rateset_shm_offset): for each
 * of the `count` rates its entry offset and (MAC revisions >= 40) the word at
 * `off` in that entry, into the 6-byte entries; byte +1 is not written.
 */
void bcm4360_bmac_stf_get_rateset_shm_offset(struct bcm4360_mac *mac, const u8 *rates,
					     u32 count, u16 off, u8 *entries)
{
	u32 i;

	for (i = 0; i < count; i++) {
		u8 *e = entries + RS_ENTRY_BYTES * i;
		u8 r = rates[i] & RATE_MASK;				/* step 1 */
		u32 o = bcm4360_bmac_rate_shm_offset(mac, r);

		if (mac->mac_rev >= MAC_REV_40)				/* step 2 */
			entry_store_le16(e + RS_ENTRY_VALUE,
					 shm_read_wide(mac, (o & 0xffff) + off));
		entry_store_le16(e + RS_ENTRY_OFFSET, (u16)o);		/* step 3 */
		e[RS_ENTRY_RATE] = r;
	}
}

/*
 * docs/re/spec/bmac-run.md R6 (wlc_bmac_stf_set_rateset_shm_offset): write
 * the value of each of the `count` entries to the word at `off` in its rate's
 * table entry.
 */
void bcm4360_bmac_stf_set_rateset_shm_offset(struct bcm4360_mac *mac, u32 count, u16 off,
					     const u8 *entries)
{
	u32 i;

	for (i = 0; i < count; i++) {
		const u8 *e = entries + RS_ENTRY_BYTES * i;

		shm_write_wide(mac, (u32)off + entry_le16(e + RS_ENTRY_OFFSET),
			       entry_le16(e + RS_ENTRY_VALUE));
	}
}

/* ---- R7: TSF ---- */

/*
 * docs/re/spec/bmac-run.md R7 (wlc_bmac_read_tsf): the TSF, low word first.
 * The low word is read always, the high word only if it is wanted.
 */
void bcm4360_bmac_read_tsf(struct bcm4360_mac *mac, u32 *lo, u32 *hi)
{
	u32 v = d11_read32(mac->hw, D11_TSF_TIMERLOW);

	if (lo)
		*lo = v;
	if (hi)
		*hi = d11_read32(mac->hw, D11_TSF_TIMERHIGH);
}
