// SPDX-License-Identifier: ISC
/*
 * The object memories of the 802.11 core (wlc_bmac.c) - shared memory,
 * scratch registers of the microcode, internal hardware registers, address
 * match table, template RAM - and the state setters that write through them:
 * contention window, hardware MAC address, receive address match, retry
 * limits, register initialisation lists.
 *
 * Written from docs/re/spec/bmac-init.md, sections A8, B4, B5, B6, B11, E1,
 * E3, E7, F1, F2 and the introduction of section F, for MAC core revision 42
 * (chip 0x4360, PCIe).
 */
#include <bcm4360/mac.h>
#include <bcm4360/access.h>

/*
 * Selectors of the object memories: memory number << 16, written into the
 * high bits of D11(0x160) (docs/re/spec/bmac-init.md, section F).
 */
#define OBJ_SEL_SHM		(D11_OBJ_MEM_SHM << D11_OBJ_MEM_SHIFT)		/* 0x10000 */
#define OBJ_SEL_SCRATCH		(D11_OBJ_MEM_SCRATCH << D11_OBJ_MEM_SHIFT)	/* 0x20000 */
#define OBJ_SEL_IHR		(D11_OBJ_MEM_IHR << D11_OBJ_MEM_SHIFT)		/* 0x30000 */
#define OBJ_SEL_AMT		(D11_OBJ_MEM_AMT << D11_OBJ_MEM_SHIFT)		/* 0x40000 */

/* B4: a selector with bit 18 is transferred in 32-bit words */
#define OBJ_SEL_WORD32		BIT(18)

/*
 * The address written into D11(0x160) is selector | (byte offset >> 2); bit 1
 * of the byte offset selects the upper 16 bits of the data register (A8).
 */
#define OBJ_WORD_SHIFT		2
#define OBJ_UPPER_HALF		0x2

/* Bytes of one 16-bit and of one 32-bit object-memory word (B4) */
#define OBJ_HALF_BYTES		2
#define OBJ_WORD_BYTES		4

/*
 * Scratch registers of the microcode and internal hardware registers are
 * numbered: register n is at byte offset 4n (section F; G, last paragraph).
 */
#define OBJ_REG_OFFSET(n)	((u32)(n) << OBJ_WORD_SHIFT)
#define SCR_CWMIN		3	/* contention window minimum (B11) */
#define SCR_CWMAX		4	/* contention window maximum (B11) */
#define SCR_SRL			6	/* short retry limit (E7) */
#define SCR_LRL			7	/* long retry limit (E7) */

/*
 * The address match table of MAC revisions >= 40: 64 entries of 8 bytes, a
 * MAC address and a 16-bit attribute word (B5, E3).
 */
#define AMT_ENTRY_BYTES		8
#define AMT_ATTR_OFFSET		6	/* the attribute word within an entry */
#define AMT_ATTR_OR		0x8000	/* attribute bit 15: OR with the existing entry */
#define AMT_ATTR_BIT2		0x0004	/* attribute bit 2, set by E3 (purpose not specified) */
#define AMT_MIN_MAC_REV		40	/* first MAC revision with the address match table */
#define MAC_ADDR_BYTES		6

/* The older RCMTA table (MAC revisions below 40, E3): entry n at byte 8n */
#define RCMTA_ENTRY_SHIFT	3

/* Template RAM (F2) */
#define D11_MACCONTROL		0x120		/* MAC control register */
#define D11_TPL_WRPTR		0x130		/* template write pointer (32 bit) */
#define D11_TPL_WRDATA		0x134		/* template write data (32 bit) */
#define MCTL_BIGEND		0x00010000	/* bit 16: byte-swap the template words */

/*
 * Register initialisation lists (B6, docs/re/01-anatomy.md): entries of 8
 * bytes, little endian: 16-bit register offset, 16-bit access size, 32-bit
 * value; the list ends with offset 0xffff.
 */
#define INITS_ENTRY_BYTES	8
#define INITS_OFF_OFFSET	0
#define INITS_SIZE_OFFSET	2
#define INITS_VALUE_OFFSET	4
#define INITS_END		0xffff
#define INITS_SIZE_16		2
#define INITS_SIZE_32		4

