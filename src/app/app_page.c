/*
 * A game's page in the app's menu (app_page.h).
 *
 * One panel, filled from the page_t of the game it shows. Laid out for
 * 480x272 and scaled with the menu (ui.h):
 *
 *   NAME                                                     [x]
 *   +-------------------+   Play / Continue
 *   |  the game, small  |   New game
 *   +-------------------+   settings ...
 *   best score              what the game is (when it fits)
 *   keys
 *   hint
 */

#include "app_page.h"
#include "app.h"
#include "app_ui.h"
#include "lvgl_private.h" /* drawing widgets into our own buffer, as lv_snapshot does */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#define MAX_ROWS 10 /* the column holds 10 (ROW_H + ROW_GAP each); Rolling Rob has 9 */
#define PIC_W 214 /* the picture's width; its height follows the screen */
#define COL_Y 30
#define COL_H 222
#define ROW_H 19
#define ROW_GAP 3

static const page_t * s_p;
static int s_sel;
static lv_obj_t * s_panel, * s_head, * s_about, * s_keys, * s_best, * s_right;
static lv_obj_t * s_row[MAX_ROWS], * s_row_label[MAX_ROWS], * s_row_value[MAX_ROWS];
static void (*s_back)(void);
static uint32_t s_rows_shown; /* which rows are up: when it changes, the about text may (not) fit */

/* the picture */
static lv_obj_t * s_pic_box, * s_pic;
static lv_draw_buf_t * s_pic_buf;
static uint32_t * s_frame; /* full size */
static int s_fw, s_fh;
static lv_draw_buf_t s_frame_buf; /* s_frame, for LVGL to draw widgets into */
static int32_t * s_col0;           /* the frame columns where each picture column starts */
static uint32_t s_recip[257];      /* 65536 / n, for averaging n pixels without a divide */
static double s_widget_ms; /* time in page_pic_widgets for this picture (ROLLING_ROB_FPS prints it) */
static bool s_pic_due;     /* a picture is wanted at s_pic_at (page_refresh draws it) */
static bool s_pic_asked;   /* the action being done asked for its own moment */
static uint32_t s_pic_at;

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static bool shown(int i) { return i < s_p->n && (!s_p->rows[i].shown || s_p->rows[i].shown()); }

static void set_text(lv_obj_t * l, const char * t)
{
    if(strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t);
}

/* ---------------- the picture ---------------- */

uint32_t * page_pic_frame(int * w, int * h)
{
    if(w) *w = s_fw;
    if(h) *h = s_fh;
    return s_frame;
}

void page_pic_pixels(const uint32_t * px, int stride_px)
{
    if(!s_frame) return;
    for(int y = 0; y < s_fh; y++) memcpy(s_frame + (size_t)y * s_fw, px + (size_t)y * stride_px, (size_t)s_fw * 4);
}

/* Bands may come from several threads at once; they never share a row. */
void page_pic_band(uint32_t * band, int stride_px, int y0, int rows, void * user)
{
    (void)user;
    if(!s_frame) return;
    for(int r = 0; r < rows && y0 + r < s_fh; r++)
        if(y0 + r >= 0) memcpy(s_frame + (size_t)(y0 + r) * s_fw, band + (size_t)r * stride_px, (size_t)s_fw * 4);
}

/* LVGL draws the object straight into the frame, the way lv_snapshot draws
 * into its buffer -- no buffer of its own to clear, fill and blend over
 * (that was slow on a small processor). */
