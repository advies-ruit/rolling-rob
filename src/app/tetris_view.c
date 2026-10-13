/*
 * The falling-blocks game in the app (rules: src/tetris).
 *
 * The well, the blocks and the hold/next boxes are drawn into an opaque
 * canvas, only when something changed; text and buttons are LVGL widgets.
 * Laid out for 480x272 and scaled like the rest (ui.h): the well in the
 * middle, hold and score on the left, the next pieces on the right, touch
 * buttons in the bottom corners. The first finger is LVGL's pointer; further
 * fingers come in through tetris_view_touch, so an arrow can be held while
 * the other thumb turns or drops.
 *
 * Keys: left/right move, down soft drop, up or X turn clockwise, Z turn the
 * other way, Space hard drop, C or Shift hold, P pause. Esc (app.c) goes to
 * the game's page in the menu, which is the pause.
 */

#include "tetris_view.h"
#include "app_page.h"
#include "app.h"
#include "tetris.h"
#include "ui.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Its own scale: the whole screen is Tetris's, so the 480x272 layout is
 * scaled to fill it -- not the HUD's scale, which a platform may cap for
 * the cost of blending it over the game (1280x720: 2.65 here). The layout
 * box is centred (s_ox, s_oy). */
static float s_sc = 1;
static int s_ox, s_oy;
#define K(n)    ((int)((float)(n) * s_sc + 0.5f))
#define FONT(n) rr_ui_font((float)(n) * s_sc)

static const lv_color_t GOLD = LV_COLOR_MAKE(0xFF, 0xD7, 0x00);

static tt_game_t s_g;
static bool s_shown, s_paused, s_started;
static lv_obj_t * s_canvas;
static lv_draw_buf_t * s_buf;
static uint32_t * s_bg; /* background with the empty boxes, copied in first */
static int s_w, s_h, s_stride;
static uint32_t s_drawn = ~0u;
static int s_flash_drawn = -1;
static char s_save[512];
static int s_best_before; /* the record when this game started */

/* layout, screen pixels */
static int s_cs, s_fx, s_fy;             /* cell size, well's top left (visible rows) */
static int s_hx, s_hy, s_nx, s_ny, s_bw, s_hh, s_nh; /* hold and next boxes */

/* ---------------- best score ---------------- */

static int best_load(void)
{
    FILE * f = fopen(s_save, "r");
    int b = 0;
    if(f) {
        if(fscanf(f, "best %d", &b) != 1) b = 0;
        fclose(f);
    }
    return b;
}

static void best_save(void)
{
    FILE * f = fopen(s_save, "w");
    if(f) {
        fprintf(f, "best %d\n", s_g.best);
        fclose(f);
    }
}

/* ---------------- drawing ---------------- */

static inline uint32_t mix(uint32_t a, uint32_t b, int t) /* t/256 of b */
{
    uint32_t r = (((a >> 16) & 255) * (256 - t) + ((b >> 16) & 255) * t) >> 8;
    uint32_t g = (((a >> 8) & 255) * (256 - t) + ((b >> 8) & 255) * t) >> 8;
    uint32_t bl = ((a & 255) * (256 - t) + (b & 255) * t) >> 8;
    return 0xFF000000u | r << 16 | g << 8 | bl;
}

static void rect(uint32_t * px, int x, int y, int w, int h, uint32_t c)
{
    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > s_w) w = s_w - x;
    if(y + h > s_h) h = s_h - y;
    for(int j = 0; j < h; j++) {
        uint32_t * row = px + (size_t)(y + j) * s_stride + x;
        for(int i = 0; i < w; i++) row[i] = c;
    }
}

/* see-through: t/256 of c over what is there */
static void rect_over(uint32_t * px, int x, int y, int w, int h, uint32_t c, int t)
{
    for(int j = 0; j < h; j++) {
        if(y + j < 0 || y + j >= s_h) continue;
        uint32_t * row = px + (size_t)(y + j) * s_stride;
        for(int i = 0; i < w; i++)
            if(x + i >= 0 && x + i < s_w) row[x + i] = mix(row[x + i], c, t);
    }
}

