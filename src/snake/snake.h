#ifndef ROLLING_ROB_SNAKE_H
#define ROLLING_ROB_SNAKE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Snake, the rules only (the view is src/app/snake_view.c).
 *   - a field of SN_W x SN_H cells with walls around it; the snake starts
 *     four long in the middle, heading right, and waits for the first turn
 *   - one step per tick of step_ms; it starts at 150 ms and gets 3 ms
 *     quicker per apple, down to 65
 *   - an apple: 10 points and one cell longer; every fifth apple brings a
 *     golden one for 45 steps: 50 points and three cells
 *   - a wall or its own body ends the game (moving into the cell the tail
 *     leaves is allowed); a full field wins it
 *   - up to two turns are kept, so a quick double turn is not lost; a turn
 *     back into itself is ignored
 */

#define SN_W 28
#define SN_H 15
#define SN_MAX (SN_W * SN_H)
#define SN_BONUS_STEPS 45

typedef enum { SN_UP, SN_RIGHT, SN_DOWN, SN_LEFT } sn_dir_t;
typedef enum { SN_EMPTY, SN_BODY, SN_HEAD, SN_FOOD, SN_BONUS } sn_cell_t;

typedef struct {
    uint8_t x[SN_MAX], y[SN_MAX]; /* ring: the head at [head], the tail len - 1 behind */
    int head, len, grow;
    uint8_t occ[SN_H][SN_W];      /* 1 where the body is */
    sn_dir_t dir;
    sn_dir_t queue[2];
    int queued;
    int fx, fy;                   /* the apple, -1 if none */
    int bx, by, bonus_steps;      /* the golden apple while bonus_steps > 0 */
    int score, best, eaten;
    bool started, over, won;
    int step_ms, acc_ms;
    uint32_t steps;               /* counts steps (and new games): the view redraws on a change */
    uint32_t rng;
} sn_game_t;

void sn_new(sn_game_t * g, uint32_t seed, int best);
void sn_turn(sn_game_t * g, sn_dir_t d);
/* Returns whether the snake stepped. */
bool sn_update(sn_game_t * g, int dt_ms);
sn_cell_t sn_cell(const sn_game_t * g, int x, int y);

#endif /* ROLLING_ROB_SNAKE_H */
