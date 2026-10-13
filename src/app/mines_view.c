/*
 * Minesweeper in the app (rules: src/mines).
 *
 * Three sizes -- 9x9 with 10 mines, 16x16 with 40, 30x16 with 99 -- on the
 * shared canvas (pix.h); a cell is redrawn only when it changes. The
 * keyboard has a cursor: arrows move it, Space or Enter opens (on a number:
 * the rest around it, once its flags are placed), F or X flags, 1 2 3 pick
 * the size, N starts again. A tap opens, a long press flags. Esc (app.c)
 * or the home button goes to the game's page in the menu, which also sets
 * the size and "no guessing" (on by default: boards a solver clears without
 * a guess, mines.h). The best time per size is kept, and the choice.
 *
 * Laid out for 480x272 and scaled to fill the screen, like Tetris.
 */

#include "mines_view.h"
#include "app_page.h"
#include "mines.h"
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

typedef struct { int w, h, mines; const char * name; } mi_size_t;
static const mi_size_t k_sizes[3] = { { 9, 9, 10, "Easy" }, { 16, 16, 40, "Medium" }, { 30, 16, 99, "Hard" } };

static mi_game_t s_g;
static int s_size, s_cx, s_cy;           /* the size, the cursor */
static bool s_shown, s_started;
static char s_save[512];
static uint32_t s_best[3];               /* ms, 0: none */
static bool s_no_guess = true;           /* boards that never need a guess (mines.h) */
static bool s_new_best;
static uint32_t s_over_at;               /* when the game ended: Space starts again only after a second */
static int s_cs, s_fx, s_fy;             /* cell size, field's top left: screen pixels */
static uint16_t s_drawn[MI_MAXH][MI_MAXW];
static uint32_t s_seen = ~0u;
static int s_seen_cx = -1, s_seen_cy = -1;
static lv_obj_t * s_ui, * s_stats, * s_status, * s_size_btn[3];

/* ---------------- best times ---------------- */

static void best_load(void)
{
    FILE * f = fopen(s_save, "r");
    unsigned a = 0, b = 0, c = 0;
    int ng = 1;
    if(f) {
        if(fscanf(f, "best %u %u %u", &a, &b, &c) != 3) a = b = c = 0;
        else if(fscanf(f, " noguess %d", &ng) != 1) ng = 1;
        fclose(f);
    }
    s_no_guess = ng != 0;
    s_best[0] = a;
    s_best[1] = b;
    s_best[2] = c;
}

static void best_save(void)
{
    FILE * f = fopen(s_save, "w");
    if(f) {
        fprintf(f, "best %u %u %u\nnoguess %d\n", (unsigned)s_best[0], (unsigned)s_best[1], (unsigned)s_best[2],
                s_no_guess ? 1 : 0);
        fclose(f);
    }
}

/* ---------------- drawing ---------------- */

enum { C_CLOSED, C_FLAG, C_OPEN, C_MINE, C_BOOM, C_WRONG };

static int cell_code(int x, int y)
{
    int kind;
    bool lost = s_g.over && !s_g.won;
    if(s_g.open[y][x]) kind = C_OPEN;
    else if(s_g.flag[y][x]) kind = lost && !s_g.mine[y][x] ? C_WRONG : C_FLAG;
    else if(lost && s_g.mine[y][x]) kind = x == s_g.boom_x && y == s_g.boom_y ? C_BOOM : C_MINE;
    else kind = C_CLOSED;
    int adj = kind == C_OPEN ? s_g.adj[y][x] : 0;
    return kind | adj << 3 | (x == s_cx && y == s_cy) << 7;
}

static void draw_mine(pix_t * p, int x, int y, int s)
{
    float cx = (float)x + s * 0.5f, cy = (float)y + s * 0.5f;
    int t = s / 12 < 1 ? 1 : s / 12, l = s * 7 / 10;
    pix_rect(p, (int)cx - l / 2, (int)cy - t / 2, l, t, 0x111111);
    pix_rect(p, (int)cx - t / 2, (int)cy - l / 2, t, l, 0x111111);
    pix_disc(p, cx, cy, s * 0.26f, 0x111111);
    pix_disc(p, cx - s * 0.08f, cy - s * 0.08f, s * 0.07f, 0xBBBBBB);
}

