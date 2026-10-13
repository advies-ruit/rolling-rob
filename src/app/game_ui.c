/*
 * Widgets of the small games (game_ui.h).
 */

#include "game_ui.h"
#include "ui.h"
#include <string.h>

static const lv_color_t GOLD = LV_COLOR_MAKE(0xFF, 0xD7, 0x00);

lv_obj_t * gu_layer(void)
{
    lv_obj_t * o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_hidden(o, true);
    return o;
}

lv_obj_t * gu_label(lv_obj_t * parent, const char * text, int font_px, lv_color_t c)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_label_set_recolor(l, true);
    lv_obj_set_style_text_font(l, rr_ui_font((float)font_px), 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

lv_obj_t * gu_button(lv_obj_t * parent, const char * text, int x, int y, int w, int h, int font_px,
                     lv_event_cb_t cb, void * user)
{
    lv_obj_t * b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2A2F38), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x3C4250), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, lv_color_hex(0x6A7080), 0);
    lv_obj_set_style_border_width(b, h / 20 > 0 ? h / 20 : 1, 0);
    lv_obj_set_style_radius(b, h / 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_t * l = gu_label(b, text, font_px, lv_color_white());
    lv_obj_center(l);
    if(cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t * gu_message(lv_obj_t * parent, int font_px, int unit, lv_event_cb_t cb)
{
    lv_obj_t * m = gu_label(parent, "", font_px, lv_color_white());
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(m, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_80, 0);
    lv_obj_set_style_border_color(m, GOLD, 0);
    lv_obj_set_style_border_width(m, unit, 0);
    lv_obj_set_style_radius(m, unit * 6, 0);
    lv_obj_set_style_pad_all(m, unit * 8, 0);
    lv_obj_set_clickable(m, true);
    if(cb) lv_obj_add_event_cb(m, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(m, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(m, true);
    return m;
}

void gu_set_text(lv_obj_t * label, const char * text)
{
    if(strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

void gu_message_set(lv_obj_t * msg, const char * text)
{
    if(text) gu_set_text(msg, text);
    if(lv_obj_is_hidden(msg) == (text != NULL)) lv_obj_set_hidden(msg, text == NULL);
}
