/*
 * The app around the game: desktop and web (see app.h).
 *
 *   title    Rolling Rob's title screen: Space plays, Enter (or the Menu
 *            button) opens the menu; Rolling Rob's own keys work here (A
 *            look, M / N music, F frame counter, L levels).
 *   menu     the games as tiles (Rolling Rob, Tetris, 2048, 3D, Snake,
 *            Breakout, Minesweeper), and the rest: info. Arrows and Enter, or the key on a tile.
 *            Esc: the title screen.
 *   page     each game's page (app_page.c): Play / Continue, New game, and
 *            what that game lets you set -- Rolling Rob's level, look, music
 *            and frame counter, Minesweeper's size and "no guessing". It is
 *            the pause too: Esc in a game opens its page, Esc there the menu.
 *   levels   Rolling Rob's level picker (hud.c): from its page, or L on the
 *            title screen and while playing; arrows and Enter.
 *   tetris   tetris_view.c.
 *   snake, breakout, minesweeper
 *            snake_view.c, breakout_view.c, mines_view.c (keys N, B, M), on one
 *            shared canvas (pix.c).
 *   2048     g2048_view.c (key 2).
 *   3D       world3d_view.c (key 3): the platform draws its frame
 *            (app_draws_frame, app_render_bands).
 *   fractals fractal_view.c (key F, under "more"): drawn like the 3D world.
 *   info     what this device offers: picture, frame rate, graphics, touch
 *            points (with a live finger count), processor, memory -- the
 *            lines come from the platform (app_info_fill). Esc or I closes.
 *
 * Laid out for 480x272 like the HUD and scaled with it (ui.h).
 */

#include "app.h"
#include "app_ui.h"
#include "game.h"
#include "hud.h"
#include "ui.h"
#include "draw.h"
#include "art.h"
#include "fps_overlay.h"
#include "tetris_view.h"
#include "g2048_view.h"
#include "world3d_view.h"
#include "rob3d_view.h"
#include "snake_view.h"
#include "breakout_view.h"
#include "mines_view.h"
#include "fractal_view.h"
#include "pix.h"
#include "app_page.h"
#if defined(ROLLING_ROB_TARGET_SIM) && !defined(__EMSCRIPTEN__)
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef enum {
    MODE_PLAY, MODE_HOME, MODE_PAGE, MODE_TETRIS, MODE_2048, MODE_3D, MODE_SNAKE, MODE_BREAKOUT, MODE_MINES,
    MODE_FRACTAL, MODE_DEMO, MODE_VIDEO, MODE_INFO
} app_mode_t;

static app_mode_t s_mode = MODE_PLAY;
static lv_display_t * s_disp;
static int s_w, s_h, s_k;

/* ---------------- keys ---------------- */

#define KEYQ 16
static int s_keyq[KEYQ];
static int s_keyn;

void app_key(int k)
{
    if(s_keyn < KEYQ) s_keyq[s_keyn++] = k;
}

/* ---------------- statistics: frame rate, loop time, fingers ---------------- */

static uint32_t s_refr, s_refr_start;
static float s_fps;
static float s_loop_ms;
#define MAX_FINGERS 16
static int64_t s_finger[MAX_FINGERS];
static int s_fingers, s_fingers_max;

static uint32_t (*s_frames)(void);

static void refr_cb(lv_event_t * e)
{
    (void)e;
    s_refr++;
}

void app_set_frame_counter(uint32_t (*frames)(void)) { s_frames = frames; }

static void count_frames(void)
{
    static uint32_t base;
    uint32_t el = lv_tick_elaps(s_refr_start);
    if(el < 1000) return;
    uint32_t n = s_refr;
    if(s_frames) {
        uint32_t now = s_frames();
        n = now - base;
        base = now;
    }
    s_fps = (float)n * 1000.0f / (float)el;
    s_refr = 0;
    s_refr_start = lv_tick_get();
}

void app_save_path(char * out, size_t n, const char * name)
{
#ifdef __EMSCRIPTEN__
    snprintf(out, n, "/save/%s_best", name);
#else
    const char * home = getenv("HOME");
    if(home && home[0]) snprintf(out, n, "%s/.rolling_rob_%s", home, name);
    else {
#ifdef ROLLING_ROB_TARGET_SIM
        char * pref = SDL_GetPrefPath("Vader", "Rolling Rob");
        snprintf(out, n, "%s%s_best.txt", pref ? pref : "", name);
        SDL_free(pref);
#else
        snprintf(out, n, "/tmp/rolling_rob_%s", name);
#endif
    }
#endif
}

lv_display_t * app_display(void) { return s_disp; }
void app_picture(int * w, int * h, int * k) { *w = s_w; *h = s_h; *k = s_k; }
float app_fps(void) { return s_fps; }
float app_loop_ms(void) { return s_loop_ms; }
int app_fingers(int * most) { *most = s_fingers_max; return s_fingers; }

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

