/*
 * 2048 in the app (rules: src/g2048).
 *
 * A page of its own in the original's colours: the grid in the middle,
 * title, score and best on the left, New game and Undo on the right. Tiles
 * are LVGL objects from a pool of 16: a move slides them (90 ms), then the
 * grid is laid out again and merged tiles pop, the new one grows in
 * (130 ms). A move during the animation finishes it first. Swipe anywhere,
 * or arrows / WASD; N new game, U or Backspace undo; Esc (app.c) or the
 * home button goes back to the menu.
 *
 * Laid out for 480x272 and scaled to fill the screen, like Tetris.
 */

#include "g2048_view.h"
#include "app_page.h"
#include "g2048.h"
#include "app.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static float s_sc = 1;
static int s_ox, s_oy;
#define K(n)    ((int)((float)(n) * s_sc + 0.5f))
#define FONT(n) rr_ui_font((float)(n) * s_sc)
#define X(n)    (s_ox + K(n))
#define Y(n)    (s_oy + K(n))

#define C_PAGE  lv_color_hex(0xFAF8EF)
#define C_TEXT  lv_color_hex(0x776E65)
#define C_BOARD lv_color_hex(0xBBADA0)
#define C_EMPTY lv_color_hex(0xCDC1B4)
#define C_BTN   lv_color_hex(0x8F7A66)
#define C_LIGHT lv_color_hex(0xF9F6F2)

#define SLIDE_MS 90
#define POP_MS   130

static g2_game_t s_g;
static bool s_shown, s_started, s_win_seen;
static char s_save[512];
static int s_best_before;

static lv_obj_t * s_page, * s_board, * s_tile[16], * s_tile_label[16];
static lv_obj_t * s_score, * s_best, * s_plus, * s_undo;
static lv_obj_t * s_msg, * s_msg_label, * s_msg_btn[2];
static int s_cell, s_gap;
static uint32_t s_shown_moves = ~0u;

/* animation */
enum { IDLE, SLIDE, POP };
static int s_phase = IDLE, s_t;
typedef struct { lv_obj_t * obj; int r, c; bool grow; } pop_t;
static pop_t s_pop[17];
static int s_pops;
static uint32_t s_plus_until;

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
    if(s_g.best <= 0) return;
    FILE * f = fopen(s_save, "w");
    if(f) {
        fprintf(f, "best %d\n", s_g.best);
        fclose(f);
    }
}

/* ---------------- tiles ---------------- */

static lv_color_t tile_bg(int e)
{
    static const uint32_t c[] = { 0xCDC1B4, 0xEEE4DA, 0xEDE0C8, 0xF2B179, 0xF59563, 0xF67C5F, 0xF65E3B,
                                  0xEDCF72, 0xEDCC61, 0xEDC850, 0xEDC53F, 0xEDC22E };
    return lv_color_hex(e < (int)(sizeof(c) / sizeof(c[0])) ? c[e] : 0x3C3A32);
}

static int cell_x(int c) { return s_gap + c * (s_cell + s_gap); }

static void tile_set(int i, int e)
{
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", 1u << e);
    size_t n = strlen(buf);
    lv_obj_set_style_bg_color(s_tile[i], tile_bg(e), 0);
    lv_obj_t * l = s_tile_label[i];
    if(strcmp(lv_label_get_text(l), buf)) lv_label_set_text(l, buf);
    lv_obj_set_style_text_font(l, FONT(n <= 2 ? 26 : n == 3 ? 22 : n == 4 ? 18 : 14), 0);
    lv_obj_set_style_text_color(l, e <= 2 ? C_TEXT : C_LIGHT, 0);
    lv_obj_set_hidden(l, false);
}

/* Size `s` (1 = a cell) around the cell's centre. */
static void tile_place(lv_obj_t * o, float x, float y, float s)
{
    int w = (int)((float)s_cell * s + 0.5f);
    lv_obj_set_size(o, w, w);
    lv_obj_set_pos(o, (int)(x + ((float)s_cell - (float)w) / 2), (int)(y + ((float)s_cell - (float)w) / 2));
}

/* The pool shows the grid as it is; `pop`: merged tiles and the new one get
 * the second half of the animation. */
