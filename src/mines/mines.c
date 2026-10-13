#include "mines.h"
#include <string.h>
#ifdef MI_CHECK
#include <assert.h>
#endif

static uint32_t rnd(mi_game_t * g)
{
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng >> 8;
}

static bool inside(const mi_game_t * g, int x, int y) { return x >= 0 && x < g->w && y >= 0 && y < g->h; }

void mi_new(mi_game_t * g, int w, int h, int mines, uint32_t seed, bool no_guess)
{
    uint32_t changed = g->changed;
    memset(g, 0, sizeof(*g));
    g->changed = changed + 1;
    g->w = w > MI_MAXW ? MI_MAXW : w;
    g->h = h > MI_MAXH ? MI_MAXH : h;
    int most = g->w * g->h - 9;
    g->mines = mines > most ? most : mines;
    g->rng = seed ? seed : 1;
    g->boom_x = g->boom_y = -1;
    g->no_guess = no_guess;
}

/* ---------------- the solver ---------------- */

/* What the solver knows of a cell */
enum { S_UNKNOWN, S_OPEN, S_MINE };

typedef struct {
    const mi_game_t * g;
    uint8_t k[MI_MAXH][MI_MAXW];
    int open, mines_found;
} solver_t;

static void s_open(solver_t * s, int x0, int y0)
{
    static int16_t stack[MI_MAXW * MI_MAXH][2];
    int sp = 0;
    if(s->k[y0][x0] != S_UNKNOWN) return;
    s->k[y0][x0] = S_OPEN;
    s->open++;
    stack[sp][0] = (int16_t)x0;
    stack[sp][1] = (int16_t)y0;
    sp++;
    while(sp > 0) {
        sp--;
        int x = stack[sp][0], y = stack[sp][1];
        if(s->g->adj[y][x]) continue;
        for(int dy = -1; dy <= 1; dy++)
            for(int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, ny = y + dy;
                if(inside(s->g, nx, ny) && s->k[ny][nx] == S_UNKNOWN) {
                    s->k[ny][nx] = S_OPEN;
                    s->open++;
                    stack[sp][0] = (int16_t)nx;
                    stack[sp][1] = (int16_t)ny;
                    sp++;
                }
            }
    }
}

/* An opened number's closed neighbours (as y * MI_MAXW + x) and how many
 * mines among them are not found yet. */
typedef struct {
    int16_t cell[8];
    int n, need;
} con_t;

static bool constraint(const solver_t * s, int x, int y, con_t * c)
{
    if(s->k[y][x] != S_OPEN || s->g->adj[y][x] == 0) return false;
    c->n = 0;
    c->need = s->g->adj[y][x];
    for(int dy = -1; dy <= 1; dy++)
        for(int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if(!inside(s->g, nx, ny)) continue;
            if(s->k[ny][nx] == S_MINE) c->need--;
            else if(s->k[ny][nx] == S_UNKNOWN) c->cell[c->n++] = (int16_t)(ny * MI_MAXW + nx);
        }
    return c->n > 0;
}

static bool has(const con_t * c, int16_t v)
{
    for(int i = 0; i < c->n; i++)
        if(c->cell[i] == v) return true;
    return false;
}

static bool settle(solver_t * s, int16_t v, bool mine)
{
    int x = v % MI_MAXW, y = v / MI_MAXW;
    if(s->k[y][x] != S_UNKNOWN) return false;
#ifdef MI_CHECK
    assert(s->g->mine[y][x] == (mine ? 1 : 0)); /* the tests: every conclusion is true */
#endif
    if(mine) {
        s->k[y][x] = S_MINE;
        s->mines_found++;
    }
    else s_open(s, x, y);
    return true;
}

/* One pass of the rules; whether it learnt anything. */
static bool reason(solver_t * s)
{
    const mi_game_t * g = s->g;
    bool progress = false;
    con_t a, b;
    for(int y = 0; y < g->h; y++)
        for(int x = 0; x < g->w; x++) {
            if(!constraint(s, x, y, &a)) continue;
            if(a.need == 0 || a.need == a.n) {
                for(int i = 0; i < a.n; i++) progress |= settle(s, a.cell[i], a.need > 0);
                continue;
            }
            /* against the numbers up to two cells away: a's closed cells inside b's */
            for(int y2 = y - 2; y2 <= y + 2; y2++)
                for(int x2 = x - 2; x2 <= x + 2; x2++) {
                    if((x2 == x && y2 == y) || !inside(g, x2, y2) || !constraint(s, x2, y2, &b)) continue;
                    bool subset = true;
                    for(int i = 0; i < a.n && subset; i++) subset = has(&b, a.cell[i]);
                    if(!subset || b.n == a.n) continue;
                    int rest = b.n - a.n, need = b.need - a.need;
                    if(need != 0 && need != rest) continue;
                    for(int i = 0; i < b.n; i++)
                        if(!has(&a, b.cell[i])) progress |= settle(s, b.cell[i], need > 0);
                }
        }
    if(progress) return true;
    /* the mine count: none left, or as many as closed cells */
    int unknown = 0;
    for(int y = 0; y < g->h; y++)
        for(int x = 0; x < g->w; x++) unknown += s->k[y][x] == S_UNKNOWN;
    int left = g->mines - s->mines_found;
    if(unknown == 0 || (left != 0 && left != unknown)) return false;
    for(int y = 0; y < g->h; y++)
        for(int x = 0; x < g->w; x++)
            if(s->k[y][x] == S_UNKNOWN) progress |= settle(s, (int16_t)(y * MI_MAXW + x), left > 0);
    return progress;
}

