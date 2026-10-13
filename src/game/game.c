#include "game.h"
#include "gfx.h"
#include "draw.h"
#include "art.h"
#include "prof.h"
#include "level.h"
#include "terrain.h"
#include "entities.h"
#include "ui.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Original constants (rolling-rob.html `rob` object + updatePhysics()). */
#define ROB_GRAVITY        0.4f
#define ROB_BOUNCE         0.6f
#define ROB_FRICTION       0.98f
#define ARROW_ACCEL_X      0.5f
#define ARROW_ACCEL_Y      0.3f
#define WORLD_W            1920.0f
#define WORLD_H            1080.0f
#define WORLD_FALL_LIMIT   (WORLD_H + 50.0f)
#define FIXED_STEP_MS      16.6667f /* the original assumes a 60fps rAF loop */
#define PUSH_CHARGE_MS     480.0f   /* 30 ticks * 16ms to full charge */
#define AUTO_ADVANCE_STEPS 300      /* 300 frames = 5s @ 60fps */
#define ARRAY_LEN(a) (int32_t)(sizeof(a) / sizeof((a)[0]))

static int32_t s_screen_w, s_screen_h;
static float s_scale;      /* device px per world unit */
static float s_view_w;     /* visible world width/height */
static float s_view_h;

static rr_terrain_t s_terrain;
static int32_t s_level;

static struct {
    float x, y, vx, vy, rotation;
    int32_t radius;
    int32_t jumps_used;
    bool on_ground;
} s_rob;

static float s_camera_x, s_camera_y;
static struct { bool left, right, up, down; } s_keys;      /* physical keys */
static struct { bool left, right, up, down; } s_touch_keys; /* on-screen buttons */
static bool s_space_down;
static bool s_touch_controls = true;

static bool s_game_started, s_game_over, s_game_won, s_show_next_button;
static float s_game_over_at_ms;      /* anim clock when the run ended (for the reset lockout) */

/* Speedrun timing (see game.h) */
static int32_t s_level_steps;        /* physics steps since this attempt's first launch */
static bool s_timer_running;
static int32_t s_level_time_ms;      /* frozen at the finish line */
static int32_t s_best_ms[RR_LEVEL_COUNT];
static int32_t s_run_next_level = -1;/* level index the active run expects next; -1 = no run */
static int32_t s_run_total_ms;
static int32_t s_best_run_ms;
static char s_save_path[512];
#define STEP_TO_MS(steps) ((int32_t)((int64_t)(steps) * 1000 / 60))
#define RESET_LOCKOUT_MS 700.0f      /* ignore reset presses right after dying */
static int32_t s_auto_advance_steps, s_distance_m;
static float s_anim_ms;
static char s_message[200];

/* Touch slots */
/* ZONE_JUMP is the space bar: a jump the moment it is pressed (players with
 * a keyboard expect the same). ZONE_PLAYFIELD is the
 * mouse: tap = jump, hold = charge, release = launch (the original's
 * click-and-hold on Rob). ZONE_BUTTON = the arrow buttons. */
typedef enum { ZONE_NONE, ZONE_PLAYFIELD, ZONE_JUMP, ZONE_BUTTON, ZONE_RESET } touch_zone_t;
#define TAP_MAX_MS 150.0f /* shorter than this = tap (jump), longer = charged launch */
static struct {
    bool active;
    int32_t x, y;
    touch_zone_t zone;
    float held_ms;
} s_touch[RR_TOUCH_SLOTS];

/* Particles */
static rr_confetti_t s_confetti[RR_CONFETTI_MAX];
static rr_particle_t s_fireworks[RR_FIREWORK_MAX];
static int32_t s_firework_next;
static int32_t s_firework_timer;
static rr_particle_t s_explosion[RR_EXPLOSION_MAX];
static int32_t s_explosion_next;

static float randf(void) { return rr_randf(); }

/* --- Flavor text, 1:1 from rolling-rob.html --- */
static const char * const k_start_messages[] = {
    "Let's f*cking roll!", "Holy sh*t, here we go!", "Time to get baked and roll!",
    "Hell yeah, let's do this!", "F*ck yeah, rolling out!",
};
static const char * const k_startup_hints[] = {
    "Use ARROW KEYS to move, SPACEBAR to jump! Click and hold Rob for power launch!",
    "Arrow keys control movement, SPACE for double jump! Let's go!",
    "Move with arrows, jump with SPACE! Click Rob to launch hard!",
    "Arrow keys + SPACE = control! Watch out for Julians and mines!",
    "ARROWS move, SPACE jumps, CLICK launches! F*ck yeah!",
};
static const char * const k_touch_hints[] = {
    "JUMP jumps, twice in the air! Hold the screen for a power launch!",
    "Tap to jump, hold the screen to power-launch! Watch out for Julians and mines!",
    "Hold the screen for a big launch, hold UP to float! Let's go!",
};
static const char * const k_abduct_messages[] = {
    "Holy sh*t! The UFO set you back 20 meters!", "F*ck! Abducted and dumped back!",
    "Damn aliens! They got you!", "UFO got your ass! Back 20 meters!",
};
static const char * const k_fire_messages[] = {
    "Julian's fire roasted your ass! Game over!", "Holy sh*t! Julian's fireball f*cked you up!",
    "Damn! Julian burned you to a crisp!", "F*ck! Julian's fire killed you! Try again!",
};
static const char * const k_julian_messages[] = {
    "Julian crushed you! Boss fight failed!", "Holy sh*t! Julian destroyed your ass!",
    "Damn! You got too close to Julian!", "F*ck! Julian killed you! Dodge better!",
};
static const char * const k_mine_messages[] = {
    "BOOM! You hit a f*cking mine! Game over!", "Holy sh*t! Mine explosion! You're dead!",
    "Damn! That mine blew your ass up!", "F*ck! Mine detonated! Try again!",
};
static const char * const k_fall_messages[] = {
    "Oh sh*t! You fell off the damn mountain!", "F*ck! Gravity's a b*tch, try again!",
    "Damn it! You ate sh*t hard!", "Aw hell, you f*cked that up!", "Sh*t! Down you go!",
};
static const char * const k_checkpoint_messages[] = {
    "Checkpoint %d reached! Keep going!", "F*ck yeah! Checkpoint %d saved!",
    "Holy sh*t! Checkpoint %d activated!", "Damn! Checkpoint %d is yours!",
};
static const char * const k_win_messages[] = {
    "F*ck yeah! Level %d crushed!", "Holy sh*t, you did it! Level %d complete!",
    "Damn, that was sick! Level %d done!", "Hell yeah! Level %d in the bag!",
    "Shit yeah! Level %d demolished!",
};