static void layout(bool pop)
{
    int i = 0;
    s_pops = 0;
    for(int r = 0; r < G2_N; r++)
        for(int c = 0; c < G2_N; c++) {
            int e = s_g.cell[r][c];
            if(!e) continue;
            tile_set(i, e);
            tile_place(s_tile[i], (float)cell_x(c), (float)cell_x(r), 1);
            lv_obj_set_hidden(s_tile[i], false);
            bool grow = r == s_g.new_r && c == s_g.new_c;
            if(pop && (grow || s_g.merged[r][c])) s_pop[s_pops++] = (pop_t){ s_tile[i], r, c, grow };
            if(pop && grow) tile_place(s_tile[i], (float)cell_x(c), (float)cell_x(r), 0.2f);
            if(pop && grow) lv_obj_set_hidden(s_tile_label[i], true);
            i++;
        }
    for(; i < 16; i++) lv_obj_set_hidden(s_tile[i], true);
    s_phase = pop && s_pops ? POP : IDLE;
    s_t = 0;
}

static float ease_out(float f) { return 1 - (1 - f) * (1 - f); }

static void animate(uint32_t dt_ms)
{
    if(s_phase == IDLE) return;
    s_t += (int)dt_ms;
    if(s_phase == SLIDE) {
        float f = s_t >= SLIDE_MS ? 1 : ease_out((float)s_t / SLIDE_MS);
        for(int i = 0; i < s_g.slides; i++) {
            const g2_slide_t * sl = &s_g.slide[i];
            float x = (float)cell_x(sl->from_c) + (float)(cell_x(sl->to_c) - cell_x(sl->from_c)) * f;
            float y = (float)cell_x(sl->from_r) + (float)(cell_x(sl->to_r) - cell_x(sl->from_r)) * f;
            tile_place(s_tile[i], x, y, 1);
        }
        if(s_t >= SLIDE_MS) layout(true);
        return;
    }
    float f = s_t >= POP_MS ? 1 : (float)s_t / POP_MS;
    for(int i = 0; i < s_pops; i++) {
        float s = s_pop[i].grow ? 0.2f + 0.8f * ease_out(f) : 1 + 0.14f * sinf(3.14159f * f);
        tile_place(s_pop[i].obj, (float)cell_x(s_pop[i].c), (float)cell_x(s_pop[i].r), s);
        if(s_pop[i].grow && s > 0.6f) lv_obj_set_hidden(lv_obj_get_child(s_pop[i].obj, 0), false);
    }
    if(s_t >= POP_MS) s_phase = IDLE;
}

static void finish_animation(void)
{
    if(s_phase != IDLE) layout(false);
}

/* ---------------- the game ---------------- */

static void ui_update(void);

static void do_move(g2_dir_t d)
{
    if(!lv_obj_is_hidden(s_msg)) return;
    finish_animation();
    bool was_over = s_g.over;
    if(!g2_move(&s_g, d)) return;
    /* the pool takes the tiles where they were, then slides them */
    for(int i = 0; i < 16; i++) lv_obj_set_hidden(s_tile[i], i >= s_g.slides);
    for(int i = 0; i < s_g.slides; i++) {
        tile_set(i, s_g.slide[i].exp);
        tile_place(s_tile[i], (float)cell_x(s_g.slide[i].from_c), (float)cell_x(s_g.slide[i].from_r), 1);
    }
    s_phase = SLIDE;
    s_t = 0;
    if(s_g.gained > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "+%d", s_g.gained);
        lv_label_set_text(s_plus, buf);
        lv_obj_set_hidden(s_plus, false);
        s_plus_until = lv_tick_get() + 600;
    }
    if(s_g.over && !was_over) best_save();
}

static void new_game(void)
{
    best_save();
    finish_animation();
    g2_new(&s_g, (uint32_t)time(NULL) ^ lv_tick_get() * 2654435761u, s_g.best > 0 ? s_g.best : best_load());
    s_best_before = s_g.best;
    s_win_seen = false;
    s_started = true;
    lv_obj_set_hidden(s_msg, true);
    layout(false);
}

static void undo(void)
{
    finish_animation();
    if(!g2_undo(&s_g)) return;
    lv_obj_set_hidden(s_msg, true);
    layout(false);
}

/* ---------------- widgets ---------------- */

static void (*s_menu_cb)(void);
static void menu_cb(lv_event_t * e) { (void)e; if(s_menu_cb) s_menu_cb(); }
static void new_cb(lv_event_t * e) { (void)e; new_game(); }
static void undo_cb(lv_event_t * e) { (void)e; undo(); }

void g2048_view_set_menu(void (*cb)(void)) { s_menu_cb = cb; }