void page_pic_widgets(lv_obj_t * root)
{
    if(!s_frame || !root) return;
    double t0 = now_ms();
    lv_obj_update_layout(root);
    lv_display_t * disp = lv_obj_get_display(root);
    lv_area_t all = { 0, 0, s_fw - 1, s_fh - 1 };
    lv_layer_t layer;
    lv_layer_init(&layer);
    layer.draw_buf = &s_frame_buf;
    layer.display = disp;
    layer.buf_area = all;
    layer.color_format = LV_COLOR_FORMAT_XRGB8888;
    layer._clip_area = all;
    layer.phy_clip_area = all;
    lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_CREATED, &layer);
    lv_display_t * disp_old = lv_refr_get_disp_refreshing();
    lv_layer_t * head_old = disp->layer_head;
    disp->layer_head = &layer;
    lv_refr_set_disp_refreshing(disp);
    lv_obj_redraw(&layer, root);
    layer.all_tasks_added = true;
    for(int guard = 0; layer.draw_task_head && guard < 100000; guard++) lv_draw_dispatch();
    lv_draw_wait_for_finish();
    disp->layer_head = head_old;
    lv_refr_set_disp_refreshing(disp_old);
    lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_DELETED, &layer);
    s_widget_ms += now_ms() - t0;
}

/* The frame into the picture: each picture pixel the average of the frame
 * pixels it covers. Per picture row, the frame rows it covers are first
 * summed column by column (one straight pass, red and blue together in one
 * word), then runs of those sums; the average is a multiply (some ARM cores
 * have no divide instruction). */
static uint32_t * s_sum_rb, * s_sum_g; /* one frame row of column sums */

static void pic_scale(void)
{
    int pw = (int)s_pic_buf->header.w, ph = (int)s_pic_buf->header.h, ds = (int)(s_pic_buf->header.stride / 4);
    uint32_t * dst = (uint32_t *)s_pic_buf->data;
    uint32_t * restrict srb = s_sum_rb, * restrict sg = s_sum_g;
    for(int y = 0; y < ph; y++) {
        int y0 = y * s_fh / ph, y1 = (y + 1) * s_fh / ph;
        if(y1 <= y0) y1 = y0 + 1;
        for(int sy = y0; sy < y1; sy++) {
            const uint32_t * restrict p = s_frame + (size_t)sy * s_fw;
            int x = 0;
            if(sy == y0) {
#if defined(__ARM_NEON)
                for(; x + 4 <= s_fw; x += 4) {
                    uint32x4_t v = vld1q_u32(p + x);
                    vst1q_u32(srb + x, vandq_u32(v, vdupq_n_u32(0x00FF00FFu)));
                    vst1q_u32(sg + x, vandq_u32(v, vdupq_n_u32(0x0000FF00u)));
                }
#endif
                for(; x < s_fw; x++) {
                    srb[x] = p[x] & 0x00FF00FFu;
                    sg[x] = p[x] & 0x0000FF00u;
                }
                continue;
            }
#if defined(__ARM_NEON)
            for(; x + 4 <= s_fw; x += 4) {
                uint32x4_t v = vld1q_u32(p + x);
                vst1q_u32(srb + x, vaddq_u32(vld1q_u32(srb + x), vandq_u32(v, vdupq_n_u32(0x00FF00FFu))));
                vst1q_u32(sg + x, vaddq_u32(vld1q_u32(sg + x), vandq_u32(v, vdupq_n_u32(0x0000FF00u))));
            }
#endif
            for(; x < s_fw; x++) {
                srb[x] += p[x] & 0x00FF00FFu;
                sg[x] += p[x] & 0x0000FF00u;
            }
        }
        int ny = y1 - y0;
        uint32_t * out = dst + (size_t)y * ds;
        for(int x = 0; x < pw; x++) {
            int x0 = s_col0[x], x1 = s_col0[x + 1];
            uint32_t rb = 0, g = 0;
            for(int sx = x0; sx < x1; sx++) {
                rb += srb[sx];
                g += sg[sx];
            }
            uint32_t inv = s_recip[(x1 - x0) * ny];
            uint32_t r = ((rb >> 16) * inv) >> 16, b = ((rb & 0xFFFF) * inv) >> 16, gg = ((g >> 8) * inv) >> 16;
            out[x] = 0xFF000000u | r << 16 | gg << 8 | b;
        }
    }
    lv_obj_invalidate(s_pic);
}

