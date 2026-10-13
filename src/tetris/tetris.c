#include "tetris.h"

#include <math.h>
#include <string.h>

/* Spawn shapes in their boxes (x, y, y down); the other rotations are the
 * box turned, which is exactly SRS. */
static const int8_t k_shape[TT_PIECES][4][2] = {
    [TT_I] = { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 } },
    [TT_O] = { { 1, 0 }, { 2, 0 }, { 1, 1 }, { 2, 1 } },
    [TT_T] = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } },
    [TT_S] = { { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 } },
    [TT_Z] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 } },
    [TT_J] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } },
    [TT_L] = { { 2, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } },
};
static const int k_box[TT_PIECES] = { 4, 4, 3, 3, 3, 3, 3 };

/* SRS wall kicks, (x, y) with y up as the tables are usually written; index
 * by the rotation turned from, for a clockwise turn. A counter-clockwise turn
 * from r uses the clockwise kicks into r, negated. */
static const int8_t k_kick_jlstz[4][5][2] = {
    { { 0, 0 }, { -1, 0 }, { -1, 1 }, { 0, -2 }, { -1, -2 } }, /* 0 -> R */
    { { 0, 0 }, { 1, 0 }, { 1, -1 }, { 0, 2 }, { 1, 2 } },     /* R -> 2 */
    { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, -2 }, { 1, -2 } },    /* 2 -> L */
    { { 0, 0 }, { -1, 0 }, { -1, -1 }, { 0, 2 }, { -1, 2 } },  /* L -> 0 */
};
static const int8_t k_kick_i[4][5][2] = {
    { { 0, 0 }, { -2, 0 }, { 1, 0 }, { -2, -1 }, { 1, 2 } }, /* 0 -> R */
    { { 0, 0 }, { -1, 0 }, { 2, 0 }, { -1, 2 }, { 2, -1 } }, /* R -> 2 */
    { { 0, 0 }, { 2, 0 }, { -1, 0 }, { 2, 1 }, { -1, -2 } }, /* 2 -> L */
    { { 0, 0 }, { 1, 0 }, { -2, 0 }, { 1, -2 }, { -2, 1 } }, /* L -> 0 */
};

#define LOCK_MS 500
#define LOCK_RESETS 15
#define DAS_MS 167
#define ARR_MS 33
#define SOFT_MS 33

void tt_cells(int piece, int rot, int out[4][2])
{
    int n = k_box[piece];
    for(int i = 0; i < 4; i++) {
        int x = k_shape[piece][i][0], y = k_shape[piece][i][1];
        if(piece != TT_O)
            for(int r = 0; r < (rot & 3); r++) {
                int t = x;
                x = n - 1 - y;
                y = t;
            }
        out[i][0] = x;
        out[i][1] = y;
    }
}

uint32_t tt_color(int piece)
{
    static const uint32_t c[TT_PIECES] = { 0x31C7EF, 0xF7D308, 0xAD4D9C, 0x42B642, 0xEF2029, 0x5A65AD, 0xEF7921 };
    return piece >= 0 && piece < TT_PIECES ? c[piece] : 0x808080;
}

