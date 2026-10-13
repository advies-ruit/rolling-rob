#ifndef ROLLING_ROB_TETRIS_H
#define ROLLING_ROB_TETRIS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * A falling-blocks game, the rules only -- no drawing, no platform (the
 * desktop/web view is src/platform/sim/tetris_view.c). Modern rules, as most
 * players know them:
 *   - 10 x 20 well (plus two hidden rows above), seven pieces from a shuffled
 *     bag of seven, the next five shown
 *   - SRS rotation with its wall kicks, both directions
 *   - hold (once per piece), ghost piece, hard and soft drop
 *   - lock delay 0.5 s, reset by moving or turning (15 times at most)
 *   - auto-repeat for held left/right: 167 ms delay, then every 33 ms
 *   - gravity per level ((0.8 - 0.007 (L-1))^(L-1) s per row), a level per
 *     10 lines
 *   - score: 100 / 300 / 500 / 800 x level for 1-4 lines, a Tetris after a
 *     Tetris x1.5, combos 50 x combo x level, soft drop 1 and hard drop 2 a row
 */

#define TT_W 10
#define TT_H 20       /* visible rows */
#define TT_HIDDEN 2   /* above them */
#define TT_ROWS (TT_H + TT_HIDDEN)
#define TT_NEXT 5
#define TT_CLEAR_MS 300 /* full rows flash this long before they go */

enum { TT_I, TT_O, TT_T, TT_S, TT_Z, TT_J, TT_L, TT_PIECES };

typedef enum {
    TT_LEFT,   /* held */
    TT_RIGHT,  /* held */
    TT_SOFT,   /* held: soft drop */
    TT_CW,     /* pressed: turn clockwise */
    TT_CCW,    /* pressed: turn counter-clockwise */
    TT_HARD,   /* pressed: hard drop */
    TT_HOLD,   /* pressed */
} tt_input_t;

typedef struct {
    uint8_t cell[TT_ROWS][TT_W]; /* 0 empty, else piece + 1; row 0 is the top hidden row */
    int piece, rot, x, y;        /* falling piece: its box's top left in the well */
    bool active;                 /* false while rows clear, and after game over */
    int hold;                    /* -1: nothing held */
    bool hold_used;
    int next[TT_NEXT];
    int bag[TT_PIECES], bag_left;
    uint32_t rng;

    int score, lines, level, best;
    int combo;                   /* -1: the last piece cleared nothing */
    bool b2b;                    /* the last clear was a Tetris */
    bool over;

    /* timing */
    int fall_ms, lock_ms, lock_resets;
    bool held[3];                /* left, right, soft */
    int das_dir, das_ms;
    int clear_ms;
    bool clearing[TT_ROWS];

    /* for the view */
    uint32_t changed;            /* counts every visible change */
    int last_lines;              /* rows of the last clear (1..4), for a word on screen */
    uint32_t last_clear_seq;     /* changes when a clear happens */
} tt_game_t;

void tt_new(tt_game_t * g, uint32_t seed, int best);
void tt_input(tt_game_t * g, tt_input_t in, bool down);
void tt_update(tt_game_t * g, int dt_ms);

/* The four cells of a piece in a rotation: x, y inside its box. */
void tt_cells(int piece, int rot, int out[4][2]);
/* Row the falling piece would land on (its y). */
int tt_ghost_y(const tt_game_t * g);
/* Colour of a piece, 0xRRGGBB. */
uint32_t tt_color(int piece);

#endif /* ROLLING_ROB_TETRIS_H */