/* docs/re/spec/bmac-init.md, B4: a 16-bit word from two bytes, low byte first */
static u16 bytes_le16(const u8 *p)
{
	return (u16)(p[0] | p[1] << 8);
}

/* docs/re/spec/bmac-init.md, B4: a 32-bit word from four bytes, low byte first */
static u32 bytes_le32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* docs/re/spec/bmac-init.md, B4: store a 32-bit word as four bytes, low byte first */
static void store_le32(u8 *p, u32 v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = v >> 24;
}

/*
 * docs/re/spec/bmac-init.md, A8 (tcm_sem_enter): mutual exclusion with the
 * offload firmware of the on-chip ARM; nothing to do without it.
 */
static void tcm_sem_enter(struct bcm4360_mac *mac)
{
	if (mac->sem_enter)
		mac->sem_enter(mac->sem_ctx);
}

/* docs/re/spec/bmac-init.md, A8 (tcm_sem_exit): the counterpart of tcm_sem_enter */
static void tcm_sem_exit(struct bcm4360_mac *mac)
{
	if (mac->sem_exit)
		mac->sem_exit(mac->sem_ctx);
}

/*
 * docs/re/spec/bmac-init.md, A8 step 2 (and B4, 32-bit words): select the
 * object-memory word of `offset` in memory `sel`: address write, then one
 * read back of D11(0x160) (the value is not used).
 */
static void obj_select(struct bcm4360_mac *mac, u32 offset, u32 sel)
{
	d11_write32(mac->hw, D11_OBJ_ADDR, sel | (offset >> OBJ_WORD_SHIFT));
	(void)d11_read32(mac->hw, D11_OBJ_ADDR);
}

/* docs/re/spec/bmac-init.md, A8 step 3: the half of the data register for `offset` */
static u32 obj_data_reg(u32 offset)
{
	return (offset & OBJ_UPPER_HALF) ? D11_OBJ_DATA_HI : D11_OBJ_DATA_LO;
}

/*
 * docs/re/spec/bmac-init.md, A8, sub_06188a (wlc_bmac_read_objmem): one 16-bit
 * word of any object memory, with the semaphore around the whole access. Bit
 * 0 of `offset` is ignored.
 */
static u16 wlc_bmac_read_objmem(struct bcm4360_mac *mac, u32 offset, u32 sel)
{
	u16 v;

	tcm_sem_enter(mac);
	obj_select(mac, offset, sel);
	v = d11_read16(mac->hw, obj_data_reg(offset));
	tcm_sem_exit(mac);
	return v;
}

/*
 * docs/re/spec/bmac-init.md, A8, sub_061ed5 (wlc_bmac_write_objmem): write
 * one 16-bit word of any object memory, with the semaphore around the whole
 * access. Bit 0 of `offset` is ignored.
 */
static void wlc_bmac_write_objmem(struct bcm4360_mac *mac, u32 offset, u16 v, u32 sel)
{
	tcm_sem_enter(mac);
	obj_select(mac, offset, sel);
	d11_write16(mac->hw, obj_data_reg(offset), v);
	tcm_sem_exit(mac);
}

/*
 * docs/re/spec/bmac-init.md, A8 (wlc_bmac_set_shm): the same value to the
 * shared-memory words at offset, offset + 2, ... while the distance is less
 * than `len` bytes, each with a complete worker call. Nothing for len <= 0.
 */
void bcm4360_bmac_set_shm(struct bcm4360_mac *mac, u32 offset, u16 v, s32 len)
{
	s32 i;

	for (i = 0; i < len; i += OBJ_HALF_BYTES)
		wlc_bmac_write_objmem(mac, offset + i, v, OBJ_SEL_SHM);
}

