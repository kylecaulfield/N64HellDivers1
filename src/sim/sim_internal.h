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

// A projectile from the pool (none if it is exhausted). Returns its index or -1.
int sim_spawn_projectile(sim_t *s, uint8_t owner, uint8_t weapon, vec2 pos, vec2 dir,
                         float speed, float range, float damage);

// Damage entry points (also used by the tests). `by` gets the kill credit:
// a player index or SOURCE_NONE.
void player_damage(sim_t *s, int idx, float amount);
void enemy_damage(sim_t *s, int idx, float amount, vec2 hit_pos, vec2 shot_dir, int by);

#endif
