#!/usr/bin/env bash
# SPDX-License-Identifier: ISC
#
# The open driver bcm4360_open (docs/re/07-open-driver.md) on the card, and
# the installed driver back afterwards, whatever happens:
#
#   open-test.sh KO [--stage rx|join|all] [--owner USER] [--deadline MIN] [--data FILE]
#                   [--gap SEC]
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
#   join (milestone 3) the open driver connects through NetworkManager
#       (which keeps running), twice: first with nohwcrypt=1 (encryption in
#       mac80211), then with the core's hardware keys. Each round: link,
#       station, 20 pings, throughput both ways, a scan, DNS, a minute's
#       watch, the kernel log. powersave stays 0. The hardware-key round is
#       skipped if the software one did not connect. Connection attempts are
#       --gap seconds apart (default 90: eduroam refuses rapid ones).
#       PASS: both rounds connected with traffic, kernel log clean.
#   all rx, then join if rx passed.
#
# The installed driver comes back through card-test-restore.sh (rmmod
# bcm4360_open, modprobe bcm4360, wpa_supplicant and NetworkManager
# started, the connection activated): at the end, on any failure, and from a
# deadman timer if the test runs past --deadline (default 10 min for rx,
# 25 for join and all).
#
# The Wi-Fi link is down while it runs. Start it detached:
#
#   sudo systemd-run --unit=bcm4360-open-test --collect \
#       /bin/bash /path/to/scripts/open-test.sh /path/to/bcm4360_open.ko --owner $USER
#
# Results: /var/log/bcm4360-open-test-DATE/ (log.txt, kernel.txt, scan-*.txt).
set -u

here=$(cd "$(dirname "$0")" && pwd)
ko=${1:?usage: open-test.sh KO [--stage rx|join|all] [--owner USER] [--deadline MIN] [--data FILE] [--gap SEC]}
shift
stage=rx
owner=
deadline=
gap=90
data=$here/../lib/wlc_hybrid.o_shipped
while [ $# -gt 0 ]; do
	case $1 in
	--stage) stage=$2; shift 2 ;;
	--owner) owner=$2; shift 2 ;;
	--deadline) deadline=$2; shift 2 ;;
	--data) data=$2; shift 2 ;;
	--gap) gap=$2; shift 2 ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ "$(id -u)" -eq 0 ] || { echo "must run as root" >&2; exit 2; }
[ -f "$ko" ] || { echo "$ko: no such file" >&2; exit 2; }
case $stage in
rx) : "${deadline:=10}" ;;
join|all) : "${deadline:=25}" ;;
*) echo "stage $stage: rx, join or all" >&2; exit 2 ;;
esac

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
	# the kernel log first: the restore may find the card held until a reboot
	journalctl -k --since "@$t_start" --no-pager -o short-monotonic > "$out/kernel.txt" 2>/dev/null
	ifname=$(open_iface) && timeout 20 ip link set "$ifname" down 2>/dev/null
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

DL_URL="https://speed.cloudflare.com/__down?bytes=20000000"
UL_URL=https://speed.cloudflare.com/__up

# wait for the interface of bcm4360_open (20 s)
wait_iface() {
	local t=0
	until ifname=$(open_iface); do
		[ "$t" -ge 20 ] && return 1
		sleep 1
		t=$((t + 1))
	done
	say "interface $ifname ($(cat "/sys/class/net/$ifname/address"))"
}

# rmmod bcm4360_open without blocking this script for good
remove_open() {
	local p t=0
	ifname=$(open_iface) && timeout 20 ip link set "$ifname" down 2>/dev/null
	rmmod bcm4360_open &
	p=$!
	while kill -0 "$p" 2>/dev/null; do
		[ "$t" -ge 60 ] && { fail "rmmod bcm4360_open hangs"; return 1; }
		sleep 1
		t=$((t + 1))
	done
	wait "$p" && say "bcm4360_open removed" || { fail "rmmod bcm4360_open"; return 1; }
}

load_open() {
	echo "open-test: loading bcm4360_open $*" > /dev/kmsg
	insmod "$ko" "$@" || { fail "insmod $*"; return 1; }
	wait_iface || { fail "no interface after 20 s"; return 1; }
}

check_kernel() {
	local problems
	problems=$(kernel_problems)
	if [ -n "$problems" ]; then
		fail "kernel log ($1):"
		printf '%s\n' "$problems" | sed 's/^/    /'
		return 1
	fi
	say "kernel log clean ($1)"
}

stage_rx() {
	say "----- stage rx: receive only -----"
	systemctl stop NetworkManager wpa_supplicant && say "NetworkManager and wpa_supplicant stopped"
	if lsmod | grep -q '^bcm4360 '; then
		rmmod bcm4360 || { fail "rmmod bcm4360"; return 1; }
		say "bcm4360 removed"
	fi
	modprobe mac80211 || { fail "modprobe mac80211"; return 1; }
	load_open allow_tx=0 || return 1
	iw dev "$ifname" info | sed 's/^/    /'
	ip link set "$ifname" up || { fail "interface up"; return 1; }
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
	remove_open || return 1
	check_kernel "stage rx"
	systemctl start wpa_supplicant NetworkManager && say "wpa_supplicant and NetworkManager started"
}

