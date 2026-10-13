#ifndef ROLLING_ROB_RENDER_H
#define ROLLING_ROB_RENDER_H

#include "lvgl.h"

/* Small reusable draw helpers on top of LVGL's layer draw API.
 * The original HTML game draws everything with Canvas2D primitives
 * (fillRect/arc); these mirror that at the primitive level so the
 * game-core code (Fase 2+) can stay close to the original logic. */

void rr_draw_filled_rect(lv_layer_t * layer, int32_t x, int32_t y, int32_t w, int32_t h,
                          lv_color_t color, lv_opa_t opa);

/* Top-to-bottom gradient fill, used for the per-level theme sky. */
void rr_draw_vgrad_rect(lv_layer_t * layer, int32_t x, int32_t y, int32_t w, int32_t h,
                         lv_color_t color_top, lv_color_t color_bottom, lv_opa_t opa);

void rr_draw_filled_circle(lv_layer_t * layer, int32_t cx, int32_t cy, int32_t r,
                            lv_color_t color, lv_opa_t opa);

/* Stroked arc, angles in degrees: 0 = 3 o'clock, 90 = 6 o'clock (clockwise),
 * matching LVGL's convention. Used for small face/decoration details. */
void rr_draw_arc(lv_layer_t * layer, int32_t cx, int32_t cy, int32_t r,
                  int32_t start_deg, int32_t end_deg, int32_t width,
                  lv_color_t color, lv_opa_t opa);

void rr_draw_line(lv_layer_t * layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                   int32_t width, lv_color_t color, lv_opa_t opa);

void rr_draw_filled_triangle(lv_layer_t * layer, int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2, int32_t x3, int32_t y3,
                              lv_color_t color, lv_opa_t opa);

#endif /* ROLLING_ROB_RENDER_H */
