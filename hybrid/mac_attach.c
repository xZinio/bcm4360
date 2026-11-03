// SPDX-License-Identifier: ISC
/*
 * Hybrid build: the attach and detach of the MAC layer in Broadcom's core
 * (wlc_bmac.c, wlc_hw.c, and the helpers of wlc.c, wlc_alloc.c, bcmutils.c and
 * wlc_phy_shim.c that only these call) by the open code of
 * open/mac/bmac_attach.c (docs/re/spec/bmac-attach.md). Part of the MAC
 * layer: switch open_mac, group 0x200 of open_mac_mask.
 *
 * The open attach builds wlc_hw in Broadcom's layout itself, so these
 * adapters pass the object's arguments through; only the services of
 * open/include/bcm4360/bmac_attach.h that run MAC code of the other groups
 * (A13 of the clock group) take the view of hybrid/mac.c.
 *
 * Integer arguments narrower than 32 bit arrive as u32 and are truncated.
 */
#include "hybrid.h"
#include <bcm4360/bmac_attach.h>
#include <bcm4360/mac.h>

HYB_TAKE(void *, wlc_hw_attach, (void *wlc, void *osh, u32 unit, u32 *err));
HYB_TAKE(void, wlc_hw_detach, (void *hw));
HYB_TAKE(u32, wlc_bmac_attach, (void *wlc, u32 vendor, u32 device, u32 unit, u32 piomode,
				void *osh, void *regsva, u32 bustype, void *btparam));
HYB_TAKE(s32, wlc_bmac_detach, (void *wlc));
HYB_TAKE(void, wlc_hw_set_di, (void *hw, u32 fifo, void *di));
HYB_TAKE(void, wlc_hw_get_txavail, (void *hw, void **out));
HYB_TAKE(u32, wlc_hw_get_bandunit, (void *hw));
HYB_TAKE(void, wlc_template_cfg_init, (void *wlc, u32 corerev));
HYB_TAKE(void *, wlc_bmac_si_attach, (u32 device, void *osh, void *regs, u32 bustype,
				      void *btparam, char **vars, u32 *varsz));
HYB_TAKE(void, wlc_bmac_si_detach, (void *osh, void *sih));
HYB_TAKE(void *, wlc_phy_shim_attach, (void *hw, void *wl, void *wlc));
HYB_TAKE(void, wlc_phy_shim_detach, (void *shim));
HYB_TAKE(u8, wlc_chipmatch, (u32 vendor, u32 device));
HYB_TAKE(u8, wlc_is_singleband_5g, (u32 device));
HYB_TAKE(void, wlc_tunables_override, (u32 *tunables, u32 device, u32 corerev));
HYB_TAKE(void *, wlc_calloc, (void *osh, u32 unit, u32 size));
HYB_TAKE(u8, bcm_ether_atoe, (const char *s, u8 *ea));
HYB_TAKE(void, wlc_bmac_ampdu_set, (void *hw, u32 mode));
HYB_TAKE(s32, wlc_bmac_xmtfifo_sz_get, (void *hw, u32 fifo, u32 *out));

/* Broadcom's functions and the open takes of other layers the services call */
char *getvar(char *vars, const char *name);
s32 getintvar(char *vars, const char *name);
void *si_attach(u32 device, void *osh, void *regs, u32 bustype, void *sdh, char **vars,
		u32 *varsz);
void si_detach(void *sih);
void *si_setcore(void *sih, u32 coreid, u32 unit);
u32 si_corerev(void *sih);
u32 si_coreidx(void *sih);
void si_clkctl_init(void *sih);
void si_pcie_ltr_war(void *sih);
u32 si_pcieltrenable(void *sih, u32 mask, u32 val);
u32 si_pcieobffenable(void *sih, u32 mask, u32 val);
u8 si_pci_war16165(void *sih);
u32 si_chip_hostif(void *sih);
u32 si_pcie_get_request_size(void *sih);
void si_pmu_rfldo(void *sih, u32 on);
void si_pci_down(void *sih);
void si_pci_sleep(void *sih);
s32 si_register_intr_callback(void *sih, void *intrsoff, void *intrsrestore, void *intrsenabled,
			      void *arg);
