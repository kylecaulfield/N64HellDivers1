#ifndef SIM_DIFFICULTY_H
#define SIM_DIFFICULTY_H

// Difficulty levels, picked on the briefing. STANDARD is the tuning in
// tuning.h; the others scale it. The enemy cap doesn't change: it is set by
// what the hardware can draw, so harder levels bring tougher mixes, faster
// waves and harder hits rather than more bugs at once.

#include "sim.h"

typedef struct {
    const char *name;
    uint8_t  wave_size;       // percent of the standard wave
    uint8_t  wave_offset;     // waves start this many steps further along the ramp
    uint8_t  cadence;         // percent of the standard group interval, wave break and nest interval
    uint8_t  damage;          // percent of the standard bug damage
    uint8_t  reinforcements;  // the squad's shared pool
    uint8_t  zones;           // zones to hold (OBJ_ZONES)
    uint8_t  nests;           // nests to destroy (OBJ_NESTS)
    uint8_t  stages;          // upload stages (OBJ_UPLOAD)
    uint8_t  nest_group;      // bugs per nest group
    uint16_t score;           // percent: the mission's score is multiplied by it
} difficulty_def_t;

extern const difficulty_def_t difficulty_defs[DIFFICULTY_COUNT];

// The current mission's level.
const difficulty_def_t *sim_difficulty(const sim_t *s);
// Objective sites the mission needs: zones, nests or the terminal.
int difficulty_sites(int difficulty, objective_type_t objective);
// Display names.
const char *objective_name(objective_type_t objective);

#endif
