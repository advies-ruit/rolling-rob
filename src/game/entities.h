#ifndef ROLLING_ROB_ENTITIES_H
#define ROLLING_ROB_ENTITIES_H

#include <stdbool.h>
#include <stdint.h>
#include "terrain.h"

/* Obstacles/saucers/Julians/mines/checkpoints, ported 1:1 from the
 * generate*()/update*() functions in rolling-rob.html. All coordinates
 * are world units (the original 1920x1080 canvas space). */

#define RR_MAX_OBSTACLES   30
#define RR_MAX_SAUCERS     40
#define RR_MAX_MINES       10
#define RR_MAX_JULIANS     10
#define RR_MAX_FIREBALLS  120
#define RR_MAX_CHECKPOINTS  5

#define RR_SAUCER_W   80.0f
#define RR_SAUCER_H   40.0f
#define RR_JULIAN_W  100.0f
#define RR_JULIAN_H  120.0f
#define RR_MINE_R     20.0f
#define RR_CP_W       60.0f
#define RR_CP_H      100.0f

typedef struct { float x, y, w, h; } rr_obstacle_t;
typedef struct { float x, y, vx, vy, beam_timer, phase; } rr_saucer_t;
typedef struct { float x, y, pulse; } rr_mine_t;
typedef struct { float x, y, vy, jump_timer, fire_timer; } rr_julian_t;
typedef struct { float x, y, vx, vy, life; } rr_fireball_t;
typedef struct { float x, y; bool activated; } rr_checkpoint_t;

extern rr_obstacle_t   rr_obstacles[RR_MAX_OBSTACLES];
extern int32_t         rr_obstacle_count;
extern rr_saucer_t     rr_saucers[RR_MAX_SAUCERS];
extern int32_t         rr_saucer_count;
extern rr_mine_t       rr_mines[RR_MAX_MINES];
extern int32_t         rr_mine_count;
extern rr_julian_t     rr_julians[RR_MAX_JULIANS];
extern int32_t         rr_julian_count;
extern rr_fireball_t   rr_fireballs[RR_MAX_FIREBALLS];
extern int32_t         rr_fireball_count;
extern rr_checkpoint_t rr_checkpoints[RR_MAX_CHECKPOINTS];
extern int32_t         rr_checkpoint_count;
extern int32_t         rr_last_checkpoint; /* -1 if none activated yet */

extern bool    rr_beam_active;
extern int32_t rr_beam_saucer_index; /* -1 if none */
extern float   rr_beam_progress;

/* JS Math.random(): float in [0, 1) */
float rr_randf(void);

/* (Re)populates every entity array for the given level, mirroring
 * generateMountain()'s obstacle/saucer/mine/julian/checkpoint blocks. */
void rr_entities_generate(int32_t level, const rr_terrain_t * terrain);

/* Advances saucers/Julians/fireballs by one fixed physics step.
 * `hunting` = gameStarted && !gameOver (saucers only chase while alive). */
void rr_entities_step(const rr_terrain_t * terrain, bool game_started, bool hunting,
                      float rob_x, float rob_y);

typedef enum {
    RR_DEATH_NONE,
    RR_DEATH_MINE,
    RR_DEATH_JULIAN,
    RR_DEATH_FIREBALL,
} rr_death_cause_t;

typedef struct {
    rr_death_cause_t death;   /* != RR_DEATH_NONE -> caller should end the run */
    float death_x, death_y;   /* second explosion site (mine / fireball) */
    bool checkpoint_hit;      /* a new checkpoint got activated this step */
    bool abducted;            /* UFO beam completed -> rob was teleported back */
} rr_collision_result_t;

/* Resolves rob<->entity collisions for one step: bounces rob off
 * obstacles/saucers directly, applies the tractor beam pull/abduction,
 * and reports lethal hits / checkpoint activations. */
rr_collision_result_t rr_entities_collide(float * rob_x, float * rob_y,
                                           float * rob_vx, float * rob_vy,
                                           int32_t rob_radius);

#endif /* ROLLING_ROB_ENTITIES_H */
