#include "bot.h"

#include <math.h>

#include "calldown.h"
#include "camera.h"
#include "collide.h"
#include "enemy.h"
#include "mission.h"
#include "weapons.h"

// Enter a code through the same code entry a player uses: hold R, one input
// every other tick, release when complete.
static void enter_code(const sim_t *s, int idx, const uint8_t *code, int len, player_intent_t *out)
{
    const player_t *me = &s->players[idx];
    if (me->code.entering && me->code.len == len)
        return; // code complete: leave code_mode off, which releases R
    out->code_mode = true;
    if (s->tick % 2 == 0)
        out->code_input = code[me->code.entering ? me->code.len : 0];
}

// Face somewhere a thrown beacon lands clear of the squad and inside the
// arena: the wanted direction if it is safe, otherwise the nearest safe
// one of eight. Whatever drops on the beacon crushes teammates too.
static void aim_throw(const sim_t *s, int idx, vec2 want, player_intent_t *out)
{
    const player_t *me = &s->players[idx];
    const float reach = BEACON_THROW_SPEED * BEACON_FLIGHT_TICKS * SIM_DT;
    const float clear = DROP_CRUSH_RADIUS + PLAYER_RADIUS + BOT_DROP_CLEARANCE;
    vec2 dir = want;
    for (int k = 0; k < 8; k++) {
        float turn = (float)((k + 1) / 2) * 0.785398f * (k % 2 ? 1.0f : -1.0f);
        dir = v2_rotate(want, turn);
        vec2 lands = v2_add(me->pos, v2_scale(dir, reach));
        bool ok = lands.x > s->arena.min.x + 1.0f && lands.x < s->arena.max.x - 1.0f &&
                  lands.y > s->arena.min.y + 1.0f && lands.y < s->arena.max.y - 1.0f;
        for (int i = 0; i < SIM_MAX_PLAYERS && ok; i++)
            ok = i == idx || !sim_player_alive(&s->players[i]) || v2_dist2(s->players[i].pos, lands) > clear * clear;
        if (ok)
            break;
    }
    out->aim = dir;
    out->lock_facing = true;
}

// Is `pos` about to be hit by a calldown (a drop landing on its beacon, a
// strike's line or area)? `from` gets the point to get away from.
static bool in_danger(const sim_t *s, vec2 pos, vec2 *from)
{
    for (int i = 0; i < SIM_MAX_BEACONS + SIM_MAX_STRIKES; i++) {
        uint8_t type;
        vec2 c, dir;
        if (i < SIM_MAX_BEACONS) {
            const beacon_t *b = &s->beacons[i];
            if (!b->active || b->state != BEACON_ARMED || b->type == CALLDOWN_EXTRACTION)
                continue;
            type = b->type, c = b->pos, dir = b->dir;
        } else {
            const strike_t *k = &s->strikes[i - SIM_MAX_BEACONS];
            if (!k->active)
                continue;
            type = k->type, c = k->center, dir = k->dir;
        }
        float r;
        if (type == CALLDOWN_AIRSTRIKE) {
            // The bombs fall along a line through the beacon.
            float half = (AIRSTRIKE_BOMBS - 1) * 0.5f * AIRSTRIKE_SPACING;
            float along = clampf(v2_dot(v2_sub(pos, c), dir), -half, half);
            c = v2_add(c, v2_scale(dir, along));
            r = AIRSTRIKE_RADIUS + PLAYER_RADIUS + 1.0f;
        } else if (type == CALLDOWN_BARRAGE) {
            r = BARRAGE_AREA + BARRAGE_RADIUS + PLAYER_RADIUS;
        } else {
            r = DROP_CRUSH_RADIUS + PLAYER_RADIUS + BOT_DROP_CLEARANCE;
        }
        if (v2_dist2(pos, c) < r * r) {
            *from = c;
            return true;
        }
    }
    return false;
}

// Call Reinforce when someone is down. False when not needed.
static bool call_reinforce(const sim_t *s, int idx, player_intent_t *out)
{
    if (!calldown_available(s, idx, CALLDOWN_REINFORCE))
        return false;
    const calldown_def_t *def = &calldown_defs[CALLDOWN_REINFORCE];
    enter_code(s, idx, def->code, def->code_len, out);
    aim_throw(s, idx, s->players[idx].facing, out);
    return true;
}

