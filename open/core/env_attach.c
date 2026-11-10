/*
 * The services of open/include/bcm4360/bmac_attach.h, bmac_led.h and dma.h
 * (hybrid/mac_attach.c, mac_led.c, dma.c with the open functions in place of
 * Broadcom's). `void *hw` is the raw wlc_hw, `void *sih` the core's handle,
 * `void *wl` the core itself.
 */
#include "core.h"
#include <bcm4360/bmac_attach.h>
#include <bcm4360/bmac_led.h>
#include <bcm4360/dma.h>
#include <bcm4360/phy_api.h>

#define CS(sih)		(&core_of_sih(sih)->si)

const char *bcm4360_attach_env_getvar(const char *vars, const char *name)
{
	return core_getvar(vars, name);
}

s32 bcm4360_attach_env_getintvar(const char *vars, const char *name)
{
	return core_getintvar(vars, name);
}

void *bcm4360_attach_env_si_attach(u32 device, void *osh, void *regs, u32 bustype,
				   void *btparam, char **vars, u32 *varsz)
{
	/* the core attaches its backplane itself (core_attach), before the MAC layer's attach */
	(void)device; (void)osh; (void)regs; (void)bustype; (void)btparam; (void)vars;
	(void)varsz;
	return NULL;
}

void bcm4360_attach_env_si_detach(void *sih) { core_si_detach(core_of_sih(sih)); }

void *bcm4360_attach_env_si_setcore(void *sih, u32 coreid, u32 unit)
{
	return bcm4360_si_setcore(CS(sih), coreid, unit);
}

u32 bcm4360_attach_env_si_corerev(void *sih) { return bcm4360_si_corerev(CS(sih)); }
u32 bcm4360_attach_env_si_coreidx(void *sih) { return bcm4360_si_coreidx(CS(sih)); }
void bcm4360_attach_env_si_clkctl_init(void *sih) { core_si_clkctl_init(core_of_sih(sih)); }
void bcm4360_attach_env_si_pcie_ltr_war(void *sih) { core_si_pcie_ltr_war(core_of_sih(sih)); }

void bcm4360_attach_env_si_pcieltrenable(void *sih, u32 mask, u32 val)
{
	core_si_pcieltrenable(core_of_sih(sih), mask, val);
}

void bcm4360_attach_env_si_pcieobffenable(void *sih, u32 mask, u32 val)
{
	core_si_pcieobffenable(core_of_sih(sih), mask, val);
}

bool bcm4360_attach_env_si_pci_war16165(void *sih) { return bcm4360_si_pci_war16165(CS(sih)); }
u32 bcm4360_attach_env_si_chip_hostif(void *sih) { return bcm4360_si_chip_hostif(CS(sih)); }

/* chip-attach.md B14: 0 unless bus core 0x820 */
u32 bcm4360_attach_env_si_pcie_get_request_size(void *sih)
{
	(void)sih;
	return 0;
}

void bcm4360_attach_env_si_pmu_rfldo(void *sih, bool on)
{
	bcm4360_pmu_rfldo(&core_of_sih(sih)->pmu, on);
}

void bcm4360_attach_env_si_pci_down(void *sih) { core_si_pci_down(core_of_sih(sih)); }
void bcm4360_attach_env_si_pci_sleep(void *sih) { core_si_pci_sleep(core_of_sih(sih)); }

void bcm4360_attach_env_si_register_intr_callback(void *sih, u32 (*intrsoff)(void *arg),
						  void (*intrsrestore)(void *arg, u32 mask),
						  void *arg)
{
	struct bcm4360_core *c = core_of_sih(sih);

	c->si.intr_coreid = bcm4360_si_coreid(&c->si);
	c->si.intr_arg = arg;
	c->si.intr_off = intrsoff;
	c->si.intr_restore = intrsrestore;
}