void page_pic_fill(uint32_t rgb)
{
    if(!s_frame) return;
    for(size_t i = 0; i < (size_t)s_fw * s_fh; i++) s_frame[i] = 0xFF000000u | rgb;
}

void page_pic_later(uint32_t ms)
{
    s_pic_due = true;
    s_pic_asked = true;
    s_pic_at = lv_tick_get() + ms;
}

void page_pic_update(void)
{
    if(!s_p || !s_frame || !s_pic_buf || lv_obj_is_hidden(s_panel)) return;
    if(!s_p->preview) return;
    s_pic_due = false;
    double t0 = now_ms();
    s_widget_ms = 0;
    s_p->preview();
    double t1 = now_ms();
    pic_scale();
    double t2 = now_ms();
    static int verbose = -1;
    if(verbose < 0) verbose = getenv("ROLLING_ROB_FPS") != NULL;
    if(verbose)
        printf("page picture %s: %.1f ms (game %.1f, widgets %.1f, scale %.1f)\n", s_p->name, t2 - t0,
               t1 - t0 - s_widget_ms, s_widget_ms, t2 - t1);
}

/* ---------------- the lines ---------------- */

/* Whether "what the game is" fits under the rows. */
static void fit_about(void)
{
    lv_obj_set_hidden(s_about, false);
    lv_obj_update_layout(s_right);
    lv_area_t a, r;
    lv_obj_get_coords(s_about, &a);
    lv_obj_get_coords(s_right, &r);
    lv_obj_set_hidden(s_about, a.y2 > r.y2);
}

void page_refresh(void)
{
    if(!s_p || lv_obj_is_hidden(s_panel)) return;
    uint32_t mask = 0;
    for(int i = 0; i < MAX_ROWS; i++) {
        bool on = shown(i);
        if(on) mask |= 1u << i;
        if(lv_obj_is_hidden(s_row[i]) == on) lv_obj_set_hidden(s_row[i], !on);
        if(!on) continue;
        const page_row_t * r = &s_p->rows[i];
        char buf[128];
        const char * text = r->label ? r->label() : r->text;
        if(r->key) {
            char k = (char)(r->key >= 'a' && r->key <= 'z' ? r->key - 'a' + 'A' : r->key);
            snprintf(buf, sizeof(buf), "%s  #9aa09a %c#", text, k);
        }
        else snprintf(buf, sizeof(buf), "%s", text);
        set_text(s_row_label[i], buf);
        bool sel = i == s_sel;
        if(r->value) {
            if(sel) snprintf(buf, sizeof(buf), LV_SYMBOL_LEFT "  %s  " LV_SYMBOL_RIGHT, r->value());
            else snprintf(buf, sizeof(buf), "%s", r->value());
            set_text(s_row_value[i], buf);
        }
        else set_text(s_row_value[i], "");
        lv_obj_set_style_border_color(s_row[i], sel ? GOLD : lv_color_make(0x44, 0x48, 0x44), 0);
        lv_obj_set_style_border_width(s_row[i], sel ? K(2) : K(1), 0);
        lv_obj_set_style_bg_color(s_row[i], sel ? lv_color_make(0x33, 0x30, 0x18) : TILE, 0);
    }
    if(s_p->best) set_text(s_best, s_p->best());
    if(mask != s_rows_shown) {
        s_rows_shown = mask;
        fit_about();
    }
    if(s_pic_due && (int32_t)(lv_tick_get() - s_pic_at) >= 0) page_pic_update();
}

static void select_row(int i)
{
    s_sel = i;
    page_refresh();
}

static void move(int d)
{
    int i = s_sel;
    for(int n = 0; n < s_p->n; n++) {
        i = (i + d + s_p->n) % s_p->n;
        if(shown(i)) {
            select_row(i);
            return;
        }
    }
}

