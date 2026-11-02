#include "bot.h"
#include "game.h"
#include "entities.h"
#include "level.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Strategy (exploits the same physics a human can):
 *  - charge a full-power launch (hold the playfield 32 frames), then keep
 *    RIGHT + UP held: UP cancels 0.3 of the 0.4 gravity, so Rob glides
 *  - spend the second jump when sinking towards the ground
 *  - with both jumps gone, predict the landing spot; if a mine, Julian or
 *    monster sits there, hold LEFT to land short of it (or keep RIGHT to
 *    fly past it)
 *  - the moment Rob touches down (jumps reset) hop again
 *  - after the finish, tap Space (next level); after a death, wait for the
 *    reset lockout and tap Space (restart), counting the death
 */

#define GRAVITY_UP   0.1f   /* 0.4 gravity - 0.3 ArrowUp */
#define FRICTION     0.98f
#define ACCEL        0.5f
#define MAX_PREDICT  600

static bot_mode_t s_mode;
static int32_t s_max_jumps = 2; /* 1 in BOT_MODE_SINGLE */
static int32_t s_until_level;
static int32_t s_deaths[RR_LEVEL_COUNT];
static int32_t s_times_ms[RR_LEVEL_COUNT];
static int32_t s_cur_level;
static int32_t s_charge_frames;
static int32_t s_won_frames;
static int32_t s_over_frames;
static int32_t s_space_release_at; /* frame at which to release Space (edge-trigger) */
static bool s_done;
static bool s_reported_win;
static bool s_stalled[RR_LEVEL_COUNT];
static float s_best_x;
static int32_t s_stall_frames;
#define STALL_LIMIT 900 /* 15 s without gaining ground = stuck */

static void press_space(int32_t frame)
{
    game_input_key(RR_KEY_SPACE, true);
    s_space_release_at = frame + 2;
}

/* Where would Rob touch down if RIGHT + UP stay held and no jump is used? */
static float predict_landing_x(const rr_bot_view_t * v)
{
    float x = v->x, y = v->y, vx = v->vx, vy = v->vy;
    for(int32_t i = 0; i < MAX_PREDICT; i++) {
        vy += GRAVITY_UP;
        vx = vx * FRICTION + ACCEL;
        x += vx;
        y += vy;
        if(y + (float)v->radius >= game_terrain_height(x)) return x;
    }
    return x;
}

/* Returns the x of the nearest lethal/bouncy thing around a landing spot, or -1. */
static float hazard_near(float lx)
{
    float best = -1, best_d = 1e9f;
    for(int32_t i = 0; i < rr_mine_count; i++) {
        float d = fabsf(rr_mines[i].x - lx);
        if(d < 90.0f && d < best_d) { best = rr_mines[i].x; best_d = d; }
    }
    for(int32_t i = 0; i < rr_julian_count; i++) {
        float cx = rr_julians[i].x + RR_JULIAN_W / 2;
        float d = fabsf(cx - lx);
        if(d < 320.0f && d < best_d) { best = cx; best_d = d; }
    }
    for(int32_t i = 0; i < rr_obstacle_count; i++) {
        float cx = rr_obstacles[i].x + rr_obstacles[i].w / 2;
        float d = fabsf(cx - lx);
        if(d < 80.0f && d < best_d) { best = cx; best_d = d; }
    }
    return best;
}

/* A Julian less than 320 ahead while Rob is low: fireballs live in the
 * bottom ~100 units and Julian himself kills within 80 of his centre. */
static bool julian_ahead_low(const rr_bot_view_t * v, float h)
{
    if(h > 200.0f) return false;
    for(int32_t i = 0; i < rr_julian_count; i++) {
        float cx = rr_julians[i].x + RR_JULIAN_W / 2;
        if(cx > v->x - 60.0f && cx < v->x + 320.0f) return true;
    }
    return false;
}

