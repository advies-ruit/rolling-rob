#include "rob3d_view.h"

#include "draw.h"
#include "entities.h"
#include "game.h"
#include "level.h"
#include "r3d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The game's world in 3D (see rob3d_view.h). World units are the game's
 * (the original's 1920x1080 canvas); x stays x, y turns round (up is +y
 * here), and the game's plane is z = 0, where Rob rolls and everything that
 * can hit him moves.
 *
 * The camera looks straight down -z from in front, raised above the centre
 * of the game's view and with its lens shifted down to match (an off-axis
 * projection): the plane z = 0 lands on the screen exactly where draw.c puts
 * it, at any distance, so Rob, the ground line and the monsters are where the
 * 2D game shows them -- and the raised eye looks down onto the ground's top.
 * A long lens (narrow angle) keeps the depth gentle.
 *
 * Nothing here calls rand(): the game draws its levels from rand(), and a
 * call from the renderer would change the next level. Decoration comes from
 * our own generator, seeded from the terrain.
 */

#define ZF 150.0f       /* the ground strip reaches this far towards the camera */
#define ZB 260.0f       /* and this far back */
#define BOTTOM 1500.0f  /* world y the ground's face goes down to */
#define SEGS 16         /* ground segments per mesh (culled per mesh) */
#define HILL_Z -900.0f
#define FAR_Z -3200.0f

static bool s_on;
static bool s_ready;
static uint32_t s_sig;
static float s_built_vw, s_built_vh;

static r3_mesh * s_ground; /* chunks of the strip, trees in them */
static r3_mesh * s_face;   /* the strip's front, textured, one per ground chunk */
static int s_nground;
static r3_texture s_rock;
static bool s_have_rock;
static r3_mesh * s_hills;
static int s_nhills;
static r3_mesh * s_far;
static int s_nfar;
static r3_mesh * s_clouds;
static int s_nclouds;
static r3_mesh s_rob, s_shadow, s_monster, s_saucer, s_mine, s_mine_core, s_julian, s_fireball;
static r3_mesh s_cp_on, s_cp_off, s_finish, s_beam, s_parts, s_sky;
static int s_monster_type = -1;
static r3_vec s_light;

/* ---------------- small helpers ---------------- */

static uint32_t s_rng;
static float rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (float)(s_rng >> 8) / 16777216.0f;
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    if(t < 0) t = 0;
    if(t > 1) t = 1;
    uint32_t o = 0;
    for(int sh = 0; sh < 24; sh += 8) {
        float c = (float)((a >> sh) & 255) * (1 - t) + (float)((b >> sh) & 255) * t;
        o |= (uint32_t)(c + 0.5f) << sh;
    }
    return o;
}

static uint32_t mul(uint32_t a, float k)
{
    uint32_t o = 0;
    for(int sh = 0; sh < 24; sh += 8) {
        float c = (float)((a >> sh) & 255) * k;
        o |= (uint32_t)(c > 255 ? 255 : c) << sh;
    }
    return o;
}

/* Appends src to dst, moved by m (rotation, translation and one scale), in
 * colour rgb (0: src's own). */
static void append(r3_mesh * dst, const r3_mesh * src, const r3_mat * m, uint32_t rgb)
{
    int base = dst->nv;
    for(int i = 0; i < src->nv; i++) {
        const r3_vertex * v = &src->v[i];
        r3_vec p = r3_transform(m, r3v(v->px, v->py, v->pz));
        r3_vec n = r3_norm(r3v(m->m[0] * v->nx + m->m[4] * v->ny + m->m[8] * v->nz,
                               m->m[1] * v->nx + m->m[5] * v->ny + m->m[9] * v->nz,
                               m->m[2] * v->nx + m->m[6] * v->ny + m->m[10] * v->nz));
        r3_mesh_vertex(dst, p, n, rgb ? rgb : v->rgb);
    }
    for(int i = 0; i + 2 < src->ni; i += 3)
        r3_mesh_tri(dst, base + (int)src->idx[i], base + (int)src->idx[i + 1], base + (int)src->idx[i + 2]);
}

/* An ellipsoid: radii rx, ry, rz round c. */
static void ellipsoid(r3_mesh * dst, r3_vec c, float rx, float ry, float rz, int subdiv, uint32_t rgb)
{
    r3_mesh u;
    r3_mesh_init(&u);
    r3_mesh_sphere(&u, r3v(0, 0, 0), 1, subdiv, rgb);
    int base = dst->nv;
    for(int i = 0; i < u.nv; i++) {
        const r3_vertex * v = &u.v[i];
        r3_vec p = r3v(c.x + v->px * rx, c.y + v->py * ry, c.z + v->pz * rz);
        r3_vec n = r3_norm(r3v(v->px / rx, v->py / ry, v->pz / rz));
        r3_mesh_vertex(dst, p, n, rgb);
    }
    for(int i = 0; i + 2 < u.ni; i += 3)
        r3_mesh_tri(dst, base + (int)u.idx[i], base + (int)u.idx[i + 1], base + (int)u.idx[i + 2]);
    r3_mesh_free(&u);
}

/* A cone from base along dir. */
static void cone_dir(r3_mesh * dst, r3_vec base, r3_vec dir, float r, float h, int seg, uint32_t rgb)
{
    r3_mesh u;
    r3_mesh_init(&u);
    r3_mesh_cone(&u, r3v(0, 0, 0), r, h, seg, rgb);
    dir = r3_norm(dir);
    r3_vec axis = r3_cross(r3v(0, 1, 0), dir);
    float s = r3_len(axis), c = dir.y;
    r3_mat rot = s > 1e-4f ? r3_rotate_axis(r3_scale(axis, 1 / s), atan2f(s, c)) : c < 0 ? r3_rotate_x((float)M_PI) : r3_identity();
    r3_mat t = r3_translate(base), m = r3_mul(&t, &rot);
    append(dst, &u, &m, rgb);
    r3_mesh_free(&u);
}

/* Normals towards the light: the mesh is lit fully whatever way it faces
 * (fire, the beam, sparks). Only for meshes drawn without rotation. */
static void glow(r3_mesh * m, int from)
{
    for(int i = from; i < m->nv; i++) {
        m->v[i].nx = s_light.x;
        m->v[i].ny = s_light.y;
        m->v[i].nz = s_light.z;
    }
}

