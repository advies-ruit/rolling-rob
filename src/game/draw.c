#include "draw.h"
#include "level.h"
#include "entities.h"
#include "prof.h"
#include "art.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WORLD_W 1920.0f
#define WORLD_H 1080.0f

/* Frame-local shorthand so the ported functions read like the original.
 * Thread-local: draw_mt.c runs rr_draw_frame() on several threads at once,
 * each with its own context owning a share of the rows. */
static _Thread_local gfx_ctx_t * G;
static _Thread_local const rr_view_t * V;
static _Thread_local float S;            /* view scale */
static _Thread_local float SCREEN_W_USER; /* screen width in world units */
static _Thread_local float SCREEN_H_USER;
static bool s_no_sky_mask; /* ROLLING_ROB_NO_SKY_MASK, read once in rr_draw_prepare */
unsigned rr_draw_layers = RR_LAYER_ALL;
static bool s_no_light; /* ROLLING_ROB_NO_LIGHT, read once in rr_draw_prepare */
static bool s_art; /* this frame uses the painted art (art.c); set before the threads start */

#define FILL(c)              gfx_set_fill(G, (c))
#define STROKE(c)            gfx_set_stroke(G, (c))
#define LW(w)                gfx_set_line_width(G, (w))
#define RECT(x, y, w, h)     gfx_fill_rect(G, (x), (y), (w), (h))
#define CIRCLE(x, y, r)      gfx_fill_circle(G, (x), (y), (r))
#define ELLIPSE(x, y, rx, ry) gfx_fill_ellipse(G, (x), (y), (rx), (ry), 0)
#define LINE(a, b, c, d)     gfx_line(G, (a), (b), (c), (d))
#define RGBA(r, g, b, a)     gfx_rgba((r), (g), (b), (a))
#define HEX(h)               GFX_HEX(h)

static inline float now_ms(void) { return V->anim_ms; }
static inline float jsmod(float a, float m) { float r = fmodf(a, m); return r < 0 ? r + m : r; }

/* Is user-space x (under the current transform) within the screen, with a
 * user-space margin? Used to cull the original's "draw everything along the
 * whole level" loops down to what is actually visible. */
static bool vis_x(float x, float margin)
{
    float dx, dy;
    gfx_user_to_device(G, x, 0, &dx, &dy);
    float m = margin * gfx_current_scale(G);
    return dx > -m && dx < (float)G->w + m;
}

static bool vis_xy(float x, float y, float margin)
{
    float dx, dy;
    gfx_user_to_device(G, x, y, &dx, &dy);
    float m = margin * gfx_current_scale(G);
    return dx > -m && dx < (float)G->w + m && dy > -m && dy < (float)G->h + m;
}

/* Does user-space x, y0..y1 (under the current transform) reach the rows
 * this context draws, with a device-pixel margin? In band mode most sky
 * items miss most bands; skipping them before any path or transform work
 * is where a band's fixed cost went. Only for items that draw nothing else
 * and use no gfx_randf (skipping those would shift the sequence). */
static bool vis_rows(float x, float y0, float y1, float margin)
{
    if(G->y_lo <= 0 && G->y_hi >= G->h) return true;
    float dx, d0, d1;
    gfx_user_to_device(G, x, y0, &dx, &d0);
    gfx_user_to_device(G, x, y1, &dx, &d1);
    if(d1 < d0) { float t = d0; d0 = d1; d1 = t; }
    return d1 + margin >= (float)G->y_lo && d0 - margin < (float)G->y_hi;
}

/* ------------------------------------------------------------------ */
/* drawSky                                                             */
/* ------------------------------------------------------------------ */

