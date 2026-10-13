/*
 * Background music: see music.h.
 *
 * The audio callback only decodes and scales; everything that allocates or
 * touches files (opening the next track) happens in music_update() on the
 * main thread, under SDL_LockAudioDevice. On the web build the callback runs
 * on the main thread anyway and the lock is a no-op.
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

static SDL_AudioDeviceID s_dev;
static int s_rate;

/* Heap-allocated: a decoder's backend keeps a pointer to it, so it must not
 * move after ma_decoder_init. */
static ma_decoder * s_dec;
static void * s_data; /* the whole file: decoders read from memory */

static float s_volume = 0.7f;
static float s_gain;            /* current, ramps toward s_target */
static volatile float s_target;
static float s_step;            /* gain change per sample frame */
static volatile bool s_ended;   /* set by the callback, handled in music_update */
static volatile bool s_loop;    /* the title track: start over instead of ending */
static volatile bool s_snap;    /* jump to the target gain at once (the hard start) */

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
    if((int)got < frames && s_loop) {
        ma_uint64 more = 0;
        ma_decoder_seek_to_pcm_frame(s_dec, 0);
        ma_decoder_read_pcm_frames(s_dec, out + got * 2, (ma_uint64)frames - got, &more);
        got += more;
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

/* New shuffled order; never starts with the track that just played. */
static void shuffle(int avoid_first)
{
    for(int i = 0; i < s_track_n; i++) s_order[i] = i;
    for(int i = s_track_n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = s_order[i]; s_order[i] = s_order[j]; s_order[j] = t;
    }
    if(s_track_n > 1 && s_order[0] == avoid_first) {
        int t = s_order[0]; s_order[0] = s_order[1]; s_order[1] = t;
    }
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
        size_t size;
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
    if(snap) s_snap = true;
    SDL_UnlockAudioDevice(s_dev);
    if(old_dec) { ma_decoder_uninit(old_dec); free(old_dec); }
    free(old_data);
    if(path) {
        const char * slash = strrchr(path, '/');
        printf("music: %s\n", slash ? slash + 1 : path);
        fflush(stdout);
    }
    return true;
}

/* Main thread only. Tries the following tracks until one opens. */
static void open_next(bool snap)
{
    s_playing_intro = false;
    if(s_track_n == 0) {
        open_path(NULL, false, false);
        return;
    }
    for(int tries = 0; tries < s_track_n; tries++) {
        int last = s_pos >= 0 ? s_order[s_pos] : -1;
        if(++s_pos >= s_track_n) {
            shuffle(last);
            s_pos = 0;
        }
        if(open_path(s_tracks[s_order[s_pos]], false, snap)) return;
    }
}

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
        s_volume = (v < 0 ? 0 : v > 100 ? 100 : v) / 100.0f;
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
    shuffle(-1);
    s_pos = -1;
    if(game_title_active()) open_intro();
    else open_next(false);
    s_target = game_music_enabled() ? s_volume : 0.0f;
    SDL_PauseAudioDevice(s_dev, 0);
    game_set_music_available(true);
    return true;
}

void music_update(void)
{
    if(s_dev == 0) return;
    s_target = game_music_enabled() ? s_volume : 0.0f;
    bool title = game_title_active();
    if(title && !s_playing_intro) open_intro();
    /* Leaving the title: no fade. The calm track stops dead and the first
     * game track starts at full volume -- the contrast is the point. */
    else if(!title && s_playing_intro) open_next(true);
    else if(!title && (game_music_take_next() || s_ended)) open_next(false);
    else if(title) game_music_take_next(); /* N on the title: nothing to skip to */
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
