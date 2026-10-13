/*
 * Breakout in the app (rules: src/breakout).
 *
 * The field in the middle of the shared canvas (pix.h), score and balls on
 * the left, the keys on the right. Each frame redraws only the rectangles
 * the ball and the paddle left and entered, and the bricks that changed:
 * a few dozen rows, not the screen. Left / right or A / D move (held), a
 * finger drags the paddle; Space, up or a tap launches; P pauses; N starts
 * again. Esc (app.c) or the home button goes to the game's page in the
 * menu: the pause.
 *
 * Laid out for 480x272 and scaled to fill the screen, like Tetris.
 */

#include "breakout_view.h"
#include "app_page.h"
#include "breakout.h"
#include "app.h"
#include "game_ui.h"
#include "pix.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static float s_sc = 1;
static int s_ox, s_oy;
#define K(n)    ((int)((float)(n) * s_sc + 0.5f))
#define X(n)    (s_ox + K(n))
#define Y(n)    (s_oy + K(n))

typedef struct { int x, y, w, h; } rc_t;

static bo_game_t s_g;
static bool s_shown, s_paused, s_started, s_launch;
static char s_save[512];
static int s_best_before;
static float s_touch_x = -1;
static int s_fx, s_fy, s_fw, s_fh;           /* the field on screen */
static uint8_t s_brick_drawn[BO_ROWS][BO_COLS];
static uint32_t s_bricks_seen = ~0u;
static rc_t s_ball_drawn, s_paddle_drawn;
static lv_obj_t * s_ui, * s_stats, * s_msg, * s_hint, * s_pause_label;

static const uint32_t k_row_rgb[BO_ROWS] = { 0xE53935, 0xFB8C00, 0xFDD835, 0x43A047, 0x00ACC1, 0x1E88E5, 0x8E24AA };

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

/* ---------------- drawing ---------------- */

static int sx(float u) { return s_fx + (int)lroundf(u * s_sc); }
static int sy(float u) { return s_fy + (int)lroundf(u * s_sc); }

static rc_t brick_rect(int r, int c)
{
    int x0 = sx((float)c * BO_BRICK_W), x1 = sx((float)(c + 1) * BO_BRICK_W);
    int y0 = sy(BO_BRICK_TOP + (float)r * BO_BRICK_H), y1 = sy(BO_BRICK_TOP + (float)(r + 1) * BO_BRICK_H);
    rc_t rc = { x0, y0, x1 - x0, y1 - y0 };
    return rc;
}

static rc_t ball_rect(void)
{
    float r = BO_BALL_R * s_sc + 1;
    float cx = (float)s_fx + s_g.bx * s_sc, cy = (float)s_fy + s_g.by * s_sc;
    rc_t rc = { (int)floorf(cx - r), (int)floorf(cy - r), 0, 0 };
    rc.w = (int)ceilf(cx + r) - rc.x;
    rc.h = (int)ceilf(cy + r) - rc.y;
    return rc;
}

static rc_t paddle_rect(void)
{
    rc_t rc = { sx(s_g.px - s_g.pw / 2), sy(BO_PADDLE_Y), 0, 0 };
    rc.w = sx(s_g.px + s_g.pw / 2) - rc.x;
    rc.h = sy(BO_PADDLE_Y + BO_PADDLE_H) - rc.y;
    return rc;
}

