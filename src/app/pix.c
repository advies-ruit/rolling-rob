/*
 * The shared canvas of the app's small games and their drawing (pix.h).
 */

#include "pix.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t * s_canvas;
static lv_draw_buf_t * s_buf;
static pix_t s_pix;
static bool s_ok;
static uint32_t * s_bg;
static void (*s_input)(lv_event_code_t code, int x, int y);

static void canvas_event(lv_event_t * e)
{
    if(!s_input) return;
    lv_indev_t * in = lv_indev_active();
    if(in == NULL) return;
    lv_point_t pt;
    lv_indev_get_point(in, &pt);
    s_input(lv_event_get_code(e), pt.x, pt.y);
}

void pix_stage_create(lv_obj_t * parent, int w, int h)
{
    s_canvas = lv_canvas_create(parent);
    s_buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_XRGB8888, LV_STRIDE_AUTO);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_set_hidden(s_canvas, true);
    if(s_buf == NULL) return;
    s_pix.px = (uint32_t *)s_buf->data;
    s_pix.w = w;
    s_pix.h = h;
    s_pix.stride = (int)(s_buf->header.stride / 4);
    s_bg = malloc((size_t)s_pix.stride * h * 4);
    if(s_bg == NULL) return;
    s_ok = true;
    pix_noclip(&s_pix);
    lv_canvas_set_draw_buf(s_canvas, s_buf);
    lv_obj_set_clickable(s_canvas, true);
    static const lv_event_code_t codes[] = { LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED,
                                             LV_EVENT_SHORT_CLICKED, LV_EVENT_LONG_PRESSED };
    for(size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
        lv_obj_add_event_cb(s_canvas, canvas_event, codes[i], NULL);
}

pix_t * pix_stage(void) { return s_ok ? &s_pix : NULL; }
uint32_t * pix_stage_bg(void) { return s_ok ? s_bg : NULL; }

void pix_stage_show(bool on)
{
    if(s_canvas) lv_obj_set_hidden(s_canvas, !on);
}

void pix_stage_set_input(void (*cb)(lv_event_code_t code, int x, int y)) { s_input = cb; }

void pix_flush(int x, int y, int w, int h)
{
    if(!s_canvas || w <= 0 || h <= 0) return;
    lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    lv_obj_invalidate_area(s_canvas, &a);
}

void pix_flush_all(void)
{
    if(s_canvas) lv_obj_invalidate(s_canvas);
}

void pix_clip(pix_t * p, int x, int y, int w, int h)
{
    p->cx1 = x < 0 ? 0 : x;
    p->cy1 = y < 0 ? 0 : y;
    p->cx2 = x + w > p->w ? p->w : x + w;
    p->cy2 = y + h > p->h ? p->h : y + h;
}

void pix_noclip(pix_t * p)
{
    p->cx1 = p->cy1 = 0;
    p->cx2 = p->w;
    p->cy2 = p->h;
}

uint32_t pix_mix(uint32_t a, uint32_t b, int t)
{
    uint32_t r = (((a >> 16) & 255) * (uint32_t)(256 - t) + ((b >> 16) & 255) * (uint32_t)t) >> 8;
    uint32_t g = (((a >> 8) & 255) * (uint32_t)(256 - t) + ((b >> 8) & 255) * (uint32_t)t) >> 8;
    uint32_t bl = ((a & 255) * (uint32_t)(256 - t) + (b & 255) * (uint32_t)t) >> 8;
    return 0xFF000000u | r << 16 | g << 8 | bl;
}

/* x, y, w, h clipped; false if nothing is left */
static bool clip(const pix_t * p, int * x, int * y, int * w, int * h)
{
    int x2 = *x + *w, y2 = *y + *h;
    if(*x < p->cx1) *x = p->cx1;
    if(*y < p->cy1) *y = p->cy1;
    if(x2 > p->cx2) x2 = p->cx2;
    if(y2 > p->cy2) y2 = p->cy2;
    *w = x2 - *x;
    *h = y2 - *y;
    return *w > 0 && *h > 0;
}

