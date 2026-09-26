#!/bin/sh
# SPDX-License-Identifier: ISC
#
# Download Broadcom's proprietary 802.11 core from Broadcom, verify it and
# unpack it into lib/. It is linked into the module at build time and stays
# under Broadcom's license (copied to lib/LICENSE.txt); it is not part of this
# repository and is used exactly as Broadcom ships it.
#
#   scripts/fetch-blob.sh [LOCAL_TARBALL]
set -eu

cd "$(dirname "$0")/.."

TARBALL=hybrid-v35_64-nodebug-pcoem-6_30_223_271.tar.gz
URL="https://docs.broadcom.com/docs-and-downloads/docs/linux_sta/$TARBALL"
SHA256=5f79774d5beec8f7636b59c0fb07a03108eef1e3fd3245638b20858c714144be
# BLAKE2b as pinned by Arch Linux's broadcom-wl-dkms package
B2SUM=e9d01c1a1a63c07f720e3ee53ee3ef634ab12694135300cb0ce47ade0e9e0084967a0b6df64d983e8184240eb3defb128f650bddb7727e901d50315307f3398a

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

if [ $# -ge 1 ]; then
	cp "$1" "$tmp/$TARBALL"
else
	echo "Downloading $URL"
	curl -fL --retry 3 -o "$tmp/$TARBALL" "$URL"
fi

echo "$SHA256  $tmp/$TARBALL" | sha256sum -c -
if command -v b2sum >/dev/null 2>&1; then
	echo "$B2SUM  $tmp/$TARBALL" | b2sum -c -
fi

tar -xzf "$tmp/$TARBALL" -C "$tmp" lib/wlc_hybrid.o_shipped lib/LICENSE.txt
mkdir -p lib
install -m 0644 "$tmp/lib/wlc_hybrid.o_shipped" "$tmp/lib/LICENSE.txt" lib/
echo "OK: lib/wlc_hybrid.o_shipped (Broadcom 6.30.223.271, license: lib/LICENSE.txt)"