static void draw_sky(void)
{
    const rr_level_config_t * config = &rr_levels[V->level];
    float finish_line = V->terrain->finish_line;

    /* Vertical gradient over world y 0..1080, seen through the camera. */
    float t0 = V->camera_y / WORLD_H;
    float t1 = (V->camera_y + SCREEN_H_USER) / WORLD_H;
    gfx_vgradient_device(G, 0, G->h, HEX(config->sky_color1), HEX(config->sky_color2), t0, t1);

    float cam = V->camera_x;

    if(config->theme == RR_THEME_SPACE) {
        /* Multiple moons */
        for(int i = 0; i < 3; i++) {
            float moon_x = jsmod((float)i * 800.0f + cam * 0.05f, finish_line + 1500.0f);
            float moon_y = 150.0f + (float)i * 100.0f;
            float moon_size = 60.0f + (float)i * 20.0f;
            if(!vis_x(moon_x, moon_size)) continue;

            gfx_set_fill_radial(G, moon_x, moon_y, 10.0f, moon_size);
            gfx_add_stop(G, 0.0f, HEX(0xFFFFFF));
            gfx_add_stop(G, 1.0f, HEX(0x999999));
            CIRCLE(moon_x, moon_y, moon_size);

            FILL(RGBA(100, 100, 100, 0.3f));
            for(int j = 0; j < 5; j++) {
                float crater_angle = ((float)j / 5.0f) * (float)M_PI * 2.0f;
                float crater_dist = moon_size * 0.6f;
                CIRCLE(moon_x + cosf(crater_angle) * crater_dist, moon_y + sinf(crater_angle) * crater_dist,
                       moon_size * 0.2f);
            }
        }

        /* Sun (far away) */
        float sun_x = jsmod(cam * 0.02f + 500.0f, finish_line + 2000.0f);
        float sun_y = 100.0f;
        if(vis_x(sun_x, 150.0f)) {
            gfx_set_fill_radial(G, sun_x, sun_y, 0, 150.0f);
            gfx_add_stop(G, 0.0f, HEX(0xFFFF00));
            gfx_add_stop(G, 0.3f, HEX(0xFFA500));
            gfx_add_stop(G, 1.0f, RGBA(255, 165, 0, 0.0f));
            CIRCLE(sun_x, sun_y, 150.0f);
            FILL(HEX(0xFFFF00));
            CIRCLE(sun_x, sun_y, 80.0f);
        }

        /* Planets with rings */
        static const uint32_t planet_c0[2] = { 0xFF6B9D, 0x4B0082 };
        static const uint32_t planet_c1[2] = { 0x8B008B, 0x1a0033 };
        for(int i = 0; i < 2; i++) {
            float planet_x = jsmod((float)i * 1000.0f + cam * 0.04f + 300.0f, finish_line + 1800.0f);
            float planet_y = 200.0f + (float)i * 150.0f;
            float planet_size = 70.0f + (float)i * 30.0f;
            if(!vis_x(planet_x, planet_size * 1.8f)) continue;

            gfx_set_fill_radial(G, planet_x, planet_y, 10.0f, planet_size);
            gfx_add_stop(G, 0.0f, HEX(planet_c0[i]));
            gfx_add_stop(G, 1.0f, HEX(planet_c1[i]));
            CIRCLE(planet_x, planet_y, planet_size);

            STROKE(RGBA(200, 150, 100, 0.5f));
            LW(8);
            gfx_stroke_ellipse(G, planet_x, planet_y, planet_size * 1.5f, planet_size * 0.4f, (float)M_PI * 0.2f);
            LW(5);
            STROKE(RGBA(255, 200, 150, 0.3f));
            gfx_stroke_ellipse(G, planet_x, planet_y, planet_size * 1.7f, planet_size * 0.5f, (float)M_PI * 0.2f);
        }

        /* Black hole */
        float bh_x = jsmod(cam * 0.03f + 1200.0f, finish_line + 2000.0f);
        float bh_y = 250.0f;
        if(vis_x(bh_x, 120.0f)) {
            gfx_set_fill_radial(G, bh_x, bh_y, 0, 100.0f);
            gfx_add_stop(G, 0.0f, HEX(0x000000));
            gfx_add_stop(G, 0.5f, HEX(0x1a0033));
            gfx_add_stop(G, 0.7f, HEX(0x4B0082));
            gfx_add_stop(G, 1.0f, RGBA(75, 0, 130, 0.0f));
            CIRCLE(bh_x, bh_y, 100.0f);
            STROKE(RGBA(255, 100, 0, 0.4f));
            LW(3);
            for(int i = 0; i < 5; i++) {
                gfx_stroke_ellipse(G, bh_x, bh_y, 40.0f + (float)i * 15.0f, 10.0f + (float)i * 5.0f, (float)M_PI * 0.3f);
            }
        }

        /* Asteroids everywhere! */
        for(int i = 0; i < 30; i++) {
            float ast_x = jsmod((float)i * 250.0f + cam * 0.6f + now_ms() / 50.0f, finish_line + 1500.0f);
            float ast_y = (float)((i * 137) % (int)(WORLD_H - 100.0f));
            float ast_size = 5.0f + (float)(i % 15);
            float rotation = (now_ms() / 1000.0f + (float)i) * 0.5f;
            if(!vis_x(ast_x, ast_size * 2.0f)) continue;

            gfx_save(G);
            gfx_translate(G, ast_x, ast_y);
            gfx_rotate(G, rotation);
            FILL(HEX(0x8B7355));
            gfx_begin_path(G);
            for(int j = 0; j < 8; j++) {
                float angle = ((float)j / 8.0f) * (float)M_PI * 2.0f;
                float radius = ast_size * (0.7f + gfx_randf(G) * 0.6f);
                float x = cosf(angle) * radius, y = sinf(angle) * radius;
                if(j == 0) gfx_move_to(G, x, y);
                else gfx_line_to(G, x, y);
            }
            gfx_fill(G);
            STROKE(HEX(0x654321));
            LW(1);
            gfx_stroke(G);
            gfx_restore(G);
        }
    }

    /* Spooky background trees for forest/jungle themes */
    if(config->theme == RR_THEME_FOREST || config->theme == RR_THEME_JUNGLE) {
        FILL(RGBA(20, 50, 20, 0.4f));
        for(int i = 0; i < 50; i++) {
            float x = jsmod((float)i * 200.0f + cam * 0.1f, finish_line + 1500.0f);
            float y = 300.0f + (float)((i * 60) % 300);
            if(!vis_x(x, 60.0f) || !vis_rows(x, y - 20.0f, y + 120.0f, 8.0f)) continue;
            gfx_save(G);
            gfx_translate(G, x, y);
            gfx_rotate(G, (float)(i % 3 - 1) * 0.1f);
            RECT(-8, 0, 16, 120);
            RECT(-30, 20, 20, 5);
            RECT(10, 40, 25, 5);
            RECT(-25, 60, 15, 5);
            gfx_begin_path(G);
            gfx_move_to(G, 0, -20);
            gfx_line_to(G, -30, 30);
            gfx_line_to(G, 30, 30);
            gfx_fill(G);
            gfx_restore(G);
        }

        FILL(RGBA(34, 80, 34, 0.5f));
        for(int i = 0; i < 40; i++) {
            float x = jsmod((float)i * 250.0f + cam * 0.15f, finish_line + 1200.0f);
            float y = 350.0f + (float)((i * 40) % 250);
            if(!vis_x(x, 60.0f) || !vis_rows(x, y - 40.0f, y + 130.0f, 4.0f)) continue;
            RECT(x - 12, y, 24, 130);
            for(int j = 0; j < 4; j++) {
                float branch_y = y + (float)j * 30.0f;
                RECT(x - 35, branch_y, 25, 6);
                RECT(x + 10, branch_y + 10, 30, 6);
            }
            gfx_begin_path(G);
            gfx_move_to(G, x, y - 40);
            gfx_line_to(G, x - 45, y + 30);
            gfx_line_to(G, x + 45, y + 30);
            gfx_fill(G);
        }

        FILL(RGBA(34, 100, 34, 0.6f));
        for(int i = 0; i < 30; i++) {
            float x = jsmod((float)i * 300.0f + cam * 0.2f, finish_line + 1000.0f);
            float y = 400.0f + (float)((i * 50) % 200);
            if(!vis_x(x, 60.0f) || !vis_rows(x, y - 60.0f, y + 150.0f, 4.0f)) continue;
            RECT(x - 15, y, 30, 150);
            gfx_begin_path(G);
            gfx_move_to(G, x, y - 60);
            gfx_line_to(G, x - 50, y + 20);
            gfx_line_to(G, x + 50, y + 20);
            gfx_fill(G);
        }

        /* Fog/mist effect */
        FILL(RGBA(200, 200, 200, 0.1f));
        for(int i = 0; i < 10; i++) {
            float x = jsmod((float)i * 400.0f + cam * 0.05f + now_ms() / 200.0f, finish_line + 1500.0f);
            float y = 400.0f + sinf(now_ms() / 1000.0f + (float)i) * 100.0f;
            if(!vis_x(x, 160.0f) || !vis_rows(x, y - 50.0f, y + 50.0f, 4.0f)) continue;
            ELLIPSE(x, y, 150, 50);
        }
    }

    /* Background mountains for outdoor themes */
    if(config->theme == RR_THEME_DESERT || config->theme == RR_THEME_SNOW || config->theme == RR_THEME_VOLCANO) {
        FILL(RGBA(0, 0, 0, 0.2f));
        for(int i = 0; i < 5; i++) {
            float x = jsmod((float)i * 800.0f + cam * 0.1f, finish_line + 1500.0f);
            if(!vis_x(x, 320.0f)) continue;
            gfx_begin_path(G);
            gfx_move_to(G, x - 300, 600);
            gfx_line_to(G, x, 200);
            gfx_line_to(G, x + 300, 600);
            gfx_fill(G);
        }
    }

    /* Add texture based on theme */
    if(config->theme == RR_THEME_SPACE) {
        for(int i = 0; i < 150; i++) {
            float x = jsmod((float)i * 173.0f + cam * 0.5f, finish_line + 1000.0f);
            float y = (float)((i * 241) % (int)WORLD_H);
            if(!vis_x(x, 5.0f) || !vis_rows(x, y, y + 3.0f, 4.0f)) continue;
            float twinkle = sinf(now_ms() / 500.0f + (float)i) * 0.5f + 0.5f;
            FILL(RGBA(255, 255, 255, twinkle));
            RECT(x, y, 3, 3);
        }
    }
    else if(config->theme == RR_THEME_SNOW) {
        FILL(RGBA(255, 255, 255, 0.6f));
        for(int i = 0; i < 100; i++) {
            float x = jsmod((float)i * 197.0f + now_ms() / 50.0f, finish_line + 1000.0f);
            float y = jsmod((float)i * 223.0f + now_ms() / 30.0f, WORLD_H);
            if(!vis_x(x, 5.0f) || !vis_rows(x, y - 3.0f, y + 3.0f, 4.0f)) continue;
            CIRCLE(x, y, 3);
        }
    }
    else if(config->theme == RR_THEME_VOLCANO) {
        for(int i = 0; i < 80; i++) {
            float x = jsmod((float)i * 211.0f + now_ms() / 40.0f, finish_line + 1000.0f);
            float y = jsmod((float)i * 193.0f + now_ms() / 20.0f, WORLD_H);
            if(!vis_x(x, 5.0f) || !vis_rows(x, y - 2.0f, y + 2.0f, 4.0f)) continue;
            FILL(RGBA(255, 100 + i % 100, 0, 0.3f + (float)(i % 5) * 0.1f));
            CIRCLE(x, y, 2);
        }
    }
    else if(config->theme == RR_THEME_CANDY) {
        for(int i = 0; i < 50; i++) {
            float x = jsmod((float)i * 227.0f + now_ms() / 60.0f, finish_line + 1000.0f);
            float y = jsmod((float)i * 251.0f + sinf(now_ms() / 1000.0f + (float)i) * 50.0f, WORLD_H);
            if(!vis_x(x, 10.0f) || !vis_rows(x, y, y + 8.0f, 4.0f)) continue;
            FILL(gfx_hsl((float)i * 50.0f, 1.0f, 0.7f));
            RECT(x, y, 8, 8);
        }
    }

    /* Clouds for outdoor themes */
    if(config->theme == RR_THEME_FOREST || config->theme == RR_THEME_DESERT || config->theme == RR_THEME_JUNGLE) {
        FILL(RGBA(255, 255, 255, 0.3f));
        for(int i = 0; i < 8; i++) {
            float x = jsmod((float)i * 400.0f + cam * 0.3f + now_ms() / 100.0f, finish_line + 1000.0f);
            float y = 100.0f + (float)((i * 80) % 300);
            if(!vis_x(x, 140.0f) || !vis_rows(x, y - 40.0f, y + 40.0f, 4.0f)) continue;
            /* One path with three ellipses in the original; drawn as three
             * separate fills here, so the overlaps double-blend slightly. */
            gfx_begin_path(G);
            gfx_path_ellipse(G, x, y, 80, 40, 0);
            gfx_path_ellipse(G, x + 50, y, 60, 35, 0);
            gfx_path_ellipse(G, x - 50, y, 60, 35, 0);
            gfx_fill(G);
        }
    }
}