/* Both sides of a flat shape: the rasterizer culls back faces. */
static void two_sided(r3_mesh * m, int from_tri)
{
    int n = m->ni;
    for(int i = from_tri * 3; i + 2 < n; i += 3) r3_mesh_tri(m, (int)m->idx[i], (int)m->idx[i + 2], (int)m->idx[i + 1]);
}

/* ---------------- the level: ground, trees, hills, mountains ---------------- */

static float ground_y(const rr_terrain_t * t, float x) { return rr_terrain_height_at(t, x); }

static void tree(r3_mesh * m, rr_theme_t theme, float x, float z, float gy, float h)
{
    r3_vec b = r3v(x, -gy, z);
    switch(theme) {
        case RR_THEME_DESERT:
            r3_mesh_cylinder(m, b, h * 0.09f, h * 0.8f, 7, 0x3E8E3A, 0x4CA84A);
            r3_mesh_cylinder(m, r3v(x - h * 0.2f, -gy + h * 0.35f, z), h * 0.06f, h * 0.3f, 6, 0x3E8E3A, 0x4CA84A);
            r3_mesh_cylinder(m, r3v(x + h * 0.2f, -gy + h * 0.45f, z), h * 0.06f, h * 0.25f, 6, 0x3E8E3A, 0x4CA84A);
            break;
        case RR_THEME_FOREST:
        case RR_THEME_SNOW:
        case RR_THEME_JUNGLE: {
            uint32_t leaf = theme == RR_THEME_SNOW ? 0xE8F0F4 : theme == RR_THEME_JUNGLE ? 0x2F8F3A : 0x2E6B34;
            r3_mesh_cylinder(m, b, h * 0.05f, h * 0.3f, 4, 0x6B4A2E, 0x6B4A2E);
            r3_mesh_cone(m, r3v(x, -gy + h * 0.2f, z), h * 0.3f, h * 0.5f, 7, leaf);
            r3_mesh_cone(m, r3v(x, -gy + h * 0.5f, z), h * 0.22f, h * 0.5f, 7, mul(leaf, 1.1f));
            break;
        }
        case RR_THEME_CANDY:
            r3_mesh_cylinder(m, b, h * 0.04f, h * 0.6f, 6, 0xFFFFFF, 0xFFFFFF);
            ellipsoid(m, r3v(x, -gy + h * 0.75f, z), h * 0.2f, h * 0.2f, h * 0.08f, 1, rnd() < 0.5f ? 0xFF6EC7 : 0x7EE0FF);
            break;
        case RR_THEME_CYBER:
            r3_mesh_box(m, r3v(x - h * 0.12f, -gy, z - h * 0.12f), r3v(x + h * 0.12f, -gy + h * 1.4f, z + h * 0.12f), 0x30E0FF,
                        0x203048);
            break;
        default: /* rocks */
            ellipsoid(m, r3v(x, -gy + h * 0.1f, z), h * 0.3f, h * 0.22f, h * 0.25f, 1, 0x7A7068);
            break;
    }
}

/* The face's rock, 256 x 256 for 512 x 512 world units, in the level's
 * ground colour (a little lighter: the light falls on it at a slant):
 * layers, pebbles, cracks, gold veins and gems (draw.c has them every 7th
 * and 5th ridge point), placed at random so the repeat does not show. */
static void put(uint32_t * px, int x, int y, uint32_t c) { px[(y & 255) * 256 + (x & 255)] = c; }

static void make_rock(uint32_t g)
{
    enum { N = 256 };
    static uint32_t px[N * N];
    s_rng = 0xA5A5F00Du;
    g = mul(g, 1.3f);
    float band[N];
    for(int y = 0; y < N; y++) {
        float a = 6.2831853f * (float)y / N;
        band[y] = 0.92f + 0.07f * sinf(a * 5 + 0.7f) + 0.05f * sinf(a * 11 + 2.1f) + 0.04f * sinf(a * 23);
    }
    for(int y = 0; y < N; y++)
        for(int x = 0; x < N; x++) px[y * N + x] = mul(g, band[y] * (0.9f + 0.15f * rnd()));
    for(int k = 0; k < 140; k++) { /* pebbles */
        int cx = (int)(rnd() * N), cy = (int)(rnd() * N), r = 1 + (int)(rnd() * 3);
        float f = rnd() < 0.5f ? 0.72f : 1.18f;
        for(int dy = -r; dy <= r; dy++)
            for(int dx = -r; dx <= r; dx++)
                if(dx * dx + dy * dy <= r * r) {
                    uint32_t * q = &px[((cy + dy) & (N - 1)) * N + ((cx + dx) & (N - 1))];
                    *q = mul(*q, f);
                }
    }
    for(int k = 0; k < 18; k++) { /* cracks */
        float x = rnd() * N, y = rnd() * N;
        for(int i = 0; i < 14 + (int)(rnd() * 14); i++) {
            uint32_t * q = &px[((int)y & (N - 1)) * N + ((int)x & (N - 1))];
            *q = mul(*q, 0.55f);
            x += rnd() * 2 - 1;
            y += 1;
        }
    }
    for(int k = 0; k < 3; k++) { /* gold veins: a zigzag, two texels thick, nuggets at the bends */
        int x = (int)(rnd() * N), y = (int)(rnd() * N);
        for(int s = 0; s < 3; s++) {
            int nx = x + 22 + (int)(rnd() * 14), ny = y + (int)(rnd() * 24) - 8;
            for(int i = 0; i <= 40; i++) {
                float t = (float)i / 40;
                int px_ = (int)((float)x + (float)(nx - x) * t), py = (int)((float)y + (float)(ny - y) * t);
                put(px, px_, py, 0xFFD700);
                put(px, px_, py + 1, 0xD8A800);
            }
            for(int dy = -2; dy <= 2; dy++)
                for(int dx = -2; dx <= 2; dx++)
                    if(dx * dx + dy * dy <= 5) put(px, nx + dx, ny + dy, dx + dy < 0 ? 0xFFF3A0 : 0xE8B800);
            x = nx;
            y = ny;
        }
    }
    static const uint32_t gc[3][2] = { { 0x80FFFF, 0x10B8C8 }, { 0xFF90FF, 0xC020C0 }, { 0xA0FF90, 0x20B040 } };
    for(int k = 0; k < 7; k++) { /* gems: little diamonds */
        int x = (int)(rnd() * N), y = (int)(rnd() * N), c = k % 3;
        for(int dy = -3; dy <= 3; dy++)
            for(int dx = -3; dx <= 3; dx++)
                if(abs(dx) + abs(dy) <= 3) put(px, x + dx, y + dy, gc[c][dx + dy < 0 ? 0 : 1]);
    }
    if(s_have_rock) r3_texture_free(&s_rock);
    s_have_rock = r3_texture_init(&s_rock, px, 8, 8);
}

