#ifndef ROLLING_ROB_APP_PAGE_H
#define ROLLING_ROB_APP_PAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/*
 * A game's page in the app's menu (app_page.c): every game gets the same
 * one. On the left a picture of the game's screen as it is now, scaled down,
 * with the best score and the keys under it; on the right a list -- Play or
 * Continue first, then New game and whatever the game lets you set (Rolling
 * Rob: the level, the look, the music, the frame counter; Minesweeper: the
 * size, "no guessing") -- and what the game is, when there is room. The
 * picture is drawn again after every change, so a setting shows its effect
 * at once. It is also the game's pause: Esc in a game opens its page.
 *
 * Keys: up / down choose a line, Enter does it, left / right change a value,
 * the key shown on a line does it directly. Esc (app.c) goes back.
 */

typedef struct {
    const char * text;               /* the line, or ... */
    const char * (*label)(void);     /* ... one that changes (Play / Continue) */
    const char * (*value)(void);     /* NULL: the line is an action */
    void (*act)(int dir);            /* 0: Enter or the key; -1 / +1: left / right (values only) */
    bool (*shown)(void);             /* NULL: always */
    int key;                         /* app key (app.h), 0: none */
} page_row_t;

typedef struct {
    const char * name;
    const char * about;              /* a sentence or two */
    const char * keys;               /* the game's keys, a line each; may be recoloured */
    const char * (*best)(void);      /* a line under the picture, NULL: none */
    const page_row_t * rows;
    int n;
    /* Draws the game's screen with the page_pic_* calls below; NULL: no picture. */
    void (*preview)(void);
} page_t;

/* Before the first page_open: the panel on the top layer, hidden. `back`:
 * its close button. */
void page_create(void (*back)(void));
void page_open(const page_t * p);
void page_hide(void);
/* A key while the page is up; false if it is not the page's (Esc). */
bool page_key(int k);
/* Per tick: values and labels that changed. */
void page_refresh(void);

/* The picture. A page's preview() draws the whole screen at full size (the
 * app's picture, w x h) into a frame that starts black: the game's pixels
 * (page_pic_pixels, or bands through page_pic_band -- an rr_band_sink_t),
 * then its widgets over them (page_pic_widgets: an LVGL object and its
 * children, drawn even while hidden). The page then scales it down. */
uint32_t * page_pic_frame(int * w, int * h);
void page_pic_pixels(const uint32_t * px, int stride_px);
void page_pic_band(uint32_t * band, int stride_px, int y0, int rows, void * user);
void page_pic_widgets(lv_obj_t * root);
/* The frame starts as it was left: a preview that does not cover the whole
 * screen fills it first. */
void page_pic_fill(uint32_t rgb);
/* Draws it again (after a change the page did not make itself). */
void page_pic_update(void);
/* Draws it again in `ms` (an action that may be repeated quickly, e.g. a
 * level that has to load: only the last one is drawn). */
void page_pic_later(uint32_t ms);

#endif /* ROLLING_ROB_APP_PAGE_H */