/* ------------------------------------------------------------------ */
/* drawMountain                                                        */
/* ------------------------------------------------------------------ */

/* The opaque ground polygon: the visible stretch of the ridge, closed far
 * below the screen. Shared by the fill and by the sky mask, which must cover
 * exactly the same pixels. */
static void ground_path(int i0, int i1)
{
    const rr_terrain_t * t = V->terrain;
    int ridge_count = t->count - 2;
    gfx_begin_path(G);
    for(int i = i0; i <= i1; i++) {
        if(i == i0) gfx_move_to(G, t->points[i].x, t->points[i].y);
        else gfx_line_to(G, t->points[i].x, t->points[i].y);
    }
    if(i1 == ridge_count - 1) {
        /* include the original's closing slope down to the bottom */
        gfx_line_to(G, t->points[ridge_count].x, t->points[ridge_count].y);
    }
    gfx_line_to(G, t->points[i1 == ridge_count - 1 ? ridge_count : i1].x, WORLD_H + 600.0f);
    gfx_line_to(G, t->points[i0].x, WORLD_H + 600.0f);
}

static void ground_range(int * i0_out, int * i1_out)
{
    const rr_terrain_t * t = V->terrain;
    int ridge_count = t->count - 2; /* last two points close the polygon */
    int i0 = 0, i1 = ridge_count - 1;
    for(int i = 0; i < ridge_count; i++) {
        if(vis_x(t->points[i].x, 0)) { i0 = i > 0 ? i - 1 : 0; break; }
        i0 = i;
    }
    /* Up to the first point past the right edge. The test is made where
     * the point is drawn: the camera is snapped to whole pixels, and a point
     * just left of the screen can lie right of the unsnapped camera_x --
     * comparing with that ended the range at i0, and the ground vanished
     * for a frame now and then. */
    for(int i = i0; i < ridge_count; i++) {
        i1 = i;
        float dx, dy;
        gfx_user_to_device(G, t->points[i].x, 0, &dx, &dy);
        if(dx >= (float)G->w) break;
    }
    *i0_out = i0;
    *i1_out = i1;
}

static void draw_mountain(void)
{
    const rr_level_config_t * config = &rr_levels[V->level];
    const rr_terrain_t * t = V->terrain;

    /* Visible ridge range */
    int i0, i1;
    ground_range(&i0, &i1);

    FILL(HEX(config->ground_color));
    ground_path(i0, i1);
    gfx_fill(G);

    /* Ground texture - rocks, grass, GOLD VEINS, DIAMONDS, MOLES! */
    for(int i = (i0 < 1 ? 1 : i0); i <= i1 && i < t->count - 2; i++) {
        const rr_point_t * p = &t->points[i];
        /* everything below reaches from p->y - 18 (a mole's whiskers) to
         * p->y + 60 (the gold arc's control point) */
        if(!vis_rows(p->x, p->y - 20.0f, p->y + 62.0f, 6.0f)) continue;

        STROKE(RGBA(0, 0, 0, 0.3f));
        LW(2);
        for(int j = 0; j < 5; j++) {
            LINE(p->x + j * 15, p->y + j * 3, p->x + j * 15, p->y + j * 3 + 8);
        }

        if(i % 5 == 0) {
            FILL(RGBA(0, 0, 0, 0.2f));
            CIRCLE(p->x + 20, p->y + 15, 8);
            CIRCLE(p->x + 35, p->y + 18, 6);
        }

        if(i % 7 == 0) {
            STROKE(HEX(0xFFD700));
            LW(3);
            gfx_begin_path(G);
            gfx_move_to(G, p->x, p->y + 20);
            gfx_line_to(G, p->x + 30, p->y + 35);
            gfx_line_to(G, p->x + 50, p->y + 30);
            gfx_line_to(G, p->x + 70, p->y + 45);
            gfx_stroke(G);
            FILL(HEX(0xFFD700));
            CIRCLE(p->x + 30, p->y + 35, 4);
            CIRCLE(p->x + 50, p->y + 30, 5);
        }

        if(i % 9 == 0) {
            float dx = p->x + 40, dy = p->y + 25, ds = 8;
            FILL(HEX(0x00FFFF));
            gfx_begin_path(G);
            gfx_move_to(G, dx, dy - ds);
            gfx_line_to(G, dx - ds * 0.7f, dy);
            gfx_line_to(G, dx, dy + ds);
            gfx_line_to(G, dx + ds * 0.7f, dy);
            gfx_fill(G);
            FILL(HEX(0xFFFFFF));
            RECT(dx - 2, dy - 2, 4, 4);

            float d2x = p->x + 60, d2y = p->y + 40;
            FILL(HEX(0xFF00FF));
            gfx_begin_path(G);
            gfx_move_to(G, d2x, d2y - 6);
            gfx_line_to(G, d2x - 5, d2y);
            gfx_line_to(G, d2x, d2y + 6);
            gfx_line_to(G, d2x + 5, d2y);
            gfx_fill(G);
        }

        if(i % 11 == 0) {
            float mx = p->x + 25, my = p->y + 30;
            FILL(RGBA(101, 67, 33, 0.8f));
            ELLIPSE(mx, my, 15, 8);
            FILL(HEX(0x654321));
            CIRCLE(mx, my - 8, 10);
            FILL(HEX(0xFFB6C1));
            CIRCLE(mx, my - 5, 3);
            FILL(HEX(0x000000));
            RECT(mx - 5, my - 10, 2, 2);
            RECT(mx + 3, my - 10, 2, 2);
            STROKE(HEX(0x000000));
            LW(1);
            for(int w = -1; w <= 1; w += 2) {
                LINE(mx + w * 10, my - 5, mx + w * 18, my - 4);
                LINE(mx + w * 10, my - 3, mx + w * 18, my - 2);
            }
            FILL(HEX(0x8B4513));
            RECT(mx - 12, my, 5, 3);
            RECT(mx + 7, my, 5, 3);
        }

        if(i % 6 == 0) {
            STROKE(RGBA(255, 215, 0, 0.6f));
            LW(4);
            gfx_begin_path(G);
            gfx_move_to(G, p->x + 10, p->y + 50);
            gfx_quad_to(G, p->x + 40, p->y + 60, p->x + 80, p->y + 55);
            gfx_stroke(G);
        }

        if(i % 8 == 0) {
            STROKE(RGBA(192, 192, 192, 0.7f));
            LW(3);
            gfx_begin_path(G);
            gfx_move_to(G, p->x + 5, p->y + 40);
            gfx_line_to(G, p->x + 25, p->y + 50);
            gfx_line_to(G, p->x + 45, p->y + 45);
            gfx_stroke(G);
        }
    }

    /* Ridge outline */
    STROKE(RGBA(0, 0, 0, 0.4f));
    LW(4);
    gfx_begin_path(G);
    for(int i = i0; i <= i1; i++) {
        if(i == i0) gfx_move_to(G, t->points[i].x, t->points[i].y);
        else gfx_line_to(G, t->points[i].x, t->points[i].y);
    }
    gfx_stroke(G);
}

/* ------------------------------------------------------------------ */
/* Finish line / checkpoints / mines                                   */
/* ------------------------------------------------------------------ */

static void draw_finish_line(void)
{
    float fx = V->terrain->finish_line;
    if(!vis_x(fx, 100.0f)) return;
    float terrain_y = rr_terrain_height_at(V->terrain, fx);

    FILL(RGBA(255, 215, 0, 0.3f));
    RECT(fx - 5, 0, 10, terrain_y);

    STROKE(HEX(0xFFD700));
    LW(4);
    gfx_dashed_line(G, fx, 0, fx, terrain_y, 10, 10);

    gfx_fill_text(G, "FINISH", fx - 35, terrain_y + 30, 20, 0, HEX(0xFFD700), 0);
}

