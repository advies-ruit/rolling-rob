#ifndef ROLLING_ROB_DRAW_H
#define ROLLING_ROB_DRAW_H

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"
#include "terrain.h"

/*
 * Frame rendering: a near line-by-line port of every draw*() function in
 * rolling-rob.html on top of the gfx rasterizer. game.c owns the state
 * and hands a read-only view of it to rr_draw_frame() once per frame.
 */

#define RR_CONFETTI_MAX   100
#define RR_FIREWORK_MAX   150
#define RR_EXPLOSION_MAX   90

typedef struct {
    float x, y, vx, vy;
    float rotation, rotation_speed;
    float size;
    gfx_color_t color;
    bool alive;
} rr_confetti_t;

typedef struct {
    float x, y, vx, vy;
    float life;
    float size;
    gfx_color_t color;
} rr_particle_t;

typedef struct {
    float x, y, vx, vy;
    float rotation;
    int32_t radius;
} rr_rob_view_t;

typedef struct {
    int32_t level;
    const rr_terrain_t * terrain;
    rr_rob_view_t rob;
    float camera_x, camera_y;
    bool started, over, won;
    bool rob_visible;
    float anim_ms;            /* monotonic clock for Date.now()-driven animation */

    bool charging;            /* pointer/touch held on the playfield */
    float charge_power;       /* 0..1 */

    /* On-screen touch controls (bottom corners). */
    bool touch_controls;
    bool btn_left, btn_right, btn_up, btn_jump;

    const rr_confetti_t * confetti;
    const rr_particle_t * fireworks;
    const rr_particle_t * explosion;
} rr_view_t;

/* Touch-button layout in screen pixels (also used by game.c for hit tests):
 * three round arrow buttons and one big rounded JUMP button. */
typedef struct { int cx, cy, r; } rr_touch_btn_t;
typedef struct { int x, y, w, h; } rr_touch_rect_t;
void rr_draw_touch_layout(int screen_w, int screen_h, rr_touch_btn_t * left, rr_touch_btn_t * right,
                          rr_touch_btn_t * up, rr_touch_rect_t * jump);

/* Which parts of the frame rr_draw_frame() draws -- all of them, except for
 * a development tool that draws them one at a time. */
#define RR_LAYER_BG      1u /* sky, or the art's picture */
#define RR_LAYER_WORLD   2u /* fixed in the world: ground, trees, mines, flags */
#define RR_LAYER_ACTORS  4u /* moving: Rob, monsters, saucers, Julian, effects */
#define RR_LAYER_OVERLAY 8u /* fixed on screen: charge meter, touch buttons */
#define RR_LAYER_ALL     15u
extern unsigned rr_draw_layers;

/* Renders one full frame into `g` (already begun on the canvas buffer).
 * `view_scale` = device pixels per world unit (0.25 for 480x272). Safe to
 * call from several threads at once on contexts owning disjoint rows. */
void rr_draw_frame(gfx_ctx_t * g, const rr_view_t * v, float view_scale);

/* Builds the per-screen-size caches (touch buttons). Single-threaded: call
 * before a frame is drawn in parallel. Set ROLLING_ROB_NO_BTN_CACHE to draw
 * the buttons directly instead. */
void rr_draw_prepare(int screen_w, int screen_h);

/* Computes the sky mask for this frame (see draw.c). Single-threaded: call
 * before rr_draw_frame() runs, once per frame; rr_draw_frame_parallel does. */
void rr_draw_sky_mask(uint32_t * px, int w, int h, int stride_px, const rr_view_t * v, float view_scale);

/* Draws a frame into `px` on up to four threads (ROLLING_ROB_THREADS=N
 * overrides; 1 = single-threaded). The pixels are identical either way. */
/* Preferred thread count, before the first frame; 0 = one per core. Every
 * thread walks the whole draw list, so more threads cost more CPU in total
 * and only pay off when a single core cannot keep up (60 fps on a small device). */
void rr_draw_set_threads(int n);
/* Threads the frames are drawn with (0 before the first frame). */
int rr_draw_threads(void);
void rr_draw_frame_parallel(uint32_t * px, int w, int h, int stride_px, const rr_view_t * v,
                            float view_scale);

/* Band rendering: the frame is drawn in bands of `band_rows` rows, each into
 * a small per-thread buffer that stays in the cache, and every finished band
 * goes to `sink` (rows y0 .. y0 + rows - 1, `stride_px` apart; the sink may
 * modify it). No full-frame buffer is ever written, so a sink can put the
 * band straight into the framebuffer. Pixels are identical to
 * rr_draw_frame_parallel(). Text is queued per band and drawn by `text`
 * (may be NULL) into the band before it reaches the sink. Bands are handed
 * out dynamically to the same threads as rr_draw_frame_parallel(). */
typedef void (*rr_band_sink_t)(uint32_t * band, int stride_px, int y0, int rows, void * user);
typedef void (*rr_band_text_t)(gfx_ctx_t * g, const gfx_text_t * items, int n);
/* Hands bands out bottom-up instead of top-down. */
void rr_draw_set_band_order(bool bottom_up);
void rr_draw_frame_bands(int w, int h, const rr_view_t * v, float view_scale, int band_rows,
                         rr_band_sink_t sink, rr_band_text_t text, void * user);

#endif /* ROLLING_ROB_DRAW_H */
