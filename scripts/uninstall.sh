#!/bin/sh
# SPDX-License-Identifier: ISC
#
# Remove bcm4360. From the next boot the previously installed driver
# (e.g. Broadcom's wl from broadcom-wl-dkms) is used again.
set -eu

[ "$(id -u)" -eq 0 ] || { echo "uninstall.sh must run as root" >&2; exit 1; }
cd "$(dirname "$0")/.."

name=bcm4360
ver=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"$/\1/p' dkms.conf)

if dkms status -m "$name" -v "$ver" 2>/dev/null | grep -q .; then
	dkms remove -m "$name" -v "$ver" --all
fi
rm -rf "/usr/src/$name-$ver" "/usr/share/licenses/$name"
rm -f /etc/modprobe.d/bcm4360.conf
depmod -a

echo "bcm4360 removed. The previous driver takes over at the next boot;"
echo "to switch right now: sudo systemd-run --unit=bcm4360-swap --collect /bin/bash $(pwd)/scripts/swap-test.sh wl"