static void draw_checkpoints(void)
{
    for(int i = 0; i < rr_checkpoint_count; i++) {
        const rr_checkpoint_t * cp = &rr_checkpoints[i];
        if(!vis_x(cp->x, 80.0f)) continue;

        FILL(cp->activated ? HEX(0x00FF00) : HEX(0xFF0000));
        RECT(cp->x, cp->y, 8, RR_CP_H);

        FILL(cp->activated ? RGBA(0, 255, 0, 0.7f) : RGBA(255, 0, 0, 0.7f));
        gfx_begin_path(G);
        gfx_move_to(G, cp->x + 8, cp->y);
        gfx_line_to(G, cp->x + RR_CP_W, cp->y + 20);
        gfx_line_to(G, cp->x + 8, cp->y + 40);
        gfx_fill(G);

        char buf[8];
        snprintf(buf, sizeof(buf), "CP%d", i + 1);
        gfx_fill_text(G, buf, cp->x + 15, cp->y + 25, 16, 0, HEX(0xFFFFFF), 0);

        if(cp->activated) {
            FILL(RGBA(0, 255, 0, 0.3f));
            CIRCLE(cp->x + 4, cp->y + RR_CP_H / 2, 40);
        }
    }
}

static void draw_mines(void)
{
    if(s_art) {
        for(int i = 0; i < rr_mine_count; i++) {
            const rr_mine_t * m = &rr_mines[i];
            if(!vis_x(m->x, 40.0f)) continue;
            float pulse = sinf(now_ms() / 300.0f + m->pulse) * 0.3f + 0.7f;
            FILL(RGBA(255, 0, 0, pulse * 0.3f));
            CIRCLE(m->x, m->y, RR_MINE_R * 1.5f);
            rr_art_mine(G, V, m);
        }
        return;
    }
    for(int i = 0; i < rr_mine_count; i++) {
        const rr_mine_t * m = &rr_mines[i];
        if(!vis_x(m->x, 40.0f) || !vis_rows(m->x, m->y - RR_MINE_R * 1.5f, m->y + RR_MINE_R * 1.5f, 6.0f)) continue;
        float pulse = sinf(now_ms() / 300.0f + m->pulse) * 0.3f + 0.7f;
        float r = RR_MINE_R;

        FILL(HEX(0x333333));
        CIRCLE(m->x, m->y, r);

        STROKE(HEX(0x666666));
        LW(4);
        for(int k = 0; k < 8; k++) {
            float angle = ((float)M_PI * 2.0f * (float)k) / 8.0f;
            LINE(m->x, m->y, m->x + cosf(angle) * r * 1.5f, m->y + sinf(angle) * r * 1.5f);
        }

        FILL(RGBA(255, 0, 0, pulse));
        CIRCLE(m->x, m->y, r * 0.5f);
        FILL(RGBA(255, 0, 0, pulse * 0.3f));
        CIRCLE(m->x, m->y, r * 1.5f);
    }
}

/* ------------------------------------------------------------------ */
/* drawMonster                                                         */
/* ------------------------------------------------------------------ */

static void draw_monster(const rr_obstacle_t * obs)
{
    if(s_art) {
        if(vis_x(obs->x, 80.0f)) rr_art_monster(G, V, obs);
        return;
    }
    if(!vis_x(obs->x, 80.0f)) return;
    /* drawn around the centre at 1.7x, at most 25 units out */
    float oy = obs->y + obs->h / 2;
    if(!vis_rows(obs->x, oy - 45.0f, oy + 45.0f, 4.0f)) return;
    rr_monster_t type = rr_levels[V->level].monster_type;
    const float scale = 1.7f;
    gfx_save(G);
    gfx_translate(G, obs->x + obs->w / 2, obs->y + obs->h / 2);
    gfx_scale(G, scale, scale);

    switch(type) {
        case RR_MONSTER_SLIME:
            FILL(HEX(0x4CAF50));
            ELLIPSE(0, 5, 18, 15);
            FILL(HEX(0x000000));
            RECT(-8, -5, 6, 8);
            RECT(2, -5, 6, 8);
            break;
        case RR_MONSTER_CACTUS:
            FILL(HEX(0x2E7D32));
            RECT(-8, -15, 16, 30);
            RECT(-15, -5, 10, 10);
            RECT(5, -5, 10, 10);
            break;
        case RR_MONSTER_YETI:
            FILL(HEX(0xE0E0E0));
            RECT(-10, -10, 20, 25);
            FILL(HEX(0x000000));
            RECT(-8, -8, 4, 4);
            RECT(4, -8, 4, 4);
            FILL(HEX(0x555555));
            RECT(-3, 0, 6, 3);
            break;
        case RR_MONSTER_LAVA:
            FILL(HEX(0xFF4500));
            gfx_begin_path(G);
            gfx_move_to(G, -15, 15);
            gfx_line_to(G, -10, -15);
            gfx_line_to(G, -5, 15);
            gfx_line_to(G, 0, -15);
            gfx_line_to(G, 5, 15);
            gfx_line_to(G, 10, -15);
            gfx_line_to(G, 15, 15);
            gfx_fill(G);
            break;
        case RR_MONSTER_PLANT:
            FILL(HEX(0x1B5E20));
            RECT(-5, -5, 10, 20);
            /* The original sets fillStyle red but strokes with the last
             * strokeStyle; we keep the intent (red-ish rings). */
            STROKE(HEX(0xFF0000));
            LW(1);
            for(int i = 0; i < 5; i++) {
                gfx_stroke_arc(G, 0, -10, 15 - i * 3, 0, (float)M_PI * 2.0f);
            }
            break;
        case RR_MONSTER_ALIEN:
            FILL(HEX(0x00FF00));
            ELLIPSE(0, -5, 12, 15);
            FILL(HEX(0x000000));
            CIRCLE(-5, -8, 4);
            CIRCLE(5, -8, 4);
            break;
        case RR_MONSTER_CANDY:
            FILL(HEX(0xFF69B4));
            CIRCLE(0, 0, 15);
            FILL(HEX(0xFFFFFF));
            CIRCLE(-5, -5, 3);
            CIRCLE(5, 5, 3);
            break;
        case RR_MONSTER_ROBOT:
            FILL(HEX(0x607D8B));
            RECT(-10, -12, 20, 24);
            FILL(HEX(0x00FFFF));
            RECT(-7, -8, 5, 5);
            RECT(2, -8, 5, 5);
            RECT(-5, 2, 10, 3);
            break;
        case RR_MONSTER_DEMON:
            FILL(HEX(0x8B0000));
            RECT(-10, -10, 20, 20);
            FILL(HEX(0xFF0000));
            gfx_begin_path(G);
            gfx_move_to(G, -12, -12);
            gfx_line_to(G, -8, -18);
            gfx_line_to(G, -4, -12);
            gfx_fill(G);
            gfx_begin_path(G);
            gfx_move_to(G, 12, -12);
            gfx_line_to(G, 8, -18);
            gfx_line_to(G, 4, -12);
            gfx_fill(G);
            break;
        case RR_MONSTER_CRYSTAL:
            FILL(HEX(0x9C27B0));
            gfx_begin_path(G);
            gfx_move_to(G, 0, -15);
            gfx_line_to(G, -10, 5);
            gfx_line_to(G, -3, 15);
            gfx_line_to(G, 3, 15);
            gfx_line_to(G, 10, 5);
            gfx_fill(G);
            STROKE(HEX(0xFFFFFF));
            LW(2);
            gfx_stroke(G);
            break;
    }
    gfx_restore(G);
}

/* ------------------------------------------------------------------ */
/* drawSaucer / drawBeam                                               */
/* ------------------------------------------------------------------ */