static void build_ground(const rr_terrain_t * t, const rr_level_config_t * cfg)
{
    int nseg = t->count - 2; /* the ridge and its last slope: points 0 .. count - 2; the last one closes the polygon */
    s_nground = (nseg + SEGS - 1) / SEGS;
    s_ground = calloc((size_t)s_nground, sizeof(r3_mesh));
    s_face = calloc((size_t)s_nground, sizeof(r3_mesh));
    uint32_t g = cfg->ground_color;
    make_rock(g);
    uint32_t top = mix(mul(g, 1.25f), 0x6FBF4A, cfg->theme == RR_THEME_FOREST || cfg->theme == RR_THEME_JUNGLE ? 0.6f : 0.0f);
    if(cfg->theme == RR_THEME_SNOW) top = 0xF4F8FC;
    static const float tz[3] = { ZF, 0, -ZB };
    static const float fd[4] = { 0, 30, 160, -1 }; /* the face's rows below the top; -1 = BOTTOM */
    for(int c = 0; c < s_nground; c++) {
        r3_mesh * m = &s_ground[c];
        r3_mesh_init(m);
        int i0 = c * SEGS, i1 = i0 + SEGS < nseg ? i0 + SEGS : nseg;
        int n = i1 - i0 + 1;
        int top0 = m->nv;
        for(int i = i0; i <= i1; i++) {
            const rr_point_t * p = &t->points[i];
            /* the top's normal: the two segments' either side, averaged */
            const rr_point_t * a = &t->points[i > 0 ? i - 1 : i], * b = &t->points[i < nseg ? i + 1 : i];
            r3_vec nrm = r3_norm(r3v(-(-(b->y) + a->y), b->x - a->x, 0)); /* (-dY, dX) with Y = -y */
            for(int r = 0; r < 3; r++) {
                s_rng = (uint32_t)i * 2654435761u ^ (uint32_t)r * 40503u ^ 0x5bd1e995u;
                rnd();
                float k = 0.9f + 0.2f * rnd();
                uint32_t col = r == 0 ? mul(top, 0.8f * k) : mul(top, k);
                r3_mesh_vertex(m, r3v(p->x, -p->y, tz[r]), nrm, col);
            }
        }
        for(int s = 0; s + 1 < n; s++)
            for(int r = 0; r < 2; r++) {
                int a = top0 + s * 3 + r, b = a + 3;
                /* seen from above, front row first: (x, z) counter-clockwise from +y */
                r3_mesh_tri(m, a, b, b + 1);
                r3_mesh_tri(m, a, b + 1, a + 1);
            }
        /* the face: the rock texture in world units (layers run level through
         * the hill), darker the deeper */
        r3_mesh * f = &s_face[c];
        r3_mesh_init(f);
        f->tex = s_have_rock ? &s_rock : NULL;
        for(int i = i0; i <= i1; i++) {
            const rr_point_t * p = &t->points[i];
            for(int r = 0; r < 4; r++) {
                float y = fd[r] < 0 ? BOTTOM : p->y + fd[r];
                float k = r == 0 ? 1.0f : r == 1 ? 0.85f : r == 2 ? 0.68f : 0.4f;
                uint32_t col = f->tex ? mul(0xFFFFFF, k) : mul(g, k);
                r3_mesh_vertex_uv(f, r3v(p->x, -y, ZF), r3v(0, 0, 1), col, p->x / 512.0f, y / 512.0f);
            }
        }
        for(int s = 0; s + 1 < n; s++)
            for(int r = 0; r < 3; r++) {
                int a = s * 4 + r, b = a + 4;
                r3_mesh_tri(f, a, a + 1, b + 1);
                r3_mesh_tri(f, a, b + 1, b);
            }
        r3_mesh_bounds(f);
        /* trees and rocks behind the plane, never in front of it */
        s_rng = 0x9E3779B9u ^ (uint32_t)c * 0x85EBCA6Bu ^ (uint32_t)cfg->theme;
        for(int i = i0; i < i1; i++) {
            if(rnd() > 0.35f) continue;
            const rr_point_t * p = &t->points[i], * q = &t->points[i + 1];
            float x = p->x + (q->x - p->x) * rnd();
            float z = -ZB * (0.3f + 0.6f * rnd());
            tree(m, cfg->theme, x, z, ground_y(t, x) + 2, 70 + 90 * rnd());
        }
        r3_mesh_bounds(m);
    }
}

/* A ridge of wall at depth z: heights from fn, coloured from base up to tip. */
static int build_ridge(r3_mesh ** out, float x0, float x1, float step, float z, const float * hy, int nh, uint32_t tip,
                       uint32_t base)
{
    int per = 24;
    int nc = (nh - 1 + per - 1) / per;
    *out = calloc((size_t)nc, sizeof(r3_mesh));
    for(int c = 0; c < nc; c++) {
        r3_mesh * m = &(*out)[c];
        r3_mesh_init(m);
        int i0 = c * per, i1 = i0 + per < nh - 1 ? i0 + per : nh - 1;
        int v0 = m->nv;
        for(int i = i0; i <= i1; i++) {
            float x = x0 + step * (float)i;
            if(x > x1) x = x1;
            r3_mesh_vertex(m, r3v(x, -hy[i], z), r3v(0, 0.3f, 1), tip);
            r3_mesh_vertex(m, r3v(x, -1700, z), r3v(0, 0, 1), base); /* low enough to meet the ground */
        }
        for(int s = 0; s < i1 - i0; s++) {
            int a = v0 + s * 2;
            r3_mesh_tri(m, a, a + 1, a + 3);
            r3_mesh_tri(m, a, a + 3, a + 2);
        }
        r3_mesh_bounds(m);
    }
    return nc;
}

