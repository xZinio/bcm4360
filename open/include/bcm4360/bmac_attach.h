/*
 * Attach and detach of the hardware MAC layer: the allocation of the MAC
 * layer's state (wlc_hw and its blocks), the identity and board checks, the
 * first contact with the core, the PHY attach per band, the DMA engines, the
 * coexistence defaults, the MAC address; the helpers that only these call.
 *
 * Specification: docs/re/spec/bmac-attach.md (with bmac-data.md P54 for the
 * DMA parameters and bmac-run.md R8, R9 for the FIFO size table and the
 * backplane's interrupt callbacks). Until the readers of the MAC layer's state
 * are all open, that state keeps Broadcom's layout (bmac-attach.md,
 * "Consequence for an open attach"): the procedures work on the blocks with
 * the offsets of the specification's Data section, and every other function
 * is reached through the services below.
 *
 * MAC core revisions below 40 are refused (error 0x0d): their transmit FIFO
 * size tables are not specified, and this driver is for the BCM4360 family
 * (revision 42).
 */
#ifndef BCM4360_BMAC_ATTACH_H
#define BCM4360_BMAC_ATTACH_H

#include <bcm4360/types.h>

/* --- the procedures (bmac-attach.md "Procedures"; the object's names) --- */

/* wlc_hw_attach: NULL with *err set on failure */
void *bcm4360_hw_attach(void *wlc, void *osh, u32 unit, u32 *err);
void bcm4360_hw_detach(void *hw);					/* wlc_hw_detach */
/* wlc_bmac_attach: 0 or the error code of the specification's table */
u32 bcm4360_bmac_attach(void *wlc, u16 vendor, u16 device, u32 unit, u8 piomode, void *osh,
			void *regsva, u32 bustype, void *btparam);
s32 bcm4360_bmac_detach(void *wlc);					/* wlc_bmac_detach */
void bcm4360_hw_set_di(void *hw, u32 fifo, void *di);			/* wlc_hw_set_di */
void bcm4360_hw_get_txavail(void *hw, void **out);			/* wlc_hw_get_txavail */
u32 bcm4360_hw_get_bandunit(void *hw);					/* wlc_hw_get_bandunit */
void bcm4360_template_cfg_init(void *wlc, u32 corerev);		/* wlc_template_cfg_init */
void *bcm4360_bmac_si_attach(u32 device, void *osh, void *regs, u32 bustype, void *btparam,
			     char **vars, u32 *varsz);			/* wlc_bmac_si_attach */
void bcm4360_bmac_si_detach(void *osh, void *sih);			/* wlc_bmac_si_detach */
void *bcm4360_phy_shim_attach(void *hw, void *wl, void *wlc);		/* wlc_phy_shim_attach */
void bcm4360_phy_shim_detach(void *shim);				/* wlc_phy_shim_detach */
bool bcm4360_chipmatch(u16 vendor, u16 device);				/* wlc_chipmatch */
bool bcm4360_is_singleband_5g(u32 device);				/* wlc_is_singleband_5g */
void bcm4360_tunables_override(u32 *tunables, u32 device, u32 corerev);
								/* wlc_tunables_override */
void *bcm4360_calloc(void *osh, u32 unit, u32 size);			/* wlc_calloc */
bool bcm4360_ether_atoe(const char *s, u8 *ea);				/* bcm_ether_atoe */
/* sub_069659 (bmac-data.md P54, name assigned wlc_bmac_dma_param_init) */
void bcm4360_bmac_dma_param_init(void *hw, u32 bustype, void *di);
/* bmac-run.md R8 */
void bcm4360_bmac_ampdu_set(void *hw, u8 mode);				/* wlc_bmac_ampdu_set */
s32 bcm4360_bmac_xmtfifo_sz_get(void *hw, u32 fifo, u32 *out);		/* wlc_bmac_xmtfifo_sz_get */
/* bmac-run.md R9: the backplane's callbacks (sub_061268, sub_061285) */
u32 bcm4360_bmac_wlintrsoff(void *hw);
void bcm4360_bmac_wlintrsrestore(void *hw, u32 mask);

/* --- services of the platform (the function each stands for in its comment) --- */

/* the card's variables */
const char *bcm4360_attach_env_getvar(const char *vars, const char *name);	/* getvar */
s32 bcm4360_attach_env_getintvar(const char *vars, const char *name);	/* getintvar */

/* the backplane (spec chip; sih is the backplane handle of the card) */
void *bcm4360_attach_env_si_attach(u32 device, void *osh, void *regs, u32 bustype,
				   void *btparam, char **vars, u32 *varsz);	/* si_attach */