static void draw_saucer(const rr_saucer_t * saucer)
{
    if(s_art) {
        if(!vis_x(saucer->x, 60.0f)) return;
        FILL(RGBA(0, 0, 0, 0.2f));
        ELLIPSE(saucer->x, saucer->y + 50, 45, 10);
        rr_art_saucer(G, V, saucer);
        return;
    }
    if(!vis_x(saucer->x, 60.0f)) return;
    gfx_save(G);
    gfx_translate(G, saucer->x, saucer->y);

    FILL(RGBA(0, 0, 0, 0.2f));
    ELLIPSE(0, 50, 45, 10);

    gfx_set_fill_radial(G, 0, 0, 0, 40);
    gfx_add_stop(G, 0.0f, HEX(0xE0E0E0));
    gfx_add_stop(G, 1.0f, HEX(0x808080));
    ELLIPSE(0, 0, 40, 16);
    STROKE(HEX(0x606060));
    LW(2);
    gfx_stroke_ellipse(G, 0, 0, 40, 16, 0);

    /* Dome: upper half ellipse */
    gfx_set_fill_radial(G, 0, -12, 0, 20);
    gfx_add_stop(G, 0.0f, HEX(0x00FFFF));
    gfx_add_stop(G, 1.0f, HEX(0x0088AA));
    gfx_begin_path(G);
    {
        const int n = 16;
        for(int i = 0; i <= n; i++) {
            float a = (float)M_PI + (float)M_PI * (float)i / (float)n;
            if(i == 0) gfx_move_to(G, cosf(a) * 20.0f, -12.0f + sinf(a) * 14.0f);
            else gfx_line_to(G, cosf(a) * 20.0f, -12.0f + sinf(a) * 14.0f);
        }
    }
    gfx_fill(G);

    FILL(RGBA(255, 255, 255, 0.4f));
    ELLIPSE(-5, -16, 8, 6);

    static const uint32_t light_colors[3] = { 0xFF0000, 0x00FF00, 0x0000FF };
    float pulse = sinf(now_ms() / 200.0f) * 0.3f + 0.7f;
    for(int i = 0; i < 3; i++) {
        FILL(gfx_with_alpha(HEX(light_colors[i]), 0x40 / 255.0f));
        CIRCLE(-24 + i * 24, 8, 10);
        FILL(HEX(light_colors[i]));
        CIRCLE(-24 + i * 24, 8, 5 * pulse);
    }

    STROKE(HEX(0x808080));
    LW(2);
    LINE(0, -26, 0, -32);
    FILL(HEX(0xFF0000));
    CIRCLE(0, -34, 3);

    gfx_restore(G);
}

static void draw_beam(void)
{
    if(!rr_beam_active || rr_beam_saucer_index < 0) return;
    const rr_saucer_t * saucer = &rr_saucers[rr_beam_saucer_index];
    if(!vis_x(saucer->x, 200.0f)) return;

    gfx_save(G);
    gfx_set_global_alpha(G, 0.6f);

    gfx_set_fill_linear(G, saucer->x, saucer->y + 20, saucer->x, saucer->y + 300);
    gfx_add_stop(G, 0.0f, RGBA(0, 255, 255, 0.8f));
    gfx_add_stop(G, 1.0f, RGBA(0, 255, 255, 0.1f));
    gfx_begin_path(G);
    gfx_move_to(G, saucer->x - 20, saucer->y + 20);
    gfx_line_to(G, saucer->x - 50 - rr_beam_progress * 0.5f, saucer->y + 300);
    gfx_line_to(G, saucer->x + 50 + rr_beam_progress * 0.5f, saucer->y + 300);
    gfx_line_to(G, saucer->x + 20, saucer->y + 20);
    gfx_fill(G);

    for(int i = 0; i < 20; i++) {
        float t = jsmod(now_ms() / 50.0f + (float)i, 1.0f);
        float x = saucer->x + (gfx_randf(G) - 0.5f) * 50.0f;
        float y = saucer->y + 20 + t * 280;
        FILL(RGBA(0, 255, 255, 1.0f - t));
        RECT(x, y, 3, 8);
    }

    gfx_set_global_alpha(G, 1.0f);
    gfx_restore(G);
}

/* ------------------------------------------------------------------ */
/* drawJulian / drawFireballs                                          */
/* ------------------------------------------------------------------ */

static void draw_julian(const rr_julian_t * julian)
{
    if(s_art) {
        if(!vis_x(julian->x, 120.0f)) return;
        rr_art_julian(G, V, julian);
        gfx_fill_text(G, "JULIAN", julian->x + RR_JULIAN_W / 2, julian->y + RR_JULIAN_H / 2 - 90, 20, 1,
                      HEX(0xFFD700), HEX(0x000000));
        return;
    }
    if(!vis_x(julian->x, 120.0f)) return;
    gfx_save(G);
    gfx_translate(G, julian->x + RR_JULIAN_W / 2, julian->y + RR_JULIAN_H / 2);

    FILL(HEX(0x8B0000));
    RECT(-50, -60, 100, 120);

    FILL(HEX(0x4d0000));
    RECT(-45, -55, 90, 30);
    RECT(-45, -15, 90, 30);
    RECT(-45, 25, 90, 30);

    FILL(HEX(0xFFD700));
    CIRCLE(-20, -40, 8);
    CIRCLE(20, -40, 8);
    FILL(HEX(0xFF0000));
    CIRCLE(-20, -40, 4);
    CIRCLE(20, -40, 4);

    STROKE(HEX(0x000000));
    LW(3);
    gfx_stroke_arc(G, 0, -20, 15, 0.2f, (float)M_PI - 0.2f);

    FILL(HEX(0xFFFFFF));
    for(int i = -12; i <= 12; i += 6) {
        gfx_begin_path(G);
        gfx_move_to(G, i, -10);
        gfx_line_to(G, i - 3, -5);
        gfx_line_to(G, i + 3, -5);
        gfx_fill(G);
    }

    FILL(HEX(0x4d0000));
    gfx_begin_path(G);
    gfx_move_to(G, -40, -60);
    gfx_line_to(G, -50, -80);
    gfx_line_to(G, -35, -65);
    gfx_fill(G);
    gfx_begin_path(G);
    gfx_move_to(G, 40, -60);
    gfx_line_to(G, 50, -80);
    gfx_line_to(G, 35, -65);
    gfx_fill(G);

    gfx_fill_text(G, "JULIAN", 0, -90, 20, 1, HEX(0xFFD700), HEX(0x000000));

    gfx_restore(G);
}

static void draw_fireballs(void)
{
    for(int i = 0; i < rr_fireball_count; i++) {
        const rr_fireball_t * fb = &rr_fireballs[i];
        if(!vis_xy(fb->x, fb->y, 40.0f)) continue;
        const float size = 15.0f;
        gfx_set_global_alpha(G, fb->life);

        gfx_set_fill_radial(G, fb->x, fb->y, 0, size * 2);
        gfx_add_stop(G, 0.0f, HEX(0xFFFF00));
        gfx_add_stop(G, 0.3f, HEX(0xFF6600));
        gfx_add_stop(G, 1.0f, RGBA(255, 0, 0, 0.0f));
        CIRCLE(fb->x, fb->y, size * 2);

        FILL(HEX(0xFFFF00));
        CIRCLE(fb->x, fb->y, size);
        FILL(HEX(0xFF4500));
        CIRCLE(fb->x - 3, fb->y, size * 0.7f);

        gfx_set_global_alpha(G, 1.0f);
    }
}

/* ------------------------------------------------------------------ */
/* drawRob                                                             */
/* ------------------------------------------------------------------ */