static void draw_flag(pix_t * p, int x, int y, int s)
{
    int t = s / 12 < 1 ? 1 : s / 12;
    int pole = x + s * 58 / 100, top = y + s / 5, bottom = y + s * 78 / 100;
    pix_rect(p, pole, top, t, bottom - top, 0x202020);
    pix_rect(p, x + s * 3 / 10, bottom - t, s * 45 / 100, t + t / 2 + 1, 0x202020);
    int th = s * 34 / 100, tw = s * 34 / 100;
    for(int j = 0; j < th; j++) {
        int d = j < th / 2 ? j : th - 1 - j;
        int w = tw * (2 * d + 1) / (th > 1 ? th : 1);
        pix_rect(p, pole - w, top + j, w, 1, 0xE53935);
    }
}

static void draw_cell(int x, int y, int code)
{
    static const uint32_t k_num[9] = { 0, 0x64B5F6, 0x81C784, 0xE57373, 0xBA68C8, 0xFFB74D, 0x4DD0E1, 0xEEEEEE, 0x9E9E9E };
    pix_t * p = pix_stage();
    int px = s_fx + x * s_cs, py = s_fy + y * s_cs, s = s_cs;
    int kind = code & 7, adj = code >> 3 & 15;
    pix_clip(p, px, py, s, s);
    if(kind == C_CLOSED || kind == C_FLAG) {
        pix_block(p, px, py, s, s, 0x5A6272);
        if(kind == C_FLAG) draw_flag(p, px, py, s);
    }
    else {
        pix_rect(p, px, py, s, s, 0x171B22);
        pix_rect(p, px + 1, py + 1, s - 1, s - 1, kind == C_BOOM ? 0xC62828 : 0x262C36);
        if(kind == C_OPEN && adj > 0) {
            int sc = s * 55 / 100 / 7 < 1 ? 1 : s * 55 / 100 / 7;
            pix_digit(p, px + (s - 5 * sc + 1) / 2, py + (s - 7 * sc + 1) / 2, sc, adj, k_num[adj]);
        }
        if(kind == C_MINE || kind == C_BOOM || kind == C_WRONG) draw_mine(p, px, py, s);
        if(kind == C_WRONG) {
            /* a red cross over it */
            int t = s / 10 < 1 ? 1 : s / 10;
            for(int i = s / 5; i < s - s / 5; i++) {
                pix_rect(p, px + i, py + i - t / 2, t, t, 0xFF1744);
                pix_rect(p, px + s - 1 - i, py + i - t / 2, t, t, 0xFF1744);
            }
        }
    }
    if(code >> 7 & 1) {
        int b = s / 10 < 2 ? 2 : s / 10;
        pix_rect(p, px, py, s, b, 0xFFD700);
        pix_rect(p, px, py + s - b, s, b, 0xFFD700);
        pix_rect(p, px, py, b, s, 0xFFD700);
        pix_rect(p, px + s - b, py, b, s, 0xFFD700);
    }
    pix_noclip(p);
    pix_flush(px, py, s, s);
}

static void layout(void)
{
    const mi_size_t * z = &k_sizes[s_size];
    int a = K(460) / z->w, b = K(222) / z->h;
    s_cs = a < b ? a : b;
    pix_t * p = pix_stage();
    int w = p ? p->w : K(480);
    s_fx = (w - z->w * s_cs) / 2;
    s_fy = Y(30) + (K(222) - z->h * s_cs) / 2;
}

static void make_background(void)
{
    pix_t b = *pix_stage();
    b.px = pix_stage_bg();
    pix_noclip(&b);
    pix_bg_gradient(0x141820, 0x1E2230);
    int bd = K(2);
    const mi_size_t * z = &k_sizes[s_size];
    pix_rect(&b, s_fx - bd, s_fy - bd, z->w * s_cs + 2 * bd, z->h * s_cs + 2 * bd, 0x6A7080);
}

static void draw(bool full)
{
    pix_t * p = pix_stage();
    if(!p) return;
    if(full) {
        make_background();
        pix_noclip(p);
        pix_restore(p, 0, 0, p->w, p->h);
        pix_flush_all();
        memset(s_drawn, 0, sizeof(s_drawn));
        s_seen = ~0u;
    }
    if(s_g.changed == s_seen && s_cx == s_seen_cx && s_cy == s_seen_cy) return;
    s_seen = s_g.changed;
    s_seen_cx = s_cx;
    s_seen_cy = s_cy;
    for(int y = 0; y < s_g.h; y++)
        for(int x = 0; x < s_g.w; x++) {
            int c = cell_code(x, y);
            if(s_drawn[y][x] != c + 1) {
                s_drawn[y][x] = (uint16_t)(c + 1);
                draw_cell(x, y, c);
            }
        }
}