static void build_back(const rr_terrain_t * t, const rr_level_config_t * cfg, float vw, float D)
{
    uint32_t haze = cfg->sky_color2;
    float x_end = t->finish_line + 500 + vw;
    /* hills: the ridge again, smoothed, raised, half in the haze */
    {
        float k = (D - HILL_Z) / D, step = 60;
        float x0 = -vw * k, x1 = x_end + vw * k;
        int nh = (int)((x1 - x0) / step) + 2;
        float * hy = malloc(sizeof(float) * (size_t)nh);
        s_rng = 0x1234567u ^ (uint32_t)cfg->theme;
        for(int i = 0; i < nh; i++) {
            float x = x0 + step * (float)i, s = 0;
            for(int j = -4; j <= 4; j++) s += ground_y(t, x * 0.8f + (float)j * 90 + 400);
            hy[i] = s / 9 - 130 + 60 * sinf(x * 0.0021f) + 25 * rnd();
        }
        uint32_t tip = mix(mul(cfg->ground_color, 1.1f), haze, 0.45f);
        s_nhills = build_ridge(&s_hills, x0, x1, step, HILL_Z, hy, nh, tip, mix(cfg->ground_color, haze, 0.6f));
        free(hy);
    }
    /* mountains far away */
    {
        float k = (D - FAR_Z) / D, step = 160;
        float x0 = -vw * k - 2000, x1 = x_end + vw * k + 2000;
        int nh = (int)((x1 - x0) / step) + 2;
        float * hy = malloc(sizeof(float) * (size_t)nh);
        s_rng = 0x7654321u ^ (uint32_t)cfg->theme;
        float ph = rnd() * 10;
        for(int i = 0; i < nh; i++) {
            float x = x0 + step * (float)i;
            float h = 0.55f * sinf(x * 0.0009f + ph) + 0.3f * sinf(x * 0.0023f + ph * 2) + 0.15f * (rnd() * 2 - 1);
            hy[i] = 430 - 330 * h;
        }
        uint32_t rock = cfg->theme == RR_THEME_SNOW ? 0xDDE6EE : mul(cfg->ground_color, 0.9f);
        s_nfar = build_ridge(&s_far, x0, x1, step, FAR_Z, hy, nh, mix(rock, haze, 0.55f), mix(rock, haze, 0.8f));
        free(hy);
    }
}

/* clouds where draw.c has them: soft white heaps far back, a few per mesh */
static void build_clouds(const rr_terrain_t * t, const rr_level_config_t * cfg, float vw, float D)
{
    rr_theme_t th = cfg->theme;
    if(th == RR_THEME_SPACE || th == RR_THEME_CYBER || th == RR_THEME_HELL || th == RR_THEME_VOLCANO) return;
    const float z = -1700, span = 2400;
    float k = (D - z) / D;
    float x0 = -vw * k, x1 = t->finish_line + 500 + vw + vw * k;
    s_nclouds = (int)((x1 - x0) / span) + 1;
    s_clouds = calloc((size_t)s_nclouds, sizeof(r3_mesh));
    uint32_t col = th == RR_THEME_CANDY ? 0xFFE8F6 : 0xF6FAFF;
    s_rng = 0xC10D5u ^ (uint32_t)th;
    for(int c = 0; c < s_nclouds; c++) {
        r3_mesh * m = &s_clouds[c];
        r3_mesh_init(m);
        for(float x = x0 + span * (float)c + 300 * rnd(); x < x0 + span * (float)(c + 1); x += 700 + 600 * rnd()) {
            float y = -(40 + 260 * rnd()), zz = z - 300 * rnd(), w = 120 + 120 * rnd();
            int from = m->nv;
            for(int b = 0; b < 3; b++)
                ellipsoid(m, r3v(x + w * 0.55f * (float)(b - 1), y + (b == 1 ? w * 0.18f : 0), zz), w * (b == 1 ? 0.55f : 0.42f),
                          w * (b == 1 ? 0.32f : 0.24f), w * 0.2f, 1, mul(col, b == 1 ? 1.0f : 0.94f));
            glow(m, from);
        }
        r3_mesh_bounds(m);
    }
}

/* ---------------- the actors ---------------- */

static void build_rob(void)
{
    r3_mesh_init(&s_rob);
    const float r = 30;
    /* the ball: dark, a little lighter at the top so it reads as round */
    ellipsoid(&s_rob, r3v(0, 0, 0), r, r, r, 2, 0x2A2A2A);
    /* everything on the face is placed on the sphere, facing the camera;
     * local (x, y) is draw.c's with y turned round */
#define ON(x, y, lift) r3v((x), -(y), sqrtf(fmaxf(r * r - (x) * (x) - (y) * (y), 0)) + (lift))
    ellipsoid(&s_rob, ON(-8, -10, -3), 8, 8, 4, 0, 0x444444); /* the shine */
    int e0 = s_rob.nv;
    ellipsoid(&s_rob, ON(-8, -5, -1), 6, 6, 3.5f, 0, 0xFFD700); /* eyes, glowing a little */
    ellipsoid(&s_rob, ON(8, -5, -1), 6, 6, 3.5f, 0, 0xFFD700);
    glow(&s_rob, e0);
    ellipsoid(&s_rob, ON(-8, -5, 2), 2.4f, 2.4f, 1.5f, 0, 0x000000);
    ellipsoid(&s_rob, ON(8, -5, 2), 2.4f, 2.4f, 1.5f, 0, 0x000000);
    for(int i = 0; i <= 4; i++) { /* the grin: gold beads on an arc */
        float a = 0.1f + ((float)M_PI - 0.2f) * (float)i / 4;
        float x = 12 * cosf(a), y = 3 + 12 * sinf(a);
        ellipsoid(&s_rob, ON(x, y, 0), 1.8f, 1.8f, 1.5f, 0, 0xFFD700);
    }
    for(int i = -6; i <= 6; i += 6) ellipsoid(&s_rob, ON((float)i + 1.5f, 10, 0), 1.8f, 2.2f, 1.2f, 0, 0xFFFFFF); /* teeth */
#undef ON
    /* the cigarette, sticking out sideways, its tip glowing */
    {
        r3_mesh c;
        r3_mesh_init(&c);
        r3_mesh_cylinder(&c, r3v(0, 0, 0), 1.7f, 18, 6, 0xF4F4F4, 0xF4F4F4);
        r3_mat rz = r3_rotate_axis(r3v(0, 0, 1), -(float)M_PI / 2);
        r3_mat tr = r3_translate(r3v(12, -6.5f, 20));
        r3_mat m = r3_mul(&tr, &rz);
        append(&s_rob, &c, &m, 0);
        r3_mesh_free(&c);
        ellipsoid(&s_rob, r3v(30.5f, -6.5f, 20), 2.6f, 2.6f, 2.6f, 0, 0xFF4500);
    }
    r3_mesh_bounds(&s_rob);

    /* a soft dark disc under him */
    r3_mesh_init(&s_shadow);
    int c = r3_mesh_vertex(&s_shadow, r3v(0, 0, 0), r3v(0, 1, 0), 0x000000);
    for(int i = 0; i < 16; i++) {
        float a = (float)i / 16 * 6.2831853f;
        r3_mesh_vertex(&s_shadow, r3v(cosf(a), 0, sinf(a)), r3v(0, 1, 0), 0x000000);
    }
    for(int i = 0; i < 16; i++) r3_mesh_tri(&s_shadow, c, c + 1 + (i + 1) % 16, c + 1 + i);
    r3_mesh_bounds(&s_shadow);
}

