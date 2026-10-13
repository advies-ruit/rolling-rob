/*
 * The 3D demo (see world3d_view.h).
 *
 * The world: 80 x 80 m of hills (flat in the middle), a climb of platforms
 * to a tower, a few loose platforms, trees, and coins on and between the
 * platforms. Rob is a black ball with a lighter band (so the rolling shows)
 * and eyes that look where he goes. The camera stays behind him; it turns
 * with Q / E or the round pads.
 *
 * Keys: arrows or WASD move (relative to the camera), Space jumps (twice in
 * the air), Q / E turn the camera, R resolution, G detail, T textures on/off,
 * B benchmark;
 * Esc (app.c) or the home button back to the menu. Touch: the pads.
 *
 * Detail 1..5 changes the ground's grid, the number of trees and how round
 * things are; the resolution is the screen divided by 1..4 (scaled back up
 * when shown). The benchmark runs every resolution against every detail for
 * 2.5 s each along a fixed camera path and shows frames per second.
 */

#include "world3d_view.h"
#include "app_page.h"
#include "app.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Laid out for 480x272 and scaled to the screen, like the other games. */
static float s_sc = 1;
static int s_ox, s_oy;
#define K(n)    ((int)((float)(n) * s_sc + 0.5f))
#define FONT(n) rr_ui_font((float)(n) * s_sc)

#define WORLD 40.0f   /* half the world's size */
#define ROB_R 0.6f
#define N_DETAIL 5

static int s_w, s_h;
static bool s_shown;

/* ---------------------------------------------------------------- the world */

typedef struct { float x, z, top, w, d; } plat_t;
static const plat_t k_plat[] = {
    { 0, -5, 1.0f, 3.5f, 3.5f },  { 4, -10, 2.2f, 3, 3 },  { 8, -15, 3.4f, 2.6f, 2.6f }, { 4, -20, 4.6f, 2.6f, 2.6f },
    { -1, -23, 5.8f, 3, 3 },     { -6, -20, 7.0f, 2.6f, 2.6f }, { -9, -15, 8.2f, 2.4f, 2.4f }, { -6, -10, 9.4f, 2.2f, 2.2f },
    { -1, -12, 10.6f, 4.5f, 4.5f }, { 11, 6, 1.2f, 4, 4 }, { -12, 7, 1.6f, 3, 6 }, { 15, -4, 2.0f, 3, 3 },
    { -16, -4, 2.4f, 3.5f, 3.5f }, { 0, 14, 0.8f, 6, 2 },
};
#define N_PLAT ((int)(sizeof(k_plat) / sizeof(k_plat[0])))

#define N_COIN 24
typedef struct { float x, y, z; bool got; } coin_t;
static coin_t s_coin[N_COIN];
static int s_coins_got;

#define MAX_TREES 260
typedef struct { float x, z, s, rot; } tree_t;
static tree_t s_tree[MAX_TREES];
static const int k_trees[N_DETAIL] = { 25, 50, 100, 170, 260 };

static float smooth01(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}

/* Ground height: hills, flattened in the middle where the climb is. */
static float ground_h(float x, float z)
{
    float hill = 1.8f * sinf(x * 0.11f) * cosf(z * 0.13f) + 0.9f * sinf(x * 0.31f + 1.3f) * sinf(z * 0.27f + 0.4f) +
                 0.3f * sinf(x * 0.9f + z * 0.7f);
    float d = sqrtf(x * x + (z + 10) * (z + 10));
    return hill * smooth01(14, 24, d) - 0.4f + 0.4f * smooth01(14, 24, d);
}

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static float rnd01(uint32_t * s)
{
    *s = *s * 1664525u + 1013904223u;
    return (float)(*s >> 8) / 16777216.0f;
}

static void place_world(void)
{
    /* coins: above every platform, and rings on the ground */
    int n = 0;
    for(int i = 0; i < N_PLAT && n < N_COIN; i++) s_coin[n++] = (coin_t){ k_plat[i].x, k_plat[i].top + 1.0f, k_plat[i].z, false };
    for(int i = 0; n < N_COIN; i++) {
        float a = (float)i * 0.62f, r = 8 + (float)(i % 3) * 7;
        float x = cosf(a) * r, z = sinf(a) * r + 2;
        s_coin[n++] = (coin_t){ x, ground_h(x, z) + 1.0f, z, false };
    }
    s_coins_got = 0;
    /* trees: away from the middle and the platforms */
    uint32_t seed = 12345;
    int t = 0;
    while(t < MAX_TREES) {
        float x = (rnd01(&seed) * 2 - 1) * (WORLD - 2), z = (rnd01(&seed) * 2 - 1) * (WORLD - 2);
        if(sqrtf(x * x + (z + 8) * (z + 8)) < 12 || sqrtf(x * x + (z - 4) * (z - 4)) < 8) continue;
        bool near = false;
        for(int i = 0; i < N_PLAT && !near; i++)
            near = fabsf(x - k_plat[i].x) < k_plat[i].w * 0.5f + 2 && fabsf(z - k_plat[i].z) < k_plat[i].d * 0.5f + 2;
        if(near) continue;
        s_tree[t++] = (tree_t){ x, z, 0.8f + rnd01(&seed) * 0.6f, rnd01(&seed) * 6.28f };
    }
}

/* ---------------------------------------------------------------- textures */

enum { TX_GRASS, TX_BRICK, TX_STONE, TX_BARK, TX_LEAF, TX_N };
static r3_texture s_tex[TX_N];
static bool s_textured = true;

static uint32_t rgb3(int r, int g, int b)
{
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return 0xFF000000u | (uint32_t)(r << 16 | g << 8 | b);
}

