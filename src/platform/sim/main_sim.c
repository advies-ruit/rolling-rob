/*
 * Desktop simulator (macOS/Linux/Windows via SDL2).
 *
 * Renders at the exact logical resolution of the game's 480x272 screen
 * (ROLLING_ROB_SCREEN_WIDTH x ROLLING_ROB_SCREEN_HEIGHT) and scales the
 * window by ROLLING_ROB_SIM_ZOOM. Game behaviour is the same at every size.
 *
 * --scale K (2..4) renders at K times that size instead, with the view scale
 * and the HUD (ui.h) scaled along: the same slice of the world, sharper.
 *
 * Headless mode (no window, for automated screenshots / CI):
 *   rolling_rob_sim --headless --frames 120 --out frame.raw [--level 3]
 *                   [--touch F0,F1,X,Y]... [--key NAME,F0,F1]...
 *                   [--app [--app-key K,F]...]  (the app's menu; key K at frame F)
 * writes the final frame as raw 32-bit BGRA (little-endian ARGB8888).
 */

#include "lvgl.h"
#include "game.h"
#include "rob3d_view.h"
#include "hud.h"
#include "fps_overlay.h"
#include "bot.h"
#include "art.h"
#include "gfx.h"
#include "music.h"
#include "app.h"
#include "level.h"
#define SDL_MAIN_HANDLED /* our own main(), no SDL2main library (Windows) */
#include <SDL2/SDL.h>
#include "fbdev_sim.h" /* after SDL_MAIN_HANDLED: it includes SDL too */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif

#ifndef ROLLING_ROB_VIEW_SCALE
#define ROLLING_ROB_VIEW_SCALE 0.25f
#endif

static int s_k = 1; /* --scale, or chosen at start (choose_size) */
static bool s_scale_given; /* --scale */
static bool s_zoom_given;  /* --zoom */
#define SCR_W (ROLLING_ROB_SCREEN_WIDTH * s_k)
#define SCR_H (ROLLING_ROB_SCREEN_HEIGHT * s_k)
#define VIEW_SCALE (ROLLING_ROB_VIEW_SCALE * (float)s_k)

static lv_obj_t * s_canvas;
static lv_draw_buf_t * s_draw_buf;
static lv_indev_t * s_mouse;
static bool s_no_hud; /* --no-hud: bare game canvas (used for level thumbnails) */
static bool s_fps;    /* --fps: on-screen frame counter, whatever the saved choice */
static bool s_fps_overlay; /* created (windowed): F and the picker tile switch it */
static bool s_no_title;   /* --no-title: straight into level 1 */
static bool s_title_shot; /* headless --title: show the title screen */

/* ---------------- shared ---------------- */

/* A key pressed and released between two polls (a quick tap, or a
 * synthetic key event) would be missed by the state alone: latch presses
 * from the event stream (event_filter) until the next poll. */
static bool s_key_latch[SDL_NUM_SCANCODES];

#if LV_USE_LINUX_FBDEV && LV_USE_EVDEV && !defined(__EMSCRIPTEN__)
#define RR_FBDEV 1 /* the fallback on a Linux device with only /dev/fb0 */
#endif

/* The picture in /dev/fb0 (fbdev_sim.c), the keys from /dev/input in
 * s_fb_keys: SDL has no window then, nor a keyboard state of its own. */
static bool s_fbdev;
static Uint8 s_fb_keys[SDL_NUM_SCANCODES];

static const Uint8 * keyboard_state(void)
{
    return s_fbdev ? s_fb_keys : SDL_GetKeyboardState(NULL);
}

static bool key_down(const Uint8 * keys, SDL_Scancode sc)
{
    bool down = keys[sc] || s_key_latch[sc];
    s_key_latch[sc] = false;
    return down;
}

static void poll_keyboard(void)
{
    const Uint8 * keys = keyboard_state();
    game_input_key(RR_KEY_LEFT,  key_down(keys, SDL_SCANCODE_LEFT));
    game_input_key(RR_KEY_RIGHT, key_down(keys, SDL_SCANCODE_RIGHT));
    game_input_key(RR_KEY_UP,    key_down(keys, SDL_SCANCODE_UP));
    game_input_key(RR_KEY_DOWN,  key_down(keys, SDL_SCANCODE_DOWN));
    game_input_key(RR_KEY_SPACE, key_down(keys, SDL_SCANCODE_SPACE)); /* Enter: the app's menu */
    game_input_key(RR_KEY_ART,   key_down(keys, SDL_SCANCODE_A));
    game_input_key(RR_KEY_MUSIC, key_down(keys, SDL_SCANCODE_M));
    game_input_key(RR_KEY_NEXT_TRACK, key_down(keys, SDL_SCANCODE_N));
    game_input_key(RR_KEY_FPS, key_down(keys, SDL_SCANCODE_F));
}

/* Over the menu, a game or a page the presses are dropped, or a Space from
 * Tetris would start the game on the way back. */
static void drop_keys(void)
{
    memset(s_key_latch, 0, sizeof(s_key_latch));
}

static void canvas_pointer_event_cb(lv_event_t * e)
{
    lv_indev_t * indev = lv_indev_active();
    if(indev == NULL) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_event_code_t code = lv_event_get_code(e);
    bool pressed = (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING);
    game_input_touch(0, pressed, p.x, p.y);
}

