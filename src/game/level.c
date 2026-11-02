#include "level.h"

/* Levels 1-10 verbatim from the original `levels[]` (finishDistance is
 * unused there: the real finish line comes out of generateMountain()).
 * Level 11 continues their curve (points +65, steepness +0.2, obstacles +2)
 * in the space theme, at night. */
const rr_level_config_t rr_levels[RR_LEVEL_COUNT] = {
    { "Beginner's Hill",    RR_THEME_FOREST,  60,  0.3f,  8, 0x87CEEB, 0x90EE90, 0x8B7355, RR_MONSTER_SLIME   },
    { "Rocky Ridge",        RR_THEME_DESERT,  85,  0.5f, 10, 0xFFB347, 0xFFA500, 0xD2691E, RR_MONSTER_CACTUS  },
    { "Steep Slope",        RR_THEME_SNOW,   110,  0.7f, 12, 0xB0E0E6, 0xF0F8FF, 0xE0E0E0, RR_MONSTER_YETI    },
    { "Boulder Boulevard",  RR_THEME_VOLCANO,140,  0.9f, 14, 0x8B0000, 0xFF4500, 0x654321, RR_MONSTER_LAVA    },
    { "Danger Drop",        RR_THEME_JUNGLE, 175,  1.1f, 16, 0x228B22, 0x90EE90, 0x556B2F, RR_MONSTER_PLANT   },
    { "Extreme Edge",       RR_THEME_SPACE,  215,  1.3f, 18, 0x000033, 0x1a1a4d, 0x4a4a4a, RR_MONSTER_ALIEN   },
    { "Mega Mountain",      RR_THEME_CANDY,  260,  1.5f, 20, 0xFFB6C1, 0xFFC0CB, 0xDDA0DD, RR_MONSTER_CANDY   },
    { "Insane Incline",     RR_THEME_CYBER,  310,  1.7f, 22, 0x0F0F3F, 0x1F1F6F, 0x2F2F4F, RR_MONSTER_ROBOT   },
    { "Death Descent",      RR_THEME_HELL,   365,  1.9f, 24, 0x330000, 0x660000, 0x4d0000, RR_MONSTER_DEMON   },
    { "Ultimate Challenge", RR_THEME_RAINBOW,425,  2.1f, 26, 0x9400D3, 0xFFD700, 0xFF69B4, RR_MONSTER_CRYSTAL },
    { "Beyond the Summit",  RR_THEME_SPACE,  490,  2.3f, 28, 0x0B0B2B, 0x3A1A5E, 0x3B3B5B, RR_MONSTER_ALIEN   },
};
