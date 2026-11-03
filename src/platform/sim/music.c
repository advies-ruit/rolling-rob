/*
 * Background music: see music.h.
 *
 * The audio callback only decodes and scales; everything that allocates or
 * touches files (opening the next track) happens in music_update() on the
 * main thread, under SDL_LockAudioDevice. On the web build the callback runs
 * on the main thread anyway and the lock is a no-op.
 *
 * The player's settings are the game's (game.h): on/off, volume, order
 * (shuffled, in order, this track again and again). A skip (N, or the page's
 * Track line) fades the track out before the next one starts; "previous"
 * restarts the track when it has played a few seconds, as players do.
 */
#include "music.h"
#include "game.h"
#include <SDL2/SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Only the decoders: no device I/O (SDL does that), no engine. */
#define MA_NO_DEVICE_IO
#define MA_NO_THREADING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "miniaudio.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define MAX_TRACKS 64
#define FADE_S 0.35f

static char * s_tracks[MAX_TRACKS];
static int s_track_n;
static int s_order[MAX_TRACKS];
static int s_pos = -1; /* index into s_order of the playing track */
static rr_music_order_t s_order_kind = RR_MUSIC_SHUFFLE; /* what s_order was made for */
static int s_skip;      /* a skip waiting for the fade out: +1 / -1 */
static char s_title[64];

static SDL_AudioDeviceID s_dev;
static int s_rate;

/* Heap-allocated: a decoder's backend keeps a pointer to it, so it must not
 * move after ma_decoder_init. */
static ma_decoder * s_dec;
static void * s_data; /* the whole file: decoders read from memory */

static volatile float s_gain;   /* current, ramps toward s_target */
static volatile float s_target;
static float s_step;            /* gain change per sample frame */
static volatile bool s_ended;   /* set by the callback, handled in music_update */
static volatile bool s_loop;    /* the title track: start over instead of ending */
static volatile bool s_snap;    /* jump to the target gain at once (the hard start) */
static volatile uint64_t s_played; /* frames of this track played, for the time shown */

/* The title screen's track: intro.* in the music directory, kept out of the
 * playlist. s_playing_intro says which kind is loaded. */
static char * s_intro;
static bool s_playing_intro;

static void audio_cb(void * user, Uint8 * stream, int len)
{
    (void)user;
    float * out = (float *)stream;
    int frames = len / (int)(2 * sizeof(float));
    float target = s_target;

    /* Silent and staying silent: keep the decoder where it is (pause). */
    if(s_dec == NULL || s_ended || (s_gain == 0.0f && target == 0.0f)) {
        memset(stream, 0, (size_t)len);
        return;
    }
    if(s_snap) {
        s_gain = target;
        s_snap = false;
    }
    ma_uint64 got = 0;
    ma_decoder_read_pcm_frames(s_dec, out, (ma_uint64)frames, &got);
    s_played += got;
    if((int)got < frames && s_loop) {
        ma_uint64 more = 0;
        ma_decoder_seek_to_pcm_frame(s_dec, 0);
        ma_decoder_read_pcm_frames(s_dec, out + got * 2, (ma_uint64)frames - got, &more);
        got += more;
        s_played = more;
    }
    if((int)got < frames) {
        memset(out + got * 2, 0, (size_t)(frames - (int)got) * 2 * sizeof(float));
        s_ended = true;
    }
    float g = s_gain;
    for(int i = 0; i < (int)got; i++) {
        if(g < target) { g += s_step; if(g > target) g = target; }
        else if(g > target) { g -= s_step; if(g < target) g = target; }
        out[2 * i] *= g;
        out[2 * i + 1] *= g;
    }
    s_gain = g;
}

static bool has_music_ext(const char * name)
{
    const char * dot = strrchr(name, '.');
    if(dot == NULL) return false;
    return !SDL_strcasecmp(dot, ".mp3") || !SDL_strcasecmp(dot, ".flac") || !SDL_strcasecmp(dot, ".wav");
}

static int cmp_str(const void * a, const void * b)
{
    return strcmp(*(char * const *)a, *(char * const *)b);
}

static int scan_dir(const char * dir)
{
    DIR * d = opendir(dir);
    if(d == NULL) return 0;
    struct dirent * e;
    while((e = readdir(d)) != NULL && s_track_n < MAX_TRACKS) {
        if(e->d_name[0] == '.' || !has_music_ext(e->d_name)) continue;
        size_t n = strlen(dir) + strlen(e->d_name) + 2;
        char * p = malloc(n);
        if(p == NULL) break;
        snprintf(p, n, "%s/%s", dir, e->d_name);
        if(SDL_strncasecmp(e->d_name, "intro", 5) == 0 && s_intro == NULL) s_intro = p;
        else s_tracks[s_track_n++] = p;
    }
    closedir(d);
    qsort(s_tracks, (size_t)s_track_n, sizeof(s_tracks[0]), cmp_str);
    return s_track_n;
}

