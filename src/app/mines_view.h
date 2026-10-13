#ifndef ROLLING_ROB_MINES_VIEW_H
#define ROLLING_ROB_MINES_VIEW_H

#include <stdbool.h>
#include <stdint.h>

/* Minesweeper (src/mines) in the app, on the games' shared canvas (pix.h),
 * w x h. Hidden until shown. */
void mines_view_create(int w, int h);
void mines_view_show(bool on);
void mines_view_tick(uint32_t dt_ms);
/* A key went down (APP_KEY_*, app.h): arrows move the cursor, Space / Enter
 * open, F / X flag, 1 2 3 the size, N new game. */
void mines_view_key(int k);
/* The home button's action (back to the app's menu). */
void mines_view_set_menu(void (*cb)(void));

/* For the game's page in the menu (app_page.c): a game under way, a new
 * one, the size (0 easy, 1 medium, 2 hard; setting it starts a new game),
 * "no guessing" (kept; the next board takes it), the best times. */
bool mines_view_running(void);
void mines_view_new_game(void);
int mines_view_size(void);
const char * mines_view_size_name(int size);
void mines_view_set_size(int size);
bool mines_view_no_guess(void);
void mines_view_set_no_guess(bool on);
const char * mines_view_best_text(void);

/* Its screen, as it would look, into the picture on its page (app_page.h). */
void mines_view_preview(void);

#endif /* ROLLING_ROB_MINES_VIEW_H */
