#ifndef ROLLING_ROB_TETRIS_VIEW_H
#define ROLLING_ROB_TETRIS_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

/* The falling-blocks game (src/tetris) in the app: its own canvas on
 * `parent`, its widgets on the top layer. Hidden until shown; hiding pauses. */
void tetris_view_create(lv_obj_t * parent, int w, int h);
void tetris_view_show(bool on);
void tetris_view_tick(uint32_t dt_ms);
/* A key went down (the held ones -- arrows -- are read from SDL's state). */
void tetris_view_key(SDL_Scancode k);
/* Fingers after the first (the first is LVGL's pointer): screen pixels. */
void tetris_view_touch(int slot, bool down, int32_t x, int32_t y);
/* The home button's action (back to the app's menu). */
void tetris_view_set_menu(void (*cb)(void));

#endif /* ROLLING_ROB_TETRIS_VIEW_H */
