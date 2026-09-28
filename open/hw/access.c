// SPDX-License-Identifier: ISC
/*
 * Register access of the BCM4360: PHY registers, radio registers, PHY tables
 * and the shared memory of driver and microcode.
 *
 * Written from docs/re/spec/access.md, for MAC core revision 42 on a PCIe
 * card with an AC-PHY.
 */
#include <bcm4360/access.h>

/* a value of 32 bit is transferred as two halves of 16 bit */
#define HALF_BITS		16
#define HALF_MASK		0xffff

/* number of 16 bit words of a table entry of 48 bit and of 60 or 64 bit */
#define TBL_WORDS_48		3
#define TBL_WORDS_64		4

/*
 * Shared memory: the object memory is addressed in words of 32 bit, a byte
 * offset selects one half of such a word.
 */
#define SHM_WORD_SHIFT		2	/* byte offset -> address of the word */
#define SHM_UPPER_HALF		0x2	/* bit of the byte offset: upper 16 bit */

/* docs/re/spec/access.md, "PHY tables": the lower half of a 32 bit value */
static u16 lower_half(u32 val)
{
	return val & HALF_MASK;
}

/* docs/re/spec/access.md, "PHY tables": the upper half of a 32 bit value */
static u16 upper_half(u32 val)
{
	return val >> HALF_BITS;
}

/* docs/re/spec/access.md, "PHY tables": a 32 bit value from its halves */
static u32 join_halves(u16 lower, u16 upper)
{
	return (u32)upper << HALF_BITS | lower;
}

/* docs/re/spec/access.md, "PHY registers", write pacing: the state at start */
void bcm4360_phy_io_init(struct bcm4360_phy_io *io, struct bcm4360_hw *hw,
			 bool pci, u16 write_limit)
{
	io->hw = hw;
	io->pci = pci;
	io->write_limit = write_limit;
	io->write_count = 0;
}

/*
 * docs/re/spec/access.md, "PHY registers", write pacing: count a write of
 * phy_reg_write or write_radio_reg. Returns true when a dummy read has to be
 * made before the write.
 */
static bool phy_io_pace_write(struct bcm4360_phy_io *io)
{
	if (!io->pci)
		return false;
	if (++io->write_count < io->write_limit)
		return false;
	io->write_count = 0;
	return true;
}

/*
 * docs/re/spec/access.md, "PHY registers", write pacing: a read ends the row
 * of writes.
 */
static void phy_io_pace_read(struct bcm4360_phy_io *io)
{
	io->write_count = 0;
}

/* docs/re/spec/access.md, "PHY registers", read (phy_reg_read) */
u16 bcm4360_phy_read(struct bcm4360_phy_io *io, u16 addr)
{
	phy_io_pace_read(io);
	d11_write16(io->hw, D11_PHY_ADDR, addr);
	return d11_read16(io->hw, D11_PHY_DATA);
}

/*
 * docs/re/spec/access.md, "PHY registers", write (phy_reg_write): address and
 * data register are adjacent, one write sets both.
 */
void bcm4360_phy_write(struct bcm4360_phy_io *io, u16 addr, u16 val)
{
	if (phy_io_pace_write(io))
		(void)d11_read16(io->hw, D11_PHY_VERSION);
	d11_write32(io->hw, D11_PHY_ADDR, join_halves(addr, val));
}

/* docs/re/spec/access.md, "PHY registers", phy_reg_mod */
void bcm4360_phy_mod(struct bcm4360_phy_io *io, u16 addr, u16 mask, u16 val)
{
	u16 old = bcm4360_phy_read(io, addr);

	d11_write16(io->hw, D11_PHY_DATA, (old & ~mask) | (val & mask));
}

/* docs/re/spec/access.md, "PHY registers", phy_reg_and */
void bcm4360_phy_and(struct bcm4360_phy_io *io, u16 addr, u16 val)
{
	u16 old = bcm4360_phy_read(io, addr);

	d11_write16(io->hw, D11_PHY_DATA, old & val);
}

/* docs/re/spec/access.md, "PHY registers", phy_reg_or */
void bcm4360_phy_or(struct bcm4360_phy_io *io, u16 addr, u16 val)
{
	u16 old = bcm4360_phy_read(io, addr);

	d11_write16(io->hw, D11_PHY_DATA, old | val);
}

