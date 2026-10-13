#ifndef ROLLING_ROB_LEVEL_H
#define ROLLING_ROB_LEVEL_H

#include <stdint.h>

/* Level table, 1:1 from `levels[]` in rolling-rob.html. Colors are
 * 0xRRGGBB. The theme drives the sky decoration (drawSky) and the
 * monster type drives drawMonster(). */

typedef enum {
    RR_THEME_FOREST,
    RR_THEME_DESERT,
    RR_THEME_SNOW,
    RR_THEME_VOLCANO,
    RR_THEME_JUNGLE,
    RR_THEME_SPACE,
    RR_THEME_CANDY,
    RR_THEME_CYBER,
    RR_THEME_HELL,
    RR_THEME_RAINBOW,
} rr_theme_t;

typedef enum {
    RR_MONSTER_SLIME,
    RR_MONSTER_CACTUS,
    RR_MONSTER_YETI,
    RR_MONSTER_LAVA,
    RR_MONSTER_PLANT,
    RR_MONSTER_ALIEN,
    RR_MONSTER_CANDY,
    RR_MONSTER_ROBOT,
    RR_MONSTER_DEMON,
    RR_MONSTER_CRYSTAL,
} rr_monster_t;

typedef struct {
    const char * name;
    rr_theme_t theme;
    int32_t num_points;
    float steepness;
    int32_t obstacle_count;
    uint32_t sky_color1;
    uint32_t sky_color2;
    uint32_t ground_color;
    rr_monster_t monster_type;
} rr_level_config_t;

#define RR_LEVEL_COUNT 10

extern const rr_level_config_t rr_levels[RR_LEVEL_COUNT];

#endif /* ROLLING_ROB_LEVEL_H */