static bool overlap(rc_t a, rc_t b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static rc_t join(rc_t a, rc_t b)
{
    if(a.w <= 0) return b;
    if(b.w <= 0) return a;
    int x2 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w, y2 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    rc_t r = { a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, 0, 0 };
    r.w = x2 - r.x;
    r.h = y2 - r.y;
    return r;
}

static void make_background(void)
{
    pix_t b = *pix_stage();
    b.px = pix_stage_bg();
    pix_noclip(&b);
    pix_bg_gradient(0x101830, 0x1C1030);
    int bd = K(2);
    pix_rect(&b, s_fx - bd, s_fy - bd, s_fw + 2 * bd, s_fh + bd, 0x5A6A9A);
    pix_rect(&b, s_fx, s_fy, s_fw, s_fh, 0x0A0E1C);
    /* a faint grid */
    for(int i = 1; i < 8; i++) pix_rect(&b, s_fx + s_fw * i / 8, s_fy, 1, s_fh, 0x10162A);
    for(int i = 1; i < 8; i++) pix_rect(&b, s_fx, s_fy + s_fh * i / 8, s_fw, 1, 0x10162A);
}

static void draw_brick(pix_t * p, int r, int c)
{
    int v = s_g.brick[r][c];
    if(!v) return;
    rc_t b = brick_rect(r, c);
    int g = K(1) > 0 ? K(1) : 1;
    uint32_t rgb = s_g.silver[r][c] ? (v >= 2 ? 0xC0C4CC : 0x80848C) : k_row_rgb[r];
    pix_block(p, b.x + g / 2, b.y + g / 2, b.w - g, b.h - g, rgb);
}

/* Everything inside `r` (and the field) again, and that rectangle to the screen. */
static void redraw(rc_t r)
{
    pix_t * p = pix_stage();
    rc_t f = { s_fx, s_fy, s_fw, s_fh };
    if(!overlap(r, f)) return;
    int x2 = r.x + r.w < f.x + f.w ? r.x + r.w : f.x + f.w, y2 = r.y + r.h < f.y + f.h ? r.y + r.h : f.y + f.h;
    r.x = r.x > f.x ? r.x : f.x;
    r.y = r.y > f.y ? r.y : f.y;
    r.w = x2 - r.x;
    r.h = y2 - r.y;
    pix_clip(p, r.x, r.y, r.w, r.h);
    pix_restore(p, r.x, r.y, r.w, r.h);
    for(int row = 0; row < BO_ROWS; row++)
        for(int c = 0; c < BO_COLS; c++)
            if(s_g.brick[row][c] && overlap(r, brick_rect(row, c))) draw_brick(p, row, c);
    rc_t pr = paddle_rect();
    if(overlap(r, pr)) {
        pix_block(p, pr.x, pr.y, pr.w, pr.h, 0xFFC107);
        pix_rect(p, pr.x + pr.w / 2 - K(4), pr.y + pr.h / 3, K(8), pr.h / 3 > 0 ? pr.h / 3 : 1, 0xFFE082);
    }
    if(!s_g.over) {
        rc_t br = ball_rect();
        if(overlap(r, br))
            pix_disc(p, (float)s_fx + s_g.bx * s_sc, (float)s_fy + s_g.by * s_sc, BO_BALL_R * s_sc, 0xFFFFFF);
    }
    pix_noclip(p);
    pix_flush(r.x, r.y, r.w, r.h);
}

static void draw(bool full)
{
    pix_t * p = pix_stage();
    if(!p) return;
    if(full) {
        pix_noclip(p);
        pix_restore(p, 0, 0, p->w, p->h);
        pix_flush_all();
        rc_t f = { s_fx, s_fy, s_fw, s_fh };
        redraw(f);
        for(int r = 0; r < BO_ROWS; r++)
            for(int c = 0; c < BO_COLS; c++) s_brick_drawn[r][c] = (uint8_t)(s_g.brick[r][c] + 1);
        s_bricks_seen = s_g.bricks_changed;
        s_ball_drawn = ball_rect();
        s_paddle_drawn = paddle_rect();
        return;
    }
    if(s_g.bricks_changed != s_bricks_seen) {
        s_bricks_seen = s_g.bricks_changed;
        for(int r = 0; r < BO_ROWS; r++)
            for(int c = 0; c < BO_COLS; c++)
                if(s_brick_drawn[r][c] != s_g.brick[r][c] + 1) {
                    s_brick_drawn[r][c] = (uint8_t)(s_g.brick[r][c] + 1);
                    redraw(brick_rect(r, c));
                }
    }
    rc_t b = s_g.over ? (rc_t){ 0, 0, 0, 0 } : ball_rect();
    if(memcmp(&b, &s_ball_drawn, sizeof(b))) {
        redraw(join(s_ball_drawn, b));
        s_ball_drawn = b;
    }
    rc_t pr = paddle_rect();
    if(memcmp(&pr, &s_paddle_drawn, sizeof(pr))) {
        redraw(join(s_paddle_drawn, pr));
        s_paddle_drawn = pr;
    }
}

/* ---------------- the game ---------------- */

static void new_game(void)
{
    best_save();
    bo_new(&s_g, (uint32_t)time(NULL) ^ lv_tick_get() * 2654435761u, s_g.best > 0 ? s_g.best : best_load());
    s_best_before = s_g.best;
    s_paused = false;
    s_started = true;
    if(s_shown && pix_stage()) draw(true);
}

static void toggle_pause(void)
{
    if(!s_g.over) s_paused = !s_paused;
}

static void (*s_menu_cb)(void);
static void menu_cb(lv_event_t * e) { (void)e; if(s_menu_cb) s_menu_cb(); }
static void pause_cb(lv_event_t * e) { (void)e; toggle_pause(); }
static void msg_cb(lv_event_t * e)
{
    (void)e;
    if(s_g.over) new_game();
    else s_paused = false;
}

void breakout_view_set_menu(void (*cb)(void)) { s_menu_cb = cb; }

/* A finger drags the paddle; a tap launches. */
static void input(lv_event_code_t code, int x, int y)
{
    (void)y;
    if(s_paused || s_g.over) return;
    if(code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) s_touch_x = (float)(x - s_fx) / s_sc;
    else if(code == LV_EVENT_RELEASED) s_touch_x = -1;
    if(code == LV_EVENT_SHORT_CLICKED) s_launch = true;
}

void breakout_view_create(int w, int h)
{
    s_sc = (float)w / 480.0f < (float)h / 272.0f ? (float)w / 480.0f : (float)h / 272.0f;
    s_ox = (w - K(480)) / 2;
    s_oy = (h - K(272)) / 2;
    app_save_path(s_save, sizeof(s_save), "breakout");
    s_fw = K(BO_FW);
    s_fh = K(BO_FH);
    s_fx = (w - s_fw) / 2;
    s_fy = Y(8);

    s_ui = gu_layer();
    gu_button(s_ui, LV_SYMBOL_HOME, X(8), Y(8), K(30), K(22), K(12), menu_cb, NULL);
    lv_obj_t * p = gu_button(s_ui, LV_SYMBOL_PAUSE, X(44), Y(8), K(30), K(22), K(12), pause_cb, NULL);
    s_pause_label = lv_obj_get_child(p, 0);
    lv_obj_t * t = gu_label(s_ui, "BREAKOUT", K(11), lv_color_hex(0xFFD700));
    lv_obj_set_pos(t, X(8), Y(40));
    s_stats = gu_label(s_ui, "", K(12), lv_color_white());
    lv_obj_set_pos(s_stats, X(8), Y(62));
    lv_obj_t * help = gu_label(s_ui,
                               "#ffd700 " LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT "# or A D\nmove\n\n"
                               "#ffd700 Space# or tap\nlaunch\n\n#ffd700 P# pause\n#ffd700 N# new game\n#ffd700 Esc# menu",
                               K(10), lv_color_hex(0xB0B8D0));
    lv_obj_set_pos(help, X(408), Y(40));
    s_hint = gu_label(s_ui, "", K(12), lv_color_white());
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_hidden(s_hint, true);
    s_msg = gu_message(s_ui, K(13), K(1), msg_cb);

    bo_new(&s_g, 1, 0);
}

void breakout_view_show(bool on)
{
    if(on == s_shown) return;
    s_shown = on;
    lv_obj_set_hidden(s_ui, !on);
    pix_stage_show(on);
    s_touch_x = -1;
    if(on) {
        pix_stage_set_input(input);
        if(!s_started) {
            s_g.best = best_load();
            new_game();
        }
        s_paused = false; /* back from the game's page: Continue means go on */
        if(pix_stage()) {
            make_background();
            draw(true);
        }
    }
    else best_save();
}

void breakout_view_key(int k)
{
    bool go = k == APP_KEY_SPACE || k == APP_KEY_ENTER;
    if(s_g.over) {
        if(go || k == 'n') new_game();
        return;
    }
    if(k == 'p') toggle_pause();
    else if(k == 'n') new_game();
    else if(s_paused) {
        if(go) s_paused = false;
    }
    else if(go || k == APP_KEY_UP || k == 'w') s_launch = true;
}

static void ui_update(void)
{
    char buf[200], balls[16] = "";
    for(int i = 0; i < s_g.lives && i < 9; i++) strcat(balls, i ? " o" : "o");
    snprintf(buf, sizeof(buf), "#b0b8d0 SCORE#\n%d\n#b0b8d0 BEST#\n%d\n#b0b8d0 LEVEL#\n%d\n#b0b8d0 BALLS#\n%s",
             s_g.score, s_g.best, s_g.level, s_g.lives > 0 ? balls : "-");
    gu_set_text(s_stats, buf);

    const char * msg = NULL;
    if(s_g.over) {
        snprintf(buf, sizeof(buf), "#ffd700 GAME OVER#\nScore %d%s\n\nSpace or tap here: play again", s_g.score,
                 s_g.score > s_best_before ? "   NEW BEST!" : "");
        msg = buf;
    }
    else if(s_paused) msg = "#ffd700 PAUSED#\n\nSpace, P or tap here: go on";
    gu_message_set(s_msg, msg);

    bool hint = s_g.stuck && !s_g.over && !s_paused;
    if(hint) {
        snprintf(buf, sizeof(buf), "#ffd700 LEVEL %d#\nSpace or tap: launch", s_g.level);
        gu_set_text(s_hint, buf);
        lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, sy(150));
    }
    if(lv_obj_is_hidden(s_hint) == hint) lv_obj_set_hidden(s_hint, !hint);
    gu_set_text(s_pause_label, s_paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
}

void breakout_view_tick(uint32_t dt_ms)
{
    if(!s_shown) return;
    int move = (app_key_held(APP_KEY_RIGHT) || app_key_held('d')) - (app_key_held(APP_KEY_LEFT) || app_key_held('a'));
    bool was_over = s_g.over;
    if(!s_paused) bo_update(&s_g, (int)dt_ms, move, s_touch_x, s_launch);
    s_launch = false;
    if(s_g.over && !was_over) best_save();
    if(pix_stage()) draw(false);
    ui_update();
}

bool breakout_view_running(void) { return s_started && !s_g.over && (s_g.score > 0 || !s_g.stuck || s_g.lives < 3); }
void breakout_view_new_game(void) { new_game(); }
int breakout_view_best(void) { return s_started ? s_g.best : best_load(); }

void breakout_view_preview(void)
{
    if(!s_started) {
        s_g.best = best_load();
        new_game();
    }
    pix_t * p = pix_stage();
    if(p) {
        make_background();
        draw(true);
        page_pic_pixels(p->px, p->stride);
    }
    ui_update();
    page_pic_widgets(s_ui);
}