static bool fireball_close(const rr_bot_view_t * v)
{
    for(int32_t i = 0; i < rr_fireball_count; i++) {
        float dx = rr_fireballs[i].x - v->x, dy = rr_fireballs[i].y - v->y;
        if(dx * dx + dy * dy < 160.0f * 160.0f) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* No-UP planner: simulate the real physics for candidate jump schedules */
/* ------------------------------------------------------------------ */

#define PLAN_HORIZON_MAX 260
/* Planner tuning, overridable via env for offline sweeps (BOT_HORIZON,
 * BOT_VXW, BOT_BRAKEP). Defaults are the values that won the sweep. */
static int32_t s_horizon = 150;
static float s_vx_weight = 0.0f;
static float s_brake_penalty = 0.0f;
static float s_bounce_cost = 400.0f;
static float s_beam_cost = 0.0f;

static void tune_from_env(void)
{
    const char * e;
    if((e = getenv("BOT_HORIZON"))) s_horizon = atoi(e);
    if(s_horizon < 40) s_horizon = 40;
    if(s_horizon > PLAN_HORIZON_MAX) s_horizon = PLAN_HORIZON_MAX;
    if((e = getenv("BOT_VXW"))) s_vx_weight = (float)atof(e);
    if((e = getenv("BOT_BRAKEP"))) s_brake_penalty = (float)atof(e);
    if((e = getenv("BOT_BOUNCE"))) s_bounce_cost = (float)atof(e);
    if((e = getenv("BOT_BEAM"))) s_beam_cost = (float)atof(e);
}
#define MAX_SIM_FB   48
#define MAX_SIM_ENT  64

typedef struct { float x, y, vx, vy; int32_t jumps; bool on_ground; } sim_rob_t;
typedef struct { float x, y, vx, vy; bool alive; } sim_fb_t;
typedef struct { float x, y, vy, jump_timer, fire_timer; } sim_julian_t;

/* Entities within reach of the plan, gathered once per frame. */
typedef struct {
    float mine_x[MAX_SIM_ENT], mine_y[MAX_SIM_ENT]; int32_t n_mine;
    float obs_x[MAX_SIM_ENT], obs_y[MAX_SIM_ENT], obs_w[MAX_SIM_ENT], obs_h[MAX_SIM_ENT]; int32_t n_obs;
    float sc_x[MAX_SIM_ENT], sc_y[MAX_SIM_ENT]; int32_t n_sc;
    sim_julian_t jul[16]; int32_t n_jul;
    sim_fb_t fb[MAX_SIM_FB]; int32_t n_fb;
} sim_world_t;

static void world_gather(sim_world_t * w, float x0, float x1)
{
    w->n_mine = w->n_obs = w->n_sc = w->n_jul = w->n_fb = 0;
    for(int32_t i = 0; i < rr_mine_count && w->n_mine < MAX_SIM_ENT; i++) {
        if(rr_mines[i].x < x0 || rr_mines[i].x > x1) continue;
        w->mine_x[w->n_mine] = rr_mines[i].x; w->mine_y[w->n_mine] = rr_mines[i].y; w->n_mine++;
    }
    for(int32_t i = 0; i < rr_obstacle_count && w->n_obs < MAX_SIM_ENT; i++) {
        if(rr_obstacles[i].x + 60 < x0 || rr_obstacles[i].x > x1) continue;
        w->obs_x[w->n_obs] = rr_obstacles[i].x; w->obs_y[w->n_obs] = rr_obstacles[i].y;
        w->obs_w[w->n_obs] = rr_obstacles[i].w; w->obs_h[w->n_obs] = rr_obstacles[i].h; w->n_obs++;
    }
    for(int32_t i = 0; i < rr_saucer_count && w->n_sc < MAX_SIM_ENT; i++) {
        if(rr_saucers[i].x < x0 - 100 || rr_saucers[i].x > x1) continue;
        w->sc_x[w->n_sc] = rr_saucers[i].x; w->sc_y[w->n_sc] = rr_saucers[i].y; w->n_sc++;
    }
    for(int32_t i = 0; i < rr_julian_count && w->n_jul < 16; i++) {
        if(rr_julians[i].x + 100 < x0 - 200 || rr_julians[i].x > x1) continue;
        w->jul[w->n_jul] = (sim_julian_t) { rr_julians[i].x, rr_julians[i].y, rr_julians[i].vy,
                                            rr_julians[i].jump_timer, rr_julians[i].fire_timer };
        w->n_jul++;
    }
    for(int32_t i = 0; i < rr_fireball_count && w->n_fb < MAX_SIM_FB; i++) {
        if(rr_fireballs[i].x < x0 - 200 || rr_fireballs[i].x > x1) continue;
        w->fb[w->n_fb] = (sim_fb_t) { rr_fireballs[i].x, rr_fireballs[i].y, rr_fireballs[i].vx, rr_fireballs[i].vy, true };
        w->n_fb++;
    }
}

/* Julians hop every 120 frames and spit 3 fireballs every 90 frames
 * (updateJulians): both timers are known, so their future is exact. */
static void world_step(sim_world_t * w)
{
    for(int32_t i = 0; i < w->n_jul; i++) {
        sim_julian_t * j = &w->jul[i];
        j->jump_timer++;
        if(j->jump_timer >= 120 && j->vy == 0) { j->vy = -15; j->jump_timer = 0; }
        j->vy += 0.5f;
        j->y += j->vy;
        float gy = game_terrain_height(j->x + 50) - 120;
        if(j->y >= gy) { j->y = gy; j->vy = 0; }
        j->fire_timer++;
        if(j->fire_timer >= 90) {
            for(int32_t k = -1; k <= 1 && w->n_fb < MAX_SIM_FB; k++) {
                w->fb[w->n_fb++] = (sim_fb_t) { j->x, j->y + 40, -8.0f + k * 2.0f, -5.0f + k * 1.5f, true };
            }
            j->fire_timer = 0;
        }
    }
    for(int32_t i = 0; i < w->n_fb; i++) {
        sim_fb_t * f = &w->fb[i];
        if(!f->alive) continue;
        f->vy += 0.3f; f->x += f->vx; f->y += f->vy;
        if(f->y > 1080) f->alive = false;
    }
}

/* One physics step, same order as updatePhysics() (+ the space-bar jump
 * that the original applies on keydown before the step). */
static void sim_step(sim_rob_t * r, const sim_world_t * w, bool jump, bool left, bool right, bool down,
                     int32_t * bounces)
{
    if(jump && r->jumps < s_max_jumps) { r->vx += 4.0f; r->vy = -8.0f; r->jumps++; }
    if(left) r->vx -= 0.5f;
    if(right) r->vx += 0.5f;
    if(down) r->vy += 0.3f;
    r->vy += 0.4f;
    r->vx *= 0.98f;
    r->x += r->vx;
    r->y += r->vy;
    r->on_ground = false;
    float th = game_terrain_height(r->x);
    if(r->y + 30.0f > th) {
        r->y = th - 30.0f;
        r->vy *= -0.6f;
        if(fabsf(r->vy) < 0.5f) r->vy = 0;
        r->on_ground = true;
        r->jumps = 0;
        r->vx += game_terrain_slope(r->x) * 0.5f;
    }
    for(int32_t i = 0; i < w->n_obs; i++) {
        if(r->x + 30 > w->obs_x[i] && r->x - 30 < w->obs_x[i] + w->obs_w[i] &&
           r->y + 30 > w->obs_y[i] && r->y - 30 < w->obs_y[i] + w->obs_h[i]) {
            float dx = r->x - (w->obs_x[i] + w->obs_w[i] / 2), dy = r->y - (w->obs_y[i] + w->obs_h[i] / 2);
            float d = sqrtf(dx * dx + dy * dy);
            if(d > 0) { r->vx = dx / d * 8.0f; r->vy = dy / d * 8.0f - 2.0f; if(bounces) (*bounces)++; }
        }
    }
    for(int32_t i = 0; i < w->n_sc; i++) {
        if(r->x + 30 > w->sc_x[i] - 40 && r->x - 30 < w->sc_x[i] + 40 && r->y + 30 > w->sc_y[i] - 20 && r->y - 30 < w->sc_y[i] + 20) {
            float dx = r->x - w->sc_x[i], dy = r->y - w->sc_y[i];
            float d = sqrtf(dx * dx + dy * dy);
            if(d > 0) { r->vx = dx / d * 10.0f; r->vy = dy / d * 10.0f - 3.0f; if(bounces) (*bounces)++; }
        }
    }
}

static bool sim_dead(const sim_rob_t * r, const sim_world_t * w)
{
    if(r->y > 1130.0f) return true;
    for(int32_t i = 0; i < w->n_mine; i++) {
        float dx = r->x - w->mine_x[i], dy = r->y - w->mine_y[i];
        if(dx * dx + dy * dy < 53.0f * 53.0f) return true;
    }
    for(int32_t i = 0; i < w->n_jul; i++) {
        float dx = r->x - (w->jul[i].x + 50.0f), dy = r->y - (w->jul[i].y + 60.0f);
        if(dx * dx + dy * dy < 84.0f * 84.0f) return true;
    }
    for(int32_t i = 0; i < w->n_fb; i++) {
        if(!w->fb[i].alive) continue;
        float dx = r->x - w->fb[i].x, dy = r->y - w->fb[i].y;
        if(dx * dx + dy * dy < 49.0f * 49.0f) return true;
    }
    return false;
}

static sim_world_t s_world; /* gathered once per frame */

/* Score of one plan: jump at frame j1 and j2 (-1 = never), RIGHT held,
 * optionally LEFT for the first `brake` frames, DOWN after the last jump.
 *
 * Scoring is speed-aware, not just distance: ending the horizon at full
 * roll (vx ~24.5, the friction/accel equilibrium) is worth much more than
 * ending a few units further while crawling, because the next plan starts
 * from that velocity. Braking is charged for the time it costs. */
static float plan_score(const rr_bot_view_t * v, int32_t j1, int32_t j2, int32_t brake, bool down_after)
{
    sim_rob_t r = { v->x, v->y, v->vx, v->vy, v->jumps_used, v->on_ground };
    sim_world_t w = s_world;
    int32_t last_jump = j2 >= 0 ? j2 : j1;
    int32_t bounces = 0, beam_frames = 0;
    for(int32_t t = 0; t < s_horizon; t++) {
        bool jump = (t == j1 || t == j2);
        bool left = t < brake;
        bool down = down_after && last_jump >= 0 && t > last_jump + 8 && r.vy > -2.0f;
        sim_step(&r, &w, jump, left, !left, down, &bounces);
        world_step(&w);
        if(sim_dead(&r, &w)) return r.x - 20000.0f + (float)t * 10.0f; /* later death = slightly better */
        if(r.x >= v->finish_line) return 1e6f - (float)t - s_bounce_cost * (float)bounces;
        /* A saucer within 200 units charges its beam timer; a completed beam
         * teleports Rob 2000 units back, by far the worst thing that can
         * happen to a run, so staying out of range is worth real distance. */
        for(int32_t i = 0; i < w.n_sc; i++) {
            float dx = r.x - w.sc_x[i], dy = (r.y - 150.0f) - w.sc_y[i];
            if(dx * dx + dy * dy < 200.0f * 200.0f) { beam_frames++; break; }
        }
    }
    /* Knock-backs set vx to +-8/10 instead of adding to it, so each one
     * throws away the roll that takes ~50 frames to rebuild. */
    return r.x + s_vx_weight * r.vx
           - s_brake_penalty * (float)brake
           - s_bounce_cost * (float)bounces
           - s_beam_cost * (float)beam_frames;
}

typedef struct { int32_t j1, j2, brake; bool down; } plan_t;

static plan_t plan_best(const rr_bot_view_t * v)
{
    world_gather(&s_world, v->x - 150.0f, v->x + 4500.0f);
    /* Fine granularity for the near future (the jump frame the bot may act
     * on this tick), coarser further out where a replan will refine it. */
    static const int32_t j1s[] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                   18, 20, 22, 24, 26, 29, 32, 36, 40, 45, 50, 56, 62, 70 };
    static const int32_t gaps[] = { -1, 3, 6, 10, 14, 18, 22, 27, 32, 40 };
    static const int32_t brakes[] = { 0, 5, 12, 20, 30, 45 };
    plan_t best = { -1, -1, 0, false };
    float best_s = -1e9f;
    for(size_t a = 0; a < sizeof(j1s) / sizeof(j1s[0]); a++) {
        for(size_t g = 0; g < sizeof(gaps) / sizeof(gaps[0]); g++) {
            if(j1s[a] < 0 && gaps[g] >= 0) continue;
            if(s_max_jumps < 2 && gaps[g] >= 0) continue; /* no second jump in single mode */
            int32_t j2 = (j1s[a] >= 0 && gaps[g] >= 0) ? j1s[a] + gaps[g] : -1;
            for(int32_t d = 0; d < 2; d++) {
                for(size_t b = 0; b < sizeof(brakes) / sizeof(brakes[0]); b++) {
                    if(brakes[b] > 0 && j1s[a] >= 0 && j1s[a] < brakes[b]) continue;
                    float sc = plan_score(v, j1s[a], j2, brakes[b], d == 1);
                    if(sc > best_s) { best_s = sc; best = (plan_t) { j1s[a], j2, brakes[b], d == 1 }; }
                }
            }
        }
    }
    return best;
}

void bot_init(int32_t until_level, bot_mode_t mode)
{
    s_mode = mode;
    s_max_jumps = (mode == BOT_MODE_SINGLE) ? 1 : 2;
    tune_from_env();
    s_until_level = until_level;
    s_cur_level = game_get_level();
    for(int32_t i = 0; i < RR_LEVEL_COUNT; i++) { s_deaths[i] = 0; s_times_ms[i] = 0; }
    s_charge_frames = s_won_frames = s_over_frames = 0;
    s_space_release_at = -1;
    s_done = false;
    s_reported_win = false;
    for(int32_t i = 0; i < RR_LEVEL_COUNT; i++) s_stalled[i] = false;
    s_best_x = 0;
    s_stall_frames = 0;
}

bool bot_step(int32_t frame)
{
    if(s_done) return false;
    if(s_space_release_at >= 0 && frame >= s_space_release_at) {
        game_input_key(RR_KEY_SPACE, false);
        s_space_release_at = -1;
    }

    rr_bot_view_t v;
    game_get_bot_view(&v);
    int32_t level = game_get_level();
    if(level != s_cur_level) {
        s_cur_level = level;
        s_charge_frames = s_won_frames = s_over_frames = 0;
        s_reported_win = false;
        s_best_x = 0;
        s_stall_frames = 0;
    }

    /* Stall watchdog: if the planner stops making forward progress the run
     * is not "fast", it is stuck - mark the level so the report says so. */
    if(v.started && !v.over && !v.won) {
        if(v.x > s_best_x + 1.0f) { s_best_x = v.x; s_stall_frames = 0; }
        else if(++s_stall_frames > STALL_LIMIT) {
            if(!s_stalled[level]) {
                s_stalled[level] = true;
                fprintf(stderr, "bot: STALLED on level %d at x=%.0f (frame %d)\n",
                        (int)level + 1, (double)v.x, (int)frame);
            }
        }
    }
    else if(v.over) {
        s_best_x = 0;
        s_stall_frames = 0;
    }

    bool left = false, right = false, up = false;

    if(v.won) {
        if(!s_reported_win) {
            s_times_ms[level] = (int32_t)v.level_time_ms;
            if(getenv("BOT_DEBUG")) fprintf(stderr, "win level %d at frame %d: time %d ms\n", (int)level + 1, (int)frame, (int)v.level_time_ms);
            s_reported_win = true;
            if(level + 1 >= s_until_level) {
                s_done = true;
            }
        }
        s_won_frames++;
        if(!s_done && s_won_frames == 12) press_space(frame); /* next level */
    }
    else if(v.over) {
        if(s_over_frames == 0) s_deaths[level]++;
        s_over_frames++;
        if(s_over_frames == 50) press_space(frame); /* restart after the lockout */
        if(s_over_frames > 60) s_over_frames = 0;   /* in case the press was swallowed */
    }
    else if(!v.started) {
        /* full-power launch: hold the playfield for > 480 ms */
        s_charge_frames++;
        game_input_touch(4, s_charge_frames <= 32, 240, 100);
        if(s_charge_frames > 40) s_charge_frames = 0;
    }
    else if(s_mode == BOT_MODE_NOUP || s_mode == BOT_MODE_SINGLE) {
        plan_t p = plan_best(&v);
        bool down = false;
        if(p.brake > 0) { left = true; right = false; }
        else right = true;
        if(p.j1 == 0 && v.jumps_used < s_max_jumps) press_space(frame);
        if(p.down && p.j1 != 0 && !v.on_ground && v.vy > -2.0f && v.jumps_used >= 1) down = true;
        game_input_key(RR_KEY_DOWN, down);
    }
    else {
        right = true;
        up = true;
        float h = game_terrain_height(v.x) - (v.y + (float)v.radius);

        if(v.on_ground) {
            press_space(frame); /* hop straight away: never roll into things */
        }
        else if(v.jumps_used < 2 && v.vy > 0 && h < 220.0f) {
            press_space(frame); /* second jump while sinking */
        }
        else if(v.jumps_used < 2 && (fireball_close(&v) || julian_ahead_low(&v, h))) {
            press_space(frame);
        }
        else if(v.jumps_used >= 2 && v.vy > 0) {
            float lx = predict_landing_x(&v);
            float hz = hazard_near(lx);
            if(hz >= 0) {
                if(hz > lx) { right = false; left = true; } /* land short of it */
                /* else: keep RIGHT and overshoot it */
            }
        }
    }

    game_input_key(RR_KEY_LEFT, left);
    game_input_key(RR_KEY_RIGHT, right);
    game_input_key(RR_KEY_UP, up);
    return !s_done;
}

void bot_print_report(void)
{
    int32_t total = 0, deaths = 0;
    printf("\nBot speedrun report (%s)\n",
           s_mode == BOT_MODE_SINGLE ? "no UP, single jump only" :
           s_mode == BOT_MODE_NOUP ? "no UP button: real gravity" : "UP held: glide");
    printf("level  time      deaths\n");
    int32_t finished = 0, stalled = 0;
    for(int32_t i = 0; i < RR_LEVEL_COUNT; i++) {
        if(s_times_ms[i] == 0 && s_deaths[i] == 0 && !s_stalled[i]) continue;
        printf("%-6d %3d.%03d s   %-6d %s\n", (int)i + 1, (int)s_times_ms[i] / 1000, (int)s_times_ms[i] % 1000,
               (int)s_deaths[i], s_times_ms[i] > 0 ? "" : (s_stalled[i] ? "STALLED" : "unfinished"));
        total += s_times_ms[i];
        deaths += s_deaths[i];
        if(s_times_ms[i] > 0) finished++;
        if(s_stalled[i]) stalled++;
    }
    printf("total  %3d.%03d s   %d deaths, %d/%d levels finished%s\n",
           (int)total / 1000, (int)total % 1000, (int)deaths, (int)finished, RR_LEVEL_COUNT,
           stalled ? " (STALLED - total is meaningless)" : "");
}