/* The message's buttons: [0] new game / keep going, [1] undo. */
static void msg_btn_cb(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i == 1) undo();
    else if(s_g.over) new_game();
    else lv_obj_set_hidden(s_msg, true); /* keep going after 2048 */
}

/* Swipes: anywhere on the page, decided as soon as the finger has gone far
 * enough, once per touch. */
static lv_point_t s_press;
static bool s_swipe_armed;

static void page_cb(lv_event_t * e)
{
    lv_indev_t * in = lv_indev_active();
    if(in == NULL) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_PRESSED) {
        s_press = p;
        s_swipe_armed = true;
        return;
    }
    if(code != LV_EVENT_PRESSING || !s_swipe_armed) return;
    int dx = p.x - s_press.x, dy = p.y - s_press.y;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if((adx > ady ? adx : ady) < K(22)) return;
    s_swipe_armed = false;
    do_move(adx > ady ? (dx < 0 ? G2_LEFT : G2_RIGHT) : (dy < 0 ? G2_UP : G2_DOWN));
}

static lv_obj_t * box(lv_obj_t * parent, int x, int y, int w, int h, lv_color_t c, int radius)
{
    lv_obj_t * o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t * label(lv_obj_t * parent, const char * text, int size, lv_color_t c)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, FONT(size), 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

static lv_obj_t * button(lv_obj_t * parent, const char * text, int x, int y, int w, int h, lv_event_cb_t cb, void * user)
{
    lv_obj_t * b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, C_BTN, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x6F5D4D), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, K(4), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t * l = label(b, text, 12, C_LIGHT);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

/* SCORE / BEST: a small heading over the number. */
static lv_obj_t * score_box(int x, int y, const char * title)
{
    lv_obj_t * b = box(s_page, X(x), Y(y), K(96), K(40), C_BOARD, K(4));
    lv_obj_t * t = label(b, title, 10, lv_color_hex(0xEEE4DA));
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, K(3));
    lv_obj_t * v = label(b, "0", 16, lv_color_white());
    lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0, K(-3));
    return v;
}

void g2048_view_create(int w, int h)
{
    s_sc = (float)w / 480.0f < (float)h / 272.0f ? (float)w / 480.0f : (float)h / 272.0f;
    s_ox = (w - K(480)) / 2;
    s_oy = (h - K(272)) / 2;
    app_save_path(s_save, sizeof(s_save), "2048");

    s_page = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, w, h);
    lv_obj_set_style_bg_color(s_page, C_PAGE, 0);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_page, false);
    lv_obj_set_clickable(s_page, true);
    lv_obj_add_event_cb(s_page, page_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_page, page_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_set_hidden(s_page, true);

    /* left: home, title, score, best */
    lv_obj_t * home = button(s_page, LV_SYMBOL_HOME, X(10), Y(8), K(30), K(30), menu_cb, NULL);
    lv_obj_set_style_text_font(lv_obj_get_child(home, 0), FONT(14), 0);
    lv_obj_t * t = label(s_page, "2048", 34, C_TEXT);
    lv_obj_set_pos(t, X(10), Y(44));
    s_score = score_box(10, 98, "SCORE");
    s_best = score_box(10, 146, "BEST");
    s_plus = label(s_page, "", 14, C_TEXT);
    lv_obj_set_pos(s_plus, X(70), Y(84));
    lv_obj_set_hidden(s_plus, true);

    /* the grid */
    s_cell = K(52);
    s_gap = K(7);
    int bs = 4 * s_cell + 5 * s_gap;
    s_board = box(s_page, X(122), s_oy + (K(272) - bs) / 2, bs, bs, C_BOARD, K(6));
    for(int r = 0; r < G2_N; r++)
        for(int c = 0; c < G2_N; c++) box(s_board, cell_x(c), cell_x(r), s_cell, s_cell, C_EMPTY, K(4));
    for(int i = 0; i < 16; i++) {
        s_tile[i] = box(s_board, 0, 0, s_cell, s_cell, tile_bg(1), K(4));
        s_tile_label[i] = label(s_tile[i], "", 26, C_TEXT);
        lv_obj_center(s_tile_label[i]);
        lv_obj_set_hidden(s_tile[i], true);
    }

    /* right: new game, undo, how to play */
    button(s_page, "New game", X(378), Y(98), K(92), K(32), new_cb, NULL);
    s_undo = button(s_page, LV_SYMBOL_LEFT "  Undo", X(378), Y(138), K(92), K(32), undo_cb, NULL);
    lv_obj_t * help = label(s_page, "Swipe or use the\narrows. Equal tiles\njoin: make 2048!", 10, C_TEXT);
    lv_obj_set_pos(help, X(378), Y(180));

    /* win / game over, over the grid */
    s_msg = box(s_board, 0, 0, bs, bs, lv_color_hex(0xEEE4DA), K(6));
    lv_obj_set_style_bg_opa(s_msg, LV_OPA_80, 0);
    lv_obj_set_clickable(s_msg, true); /* swipes do not reach the grid under it */
    s_msg_label = label(s_msg, "", 24, C_TEXT);
    lv_obj_set_style_text_align(s_msg_label, LV_TEXT_ALIGN_CENTER, 0);
    for(int i = 0; i < 2; i++) {
        s_msg_btn[i] = button(s_msg, "", 0, 0, K(92), K(30), msg_btn_cb, (void *)(intptr_t)i);
        lv_obj_align(s_msg_btn[i], LV_ALIGN_CENTER, i ? K(50) : K(-50), K(30));
    }
    lv_obj_set_hidden(s_msg, true);
}