static uint32_t rnd(tt_game_t * g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

static int from_bag(tt_game_t * g)
{
    if(g->bag_left == 0) {
        for(int i = 0; i < TT_PIECES; i++) g->bag[i] = i;
        for(int i = TT_PIECES - 1; i > 0; i--) {
            int j = (int)(rnd(g) % (uint32_t)(i + 1));
            int t = g->bag[i];
            g->bag[i] = g->bag[j];
            g->bag[j] = t;
        }
        g->bag_left = TT_PIECES;
    }
    return g->bag[--g->bag_left];
}

static bool fits(const tt_game_t * g, int piece, int rot, int x, int y)
{
    int c[4][2];
    tt_cells(piece, rot, c);
    for(int i = 0; i < 4; i++) {
        int cx = x + c[i][0], cy = y + c[i][1];
        if(cx < 0 || cx >= TT_W || cy >= TT_ROWS) return false;
        if(cy >= 0 && g->cell[cy][cx]) return false;
    }
    return true;
}

static bool on_ground(const tt_game_t * g) { return !fits(g, g->piece, g->rot, g->x, g->y + 1); }

static int gravity_ms(int level)
{
    if(level > 20) level = 20;
    double s = pow(0.8 - (level - 1) * 0.007, level - 1);
    int ms = (int)(s * 1000.0 + 0.5);
    return ms < 1 ? 1 : ms;
}

static void spawn(tt_game_t * g, int piece)
{
    g->piece = piece;
    g->rot = 0;
    g->x = 3; /* the middle: columns 3-6 for I, 4-5 for O, 3-5 for the rest */
    g->y = 0; /* the hidden rows */
    g->fall_ms = 0;
    g->lock_ms = 0;
    g->lock_resets = 0;
    g->active = true;
    if(!fits(g, g->piece, 0, g->x, g->y)) {
        g->over = true;
        g->active = false;
    }
    else if(fits(g, g->piece, 0, g->x, g->y + 1)) {
        g->y++; /* into view straight away */
    }
    g->changed++;
}

static void spawn_next(tt_game_t * g)
{
    int p = g->next[0];
    memmove(g->next, g->next + 1, sizeof(g->next[0]) * (TT_NEXT - 1));
    g->next[TT_NEXT - 1] = from_bag(g);
    g->hold_used = false;
    spawn(g, p);
}

void tt_new(tt_game_t * g, uint32_t seed, int best)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0x9E3779B9u;
    g->hold = -1;
    g->level = 1;
    g->combo = -1;
    g->best = best;
    for(int i = 0; i < TT_NEXT; i++) g->next[i] = from_bag(g);
    spawn_next(g);
}

/* A move or turn that worked while resting on the ground buys time. */
static void moved(tt_game_t * g)
{
    g->changed++;
    if(on_ground(g) && g->lock_resets < LOCK_RESETS) {
        g->lock_ms = 0;
        g->lock_resets++;
    }
}

static bool shift(tt_game_t * g, int dx)
{
    if(!fits(g, g->piece, g->rot, g->x + dx, g->y)) return false;
    g->x += dx;
    moved(g);
    return true;
}

static void turn(tt_game_t * g, int dir)
{
    if(g->piece == TT_O) return;
    int to = (g->rot + dir) & 3;
    const int8_t(*kick)[5][2] = g->piece == TT_I ? k_kick_i : k_kick_jlstz;
    for(int k = 0; k < 5; k++) {
        int kx, ky;
        if(dir > 0) {
            kx = kick[g->rot][k][0];
            ky = kick[g->rot][k][1];
        }
        else {
            kx = -kick[to][k][0];
            ky = -kick[to][k][1];
        }
        if(fits(g, g->piece, to, g->x + kx, g->y - ky)) {
            g->x += kx;
            g->y -= ky;
            g->rot = to;
            moved(g);
            return;
        }
    }
}

static void lock(tt_game_t * g)
{
    int c[4][2];
    tt_cells(g->piece, g->rot, c);
    bool above = true;
    for(int i = 0; i < 4; i++) {
        int cy = g->y + c[i][1];
        if(cy >= 0) g->cell[cy][g->x + c[i][0]] = (uint8_t)(g->piece + 1);
        if(cy >= TT_HIDDEN) above = false;
    }
    g->active = false;
    g->changed++;
    if(above) { /* locked wholly above the well: out */
        g->over = true;
        return;
    }
    int n = 0;
    for(int y = 0; y < TT_ROWS; y++) {
        bool full = true;
        for(int x = 0; x < TT_W && full; x++) full = g->cell[y][x] != 0;
        g->clearing[y] = full;
        n += full;
    }
    if(n == 0) {
        g->combo = -1;
        spawn_next(g);
        return;
    }
    static const int pts[5] = { 0, 100, 300, 500, 800 };
    int s = pts[n] * g->level;
    if(n == 4 && g->b2b) s = s * 3 / 2;
    g->b2b = n == 4;
    g->combo++;
    s += 50 * g->combo * g->level;
    g->score += s;
    g->lines += n;
    g->level = 1 + g->lines / 10;
    g->last_lines = n;
    g->last_clear_seq++;
    g->clear_ms = TT_CLEAR_MS;
}

