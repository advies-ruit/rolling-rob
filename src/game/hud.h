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

#endif /* ROLLING_ROB_HUD_H */