/* A bevelled block: light top and left, dark bottom and right. */
static void block(uint32_t * px, int x, int y, int s, uint32_t rgb)
{
    uint32_t c = 0xFF000000u | rgb;
    int b = s / 7 < 1 ? 1 : s / 7;
    rect(px, x, y, s, s, c);
    rect(px, x, y, s, b, mix(c, 0xFFFFFF, 110));
    rect(px, x, y + b, b, s - b, mix(c, 0xFFFFFF, 70));
    rect(px, x + s - b, y + b, b, s - b, mix(c, 0x000000, 90));
    rect(px, x + b, y + s - b, s - 2 * b, b, mix(c, 0x000000, 120));
    rect(px, x + b, y + b, s - 2 * b, (s - 2 * b) / 3, mix(c, 0xFFFFFF, 30));
}

static void ghost(uint32_t * px, int x, int y, int s, uint32_t rgb)
{
    int b = s / 8 < 1 ? 1 : s / 8;
    rect_over(px, x, y, s, s, rgb, 40);
    rect_over(px, x, y, s, b, rgb, 150);
    rect_over(px, x, y + s - b, s, b, rgb, 150);
    rect_over(px, x, y + b, b, s - 2 * b, rgb, 150);
    rect_over(px, x + s - b, y + b, b, s - 2 * b, rgb, 150);
}

/* A piece centred in a box (hold, next). */
static void piece_in(uint32_t * px, int piece, int bx, int by, int bw, int bh, int s, bool dim)
{
    int c[4][2], minx = 9, maxx = -1, miny = 9, maxy = -1;
    tt_cells(piece, 0, c);
    for(int i = 0; i < 4; i++) {
        if(c[i][0] < minx) minx = c[i][0];
        if(c[i][0] > maxx) maxx = c[i][0];
        if(c[i][1] < miny) miny = c[i][1];
        if(c[i][1] > maxy) maxy = c[i][1];
    }
    int pw = (maxx - minx + 1) * s, ph = (maxy - miny + 1) * s;
    uint32_t rgb = tt_color(piece);
    if(dim) rgb = mix(rgb, 0x404040, 160) & 0xFFFFFF;
    for(int i = 0; i < 4; i++)
        block(px, bx + (bw - pw) / 2 + (c[i][0] - minx) * s, by + (bh - ph) / 2 + (c[i][1] - miny) * s, s, rgb);
}

static void make_background(void)
{
    /* night gradient */
    for(int y = 0; y < s_h; y++) {
        uint32_t c = mix(0xFF141030, 0xFF2A1440, y * 256 / s_h);
        uint32_t * row = s_bg + (size_t)y * s_stride;
        for(int x = 0; x < s_w; x++) row[x] = c;
    }
    /* a few stars */
    uint32_t r = 12345;
    for(int i = 0; i < 160; i++) {
        r = r * 1103515245u + 12345u;
        int x = (int)((r >> 8) % (uint32_t)s_w);
        r = r * 1103515245u + 12345u;
        int y = (int)((r >> 8) % (uint32_t)s_h);
        int t = 60 + (int)((r >> 4) & 127);
        rect_over(s_bg, x, y, K(1) > 0 ? K(1) : 1, K(1) > 0 ? K(1) : 1, 0xFFFFFF, t);
    }
    /* the well: dark, a faint grid, a border */
    int bd = K(2);
    rect(s_bg, s_fx - bd, s_fy - bd, TT_W * s_cs + 2 * bd, TT_H * s_cs + 2 * bd, 0xFF6A6A8A);
    rect(s_bg, s_fx, s_fy, TT_W * s_cs, TT_H * s_cs, 0xFF0A0A16);
    for(int x = 1; x < TT_W; x++) rect(s_bg, s_fx + x * s_cs, s_fy, 1, TT_H * s_cs, 0xFF161628);
    for(int y = 1; y < TT_H; y++) rect(s_bg, s_fx, s_fy + y * s_cs, TT_W * s_cs, 1, 0xFF161628);
    /* hold and next */
    int bx[2] = { s_hx, s_nx }, by[2] = { s_hy, s_ny }, bh[2] = { s_hh, s_nh };
    for(int i = 0; i < 2; i++) {
        rect(s_bg, bx[i] - bd, by[i] - bd, s_bw + 2 * bd, bh[i] + 2 * bd, 0xFF6A6A8A);
        rect(s_bg, bx[i], by[i], s_bw, bh[i], 0xFF0A0A16);
    }
}

