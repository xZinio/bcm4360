#!/usr/bin/env bash
# SPDX-License-Identifier: ISC
#
# Build the experimental HYBRID module (layers of Broadcom's object replaced
# by the open code, see hybrid/README.md) in a directory of its own, check
# that every replaced function of the object now resolves to the open code,
# and print how to try it with swap-test.sh. Does not install anything and
# does not touch the installed module or the Wi-Fi link.
#
#   scripts/hybrid-build.sh [LAYERS] [OUTDIR]
#
# LAYERS: default "access pmu mac"; OUTDIR: default /tmp/bcm4360-hybrid.
set -eu

layers=${1:-access pmu mac}
out=${2:-/tmp/bcm4360-hybrid}
here=$(cd "$(dirname "$0")/.." && pwd)
kdir=${KDIR:-/lib/modules/$(uname -r)/build}

[ -f "$here/lib/wlc_hybrid.o_shipped" ] || { echo "lib/wlc_hybrid.o_shipped is missing: run 'make fetch' first" >&2; exit 1; }
rm -rf "$out"
mkdir -p "$out"
cp -r "$here/Makefile" "$here/src" "$here/lib" "$here/open" "$here/hybrid" "$out/"
echo "building HYBRID=\"$layers\" in $out against $kdir"
make -C "$kdir" M="$out" HYBRID="$layers" modules >"$out/build.log" 2>&1 || {
	tail -n 30 "$out/build.log"; echo "BUILD FAILED (log: $out/build.log)" >&2; exit 1; }

# Every replaced name: weak in the processed copy of the object, defined once
# (strong, by the bridge) in the linked module.
bad=0
for l in $layers; do grep -v '^#' "$out/hybrid/$l.weaken"; done | sort -u >"$out/weak.lst"
nm "$out/wlc_hybrid_open.o" >"$out/blob.nm"
nm "$out/bcm4360.o" >"$out/mod.nm"
while read -r s; do
	b=$(awk -v s="$s" '$3 == s { print $2 }' "$out/blob.nm" | tr '\n' ' ')
	m=$(awk -v s="$s" '$3 == s { print $2 }' "$out/mod.nm" | tr '\n' ' ')
	[ "$b" = "W " ] || { echo "  $s: '$b' in the processed object (expected W)"; bad=1; }
	[ "$m" = "T " ] || { echo "  $s: '$m' in the module (expected one T)"; bad=1; }
done <"$out/weak.lst"
[ "$bad" -eq 0 ] || { echo "SYMBOL CHECK FAILED" >&2; exit 1; }
echo "ok: $(wc -l <"$out/weak.lst") functions of Broadcom's object now run as open code"
echo "module: $out/bcm4360.ko ($(modinfo -F vermagic "$out/bcm4360.ko"))"
cat <<EOF

Try it (the Wi-Fi link goes down for a minute; swap-test.sh checks the link
and goes back to the installed bcm4360 by itself if anything fails):

  sudo systemd-run --unit=bcm4360-hybrid --collect \\
      systemd-inhibit --what=sleep:idle --why="hybrid driver test" \\
      $here/scripts/swap-test.sh $out/bcm4360.ko --fallback bcm4360 --reconnects 3
  sudo tail -f /var/log/bcm4360-swap-test.log

To return to the installed module afterwards: sudo rmmod bcm4360; sudo modprobe bcm4360
EOF
