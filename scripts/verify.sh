#!/usr/bin/env bash
# SPDX-License-Identifier: ISC
#
# Non-disruptive health check of the running Wi-Fi driver: identity, link,
# addressing, reachability, scan (as root), unprivileged queries (as a normal
# user), a throughput sample and the kernel log since the module was loaded.
#
#   scripts/verify.sh [--throughput-mb N]
set -u

mb=25
[ "${1:-}" = "--throughput-mb" ] && mb=${2:-25}

pass=0 fail=0
ok() { printf '  ok    %s\n' "$*"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$*"; fail=$((fail + 1)); }
info() { printf '        %s\n' "$*"; }

iface=
for d in /sys/class/net/*; do [ -e "$d/phy80211" ] && { iface=$(basename "$d"); break; }; done
[ -n "$iface" ] || { echo "no Wi-Fi interface found"; exit 1; }

echo "== driver"
drv=$(basename "$(readlink -f "/sys/class/net/$iface/device/driver")")
pci=$(basename "$(readlink -f "/sys/class/net/$iface/device")")
ids=$(cat "/sys/class/net/$iface/device/vendor" "/sys/class/net/$iface/device/device" | tr '\n' ' ')
info "$iface on $pci [$ids] bound to '$drv'"
[ "$drv" = bcm4360 ] && ok "bcm4360 drives the card" || bad "card is driven by '$drv', not bcm4360"
if [ -r /sys/module/bcm4360/version ]; then info "bcm4360 $(cat /sys/module/bcm4360/version), srcversion $(cat /sys/module/bcm4360/srcversion)"; fi

echo "== link"
link=$(iw dev "$iface" link 2>&1)
if printf '%s\n' "$link" | grep -q '^Connected to'; then
	ok "associated: $(printf '%s\n' "$link" | sed -n 's/^\s*SSID: //p') $(printf '%s\n' "$link" | sed -n 's/^\s*freq: \(.*\)/\1 MHz/p'), $(printf '%s\n' "$link" | sed -n 's/^\s*signal: //p')"
	info "$(printf '%s\n' "$link" | sed -n 's/^\s*\(tx bitrate: .*\)/\1/p')"
else
	bad "not associated"
fi
addr=$(ip -4 -o addr show dev "$iface" | awk '{ print $4; exit }')
[ -n "$addr" ] && ok "IPv4 address $addr" || bad "no IPv4 address"
gw=$(ip -4 route show default dev "$iface" | awk '{ print $3; exit }')
if [ -n "$gw" ] && ping -I "$iface" -c 5 -i 0.2 -W 2 -q "$gw" >/tmp/.bcm4360-ping 2>&1; then
	ok "gateway $gw: $(tail -n 1 /tmp/.bcm4360-ping)"
else
	bad "gateway ${gw:-(none)} unreachable"
fi
if ping -I "$iface" -c 5 -i 0.2 -W 2 -q 1.1.1.1 >/tmp/.bcm4360-ping 2>&1; then
	ok "internet 1.1.1.1: $(tail -n 1 /tmp/.bcm4360-ping)"
else
	bad "internet unreachable"
fi
rm -f /tmp/.bcm4360-ping
getent hosts archlinux.org >/dev/null && ok "DNS resolves" || bad "DNS does not resolve"

echo "== queries"
before=$(journalctl -k -o cat --no-pager 2>/dev/null | grep -c 'ERROR @')
if [ "$(id -u)" -eq 0 ]; then
	n=$(iw dev "$iface" scan 2>/dev/null | grep -c '^BSS ')
	[ "$n" -gt 0 ] && ok "scan found $n networks" || bad "scan returned nothing"
	user=${SUDO_USER:-$(logname 2>/dev/null || echo nobody)}
	q() { runuser -u "$user" -- "$@"; }
else
	info "(not root: skipping active scan)"
	q() { "$@"; }
fi
txp=$(q iw dev "$iface" info 2>&1 | sed -n 's/^\s*txpower \(.*\)/\1/p')
[ -n "$txp" ] && ok "tx power query as unprivileged user: $txp" || bad "tx power query failed"
bssid=$(printf '%s\n' "$link" | awk '/^Connected to/ { print $3 }')
sta=$(q iw dev "$iface" station get "$bssid" 2>&1)
if printf '%s\n' "$sta" | grep -q 'signal'; then
	ok "station get as unprivileged user: signal $(printf '%s\n' "$sta" | sed -n 's/^\s*signal:\s*//p' | head -n1)"
else
	bad "station get failed: $sta"
fi
sta=$(q iw dev "$iface" station dump 2>&1)
if printf '%s\n' "$sta" | grep -q "^Station $bssid"; then
	ok "station dump lists the AP"
else
	bad "station dump does not list the AP"
fi
after=$(journalctl -k -o cat --no-pager 2>/dev/null | grep -c 'ERROR @')
[ "$after" -eq "$before" ] && ok "queries logged no driver errors" || bad "queries logged $((after - before)) driver error(s)"

echo "== throughput"
if command -v curl >/dev/null; then
	r=$(curl -s -o /dev/null --interface "$iface" --max-time 60 -w '%{speed_download} %{size_download}' \
		"https://speed.cloudflare.com/__down?bytes=$((mb * 1000000))") || r=
	if [ -n "$r" ] && [ "${r#* }" -gt 0 ]; then
		ok "download $(awk -v s="${r%% *}" -v n="${r#* }" 'BEGIN { printf "%.1f MB in %.1f Mbit/s", n / 1e6, s * 8 / 1e6 }')"
	else
		bad "download test failed"
	fi
fi

echo "== kernel log since bcm4360 was loaded"
klog=$(journalctl -k -b -o cat --no-pager 2>/dev/null)
since=$(printf '%s\n' "$klog" | grep -n 'bcm4360: BCM4360 driver' | tail -n 1 | cut -d: -f1)
if [ -n "$since" ]; then
	part=$(printf '%s\n' "$klog" | tail -n +"$since")
	probs=$(printf '%s\n' "$part" | grep -E 'WARNING:|BUG:|Oops|general protection|field-spanning|bss_not_found' | grep -v 'arch/x86/kernel/cpu/bugs.c')
	[ -z "$probs" ] && ok "no warnings, oopses or FORTIFY reports" || { bad "kernel problems:"; printf '%s\n' "$probs" | sed 's/^/          /'; }
	errs=$(printf '%s\n' "$part" | grep -c 'bcm4360: ERROR')
	info "$errs driver error line(s) since load"
	printf '%s\n' "$part" | grep -q 'Unpatched return thunk' && info "note: one 'Unpatched return thunk' warning (Broadcom core, once per boot)"
else
	info "(bcm4360 load message not in this boot's log, or no permission to read it)"
fi

echo
echo "verify: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
