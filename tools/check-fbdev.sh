#!/bin/sh
# The desktop program on a Linux device with only /dev/fb0, without one: a
# plain file plays the framebuffer and a FIFO the keyboard (tests/fakefb.c,
# LD_PRELOAD). Checks, for 480x272 and 1280x720 at 32 bits and 480x272 at
# 16 bits, 1280x720 at 32 and 16 bits with the picture drawn at 480x272 and
# zoomed (--scale 1), and once with a monotonic clock 29 days on: the bot plays two levels on it as it does headless (same times),
# every pixel of a 32-bit framebuffer is opaque (alpha 0xFF), the picture
# sits in the middle with black around it, and the keyboard is found. And
# that without any screen the program says so and stops.
#   tools/check-fbdev.sh <rolling_rob_sim> (from the source tree: art/)
set -eu
PROG=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cc -shared -fPIC -o "$T/fakefb.so" tests/fakefb.c -ldl
mkfifo "$T/kbd"

want=$(HOME=$T "$PROG" --headless --bot --bot-levels 2 --seed 6 --frames 2000 --out "$T/h.raw" 2>&1 | grep '^total')
echo "headless: $want"

# the last one as on a device up for 29 days: its monotonic clock in ms no
# longer fits an int32 (the keyboard must still be found)
for mode in 480x272x32 1280x720x32 480x272x16 1280x720x32@s1 1280x720x16@s1 480x272x32@up; do
  up=; scale=; case $mode in *@up) up=2530000000; mode=${mode%@up} ;; *@s1) scale=1; mode=${mode%@s1} ;; esac
  w=${mode%%x*}; rest=${mode#*x}; h=${rest%%x*}; bpp=${rest#*x}
  rm -f "$T/fb" && truncate -s $((w * h * 2 * bpp / 8)) "$T/fb"
  # a writer keeps the keyboard FIFO open; it presses nothing
  sleep 60 > "$T/kbd" &
  writer=$!
  env -u DISPLAY -u WAYLAND_DISPLAY HOME="$T" ROLLING_ROB_FBDEV="$T/fb" FAKEFB="$T/fb" FAKEFB_MODE=$mode \
    ${up:+FAKE_MONOTONIC_MS=$up} \
    FAKEKBD="$T/kbd" LD_PRELOAD="$T/fakefb.so" timeout 120 "$PROG" ${scale:+--scale $scale} --bot --bot-levels 2 --seed 6 > "$T/out.txt" 2>&1 \
    || { cat "$T/out.txt"; kill $writer; exit 1; }
  kill $writer 2>/dev/null || true
  echo "== $mode${up:+, up ${up} ms}${scale:+, --scale $scale}"
  grep -E '^(video|keyboard|total|fbdev: [0-9])' "$T/out.txt"
  got=$(grep '^total' "$T/out.txt")
  [ "$got" = "$want" ] || { echo "check-fbdev: $mode: not as headless"; exit 1; }
  grep -q '^keyboard: /dev/input/event0' "$T/out.txt" || { echo "check-fbdev: $mode: no keyboard"; exit 1; }
  grep -q 'art: level 0 landscape' "$T/out.txt" || { echo "check-fbdev: $mode: no painted art"; exit 1; }
  python3 -I - "$T/fb" "$w" "$h" "$bpp" <<'PY'
import sys
path, w, h, bpp = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
d = open(path, 'rb').read()
bp = bpp // 8
k = min(w // 480, h // 272)
pw, ph = 480 * k, 272 * k
ox, oy = (w - pw) // 2, (h - ph) // 2
def px(x, y):
    o = (y * w + x) * bp
    return d[o:o + bp]
if bpp == 32:
    bad = sum(1 for i in range(3, w * h * 4, 4) if d[i] != 0xFF)
    if bad: sys.exit("check-fbdev: %d pixels not opaque" % bad)
lit = sum(1 for y in range(oy, oy + ph, 4) for x in range(ox, ox + pw, 4) if any(px(x, y)[:3]))
if lit < (pw // 4) * (ph // 4) // 2: sys.exit("check-fbdev: the picture is mostly black")
if ox or oy:
    around = [px(x, y) for y in range(0, h, 8) for x in range(0, w, 8)
              if not (ox <= x < ox + pw and oy <= y < oy + ph)]
    if any(p[:3] != b'\0\0\0'[:min(3, bp)] for p in around): sys.exit("check-fbdev: not black around the picture")
print("check-fbdev: %dx%d at %d bits: picture %dx%d at %d,%d%s" % (w, h, bpp, pw, ph, ox, oy, ", every pixel opaque" if bpp == 32 else ""))
PY
done

# no screen and no framebuffer: a message, and exit 1
if env -u DISPLAY -u WAYLAND_DISPLAY ROLLING_ROB_FBDEV=/nonexistent "$PROG" > "$T/out.txt" 2>&1; then
  echo "check-fbdev: ran without a screen"; exit 1
fi
grep -q 'no screen to show the game on' "$T/out.txt" || { cat "$T/out.txt"; exit 1; }
echo "check-fbdev: without a screen: $(grep 'no screen' "$T/out.txt")"
echo "check-fbdev: all good"