static void create_canvas(void)
{
    s_canvas = lv_canvas_create(lv_screen_active());
    lv_obj_set_pos(s_canvas, 0, 0);
    /* XRGB8888: gfx always writes alpha 0xFF, and without alpha to honour
     * LVGL copies the canvas line by line instead of blending every pixel
     * (1920x1088 on the web: ~2 ms) */
    s_draw_buf = lv_draw_buf_create(SCR_W, SCR_H,
                                    LV_COLOR_FORMAT_XRGB8888, LV_STRIDE_AUTO);
    if(s_draw_buf == NULL) {
        fprintf(stderr, "Failed to allocate canvas draw buffer\n");
        exit(1);
    }
    lv_canvas_set_draw_buf(s_canvas, s_draw_buf);
    lv_obj_set_clickable(s_canvas, true);
    lv_obj_add_event_cb(s_canvas, canvas_pointer_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_canvas, canvas_pointer_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_canvas, canvas_pointer_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_canvas, canvas_pointer_event_cb, LV_EVENT_PRESS_LOST, NULL);
}

/* --backdrop: the art's picture is left to a (pretend) hardware layer under
 * a colour-keyed screen. The layer is played here by filling the
 * key pixels in from the picture, so a recording must match --rgb565 exactly. */
static bool s_backdrop;

static void fill_backdrop(void)
{
    lv_draw_buf_t * buf = lv_canvas_get_draw_buf(s_canvas);
    if(buf == NULL || gfx_backdrop.px == NULL) return;
    for(int y = 0; y < (int)buf->header.h; y++) {
        uint32_t * row = (uint32_t *)(buf->data + (size_t)y * buf->header.stride);
        for(int x = 0; x < (int)buf->header.w; x++) row[x] = gfx_under(row[x], x, y);
    }
}

static void canvas_band_sink(uint32_t * band, int stride_px, int y0, int rows, void * user)
{
    lv_draw_buf_t * buf = user;
    for(int r = 0; r < rows; r++)
        memcpy(buf->data + (size_t)(y0 + r) * buf->header.stride, band + (size_t)r * stride_px, (size_t)buf->header.w * 4);
}

static void frame(uint32_t dt_ms)
{
    if(!s_no_hud && hud_is_modal_open()) return; /* level picker open: game frozen */
    game_update(dt_ms);
    game_render(s_canvas);
    if(s_backdrop) fill_backdrop();
    if(!s_no_hud) hud_update();
}

/* ---------------- windowed (SDL) ---------------- */

/* Touchscreens (phones on the web build, Windows tablets). SDL turns the
 * first finger into mouse events, which LVGL handles as its pointer (slot 0:
 * HUD and playfield). LVGL's own finger handling divides normalised
 * coordinates by the zoom, so finger events are kept away from it; further
 * fingers go straight to the game's touch slots 1.., which is what lets a
 * player hold an arrow and tap JUMP at the same time. */
#define SIM_TOUCH_MAX RR_TOUCH_SLOTS
static SDL_FingerID s_fingers[SIM_TOUCH_MAX]; /* index = slot; 0 = the mouse finger */
static bool s_finger_used[SIM_TOUCH_MAX];

static int finger_slot(SDL_FingerID id)
{
    for(int i = 0; i < SIM_TOUCH_MAX; i++)
        if(s_finger_used[i] && s_fingers[i] == id) return i;
    return -1;
}

static Uint32 s_window_id; /* LVGL's only window (it has no getter for it) */

