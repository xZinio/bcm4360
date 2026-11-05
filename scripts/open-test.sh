#!/usr/bin/env bash
# SPDX-License-Identifier: ISC
#
# The open driver bcm4360_open (docs/re/07-open-driver.md) on the card, and
# the installed driver back afterwards, whatever happens:
#
#   open-test.sh KO [--stage rx] [--owner USER] [--deadline MIN] [--data FILE]
#
# KO: the bcm4360_open.ko to test (`make open`). --data: Broadcom's object or
# download for the firmware file, if /lib/firmware/bcm4360/bcm4360-data.bin
# does not exist yet (default: lib/wlc_hybrid.o_shipped of this tree).
#
# Stages:
#   rx  (milestone 2) receive only. NetworkManager and wpa_supplicant are
#       stopped, nothing asks the driver to join, and the module is loaded
#       with allow_tx=0, so it transmits nothing. The interface goes up,
#       three passive scans, down, up, one more passive scan, then the
#       module is removed. PASS: every scan lists access points and the
#       kernel log has no warning, oops or bug.
#
# The installed driver comes back through card-test-restore.sh (rmmod
# bcm4360_open, modprobe bcm4360, wpa_supplicant and NetworkManager
# started, the connection activated): at the end, on any failure, and from a
# deadman timer if the test runs past --deadline (default 10 min).
#
# The Wi-Fi link is down while it runs. Start it detached:
#
#   sudo systemd-run --unit=bcm4360-open-test --collect \
#       /bin/bash /path/to/scripts/open-test.sh /path/to/bcm4360_open.ko --owner $USER
#
# Results: /var/log/bcm4360-open-test-DATE/ (log.txt, kernel.txt, scan-*.txt).
set -u

here=$(cd "$(dirname "$0")" && pwd)
ko=${1:?usage: open-test.sh KO [--stage rx] [--owner USER] [--deadline MIN] [--data FILE]}
shift
stage=rx
owner=
deadline=10
data=$here/../lib/wlc_hybrid.o_shipped
while [ $# -gt 0 ]; do
	case $1 in
	--stage) stage=$2; shift 2 ;;
	--owner) owner=$2; shift 2 ;;
	--deadline) deadline=$2; shift 2 ;;
	--data) data=$2; shift 2 ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ "$(id -u)" -eq 0 ] || { echo "must run as root" >&2; exit 2; }
[ -f "$ko" ] || { echo "$ko: no such file" >&2; exit 2; }
[ "$stage" = rx ] || { echo "stage $stage: not written yet" >&2; exit 2; }

out=/var/log/bcm4360-open-test-$(date +%Y%m%d-%H%M%S)
mkdir -p "$out"
log=$out/log.txt
exec > >(tee -a "$log") 2>&1
say() { printf '[%s] %s\n' "$(date +%T)" "$*"; }
FW=/lib/firmware/bcm4360/bcm4360-data.bin
restore_args=(--out "$out" ${owner:+--owner "$owner"})
conn=$(nmcli -t -f NAME,TYPE con show --active 2>/dev/null | awk -F: '$2 == "802-11-wireless" { print $1; exit }')
[ -n "$conn" ] && restore_args+=(--conn "$conn")
failed=0
fail() { say "FAIL: $*"; failed=1; }

on_exit() {
	say "----- restore: the installed driver -----"
	ifname=$(open_iface) && ip link set "$ifname" down 2>/dev/null
	bash "$here/card-test-restore.sh" "${restore_args[@]}"
	journalctl -k --since "@$t_start" --no-pager -o short-monotonic > "$out/kernel.txt" 2>/dev/null
	say "after restore: $(lsmod | awk '$1 == "bcm4360" || $1 == "wl" { print $1 }' | head -n 1)" \
	    "$(cat /sys/module/bcm4360/version 2>/dev/null)"
	say "RESULT: $([ "$failed" -eq 0 ] && echo PASS || echo FAIL) (stage $stage; $out)"
	[ -n "$owner" ] && chown -R "$owner": "$out" 2>/dev/null
	systemctl stop bcm4360-card-deadman.timer 2>/dev/null
}

