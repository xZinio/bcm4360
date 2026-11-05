#!/usr/bin/env bash
# SPDX-License-Identifier: ISC
#
# The full test of a hybrid bcm4360 build on the card, from Broadcom's code
# everywhere to the open PHY, with measurements at every stage and the
# installed driver restored at the end, whatever happens:
#
#   card-test.sh KO [--owner USER] [--soak MIN] [--base-soak MIN] [--deadline MIN]
#                   [--no-capture] [--no-24] [--watch24 SEC] [--suspend] [--hotspot NAME]
#                   [--gap SEC]
#                   [--extra PARAMS] [--no-off] [--confirm PARAMS]
#                   [--captures "NAME:PARAMS;NAME:PARAMS"] [--def-repeat N]
#                   [--quick "NAME:PARAMS;NAME:PARAMS"] [--cycles N] [--no-measure]
#                   [--capture-radio "NAME:PARAMS;NAME:PARAMS"]
#                   [--ab24 "NAME:PARAMS;NAME:PARAMS"]
#
# --watch24 sets how long the 2.4 GHz stage of def and phy watches the link
# (default 120 s); with both on the same BSSID it compares the two PHYs on
# one AP, the MAC counters (diag) are recorded at its end.
# --extra adds module parameters to the stages def and phy (e.g. "open_pmu=0"
# to test the open PHY while the PMU layer is under suspicion); --no-off
# skips the stage off; --confirm runs one more swap with PARAMS before def
# (e.g. to confirm a failure the bisection found); --captures replaces the
# default recordings ("broadcom:<every switch off>;openphy:open_phy=1");
# --def-repeat loads the defaults N times in all (intermittent failures).
# --quick replaces all stages by a short matrix: for each configuration, a
# load, two radio switches (rfkill, as NetworkManager's "radio off/on"), then
# a measurement (throughput both ways, then a scan) and a minute's watch; a
# failure there is recorded and the next configuration's load goes on.
# --cycles sets the number of radio switches per configuration (default 2),
# --no-measure leaves out the measurement after them.
# --capture-radio replaces all stages too: for each configuration an
# mmiotrace from before the load (mmiotrace only sees mappings made while it
# runs) through the connection, one radio switch (off 5 s, on) and the
# reconnection attempt, into capture-radio-NAME/ with marks at each step;
# the core's state once connected (country, chains, counters) goes into
# diag-capture-radio-NAME.txt.
# --ab24 replaces all stages too: for each configuration a load (swap-test),
# a measurement, then the 2.4 GHz watch of --watch24 seconds with the diag at
# its start and end, all configurations on the AP the first one joined (its
# BSSID is kept), to compare them on one AP (PARAMS ALLOFF: every switch off).
# The 2.4 GHz stage takes hardware snapshots (scripts/phy-snap.py: PHY and
# radio registers, PHY tables, shared memory) after the join and twice at the
# end of the watch, into snap-LABEL-2g*.json, when the module has them.
# Every swap also reads the card's identity and PMU registers
# (peek-on-target.py) 20 s after loading, into peek-LABEL.txt.
#
# KO is a freshly built bcm4360.ko (the installed bcm4360 stays installed:
# it is what every rollback and the final restore load). Stages:
#
#   base     the installed driver as it runs now: link, ping, throughput both
#            ways, a scan (no reconnect)
#   peek     tools/re/peek-on-target.py: the identity of the card (read only)
#   off      KO with every switch off (Broadcom's code everywhere): swap-test
#            + measurements
#   def      KO with the open layers and Broadcom's PHY (open_phy=0): swap-test,
#            measurements, a join to a 2.4 GHz AP, a short soak
#   phy      KO with the open PHY (open_phy=1): the same, then reconnects,
#            interface down/up, radio off/on, (--suspend) suspend to RAM, and
#            the long soak (default 20 min: past the PHY's periodic
#            calibration at 900 s)
#   capture  mmiotrace recordings of the driver loading with Broadcom's code
#            and with the open PHY (tools/re/mmiotrace.py replay)
#   restore  card-test-restore.sh: the installed driver, connected
#
# Each connection attempt waits until --gap seconds (default 90) have passed
# since the previous one: eduroam refuses EAP after rapid re-activations.
# A guard checks the connection every 10 s: without one for 2 minutes (6
# minutes inside a step that disconnects on purpose: a swap, a reconnect, a
# capture), it runs card-test-restore.sh in a unit of its own, which stops
# this test and brings back the installed driver. A deadman timer
# (--deadline, default 110 min) does the same if this script hangs. Needs root; start it detached so that it survives the
# Wi-Fi link going down:
#
#   systemd-run --unit=bcm4360-card-test --collect \
#       systemd-inhibit --what=sleep:idle --why="Wi-Fi driver test" \
#       /path/to/scripts/card-test.sh /path/to/bcm4360.ko --owner USER
#
# Results: /var/log/bcm4360-card-test-DATE/summary.log (one line per check,
# RESULT lines), the measurements, scans, swap-test log and captures next to
# it. The captures and scans contain the card's MAC address and the networks
# in range: keep them local.
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
ko=${1:?usage: card-test.sh KO [options]}
shift
owner=
soak=20
base_soak=8
deadline=110
capture=1
band24=1
watch24=120
suspend=0
hotspot=
gap=90
extra=
def_repeat=1
quick=
capture_radio=
ab24=
fixed_bssid=
last24_bssid=
cycles=2
measure_after=1
skip_off=0
confirm=
captures=
while [ $# -gt 0 ]; do
	case $1 in
	--owner) owner=$2; shift 2 ;;
	--soak) soak=$2; shift 2 ;;
	--base-soak) base_soak=$2; shift 2 ;;
	--deadline) deadline=$2; shift 2 ;;
	--no-capture) capture=0; shift ;;
	--no-24) band24=0; shift ;;
	--watch24) watch24=$2; shift 2 ;;
	--suspend) suspend=1; shift ;;
	--hotspot) hotspot=$2; shift 2 ;;
	--gap) gap=$2; shift 2 ;;
	--extra) extra=$2; shift 2 ;;
	--no-off) skip_off=1; shift ;;
	--confirm) confirm=$2; shift 2 ;;
	--captures) captures=$2; shift 2 ;;
	--def-repeat) def_repeat=$2; shift 2 ;;
	--quick) quick=$2; shift 2 ;;
	--capture-radio) capture_radio=$2; shift 2 ;;
	--ab24) ab24=$2; shift 2 ;;
	--cycles) cycles=$2; shift 2 ;;
	--no-measure) measure_after=0; shift ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ "$(id -u)" -eq 0 ] || { echo "card-test.sh must run as root" >&2; exit 2; }

