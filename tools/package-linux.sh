#!/bin/sh
# Linux AppImage of the desktop program: one file with art/ and music/
# inside. The program is built by tools/build-linux.sh (Debian 11, SDL2
# linked in), so it needs only glibc 2.31 or newer from the system;
# nothing else is bundled.
#   tools/package-linux.sh <build dir> <out dir> <arch: x86_64, aarch64 or armhf>
# Needs appimagetool for the machine this runs on ($APPIMAGETOOL) and the
# AppImage runtime for the target arch ($RUNTIME): the static type-2 runtime
# from github.com/AppImage/type2-runtime, which needs no libfuse on the
# player's system.
set -eu
cd "$(dirname "$0")/.."
BUILD=${1:?build dir}
OUT=$(mkdir -p "${2:?out dir}" && cd "$2" && pwd)
ARCH=${3:?arch}
: "${APPIMAGETOOL:?APPIMAGETOOL}" "${RUNTIME:?RUNTIME}"
APPDIR="$BUILD/AppDir"
rm -rf "$APPDIR" && mkdir -p "$APPDIR/usr/bin/music"
# The program finds art/ and music/ next to itself (main_sim.c, find_data).
cp "$BUILD/rolling_rob_sim" "$APPDIR/usr/bin/rolling-rob"
cp -R art "$APPDIR/usr/bin/art"
cp -R music/mp3 "$APPDIR/usr/bin/music/mp3"
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
exec "$HERE/usr/bin/rolling-rob" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"
cat > "$APPDIR/rolling-rob.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Rolling Rob
Comment=Roll Rob down the hills, and a few more games
Exec=rolling-rob
Icon=rolling-rob
Categories=Game;ArcadeGame;
Terminal=false
DESKTOP
cp web/icon-512.png "$APPDIR/rolling-rob.png"
cp web/icon-512.png "$APPDIR/.DirIcon"
# APPIMAGE_EXTRACT_AND_RUN: appimagetool is an AppImage itself, and CI has no FUSE
ARCH="$ARCH" APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" --no-appstream --runtime-file "$RUNTIME" \
  "$APPDIR" "$OUT/Rolling-Rob-Linux-$ARCH.AppImage"
ls -l "$OUT/Rolling-Rob-Linux-$ARCH.AppImage"
