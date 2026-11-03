#!/bin/sh
# macOS app of the desktop program: "Rolling Rob.app", universal (Apple
# silicon and Intel), SDL2 linked in, art/ and music/ in Contents/Resources,
# zipped. Not notarized: signed ad hoc only, so the first start needs a
# right-click -> Open (or System Settings -> Privacy & Security -> Open).
#   tools/package-macos.sh <build dir> <out dir> [version]
# The build dir holds rolling_rob_sim, built universal against a universal
# static SDL2:
#   cmake -S . -B build-mac -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
#         -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DCMAKE_PREFIX_PATH=<SDL2> -DROLLING_ROB_SDL2_STATIC=ON
set -e
cd "$(dirname "$0")/.."
BUILD=${1:?build dir}
OUT=$(mkdir -p "${2:?out dir}" && cd "$2" && pwd)
VERSION=${3:-dev}
SHORT=$(echo "$VERSION" | sed -n 's/^v\{0,1\}\([0-9][0-9.]*\).*/\1/p')
SHORT=${SHORT:-0.0}
APP="$BUILD/package/Rolling Rob.app"
rm -rf "$BUILD/package" && mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/music"
cp "$BUILD/rolling_rob_sim" "$APP/Contents/MacOS/RollingRob"
strip -x "$APP/Contents/MacOS/RollingRob"
# The program finds art/ and music/ in Contents/Resources (main_sim.c,
# find_data: SDL_GetBasePath of an app is its Resources folder).
cp -R art "$APP/Contents/Resources/art"
cp -R music/mp3 "$APP/Contents/Resources/music/mp3"
ICONSET="$BUILD/RollingRob.iconset"
rm -rf "$ICONSET" && mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  sips -z $s $s web/icon-512.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  d=$((s * 2)); [ $d -le 512 ] && sips -z $d $d web/icon-512.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/RollingRob.icns"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Rolling Rob</string>
  <key>CFBundleDisplayName</key><string>Rolling Rob</string>
  <key>CFBundleIdentifier</key><string>io.github.advies-ruit.rolling-rob</string>
  <key>CFBundleExecutable</key><string>RollingRob</string>
  <key>CFBundleIconFile</key><string>RollingRob</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$SHORT</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.arcade-games</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
codesign --force --deep --sign - "$APP"
lipo -archs "$APP/Contents/MacOS/RollingRob"
rm -f "$OUT/Rolling-Rob-macOS.zip"
ditto -c -k --keepParent "$APP" "$OUT/Rolling-Rob-macOS.zip"
ls -l "$OUT/Rolling-Rob-macOS.zip"
