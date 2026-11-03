#include "art.h"
#include "level.h"
#include "terrain.h"
#include "lvgl.h"
#include "src/libs/lodepng/lodepng.h"
#include "src/libs/tjpgd/tjpgd.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Premultiplied 0xAARRGGBB. Premultiplied so that scaling and rotating do
 * not darken the soft edges of the sprites, and blending is one multiply. */
typedef struct {
    uint32_t * px;
    int w, h;
} img_t;

/* A sprite: the picture as loaded (trimmed to its visible part), and the
 * copy scaled for the current view scale. */
typedef struct {
    img_t src;
    img_t dev;
} sprite_t;

#define ROB_FRAMES   64 /* at most; the rendered sheet holds this many */
/* How many Rob turns: ROLLING_ROB_ROB_FRAMES, 8, 16, 32 or 64. Rob turns fast
 * and at 60 fps the difference between 16 and 64 is hard to see; fewer
 * frames load faster and give a slightly stepped, hand-made roll. */
static int rob_frames(void)
{
    static int n = 0;
    if(n == 0) {
        const char * e = getenv("ROLLING_ROB_ROB_FRAMES");
        n = e ? atoi(e) : 16;
        if(n != 8 && n != 16 && n != 32 && n != 64) n = 16;
    }
    return n;
}
#define TREE_SIZES   25
#define MAX_COLS   2048

/* Where the ball sits in rob.png, which is a whole atlas cell (not trimmed):
 * centre and radius as fractions of the cell, measured on the picture. */
#define ROB_CX 0.634f
#define ROB_CY 0.540f
#define ROB_R  0.315f

/* One picture set per level: art/bg-N.jpg, tex-N.jpg, tree-N.png and
 * enemy-N.png, N = level - 1. A set that is missing or broken (any of its
 * pictures cannot be read or decoded) is not tried again this session; its
 * level then draws the fallback set if it has one (level 11, Beyond the
 * Summit: level 6's space set, should its own pictures be missing or
 * broken), else the original look. Either way the other levels keep their pictures. */
#define ART_SETS RR_LEVEL_COUNT
static const int8_t s_fallback[ART_SETS] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 5 };

/* The lip colour of the ground per picture set (the painted set's accent);
 * set 0 derives it from the soil itself. Set 10's teal matches its
 * glowing plants. */
static const uint32_t s_accent[ART_SETS] = {
    0, 0xF4C37D, 0xF0F9ED, 0xFFAD53, 0xA5CA75, 0xA5A6D6, 0xF9DFB1, 0x78EDDA, 0xF58458, 0xF1D781, 0x83D7D2,
};
static bool s_set_failed[ART_SETS];

/* The set a level draws: its own, or the fallback once its own has failed;
 * -1 when there is none left (the original look). */
static int art_set(int level)
{
    int set = level >= 0 && level < ART_SETS ? level : -1;
    while(set >= 0 && s_set_failed[set]) set = s_fallback[set];
    return set;
}

static enum { ART_UNKNOWN, ART_READY, ART_OFF } s_state;
static bool s_user_off = false; /* the painted look is the default (where art/ is); A, the picker or a saved choice switch */
static char s_dir[512];
/* The rendered set (made separately): the directory next to s_dir with "-3d"
 * added. It holds what it renders; everything else comes from s_dir. */
static bool s_rendered;
static char s_dir3[520];

static int s_level = -1;
static float s_scale;

static img_t s_bg, s_tex;
/* For a 16 bpp screen (rr_art_set_rgb565): the background and ground tile
 * also as RGB565, dithered once when loaded. The two biggest pictures are
 * read every frame, and at 720p reading them from DRAM is where the time
 * goes; half the bytes each. */
static bool s_565;
static uint16_t * s_bg16, * s_tex16;
static bool s_backdrop;     /* the picture is on a hardware layer: draw the key instead */
static unsigned s_bg_gen;   /* counts loaded pictures */
static sprite_t s_tree, s_deco;
static bool s_land3d_loaded; /* the landscape loaded is the rendered one */
/* The figures, one set per look (painted, rendered), so switching between
 * the two looks keeps both and costs nothing after the first time; the
 * landscape is the same in both. Rob and Julian are rebuilt only when the
 * file they come from or the size changes (a level may have its own,
 * lit for it, in art-3d/level-N). */
typedef struct {
    int level;
    float scale;
    sprite_t rob, boss, enemy, mine, ufo;
    img_t rob_frames[ROB_FRAMES];
    char rob_path[640], boss_path[640];
    float rob_scale, rob_radius;
} figs_t;
static figs_t s_figs[2] = { { .level = -1 }, { .level = -1 } };
#define FIG (&s_figs[s_rendered ? 1 : 0])
static img_t s_trees[TREE_SIZES];
static bool s_have_deco;

/* Per frame, written by rr_art_prepare() before the frame is drawn. */
static float s_cam_x, s_cam_y; /* snapped to whole device pixels, like frame_setup() */
static int s_camx_px, s_camy_px;
static int s_bg_x, s_bg_y;
static int32_t s_edge[MAX_COLS]; /* ground surface per column, device px * 256; INT32_MAX = none */
static uint16_t s_tx[MAX_COLS];  /* texture column per screen column */
static int32_t s_edge_min, s_edge_max; /* over all columns */
/* s_tx as runs of consecutive texture columns, for copying whole rows. */
typedef struct { int16_t x, n, tx, step; } tex_run_t;
static tex_run_t s_runs[64];
static int s_run_n;
/* Trees and decorations placed this frame (device position of the image). */
typedef struct { const img_t * im; int x, y; } placed_t;
static placed_t s_placed_trees[48], s_placed_deco[48];
static int s_trees_n, s_deco_n;
static int s_w, s_h;

/* ------------------------------------------------------------------ */
/* Loading                                                             */
/* ------------------------------------------------------------------ */

static void img_free(img_t * i)
{
    free(i->px);
    memset(i, 0, sizeof(*i));
}

static bool img_alloc(img_t * i, int w, int h)
{
    img_free(i);
    i->px = calloc((size_t)w * (size_t)h, 4);
    if(i->px == NULL) return false;
    i->w = w;
    i->h = h;
    return true;
}

/* The rendered look puts the rendered figures on the painted landscape: the
 * painted pictures carry more with the fire's light than the rendered ones,
 * the rendered figures are better than the painted. The rendered landscape
 * (art-3d bg/tex/tree/deco) stays available with ROLLING_ROB_3D_LANDSCAPE=1. */
static bool land3d(void)
{
    static int on = -1;
    if(on < 0) on = getenv("ROLLING_ROB_3D_LANDSCAPE") != NULL;
    return on;
}