/* docs/re/spec/access.md, "Radio registers", read (read_radio_reg) */
u16 bcm4360_radio_read(struct bcm4360_phy_io *io, u16 addr)
{
	phy_io_pace_read(io);
	d11_write16(io->hw, D11_RADIO_ADDR, addr);
	return d11_read16(io->hw, D11_RADIO_DATA);
}

/*
 * docs/re/spec/access.md, "Radio registers", write (write_radio_reg); its
 * pacing is specified in "PHY registers".
 */
void bcm4360_radio_write(struct bcm4360_phy_io *io, u16 addr, u16 val)
{
	if (phy_io_pace_write(io))
		(void)d11_read32(io->hw, D11_REG_0x120);
	d11_write16(io->hw, D11_RADIO_ADDR, addr);
	d11_write16(io->hw, D11_RADIO_DATA, val);
}

/*
 * docs/re/spec/access.md, "Radio registers"; mod_radio_reg is specified in
 * "PHY registers" (write pacing): a read_radio_reg, then a write_radio_reg.
 */
void bcm4360_radio_mod(struct bcm4360_phy_io *io, u16 addr, u16 mask, u16 val)
{
	u16 old = bcm4360_radio_read(io, addr);

	bcm4360_radio_write(io, addr, (old & ~mask) | (val & mask));
}

/*
 * docs/re/spec/access.md, "Radio registers"; and_radio_reg is specified in
 * "PHY registers" (write pacing): a read_radio_reg, then a write_radio_reg.
 */
void bcm4360_radio_and(struct bcm4360_phy_io *io, u16 addr, u16 val)
{
	u16 old = bcm4360_radio_read(io, addr);

	bcm4360_radio_write(io, addr, old & val);
}

/*
 * docs/re/spec/access.md, "Radio registers"; or_radio_reg is specified in
 * "PHY registers" (write pacing): a read_radio_reg, then a write_radio_reg.
 */
void bcm4360_radio_or(struct bcm4360_phy_io *io, u16 addr, u16 val)
{
	u16 old = bcm4360_radio_read(io, addr);

	bcm4360_radio_write(io, addr, old | val);
}

/*
 * docs/re/spec/access.md, "Radio registers"; xor_radio_reg is specified in
 * "PHY registers" (write pacing): a read_radio_reg, then a write_radio_reg.
 */
void bcm4360_radio_xor(struct bcm4360_phy_io *io, u16 addr, u16 val)
{
	u16 old = bcm4360_radio_read(io, addr);

	bcm4360_radio_write(io, addr, old ^ val);
}

/*
 * docs/re/spec/access.md, "PHY tables": the size of an entry of `width` bits
 * in the memory of the driver, in bytes; 0 if no table has such entries.
 */
static u32 tbl_entry_size(u32 width)
{
	switch (width) {
	case 8:
		return sizeof(u8);
	case 16:
		return sizeof(u16);
	case 32:
		return sizeof(u32);
	case 48:
		return TBL_WORDS_48 * sizeof(u16);
	case 60:
	case 64:
		return 2 * sizeof(u32);
	default:
		return 0;
	}
}

/* docs/re/spec/access.md, "PHY tables", step 1: the table and the first entry */
static void tbl_select(struct bcm4360_phy_io *io, u32 id, u32 offset)
{
	bcm4360_phy_write(io, ACPHY_TBL_ID, id);
	bcm4360_phy_write(io, ACPHY_TBL_OFFSET, offset);
}

/*
 * docs/re/spec/access.md, "PHY tables", step 2, 48, 60 and 64 bit: the first
 * word with a register write, the others directly to the data register.
 */
static void tbl_write_wide(struct bcm4360_phy_io *io, const u16 *words, u32 n)
{
	u32 i;

	bcm4360_phy_write(io, ACPHY_TBL_DATA_WIDE, words[0]);
	for (i = 1; i < n; i++)
		d11_write16(io->hw, D11_PHY_DATA, words[i]);
}

/*
 * docs/re/spec/access.md, "PHY tables", step 2, 48, 60 and 64 bit: the first
 * word with a register read, the others directly from the data register.
 */
static void tbl_read_wide(struct bcm4360_phy_io *io, u16 *words, u32 n)
{
	u32 i;

	words[0] = bcm4360_phy_read(io, ACPHY_TBL_DATA_WIDE);
	for (i = 1; i < n; i++)
		words[i] = d11_read16(io->hw, D11_PHY_DATA);
}

