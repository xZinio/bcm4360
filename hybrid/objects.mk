# SPDX-License-Identifier: ISC
#
# Objects of the hybrid build (hybrid/README.md): the adapters in hybrid/ and
# the open code they use. Included by the Makefile; tools/re/hybrid.py reads
# the same list to link the hybrid core for the emulator.

hybrid-objs := hybrid/card.o \
               hybrid/pmu.o open/chip/pmu.o \
               hybrid/access.o open/hw/access.o \
               hybrid/mac.o open/mac/bmac_ctrl.o open/mac/bmac_mhf.o \
               hybrid/mac_mem.o open/mac/bmac_mem.o \
               hybrid/mac_clk.o open/mac/bmac_clk.o \
               hybrid/mac_init.o open/mac/bmac_init.o \
               hybrid/mac_updown.o open/mac/bmac_updown.o \
               hybrid/mac_chan.o open/mac/bmac_chan.o \
               hybrid/mac_runtime.o open/mac/bmac_runtime.o \
               hybrid/mac_btc.o open/mac/bmac_btc.o \
               hybrid/utils.o open/lib/utils.o \
               hybrid/clm.o open/lib/clm.o \
               hybrid/ppr.o open/lib/ratepwr.o \
               hybrid/srom.o open/chip/srom.o \
               hybrid/si.o open/chip/si.o \
               hybrid/dma.o open/mac/dma.o \
               hybrid/chan.o open/lib/channel.o \
               hybrid/iovar.o open/lib/iovar.o

# Being written, one variable hybrid-new*-objs per layer: each is linked by
# tools/re/hybrid.py for the emulator once ALL its sources exist, not yet in
# the module.
# The offload semaphore (switch open_mac), the global NVRAM (open_nvram), the
# module table (open_iovar): task docs/re/tasks/small.md.
hybrid-new-olsem-objs := hybrid/mac_sem.o open/mac/offload.o
hybrid-new-nvram-objs := hybrid/nvram.o open/chip/nvram.o
hybrid-new-module-objs := hybrid/iovar_mod.o open/lib/module.o
# The PCIe bridge, si_survive_perst_war (switch open_si): the same task.
hybrid-new-perst-objs := hybrid/si_perst.o open/chip/si_pcie.o

# The PHY layer (hybrid/phy.c, switch open_phy, default off): the adapter and
# the open PHY (its register access, open/hw/access.o, is in the list above).
# All 79 entry points exist (docs/re/spec/acphy-api*.md) and the whole session
# of tools/re/hybrid.py is identical to the object with open_phy on. It is in
# the module with the switch off until it has run on a card.
hybrid-phy-objs := hybrid/phy.o \
                   open/phy/phy_api.o open/phy/phy_attach.o open/phy/phy_cal.o open/phy/phy_chanspec.o \
                   open/phy/phy_desense.o open/phy/phy_init.o open/phy/phy_rxgain.o \
                   open/phy/phy_txpower.o open/phy/radio2069.o \
                   open/phy/phy_rx.o open/phy/phy_wd.o

