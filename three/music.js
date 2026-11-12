// ---- music, as in the LVGL desktop/web build: the tracks as a shuffled
// playlist, M on/off, N next, - and + volume (default 70), a short fade on a
// skip. The choice and the volume are kept in localStorage. Browsers start
// audio only after a gesture, so the first key or click starts it.
(function () {
    const TRACKS = ['01_arcade_dash', '02_arcade_dash_2', '03_arcade_dash_3', '04_arcade_dash_4', '05_arcade_dash_video', '06_arcade_rush'];
    const TITLE = n => n.replace(/^\d+_/, '').replace(/_/g, ' ').replace(/\b\w/g, c => c.toUpperCase()).replace('Video', '(video)');
    const FADE = 0.35;
    const load = (k, d) => { try { const v = localStorage.getItem('rr3d.' + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } };
    const save = (k, v) => { try { localStorage.setItem('rr3d.' + k, JSON.stringify(v)); } catch (e) {} };
    let on = load('music', true), vol = load('volume', 70);
    let order = [], pos = -1, started = false, gain = 0, skipping = false, last = -1;
    const audio = new Audio();
    audio.preload = 'auto';
    const label = document.createElement('div');
    label.id = 'music3d';
    document.body.appendChild(label);
    let labelT = 0;
    function show(text) { label.textContent = text; label.style.opacity = 0.85; labelT = performance.now(); }
    function shuffle() {
        order = TRACKS.map((_, i) => i);
        for (let i = order.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [order[i], order[j]] = [order[j], order[i]]; }
        if (order.length > 1 && order[0] === last) [order[0], order[1]] = [order[1], order[0]];
    }
    function next(full) {
        if (++pos >= order.length || !order.length) { shuffle(); pos = 0; }
        last = order[pos];
        audio.src = 'music/' + TRACKS[last] + '.mp3';
        gain = full ? 1 : 0;
        audio.volume = on ? gain * vol / 100 : 0;
        audio.play().catch(() => {});
        if (on) show('♪ ' + TITLE(TRACKS[last]));
    }
    function set(v) {
        on = !!v; save('music', on);
        if (!started) start();
        if (on) { audio.play().catch(() => {}); show('♪ music on: ' + TITLE(TRACKS[last])); }
        else { audio.pause(); show('music off'); }
    }
    function skip() { start(); if (!on) { on = true; save('music', on); } skipping = true; }
    function volume(v) {
        vol = Math.max(0, Math.min(100, Math.round(v))); save('volume', vol);
        show('volume ' + vol + '%');
        if (!started) start();
    }
    audio.addEventListener('ended', () => next(true));
    audio.addEventListener('error', () => { if (started) setTimeout(() => next(true), 500); });
    function start() {
        if (started) return;
        started = true;
        next(true); // the first track at full volume, as in the LVGL build
    }
    // the volume follows on/off and the skip fade, a little every 30 ms
    // (a timer, not animation frames: those stop in a hidden window)
    let prev = performance.now();
    function tick() {
        const now = performance.now();
        const dt = Math.min(0.1, (now - prev) / 1000); prev = now;
        if (started) {
            const want = skipping ? 0 : 1;
            gain += Math.sign(want - gain) * Math.min(Math.abs(want - gain), dt / FADE);
            if (skipping && gain <= 0) { skipping = false; next(true); }
            const v = on ? gain * vol / 100 : 0;
            if (Math.abs(audio.volume - v) > 0.001) audio.volume = Math.max(0, Math.min(1, v));
            if (on && audio.paused && !document.hidden && audio.src) audio.play().catch(() => {});
        }
        if (labelT && now - labelT > 4000) { label.style.opacity = 0; labelT = 0; }
    }
    setInterval(tick, 30);
    window.addEventListener('keydown', e => {
        if (e.code === 'KeyM' && !e.repeat) { set(!on); return; }
        if (e.code === 'KeyN' && !e.repeat) { skip(); return; }
        if (e.key === '-' || e.key === '+' || e.key === '=') { volume(vol + (e.key === '-' ? -10 : 10)); return; }
        if (on) start();
    }, true);
    window.addEventListener('pointerdown', () => { if (on) start(); }, true);
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) audio.pause(); else if (on && started) audio.play().catch(() => {});
    });
    window.RRMusic = { set, skip, volume, get vol() { return vol; }, get on() { return on; }, get title() { return last >= 0 ? TITLE(TRACKS[last]) : ''; }, audio };
})();
