// SPDX-License-Identifier: ISC
/*
 * Hybrid bridge, layer "access": the register access primitives of
 * Broadcom's object are done by the open code (open/hw/access.c, written
 * from docs/re/spec/access.md).
 *
 * Every function here has the name and the arguments of the object's
 * function it stands in for. It reads the state the primitive needs from the
 * object's structures (blob_abi.h), calls the open primitive and writes the
 * state back - nothing else. The object's calls reach these functions because
 * its references to these names are pointed here: by tools/re/hybrid.py in
 * the emulator, by the linker in the HYBRID build of the module.
 *
 * Integer arguments narrower than 32 bit are taken as u32 and truncated
 * here: the object was compiled by GCC, which does not extend them, and the
 * open code may be compiled by a compiler that relies on the extension.
 */
#include <bcm4360/access.h>
#include "blob_abi.h"

/* the open state of the access of the PHY described by the object's `pi` */
static void phy_io(void *pi, struct bcm4360_hw *hw, struct bcm4360_phy_io *io)
{
	void *sh = BLOB_PTR(pi, PI_SH);

	hw_memset(hw, 0, sizeof(*hw));
	hw->d11 = BLOB_PTR(pi, PI_REGS);
	io->hw = hw;
	io->pci = BLOB_U32(sh, SH_BUSTYPE) == BLOB_BUSTYPE_PCI;
	io->write_limit = BLOB_U16(pi, PI_WREG_LIMIT);
	io->write_count = BLOB_U16(pi, PI_WREG_COUNT);
}

static void phy_io_done(void *pi, const struct bcm4360_phy_io *io)
{
	BLOB_U16(pi, PI_WREG_COUNT) = io->write_count;
}

#define BRIDGE_READ(name, open_fn)					\
u32 name(void *pi, u32 addr)						\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_phy_io io;					\
	u16 v;								\
									\
	phy_io(pi, &hw, &io);						\
	v = open_fn(&io, (u16)addr);					\
	phy_io_done(pi, &io);						\
	return v;							\
}

#define BRIDGE_WRITE(name, open_fn)					\
void name(void *pi, u32 addr, u32 val)					\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_phy_io io;					\
									\
	phy_io(pi, &hw, &io);						\
	open_fn(&io, (u16)addr, (u16)val);				\
	phy_io_done(pi, &io);						\
}

#define BRIDGE_MOD(name, open_fn)					\
void name(void *pi, u32 addr, u32 mask, u32 val)			\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_phy_io io;					\
									\
	phy_io(pi, &hw, &io);						\
	open_fn(&io, (u16)addr, (u16)mask, (u16)val);			\
	phy_io_done(pi, &io);						\
}

/* PHY registers: access.md, "PHY registers" */
u32 phy_reg_read(void *pi, u32 addr);
void phy_reg_write(void *pi, u32 addr, u32 val);
void phy_reg_mod(void *pi, u32 addr, u32 mask, u32 val);
void phy_reg_and(void *pi, u32 addr, u32 val);
void phy_reg_or(void *pi, u32 addr, u32 val);
BRIDGE_READ(phy_reg_read, bcm4360_phy_read)
BRIDGE_WRITE(phy_reg_write, bcm4360_phy_write)
BRIDGE_MOD(phy_reg_mod, bcm4360_phy_mod)
BRIDGE_WRITE(phy_reg_and, bcm4360_phy_and)
BRIDGE_WRITE(phy_reg_or, bcm4360_phy_or)

/* radio registers: access.md, "Radio registers" */
u32 read_radio_reg(void *pi, u32 addr);
void write_radio_reg(void *pi, u32 addr, u32 val);
void mod_radio_reg(void *pi, u32 addr, u32 mask, u32 val);
void and_radio_reg(void *pi, u32 addr, u32 val);
void or_radio_reg(void *pi, u32 addr, u32 val);
void xor_radio_reg(void *pi, u32 addr, u32 val);
BRIDGE_READ(read_radio_reg, bcm4360_radio_read)
BRIDGE_WRITE(write_radio_reg, bcm4360_radio_write)
BRIDGE_MOD(mod_radio_reg, bcm4360_radio_mod)
BRIDGE_WRITE(and_radio_reg, bcm4360_radio_and)
BRIDGE_WRITE(or_radio_reg, bcm4360_radio_or)
BRIDGE_WRITE(xor_radio_reg, bcm4360_radio_xor)

/* PHY tables: access.md, "PHY tables" */
void wlc_phy_table_write_acphy(void *pi, u32 id, u32 n, u32 offset, u32 width,
			       const void *data);
void wlc_phy_table_read_acphy(void *pi, u32 id, u32 n, u32 offset, u32 width, void *data);

void wlc_phy_table_write_acphy(void *pi, u32 id, u32 n, u32 offset, u32 width,
			       const void *data)
{
	struct bcm4360_hw hw;
	struct bcm4360_phy_io io;

	phy_io(pi, &hw, &io);
	bcm4360_tbl_write(&io, id, n, offset, width, data);
	phy_io_done(pi, &io);
}

void wlc_phy_table_read_acphy(void *pi, u32 id, u32 n, u32 offset, u32 width, void *data)
{
	struct bcm4360_hw hw;
	struct bcm4360_phy_io io;

	phy_io(pi, &hw, &io);
	bcm4360_tbl_read(&io, id, n, offset, width, data);
	phy_io_done(pi, &io);
}

/*
 * Shared memory: access.md, "Object memories of the MAC"; bmac-init.md A8.
 * The object brackets every access with the semaphore it shares with the
 * offload firmware of the on-chip ARM (tcm_sem_enter/exit, wlc_offloads.c);
 * that part of the upper layers stays the object's.
 */
void tcm_sem_enter(void *wlc);
void tcm_sem_exit(void *wlc);
u32 wlc_bmac_read_shm(void *wlc_hw, u32 offset);
void wlc_bmac_write_shm(void *wlc_hw, u32 offset, u32 val);

u32 wlc_bmac_read_shm(void *wlc_hw, u32 offset)
{
	struct bcm4360_hw hw;
	void *wlc = BLOB_PTR(wlc_hw, WLC_HW_WLC);
	u16 v;

	hw_memset(&hw, 0, sizeof(hw));
	hw.d11 = BLOB_PTR(wlc_hw, WLC_HW_REGS);
	tcm_sem_enter(wlc);
	v = bcm4360_shm_read(&hw, (u16)offset);
	tcm_sem_exit(wlc);
	return v;
}

void wlc_bmac_write_shm(void *wlc_hw, u32 offset, u32 val)
{
	struct bcm4360_hw hw;
	void *wlc = BLOB_PTR(wlc_hw, WLC_HW_WLC);

	hw_memset(&hw, 0, sizeof(hw));
	hw.d11 = BLOB_PTR(wlc_hw, WLC_HW_REGS);
	tcm_sem_enter(wlc);
	bcm4360_shm_write(&hw, (u16)offset, (u16)val);
	tcm_sem_exit(wlc);
}
