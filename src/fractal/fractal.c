/*
 * Escape-time fractals (fractal.h).
 *
 * Smooth count: with a large bailout (|z| >= 256) the fractional part
 * 1 - log2(log2 |z|) makes the bands between iterations vanish.
 *
 * On a small ARM core a dependent float multiply-add takes ~12 cycles, so the
 * NEON loop keeps two vectors (eight pixels) in flight to hide it. Points
 * inside the main cardioid and the period-2 bulb of the Mandelbrot set are
 * found by formula; the double loop also stops when z repeats (Brent's
 * cycle check), which is what makes deep views with much black affordable.
 */

#include "fractal.h"
#include <math.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#define BAILOUT2 65536.0 /* |z|^2 */

static uint64_t s_work; /* pixel-iterations done, including lanes that wait (atomic) */
uint64_t fr_take_work(void) { return __atomic_exchange_n(&s_work, 0, __ATOMIC_RELAXED); }

const char * fr_kind_name(fr_kind_t k)
{
    static const char * const names[FR_KINDS] = { "Mandelbrot", "Julia", "Burning Ship" };
    return (unsigned)k < FR_KINDS ? names[k] : "?";
}

static double coord_mag(const fr_view_t * v)
{
    double m = fabs(v->cx) > fabs(v->cy) ? fabs(v->cx) : fabs(v->cy);
    return m > 1.0 ? m : 1.0;
}

bool fr_float_ok(const fr_view_t * v) { return v->scale > 4e-6 * coord_mag(v); }
bool fr_double_ok(const fr_view_t * v) { return v->scale > 4e-15 * coord_mag(v); }

static bool in_main_parts(double x, double y)
{
    double xq = x - 0.25, q = xq * xq + y * y;
    if(q * (q + xq) <= 0.25 * y * y) return true;           /* cardioid */
    return (x + 1.0) * (x + 1.0) + y * y <= 1.0 / 16.0;    /* period-2 bulb */
}

/* log2 to about 0.005: the exponent plus a parabola through the mantissa
 * (libm's double log2 cost more than the iterations of a typical pixel) */
static inline float fast_log2(float x)
{
    union { float f; uint32_t i; } u = { x };
    float e = (float)(int)((u.i >> 23) & 255) - 128.0f;
    u.i = (u.i & 0x007FFFFFu) | 0x3F800000u; /* the mantissa, 1..2 */
    float m = u.f;
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}

static uint16_t smooth(int n, float mag2)
{
    float mu = (float)n + 1.0f - fast_log2(0.5f * fast_log2(mag2));
    if(mu < 0) mu = 0;
    float v = mu * 8.0f;
    return v >= (float)FR_MAX_VALUE ? FR_MAX_VALUE : (uint16_t)v;
}

/* One pixel, double precision. (px, py): the pixel; y already turned for the ship. */
static uint16_t pixel_d(const fr_view_t * v, double px, double py)
{
    double zx, zy, cx, cy;
    if(v->kind == FR_JULIA) {
        zx = px;
        zy = py;
        cx = v->jx;
        cy = v->jy;
    }
    else {
        if(v->kind == FR_MANDELBROT && in_main_parts(px, py)) return FR_INSIDE;
        zx = zy = 0;
        cx = px;
        cy = py;
    }
    bool ship = v->kind == FR_SHIP;
    double sx = zx, sy = zy; /* Brent: z saved at powers of two */
    int lap = 8;
    for(int n = 0; n < v->max_iter; n++) {
        double x2 = zx * zx, y2 = zy * zy;
        if(x2 + y2 >= BAILOUT2) {
            __atomic_add_fetch(&s_work, (uint64_t)n, __ATOMIC_RELAXED);
            return smooth(n, (float)(x2 + y2));
        }
        double xy = zx * zy;
        if(ship) xy = fabs(xy);
        zy = 2.0 * xy + cy;
        zx = x2 - y2 + cx;
        if(zx == sx && zy == sy) return FR_INSIDE;
        if(n == lap) {
            sx = zx;
            sy = zy;
            lap *= 2;
        }
    }
    return FR_INSIDE;
}