/* ---------------- the game ---------------- */

static void new_game(int size)
{
    bool resize = size != s_size || !s_started;
    s_size = size;
    const mi_size_t * z = &k_sizes[size];
    mi_new(&s_g, z->w, z->h, z->mines, (uint32_t)time(NULL) ^ lv_tick_get() * 2654435761u, s_no_guess);
    s_cx = z->w / 2;
    s_cy = z->h / 2;
    s_new_best = false;
    s_started = true;
    for(int i = 0; i < 3; i++)
        lv_obj_set_style_border_color(s_size_btn[i], i == size ? lv_color_hex(0xFFD700) : lv_color_hex(0x6A7080), 0);
    if(resize) layout();
    if(s_shown) draw(resize);
}

static void after_move(bool was_over)
{
    if(s_g.over && !was_over) s_over_at = lv_tick_get();
    if(s_g.over && !was_over && s_g.won) {
        uint32_t *b = &s_best[s_size];
        if(*b == 0 || s_g.ms < *b) {
            *b = s_g.ms;
            s_new_best = true;
            best_save();
        }
    }
}

static void open_at(int x, int y)
{
    bool was = s_g.over;
    mi_open(&s_g, x, y);
    after_move(was);
}

static void (*s_menu_cb)(void);
static void menu_cb(lv_event_t * e) { (void)e; if(s_menu_cb) s_menu_cb(); }
static void size_cb(lv_event_t * e) { new_game((int)(intptr_t)lv_event_get_user_data(e)); }

void mines_view_set_menu(void (*cb)(void)) { s_menu_cb = cb; }

/* A tap opens, a long press flags. */
static void input(lv_event_code_t code, int x, int y)
{
    if(code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
    if(x < s_fx || y < s_fy) return;
    int cx = (x - s_fx) / s_cs, cy = (y - s_fy) / s_cs;
    if(cx >= s_g.w || cy >= s_g.h) return;
    if(s_g.over) {
        if(lv_tick_elaps(s_over_at) > 1000) new_game(s_size);
        return;
    }
    s_cx = cx;
    s_cy = cy;
    if(code == LV_EVENT_LONG_PRESSED) mi_flag(&s_g, cx, cy);
    else open_at(cx, cy);
}

void mines_view_create(int w, int h)
{
    s_sc = (float)w / 480.0f < (float)h / 272.0f ? (float)w / 480.0f : (float)h / 272.0f;
    s_ox = (w - K(480)) / 2;
    s_oy = (h - K(272)) / 2;
    app_save_path(s_save, sizeof(s_save), "mines");

    s_ui = gu_layer();
    gu_button(s_ui, LV_SYMBOL_HOME, X(6), Y(4), K(26), K(20), K(12), menu_cb, NULL);
    lv_obj_t * t = gu_label(s_ui, "MINESWEEPER", K(13), lv_color_hex(0xFFD700));
    lv_obj_set_pos(t, X(38), Y(6));
    for(int i = 0; i < 3; i++) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d  %s", i + 1, k_sizes[i].name);
        s_size_btn[i] = gu_button(s_ui, buf, X(150 + i * 60), Y(4), K(56), K(20), K(10), size_cb, (void *)(intptr_t)i);
    }
    s_stats = gu_label(s_ui, "", K(12), lv_color_white());
    lv_obj_set_style_text_align(s_stats, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_stats, K(140));
    lv_obj_set_pos(s_stats, X(480 - 6 - 140), Y(6));
    s_status = gu_label(s_ui, "", K(10), lv_color_hex(0xB0B8C8));
    lv_obj_set_width(s_status, K(480));
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_status, X(0), Y(256));
    best_load();
}

void mines_view_show(bool on)
{
    if(on == s_shown) return;
    s_shown = on;
    lv_obj_set_hidden(s_ui, !on);
    pix_stage_show(on);
    if(on) {
        pix_stage_set_input(input);
        if(!s_started) new_game(0);
        else layout();
        draw(true);
    }
}