out=/var/log/bcm4360-card-test-$(date +%Y%m%d-%H%M%S)
mkdir -p "$out"
sum=$out/summary.log
swaplog=$out/swap-test.log
exec >>"$out/harness.log" 2>&1
say() { printf '[%s] %s\n' "$(date +%T)" "$*" | tee -a "$sum"; }
note() { printf '[%s] %s\n' "$(date +%T)" "$*"; }

TEST_PROFILE=bcm4360-test-24
DIRTY=/run/bcm4360-card-test.dirty
# set while a step disconnects on purpose (swap, reconnect, capture, 2.4 GHz)
BUSY=/run/bcm4360-card-test.busy
TR=/sys/kernel/tracing
[ -d "$TR" ] || TR=/sys/kernel/debug/tracing
DL_URLS="https://speed.cloudflare.com/__down?bytes=40000000 https://mirror.one.com/archlinux/iso/latest/archlinux-x86_64.iso"
UL_URL=https://speed.cloudflare.com/__up
LAYERS="pmu access mac utils clm ppr srom si dma chan iovar nvram rate cca stf stf_policy phy"
ALL_OFF=
for l in $LAYERS; do ALL_OFF="$ALL_OFF open_$l=0"; done
ALL_OFF=${ALL_OFF# }

restore_args=(--out "$out" ${owner:+--owner "$owner"} ${hotspot:+--hotspot "$hotspot"})

# ---- helpers -------------------------------------------------------------------

wifi_iface() {
	local d
	for d in /sys/class/net/*; do
		[ -e "$d/phy80211" ] && { basename "$d"; return 0; }
	done
	return 1
}
connected() {
	iface=$(wifi_iface) || return 1
	[ "$(nmcli -g GENERAL.STATE dev show "$iface" 2>/dev/null)" = "100 (connected)" ]
}
# wait_connected SECONDS [CONNECTION]: nudges NetworkManager once after 30 s
wait_connected() {
	local t=0 kicked=0
	while [ "$t" -lt "$1" ]; do
		if connected; then
			note "connected on $iface after ${t}s"
			return 0
		fi
		if [ "$kicked" -eq 0 ] && [ "$t" -ge 30 ] && [ -n "${2:-}" ] && [ -n "${iface:-}" ]; then
			note "no autoconnect yet: nmcli con up '$2'"
			nmcli --wait 30 con up "$2" ifname "$iface" >/dev/null 2>&1 || true
			kicked=1
		fi
		sleep 1
		t=$((t + 1))
	done
	note "not connected after $1 s"
	return 1
}
# pace: let --gap seconds pass since the last connection attempt
last_attempt=0
pace() {
	local now wait
	now=$(date +%s)
	wait=$((last_attempt + gap - now))
	if [ "$wait" -gt 0 ]; then
		note "pacing: ${wait}s until the next connection attempt"
		sleep "$wait"
	fi
	last_attempt=$(date +%s)
}
attempted() { last_attempt=$(date +%s); }
busy() { touch "$BUSY"; }
idle() { rm -f "$BUSY"; }

# The guard (background): restore after 120 s without a connection (360 s
# while a step is busy). The restore runs as a unit of its own because it
# stops this test's unit, the guard's own included.
guard() {
	local down=0 limit
	while sleep 10; do
		if connected; then down=0; continue; fi
		down=$((down + 10))
		limit=120
		[ -e "$BUSY" ] && limit=360
		[ "$down" -lt "$limit" ] && continue
		say "GUARD: no connection for ${down}s: restoring the installed driver"
		systemd-run --unit=bcm4360-card-guard --collect \
			/bin/bash "$here/card-test-restore.sh" "${restore_args[@]}" ${conn:+--conn "$conn"}
		return
	done
}

running_version() {
	printf '%s %s' "$(lsmod | awk '$1 == "wl" || $1 == "bcm4360" { print $1; exit }')" \
		"$(cat /sys/module/bcm4360/version 2>/dev/null)"
}
switches() {
	local p line=
	for p in /sys/module/bcm4360/parameters/open_*; do
		[ -r "$p" ] && line="$line ${p##*/}=$(cat "$p")"
	done
	printf '%s' "${line# }"
}