/* Value noise that wraps at 64, for seamless textures. */
static float vnoise(int x, int y, int cell, uint32_t seed)
{
    int n = 64 / cell;
    int cx = x / cell, cy = y / cell;
    float fx = (float)(x % cell) / (float)cell, fy = (float)(y % cell) / (float)cell;
    float v[4];
    for(int k = 0; k < 4; k++) {
        int ix = (cx + (k & 1)) % n, iy = (cy + (k >> 1)) % n;
        v[k] = (float)(hash((uint32_t)(ix * 73856093) ^ (uint32_t)(iy * 19349663) ^ seed) & 1023) / 1023.0f;
    }
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    return (v[0] * (1 - fx) + v[1] * fx) * (1 - fy) + (v[2] * (1 - fx) + v[3] * fx) * fy;
}

/* All 64 x 64, made here: the vertex colour tints them (the ground's grass
 * and sand are its vertex colours over one neutral texture). */
static void build_textures(void)
{
    static uint32_t px[64 * 64];
    for(int t = 0; t < TX_N; t++) {
        for(int y = 0; y < 64; y++)
            for(int x = 0; x < 64; x++) {
                float n1 = vnoise(x, y, 16, (uint32_t)t * 7u + 1u), n2 = vnoise(x, y, 4, (uint32_t)t * 7u + 2u);
                uint32_t h = hash((uint32_t)(x * 64 + y) ^ (uint32_t)(t * 999)) & 255;
                uint32_t c = 0;
                switch(t) {
                    case TX_GRASS: { /* light, with darker blades */
                        int v = 200 + (int)(n1 * 30) + (int)(n2 * 20) - (h < 40 ? 45 : 0);
                        c = rgb3(v, v, v);
                        break;
                    }
                    case TX_BRICK: { /* 4 rows a metre, offset every other row */
                        int row = y / 16, bx = (x + (row & 1) * 16) % 32;
                        bool mortar = y % 16 < 2 || bx < 2;
                        int v = (int)(n2 * 30) + (int)(h % 16);
                        c = mortar ? rgb3(170 + v / 2, 165 + v / 2, 155 + v / 2)
                                   : rgb3(165 + v + (int)(n1 * 30), 82 + v / 2, 60 + v / 3);
                        break;
                    }
                    case TX_STONE: { /* square slabs */
                        bool joint = x % 32 < 1 || y % 32 < 1;
                        int v = 175 + (int)(n1 * 40) + (int)(n2 * 25);
                        c = joint ? rgb3(110, 105, 95) : rgb3(v, v - 8, v - 22);
                        break;
                    }
                    case TX_BARK: { /* vertical grooves */
                        float gx = vnoise(x, y / 4, 4, 77) * 0.7f + n2 * 0.3f;
                        int v = 70 + (int)(gx * 70);
                        c = rgb3(v + 25, v, v - 25);
                        break;
                    }
                    default: { /* leaves: neutral, tinted green */
                        int v = 150 + (int)(n2 * 80) + (int)(n1 * 25) - (h < 30 ? 50 : 0);
                        c = rgb3(v, v, v);
                        break;
                    }
                }
                px[y * 64 + x] = c;
            }
        r3_texture_init(&s_tex[t], px, 6, 6);
    }
}

static const r3_texture * tex(int t) { return s_textured ? &s_tex[t] : NULL; }

/* ---------------------------------------------------------------- meshes */

#define TILES 8 /* the ground in 8 x 8 pieces, so what is out of view is skipped whole */
static r3_mesh s_ground[TILES * TILES];
static r3_mesh s_trunk_mesh, s_tree_mesh, s_plat_tops, s_coin_mesh, s_body, s_eyes, s_plats, s_shadow;
static int s_detail = 3; /* 1..5 */
static int s_scale_i;    /* index into s_scales */
static int s_scales[4], s_nscales;

static uint32_t ground_rgb(int i, int j, float x, float z, float hgt)
{
    uint32_t v = hash((uint32_t)(i * 7919 + j * 104729)) & 31;
    int r = 70 + (int)v / 2 + (int)(hgt * 6), g = 140 + (int)v + (int)(hgt * 8), b = 60 + (int)v / 3;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    /* a sandy path along the climb */
    float px = x, pz = z + 10, pd = fabsf(sqrtf(px * px + pz * pz) - 11);
    if(pd < 1.2f && hgt < 0.2f) {
        r = 196 + (int)v / 2;
        g = 170 + (int)v / 2;
        b = 120;
    }
    return (uint32_t)(r << 16 | g << 8 | b);
}

/* cells x cells quads over the world, in TILES x TILES meshes */
static void build_ground(int cells)
{
    float step = 2 * WORLD / (float)cells;
    int per = cells / TILES;
    for(int tj = 0; tj < TILES; tj++)
        for(int ti = 0; ti < TILES; ti++) {
            r3_mesh * m = &s_ground[tj * TILES + ti];
            r3_mesh_clear(m);
            m->tex = tex(TX_GRASS);
            for(int j = tj * per; j <= (tj + 1) * per; j++)
                for(int i = ti * per; i <= (ti + 1) * per; i++) {
                    float x = -WORLD + (float)i * step, z = -WORLD + (float)j * step;
                    float e = 0.1f;
                    r3_vec n = r3_norm(r3v(ground_h(x - e, z) - ground_h(x + e, z), 2 * e, ground_h(x, z - e) - ground_h(x, z + e)));
                    float hgt = ground_h(x, z);
                    uint32_t rgb = ground_rgb(i, j, x, z, hgt);
                    if(m->tex) { /* the texture darkens: tint a little brighter */
                        int r = (int)((rgb >> 16) & 255) * 5 / 4, g = (int)((rgb >> 8) & 255) * 5 / 4, b = (int)(rgb & 255) * 5 / 4;
                        rgb = rgb3(r, g, b) & 0xFFFFFF;
                    }
                    r3_mesh_vertex_uv(m, r3v(x, hgt, z), n, rgb, x * 0.5f, z * 0.5f);
                }
            for(int j = 0; j < per; j++)
                for(int i = 0; i < per; i++) {
                    int a = j * (per + 1) + i, b = a + 1, c = a + per + 1, d = c + 1;
                    r3_mesh_tri(m, a, c, d);
                    r3_mesh_tri(m, a, d, b);
                }
            r3_mesh_bounds(m);
        }
}

