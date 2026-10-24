#ifndef SIM_TICKCLOCK_H
#define SIM_TICKCLOCK_H

// Fixed-timestep scheduler locked to the video interrupt.
//
// The sim runs one tick every `vblanks_per_tick` vertical blanks (2 on NTSC,
// giving ~29.91 Hz: the N64 runs 240p NTSC at 59.826 Hz). Driving it from
// the VI counter instead of a wall clock means the sim and the display can
// never drift apart, so there are no periodic double-steps. If the game falls
// behind, at most `max_catchup` ticks run per frame and the rest are dropped
// (and counted).

#include <stdint.h>

typedef struct {
    uint32_t vblanks_per_tick;
    uint32_t max_catchup;
    uint32_t last_vblank;   // VI counter value at the previous advance
    uint32_t accum;         // vblanks not yet turned into ticks
    uint32_t total_ticks;   // ticks handed out since init
    uint32_t dropped_ticks; // ticks discarded because we were too far behind
} tickclock_t;

void tickclock_init(tickclock_t *c, uint32_t vblanks_per_tick,
                    uint32_t max_catchup, uint32_t vblank_now);

// Returns the number of sim ticks to run this frame (0..max_catchup).
// `vblank_now` is a free-running counter; wraparound is handled.
uint32_t tickclock_advance(tickclock_t *c, uint32_t vblank_now);

#endif