/*
 * docs/re/spec/bmac-init.md, B4 (wlc_bmac_copyto_objmem): copy `len` bytes
 * to object memory `sel` at byte offset `offset`. 16-bit words with the
 * worker of A8, an odd `len` rounded up (one byte beyond the buffer is read);
 * for the address match table (selector bit 18) 32-bit words, each with the
 * semaphore around it, and one 16-bit word more for a remainder of len & 3.
 * Nothing for len <= 0.
 */
void bcm4360_bmac_copyto_objmem(struct bcm4360_mac *mac, u32 offset, const void *buf,
				s32 len, u32 sel)
{
	const u8 *p = buf;
	s32 i;

	if (len <= 0)
		return;

	if (!(sel & OBJ_SEL_WORD32)) {
		for (i = 0; i < len; i += OBJ_HALF_BYTES)
			wlc_bmac_write_objmem(mac, offset + i, bytes_le16(p + i), sel);
		return;
	}

	for (i = 0; i < (len & ~(OBJ_WORD_BYTES - 1)); i += OBJ_WORD_BYTES) {
		tcm_sem_enter(mac);
		obj_select(mac, offset + i, sel);
		d11_write32(mac->hw, D11_OBJ_DATA_LO, bytes_le32(p + i));
		tcm_sem_exit(mac);
	}
	if (len & (OBJ_WORD_BYTES - 1))
		wlc_bmac_write_objmem(mac, offset + i, bytes_le16(p + i), sel);
}

/*
 * docs/re/spec/bmac-init.md, B4: store a 16-bit word read from an object
 * memory, low byte first; a byte that would lie beyond `len` is not stored
 * (see docs/re/questions/bmac-mem.md).
 */
static void obj_store16(u8 *p, s32 i, s32 len, u16 v)
{
	p[i] = v & 0xff;
	if (i + 1 < len)
		p[i + 1] = v >> 8;
}

/*
 * docs/re/spec/bmac-init.md, B4 (wlc_bmac_copyfrom_objmem): the reading
 * counterpart of bcm4360_bmac_copyto_objmem, bytes stored low byte first.
 */
void bcm4360_bmac_copyfrom_objmem(struct bcm4360_mac *mac, u32 offset, void *buf,
				  s32 len, u32 sel)
{
	u8 *p = buf;
	s32 i;

	if (len <= 0)
		return;

	if (!(sel & OBJ_SEL_WORD32)) {
		for (i = 0; i < len; i += OBJ_HALF_BYTES)
			obj_store16(p, i, len, wlc_bmac_read_objmem(mac, offset + i, sel));
		return;
	}

	for (i = 0; i < (len & ~(OBJ_WORD_BYTES - 1)); i += OBJ_WORD_BYTES) {
		tcm_sem_enter(mac);
		obj_select(mac, offset + i, sel);
		store_le32(p + i, d11_read32(mac->hw, D11_OBJ_DATA_LO));
		tcm_sem_exit(mac);
	}
	if (len & (OBJ_WORD_BYTES - 1))
		obj_store16(p, i, len, wlc_bmac_read_objmem(mac, offset + i, sel));
}

/*
 * docs/re/spec/bmac-init.md, B5, sub_061e23 (wlc_bmac_read_amt): read entry
 * `idx` of the address match table (two 32-bit reads as in B4).
 */
static void wlc_bmac_read_amt(struct bcm4360_mac *mac, s32 idx, u8 *addr, u16 *attr)
{
	u8 entry[AMT_ENTRY_BYTES];
	u32 i;

	bcm4360_bmac_copyfrom_objmem(mac, (u32)idx * AMT_ENTRY_BYTES, entry,
				     AMT_ENTRY_BYTES, OBJ_SEL_AMT);
	for (i = 0; i < MAC_ADDR_BYTES; i++)
		addr[i] = entry[i];
	*attr = bytes_le16(entry + AMT_ATTR_OFFSET);
}

/*
 * docs/re/spec/bmac-init.md, B5 (wlc_bmac_write_amt): write entry `idx` of the
 * address match table: the MAC address and the attribute word. With attribute
 * bit 15 the entry is read first and its attribute word ORed in.
 */
