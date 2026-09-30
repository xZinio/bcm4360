# SPDX-License-Identifier: ISC
#
# bcm4360: Broadcom BCM4360 802.11ac driver for Linux (MacBook Air 2013-2017).
#
# The Linux/cfg80211 side (src/, ISC licensed) is open source. It links
# against Broadcom's proprietary 802.11 core, lib/wlc_hybrid.o_shipped, which
# is NOT part of this tree: `make fetch` downloads Broadcom's original release
# and verifies its checksums before unpacking it into lib/.

BCM4360_VERSION := 1.0.2

ifneq ($(KERNELRELEASE),)
# ---- Kbuild -----------------------------------------------------------------

obj-m        := bcm4360.o
bcm4360-objs := src/shared/linux_osl.o \
                src/wl/sys/wl_linux.o \
                src/wl/sys/wl_iw.o \
                src/wl/sys/wl_cfg80211_hybrid.o

ccflags-y := -DUSE_CFG80211 -DBCM4360_DRV_VERSION=\"$(BCM4360_VERSION)\" -Wno-date-time
ccflags-y += -I$(src)/src/include -I$(src)/src/common/include
ccflags-y += -I$(src)/src/wl/sys -I$(src)/src/shared/bcmwifi/include

ldflags-y := $(src)/lib/wlc_hybrid.o_shipped

# objtool cannot process Broadcom's prebuilt 2015 object and its errors are
# fatal since Linux 6.15, so it is skipped for the final link of the module,
# the same way distributions package this core (e.g. Arch's broadcom-wl-dkms).
bcm4360.o: override objtool-enabled =

else
# ---- convenience targets ------------------------------------------------------

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build

all: lib/wlc_hybrid.o_shipped
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

lib/wlc_hybrid.o_shipped:
	@echo "lib/wlc_hybrid.o_shipped is missing: run 'make fetch' first" >&2
	@false

fetch:
	sh scripts/fetch-blob.sh

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

install:
	sh scripts/install.sh

uninstall:
	sh scripts/uninstall.sh

.PHONY: all fetch clean install uninstall
endif
