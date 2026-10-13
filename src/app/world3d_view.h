#ifndef ROLLING_ROB_WORLD3D_VIEW_H
#define ROLLING_ROB_WORLD3D_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "r3d.h"

/*
 * The 3D demo in the app (src/r3d): Rob as a rolling ball in a small world of
 * hills, platforms, trees and coins, seen from a camera behind him -- and a
 * benchmark of what the processor manages at each resolution and detail.
 * The frame is drawn by the platform through world3d_render() in bands, like
 * the game's own frame; the controls and figures are LVGL widgets over it.
 */
void world3d_view_create(int w, int h);
void world3d_view_show(bool on);
void world3d_view_tick(uint32_t dt_ms);
void world3d_view_key(int k); /* APP_KEY_*, app.h */
void world3d_view_touch(int slot, bool down, int32_t x, int32_t y);
void world3d_view_set_menu(void (*cb)(void));
/* The whole frame, out_w x out_h, in bands to `sink`. */
void world3d_render(int out_w, int out_h, r3_sink_t sink, void * user);

/* Its screen, as it would look, into the picture on its page (app_page.h). */
void world3d_view_preview(void);

#endif /* ROLLING_ROB_WORLD3D_VIEW_H */