static void show_message(const char * text, const char * b0, const char * b1)
{
    lv_label_set_text(s_msg_label, text);
    lv_obj_align(s_msg_label, LV_ALIGN_CENTER, 0, strchr(text, '\n') ? K(-38) : K(-26));
    lv_label_set_text(lv_obj_get_child(s_msg_btn[0], 0), b0);
    lv_label_set_text(lv_obj_get_child(s_msg_btn[1], 0), b1);
    lv_obj_set_hidden(s_msg, false);
}

static void ui_update(void)
{
    if(s_g.moves != s_shown_moves) {
        s_shown_moves = s_g.moves;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", s_g.score);
        lv_label_set_text(s_score, buf);
        snprintf(buf, sizeof(buf), "%d", s_g.best);
        lv_label_set_text(s_best, buf);
        lv_obj_set_state(s_undo, LV_STATE_DISABLED, !s_g.can_undo);
    }
    if(!lv_obj_is_hidden(s_plus) && (int32_t)(lv_tick_get() - s_plus_until) > 0) lv_obj_set_hidden(s_plus, true);
    /* the messages, once the tiles have stopped */
    if(s_phase != IDLE || !lv_obj_is_hidden(s_msg)) return;
    if(s_g.over) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Game over!%s", s_g.score > s_best_before ? "\nNew best!" : "");
        show_message(buf, "New game", LV_SYMBOL_LEFT "  Undo");
    }
    else if(s_g.won && !s_win_seen) {
        s_win_seen = true;
        show_message("You made 2048!", "Keep going", LV_SYMBOL_LEFT "  Undo");
    }
}

void g2048_view_show(bool on)
{
    if(on == s_shown) return;
    s_shown = on;
    lv_obj_set_hidden(s_page, !on);
    if(on && !s_started) {
        s_g.best = best_load();
        new_game();
    }
    if(!on) {
        finish_animation();
        best_save();
    }
}

void g2048_view_key(int k)
{
    if(!lv_obj_is_hidden(s_msg)) {
        if(k == 'u' || k == APP_KEY_BACKSPACE) undo();
        else if(k == 'n' || k == APP_KEY_ENTER || k == APP_KEY_SPACE) {
            if(s_g.over || k == 'n') new_game();
            else lv_obj_set_hidden(s_msg, true);
        }
        return;
    }
    switch(k) {
        case APP_KEY_LEFT: case 'a': do_move(G2_LEFT); break;
        case APP_KEY_RIGHT: case 'd': do_move(G2_RIGHT); break;
        case APP_KEY_UP: case 'w': do_move(G2_UP); break;
        case APP_KEY_DOWN: case 's': do_move(G2_DOWN); break;
        case 'n': new_game(); break;
        case 'u': case APP_KEY_BACKSPACE: undo(); break;
        default: break;
    }
}

void g2048_view_tick(uint32_t dt_ms)
{
    if(!s_shown) return;
    animate(dt_ms);
    ui_update();
}

bool g2048_view_running(void) { return s_started && !s_g.over && s_g.score > 0; }
void g2048_view_new_game(void) { new_game(); }
int g2048_view_best(void) { return s_started ? s_g.best : best_load(); }

void g2048_view_preview(void)
{
    if(!s_started) {
        s_g.best = best_load();
        new_game();
    }
    finish_animation();
    ui_update();
    page_pic_fill(0x000000);
    page_pic_widgets(s_page);
}