/* SDL's keys as the app's (app.h): letters, digits, and the few others it uses. */
static int app_key_of(SDL_Scancode sc)
{
    if(sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'a' + (sc - SDL_SCANCODE_A);
    if(sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
    if(sc == SDL_SCANCODE_0) return '0';
    switch(sc) {
        case SDL_SCANCODE_ESCAPE: return APP_KEY_ESC;
        case SDL_SCANCODE_BACKSPACE: return APP_KEY_BACKSPACE;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: return APP_KEY_ENTER;
        case SDL_SCANCODE_SPACE: return APP_KEY_SPACE;
        case SDL_SCANCODE_LEFT: return APP_KEY_LEFT;
        case SDL_SCANCODE_RIGHT: return APP_KEY_RIGHT;
        case SDL_SCANCODE_UP: return APP_KEY_UP;
        case SDL_SCANCODE_DOWN: return APP_KEY_DOWN;
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL: return APP_KEY_CTRL;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: return APP_KEY_SHIFT;
        default: return 0;
    }
}

bool app_key_held(int k)
{
    const Uint8 * kb = keyboard_state();
    if(k >= 'a' && k <= 'z') return kb[SDL_SCANCODE_A + (k - 'a')];
    switch(k) {
        case APP_KEY_SPACE: return kb[SDL_SCANCODE_SPACE];
        case APP_KEY_LEFT: return kb[SDL_SCANCODE_LEFT];
        case APP_KEY_RIGHT: return kb[SDL_SCANCODE_RIGHT];
        case APP_KEY_UP: return kb[SDL_SCANCODE_UP];
        case APP_KEY_DOWN: return kb[SDL_SCANCODE_DOWN];
        default: return false;
    }
}

/* A key went down: for the game (latched till its next poll) and the app. */
static void key_pressed(SDL_Scancode sc)
{
    s_key_latch[sc] = true;
    int k = app_key_of(sc);
    if(k) app_key(k);
}

static int SDLCALL event_filter(void * user, SDL_Event * e)
{
    LV_UNUSED(user);
#ifndef __EMSCRIPTEN__
    /* The picture has a fixed size (choose_size), and the window can't be
     * dragged to another. A window the system resized -- LVGL first makes
     * it the picture's size, too big for the screen at x4, before the zoom
     * shrinks it -- must not reach LVGL, which would take window size /
     * zoom as its new resolution and show only part of the picture. */
    if(e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_RESIZED) return 0;
#endif
    if(e->type == SDL_KEYDOWN && !e->key.repeat && e->key.keysym.scancode < SDL_NUM_SCANCODES)
        key_pressed(e->key.keysym.scancode);
    /* A click with no mouse movement before it (a synthetic click, or the
     * very first one on a web page) can arrive without a window, and LVGL
     * drops events it cannot match to its window. There is only one. */
    if((e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) && e->button.windowID == 0)
        e->button.windowID = s_window_id;
    if(e->type == SDL_MOUSEMOTION && e->motion.windowID == 0) e->motion.windowID = s_window_id;
    if(e->type != SDL_FINGERDOWN && e->type != SDL_FINGERUP && e->type != SDL_FINGERMOTION) return 1;
    if(e->type != SDL_FINGERMOTION) app_note_finger(e->type == SDL_FINGERDOWN, (int64_t)e->tfinger.fingerId);
    if(SDL_GetTouchDeviceType(e->tfinger.touchId) != SDL_TOUCH_DEVICE_DIRECT) return 0;

    int32_t x = (int32_t)(e->tfinger.x * SCR_W);
    int32_t y = (int32_t)(e->tfinger.y * SCR_H);
    int slot = finger_slot(e->tfinger.fingerId);
    if(e->type == SDL_FINGERDOWN && slot < 0) {
        /* The first finger while none is down is the one SDL makes the mouse. */
        int start = s_finger_used[0] ? 1 : 0;
        for(int i = start; i < SIM_TOUCH_MAX; i++) {
            if(!s_finger_used[i]) { slot = i; break; }
        }
        if(slot < 0) return 0;
        s_finger_used[slot] = true;
        s_fingers[slot] = e->tfinger.fingerId;
    }
    if(slot < 0) return 0;
    bool down = e->type != SDL_FINGERUP;
    if(slot > 0 && app_game_input()) game_input_touch(slot, down, x, y);
    else if(slot > 0) app_touch(slot, down, x, y);
    if(!down) s_finger_used[slot] = false;
    return 0; /* never to LVGL */
}

static bool windowed_bot_step(void);
static bool windowed_bot_begin(int level);
static void windowed_bot_tick(void);
static bool windowed_bot_on(void);

static void render_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    static uint32_t last_ms = 0;
    uint32_t now_ms = lv_tick_get();
    uint32_t dt_ms = last_ms == 0 ? 16 : now_ms - last_ms;
    last_ms = now_ms;
#ifdef RR_FBDEV
    if(s_fbdev) fbdev_poll_keys(s_fb_keys, key_pressed);
#endif
    if(windowed_bot_step()) dt_ms = 16; /* the bot plays as headless: 16 ms a frame */
    else if(app_game_input()) poll_keyboard();
    else drop_keys();
    if(app_game_runs()) frame(dt_ms);
    app_tick(dt_ms);
    if(app_draws_frame()) {
        /* the 3D demo: its bands into the game's canvas */
        app_render_bands((int)s_draw_buf->header.w, (int)s_draw_buf->header.h, canvas_band_sink, s_draw_buf);
        lv_obj_invalidate(s_canvas);
    }
    music_update();
    if(s_fps_overlay) fps_overlay_set_visible(s_fps || app_fps_wanted());
}

int rr_sdl_accelerated = 1; /* LV_SDL_ACCELERATED, see lv_conf.h */

/* The canvas covers the whole window, so nothing needs the screen's own
 * background or alpha: an opaque (XRGB8888) display is not cleared before
 * every frame, and a see-through screen is not filled. At 1920x1088 in the
 * browser those two passes cost as much as drawing the game. */
void sim_display_opaque(lv_display_t * disp)
{
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
    lv_obj_set_style_bg_opa(lv_display_get_screen_active(disp), LV_OPA_TRANSP, 0);
}

#ifdef __EMSCRIPTEN__
/* Can this browser make a WebGL context? Not with hardware acceleration off,
 * in some VMs and locked-down browsers -- SDL's accelerated renderer then fails
 * ("failed to create WebGL context") and the game would not start. */
EM_JS(int, web_has_webgl, (void), {
    try {
        var c = document.createElement('canvas');
        var gl = c.getContext('webgl') || c.getContext('experimental-webgl');
        if(!gl) return 0;
        var lose = gl.getExtension('WEBGL_lose_context');
        if(lose) lose.loseContext();
        return 1;
    } catch(e) {
        return 0;
    }
});

static void web_loop(void)
{
    windowed_bot_tick();
    double a = emscripten_get_now();
    lv_timer_handler();
    app_note_loop_ms((float)(emscripten_get_now() - a));
}
#endif

/* Packaged builds (macOS .app, Windows zip) keep art/ and music/ next to the
 * program -- in Contents/Resources for the .app -- while a development run
 * starts in the source tree. Move to the program's directory if the working
 * directory has no art/. Also give Windows a place for the times file. */
static void find_data(void)
{
#ifndef __EMSCRIPTEN__
    struct stat st;
    if(getenv("ROLLING_ROB_ART") == NULL && stat("art", &st) != 0) {
        char * base = SDL_GetBasePath();
        if(base) {
            if(chdir(base) != 0) fprintf(stderr, "cannot enter %s\n", base);
            SDL_free(base);
        }
    }
#endif
#ifdef _WIN32
    if(getenv("ROLLING_ROB_SAVE") == NULL && getenv("HOME") == NULL) {
        char * pref = SDL_GetPrefPath("Vader", "Rolling Rob"); /* %APPDATA%\Vader\Rolling Rob\ */
        if(pref) {
            static char path[1024];
            snprintf(path, sizeof(path), "%srolling_rob_times.txt", pref);
            game_set_save_path(path);
            SDL_free(pref);
        }
    }
#endif
}

bool sim_scale_choosable(void)
{
#ifdef __EMSCRIPTEN__
    return false; /* the page chooses (web/shell.html, Resolution) */
#else
    return true;
#endif
}
int sim_scale_now(void) { return s_k; }

#ifndef __EMSCRIPTEN__
/* SDL windows with the screen's own pixels. LVGL creates its window itself
 * and asks for no high resolution, so on a Retina Mac (or a Windows laptop
 * at 150 %) the system enlarged a picture of half the size. CMakeLists.txt
 * compiles LVGL's lv_sdl_window.c with SDL_CreateWindow renamed to this. */
SDL_Window * SDLCALL rr_sdl_create_window(const char * title, int x, int y, int w, int h, Uint32 flags)
{
    return SDL_CreateWindow(title, x, y, w, h, flags | SDL_WINDOW_ALLOW_HIGHDPI);
}

/* The picture size (s_k: 1-4 times the 480x272 LCD) and the window zoom
 * (window points per picture pixel). The window is ROLLING_ROB_SIM_ZOOM
 * times the LCD (or --zoom), made smaller where the screen is small. The
 * picture: --scale, else the saved choice (the Resolution line on Rolling
 * Rob's page), else auto -- as many screen pixels as the window has, like
 * the web page's Auto: x2 on a plain screen, x4 on a Retina one. */
/* Without a screen there is nothing to play on: say so and stop, instead of
 * running on unseen (SDL then falls back to its offscreen driver, or LVGL
 * runs without a window -- no message, nothing shown, never ending). */
static void no_screen(const char * what)
{
    fprintf(stderr,
            "Rolling Rob: no screen to show the game on (%s).\n"
            "It needs an X11 or Wayland desktop, KMS/DRM (/dev/dri) without one,\n"
            "or on Linux a framebuffer (/dev/fb0) it may write to.\n",
            what);
    exit(1);
}

/* Has SDL a screen? Else false, and why. */
static bool sdl_screen(char * why, size_t why_n)
{
    if(SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        snprintf(why, why_n, "SDL: %s", SDL_GetError());
        return false;
    }
    /* offscreen and dummy draw into memory only: fine when asked for
     * (SDL_VIDEODRIVER), not when SDL settles for one as nothing else works */
    const char * driver = SDL_GetCurrentVideoDriver();
    const char * asked = getenv("SDL_VIDEODRIVER");
    if(driver && (!strcmp(driver, "offscreen") || !strcmp(driver, "dummy")) && !(asked && strstr(asked, driver))) {
        snprintf(why, why_n, "SDL found only its \"%s\" driver", driver);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    fprintf(stderr, "video: %s\n", driver ? driver : "?");
    return true;
}

#ifdef RR_FBDEV
/* The picture for a framebuffer of fw x fh: --scale, else the saved
 * choice, else the largest that fits (at most x1 on 32-bit ARM, x2 on
 * 64-bit: the CPU draws every pixel). False if not even 480x272 fits. */
static bool choose_fb_size(int fw, int fh)
{
    const int lw = ROLLING_ROB_SCREEN_WIDTH, lh = ROLLING_ROB_SCREEN_HEIGHT;
    int fit = fw / lw < fh / lh ? fw / lw : fh / lh;
    if(fit < 1) return false;
    int k = s_scale_given ? s_k : game_scale_choice();
    if(k == 0) {
        k = fit;
#if defined(__arm__)
        if(k > 1) k = 1;
#elif defined(__aarch64__)
        if(k > 2) k = 2;
#endif
    }
    s_k = k < 1 ? 1 : k > fit ? fit : k > 4 ? 4 : k;
    return true;
}
#endif

static float choose_size(float zoom, bool zoom_given)
{
    const float lw = (float)ROLLING_ROB_SCREEN_WIDTH, lh = (float)ROLLING_ROB_SCREEN_HEIGHT;
    SDL_Rect r;
    if(!zoom_given && SDL_GetDisplayUsableBounds(0, &r) == 0) {
        float fit = fminf((float)r.w * 0.9f / lw, (float)r.h * 0.9f / lh);
        if(zoom > fit) zoom = fit < 1.0f ? 1.0f : fit;
    }
    float wpts = lw * zoom;
    /* screen pixels per window point: 2 on a Retina Mac, 1.5 at 150 % */
    float density = 1.0f;
    SDL_Window * probe = rr_sdl_create_window("", 0, 0, 100, 100, SDL_WINDOW_HIDDEN);
    if(probe) {
        int w = 0, h = 0, pw = 0, ph = 0;
        SDL_GetWindowSize(probe, &w, &h);
        SDL_GetWindowSizeInPixels(probe, &pw, &ph);
        if(w > 0 && pw > 0) density = (float)pw / (float)w;
        SDL_DestroyWindow(probe);
    }
    if(!s_scale_given) {
        int k = game_scale_choice();
        if(k == 0) {
            k = (int)(wpts * density / lw + 0.01f);
#if defined(__arm__)
            if(k > 1) k = 1; /* 32-bit ARM (a Raspberry Pi 2 or 3): the CPU draws every pixel */
#elif defined(__aarch64__) && defined(__linux__)
            if(k > 2) k = 2; /* a Raspberry Pi 4 or 5 */
#endif
        }
        s_k = k < 1 ? 1 : k > 4 ? 4 : k;
    }
    fprintf(stderr, "window %.0fx%.0f points, %.2f pixels a point: picture %dx%d (x%d)\n", wpts, lh * zoom, (double)density,
            SCR_W, SCR_H, s_k);
    return wpts / (float)SCR_W;
}
#endif

static int run_windowed(float zoom, int level)
{
    find_data();
#ifdef __EMSCRIPTEN__
    if(!web_has_webgl()) {
        rr_sdl_accelerated = 0;
        fprintf(stderr, "no WebGL: drawing with SDL's software renderer\n");
    }
#endif
#ifndef __EMSCRIPTEN__
    game_load_settings(); /* the saved Resolution choice, before the window */
    char why[512] = "";
    /* ROLLING_ROB_FBDEV: straight to the framebuffer, whatever SDL has */
    const char * fb_asked = getenv("ROLLING_ROB_FBDEV");
    if((fb_asked && fb_asked[0]) || !sdl_screen(why, sizeof why)) {
#ifdef RR_FBDEV
        /* a Linux device with only /dev/fb0 */
        int fw = 0, fh = 0;
        char fb_why[256];
        if(!fbdev_probe(&fw, &fh, fb_why, sizeof fb_why)) {
            size_t n = strlen(why);
            snprintf(why + n, sizeof why - n, "%s%s", n ? "; " : "", fb_why);
            no_screen(why);
        }
        if(!choose_fb_size(fw, fh)) {
            snprintf(why, sizeof why, "%s is %dx%d, smaller than %dx%d", fbdev_path(), fw, fh,
                     ROLLING_ROB_SCREEN_WIDTH, ROLLING_ROB_SCREEN_HEIGHT);
            no_screen(why);
        }
        s_fbdev = true;
#else
        no_screen(why);
#endif
    }
    else {
        zoom = choose_size(zoom, s_zoom_given);
        SDL_SetEventFilter(event_filter, NULL); /* before the window: see event_filter */
    }
#endif
    lv_init();
    lv_display_t * disp;
#ifdef RR_FBDEV
    if(s_fbdev) {
        disp = fbdev_display_create(SCR_W, SCR_H);
        if(disp == NULL) no_screen("the framebuffer could not be set up");
        /* the canvas covers the picture: no background to clear */
        lv_obj_set_style_bg_opa(lv_display_get_screen_active(disp), LV_OPA_TRANSP, 0);
        fbdev_touch_create(disp);
    }
    else
#endif
    {
    disp = lv_sdl_window_create(SCR_W, SCR_H);
#ifndef __EMSCRIPTEN__
    bool have_window = false; /* the probe in choose_size took id 1 */
    for(Uint32 id = 1; id < 64 && !have_window; id++) have_window = SDL_GetWindowFromID(id) != NULL;
    if(!disp || !have_window) {
        char what[256];
        snprintf(what, sizeof what, "no window: %s", SDL_GetError());
        no_screen(what);
    }
#endif
    lv_sdl_window_set_zoom(disp, zoom);
#ifndef __EMSCRIPTEN__
    /* LVGL centred the window at the picture's size and the zoom shrank (or
     * grew) it from its corner: at x4 it hung off the top left of the
     * screen. Centre it at its real size. */
    for(Uint32 id = 1; id < 64; id++) {
        SDL_Window * w = SDL_GetWindowFromID(id);
        if(w) {
            SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            break;
        }
    }
#endif
    lv_sdl_window_set_title(disp, "Rolling Rob");
    sim_display_opaque(disp);
    }
    /* Show every 60 Hz tick, not LVGL's default 33 ms (30 fps); the desktop
     * and the browser have the headroom for it.
     * In the browser the animation frame sets the pace, and the clock it is
     * measured with is coarse (Firefox rounds it): a 16.7 ms frame can read
     * as 15 ms, and a 16 ms period then skipped that frame -- a visible
     * hitch in any scroll. 12 ms takes every 60 Hz frame and every other
     * one on a 120 Hz screen. */
#ifdef __EMSCRIPTEN__
    uint32_t refr_ms = 12;
#else
    uint32_t refr_ms = 16;
#endif
    const char * refr_env = getenv("ROLLING_ROB_REFR_MS");
    if(refr_env && atoi(refr_env) > 0) refr_ms = (uint32_t)atoi(refr_env);
    lv_timer_set_period(lv_display_get_refr_timer(disp), refr_ms);
    if(!s_fbdev) {
        s_mouse = lv_sdl_mouse_create();
        lv_sdl_keyboard_create();
        for(Uint32 id = 1; id < 64 && s_window_id == 0; id++)
            if(SDL_GetWindowFromID(id)) s_window_id = id;
        SDL_SetEventFilter(event_filter, NULL);
    }

    create_canvas();
    game_init(SCR_W, SCR_H, VIEW_SCALE);
    if(windowed_bot_on()) { /* windowed_bot_begin, below */ }
    else if(level > 0) game_goto_level(level);
    else if(!s_no_title) game_title_begin();
    music_init();
    hud_enable_art_tile(true);
    if(!s_no_hud) {
        app_menu_button();
        hud_create(lv_screen_active());
        app_init(disp, SCR_W, SCR_H, s_k);
    }
    /* --bot: it plays, from level 1 (or --level); after the app, which
     * draws on rand() too, so with --seed the run is the headless one */
    windowed_bot_begin(level);
    fps_overlay_create(disp, NULL);
    s_fps_overlay = true;
    fps_overlay_set_visible(s_fps || app_fps_wanted());

    lv_timer_create(render_timer_cb, refr_ms, NULL);
#ifdef __EMSCRIPTEN__
    /* The browser owns the loop: one LVGL pass per animation frame. */
    emscripten_set_main_loop(web_loop, 0, 1);
#else
    while(1) {
        windowed_bot_tick();
        Uint64 a = SDL_GetPerformanceCounter();
        uint32_t sleep_ms = lv_timer_handler();
        app_note_loop_ms((float)((double)(SDL_GetPerformanceCounter() - a) * 1000.0 / (double)SDL_GetPerformanceFrequency()));
        if(sleep_ms == LV_NO_TIMER_READY || sleep_ms > 16) sleep_ms = 16;
        SDL_Delay(sleep_ms);
    }
#endif
    return 0;
}

/* ---------------- headless ---------------- */

#define MAX_SCRIPT 32
typedef struct { int f0, f1, x, y; } touch_evt_t;
typedef struct { rr_key_t key; int f0, f1; } key_evt_t;
static touch_evt_t s_touches[MAX_SCRIPT];
static int s_touch_n;
static key_evt_t s_keys[MAX_SCRIPT];
/* --app: the app's menu around the game, headless too; --app-key K,F sends
 * key K (a letter, or esc enter space left right up down) at frame F */
static bool s_app_headless;
typedef struct { int key, f; } app_key_evt_t;
static app_key_evt_t s_app_keys[MAX_SCRIPT];
static int s_app_key_n;
static int s_key_n;

static uint32_t s_fake_tick;
static uint32_t fake_tick_cb(void) { return s_fake_tick; }

static void headless_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    LV_UNUSED(area);
    LV_UNUSED(px_map);
    lv_display_flush_ready(disp);
}

static void headless_indev_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    LV_UNUSED(indev);
    int frame_idx = (int)(s_fake_tick / 16);
    data->state = LV_INDEV_STATE_RELEASED;
    for(int i = 0; i < s_touch_n; i++) {
        if(frame_idx >= s_touches[i].f0 && frame_idx < s_touches[i].f1) {
            data->state = LV_INDEV_STATE_PRESSED;
            data->point.x = s_touches[i].x;
            data->point.y = s_touches[i].y;
        }
    }
}

