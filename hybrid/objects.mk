# SPDX-License-Identifier: ISC
#
# Objects of the hybrid build (hybrid/README.md): the adapters in hybrid/ and
# the open code they use. Included by the Makefile; tools/re/hybrid.py reads
# the same list to link the hybrid core for the emulator.

hybrid-objs := hybrid/card.o \
               hybrid/pmu.o open/chip/pmu.o \
               hybrid/access.o open/hw/access.o \
               hybrid/mac.o open/mac/bmac_ctrl.o open/mac/bmac_mhf.o \
               hybrid/utils.o open/lib/utils.o

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