static void set_message(const char * text) { snprintf(s_message, sizeof(s_message), "%s", text); }
static void set_random_message(const char * const * options, int32_t count) { set_message(options[rand() % count]); }
static void set_random_message_fmt(const char * const * options, int32_t count, int32_t value)
{
    char buf[200];
    snprintf(buf, sizeof(buf), options[rand() % count], value);
    set_message(buf);
}
static void set_start_hint(void)
{
    if(s_touch_controls) set_random_message(k_touch_hints, ARRAY_LEN(k_touch_hints));
    else set_random_message(k_startup_hints, ARRAY_LEN(k_startup_hints));
}

/* ------------------------------------------------------------------ */
/* Particles (createConfetti / createFirework / createExplosion)       */
/* ------------------------------------------------------------------ */

static void create_confetti(void)
{
    for(int32_t i = 0; i < RR_CONFETTI_MAX; i++) {
        rr_confetti_t * c = &s_confetti[i];
        c->x = s_rob.x + (randf() - 0.5f) * 100.0f;
        c->y = s_rob.y - randf() * 50.0f;
        c->vx = (randf() - 0.5f) * 10.0f;
        c->vy = -randf() * 10.0f - 5.0f;
        c->color = gfx_hsl(randf() * 360.0f, 1.0f, 0.5f);
        c->size = randf() * 5.0f + 3.0f;
        c->rotation = randf() * (float)M_PI * 2.0f;
        c->rotation_speed = (randf() - 0.5f) * 0.3f;
        c->alive = true;
    }
}

static void update_confetti(void)
{
    for(int32_t i = 0; i < RR_CONFETTI_MAX; i++) {
        rr_confetti_t * c = &s_confetti[i];
        if(!c->alive) continue;
        c->vy += 0.3f;
        c->x += c->vx;
        c->y += c->vy;
        c->rotation += c->rotation_speed;
        if(c->y > WORLD_H) c->alive = false;
    }
}

static void create_firework(float x, float y)
{
    const int32_t particles = 50;
    for(int32_t i = 0; i < particles; i++) {
        float angle = ((float)M_PI * 2.0f * (float)i) / (float)particles;
        float speed = randf() * 3.0f + 2.0f;
        rr_particle_t * f = &s_fireworks[s_firework_next];
        s_firework_next = (s_firework_next + 1) % RR_FIREWORK_MAX;
        f->x = x;
        f->y = y;
        f->vx = cosf(angle) * speed;
        f->vy = sinf(angle) * speed;
        f->color = gfx_hsl(randf() * 360.0f, 1.0f, 0.5f);
        f->life = 1.0f;
        f->size = randf() * 3.0f + 2.0f;
    }
}

static void update_fireworks(void)
{
    for(int32_t i = 0; i < RR_FIREWORK_MAX; i++) {
        rr_particle_t * f = &s_fireworks[i];
        if(f->life <= 0) continue;
        f->x += f->vx;
        f->y += f->vy;
        f->vy += 0.1f;
        f->life -= 0.02f;
    }
}

static void create_explosion(float x, float y)
{
    for(int32_t i = 0; i < 30; i++) {
        float angle = ((float)M_PI * 2.0f * (float)i) / 30.0f;
        float speed = randf() * 5.0f + 2.0f;
        rr_particle_t * p = &s_explosion[s_explosion_next];
        s_explosion_next = (s_explosion_next + 1) % RR_EXPLOSION_MAX;
        p->x = x;
        p->y = y;
        p->vx = cosf(angle) * speed;
        p->vy = sinf(angle) * speed;
        p->life = 1.0f;
        p->size = randf() * 4.0f + 2.0f;
        p->color = randf() > 0.5f ? GFX_HEX(0xFF4500) : GFX_HEX(0xFFD700);
    }
}

static void update_explosion(void)
{
    for(int32_t i = 0; i < RR_EXPLOSION_MAX; i++) {
        rr_particle_t * p = &s_explosion[i];
        if(p->life <= 0) continue;
        p->x += p->vx;
        p->y += p->vy;
        p->vy += 0.2f;
        p->life -= 0.02f;
    }
}

static bool explosion_active(void)
{
    for(int32_t i = 0; i < RR_EXPLOSION_MAX; i++) if(s_explosion[i].life > 0) return true;
    return false;
}

/* ------------------------------------------------------------------ */
/* Best times and the art choice: tiny text file                        */
/* ------------------------------------------------------------------ */

void game_set_save_path(const char * path)
{
    if(path) snprintf(s_save_path, sizeof(s_save_path), "%s", path);
    else s_save_path[0] = '\0';
}

static void times_default_path(void)
{
    if(s_save_path[0]) return;
    const char * env = getenv("ROLLING_ROB_SAVE");
    if(env && env[0]) {
        snprintf(s_save_path, sizeof(s_save_path), "%s", env);
        return;
    }
    const char * home = getenv("HOME");
    snprintf(s_save_path, sizeof(s_save_path), "%s/.rolling_rob_times", home && home[0] ? home : "/tmp");
}

static void times_save(void);
static bool s_music_on = true, s_music_available;
static int s_music_skip;
static int s_music_volume = -1;
static rr_music_order_t s_music_order = RR_MUSIC_SHUFFLE;
static char s_music_title[64];
static int32_t s_music_pos_ms;
static bool s_fps_on;

bool game_fps_enabled(void) { return s_fps_on; }

void game_set_fps_enabled(bool on)
{
    if(on == s_fps_on) return;
    s_fps_on = on;
    times_save();
}

void game_set_music_available(bool available) { s_music_available = available; }
bool game_music_available(void) { return s_music_available; }
bool game_music_enabled(void) { return s_music_on; }
void game_music_request_next(void) { s_music_skip = 1; }
void game_music_request_skip(int dir) { s_music_skip = dir < 0 ? -1 : 1; }

