#include "prof.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

bool rr_prof_on;
static uint64_t s_acc[RR_PROF_N];
static const char * const s_name[RR_PROF_N] = {
    "update", "render", "  sky", "  mountain", "  objects", "  rob+fx", "  overlay", "lvgl refr",
    "  mask", "  join", "  text", "submit", "p.wait", "p.copy", "p.hud",
};

void rr_prof_init(void)
{
    rr_prof_on = getenv("ROLLING_ROB_PROFILE") != NULL;
}

uint64_t rr_prof_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

void rr_prof_add(int slot, uint64_t ns)
{
    __atomic_add_fetch(&s_acc[slot], ns, __ATOMIC_RELAXED); /* other threads add too */
}

void rr_prof_report(uint32_t frames)
{
    if(!rr_prof_on || frames == 0) return;
    printf("profile, ms per drawn frame:");
    for(int i = 0; i < RR_PROF_N; i++) {
        printf(" %s %.2f%s", s_name[i], (double)s_acc[i] / 1e6 / frames, i + 1 < RR_PROF_N ? " |" : "\n");
        s_acc[i] = 0;
    }
}
