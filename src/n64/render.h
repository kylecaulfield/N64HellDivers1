#ifndef N64_RENDER_H
#define N64_RENDER_H

#include "sim.h"

// Framebuffers in flight. Per-instance matrices and particle buffers are
// buffered this many times because the RSP may still be reading last frame's.
#define FB_COUNT 3

// Render units per metre. Models are imported at 64 units per metre (the
// Tiny3D importer default), so model matrices scale by WORLD_SCALE / 64.
#define WORLD_SCALE 16.0f

void render_init(void);
// Build the static matrices (floor, rocks). Call after the sim made its arena.
void render_bind_world(const sim_t *sim);
// Attach the next framebuffer and draw the 3D scene and particles. Returns
// the time in microseconds spent blocked waiting for a free framebuffer.
uint32_t render_scene(const sim_t *sim);
// Present the frame.
void render_end(void);

#endif