static void draw(void)
{
    uint32_t * px = (uint32_t *)s_buf->data;
    for(int y = 0; y < s_h; y++) memcpy(px + (size_t)y * s_stride, s_bg + (size_t)y * s_stride, (size_t)s_w * 4);

    /* the well */
    for(int y = TT_HIDDEN; y < TT_ROWS; y++)
        for(int x = 0; x < TT_W; x++)
            if(s_g.cell[y][x]) block(px, s_fx + x * s_cs, s_fy + (y - TT_HIDDEN) * s_cs, s_cs, tt_color(s_g.cell[y][x] - 1));
    /* rows about to go: a white flash fading out */
    if(s_g.clear_ms > 0) {
        int t = 60 + 190 * s_g.clear_ms / TT_CLEAR_MS;
        for(int y = TT_HIDDEN; y < TT_ROWS; y++)
            if(s_g.clearing[y]) rect_over(px, s_fx, s_fy + (y - TT_HIDDEN) * s_cs, TT_W * s_cs, s_cs, 0xFFFFFF, t);
    }
    if(s_g.active) {
        int c[4][2];
        tt_cells(s_g.piece, s_g.rot, c);
        int gy = tt_ghost_y(&s_g);
        for(int i = 0; i < 4; i++) {
            int row = gy + c[i][1] - TT_HIDDEN;
            if(row >= 0 && gy != s_g.y) ghost(px, s_fx + (s_g.x + c[i][0]) * s_cs, s_fy + row * s_cs, s_cs, tt_color(s_g.piece));
        }
        for(int i = 0; i < 4; i++) {
            int row = s_g.y + c[i][1] - TT_HIDDEN;
            if(row >= 0) block(px, s_fx + (s_g.x + c[i][0]) * s_cs, s_fy + row * s_cs, s_cs, tt_color(s_g.piece));
        }
    }
    /* hold, next */
    int small = s_cs * 3 / 4, label = K(13);
    if(s_g.hold >= 0) piece_in(px, s_g.hold, s_hx, s_hy + label, s_bw, s_hh - label, small, s_g.hold_used);
    int step = (s_nh - label) / TT_NEXT;
    for(int i = 0; i < TT_NEXT; i++)
        piece_in(px, s_g.next[i], s_nx, s_ny + label + i * step, s_bw, step, i == 0 ? small : small * 4 / 5, false);
    lv_obj_invalidate(s_canvas);
}

/* ---------------- widgets ---------------- */

enum { B_LEFT, B_RIGHT, B_SOFT, B_CW, B_HARD, B_HOLD, B_N };
static const tt_input_t k_btn_in[B_N] = { TT_LEFT, TT_RIGHT, TT_SOFT, TT_CW, TT_HARD, TT_HOLD };

static lv_obj_t * s_ui, * s_btn[B_N], * s_stats, * s_msg, * s_word, * s_pause_label;
static bool s_lv_down[B_N];      /* the first finger / the mouse, through LVGL */
static int s_taps[B_N];          /* presses since the last tick: a tap can be shorter than a frame */
static int s_finger_btn[16];     /* further fingers: the button under each, or -1 */
static bool s_prev[B_N];
static uint32_t s_word_until;
static uint32_t s_clear_seen;

static void btn_event(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_PRESSED) {
        s_lv_down[i] = true;
        s_taps[i]++;
    }
    if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) s_lv_down[i] = false;
}