static uint32_t monster_colour(rr_monster_t t)
{
    static const uint32_t k[] = { 0x4CAF50, 0x2E7D32, 0xE0E0E0, 0xFF4500, 0x1B5E20,
                                  0x7CFC00, 0xFF69B4, 0x9E9E9E, 0xB71C1C, 0x40E0FF };
    return (int)t >= 0 && (int)t < (int)(sizeof(k) / sizeof(k[0])) ? k[t] : 0x4CAF50;
}

/* A monster round its centre, about draw.c's size (1.7 x its 2D shape). */
static void build_monster(rr_monster_t type)
{
    r3_mesh_free(&s_monster);
    r3_mesh_init(&s_monster);
    uint32_t col = monster_colour(type);
    switch(type) {
        case RR_MONSTER_CACTUS:
            r3_mesh_cylinder(&s_monster, r3v(0, -26, 0), 13, 52, 8, col, mul(col, 1.2f));
            r3_mesh_cylinder(&s_monster, r3v(-20, -4, 0), 7, 18, 6, col, mul(col, 1.2f));
            r3_mesh_cylinder(&s_monster, r3v(20, -4, 0), 7, 18, 6, col, mul(col, 1.2f));
            break;
        case RR_MONSTER_YETI:
        case RR_MONSTER_ROBOT:
            r3_mesh_box(&s_monster, r3v(-17, -25, -17), r3v(17, 17, 17), mul(col, 1.1f), col);
            break;
        case RR_MONSTER_LAVA:
            for(int i = 0; i < 4; i++) cone_dir(&s_monster, r3v(-18 + 12 * (float)i, -26, 0), r3v(0, 1, 0), 9, 50, 7, col);
            glow(&s_monster, 0);
            break;
        case RR_MONSTER_CRYSTAL:
            cone_dir(&s_monster, r3v(0, 0, 0), r3v(0, 1, 0), 18, 30, 6, col);
            cone_dir(&s_monster, r3v(0, 0, 0), r3v(0, -1, 0), 18, 25, 6, mul(col, 0.8f));
            break;
        default:
            ellipsoid(&s_monster, r3v(0, -8, 0), 30, 25, 26, 1, col);
            break;
    }
    if(type != RR_MONSTER_LAVA) {
        ellipsoid(&s_monster, r3v(-9, 4, 20), 5, 6, 4, 0, 0xFFFFFF);
        ellipsoid(&s_monster, r3v(9, 4, 20), 5, 6, 4, 0, 0xFFFFFF);
        ellipsoid(&s_monster, r3v(-9, 4, 23.5f), 2.5f, 3, 1.5f, 0, 0x000000);
        ellipsoid(&s_monster, r3v(9, 4, 23.5f), 2.5f, 3, 1.5f, 0, 0x000000);
    }
    r3_mesh_bounds(&s_monster);
    s_monster_type = (int)type;
}

