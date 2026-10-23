#ifndef N64_RENDER_H
#define N64_RENDER_H

#include "sim.h"

// Framebuffers in flight. Per-instance matrices, skeleton matrices and
// particle buffers are buffered this many times because the RSP may still be
// reading the previous frames'.
#define FB_COUNT 3

// Render units per metre. Models are imported at 64 units per metre (the
// Tiny3D importer default), so model matrices scale by WORLD_SCALE / 64.
#define WORLD_SCALE 16.0f

// Bugs of a type share this many skeletons ("pose slots"): walk cycles at
// evenly spread phases, plus one attack pose. Animation cost is per slot,
// not per bug.
#define WALK_SLOTS 6
#define POSE_SLOTS (WALK_SLOTS + 1)

typedef struct {
    uint32_t anim_us;     // skeleton/animation updates
    uint32_t build_us;    // matrices and particle buffers
    uint16_t drawn_bugs;  // after culling
    uint16_t skeletons;   // skeletons updated this frame
} render_stats_t;

void render_init(void);
// Build the static matrices (floor, rocks). Call after the sim made its arena.
void render_bind_world(const sim_t *sim);
// Attach the next framebuffer and draw the 3D scene and particles.
// `anim_dt` is the game time that passed (0 while paused). Returns the time
// in microseconds spent blocked waiting for a free framebuffer.
uint32_t render_scene(const sim_t *sim, float anim_dt);
// Present the frame.
void render_end(void);
const render_stats_t *render_stats(void);

#endif
