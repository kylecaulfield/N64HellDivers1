#ifndef SIM_WAVES_H
#define SIM_WAVES_H

// Wave spawner. Each wave has a composition (how many of each bug type);
// bugs arrive in groups at a "breach" point off-screen around the players.
// When the wave is spent and nearly cleared, a short break, then the next.
// Groups that don't fit under the enemy cap wait for room: nothing is lost.

#include "sim.h"

// Wave `wave`'s bugs of each type at a difficulty level, for a squad of
// `troopers` (bots count; three is the standard).
void wave_composition(int wave, int difficulty, int troopers, uint16_t out[ENEMY_TYPE_COUNT]);
void waves_reset(sim_t *s);
void waves_update(sim_t *s);

#endif
