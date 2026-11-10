#!/bin/sh
# The comments cite the specifications this code was written from
# (docs/re/spec/frames.md, or just frames.md, docs/re/tasks/..., docs/re/questions/...).
# Those files left the tree in 1.1.1 and are read in the history, at commit
# 6a58d4e (README, "How it is made"). Every one cited must be there.
set -eu

cd "$(dirname "$0")/.."
specs=6a58d4e
git cat-file -e "$specs^{commit}" 2>/dev/null ||
	{ echo "references: commit $specs is not in this clone (shallow?)" >&2; exit 1; }

in_history() { git cat-file -e "$specs:$1" 2>/dev/null; }

bad=0 n=0
for p in $(git grep -ohE 'docs/re/[A-Za-z0-9_./-]+\.md' -- . ':!tests/' ':!README.md' | sort -u); do
	n=$((n + 1))
	in_history "$p" || { echo "not in $specs: $p"; bad=1; }
done
# bare names (frames.md 3.2): a file of spec/, tasks/, questions/ or docs/re/
for f in $(git grep -ohE '\b[A-Za-z0-9_-]+\.md\b' -- '*.c' '*.h' '*.mk' '*.py' '*.sh' '*.txt' Makefile ':!tests/' |
	sort -u); do
	n=$((n + 1))
	in_history "docs/re/spec/$f" || in_history "docs/re/tasks/$f" ||
		in_history "docs/re/questions/$f" || in_history "docs/re/$f" ||
		{ echo "not in $specs: $f"; bad=1; }
done
[ "$bad" -eq 0 ] && echo "references: all $n cited files are in $specs"
exit "$bad"
