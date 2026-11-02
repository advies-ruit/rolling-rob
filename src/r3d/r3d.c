/*
 * The 3D rasterizer (see r3d.h).
 *
 * Per frame:
 *   1. geometry (one thread): per object, a bounding-sphere test against the
 *      frustum; per vertex, the world position, Gouraud light (ambient +
 *      one directional light) and fog by distance, then clip coordinates;
 *      per triangle, clipping where it crosses the near plane or a guard band
 *      round the screen (Sutherland-Hodgman, all attributes), the projection,
 *      back-face culling, and the setup: the three corners sorted by y and a
 *      plane a*x + b*y + c for each of 1/w, red, green and blue.
 *   2. binning: each triangle goes into the list of every band of rows it
 *      touches.
 *   3. raster (every core): bands are handed out one at a time; a band is
 *      cleared to the sky gradient and to "infinitely far", its triangles
 *      are drawn span by span (depth test on 1/w, which is linear in screen
 *      space, as a 4.28 fixed-point integer; colours in 16.16 fixed point,
 *      clamped at the span ends), and the band is scaled up by `scale` into
 *      the caller's sink.
 *
 * Measured on a small ARM board: a band lives in a small per-thread
 * buffer that is reused band after band, so it stays in the cache. With one
 * big frame buffer the clear ran at ~50 cycles a pixel: long runs of stores
 * into lines not in the cache make a small ARM core stream them to DRAM (no line
 * allocation), and the spans then missed on every read of the depth.
 */

#include "r3d.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

/* ---------------------------------------------------------------- math */

