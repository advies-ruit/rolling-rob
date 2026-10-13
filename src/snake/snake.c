#include "snake.h"
#include <string.h>

static uint32_t rnd(sn_game_t * g)
{
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng >> 8;
}

static int seg(const sn_game_t * g, int k) { return (g->head - k + SN_MAX) % SN_MAX; }

/* A random free cell (not the body, not an apple); false when there is none. */
static bool free_cell(sn_game_t * g, int * ox, int * oy)
{
    int n = 0;
    for(int y = 0; y < SN_H; y++)
        for(int x = 0; x < SN_W; x++)
            if(sn_cell(g, x, y) == SN_EMPTY) n++;
    if(n == 0) return false;
    int k = (int)(rnd(g) % (uint32_t)n);
    for(int y = 0; y < SN_H; y++)
        for(int x = 0; x < SN_W; x++)
            if(sn_cell(g, x, y) == SN_EMPTY && k-- == 0) {
                *ox = x;
                *oy = y;
                return true;
            }
    return false;
}

void sn_new(sn_game_t * g, uint32_t seed, int best)
{
    uint32_t steps = g->steps;
    memset(g, 0, sizeof(*g));
    g->steps = steps + 1;
    g->rng = seed ? seed : 1;
    g->best = best;
    g->len = 4;
    g->head = 3;
    for(int k = 0; k < 4; k++) {
        g->x[k] = (uint8_t)(SN_W / 2 - 3 + k);
        g->y[k] = SN_H / 2;
        g->occ[g->y[k]][g->x[k]] = 1;
    }
    g->dir = SN_RIGHT;
    g->step_ms = 150;
    g->bx = g->by = -1;
    if(!free_cell(g, &g->fx, &g->fy)) g->fx = g->fy = -1;
}

void sn_turn(sn_game_t * g, sn_dir_t d)
{
    if(g->over) return;
    sn_dir_t last = g->queued ? g->queue[g->queued - 1] : g->dir;
    if(d == last || (d + 2) % 4 == last) return;
    if(g->queued < 2) g->queue[g->queued++] = d;
    g->started = true;
}

static void step(sn_game_t * g)
{
    if(g->queued) {
        g->dir = g->queue[0];
        g->queue[0] = g->queue[1];
        g->queued--;
    }
    static const int dx[4] = { 0, 1, 0, -1 }, dy[4] = { -1, 0, 1, 0 };
    int h = g->head;
    int nx = g->x[h] + dx[g->dir], ny = g->y[h] + dy[g->dir];
    g->steps++;
    if(nx < 0 || nx >= SN_W || ny < 0 || ny >= SN_H) {
        g->over = true;
        return;
    }
    /* the tail moves on first, unless growing */
    int t = seg(g, g->len - 1);
    if(g->grow == 0) g->occ[g->y[t]][g->x[t]] = 0;
    if(g->occ[ny][nx]) {
        if(g->grow == 0) g->occ[g->y[t]][g->x[t]] = 1;
        g->over = true;
        return;
    }
    g->head = (h + 1) % SN_MAX;
    g->x[g->head] = (uint8_t)nx;
    g->y[g->head] = (uint8_t)ny;
    g->occ[ny][nx] = 1;
    if(g->grow > 0) {
        g->grow--;
        g->len++;
    }

    if(g->bonus_steps > 0 && --g->bonus_steps == 0) g->bx = g->by = -1;
    if(nx == g->bx && ny == g->by) {
        g->score += 50;
        g->grow += 3;
        g->bonus_steps = 0;
        g->bx = g->by = -1;
    }
    if(nx == g->fx && ny == g->fy) {
        g->score += 10;
        g->grow += 1;
        g->eaten++;
        g->step_ms = 150 - 3 * g->eaten < 65 ? 65 : 150 - 3 * g->eaten;
        g->fx = g->fy = -1;
        if(!free_cell(g, &g->fx, &g->fy)) {
            g->fx = g->fy = -1;
            g->won = g->over = true;
        }
        else if(g->eaten % 5 == 0 && free_cell(g, &g->bx, &g->by)) g->bonus_steps = SN_BONUS_STEPS;
    }
    if(g->score > g->best) g->best = g->score;
}

bool sn_update(sn_game_t * g, int dt_ms)
{
    if(!g->started || g->over) return false;
    g->acc_ms += dt_ms;
    if(g->acc_ms > 4 * g->step_ms) g->acc_ms = 4 * g->step_ms; /* after a stall: no burst */
    bool stepped = false;
    while(g->acc_ms >= g->step_ms && !g->over) {
        g->acc_ms -= g->step_ms;
        step(g);
        stepped = true;
    }
    return stepped;
}

sn_cell_t sn_cell(const sn_game_t * g, int x, int y)
{
    if(x == g->x[g->head] && y == g->y[g->head]) return SN_HEAD;
    if(g->occ[y][x]) return SN_BODY;
    if(x == g->fx && y == g->fy) return SN_FOOD;
    if(g->bonus_steps > 0 && x == g->bx && y == g->by) return SN_BONUS;
    return SN_EMPTY;
}
