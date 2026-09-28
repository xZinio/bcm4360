#!/bin/sh
# SPDX-License-Identifier: ISC
#
# Decompile lib/wlc_hybrid.o_shipped with Ghidra (headless) into re-out/ghidra
# and build the cross reference index.
#
#   decompile.sh [GHIDRA_DIR]
#
# GHIDRA_DIR defaults to $GHIDRA_INSTALL_DIR. Needs a JDK 21 (JAVA_HOME or PATH).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=$root/re-out
ghidra=${1:-${GHIDRA_INSTALL_DIR:-}}
[ -n "$ghidra" ] || { echo "usage: $0 GHIDRA_DIR" >&2; exit 1; }
blob=$root/lib/wlc_hybrid.o_shipped
[ -f "$blob" ] || { echo "$blob is missing (make fetch)" >&2; exit 1; }

mkdir -p "$out/ghidra" "$out/ghidra-proj"
cd "$here"
python3 blob.py funcs | cut -d' ' -f1 > "$out/entries.txt"
"$ghidra/support/analyzeHeadless" "$out/ghidra-proj" bcm4360 \
	-import "$blob" -overwrite -scriptPath "$here/ghidra" \
	-postScript ExportDecomp.java "$out/ghidra" "$out/entries.txt" \
	> "$out/ghidra-run.log" 2>&1
grep -E 'ExportDecomp.java>|ERROR|REPORT' "$out/ghidra-run.log" || true
python3 index.py build
