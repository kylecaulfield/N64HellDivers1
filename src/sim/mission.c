#include "mission.h"

#include <stddef.h>

#include "calldown.h"
#include "collide.h"
#include "difficulty.h"
#include "enemy.h"
#include "grid.h"
#include "sim_internal.h"

static void enter(sim_t *s, mission_phase_t phase)
{
    s->mission.phase = (uint8_t)phase;
    sim_push_event(s, EV_MISSION, (uint8_t)phase, s->arena.lz, v2(0.0f, 0.0f));
}

// A fresh terminal code: random, and no calldown's code may start it, so
// entering it can never throw a calldown on the way.
static void new_code(sim_t *s)
{
    upload_t *u = &s->mission.upload;
    u->code_len = UPLOAD_CODE_LEN;
    for (int tries = 0; tries < 64; tries++) {
        for (int k = 0; k < UPLOAD_CODE_LEN; k++)
            u->code[k] = (uint8_t)(CODE_UP + rng_range(&s->rng, 4));
        bool clash = false;
        for (int len = 1; len <= UPLOAD_CODE_LEN && !clash; len++)
            clash = calldown_match(u->code, len) >= 0;
        if (!clash)
            return;
    }
}

void mission_init(sim_t *s)
{
    const sim_settings_t *st = &s->settings;
    mission_t *m = &s->mission;
    *m = (mission_t){
        .phase = MISSION_DROP,
        .timer = DROP_IN_TICKS,
        .objective = st->objective < OBJ_COUNT ? st->objective : OBJ_ZONES,
        .difficulty = st->difficulty < DIFFICULTY_COUNT ? st->difficulty : DIFFICULTY_STANDARD,
    };
    for (int n = 0; n < ARENA_SITES; n++)
        m->nests[n].hp = mission_nest_hp(s);
    m->upload.stages = sim_difficulty(s)->stages;
    if (m->objective == OBJ_UPLOAD)
        new_code(s);
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        player_t *p = &s->players[i];
        if (p->active) {
            p->state = PSTATE_DROPPING;
            p->state_timer = DROP_IN_TICKS;
        }
    }
}

bool mission_over(const sim_t *s)
{
    return s->mission.phase == MISSION_SUCCESS || s->mission.phase == MISSION_FAILED;
}

int mission_capture_ticks(const sim_t *s)
{
    return s->settings.quick_mission ? ZONE_CAPTURE_TICKS / QUICK_MISSION_DIV : ZONE_CAPTURE_TICKS;
}

int mission_hold_ticks(const sim_t *s)
{
    return s->settings.quick_mission ? EXTRACT_HOLD_TICKS / QUICK_MISSION_DIV : EXTRACT_HOLD_TICKS;
}

int mission_stage_ticks(const sim_t *s)
{
    return s->settings.quick_mission ? UPLOAD_STAGE_TICKS / QUICK_MISSION_DIV : UPLOAD_STAGE_TICKS;
}

float mission_nest_hp(const sim_t *s)
{
    return s->settings.quick_mission ? NEST_HP / QUICK_MISSION_DIV : NEST_HP;
}

int mission_parts(const sim_t *s, int *total)
{
    const mission_t *m = &s->mission;
    int done = 0, n = s->arena.site_count;
    switch (m->objective) {
    case OBJ_NESTS:
        for (int i = 0; i < n; i++)
            done += m->nests[i].destroyed;
        break;
    case OBJ_UPLOAD:
        n = m->upload.stages;
        done = m->upload.stage;
        break;
    default:
        for (int i = 0; i < n; i++)
            done += m->zones[i].captured;
        break;
    }
    if (total)
        *total = n;
    return done;
}

player_stats_t mission_squad_stats(const mission_stats_t *st)
{
    player_stats_t sum = {0};
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_stats_t *p = &st->players[i];
        sum.kills += p->kills;
        sum.deaths += p->deaths;
        sum.shots += p->shots;
        sum.hits += p->hits;
        sum.friendly_hits += p->friendly_hits;
        sum.calldowns += p->calldowns;
    }
    return sum;
}

