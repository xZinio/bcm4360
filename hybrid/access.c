/*
 * Hybrid build: the register access primitives of Broadcom's core - PHY and
 * radio registers, PHY tables, shared memory - by the open code of
 * open/hw/access.c (docs/re/spec/access.md).
 *
 * Every function here has the name and the arguments of the function of the
 * object it takes over (hybrid/takeover.txt). It reads the state the
 * primitive needs from the object's structures (the offsets below, as the
 * specifications give them), calls the open primitive and writes the state
 * back. With the switch open_access off the object's own function runs
 * (bcm4360_blob_*).
 *
 * Integer arguments narrower than 32 bit arrive as u32 and are truncated
 * here: the object was compiled by GCC, which does not extend them.
 */
#include "hybrid.h"
#include <bcm4360/access.h>

/* phy_info_t (`pi`) and shared_phy_t (`sh`): docs/re/spec/acphy-attach.md, "Data" */
#define PI_SH			0x020	/* shared_phy_t * */
#define PI_REGS			0x148	/* registers of the 802.11 core */
#define PI_WREG_COUNT		0x226	/* PHY/radio writes in a row (access.md, write pacing) */
#define PI_WREG_LIMIT		0x228	/* phy_wreg_limit */
#define SH_BUSTYPE		0x06c	/* bus type; 1: PCI */
/* wlc_hw_info_t (`wlc_hw`): docs/re/spec/bmac-init.md */
#define WLC_HW_WLC		0x000	/* wlc_info_t * */
#define WLC_HW_REGS		0x0d0	/* registers of the 802.11 core */

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

HYB_TAKE(u32, phy_reg_read, (void *pi, u32 addr));
HYB_TAKE(void, phy_reg_write, (void *pi, u32 addr, u32 val));
HYB_TAKE(void, phy_reg_mod, (void *pi, u32 addr, u32 mask, u32 val));
HYB_TAKE(void, phy_reg_and, (void *pi, u32 addr, u32 val));
HYB_TAKE(void, phy_reg_or, (void *pi, u32 addr, u32 val));
HYB_TAKE(u32, read_radio_reg, (void *pi, u32 addr));
HYB_TAKE(void, write_radio_reg, (void *pi, u32 addr, u32 val));
HYB_TAKE(void, mod_radio_reg, (void *pi, u32 addr, u32 mask, u32 val));
HYB_TAKE(void, and_radio_reg, (void *pi, u32 addr, u32 val));
HYB_TAKE(void, or_radio_reg, (void *pi, u32 addr, u32 val));
HYB_TAKE(void, xor_radio_reg, (void *pi, u32 addr, u32 val));
HYB_TAKE(void, wlc_phy_table_write_acphy, (void *pi, u32 id, u32 n, u32 offset, u32 width,
					   const void *data));
HYB_TAKE(void, wlc_phy_table_read_acphy, (void *pi, u32 id, u32 n, u32 offset, u32 width,
					  void *data));
HYB_TAKE(u32, wlc_bmac_read_shm, (void *wlc_hw, u32 offset));
HYB_TAKE(void, wlc_bmac_write_shm, (void *wlc_hw, u32 offset, u32 val));

/* the semaphore shared with the on-chip ARM's offload firmware (bmac-init.md A8) */
void tcm_sem_enter(void *wlc);
void tcm_sem_exit(void *wlc);

/* the open state of the register access of the PHY described by `pi` */
static void phy_io(void *pi, struct bcm4360_hw *hw, struct bcm4360_phy_io *io)
{
	void *sh = FIELD(pi, PI_SH, void *);

	hw_memset(hw, 0, sizeof(*hw));
	hw->d11 = FIELD(pi, PI_REGS, void *);
	io->hw = hw;
	io->pci = FIELD(sh, SH_BUSTYPE, u32) == 1;
	io->write_limit = FIELD(pi, PI_WREG_LIMIT, u16);
	io->write_count = FIELD(pi, PI_WREG_COUNT, u16);
}

static void phy_io_done(void *pi, const struct bcm4360_phy_io *io)
{
	FIELD(pi, PI_WREG_COUNT, u16) = io->write_count;
}

#define ADAPT_READ(name, open_fn)					\
u32 name(void *pi, u32 addr)						\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_phy_io io;					\
	u16 v;								\
									\
	if (!bcm4360_open_access)					\
		return bcm4360_blob_##name(pi, addr);			\
	phy_io(pi, &hw, &io);						\
	v = open_fn(&io, (u16)addr);					\
	phy_io_done(pi, &io);						\
	return v;							\
}