void game_set_music_enabled(bool on)
{
    if(on == s_music_on) return;
    s_music_on = on;
    times_save();
}

int game_music_take_skip(void)
{
    int r = s_music_skip;
    s_music_skip = 0;
    return r;
}

static int s_music_volume_default = 70;
int game_music_volume(void) { return s_music_volume >= 0 ? s_music_volume : s_music_volume_default; }
void game_set_music_volume_default(int percent) { s_music_volume_default = percent < 0 ? 0 : percent > 100 ? 100 : percent; }

void game_set_music_volume(int percent)
{
    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;
    if(percent == game_music_volume() && s_music_volume >= 0) return;
    s_music_volume = percent;
    times_save();
}

rr_music_order_t game_music_order(void) { return s_music_order; }

void game_set_music_order(rr_music_order_t o)
{
    if(o == s_music_order || (unsigned)o >= RR_MUSIC_ORDERS) return;
    s_music_order = o;
    times_save();
}

void game_music_set_playing(const char * title, int32_t pos_ms)
{
    snprintf(s_music_title, sizeof(s_music_title), "%s", title ? title : "");
    s_music_pos_ms = pos_ms;
}

const char * game_music_playing(int32_t * pos_ms)
{
    if(pos_ms) *pos_ms = s_music_pos_ms;
    return s_music_title;
}

static void times_load(void)
{
    times_default_path();
    FILE * fp = fopen(s_save_path, "r");
    if(!fp) return;
    char key[16];
    int lv, ms;
    while(fscanf(fp, "%15s %d %d", key, &lv, &ms) == 3) {
        if(strcmp(key, "best") == 0 && lv >= 1 && lv <= RR_LEVEL_COUNT) s_best_ms[lv - 1] = ms;
        else if(strcmp(key, "run") == 0) s_best_run_ms = ms;
        else if(strcmp(key, "art") == 0) {
            rr_art_set_enabled(ms != 0);
            rr_art_set_rendered(ms == 2);
        }
        else if(strcmp(key, "music") == 0) s_music_on = ms != 0;
        else if(strcmp(key, "volume") == 0 && ms >= 0 && ms <= 100) s_music_volume = ms;
        else if(strcmp(key, "order") == 0 && ms >= 0 && ms < RR_MUSIC_ORDERS) s_music_order = (rr_music_order_t)ms;
        else if(strcmp(key, "fps") == 0) s_fps_on = ms != 0;
    }
    fclose(fp);
}

static void times_save(void)
{
    if(!s_save_path[0]) return;
    FILE * fp = fopen(s_save_path, "w");
    if(!fp) return;
    for(int32_t i = 0; i < RR_LEVEL_COUNT; i++) {
        if(s_best_ms[i] > 0) fprintf(fp, "best %d %d\n", (int)i + 1, (int)s_best_ms[i]);
    }
    if(s_best_run_ms > 0) fprintf(fp, "run 0 %d\n", (int)s_best_run_ms);
    /* the original look 0, painted art 1, rendered 2 */
    fprintf(fp, "art 0 %d\n", !rr_art_enabled() ? 0 : rr_art_rendered() ? 2 : 1);
    fprintf(fp, "music 0 %d\n", s_music_on ? 1 : 0);
    if(s_music_volume >= 0) fprintf(fp, "volume 0 %d\n", s_music_volume);
    fprintf(fp, "order 0 %d\n", (int)s_music_order);
    fprintf(fp, "fps 0 %d\n", s_fps_on ? 1 : 0);
    fclose(fp);
}

bool game_is_started(void) { return s_game_started; }
int32_t game_get_level_time_ms(void) { return s_game_won ? s_level_time_ms : STEP_TO_MS(s_level_steps); }
int32_t game_get_best_time_ms(int32_t level) { return (level >= 0 && level < RR_LEVEL_COUNT) ? s_best_ms[level] : 0; }
bool game_is_run_active(void) { return s_run_next_level >= 0; }
int32_t game_get_run_total_ms(void) { return s_run_total_ms; }
int32_t game_get_best_run_ms(void) { return s_best_run_ms; }

/* Called at the finish line. */
static void timer_finish(void)
{
    s_timer_running = false;
    s_level_time_ms = STEP_TO_MS(s_level_steps);
    bool record = false;
    if(s_best_ms[s_level] == 0 || s_level_time_ms < s_best_ms[s_level]) {
        s_best_ms[s_level] = s_level_time_ms;
        record = true;
    }
    if(s_run_next_level == s_level) {
        s_run_total_ms += s_level_time_ms;
        s_run_next_level = s_level + 1;
        if(s_level == RR_LEVEL_COUNT - 1) {
            if(s_best_run_ms == 0 || s_run_total_ms < s_best_run_ms) {
                s_best_run_ms = s_run_total_ms;
                record = true;
            }
            s_run_next_level = -1; /* run complete */
        }
    }
    if(record) times_save();
}

/* ------------------------------------------------------------------ */
/* Level / reset flow                                                  */
/* ------------------------------------------------------------------ */

static void spawn_rob(void)
{
    if(rr_last_checkpoint >= 0) {
        s_rob.x = rr_checkpoints[rr_last_checkpoint].x + 100.0f;
        s_rob.y = rr_checkpoints[rr_last_checkpoint].y;
    }
    else {
        s_rob.x = 200.0f;
        s_rob.y = 150.0f;
    }
    s_rob.vx = s_rob.vy = 0;
    s_rob.rotation = 0;
    s_rob.jumps_used = 0;
    s_rob.on_ground = false;
    s_distance_m = (rr_last_checkpoint >= 0) ? (int32_t)((s_rob.x - 200.0f) / 10.0f) : 0;
}

static void reset_run_state(void)
{
    spawn_rob();
    /* The level clock restarts from zero when the level starts over, but
     * keeps counting across a checkpoint respawn (level 10): you did not
     * restart the level, you continued it. It pauses until the next launch. */
    if(rr_last_checkpoint < 0) s_level_steps = 0;
    s_timer_running = false;
    s_level_time_ms = 0;
    s_game_started = s_game_over = s_game_won = s_show_next_button = false;
    s_auto_advance_steps = 0;
    s_space_down = false;
    memset(&s_keys, 0, sizeof(s_keys));
    memset(&s_touch_keys, 0, sizeof(s_touch_keys));
    memset(s_touch, 0, sizeof(s_touch));
    rr_beam_active = false;
    rr_beam_saucer_index = -1;
    rr_beam_progress = 0;
    for(int32_t i = 0; i < RR_CONFETTI_MAX; i++) s_confetti[i].alive = false;
    for(int32_t i = 0; i < RR_FIREWORK_MAX; i++) s_fireworks[i].life = 0;
    for(int32_t i = 0; i < RR_EXPLOSION_MAX; i++) s_explosion[i].life = 0;
    s_firework_timer = 0;
}

