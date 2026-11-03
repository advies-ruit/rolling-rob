#!/bin/sh
# Checks the output (stdout and stderr) of a headless bot run of a package
# (.github/workflows/packages.yml): every level finished, none stalled, and
# the painted art loaded from the package itself -- the first level's
# pictures, and not one file the program could not open.
#   tools/check-bot-run.sh <output file>
set -e
f=${1:?output file}
grep -Eq '([0-9]+)/\1 levels finished' "$f" || { echo "check: not all levels finished"; exit 1; }
if grep -q STALLED "$f"; then echo "check: the bot stalled"; exit 1; fi
grep -q 'art: level 0 landscape' "$f" || { echo "check: the painted art was not loaded"; exit 1; }
if grep -q 'cannot open' "$f"; then echo "check: files missing:"; grep 'cannot open' "$f"; exit 1; fi
echo "check: all levels finished, with the package's own art"
