#ifndef ROLLING_ROB_PROF_H
#define ROLLING_ROB_PROF_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Tiny frame profiler, off unless ROLLING_ROB_PROFILE is set. Accumulates
 * wall time per slot and prints the average per drawn frame. Draw-code slots
 * are only timed on the row-phase-0 context, so run with ROLLING_ROB_THREADS=1
 * for a clean split.
 */
enum {
    RR_PROF_UPDATE,   /* game_update: input, physics, entities */
    RR_PROF_RENDER,   /* game_render: everything below, plus text */
    RR_PROF_SKY,
    RR_PROF_MOUNTAIN,
    RR_PROF_OBJECTS,  /* finish line, checkpoints, monsters, mines, saucers, Julians */
    RR_PROF_ROB_FX,   /* Rob, explosion, confetti, fireworks */
    RR_PROF_OVERLAY,  /* charge meter, touch buttons */
    RR_PROF_REFR,     /* LVGL refresh: compose the canvas and HUD, flush */
    RR_PROF_MASK,     /* sky mask from the ground polygon */
    RR_PROF_JOIN,     /* waiting for the other rasterizer threads */
    RR_PROF_TEXT,     /* queued text drawn with LVGL fonts */
    RR_PROF_SUBMIT,   /* waiting for the display thread to take the frame */
    RR_PROF_P_WAIT,   /* display thread: waiting for the previous flip */
    RR_PROF_P_COPY,   /* display thread: game frame to the screen */
    RR_PROF_P_HUD,    /* display thread: HUD tiles over it */
    RR_PROF_N
};

extern bool rr_prof_on;
void rr_prof_init(void);
uint64_t rr_prof_now(void);
void rr_prof_add(int slot, uint64_t ns);
/* Prints per-frame averages over `frames` drawn frames and resets. */
void rr_prof_report(uint32_t frames);

#define RR_PROF(slot, stmt)                                  \
    do {                                                     \
        if(rr_prof_on) {                                     \
            uint64_t rr_prof_t0_ = rr_prof_now();            \
            stmt;                                            \
            rr_prof_add((slot), rr_prof_now() - rr_prof_t0_); \
        }                                                    \
        else {                                               \
            stmt;                                            \
        }                                                    \
    } while(0)

#endif /* ROLLING_ROB_PROF_H */
