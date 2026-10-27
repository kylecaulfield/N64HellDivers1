#ifndef N64_BENCH_H
#define N64_BENCH_H

// Stress bench for the M2 go/no-go. Starts from the debug menu, or at boot
// in `make BENCH=1` builds (hivebreakers-bench.z64).
//
// Setup: bots on (4 players), god mode, infinite ammo, camera at max zoom,
// waves off. Then it holds the live bug count at each level in turn
// (16, 32, ... 256; a mixed swarm spawned on screen and topped up as the
// bots kill them), lets each level settle for 2 s and measures for 4 s, and
// logs one "BENCH level=..." line per level. Then a breakdown: the 96-bug
// level again with one part of the frame left out each time (see
// bench_skip), one "BENCH skip=..." line each, then "BENCH DONE". The
// breakdown runs frames one at a time (BENCH_SERIAL): slower, but its
// gpu_done is then each frame's own GPU time, not time queued behind the
// previous frame's.

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

// What the breakdown leaves out of the current frame (0 outside it).
#define BENCH_SKIP_HUD       0x01 // the HUD and overlay
#define BENCH_SKIP_BUGS      0x02 // drawing bugs (their matrices are still built)
#define BENCH_SKIP_PARTICLES 0x04 // particles and glows
#define BENCH_SKIP_WORLD     0x08 // the ground and rocks
#define BENCH_SERIAL         0x10 // wait for the RSP after each frame, so gpu_done is that frame's own
uint32_t bench_skip(void);

#endif
