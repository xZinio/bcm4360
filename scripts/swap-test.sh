#!/usr/bin/env bash
# SPDX-License-Identifier: ISC
#
# Swap the running Broadcom Wi-Fi driver (wl or bcm4360) for another one,
# check the link end to end and roll back automatically if anything fails.
#
#   swap-test.sh NEW [--settle SECONDS] [--reconnects N] [--log FILE] [--fallback MOD]
#
# NEW is a module name (bcm4360, wl) or the path of a .ko file; a .ko with the
# same name as the running module replaces it (new build). On failure the
# previous module is reloaded, or --fallback (default: wl) when the previous
# one cannot be reloaded by name. Needs root.
# The Wi-Fi link goes down while it runs, so start it detached, e.g.
#
#   systemd-run --unit=bcm4360-swap --collect \
#       systemd-inhibit --what=sleep:idle --why="Wi-Fi driver test" \
#       /path/to/scripts/swap-test.sh bcm4360
#
# so that it survives an SSH session carried over this very link.
set -u

new=${1:?usage: swap-test.sh NEW [--settle SECONDS] [--reconnects N] [--log FILE]}
shift
settle=60
reconnects=0
log=/var/log/bcm4360-swap-test.log
fallback=wl
while [ $# -gt 0 ]; do
	case $1 in
	--settle) settle=$2; shift 2 ;;
	--reconnects) reconnects=$2; shift 2 ;;
	--log) log=$2; shift 2 ;;
	--fallback) fallback=$2; shift 2 ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ "$(id -u)" -eq 0 ] || { echo "swap-test.sh must run as root" >&2; exit 2; }

exec >>"$log" 2>&1
say() { printf '[%s] %s\n' "$(date +%T)" "$*"; }

