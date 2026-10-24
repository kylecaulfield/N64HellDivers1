#ifndef SIM_ENEMY_H
#define SIM_ENEMY_H

#include "sim.h"

typedef struct {
    const char *name;
    float    radius;
    float    hp;
    float    speed;        // m/s
    float    melee_range;  // centre distance that starts an attack
    float    melee_reach;  // centre distance at which the blow still lands
    float    damage;
    uint16_t windup;       // ticks of telegraph before the blow
    uint16_t recover;      // ticks after it
    float    front_armor;  // damage multiplier for hits on the front arc (1 = none)
    float    mass;         // share of separation pushes (heavier moves less)
} enemy_def_t;

extern const enemy_def_t enemy_defs[ENEMY_TYPE_COUNT];

// Rebuild the list of active enemy indices (once per tick, before use).
void enemies_collect_active(sim_t *s);

#endif