static lv_obj_t * pad(const char * text, int x, int y, int size, int i)
{
    lv_obj_t * b = lv_button_create(s_ui);
    lv_obj_set_size(b, K(size), K(size));
    lv_obj_set_pos(b, s_ox + K(x), s_oy + K(y));
    lv_obj_set_style_bg_color(b, lv_color_make(0x30, 0x30, 0x50), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(b, lv_color_make(0x60, 0x60, 0x90), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, lv_color_make(0x8A, 0x8A, 0xB0), 0);
    lv_obj_set_style_border_width(b, K(1), 0);
    lv_obj_set_style_radius(b, K(8), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t * l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, FONT(text[0] & 0x80 ? 18 : 11), 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    if(i >= 0) {
        lv_obj_add_event_cb(b, btn_event, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, btn_event, LV_EVENT_RELEASED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, btn_event, LV_EVENT_PRESS_LOST, (void *)(intptr_t)i);
        s_btn[i] = b;
    }
    return b;
}

static lv_obj_t * box_label(const char * text, int x, int y)
{
    lv_obj_t * l = lv_label_create(s_ui);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, FONT(10), 0);
    lv_obj_set_style_text_color(l, lv_color_make(0xB0, 0xB0, 0xD0), 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void toggle_pause(void)
{
    if(s_g.over) return;
    s_paused = !s_paused;
}

static void pause_cb(lv_event_t * e) { (void)e; toggle_pause(); }

static void new_game(void)
{
    tt_new(&s_g, (uint32_t)time(NULL) ^ (uint32_t)lv_tick_get() * 2654435761u, s_g.best > 0 ? s_g.best : best_load());
    s_best_before = s_g.best;
    s_paused = false;
    s_started = true;
    s_drawn = ~0u;
}

static void msg_cb(lv_event_t * e)
{
    (void)e;
    if(s_g.over) new_game();
    else if(s_paused) s_paused = false;
}

static void (*s_menu_cb)(void);
static void menu_cb(lv_event_t * e) { (void)e; if(s_menu_cb) s_menu_cb(); }

void tetris_view_set_menu(void (*cb)(void)) { s_menu_cb = cb; }

void tetris_view_create(lv_obj_t * parent, int w, int h)
{
    s_w = w;
    s_h = h;
    app_save_path(s_save, sizeof(s_save), "tetris");
    s_canvas = lv_canvas_create(parent);
    s_buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_XRGB8888, LV_STRIDE_AUTO);
    if(s_buf == NULL) return;
    s_stride = (int)(s_buf->header.stride / 4);
    s_bg = malloc((size_t)s_stride * h * 4);
    lv_canvas_set_draw_buf(s_canvas, s_buf);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_set_clickable(s_canvas, false);
    lv_obj_set_hidden(s_canvas, true);

    /* layout in 480x272 units */
    s_sc = (float)w / 480.0f < (float)h / 272.0f ? (float)w / 480.0f : (float)h / 272.0f;
    s_ox = (w - K(480)) / 2;
    s_oy = (h - K(272)) / 2;
    s_cs = K(12);
    s_fx = (w - TT_W * s_cs) / 2;
    s_fy = (h - TT_H * s_cs) / 2;
    s_bw = K(58);
    s_hx = s_fx - K(8) - s_bw;
    s_hy = s_fy;
    s_hh = K(50);
    s_nx = s_fx + TT_W * s_cs + K(8);
    s_ny = s_fy;
    s_nh = K(170);
    if(s_bg) make_background();

    s_ui = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_ui);
    lv_obj_set_size(s_ui, LV_PCT(100), LV_PCT(100));
    lv_obj_set_clickable(s_ui, false);
    lv_obj_set_scrollable(s_ui, false);
    lv_obj_set_hidden(s_ui, true);

    box_label("HOLD", s_hx + K(4), s_hy + K(2));
    box_label("NEXT", s_nx + K(4), s_ny + K(2));

    s_stats = lv_label_create(s_ui);
    lv_label_set_recolor(s_stats, true);
    lv_obj_set_style_text_font(s_stats, FONT(11), 0);
    lv_obj_set_style_text_color(s_stats, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_stats, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_stats, s_bw + K(40));
    lv_obj_set_pos(s_stats, s_hx + s_bw - (s_bw + K(40)), s_hy + s_hh + K(10));

    /* top left: back to the menu, pause */
    lv_obj_t * m = pad(LV_SYMBOL_HOME, 6, 6, 30, -1);
    lv_obj_add_event_cb(m, menu_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * p = pad(LV_SYMBOL_PAUSE, 42, 6, 30, -1);
    lv_obj_add_event_cb(p, pause_cb, LV_EVENT_CLICKED, NULL);
    s_pause_label = lv_obj_get_child(p, 0);

    /* touch pads: move and soft drop on the left, hold / turn / drop on the right */
    pad(LV_SYMBOL_LEFT, 8, 214, 50, B_LEFT);
    pad(LV_SYMBOL_RIGHT, 64, 214, 50, B_RIGHT);
    pad(LV_SYMBOL_DOWN, 120, 214, 50, B_SOFT);
    pad("HOLD", 310, 214, 50, B_HOLD);
    pad(LV_SYMBOL_REFRESH, 366, 214, 50, B_CW);
    pad(LV_SYMBOL_DOWNLOAD, 422, 214, 50, B_HARD);

    /* a word after a clear, and the pause / game over message, over the well */
    s_word = lv_label_create(s_ui);
    lv_obj_set_style_text_font(s_word, FONT(16), 0);
    lv_obj_set_style_text_color(s_word, GOLD, 0);
    lv_obj_align(s_word, LV_ALIGN_TOP_MID, 0, s_fy + K(60));
    lv_obj_set_hidden(s_word, true);

    s_msg = lv_label_create(s_ui);
    lv_obj_set_style_text_font(s_msg, FONT(13), 0);
    lv_obj_set_style_text_color(s_msg, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(s_msg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_msg, LV_OPA_80, 0);
    lv_obj_set_style_border_color(s_msg, GOLD, 0);
    lv_obj_set_style_border_width(s_msg, K(1), 0);
    lv_obj_set_style_radius(s_msg, K(6), 0);
    lv_obj_set_style_pad_all(s_msg, K(8), 0);
    lv_obj_set_clickable(s_msg, true);
    lv_obj_add_event_cb(s_msg, msg_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(s_msg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(s_msg, true);

    for(int i = 0; i < 16; i++) s_finger_btn[i] = -1;
    tt_new(&s_g, 1, best_load());
    s_g.over = false;
}

void tetris_view_show(bool on)
{
    if(on == s_shown) return;
    s_shown = on;
    lv_obj_set_hidden(s_canvas, !on);
    lv_obj_set_hidden(s_ui, !on);
    if(on) {
        if(!s_started) new_game();
        s_paused = false; /* back from the game's page: Continue means go on */
        s_drawn = ~0u;
    }
    else {
        memset(s_lv_down, 0, sizeof(s_lv_down));
        for(int i = 0; i < 16; i++) s_finger_btn[i] = -1;
        if(s_g.best > 0) best_save();
    }
}

void tetris_view_key(int k)
{
    if(s_g.over) {
        if(k == APP_KEY_SPACE || k == APP_KEY_ENTER) new_game();
        return;
    }
    if(k == 'p') {
        toggle_pause();
        return;
    }
    if(s_paused) {
        if(k == APP_KEY_SPACE || k == APP_KEY_ENTER) s_paused = false;
        return;
    }
    /* the held ones: a tap shorter than a frame would be missed by the state */
    int held = k == APP_KEY_LEFT ? B_LEFT : k == APP_KEY_RIGHT ? B_RIGHT : k == APP_KEY_DOWN ? B_SOFT : -1;
    if(held >= 0) s_taps[held]++;
    switch(k) {
        case APP_KEY_UP:
        case 'x': tt_input(&s_g, TT_CW, true); break;
        case 'z':
        case APP_KEY_CTRL: tt_input(&s_g, TT_CCW, true); break;
        case APP_KEY_SPACE: tt_input(&s_g, TT_HARD, true); break;
        case 'c':
        case APP_KEY_SHIFT: tt_input(&s_g, TT_HOLD, true); break;
        default: break;
    }
}

void tetris_view_touch(int slot, bool down, int32_t x, int32_t y)
{
    if(!s_shown || slot < 0 || slot >= 16) return;
    if(!down) {
        s_finger_btn[slot] = -1;
        return;
    }
    int hit = -1;
    for(int i = 0; i < B_N; i++) {
        lv_area_t a;
        lv_obj_get_coords(s_btn[i], &a);
        if(x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) hit = i;
    }
    if(hit >= 0 && hit != s_finger_btn[slot]) s_taps[hit]++;
    s_finger_btn[slot] = hit; /* a finger slid off a pad lets it go */
}

static void ui_update(void)
{
    char buf[200];
    snprintf(buf, sizeof(buf),
             "#b0b0d0 SCORE#\n%d\n#b0b0d0 LEVEL#\n%d\n#b0b0d0 LINES#\n%d\n#b0b0d0 BEST#\n%d",
             s_g.score, s_g.level, s_g.lines, s_g.best);
    if(strcmp(lv_label_get_text(s_stats), buf)) lv_label_set_text(s_stats, buf);

    const char * msg = NULL;
    if(s_g.over) {
        snprintf(buf, sizeof(buf), "#ffd700 GAME OVER#\nScore %d%s\n\nTap here or press Space\nto play again", s_g.score,
                 s_g.score > s_best_before ? "   NEW BEST!" : "");
        msg = buf;
    }
    else if(s_paused) msg = "#ffd700 PAUSED#\n\nTap here or press P\nto go on";
    lv_label_set_recolor(s_msg, true);
    if(msg) {
        if(strcmp(lv_label_get_text(s_msg), msg)) lv_label_set_text(s_msg, msg);
    }
    if(lv_obj_is_hidden(s_msg) == (msg != NULL)) lv_obj_set_hidden(s_msg, msg == NULL);
    const char * pl = s_paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE;
    if(strcmp(lv_label_get_text(s_pause_label), pl)) lv_label_set_text(s_pause_label, pl);

    if(s_g.last_clear_seq != s_clear_seen) {
        s_clear_seen = s_g.last_clear_seq;
        static const char * const words[5] = { "", "SINGLE", "DOUBLE", "TRIPLE", "TETRIS!" };
        lv_label_set_text(s_word, words[s_g.last_lines]);
        lv_obj_align(s_word, LV_ALIGN_TOP_MID, 0, s_fy + K(60));
        lv_obj_set_hidden(s_word, false);
        s_word_until = lv_tick_get() + 1000;
    }
    if(!lv_obj_is_hidden(s_word) && (int32_t)(lv_tick_get() - s_word_until) > 0) lv_obj_set_hidden(s_word, true);
}

void tetris_view_tick(uint32_t dt_ms)
{
    if(!s_shown) return;
    bool key[B_N] = { app_key_held(APP_KEY_LEFT), app_key_held(APP_KEY_RIGHT), app_key_held(APP_KEY_DOWN), false, false, false };
    for(int i = 0; i < B_N; i++) {
        bool d = key[i] || s_lv_down[i];
        for(int f = 0; f < 16; f++) d = d || s_finger_btn[f] == i;
        int taps = s_taps[i];
        s_taps[i] = 0;
        if(s_paused || s_g.over) {
            d = false; /* and a held one is let go */
            taps = 0;
        }
        if(i <= B_SOFT) {
            /* held: every press counts (a move each), then the real state */
            for(int t = 0; t < taps; t++) {
                if(s_prev[i]) tt_input(&s_g, k_btn_in[i], false);
                tt_input(&s_g, k_btn_in[i], true);
                s_prev[i] = true;
            }
            if(d != s_prev[i]) tt_input(&s_g, k_btn_in[i], d);
        }
        else {
            for(int t = 0; t < taps; t++) tt_input(&s_g, k_btn_in[i], true);
        }
        s_prev[i] = d;
    }
    bool was_over = s_g.over;
    if(!s_paused) tt_update(&s_g, (int)dt_ms);
    if(s_g.over && !was_over) best_save();

    int flash = s_g.clear_ms > 0 ? s_g.clear_ms / 40 : -1;
    if(s_g.changed != s_drawn || flash != s_flash_drawn) {
        s_drawn = s_g.changed;
        s_flash_drawn = flash;
        draw();
    }
    ui_update();
}

bool tetris_view_running(void) { return s_started && !s_g.over; }
void tetris_view_new_game(void) { new_game(); }
int tetris_view_best(void) { return s_started ? s_g.best : best_load(); }

void tetris_view_preview(void)
{
    bool started = s_started;
    if(!started) new_game();
    s_started = started; /* a game shown is not one played: the page keeps "Play" */
    if(s_buf) {
        draw();
        page_pic_pixels((const uint32_t *)s_buf->data, s_stride);
    }
    ui_update();
    page_pic_widgets(s_ui);
}