/* ---------------- title screen ---------------- */

static lv_obj_t * s_title_menu;
static bool s_paused; /* Rolling Rob left mid-level: its page's Continue goes on */

static void start_game(void)
{
    game_input_key(RR_KEY_SPACE, true); /* the game's own start: Space */
    game_input_key(RR_KEY_SPACE, false);
}

static void play_cb(lv_event_t * e) { (void)e; start_game(); }
static void menu_cb(lv_event_t * e) { (void)e; go(MODE_HOME); }

static lv_obj_t * hint_label(lv_obj_t * parent, const char * text)
{
    lv_obj_t * hint = lv_label_create(parent);
    lv_label_set_text(hint, text);
    lv_obj_set_style_text_font(hint, FONT(10), 0);
    lv_obj_set_style_text_color(hint, lv_color_make(0xDD, 0xDD, 0xDD), 0);
    lv_obj_set_style_bg_color(hint, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_40, 0);
    lv_obj_set_style_pad_hor(hint, K(3), 0);
    lv_obj_set_style_radius(hint, K(2), 0);
    return hint;
}

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
    app_button(row, LV_SYMBOL_LIST "  Menu", false, menu_cb, NULL);

    lv_obj_t * hint = hint_label(s_title_menu, "Space: play     Enter: the menu with every game");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);
}

/* ---------------- the games ---------------- */

typedef enum { G_ROB, G_TETRIS, G_2048, G_3D, G_SNAKE, G_BREAKOUT, G_MINES, G_FRACTAL, G_COUNT } game_id_t;

static const app_mode_t k_game_mode[G_COUNT] = { MODE_PLAY, MODE_TETRIS, MODE_2048, MODE_3D,
                                                 MODE_SNAKE, MODE_BREAKOUT, MODE_MINES, MODE_FRACTAL };
static game_id_t s_page_game; /* whose page MODE_PAGE shows */
static int s_rob_level;       /* the level Rolling Rob's Play starts */
static bool s_rob_level_moved; /* changed on the page: its picture loads it */
static bool s_picker_from_page;

static void open_page(game_id_t g)
{
    s_page_game = g;
    go(MODE_PAGE);
}

/* The game the mode belongs to (a game's own home button opens its page). */
static void to_own_page(void)
{
    for(int g = 0; g < G_COUNT; g++)
        if(k_game_mode[g] == s_mode) open_page((game_id_t)g);
}

/* ---- Rolling Rob ---- */

static const char * rob_play_label(void)
{
    static char buf[64];
    if(s_paused) snprintf(buf, sizeof(buf), "Continue level %d", (int)game_get_level() + 1);
    else snprintf(buf, sizeof(buf), "Play level %d", s_rob_level + 1);
    return buf;
}

static void rob_play(int dir)
{
    (void)dir;
    bool resume = s_paused;
    go(MODE_PLAY);
    if(!resume) game_play_level(s_rob_level);
}

static const char * rob_level_value(void)
{
    static char buf[48];
    snprintf(buf, sizeof(buf), "%d  %s", s_rob_level + 1, game_get_level_name(s_rob_level));
    return buf;
}

static void rob_level(int dir)
{
    int n = (int)game_get_level_count();
    if(dir) {
        s_rob_level = (s_rob_level + dir + n) % n;
        s_paused = false; /* another level: the one under way is given up */
        /* the picture loads it (the title screen then glides over it), once
         * the arrows have rested: a level takes a moment to load */
        s_rob_level_moved = true;
        page_pic_later(300);
        return;
    }
    /* Enter: the picker with the pictures and the best times */
    s_picker_from_page = true;
    go(MODE_PLAY);
    hud_picker_open_at(s_rob_level);
}

static const char * rob_look_value(void) { return !rr_art_enabled() ? "original" : rr_art_rendered() ? "rendered" : "painted"; }
static void rob_look(int dir)
{
    (void)dir;
    game_input_key(RR_KEY_ART, true); /* the A key's path, which saves it */
    game_input_key(RR_KEY_ART, false);
}
static const char * rob_music_value(void) { return game_music_enabled() ? "on" : "off"; }
static void rob_music(int dir) { (void)dir; game_set_music_enabled(!game_music_enabled()); }
/* the track playing and how far it is; left / right: the one before / after */
static const char * rob_track_value(void)
{
    static char buf[96];
    int32_t ms;
    const char * t = game_music_playing(&ms);
    if(!game_music_enabled()) return "-";
    if(!t[0]) return "...";
    snprintf(buf, sizeof(buf), "%s  %d:%02d", t, (int)(ms / 60000), (int)(ms / 1000 % 60));
    return buf;
}
static void rob_track(int dir)
{
    if(!game_music_enabled()) game_set_music_enabled(true);
    game_music_request_skip(dir < 0 ? -1 : 1);
}
static const char * rob_volume_value(void)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d %%", game_music_volume());
    return buf;
}
static void rob_volume(int dir)
{
    int v = game_music_volume();
    if(dir) v += dir * 10;
    else v = v >= 100 ? 0 : v + 10; /* Enter: louder, then round again */
    game_set_music_volume(v < 0 ? 0 : v > 100 ? 100 : v / 10 * 10);
}
static const char * rob_order_value(void)
{
    static const char * names[RR_MUSIC_ORDERS] = { "shuffled", "in order", "this track again" };
    return names[game_music_order()];
}
static void rob_order(int dir)
{
    int o = ((int)game_music_order() + (dir < 0 ? -1 : 1) + RR_MUSIC_ORDERS) % RR_MUSIC_ORDERS;
    game_set_music_order((rr_music_order_t)o);
}
static bool rob_has_music(void) { return game_music_available(); }
static const char * rob_fps_value(void) { return game_fps_enabled() ? "on" : "off"; }
static void rob_fps(int dir) { (void)dir; game_set_fps_enabled(!game_fps_enabled()); }