static void build_meshes(void)
{
    static const int cells[N_DETAIL] = { 24, 40, 64, 96, 128 };
    static const int seg[N_DETAIL] = { 5, 6, 8, 12, 16 };
    static const int sub[N_DETAIL] = { 1, 2, 2, 3, 4 };
    int d = s_detail - 1;
    build_ground(cells[d]);

    r3_mesh_clear(&s_trunk_mesh);
    s_trunk_mesh.tex = tex(TX_BARK);
    r3_mesh_cylinder(&s_trunk_mesh, r3v(0, -0.2f, 0), 0.22f, 1.4f, seg[d] < 6 ? 4 : seg[d] / 2,
                     s_trunk_mesh.tex ? 0xFFFFFF : 0x6D4C2F, s_trunk_mesh.tex ? 0xFFFFFF : 0x6D4C2F);
    r3_mesh_bounds(&s_trunk_mesh);
    r3_mesh_clear(&s_tree_mesh);
    s_tree_mesh.tex = tex(TX_LEAF);
    r3_mesh_cone(&s_tree_mesh, r3v(0, 1.0f, 0), 1.3f, 2.4f, seg[d], s_tree_mesh.tex ? 0x4CB050 : 0x2E7D32);
    if(d >= 2) r3_mesh_cone(&s_tree_mesh, r3v(0, 2.2f, 0), 0.95f, 1.8f, seg[d], s_tree_mesh.tex ? 0x5CC060 : 0x388E3C);
    r3_mesh_bounds(&s_tree_mesh);

    r3_mesh_clear(&s_coin_mesh);
    r3_mesh_cylinder(&s_coin_mesh, r3v(0, -0.07f, 0), 0.45f, 0.14f, seg[d] * 2, 0xE6B800, 0xFFD700);
    r3_mesh_bounds(&s_coin_mesh);

    /* Rob: a black ball with a grey band round it, so the rolling shows */
    r3_mesh_clear(&s_body);
    r3_mesh_sphere(&s_body, r3v(0, 0, 0), ROB_R, sub[d], 0x1A1A1A);
    for(int i = 0; i < s_body.nv; i++)
        if(fabsf(s_body.v[i].px) < ROB_R * 0.22f) s_body.v[i].rgb = 0x9E9E9E;
    r3_mesh_bounds(&s_body);
    r3_mesh_clear(&s_eyes);
    for(int e = -1; e <= 1; e += 2) {
        r3_vec c = r3v((float)e * 0.22f, 0.2f, -0.5f);
        r3_mesh_sphere(&s_eyes, c, 0.17f, d >= 3 ? 2 : 1, 0xFFFFFF);
        r3_mesh_sphere(&s_eyes, r3_add(c, r3v(0, 0.02f, -0.12f)), 0.08f, 1, 0x101010);
    }
    r3_mesh_bounds(&s_eyes);

    /* platforms: brick sides in one mesh, stone tops in another (a texture
     * per mesh); the box's faces are split by their normal */
    r3_mesh_clear(&s_plats);
    r3_mesh_clear(&s_plat_tops);
    s_plats.tex = tex(TX_BRICK);
    s_plat_tops.tex = tex(TX_STONE);
    for(int i = 0; i < N_PLAT; i++) {
        const plat_t * p = &k_plat[i];
        float lo = ground_h(p->x, p->z) - 1.5f;
        r3_mesh box;
        r3_mesh_init(&box);
        bool gold = i == 8;
        uint32_t top = s_plats.tex ? (gold ? 0xFFE080 : 0xFFFFFF) : (gold ? 0xFFD54F : 0xD7C49E);
        uint32_t side = s_plats.tex ? (gold ? 0xFFC060 : 0xFFFFFF) : (gold ? 0xB8860B : 0xA0522D);
        r3_mesh_box(&box, r3v(p->x - p->w / 2, lo, p->z - p->d / 2), r3v(p->x + p->w / 2, p->top, p->z + p->d / 2), top, side);
        for(int f = 0; f < box.ni; f += 6) {
            r3_mesh * to = box.v[box.idx[f]].ny > 0.5f ? &s_plat_tops : &s_plats;
            int base = to->nv;
            for(int k = 0; k < 4; k++) {
                const r3_vertex * v = &box.v[box.idx[f] + k];
                r3_mesh_vertex_uv(to, r3v(v->px, v->py, v->pz), r3v(v->nx, v->ny, v->nz), v->rgb, v->u, v->v);
            }
            r3_mesh_tri(to, base, base + 1, base + 2);
            r3_mesh_tri(to, base, base + 2, base + 3);
        }
        r3_mesh_free(&box);
    }
    r3_mesh_bounds(&s_plats);
    r3_mesh_bounds(&s_plat_tops);

    /* a dark disc under Rob (lit from above only, so no light falls on it) */
    r3_mesh_clear(&s_shadow);
    int c0 = r3_mesh_vertex(&s_shadow, r3v(0, 0, 0), r3v(0, 1, 0), 0x0C1A08);
    for(int i = 0; i < 12; i++) {
        float a = (float)i / 12 * 6.2831853f;
        r3_mesh_vertex(&s_shadow, r3v(cosf(a) * ROB_R * 0.9f, 0, -sinf(a) * ROB_R * 0.9f), r3v(0, 1, 0), 0x0C1A08);
    }
    for(int i = 0; i < 12; i++) r3_mesh_tri(&s_shadow, c0, c0 + 1 + i, c0 + 1 + (i + 1) % 12);
    r3_mesh_bounds(&s_shadow);
}

/* ---------------------------------------------------------------- Rob */

