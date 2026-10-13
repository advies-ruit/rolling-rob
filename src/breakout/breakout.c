#include "breakout.h"
#include <math.h>
#include <string.h>

#define MAX_SPEED 340.0f

static float base_speed(int level)
{
    float s = 170.0f + 15.0f * (float)(level - 1);
    return s > 280.0f ? 280.0f : s;
}

/* The level's wall: four patterns in turn; from level 2 the top row is silver. */
static void layout(bo_game_t * g)
{
    memset(g->brick, 0, sizeof(g->brick));
    memset(g->silver, 0, sizeof(g->silver));
    int pat = (g->level - 1) % 4;
    for(int r = 0; r < BO_ROWS; r++)
        for(int c = 0; c < BO_COLS; c++) {
            bool on = false;
            switch(pat) {
                case 0: on = r >= 1; break;                                       /* a full wall */
                case 1: on = fabsf((float)c - 5.5f) <= (float)r + 0.5f; break;     /* a pyramid */
                case 2: on = r % 2 == 0 || c % 2 == 0; break;                     /* stripes */
                default: on = (r + c) % 2 == 0 || r == 0 || r == BO_ROWS - 1; break; /* a chequer in a frame */
            }
            if(on) g->brick[r][c] = 1;
        }
    if(g->level >= 2)
        for(int r = 0; r < BO_ROWS; r++) {
            bool any = false;
            for(int c = 0; c < BO_COLS; c++)
                if(g->brick[r][c]) {
                    g->brick[r][c] = 2;
                    g->silver[r][c] = 1;
                    any = true;
                }
            if(any) break;
        }
    g->left = 0;
    for(int r = 0; r < BO_ROWS; r++)
        for(int c = 0; c < BO_COLS; c++) g->left += g->brick[r][c] > 0;
    g->bricks_changed++;
}

static void serve(bo_game_t * g)
{
    g->stuck = true;
    g->speed = base_speed(g->level);
    g->vx = g->vy = 0;
}

void bo_new(bo_game_t * g, uint32_t seed, int best)
{
    uint32_t changed = g->bricks_changed;
    memset(g, 0, sizeof(*g));
    g->bricks_changed = changed;
    g->rng = seed ? seed : 1;
    g->best = best;
    g->lives = 3;
    g->level = 1;
    g->px = BO_FW / 2;
    g->pw = 48;
    layout(g);
    serve(g);
}

/* The first brick under the ball, or -1. */
static int brick_at(const bo_game_t * g, float x, float y)
{
    int c0 = (int)floorf((x - BO_BALL_R) / BO_BRICK_W), c1 = (int)floorf((x + BO_BALL_R) / BO_BRICK_W);
    int r0 = (int)floorf((y - BO_BALL_R - BO_BRICK_TOP) / BO_BRICK_H);
    int r1 = (int)floorf((y + BO_BALL_R - BO_BRICK_TOP) / BO_BRICK_H);
    for(int r = r0; r <= r1; r++)
        for(int c = c0; c <= c1; c++)
            if(r >= 0 && r < BO_ROWS && c >= 0 && c < BO_COLS && g->brick[r][c]) return r * BO_COLS + c;
    return -1;
}

static void hit(bo_game_t * g, int i)
{
    int r = i / BO_COLS, c = i % BO_COLS;
    if(--g->brick[r][c] == 0) {
        g->left--;
        g->score += 10 * (BO_ROWS - r);
        if(g->score > g->best) g->best = g->score;
    }
    g->bricks_changed++;
    g->speed = g->speed + 2.0f > MAX_SPEED ? MAX_SPEED : g->speed + 2.0f;
    float n = sqrtf(g->vx * g->vx + g->vy * g->vy);
    if(n > 0) {
        g->vx *= g->speed / n;
        g->vy *= g->speed / n;
    }
}

static void step(bo_game_t * g, float h)
{
    int i;
    g->bx += g->vx * h;
    if((i = brick_at(g, g->bx, g->by)) >= 0) {
        g->bx -= g->vx * h;
        g->vx = -g->vx;
        hit(g, i);
    }
    if(g->bx < BO_BALL_R) {
        g->bx = BO_BALL_R;
        g->vx = fabsf(g->vx);
    }
    if(g->bx > BO_FW - BO_BALL_R) {
        g->bx = BO_FW - BO_BALL_R;
        g->vx = -fabsf(g->vx);
    }
    g->by += g->vy * h;
    if((i = brick_at(g, g->bx, g->by)) >= 0) {
        g->by -= g->vy * h;
        g->vy = -g->vy;
        hit(g, i);
    }
    if(g->by < BO_BALL_R) {
        g->by = BO_BALL_R;
        g->vy = fabsf(g->vy);
    }
    /* the paddle: the angle from where it hits */
    float half = g->pw / 2;
    if(g->vy > 0 && g->by + BO_BALL_R >= BO_PADDLE_Y && g->by - BO_BALL_R < BO_PADDLE_Y + BO_PADDLE_H &&
       g->bx >= g->px - half - BO_BALL_R && g->bx <= g->px + half + BO_BALL_R) {
        float rel = (g->bx - g->px) / half;
        if(rel < -1) rel = -1;
        if(rel > 1) rel = 1;
        float a = rel * 1.0472f; /* 60 degrees */
        g->vx = g->speed * sinf(a);
        g->vy = -g->speed * cosf(a);
        g->by = BO_PADDLE_Y - BO_BALL_R;
    }
}

void bo_update(bo_game_t * g, int dt_ms, int move, float target_x, bool launch)
{
    if(g->over) return;
    if(dt_ms > 50) dt_ms = 50;
    float dt = (float)dt_ms / 1000.0f, half = g->pw / 2;
    if(target_x >= 0) g->px = target_x;
    g->px += (float)move * 380.0f * dt;
    if(g->px < half) g->px = half;
    if(g->px > BO_FW - half) g->px = BO_FW - half;

    if(g->stuck) {
        g->bx = g->px;
        g->by = BO_PADDLE_Y - BO_BALL_R;
        if(!launch) return;
        g->rng = g->rng * 1664525u + 1013904223u;
        float a = ((float)((g->rng >> 8) % 1000) / 1000.0f - 0.5f) * 0.6f; /* up to 17 degrees off */
        g->vx = g->speed * sinf(a);
        g->vy = -g->speed * cosf(a);
        g->stuck = false;
    }
    /* steps of at most 2 units: never through a brick (10 high) */
    int n = (int)ceilf(g->speed * dt / 2.0f);
    if(n < 1) n = 1;
    for(int k = 0; k < n && !g->stuck; k++) {
        step(g, dt / (float)n);
        if(g->left == 0) {
            g->level++;
            layout(g);
            serve(g);
        }
        else if(g->by - BO_BALL_R > BO_FH) {
            if(--g->lives <= 0) g->over = true;
            else serve(g);
            return;
        }
    }
}
