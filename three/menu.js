// ---- the menu: one place for everything that is not the game itself.
// While playing only the game is on screen, a menu button at the top right and (if
// wanted) one status line at the bottom. Esc, the button, a gamepad's Start or leaving
// the window opens the menu, which pauses the game (qol.js). It has a main page and
// sub pages, at most two deep: levels, speedrun, settings (view, sound, controls, app).
// Mouse, touch, the arrow keys with Enter, and a gamepad all work in it.
// The other modules keep their keys (Q, V, G, L, B, M ...); the menu calls the same
// functions they do: RRQol (pause, again, pick), RRView (rr3d.js), RRGhost, RRBot,
// RRMusic, RRSfx, RRApp (the offline copy, below).
(function () {
    const load = (k, d) => { try { const v = localStorage.getItem('rr3d.' + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } };
    const save = (k, v) => { try { localStorage.setItem('rr3d.' + k, JSON.stringify(v)); } catch (e) {} };
    const esc = v => String(v).replace(/[&<>"']/g, c => '&#' + c.charCodeAt(0) + ';');
    const sec = f => (f / 60).toFixed(2) + ' s';
    const V = () => window.RRView || null;
    const touchy = () => document.body.classList.contains('touch3d');

    const css = document.createElement('style');
    css.textContent = `
#menubtn3d { position: fixed; top: max(10px, env(safe-area-inset-top)); right: max(10px, env(safe-area-inset-right)); z-index: 25;
  width: 44px; height: 44px; padding: 0; margin: 0; transform: none; border-radius: 10px; border: 0; cursor: pointer;
  background: rgba(0,0,0,.45); color: #fff; font: 22px/44px system-ui, sans-serif; -webkit-tap-highlight-color: transparent; }
#menubtn3d:hover { background: rgba(0,0,0,.65); transform: none; }
#status3d { position: fixed; left: max(12px, env(safe-area-inset-left)); bottom: 10px; z-index: 10; color: #fff; pointer-events: none;
  font: 14px ui-monospace, Menlo, monospace; text-shadow: 1px 1px 2px #000; opacity: .75; }
body.touch3d #status3d { display: none; }
#menu3d { display: none; position: fixed; inset: 0; z-index: 45; background: rgba(0,0,0,.55); align-items: center; justify-content: center;
  font: 16px/1.4 system-ui, -apple-system, sans-serif; color: #eee; }
#menu3d .box { width: min(92vw, 640px); max-height: 90vh; overflow: auto; box-sizing: border-box; padding: 14px 16px 16px;
  border-radius: 14px; background: rgba(16,20,28,.97); box-shadow: 0 10px 40px rgba(0,0,0,.5); }
#menu3d .head { display: flex; align-items: center; gap: 8px; margin-bottom: 10px; min-height: 40px; }
#menu3d .head h2 { flex: 1; margin: 0; font-size: 20px; font-weight: 600; color: #fff; }
#menu3d .head .sub { font-size: 13px; color: #9aa; font-weight: 400; margin-left: 6px; }
#menu3d button { position: static; transform: none; margin: 0; font: inherit; color: #eee; cursor: pointer; -webkit-tap-highlight-color: transparent; }
#menu3d .hb { width: 40px; height: 40px; padding: 0; border-radius: 10px; border: 0; background: rgba(255,255,255,.08); font-size: 20px; }
#menu3d .mi { display: flex; width: 100%; align-items: center; justify-content: space-between; gap: 10px; box-sizing: border-box;
  padding: 12px 14px; margin: 0 0 6px; border-radius: 10px; border: 1px solid transparent; background: rgba(255,255,255,.06); text-align: left; }
#menu3d .mi:hover, #menu3d .mi:focus-visible, #menu3d .tile:focus-visible, #menu3d .card:focus-visible, #menu3d .seg button:focus-visible, #menu3d .hb:focus-visible
  { outline: none; border-color: #4d8dff; background: rgba(45,108,223,.28); }
#menu3d .mi.main { background: #2d6cdf; color: #fff; font-weight: 600; }
#menu3d .mi .r { color: #9aa; font-size: 14px; white-space: nowrap; }
#menu3d .mi .r.on { color: #7ee07e; }
#menu3d .note { color: #9aa; font-size: 14px; margin: 4px 2px 10px; }
#menu3d h3 { margin: 14px 2px 8px; font-size: 14px; font-weight: 600; color: #9aa; text-transform: uppercase; letter-spacing: .04em; }
#menu3d .tiles { display: grid; grid-template-columns: repeat(5, 1fr); gap: 8px; }
#menu3d .tile { position: relative; padding: 0; border-radius: 10px; overflow: hidden; border: 2px solid transparent; aspect-ratio: 16 / 9;
  background-size: cover; background-position: center; text-align: left; }
#menu3d .tile.cur { border-color: #ffd700; }
#menu3d .tile span { position: absolute; left: 0; right: 0; bottom: 0; padding: 14px 6px 4px; font-size: 12px; line-height: 1.25; color: #fff;
  background: linear-gradient(transparent, rgba(0,0,0,.8)); text-shadow: 0 1px 2px #000; }
#menu3d .tile b { font-size: 15px; }
#menu3d .cards { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; }
#menu3d .card { padding: 12px; border-radius: 10px; border: 2px solid rgba(255,255,255,.12); background: rgba(255,255,255,.05); text-align: left; }
#menu3d .card.on { border-color: #4d8dff; background: rgba(45,108,223,.22); }
#menu3d .card small { display: block; color: #9aa; font-size: 13px; margin-top: 4px; }
#menu3d .seg { display: flex; flex-wrap: wrap; gap: 6px; }
#menu3d .seg button { flex: 1 1 auto; padding: 9px 10px; border-radius: 9px; border: 2px solid rgba(255,255,255,.12); background: rgba(255,255,255,.05); }
#menu3d .seg button.on { border-color: #4d8dff; background: rgba(45,108,223,.3); }
#menu3d input[type=range] { width: 100%; accent-color: #4d8dff; }
#menu3d table { border-collapse: collapse; width: 100%; font-size: 15px; }
#menu3d td { padding: 4px 12px 4px 2px; vertical-align: top; }
#menu3d td.k { color: #ffd700; white-space: nowrap; }
#menu3d .up { color: #6f6; } #menu3d .down { color: #f77; }
#menu3d details summary { cursor: pointer; color: #9aa; margin: 12px 2px 8px; }
@media (max-width: 700px), (max-height: 520px) {
  #menu3d .box { width: 100vw; max-height: 100vh; height: 100%; border-radius: 0;
    padding: max(12px, env(safe-area-inset-top)) max(14px, env(safe-area-inset-right)) 16px max(14px, env(safe-area-inset-left)); }
  #menu3d .tiles { grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); }
  #menu3d .cards { grid-template-columns: 1fr; }
}
#title, #levelSelect, #resetBtn, #nextBtn, #fps3d { display: none !important; }
body.menu3d-open #info, body.menu3d-open #ghost3d, body.menu3d-open #speedrun3d, body.menu3d-open #music3d { visibility: hidden; }
#finish3d { display: none; position: fixed; left: 50%; top: 64px; transform: translateX(-50%); z-index: 22; width: max-content; max-width: 92vw;
  box-sizing: border-box; padding: 12px 16px; border-radius: 12px; background: rgba(16,20,28,.9); color: #eee; text-align: center;
  font: 16px/1.4 system-ui, -apple-system, sans-serif; box-shadow: 0 6px 24px rgba(0,0,0,.45); }
#over3d { display: none; position: fixed; left: 50%; top: 64px; transform: translateX(-50%); z-index: 22; padding: 8px 16px; border-radius: 10px;
  background: rgba(16,20,28,.85); color: #fff; font: 16px system-ui, -apple-system, sans-serif; pointer-events: none; }
body.menu3d-open #over3d { display: none !important; }
#finish3d .t { font-size: 22px; font-weight: 700; color: #ffd700; }
#finish3d .s { color: #bcc; font-size: 14px; margin-top: 2px; }
#finish3d .b { display: flex; gap: 8px; justify-content: center; margin-top: 10px; flex-wrap: wrap; }
#finish3d button { position: static; transform: none; margin: 0; padding: 8px 14px; border-radius: 9px; border: 0; cursor: pointer;
  font: inherit; font-size: 15px; background: rgba(255,255,255,.12); color: #fff; }
#finish3d button.main { background: #2d6cdf; font-weight: 600; }
#finish3d button:hover { transform: none; filter: brightness(1.15); }
`;
    document.head.appendChild(css);

    // the original's own controls go: its title and level list (the menu has the levels),
    // its Reset button (R, the menu, or space / a tap after a fall) and its Next button
    // (the finish card has it). They stay in the page, so its own code keeps working.
    // (a stylesheet rule with !important: the game sets the Next button's inline display itself)
    const oldOpts = document.querySelector('#touch3d .opts'); if (oldOpts) oldOpts.remove();

    const btn = document.createElement('button'); btn.id = 'menubtn3d'; btn.setAttribute('aria-label', 'menu'); btn.textContent = '☰';
    const status = document.createElement('div'); status.id = 'status3d';
    const root = document.createElement('div'); root.id = 'menu3d'; root.setAttribute('role', 'dialog'); root.setAttribute('aria-modal', 'true');
    root.innerHTML = '<div class="box"><div class="head"></div><div class="body"></div></div>';
    const fin = document.createElement('div'); fin.id = 'finish3d';
    const over = document.createElement('div'); over.id = 'over3d';
    document.body.append(btn, status, root, fin, over);
    const head = root.querySelector('.head'), body = root.querySelector('.body');
    for (const el of [btn, root, fin]) el.addEventListener('pointerdown', e => e.stopPropagation()); // not a tap on the playfield
    btn.addEventListener('mousedown', e => e.preventDefault());
    btn.addEventListener('click', () => { if (window.RRQol) RRQol.pause(true); else open(); });

    // ---- pages. Each returns [title, html, after(body)] and is drawn again after every change
    let page = 'main', stack = [], isOpen = false;
    const mi = (a, label, right = '', cls = '') => `<button class="mi ${cls}" data-a="${a}"><span>${label}</span><span class="r ${right === 'on' ? 'on' : ''}">${right}</span></button>`;
    const onoff = v => v ? 'on' : 'off';
    const LV = () => typeof levels !== 'undefined' ? levels : []; // the game's own list (a global let, not on window)
    const lvName = i => (LV()[i] ? LV()[i].name : 'Level ' + (i + 1));
    const FRAMED = (() => { try { return window.top !== window; } catch (e) { return true; } })();
    const PAGES = {
        main() {
            const g = window.RRGhost && RRGhost.bests ? RRGhost.bests() : null;
            const sub = `level ${currentLevel + 1}: ${esc(lvName(currentLevel))}${g && g.seed != null ? ' · seed ' + g.seed : ''}`;
            return ['Rolling Rob', `<div class="note">${sub}</div>` +
                mi('go', 'Continue', 'Esc', 'main') + mi('again', 'Restart level', 'R') + mi('run', 'New run from level 1', 'shift-R') +
                mi('p:levels', 'Levels', '›') + mi('p:speedrun', 'Speedrun and ghosts', '›') + mi('p:settings', 'Settings', '›') +
                (window.RR_HOME ? mi('home', 'Back to Rolling Rob', '↩') : '')];
        },
        levels() {
            const g = window.RRGhost && RRGhost.bests ? RRGhost.bests() : { levels: [], run: null };
            const T = window.RR_THUMBS || [];
            const tiles = LV().map((L, i) => {
                const bg = T[i] ? `background-image:url(${T[i]})` : `background:linear-gradient(${L.skyColor1},${L.skyColor2} 70%,${L.groundColor} 70%)`;
                const best = g.levels[i] != null ? sec(g.levels[i]) : '';
                return `<button class="tile${i === currentLevel ? ' cur' : ''}" data-a="lv:${i}" style="${bg}" aria-label="level ${i + 1}: ${esc(L.name)}"><span><b>${i + 1}</b> ${esc(L.name)}${best ? '<br>best ' + best : ''}</span></button>`;
            }).join('');
            const run = g.run ? `Your best full run on this seed: <b>${sec(g.run.total)}</b>.` : 'A full run is levels 1 to 10 in one go; the times add up.';
            return ['Levels', `<div class="tiles">${tiles}</div><div class="note" style="margin-top:10px">${run} Keys 1 … 9 and 0 pick a level too.</div>`];
        },
        speedrun() {
            const B = window.RRBot, G = window.RRGhost;
            let h = '';
            if (B && B.on) h += mi('bot:stop', 'Stop the speedrun', 'B', 'main');
            else {
                if (B && B.planned) h += mi('bot:plan', 'Watch the planned speedrun', 'B');
                if (window.RRRoutes && G) h += mi('race:bot', 'Race the planned speedrun', 'shift-G');
                if (B) h += mi('bot:live', 'Let the bot play live', 'shift-B');
            }
            if (G) h += mi('ghosts', 'Show ghosts', onoff(G.show)) + mi('share', 'Share your run, or race a link', 'L');
            const r = B && B.report ? B.report() : null;
            if (r && r.splits.length) {
                const b = r.best;
                const rows = r.splits.map((v, i) => { const d = b && b.splits[i] ? v - b.splits[i] : null;
                    return `<tr><td class="k">${i + 1}</td><td>${sec(v)}</td><td>${d === null ? '' : `<span class="${d <= 0 ? 'up' : 'down'}">${d <= 0 ? '−' : '+'}${Math.abs(d / 60).toFixed(2)}</span>`}</td></tr>`; }).join('');
                h += `<h3>Last speedrun: ${esc(r.what)}</h3><table>${rows}<tr><td></td><td><b>${sec(r.total)}</b></td><td>${b ? 'best ' + sec(b.total) : ''}</td></tr></table>`;
            }
            h += '<div class="note" style="margin-top:12px">A ghost is an earlier run riding along: your own best, the planned speedrun or a shared link. While the bot runs a speedrun, your own input does nothing.</div>';
            return ['Speedrun and ghosts', h];
        },
        settings() {
            const v = V();
            return ['Settings', mi('p:display', 'Display', v ? esc(v.viewName()) + ' ›' : '›') + mi('p:sound', 'Sound', '›') + mi('p:keys', 'Controls', '›') +
                (APP.can ? mi('p:app', 'Offline and updates', (APP.ready ? 'ready offline' : '') + ' ›') : '')];
        },
        display() {
            const v = V();
            if (!v) return ['Display', '<div class="note">This browser cannot show 3D (no WebGL). You are playing the original in 2D.</div>' + mi('status', 'Status line at the bottom', onoff(load('status', true)))];
            const DESC = { '2d': 'the game as it was made', '3d': 'the world in 3D, plain colours', tex: 'the world in 3D with textures' };
            const cards = v.views.map(m => `<button class="card${v.view === m ? ' on' : ''}" data-a="view:${m}"><b>${esc(v.viewName(m))}</b><small>${DESC[m]}</small></button>`).join('');
            const flat = v.view === '2d';
            const seg = v.qualities.map(q => `<button class="${v.quality === q ? 'on' : ''}" data-a="q:${q}">${esc(v.qualityLabel(q))}</button>`).join('');
            const qnote = flat ? 'Quality applies to the 3D views.' : v.quality === 'auto' ? `Auto picks what this device runs smoothly; now ${esc(v.qualityLabel(v.qualityName))}.` : '';
            return ['Display', `<h3>View <span style="text-transform:none;font-weight:400">(V)</span></h3><div class="cards">${cards}</div>
                <h3>Quality <span style="text-transform:none;font-weight:400">(Q)</span></h3><div class="seg">${seg}</div><div class="note">${qnote}</div>
                <details${load('adv', false) ? ' open' : ''}><summary>More</summary>` +
                mi('fx', 'Effects (glow, post-processing)', onoff(v.fx)) + mi('shadow', 'Shadows', onoff(v.shadow)) + mi('cam', 'Moving camera', onoff(v.cam)) +
                mi('perf', 'Measure performance', onoff(v.perf)) + mi('status', 'Status line at the bottom', onoff(load('status', true))) + '</details>',
                b => { const d = b.querySelector('details'); if (d) d.addEventListener('toggle', () => save('adv', d.open)); }];
        },
        sound() {
            const M = window.RRMusic, S = window.RRSfx;
            let h = '';
            if (M) h += mi('music', 'Music', onoff(M.on)) + mi('next', 'Next track', M.title ? esc(M.title) : '') +
                `<h3>Volume</h3><input type="range" min="0" max="100" step="10" value="${M.vol}" data-a="vol" aria-label="volume">`;
            if (S) h += `<div style="height:10px"></div>` + mi('sfx', 'Sound effects', onoff(S.on));
            return ['Sound', h || '<div class="note">No sound in this version.</div>'];
        },
        keys() {
            const K = [
                ['← →', 'roll and brake'], ['↑ ↓', 'float, down'], ['space', 'jump (twice); after the finish: next level'],
                ['mouse on Rob', 'hold and let go: launch'], ['tap / hold', 'on a phone: jump / launch'],
                ['R, shift-R', 'restart level, restart the whole run'], ['1 … 9, 0', 'pick a level'], ['Esc or P', 'this menu (and pause)'],
                ['V, Q', 'view, quality'], ['G, shift-G', 'ghosts, race the speedrun'], ['L', 'share your run'], ['B, shift-B', 'planned speedrun, live bot'],
                ['M, N, − +', 'music, next track, volume'], ['S', 'sound effects'], ['F, H, C, shift-P', 'effects, shadows, camera, performance'],
                ['gamepad', 'stick: roll · A: jump · hold B/X: launch · Start: menu · Back: R'],
            ];
            return ['Controls', `<table>${K.map(([k, v]) => `<tr><td class="k">${k}</td><td>${v}</td></tr>`).join('')}</table>`];
        },
        app() {
            let h = `<div class="note">${APP.ready ? 'The game is saved on this device and plays without internet too. Music you have heard once plays offline as well.' : 'The game is saved on this device once it has loaded.'}</div>`;
            if (APP.version) h += `<div class="note">Version ${esc(APP.version)}${APP.built ? ', built ' + esc(APP.built) : ''}</div>`;
            if (APP.waiting) h += mi('update', 'New version: update', '', 'main');
            else h += mi('check', 'Check for a new version', APP.checked || '');
            return ['Offline and updates', h];
        },
    };
    const SUB = { levels: 'main', speedrun: 'main', settings: 'main', display: 'settings', sound: 'settings', keys: 'settings', app: 'settings' };

    function draw(focusFirst) {
        const r = PAGES[page](), focused = root.contains(document.activeElement) ? document.activeElement.dataset.a : null;
        const back = page !== 'main';
        head.innerHTML = `${back ? '<button class="hb" data-a="back" aria-label="back">‹</button>' : ''}<h2>${r[0]}</h2><button class="hb" data-a="close" aria-label="close">✕</button>`;
        body.innerHTML = r[1];
        if (r[2]) r[2](body);
        const want = focused && body.querySelector(`[data-a="${CSS.escape(focused)}"]`);
        if (want) want.focus({ preventScroll: true });
        else if (focusFirst) { const f = body.querySelector('button, input'); if (f) f.focus({ preventScroll: true }); }
    }
    function go(p) { page = p; draw(true); root.querySelector('.box').scrollTop = 0; }
    function open(p) {
        if (!isOpen) { isOpen = true; root.style.display = 'flex'; btn.style.display = 'none'; document.body.classList.add('menu3d-open'); hideFinish(); }
        page = p && PAGES[p] ? p : 'main';
        draw(true);
    }
    function close() {
        if (!isOpen) return;
        isOpen = false; root.style.display = 'none'; btn.style.display = ''; document.body.classList.remove('menu3d-open');
        if (root.contains(document.activeElement)) document.activeElement.blur(); // space is the game's again
        if (window.RRQol && RRQol.paused) RRQol.pause(false);
    }
    const back = () => { if (page === 'main') close(); else go(SUB[page] || 'main'); };

    root.addEventListener('click', e => {
        if (e.target === root) { close(); return; }
        const el = e.target.closest('[data-a]'); if (!el || el.tagName === 'INPUT') return;
        act(el.dataset.a);
    });
    root.addEventListener('input', e => { if (e.target.dataset.a === 'vol' && window.RRMusic) RRMusic.volume(+e.target.value); });
    function act(a) {
        const v = V(), Q = window.RRQol, G = window.RRGhost, B = window.RRBot, M = window.RRMusic, S = window.RRSfx;
        if (a === 'close' || a === 'go') return close();
        if (a === 'back') return back();
        if (a === 'home' && window.RR_HOME) { location.href = window.RR_HOME + location.search; return; }
        if (a.startsWith('p:')) return go(a.slice(2));
        if (a === 'again' || a === 'run') { close(); if (Q) Q.again(a === 'run'); return; }
        if (a.startsWith('lv:')) { close(); if (Q) Q.pick(+a.slice(3)); else loadLevel(+a.slice(3)); return; }
        if (a === 'bot:plan' && B) { close(); B.startPlanned(); return; }
        if (a === 'bot:live' && B) { close(); B.startLive(); return; }
        if (a === 'bot:stop' && B) { B.stop(false); draw(); return; }
        if (a === 'race:bot' && G) { close(); G.raceBot(); return; }
        if (a === 'share' && G) { close(); G.openShare(); return; }
        if (a === 'ghosts' && G) G.setShow(!G.show);
        if (a.startsWith('view:') && v) v.setView(a.slice(5));
        if (a.startsWith('q:') && v) v.setQuality(a.slice(2));
        if (a === 'fx' && v) v.setFx(!v.fx);
        if (a === 'shadow' && v) v.setShadow(!v.shadow);
        if (a === 'cam' && v) v.setCam(!v.cam);
        if (a === 'perf' && v) v.setPerf(!v.perf);
        if (a === 'status') { save('status', !load('status', true)); tick(); }
        if (a === 'music' && M) M.set(!M.on);
        if (a === 'next' && M) { M.skip(); setTimeout(() => isOpen && page === 'sound' && draw(), 600); }
        if (a === 'sfx' && S) S.set(!S.on);
        if (a === 'update') APP.update();
        if (a === 'check') APP.check();
        draw();
    }

    // keys while the menu is open: they are the menu's, not the game's (this listener comes
    // before qol.js's, so Esc goes one page back here first)
    window.addEventListener('keydown', e => {
        if (!isOpen) return;
        const t = e.target, field = t && /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName || '') && t.type !== 'range';
        if (field) return;
        const k = e.code;
        if (k === 'Escape' || k === 'Backspace') { e.preventDefault(); e.stopImmediatePropagation(); if (!e.repeat) back(); return; }
        // P: the menu shut at once, from any page. In a framed page (a claude.ai artifact) Esc
        // can take the keyboard focus out of the page, so P is the key that always works there.
        if (k === 'KeyP' && !e.shiftKey && !e.ctrlKey && !e.metaKey && !e.altKey) { e.preventDefault(); e.stopImmediatePropagation(); if (!e.repeat) close(); return; }
        if (k === 'ArrowDown' || k === 'ArrowUp' || ((k === 'ArrowLeft' || k === 'ArrowRight') && !(t && t.type === 'range'))) {
            e.preventDefault(); e.stopImmediatePropagation();
            const all = [...root.querySelectorAll('button, input')].filter(el => el.offsetParent !== null && !el.closest('.head'));
            if (!all.length) return;
            let i = all.indexOf(document.activeElement);
            const grid = document.activeElement && document.activeElement.classList.contains('tile');
            const cols = grid ? Math.max(1, Math.round(body.querySelector('.tiles').clientWidth / document.activeElement.offsetWidth)) : 1;
            const step = (k === 'ArrowDown' ? cols : k === 'ArrowUp' ? -cols : k === 'ArrowRight' ? 1 : -1);
            i = i < 0 ? 0 : Math.max(0, Math.min(all.length - 1, i + step));
            all[i].focus();
            return;
        }
        if (k === 'Space' || k === 'Enter' || k === 'NumpadEnter') {
            e.stopImmediatePropagation(); // the button's own click follows; the game gets nothing
            if (!e.isTrusted) { const f = document.activeElement; if (root.contains(f) && f.click) f.click(); return; } // a gamepad's A (qol.js) presses no button by itself
            if (!root.contains(t) || t === document.body) { e.preventDefault(); const f = body.querySelector('button, input'); if (f) f.focus(); }
            return;
        }
        if (k === 'ArrowLeft' || k === 'ArrowRight') { e.stopImmediatePropagation(); return; } // the slider's own
        // other keys (Q, V, M ...) do their work and the page shows it
        setTimeout(() => isOpen && draw(), 50);
    }, true);

    // ---- the finish card: time, record, the run so far; next level, again, share
    let finLv = -1;
    function finished(f) {
        finLv = f.lv;
        const rec = f.old == null ? 'your first time on this level' : f.time < f.old ? `new record, ${((f.old - f.time) / 60).toFixed(2)} s faster` : `record ${sec(f.old)}`;
        const last = f.lv >= LV().length - 1;
        fin.innerHTML = `<div class="t">Level ${f.lv + 1}: ${sec(f.time)}</div><div class="s">${esc(rec)}${f.run ? ' · ' + esc(f.run) : ''}</div>
            <div class="b">${last ? '' : '<button class="main" data-a="next">Next level (space)</button>'}<button data-a="again">Again (R)</button><button data-a="share">Share (L)</button><button data-a="menu">Menu</button></div>`;
        fin.style.display = 'block';
    }
    function hideFinish() { fin.style.display = 'none'; finLv = -1; }
    fin.addEventListener('mousedown', e => e.preventDefault());
    fin.addEventListener('click', e => {
        const a = e.target.dataset && e.target.dataset.a; if (!a) return;
        hideFinish();
        if (a === 'next') { const k = (d) => document.dispatchEvent(new KeyboardEvent(d ? 'keydown' : 'keyup', { code: 'Space', key: ' ', bubbles: true })); k(true); k(false); }
        if (a === 'again' && window.RRQol) RRQol.again(false);
        if (a === 'share' && window.RRGhost) RRGhost.openShare();
        if (a === 'menu' && window.RRQol) RRQol.pause(true);
    });

    // ---- the status line: frames per second (counted here, so in 2D as well), the
    // quality, the view and how to reach the menu
    let frames = 0, at = performance.now(), fps = 0;
    (function count() { frames++; requestAnimationFrame(count); })();
    function tick() {
        const now = performance.now(); fps = frames * 1000 / (now - at); frames = 0; at = now;
        if (finLv >= 0 && (currentLevel !== finLv || !gameWon)) hideFinish();
        if (!load('status', true)) { status.style.display = 'none'; return; }
        status.style.display = '';
        const v = V(), parts = [Math.round(fps) + ' fps'];
        if (v && v.view !== '2d') parts.push(v.qualityLabel());
        parts.push(v ? v.viewName() : 'Original 2D', FRAMED ? 'P: menu' : 'Esc: menu');
        status.textContent = parts.join(' · ');
    }
    setInterval(tick, 1000);
    // after a fall: how to go again (the original had its Reset button for that); not
    // during a race (R is the race's own reset there) or while the bot plays
    setInterval(() => {
        const show = typeof gameOver !== 'undefined' && gameOver && !gameWon && !(window.RRBot && RRBot.on);
        if (show && over.style.display !== 'block') over.textContent = touchy() ? 'Tap to try again' : 'Space or R: try again';
        over.style.display = show ? 'block' : 'none';
    }, 100);

    // ---- the offline copy (sw.js, made by build.py): only where the page is its own site,
    // not in a frame (an artifact) and not opened as a file
    const APP = { can: false, ready: false, waiting: null, version: '', built: '', checked: '', reg: null,
        update() { if (APP.waiting) APP.waiting.postMessage('update'); },
        check() { if (APP.reg) { APP.checked = 'checking…'; APP.reg.update().then(() => { APP.checked = APP.waiting ? '' : 'you have the latest'; if (isOpen) draw(); }).catch(() => { APP.checked = 'no connection'; if (isOpen) draw(); }); } },
    };
    (function () {
        let framed = true; try { framed = window.top !== window; } catch (e) {}
        if (!('serviceWorker' in navigator) || framed || !/^https?:$/.test(location.protocol) || window.RR_NO_SW) return;
        APP.can = true;
        const sw = navigator.serviceWorker;
        const ask = () => { if (sw.controller) sw.controller.postMessage('version'); };
        sw.addEventListener('message', e => { if (e.data && e.data.version) { APP.version = e.data.version; APP.built = e.data.built || ''; } });
        let reloading = false;
        sw.addEventListener('controllerchange', () => { if (APP.waiting && !reloading) { reloading = true; location.reload(); } ask(); });
        sw.register('sw.js').then(reg => {
            APP.reg = reg;
            const watch = w => { if (!w) return; w.addEventListener('statechange', () => { if (w.state === 'installed' && sw.controller) { APP.waiting = w; RRGhost && RRGhost.say && RRGhost.say('a new version is ready: menu, Settings, Offline and updates'); } if (w.state === 'activated') APP.ready = true; }); };
            if (reg.waiting && sw.controller) APP.waiting = reg.waiting;
            watch(reg.installing);
            reg.addEventListener('updatefound', () => watch(reg.installing));
            sw.ready.then(() => { APP.ready = true; ask(); });
        }).catch(() => { APP.can = false; });
    })();
    window.RRApp = APP;

    window.RRMenu = { open, close, finished, get isOpen() { return isOpen; }, get page() { return page; } };
})();
