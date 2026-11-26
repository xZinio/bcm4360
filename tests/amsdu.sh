#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# No root, hardware or Broadcom object needed.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -DBCM4360_EMU \
	-I. -Iopen/include tests/amsdu.c hybrid/amsdu.c -o "$tmp/amsdu"
"$tmp/amsdu"