/* The picture size (the desktop program): auto or 1-4 times the LCD, from
 * the next start; the value says what runs now, or that a restart is due. */
static const char * rob_scale_value(void)
{
    static char buf[48];
    int c = sim_scale_choice(), now = sim_scale_now();
    static const char * px[5] = { "", "480x272", "960x544", "1440x816", "1920x1088" };
    if(c == 0) snprintf(buf, sizeof(buf), "auto (%s)", px[now]);
    else if(c == now) snprintf(buf, sizeof(buf), "%s", px[c]);
    else snprintf(buf, sizeof(buf), "%s, restart", px[c]); /* takes effect at the next start */
    if(c == 0 && sim_scale_pending()) snprintf(buf, sizeof(buf), "auto, restart");
    return buf;
}
static void rob_scale(int dir)
{
    /* auto, x1 .. as large as fits (x4 in a window), round again */
    int n = sim_scale_max() + 1, c = sim_scale_choice();
    if(c >= n) c = 0;
    c = (c + (dir < 0 ? n - 1 : 1)) % n;
    sim_scale_set_choice(c);
}
static bool rob_scale_shown(void) { return sim_scale_choosable(); }
/* the web page restarts at once (it keeps the best times); the desktop
 * program uses the choice at its next start */
static void rob_scale_restart(int dir) { (void)dir; sim_scale_restart(); }
static bool rob_scale_restart_shown(void) { return sim_scale_can_restart() && sim_scale_pending(); }

static const char * rob_best(void)
{
    static char buf[64];
    int32_t ms = game_get_best_run_ms();
    if(ms <= 0) return "Best run of all levels: none yet";
    snprintf(buf, sizeof(buf), "Best run of all levels: %d:%02d.%d", (int)(ms / 60000), (int)(ms / 1000 % 60),
             (int)(ms / 100 % 10));
    return buf;
}

/* The page's picture: the game as it stands (or the title screen), with its
 * HUD and, when it is on, the frame counter. */
static void rob_preview(void)
{
    int w, h;
    page_pic_frame(&w, &h);
    if(s_rob_level_moved && !s_paused && game_get_level() != s_rob_level) game_goto_level(s_rob_level);
    s_rob_level_moved = false;
    game_render_bands(w, h, 16, page_pic_band, NULL);
    if(game_render_bands_missed()) game_render_bands(w, h, 16, page_pic_band, NULL); /* the text it lacked */
    hud_update();
    page_pic_widgets(hud_screen_box());
    if(game_title_active()) page_pic_widgets(s_title_menu);
    if(game_fps_enabled()) page_pic_widgets(fps_overlay_label());
}

static const page_row_t k_rob_rows[] = {
    { NULL, rob_play_label, NULL, rob_play, NULL, 0 },
    { "Level", NULL, rob_level_value, rob_level, NULL, 'l' },
    { "Look", NULL, rob_look_value, rob_look, NULL, 'a' },
    { "Music", NULL, rob_music_value, rob_music, rob_has_music, 'm' },
    { "Track", NULL, rob_track_value, rob_track, rob_has_music, 'n' },
    { "Volume", NULL, rob_volume_value, rob_volume, rob_has_music, 0 },
    { "Order", NULL, rob_order_value, rob_order, rob_has_music, 'o' },
    { "Frame counter", NULL, rob_fps_value, rob_fps, NULL, 'f' },
    { "Resolution", NULL, rob_scale_value, rob_scale, rob_scale_shown, 0 },
    { "Restart at this resolution", NULL, NULL, rob_scale_restart, rob_scale_restart_shown, 0 },
};

/* ---- the other games: Play / Continue and New game, and their own lines ---- */

typedef struct {
    bool (*running)(void);
    void (*new_game)(void);
} game_fns_t;

