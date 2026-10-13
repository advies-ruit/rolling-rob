/*
 * The fractal explorer (see fractal_view.h).
 *
 * The picture is held as escape values (fractal.h), one per pixel at the
 * chosen resolution (the screen, or a half or a third of it, scaled back up
 * when drawn), with each pixel's "level": the grid step it was computed
 * or filled at (1 = exact). A new picture is computed in passes on grids of
 * 8, 4, 2 and 1 pixels; each sample fills the block of its step wherever
 * nothing finer is known yet, so the picture sharpens in place. A pass is
 * cut into rows, handed out in batches to all cores (r3_parallel) until the
 * frame's time budget is used.
 *
 * After a move the old values are carried along first: a pan by whole
 * pixels keeps them exact (only the new strip is computed), a zoom scales
 * them as placeholders, which the passes from the 4-grid on replace.
 */

#include "fractal_view.h"
#include "fractal.h"
#include "app_page.h"
#include "app.h"
#include "game_ui.h"
#include "r3d.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

/* Laid out for 480x272 and scaled to the screen, like the other games. */
static float s_sc = 1;
#define K(n) ((int)((float)(n) * s_sc + 0.5f))

#define LVL_PLACEHOLDER 6  /* carried along by a zoom: better than the 8-grid, worse than the 4-grid */
#define LVL_NONE 255

static int s_w, s_h;           /* the screen */
static int s_div = 1;          /* resolution: the screen divided by this */
static int s_iw, s_ih;         /* the picture */
static uint16_t * s_val, * s_old;
static uint8_t * s_lvl, * s_old_lvl;
static int * s_map_x, * s_map_y;
static bool s_shown, s_ready;
static void (*s_menu)(void);

static fr_view_t s_v;          /* scale: units per *screen* pixel */
static fr_view_t s_held;       /* the view the values belong to */
static fr_view_t s_mandel;     /* the Mandelbrot view to go back to from its Julia */
static bool s_have_mandel;
static double s_base[FR_KINDS];

/* progress */
static int s_step;             /* the pass's grid, 0: done */
static int s_prow;             /* its next row (in grid rows) */
static int s_batch_y, s_batch_n;
static int s_rx0, s_rx1, s_ry0, s_ry1; /* where the passes look: everything, or the strip a pan uncovered */
static double s_took_ms;       /* computing time of the picture so far */
static int s_threads = 1;
static double s_budget_ms = 8;
static double s_ms_compute, s_ms_moved, s_ms_render; /* per second, ROLLING_ROB_FPS */
static int64_t s_ns_scale, s_ns_sink; /* summed over the threads (atomic) */

/* moves */
static double s_goal;          /* the scale zoomed towards */
static double s_pan_x, s_pan_y; /* pending pan, in picture pixels */
static bool s_auto;            /* the demo zoom (the tour, below) is on */
static uint32_t s_z_held_ms, s_x_held_ms;
static uint32_t s_tap_until[4]; /* a tap moves like a hold of 150 ms: left, right, up, down */

/* colours */
static int s_pal;
static bool s_cycle = true;
static float s_offset;
static uint32_t s_lut[256];
static int s_iter_mode;        /* 0: by the zoom, else k_iters[] */
static const int k_iters[] = { 0, 256, 512, 1024, 2048, 4096, 8192 };
#define N_ITERS ((int)(sizeof(k_iters) / sizeof(k_iters[0])))

static lv_obj_t * s_ui, * s_info, * s_hint;

