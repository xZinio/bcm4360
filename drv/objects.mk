# Objects of bcm4360_open, the open mac80211 driver (docs/re/07-open-driver.md):
# the Linux side (drv/), the core (open/core/) and the open layers below it,
# on Broadcom's OS layer (src/shared/linux_osl.c). No object of
# Broadcom's is linked. Included by the Makefile.

open-drv-objs := drv/main.o drv/mac80211.o drv/fw.o drv/timer.o \
                 src/shared/linux_osl.o \
                 $(patsubst $(src)/%.c,%.o,$(wildcard $(src)/open/core/*.c)) \
                 open/chip/nvram.o open/chip/pmu.o open/chip/si.o open/chip/si_pcie.o \
                 open/chip/srom.o open/hw/access.o \
                 open/lib/cca.o open/lib/channel.o open/lib/clm.o open/lib/rate.o \
                 open/lib/ratepwr.o open/lib/stf.o open/lib/utils.o \
                 open/mac/bmac_attach.o open/mac/bmac_btc.o open/mac/bmac_chan.o \
                 open/mac/bmac_clk.o open/mac/bmac_ctrl.o open/mac/bmac_init.o \
                 open/mac/bmac_led.o open/mac/bmac_mem.o open/mac/bmac_mhf.o \
                 open/mac/bmac_runtime.o open/mac/bmac_updown.o open/mac/dma.o \
                 open/phy/phy_api.o open/phy/phy_attach.o open/phy/phy_cal.o \
                 open/phy/phy_chanspec.o open/phy/phy_desense.o open/phy/phy_init.o \
                 open/phy/phy_rx.o open/phy/phy_rxgain.o open/phy/phy_txpower.o \
                 open/phy/phy_wd.o open/phy/radio2069.o
