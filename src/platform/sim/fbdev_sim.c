/* The desktop program on a Linux device with only /dev/fb0 (fbdev_sim.h). */
#include "fbdev_sim.h"

#if LV_USE_LINUX_FBDEV && LV_USE_EVDEV

#include "src/display/lv_display_private.h" /* the driver's own flush_cb, wrapped below */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int s_fb_w, s_fb_h, s_bpp; /* the framebuffer's visible size */
static int s_ox, s_oy;            /* where the picture sits in it */
static bool s_rot180;             /* ROLLING_ROB_ROTATE=180 */
static lv_display_flush_cb_t s_driver_flush;

const char * fbdev_path(void)
{
    const char * p = getenv("ROLLING_ROB_FBDEV");
    return p && p[0] && strcmp(p, "1") != 0 ? p : "/dev/fb0";
}

bool fbdev_probe(int * w, int * h, char * why, size_t why_n)
{
    const char * path = fbdev_path();
    int fd = open(path, O_RDWR);
    if(fd < 0) {
        snprintf(why, why_n, "%s: %s", path, strerror(errno));
        return false;
    }
    struct fb_var_screeninfo v;
    bool ok = ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0;
    close(fd);
    if(!ok) {
        snprintf(why, why_n, "%s: not a framebuffer (%s)", path, strerror(errno));
        return false;
    }
    if(v.bits_per_pixel != 16 && v.bits_per_pixel != 32) {
        snprintf(why, why_n, "%s: %u bits a pixel (16 or 32 wanted)", path, v.bits_per_pixel);
        return false;
    }
    *w = (int)v.xres;
    *h = (int)v.yres;
    return true;
}

/* ---- console: graphics mode, so kernel messages and the cursor stay off
 * the picture; text again when the program ends, however it ends ---- */

static int s_tty = -1;
static struct termios s_tty_mode;
static bool s_tty_mode_saved;

static void console_text(void)
{
    if(s_tty < 0) return;
    ioctl(s_tty, KDSETMODE, KD_TEXT);
    if(s_tty_mode_saved) {
        tcflush(s_tty, TCIFLUSH); /* the keys typed while playing, not for the shell */
        tcsetattr(s_tty, TCSANOW, &s_tty_mode);
    }
}

