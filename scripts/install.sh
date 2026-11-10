#!/bin/sh
# Build and install bcm4360 through DKMS (rebuilt automatically for new
# kernels) and make it the driver for the card from the next boot on.
# It does not touch the running driver.
#
#   install.sh           bcm4360: Broadcom's core with the open layers
#   install.sh --open    bcm4360_open: no Broadcom code. bcm4360 is installed
#                        too and stays the fallback: the watchdog switches to it
#                        if bcm4360_open does not connect or cannot keep a
#                        connection, the fallback service if it is not built
#                        for a new kernel.
#
# Nothing on the system changes before Broadcom's download is verified, the
# firmware data is cut (--open) and the drivers have built for the running
# kernel.
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
oname=bcm4360-open
ver=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"$/\1/p' dkms.conf)
kver=$(uname -r)
kdir=/lib/modules/$kver/build

# ---- checks and preparation; nothing installed yet ---------------------------

need() {
	command -v "$1" >/dev/null 2>&1 || { echo "install.sh: $1 is missing ($2)" >&2; exit 1; }
}
need dkms "install your distribution's dkms package"
need make "install your distribution's build tools"
[ "$open" -eq 0 ] || need python3 "install.sh --open needs it to cut the firmware data"
[ -d "$kdir" ] || { echo "install.sh: no headers for kernel $kver in $kdir" >&2; exit 1; }

# Broadcom's core: an object already in lib/ is checked again; a wrong one
# is fetched anew
if ! sh scripts/fetch-blob.sh --check; then
	[ ! -e lib/wlc_hybrid.o_shipped ] || echo "lib/wlc_hybrid.o_shipped is not Broadcom's 6.30.223.271: fetching it again"
	sh scripts/fetch-blob.sh
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

# bcm4360_open's data (microcode, tables, regulatory database), cut out of
# Broadcom's download like b43-fwcutter does
if [ "$open" -eq 1 ]; then
	python3 -I scripts/bcm4360-fwcut.py lib/wlc_hybrid.o_shipped "$tmp/bcm4360-data.bin"
fi

# The source trees as DKMS gets them: bcm4360's with Broadcom's core and the
# hybrid adapters, bcm4360-open's with drv/. Objects from a local `make` must
# never stand in for a clean build.
stage() {	# stage DIR DKMS_CONF FILE...
	d=$1 conf=$2
	shift 2
	mkdir -p "$d"
	cp -r "$@" "$d/"
	sed "s/@VERSION@/$ver/" "$conf" >"$d/dkms.conf"
	find "$d" \( -name '*.o' -o -name '*.ko' -o -name '.*.cmd' -o -name '*.mod' \
		-o -name '*.mod.c' -o -name 'modules.order' -o -name 'Module.symvers' \) -delete
}
stage "$tmp/$name" dkms.conf Makefile src lib hybrid open
mkdir -p "$tmp/$name/scripts"
cp scripts/mkhybrid.sh "$tmp/$name/scripts/"
[ "$open" -eq 0 ] || stage "$tmp/$oname" dkms-open.conf Makefile src open drv

# A build for the running kernel first, in a copy: a tree that does not build
# never replaces what is installed.
trial() {	# trial TREE TARGET
	echo "Building $1 for $kver (a trial before installing)..."
	cp -r "$tmp/$1" "$tmp/trial-$1"
	if ! make -C "$tmp/trial-$1" -j"$(nproc 2>/dev/null || echo 2)" KVER="$kver" "$2" \
		>"$tmp/trial-$1.log" 2>&1; then
		tail -n 30 "$tmp/trial-$1.log" >&2
		echo "install.sh: $1 does not build for kernel $kver; nothing was changed" >&2
		exit 1
	fi
}
trial "$name" all
[ "$open" -eq 0 ] || trial "$oname" open

# ---- installation ------------------------------------------------------------

# the versions of a DKMS package (dkms 3: "name/ver, ...", dkms 2: "name, ver, ...")
versions() {
	dkms status -m "$1" 2>/dev/null |
		sed -n -e "s|^$1/\([^,:]*\)[,:].*|\1|p" -e "s|^$1, \([^,:]*\)[,:].*|\1|p" | sort -u
}

remove() {	# remove PACKAGE VERSION
	dkms remove -m "$1" -v "$2" --all
	rm -rf "/usr/src/$1-$2"
}

# Install this version of a package and drop every other one: they all
# install to the same module path.
replace() {	# replace PACKAGE
	# the same version cannot be added twice: it goes first (it built above)
	if versions "$1" | grep -qxF "$ver"; then
		remove "$1" "$ver"
	fi
	rm -rf "/usr/src/$1-$ver"
	cp -r "$tmp/$1" "/usr/src/$1-$ver"
	dkms add -m "$1" -v "$ver"
	dkms build -m "$1" -v "$ver"
	for old in $(versions "$1"); do
		[ "$old" = "$ver" ] || remove "$1" "$old"
	done
	dkms install -m "$1" -v "$ver"
}

replace "$name"
if [ "$open" -eq 1 ]; then
	replace "$oname"
else
	for old in $(versions "$oname"); do
		remove "$oname" "$old"
	done
fi

install -Dm644 modprobe.d/bcm4360.conf /etc/modprobe.d/bcm4360.conf
if [ "$open" -eq 1 ]; then
	# blacklisting bcm4360 lets bcm4360_open take the card at boot (an
	# explicit modprobe of bcm4360, the watchdog's fallback, still works)
	install -Dm644 "$tmp/bcm4360-data.bin" /lib/firmware/bcm4360/bcm4360-data.bin
	install -Dm644 modprobe.d/bcm4360-open.conf /etc/modprobe.d/bcm4360-open.conf
else
	rm -f /etc/modprobe.d/bcm4360-open.conf
fi

# the watchdog ran from a timer up to 1.2.1
systemctl disable --now bcm4360-watchdog.timer 2>/dev/null || true
rm -f /etc/systemd/system/bcm4360-watchdog.timer
install -Dm755 systemd/bcm4360-fallback /usr/local/lib/bcm4360/bcm4360-fallback
install -Dm644 systemd/bcm4360-fallback.service /etc/systemd/system/bcm4360-fallback.service
install -Dm755 systemd/bcm4360-watchdog /usr/local/lib/bcm4360/bcm4360-watchdog
install -Dm644 systemd/bcm4360-watchdog.service /etc/systemd/system/bcm4360-watchdog.service
systemctl daemon-reload
systemctl enable bcm4360-fallback.service
systemctl enable bcm4360-watchdog.service
systemctl restart bcm4360-watchdog.service
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
