# SPDX-License-Identifier: ISC
#
# Objects of the hybrid build (hybrid/README.md): the adapters in hybrid/ and
# the open code they use. Included by the Makefile; tools/re/hybrid.py reads
# the same list to link the hybrid core for the emulator.

hybrid-objs := hybrid/card.o \
               hybrid/pmu.o open/chip/pmu.o

# The PHY layer (hybrid/phy.c, switch open_phy): the adapter, the open PHY and
# the register access it uses. Not yet in the module: the open PHY does not
# answer all 79 entry points yet (docs/re/spec/acphy-api.md); tools/re/hybrid.py
# links it for the emulator, where an open function that is still missing
# stops the session with its name.
hybrid-phy-objs := hybrid/phy.o open/hw/access.o \
                   open/phy/phy_api.o open/phy/phy_attach.o open/phy/phy_cal.o open/phy/phy_chanspec.o \
                   open/phy/phy_desense.o open/phy/phy_init.o open/phy/phy_rxgain.o \
                   open/phy/phy_txpower.o open/phy/radio2069.o
