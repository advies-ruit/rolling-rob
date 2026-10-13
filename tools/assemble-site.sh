#!/bin/sh
# The site GitHub Pages serves, from this repository's GitHub releases (each
# holds Rolling-Rob-Web.zip): the newest release at the top, every release in
# v/<tag>/, and versions.json listing them newest first for the start
# screen's version choice. Needs gh (GitHub Actions has it).
#   tools/assemble-site.sh <out dir>
set -e
OUT=${1:-site}
rm -rf "$OUT" && mkdir -p "$OUT/v"
TMP=$(mktemp -d)
TAGS=$(gh release list --limit 500 --json tagName,isDraft,isPrerelease,publishedAt \
  --jq '[.[] | select((.isDraft or .isPrerelease) | not)] | sort_by(.publishedAt) | reverse | .[].tagName')
[ -n "$TAGS" ] || { echo "no releases yet"; exit 1; }
printf '[' > "$OUT/versions.json"
first=1
for t in $TAGS; do
  case "$t" in *[!A-Za-z0-9._-]*) echo "skipping tag $t"; continue ;; esac
  gh release download "$t" --pattern Rolling-Rob-Web.zip --dir "$TMP/$t"
  (cd "$TMP/$t" && unzip -q Rolling-Rob-Web.zip)
  mkdir -p "$OUT/v/$t" && cp -R "$TMP/$t/web/." "$OUT/v/$t/"
  date=$(gh release view "$t" --json publishedAt --jq '.publishedAt[:10]')
  [ $first = 1 ] || printf ',' >> "$OUT/versions.json"
  printf '\n  {"tag": "%s", "date": "%s"}' "$t" "$date" >> "$OUT/versions.json"
  if [ $first = 1 ]; then cp -R "$TMP/$t/web/." "$OUT/"; first=0; fi
done
printf '\n]\n' >> "$OUT/versions.json"
cat "$OUT/versions.json"
