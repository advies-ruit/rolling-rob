/*
 * The app's info page on the desktop and the web (app.h, app_info_fill):
 * SDL's view of the machine, and on the web the browser's (WebGL and GPU,
 * touch points, cores, memory, storage, offline state).
 */

#include "app.h"
#include "draw.h"
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/heap.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define put app_info_put

#ifdef __EMSCRIPTEN__
/* The probes are page script (web/shell.html: rrInfoStart, rrInfoGet). */
EM_JS_DEPS(rr_info, "$stringToNewUTF8,$UTF8ToString");
EM_JS(void, web_info_start, (void), {
    if(window.rrInfoStart) window.rrInfoStart();
});
EM_JS(char *, web_info_get, (const char * key), {
    var v = window.rrInfoGet ? window.rrInfoGet(UTF8ToString(key)) : "";
    return stringToNewUTF8(String(v));
});

static const char * web(const char * key)
{
    static char out[8][128];
    static int n;
    char * s = web_info_get(key);
    char * o = out[n++ & 7];
    snprintf(o, 128, "%s", s ? s : "");
    free(s);
    return o;
}
#endif

void app_info_fill(bool open)
{
#ifdef __EMSCRIPTEN__
    if(open) web_info_start();
#else
    (void)open;
#endif
    int w, h, k, most;
    app_picture(&w, &h, &k);
    int fingers = app_fingers(&most);
    /* the headless display (--headless) is no SDL window: it has no renderer */
    lv_display_t * disp = app_display();
    SDL_Renderer * r = disp && lv_display_get_driver_data(disp) ? lv_sdl_window_get_renderer(disp) : NULL;
    SDL_RendererInfo ri = { 0 };
    if(r) SDL_GetRendererInfo(r, &ri);
    bool gl = ri.name && strcmp(ri.name, "software") != 0;

    put(0, "#THIS APP", "");
    /* set by CMakeLists.txt (the release tag, its commit, when it was built) */
    put(0, "Version", "%s", RR_VERSION);
    put(0, "Built", "%s", RR_BUILT);
    put(0, "Commit", "%s", RR_COMMIT);
    put(0, "Picture", "%dx%d (%dx LCD)", w, h, k);
    put(0, "Frame rate", "%.1f fps", (double)app_fps());
    put(0, "Busy/frame", "%.1f ms of 16.7", (double)app_loop_ms());
    put(0, "Draw threads", "%d", rr_draw_threads() > 0 ? rr_draw_threads() : 1);
#ifdef __EMSCRIPTEN__
    put(0, "Drawn with", "%s", gl ? "WebGL (GPU shows it)" : "2D canvas (no WebGL)");
#else
    (void)gl;
    put(0, "Drawn with", "SDL %s", ri.name ? ri.name : "?");
#endif

    put(0, "#SCREEN", "");
#ifdef __EMSCRIPTEN__
    put(0, "Screen", "%s", web("screen"));
    put(0, "Window", "%s", web("window"));
    put(0, "Refresh", "%s", web("hz"));
#else
    SDL_DisplayMode dm;
    if(SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        put(0, "Screen", "%dx%d", dm.w, dm.h);
        put(0, "Refresh", "%d Hz", dm.refresh_rate);
    }
#endif

    put(0, "#GRAPHICS", "");
#ifdef __EMSCRIPTEN__
    put(0, "WebGL", "%s", web("gl"));
    put(0, "GPU", "%s", web("gpu"));
    put(0, "Max texture", "%s", web("maxtex"));
#else
    put(0, "Max texture", "%dx%d", ri.max_texture_width, ri.max_texture_height);
#endif
    put(0, "VRAM", "not reported (no API)");
    put(0, "Our picture", "%.1f MB a frame", (double)w * h * 4 / 1048576.0);

    put(1, "#TOUCH AND INPUT", "");
#ifdef __EMSCRIPTEN__
    put(1, "Touch", "%s", web("touch"));
    put(1, "Pointer", "%s", web("pointer"));
#else
    put(1, "Touch", "%d", SDL_GetNumTouchDevices());
#endif
    put(1, "Fingers now", "%d  (most at once: %d)", fingers, most);
    put(1, "", "put fingers on the screen");

    put(1, "#PROCESSOR AND MEMORY", "");
#ifdef __EMSCRIPTEN__
    put(1, "Cores", "%s", web("cores"));
    put(1, "Threads", "%s", web("threads"));
    put(1, "WASM SIMD", "%s", web("simd"));
    put(1, "Memory", "%s", web("devmem"));
    put(1, "App memory", "%.0f MB%s", (double)emscripten_get_heap_size() / 1048576.0, web("jsheap"));
#else
    put(1, "Cores", "%d", SDL_GetCPUCount());
    put(1, "Memory", "%d MB", SDL_GetSystemRAM());
    put(1, "SIMD", "%s%s%s", SDL_HasNEON() ? "NEON " : "", SDL_HasAVX2() ? "AVX2 " : "", SDL_HasSSE41() ? "SSE4.1" : "");
#endif

    put(1, "#SYSTEM", "");
#ifdef __EMSCRIPTEN__
    put(1, "System", "%s", web("system"));
    put(1, "Browser", "%s", web("browser"));
    put(1, "Offline", "%s", web("offline"));
    put(1, "Storage", "%s", web("storage"));
    put(1, "Sound", "%s", web("audio"));
#else
    put(1, "System", "%s", SDL_GetPlatform());
    const char * ad = SDL_GetCurrentAudioDriver();
    put(1, "Sound", "%s", ad ? ad : "off");
#endif
}
