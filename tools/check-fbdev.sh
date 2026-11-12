#!/bin/sh
# The desktop program on a Linux device with only /dev/fb0, without one: a
# plain file plays the framebuffer and a FIFO the keyboard (tests/fakefb.c,
# LD_PRELOAD). Checks, for 480x272 and 1280x720 at 32 bits and 480x272 at
# 16 bits, 1280x720 at 32 and 16 bits with the picture drawn at 480x272 and
# scaled to fill it (--scale 1), 1280x720 at two whole times
# (ROLLING_ROB_FB_ZOOM=2), 1024x768 (another shape: black bars), and once with a monotonic clock 29 days on: the bot plays two levels on it as it does headless (same times),
# every pixel of a 32-bit framebuffer is opaque (alpha 0xFF), the picture
# sits where the log says (in the middle) with black around it, and the keyboard is found. That
# SIGTERM ends it, and that without any screen the program says so and stops.
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
for mode in 480x272x32 1280x720x32 480x272x16 1280x720x32@s1 1280x720x16@s1 1280x720x32@z2 1024x768x32@s1 480x272x32@up; do
  up=; scale=; zoom=; case $mode in *@up) up=2530000000; mode=${mode%@up} ;; *@s1) scale=1; mode=${mode%@s1} ;; *@z2) zoom=2; scale=1; mode=${mode%@z2} ;; esac
  w=${mode%%x*}; rest=${mode#*x}; h=${rest%%x*}; bpp=${rest#*x}
  rm -f "$T/fb" && truncate -s $((w * h * 2 * bpp / 8)) "$T/fb"
  # a writer keeps the keyboard FIFO open; it presses nothing
  sleep 60 > "$T/kbd" &
  writer=$!
  env -u DISPLAY -u WAYLAND_DISPLAY HOME="$T" ROLLING_ROB_FBDEV="$T/fb" FAKEFB="$T/fb" FAKEFB_MODE=$mode \
    ${up:+FAKE_MONOTONIC_MS=$up} ${zoom:+ROLLING_ROB_FB_ZOOM=$zoom} \
    FAKEKBD="$T/kbd" LD_PRELOAD="$T/fakefb.so" timeout 120 "$PROG" ${scale:+--scale $scale} --bot --bot-levels 2 --seed 6 > "$T/out.txt" 2>&1 \
    || { cat "$T/out.txt"; kill $writer; exit 1; }
  kill $writer 2>/dev/null || true
  echo "== $mode${up:+, up ${up} ms}${scale:+, --scale $scale}${zoom:+, ROLLING_ROB_FB_ZOOM=$zoom}"
  grep -E '^(video|keyboard|total|fbdev: [0-9])' "$T/out.txt"
  got=$(grep '^total' "$T/out.txt")
  [ "$got" = "$want" ] || { echo "check-fbdev: $mode: not as headless"; exit 1; }
  grep -q '^keyboard: /dev/input/event0' "$T/out.txt" || { echo "check-fbdev: $mode: no keyboard"; exit 1; }
  grep -q 'art: level 0 landscape' "$T/out.txt" || { echo "check-fbdev: $mode: no painted art"; exit 1; }
  # where the picture is shown, from the log: "shown WxH at X,Y"
  shown=$(sed -n 's/^video: .*, shown \([0-9]*\)x\([0-9]*\) at \([0-9]*\),\([0-9]*\).*/\1 \2 \3 \4/p' "$T/out.txt")
  [ -n "$shown" ] || { echo "check-fbdev: $mode: no 'shown' in the log"; exit 1; }
  python3 -I - "$T/fb" "$w" "$h" "$bpp" $shown "$mode" "$zoom" <<'PY'
import sys
path, w, h, bpp, pw, ph, ox, oy = sys.argv[1], *map(int, sys.argv[2:9])
mode, zoom = sys.argv[9], sys.argv[10]
d = open(path, 'rb').read()
bp = bpp // 8
# what it should be: whole times with ROLLING_ROB_FB_ZOOM, else filling the
# framebuffer, the shape kept where it is more than 1 % off
if zoom:
    k = min(int(zoom), w // 480, h // 272)
    want = None if w == 480 else (480 * k, 272 * k)
elif abs(w * 272 - h * 480) * 100 <= min(w * 272, h * 480):
    want = (w, h)
elif w * 272 < h * 480:
    want = (w, w * 272 // 480)
else:
    want = (h * 480 // 272, h)
if want and (pw, ph) != want: sys.exit("check-fbdev: %s: shown %dx%d, not %dx%d" % (mode, pw, ph, *want))
if (ox, oy) != ((w - pw) // 2, (h - ph) // 2): sys.exit("check-fbdev: %s: not in the middle" % mode)
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

# stopped from outside (kill, timeout, Ctrl+C on a console where it has no
# window): it ends, within a few seconds. Found as /dev/fb0, as on a device:
# SDL has looked for a screen first (it took SIGINT and SIGTERM then)
rm -f "$T/fb" && truncate -s $((1280 * 720 * 2 * 4)) "$T/fb"
sleep 60 > "$T/kbd" &
writer=$!
env -u DISPLAY -u WAYLAND_DISPLAY -u ROLLING_ROB_FBDEV HOME="$T" FAKEFB="$T/fb" FAKEFB_AS_FB0=1 FAKEFB_MODE=1280x720x32 \
  FAKEKBD="$T/kbd" LD_PRELOAD="$T/fakefb.so" "$PROG" --bot > "$T/out.txt" 2>&1 &
p=$!
sleep 5
kill -TERM $p
i=0
while kill -0 $p 2>/dev/null && [ $i -lt 10 ]; do sleep 1; i=$((i + 1)); done
kill $writer 2>/dev/null || true
if kill -0 $p 2>/dev/null; then kill -9 $p; cat "$T/out.txt"; echo "check-fbdev: SIGTERM did not end it"; exit 1; fi
grep -q '^video: /dev/fb0' "$T/out.txt" || { cat "$T/out.txt"; echo "check-fbdev: did not run on /dev/fb0"; exit 1; }
echo "check-fbdev: on /dev/fb0, SIGTERM ended it within ${i} s"

# no screen and no framebuffer: a message, and exit 1
if env -u DISPLAY -u WAYLAND_DISPLAY ROLLING_ROB_FBDEV=/nonexistent "$PROG" > "$T/out.txt" 2>&1; then
  echo "check-fbdev: ran without a screen"; exit 1
fi
grep -q 'no screen to show the game on' "$T/out.txt" || { cat "$T/out.txt"; exit 1; }
echo "check-fbdev: without a screen: $(grep 'no screen' "$T/out.txt")"
echo "check-fbdev: all good"
