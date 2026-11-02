#!/usr/bin/env bash
# SPDX-License-Identifier: ISC
#
# Test the open layers of the hybrid module on the card, step by step, from
# Broadcom's code everywhere to the most open configuration. Each step is one
# run of swap-test.sh (link check, kernel log check, automatic rollback).
#
#   layer-test.sh [MODULE] [--fallback MOD] [--from STEP] [--reconnects N]
#
# MODULE: the module to test (default bcm4360, i.e. the installed one; or the
# path of a freshly built bcm4360.ko). The steps:
#   1  every switch off: the hybrid build running Broadcom's own functions
#      (tests the build mechanism itself)
#   2  the defaults: every layer open except the PHY
#   3  the defaults and the open PHY (open_phy=1)
# The run stops at the first failing step and prints how to find the layer at
# fault. Needs root. The Wi-Fi link goes down while it runs: start it
# detached, e.g.
#
#   systemd-run --unit=bcm4360-layers --collect \
#       systemd-inhibit --what=sleep:idle --why="Wi-Fi driver test" \
#       /path/to/scripts/layer-test.sh
#
# and read /var/log/bcm4360-swap-test.log afterwards (hybrid/README.md,
# "Testing on the card").
set -u

here=$(cd "$(dirname "$0")" && pwd)
module=bcm4360
fallback=wl
from=1
reconnects=1
case ${1:-} in --*|'') ;; *) module=$1; shift ;; esac
while [ $# -gt 0 ]; do
	case $1 in
	--fallback) fallback=$2; shift 2 ;;
	--from) from=$2; shift 2 ;;
	--reconnects) reconnects=$2; shift 2 ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ "$(id -u)" -eq 0 ] || { echo "layer-test.sh must run as root" >&2; exit 2; }

# every switch of the hybrid build (hybrid/card.c and the adapters)
layers="pmu access mac utils clm ppr srom si dma phy"
all_off=
for l in $layers; do all_off="$all_off open_$l=0"; done

steps=("$all_off" "" "open_phy=1")
names=("all switches off (Broadcom's code everywhere)" "defaults (all layers open but the PHY)" "defaults + open PHY")

log=/var/log/bcm4360-swap-test.log
for i in 1 2 3; do
	[ "$i" -lt "$from" ] && continue
	p=${steps[$((i - 1))]}
	printf '[%s] ===== layer-test step %d: %s =====\n' "$(date +%T)" "$i" "${names[$((i - 1))]}" >>"$log"
	if ! bash "$here/swap-test.sh" "$module" --fallback "$fallback" --reconnects "$reconnects" \
		${p:+--params "$p"}; then
		{
			printf '[%s] layer-test: step %d FAILED (%s)\n' "$(date +%T)" "$i" "${names[$((i - 1))]}"
			if [ "$i" -eq 1 ]; then
				echo "  The hybrid build fails even on Broadcom's code: report the log above."
			else
				echo "  To find the layer at fault, repeat with one layer off at a time, e.g."
				echo "    swap-test.sh $module --params \"open_dma=0\""
				echo "  for each of: $layers (open_phy=0 is the default)."
			fi
		} >>"$log"
		echo "step $i failed; see $log"
		exit 1
	fi
done
printf '[%s] layer-test: all steps passed\n' "$(date +%T)" >>"$log"
echo "all steps passed; see $log"
