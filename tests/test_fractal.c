/* Fractals: points known to be inside or outside, Julia, the precision
 * limits. */

#include "check.h"
#include "fractal.h"

static uint16_t at(fr_kind_t kind, double x, double y)
{
    fr_view_t v = { kind, x, y, 1e-3, -0.8, 0.156, 500 };
    uint16_t out = 0;
    fr_row(&v, 1, 1, 0, 0, 1, 1, &out);
    return out;
}

static void test_mandelbrot(void)
{
    CHECK_EQ(at(FR_MANDELBROT, 0.0, 0.0), FR_INSIDE);
    CHECK_EQ(at(FR_MANDELBROT, -1.0, 0.0), FR_INSIDE); /* the period-2 bulb */
    CHECK_EQ(at(FR_MANDELBROT, -0.1, 0.1), FR_INSIDE);
    CHECK(at(FR_MANDELBROT, 1.0, 1.0) <= FR_MAX_VALUE);
    CHECK(at(FR_MANDELBROT, 0.5, 0.0) <= FR_MAX_VALUE);
    /* further out escapes sooner */
    CHECK(at(FR_MANDELBROT, 3.0, 3.0) < at(FR_MANDELBROT, 0.3, 0.5));
}

static void test_julia_and_ship(void)
{
    CHECK(at(FR_JULIA, 2.0, 2.0) <= FR_MAX_VALUE);
    CHECK(at(FR_SHIP, 2.0, 2.0) <= FR_MAX_VALUE);
    CHECK_EQ(at(FR_SHIP, 0.0, 0.0), FR_INSIDE);
    for(int k = 0; k < FR_KINDS; k++) CHECK(fr_kind_name((fr_kind_t)k) != NULL);
}

static void test_row(void)
{
    /* A row across the set: the ends escape, the middle does not. */
    enum { W = 64 };
    fr_view_t v = { FR_MANDELBROT, -0.5, 0.0, 4.0 / W, 0, 0, 200 };
    uint16_t out[W];
    fr_row(&v, W, 1, 0, 0, 1, W, out);
    CHECK(out[0] <= FR_MAX_VALUE);
    CHECK(out[W - 1] <= FR_MAX_VALUE);
    CHECK_EQ(out[W / 2], FR_INSIDE);
    /* every other pixel: the same values */
    uint16_t half[W / 2];
    fr_row(&v, W, 1, 0, 0, 2, W / 2, half);
    for(int i = 0; i < W / 2; i++) CHECK_EQ(half[i], out[2 * i]);
    CHECK(fr_take_work() > 0);
    CHECK_EQ(fr_take_work(), 0);
}

static void test_precision(void)
{
    fr_view_t v = { FR_MANDELBROT, -0.5, 0.0, 1e-3, 0, 0, 100 };
    CHECK(fr_float_ok(&v));
    CHECK(fr_double_ok(&v));
    v.scale = 1e-12;
    CHECK(!fr_float_ok(&v));
    CHECK(fr_double_ok(&v));
    v.scale = 1e-20;
    CHECK(!fr_double_ok(&v));
}

int main(void)
{
    RUN(test_mandelbrot);
    RUN(test_julia_and_ship);
    RUN(test_row);
    RUN(test_precision);
    return check_result();
}
