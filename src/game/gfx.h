#ifndef ROLLING_ROB_GFX_H
#define ROLLING_ROB_GFX_H

/*
 * gfx: a tiny software rasterizer that mimics the subset of the HTML5
 * Canvas 2D API used by rolling-rob.html (save/restore, translate/rotate/
 * scale, fillRect, arc/ellipse, paths, strokes, linear/radial gradients,
 * globalAlpha, rgba colors). It writes straight into an ARGB8888 pixel
 * buffer (the LVGL canvas draw buffer), which keeps the whole game frame
 * cheap enough for a processor without a GPU and lets the draw
 * code in draw.c stay a near line-by-line port of the original.
 *
 * Coordinates are floats in "user space"; the current transform maps them
 * to device pixels. Text is not rasterized here: gfx_fill_text() queues
 * the request and the platform layer renders it with an LVGL font.
 */

#include <stdint.h>
#include <stdbool.h>

typedef uint32_t gfx_color_t; /* 0xAARRGGBB, straight (non-premultiplied) alpha */

#define GFX_RGB(r, g, b)     (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define GFX_RGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define GFX_HEX(rgb)         (0xFF000000u | ((uint32_t)(rgb) & 0xFFFFFFu))

/* Backdrop: with a hardware layer under a colour-keyed screen (the art's
 * painted background on a video layer, see art.h), the background is not
 * drawn: its pixels hold GFX_KEY, which reaches the screen as RGB565 0xF81F,
 * the key colour. Anything blended onto such a pixel must mix with the real
 * background, so every blend reads the pixel under it through gfx_under(),
 * which looks the background up here. The frame then comes out identical to
 * one with the background drawn in, except that untouched pixels show the
 * hardware layer. px == NULL: off (the normal case). Set once per frame,
 * before any drawing, and only read while drawing. */
#define GFX_KEY 0xFFF800F8u
typedef struct {
    const uint16_t * px; /* RGB565 picture */
    int stride;          /* in pixels */
    int x0, y0;          /* where its top left pixel sits on the screen */
    int w, h;            /* rows and columns beyond these repeat the edge */
} gfx_backdrop_t;
extern gfx_backdrop_t gfx_backdrop;

static inline uint32_t gfx_under(uint32_t d, int x, int y)
{
    if(d != GFX_KEY || gfx_backdrop.px == 0) return d;
    int sx = x - gfx_backdrop.x0, sy = y - gfx_backdrop.y0;
    sx = sx < 0 ? 0 : sx >= gfx_backdrop.w ? gfx_backdrop.w - 1 : sx;
    sy = sy < 0 ? 0 : sy >= gfx_backdrop.h ? gfx_backdrop.h - 1 : sy;
    uint32_t p = gfx_backdrop.px[(long)sy * gfx_backdrop.stride + sx];
    return 0xFF000000u | (p & 0xF800) << 8 | (p & 0x07E0) << 5 | (p & 0x001F) << 3;
}

/* rgba(r,g,b,alpha) with alpha as a float in [0,1], like CSS. */
gfx_color_t gfx_rgba(int r, int g, int b, float alpha);
/* hsl(h, s%, l%) like CSS: h in degrees, s/l in [0,1]. */
gfx_color_t gfx_hsl(float h, float s, float l);
/* Returns `c` with its alpha multiplied by `alpha` in [0,1]. */
gfx_color_t gfx_with_alpha(gfx_color_t c, float alpha);

#define GFX_MAX_STOPS 4
#define GFX_PATH_MAX  600
#define GFX_STACK_MAX 16

typedef struct {
    float a, b, c, d, e, f; /* x' = a*x + c*y + e ; y' = b*x + d*y + f */
} gfx_mat_t;

typedef enum {
    GFX_PAINT_SOLID,
    GFX_PAINT_LINEAR,
    GFX_PAINT_RADIAL,
} gfx_paint_type_t;

