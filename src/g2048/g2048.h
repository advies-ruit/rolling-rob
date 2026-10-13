#ifndef ROLLING_ROB_G2048_H
#define ROLLING_ROB_G2048_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 2048, the rules only -- no drawing, no platform (the view is
 * src/app/g2048_view.c). As the original (Gabriele Cirulli, 2014):
 *   - a 4 x 4 grid; a move slides every tile as far as it goes in one
 *     direction, and two equal tiles that meet become one of twice the value
 *     (each tile merges at most once per move; the pair nearest the wall
 *     first)
 *   - a move that changes nothing does not count and adds no tile
 *   - after every move a new tile: 2 (90 %) or 4 (10 %) in a random free cell
 *   - the score goes up by every merged tile's new value
 *   - 2048 wins (play on if you like); no move left ends the game
 * Plus one undo of the last move.
 */

#define G2_N 4

typedef enum { G2_LEFT, G2_RIGHT, G2_UP, G2_DOWN } g2_dir_t;

/* Where a tile went in the last move, for the view's slide. */
typedef struct {
    int8_t from_r, from_c, to_r, to_c;
    uint8_t exp; /* its value before the move: 1 << exp */
} g2_slide_t;

typedef struct {
    uint8_t cell[G2_N][G2_N]; /* 0 empty, else the value's exponent: 1 = 2, 11 = 2048 */
    int score, best;
    bool won;        /* 2048 reached (once) */
    bool over;       /* no move left */
    uint32_t rng;

    /* the last move, for the view */
    g2_slide_t slide[G2_N * G2_N];
    int slides;
    bool merged[G2_N][G2_N]; /* cells that became a merged tile */
    int new_r, new_c;        /* the tile added after it, -1 if none */
    int gained;              /* its score */
    uint32_t moves;          /* counts moves that changed the grid (and new games, undos) */

    /* undo: the state before the last move */
    uint8_t prev_cell[G2_N][G2_N];
    int prev_score;
    bool can_undo;
} g2_game_t;

void g2_new(g2_game_t * g, uint32_t seed, int best);
/* Returns whether anything moved. */
bool g2_move(g2_game_t * g, g2_dir_t dir);
bool g2_undo(g2_game_t * g);
/* Whether any move is possible. */
bool g2_can_move(const g2_game_t * g);
/* The largest tile's exponent. */
int g2_max_exp(const g2_game_t * g);

#endif /* ROLLING_ROB_G2048_H */
