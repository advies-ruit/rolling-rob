/* Tetris: shapes, the bag of seven, gravity, hard drop, line clears and
 * scoring, hold, game over. */

#include "check.h"
#include "tetris.h"
#include <string.h>

#define BOTTOM (TT_ROWS - 1)

/* A game with the piece of our choice at the top of an empty well. */
static void with_piece(tt_game_t * g, int piece, int rot, int x)
{
    tt_new(g, 99, 0);
    memset(g->cell, 0, sizeof(g->cell));
    g->piece = piece;
    g->rot = rot;
    g->x = x;
    g->y = 0;
}

static void test_shapes(void)
{
    for(int p = 0; p < TT_PIECES; p++) {
        int first[4][2];
        tt_cells(p, 0, first);
        for(int r = 0; r < 4; r++) {
            int c[4][2];
            tt_cells(p, r, c);
            for(int i = 0; i < 4; i++)
                for(int j = i + 1; j < 4; j++) CHECK(c[i][0] != c[j][0] || c[i][1] != c[j][1]);
        }
        int again[4][2];
        tt_cells(p, 4, again); /* four turns: back where it started */
        CHECK(memcmp(first, again, sizeof(first)) == 0);
        CHECK(tt_color(p) != 0x808080);
    }
}

static void test_bag_of_seven(void)
{
    tt_game_t g;
    tt_new(&g, 12345, 0);
    int seen[3 * TT_PIECES], n = 0;
    seen[n++] = g.piece;
    for(int i = 0; i < TT_NEXT; i++) seen[n++] = g.next[i];
    while(n < 3 * TT_PIECES) {
        tt_input(&g, TT_HARD, true);
        memset(g.cell, 0, sizeof(g.cell)); /* never fill up */
        seen[n++] = g.next[TT_NEXT - 1];
    }
    CHECK(!g.over);
    for(int bag = 0; bag < 3; bag++) {
        int count[TT_PIECES] = { 0 };
        for(int i = 0; i < TT_PIECES; i++) count[seen[bag * TT_PIECES + i]]++;
        for(int p = 0; p < TT_PIECES; p++) CHECK_EQ(count[p], 1);
    }
}

static void test_spawn_and_gravity(void)
{
    tt_game_t g;
    tt_new(&g, 5, 0);
    CHECK(g.active);
    CHECK_EQ(g.x, 3);
    CHECK_EQ(g.y, 1); /* one row down: into view */
    CHECK_EQ(g.level, 1);
    tt_update(&g, 999);
    CHECK_EQ(g.y, 1);
    tt_update(&g, 1); /* level 1: a row a second */
    CHECK_EQ(g.y, 2);

    /* a soft drop: a row per 33 ms, a point a row */
    tt_input(&g, TT_SOFT, true);
    tt_update(&g, 33);
    CHECK_EQ(g.y, 3);
    CHECK_EQ(g.score, 1);
}

static void test_move_and_walls(void)
{
    tt_game_t g;
    with_piece(&g, TT_O, 0, 3);
    for(int i = 0; i < 10; i++) {
        tt_input(&g, TT_LEFT, true);
        tt_input(&g, TT_LEFT, false);
    }
    int c[4][2];
    tt_cells(TT_O, 0, c);
    int min_x = 99;
    for(int i = 0; i < 4; i++)
        if(g.x + c[i][0] < min_x) min_x = g.x + c[i][0];
    CHECK_EQ(min_x, 0); /* stopped at the wall */

    /* auto-repeat: held right, after 167 ms a move, then one per 33 ms */
    int x = g.x;
    tt_input(&g, TT_RIGHT, true);
    CHECK_EQ(g.x, x + 1);
    tt_update(&g, 166);
    CHECK_EQ(g.x, x + 1);
    tt_update(&g, 1);
    CHECK_EQ(g.x, x + 2);
    tt_update(&g, 33);
    CHECK_EQ(g.x, x + 3);
}

static void test_hard_drop(void)
{
    tt_game_t g;
    with_piece(&g, TT_T, 0, 3);
    int ghost = tt_ghost_y(&g);
    CHECK_EQ(ghost, BOTTOM - 1); /* the T is two rows high */
    tt_input(&g, TT_HARD, true);
    CHECK_EQ(g.score, 2 * ghost);
    CHECK_EQ(g.cell[BOTTOM][3], TT_T + 1);
    CHECK_EQ(g.cell[BOTTOM][4], TT_T + 1);
    CHECK_EQ(g.cell[BOTTOM][5], TT_T + 1);
    CHECK_EQ(g.cell[BOTTOM - 1][4], TT_T + 1);
    CHECK(g.active); /* the next piece is there */
}