static void on_signal(int sig)
{
    console_text();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void console_graphics(void)
{
    const char * ttys[] = { NULL, "/dev/tty", "/dev/tty0" };
    int err = 0;
    for(size_t i = 0; i < sizeof ttys / sizeof ttys[0]; i++) {
        int fd = ttys[i] ? open(ttys[i], O_RDWR | O_CLOEXEC) : (isatty(0) ? dup(0) : -1);
        if(fd < 0) continue;
        if(ioctl(fd, KDSETMODE, KD_GRAPHICS) == 0) {
            fprintf(stderr, "fbdev: console %s in graphics mode\n", ttys[i] ? ttys[i] : "on stdin");
            s_tty = fd;
            /* the keys come from /dev/input; the console gets them too: no
             * echo, no lines, but Ctrl+C still ends the program */
            if(tcgetattr(fd, &s_tty_mode) == 0) {
                s_tty_mode_saved = true;
                struct termios quiet = s_tty_mode;
                quiet.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
                tcsetattr(fd, TCSANOW, &quiet);
            }
            atexit(console_text);
            signal(SIGINT, on_signal);
            signal(SIGTERM, on_signal);
            signal(SIGHUP, on_signal);
            signal(SIGQUIT, on_signal);
            signal(SIGSEGV, on_signal);
            signal(SIGABRT, on_signal);
            return;
        }
        err = errno;
        close(fd);
    }
    fprintf(stderr, "fbdev: the console stays in text mode, kernel messages and its cursor may show%s%s\n",
            err ? ": " : "", err ? strerror(err) : "");
}

/* ---- the picture ---- */

/* Every pixel opaque: on some devices the framebuffer's alpha byte counts in
 * the display mixer, and LVGL's XRGB8888 leaves it as it is. */
static void opaque_rows(uint8_t * px, int32_t w, int32_t h, uint32_t stride)
{
    for(int32_t y = 0; y < h; y++) {
        uint32_t * p = (uint32_t *)(px + (size_t)y * stride);
        for(int32_t x = 0; x < w; x++) p[x] |= 0xFF000000u;
    }
}

/* 180 degrees in place: the rows in reverse order, each row reversed */
static void rotate180(uint8_t * px, int32_t w, int32_t h, uint32_t stride, uint32_t px_size)
{
    for(int32_t y = 0; y < (h + 1) / 2; y++) {
        uint8_t * a = px + (size_t)y * stride;
        uint8_t * b = px + (size_t)(h - 1 - y) * stride;
        for(int32_t x = 0; x < (y == h - 1 - y ? w / 2 : w); x++) {
            uint8_t * pa = a + (size_t)x * px_size;
            uint8_t * pb = b + (size_t)(w - 1 - x) * px_size;
            uint8_t t[4];
            memcpy(t, pa, px_size);
            memcpy(pa, pb, px_size);
            memcpy(pb, t, px_size);
        }
    }
}

static double s_flush_ms; /* time spent in flush_cb, for the log */

static double now_ms_f(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

double fbdev_take_flush_ms(void)
{
    double ms = s_flush_ms;
    s_flush_ms = 0;
    return ms;
}

static void flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px)
{
    const double t0 = now_ms_f();
    const lv_color_format_t cf = lv_display_get_color_format(disp);
    const int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    const uint32_t stride = lv_draw_buf_width_to_stride(w, cf);
    if(s_bpp == 32) opaque_rows(px, w, h, stride);
    lv_area_t a = *area;
    if(s_rot180) {
        rotate180(px, w, h, stride, lv_color_format_get_size(cf));
        const int32_t pw = lv_display_get_horizontal_resolution(disp);
        const int32_t ph = lv_display_get_vertical_resolution(disp);
        lv_area_set(&a, pw - 1 - area->x2, ph - 1 - area->y2, pw - 1 - area->x1, ph - 1 - area->y1);
    }
    lv_area_move(&a, s_ox, s_oy); /* into the middle of the framebuffer */
    s_driver_flush(disp, &a, px);
    s_flush_ms += now_ms_f() - t0;
}

/* The framebuffer around the picture: opaque black once. */
static void clear_fb(void)
{
    int fd = open(fbdev_path(), O_RDWR);
    if(fd < 0) return;
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK); /* not every driver knows it: quietly */
    if(ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0 && ioctl(fd, FBIOGET_FSCREENINFO, &f) == 0) {
        uint8_t * m = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if(m != MAP_FAILED) {
            for(uint32_t y = 0; y < v.yres; y++) {
                uint8_t * row = m + (size_t)(y + v.yoffset) * f.line_length + (size_t)v.xoffset * (v.bits_per_pixel / 8);
                if((size_t)(row - m) + (size_t)v.xres * (v.bits_per_pixel / 8) > f.smem_len) break;
                if(v.bits_per_pixel == 32)
                    for(uint32_t x = 0; x < v.xres; x++) ((uint32_t *)row)[x] = 0xFF000000u;
                else memset(row, 0, (size_t)v.xres * (v.bits_per_pixel / 8));
            }
            munmap(m, f.smem_len);
        }
    }
    close(fd);
}

lv_display_t * fbdev_display_create(int pic_w, int pic_h)
{
    char why[256];
    if(!fbdev_probe(&s_fb_w, &s_fb_h, why, sizeof why)) return NULL;
    const char * rot = getenv("ROLLING_ROB_ROTATE");
    s_rot180 = rot && atoi(rot) == 180;
    console_graphics();
    clear_fb();
    lv_display_t * disp = lv_linux_fbdev_create();
    if(disp) lv_linux_fbdev_set_skip_unblank(disp, true); /* done in clear_fb, without a message */
    if(disp == NULL || lv_linux_fbdev_set_file(disp, fbdev_path()) != LV_RESULT_OK) return NULL;
    s_bpp = lv_color_format_get_size(lv_display_get_color_format(disp)) * 8;
    /* the display is the picture; the flush puts it in the middle */
    lv_display_set_resolution(disp, pic_w, pic_h);
    s_ox = (s_fb_w - pic_w) / 2;
    s_oy = (s_fb_h - pic_h) / 2;
    s_driver_flush = disp->flush_cb;
    lv_display_set_flush_cb(disp, flush_cb);
    fprintf(stderr, "video: %s, %dx%d at %d bits a pixel: picture %dx%d%s\n", fbdev_path(), s_fb_w, s_fb_h, s_bpp,
            pic_w, pic_h, s_rot180 ? ", turned 180 degrees" : "");
    return disp;
}

