#ifndef ROLLING_ROB_ART_H
#define ROLLING_ROB_ART_H

#include <stdbool.h>
#include "draw.h"
#include "entities.h"
#include "gfx.h"

/*
 * Painted art (prepared separately, in art/):
 * a background picture per level instead of drawSky, a textured ground with
 * a coloured lip instead of drawMountain, painted trees along the ground, and
 * sprites for Rob, the monsters, the mines, the saucers and Julian. The
 * game itself is untouched; everything else (beam, fireballs, flags, text,
 * particles, buttons) stays the original drawing.
 *
 * The files are looked for in $ROLLING_ROB_ART, else ./art. ROLLING_ROB_ART=0
 * or a missing file gives the original look; the A key switches at run time
 * and the choice is kept with the best times (game.c).
 * Everything is scaled once, when a level is first drawn at a view scale, so
 * a frame only copies and blends pixels.
 */

/* Single-threaded, once per frame before the frame is drawn: loads and
 * scales what the level needs and works out the ground edge. Returns whether
 * this frame is drawn with the art. */
bool rr_art_prepare(const rr_view_t * v, float view_scale, int w, int h);
/* The player's choice, kept in the save file. On draws the art only when
 * its files are there. */
void rr_art_set_enabled(bool on);
bool rr_art_enabled(void);
/* The rendered set (art-3d next to art, made separately) instead of
 * the painted one; what it lacks still comes from the painted set. Whether
 * it is there at all (its rob-3d.png). */
void rr_art_set_rendered(bool on);
bool rr_art_rendered(void);
bool rr_art_rendered_available(void);
/* The picture set a level (0-based) is drawn with in the current choice:
 * its own, or its fallback when its own is missing or broken; -1 for the
 * original look (chosen, or no set left). A set not tried yet counts as
 * there when its files are; nothing is loaded. For the level picker. */
int rr_art_set_for_level(int level);
/* Whether the last prepared frame was drawn with the art (the choice, and
 * the files are there). */
bool rr_art_drawn(void);
/* For a 16 bpp screen: keep the background and ground as dithered RGB565,
 * half the bytes to read per frame. Before the first frame. */
void rr_art_set_rgb565(bool on);
/* The background picture lives on a hardware layer under a colour-keyed
 * screen: its pixels are drawn as
 * GFX_KEY and blends read the picture through gfx_backdrop (gfx.h). Needs
 * the RGB565 copies. */
void rr_art_set_backdrop(bool on);
/* After rr_art_prepare, for the platform that shows the picture: the picture
 * (opaque 0xFFRRGGBB, w x h) and where its top left sits on the screen; gen
 * changes whenever another picture is loaded. False when this frame has no
 * backdrop (original look, menus, backdrop off). */
bool rr_art_backdrop(const uint32_t ** px, int * w, int * h, int * x, int * y, unsigned * gen);

/* Loads one picture from the art directory (PNG or baseline JPEG) as
 * premultiplied 0xAARRGGBB, w x h, malloc'ed; free() it. For the demos. */
bool rr_art_load_file(const char * name, uint32_t ** px, int * w, int * h);

/* The rest may run on several contexts at once (rows interleaved). */
void rr_art_background(gfx_ctx_t * g, const rr_view_t * v); /* picture and trees */
void rr_art_ground(gfx_ctx_t * g, const rr_view_t * v);
void rr_art_monster(gfx_ctx_t * g, const rr_view_t * v, const rr_obstacle_t * obs);
void rr_art_mine(gfx_ctx_t * g, const rr_view_t * v, const rr_mine_t * m);
void rr_art_saucer(gfx_ctx_t * g, const rr_view_t * v, const rr_saucer_t * s);
void rr_art_julian(gfx_ctx_t * g, const rr_view_t * v, const rr_julian_t * j);
void rr_art_rob(gfx_ctx_t * g, const rr_view_t * v);

#endif /* ROLLING_ROB_ART_H */
