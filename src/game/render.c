#include "render.h"

void rr_draw_filled_rect(lv_layer_t * layer, int32_t x, int32_t y, int32_t w, int32_t h,
                          lv_color_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = 0;
    dsc.border_width = 0;

    lv_area_t area;
    area.x1 = x;
    area.y1 = y;
    area.x2 = x + w - 1;
    area.y2 = y + h - 1;

    lv_draw_rect(layer, &dsc, &area);
}

void rr_draw_vgrad_rect(lv_layer_t * layer, int32_t x, int32_t y, int32_t w, int32_t h,
                         lv_color_t color_top, lv_color_t color_bottom, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = opa;
    dsc.radius = 0;
    dsc.border_width = 0;
    dsc.bg_grad.dir = LV_GRAD_DIR_VER;
    dsc.bg_grad.stops_count = 2;
    dsc.bg_grad.stops[0].color = color_top;
    dsc.bg_grad.stops[0].opa = LV_OPA_COVER;
    dsc.bg_grad.stops[0].frac = 0;
    dsc.bg_grad.stops[1].color = color_bottom;
    dsc.bg_grad.stops[1].opa = LV_OPA_COVER;
    dsc.bg_grad.stops[1].frac = 255;

    lv_area_t area;
    area.x1 = x;
    area.y1 = y;
    area.x2 = x + w - 1;
    area.y2 = y + h - 1;

    lv_draw_rect(layer, &dsc, &area);
}

void rr_draw_filled_circle(lv_layer_t * layer, int32_t cx, int32_t cy, int32_t r,
                            lv_color_t color, lv_opa_t opa)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = color;
    dsc.opa = opa;
    dsc.center.x = cx;
    dsc.center.y = cy;
    dsc.radius = (uint16_t)r;
    dsc.width = (int32_t)r; /* width == radius -> filled disc */
    dsc.start_angle = 0;
    dsc.end_angle = 360;

    lv_draw_arc(layer, &dsc);
}

void rr_draw_arc(lv_layer_t * layer, int32_t cx, int32_t cy, int32_t r,
                  int32_t start_deg, int32_t end_deg, int32_t width,
                  lv_color_t color, lv_opa_t opa)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = color;
    dsc.opa = opa;
    dsc.center.x = cx;
    dsc.center.y = cy;
    dsc.radius = (uint16_t)r;
    dsc.width = width;
    dsc.start_angle = (lv_value_precise_t)start_deg;
    dsc.end_angle = (lv_value_precise_t)end_deg;
    dsc.rounded = 1;

    lv_draw_arc(layer, &dsc);
}

void rr_draw_line(lv_layer_t * layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                   int32_t width, lv_color_t color, lv_opa_t opa)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1.x = x1;
    dsc.p1.y = y1;
    dsc.p2.x = x2;
    dsc.p2.y = y2;
    dsc.width = width;
    dsc.color = color;
    dsc.opa = opa;
    dsc.round_start = 1;
    dsc.round_end = 1;

    lv_draw_line(layer, &dsc);
}

void rr_draw_filled_triangle(lv_layer_t * layer, int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2, int32_t x3, int32_t y3,
                              lv_color_t color, lv_opa_t opa)
{
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.p[0].x = x1;
    dsc.p[0].y = y1;
    dsc.p[1].x = x2;
    dsc.p[1].y = y2;
    dsc.p[2].x = x3;
    dsc.p[2].y = y3;
    dsc.color = color;
    dsc.opa = opa;

    lv_draw_triangle(layer, &dsc);
}
