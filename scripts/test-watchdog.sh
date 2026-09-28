#!/bin/sh
# SPDX-License-Identifier: ISC
#
# Checks the decisions of systemd/bcm4360-watchdog on simulated timelines
# (runs every 30 s). Needs no root and touches nothing outside a temp dir.
set -u
cd "$(dirname "$0")/.."
wd=systemd/bcm4360-watchdog
fails=0

# scenario NAME EXPECTED_RELOAD_TIMES STEPS...
# each step: "FROM TO LOADED WIFI STATE AUTOCONNECT" (state code only)
scenario() {
	name=$1 want=$2
	shift 2
	dir=$(mktemp -d)
	got=
	for step in "$@"; do
		set -- $step
		t=$1
		while [ "$t" -le "$2" ]; do
			out=$(BCM4360_WD_TEST=1 BCM4360_WD_DIR=$dir BCM4360_WD_NOW=$t BCM4360_WD_LOADED=$3 \
				BCM4360_WD_WIFI=$4 BCM4360_WD_STATE="$5 (x)" BCM4360_WD_AUTOCONNECT=$6 sh "$wd")
			case $out in *RELOAD*) got="$got $t" ;; esac
			t=$((t + 30))
		done
	done
	rm -rf "$dir"
	got=${got# }
	if [ "$got" = "$want" ]; then
		echo "ok    $name: reloads at [${got}]"
	else
		echo "FAIL  $name: reloads at [${got}], expected [${want}]"
		fails=$((fails + 1))
	fi
}

scenario "stuck after a drop (the 2026-09-28 failures)" "300 900 1500" \
	"0 120 yes enabled 100 yes" "150 2400 yes enabled 30 yes"
scenario "connected all along" "" \
	"0 3600 yes enabled 100 yes"
scenario "never connected (no known network around)" "" \
	"0 3600 yes enabled 30 yes"
scenario "stuck connecting (EAP stall)" "300" \
	"0 120 yes enabled 100 yes" "150 330 yes enabled 50 yes" "360 600 yes enabled 100 yes"
scenario "short drop, reconnects by itself" "" \
	"0 120 yes enabled 100 yes" "150 240 yes enabled 30 yes" "270 3600 yes enabled 100 yes"
scenario "radio switched off" "" \
	"0 120 yes enabled 100 yes" "150 3600 yes disabled 20 yes"
scenario "deliberate disconnect" "" \
	"0 120 yes enabled 100 yes" "150 3600 yes enabled 30 no"
scenario "on the stock wl driver" "" \
	"0 120 no enabled 100 yes" "150 3600 no enabled 30 yes"
scenario "suspended right after a drop" "" \
	"0 120 yes enabled 100 yes" "150 150 yes enabled 30 yes" "1000 1060 yes enabled 30 yes" "1090 3600 yes enabled 100 yes"
scenario "suspended, still stuck after resume" "1180" \
	"0 120 yes enabled 100 yes" "150 150 yes enabled 30 yes" "1030 1300 yes enabled 30 yes" "1330 3600 yes enabled 100 yes"

[ "$fails" -eq 0 ] && echo "all watchdog scenarios pass" || { echo "$fails scenario(s) failed"; exit 1; }
