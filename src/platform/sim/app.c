/*
 * The app around the game, desktop and web (see app.h).
 *
 *   title    the game's title screen with buttons: Play (Space), Tetris (T),
 *            Info (I). Esc in the game comes back here.
 *   tetris   tetris_view.c.
 *   info     what this device and browser offer: picture, frame rate,
 *            WebGL and GPU, touch points (with a live finger count), cores,
 *            memory, storage, offline state. Esc or I closes.
 *
 * Laid out for 480x272 like the HUD and scaled with it (ui.h).
 */

#include "app.h"
#include "app_ui.h"
#include "game.h"
#include "hud.h"
#include "ui.h"
#include "draw.h"
#include "tetris_view.h"
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/heap.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef enum { MODE_PLAY, MODE_TETRIS, MODE_DEMO, MODE_INFO } app_mode_t;

static app_mode_t s_mode = MODE_PLAY;
static lv_display_t * s_disp;
static int s_w, s_h, s_k;

/* ---------------- keys ---------------- */

#define KEYQ 16
static SDL_Scancode s_keyq[KEYQ];
static int s_keyn;

static int SDLCALL key_watch(void * user, SDL_Event * e)
{
    (void)user;
    if(e->type == SDL_KEYDOWN && !e->key.repeat && s_keyn < KEYQ) s_keyq[s_keyn++] = e->key.keysym.scancode;
    return 0;
}

/* ---------------- statistics: frame rate, loop time, fingers ---------------- */

static uint32_t s_refr, s_refr_start;
static float s_fps;
static float s_loop_ms;
#define MAX_FINGERS 16
static int64_t s_finger[MAX_FINGERS];
static int s_fingers, s_fingers_max;

static void refr_cb(lv_event_t * e)
{
    (void)e;
    s_refr++;
}

static void count_frames(void)
{
    uint32_t el = lv_tick_elaps(s_refr_start);
    if(el < 1000) return;
    s_fps = (float)s_refr * 1000.0f / (float)el;
    s_refr = 0;
    s_refr_start = lv_tick_get();
}

void app_note_loop_ms(float ms) { s_loop_ms = s_loop_ms == 0 ? ms : s_loop_ms * 0.95f + ms * 0.05f; }

void app_note_finger(bool down, int64_t id)
{
    int at = -1;
    for(int i = 0; i < s_fingers; i++)
        if(s_finger[i] == id) at = i;
    if(down && at < 0 && s_fingers < MAX_FINGERS) {
        s_finger[s_fingers++] = id;
        if(s_fingers > s_fingers_max) s_fingers_max = s_fingers;
    }
    if(!down && at >= 0) s_finger[at] = s_finger[--s_fingers];
}

/* ---------------- widgets ---------------- */

lv_obj_t * app_button(lv_obj_t * parent, const char * text, bool primary, lv_event_cb_t cb, void * user)
{
    lv_obj_t * b = lv_button_create(parent);
    lv_obj_set_style_bg_color(b, primary ? GOLD : TILE, 0);
    lv_obj_set_style_bg_color(b, primary ? lv_color_make(0xE0, 0xB8, 0x00) : lv_color_make(0x3A, 0x40, 0x3A), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, primary ? GOLD : lv_color_make(0x55, 0x5A, 0x55), 0);
    lv_obj_set_style_border_width(b, K(1), 0);
    lv_obj_set_style_radius(b, K(4), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, K(8), 0);
    lv_obj_set_style_pad_ver(b, K(4), 0);
    lv_obj_t * l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, FONT(12), 0);
    lv_obj_set_style_text_color(l, primary ? lv_color_make(0x11, 0x11, 0x11) : lv_color_white(), 0);
    lv_obj_center(l);
    if(cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t * app_panel(void)
{
    lv_obj_t * p = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(p, PANEL, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(p, false);
    lv_obj_set_hidden(p, true);
    return p;
}

lv_obj_t * app_heading(lv_obj_t * parent, const char * text)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, FONT(14), 0);
    lv_obj_set_style_text_color(l, GOLD, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, K(10), K(6));
    return l;
}

lv_obj_t * app_close_button(lv_obj_t * parent, lv_event_cb_t cb)
{
    lv_obj_t * b = app_button(parent, LV_SYMBOL_CLOSE, false, cb, NULL);
    lv_obj_set_style_pad_hor(b, K(6), 0);
    lv_obj_set_style_pad_ver(b, K(3), 0);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, K(-8), K(5));
    return b;
}

