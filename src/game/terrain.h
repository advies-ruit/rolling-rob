#ifndef ROLLING_ROB_TERRAIN_H
#define ROLLING_ROB_TERRAIN_H

#include <stdint.h>
#include "level.h"

/* Procedural mountain terrain, ported 1:1 from generateMountain()/
 * getTerrainHeight()/getSlope() in the original rolling-rob.html.
 * Coordinates are in "world units" (the original's 1920x1080 canvas
 * space) - the renderer scales them down to the real 480x272 panel. */

/* Largest level (Ultimate Challenge) has 425 ridge points + 2 closing points. */
#define RR_TERRAIN_MAX_POINTS 427

typedef struct {
    float x, y;
} rr_point_t;

typedef struct {
    rr_point_t points[RR_TERRAIN_MAX_POINTS];
    int32_t count;
    float finish_line;
} rr_terrain_t;

void rr_terrain_generate(rr_terrain_t * terrain, const rr_level_config_t * cfg);
float rr_terrain_height_at(const rr_terrain_t * terrain, float x);
float rr_terrain_slope_at(const rr_terrain_t * terrain, float x);

#endif /* ROLLING_ROB_TERRAIN_H */
