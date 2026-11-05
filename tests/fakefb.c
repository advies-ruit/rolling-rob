/* A framebuffer for tests (tools/check-fbdev.sh): with LD_PRELOAD, a plain
 * file named by $FAKEFB answers the framebuffer ioctls as a /dev/fb0 would,
 * so the desktop program's fallback (src/platform/sim/fbdev_sim.c) draws
 * into it where there is no real one. $FAKEFB_MODE: WxHxBPP (480x272x32),
 * two pages high; the file must be at least line length x 2H bytes.
 * $FAKEKBD: a FIFO that stands in for /dev/input/event0, a keyboard; what
 * is written to it (struct input_event) are its key presses.
 * $FAKE_MONOTONIC_MS: added to CLOCK_MONOTONIC, a device that has been up
 * that long (its ms no longer fit an int32 after ~25 days).
 *   cc -shared -fPIC -o fakefb.so tests/fakefb.c -ldl */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int is_fake(int fd)
{
    const char * path = getenv("FAKEFB");
    struct stat a, b;
    return path && fstat(fd, &a) == 0 && stat(path, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

static int is_kbd(int fd)
{
    const char * path = getenv("FAKEKBD");
    struct stat a, b;
    return path && fstat(fd, &a) == 0 && stat(path, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

/* open: /dev/input/event0 is $FAKEKBD; other event devices are not there */
static const char * redirect(const char * path)
{
    const char * kbd = getenv("FAKEKBD");
    if(kbd && strncmp(path, "/dev/input/event", 16) == 0) return strcmp(path + 16, "0") == 0 ? kbd : "/nonexistent";
    return path;
}

#define OPEN(name)                                                                   \
    int name(const char * path, int flags, ...)                                      \
    {                                                                                \
        va_list ap;                                                                  \
        va_start(ap, flags);                                                         \
        int m = (flags & O_CREAT) ? va_arg(ap, int) : 0;                             \
        va_end(ap);                                                                  \
        static int (*real)(const char *, int, ...);                                  \
        if(!real) real = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, #name);   \
        return real(redirect(path), flags, m);                                       \
    }
OPEN(open)
OPEN(open64)

static void set_bit(unsigned long * bits, int n)
{
    bits[n / (8 * sizeof(unsigned long))] |= 1UL << (n % (8 * sizeof(unsigned long)));
}

static void mode(unsigned * w, unsigned * h, unsigned * bpp)
{
    *w = 480, *h = 272, *bpp = 32;
    const char * m = getenv("FAKEFB_MODE");
    if(m) sscanf(m, "%ux%ux%u", w, h, bpp);
}

int ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    va_start(ap, req);
    void * arg = va_arg(ap, void *);
    va_end(ap);
    if(is_fake(fd)) {
        unsigned w, h, bpp;
        mode(&w, &h, &bpp);
        if(req == FBIOGET_VSCREENINFO) {
            struct fb_var_screeninfo * v = arg;
            memset(v, 0, sizeof *v);
            v->xres = v->xres_virtual = w;
            v->yres = h;
            v->yres_virtual = 2 * h;
            v->bits_per_pixel = bpp;
            if(bpp == 32) {
                v->red = (struct fb_bitfield){ 16, 8, 0 };
                v->green = (struct fb_bitfield){ 8, 8, 0 };
                v->blue = (struct fb_bitfield){ 0, 8, 0 };
                v->transp = (struct fb_bitfield){ 24, 8, 0 };
            }
            else {
                v->red = (struct fb_bitfield){ 11, 5, 0 };
                v->green = (struct fb_bitfield){ 5, 6, 0 };
                v->blue = (struct fb_bitfield){ 0, 5, 0 };
            }
            return 0;
        }
        if(req == FBIOGET_FSCREENINFO) {
            struct fb_fix_screeninfo * f = arg;
            memset(f, 0, sizeof *f);
            f->line_length = w * bpp / 8;
            f->smem_len = f->line_length * 2 * h;
            f->visual = FB_VISUAL_TRUECOLOR;
            return 0;
        }
        if(req == FBIOBLANK || req == FBIOPAN_DISPLAY || req == FBIOPUT_VSCREENINFO) return 0;
        errno = ENOTTY;
        return -1;
    }
    if(is_kbd(fd)) {
        if((req & ~(unsigned long)IOCSIZE_MASK) == (EVIOCGBIT(0, 0) & ~(unsigned long)IOCSIZE_MASK)) {
            memset(arg, 0, _IOC_SIZE(req));
            set_bit(arg, EV_KEY);
            return 0;
        }
        if((req & ~(unsigned long)IOCSIZE_MASK) == (EVIOCGBIT(EV_KEY, 0) & ~(unsigned long)IOCSIZE_MASK)) {
            memset(arg, 0, _IOC_SIZE(req));
            int keys[] = { KEY_SPACE, KEY_ENTER, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_ESC, KEY_A };
            for(size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) set_bit(arg, keys[i]);
            return 0;
        }
        errno = ENOTTY;
        return -1;
    }
    static int (*real)(int, unsigned long, void *);
    if(!real) real = (int (*)(int, unsigned long, void *))dlsym(RTLD_NEXT, "ioctl");
    return real(fd, req, arg);
}

int clock_gettime(clockid_t id, struct timespec * ts)
{
    static int (*real)(clockid_t, struct timespec *);
    if(!real) real = (int (*)(clockid_t, struct timespec *))dlsym(RTLD_NEXT, "clock_gettime");
    int r = real(id, ts);
    const char * up = getenv("FAKE_MONOTONIC_MS");
    if(r == 0 && up && (id == CLOCK_MONOTONIC || id == CLOCK_MONOTONIC_RAW)) {
        long long ms = atoll(up);
        ts->tv_sec += (time_t)(ms / 1000);
        ts->tv_nsec += (long)(ms % 1000) * 1000000L;
        if(ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
    }
    return r;
}