static void go(app_mode_t m);

/* ---------------- title menu ---------------- */

static lv_obj_t * s_title_menu;

static void play_cb(lv_event_t * e)
{
    (void)e;
    game_input_key(RR_KEY_SPACE, true); /* the game's own start: Space */
    game_input_key(RR_KEY_SPACE, false);
}
static void tetris_cb(lv_event_t * e) { (void)e; go(MODE_TETRIS); }
static void info_cb(lv_event_t * e) { (void)e; go(MODE_INFO); }

static void title_menu_create(void)
{
    s_title_menu = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_title_menu);
    lv_obj_set_size(s_title_menu, LV_PCT(100), K(46));
    lv_obj_align(s_title_menu, LV_ALIGN_CENTER, 0, hud_title_menu_y() + K(8));
    lv_obj_set_clickable(s_title_menu, false);
    lv_obj_set_scrollable(s_title_menu, false);

    lv_obj_t * row = lv_obj_create(s_title_menu);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_clickable(row, false);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, K(10), 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 0);
    app_button(row, LV_SYMBOL_PLAY "  Play", true, play_cb, NULL);
    app_button(row, LV_SYMBOL_SHUFFLE "  Tetris", false, tetris_cb, NULL);
    app_button(row, LV_SYMBOL_SETTINGS "  Info", false, info_cb, NULL);

    lv_obj_t * hint = lv_label_create(s_title_menu);
    lv_label_set_text(hint, "Space play   T tetris   I info");
    lv_obj_set_style_text_font(hint, FONT(10), 0);
    lv_obj_set_style_text_color(hint, lv_color_make(0xDD, 0xDD, 0xDD), 0);
    lv_obj_set_style_bg_color(hint, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_40, 0);
    lv_obj_set_style_pad_hor(hint, K(3), 0);
    lv_obj_set_style_radius(hint, K(2), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);
}

/* ---------------- info ---------------- */

#ifdef __EMSCRIPTEN__
/* The probes are page script (web/shell.html: rrInfoStart, rrInfoGet). */
EM_JS_DEPS(rr_info, "$stringToNewUTF8,$UTF8ToString");
EM_JS(void, web_info_start, (void), {
    if(window.rrInfoStart) window.rrInfoStart();
});
EM_JS(char *, web_info_get, (const char * key), {
    var v = window.rrInfoGet ? window.rrInfoGet(UTF8ToString(key)) : "";
    return stringToNewUTF8(String(v));
});
#endif

static lv_obj_t * s_info, * s_info_cols[2][2];

static void info_close_cb(lv_event_t * e) { (void)e; go(MODE_PLAY); }

static void info_create(void)
{
    s_info = app_panel();
    lv_obj_set_style_bg_opa(s_info, LV_OPA_90, 0);
    app_heading(s_info, "SYSTEM INFORMATION");
    app_close_button(s_info, info_close_cb);
    for(int c = 0; c < 2; c++)
        for(int kv = 0; kv < 2; kv++) {
            lv_obj_t * l = lv_label_create(s_info);
            lv_label_set_recolor(l, true);
            lv_obj_set_style_text_font(l, FONT(10), 0);
            lv_obj_set_style_text_color(l, kv ? lv_color_white() : DIM, 0);
            lv_obj_set_pos(l, K(10 + c * 236 + kv * 76), K(28));
            if(kv) {
                lv_obj_set_width(l, K(156));
                lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
            }
            s_info_cols[c][kv] = l;
        }
}

/* Two columns of "key  value" lines; a key starting with '#' is a heading. */
typedef struct {
    char k[2][900], v[2][900];
} cols_t;