static bool w3d_running(void) { return false; }
static void w3d_new(void) {}
static const game_fns_t k_fns[G_COUNT] = {
    { NULL, NULL },
    { tetris_view_running, tetris_view_new_game },
    { g2048_view_running, g2048_view_new_game },
    { w3d_running, w3d_new },
    { snake_view_running, snake_view_new_game },
    { breakout_view_running, breakout_view_new_game },
    { mines_view_running, mines_view_new_game },
    { w3d_running, w3d_new },
};

static const char * play_label(void) { return k_fns[s_page_game].running() ? "Continue" : "Play"; }
static void play(int dir) { (void)dir; go(k_game_mode[s_page_game]); }
static bool running(void) { return k_fns[s_page_game].running(); }
static void new_game(int dir)
{
    (void)dir;
    k_fns[s_page_game].new_game();
    go(k_game_mode[s_page_game]);
}

static const char * mines_size_value(void) { return mines_view_size_name(mines_view_size()); }
static void mines_size(int dir) { mines_view_set_size(mines_view_size() + (dir ? dir : 1)); }
static const char * mines_ng_value(void) { return mines_view_no_guess() ? "on" : "off"; }
static void mines_ng(int dir) { (void)dir; mines_view_set_no_guess(!mines_view_no_guess()); }

static const page_row_t k_game_rows[] = {
    { NULL, play_label, NULL, play, NULL, 0 },
    { "New game", NULL, NULL, new_game, running, 'n' },
};
static const page_row_t k_3d_rows[] = {
    { "Play", NULL, NULL, play, NULL, 0 },
};
static const page_row_t k_mines_rows[] = {
    { NULL, play_label, NULL, play, NULL, 0 },
    { "New game", NULL, NULL, new_game, running, 'n' },
    { "Size", NULL, mines_size_value, mines_size, NULL, 's' },
    { "No guessing", NULL, mines_ng_value, mines_ng, NULL, 'g' },
};

/* the fractals: Explore, then what to see and how */
static const char * fr_kind_value(void) { return fractal_view_kind_name(); }
static void fr_kind(int dir) { fractal_view_kind_step(dir ? dir : 1); }
static const char * fr_pal_value(void) { return fractal_view_palette_name(); }
static void fr_pal(int dir) { fractal_view_palette_step(dir ? dir : 1); }
static const char * fr_cycle_value(void) { return fractal_view_cycling() ? "on" : "off"; }
static void fr_cycle(int dir) { (void)dir; fractal_view_set_cycling(!fractal_view_cycling()); }
static const char * fr_iter_value(void) { return fractal_view_iter_text(); }
static void fr_iter(int dir) { fractal_view_iter_step(dir ? dir : 1); }
static const char * fr_res_value(void) { return fractal_view_res_text(); }
static void fr_res(int dir) { fractal_view_res_step(dir ? dir : 1); }
static void fr_reset(int dir) { (void)dir; fractal_view_reset(); }
static const page_row_t k_fractal_rows[] = {
    { "Explore", NULL, NULL, play, NULL, 0 },
    { "Fractal", NULL, fr_kind_value, fr_kind, NULL, 'k' },
    { "Colours", NULL, fr_pal_value, fr_pal, NULL, 'p' },
    { "Colour cycling", NULL, fr_cycle_value, fr_cycle, NULL, 'c' },
    { "Iterations", NULL, fr_iter_value, fr_iter, NULL, 'i' },
    { "Resolution", NULL, fr_res_value, fr_res, NULL, 'r' },
    { "Back to the start view", NULL, NULL, fr_reset, NULL, 'h' },
};
static const char * fractal_where(void) { return fractal_view_where(); }

static const char * best_of(int v)
{
    static char buf[40];
    if(v > 0) snprintf(buf, sizeof(buf), "Best score: %d", v);
    else snprintf(buf, sizeof(buf), "Best score: none yet");
    return buf;
}
static const char * tetris_best(void) { return best_of(tetris_view_best()); }
static const char * g2048_best(void) { return best_of(g2048_view_best()); }
static const char * snake_best(void) { return best_of(snake_view_best()); }
static const char * breakout_best(void) { return best_of(breakout_view_best()); }
static const char * mines_best(void)
{
    static char buf[96];
    size_t n = (size_t)snprintf(buf, sizeof(buf), "Best: ");
    for(const char * c = mines_view_best_text(); *c && n + 3 < sizeof(buf); c++) {
        if(*c == '\n') {
            buf[n++] = ' ';
            buf[n++] = ' ';
        }
        else buf[n++] = *c;
    }
    buf[n] = 0;
    return buf;
}

