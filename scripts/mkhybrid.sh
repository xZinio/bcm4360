#!/bin/sh
# Prepare Broadcom's core for linking with the open code that takes over parts
# of it ("hybrid" build, see hybrid/README.md).
#
#   scripts/mkhybrid.sh IN.o OUT.o LIST
#
# IN.o is lib/wlc_hybrid.o_shipped, OUT.o the copy that is linked into the
# module. LIST (hybrid/takeover.txt) names symbols of the object:
#
#   take NAME    a function the open code takes over. NAME becomes a weak
#                global symbol, so that the open definition of NAME wins at
#                link time and every call of the object to NAME goes to it;
#                Broadcom's own body stays reachable as bcm4360_blob_NAME.
#   use NAME     a function or data object of the object that the open code
#                calls or reads. NAME becomes a global symbol, unchanged.
#   name NAME SECTION OFFSET
#                a data object of the object that has no symbol (a static
#                variable of Broadcom's), at OFFSET in SECTION: a new global
#                symbol NAME is added there, so that an adapter can pass its
#                address. The object's code still reaches it as before.
#
# Nothing else changes: the code and data of the object are copied as they
# are, only the binding of these symbols differs. The script runs where the
# module is built (DKMS) and needs only nm and objcopy from binutils.
set -eu

[ $# -eq 3 ] || { echo "usage: $0 IN.o OUT.o LIST" >&2; exit 1; }
in=$1
out=$2
list=$3
NM=${NM:-nm}
OBJCOPY=${OBJCOPY:-objcopy}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

# defined symbols of the object: offset, type letter, name
"$NM" "$in" | awk 'NF == 3 { print $1, $2, $3 }' > "$tmp/syms"

# the list without carriage returns (a checkout on Windows may have CRLF)
tr -d '\r' < "$list" > "$tmp/list"

# objcopy weakens a symbol only if it is global already when it gets there,
# and it globalizes after it weakens: two passes, globalize then weaken
args=$tmp/args
weak=$tmp/weak
: > "$args"
: > "$weak"
n_take=0
n_use=0
n_name=0
while read -r kind name rest; do
	case $kind in
	''|'#'*) continue ;;
	esac
	[ -n "$name" ] || { echo "$list: '$kind' without a name" >&2; exit 1; }
	count=$(awk -v n="$name" '$3 == n' "$tmp/syms" | wc -l)
	if [ "$kind" = name ]; then
		# shellcheck disable=SC2086
		set -- $rest
		if [ $# -ne 2 ]; then
			echo "$list: name $name: need SECTION OFFSET" >&2
			exit 1
		fi
		if [ "$count" -ne 0 ]; then
			echo "$list: name $name: already defined in $in" >&2
			exit 1
		fi
		printf '%s\n' "--add-symbol" "$name=$1:$2,global,object" >> "$args"
		n_name=$((n_name + 1))
		continue
	fi
	if [ "$count" -ne 1 ]; then
		echo "$list: $name: $count definitions in $in (need exactly 1)" >&2
		exit 1
	fi
	type=$(awk -v n="$name" '$3 == n { print $2 }' "$tmp/syms")
	case $kind in
	take)
		case $type in
		t|T) ;;
		*) echo "$list: take $name: not a function (nm type $type)" >&2; exit 1 ;;
		esac
		off=$(awk -v n="$name" '$3 == n { print $1 }' "$tmp/syms")
		printf '%s\n' "--add-symbol" "bcm4360_blob_$name=.text:0x$off,global,function" \
			"--globalize-symbol=$name" >> "$args"
		printf '%s\n' "--weaken-symbol=$name" >> "$weak"
		n_take=$((n_take + 1))
		;;
	use)
		printf '%s\n' "--globalize-symbol=$name" >> "$args"
		n_use=$((n_use + 1))
		;;
	*)
		echo "$list: unknown kind '$kind'" >&2
		exit 1
		;;
	esac
done < "$tmp/list"

# the argument lists have no spaces in their words
# shellcheck disable=SC2046
"$OBJCOPY" $(cat "$args") "$in" "$out.tmp1"
if [ -s "$weak" ]; then
	# shellcheck disable=SC2046
	"$OBJCOPY" $(cat "$weak") "$out.tmp1" "$out.tmp2"
	rm -f "$out.tmp1"
else
	mv "$out.tmp1" "$out.tmp2"
fi
# every taken function must have come out weak
for name in $(sed -n 's/^--weaken-symbol=//p' "$weak"); do
	if ! "$NM" "$out.tmp2" | awk -v n="$name" '$3 == n && $2 == "W" { f = 1 } END { exit !f }'; then
		echo "mkhybrid: $name did not become weak" >&2
		rm -f "$out.tmp2"
		exit 1
	fi
done
mv "$out.tmp2" "$out"
echo "mkhybrid: $n_take functions taken over, $n_use symbols used by the open code, $n_name data objects named"
