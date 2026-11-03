#!/usr/bin/env bash
# SPDX-License-Identifier: ISC
#
# Bring Wi-Fi back to the installed driver after (or instead of) card-test.sh:
#
#   card-test-restore.sh [--out DIR] [--owner USER] [--conn NAME] [--hotspot NAME]
#
# Called by card-test.sh when it ends (normally, failed or killed) and by its
# deadman timer, which fires if the test runs past its deadline. Idempotent
# and serialised with a lock, so both may run. In this order, until
# NetworkManager reports the Wi-Fi device connected:
#   1. stop the test unit, end any mmiotrace, put all CPUs back online,
#      remove the temporary 2.4 GHz test profile;
#   2. if the loaded bcm4360 is not the installed one (another build or other
#      parameters), rmmod it and modprobe the installed one (never
#      `modprobe -r`: it unloads cfg80211 and leaves wpa_supplicant with a
#      stale nl80211 id);
#   3. restart wpa_supplicant and NetworkManager, activate the connection;
#   4. Broadcom's stock wl;
#   5. the hotspot profile, if given.
# Then the bcm4360 watchdog timer is started again and the deadman stopped.
set -u

out=/var/log
owner=
conn=
hotspot=
while [ $# -gt 0 ]; do
	case $1 in
	--out) out=$2; shift 2 ;;
	--owner) owner=$2; shift 2 ;;
	--conn) conn=$2; shift 2 ;;
	--hotspot) hotspot=$2; shift 2 ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ "$(id -u)" -eq 0 ] || { echo "must run as root" >&2; exit 2; }

log=$out/restore.log
mkdir -p "$out"
exec 9>/run/bcm4360-card-test-restore.lock
flock 9
exec >>"$log" 2>&1
say() { printf '[%s] restore: %s\n' "$(date +%T)" "$*"; }

TEST_PROFILE=bcm4360-test-24
TR=/sys/kernel/tracing
[ -d "$TR" ] || TR=/sys/kernel/debug/tracing

wifi_iface() {
	local d
	for d in /sys/class/net/*; do
		[ -e "$d/phy80211" ] && { basename "$d"; return 0; }
	done
	return 1
}
connected() {
	local dev
	dev=$(wifi_iface) || return 1
	[ "$(nmcli -g GENERAL.STATE dev show "$dev" 2>/dev/null)" = "100 (connected)" ]
}
wait_connected() {
	local t=0
	while [ "$t" -lt "$1" ]; do
		connected && { say "connected after ${t}s"; return 0; }
		sleep 2
		t=$((t + 2))
	done
	say "not connected after $1 s"
	return 1
}
activate() {
	local dev
	dev=$(wifi_iface) || return 1
	[ -n "$1" ] || return 1
	say "nmcli con up '$1' ifname $dev"
	nmcli --wait 60 con up "$1" ifname "$dev" || true
}
finish() {
	systemctl start bcm4360-watchdog.timer 2>/dev/null && say "watchdog timer started"
	[ -n "$owner" ] && chown -R "$owner": "$out" 2>/dev/null
	say "done: driver $(lsmod | awk '$1 == "wl" || $1 == "bcm4360" { print $1 }' | head -n 1)" \
	    "$(cat /sys/module/bcm4360/version 2>/dev/null)," \
	    "$(connected && echo connected || echo NOT connected)"
	# last: this may be the deadman's own unit
	systemctl stop bcm4360-card-deadman.timer 2>/dev/null
	exit 0
}

say "===== restore ====="
if systemctl is-active -q bcm4360-card-test.service 2>/dev/null &&
	[ "${INVOCATION_ID:-}" != "$(systemctl show -P InvocationID bcm4360-card-test.service)" ]; then
	say "stopping the test unit"
	# tells the test's trap not to run this script itself
	touch /run/bcm4360-card-test.deadman
	systemctl stop bcm4360-card-test.service
fi
if [ "$(cat "$TR/current_tracer" 2>/dev/null)" = mmiotrace ]; then
	echo nop > "$TR/current_tracer" && say "mmiotrace ended"
fi
pkill -f "cat $TR/trace_pipe" 2>/dev/null
for c in /sys/devices/system/cpu/cpu[0-9]*/online; do
	[ "$(cat "$c")" = 1 ] || { echo 1 > "$c" && say "${c%/online} back online"; }
done
nmcli -t -f NAME con show | grep -qx "$TEST_PROFILE" &&
	nmcli con delete "$TEST_PROFILE" && say "removed the profile $TEST_PROFILE"

# The test loads other builds and parameters; it marks that in this file.
DIRTY=/run/bcm4360-card-test.dirty
installed=$(modinfo -F srcversion bcm4360 2>/dev/null)
loaded=$(cat /sys/module/bcm4360/srcversion 2>/dev/null)
if lsmod | grep -q '^wl '; then
	say "wl is loaded (the fallback); leaving it"
elif [ -z "$loaded" ] || [ "$loaded" != "$installed" ] || [ -e "$DIRTY" ]; then
	say "loaded bcm4360: ${loaded:-none} $(cat /sys/module/bcm4360/version 2>/dev/null)," \
	    "installed: ${installed:-none} $(modinfo -F version bcm4360 2>/dev/null)"
	if [ -n "$loaded" ]; then
		say "rmmod bcm4360"
		timeout 60 rmmod bcm4360 || say "rmmod failed or hung"
	fi
	say "modprobe bcm4360"
	if modprobe bcm4360; then rm -f "$DIRTY"; else say "modprobe bcm4360 failed"; fi
	# give NetworkManager its own autoconnect first
	sleep 5
fi
[ -n "$conn" ] || conn=$(nmcli -g NAME,TYPE con show --active 2>/dev/null |
	awk -F: '$2 == "802-11-wireless" { print $1; exit }')

if wait_connected 60; then finish; fi
activate "$conn"
if wait_connected 60; then finish; fi

say "restarting wpa_supplicant and NetworkManager"
systemctl restart wpa_supplicant NetworkManager
sleep 10
activate "$conn"
if wait_connected 90; then finish; fi

say "falling back to Broadcom's wl"
timeout 60 rmmod bcm4360 2>/dev/null
modprobe wl || say "modprobe wl failed"
systemctl restart wpa_supplicant
sleep 10
activate "$conn"
if wait_connected 90; then finish; fi

if [ -n "$hotspot" ]; then
	activate "$hotspot"
	if wait_connected 60; then finish; fi
fi
say "NOT connected; reboot to recover (the installed bcm4360 loads at boot)"
finish