typedef struct {
    gfx_paint_type_t type;
    gfx_color_t color;
    /* Gradient geometry in device space. Linear: p0 -> p1. Radial: center
     * (x0,y0), inner radius r0, outer radius r1 (concentric). */
    float x0, y0, x1, y1, r0, r1;
    int stop_count;
    float stop_pos[GFX_MAX_STOPS];
    gfx_color_t stop_color[GFX_MAX_STOPS];
} gfx_paint_t;

typedef struct {
    uint32_t * px;
    int w, h;
    int stride_px; /* in pixels, not bytes */

    gfx_mat_t m;
    gfx_mat_t stack[GFX_STACK_MAX];
    int sp;

    float global_alpha;
    gfx_paint_t fill;
    gfx_paint_t stroke;
    float line_width;

    /* Current path, already transformed to device space. */
    float px_x[GFX_PATH_MAX];
    float px_y[GFX_PATH_MAX];
    int path_n;
    int subpath_start[64];
    int subpath_n;
    bool has_current_point;

    uint32_t rng_state;

    /* Row ownership for parallel rendering: this context only writes rows
     * with y % row_step == row_phase. Every pixel belongs to exactly one
     * context and sees the same operations in the same order, so the result
     * is bit-identical to a single-threaded frame. Default 0 / 1 = all rows. */
    int row_phase, row_step;
    /* Band: this context only writes rows y_lo <= y < y_hi (default 0..h).
     * `px` still addresses the whole frame, so a band can live in a small
     * buffer by pointing px at (band - y_lo * stride). Coordinates, masks and
     * gradients all stay in whole-frame terms. */
    int y_lo, y_hi;
    /* Only one context of a parallel frame may queue text. */
    bool emit_text;
    /* When set, text is queued here (per context) instead of globally. */
    struct gfx_text_s * text_q;
    int text_n, text_max;

    /* Column mask: when set, nothing is written at y >= col_mask[x]. Used to
     * skip sky that the opaque ground will cover anyway. */
    const int16_t * col_mask;
    int col_mask_min, col_mask_max;
} gfx_ctx_t;

/* --- Context --- */
void gfx_begin(gfx_ctx_t * g, uint32_t * pixels, int w, int h, int stride_px);
/* Restrict this context to rows y % step == phase (see gfx_ctx_t). */
void gfx_set_rows(gfx_ctx_t * g, int phase, int step, bool emit_text);
/* Restrict this context to the band y_lo <= y < y_hi (see gfx_ctx_t). */
void gfx_set_band(gfx_ctx_t * g, int y_lo, int y_hi);

/* First row >= y this context writes, and the row after the last one. */
static inline int gfx_first_row(const gfx_ctx_t * g, int y)
{
    if(y < g->y_lo) y = g->y_lo;
    if(g->row_step <= 1) return y;
    return y + ((g->row_phase - y % g->row_step) + g->row_step) % g->row_step;
}
static inline int gfx_row_end(const gfx_ctx_t * g) { return g->y_hi; }
void gfx_save(gfx_ctx_t * g);
void gfx_restore(gfx_ctx_t * g);
void gfx_reset_transform(gfx_ctx_t * g);
void gfx_translate(gfx_ctx_t * g, float tx, float ty);
void gfx_rotate(gfx_ctx_t * g, float radians);
void gfx_scale(gfx_ctx_t * g, float sx, float sy);
void gfx_set_global_alpha(gfx_ctx_t * g, float alpha);
/* Device-space scale factor of the current transform (for radii/line widths). */
float gfx_current_scale(const gfx_ctx_t * g);
void gfx_user_to_device(const gfx_ctx_t * g, float x, float y, float * dx, float * dy);

/* --- Styles (like ctx.fillStyle / ctx.strokeStyle / ctx.lineWidth) --- */
void gfx_set_fill(gfx_ctx_t * g, gfx_color_t color);
void gfx_set_stroke(gfx_ctx_t * g, gfx_color_t color);
void gfx_set_line_width(gfx_ctx_t * g, float width);
/* Gradient fill styles; coordinates are user space (transformed at call time). */
void gfx_set_fill_linear(gfx_ctx_t * g, float x0, float y0, float x1, float y1);
void gfx_set_fill_radial(gfx_ctx_t * g, float cx, float cy, float r0, float r1);
void gfx_add_stop(gfx_ctx_t * g, float pos, gfx_color_t color);

