#ifndef ROLLING_ROB_BREAKOUT_H
#define ROLLING_ROB_BREAKOUT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Breakout, the rules only (the view is src/app/breakout_view.c).
 *   - a field of BO_FW x BO_FH units (the view's pixels before scaling),
 *     a wall of BO_ROWS x BO_COLS bricks near the top, a paddle at the bottom
 *   - the ball waits on the paddle until launched; it bounces off the walls,
 *     the ceiling, bricks and the paddle -- where it hits the paddle sets the
 *     angle (up to 60 degrees off vertical)
 *   - a brick: 10 points for the bottom row, 10 more per row up; a silver
 *     one (from level 2) takes two hits; every brick gone: the next level,
 *     another pattern, a quicker ball
 *   - the ball speeds up a little with every brick, up to a limit
 *   - three balls; past the paddle costs one
 */

#define BO_COLS 12
#define BO_ROWS 7
#define BO_FW 320.0f
#define BO_FH 256.0f
#define BO_BRICK_TOP 28.0f
#define BO_BRICK_H 10.0f
#define BO_BRICK_W (BO_FW / BO_COLS)
#define BO_PADDLE_Y 240.0f /* the paddle's top */
#define BO_PADDLE_H 7.0f
#define BO_BALL_R 3.5f

typedef struct {
    uint8_t brick[BO_ROWS][BO_COLS]; /* hits left, 0: gone */
    uint8_t silver[BO_ROWS][BO_COLS];
    float px, pw;                    /* paddle centre and width */
    float bx, by, vx, vy, speed;
    bool stuck;                      /* the ball waits on the paddle */
    int lives, score, best, level, left;
    bool over;
    uint32_t bricks_changed;         /* counts brick changes, for the view */
    uint32_t rng;
} bo_game_t;

void bo_new(bo_game_t * g, uint32_t seed, int best);
/* move: -1, 0, 1 (keys); target_x >= 0: the paddle goes there (a finger),
 * in field units; launch: let the ball go. */
void bo_update(bo_game_t * g, int dt_ms, int move, float target_x, bool launch);

#endif /* ROLLING_ROB_BREAKOUT_H */
