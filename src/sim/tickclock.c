#include "tickclock.h"

void tickclock_init(tickclock_t *c, uint32_t vblanks_per_tick,
                    uint32_t max_catchup, uint32_t vblank_now)
{
    c->vblanks_per_tick = vblanks_per_tick ? vblanks_per_tick : 1;
    c->max_catchup = max_catchup ? max_catchup : 1;
    c->last_vblank = vblank_now;
    c->accum = 0;
    c->total_ticks = 0;
    c->dropped_ticks = 0;
}

uint32_t tickclock_advance(tickclock_t *c, uint32_t vblank_now)
{
    // Unsigned subtraction handles counter wraparound.
    uint32_t elapsed = vblank_now - c->last_vblank;
    c->last_vblank = vblank_now;

    c->accum += elapsed;
    uint32_t ticks = c->accum / c->vblanks_per_tick;
    c->accum -= ticks * c->vblanks_per_tick;

    if (ticks > c->max_catchup) {
        c->dropped_ticks += ticks - c->max_catchup;
        ticks = c->max_catchup;
    }
    c->total_ticks += ticks;
    return ticks;
}
