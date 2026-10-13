#include "draw.h"
#include "prof.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef _WIN32
/* mingw-w64 has neither C11 aligned_alloc nor sysconf. */
#include <malloc.h>
#include <windows.h>
#define rr_aligned_alloc(a, n) _aligned_malloc((n), (a))
#define rr_aligned_free _aligned_free
static long rr_ncpu(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (long)si.dwNumberOfProcessors;
}
#else
#define rr_aligned_alloc aligned_alloc
#define rr_aligned_free free
static long rr_ncpu(void) { return sysconf(_SC_NPROCESSORS_ONLN); }
#endif

/*
 * Parallel frame rendering. Every thread runs the whole of rr_draw_frame()
 * -- the draw code, the paths, the transforms -- but its context only writes
 * the rows y % n == phase, so the rasterizing (the part that costs) is split
 * n ways while each pixel still sees exactly the operations it would in a
 * single-threaded frame, in the same order. Interleaved rows rather than
 * bands keep the load even: the sky, the ground and the buttons are not
 * spread evenly down the screen.
 */

#define RR_MAX_THREADS 4

static int s_n; /* contexts per frame, including the calling thread; 0 = not set up */
static int s_wanted; /* from rr_draw_set_threads(); 0 = one per core */
static pthread_mutex_t s_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_go = PTHREAD_COND_INITIALIZER;
static pthread_cond_t s_done = PTHREAD_COND_INITIALIZER;
static unsigned s_gen;
static int s_pending;

static struct {
    uint32_t * px;
    int w, h, stride;
    const rr_view_t * v;
    float scale;
    /* band mode (px == NULL) */
    int band_rows, bands;
    rr_band_sink_t sink;
    rr_band_text_t text;
    void * user;
} s_job;
static int s_next_band; /* next band to hand out, atomic */
static bool s_bottom_up;

void rr_draw_set_band_order(bool bottom_up) { s_bottom_up = bottom_up; }

/* One buffer per thread, reused; it only has to hold one band. */
static _Thread_local uint32_t * t_band;
static _Thread_local size_t t_band_size;

static void draw_bands(void)
{
    size_t need = (size_t)s_job.band_rows * (size_t)s_job.w * 4;
    if(t_band_size < need) {
        rr_aligned_free(t_band);
        t_band = rr_aligned_alloc(64, (need + 63) & ~(size_t)63);
        t_band_size = t_band ? need : 0;
        if(t_band == NULL) return;
    }
    gfx_text_t text[GFX_TEXT_MAX];
    for(;;) {
        int b = __atomic_fetch_add(&s_next_band, 1, __ATOMIC_RELAXED);
        if(b >= s_job.bands) break;
        /* In the order the beam frees them, when racing it. */
        if(s_bottom_up) b = s_job.bands - 1 - b;
        int y0 = b * s_job.band_rows;
        int y1 = y0 + s_job.band_rows < s_job.h ? y0 + s_job.band_rows : s_job.h;
        gfx_ctx_t g;
        /* px addresses the whole frame; only rows y0..y1-1 are ever touched. */
        gfx_begin(&g, t_band - (ptrdiff_t)y0 * s_job.w, s_job.w, s_job.h, s_job.w);
        gfx_set_band(&g, y0, y1);
        g.text_q = text;
        g.text_max = GFX_TEXT_MAX;
        rr_draw_frame(&g, s_job.v, s_job.scale);
        if(s_job.text && g.text_n) s_job.text(&g, text, g.text_n);
        s_job.sink(t_band, s_job.w, y0, y1 - y0, s_job.user);
    }
}

static void draw_rows(int phase)
{
    if(s_job.px == NULL) {
        draw_bands();
        return;
    }
    gfx_ctx_t g;
    gfx_begin(&g, s_job.px, s_job.w, s_job.h, s_job.stride);
    gfx_set_rows(&g, phase, s_n, phase == 0);
    rr_draw_frame(&g, s_job.v, s_job.scale);
}

static void * worker(void * arg)
{
    int phase = (int)(intptr_t)arg;
    unsigned seen = 0;
    pthread_mutex_lock(&s_mx);
    for(;;) {
        while(s_gen == seen) pthread_cond_wait(&s_go, &s_mx);
        seen = s_gen;
        pthread_mutex_unlock(&s_mx);
        draw_rows(phase);
        pthread_mutex_lock(&s_mx);
        if(--s_pending == 0) pthread_cond_signal(&s_done);
    }
    return NULL;
}

static void setup(void)
{
    const char * env = getenv("ROLLING_ROB_THREADS");
    long n = env ? atol(env) : (s_wanted > 0 ? s_wanted : rr_ncpu());
    if(n < 1) n = 1;
    if(n > RR_MAX_THREADS) n = RR_MAX_THREADS;
    s_n = 1;
    for(int i = 1; i < n; i++) {
        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        /* gfx_ctx_t is ~10 KB; the draw code is not deeply recursive. */
        pthread_attr_setstacksize(&attr, 256 * 1024);
        int err = pthread_create(&t, &attr, worker, (void *)(intptr_t)i);
        pthread_attr_destroy(&attr);
        if(err != 0) break;
        pthread_detach(t);
        s_n++;
    }
}

void rr_draw_set_threads(int n)
{
    s_wanted = n;
}

int rr_draw_threads(void) { return s_n; }

static void run_job(void);

void rr_draw_frame_parallel(uint32_t * px, int w, int h, int stride_px, const rr_view_t * v,
                            float view_scale)
{
    if(s_n == 0) setup();
    rr_draw_prepare(w, h);

    s_job.px = px;
    s_job.w = w;
    s_job.h = h;
    s_job.stride = stride_px;
    s_job.v = v;
    s_job.scale = view_scale;
    rr_draw_sky_mask(px, w, h, stride_px, v, view_scale);
    run_job();
}

void rr_draw_frame_bands(int w, int h, const rr_view_t * v, float view_scale, int band_rows,
                         rr_band_sink_t sink, rr_band_text_t text, void * user)
{
    if(s_n == 0) setup();
    rr_draw_prepare(w, h);
    if(band_rows < 1) band_rows = 1;

    s_job.px = NULL;
    s_job.w = w;
    s_job.h = h;
    s_job.stride = w;
    s_job.v = v;
    s_job.scale = view_scale;
    s_job.band_rows = band_rows;
    s_job.bands = (h + band_rows - 1) / band_rows;
    s_job.sink = sink;
    s_job.text = text;
    s_job.user = user;
    s_next_band = 0;
    /* The mask is computed without writing a pixel, so no buffer is needed. */
    rr_draw_sky_mask(NULL, w, h, w, v, view_scale);
    run_job();
}

static void run_job(void)
{

    if(s_n > 1) {
        pthread_mutex_lock(&s_mx);
        s_pending = s_n - 1;
        s_gen++;
        pthread_cond_broadcast(&s_go);
        pthread_mutex_unlock(&s_mx);
    }
    draw_rows(0);
    if(s_n > 1) {
        RR_PROF(RR_PROF_JOIN, {
            pthread_mutex_lock(&s_mx);
            while(s_pending > 0) pthread_cond_wait(&s_done, &s_mx);
            pthread_mutex_unlock(&s_mx);
        });
    }
}