/* ---- input ---- */

static bool has_bit(const unsigned long * bits, int n)
{
    const int b = (int)(8 * sizeof(unsigned long));
    return (bits[n / b] >> (n % b)) & 1UL;
}

#define BITS(n) (((n) + 8 * (int)sizeof(unsigned long) - 1) / (8 * (int)sizeof(unsigned long)))

static bool is_touchscreen(int fd)
{
    unsigned long ev[BITS(EV_MAX + 1)] = { 0 }, abs[BITS(ABS_MAX + 1)] = { 0 }, key[BITS(KEY_MAX + 1)] = { 0 };
    if(ioctl(fd, EVIOCGBIT(0, sizeof ev), ev) < 0 || !has_bit(ev, EV_ABS)) return false;
    if(ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs) < 0 || !has_bit(abs, ABS_X)) return false;
    return ioctl(fd, EVIOCGBIT(EV_KEY, sizeof key), key) >= 0 && has_bit(key, BTN_TOUCH);
}

static bool is_keyboard(int fd)
{
    unsigned long ev[BITS(EV_MAX + 1)] = { 0 }, key[BITS(KEY_MAX + 1)] = { 0 };
    if(ioctl(fd, EVIOCGBIT(0, sizeof ev), ev) < 0 || !has_bit(ev, EV_KEY)) return false;
    if(ioctl(fd, EVIOCGBIT(EV_KEY, sizeof key), key) < 0) return false;
    return has_bit(key, KEY_SPACE) || has_bit(key, KEY_ENTER) || has_bit(key, KEY_LEFT);
}

void fbdev_touch_create(lv_display_t * disp)
{
    for(int i = 0; i < 32; i++) {
        char path[32];
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if(fd < 0) continue;
        struct input_absinfo ax, ay;
        bool touch = is_touchscreen(fd) && ioctl(fd, EVIOCGABS(ABS_X), &ax) == 0 && ioctl(fd, EVIOCGABS(ABS_Y), &ay) == 0;
        close(fd);
        if(!touch) continue;
        lv_indev_t * indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, path);
        if(indev == NULL) continue;
        lv_indev_set_display(indev, disp);
        /* the touchscreen covers the whole framebuffer, the picture its middle */
        const int pw = lv_display_get_horizontal_resolution(disp);
        const int ph = lv_display_get_vertical_resolution(disp);
        const double rx = (double)(ax.maximum - ax.minimum) / s_fb_w, ry = (double)(ay.maximum - ay.minimum) / s_fb_h;
        int x0 = ax.minimum + (int)(rx * s_ox), x1 = ax.minimum + (int)(rx * (s_ox + pw));
        int y0 = ay.minimum + (int)(ry * s_oy), y1 = ay.minimum + (int)(ry * (s_oy + ph));
        const char * flip = getenv("ROLLING_ROB_TOUCH_FLIP");
        if(s_rot180 != (flip && atoi(flip) == 1)) {
            int t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
        }
        lv_evdev_set_calibration(indev, x0, y0, x1, y1);
        fprintf(stderr, "touch: %s\n", path);
        return;
    }
}

/* Linux key codes of a..z (not in order) */
static const unsigned short s_letters[26] = {
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
};

