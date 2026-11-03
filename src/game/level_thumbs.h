#ifndef ROLLING_ROB_LEVEL_THUMBS_H
#define ROLLING_ROB_LEVEL_THUMBS_H

#include "lvgl.h"

/* Level thumbnails shown in the level picker, one set per look (the
 * picker shows the one of the set each level is drawn with,
 * rr_art_set_for_level). level_thumbs.c is generated from headless
 * simulator renders. */

#define RR_LEVEL_THUMB_COUNT 11
#define RR_LEVEL_THUMB_W 108
#define RR_LEVEL_THUMB_H 61

extern const lv_image_dsc_t rr_level_thumbs[RR_LEVEL_THUMB_COUNT];         /* original look */
extern const lv_image_dsc_t rr_level_thumbs_painted[RR_LEVEL_THUMB_COUNT]; /* painted art/, by set */

#endif /* ROLLING_ROB_LEVEL_THUMBS_H */