/* --- Immediate shapes --- */
void gfx_fill_rect(gfx_ctx_t * g, float x, float y, float w, float h);
void gfx_stroke_rect(gfx_ctx_t * g, float x, float y, float w, float h);
void gfx_fill_circle(gfx_ctx_t * g, float cx, float cy, float r);
void gfx_fill_ellipse(gfx_ctx_t * g, float cx, float cy, float rx, float ry, float rotation);
void gfx_stroke_ellipse(gfx_ctx_t * g, float cx, float cy, float rx, float ry, float rotation);
/* Stroked arc, angles in radians like ctx.arc(); clockwise from +x. */
void gfx_stroke_arc(gfx_ctx_t * g, float cx, float cy, float r, float a0, float a1);
void gfx_line(gfx_ctx_t * g, float x1, float y1, float x2, float y2);
/* Dashed line: dash/gap lengths in user units. */
void gfx_dashed_line(gfx_ctx_t * g, float x1, float y1, float x2, float y2, float dash, float gap);

/* --- Paths (beginPath / moveTo / lineTo / arc / ellipse / closePath / fill / stroke) --- */
void gfx_begin_path(gfx_ctx_t * g);
void gfx_move_to(gfx_ctx_t * g, float x, float y);
void gfx_line_to(gfx_ctx_t * g, float x, float y);
void gfx_quad_to(gfx_ctx_t * g, float cx, float cy, float x, float y);
void gfx_path_arc(gfx_ctx_t * g, float cx, float cy, float r, float a0, float a1, bool anticlockwise);
void gfx_path_ellipse(gfx_ctx_t * g, float cx, float cy, float rx, float ry, float rotation);
void gfx_close_path(gfx_ctx_t * g);
void gfx_fill(gfx_ctx_t * g);
/* For the current path as gfx_fill() would cover it (nonzero rule, full
 * coverage): per column, the first row from which every row down to the
 * bottom of the buffer is covered; `h` where there is none. Ignores the row
 * ownership, so every context gets the whole mask. `out` holds `w` entries. */
void gfx_path_cover_below(gfx_ctx_t * g, int16_t * out);
/* Sets (or with NULL clears) the column mask; see gfx_ctx_t. */
void gfx_set_col_mask(gfx_ctx_t * g, const int16_t * mask);
void gfx_stroke(gfx_ctx_t * g);

/* --- Screen-space helpers (ignore the transform) --- */
/* Source-over of a straight-alpha ARGB image at device (x, y). */
void gfx_draw_sprite(gfx_ctx_t * g, int x, int y, int w, int h, const uint32_t * argb);
void gfx_fill_rect_device(gfx_ctx_t * g, int x, int y, int w, int h, gfx_color_t color);
/* Source-over of a premultiplied ARGB image (stride = w) at device (x, y). */
void gfx_blit_pm(gfx_ctx_t * g, int x, int y, int w, int h, const uint32_t * pm);
void gfx_vgradient_device(gfx_ctx_t * g, int y_top, int y_bottom, gfx_color_t top, gfx_color_t bottom,
                          float t_start, float t_end);

/* --- Deferred text --- */
#define GFX_TEXT_MAX 24
typedef struct gfx_text_s {
    char text[24];
    float x, y;        /* device space; y = text baseline like ctx.fillText */
    float size_px;     /* requested font size in device pixels */
    gfx_color_t color;
    gfx_color_t outline; /* 0 = none */
    int align;         /* 0 = left, 1 = center */
} gfx_text_t;

extern gfx_text_t gfx_text_queue[GFX_TEXT_MAX];
extern int gfx_text_count;
void gfx_fill_text(gfx_ctx_t * g, const char * text, float x, float y, float size_px, int align,
                   gfx_color_t color, gfx_color_t outline);
void gfx_text_clear(void);

/* Cheap deterministic PRNG for per-frame visual jitter (asteroids, beam sparks). */
float gfx_randf(gfx_ctx_t * g);

#endif /* ROLLING_ROB_GFX_H */