void si_deregister_intr_callback(void *sih);
void wlc_bmac_corereset(void *wlc_hw, u32 flags);
u8 wlc_bmac_validate_chip_access(void *wlc_hw);
void wlc_bmac_pllreq(void *wlc_hw, u32 set, u32 req_bit);
void wlc_setxband(void *wlc_hw, u32 unit);
s32 wlc_bmac_set_btswitch(void *wlc_hw, u32 sw);
s32 wlc_bmac_btc_wire_set(void *wlc_hw, s32 wire);
s32 wlc_bmac_btc_mode_set(void *wlc_hw, s32 mode);
void wlc_coredisable(void *wlc_hw);
void wlc_bmac_xtal(void *wlc_hw, u32 want);
void *wlc_bmac_led_attach(void *wlc_hw);
s32 wlc_bmac_led_detach(void *wlc_hw);
void *wlc_phy_shared_attach(void *params);
void wlc_phy_shared_detach(void *sh);
void *wlc_phy_attach(void *sh, void *regs, int bandtype, char *vars);
void wlc_phy_detach(void *pi);
void wlc_phy_machwcap_set(void *pi, u32 caps);
u8 wlc_phy_get_phyversion(void *pi, u16 *type, u16 *rev, u16 *radioid, u16 *radiover);
u8 wlc_phy_get_encore(void *pi);
u32 wlc_phy_get_coreflags(void *pi);
u32 dma_addrwidth(void *sih, void *dmaregs);
void *dma_attach(void *osh, const char *name, void *sih, void *txregs, void *rxregs, u32 ntxd,
		 u32 nrxd, u32 rxbufsize, s32 rxextheadroom, u32 nrxpost, u32 rxoffset,
		 u32 *msg_level);
extern u32 wl_msg_level;
/* the glue (src/wl/sys/wl_linux.c) */
bool wl_alloc_dma_resources(void *wl, u32 addrwidth);
u32 wl_intrsoff(void *wl);
void wl_intrsrestore(void *wl, u32 mask);

#define ON()	hyb_mac_on(HYB_MAC_ATTACH)

/* --- the adapters --- */

void *wlc_hw_attach(void *wlc, void *osh, u32 unit, u32 *err)
{
	if (!ON())
		return bcm4360_blob_wlc_hw_attach(wlc, osh, unit, err);
	return bcm4360_hw_attach(wlc, osh, unit, err);
}

void wlc_hw_detach(void *hw)
{
	if (!ON()) {
		bcm4360_blob_wlc_hw_detach(hw);
		return;
	}
	bcm4360_hw_detach(hw);
}

u32 wlc_bmac_attach(void *wlc, u32 vendor, u32 device, u32 unit, u32 piomode, void *osh,
		    void *regsva, u32 bustype, void *btparam)
{
	if (!ON())
		return bcm4360_blob_wlc_bmac_attach(wlc, vendor, device, unit, piomode, osh, regsva,
						    bustype, btparam);
	return bcm4360_bmac_attach(wlc, (u16)vendor, (u16)device, unit, (u8)piomode, osh, regsva,
				   bustype, btparam);
}

s32 wlc_bmac_detach(void *wlc)
{
	if (!ON())
		return bcm4360_blob_wlc_bmac_detach(wlc);
	return bcm4360_bmac_detach(wlc);
}

void wlc_hw_set_di(void *hw, u32 fifo, void *di)
{
	if (!ON()) {
		bcm4360_blob_wlc_hw_set_di(hw, fifo, di);
		return;
	}
	bcm4360_hw_set_di(hw, fifo, di);
}