# kernel problems since the epoch $1 (as swap-test.sh counts them)
klog_check() {
	local all bad
	all=$(journalctl -k --since "@$1" --no-pager -o short-monotonic 2>/dev/null)
	printf '%s\n' "$all" > "$out/klog-$2.txt"
	bad=$(printf '%s\n' "$all" | grep -E 'WARNING:|BUG:|Oops|general protection|field-spanning|bss_not_found|refcount_t|use-after-free' \
		| grep -v 'arch/x86/kernel/cpu/bugs.c')
	if [ -n "$bad" ]; then
		say "FAIL $2: kernel log:"
		printf '%s\n' "$bad" | head -n 20 | sed 's/^/    /' | tee -a "$sum"
		return 1
	fi
	say "ok   $2: kernel log clean ($(printf '%s\n' "$all" | grep -c 'bcm4360\|wl[0-9]:\|ERROR @') driver lines)"
	return 0
}

# the current frequency in MHz (integer), 0 when not associated
cur_freq() {
	iw dev "${iface:-wlan0}" link 2>/dev/null | awk '/freq:/ { f = int($2) } END { print f + 0 }'
}

# diag LABEL: the core's state and counters (scripts/wl-iovar.py) into diag-LABEL.txt
diag() {
	local dev
	dev=$(wifi_iface) || return 0
	{ date; timeout 20 python3 -I "$here/wl-iovar.py" --dev "$dev" diag; } \
		> "$out/diag-$1.txt" 2>&1
}

# snap LABEL: the hardware snapshot of the PHY, radio, PHY tables and shared
# memory (scripts/phy-snap.py, hybrid/snap.c) into snap-LABEL.json; nothing
# when the loaded module has none (the installed 1.0.3)
snap() {
	compgen -G '/proc/bcm4360_snap*' >/dev/null || return 0
	timeout 30 python3 -I "$here/phy-snap.py" take --label "$1" -o "$out/snap-$1.json" \
		>> "$out/snap.log" 2>&1 || note "snapshot $1 failed (snap.log)"
}

mbit() { awk -v b="${1:-0}" 'BEGIN { printf "%.1f", b * 8 / 1e6 }'; }

# throughput down and up over the Wi-Fi interface, Mbit/s
throughput() {
	local u v down=0 up=0 f
	for u in $DL_URLS; do
		v=$(curl -s -o /dev/null --interface "$iface" -r 0-39999999 --max-time 40 \
			-w '%{http_code} %{speed_download}' "$u" 2>/dev/null)
		case $v in 2*) down=${v#* }; break ;; esac
	done
	f=$(mktemp /dev/shm/bcm4360-up.XXXXXX)
	head -c 15000000 /dev/urandom > "$f"
	v=$(curl -s -o /dev/null --interface "$iface" --max-time 40 -X POST --data-binary @"$f" \
		-w '%{http_code} %{speed_upload}' "$UL_URL" 2>/dev/null)
	rm -f "$f"
	case $v in 2*) up=${v#* } ;; esac
	printf '%s %s' "$(mbit "$down")" "$(mbit "$up")"
}

# measure LABEL: link, station, ping, throughput, scan
measure() {
	local l=$1 f=$out/m-$1.txt freq sig txr rxr p loss rtt tp n n2 n5 s=$out/scan-$1.txt
	if ! connected; then say "FAIL $l: not connected"; return 1; fi
	{
		date
		echo "--- driver: $(running_version), switches: $(switches)"
		iw dev "$iface" link
		iw dev "$iface" info
		iw dev "$iface" station dump
		ip -4 addr show dev "$iface"
	} > "$f" 2>&1
	freq=$(iw dev "$iface" link | awk '/freq:/ { print $2; exit }')
	sig=$(iw dev "$iface" link | awk '/signal:/ { print $2; exit }')
	txr=$(iw dev "$iface" link | sed -n 's/.*tx bitrate: \([0-9.]*\) MBit\/s\(.*\)/\1\2/p' | head -n 1)
	rxr=$(iw dev "$iface" link | sed -n 's/.*rx bitrate: \([0-9.]*\) MBit\/s.*/\1/p' | head -n 1)
	p=$(ping -I "$iface" -c 20 -i 0.3 -W 2 -q 1.1.1.1 2>&1)
	printf '%s\n' "$p" >> "$f"
	loss=$(printf '%s\n' "$p" | sed -n 's/.* \([0-9.]*%\) packet loss.*/\1/p')
	rtt=$(printf '%s\n' "$p" | awk -F/ '/^rtt/ { print $5 }')
	tp=$(throughput)
	timeout 30 iw dev "$iface" scan > "$s" 2>&1 || timeout 30 iw dev "$iface" scan > "$s" 2>&1
	n=$(grep -c '^BSS ' "$s")
	n2=$(awk '/^BSS /{b=1} b && /freq:/ { if ($2 < 3000) c++; b=0 } END { print c + 0 }' "$s")
	n5=$((n - n2))
	say "MEASURE $l: freq=${freq:-?} signal=${sig:-?} tx=[${txr:-?}] rx=${rxr:-?} ping_loss=${loss:-?} rtt=${rtt:-?}ms down/up=${tp% *}/${tp#* } Mbit/s scan=$n BSS (2.4 GHz $n2, 5 GHz $n5)"
	getent hosts archlinux.org >/dev/null || say "FAIL $l: DNS"
	[ "$n" -gt 0 ] || say "FAIL $l: the scan found nothing"
	diag "$l"
	return 0
}

