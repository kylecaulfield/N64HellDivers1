#ifndef N64_BENCH_H
#define N64_BENCH_H

// Stress bench for the M2 go/no-go. Starts from the debug menu, or at boot
// in `make BENCH=1` builds (hivebreakers-bench.z64).
//
// Setup: bots on (4 players), god mode, infinite ammo, camera at max zoom,
// waves off. Then it holds the live bug count at each level in turn
// (16, 32, ... 256; a mixed swarm spawned on screen and topped up as the
// bots kill them), lets each level settle for 2 s and measures for 4 s, and
// logs one "BENCH level=..." line per level, then "BENCH DONE".

#include <stdbool.h>

#include "overlay.h"
#include "sim.h"
#include "tickclock.h"

void bench_start(sim_t *sim);
bool bench_active(void);
// Call once per frame after the frame's sim ticks and rendering.
void bench_frame(sim_t *sim, const frame_timing_t *t, const tickclock_t *clock);
// Short status for the HUD, or NULL when not running.
const char *bench_status(void);

#endif