// Rounds left in a weapon slot, magazine and reserve.
static int rounds(const weapon_slot_t *w)
{
    return w->owned ? w->mag + w->reserve : 0;
}

// Nearly out of ammo: under a magazine left for the rifle and the support
// weapon (the shotgun is for close quarters, not a whole fight).
static bool low_on_ammo(const player_t *me)
{
    for (int i = 0; i < PLAYER_WEAPON_SLOTS; i++) {
        const weapon_slot_t *w = &me->weapons[i];
        if (w->type != WEAPON_SHOTGUN && rounds(w) >= weapon_defs[w->type].mag_size)
            return false;
    }
    return true;
}

// The nearest resupply crate within `range`, or -1.
static int crate_in_range(const sim_t *s, vec2 from, float range)
{
    int best = -1;
    float best_d2 = range * range;
    for (int i = 0; i < SIM_MAX_PICKUPS; i++) {
        const pickup_t *k = &s->pickups[i];
        float d2 = v2_dist2(k->pos, from);
        if (k->active && k->type == PICKUP_RESUPPLY && d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    return best;
}

// Call a Resupply when nearly dry and no crate is near.
static bool call_resupply(const sim_t *s, int idx, player_intent_t *out)
{
    const player_t *me = &s->players[idx];
    if (!low_on_ammo(me) || crate_in_range(s, me->pos, BOT_CRATE_RANGE) >= 0 ||
        !calldown_available(s, idx, CALLDOWN_RESUPPLY))
        return false;
    const calldown_def_t *def = &calldown_defs[CALLDOWN_RESUPPLY];
    enter_code(s, idx, def->code, def->code_len, out);
    aim_throw(s, idx, me->facing, out);
    return true;
}

// Drop a sentry into a swarm: enough bugs close by, and nobody where it
// lands (a drop crushes teammates too).
static bool call_sentry(const sim_t *s, int idx, int bugs_near, player_intent_t *out)
{
    const player_t *me = &s->players[idx];
    if (bugs_near < BOT_SENTRY_BUGS || !calldown_available(s, idx, CALLDOWN_SENTRY))
        return false;
    const calldown_def_t *def = &calldown_defs[CALLDOWN_SENTRY];
    enter_code(s, idx, def->code, def->code_len, out);
    aim_throw(s, idx, me->facing, out);
    return true;
}

// A bug winding up a blow on this bot, close enough to land it.
static int attacker(const sim_t *s, int idx)
{
    const player_t *me = &s->players[idx];
    for (int n = 0; n < s->active_count; n++) {
        const enemy_t *e = &s->enemies[s->active[n]];
        float reach = enemy_defs[e->type].melee_reach + 0.3f;
        if (e->active && e->state == ESTATE_WINDUP && e->target == idx && v2_dist2(e->pos, me->pos) < reach * reach)
            return s->active[n];
    }
    return -1;
}

// A bot leading the squad (the title screen's demo, where nobody plays)
// calls Extraction itself; with people playing, they decide when to leave.
static bool call_extraction(const sim_t *s, int idx, player_intent_t *out)
{
    if (idx != 0 || !calldown_available(s, idx, CALLDOWN_EXTRACTION))
        return false;
    const calldown_def_t *def = &calldown_defs[CALLDOWN_EXTRACTION];
    enter_code(s, idx, def->code, def->code_len, out);
    return true;
}

// Enter the terminal's code: the first bot in the ring does it (unless a
// person in the ring is already entering a code).
static bool use_terminal(const sim_t *s, int idx, player_intent_t *out)
{
    if (!mission_terminal_open(s, idx))
        return false;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &s->players[i];
        if (i == idx || !mission_terminal_open(s, i))
            continue;
        if (!p->bot ? p->code.entering : i < idx)
            return false;
    }
    enter_code(s, idx, s->mission.upload.code, s->mission.upload.code_len, out);
    return true;
}

// Would a shot from `from` to `to` pass a teammate first?
static bool teammate_in_line(const sim_t *s, int idx, vec2 from, vec2 to)
{
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        float t;
        if (i != idx && sim_player_alive(&s->players[i]) &&
            segment_circle(from, to, s->players[i].pos, PLAYER_RADIUS + BOT_FF_CLEARANCE, &t))
            return true;
    }
    return false;
}