mission_score_t mission_score(const sim_t *s)
{
    const mission_t *m = &s->mission;
    mission_score_t sc = {0};
    sc.kills = m->stats.kills[ENEMY_SKITTER] * SCORE_KILL_SKITTER + m->stats.kills[ENEMY_GNASHER] * SCORE_KILL_GNASHER +
               m->stats.kills[ENEMY_BULWARK] * SCORE_KILL_BULWARK;
    static const int32_t PER_PART[OBJ_COUNT] = { [OBJ_ZONES] = SCORE_ZONE, [OBJ_NESTS] = SCORE_NEST,
                                                 [OBJ_UPLOAD] = SCORE_STAGE };
    sc.objective = mission_parts(s, NULL) * PER_PART[m->objective < OBJ_COUNT ? m->objective : OBJ_ZONES];
    if (m->phase == MISSION_SUCCESS) {
        int aboard = 0;
        for (int i = 0; i < SIM_MAX_PLAYERS; i++)
            aboard += (m->extracted >> i) & 1;
        sc.extraction = SCORE_EXTRACTED + aboard * SCORE_ABOARD;
        int secs = (int)(m->stats.ticks / SIM_HZ);
        sc.time = secs < SCORE_PAR_SECONDS ? (SCORE_PAR_SECONDS - secs) * SCORE_PER_SECOND : 0;
    }
    sc.reinforcements = -(int32_t)m->stats.reinforcements * SCORE_REINFORCEMENT;
    sc.multiplier = sim_difficulty(s)->score;
    int32_t base = sc.kills + sc.objective + sc.extraction + sc.time + sc.reinforcements;
    sc.total = base > 0 ? base * sc.multiplier / 100 : 0;
    return sc;
}

static bool in_circle(vec2 p, vec2 c, float r)
{
    return v2_dist2(p, c) <= r * r;
}

static bool on_ground(const player_t *p)
{
    return sim_player_alive(p) && p->state != PSTATE_DROPPING;
}

bool mission_can_call_extraction(const sim_t *s, int idx)
{
    return s->mission.phase == MISSION_EXTRACT && on_ground(&s->players[idx]) &&
           in_circle(s->players[idx].pos, s->arena.lz, LZ_RADIUS);
}

void mission_start_extraction(sim_t *s)
{
    if (s->mission.phase != MISSION_EXTRACT)
        return;
    s->mission.timer = (uint16_t)mission_hold_ticks(s);
    enter(s, MISSION_HOLD);
}

void mission_complete_objective(sim_t *s)
{
    mission_t *m = &s->mission;
    m->assisted = true;
    for (int i = 0; i < s->arena.site_count; i++) {
        m->zones[i].captured = true;
        m->zones[i].held = (uint16_t)mission_capture_ticks(s);
        m->nests[i].destroyed = true;
        m->nests[i].hp = 0.0f;
    }
    m->upload.stage = m->upload.stages;
    m->upload.running = false;
}

// --- Nests ------------------------------------------------------------------------

bool mission_nest_live(const sim_t *s, int n)
{
    return s->mission.objective == OBJ_NESTS && n < s->arena.site_count && !s->mission.nests[n].destroyed;
}

void mission_damage_nest(sim_t *s, int n, float amount, vec2 at)
{
    if (!mission_nest_live(s, n) || amount <= 0.0f)
        return;
    nest_t *ne = &s->mission.nests[n];
    ne->hp -= amount;
    sim_push_event(s, EV_NEST_HIT, (uint8_t)n, at, v2(0.0f, 0.0f));
    if (!ne->awake) { // hurting it wakes it, from any range
        ne->awake = true;
        ne->timer = NEST_FIRST_TICKS;
    }
    if (ne->hp > 0.0f)
        return;
    ne->hp = 0.0f;
    ne->destroyed = true;
    sim_push_event(s, EV_NEST_DESTROYED, (uint8_t)n, s->arena.sites[n], v2(0.0f, 0.0f));
}

