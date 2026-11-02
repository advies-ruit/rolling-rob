/* 2048: sliding, merging once, no-op moves, undo, game over. */

#include "check.h"
#include "g2048.h"
#include <string.h>

static int tiles(const g2_game_t * g)
{
    int n = 0;
    for(int r = 0; r < G2_N; r++)
        for(int c = 0; c < G2_N; c++) n += g->cell[r][c] != 0;
    return n;
}

/* A game with an empty grid, to set up by hand. */
static void blank(g2_game_t * g)
{
    g2_new(g, 1234, 0);
    memset(g->cell, 0, sizeof(g->cell));
}

static void test_new_game(void)
{
    g2_game_t g = { 0 };
    g2_new(&g, 42, 77);
    CHECK_EQ(tiles(&g), 2);
    CHECK_EQ(g.score, 0);
    CHECK_EQ(g.best, 77);
    CHECK(!g.over && !g.won && !g.can_undo);
    for(int r = 0; r < G2_N; r++)
        for(int c = 0; c < G2_N; c++) CHECK(g.cell[r][c] <= 2);

    g2_game_t h = { 0 };
    g2_new(&h, 42, 77);
    CHECK(memcmp(g.cell, h.cell, sizeof(g.cell)) == 0); /* same seed, same start */
}

static void test_merge_pairs(void)
{
    g2_game_t g;
    blank(&g);
    /* 2 2 2 2 -> 4 4 . . : two merges, 8 points */
    for(int c = 0; c < G2_N; c++) g.cell[0][c] = 1;
    CHECK(g2_move(&g, G2_LEFT));
    CHECK_EQ(g.cell[0][0], 2);
    CHECK_EQ(g.cell[0][1], 2);
    CHECK_EQ(g.score, 8);
    CHECK_EQ(g.gained, 8);
    CHECK(g.merged[0][0] && g.merged[0][1]);
    CHECK_EQ(tiles(&g), 3); /* two tiles and the new one */
    CHECK(g.new_r >= 0 && g.new_c >= 0);
}

static void test_merge_once(void)
{
    g2_game_t g;
    blank(&g);
    /* 2 2 4 . -> 4 4 . . : the new 4 does not merge again */
    g.cell[1][0] = 1;
    g.cell[1][1] = 1;
    g.cell[1][2] = 2;
    CHECK(g2_move(&g, G2_LEFT));
    CHECK_EQ(g.cell[1][0], 2);
    CHECK_EQ(g.cell[1][1], 2);
    CHECK_EQ(g.score, 4);
}

static void test_nearest_wall_first(void)
{
    g2_game_t g;
    blank(&g);
    /* column 2 2 2 . moved down: the bottom pair merges, the top 2 stays */
    g.cell[0][3] = 1;
    g.cell[1][3] = 1;
    g.cell[2][3] = 1;
    CHECK(g2_move(&g, G2_DOWN));
    CHECK_EQ(g.cell[3][3], 2);
    CHECK_EQ(g.cell[2][3], 1);
    CHECK(g.merged[3][3] && !g.merged[2][3]);
}

static void test_noop_move(void)
{
    g2_game_t g;
    blank(&g);
    g.cell[0][0] = 1;
    g.cell[0][1] = 2;
    uint32_t moves = g.moves;
    CHECK(!g2_move(&g, G2_LEFT));
    CHECK(!g2_move(&g, G2_UP));
    CHECK_EQ(tiles(&g), 2); /* no new tile */
    CHECK_EQ(g.moves, moves);
}

static void test_undo(void)
{
    g2_game_t g;
    blank(&g);
    g.cell[2][0] = 3;
    g.cell[2][3] = 3;
    uint8_t before[G2_N][G2_N];
    memcpy(before, g.cell, sizeof(before));
    CHECK(!g2_undo(&g)); /* nothing to undo yet */
    CHECK(g2_move(&g, G2_RIGHT));
    CHECK_EQ(g.cell[2][3], 4);
    CHECK_EQ(g.score, 16);
    CHECK(g2_undo(&g));
    CHECK(memcmp(g.cell, before, sizeof(before)) == 0);
    CHECK_EQ(g.score, 0);
    CHECK(!g2_undo(&g)); /* only one */
}

static void test_game_over_and_win(void)
{
    g2_game_t g;
    blank(&g);
    /* a full grid with no equal neighbours */
    for(int r = 0; r < G2_N; r++)
        for(int c = 0; c < G2_N; c++) g.cell[r][c] = (uint8_t)((r + c) % 2 ? 1 : 2);
    CHECK(!g2_can_move(&g));

    blank(&g);
    g.cell[0][0] = 10;
    g.cell[0][1] = 10;
    CHECK(g2_move(&g, G2_LEFT));
    CHECK(g.won);
    CHECK_EQ(g2_max_exp(&g), 11);
}

static void test_random_play(void)
{
    /* Many random games: the score only grows and is the sum of the merges,
     * the best follows it, and the game ends when no move is left. */
    for(uint32_t seed = 1; seed <= 50; seed++) {
        g2_game_t g = { 0 };
        g2_new(&g, seed, 0);
        uint32_t rng = seed * 2654435761u;
        int moves = 0;
        while(!g.over && moves < 5000) {
            rng = rng * 1664525u + 1013904223u;
            int score = g.score;
            if(g2_move(&g, (g2_dir_t)((rng >> 16) & 3))) {
                moves++;
                CHECK_EQ(g.score, score + g.gained);
                CHECK(g.best >= g.score);
            }
        }
        CHECK(g.over);
        CHECK(!g2_can_move(&g));
        CHECK_EQ(tiles(&g), G2_N * G2_N);
    }
}

int main(void)
{
    RUN(test_new_game);
    RUN(test_merge_pairs);
    RUN(test_merge_once);
    RUN(test_nearest_wall_first);
    RUN(test_noop_move);
    RUN(test_undo);
    RUN(test_game_over_and_win);
    RUN(test_random_play);
    return check_result();
}
