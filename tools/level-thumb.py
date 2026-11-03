#!/usr/bin/env python3
"""Renders a level thumbnail for the level picker (src/game/level_thumbs.c).

    tools/level-thumb.py BUILD_DIR LEVEL [--painted] [--png out.png]

Runs the headless simulator (BUILD_DIR/rolling_rob_sim) from the repository
root: LEVEL (1-based) in the original look (or, with --painted, the painted
look from art/), frame 40, no HUD and no touch buttons, seed 1, then scales
the 480x272 frame down to 108x61 (bilinear) and prints it as a C array of
B,G,R bytes named thumb_<LEVEL-1>_map (painted: thumb_painted_<LEVEL-1>_map),
to paste into level_thumbs.c (and its entry into rr_level_thumbs[] or
rr_level_thumbs_painted[]).
Needs Pillow.
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image

W, H, TW, TH = 480, 272, 108, 61


def main():
    args = [a for a in sys.argv[1:] if a != "--painted"]
    painted = len(args) != len(sys.argv) - 1
    png = None
    if "--png" in args:
        i = args.index("--png")
        png = args[i + 1]
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    build, level = args[0], int(args[1])
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with tempfile.TemporaryDirectory() as tmp:
        raw = os.path.join(tmp, "frame.raw")
        # A save file of its own every run (the game rewrites it): the look,
        # and no best times or other choices from a player's save.
        save = os.path.join(tmp, "save.txt")
        with open(save, "w") as f:
            f.write("art 0 %d\n" % (1 if painted else 0))
        env = dict(os.environ)
        env.pop("ROLLING_ROB_ART", None)  # art/ in the repository root
        subprocess.run([os.path.abspath(os.path.join(build, "rolling_rob_sim")), "--headless", "--no-hud",
                        "--no-touch-ui", "--seed", "1", "--frames", "40", "--level", str(level), "--save", save,
                        "--out", raw],
                       cwd=root, env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(raw, "rb") as f:
            frame = Image.frombytes("RGBA", (W, H), f.read(), "raw", "BGRA").convert("RGB")
    thumb = frame.resize((TW, TH), Image.BILINEAR)
    if png:
        thumb.save(png)
    data = thumb.tobytes("raw", "BGR")
    print("static const uint8_t thumb_%s%d_map[] = {" % ("painted_" if painted else "", level - 1))
    for i in range(0, len(data), 24):
        print("    " + ",".join("0x%02X" % b for b in data[i:i + 24]) + ",")
    print("};")


if __name__ == "__main__":
    main()
