/*
 * Hybrid build: the PCIe bridge functions of Broadcom's core (nicpci.c and
 * the si_pci* functions of siutils.c) by the open code of
 * open/chip/si_pcie.c (docs/re/spec/chip.md). Part of the backplane layer:
 * switch open_si, the same open view of the backplane as hybrid/si.c
 * (hyb_si_view). For now: si_survive_perst_war (with pcie_survive_perst,
 * which only it calls).
 *
 * Parameters of type bool of the object are bytes: taken as u32 and
 * truncated.
 */
#include "hybrid.h"
#include <bcm4360/si.h>

HYB_TAKE(void, si_survive_perst_war, (void *sih, u32 reset, u32 mask, u32 val));

void si_survive_perst_war(void *sih, u32 reset, u32 mask, u32 val)
{
	struct bcm4360_si si;

	if (!hyb_si_view(sih, &si)) {
		bcm4360_blob_si_survive_perst_war(sih, reset, mask, val);
		return;
	}
	bcm4360_si_survive_perst_war(&si, (u8)reset != 0, mask, val);
}
