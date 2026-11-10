# bcm4360: Broadcom BCM4360 802.11ac driver for Linux (MacBook Air 2013-2017).
#
# The Linux/cfg80211 side is in src/. It links against Broadcom's 802.11
# core, lib/wlc_hybrid.o_shipped, which is not part of this tree: `make fetch`
# downloads Broadcom's original release and verifies its checksums before
# unpacking it into lib/. Open code (hybrid/, open/) takes over parts of that
# core at link time.

# the version: dkms.conf's, the one place it is set
BCM4360_VERSION := $(shell sed -n 's/^PACKAGE_VERSION="\(.*\)"$$/\1/p' \
			$(or $(src),$(CURDIR))/dkms.conf 2>/dev/null)

ifneq ($(KERNELRELEASE),)
# ---- Kbuild -----------------------------------------------------------------

# HYBRID=1 (default): open code (hybrid/, open/) takes over parts of
# Broadcom's core inside the module.
# HYBRID=0: Broadcom's core alone, as up to version 1.0.3.
HYBRID ?= 1
# BCM4360_MODULES: which modules to build: bcm4360 (Broadcom's core with the
# open layers, the default driver) and/or bcm4360_open (the open mac80211
# driver, docs/re/07-open-driver.md; needs no Broadcom code). DKMS builds each
# as its own package (dkms.conf, dkms-open.conf), so that one failing to build
# for a new kernel never keeps the other from being installed.
BCM4360_MODULES ?= bcm4360

obj-m := $(addsuffix .o,$(BCM4360_MODULES))

ccflags-y := -DUSE_CFG80211 -DBCM4360_DRV_VERSION=\"$(BCM4360_VERSION)\" -Wno-date-time
ccflags-y += -I$(src)/src/include -I$(src)/src/common/include
ccflags-y += -I$(src)/src/wl/sys -I$(src)/src/shared/bcmwifi/include
# the open code (open/, hybrid/, drv/) on Broadcom's OS layer
ccflags-y += -DBCM4360_OSL -I$(src)/open/include

# bcm4360_open: drv/, the open core and layers; not in bcm4360's DKMS tree
ifneq ($(filter bcm4360_open,$(BCM4360_MODULES)),)
include $(src)/drv/objects.mk
bcm4360_open-objs := $(open-drv-objs)
endif

# bcm4360: Broadcom's core and the open layers; drv/ is not part of its tree
ifneq ($(filter bcm4360,$(BCM4360_MODULES)),)
bcm4360-objs := src/shared/linux_osl.o \
                src/wl/sys/wl_linux.o \
                src/wl/sys/wl_cfg80211_hybrid.o

ifeq ($(HYBRID),1)
# the adapters and the open code they use
include $(src)/hybrid/objects.mk
bcm4360-objs += $(hybrid-objs) $(hybrid-phy-objs)
ccflags-y += -DBCM4360_HYBRID

# Broadcom's core with the functions the open code takes over made weak
core := $(obj)/lib/wlc_hybrid_open.o
$(obj)/bcm4360.o: $(core)
$(core): $(src)/lib/wlc_hybrid.o_shipped $(src)/hybrid/takeover.txt $(src)/scripts/mkhybrid.sh
	$(Q)NM="$(NM)" OBJCOPY="$(OBJCOPY)" $(CONFIG_SHELL) $(src)/scripts/mkhybrid.sh \
		$< $@ $(src)/hybrid/takeover.txt
else
core := $(src)/lib/wlc_hybrid.o_shipped
endif

# linked into bcm4360.ko only, never into bcm4360_open.ko
LDFLAGS_bcm4360.o := $(core)
clean-files := lib/wlc_hybrid_open.o

# objtool cannot process Broadcom's prebuilt 2015 object and its errors are
# fatal since Linux 6.15, so it is skipped for the final link of the module,
# the same way distributions package this core (e.g. Arch's broadcom-wl-dkms).
bcm4360.o: override objtool-enabled =
endif

else
# ---- convenience targets ------------------------------------------------------

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
# A kernel built with clang needs its modules built with clang too: gcc fails
# on the compiler flags it inherits. DKMS does this by itself.
LLVM_FLAG := $(if $(shell grep -s '^CONFIG_CC_IS_CLANG=y' $(KDIR)/.config),LLVM=1)
KBUILD = $(MAKE) -C $(KDIR) M=$(CURDIR) $(LLVM_FLAG)

all: lib/wlc_hybrid.o_shipped
	$(KBUILD) modules

# the open driver alone (bcm4360_open.ko)
open:
	$(KBUILD) BCM4360_MODULES=bcm4360_open modules

lib/wlc_hybrid.o_shipped:
	@echo "lib/wlc_hybrid.o_shipped is missing: run 'make fetch' first" >&2
	@false

fetch:
	sh scripts/fetch-blob.sh

clean:
	$(KBUILD) BCM4360_MODULES='bcm4360 bcm4360_open' clean

install:
	sh scripts/install.sh

uninstall:
	sh scripts/uninstall.sh

# the tests that need neither root nor the card (also run by CI)
check:
	sh tests/watchdog.sh

.PHONY: all open fetch clean install uninstall check
endif
