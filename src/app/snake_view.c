/*
 * Snake in the app (rules: src/snake).
 *
 * The field is a dark chequerboard on the shared canvas (pix.h); a cell is
 * redrawn only when what is on it changed, so a step costs three or four
 * cells. Score and help sit in a bar along the top. Arrows or WASD steer,
 * a swipe too; P pauses; Space or a tap on the message starts again. Esc
 * (app.c) or the home button goes to the game's page in the menu: the pause.
 *
 * Laid out for 480x272 and scaled to fill the screen, like Tetris.
 */

#include "snake_view.h"
#include "app_page.h"
#include "snake.h"
#include "app.h"
#include "game_ui.h"
#include "pix.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static float s_sc = 1;
static int s_ox, s_oy;
#define K(n)    ((int)((float)(n) * s_sc + 0.5f))
#define X(n)    (s_ox + K(n))
#define Y(n)    (s_oy + K(n))

static sn_game_t s_g;
static bool s_shown, s_paused, s_started;
static char s_save[512];
static int s_best_before;
static int s_cs, s_fx, s_fy;              /* cell size, field's top left: screen pixels */
static uint8_t s_drawn[SN_H][SN_W];       /* what each cell shows, +1; 0: draw it */
static lv_obj_t * s_ui, * s_stats, * s_msg, * s_pause_label;

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

static void make_background(void)
{
    pix_t * p = pix_stage();
    pix_t b = *p;
    b.px = pix_stage_bg();
    pix_noclip(&b);
    pix_bg_gradient(0x0C1A10, 0x14261A);
    int bd = K(2);
    pix_rect(&b, s_fx - bd, s_fy - bd, SN_W * s_cs + 2 * bd, SN_H * s_cs + 2 * bd, 0x3E6A48);
    for(int y = 0; y < SN_H; y++)
        for(int x = 0; x < SN_W; x++)
            pix_rect(&b, s_fx + x * s_cs, s_fy + y * s_cs, s_cs, s_cs, (x + y) & 1 ? 0x12241A : 0x162B1F);
}

static int cell_code(int x, int y)
{
    sn_cell_t c = sn_cell(&s_g, x, y);
    int code = (int)c;
    if(c == SN_HEAD) code |= (int)s_g.dir << 3 | (s_g.over && !s_g.won) << 5;
    if(c == SN_BONUS && s_g.bonus_steps < 12) code |= (s_g.bonus_steps & 1) << 6; /* blinks before it goes */
    return code;
}

static void draw_cell(int x, int y, int code)
{
    pix_t * p = pix_stage();
    int px = s_fx + x * s_cs, py = s_fy + y * s_cs, s = s_cs;
    pix_clip(p, px, py, s, s);
    pix_restore(p, px, py, s, s);
    int in = s / 10 < 1 ? 1 : s / 10;
    switch(code & 7) {
        case SN_BODY:
            pix_block(p, px + in, py + in, s - 2 * in, s - 2 * in, (x + y) & 1 ? 0x43A047 : 0x4CAF50);
            break;
        case SN_HEAD: {
            int dir = code >> 3 & 3;
            bool dead = code >> 5 & 1;
            pix_block(p, px + in, py + in, s - 2 * in, s - 2 * in, dead ? 0xE53935 : 0x7CD67F);
            /* two eyes, towards where it goes */
            int e = s / 5 < 1 ? 1 : s / 5, a = s / 4, b = s - s / 4 - e, f = s - s / 3 - e / 2;
            int ex[2], ey[2];
            if(dir == SN_RIGHT || dir == SN_LEFT) {
                ex[0] = ex[1] = dir == SN_RIGHT ? f : s / 3 - e / 2;
                ey[0] = a;
                ey[1] = b;
            }
            else {
                ey[0] = ey[1] = dir == SN_DOWN ? f : s / 3 - e / 2;
                ex[0] = a;
                ex[1] = b;
            }
            for(int i = 0; i < 2; i++) pix_rect(p, px + ex[i], py + ey[i], e, e, 0x10140F);
            break;
        }
        case SN_FOOD:
            pix_disc(p, (float)px + s * 0.5f, (float)py + s * 0.55f, s * 0.36f, 0xE53935);
            pix_disc(p, (float)px + s * 0.4f, (float)py + s * 0.45f, s * 0.1f, 0xFF8A80);
            pix_rect(p, px + s / 2, py + s / 10, s / 6 > 0 ? s / 6 : 1, s / 5, 0x66BB6A);
            break;
        case SN_BONUS:
            if(!(code >> 6 & 1)) {
                pix_disc(p, (float)px + s * 0.5f, (float)py + s * 0.5f, s * 0.42f, 0xFFC107);
                pix_disc(p, (float)px + s * 0.38f, (float)py + s * 0.38f, s * 0.12f, 0xFFF59D);
            }
            break;
        default: break;
    }
    pix_noclip(p);
    pix_flush(px, py, s, s);
}

/* Every cell whose picture changed (all of them after `full`). */
static void draw(bool full)
{
    pix_t * p = pix_stage();
    if(!p) return;
    if(full) {
        pix_noclip(p);
        pix_restore(p, 0, 0, p->w, p->h);
        pix_flush_all();
        memset(s_drawn, 0, sizeof(s_drawn));
    }
    for(int y = 0; y < SN_H; y++)
        for(int x = 0; x < SN_W; x++) {
            int c = cell_code(x, y);
            if(s_drawn[y][x] != c + 1) {
                s_drawn[y][x] = (uint8_t)(c + 1);
                draw_cell(x, y, c);
            }
        }
}

