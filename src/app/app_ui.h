#ifndef ROLLING_ROB_APP_UI_H
#define ROLLING_ROB_APP_UI_H

/* Widgets and colours shared by the app's pages (app.c). */

#include "lvgl.h"
#include "ui.h"

#define K(n)    rr_ui_px(n)
#define FONT(n) rr_ui_font((float)(n) * rr_ui_scale())

static const lv_color_t GOLD = LV_COLOR_MAKE(0xFF, 0xD7, 0x00);
static const lv_color_t PANEL = LV_COLOR_MAKE(0x10, 0x14, 0x10);
static const lv_color_t TILE = LV_COLOR_MAKE(0x26, 0x2B, 0x26);
static const lv_color_t DIM = LV_COLOR_MAKE(0x9A, 0xA0, 0x9A);

lv_obj_t * app_button(lv_obj_t * parent, const char * text, bool primary, lv_event_cb_t cb, void * user);
/* full screen on the top layer, hidden */
lv_obj_t * app_panel(void);
lv_obj_t * app_heading(lv_obj_t * parent, const char * text);
lv_obj_t * app_close_button(lv_obj_t * parent, lv_event_cb_t cb);

#endif /* ROLLING_ROB_APP_UI_H */