void bcm4360_attach_env_si_detach(void *sih);				/* si_detach */
void *bcm4360_attach_env_si_setcore(void *sih, u32 coreid, u32 unit);	/* si_setcore */
u32 bcm4360_attach_env_si_corerev(void *sih);				/* si_corerev */
u32 bcm4360_attach_env_si_coreidx(void *sih);				/* si_coreidx */
void bcm4360_attach_env_si_clkctl_init(void *sih);			/* si_clkctl_init */
void bcm4360_attach_env_si_pcie_ltr_war(void *sih);			/* si_pcie_ltr_war */
void bcm4360_attach_env_si_pcieltrenable(void *sih, u32 mask, u32 val);	/* si_pcieltrenable */
void bcm4360_attach_env_si_pcieobffenable(void *sih, u32 mask, u32 val);	/* si_pcieobffenable */
bool bcm4360_attach_env_si_pci_war16165(void *sih);			/* si_pci_war16165 */
u32 bcm4360_attach_env_si_chip_hostif(void *sih);			/* si_chip_hostif */
u32 bcm4360_attach_env_si_pcie_get_request_size(void *sih);		/* si_pcie_get_request_size */
void bcm4360_attach_env_si_pmu_rfldo(void *sih, bool on);		/* si_pmu_rfldo */
void bcm4360_attach_env_si_pci_down(void *sih);				/* si_pci_down */
void bcm4360_attach_env_si_pci_sleep(void *sih);			/* si_pci_sleep */
/* si_register_intr_callback(sih, intrsoff, intrsrestore, NULL, arg) */
void bcm4360_attach_env_si_register_intr_callback(void *sih, u32 (*intrsoff)(void *arg),
						  void (*intrsrestore)(void *arg, u32 mask),
						  void *arg);
void bcm4360_attach_env_si_deregister_intr_callback(void *sih);	/* si_deregister_intr_callback */

/* the MAC layer's own functions specified elsewhere (bmac-init.md, bmac-run.md) */
void bcm4360_attach_env_clkctl_clk(void *hw, u32 mode);		/* A13 (sub_064887) */
void bcm4360_attach_env_corereset(void *hw, u32 flags);		/* wlc_bmac_corereset */
bool bcm4360_attach_env_validate_chip_access(void *hw);		/* wlc_bmac_validate_chip_access */
void bcm4360_attach_env_pllreq(void *hw, bool set, u32 req_bit);	/* wlc_bmac_pllreq */
void bcm4360_attach_env_setxband(void *hw, u32 bandunit);		/* wlc_setxband */
void bcm4360_attach_env_set_btswitch(void *hw, s8 mode);		/* wlc_bmac_set_btswitch */
void bcm4360_attach_env_btc_wire_set(void *hw, s32 wire);		/* wlc_bmac_btc_wire_set */
void bcm4360_attach_env_btc_mode_set(void *hw, s32 mode);		/* wlc_bmac_btc_mode_set */
void bcm4360_attach_env_coredisable(void *hw);				/* wlc_coredisable */
void bcm4360_attach_env_xtal(void *hw, bool want);			/* wlc_bmac_xtal */
void *bcm4360_attach_env_led_attach(void *hw);				/* wlc_bmac_led_attach */
s32 bcm4360_attach_env_led_detach(void *hw);				/* wlc_bmac_led_detach */

/* the PHY (acphy-attach.md, acphy-api.md) */
void *bcm4360_attach_env_phy_shared_attach(void *desc);		/* wlc_phy_shared_attach */
void bcm4360_attach_env_phy_shared_detach(void *phy_sh);		/* wlc_phy_shared_detach */
void *bcm4360_attach_env_phy_attach(void *phy_sh, void *regs, s32 bandtype, char *vars);
								/* wlc_phy_attach */
void bcm4360_attach_env_phy_detach(void *pi);				/* wlc_phy_detach */
void bcm4360_attach_env_phy_machwcap_set(void *pi, u32 machwcap);	/* wlc_phy_machwcap_set */
void bcm4360_attach_env_phy_get_phyversion(void *pi, u16 *phytype, u16 *phyrev, u16 *radioid,
					   u16 *radiorev);		/* wlc_phy_get_phyversion */
u8 bcm4360_attach_env_phy_get_encore(void *pi);			/* wlc_phy_get_encore */
u32 bcm4360_attach_env_phy_get_coreflags(void *pi);			/* wlc_phy_get_coreflags */

/* the DMA engines (bmac-data.md P1, P2; the slots of the engine's table) */
u32 bcm4360_attach_env_dma_addrwidth(void *sih, void *dmaregs);	/* dma_addrwidth */
void *bcm4360_attach_env_dma_attach(void *osh, const char *name, void *sih, void *txregs,
				    void *rxregs, u32 ntxd, u32 nrxd, u32 rxbufsize,
				    s32 rxextheadroom, u32 nrxpost, u32 rxoffset);
						/* dma_attach(.., &wl_msg_level) */
void bcm4360_attach_env_dma_detach(void *di);				/* slot 0x000 */
void *bcm4360_attach_env_dma_txavail(void *di);		/* slot 0x108: getvar(di, "&txavail") */
void bcm4360_attach_env_dma_ctrlflags(void *di, u32 mask, u32 flags);	/* slot 0x118 */
void bcm4360_attach_env_dma_burstlen_set(void *di, u8 rxburstlen, u8 txburstlen);
								/* slot 0x160 */
void bcm4360_attach_env_dma_param_set(void *di, u16 id, u16 value);	/* slot 0x170 */

/* the glue (wl_linux.c) */
bool bcm4360_attach_env_alloc_dma_resources(void *wl, u32 addrwidth);	/* wl_alloc_dma_resources */
u32 bcm4360_attach_env_intrsoff(void *wl);				/* wl_intrsoff */
void bcm4360_attach_env_intrsrestore(void *wl, u32 mask);		/* wl_intrsrestore */

#endif
