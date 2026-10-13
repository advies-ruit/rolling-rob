#ifndef ROLLING_ROB_FRACTAL_H
#define ROLLING_ROB_FRACTAL_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Escape-time fractals, computed row by row (no LVGL, no threads: the
 * caller spreads rows over its threads).
 *
 *   Mandelbrot     z = z^2 + c, z starts at 0, c is the pixel
 *   Julia          z = z^2 + c, z starts at the pixel, c is fixed
 *   Burning Ship   z = (|re z| + i |im z|)^2 + c, drawn the usual way up
 *
 * A pixel's value is the "smooth" escape count times 8 (so colours can
 * change within one iteration), or FR_INSIDE when it did not escape in
 * max_iter iterations. With NEON (ARM) a row is computed eight pixels
 * at a time in single precision while that is precise enough for the zoom
 * (fr_float_ok), and in double precision one pixel at a time beyond it.
 */

#define FR_INSIDE 0xFFFFu
#define FR_UNKNOWN 0xFFFEu /* not computed yet (for the caller; never returned) */
#define FR_MAX_VALUE 0xFFFDu

typedef enum { FR_MANDELBROT, FR_JULIA, FR_SHIP, FR_KINDS } fr_kind_t;

typedef struct {
    fr_kind_t kind;
    double cx, cy;    /* the picture's centre; y up */
    double scale;     /* units per pixel */
    double jx, jy;    /* Julia's c */
    int max_iter;
} fr_view_t;

/* n pixels of row y of a w x h picture: x = x0, x0 + step, ...; out[k] for
 * the k-th. */
void fr_row(const fr_view_t * v, int w, int h, int y, int x0, int step, int n, uint16_t * out);

/* Whether single precision still separates neighbouring pixels. */
bool fr_float_ok(const fr_view_t * v);
/* Whether fr_row computes this view in single precision (NEON and fr_float_ok). */
bool fr_uses_float(const fr_view_t * v);
/* Whether double precision still does (zooming further shows blocks). */
bool fr_double_ok(const fr_view_t * v);

const char * fr_kind_name(fr_kind_t k);
/* Pixel-iterations computed since the last call (for measuring). */
uint64_t fr_take_work(void);

#endif /* ROLLING_ROB_FRACTAL_H */
