#!/bin/sh
# Builds the desktop program for the Linux packages, inside a Debian 11
# ("bullseye") container of the target architecture (packages.yml): a
# program built against glibc 2.31 runs on every system with that or newer
# -- Debian 11+, Ubuntu 20.04+, Raspberry Pi OS 11 ("Bullseye") and newer.
# SDL2 is built from source and linked in (Debian 11's own is old); it
# loads X11, Wayland, KMS/DRM, ALSA and PulseAudio from the system when it
# runs, so the program needs nothing beyond glibc.
#   tools/build-linux.sh <build dir>
# Environment: SDL2_VERSION (e.g. 2.32.10), RR_VERSION (the release tag, or
# empty for git describe).
set -eu
cd "$(dirname "$0")/.."
BUILD=${1:?build dir}
: "${SDL2_VERSION:?SDL2_VERSION}"
sh tools/debian-apt.sh \
  build-essential cmake git curl ca-certificates pkg-config file binutils \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev \
  libxxf86vm-dev libxkbcommon-dev libwayland-dev wayland-protocols \
  libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev \
  libasound2-dev libpulse-dev libudev-dev libdbus-1-dev
git config --global --add safe.directory "$PWD"
git config --global --add safe.directory "$PWD/third_party/lvgl"

SDL_SRC=/tmp/SDL2-$SDL2_VERSION
SDL=/opt/sdl2
curl -sSfL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz" | tar -xz -C /tmp
cmake -S "$SDL_SRC" -B /tmp/sdl-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SDL" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF >/dev/null
cmake --build /tmp/sdl-build -j"$(nproc)" >/dev/null
cmake --install /tmp/sdl-build >/dev/null
# what this SDL can drive (it picks one at run time)
grep -E 'SDL_VIDEO_DRIVER_(X11|WAYLAND|KMSDRM)|SDL_AUDIO_DRIVER_(ALSA|PULSEAUDIO)' "$SDL/include/SDL2/SDL_config.h" || true

cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$SDL" -DROLLING_ROB_SDL2_STATIC=ON \
  -DROLLING_ROB_TESTS=OFF -DROLLING_ROB_VERSION="${RR_VERSION:-}"
cmake --build "$BUILD" -j"$(nproc)"
strip "$BUILD/rolling_rob_sim"