static void remove_rows(tt_game_t * g)
{
    int to = TT_ROWS - 1;
    for(int y = TT_ROWS - 1; y >= 0; y--) {
        if(g->clearing[y]) continue;
        if(to != y) memcpy(g->cell[to], g->cell[y], TT_W);
        to--;
    }
    for(; to >= 0; to--) memset(g->cell[to], 0, TT_W);
    memset(g->clearing, 0, sizeof(g->clearing));
    g->changed++;
}

void tt_input(tt_game_t * g, tt_input_t in, bool down)
{
    if(in <= TT_SOFT) {
        bool was = g->held[in];
        g->held[in] = down;
        if(down && !was && in != TT_SOFT && g->active && !g->over) {
            int dir = in == TT_LEFT ? -1 : 1;
            g->das_dir = dir;
            g->das_ms = 0;
            shift(g, dir);
        }
        if(!down && (int)in < 2 && g->das_dir == (in == TT_LEFT ? -1 : 1)) {
            /* let go: the other one may still be held */
            int other = in == TT_LEFT ? TT_RIGHT : TT_LEFT;
            g->das_dir = g->held[other] ? (other == TT_LEFT ? -1 : 1) : 0;
            g->das_ms = 0;
        }
        return;
    }
    if(!down || !g->active || g->over) return;
    switch(in) {
        case TT_CW: turn(g, 1); break;
        case TT_CCW: turn(g, -1); break;
        case TT_HARD: {
            int rows = 0;
            while(fits(g, g->piece, g->rot, g->x, g->y + 1)) {
                g->y++;
                rows++;
            }
            g->score += 2 * rows;
            lock(g);
            break;
        }
        case TT_HOLD:
            if(g->hold_used) break;
            if(g->hold < 0) {
                g->hold = g->piece;
                spawn_next(g);
            }
            else {
                int h = g->hold;
                g->hold = g->piece;
                spawn(g, h);
            }
            g->hold_used = true;
            break;
        default: break;
    }
}

int tt_ghost_y(const tt_game_t * g)
{
    int y = g->y;
    while(fits(g, g->piece, g->rot, g->x, y + 1)) y++;
    return y;
}

void tt_update(tt_game_t * g, int dt_ms)
{
    if(g->over) {
        if(g->score > g->best) g->best = g->score;
        return;
    }
    if(g->clear_ms > 0) {
        g->clear_ms -= dt_ms;
        if(g->clear_ms <= 0) {
            g->clear_ms = 0;
            remove_rows(g);
            spawn_next(g);
        }
        return;
    }
    if(!g->active) return;

    /* auto-repeat */
    if(g->das_dir) {
        /* moves owed: none before the delay, one at it, then one per ARR_MS */
        int before = g->das_ms < DAS_MS ? 0 : 1 + (g->das_ms - DAS_MS) / ARR_MS;
        g->das_ms += dt_ms;
        int now = g->das_ms < DAS_MS ? 0 : 1 + (g->das_ms - DAS_MS) / ARR_MS;
        for(int i = before; i < now; i++)
            if(!shift(g, g->das_dir)) break;
    }

    /* gravity; a soft drop is at least one row per 33 ms and scores */
    int per_row = gravity_ms(g->level);
    bool soft = g->held[2] && per_row > SOFT_MS;
    if(soft) per_row = SOFT_MS;
    if(!on_ground(g)) {
        g->fall_ms += dt_ms;
        while(g->fall_ms >= per_row && !on_ground(g)) {
            g->fall_ms -= per_row;
            g->y++;
            if(soft) g->score++;
            g->changed++;
            g->lock_ms = 0;
        }
        if(on_ground(g)) g->fall_ms = 0;
    }
    if(on_ground(g)) {
        g->lock_ms += dt_ms;
        if(g->lock_ms >= LOCK_MS) lock(g);
    }
    if(g->score > g->best) g->best = g->score;
}
