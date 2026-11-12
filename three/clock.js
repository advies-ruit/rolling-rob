// A fixed 60 Hz game step under a screen of any refresh rate.
// The original runs its physics once per requestAnimationFrame, so on a 120 Hz
// screen the game ran twice as fast. Here gameLoop (physics, 2D canvas, bot,
// sound) runs exactly 60 times a second, and the 3D world is drawn once per
// screen refresh: on a 60 Hz screen right after each step, as before; on a
// faster (or slower) one with Rob, the camera and the figures placed between
// the last two steps, so 120 Hz looks smooth without changing the game.
// The game's state itself is never changed: the in-between positions are set
// only while the 3D world draws, then put back.
window.RRClock = (() => {
    const STEP = 1000 / 60;
    let cap = true, last = null, acc = 0, P = STEP, alpha = 1, lock = true, steps = 0;
    let prev = null, cur = null;
    const gaps = new Array(31).fill(STEP); let gi = 0, gn = 0;
    const lists = () => [obstacles, mines, saucers, julians, fireballs];

    // what the 3D world reads, as it is where gameLoop draws (before the physics)
    function snap() {
        const m = new Map();
        for (const l of lists()) for (const o of l) m.set(o, [o.x, o.y]);
        return { cx: cameraX, cy: cameraY, rx: rob.x, ry: rob.y, rr: rob.rotation, m };
    }
    // called by gameLoop where the original drew; the world is drawn later
    function at() { prev = cur; cur = snap(); }

    function draw() {
        if (!window.RR3D) return;
        if (!prev || !cur || alpha >= 1) {
            // state of the last step, as the original drew it
            if (cur) return withState(cur, null, 1);
            return RR3D.render();
        }
        withState(prev, cur, alpha);
    }
    // draw with the state between a and b (b null: a itself), then put the game back
    function withState(a, b, k) {
        // a big jump (new level, respawn at a checkpoint) is not blended
        if (b && Math.abs(b.rx - a.rx) + Math.abs(b.ry - a.ry) > 200) { a = b; b = null; }
        const lerp = (p, q) => b ? p + (q - p) * k : p;
        const keep = { cx: cameraX, cy: cameraY, rx: rob.x, ry: rob.y, rr: rob.rotation }, moved = [];
        const src = b || a;
        cameraX = lerp(a.cx, src.cx); cameraY = lerp(a.cy, src.cy);
        rob.x = lerp(a.rx, src.rx); rob.y = lerp(a.ry, src.ry); rob.rotation = lerp(a.rr, src.rr);
        for (const l of lists()) for (const o of l) {
            const q = src.m.get(o), p = a.m.get(o) || q;
            if (!q) continue;
            moved.push([o, o.x, o.y]);
            o.x = b ? p[0] + (q[0] - p[0]) * k : q[0]; o.y = b ? p[1] + (q[1] - p[1]) * k : q[1];
        }
        try { RR3D.render(); }
        finally {
            cameraX = keep.cx; cameraY = keep.cy; rob.x = keep.rx; rob.y = keep.ry; rob.rotation = keep.rr;
            for (const [o, x, y] of moved) { o.x = x; o.y = y; }
        }
    }

    function frame(ts) {
        requestAnimationFrame(frame);
        tick(ts);
    }
    function tick(ts) {
        if (last === null) last = ts - STEP;
        let dt = ts - last; last = ts;
        if (dt > 250 || dt < 0) dt = STEP; // back from a hidden tab: go on, don't catch up
        // the screen's refresh interval: the lower quarter of the last 31 intervals --
        // a stall (a level being built, the quality test) does not move it, and a
        // device that misses refreshes now and then still shows the screen's rate
        if (dt < 100) { gaps[gi] = dt; gi = (gi + 1) % gaps.length; if (gn < gaps.length) gn++; }
        if (gn >= 5) { const g = gaps.slice(0, gn).sort((a, b) => a - b); P = g[gn >> 2]; }
        lock = Math.abs(P - STEP) < 1.5;
        let n;
        if (lock) {
            // a 60 Hz screen: one step per refresh, as the original (more after a missed frame)
            n = Math.max(1, Math.min(4, Math.round(dt / STEP)));
            acc = 0; alpha = 1;
        } else {
            // the real time, so the game keeps exactly 60 steps a second; a step may come
            // up to 2 ms early (acc goes a little below 0 then) so the jitter of the
            // timestamps does not make the steps uneven (at 120 Hz: every second frame)
            acc += dt;
            n = 0;
            while (acc >= STEP - 2 && n < 4) { acc -= STEP; n++; }
            if (n === 4 && acc > 0) acc = 0;
            alpha = Math.max(0, Math.min(1, acc / STEP));
        }
        if ((window.RRQol && RRQol.paused) || (window.RRNet && RRNet.hold)) { n = 0; acc = 0; } // paused (qol.js): no steps, the picture stays
        for (let i = 0; i < n; i++) { gameLoop(); steps++; }
        // capped (the default): on a screen faster than 60 Hz the world is drawn only
        // when the game took a step, showing that step -- 60 pictures a second, half
        // the work of 120 Hz. Uncapped: every refresh, with in-between positions.
        if (cap && !lock) { if (n > 0) { alpha = 1; draw(); } }
        else draw();
    }

    return {
        start() { requestAnimationFrame(frame); },
        reset() { prev = cur = null; }, // after the 3D world was away (2D mode): no blending with old positions
        at, tick,
        // the screen's refresh rate as measured, and whether the steps follow it one to one
        get hz() { return 1000 / P; },
        get lock() { return lock; },
        get steps() { return steps; },
        get cap() { return cap; }, set cap(v) { cap = !!v; },
        STEP,
    };
})();
