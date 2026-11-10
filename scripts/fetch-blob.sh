#!/bin/sh
# Download Broadcom's 802.11 core, verify it and unpack it into lib/. It is
# linked into the module at build time; it is not part of this repository and
# is used exactly as Broadcom ships it.
#
#   scripts/fetch-blob.sh [LOCAL_TARBALL]
#   scripts/fetch-blob.sh --check    exit 0 if lib/ holds the right object
set -eu

cd "$(dirname "$0")/.."

TARBALL=hybrid-v35_64-nodebug-pcoem-6_30_223_271.tar.gz
# Broadcom's own download first, then a copy of the same file at the Internet
# Archive. Any source is safe: the checksums below decide, not the source.
URLS="https://docs.broadcom.com/docs-and-downloads/docs/linux_sta/$TARBALL
https://web.archive.org/web/2024id_/https://docs.broadcom.com/docs-and-downloads/docs/linux_sta/$TARBALL"
SHA256=5f79774d5beec8f7636b59c0fb07a03108eef1e3fd3245638b20858c714144be
# BLAKE2b as pinned by Arch Linux's broadcom-wl-dkms package
B2SUM=e9d01c1a1a63c07f720e3ee53ee3ef634ab12694135300cb0ce47ade0e9e0084967a0b6df64d983e8184240eb3defb128f650bddb7727e901d50315307f3398a
# the object in it (scripts/bcm4360-fwcut.py checks the same)
OBJECT=lib/wlc_hybrid.o_shipped
OBJECT_SHA256=352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743

sha256() { sha256sum "$1" | cut -d' ' -f1; }

object_ok() {
	[ -f "$1" ] && [ "$(sha256 "$1")" = "$OBJECT_SHA256" ]
}

if [ "${1:-}" = --check ]; then
	object_ok "$OBJECT"
	exit
fi

tarball_ok() {
	if [ "$(sha256 "$1")" != "$SHA256" ]; then
		echo "$1: wrong SHA-256" >&2
		return 1
	fi
	if command -v b2sum >/dev/null 2>&1 &&
		[ "$(b2sum "$1" | cut -d' ' -f1)" != "$B2SUM" ]; then
		echo "$1: wrong BLAKE2b" >&2
		return 1
	fi
	echo "$TARBALL: checksums OK"
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

if [ $# -ge 1 ]; then
	cp "$1" "$tmp/$TARBALL"
	tarball_ok "$tmp/$TARBALL" || exit 1
else
	command -v curl >/dev/null 2>&1 || { echo "fetch-blob: curl is missing" >&2; exit 1; }
	ok=
	for url in $URLS; do
		echo "Downloading $url"
		if curl -fL --retry 3 -o "$tmp/$TARBALL" "$url" && tarball_ok "$tmp/$TARBALL"; then
			ok=1
			break
		fi
		echo "not usable; trying the next source" >&2
	done
	[ -n "$ok" ] || { echo "fetch-blob: no source gave Broadcom's file" >&2; exit 1; }
fi

tar -xzf "$tmp/$TARBALL" -C "$tmp" "$OBJECT"
object_ok "$tmp/$OBJECT" || { echo "fetch-blob: $OBJECT in the tarball: wrong SHA-256" >&2; exit 1; }
mkdir -p lib
install -m 0644 "$tmp/$OBJECT" lib/
echo "OK: $OBJECT (Broadcom 6.30.223.271)"