static void test_single_line(void)
{
    tt_game_t g;
    with_piece(&g, TT_I, 0, 3); /* flat, columns 3-6 */
    for(int x = 0; x < TT_W; x++)
        if(x < 3 || x > 6) g.cell[BOTTOM][x] = 1;
    g.cell[BOTTOM - 1][0] = 1; /* a block above that drops a row */
    int rows = tt_ghost_y(&g);
    tt_input(&g, TT_HARD, true);
    CHECK_EQ(g.lines, 1);
    CHECK_EQ(g.last_lines, 1);
    CHECK_EQ(g.score, 2 * rows + 100);
    CHECK(!g.active); /* the row flashes first */
    CHECK(g.clearing[BOTTOM]);
    tt_update(&g, TT_CLEAR_MS);
    CHECK(g.active);
    CHECK_EQ(g.cell[BOTTOM][0], 1); /* the block above came down */
    for(int x = 1; x < TT_W; x++) CHECK_EQ(g.cell[BOTTOM][x], 0);
}

static void test_tetris_and_back_to_back(void)
{
    tt_game_t g;
    with_piece(&g, TT_I, 1, 7); /* upright, in column 9 */
    for(int y = BOTTOM - 7; y <= BOTTOM; y++)
        for(int x = 0; x < TT_W - 1; x++) g.cell[y][x] = 1;
    tt_input(&g, TT_HARD, true);
    int drop = 2 * (BOTTOM - 3);
    CHECK_EQ(g.lines, 4);
    CHECK_EQ(g.score, drop + 800);
    CHECK(g.b2b);
    tt_update(&g, TT_CLEAR_MS);

    /* a second Tetris straight after: x1.5, and a combo of one */
    g.piece = TT_I;
    g.rot = 1;
    g.x = 7;
    g.y = 0;
    int before = g.score;
    tt_input(&g, TT_HARD, true);
    CHECK_EQ(g.lines, 8);
    CHECK_EQ(g.score - before, drop + 800 * 3 / 2 + 50);
}

static void test_level_up(void)
{
    tt_game_t g;
    with_piece(&g, TT_I, 0, 3);
    g.lines = 9;
    for(int x = 0; x < TT_W; x++)
        if(x < 3 || x > 6) g.cell[BOTTOM][x] = 1;
    tt_input(&g, TT_HARD, true);
    CHECK_EQ(g.lines, 10);
    CHECK_EQ(g.level, 2);
}

static void test_hold(void)
{
    tt_game_t g;
    tt_new(&g, 8, 0);
    int first = g.piece, second = g.next[0];
    tt_input(&g, TT_HOLD, true);
    CHECK_EQ(g.hold, first);
    CHECK_EQ(g.piece, second);
    tt_input(&g, TT_HOLD, true); /* once per piece */
    CHECK_EQ(g.hold, first);
    CHECK_EQ(g.piece, second);
    tt_input(&g, TT_HARD, true);
    tt_input(&g, TT_HOLD, true); /* swaps now */
    CHECK_EQ(g.piece, first);
}

static void test_rotation_kick(void)
{
    /* a T against the left wall, turned: it fits after a kick */
    tt_game_t g;
    with_piece(&g, TT_T, 1, -1); /* rotation R: column 1 of its box */
    g.y = 5;
    tt_input(&g, TT_CW, true);
    CHECK_EQ(g.rot, 2);
    int c[4][2];
    tt_cells(TT_T, g.rot, c);
    for(int i = 0; i < 4; i++) CHECK(g.x + c[i][0] >= 0);
    /* the O never turns */
    with_piece(&g, TT_O, 0, 3);
    tt_input(&g, TT_CW, true);
    CHECK_EQ(g.rot, 0);
}

static void test_game_over(void)
{
    tt_game_t g;
    tt_new(&g, 1, 50);
    for(int y = 0; y < TT_ROWS; y++)
        for(int x = 1; x < TT_W; x++) g.cell[y][x] = 1; /* full but for column 0 */
    tt_input(&g, TT_HOLD, true); /* the next piece cannot appear */
    CHECK(g.over);
    CHECK(!g.active);
    tt_update(&g, 16);
    CHECK_EQ(g.best, 50);
}

static void test_random_play(void)
{
    /* Random input until the game ends: the well stays consistent. */
    for(uint32_t seed = 1; seed <= 20; seed++) {
        tt_game_t g;
        tt_new(&g, seed, 0);
        uint32_t rng = seed;
        for(int t = 0; t < 200000 && !g.over; t++) {
            rng = rng * 1664525u + 1013904223u;
            int in = (int)((rng >> 16) % 7);
            tt_input(&g, (tt_input_t)in, (rng >> 8) & 1);
            tt_update(&g, 16);
            for(int y = 0; y < TT_ROWS; y++)
                for(int x = 0; x < TT_W; x++) CHECK(g.cell[y][x] <= TT_PIECES);
            CHECK_EQ(g.level, 1 + g.lines / 10);
            CHECK(g.best >= g.score);
        }
        CHECK(g.over);
    }
}

int main(void)
{
    RUN(test_shapes);
    RUN(test_bag_of_seven);
    RUN(test_spawn_and_gravity);
    RUN(test_move_and_walls);
    RUN(test_hard_drop);
    RUN(test_single_line);
    RUN(test_tetris_and_back_to_back);
    RUN(test_level_up);
    RUN(test_hold);
    RUN(test_rotation_kick);
    RUN(test_game_over);
    RUN(test_random_play);
    return check_result();
}