#define ROWS(r) r, (int)(sizeof(r) / sizeof(r[0]))
#define Y_(s) "#ffd700 " s "#"
static const page_t k_pages[G_COUNT] = {
    { "ROLLING ROB", "Roll Rob down the hills to the finish line. He jumps twice in the air; hold on for a power launch.",
      Y_("Space") "  jump\n" Y_(LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT) "  steer\n"
      "Hold a finger or the mouse: launch\n" Y_("L") "  levels    " Y_("Esc") "  this page",
      rob_best, ROWS(k_rob_rows), rob_preview },
    { "TETRIS", "Stack the falling blocks: a full row clears. More rows at once score more.",
      Y_(LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT) "  move    " Y_(LV_SYMBOL_DOWN) "  soft drop\n"
      Y_(LV_SYMBOL_UP " X") "  turn    " Y_("Z") "  turn back\n" Y_("Space") "  drop    " Y_("C") "  hold\n"
      Y_("P") "  pause    " Y_("Esc") "  this page",
      tetris_best, ROWS(k_game_rows), tetris_view_preview },
    { "2048", "Slide the tiles: two equal ones join into one. Make a 2048 tile.",
      Y_("Arrows / WASD") "  slide\n" Y_("U") "  undo    " Y_("N") "  new game\n" Y_("Esc") "  this page",
      g2048_best, ROWS(k_game_rows), g2048_view_preview },
    { "3D WORLD", "Rob in a small 3D world, drawn by the processor alone. Roll, jump, collect the coins -- and B "
                  "measures how far the CPU gets.",
      Y_("Arrows / WASD") "  roll    " Y_("Space") "  jump\n" Y_("Q E") "  turn the camera\n" Y_("R") "  resolution    "
      Y_("G") "  detail\n" Y_("T") "  textures    " Y_("B") "  benchmark\n" Y_("Esc") "  this page",
      NULL, ROWS(k_3d_rows), world3d_view_preview },
    { "SNAKE", "Eat the apples and grow longer; keep off the walls and your own tail. A golden apple is worth five.",
      Y_("Arrows / WASD") "  steer\n" Y_("P") "  pause    " Y_("N") "  new game\n" Y_("Esc") "  this page",
      snake_best, ROWS(k_game_rows), snake_view_preview },
    { "BREAKOUT", "Keep the ball in play with the paddle and break every brick. Silver bricks take two hits.",
      Y_(LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT " / A D") "  move\n" Y_("Space / " LV_SYMBOL_UP) "  launch\n"
      Y_("P") "  pause    " Y_("N") "  new game\n" Y_("Esc") "  this page",
      breakout_best, ROWS(k_game_rows), breakout_view_preview },
    { "MINESWEEPER", "Open every cell that has no mine. A number says how many of its eight neighbours are mined. "
                     "With no guessing, every board can be cleared by reasoning alone.",
      Y_("Arrows") "  move    " Y_("Space") "  open\n" Y_("F / X") "  flag\n" Y_("1 2 3") "  size    "
      Y_("N") "  new game\n" Y_("Esc") "  this page",
      mines_best, ROWS(k_mines_rows), mines_view_preview },
    { "FRACTALS", "Zoom into the Mandelbrot set, its Julia sets and the Burning Ship -- a billion times and more. "
                  "Computed on every core, coarse first and then sharper.",
      Y_("Arrows / WASD") "  move\n" Y_("Z X") "  zoom in / out\n" Y_("Space") "  zoom by itself\n"
      Y_("J") "  Julia here    " Y_("K") "  next fractal\n" Y_("C P") "  colours    " Y_("Esc") "  this page",
      fractal_where, ROWS(k_fractal_rows), fractal_view_preview }
};

/* ---------------- the menu ---------------- */

typedef struct {
    int game;            /* game_id_t, or -1: a page of the app (mode) */
    app_mode_t mode;
    int group;           /* 0 games, 1 the rest */
    const char * name;
    int key;             /* app key, shown on the tile */
    const char * what;
} item_t;

static const item_t k_items[] = {
    { G_ROB, MODE_PAGE, 0, "Rolling Rob", 'p', "Roll down the hills to the finish." },
    { G_TETRIS, MODE_PAGE, 0, "Tetris", 't', "Stack the falling blocks." },
    { G_2048, MODE_PAGE, 0, "2048", '2', "Slide and merge tiles up to 2048." },
    { G_3D, MODE_PAGE, 0, "3D world", '3', "Rob in 3D, and a CPU benchmark." },
    { G_SNAKE, MODE_PAGE, 0, "Snake", 'n', "Eat the apples and grow longer." },
    { G_BREAKOUT, MODE_PAGE, 0, "Breakout", 'b', "Bounce the ball, break the bricks." },
    { G_MINES, MODE_PAGE, 0, "Mines", 'm', "Minesweeper, without guessing." },
    { G_FRACTAL, MODE_PAGE, 1, "Fractals", 'f', "Zoom into the Mandelbrot set." },
    { -1, MODE_INFO, 1, "Info", 'i', "This device: screen, processor, memory." },
};
#define N_ITEMS ((int)(sizeof(k_items) / sizeof(k_items[0])))
#define HOME_COLS 4

static lv_obj_t * s_home, * s_home_tile[N_ITEMS], * s_play_what;
static int s_row[N_ITEMS], s_col[N_ITEMS];
static int s_hsel;

