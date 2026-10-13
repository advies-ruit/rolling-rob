#ifndef ROLLING_ROB_FRACTAL_VIEW_H
#define ROLLING_ROB_FRACTAL_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/*
 * The fractal explorer in the app (src/fractal): Mandelbrot, Julia and the
 * Burning Ship, zoomed and moved by keys, mouse or finger. The picture is
 * computed progressively on all cores within a time budget per frame -- a
 * coarse grid first, then finer -- and after every move the previous
 * picture is moved or scaled along first, so there is always something to
 * see. Colours are applied per frame (colour cycling costs no recomputing).
 * The platform draws the frame through fractal_render() in bands, like the
 * 3D world; the figures and buttons are LVGL widgets over it.
 *
 * Keys: arrows / WASD move, Z / X zoom in / out (held: smoothly), Space
 * zooms on by itself, J Mandelbrot <-> Julia of the point in the middle,
 * K the next fractal, C colour cycling, P palette, I iterations, R
 * resolution, H back to the start view. Mouse / finger: a tap zooms in on
 * that point, a long press zooms out.
 */
void fractal_view_create(int w, int h);
void fractal_view_show(bool on);
void fractal_view_tick(uint32_t dt_ms);
void fractal_view_key(int k); /* APP_KEY_*, app.h */
void fractal_view_set_menu(void (*cb)(void));
/* The whole frame, out_w x out_h, in bands to `sink` (any thread, any order). */
void fractal_render(int out_w, int out_h, void (*sink)(uint32_t * band, int stride_px, int y0, int rows, void * user),
                    void * user);

/* For its page (app_page.c): the settings, each with a step left / right. */
const char * fractal_view_kind_name(void);
void fractal_view_kind_step(int dir);
const char * fractal_view_palette_name(void);
void fractal_view_palette_step(int dir);
bool fractal_view_cycling(void);
void fractal_view_set_cycling(bool on);
const char * fractal_view_iter_text(void);
void fractal_view_iter_step(int dir);
const char * fractal_view_res_text(void);
void fractal_view_res_step(int dir);
void fractal_view_reset(void);
/* Where the view is: a line for the page. */
const char * fractal_view_where(void);
/* Its screen, as it would look, into the picture on its page (app_page.h). */
void fractal_view_preview(void);

#endif /* ROLLING_ROB_FRACTAL_VIEW_H */
