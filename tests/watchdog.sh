#!/bin/sh
# Tests of systemd/bcm4360-watchdog in its test mode (BCM4360_WD_TEST=1):
# sequences of checks with made-up observations, and the action each one
# must end in. No root, no card, no NetworkManager needed.
#
#   sh tests/watchdog.sh [SHELL]    SHELL runs the watchdog (default: sh)
set -u

cd "$(dirname "$0")/.."
wd=systemd/bcm4360-watchdog
run_sh=${1:-sh}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
fails=0 n=0

# defaults of a check: bcm4360, connected, gateway answers
reset() {
	rm -rf "$tmp/state"
	export BCM4360_WD_TEST=1 BCM4360_WD_DIR="$tmp/state" BCM4360_WD_LOADED=yes
	export BCM4360_WD_WIFI=enabled BCM4360_WD_AUTOCONNECT=yes BCM4360_WD_DRIVER=bcm4360
	export BCM4360_WD_STATE="100 (connected)" BCM4360_WD_PROBE=ok
	export BCM4360_WD_IFACE=wlan0 BCM4360_WD_APS=1 BCM4360_WD_KNOWN=no
	t=1000000
}

# check SECONDS_LATER: one check that much later; its last line in $out
check() {
	t=$((t + $1))
	out=$(BCM4360_WD_NOW=$t "$run_sh" "$wd" | tail -n 1)
}

# checks EVERY UNTIL: checks every EVERY s for UNTIL s; stops at the first action
checks() {
	_end=$((t + $2))
	while [ "$t" -lt "$_end" ]; do
		check "$1"
		[ -z "$out" ] || return 0
	done
}

expect() {	# expect NAME WANTED
	n=$((n + 1))
	if [ "$out" = "$2" ]; then
		echo "ok   $1"
	else
		echo "FAIL $1: got '${out}', want '$2'"
		fails=$((fails + 1))
	fi
}

# ---- bcm4360 ------------------------------------------------------------------

reset
checks 30 3600
expect "connected for an hour: nothing" ""

reset
checks 30 120
BCM4360_WD_STATE="30 (disconnected)"
checks 30 140
expect "disconnected 140 s after a connection: not yet" ""
checks 30 60
expect "disconnected 150 s and more after a connection: reload" RELOAD

reset
BCM4360_WD_STATE="30 (disconnected)"
checks 30 1800
expect "never connected (no network around): nothing" ""

reset
checks 30 120
BCM4360_WD_STATE="30 (disconnected)" BCM4360_WD_AUTOCONNECT=no
checks 30 600
expect "deliberate disconnect (autoconnect off): nothing" ""

reset
checks 30 120
BCM4360_WD_STATE="30 (disconnected)" BCM4360_WD_WIFI=disabled
checks 30 600
expect "radio off: nothing" ""

reset
checks 30 120
BCM4360_WD_STATE="30 (disconnected)"
check 30
check 600	# a suspend: the gap starts the count over
checks 30 120
expect "suspended while disconnected: counting starts over" ""

reset
checks 30 120
BCM4360_WD_PROBE="fail 192.168.1.1"
check 30
expect "gateway silent once: nothing yet" ""
check 30
expect "gateway silent twice: reconnect" RECONNECT
checks 30 60
BCM4360_WD_PROBE=ok
checks 30 60
BCM4360_WD_PROBE="fail 192.168.1.1"
checks 30 120
expect "stall again within 5 minutes of the reconnect: reload" RELOAD

reset
BCM4360_WD_PROBE="fail 192.168.1.1"
checks 30 50
expect "gateway silent in the first minute of a connection: nothing" ""

reset
BCM4360_WD_STATE="30 (disconnected)" BCM4360_WD_IFACE=
checks 30 1800
expect "bcm4360 without an interface: nothing (not bcm4360_open's rule)" ""

# ---- bcm4360_open --------------------------------------------------------------

reset
BCM4360_WD_DRIVER=bcm4360_open
checks 30 120
BCM4360_WD_STATE="30 (disconnected)"
checks 30 200
expect "bcm4360_open disconnected after a connection: switch to bcm4360" FALLBACK

reset
BCM4360_WD_DRIVER=bcm4360_open BCM4360_WD_IFACE= BCM4360_WD_STATE= BCM4360_WD_WIFI=
BCM4360_WD_AUTOCONNECT=
checks 30 270
expect "bcm4360_open without an interface (failed probe), 4.5 min: not yet" ""
checks 30 60
expect "bcm4360_open without an interface, 5 min: switch" FALLBACK

reset
BCM4360_WD_DRIVER=bcm4360_open BCM4360_WD_STATE="30 (disconnected)" BCM4360_WD_APS=0
checks 30 1800
expect "bcm4360_open never connected, no access point seen: switch" FALLBACK

reset
BCM4360_WD_DRIVER=bcm4360_open BCM4360_WD_STATE="30 (disconnected)" BCM4360_WD_KNOWN=yes
checks 30 1800
expect "bcm4360_open never connected, a saved network in range: switch" FALLBACK

reset
BCM4360_WD_DRIVER=bcm4360_open BCM4360_WD_STATE="30 (disconnected)" BCM4360_WD_APS=12
checks 30 1800
expect "bcm4360_open never connected, only unknown networks: nothing" ""

reset
BCM4360_WD_DRIVER=bcm4360_open BCM4360_WD_STATE="30 (disconnected)" BCM4360_WD_APS=0
BCM4360_WD_WIFI=disabled
checks 30 1800
expect "bcm4360_open, radio off: nothing" ""

reset
BCM4360_WD_DRIVER=bcm4360_open BCM4360_WD_STATE="70 (connecting (getting IP configuration))"
BCM4360_WD_KNOWN=yes
checks 30 240
BCM4360_WD_STATE="100 (connected)"
checks 30 1800
expect "bcm4360_open connects within 5 min: nothing" ""

reset
BCM4360_WD_DRIVER=bcm4360_open BCM4360_WD_STATE="30 (disconnected)" BCM4360_WD_KNOWN=yes
checks 30 240
check 600	# a suspend before the 5 minutes were up
checks 30 240
expect "bcm4360_open, suspended before its first join: counting starts over" ""

echo
if [ "$fails" -eq 0 ]; then
	echo "watchdog: all $n tests passed"
else
	echo "watchdog: $fails of $n tests failed"
	exit 1
fi