void bcm4360_bmac_write_amt(struct bcm4360_mac *mac, s32 idx, const u8 *addr, u16 attr)
{
	u8 entry[AMT_ENTRY_BYTES];
	u8 old_addr[MAC_ADDR_BYTES];
	u16 old_attr;
	u32 i;

	/* step 1 */
	if (attr & AMT_ATTR_OR) {
		wlc_bmac_read_amt(mac, idx, old_addr, &old_attr);
		attr |= old_attr;
	}

	/* step 2: word0 = address bytes 0..3, word1 = bytes 4, 5 and the attributes
	 * (bit 15 stays in the attribute word that is written) */
	for (i = 0; i < MAC_ADDR_BYTES; i++)
		entry[i] = addr[i];
	entry[AMT_ATTR_OFFSET] = attr & 0xff;
	entry[AMT_ATTR_OFFSET + 1] = attr >> 8;

	/* step 3 */
	bcm4360_bmac_copyto_objmem(mac, (u32)idx * AMT_ENTRY_BYTES, entry,
				   AMT_ENTRY_BYTES, OBJ_SEL_AMT);
}

/*
 * docs/re/spec/bmac-init.md, B6, sub_060f67 (wlc_write_inits): write a
 * register initialisation list: size 2 a 16-bit write of the low half of the
 * value, size 4 a 32-bit write, any other size skipped; up to the end marker.
 * No reads, no delays.
 */
static void wlc_write_inits(struct bcm4360_mac *mac, const u8 *entry)
{
	for (;; entry += INITS_ENTRY_BYTES) {
		u16 off = bytes_le16(entry + INITS_OFF_OFFSET);
		u16 size = bytes_le16(entry + INITS_SIZE_OFFSET);
		u32 v = bytes_le32(entry + INITS_VALUE_OFFSET);

		if (off == INITS_END)
			break;
		if (size == INITS_SIZE_16)
			d11_write16(mac->hw, off, v & 0xffff);
		else if (size == INITS_SIZE_32)
			d11_write32(mac->hw, off, v);
	}
}

/* docs/re/spec/bmac-init.md, B6 (wlc_bmac_write_inits): wrapper of wlc_write_inits */
void bcm4360_bmac_write_inits(struct bcm4360_mac *mac, const void *inits)
{
	wlc_write_inits(mac, inits);
}

/*
 * docs/re/spec/bmac-init.md, B11: write a 16-bit value to scratch register
 * `n` of the microcode (copyto_objmem of 2 bytes, low byte first).
 */
static void scratch_write(struct bcm4360_mac *mac, u32 n, u16 v)
{
	u8 b[OBJ_HALF_BYTES] = { v & 0xff, v >> 8 };

	bcm4360_bmac_copyto_objmem(mac, OBJ_REG_OFFSET(n), b, sizeof(b), OBJ_SEL_SCRATCH);
}

/*
 * docs/re/spec/bmac-init.md, B11 (wlc_bmac_set_cwmin): the contention window
 * minimum of the current band, kept and written to scratch register 3.
 */
void bcm4360_bmac_set_cwmin(struct bcm4360_mac *mac, u16 v)
{
	mac->cwmin = v;
	scratch_write(mac, SCR_CWMIN, v);
}

/*
 * docs/re/spec/bmac-init.md, B11 (wlc_bmac_set_cwmax): the contention window
 * maximum of the current band, kept and written to scratch register 4.
 */
void bcm4360_bmac_set_cwmax(struct bcm4360_mac *mac, u16 v)
{
	mac->cwmax = v;
	scratch_write(mac, SCR_CWMAX, v);
}

/*
 * docs/re/spec/bmac-init.md, E1 (wlc_bmac_set_hw_etheraddr): save the
 * hardware MAC address. No register access.
 */
void bcm4360_bmac_set_hw_etheraddr(struct bcm4360_mac *mac, const u8 *addr)
{
	u32 i;

	for (i = 0; i < MAC_ADDR_BYTES; i++)
		mac->hw_etheraddr[i] = addr[i];
}

/*
 * docs/re/spec/bmac-init.md, E1 (wlc_bmac_hw_etheraddr): the saved hardware
 * MAC address. No register access.
 */
