#include "hud.h"
#include "game.h"
#include "level_thumbs.h"
#include "art.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

/*
 * HUD built from LVGL widgets, laid out like the original page:
 *   top-left     : "Distance: Xm | Goal: Ym"   (#score)
 *   top-right    : "Rolling Rob: Vader Edition" (#title) + "Level:" button
 *   bottom-center: info text box (#info) above the Reset / Next buttons
 * The original's <select> is replaced by a full-screen level picker with
 * one tile (screenshot + name) per level, which is far easier to hit on a
 * 4.3" touchscreen than a 10-row dropdown. While it is open the game is
 * frozen (see hud_is_modal_open()).
 */

static lv_obj_t * s_score_label;
static lv_obj_t * s_score_shadow;
static lv_obj_t * s_info_label;
static lv_obj_t * s_level_btn;
static lv_obj_t * s_level_btn_label;
static lv_obj_t * s_next_btn;
static lv_obj_t * s_picker;
/* Size of the play area the HUD was created over; the level picker centres a
 * box of these dimensions on screen so its layout keeps its own coordinates. */
static int32_t s_ui_w, s_ui_h;
static lv_obj_t * s_tiles[RR_LEVEL_THUMB_COUNT];
static lv_obj_t * s_tile_best[RR_LEVEL_THUMB_COUNT];
static lv_obj_t * s_tile_img[RR_LEVEL_THUMB_COUNT];
static lv_obj_t * s_time_label;
static char s_last_time[96];
static char s_last_message[200];
static int32_t s_shown_level = -1;
/* Settings tiles in the picker's free cells (after the last level). With
 * eleven levels one cell is left: both groups then share it, two by two,
 * with short labels (s_settings_compact). */
static bool s_art_tile;
static bool s_settings_compact;
static lv_obj_t * s_music_label;
static lv_obj_t * s_art_label;
static lv_obj_t * s_fps_label;
/* Title screen (game_title_active()): the game HUD lives in s_hud_box so it
 * can be hidden as a whole; the title has its own box. */
static lv_obj_t * s_hud_box;
static lv_obj_t * s_title_box;
static lv_obj_t * s_title_prompt;
static bool s_title_menu; /* the platform shows a menu where the prompt was */
static void (*s_menu_cb)(void); /* a "Menu" button in the picker, back to it */
static int32_t s_pick;          /* the picker's keyboard cursor */
static void (*s_picker_closed)(bool chose);
static lv_obj_t * s_title_hint;

/* Laid out for 480x272; K() and FONT() scale it with the screen (ui.h). */
#define K(n)    rr_ui_px(n)
#define FONT(n) rr_ui_font((float)(n) * rr_ui_scale())

static const lv_color_t GOLD = LV_COLOR_MAKE(0xFF, 0xD7, 0x00);
static const lv_color_t GREEN = LV_COLOR_MAKE(0x4C, 0xAF, 0x50);

static void reset_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    game_reset_level();
}

static void next_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    int32_t level = game_get_level();
    if(level < game_get_level_count() - 1) game_goto_level(level + 1);
}

/* ---------------- level picker ---------------- */

static void picker_update_selection(void);

static void picker_set_open(bool open)
{
    if(open) s_pick = game_title_active() ? 0 : game_get_level();
    lv_obj_set_hidden(s_picker, !open);
    if(open) picker_update_selection();
}

/* Closed by the player: with a level chosen, or backed out of. */
static void picker_closed(bool chose)
{
    picker_set_open(false);
    if(s_picker_closed) s_picker_closed(chose);
}

static void format_ms(char * buf, size_t n, int32_t ms)
{
    int32_t m = ms / 60000, sec = (ms / 1000) % 60, tenths = (ms / 100) % 10;
    if(m > 0) snprintf(buf, n, "%d:%02d.%d", (int)m, (int)sec, (int)tenths);
    else snprintf(buf, n, "%d.%d s", (int)sec, (int)tenths);
}

