#ifndef N64_FX_H
#define N64_FX_H

// Visual-only effects spawned from the sim's events: particles (muzzle
// flashes, hit sparks, death bursts, explosions, dust), camera shake and a
// screen flash. They never feed back into the sim, so they use their own RNG
// and can be dropped freely when the pool is full.

#include <stdint.h>

#include "sim.h"

#define FX_MAX 256

typedef struct {
    float   x, y, z;     // metres; y is up (render space, sim north = -z)
    float   vx, vy, vz;  // m/s
    uint8_t life;        // ticks left, 0 = free
    uint8_t life_max;
    uint8_t size;        // particle size at full life
    uint8_t rgb[3];
    bool    gravity;
    bool    glow;        // drawn semi-transparent in a second pass (explosion cores)
} fx_particle_t;

void fx_init(void);
// Spawn effects for every event the sim produced since the last call.
void fx_spawn_from_events(const sim_t *s);
// Advance by the number of sim ticks run this frame (0 while paused).
void fx_update(uint32_t ticks);
const fx_particle_t *fx_particles(void);
int fx_count(void);
// Camera shake for this frame, metres on the ground plane (zero when calm).
vec2 fx_shake(void);
// Screen flash strength, 0..1.
float fx_flash(void);

#endif