static void put(cols_t * t, int col, const char * key, const char * fmt, ...)
{
    char val[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(val, sizeof(val), fmt, ap);
    va_end(ap);
    if(strlen(val) > 30) strcpy(val + 27, "...");
    char * K_ = t->k[col], * V = t->v[col];
    size_t kl = strlen(K_), vl = strlen(V);
    if(key[0] == '#') {
        snprintf(K_ + kl, sizeof(t->k[col]) - kl, "%s#ffd700 %s#\n", kl ? "\n" : "", key + 1);
        snprintf(V + vl, sizeof(t->v[col]) - vl, "%s\n", vl || kl ? "\n" : "");
    }
    else {
        snprintf(K_ + kl, sizeof(t->k[col]) - kl, "%s\n", key);
        snprintf(V + vl, sizeof(t->v[col]) - vl, "%s\n", val);
    }
}

#ifdef __EMSCRIPTEN__
static const char * web(const char * key)
{
    static char out[8][128];
    static int n;
    char * s = web_info_get(key);
    char * o = out[n++ & 7];
    snprintf(o, 128, "%s", s ? s : "");
    free(s);
    return o;
}
#endif

static void info_update(void)
{
    static cols_t t;
    memset(&t, 0, sizeof(t));
    SDL_Renderer * r = lv_sdl_window_get_renderer(s_disp);
    SDL_RendererInfo ri = { 0 };
    if(r) SDL_GetRendererInfo(r, &ri);
    bool gl = ri.name && strcmp(ri.name, "software") != 0;

    put(&t, 0, "#THIS APP", "");
#ifdef __EMSCRIPTEN__
    put(&t, 0, "Version", "%s", web("version"));
#else
    put(&t, 0, "Built", "%s", __DATE__);
#endif
    put(&t, 0, "Picture", "%dx%d (%dx LCD)", s_w, s_h, s_k);
    put(&t, 0, "Frame rate", "%.1f fps", (double)s_fps);
    put(&t, 0, "Busy/frame", "%.1f ms of 16.7", (double)s_loop_ms);
    put(&t, 0, "Draw threads", "%d", rr_draw_threads() > 0 ? rr_draw_threads() : 1);
#ifdef __EMSCRIPTEN__
    put(&t, 0, "Drawn with", "%s", gl ? "WebGL (GPU shows it)" : "2D canvas (no WebGL)");
#else
    put(&t, 0, "Drawn with", "SDL %s", ri.name ? ri.name : "?");
#endif

    put(&t, 0, "#SCREEN", "");
#ifdef __EMSCRIPTEN__
    put(&t, 0, "Screen", "%s", web("screen"));
    put(&t, 0, "Window", "%s", web("window"));
    put(&t, 0, "Refresh", "%s", web("hz"));
#else
    SDL_DisplayMode dm;
    if(SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        put(&t, 0, "Screen", "%dx%d", dm.w, dm.h);
        put(&t, 0, "Refresh", "%d Hz", dm.refresh_rate);
    }
#endif

    put(&t, 0, "#GRAPHICS", "");
#ifdef __EMSCRIPTEN__
    put(&t, 0, "WebGL", "%s", web("gl"));
    put(&t, 0, "GPU", "%s", web("gpu"));
    put(&t, 0, "Max texture", "%s", web("maxtex"));
#else
    put(&t, 0, "Max texture", "%dx%d", ri.max_texture_width, ri.max_texture_height);
#endif
    put(&t, 0, "VRAM", "not reported (no API)");
    put(&t, 0, "Our picture", "%.1f MB a frame", (double)s_w * s_h * 4 / 1048576.0);

    put(&t, 1, "#TOUCH AND INPUT", "");
#ifdef __EMSCRIPTEN__
    put(&t, 1, "Touch", "%s", web("touch"));
    put(&t, 1, "Pointer", "%s", web("pointer"));
#else
    put(&t, 1, "Touch", "%d", SDL_GetNumTouchDevices());
#endif
    put(&t, 1, "Fingers now", "%d  (most at once: %d)", s_fingers, s_fingers_max);
    put(&t, 1, "", "put fingers on the screen");

    put(&t, 1, "#PROCESSOR AND MEMORY", "");
#ifdef __EMSCRIPTEN__
    put(&t, 1, "Cores", "%s", web("cores"));
    put(&t, 1, "Threads", "%s", web("threads"));
    put(&t, 1, "WASM SIMD", "%s", web("simd"));
    put(&t, 1, "Memory", "%s", web("devmem"));
    put(&t, 1, "App memory", "%.0f MB%s", (double)emscripten_get_heap_size() / 1048576.0, web("jsheap"));
#else
    put(&t, 1, "Cores", "%d", SDL_GetCPUCount());
    put(&t, 1, "Memory", "%d MB", SDL_GetSystemRAM());
    put(&t, 1, "SIMD", "%s%s%s", SDL_HasNEON() ? "NEON " : "", SDL_HasAVX2() ? "AVX2 " : "", SDL_HasSSE41() ? "SSE4.1" : "");
#endif

    put(&t, 1, "#SYSTEM", "");
#ifdef __EMSCRIPTEN__
    put(&t, 1, "System", "%s", web("system"));
    put(&t, 1, "Browser", "%s", web("browser"));
    put(&t, 1, "Offline", "%s", web("offline"));
    put(&t, 1, "Storage", "%s", web("storage"));
    put(&t, 1, "Sound", "%s", web("audio"));
#else
    put(&t, 1, "System", "%s", SDL_GetPlatform());
    const char * ad = SDL_GetCurrentAudioDriver();
    put(&t, 1, "Sound", "%s", ad ? ad : "off");
#endif

    for(int c = 0; c < 2; c++) {
        if(strcmp(lv_label_get_text(s_info_cols[c][0]), t.k[c])) lv_label_set_text(s_info_cols[c][0], t.k[c]);
        if(strcmp(lv_label_get_text(s_info_cols[c][1]), t.v[c])) lv_label_set_text(s_info_cols[c][1], t.v[c]);
    }
}

/* ---------------- modes ---------------- */

static void go(app_mode_t m)
{
    app_mode_t was = s_mode;
    s_mode = m;
    tetris_view_show(m == MODE_TETRIS);
    lv_obj_set_hidden(s_info, m != MODE_INFO);
    if(m == MODE_INFO) {
#ifdef __EMSCRIPTEN__
        web_info_start();
#endif
        s_fingers_max = s_fingers;
        info_update();
    }
    if(m == MODE_PLAY && was != MODE_PLAY && !game_title_active()) game_title_begin();
}

bool app_game_runs(void) { return s_mode == MODE_PLAY || s_mode == MODE_INFO; }
bool app_game_input(void) { return s_mode == MODE_PLAY && !hud_is_modal_open(); }

static void keys(void)
{
    for(int q = 0; q < s_keyn; q++) {
        SDL_Scancode k = s_keyq[q];
        bool back = k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_BACKSPACE;
        switch(s_mode) {
            case MODE_PLAY:
                if(hud_is_modal_open()) break;
                if(game_title_active()) {
                    if(k == SDL_SCANCODE_T) go(MODE_TETRIS);
                    if(k == SDL_SCANCODE_I) go(MODE_INFO);
                }
                else if(k == SDL_SCANCODE_ESCAPE) {
                    game_title_begin();
                }
                break;
            case MODE_DEMO:
                break;
            case MODE_INFO:
                if(back || k == SDL_SCANCODE_I) go(MODE_PLAY);
                break;
            case MODE_TETRIS:
                if(k == SDL_SCANCODE_ESCAPE) go(MODE_PLAY);
                else tetris_view_key(k);
                break;
        }
    }
    s_keyn = 0;
}

void app_tick(uint32_t dt_ms)
{
    keys();
    count_frames();
    bool menu = s_mode == MODE_PLAY && game_title_active() && !hud_is_modal_open();
    if(menu == lv_obj_is_hidden(s_title_menu)) lv_obj_set_hidden(s_title_menu, !menu);
    if(s_mode == MODE_TETRIS) tetris_view_tick(dt_ms);
    if(s_mode == MODE_INFO) {
        static uint32_t at;
        if(lv_tick_elaps(at) >= 500) {
            at = lv_tick_get();
            info_update();
        }
    }
}

static void menu_from_picker(void) { game_title_begin(); }
static void menu_from_tetris(void) { go(MODE_PLAY); } /* and from the demo */

void app_touch(int slot, bool down, int32_t x, int32_t y)
{
    if(s_mode == MODE_TETRIS) tetris_view_touch(slot, down, x, y);
}

void app_menu_button(void) { hud_set_menu_button(menu_from_picker); }

void app_init(lv_display_t * disp, int w, int h, int k)
{
    s_disp = disp;
    s_w = w;
    s_h = h;
    s_k = k;
    game_title_set_tap_start(false);
    hud_title_set_menu(true);
    tetris_view_create(lv_screen_active(), w, h);
    tetris_view_set_menu(menu_from_tetris);
    title_menu_create();
    info_create();
    lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_READY, NULL);
    s_refr_start = lv_tick_get();
    SDL_AddEventWatch(key_watch, NULL);
}
