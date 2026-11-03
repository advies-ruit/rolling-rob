#!/bin/sh
# Windows x64 zip of the desktop program: RollingRob.exe (SDL2 linked in,
# nothing to install) with art/ and music/ next to it.
#   tools/package-windows.sh <build dir> <out dir>
# The build dir holds RollingRob.exe, cross-compiled with mingw-w64:
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#         -DCMAKE_PREFIX_PATH=<SDL2 mingw>/x86_64-w64-mingw32 -DROLLING_ROB_SDL2_STATIC=ON
set -e
cd "$(dirname "$0")/.."
BUILD=${1:?build dir}
OUT=$(mkdir -p "${2:?out dir}" && cd "$2" && pwd)
STRIP=${STRIP:-x86_64-w64-mingw32-strip}
DIR="$BUILD/package/Rolling Rob"
rm -rf "$BUILD/package" && mkdir -p "$DIR/music"
cp "$BUILD/RollingRob.exe" "$DIR/"
"$STRIP" "$DIR/RollingRob.exe"
cp -R art "$DIR/art"
cp -R music/mp3 "$DIR/music/mp3"
rm -f "$OUT/Rolling-Rob-Windows-x64.zip"
(cd "$BUILD/package" && zip -qr "$OUT/Rolling-Rob-Windows-x64.zip" "Rolling Rob")
ls -l "$OUT/Rolling-Rob-Windows-x64.zip"