static void draw_rob(void)
{
    if(s_art) {
        rr_art_rob(G, V);
        return;
    }
    const rr_rob_view_t * rob = &V->rob;
    gfx_save(G);
    gfx_translate(G, rob->x, rob->y);
    gfx_rotate(G, rob->rotation);
    float r = (float)rob->radius;

    /* Cape/hoodie (drawn first here so it sits behind the head; the
     * original draws it last but the arc segment ends up hidden anyway) */
    FILL(HEX(0x000000));
    gfx_begin_path(G);
    gfx_path_arc(G, 0, 0, r + 5, (float)M_PI * 0.25f, (float)M_PI * 0.75f, false);
    gfx_fill(G);

    FILL(HEX(0x1a1a1a));
    CIRCLE(0, 0, r);
    STROKE(HEX(0x000000));
    LW(3);
    gfx_stroke_arc(G, 0, 0, r, 0, (float)M_PI * 2.0f);

    FILL(HEX(0x444444));
    CIRCLE(-8, -10, 8);

    FILL(HEX(0xFFD700));
    CIRCLE(-8, -5, 5);
    CIRCLE(8, -5, 5);

    STROKE(HEX(0xFF0000));
    LW(1);
    LINE(-10, -5, -6, -5);
    LINE(-8, -7, -8, -3);
    LINE(6, -5, 10, -5);
    LINE(8, -7, 8, -3);

    FILL(HEX(0x000000));
    CIRCLE(-8, -5, 2);
    CIRCLE(8, -5, 2);

    STROKE(HEX(0xFFD700));
    LW(2);
    gfx_stroke_arc(G, 0, 3, 12, 0.1f, (float)M_PI - 0.1f);

    FILL(HEX(0xFFFFFF));
    for(int i = -8; i <= 8; i += 4) RECT(i, 8, 3, 4);

    FILL(HEX(0xFFFFFF));
    RECT(12, 5, 18, 3);
    FILL(HEX(0xFF4500));
    CIRCLE(30, 6.5f, 3);

    float time = now_ms() / 100.0f;
    for(int i = 0; i < 5; i++) {
        FILL(RGBA(200, 200, 200, 0.6f - (float)i * 0.1f));
        float offset = jsmod(time + (float)i * 0.5f, 3.0f);
        CIRCLE(30 + offset * 8, 6.5f - offset * 12, 3 + offset * 2);
    }

    FILL(HEX(0x2a2a2a));
    RECT(-10, 15, 20, 7);
    STROKE(HEX(0x555555));
    LW(2);
    for(int i = -8; i <= 8; i += 4) LINE(i, 16, i, 21);

    gfx_restore(G);
}

/* ------------------------------------------------------------------ */
/* Particles                                                           */
/* ------------------------------------------------------------------ */

static void draw_confetti(void)
{
    for(int i = 0; i < RR_CONFETTI_MAX; i++) {
        const rr_confetti_t * c = &V->confetti[i];
        if(!c->alive || !vis_xy(c->x, c->y, 10.0f)) continue;
        gfx_save(G);
        gfx_translate(G, c->x, c->y);
        gfx_rotate(G, c->rotation);
        FILL(c->color);
        RECT(-c->size / 2, -c->size / 2, c->size, c->size);
        gfx_restore(G);
    }
}

static void draw_fireworks(void)
{
    for(int i = 0; i < RR_FIREWORK_MAX; i++) {
        const rr_particle_t * f = &V->fireworks[i];
        if(f->life <= 0 || !vis_xy(f->x, f->y, 10.0f)) continue;
        gfx_set_global_alpha(G, f->life);
        FILL(f->color);
        CIRCLE(f->x, f->y, f->size);
    }
    gfx_set_global_alpha(G, 1.0f);
}

static void draw_explosion(void)
{
    for(int i = 0; i < RR_EXPLOSION_MAX; i++) {
        const rr_particle_t * p = &V->explosion[i];
        if(p->life <= 0 || !vis_xy(p->x, p->y, 10.0f)) continue;
        gfx_set_global_alpha(G, p->life);
        FILL(p->color);
        RECT(p->x - p->size / 2, p->y - p->size / 2, p->size, p->size);
    }
    gfx_set_global_alpha(G, 1.0f);
}

/* ------------------------------------------------------------------ */
/* Light: the glow of fireballs and explosions (rendered look only)    */
/* ------------------------------------------------------------------ */

/* The prerendered art is lit by a fixed sun; fire should light its
 * surroundings too. Each light brightens what is already drawn around it:
 * the pixel gains a share of itself tinted by the light (lit surfaces) plus a
 * little of the light's own colour (the glow in the air), falling off
 * smoothly to nothing at the radius. Purely visual: reads the view, writes
 * pixels after the actors, and touches no game state or random sequence. */
typedef struct { float x, y, r, cr, cg, cb, i; } light_t;
#define LIGHT_MAX 24

static int collect_lights(light_t * L)
{
    int n = 0;
    float flick = 0.85f + 0.15f * sinf(now_ms() * 0.031f);
    for(int i = 0; i < rr_fireball_count && n < LIGHT_MAX; i++) {
        const rr_fireball_t * fb = &rr_fireballs[i];
        float f = flick + 0.1f * sinf(now_ms() * 0.047f + (float)i * 2.1f);
        L[n++] = (light_t) { fb->x, fb->y, 130.0f, 1.0f, 0.55f, 0.15f, fb->life * f };
    }
    /* Explosion particles gathered into a light per blast: the 30 sparks of
     * one blast start in one spot and fly apart together. */
    int first = n;
    float sum[LIGHT_MAX];
    for(int i = 0; i < RR_EXPLOSION_MAX; i++) {
        const rr_particle_t * p = &V->explosion[i];
        if(p->life <= 0) continue;
        int k = first;
        for(; k < n; k++) {
            float dx = p->x - L[k].x / sum[k], dy = p->y - L[k].y / sum[k];
            if(dx * dx + dy * dy < 200.0f * 200.0f) break;
        }
        if(k == n) {
            if(n == LIGHT_MAX) continue;
            L[n] = (light_t) { 0 };
            sum[n++] = 0;
        }
        L[k].x += p->x * p->life;
        L[k].y += p->y * p->life;
        sum[k] += p->life;
        L[k].i += 1.0f;
    }
    for(int k = first; k < n; k++) {
        float life = sum[k] / L[k].i; /* mean life, 1 at the bang */
        L[k].x /= sum[k];
        L[k].y /= sum[k];
        L[k].r = 150.0f + 150.0f * (1.0f - life);
        L[k].cr = 1.0f;
        L[k].cg = 0.45f + 0.35f * life;
        L[k].cb = 0.1f + 0.2f * life * life;
        L[k].i = fminf(1.0f, sum[k] / 12.0f) * (0.5f + 1.0f * life) * flick;
    }
    return n;
}

static void apply_light(const light_t * l, float r, float ex, float ey)
{
    int x0 = (int)floorf(ex - r), x1 = (int)ceilf(ex + r);
    int y0 = (int)floorf(ey - r), y1 = (int)ceilf(ey + r);
    if(x0 < 0) x0 = 0;
    if(x1 > G->w) x1 = G->w;
    if(y1 > G->y_hi) y1 = G->y_hi;
    if(y1 > G->h) y1 = G->h;
    if(x0 >= x1) return;
    float inv = 1.0f / (r * r);
    /* At the centre a surface gains `mul` of its own colour (what is lit
     * stands out) and the gap to white closes by `scr` (the glow in the air),
     * both tinted by the light; bright pixels thus cannot burn out. */
    float mul = 0.9f * l->i, scr = 0.3f * l->i;
    float mr = mul * l->cr, mg = mul * l->cg, mb = mul * l->cb;
    float sr = scr * l->cr, sg = scr * l->cg, sb = scr * l->cb;
    int step = G->row_step > 1 ? G->row_step : 1;
    for(int y = gfx_first_row(G, y0); y < y1; y += step) {
        float dy = (float)y + 0.5f - ey;
        float dy2 = dy * dy * inv;
        if(dy2 >= 1.0f) continue;
        uint32_t * row = G->px + (long)y * G->stride_px;
        for(int x = x0; x < x1; x++) {
            float dx = (float)x + 0.5f - ex;
            float t = 1.0f - (dx * dx * inv + dy2);
            if(t <= 0) continue;
            t *= t;
            uint32_t d = gfx_under(row[x], x, y);
            float cr = (float)((d >> 16) & 255), cg = (float)((d >> 8) & 255), cb = (float)(d & 255);
            int nr = (int)(cr + (cr * mr + (255.0f - cr) * sr) * t);
            int ng = (int)(cg + (cg * mg + (255.0f - cg) * sg) * t);
            int nb = (int)(cb + (cb * mb + (255.0f - cb) * sb) * t);
            nr = nr > 255 ? 255 : nr;
            ng = ng > 255 ? 255 : ng;
            nb = nb > 255 ? 255 : nb;
            row[x] = 0xFF000000u | (uint32_t)nr << 16 | (uint32_t)ng << 8 | (uint32_t)nb;
        }
    }
}

static void draw_lights(void)
{
    light_t L[LIGHT_MAX];
    int n = collect_lights(L);
    float sc = gfx_current_scale(G);
    for(int k = 0; k < n; k++) {
        if(L[k].i <= 0.01f) continue;
        float ex, ey;
        gfx_user_to_device(G, L[k].x, L[k].y, &ex, &ey);
        float r = L[k].r * sc;
        if(ey + r < (float)G->y_lo || ey - r >= (float)G->y_hi) continue;
        if(ex + r < 0 || ex - r >= (float)G->w) continue;
        apply_light(&L[k], r, ex, ey);
    }
}