static void build_props(void)
{
    /* the saucer: a lens of metal, a glass dome, three lights */
    r3_mesh_init(&s_saucer);
    ellipsoid(&s_saucer, r3v(0, 0, 0), 40, 12, 40, 1, 0xB8B8C0);
    ellipsoid(&s_saucer, r3v(0, 10, 0), 20, 16, 20, 1, 0x30C8E0);
    int l0 = s_saucer.nv;
    static const uint32_t lc[3] = { 0xFF2020, 0x20FF20, 0x4040FF };
    for(int i = 0; i < 3; i++) ellipsoid(&s_saucer, r3v(-24 + 24 * (float)i, -6, 32), 5, 4, 4, 0, lc[i]);
    glow(&s_saucer, l0);
    r3_mesh_bounds(&s_saucer);

    /* the mine: a dark ball with spikes, a red light in front */
    r3_mesh_init(&s_mine);
    ellipsoid(&s_mine, r3v(0, 0, 0), RR_MINE_R, RR_MINE_R, RR_MINE_R, 1, 0x333333);
    for(int k = 0; k < 8; k++) {
        float a = (float)k / 8 * 6.2831853f;
        r3_vec d = r3v(cosf(a), sinf(a), 0.35f * (float)((k & 1) * 2 - 1));
        cone_dir(&s_mine, r3_scale(r3_norm(d), RR_MINE_R * 0.8f), d, 4, 12, 5, 0x777777);
    }
    for(int k = 0; k < 2; k++) cone_dir(&s_mine, r3v(0, 0, k ? -16 : 16), r3v(0, 0, k ? -1 : 1), 4, 12, 5, 0x777777);
    r3_mesh_bounds(&s_mine);
    r3_mesh_init(&s_mine_core);
    ellipsoid(&s_mine_core, r3v(0, 0, 0), 1, 1, 1, 1, 0xFF2020);
    glow(&s_mine_core, 0);
    r3_mesh_bounds(&s_mine_core);

    /* Julian: a dark red block with stripes, gold eyes, horns */
    r3_mesh_init(&s_julian);
    r3_mesh_box(&s_julian, r3v(-50, -60, -35), r3v(50, 60, 35), 0x9B1010, 0x8B0000);
    for(int i = 0; i < 3; i++)
        r3_mesh_box(&s_julian, r3v(-45, 25 - 40 * (float)i, 35), r3v(45, 55 - 40 * (float)i, 38), 0x4D0000, 0x4D0000);
    ellipsoid(&s_julian, r3v(-20, 40, 40), 8, 8, 4, 1, 0xFFD700);
    ellipsoid(&s_julian, r3v(20, 40, 40), 8, 8, 4, 1, 0xFFD700);
    ellipsoid(&s_julian, r3v(-20, 40, 43), 4, 4, 2, 0, 0xFF0000);
    ellipsoid(&s_julian, r3v(20, 40, 43), 4, 4, 2, 0, 0xFF0000);
    cone_dir(&s_julian, r3v(-40, 58, 0), r3v(-0.4f, 1, 0), 8, 26, 6, 0x4D0000);
    cone_dir(&s_julian, r3v(40, 58, 0), r3v(0.4f, 1, 0), 8, 26, 6, 0x4D0000);
    r3_mesh_bounds(&s_julian);

    /* a fireball: a glowing core in a bigger orange ball */
    r3_mesh_init(&s_fireball);
    ellipsoid(&s_fireball, r3v(0, 0, 0), 15, 15, 15, 0, 0xFF6A00);
    ellipsoid(&s_fireball, r3v(0, 0, 8), 9, 9, 9, 0, 0xFFF04A);
    glow(&s_fireball, 0);
    r3_mesh_bounds(&s_fireball);

    /* checkpoints: a pole and a flag (draw.c: 8 x 100 pole, flag to 60 wide) */
    for(int on = 0; on < 2; on++) {
        r3_mesh * m = on ? &s_cp_on : &s_cp_off;
        uint32_t col = on ? 0x00E000 : 0xE00000;
        r3_mesh_init(m);
        r3_mesh_box(m, r3v(0, -RR_CP_H, -4), r3v(8, 0, 4), col, mul(col, 0.8f));
        int t0 = m->ni / 3;
        int a = r3_mesh_vertex(m, r3v(8, 0, 0), r3v(0, 0, 1), col);
        int b = r3_mesh_vertex(m, r3v(RR_CP_W, -20, 0), r3v(0, 0, 1), col);
        int c = r3_mesh_vertex(m, r3v(8, -40, 0), r3v(0, 0, 1), col);
        r3_mesh_tri(m, a, c, b);
        two_sided(m, t0);
        r3_mesh_bounds(m);
    }

    /* the finish: two golden posts across the strip and a chequered banner */
    r3_mesh_init(&s_finish);
    r3_mesh_cylinder(&s_finish, r3v(0, 0, ZF * 0.8f), 6, 320, 8, 0xFFD700, 0xFFD700);
    r3_mesh_cylinder(&s_finish, r3v(0, 0, -ZB * 0.8f), 6, 320, 8, 0xFFD700, 0xFFD700);
    for(int k = 0; k < 2; k++) ellipsoid(&s_finish, r3v(0, 326, k ? -ZB * 0.8f : ZF * 0.8f), 10, 10, 10, 0, 0xFFE860);
    for(int i = 0; i < 8; i++)
        for(int j = 0; j < 2; j++) {
            float z0 = ZF * 0.8f - (ZF * 0.8f + ZB * 0.8f) * (float)(i + 1) / 8, z1 = z0 + (ZF * 0.8f + ZB * 0.8f) / 8;
            uint32_t col = (i + j) & 1 ? 0xFFFFFF : 0x111111;
            r3_mesh_box(&s_finish, r3v(-4, 270 + 20 * (float)j, z0), r3v(4, 290 + 20 * (float)j, z1), col, col);
        }
    r3_mesh_bounds(&s_finish);

    /* the tractor beam: a cone of light, kept behind the plane so Rob shows */
    r3_mesh_init(&s_beam);
    cone_dir(&s_beam, r3v(0, -300, 0), r3v(0, 1, 0), 60, 280, 12, 0x60F0F0);
    glow(&s_beam, 0);
    r3_mesh_bounds(&s_beam);

    r3_mesh_init(&s_parts);
    r3_mesh_init(&s_sky);
}

/* sparks, confetti and the explosion: little diamonds, rebuilt per frame */
static void part(float x, float y, float size, uint32_t rgb)
{
    float h = size * 0.7f;
    r3_mesh * m = &s_parts;
    int c[6];
    c[0] = r3_mesh_vertex(m, r3v(x + h, -y, 0), s_light, rgb);
    c[1] = r3_mesh_vertex(m, r3v(x, -y + h, 0), s_light, rgb);
    c[2] = r3_mesh_vertex(m, r3v(x - h, -y, 0), s_light, rgb);
    c[3] = r3_mesh_vertex(m, r3v(x, -y - h, 0), s_light, rgb);
    c[4] = r3_mesh_vertex(m, r3v(x, -y, h), s_light, rgb);
    c[5] = r3_mesh_vertex(m, r3v(x, -y, -h), s_light, rgb);
    for(int i = 0; i < 4; i++) {
        r3_mesh_tri(m, c[i], c[(i + 1) % 4], c[4]);
        r3_mesh_tri(m, c[(i + 1) % 4], c[i], c[5]);
    }
}

/* ---------------- a frame ---------------- */

static uint32_t terrain_sig(const rr_view_t * v)
{
    uint32_t h = 2166136261u ^ (uint32_t)v->level;
    const rr_terrain_t * t = v->terrain;
    for(int i = 0; i < t->count; i++) {
        uint32_t a, b;
        memcpy(&a, &t->points[i].x, 4);
        memcpy(&b, &t->points[i].y, 4);
        h = (h ^ a) * 16777619u;
        h = (h ^ b) * 16777619u;
    }
    return h;
}

static void free_level(void)
{
    for(int i = 0; i < s_nground; i++) {
        r3_mesh_free(&s_ground[i]);
        r3_mesh_free(&s_face[i]);
    }
    for(int i = 0; i < s_nhills; i++) r3_mesh_free(&s_hills[i]);
    for(int i = 0; i < s_nfar; i++) r3_mesh_free(&s_far[i]);
    for(int i = 0; i < s_nclouds; i++) r3_mesh_free(&s_clouds[i]);
    free(s_ground);
    free(s_face);
    s_face = NULL;
    free(s_hills);
    free(s_far);
    free(s_clouds);
    s_ground = s_hills = s_far = s_clouds = NULL;
    s_nground = s_nhills = s_nfar = s_nclouds = 0;
}

