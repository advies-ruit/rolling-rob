#!/bin/sh
# Linux AppImage of the desktop program: one file with art/ and music/
# inside. The program is built by tools/build-linux.sh (Debian 11, SDL2
# linked in), so it needs only glibc 2.31 or newer from the system;
# nothing else is bundled. Also the same as a plain tar.gz, for a device
# without FUSE (an AppImage there unpacks itself again at every start),
# such as a Linux device with only /dev/fb0.
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

# the same files as a tar.gz: Rolling-Rob/ with the program, art/, music/
TAR="$BUILD/tar/Rolling-Rob"
rm -rf "$BUILD/tar" && mkdir -p "$TAR/music"
cp "$APPDIR/usr/bin/rolling-rob" "$TAR/rolling-rob"
cp -R art "$TAR/art"
cp -R music/mp3 "$TAR/music/mp3"
cat > "$TAR/README.txt" <<'README'
Rolling Rob -- https://github.com/advies-ruit/rolling-rob

Start: ./rolling-rob   (needs glibc 2.31 or newer)

It shows the game on an X11 or Wayland desktop, with KMS/DRM without one,
or else on the framebuffer (/dev/fb0, the user in the group video; keys from
a keyboard and a touchscreen in /dev/input, the group input).

On a framebuffer:
  ROLLING_ROB_FBDEV=/dev/fb1   another framebuffer (or straight to one)
  ROLLING_ROB_ROTATE=180       the picture (and touch) turned round
  ROLLING_ROB_TOUCH_FLIP=1     only the touchscreen turned round
  ROLLING_ROB_FB_ZOOM=1        the picture not zoomed to fill the screen
  --scale 1..4                 the picture 480x272 times 1..4
  --bot                        the bot plays all levels and says how it went
README
tar -C "$BUILD/tar" -czf "$OUT/Rolling-Rob-Linux-$ARCH.tar.gz" --owner=0 --group=0 Rolling-Rob
ls -l "$OUT/Rolling-Rob-Linux-$ARCH.tar.gz"
