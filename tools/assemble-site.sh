#!/bin/sh
# The site GitHub Pages serves, from this repository's GitHub releases (each
# holds Rolling-Rob-Web.zip): the newest release at the top, every release in
# v/<tag>/, and versions.json listing them newest first for the start
# screen's version choice. Needs gh (GitHub Actions has it).
#   tools/assemble-site.sh <out dir>
#
# RETIRED: versions taken off the site, even while their release still
# exists. Their folder v/<tag>/ gets a farewell instead (web/retired/): a
# page that sends to the newest version and forgets the choice, and a
# service worker that removes the version from devices that had it.
# The newest release is never retired (it stays at the top until a newer
# one is out), so the list can go in before that release.
#   v1.2: the same game as v1.3, without the fix for switching versions
#         on iOS (the start screen could hang at the end of loading).
RETIRED="v1.2"
set -e
OUT=${1:-site}
rm -rf "$OUT" && mkdir -p "$OUT/v"
TMP=$(mktemp -d)
TAGS=$(gh release list --limit 500 --json tagName,isDraft,isPrerelease,publishedAt \
  --jq '[.[] | select((.isDraft or .isPrerelease) | not)] | sort_by(.publishedAt) | reverse | .[].tagName')
[ -n "$TAGS" ] || { echo "no releases yet"; exit 1; }
NEWEST=$(echo "$TAGS" | head -n 1)
GONE=""
for t in $RETIRED; do
  if [ "$t" = "$NEWEST" ]; then echo "not retiring $t: it is the newest release"; else GONE="$GONE $t"; fi
done
printf '[' > "$OUT/versions.json"
first=1
for t in $TAGS; do
  case "$t" in *[!A-Za-z0-9._-]*) echo "skipping tag $t"; continue ;; esac
  case " $GONE " in *" $t "*) echo "retired: $t"; continue ;; esac
  gh release download "$t" --pattern Rolling-Rob-Web.zip --dir "$TMP/$t"
  (cd "$TMP/$t" && unzip -q Rolling-Rob-Web.zip)
  mkdir -p "$OUT/v/$t" && cp -R "$TMP/$t/web/." "$OUT/v/$t/"
  date=$(gh release view "$t" --json publishedAt --jq '.publishedAt[:10]')
  [ $first = 1 ] || printf ',' >> "$OUT/versions.json"
  printf '\n  {"tag": "%s", "date": "%s"}' "$t" "$date" >> "$OUT/versions.json"
  if [ $first = 1 ]; then cp -R "$TMP/$t/web/." "$OUT/"; first=0; fi
done
printf '\n]\n' >> "$OUT/versions.json"
for t in $GONE; do
  mkdir -p "$OUT/v/$t"
  sed "s/__TAG__/$t/g" web/retired/index.html > "$OUT/v/$t/index.html"
  cp web/retired/sw.js "$OUT/v/$t/sw.js"
  # its 3D page (since v2.0) had a worker of its own: retire that one too
  mkdir -p "$OUT/v/$t/3d"
  sed "s/__TAG__/$t/g; s|\.\./\.\./|../../../|g" web/retired/index.html > "$OUT/v/$t/3d/index.html"
  cp web/retired/sw.js "$OUT/v/$t/3d/sw.js"
done
cat "$OUT/versions.json"