static bool is_landscape(const char * name)
{
    return !strncmp(name, "bg-", 3) || !strncmp(name, "tex-", 4) || !strncmp(name, "tree-", 5) ||
           !strncmp(name, "deco-", 5);
}

static int s_loading = -1;    /* the level being loaded, for art-3d/level-N */

/* Opens an art file the way the current look finds it; `path` gets the
 * file's path (or the last one tried). */
static FILE * open_art(const char * name, char path[640])
{
    FILE * f = NULL;
    if(s_rendered && (land3d() || !is_landscape(name))) {
        /* figures lit for this level first (art-3d/level-N, N as in bg-N) */
        if(s_loading >= 0) {
            snprintf(path, 640, "%s/level-%d/%s", s_dir3, s_loading, name);
            f = fopen(path, "rb");
        }
        if(f == NULL) {
            snprintf(path, 640, "%s/%s", s_dir3, name);
            f = fopen(path, "rb");
        }
    }
    if(f == NULL) {
        snprintf(path, 640, "%s/%s", s_dir, name);
        f = fopen(path, "rb");
    }
    return f;
}

static uint8_t * read_file(const char * name, size_t * len)
{
    char path[640];
    FILE * f = open_art(name, path);
    if(f == NULL) {
        fprintf(stderr, "art: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t * data = n > 0 ? malloc((size_t)n) : NULL;
    if(data && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return data;
}

/* The pictures are a few hundred pixels; anything past this is not one of
 * them (and would only take memory). */
#define MAX_PICTURE 4096
static bool size_ok(unsigned w, unsigned h) { return w > 0 && h > 0 && w <= MAX_PICTURE && h <= MAX_PICTURE; }

static bool load_png(const char * name, img_t * out)
{
    size_t n;
    uint8_t * data = read_file(name, &n);
    if(data == NULL) return false;
    /* the size from the header first (IHDR: width, height at bytes 16-23),
     * so a picture far too large is not decoded at all */
    if(n < 24 || !size_ok((unsigned)data[16] << 24 | data[17] << 16 | data[18] << 8 | data[19],
                          (unsigned)data[20] << 24 | data[21] << 16 | data[22] << 8 | data[23])) {
        fprintf(stderr, "art: %s/%s is not a picture of a usable size\n", s_dir, name);
        free(data);
        return false;
    }
    unsigned char * dec = NULL;
    unsigned w = 0, h = 0;
    unsigned err = lodepng_decode32(&dec, &w, &h, data, n);
    free(data);
    /* LVGL's copy of lodepng hands back a draw buffer, not a plain array. */
    lv_draw_buf_t * buf = (lv_draw_buf_t *)dec;
    bool ok = err == 0 && buf != NULL && size_ok(w, h) && img_alloc(out, (int)w, (int)h);
    if(ok) {
        for(unsigned y = 0; y < h; y++) {
            const uint8_t * s = buf->data + (size_t)y * buf->header.stride;
            uint32_t * d = out->px + (size_t)y * w;
            for(unsigned x = 0; x < w; x++, s += 4) {
                uint32_t a = s[3];
                uint32_t r = (s[0] * a + 127) / 255, g = (s[1] * a + 127) / 255, b = (s[2] * a + 127) / 255;
                d[x] = a << 24 | r << 16 | g << 8 | b;
            }
        }
    }
    else {
        fprintf(stderr, "art: cannot decode %s/%s (%u)\n", s_dir, name, err);
    }
    if(buf) lv_draw_buf_destroy(buf);
    return ok;
}

typedef struct {
    const uint8_t * p;
    size_t left;
    img_t * img;
} jpg_io_t;

static size_t jpg_in(JDEC * jd, uint8_t * buf, size_t n)
{
    jpg_io_t * io = jd->device;
    if(n > io->left) n = io->left;
    if(buf) memcpy(buf, io->p, n);
    io->p += n;
    io->left -= n;
    return n;
}

static int jpg_out(JDEC * jd, void * bitmap, JRECT * r)
{
    jpg_io_t * io = jd->device;
    const uint8_t * s = bitmap; /* this tjpgd writes B, G, R */
    for(int y = r->top; y <= r->bottom; y++) {
        uint32_t * d = io->img->px + (size_t)y * (size_t)io->img->w;
        for(int x = r->left; x <= r->right; x++, s += 3) d[x] = 0xFF000000u | s[2] << 16 | s[1] << 8 | s[0];
    }
    return 1;
}

static bool load_jpg(const char * name, img_t * out)
{
    size_t n;
    uint8_t * data = read_file(name, &n);
    if(data == NULL) return false;
    jpg_io_t io = { data, n, out };
    JDEC jd;
    void * pool = malloc(32 * 1024);
    bool ok = pool && jd_prepare(&jd, jpg_in, pool, 32 * 1024, &io) == JDR_OK && size_ok(jd.width, jd.height) &&
              img_alloc(out, jd.width, jd.height) && jd_decomp(&jd, jpg_out, 0) == JDR_OK;
    if(!ok) fprintf(stderr, "art: cannot decode %s/%s (baseline JPEG only)\n", s_dir, name);
    free(pool);
    free(data);
    return ok;
}

/* Cuts a sprite down to where it is visible (alpha > 16), plus 2 pixels. */
static void trim(img_t * im)
{
    int x0 = im->w, y0 = im->h, x1 = -1, y1 = -1;
    for(int y = 0; y < im->h; y++) {
        for(int x = 0; x < im->w; x++) {
            if((im->px[(size_t)y * im->w + x] >> 24) > 16) {
                if(x < x0) x0 = x;
                if(x > x1) x1 = x;
                if(y < y0) y0 = y;
                if(y > y1) y1 = y;
            }
        }
    }
    if(x1 < 0) return;
    x0 = x0 > 2 ? x0 - 2 : 0;
    y0 = y0 > 2 ? y0 - 2 : 0;
    x1 = x1 + 2 < im->w ? x1 + 2 : im->w - 1;
    y1 = y1 + 2 < im->h ? y1 + 2 : im->h - 1;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    for(int y = 0; y < h; y++) memmove(im->px + (size_t)y * w, im->px + (size_t)(y + y0) * im->w + x0, (size_t)w * 4);
    im->w = w;
    im->h = h;
}

static bool load_sprite(const char * name, sprite_t * s, bool trimmed)
{
    img_free(&s->dev);
    if(!load_png(name, &s->src)) return false;
    if(trimmed) trim(&s->src);
    return true;
}

/* ------------------------------------------------------------------ */
/* Scaling                                                             */
/* ------------------------------------------------------------------ */

/* 1-D filter taps for resampling `sn` source pixels to `dn`: the area each
 * output pixel covers when shrinking, linear interpolation when enlarging. */
typedef struct {
    int * first;
    int * count;
    float * w; /* `taps` weights per output pixel */
    int taps;
} taps_t;

static bool taps_make(taps_t * t, int sn, int dn)
{
    float r = (float)sn / (float)dn;
    t->taps = r > 1.0f ? (int)ceilf(r) + 2 : 2;
    t->first = malloc(sizeof(int) * (size_t)dn);
    t->count = malloc(sizeof(int) * (size_t)dn);
    t->w = calloc((size_t)dn * (size_t)t->taps, sizeof(float));
    if(!t->first || !t->count || !t->w) return false;
    for(int o = 0; o < dn; o++) {
        float * w = t->w + (size_t)o * t->taps;
        if(r > 1.0f) {
            float a = (float)o * r, b = a + r;
            int i0 = (int)floorf(a), n = 0;
            for(int i = i0; (float)i < b && n < t->taps; i++, n++) {
                float lo = (float)i > a ? (float)i : a;
                float hi = (float)(i + 1) < b ? (float)(i + 1) : b;
                w[n] = (hi - lo) / r;
            }
            t->first[o] = i0;
            t->count[o] = n;
        }
        else {
            float c = ((float)o + 0.5f) * r - 0.5f;
            int i0 = (int)floorf(c);
            float f = c - (float)i0;
            t->first[o] = i0;
            t->count[o] = 2;
            w[0] = 1.0f - f;
            w[1] = f;
        }
    }
    return true;
}

static void taps_free(taps_t * t)
{
    free(t->first);
    free(t->count);
    free(t->w);
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* `src` scaled to dw x dh into `out`. */
static bool resample(const img_t * src, int dw, int dh, img_t * out)
{
    if(dw < 1) dw = 1;
    if(dh < 1) dh = 1;
    taps_t tx, ty;
    memset(&tx, 0, sizeof(tx));
    memset(&ty, 0, sizeof(ty));
    float * mid = malloc(sizeof(float) * 4 * (size_t)dw * (size_t)src->h);
    bool ok = mid && taps_make(&tx, src->w, dw) && taps_make(&ty, src->h, dh) && img_alloc(out, dw, dh);
    if(ok) {
        for(int y = 0; y < src->h; y++) {
            const uint32_t * s = src->px + (size_t)y * src->w;
            float * m = mid + (size_t)y * dw * 4;
            for(int o = 0; o < dw; o++, m += 4) {
                const float * w = tx.w + (size_t)o * tx.taps;
                float c[4] = { 0, 0, 0, 0 };
                for(int k = 0; k < tx.count[o]; k++) {
                    uint32_t p = s[clampi(tx.first[o] + k, 0, src->w - 1)];
                    c[0] += w[k] * (float)(p >> 24);
                    c[1] += w[k] * (float)((p >> 16) & 0xFF);
                    c[2] += w[k] * (float)((p >> 8) & 0xFF);
                    c[3] += w[k] * (float)(p & 0xFF);
                }
                memcpy(m, c, sizeof(c));
            }
        }
        for(int o = 0; o < dh; o++) {
            const float * w = ty.w + (size_t)o * ty.taps;
            uint32_t * d = out->px + (size_t)o * dw;
            for(int x = 0; x < dw; x++) {
                float c[4] = { 0, 0, 0, 0 };
                for(int k = 0; k < ty.count[o]; k++) {
                    const float * m = mid + ((size_t)clampi(ty.first[o] + k, 0, src->h - 1) * dw + x) * 4;
                    for(int ch = 0; ch < 4; ch++) c[ch] += w[k] * m[ch];
                }
                uint32_t v[4];
                for(int ch = 0; ch < 4; ch++) v[ch] = (uint32_t)clampi((int)(c[ch] + 0.5f), 0, 255);
                for(int ch = 1; ch < 4; ch++) if(v[ch] > v[0]) v[ch] = v[0];
                d[x] = v[0] << 24 | v[1] << 16 | v[2] << 8 | v[3];
            }
        }
    }
    free(mid);
    taps_free(&tx);
    taps_free(&ty);
    return ok;
}

static inline int iround(float v) { return (int)floorf(v + 0.5f); }
static inline int dev_x(float wx);
static inline int dev_y(float wy);

/* The sprite scaled to fit a box of bw x bh world units, aspect kept. */
static bool fit(sprite_t * s, float bw, float bh, img_t * out)
{
    float f = fminf(bw / (float)s->src.w, bh / (float)s->src.h) * s_scale;
    return resample(&s->src, iround((float)s->src.w * f), iround((float)s->src.h * f), out);
}

/* Bilinear sample of a premultiplied image; transparent outside. */
static uint32_t sample(const img_t * im, float x, float y)
{
    x -= 0.5f;
    y -= 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - (float)x0, fy = y - (float)y0;
    float c[4] = { 0, 0, 0, 0 };
    for(int k = 0; k < 4; k++) {
        int xx = x0 + (k & 1), yy = y0 + (k >> 1);
        if(xx < 0 || yy < 0 || xx >= im->w || yy >= im->h) continue;
        float w = ((k & 1) ? fx : 1.0f - fx) * ((k >> 1) ? fy : 1.0f - fy);
        uint32_t p = im->px[(size_t)yy * im->w + xx];
        c[0] += w * (float)(p >> 24);
        c[1] += w * (float)((p >> 16) & 0xFF);
        c[2] += w * (float)((p >> 8) & 0xFF);
        c[3] += w * (float)(p & 0xFF);
    }
    uint32_t v[4];
    for(int ch = 0; ch < 4; ch++) v[ch] = (uint32_t)clampi((int)(c[ch] + 0.5f), 0, 255);
    for(int ch = 1; ch < 4; ch++) if(v[ch] > v[0]) v[ch] = v[0];
    return v[0] << 24 | v[1] << 16 | v[2] << 8 | v[3];
}

/* Rob, pre-rotated: ROB_FRAMES square frames with the ball's centre in the
 * middle, so drawing him is one blit at his position. */
static bool build_rob(float radius)
{
    img_t cell;
    memset(&cell, 0, sizeof(cell));
    float ball = (2.0f * radius + 4.0f) * s_scale; /* the original's outline included */
    int c = iround(ball / (2.0f * ROB_R));
    if(!resample(&FIG->rob.src, c, c, &cell)) return false;
    float px = ROB_CX * (float)c, py = ROB_CY * (float)c;
    float rx = fmaxf(px, (float)c - px), ry = fmaxf(py, (float)c - py);
    int d = 2 * (int)ceilf(sqrtf(rx * rx + ry * ry)) + 2;
    bool ok = true;
    const int n = rob_frames();
    for(int k = 0; k < n && ok; k++) {
        img_t * f = &FIG->rob_frames[k];
        ok = img_alloc(f, d, d);
        float a = 2.0f * (float)M_PI * (float)k / (float)n, ca = cosf(a), sa = sinf(a);
        for(int y = 0; ok && y < d; y++) {
            for(int x = 0; x < d; x++) {
                float dx = (float)x + 0.5f - (float)d * 0.5f, dy = (float)y + 0.5f - (float)d * 0.5f;
                f->px[(size_t)y * d + x] = sample(&cell, px + dx * ca + dy * sa, py - dx * sa + dy * ca);
            }
        }
    }
    img_free(&cell);
    return ok;
}

/* Rob from the rendered set: rob-3d.png holds ROB_FRAMES cells, 8 a row,
 * each Rob turned by k / ROB_FRAMES of a turn with the light kept where it
 * is (turning one picture turns its highlights too). The ball's radius is
 * ROB3D_R of a cell, its centre the cell's. */
#define ROB3D_R 0.25f
static bool build_rob3d(const img_t * sheet, float radius)
{
    int cell = sheet->w / 8;
    if(cell < 8 || sheet->h < cell * (ROB_FRAMES / 8)) return false;
    float ball = (2.0f * radius + 4.0f) * s_scale;
    int d = iround(ball / (2.0f * ROB3D_R));
    if(d < 4) d = 4;
    img_t one;
    memset(&one, 0, sizeof(one));
    bool ok = img_alloc(&one, cell, cell);
    const int n = rob_frames();
    for(int k = 0; ok && k < n; k++) {
        int c = k * (ROB_FRAMES / n); /* every (64/n)th cell of the sheet */
        int cx = (c % 8) * cell, cy = (c / 8) * cell;
        for(int y = 0; y < cell; y++) memcpy(one.px + (size_t)y * cell, sheet->px + (size_t)(cy + y) * sheet->w + cx, (size_t)cell * 4);
        ok = resample(&one, d, d, &FIG->rob_frames[k]);
    }
    img_free(&one);
    return ok;
}

/* Tree heights in world units, as the painted version varies them: forest
 * trees 48..72 logical pixels (4 world units each), the others 40..63. */
static float tree_height(int level, int i)
{
    return level == 0 ? (float)(48 + (i * 11) % 25) * 4.0f : (float)(40 + (i * 11) % 24) * 4.0f;
}

static bool tree_here(int level, int i)
{
    return level == 0 ? i % 4 == 2 : i % 5 == 2;
}

static void free_landscape(void)
{
    img_free(&s_bg);
    img_free(&s_tex);
    free(s_bg16);
    free(s_tex16);
    s_bg16 = s_tex16 = NULL;
    sprite_t * lv[] = { &s_tree, &s_deco };
    for(unsigned i = 0; i < sizeof(lv) / sizeof(lv[0]); i++) {
        img_free(&lv[i]->src);
        img_free(&lv[i]->dev);
    }
    for(int i = 0; i < TREE_SIZES; i++) img_free(&s_trees[i]);
    s_level = -1;
}

static void free_figs(figs_t * F)
{
    sprite_t * lv[] = { &F->rob, &F->boss, &F->enemy, &F->mine, &F->ufo };
    for(unsigned i = 0; i < sizeof(lv) / sizeof(lv[0]); i++) {
        img_free(&lv[i]->src);
        img_free(&lv[i]->dev);
    }
    for(int i = 0; i < ROB_FRAMES; i++) img_free(&F->rob_frames[i]);
    F->rob_path[0] = F->boss_path[0] = 0;
    F->level = -1;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* An opaque picture as RGB565 with a 4x4 ordered dither, anchored to
 * the picture. Expanded again with the low bits
 * zero (expand565), a 16 bpp screen's dither adds less than one RGB565 step,
 * so these pixels reach the screen exactly as dithered here. */
static uint16_t * make565(const img_t * im)
{
    static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    uint16_t * out = malloc((size_t)im->w * (size_t)im->h * 2);
    if(out == NULL) return NULL;
    for(int y = 0; y < im->h; y++) {
        const uint32_t * s = im->px + (size_t)y * im->w;
        uint16_t * d = out + (size_t)y * im->w;
        for(int x = 0; x < im->w; x++) {
            uint32_t t = bayer[y & 3][x & 3];
            uint32_t r = ((s[x] >> 16) & 0xFF) + (t >> 1), g = ((s[x] >> 8) & 0xFF) + (t >> 2), b = (s[x] & 0xFF) + (t >> 1);
            if(r > 255) r = 255;
            if(g > 255) g = 255;
            if(b > 255) b = 255;
            d[x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        }
    }
    return out;
}

static inline uint32_t from565(uint16_t p)
{
    return 0xFF000000u | (uint32_t)(p & 0xF800) << 8 | (uint32_t)(p & 0x07E0) << 5 | (uint32_t)(p & 0x001F) << 3;
}

/* n RGB565 pixels to opaque 0xAARRGGBB, low bits zero. */
static void expand565(uint32_t * d, const uint16_t * s, int n)
{
    int i = 0;
#if defined(__ARM_NEON)
    const uint8x8_t ff = vdup_n_u8(0xFF);
    for(; i + 8 <= n; i += 8) {
        uint16x8_t p = vld1q_u16(s + i);
        uint8x8x4_t o;
        o.val[0] = vshl_n_u8(vmovn_u16(p), 3);                 /* blue */
        o.val[1] = vshl_n_u8(vshrn_n_u16(p, 5), 2);            /* green */
        o.val[2] = vand_u8(vshrn_n_u16(p, 8), vdup_n_u8(0xF8)); /* red */
        o.val[3] = ff;
        vst4_u8((uint8_t *)(d + i), o);
    }
#endif
    for(; i < n; i++) d[i] = from565(s[i]);
}

/* n pixels of GFX_KEY. */
static void key_fill(uint32_t * d, int n)
{
    int i = 0;
#if defined(__ARM_NEON)
    const uint32x4_t k = vdupq_n_u32(GFX_KEY);
    for(; i + 16 <= n; i += 16) {
        vst1q_u32(d + i, k);
        vst1q_u32(d + i + 4, k);
        vst1q_u32(d + i + 8, k);
        vst1q_u32(d + i + 12, k);
    }
#endif
    for(; i < n; i++) d[i] = GFX_KEY;
}

/* Where a load spends its time, printed with the total. */
typedef struct { double t0, tl; char s[256]; int n; } steps_t;
static void step(steps_t * t, const char * what)
{
    double now = now_s();
    if(t->n < (int)sizeof(t->s) - 32)
        t->n += snprintf(t->s + t->n, sizeof(t->s) - (size_t)t->n, " %s %.0f", what, (now - t->tl) * 1000.0);
    t->tl = now;
}

/* Loads the level's landscape (background, ground, trees) and scales it for
 * the view scale. */
static bool load_landscape(int level, float scale)
{
    steps_t t = { .t0 = now_s() };
    t.tl = t.t0;
    free_landscape();
    s_scale = scale;
    s_loading = level;
    s_land3d_loaded = s_rendered && land3d();
    char name[32];
    img_t raw;
    memset(&raw, 0, sizeof(raw));

    /* Backgrounds are 570 (forest) or 560 logical pixels wide in the painted
     * version, 4 world units each. */
    snprintf(name, sizeof(name), "bg-%d.jpg", level);
    if(!load_jpg(name, &raw)) return false;
    float bw = (level == 0 ? 2280.0f : 2240.0f) * scale;
    bool ok = resample(&raw, iround(bw), iround(bw * (float)raw.h / (float)raw.w), &s_bg);
    img_free(&raw);
    step(&t, "bg");

    /* One ground tile spans 1024 world units. */
    snprintf(name, sizeof(name), "tex-%d.jpg", level);
    ok = ok && load_jpg(name, &raw);
    ok = ok && resample(&raw, iround(1024.0f * scale), iround(1024.0f * scale), &s_tex);
    img_free(&raw);
    step(&t, "tex");

    snprintf(name, sizeof(name), "tree-%d.png", level);
    ok = ok && load_sprite(name, &s_tree, true);
    for(int i = 0; ok && i < TREE_SIZES; i++) {
        float h = tree_height(level, i);
        ok = fit(&s_tree, 4.0f * h, h, &s_trees[i]);
    }
    s_have_deco = level == 0;
    if(s_have_deco) ok = ok && load_sprite("deco-0.png", &s_deco, true) && fit(&s_deco, 52, 44, &s_deco.dev);
    step(&t, "trees");
    if(ok && s_565) {
        s_bg16 = make565(&s_bg);
        s_tex16 = make565(&s_tex);
        ok = s_bg16 && s_tex16;
        step(&t, "565");
    }
    if(!ok) return false;

    s_level = level;
    s_bg_gen++;
    fprintf(stderr, "art: level %d landscape at scale %.3f in %.0f ms (ms:%s)\n", level, (double)scale,
            (now_s() - t.t0) * 1000.0, t.s);
    return true;
}

/* Loads the level's figures for the current look into its set. */
static bool load_figs(int level, float scale, float rob_radius)
{
    steps_t t = { .t0 = now_s() };
    t.tl = t.t0;
    figs_t * F = FIG;
    F->level = -1;
    s_loading = level;
    char name[32], path[640];
    const char * sfx = level == 0 ? "-0" : "";
    sprite_t * lv[] = { &F->enemy, &F->mine, &F->ufo };
    for(unsigned i = 0; i < sizeof(lv) / sizeof(lv[0]); i++) {
        img_free(&lv[i]->src);
        img_free(&lv[i]->dev);
    }

    snprintf(name, sizeof(name), "enemy-%d.png", level);
    bool ok = load_sprite(name, &F->enemy, true) && fit(&F->enemy, 84, 84, &F->enemy.dev);
    snprintf(name, sizeof(name), "mine%s.png", sfx);
    ok = ok && load_sprite(name, &F->mine, true) && fit(&F->mine, 66, 66, &F->mine.dev);
    snprintf(name, sizeof(name), "ufo%s.png", sfx);
    ok = ok && load_sprite(name, &F->ufo, true) && fit(&F->ufo, 112, 72, &F->ufo.dev);
    step(&t, "sprites");

    /* Julian */
    FILE * f = open_art("boss.png", path);
    if(f) fclose(f);
    if(ok && (strcmp(path, F->boss_path) || F->scale != scale)) {
        ok = load_sprite("boss.png", &F->boss, true) && fit(&F->boss, 140, 150, &F->boss.dev);
        snprintf(F->boss_path, sizeof(F->boss_path), "%s", ok ? path : "");
        step(&t, "julian");
    }

    /* Rob: the rendered sheet, or the painted picture turned */
    const char * rob = s_rendered ? "rob-3d.png" : "rob.png";
    f = open_art(rob, path);
    if(f) fclose(f);
    if(ok && (strcmp(path, F->rob_path) || F->rob_scale != scale || F->rob_radius != rob_radius)) {
        bool built = false;
        if(s_rendered) {
            img_t sheet;
            memset(&sheet, 0, sizeof(sheet));
            built = load_png(rob, &sheet) && build_rob3d(&sheet, rob_radius);
            img_free(&sheet);
        }
        if(!built) built = load_sprite("rob.png", &F->rob, false) && build_rob(rob_radius);
        ok = built;
        snprintf(F->rob_path, sizeof(F->rob_path), "%s", ok ? path : "");
        F->rob_scale = scale;
        F->rob_radius = rob_radius;
        step(&t, "rob");
    }
    if(!ok) return false;

    F->level = level;
    F->scale = scale;
    fprintf(stderr, "art: level %d %s figures in %.0f ms (ms:%s)\n", level, s_rendered ? "rendered" : "painted",
            (now_s() - t.t0) * 1000.0, t.s);
    return true;
}

/* ------------------------------------------------------------------ */
/* Per frame                                                           */
/* ------------------------------------------------------------------ */

void rr_art_set_enabled(bool on)
{
    s_user_off = !on;
}

void rr_art_set_rgb565(bool on)
{
    if(on != s_565) s_level = -1; /* reload with or without the copies */
    s_565 = on;
}

void rr_art_set_backdrop(bool on)
{
    s_backdrop = on;
}

bool rr_art_enabled(void)
{
    return !s_user_off;
}

static void find_dir(void);

bool rr_art_rendered_available(void)
{
    if(s_state == ART_UNKNOWN) find_dir();
    if(s_state != ART_READY) return false;
    char path[600];
    snprintf(path, sizeof(path), "%s/rob-3d.png", s_dir3);
    FILE * f = fopen(path, "rb");
    if(f) fclose(f);
    return f != NULL;
}

void rr_art_set_rendered(bool on)
{
    if(on == s_rendered) return;
    s_rendered = on; /* each look keeps its own figures; the landscape is shared */
}

bool rr_art_rendered(void) { return s_rendered; }

/* Whether a set's files are in the art directory (not whether they decode;
 * a set that fails to load is marked in s_set_failed). Checked once. */
static bool set_files_present(int set)
{
    static int8_t s_present[ART_SETS]; /* 0 unknown, 1 there, -1 missing */
    if(s_present[set] == 0) {
        char names[9][16];
        int n = 0;
        snprintf(names[n++], 16, "bg-%d.jpg", set);
        snprintf(names[n++], 16, "tex-%d.jpg", set);
        snprintf(names[n++], 16, "tree-%d.png", set);
        snprintf(names[n++], 16, "enemy-%d.png", set);
        snprintf(names[n++], 16, "mine%s.png", set == 0 ? "-0" : "");
        snprintf(names[n++], 16, "ufo%s.png", set == 0 ? "-0" : "");
        snprintf(names[n++], 16, "boss.png");
        snprintf(names[n++], 16, "rob.png");
        if(set == 0) snprintf(names[n++], 16, "deco-0.png");
        s_present[set] = 1;
        for(int i = 0; i < n && s_present[set] > 0; i++) {
            /* the painted directory: the rendered set takes what it lacks from it */
            char path[600];
            FILE * f = snprintf(path, sizeof(path), "%s/%s", s_dir, names[i]) < (int)sizeof(path) ? fopen(path, "rb")
                                                                                                : NULL;
            if(f) fclose(f);
            else s_present[set] = -1;
        }
    }
    return s_present[set] > 0;
}

int rr_art_set_for_level(int level)
{
    if(s_state == ART_UNKNOWN) find_dir();
    if(s_state != ART_READY || s_user_off) return -1;
    int set = level >= 0 && level < ART_SETS ? level : -1;
    while(set >= 0 && (s_set_failed[set] || !set_files_present(set))) set = s_fallback[set];
    return set;
}

static void find_dir(void)
{
    const char * env = getenv("ROLLING_ROB_ART");
    if(env && strcmp(env, "0") == 0) {
        s_state = ART_OFF;
        return;
    }
    snprintf(s_dir, sizeof(s_dir), "%s", env && *env ? env : "art");
    snprintf(s_dir3, sizeof(s_dir3), "%s-3d", s_dir);
    s_state = ART_READY;
}

bool rr_art_load_file(const char * name, uint32_t ** px, int * w, int * h)
{
    if(s_state == ART_UNKNOWN) find_dir();
    if(s_state != ART_READY) return false;
    img_t im;
    memset(&im, 0, sizeof(im));
    size_t n = strlen(name);
    bool ok = n > 4 && strcmp(name + n - 4, ".png") == 0 ? load_png(name, &im) : load_jpg(name, &im);
    if(!ok) {
        img_free(&im);
        return false;
    }
    *px = im.px;
    *w = im.w;
    *h = im.h;
    return true;
}

static inline int dev_x(float wx) { return iround((wx - s_cam_x) * s_scale); }
static inline int dev_y(float wy) { return iround((wy - s_cam_y) * s_scale); }

/* The ground surface under each screen column, from the same straight
 * segments the physics uses (rr_terrain_height_at), walked once left to right. */
static void compute_edge(const rr_terrain_t * t)
{
    int last = t->count - 2; /* the ridge ends with its closing slope at points[count - 2] */
    int seg = 0;
    for(int x = 0; x < s_w; x++) {
        float wx = s_cam_x + ((float)x + 0.5f) / s_scale;
        if(last < 1 || wx < t->points[0].x || wx > t->points[last].x) {
            s_edge[x] = INT32_MAX;
            continue;
        }
        while(seg < last - 1 && wx > t->points[seg + 1].x) seg++;
        const rr_point_t * p1 = &t->points[seg];
        const rr_point_t * p2 = &t->points[seg + 1];
        float dx = p2->x - p1->x;
        float y = dx > 0 ? p1->y + (p2->y - p1->y) * (wx - p1->x) / dx : p1->y;
        s_edge[x] = (int32_t)lroundf((y - s_cam_y) * s_scale * 256.0f);
    }
}

static bool s_drawn; /* the last prepared frame was drawn with the art */

bool rr_art_drawn(void)
{
    return s_drawn;
}

bool rr_art_backdrop(const uint32_t ** px, int * w, int * h, int * x, int * y, unsigned * gen)
{
    if(!s_drawn || !s_backdrop || s_bg16 == NULL) return false;
    *px = s_bg.px;
    *w = s_bg.w;
    *h = s_bg.h;
    *x = s_bg_x;
    *y = s_bg_y;
    *gen = s_bg_gen;
    return true;
}

bool rr_art_prepare(const rr_view_t * v, float view_scale, int w, int h)
{
    s_drawn = false;
    gfx_backdrop.px = NULL;
    if(s_state == ART_UNKNOWN) find_dir();
    if(s_state != ART_READY || s_user_off || w > MAX_COLS) return false;
    bool ok = false;
    for(int set = art_set(v->level); set >= 0 && !ok; set = art_set(v->level)) {
        ok = true;
        if(set != s_level || view_scale != s_scale || s_land3d_loaded != (s_rendered && land3d()))
            ok = load_landscape(set, view_scale);
        if(ok && (FIG->level != set || FIG->scale != view_scale))
            ok = load_figs(set, view_scale, (float)v->rob.radius);
        if(!ok) {
            /* not tried again; the other levels keep their pictures */
            free_landscape();
            free_figs(&s_figs[0]);
            free_figs(&s_figs[1]);
            s_set_failed[set] = true;
            if(s_fallback[set] >= 0)
                fprintf(stderr, "art: level %d: set %d missing or broken, set %d instead\n", (int)v->level + 1, set,
                        s_fallback[set]);
            else fprintf(stderr, "art: level %d: drawing the original look instead\n", (int)v->level + 1);
        }
    }
    if(!ok) return false;
    s_drawn = true;
    s_w = w;
    s_h = h;
    s_camx_px = (int)floorf(v->camera_x * view_scale + 0.5f);
    s_camy_px = (int)floorf(v->camera_y * view_scale + 0.5f);
    s_cam_x = (float)s_camx_px / view_scale;
    s_cam_y = (float)s_camy_px / view_scale;
    compute_edge(v->terrain);

    /* Mirrored repeats join matching edge pixels, so the tiles show no
     * seams; the forest soil tiles as it is. */
    int t = s_tex.w;
    for(int x = 0; x < w; x++) {
        int i = x + s_camx_px;
        if(s_level == 0) s_tx[x] = (uint16_t)(((i % t) + t) % t);
        else {
            int m = ((i % (2 * t)) + 2 * t) % (2 * t);
            s_tx[x] = (uint16_t)(m < t ? m : 2 * t - 1 - m);
        }
    }

    s_edge_min = INT32_MAX;
    s_edge_max = INT32_MIN;
    for(int x = 0; x < w; x++) {
        if(s_edge[x] < s_edge_min) s_edge_min = s_edge[x];
        if(s_edge[x] > s_edge_max) s_edge_max = s_edge[x];
    }
    s_run_n = 0;
    for(int x = 0; x < w;) {
        int step = x + 1 < w && s_tx[x + 1] == s_tx[x] - 1 ? -1 : 1;
        int n = 1;
        while(x + n < w && s_tx[x + n] == s_tx[x] + n * step) n++;
        if(s_run_n == (int)(sizeof(s_runs) / sizeof(s_runs[0]))) {
            s_run_n = -1; /* too fragmented: rows go pixel by pixel */
            break;
        }
        s_runs[s_run_n++] = (tex_run_t){ (int16_t)x, (int16_t)n, (int16_t)s_tx[x], (int16_t)step };
        x += n;
    }

    /* Trees stand behind the ground, their feet in it; placed once per frame,
     * not once per band. */
    const rr_terrain_t * tr = v->terrain;
    float right = tr->points[tr->count - 2].x;
    float span = (float)w / view_scale;
    s_trees_n = 0;
    int first = (int)(s_cam_x / 95.0f) - 3;
    if(first < 0) first = 0;
    for(int i = first; (float)i * 95.0f + 40.0f < s_cam_x + span + 200.0f && s_trees_n < 48; i++) {
        float wx = (float)i * 95.0f + 40.0f;
        if(!tree_here(s_level, i) || wx > right) continue;
        const img_t * im = &s_trees[i % TREE_SIZES];
        float wy = rr_terrain_height_at(tr, wx) + 8.0f;
        s_placed_trees[s_trees_n++] = (placed_t){ im, dev_x(wx) - im->w / 2, dev_y(wy) - im->h };
    }
    s_deco_n = 0;
    if(s_have_deco) {
        first = (int)(s_cam_x / 23.0f) - 3;
        if(first < 0) first = 0;
        for(int i = first; (float)i * 23.0f + 10.0f < s_cam_x + span + 60.0f && s_deco_n < 48; i++) {
            float wx = (float)i * 23.0f + 10.0f;
            if(i % 13 != 0 || wx > right) continue;
            float wy = rr_terrain_height_at(tr, wx) + 4.0f;
            s_placed_deco[s_deco_n++] = (placed_t){ &s_deco.dev, dev_x(wx) - s_deco.dev.w / 2, dev_y(wy) - s_deco.dev.h };
        }
    }

    /* The background hardly moves: the forest drifts with the camera, the
     * other levels pan 50 logical pixels over the whole run. Kept inside
     * the screen either way. */
    float bx;
    if(s_level == 0) bx = -120.0f - v->camera_x * 0.1f;
    else {
        float p = v->terrain->finish_line > 1.0f ? v->camera_x / v->terrain->finish_line : 0.0f;
        bx = -60.0f - (p < 0 ? 0 : p > 1 ? 1 : p) * 200.0f;
    }
    s_bg_x = clampi(iround(bx * view_scale), w - s_bg.w, 0);
    s_bg_y = iround(((float)h - (float)s_bg.h) * 0.5f);
    if(s_level == 0) s_bg_y = iround(-376.0f * view_scale);
    if(s_backdrop && s_bg16)
        gfx_backdrop = (gfx_backdrop_t){ s_bg16, s_bg.w, s_bg_x, s_bg_y, s_bg.w, s_bg.h };
    return true;
}

static inline int first_row(const gfx_ctx_t * g, int y) { return gfx_first_row(g, y); }

static inline int row_step(const gfx_ctx_t * g) { return g->row_step > 1 ? g->row_step : 1; }

static inline uint32_t over(uint32_t d, uint32_t s)
{
    uint32_t a = s >> 24;
    if(a == 255) return s;
    uint32_t ia = 255 - a;
    uint32_t rb = (d & 0x00FF00FFu) * ia + 0x00800080u;
    uint32_t g = (d & 0x0000FF00u) * ia + 0x00008000u;
    rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
    g = ((g + ((g >> 8) & 0x0000FF00u)) >> 8) & 0x0000FF00u;
    return 0xFF000000u | ((s & 0x00FFFFFFu) + rb + g);
}

/* Source-over of a premultiplied image with its top left at device (x, y). */
static void blit(gfx_ctx_t * g, const img_t * im, int x, int y)
{
    if(im->px == NULL) return;
    int ya = y < 0 ? 0 : y, yb = y + im->h > gfx_row_end(g) ? gfx_row_end(g) : y + im->h;
    int xa = x < 0 ? 0 : x, xb = x + im->w > g->w ? g->w : x + im->w;
    if(xa >= xb) return;
    for(int yy = first_row(g, ya); yy < yb; yy += row_step(g)) {
        const uint32_t * s = im->px + (size_t)(yy - y) * im->w - x;
        uint32_t * d = g->px + (size_t)yy * g->stride_px;
        for(int xx = xa; xx < xb; xx++) {
            uint32_t c = s[xx];
            if(c >> 24) d[xx] = over(gfx_under(d[xx], xx, yy), c);
        }
    }
}


/* Draws `im` centred on world x, with its bottom at world y. */
static void blit_bottom(gfx_ctx_t * g, const img_t * im, float wx, float wy)
{
    blit(g, im, dev_x(wx) - im->w / 2, dev_y(wy) - im->h);
}

static void blit_centre(gfx_ctx_t * g, const img_t * im, float wx, float wy)
{
    blit(g, im, dev_x(wx) - im->w / 2, dev_y(wy) - im->h / 2);
}

void rr_art_background(gfx_ctx_t * g, const rr_view_t * v)
{
    LV_UNUSED(v);
    /* The picture, down to the ground line; the ground is opaque. Rows wholly
     * above the ground are one copy, rows wholly below it are skipped. */
    if(rr_draw_layers & RR_LAYER_BG) for(int y = first_row(g, 0); y < gfx_row_end(g); y += row_step(g)) {
        int32_t top = y * 256;
        if(top >= s_edge_max) continue;
        int sy = clampi(y - s_bg_y, 0, s_bg.h - 1);
        uint32_t * d = g->px + (size_t)y * g->stride_px;
        if(gfx_backdrop.px) {
            /* On the hardware layer: only the key, where the picture shows. */
            if(top < s_edge_min) key_fill(d, g->w);
            else for(int x = 0; x < g->w; x++) if(top < s_edge[x]) d[x] = GFX_KEY;
            continue;
        }
        if(s_bg16) {
            const uint16_t * s = s_bg16 + (size_t)sy * s_bg.w - s_bg_x;
            if(top < s_edge_min) expand565(d, s, g->w);
            else for(int x = 0; x < g->w; x++) if(top < s_edge[x]) d[x] = from565(s[x]);
            continue;
        }
        const uint32_t * s = s_bg.px + (size_t)sy * s_bg.w - s_bg_x;
        if(top < s_edge_min) {
            memcpy(d, s, (size_t)g->w * 4);
            continue;
        }
        for(int x = 0; x < g->w; x++) if(top < s_edge[x]) d[x] = s[x];
    }
    if(rr_draw_layers & RR_LAYER_WORLD)
        for(int i = 0; i < s_trees_n; i++) blit(g, s_placed_trees[i].im, s_placed_trees[i].x, s_placed_trees[i].y);
}

void rr_art_ground(gfx_ctx_t * g, const rr_view_t * v)
{
    int t = s_tex.w;
    int32_t lip = (int32_t)(20.0f * s_scale * 256.0f);  /* 5 logical pixels */
    int32_t band = (int32_t)(32.0f * s_scale * 256.0f); /* then 3 darker ones */
    /* The rendered set follows the original: no grass lip, the ridge's dark
     * outline (draw.c: 4 wide, 40 % black, half of it inside the ground). */
    if(s_rendered && land3d()) lip = band = (int32_t)(3.0f * s_scale * 256.0f) > 256 ? (int32_t)(3.0f * s_scale * 256.0f) : 256;
    uint32_t acc = s_accent[s_level];
    uint32_t ar = (acc >> 16) & 0xFF, ag = (acc >> 8) & 0xFF, ab = acc & 0xFF;
    for(int y = first_row(g, 0); y < gfx_row_end(g); y += row_step(g)) {
        int m = ((y + s_camy_px) % (2 * t) + 2 * t) % (2 * t);
        int ty = s_level == 0 ? m % t : (m < t ? m : 2 * t - 1 - m);
        const uint32_t * tex = s_tex.px + (size_t)ty * t;
        const uint16_t * tex16 = s_tex16 ? s_tex16 + (size_t)ty * t : NULL;
        uint32_t * d = g->px + (size_t)y * g->stride_px;
        int32_t bottom = (y + 1) * 256, mid = y * 256 + 128;
        if(bottom <= s_edge_min) continue; /* above the ground everywhere */
        if(s_run_n > 0 && s_edge_max <= y * 256 && s_edge_max <= mid - band) {
            /* Below the lip everywhere: plain texture, copied run by run. */
            for(int r = 0; r < s_run_n; r++) {
                const tex_run_t * run = &s_runs[r];
                if(tex16) {
                    if(run->step > 0) expand565(d + run->x, tex16 + run->tx, run->n);
                    else for(int i = 0; i < run->n; i++) d[run->x + i] = from565(tex16[run->tx - i]);
                }
                else if(run->step > 0) memcpy(d + run->x, tex + run->tx, (size_t)run->n * 4);
                else for(int i = 0; i < run->n; i++) d[run->x + i] = tex[run->tx - i];
            }
            continue;
        }
        for(int x = 0; x < g->w; x++) {
            int32_t e = s_edge[x];
            if(bottom <= e) continue;
            uint32_t c = tex16 ? from565(tex16[s_tx[x]]) : tex[s_tx[x]];
            int32_t depth = mid - e;
            if(depth < lip && s_rendered && land3d()) c = 0xFF000000u | ((c & 0x00FEFEFEu) >> 1);
            else if(depth < lip) {
                /* A textured lip follows the collidable surface exactly. */
                uint32_t grain = (c >> 8) & 0xFF, r, gg, b;
                if(s_level == 0) {
                    r = 62 + grain / 3;
                    gg = 91 + grain / 2;
                    b = 35 + grain / 5;
                }
                else {
                    r = ar * (170 + grain / 3) / 255;
                    gg = ag * (170 + grain / 3) / 255;
                    b = ab * (170 + grain / 3) / 255;
                }
                c = 0xFF000000u | (r > 255 ? 255 : r) << 16 | (gg > 255 ? 255 : gg) << 8 | (b > 255 ? 255 : b);
            }
            else if(depth < band) c = 0xFF000000u | ((c & 0x00FEFEFEu) >> 1);
            int32_t cov = bottom - e;
            if(cov >= 256) d[x] = c;
            else {
                /* The surface pixel: anti-aliased against the background. */
                uint32_t a = (uint32_t)cov * 255 / 256;
                uint32_t pm = a << 24 | (((c >> 16) & 0xFF) * a / 255) << 16 | (((c >> 8) & 0xFF) * a / 255) << 8 |
                              ((c & 0xFF) * a / 255);
                d[x] = over(gfx_under(d[x], x, y), pm);
            }
        }
    }

    for(int i = 0; i < s_deco_n; i++) blit(g, s_placed_deco[i].im, s_placed_deco[i].x, s_placed_deco[i].y);
}

void rr_art_monster(gfx_ctx_t * g, const rr_view_t * v, const rr_obstacle_t * obs)
{
    blit_bottom(g, &FIG->enemy.dev, obs->x + obs->w * 0.5f, obs->y + obs->h + 4.0f);
}

void rr_art_mine(gfx_ctx_t * g, const rr_view_t * v, const rr_mine_t * m)
{
    blit_centre(g, &FIG->mine.dev, m->x, m->y);
}

void rr_art_saucer(gfx_ctx_t * g, const rr_view_t * v, const rr_saucer_t * s)
{
    blit_centre(g, &FIG->ufo.dev, s->x, s->y - 8.0f);
}

void rr_art_julian(gfx_ctx_t * g, const rr_view_t * v, const rr_julian_t * j)
{
    blit_bottom(g, &FIG->boss.dev, j->x + RR_JULIAN_W * 0.5f, j->y + RR_JULIAN_H + 4.0f);
}

void rr_art_rob(gfx_ctx_t * g, const rr_view_t * v)
{
    float a = fmodf(v->rob.rotation, 2.0f * (float)M_PI);
    if(a < 0) a += 2.0f * (float)M_PI;
    const int n = rob_frames();
    int k = iround(a / (2.0f * (float)M_PI) * (float)n) % n;
    blit_centre(g, &FIG->rob_frames[k], v->rob.x, v->rob.y);
}
