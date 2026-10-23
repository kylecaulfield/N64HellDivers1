#ifndef SIM_INTERNAL_H
#define SIM_INTERNAL_H

// Per-system update functions, called by sim_step in this order.

#include "controls.h"
#include "sim.h"

void players_update(sim_t *s, const player_intent_t intents[SIM_MAX_PLAYERS]);
void enemies_update(sim_t *s);
void projectiles_update(sim_t *s);
void spawner_update(sim_t *s);

// Damage entry points (also used by the tests).
void player_damage(sim_t *s, int idx, float amount);
void enemy_damage(sim_t *s, int idx, float amount, vec2 hit_pos);

#endif
