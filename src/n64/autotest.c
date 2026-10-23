#include "autotest.h"

#ifdef AUTOTEST

#include <libdragon.h>

// Phases, in rendered frames (~30 per second).
#define F_WALK        60
#define F_SCHEME_A    150
#define F_DIVE        420
#define F_SCHEME_B    480
#define F_MENU        780
#define F_PAUSE       870
#define F_UNPAUSE     930
#define F_END         1050

static uint32_t shots_seen;

static int nearest_enemy(const sim_t *sim, vec2 from)
{
    int best = -1;
    float best_d2 = 0.0f;
    for (int i = 0; i < SIM_MAX_ENEMIES; i++) {
        const enemy_t *e = &sim->enemies[i];
        if (!e->active)
            continue;
        float d2 = v2_dist2(e->pos, from);
        if (best < 0 || d2 < best_d2) {
            best = i;
            best_d2 = d2;
        }
    }
    return best;
}

static void stick_towards(sim_input_t *p, vec2 dir)
{
    dir = v2_norm_or(dir, v2(0.0f, 1.0f));
    p->stick_x = (int8_t)(dir.x * 80.0f);
    p->stick_y = (int8_t)(dir.y * 80.0f);
}

// Snap a direction to the C-button combination nearest to it (8-way).
static uint16_t cbuttons_towards(vec2 dir)
{
    uint16_t b = 0;
    if (dir.x > 0.38f)  b |= SIM_BTN_CRIGHT;
    if (dir.x < -0.38f) b |= SIM_BTN_CLEFT;
    if (dir.y > 0.38f)  b |= SIM_BTN_CUP;
    if (dir.y < -0.38f) b |= SIM_BTN_CDOWN;
    return b;
}

static void phase(uint32_t frame, const char *name, const sim_t *sim)
{
    debugf("AUTOTEST frame %lu tick %lu: %s (kills %lu, bugs %u, hp %d)\n",
           (unsigned long)frame, (unsigned long)sim->tick, name, (unsigned long)sim->kills,
           sim->counts.enemies, (int)sim->players[0].hp);
}

void autotest_input(input_frame_t *in, uint32_t frame, sim_t *sim)
{
    sim_input_t *p = &in->ports[0];
    *p = (sim_input_t){ .connected = true };
    in->pressed_any = 0;
    in->toggle_menu = false;

    const player_t *me = &sim->players[0];
    int target = nearest_enemy(sim, me->pos);
    vec2 to = target >= 0 ? v2_sub(sim->enemies[target].pos, me->pos) : v2(0.0f, 1.0f);

    if (frame == 0) {
        phase(frame, "start: idle while wave 1 closes in", sim);
        sim->settings.god_mode = true; // keep the script deterministic in outcome
    } else if (frame < F_WALK) {
        // idle
    } else if (frame < F_SCHEME_A) {
        if (frame == F_WALK)
            phase(frame, "walk east", sim);
        p->stick_x = 80;
    } else if (frame < F_DIVE) {
        // Scheme A: turn towards the nearest bug (Z released), then fire with Z
        // held (facing locks), strafing slightly.
        if (frame == F_SCHEME_A)
            phase(frame, "scheme A: turn and fire at nearest bug", sim);
        stick_towards(p, to);
        if ((frame / 20) % 2) {
            p->held = SIM_BTN_Z;
            p->stick_x /= 3;
            p->stick_y /= 3;
        }
    } else if (frame < F_SCHEME_B) {
        if (frame == F_DIVE) {
            phase(frame, "dive", sim);
            p->pressed = SIM_BTN_A;
        }
        p->stick_y = -80;
    } else if (frame < F_MENU) {
        if (frame == F_SCHEME_B) {
            sim->settings.scheme[0] = SCHEME_B;
            phase(frame, "scheme B: C-buttons towards nearest bug, B-hold to shotgun at 600", sim);
        }
        p->held = cbuttons_towards(v2_norm_or(to, v2(0.0f, 1.0f)));
        p->stick_x = (int8_t)((frame / 45) % 2 ? 50 : -50);
        if (frame >= 600 && frame < 600 + HOLD_B_SWITCH + 2)
            p->held = SIM_BTN_B; // hold B: switch to the shotgun
    } else if (frame < F_PAUSE) {
        if (frame == F_MENU) {
            phase(frame, "open debug menu, spawn 10 mixed bugs", sim);
            in->toggle_menu = true;
        } else if (frame == F_MENU + 10 || frame == F_MENU + 15) {
            in->pressed_any = SIM_BTN_DDOWN; // -> "Spawn 10 mixed bugs"
        } else if (frame == F_MENU + 25) {
            in->pressed_any = SIM_BTN_A;
        } else if (frame == F_MENU + 60) {
            in->pressed_any = SIM_BTN_B; // close
        }
    } else if (frame < F_UNPAUSE) {
        if (frame == F_PAUSE) {
            phase(frame, "pause", sim);
            in->pressed_any = SIM_BTN_START;
        }
    } else if (frame < F_END) {
        if (frame == F_UNPAUSE) {
            phase(frame, "unpause, fight with the shotgun", sim);
            in->pressed_any = SIM_BTN_START;
        }
        p->held = cbuttons_towards(v2_norm_or(to, v2(0.0f, 1.0f)));
    } else if (frame == F_END) {
        bool ok = sim->kills >= 5 && shots_seen > 50 && me->weapon == 1 && sim->waves.number >= 1;
        debugf("AUTOTEST %s: kills %lu, shots %lu, weapon %s, wave %u, bugs alive %u, dropped ticks see STATS\n",
               ok ? "PASS" : "FAIL", (unsigned long)sim->kills, (unsigned long)shots_seen,
               me->weapon == 1 ? "shotgun" : "rifle", sim->waves.number, sim->counts.enemies);
    }
    in->pressed_any |= p->pressed;
}

void autotest_observe(const sim_t *sim)
{
    for (int i = 0; i < sim->event_count; i++)
        if (sim->events[i].type == EV_SHOT)
            shots_seen++;
}

#endif
