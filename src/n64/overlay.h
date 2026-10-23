#ifndef N64_OVERLAY_H
#define N64_OVERLAY_H

// Debug overlay (toggle: hold L, press D-Up) and the once-per-second stats
// line sent over USB with debugf. Both show the same numbers.

#include <stdint.h>

#include "sim.h"
#include "tickclock.h"

typedef struct {
    uint32_t frame_us; // wall time since the previous frame started
    uint32_t cpu_us;   // CPU busy time this frame (sim + render submission)
    uint32_t sim_us;   // part of cpu_us spent in sim ticks
} frame_timing_t;

void overlay_init(void);
void overlay_toggle(void);
// Snapshot the heap usage; any growth after this point is flagged, because
// gameplay must not allocate. Call once loading is finished.
void overlay_set_heap_baseline(void);
// Record this frame's numbers; logs the stats line once per second.
void overlay_record(const frame_timing_t *t, const sim_t *sim, const tickclock_t *clock);
// Draw into the currently attached framebuffer (call after the 3D scene).
void overlay_draw(void);

#endif
