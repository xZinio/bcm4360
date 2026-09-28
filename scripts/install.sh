#!/bin/sh
# SPDX-License-Identifier: ISC
#
# Build and install bcm4360 through DKMS (rebuilt automatically for new
# kernels) and make it the driver for the card from the next boot on.
# It does not touch the running driver; to switch without rebooting use
# scripts/swap-test.sh, which reconnects and rolls back on failure.
set -eu

[ "$(id -u)" -eq 0 ] || { echo "install.sh must run as root" >&2; exit 1; }
cd "$(dirname "$0")/.."

name=bcm4360
ver=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"$/\1/p' dkms.conf)
dest=/usr/src/$name-$ver

[ -f lib/wlc_hybrid.o_shipped ] || sh scripts/fetch-blob.sh

if dkms status -m "$name" -v "$ver" 2>/dev/null | grep -q .; then
	dkms remove -m "$name" -v "$ver" --all
fi
rm -rf "$dest"
mkdir -p "$dest"
cp -r Makefile dkms.conf src lib "$dest/"
# Objects from a local `make` must never stand in for a clean DKMS build.
find "$dest/src" \( -name '*.o' -o -name '.*.cmd' -o -name '*.mod' -o -name '*.mod.c' \) -delete

dkms add -m "$name" -v "$ver"
dkms build -m "$name" -v "$ver"
# Only now that this version has built, drop any other installed version:
# both install to the same module path.
for old in $(dkms status -m "$name" 2>/dev/null | sed -n "s|^$name/\([^,:]*\)[,:].*|\1|p" | sort -u); do
	[ "$old" = "$ver" ] && continue
	dkms remove -m "$name" -v "$old" --all
	rm -rf "/usr/src/$name-$old"
done
dkms install -m "$name" -v "$ver"

install -Dm644 modprobe.d/bcm4360.conf /etc/modprobe.d/bcm4360.conf
install -Dm644 lib/LICENSE.txt "/usr/share/licenses/$name/BROADCOM-LICENSE.txt"
depmod -a

echo
echo "bcm4360 $ver installed; it takes over from the next boot."
echo "To switch right now (drops Wi-Fi for ~10 s, rolls back on failure):"
echo "  sudo systemd-run --unit=bcm4360-swap --collect /bin/bash $(pwd)/scripts/swap-test.sh bcm4360"
echo "  (result: /var/log/bcm4360-swap-test.log)"