static r3_vec s_pos, s_vel;
static r3_mat s_roll;
static float s_heading; /* radians, 0 = looking down -z */
static bool s_on_ground;
static int s_jumps;
static float s_cam_yaw;   /* the camera's angle round Rob */
static r3_vec s_cam_pos;
static bool s_cam_init;

static void respawn(void)
{
    s_pos = r3v(0, ground_h(0, 4) + ROB_R + 0.5f, 4);
    s_vel = r3v(0, 0, 0);
    s_roll = r3_identity();
    s_heading = 0;
    s_cam_yaw = 0;
    s_cam_init = false;
}

/* Pushes Rob out of the platforms; true when he stands on one. */
static bool collide_platforms(void)
{
    bool stand = false;
    for(int i = 0; i < N_PLAT; i++) {
        const plat_t * p = &k_plat[i];
        float lo = ground_h(p->x, p->z) - 1.5f;
        float cx = fmaxf(p->x - p->w / 2, fminf(s_pos.x, p->x + p->w / 2));
        float cy = fmaxf(lo, fminf(s_pos.y, p->top));
        float cz = fmaxf(p->z - p->d / 2, fminf(s_pos.z, p->z + p->d / 2));
        r3_vec d = r3v(s_pos.x - cx, s_pos.y - cy, s_pos.z - cz);
        float l = r3_len(d);
        if(l >= ROB_R) continue;
        r3_vec n = l > 1e-4f ? r3_scale(d, 1.0f / l) : r3v(0, 1, 0);
        s_pos = r3_add(s_pos, r3_scale(n, ROB_R - l));
        float vn = r3_dot(s_vel, n);
        if(vn < 0) s_vel = r3_sub(s_vel, r3_scale(n, vn));
        if(n.y > 0.7f) stand = true;
    }
    return stand;
}

/* input, filled by keys and pads: forward/back, left/right, camera, jump */
static float s_in_fwd, s_in_side, s_in_cam;
static int s_jump_req;

static void physics(float dt)
{
    float fx = -sinf(s_cam_yaw), fz = -cosf(s_cam_yaw); /* away from the camera */
    float rx = cosf(s_cam_yaw), rz = -sinf(s_cam_yaw);
    float wx = fx * s_in_fwd + rx * s_in_side, wz = fz * s_in_fwd + rz * s_in_side;
    float wl = sqrtf(wx * wx + wz * wz);
    if(wl > 1) {
        wx /= wl;
        wz /= wl;
    }
    const float speed = 8.0f;
    float k = fminf(1.0f, dt * (s_on_ground ? 7.0f : 2.0f));
    s_vel.x += (wx * speed - s_vel.x) * k;
    s_vel.z += (wz * speed - s_vel.z) * k;
    if(wl > 0.1f) {
        float want = atan2f(-wx, -wz), d = want - s_heading;
        while(d > 3.14159f) d -= 6.28318f;
        while(d < -3.14159f) d += 6.28318f;
        s_heading += d * fminf(1.0f, dt * 10);
    }
    while(s_jump_req > 0) {
        s_jump_req--;
        if(s_on_ground || s_jumps < 2) {
            s_vel.y = s_on_ground ? 11.0f : 9.5f;
            s_jumps = s_on_ground ? 1 : s_jumps + 1;
            s_on_ground = false;
        }
    }
    s_vel.y -= 28.0f * dt;
    r3_vec before = s_pos;
    s_pos = r3_add(s_pos, r3_scale(s_vel, dt));
    if(s_pos.x < -WORLD + 1) s_pos.x = -WORLD + 1;
    if(s_pos.x > WORLD - 1) s_pos.x = WORLD - 1;
    if(s_pos.z < -WORLD + 1) s_pos.z = -WORLD + 1;
    if(s_pos.z > WORLD - 1) s_pos.z = WORLD - 1;
    bool stand = collide_platforms();
    float g = ground_h(s_pos.x, s_pos.z) + ROB_R;
    if(s_pos.y <= g) {
        s_pos.y = g;
        if(s_vel.y < 0) s_vel.y = 0;
        stand = true;
    }
    s_on_ground = stand;
    if(stand) s_jumps = 0;
    if(s_pos.y < -20) respawn();
    /* rolling: about the axis across the motion, by distance / radius */
    r3_vec mv = r3v(s_pos.x - before.x, 0, s_pos.z - before.z);
    float dist = r3_len(mv);
    if(dist > 1e-5f) {
        r3_vec axis = r3_cross(r3v(0, 1, 0), r3_scale(mv, 1.0f / dist));
        r3_mat r = r3_rotate_axis(axis, dist / ROB_R);
        s_roll = r3_mul(&r, &s_roll);
    }
    /* coins */
    for(int i = 0; i < N_COIN; i++) {
        coin_t * c = &s_coin[i];
        if(c->got) continue;
        r3_vec d = r3v(c->x - s_pos.x, c->y - s_pos.y, c->z - s_pos.z);
        if(r3_dot(d, d) < 1.2f * 1.2f) {
            c->got = true;
            s_coins_got++;
        }
    }
    if(s_coins_got == N_COIN) place_world();
}

/* ---------------------------------------------------------------- the frame */

static float s_time;
static bool s_bench;
static float s_bench_t;

static void camera(r3_vec * eye, r3_vec * target)
{
    if(s_bench) {
        /* a fixed path round the climb, the same for every configuration */
        float a = s_bench_t * 0.35f;
        *eye = r3v(sinf(a) * 20, 9 + 3 * sinf(a * 0.7f), -10 + cosf(a) * 20);
        *target = r3v(0, 4, -12);
        return;
    }
    r3_vec want = r3v(s_pos.x + sinf(s_cam_yaw) * 7.5f, s_pos.y + 3.2f, s_pos.z + cosf(s_cam_yaw) * 7.5f);
    float gh = ground_h(want.x, want.z) + 0.8f;
    if(want.y < gh) want.y = gh;
    if(!s_cam_init) {
        s_cam_pos = want;
        s_cam_init = true;
    }
    s_cam_pos = r3_add(s_cam_pos, r3_scale(r3_sub(want, s_cam_pos), 0.15f));
    *eye = s_cam_pos;
    *target = r3_add(s_pos, r3v(0, 1.0f, 0));
}

