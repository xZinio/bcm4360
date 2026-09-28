// SPDX-License-Identifier: ISC
/*
 * Test of the comparison tool itself (ab.py selftest), not part of the driver:
 * a function that uses what the tool provides to open code - register access,
 * table transfers, services of another layer, variables.
 */
#include <bcm4360/access.h>
#include <bcm4360/hw.h>

void bcm4360_test_service(struct bcm4360_hw *hw, u32 a, u32 b);
u32 bcm4360_test_query(struct bcm4360_hw *hw, u32 a, u32 b);

u32 harness_test(struct bcm4360_phy_io *io)
{
	static const u16 words[3] = { 0x1111, 0x2222, 0x3333 };
	const char *v = hw_getvar(io->hw, "boardtype");
	const char *none = hw_getvar(io->hw, "no such variable");
	u16 back[3];
	u32 r;

	bcm4360_test_service(io->hw, 0x12, 0x3456);
	bcm4360_phy_write(io, 0x400, 0x55aa);
	bcm4360_tbl_write(io, 0x40, 3, 7, 16, words);
	bcm4360_tbl_read(io, 0x40, 3, 7, 16, back);
	r = bcm4360_test_query(io->hw, 0, 0);
	hw_udelay(10);
	return (v ? (u32)v[0] : 0) | (none ? 0x100 : 0) | back[1] << 16 | r << 12;
}
