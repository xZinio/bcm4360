/* SPDX-License-Identifier: ISC */
/*
 * The facts about Broadcom's object that the hybrid bridges need: offsets of
 * fields in the object's own structures, as the specifications in
 * docs/re/spec/ give them. Only the bridges in hybrid/ use this header; the
 * open code in open/ never sees the object's structures.
 */
#ifndef HYBRID_BLOB_ABI_H
#define HYBRID_BLOB_ABI_H

#include <bcm4360/types.h>

#define BLOB_FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))
#define BLOB_PTR(p, off)		BLOB_FIELD(p, off, void *)
#define BLOB_U16(p, off)		BLOB_FIELD(p, off, u16)
#define BLOB_U32(p, off)		BLOB_FIELD(p, off, u32)

/* phy_info_t, `pi`: docs/re/spec/acphy-attach.md, "Data" */
#define PI_SH			0x020	/* shared_phy_t * */
#define PI_REGS			0x148	/* registers of the 802.11 core */
#define PI_WREG_COUNT		0x226	/* PHY/radio writes in a row (access.md, write pacing) */
#define PI_WREG_LIMIT		0x228	/* phy_wreg_limit */

/* shared_phy_t, `sh`: docs/re/spec/acphy-attach.md, "Data" */
#define SH_BUSTYPE		0x06c	/* bus type, copied from sih+0x04 */
#define BLOB_BUSTYPE_PCI	1

/* wlc_hw_info_t, `wlc_hw`: docs/re/spec/bmac-init.md */
#define WLC_HW_WLC		0x000	/* wlc_info_t * */
#define WLC_HW_COREREV		0x084	/* MAC core revision */
#define WLC_HW_SIH		0x0b8	/* si_t * */
#define WLC_HW_REGS		0x0d0	/* registers of the 802.11 core */
#define WLC_HW_BAND		0x0e8	/* current band state */
#define WLC_HW_BAND_2G		0x0f0	/* band state of 2.4 GHz */
#define WLC_HW_BAND_5G		0x0f8	/* band state of 5 GHz */
#define WLC_HW_MACCONTROL	0x168	/* copy of D11(0x120) without the overrides (A1) */
#define WLC_HW_SUSPEND_DEPTH	0x16c	/* nesting of suspend/enable (A6, A7) */
#define WLC_HW_WAKE_OVERRIDE	0x170	/* reasons to keep the microcode awake (A3, A4) */
#define WLC_HW_MUTE_OVERRIDE	0x174	/* transmitter muted (A1) */
#define WLC_HW_CLK		0x186	/* the core's clock is on (u8) */
#define WLC_HW_FASTPWRUP_DLY	0x192	/* fast power-up delay, microseconds (u16, A5) */

/* band state (wlc_hwband_t): the host flag copies, bmac-init.md A9 */
#define BAND_MHFS		0x008	/* u16 [5] */
#define BAND_MHFS_N		5

/*
 * si_t, `sih`, the handle of the backplane utilities: the identification of
 * chip and board that si_attach reads (docs/re/spec/pmu.md, "Overview" 1.2,
 * and the field lists of the specifications), and the mapping of BAR0.
 */
#define SIH_CCREV		0x014	/* ChipCommon revision */
#define SIH_CCCAPS		0x018	/* CC(0x04) */
#define SIH_CCCAPS_EXT		0x01c	/* CC(0xac) */
#define SIH_PMUREV		0x020	/* bits 0..7 of CC(0x604) */
#define SIH_PMUCAPS		0x024	/* CC(0x604) */
#define SIH_CHIP		0x03c	/* chip id */
#define SIH_CHIPREV		0x040
#define SIH_CHIPPKG		0x044
#define SIH_CHIPST		0x048	/* chip status, CC(0x2c) */
#define SIH_OSH			0x058	/* handle of the OS layer (osl_pci_*_config) */
#define SIH_REGS		0x0b8	/* BAR0 as mapped by the driver (regsva of wlc_attach) */

/* fixed windows in BAR0 on a PCIe card: docs/re/spec/access.md */
#define BAR0_WRAP		0x1000
#define BAR0_PCIE		0x2000
#define BAR0_CC			0x3000

#endif
