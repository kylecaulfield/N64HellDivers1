#include "sim.h"

#include <string.h>

#include "arena.h"
#include "bot.h"
#include "calldown.h"
#include "camera.h"
#include "difficulty.h"
#include "enemy.h"
#include "flow.h"
#include "grid.h"
#include "mission.h"
#include "sim_internal.h"
#include "waves.h"

// Where each port's pod lands at the start of a mission.
static const vec2 DROP_SLOT[SIM_MAX_PLAYERS] = {
    { 0.0f, 0.0f }, { -2.5f, -1.5f }, { 2.5f, -1.5f }, { 0.0f, -3.0f },
};

// A fresh arena and mission. Everyone playing, and bots on the other ports
// if they are on, rides a pod down.
static void start(sim_t *s, uint32_t seed, uint8_t humans)
{
    rng_seed(&s->rng, seed);
    s->humans = humans;
    s->squad = SQUAD_FIGHTING;
    arena_generate(&s->arena, &s->rng,
                   difficulty_sites(s->settings.difficulty, (objective_type_t)s->settings.objective));
    flow_init(&s->flow, &s->arena);
    waves_reset(s);
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        bool human = s->humans & (1u << i);
        if (!human && !s->settings.bots)
            continue;
        s->players[i].bot = !human;
        sim_spawn_player(s, i, DROP_SLOT[i]);
    }
    mission_init(s);
    s->reinforcements = sim_difficulty(s)->reinforcements;
    camera_reset(s);
    sim_update_counts(s);
    grid_build(s);
}

void sim_init(sim_t *s, uint32_t seed)
{
    memset(s, 0, sizeof(*s));
    s->settings.enemy_cap = SIM_ENEMY_CAP_DEFAULT;
    s->settings.flow_budget = FLOW_BUDGET_DEFAULT;
    s->settings.waves = true;
    s->settings.difficulty = DIFFICULTY_STANDARD;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        memcpy(s->settings.loadout[i], LOADOUT_DEFAULT, LOADOUT_SLOTS);
    start(s, seed, 1u);
}

// A fresh start keeping the settings and the profiling hook.
static void reset(sim_t *s, uint32_t seed, uint8_t humans)
{
    sim_settings_t settings = s->settings;
    sim_mark_fn mark = s->mark;
    void *mark_ctx = s->mark_ctx;
    memset(s, 0, sizeof(*s));
    s->settings = settings;
    s->mark = mark;
    s->mark_ctx = mark_ctx;
    start(s, seed, humans);
}

void sim_restart(sim_t *s, uint32_t seed)
{
    reset(s, seed, s->humans | 1u);
}

void sim_demo(sim_t *s, uint32_t seed)
{
    s->settings.bots = true;
    reset(s, seed, 0);
}

// A spot near the squad for a pod: inside the arena, clear of rocks, and
// clear of every trooper (a landing pod crushes teammates too), including
// pods still coming down.
static vec2 drop_spot(const sim_t *s)
{
    static const vec2 AROUND[] = {
        { 0.0f, -2.5f }, { 2.5f, -1.5f }, { -2.5f, -1.5f }, { 0.0f, 2.5f }, { 3.5f, 1.0f }, { -3.5f, 1.0f },
    };
    const arena_t *a = &s->arena;
    vec2 c = s->camera.target;
    for (int k = 0; k < (int)(sizeof(AROUND) / sizeof(AROUND[0])); k++) {
        vec2 at = v2_add(c, AROUND[k]);
        bool clear = at.x > a->min.x + 1.0f && at.x < a->max.x - 1.0f &&
                     at.y > a->min.y + 1.0f && at.y < a->max.y - 1.0f;
        for (int i = 0; i < a->obstacle_count && clear; i++) {
            float r = a->obstacles[i].radius + PLAYER_RADIUS + 0.5f;
            clear = v2_dist2(at, a->obstacles[i].pos) > r * r;
        }
        const float crush = DROP_CRUSH_RADIUS + PLAYER_RADIUS + 0.5f;
        for (int i = 0; i < SIM_MAX_PLAYERS && clear; i++)
            clear = !sim_player_alive(&s->players[i]) || v2_dist2(at, s->players[i].pos) > crush * crush;
        if (clear)
            return at;
    }
    return c; // the landing pushes the trooper out of whatever it hit
}

bool sim_player_join(sim_t *s, int port)
{
    player_t *p = &s->players[port];
    uint8_t bit = (uint8_t)(1u << port);
    if (s->humans & bit)
        return p->active;
    s->humans |= bit;
    if (p->bot) {
        p->bot = false;
    } else if (!p->active && s->mission.phase < MISSION_BOARDING) {
        sim_spawn_player(s, port, drop_spot(s));
        p->state = PSTATE_DROPPING;
        p->state_timer = DROP_IN_TICKS;
    }
    return p->active;
}

void sim_player_leave(sim_t *s, int port)
{
    uint8_t bit = (uint8_t)(1u << port);
    if (port == 0 || !(s->humans & bit))
        return;
    s->humans &= (uint8_t)~bit;
    player_t *p = &s->players[port];
    if (s->settings.bots) {
        p->bot = true;
    } else {
        p->active = false;
        p->code = (code_entry_t){0};
    }
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
    for (int i = 0; i < SIM_MAX_MINES; i++)
        calldowns += s->mines[i].active;
    for (int i = 0; i < SIM_MAX_FIRES; i++)
        calldowns += s->fires[i].active;
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
    const sim_settings_t *st = &s->settings;
    if (st->god_mode || st->infinite_ammo || st->free_calldowns || st->quick_mission)
        s->mission.assisted = true;

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