static rr_key_t parse_key(const char * s)
{
    if(!strcmp(s, "left")) return RR_KEY_LEFT;
    if(!strcmp(s, "right")) return RR_KEY_RIGHT;
    if(!strcmp(s, "up")) return RR_KEY_UP;
    if(!strcmp(s, "down")) return RR_KEY_DOWN;
    if(!strcmp(s, "art")) return RR_KEY_ART;
    return RR_KEY_SPACE;
}

static float s_debug_rob_x = -1;
static int s_seed = -1;          /* --seed: deterministic level generation for tests */
static bool s_bot;               /* --bot: autopilot plays the levels */
static int s_bot_until = RR_LEVEL_COUNT; /* --bot-levels N: stop after level N is won */
static bool s_bot_noup;          /* --bot-noup: no UP button (real gravity, planned jumps) */
static bool s_bot_single;        /* --bot-single: no UP and no double jump */
static const char * s_record;    /* --record file.raw: append frames (BGRA) */
static int s_record_every = 3;   /* --record-every N: every Nth frame (60/3 = 20 fps) */

static bool s_save_given;
static bool s_music_ui;          /* --music-ui: show the music/art tiles without playing (screenshots) */

/* --bot in a window (or on a framebuffer): the bot plays where it can be
 * watched, 16 ms a frame as headless -- LVGL's clock too, a step per pass
 * of the loop, so with --seed it is the same run --, says how it went and
 * ends. The best times are left alone. */
