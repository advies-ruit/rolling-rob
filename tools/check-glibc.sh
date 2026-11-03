#!/bin/sh
# The newest glibc a package needs: the highest GLIBC_x.y version any of its
# programs and libraries asks for. A Linux program runs only where the
# system's glibc is at least that (glibc is never part of a package).
#   tools/check-glibc.sh <file or directory> <highest allowed, e.g. 2.31>
# Lists what each ELF file needs and fails above the limit. Needs objdump.
set -eu
WHERE=${1:?file or directory}
MAX=${2:?highest allowed glibc}
newest=0
for f in $(find "$WHERE" -type f); do
  head -c 4 "$f" 2>/dev/null | grep -q 'ELF' || continue
  need=$(objdump -T "$f" 2>/dev/null | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -n 1)
  [ -n "$need" ] || continue
  echo "$f: glibc $need"
  newest=$(printf '%s\n%s\n' "$newest" "$need" | sort -V | tail -n 1)
done
echo "needs glibc $newest (allowed: $MAX)"
if [ "$(printf '%s\n%s\n' "$newest" "$MAX" | sort -V | tail -n 1)" != "$MAX" ]; then
  echo "check-glibc: needs a newer glibc than $MAX"
  exit 1
fi
