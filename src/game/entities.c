#include "entities.h"
#include "level.h"
#include <math.h>
#include <stdlib.h>

#define WORLD_HEIGHT 1080.0f /* original: canvas.height */

rr_obstacle_t   rr_obstacles[RR_MAX_OBSTACLES];
int32_t         rr_obstacle_count;
rr_saucer_t     rr_saucers[RR_MAX_SAUCERS];
int32_t         rr_saucer_count;
rr_mine_t       rr_mines[RR_MAX_MINES];
int32_t         rr_mine_count;
rr_julian_t     rr_julians[RR_MAX_JULIANS];
int32_t         rr_julian_count;
rr_fireball_t   rr_fireballs[RR_MAX_FIREBALLS];
int32_t         rr_fireball_count;
rr_checkpoint_t rr_checkpoints[RR_MAX_CHECKPOINTS];
int32_t         rr_checkpoint_count;
int32_t         rr_last_checkpoint = -1;

bool    rr_beam_active;
int32_t rr_beam_saucer_index = -1;
float   rr_beam_progress;

/* JS Math.random(): float in [0, 1) */
float rr_randf(void)
{
    return (float)rand() / ((float)RAND_MAX + 1.0f);
}

void rr_entities_generate(int32_t level, const rr_terrain_t * terrain)
{
    const rr_level_config_t * cfg = &rr_levels[level];
    float finish_line = terrain->finish_line;

    rr_obstacle_count = 0;
    for(int32_t i = 0; i < cfg->obstacle_count && rr_obstacle_count < RR_MAX_OBSTACLES; i++) {
        float x = 400.0f + (float)i * (finish_line / (float)cfg->obstacle_count) + rr_randf() * 100.0f;
        rr_obstacles[rr_obstacle_count].x = x;
        rr_obstacles[rr_obstacle_count].y = rr_terrain_height_at(terrain, x) - 60.0f;
        rr_obstacles[rr_obstacle_count].w = 60.0f;
        rr_obstacles[rr_obstacle_count].h = 60.0f;
        rr_obstacle_count++;
    }

    rr_saucer_count = 0;
    int32_t saucer_target = cfg->obstacle_count / 2 + level * 2;
    for(int32_t i = 0; i < saucer_target && rr_saucer_count < RR_MAX_SAUCERS; i++) {
        rr_saucer_t * s = &rr_saucers[rr_saucer_count++];
        s->x = 300.0f + rr_randf() * (finish_line - 300.0f);
        s->y = 150.0f + rr_randf() * 400.0f;
        s->vx = 0;
        s->vy = 0;
        s->beam_timer = rr_randf() * 300.0f;
        s->phase = rr_randf() * (float)M_PI * 2.0f;
    }

    rr_mine_count = 0;
    int32_t mine_target = level / 2 + 2;
    for(int32_t i = 0; i < mine_target && rr_mine_count < RR_MAX_MINES; i++) {
        float x = 600.0f + (float)i * 500.0f + rr_randf() * 200.0f;
        rr_mines[rr_mine_count].x = x;
        rr_mines[rr_mine_count].y = rr_terrain_height_at(terrain, x) - 40.0f;
        rr_mines[rr_mine_count].pulse = rr_randf() * (float)M_PI * 2.0f;
        rr_mine_count++;
    }

    rr_julian_count = 0;
    rr_fireball_count = 0;
    int32_t julian_target = level - 2;
    if(julian_target < 0) julian_target = 0;
    for(int32_t i = 0; i < julian_target && rr_julian_count < RR_MAX_JULIANS; i++) {
        float spacing = finish_line / (float)(julian_target + 1);
        float x = 800.0f + spacing * (float)(i + 1) + (rr_randf() - 0.5f) * 300.0f;
        rr_julian_t * j = &rr_julians[rr_julian_count++];
        j->x = x;
        j->y = rr_terrain_height_at(terrain, x) - 120.0f;
        j->vy = 0;
        j->jump_timer = rr_randf() * 120.0f;
        j->fire_timer = rr_randf() * 90.0f;
    }

    rr_checkpoint_count = 0;
    rr_last_checkpoint = -1;
    if(level == 9) {
        for(int32_t i = 0; i < 5; i++) {
            float x = 1500.0f + (finish_line - 1500.0f) * ((float)(i + 1) / 6.0f);
            rr_checkpoints[rr_checkpoint_count].x = x;
            rr_checkpoints[rr_checkpoint_count].y = rr_terrain_height_at(terrain, x) - 100.0f;
            rr_checkpoints[rr_checkpoint_count].activated = false;
            rr_checkpoint_count++;
        }
    }

    rr_beam_active = false;
    rr_beam_saucer_index = -1;
    rr_beam_progress = 0;
}