static void act(int i, int dir)
{
    const page_t * p = s_p;
    if(dir && !p->rows[i].value) return;
    s_pic_asked = false;
    p->rows[i].act(dir);
    if(s_p == p && !lv_obj_is_hidden(s_panel)) {
        /* the action may have left the page; if not, show what it did: the
         * line now, the picture (it takes a while) from the next tick,
         * unless the action asked for it later */
        if(!s_pic_asked) page_pic_later(0);
        page_refresh();
    }
}

static void row_cb(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_sel = i;
    act(i, 0);
}

static void close_cb(lv_event_t * e)
{
    (void)e;
    if(s_back) s_back();
}

static lv_obj_t * column(int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t * c = lv_obj_create(s_panel);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_clickable(c, false);
    lv_obj_set_scrollable(c, false);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    return c;
}

void page_create(void (*back)(void))
{
    s_back = back;
    s_panel = app_panel();
    s_head = app_heading(s_panel, "");
    app_close_button(s_panel, close_cb);

    /* left: the picture, the best, the keys */
    int k;
    app_picture(&s_fw, &s_fh, &k);
    int32_t pw = K(PIC_W), ph = pw * s_fh / s_fw;
    s_frame = malloc((size_t)s_fw * s_fh * 4);
    if(s_frame)
        lv_draw_buf_init(&s_frame_buf, (uint32_t)s_fw, (uint32_t)s_fh, LV_COLOR_FORMAT_XRGB8888, (uint32_t)s_fw * 4, s_frame,
                         (uint32_t)s_fw * s_fh * 4);
    s_pic_buf = lv_draw_buf_create((uint32_t)pw, (uint32_t)ph, LV_COLOR_FORMAT_XRGB8888, LV_STRIDE_AUTO);
    /* a picture pixel covers at most 256 frame pixels (16 x 16: a 3440 px wide frame) */
    s_col0 = malloc(sizeof(int32_t) * (size_t)(pw + 1));
    if(s_col0)
        for(int x = 0; x <= pw; x++) s_col0[x] = x * s_fw / pw;
    for(int n = 1; n <= 256; n++) s_recip[n] = (65536u + (uint32_t)n / 2) / (uint32_t)n;
    s_sum_rb = malloc(sizeof(uint32_t) * (size_t)s_fw);
    s_sum_g = malloc(sizeof(uint32_t) * (size_t)s_fw);
    if(!s_col0 || !s_sum_rb || !s_sum_g || s_fw > 16 * pw || s_fh > 16 * (int)ph) {
        free(s_frame);
        s_frame = NULL;
    }
    s_pic_box = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_pic_box);
    lv_obj_set_size(s_pic_box, pw + 2 * K(1), ph + 2 * K(1));
    lv_obj_set_pos(s_pic_box, K(12) - K(1), K(COL_Y) - K(1));
    lv_obj_set_style_border_color(s_pic_box, lv_color_make(0x55, 0x5A, 0x55), 0);
    lv_obj_set_style_border_width(s_pic_box, K(1), 0);
    lv_obj_set_clickable(s_pic_box, false);
    lv_obj_set_scrollable(s_pic_box, false);
    if(s_pic_buf) {
        lv_draw_buf_clear(s_pic_buf, NULL);
        s_pic = lv_canvas_create(s_pic_box);
        lv_canvas_set_draw_buf(s_pic, s_pic_buf);
        lv_obj_center(s_pic);
    }

    int32_t ly = K(COL_Y) + ph + K(5);
    lv_obj_t * left = column(K(12), ly, pw, K(COL_Y + COL_H) - ly);
    lv_obj_set_style_pad_row(left, K(4), 0);
    s_best = lv_label_create(left);
    lv_label_set_recolor(s_best, true);
    lv_label_set_long_mode(s_best, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_best, LV_PCT(100));
    lv_obj_set_style_text_font(s_best, FONT(10), 0);
    lv_obj_set_style_text_color(s_best, GOLD, 0);
    s_keys = lv_label_create(left);
    lv_label_set_recolor(s_keys, true);
    lv_obj_set_style_text_font(s_keys, FONT(10), 0);
    lv_obj_set_style_text_color(s_keys, lv_color_make(0xD8, 0xD8, 0xD8), 0);

    /* right: the lines, then what the game is */
    s_right = column(K(12) + pw + K(10), K(COL_Y), K(480 - 12) - (K(12) + pw + K(10)), K(COL_H));
    lv_obj_set_style_pad_row(s_right, K(ROW_GAP), 0);
    for(int i = 0; i < MAX_ROWS; i++) {
        lv_obj_t * r = lv_button_create(s_right);
        s_row[i] = r;
        lv_obj_set_size(r, LV_PCT(100), K(ROW_H));
        lv_obj_set_style_radius(r, K(4), 0);
        lv_obj_set_style_shadow_width(r, 0, 0);
        lv_obj_set_style_pad_hor(r, K(7), 0);
        lv_obj_set_style_pad_ver(r, 0, 0);
        lv_obj_add_event_cb(r, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_row_label[i] = lv_label_create(r);
        lv_label_set_recolor(s_row_label[i], true);
        lv_label_set_text(s_row_label[i], "");
        lv_obj_set_style_text_font(s_row_label[i], FONT(12), 0);
        lv_obj_set_style_text_color(s_row_label[i], lv_color_white(), 0);
        lv_obj_align(s_row_label[i], LV_ALIGN_LEFT_MID, 0, 0);
        s_row_value[i] = lv_label_create(r);
        lv_label_set_text(s_row_value[i], "");
        lv_obj_set_style_text_font(s_row_value[i], FONT(11), 0);
        lv_obj_set_style_text_color(s_row_value[i], GOLD, 0);
        lv_obj_align(s_row_value[i], LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_hidden(r, true);
    }
    s_about = lv_label_create(s_right);
    lv_label_set_long_mode(s_about, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_about, LV_PCT(100));
    lv_obj_set_style_text_font(s_about, FONT(10), 0);
    lv_obj_set_style_text_color(s_about, lv_color_make(0xB8, 0xBC, 0xB8), 0);
    lv_obj_set_style_pad_top(s_about, K(3), 0);

    lv_obj_t * hint = lv_label_create(s_panel);
    lv_label_set_text(hint, "Up / down: choose    Enter: do it    " LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT
                            ": change a setting    Esc: back to the menu");
    lv_obj_set_style_text_font(hint, FONT(10), 0);
    lv_obj_set_style_text_color(hint, DIM, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, K(12), K(-5));
}

void page_open(const page_t * p)
{
    s_p = p;
    lv_label_set_text(s_head, p->name);
    lv_label_set_text(s_about, p->about);
    lv_label_set_text(s_keys, p->keys);
    lv_obj_set_hidden(s_best, p->best == NULL);
    lv_obj_set_hidden(s_pic_box, p->preview == NULL);
    lv_obj_set_hidden(s_panel, false);
    s_sel = 0;
    s_rows_shown = ~0u;
    page_refresh();
    page_pic_update();
}

void page_hide(void)
{
    if(s_panel) lv_obj_set_hidden(s_panel, true);
}

bool page_key(int k)
{
    if(!s_p) return false;
    if(k == APP_KEY_UP) move(-1);
    else if(k == APP_KEY_DOWN) move(1);
    else if(k == APP_KEY_LEFT) act(s_sel, -1);
    else if(k == APP_KEY_RIGHT) act(s_sel, 1);
    else if(k == APP_KEY_ENTER || k == APP_KEY_SPACE) act(s_sel, 0);
    else {
        for(int i = 0; i < s_p->n; i++)
            if(s_p->rows[i].key == k && shown(i)) {
                s_sel = i;
                act(i, 0);
                return true;
            }
        return false;
    }
    return true;
}