/* ---------------- the game ---------------- */

static void new_game(void)
{
    best_save();
    sn_new(&s_g, (uint32_t)time(NULL) ^ lv_tick_get() * 2654435761u, s_g.best > 0 ? s_g.best : best_load());
    s_best_before = s_g.best;
    s_paused = false;
    s_started = true;
}

static void toggle_pause(void)
{
    if(!s_g.over && s_g.started) s_paused = !s_paused;
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

void snake_view_set_menu(void (*cb)(void)) { s_menu_cb = cb; }

/* A swipe anywhere on the field turns, once per touch. */
static int s_press_x, s_press_y;
static bool s_swipe_armed;

static void input(lv_event_code_t code, int x, int y)
{
    if(code == LV_EVENT_PRESSED) {
        s_press_x = x;
        s_press_y = y;
        s_swipe_armed = true;
        return;
    }
    if(code != LV_EVENT_PRESSING || !s_swipe_armed || s_paused) return;
    int dx = x - s_press_x, dy = y - s_press_y;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if((adx > ady ? adx : ady) < K(18)) return;
    s_swipe_armed = false;
    sn_turn(&s_g, adx > ady ? (dx < 0 ? SN_LEFT : SN_RIGHT) : (dy < 0 ? SN_UP : SN_DOWN));
}

void snake_view_create(int w, int h)
{
    s_sc = (float)w / 480.0f < (float)h / 272.0f ? (float)w / 480.0f : (float)h / 272.0f;
    s_ox = (w - K(480)) / 2;
    s_oy = (h - K(272)) / 2;
    app_save_path(s_save, sizeof(s_save), "snake");
    s_cs = K(16);
    s_fx = (w - SN_W * s_cs) / 2;
    s_fy = Y(28);

    s_ui = gu_layer();
    lv_obj_t * home = gu_button(s_ui, LV_SYMBOL_HOME, X(6), Y(4), K(26), K(20), K(12), menu_cb, NULL);
    (void)home;
    lv_obj_t * p = gu_button(s_ui, LV_SYMBOL_PAUSE, X(36), Y(4), K(26), K(20), K(12), pause_cb, NULL);
    s_pause_label = lv_obj_get_child(p, 0);
    lv_obj_t * t = gu_label(s_ui, "SNAKE", K(14), lv_color_hex(0xFFD700));
    lv_obj_set_pos(t, X(70), Y(6));
    lv_obj_t * help = gu_label(s_ui, "Arrows or swipe: steer    P: pause    Esc: menu", K(10), lv_color_hex(0xA0B8A4));
    lv_obj_set_pos(help, X(132), Y(9));
    s_stats = gu_label(s_ui, "", K(12), lv_color_white());
    lv_obj_set_style_text_align(s_stats, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_stats, K(130));
    lv_obj_set_pos(s_stats, X(480 - 8 - 130), Y(6));
    s_msg = gu_message(s_ui, K(13), K(1), msg_cb);

    sn_new(&s_g, 1, 0);
}

void snake_view_show(bool on)
{
    if(on == s_shown) return;
    s_shown = on;
    lv_obj_set_hidden(s_ui, !on);
    pix_stage_show(on);
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

void snake_view_key(int k)
{
    bool go = k == APP_KEY_SPACE || k == APP_KEY_ENTER;
    if(s_g.over) {
        if(go || k == 'n') new_game();
        return;
    }
    if(k == 'p') {
        toggle_pause();
        return;
    }
    if(k == 'n') {
        new_game();
        return;
    }
    if(s_paused) {
        if(go) s_paused = false;
        return;
    }
    switch(k) {
        case APP_KEY_UP: case 'w': sn_turn(&s_g, SN_UP); break;
        case APP_KEY_RIGHT: case 'd': sn_turn(&s_g, SN_RIGHT); break;
        case APP_KEY_DOWN: case 's': sn_turn(&s_g, SN_DOWN); break;
        case APP_KEY_LEFT: case 'a': sn_turn(&s_g, SN_LEFT); break;
        default: break;
    }
}

static void ui_update(void)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "#a0b8a4 Score#  %d    #a0b8a4 Best#  %d", s_g.score, s_g.best);
    gu_set_text(s_stats, buf);
    const char * msg = NULL;
    if(s_g.over) {
        snprintf(buf, sizeof(buf), "#ffd700 %s#\nScore %d%s\n\nSpace or tap here: play again",
                 s_g.won ? "THE FIELD IS FULL!" : "GAME OVER", s_g.score,
                 s_g.score > s_best_before ? "   NEW BEST!" : "");
        msg = buf;
    }
    else if(s_paused) msg = "#ffd700 PAUSED#\n\nSpace, P or tap here: go on";
    else if(!s_g.started) msg = "#ffd700 SNAKE#\nEat the apples, grow, and keep off\nthe walls and your own tail.\n\n"
                                "An arrow key or a swipe starts.";
    gu_message_set(s_msg, msg);
    const char * pl = s_paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE;
    gu_set_text(s_pause_label, pl);
}

void snake_view_tick(uint32_t dt_ms)
{
    if(!s_shown) return;
    bool was_over = s_g.over;
    if(!s_paused) sn_update(&s_g, (int)dt_ms);
    if(s_g.over && !was_over) best_save();
    if(pix_stage()) draw(false);
    ui_update();
}

bool snake_view_running(void) { return s_started && s_g.started && !s_g.over; }
void snake_view_new_game(void) { new_game(); }
int snake_view_best(void) { return s_started ? s_g.best : best_load(); }

void snake_view_preview(void)
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