void world3d_render(int out_w, int out_h, r3_sink_t sink, void * user)
{
    r3_vec eye, target;
    camera(&eye, &target);
    r3_mat view = r3_look_at(eye, target, r3v(0, 1, 0));
    r3_mat proj = r3_perspective(1.05f, (float)out_w / (float)out_h, 0.2f, 120);
    r3_begin(&view, &proj, eye);
    r3_set_light(r3v(0.5f, 1.0f, 0.3f), 0.4f);
    r3_set_fog(0xC8DCF0, 18, WORLD + 6);
    r3_set_sky(0x3A78C8, 0xC8DCF0);

    r3_mat I = r3_identity();
    for(int i = 0; i < TILES * TILES; i++) r3_draw(&s_ground[i], &I);
    r3_draw(&s_plats, &I);
    r3_draw(&s_plat_tops, &I);
    int nt = k_trees[s_detail - 1];
    for(int i = 0; i < nt; i++) {
        const tree_t * t = &s_tree[i];
        r3_mat m = r3_translate(r3v(t->x, ground_h(t->x, t->z), t->z));
        r3_mat r = r3_rotate_y(t->rot), s = r3_scale_m(t->s);
        m = r3_mul(&m, &r);
        m = r3_mul(&m, &s);
        r3_draw(&s_trunk_mesh, &m);
        r3_draw(&s_tree_mesh, &m);
    }
    for(int i = 0; i < N_COIN; i++) {
        const coin_t * c = &s_coin[i];
        if(c->got) continue;
        r3_mat m = r3_translate(r3v(c->x, c->y + 0.15f * sinf(s_time * 3 + (float)i), c->z));
        r3_mat ry = r3_rotate_y(s_time * 2.5f + (float)i), rx = r3_rotate_x(1.5708f);
        m = r3_mul(&m, &ry);
        m = r3_mul(&m, &rx);
        r3_draw(&s_coin_mesh, &m);
    }
    /* Rob, his eyes, and a shadow on whatever is below him */
    r3_mat body = r3_translate(s_pos);
    body = r3_mul(&body, &s_roll);
    r3_draw(&s_body, &body);
    r3_mat eyes = r3_translate(s_pos), ry = r3_rotate_y(s_heading);
    eyes = r3_mul(&eyes, &ry);
    r3_draw(&s_eyes, &eyes);
    float floor_y = ground_h(s_pos.x, s_pos.z);
    for(int i = 0; i < N_PLAT; i++) {
        const plat_t * p = &k_plat[i];
        if(fabsf(s_pos.x - p->x) < p->w / 2 && fabsf(s_pos.z - p->z) < p->d / 2 && p->top <= s_pos.y && p->top > floor_y)
            floor_y = p->top;
    }
    float above = s_pos.y - ROB_R - floor_y;
    r3_mat sh = r3_translate(r3v(s_pos.x, floor_y + 0.03f, s_pos.z)), ss = r3_scale_m(fmaxf(0.4f, 1.0f - above * 0.08f));
    sh = r3_mul(&sh, &ss);
    r3_draw(&s_shadow, &sh);

    r3_render(out_w, out_h, s_scales[s_scale_i], sink, user);
}

/* ---------------------------------------------------------------- widgets */

enum { P_UP, P_DOWN, P_LEFT, P_RIGHT, P_JUMP, P_CAML, P_CAMR, P_N };
static lv_obj_t * s_ui, * s_pad[P_N], * s_stats, * s_res_label, * s_det_label, * s_bench_panel, * s_bench_title;
static lv_obj_t * s_cell[5][6];
static lv_obj_t * s_tex_label;
static bool s_lv_down[P_N];
static int s_taps[P_N];
static int s_finger_pad[16];
static void (*s_menu_cb)(void);

void world3d_view_set_menu(void (*cb)(void)) { s_menu_cb = cb; }

static void pad_event(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_PRESSED) {
        s_lv_down[i] = true;
        s_taps[i]++;
    }
    if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) s_lv_down[i] = false;
}

