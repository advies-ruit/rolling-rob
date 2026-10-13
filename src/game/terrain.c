#include "terrain.h"
#include <stdlib.h>

/* JS Math.random(): float in [0, 1) */
static float rr_randf(void)
{
    return (float)rand() / ((float)RAND_MAX + 1.0f);
}

void rr_terrain_generate(rr_terrain_t * terrain, const rr_level_config_t * cfg)
{
    terrain->count = 0;
    terrain->points[terrain->count].x = 0;
    terrain->points[terrain->count].y = 200;
    terrain->count++;

    float current_x = 0;
    float current_y = 200;

    for(int32_t i = 1; i < cfg->num_points; i++) {
        current_x += 30.0f + rr_randf() * 40.0f;
        current_y += rr_randf() * 120.0f * cfg->steepness - 20.0f;
        if(current_y < 400.0f) current_y = 400.0f;
        if(current_y > 950.0f) current_y = 950.0f;

        terrain->points[terrain->count].x = current_x;
        terrain->points[terrain->count].y = current_y;
        terrain->count++;
    }

    terrain->points[terrain->count].x = current_x + 150.0f;
    terrain->points[terrain->count].y = 1080.0f;
    terrain->count++;

    terrain->points[terrain->count].x = 0;
    terrain->points[terrain->count].y = 1080.0f;
    terrain->count++;

    terrain->finish_line = current_x - 300.0f;
}

float rr_terrain_height_at(const rr_terrain_t * terrain, float x)
{
    for(int32_t i = 0; i < terrain->count - 1; i++) {
        const rr_point_t * p1 = &terrain->points[i];
        const rr_point_t * p2 = &terrain->points[i + 1];

        if(x >= p1->x && x <= p2->x) {
            float t = (x - p1->x) / (p2->x - p1->x);
            return p1->y + (p2->y - p1->y) * t;
        }
    }
    return 600.0f;
}

float rr_terrain_slope_at(const rr_terrain_t * terrain, float x)
{
    for(int32_t i = 0; i < terrain->count - 1; i++) {
        const rr_point_t * p1 = &terrain->points[i];
        const rr_point_t * p2 = &terrain->points[i + 1];

        if(x >= p1->x && x <= p2->x) {
            return (p2->y - p1->y) / (p2->x - p1->x);
        }
    }
    return 0.0f;
}