static uint32_t s_bot_tick;
static uint32_t bot_tick_cb(void) { return s_bot_tick; }

static bool windowed_bot_on(void) { return s_bot; }

static void windowed_bot_tick(void)
{
    if(s_bot) s_bot_tick += 16;
}

static bool windowed_bot_begin(int level)
{
    if(!s_bot) return false;
    s_bot_tick = lv_tick_get();
    lv_tick_set_cb(bot_tick_cb);
    if(!s_save_given) game_set_save_path("/dev/null");
    if(s_seed >= 0) srand((unsigned)s_seed);
    game_goto_level(level);
    bot_init(s_bot_until, s_bot_single ? BOT_MODE_SINGLE : s_bot_noup ? BOT_MODE_NOUP : BOT_MODE_GLIDE);
    return true;
}

static bool windowed_bot_step(void)
{
    static int32_t f;
    if(!s_bot) return false;
    if(!bot_step(f++)) {
        bot_print_report();
        exit(0);
    }
    return true;
}

static int run_headless(int frames, const char * out_path, int level)
{
    find_data(); /* as a window does: a package started from another folder finds its art */
    if(!s_save_given) game_set_save_path("/dev/null");
    lv_init();
    lv_tick_set_cb(fake_tick_cb);

    const int w = SCR_W, h = SCR_H;
    lv_display_t * disp = lv_display_create(w, h);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);
    size_t buf_size = (size_t)w * (size_t)h * 4;
    uint8_t * fb = calloc(1, buf_size);
    lv_display_set_buffers(disp, fb, NULL, (uint32_t)buf_size, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, headless_flush_cb);

    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, headless_indev_read);

    create_canvas();
    game_init(w, h, VIEW_SCALE);
    rob3d_view_init(); /* ROLLING_ROB_3D=1: the frames in 3D */
    if(s_seed >= 0) {
        srand((unsigned)s_seed);
        game_goto_level(level); /* regenerate deterministically */
    }
    else if(level > 0) game_goto_level(level);
    if(s_debug_rob_x >= 0) game_debug_set_rob_x(s_debug_rob_x);
    if(s_title_shot) {
        game_title_begin();
        if(level > 0) game_goto_level(level); /* --title --level N: that level behind it */
    }
    if(s_music_ui) {
        game_set_music_available(true);
        hud_enable_art_tile(true);
    }
    if(s_app_headless) app_menu_button();
    if(!s_no_hud) hud_create(lv_screen_active());
    if(s_app_headless) app_init(disp, w, h, 1);
    if(s_fps) fps_overlay_create(disp, NULL);

    FILE * rec = NULL;
    if(s_record) {
        rec = fopen(s_record, "wb");
        if(rec == NULL) fprintf(stderr, "cannot write %s\n", s_record);
    }
    if(s_bot) bot_init(s_bot_until, s_bot_single ? BOT_MODE_SINGLE : s_bot_noup ? BOT_MODE_NOUP : BOT_MODE_GLIDE);

    int f;
    for(f = 0; f < frames; f++) {
        s_fake_tick += 16;
        for(int i = 0; i < s_key_n; i++) {
            game_input_key(s_keys[i].key, f >= s_keys[i].f0 && f < s_keys[i].f1);
        }
        if(s_bot && !bot_step(f)) { frame(16); lv_timer_handler(); lv_refr_now(disp); break; }
        if(s_app_headless) {
            for(int i = 0; i < s_app_key_n; i++)
                if(s_app_keys[i].f == f) app_key(s_app_keys[i].key);
            if(app_game_runs()) frame(16);
            app_tick(16);
        }
        else frame(16);
        lv_timer_handler();
        /* Force a full refresh so the buffer always holds the complete frame. */
        lv_refr_now(disp);
        if(rec && f % s_record_every == 0) fwrite(fb, 1, buf_size, rec);
    }
    if(rec) {
        /* hold the last frame for a second */
        for(int i = 0; i < 60 / s_record_every; i++) fwrite(fb, 1, buf_size, rec);
        fclose(rec);
        printf("recorded %d frames to %s\n", f / s_record_every, s_record);
    }
    if(s_bot) bot_print_report();

    FILE * fp = fopen(out_path, "wb");
    if(fp == NULL) {
        fprintf(stderr, "cannot write %s\n", out_path);
        return 1;
    }
    fwrite(fb, 1, buf_size, fp);
    fclose(fp);
    printf("wrote %s (%dx%d ARGB8888), level %d, distance %dm, msg: %s\n", out_path, w, h,
           (int)game_get_level() + 1, (int)game_get_distance_m(), game_get_message());
    return 0;
}