void pix_rect(pix_t * p, int x, int y, int w, int h, uint32_t rgb)
{
    if(!clip(p, &x, &y, &w, &h)) return;
    uint32_t c = 0xFF000000u | rgb;
    for(int j = 0; j < h; j++) {
        uint32_t * row = p->px + (size_t)(y + j) * p->stride + x;
        for(int i = 0; i < w; i++) row[i] = c;
    }
}

void pix_rect_over(pix_t * p, int x, int y, int w, int h, uint32_t rgb, int t)
{
    if(!clip(p, &x, &y, &w, &h)) return;
    for(int j = 0; j < h; j++) {
        uint32_t * row = p->px + (size_t)(y + j) * p->stride + x;
        for(int i = 0; i < w; i++) row[i] = pix_mix(row[i], rgb, t);
    }
}

void pix_block(pix_t * p, int x, int y, int w, int h, uint32_t rgb)
{
    int s = w < h ? w : h;
    int b = s / 7 < 1 ? 1 : s / 7;
    pix_rect(p, x, y, w, h, rgb);
    pix_rect(p, x, y, w, b, pix_mix(rgb, 0xFFFFFF, 110));
    pix_rect(p, x, y + b, b, h - b, pix_mix(rgb, 0xFFFFFF, 70));
    pix_rect(p, x + w - b, y + b, b, h - b, pix_mix(rgb, 0x000000, 90));
    pix_rect(p, x + b, y + h - b, w - 2 * b, b, pix_mix(rgb, 0x000000, 120));
}

void pix_disc(pix_t * p, float cx, float cy, float r, uint32_t rgb)
{
    int x0 = (int)floorf(cx - r - 1), y0 = (int)floorf(cy - r - 1);
    int x1 = (int)ceilf(cx + r + 1), y1 = (int)ceilf(cy + r + 1);
    if(x0 < p->cx1) x0 = p->cx1;
    if(y0 < p->cy1) y0 = p->cy1;
    if(x1 > p->cx2) x1 = p->cx2;
    if(y1 > p->cy2) y1 = p->cy2;
    for(int y = y0; y < y1; y++) {
        uint32_t * row = p->px + (size_t)y * p->stride;
        float dy = (float)y + 0.5f - cy;
        for(int x = x0; x < x1; x++) {
            float dx = (float)x + 0.5f - cx;
            float cover = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if(cover <= 0) continue;
            row[x] = cover >= 1 ? 0xFF000000u | rgb : pix_mix(row[x], rgb, (int)(cover * 256));
        }
    }
}

/* 5x7, a row per byte, bit 4 the left column */
static const uint8_t k_digits[10][7] = {
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
};

void pix_digit(pix_t * p, int x, int y, int scale, int digit, uint32_t rgb)
{
    if(digit < 0 || digit > 9) return;
    for(int r = 0; r < 7; r++)
        for(int c = 0; c < 5; c++)
            if(k_digits[digit][r] & (0x10 >> c)) pix_rect(p, x + c * scale, y + r * scale, scale, scale, rgb);
}

void pix_restore(pix_t * p, int x, int y, int w, int h)
{
    if(!clip(p, &x, &y, &w, &h)) return;
    for(int j = 0; j < h; j++) {
        size_t o = (size_t)(y + j) * p->stride + x;
        memcpy(p->px + o, s_bg + o, (size_t)w * 4);
    }
}

void pix_bg_gradient(uint32_t top, uint32_t bottom)
{
    if(!s_ok) return;
    for(int y = 0; y < s_pix.h; y++) {
        uint32_t c = pix_mix(top, bottom, y * 256 / s_pix.h);
        uint32_t * row = s_bg + (size_t)y * s_pix.stride;
        for(int x = 0; x < s_pix.w; x++) row[x] = c;
    }
}