void wlc_hw_get_txavail(void *hw, void **out)
{
	if (!ON()) {
		bcm4360_blob_wlc_hw_get_txavail(hw, out);
		return;
	}
	bcm4360_hw_get_txavail(hw, out);
}

u32 wlc_hw_get_bandunit(void *hw)
{
	if (!ON())
		return bcm4360_blob_wlc_hw_get_bandunit(hw);
	return bcm4360_hw_get_bandunit(hw);
}

void wlc_template_cfg_init(void *wlc, u32 corerev)
{
	if (!ON()) {
		bcm4360_blob_wlc_template_cfg_init(wlc, corerev);
		return;
	}
	bcm4360_template_cfg_init(wlc, corerev);
}

void *wlc_bmac_si_attach(u32 device, void *osh, void *regs, u32 bustype, void *btparam,
			 char **vars, u32 *varsz)
{
	if (!ON())
		return bcm4360_blob_wlc_bmac_si_attach(device, osh, regs, bustype, btparam, vars,
						       varsz);
	return bcm4360_bmac_si_attach(device, osh, regs, bustype, btparam, vars, varsz);
}

void wlc_bmac_si_detach(void *osh, void *sih)
{
	if (!ON()) {
		bcm4360_blob_wlc_bmac_si_detach(osh, sih);
		return;
	}
	bcm4360_bmac_si_detach(osh, sih);
}

void *wlc_phy_shim_attach(void *hw, void *wl, void *wlc)
{
	if (!ON())
		return bcm4360_blob_wlc_phy_shim_attach(hw, wl, wlc);
	return bcm4360_phy_shim_attach(hw, wl, wlc);
}

void wlc_phy_shim_detach(void *shim)
{
	if (!ON()) {
		bcm4360_blob_wlc_phy_shim_detach(shim);
		return;
	}
	bcm4360_phy_shim_detach(shim);
}

u8 wlc_chipmatch(u32 vendor, u32 device)
{
	if (!ON())
		return bcm4360_blob_wlc_chipmatch(vendor, device);
	return bcm4360_chipmatch((u16)vendor, (u16)device);
}

u8 wlc_is_singleband_5g(u32 device)
{
	if (!ON())
		return bcm4360_blob_wlc_is_singleband_5g(device);
	return bcm4360_is_singleband_5g(device);
}

void wlc_tunables_override(u32 *tunables, u32 device, u32 corerev)
{
	if (!ON()) {
		bcm4360_blob_wlc_tunables_override(tunables, device, corerev);
		return;
	}
	bcm4360_tunables_override(tunables, device, corerev);
}

void *wlc_calloc(void *osh, u32 unit, u32 size)
{
	if (!ON())
		return bcm4360_blob_wlc_calloc(osh, unit, size);
	return bcm4360_calloc(osh, unit, size);
}

u8 bcm_ether_atoe(const char *s, u8 *ea)
{
	if (!ON())
		return bcm4360_blob_bcm_ether_atoe(s, ea);
	return bcm4360_ether_atoe(s, ea);
}

void wlc_bmac_ampdu_set(void *hw, u32 mode)
{
	if (!ON()) {
		bcm4360_blob_wlc_bmac_ampdu_set(hw, mode);
		return;
	}
	bcm4360_bmac_ampdu_set(hw, (u8)mode);
}

s32 wlc_bmac_xmtfifo_sz_get(void *hw, u32 fifo, u32 *out)
{
	if (!ON())
		return bcm4360_blob_wlc_bmac_xmtfifo_sz_get(hw, fifo, out);
	return bcm4360_bmac_xmtfifo_sz_get(hw, fifo, out);
}

/* --- the services of open/include/bcm4360/bmac_attach.h --- */

const char *bcm4360_attach_env_getvar(const char *vars, const char *name)
{
	return getvar((char *)vars, name);
}

