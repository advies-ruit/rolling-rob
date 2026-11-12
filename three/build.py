#!/usr/bin/env python3
"""Build three/rolling-rob-3d.html: the original game (reference/rolling-rob.html)
with the three.js world of three/rr3d.js behind it. One HTML file; three.js comes
from cdnjs. The site (below) uses its own copy, vendor/three.module.min.js, if it
is there. Without WebGL or three.js the page plays exactly like the original.

Each change below must match exactly once in the original, or the build stops.
"""
import pathlib, sys

HERE = pathlib.Path(__file__).resolve().parent
# Two layouts: this repository (the original in reference/, the figures cut from
# tools/prerender/render.js, textures made from textures-src/ with cwebp) and a
# copy of three/ on its own (original.html, figures.js, textures/*.webp next to
# this file; tools/sync_3d_public.py makes it).
SRC = HERE / 'original.html' if (HERE / 'original.html').exists() else HERE.parent / 'reference' / 'rolling-rob.html'
OUT = HERE / 'rolling-rob-3d.html'
THREE = 'https://cdnjs.cloudflare.com/ajax/libs/three.js/0.160.0/three.module.min.js'
VENDOR = HERE / 'vendor' / 'three.module.min.js'  # the same file, kept here (MIT, vendor/LICENSE-three.js.txt)
RENDER = HERE.parent / 'tools' / 'prerender' / 'render.js'

html = SRC.read_text(encoding='utf-8')
rr3d = (HERE / 'rr3d.js').read_text(encoding='utf-8')
rbox = (HERE / 'rounded-box.js').read_text(encoding='utf-8')
music = (HERE / 'music.js').read_text(encoding='utf-8')  # the tracks: music/*.mp3 next to the page
sfx = (HERE / 'sfx.js').read_text(encoding='utf-8')
touch = (HERE / 'touch.js').read_text(encoding='utf-8')
bot = (HERE / 'bot.js').read_text(encoding='utf-8')
ghost = (HERE / 'ghost.js').read_text(encoding='utf-8')  # ghost Rob
qol = (HERE / 'qol.js').read_text(encoding='utf-8')  # R, number keys, pause, gamepad
menu = (HERE / 'menu.js').read_text(encoding='utf-8')  # the menu (Esc), status line, finish card, offline copy
# race rooms (?room=), off without one; left out where mp.js is not there (the public copy)
mp = (HERE / 'mp.js').read_text(encoding='utf-8') if (HERE / 'mp.js').exists() else ''
clock = (HERE / 'clock.js').read_text(encoding='utf-8')  # the fixed 60 Hz step
# the speedrun planned beforehand (made by the seed search), if there is one
routes = (HERE / 'routes.js').read_text(encoding='utf-8') if (HERE / 'routes.js').exists() else ''

