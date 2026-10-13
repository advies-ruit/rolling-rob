#ifndef ROLLING_ROB_UI_H
#define ROLLING_ROB_UI_H

#include "lvgl.h"

/*
 * Size of everything drawn in screen pixels -- HUD, level picker, title,
 * touch buttons, game text -- relative to the 480x272 panel it was laid out
 * on. The game can render larger -- 2x .. 4x in the desktop and web builds,
 * or 1.5x (720x408) and 2.67x (1280x720): the world
 * view scales with it, so the game shows the same slice and plays the same,
 * and this factor keeps the overlays the same size relative to the picture.
 */

/* From the render width: 480 -> 1, 720 -> 1.5, 1280 -> 2.67 ... Called by
 * game_init(). */
void rr_ui_init(int32_t screen_w);

/* Overrides it, after game_init() and before the HUD is made: a slow
 * device can keep its HUD at 2x on 1280x720, where 2.67x costs frames. */
void rr_ui_set_scale(float k);

float rr_ui_scale(void);

/* n layout pixels at 480x272, in screen pixels (rounded). */
static inline int32_t rr_ui_px(int32_t n)
{
    float v = (float)n * rr_ui_scale();
    return (int32_t)(v < 0 ? v - 0.5f : v + 0.5f);
}

/* The built-in font nearest to px (Montserrat 10 .. 48, see lv_conf.h). */
const lv_font_t * rr_ui_font(float px);

/* Its nominal size in pixels. */
int32_t rr_ui_font_size(float px);

#endif /* ROLLING_ROB_UI_H */
