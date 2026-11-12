#!/bin/sh
# WebAssembly build: the site in $BUILD_DIR/web (index.html, index.js,
# index.wasm, index.data with the art and music, icons, manifest, sw.js).
# Needs Emscripten (the version in .github/workflows/pages.yml for the same
# bytes as the published site). Serve it over http(s), e.g.
#   python3 -m http.server -d build-web/web
set -e
cd "$(dirname "$0")/.."
BUILD_DIR=${BUILD_DIR:-build-web}
# Which build this is (the info page and the start screen): RR_VERSION is
# the release tag (GitHub Actions passes it for a tag), else git describe.
BUILT=$(date -u "+%-d %b %Y %H:%M UTC")
TAG=${RR_VERSION:-$(git describe --tags --always --dirty 2>/dev/null || echo dev)}
emcmake cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DROLLING_ROB_VERSION="$TAG" -DROLLING_ROB_BUILT="$BUILT"
# index.data is packed at link time from art/ and music/, which CMake does
# not track: remove the outputs so a changed set is always repacked.
rm -f "$BUILD_DIR"/index.html "$BUILD_DIR"/index.js "$BUILD_DIR"/index.wasm "$BUILD_DIR"/index.data
cmake --build "$BUILD_DIR" -j --target rolling_rob_sim
SITE="$BUILD_DIR/web"
rm -rf "$SITE" && mkdir -p "$SITE"
cp "$BUILD_DIR"/index.html "$BUILD_DIR"/index.js "$BUILD_DIR"/index.wasm "$BUILD_DIR"/index.data \
   web/icon-192.png web/icon-512.png web/manifest.webmanifest \
   web/card-original.webp web/card-port.webp web/card-3d.webp "$SITE/"
# the original game, unchanged (kept with the maker's permission), for the start page's first card
cp three/original.html "$SITE/original.html"
# Offline: sw.js caches these files under a version made from their contents,
# so every changed build is a new sw.js, which installed copies offer as Update.
VERSION=$(cd "$SITE" && cat index.html index.js index.wasm index.data icon-192.png icon-512.png manifest.webmanifest card-original.webp card-port.webp card-3d.webp original.html | shasum -a 256 | cut -c1-12)
sed -e "s/__VERSION__/$VERSION/" -e "s/__BUILT__/$BUILT/" -e "s/__TAG__/$TAG/" web/sw.js > "$SITE/sw.js"
echo "web $TAG, built $BUILT, cache version $VERSION"
# Rolling Rob 3D (three/, the original game with a three.js world) in 3d/,
# with its own offline copy (scope 3d/) and the same music as files. The
# start screen links to it, its menu links back (RR3D_HOME); no manifest of
# its own, so in the installed app it stays part of this one.
RR3D_SITE="$SITE/3d" RR3D_HOME=../ RR3D_MANIFEST=0 python3 three/build.py
mkdir -p "$SITE/3d/music" && cp music/mp3/*.mp3 "$SITE/3d/music/"