void bcm4360_attach_env_si_deregister_intr_callback(void *sih)
{
	struct bcm4360_core *c = core_of_sih(sih);

	c->si.intr_arg = NULL;
	c->si.intr_off = NULL;
	c->si.intr_restore = NULL;
}

/* A13 of the clock group, on the view of wlc_hw */
void bcm4360_attach_env_clkctl_clk(void *hw, u32 mode)
{
	struct bcm4360_core *c = core_of_rawhw(hw);

	CORE_MAC_CALL(c, bcm4360_bmac_clkctl_clk(m, mode));
}

void bcm4360_attach_env_corereset(void *hw, u32 flags)
{
	struct bcm4360_core *c = core_of_rawhw(hw);

	CORE_MAC_CALL(c, bcm4360_bmac_corereset(m, flags));
}

bool bcm4360_attach_env_validate_chip_access(void *hw)
{
	struct bcm4360_core *c = core_of_rawhw(hw);
	bool r = false;

	CORE_MAC_CALL(c, r = bcm4360_bmac_validate_chip_access(m));
	return r;
}

void bcm4360_attach_env_pllreq(void *hw, bool set, u32 req_bit)
{
	struct bcm4360_core *c = core_of_rawhw(hw);

	CORE_MAC_CALL(c, bcm4360_bmac_pllreq(m, set, req_bit));
}

void bcm4360_attach_env_setxband(void *hw, u32 bandunit)
{
	struct bcm4360_core *c = core_of_rawhw(hw);

	CORE_MAC_CALL(c, bcm4360_bmac_setxband(m, bandunit));
}

void bcm4360_attach_env_set_btswitch(void *hw, s8 mode)
{
	struct bcm4360_core *c = core_of_rawhw(hw);

	CORE_MAC_CALL(c, bcm4360_bmac_set_btswitch(m, mode));
}

void bcm4360_attach_env_btc_wire_set(void *hw, s32 wire)
{
	struct bcm4360_core *c = core_of_rawhw(hw);

	CORE_MAC_CALL(c, bcm4360_bmac_btc_wire_set(m, wire));
}

void bcm4360_attach_env_btc_mode_set(void *hw, s32 mode)
{
	struct bcm4360_core *c = core_of_rawhw(hw);

	CORE_MAC_CALL(c, bcm4360_bmac_btc_mode_set(m, mode));
}

void bcm4360_attach_env_coredisable(void *hw)
{
	struct bcm4360_core *c = core_of_rawhw(hw);

	CORE_MAC_CALL(c, bcm4360_bmac_coredisable(m));
}

void bcm4360_attach_env_xtal(void *hw, bool want)
{
	struct bcm4360_core *c = core_of_rawhw(hw);

	CORE_MAC_CALL(c, bcm4360_bmac_xtal(m, want));
}

void *bcm4360_attach_env_led_attach(void *hw) { return bcm4360_bmac_led_attach(hw); }
s32 bcm4360_attach_env_led_detach(void *hw) { return bcm4360_bmac_led_detach(hw); }

/* --- the PHY (acphy-attach.md, acphy-api.md A1, A2) --- */

/* the record wlc_bmac_attach passes to wlc_phy_shared_attach (bmac-attach.md, Data) */
void *bcm4360_attach_env_phy_shared_attach(void *desc)
{
	struct bcm4360_core *c = core_of_sih(FIELD(desc, 0x08, void *));
	struct bcm4360_phy_board *b = &c->board;

	b->chip = FIELD(desc, 0x34, u32);
	b->chiprev = FIELD(desc, 0x38, u32);
	b->chippkg = FIELD(desc, 0x3c, u32);
	b->corerev = FIELD(desc, 0x1c, u32);
	b->sromrev = FIELD(desc, 0x40, u32);
	b->boardtype = FIELD(desc, 0x44, u32);
	b->boardrev = FIELD(desc, 0x48, u32);
	b->boardvendor = FIELD(desc, 0x4c, u32);
	b->boardflags = FIELD(desc, 0x50, u32);
	b->boardflags2 = FIELD(desc, 0x54, u32);
	b->pci_vendor = FIELD(desc, 0x30, u16);
	b->pci_device = FIELD(desc, 0x32, u16);
	b->xtal_hz = bcm4360_pmu_alp_clock(&c->pmu);
	b->pmu = &c->pmu;
	c->phy_shared = true;
	return c;
}