// The nearest live nest within `range`, or -1.
static int nest_in_range(const sim_t *s, vec2 from, float range)
{
    int best = -1;
    float best_d2 = range * range;
    for (int n = 0; n < s->arena.site_count; n++) {
        float d2 = v2_dist2(s->arena.sites[n], from);
        if (mission_nest_live(s, n) && d2 < best_d2) {
            best_d2 = d2;
            best = n;
        }
    }
    return best;
}

// Where the squad's leader heads: the next zone not yet held, a spot to
// shoot the nearest nest from, a spot in the terminal's ring; then the LZ.
static vec2 next_objective(const sim_t *s, vec2 me)
{
    const mission_t *m = &s->mission;
    if (m->phase <= MISSION_OBJECTIVE) {
        switch (m->objective) {
        case OBJ_NESTS: {
            int n = nest_in_range(s, me, 1000.0f);
            if (n >= 0) {
                vec2 c = s->arena.sites[n];
                return v2_add(c, v2_scale(v2_norm_or(v2_sub(me, c), v2(0.0f, -1.0f)), BOT_NEST_STANDOFF));
            }
            break;
        }
        case OBJ_UPLOAD: {
            vec2 c = s->arena.sites[0];
            return v2_add(c, v2_scale(v2_norm_or(v2_sub(me, c), v2(0.0f, -1.0f)), UPLOAD_RADIUS * 0.4f));
        }
        default:
            for (int z = 0; z < s->arena.site_count; z++)
                if (!m->zones[z].captured)
                    return s->arena.sites[z];
            break;
        }
    }
    return s->arena.lz;
}

// Back away from `aim`, or slide along the edge when the arena (or the
// camera's leash) is behind.
static vec2 back_off(const sim_t *s, vec2 pos, vec2 aim)
{
    vec2 away = v2_scale(aim, -1.0f);
    vec2 to = v2_add(pos, v2_scale(away, BOT_WALL_MARGIN));
    vec2 lo = s->camera.leash_min, hi = s->camera.leash_max;
    lo.x = fmaxf(lo.x, s->arena.min.x);
    lo.y = fmaxf(lo.y, s->arena.min.y);
    hi.x = fminf(hi.x, s->arena.max.x);
    hi.y = fminf(hi.y, s->arena.max.y);
    if (to.x > lo.x && to.x < hi.x && to.y > lo.y && to.y < hi.y)
        return away;
    // Sideways, whichever way leads towards the middle of the free space.
    vec2 side = v2(-aim.y, aim.x);
    vec2 mid = v2_scale(v2_add(lo, hi), 0.5f);
    return v2_dot(side, v2_sub(mid, pos)) >= 0.0f ? side : v2_scale(side, -1.0f);
}

