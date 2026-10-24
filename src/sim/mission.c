#include "mission.h"

#include "calldown.h"
#include "grid.h"
#include "sim_internal.h"

static void enter(sim_t *s, mission_phase_t phase)
{
    s->mission.phase = (uint8_t)phase;
    sim_push_event(s, EV_MISSION, (uint8_t)phase, s->arena.lz, v2(0.0f, 0.0f));
}

void mission_init(sim_t *s)
{
    s->mission = (mission_t){ .phase = MISSION_DROP, .timer = DROP_IN_TICKS };
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
    for (int z = 0; z < MISSION_ZONES; z++)
        sc.zones += m->zones[z].captured ? SCORE_ZONE : 0;
    if (m->phase == MISSION_SUCCESS) {
        int aboard = 0;
        for (int i = 0; i < SIM_MAX_PLAYERS; i++)
            aboard += (m->extracted >> i) & 1;
        sc.extraction = SCORE_EXTRACTED + aboard * SCORE_ABOARD;
        int secs = (int)(m->stats.ticks / SIM_HZ);
        sc.time = secs < SCORE_PAR_SECONDS ? (SCORE_PAR_SECONDS - secs) * SCORE_PER_SECOND : 0;
    }
    sc.reinforcements = -(int32_t)m->stats.reinforcements * SCORE_REINFORCEMENT;
    sc.total = sc.kills + sc.zones + sc.extraction + sc.time + sc.reinforcements;
    if (sc.total < 0)
        sc.total = 0;
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
    s->mission.assisted = true;
    for (int i = 0; i < MISSION_ZONES; i++) {
        s->mission.zones[i].captured = true;
        s->mission.zones[i].held = (uint16_t)mission_capture_ticks(s);
    }
}

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
    for (int z = 0; z < MISSION_ZONES; z++) {
        zone_state_t *zs = &s->mission.zones[z];
        vec2 c = s->arena.zones[z];
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
        update_zones(s);
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
