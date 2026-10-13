#ifndef ROLLING_ROB_TETRIS_VIEW_H
#define ROLLING_ROB_TETRIS_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/* The falling-blocks game (src/tetris) in the app: its own canvas on
 * `parent`, its widgets on the top layer. Hidden until shown; hiding pauses. */
void tetris_view_create(lv_obj_t * parent, int w, int h);
void tetris_view_show(bool on);
void tetris_view_tick(uint32_t dt_ms);
/* A key went down (APP_KEY_*, app.h; the held ones -- arrows -- are read
 * with app_key_held). */
void tetris_view_key(int k);
/* Fingers after the first (the first is LVGL's pointer): screen pixels. */
void tetris_view_touch(int slot, bool down, int32_t x, int32_t y);
/* The home button's action (back to the app's menu). */
void tetris_view_set_menu(void (*cb)(void));

/* For the game's page in the menu (app_page.c): a game is under way (the
 * page offers Continue), start a new one, the best score. */
bool tetris_view_running(void);
void tetris_view_new_game(void);
int tetris_view_best(void);

/* Its screen, as it would look, into the picture on its page (app_page.h). */
void tetris_view_preview(void);

#endif /* ROLLING_ROB_TETRIS_VIEW_H */
