// ---- touch, for phones and tablets: as in the LVGL build. A tap on the playfield
// is the space jump (and the next level after the finish); holding longer than
// 150 ms is the charged launch the mouse gives on Rob, with the meter by Rob;
// on-screen buttons are the arrow keys (several fingers at once work). After a
// game over a tap restarts the level, once 0.7 s have passed (the LVGL build
// does the same; the original has only its Reset button). The small buttons
// stand in for the keys a phone has not got: M music, S sound, F effects.
// Shown from the first touch, or at once on a touch-only device. The mouse and
// the keyboard work as before, except that space or enter after a game over restarts,
// and arrow keys held through a reset or into the next level stay held (as in LVGL).
(function () {
    const HOLD_MS = 150, LOCK_MS = 700;
    const capture = (el, id) => { try { el.setPointerCapture(id); } catch (e) {} }; // keeps the finger's events here
    const key = (code, down) => document.dispatchEvent(new KeyboardEvent(down ? 'keydown' : 'keyup', { code, key: code === 'Space' ? ' ' : code, bubbles: true }));
    const winKey = code => { const shiftKey = code.startsWith('shift+'); code = code.replace('shift+', '');
        window.dispatchEvent(new KeyboardEvent('keydown', { code, shiftKey, key: code.slice(3).toLowerCase(), bubbles: true })); };
    let shown = false;
    const ui = document.createElement('div');
    ui.id = 'touch3d';
    ui.innerHTML = `
        <div class="pad left"><button data-k="ArrowLeft" aria-label="left">&#9664;</button><button data-k="ArrowRight" aria-label="right">&#9654;</button></div>
        <div class="pad right"><button data-k="ArrowUp" aria-label="up">&#9650;</button></div>
        <div class="opts"><button data-o="KeyM" aria-label="music">&#9835;</button><button data-o="KeyS" aria-label="sound">&#128266;</button><button data-o="KeyF" aria-label="effects">FX</button><button data-o="KeyB" aria-label="speedrun-bot">BOT</button><button data-o="KeyQ" aria-label="quality">Q</button><button data-o="KeyG" aria-label="ghosts">G</button><button data-o="KeyL" aria-label="share your run">&#10548;</button><button data-o="shift+KeyP" aria-label="performance">ms</button><button data-o="Escape" aria-label="pause">II</button></div>
        <div class="turn">Turn your device for a bigger picture</div>`;
    document.body.appendChild(ui);
    function show() { if (!shown) { shown = true; document.body.classList.add('touch3d'); } }
    if (window.matchMedia && matchMedia('(hover: none) and (pointer: coarse)').matches) show();

    // the arrow buttons: held while a finger is on them
    for (const b of ui.querySelectorAll('[data-k]')) {
        const k = b.dataset.k;
        let on = 0;
        const up = e => { if (on && (!e || e.pointerId === on)) { on = 0; b.classList.remove('down'); key(k, false); } };
        b.addEventListener('pointerdown', e => { e.preventDefault(); e.stopPropagation(); show();
            if (on) return; on = e.pointerId; capture(b, e.pointerId); b.classList.add('down'); key(k, true); });
        b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up); b.addEventListener('lostpointercapture', up);
        b.addEventListener('contextmenu', e => e.preventDefault());
    }
    for (const b of ui.querySelectorAll('[data-o]'))
        b.addEventListener('pointerdown', e => { e.preventDefault(); e.stopPropagation(); winKey(b.dataset.o); b.classList.add('down'); setTimeout(() => b.classList.remove('down'), 180); });

    // the playfield: tap = jump, hold = charge
    const field = document.getElementById('gameCanvas');
    let finger = 0, t0 = 0, charging = false, holdTimer = 0, overAt = 0;
    setInterval(() => { if (gameOver && !gameWon) { if (!overAt) overAt = performance.now(); } else overAt = 0; }, 50);
    // the keyboard the same way: space or enter after a game over starts the level again
    // (the original ignores space then, and enter only works when its Reset button has the
    // focus); a fresh press, not a held key, and not while typing in a field
    window.addEventListener('keydown', e => {
        if ((e.code !== 'Space' && e.code !== 'Enter' && e.code !== 'NumpadEnter') || e.repeat) return;
        if (!gameOver || gameWon || (window.RRBot && RRBot.on) || /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName || '')) return;
        e.preventDefault();
        if (overAt && performance.now() - overAt > LOCK_MS) resetLevel();
    });
    // arrow keys held through a reset or a new level stay held, as in the LVGL build (which
    // reads the keyboard's state every frame); the original lets go of them in resetLevel,
    // and only the last key pressed comes back, by the key repeat. Space is not kept: a
    // jump needs a fresh press there too. Leaving the window lets go of everything.
    const ARROWS = ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'], held = new Set();
    const mine = () => !window.RRSim && !(window.RRBot && RRBot.on); // not the bot's, not a ghost's replay
    window.addEventListener('keydown', e => {
        if (ARROWS.includes(e.code) && !/^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName || '')) held.add(e.code);
    }, true);
    window.addEventListener('keyup', e => { held.delete(e.code); }, true);
    const letGo = () => { held.clear(); if (mine()) for (const k of ARROWS) keysPressed[k] = false; };
    window.addEventListener('blur', letGo);
    document.addEventListener('visibilitychange', () => { if (document.hidden) letGo(); });
    setTimeout(() => { // after the game's script has defined resetLevel (loadLevel calls it too)
        const reset = window.resetLevel;
        window.resetLevel = function () {
            const r = reset.apply(this, arguments);
            if (mine()) for (const k of held) keysPressed[k] = true;
            return r;
        };
    }, 0);
    field.addEventListener('pointerdown', e => {
        if (e.pointerType === 'mouse') return; // the game's own mouse handling
        e.preventDefault(); show();
        if (finger) return;
        if (gameOver && !gameWon) { // dead: a tap starts the level again, not by accident straight away
            if (overAt && performance.now() - overAt > LOCK_MS) resetLevel();
            return;
        }
        if (gameWon) { key('Space', true); key('Space', false); return; } // the game's own "next level"
        finger = e.pointerId; t0 = performance.now(); charging = false;
        capture(field, e.pointerId);
        holdTimer = setTimeout(() => { // held: the mouse's charge, counted from the touch
            if (!finger || gameOver) return;
            charging = true; isMouseDown = true; mouseDownTime = Math.round(HOLD_MS / 16);
        }, HOLD_MS);
    });
    function release(e, cancel) {
        if (!finger || e.pointerId !== finger) return;
        finger = 0; clearTimeout(holdTimer);
        if (charging) {
            charging = false;
            if (isMouseDown && !cancel && !gameOver) pushRob(Math.min(mouseDownTime / 30, 1));
            isMouseDown = false; mouseDownTime = 0;
        } else if (!cancel) { key('Space', true); key('Space', false); }
    }
    field.addEventListener('pointerup', e => release(e, false));
    field.addEventListener('pointercancel', e => release(e, true));
    field.addEventListener('contextmenu', e => { if (shown) e.preventDefault(); });
    window.RRTouch = { show };
})();