#define ADAPT_WRITE(name, open_fn)					\
void name(void *pi, u32 addr, u32 val)					\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_phy_io io;					\
									\
	if (!bcm4360_open_access) {					\
		bcm4360_blob_##name(pi, addr, val);			\
		return;							\
	}								\
	phy_io(pi, &hw, &io);						\
	open_fn(&io, (u16)addr, (u16)val);				\
	phy_io_done(pi, &io);						\
}

#define ADAPT_MOD(name, open_fn)					\
void name(void *pi, u32 addr, u32 mask, u32 val)			\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_phy_io io;					\
									\
	if (!bcm4360_open_access) {					\
		bcm4360_blob_##name(pi, addr, mask, val);		\
		return;							\
	}								\
	phy_io(pi, &hw, &io);						\
	open_fn(&io, (u16)addr, (u16)mask, (u16)val);			\
	phy_io_done(pi, &io);						\
}

/* access.md, "PHY registers" */
ADAPT_READ(phy_reg_read, bcm4360_phy_read)
ADAPT_WRITE(phy_reg_write, bcm4360_phy_write)
ADAPT_MOD(phy_reg_mod, bcm4360_phy_mod)
ADAPT_WRITE(phy_reg_and, bcm4360_phy_and)
ADAPT_WRITE(phy_reg_or, bcm4360_phy_or)

/* access.md, "Radio registers" */
ADAPT_READ(read_radio_reg, bcm4360_radio_read)
ADAPT_WRITE(write_radio_reg, bcm4360_radio_write)
ADAPT_MOD(mod_radio_reg, bcm4360_radio_mod)
ADAPT_WRITE(and_radio_reg, bcm4360_radio_and)
ADAPT_WRITE(or_radio_reg, bcm4360_radio_or)
ADAPT_WRITE(xor_radio_reg, bcm4360_radio_xor)

/* access.md, "PHY tables" */
void wlc_phy_table_write_acphy(void *pi, u32 id, u32 n, u32 offset, u32 width,
			       const void *data)
{
	struct bcm4360_hw hw;
	struct bcm4360_phy_io io;

	if (!bcm4360_open_access) {
		bcm4360_blob_wlc_phy_table_write_acphy(pi, id, n, offset, width, data);
		return;
	}
	phy_io(pi, &hw, &io);
	bcm4360_tbl_write(&io, id, n, offset, width, data);
	phy_io_done(pi, &io);
}

void wlc_phy_table_read_acphy(void *pi, u32 id, u32 n, u32 offset, u32 width, void *data)
{
	struct bcm4360_hw hw;
	struct bcm4360_phy_io io;

	if (!bcm4360_open_access) {
		bcm4360_blob_wlc_phy_table_read_acphy(pi, id, n, offset, width, data);
		return;
	}
	phy_io(pi, &hw, &io);
	bcm4360_tbl_read(&io, id, n, offset, width, data);
	phy_io_done(pi, &io);
}

/*
 * Shared memory: access.md, "Object memories of the MAC"; bmac-init.md A8.
 * The object brackets every access with the offload semaphore; that part of
 * the upper layers stays the object's.
 */
u32 wlc_bmac_read_shm(void *wlc_hw, u32 offset)
{
	struct bcm4360_hw hw;
	void *wlc = FIELD(wlc_hw, WLC_HW_WLC, void *);
	u16 v;

	if (!bcm4360_open_access)
		return bcm4360_blob_wlc_bmac_read_shm(wlc_hw, offset);
	hw_memset(&hw, 0, sizeof(hw));
	hw.d11 = FIELD(wlc_hw, WLC_HW_REGS, void *);
	tcm_sem_enter(wlc);
	v = bcm4360_shm_read(&hw, (u16)offset);
	tcm_sem_exit(wlc);
	return v;
}

void wlc_bmac_write_shm(void *wlc_hw, u32 offset, u32 val)
{
	struct bcm4360_hw hw;
	void *wlc = FIELD(wlc_hw, WLC_HW_WLC, void *);

	if (!bcm4360_open_access) {
		bcm4360_blob_wlc_bmac_write_shm(wlc_hw, offset, val);
		return;
	}
	hw_memset(&hw, 0, sizeof(hw));
	hw.d11 = FIELD(wlc_hw, WLC_HW_REGS, void *);
	tcm_sem_enter(wlc);
	bcm4360_shm_write(&hw, (u16)offset, (u16)val);
	tcm_sem_exit(wlc);
}