static double cpu_ms(void) /* this thread's CPU time */
{
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

/* ---------------------------------------------------------------- palettes */

typedef struct { float at; uint32_t rgb; } stop_t;
typedef struct { const char * name; int n; stop_t s[8]; } pal_t;
static const pal_t k_pals[] = {
    { "Classic", 6, { { 0, 0x000764 }, { 0.16f, 0x206BCB }, { 0.42f, 0xEDFFFF }, { 0.6425f, 0xFFAA00 }, { 0.8575f, 0x000200 }, { 1, 0x000764 } } },
    { "Fire", 6, { { 0, 0x000000 }, { 0.25f, 0x8A0A00 }, { 0.5f, 0xFF7A00 }, { 0.7f, 0xFFF06A }, { 0.85f, 0xFFFFFF }, { 1, 0x000000 } } },
    { "Ocean", 6, { { 0, 0x02123A }, { 0.3f, 0x0B6E99 }, { 0.5f, 0x7FE8E0 }, { 0.65f, 0xFFFFFF }, { 0.8f, 0xE0B04A }, { 1, 0x02123A } } },
    { "Rainbow", 7, { { 0, 0xFF0000 }, { 0.17f, 0xFFFF00 }, { 0.33f, 0x00FF00 }, { 0.5f, 0x00FFFF }, { 0.67f, 0x0000FF }, { 0.83f, 0xFF00FF }, { 1, 0xFF0000 } } },
    { "Ink", 3, { { 0, 0x101010 }, { 0.5f, 0xF4F0E6 }, { 1, 0x101010 } } },
};
#define N_PALS ((int)(sizeof(k_pals) / sizeof(k_pals[0])))

static void make_lut(void)
{
    const pal_t * p = &k_pals[s_pal];
    for(int i = 0; i < 256; i++) {
        float t = (float)i / 256.0f;
        int k = 0;
        while(k + 2 < p->n && t > p->s[k + 1].at) k++;
        const stop_t * a = &p->s[k], * b = &p->s[k + 1];
        float f = (t - a->at) / (b->at - a->at);
        if(f < 0) f = 0;
        if(f > 1) f = 1;
        uint32_t c = 0xFF000000u;
        for(int sh = 0; sh <= 16; sh += 8) {
            float ca = (float)((a->rgb >> sh) & 255), cb = (float)((b->rgb >> sh) & 255);
            c |= (uint32_t)(ca + (cb - ca) * f + 0.5f) << sh;
        }
        s_lut[i] = c;
    }
}

/* ---------------------------------------------------------------- the view */

static void start_view(fr_kind_t k)
{
    s_v.kind = k;
    double ww, hh;
    if(k == FR_MANDELBROT) {
        s_v.cx = -0.6;
        s_v.cy = 0;
        ww = 3.6;
        hh = 2.5;
    }
    else if(k == FR_JULIA) {
        s_v.cx = 0;
        s_v.cy = 0;
        ww = 3.4;
        hh = 2.2;
    }
    else {
        s_v.cx = -0.45;
        s_v.cy = 0.5;
        ww = 3.6;
        hh = 2.6;
    }
    s_v.scale = ww / s_w > hh / s_h ? ww / s_w : hh / s_h;
    s_base[k] = s_v.scale;
    s_goal = s_v.scale;
    s_pan_x = s_pan_y = 0;
}

static double zoom_of(void) { return s_base[s_v.kind] / s_v.scale; }

static int iterations_at(double zoom)
{
    if(s_iter_mode) return k_iters[s_iter_mode];
    int n = 200 + (int)(64.0 * log2(zoom > 1 ? zoom : 1));
    return n > 8192 ? 8192 : n;
}

static int iterations(void) { return iterations_at(zoom_of()); }

/* the view the computation uses: per picture pixel */
static fr_view_t pic_view(void)
{
    fr_view_t v = s_v;
    v.scale *= s_div;
    v.max_iter = iterations();
    return v;
}

/* Everything again, from the 8-grid. */
static void fresh(void)
{
    for(int i = 0; i < s_iw * s_ih; i++) {
        s_val[i] = FR_UNKNOWN;
        s_lvl[i] = LVL_NONE;
    }
    s_held = s_v;
    s_step = 8;
    s_prow = 0;
    s_took_ms = 0;
    s_rx0 = s_ry0 = 0;
    s_rx1 = s_iw;
    s_ry1 = s_ih;
}

/* The view moved or zoomed (same fractal, same iterations): carry the
 * values along, then compute what is missing. */
static bool s_same_scale;
static int s_map_x0, s_map_x1;
static int s_job_box[128][4]; /* per carry job: x0, x1, y0, y1 of the pixels not exact */
static void carry_job(int job, int tid);

static void moved(void)
{
    double os = s_held.scale * s_div, ns = s_v.scale * s_div;
    bool same_scale = os == ns;
    s_map_x0 = s_iw;
    s_map_x1 = 0;
    for(int x = 0; x < s_iw; x++) {
        double wx = s_v.cx + ((double)x - s_iw * 0.5 + 0.5) * ns;
        double ox = (wx - s_held.cx) / os + s_iw * 0.5 - 0.5;
        int o = (int)floor(ox + 0.5);
        s_map_x[x] = o >= 0 && o < s_iw ? o : -1;
        if(s_map_x[x] >= 0) {
            if(x < s_map_x0) s_map_x0 = x;
            s_map_x1 = x + 1;
        }
    }
    for(int y = 0; y < s_ih; y++) {
        double wy = s_v.cy - ((double)y - s_ih * 0.5 + 0.5) * ns;
        double oy = (s_held.cy - wy) / os + s_ih * 0.5 - 0.5;
        int o = (int)floor(oy + 0.5);
        s_map_y[y] = o >= 0 && o < s_ih ? o : -1;
    }
    /* the old values become the source: swap, no copy */
    uint16_t * tv = s_old;
    s_old = s_val;
    s_val = tv;
    uint8_t * tl = s_old_lvl;
    s_old_lvl = s_lvl;
    s_lvl = tl;
    s_same_scale = same_scale;
    r3_parallel((s_ih + 15) / 16, carry_job);
    /* the passes look only where something is not exact yet: after a pan
     * the strip it uncovered (and whatever was still being computed) */
    s_rx0 = s_iw;
    s_rx1 = 0;
    s_ry0 = s_ih;
    s_ry1 = 0;
    for(int j = 0; j < (s_ih + 15) / 16; j++) {
        if(s_job_box[j][0] >= s_job_box[j][1]) continue;
        if(s_job_box[j][0] < s_rx0) s_rx0 = s_job_box[j][0];
        if(s_job_box[j][1] > s_rx1) s_rx1 = s_job_box[j][1];
        if(s_job_box[j][2] < s_ry0) s_ry0 = s_job_box[j][2];
        if(s_job_box[j][3] > s_ry1) s_ry1 = s_job_box[j][3];
    }
    if(s_rx0 >= s_rx1) s_rx0 = s_rx1 = s_ry0 = s_ry1 = 0; /* nothing to do */
    s_held = s_v;
    s_step = same_scale ? 8 : 4; /* a pan: the new strip coarse first */
    s_prow = 0;
    s_took_ms = 0;
}

/* The columns that map into the old picture are one run (the map only
 * grows): unknown before and after it, a straight copy through the map in
 * between -- no tests per pixel. */

static void carry_job(int job, int tid)
{
    (void)tid;
    bool same_scale = s_same_scale;
    int ye = job * 16 + 16 < s_ih ? job * 16 + 16 : s_ih, xa = s_map_x0, xb = s_map_x1;
    const int * map = s_map_x;
    int bx0 = s_iw, bx1 = 0, by0 = s_ih, by1 = 0;
    for(int y = job * 16; y < ye; y++) {
        uint16_t * v = s_val + (size_t)y * s_iw;
        uint8_t * l = s_lvl + (size_t)y * s_iw;
        int oy = s_map_y[y];
        int a = oy < 0 ? s_iw : xa, b = oy < 0 ? s_iw : xb;
        for(int x = 0; x < a; x++) v[x] = FR_UNKNOWN;
        for(int x = b; x < s_iw; x++) v[x] = FR_UNKNOWN;
        memset(l, LVL_NONE, (size_t)a);
        memset(l + b, LVL_NONE, (size_t)(s_iw - b));
        int rx0 = s_iw, rx1 = 0; /* this row's pixels not exact */
        if(a > 0) {
            rx0 = 0;
            rx1 = a;
        }
        if(b < s_iw) {
            if(rx0 > b) rx0 = b;
            rx1 = s_iw;
        }
        if(a < b) {
            const uint16_t * ov = s_old + (size_t)oy * s_iw;
            const uint8_t * ol = s_old_lvl + (size_t)oy * s_iw;
            for(int x = a; x < b; x++) v[x] = ov[map[x]];
            if(same_scale) { /* a pan by whole pixels keeps them exact */
                for(int x = a; x < b; x++) l[x] = ol[map[x]];
                int f = a, e = b;
                while(f < e && l[f] == 1) f++;
                while(e > f && l[e - 1] == 1) e--;
                if(f < e) {
                    if(f < rx0) rx0 = f;
                    if(e > rx1) rx1 = e;
                }
            }
            else {
                for(int x = a; x < b; x++) l[x] = v[x] == FR_UNKNOWN ? LVL_NONE : LVL_PLACEHOLDER;
                rx0 = 0;
                rx1 = s_iw;
            }
        }
        if(rx0 < rx1) {
            if(rx0 < bx0) bx0 = rx0;
            if(rx1 > bx1) bx1 = rx1;
            if(y < by0) by0 = y;
            by1 = y + 1;
        }
    }
    s_job_box[job][0] = bx0;
    s_job_box[job][1] = bx1;
    s_job_box[job][2] = by0;
    s_job_box[job][3] = by1;
}

/* ---------------------------------------------------------------- computing */

static fr_view_t s_cv; /* the view the batch computes */

static void row_job(int job, int tid)
{
    (void)tid;
    int s = s_step, y = s_batch_y + job * s;
    if(y >= s_ry1) return;
    /* every point of this grid in the region not known exactly yet (the
     * coarser grids' points are, unless a move uncovered them) */
    int x0 = (s_rx0 + s - 1) / s * s, xs = s;
    uint16_t buf[64];
    int xpos[64];
    uint8_t * lrow = s_lvl + (size_t)y * s_iw;
    /* runs of samples that need computing: not known at this step or finer */
    int x = x0;
    while(x < s_rx1) {
        int n = 0;
        while(x < s_rx1 && n < 64) {
            if(lrow[x] > s) xpos[n++] = x;
            else if(n) break;
            x += xs;
        }
        if(n == 0) continue;
        fr_row(&s_cv, s_iw, s_ih, y, xpos[0], xs, n, buf);
        for(int k = 0; k < n; k++) {
            int px = xpos[k];
            uint16_t v = buf[k];
            int bw = px + s <= s_iw ? s : s_iw - px, bh = y + s <= s_ih ? s : s_ih - y;
            for(int by = 0; by < bh; by++) {
                uint16_t * vr = s_val + (size_t)(y + by) * s_iw + px;
                uint8_t * lr = s_lvl + (size_t)(y + by) * s_iw + px;
                for(int bx = 0; bx < bw; bx++)
                    if(lr[bx] > s) {
                        vr[bx] = v;
                        lr[bx] = (uint8_t)(bx == 0 && by == 0 ? 1 : s);
                    }
            }
            s_lvl[(size_t)y * s_iw + px] = 1;
            s_val[(size_t)y * s_iw + px] = v;
        }
    }
}

/* One batch of rows of the current pass; false when everything is done. */
static bool compute_batch(void)
{
    if(s_step == 0) return false;
    int first = (s_ry0 + s_step - 1) / s_step, rows = (s_ry1 + s_step - 1) / s_step - first;
    if(s_prow >= rows) {
        s_step /= 2;
        s_prow = 0;
        if(s_step == 0) {
            static int verbose = -1;
            if(verbose < 0) verbose = getenv("ROLLING_ROB_FPS") != NULL;
            fr_view_t v = pic_view();
            if(verbose)
                printf("fractal: %s, zoom %.3g, %dx%d, %d iterations, %s: %.0f ms computing\n", fr_kind_name(v.kind),
                       zoom_of(), s_iw, s_ih, v.max_iter, fr_uses_float(&v) ? "float" : "double", s_took_ms);
            return false;
        }
        first = (s_ry0 + s_step - 1) / s_step;
        rows = (s_ry1 + s_step - 1) / s_step - first;
    }
    int n = s_threads * (s_step >= 4 ? 8 : 2);
    if(n > rows - s_prow) n = rows - s_prow;
    s_cv = pic_view();
    s_batch_y = (first + s_prow) * s_step;
    s_batch_n = n;
    r3_parallel(n, row_job);
    s_prow += n;
    return true;
}

static void compute_for(double budget_ms)
{
    double t0 = now_ms(), t = t0;
    while(compute_batch()) {
        t = now_ms();
        if(t - t0 >= budget_ms) break;
    }
    s_took_ms += now_ms() - t0;
    s_ms_compute += now_ms() - t0;
}

/* ---------------------------------------------------------------- drawing */

/* ---------------------------------------------------------------- the tour
 *
 * The demo zoom, the way the 1993 demos did it (Crystal Dream 2): pictures
 * ("keyframes") at every 2x step along a fixed path, computed ahead, and in
 * between only scaled -- smoothly, at the display's rate. The middle of the
 * screen comes from the next keyframe (sharper), the rest from the current
 * one, magnified at most 2x with bilinear interpolation. The zoom never
 * passes a keyframe that is not ready; all of them are kept, so the way back
 * out and every later round are only scaling.
 *
 * From the Mandelbrot start view the path ends at a mini-Mandelbrot in the
 * seahorse valley (found by scanning the classic seahorse point: at ~3.6e5x
 * it fills half the screen); from anywhere else it zooms 2^16 into the middle.
 */

#define TOUR_MAX 26
#define MINI_X -0.74364297723185313 /* the mini-Mandelbrot's middle */
#define MINI_Y 0.13182636769104777

static uint16_t * s_kf[TOUR_MAX];
static int s_kf_n, s_kf_done, s_kf_row; /* keyframes on the path, computed, next row */
static fr_view_t s_tour;                /* the path: kind, centre, keyframe 0's scale per screen pixel */
static fr_view_t s_kfv;                 /* the keyframe being computed (per picture pixel) */
static int s_tour_iw, s_tour_ih, s_tour_iters;
static double s_tz, s_tour_end;         /* where on the path and its end, in doublings */
static int s_tour_dir = 1;
static float s_tour_hold;               /* seconds to stay at an end */
static bool s_tour_waiting;             /* for the next keyframe */
static int s_kf_batch_y;

static fr_view_t kf_view(int k)
{
    fr_view_t v = s_tour;
    double sc = s_tour.scale * ldexp(1.0, -k);
    v.scale = sc * s_div;
    v.max_iter = iterations_at(s_base[v.kind] / sc);
    return v;
}

static void kf_row_job(int job, int tid)
{
    (void)tid;
    int y = s_kf_batch_y + job;
    if(y < s_ih) fr_row(&s_kfv, s_iw, s_ih, y, 0, 1, s_iw, s_kf[s_kf_done] + (size_t)y * s_iw);
}

static void tour_compute(double budget_ms)
{
    double t0 = now_ms();
    while(s_kf_done < s_kf_n) {
        if(!s_kf[s_kf_done]) {
            s_kf[s_kf_done] = malloc(sizeof(uint16_t) * (size_t)s_iw * s_ih);
            if(!s_kf[s_kf_done]) {
                s_kf_n = s_kf_done; /* out of memory: the path ends here */
                break;
            }
        }
        if(s_kf_row == 0) s_kfv = kf_view(s_kf_done);
        int n = s_threads * 2;
        if(n > s_ih - s_kf_row) n = s_ih - s_kf_row;
        s_kf_batch_y = s_kf_row;
        r3_parallel(n, kf_row_job);
        s_kf_row += n;
        if(s_kf_row >= s_ih) {
            static int verbose = -1;
            if(verbose < 0) verbose = getenv("ROLLING_ROB_FPS") != NULL;
            if(verbose)
                printf("fractal: keyframe %d of %d (%d iterations, %s) done\n", s_kf_done + 1, s_kf_n, s_kfv.max_iter,
                       fr_uses_float(&s_kfv) ? "float" : "double");
            s_kf_done++;
            s_kf_row = 0;
        }
        if(now_ms() - t0 >= budget_ms) break;
    }
    s_ms_compute += now_ms() - t0;
}

static double limit_scale(void);
static void moved(void);

static void tour_start(void)
{
    fr_view_t t = s_v;
    double end = 16;
    bool at_start = fabs(s_v.scale - s_base[s_v.kind]) < s_base[s_v.kind] * 1e-9;
    if(s_v.kind == FR_MANDELBROT && at_start) {
        t.cx = MINI_X;
        t.cy = MINI_Y;
        end = 18.5;
    }
    while(end > 1 && t.scale * ldexp(1.0, -(int)ceil(end)) < limit_scale()) end -= 1;
    /* the same path as last time (and the same picture size): keep its keyframes */
    bool same = s_kf_done > 0 && t.kind == s_tour.kind && t.cx == s_tour.cx && t.cy == s_tour.cy &&
                t.scale == s_tour.scale && t.jx == s_tour.jx && t.jy == s_tour.jy && s_tour_iw == s_iw &&
                s_tour_ih == s_ih && s_tour_iters == s_iter_mode;
    if(!same) {
        s_kf_done = 0;
        s_kf_row = 0;
        if(s_tour_iw != s_iw || s_tour_ih != s_ih)
            for(int k = 0; k < TOUR_MAX; k++) {
                free(s_kf[k]);
                s_kf[k] = NULL;
            }
    }
    s_tour = t;
    s_tour_iw = s_iw;
    s_tour_ih = s_ih;
    s_tour_iters = s_iter_mode;
    s_tour_end = end;
    s_kf_n = (int)ceil(end) + 1;
    if(s_kf_n > TOUR_MAX) s_kf_n = TOUR_MAX;
    if(s_kf_done > s_kf_n) s_kf_done = s_kf_n;
    s_tz = 0;
    s_tour_waiting = false;
    s_tour_dir = 1;
    s_tour_hold = 0.5f;
    s_auto = true;
}

/* Back to exploring where the tour is: the keyframe in use becomes the
 * picture to carry along, so nothing starts from scratch. */
static void tour_stop(void)
{
    if(!s_auto) return;
    s_auto = false;
    int a = (int)floor(s_tz);
    if(a > s_kf_done - 1) a = s_kf_done - 1;
    s_v = s_tour;
    s_v.scale = s_tour.scale * pow(2.0, -s_tz);
    s_goal = s_v.scale;
    if(a < 0) {
        fresh();
        return;
    }
    memcpy(s_val, s_kf[a], sizeof(uint16_t) * (size_t)s_iw * s_ih);
    memset(s_lvl, 1, (size_t)s_iw * s_ih);
    s_held = s_tour;
    s_held.scale = s_tour.scale * ldexp(1.0, -a);
    s_step = 0;
    moved();
}

static void tour_tick(float dt)
{
    tour_compute(s_budget_ms);
    if(s_tour_hold > 0) s_tour_hold -= dt;
    else {
        double nz = s_tz + s_tour_dir * dt / 2.0; /* 2x in 2 s */
        if(s_tour_dir > 0) {
            if(nz > s_tour_end) nz = s_tour_end;
            int need = (int)floor(nz) + 1;
            if(need > s_kf_n - 1) need = s_kf_n - 1;
            s_tour_waiting = s_kf_done <= need;
            if(s_tour_waiting) nz = s_tz; /* wait for the keyframe */
            else if(nz >= s_tour_end) {
                s_tour_dir = -1;
                s_tour_hold = 2.5f;
            }
        }
        else if(nz <= 0) {
            nz = 0;
            s_tour_dir = 1;
            s_tour_hold = 1.5f;
        }
        s_tz = nz;
    }
    s_v.cx = s_tour.cx;
    s_v.cy = s_tour.cy;
    s_v.scale = s_tour.scale * pow(2.0, -s_tz);
    s_goal = s_v.scale;
}

/* Per frame: where each screen column and row falls in the two keyframes,
 * in 16.16 picture pixels; -1 outside. */
static int32_t * s_ta_x, * s_ta_y, * s_tb_x, * s_tb_y;
static int s_ta, s_tb;
static bool s_tour_draw; /* this frame comes from the keyframes */
static int s_tb_x0, s_tb_x1;  /* the screen columns the sharper keyframe covers */


static void tour_maps(int out_w, int out_h)
{
    if(!s_ta_x) {
        s_ta_x = malloc(sizeof(int32_t) * (size_t)s_w);
        s_tb_x = malloc(sizeof(int32_t) * (size_t)s_w);
        s_ta_y = malloc(sizeof(int32_t) * (size_t)s_h);
        s_tb_y = malloc(sizeof(int32_t) * (size_t)s_h);
    }
    int a = (int)floor(s_tz);
    if(a > s_kf_done - 1) a = s_kf_done - 1;
    if(a < 0) a = 0;
    s_ta = a;
    s_tb = a + 1 < s_kf_done ? a + 1 : -1;
    for(int pass = 0; pass < 2; pass++) {
        int k = pass ? s_tb : s_ta;
        int32_t * mx = pass ? s_tb_x : s_ta_x, * my = pass ? s_tb_y : s_ta_y;
        if(k < 0) continue;
        /* screen pixels per keyframe pixel */
        double r = pow(2.0, k - s_tz) / s_div;
        double lim_x = (double)(s_iw - 1), lim_y = (double)(s_ih - 1);
        for(int x = 0; x < out_w; x++) {
            double u = ((double)x + 0.5 - out_w * 0.5) * r + s_iw * 0.5 - 0.5;
            if(u < 0 || u > lim_x) mx[x] = pass ? -1 : (int32_t)((u < 0 ? 0 : lim_x) * 65536.0);
            else mx[x] = (int32_t)(u * 65536.0);
        }
        if(pass) {
            s_tb_x0 = out_w;
            s_tb_x1 = 0;
            for(int x = 0; x < out_w; x++)
                if(mx[x] >= 0) {
                    if(x < s_tb_x0) s_tb_x0 = x;
                    s_tb_x1 = x + 1;
                }
        }
        for(int y = 0; y < out_h; y++) {
            double u = ((double)y + 0.5 - out_h * 0.5) * r + s_ih * 0.5 - 0.5;
            if(u < 0 || u > lim_y) my[y] = pass ? -1 : (int32_t)((u < 0 ? 0 : lim_y) * 65536.0);
            else my[y] = (int32_t)(u * 65536.0);
        }
    }
}

/* Scaling a keyframe, row by row: first the two keyframe rows around the
 * screen row blended into one (a straight pass), then along it, two reads a
 * pixel. "Inside" and "unknown" do not mix: next to one of them the nearer
 * value is taken. */
static _Thread_local uint16_t * t_vrow;
static _Thread_local int t_vrow_size;
static _Thread_local bool t_vrow_special; /* the blended row holds "inside" or "unknown" */

static void tour_vrow(const uint16_t * buf, int32_t fy, int c0, int c1)
{
    int iy = fy >> 16, ay = (fy >> 8) & 255;
    const uint16_t * r0 = buf + (size_t)iy * s_iw, * r1 = iy < s_ih - 1 ? r0 + s_iw : r0;
    uint16_t * o = t_vrow;
    if(c1 > s_iw) c1 = s_iw;
    bool special = false;
    int x = c0;
#if defined(__ARM_NEON)
    /* 8 at a time; "inside" / "unknown" picked by mask, not by branch */
    {
        const uint16x8_t un = vdupq_n_u16(FR_UNKNOWN);
        uint16x8_t sp_any = vdupq_n_u16(0);
        uint16_t wa = (uint16_t)(256 - ay), wb = (uint16_t)ay;
        for(; x + 8 <= c1; x += 8) {
            uint16x8_t a = vld1q_u16(r0 + x), b = vld1q_u16(r1 + x);
            uint32x4_t lo = vmlal_n_u16(vmull_n_u16(vget_low_u16(a), wa), vget_low_u16(b), wb);
            uint32x4_t hi = vmlal_n_u16(vmull_n_u16(vget_high_u16(a), wa), vget_high_u16(b), wb);
            uint16x8_t e = vcombine_u16(vshrn_n_u32(lo, 8), vshrn_n_u32(hi, 8));
            uint16x8_t sp = vorrq_u16(vcgeq_u16(a, un), vcgeq_u16(b, un));
            sp_any = vorrq_u16(sp_any, sp);
            vst1q_u16(o + x, vbslq_u16(sp, ay < 128 ? a : b, e));
        }
        uint16x4_t m = vorr_u16(vget_low_u16(sp_any), vget_high_u16(sp_any));
        special = vget_lane_u64(vreinterpret_u64_u16(m), 0) != 0;
    }
#endif
    for(; x < c1; x++) {
        uint32_t a = r0[x], b = r1[x];
        if(a >= FR_UNKNOWN || b >= FR_UNKNOWN) {
            o[x] = (uint16_t)(ay < 128 ? a : b);
            special = true;
        }
        else o[x] = (uint16_t)((a * (uint32_t)(256 - ay) + b * (uint32_t)ay) >> 8);
    }
    o[s_iw] = o[s_iw - 1];
    t_vrow_special = special || (c1 == s_iw && o[s_iw] >= FR_UNKNOWN);
}

/* Along the row: plain C. A NEON version (vtbl from a 16-value table per
 * 4 columns) was slower on a small ARM core -- 28 against 19 ns a pixel on one core --
 * because the palette look-up needs each value back in a core register. */
static void tour_hrow(const int32_t * map, int x0, int x1, const uint32_t * lut, uint32_t * out)
{
    const uint16_t * v = t_vrow;
    if(!t_vrow_special) {
        /* no "inside" or "unknown" in the row: no checks, two pixels a turn
         * (an in-order core: the second fills the first's load delays) */
        int x = x0;
        for(; x + 1 < x1; x += 2) {
            int32_t f0 = map[x], f1 = map[x + 1];
            const uint16_t * p0 = v + (f0 >> 16), * p1 = v + (f1 >> 16);
            int32_t a0 = p0[0], b0 = p0[1], a1 = p1[0], b1 = p1[1];
            int32_t e0 = a0 + (((b0 - a0) * ((f0 >> 8) & 255)) >> 8);
            int32_t e1 = a1 + (((b1 - a1) * ((f1 >> 8) & 255)) >> 8);
            out[x] = lut[e0 & 255];
            out[x + 1] = lut[e1 & 255];
        }
        for(; x < x1; x++) {
            int32_t f = map[x];
            const uint16_t * p = v + (f >> 16);
            int32_t a = p[0], b = p[1];
            out[x] = lut[(a + (((b - a) * ((f >> 8) & 255)) >> 8)) & 255];
        }
        return;
    }
    for(int x = x0; x < x1; x++) {
        int32_t f = map[x];
        int ix = f >> 16, ax = (f >> 8) & 255;
        int32_t a = v[ix], b = v[ix + 1];
        uint32_t e;
        if(a < (int32_t)FR_UNKNOWN && b < (int32_t)FR_UNKNOWN)
            e = (uint32_t)(a + (((b - a) * ax) >> 8));
        else e = (uint32_t)(ax < 128 ? a : b);
        out[x] = lut[e < FR_UNKNOWN ? (e & 255) : e - FR_UNKNOWN + 256];
    }
}

static _Thread_local uint32_t * t_band;
static _Thread_local size_t t_band_size;
static int s_out_w, s_out_h, s_off;
static void (*s_sink)(uint32_t *, int, int, int, void *);
static void * s_user;
#define BAND 16
/* The demo zoom's bands: 4 rows (20 KB at 1280 wide) stay in a 32 KB L1
 * cache; 16 rows (80 KB) did not, and a small ARM core's write streaming
 * then sent every store to DRAM, to be read back from there. */
#define TOUR_BAND 4
#if defined(__arm__) && !defined(__aarch64__)
#define PREFETCH_W(p) __asm__ volatile(".arch_extension mp\n\tpldw [%0]" : : "r"(p))
#else
#define PREFETCH_W(p) __builtin_prefetch((p), 1, 3)
#endif

static void band_job(int job, int tid)
{
    (void)tid;
    int rows = s_tour_draw ? TOUR_BAND : BAND;
    int y0 = job * rows, y1 = y0 + rows < s_out_h ? y0 + rows : s_out_h;
    size_t need = (size_t)s_out_w * BAND;
    if(t_band_size < need) {
        free(t_band);
        t_band = malloc(need * 4);
        t_band_size = t_band ? need : 0;
        if(!t_band) return;
    }
    /* the palette turned by the cycling offset, plus the two special values
     * at the end: one look-up per pixel, no divides (some ARM cores have none) */
    uint32_t lut[258];
    for(int i = 0; i < 256; i++) lut[i] = s_lut[(i + s_off) & 255];
    lut[256] = 0xFF181818u; /* FR_UNKNOWN */
    lut[257] = 0xFF000000u; /* FR_INSIDE */
    if(s_tour_draw) {
        if(t_vrow_size < s_iw + 1) {
            free(t_vrow);
            t_vrow = calloc((size_t)(s_iw + 1), sizeof(uint16_t));
            t_vrow_size = t_vrow ? s_iw + 1 : 0;
            if(!t_vrow) return;
        }
        const uint16_t * ka = s_kf[s_ta], * kb = s_tb >= 0 ? s_kf[s_tb] : NULL;
        double ts = cpu_ms();
        for(int y = y0; y < y1; y++) {
            uint32_t * o = t_band + (size_t)(y - y0) * s_out_w;
            for(int x = 0; x < s_out_w; x += 16) PREFETCH_W(o + x); /* own the lines before writing them */
            int32_t by = kb ? s_tb_y[y] : -1;
            int bx0 = by >= 0 ? s_tb_x0 : s_out_w, bx1 = by >= 0 ? s_tb_x1 : s_out_w;
            if(bx0 > 0 || bx1 < s_out_w) {
                /* the edges from the current keyframe: only its columns needed */
                if(bx0 > 0) {
                    tour_vrow(ka, s_ta_y[y], 0, (s_ta_x[bx0 - 1] >> 16) + 2);
                    tour_hrow(s_ta_x, 0, bx0, lut, o);
                }
                if(bx1 < s_out_w) {
                    tour_vrow(ka, s_ta_y[y], s_ta_x[bx1] >> 16, s_iw);
                    tour_hrow(s_ta_x, bx1, s_out_w, lut, o);
                }
            }
            if(bx0 < bx1) {
                tour_vrow(kb, by, s_tb_x[bx0] >> 16, (s_tb_x[bx1 - 1] >> 16) + 2);
                tour_hrow(s_tb_x, bx0, bx1, lut, o);
            }
        }
        double tm = cpu_ms();
        s_sink(t_band, s_out_w, y0, y1 - y0, s_user);
        __atomic_add_fetch(&s_ns_scale, (int64_t)((tm - ts) * 1e6), __ATOMIC_RELAXED);
        __atomic_add_fetch(&s_ns_sink, (int64_t)((cpu_ms() - tm) * 1e6), __ATOMIC_RELAXED);
        return;
    }
    for(int y = y0; y < y1; y++) {
        int iy = s_div == 1 ? y : s_div == 2 ? y >> 1 : y * 21846 >> 16; /* y / 3 */
        if(iy >= s_ih) iy = s_ih - 1;
        const uint16_t * v = s_val + (size_t)iy * s_iw;
        uint32_t * o = t_band + (size_t)(y - y0) * s_out_w;
        if(s_div == 1) {
            for(int x = 0; x < s_out_w; x++) {
                uint32_t e = v[x];
                o[x] = lut[e < FR_UNKNOWN ? (e & 255) : e - FR_UNKNOWN + 256];
            }
            continue;
        }
        for(int ix = 0, x = 0; x < s_out_w; ix++) {
            uint32_t e = v[ix < s_iw ? ix : s_iw - 1];
            uint32_t c = lut[e < FR_UNKNOWN ? (e & 255) : e - FR_UNKNOWN + 256];
            for(int r = 0; r < s_div && x < s_out_w; r++) o[x++] = c;
        }
    }
    s_sink(t_band, s_out_w, y0, y1 - y0, s_user);
}

void fractal_render(int out_w, int out_h, void (*sink)(uint32_t * band, int stride_px, int y0, int rows, void * user),
                    void * user)
{
    if(!s_val) return;
    s_out_w = out_w < s_w ? out_w : s_w;
    s_out_h = out_h < s_h ? out_h : s_h;
    s_sink = sink;
    s_user = user;
    s_off = (int)s_offset;
    bool tour = s_auto && s_kf_done > 0; /* until its first keyframe: the picture as it was */
    if(tour) tour_maps(s_out_w, s_out_h);
    s_tour_draw = tour;
    double t0 = now_ms();
    int rows = tour ? TOUR_BAND : BAND;
    r3_parallel((s_out_h + rows - 1) / rows, band_job);
    s_ms_render += now_ms() - t0;
}

/* ---------------------------------------------------------------- moves */

static double limit_scale(void)
{
    /* the finest scale double precision still resolves, per screen pixel */
    double m = fabs(s_v.cx) > fabs(s_v.cy) ? fabs(s_v.cx) : fabs(s_v.cy);
    return 8e-15 * (m > 1 ? m : 1) / s_div;
}

static void zoom_at(double factor, int sx, int sy)
{
    /* recentre on the screen point, then zoom */
    s_v.cx += ((double)sx - s_w * 0.5) * s_v.scale;
    s_v.cy -= ((double)sy - s_h * 0.5) * s_v.scale;
    s_goal = s_goal * factor;
    if(s_goal < limit_scale()) s_goal = limit_scale();
    if(s_goal > s_base[s_v.kind] * 4) s_goal = s_base[s_v.kind] * 4;
    moved();
}

static void set_kind(fr_kind_t k)
{
    if(k == FR_JULIA && s_v.kind == FR_MANDELBROT) {
        s_mandel = s_v;
        s_have_mandel = true;
    }
    start_view(k);
    if(k == FR_MANDELBROT && s_have_mandel) {
        s_v = s_mandel;
        s_goal = s_v.scale;
    }
    s_auto = false;
    fresh();
}

static void julia_here(void)
{
    if(s_v.kind == FR_JULIA) {
        set_kind(FR_MANDELBROT);
        return;
    }
    double jx = s_v.cx, jy = s_v.cy;
    if(s_v.kind == FR_SHIP) jy = -jy;
    set_kind(FR_JULIA);
    s_v.jx = jx;
    s_v.jy = jy;
    fresh();
}

static void start_auto(void)
{
    if(s_auto) tour_stop();
    else tour_start();
}

static void menu_cb(lv_event_t * e)
{
    (void)e;
    if(s_menu) s_menu();
}

static void plus_cb(lv_event_t * e)
{
    (void)e;
    tour_stop();
    zoom_at(0.5, s_w / 2, s_h / 2);
}
static void minus_cb(lv_event_t * e)
{
    (void)e;
    tour_stop();
    zoom_at(2.0, s_w / 2, s_h / 2);
}
static void auto_cb(lv_event_t * e) { (void)e; start_auto(); }

static void stage_cb(lv_event_t * e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    tour_stop();
    if(lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) zoom_at(0.5, (int)p.x, (int)p.y);
    else zoom_at(2.0, s_w / 2, s_h / 2);
}

/* ---------------------------------------------------------------- the app */

static void init_once(void)
{
    if(s_ready) return;
    s_ready = true;
    s_threads = r3_thread_count();
    const char * b = getenv("ROLLING_ROB_FRACTAL_MS");
    if(b) s_budget_ms = atof(b);
    for(int k = 0; k < FR_KINDS; k++) start_view((fr_kind_t)k);
    s_v.jx = -0.8;
    s_v.jy = 0.156;
    start_view(FR_MANDELBROT);
    make_lut();
    fresh();
}

static void set_res(int div)
{
    s_auto = false; /* the keyframes are the old size */
    s_div = div;
    s_iw = (s_w + div - 1) / div;
    s_ih = (s_h + div - 1) / div;
    fresh();
}

void fractal_view_create(int w, int h)
{
    s_w = w;
    s_h = h;
    float sx = (float)w / 480.0f, sy = (float)h / 272.0f;
    s_sc = sx < sy ? sx : sy;
    size_t n = (size_t)w * h;
    s_val = malloc(n * 2);
    s_old = malloc(n * 2);
    s_lvl = malloc(n);
    s_old_lvl = malloc(n);
    s_map_x = malloc(sizeof(int) * (size_t)w);
    s_map_y = malloc(sizeof(int) * (size_t)h);
    if(!s_val || !s_old || !s_lvl || !s_old_lvl || !s_map_x || !s_map_y) {
        free(s_val);
        s_val = NULL;
    }
    s_iw = w;
    s_ih = h;

    s_ui = gu_layer();
    lv_obj_t * stage = lv_obj_create(s_ui);
    lv_obj_remove_style_all(stage);
    lv_obj_set_size(stage, LV_PCT(100), LV_PCT(100));
    lv_obj_add_event_cb(stage, stage_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(stage, stage_cb, LV_EVENT_LONG_PRESSED, NULL);
    gu_button(s_ui, LV_SYMBOL_HOME, K(6), K(6), K(26), K(20), K(11), menu_cb, NULL);
    s_info = gu_label(s_ui, "", K(10), lv_color_white());
    lv_obj_set_style_bg_color(s_info, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_info, LV_OPA_50, 0);
    lv_obj_set_style_pad_hor(s_info, K(4), 0);
    lv_obj_set_style_pad_ver(s_info, K(2), 0);
    lv_obj_set_style_radius(s_info, K(3), 0);
    lv_obj_set_pos(s_info, K(38), K(8));
    s_hint = gu_label(s_ui, "#ffd700 Arrows# move  #ffd700 Z X# zoom  #ffd700 Space# auto  #ffd700 J# Julia  "
                            "#ffd700 K# fractal  #ffd700 C P# colours  #ffd700 I# iterations  #ffd700 H# start",
                      K(9), lv_color_white());
    lv_obj_set_style_bg_color(s_hint, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_50, 0);
    lv_obj_set_style_pad_hor(s_hint, K(4), 0);
    lv_obj_set_style_pad_ver(s_hint, K(2), 0);
    lv_obj_set_style_radius(s_hint, K(3), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_LEFT, K(6), K(-6));
    int bw = K(26), bh = K(22), gap = K(4), x = w - K(6) - bw, y = h - K(6) - bh;
    gu_button(s_ui, LV_SYMBOL_PLAY, x, y - bh - gap, bw, bh, K(11), auto_cb, NULL);
    gu_button(s_ui, LV_SYMBOL_MINUS, x - bw - gap, y - bh - gap, bw, bh, K(12), minus_cb, NULL);
    gu_button(s_ui, LV_SYMBOL_PLUS, x - 2 * (bw + gap), y - bh - gap, bw, bh, K(12), plus_cb, NULL);
}

void fractal_view_set_menu(void (*cb)(void)) { s_menu = cb; }

void fractal_view_show(bool on)
{
    if(on == s_shown) return;
    s_shown = on;
    lv_obj_set_hidden(s_ui, !on);
    if(on) init_once();
    else tour_stop();
}

static void update_info(void)
{
    /* at most five times a second: a label changing every frame made LVGL
     * redraw and lay it over the picture every frame (slow on small devices) */
    static uint32_t last;
    if(lv_tick_elaps(last) < 200) return;
    last = lv_tick_get();
    char buf[200], z[24];
    double zoom = zoom_of();
    if(zoom < 1e4) snprintf(z, sizeof(z), "%.0f", zoom);
    else snprintf(z, sizeof(z), "%.1e", zoom);
    fr_view_t v = pic_view();
    char state[48];
    if(s_auto) {
        if(s_tour_waiting && s_tour_dir > 0 && s_tour_hold <= 0)
            snprintf(state, sizeof(state), "#ffd700 demo zoom: computing picture %d of %d#", s_kf_done + 1, s_kf_n);
        else snprintf(state, sizeof(state), "#ffd700 demo zoom#");
    }
    else if(s_step) {
        int rows = (s_ry1 + s_step - 1) / s_step - (s_ry0 + s_step - 1) / s_step;
        int pass = s_step == 8 ? 0 : s_step == 4 ? 1 : s_step == 2 ? 2 : 3;
        snprintf(state, sizeof(state), "#ffd700 computing %d%%#", (pass * 100 + s_prow * 100 / (rows ? rows : 1)) / 4);
    }
    else snprintf(state, sizeof(state), "%.0f ms", s_took_ms);
    snprintf(buf, sizeof(buf), "%s   zoom %s   %d iterations   %s\n#b0b0b0 x %.12g   y %.12g   %s, %d %s#",
             fr_kind_name(s_v.kind), z, v.max_iter, state, s_v.cx, s_v.cy, fr_uses_float(&v) ? "float" : "double",
             s_threads, s_threads == 1 ? "core" : "cores");
    gu_set_text(s_info, buf);
}

/* ROLLING_ROB_FPS: where the time went, once a second */
static void log_second(void)
{
    static int verbose = -1;
    static double last;
    if(verbose < 0) verbose = getenv("ROLLING_ROB_FPS") != NULL;
    double t = now_ms();
    if(verbose && t - last >= 1000) {
        uint64_t work = fr_take_work();
        if(last > 0)
            printf("fractal per s: compute %.0f ms (%.1f M pixel-iterations, %.2f ns each), carry %.0f ms, colour %.0f ms"
                   " (scaling %.0f + to the screen %.0f ms CPU)\n",
                   s_ms_compute, work / 1e6, work ? s_ms_compute * 1e6 / (double)work : 0.0, s_ms_moved, s_ms_render,
                   __atomic_exchange_n(&s_ns_scale, 0, __ATOMIC_RELAXED) / 1e6,
                   __atomic_exchange_n(&s_ns_sink, 0, __ATOMIC_RELAXED) / 1e6);
        last = t;
        s_ms_compute = s_ms_moved = s_ms_render = 0;

    }
}

void fractal_view_tick(uint32_t dt_ms)
{
    if(!s_shown || !s_val) return;
    float dt = (float)dt_ms / 1000.0f;
    if(s_cycle) {
        s_offset += dt * 24.0f;
        if(s_offset >= 256.0f) s_offset -= 256.0f;
    }

    /* held keys: smooth pan and zoom */
    uint32_t tick = lv_tick_get();
#define TAPPED(i) ((int32_t)(s_tap_until[i] - tick) > 0)
    int dx = (app_key_held(APP_KEY_RIGHT) || app_key_held('d') || TAPPED(1)) -
             (app_key_held(APP_KEY_LEFT) || app_key_held('a') || TAPPED(0));
    int dy = (app_key_held(APP_KEY_DOWN) || app_key_held('s') || TAPPED(3)) -
             (app_key_held(APP_KEY_UP) || app_key_held('w') || TAPPED(2));
#undef TAPPED
    bool zin = app_key_held('z'), zout = app_key_held('x');
    if(s_auto && (dx || dy || zin || zout)) tour_stop();
    if(s_auto) {
        tour_tick(dt);
        update_info();
        log_second();
        return;
    }
    s_pan_x += dx * 0.5 * s_iw * dt;
    s_pan_y += dy * 0.5 * s_iw * dt;
    s_z_held_ms = zin ? s_z_held_ms + dt_ms : 0;
    s_x_held_ms = zout ? s_x_held_ms + dt_ms : 0;
    if(s_z_held_ms > 250) s_goal *= pow(2.0, -dt / 0.6);
    if(s_x_held_ms > 250) s_goal *= pow(2.0, dt / 0.6);
    if(s_goal < limit_scale()) s_goal = limit_scale();
    if(s_goal > s_base[s_v.kind] * 4) s_goal = s_base[s_v.kind] * 4;

    bool changed = false;
    int px = (int)s_pan_x, py = (int)s_pan_y;
    if(px || py) {
        s_pan_x -= px;
        s_pan_y -= py;
        s_v.cx += px * s_v.scale * s_div;
        s_v.cy -= py * s_v.scale * s_div;
        changed = true;
    }
    if(s_goal != s_v.scale) {
        double f = dt / 0.12;
        if(f > 1) f = 1;
        s_v.scale *= pow(s_goal / s_v.scale, f);
        if(fabs(s_v.scale / s_goal - 1.0) < 1e-3) s_v.scale = s_goal;
        changed = true;
    }
    if(changed) {
        double t0 = now_ms();
        moved();
        s_ms_moved += now_ms() - t0;
    }
    compute_for(s_budget_ms);
    update_info();
    log_second();
}

void fractal_view_key(int k)
{
    int dir = k == APP_KEY_LEFT || k == 'a' ? 0 : k == APP_KEY_RIGHT || k == 'd' ? 1 : k == APP_KEY_UP || k == 'w' ? 2
            : k == APP_KEY_DOWN || k == 's' ? 3 : -1;
    if(dir >= 0) s_tap_until[dir] = lv_tick_get() + 150;
    switch(k) {
        case 'z': tour_stop(); zoom_at(0.5, s_w / 2, s_h / 2); break;
        case 'x': tour_stop(); zoom_at(2.0, s_w / 2, s_h / 2); break;
        case APP_KEY_SPACE: start_auto(); break;
        case 'j': julia_here(); break;
        case 'k': fractal_view_kind_step(1); break;
        case 'c': s_cycle = !s_cycle; break;
        case 'p': fractal_view_palette_step(1); break;
        case 'i': fractal_view_iter_step(1); break;
        case 'r': fractal_view_res_step(1); break;
        case 'h': fractal_view_reset(); break;
        default: break;
    }
}

/* ---------------------------------------------------------------- the page */

const char * fractal_view_kind_name(void) { return fr_kind_name(s_v.kind); }
void fractal_view_kind_step(int dir)
{
    init_once();
    set_kind((fr_kind_t)(((int)s_v.kind + (dir < 0 ? -1 : 1) + FR_KINDS) % FR_KINDS));
}
const char * fractal_view_palette_name(void) { return k_pals[s_pal].name; }
void fractal_view_palette_step(int dir)
{
    s_pal = (s_pal + (dir < 0 ? -1 : 1) + N_PALS) % N_PALS;
    make_lut();
}
bool fractal_view_cycling(void) { return s_cycle; }
void fractal_view_set_cycling(bool on) { s_cycle = on; }
const char * fractal_view_iter_text(void)
{
    static char buf[32];
    if(s_iter_mode == 0) snprintf(buf, sizeof(buf), "by the zoom (%d)", iterations());
    else snprintf(buf, sizeof(buf), "%d", k_iters[s_iter_mode]);
    return buf;
}
void fractal_view_iter_step(int dir)
{
    init_once();
    s_auto = false;
    s_iter_mode = (s_iter_mode + (dir < 0 ? -1 : 1) + N_ITERS) % N_ITERS;
    fresh();
}
const char * fractal_view_res_text(void)
{
    static char buf[40];
    if(s_div == 1) snprintf(buf, sizeof(buf), "full, %dx%d", s_w, s_h);
    else snprintf(buf, sizeof(buf), "1/%d, %dx%d", s_div, s_iw, s_ih);
    return buf;
}
void fractal_view_res_step(int dir)
{
    init_once();
    int d = s_div + (dir < 0 ? -1 : 1);
    set_res(d < 1 ? 3 : d > 3 ? 1 : d);
}
void fractal_view_reset(void)
{
    init_once();
    s_have_mandel = false;
    double jx = s_v.jx, jy = s_v.jy;
    start_view(s_v.kind);
    s_v.jx = jx;
    s_v.jy = jy;
    s_auto = false;
    fresh();
}
const char * fractal_view_where(void)
{
    static char buf[96];
    if(!s_ready) return "The start view";
    double z = zoom_of();
    if(z < 1.001) snprintf(buf, sizeof(buf), "%s, the start view", fr_kind_name(s_v.kind));
    else if(z < 1e4) snprintf(buf, sizeof(buf), "%s, zoomed %.0fx", fr_kind_name(s_v.kind), z);
    else snprintf(buf, sizeof(buf), "%s, zoomed %.1ex", fr_kind_name(s_v.kind), z);
    return buf;
}

void fractal_view_preview(void)
{
    if(!s_val) return;
    init_once();
    /* sharp enough for a picture a third of the size: up to the 2-grid */
    double t0 = now_ms();
    while(s_step > 1 && compute_batch()) {
    }
    s_took_ms += now_ms() - t0;
    int w, h;
    page_pic_frame(&w, &h);
    fractal_render(w, h, page_pic_band, NULL);
    update_info();
    page_pic_widgets(s_ui);
}