# the interface of bcm4360_open
open_iface() {
	local d
	for d in /sys/class/net/*; do
		[ "$(basename "$(readlink -f "$d/device/driver" 2>/dev/null)")" = bcm4360_open ] &&
			{ basename "$d"; return 0; }
	done
	return 1
}

# warnings, oopses, bugs in the kernel log since the start
kernel_problems() {
	journalctl -k --since "@$t_start" --no-pager -o cat 2>/dev/null |
		grep -E 'WARNING:|BUG:|Oops|general protection|refcount_t|use-after-free|bcm4360_open.*(error|fail)' |
		grep -v 'arch/x86/kernel/cpu/bugs.c'
}

scan() {
	local n=$1 f=$out/scan-$1.txt c
	if ! timeout 90 iw dev "$ifname" scan passive > "$f" 2>&1; then
		fail "scan $n: $(head -n 1 "$f")"
		return
	fi
	c=$(grep -c '^BSS ' "$f")
	say "scan $n: $c access points$(grep -E '^\s+SSID: .+' "$f" | sed 's/^\s*SSID: //' | sort -u | head -n 5 | tr '\n' ',' | sed 's/,$//; s/^/ (/; s/$/)/')"
	[ "$c" -gt 0 ] || fail "scan $n: nothing received"
}

t_start=$(date +%s)
say "===== open-test: $ko, stage $stage ====="
say "module: $(modinfo -F version "$ko" 2>/dev/null) $(modinfo -F srcversion "$ko" 2>/dev/null)," \
    "connection before: ${conn:-none}"

systemctl stop bcm4360-card-deadman.timer bcm4360-card-deadman.service 2>/dev/null
systemctl reset-failed bcm4360-card-deadman.timer bcm4360-card-deadman.service 2>/dev/null
if ! systemd-run --unit=bcm4360-card-deadman --collect --on-active="${deadline}m" \
	--timer-property=AccuracySec=10s \
	/bin/bash "$here/card-test-restore.sh" "${restore_args[@]}"; then
	say "ABORT: cannot arm the deadman timer"
	exit 1
fi
trap on_exit EXIT
trap 'say "interrupted"; exit 1' TERM INT
say "deadman armed: $deadline min"
systemctl stop bcm4360-watchdog.timer && say "watchdog timer paused"

if [ ! -s "$FW" ]; then
	say "making $FW from $data"
	mkdir -p "$(dirname "$FW")"
	python3 -I "$here/bcm4360-fwcut.py" "$data" "$FW" || { fail "firmware file"; exit 1; }
fi
say "firmware file: $(stat -c %s "$FW") bytes"

say "----- stage rx: receive only -----"
systemctl stop NetworkManager wpa_supplicant && say "NetworkManager and wpa_supplicant stopped"
if lsmod | grep -q '^bcm4360 '; then
	rmmod bcm4360 || { fail "rmmod bcm4360"; exit 1; }
	say "bcm4360 removed"
fi
modprobe mac80211 || { fail "modprobe mac80211"; exit 1; }
echo "open-test: loading bcm4360_open" > /dev/kmsg
if ! insmod "$ko" allow_tx=0; then
	fail "insmod"
	exit 1
fi
t=0
until ifname=$(open_iface); do
	[ "$t" -ge 20 ] && { fail "no interface after 20 s"; exit 1; }
	sleep 1
	t=$((t + 1))
done
say "interface $ifname ($(cat "/sys/class/net/$ifname/address"))"
iw dev "$ifname" info | sed 's/^/    /'
if ! ip link set "$ifname" up; then
	fail "interface up"
	exit 1
fi
sleep 3
for n in 1 2 3; do
	scan "$n"
	sleep 2
done
ip link set "$ifname" down && sleep 2 && ip link set "$ifname" up && say "down and up again" ||
	fail "down/up"
sleep 3
scan 4
grep -E "$(cat "/sys/class/net/$ifname/device/irq" 2>/dev/null || echo none):" /proc/interrupts |
	sed 's/^/    interrupts: /'
ip link set "$ifname" down
rmmod bcm4360_open && say "bcm4360_open removed" || fail "rmmod bcm4360_open"
problems=$(kernel_problems)
if [ -n "$problems" ]; then
	fail "kernel log:"
	printf '%s\n' "$problems" | sed 's/^/    /'
else
	say "kernel log clean"
fi
exit 0
