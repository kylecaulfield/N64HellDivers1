#ifndef SIM_INTERNAL_H
#define SIM_INTERNAL_H

// Per-system update functions, called by sim_step in this order.

#include "controls.h"
#include "sim.h"

void players_update(sim_t *s, const player_intent_t intents[SIM_MAX_PLAYERS]);
void enemies_update(sim_t *s); // marks SIM_PHASE_CROWD part way through
void projectiles_update(sim_t *s);

// Profiling hook, see sim_t.mark.
static inline void sim_mark(sim_t *s, sim_phase_t phase)
{
    if (s->mark)
        s->mark(s->mark_ctx, phase);
}

// Damage entry points (also used by the tests).
void player_damage(sim_t *s, int idx, float amount);
void enemy_damage(sim_t *s, int idx, float amount, vec2 hit_pos, vec2 shot_dir);

#endif
