#!/usr/bin/env python3
"""Renders a level thumbnail for the level picker (src/game/level_thumbs.c).

    tools/level-thumb.py BUILD_DIR LEVEL [--png out.png]

Runs the headless simulator (BUILD_DIR/rolling_rob_sim) from the repository
root: LEVEL (1-based) in the original look, frame 40, no HUD and no touch
buttons, seed 1, then scales the 480x272 frame down to 108x61 (bilinear)
and prints it as a C array of B,G,R bytes named thumb_<LEVEL-1>_map, to
paste into level_thumbs.c (and its entry into rr_level_thumbs[]).
Needs Pillow.
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image

W, H, TW, TH = 480, 272, 108, 61


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    build, level = sys.argv[1], int(sys.argv[2])
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with tempfile.TemporaryDirectory() as tmp:
        raw = os.path.join(tmp, "frame.raw")
        subprocess.run([os.path.abspath(os.path.join(build, "rolling_rob_sim")), "--headless", "--no-hud",
                        "--no-touch-ui", "--seed", "1", "--frames", "40", "--level", str(level), "--out", raw],
                       cwd=root, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(raw, "rb") as f:
            frame = Image.frombytes("RGBA", (W, H), f.read(), "raw", "BGRA").convert("RGB")
    thumb = frame.resize((TW, TH), Image.BILINEAR)
    if "--png" in sys.argv:
        thumb.save(sys.argv[sys.argv.index("--png") + 1])
    data = thumb.tobytes("raw", "BGR")
    print("static const uint8_t thumb_%d_map[] = {" % (level - 1))
    for i in range(0, len(data), 24):
        print("    " + ",".join("0x%02X" % b for b in data[i:i + 24]) + ",")
    print("};")


if __name__ == "__main__":
    main()
