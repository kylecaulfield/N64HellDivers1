#include "sim.h"

#include <string.h>

#include "arena.h"
#include "bot.h"
#include "calldown.h"
#include "camera.h"
#include "enemy.h"
#include "flow.h"
#include "grid.h"
#include "mission.h"
#include "sim_internal.h"
#include "waves.h"

void sim_init(sim_t *s, uint32_t seed)
{
    memset(s, 0, sizeof(*s));
    rng_seed(&s->rng, seed);
    s->settings.enemy_cap = SIM_ENEMY_CAP_DEFAULT;
    s->settings.flow_budget = FLOW_BUDGET_DEFAULT;
    s->settings.waves = true;
    s->reinforcements = REINFORCE_POOL;
    s->squad = SQUAD_FIGHTING;
    arena_generate(&s->arena, &s->rng);
    flow_init(&s->flow, &s->arena);
    waves_reset(s);
    // Player 1 drops in at the start; joining arrives in M5.
    sim_spawn_player(s, 0, v2(0.0f, 0.0f));
    mission_init(s);
    camera_reset(s);
    sim_update_counts(s);
    grid_build(s);
}

void sim_restart(sim_t *s, uint32_t seed)
{
    sim_settings_t settings = s->settings;
    bool bots = s->players[1].bot;
    sim_mark_fn mark = s->mark;
    void *mark_ctx = s->mark_ctx;
    sim_init(s, seed);
    s->settings = settings;
    s->mark = mark;
    s->mark_ctx = mark_ctx;
    if (bots)
        sim_set_bots(s, true);
}

void sim_push_event(sim_t *s, sim_event_type_t type, uint8_t a, vec2 pos, vec2 dir)
{
    if (s->event_count >= SIM_MAX_EVENTS) {
        s->events_dropped++;
        return;
    }
    s->events[s->event_count++] = (sim_event_t){ .type = (uint8_t)type, .a = a, .pos = pos, .dir = dir };
}

void sim_clear_events(sim_t *s)
{
    s->event_count = 0;
}

void sim_update_counts(sim_t *s)
{
    uint16_t players = 0, projectiles = 0, calldowns = 0;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        if (s->players[i].active)
            players++;
    for (int i = 0; i < SIM_MAX_PROJECTILES; i++)
        if (s->projectiles[i].active)
            projectiles++;
    for (int i = 0; i < SIM_MAX_BEACONS; i++)
        calldowns += s->beacons[i].active;
    for (int i = 0; i < SIM_MAX_STRIKES; i++)
        calldowns += s->strikes[i].active;
    for (int i = 0; i < SIM_MAX_TURRETS; i++)
        calldowns += s->turrets[i].active;
    for (int i = 0; i < SIM_MAX_PICKUPS; i++)
        calldowns += s->pickups[i].active;
    s->counts.calldowns = calldowns;
    enemies_collect_active(s);
    s->counts.players = players;
    s->counts.enemies = s->active_count;
    s->counts.projectiles = projectiles;
}

void sim_step(sim_t *s, const sim_input_t inputs[SIM_MAX_PLAYERS])
{
    if (mission_over(s))
        return; // the world stops behind the results screen

    // Everything this tick reads the enemy list and grid built here
    // (timed as part of the players phase).
    sim_mark(s, SIM_PHASE_PLAYERS);
    enemies_collect_active(s);
    grid_build(s);

    player_intent_t intents[SIM_MAX_PLAYERS];
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        player_t *p = &s->players[i];
        p->present = inputs[i].connected;
        if (p->bot)
            intents[i] = bot_intent(s, i);
        else
            intents[i] = controls_read((control_scheme_t)s->settings.scheme[i], &inputs[i], &p->b_held);
    }
    players_update(s, intents);

    // Nobody reads the field while no bugs are alive (wave breaks); a group
    // that spawns walks the last field for the few ticks a fresh one takes.
    // Bugs within FLOW_DIRECT_RANGE head straight in, so a field a few ticks
    // old is fine: builds start at most every FLOW_REBUILD_TICKS.
    sim_mark(s, SIM_PHASE_FLOW);
    const flow_t *f = &s->flow;
    if (s->active_count && (f->building || !f->ready || s->tick - f->started >= FLOW_REBUILD_TICKS))
        flow_step(s, s->settings.flow_budget);

    sim_mark(s, SIM_PHASE_ENEMIES);
    enemies_update(s);

    sim_mark(s, SIM_PHASE_PROJECTILES);
    projectiles_update(s);

    sim_mark(s, SIM_PHASE_CALLDOWNS);
    calldowns_update(s);

    sim_mark(s, SIM_PHASE_SPAWN);
    waves_update(s);
    mission_update(s);
    camera_update(s);
    sim_update_counts(s);
    s->tick++;
    sim_mark(s, SIM_PHASE_END);
}
