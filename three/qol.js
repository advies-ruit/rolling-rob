// ---- for players who come back: R plays the level again from the start (shift-R:
// the whole run from level 1), the number keys pick a level (1-9, 0 for 10), Esc
// opens the menu (menu.js), which pauses the game (? opens it at the keys), the game pauses by itself when
// you leave the window mid-level, and a gamepad plays: stick or d-pad, A jumps,
// B or X held launches (as the mouse on Rob), Start pauses, Back/Select is R.
// The level menu lets go of the focus after a choice (the original kept it, so the
// arrow keys then changed the level instead of steering Rob).
// Nothing here changes the game's rules: R and the numbers load a level as the menu
// does, and while paused the game simply takes no steps (clock.js asks paused).
(function () {
    const typing = e => e.target && /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName || '');
    const say = s => { if (window.RRGhost && RRGhost.say) RRGhost.say(s); };
    const stopBot = () => { if (window.RRBot && RRBot.on) RRBot.stop(false); };
    const share = () => document.getElementById('share3d');
    const shareOpen = () => { const p = share(); return p && p.style.display !== 'none'; };
    let paused = false;

    function again(all) {
        // in a race (mp.js) R is the game's own reset: back to the start, the race clock runs on
        if (window.RRNet && RRNet.racing) { pause(false); resetLevel(); say('race: back to the start, the clock keeps running'); return; }
        stopBot();
        const lv = all ? 0 : currentLevel;
        loadLevel(lv);
        say(all ? 'new run from level 1' : lv ? `level ${lv + 1} again - a run only goes on after a finish (shift-R: new run)` : 'level 1 again: new run');
    }
    function pick(lv) {
        if (lv < 0 || lv >= levels.length) return;
        if (window.RRNet && RRNet.racing) { say('no other level during a race'); return; }
        stopBot(); loadLevel(lv);
        say(lv ? `level ${lv + 1} - practice: does not count as a full run (that starts at level 1)` : 'level 1: new run');
    }

    // ---- pause: the menu (menu.js) is the pause screen; while it is open the game takes no steps
    function pause(on) {
        if (on === paused) return;
        // a race (mp.js) is live: the others do not wait
        if (on && window.RRNet && RRNet.racing) { say('no pause during a race'); return; }
        paused = on;
        if (on) { isMouseDown = false; mouseDownTime = 0; } // a charge does not wait through the pause
        if (window.RRMenu) { if (on) RRMenu.open(); else RRMenu.close(); }
    }
    const midLevel = () => gameStarted && !gameOver && !gameWon && !(window.RRBot && RRBot.on);
    window.addEventListener('blur', () => { if (midLevel()) pause(true); });
    document.addEventListener('visibilitychange', () => { if (document.hidden && midLevel()) pause(true); });

    // while paused: the game's own keys wait (a jump, the restart), the others still work
    window.addEventListener('keydown', e => {
        if (!paused || typing(e)) return;
        if (e.code === 'Space' || e.code === 'Enter' || e.code === 'NumpadEnter') { e.preventDefault(); e.stopImmediatePropagation(); }
    }, true);

    window.addEventListener('keydown', e => {
        if (typing(e) || e.ctrlKey || e.metaKey || e.altKey) return;
        if (e.code === 'Escape' || e.key === '?' || (e.code === 'KeyP' && !e.shiftKey)) {
            if (e.repeat) return;
            if (shareOpen()) { share().style.display = 'none'; return; }
            pause(!paused);
            if (paused && e.key === '?' && window.RRMenu) RRMenu.open('keys');
            return;
        }
        if (e.code === 'KeyR' && !e.repeat) { pause(false); again(e.shiftKey); return; }
        const m = /^Digit([0-9])$/.exec(e.code);
        if (m && !e.shiftKey && !e.repeat) { pause(false); pick(m[1] === '0' ? 9 : +m[1] - 1); }
    }, true); // before the share panel's own Esc, so one Esc closes it and does not pause too

    // the level menu: back to the game after a choice
    const menu = document.getElementById('levelDropdown');
    if (menu) menu.addEventListener('change', () => menu.blur());

    // ---- a gamepad (standard mapping): pressed as keys, so all else works as with keys
    const key = (code, down) => document.dispatchEvent(new KeyboardEvent(down ? 'keydown' : 'keyup', { code, key: code === 'Space' ? ' ' : code, bubbles: true }));
    const was = {};
    let polling = false, charge = false;
    function poll() {
        const gp = [...(navigator.getGamepads ? navigator.getGamepads() : [])].find(g => g && g.connected);
        if (!gp) { polling = false; for (const k in was) if (was[k]) { key(k, false); was[k] = false; } return; }
        requestAnimationFrame(poll);
        const b = i => !!(gp.buttons[i] && gp.buttons[i].pressed), ax = i => gp.axes[i] || 0;
        const want = {
            ArrowLeft: b(14) || ax(0) < -0.5, ArrowRight: b(15) || ax(0) > 0.5,
            ArrowUp: b(12) || ax(1) < -0.5, ArrowDown: b(13) || ax(1) > 0.5,
            Space: b(0), Escape: b(9) || (paused && b(1)), KeyR: b(8), // B: back in the menu
        };
        for (const k in want) if (want[k] !== !!was[k]) { was[k] = want[k]; key(k, want[k]); }
        // B or X held: the mouse's charge on Rob, released: the launch (as the original's mouseup)
        const c = b(1) || b(2);
        if (c && !charge && !paused && !gameOver && !gameWon) { stopBot(); isMouseDown = true; mouseDownTime = 0; }
        if (!c && charge && isMouseDown) { pushRob(Math.min(mouseDownTime / 30, 1)); isMouseDown = false; mouseDownTime = 0; }
        charge = c;
    }
    function start() { if (!polling) { polling = true; requestAnimationFrame(poll); } }
    window.addEventListener('gamepadconnected', e => { start(); say(`gamepad: ${String(e.gamepad.id || '').slice(0, 40).replace(/[<>&]/g, '')} - Start: pause and the buttons`); });
    if (navigator.getGamepads && [...navigator.getGamepads()].some(Boolean)) start();

    window.RRQol = { get paused() { return paused; }, pause, again, pick, get pad() { return { polling, charge, was: { ...was } }; } };
})();