static void step_saucers(const rr_terrain_t * terrain, bool hunting, float rob_x, float rob_y)
{
    (void)terrain;
    for(int32_t i = 0; i < rr_saucer_count; i++) {
        rr_saucer_t * s = &rr_saucers[i];

        if(hunting) {
            float dx = rob_x - s->x;
            float dy = (rob_y - 150.0f) - s->y;
            float dist = sqrtf(dx * dx + dy * dy);

            if(dist > 0) {
                float speed = 1.5f;
                s->vx += (dx / dist) * speed * 0.1f;
                s->vy += (dy / dist) * speed * 0.1f;

                float cur_speed = sqrtf(s->vx * s->vx + s->vy * s->vy);
                if(cur_speed > 3.0f) {
                    s->vx = (s->vx / cur_speed) * 3.0f;
                    s->vy = (s->vy / cur_speed) * 3.0f;
                }
            }

            if(dist < 200.0f && !rr_beam_active) {
                s->beam_timer++;
                if(s->beam_timer > 180.0f) {
                    rr_beam_active = true;
                    rr_beam_saucer_index = i;
                    rr_beam_progress = 0;
                    s->beam_timer = 0;
                }
            }
        }

        s->x += s->vx;
        s->y += s->vy;
        s->vx *= 0.98f;
        s->vy *= 0.98f;

        if(s->y < 100.0f) s->y = 100.0f;
        if(s->y > 500.0f) s->y = 500.0f;
    }
}

static void spawn_fireball_volley(float x, float y)
{
    for(int32_t i = -1; i <= 1; i++) {
        if(rr_fireball_count >= RR_MAX_FIREBALLS) {
            /* Cap reached: recycle the oldest so the newest volley always shows. */
            rr_fireballs[0] = rr_fireballs[rr_fireball_count - 1];
            rr_fireball_count--;
        }
        rr_fireball_t * fb = &rr_fireballs[rr_fireball_count++];
        fb->x = x;
        fb->y = y;
        fb->vx = -8.0f + (float)i * 2.0f;
        fb->vy = -5.0f + (float)i * 1.5f;
        fb->life = 1.0f;
    }
}

static void step_julians(const rr_terrain_t * terrain, bool game_started)
{
    if(!game_started) return;

    for(int32_t i = 0; i < rr_julian_count; i++) {
        rr_julian_t * j = &rr_julians[i];

        j->jump_timer++;
        if(j->jump_timer >= 120.0f && j->vy == 0.0f) {
            j->vy = -15.0f;
            j->jump_timer = 0;
        }

        j->vy += 0.5f;
        j->y += j->vy;

        float ground_y = rr_terrain_height_at(terrain, j->x + 50.0f) - 120.0f; /* width/2, height */
        if(j->y >= ground_y) {
            j->y = ground_y;
            j->vy = 0;
        }

        j->fire_timer++;
        if(j->fire_timer >= 90.0f) {
            spawn_fireball_volley(j->x, j->y + 40.0f);
            j->fire_timer = 0;
        }
    }
}

static void step_fireballs(void)
{
    for(int32_t i = rr_fireball_count - 1; i >= 0; i--) {
        rr_fireball_t * fb = &rr_fireballs[i];
        fb->vy += 0.3f;
        fb->x += fb->vx;
        fb->y += fb->vy;
        fb->life -= 0.005f;

        if(fb->y > WORLD_HEIGHT || fb->x < -100.0f || fb->life <= 0.0f) {
            rr_fireballs[i] = rr_fireballs[rr_fireball_count - 1];
            rr_fireball_count--;
        }
    }
}

void rr_entities_step(const rr_terrain_t * terrain, bool game_started, bool hunting,
                      float rob_x, float rob_y)
{
    step_saucers(terrain, hunting, rob_x, rob_y);
    step_julians(terrain, game_started);
    step_fireballs();
}

static bool aabb_overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