// A group crawls out: mostly skitters, a gnasher in every three, and from
// HAZARD up sometimes a bulwark.
static void nest_spawn(sim_t *s, int n)
{
    const difficulty_def_t *d = sim_difficulty(s);
    vec2 c = s->arena.sites[n];
    for (int k = 0; k < d->nest_group; k++) {
        enemy_type_t type = k % 3 == 2 ? ENEMY_GNASHER : ENEMY_SKITTER;
        if (k == 0 && s->mission.difficulty >= 2 && rng_range(&s->rng, 4) == 0)
            type = ENEMY_BULWARK;
        float ang = rng_float(&s->rng) * 6.2832f;
        vec2 at = v2_add(c, v2_rotate(v2(NEST_RADIUS + enemy_defs[type].radius + 0.2f, 0.0f), ang));
        if (sim_spawn_enemy(s, type, at) < 0)
            return; // at the cap
    }
}

static bool trooper_near(const sim_t *s, vec2 c, float r)
{
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        if (on_ground(&s->players[i]) && in_circle(s->players[i].pos, c, r))
            return true;
    return false;
}

static void update_nests(sim_t *s)
{
    mission_t *m = &s->mission;
    int left = 0;
    for (int n = 0; n < s->arena.site_count; n++) {
        nest_t *ne = &m->nests[n];
        if (ne->destroyed)
            continue;
        left++;
        if (!ne->awake) {
            if (trooper_near(s, s->arena.sites[n], NEST_WAKE_RADIUS)) {
                ne->awake = true;
                ne->timer = NEST_FIRST_TICKS;
            }
            continue;
        }
        if (ne->timer && --ne->timer)
            continue;
        nest_spawn(s, n);
        ne->timer = (uint16_t)(NEST_SPAWN_TICKS * sim_difficulty(s)->cadence / 100);
    }
    if (left == 0)
        enter(s, MISSION_EXTRACT);
}

// --- Upload -------------------------------------------------------------------------

bool mission_terminal_open(const sim_t *s, int idx)
{
    const mission_t *m = &s->mission;
    const player_t *p = &s->players[idx];
    return m->objective == OBJ_UPLOAD && m->phase == MISSION_OBJECTIVE && !m->upload.running &&
           m->upload.stage < m->upload.stages && on_ground(p) && in_circle(p->pos, s->arena.sites[0], UPLOAD_RADIUS);
}

bool mission_terminal_prefix(const sim_t *s, int idx, const uint8_t *inputs, int len)
{
    const upload_t *u = &s->mission.upload;
    if (!mission_terminal_open(s, idx) || len > u->code_len)
        return false;
    for (int k = 0; k < len; k++)
        if (u->code[k] != inputs[k])
            return false;
    return true;
}

void mission_upload_start(sim_t *s, int idx)
{
    upload_t *u = &s->mission.upload;
    if (!mission_terminal_open(s, idx))
        return;
    u->running = true;
    u->progress = 0;
    sim_push_event(s, EV_UPLOAD_START, (uint8_t)idx, s->arena.sites[0], v2(0.0f, 0.0f));
}

static void update_upload(sim_t *s)
{
    upload_t *u = &s->mission.upload;
    vec2 c = s->arena.sites[0];
    u->players_in = 0;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        u->players_in += on_ground(&s->players[i]) && in_circle(s->players[i].pos, c, UPLOAD_RADIUS);
    if (!u->running || !u->players_in || ++u->progress < mission_stage_ticks(s))
        return;
    u->running = false;
    u->progress = 0;
    u->stage++;
    sim_push_event(s, EV_UPLOAD_STAGE, u->stage, c, v2(0.0f, 0.0f));
    if (u->stage >= u->stages)
        enter(s, MISSION_EXTRACT);
    else
        new_code(s);
}

