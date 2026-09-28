/* SPDX-License-Identifier: ISC */
/*
 * Register access: PHY registers, radio registers, PHY tables and the shared
 * memory of driver and microcode.
 *
 * Specification: docs/re/spec/access.md. The notation PHY(a), RADIO(a),
 * TBL(id)[i], SHM(o) and D11(o) in the comments is the one defined there.
 */
#ifndef BCM4360_ACCESS_H
#define BCM4360_ACCESS_H

#include <bcm4360/types.h>
#include <bcm4360/hw.h>

/*
 * Registers of the 802.11 core that the indirect accesses go through, D11(o).
 * A register whose purpose is not known is named by its address.
 */
#define D11_REG_0x120		0x120	/* read by the pacing of radio writes */
#define D11_OBJ_ADDR		0x160	/* object memories: memory and address */
#define D11_OBJ_DATA_LO		0x164	/* object memories: data, bits 0..15 */
#define D11_OBJ_DATA_HI		0x166	/* object memories: data, bits 16..31 */
#define D11_RADIO_ADDR		0x3d8	/* radio registers: address */
#define D11_RADIO_DATA		0x3da	/* radio registers: data */
#define D11_PHY_VERSION		0x3e0	/* read by the pacing of PHY writes */
#define D11_PHY_ADDR		0x3fc	/* PHY registers: address */
#define D11_PHY_DATA		0x3fe	/* PHY registers: data */

/* D11_OBJ_ADDR: the memory in bits 16..19, the address in the bits below */
#define D11_OBJ_MEM_SHIFT	16
#define D11_OBJ_MEM_UCODE	0	/* microcode */
#define D11_OBJ_MEM_SHM		1	/* shared memory of driver and microcode */
#define D11_OBJ_MEM_SCRATCH	2	/* scratch registers of the microcode */
#define D11_OBJ_MEM_IHR		3	/* internal hardware registers */
#define D11_OBJ_MEM_AMT		4	/* address match table */

/* PHY registers that the table access goes through, PHY(a) */
#define ACPHY_TBL_ID		0x0d	/* id of the table */
#define ACPHY_TBL_OFFSET	0x0e	/* index of the entry, advances by itself */
#define ACPHY_TBL_DATA_LO	0x0f	/* entries of 8 and 16 bit, lower half of 32 bit */
#define ACPHY_TBL_DATA_HI	0x10	/* upper half of entries of 32 bit */
#define ACPHY_TBL_DATA_WIDE	0x11	/* entries of 48, 60 and 64 bit */

/* state of the register access of one PHY */
struct bcm4360_phy_io {
	struct bcm4360_hw *hw;
	/* write pacing, see the specification ("PHY registers") */
	bool pci;		/* bus type PCI: only there the writes are paced */
	u16 write_limit;	/* a dummy read before every write_limit-th write in a row */
	u16 write_count;	/* PHY and radio register writes since the last read */
};

void bcm4360_phy_io_init(struct bcm4360_phy_io *io, struct bcm4360_hw *hw,
			 bool pci, u16 write_limit);

u16  bcm4360_phy_read(struct bcm4360_phy_io *io, u16 addr);
void bcm4360_phy_write(struct bcm4360_phy_io *io, u16 addr, u16 val);
void bcm4360_phy_mod(struct bcm4360_phy_io *io, u16 addr, u16 mask, u16 val);
void bcm4360_phy_and(struct bcm4360_phy_io *io, u16 addr, u16 val);
void bcm4360_phy_or(struct bcm4360_phy_io *io, u16 addr, u16 val);

u16  bcm4360_radio_read(struct bcm4360_phy_io *io, u16 addr);
void bcm4360_radio_write(struct bcm4360_phy_io *io, u16 addr, u16 val);
void bcm4360_radio_mod(struct bcm4360_phy_io *io, u16 addr, u16 mask, u16 val);
void bcm4360_radio_and(struct bcm4360_phy_io *io, u16 addr, u16 val);
void bcm4360_radio_or(struct bcm4360_phy_io *io, u16 addr, u16 val);
void bcm4360_radio_xor(struct bcm4360_phy_io *io, u16 addr, u16 val);

/*
 * n entries of `width` bits (8, 16, 32, 48, 60 or 64) starting at `offset` of
 * table `id`. Layout of `data`: width 8: bytes; 16: u16; 32: u32; 48: three
 * u16 per entry, lowest first; 60 and 64: two u32 per entry, lower first.
 * `data` must be aligned for the type it holds. A transfer with any other
 * width is not made.
 */
void bcm4360_tbl_write(struct bcm4360_phy_io *io, u32 id, u32 n, u32 offset,
		       u32 width, const void *data);
void bcm4360_tbl_read(struct bcm4360_phy_io *io, u32 id, u32 n, u32 offset,
		      u32 width, void *data);

/* shared memory of driver and microcode, offset in bytes */
u16  bcm4360_shm_read(struct bcm4360_hw *hw, u16 offset);
void bcm4360_shm_write(struct bcm4360_hw *hw, u16 offset, u16 val);

#endif
