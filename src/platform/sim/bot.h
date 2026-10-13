#ifndef ROLLING_ROB_BOT_H
#define ROLLING_ROB_BOT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Autopilot for the headless simulator: plays the levels as fast as it can
 * through the normal input API (keys + a touch slot), so it exercises the
 * exact code path a player uses. Used for the speedrun demo video and as a
 * "every level is completable" smoke test.
 */

typedef enum {
    BOT_MODE_GLIDE, /* holds UP: near-weightless glide, easy */
    BOT_MODE_NOUP,  /* no UP: real gravity, jumps planned by simulating the physics */
    BOT_MODE_SINGLE,/* no UP and no double jump: one jump per landing */
} bot_mode_t;

void bot_init(int32_t until_level /* 1-based, inclusive */, bot_mode_t mode);
/* Call once per frame before game_update(). Returns false when the run is done. */
bool bot_step(int32_t frame);
void bot_print_report(void);

#endif /* ROLLING_ROB_BOT_H */
