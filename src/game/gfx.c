#include "gfx.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

gfx_backdrop_t gfx_backdrop;
gfx_text_t gfx_text_queue[GFX_TEXT_MAX];
int gfx_text_count;

/* ------------------------------------------------------------------ */
/* Colors                                                              */
/* ------------------------------------------------------------------ */

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

gfx_color_t gfx_rgba(int r, int g, int b, float alpha)
{
    int a = (int)(clampf(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return GFX_RGBA(clampi(r, 0, 255), clampi(g, 0, 255), clampi(b, 0, 255), a);
}

gfx_color_t gfx_with_alpha(gfx_color_t c, float alpha)
{
    uint32_t a = c >> 24;
    a = (uint32_t)((float)a * clampf(alpha, 0.0f, 1.0f) + 0.5f);
    return (c & 0x00FFFFFFu) | (a << 24);
}

static float hue_to_rgb(float p, float q, float t)
{
    if(t < 0) t += 1;
    if(t > 1) t -= 1;
    if(t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if(t < 0.5f) return q;
    if(t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

gfx_color_t gfx_hsl(float h, float s, float l)
{
    h = fmodf(h, 360.0f);
    if(h < 0) h += 360.0f;
    h /= 360.0f;
    float r, g, b;
    if(s <= 0.0f) {
        r = g = b = l;
    }
    else {
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
        float p = 2 * l - q;
        r = hue_to_rgb(p, q, h + 1.0f / 3.0f);
        g = hue_to_rgb(p, q, h);
        b = hue_to_rgb(p, q, h - 1.0f / 3.0f);
    }
    return GFX_RGB((int)(r * 255.0f + 0.5f), (int)(g * 255.0f + 0.5f), (int)(b * 255.0f + 0.5f));
}

/* Source-over blend of `c` (straight alpha) onto opaque destination pixel
 * row[x] of row y, with `cov` (0..255) as extra coverage/global alpha
 * multiplier. */
static inline void blend_px(uint32_t * row, int x, int y, gfx_color_t c, uint32_t cov)
{
    uint32_t * dst = &row[x];
    uint32_t a = ((c >> 24) * cov + 127) / 255;
    if(a == 0) return;
    if(a >= 255) {
        *dst = 0xFF000000u | (c & 0x00FFFFFFu);
        return;
    }
    uint32_t d = gfx_under(*dst, x, y);
    uint32_t ia = 255 - a;
    uint32_t r = (((c >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * ia + 127) / 255;
    uint32_t g = (((c >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * ia + 127) / 255;
    uint32_t b = ((c & 0xFF) * a + (d & 0xFF) * ia + 127) / 255;
    *dst = 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* blend_px over row[x0..x1) for one solid colour, a = its effective alpha
 * (1..254, as blend_px works it out). With a column mask m, only pixels
 * with y < m[x] are touched. The same integer result as blend_px: for
 * 0 <= v < 65535, v / 255 == (v + 1 + (v >> 8)) >> 8. */
static void blend_span_solid(uint32_t * row, int x0, int x1, gfx_color_t c, uint32_t a, const int16_t * m, int y)
{
    uint32_t ia = 255 - a;
    uint32_t kr = ((c >> 16) & 0xFF) * a + 127, kg = ((c >> 8) & 0xFF) * a + 127, kb = (c & 0xFF) * a + 127;
    int x = x0;
#if defined(__ARM_NEON)
    const uint8x8_t via = vdup_n_u8((uint8_t)ia);
    const uint16x8_t vkr = vdupq_n_u16((uint16_t)kr), vkg = vdupq_n_u16((uint16_t)kg), vkb = vdupq_n_u16((uint16_t)kb);
    const uint16x8_t one = vdupq_n_u16(1);
    const int16x8_t vy = vdupq_n_s16((int16_t)y);
    const bool bd = gfx_backdrop.px != 0;
    for(; x + 8 <= x1; x += 8) {
        if(bd) {
            /* Undrawn background under these lanes: put the real pixels in
             * first (only where the blend writes). */
            const uint32x4_t key = vdupq_n_u32(GFX_KEY);
            uint32x4_t k = vorrq_u32(vceqq_u32(vld1q_u32(row + x), key), vceqq_u32(vld1q_u32(row + x + 4), key));
            if(vgetq_lane_u64(vreinterpretq_u64_u32(k), 0) | vgetq_lane_u64(vreinterpretq_u64_u32(k), 1)) {
                for(int i = x; i < x + 8; i++)
                    if(!(m && y >= m[i])) row[i] = gfx_under(row[i], i, y);
            }
        }
        uint8x8x4_t d = vld4_u8((const uint8_t *)(row + x));
        uint16x8_t tb = vmlal_u8(vkb, d.val[0], via);
        uint16x8_t tg = vmlal_u8(vkg, d.val[1], via);
        uint16x8_t tr = vmlal_u8(vkr, d.val[2], via);
        uint8x8x4_t o;
        o.val[0] = vshrn_n_u16(vaddq_u16(vaddq_u16(tb, one), vshrq_n_u16(tb, 8)), 8);
        o.val[1] = vshrn_n_u16(vaddq_u16(vaddq_u16(tg, one), vshrq_n_u16(tg, 8)), 8);
        o.val[2] = vshrn_n_u16(vaddq_u16(vaddq_u16(tr, one), vshrq_n_u16(tr, 8)), 8);
        o.val[3] = vdup_n_u8(0xFF);
        if(m) {
            /* keep the old pixel where y >= m[x] */
            uint8x8_t keep = vmovn_u16(vcgeq_s16(vy, vld1q_s16(m + x)));
            if(vget_lane_u64(vreinterpret_u64_u8(keep), 0) == ~0ull) continue;
            for(int k = 0; k < 4; k++) o.val[k] = vbsl_u8(keep, d.val[k], o.val[k]);
        }
        vst4_u8((uint8_t *)(row + x), o);
    }
#endif
    for(; x < x1; x++) {
        if(m && y >= m[x]) continue;
        uint32_t d = gfx_under(row[x], x, y);
        uint32_t r = (kr + ((d >> 16) & 0xFF) * ia) / 255;
        uint32_t g = (kg + ((d >> 8) & 0xFF) * ia) / 255;
        uint32_t b = (kb + (d & 0xFF) * ia) / 255;
        row[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

static inline gfx_color_t lerp_color(gfx_color_t c0, gfx_color_t c1, float t)
{
    t = clampf(t, 0.0f, 1.0f);
    int a = (int)((float)(c0 >> 24) + ((float)(c1 >> 24) - (float)(c0 >> 24)) * t + 0.5f);
    int r = (int)((float)((c0 >> 16) & 0xFF) + ((float)((c1 >> 16) & 0xFF) - (float)((c0 >> 16) & 0xFF)) * t + 0.5f);
    int g = (int)((float)((c0 >> 8) & 0xFF) + ((float)((c1 >> 8) & 0xFF) - (float)((c0 >> 8) & 0xFF)) * t + 0.5f);
    int b = (int)((float)(c0 & 0xFF) + ((float)(c1 & 0xFF) - (float)(c0 & 0xFF)) * t + 0.5f);
    return GFX_RGBA(r, g, b, a);
}

static gfx_color_t gradient_color(const gfx_paint_t * p, float t)
{
    if(p->stop_count == 0) return p->color;
    if(t <= p->stop_pos[0]) return p->stop_color[0];
    for(int i = 1; i < p->stop_count; i++) {
        if(t <= p->stop_pos[i]) {
            float span = p->stop_pos[i] - p->stop_pos[i - 1];
            float u = span > 0 ? (t - p->stop_pos[i - 1]) / span : 1.0f;
            return lerp_color(p->stop_color[i - 1], p->stop_color[i], u);
        }
    }
    return p->stop_color[p->stop_count - 1];
}

static inline gfx_color_t paint_at(const gfx_paint_t * p, float dx, float dy)
{
    switch(p->type) {
        case GFX_PAINT_SOLID:
            return p->color;
        case GFX_PAINT_LINEAR: {
            float vx = p->x1 - p->x0, vy = p->y1 - p->y0;
            float len2 = vx * vx + vy * vy;
            float t = len2 > 0 ? ((dx - p->x0) * vx + (dy - p->y0) * vy) / len2 : 0;
            return gradient_color(p, t);
        }
        case GFX_PAINT_RADIAL: {
            float ex = dx - p->x0, ey = dy - p->y0;
            float d = sqrtf(ex * ex + ey * ey);
            float span = p->r1 - p->r0;
            float t = span > 0 ? (d - p->r0) / span : 1.0f;
            return gradient_color(p, t);
        }
    }
    return p->color;
}

/* Fill [x0, x1) on row y with the paint. cov is 0..255 (global alpha * coverage). */
static inline bool row_mine(const gfx_ctx_t * g, int y)
{
    return y >= g->y_lo && y < g->y_hi && (g->row_step == 1 || y % g->row_step == g->row_phase);
}

/* First row >= y (y >= 0) that this context owns. */
static inline int first_row(const gfx_ctx_t * g, int y) { return gfx_first_row(g, y); }

/* Clamps an exclusive row end to what this context writes. */
static inline int row_end(const gfx_ctx_t * g, int y1) { return y1 < g->y_hi ? y1 : g->y_hi; }

/* The column mask for row y: NULL if the row is unmasked; `*skip` if the
 * whole row is masked. */
static inline const int16_t * row_mask(const gfx_ctx_t * g, int y, bool * skip)
{
    *skip = false;
    if(g->col_mask == NULL || y < g->col_mask_min) return NULL;
    if(y >= g->col_mask_max) *skip = true;
    return g->col_mask;
}

static void fill_span_masked(gfx_ctx_t * g, int y, int x0, int x1, const gfx_paint_t * paint, uint32_t cov,
                             const int16_t * m)
{
    uint32_t * row = g->px + (size_t)y * (size_t)g->stride_px;
    if(paint->type == GFX_PAINT_SOLID) {
        uint32_t a = ((paint->color >> 24) * cov + 127) / 255;
        if(a == 0) return;
        if(a < 255) {
            blend_span_solid(row, x0, x1, paint->color, a, m, y);
            return;
        }
    }
    float fy = (float)y + 0.5f;
    for(int x = x0; x < x1; x++) {
        if(y >= m[x]) continue;
        gfx_color_t c = paint->type == GFX_PAINT_SOLID ? paint->color : paint_at(paint, (float)x + 0.5f, fy);
        blend_px(row, x, y, c, cov);
    }
}

static void fill_span(gfx_ctx_t * g, int y, int x0, int x1, const gfx_paint_t * paint, uint32_t cov)
{
    if(y < 0 || y >= g->h || !row_mine(g, y)) return;
    if(x0 < 0) x0 = 0;
    if(x1 > g->w) x1 = g->w;
    if(x0 >= x1 || cov == 0) return;

    bool skip;
    const int16_t * m = row_mask(g, y, &skip);
    if(skip) return;
    if(m) {
        fill_span_masked(g, y, x0, x1, paint, cov, m);
        return;
    }

    uint32_t * row = g->px + (size_t)y * (size_t)g->stride_px;
    if(paint->type == GFX_PAINT_SOLID) {
        gfx_color_t c = paint->color;
        uint32_t a = ((c >> 24) * cov + 127) / 255;
        if(a >= 255) {
            uint32_t v = 0xFF000000u | (c & 0x00FFFFFFu);
            for(int x = x0; x < x1; x++) row[x] = v;
        }
        else if(a > 0) {
            blend_span_solid(row, x0, x1, c, a, NULL, y);
        }
    }
    else {
        float fy = (float)y + 0.5f;
        for(int x = x0; x < x1; x++) {
            gfx_color_t c = paint_at(paint, (float)x + 0.5f, fy);
            blend_px(row, x, y, c, cov);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Context / transform                                                 */
/* ------------------------------------------------------------------ */

static const gfx_mat_t k_identity = { 1, 0, 0, 1, 0, 0 };

void gfx_begin(gfx_ctx_t * g, uint32_t * pixels, int w, int h, int stride_px)
{
    memset(g, 0, sizeof(*g));
    g->px = pixels;
    g->w = w;
    g->h = h;
    g->stride_px = stride_px;
    g->m = k_identity;
    g->global_alpha = 1.0f;
    g->fill.type = GFX_PAINT_SOLID;
    g->fill.color = GFX_RGB(0, 0, 0);
    g->stroke.type = GFX_PAINT_SOLID;
    g->stroke.color = GFX_RGB(0, 0, 0);
    g->line_width = 1.0f;
    g->rng_state = 0x12345678u;
    g->row_phase = 0;
    g->row_step = 1;
    g->y_lo = 0;
    g->y_hi = h;
    g->emit_text = true;
}

void gfx_set_band(gfx_ctx_t * g, int y_lo, int y_hi)
{
    g->y_lo = y_lo < 0 ? 0 : y_lo;
    g->y_hi = y_hi > g->h ? g->h : y_hi;
}

void gfx_set_rows(gfx_ctx_t * g, int phase, int step, bool emit_text)
{
    g->row_step = step < 1 ? 1 : step;
    g->row_phase = phase % g->row_step;
    g->emit_text = emit_text;
}

void gfx_save(gfx_ctx_t * g)
{
    if(g->sp < GFX_STACK_MAX) g->stack[g->sp] = g->m;
    g->sp++;
}

void gfx_restore(gfx_ctx_t * g)
{
    if(g->sp <= 0) return;
    g->sp--;
    if(g->sp < GFX_STACK_MAX) g->m = g->stack[g->sp];
}

void gfx_reset_transform(gfx_ctx_t * g) { g->m = k_identity; }

void gfx_translate(gfx_ctx_t * g, float tx, float ty)
{
    g->m.e += g->m.a * tx + g->m.c * ty;
    g->m.f += g->m.b * tx + g->m.d * ty;
}

void gfx_rotate(gfx_ctx_t * g, float rad)
{
    float cs = cosf(rad), sn = sinf(rad);
    gfx_mat_t m = g->m;
    g->m.a = m.a * cs + m.c * sn;
    g->m.b = m.b * cs + m.d * sn;
    g->m.c = -m.a * sn + m.c * cs;
    g->m.d = -m.b * sn + m.d * cs;
}

void gfx_scale(gfx_ctx_t * g, float sx, float sy)
{
    g->m.a *= sx;
    g->m.b *= sx;
    g->m.c *= sy;
    g->m.d *= sy;
}

void gfx_set_global_alpha(gfx_ctx_t * g, float alpha) { g->global_alpha = clampf(alpha, 0, 1); }

float gfx_current_scale(const gfx_ctx_t * g)
{
    return sqrtf(fabsf(g->m.a * g->m.d - g->m.b * g->m.c));
}

void gfx_user_to_device(const gfx_ctx_t * g, float x, float y, float * dx, float * dy)
{
    *dx = g->m.a * x + g->m.c * y + g->m.e;
    *dy = g->m.b * x + g->m.d * y + g->m.f;
}

static inline bool is_axis_aligned(const gfx_ctx_t * g)
{
    return fabsf(g->m.b) < 1e-5f && fabsf(g->m.c) < 1e-5f;
}

static inline uint32_t cov_from_alpha(const gfx_ctx_t * g)
{
    return (uint32_t)(g->global_alpha * 255.0f + 0.5f);
}

/* ------------------------------------------------------------------ */
/* Styles                                                              */
/* ------------------------------------------------------------------ */

void gfx_set_fill(gfx_ctx_t * g, gfx_color_t color)
{
    g->fill.type = GFX_PAINT_SOLID;
    g->fill.color = color;
    g->fill.stop_count = 0;
}

void gfx_set_stroke(gfx_ctx_t * g, gfx_color_t color)
{
    g->stroke.type = GFX_PAINT_SOLID;
    g->stroke.color = color;
    g->stroke.stop_count = 0;
}

void gfx_set_line_width(gfx_ctx_t * g, float width) { g->line_width = width; }

void gfx_set_fill_linear(gfx_ctx_t * g, float x0, float y0, float x1, float y1)
{
    g->fill.type = GFX_PAINT_LINEAR;
    g->fill.stop_count = 0;
    gfx_user_to_device(g, x0, y0, &g->fill.x0, &g->fill.y0);
    gfx_user_to_device(g, x1, y1, &g->fill.x1, &g->fill.y1);
}

void gfx_set_fill_radial(gfx_ctx_t * g, float cx, float cy, float r0, float r1)
{
    g->fill.type = GFX_PAINT_RADIAL;
    g->fill.stop_count = 0;
    gfx_user_to_device(g, cx, cy, &g->fill.x0, &g->fill.y0);
    float s = gfx_current_scale(g);
    g->fill.r0 = r0 * s;
    g->fill.r1 = r1 * s;
}

void gfx_add_stop(gfx_ctx_t * g, float pos, gfx_color_t color)
{
    if(g->fill.stop_count >= GFX_MAX_STOPS) return;
    g->fill.stop_pos[g->fill.stop_count] = pos;
    g->fill.stop_color[g->fill.stop_count] = color;
    g->fill.stop_count++;
}

/* ------------------------------------------------------------------ */
/* Polygon scanline fill (even-odd), device space                      */
/* ------------------------------------------------------------------ */

/* With `cover` non-NULL nothing is drawn: instead cover[x] receives, per
 * column, the last row that is *not* covered (see gfx_path_cover_below). The
 * spans are exactly the ones a fill would draw, so the mask matches pixel for
 * pixel. */
static void polygon_rows(gfx_ctx_t * g, const float * xs, const float * ys, int n,
                         const int * sub_start, int sub_n, const gfx_paint_t * paint, int16_t * cover);

static void fill_polygon_device(gfx_ctx_t * g, const float * xs, const float * ys, int n,
                                const int * sub_start, int sub_n, const gfx_paint_t * paint)
{
    polygon_rows(g, xs, ys, n, sub_start, sub_n, paint, NULL);
}

static void polygon_rows(gfx_ctx_t * g, const float * xs, const float * ys, int n,
                         const int * sub_start, int sub_n, const gfx_paint_t * paint, int16_t * cover)
{
    if(n < 3) return;
    uint32_t cov = cover ? 255 : cov_from_alpha(g);
    if(cov == 0) return;

    float ymin = ys[0], ymax = ys[0];
    for(int i = 1; i < n; i++) {
        if(ys[i] < ymin) ymin = ys[i];
        if(ys[i] > ymax) ymax = ys[i];
    }
    int y0 = (int)floorf(ymin);
    int y1 = (int)ceilf(ymax);
    if(y0 < 0) y0 = 0;
    if(y1 > g->h) y1 = g->h;
    int step = g->row_step;
    if(cover) {
        /* Every row, whatever this context owns: the mask is per column. */
        step = 1;
        int top = y0 < g->h ? y0 : g->h;
        for(int x = 0; x < g->w; x++) cover[x] = (int16_t)(top - 1);
        /* Rows below the path are not covered either. */
        if(y1 < g->h) {
            for(int x = 0; x < g->w; x++) cover[x] = (int16_t)(g->h - 1);
            return;
        }
    }
    else {
        y0 = first_row(g, y0);
        y1 = row_end(g, y1);
    }
    if(y0 >= y1) return;
    bool skip_rest = false;
    /* Cover mode: a row without crossings leaves every column uncovered;
     * instead of writing all of them, remember the last such row and fold
     * it in at the end. Rows only go down, so each column's answer is the
     * larger of that row and its own last gap. */
    int all_free = -1;

    /* Crossings with their winding direction; nonzero rule like Canvas 2D. */
    float xs_cross[128];
    int8_t dir_cross[128];

    /* Long paths (the ground: hundreds of edges, most rows touch two or
     * three) keep a list of the edges that can reach the current row
     * instead of testing every edge on every row. The list stays in the
     * original edge order and each edge is tested exactly as below, so the
     * crossings -- including their order for equal x -- come out the same. */
    #define MAX_SPAN_ROWS 2048
    bool use_active = n > 24 && y1 - y0 + 2 <= MAX_SPAN_ROWS;
    int n_edges = 0, next_start = 0, n_active = 0;
    static _Thread_local struct { int16_t ia, ib; int first, last; } edges[GFX_PATH_MAX];
    static _Thread_local int16_t by_first[GFX_PATH_MAX], active[GFX_PATH_MAX];
    static _Thread_local int count[MAX_SPAN_ROWS + 1];
    if(use_active) {
        for(int s = 0; s < sub_n; s++) {
            int i0 = sub_start[s];
            int i1 = (s + 1 < sub_n) ? sub_start[s + 1] : n;
            int cnt = i1 - i0;
            if(cnt < 2) continue;
            for(int k = 0; k < cnt; k++) {
                int ia = i0 + k, ib = i0 + ((k + 1) % cnt);
                float lo = ys[ia] < ys[ib] ? ys[ia] : ys[ib], hi = ys[ia] < ys[ib] ? ys[ib] : ys[ia];
                if(!(lo < hi)) continue; /* flat (or NaN): never crossed */
                /* rows whose centre may lie in [lo, hi), with a row to spare
                 * each way; the exact test is still made per row */
                int first = (int)floorf(lo) - 1, last = (int)ceilf(hi) + 1;
                if(last < y0 || first >= y1) continue; /* misses the rows drawn here */
                edges[n_edges].ia = (int16_t)ia;
                edges[n_edges].ib = (int16_t)ib;
                edges[n_edges].first = first < y0 ? y0 : first;
                edges[n_edges].last = last;
                n_edges++;
            }
        }
        /* edge numbers by first row: a counting sort, stable, so ties keep
         * edge order */
        int nrows = y1 - y0 + 1;
        memset(count, 0, sizeof(count[0]) * (size_t)(nrows + 1));
        for(int e = 0; e < n_edges; e++) count[edges[e].first - y0 + 1]++;
        for(int r = 0; r < nrows; r++) count[r + 1] += count[r];
        for(int e = 0; e < n_edges; e++) by_first[count[edges[e].first - y0]++] = (int16_t)e;
    }

    for(int y = y0; y < y1 && !skip_rest; y += step) {
        float sy = (float)y + 0.5f;
        int nc = 0;

        if(use_active) {
            /* drop edges that ended above this row, add those that start */
            int k = 0;
            for(int i = 0; i < n_active; i++) if(edges[active[i]].last >= y) active[k++] = active[i];
            n_active = k;
            while(next_start < n_edges && edges[by_first[next_start]].first <= y) {
                int16_t e = by_first[next_start++];
                if(edges[e].last < y) continue;
                int j = n_active++;
                while(j > 0 && active[j - 1] > e) {
                    active[j] = active[j - 1];
                    j--;
                }
                active[j] = e;
            }
            for(int i = 0; i < n_active; i++) {
                int ia = edges[active[i]].ia, ib = edges[active[i]].ib;
                float ay = ys[ia], by = ys[ib];
                int8_t dir;
                if(sy >= ay && sy < by) dir = 1;
                else if(sy >= by && sy < ay) dir = -1;
                else continue;
                float t = (sy - ay) / (by - ay);
                float x = xs[ia] + (xs[ib] - xs[ia]) * t;
                if(nc < 128) {
                    xs_cross[nc] = x;
                    dir_cross[nc] = dir;
                    nc++;
                }
            }
        }
        else for(int s = 0; s < sub_n; s++) {
            int i0 = sub_start[s];
            int i1 = (s + 1 < sub_n) ? sub_start[s + 1] : n;
            int cnt = i1 - i0;
            if(cnt < 2) continue;
            for(int k = 0; k < cnt; k++) {
                int ia = i0 + k;
                int ib = i0 + ((k + 1) % cnt);
                float ay = ys[ia], by = ys[ib];
                int8_t dir;
                if(sy >= ay && sy < by) dir = 1;
                else if(sy >= by && sy < ay) dir = -1;
                else continue;
                float t = (sy - ay) / (by - ay);
                float x = xs[ia] + (xs[ib] - xs[ia]) * t;
                if(nc < 128) {
                    xs_cross[nc] = x;
                    dir_cross[nc] = dir;
                    nc++;
                }
            }
        }
        if(nc < 2) {
            if(cover) all_free = y;
            continue;
        }
        if(!cover && g->col_mask && y >= g->col_mask_max) skip_rest = true;

        for(int i = 1; i < nc; i++) {
            float v = xs_cross[i];
            int8_t d = dir_cross[i];
            int j = i - 1;
            while(j >= 0 && xs_cross[j] > v) {
                xs_cross[j + 1] = xs_cross[j];
                dir_cross[j + 1] = dir_cross[j];
                j--;
            }
            xs_cross[j + 1] = v;
            dir_cross[j + 1] = d;
        }

        int pos = 0; /* cover mode: columns before pos are accounted for */
        int winding = 0;
        for(int i = 0; i + 1 < nc; i++) {
            winding += dir_cross[i];
            if(winding == 0) continue;
            int xa = (int)floorf(xs_cross[i] + 0.5f);
            int xb = (int)floorf(xs_cross[i + 1] + 0.5f);
            if(xb <= xa) xb = xa + 1; /* keep hairline-thin slivers visible */
            if(cover) {
                /* the clamping fill_span would apply; spans arrive left to
                 * right, so everything between them is a gap */
                if(xa < 0) xa = 0;
                if(xb > g->w) xb = g->w;
                if(xa >= xb) continue;
                for(int x = pos; x < xa; x++) cover[x] = (int16_t)y;
                if(xb > pos) pos = xb;
            }
            else {
                fill_span(g, y, xa, xb, paint, cov);
            }
        }
        if(cover) {
            for(int x = pos; x < g->w; x++) cover[x] = (int16_t)y;
        }
    }
    if(cover && all_free >= 0) {
        for(int x = 0; x < g->w; x++) if(cover[x] < all_free) cover[x] = (int16_t)all_free;
    }
}

void gfx_path_cover_below(gfx_ctx_t * g, int16_t * out)
{
    polygon_rows(g, g->px_x, g->px_y, g->path_n, g->subpath_start, g->subpath_n, &g->fill, out);
    /* out[x] is the last uncovered row; the mask starts one below it. If the
     * path never reached the rows, polygon_rows left h - 1 (nothing masked). */
    for(int x = 0; x < g->w; x++) out[x] = (int16_t)(out[x] + 1);
}

void gfx_set_col_mask(gfx_ctx_t * g, const int16_t * mask)
{
    g->col_mask = mask;
    if(mask == NULL) return;
    int lo = g->h, hi = 0;
    for(int x = 0; x < g->w; x++) {
        if(mask[x] < lo) lo = mask[x];
        if(mask[x] > hi) hi = mask[x];
    }
    g->col_mask_min = lo;
    g->col_mask_max = hi;
}

/* Convex quad helper (used for thick line segments). */
static void fill_quad_device(gfx_ctx_t * g, float x0, float y0, float x1, float y1,
                             float x2, float y2, float x3, float y3, const gfx_paint_t * paint)
{
    float xs[4] = { x0, x1, x2, x3 };
    float ys[4] = { y0, y1, y2, y3 };
    int start = 0;
    fill_polygon_device(g, xs, ys, 4, &start, 1, paint);
}

/* ------------------------------------------------------------------ */
/* Circles / ellipses (analytic anti-aliased edge, device space)       */
/* ------------------------------------------------------------------ */

static void fill_ellipse_device(gfx_ctx_t * g, float cx, float cy, float rx, float ry, const gfx_paint_t * paint)
{
    uint32_t cov = cov_from_alpha(g);
    if(cov == 0 || rx <= 0 || ry <= 0) return;

    int y0 = (int)floorf(cy - ry - 1);
    int y1 = (int)ceilf(cy + ry + 1);
    int x0 = (int)floorf(cx - rx - 1);
    int x1 = (int)ceilf(cx + rx + 1);
    if(y0 < 0) y0 = 0;
    if(y1 > g->h) y1 = g->h;
    if(x0 < 0) x0 = 0;
    if(x1 > g->w) x1 = g->w;
    y0 = first_row(g, y0);
    y1 = row_end(g, y1);

    float rmin = rx < ry ? rx : ry;
    bool solid = paint->type == GFX_PAINT_SOLID;
    gfx_color_t sc = paint->color;

    /* Coverage is a = 0.5 - (sqrt(q) - 1) * rmin, clamped to [0, 1]. Where it
     * is certainly 1 or certainly 0 there is no need to evaluate it per pixel:
     * a >= 1 inside sqrt(q) <= kin, a <= 0 outside sqrt(q) >= kout. Both
     * ranges are found per row analytically and pulled in by a pixel of
     * margin, so every pixel still gets exactly the value the per-pixel
     * formula gives -- inside that is full coverage, `cov`. */
    const float kin = 1.0f - 0.5f / rmin;
    const float kout = 1.0f + 0.5f / rmin;

    for(int y = y0; y < y1; y += g->row_step) {
        float dy = ((float)y + 0.5f - cy) / ry;
        float dy2 = dy * dy;
        float to = kout * kout - dy2;
        if(to <= 0) continue; /* the whole row is outside */
        float ho = sqrtf(to) * rx;
        int ox0 = (int)floorf(cx - ho - 0.5f) - 1;
        int ox1 = (int)ceilf(cx + ho - 0.5f) + 2;
        if(ox0 < x0) ox0 = x0;
        if(ox1 > x1) ox1 = x1;
        int ix0 = ox1, ix1 = ox1; /* empty interior */
        float ti = kin * kin - dy2;
        if(kin > 0 && ti > 0) {
            float hi = sqrtf(ti) * rx;
            ix0 = (int)ceilf(cx - hi - 0.5f) + 1;
            ix1 = (int)floorf(cx + hi - 0.5f);
            if(ix0 < ox0) ix0 = ox0;
            if(ix1 > ox1) ix1 = ox1;
            if(ix1 < ix0) ix1 = ix0;
        }
        bool skip;
        const int16_t * m = row_mask(g, y, &skip);
        if(skip) continue;
        uint32_t * row = g->px + (size_t)y * (size_t)g->stride_px;
        for(int x = ox0; x < ox1; x++) {
            if(x == ix0 && ix1 > ix0 && solid) {
                uint32_t a = ((sc >> 24) * cov + 127) / 255;
                if(a >= 255) {
                    uint32_t v = 0xFF000000u | (sc & 0x00FFFFFFu);
                    for(; x < ix1; x++) if(!m || y < m[x]) row[x] = v;
                }
                else {
                    if(a > 0) blend_span_solid(row, x, ix1, sc, a, m, y);
                    x = ix1;
                }
                if(x >= ox1) break;
            }
            if(x == ix0 && ix1 > ix0) {
                for(; x < ix1; x++) {
                    if(m && y >= m[x]) continue;
                    gfx_color_t col = solid ? sc : paint_at(paint, (float)x + 0.5f, (float)y + 0.5f);
                    blend_px(row, x, y, col, cov);
                }
                if(x >= ox1) break;
            }
            if(m && y >= m[x]) continue;
            float dx = ((float)x + 0.5f - cx) / rx;
            float q = dx * dx + dy2;
            /* signed distance approximation to the ellipse edge, in pixels */
            float d = (sqrtf(q) - 1.0f) * rmin;
            float a = 0.5f - d;
            if(a <= 0) continue;
            if(a > 1) a = 1;
            uint32_t c = (uint32_t)(a * (float)cov + 0.5f);
            gfx_color_t col = solid ? sc : paint_at(paint, (float)x + 0.5f, (float)y + 0.5f);
            blend_px(row, x, y, col, c);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Immediate shapes                                                    */
/* ------------------------------------------------------------------ */

void gfx_fill_rect(gfx_ctx_t * g, float x, float y, float w, float h)
{
    if(is_axis_aligned(g)) {
        float dx0, dy0, dx1, dy1;
        gfx_user_to_device(g, x, y, &dx0, &dy0);
        gfx_user_to_device(g, x + w, y + h, &dx1, &dy1);
        if(dx1 < dx0) { float t = dx0; dx0 = dx1; dx1 = t; }
        if(dy1 < dy0) { float t = dy0; dy0 = dy1; dy1 = t; }
        int ix0 = (int)floorf(dx0 + 0.5f), ix1 = (int)floorf(dx1 + 0.5f);
        int iy0 = (int)floorf(dy0 + 0.5f), iy1 = (int)floorf(dy1 + 0.5f);
        if(ix1 <= ix0) ix1 = ix0 + 1; /* sub-pixel rects still leave a mark */
        if(iy1 <= iy0) iy1 = iy0 + 1;
        uint32_t cov = cov_from_alpha(g);
        if(iy0 < g->y_lo) iy0 = g->y_lo;
        iy1 = row_end(g, iy1);
        for(int yy = iy0; yy < iy1; yy++) fill_span(g, yy, ix0, ix1, &g->fill, cov);
        return;
    }
    float xs[4], ys[4];
    gfx_user_to_device(g, x, y, &xs[0], &ys[0]);
    gfx_user_to_device(g, x + w, y, &xs[1], &ys[1]);
    gfx_user_to_device(g, x + w, y + h, &xs[2], &ys[2]);
    gfx_user_to_device(g, x, y + h, &xs[3], &ys[3]);
    int start = 0;
    fill_polygon_device(g, xs, ys, 4, &start, 1, &g->fill);
}

void gfx_fill_rect_device(gfx_ctx_t * g, int x, int y, int w, int h, gfx_color_t color)
{
    gfx_paint_t p = { .type = GFX_PAINT_SOLID, .color = color };
    uint32_t cov = cov_from_alpha(g);
    int y0 = y < g->y_lo ? g->y_lo : y, y1 = row_end(g, y + h);
    for(int yy = y0; yy < y1; yy++) fill_span(g, yy, x, x + w, &p, cov);
}

void gfx_draw_sprite(gfx_ctx_t * g, int x, int y, int w, int h, const uint32_t * argb)
{
    int ya = y < 0 ? 0 : y, yb = row_end(g, y + h > g->h ? g->h : y + h);
    int xa = x < 0 ? 0 : x, xb = x + w > g->w ? g->w : x + w;
    for(int yy = first_row(g, ya); yy < yb; yy += g->row_step) {
        const uint32_t * src = argb + (size_t)(yy - y) * (size_t)w;
        uint32_t * row = g->px + (size_t)yy * (size_t)g->stride_px;
        for(int xx = xa; xx < xb; xx++) {
            uint32_t c = src[xx - x];
            if((c >> 24) && !(g->col_mask && yy >= g->col_mask[xx])) blend_px(row, xx, yy, c, 255);
        }
    }
}

void gfx_blit_pm(gfx_ctx_t * g, int x, int y, int w, int h, const uint32_t * pm)
{
    int ya = y < 0 ? 0 : y, yb = row_end(g, y + h > g->h ? g->h : y + h);
    int xa = x < 0 ? 0 : x, xb = x + w > g->w ? g->w : x + w;
    for(int yy = first_row(g, ya); yy < yb; yy += g->row_step) {
        const uint32_t * src = pm + (size_t)(yy - y) * (size_t)w - x;
        uint32_t * row = g->px + (size_t)yy * (size_t)g->stride_px;
        for(int xx = xa; xx < xb; xx++) {
            uint32_t c = src[xx], a = c >> 24;
            if(a == 0) continue;
            if(a == 255) {
                row[xx] = c;
                continue;
            }
            uint32_t d = gfx_under(row[xx], xx, yy), ia = 255 - a;
            uint32_t r = ((c >> 16) & 0xFF) + (((d >> 16) & 0xFF) * ia + 127) / 255;
            uint32_t gg = ((c >> 8) & 0xFF) + (((d >> 8) & 0xFF) * ia + 127) / 255;
            uint32_t b = (c & 0xFF) + ((d & 0xFF) * ia + 127) / 255;
            row[xx] = 0xFF000000u | (r > 255 ? 255 : r) << 16 | (gg > 255 ? 255 : gg) << 8 | (b > 255 ? 255 : b);
        }
    }
}

void gfx_vgradient_device(gfx_ctx_t * g, int y_top, int y_bottom, gfx_color_t top, gfx_color_t bottom,
                          float t_start, float t_end)
{
    if(y_top < 0) y_top = 0;
    if(y_bottom > g->h) y_bottom = g->h;
    int rows = y_bottom - y_top;
    if(rows <= 0) return;
    int end = row_end(g, y_bottom); /* the gradient still spans y_top..y_bottom */
    for(int y = first_row(g, y_top); y < end; y += g->row_step) {
        float t = t_start + (t_end - t_start) * ((float)(y - y_top) / (float)rows);
        gfx_color_t c = lerp_color(top, bottom, t);
        uint32_t v = 0xFF000000u | (c & 0x00FFFFFFu);
        uint32_t * row = g->px + (size_t)y * (size_t)g->stride_px;
        bool skip;
        const int16_t * m = row_mask(g, y, &skip);
        if(skip) break; /* rows only go down, and every later row is masked too */
        /* A partly masked row is filled whole: the masked pixels are exactly
         * the ones the opaque ground fill paints over afterwards, so testing
         * each of them only costs time. */
        (void)m;
        for(int x = 0; x < g->w; x++) row[x] = v;
    }
}

void gfx_fill_circle(gfx_ctx_t * g, float cx, float cy, float r)
{
    float dx, dy;
    gfx_user_to_device(g, cx, cy, &dx, &dy);
    float rd = r * gfx_current_scale(g);
    if(rd < 0.5f) rd = 0.5f;
    fill_ellipse_device(g, dx, dy, rd, rd, &g->fill);
}

static int ellipse_segments(float rd)
{
    int n = (int)(rd * 2.0f);
    if(n < 8) n = 8;
    if(n > 64) n = 64;
    return n;
}

void gfx_fill_ellipse(gfx_ctx_t * g, float cx, float cy, float rx, float ry, float rotation)
{
    if(is_axis_aligned(g) && fabsf(rotation) < 1e-4f) {
        float dx, dy;
        gfx_user_to_device(g, cx, cy, &dx, &dy);
        float rxd = rx * fabsf(g->m.a), ryd = ry * fabsf(g->m.d);
        if(rxd < 0.5f) rxd = 0.5f;
        if(ryd < 0.5f) ryd = 0.5f;
        fill_ellipse_device(g, dx, dy, rxd, ryd, &g->fill);
        return;
    }
    gfx_begin_path(g);
    gfx_path_ellipse(g, cx, cy, rx, ry, rotation);
    gfx_fill(g);
}

void gfx_stroke_ellipse(gfx_ctx_t * g, float cx, float cy, float rx, float ry, float rotation)
{
    gfx_begin_path(g);
    gfx_path_ellipse(g, cx, cy, rx, ry, rotation);
    gfx_stroke(g);
}

void gfx_stroke_arc(gfx_ctx_t * g, float cx, float cy, float r, float a0, float a1)
{
    gfx_begin_path(g);
    gfx_path_arc(g, cx, cy, r, a0, a1, false);
    gfx_stroke(g);
}

/* Anti-aliased 1px device-space line (Xiaolin Wu). `strength` scales the
 * coverage so sub-pixel widths fade out like they do when the original
 * 1920x1080 canvas is scaled down (a 1px line becomes a quarter-strength
 * line at 0.25). */
static inline void plot_aa(gfx_ctx_t * g, int x, int y, float c, const gfx_paint_t * paint, uint32_t cov)
{
    if(x < 0 || x >= g->w || y < 0 || y >= g->h || c <= 0.002f || !row_mine(g, y)) return;
    if(g->col_mask && y >= g->col_mask[x]) return;
    uint32_t a = (uint32_t)(c * (float)cov + 0.5f);
    gfx_color_t col = paint_at(paint, (float)x + 0.5f, (float)y + 0.5f);
    blend_px(g->px + (size_t)y * (size_t)g->stride_px, x, y, col, a);
}

static void hairline_device(gfx_ctx_t * g, float x0, float y0, float x1, float y1, float strength,
                            const gfx_paint_t * paint)
{
    uint32_t cov = (uint32_t)((float)cov_from_alpha(g) * strength + 0.5f);
    if(cov == 0) return;
    bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);
    if(steep) { float t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if(x0 > x1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    float dx = x1 - x0, dy = y1 - y0;
    float grad = dx == 0 ? 1.0f : dy / dx;

    int xend = (int)floorf(x0 + 0.5f);
    float yend = y0 + grad * ((float)xend - x0);
    float xgap = 1.0f - (x0 + 0.5f - floorf(x0 + 0.5f));
    int xpx1 = xend;
    int ypx1 = (int)floorf(yend);
    float fy = yend - floorf(yend);
    if(steep) { plot_aa(g, ypx1, xpx1, (1 - fy) * xgap, paint, cov); plot_aa(g, ypx1 + 1, xpx1, fy * xgap, paint, cov); }
    else      { plot_aa(g, xpx1, ypx1, (1 - fy) * xgap, paint, cov); plot_aa(g, xpx1, ypx1 + 1, fy * xgap, paint, cov); }
    float intery = yend + grad;

    xend = (int)floorf(x1 + 0.5f);
    yend = y1 + grad * ((float)xend - x1);
    xgap = (x1 + 0.5f) - floorf(x1 + 0.5f);
    int xpx2 = xend;
    int ypx2 = (int)floorf(yend);
    fy = yend - floorf(yend);
    if(steep) { plot_aa(g, ypx2, xpx2, (1 - fy) * xgap, paint, cov); plot_aa(g, ypx2 + 1, xpx2, fy * xgap, paint, cov); }
    else      { plot_aa(g, xpx2, ypx2, (1 - fy) * xgap, paint, cov); plot_aa(g, xpx2, ypx2 + 1, fy * xgap, paint, cov); }

    /* Only the rows this context writes; intery still advances every step,
     * so the pixels that are drawn come out exactly as before. */
    const float lo = (float)g->y_lo - 1.0f, hi = (float)g->y_hi;
    for(int x = xpx1 + 1; x < xpx2; x++) {
        if(steep ? ((float)x < lo || (float)x >= hi) : (intery < lo || intery >= hi)) {
            intery += grad;
            continue;
        }
        int iy = (int)floorf(intery);
        float f = intery - floorf(intery);
        if(steep) { plot_aa(g, iy, x, 1 - f, paint, cov); plot_aa(g, iy + 1, x, f, paint, cov); }
        else      { plot_aa(g, x, iy, 1 - f, paint, cov); plot_aa(g, x, iy + 1, f, paint, cov); }
        intery += grad;
    }
}

static void stroke_segment_device(gfx_ctx_t * g, float x0, float y0, float x1, float y1, float wd,
                                  const gfx_paint_t * paint)
{
    /* Wholly above or below the rows this context writes: nothing to do
     * (half the width plus the anti-aliasing and rounding margin). */
    float m = wd * 0.5f + 2.0f;
    if((y0 > y1 ? y0 : y1) + m < (float)g->y_lo || (y0 < y1 ? y0 : y1) - m >= (float)g->y_hi) return;
    if(wd <= 1.2f) {
        float strength = wd < 0.3f ? 0.3f : (wd > 1.0f ? 1.0f : wd);
        hairline_device(g, x0, y0, x1, y1, strength, paint);
        return;
    }
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if(len < 1e-4f) {
        fill_ellipse_device(g, x0, y0, wd * 0.5f, wd * 0.5f, paint);
        return;
    }
    float nx = -dy / len * wd * 0.5f, ny = dx / len * wd * 0.5f;
    fill_quad_device(g, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, paint);
}

void gfx_line(gfx_ctx_t * g, float x1, float y1, float x2, float y2)
{
    float ax, ay, bx, by;
    gfx_user_to_device(g, x1, y1, &ax, &ay);
    gfx_user_to_device(g, x2, y2, &bx, &by);
    float wd = g->line_width * gfx_current_scale(g);
    stroke_segment_device(g, ax, ay, bx, by, wd, &g->stroke);
    if(wd > 2.5f) {
        fill_ellipse_device(g, ax, ay, wd * 0.5f, wd * 0.5f, &g->stroke);
        fill_ellipse_device(g, bx, by, wd * 0.5f, wd * 0.5f, &g->stroke);
    }
}

void gfx_dashed_line(gfx_ctx_t * g, float x1, float y1, float x2, float y2, float dash, float gap)
{
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if(len < 1e-4f) return;
    float ux = dx / len, uy = dy / len;
    float pos = 0;
    while(pos < len) {
        float end = pos + dash;
        if(end > len) end = len;
        gfx_line(g, x1 + ux * pos, y1 + uy * pos, x1 + ux * end, y1 + uy * end);
        pos = end + gap;
    }
}

void gfx_stroke_rect(gfx_ctx_t * g, float x, float y, float w, float h)
{
    gfx_line(g, x, y, x + w, y);
    gfx_line(g, x + w, y, x + w, y + h);
    gfx_line(g, x + w, y + h, x, y + h);
    gfx_line(g, x, y + h, x, y);
}

/* ------------------------------------------------------------------ */
/* Paths                                                               */
/* ------------------------------------------------------------------ */

void gfx_begin_path(gfx_ctx_t * g)
{
    g->path_n = 0;
    g->subpath_n = 0;
    g->has_current_point = false;
}

static void path_push(gfx_ctx_t * g, float x, float y)
{
    if(g->path_n >= GFX_PATH_MAX) return;
    if(g->subpath_n == 0) {
        g->subpath_start[0] = 0;
        g->subpath_n = 1;
    }
    gfx_user_to_device(g, x, y, &g->px_x[g->path_n], &g->px_y[g->path_n]);
    g->path_n++;
    g->has_current_point = true;
}

void gfx_move_to(gfx_ctx_t * g, float x, float y)
{
    if(g->path_n > 0 && g->subpath_n < 64) {
        g->subpath_start[g->subpath_n++] = g->path_n;
    }
    path_push(g, x, y);
}

void gfx_line_to(gfx_ctx_t * g, float x, float y) { path_push(g, x, y); }

void gfx_quad_to(gfx_ctx_t * g, float cx, float cy, float x, float y)
{
    if(g->path_n == 0) {
        path_push(g, cx, cy);
    }
    /* Start point in user space: invert the last device point (cheap: we
     * keep it by re-evaluating from the parametric curve with the device
     * coordinates of the start). */
    float sx = g->px_x[g->path_n - 1], sy = g->px_y[g->path_n - 1];
    float cdx, cdy, edx, edy;
    gfx_user_to_device(g, cx, cy, &cdx, &cdy);
    gfx_user_to_device(g, x, y, &edx, &edy);
    const int steps = 8;
    for(int i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float it = 1.0f - t;
        float px = it * it * sx + 2 * it * t * cdx + t * t * edx;
        float py = it * it * sy + 2 * it * t * cdy + t * t * edy;
        if(g->path_n < GFX_PATH_MAX) {
            g->px_x[g->path_n] = px;
            g->px_y[g->path_n] = py;
            g->path_n++;
        }
    }
}

void gfx_path_arc(gfx_ctx_t * g, float cx, float cy, float r, float a0, float a1, bool anticlockwise)
{
    float sweep = a1 - a0;
    if(!anticlockwise) {
        while(sweep < 0) sweep += 2.0f * (float)M_PI;
        if(sweep > 2.0f * (float)M_PI) sweep = 2.0f * (float)M_PI;
    }
    else {
        while(sweep > 0) sweep -= 2.0f * (float)M_PI;
        if(sweep < -2.0f * (float)M_PI) sweep = -2.0f * (float)M_PI;
    }
    float rd = r * gfx_current_scale(g);
    int n = ellipse_segments(rd);
    n = (int)((float)n * fabsf(sweep) / (2.0f * (float)M_PI)) + 2;
    for(int i = 0; i <= n; i++) {
        float a = a0 + sweep * (float)i / (float)n;
        path_push(g, cx + cosf(a) * r, cy + sinf(a) * r);
    }
}

/* cos and sin of 2 pi i / n for every segment count ellipse_segments()
 * gives, worked out once with the same expression the loop used, so paths
 * come out the same without two libm calls per point. */
#define UNIT_MIN 8
#define UNIT_MAX 64
static float s_unit_cos[UNIT_MAX + 1][UNIT_MAX + 1], s_unit_sin[UNIT_MAX + 1][UNIT_MAX + 1];

__attribute__((constructor)) static void unit_circle_init(void)
{
    for(int n = UNIT_MIN; n <= UNIT_MAX; n++) {
        for(int i = 0; i <= n; i++) {
            float a = 2.0f * (float)M_PI * (float)i / (float)n;
            s_unit_cos[n][i] = cosf(a);
            s_unit_sin[n][i] = sinf(a);
        }
    }
}

void gfx_path_ellipse(gfx_ctx_t * g, float cx, float cy, float rx, float ry, float rotation)
{
    float rd = (rx > ry ? rx : ry) * gfx_current_scale(g);
    /* In band mode, an ellipse whose rows lie wholly outside the band --
     * with room for any stroke -- adds nothing a fill or stroke would draw
     * there, so it is left out. Every caller builds a path of whole
     * ellipses only, so no later segment joins onto it. */
    if(g->y_lo > 0 || g->y_hi < g->h) {
        float r = rx > ry ? rx : ry;
        float dcx, dcy;
        gfx_user_to_device(g, cx, cy, &dcx, &dcy);
        float ext = (fabsf(g->m.b) + fabsf(g->m.d)) * r + g->line_width * gfx_current_scale(g) + 24.0f;
        if(dcy + ext < (float)g->y_lo || dcy - ext >= (float)g->y_hi) return;
    }
    int n = ellipse_segments(rd);
    float cr = cosf(rotation), sr = sinf(rotation);
    if(g->path_n > 0 && g->subpath_n < 64) g->subpath_start[g->subpath_n++] = g->path_n;
    const float * uc = s_unit_cos[n], * us = s_unit_sin[n];
    for(int i = 0; i <= n; i++) { /* <= n: repeat the first point so strokes close */
        float ex = uc[i] * rx, ey = us[i] * ry;
        path_push(g, cx + ex * cr - ey * sr, cy + ex * sr + ey * cr);
    }
}

void gfx_close_path(gfx_ctx_t * g) { (void)g; /* subpaths are implicitly closed on fill */ }

void gfx_fill(gfx_ctx_t * g)
{
    fill_polygon_device(g, g->px_x, g->px_y, g->path_n, g->subpath_start, g->subpath_n, &g->fill);
}

void gfx_stroke(gfx_ctx_t * g)
{
    float wd = g->line_width * gfx_current_scale(g);
    for(int s = 0; s < g->subpath_n; s++) {
        int i0 = g->subpath_start[s];
        int i1 = (s + 1 < g->subpath_n) ? g->subpath_start[s + 1] : g->path_n;
        for(int i = i0; i + 1 < i1; i++) {
            stroke_segment_device(g, g->px_x[i], g->px_y[i], g->px_x[i + 1], g->px_y[i + 1], wd, &g->stroke);
            if(wd > 2.5f && i + 2 < i1) {
                fill_ellipse_device(g, g->px_x[i + 1], g->px_y[i + 1], wd * 0.5f, wd * 0.5f, &g->stroke);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Text queue                                                          */
/* ------------------------------------------------------------------ */

void gfx_text_clear(void) { gfx_text_count = 0; }

void gfx_fill_text(gfx_ctx_t * g, const char * text, float x, float y, float size_px, int align,
                   gfx_color_t color, gfx_color_t outline)
{
    if(!g->emit_text) return;
    gfx_text_t * t;
    if(g->text_q) {
        if(g->text_n >= g->text_max) return;
        t = &g->text_q[g->text_n];
    }
    else {
        if(gfx_text_count >= GFX_TEXT_MAX) return;
        t = &gfx_text_queue[gfx_text_count];
    }
    gfx_user_to_device(g, x, y, &t->x, &t->y);
    if(t->x < -200 || t->x > (float)g->w + 200 || t->y < -50 || t->y > (float)g->h + 50) return;
    snprintf(t->text, sizeof(t->text), "%s", text);
    t->size_px = size_px * gfx_current_scale(g);
    t->color = gfx_with_alpha(color, g->global_alpha);
    t->outline = outline;
    t->align = align;
    if(g->text_q) g->text_n++;
    else gfx_text_count++;
}

/* ------------------------------------------------------------------ */
/* PRNG                                                                */
/* ------------------------------------------------------------------ */

float gfx_randf(gfx_ctx_t * g)
{
    g->rng_state = g->rng_state * 1664525u + 1013904223u;
    return (float)(g->rng_state >> 8) / 16777216.0f;
}
