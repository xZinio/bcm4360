#!/bin/sh
# Remove bcm4360 and bcm4360_open, whatever versions DKMS has of them (not
# only the one of this checkout). From the next boot the previously installed
# driver (e.g. Broadcom's wl from broadcom-wl-dkms) is used again.
set -eu

[ "$(id -u)" -eq 0 ] || { echo "uninstall.sh must run as root" >&2; exit 1; }

# the versions of a DKMS package (dkms 3: "name/ver, ...", dkms 2: "name, ver, ...")
versions() {
	dkms status -m "$1" 2>/dev/null |
		sed -n -e "s|^$1/\([^,:]*\)[,:].*|\1|p" -e "s|^$1, \([^,:]*\)[,:].*|\1|p" | sort -u
}

if command -v dkms >/dev/null 2>&1; then
	for pkg in bcm4360-open bcm4360; do
		for v in $(versions "$pkg"); do
			dkms remove -m "$pkg" -v "$v" --all
		done
	done
fi
# the source trees, also those DKMS no longer knows
for d in /usr/src/bcm4360-[0-9]* /usr/src/bcm4360-open-[0-9]*; do
	[ -d "$d" ] && rm -rf "$d"
done
rm -f /etc/modprobe.d/bcm4360.conf /etc/modprobe.d/bcm4360-open.conf
rm -rf /lib/firmware/bcm4360
systemctl disable bcm4360-fallback.service 2>/dev/null || true
systemctl disable --now bcm4360-watchdog.service 2>/dev/null || true
systemctl disable --now bcm4360-watchdog.timer 2>/dev/null || true	# up to 1.2.1
rm -f /etc/systemd/system/bcm4360-fallback.service \
	/etc/systemd/system/bcm4360-watchdog.service /etc/systemd/system/bcm4360-watchdog.timer
rm -rf /usr/local/lib/bcm4360 /run/bcm4360-watchdog
systemctl daemon-reload
depmod -a

echo "bcm4360 removed. The previous driver takes over at the next boot;"
echo "to switch right now: sudo rmmod bcm4360; sudo modprobe wl; sudo systemctl restart wpa_supplicant"
