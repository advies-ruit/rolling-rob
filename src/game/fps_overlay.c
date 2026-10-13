#include "fps_overlay.h"
#include "ui.h"

static lv_obj_t * s_label;
static uint32_t s_frames;
static uint32_t s_start_ms;
static uint32_t (*s_frames_cb)(void);
static uint32_t s_frames_base;

static void refr_ready_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    s_frames++;
}

static void update_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    uint32_t now = lv_tick_get();
    uint32_t elapsed = now - s_start_ms;
    if(elapsed == 0) return;
    /* One decimal, without pulling in float printf. */
    if(s_frames_cb) {
        uint32_t n = s_frames_cb();
        s_frames = n - s_frames_base;
        s_frames_base = n;
    }
    uint32_t fps10 = (s_frames * 10000u + elapsed / 2) / elapsed;
    /* LVGL redraws only what changed: a still screen (the level picker) gives
     * one or two redraws a second, which is not a slow frame rate */
    if(s_frames_cb == NULL && fps10 < 30) lv_label_set_text(s_label, "still");
    else lv_label_set_text_fmt(s_label, "%u.%u fps", (unsigned)(fps10 / 10), (unsigned)(fps10 % 10));
    s_frames = 0;
    s_start_ms = now;
}

void fps_overlay_create(lv_display_t * disp, uint32_t (*frames)(void))
{
    s_frames_cb = frames;
    if(frames) s_frames_base = frames();
    s_label = lv_label_create(lv_display_get_layer_top(disp));
    lv_label_set_text(s_label, "-- fps");
    lv_obj_set_style_text_font(s_label, rr_ui_font(10.0f * rr_ui_scale()), 0);
    lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_label, LV_OPA_50, 0);
    lv_obj_set_style_pad_hor(s_label, rr_ui_px(3), 0);
    lv_obj_set_style_radius(s_label, rr_ui_px(2), 0);
    lv_obj_align(s_label, LV_ALIGN_TOP_MID, 0, rr_ui_px(4));

    if(frames == NULL) lv_display_add_event_cb(disp, refr_ready_cb, LV_EVENT_REFR_READY, NULL);
    s_start_ms = lv_tick_get();
    lv_timer_create(update_timer_cb, 1000, NULL);
}

lv_obj_t * fps_overlay_label(void) { return s_label; }

void fps_overlay_set_visible(bool on)
{
    if(s_label && lv_obj_is_hidden(s_label) == on) lv_obj_set_hidden(s_label, !on);
}
