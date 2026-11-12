/* The desktop program on a Linux device with only /dev/fb0: no desktop, no
 * KMS/DRM, so SDL has no screen. The picture then goes straight into the
 * framebuffer (LVGL's fbdev driver), keys come from keyboards in /dev/input
 * (evdev) and a touchscreen there is the pointer. main_sim.c falls back to
 * this when SDL finds no screen; ROLLING_ROB_FBDEV=/dev/fbN asks for it. */
#pragma once

#include "lvgl.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stddef.h>

/* The framebuffer to use: $ROLLING_ROB_FBDEV, else /dev/fb0. */
const char * fbdev_path(void);

/* Can the picture go there? Its size in *w, *h; else false and why. */
bool fbdev_probe(int * w, int * h, char * why, size_t why_n);

/* The display: a pic_w x pic_h picture scaled to fill the framebuffer
 * (its shape kept, unless that differs by at most 1 %; ROLLING_ROB_FB_ZOOM=
 * 1..4: that many times instead, as far as it fits), in the middle, every
 * pixel opaque. Also switches the console to graphics (no kernel
 * messages or cursor over the picture) until the program ends. */
lv_display_t * fbdev_display_create(int pic_w, int pic_h);

/* A touchscreen in /dev/input as LVGL's pointer, if there is one. */
void fbdev_touch_create(lv_display_t * disp);

/* Reads the keyboards (plugged in later ones too) into keys, indexed by
 * SDL scancode as SDL_GetKeyboardState is; pressed() for every press. */
void fbdev_poll_keys(Uint8 * keys, void (*pressed)(SDL_Scancode sc));

/* The time spent writing into the framebuffer since the last call, ms. */
double fbdev_take_flush_ms(void);

/* Is that writing done on a thread of its own (with more than one core),
 * next to LVGL's drawing rather than part of it? */
bool fbdev_writes_apart(void);