void game_reset_level(void)
{
    reset_run_state();
    set_start_hint();
}

void game_goto_level(int32_t level)
{
    if(level < 0) level = 0;
    if(level >= RR_LEVEL_COUNT) level = RR_LEVEL_COUNT - 1;
    /* Run bookkeeping: level 1 starts a run; any other jump than "the level
     * the run expects next" (auto-advance, Next, retry of the current level)
     * ends it. */
    if(level == 0) {
        s_run_next_level = 0;
        s_run_total_ms = 0;
    }
    else if(level != s_run_next_level) {
        s_run_next_level = -1;
    }
    s_level = level;
    rr_terrain_generate(&s_terrain, &rr_levels[level]);
    rr_entities_generate(level, &s_terrain);
    s_camera_x = 0;
    s_camera_y = 0;
    reset_run_state();
    set_start_hint();
}

void game_init(int32_t screen_w, int32_t screen_h, float view_scale)
{
    s_screen_w = screen_w;
    s_screen_h = screen_h;
    s_scale = view_scale;
    s_view_w = (float)screen_w / view_scale;
    s_view_h = (float)screen_h / view_scale;
    rr_ui_init(screen_w);
    s_rob.radius = 30;
    srand((unsigned int)time(NULL));
    times_load();
    game_goto_level(0);
}

void game_set_touch_controls(bool enabled) { s_touch_controls = enabled; }

void game_get_bot_view(rr_bot_view_t * out)
{
    out->x = s_rob.x;
    out->y = s_rob.y;
    out->vx = s_rob.vx;
    out->vy = s_rob.vy;
    out->radius = s_rob.radius;
    out->jumps_used = s_rob.jumps_used;
    out->on_ground = s_rob.on_ground;
    out->started = s_game_started;
    out->over = s_game_over;
    out->won = s_game_won;
    out->finish_line = s_terrain.finish_line;
    out->level_time_ms = (float)game_get_level_time_ms();
}

float game_terrain_height(float x) { return rr_terrain_height_at(&s_terrain, x); }
float game_terrain_slope(float x) { return rr_terrain_slope_at(&s_terrain, x); }