bool mi_solvable(const mi_game_t * g, int sx, int sy)
{
    static solver_t s;
    memset(&s, 0, sizeof(s));
    s.g = g;
    s_open(&s, sx, sy);
    while(s.open < g->w * g->h - g->mines && reason(&s)) {
    }
    return s.open == g->w * g->h - g->mines;
}

/* ---------------- laying the mines ---------------- */

/* Away from (sx, sy) and its neighbours. */
static void lay_once(mi_game_t * g, int sx, int sy)
{
    memset(g->mine, 0, sizeof(g->mine));
    int n = 0;
    while(n < g->mines) {
        int x = (int)(rnd(g) % (uint32_t)g->w), y = (int)(rnd(g) % (uint32_t)g->h);
        if(g->mine[y][x] || (x >= sx - 1 && x <= sx + 1 && y >= sy - 1 && y <= sy + 1)) continue;
        g->mine[y][x] = 1;
        n++;
    }
    for(int y = 0; y < g->h; y++)
        for(int x = 0; x < g->w; x++) {
            int a = 0;
            for(int dy = -1; dy <= 1; dy++)
                for(int dx = -1; dx <= 1; dx++)
                    if(inside(g, x + dx, y + dy)) a += g->mine[y + dy][x + dx];
            g->adj[y][x] = (uint8_t)a;
        }
}

static void lay(mi_game_t * g, int sx, int sy)
{
    g->tries = 0;
    do {
        lay_once(g, sx, sy);
        g->tries++;
        g->solvable = mi_solvable(g, sx, sy);
    } while(g->no_guess && !g->solvable && g->tries < MI_TRIES);
    g->laid = true;
}

static void lose(mi_game_t * g, int x, int y)
{
    g->over = true;
    g->boom_x = x;
    g->boom_y = y;
}

/* Opens one cell, and the zero area around it (a stack, not recursion). */
static void flood(mi_game_t * g, int x0, int y0)
{
    static int16_t stack[MI_MAXW * MI_MAXH][2];
    static uint8_t queued[MI_MAXH][MI_MAXW]; /* each cell on the stack once: it cannot overflow */
    memset(queued, 0, sizeof(queued));
    int sp = 0;
    queued[y0][x0] = 1;
    stack[sp][0] = (int16_t)x0;
    stack[sp][1] = (int16_t)y0;
    sp++;
    while(sp > 0) {
        sp--;
        int x = stack[sp][0], y = stack[sp][1];
        if(g->open[y][x] || g->flag[y][x]) continue;
        g->open[y][x] = 1;
        g->opened++;
        if(g->adj[y][x]) continue;
        for(int dy = -1; dy <= 1; dy++)
            for(int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, ny = y + dy;
                if(inside(g, nx, ny) && !g->open[ny][nx] && !g->flag[ny][nx] && !queued[ny][nx]) {
                    queued[ny][nx] = 1;
                    stack[sp][0] = (int16_t)nx;
                    stack[sp][1] = (int16_t)ny;
                    sp++;
                }
            }
    }
}

static void check_win(mi_game_t * g)
{
    if(g->over || g->opened != g->w * g->h - g->mines) return;
    g->over = g->won = true;
    for(int y = 0; y < g->h; y++)
        for(int x = 0; x < g->w; x++)
            if(g->mine[y][x] && !g->flag[y][x]) {
                g->flag[y][x] = 1;
                g->flags++;
            }
}

void mi_open(mi_game_t * g, int x, int y)
{
    if(g->over || !inside(g, x, y) || g->flag[y][x]) return;
    if(!g->laid) lay(g, x, y);
    g->changed++;
    if(g->open[y][x]) {
        /* a number with its flags placed: the rest around it */
        int f = 0;
        for(int dy = -1; dy <= 1; dy++)
            for(int dx = -1; dx <= 1; dx++)
                if(inside(g, x + dx, y + dy)) f += g->flag[y + dy][x + dx];
        if(g->adj[y][x] == 0 || f != g->adj[y][x]) return;
        for(int dy = -1; dy <= 1; dy++)
            for(int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, ny = y + dy;
                if(!inside(g, nx, ny) || g->open[ny][nx] || g->flag[ny][nx]) continue;
                if(g->mine[ny][nx]) lose(g, nx, ny);
                else flood(g, nx, ny);
            }
        check_win(g);
        return;
    }
    if(g->mine[y][x]) {
        lose(g, x, y);
        return;
    }
    flood(g, x, y);
    check_win(g);
}

void mi_flag(mi_game_t * g, int x, int y)
{
    if(g->over || !inside(g, x, y) || g->open[y][x]) return;
    g->flag[y][x] ^= 1;
    g->flags += g->flag[y][x] ? 1 : -1;
    g->changed++;
}

void mi_tick(mi_game_t * g, int dt_ms)
{
    if(g->laid && !g->over && g->ms < 999000) g->ms += (uint32_t)dt_ms;
}