static void row_d(const fr_view_t * v, int w, int h, int y, int x0, int step, int n, uint16_t * out)
{
    double py = v->cy - ((double)y - h * 0.5 + 0.5) * v->scale;
    if(v->kind == FR_SHIP) py = -py;
    for(int k = 0; k < n; k++) {
        double px = v->cx + ((double)(x0 + k * step) - w * 0.5 + 0.5) * v->scale;
        out[k] = pixel_d(v, px, py);
    }
}

#if defined(__ARM_NEON)
static inline bool any4(uint32x4_t a)
{
    uint32x2_t m = vpmax_u32(vget_low_u32(a), vget_high_u32(a));
    m = vpmax_u32(m, m);
    return vget_lane_u32(m, 0) != 0;
}

/* Eight pixels in two vectors, single precision. On a small ARM core the loop
 * is bound by latency (a float multiply, add, compare and select in a row
 * each wait for the one before), so two independent vectors run side by
 * side -- but everything must fit the 16 registers, or GCC moves values to
 * the stack and back every iteration (that was three times slower). So: an
 * escaped pixel's z is frozen (it stays outside the bailout, so the compare
 * alone says which lanes still run; no running mask), the counts subtract
 * the compare's all-ones, and |z|^2 is computed again at the end. `ship`
 * and `julia` are constants at each call: each fractal gets its own loop. */
