/* Breakout: the walls, the paddle, launching, losing balls, levels. */

#include "check.h"
#include "breakout.h"
#include <math.h>

static int bricks_left(const bo_game_t * g)
{
    int n = 0;
    for(int r = 0; r < BO_ROWS; r++)
        for(int c = 0; c < BO_COLS; c++) n += g->brick[r][c] > 0;
    return n;
}

static void test_new_game(void)
{
    bo_game_t g = { 0 };
    bo_new(&g, 1, 120);
    CHECK_EQ(g.lives, 3);
    CHECK_EQ(g.level, 1);
    CHECK_EQ(g.best, 120);
    CHECK(g.stuck);
    CHECK_EQ(g.left, (BO_ROWS - 1) * BO_COLS); /* a full wall but the top row */
    CHECK_EQ(g.left, bricks_left(&g));
}

static void test_paddle(void)
{
    bo_game_t g = { 0 };
    bo_new(&g, 1, 0);
    bo_update(&g, 16, 0, 100.0f, false);
    CHECK(fabsf(g.px - 100.0f) < 0.01f);
    CHECK(fabsf(g.bx - g.px) < 0.01f); /* the ball rides along */
    bo_update(&g, 16, 0, 0.0f, false);
    CHECK(fabsf(g.px - g.pw / 2) < 0.01f); /* kept inside */
    for(int i = 0; i < 100; i++) bo_update(&g, 16, 1, -1.0f, false);
    CHECK(fabsf(g.px - (BO_FW - g.pw / 2)) < 0.01f);
    CHECK(g.stuck);
}

static void test_launch(void)
{
    bo_game_t g = { 0 };
    bo_new(&g, 1, 0);
    bo_update(&g, 16, 0, -1.0f, true);
    CHECK(!g.stuck);
    CHECK(g.vy < 0); /* upwards */
    CHECK(fabsf(g.vx) < fabsf(g.vy));
    CHECK(fabsf(sqrtf(g.vx * g.vx + g.vy * g.vy) - g.speed) < 0.5f);
}

static void test_lose_balls(void)
{
    /* The paddle kept away from the ball: three balls, then over. */
    bo_game_t g = { 0 };
    bo_new(&g, 2, 0);
    int t = 0;
    while(!g.over && t < 100000) {
        float away = g.bx < BO_FW / 2 ? BO_FW : 0.0f;
        int lives = g.lives;
        bo_update(&g, 16, 0, g.stuck ? -1.0f : away, true);
        CHECK(g.lives == lives || g.lives == lives - 1);
        CHECK_EQ(g.left, bricks_left(&g));
        t++;
    }
    CHECK(g.over);
    CHECK_EQ(g.lives, 0);
    /* nothing moves after the end */
    float bx = g.bx, by = g.by;
    bo_update(&g, 16, 1, -1.0f, true);
    CHECK(g.bx == bx && g.by == by);
}

static void test_play_through_a_level(void)
{
    /* The paddle under the ball, a little off centre so the ball goes
     * sideways: it clears the first wall. */
    bo_game_t g = { 0 };
    bo_new(&g, 3, 0);
    int t = 0, score = 0;
    while(!g.over && g.level == 1 && t < 200000) {
        bo_update(&g, 16, 0, g.bx + (t / 2000 % 2 ? 6.0f : -6.0f), true);
        CHECK(g.score >= score);
        score = g.score;
        CHECK(g.speed <= 340.0f + 0.01f);
        CHECK(g.by > -BO_BALL_R && g.bx > -BO_BALL_R && g.bx < BO_FW + BO_BALL_R);
        t++;
    }
    CHECK_EQ(g.level, 2);
    CHECK(g.score >= (BO_ROWS - 1) * BO_COLS * 10);
    CHECK(g.stuck); /* served again */
    /* level 2: the top row of its wall is silver and takes two hits */
    int silver = 0;
    for(int r = 0; r < BO_ROWS; r++)
        for(int c = 0; c < BO_COLS; c++)
            if(g.silver[r][c]) {
                silver++;
                CHECK_EQ(g.brick[r][c], 2);
            }
    CHECK(silver > 0);
}

int main(void)
{
    RUN(test_new_game);
    RUN(test_paddle);
    RUN(test_launch);
    RUN(test_lose_balls);
    RUN(test_play_through_a_level);
    return check_result();
}
