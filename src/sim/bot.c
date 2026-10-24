#include "bot.h"
#include "calldown.h"
#include "camera.h"
#include "collide.h"

// Enter the Reinforce code through the same code entry a player uses: hold R,
// one input every other tick, release when complete. False when not needed.
static bool call_reinforce(const sim_t *s, int idx, player_intent_t *out)
{
    const player_t *me = &s->players[idx];
    if (!calldown_available(s, idx, CALLDOWN_REINFORCE))
        return false;
    const calldown_def_t *def = &calldown_defs[CALLDOWN_REINFORCE];
    if (me->code.entering && me->code.len == def->code_len)
        return true; // code complete: leave code_mode off, which releases R
    out->code_mode = true;
    if (s->tick % 2 == 0)
        out->code_input = def->code[me->code.entering ? me->code.len : 0];
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

player_intent_t bot_intent(const sim_t *s, int idx)
{
    player_intent_t out = {0};
    const player_t *me = &s->players[idx];
    bool calling = call_reinforce(s, idx, &out);

    // Nearest bug in range.
    int target = -1;
    float best_d2 = BOT_ENGAGE_RANGE * BOT_ENGAGE_RANGE;
    for (int n = 0; n < s->active_count; n++) {
        const enemy_t *e = &s->enemies[s->active[n]];
        if (!e->active)
            continue;
        float d2 = v2_dist2(e->pos, me->pos);
        if (d2 < best_d2) {
            best_d2 = d2;
            target = s->active[n];
        }
    }

    // Formation spot around player 1 (or the camera if player 1 is down).
    static const vec2 SLOT[SIM_MAX_PLAYERS] = { { 0, 0 }, { -3, -2 }, { 3, -2 }, { 0, -4 } };
    vec2 leader = sim_player_alive(&s->players[0]) ? s->players[0].pos : s->camera.target;
    vec2 to_spot = v2_sub(v2_add(leader, SLOT[idx]), me->pos);

    if (target >= 0) {
        vec2 to_bug = v2_sub(s->enemies[target].pos, me->pos);
        out.aim = v2_norm_or(to_bug, me->facing);
        out.lock_facing = true;
        if (teammate_in_line(s, idx, me->pos, s->enemies[target].pos))
            out.move = v2(-out.aim.y, out.aim.x); // friendly fire is on: sidestep instead
        else
            out.fire = !calling;
        if (best_d2 < BOT_KITE_RANGE * BOT_KITE_RANGE)
            out.move = v2_scale(out.aim, -1.0f); // back off while shooting
    }
    if (v2_len2(out.move) == 0.0f && v2_len2(to_spot) > BOT_FOLLOW_DIST * BOT_FOLLOW_DIST)
        out.move = v2_norm_or(to_spot, v2(0.0f, 0.0f));
    return out;
}

void sim_set_bots(sim_t *s, bool on)
{
    for (int i = 1; i < SIM_MAX_PLAYERS; i++) {
        player_t *p = &s->players[i];
        if (on && !p->active) {
            vec2 at = v2(s->camera.target.x + (float)(i * 2 - 4), s->camera.target.y - 2.0f);
            sim_spawn_player(s, i, at);
            p->bot = true;
            s->settings.scheme[i] = SCHEME_B; // bots aim like C-button players
        } else if (!on && p->bot) {
            p->active = false;
            p->bot = false;
        }
    }
    camera_update_leash(s);
}
