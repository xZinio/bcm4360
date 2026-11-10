#!/bin/sh
# The firmware cutter makes, from Broadcom's object in lib/ (make fetch), the
# very file it always made: bcm4360_open's data must not change by accident.
set -eu

cd "$(dirname "$0")/.."
want=3a3a2ba9771144de6922e26b0b15c47837030cefe62c898f13c87689c524a2e6
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

python3 -I scripts/bcm4360-fwcut.py lib/wlc_hybrid.o_shipped "$tmp/bcm4360-data.bin"
got=$(sha256sum "$tmp/bcm4360-data.bin" | cut -d' ' -f1)
if [ "$got" != "$want" ]; then
	echo "fwcut: SHA-256 of the output is $got, not $want" >&2
	exit 1
fi
echo "fwcut: output as pinned"
