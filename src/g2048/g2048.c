/* 2048, the rules (see g2048.h). */

#include "g2048.h"
#include <string.h>

static uint32_t rnd(g2_game_t * g)
{
    /* xorshift32 */
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g->rng = x;
}

static bool add_tile(g2_game_t * g)
{
    int free_cells[G2_N * G2_N], n = 0;
    for(int i = 0; i < G2_N * G2_N; i++)
        if(g->cell[i / G2_N][i % G2_N] == 0) free_cells[n++] = i;
    if(n == 0) return false;
    int at = free_cells[rnd(g) % (uint32_t)n];
    g->new_r = at / G2_N;
    g->new_c = at % G2_N;
    g->cell[g->new_r][g->new_c] = rnd(g) % 10 == 0 ? 2 : 1;
    return true;
}

void g2_new(g2_game_t * g, uint32_t seed, int best)
{
    uint32_t moves = g->moves;
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0x2048u;
    g->best = best;
    add_tile(g);
    add_tile(g);
    g->new_r = -1; /* both appear together, without the "new" pop */
    g->moves = moves + 1;
}

bool g2_can_move(const g2_game_t * g)
{
    for(int r = 0; r < G2_N; r++)
        for(int c = 0; c < G2_N; c++) {
            if(g->cell[r][c] == 0) return true;
            if(c + 1 < G2_N && g->cell[r][c] == g->cell[r][c + 1]) return true;
            if(r + 1 < G2_N && g->cell[r][c] == g->cell[r + 1][c]) return true;
        }
    return false;
}

int g2_max_exp(const g2_game_t * g)
{
    int m = 0;
    for(int r = 0; r < G2_N; r++)
        for(int c = 0; c < G2_N; c++)
            if(g->cell[r][c] > m) m = g->cell[r][c];
    return m;
}

bool g2_move(g2_game_t * g, g2_dir_t dir)
{
    if(g->over) return false;
    uint8_t before[G2_N][G2_N];
    memcpy(before, g->cell, sizeof(before));
    int score_before = g->score;
    g->slides = 0;
    g->gained = 0;
    memset(g->merged, 0, sizeof(g->merged));

    /* Each line is walked from the wall the tiles move towards: position i
     * of line l is the cell (r, c) below. */
    for(int l = 0; l < G2_N; l++) {
        int target = 0;     /* next free place in the line */
        int last_exp = 0;   /* the tile at target - 1, if it may still merge */
        for(int i = 0; i < G2_N; i++) {
            int r, c;
            switch(dir) {
                case G2_LEFT: r = l; c = i; break;
                case G2_RIGHT: r = l; c = G2_N - 1 - i; break;
                case G2_UP: r = i; c = l; break;
                default: r = G2_N - 1 - i; c = l; break;
            }
            int e = before[r][c];
            if(e == 0) continue;
            int at;
            bool merge = last_exp == e;
            if(merge) {
                at = target - 1;
                last_exp = 0; /* merged once */
            }
            else {
                at = target++;
                last_exp = e;
            }
            int tr, tc;
            switch(dir) {
                case G2_LEFT: tr = l; tc = at; break;
                case G2_RIGHT: tr = l; tc = G2_N - 1 - at; break;
                case G2_UP: tr = at; tc = l; break;
                default: tr = G2_N - 1 - at; tc = l; break;
            }
            g->slide[g->slides++] = (g2_slide_t){ (int8_t)r, (int8_t)c, (int8_t)tr, (int8_t)tc, (uint8_t)e };
            if(merge) {
                g->merged[tr][tc] = true;
                g->score += 1 << (e + 1);
                g->gained += 1 << (e + 1);
            }
        }
    }
    /* the grid after the slides */
    memset(g->cell, 0, sizeof(g->cell));
    for(int i = 0; i < g->slides; i++) {
        const g2_slide_t * s = &g->slide[i];
        g->cell[s->to_r][s->to_c] = g->merged[s->to_r][s->to_c] ? (uint8_t)(s->exp + 1) : s->exp;
    }
    if(memcmp(before, g->cell, sizeof(before)) == 0) {
        g->slides = 0;
        return false;
    }
    memcpy(g->prev_cell, before, sizeof(before));
    g->prev_score = score_before;
    g->can_undo = true;
    add_tile(g);
    if(g->score > g->best) g->best = g->score;
    if(!g->won && g2_max_exp(g) >= 11) g->won = true;
    g->over = !g2_can_move(g);
    g->moves++;
    return true;
}

bool g2_undo(g2_game_t * g)
{
    if(!g->can_undo) return false;
    memcpy(g->cell, g->prev_cell, sizeof(g->cell));
    g->score = g->prev_score;
    g->can_undo = false;
    g->over = false;
    g->slides = 0;
    g->new_r = -1;
    g->gained = 0;
    memset(g->merged, 0, sizeof(g->merged));
    g->moves++;
    return true;
}