/* ---------------- main ---------------- */

int main(int argc, char ** argv)
{
    SDL_SetMainReady();
#ifdef _WIN32
    /* Windows: the screen's own pixels at 125 % or 150 %, in points as on a
     * Mac (rr_sdl_create_window asks for them), instead of a stretched
     * picture */
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
#ifdef __EMSCRIPTEN__
    /* web/shell.html mounts IndexedDB here, so times and choices persist. */
    game_set_save_path("/save/rolling_rob_times");
#endif
    bool headless = false;
    int frames = 60;
    int level = 0;
    float zoom = (float)ROLLING_ROB_SIM_ZOOM;
    const char * out = "frame.raw";

    for(int i = 1; i < argc; i++) {
        if(!strcmp(argv[i], "--headless")) headless = true;
        else if(!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if(!strcmp(argv[i], "--level") && i + 1 < argc) level = atoi(argv[++i]) - 1;
        else if(!strcmp(argv[i], "--zoom") && i + 1 < argc) { zoom = (float)atof(argv[++i]); s_zoom_given = true; }
        else if(!strcmp(argv[i], "--scale") && i + 1 < argc) {
            s_k = atoi(argv[++i]);
            if(s_k < 1) s_k = 1;
            if(s_k > 4) s_k = 4;
            s_scale_given = true;
        }
        else if(!strcmp(argv[i], "--no-touch-ui")) game_set_touch_controls(false);
        else if(!strcmp(argv[i], "--no-hud")) s_no_hud = true;
        else if(!strcmp(argv[i], "--fps")) s_fps = true;
        else if(!strcmp(argv[i], "--seed") && i + 1 < argc) s_seed = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--bot")) s_bot = true;
        else if(!strcmp(argv[i], "--music-ui")) s_music_ui = true;
        else if(!strcmp(argv[i], "--no-title")) s_no_title = true;
        else if(!strcmp(argv[i], "--title")) s_title_shot = true;
        else if(!strcmp(argv[i], "--rgb565")) rr_art_set_rgb565(true);
        else if(!strcmp(argv[i], "--backdrop")) {
            rr_art_set_rgb565(true);
            rr_art_set_backdrop(true);
            s_backdrop = true;
        }
        else if(!strcmp(argv[i], "--bot-levels") && i + 1 < argc) s_bot_until = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--bot-noup")) { s_bot = true; s_bot_noup = true; }
        else if(!strcmp(argv[i], "--bot-single")) { s_bot = true; s_bot_single = true; }
        else if(!strcmp(argv[i], "--record") && i + 1 < argc) s_record = argv[++i];
        else if(!strcmp(argv[i], "--record-every") && i + 1 < argc) s_record_every = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--save") && i + 1 < argc) { game_set_save_path(argv[++i]); s_save_given = true; }
        else if(!strcmp(argv[i], "--rob-x") && i + 1 < argc) s_debug_rob_x = (float)atof(argv[++i]);
        else if(!strcmp(argv[i], "--touch") && i + 1 < argc && s_touch_n < MAX_SCRIPT) {
            touch_evt_t t;
            if(sscanf(argv[++i], "%d,%d,%d,%d", &t.f0, &t.f1, &t.x, &t.y) == 4) s_touches[s_touch_n++] = t;
        }
        else if(!strcmp(argv[i], "--app")) s_app_headless = true;
        else if(!strcmp(argv[i], "--app-key") && i + 1 < argc && s_app_key_n < MAX_SCRIPT) {
            char name[16];
            int f;
            if(sscanf(argv[++i], "%15[a-z0-9],%d", name, &f) == 2) {
                int k = name[1] == 0 ? name[0] : !strcmp(name, "esc") ? APP_KEY_ESC : !strcmp(name, "enter") ? APP_KEY_ENTER
                        : !strcmp(name, "space") ? APP_KEY_SPACE : !strcmp(name, "left") ? APP_KEY_LEFT
                        : !strcmp(name, "right") ? APP_KEY_RIGHT : !strcmp(name, "up") ? APP_KEY_UP
                        : !strcmp(name, "down") ? APP_KEY_DOWN : 0;
                if(k) s_app_keys[s_app_key_n++] = (app_key_evt_t){ k, f };
            }
        }
        else if(!strcmp(argv[i], "--key") && i + 1 < argc && s_key_n < MAX_SCRIPT) {
            char name[16];
            key_evt_t k;
            if(sscanf(argv[++i], "%15[a-z],%d,%d", name, &k.f0, &k.f1) == 3) {
                k.key = parse_key(name);
                s_keys[s_key_n++] = k;
            }
        }
        else {
            fprintf(stderr, "usage: %s [--scale 1..4] [--zoom Z] [--level N] [--no-touch-ui] [--no-hud] [--fps]\n"
                            "       %s --headless --frames N --out file.raw [--level N] [--rob-x X] [--seed N] [--no-hud]\n"
                            "          [--touch F0,F1,X,Y] [--key name,F0,F1] [--bot [--bot-levels N]] [--record f.raw [--record-every N]] [--save times.txt]\n",
                    argv[0], argv[0]);
            return 2;
        }
    }

    if(headless) return run_headless(frames, out, level);
#ifdef __EMSCRIPTEN__
    /* a larger picture needs less window zoom for the same window (the
     * desktop chooses in choose_size) */
    if(!s_zoom_given && s_k > 1) zoom = zoom / (float)s_k < 1.0f ? 1.0f : zoom / (float)s_k;
#endif
    return run_windowed(zoom, level);
}