void bcm4360_attach_env_phy_shared_detach(void *phy_sh)
{
	struct bcm4360_core *c = phy_sh;

	if (c)
		c->phy_shared = false;
}

void *bcm4360_attach_env_phy_attach(void *phy_sh, void *regs, s32 bandtype, char *vars)
{
	struct bcm4360_core *c = phy_sh;

	(void)regs; (void)vars;
	if (c->phy)
		return bcm4360_phy_attach_band(c->phy, bandtype) ? c->phy : NULL;
	/* the 2.4 GHz band attaches first (2 = 2.4 GHz, 1 = 5 GHz) */
	if (bandtype != 2)
		return NULL;
	c->phy = bcm4360_phy_attach(c->hw, &c->board);
	return c->phy;
}

void bcm4360_attach_env_phy_detach(void *pi)
{
	struct bcm4360_phy *phy = pi;
	struct bcm4360_core *c = phy ? core_of_hw(phy->hw) : NULL;

	if (!phy)
		return;
	/* the last detach frees the PHY: forget the handle first */
	if (phy->refcnt == 1 && c)
		c->phy = NULL;
	bcm4360_phy_detach(phy);
}

void bcm4360_attach_env_phy_machwcap_set(void *pi, u32 machwcap)
{
	bcm4360_phy_machwcap_set(pi, machwcap);
}

void bcm4360_attach_env_phy_get_phyversion(void *pi, u16 *phytype, u16 *phyrev, u16 *radioid,
					   u16 *radiorev)
{
	bcm4360_phy_get_phyversion(pi, phytype, phyrev, radioid, radiorev);
}

u8 bcm4360_attach_env_phy_get_encore(void *pi) { return bcm4360_phy_get_encore(pi); }
u32 bcm4360_attach_env_phy_get_coreflags(void *pi) { return bcm4360_phy_get_coreflags(pi); }

/* --- the DMA engines --- */

static void dma_bus(struct bcm4360_core *c, struct bcm4360_dma_bus *bus)
{
	bus->ctx = c;
	bus->bustype = c->sihp.bustype;
	bus->buscoreid = c->sihp.buscoretype;
	bus->chip = c->sihp.chip;
}

u32 bcm4360_attach_env_dma_addrwidth(void *sih, void *dmaregs)
{
	struct bcm4360_dma_bus bus;

	dma_bus(core_of_sih(sih), &bus);
	return bcm4360_dma_addrwidth(&bus, dmaregs);
}

void *bcm4360_attach_env_dma_attach(void *osh, const char *name, void *sih, void *txregs,
				    void *rxregs, u32 ntxd, u32 nrxd, u32 rxbufsize,
				    s32 rxextheadroom, u32 nrxpost, u32 rxoffset)
{
	struct bcm4360_core *c = core_of_sih(sih);
	struct bcm4360_dma_bus bus;
	struct bcm4360_dma *di;

	dma_bus(c, &bus);
	di =bcm4360_dma_attach(osh, name, &bus, txregs, rxregs, ntxd, nrxd, rxbufsize,
				rxextheadroom, nrxpost, rxoffset, &c->msg_level);
	/* every transmit frame is the platform's: back to it, never freed here */
	if (di)
		bcm4360_dma_txfree_set(di, core_tx_drop, c);
	return di;
}

void bcm4360_attach_env_dma_detach(void *di) { bcm4360_dma_detach(di); }