void bcm4360_bmac_hw_etheraddr(struct bcm4360_mac *mac, u8 *out)
{
	u32 i;

	for (i = 0; i < MAC_ADDR_BYTES; i++)
		out[i] = mac->hw_etheraddr[i];
}

/* docs/re/spec/bmac-init.md, E3: the address is 00:00:00:00:00:00 */
static bool ether_addr_is_zero(const u8 *addr)
{
	u32 i;

	for (i = 0; i < MAC_ADDR_BYTES; i++)
		if (addr[i])
			return false;
	return true;
}

/*
 * docs/re/spec/bmac-init.md, E3 (wlc_bmac_set_rcmta): set a receive address
 * match entry. MAC revisions >= 40: an address match table entry with the
 * attributes 0 for the all-zero address, else 0x8004 (OR into the existing
 * attribute word, bit 2). Older revisions: the old RCMTA table.
 */
void bcm4360_bmac_set_rcmta(struct bcm4360_mac *mac, s32 idx, const u8 *addr)
{
	u16 attr;

	if (mac->mac_rev < AMT_MIN_MAC_REV) {
		bcm4360_bmac_copyto_objmem(mac, (u32)idx << RCMTA_ENTRY_SHIFT, addr,
					   MAC_ADDR_BYTES, OBJ_SEL_AMT);
		return;
	}

	attr = ether_addr_is_zero(addr) ? 0 : (AMT_ATTR_OR | AMT_ATTR_BIT2);
	bcm4360_bmac_write_amt(mac, idx, addr, attr);
}

/*
 * docs/re/spec/bmac-init.md, E7 (wlc_bmac_retrylimit_upd): keep the short and
 * long retry limits; when the interface is up write them to scratch
 * registers 6 and 7.
 */
void bcm4360_bmac_retrylimit_upd(struct bcm4360_mac *mac, u16 srl, u16 lrl)
{
	mac->srl = srl;
	mac->lrl = lrl;
	if (!mac->up)
		return;
	scratch_write(mac, SCR_SRL, srl);
	scratch_write(mac, SCR_LRL, lrl);
}

/*
 * docs/re/spec/bmac-init.md, F1 (wlc_bmac_write_ihr): a 16-bit write to
 * internal hardware register `reg` (copyto_objmem at reg << 2, selector
 * 0x30000).
 */
void bcm4360_bmac_write_ihr(struct bcm4360_mac *mac, u32 reg, u16 v)
{
	u8 b[OBJ_HALF_BYTES] = { v & 0xff, v >> 8 };

	bcm4360_bmac_copyto_objmem(mac, OBJ_REG_OFFSET(reg), b, sizeof(b), OBJ_SEL_IHR);
}

/* docs/re/spec/bmac-init.md, F2 step 3: the bytes of a 32-bit word swapped */
static u32 tpl_swap32(u32 v)
{
	return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
}

/*
 * docs/re/spec/bmac-init.md, F2 (wlc_bmac_write_template_ram): write `len`
 * bytes to the template RAM at byte offset `offset`: the write pointer, one
 * read of the MAC control register for the byte-swap bit, then one 32-bit
 * data write per 4 bytes (the pointer advances by itself). A length that is
 * not a multiple of 4 still transfers a last whole word.
 */
void bcm4360_bmac_write_template_ram(struct bcm4360_mac *mac, s32 offset, s32 len,
				     const void *buf)
{
	const u8 *p = buf;
	bool swap;
	s32 i;

	/* step 1 */
	d11_write32(mac->hw, D11_TPL_WRPTR, (u32)offset);

	/* step 2 */
	swap = (d11_read32(mac->hw, D11_MACCONTROL) & MCTL_BIGEND) != 0;

	/* step 3 */
	for (i = 0; i < len; i += OBJ_WORD_BYTES) {
		u32 w = bytes_le32(p + i);

		if (swap)
			w = tpl_swap32(w);
		d11_write32(mac->hw, D11_TPL_WRDATA, w);
	}
}
