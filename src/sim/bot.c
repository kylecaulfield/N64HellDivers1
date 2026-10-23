#include "bot.h"
#include "camera.h"

player_intent_t bot_intent(const sim_t *s, int idx)
{
    player_intent_t out = {0};
    const player_t *me = &s->players[idx];

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
        out.fire = true;
        out.lock_facing = true;
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