r3_vec r3_add(r3_vec a, r3_vec b) { return r3v(a.x + b.x, a.y + b.y, a.z + b.z); }
r3_vec r3_sub(r3_vec a, r3_vec b) { return r3v(a.x - b.x, a.y - b.y, a.z - b.z); }
r3_vec r3_scale(r3_vec a, float s) { return r3v(a.x * s, a.y * s, a.z * s); }
float r3_dot(r3_vec a, r3_vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
r3_vec r3_cross(r3_vec a, r3_vec b) { return r3v(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
float r3_len(r3_vec a) { return sqrtf(r3_dot(a, a)); }
r3_vec r3_norm(r3_vec a)
{
    float l = r3_len(a);
    return l > 0 ? r3_scale(a, 1.0f / l) : a;
}

r3_mat r3_identity(void)
{
    r3_mat m = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } };
    return m;
}

r3_mat r3_mul(const r3_mat * a, const r3_mat * b)
{
    r3_mat r;
    for(int c = 0; c < 4; c++)
        for(int row = 0; row < 4; row++) {
            float s = 0;
            for(int k = 0; k < 4; k++) s += a->m[k * 4 + row] * b->m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

r3_mat r3_translate(r3_vec t)
{
    r3_mat m = r3_identity();
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    return m;
}

r3_mat r3_rotate_x(float a)
{
    r3_mat m = r3_identity();
    float c = cosf(a), s = sinf(a);
    m.m[5] = c;
    m.m[6] = s;
    m.m[9] = -s;
    m.m[10] = c;
    return m;
}

r3_mat r3_rotate_y(float a)
{
    r3_mat m = r3_identity();
    float c = cosf(a), s = sinf(a);
    m.m[0] = c;
    m.m[2] = -s;
    m.m[8] = s;
    m.m[10] = c;
    return m;
}

r3_mat r3_rotate_axis(r3_vec ax, float a)
{
    ax = r3_norm(ax);
    float c = cosf(a), s = sinf(a), t = 1 - c;
    r3_mat m = r3_identity();
    m.m[0] = t * ax.x * ax.x + c;
    m.m[1] = t * ax.x * ax.y + s * ax.z;
    m.m[2] = t * ax.x * ax.z - s * ax.y;
    m.m[4] = t * ax.x * ax.y - s * ax.z;
    m.m[5] = t * ax.y * ax.y + c;
    m.m[6] = t * ax.y * ax.z + s * ax.x;
    m.m[8] = t * ax.x * ax.z + s * ax.y;
    m.m[9] = t * ax.y * ax.z - s * ax.x;
    m.m[10] = t * ax.z * ax.z + c;
    return m;
}

r3_mat r3_scale_m(float s)
{
    r3_mat m = r3_identity();
    m.m[0] = m.m[5] = m.m[10] = s;
    return m;
}

r3_mat r3_perspective(float fov_y, float aspect, float n, float f)
{
    r3_mat m;
    memset(&m, 0, sizeof(m));
    float t = 1.0f / tanf(fov_y * 0.5f);
    m.m[0] = t / aspect;
    m.m[5] = t;
    m.m[10] = (f + n) / (n - f);
    m.m[11] = -1;
    m.m[14] = 2 * f * n / (n - f);
    return m;
}

r3_mat r3_look_at(r3_vec eye, r3_vec target, r3_vec up)
{
    r3_vec f = r3_norm(r3_sub(target, eye));
    r3_vec s = r3_norm(r3_cross(f, up));
    r3_vec u = r3_cross(s, f);
    r3_mat m = r3_identity();
    m.m[0] = s.x;
    m.m[4] = s.y;
    m.m[8] = s.z;
    m.m[1] = u.x;
    m.m[5] = u.y;
    m.m[9] = u.z;
    m.m[2] = -f.x;
    m.m[6] = -f.y;
    m.m[10] = -f.z;
    m.m[12] = -r3_dot(s, eye);
    m.m[13] = -r3_dot(u, eye);
    m.m[14] = r3_dot(f, eye);
    return m;
}

r3_vec r3_transform(const r3_mat * m, r3_vec p)
{
    return r3v(m->m[0] * p.x + m->m[4] * p.y + m->m[8] * p.z + m->m[12],
               m->m[1] * p.x + m->m[5] * p.y + m->m[9] * p.z + m->m[13],
               m->m[2] * p.x + m->m[6] * p.y + m->m[10] * p.z + m->m[14]);
}

/* ---------------------------------------------------------------- meshes */

void r3_mesh_init(r3_mesh * m) { memset(m, 0, sizeof(*m)); }

void r3_mesh_free(r3_mesh * m)
{
    free(m->v);
    free(m->idx);
    free(m->soa);
    r3_mesh_init(m);
}

void r3_mesh_clear(r3_mesh * m) { m->nv = m->ni = 0; }

int r3_mesh_vertex(r3_mesh * m, r3_vec p, r3_vec n, uint32_t rgb) { return r3_mesh_vertex_uv(m, p, n, rgb, 0, 0); }

int r3_mesh_vertex_uv(r3_mesh * m, r3_vec p, r3_vec n, uint32_t rgb, float u, float v)
{
    if(m->nv == m->cap_v) {
        int cap = m->cap_v ? m->cap_v * 2 : 64;
        r3_vertex * v = realloc(m->v, (size_t)cap * sizeof(*v));
        if(v == NULL) return 0;
        m->v = v;
        m->cap_v = cap;
    }
    m->v[m->nv] = (r3_vertex){ p.x, p.y, p.z, n.x, n.y, n.z, rgb, u, v };
    return m->nv++;
}

void r3_mesh_tri(r3_mesh * m, int a, int b, int c)
{
    if(m->ni + 3 > m->cap_i) {
        int cap = m->cap_i ? m->cap_i * 2 : 96;
        uint32_t * i = realloc(m->idx, (size_t)cap * sizeof(*i));
        if(i == NULL) return;
        m->idx = i;
        m->cap_i = cap;
    }
    m->idx[m->ni++] = (uint32_t)a;
    m->idx[m->ni++] = (uint32_t)b;
    m->idx[m->ni++] = (uint32_t)c;
}

static void quad(r3_mesh * m, r3_vec a, r3_vec b, r3_vec c, r3_vec d, uint32_t rgb)
{
    r3_vec n = r3_norm(r3_cross(r3_sub(b, a), r3_sub(c, a)));
    float lu = r3_len(r3_sub(b, a)), lv = r3_len(r3_sub(d, a)); /* metres: one repeat per metre */
    int i0 = r3_mesh_vertex_uv(m, a, n, rgb, 0, lv), i1 = r3_mesh_vertex_uv(m, b, n, rgb, lu, lv);
    int i2 = r3_mesh_vertex_uv(m, c, n, rgb, lu, 0), i3 = r3_mesh_vertex_uv(m, d, n, rgb, 0, 0);
    r3_mesh_tri(m, i0, i1, i2);
    r3_mesh_tri(m, i0, i2, i3);
}

void r3_mesh_box(r3_mesh * m, r3_vec lo, r3_vec hi, uint32_t top, uint32_t side)
{
    r3_vec p000 = lo, p111 = hi;
    r3_vec p100 = r3v(hi.x, lo.y, lo.z), p010 = r3v(lo.x, hi.y, lo.z), p001 = r3v(lo.x, lo.y, hi.z);
    r3_vec p110 = r3v(hi.x, hi.y, lo.z), p101 = r3v(hi.x, lo.y, hi.z), p011 = r3v(lo.x, hi.y, hi.z);
    quad(m, p010, p011, p111, p110, top);  /* +y */
    quad(m, p000, p100, p101, p001, side); /* -y */
    quad(m, p001, p101, p111, p011, side); /* +z */
    quad(m, p100, p000, p010, p110, side); /* -z */
    quad(m, p101, p100, p110, p111, side); /* +x */
    quad(m, p000, p001, p011, p010, side); /* -x */
}

void r3_mesh_cylinder(r3_mesh * m, r3_vec b, float r, float h, int seg, uint32_t side, uint32_t cap)
{
    if(seg < 3) seg = 3;
    int first = m->nv;
    for(int i = 0; i <= seg; i++) {
        float a = (float)i / (float)seg * 6.2831853f;
        r3_vec n = r3v(cosf(a), 0, -sinf(a));
        float u = (float)i / (float)seg * 6.2831853f * r;
        r3_mesh_vertex_uv(m, r3v(b.x + n.x * r, b.y, b.z + n.z * r), n, side, u, h);
        r3_mesh_vertex_uv(m, r3v(b.x + n.x * r, b.y + h, b.z + n.z * r), n, side, u, 0);
    }
    for(int i = 0; i < seg; i++) {
        int k = first + i * 2;
        r3_mesh_tri(m, k, k + 2, k + 3);
        r3_mesh_tri(m, k, k + 3, k + 1);
    }
    for(int t = 0; t < 2; t++) {
        float y = b.y + (t ? h : 0);
        r3_vec n = r3v(0, t ? 1.0f : -1.0f, 0);
        int c = r3_mesh_vertex(m, r3v(b.x, y, b.z), n, cap);
        int ring = m->nv;
        for(int i = 0; i < seg; i++) {
            float a = (float)i / (float)seg * 6.2831853f;
            r3_mesh_vertex(m, r3v(b.x + cosf(a) * r, y, b.z - sinf(a) * r), n, cap);
        }
        for(int i = 0; i < seg; i++) {
            int j = (i + 1) % seg;
            if(t) r3_mesh_tri(m, c, ring + i, ring + j);
            else r3_mesh_tri(m, c, ring + j, ring + i);
        }
    }
}

void r3_mesh_cone(r3_mesh * m, r3_vec b, float r, float h, int seg, uint32_t rgb)
{
    if(seg < 3) seg = 3;
    float slope = r / h;
    for(int i = 0; i < seg; i++) {
        float a0 = (float)i / (float)seg * 6.2831853f, a1 = (float)(i + 1) / (float)seg * 6.2831853f;
        float am = (a0 + a1) * 0.5f;
        r3_vec n0 = r3_norm(r3v(cosf(a0), slope, -sinf(a0)));
        r3_vec n1 = r3_norm(r3v(cosf(a1), slope, -sinf(a1)));
        r3_vec nm = r3_norm(r3v(cosf(am), slope, -sinf(am)));
        float slant = sqrtf(r * r + h * h), c0 = (float)i / (float)seg * 6.2831853f * r;
        float c1 = (float)(i + 1) / (float)seg * 6.2831853f * r;
        int k0 = r3_mesh_vertex_uv(m, r3v(b.x + cosf(a0) * r, b.y, b.z - sinf(a0) * r), n0, rgb, c0, slant);
        int k1 = r3_mesh_vertex_uv(m, r3v(b.x + cosf(a1) * r, b.y, b.z - sinf(a1) * r), n1, rgb, c1, slant);
        int kt = r3_mesh_vertex_uv(m, r3v(b.x, b.y + h, b.z), nm, rgb, (c0 + c1) * 0.5f, 0);
        r3_mesh_tri(m, k0, k1, kt);
    }
}

/* An icosphere: subdivided by halving edges (shared through a small table). */
typedef struct { uint32_t a, b; int mid; } edge_t;

static int midpoint(r3_mesh * m, edge_t ** tab, int * n, int * cap, int a, int b, r3_vec c, float r, uint32_t rgb, int base)
{
    uint32_t lo = (uint32_t)(a < b ? a : b), hi = (uint32_t)(a < b ? b : a);
    for(int i = 0; i < *n; i++)
        if((*tab)[i].a == lo && (*tab)[i].b == hi) return (*tab)[i].mid;
    r3_vec pa = r3v(m->v[a].nx, m->v[a].ny, m->v[a].nz), pb = r3v(m->v[b].nx, m->v[b].ny, m->v[b].nz);
    r3_vec nrm = r3_norm(r3_add(pa, pb));
    int k = r3_mesh_vertex(m, r3_add(c, r3_scale(nrm, r)), nrm, rgb);
    if(*n == *cap) {
        *cap = *cap ? *cap * 2 : 256;
        *tab = realloc(*tab, (size_t)*cap * sizeof(edge_t));
    }
    (*tab)[(*n)++] = (edge_t){ lo, hi, k };
    (void)base;
    return k;
}

void r3_mesh_sphere(r3_mesh * m, r3_vec c, float r, int subdiv, uint32_t rgb)
{
    const float t = 1.6180340f;
    r3_vec p[12] = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t },
                     { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
    static const uint8_t f[20][3] = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
                                      { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
                                      { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
                                      { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    int base = m->nv;
    for(int i = 0; i < 12; i++) {
        r3_vec n = r3_norm(p[i]);
        r3_mesh_vertex(m, r3_add(c, r3_scale(n, r)), n, rgb);
    }
    int ntri = 20;
    uint32_t * tris = malloc(sizeof(uint32_t) * 3 * 20);
    for(int i = 0; i < 20; i++)
        for(int k = 0; k < 3; k++) tris[i * 3 + k] = (uint32_t)(base + f[i][k]);
    for(int s = 0; s < subdiv; s++) {
        edge_t * tab = NULL;
        int n = 0, cap = 0;
        uint32_t * nt = malloc(sizeof(uint32_t) * 3 * (size_t)ntri * 4);
        for(int i = 0; i < ntri; i++) {
            int a = (int)tris[i * 3], b = (int)tris[i * 3 + 1], cc = (int)tris[i * 3 + 2];
            int ab = midpoint(m, &tab, &n, &cap, a, b, c, r, rgb, base);
            int bc = midpoint(m, &tab, &n, &cap, b, cc, c, r, rgb, base);
            int ca = midpoint(m, &tab, &n, &cap, cc, a, c, r, rgb, base);
            uint32_t q[12] = { (uint32_t)a, (uint32_t)ab, (uint32_t)ca, (uint32_t)b, (uint32_t)bc, (uint32_t)ab,
                               (uint32_t)cc, (uint32_t)ca, (uint32_t)bc, (uint32_t)ab, (uint32_t)bc, (uint32_t)ca };
            memcpy(nt + (size_t)i * 12, q, sizeof(q));
        }
        free(tab);
        free(tris);
        tris = nt;
        ntri *= 4;
    }
    /* the icosahedron's faces wind clockwise seen from outside: flip */
    for(int i = 0; i < ntri; i++) r3_mesh_tri(m, (int)tris[i * 3], (int)tris[i * 3 + 2], (int)tris[i * 3 + 1]);
    free(tris);
}

void r3_mesh_bounds(r3_mesh * m)
{
    if(m->nv == 0) return;
    r3_vec lo = r3v(m->v[0].px, m->v[0].py, m->v[0].pz), hi = lo;
    for(int i = 1; i < m->nv; i++) {
        lo.x = fminf(lo.x, m->v[i].px);
        lo.y = fminf(lo.y, m->v[i].py);
        lo.z = fminf(lo.z, m->v[i].pz);
        hi.x = fmaxf(hi.x, m->v[i].px);
        hi.y = fmaxf(hi.y, m->v[i].py);
        hi.z = fmaxf(hi.z, m->v[i].pz);
    }
    m->center = r3_scale(r3_add(lo, hi), 0.5f);
    float r2 = 0;
    for(int i = 0; i < m->nv; i++) {
        r3_vec d = r3_sub(r3v(m->v[i].px, m->v[i].py, m->v[i].pz), m->center);
        float l = r3_dot(d, d);
        if(l > r2) r2 = l;
    }
    m->radius = sqrtf(r2);
    /* blocks of four vertices, nine fields each: position, normal, colour
     * (the packed word), u, v -- one stream for the transform to read */
    int n4 = (m->nv + 3) & ~3;
    if(n4 * 9 > m->soa_cap) {
        free(m->soa);
        m->soa_cap = n4 * 9;
        m->soa = malloc(sizeof(float) * (size_t)m->soa_cap);
        if(m->soa == NULL) {
            m->soa_cap = 0;
            return;
        }
    }
    for(int i = 0; i < n4; i++) {
        const r3_vertex * v = &m->v[i < m->nv ? i : m->nv - 1];
        float col;
        memcpy(&col, &v->rgb, 4);
        float f[9] = { v->px, v->py, v->pz, v->nx, v->ny, v->nz, col, v->u, v->v };
        float * blk = m->soa + (size_t)(i / 4) * 36 + (size_t)(i % 4);
        for(int k = 0; k < 9; k++) blk[k * 4] = f[k];
    }
}

/* ---------------------------------------------------------------- textures */

bool r3_texture_init(r3_texture * t, const uint32_t * px, int wl, int hl)
{
    memset(t, 0, sizeof(*t));
    for(int l = 0; l < R3_MIPS; l++) {
        int w = 1 << wl, h = 1 << hl;
        uint32_t * p = malloc((size_t)w * (size_t)h * 4);
        if(p == NULL) return false;
        if(l == 0) memcpy(p, px, (size_t)w * (size_t)h * 4);
        else {
            const uint32_t * q = t->lv[l - 1].px;
            int qw = w * 2;
            for(int y = 0; y < h; y++)
                for(int x = 0; x < w; x++) {
                    uint32_t a = q[(y * 2) * qw + x * 2], b = q[(y * 2) * qw + x * 2 + 1];
                    uint32_t c = q[(y * 2 + 1) * qw + x * 2], d = q[(y * 2 + 1) * qw + x * 2 + 1];
                    uint32_t r = (((a >> 16) & 255) + ((b >> 16) & 255) + ((c >> 16) & 255) + ((d >> 16) & 255) + 2) / 4;
                    uint32_t g = (((a >> 8) & 255) + ((b >> 8) & 255) + ((c >> 8) & 255) + ((d >> 8) & 255) + 2) / 4;
                    uint32_t bl = ((a & 255) + (b & 255) + (c & 255) + (d & 255) + 2) / 4;
                    p[y * w + x] = 0xFF000000u | r << 16 | g << 8 | bl;
                }
        }
        t->lv[l].px = p;
        t->lv[l].wl = wl;
        t->lv[l].hl = hl;
        t->levels = l + 1;
        if(wl <= 2 || hl <= 2) break;
        wl--;
        hl--;
    }
    return true;
}

void r3_texture_free(r3_texture * t)
{
    for(int l = 0; l < t->levels; l++) free(t->lv[l].px);
    memset(t, 0, sizeof(*t));
}

/* ---------------------------------------------------------------- frame state */

typedef struct {
    const r3_mesh * mesh;
    r3_mat model;
} draw_t;

static draw_t * s_draws;
static int s_ndraws, s_cap_draws;
static r3_mat s_view, s_proj, s_vp;
static r3_vec s_eye;
static r3_vec s_to_light = { 0.4f, 0.8f, 0.3f };
static float s_ambient = 0.35f;
static float s_fog_r = 180, s_fog_g = 200, s_fog_b = 230, s_fog_start = 30, s_fog_end = 80;
static uint32_t s_sky_top = 0x3060B0, s_sky_horizon = 0xB4C8E6;
static float s_fog_span; /* 1 / (fog end - fog start), set per frame */
static r3_stats_t s_stats;

void r3_begin(const r3_mat * view, const r3_mat * proj, r3_vec eye)
{
    s_view = *view;
    s_proj = *proj;
    s_vp = r3_mul(proj, view);
    s_eye = eye;
    s_ndraws = 0;
}

void r3_set_light(r3_vec to_light, float ambient)
{
    s_to_light = r3_norm(to_light);
    s_ambient = ambient;
}

void r3_set_fog(uint32_t rgb, float start, float end)
{
    s_fog_r = (float)((rgb >> 16) & 255);
    s_fog_g = (float)((rgb >> 8) & 255);
    s_fog_b = (float)(rgb & 255);
    s_fog_start = start;
    s_fog_end = end > start ? end : start + 1;
}

void r3_set_sky(uint32_t top, uint32_t horizon)
{
    s_sky_top = top;
    s_sky_horizon = horizon;
}

void r3_draw(const r3_mesh * m, const r3_mat * model)
{
    if(m == NULL || m->ni == 0) return;
    if(s_ndraws == s_cap_draws) {
        int cap = s_cap_draws ? s_cap_draws * 2 : 64;
        draw_t * d = realloc(s_draws, (size_t)cap * sizeof(*d));
        if(d == NULL) return;
        s_draws = d;
        s_cap_draws = cap;
    }
    s_draws[s_ndraws++] = (draw_t){ m, *model };
}

const r3_stats_t * r3_stats(void) { return &s_stats; }

/* ---------------------------------------------------------------- geometry */

/* A vertex after the transform, in three groups of four as NEON writes
 * them: clip coordinates (z is not needed) and the clip code; the colour --
 * lit and fogged, or on a textured mesh lit and faded by the fog (the fog's
 * own colour is added per pixel) -- and u; the screen position, 1/w and v. */
typedef struct {
    float x, y, w;
    int code;          /* outside which clip planes */
    float r, g, b, u;
    float sx, sy, iw;  /* on screen, and 1/w (when w > NEAR_W) */
    float v;
} cvert_t;
_Static_assert(sizeof(cvert_t) == 48, "cvert_t: three groups of four floats");

/* A triangle ready for the raster: corners sorted by y, planes for 1/w and
 * the colour. */
typedef struct {
    float x0, y0, x1, y1, x2, y2;  /* top, middle, bottom */
    float d02, d01, d12;           /* dx/dy of the long edge and the two short ones */
    /* planes for 1/w x 2^28, red, green, blue (one per lane, NEON does the
     * four at once): value = pa x + pb y + pc */
    float pa[4], pb[4], pc[4];
    /* textured: planes for u/w, v/w (in texels of the chosen level) and the
     * fog 0..1; the level's pixels */
    float qa[4], qb[4], qc[4];
    const uint32_t * tex;
    uint8_t twl, thl;
    int ya, yb;                    /* first and last+1 row (pixel centres) */
    int16_t b0, b1;                /* first and last band */
} tri_t;

#define MAX_THREADS 4

static cvert_t * s_cv;      /* every visible object's vertices, one after the other */
static int s_cap_cv;
/* the triangles, one list per thread (geometry runs on every core) */
static tri_t * s_tl[MAX_THREADS];
static int s_tn[MAX_THREADS], s_tcap[MAX_THREADS];
/* per thread and band: the triangles counted in setup, then where they go */
#define MAX_BANDS 1024
static int s_bcount[MAX_THREADS][MAX_BANDS];
static uint32_t s_band_inv; /* row * this >> 16 = band (no divide instruction on some ARM cores) */
static int s_bands;
static int s_w, s_h; /* the internal picture */
static int s_band_rows; /* internal rows per band, for this frame */

#define GUARD 3.0f    /* clip only what reaches beyond 3x the screen */
#define ZONE 268435456.0f /* depth: 1/w in 4.28 fixed point (1/w < 8: w > 0.125) */
#define NEAR_W 0.15f  /* clip at w = this (view distance); keeps 1/w < 8 */

/* Prefetch for writing (pldw on 32-bit ARM): makes the core allocate the line in
 * the cache. Without it, a long run of stores into lines that are not cached
 * is streamed to DRAM, and reading the table back misses every time. */
#if defined(__arm__) && !defined(__aarch64__)
/* GCC only emits pldw when told the CPU has the MP extensions (as -mcpu would) */
#define PREFETCH_W(p) __asm__ volatile(".arch_extension mp\n\tpldw [%0]" : : "r"(p))
#else
#define PREFETCH_W(p) __builtin_prefetch((p), 1, 3)
#endif

static tri_t * new_tri(int tid)
{
    if(s_tn[tid] == s_tcap[tid]) {
        int cap = s_tcap[tid] ? s_tcap[tid] * 2 : 2048;
        tri_t * t = realloc(s_tl[tid], (size_t)cap * sizeof(*t));
        if(t == NULL) return NULL;
        s_tl[tid] = t;
        s_tcap[tid] = cap;
    }
    PREFETCH_W(&s_tl[tid][s_tn[tid] + 4]);
    return &s_tl[tid][s_tn[tid]++];
}

static float s_hw, s_hh; /* half the internal size */

static inline void project(cvert_t * v)
{
    v->iw = 1.0f / v->w;
    v->sx = (v->x * v->iw + 1.0f) * s_hw;
    v->sy = (1.0f - v->y * v->iw) * s_hh;
}

static inline int ceil_half(float v) /* ceil(v - 0.5) for v >= 0, else <= 0 */
{
    float f = v - 0.5f;
    int i = (int)f;
    return f > (float)i ? i + 1 : i;
}

/* Sets a projected triangle up; back faces and empty ones go. */
static void setup(const cvert_t * a, const cvert_t * b, const cvert_t * c, const r3_texture * tx, int tid)
{
    float sx[3] = { a->sx, b->sx, c->sx }, sy[3] = { a->sy, b->sy, c->sy }, iw[3] = { a->iw, b->iw, c->iw };
    float det = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if(det >= -1e-4f) return; /* back face (counter-clockwise turns clockwise with y down) or empty */
    float inv = 1.0f / det;
    /* sort by y */
    int o[3] = { 0, 1, 2 };
    if(sy[o[1]] < sy[o[0]]) { int t = o[0]; o[0] = o[1]; o[1] = t; }
    if(sy[o[2]] < sy[o[1]]) { int t = o[1]; o[1] = o[2]; o[2] = t; }
    if(sy[o[1]] < sy[o[0]]) { int t = o[0]; o[0] = o[1]; o[1] = t; }
    int ya = ceil_half(sy[o[0]]), yb = ceil_half(sy[o[2]]);
    if(ya < 0) ya = 0;
    if(yb > s_h) yb = s_h;
    if(ya >= yb) return;
    float minx = sx[0] < sx[1] ? sx[0] : sx[1], maxx = sx[0] < sx[1] ? sx[1] : sx[0];
    minx = sx[2] < minx ? sx[2] : minx;
    maxx = sx[2] > maxx ? sx[2] : maxx;
    if(maxx < 0 || minx > (float)s_w) return;
    tri_t * t = new_tri(tid);
    if(t == NULL) return;
    t->x0 = sx[o[0]];
    t->y0 = sy[o[0]];
    t->x1 = sx[o[1]];
    t->y1 = sy[o[1]];
    t->x2 = sx[o[2]];
    t->y2 = sy[o[2]];
    t->ya = ya;
    t->yb = yb;
    t->b0 = (int16_t)(((uint32_t)ya * s_band_inv) >> 16);
    t->b1 = (int16_t)(((uint32_t)(yb - 1) * s_band_inv) >> 16);
    for(int b = t->b0; b <= t->b1; b++) s_bcount[tid][b]++;
    t->d02 = t->y2 > t->y0 ? (t->x2 - t->x0) / (t->y2 - t->y0) : 0;
    t->d01 = t->y1 > t->y0 ? (t->x1 - t->x0) / (t->y1 - t->y0) : 0;
    t->d12 = t->y2 > t->y1 ? (t->x2 - t->x1) / (t->y2 - t->y1) : 0;
    float dx1 = sx[1] - sx[0], dy1 = sy[1] - sy[0], dx2 = sx[2] - sx[0], dy2 = sy[2] - sy[0];
    float A0[4] = { iw[0] * ZONE, a->r, a->g, a->b };
    float A1[4] = { iw[1] * ZONE, b->r, b->g, b->b };
    float A2[4] = { iw[2] * ZONE, c->r, c->g, c->b };
#if defined(__ARM_NEON)
    float32x4_t a0 = vld1q_f32(A0), d1 = vsubq_f32(vld1q_f32(A1), a0), d2 = vsubq_f32(vld1q_f32(A2), a0);
    float32x4_t pa = vmulq_n_f32(vsubq_f32(vmulq_n_f32(d1, dy2), vmulq_n_f32(d2, dy1)), inv);
    float32x4_t pb = vmulq_n_f32(vsubq_f32(vmulq_n_f32(d2, dx1), vmulq_n_f32(d1, dx2)), inv);
    float32x4_t pc = vmlsq_n_f32(vmlsq_n_f32(a0, pa, sx[0]), pb, sy[0]);
    vst1q_f32(t->pa, pa);
    vst1q_f32(t->pb, pb);
    vst1q_f32(t->pc, pc);
#else
    for(int k = 0; k < 4; k++) {
        float da1 = A1[k] - A0[k], da2 = A2[k] - A0[k];
        t->pa[k] = (da1 * dy2 - da2 * dy1) * inv;
        t->pb[k] = (dx1 * da2 - dx2 * da1) * inv;
        t->pc[k] = A0[k] - t->pa[k] * sx[0] - t->pb[k] * sy[0];
    }
#endif
    t->tex = NULL;
    if(tx == NULL) return;
    /* the texture level: about one texel per pixel (area of the triangle in
     * texels against its area on screen) */
    float tw0 = (float)(1 << tx->lv[0].wl), th0 = (float)(1 << tx->lv[0].hl);
    float du1 = (b->u - a->u) * tw0, dv1 = (b->v - a->v) * th0, du2 = (c->u - a->u) * tw0, dv2 = (c->v - a->v) * th0;
    float ratio = fabsf(du1 * dv2 - du2 * dv1) / -det;
    int l = 0;
    float k = 1.0f;
    while(ratio > 2.5f && l + 1 < tx->levels) {
        ratio *= 0.25f;
        k *= 0.5f;
        l++;
    }
    t->tex = tx->lv[l].px;
    t->twl = (uint8_t)tx->lv[l].wl;
    t->thl = (uint8_t)tx->lv[l].hl;
    float su = tw0 * k, sv = th0 * k;
    const cvert_t * v3[3] = { a, b, c };
    float B[3][4];
    for(int i = 0; i < 3; i++) {
        float fog = (v3[i]->w - s_fog_start) * s_fog_span;
        fog = fog < 0 ? 0 : fog > 1 ? 1 : fog;
        B[i][0] = v3[i]->u * su * iw[i];
        B[i][1] = v3[i]->v * sv * iw[i];
        B[i][2] = fog;
        B[i][3] = 0;
    }
#if defined(__ARM_NEON)
    float32x4_t q0 = vld1q_f32(B[0]), e1 = vsubq_f32(vld1q_f32(B[1]), q0), e2 = vsubq_f32(vld1q_f32(B[2]), q0);
    float32x4_t qa = vmulq_n_f32(vsubq_f32(vmulq_n_f32(e1, dy2), vmulq_n_f32(e2, dy1)), inv);
    float32x4_t qb = vmulq_n_f32(vsubq_f32(vmulq_n_f32(e2, dx1), vmulq_n_f32(e1, dx2)), inv);
    vst1q_f32(t->qa, qa);
    vst1q_f32(t->qb, qb);
    vst1q_f32(t->qc, vmlsq_n_f32(vmlsq_n_f32(q0, qa, sx[0]), qb, sy[0]));
#else
    for(int k2 = 0; k2 < 4; k2++) {
        float da1 = B[1][k2] - B[0][k2], da2 = B[2][k2] - B[0][k2];
        t->qa[k2] = (da1 * dy2 - da2 * dy1) * inv;
        t->qb[k2] = (dx1 * da2 - dx2 * da1) * inv;
        t->qc[k2] = B[0][k2] - t->qa[k2] * sx[0] - t->qb[k2] * sy[0];
    }
#endif
}

/* Distance of a clip-space vertex inside plane p (>= 0 inside). */
static float plane_dist(const cvert_t * v, int p)
{
    switch(p) {
        case 0: return v->w - NEAR_W;
        case 1: return GUARD * v->w - v->x;
        case 2: return GUARD * v->w + v->x;
        case 3: return GUARD * v->w - v->y;
        default: return GUARD * v->w + v->y;
    }
}

static int outcode(const cvert_t * v)
{
    int c = 0;
    for(int p = 0; p < 5; p++)
        if(plane_dist(v, p) < 0) c |= 1 << p;
    return c;
}

static cvert_t lerp_cv(const cvert_t * a, const cvert_t * b, float t)
{
    cvert_t r;
    r.x = a->x + (b->x - a->x) * t;
    r.y = a->y + (b->y - a->y) * t;
    r.w = a->w + (b->w - a->w) * t;
    r.r = a->r + (b->r - a->r) * t;
    r.g = a->g + (b->g - a->g) * t;
    r.b = a->b + (b->b - a->b) * t;
    r.u = a->u + (b->u - a->u) * t;
    r.v = a->v + (b->v - a->v) * t;
    r.code = 0;
    return r;
}

static void clip_and_setup(const cvert_t * a, const cvert_t * b, const cvert_t * c, const r3_texture * tx, int tid)
{
    int ca = a->code, cb = b->code, cc = c->code;
    if(ca & cb & cc) return; /* all outside one plane */
    if((ca | cb | cc) == 0) {
        setup(a, b, c, tx, tid);
        return;
    }
    cvert_t poly[2][12];
    int n = 3, cur = 0;
    poly[0][0] = *a;
    poly[0][1] = *b;
    poly[0][2] = *c;
    int need = ca | cb | cc;
    for(int p = 0; p < 5 && n >= 3; p++) {
        if(!(need & (1 << p))) continue;
        int m = 0;
        for(int i = 0; i < n; i++) {
            const cvert_t * u = &poly[cur][i], * v = &poly[cur][(i + 1) % n];
            float du = plane_dist(u, p), dv = plane_dist(v, p);
            if(du >= 0) poly[cur ^ 1][m++] = *u;
            if((du >= 0) != (dv >= 0) && m < 12) poly[cur ^ 1][m++] = lerp_cv(u, v, du / (du - dv));
        }
        n = m;
        cur ^= 1;
    }
    for(int i = 0; i < n; i++) project(&poly[cur][i]);
    for(int i = 1; i + 1 < n; i++) setup(&poly[cur][0], &poly[cur][i], &poly[cur][i + 1], tx, tid);
}

/* The six planes of the frustum (a, b, c, d: a x + b y + c z + d >= 0 inside). */
static float s_frustum[6][4];

static void frustum_planes(void)
{
    const float * m = s_vp.m;
    for(int i = 0; i < 6; i++) {
        int row = i / 2;
        float sgn = (i & 1) ? -1.0f : 1.0f;
        for(int k = 0; k < 4; k++) s_frustum[i][k] = m[k * 4 + 3] + sgn * m[k * 4 + row];
        float l = sqrtf(s_frustum[i][0] * s_frustum[i][0] + s_frustum[i][1] * s_frustum[i][1] + s_frustum[i][2] * s_frustum[i][2]);
        for(int k = 0; k < 4; k++) s_frustum[i][k] /= l;
    }
}

static double now_ms(void);
static void parallel(int jobs, void (*fn)(int job, int tid));

/* A visible object: its transform for this frame and where its vertices go. */
typedef struct {
    const r3_mesh * mesh;
    r3_mat mvp;
    float lx, ly, lz; /* the light in the mesh's own space, scale divided out */
    int cv0;          /* its first vertex in s_cv */
} vis_t;

/* A piece of work: vertices v0..v1 or triangles (index i0..i1) of one object. */
typedef struct { int vis, a, b; } chunk_t;

static vis_t * s_vis;
static int s_nvis, s_cap_vis;
static chunk_t * s_chunks;
static int s_nchunks, s_cap_chunks;
#define CHUNK 384

static void add_chunk(int vis, int a, int b)
{
    if(s_nchunks == s_cap_chunks) {
        s_cap_chunks = s_cap_chunks ? s_cap_chunks * 2 : 256;
        s_chunks = realloc(s_chunks, (size_t)s_cap_chunks * sizeof(chunk_t));
    }
    s_chunks[s_nchunks++] = (chunk_t){ vis, a, b };
}

/* One vertex, the plain way (no NEON, and the last 1-3 of a chunk). */
static void xform_one(const vis_t * vs, int i, cvert_t * o)
{
    const r3_vertex * v = &vs->mesh->v[i];
    const float * m = vs->mvp.m;
    float px = v->px, py = v->py, pz = v->pz;
    o->x = m[0] * px + m[4] * py + m[8] * pz + m[12];
    o->y = m[1] * px + m[5] * py + m[9] * pz + m[13];
    o->w = m[3] * px + m[7] * py + m[11] * pz + m[15];
    float ndl = v->nx * vs->lx + v->ny * vs->ly + v->nz * vs->lz;
    float light = s_ambient + (1.0f - s_ambient) * (ndl > 0 ? ndl : 0);
    /* fog by view depth (w), not by distance: no square root */
    float fog = (o->w - s_fog_start) * s_fog_span;
    fog = fog < 0 ? 0 : fog > 1 ? 1 : fog;
    float keep = (1 - fog) * light, add = vs->mesh->tex ? 0 : fog; /* textured: fog added per pixel */
    uint32_t rgb = v->rgb;
    o->r = (float)((rgb >> 16) & 255) * keep + s_fog_r * add;
    o->g = (float)((rgb >> 8) & 255) * keep + s_fog_g * add;
    o->b = (float)(rgb & 255) * keep + s_fog_b * add;
    o->u = v->u;
    o->v = v->v;
    o->code = outcode(o);
    if(o->code == 0) project(o);
}

#if defined(__ARM_NEON)
/* The columns of a 4x4 block become its rows (lane i of each -> vector i). */
static inline void transpose4(float32x4_t * a, float32x4_t * b, float32x4_t * c, float32x4_t * d)
{
    float32x4x2_t ab = vtrnq_f32(*a, *b), cd = vtrnq_f32(*c, *d);
    *a = vcombine_f32(vget_low_f32(ab.val[0]), vget_low_f32(cd.val[0]));
    *b = vcombine_f32(vget_low_f32(ab.val[1]), vget_low_f32(cd.val[1]));
    *c = vcombine_f32(vget_high_f32(ab.val[0]), vget_high_f32(cd.val[0]));
    *d = vcombine_f32(vget_high_f32(ab.val[1]), vget_high_f32(cd.val[1]));
}
#endif

/* Measured on a small ARM board: scalar float waits ~12 cycles on every
 * dependent multiply-add (~330 cycles a vertex in all). NEON does four
 * vertices per instruction, reading them from the mesh's blocks of four
 * (one stream: nine separate ones were more than the prefetcher follows) and
 * writing each vertex's three groups of four with one store each. */
static void xform_job(int job, int tid)
{
    (void)tid;
    const chunk_t * ch = &s_chunks[job];
    const vis_t * vs = &s_vis[ch->vis];
    const r3_mesh * mesh = vs->mesh;
    (void)mesh; /* only the NEON path reads it */
    cvert_t * out = s_cv + vs->cv0;
    int i = ch->a;
#if defined(__ARM_NEON)
    if(mesh->soa) {
        /* The scalars four to a register (used as lanes), and each group
         * written out as soon as it is complete: with a vector per constant
         * the loop needed more than the 16 NEON registers and went through
         * the stack (78 stack accesses an iteration). */
        const float * m = vs->mvp.m;
        const float kx[4] = { m[0], m[4], m[8], m[12] }, ky[4] = { m[1], m[5], m[9], m[13] };
        const float kw[4] = { m[3], m[7], m[11], m[15] }, kl[4] = { vs->lx, vs->ly, vs->lz, s_ambient };
        const float kf[4] = { 1.0f - s_ambient, s_fog_start, s_fog_span, mesh->tex ? 0.0f : 1.0f };
        const float kc[4] = { s_fog_r, s_fog_g, s_fog_b, 0 }, ks[4] = { s_hw, s_hh, NEAR_W, GUARD };
        float32x4_t KX = vld1q_f32(kx), KY = vld1q_f32(ky), KW = vld1q_f32(kw), KL = vld1q_f32(kl);
        float32x4_t KF = vld1q_f32(kf), KC = vld1q_f32(kc), KS = vld1q_f32(ks);
        for(; i + 4 <= ch->b; i += 4) {
            const float * P = mesh->soa + (size_t)(i / 4) * 36;
            float * o = (float *)&out[i];
            __builtin_prefetch(P + 72);
            PREFETCH_W(o + 48);
            PREFETCH_W(o + 64);
            PREFETCH_W(o + 80);
            float32x4_t px = vld1q_f32(P), py = vld1q_f32(P + 4), pz = vld1q_f32(P + 8);
            float32x4_t x = vmlaq_lane_f32(vmlaq_lane_f32(vmlaq_lane_f32(vdupq_lane_f32(vget_high_f32(KX), 1), px, vget_low_f32(KX), 0),
                                                          py, vget_low_f32(KX), 1), pz, vget_high_f32(KX), 0);
            float32x4_t y = vmlaq_lane_f32(vmlaq_lane_f32(vmlaq_lane_f32(vdupq_lane_f32(vget_high_f32(KY), 1), px, vget_low_f32(KY), 0),
                                                          py, vget_low_f32(KY), 1), pz, vget_high_f32(KY), 0);
            float32x4_t w = vmlaq_lane_f32(vmlaq_lane_f32(vmlaq_lane_f32(vdupq_lane_f32(vget_high_f32(KW), 1), px, vget_low_f32(KW), 0),
                                                          py, vget_low_f32(KW), 1), pz, vget_high_f32(KW), 0);
            /* fog from the view depth, before w is packed away */
            float32x4_t fog = vmulq_lane_f32(vsubq_f32(w, vdupq_lane_f32(vget_low_f32(KF), 1)), vget_high_f32(KF), 0);
            fog = vminq_f32(vmaxq_f32(fog, vdupq_n_f32(0)), vdupq_n_f32(1.0f));
            /* outside which planes, as in outcode(): each mask's top bit to its place */
            float32x4_t gw = vmulq_lane_f32(w, vget_high_f32(KS), 1);
            uint32x4_t code = vshrq_n_u32(vcltq_f32(w, vdupq_lane_f32(vget_high_f32(KS), 0)), 31);
            code = vsraq_n_u32(code, vandq_u32(vcltq_f32(gw, x), vdupq_n_u32(0x80000000u)), 30);
            code = vsraq_n_u32(code, vandq_u32(vcltq_f32(vaddq_f32(gw, x), vdupq_n_f32(0)), vdupq_n_u32(0x80000000u)), 29);
            code = vsraq_n_u32(code, vandq_u32(vcltq_f32(gw, y), vdupq_n_u32(0x80000000u)), 28);
            code = vsraq_n_u32(code, vandq_u32(vcltq_f32(vaddq_f32(gw, y), vdupq_n_f32(0)), vdupq_n_u32(0x80000000u)), 27);
            /* 1/w: estimate + two Newton steps (used only where w > NEAR_W) */
            float32x4_t iw = vrecpeq_f32(w);
            iw = vmulq_f32(iw, vrecpsq_f32(w, iw));
            iw = vmulq_f32(iw, vrecpsq_f32(w, iw));
            float32x4_t sx = vmulq_lane_f32(vmlaq_f32(vdupq_n_f32(1.0f), x, iw), vget_low_f32(KS), 0);
            float32x4_t sy = vmulq_lane_f32(vmlsq_f32(vdupq_n_f32(1.0f), y, iw), vget_low_f32(KS), 1);
            float32x4_t cf = vreinterpretq_f32_u32(code);
            transpose4(&x, &y, &w, &cf);
            vst1q_f32(o, x);
            vst1q_f32(o + 12, y);
            vst1q_f32(o + 24, w);
            vst1q_f32(o + 36, cf);
            float32x4_t tv = vld1q_f32(P + 32);
            transpose4(&sx, &sy, &iw, &tv);
            vst1q_f32(o + 8, sx);
            vst1q_f32(o + 20, sy);
            vst1q_f32(o + 32, iw);
            vst1q_f32(o + 44, tv);
            /* light, and the colour */
            float32x4_t ndl = vmlaq_lane_f32(vmlaq_lane_f32(vmulq_lane_f32(vld1q_f32(P + 12), vget_low_f32(KL), 0),
                                                            vld1q_f32(P + 16), vget_low_f32(KL), 1),
                                             vld1q_f32(P + 20), vget_high_f32(KL), 0);
            float32x4_t light = vmlaq_lane_f32(vdupq_lane_f32(vget_high_f32(KL), 1), vmaxq_f32(ndl, vdupq_n_f32(0)), vget_low_f32(KF), 0);
            float32x4_t keep = vmulq_f32(vsubq_f32(vdupq_n_f32(1.0f), fog), light);
            float32x4_t add = vmulq_lane_f32(fog, vget_high_f32(KF), 1);
            uint32x4_t col = vld1q_u32((const uint32_t *)(P + 24));
            float32x4_t r = vmlaq_f32(vmulq_lane_f32(add, vget_low_f32(KC), 0), vcvtq_f32_u32(vshrq_n_u32(vshlq_n_u32(col, 8), 24)), keep);
            float32x4_t g = vmlaq_f32(vmulq_lane_f32(add, vget_low_f32(KC), 1), vcvtq_f32_u32(vshrq_n_u32(vshlq_n_u32(col, 16), 24)), keep);
            float32x4_t bl = vmlaq_f32(vmulq_lane_f32(add, vget_high_f32(KC), 0), vcvtq_f32_u32(vshrq_n_u32(vshlq_n_u32(col, 24), 24)), keep);
            float32x4_t tu = vld1q_f32(P + 28);
            transpose4(&r, &g, &bl, &tu);
            vst1q_f32(o + 4, r);
            vst1q_f32(o + 16, g);
            vst1q_f32(o + 28, bl);
            vst1q_f32(o + 40, tu);
        }
    }
#endif
    for(; i < ch->b; i++) xform_one(vs, i, &out[i]);
}

static void tri_job(int job, int tid)
{
    const chunk_t * ch = &s_chunks[job];
    const vis_t * vs = &s_vis[ch->vis];
    const uint32_t * idx = vs->mesh->idx;
    const cvert_t * cv = s_cv + vs->cv0;
    const r3_texture * tx = vs->mesh->tex;
    for(int i = ch->a; i < ch->b; i += 3) clip_and_setup(&cv[idx[i]], &cv[idx[i + 1]], &cv[idx[i + 2]], tx, tid);
}

static void geometry(void)
{
    frustum_planes();
    s_stats.objects = s_ndraws;
    s_stats.verts = 0;
    s_stats.tris_in = 0;
    s_fog_span = 1.0f / (s_fog_end - s_fog_start);
    /* which objects are in view, and their vertices' place */
    if(s_cap_vis < s_ndraws) {
        s_cap_vis = s_ndraws * 2;
        s_vis = realloc(s_vis, (size_t)s_cap_vis * sizeof(vis_t));
    }
    s_nvis = 0;
    int nv = 0;
    for(int d = 0; d < s_ndraws; d++) {
        const r3_mesh * mesh = s_draws[d].mesh;
        const r3_mat * M = &s_draws[d].model;
        r3_vec c = r3_transform(M, mesh->center);
        float sc = sqrtf(M->m[0] * M->m[0] + M->m[1] * M->m[1] + M->m[2] * M->m[2]);
        float r = mesh->radius * sc;
        bool out = false;
        for(int p = 0; p < 6 && !out; p++)
            out = s_frustum[p][0] * c.x + s_frustum[p][1] * c.y + s_frustum[p][2] * c.z + s_frustum[p][3] < -r;
        if(out) continue;
        vis_t * vs = &s_vis[s_nvis++];
        vs->mesh = mesh;
        vs->mvp = r3_mul(&s_vp, M);
        float inv_sc = sc > 0 ? 1.0f / sc : 1.0f;
        r3_vec L = s_to_light;
        vs->lx = (M->m[0] * L.x + M->m[1] * L.y + M->m[2] * L.z) * inv_sc;
        vs->ly = (M->m[4] * L.x + M->m[5] * L.y + M->m[6] * L.z) * inv_sc;
        vs->lz = (M->m[8] * L.x + M->m[9] * L.y + M->m[10] * L.z) * inv_sc;
        vs->cv0 = nv;
        nv += mesh->nv;
        s_stats.verts += mesh->nv;
        s_stats.tris_in += mesh->ni / 3;
    }
    s_stats.objects_drawn = s_nvis;
    if(nv > s_cap_cv) {
        free(s_cv);
        s_cap_cv = nv * 2;
        s_cv = malloc((size_t)s_cap_cv * sizeof(cvert_t));
        if(s_cv == NULL) {
            s_cap_cv = 0;
            s_nvis = 0;
        }
    }
    /* vertices on every core, then the triangles */
    double t0 = now_ms();
    s_nchunks = 0;
    for(int i = 0; i < s_nvis; i++)
        for(int a = 0; a < s_vis[i].mesh->nv; a += CHUNK)
            add_chunk(i, a, a + CHUNK < s_vis[i].mesh->nv ? a + CHUNK : s_vis[i].mesh->nv);
    parallel(s_nchunks, xform_job);
    double t1 = now_ms();
    s_nchunks = 0;
    for(int i = 0; i < s_nvis; i++)
        for(int a = 0; a < s_vis[i].mesh->ni; a += CHUNK * 3)
            add_chunk(i, a, a + CHUNK * 3 < s_vis[i].mesh->ni ? a + CHUNK * 3 : s_vis[i].mesh->ni);
    for(int t = 0; t < MAX_THREADS; t++) {
        s_tn[t] = 0;
        memset(s_bcount[t], 0, sizeof(int) * (size_t)s_bands);
    }
    parallel(s_nchunks, tri_job);
    s_stats.xform_ms = (float)(t1 - t0);
    s_stats.tri_ms = (float)(now_ms() - t1);
    s_stats.tris_drawn = 0;
    for(int t = 0; t < MAX_THREADS; t++) s_stats.tris_drawn += s_tn[t];
}

/* ---------------------------------------------------------------- binning */

static const tri_t ** s_bin; /* the triangles, band by band */
static int s_bin_off[MAX_BANDS + 1]; /* start of each band's list in s_bin, and the end */
static int s_cap_bin;

/* Thread list `job`'s triangles into their bands (each list writes its own
 * places, worked out in binning()). */
static void fill_job(int job, int tid)
{
    (void)tid;
    int * at = s_bcount[job];
    for(int i = 0; i < s_tn[job]; i++) {
        const tri_t * tr = &s_tl[job][i];
        for(int b = tr->b0; b <= tr->b1; b++) s_bin[at[b]++] = tr;
    }
}

static void binning(void)
{
    /* counted per thread and band in setup(): turn the counts into places */
    int off = 0;
    for(int b = 0; b < s_bands; b++) {
        s_bin_off[b] = off;
        for(int t = 0; t < MAX_THREADS; t++) {
            int n = s_bcount[t][b];
            s_bcount[t][b] = off;
            off += n;
        }
    }
    s_bin_off[s_bands] = off;
    if(off > s_cap_bin) {
        free(s_bin);
        s_cap_bin = off * 2;
        s_bin = malloc(sizeof(*s_bin) * (size_t)s_cap_bin);
    }
    parallel(MAX_THREADS, fill_job);
}

/* ---------------------------------------------------------------- raster */

static uint32_t * s_sky_rows; /* one colour per internal row */
static int s_cap_sky;

/* floor(v + 0.5) for v >= 0 -- the first pixel centre at or right of v - 0.5;
 * negative values only need to come out <= 0 (they are clamped) */
static inline int first_px(float v) { return v > 0 ? (int)(v + 0.5f) : 0; }

#if !defined(__ARM_NEON)
static inline int clamp_c(int v) { return v < 0 ? 0 : v > (255 << 16) ? (255 << 16) : v; }
#endif

static void span(const tri_t * t, int y, uint32_t * crow, uint32_t * zrow)
{
    float yc = (float)y + 0.5f;
    /* x on the long edge (top -> bottom) and on the short one */
    float xl = t->x0 + (yc - t->y0) * t->d02;
    float xs = yc < t->y1 ? t->x0 + (yc - t->y0) * t->d01 : t->x1 + (yc - t->y1) * t->d12;
    float a = xl < xs ? xl : xs, b = xl < xs ? xs : xl;
    int x0 = first_px(a), x1 = first_px(b);
    if(x1 > s_w) x1 = s_w;
    int n = x1 - x0;
    if(n <= 0) return;
    float xc = (float)x0 + 0.5f;
    uint32_t z, dz;
    int r0, g0, b0, dr, dg, db;
#if defined(__ARM_NEON)
    /* the four planes at both ends of the span; colours clamped there and
     * stepped in between (so the steps never leave 0..255) */
    static const float k_scale[4] = { 1.0f, 65536.0f, 65536.0f, 65536.0f };
    float32x4_t sc = vld1q_f32(k_scale);
    float32x4_t pa = vld1q_f32(t->pa);
    float32x4_t vs = vmlaq_n_f32(vmlaq_n_f32(vld1q_f32(t->pc), vld1q_f32(t->pb), yc), pa, xc);
    float32x4_t ve = vmlaq_n_f32(vs, pa, (float)(n - 1));
    int32x4_t is = vcvtq_s32_f32(vmulq_f32(vs, sc)), ie = vcvtq_s32_f32(vmulq_f32(ve, sc));
    static const int32_t k_hi[4] = { 0x7FFFFFFF, 255 << 16, 255 << 16, 255 << 16 }; /* depth: not clamped */
    int32x4_t lo = vdupq_n_s32(0), hi = vld1q_s32(k_hi);
    is = vminq_s32(vmaxq_s32(is, lo), hi);
    ie = vminq_s32(vmaxq_s32(ie, lo), hi);
    float inv = n > 1 ? 1.0f / (float)(n - 1) : 0;
    int32x4_t d = vcvtq_s32_f32(vmulq_n_f32(vcvtq_f32_s32(vsubq_s32(ie, is)), inv));
    z = (uint32_t)vgetq_lane_s32(is, 0);
    dz = (uint32_t)(int32_t)t->pa[0];
    r0 = vgetq_lane_s32(is, 1);
    g0 = vgetq_lane_s32(is, 2);
    b0 = vgetq_lane_s32(is, 3);
    dr = vgetq_lane_s32(d, 1);
    dg = vgetq_lane_s32(d, 2);
    db = vgetq_lane_s32(d, 3);
#else
    float zf = t->pa[0] * xc + t->pb[0] * yc + t->pc[0];
    z = zf > 0 ? (uint32_t)zf : 0;
    dz = (uint32_t)(int32_t)t->pa[0];
    float xe = xc + (float)(n - 1);
    int v0[3], v1[3];
    for(int k = 0; k < 3; k++) {
        v0[k] = clamp_c((int)((t->pa[k + 1] * xc + t->pb[k + 1] * yc + t->pc[k + 1]) * 65536.0f));
        v1[k] = clamp_c((int)((t->pa[k + 1] * xe + t->pb[k + 1] * yc + t->pc[k + 1]) * 65536.0f));
    }
    float inv = n > 1 ? 1.0f / (float)(n - 1) : 0;
    r0 = v0[0];
    g0 = v0[1];
    b0 = v0[2];
    dr = (int)((float)(v1[0] - v0[0]) * inv);
    dg = (int)((float)(v1[1] - v0[1]) * inv);
    db = (int)((float)(v1[2] - v0[2]) * inv);
#endif
    uint32_t * c = crow + x0;
    uint32_t * zp = zrow + x0;
    for(int i = 0; i < n; i++) {
        if(z > zp[i]) {
            zp[i] = z;
            c[i] = 0xFF000000u | ((uint32_t)(r0 >> 16) << 16) | ((uint32_t)(g0 >> 16) << 8) | (uint32_t)(b0 >> 16);
        }
        z += dz;
        r0 += dr;
        g0 += dg;
        b0 += db;
    }
}

/* A span of a textured triangle. The colour planes hold the vertex colour
 * times the light, faded by the fog; per pixel: texel x that colour + fog
 * colour x fog. u and v are exact (divided by 1/w) every 16 pixels and
 * stepped in between; nearest texel, wrapping. Light and fog are taken once
 * per 16 pixels too: with them stepped per pixel the loop had more live
 * values than a 32-bit ARM core has registers, and every pixel went through the stack
 * (~125 cycles a pixel; the steps are not visible). */
static void span_tex(const tri_t * t, int y, uint32_t * crow, uint32_t * zrow)
{
    float yc = (float)y + 0.5f;
    float xl = t->x0 + (yc - t->y0) * t->d02;
    float xs = yc < t->y1 ? t->x0 + (yc - t->y0) * t->d01 : t->x1 + (yc - t->y1) * t->d12;
    float a = xl < xs ? xl : xs, b = xl < xs ? xs : xl;
    int x0 = first_px(a), x1 = first_px(b);
    if(x1 > s_w) x1 = s_w;
    int n = x1 - x0;
    if(n <= 0) return;
    float xc = (float)x0 + 0.5f;
    float zf = t->pa[0] * xc + t->pb[0] * yc + t->pc[0];
    uint32_t z = zf > 0 ? (uint32_t)zf : 0, dz = (uint32_t)(int32_t)t->pa[0];
    const uint32_t * tex = t->tex;
    int wl = t->twl;
    uint32_t um = (1u << t->twl) - 1, vm = (1u << t->thl) - 1;
    uint32_t * c = crow + x0;
    uint32_t * zp = zrow + x0;
    /* the planes along this row: value(x) = row + a x */
    float row_w = t->pb[0] * yc + t->pc[0], aw = t->pa[0];
    float row_u = t->qb[0] * yc + t->qc[0], au = t->qa[0];
    float row_v = t->qb[1] * yc + t->qc[1], av = t->qa[1];
    float row_f = t->qb[2] * yc + t->qc[2], af = t->qa[2];
    float row_c[3], ac[3];
    for(int k = 0; k < 3; k++) {
        row_c[k] = t->pb[k + 1] * yc + t->pc[k + 1];
        ac[k] = t->pa[k + 1];
    }
    float x = xc;
    float iw = (row_w + aw * x) * (1.0f / ZONE);
    float rw = iw > 1e-9f ? 1.0f / iw : 0;
    int32_t u = (int32_t)((row_u + au * x) * rw * 65536.0f);
    int32_t v = (int32_t)((row_v + av * x) * rw * 65536.0f);
    for(int i = 0; i < n;) {
        int seg = n - i < 16 ? n - i : 16;
        float xn = x + (float)seg;
        float iwn = (row_w + aw * xn) * (1.0f / ZONE);
        float rwn = iwn > 1e-9f ? 1.0f / iwn : 0;
        int32_t un = (int32_t)((row_u + au * xn) * rwn * 65536.0f);
        int32_t vn = (int32_t)((row_v + av * xn) * rwn * 65536.0f);
        int32_t du, dv;
        if(seg == 16) {
            du = (un - u) >> 4;
            dv = (vn - v) >> 4;
        }
        else {
            float k = 1.0f / (float)seg;
            du = (int32_t)((float)(un - u) * k);
            dv = (int32_t)((float)(vn - v) * k);
        }
        /* light and fog at the middle of these pixels */
        float xm = x + (float)seg * 0.5f - 0.5f;
        float fog = row_f + af * xm;
        fog = fog < 0 ? 0 : fog > 1 ? 1 : fog;
        uint32_t m[3];
        for(int k = 0; k < 3; k++) {
            float mv = row_c[k] + ac[k] * xm;
            mv = mv < 0 ? 0 : mv > 255 ? 255 : mv;
            m[k] = (uint32_t)(mv * 256.0f); /* 8.8 */
        }
        uint32_t fogw = (uint32_t)(s_fog_r * fog) << 16 | (uint32_t)(s_fog_g * fog) << 8 | (uint32_t)(s_fog_b * fog);
        uint32_t mr = m[0], mg = m[1], mb = m[2];
        for(int e = i + seg; i < e; i++) {
            if(z > zp[i]) {
                zp[i] = z;
                uint32_t tx = tex[((((uint32_t)v >> 16) & vm) << wl) | (((uint32_t)u >> 16) & um)];
                /* texel x colour stays below 256 - fog, so the packed add
                 * of the fog never carries */
                c[i] = 0xFF000000u + fogw + ((((tx >> 16) & 255) * mr) >> 16 << 16) + ((((tx >> 8) & 255) * mg) >> 16 << 8) +
                       (((tx & 255) * mb) >> 16);
            }
            z += dz;
            u += du;
            v += dv;
        }
        x = xn;
        u = un;
        v = vn;
    }
}

/* ---------------------------------------------------------------- threads */

static int s_threads, s_want_threads;
static pthread_mutex_t s_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_go = PTHREAD_COND_INITIALIZER, s_done = PTHREAD_COND_INITIALIZER;
static unsigned s_gen;
/* One set of jobs per call of parallel(); two, so a thread that wakes late
 * for the last set can only ever join the current one (see run_jobs). */
typedef struct {
    void (*fn)(int job, int tid);
    int njobs;
    int next, done; /* atomic */
} jobset_t;
static jobset_t s_js[2];
static bool s_bottom_up;
static int s_out_w, s_out_h, s_scale;
static r3_sink_t s_sink;
static void * s_user;

void r3_set_band_order(bool bottom_up) { s_bottom_up = bottom_up; }
void r3_set_threads(int n) { s_want_threads = n; }

/* Per thread, reused band after band (so they stay in the cache): the
 * band's colour and depth at the internal size, and the scaled-up rows. */
static _Thread_local uint32_t * t_color, * t_depth, * t_out;
static _Thread_local size_t t_band_size, t_out_size;

static int64_t s_draw_ns, s_out_ns; /* atomic */

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static void out_band(int band)
{
    int64_t t0 = now_ns();
    int y0 = band * s_band_rows, y1 = y0 + s_band_rows < s_h ? y0 + s_band_rows : s_h;
    size_t need = (size_t)s_w * (size_t)s_band_rows;
    if(t_band_size < need) {
        free(t_color);
        free(t_depth);
        t_color = malloc(need * 4);
        t_depth = malloc(need * 4);
        t_band_size = t_color && t_depth ? need : 0;
        if(t_band_size == 0) return;
    }
    /* clear: sky and far away */
    for(int y = y0; y < y1; y++) {
        uint32_t * c = t_color + (size_t)(y - y0) * s_w;
        uint32_t * z = t_depth + (size_t)(y - y0) * s_w;
        uint32_t sky = s_sky_rows[y];
        for(int x = 0; x < s_w; x += 16) {
            PREFETCH_W(c + x);
            PREFETCH_W(z + x);
        }
        for(int x = 0; x < s_w; x++) {
            c[x] = sky;
            z[x] = 0;
        }
    }
    for(int k = s_bin_off[band]; k < s_bin_off[band + 1]; k++) {
        const tri_t * t = s_bin[k];
        int ya = t->ya > y0 ? t->ya : y0, yb = t->yb < y1 ? t->yb : y1;
        if(t->tex)
            for(int y = ya; y < yb; y++) span_tex(t, y, t_color + (size_t)(y - y0) * s_w, t_depth + (size_t)(y - y0) * s_w);
        else
            for(int y = ya; y < yb; y++) span(t, y, t_color + (size_t)(y - y0) * s_w, t_depth + (size_t)(y - y0) * s_w);
    }
    int64_t t1 = now_ns();
    __atomic_add_fetch(&s_draw_ns, t1 - t0, __ATOMIC_RELAXED);
    /* out, scaled up */
    int oy0 = y0 * s_scale, oy1 = y1 * s_scale;
    if(oy1 > s_out_h) oy1 = s_out_h;
    if(oy0 >= oy1) return;
    if(s_scale == 1 && s_out_w == s_w) {
        s_sink(t_color, s_w, oy0, oy1 - oy0, s_user);
        __atomic_add_fetch(&s_out_ns, now_ns() - t1, __ATOMIC_RELAXED);
        return;
    }
    /* one internal row at a time: its `scale` output rows stay small enough
     * for the cache (a whole band's rows at 4x were 184 KB: through DRAM) */
    size_t oneed = (size_t)s_out_w * (size_t)s_scale * 4;
    if(t_out_size < oneed) {
        free(t_out);
        t_out = malloc(oneed);
        t_out_size = t_out ? oneed : 0;
        if(t_out == NULL) return;
    }
    for(int y = y0; y < y1; y++) {
        int oy = y * s_scale, rows = oy + s_scale <= s_out_h ? s_scale : s_out_h - oy;
        if(rows <= 0) break;
        const uint32_t * src = t_color + (size_t)(y - y0) * s_w;
        uint32_t * dst = t_out;
        for(int x = 0; x < s_out_w; x += 16) PREFETCH_W(dst + x);
        int x = 0;
#if defined(__ARM_NEON)
        if(s_scale == 2)
            for(; x + 8 <= s_out_w; x += 8) {
                uint32x4_t p = vld1q_u32(src + x / 2);
                uint32x4x2_t d = vzipq_u32(p, p);
                vst1q_u32(dst + x, d.val[0]);
                vst1q_u32(dst + x + 4, d.val[1]);
            }
        if(s_scale == 3)
            for(; x + 12 <= s_out_w; x += 12) {
                uint32x4_t p = vld1q_u32(src + x / 3);
                uint32x4x3_t t3 = { { p, p, p } };
                vst3q_u32(dst + x, t3); /* p0 p0 p0 p1 p1 p1 ... */
            }
        if(s_scale == 4)
            for(; x + 16 <= s_out_w; x += 16) {
                uint32x4_t p = vld1q_u32(src + x / 4);
                uint32x4x4_t t4 = { { p, p, p, p } };
                vst4q_u32(dst + x, t4);
            }
#endif
        for(; x < s_out_w; x++) dst[x] = src[x / s_scale];
        for(int r = 1; r < rows; r++) {
            uint32_t * d = t_out + (size_t)r * s_out_w;
            for(int k = 0; k < s_out_w; k += 16) PREFETCH_W(d + k);
            memcpy(d, t_out, (size_t)s_out_w * 4);
        }
        s_sink(t_out, s_out_w, oy, rows, s_user);
    }
    __atomic_add_fetch(&s_out_ns, now_ns() - t1, __ATOMIC_RELAXED);
}

static void band_job(int job, int tid)
{
    (void)tid;
    out_band(s_bottom_up ? s_bands - 1 - job : job);
}

/* Takes jobs until none are left. The caller of parallel() waits for the
 * jobs to be done, not for the threads: a worker the scheduler wakes late
 * (the cores are shared with LVGL and other programs) just
 * finds nothing left. Measured on a small ARM board: waiting for every worker cost
 * up to ~6 ms a call. */
static void run_jobs(jobset_t * js, int tid)
{
    for(;;) {
        int j = __atomic_fetch_add(&js->next, 1, __ATOMIC_ACQUIRE);
        if(j >= js->njobs) break;
        js->fn(j, tid);
        if(__atomic_add_fetch(&js->done, 1, __ATOMIC_ACQ_REL) == js->njobs) {
            pthread_mutex_lock(&s_mx);
            pthread_cond_signal(&s_done);
            pthread_mutex_unlock(&s_mx);
        }
    }
}

static void * worker(void * arg)
{
    int tid = (int)(intptr_t)arg;
    unsigned seen = 0;
    for(;;) {
        pthread_mutex_lock(&s_mx);
        while(s_gen == seen) pthread_cond_wait(&s_go, &s_mx);
        seen = s_gen;
        pthread_mutex_unlock(&s_mx);
        run_jobs(&s_js[seen & 1], tid);
    }
    return NULL;
}

static void threads_start(void)
{
    long n = s_want_threads;
    const char * env = getenv("R3D_THREADS");
    if(env) n = atol(env);
#ifndef _WIN32
    if(n <= 0) n = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if(n < 1) n = 1;
    if(n > MAX_THREADS) n = MAX_THREADS;
    s_threads = 1;
    for(int i = 1; i < n; i++) {
        pthread_t t;
        if(pthread_create(&t, NULL, worker, (void *)(intptr_t)i) != 0) break;
        pthread_detach(t);
        s_threads++;
    }
}

/* fn(job, thread) for jobs 0..jobs-1 on every thread, this one included. */
static void parallel(int jobs, void (*fn)(int job, int tid))
{
    if(s_threads == 0) threads_start();
    if(jobs <= 0) return;
    unsigned g = s_gen + 1;
    jobset_t * js = &s_js[g & 1];
    js->fn = fn;
    js->njobs = jobs;
    __atomic_store_n(&js->done, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&js->next, 0, __ATOMIC_RELEASE);
    if(s_threads > 1 && jobs > 1) {
        pthread_mutex_lock(&s_mx);
        s_gen = g;
        pthread_cond_broadcast(&s_go);
        pthread_mutex_unlock(&s_mx);
    }
    else s_gen = g;
    run_jobs(js, 0);
    if(__atomic_load_n(&js->done, __ATOMIC_ACQUIRE) < jobs) {
        pthread_mutex_lock(&s_mx);
        while(__atomic_load_n(&js->done, __ATOMIC_ACQUIRE) < jobs) pthread_cond_wait(&s_done, &s_mx);
        pthread_mutex_unlock(&s_mx);
    }
}

void r3_parallel(int jobs, void (*fn)(int job, int tid)) { parallel(jobs, fn); }

int r3_thread_count(void)
{
    if(s_threads == 0) threads_start();
    return s_threads;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    if(t < 0) t = 0;
    if(t > 1) t = 1;
    uint32_t r = (uint32_t)((float)((a >> 16) & 255) * (1 - t) + (float)((b >> 16) & 255) * t);
    uint32_t g = (uint32_t)((float)((a >> 8) & 255) * (1 - t) + (float)((b >> 8) & 255) * t);
    uint32_t bl = (uint32_t)((float)(a & 255) * (1 - t) + (float)(b & 255) * t);
    return 0xFF000000u | r << 16 | g << 8 | bl;
}

/* The sky per row: a gradient down to the horizon (where a point far ahead
 * at eye height lands), fog colour below it. */
static void sky_rows(void)
{
    if(s_cap_sky < s_h) {
        free(s_sky_rows);
        s_cap_sky = s_h;
        s_sky_rows = malloc(sizeof(uint32_t) * (size_t)s_cap_sky);
    }
    /* the view direction, flattened */
    r3_vec fwd = r3v(-s_view.m[2], 0, -s_view.m[10]);
    float l = r3_len(fwd);
    float hy = (float)s_h * 0.5f;
    if(l > 1e-3f) {
        fwd = r3_scale(fwd, 1000.0f / l);
        r3_vec p = r3_add(s_eye, fwd);
        const float * m = s_vp.m;
        float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        if(w > 0) hy = (0.5f - y / w * 0.5f) * (float)s_h;
    }
    uint32_t fog = 0xFF000000u | (uint32_t)s_fog_r << 16 | (uint32_t)s_fog_g << 8 | (uint32_t)s_fog_b;
    for(int y = 0; y < s_h; y++) {
        float t = hy > 1 ? (float)y / hy : 1;
        s_sky_rows[y] = (float)y >= hy ? fog : mix(s_sky_top, s_sky_horizon, t);
    }
}

void r3_render(int out_w, int out_h, int scale, r3_sink_t sink, void * user)
{
    if(scale < 1) scale = 1;
    double t0 = now_ms();
    s_out_w = out_w;
    s_out_h = out_h;
    s_scale = scale;
    s_w = (out_w + scale - 1) / scale;
    s_h = (out_h + scale - 1) / scale;
    s_sink = sink;
    s_user = user;
    /* a band's colour and depth in ~24 KB (small ARM cores have 32 KB of L1 data) */
    s_band_rows = 24576 / (s_w * 8);
    if(s_band_rows < 2) s_band_rows = 2;
    if(s_band_rows > 16) s_band_rows = 16;
    s_hw = (float)s_w * 0.5f;
    s_hh = (float)s_h * 0.5f;
    s_bands = (s_h + s_band_rows - 1) / s_band_rows;
    if(s_bands > MAX_BANDS) s_bands = MAX_BANDS;
    s_band_inv = 65536u / (uint32_t)s_band_rows + 1;
    if(s_threads == 0) threads_start();
    geometry();
    double tb = now_ms();
    binning();
    sky_rows();
    s_stats.bin_ms = (float)(now_ms() - tb);
    double t1 = now_ms();

    s_draw_ns = s_out_ns = 0;
    parallel(s_bands, band_job);
    s_stats.draw_cpu_ms = (float)s_draw_ns / 1e6f;
    s_stats.out_cpu_ms = (float)s_out_ns / 1e6f;
    double t2 = now_ms();
    s_stats.w = s_w;
    s_stats.h = s_h;
    s_stats.scale = scale;
    s_stats.threads = s_threads;
    s_stats.geo_ms = (float)(t1 - t0);
    s_stats.raster_ms = (float)(t2 - t1);
    s_stats.total_ms = (float)(t2 - t0);
    s_stats.frame++;
}
