#ifndef ROLLING_ROB_PIX_H
#define ROLLING_ROB_PIX_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/*
 * Pixels for the app's small games (Snake, Breakout, Minesweeper): one
 * opaque full-screen canvas they share -- only one is on screen at a time --
 * with a second buffer for each game's background, and a few drawing
 * routines. A game draws what changed straight into the canvas and
 * invalidates just that rectangle, so a moving ball costs a few rows, not
 * a frame.
 *
 * Colours are 0xRRGGBB. Everything is clipped to the clip rectangle
 * (pix_clip), which is the whole canvas unless set.
 */

typedef struct {
    uint32_t * px;
    int w, h, stride;          /* stride in pixels */
    int cx1, cy1, cx2, cy2;    /* clip, x2 / y2 exclusive */
} pix_t;

/* From app_init: the canvas on `parent`, w x h, hidden. */
void pix_stage_create(lv_obj_t * parent, int w, int h);
pix_t * pix_stage(void);       /* NULL if there was no memory */
uint32_t * pix_stage_bg(void); /* the background buffer, same size and stride */
void pix_stage_show(bool on);
/* Presses on the canvas for the game shown: LV_EVENT_PRESSED, PRESSING,
 * RELEASED, SHORT_CLICKED, LONG_PRESSED, at screen pixel x, y. */
void pix_stage_set_input(void (*cb)(lv_event_code_t code, int x, int y));
/* The rectangle changed: LVGL redraws it. */
void pix_flush(int x, int y, int w, int h);
void pix_flush_all(void);

void pix_clip(pix_t * p, int x, int y, int w, int h);
void pix_noclip(pix_t * p);

uint32_t pix_mix(uint32_t a, uint32_t b, int t); /* t/256 of b over a */
void pix_rect(pix_t * p, int x, int y, int w, int h, uint32_t rgb);
void pix_rect_over(pix_t * p, int x, int y, int w, int h, uint32_t rgb, int t);
/* Raised: light top and left, dark bottom and right. */
void pix_block(pix_t * p, int x, int y, int w, int h, uint32_t rgb);
/* Anti-aliased. */
void pix_disc(pix_t * p, float cx, float cy, float r, uint32_t rgb);
/* A digit 0-9 from a 5x7 font, each dot `scale` pixels, top left at x, y. */
void pix_digit(pix_t * p, int x, int y, int scale, int digit, uint32_t rgb);
/* Copies the background back (the clip applies). */
void pix_restore(pix_t * p, int x, int y, int w, int h);
/* A vertical gradient over the whole background buffer. */
void pix_bg_gradient(uint32_t top, uint32_t bottom);

#endif /* ROLLING_ROB_PIX_H */