static void draw_at(const r3_mesh * m, float x, float y, float z)
{
    r3_mat t = r3_translate(r3v(x, y, z));
    r3_draw(m, &t);
}

/* Everything that moves is drawn closer to the eye and smaller by the same
 * ratio: along the lines of sight, so on the screen it is exactly where and
 * how big the game has it, but in front of the ground. In 2D a saucer flies
 * through the hill and is still seen; here the hill would hide it. */
#define PULL_Z (ZF + 70.0f)
static r3_vec s_eye;
static float s_pull; /* the ratio: (D - PULL_Z) / D */

/* where (x, y, 0) is drawn, and at what scale, times extra */
static r3_mat pulled(float x, float y, float extra)
{
    r3_vec p = r3_add(s_eye, r3_scale(r3_sub(r3v(x, y, 0), s_eye), s_pull));
    r3_mat t = r3_translate(p), sc = r3_scale_m(s_pull * extra);
    return r3_mul(&t, &sc);
}

static void draw_pulled(const r3_mesh * m, float x, float y)
{
    r3_mat t = pulled(x, y, 1);
    r3_draw(m, &t);
}

/* the platform's sink, behind ours: the game's overlay goes on each band first */
static rr_band_sink_t s_sink;
static void * s_user;
static int s_w, s_h;

static void band_out(uint32_t * band, int stride_px, int y0, int rows, void * user)
{
    (void)user;
    game_overlay_band(band, stride_px, y0, rows, s_w, s_h);
    s_sink(band, stride_px, y0, rows, s_user);
}

