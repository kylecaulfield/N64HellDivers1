#ifndef TEST_UTIL_H
#define TEST_UTIL_H

// Shared fixtures for the sim tests.

#include <string.h>

#include "enemy.h"
#include "flow.h"
#include "grid.h"
#include "sim.h"
#include "sim_internal.h"

// A sim with player 1 at the origin, no rocks and no automatic spawning, so
// each test controls exactly what is in the world.
static inline void setup_empty(sim_t *s)
{
    sim_init(s, 1234);
    s->arena.obstacle_count = 0;
    flow_init(&s->flow, &s->arena);
    s->settings.waves = false;
    sim_kill_all_enemies(s);
    for (int i = 0; i < SIM_MAX_PROJECTILES; i++)
        s->projectiles[i].active = false;
    sim_clear_events(s);
}

// Rebuild the per-tick enemy list and grid, for tests that call a single
// system (e.g. projectiles_update) instead of sim_step.
static inline void refresh_world(sim_t *s)
{
    enemies_collect_active(s);
    grid_build(s);
}

static inline void inputs_none(sim_input_t in[SIM_MAX_PLAYERS])
{
    memset(in, 0, sizeof(sim_input_t) * SIM_MAX_PLAYERS);
    in[0].connected = true;
}

static inline void step_n(sim_t *s, const sim_input_t in[SIM_MAX_PLAYERS], int n)
{
    for (int i = 0; i < n; i++)
        sim_step(s, in);
}

static inline int count_events(const sim_t *s, sim_event_type_t type)
{
    int n = 0;
    for (int i = 0; i < s->event_count; i++)
        if (s->events[i].type == type)
            n++;
    return n;
}

static inline int count_projectiles(const sim_t *s)
{
    int n = 0;
    for (int i = 0; i < SIM_MAX_PROJECTILES; i++)
        if (s->projectiles[i].active)
            n++;
    return n;
}

#endif
