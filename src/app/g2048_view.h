#ifndef ROLLING_ROB_G2048_VIEW_H
#define ROLLING_ROB_G2048_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/* 2048 (src/g2048) in the app: a page of its own on the top layer, w x h.
 * Hidden until shown. */
void g2048_view_create(int w, int h);
void g2048_view_show(bool on);
void g2048_view_tick(uint32_t dt_ms);
/* A key went down (APP_KEY_*, app.h): arrows or WASD move, N new game,
 * U or Backspace undo. */
void g2048_view_key(int k);
/* The home button's action (back to the app's menu). */
void g2048_view_set_menu(void (*cb)(void));

/* For the game's page in the menu (app_page.c): a game is under way (the
 * page offers Continue), start a new one, the best score. */
bool g2048_view_running(void);
void g2048_view_new_game(void);
int g2048_view_best(void);

/* Its screen, as it would look, into the picture on its page (app_page.h). */
void g2048_view_preview(void);

#endif /* ROLLING_ROB_G2048_VIEW_H */
