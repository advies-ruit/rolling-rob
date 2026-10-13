#ifndef ROLLING_ROB_FPS_OVERLAY_H
#define ROLLING_ROB_FPS_OVERLAY_H

#include "lvgl.h"

/*
 * On-screen frame counter, top centre, on LVGL's top layer so it stays above
 * the HUD and the level picker. It counts the display's completed refreshes
 * (LV_EVENT_REFR_READY), i.e. frames that actually reached the screen, not
 * how often the game rasterized. Updated once a second.
 */
/* `frames` returns a running count of frames shown, for displays whose
 * refreshes are not the frames (when LVGL draws only the HUD); NULL
 * counts the display's refreshes. */
void fps_overlay_create(lv_display_t * disp, uint32_t (*frames)(void));

/* Shown from creation on; this hides and shows it again. */
void fps_overlay_set_visible(bool on);
/* The label, NULL before fps_overlay_create (for a picture of the screen). */
lv_obj_t * fps_overlay_label(void);

#endif /* ROLLING_ROB_FPS_OVERLAY_H */
