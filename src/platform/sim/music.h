#ifndef ROLLING_ROB_MUSIC_H
#define ROLLING_ROB_MUSIC_H

#include <stdbool.h>

/*
 * Background music for the SDL builds (desktop and web). Plays every .mp3/.flac/.wav in one directory
 * as a shuffled playlist through SDL audio, decoded on the fly by miniaudio.
 *
 * Directory: $ROLLING_ROB_MUSIC, else ./music/mp3, else ./music.
 * ROLLING_ROB_MUSIC=0 turns it off; ROLLING_ROB_MUSIC_VOLUME=0..100 (70).
 *
 * The on/off choice is the game's (game_music_enabled(), saved with the
 * times); music_update() follows it and fades rather than clicks.
 *
 * A file named intro.* is the title screen's track: not in the playlist,
 * looped while game_title_active(), and cut off dead when the game starts,
 * with the first game track starting at full volume (no fade). Without one
 * the title screen is silent.
 */

/* Before hud_create(): finds the tracks and opens the audio device.
 * Returns false (and stays silent) when there is nothing to play. */
bool music_init(void);

/* Once per frame, on the main thread. */
void music_update(void);

void music_shutdown(void);

#endif /* ROLLING_ROB_MUSIC_H */
