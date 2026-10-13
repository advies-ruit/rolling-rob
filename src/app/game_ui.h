#ifndef ROLLING_ROB_GAME_UI_H
#define ROLLING_ROB_GAME_UI_H

#include "lvgl.h"

/*
 * Widgets the small games (Snake, Breakout, Minesweeper) put over their
 * canvas (pix.h): a layer on LVGL's top layer, labels, buttons and the
 * message box in the middle. Sizes are screen pixels -- each game scales
 * its 480x272 layout to fill the screen, like Tetris.
 */

/* Full screen on the top layer, hidden; taps go through to the canvas. */
lv_obj_t * gu_layer(void);
lv_obj_t * gu_label(lv_obj_t * parent, const char * text, int font_px, lv_color_t c);
lv_obj_t * gu_button(lv_obj_t * parent, const char * text, int x, int y, int w, int h, int font_px,
                     lv_event_cb_t cb, void * user);
/* The pause / game over box: recoloured text, gold edge, centred; tapping
 * it calls cb. */
lv_obj_t * gu_message(lv_obj_t * parent, int font_px, int unit, lv_event_cb_t cb);
/* Sets the text and shows it, or hides it for NULL (only when it changes). */
void gu_message_set(lv_obj_t * msg, const char * text);
/* lv_label_set_text only when the text changes. */
void gu_set_text(lv_obj_t * label, const char * text);

#endif /* ROLLING_ROB_GAME_UI_H */
