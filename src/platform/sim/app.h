#ifndef ROLLING_ROB_APP_H
#define ROLLING_ROB_APP_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/*
 * The desktop and web app around the game (app.c): a menu on the title
 * screen -- play, Tetris, system information -- and statistics.
 */

/* Before hud_create(): the level picker gets a "Menu" button. */
void app_menu_button(void);

/* After hud_create(). w x h is the picture size, k the render scale (1..4). */
void app_init(lv_display_t * disp, int w, int h, int k);

/* Per tick, after the game's frame. */
void app_tick(uint32_t dt_ms);

/* The game is drawn (title, play, or behind the info page) ... */
bool app_game_runs(void);
/* ... and takes keys and touches (title or play, nothing over it). */
bool app_game_input(void);

/* From the SDL event filter: every finger, for the touch test. */
void app_note_finger(bool down, int64_t id);
/* Fingers after the first, when the game does not take them: screen pixels. */
void app_touch(int slot, bool down, int32_t x, int32_t y);
/* From the main loop: how long one pass of lv_timer_handler() took. */
void app_note_loop_ms(float ms);

/* main_sim.c: an SDL window the program covers whole needs no alpha and no
 * screen background (saves two full-screen passes per frame). */
void sim_display_opaque(lv_display_t * disp);

#endif /* ROLLING_ROB_APP_H */