static inline __attribute__((always_inline)) void eight_k(float32x4_t zx0, float32x4_t zy0, float32x4_t zx1,
                                                          float32x4_t zy1, float32x4_t cx0, float32x4_t cx1,
                                                          float32x4_t cy, int max_iter, bool ship, uint16_t * out)
{
    float32x4_t bail = vdupq_n_f32((float)BAILOUT2);
    uint32x4_t cnt0 = vdupq_n_u32(0), cnt1 = cnt0;
    int i = 0;
#if defined(__arm__) && !defined(__aarch64__)
    /* 32-bit ARM: GCC 10 cannot keep this loop in the 16 registers,
     * so it is written out -- 10 for the state, 6 for the steps, nothing on
     * the stack. Eight iterations at a time, then the escape check. */
    while(i < max_iter) {
        int n = max_iter - i < 8 ? max_iter - i : 8;
        float32x4_t t0, t1, t2, t3, m0, m1;
#define FR_STEP_A                                                                                              \
    "1:\n"                                                                                                     \
    "vmul.f32 %q[t0], %q[zx0], %q[zx0]\n"                                                                      \
    "vmul.f32 %q[t1], %q[zy0], %q[zy0]\n"                                                                      \
    "vmul.f32 %q[t2], %q[zx1], %q[zx1]\n"                                                                      \
    "vmul.f32 %q[t3], %q[zy1], %q[zy1]\n"                                                                      \
    "vadd.f32 %q[m0], %q[t0], %q[t1]\n"                                                                        \
    "vadd.f32 %q[m1], %q[t2], %q[t3]\n"                                                                        \
    "vsub.f32 %q[t0], %q[t0], %q[t1]\n"                                                                        \
    "vsub.f32 %q[t2], %q[t2], %q[t3]\n"                                                                        \
    "vmul.f32 %q[t1], %q[zx0], %q[zy0]\n"                                                                      \
    "vmul.f32 %q[t3], %q[zx1], %q[zy1]\n"                                                                      \
    "vcgt.f32 %q[m0], %q[bail], %q[m0]\n"                                                                      \
    "vcgt.f32 %q[m1], %q[bail], %q[m1]\n"                                                                      \
    "vadd.f32 %q[t0], %q[t0], %q[cx0]\n"                                                                       \
    "vadd.f32 %q[t2], %q[t2], %q[cx1]\n"
#define FR_STEP_B                                                                                              \
    "vadd.f32 %q[t1], %q[t1], %q[t1]\n"                                                                        \
    "vadd.f32 %q[t3], %q[t3], %q[t3]\n"                                                                        \
    "vadd.f32 %q[t1], %q[t1], %q[cy]\n"                                                                        \
    "vadd.f32 %q[t3], %q[t3], %q[cy]\n"                                                                        \
    "vsub.i32 %q[c0], %q[c0], %q[m0]\n"                                                                        \
    "vsub.i32 %q[c1], %q[c1], %q[m1]\n"                                                                        \
    "vbit %q[zx0], %q[t0], %q[m0]\n"                                                                           \
    "vbit %q[zy0], %q[t1], %q[m0]\n"                                                                           \
    "vbit %q[zx1], %q[t2], %q[m1]\n"                                                                           \
    "vbit %q[zy1], %q[t3], %q[m1]\n"                                                                           \
    "subs %[n], %[n], #1\n"                                                                                    \
    "bne 1b\n"
#define FR_OPS                                                                                                 \
    : [zx0] "+w"(zx0), [zy0] "+w"(zy0), [zx1] "+w"(zx1), [zy1] "+w"(zy1), [c0] "+w"(cnt0), [c1] "+w"(cnt1),   \
      [n] "+r"(n), [t0] "=&w"(t0), [t1] "=&w"(t1), [t2] "=&w"(t2), [t3] "=&w"(t3), [m0] "=&w"(m0),             \
      [m1] "=&w"(m1)                                                                                           \
    : [cx0] "w"(cx0), [cx1] "w"(cx1), [cy] "w"(cy), [bail] "w"(bail)                                           \
    : "cc"
        int done = n;
        if(ship) __asm__ volatile(FR_STEP_A "vabs.f32 %q[t1], %q[t1]\n"
                                            "vabs.f32 %q[t3], %q[t3]\n" FR_STEP_B FR_OPS);
        else __asm__ volatile(FR_STEP_A FR_STEP_B FR_OPS);
#undef FR_STEP_A
#undef FR_STEP_B
#undef FR_OPS
        i += done;
        if(!any4(vorrq_u32(vreinterpretq_u32_f32(m0), vreinterpretq_u32_f32(m1)))) break;
    }
#else
    for(i = 0; i < max_iter; i++) {
        float32x4_t x20 = vmulq_f32(zx0, zx0), y20 = vmulq_f32(zy0, zy0);
        float32x4_t x21 = vmulq_f32(zx1, zx1), y21 = vmulq_f32(zy1, zy1);
        uint32x4_t in0 = vcltq_f32(vaddq_f32(x20, y20), bail), in1 = vcltq_f32(vaddq_f32(x21, y21), bail);
        if((i & 7) == 7 && !any4(vorrq_u32(in0, in1))) break;
        cnt0 = vsubq_u32(cnt0, in0);
        cnt1 = vsubq_u32(cnt1, in1);
        float32x4_t xy0 = vmulq_f32(zx0, zy0), xy1 = vmulq_f32(zx1, zy1);
        if(ship) {
            xy0 = vabsq_f32(xy0);
            xy1 = vabsq_f32(xy1);
        }
        zy0 = vbslq_f32(in0, vaddq_f32(vaddq_f32(xy0, xy0), cy), zy0);
        zy1 = vbslq_f32(in1, vaddq_f32(vaddq_f32(xy1, xy1), cy), zy1);
        zx0 = vbslq_f32(in0, vaddq_f32(vsubq_f32(x20, y20), cx0), zx0);
        zx1 = vbslq_f32(in1, vaddq_f32(vsubq_f32(x21, y21), cx1), zx1);
    }
#endif
    __atomic_add_fetch(&s_work, (uint64_t)i * 8, __ATOMIC_RELAXED);
    /* a lane whose z is still inside the bailout never escaped; the others
     * escaped after cnt iterations with that |z| */
    uint32_t c[8];
    float m[8];
    vst1q_u32(c, cnt0);
    vst1q_u32(c + 4, cnt1);
    vst1q_f32(m, vmlaq_f32(vmulq_f32(zx0, zx0), zy0, zy0));
    vst1q_f32(m + 4, vmlaq_f32(vmulq_f32(zx1, zx1), zy1, zy1));
    for(int k = 0; k < 8; k++) out[k] = m[k] < (float)BAILOUT2 ? FR_INSIDE : smooth((int)c[k], m[k]);
}