player_intent_t bot_intent(const sim_t *s, int idx)
{
    player_intent_t out = {0};
    const player_t *me = &s->players[idx];
    bool calling = call_reinforce(s, idx, &out) || use_terminal(s, idx, &out) || call_extraction(s, idx, &out) ||
                   call_resupply(s, idx, &out);

    // An empty weapon: switch to one with rounds left.
    const weapon_slot_t *w = &me->weapons[me->weapon];
    if (rounds(w) == 0 && !me->reload_timer) {
        for (int i = 0; i < PLAYER_WEAPON_SLOTS; i++)
            out.switch_weapon |= i != me->weapon && rounds(&me->weapons[i]) > 0;
    }

    // Nearest bug in range; a bulwark's armored front soaks bullets, so
    // one counts as twice as far unless it is showing its flank.
    int target = -1, bugs_near = 0;
    float best_score = BOT_ENGAGE_RANGE * BOT_ENGAGE_RANGE, best_d2 = best_score;
    for (int n = 0; n < s->active_count; n++) {
        const enemy_t *e = &s->enemies[s->active[n]];
        if (!e->active)
            continue;
        float d2 = v2_dist2(e->pos, me->pos);
        bugs_near += d2 < BOT_SENTRY_RANGE * BOT_SENTRY_RANGE;
        float score = d2;
        if (enemy_defs[e->type].front_armor < 1.0f &&
            v2_dot(e->facing, v2_norm_or(v2_sub(me->pos, e->pos), e->facing)) > FRONT_ARC_COS)
            score *= 4.0f;
        if (score < best_score) {
            best_score = score;
            best_d2 = d2;
            target = s->active[n];
        }
    }
    if (!calling)
        calling = call_sentry(s, idx, bugs_near, &out);
    bool throwing = out.lock_facing; // facing where a beacon can land safely

    // Formation spot around player 1 (or the camera if player 1 is down).
    // A bot playing port 1 itself (the title screen's demo) leads: it heads
    // for the next zone to hold, then the LZ. Once the dropship is down,
    // everyone heads for the middle of the LZ whatever is chasing them,
    // shooting on the way.
    static const vec2 SLOT[SIM_MAX_PLAYERS] = { { 0, 0 }, { -3, -2 }, { 3, -2 }, { 0, -4 } };
    bool boarding = s->mission.phase == MISSION_BOARDING;
    vec2 leader = sim_player_alive(&s->players[0]) ? s->players[0].pos : s->camera.target;
    vec2 spot = boarding ? s->arena.lz : idx == 0 ? next_objective(s, me->pos) : v2_add(leader, SLOT[idx]);
    // Nearly dry: a crate nearby comes first.
    int crate = !boarding && low_on_ammo(me) ? crate_in_range(s, me->pos, BOT_CRATE_RANGE) : -1;
    if (crate >= 0)
        spot = s->pickups[crate].pos;
    vec2 to_spot = v2_sub(spot, me->pos);

    // Nothing to shoot at: a nest in range will do.
    int nest = target < 0 ? nest_in_range(s, me->pos, BOT_ENGAGE_RANGE) : -1;
    if (nest >= 0 && !throwing) {
        vec2 c = s->arena.sites[nest];
        out.aim = v2_norm_or(v2_sub(c, me->pos), me->facing);
        out.lock_facing = true;
        if (!teammate_in_line(s, idx, me->pos, c))
            out.fire = !calling;
    }

    if (target >= 0) {
        vec2 to_bug = v2_norm_or(v2_sub(s->enemies[target].pos, me->pos), me->facing);
        if (!throwing) {
            out.aim = to_bug;
            out.lock_facing = true;
        }
        if (teammate_in_line(s, idx, me->pos, s->enemies[target].pos))
            out.move = v2(-to_bug.y, to_bug.x); // friendly fire is on: sidestep instead
        else
            out.fire = !calling;
        if (best_d2 < BOT_KITE_RANGE * BOT_KITE_RANGE)
            out.move = back_off(s, me->pos, to_bug);
    }
    if (boarding || (v2_len2(out.move) == 0.0f && v2_len2(to_spot) > BOT_FOLLOW_DIST * BOT_FOLLOW_DIST))
        out.move = v2_norm_or(to_spot, v2(0.0f, 0.0f));

    // Something is about to land here: get out from under it.
    vec2 from;
    if (!boarding && in_danger(s, me->pos, &from))
        out.move = v2_norm_or(v2_sub(me->pos, from), v2_scale(me->facing, -1.0f));

    // A blow is coming: dive out of its reach, the way a player would.
    int threat = attacker(s, idx);
    if (threat >= 0 && !me->dive_cooldown) {
        out.move = back_off(s, me->pos, v2_norm_or(v2_sub(s->enemies[threat].pos, me->pos), me->facing));
        out.dive = true;
    }
    return out;
}

void sim_set_bots(sim_t *s, bool on)
{
    s->settings.bots = on;
    for (int i = 1; i < SIM_MAX_PLAYERS; i++) {
        player_t *p = &s->players[i];
        if (s->humans & (1u << i))
            continue;
        if (on && !p->active) {
            vec2 at = v2(s->camera.target.x + (float)(i * 2 - 4), s->camera.target.y - 2.0f);
            p->bot = true;
            sim_spawn_player(s, i, at);
        } else if (!on && p->bot) {
            p->active = false;
            p->bot = false;
        }
    }
    camera_update_leash(s);
}