s32 bcm4360_attach_env_getintvar(const char *vars, const char *name)
{
	return getintvar((char *)vars, name);
}

void *bcm4360_attach_env_si_attach(u32 device, void *osh, void *regs, u32 bustype,
				   void *btparam, char **vars, u32 *varsz)
{
	return si_attach(device, osh, regs, bustype, btparam, vars, varsz);
}

void bcm4360_attach_env_si_detach(void *sih) { si_detach(sih); }
void *bcm4360_attach_env_si_setcore(void *sih, u32 coreid, u32 unit)
{
	return si_setcore(sih, coreid, unit);
}
u32 bcm4360_attach_env_si_corerev(void *sih) { return si_corerev(sih); }
u32 bcm4360_attach_env_si_coreidx(void *sih) { return si_coreidx(sih); }
void bcm4360_attach_env_si_clkctl_init(void *sih) { si_clkctl_init(sih); }
void bcm4360_attach_env_si_pcie_ltr_war(void *sih) { si_pcie_ltr_war(sih); }
void bcm4360_attach_env_si_pcieltrenable(void *sih, u32 mask, u32 val)
{
	si_pcieltrenable(sih, mask, val);
}
void bcm4360_attach_env_si_pcieobffenable(void *sih, u32 mask, u32 val)
{
	si_pcieobffenable(sih, mask, val);
}
bool bcm4360_attach_env_si_pci_war16165(void *sih) { return si_pci_war16165(sih) != 0; }
u32 bcm4360_attach_env_si_chip_hostif(void *sih) { return si_chip_hostif(sih); }
u32 bcm4360_attach_env_si_pcie_get_request_size(void *sih)
{
	return si_pcie_get_request_size(sih);
}
void bcm4360_attach_env_si_pmu_rfldo(void *sih, bool on) { si_pmu_rfldo(sih, on); }
void bcm4360_attach_env_si_pci_down(void *sih) { si_pci_down(sih); }
void bcm4360_attach_env_si_pci_sleep(void *sih) { si_pci_sleep(sih); }

void bcm4360_attach_env_si_register_intr_callback(void *sih, u32 (*intrsoff)(void *arg),
						  void (*intrsrestore)(void *arg, u32 mask),
						  void *arg)
{
	si_register_intr_callback(sih, (void *)intrsoff, (void *)intrsrestore, NULL, arg);
}

void bcm4360_attach_env_si_deregister_intr_callback(void *sih)
{
	si_deregister_intr_callback(sih);
}

/* A13 is Broadcom's static copy in the object (bmac-attach.md, "Calls"): the
 * open A13 of the clock group, with the view of the MAC state */
void bcm4360_attach_env_clkctl_clk(void *wlc_hw, u32 mode)
{
	struct bcm4360_hw hw;
	struct bcm4360_mac mac, mac0;

	hyb_mac_in(wlc_hw, &hw, &mac, &mac0);
	bcm4360_bmac_clkctl_clk(&mac, mode);
	hyb_mac_out(wlc_hw, &mac, &mac0);
}

void bcm4360_attach_env_corereset(void *hw, u32 flags) { wlc_bmac_corereset(hw, flags); }
bool bcm4360_attach_env_validate_chip_access(void *hw)
{
	return wlc_bmac_validate_chip_access(hw) != 0;
}
void bcm4360_attach_env_pllreq(void *hw, bool set, u32 req_bit) { wlc_bmac_pllreq(hw, set, req_bit); }
void bcm4360_attach_env_setxband(void *hw, u32 bandunit) { wlc_setxband(hw, bandunit); }
void bcm4360_attach_env_set_btswitch(void *hw, s8 mode) { wlc_bmac_set_btswitch(hw, (u32)(s32)mode); }
void bcm4360_attach_env_btc_wire_set(void *hw, s32 wire) { wlc_bmac_btc_wire_set(hw, wire); }
void bcm4360_attach_env_btc_mode_set(void *hw, s32 mode) { wlc_bmac_btc_mode_set(hw, mode); }
void bcm4360_attach_env_coredisable(void *hw) { wlc_coredisable(hw); }
void bcm4360_attach_env_xtal(void *hw, bool want) { wlc_bmac_xtal(hw, want); }
void *bcm4360_attach_env_led_attach(void *hw) { return wlc_bmac_led_attach(hw); }
s32 bcm4360_attach_env_led_detach(void *hw) { return wlc_bmac_led_detach(hw); }

