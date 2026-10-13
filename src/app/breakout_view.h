#ifndef ROLLING_ROB_BREAKOUT_VIEW_H
#define ROLLING_ROB_BREAKOUT_VIEW_H

#include <stdbool.h>
#include <stdint.h>

/* Breakout (src/breakout) in the app, on the games' shared canvas (pix.h),
 * w x h. Hidden until shown; hiding pauses. */
void breakout_view_create(int w, int h);
void breakout_view_show(bool on);
void breakout_view_tick(uint32_t dt_ms);
/* A key went down (APP_KEY_*, app.h): Space / up launch, P pause, N new
 * game; the paddle follows the held arrows or A / D (app_key_held). */
void breakout_view_key(int k);
/* The home button's action (back to the app's menu). */
void breakout_view_set_menu(void (*cb)(void));

/* For the game's page in the menu (app_page.c): a game is under way (the
 * page offers Continue), start a new one, the best score. */
bool breakout_view_running(void);
void breakout_view_new_game(void);
int breakout_view_best(void);

/* Its screen, as it would look, into the picture on its page (app_page.h). */
void breakout_view_preview(void);

#endif /* ROLLING_ROB_BREAKOUT_VIEW_H */
