#!/bin/sh
# Linux AppImage of the desktop program: one file that runs on most
# distributions (and on Raspberry Pi OS, 64-bit and 32-bit), with SDL2,
# art/ and music/ inside.
#   tools/package-linux.sh <build dir> <out dir>
# The build dir holds rolling_rob_sim (cmake --build). Needs linuxdeploy
# for this machine's architecture ($LINUXDEPLOY, else linuxdeploy on PATH).
# Build on an old distribution (GitHub Actions: Ubuntu 22.04): the AppImage
# needs at least the glibc it was built with.
set -e
cd "$(dirname "$0")/.."
BUILD=${1:?build dir}
OUT=${2:?out dir}
ARCH=$(uname -m)
# 32-bit ARM reports armv7l; AppImage tools (and Debian) call it armhf
case "$ARCH" in armv7l|armv8l) ARCH=armhf ;; esac
export ARCH
LINUXDEPLOY=${LINUXDEPLOY:-linuxdeploy}
APPDIR="$BUILD/AppDir"
rm -rf "$APPDIR" && mkdir -p "$APPDIR/usr/bin" "$OUT"
# The program finds art/ and music/ next to itself (main_sim.c, find_data).
cp "$BUILD/rolling_rob_sim" "$APPDIR/usr/bin/rolling-rob"
cp -R art "$APPDIR/usr/bin/art"
mkdir -p "$APPDIR/usr/bin/music" && cp -R music/mp3 "$APPDIR/usr/bin/music/mp3"
cat > "$BUILD/rolling-rob.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Rolling Rob
Comment=Roll Rob down the hills, and a few more games
Exec=rolling-rob
Icon=rolling-rob
Categories=Game;ArcadeGame;
Terminal=false
DESKTOP
cp web/icon-512.png "$BUILD/rolling-rob.png"
# APPIMAGE_EXTRACT_AND_RUN: linuxdeploy is an AppImage itself, and CI has no FUSE
OUTPUT="$OUT/Rolling-Rob-Linux-$ARCH.AppImage" APPIMAGE_EXTRACT_AND_RUN=1 "$LINUXDEPLOY" \
  --appdir "$APPDIR" --executable "$APPDIR/usr/bin/rolling-rob" \
  --desktop-file "$BUILD/rolling-rob.desktop" --icon-file "$BUILD/rolling-rob.png" --output appimage
ls -l "$OUT/Rolling-Rob-Linux-$ARCH.AppImage"