/* ------------------------------------------------------------------ */
/* Screen-space overlays: charge meter + touch controls                */
/* ------------------------------------------------------------------ */

void rr_draw_touch_layout(int screen_w, int screen_h, rr_touch_btn_t * left, rr_touch_btn_t * right,
                          rr_touch_btn_t * up, rr_touch_rect_t * jump)
{
    /* laid out for 480x272, scaled with the screen */
    int r = rr_ui_px(19);
    int cy = screen_h - rr_ui_px(26);
    *left  = (rr_touch_btn_t) { rr_ui_px(26), cy, r };
    *right = (rr_touch_btn_t) { rr_ui_px(26) + 2 * r + rr_ui_px(8), cy, r };
    *jump  = (rr_touch_rect_t) { screen_w - rr_ui_px(8) - rr_ui_px(66), screen_h - rr_ui_px(8) - rr_ui_px(38),
                                 rr_ui_px(66), rr_ui_px(38) };
    *up    = (rr_touch_btn_t) { jump->x - rr_ui_px(8) - r, cy, r };
}

/* JUMP POWER meter, right above the JUMP button so the eye stays near the
 * thumb (the original draws it centred at the bottom of the canvas). Shown
 * whenever a launch is being charged, from the button or the playfield. */
static void draw_charge_meter(void)
{
    if(!V->charging) return;
    rr_touch_btn_t l, r, u;
    rr_touch_rect_t j;
    rr_draw_touch_layout(G->w, G->h, &l, &r, &u, &j);
    float power = V->charge_power;
    int mw = j.w, mh = rr_ui_px(9);
    int mx = j.x;
    int my = j.y - rr_ui_px(6) - mh;

    gfx_reset_transform(G);
    FILL(RGBA(0, 0, 0, 0.7f));
    RECT(mx, my, mw, mh);
    FILL(gfx_hsl(120.0f * power, 1.0f, 0.5f));
    RECT(mx + rr_ui_px(2), my + rr_ui_px(2), (mw - rr_ui_px(4)) * power, mh - rr_ui_px(4));
    STROKE(HEX(0xFFD700));
    LW(rr_ui_px(1));
    gfx_stroke_rect(G, mx, my, mw, mh);
    gfx_fill_text(G, "JUMP POWER", mx + mw / 2, my - rr_ui_px(2), 10.0f * rr_ui_scale(), 1, HEX(0xFFFFFF), HEX(0x000000));
}

static void draw_touch_button(const rr_touch_btn_t * b, bool pressed, int dir)
{
    FILL(pressed ? RGBA(255, 215, 0, 0.55f) : RGBA(0, 0, 0, 0.35f));
    CIRCLE(b->cx, b->cy, b->r);
    STROKE(pressed ? RGBA(255, 215, 0, 0.9f) : RGBA(255, 255, 255, 0.5f));
    LW(rr_ui_px(1));
    gfx_stroke_arc(G, b->cx, b->cy, b->r, 0, (float)M_PI * 2.0f);

    /* Arrow glyph: dir 0=left 1=right 2=up */
    float s = (float)b->r * 0.5f;
    float cx = (float)b->cx, cy = (float)b->cy;
    FILL(pressed ? HEX(0x000000) : RGBA(255, 255, 255, 0.85f));
    gfx_begin_path(G);
    switch(dir) {
        case 0: gfx_move_to(G, cx - s, cy); gfx_line_to(G, cx + s * 0.6f, cy - s); gfx_line_to(G, cx + s * 0.6f, cy + s); break;
        case 1: gfx_move_to(G, cx + s, cy); gfx_line_to(G, cx - s * 0.6f, cy - s); gfx_line_to(G, cx - s * 0.6f, cy + s); break;
        default: gfx_move_to(G, cx, cy - s); gfx_line_to(G, cx - s, cy + s * 0.6f); gfx_line_to(G, cx + s, cy + s * 0.6f); break;
    }
    gfx_fill(G);
}

/* One closed path (no overlapping pieces, so translucent fills stay even). */
static void rounded_rect_path(float x, float y, float w, float h, float rad)
{
    const float pi = (float)M_PI;
    gfx_begin_path(G);
    gfx_move_to(G, x + rad, y);
    gfx_line_to(G, x + w - rad, y);
    gfx_path_arc(G, x + w - rad, y + rad, rad, -pi / 2, 0, false);
    gfx_line_to(G, x + w, y + h - rad);
    gfx_path_arc(G, x + w - rad, y + h - rad, rad, 0, pi / 2, false);
    gfx_line_to(G, x + rad, y + h);
    gfx_path_arc(G, x + rad, y + h - rad, rad, pi / 2, pi, false);
    gfx_line_to(G, x, y + rad);
    gfx_path_arc(G, x + rad, y + rad, rad, pi, pi * 1.5f, false);
    gfx_line_to(G, x + rad, y);
}

static void draw_jump_shape(const rr_touch_rect_t * j, bool pressed)
{
    float x = (float)j->x, y = (float)j->y, w = (float)j->w, h = (float)j->h;
    /* pressed: solid gold; idle: translucent dark with a gold outline */
    FILL(pressed ? RGBA(255, 215, 0, 0.9f) : RGBA(0, 0, 0, 0.45f));
    rounded_rect_path(x, y, w, h, 7.0f * rr_ui_scale());
    gfx_fill(G);
    STROKE(RGBA(255, 215, 0, pressed ? 1.0f : 0.8f));
    LW(rr_ui_px(1));
    gfx_stroke(G);
}

static void draw_jump_text(const rr_touch_rect_t * j, bool pressed)
{
    float x = (float)j->x, y = (float)j->y, w = (float)j->w, h = (float)j->h;
    float k = rr_ui_scale();
    gfx_fill_text(G, "JUMP", x + w / 2, y + h / 2 + 5 * k, 12 * k, 1,
                  pressed ? HEX(0x000000) : HEX(0xFFD700), pressed ? 0 : HEX(0x000000));
}

/* The buttons never change except for their pressed state, yet rasterizing
 * them (AA circles, arcs, a rounded path) took about a tenth of a small
 * device's frame. So each of the eight looks (4 buttons x idle/pressed) is drawn once,
 * by the functions above, into a small straight-alpha image, and composited
 * every frame. The alpha is recovered by drawing twice, on black and on
 * white: over black a pixel is c*a, over white c*a + 255*(1-a). */
typedef struct {
    int x, y, w, h;
    uint32_t * px;
} btn_sprite_t;

static btn_sprite_t s_btn[4][2];
static int s_btn_w, s_btn_h; /* screen size the cache was built for; 0 = none */

static void build_btn_sprite(btn_sprite_t * sp, int x, int y, int w, int h, int which, bool pressed,
                             const rr_touch_btn_t * b, const rr_touch_rect_t * j)
{
    size_t n = (size_t)w * (size_t)h;
    uint32_t * on[2] = { malloc(n * 4), malloc(n * 4) };
    free(sp->px);
    sp->px = malloc(n * 4);
    if(on[0] == NULL || on[1] == NULL || sp->px == NULL) {
        free(on[0]);
        free(on[1]);
        free(sp->px);
        sp->px = NULL;
        return;
    }
    gfx_ctx_t * saved = G;
    for(int bg = 0; bg < 2; bg++) {
        for(size_t i = 0; i < n; i++) on[bg][i] = bg ? 0xFFFFFFFFu : 0xFF000000u;
        gfx_ctx_t tmp;
        gfx_begin(&tmp, on[bg], w, h, w);
        gfx_set_rows(&tmp, 0, 1, false);
        gfx_translate(&tmp, (float)-x, (float)-y);
        G = &tmp;
        if(which < 3) draw_touch_button(b, pressed, which);
        else draw_jump_shape(j, pressed);
    }
    G = saved;
    for(size_t i = 0; i < n; i++) {
        uint32_t k = on[0][i], wt = on[1][i];
        int sum = 0;
        for(int sh = 0; sh <= 16; sh += 8) sum += 255 - ((int)((wt >> sh) & 0xFF) - (int)((k >> sh) & 0xFF));
        int ai = (sum + 1) / 3;
        uint32_t a = ai < 0 ? 0 : (ai > 255 ? 255 : (uint32_t)ai);
        if(a == 0) {
            sp->px[i] = 0;
            continue;
        }
        uint32_t c = 0;
        for(int sh = 0; sh <= 16; sh += 8) {
            uint32_t v = (((k >> sh) & 0xFF) * 255 + a / 2) / a;
            c |= (v > 255 ? 255 : v) << sh;
        }
        sp->px[i] = (a << 24) | c;
    }
    free(on[0]);
    free(on[1]);
    sp->x = x;
    sp->y = y;
    sp->w = w;
    sp->h = h;
}