static void open_item(int i)
{
    if(k_items[i].game >= 0) open_page((game_id_t)k_items[i].game);
    else go(k_items[i].mode);
}

static void home_select(int i)
{
    s_hsel = i;
    for(int t = 0; t < N_ITEMS; t++) {
        bool sel = t == s_hsel;
        lv_obj_set_style_border_color(s_home_tile[t], sel ? GOLD : lv_color_make(0x44, 0x48, 0x44), 0);
        lv_obj_set_style_border_width(s_home_tile[t], sel ? K(2) : K(1), 0);
        lv_obj_set_style_bg_color(s_home_tile[t], sel ? lv_color_make(0x33, 0x30, 0x18) : TILE, 0);
    }
}

/* The arrows: along the row, or to the nearest tile in the row above / below. */
static void home_move(int dx, int dy)
{
    if(dx) {
        home_select((s_hsel + dx + N_ITEMS) % N_ITEMS);
        return;
    }
    int row = s_row[s_hsel] + dy, best = -1;
    for(int i = 0; i < N_ITEMS; i++)
        if(s_row[i] == row && (best < 0 || abs(s_col[i] - s_col[s_hsel]) < abs(s_col[best] - s_col[s_hsel])))
            best = i;
    if(best >= 0) home_select(best);
}

static void home_tile_cb(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    home_select(i);
    open_item(i);
}

static void to_title(void)
{
    s_paused = false; /* the title is a fresh start */
    game_title_begin();
    go(MODE_PLAY);
}

static void home_close_cb(lv_event_t * e) { (void)e; to_title(); }

static void home_update(void)
{
    char buf[64];
    if(s_paused) snprintf(buf, sizeof(buf), "Paused in level %d: %s.", (int)game_get_level() + 1,
                          game_get_level_name(game_get_level()));
    else snprintf(buf, sizeof(buf), "%s", k_items[0].what);
    lv_label_set_text(s_play_what, buf);
}

