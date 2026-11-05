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
# a module command that cannot block this script: a driver stuck in the
# kernel (in probe or remove) leaves it unkillable, and then only a reboot
# helps. Gives up waiting after $1 seconds.
bounded() {
	local s=$1 p t=0
	shift
	"$@" &
	p=$!
	while kill -0 "$p" 2>/dev/null; do
		[ "$t" -ge "$s" ] && { say "$* hangs: reboot to recover"; return 124; }
		sleep 1
		t=$((t + 1))
	done
	wait "$p"
}
try_rmmod() { bounded 60 rmmod "$1"; }
# a desktop notification for the owner (no network: they may not see the log)
notify_owner() {
	local uid
	[ -n "$owner" ] && uid=$(id -u "$owner" 2>/dev/null) || return 0
	timeout 10 runuser -u "$owner" -- env DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$uid/bus" \
		notify-send -u critical -a bcm4360 "$1" "$2" >/dev/null 2>&1 || true
}
wedged=0
finish() {
	if [ "$wedged" -eq 0 ]; then
		systemctl start bcm4360-watchdog.timer 2>/dev/null && say "watchdog timer started"
	else
		# its reload would block on the held card as well
		say "watchdog timer left stopped: the card is held until a reboot"
	fi
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
# the tracer cannot be changed while trace_pipe is open (EBUSY): reader first
pkill -f "cat $TR/trace_pipe" 2>/dev/null && sleep 1
if [ "$(cat "$TR/current_tracer" 2>/dev/null)" = mmiotrace ]; then
	echo nop > "$TR/current_tracer" && say "mmiotrace ended"
fi
for c in /sys/devices/system/cpu/cpu[0-9]*/online; do
	[ "$(cat "$c")" = 1 ] || { echo 1 > "$c" && say "${c%/online} back online"; }
done
# the test may have been stopped between radio off and on
[ "$(nmcli radio wifi)" = enabled ] || { nmcli radio wifi on && say "Wi-Fi radio switched on again"; }
nmcli -t -f NAME con show | grep -qx "$TEST_PROFILE" &&
	nmcli con delete "$TEST_PROFILE" && say "removed the profile $TEST_PROFILE"

# The open driver (scripts/open-test.sh) has the card: remove it first; the
# installed bcm4360 is then loaded below. NetworkManager and wpa_supplicant,
# which that test stops, are started again in step 3.
if lsmod | grep -q '^bcm4360_open '; then
	for d in /sys/class/net/*; do
		[ "$(basename "$(readlink -f "$d/device/driver" 2>/dev/null)")" = bcm4360_open ] &&
			ip link set "$(basename "$d")" down 2>/dev/null
	done
	say "rmmod bcm4360_open"
	try_rmmod bcm4360_open || say "rmmod bcm4360_open failed"
fi
systemctl is-active -q wpa_supplicant || { systemctl start wpa_supplicant && say "wpa_supplicant started"; }
systemctl is-active -q NetworkManager || { systemctl start NetworkManager && say "NetworkManager started"; }
# Still loaded: it crashed in the kernel (an oops in its probe leaves it
# "coming" with the card's device lock held) or its remove hangs. No driver
# can have the card until a reboot; loading one would block as well.
if lsmod | grep -q '^bcm4360_open '; then
	wedged=1
	say "bcm4360_open cannot be removed (state $(cat /sys/module/bcm4360_open/initstate 2>/dev/null)):" \
	    "it holds the card until a reboot"
	journalctl -k -b --no-pager -o cat 2>/dev/null | grep -E -m 3 -A 1 'BUG:|Oops|general protection' |
		sed 's/^/    /'
	notify_owner "Wi-Fi: reboot needed" \
		"The open driver test crashed in the kernel and holds the Wi-Fi card. Reboot to get Wi-Fi back (the installed driver loads at boot). Log: $out"
	say "NOT connected; reboot to recover"
	finish
fi

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
		try_rmmod bcm4360 || say "rmmod bcm4360 failed"
	fi
	say "modprobe bcm4360"
	if bounded 120 modprobe bcm4360; then rm -f "$DIRTY"; else say "modprobe bcm4360 failed"; fi
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
try_rmmod bcm4360 2>/dev/null
bounded 120 modprobe wl || say "modprobe wl failed"
systemctl restart wpa_supplicant
sleep 10
activate "$conn"
if wait_connected 90; then finish; fi

if [ -n "$hotspot" ]; then
	activate "$hotspot"
	if wait_connected 60; then finish; fi
fi
say "NOT connected; reboot to recover (the installed bcm4360 loads at boot)"
notify_owner "Wi-Fi: not connected" "The card test could not reconnect. Reboot to recover. Log: $out"
finish
