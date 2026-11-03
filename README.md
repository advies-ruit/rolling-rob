# Rolling Rob

A port of the original [Rolling Rob](https://vader.nl/rolling-rob.html) to C, with
LVGL and SDL2, plus a small app around it: Tetris, 2048, Snake, Breakout,
Minesweeper, fractals and a 3D world. It runs in the browser (WebAssembly) and
as a desktop program.

Play it at https://advies-ruit.github.io/rolling-rob/ -- it also installs as
an app and works offline.

![Rob rolling through level 4, Boulder Boulevard, in the painted look](docs/speedrun.webp)

<p>
  <img src="docs/menu.png" width="32%" alt="The menu: Rolling Rob, Tetris, 2048, 3D world, Snake, Breakout, Mines, fractals">
  <img src="docs/levels.png" width="32%" alt="Choosing one of the ten levels">
  <img src="docs/level-4.webp" width="32%" alt="Level 4 in the painted look">
</p>

## Level 11

Levels 1-10 are the original's. Level 11, Beyond the Summit, is a bonus
level of our own: it continues their curve (longer, steeper, more
obstacles) in the space theme at night. The run (the total time of levels
1-10 in a row) stays the original's ten levels. Its picker thumbnail comes
from `tools/level-thumb.py`.

Its painted look has pictures of its own (a starry sky, alien ground, a
glowing space plant and an alien). Should one of the four be missing or
unreadable, the level draws level 6's space set instead:

| File | Size | Format |
|---|---|---|
| `bg-10.jpg` | 560 x 373 | baseline JPEG (not progressive) |
| `tex-10.jpg` | 256 x 256 | baseline JPEG; the game mirrors it, so its edges should meet |
| `tree-10.png` | 105 x 105 | PNG with transparency |
| `enemy-10.png` | 33 x 33 | PNG with transparency |

A set that is missing or broken only affects its own level (`art_set()`
and `s_fallback[]` in `src/game/art.c`). The ground's lip colour for the
set is `s_accent[10]`, a teal that matches its plants.

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

## Tests

The rules of the games (Tetris, 2048, Snake, Breakout, Minesweeper, the
fractals) have unit tests in `tests/`. They need neither LVGL nor SDL:

    cmake -S tests -B build-tests && cmake --build build-tests -j
    ctest --test-dir build-tests --output-on-failure

They also come along with the desktop build (`ctest --test-dir build`).
GitHub Actions (`.github/workflows/ci.yml`) runs them on every push and pull
request, with AddressSanitizer and UBSan, builds the desktop program on Linux
and lets the bot play all eleven levels without a window.

The site is built by GitHub Actions from this repository
(`.github/workflows/pages.yml`). Every tag `v*` becomes a release (the site as
a zip, with the SHA-256 of its files) and the site is published from all
releases: the newest at the top, each one also under `v/<tag>/`, so the
start screen can switch to an older version and back.

A version can be taken off the site: add its tag to `RETIRED` in
`tools/assemble-site.sh` (v1.2 is: v1.3 is the same game with a fix for
switching versions on iOS). Its folder then holds a farewell
(`web/retired/`): a page that sends to the newest version and forgets the
choice, and a service worker that removes the version from devices that
had it. The newest release is never retired.

Which build is running: the app's Info page shows the version (the
release tag), when it was built and its commit; the start screen shows the
version and the build time.

## Licences

- The game code: licence not chosen yet (until then all rights reserved).
- The art (`art/`) and the music (`music/`): all rights reserved.
- LVGL (`third_party/lvgl`): MIT. miniaudio: public domain / MIT-0. SDL2
  (through Emscripten or the system): zlib. The Montserrat font in LVGL: OFL.