void game_debug_set_rob_x(float x)
{
    s_rob.x = x;
    s_rob.y = rr_terrain_height_at(&s_terrain, x) - (float)s_rob.radius - 100.0f;
    s_camera_x = x - s_view_w / 3.0f;
    if(s_camera_x < 0) s_camera_x = 0;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Title screen                                                        */
/* ------------------------------------------------------------------ */

#define TITLE_PAN_PER_STEP 1.2f /* world units per 60 Hz step: 72/s, 18 px/s on screen */

static bool s_title;
static bool s_title_tap = true; /* a tap on the picture starts (no menu) */

void game_title_set_tap_start(bool on) { s_title_tap = on; }

void game_title_begin(void)
{
    game_goto_level(rand() % RR_LEVEL_COUNT);
    s_title = true;
}

bool game_title_active(void) { return s_title; }

void game_play_level(int32_t level)
{
    s_title = false;
    game_goto_level(level);
}

static void title_end(void)
{
    s_title = false;
    game_goto_level(0);
}

/* The glide: along the level at a walking pace, the height following the
 * ground ahead, and back to the start once the finish is in view. */
static void title_camera_step(void)
{
    float max_x = s_terrain.finish_line + 500.0f - s_view_w;
    s_camera_x += TITLE_PAN_PER_STEP;
    if(s_camera_x > max_x) s_camera_x = 0;
    float target_y = rr_terrain_height_at(&s_terrain, s_camera_x + s_view_w * 0.5f) - s_view_h * 0.6f;
    if(target_y < 0) target_y = 0;
    if(target_y > 200.0f) target_y = 200.0f;
    s_camera_y += (target_y - s_camera_y) * 0.02f;
}

static void start_game_if_needed(void)
{
    if(!s_game_started) {
        s_game_started = true;
        s_timer_running = true;
        set_random_message(k_start_messages, ARRAY_LEN(k_start_messages));
    }
}

/* pushRob(power) */
static void push_rob(float power)
{
    start_game_if_needed();
    if(s_rob.jumps_used < 2) {
        s_rob.vx += 4.0f + power * 4.0f;
        s_rob.vy += -3.0f - power * 5.0f;
        if(s_rob.vx > 25.0f) s_rob.vx = 25.0f;
        if(s_rob.vy < -20.0f) s_rob.vy = -20.0f;
        s_rob.jumps_used++;
    }
}

/* Can the run be restarted by a press right now? (dead, explosion done,
 * and the message has been on screen long enough to be read) */
static bool reset_allowed(void)
{
    return s_game_over && !s_game_won && s_anim_ms - s_game_over_at_ms >= RESET_LOCKOUT_MS;
}

/* Space bar: instant (double) jump, advance after a win, or - unlike the
 * original, where space is dead after a game over - restart the level. */
static void space_pressed(void)
{
    if(s_game_won && s_level < RR_LEVEL_COUNT - 1) {
        game_goto_level(s_level + 1);
        return;
    }
    if(reset_allowed()) {
        game_reset_level();
        return;
    }
    if(!s_game_over && !s_game_won) {
        start_game_if_needed();
        if(s_rob.jumps_used < 2) {
            s_rob.vx += 4.0f;
            s_rob.vy = -8.0f;
            s_rob.jumps_used++;
        }
    }
}

static inline bool in_btn(const rr_touch_btn_t * b, int32_t x, int32_t y)
{
    int32_t dx = x - b->cx, dy = y - b->cy;
    int32_t r = b->r + 6; /* a little slack for fingers */
    return dx * dx + dy * dy <= r * r;
}

static inline bool in_rect(const rr_touch_rect_t * r, int32_t x, int32_t y)
{
    const int32_t slack = 6;
    return x >= r->x - slack && x < r->x + r->w + slack && y >= r->y - slack && y < r->y + r->h + slack;
}

static touch_zone_t classify_touch(int32_t x, int32_t y, bool * l, bool * r, bool * u)
{
    *l = *r = *u = false;
    if(s_touch_controls) {
        rr_touch_btn_t bl, br, bu;
        rr_touch_rect_t bj;
        rr_draw_touch_layout(s_screen_w, s_screen_h, &bl, &br, &bu, &bj);
        if(in_rect(&bj, x, y)) return ZONE_JUMP;
        if(in_btn(&bl, x, y)) { *l = true; return ZONE_BUTTON; }
        if(in_btn(&br, x, y)) { *r = true; return ZONE_BUTTON; }
        if(in_btn(&bu, x, y)) { *u = true; return ZONE_BUTTON; }
    }
    return ZONE_PLAYFIELD;
}

static inline bool is_launch_zone(touch_zone_t z) { return z == ZONE_PLAYFIELD; }

void game_input_touch(int32_t slot, bool pressed, int32_t x, int32_t y)
{
    if(slot < 0 || slot >= RR_TOUCH_SLOTS) return;
    if(s_title) {
        /* Start on release, so the finger that started is not taken for a
         * held jump in the level. */
        static bool down;
        if(!pressed && down && s_title_tap) title_end();
        down = pressed;
        return;
    }
    bool l, r, u;

    if(pressed) {
        if(!s_touch[slot].active) {
            /* touch start: like the original's mousedown */
            s_touch[slot].active = true;
            s_touch[slot].held_ms = 0;
            s_touch[slot].zone = classify_touch(x, y, &l, &r, &u);
            if(s_touch[slot].zone == ZONE_JUMP) space_pressed(); /* as the space bar, on press */
            if(is_launch_zone(s_touch[slot].zone) && s_game_over && !s_game_won) {
                /* original: mousedown ignored when gameOver; here a tap
                 * restarts the level once the death message had its moment */
                s_touch[slot].zone = reset_allowed() ? ZONE_RESET : ZONE_NONE;
            }
        }
        s_touch[slot].x = x;
        s_touch[slot].y = y;
        return;
    }

    if(!s_touch[slot].active) return;
    /* touch end: like the original's mouseup */
    if(s_touch[slot].zone == ZONE_RESET) {
        if(reset_allowed()) game_reset_level();
    }
    else if(is_launch_zone(s_touch[slot].zone)) {
        if(s_game_won) {
            if(s_level < RR_LEVEL_COUNT - 1) game_goto_level(s_level + 1);
        }
        else if(!s_game_over) {
            if(s_touch[slot].held_ms < TAP_MAX_MS) {
                space_pressed(); /* tap = the original's instant space jump */
            }
            else {
                float power = s_touch[slot].held_ms / PUSH_CHARGE_MS;
                if(power > 1.0f) power = 1.0f;
                push_rob(power);
            }
        }
    }
    s_touch[slot].active = false;
    s_touch[slot].zone = ZONE_NONE;
}

void game_input_key(rr_key_t key, bool pressed)
{
    switch(key) {
        case RR_KEY_LEFT:  s_keys.left = pressed;  break;
        case RR_KEY_RIGHT: s_keys.right = pressed; break;
        case RR_KEY_UP:    s_keys.up = pressed;    break;
        case RR_KEY_DOWN:  s_keys.down = pressed;  break;
        case RR_KEY_SPACE:
            if(s_title) {
                if(pressed && !s_space_down) title_end();
                s_space_down = pressed; /* set after title_end, so holding on is no jump */
                break;
            }
            if(pressed && !s_space_down) space_pressed();
            s_space_down = pressed;
            break;
        case RR_KEY_ART: {
            /* Keys arrive as their state on every poll: act on the press. */
            static bool down;
            if(pressed && !down) {
                /* original -> painted -> rendered (when the set is there) -> original */
                if(!rr_art_enabled()) {
                    rr_art_set_enabled(true);
                    rr_art_set_rendered(false);
                }
                else if(!rr_art_rendered() && rr_art_rendered_available()) rr_art_set_rendered(true);
                else {
                    rr_art_set_enabled(false);
                    rr_art_set_rendered(false);
                }
                times_save();
            }
            down = pressed;
            break;
        }
        case RR_KEY_MUSIC: {
            static bool down;
            if(pressed && !down) game_set_music_enabled(!s_music_on);
            down = pressed;
            break;
        }
        case RR_KEY_NEXT_TRACK: {
            static bool down;
            if(pressed && !down) s_music_skip = 1;
            down = pressed;
            break;
        }
        case RR_KEY_FPS: {
            static bool down;
            if(pressed && !down) game_set_fps_enabled(!s_fps_on);
            down = pressed;
            break;
        }
    }
}

static bool s_jump_btn_down;

/* Recompute the on-screen button state from the live touch slots. */
static void update_touch_keys(uint32_t dt_ms)
{
    memset(&s_touch_keys, 0, sizeof(s_touch_keys));
    s_jump_btn_down = false;
    for(int32_t i = 0; i < RR_TOUCH_SLOTS; i++) {
        if(!s_touch[i].active) continue;
        s_touch[i].held_ms += (float)dt_ms;
        if(s_touch[i].zone == ZONE_BUTTON) {
            bool l, r, u;
            classify_touch(s_touch[i].x, s_touch[i].y, &l, &r, &u);
            s_touch_keys.left |= l;
            s_touch_keys.right |= r;
            s_touch_keys.up |= u;
        }
        else if(s_touch[i].zone == ZONE_JUMP) {
            s_jump_btn_down = true;
        }
    }
}

/* A launch is being charged once a playfield touch outlives a tap. */
static bool charging(float * power)
{
    for(int32_t i = 0; i < RR_TOUCH_SLOTS; i++) {
        if(s_touch[i].active && is_launch_zone(s_touch[i].zone) && s_touch[i].held_ms >= TAP_MAX_MS) {
            float p = s_touch[i].held_ms / PUSH_CHARGE_MS;
            *power = p > 1.0f ? 1.0f : p;
            return true;
        }
    }
    *power = 0;
    return false;
}

/* ------------------------------------------------------------------ */
/* updatePhysics()                                                     */
/* ------------------------------------------------------------------ */

static void physics_step(void)
{
    if(s_game_over && !explosion_active() && !s_game_won) return;

    if(s_game_started && !s_game_over && !s_game_won) {
        if(s_timer_running) s_level_steps++;
        if(s_keys.left || s_touch_keys.left)   s_rob.vx -= ARROW_ACCEL_X;
        if(s_keys.right || s_touch_keys.right) s_rob.vx += ARROW_ACCEL_X;
        if(s_keys.up || s_touch_keys.up)       s_rob.vy -= ARROW_ACCEL_Y;
        if(s_keys.down || s_touch_keys.down)   s_rob.vy += ARROW_ACCEL_Y;

        s_rob.vy += ROB_GRAVITY;
        s_rob.vx *= ROB_FRICTION;
        s_rob.x += s_rob.vx;
        s_rob.y += s_rob.vy;
        s_rob.rotation += s_rob.vx * 0.05f;

        s_rob.on_ground = false;
        float terrain_h = rr_terrain_height_at(&s_terrain, s_rob.x);
        if(s_rob.y + (float)s_rob.radius > terrain_h) {
            s_rob.y = terrain_h - (float)s_rob.radius;
            s_rob.vy *= -ROB_BOUNCE;
            if(fabsf(s_rob.vy) < 0.5f) s_rob.vy = 0;
            s_rob.on_ground = true;
            s_rob.jumps_used = 0;
            s_rob.vx += rr_terrain_slope_at(&s_terrain, s_rob.x) * 0.5f;
        }

        rr_collision_result_t coll = rr_entities_collide(&s_rob.x, &s_rob.y, &s_rob.vx, &s_rob.vy, s_rob.radius);

        if(coll.abducted) set_random_message(k_abduct_messages, ARRAY_LEN(k_abduct_messages));
        if(coll.checkpoint_hit) {
            set_random_message_fmt(k_checkpoint_messages, ARRAY_LEN(k_checkpoint_messages), rr_last_checkpoint + 1);
        }

        if(coll.death != RR_DEATH_NONE) {
            s_game_over = true;
            s_game_over_at_ms = s_anim_ms;
            create_explosion(s_rob.x, s_rob.y);
            switch(coll.death) {
                case RR_DEATH_FIREBALL:
                    create_explosion(coll.death_x, coll.death_y);
                    set_random_message(k_fire_messages, ARRAY_LEN(k_fire_messages));
                    break;
                case RR_DEATH_JULIAN:
                    set_random_message(k_julian_messages, ARRAY_LEN(k_julian_messages));
                    break;
                case RR_DEATH_MINE:
                    create_explosion(coll.death_x, coll.death_y);
                    set_random_message(k_mine_messages, ARRAY_LEN(k_mine_messages));
                    break;
                default: break;
            }
            return; /* the original returns before updating saucers this frame */
        }

        int32_t d = (int32_t)floorf((s_rob.x - 200.0f) / 10.0f);
        if(d > s_distance_m) s_distance_m = d;

        if(s_rob.x >= s_terrain.finish_line && !s_game_won) {
            s_game_won = true;
            s_game_over = true;
            timer_finish();
            create_confetti();
            s_auto_advance_steps = 0;
            if(s_level >= RR_LEVEL_COUNT - 1) {
                set_message("Holy f*cking sh*t! You beat ALL the levels! You're a goddamn legend!");
                s_show_next_button = true;
            }
        }

        if(s_rob.y > WORLD_FALL_LIMIT && !s_game_won) {
            s_game_over = true;
            s_game_over_at_ms = s_anim_ms;
            set_random_message(k_fall_messages, ARRAY_LEN(k_fall_messages));
        }
    }

    if(s_game_won && s_level < RR_LEVEL_COUNT - 1) {
        s_auto_advance_steps++;
        if(s_auto_advance_steps <= AUTO_ADVANCE_STEPS) {
            int32_t seconds_left = 5 - s_auto_advance_steps / 60;
            char base[160];
            snprintf(base, sizeof(base), k_win_messages[(s_auto_advance_steps / 60) % ARRAY_LEN(k_win_messages)], s_level + 1);
            if(seconds_left > 0) snprintf(s_message, sizeof(s_message), "%s Next in %d...", base, seconds_left);
            else snprintf(s_message, sizeof(s_message), "%s Loading next level...", base);
        }
        if(s_auto_advance_steps == AUTO_ADVANCE_STEPS) {
            game_goto_level(s_level + 1);
            return;
        }
    }

    rr_entities_step(&s_terrain, s_game_started, s_game_started && !s_game_over, s_rob.x, s_rob.y);
    update_explosion();
}

/* gameLoop()'s per-frame celebration part */
static void celebration_step(void)
{
    if(!s_game_won) return;
    update_confetti();
    update_fireworks();
    s_firework_timer++;
    if(s_firework_timer % 20 == 0) {
        create_firework(s_camera_x + randf() * s_view_w, randf() * s_view_h * 0.5f);
    }
}

static void update_camera(void)
{
    float target_x = s_rob.x - s_view_w / 3.0f;
    if(target_x < 0) target_x = 0;
    s_camera_x += (target_x - s_camera_x) * 0.1f;

    float target_y = s_rob.y - s_view_h / 2.0f;
    if(target_y < 0) target_y = 0;
    s_camera_y += (target_y - s_camera_y) * 0.08f;

    float max_camera_x = s_terrain.finish_line + 500.0f - s_view_w;
    if(max_camera_x < 0) max_camera_x = 0;
    if(s_camera_x > max_camera_x) s_camera_x = max_camera_x;

    if(s_camera_y < 0) s_camera_y = 0;
    if(s_camera_y > 200.0f) s_camera_y = 200.0f;
}

void game_update(uint32_t dt_ms)
{
    static float accumulator_ms = 0;
    if(dt_ms > 100) dt_ms = 100; /* don't spiral after a stall */
    accumulator_ms += (float)dt_ms;
    update_touch_keys(dt_ms);

    while(accumulator_ms >= FIXED_STEP_MS) {
        physics_step();
        celebration_step();
        if(s_title) title_camera_step();
        accumulator_ms -= FIXED_STEP_MS;
    }

    s_anim_ms += (float)dt_ms;
    if(!s_title) update_camera();
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

/* Game text: never smaller than 10 px at 480x272 (world text would be 4-5),
 * the same share of the picture at the larger scales. */
static const lv_font_t * font_for_px(float px)
{
    float min_px = 10.0f * rr_ui_scale();
    return rr_ui_font(px < min_px ? min_px : px);
}

static void draw_queued_text(lv_obj_t * canvas)
{
    if(gfx_text_count == 0) return;
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    for(int32_t i = 0; i < gfx_text_count; i++) {
        const gfx_text_t * t = &gfx_text_queue[i];
        const lv_font_t * font = font_for_px(t->size_px);

        lv_point_t size;
        lv_text_get_size(&size, t->text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int32_t x = (int32_t)t->x - (t->align == 1 ? size.x / 2 : 0);
        int32_t y = (int32_t)t->y - lv_font_get_line_height(font); /* fillText's y is the baseline */

        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.font = font;
        dsc.text = t->text;
        dsc.opa = (lv_opa_t)(t->color >> 24);

        if(t->outline) {
            dsc.color = lv_color_hex(t->outline & 0xFFFFFF);
            static const int8_t ofs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
            for(int k = 0; k < 4; k++) {
                lv_area_t a = { x + ofs[k][0], y + ofs[k][1], x + ofs[k][0] + size.x, y + ofs[k][1] + size.y };
                lv_draw_label(&layer, &dsc, &a);
            }
        }
        dsc.color = lv_color_hex(t->color & 0xFFFFFF);
        lv_area_t a = { x, y, x + size.x, y + size.y };
        lv_draw_label(&layer, &dsc, &a);
    }
    lv_canvas_finish_layer(canvas, &layer);
}

/* ------------------------------------------------------------------ */
/* Text in band mode                                                   */
/* ------------------------------------------------------------------ */

/* Bands never see a whole frame, so LVGL cannot draw the game's text onto
 * it afterwards. Each distinct label (text, font, colours, alignment) is
 * drawn once by LVGL onto a transparent buffer, exactly as draw_queued_text
 * would place it, and the bands blend that image in. The set is small and
 * fixed (FINISH, CP1..5, JULIAN, JUMP, JUMP POWER). A label first seen during
 * a frame is made before the next one: LVGL may only run on this thread. */
typedef struct {
    char text[24];
    const lv_font_t * font;
    gfx_color_t color, outline;
    int align;
    int dx, dy, w, h; /* image origin relative to the queued (x, y) */
    uint32_t * pm;    /* premultiplied ARGB */
} text_sprite_t;

#define TEXT_SPRITES 48
static text_sprite_t s_ts[TEXT_SPRITES];
static int s_ts_n; /* written by the main thread between frames only */
static gfx_text_t s_ts_miss[8];
static int s_ts_miss_n;
static pthread_mutex_t s_ts_mx = PTHREAD_MUTEX_INITIALIZER;

static bool ts_match(const text_sprite_t * s, const gfx_text_t * t, const lv_font_t * font)
{
    return s->font == font && s->color == t->color && s->outline == t->outline && s->align == t->align &&
           strcmp(s->text, t->text) == 0;
}

static void ts_make(const gfx_text_t * t)
{
    static lv_obj_t * canvas;
    if(s_ts_n >= TEXT_SPRITES) return;
    const lv_font_t * font = font_for_px(t->size_px);
    for(int i = 0; i < s_ts_n; i++) if(ts_match(&s_ts[i], t, font)) return;
    lv_point_t size;
    lv_text_get_size(&size, t->text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    /* one pixel all round for the outline, and lv_area_t is inclusive */
    int w = size.x + 3, h = size.y + 3;
    lv_draw_buf_t * buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_ARGB8888, 0);
    uint32_t * pm = malloc((size_t)w * h * 4);
    if(buf == NULL || pm == NULL) {
        if(buf) lv_draw_buf_destroy(buf);
        free(pm);
        return;
    }
    lv_draw_buf_clear(buf, NULL);
    if(canvas == NULL) {
        canvas = lv_canvas_create(lv_layer_top());
        lv_obj_set_hidden(canvas, true);
    }
    lv_canvas_set_draw_buf(canvas, buf);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = font;
    dsc.text = t->text;
    dsc.opa = (lv_opa_t)(t->color >> 24);
    if(t->outline) {
        dsc.color = lv_color_hex(t->outline & 0xFFFFFF);
        static const int8_t ofs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        for(int k = 0; k < 4; k++) {
            lv_area_t a = { 1 + ofs[k][0], 1 + ofs[k][1], 1 + ofs[k][0] + size.x, 1 + ofs[k][1] + size.y };
            lv_draw_label(&layer, &dsc, &a);
        }
    }
    dsc.color = lv_color_hex(t->color & 0xFFFFFF);
    lv_area_t a = { 1, 1, 1 + size.x, 1 + size.y };
    lv_draw_label(&layer, &dsc, &a);
    lv_canvas_finish_layer(canvas, &layer);
    for(int y = 0; y < h; y++) {
        const uint32_t * src = (const uint32_t *)(buf->data + (size_t)y * buf->header.stride);
        for(int x = 0; x < w; x++) {
            uint32_t c = src[x], al = c >> 24;
            pm[y * w + x] = al << 24 | (((c >> 16) & 0xFF) * al / 255) << 16 | (((c >> 8) & 0xFF) * al / 255) << 8 |
                            ((c & 0xFF) * al / 255);
        }
    }
    lv_canvas_set_draw_buf(canvas, NULL);
    lv_draw_buf_destroy(buf);

    text_sprite_t * ts = &s_ts[s_ts_n];
    snprintf(ts->text, sizeof(ts->text), "%s", t->text);
    ts->font = font;
    ts->color = t->color;
    ts->outline = t->outline;
    ts->align = t->align;
    ts->dx = -(t->align == 1 ? size.x / 2 : 0) - 1;
    ts->dy = -lv_font_get_line_height(font) - 1;
    ts->w = w;
    ts->h = h;
    ts->pm = pm;
    s_ts_n++;
}

/* Runs on the band threads: the cache is only read here. */
static void band_text(gfx_ctx_t * g, const gfx_text_t * items, int n)
{
    for(int i = 0; i < n; i++) {
        const gfx_text_t * t = &items[i];
        const lv_font_t * font = font_for_px(t->size_px);
        const text_sprite_t * ts = NULL;
        for(int k = 0; k < s_ts_n; k++) {
            if(ts_match(&s_ts[k], t, font)) {
                ts = &s_ts[k];
                break;
            }
        }
        if(ts) {
            gfx_blit_pm(g, (int32_t)t->x + ts->dx, (int32_t)t->y + ts->dy, ts->w, ts->h, ts->pm);
            continue;
        }
        pthread_mutex_lock(&s_ts_mx);
        bool known = false;
        for(int k = 0; k < s_ts_miss_n; k++) known |= strcmp(s_ts_miss[k].text, t->text) == 0 &&
                                                        s_ts_miss[k].color == t->color && s_ts_miss[k].outline == t->outline;
        if(!known && s_ts_miss_n < (int)(sizeof(s_ts_miss) / sizeof(s_ts_miss[0]))) s_ts_miss[s_ts_miss_n++] = *t;
        pthread_mutex_unlock(&s_ts_mx);
    }
}

static void fill_view(rr_view_t * out);

bool game_render_bands_missed(void) { return s_ts_miss_n > 0; }

static game_renderer_t s_renderer;
static const rr_view_t * s_overlay_view; /* while s_renderer draws */

void game_set_renderer(game_renderer_t r) { s_renderer = r; }

void game_overlay_band(uint32_t * band, int stride_px, int y0, int rows, int w, int h)
{
    if(s_overlay_view == NULL) return;
    gfx_ctx_t g;
    gfx_text_t text[GFX_TEXT_MAX];
    gfx_begin(&g, band - (ptrdiff_t)y0 * stride_px, w, h, stride_px);
    gfx_set_band(&g, y0, y0 + rows);
    g.text_q = text;
    g.text_max = GFX_TEXT_MAX;
    rr_draw_frame(&g, s_overlay_view, s_scale); /* rr_draw_layers is the overlay alone */
    if(g.text_n) band_text(&g, text, g.text_n);
}

void game_render_bands(int w, int h, int band_rows, rr_band_sink_t sink, void * user)
{
    if(s_renderer) {
        for(int i = 0; i < s_ts_miss_n; i++) ts_make(&s_ts_miss[i]);
        s_ts_miss_n = 0;
        rr_view_t v;
        fill_view(&v);
        rr_draw_prepare(w, h);
        unsigned layers = rr_draw_layers;
        rr_draw_layers = RR_LAYER_OVERLAY;
        s_overlay_view = &v;
        s_renderer(w, h, &v, s_scale, sink, user);
        s_overlay_view = NULL;
        rr_draw_layers = layers;
        return;
    }
    for(int i = 0; i < s_ts_miss_n; i++) ts_make(&s_ts_miss[i]);
    s_ts_miss_n = 0;
    rr_view_t v;
    fill_view(&v);
    rr_draw_frame_bands(w, h, &v, s_scale, band_rows, sink, band_text, user);
}

/* For comparing band rendering with the canvas path (ROLLING_ROB_BANDS). */
static void canvas_sink(uint32_t * band, int stride_px, int y0, int rows, void * user)
{
    lv_draw_buf_t * buf = user;
    for(int r = 0; r < rows; r++)
        memcpy(buf->data + (size_t)(y0 + r) * buf->header.stride, band + (size_t)r * stride_px, (size_t)buf->header.w * 4);
}

void game_render(lv_obj_t * canvas)
{
    lv_draw_buf_t * buf = lv_canvas_get_draw_buf(canvas);
    if(buf == NULL) return;

    static int bands = -1;
    if(bands < 0) bands = getenv("ROLLING_ROB_BANDS") ? atoi(getenv("ROLLING_ROB_BANDS")) : 0;
    if(bands > 0 || s_renderer) {
        game_render_bands((int)buf->header.w, (int)buf->header.h, bands, canvas_sink, buf);
        lv_obj_invalidate(canvas);
        return;
    }

    rr_view_t v;
    fill_view(&v);
    rr_draw_frame_parallel((uint32_t *)buf->data, (int)buf->header.w, (int)buf->header.h,
                           (int)(buf->header.stride / 4), &v, s_scale);
    RR_PROF(RR_PROF_TEXT, draw_queued_text(canvas));
    lv_obj_invalidate(canvas);
}


static void fill_view(rr_view_t * out)
{
    rr_view_t v;
    memset(&v, 0, sizeof(v));
    v.level = s_level;
    v.terrain = &s_terrain;
    v.rob.x = s_rob.x;
    v.rob.y = s_rob.y;
    v.rob.vx = s_rob.vx;
    v.rob.vy = s_rob.vy;
    v.rob.rotation = s_rob.rotation;
    v.rob.radius = s_rob.radius;
    v.camera_x = s_camera_x;
    v.camera_y = s_camera_y;
    v.started = s_game_started;
    v.over = s_game_over;
    v.won = s_game_won;
    v.rob_visible = !s_game_over || explosion_active() || s_game_won;
    v.anim_ms = s_anim_ms;
    v.charging = charging(&v.charge_power);
    v.touch_controls = s_touch_controls && !s_title;
    v.btn_left = s_touch_keys.left || s_keys.left;
    v.btn_right = s_touch_keys.right || s_keys.right;
    v.btn_up = s_touch_keys.up || s_keys.up;
    v.btn_jump = s_jump_btn_down || s_space_down;
    v.confetti = s_confetti;
    v.fireworks = s_fireworks;
    v.explosion = s_explosion;
    *out = v;
}

/* ------------------------------------------------------------------ */
/* HUD getters                                                         */
/* ------------------------------------------------------------------ */

int32_t game_get_level(void) { return s_level; }
int32_t game_get_level_count(void) { return RR_LEVEL_COUNT; }
const char * game_get_level_name(int32_t level) { return rr_levels[level].name; }
int32_t game_get_distance_m(void) { return s_distance_m; }
int32_t game_get_goal_m(void) { return (int32_t)floorf(s_terrain.finish_line / 10.0f); }
const char * game_get_message(void) { return s_message; }
bool game_is_over(void) { return s_game_over; }
bool game_is_won(void) { return s_game_won; }
bool game_should_show_next_button(void) { return s_show_next_button; }