/* Eight pixels at (xs[], py), single precision. */
static void eight_f(const fr_view_t * v, const float * xs, float py, uint16_t * out)
{
    float32x4_t px0 = vld1q_f32(xs), px1 = vld1q_f32(xs + 4), pyv = vdupq_n_f32(py), zero = vdupq_n_f32(0);
    if(v->kind == FR_JULIA) {
        float32x4_t jx = vdupq_n_f32((float)v->jx), jy = vdupq_n_f32((float)v->jy);
        eight_k(px0, pyv, px1, pyv, jx, jx, jy, v->max_iter, false, out);
    }
    else if(v->kind == FR_SHIP) eight_k(zero, zero, zero, zero, px0, px1, pyv, v->max_iter, true, out);
    else eight_k(zero, zero, zero, zero, px0, px1, pyv, v->max_iter, false, out);
}

/* Whether all eight points are in the main cardioid or the period-2 bulb. */
static bool eight_inside(const float * xs, float y)
{
    float32x4_t y2 = vdupq_n_f32(y * y), qy = vdupq_n_f32(0.25f * y * y);
    uint32x4_t in[2];
    for(int h = 0; h < 2; h++) {
        float32x4_t x = vld1q_f32(xs + 4 * h);
        float32x4_t xq = vsubq_f32(x, vdupq_n_f32(0.25f));
        float32x4_t q = vmlaq_f32(y2, xq, xq);
        uint32x4_t card = vcleq_f32(vmulq_f32(q, vaddq_f32(q, xq)), qy);
        float32x4_t x1 = vaddq_f32(x, vdupq_n_f32(1.0f));
        uint32x4_t bulb = vcleq_f32(vmlaq_f32(y2, x1, x1), vdupq_n_f32(1.0f / 16.0f));
        in[h] = vorrq_u32(card, bulb);
    }
    uint32x4_t a = vandq_u32(in[0], in[1]);
    uint32x2_t m = vpmin_u32(vget_low_u32(a), vget_high_u32(a));
    m = vpmin_u32(m, m);
    return vget_lane_u32(m, 0) != 0;
}

static void row_f(const fr_view_t * v, int w, int h, int y, int x0, int step, int n, uint16_t * out)
{
    double pyd = v->cy - ((double)y - h * 0.5 + 0.5) * v->scale;
    if(v->kind == FR_SHIP) pyd = -pyd;
    /* single precision is enough here by definition (fr_float_ok) */
    double px0 = v->cx + ((double)x0 - w * 0.5 + 0.5) * v->scale, dx = step * v->scale;
    float xs[8];
    uint16_t tmp[8];
    for(int k = 0; k < n; k += 8) {
        int m = n - k < 8 ? n - k : 8;
        for(int j = 0; j < 8; j++) xs[j] = (float)(px0 + (double)(k + (j < m ? j : m - 1)) * dx);
        if(v->kind == FR_MANDELBROT && eight_inside(xs, (float)pyd)) {
            for(int j = 0; j < m; j++) out[k + j] = FR_INSIDE;
            continue;
        }
        eight_f(v, xs, (float)pyd, tmp);
        for(int j = 0; j < m; j++) out[k + j] = tmp[j];
    }
}
#endif

bool fr_uses_float(const fr_view_t * v)
{
#if defined(__ARM_NEON)
    return fr_float_ok(v);
#else
    (void)v;
    return false;
#endif
}

void fr_row(const fr_view_t * v, int w, int h, int y, int x0, int step, int n, uint16_t * out)
{
#if defined(__ARM_NEON)
    if(fr_float_ok(v)) {
        row_f(v, w, h, y, x0, step, n, out);
        return;
    }
#endif
    row_d(v, w, h, y, x0, step, n, out);
}
