# SPDX-License-Identifier: ISC
#
# Objects of the hybrid build (hybrid/README.md): the adapters in hybrid/ and
# the open code they use. Included by the Makefile; tools/re/hybrid.py reads
# the same list to link the hybrid core for the emulator.

hybrid-objs := hybrid/card.o \
               hybrid/pmu.o open/chip/pmu.o
