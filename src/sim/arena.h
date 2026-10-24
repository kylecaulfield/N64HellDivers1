#ifndef SIM_ARENA_H
#define SIM_ARENA_H

#include "sim.h"

// Collision radius of each rock model (matches tools/genmodels.py).
extern const float rock_radius[ROCK_KIND_COUNT];

// Bounded square arena: the mission sites (`site_count` objective sites and
// the LZ, see mission.h), then procedurally placed rocks that keep clear of
// them and of the drop zone. All from the sim's RNG.
void arena_generate(arena_t *a, rng_t *rng, int site_count);

// Resolve a circle against the arena: push out of rocks, clamp to bounds.
void arena_resolve(const arena_t *a, vec2 *pos, float radius);
// Just the bounds part of arena_resolve.
void arena_clamp(const arena_t *a, vec2 *pos, float radius);

#endif