# watch SECONDS LABEL: ping 1.1.1.1 every second; outages, band changes
watch_link() {
	local secs=$1 l=$2 t=0 lost=0 run=0 longest=0 freqs=
	while [ "$t" -lt "$secs" ]; do
		if ping -I "${iface:-wlan0}" -c 1 -W 1 -q 1.1.1.1 >/dev/null 2>&1; then
			run=0
		else
			lost=$((lost + 1)); run=$((run + 1))
			[ "$run" -gt "$longest" ] && longest=$run
		fi
		if [ $((t % 15)) -eq 0 ]; then
			f=$(iw dev "${iface:-wlan0}" link 2>/dev/null | awk '/freq:/ { print $2 }')
			case " $freqs " in *" ${f:-none} "*) ;; *) freqs="$freqs ${f:-none}" ;; esac
		fi
		sleep 1
		t=$((t + 1))
	done
	say "WATCH $l: ${secs}s, $lost pings lost, longest outage ${longest}s, frequencies:$freqs"
}

# soak MINUTES LABEL: every 30 s link + 3 pings; every 5 min throughput.
# Stops after 2 minutes without a connection.
soak() {
	local mins=$1 l=$2 end i=0 samples=0 down=0 run_down=0 lost=0 sent=0 tps= p r f sig txr
	end=$(($(date +%s) + mins * 60))
	note "soak $l: $mins min"
	while [ "$(date +%s)" -lt "$end" ]; do
		samples=$((samples + 1))
		if connected; then
			run_down=0
			p=$(ping -I "$iface" -c 3 -W 2 -q 1.1.1.1 2>&1)
			r=$(printf '%s\n' "$p" | sed -n 's/.* \([0-9]*\) received.*/\1/p')
			sent=$((sent + 3)); lost=$((lost + 3 - ${r:-0}))
			f=$(iw dev "$iface" link | awk '/freq:/ { print $2 }')
			sig=$(iw dev "$iface" link | awk '/signal:/ { print $2 }')
			txr=$(iw dev "$iface" link | sed -n 's/.*tx bitrate: \([0-9.]*\) MBit.*/\1/p')
			note "soak $l: freq=$f signal=$sig tx=$txr received ${r:-0}/3"
			if [ $((i % 10)) -eq 9 ]; then
				tps="$tps $(throughput | tr ' ' '/')"
				diag "$l-soak-$i"
			fi
		else
			down=$((down + 1))
			run_down=$((run_down + 1))
			note "soak $l: NOT connected"
			[ "$run_down" -eq 1 ] && diag "$l-soak-down-$i"
			if [ "$run_down" -ge 4 ]; then
				say "SOAK $l: stopped after $samples samples: no connection for 2 minutes"
				return 1
			fi
		fi
		i=$((i + 1))
		sleep 30
	done
	say "SOAK $l: $mins min, $samples samples, $down without connection, pings lost $lost/$sent, down/up Mbit/s:${tps:- -}"
	[ "$down" -le $((samples / 20)) ]
}

# swap PARAMS LABEL: swap-test.sh with KO and these parameters
swap() {
	local params=$1 l=$2 t0 r peekpid
	pace
	busy
	touch "$DIRTY"
	t0=$(date +%s)
	note "swap $l: insmod $tko $params"
	# swap-test.sh unloads after 5 s: this reads the card 20 s after the load
	(sleep 25; cd "$out" && timeout 60 python3 -I "$root/tools/re/peek-on-target.py" \
		> "$out/peek-$l.txt" 2>/dev/null) &
	peekpid=$!
	BCM4360_DIAG="$out/diag-$l-swapfail.txt" \
	bash "$here/swap-test.sh" "$tko" --fallback bcm4360 --settle 120 --log "$swaplog" \
		${params:+--params "$params"}
	r=$?
	# only the peek: a plain `wait` would also wait for the guard, forever
	wait "$peekpid"
	attempted
	if [ "$r" -eq 0 ]; then
		say "PASS $l: $(running_version) [$(switches)] connected, link, gateway, DNS, kernel log (swap-test)"
	else
		say "FAIL $l: swap-test failed and rolled back to $(running_version) (see swap-test.log)"
		klog_check "$t0" "$l-swap"
	fi
	idle
	return $r
}

