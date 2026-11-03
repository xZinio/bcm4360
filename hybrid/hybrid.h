/* SPDX-License-Identifier: ISC */
/*
 * Hybrid build: open code takes over functions of Broadcom's object inside
 * bcm4360.ko (hybrid/README.md). This header holds what the adapters need to
 * know of the object's side: the layouts of its public structures and the
 * switches that select, per layer, the open code or Broadcom's.
 *
 * Everything here is interface: names, arguments and the layout of
 * structures that the object shares with the glue in src/. The behaviour is
 * in open/.
 */
#ifndef BCM4360_HYBRID_H
#define BCM4360_HYBRID_H

#include <bcm4360/types.h>
#include <bcm4360/hw.h>

/*
 * The public part of Broadcom's si_t, `struct si_pub` of
 * src/include/siutils.h, with which the object and the glue were built; the
 * fields up to `chipst`, which the adapters read.
 */
struct hyb_si_pub {
	u32 socitype;
	u32 bustype;
	u32 buscoretype;
	u32 buscorerev;
	u32 buscoreidx;
	s32 ccrev;
	u32 cccaps;
	u32 cccaps_ext;
	s32 pmurev;
	u32 pmucaps;
	u32 boardtype;
	u32 boardrev;
	u32 boardvendor;
	u32 boardflags;
	u32 boardflags2;
	u32 chip;
	u32 chiprev;
	u32 chippkg;
	u32 chipst;
};

/*
 * HYB_TAKE(ret, NAME, (args)) declares the function NAME of the object that
 * an adapter takes over (it defines NAME), and bcm4360_blob_NAME, the
 * object's own body of it (scripts/mkhybrid.sh).
 */
#define HYB_TAKE(ret, name, args)	ret name args; ret bcm4360_blob_##name args

/* the BAR0 window of the card is mapped with this size (PCI_BAR0_WINSZ) */
#define HYB_BAR0_SIZE		0x4000
/* fixed windows inside BAR0 (src/include/pcicfg.h, PCI_16KB0_*_OFFSET) */
#define HYB_BAR0_WRAP		0x1000	/* second movable window: a core's wrapper */
#define HYB_BAR0_PCIE		0x2000	/* registers of the PCIe core */
#define HYB_BAR0_CC		0x3000	/* registers of ChipCommon */

/*
 * A card as the adapters know it: Broadcom's handle of its backplane and the
 * open code's platform structure.
 */
struct hyb_card {
	const struct hyb_si_pub *sih;	/* NULL: slot free */
	void *osh;			/* Broadcom's OS handle of the card */
	struct bcm4360_hw hw;
};

/* hybrid/card.c: the card of a backplane handle of the object, or NULL */
struct hyb_card *hyb_card_of(const void *sih);

/* hybrid/pmu.c: a backplane handle is gone, with the PMU state kept for it */
void hyb_pmu_forget(const void *sih);

/* hybrid/pmu.c: the open PMU state of a card (also with open_pmu off) */
struct bcm4360_pmu;
struct bcm4360_pmu *hyb_pmu_of(const void *sih);

/*
 * hybrid/mac.c: the open MAC state (struct bcm4360_mac) filled from the
 * object's `wlc_hw` before a call of the open MAC, and written back after it.
 */
struct bcm4360_mac;
void hyb_mac_in(void *wlc_hw, struct bcm4360_hw *hw, struct bcm4360_mac *mac);
void hyb_mac_out(void *wlc_hw, const struct bcm4360_mac *mac);

/*
 * hybrid/si.c: the open view of a backplane handle of the object (struct
 * bcm4360_si, bcm4360/si.h); false if Broadcom's function must run (switch
 * open_si off, not an AXI backplane on PCI of a BCM4360-family chip).
 */
struct bcm4360_si;
bool hyb_si_view(void *sih, struct bcm4360_si *si);

/*
 * Switches, one per layer: true selects the open code. They are module
 * parameters of bcm4360.ko (read when a card is attached), and variables
 * that the emulator sets in its tests.
 */
extern bool bcm4360_open_pmu;
extern bool bcm4360_open_phy;
extern bool bcm4360_open_access;
extern bool bcm4360_open_mac;
extern bool bcm4360_open_utils;
extern bool bcm4360_open_clm;
extern bool bcm4360_open_ppr;
extern bool bcm4360_open_srom;
extern bool bcm4360_open_si;
extern bool bcm4360_open_dma;
extern bool bcm4360_open_chan;
extern bool bcm4360_open_iovar;
extern bool bcm4360_open_nvram;
extern bool bcm4360_open_rate;

#endif