void *bcm4360_attach_env_dma_txavail(void *di) { return bcm4360_dma_getvar(di, "&txavail"); }

void bcm4360_attach_env_dma_ctrlflags(void *di, u32 mask, u32 flags)
{
	bcm4360_dma_ctrlflags(di, mask, flags);
}

void bcm4360_attach_env_dma_burstlen_set(void *di, u8 rx, u8 tx)
{
	bcm4360_dma_burstlen_set(di, rx, tx);
}

void bcm4360_attach_env_dma_param_set(void *di, u16 id, u16 value)
{
	bcm4360_dma_param_set(di, id, value);
}

u32 bcm4360_dma_env_core_sflags(void *ctx, u32 mask, u32 val)
{
	return bcm4360_si_core_sflags(&((struct bcm4360_core *)ctx)->si, mask, val);
}

u32 bcm4360_dma_env_coreid(void *ctx) { return bcm4360_si_coreid(&((struct bcm4360_core *)ctx)->si); }
u32 bcm4360_dma_env_corerev(void *ctx) { return bcm4360_si_corerev(&((struct bcm4360_core *)ctx)->si); }

bool bcm4360_dma_env_backplane64(void *ctx)
{
	return bcm4360_si_backplane64(&((struct bcm4360_core *)ctx)->si);
}

/* --- the glue: wl_alloc_dma_resources, wl_intrsoff, wl_intrsrestore --- */

/* the platform sets the DMA mask before attach (drv/main.c) */
bool bcm4360_attach_env_alloc_dma_resources(void *wl, u32 addrwidth)
{
	(void)wl; (void)addrwidth;
	return true;
}

u32 bcm4360_attach_env_intrsoff(void *wl) { return core_intrsoff(wl); }
void bcm4360_attach_env_intrsrestore(void *wl, u32 mask) { core_intrsrestore(wl, mask); }

/* --- the LEDs (hybrid/mac_led.c) --- */

const char *bcm4360_led_env_getvar(const char *vars, const char *name)
{
	return core_getvar(vars, name);
}

void *bcm4360_led_env_init_timer(void *wl, void (*fn)(void *arg), void *arg, const char *name)
{
	return bcm4360_plat_timer_init(((struct bcm4360_core *)wl)->plat, fn, arg, name);
}

void bcm4360_led_env_add_timer(void *wl, void *timer, u32 ms, bool periodic)
{
	bcm4360_plat_timer_add(((struct bcm4360_core *)wl)->plat, timer, ms, periodic);
}

bool bcm4360_led_env_del_timer(void *wl, void *timer)
{
	return bcm4360_plat_timer_del(((struct bcm4360_core *)wl)->plat, timer);
}

void bcm4360_led_env_free_timer(void *wl, void *timer)
{
	bcm4360_plat_timer_free(((struct bcm4360_core *)wl)->plat, timer);
}

/* wl_down: the platform brings the interface down; here the core goes down */
void bcm4360_led_env_wl_down(void *wl) { bcm4360_core_down(wl); }

u32 bcm4360_led_env_gpioout(void *sih, u32 mask, u32 val)
{
	return bcm4360_si_gpioout(CS(sih), mask, val, 0);
}

u32 bcm4360_led_env_gpioouten(void *sih, u32 mask, u32 val)
{
	return bcm4360_si_gpioouten(CS(sih), mask, val, 0);
}

u32 bcm4360_led_env_gpioled(void *sih, u32 mask, u32 val)
{
	return bcm4360_si_gpioled(CS(sih), mask, val);
}

void bcm4360_led_env_xtal(void *hw, bool want) { bcm4360_attach_env_xtal(hw, want); }

bool bcm4360_led_env_deviceremoved(void *hw)
{
	struct bcm4360_core *c = core_of_rawhw(hw);
	bool r = false;

	CORE_MAC_CALL(c, r = bcm4360_bmac_deviceremoved(m));
	return r;
}
