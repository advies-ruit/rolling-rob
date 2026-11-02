/* Snake: waiting for the first turn, turning, walls, apples, the body. */

#include "check.h"
#include "snake.h"

static int head_x(const sn_game_t * g) { return g->x[g->head]; }
static int head_y(const sn_game_t * g) { return g->y[g->head]; }

static void test_new_game(void)
{
    sn_game_t g = { 0 };
    sn_new(&g, 7, 30);
    CHECK_EQ(g.len, 4);
    CHECK_EQ(g.dir, SN_RIGHT);
    CHECK_EQ(g.best, 30);
    CHECK(!g.started && !g.over);
    CHECK(g.fx >= 0 && g.fy >= 0);
    CHECK_EQ(sn_cell(&g, g.fx, g.fy), SN_FOOD);
    CHECK_EQ(sn_cell(&g, head_x(&g), head_y(&g)), SN_HEAD);
    /* it waits for the first turn */
    CHECK(!sn_update(&g, 1000));
    CHECK_EQ(head_x(&g), SN_W / 2);
}

static void test_turns(void)
{
    sn_game_t g = { 0 };
    sn_new(&g, 7, 0);
    g.fx = g.fy = -1; /* no apple in the way */
    int x = head_x(&g), y = head_y(&g);
    sn_turn(&g, SN_LEFT); /* straight back into itself: ignored */
    CHECK(!g.started);
    sn_turn(&g, SN_UP);
    CHECK(g.started);
    CHECK(!sn_update(&g, 149));
    CHECK(sn_update(&g, 1));
    CHECK_EQ(head_x(&g), x);
    CHECK_EQ(head_y(&g), y - 1);

    /* two quick turns are both kept */
    sn_turn(&g, SN_LEFT);
    sn_turn(&g, SN_DOWN);
    sn_update(&g, 150);
    CHECK_EQ(head_x(&g), x - 1);
    CHECK_EQ(head_y(&g), y - 1);
    sn_update(&g, 150);
    CHECK_EQ(head_x(&g), x - 1);
    CHECK_EQ(head_y(&g), y); /* into the cell the tail just left: allowed */
    CHECK(!g.over);
}

static void test_body_kills(void)
{
    sn_game_t g = { 0 };
    sn_new(&g, 7, 0);
    g.fx = g.fy = -1;
    g.grow = 1; /* now the tail stays where it is */
    sn_turn(&g, SN_UP);
    sn_update(&g, 150);
    sn_turn(&g, SN_LEFT);
    sn_update(&g, 150);
    sn_turn(&g, SN_DOWN);
    sn_update(&g, 150);
    CHECK(g.over);
    CHECK(!g.won);
}

static void test_wall_kills(void)
{
    sn_game_t g = { 0 };
    sn_new(&g, 7, 0);
    g.fx = g.fy = -1;
    sn_turn(&g, SN_UP);
    int steps = 0;
    while(!g.over && steps < 100) {
        sn_update(&g, g.step_ms);
        steps++;
    }
    CHECK(g.over);
    CHECK_EQ(steps, SN_H / 2 + 1); /* the rows above it, then the wall */
}

static void test_apple(void)
{
    sn_game_t g = { 0 };
    sn_new(&g, 7, 0);
    g.fx = head_x(&g);
    g.fy = head_y(&g) - 1;
    sn_turn(&g, SN_UP);
    sn_update(&g, 150);
    CHECK_EQ(g.score, 10);
    CHECK_EQ(g.eaten, 1);
    CHECK_EQ(g.step_ms, 147); /* 3 ms quicker */
    CHECK(g.fx >= 0);         /* a new apple */
    CHECK(sn_cell(&g, g.fx, g.fy) == SN_FOOD);
    CHECK_EQ(g.len, 4);
    sn_update(&g, g.step_ms);
    CHECK_EQ(g.len, 5); /* one longer, a step later */
}

static void test_golden_apple(void)
{
    sn_game_t g = { 0 };
    sn_new(&g, 7, 0);
    g.eaten = 4; /* the next one is the fifth */
    g.fx = head_x(&g);
    g.fy = head_y(&g) - 1;
    sn_turn(&g, SN_UP);
    sn_update(&g, 150);
    CHECK_EQ(g.bonus_steps, SN_BONUS_STEPS);
    CHECK(g.bx >= 0);
    CHECK_EQ(sn_cell(&g, g.bx, g.by), SN_BONUS);
    /* it goes away when its steps run out */
    sn_game_t h = g;
    h.fx = h.fy = -1;
    h.bx = h.by = 0; /* out of the way, in a corner */
    h.bonus_steps = 1;
    sn_update(&h, h.step_ms);
    CHECK(!h.over);
    CHECK_EQ(h.bonus_steps, 0);
    CHECK_EQ(h.bx, -1);
}

static void test_no_burst_after_stall(void)
{
    sn_game_t g = { 0 };
    sn_new(&g, 7, 0);
    g.fx = g.fy = -1;
    sn_turn(&g, SN_UP);
    uint32_t steps = g.steps;
    sn_update(&g, 10000);
    CHECK_EQ(g.steps - steps, 4); /* at most four steps at once */
}

int main(void)
{
    RUN(test_new_game);
    RUN(test_turns);
    RUN(test_body_kills);
    RUN(test_wall_kills);
    RUN(test_apple);
    RUN(test_golden_apple);
    RUN(test_no_burst_after_stall);
    return check_result();
}
