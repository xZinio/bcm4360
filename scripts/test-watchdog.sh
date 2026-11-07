#!/bin/sh
# SPDX-License-Identifier: ISC
#
# Checks the decisions of systemd/bcm4360-watchdog on simulated timelines
# (runs every 30 s). Needs no root and touches nothing outside a temp dir.
set -u
cd "$(dirname "$0")/.."
wd=systemd/bcm4360-watchdog
fails=0

# scenario NAME EXPECTED_ACTIONS STEPS...
# each step: "FROM TO LOADED WIFI STATE AUTOCONNECT [PROBE]" (state code only;
# PROBE: whether the gateway answers, ok or fail, default ok). Actions are
# listed as R<time> (reload) and C<time> (reconnect).
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
				BCM4360_WD_WIFI=$4 BCM4360_WD_STATE="$5 (x)" BCM4360_WD_AUTOCONNECT=$6 \
				BCM4360_WD_PROBE="${7:-ok}" sh "$wd")
			case $out in *RELOAD*) got="$got R$t" ;; *RECONNECT*) got="$got C$t" ;; esac
			t=$((t + 30))
		done
	done
	rm -rf "$dir"
	got=${got# }
	if [ "$got" = "$want" ]; then
		echo "ok    $name: [${got}]"
	else
		echo "FAIL  $name: [${got}], expected [${want}]"
		fails=$((fails + 1))
	fi
}

scenario "stuck after a drop (the 2026-09-28 failures)" "R300 R900 R1500" \
	"0 120 yes enabled 100 yes" "150 2400 yes enabled 30 yes"
scenario "connected all along" "" \
	"0 3600 yes enabled 100 yes"
scenario "never connected (no known network around)" "" \
	"0 3600 yes enabled 30 yes"
scenario "stuck connecting (EAP stall)" "R300" \
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
scenario "suspended, still stuck after resume" "R1180" \
	"0 120 yes enabled 100 yes" "150 150 yes enabled 30 yes" "1030 1300 yes enabled 30 yes" "1330 3600 yes enabled 100 yes"

# stalls: associated (state 100), but the gateway does not answer
scenario "gateway answers all along" "" \
	"0 3600 yes enabled 100 yes ok"
scenario "short stall (one run), recovers by itself" "" \
	"0 300 yes enabled 100 yes ok" "330 330 yes enabled 100 yes fail" "360 3600 yes enabled 100 yes ok"
scenario "stall, a reconnect fixes it" "C360" \
	"0 300 yes enabled 100 yes ok" "330 360 yes enabled 100 yes fail" "390 3600 yes enabled 100 yes ok"
scenario "stall comes back soon after the reconnect: reload" "C360 R510" \
	"0 300 yes enabled 100 yes ok" "330 360 yes enabled 100 yes fail" "390 450 yes enabled 100 yes ok" \
	"480 510 yes enabled 100 yes fail" "540 3600 yes enabled 100 yes ok"
scenario "stall that never ends: reconnect, reload, then paced" "C360 R450 C540 C720 C900 R1050 C1140 C1320 C1500 R1650 C1740" \
	"0 300 yes enabled 100 yes ok" "330 1800 yes enabled 100 yes fail"
scenario "stalls far apart: reconnects only" "C360 C1260" \
	"0 300 yes enabled 100 yes ok" "330 360 yes enabled 100 yes fail" "390 1200 yes enabled 100 yes ok" \
	"1230 1260 yes enabled 100 yes fail" "1290 3600 yes enabled 100 yes ok"
scenario "no answer in the first minute of a connection" "" \
	"0 30 yes enabled 100 yes fail" "60 3600 yes enabled 100 yes ok"
scenario "stall with the radio switched off" "" \
	"0 300 yes enabled 100 yes ok" "330 3600 yes disabled 100 yes fail"
scenario "stall on the stock wl driver" "" \
	"0 300 no enabled 100 yes ok" "330 3600 no enabled 100 yes fail"
scenario "stall, then a suspend in between" "" \
	"0 300 yes enabled 100 yes ok" "330 330 yes enabled 100 yes fail" "1000 1000 yes enabled 100 yes fail" \
	"1030 3600 yes enabled 100 yes ok"

[ "$fails" -eq 0 ] && echo "all watchdog scenarios pass" || { echo "$fails scenario(s) failed"; exit 1; }