void rr_draw_prepare(int screen_w, int screen_h)
{
    static int disabled = -1;
    if(disabled < 0) s_no_sky_mask = getenv("ROLLING_ROB_NO_SKY_MASK") != NULL;
    if(disabled < 0) s_no_light = getenv("ROLLING_ROB_NO_LIGHT") != NULL;
    if(disabled < 0) disabled = getenv("ROLLING_ROB_NO_BTN_CACHE") != NULL;
    if(disabled || (screen_w == s_btn_w && screen_h == s_btn_h)) return;
    rr_touch_btn_t b[3];
    rr_touch_rect_t j;
    rr_draw_touch_layout(screen_w, screen_h, &b[0], &b[1], &b[2], &j);
    bool ok = true;
    for(int p = 0; p < 2; p++) {
        for(int i = 0; i < 3; i++) {
            int m = b[i].r + 3;
            build_btn_sprite(&s_btn[i][p], b[i].cx - m, b[i].cy - m, 2 * m, 2 * m, i, p, &b[i], NULL);
            ok = ok && s_btn[i][p].px;
        }
        build_btn_sprite(&s_btn[3][p], j.x - 3, j.y - 3, j.w + 6, j.h + 6, 3, p, NULL, &j);
        ok = ok && s_btn[3][p].px;
    }
    s_btn_w = ok ? screen_w : 0;
    s_btn_h = ok ? screen_h : 0;
}

static void draw_touch_controls(void)
{
    if(!V->touch_controls) return;
    rr_touch_btn_t l, r, u;
    rr_touch_rect_t j;
    rr_draw_touch_layout(G->w, G->h, &l, &r, &u, &j);
    gfx_reset_transform(G);
    if(s_btn_w == G->w && s_btn_h == G->h) {
        const bool down[4] = { V->btn_left, V->btn_right, V->btn_up, V->btn_jump };
        for(int i = 0; i < 4; i++) {
            const btn_sprite_t * sp = &s_btn[i][down[i]];
            gfx_draw_sprite(G, sp->x, sp->y, sp->w, sp->h, sp->px);
        }
    }
    else {
        draw_touch_button(&l, V->btn_left, 0);
        draw_touch_button(&r, V->btn_right, 1);
        draw_touch_button(&u, V->btn_up, 2);
        draw_jump_shape(&j, V->btn_jump);
    }
    draw_jump_text(&j, V->btn_jump);
}

/* ------------------------------------------------------------------ */
/* gameLoop() draw order                                               */
/* ------------------------------------------------------------------ */

/* The camera transform, shared by the frame and by the sky mask. */
static void frame_setup(gfx_ctx_t * g, const rr_view_t * v, float view_scale)
{
    G = g;
    V = v;
    S = view_scale;
    SCREEN_W_USER = (float)g->w / S;
    SCREEN_H_USER = (float)g->h / S;

    /* ctx.save(); ctx.translate(-cameraX, -cameraY);
     * The camera eases, so it usually sits between pixels, and every edge in
     * the world then lands on a slightly different pixel grid each frame.
     * Polygons and lines are not anti-aliased here (the browser's are), so
     * scrolling made lines visibly thicken and thin. Moving the world in
     * whole device pixels keeps every shape's rasterization identical from
     * frame to frame; the physics and the camera itself are untouched. */
    float cam_x = floorf(v->camera_x * S + 0.5f) / S;
    float cam_y = floorf(v->camera_y * S + 0.5f) / S;
    gfx_reset_transform(g);
    gfx_scale(g, S, S);
    gfx_translate(g, -cam_x, -cam_y);
}

/* Everything the sky draws below the ridge is painted over by the opaque
 * ground straight after, so the sky skips those pixels. The mask comes from
 * the ground polygon itself, rasterized the same way, so the frame is
 * identical -- only the hidden work is gone. Computed once per frame, before
 * the rasterizer threads start, and shared read-only. */
static int16_t s_sky_mask[2048];
static bool s_sky_mask_valid;

void rr_draw_sky_mask(uint32_t * px, int w, int h, int stride_px, const rr_view_t * v, float view_scale)
{
    s_sky_mask_valid = false;
    /* With the art the ground is a textured fill that masks the picture
     * itself, and the polygon is not drawn at all. */
    s_art = rr_art_prepare(v, view_scale, w, h);
    if(s_art) return;
    if(s_no_sky_mask || w > (int)(sizeof(s_sky_mask) / sizeof(s_sky_mask[0]))) return;
    uint64_t t0 = rr_prof_on ? rr_prof_now() : 0;
    gfx_ctx_t g;
    gfx_begin(&g, px, w, h, stride_px);
    frame_setup(&g, v, view_scale);
    int i0;
    int i1;
    ground_range(&i0, &i1);
    ground_path(i0, i1);
    gfx_path_cover_below(&g, s_sky_mask);
    G = NULL;
    if(rr_prof_on) rr_prof_add(RR_PROF_MASK, rr_prof_now() - t0);
    s_sky_mask_valid = true;
}

void rr_draw_frame(gfx_ctx_t * g, const rr_view_t * v, float view_scale)
{
    frame_setup(g, v, view_scale);
    if(g->emit_text && !g->text_q) gfx_text_clear();
    g->rng_state = 0x9E3779B9u ^ (uint32_t)(v->anim_ms / 100.0f);

    /* Only one context of a parallel frame adds to the profile. */
    #define P(slot, stmt) do { if(g->row_phase == 0) RR_PROF(slot, stmt); else { stmt; } } while(0)

    bool masked = s_sky_mask_valid && g->w <= (int)(sizeof(s_sky_mask) / sizeof(s_sky_mask[0]));
    if(masked) gfx_set_col_mask(g, s_sky_mask);
    const unsigned L = rr_draw_layers;
    if(s_art) {
        if(L & RR_LAYER_BG) P(RR_PROF_SKY, rr_art_background(g, v)); /* the picture is BG, the trees WORLD */
        if(L & RR_LAYER_WORLD) P(RR_PROF_MOUNTAIN, rr_art_ground(g, v));
    }
    else {
        if(L & RR_LAYER_BG) P(RR_PROF_SKY, draw_sky());
        if(masked) gfx_set_col_mask(g, NULL);
        if(L & RR_LAYER_WORLD) P(RR_PROF_MOUNTAIN, draw_mountain());
    }
    if(masked) gfx_set_col_mask(g, NULL);
    P(RR_PROF_OBJECTS, {
        if(L & RR_LAYER_WORLD) {
            draw_finish_line();
            draw_checkpoints();
        }
        if(L & RR_LAYER_ACTORS)
            for(int i = 0; i < rr_obstacle_count; i++) draw_monster(&rr_obstacles[i]);
        if(L & RR_LAYER_WORLD) draw_mines();
        if(L & RR_LAYER_ACTORS) {
            for(int i = 0; i < rr_saucer_count; i++) draw_saucer(&rr_saucers[i]);
            draw_beam();
            for(int i = 0; i < rr_julian_count; i++) draw_julian(&rr_julians[i]);
            draw_fireballs();
        }
    });
    if(L & RR_LAYER_ACTORS) P(RR_PROF_ROB_FX, {
        if(v->rob_visible) draw_rob();
        draw_explosion();
        if(s_art && rr_art_rendered() && !s_no_light) draw_lights();
        if(v->won) {
            draw_confetti();
            draw_fireworks();
        }
    });
    /* ctx.restore(); screen-space overlays */
    if(L & RR_LAYER_OVERLAY) P(RR_PROF_OVERLAY, {
        draw_charge_meter();
        draw_touch_controls();
    });
    #undef P
}