# Textures that come with the page: textures-src/ground (tiling, get a normal map in
# the page) and textures-src/foliage (see-through), as WebP data URLs. Needs cwebp;
# without it or without the folder the page simply has none.
def textures():
    import base64, json, shutil, subprocess, tempfile
    ready = HERE / 'textures' / 'index.json'
    if ready.exists():  # already WebP (the copy on its own)
        idx = json.loads(ready.read_text(encoding='utf-8'))
        out = {k: {'leaf': v['leaf'], 'sky': v['sky'], 'url': 'data:image/webp;base64,' +
                   base64.b64encode((HERE / 'textures' / (k + '.webp')).read_bytes()).decode()} for k, v in idx.items()}
        return 'window.RR_TEX = ' + json.dumps(out) + ';'
    root = HERE / 'textures-src'
    if not root.exists() or not shutil.which('cwebp'):
        print('build: no textures (textures-src or cwebp missing)')
        return ''
    out, index = {}, {}
    with tempfile.TemporaryDirectory() as tmp:
        # Sizes from what is on screen: the picture is at most 1920 wide (ultra 2880), about one
        # pixel per world unit at the playing line, whatever the monitor; further back less.
        # The sources stay large; only these numbers decide what the page carries.
        SIZES = {'forest-floor': 512, 'dark-earth': 768, 'granite': 768, 'pine-bark': 256, 'clouds-far': 1024, 'clouds-cirrus': 1024, 'desert-cactus-skin': 256}
        for sub_dir, default, q, leaf in (('ground', 512, 82, False), ('foliage', 256, 85, True), ('sky', 512, 85, False)):
            for f in sorted((root / sub_dir).glob('*.png')):
                size = SIZES.get(f.stem, default)
                w = pathlib.Path(tmp) / (f.stem + '.webp')
                subprocess.run(['cwebp', '-quiet', '-q', str(q), '-alpha_q', '90', '-exact', '-sharp_yuv', '-resize', str(size), '0', str(f), '-o', str(w)], check=True)
                out[f.stem] = {'leaf': leaf, 'sky': sub_dir == 'sky', 'url': 'data:image/webp;base64,' + base64.b64encode(w.read_bytes()).decode()}
                if EXPORT:  # the WebP files for a copy on its own
                    EXPORT.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(w, EXPORT / w.name)
                    index[f.stem] = {'leaf': leaf, 'sky': sub_dir == 'sky'}
    if EXPORT:
        (EXPORT / 'index.json').write_text(json.dumps(index, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    return 'window.RR_TEX = ' + json.dumps(out) + ';'
import os
EXPORT = pathlib.Path(os.environ['RR3D_EXPORT_TEX']) if os.environ.get('RR3D_EXPORT_TEX') else None
tex = textures()

# the level menu's pictures (thumbs/level-N.webp, screenshots of each level's start in
# 3D with textures, 320x180), as data URLs; without them the tiles show the sky colours
def thumbs():
    import base64, json
    d = HERE / 'thumbs'
    urls = []
    for i in range(10):
        f = d / f'level-{i}.webp'
        urls.append('data:image/webp;base64,' + base64.b64encode(f.read_bytes()).decode() if f.exists() else None)
    return 'window.RR_THUMBS = ' + json.dumps(urls) + ';' if any(urls) else ''
thumbs_js = thumbs()

# The figures' models, taken as they are from the prerender page (one source
# for the LVGL renders and this): its helpers and make* functions, from the
# materials section up to the level-1 section. Its render*/save code stays
# there; nothing in this part runs on its own.
src = (HERE / 'figures.js').read_text(encoding='utf-8') if (HERE / 'figures.js').exists() else RENDER.read_text(encoding='utf-8')
A = '// ---------------- materials and small helpers ----------------'
B = '// ---------------- level 1: no trees on the ridge ----------------'
if src.count(A) != 1 or src.count(B) != 1:
    sys.exit('build: render.js markers not found')
figures = src[src.index(A):src.index(B)]
for name in ['function makeRob(', 'function makeSlime(', 'function makeMine(', 'function makeUfo(', 'function makeJulian(', 'function makeCrystal(']:
    if name not in figures:
        sys.exit('build: render.js has no ' + name)

def sub(old, new):
    global html
    n = html.count(old)
    if n != 1:
        sys.exit(f'build: expected 1 match, found {n}: {old[:60]!r}')
    html = html.replace(old, new)

# the title in the tab
sub('<title>', '<title>3D · ')

# a stage that holds both canvases at the same size
sub('''        #title {''', '''        :root { color-scheme: dark; --bg: #000; }
        #stage { position: relative; display: inline-block; line-height: 0; }
        #stage #gameCanvas { position: relative; z-index: 1; }
        #three { position: absolute; left: 0; top: 0; width: 100%; height: 100%;
                 max-width: none; max-height: none; pointer-events: none; }
        #fps3d { position: absolute; left: 12px; bottom: 10px; z-index: 10; color: #fff;
                 font: 14px monospace; text-shadow: 1px 1px 2px #000; opacity: 0.7; }
        #music3d { position: fixed; right: 14px; bottom: 10px; z-index: 10; color: #fff; pointer-events: none;
                   font: 14px monospace; text-shadow: 1px 1px 2px #000; opacity: 0; transition: opacity 0.6s; }
        #title {''')
sub('''        <canvas id="gameCanvas" width="1920" height="1080"></canvas>''',
    '''        <div id="stage">
            <canvas id="three" width="1920" height="1080"></canvas>
            <canvas id="gameCanvas" width="1920" height="1080"></canvas>
        </div>''')
sub('''    <script>
        const canvas = document.getElementById('gameCanvas');''',
    f'''    <div id="fps3d"></div>
    <script type="module">
import * as THREE from '{THREE}';
{tex}
{rbox}
// ---- from tools/prerender/render.js ----
{figures}
// ---- the world (three/rr3d.js) ----
{rr3d}
    </script>
    <script>
{music}
{sfx}
{touch}
{thumbs_js}
{menu}
{routes}
{bot}
{ghost}
{qol}
{mp}
{clock}
    </script>
    <script>
        const canvas = document.getElementById('gameCanvas');''')

F3 = '(window.RR3D && RR3D.figures)'
sub('                drawMonster(obs);', f'                if (!{F3}) drawMonster(obs);')
sub('            drawMines();', f'            if (!{F3}) drawMines();')
sub('                drawSaucer(saucer);', f'                if (!{F3}) drawSaucer(saucer);')
sub('                drawJulian(julian);', f'                if (!{F3}) drawJulian(julian);')
sub('                drawRob();', f'                if (!{F3}) drawRob();')

# the fireballs and explosions in 3D: the game tells the world where it blew up
FX3 = '(window.RR3D && RR3D.fire)'
sub('            drawFireballs();', f'            if (!{FX3}) drawFireballs();')
sub('            drawExplosion();', f'            if (!{FX3}) drawExplosion();')
sub('''        function createExplosion(x, y) {''', '''        function createExplosion(x, y) {
            if (!window.RRSim) { // not while the speedrun bot looks ahead
            if (window.RRSfx) RRSfx.boom(x);
            if (window.RR3D && RR3D.boom) RR3D.boom(x, y);
            }''')
# confetti and fireworks at the finish: the game keeps its lists, the world draws its own
sub('                drawConfetti();', f'                if (!{FX3}) drawConfetti();')
sub('                drawFireworks();', f'                if (!{FX3}) drawFireworks();')
sub('''        function createConfetti() {''', '''        function createConfetti() {
            if (window.RR3D && RR3D.confetti && !window.RRSim) RR3D.confetti(rob.x, rob.y);''')
sub('''        function createFirework(x, y) {''', '''        function createFirework(x, y) {
            if (window.RR3D && RR3D.firework) RR3D.firework(x, y);''')

# the finish, the checkpoints and the jump power meter (Julian's sign comes with them)
CO = '(window.RR3D && RR3D.course)'
sub('''            drawFinishLine();
            drawCheckpoints();''', f'''            if (!{CO}) {{ drawFinishLine(); drawCheckpoints(); }}''')
sub('''            // Draw charge meter (in screen space) - only for mouse
            if (isMouseDown) {''', f'''            // Draw charge meter (in screen space) - only for mouse
            if (isMouseDown && !{CO}) {{''')

# a click on Rob: the original compares page pixels with world coordinates, so it
# only hits near the start of a level (camera at 0) on a full-size canvas. Here it
# is tested where Rob is drawn: through the 3D camera (which tilts and zooms), or
# the 2D camera without 3D, with the canvas's on-screen scale taken into account.
sub('''            const dx = clickX - rob.x;
            const dy = clickY - rob.y;
            const distance = Math.sqrt(dx * dx + dy * dy);

            if (distance < rob.radius + 30) {''', '''            const sc = canvas.width / rect.width;
            const seen = (window.RR3D && RR3D.toScreen) ? RR3D.toScreen(rob.x, rob.y, rob.radius)
                                                        : { x: rob.x - cameraX, y: rob.y - cameraY, r: rob.radius };
            const dx = clickX * sc - seen.x;
            const dy = clickY * sc - seen.y;
            const distance = Math.sqrt(dx * dx + dy * dy);

            if (distance < seen.r + 30) {''')

# the beam: only its drawing; who is in it and the abduction stay the game's
sub('''            ctx.save();
            ctx.globalAlpha = 0.6;''', f'''            if (!{FX3}) {{
            ctx.save();
            ctx.globalAlpha = 0.6;''')
sub('''            ctx.restore();

            // Check if Rob is still in beam''', '''            ctx.restore();
            }

            // Check if Rob is still in beam''')
# the 3D world draws the sky and the ground; the 2D canvas is cleared and keeps the rest
sub('''            ctx.save();
            ctx.translate(-cameraX, -cameraY);

            drawSky();
            drawMountain();''',
    '''            if (window.RRSfx) RRSfx.frame();
            if (window.RR3D) { ctx.clearRect(0, 0, canvas.width, canvas.height); RRClock.at(); } // drawn by RRClock
            if (window.RRBot) RRBot.frame(); // the speedrun bot: after the camera moved, before the physics
            if (window.RRGhost) RRGhost.frame(); // ghost Rob: records your input, places the ghosts

            ctx.save();
            ctx.translate(-cameraX, -cameraY);

            if (!window.RR3D) drawSky();
            if (!(window.RR3D && RR3D.soil)) drawMountain();''')
sub('''            ctx.closePath();
            ctx.fill();

            // Add ground texture''',
    '''            ctx.closePath();
            if (!window.RR3D) ctx.fill();

            // Add ground texture''')
sub('''            for (let i = 1; i < mountainPoints.length - 2; i++) {
                ctx.lineTo(mountainPoints[i].x, mountainPoints[i].y);
            }
            ctx.stroke();''',
    '''            for (let i = 1; i < mountainPoints.length - 2; i++) {
                ctx.lineTo(mountainPoints[i].x, mountainPoints[i].y);
            }
            if (!window.RR3D) ctx.stroke();''')
sub('''            generateMountain(level);
            resetLevel();''',
    '''            if (window.RRSeed) RRSeed.around(level, () => generateMountain(level)); else generateMountain(level);
            if (window.RR3D) RR3D.build(level);
            resetLevel();''')

# a fixed 60 Hz step whatever the screen's refresh rate (clock.js): gameLoop no
# longer asks for the next frame itself, the clock calls it 60 times a second
sub('''                ctx.textAlign = 'left';
            }

            requestAnimationFrame(gameLoop);
        }''', '''                ctx.textAlign = 'left';
            }
        }''')
sub('''        loadLevel(0);
        gameLoop();''', '''        loadLevel(0);
        RRClock.start();''')

# touch buttons and smaller texts on small screens: after the original's own styles
sub('''    </style>''', '''        /* touch: buttons for the arrow keys and the keys a phone lacks (touch.js) */
        #gameCanvas { touch-action: none; -webkit-user-select: none; user-select: none; -webkit-touch-callout: none; }
        #touch3d { display: none; }
        body.touch3d { overscroll-behavior: none; touch-action: manipulation; -webkit-user-select: none; user-select: none; }
        body.touch3d #touch3d { display: block; }
        #touch3d .pad { position: fixed; bottom: max(14px, env(safe-area-inset-bottom)); z-index: 20; display: flex; gap: 12px; }
        #touch3d .pad.left { left: max(14px, env(safe-area-inset-left)); }
        #touch3d .pad.right { right: max(14px, env(safe-area-inset-right)); }
        #touch3d .pad button, #touch3d .opts button {
            position: static; transform: none; margin: 0; padding: 0; border-radius: 50%; touch-action: none;
            background: rgba(0,0,0,0.35); color: #fff; border: 2px solid rgba(255,215,0,0.8); box-shadow: 0 3px 8px rgba(0,0,0,0.4);
            -webkit-tap-highlight-color: transparent; }
        #touch3d .pad button { width: clamp(54px, 13vmin, 92px); height: clamp(54px, 13vmin, 92px); font-size: clamp(22px, 5.5vmin, 36px); }
        #touch3d .opts { position: fixed; left: max(10px, env(safe-area-inset-left)); top: 34px; z-index: 20; display: flex; gap: 8px; }
        body.touch3d #fps3d { display: none; }
        #perf3d { position: absolute; right: 10px; bottom: 10px; z-index: 12; max-width: min(640px, calc(100vw - 20px)); padding: 8px 10px;
            background: rgba(0,0,0,.72); color: #fff; font: 13px/1.45 ui-monospace, Menlo, monospace; border-radius: 6px; pointer-events: auto; cursor: pointer; }
        /* the speedrun bot (bot.js) */
        #botpath3d { position: absolute; left: 0; top: 0; width: 100%; height: 100%; z-index: 2; pointer-events: none; }
        #speedrun3d { display: none; position: fixed; left: 50%; transform: translateX(-50%); top: 52px; z-index: 15; pointer-events: none;
                      font: 15px monospace; color: #fff; background: rgba(0,0,0,0.55); padding: 6px 12px; border-radius: 8px;
                      text-align: center; text-shadow: 1px 1px 2px #000; max-width: 92vw; }
        #speedrun3d b { color: #ffd700; } #speedrun3d .up { color: #6f6; } #speedrun3d .down { color: #f77; }
        #touch3d .opts button { width: 36px; height: 36px; font-size: 15px; }
        #touch3d button.down { background: rgba(255,215,0,0.55); transform: scale(0.94); }
        #touch3d button:hover { transform: none; background: rgba(0,0,0,0.35); }
        #touch3d .turn { display: none; position: fixed; left: 0; right: 0; bottom: 30%; text-align: center; color: #fff; z-index: 20;
                         font: 16px sans-serif; text-shadow: 1px 1px 3px #000; pointer-events: none; }
        @media (orientation: portrait) { body.touch3d #touch3d .turn { display: block; } }
        @media (orientation: portrait) and (max-width: 700px) { /* a phone held upright: the game in the middle */
            #gameContainer { justify-content: center; }
            #title { display: none; }
            #touch3d .turn { bottom: auto; top: 18%; }
            html #info { bottom: 170px; max-width: 90vw; }
            html body > button { bottom: 120px; }
        }
        /* the original's texts are sized for 1920 wide; smaller on a small screen */
        @media (max-width: 1000px), (max-height: 560px) {
            #title { font-size: 18px; top: 6px; right: 10px; }
            #levelSelect { font-size: 12px; top: 32px; right: 10px; }
            #levelSelect select { font-size: 12px; padding: 2px 4px; }
            #score { font-size: 15px; top: 8px; left: 10px; }
            #info { font-size: 14px; bottom: 52px; padding: 6px 12px; max-width: 70vw; }
            body > button { font-size: 13px; padding: 6px 12px; bottom: 10px; }
            #resetBtn { transform: translateX(-100px); }
            #nextBtn { transform: translateX(20px); }
        }
    </style>''')

OUT.write_text(html, encoding='utf-8')
print(f'build: {OUT} ({len(html) // 1024} KB)')

# The site for a repository of its own (GitHub Pages): site/index.html is the same page
# with the manifest and icons named in its head, plus the offline copy (app/sw.js, its
# version a hash of what it keeps), the manifest and the icons. Music is not part of
# it: music/*.mp3 next to index.html plays (and is kept offline once heard) if it is there.
# RR3D_SITE: where (default three/site). RR3D_HOME: the page of the LVGL version
# (e.g. ../), which adds "back" to the menu. RR3D_MANIFEST=0: no manifest of its own,
# so inside a bigger site the page stays part of that site's app.
def site():
    import hashlib, shutil, datetime, json
    out = pathlib.Path(os.environ.get('RR3D_SITE') or HERE / 'site')
    out.mkdir(parents=True, exist_ok=True)
    home = os.environ.get('RR3D_HOME', '')
    own = os.environ.get('RR3D_MANIFEST', '1') != '0'
    ICONS = ['icon-192.png', 'icon-512.png', 'icon-maskable-192.png', 'icon-maskable-512.png']
    head = (('<link rel="manifest" href="manifest.webmanifest">\n    ' if own else '') +
            '<link rel="icon" href="icon-192.png">\n'
            '    <link rel="apple-touch-icon" href="icon-192.png">\n'
            '    <meta name="theme-color" content="#000000">\n'
            '    <meta name="apple-mobile-web-app-capable" content="yes">\n' +
            (f'    <script>window.RR_HOME = {json.dumps(home)};</script>\n' if home else '') + '    <title>')
    page = html.replace('<title>', head, 1)
    three = THREE
    if VENDOR.exists():  # three.js from the site itself, not from a CDN
        three = 'three.module.min.js'
        page = page.replace(f"import * as THREE from '{THREE}';", f"import * as THREE from './{three}';", 1)
        shutil.copyfile(VENDOR, out / three)
    (out / 'index.html').write_text(page, encoding='utf-8')
    app = HERE / 'app'
    for f in ['manifest.webmanifest'] + ICONS:
        shutil.copyfile(app / f, out / f)
    h = hashlib.sha256()
    for f in ['index.html', 'manifest.webmanifest'] + ICONS:
        h.update((out / f).read_bytes())
    if three != THREE:
        h.update((out / three).read_bytes())
    sw = (app / 'sw.js').read_text(encoding='utf-8')
    h.update(sw.encode())
    sw = (sw.replace('__VERSION__', h.hexdigest()[:12]).replace('__THREE__', three)
            .replace('__BUILT__', datetime.date.today().isoformat()))
    (out / 'sw.js').write_text(sw, encoding='utf-8')
    print(f'build: {out}/ (index.html, sw.js, manifest, icons)')
site()
