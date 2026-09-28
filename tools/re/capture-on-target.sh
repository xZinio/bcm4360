#!/bin/bash
# SPDX-License-Identifier: ISC
#
# Run on the machine that has the BCM4360 (as root). Records what is needed to
# make the emulator's model of the card match the real card:
#
#   pci.txt         lspci output and the PCI configuration space
#   mmiotrace.txt   every register access of the driver while it loads,
#                   brings the interface up and connects (kernel mmiotrace)
#   sections.txt    load addresses of the module's sections, to map the
#                   program counters in the trace to functions
#   dmesg.txt       kernel log of the session
#
#   capture-on-target.sh [MODULE] [SECONDS]      default: bcm4360 45
#
# Wi-Fi is down while this runs. mmiotrace takes all CPUs but one offline for
# the duration of the trace and slows the machine down; both end with the
# trace. Nothing is written to the card. The result is a directory
# bcm4360-capture-<date>/ and a tarball of it next to it. The trace contains
# the MAC address of the card, the names of the networks in range and the
# frames received while tracing: look at it before you hand it to anyone.
set -eu

MODULE=${1:-bcm4360}
SECONDS_UP=${2:-45}
TR=/sys/kernel/tracing
[ -d "$TR" ] || TR=/sys/kernel/debug/tracing

[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }
[ -d "$TR" ] || { echo "no tracefs at /sys/kernel/tracing" >&2; exit 1; }
grep -qw mmiotrace "$TR/available_tracers" || {
	echo "this kernel has no mmiotrace (CONFIG_MMIOTRACE)" >&2; exit 1; }
DEV=$(lspci -Dn -d 14e4:43a0 | awk 'NR==1 {print $1}')
[ -n "$DEV" ] || { echo "no 14e4:43a0 device" >&2; exit 1; }

OUT=$PWD/bcm4360-capture-$(date +%Y%m%d-%H%M%S)
mkdir "$OUT"

restore() {
	echo nop > "$TR/current_tracer" 2>/dev/null || true
	[ -n "${CATPID:-}" ] && kill "$CATPID" 2>/dev/null || true
	for c in /sys/devices/system/cpu/cpu[0-9]*/online; do
		echo 1 > "$c" 2>/dev/null || true
	done
}
trap restore EXIT INT TERM

{
	uname -a
	lspci -vvnn -s "$DEV"
	echo "--- configuration space"
	lspci -xxxx -s "$DEV"
	echo "--- resources"
	cat "/sys/bus/pci/devices/$DEV/resource"
} > "$OUT/pci.txt" 2>&1

echo "unloading drivers of $DEV"
for m in "$MODULE" wl b43 brcmsmac brcmfmac bcma ssb; do
	modprobe -r "$m" 2>/dev/null || true
done

echo "bcm4360-capture: start" > /dev/kmsg
echo 65536 > "$TR/buffer_size_kb"
echo mmiotrace > "$TR/current_tracer"
cat "$TR/trace_pipe" > "$OUT/mmiotrace.txt" &
CATPID=$!
sleep 1

echo "loading $MODULE and waiting $SECONDS_UP s (connect to a network now if it does not happen by itself)"
echo "MARK load $MODULE" > "$TR/trace_marker"
modprobe "$MODULE"
for s in /sys/module/$MODULE/sections/.text /sys/module/$MODULE/sections/.rodata \
	/sys/module/$MODULE/sections/.data /sys/module/$MODULE/sections/.bss; do
	[ -r "$s" ] && echo "$(basename "$s") $(cat "$s")"
done > "$OUT/sections.txt"
sleep "$SECONDS_UP"
echo "MARK end" > "$TR/trace_marker"

echo nop > "$TR/current_tracer"
sleep 1
kill "$CATPID" 2>/dev/null || true
CATPID=
dmesg | sed -n '/bcm4360-capture: start/,$p' > "$OUT/dmesg.txt"
ip -br link > "$OUT/links.txt" 2>&1 || true

lines=$(wc -l < "$OUT/mmiotrace.txt")
echo "$lines lines of trace"
tar -C "$(dirname "$OUT")" -czf "$OUT.tar.gz" "$(basename "$OUT")"
echo "done: $OUT.tar.gz"
