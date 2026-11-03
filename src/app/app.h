#ifndef ROLLING_ROB_APP_H
#define ROLLING_ROB_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"

/*
 * The app around the game (app.c): a menu on the title screen -- play,
 * Tetris, 2048, the 3D demo, system information -- and statistics. Used by
 * the desktop/web simulator (main_sim.c); what a platform supplies is the key
 * source and the info page's contents (app_info_sdl.c).
 */

/* Keys, whatever the platform calls them: letters are 'a'..'z', digits '0'..'9'. */
enum {
    APP_KEY_BACKSPACE = 8,
    APP_KEY_ENTER = 13,
    APP_KEY_ESC = 27,
    APP_KEY_SPACE = ' ',
    APP_KEY_LEFT = 0x100,
    APP_KEY_RIGHT,
    APP_KEY_UP,
    APP_KEY_DOWN,
    APP_KEY_CTRL,
    APP_KEY_SHIFT,
};

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

/* The app draws the frame itself (the 3D demo): the platform calls
 * app_render_bands() where it would draw the game, with its band sink. */
bool app_draws_frame(void);
void app_render_bands(int w, int h, void (*sink)(uint32_t * band, int stride_px, int y0, int rows, void * user),
                      void * user);

/* Rolling Rob's frame counter is switched on and Rolling Rob is on screen:
 * the platform shows its fps overlay (or always, when asked to measure). */
bool app_fps_wanted(void);

/* A key went down (no repeats). */
void app_key(int k);
/* Platform: whether a key is held now (Tetris reads the arrows this way,
 * the 3D demo the arrows, letters and Space). */
bool app_key_held(int k);

/* Every finger, for the touch test: down / up with an id unique while down. */
void app_note_finger(bool down, int64_t id);
/* Fingers after the first, when the game does not take them: screen pixels. */
void app_touch(int slot, bool down, int32_t x, int32_t y);
/* From the main loop: how long one pass of lv_timer_handler() took. */
void app_note_loop_ms(float ms);
/* The frames that reached the screen, when that is not one per LVGL redraw
 * (a platform that shows frames itself). NULL: LVGL's redraws are counted. */
void app_set_frame_counter(uint32_t (*frames)(void));

/* Where a game keeps its best score: /save/<name>_best on the web (IndexedDB,
 * synced by the page), ~/.rolling_rob_<name> elsewhere. */
void app_save_path(char * out, size_t n, const char * name);

/* ---- the info page ---- */

/* Platform: fills the page with app_info_put() lines, twice a second;
 * `open` the first time after the page came up. */
void app_info_fill(bool open);
/* A line in column 0 or 1; a key starting with '#' is a heading. */
void app_info_put(int col, const char * key, const char * fmt, ...);
/* For app_info_fill. */
lv_display_t * app_display(void);
void app_picture(int * w, int * h, int * k);
float app_fps(void);
float app_loop_ms(void);
int app_fingers(int * most);

/* main_sim.c: whether the picture size can be chosen here (the desktop
 * program; the web page has its own choice on the start screen), and the
 * one it runs at now (1-4 times the 480x272 LCD). */
bool sim_scale_choosable(void);
int sim_scale_now(void);

/* main_sim.c: an SDL window the program covers whole needs no alpha and no
 * screen background (saves two full-screen passes per frame). */
void sim_display_opaque(lv_display_t * disp);

#endif /* ROLLING_ROB_APP_H */
