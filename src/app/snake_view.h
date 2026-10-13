#ifndef ROLLING_ROB_SNAKE_VIEW_H
#define ROLLING_ROB_SNAKE_VIEW_H

#include <stdbool.h>
#include <stdint.h>

/* Snake (src/snake) in the app, on the games' shared canvas (pix.h), w x h.
 * Hidden until shown; hiding pauses. */
void snake_view_create(int w, int h);
void snake_view_show(bool on);
void snake_view_tick(uint32_t dt_ms);
/* A key went down (APP_KEY_*, app.h): arrows or WASD steer, P pause,
 * N new game, Space / Enter go on or play again. */
void snake_view_key(int k);
/* The home button's action (back to the app's menu). */
void snake_view_set_menu(void (*cb)(void));

/* For the game's page in the menu (app_page.c): a game is under way (the
 * page offers Continue), start a new one, the best score. */
bool snake_view_running(void);
void snake_view_new_game(void);
int snake_view_best(void);

/* Its screen, as it would look, into the picture on its page (app_page.h). */
void snake_view_preview(void);

#endif /* ROLLING_ROB_SNAKE_VIEW_H */