/* docs/re/spec/access.md, "PHY tables", step 2: write one entry */
static void tbl_write_entry(struct bcm4360_phy_io *io, u32 width, const void *entry)
{
	const u32 *val = entry;
	u16 words[TBL_WORDS_64];

	switch (width) {
	case 8:
		bcm4360_phy_write(io, ACPHY_TBL_DATA_LO, *(const u8 *)entry);
		break;
	case 16:
		bcm4360_phy_write(io, ACPHY_TBL_DATA_LO, *(const u16 *)entry);
		break;
	case 32:
		bcm4360_phy_write(io, ACPHY_TBL_DATA_HI, upper_half(val[0]));
		bcm4360_phy_write(io, ACPHY_TBL_DATA_LO, lower_half(val[0]));
		break;
	case 48:
		tbl_write_wide(io, entry, TBL_WORDS_48);
		break;
	case 60:
	case 64:
		words[0] = lower_half(val[0]);
		words[1] = upper_half(val[0]);
		words[2] = lower_half(val[1]);
		words[3] = upper_half(val[1]);
		tbl_write_wide(io, words, TBL_WORDS_64);
		break;
	}
}

/* docs/re/spec/access.md, "PHY tables", step 2: read one entry */
static void tbl_read_entry(struct bcm4360_phy_io *io, u32 width, void *entry)
{
	u32 *val = entry;
	u16 words[TBL_WORDS_64];

	switch (width) {
	case 8:
		*(u8 *)entry = bcm4360_phy_read(io, ACPHY_TBL_DATA_LO);
		break;
	case 16:
		*(u16 *)entry = bcm4360_phy_read(io, ACPHY_TBL_DATA_LO);
		break;
	case 32:
		words[0] = bcm4360_phy_read(io, ACPHY_TBL_DATA_LO);
		words[1] = bcm4360_phy_read(io, ACPHY_TBL_DATA_HI);
		val[0] = join_halves(words[0], words[1]);
		break;
	case 48:
		tbl_read_wide(io, entry, TBL_WORDS_48);
		break;
	case 60:
	case 64:
		tbl_read_wide(io, words, TBL_WORDS_64);
		val[0] = join_halves(words[0], words[1]);
		val[1] = join_halves(words[2], words[3]);
		break;
	}
}

/*
 * docs/re/spec/access.md, "PHY tables": write a transfer
 * (wlc_phy_table_write_acphy)
 */
void bcm4360_tbl_write(struct bcm4360_phy_io *io, u32 id, u32 n, u32 offset,
		       u32 width, const void *data)
{
	u32 size = tbl_entry_size(width);
	const u8 *entry = data;
	u32 i;

	if (!size)
		return;

	tbl_select(io, id, offset);
	for (i = 0; i < n; i++, entry += size)
		tbl_write_entry(io, width, entry);
}

/*
 * docs/re/spec/access.md, "PHY tables": read a transfer
 * (wlc_phy_table_read_acphy)
 */
void bcm4360_tbl_read(struct bcm4360_phy_io *io, u32 id, u32 n, u32 offset,
		      u32 width, void *data)
{
	u32 size = tbl_entry_size(width);
	u8 *entry = data;
	u32 i;

	if (!size)
		return;

	tbl_select(io, id, offset);
	for (i = 0; i < n; i++, entry += size)
		tbl_read_entry(io, width, entry);
}

/*
 * docs/re/spec/access.md, "Object memories of the MAC": select the word at
 * byte offset `offset` of the shared memory. Returns the half of the data
 * register to access.
 */
static u32 shm_select(struct bcm4360_hw *hw, u16 offset)
{
	u32 sel = D11_OBJ_MEM_SHM << D11_OBJ_MEM_SHIFT;

	d11_write32(hw, D11_OBJ_ADDR, sel | (offset >> SHM_WORD_SHIFT));
	(void)d11_read32(hw, D11_OBJ_ADDR);	/* the flush */
	return (offset & SHM_UPPER_HALF) ? D11_OBJ_DATA_HI : D11_OBJ_DATA_LO;
}

/* docs/re/spec/access.md, "Object memories of the MAC": read a word of the shared memory */
u16 bcm4360_shm_read(struct bcm4360_hw *hw, u16 offset)
{
	u32 reg = shm_select(hw, offset);

	return d11_read16(hw, reg);
}

/* docs/re/spec/access.md, "Object memories of the MAC": write a word of the shared memory */
void bcm4360_shm_write(struct bcm4360_hw *hw, u16 offset, u16 val)
{
	u32 reg = shm_select(hw, offset);

	d11_write16(hw, reg, val);
}