static void home_create(void)
{
    s_home = app_panel();
    app_heading(s_home, "MENU");
    app_close_button(s_home, home_close_cb);

    static const char * groups[2] = { "GAMES", "MORE" };
    const int32_t x0 = K(10), gap = K(6);
    const int32_t cw = (K(480) - 2 * x0 - (HOME_COLS - 1) * gap) / HOME_COLS, ch = K(50);
    int32_t y = K(26);
    int row = 0;
    for(int g = 0; g < 2; g++) {
        lv_obj_t * gl = lv_label_create(s_home);
        lv_label_set_text(gl, groups[g]);
        lv_obj_set_style_text_font(gl, FONT(10), 0);
        lv_obj_set_style_text_color(gl, DIM, 0);
        lv_obj_set_pos(gl, x0, y);
        y += K(13);
        int n = 0;
        for(int i = 0; i < N_ITEMS; i++) {
            if(k_items[i].group != g) continue;
            int r = n / HOME_COLS, c = n % HOME_COLS;
            s_row[i] = row + r;
            s_col[i] = c;
            n++;

            lv_obj_t * t = lv_button_create(s_home);
            s_home_tile[i] = t;
            lv_obj_set_size(t, cw, ch);
            lv_obj_set_pos(t, x0 + c * (cw + gap), y + r * (ch + gap));
            lv_obj_set_style_bg_color(t, lv_color_make(0x3A, 0x40, 0x3A), LV_STATE_PRESSED);
            lv_obj_set_style_radius(t, K(4), 0);
            lv_obj_set_style_shadow_width(t, 0, 0);
            lv_obj_set_style_pad_all(t, K(4), 0);
            lv_obj_set_scrollable(t, false);
            lv_obj_add_event_cb(t, home_tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

            /* the key, as a key cap, then the name */
            lv_obj_t * head = lv_obj_create(t);
            lv_obj_remove_style_all(head);
            lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_clickable(head, false);
            lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(head, K(5), 0);
            lv_obj_t * cap = lv_label_create(head);
            char ks[2] = { (char)(k_items[i].key >= 'a' ? k_items[i].key - 'a' + 'A' : k_items[i].key), 0 };
            lv_label_set_text(cap, ks);
            lv_obj_set_style_text_font(cap, FONT(10), 0);
            lv_obj_set_style_text_color(cap, lv_color_white(), 0);
            lv_obj_set_style_border_color(cap, lv_color_make(0x88, 0x8C, 0x88), 0);
            lv_obj_set_style_border_width(cap, K(1), 0);
            lv_obj_set_style_radius(cap, K(2), 0);
            lv_obj_set_style_pad_hor(cap, K(3), 0);
            lv_obj_t * name = lv_label_create(head);
            lv_label_set_text(name, k_items[i].name);
            lv_obj_set_style_text_font(name, FONT(12), 0);
            lv_obj_set_style_text_color(name, GOLD, 0);

            lv_obj_t * d = lv_label_create(t);
            lv_label_set_text(d, k_items[i].what);
            lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_width(d, LV_PCT(100));
            lv_obj_set_style_text_font(d, FONT(10), 0);
            lv_obj_set_style_text_color(d, lv_color_make(0xD8, 0xD8, 0xD8), 0);
            lv_obj_align(d, LV_ALIGN_TOP_LEFT, 0, K(17));
            if(k_items[i].game == G_ROB) s_play_what = d;
        }
        int rows = (n + HOME_COLS - 1) / HOME_COLS;
        row += rows;
        y += rows * (ch + gap) + K(3);
    }

    lv_obj_t * hint = lv_label_create(s_home);
    lv_label_set_text(hint, "Arrows + Enter, or the key on a tile: the game's page.   Esc: title screen");
    lv_obj_set_style_text_font(hint, FONT(10), 0);
    lv_obj_set_style_text_color(hint, DIM, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, x0, K(-6));
    home_select(0);
}

/* A key that opens a tile. */
static bool menu_shortcut(int k)
{
    for(int i = 0; i < N_ITEMS; i++)
        if(k_items[i].key == k) {
            home_select(i);
            open_item(i);
            return true;
        }
    return false;
}

/* The picker closed: with a level chosen it plays; backed out of, it goes
 * back to the page it came from. */
static void picker_closed(bool chose)
{
    bool from_page = s_picker_from_page;
    s_picker_from_page = false;
    if(chose) {
        s_paused = false;
        s_rob_level = (int)game_get_level();
    }
    else if(from_page) open_page(G_ROB);
}

/* ---------------- info ---------------- */

static lv_obj_t * s_info, * s_info_cols[2][2];

static void info_close_cb(lv_event_t * e) { (void)e; go(MODE_HOME); }

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

static cols_t s_cols;

void app_info_put(int col, const char * key, const char * fmt, ...)
{
    cols_t * t = &s_cols;
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

static void info_update(bool open)
{
    memset(&s_cols, 0, sizeof(s_cols));
    app_info_fill(open);
    for(int c = 0; c < 2; c++) {
        if(strcmp(lv_label_get_text(s_info_cols[c][0]), s_cols.k[c])) lv_label_set_text(s_info_cols[c][0], s_cols.k[c]);
        if(strcmp(lv_label_get_text(s_info_cols[c][1]), s_cols.v[c])) lv_label_set_text(s_info_cols[c][1], s_cols.v[c]);
    }
}

/* ---------------- modes ---------------- */

static void go(app_mode_t m)
{
    app_mode_t was = s_mode;
    s_mode = m;
    if(was == MODE_PLAY && m != MODE_PLAY && !game_title_active()) {
        /* left mid-level: frozen until its page continues it */
        s_paused = true;
        s_rob_level = (int)game_get_level();
    }
    if(m == MODE_PLAY) s_paused = false;
    if(m == MODE_HOME) home_update();
    lv_obj_set_hidden(s_home, m != MODE_HOME);
    if(m == MODE_PAGE) page_open(&k_pages[s_page_game]);
    else page_hide();
    tetris_view_show(m == MODE_TETRIS);
    g2048_view_show(m == MODE_2048);
    world3d_view_show(m == MODE_3D);
    snake_view_show(m == MODE_SNAKE);
    breakout_view_show(m == MODE_BREAKOUT);
    mines_view_show(m == MODE_MINES);
    fractal_view_show(m == MODE_FRACTAL);
    hud_set_hidden(m == MODE_3D || m == MODE_FRACTAL);
    lv_obj_set_hidden(s_info, m != MODE_INFO);
    if(m == MODE_INFO) {
        lv_obj_set_style_bg_opa(s_info, s_paused ? LV_OPA_COVER : LV_OPA_90, 0);
        s_fingers_max = s_fingers;
        info_update(true);
    }
}

bool app_draws_frame(void) { return s_mode == MODE_3D || s_mode == MODE_FRACTAL; }

void app_render_bands(int w, int h, void (*sink)(uint32_t * band, int stride_px, int y0, int rows, void * user),
                      void * user)
{
    if(s_mode == MODE_FRACTAL) fractal_render(w, h, sink, user);
    else world3d_render(w, h, sink, user);
}

/* Behind the info page only the title glides on; a paused level stays paused. */
bool app_game_runs(void) { return s_mode == MODE_PLAY || (s_mode == MODE_INFO && !s_paused); }
bool app_game_input(void) { return s_mode == MODE_PLAY && !hud_is_modal_open(); }
/* Rolling Rob's frame counter: on its screens only. */
bool app_fps_wanted(void) { return s_mode == MODE_PLAY && game_fps_enabled(); }

static void keys(void)
{
    for(int q = 0; q < s_keyn; q++) {
        int k = s_keyq[q];
        bool back = k == APP_KEY_ESC || k == APP_KEY_BACKSPACE;
        bool enter = k == APP_KEY_ENTER || k == APP_KEY_SPACE;
        switch(s_mode) {
            case MODE_PLAY:
                if(hud_is_modal_open()) {
                    /* the level picker */
                    if(back) hud_picker_back();
                    else if(k == APP_KEY_LEFT) hud_picker_move(-1, 0);
                    else if(k == APP_KEY_RIGHT) hud_picker_move(1, 0);
                    else if(k == APP_KEY_UP) hud_picker_move(0, -1);
                    else if(k == APP_KEY_DOWN) hud_picker_move(0, 1);
                    else if(enter) hud_picker_choose();
                    break;
                }
                /* Space, the arrows, A, M, N and F are the game's own (game.c) */
                if(game_title_active()) {
                    if(k == APP_KEY_ENTER || back) go(MODE_HOME);
                    else if(k == 'l') hud_picker_open_at(s_rob_level);
                }
                else if(back) open_page(G_ROB);
                else if(k == 'l') hud_picker_open_at((int32_t)game_get_level());
                else if(k == '3') rob3d_view_set(!rob3d_view_on()); /* the same game, drawn in 3D */
                break;
            case MODE_HOME:
                if(back) to_title();
                else if(k == APP_KEY_LEFT) home_move(-1, 0);
                else if(k == APP_KEY_RIGHT) home_move(1, 0);
                else if(k == APP_KEY_UP) home_move(0, -1);
                else if(k == APP_KEY_DOWN) home_move(0, 1);
                else if(enter) open_item(s_hsel);
                else menu_shortcut(k);
                break;
            case MODE_PAGE:
                if(back) go(MODE_HOME);
                else page_key(k);
                break;
            case MODE_DEMO:
                break;
            case MODE_VIDEO:
                break;
            case MODE_INFO:
                if(back || k == 'i') go(MODE_HOME);
                break;
            case MODE_TETRIS:
                if(k == APP_KEY_ESC) to_own_page();
                else tetris_view_key(k);
                break;
            case MODE_2048:
                if(k == APP_KEY_ESC) to_own_page();
                else g2048_view_key(k);
                break;
            case MODE_3D:
                if(k == APP_KEY_ESC) to_own_page();
                else world3d_view_key(k);
                break;
            case MODE_SNAKE:
                if(k == APP_KEY_ESC) to_own_page();
                else snake_view_key(k);
                break;
            case MODE_BREAKOUT:
                if(k == APP_KEY_ESC) to_own_page();
                else breakout_view_key(k);
                break;
            case MODE_MINES:
                if(k == APP_KEY_ESC) to_own_page();
                else mines_view_key(k);
                break;
            case MODE_FRACTAL:
                if(back) to_own_page();
                else fractal_view_key(k);
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
    if(s_mode == MODE_PAGE) page_refresh();
    if(s_mode == MODE_TETRIS) tetris_view_tick(dt_ms);
    if(s_mode == MODE_2048) g2048_view_tick(dt_ms);
    if(s_mode == MODE_3D) world3d_view_tick(dt_ms);
    if(s_mode == MODE_SNAKE) snake_view_tick(dt_ms);
    if(s_mode == MODE_BREAKOUT) breakout_view_tick(dt_ms);
    if(s_mode == MODE_MINES) mines_view_tick(dt_ms);
    if(s_mode == MODE_FRACTAL) fractal_view_tick(dt_ms);
    if(s_mode == MODE_INFO) {
        static uint32_t at;
        if(lv_tick_elaps(at) >= 500) {
            at = lv_tick_get();
            info_update(false);
        }
    }
}

/* The picker's Menu button (touch): the main menu. */
static void menu_from_picker(void)
{
    s_picker_from_page = false;
    go(MODE_HOME);
}
static void to_menu(void) { go(MODE_HOME); }

void app_touch(int slot, bool down, int32_t x, int32_t y)
{
    if(s_mode == MODE_TETRIS) tetris_view_touch(slot, down, x, y);
    if(s_mode == MODE_3D) world3d_view_touch(slot, down, x, y);
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
    /* a game's home button: its page, which is also the pause */
    tetris_view_create(lv_screen_active(), w, h);
    tetris_view_set_menu(to_own_page);
    g2048_view_create(w, h);
    g2048_view_set_menu(to_own_page);
    world3d_view_create(w, h);
    rob3d_view_init();
    world3d_view_set_menu(to_own_page);
    pix_stage_create(lv_screen_active(), w, h);
    snake_view_create(w, h);
    snake_view_set_menu(to_own_page);
    breakout_view_create(w, h);
    breakout_view_set_menu(to_own_page);
    mines_view_create(w, h);
    mines_view_set_menu(to_own_page);
    fractal_view_create(w, h);
    fractal_view_set_menu(to_own_page);
    title_menu_create();
    home_create();
    page_create(to_menu);
    info_create();
    hud_picker_set_closed_cb(picker_closed);
    lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_READY, NULL);
    s_refr_start = lv_tick_get();
}
