// ---- sound effects, made in the browser with Web Audio (no sample files).
// The game is not changed for this: once per game frame RRSfx.frame() looks
// at the game's own state (rob, fireballs, checkpoints, beamActive, gameWon,
// gameOver, isMouseDown ...) and plays what changed. Explosions and fireworks
// come through the hooks the 3D world already has. S turns the effects on or
// off (kept in localStorage); they start with the first key or click, like
// the music. Sounds are panned to where they happen on the screen.
(function () {
    const load = (k, d) => { try { const v = localStorage.getItem('rr3d.' + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } };
    const save = (k, v) => { try { localStorage.setItem('rr3d.' + k, JSON.stringify(v)); } catch (e) {} };
    let on = load('sfx', true), ac = null, out = null, noiseBuf = null, beamSnd = null, lastBoing = 0;
    const LEVEL = 0.55;

    function init() {
        if (ac || !on) return;
        const AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) return;
        ac = new AC();
        const comp = ac.createDynamicsCompressor();
        comp.threshold.value = -16; comp.ratio.value = 4; comp.attack.value = 0.003; comp.release.value = 0.2;
        out = ac.createGain(); out.gain.value = LEVEL;
        out.connect(comp); comp.connect(ac.destination);
        noiseBuf = ac.createBuffer(1, ac.sampleRate * 2, ac.sampleRate);
        const d = noiseBuf.getChannelData(0);
        for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
    }
    const ready = () => on && ac && ac.state === 'running';

    // where on the screen, -1 left .. 1 right
    function panOf(x) { return x === undefined ? 0 : Math.max(-0.9, Math.min(0.9, (x - cameraX - 960) / 1100)); }
    function chain(gain, pan) {
        const g = ac.createGain(); g.gain.value = 0;
        let last = g;
        if (ac.createStereoPanner && pan) { const p = ac.createStereoPanner(); p.pan.value = pan; g.connect(p); last = p; }
        last.connect(out);
        return g;
    }
    function env(g, t, a, peak, dur) { // attack, then an exponential fall
        g.gain.setValueAtTime(0.0001, t);
        g.gain.exponentialRampToValueAtTime(peak, t + a);
        g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    }
    // a tone that slides from f0 to f1
    function tone(f0, f1, dur, type, vol, opt = {}) {
        if (!ready()) return;
        const t = ac.currentTime + (opt.delay || 0), g = chain(vol, opt.pan);
        const o = ac.createOscillator(); o.type = type;
        o.frequency.setValueAtTime(f0, t);
        o.frequency.exponentialRampToValueAtTime(Math.max(20, f1), t + dur * (opt.slide || 1));
        if (opt.vib) { const l = ac.createOscillator(), lg = ac.createGain(); l.frequency.value = opt.vib; lg.gain.value = f0 * 0.03; l.connect(lg); lg.connect(o.frequency); l.start(t); l.stop(t + dur + 0.05); }
        o.connect(g); env(g, t, opt.a || 0.005, vol, dur);
        o.start(t); o.stop(t + dur + 0.05);
    }
    // filtered noise, the filter sliding from c0 to c1
    function noise(c0, c1, dur, vol, opt = {}) {
        if (!ready()) return;
        const t = ac.currentTime + (opt.delay || 0), g = chain(vol, opt.pan);
        const s = ac.createBufferSource(); s.buffer = noiseBuf; s.playbackRate.value = opt.rate || 1;
        const f = ac.createBiquadFilter(); f.type = opt.type || 'lowpass'; f.Q.value = opt.q || 0.8;
        f.frequency.setValueAtTime(c0, t); f.frequency.exponentialRampToValueAtTime(Math.max(30, c1), t + dur);
        s.connect(f); f.connect(g); env(g, t, opt.a || 0.004, vol, dur);
        s.start(t, Math.random() * 1.5); s.stop(t + dur + 0.05);
    }

    // ---- the sounds
    const S = {
        jump(n) { tone(n > 1 ? 330 : 220, n > 1 ? 880 : 620, 0.16, 'square', 0.12); tone(n > 1 ? 660 : 440, n > 1 ? 1500 : 1100, 0.12, 'sine', 0.08); },
        launch(p) { noise(400, 2600 + p * 3000, 0.25 + p * 0.25, 0.25 + p * 0.25, { type: 'bandpass', q: 1.2 }); tone(160, 500 + p * 700, 0.22 + p * 0.2, 'sawtooth', 0.07 + p * 0.06); },
        land(k) { tone(140, 48, 0.14, 'sine', 0.25 + 0.5 * k); noise(900, 120, 0.09, 0.15 + 0.35 * k); },
        boing(x) { tone(180, 520, 0.22, 'triangle', 0.25, { vib: 28, pan: panOf(x), slide: 0.6 }); },
        boom(x) {
            const pan = panOf(x);
            noise(2600, 70, 1.1, 0.95, { pan });
            noise(6000, 600, 0.18, 0.4, { pan, type: 'highpass' });
            tone(110, 32, 0.7, 'sine', 0.8, { pan });
        },
        spit(x) { const pan = panOf(x); noise(300, 2400, 0.35, 0.35, { pan, type: 'bandpass', q: 1.5, a: 0.06 }); noise(1500, 300, 0.5, 0.15, { pan, delay: 0.1 }); },
        checkpoint() { [784, 1047, 1319].forEach((f, i) => tone(f, f, 0.32, 'triangle', 0.18, { delay: i * 0.09 })); },
        win() {
            const notes = [523, 659, 784, 1047, 784, 1047, 1319];
            const at = [0, 0.11, 0.22, 0.33, 0.5, 0.61, 0.72];
            notes.forEach((f, i) => { tone(f, f, i === notes.length - 1 ? 0.8 : 0.16, 'square', 0.09, { delay: at[i] }); tone(f / 2, f / 2, 0.16, 'triangle', 0.12, { delay: at[i] }); });
            noise(8000, 3000, 0.6, 0.08, { type: 'highpass', delay: 0.72 });
        },
        rocket(x) { noise(800, 5000, 0.55, 0.09, { pan: panOf(x), type: 'bandpass', q: 3, a: 0.2 }); },
        burst(x) {
            const pan = panOf(x);
            noise(3000, 150, 0.6, 0.45, { pan });
            for (let i = 0; i < 9; i++) noise(9000, 4000, 0.03, 0.12, { pan: pan + (Math.random() - 0.5) * 0.4, type: 'highpass', delay: 0.15 + Math.random() * 0.6 });
        },
        fall() { tone(900, 110, 0.9, 'sine', 0.2, { vib: 9 }); },
        beamOn(x) { // a hum that rises while the beam pulls, until beamOff
            if (!ready()) return;
            S.beamOff();
            const t = ac.currentTime, g = chain(1, panOf(x));
            g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(0.14, t + 0.3);
            const a = ac.createOscillator(), b = ac.createOscillator(), l = ac.createOscillator(), lg = ac.createGain();
            a.type = 'sine'; a.frequency.value = 240; b.type = 'triangle';
            b.frequency.setValueAtTime(360, t); b.frequency.exponentialRampToValueAtTime(960, t + 3);
            l.frequency.value = 6; lg.gain.value = 8; l.connect(lg); lg.connect(a.frequency); lg.connect(b.frequency);
            const bg = ac.createGain(); bg.gain.value = 0.45; b.connect(bg); bg.connect(g); a.connect(g);
            [a, b, l].forEach(o => { o.start(t); o.stop(t + 3.5); });
            beamSnd = g;
        },
        beamOff() {
            if (!beamSnd || !ac) return;
            const t = ac.currentTime; beamSnd.gain.cancelScheduledValues(t);
            beamSnd.gain.setValueAtTime(Math.max(0.0001, beamSnd.gain.value), t); beamSnd.gain.exponentialRampToValueAtTime(0.0001, t + 0.12);
            beamSnd = null;
        },
        beamDrop() { tone(1200, 160, 0.5, 'sawtooth', 0.08); noise(5000, 300, 0.4, 0.12); },
        charge(p) { tone(300 + p * 900, 300 + p * 900, 0.05, 'square', 0.035 + p * 0.03); },
    };

    // ---- once per game frame: compare with the last frame
    let L = null, chargeT = 0, firedThisFrame = 0;
    function snap() {
        return {
            jumps: rob.jumpsUsed, onGround: rob.onGround, vy: rob.vy, x: rob.x,
            fb: fireballs.length, cps: checkpoints.reduce((n, c) => n + (c.activated ? 1 : 0), 0),
            beam: beamActive, won: gameWon, over: gameOver, mouse: isMouseDown, md: mouseDownTime,
            level: currentLevel, booms: L ? L.booms : 0,
        };
    }
    let boomsSince = 0, nearMonster = false, nearSaucer = false;
    function frame() {
        if (!ac || !on || typeof rob === 'undefined') { L = null; return; }
        const N = snap();
        if (!L || N.level !== L.level) { L = N; boomsSince = 0; S.beamOff(); return; }
        if (N.jumps > L.jumps) { if (L.mouse && !N.mouse) S.launch(Math.min(L.md / 30, 1)); else S.jump(N.jumps); }
        // landing: it was in the air and fell with some speed
        if (N.onGround && !L.onGround && L.vy > 2.5) S.land(Math.min(1, (L.vy - 2.5) / 14));
        // monsters and saucers bounce Rob: the same boxes the game tests
        let m = false, s = false;
        for (const o of obstacles) if (rob.x + rob.radius > o.x && rob.x - rob.radius < o.x + o.width && rob.y + rob.radius > o.y && rob.y - rob.radius < o.y + o.height) { m = true; break; }
        for (const u of saucers) if (rob.x + rob.radius > u.x - 40 && rob.x - rob.radius < u.x + 40 && rob.y + rob.radius > u.y - 20 && rob.y - rob.radius < u.y + 20) { s = true; break; }
        const now = performance.now(); // one boing per bump, not one per frame stuck against it
        if (((m && !nearMonster) || (s && !nearSaucer)) && now - lastBoing > 600) { lastBoing = now; S.boing(rob.x); }
        nearMonster = m; nearSaucer = s;
        if (N.fb >= L.fb + 3) { // Julian spits three at a time
            const j = julians.reduce((b, q) => !b || Math.abs(q.x - rob.x) < Math.abs(b.x - rob.x) ? q : b, null);
            if (j && Math.abs(j.x - cameraX - 960) < 1300) S.spit(j.x);
        }
        if (N.cps > L.cps) S.checkpoint();
        if (N.beam && !L.beam) S.beamOn(beamTarget ? beamTarget.x : rob.x);
        if (!N.beam && L.beam) { S.beamOff(); if (!N.over) S.beamDrop(); }
        if (N.won && !L.won) S.win();
        else if (N.over && !L.over && !N.won && boomsSince === 0) S.fall();
        if (N.mouse && !N.over) { if (++chargeT % Math.max(3, 12 - Math.floor(N.md / 3)) === 0) S.charge(Math.min(N.md / 30, 1)); } else chargeT = 0;
        if (!N.over) boomsSince = 0;
        firedThisFrame = 0;
        L = N;
    }
    // from the hooks in the game (createExplosion, createFirework) and the 3D world
    let lastBoom = 0;
    function boom(x) {
        boomsSince++;
        const now = performance.now();
        if (now - lastBoom < 60) return; // two at once (Rob and the mine): one bang
        lastBoom = now; S.boom(x);
    }
    function firework(x) { if (++firedThisFrame <= 1) S.rocket(x); }

    function set(v) { on = !!v; save('sfx', on); if (on) start(); if (ac) out.gain.value = on ? LEVEL : 0; }
    function start() { init(); if (ac && ac.state === 'suspended') ac.resume().catch(() => {}); }
    window.addEventListener('keydown', e => {
        if (e.code === 'KeyS' && !e.repeat) {
            set(!on);
            const lab = document.getElementById('music3d');
            if (lab) { lab.textContent = on ? 'sound effects on' : 'sound effects off'; lab.style.opacity = 0.85; setTimeout(() => { lab.style.opacity = 0; }, 2500); }
            return;
        }
        start();
    }, true);
    window.addEventListener('pointerdown', start, true);
    document.addEventListener('visibilitychange', () => { if (!ac) return; if (document.hidden) ac.suspend(); else if (on) ac.resume(); });
    window.RRSfx = { set, get on() { return on; }, frame, boom, firework, burst: x => S.burst(x), S, get ctx() { return ac; } };
})();