static lv_obj_t * button(const char * text, int x, int y, int w, int h, int font)
{
    lv_obj_t * b = lv_button_create(s_ui);
    lv_obj_set_size(b, K(w), K(h));
    lv_obj_set_pos(b, s_ox + K(x), s_oy + K(y));
    lv_obj_set_style_bg_color(b, lv_color_make(0x20, 0x24, 0x30), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(b, lv_color_make(0x50, 0x58, 0x70), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, lv_color_make(0xC0, 0xC8, 0xD8), 0);
    lv_obj_set_style_border_width(b, K(1), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_60, 0);
    lv_obj_set_style_radius(b, K(6), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_t * l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, FONT(font), 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    return b;
}

static lv_obj_t * pad(const char * text, int x, int y, int size, int i)
{
    lv_obj_t * b = button(text, x, y, size, size, size > 50 ? 14 : 16);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(b, pad_event, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(b, pad_event, LV_EVENT_RELEASED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(b, pad_event, LV_EVENT_PRESS_LOST, (void *)(intptr_t)i);
    s_pad[i] = b;
    return b;
}

static void home_cb(lv_event_t * e) { (void)e; if(s_menu_cb) s_menu_cb(); }
static void bench_start(void);
static void res_next(void);
static void detail_next(void);
static void tex_toggle(void);
static void res_cb(lv_event_t * e) { (void)e; res_next(); }
static void tex_cb(lv_event_t * e) { (void)e; tex_toggle(); }
static void det_cb(lv_event_t * e) { (void)e; detail_next(); }
static void bench_cb(lv_event_t * e) { (void)e; bench_start(); }
static void bench_close_cb(lv_event_t * e) { (void)e; lv_obj_set_hidden(s_bench_panel, true); }

static void labels_update(void)
{
    char buf[48];
    int sc = s_scales[s_scale_i];
    snprintf(buf, sizeof(buf), "%dx%d", (s_w + sc - 1) / sc, (s_h + sc - 1) / sc);
    lv_label_set_text(s_res_label, buf);
    snprintf(buf, sizeof(buf), "Detail %d", s_detail);
    lv_label_set_text(s_det_label, buf);
    if(s_tex_label) lv_label_set_text(s_tex_label, s_textured ? "Tex on" : "Tex off");
}

static void res_next(void)
{
    if(s_bench) return;
    s_scale_i = (s_scale_i + 1) % s_nscales;
    labels_update();
}

static void tex_toggle(void)
{
    if(s_bench) return;
    s_textured = !s_textured;
    build_meshes();
    labels_update();
}

static void detail_next(void)
{
    if(s_bench) return;
    s_detail = s_detail % N_DETAIL + 1;
    build_meshes();
    labels_update();
}

void world3d_view_create(int w, int h)
{
    s_w = w;
    s_h = h;
    s_sc = (float)w / 480.0f < (float)h / 272.0f ? (float)w / 480.0f : (float)h / 272.0f;
    s_ox = (w - K(480)) / 2;
    s_oy = (h - K(272)) / 2;
    /* resolutions: the screen over 1..4, from about 320 wide up to full */
    int want[4] = { (w + 160) / 320, (w + 240) / 480, (w + 320) / 640, 1 };
    s_nscales = 0;
    for(int i = 0; i < 4; i++) {
        int k = want[i] < 1 ? 1 : want[i] > 4 ? 4 : want[i];
        bool dup = false;
        for(int j = 0; j < s_nscales; j++) dup = dup || s_scales[j] == k;
        if(!dup) s_scales[s_nscales++] = k;
    }
    s_scale_i = 0;
    for(int i = 0; i < s_nscales; i++)
        if(s_scales[i] == ((w + 320) / 640 < 1 ? 1 : (w + 320) / 640)) s_scale_i = i;

    for(int i = 0; i < 16; i++) s_finger_pad[i] = -1;
    for(int i = 0; i < TILES * TILES; i++) r3_mesh_init(&s_ground[i]);
    r3_mesh_init(&s_tree_mesh);
    r3_mesh_init(&s_trunk_mesh);
    r3_mesh_init(&s_plat_tops);
    build_textures();
    r3_mesh_init(&s_coin_mesh);
    r3_mesh_init(&s_body);
    r3_mesh_init(&s_eyes);
    r3_mesh_init(&s_plats);
    r3_mesh_init(&s_shadow);
    place_world();
    respawn();

    s_ui = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_ui);
    lv_obj_set_size(s_ui, LV_PCT(100), LV_PCT(100));
    lv_obj_set_clickable(s_ui, false);
    lv_obj_set_scrollable(s_ui, false);
    lv_obj_set_hidden(s_ui, true);

    lv_obj_t * home = button(LV_SYMBOL_HOME, 6, 6, 30, 30, 14);
    lv_obj_add_event_cb(home, home_cb, LV_EVENT_CLICKED, NULL);
    s_stats = lv_label_create(s_ui);
    lv_label_set_recolor(s_stats, true);
    lv_obj_set_style_text_font(s_stats, FONT(10), 0);
    lv_obj_set_style_text_color(s_stats, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_stats, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_stats, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(s_stats, K(3), 0);
    lv_obj_set_style_radius(s_stats, K(3), 0);
    lv_obj_set_pos(s_stats, s_ox + K(42), s_oy + K(6));
    lv_label_set_text(s_stats, "");

    lv_obj_t * tb = button("", 234, 6, 58, 24, 10);
    s_tex_label = lv_obj_get_child(tb, 0);
    lv_obj_add_event_cb(tb, tex_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * rb = button("", 296, 6, 58, 24, 10);
    s_res_label = lv_obj_get_child(rb, 0);
    lv_obj_add_event_cb(rb, res_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * db = button("", 358, 6, 58, 24, 10);
    s_det_label = lv_obj_get_child(db, 0);
    lv_obj_add_event_cb(db, det_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * bb = button(LV_SYMBOL_PLAY " Bench", 420, 6, 54, 24, 10);
    lv_obj_add_event_cb(bb, bench_cb, LV_EVENT_CLICKED, NULL);
    labels_update();

    pad(LV_SYMBOL_UP, 50, 158, 40, P_UP);
    pad(LV_SYMBOL_LEFT, 8, 196, 40, P_LEFT);
    pad(LV_SYMBOL_RIGHT, 92, 196, 40, P_RIGHT);
    pad(LV_SYMBOL_DOWN, 50, 228, 40, P_DOWN);
    pad(LV_SYMBOL_PREV, 300, 226, 38, P_CAML);
    pad(LV_SYMBOL_NEXT, 342, 226, 38, P_CAMR);
    pad("JUMP", 396, 186, 76, P_JUMP);

    /* the benchmark's results: resolutions down, detail across */
    s_bench_panel = lv_obj_create(s_ui);
    lv_obj_remove_style_all(s_bench_panel);
    lv_obj_set_size(s_bench_panel, K(420), K(170));
    lv_obj_align(s_bench_panel, LV_ALIGN_CENTER, 0, K(-6));
    lv_obj_set_style_bg_color(s_bench_panel, lv_color_make(0x10, 0x14, 0x20), 0);
    lv_obj_set_style_bg_opa(s_bench_panel, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_bench_panel, K(6), 0);
    lv_obj_set_style_border_color(s_bench_panel, lv_color_make(0xFF, 0xD7, 0x00), 0);
    lv_obj_set_style_border_width(s_bench_panel, K(1), 0);
    lv_obj_set_scrollable(s_bench_panel, false);
    s_bench_title = lv_label_create(s_bench_panel);
    lv_label_set_recolor(s_bench_title, true);
    lv_obj_set_style_text_font(s_bench_title, FONT(11), 0);
    lv_obj_set_style_text_color(s_bench_title, lv_color_make(0xFF, 0xD7, 0x00), 0);
    lv_obj_set_pos(s_bench_title, K(8), K(6));
    for(int r = 0; r < 5; r++)
        for(int c = 0; c < 6; c++) {
            lv_obj_t * l = lv_label_create(s_bench_panel);
            lv_label_set_recolor(l, true);
            lv_obj_set_style_text_font(l, FONT(10), 0);
            lv_obj_set_style_text_color(l, c == 0 || r == 0 ? lv_color_make(0x9A, 0xA0, 0xB0) : lv_color_white(), 0);
            lv_obj_set_pos(l, K(c == 0 ? 8 : 78 + (c - 1) * 66), K(28 + r * 26));
            lv_label_set_text(l, "");
            s_cell[r][c] = l;
        }
    lv_obj_t * x = lv_button_create(s_bench_panel);
    lv_obj_set_size(x, K(22), K(20));
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, K(-4), K(4));
    lv_obj_set_style_bg_color(x, lv_color_make(0x30, 0x34, 0x40), 0);
    lv_obj_set_style_shadow_width(x, 0, 0);
    lv_obj_t * xl = lv_label_create(x);
    lv_label_set_text(xl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(xl, FONT(10), 0);
    lv_obj_center(xl);
    lv_obj_add_event_cb(x, bench_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_hidden(s_bench_panel, true);
}

void world3d_view_show(bool on)
{
    if(on == s_shown) return;
    s_shown = on;
    lv_obj_set_hidden(s_ui, !on);
    if(on && s_ground[0].nv == 0) build_meshes();
    if(!on) {
        memset(s_lv_down, 0, sizeof(s_lv_down));
        for(int i = 0; i < 16; i++) s_finger_pad[i] = -1;
        s_bench = false;
    }
}

void world3d_view_touch(int slot, bool down, int32_t x, int32_t y)
{
    if(!s_shown || slot < 0 || slot >= 16) return;
    if(!down) {
        s_finger_pad[slot] = -1;
        return;
    }
    int hit = -1;
    for(int i = 0; i < P_N; i++) {
        lv_area_t a;
        lv_obj_get_coords(s_pad[i], &a);
        if(x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) hit = i;
    }
    if(hit >= 0 && hit != s_finger_pad[slot]) s_taps[hit]++;
    s_finger_pad[slot] = hit;
}

void world3d_view_key(int k)
{
    if(k == APP_KEY_SPACE) s_jump_req++;
    else if(k == 'r') res_next();
    else if(k == 'g') detail_next();
    else if(k == 'b') bench_start();
    else if(k == 't') tex_toggle();
}

/* ---------------------------------------------------------------- benchmark */

typedef struct { int scale, detail, frames; float ms, tris, xf, tr, bn, ra, dc, oc; } bench_row_t;
static bench_row_t s_brow[4 * N_DETAIL];
static int s_bn, s_bi; /* configurations, the one running */
static float s_bcfg_t; /* seconds into it */
static int s_saved_detail, s_saved_scale;
#define B_WARM 0.6f
#define B_MEASURE 2.0f

static void bench_apply(void)
{
    s_scale_i = 0;
    for(int i = 0; i < s_nscales; i++)
        if(s_scales[i] == s_brow[s_bi].scale) s_scale_i = i;
    if(s_detail != s_brow[s_bi].detail) {
        s_detail = s_brow[s_bi].detail;
        build_meshes();
    }
    labels_update();
    s_bcfg_t = 0;
    s_bench_t = 0;
}

static void bench_table(bool done)
{
    char buf[96];
    snprintf(buf, sizeof(buf), done ? "FRAMES PER SECOND  #9aa0b0 (and ms per frame drawn)#" : "BENCHMARK %d/%d", s_bi + 1, s_bn);
    lv_label_set_text(s_bench_title, buf);
    lv_label_set_text(s_cell[0][0], "");
    for(int d = 0; d < N_DETAIL; d++) {
        float tris = 0;
        for(int i = 0; i < s_bn; i++)
            if(s_brow[i].detail == d + 1 && s_brow[i].tris > 0) tris = s_brow[i].tris;
        if(tris > 0) snprintf(buf, sizeof(buf), "detail %d\n%.1fk tris", d + 1, (double)tris / 1000.0);
        else snprintf(buf, sizeof(buf), "detail %d", d + 1);
        lv_label_set_text(s_cell[0][d + 1], buf);
    }
    for(int r = 0; r < 4; r++) {
        lv_label_set_text(s_cell[r + 1][0], "");
        for(int c = 1; c < 6; c++) lv_label_set_text(s_cell[r + 1][c], "");
    }
    for(int i = 0; i < s_bn; i++) {
        int r = 1 + i / N_DETAIL, c = 1 + (s_brow[i].detail - 1);
        if(r > 4) break;
        int sc = s_brow[i].scale;
        snprintf(buf, sizeof(buf), "%dx%d", (s_w + sc - 1) / sc, (s_h + sc - 1) / sc);
        lv_label_set_text(s_cell[r][0], buf);
        if(s_brow[i].frames > 0) {
            float fps = (float)s_brow[i].frames / B_MEASURE;
            const char * col = fps >= 55 ? "7fe07f" : fps >= 30 ? "ffd700" : "ff8060";
            snprintf(buf, sizeof(buf), "#%s %.0f#  #9aa0b0 %.1f#", col, (double)fps, (double)(s_brow[i].ms / (float)s_brow[i].frames));
        }
        else snprintf(buf, sizeof(buf), i == s_bi && !done ? "..." : "");
        lv_label_set_text(s_cell[r][c], buf);
    }
}

static void bench_start(void)
{
    if(s_bench) return;
    s_bn = 0;
    for(int s = 0; s < s_nscales; s++)
        for(int d = 1; d <= N_DETAIL; d++) s_brow[s_bn++] = (bench_row_t){ s_scales[s], d, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    s_saved_detail = s_detail;
    s_saved_scale = s_scale_i;
    s_bi = 0;
    s_bench = true;
    bench_apply();
    bench_table(false);
    lv_obj_set_hidden(s_bench_panel, false);
    printf("3d bench: %d configurations, %.1f s each\n", s_bn, (double)(B_WARM + B_MEASURE));
}

/* After each frame drawn in the benchmark. */
static void bench_frame(void)
{
    if(!s_bench || s_bcfg_t < B_WARM) return;
    const r3_stats_t * st = r3_stats();
    bench_row_t * b = &s_brow[s_bi];
    b->frames++;
    b->ms += st->total_ms;
    b->tris = (float)st->tris_drawn;
    b->xf += st->xform_ms;
    b->tr += st->tri_ms;
    b->bn += st->bin_ms;
    b->ra += st->raster_ms;
    b->dc += st->draw_cpu_ms;
    b->oc += st->out_cpu_ms;
}

static void bench_tick(float dt)
{
    s_bcfg_t += dt;
    s_bench_t += dt;
    if(s_bcfg_t < B_WARM + B_MEASURE) return;
    bench_row_t * b = &s_brow[s_bi];
    int sc = b->scale;
    float f = b->frames ? 1.0f / (float)b->frames : 0;
    printf("3d bench: %4dx%-4d detail %d  %5.1f fps  %5.1f ms drawn  %6.0f tris  (vert %.1f tri %.1f bin %.1f raster %.1f; cpu draw %.1f out %.1f)\n",
           (s_w + sc - 1) / sc, (s_h + sc - 1) / sc, b->detail, (double)((float)b->frames / B_MEASURE), (double)(b->ms * f),
           (double)b->tris, (double)(b->xf * f), (double)(b->tr * f), (double)(b->bn * f), (double)(b->ra * f),
           (double)(b->dc * f), (double)(b->oc * f));
    fflush(stdout);
    s_bi++;
    if(s_bi >= s_bn) {
        s_bench = false;
        s_bi = s_bn - 1;
        bench_table(true);
        s_detail = s_saved_detail;
        s_scale_i = s_saved_scale;
        build_meshes();
        labels_update();
        return;
    }
    bench_apply();
    bench_table(false);
}

/* ---------------------------------------------------------------- per tick */

static uint32_t s_frames_seen;

void world3d_view_tick(uint32_t dt_ms)
{
    if(!s_shown) return;
    float dt = (float)dt_ms / 1000.0f;
    if(dt > 0.1f) dt = 0.1f;
    s_time += dt;
    /* the frame drawn since the last tick counts for the benchmark */
    const r3_stats_t * st = r3_stats();
    for(; s_frames_seen != st->frame; s_frames_seen++) bench_frame();
    if(s_bench) {
        bench_tick(dt);
        return;
    }

    bool held[P_N];
    for(int i = 0; i < P_N; i++) {
        held[i] = s_lv_down[i];
        for(int f = 0; f < 16; f++) held[i] = held[i] || s_finger_pad[f] == i;
    }
    s_jump_req += s_taps[P_JUMP];
    memset(s_taps, 0, sizeof(s_taps));
    s_in_fwd = (float)((held[P_UP] || app_key_held(APP_KEY_UP) || app_key_held('w')) -
                       (held[P_DOWN] || app_key_held(APP_KEY_DOWN) || app_key_held('s')));
    s_in_side = (float)((held[P_RIGHT] || app_key_held(APP_KEY_RIGHT) || app_key_held('d')) -
                        (held[P_LEFT] || app_key_held(APP_KEY_LEFT) || app_key_held('a')));
    s_in_cam = (float)((held[P_CAMR] || app_key_held('e')) - (held[P_CAML] || app_key_held('q')));
    s_cam_yaw += s_in_cam * 2.2f * dt;
    /* fixed 120 Hz steps, so the jump height does not depend on the frame rate */
    static float acc;
    acc += dt;
    while(acc >= 1.0f / 120) {
        physics(1.0f / 120);
        acc -= 1.0f / 120;
    }

    static uint32_t at;
    if(lv_tick_elaps(at) >= 500) {
        at = lv_tick_get();
        char buf[260];
        snprintf(buf, sizeof(buf), "#ffd700 %.0f fps#  %.1f ms  %d tris\n#9aa0b0 v%.1f t%.1f b%.1f r%.1f# #ffd700 " LV_SYMBOL_OK " %d/%d#",
                 (double)app_fps(), (double)st->total_ms, st->tris_drawn, (double)st->xform_ms, (double)st->tri_ms,
                 (double)st->bin_ms, (double)st->raster_ms, s_coins_got, N_COIN);
        lv_label_set_text(s_stats, buf);
        snprintf(buf, sizeof(buf), "%.0f fps %.1f ms, %d/%d tris %d obj %d thr, vert %.1f tri %.1f bin %.1f raster %.1f (cpu draw %.1f out %.1f)",
                 (double)app_fps(), (double)st->total_ms, st->tris_drawn, st->tris_in, st->objects_drawn, st->threads,
                 (double)st->xform_ms, (double)st->tri_ms, (double)st->bin_ms, (double)st->raster_ms,
                 (double)st->draw_cpu_ms, (double)st->out_cpu_ms);
        static int every;
        if(++every % 10 == 0) printf("3d: %s\n", buf);
    }
}

void world3d_view_preview(void)
{
    if(s_ground[0].nv == 0) build_meshes();
    int w, h;
    page_pic_frame(&w, &h);
    world3d_render(w, h, page_pic_band, NULL);
    page_pic_widgets(s_ui);
}