static void render(int w, int h, const rr_view_t * v, float view_scale, rr_band_sink_t sink, void * user)
{
    const rr_level_config_t * cfg = &rr_levels[v->level];
    float vw = (float)w / view_scale, vh = (float)h / view_scale;
    /* the eye: far back with a long lens, raised a third of the view */
    const float D = 2.6f * vh, E = 0.45f * vh;
    if(!s_ready) {
        s_light = r3_norm(r3v(-0.35f, 0.85f, 0.55f));
        build_rob();
        build_props();
        s_ready = true;
    }
    uint32_t sig = terrain_sig(v);
    if(sig != s_sig || vw != s_built_vw || vh != s_built_vh) {
        free_level();
        build_ground(v->terrain, cfg);
        build_back(v->terrain, cfg, vw, D);
        build_clouds(v->terrain, cfg, vw, D);
        s_sig = sig;
        s_built_vw = vw;
        s_built_vh = vh;
    }
    if(s_monster_type != (int)cfg->monster_type) build_monster(cfg->monster_type);

    /* draw.c moves the world in whole device pixels; so does this */
    float cam_x = floorf(v->camera_x * view_scale + 0.5f) / view_scale;
    float cam_y = floorf(v->camera_y * view_scale + 0.5f) / view_scale;
    float cx = cam_x + vw / 2, cy = -(cam_y + vh / 2);
    r3_vec eye = r3v(cx, cy + E, D);
    s_eye = eye;
    s_pull = (D - PULL_Z) / D;
    r3_mat view = r3_look_at(eye, r3v(cx, cy + E, 0), r3v(0, 1, 0));
    float fov = 2 * atanf(vh / 2 / D);
    r3_mat proj = r3_perspective(fov, (float)w / (float)h, 40, D - FAR_Z + 2000);
    /* the lens shifted down by E at the plane: ndc y + 2E/vh, i.e. clip y + s w */
    float s = 2 * E / vh;
    for(int c = 0; c < 4; c++) proj.m[c * 4 + 1] += s * proj.m[c * 4 + 3];
    r3_begin(&view, &proj, eye);
    r3_set_light(s_light, 0.5f);
    r3_set_fog(cfg->sky_color2, 1e6f, 2e6f); /* no fog: the haze is in the colours */
    /* the sky as draw.c's: world y 0..1080 from colour 1 to 2, seen through
     * the view -- a sheet behind everything, its top and bottom edges on the
     * screen's (the rasterizer's own sky turns to one colour below the
     * horizon, and the raised eye puts that high up) */
    float t0 = cam_y / 1080.0f, t1 = (cam_y + vh) / 1080.0f;
    uint32_t sky_top = mix(cfg->sky_color1, cfg->sky_color2, t0), sky_bot = mix(cfg->sky_color1, cfg->sky_color2, t1);
    r3_set_sky(sky_top, sky_bot);
    {
        float L = D - FAR_Z + 1200, tn = vh / (2 * D), ax = (float)w / (float)h * L * tn * 1.05f;
        float ytop = eye.y + (1 - s) * L * tn, ybot = eye.y + (-1 - s) * L * tn, z = eye.z - L;
        r3_mesh_clear(&s_sky);
        int a0 = r3_mesh_vertex(&s_sky, r3v(eye.x - ax, ytop, z), s_light, sky_top);
        r3_mesh_vertex(&s_sky, r3v(eye.x + ax, ytop, z), s_light, sky_top);
        r3_mesh_vertex(&s_sky, r3v(eye.x - ax, ybot, z), s_light, sky_bot);
        r3_mesh_vertex(&s_sky, r3v(eye.x + ax, ybot, z), s_light, sky_bot);
        r3_mesh_tri(&s_sky, a0, a0 + 2, a0 + 3);
        r3_mesh_tri(&s_sky, a0, a0 + 3, a0 + 1);
        r3_mesh_bounds(&s_sky);
    }


    /* the finish and the checkpoints */
    r3_mat I = r3_identity();
    float fx = v->terrain->finish_line;
    draw_at(&s_finish, fx, -ground_y(v->terrain, fx), 0);
    for(int i = 0; i < rr_checkpoint_count; i++) {
        const rr_checkpoint_t * cp = &rr_checkpoints[i];
        draw_at(cp->activated ? &s_cp_on : &s_cp_off, cp->x, -cp->y, 0);
    }

    /* what can hit him, where the game has it */
    for(int i = 0; i < rr_obstacle_count; i++) {
        const rr_obstacle_t * o = &rr_obstacles[i];
        draw_pulled(&s_monster, o->x + o->w / 2, -(o->y + o->h / 2));
    }
    float now = v->anim_ms;
    for(int i = 0; i < rr_mine_count; i++) {
        const rr_mine_t * m = &rr_mines[i];
        draw_pulled(&s_mine, m->x, -m->y);
        float pulse = sinf(now / 300.0f + m->pulse) * 0.3f + 0.7f;
        r3_mat t = pulled(m->x, -m->y, 1), o = r3_translate(r3v(0, 0, RR_MINE_R * 0.75f)), sc = r3_scale_m(RR_MINE_R * 0.45f * pulse);
        r3_mat a = r3_mul(&t, &o), ms = r3_mul(&a, &sc);
        r3_draw(&s_mine_core, &ms);
    }
    for(int i = 0; i < rr_saucer_count; i++) {
        const rr_saucer_t * sa = &rr_saucers[i];
        r3_mat t = pulled(sa->x, -sa->y, 1), r = r3_rotate_y(now / 700.0f);
        r3_mat m = r3_mul(&t, &r);
        r3_draw(&s_saucer, &m);
    }
    if(rr_beam_active && rr_beam_saucer_index >= 0) {
        const rr_saucer_t * sa = &rr_saucers[rr_beam_saucer_index];
        r3_mat t = r3_translate(r3v(sa->x, -sa->y - 20, -70));
        r3_mat sc = r3_scale_m(1 + rr_beam_progress / 200.0f);
        r3_mat m = r3_mul(&t, &sc);
        r3_draw(&s_beam, &m);
    }
    for(int i = 0; i < rr_julian_count; i++) {
        const rr_julian_t * j = &rr_julians[i];
        draw_pulled(&s_julian, j->x + RR_JULIAN_W / 2, -(j->y + RR_JULIAN_H / 2));
    }
    for(int i = 0; i < rr_fireball_count; i++) {
        const rr_fireball_t * f = &rr_fireballs[i];
        r3_mat m = pulled(f->x, -f->y, 0.4f + 0.6f * f->life);
        r3_draw(&s_fireball, &m);
    }

    /* Rob and his shadow on the ground below him */
    if(v->rob_visible) {
        float rr = (float)v->rob.radius / 30.0f;
        r3_mat t = pulled(v->rob.x, -v->rob.y, rr), rz = r3_rotate_axis(r3v(0, 0, 1), -v->rob.rotation);
        r3_mat m = r3_mul(&t, &rz);
        r3_draw(&s_rob, &m);
        float gy = ground_y(v->terrain, v->rob.x);
        float above = gy - v->rob.y - (float)v->rob.radius;
        if(above > -5 && above < 600) {
            float slope = rr_terrain_slope_at(v->terrain, v->rob.x);
            float size = (float)v->rob.radius * (1.0f - above / 700.0f);
            r3_mat st = r3_translate(r3v(v->rob.x, -gy + 2.5f, 0)), sr = r3_rotate_axis(r3v(0, 0, 1), -atanf(slope));
            r3_mat ss = r3_scale_m(size), a = r3_mul(&st, &sr), b = r3_mul(&a, &ss);
            r3_draw(&s_shadow, &b);
        }
    }

    /* the particles */
    r3_mesh_clear(&s_parts);
    for(int i = 0; i < RR_EXPLOSION_MAX; i++) {
        const rr_particle_t * p = &v->explosion[i];
        if(p->life > 0) part(p->x, p->y, p->size * (0.5f + 0.5f * p->life), p->color & 0xFFFFFF);
    }
    if(v->won) {
        for(int i = 0; i < RR_FIREWORK_MAX; i++) {
            const rr_particle_t * p = &v->fireworks[i];
            if(p->life > 0) part(p->x, p->y, p->size * 1.6f, p->color & 0xFFFFFF);
        }
        for(int i = 0; i < RR_CONFETTI_MAX; i++) {
            const rr_confetti_t * c = &v->confetti[i];
            if(c->alive) part(c->x, c->y, c->size, c->color & 0xFFFFFF);
        }
    }
    if(s_parts.ni > 0) {
        r3_mesh_bounds(&s_parts);
        r3_mat pm = r3_translate(r3_scale(s_eye, 1 - s_pull)), ps = r3_scale_m(s_pull), pp = r3_mul(&pm, &ps);
        r3_draw(&s_parts, &pp); /* pulled like the actors: eye + (p - eye) * k */
    }

    /* the scenery last, nearest first: what lies behind fails the depth test
     * before it is shaded */
    for(int i = 0; i < s_nground; i++) {
        r3_draw(&s_ground[i], &I);
        r3_draw(&s_face[i], &I);
    }
    for(int i = 0; i < s_nhills; i++) r3_draw(&s_hills[i], &I);
    for(int i = 0; i < s_nclouds; i++) r3_draw(&s_clouds[i], &I);
    for(int i = 0; i < s_nfar; i++) r3_draw(&s_far[i], &I);
    r3_draw(&s_sky, &I);

    static int scale = -1;
    if(scale < 0) {
        const char * e = getenv("ROLLING_ROB_3D_SCALE");
        scale = e ? atoi(e) : 0;
    }
    int sc = scale > 0 ? scale : w >= 1280 ? 2 : 1;
    s_sink = sink;
    s_user = user;
    s_w = w;
    s_h = h;
    r3_render(w, h, sc, band_out, NULL);
    static int stats = -1;
    if(stats < 0) stats = getenv("ROLLING_ROB_3D_STATS") != NULL;
    const r3_stats_t * st = r3_stats();
    if(stats && st->frame % 60 == 0)
        printf("3d: %dx%d, %d of %d objects, %d tris in, %d drawn, geo %.2f raster %.2f total %.2f ms\n", st->w, st->h,
               st->objects_drawn, st->objects, st->tris_in, st->tris_drawn, (double)st->geo_ms, (double)st->raster_ms,
               (double)st->total_ms);
}

/* ---------------- on and off ---------------- */

void rob3d_view_init(void)
{
    const char * e = getenv("ROLLING_ROB_3D");
    rob3d_view_set(e && atoi(e) != 0);
}

bool rob3d_view_on(void) { return s_on; }

void rob3d_view_set(bool on)
{
    s_on = on;
    game_set_renderer(on ? render : NULL);
}
