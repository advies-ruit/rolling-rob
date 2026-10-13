#ifndef ROLLING_ROB_HUD_H
#define ROLLING_ROB_HUD_H

#include "lvgl.h"

/* Fase 5: real LVGL widgets (title, distance, info banner, level
 * dropdown, reset/next buttons) layered on top of the game canvas.
 * Platform-independent - the same on every platform. */

void hud_create(lv_obj_t * parent);
void hud_update(void);

/* True while the full-screen level picker is open (game is frozen then). */
bool hud_is_modal_open(void);

/* Before hud_create: show an art-look toggle in the level picker (for builds
 * without an A key at hand, e.g. the web version on a phone). The music
 * toggle appears by itself when game_music_available(). */
void hud_enable_art_tile(bool on);

/* The platform puts a menu on the title screen: the "press Space" prompt
 * goes, and hud_title_menu_y() is where it sat (offset from the centre). */
void hud_title_set_menu(bool on);
int32_t hud_title_menu_y(void);

/* Before hud_create: a "Menu" button in the level picker's header that
 * closes it and calls cb (the app's way back to its menu on touch screens). */
void hud_set_menu_button(void (*cb)(void));

/* The level picker from the keyboard: open it, move the cursor (the arrows),
 * play the level under it (Enter) or close it (Esc). The callback hears
 * every close by the player -- with a level chosen, or backed out of. */
void hud_picker_open(void);
/* ... with the cursor on `level`. */
void hud_picker_open_at(int32_t level);
void hud_picker_move(int dx, int dy);
void hud_picker_choose(void);
void hud_picker_back(void);
void hud_picker_set_closed_cb(void (*cb)(bool chose));

/* Hides the whole game HUD, title included (the app's 3D demo draws over
 * the screen and keeps only its own widgets); false shows it as it was. */
void hud_set_hidden(bool hide);

/* What the HUD puts over the game now: the title's box on the title screen,
 * else the game's (for a picture of the screen, app_page.h). */
lv_obj_t * hud_screen_box(void);

#endif /* ROLLING_ROB_HUD_H */
