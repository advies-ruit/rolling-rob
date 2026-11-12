// ---- speedrun bot: B starts a run from level 1 (again: stops it). ?bot=1 starts one at once.
// It plans: every few frames it saves the game's state, plays the game's own
// physics (updatePhysics) ahead for each of a few dozen candidate plans, puts
// the state back, and follows the best. A plan is a short series of what a
// player can do: hold right, left, up (which glides: it takes 0.3 off the 0.4
// gravity), down; jump and double jump (space); a charged launch (held on Rob,
// released after up to 30 frames). The best plan reaches the finish soonest,
// or gets furthest, without dying, being beamed up or flying off the screen.
// The plan's path is drawn as dots. The clock counts game frames (60 a second)
// from the start of a level to its finish, deaths included; the 5 s wait after
// the finish is skipped (space) and not counted. Splits and the best run are
// kept in localStorage.
(function () {
    const H = 96, EVERY = 5;                    // look ahead 1.6 s, plan again every 5 frames
    const MODES = { R: { R: 1 }, RU: { R: 1, U: 1 }, N: {}, U: { U: 1 }, L: { L: 1 }, LU: { L: 1, U: 1 }, RD: { R: 1, D: 1 } };
    const PICK = ['R', 'R', 'RU', 'RU', 'RU', 'N', 'U', 'L', 'LU', 'RD'];
    const load = (k, d) => { try { const v = localStorage.getItem('rr3d.' + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } };
    const save = (k, v) => { try { localStorage.setItem('rr3d.' + k, JSON.stringify(v)); } catch (e) {} };
    const clock = performance.now.bind(performance); // the real clock, for the planning budget
    let seed = 12345;
    // ---- levels from a seed: generateMountain draws its terrain, mines, saucers and
    // Julians from Math.random; with a seed set, that is a fixed series per level, so a
    // level looks the same every time (the original makes a new one each load).
    // ?seed=N in the address does it for a human player too.
    const urlSeed = (location.search.match(/[?&]seed=(\d+)/) || [])[1];
    window.RRSeed = {
        seed: urlSeed ? +urlSeed : null,
        around(level, fn) {
            if (this.seed == null) return fn();
            let a = (Math.imul(this.seed, 2654435761) ^ Math.imul(level + 1, 40503)) >>> 0;
            const real = Math.random;
            Math.random = () => { a = (a + 0x6D2B79F5) >>> 0; let t = Math.imul(a ^ (a >>> 15), 1 | a);
                t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
            try { fn(); } finally { Math.random = real; }
        },
    };
    const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
    const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));

    // ---- saving and restoring the game's state around a look-ahead
    let silent = null;
    function quiet(on) { // the game writes its two texts every frame; not while we look ahead
        for (const el of [scoreDisplay, infoDisplay]) {
            if (on) Object.defineProperty(el, 'textContent', { configurable: true, get() { return ''; }, set() {} });
            else delete el.textContent;
        }
    }
    function snap() {
        return {
            rob: { ...rob }, keys: { ...keysPressed },
            g: [gameStarted, gameOver, gameWon, distance, autoAdvanceTimer, beamActive, beamTarget, beamProgress, lastCheckpoint, isMouseDown, mouseDownTime],
            jul: julians.map(j => ({ ...j })), sau: saucers.map(s => ({ ...s })), cps: checkpoints.map(c => c.activated),
            fb: fireballs.map(f => [f, { ...f }]), ex: explosionParticles.map(p => [p, { ...p }]), exArr: explosionParticles, conf: confetti.length,
        };
    }
    function restore(s) {
        Object.assign(rob, s.rob); keysPressed = { ...s.keys };
        [gameStarted, gameOver, gameWon, distance, autoAdvanceTimer, beamActive, beamTarget, beamProgress, lastCheckpoint, isMouseDown, mouseDownTime] = s.g;
        julians.forEach((j, i) => Object.assign(j, s.jul[i])); saucers.forEach((u, i) => Object.assign(u, s.sau[i]));
        checkpoints.forEach((c, i) => { c.activated = s.cps[i]; });
        fireballs.length = 0; for (const [f, o] of s.fb) fireballs.push(Object.assign(f, o));
        explosionParticles = s.exArr; explosionParticles.length = 0; for (const [p, o] of s.ex) explosionParticles.push(Object.assign(p, o));
        confetti.length = s.conf;
    }
    function doJump() { // what the game's space key does
        if (!gameStarted) gameStarted = true;
        if (rob.jumpsUsed < 2) { rob.vx += 4; rob.vy = -8; rob.jumpsUsed++; }
    }

    // ---- a plan: per frame which keys, and on which frames a jump, a launch's start and release
    function makePlan(charging) {
        const m1 = PICK[ri(0, PICK.length - 1)], m2 = PICK[ri(0, PICK.length - 1)], cut = ri(4, 50);
        const p = { m1, m2, cut, jumps: [], ls: -1, lr: -1 };
        if (charging >= 0) { p.lr = ri(0, Math.max(0, 30 - charging)); }
        else if (rnd() < 0.22) { p.ls = ri(0, 24); p.lr = p.ls + ri(6, 30); }
        if (rnd() < 0.55) { const a = ri(0, 45); p.jumps.push(a); if (rnd() < 0.4) p.jumps.push(a + ri(5, 22)); }
        return p;
    }
    const fixed = charging => [
        { m1: 'R', m2: 'R', cut: 1, jumps: [], ls: -1, lr: charging >= 0 ? 0 : -1 },
        { m1: 'RU', m2: 'RU', cut: 1, jumps: [], ls: -1, lr: charging >= 0 ? Math.max(0, 30 - charging) : -1 },
        { m1: 'RU', m2: 'R', cut: 30, jumps: [0], ls: -1, lr: charging >= 0 ? 0 : -1 },
        { m1: 'N', m2: 'R', cut: 25, jumps: [], ls: -1, lr: charging >= 0 ? 0 : -1 },
        { m1: 'L', m2: 'RU', cut: 20, jumps: [], ls: -1, lr: charging >= 0 ? 0 : -1 },
    ];
    function shifted(p, n) { // last round's best, n frames on
        if (!p) return null;
        const q = { ...p, cut: Math.max(0, p.cut - n), jumps: p.jumps.map(f => f - n).filter(f => f >= 0), ls: p.ls - n, lr: p.lr - n };
        if (q.ls < 0 && q.lr < 0 && p.lr >= 0 && p.lr - n < 0) q.lr = -1;
        if (q.ls < 0 && p.ls >= 0) q.ls = -1; // a launch whose start has gone by is now "charging"
        return q;
    }
    function keysAt(p, f) { return MODES[f < p.cut ? p.m1 : p.m2]; }

    // ---- one look-ahead: how good is this plan?
    let chargeAt = -1; // frames the launch has been charging (real play), -1 if not
    function rollout(p, path) {
        let charge = chargeAt, score = 0, x0 = rob.x, f = 0;
        for (; f < H; f++) {
            const k = keysAt(p, f);
            keysPressed.ArrowRight = !!k.R; keysPressed.ArrowLeft = !!k.L; keysPressed.ArrowUp = !!k.U; keysPressed.ArrowDown = !!k.D;
            if (p.jumps.includes(f)) doJump();
            if (f === p.ls && charge < 0) { charge = 0; if (!gameStarted) gameStarted = true; }
            if (charge >= 0 && f >= p.lr && p.lr >= 0) { pushRob(Math.min(charge / 30, 1)); charge = -1; }
            else if (charge >= 0) charge++;
            if (!gameStarted) doJump();
            updatePhysics();
            if (path && f % 3 === 0) path.push(rob.x, rob.y);
            if (gameWon) return 1e7 - f * 1000;
            if (gameOver) return -1e7 + f * 1000 + rob.x - x0;
            if (beamActive) return -5e6 + f * 1000;
            if (rob.y < 40) score -= (40 - rob.y) * 3;              // above the screen: no use to anyone watching
        }
        const j = julians.find(q => q.x > rob.x - 200 && q.x - rob.x < 500);
        return score + (rob.x - x0) * 10 + rob.vx * 30 + (2 - rob.jumpsUsed) * 40 - (j ? 400 : 0);
    }
    let best = null, bestPath = [], N = 56, planMs = 0;
    function plan() {
        const t0 = clock();
        if (opt.n) N = opt.n;
        const s = snap(); quiet(true); window.RRSim = true;
        const cands = [shifted(best, EVERY), ...fixed(chargeAt)].filter(Boolean);
        while (cands.length < N) cands.push(makePlan(chargeAt));
        let top = null, topS = -Infinity;
        for (const p of cands) {
            if (chargeAt >= 0 && p.lr < 0) p.lr = 0;
            const sc = rollout(p, null);
            restore(s);
            if (sc > topS) { topS = sc; top = p; }
        }
        bestPath = []; rollout(top, bestPath); restore(s);
        window.RRSim = false; quiet(false);
        best = top; step = 0;
        planMs = clock() - t0;
        if (opt.n) N = opt.n; else N = planMs > 4 ? Math.max(20, N - 6) : planMs < 2 ? Math.min(90, N + 4) : N; // a few ms, so the frame keeps its pace
    }

    // ---- the run
    // a run's actions per frame, one small number: 1 right, 2 left, 4 up, 8 down,
    // 16 space, 32 start a launch, 64 release it. Recorded per level, played back
    // exactly (the game's physics counts frames and uses no clock or chance).
    const enc = a => { const o = []; for (let i = 0; i < a.length;) { let j = i; while (j < a.length && a[j] === a[i]) j++;
        o.push(a[i].toString(36) + (j - i > 1 ? '*' + (j - i).toString(36) : '')); i = j; } return o.join(','); };
    const dec = t => { const a = []; if (t) for (const r of t.split(',')) { const [c, n] = r.split('*'); for (let k = n ? parseInt(n, 36) : 1; k--;) a.push(parseInt(c, 36)); } return a; };
    let opt = {}, rec = [], tape = null, fi = 0;
    let on = false, step = 0, lv = 0, frames = 0, running = false, splits = [], deadFor = 0, wonFor = 0, lastX = 0, stuck = 0;
    const bestRun = () => load(bestKey(), null);
    const fmt = f => (f / 60).toFixed(2) + ' s';
    const hud = document.createElement('div'); hud.id = 'speedrun3d'; document.body.appendChild(hud);
    const ov = document.createElement('canvas'); ov.id = 'botpath3d'; ov.width = 1920; ov.height = 1080;
    (document.getElementById('stage') || document.body).appendChild(ov);
    const oc = ov.getContext('2d');
    // one short line (the splits per level are in the menu, RRBot.report); after the
    // end it stays a few seconds, then goes
    let endAt = 0;
    function show() {
        if (!on && (!splits.length || (endAt && performance.now() - endAt > 8000))) { hud.style.display = 'none'; return; }
        hud.style.display = 'block';
        const total = splits.reduce((a, v) => a + v, 0) + (running ? frames : 0);
        const what = opt.replay ? 'planned' : 'bot';
        hud.innerHTML = on ? `<b>SPEEDRUN</b> ${what} &nbsp; level ${lv + 1}: ${fmt(frames)} &nbsp; total ${fmt(total)}`
                           : `<b>SPEEDRUN</b> ${what} done &nbsp; total ${fmt(total)}`;
    }
    setInterval(() => { if (!on && endAt) show(); }, 1000);
    function report() { // for the menu: the last speedrun, level by level, against the best
        const b = bestRun();
        return { on, what: opt.replay ? `planned speedrun, seed ${opt.seed}` : opt.seed != null ? `bot, seed ${opt.seed}` : 'live bot',
                 splits: splits.slice(), best: b, total: splits.reduce((a, v) => a + v, 0) };
    }
    // start({}) the live bot on the level as it comes; start({seed, n, record}) with
    // levels from a seed, N plans per round; start({replay: routes}) plays a recorded run.
    let keepSeed = null;
    function start(o) {
        opt = o || {}; keepSeed = RRSeed.seed;
        if (opt.replay) opt.seed = opt.replay.seed;
        if (opt.seed != null) RRSeed.seed = opt.seed;
        on = true; endAt = 0; splits = []; lv = -1; frames = 0; running = false; best = null; chargeAt = -1; deadFor = 0; wonFor = 0; rec = [];
        loadLevel(opt.level || 0); show();
    }
    const bestKey = () => opt.seed != null ? 'speedrun.seed' + opt.seed : 'speedrun';
    function stop(done) {
        on = false; keysPressed = { ArrowLeft: false, ArrowRight: false, ArrowUp: false, ArrowDown: false };
        if (chargeAt >= 0) { isMouseDown = false; mouseDownTime = 0; chargeAt = -1; }
        oc.clearRect(0, 0, 1920, 1080);
        if (done) {
            const total = splits.reduce((a, v) => a + v, 0), b = bestRun();
            if (!b || total < b.total) save(bestKey(), { total, splits: splits.slice() });
        }
        RRSeed.seed = keepSeed;
        endAt = performance.now();
        show();
    }
    function act(code) {
        keysPressed.ArrowRight = !!(code & 1); keysPressed.ArrowLeft = !!(code & 2); keysPressed.ArrowUp = !!(code & 4); keysPressed.ArrowDown = !!(code & 8);
        if (code & 16) { key('Space', true); key('Space', false); }
        if ((code & 32) && chargeAt < 0) { chargeAt = 0; isMouseDown = true; mouseDownTime = 0; if (!gameStarted) gameStarted = true; }
        if (chargeAt >= 0 && (code & 64)) { isMouseDown = false; mouseDownTime = 0; pushRob(Math.min(chargeAt / 30, 1)); chargeAt = -1; }
        else if (chargeAt >= 0) { chargeAt++; mouseDownTime = chargeAt; }
    }
    function tapeAhead(from) { // the game's own steps for the next 1.6 s of the tape (space as doJump)
        let c = chargeAt;
        for (let f = from; f < Math.min(tape.length, from + H); f++) {
            const k = tape[f];
            keysPressed.ArrowRight = !!(k & 1); keysPressed.ArrowLeft = !!(k & 2); keysPressed.ArrowUp = !!(k & 4); keysPressed.ArrowDown = !!(k & 8);
            if (k & 16) doJump();
            if ((k & 32) && c < 0) { c = 0; if (!gameStarted) gameStarted = true; }
            if (c >= 0 && (k & 64)) { pushRob(Math.min(c / 30, 1)); c = -1; } else if (c >= 0) c++;
            updatePhysics();
            if ((f - from) % 3 === 0) bestPath.push(rob.x, rob.y);
            if (gameWon || gameOver) break;
        }
    }
    let keying = false; // the bot's own key presses get through (below)
    const key = (code, down) => { keying = true; try { document.dispatchEvent(new KeyboardEvent(down ? 'keydown' : 'keyup', { code, key: code === 'Space' ? ' ' : code, bubbles: true })); } finally { keying = false; } };
    // while it plays, the game takes nothing from you: no keys that steer, jump or launch, no
    // click or tap on the playfield (a planned run must come out the same every time; a
    // click once gave level 10 five seconds). The menu, B and the other keys still work.
    const GAME = new Set(['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Space', 'Enter', 'NumpadEnter']);
    const block = e => { if (on && !keying && GAME.has(e.code)) { e.stopImmediatePropagation(); e.preventDefault(); } };
    window.addEventListener('keydown', block, true); window.addEventListener('keyup', block, true);
    const field = e => { if (on && e.target && (e.target.id === 'gameCanvas' || e.target.id === 'stage')) { e.stopImmediatePropagation(); e.preventDefault(); } };
    for (const t of ['mousedown', 'mouseup', 'pointerdown', 'pointerup', 'touchstart', 'click']) window.addEventListener(t, field, { capture: true, passive: false });
    function frame() {
        if (!on) return;
        if (currentLevel !== lv) { lv = currentLevel; frames = 0; running = false; best = null; show();
            seed = 12345 + lv * 7919 + (opt.pseed || 0) * 104729; fi = 0; step = 0; rec[lv] = []; lastX = rob.x; stuck = 0;        // each level planned on its own
            tape = opt.replay ? dec(opt.replay.levels[lv]) : null; }
        if (gameWon) { // the finish: the time stands; a moment of fireworks, then on (space)
            if (running) { running = false; splits[lv] = frames; }
            keysPressed.ArrowRight = keysPressed.ArrowUp = keysPressed.ArrowLeft = keysPressed.ArrowDown = false;
            oc.clearRect(0, 0, 1920, 1080);
            if (++wonFor > 75) { wonFor = 0; if (opt.level != null) stop(false); else if (lv >= levels.length - 1) stop(true); else { key('Space', true); key('Space', false); } }
            show(); return;
        }
        wonFor = 0;
        if (gameOver) { // it happens: back to the last checkpoint, the clock running on
            if (running) frames++;
            if (++deadFor > 45) { deadFor = 0; chargeAt = -1; best = null; resetLevel(); }
            show(); return;
        }
        deadFor = 0;
        if (!gameStarted && !running) { frames = 0; }
        let code;
        if (tape && fi < tape.length) { // the recorded run; its path ahead is played out from the tape
            code = tape[fi];
            if (fi % EVERY === 0 && window.RR3D) { const s = snap(); quiet(true); window.RRSim = true; bestPath = []; tapeAhead(fi); restore(s); window.RRSim = false; quiet(false); }
        } else {
            if (tape) { tape = null; best = null; }        // off the tape (should not happen): plan live from here
            if (step % EVERY === 0 || !best) plan();
            const p = best, f = step, k = keysAt(p, f);  // what the plan says for this frame
            code = (k.R ? 1 : 0) | (k.L ? 2 : 0) | (k.U ? 4 : 0) | (k.D ? 8 : 0) | (p.jumps.includes(f) || !gameStarted ? 16 : 0);
            let c = chargeAt;
            if (f === p.ls && c < 0) { code |= 32; c = 0; }
            if (c >= 0 && p.lr >= 0 && f >= p.lr) code |= 64;
            step++;
        }
        fi++; if (rec[lv]) rec[lv].push(code);
        act(code);
        if (gameStarted) { running = true; frames++; }
        // stuck for 15 s: start the level again
        if (Math.abs(rob.x - lastX) > 150) { lastX = rob.x; stuck = 0; } else if (++stuck > 900) { stuck = 0; resetLevel(); }
        // the path it means to take
        oc.clearRect(0, 0, 1920, 1080);
        if (window.RR3D && RR3D.toScreen) {
            const pts = [];
            for (let i = 0; i < bestPath.length; i += 2) pts.push(RR3D.toScreen(bestPath[i], bestPath[i + 1]));
            oc.lineCap = 'round'; oc.setLineDash([2, 16]);
            oc.beginPath(); pts.forEach((s, i) => i ? oc.lineTo(s.x, s.y) : oc.moveTo(s.x, s.y));
            oc.strokeStyle = 'rgba(0,0,0,0.45)'; oc.lineWidth = 13; oc.stroke();   // a dark edge, so it reads on snow too
            oc.strokeStyle = 'rgba(255,236,110,0.95)'; oc.lineWidth = 8; oc.stroke();
            oc.setLineDash([]);
        }
        if (frames % 6 === 0) show();
    }
    // B: the run planned beforehand (routes.js), or the live bot when there is none;
    // shift-B: the live bot on levels as they come. ?bot=1 / ?bot=live start one at once.
    const planned = () => window.RRRoutes ? { replay: RRRoutes } : {};
    window.addEventListener('keydown', e => { if (e.code === 'KeyB') { if (on) stop(false); else start(e.shiftKey ? {} : planned()); } });
    if (/[?&]bot=1/.test(location.search)) setTimeout(() => start(planned()), 600);
    if (/[?&]bot=live/.test(location.search)) setTimeout(() => start({}), 600);
    window.RRBot = { frame, start, stop, report, startPlanned: () => start(planned()), startLive: () => start({}), get planned() { return !!window.RRRoutes; }, get on() { return on; }, get planMs() { return planMs; }, get n() { return N; }, get splits() { return splits; }, get routes() { return rec.map(enc); }, enc, dec, get level() { return lv; },
        sim: { snap, restore, quiet, doJump } }; // for ghost.js
})();