rr_collision_result_t rr_entities_collide(float * rob_x, float * rob_y,
                                           float * rob_vx, float * rob_vy,
                                           int32_t rob_radius)
{
    rr_collision_result_t result = { RR_DEATH_NONE, 0, 0, false, false };
    float rx = *rob_x, ry = *rob_y;
    float r = (float)rob_radius;

    /* Obstacles: bounce, don't kill. */
    for(int32_t i = 0; i < rr_obstacle_count; i++) {
        rr_obstacle_t * o = &rr_obstacles[i];
        if(aabb_overlap(rx - r, ry - r, r * 2, r * 2, o->x, o->y, o->w, o->h)) {
            float cx = o->x + o->w / 2.0f, cy = o->y + o->h / 2.0f;
            float dx = rx - cx, dy = ry - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            if(dist > 0) {
                *rob_vx = (dx / dist) * 8.0f;
                *rob_vy = (dy / dist) * 8.0f - 2.0f;
            }
        }
    }

    /* Saucers: bounce, don't kill. */
    for(int32_t i = 0; i < rr_saucer_count; i++) {
        rr_saucer_t * s = &rr_saucers[i];
        if(aabb_overlap(rx - r, ry - r, r * 2, r * 2, s->x - 40.0f, s->y - 20.0f, 80.0f, 40.0f)) {
            float dx = rx - s->x, dy = ry - s->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if(dist > 0) {
                *rob_vx = (dx / dist) * 10.0f;
                *rob_vy = (dy / dist) * 10.0f - 3.0f;
            }
        }
    }

    /* Julian's fireballs: instant death. */
    for(int32_t i = rr_fireball_count - 1; i >= 0; i--) {
        rr_fireball_t * fb = &rr_fireballs[i];
        float dx = rx - fb->x, dy = ry - fb->y;
        if(sqrtf(dx * dx + dy * dy) < r + 15.0f) {
            result.death_x = fb->x;
            result.death_y = fb->y;
            rr_fireballs[i] = rr_fireballs[rr_fireball_count - 1];
            rr_fireball_count--;
            result.death = RR_DEATH_FIREBALL;
            return result;
        }
    }

    /* Julians themselves: instant death. */
    for(int32_t i = 0; i < rr_julian_count; i++) {
        rr_julian_t * j = &rr_julians[i];
        float dx = rx - (j->x + 50.0f), dy = ry - (j->y + 60.0f);
        if(sqrtf(dx * dx + dy * dy) < r + 50.0f) {
            result.death = RR_DEATH_JULIAN;
            return result;
        }
    }

    /* Mines: instant death. */
    for(int32_t i = 0; i < rr_mine_count; i++) {
        rr_mine_t * m = &rr_mines[i];
        float dx = rx - m->x, dy = ry - m->y;
        if(sqrtf(dx * dx + dy * dy) < r + 20.0f) {
            result.death = RR_DEATH_MINE;
            result.death_x = m->x;
            result.death_y = m->y;
            return result;
        }
    }

    /* Checkpoints. */
    for(int32_t i = 0; i < rr_checkpoint_count; i++) {
        rr_checkpoint_t * cp = &rr_checkpoints[i];
        if(!cp->activated && aabb_overlap(rx - r, ry - r, r * 2, r * 2, cp->x, cp->y, 60.0f, 100.0f)) {
            cp->activated = true;
            rr_last_checkpoint = i;
            result.checkpoint_hit = true;
        }
    }

    /* Tractor beam pull + abduction (drawBeam()'s physics side). */
    if(rr_beam_active && rr_beam_saucer_index >= 0) {
        rr_saucer_t * s = &rr_saucers[rr_beam_saucer_index];
        float beam_center_x = s->x;
        float beam_width = 100.0f + rr_beam_progress;
        float beam_bottom = s->y + 300.0f;
        bool in_beam = rx > beam_center_x - beam_width / 2.0f &&
                       rx < beam_center_x + beam_width / 2.0f &&
                       ry > s->y + 20.0f && ry < beam_bottom;

        if(in_beam) {
            *rob_vx *= 0.85f;
            *rob_vy *= 0.9f;
            *rob_x += (beam_center_x - rx) * 0.02f;

            rr_beam_progress += 1.0f;
            if(rr_beam_progress >= 180.0f) {
                *rob_x = (rx - 2000.0f > 200.0f) ? (rx - 2000.0f) : 200.0f;
                *rob_y = 150.0f;
                *rob_vx = 0;
                *rob_vy = 0;
                rr_beam_active = false;
                rr_beam_saucer_index = -1;
                rr_beam_progress = 0;
                result.abducted = true;
            }
        }
        else {
            rr_beam_active = false;
            rr_beam_saucer_index = -1;
            rr_beam_progress = 0;
        }
    }

    return result;
}