void mines_view_key(int k)
{
    if(k >= '1' && k <= '3') {
        new_game(k - '1');
        return;
    }
    bool go = (k == APP_KEY_SPACE || k == APP_KEY_ENTER) && lv_tick_elaps(s_over_at) > 1000;
    if(k == 'n' || (s_g.over && go)) {
        new_game(s_size);
        return;
    }
    switch(k) {
        case APP_KEY_LEFT: s_cx = (s_cx - 1 + s_g.w) % s_g.w; break;
        case APP_KEY_RIGHT: s_cx = (s_cx + 1) % s_g.w; break;
        case APP_KEY_UP: s_cy = (s_cy - 1 + s_g.h) % s_g.h; break;
        case APP_KEY_DOWN: s_cy = (s_cy + 1) % s_g.h; break;
        case APP_KEY_SPACE: case APP_KEY_ENTER:
            if(!s_g.over) open_at(s_cx, s_cy);
            break;
        case 'f': case 'x': mi_flag(&s_g, s_cx, s_cy); break;
        default: break;
    }
}

static void fmt_time(char * out, size_t n, uint32_t ms) { snprintf(out, n, "%u.%u", (unsigned)(ms / 1000), (unsigned)(ms / 100 % 10)); }

static void ui_update(void)
{
    char buf[200], t[16], b[16];
    fmt_time(t, sizeof(t), s_g.ms);
    snprintf(buf, sizeof(buf), "#b0b8c8 Mines# %d   #b0b8c8 Time# %u", s_g.mines - s_g.flags, (unsigned)(s_g.ms / 1000));
    gu_set_text(s_stats, buf);
    if(s_g.over && s_g.won)
        snprintf(buf, sizeof(buf), "#ffd700 CLEARED in %s s!#%s   Space, N or a tap: again", t, s_new_best ? "  #ffd700 NEW BEST#" : "");
    else if(s_g.over) snprintf(buf, sizeof(buf), "#ff6e6e BOOM!#   Space, N or a tap: again");
    else {
        if(s_best[s_size]) fmt_time(b, sizeof(b), s_best[s_size]);
        snprintf(buf, sizeof(buf), "Arrows: move   Space: open   F: flag   1 2 3: size   N: new   Esc: menu%s%s%s",
                 s_best[s_size] ? "     #ffd700 Best " : "", s_best[s_size] ? b : "", s_best[s_size] ? " s#" : "");
    }
    gu_set_text(s_status, buf);
}

void mines_view_tick(uint32_t dt_ms)
{
    if(!s_shown) return;
    mi_tick(&s_g, (int)dt_ms);
    draw(false);
    ui_update();
}

bool mines_view_running(void) { return s_started && s_g.laid && !s_g.over; }
void mines_view_new_game(void) { new_game(s_size); }
int mines_view_size(void) { return s_size; }
const char * mines_view_size_name(int size)
{
    static char buf[40];
    const mi_size_t * z = &k_sizes[size];
    snprintf(buf, sizeof(buf), "%s  %dx%d, %d mines", z->name, z->w, z->h, z->mines);
    return buf;
}
void mines_view_set_size(int size) { new_game((size % 3 + 3) % 3); }
bool mines_view_no_guess(void) { return s_no_guess; }
void mines_view_set_no_guess(bool on)
{
    s_no_guess = on;
    if(!s_g.laid) s_g.no_guess = on; /* the board waiting for its first opening takes it too */
    best_save();
}
const char * mines_view_best_text(void)
{
    static char buf[80];
    char t[3][16];
    for(int i = 0; i < 3; i++) {
        if(s_best[i]) {
            fmt_time(t[i], sizeof(t[i]) - 2, s_best[i]);
            strcat(t[i], " s");
        }
        else snprintf(t[i], sizeof(t[i]), "-");
    }
    snprintf(buf, sizeof(buf), "Easy %s\nMedium %s\nHard %s", t[0], t[1], t[2]);
    return buf;
}

void mines_view_preview(void)
{
    if(!s_started) new_game(0);
    else layout();
    pix_t * p = pix_stage();
    if(p) {
        draw(true);
        page_pic_pixels(p->px, p->stride);
    }
    ui_update();
    page_pic_widgets(s_ui);
}
