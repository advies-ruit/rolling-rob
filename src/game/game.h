#ifndef ROLLING_ROB_GAME_H
#define ROLLING_ROB_GAME_H

#include "lvgl.h"
#include "draw.h"

/*
 * Game core: platform independent (no SDL in here). Physics,
 * entities, camera, messages and the win/lose flow are ported 1:1 from
 * rolling-rob.html; rendering goes through draw.c/gfx.c into the LVGL
 * canvas buffer, and the HUD (hud.c) is built from real LVGL widgets.
 *
 * Input model (touchscreen first, keyboard optional):
 *   - touch slots 0..RR_TOUCH_SLOTS-1 (multi-touch on the GT911; the SDL
 *     simulator only feeds slot 0 from the mouse)
 *   - a touch on the playfield = the original's click-and-hold on Rob:
 *     hold to charge, release to launch (or tap for a quick jump)
 *   - the four on-screen arrow buttons = the original's arrow keys
 *   - physical keys (arrows/space) keep working exactly like the original
 */

#define RR_TOUCH_SLOTS 5

typedef enum {
    RR_KEY_LEFT,
    RR_KEY_RIGHT,
    RR_KEY_UP,
    RR_KEY_DOWN,
    RR_KEY_SPACE,
    RR_KEY_ART,   /* switches between the painted art and the original look */
    RR_KEY_MUSIC, /* music on/off (only where the platform plays music) */
    RR_KEY_NEXT_TRACK,
    RR_KEY_FPS,   /* frame counter on/off (desktop and web builds) */
} rr_key_t;

/* view_scale: device pixels per world unit (0.25 maps the original
 * 1920x1080 canvas onto 480x272 1:1). */
void game_init(int32_t screen_w, int32_t screen_h, float view_scale);
void game_update(uint32_t dt_ms);

/* Rasterizes the frame into the canvas' draw buffer and draws the in-world
 * text (FINISH, JULIAN, CP1..) with LVGL fonts on top. */
void game_render(lv_obj_t * canvas);
/* Renders the frame in bands straight into `sink` (see rr_draw_frame_bands),
 * with no full-frame canvas; the game's text is blended into the bands. */
void game_render_bands(int w, int h, int band_rows, rr_band_sink_t sink, void * user);

/* x/y are in screen pixels, as reported by the input driver. */
void game_input_touch(int32_t slot, bool pressed, int32_t x, int32_t y);
void game_input_key(rr_key_t key, bool pressed);

/* Show/hide the on-screen arrow buttons (on by default). */
void game_set_touch_controls(bool enabled);

/* HUD state: the platform layer builds LVGL widgets around the canvas and
 * polls these each frame. */
int32_t game_get_level(void);
int32_t game_get_level_count(void);
const char * game_get_level_name(int32_t level);
int32_t game_get_distance_m(void);
int32_t game_get_goal_m(void);
const char * game_get_message(void);
bool game_is_over(void);
bool game_is_won(void);
bool game_should_show_next_button(void);

/* --- Speedrun timing ---
 * The level clock starts at the first launch of an attempt and stops at the
 * finish line (or at death; Reset restarts it). Times count physics steps,
 * so they are exact and reproducible. A "run" is a chain of levels finished
 * in order starting at level 1 (auto-advance, Next, Space/tap after a win);
 * its total is the sum of the finishing attempts. Best level times and the
 * best run total persist in a small text file. */
void game_set_save_path(const char * path); /* before game_init; NULL = default */
bool game_is_started(void);
int32_t game_get_level_time_ms(void);         /* running, or final after the finish */
int32_t game_get_best_time_ms(int32_t level); /* 0 = no time yet */
bool game_is_run_active(void);
int32_t game_get_run_total_ms(void);
int32_t game_get_best_run_ms(void);           /* 0 = no complete run yet */

/* Music. The game only keeps the choice (saved as "music 0 1|0" next to the
 * times, default on); playing it is the platform's job. A platform that can
 * play music calls game_set_music_available(true), which also shows the
 * music tile in the level picker. game_music_take_next() reports a "next
 * track" request (N key, the picker tile's long press) once. */
void game_set_music_available(bool available);
bool game_music_available(void);
bool game_music_enabled(void);
void game_set_music_enabled(bool on);         /* saves the choice */
void game_music_request_next(void);
bool game_music_take_next(void);

/* The frame counter: the game keeps the choice (saved as "fps 0 1|0"), the
 * platform shows it. */
bool game_fps_enabled(void);
void game_set_fps_enabled(bool on);           /* saves the choice */

/* Title screen (desktop and web builds). The
 * camera glides slowly over a random level with no HUD and no buttons; a tap,
 * Space or Enter ends it and starts level 1 (that input is not a jump). */
void game_title_begin(void);
bool game_title_active(void);
/* Whether a tap on the title starts the game (default); off when the
 * platform puts a menu there instead (the web/desktop app). Space always does. */
void game_title_set_tap_start(bool on);

/* Read-only snapshot for bots/tests. */
typedef struct {
    float x, y, vx, vy;
    int32_t radius;
    int32_t jumps_used;
    bool on_ground;
    bool started, over, won;
    float finish_line;
    float level_time_ms;
} rr_bot_view_t;
void game_get_bot_view(rr_bot_view_t * out);
float game_terrain_height(float x);
float game_terrain_slope(float x);

/* Test hook: move Rob to world x (e.g. just before the finish line). */
void game_debug_set_rob_x(float x);

/* Reset button: respawn Rob on the current level, keep terrain/entities. */
void game_reset_level(void);
/* Level dropdown / Next button: regenerate terrain + entities for `level`. */
void game_goto_level(int32_t level);

#endif /* ROLLING_ROB_GAME_H */