module_name() {
	case $1 in
	*.ko|*.ko.*) modinfo -F name "$1" 2>/dev/null || basename "${1%%.ko*}" ;;
	*) printf '%s\n' "$1" ;;
	esac
}
load() {
	case $1 in
	*.ko|*.ko.*) insmod "$1" ;;
	*) modprobe "$1" ;;
	esac
}
running_broadcom() { lsmod | awk '$1 == "wl" || $1 == "bcm4360" { print $1; exit }'; }
wifi_iface() {
	local d
	for d in /sys/class/net/*; do
		[ -e "$d/phy80211" ] && { basename "$d"; return 0; }
	done
	return 1
}

# Wait until NetworkManager reports the Wi-Fi device connected; sets $iface.
wait_connected() {
	local limit=$1 t=0 dev kicked=0
	while [ "$t" -lt "$limit" ]; do
		dev=$(wifi_iface) || dev=
		if [ -n "$dev" ]; then
			if [ "$(nmcli -g GENERAL.STATE dev show "$dev" 2>/dev/null)" = "100 (connected)" ]; then
				iface=$dev
				say "connected on $iface after ${t}s"
				return 0
			fi
			# NM normally autoconnects right away; nudge it once if not.
			if [ "$kicked" -eq 0 ] && [ "$t" -ge 25 ] && [ -n "$conn" ]; then
				say "no autoconnect yet, running: nmcli con up '$conn' ifname $dev"
				nmcli --wait 30 con up "$conn" ifname "$dev" || true
				kicked=1
			fi
		fi
		sleep 1
		t=$((t + 1))
	done
	say "not connected after ${limit}s"
	return 1
}

check_link() {
	local ok=0 gw
	say "link: $(iw dev "$iface" link 2>&1 | tr -s '\n\t' '  ')"
	iw dev "$iface" link | grep -q '^Connected to' || { say "FAIL: not associated"; ok=1; }
	ip -4 -o addr show dev "$iface" | grep -q ' inet ' || { say "FAIL: no IPv4 address"; ok=1; }
	gw=$(ip -4 route show default dev "$iface" | awk '{ print $3; exit }')
	if [ -n "$gw" ] && ping -I "$iface" -c 3 -W 2 -q "$gw" >/dev/null; then
		say "gateway $gw answers"
	else
		say "FAIL: gateway ${gw:-(none)} does not answer"; ok=1
	fi
	if ping -I "$iface" -c 3 -W 2 -q 1.1.1.1 >/dev/null; then
		say "internet (1.1.1.1) answers"
	else
		say "FAIL: internet unreachable"; ok=1
	fi
	if getent hosts archlinux.org >/dev/null; then say "DNS works"; else say "FAIL: DNS"; ok=1; fi
	return $ok
}

# Kernel problems logged since the swap began. The return-thunk warning is
# raised once per boot by any module linking the 2015 Broadcom core (same
# for the stock wl module), so it is reported but not counted as a failure.
check_klog() {
	local all bad
	all=$(journalctl -k --since "@$t0" --no-pager -o short-monotonic 2>/dev/null)
	bad=$(printf '%s\n' "$all" | grep -E 'WARNING:|BUG:|Oops|general protection|field-spanning|bss_not_found|refcount_t|use-after-free' \
		| grep -v 'arch/x86/kernel/cpu/bugs.c')
	printf '%s\n' "$all" | grep -q 'Unpatched return thunk' &&
		say "note: 'Unpatched return thunk' warning (inherited from the Broadcom core, once per boot)"
	printf '%s\n' "$all" | grep -E 'ERROR @|error \(' | sed 's/^/    klog: /' | tail -n 20
	if [ -n "$bad" ]; then
		say "FAIL: kernel reported problems:"
		printf '%s\n' "$bad" | sed 's/^/    klog: /'
		return 1
	fi
	say "kernel log clean"
	return 0
}

swapped=0
done_ok=0
rollback() {
	[ "$swapped" -eq 1 ] || return 0
	swapped=0
	say "ROLLBACK: removing $new_mod, loading $restore"
	rmmod "$new_mod" 2>/dev/null
	modprobe "$restore" || say "rollback: loading $restore failed"
	if wait_connected 90; then say "rollback: reconnected with $restore"; else say "rollback: NOT reconnected"; fi
}
trap 'say "interrupted"; rollback; say "RESULT: FAIL (interrupted, rolled back to $restore)"; exit 1' TERM INT

say "===== swap-test: $new ====="
old=$(running_broadcom)
[ -n "$old" ] || { say "no Broadcom driver loaded"; exit 1; }
old_ko=$(modinfo -F filename "$old" 2>/dev/null) || old_ko=
new_mod=$(module_name "$new")
iface=$(wifi_iface) || iface=
conn=$(nmcli -g GENERAL.CONNECTION dev show "$iface" 2>/dev/null)
say "current driver: $old (${old_ko:-loaded from a file}), interface: ${iface:-none}, connection: ${conn:-none}"

in_place=0
case $new in *.ko|*.ko.*) ;; *) [ "$old" = "$new_mod" ] && in_place=1 ;; esac
# Go back to the old module if modprobe can find it and it is not the one
# being replaced, else to the fallback.
restore=$old
if [ "$old" = "$new_mod" ] || [ -z "$old_ko" ]; then restore=$fallback; fi
say "rollback target: $restore"

if [ "$in_place" -eq 1 ]; then
	say "$new_mod is already running; testing it in place"
else
	# Give the SSH command that started us time to return before the link drops.
	sleep 5
	t0=$(date +%s)
	say "unloading $old"
	rmmod "$old" || { say "cannot unload $old"; say "RESULT: FAIL (nothing changed)"; exit 1; }
	swapped=1
	say "loading $new"
	if ! load "$new"; then
		say "FAIL: loading $new failed"
		rollback
		say "RESULT: FAIL (rolled back to $restore)"
		exit 1
	fi
fi
t0=${t0:-$(date +%s)}

result=0
if wait_connected "$settle"; then
	say "driver: $(ethtool -i "$iface" 2>/dev/null | head -n 2 | tr '\n' ' ')"
	check_link || result=1
	i=1
	while [ "$result" -eq 0 ] && [ "$i" -le "$reconnects" ]; do
		say "--- reconnect cycle $i/$reconnects ---"
		nmcli dev disconnect "$iface" >/dev/null 2>&1
		sleep 3
		nmcli --wait 40 dev connect "$iface" >/dev/null 2>&1 || true
		if wait_connected 60; then check_link || result=1; else result=1; fi
		i=$((i + 1))
	done
	check_klog || result=1
else
	result=1
	check_klog
fi

if [ "$result" -eq 0 ]; then
	swapped=0
	say "RESULT: PASS ($new_mod active on $iface)"
	exit 0
fi
rollback
say "RESULT: FAIL (rolled back to $restore)"
exit 1