static void picker_update_selection(void)
{
    int32_t cur = game_title_active() ? -1 : game_get_level();
    for(int32_t i = 0; i < RR_LEVEL_THUMB_COUNT; i++) {
        bool sel = (i == s_pick); /* the cursor; the level being played is tinted */
        int32_t best = game_get_best_time_ms(i);
        if(best > 0) {
            char t[24];
            format_ms(t, sizeof(t), best);
            lv_label_set_text(s_tile_best[i], t);
            lv_obj_set_hidden(s_tile_best[i], false);
        }
        else {
            lv_obj_set_hidden(s_tile_best[i], true);
        }
        lv_obj_set_style_border_color(s_tiles[i], sel ? GOLD : lv_color_make(0x55, 0x55, 0x55), 0);
        lv_obj_set_style_border_width(s_tiles[i], sel ? K(2) : K(1), 0);
        lv_obj_set_style_bg_color(s_tiles[i], i == cur ? lv_color_make(0x33, 0x2E, 0x10) : lv_color_make(0x20, 0x20, 0x20), 0);
        /* the look the level is drawn with: original, or its painted set (a
         * fallback set when its own is missing; the rendered look shows the
         * painted thumbnails) */
        int set = rr_art_set_for_level(i);
        const lv_image_dsc_t * thumb = set >= 0 && set < RR_LEVEL_THUMB_COUNT ? &rr_level_thumbs_painted[set]
                                                                               : &rr_level_thumbs[i];
        if(lv_image_get_src(s_tile_img[i]) != thumb) lv_image_set_src(s_tile_img[i], thumb);
    }
    if(s_settings_compact) {
        if(s_music_label) lv_label_set_text(s_music_label, game_music_enabled() ? LV_SYMBOL_AUDIO " On" : LV_SYMBOL_MUTE " Off");
        if(s_art_label)
            lv_label_set_text(s_art_label, !rr_art_enabled() ? LV_SYMBOL_IMAGE " Orig."
                                           : rr_art_rendered() ? LV_SYMBOL_IMAGE " Rend." : LV_SYMBOL_IMAGE " Paint");
        if(s_fps_label) lv_label_set_text(s_fps_label, game_fps_enabled() ? LV_SYMBOL_EYE_OPEN " FPS" : LV_SYMBOL_EYE_CLOSE " FPS");
        return;
    }
    if(s_music_label)
        lv_label_set_text(s_music_label, game_music_enabled() ? LV_SYMBOL_AUDIO "  Music: on" : LV_SYMBOL_MUTE "  Music: off");
    if(s_art_label)
        lv_label_set_text(s_art_label, !rr_art_enabled() ? LV_SYMBOL_IMAGE "  Look: original"
                                       : rr_art_rendered() ? LV_SYMBOL_IMAGE "  Look: rendered" : LV_SYMBOL_IMAGE "  Look: painted");
    if(s_fps_label)
        lv_label_set_text(s_fps_label, game_fps_enabled() ? LV_SYMBOL_EYE_OPEN "  FPS: on" : LV_SYMBOL_EYE_CLOSE "  FPS: off");
}

void hud_enable_art_tile(bool on)
{
    s_art_tile = on;
}

static void music_tile_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    game_set_music_enabled(!game_music_enabled());
    picker_update_selection();
}

static void next_track_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    if(!game_music_enabled()) game_set_music_enabled(true);
    game_music_request_next();
    picker_update_selection();
}

static void fps_tile_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    game_set_fps_enabled(!game_fps_enabled());
    picker_update_selection();
}

static void art_tile_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    game_input_key(RR_KEY_ART, true); /* same path as the A key, so it is saved */
    game_input_key(RR_KEY_ART, false);
    picker_update_selection();
}

/* A small settings button in a picker cell; returns its label. */
static lv_obj_t * settings_button(lv_obj_t * board, int32_t x, int32_t y, int32_t w, int32_t h,
                                  lv_event_cb_t cb)
{
    lv_obj_t * btn = lv_button_create(board);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, K(4), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x20, 0x20, 0x20), 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x30, 0x30, 0x30), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_make(0x55, 0x55, 0x55), 0);
    lv_obj_set_style_border_width(btn, K(1), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, FONT(12), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return label;
}