void *bcm4360_attach_env_phy_shared_attach(void *desc) { return wlc_phy_shared_attach(desc); }
void bcm4360_attach_env_phy_shared_detach(void *phy_sh) { wlc_phy_shared_detach(phy_sh); }
void *bcm4360_attach_env_phy_attach(void *phy_sh, void *regs, s32 bandtype, char *vars)
{
	return wlc_phy_attach(phy_sh, regs, bandtype, vars);
}
void bcm4360_attach_env_phy_detach(void *pi) { wlc_phy_detach(pi); }
void bcm4360_attach_env_phy_machwcap_set(void *pi, u32 machwcap)
{
	wlc_phy_machwcap_set(pi, machwcap);
}
void bcm4360_attach_env_phy_get_phyversion(void *pi, u16 *phytype, u16 *phyrev, u16 *radioid,
					   u16 *radiorev)
{
	wlc_phy_get_phyversion(pi, phytype, phyrev, radioid, radiorev);
}
u8 bcm4360_attach_env_phy_get_encore(void *pi) { return wlc_phy_get_encore(pi); }
u32 bcm4360_attach_env_phy_get_coreflags(void *pi) { return wlc_phy_get_coreflags(pi); }

u32 bcm4360_attach_env_dma_addrwidth(void *sih, void *dmaregs)
{
	return dma_addrwidth(sih, dmaregs);
}

void *bcm4360_attach_env_dma_attach(void *osh, const char *name, void *sih, void *txregs,
				    void *rxregs, u32 ntxd, u32 nrxd, u32 rxbufsize,
				    s32 rxextheadroom, u32 nrxpost, u32 rxoffset)
{
	return dma_attach(osh, name, sih, txregs, rxregs, ntxd, nrxd, rxbufsize, rxextheadroom,
			  nrxpost, rxoffset, &wl_msg_level);
}

/* the engine's first field points to the table of its functions */
#define DMA_FN(di, slot)	((*(const void *const *const *)(di))[(slot) / 8])

void bcm4360_attach_env_dma_detach(void *di)
{
	((void (*)(void *))DMA_FN(di, 0x000))(di);
}

void *bcm4360_attach_env_dma_txavail(void *di)
{
	return ((void *(*)(void *, const char *))DMA_FN(di, 0x108))(di, "&txavail");
}

void bcm4360_attach_env_dma_ctrlflags(void *di, u32 mask, u32 flags)
{
	((u32 (*)(void *, u32, u32))DMA_FN(di, 0x118))(di, mask, flags);
}

void bcm4360_attach_env_dma_burstlen_set(void *di, u8 rxburstlen, u8 txburstlen)
{
	((void (*)(void *, u32, u32))DMA_FN(di, 0x160))(di, rxburstlen, txburstlen);
}

void bcm4360_attach_env_dma_param_set(void *di, u16 id, u16 value)
{
	((void (*)(void *, u32, u32))DMA_FN(di, 0x170))(di, id, value);
}

bool bcm4360_attach_env_alloc_dma_resources(void *wl, u32 addrwidth)
{
	return wl_alloc_dma_resources(wl, addrwidth);
}

u32 bcm4360_attach_env_intrsoff(void *wl) { return wl_intrsoff(wl); }
void bcm4360_attach_env_intrsrestore(void *wl, u32 mask) { wl_intrsrestore(wl, mask); }
