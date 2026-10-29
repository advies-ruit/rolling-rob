# Rolling Rob

A port of the original [Rolling Rob](https://vader.nl/rolling-rob.html) to C, with
LVGL and SDL2, plus a small app around it: Tetris, 2048, Snake, Breakout,
Minesweeper, fractals and a 3D world. It runs in the browser (WebAssembly) and
as a desktop program.

Play it at https://advies-ruit.github.io/rolling-rob/ -- it also installs as
an app and works offline.

## How it was made

A port of the original Rolling Rob (vader.nl) to C with LVGL, written with the
help of an AI coding assistant (Claude). The painted landscapes were generated
with an image model and the music with Suno; both were then prepared for the
game (resized, compressed, metadata removed). The web version is built from
this source by GitHub Actions.

## Building

    git clone --recursive <this repository>
    tools/build-web.sh                 # needs Emscripten; the site lands in build-web/web
    python3 -m http.server -d build-web/web

Desktop (SDL2 installed): `cmake -S . -B build && cmake --build build -j --target rolling_rob_sim`.

The site is built by GitHub Actions from this repository
(`.github/workflows/pages.yml`). Every tag `v*` becomes a release (the site as
a zip, with the SHA-256 of its files) and the site is published from all
releases: the newest at the top, each one also under `v/<tag>/`, so the
start screen can switch to an older version and back.

## Licences

- The game code: licence not chosen yet (until then all rights reserved).
- The art (`art/`) and the music (`music/`): all rights reserved.
- LVGL (`third_party/lvgl`): MIT. miniaudio: public domain / MIT-0. SDL2
  (through Emscripten or the system): zlib. The Montserrat font in LVGL: OFL.