static void tile_event_cb(lv_event_t * e)
{
    int32_t level = (int32_t)(intptr_t)lv_event_get_user_data(e);
    game_play_level(level);
    picker_closed(true);
}

static void picker_close_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    picker_closed(false);
}

static void level_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    picker_set_open(true);
}

void hud_picker_open(void) { picker_set_open(true); }

void hud_picker_open_at(int32_t level)
{
    picker_set_open(true);
    if(level >= 0 && level < RR_LEVEL_THUMB_COUNT) s_pick = level;
    picker_update_selection();
}

void hud_picker_set_closed_cb(void (*cb)(bool chose)) { s_picker_closed = cb; }

void hud_picker_move(int dx, int dy)
{
    const int32_t cols = 4, n = RR_LEVEL_THUMB_COUNT;
    int32_t p = s_pick + dx + dy * cols;
    if(dy != 0 && (p < 0 || p >= n)) return; /* up from the top row, down from the last */
    s_pick = (p % n + n) % n;
    picker_update_selection();
}

void hud_picker_choose(void)
{
    game_play_level(s_pick);
    picker_closed(true);
}

void hud_picker_back(void) { picker_closed(false); }

static void picker_menu_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    picker_set_open(false);
    s_menu_cb();
}

static void picker_create(void)
{
    /* Full-screen dark overlay on the top layer, so it covers canvas + HUD. */
    s_picker = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_picker, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_picker, 0, 0);
    lv_obj_set_style_bg_color(s_picker, lv_color_make(0x10, 0x14, 0x10), 0);
    lv_obj_set_style_bg_opa(s_picker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_picker, 0, 0);
    lv_obj_set_style_radius(s_picker, 0, 0);
    lv_obj_set_style_pad_all(s_picker, 0, 0);
    lv_obj_set_scrollable(s_picker, false);

    /* The overlay covers the whole screen, but its contents live in a box of
     * the build resolution, centred. That way every coordinate below stays the
     * one it was designed with, whatever the framebuffer happens to be. */
    lv_obj_t * board = lv_obj_create(s_picker);
    lv_obj_remove_style_all(board);
    lv_obj_set_size(board, s_ui_w, s_ui_h);
    lv_obj_center(board);
    lv_obj_set_scrollable(board, false);

    lv_obj_t * title = lv_label_create(board);
    lv_label_set_text(title, "CHOOSE YOUR HILL");
    lv_obj_set_style_text_font(title, FONT(14), 0);
    lv_obj_set_style_text_color(title, GOLD, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, K(10), K(6));

    lv_obj_t * hint = lv_label_create(board);
    lv_label_set_text(hint, "Arrows + Enter, or tap.  Esc: back");
    lv_obj_set_style_text_font(hint, FONT(10), 0);
    lv_obj_set_style_text_color(hint, lv_color_make(0xBB, 0xBB, 0xBB), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, K(180), K(9));

    lv_obj_t * close = lv_button_create(board);
    lv_obj_set_size(close, K(30), K(20));
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, K(-8), K(4));
    lv_obj_set_style_bg_color(close, lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_set_style_border_color(close, GOLD, 0);
    lv_obj_set_style_border_width(close, K(1), 0);
    lv_obj_set_style_radius(close, K(3), 0);
    lv_obj_set_style_shadow_width(close, 0, 0);
    lv_obj_set_style_pad_all(close, 0, 0);
    lv_obj_t * x = lv_label_create(close);
    lv_label_set_text(x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(x, FONT(12), 0);
    lv_obj_center(x);
    lv_obj_add_event_cb(close, picker_close_cb, LV_EVENT_CLICKED, NULL);

    if(s_menu_cb) {
        /* the app's main menu (desktop / web): the way back on a touch screen */
        lv_obj_t * menu = lv_button_create(board);
        lv_obj_set_size(menu, K(62), K(20));
        lv_obj_align(menu, LV_ALIGN_TOP_RIGHT, K(-44), K(4));
        lv_obj_set_style_bg_color(menu, lv_color_make(0x40, 0x40, 0x40), 0);
        lv_obj_set_style_border_color(menu, GOLD, 0);
        lv_obj_set_style_border_width(menu, K(1), 0);
        lv_obj_set_style_radius(menu, K(3), 0);
        lv_obj_set_style_shadow_width(menu, 0, 0);
        lv_obj_set_style_pad_all(menu, 0, 0);
        lv_obj_t * ml = lv_label_create(menu);
        lv_label_set_text(ml, LV_SYMBOL_HOME "  Menu");
        lv_obj_set_style_text_font(ml, FONT(11), 0);
        lv_obj_center(ml);
        lv_obj_add_event_cb(menu, picker_menu_cb, LV_EVENT_CLICKED, NULL);
    }

    /* 4 x 3 grid of tiles: thumbnail + "N - Name" */
    const int32_t cols = 4, cell_w = K(114), cell_h = K(78), x0 = K(12), y0 = K(30);
    for(int32_t i = 0; i < RR_LEVEL_THUMB_COUNT; i++) {
        lv_obj_t * tile = lv_button_create(board);
        lv_obj_set_size(tile, cell_w - K(6), cell_h - K(4));
        lv_obj_set_pos(tile, x0 + (i % cols) * cell_w, y0 + (i / cols) * cell_h);
        lv_obj_set_style_radius(tile, K(4), 0);
        lv_obj_set_style_shadow_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_set_style_bg_color(tile, lv_color_make(0x30, 0x30, 0x30), LV_STATE_PRESSED);
        lv_obj_add_event_cb(tile, tile_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t * img = lv_image_create(tile);
        s_tile_img[i] = img;
        lv_image_set_src(img, &rr_level_thumbs[i]); /* the look's own: picker_update_selection */
        if(rr_ui_scale() != 1.0f) {
            /* the thumbnails are 480x272 sized: stretched with the rest */
            lv_obj_set_size(img, K(rr_level_thumbs[i].header.w), K(rr_level_thumbs[i].header.h));
            lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
        }
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 0);

        /* level number badge in the thumbnail's corner */
        lv_obj_t * badge = lv_label_create(tile);
        lv_label_set_text_fmt(badge, "%d", (int)i + 1);
        lv_obj_set_style_text_font(badge, FONT(10), 0);
        lv_obj_set_style_text_color(badge, GOLD, 0);
        lv_obj_set_style_bg_color(badge, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_70, 0);
        lv_obj_set_style_pad_left(badge, K(3), 0);
        lv_obj_set_style_pad_right(badge, K(3), 0);
        lv_obj_set_style_pad_top(badge, K(1), 0);
        lv_obj_set_style_pad_bottom(badge, K(1), 0);
        lv_obj_set_style_radius(badge, K(2), 0);
        lv_obj_align(badge, LV_ALIGN_TOP_LEFT, K(2), K(2));

        /* best time badge, top-right (hidden until a time exists) */
        lv_obj_t * best = lv_label_create(tile);
        lv_label_set_text(best, "");
        lv_obj_set_style_text_font(best, FONT(10), 0);
        lv_obj_set_style_text_color(best, lv_color_white(), 0);
        lv_obj_set_style_bg_color(best, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(best, LV_OPA_70, 0);
        lv_obj_set_style_pad_left(best, K(3), 0);
        lv_obj_set_style_pad_right(best, K(3), 0);
        lv_obj_set_style_pad_top(best, K(1), 0);
        lv_obj_set_style_pad_bottom(best, K(1), 0);
        lv_obj_set_style_radius(best, K(2), 0);
        lv_obj_align(best, LV_ALIGN_TOP_RIGHT, K(-2), K(2));
        s_tile_best[i] = best;

        lv_obj_t * name = lv_label_create(tile);
        lv_label_set_text(name, game_get_level_name(i));
        lv_obj_set_style_text_font(name, FONT(10), 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_width(name, cell_w - K(10));
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, 0);

        s_tiles[i] = tile;
    }

    /* The free cells after the last level hold the settings the desktop and
     * web builds offer: music on/off + next track, and the art look (which
     * otherwise needs the A key, absent on a phone). */
    int32_t cell = RR_LEVEL_THUMB_COUNT;
    s_settings_compact = game_music_available() && s_art_tile && cell == cols * 3 - 1;
    if(s_settings_compact) {
        /* one cell for both: music and next track above, look and FPS below */
        int32_t x = x0 + (cell % cols) * cell_w, y = y0 + (cell / cols) * cell_h;
        int32_t bw = (cell_w - K(10)) / 2, bh = (cell_h - K(8)) / 2;
        s_music_label = settings_button(board, x, y, bw, bh, music_tile_cb);
        lv_obj_t * nl = settings_button(board, x + bw + K(4), y, bw, bh, next_track_cb);
        lv_label_set_text(nl, LV_SYMBOL_NEXT " Next");
        s_art_label = settings_button(board, x, y + bh + K(4), bw, bh, art_tile_cb);
        s_fps_label = settings_button(board, x + bw + K(4), y + bh + K(4), bw, bh, fps_tile_cb);
        lv_obj_t * labels[] = { s_music_label, nl, s_art_label, s_fps_label };
        for(int32_t k = 0; k < 4; k++) lv_obj_set_style_text_font(labels[k], FONT(11), 0);
        cell = cols * 3;
    }
    if(game_music_available() && cell < cols * 3) {
        int32_t x = x0 + (cell % cols) * cell_w, y = y0 + (cell / cols) * cell_h;
        int32_t bh = (cell_h - K(8)) / 2;
        s_music_label = settings_button(board, x, y, cell_w - K(6), bh, music_tile_cb);
        lv_obj_t * nl = settings_button(board, x, y + bh + K(4), cell_w - K(6), bh, next_track_cb);
        lv_label_set_text(nl, LV_SYMBOL_NEXT "  Next track");
        cell++;
    }
    if(s_art_tile && cell < cols * 3) {
        int32_t x = x0 + (cell % cols) * cell_w, y = y0 + (cell / cols) * cell_h;
        int32_t bh = (cell_h - K(8)) / 2;
        s_art_label = settings_button(board, x, y, cell_w - K(6), bh, art_tile_cb);
        s_fps_label = settings_button(board, x, y + bh + K(4), cell_w - K(6), bh, fps_tile_cb);
    }
    picker_update_selection();
    picker_set_open(false);
}

bool hud_is_modal_open(void)
{
    return s_picker != NULL && !lv_obj_is_hidden(s_picker);
}

/* ---------------- main HUD ---------------- */

/* A label with a 1px black drop shadow (the original uses text-shadow). */
static lv_obj_t * make_shadow_label(lv_obj_t * parent, const char * text, const lv_font_t * font,
                                    lv_color_t color, lv_align_t align, int32_t x, int32_t y, lv_obj_t ** shadow_out)
{
    lv_obj_t * shadow = lv_label_create(parent);
    lv_label_set_text(shadow, text);
    lv_obj_set_style_text_font(shadow, font, 0);
    lv_obj_set_style_text_color(shadow, lv_color_black(), 0);
    lv_obj_set_style_text_opa(shadow, LV_OPA_80, 0);
    lv_obj_align(shadow, align, x + K(1), y + K(1));

    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_align(label, align, x, y);

    if(shadow_out) *shadow_out = shadow;
    return label;
}

static void title_create(lv_obj_t * parent)
{
    s_title_box = lv_obj_create(parent);
    lv_obj_remove_style_all(s_title_box);
    lv_obj_set_size(s_title_box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_clickable(s_title_box, false);
    lv_obj_set_scrollable(s_title_box, false);
#if LV_FONT_MONTSERRAT_32
    const lv_font_t * big = FONT(32);
    int32_t big_scale = (int32_t)(256.0f * 32.0f * rr_ui_scale() / (float)rr_ui_font_size(32.0f * rr_ui_scale()) + 0.5f);
#else
    const lv_font_t * big = FONT(14);
    int32_t big_scale = 256;
#endif
    /* A dark band behind the text, soft at the top and bottom edges, so it
     * reads over any of the ten backgrounds. */
    const int32_t band_h = K(136), edge = K(24);
    lv_obj_t * band = lv_obj_create(s_title_box);
    lv_obj_remove_style_all(band);
    lv_obj_set_clickable(band, false); /* taps go through to the canvas */
    lv_obj_set_size(band, LV_PCT(100), band_h - 2 * edge);
    lv_obj_align(band, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(band, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_50, 0);
    for(int i = 0; i < 2; i++) {
        lv_obj_t * fade = lv_obj_create(s_title_box);
        lv_obj_remove_style_all(fade);
        lv_obj_set_clickable(fade, false);
        lv_obj_set_size(fade, LV_PCT(100), edge);
        lv_obj_align(fade, LV_ALIGN_CENTER, 0, i == 0 ? -(band_h - edge) / 2 : (band_h - edge) / 2);
        lv_obj_set_style_bg_opa(fade, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(fade, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_color(fade, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_dir(fade, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_main_opa(fade, i == 0 ? LV_OPA_TRANSP : LV_OPA_50, 0);
        lv_obj_set_style_bg_grad_opa(fade, i == 0 ? LV_OPA_50 : LV_OPA_TRANSP, 0);
    }
    lv_obj_t * shadow;
    lv_obj_t * name = make_shadow_label(s_title_box, "ROLLING ROB", big, GOLD, LV_ALIGN_CENTER, 0, K(-40), &shadow);
    if(big_scale != 256) {
        /* past the largest built-in font: that one, magnified */
        lv_obj_t * pair[2] = { name, shadow };
        for(int i = 0; i < 2; i++) {
            lv_obj_set_style_transform_pivot_x(pair[i], LV_PCT(50), 0);
            lv_obj_set_style_transform_pivot_y(pair[i], LV_PCT(50), 0);
            lv_obj_set_style_transform_scale(pair[i], big_scale, 0);
        }
    }
    make_shadow_label(s_title_box, "Vader Edition", FONT(14), lv_color_white(),
                      LV_ALIGN_CENTER, 0, K(-10), NULL);
    /* label + shadow in one box, so they fade together */
    s_title_prompt = lv_obj_create(s_title_box);
    lv_obj_remove_style_all(s_title_prompt);
    lv_obj_set_size(s_title_prompt, K(300), K(20));
    lv_obj_set_clickable(s_title_prompt, false);
    lv_obj_align(s_title_prompt, LV_ALIGN_CENTER, 0, K(40));
    make_shadow_label(s_title_prompt, "Tap or press Space to start", FONT(12), GOLD,
                      LV_ALIGN_CENTER, 0, 0, NULL);
    lv_obj_t * hint = lv_label_create(s_title_box);
    s_title_hint = hint;
    lv_label_set_text(hint, "M music   A look   F fps");
    lv_obj_set_style_text_font(hint, FONT(10), 0);
    lv_obj_set_style_text_color(hint, lv_color_make(0xDD, 0xDD, 0xDD), 0);
    lv_obj_set_style_bg_color(hint, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_40, 0);
    lv_obj_set_style_pad_left(hint, K(3), 0);
    lv_obj_set_style_pad_right(hint, K(3), 0);
    lv_obj_set_style_radius(hint, K(2), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, K(-6));
    lv_obj_set_hidden(s_title_box, true);
}

static lv_obj_t * make_button(lv_obj_t * parent, const char * text, int32_t x_ofs)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, K(68), K(20));
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, x_ofs, K(-4));
    lv_obj_set_style_bg_color(btn, GREEN, 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x45, 0xA0, 0x49), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, GOLD, 0);
    lv_obj_set_style_border_width(btn, K(1), 0);
    lv_obj_set_style_radius(btn, K(3), 0);
    lv_obj_set_style_shadow_width(btn, K(4), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_50, 0);
    lv_obj_set_style_shadow_offset_y(btn, K(2), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FONT(10), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return btn;
}

void hud_title_set_menu(bool on)
{
    s_title_menu = on;
    /* the app's menu holds the settings, and says which keys work */
    if(s_title_hint) lv_obj_set_hidden(s_title_hint, on);
}

void hud_set_menu_button(void (*cb)(void)) { s_menu_cb = cb; }

int32_t hud_title_menu_y(void) { return K(40); }

void hud_create(lv_obj_t * parent)
{
    lv_obj_update_layout(parent);
    s_ui_w = lv_obj_get_width(parent);
    s_ui_h = lv_obj_get_height(parent);
    if(s_ui_w <= 0) s_ui_w = lv_display_get_horizontal_resolution(NULL);
    if(s_ui_h <= 0) s_ui_h = lv_display_get_vertical_resolution(NULL);

    lv_obj_set_scrollable(parent, false);
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);

    lv_obj_t * screen = parent;
    title_create(screen);
    /* Not clickable itself, so taps between the widgets reach the canvas. */
    s_hud_box = lv_obj_create(screen);
    lv_obj_remove_style_all(s_hud_box);
    lv_obj_set_size(s_hud_box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_clickable(s_hud_box, false);
    lv_obj_set_scrollable(s_hud_box, false);
    parent = s_hud_box;

    /* #title */
    make_shadow_label(parent, "Rolling Rob: Vader Edition", FONT(12), GOLD,
                      LV_ALIGN_TOP_RIGHT, K(-5), K(2), NULL);

    /* #levelSelect: "Level:" + a button that opens the picker */
    lv_obj_t * level_label = lv_label_create(parent);
    lv_label_set_text(level_label, "Level:");
    lv_obj_set_style_text_font(level_label, FONT(10), 0);
    lv_obj_set_style_text_color(level_label, lv_color_white(), 0);
    lv_obj_align(level_label, LV_ALIGN_TOP_RIGHT, K(-162), K(21));

    s_level_btn = lv_button_create(parent);
    lv_obj_set_size(s_level_btn, K(152), K(18));
    lv_obj_align(s_level_btn, LV_ALIGN_TOP_RIGHT, K(-5), K(18));
    lv_obj_set_style_bg_color(s_level_btn, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_level_btn, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(s_level_btn, lv_color_make(0x33, 0x33, 0x33), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_level_btn, GOLD, 0);
    lv_obj_set_style_border_width(s_level_btn, K(1), 0);
    lv_obj_set_style_radius(s_level_btn, K(3), 0);
    lv_obj_set_style_shadow_width(s_level_btn, 0, 0);
    lv_obj_set_style_pad_all(s_level_btn, 0, 0);
    lv_obj_set_style_pad_left(s_level_btn, K(5), 0);
    lv_obj_set_style_pad_right(s_level_btn, K(5), 0);
    s_level_btn_label = lv_label_create(s_level_btn);
    lv_obj_set_style_text_font(s_level_btn_label, FONT(10), 0);
    lv_obj_set_style_text_color(s_level_btn_label, lv_color_white(), 0);
    lv_obj_set_width(s_level_btn_label, K(142));
    lv_label_set_long_mode(s_level_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_center(s_level_btn_label);
    lv_obj_add_event_cb(s_level_btn, level_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* #score */
    s_score_label = make_shadow_label(parent, "Distance: 0m", FONT(12), GOLD,
                                      LV_ALIGN_TOP_LEFT, K(5), K(4), &s_score_shadow);

    /* speedrun clock under the score: "Time 12.3 s   Best 10.0 s   Run 1:02.4" */
    s_time_label = lv_label_create(parent);
    lv_obj_set_style_text_font(s_time_label, FONT(10), 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_time_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_time_label, LV_OPA_40, 0);
    lv_obj_set_style_pad_left(s_time_label, K(3), 0);
    lv_obj_set_style_pad_right(s_time_label, K(3), 0);
    lv_obj_set_style_radius(s_time_label, K(2), 0);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_LEFT, K(5), K(22));

    /* #info */
    s_info_label = lv_label_create(parent);
    lv_label_set_long_mode(s_info_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_info_label, K(240));
    lv_obj_set_style_text_align(s_info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_info_label, FONT(10), 0);
    lv_obj_set_style_text_color(s_info_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_info_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_info_label, LV_OPA_60, 0);
    lv_obj_set_style_radius(s_info_label, K(4), 0);
    lv_obj_set_style_pad_top(s_info_label, K(4), 0);
    lv_obj_set_style_pad_bottom(s_info_label, K(4), 0);
    lv_obj_set_style_pad_left(s_info_label, K(8), 0);
    lv_obj_set_style_pad_right(s_info_label, K(8), 0);
    lv_obj_align(s_info_label, LV_ALIGN_BOTTOM_MID, 0, K(-28));

    /* #resetBtn / #nextBtn */
    lv_obj_t * reset_btn = make_button(parent, "Reset Level", K(-37));
    lv_obj_add_event_cb(reset_btn, reset_btn_event_cb, LV_EVENT_CLICKED, NULL);

    s_next_btn = make_button(parent, "Next Level", K(37));
    lv_obj_add_event_cb(s_next_btn, next_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_hidden(s_next_btn, true);

    picker_create();
    hud_update();
}

static bool s_all_hidden;

lv_obj_t * hud_screen_box(void) { return game_title_active() ? s_title_box : s_hud_box; }

void hud_set_hidden(bool hide)
{
    s_all_hidden = hide;
    bool title = game_title_active();
    lv_obj_set_hidden(s_title_box, hide || !title);
    lv_obj_set_hidden(s_hud_box, hide || title);
}

void hud_update(void)
{
    if(s_all_hidden) return;
    bool title = game_title_active();
    if(title == lv_obj_is_hidden(s_title_box)) {
        lv_obj_set_hidden(s_title_box, !title);
        lv_obj_set_hidden(s_hud_box, title);
    }
    if(title) {
        if(s_title_menu != lv_obj_is_hidden(s_title_prompt)) lv_obj_set_hidden(s_title_prompt, s_title_menu);
        /* "press start" breathes: 1.6 s cycle, never fully gone */
        uint32_t t = lv_tick_get() % 1600;
        int32_t tri = t < 800 ? (int32_t)t : 1600 - (int32_t)t;
        lv_obj_set_style_opa(s_title_prompt, (lv_opa_t)(60 + tri * 195 / 800), 0);
        return;
    }

    char score[64];
    snprintf(score, sizeof(score), "Distance: %dm | Goal: %dm", (int)game_get_distance_m(), (int)game_get_goal_m());
    if(strcmp(lv_label_get_text(s_score_label), score) != 0) {
        lv_label_set_text(s_score_label, score);
        lv_label_set_text(s_score_shadow, score);
    }

    const char * msg = game_get_message();
    if(strcmp(s_last_message, msg) != 0) {
        snprintf(s_last_message, sizeof(s_last_message), "%s", msg);
        lv_label_set_text(s_info_label, msg);
    }

    {
        char t[24], b[24], r[24], line[96]; /* three times of 23 and the words: fits */
        format_ms(t, sizeof(t), game_get_level_time_ms());
        int32_t best = game_get_best_time_ms(game_get_level());
        if(best > 0) format_ms(b, sizeof(b), best);
        if(game_is_run_active() && game_get_run_total_ms() > 0) {
            format_ms(r, sizeof(r), game_get_run_total_ms() + (game_is_won() ? 0 : game_get_level_time_ms()));
            snprintf(line, sizeof(line), "Time %s   Best %s   Run %s", t, best > 0 ? b : "-", r);
        }
        else {
            snprintf(line, sizeof(line), "Time %s   Best %s", t, best > 0 ? b : "-");
        }
        if(strcmp(s_last_time, line) != 0) {
            snprintf(s_last_time, sizeof(s_last_time), "%s", line);
            lv_label_set_text(s_time_label, line);
        }
    }

    int32_t level = game_get_level();
    if(level != s_shown_level) {
        s_shown_level = level;
        lv_label_set_text_fmt(s_level_btn_label, "%d - %s  " LV_SYMBOL_DOWN, (int)level + 1, game_get_level_name(level));
    }

    bool show_next = game_should_show_next_button();
    if(show_next == lv_obj_is_hidden(s_next_btn)) lv_obj_set_hidden(s_next_btn, !show_next);
}
