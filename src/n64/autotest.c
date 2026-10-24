#include "autotest.h"

#ifdef AUTOTEST

#include <libdragon.h>

#include "calldown.h"

// Phases, in rendered frames (~30 per second).
#define F_WALK        60
#define F_SCHEME_A    150
#define F_DIVE        420
#define F_SCHEME_B    480
#define F_MENU        780
#define F_PAUSE       870
#define F_UNPAUSE     930
#define F_CALLDOWNS   1050  // MG, sentry, airstrike, barrage, resupply codes
#define F_PICKUPS     1200  // walk to the MG case, then the crate
#define F_DIE         1350  // solo death: emergency reinforce after 5 s
#define F_BOTS        1520  // bots on; one dies and another reinforces it
#define F_END         1720

static const struct {
    uint32_t frame;
    calldown_type_t type;
} CALLS[] = {
    { F_CALLDOWNS,       CALLDOWN_MACHINE_GUN },
    { F_CALLDOWNS + 20,  CALLDOWN_SENTRY },
    { F_CALLDOWNS + 40,  CALLDOWN_AIRSTRIKE },
    { F_CALLDOWNS + 60,  CALLDOWN_BARRAGE },
    { F_CALLDOWNS + 80,  CALLDOWN_RESUPPLY },
};
static const uint16_t CODE_BTN[] = {
    [CODE_UP] = SIM_BTN_CUP, [CODE_DOWN] = SIM_BTN_CDOWN,
    [CODE_LEFT] = SIM_BTN_CLEFT, [CODE_RIGHT] = SIM_BTN_CRIGHT,
};

static uint32_t shots_seen, bombs, shells, pickups, throws, turret_shots;
static bool shotgun_seen, mg_seen, p1_reinforced, bot_reinforced;

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

// Hold R and enter `type`'s code from frame f0, one input every other frame,
// then release. True while the script owns the controller.
static bool code_script(sim_input_t *p, uint32_t frame, uint32_t f0, calldown_type_t type)
{
    const calldown_def_t *d = &calldown_defs[type];
    if (frame < f0 || frame > f0 + 2u * d->code_len)
        return false;
    uint32_t k = frame - f0;
    if (k == 2u * d->code_len)
        return true; // nothing held: R released, the beacon flies
    p->held = SIM_BTN_R;
    if ((k & 1) == 0) {
        p->held |= CODE_BTN[d->code[k / 2]];
        p->pressed = CODE_BTN[d->code[k / 2]];
    }
    return true;
}

static int find_pickup(const sim_t *sim, pickup_type_t type)
{
    for (int i = 0; i < SIM_MAX_PICKUPS; i++)
        if (sim->pickups[i].active && sim->pickups[i].type == type)
            return i;
    return -1;
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
    } else if (frame < F_CALLDOWNS) {
        if (frame == F_UNPAUSE) {
            phase(frame, "unpause, fight with the shotgun", sim);
            in->pressed_any = SIM_BTN_START;
        }
        p->held = cbuttons_towards(v2_norm_or(to, v2(0.0f, 1.0f)));
        shotgun_seen |= me->weapon == 1;
    } else if (frame < F_PICKUPS) {
        if (frame == F_CALLDOWNS)
            phase(frame, "calldowns: machine gun, sentry, airstrike, barrage, resupply", sim);
        for (unsigned i = 0; i < sizeof(CALLS) / sizeof(CALLS[0]); i++)
            if (code_script(p, frame, CALLS[i].frame, CALLS[i].type))
                break;
    } else if (frame < F_DIE) {
        if (frame == F_PICKUPS)
            phase(frame, "walk to the machine gun case, then the resupply crate", sim);
        if (frame == F_PICKUPS + 60) // empty the rifle's reserve so the crate has work
            sim->players[0].weapons[0].reserve = 0;
        int k = find_pickup(sim, mg_seen ? PICKUP_RESUPPLY : PICKUP_MACHINE_GUN);
        if (k >= 0 && sim_player_alive(me))
            stick_towards(p, v2_sub(sim->pickups[k].pos, me->pos));
    } else if (frame < F_BOTS) {
        if (frame == F_DIE) {
            phase(frame, "P1 dies alone: emergency reinforce", sim);
            sim_kill_player(sim, 0);
        }
    } else if (frame < F_END) {
        if (frame == F_BOTS) {
            phase(frame, "bots on; P3 dies and a bot reinforces it", sim);
            sim_set_bots(sim, true);
        } else if (frame == F_BOTS + 10) {
            sim_kill_player(sim, 2);
        }
    } else if (frame == F_END) {
        bool ok = sim->kills >= 5 && shots_seen > 50 && shotgun_seen && sim->waves.number >= 1 &&
                  throws >= 5 && mg_seen && turret_shots > 0 && bombs >= AIRSTRIKE_BOMBS &&
                  shells >= BARRAGE_SHELLS / 2 && pickups >= 2 && p1_reinforced && bot_reinforced &&
                  sim->reinforcements == REINFORCE_POOL - 2;
        debugf("AUTOTEST %s: kills %lu, shots %lu, shotgun %d, wave %u, throws %lu, mg %d, turret shots %lu, "
               "bombs %lu, shells %lu, pickups %lu, reinforced p1 %d bot %d, reinforcements %u, "
               "bugs alive %u, dropped ticks see STATS\n",
               ok ? "PASS" : "FAIL", (unsigned long)sim->kills, (unsigned long)shots_seen, shotgun_seen,
               sim->waves.number, (unsigned long)throws, mg_seen, (unsigned long)turret_shots,
               (unsigned long)bombs, (unsigned long)shells, (unsigned long)pickups, p1_reinforced,
               bot_reinforced, sim->reinforcements, sim->counts.enemies);
    }
    in->pressed_any |= p->pressed;
}

void autotest_observe(const sim_t *sim)
{
    for (int i = 0; i < sim->event_count; i++) {
        const sim_event_t *e = &sim->events[i];
        switch (e->type) {
        case EV_SHOT:          shots_seen++; break;
        case EV_BEACON_THROWN: throws++; break;
        case EV_PICKUP:        pickups++; break;
        case EV_EXPLOSION:     bombs += e->a == BLAST_BOMB; shells += e->a == BLAST_SHELL; break;
        case EV_REINFORCED:    p1_reinforced |= e->a == 0; bot_reinforced |= e->a == 2; break;
        default: break;
        }
    }
    mg_seen |= sim->players[0].weapons[WEAPON_SLOT_SUPPORT].owned;
    for (int i = 0; i < SIM_MAX_TURRETS; i++)
        if (sim->turrets[i].active && SENTRY_AMMO - sim->turrets[i].ammo > (int)turret_shots)
            turret_shots = (uint32_t)(SENTRY_AMMO - sim->turrets[i].ammo);
}

#endif
