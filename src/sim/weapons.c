#include "weapons.h"

#include <math.h>

const weapon_def_t weapon_defs[WEAPON_COUNT] = {
    [WEAPON_RIFLE] = {
        .name = "RIFLE", .mag_size = 30, .pellets = 1, .damage = 20.0f,
        .fire_interval = 4, .reload_ticks = SECONDS(2.0f), .start_reserve = 180,
        .speed = 60.0f, .range = 30.0f, .spread = 0.02f,
    },
    [WEAPON_SHOTGUN] = {
        .name = "SHOTGUN", .mag_size = 6, .pellets = 8, .damage = 11.0f,
        .fire_interval = 24, .reload_ticks = SECONDS(2.5f), .start_reserve = 36,
        .speed = 45.0f, .range = 12.0f, .spread = 0.14f,
    },
};

weapon_slot_t weapon_slot_full(weapon_type_t type)
{
    return (weapon_slot_t){
        .type = (uint8_t)type,
        .mag = weapon_defs[type].mag_size,
        .reserve = weapon_defs[type].start_reserve,
    };
}

vec2 aim_assist(const sim_t *s, vec2 origin, vec2 dir, control_scheme_t scheme)
{
    float cone = scheme == SCHEME_B ? AIM_B_CONE : AIM_A_CONE;
    float strength = scheme == SCHEME_B ? AIM_B_STRENGTH : AIM_A_STRENGTH;
    float min_cos = cosf(cone);

    // Best target: most aligned with the aim, slightly preferring closer ones.
    int best = -1;
    float best_score = -2.0f;
    vec2 best_dir = dir;
    for (int i = 0; i < SIM_MAX_ENEMIES; i++) {
        const enemy_t *e = &s->enemies[i];
        if (!e->active)
            continue;
        vec2 to = v2_sub(e->pos, origin);
        float d2 = v2_len2(to);
        if (d2 > AIM_RANGE * AIM_RANGE || d2 < 1e-6f)
            continue;
        float d = sqrtf(d2);
        vec2 to_n = v2_scale(to, 1.0f / d);
        float c = v2_dot(dir, to_n);
        if (c < min_cos)
            continue;
        float score = c - 0.05f * (d / AIM_RANGE);
        if (score > best_score) {
            best_score = score;
            best = i;
            best_dir = to_n;
        }
    }
    if (best < 0)
        return dir;
    return v2_norm_or(v2_lerp(dir, best_dir, strength), dir);
}

static void fire(sim_t *s, int idx, vec2 aim)
{
    player_t *p = &s->players[idx];
    weapon_slot_t *w = &p->weapons[p->weapon];
    const weapon_def_t *def = &weapon_defs[w->type];

    vec2 dir = aim_assist(s, p->pos, aim, (control_scheme_t)s->settings.scheme[idx]);
    vec2 muzzle = v2_add(p->pos, v2_scale(dir, MUZZLE_OFFSET));

    for (int n = 0; n < def->pellets; n++) {
        int slot = -1;
        for (int i = 0; i < SIM_MAX_PROJECTILES; i++) {
            if (!s->projectiles[i].active) {
                slot = i;
                break;
            }
        }
        if (slot < 0)
            break; // pool exhausted: the shot still happens, the pellet is lost
        float dev = (rng_float(&s->rng) * 2.0f - 1.0f) * def->spread;
        vec2 pdir = v2_rotate(dir, dev);
        s->projectiles[slot] = (projectile_t){
            .active = true,
            .owner = (uint8_t)idx,
            .weapon = w->type,
            .damage = def->damage,
            .pos = muzzle,
            .vel = v2_scale(pdir, def->speed),
            .range = def->range,
        };
    }
    sim_push_event(s, EV_SHOT, w->type, muzzle, dir);

    if (!s->settings.infinite_ammo)
        w->mag--;
    p->fire_cooldown = def->fire_interval;
}

static bool start_reload(sim_t *s, int idx)
{
    player_t *p = &s->players[idx];
    weapon_slot_t *w = &p->weapons[p->weapon];
    const weapon_def_t *def = &weapon_defs[w->type];
    if (p->reload_timer || w->mag >= def->mag_size || w->reserve == 0)
        return false;
    p->reload_timer = def->reload_ticks;
    sim_push_event(s, EV_RELOAD, (uint8_t)idx, p->pos, p->facing);
    return true;
}

static void finish_reload(player_t *p)
{
    weapon_slot_t *w = &p->weapons[p->weapon];
    uint16_t need = (uint16_t)(weapon_defs[w->type].mag_size - w->mag);
    uint16_t take = need < w->reserve ? need : w->reserve;
    w->mag = (uint8_t)(w->mag + take);
    w->reserve = (uint16_t)(w->reserve - take);
}

void weapon_update(sim_t *s, int idx, bool want_fire, bool want_reload, bool want_switch, vec2 aim)
{
    player_t *p = &s->players[idx];

    if (p->fire_cooldown)
        p->fire_cooldown--;

    if (p->reload_timer) {
        if (--p->reload_timer == 0)
            finish_reload(p);
    }

    if (want_switch) {
        p->weapon = (uint8_t)((p->weapon + 1) % PLAYER_WEAPON_SLOTS);
        p->reload_timer = 0; // switching cancels a reload in progress
        if (p->fire_cooldown < PLAYER_SWITCH_TICKS)
            p->fire_cooldown = PLAYER_SWITCH_TICKS;
    }

    if (want_reload)
        start_reload(s, idx);

    if (!want_fire || p->reload_timer || p->fire_cooldown)
        return;

    weapon_slot_t *w = &p->weapons[p->weapon];
    if (w->mag > 0) {
        fire(s, idx, aim);
    } else if (!start_reload(s, idx)) {
        // Empty and nothing to reload from: click, and don't spam the event.
        sim_push_event(s, EV_DRY_FIRE, (uint8_t)idx, p->pos, p->facing);
        p->fire_cooldown = weapon_defs[w->type].fire_interval * 2;
    }
}