# band24 LABEL: join the strongest 2.4 GHz AP of the current network
# (a copy of its profile locked to that BSSID), measure, watch, go back
band24() {
	local l=$1 ssid src bssid sig t0 tmp
	[ "$band24" -eq 1 ] || return 0
	ssid=$(iw dev "$iface" link | sed -n 's/^\s*SSID: //p')
	src=$(grep -l "^uuid=$(nmcli -g connection.uuid con show "$conn" 2>/dev/null)\$" \
		/etc/NetworkManager/system-connections/*.nmconnection 2>/dev/null | head -n 1)
	[ -n "$ssid" ] && [ -r "$src" ] || { say "skip $l-2g: no profile file for '$conn'"; return 0; }
	read -r bssid sig < <(awk -v want="$ssid" '
		BEGIN { best = -999 }
		/^BSS / { b = substr($2, 1, 17); f = 0; s = -999 }
		/freq:/ { f = $2 }
		/signal:/ { s = $2 }
		/^\s*SSID: / { sub(/^\s*SSID: /, ""); if ($0 == want && f > 0 && f < 3000 && s > best) { best = s; bb = b } }
		END { if (bb != "") print bb, best }' "$out/scan-$l.txt")
	if [ -n "$fixed_bssid" ]; then
		# --ab24: the AP of the first configuration (signal from this scan, if seen)
		bssid=$fixed_bssid
		sig=$(awk -v b="$bssid" '/^BSS / { m = substr($2, 1, 17) == b } m && /signal:/ { print $2; exit }' \
			"$out/scan-$l.txt")
		sig=${sig:-?}
	fi
	[ -n "${bssid:-}" ] || { say "skip $l-2g: no 2.4 GHz AP of '$ssid' in the scan"; return 0; }
	tmp=/etc/NetworkManager/system-connections/$TEST_PROFILE.nmconnection
	python3 -I - "$src" "$tmp" "$TEST_PROFILE" "$bssid" <<-'EOF' || { say "skip $l-2g: profile copy failed"; return 0; }
		import configparser, sys, uuid
		src, dst, name, bssid = sys.argv[1:]
		c = configparser.RawConfigParser(strict=False, interpolation=None)
		c.optionxform = str
		c.read(src)
		c['connection']['id'] = name
		c['connection']['uuid'] = str(uuid.uuid4())
		c['connection']['autoconnect'] = 'false'
		c['wifi']['bssid'] = bssid
		with open(dst, 'w') as f:
		    c.write(f, space_around_delimiters=False)
	EOF
	chmod 600 "$tmp"
	nmcli con load "$tmp"
	pace
	busy
	t0=$(date +%s)
	note "$l-2g: joining $bssid ($sig dBm)"
	nmcli --wait 60 con up "$TEST_PROFILE" ifname "$iface" >/dev/null 2>&1
	attempted
	if wait_connected 60 && [ "$(cur_freq)" -gt 0 ] && [ "$(cur_freq)" -lt 3000 ]; then
		say "PASS $l-2g: joined $bssid ($sig dBm in the scan) on 2.4 GHz"
		last24_bssid=$bssid
		measure "$l-2g"
		snap "$l-2g"
		watch_link "$watch24" "$l-2g"
		diag "$l-2g-end"
		# twice, moments apart: what moves by itself (phy-snap.py diff --same)
		snap "$l-2g-end"
		sleep 2
		snap "$l-2g-end2"
	else
		say "FAIL $l-2g: no 2.4 GHz link to $bssid (frequency now $(cur_freq) MHz)"
		diag "$l-2g-fail"
	fi
	pace
	nmcli --wait 60 con up "$conn" ifname "$iface" >/dev/null 2>&1
	attempted
	wait_connected 90 "$conn" || say "FAIL $l-2g: not back on '$conn'"
	nmcli con delete "$TEST_PROFILE" >/dev/null 2>&1
	idle
	klog_check "$t0" "$l-2g"
}

# cycle LABEL CMD...: break the connection with CMD, then expect it back
cycle() {
	local l=$1 t0
	shift
	pace
	busy
	t0=$(date +%s)
	note "$l: $*"
	"$@"
	attempted
	if wait_connected 90 "$conn"; then
		say "PASS $l: connected again after $(($(date +%s) - t0))s"
		idle
	else
		say "FAIL $l: not connected again"
		diag "$l-fail"
		idle
		# a link that does not come back by itself is a failure of the stage:
		# do not test on top of it
		return 1
	fi
}
disconnect_connect() { nmcli dev disconnect "$iface"; sleep 3; nmcli --wait 60 dev connect "$iface"; }
link_down_up() { ip link set "$iface" down; sleep 3; ip link set "$iface" up; }
radio_off_on() { nmcli radio wifi off; sleep 5; nmcli radio wifi on; }
# through logind, as in daily use (NetworkManager puts the device to sleep
# first); the RTC alarm wakes the machine, our own inhibitor lock is ignored
suspend_resume() {
	local t0 t=0
	t0=$(date +%s)
	sync
	rtcwake -m no -s 45 >/dev/null
	systemctl suspend -i
	while [ "$t" -lt 300 ]; do
		journalctl -k --since "@$t0" -o cat --no-pager 2>/dev/null | grep -q 'PM: suspend exit' && break
		sleep 2
		t=$((t + 2))
	done
	note "suspend: $(journalctl -k --since "@$t0" -o cat --no-pager | grep -E 'PM: suspend (entry|exit)' | tr '\n' ' ')"
	sleep 5
}

# capture LABEL PARAMS: mmiotrace of KO loading (tools/re/capture-on-target.sh,
# but rmmod/insmod: modprobe -r would unload cfg80211)
capture() {
	local l=$1 params=$2 d=$out/capture-$1 dev catpid t0 s
	[ "$capture" -eq 1 ] || return 0
	grep -qw mmiotrace "$TR/available_tracers" 2>/dev/null || { say "skip capture: no mmiotrace"; return 0; }
	dev=$(lspci -Dn -d 14e4:43a0 | awk 'NR == 1 { print $1 }')
	mkdir -p "$d"
	{
		uname -a
		lspci -vvnn -s "$dev"
		echo "--- configuration space"
		lspci -xxxx -s "$dev"
		echo "--- resources"
		cat "/sys/bus/pci/devices/$dev/resource"
	} > "$d/pci.txt" 2>&1
	pace
	busy
	touch "$DIRTY"
	t0=$(date +%s)
	note "capture $l: rmmod, mmiotrace on, insmod $tko $params"
	timeout 60 rmmod bcm4360 || { say "FAIL capture-$l: cannot unload"; return 1; }
	echo "bcm4360-capture: start" > /dev/kmsg
	echo 65536 > "$TR/buffer_size_kb"
	echo mmiotrace > "$TR/current_tracer"
	cat "$TR/trace_pipe" > "$d/mmiotrace.txt" &
	catpid=$!
	sleep 1
	echo "MARK load bcm4360 $params" > "$TR/trace_marker"
	# shellcheck disable=SC2086
	insmod "$tko" $params || say "FAIL capture-$l: insmod failed"
	for s in .text .rodata .data .bss; do
		[ -r "/sys/module/bcm4360/sections/$s" ] &&
			echo "$s $(cat "/sys/module/bcm4360/sections/$s")"
	done > "$d/sections.txt"
	sleep 50
	echo "MARK end" > "$TR/trace_marker"
	sleep 1
	# the tracer cannot be changed while trace_pipe is open (EBUSY)
	kill "$catpid" 2>/dev/null
	wait "$catpid" 2>/dev/null
	echo nop > "$TR/current_tracer"
	for s in /sys/devices/system/cpu/cpu[0-9]*/online; do echo 1 > "$s" 2>/dev/null; done
	attempted
	journalctl -k --since "@$t0" --no-pager > "$d/dmesg.txt" 2>&1
	say "CAPTURE $l: $(wc -l < "$d/mmiotrace.txt") lines, $(grep -c '^[RW] ' "$d/mmiotrace.txt") accesses"
	wait_connected 120 "$conn" || say "FAIL capture-$l: not connected after the capture"
	idle
	klog_check "$t0" "capture-$l"
}

on_exit() {
	trap - EXIT TERM INT
	# the deadman stops this unit while it restores itself
	if [ -e /run/bcm4360-card-test.deadman ]; then
		note "stopped by the deadman"
		exit 1
	fi
	say "===== restoring the installed driver ====="
	bash "$here/card-test-restore.sh" "${restore_args[@]}" ${conn:+--conn "$conn"}
	say "after restore: $(running_version), $(connected && echo connected || echo NOT connected)"
	say "===== card-test finished ====="
	[ -n "$owner" ] && chown -R "$owner": "$out"
}

# ---- preflight -------------------------------------------------------------------

say "===== card-test: $ko ====="
rm -f /run/bcm4360-card-test.deadman
kver=$(uname -r)
if [ ! -d "/lib/modules/$kver/kernel" ]; then
	say "ABORT: /lib/modules/$kver is missing (kernel upgraded, not rebooted): nothing could be loaded"
	exit 1
fi
if [ "$(modinfo -F vermagic "$ko" | awk '{ print $1 }')" != "$kver" ]; then
	say "ABORT: $ko is not built for $kver ($(modinfo -F vermagic "$ko"))"
	exit 1
fi
modinfo bcm4360 >/dev/null 2>&1 || { say "ABORT: no installed bcm4360 to fall back to"; exit 1; }
connected || { say "ABORT: not connected now"; exit 1; }
conn=$(nmcli -g GENERAL.CONNECTION dev show "$iface")
tko=$out/bcm4360-test.ko
cp "$ko" "$tko"
say "kernel $kver; running $(running_version); installed bcm4360 $(modinfo -F version bcm4360);" \
    "wl $(modinfo -F filename wl >/dev/null 2>&1 && echo present || echo missing); test build $(modinfo -F version "$tko")" \
    "$(modinfo -F srcversion "$tko"); connection '$conn' on $iface"
say "plan: soak ${soak} min (open PHY) and ${base_soak} min (Broadcom's PHY), 2.4 GHz $band24 (${watch24}s)," \
    "capture $capture, suspend $suspend, gap ${gap}s, deadline ${deadline} min," \
    "extra '${extra}', off $((1 - skip_off)), confirm '${confirm}', captures '${captures:-default}'" \
    "${ab24:+, ab24 '$ab24'}"

trap on_exit EXIT
trap 'note "interrupted"; exit 1' TERM INT

# a deadman left over from an earlier run would make systemd-run fail
systemctl stop bcm4360-card-deadman.timer bcm4360-card-deadman.service 2>/dev/null
systemctl reset-failed bcm4360-card-deadman.timer bcm4360-card-deadman.service 2>/dev/null
if ! systemd-run --unit=bcm4360-card-deadman --collect --on-active="${deadline}m" \
	--timer-property=AccuracySec=10s \
	/bin/bash "$here/card-test-restore.sh" "${restore_args[@]}" --conn "$conn"; then
	trap - EXIT
	say "ABORT: cannot arm the deadman timer"
	exit 1
fi
note "deadman armed: ${deadline} min"
rm -f "$BUSY"
guard &
note "guard started"
systemctl stop bcm4360-watchdog.timer && note "watchdog timer paused"
t_start=$(date +%s)

# ---- stages ----------------------------------------------------------------------

say "----- base: the installed driver -----"
measure base

# --capture-radio: the radio switch of each configuration under mmiotrace
if [ -n "$capture_radio" ]; then
	grep -qw mmiotrace "$TR/available_tracers" || { say "ABORT: no mmiotrace"; exit 1; }
	IFS=';' read -r -a clist <<< "$capture_radio"
	for c in "${clist[@]}"; do
		cl=${c%%:*}
		cp=${c#*:}
		[ "$cp" = ALLOFF ] && cp=$ALL_OFF
		say "----- capture-radio $cl: ${cp:-defaults} -----"
		d=$out/capture-radio-$cl
		mkdir -p "$d"
		dev=$(lspci -Dn -d 14e4:43a0 | awk 'NR == 1 { print $1 }')
		{ lspci -vvnn -s "$dev"; echo "--- configuration space"; lspci -xxxx -s "$dev";
		  echo "--- resources"; cat "/sys/bus/pci/devices/$dev/resource"; } > "$d/pci.txt" 2>&1
		pace
		busy
		touch "$DIRTY"
		t0=$(date +%s)
		timeout 60 rmmod bcm4360 || { say "FAIL capture-radio-$cl: cannot unload"; continue; }
		echo "bcm4360-capture: start" > /dev/kmsg
		echo 131072 > "$TR/buffer_size_kb"
		echo mmiotrace > "$TR/current_tracer"
		cat "$TR/trace_pipe" > "$d/mmiotrace.txt" &
		catpid=$!
		sleep 1
		echo "MARK load bcm4360 $cp" > "$TR/trace_marker"
		# shellcheck disable=SC2086
		insmod "$tko" $cp || say "FAIL capture-radio-$cl: insmod failed"
		for s in .text .rodata .data .bss; do
			[ -r "/sys/module/bcm4360/sections/$s" ] &&
				echo "$s $(cat "/sys/module/bcm4360/sections/$s")"
		done > "$d/sections.txt"
		if wait_connected 150 "$conn"; then
			say "CAPTURE-RADIO $cl: connected under the trace after $(($(date +%s) - t0))s"
			sleep 10
			# the core's state (country, chains, counters); its own register
			# reads are between the two marks
			echo "MARK diag" > "$TR/trace_marker"
			diag "capture-radio-$cl"
			echo "MARK diag end" > "$TR/trace_marker"
			echo "MARK radio off" > "$TR/trace_marker"
			nmcli radio wifi off
			sleep 5
			echo "MARK radio on" > "$TR/trace_marker"
			nmcli radio wifi on
			t1=$(date +%s)
			if wait_connected 120 "$conn"; then
				say "CAPTURE-RADIO $cl: connected again after the radio switch, $(($(date +%s) - t1))s"
				echo "MARK reconnected" > "$TR/trace_marker"
				sleep 10
			else
				say "CAPTURE-RADIO $cl: NOT connected again after the radio switch"
				echo "MARK not reconnected" > "$TR/trace_marker"
				diag "capture-radio-$cl-fail"
			fi
		else
			say "CAPTURE-RADIO $cl: not connected under the trace"
		fi
		echo "MARK end" > "$TR/trace_marker"
		sleep 1
		kill "$catpid" 2>/dev/null
		wait "$catpid" 2>/dev/null
		echo nop > "$TR/current_tracer"
		for s in /sys/devices/system/cpu/cpu[0-9]*/online; do echo 1 > "$s" 2>/dev/null; done
		attempted
		journalctl -k --since "@$t0" --no-pager > "$d/dmesg.txt" 2>&1
		journalctl --since "@$t0" --no-pager -u wpa_supplicant -u NetworkManager > "$d/supplicant.txt" 2>&1
		say "CAPTURE-RADIO $cl: $(grep -c '^[RW] ' "$d/mmiotrace.txt") accesses"
		# the next configuration's load recovers the card
		idle
	done
	say "RESULT: radio captures done ($(($(date +%s) - t_start)) s)"
	exit 0
fi

# --ab24: each configuration loaded, measured and watched on the same 2.4 GHz
# AP; a configuration that does not load is recorded and the next one goes on
if [ -n "$ab24" ]; then
	IFS=';' read -r -a alist <<< "$ab24"
	for a in "${alist[@]}"; do
		al=${a%%:*}
		ap=${a#*:}
		[ "$ap" = ALLOFF ] && ap=$ALL_OFF
		say "----- ab24 $al: ${ap:-defaults} -----"
		busy
		if swap "$ap" "$al"; then
			t0=$(date +%s)
			if measure "$al"; then
				band24 "$al"
				[ -n "$fixed_bssid" ] || fixed_bssid=$last24_bssid
			fi
			klog_check "$t0" "$al"
		fi
		idle
	done
	say "RESULT: ab24 done on ${fixed_bssid:-no AP} ($(($(date +%s) - t_start)) s)"
	exit 0
fi

# the matrix of --quick: the step that fails ends that configuration only;
# the whole configuration counts as busy for the guard (6 minutes), and the
# next load recovers the card
if [ -n "$quick" ]; then
	IFS=';' read -r -a qlist <<< "$quick"
	for q in "${qlist[@]}"; do
		ql=${q%%:*}
		qp=${q#*:}
		[ "$qp" = ALLOFF ] && qp=$ALL_OFF
		say "----- quick $ql: ${qp:-defaults} -----"
		busy
		if swap "$qp" "$ql"; then
			busy
			passed=0
			for k in $(seq 1 "$cycles"); do
				cycle "$ql-radio-$k" radio_off_on || break
				busy
				passed=$k
			done
			say "CYCLES $ql: $passed of $cycles radio switches reconnected"
			if [ "$passed" -eq "$cycles" ] && [ "$measure_after" -eq 1 ]; then
				measure "$ql-load" && watch_link 60 "$ql-load" && measure "$ql-after"
			fi
		fi
		idle
	done
	say "RESULT: quick matrix done ($(($(date +%s) - t_start)) s)"
	exit 0
fi

say "----- peek: identity of the card -----"
(cd "$out" && timeout 60 python3 -I "$root/tools/re/peek-on-target.py" > "$out/peek.txt" 2> "$out/peek.err")
say "PEEK: rc $?, $(wc -l < "$out/peek.txt") lines"
klog_check "$t_start" base

ok_off=-1
if [ "$skip_off" -eq 0 ]; then
	say "----- off: every switch off -----"
	ok_off=0
	if swap "$ALL_OFF" off; then ok_off=1; measure off; fi
fi

if [ -n "$confirm" ]; then
	say "----- confirm: $confirm -----"
	if swap "$confirm" confirm; then measure confirm; fi
fi

say "----- def: the open layers, Broadcom's PHY (open_phy=0)${extra:+ + $extra} -----"
ok_def=0
if swap "open_phy=0${extra:+ $extra}" def; then
	ok_def=1
	t0=$(date +%s)
	measure def
	band24 def
	soak "$base_soak" def || say "FAIL def-soak: too long without connection"
	klog_check "$t0" def
fi
# further loads of the same configuration: how often does it connect?
def_pass=$ok_def
for k in $(seq 2 "$def_repeat"); do
	if swap "open_phy=0${extra:+ $extra}" "def-load-$k"; then
		def_pass=$((def_pass + 1))
		measure "def-load-$k"
	fi
done
[ "$def_repeat" -gt 1 ] && say "LOADS def: $def_pass of $def_repeat connected"

# The defaults failed: find the layer at fault by halving the set of layers
# switched off (assumes one layer at fault; about four swaps).
bisect() {
	local cand=(pmu access mac utils clm ppr srom si dma chan iovar nvram rate cca stf) half p l
	while [ "${#cand[@]}" -gt 1 ]; do
		half=("${cand[@]:0:$((${#cand[@]} / 2))}")
		p=
		for l in "${half[@]}"; do p="$p open_$l=0"; done
		if swap "${p# }" "bisect-off-$(IFS=,; echo "${half[*]}")"; then
			cand=("${half[@]}")
		else
			cand=("${cand[@]:${#half[@]}}")
		fi
	done
	say "BISECT: suspect open_${cand[0]} (each halving kept the half whose switching off made the module pass)"
}

say "----- phy: the defaults and the open PHY${extra:+ + $extra} -----"
ok_phy=0
if [ "$ok_def" -eq 0 ] && [ "$ok_off" -eq 1 ]; then
	say "skip phy: the defaults failed; bisecting the open layers"
	bisect
elif swap "open_phy=1${extra:+ $extra}" phy; then
	ok_phy=1
	t0=$(date +%s)
	measure phy
	band24 phy
	if cycle phy-reconnect-1 disconnect_connect &&
		cycle phy-radio-1 radio_off_on &&
		measure phy-radio-1 &&
		cycle phy-radio-2 radio_off_on &&
		cycle phy-linkdown link_down_up &&
		cycle phy-reconnect-2 disconnect_connect &&
		{ [ "$suspend" -eq 0 ] || { cycle phy-suspend suspend_resume && measure phy-resumed &&
			watch_link 60 phy-resumed && measure phy-resumed-2; }; }; then
		soak "$soak" phy || { ok_phy=0; say "FAIL phy-soak: too long without connection"; }
		measure phy-end
	else
		ok_phy=0
		say "FAIL phy: a reconnect step failed; no soak on top of it"
	fi
	klog_check "$t0" phy || ok_phy=0
fi

say "----- capture -----"
if [ -n "$captures" ]; then
	IFS=';' read -r -a caplist <<< "$captures"
	for c in "${caplist[@]}"; do
		capture "${c%%:*}" "${c#*:}"
	done
else
	if [ "$ok_off" -eq 1 ]; then capture broadcom "$ALL_OFF"; fi
	if [ "$ok_phy" -eq 1 ]; then capture openphy "open_phy=1${extra:+ $extra}"; fi
fi

say "RESULT: off $ok_off, def $ok_def, phy $ok_phy ($(($(date +%s) - t_start)) s)"
exit 0
