#ifndef TEST_UTIL_H
#define TEST_UTIL_H

// Shared fixtures for the sim tests.

#include <string.h>

#include "calldown.h"
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
    // Skip the drop-in: player 1 is on the ground and the objective is on.
    s->mission.phase = MISSION_OBJECTIVE;
    s->mission.timer = 0;
    s->players[0].state = PSTATE_NORMAL;
    s->players[0].state_timer = 0;
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

static const uint16_t CODE_BUTTON[] = {
    [CODE_UP] = SIM_BTN_CUP, [CODE_DOWN] = SIM_BTN_CDOWN,
    [CODE_LEFT] = SIM_BTN_CLEFT, [CODE_RIGHT] = SIM_BTN_CRIGHT,
};

// Hold R, press each input of `type`'s code on its own tick, then release R
// (the throw happens on the release tick).
static inline void enter_code(sim_t *s, sim_input_t in[SIM_MAX_PLAYERS], int port, calldown_type_t type)
{
    const calldown_def_t *d = &calldown_defs[type];
    for (int k = 0; k < d->code_len; k++) {
        in[port].held = SIM_BTN_R | CODE_BUTTON[d->code[k]];
        in[port].pressed = CODE_BUTTON[d->code[k]];
        sim_step(s, in);
        in[port].held = SIM_BTN_R;
        in[port].pressed = 0;
        sim_step(s, in);
    }
    in[port].held = 0;
    sim_step(s, in);
}

#endif
