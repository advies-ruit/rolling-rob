// ---- ghost Rob: race against your own best run of a level, or the planned speedrun.
// Every level comes from a seed (a random one per page load unless ?seed=N), so a
// level can be played again exactly. While you play, your input is recorded per game
// step (60 a second): arrows, jumps, launches with their power, resets. Finishing a
// level faster than before keeps that run (localStorage, per seed and level). When a
// level starts and there is a run for it, the game's own physics plays it through
// once, out of sight (state saved and put back, as the bot's look-ahead does), and
// the ghost then rolls along beside you, step for step from your start. On the seed
// of the planned speedrun (routes.js) the bot's run is a ghost too.
// A run can be shared: L shows a link with your best runs of this seed (#run=...);
// whoever opens it gets the same levels with your ghost (pink, with your name). The
// same panel takes a pasted link or code, for where a link loses its # part.
// G: ghosts on/off; shift-G: the planned speedrun's seed, from level 1; L: share.
(function () {
    const load = (k, d) => { try { const v = localStorage.getItem('rr3d.' + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } };
    const save = (k, v) => { try { localStorage.setItem('rr3d.' + k, JSON.stringify(v)); } catch (e) {} };
    const runKey = (seed, lv) => 'ghost.s' + seed + '.l' + lv;
    let show = load('ghost.on', true);
    // ---- a shared run in the address: #run=1.<seed>.<name>.<tape level 1>~<tape level 2>~...
    // (tapes as bot.js writes them: base36 code*count, comma separated; empty = not played)
    const ART = 'https://claude.ai/artifact/UaP8KHKWzg8upxBr9a7UsX'; // this page as published (it runs in a frame there)
    const esc = v => String(v).replace(/[&<>"']/g, c => '&#' + c.charCodeAt(0) + ';');
    function parseCode(code) { // what comes from outside is checked before anything uses it
        const f = String(code).trim().split('.');
        if (f.length !== 4 || f[0] !== '1' || !/^\d{1,9}$/.test(f[1])) return null;
        let name = ''; try { name = decodeURIComponent(f[2]).slice(0, 24); } catch (e) { return null; }
        const tapes = f[3].split('~');
        if (tapes.length > 20 || !tapes.some(Boolean) || !tapes.every(t => steps(t) >= 0)) return null;
        return { seed: +f[1], name, tapes };
    }
    // packed: '2.' + the code above deflated (the browser's own CompressionStream) in
    // link-safe base64 -- about half as long. Unpacking is asynchronous and stops at 300 KB.
    const b64 = u => btoa(String.fromCharCode(...u)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
    const unb64 = s => Uint8Array.from(atob(s.replace(/-/g, '+').replace(/_/g, '/')), c => c.charCodeAt(0));
    async function pack(code) {
        if (!window.CompressionStream) return code;
        try {
            const out = await new Response(new Blob([code]).stream().pipeThrough(new CompressionStream('deflate-raw'))).arrayBuffer();
            const p = '2.' + b64(new Uint8Array(out));
            return p.length < code.length ? p : code;
        } catch (e) { return code; }
    }
    async function unpack(code) { // -> the plain code, or null
        code = String(code).trim();
        if (!code.startsWith('2.')) return code;
        if (!window.DecompressionStream || code.length > 100000) return null;
        try {
            const r = new Blob([unb64(code.slice(2))]).stream().pipeThrough(new DecompressionStream('deflate-raw')).getReader();
            const parts = []; let n = 0;
            for (;;) { const { done, value } = await r.read(); if (done) break; if ((n += value.length) > 300000) { r.cancel(); return null; } parts.push(value); }
            return new TextDecoder().decode(await new Blob(parts).arrayBuffer());
        } catch (e) { return null; }
    }
    const runIn = a => { const m = /[#?&]run=([^&#\s]+)/.exec(a || ''); return m ? m[1] : null; };
    const fromCode = async c => c == null ? null : parseCode(await unpack(c));
    const fromAddress = a => fromCode(runIn(a));
    const fromText = a => fromCode(runIn(a) ?? a); // a whole link, or the code alone
    function steps(t) { // a tape from a link: its own characters only, at most 30000 steps (-1: refused)
        if (t.length > 60000) return -1;
        let n = 0;
        for (const r of t ? t.split(',') : []) {
            const [c, k, x] = r.split('*');
            if (x !== undefined || !/^[0-9a-z]{1,4}$/.test(c) || (k !== undefined && !/^[0-9a-z]{1,4}$/.test(k))) return -1;
            if ((n += k ? parseInt(k, 36) : 1) > 30000) return -1;
        }
        return n;
    }
    const safeDec = t => steps(t) >= 0 ? window.RRBot.dec(t) : null;
    // a plain code in the address is read at once, so the first level is already the
    // shared one; a packed one when it is unpacked (a moment later)
    let shared = null;
    { const c = runIn(location.hash) ?? runIn(location.search) ?? runIn(document.referrer);
      if (c && !c.startsWith('2.')) { shared = parseCode(c); if (shared && window.RRSeed) { RRSeed.seed = shared.seed; show = true; } }
      else if (c) fromCode(c).then(r => { if (r) race(r); }); }
    // a seed for every page load (the original makes new levels each time; so does this,
    // but now each level can be played again exactly). ?seed=N picks one.
    if (window.RRSeed && RRSeed.seed == null) RRSeed.seed = 1 + Math.floor(Math.random() * 999999);

    // ---- recording the player. Human tapes: 1 right, 2 left, 4 up, 8 down, 16 space
    // (a jump), 32 reset, 64 + power*30 << 7 a launch with that power.
    let pend = 0, pendPow = -1, wrapped = false;
    const typing = e => e.target && /^(INPUT|TEXTAREA)$/.test(e.target.tagName);
    window.addEventListener('keydown', e => {
        if (typing(e)) return;
        if (e.code === 'Space' && !isSpaceDown && !gameOver && !gameWon && !window.RRSim) pend |= 16;
    }, true); // before the game's own handler, which sets isSpaceDown
    function wrap() { // pushRob and resetLevel are the game's; wrapped once it has defined them
        if (wrapped || typeof pushRob !== 'function') return;
        wrapped = true;
        const push = window.pushRob, reset = window.resetLevel, load = window.loadLevel;
        // the ghosts of a level are worked out as it is loaded, before any input reaches it
        let loading = false; // the reset inside loadLevel is not one of yours
        window.loadLevel = function () { loading = true; try { load.apply(this, arguments); } finally { loading = false; } if (!window.RRSim) newLevel(); };
        window.pushRob = function (p) { if (!window.RRSim && !(window.RRBot && RRBot.on)) pendPow = p; return push.apply(this, arguments); };
        window.resetLevel = function () { if (!loading && !window.RRSim && !(window.RRBot && RRBot.on)) pend |= 32; return reset.apply(this, arguments); };
    }

    setTimeout(wrap, 0); // the game's script, after this one, has defined them by then
    let lv = -1, tape = [], t = -1, done = false, ghosts = [], note = '', noteT = 0, levelStart = null;
    const B = () => window.RRBot;

    // ---- a ghost's track: its run played through by the game's physics, out of sight
    // positions are kept per step of the level's clock, which runs from the first
    // move to the finish and stands still after a reset until the next move -- the
    // same clock as the player's, so ghost and player are compared at equal times
    // the original runs the UFO's tractor beam inside its drawing (drawBeam): it pulls,
    // slows and abducts Rob there, so a replay must run it too -- drawn far off the canvas
    function beam() {
        if (!beamActive || !beamTarget) return;
        ctx.save(); ctx.translate(1e7, 1e7);
        try { drawBeam(); } finally { ctx.restore(); }
    }
    function track(codes, kind) {
        const S = B().sim, s = S.snap(); S.quiet(true); window.RRSim = true;
        const xs = [], ys = [], rs = [];
        let i = 0, dead = 0, charge = -1, tt = -1, time = null;
        const keep = () => { if (tt >= 0) { xs[tt] = rob.x; ys[tt] = rob.y; rs[tt] = rob.rotation; } };
        xs[0] = rob.x; ys[0] = rob.y; rs[0] = rob.rotation;
        try {
            for (let k = 0; k < 30000; k++) {
                if (gameWon) { time = tt + 1; break; }
                if (kind === 'bot' && gameOver) { if (++dead > 45) { dead = 0; charge = -1; resetLevel(); } } // as the bot does
                else {
                    dead = 0;
                    if (i >= codes.length) break;
                    const c = codes[i++];
                    if (kind !== 'bot' && (c & 32)) resetLevel(); // first: it lets go of the keys
                    keysPressed.ArrowRight = !!(c & 1); keysPressed.ArrowLeft = !!(c & 2); keysPressed.ArrowUp = !!(c & 4); keysPressed.ArrowDown = !!(c & 8);
                    if (kind === 'bot') { // as RRBot's act()
                        if ((c & 16) && !gameOver && !gameWon) S.doJump();
                        if ((c & 32) && charge < 0) { charge = 0; isMouseDown = true; mouseDownTime = 0; if (!gameStarted) gameStarted = true; }
                        if (charge >= 0 && (c & 64)) { isMouseDown = false; mouseDownTime = 0; pushRob(Math.min(charge / 30, 1)); charge = -1; }
                        else if (charge >= 0) { charge++; mouseDownTime = charge; }
                    } else {
                        if ((c & 16) && !gameOver && !gameWon) S.doJump();
                        if (c & 64) pushRob(Math.min((c >> 7) / 30, 1));
                    }
                }
                if (gameStarted && !gameWon) tt++;
                keep();
                beam();
                updatePhysics();
            }
        } finally { S.restore(s); window.RRSim = false; S.quiet(false); }
        const mx = []; let m = -Infinity; // the furthest the ghost had come at each step
        for (let j = 0; j < xs.length; j++) { if (xs[j] != null && xs[j] > m) m = xs[j]; mx[j] = m; }
        return { kind, xs, ys, rs, mx, time };
    }
    // ---- a live tape (mp.js): played on as its codes come in, from the level's start,
    // with the same steps as track(); p keeps where it got to ({state, k}, the position
    // after each step in xs/ys/rs when given, and in px as each step begins), so each new
    // piece costs only its own steps. time: steps to the finish.
    function live(p, codes) {
        if (!levelStart || p.time != null || (p.k || 0) >= codes.length) return p;
        const S = B().sim, mine = S.snap(); S.quiet(true); window.RRSim = true;
        // the snapshot keeps only the confetti's length: after your own finish (confetti
        // flying) a replay would leave holes in it, so the celebrations are kept whole here
        const conf = confetti.slice(), fw = typeof fireworks !== 'undefined' ? fireworks.slice() : null;
        try {
            S.restore(p.state || levelStart); p.k = p.k || 0;
            while (p.k < codes.length && !gameWon) {
                const c = codes[p.k++];
                if (c & 32) resetLevel();
                if (p.px) p.px[p.k - 1] = rob.x; // where Rob is as this step begins (after a reset): mp.js's fire wall
                keysPressed.ArrowRight = !!(c & 1); keysPressed.ArrowLeft = !!(c & 2); keysPressed.ArrowUp = !!(c & 4); keysPressed.ArrowDown = !!(c & 8);
                if ((c & 16) && !gameOver && !gameWon) S.doJump();
                if (c & 64) pushRob(Math.min((c >> 7) / 30, 1));
                beam(); updatePhysics();
                if (p.xs) { p.xs[p.k - 1] = rob.x; p.ys[p.k - 1] = rob.y; p.rs[p.k - 1] = rob.rotation; }
            }
            if (gameWon) p.time = p.k;
            p.x = rob.x; p.y = rob.y; p.r = rob.rotation; p.dead = gameOver;
            p.state = S.snap();
        } finally {
            S.restore(mine); window.RRSim = false; S.quiet(false);
            confetti.length = 0; confetti.push(...conf);
            if (fw) { fireworks.length = 0; fireworks.push(...fw); }
        }
        return p;
    }

    // ---- a whole run, level 1 to 10 in one go: its time is the levels' times added up
    // (as the bot's splits). It goes on only to the next level after a finish; any other
    // level (the menu, a number key, R) ends it; level 1 always starts a new one.
    let run = null, runNext = -1;
    const runKeyAll = seed => 'run.s' + seed;
    const cum = (a, n) => a.slice(0, n).reduce((x, y) => x + y, 0);
    function runLevel() {
        if (currentLevel === 0) run = { seed: RRSeed.seed, splits: [] };
        else if (!(run && currentLevel === runNext && run.seed === RRSeed.seed)) run = null;
        runNext = -1;
    }
    function runDone(time) { // after a level's finish: its split, and the run so far against your best run
        if (!run) return '';
        run.splits[lv] = time; runNext = lv + 1;
        const best = load(runKeyAll(run.seed), null), n = lv + 1, sum = cum(run.splits, n);
        let txt = ` · run ${n}/${levels.length}: ${(sum / 60).toFixed(2)} s`;
        if (best && best.splits.length >= n) { const d = (sum - cum(best.splits, n)) / 60; txt += ` (${d > 0 ? '+' : '−'}${Math.abs(d).toFixed(2)})`; }
        if (n === levels.length) {
            if (!best || sum < best.total) { save(runKeyAll(run.seed), { total: sum, splits: run.splits.slice() }); txt += best ? ' - new run record!' : ' - your first full run'; }
            else txt += ` - runrecord ${(best.total / 60).toFixed(2)} s`;
            run = null;
        }
        return txt;
    }

    function newLevel() {
        lv = currentLevel; tape = []; t = -1; done = false; ghosts = [];
        runLevel();
        const seed = RRSeed.seed;
        if (seed == null || !B() || !B().sim) return;
        levelStart = B().sim.snap(); // as loaded, before any input: where live tapes start
        const mine = load(runKey(seed, lv), null);
        if (mine) ghosts.push(track(B().dec(mine.tape), 'me'));
        if (shared && shared.seed === seed && shared.tapes[lv]) { const c = safeDec(shared.tapes[lv]); if (c) ghosts.push(Object.assign(track(c, 'friend'), { name: shared.name })); }
        const R = window.RRRoutes;
        if (R && R.seed === seed && R.levels[lv]) ghosts.push(track(B().dec(R.levels[lv]), 'bot'));
    }

    function finish() {
        done = true;
        const seed = RRSeed.seed, old = load(runKey(seed, lv), null);
        const time = t + 1, s = (time / 60).toFixed(2), r = runDone(time);
        if (!old || time < old.t) save(runKey(seed, lv), { t: time, tape: B().enc(tape) });
        // the menu shows a finish card (time, record, the run); without it, a line
        if (window.RRMenu && RRMenu.finished) RRMenu.finished({ lv, time, old: old ? old.t : null, run: r.replace(/^ · /, '') });
        else if (!old || time < old.t)
            say(old ? `level ${lv + 1}: ${s} s, new record (${((time - old.t) / 60).toFixed(2)} s)${r} - L: share your run`
                    : `level ${lv + 1}: ${s} s${r} - your ghost rides along next time, L: share your run`);
        else say(`level ${lv + 1}: ${s} s (record ${(old.t / 60).toFixed(2)} s)${r}`);
    }

    // ---- what is on screen: a label, and the ghosts (3D figures, or circles without 3D)
    const label = document.createElement('div'); label.id = 'ghost3d';
    label.style.cssText = 'display:none;position:fixed;left:50%;transform:translateX(-50%);top:74px;z-index:15;pointer-events:none;' +
        'font:15px monospace;color:#fff;background:rgba(0,0,0,.55);padding:5px 12px;border-radius:8px;text-shadow:1px 1px 2px #000;max-width:94vw;box-sizing:border-box;text-align:center';
    document.body.appendChild(label);
    const ov = document.createElement('canvas'); ov.width = 1920; ov.height = 1080;
    ov.style.cssText = 'position:absolute;left:0;top:0;width:100%;height:100%;z-index:2;pointer-events:none';
    let placed = false;
    function say(s) { note = s; noteT = performance.now(); }
    const COL = { me: '#5fd0ff', bot: '#ffd23c', friend: '#ff7ad9' }, NAME = { me: 'your best', bot: 'planned speedrun', friend: 'shared run' };
    function draw(list) {
        if (!placed) { (document.getElementById('stage') || document.body).appendChild(ov); placed = true; }
        const c = ov.getContext('2d'); c.clearRect(0, 0, 1920, 1080);
        if (window.RR3D) { if (RR3D.ghosts) RR3D.ghosts(list); return; }
        const wx = window.RRNet && RRNet.wallX; // a race's fire wall (mp.js), as a glow from the left
        if (wx != null && wx - cameraX > 0) {
            const w = Math.min(1920, wx - cameraX), gr = c.createLinearGradient(w - 260, 0, w, 0);
            gr.addColorStop(0, 'rgba(200,40,0,.75)'); gr.addColorStop(0.7, 'rgba(255,140,20,.8)'); gr.addColorStop(1, 'rgba(255,220,120,0)');
            c.fillStyle = 'rgba(160,30,0,.7)'; c.fillRect(0, 0, Math.max(0, w - 260), 1080);
            c.fillStyle = gr; c.fillRect(Math.max(0, w - 260), 0, Math.min(w, 260), 1080);
        }
        for (const g of list) { // the original's view: plain circles on the 2D camera
            c.globalAlpha = 0.45; c.fillStyle = g.col || COL[g.kind];
            c.beginPath(); c.arc(g.x - cameraX, g.y - cameraY, rob.radius, 0, Math.PI * 2); c.fill(); c.globalAlpha = 1;
        }
        // the jumps left (rr3d.js shows them in 3D): from the first jump until landing
        if (rob.jumpsUsed > 0 && gameStarted && !gameOver && !gameWon) for (let i = 0; i < 2; i++) {
            const left = i < 2 - rob.jumpsUsed, R = rob.radius;
            c.fillStyle = left ? '#ffe14d' : '#555'; c.strokeStyle = '#111'; c.lineWidth = 4;
            c.beginPath(); c.arc(rob.x - cameraX + (i - 0.5) * R * 0.75, rob.y - cameraY - R * 1.8, R * (left ? 0.26 : 0.2), 0, Math.PI * 2); c.fill(); c.stroke();
        }
    }
    // how far you are ahead of a ghost: in time where it can (the ghost got where you are
    // now that many seconds earlier or later), else in the game's metres
    function gap(g) {
        const mx = g.mx;
        if (t >= 0 && mx && mx.length && rob.x <= mx[mx.length - 1]) {
            let lo = 0, hi = mx.length - 1; // the first step at which the ghost had come this far
            while (lo < hi) { const m = (lo + hi) >> 1; if (mx[m] >= rob.x) hi = m; else lo = m + 1; }
            const d = (t - lo) / 60;
            if (Math.abs(d) < 0.005) return 'level';
            return `<b style="color:${d < 0 ? '#6f6' : '#f77'}">${d < 0 ? '−' : '+'}${Math.abs(d).toFixed(2)} s</b>`;
        }
        const d = (rob.x - g.x) / 10;
        return Math.abs(d) < 1 ? 'level' : `${Math.abs(d).toFixed(0)} m ${d > 0 ? 'ahead' : 'behind'}`;
    }
    function text(list) {
        const now = performance.now(), parts = [];
        if (note && now - noteT < 5000) parts.push(note);
        else for (const g of list) {
            parts.push(`<span style="color:${g.col || COL[g.kind]}">●</span> ${esc(g.name || NAME[g.kind])}${g.time != null ? ' ' + (g.time / 60).toFixed(2) + ' s' : ''}: ` + (g.status || gap(g)));
        }
        if (run && t >= 0 && !(note && now - noteT < 5000) && lv > 0) parts.unshift(`run ${lv + 1}/${levels.length}: ${((cum(run.splits, lv) + t + 1) / 60).toFixed(2)} s`);
        const wx = window.RRNet && RRNet.wallX; // a race's fire wall (mp.js): how far behind you
        if (wx != null && !gameOver && !gameWon) { const m = (rob.x - wx) / 10; parts.unshift(`<b style="color:${m < 30 ? '#ff5a2a' : '#ffa040'}">fire ${Math.max(0, m).toFixed(0)} m behind you</b>`); }
        label.style.display = parts.length ? 'block' : 'none';
        label.innerHTML = parts.map(p => `<span style="white-space:nowrap">${p}</span>`).join(' &nbsp; ');
    }

    // once per game step, after the bot and before the physics (build.py calls it)
    function frame() {
        wrap();
        if (currentLevel !== lv) newLevel(); // the first level, loaded before this was wrapped
        const bot = B() && B().on;
        if (bot) { done = true; run = null; } // a level the bot played (in part) is not your run
        if (!done && RRSeed.seed != null) {
            if (gameWon) { if (t >= 0) finish(); else done = true; }
            else {
                let c = (keysPressed.ArrowRight ? 1 : 0) | (keysPressed.ArrowLeft ? 2 : 0) | (keysPressed.ArrowUp ? 4 : 0) | (keysPressed.ArrowDown ? 8 : 0) | pend;
                if (pendPow >= 0) c |= 64 | (Math.round(pendPow * 30) << 7);
                tape.push(c);
            }
        }
        pend = 0; pendPow = -1;
        if (gameStarted && !gameWon) t++;
        // the ghosts at the same time since their start as you since yours
        const list = [];
        if (show && !bot) for (const g of ghosts) {
            const i = Math.min(Math.max(t, 0), g.xs.length - 1);
            if (g.xs[i] != null) list.push({ kind: g.kind, x: g.xs[i], y: g.ys[i], r: g.rs[i], time: g.time, name: g.name, mx: g.mx });
        }
        if (window.RRNet && !bot) { RRNet.step(); for (const g of RRNet.ghosts()) list.push(g); } // the other racers, live
        draw(list); text(bot ? [] : list);
    }

    // ---- sharing: a panel with the link to your best runs of this seed, and a field for someone else's
    const panel = document.createElement('div'); panel.id = 'share3d';
    panel.style.cssText = 'display:none;position:fixed;left:50%;top:50%;transform:translate(-50%,-50%);z-index:40;width:min(92vw,560px);max-height:90vh;overflow:auto;' +
        'box-sizing:border-box;padding:16px 18px;border-radius:12px;background:rgba(18,22,30,.94);color:#eee;font:15px system-ui,sans-serif;box-shadow:0 8px 40px rgba(0,0,0,.5)';
    panel.innerHTML = `<div style="font-weight:600;font-size:17px;margin-bottom:4px">Share your run</div>
        <div data-f="info" style="opacity:.8;margin-bottom:10px"></div>
        <label style="display:block;margin-bottom:8px">Your name on your ghost <input data-f="name" maxlength="24" style="width:12em;font:inherit;padding:3px 6px;border-radius:6px;border:1px solid #555;background:#111;color:#eee"></label>
        <textarea data-f="link" readonly rows="3" style="width:100%;box-sizing:border-box;font:12px monospace;padding:6px;border-radius:6px;border:1px solid #555;background:#111;color:#cfe;resize:vertical"></textarea>
        <div style="display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 4px"><button data-a="copy">Copy link</button><button data-a="share">Share…</button><button data-a="close">Close</button><span data-f="msg" style="align-self:center;opacity:.85"></span></div>
        <div style="border-top:1px solid #333;margin:12px 0 8px"></div>
        <div style="font-weight:600;margin-bottom:6px">Race someone else</div>
        <textarea data-f="paste" rows="2" placeholder="paste a link or code here" style="width:100%;box-sizing:border-box;font:12px monospace;padding:6px;border-radius:6px;border:1px solid #555;background:#111;color:#eee;resize:vertical"></textarea>
        <div style="display:flex;gap:8px;margin-top:8px;align-items:center"><button data-a="load">Race it</button><span data-f="msg2" style="opacity:.85"></span></div>`;
    // the original styles every button (absolute, at the bottom, shifted on hover): not these
    for (const b of panel.querySelectorAll('button')) b.style.cssText = 'position:static;transform:none;font:inherit;padding:6px 12px;border-radius:7px;border:0;background:#2d6cdf;color:#fff;cursor:pointer';
    document.body.appendChild(panel);
    const F = n => panel.querySelector(`[data-f="${n}"]`);
    // keys stay in the panel only while you type in it; its buttons never take the focus,
    // and closing it lets go of any focus inside (else the space went to a hidden field)
    const hide = () => { panel.style.display = 'none'; if (panel.contains(document.activeElement)) document.activeElement.blur(); };
    panel.addEventListener('keydown', e => {
        if (e.key === 'Escape') { hide(); return; }
        if (panel.style.display !== 'none' && typing(e)) e.stopPropagation();
        else if (panel.contains(e.target) && e.target.blur) e.target.blur(); // a hidden or button focus: the key is the game's
    });
    panel.addEventListener('mousedown', e => { if (e.target.tagName === 'BUTTON') e.preventDefault(); });
    panel.addEventListener('pointerdown', e => e.stopPropagation());
    F('name').value = load('name', '');
    async function myRun() {
        const seed = RRSeed.seed, tapes = [], done = [];
        let total = 0;
        for (let l = 0; l < levels.length; l++) { const r = load(runKey(seed, l), null); tapes.push(r ? r.tape : ''); if (r) { done.push(l + 1); total += r.t; } }
        while (tapes.length && !tapes[tapes.length - 1]) tapes.pop();
        if (!done.length) return null;
        const name = F('name').value.trim().slice(0, 24);
        const code = await pack(['1', seed, encodeURIComponent(name).replace(/\./g, '%2E'), tapes.join('~')].join('.'));
        const base = window.top !== window ? ART : location.href.split('#')[0].split('?')[0];
        return { link: base + '#run=' + code, done, total };
    }
    async function fill() {
        const r = await myRun();
        F('info').textContent = r ? `seed ${RRSeed.seed} · level ${r.done.join(', ')} · together ${(r.total / 60).toFixed(2)} s`
                                  : `seed ${RRSeed.seed}: finish a level first, then you can share that run`;
        F('link').value = r ? r.link : '';
        panel.querySelector('[data-a="share"]').style.display = r && navigator.share ? '' : 'none';
        return r;
    }
    F('name').addEventListener('input', () => { save('name', F('name').value.trim().slice(0, 24)); fill(); });
    panel.addEventListener('click', async e => {
        const a = e.target.dataset && e.target.dataset.a; if (!a) return;
        const msg = s => { F('msg').textContent = s; };
        if (a === 'close') hide();
        if (a === 'copy') {
            const r = await fill(); if (!r) return;
            try { await navigator.clipboard.writeText(r.link); msg('copied'); }
            catch (err) { F('link').select(); const ok = document.execCommand && document.execCommand('copy'); if (ok) F('link').blur(); msg(ok ? 'copied' : 'select it and copy it yourself'); }
        }
        if (a === 'share') { const r = await fill(); if (r) try { await navigator.share({ title: 'Rolling Rob: race my ghost', url: r.link }); } catch (err) {} }
        if (a === 'load') {
            const r = await fromText(F('paste').value);
            if (!r) { F('msg2').textContent = 'that is not a run link or code'; return; }
            race(r); hide(); F('paste').value = ''; F('msg2').textContent = '';
        }
    });
    function race(r) { // the shared run's seed, from its first level
        shared = r;
        if (B() && B().on) B().stop(false);
        RRSeed.seed = r.seed; show = true;
        wrap(); loadLevel(Math.max(0, r.tapes.findIndex(Boolean)));
        say(`racing ${esc(r.name || 'a shared run')} (seed ${r.seed})`);
    }
    window.addEventListener('hashchange', async () => { const r = await fromAddress(location.hash); if (r) race(r); });
    function openShare() { if (window.RRMenu) RRMenu.close(); fill(); F('msg').textContent = ''; panel.style.display = 'block'; }
    window.addEventListener('keydown', e => {
        if (e.code !== 'KeyL' || e.repeat || typing(e)) return;
        if (panel.style.display === 'none') openShare(); else hide();
    });

    window.addEventListener('keydown', e => {
        if (e.code !== 'KeyG' || e.repeat || typing(e)) return;
        wrap();
        if (e.shiftKey && window.RRRoutes) { raceBot(); return; }
        setShow(!show);
    });
    function raceBot() { // the planned speedrun's levels, from level 1
        if (!window.RRRoutes) return;
        wrap();
        if (B() && B().on) B().stop(false);
        RRSeed.seed = RRRoutes.seed; show = true; save('ghost.on', true);
        loadLevel(0); say(`seed ${RRRoutes.seed}: racing the planned speedrun`);
    }
    function setShow(v) {
        show = !!v; save('ghost.on', show);
        say(show ? (ghosts.length ? 'ghosts on' : 'ghosts on - no run for this level yet') : 'ghosts off');
    }
    // for the menu: your best time per level of this seed, and your best whole run
    function bests() {
        const seed = RRSeed.seed, lv = levels.map((_, l) => { const r = load(runKey(seed, l), null); return r ? r.t : null; });
        return { seed, levels: lv, run: load(runKeyAll(seed), null) };
    }
    window.RRGhost = { frame, say, live, setShow, raceBot, openShare, bests, get show() { return show; }, get ghosts() { return ghosts; }, get tape() { return tape; }, get t() { return t; }, get run() { return run; }, get level() { return lv; } };
})();