static SDL_Scancode scancode_of(unsigned code)
{
    for(int i = 0; i < 26; i++)
        if(s_letters[i] == code) return (SDL_Scancode)(SDL_SCANCODE_A + i);
    if(code >= KEY_1 && code <= KEY_9) return (SDL_Scancode)(SDL_SCANCODE_1 + (code - KEY_1));
    switch(code) {
        case KEY_0: return SDL_SCANCODE_0;
        case KEY_LEFT: return SDL_SCANCODE_LEFT;
        case KEY_RIGHT: return SDL_SCANCODE_RIGHT;
        case KEY_UP: return SDL_SCANCODE_UP;
        case KEY_DOWN: return SDL_SCANCODE_DOWN;
        case KEY_SPACE: return SDL_SCANCODE_SPACE;
        case KEY_ENTER: return SDL_SCANCODE_RETURN;
        case KEY_KPENTER: return SDL_SCANCODE_KP_ENTER;
        case KEY_ESC: return SDL_SCANCODE_ESCAPE;
        case KEY_BACKSPACE: return SDL_SCANCODE_BACKSPACE;
        case KEY_LEFTCTRL: return SDL_SCANCODE_LCTRL;
        case KEY_RIGHTCTRL: return SDL_SCANCODE_RCTRL;
        case KEY_LEFTSHIFT: return SDL_SCANCODE_LSHIFT;
        case KEY_RIGHTSHIFT: return SDL_SCANCODE_RSHIFT;
        default: return SDL_SCANCODE_UNKNOWN;
    }
}

#define MAX_KEYBOARDS 8
static int s_kbd[32];   /* fd + 1 by event number, 0: not open */
/* The other event devices stay open, unread, so the scan skips them: on some
 * devices close() on a USB input device takes 40-100 ms, and a USB keyboard
 * often has two more (consumer and system control) -- every two seconds a
 * visible hitch. */
static int s_other[32]; /* fd + 1, 0: none */
static uint32_t s_next_scan_ms;
static bool s_scanned; /* the first scan is due at once, whatever the clock says */

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* keyboards plugged in at any time: look again every two seconds */
static void scan_keyboards(void)
{
    int open_n = 0;
    for(int i = 0; i < 32; i++) open_n += s_kbd[i] != 0;
    for(int i = 0; i < 32; i++) {
        if(!s_other[i]) continue;
        struct input_event e[16];
        ssize_t n;
        while((n = read(s_other[i] - 1, e, sizeof e)) > 0) {} /* nothing wanted from it */
        if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) { /* unplugged */
            close(s_other[i] - 1);
            s_other[i] = 0;
        }
    }
    for(int i = 0; i < 32 && open_n < MAX_KEYBOARDS; i++) {
        if(s_kbd[i] || s_other[i]) continue;
        char path[32];
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if(fd < 0) continue;
        if(is_keyboard(fd) && !is_touchscreen(fd)) {
            s_kbd[i] = fd + 1;
            open_n++;
            fprintf(stderr, "keyboard: %s\n", path);
        }
        else s_other[i] = fd + 1;
    }
}

void fbdev_poll_keys(Uint8 * keys, void (*pressed)(SDL_Scancode sc))
{
    uint32_t t = now_ms();
    /* t is the monotonic clock in ms, which wraps (and as int32 it is
     * negative after ~25 days up): only differences count */
    if(!s_scanned || (int32_t)(t - s_next_scan_ms) >= 0) {
        s_scanned = true;
        scan_keyboards();
        s_next_scan_ms = t + 2000;
    }
    for(int i = 0; i < 32; i++) {
        if(!s_kbd[i]) continue;
        struct input_event e[16];
        ssize_t n;
        while((n = read(s_kbd[i] - 1, e, sizeof e)) > 0) {
            for(size_t j = 0; j < (size_t)n / sizeof e[0]; j++) {
                if(e[j].type != EV_KEY || e[j].value == 2) continue; /* 2: auto-repeat */
                SDL_Scancode sc = scancode_of(e[j].code);
                if(sc == SDL_SCANCODE_UNKNOWN) continue;
                keys[sc] = e[j].value != 0;
                if(e[j].value == 1) pressed(sc);
            }
        }
        if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) { /* unplugged */
            close(s_kbd[i] - 1);
            s_kbd[i] = 0;
            memset(keys, 0, SDL_NUM_SCANCODES);
        }
    }
}

#endif /* LV_USE_LINUX_FBDEV && LV_USE_EVDEV */
