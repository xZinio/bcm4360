/*
 * The PCIe bridge of the backplane (Broadcom's nicpci.c and the si_pci*
 * functions of siutils.c). For now: the PERST work-around si_survive_perst_war
 * with pcie_survive_perst, which only it calls.
 *
 * Written from docs/re/spec/chip.md, "si_survive_perst_war" (with
 * "pcie_survive_perst") for the AXI backplane on a PCI bus. The core selection
 * is that of the open backplane (open/chip/si.c); the registers of the PCIe
 * core are reached through the fixed window hw->pcie (BAR0 + 0x2000).
 */
#include <bcm4360/si.h>

/* docs/re/spec/chip.md, "si_info_t", offset 0x04: bus types */
#define SI_BUS_PCI		1

/* core ids (docs/re/spec/chip.md, "Overview", "si_info_t") */
#define CORE_ID_CC		0x800	/* ChipCommon */
#define CORE_ID_PCIE		0x820	/* PCIe bridge */
#define CORE_ID_PCIE2		0x83c	/* PCIe Gen2 bridge (this card) */

/* si_survive_perst_war: the chips and revisions of the work-around */
#define CHIP_ID_4352		0x4352
#define CHIP_ID_4360		0x4360
#define PERST_WAR_CHIPREV_END	4	/* chip revisions below this only */

/* PCI configuration registers (32-bit accesses) */
#define PCICFG_SIZE		4	/* bytes of every configuration access */
#define PCICFG_BAR0_WIN		0x80	/* window 1 */

/* ChipCommon (docs/re/spec/chip.md, "ChipCommon registers used here") */
#define CC_PMU_WATCHDOG		0x634	/* PMU watchdog */
#define PMU_WATCHDOG_TICKS	2	/* the value written by the reset branch */

/* the wait after the watchdog write: 2000 times 10 us (20 ms) */
#define PERST_WAR_DELAYS	2000
#define PERST_WAR_DELAY_US	10

/* the PCIe core's register 0 (purpose not specified) */
#define PCIE_REG_0x000		0x000

/*
 * docs/re/spec/chip.md, "si_survive_perst_war": the gate of both branches -
 * PCI, chip 0x4352 or 0x4360, chip revision below 4.
 */
static bool perst_war_applies(const struct bcm4360_si *si)
{
	if (si->bustype != SI_BUS_PCI)
		return false;
	if (si->chip != CHIP_ID_4352 && si->chip != CHIP_ID_4360)
		return false;
	return si->chiprev < PERST_WAR_CHIPREV_END;
}

/*
 * docs/re/spec/chip.md, "si_survive_perst_war", step 1: the PMU watchdog
 * through window 1 with ChipCommon selected, 20 ms of delays, back to the
 * original core, window 1 restored if it moved.
 */
static void perst_war_reset(struct bcm4360_si *si)
{
	struct bcm4360_hw *hw = si->hw;
	u32 win, orig, i;
	void *p;

	win = hw_pcicfg_read(hw, PCICFG_BAR0_WIN, PCICFG_SIZE);
	orig = *si->curidx;
	p = bcm4360_si_setcore(si, CORE_ID_CC, 0);
	if (p)
		hw_write32(p, CC_PMU_WATCHDOG, PMU_WATCHDOG_TICKS);
	for (i = 0; i < PERST_WAR_DELAYS; i++)
		hw_udelay(PERST_WAR_DELAY_US);
	bcm4360_si_setcoreidx(si, orig);
	if (hw_pcicfg_read(hw, PCICFG_BAR0_WIN, PCICFG_SIZE) != win)
		hw_pcicfg_write(hw, PCICFG_BAR0_WIN, PCICFG_SIZE, win);
}

/*
 * docs/re/spec/chip.md, "si_survive_perst_war", step 2 (pcie_survive_perst):
 * read and write (old & ~mask) | val - `val` not limited to `mask` - only if
 * mask | val is not 0, then one read of the register.
 */
static void pcie_survive_perst(struct bcm4360_si *si, u32 mask, u32 val)
{
	void *pcie = si->hw->pcie;
	u32 old;

	if (mask | val) {
		old = hw_read32(pcie, PCIE_REG_0x000);
		hw_write32(pcie, PCIE_REG_0x000, (old & ~mask) | val);
	}
	hw_read32(pcie, PCIE_REG_0x000);
}

/* docs/re/spec/chip.md, "si_survive_perst_war" */
void bcm4360_si_survive_perst_war(struct bcm4360_si *si, bool reset, u32 mask, u32 val)
{
	if (!perst_war_applies(si))
		return;
	if (reset)
		perst_war_reset(si);
	if (mask && (si->buscoreid == CORE_ID_PCIE2 || si->buscoreid == CORE_ID_PCIE))
		pcie_survive_perst(si, mask, val);
}
