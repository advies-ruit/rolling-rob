#ifndef ROLLING_ROB_MINES_H
#define ROLLING_ROB_MINES_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Minesweeper, the rules only (the view is src/app/mines_view.c).
 *   - w x h cells, `mines` of them mined; the mines are laid at the first
 *     opening, never on that cell or its eight neighbours
 *   - opening a cell with no mines around opens its neighbours too, on and
 *     on; opening a number whose flags all around add up to it opens the
 *     other neighbours ("chording")
 *   - a flag marks a cell and stops it from opening
 *   - opening a mine loses; opening every other cell wins (and flags the
 *     mines)
 *   - the clock runs from the first opening to the end
 *   - "no guessing": the mines are laid again (up to MI_TRIES times) until
 *     the board can be cleared from the first opening by reasoning alone --
 *     a solver that knows what a player knows (the numbers it has opened,
 *     the mine count) and uses the plain rules: a number whose mines are all
 *     found frees its other neighbours, one with as many closed neighbours
 *     as missing mines has them all mined, the same between two numbers
 *     whose closed neighbours overlap, and the mine count at the end
 */

#define MI_TRIES 3000

#define MI_MAXW 30
#define MI_MAXH 16

typedef struct {
    int w, h, mines;
    uint8_t mine[MI_MAXH][MI_MAXW], open[MI_MAXH][MI_MAXW], flag[MI_MAXH][MI_MAXW], adj[MI_MAXH][MI_MAXW];
    bool laid, over, won;
    bool no_guess;
    int tries;           /* boards laid for this one (no guessing); solvable: whether the last was */
    bool solvable;
    int boom_x, boom_y;  /* the mine that went off */
    int opened, flags;
    uint32_t ms;         /* the clock */
    uint32_t changed;    /* counts changes, for the view */
    uint32_t rng;
} mi_game_t;

void mi_new(mi_game_t * g, int w, int h, int mines, uint32_t seed, bool no_guess);
/* Whether the board as laid can be cleared from (sx, sy) by reasoning (the
 * solver above); for tests. */
bool mi_solvable(const mi_game_t * g, int sx, int sy);
void mi_open(mi_game_t * g, int x, int y);
void mi_flag(mi_game_t * g, int x, int y);
void mi_tick(mi_game_t * g, int dt_ms);

#endif /* ROLLING_ROB_MINES_H */