# ---- stage join
last_attempt=0
pace() {
	local wait=$((last_attempt + gap - $(date +%s)))
	if [ "$wait" -gt 0 ]; then
		say "pacing: ${wait}s until the next connection attempt"
		sleep "$wait"
	fi
	last_attempt=$(date +%s)
}
nm_state() { nmcli -g GENERAL.STATE dev show "$ifname" 2>/dev/null; }
# wait_connected SECONDS: nudges NetworkManager once after 30 s
wait_connected() {
	local t=0
	while [ "$t" -lt "$1" ]; do
		[ "$(nm_state)" = "100 (connected)" ] && { say "connected after ${t}s"; return 0; }
		if [ "$t" -eq 30 ] && [ -n "$conn" ]; then
			say "no autoconnect yet: nmcli con up '$conn'"
			nmcli --wait 30 con up "$conn" ifname "$ifname" >/dev/null 2>&1 || true
		fi
		sleep 1
		t=$((t + 1))
	done
	say "not connected after $1 s (state: $(nm_state))"
	return 1
}
mbit() { awk -v b="${1:-0}" 'BEGIN { printf "%.1f", b * 8 / 1e6 }'; }

# join_round LABEL PARAMS...: load, connect, measure, watch, remove
join_round() {
	local l=$1 f p loss v down=0 up=0 tmp lost=0 t sc
	shift
	f=$out/join-$l.txt
	say "----- join $l: ${*:-default parameters} -----"
	pace
	load_open "$@" || return 1
	if ! wait_connected 150; then
		fail "join $l: not connected"
		journalctl -u wpa_supplicant -u NetworkManager --since "@$last_attempt" --no-pager \
			-o short-monotonic > "$out/join-$l-nm.txt" 2>/dev/null
		remove_open
		return 1
	fi
	{
		date
		iw dev "$ifname" link
		iw dev "$ifname" station dump
		ip -4 addr show dev "$ifname"
	} > "$f" 2>&1
	say "link: $(iw dev "$ifname" link | awk '/freq:|signal:|tx bitrate:|rx bitrate:/' | sed 's/^\s*//' | tr '\n' ';')"
	p=$(ping -I "$ifname" -c 20 -i 0.3 -W 2 -q 1.1.1.1 2>&1)
	printf '%s\n' "$p" >> "$f"
	loss=$(printf '%s\n' "$p" | sed -n 's/.* \([0-9.]*%\) packet loss.*/\1/p')
	v=$(curl -s -o /dev/null --interface "$ifname" --max-time 40 -w '%{http_code} %{speed_download}' \
		"$DL_URL" 2>/dev/null)
	case $v in 2*) down=${v#* } ;; esac
	tmp=$(mktemp /dev/shm/bcm4360-up.XXXXXX)
	head -c 10000000 /dev/urandom > "$tmp"
	v=$(curl -s -o /dev/null --interface "$ifname" --max-time 40 -X POST --data-binary @"$tmp" \
		-w '%{http_code} %{speed_upload}' "$UL_URL" 2>/dev/null)
	rm -f "$tmp"
	case $v in 2*) up=${v#* } ;; esac
	sc=$(timeout 30 iw dev "$ifname" scan 2>&1 | tee "$out/scan-join-$l.txt" | grep -c '^BSS ')
	say "MEASURE $l: ping loss ${loss:-?}, down/up $(mbit "$down")/$(mbit "$up") Mbit/s, scan $sc BSS"
	getent hosts archlinux.org >/dev/null || fail "join $l: DNS"
	[ "${loss:-100%}" != "100%" ] || fail "join $l: no ping answered"
	[ "$down" != 0 ] || fail "join $l: no download"
	for t in $(seq 60); do
		ping -I "$ifname" -c 1 -W 1 -q 1.1.1.1 >/dev/null 2>&1 || lost=$((lost + 1))
		sleep 1
	done
	say "WATCH $l: 60 s, $lost pings lost, state $(nm_state)"
	iw dev "$ifname" station dump >> "$f" 2>&1
	remove_open
	check_kernel "join $l"
}

stage_join() {
	local ok=0
	say "----- stage join -----"
	systemctl is-active -q wpa_supplicant || systemctl start wpa_supplicant
	systemctl is-active -q NetworkManager || systemctl start NetworkManager
	if lsmod | grep -q '^bcm4360 '; then
		rmmod bcm4360 || { fail "rmmod bcm4360"; return 1; }
		say "bcm4360 removed"
	fi
	modprobe mac80211 || { fail "modprobe mac80211"; return 1; }
	join_round sw nohwcrypt=1 && ok=1
	if [ "$ok" -eq 1 ] && lsmod | grep -q '^bcm4360_open ' ; then
		fail "bcm4360_open still loaded: hardware-key round skipped"
	elif [ "$ok" -eq 1 ]; then
		join_round hw
	else
		say "hardware-key round skipped: the software-crypto round failed"
	fi
}

case $stage in
rx) stage_rx ;;
join) stage_join ;;
all) stage_rx && [ "$failed" -eq 0 ] && stage_join ;;
esac
exit 0
