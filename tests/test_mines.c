/* Minesweeper: a safe first opening, the numbers, flood fill, flags,
 * chording, winning, losing, and boards without guessing. Built with
 * MI_CHECK, so the solver also asserts that every conclusion is true. */

#include "check.h"
#include "mines.h"

static int count_mines(const mi_game_t * g)
{
    int n = 0;
    for(int y = 0; y < g->h; y++)
        for(int x = 0; x < g->w; x++) n += g->mine[y][x];
    return n;
}

static int near(const mi_game_t * g, int x, int y)
{
    int n = 0;
    for(int dy = -1; dy <= 1; dy++)
        for(int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if(nx >= 0 && nx < g->w && ny >= 0 && ny < g->h) n += g->mine[ny][nx];
        }
    return n;
}

static void test_first_opening_is_safe(void)
{
    for(uint32_t seed = 1; seed <= 200; seed++) {
        mi_game_t g = { 0 };
        int sx = (int)(seed % 16), sy = (int)(seed % 16);
        mi_new(&g, 16, 16, 40, seed, false);
        CHECK(!g.laid);
        mi_open(&g, sx, sy);
        CHECK(g.laid);
        CHECK(!g.over || g.won);
        CHECK_EQ(count_mines(&g), 40);
        CHECK_EQ(g.adj[sy][sx], 0); /* no mine around it: an area opens */
        CHECK(g.opened >= 4);
        for(int y = 0; y < g.h; y++)
            for(int x = 0; x < g.w; x++) {
                CHECK_EQ(g.adj[y][x], near(&g, x, y));
                if(g.open[y][x]) CHECK(!g.mine[y][x]);
            }
    }
}

static void test_too_many_mines(void)
{
    mi_game_t g = { 0 };
    mi_new(&g, 5, 5, 100, 1, false);
    CHECK_EQ(g.mines, 25 - 9); /* room for the first opening */
    mi_new(&g, 99, 99, 10, 1, false);
    CHECK_EQ(g.w, MI_MAXW);
    CHECK_EQ(g.h, MI_MAXH);
}

/* The first mine and a safe numbered cell next to it. */
static bool find_number(const mi_game_t * g, int * ox, int * oy)
{
    for(int y = 0; y < g->h; y++)
        for(int x = 0; x < g->w; x++)
            if(!g->mine[y][x] && g->adj[y][x] > 0 && !g->open[y][x]) {
                *ox = x;
                *oy = y;
                return true;
            }
    return false;
}

static void test_lose(void)
{
    mi_game_t g = { 0 };
    mi_new(&g, 9, 9, 10, 3, false);
    mi_open(&g, 4, 4);
    for(int y = 0; y < g.h; y++)
        for(int x = 0; x < g.w; x++)
            if(g.mine[y][x] && !g.over) {
                mi_open(&g, x, y);
                CHECK(g.over && !g.won);
                CHECK_EQ(g.boom_x, x);
                CHECK_EQ(g.boom_y, y);
            }
    /* nothing happens after the end */
    int opened = g.opened;
    mi_open(&g, 0, 0);
    CHECK_EQ(g.opened, opened);
}

static void test_flags(void)
{
    mi_game_t g = { 0 };
    mi_new(&g, 9, 9, 10, 4, false);
    mi_open(&g, 4, 4);
    int x = 0, y = 0;
    CHECK(find_number(&g, &x, &y));
    mi_flag(&g, x, y);
    CHECK_EQ(g.flags, 1);
    mi_open(&g, x, y); /* a flag stops it */
    CHECK(!g.open[y][x]);
    mi_flag(&g, x, y);
    CHECK_EQ(g.flags, 0);
    mi_open(&g, x, y);
    CHECK(g.open[y][x]);
    mi_flag(&g, x, y); /* an open cell takes no flag */
    CHECK_EQ(g.flags, 0);
}

static void test_chord(void)
{
    /* An opened number with all its mines flagged opens the rest around it. */
    bool tried = false;
    for(uint32_t seed = 1; seed <= 50 && !tried; seed++) {
        mi_game_t g = { 0 };
        mi_new(&g, 9, 9, 10, seed, false);
        mi_open(&g, 4, 4);
        for(int y = 0; y < g.h && !tried; y++)
            for(int x = 0; x < g.w && !tried; x++) {
                if(!g.open[y][x] || g.adj[y][x] == 0) continue;
                int closed_safe = 0;
                for(int dy = -1; dy <= 1; dy++)
                    for(int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if(nx < 0 || nx >= g.w || ny < 0 || ny >= g.h) continue;
                        if(!g.open[ny][nx] && !g.mine[ny][nx]) closed_safe++;
                    }
                if(closed_safe == 0) continue;
                for(int dy = -1; dy <= 1; dy++)
                    for(int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if(nx >= 0 && nx < g.w && ny >= 0 && ny < g.h && g.mine[ny][nx]) mi_flag(&g, nx, ny);
                    }
                mi_open(&g, x, y);
                for(int dy = -1; dy <= 1; dy++)
                    for(int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if(nx >= 0 && nx < g.w && ny >= 0 && ny < g.h && !g.mine[ny][nx]) CHECK(g.open[ny][nx]);
                    }
                CHECK(!g.over || g.won);
                tried = true;
            }
    }
    CHECK(tried);
}

static void test_win(void)
{
    mi_game_t g = { 0 };
    mi_new(&g, 9, 9, 10, 5, false);
    mi_open(&g, 0, 0);
    for(int y = 0; y < g.h; y++)
        for(int x = 0; x < g.w; x++)
            if(!g.mine[y][x] && !g.open[y][x]) mi_open(&g, x, y);
    CHECK(g.over && g.won);
    CHECK_EQ(g.opened, 81 - 10);
    CHECK_EQ(g.flags, 10); /* the mines are flagged */
}

static void test_clock(void)
{
    mi_game_t g = { 0 };
    mi_new(&g, 9, 9, 10, 6, false);
    mi_tick(&g, 500);
    CHECK_EQ(g.ms, 0); /* not before the first opening */
    mi_open(&g, 4, 4);
    mi_tick(&g, 500);
    mi_tick(&g, 250);
    CHECK_EQ(g.ms, 750);
}

static void test_no_guessing(void)
{
    /* The three usual sizes: every board is solvable from the first opening,
     * and the solver's every conclusion is true (MI_CHECK). */
    static const int sizes[3][3] = { { 9, 9, 10 }, { 16, 16, 40 }, { 30, 16, 99 } };
    for(int s = 0; s < 3; s++)
        for(uint32_t seed = 1; seed <= 10; seed++) {
            mi_game_t g = { 0 };
            mi_new(&g, sizes[s][0], sizes[s][1], sizes[s][2], seed, true);
            int sx = g.w / 2, sy = g.h / 2;
            mi_open(&g, sx, sy);
            CHECK(g.tries >= 1 && g.tries <= MI_TRIES);
            if(g.tries < MI_TRIES) CHECK(g.solvable);
            CHECK_EQ(g.solvable, mi_solvable(&g, sx, sy));
        }
}

int main(void)
{
    RUN(test_first_opening_is_safe);
    RUN(test_too_many_mines);
    RUN(test_lose);
    RUN(test_flags);
    RUN(test_chord);
    RUN(test_win);
    RUN(test_clock);
    RUN(test_no_guessing);
    return check_result();
}