/* A new order for the kind chosen. Shuffled: never starts with the track
 * that just played. In order: by file name (music_prep.sh numbers them). */
static void make_order(int avoid_first)
{
    s_order_kind = game_music_order();
    for(int i = 0; i < s_track_n; i++) s_order[i] = i;
    if(s_order_kind != RR_MUSIC_SHUFFLE) return;
    for(int i = s_track_n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = s_order[i]; s_order[i] = s_order[j]; s_order[j] = t;
    }
    if(s_track_n > 1 && s_order[0] == avoid_first) {
        int t = s_order[0]; s_order[0] = s_order[1]; s_order[1] = t;
    }
}

/* The order changed while a track plays: a new order around it, so the
 * next one follows from where we are. */
static void reorder(void)
{
    int cur = s_pos >= 0 ? s_order[s_pos] : -1;
    make_order(-1);
    s_pos = -1;
    for(int i = 0; i < s_track_n; i++)
        if(s_order[i] == cur) {
            if(s_order_kind == RR_MUSIC_SHUFFLE) { /* the current one first, the rest shuffled after it */
                s_order[i] = s_order[0];
                s_order[0] = cur;
                s_pos = 0;
            }
            else s_pos = i;
        }
}

/* "02_arcade_dash_2.mp3" -> "Arcade Dash 2" */
static void title_of(const char * path, char * out, size_t n)
{
    const char * name = strrchr(path, '/');
    name = name ? name + 1 : path;
    const char * p = name;
    while(*p >= '0' && *p <= '9') p++;
    if(p != name && (*p == '_' || *p == ' ' || *p == '-' || *p == '.')) p++;
    else p = name;
    const char * dot = strrchr(p, '.');
    size_t len = dot ? (size_t)(dot - p) : strlen(p);
    size_t o = 0;
    bool word = true;
    for(size_t i = 0; i < len && o + 1 < n; i++) {
        char c = p[i] == '_' ? ' ' : p[i];
        if(word && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        word = c == ' ';
        out[o++] = c;
    }
    out[o] = 0;
}

static void * read_file(const char * path, size_t * size)
{
    FILE * f = fopen(path, "rb");
    if(f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void * buf = n > 0 ? malloc((size_t)n) : NULL;
    if(buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *size = buf ? (size_t)n : 0;
    return buf;
}

/* Main thread only. Replaces what plays by `path` (NULL: silence). */
static bool open_path(const char * path, bool loop, bool snap)
{
    ma_decoder * dec = NULL;
    void * data = NULL;
    if(path) {
        size_t size = 0; /* read_file leaves it alone when it fails */
        data = read_file(path, &size);
        dec = malloc(sizeof(*dec));
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, (ma_uint32)s_rate);
        if(data == NULL || dec == NULL || ma_decoder_init_memory(data, size, &cfg, dec) != MA_SUCCESS) {
            fprintf(stderr, "music: cannot play %s\n", path);
            free(data);
            free(dec);
            return false;
        }
    }
    SDL_LockAudioDevice(s_dev);
    ma_decoder * old_dec = s_dec;
    void * old_data = s_data;
    s_dec = dec;
    s_data = data;
    s_ended = false;
    s_loop = loop;
    s_played = 0;
    if(snap) s_snap = true;
    SDL_UnlockAudioDevice(s_dev);
    if(old_dec) { ma_decoder_uninit(old_dec); free(old_dec); }
    free(old_data);
    if(path) {
        const char * slash = strrchr(path, '/');
        printf("music: %s\n", slash ? slash + 1 : path);
        fflush(stdout);
        title_of(path, s_title, sizeof(s_title));
    }
    else s_title[0] = 0;
    return true;
}

/* Main thread only. dir +1: the next track, -1: the one before, 0: this
 * one again (repeat); tries further until one opens. */
static void open_step(int dir, bool snap)
{
    s_playing_intro = false;
    if(s_track_n == 0) {
        open_path(NULL, false, false);
        return;
    }
    if(game_music_order() != s_order_kind) reorder();
    bool repeat = game_music_order() == RR_MUSIC_REPEAT;
    for(int tries = 0; tries < s_track_n; tries++) {
        int last = s_pos >= 0 ? s_order[s_pos] : -1;
        if(dir == 0 && s_pos >= 0) {
            /* this one again */
        }
        else if(dir < 0) {
            if(--s_pos < 0) s_pos = s_track_n - 1;
        }
        else if(++s_pos >= s_track_n) {
            if(s_order_kind == RR_MUSIC_SHUFFLE) make_order(last);
            s_pos = 0;
        }
        if(open_path(s_tracks[s_order[s_pos]], repeat, snap)) return;
        if(dir == 0) dir = 1; /* it would not open: the next one */
    }
}

static void open_next(bool snap) { open_step(1, snap); }

static int volume_percent(void) { return game_music_volume(); }

static void open_intro(void)
{
    s_playing_intro = true;
    if(!open_path(s_intro, true, false)) open_path(NULL, false, false);
}

bool music_init(void)
{
    const char * env = getenv("ROLLING_ROB_MUSIC");
    if(env && !strcmp(env, "0")) return false;
    const char * vol = getenv("ROLLING_ROB_MUSIC_VOLUME");
    if(vol && *vol) {
        int v = atoi(vol);
        game_set_music_volume_default(v); /* until the player picks one */
    }
    if(env && *env) scan_dir(env);
    else if(scan_dir("music/mp3") == 0) scan_dir("music");
    if(s_track_n == 0 && s_intro == NULL) return false;

    if(!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "music: no audio (%s)\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 2048;
    want.callback = audio_cb;
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if(s_dev == 0) {
        fprintf(stderr, "music: cannot open audio (%s)\n", SDL_GetError());
        return false;
    }
    s_rate = have.freq;
    s_step = 1.0f / (FADE_S * (float)s_rate);

    srand((unsigned)time(NULL));
    make_order(-1);
    s_pos = -1;
    if(game_title_active()) open_intro();
    else open_next(false);
    s_target = game_music_enabled() ? volume_percent() / 100.0f : 0.0f;
    SDL_PauseAudioDevice(s_dev, 0);
    game_set_music_available(true);
    return true;
}

void music_update(void)
{
    if(s_dev == 0) return;
    float volume = game_music_enabled() ? volume_percent() / 100.0f : 0.0f;
    bool title = game_title_active();
    int skip = game_music_take_skip();
    if(title) skip = 0; /* N on the title: nothing to skip to */
    /* "Previous" a few seconds into a track: that track from the start. */
    if(skip < 0 && !s_skip && s_played > (uint64_t)s_rate * 3) skip = 2;
    if(skip) {
        s_skip = skip;
        if(!game_music_enabled()) s_gain = 0; /* silent: nothing to fade */
    }
    /* repeat on / off for the track playing */
    if(!s_playing_intro) s_loop = game_music_order() == RR_MUSIC_REPEAT;

    if(title && !s_playing_intro) {
        s_skip = 0;
        open_intro();
    }
    /* Leaving the title: no fade. The calm track stops dead and the first
     * game track starts at full volume -- the contrast is the point. */
    else if(!title && s_playing_intro) open_next(true);
    else if(!title && s_skip) {
        /* fade out, then the other track at full volume */
        volume = 0.0f;
        if(s_gain == 0.0f || s_ended || s_dec == NULL) {
            int d = s_skip;
            s_skip = 0;
            if(d == 2) {
                SDL_LockAudioDevice(s_dev);
                if(s_dec) ma_decoder_seek_to_pcm_frame(s_dec, 0);
                s_played = 0;
                s_ended = false;
                SDL_UnlockAudioDevice(s_dev);
            }
            else open_step(d, false);
            volume = game_music_enabled() ? volume_percent() / 100.0f : 0.0f;
            s_snap = true;
        }
    }
    else if(!title && s_ended) open_step(game_music_order() == RR_MUSIC_REPEAT ? 0 : 1, false);
    s_target = volume;
    uint32_t ms = s_rate ? (uint32_t)(s_played * 1000 / (uint64_t)s_rate) : 0;
    game_music_set_playing(s_playing_intro ? "Title music" : s_title, (int32_t)ms);
}

void music_shutdown(void)
{
    if(s_dev) SDL_CloseAudioDevice(s_dev);
    s_dev = 0;
    if(s_dec) { ma_decoder_uninit(s_dec); free(s_dec); }
    s_dec = NULL;
    free(s_data);
    s_data = NULL;
    for(int i = 0; i < s_track_n; i++) free(s_tracks[i]);
    s_track_n = 0;
    free(s_intro);
    s_intro = NULL;
}
