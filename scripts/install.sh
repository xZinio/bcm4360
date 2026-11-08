#!/bin/sh
# Build and install bcm4360 through DKMS (rebuilt automatically for new
# kernels) and make it the driver for the card from the next boot on.
# It does not touch the running driver.
#
#   install.sh           bcm4360: Broadcom's core with the open layers
#   install.sh --open    bcm4360_open: no Broadcom code. bcm4360 is installed
#                        too and stays the fallback: the watchdog switches to it
#                        if bcm4360_open cannot keep a connection.
set -eu

[ "$(id -u)" -eq 0 ] || { echo "install.sh must run as root" >&2; exit 1; }
cd "$(dirname "$0")/.."
open=0
case ${1:-} in
--open) open=1 ;;
"") ;;
*) echo "usage: install.sh [--open]" >&2; exit 2 ;;
esac

name=bcm4360
ver=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"$/\1/p' dkms.conf)
dest=/usr/src/$name-$ver

[ -f lib/wlc_hybrid.o_shipped ] || sh scripts/fetch-blob.sh

if dkms status -m "$name" -v "$ver" 2>/dev/null | grep -q .; then
	dkms remove -m "$name" -v "$ver" --all
fi
rm -rf "$dest"
mkdir -p "$dest/scripts"
# hybrid/ and open/: the open code that takes over parts of Broadcom's core
cp -r Makefile dkms.conf src lib hybrid open drv "$dest/"
cp scripts/mkhybrid.sh "$dest/scripts/"
# Objects from a local `make` must never stand in for a clean DKMS build.
find "$dest/src" "$dest/hybrid" "$dest/open" "$dest/drv" \
	\( -name '*.o' -o -name '.*.cmd' -o -name '*.mod' -o -name '*.mod.c' \) -delete
rm -f "$dest/lib/wlc_hybrid_open.o"

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
# bcm4360_open's data (microcode, tables, regulatory database), cut out of
# Broadcom's download like b43-fwcutter does; the default: blacklisting
# bcm4360 lets bcm4360_open take the card at boot (an explicit modprobe of
# bcm4360, the watchdog's fallback, still works)
if [ "$open" -eq 1 ]; then
	python3 -I scripts/bcm4360-fwcut.py lib/wlc_hybrid.o_shipped /lib/firmware/bcm4360/bcm4360-data.bin
	install -Dm644 modprobe.d/bcm4360-open.conf /etc/modprobe.d/bcm4360-open.conf
else
	rm -f /etc/modprobe.d/bcm4360-open.conf
fi
install -Dm644 systemd/bcm4360-fallback.service /etc/systemd/system/bcm4360-fallback.service
install -Dm755 systemd/bcm4360-watchdog /usr/local/lib/bcm4360/bcm4360-watchdog
install -Dm644 systemd/bcm4360-watchdog.service /etc/systemd/system/bcm4360-watchdog.service
install -Dm644 systemd/bcm4360-watchdog.timer /etc/systemd/system/bcm4360-watchdog.timer
systemctl daemon-reload
systemctl enable bcm4360-fallback.service
systemctl enable --now bcm4360-watchdog.timer
install -Dm644 lib/LICENSE.txt "/usr/share/licenses/$name/BROADCOM-LICENSE.txt"
depmod -a

echo
if [ "$open" -eq 1 ]; then
	echo "bcm4360 $ver installed with bcm4360_open as the driver; it takes over from the next boot."
	echo "To switch right now instead (drops Wi-Fi for ~10 s):"
	echo "  sudo rmmod bcm4360; sudo modprobe bcm4360_open"
else
	echo "bcm4360 $ver installed; it takes over from the next boot."
	echo "To switch right now instead (drops Wi-Fi for ~10 s):"
	echo "  sudo rmmod bcm4360; sudo modprobe bcm4360; sudo systemctl restart wpa_supplicant"
fi