bool mission_upload_running(const sim_t *s)
{
    return s->mission.objective == OBJ_UPLOAD && s->mission.phase == MISSION_OBJECTIVE && s->mission.upload.running;
}

// --- Zones ----------------------------------------------------------------------------

static int bugs_in(const sim_t *s, vec2 c, float r)
{
    int n = 0;
    grid_iter_t it;
    grid_iter_init(&it, s, v2(c.x - r, c.y - r), v2(c.x + r, c.y + r));
    int ei;
    while ((ei = grid_iter_next(&it)) >= 0)
        n += s->enemies[ei].active && in_circle(s->enemies[ei].pos, c, r);
    return n;
}

static void update_zones(sim_t *s)
{
    bool all = true;
    for (int z = 0; z < s->arena.site_count; z++) {
        zone_state_t *zs = &s->mission.zones[z];
        vec2 c = s->arena.sites[z];
        zs->players_in = 0;
        for (int i = 0; i < SIM_MAX_PLAYERS; i++)
            zs->players_in += on_ground(&s->players[i]) && in_circle(s->players[i].pos, c, ZONE_RADIUS);
        zs->bugs_in = (uint8_t)bugs_in(s, c, ZONE_RADIUS);
        if (!zs->captured && zs->players_in && !zs->bugs_in && ++zs->held >= mission_capture_ticks(s)) {
            zs->captured = true;
            sim_push_event(s, EV_ZONE_CAPTURED, (uint8_t)z, c, v2(0.0f, 0.0f));
        }
        all &= zs->captured;
    }
    if (all)
        enter(s, MISSION_EXTRACT);
}

// --- Solid objective structures --------------------------------------------------------

void mission_resolve(const sim_t *s, vec2 *pos, float radius)
{
    const mission_t *m = &s->mission;
    if (m->objective == OBJ_UPLOAD) {
        push_out_circle(pos, radius, s->arena.sites[0], TERMINAL_RADIUS);
    } else if (m->objective == OBJ_NESTS) {
        for (int n = 0; n < s->arena.site_count; n++)
            if (!m->nests[n].destroyed)
                push_out_circle(pos, radius, s->arena.sites[n], NEST_RADIUS);
    }
}

// --- The loop ---------------------------------------------------------------------------

// Living players in the LZ climb aboard and leave the field.
static void board(sim_t *s)
{
    int left = 0;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        player_t *p = &s->players[i];
        if (!on_ground(p))
            continue;
        if (in_circle(p->pos, s->arena.lz, LZ_RADIUS)) {
            s->mission.extracted |= (uint8_t)(1u << i);
            p->active = false;
            sim_push_event(s, EV_EXTRACTED, (uint8_t)i, p->pos, p->facing);
        } else {
            left++;
        }
    }
    if (left == 0 && s->mission.extracted)
        enter(s, MISSION_SUCCESS);
}

void mission_update(sim_t *s)
{
    mission_t *m = &s->mission;
    if (mission_over(s))
        return;
    m->stats.ticks++;
    if (s->squad == SQUAD_LOST && s->squad_timer == 0) {
        enter(s, MISSION_FAILED);
        return;
    }
    switch (m->phase) {
    case MISSION_DROP:
        if (--m->timer == 0)
            enter(s, MISSION_OBJECTIVE);
        break;
    case MISSION_OBJECTIVE:
        switch (m->objective) {
        case OBJ_NESTS:  update_nests(s); break;
        case OBJ_UPLOAD: update_upload(s); break;
        default:         update_zones(s); break;
        }
        break;
    case MISSION_EXTRACT:
        break; // waiting for the Extraction calldown
    case MISSION_HOLD:
        if (--m->timer == 0)
            enter(s, MISSION_BOARDING);
        break;
    case MISSION_BOARDING:
        board(s);
        break;
    }
}
