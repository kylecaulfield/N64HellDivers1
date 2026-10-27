#include "calldown.h"

#include <math.h>
#include <stddef.h>

#include "arena.h"
#include "collide.h"
#include "enemy.h"
#include "grid.h"
#include "mission.h"
#include "sim_internal.h"
#include "weapons.h"

#define U CODE_UP
#define D CODE_DOWN
#define L CODE_LEFT
#define R CODE_RIGHT

// Codes are original, 4-6 inputs, and no code is a prefix of another.
const calldown_def_t calldown_defs[CALLDOWN_COUNT] = {
    [CALLDOWN_REINFORCE]   = { "REINFORCE",   { L, R, U, U },       4, SECONDS(10), SECONDS(3) },
    [CALLDOWN_RESUPPLY]    = { "RESUPPLY",    { D, D, L, R },       4, SECONDS(60), SECONDS(4) },
    [CALLDOWN_MACHINE_GUN] = { "MACHINE GUN", { L, D, L, U, R },    5, SECONDS(120), SECONDS(4) },
    [CALLDOWN_AIRSTRIKE]   = { "AIRSTRIKE",   { R, U, R, D },       4, SECONDS(45), SECONDS(3) },
    [CALLDOWN_BARRAGE]     = { "BARRAGE",     { R, D, D, L, U, R }, 6, SECONDS(90), SECONDS(5) },
    [CALLDOWN_SENTRY]      = { "SENTRY",      { U, L, D, R, U },    5, SECONDS(90), SECONDS(4) },
    [CALLDOWN_MINEFIELD]   = { "MINEFIELD",   { D, L, D, R },       4, SECONDS(60), SECONDS(3) },
    [CALLDOWN_LAUNCHER]    = { "LAUNCHER",    { L, U, R, R, D },    5, SECONDS(120), SECONDS(4) },
    [CALLDOWN_NAPALM]      = { "NAPALM",      { R, L, D, D },       4, SECONDS(60), SECONDS(3) },
    [CALLDOWN_EXTRACTION]  = { "EXTRACTION",  { R, R, U, D },       4, SECONDS(10), SECONDS(2) },
};

#undef U
#undef D
#undef L
#undef R

int calldown_match(const uint8_t *inputs, int len)
{
    for (int t = 0; t < CALLDOWN_COUNT; t++) {
        const calldown_def_t *d = &calldown_defs[t];
        if (d->code_len != len)
            continue;
        int k = 0;
        while (k < len && d->code[k] == inputs[k])
            k++;
        if (k == len)
            return t;
    }
    return -1;
}

bool calldown_prefix(const uint8_t *inputs, int len)
{
    for (int t = 0; t < CALLDOWN_COUNT; t++) {
        const calldown_def_t *d = &calldown_defs[t];
        if (len > d->code_len)
            continue;
        int k = 0;
        while (k < len && d->code[k] == inputs[k])
            k++;
        if (k == len)
            return true;
    }
    return false;
}

static bool anyone_dead(const sim_t *s)
{
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        if (s->players[i].active && s->players[i].state == PSTATE_DEAD)
            return true;
    return false;
}

static bool beacon_out(const sim_t *s, calldown_type_t type)
{
    for (int i = 0; i < SIM_MAX_BEACONS; i++)
        if (s->beacons[i].active && s->beacons[i].type == type)
            return true;
    return false;
}

bool calldown_in_loadout(const sim_t *s, int idx, calldown_type_t type)
{
    if (type == CALLDOWN_REINFORCE || type == CALLDOWN_RESUPPLY || type == CALLDOWN_EXTRACTION)
        return true;
    for (int k = 0; k < LOADOUT_SLOTS; k++)
        if (s->settings.loadout[idx][k] == type)
            return true;
    return false;
}

bool calldown_available(const sim_t *s, int idx, calldown_type_t type)
{
    if (s->cooldowns[idx][type] || !calldown_in_loadout(s, idx, type))
        return false;
    if (type == CALLDOWN_REINFORCE)
        return s->reinforcements > 0 && anyone_dead(s) && !beacon_out(s, CALLDOWN_REINFORCE);
    if (type == CALLDOWN_EXTRACTION)
        return mission_can_call_extraction(s, idx) && !beacon_out(s, CALLDOWN_EXTRACTION);
    return true;
}

// --- Code entry and the throw ------------------------------------------------------

static bool throw_beacon(sim_t *s, int idx, calldown_type_t type)
{
    beacon_t *b = NULL;
    for (int i = 0; i < SIM_MAX_BEACONS && !b; i++)
        if (!s->beacons[i].active)
            b = &s->beacons[i];
    if (!b)
        return false;
    const player_t *p = &s->players[idx];
    *b = (beacon_t){
        .active = true,
        .type = (uint8_t)type,
        .owner = (uint8_t)idx,
        .state = BEACON_FLYING,
        .timer = BEACON_FLIGHT_TICKS,
        .pos = v2_add(p->pos, v2_scale(p->facing, PLAYER_RADIUS + 0.2f)),
        .vel = v2_scale(p->facing, BEACON_THROW_SPEED),
        .dir = p->facing,
    };
    if (!s->settings.free_calldowns)
        s->cooldowns[idx][type] = calldown_defs[type].cooldown;
    s->mission.stats.players[idx].calldowns++;
    sim_push_event(s, EV_BEACON_THROWN, (uint8_t)type, p->pos, p->facing);
    return true;
}

void calldown_player_input(sim_t *s, int idx, const player_intent_t *in, bool can_act)
{
    player_t *p = &s->players[idx];
    code_entry_t *c = &p->code;

    if (in->code_mode) {
        if (!c->entering) {
            c->entering = true;
            c->len = 0;
        }
        if (in->code_input == CODE_NONE || !can_act)
            return;
        if (c->len < CALLDOWN_CODE_MAX) {
            c->inputs[c->len++] = in->code_input;
            if (calldown_prefix(c->inputs, c->len) || mission_terminal_prefix(s, idx, c->inputs, c->len)) {
                sim_push_event(s, EV_CODE_INPUT, (uint8_t)idx, p->pos, p->facing);
                return;
            }
        }
        c->len = 0; // wrong input: start over
        sim_push_event(s, EV_CODE_REJECTED, (uint8_t)idx, p->pos, p->facing);
        return;
    }

    if (!c->entering)
        return;
    c->entering = false;
    int len = c->len;
    c->len = 0;
    if (len == 0)
        return; // R tapped without a code
    // The terminal's code (its codes never start with a calldown's).
    if (can_act && len == s->mission.upload.code_len && mission_terminal_prefix(s, idx, c->inputs, len)) {
        mission_upload_start(s, idx);
        return;
    }
    int type = calldown_match(c->inputs, len);
    if (type < 0 || !can_act || !calldown_available(s, idx, (calldown_type_t)type) ||
        !throw_beacon(s, idx, (calldown_type_t)type))
        sim_push_event(s, EV_CODE_REJECTED, (uint8_t)idx, p->pos, p->facing);
}

// --- Explosions ---------------------------------------------------------------------

static float falloff(float dist, float radius)
{
    float x = dist > 0.0f ? dist / radius : 0.0f;
    return 1.0f - (1.0f - EXPLOSION_EDGE) * x;
}

void sim_explode(sim_t *s, vec2 pos, float radius, float damage, blast_kind_t kind, int by)
{
    sim_push_event(s, EV_EXPLOSION, (uint8_t)kind, pos, v2(0.0f, 0.0f));

    float reach = radius + ENEMY_MAX_RADIUS;
    grid_iter_t it;
    grid_iter_init(&it, s, v2(pos.x - reach, pos.y - reach), v2(pos.x + reach, pos.y + reach));
    int ei;
    while ((ei = grid_iter_next(&it)) >= 0) {
        const enemy_t *e = &s->enemies[ei];
        if (!e->active)
            continue;
        float d = v2_len(v2_sub(e->pos, pos)) - enemy_defs[e->type].radius;
        if (d < radius) // a zero shot direction never counts as the armored front
            enemy_damage(s, ei, damage * falloff(d, radius), e->pos, v2(0.0f, 0.0f), by);
    }
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &s->players[i];
        if (!sim_player_alive(p))
            continue;
        float d = v2_len(v2_sub(p->pos, pos)) - PLAYER_RADIUS;
        if (d < radius)
            player_damage(s, i, damage * falloff(d, radius));
    }
    for (int n = 0; n < s->arena.site_count; n++) {
        if (!mission_nest_live(s, n))
            continue;
        float d = v2_len(v2_sub(s->arena.sites[n], pos)) - NEST_RADIUS;
        if (d < radius)
            mission_damage_nest(s, n, damage * falloff(d, radius), pos);
    }
}

// --- Arrivals ----------------------------------------------------------------------

static void reinforce_at(sim_t *s, vec2 pos)
{
    int n = 0;
    for (int i = 0; i < SIM_MAX_PLAYERS && s->reinforcements > 0; i++) {
        player_t *p = &s->players[i];
        if (!p->active || p->state != PSTATE_DEAD)
            continue;
        // Side by side around the pod.
        vec2 at = v2_add(pos, v2((float)(n % 2) * 1.0f - 0.5f, (float)(n / 2) * -1.0f));
        sim_spawn_player(s, i, at);
        s->reinforcements--;
        s->mission.stats.reinforcements++;
        n++;
        sim_push_event(s, EV_REINFORCED, (uint8_t)i, at, v2(0.0f, 1.0f));
    }
}

static void add_pickup(sim_t *s, pickup_type_t type, int uses, int life, vec2 pos)
{
    for (int i = 0; i < SIM_MAX_PICKUPS; i++) {
        pickup_t *k = &s->pickups[i];
        if (k->active)
            continue;
        *k = (pickup_t){ .active = true, .type = (uint8_t)type, .uses = (uint8_t)uses,
                         .life = (uint16_t)life, .pos = pos };
        return;
    }
}

static void add_strike(sim_t *s, const beacon_t *b)
{
    for (int i = 0; i < SIM_MAX_STRIKES; i++) {
        strike_t *k = &s->strikes[i];
        if (k->active)
            continue;
        *k = (strike_t){ .active = true, .type = b->type, .owner = b->owner, .timer = 1,
                         .center = b->pos, .dir = b->dir };
        return;
    }
}

static void add_turret(sim_t *s, const beacon_t *b)
{
    for (int i = 0; i < SIM_MAX_TURRETS; i++) {
        turret_t *t = &s->turrets[i];
        if (t->active)
            continue;
        *t = (turret_t){ .active = true, .owner = b->owner, .ammo = SENTRY_AMMO, .life = SENTRY_LIFE,
                         .target = -1, .pos = b->pos, .facing = b->dir };
        return;
    }
}

// A minefield: mines scattered around the beacon, clear of rocks.
static void scatter_mines(sim_t *s, const beacon_t *b)
{
    int placed = 0;
    for (int i = 0; i < SIM_MAX_MINES && placed < MINE_COUNT; i++) {
        mine_t *m = &s->mines[i];
        if (m->active)
            continue;
        float ang = rng_float(&s->rng) * 6.2831853f;
        float r = sqrtf(rng_float(&s->rng)) * MINE_SPREAD;
        vec2 at = v2_add(b->pos, v2(cosf(ang) * r, sinf(ang) * r));
        arena_resolve(&s->arena, &at, 0.2f);
        *m = (mine_t){ .active = true, .owner = b->owner, .life = MINE_LIFE, .pos = at };
        placed++;
    }
}

static void add_fire(sim_t *s, vec2 pos, int owner)
{
    for (int i = 0; i < SIM_MAX_FIRES; i++) {
        fire_t *f = &s->fires[i];
        if (f->active)
            continue;
        *f = (fire_t){ .active = true, .owner = (uint8_t)owner, .life = FIRE_LIFE, .pos = pos };
        return;
    }
}

static void arrive(sim_t *s, const beacon_t *b)
{
    sim_push_event(s, EV_CALLDOWN, b->type, b->pos, b->dir);
    switch (b->type) {
    case CALLDOWN_REINFORCE:
        sim_explode(s, b->pos, DROP_CRUSH_RADIUS, DROP_CRUSH_DAMAGE, BLAST_IMPACT, b->owner);
        reinforce_at(s, b->pos);
        break;
    case CALLDOWN_RESUPPLY:
        sim_explode(s, b->pos, DROP_CRUSH_RADIUS, DROP_CRUSH_DAMAGE, BLAST_IMPACT, b->owner);
        add_pickup(s, PICKUP_RESUPPLY, RESUPPLY_USES, RESUPPLY_LIFE, b->pos);
        break;
    case CALLDOWN_MACHINE_GUN:
        sim_explode(s, b->pos, DROP_CRUSH_RADIUS, DROP_CRUSH_DAMAGE, BLAST_IMPACT, b->owner);
        add_pickup(s, PICKUP_MACHINE_GUN, 1, MG_PICKUP_LIFE, b->pos);
        break;
    case CALLDOWN_AIRSTRIKE:
    case CALLDOWN_BARRAGE:
    case CALLDOWN_NAPALM:
        add_strike(s, b);
        break;
    case CALLDOWN_MINEFIELD:
        sim_explode(s, b->pos, DROP_CRUSH_RADIUS, DROP_CRUSH_DAMAGE, BLAST_IMPACT, b->owner);
        scatter_mines(s, b);
        break;
    case CALLDOWN_LAUNCHER:
        sim_explode(s, b->pos, DROP_CRUSH_RADIUS, DROP_CRUSH_DAMAGE, BLAST_IMPACT, b->owner);
        add_pickup(s, PICKUP_LAUNCHER, 1, MG_PICKUP_LIFE, b->pos);
        break;
    case CALLDOWN_SENTRY:
        sim_explode(s, b->pos, DROP_CRUSH_RADIUS, DROP_CRUSH_DAMAGE, BLAST_IMPACT, b->owner);
        add_turret(s, b);
        break;
    case CALLDOWN_EXTRACTION:
        mission_start_extraction(s);
        break;
    }
}

// --- Per-tick updates ----------------------------------------------------------------

static void update_beacon(sim_t *s, beacon_t *b)
{
    if (b->state == BEACON_FLYING) {
        b->pos = v2_add(b->pos, v2_scale(b->vel, SIM_DT));
        // A rock or the arena edge stops the throw where it hits.
        bool stopped = false;
        for (int i = 0; i < s->arena.obstacle_count; i++)
            stopped |= push_out_circle(&b->pos, 0.15f, s->arena.obstacles[i].pos, s->arena.obstacles[i].radius);
        const arena_t *a = &s->arena;
        vec2 inside = v2(clampf(b->pos.x, a->min.x + 0.5f, a->max.x - 0.5f),
                         clampf(b->pos.y, a->min.y + 0.5f, a->max.y - 0.5f));
        stopped |= inside.x != b->pos.x || inside.y != b->pos.y;
        b->pos = inside;
        if (stopped || --b->timer == 0) {
            b->state = BEACON_ARMED;
            b->vel = v2(0.0f, 0.0f);
            b->timer = calldown_defs[b->type].delay;
        }
        return;
    }
    if (--b->timer == 0) {
        b->active = false;
        arrive(s, b);
    }
}

static void update_strike(sim_t *s, strike_t *k)
{
    if (--k->timer)
        return;
    if (k->type == CALLDOWN_AIRSTRIKE) {
        float along = ((float)k->done - (AIRSTRIKE_BOMBS - 1) * 0.5f) * AIRSTRIKE_SPACING;
        sim_explode(s, v2_add(k->center, v2_scale(k->dir, along)), AIRSTRIKE_RADIUS, AIRSTRIKE_DAMAGE, BLAST_BOMB, k->owner);
        k->timer = AIRSTRIKE_INTERVAL;
        k->active = ++k->done < AIRSTRIKE_BOMBS;
    } else if (k->type == CALLDOWN_NAPALM) {
        float along = ((float)k->done - (NAPALM_PATCHES - 1) * 0.5f) * NAPALM_SPACING;
        vec2 at = v2_add(k->center, v2_scale(k->dir, along));
        sim_explode(s, at, NAPALM_BURST_RADIUS, NAPALM_BURST_DAMAGE, BLAST_NAPALM, k->owner);
        add_fire(s, at, k->owner);
        k->timer = NAPALM_INTERVAL;
        k->active = ++k->done < NAPALM_PATCHES;
    } else {
        // Uniform over the disc: radius from the square root.
        float ang = rng_float(&s->rng) * 6.2831853f;
        float r = sqrtf(rng_float(&s->rng)) * BARRAGE_AREA;
        vec2 at = v2_add(k->center, v2(cosf(ang) * r, sinf(ang) * r));
        sim_explode(s, at, BARRAGE_RADIUS, BARRAGE_DAMAGE, BLAST_SHELL, k->owner);
        k->timer = BARRAGE_INTERVAL;
        k->active = ++k->done < BARRAGE_SHELLS;
    }
}

static int turret_target(const sim_t *s, vec2 pos)
{
    int best = -1;
    float best_d2 = SENTRY_RANGE * SENTRY_RANGE;
    for (int n = 0; n < s->active_count; n++) {
        const enemy_t *e = &s->enemies[s->active[n]];
        if (!e->active)
            continue;
        float d2 = v2_dist2(e->pos, pos);
        if (d2 < best_d2) {
            best_d2 = d2;
            best = s->active[n];
        }
    }
    return best;
}

static void update_turret(sim_t *s, int idx)
{
    turret_t *t = &s->turrets[idx];
    if (t->life)
        t->life--;
    if (t->life == 0 || t->ammo == 0) {
        t->active = false;
        return;
    }
    if (t->fire_cooldown)
        t->fire_cooldown--;

    const enemy_t *e = t->target >= 0 ? &s->enemies[t->target] : NULL;
    bool lost = !e || !e->active || v2_dist2(e->pos, t->pos) > SENTRY_RANGE * SENTRY_RANGE;
    if (lost || (s->tick + (uint32_t)idx) % SENTRY_RETARGET == 0) {
        t->target = (int16_t)turret_target(s, t->pos);
        e = t->target >= 0 ? &s->enemies[t->target] : NULL;
    }
    if (!e)
        return;

    // Turn towards the target at a limited rate; fire once roughly on it.
    vec2 to = v2_norm_or(v2_sub(e->pos, t->pos), t->facing);
    float angle = atan2f(t->facing.x * to.y - t->facing.y * to.x, v2_dot(t->facing, to));
    float turn = clampf(angle, -SENTRY_TURN, SENTRY_TURN);
    t->facing = v2_norm_or(v2_rotate(t->facing, turn), t->facing);
    if (fabsf(angle - turn) > SENTRY_AIM_TOLERANCE || t->fire_cooldown)
        return;
    float dev = (rng_float(&s->rng) * 2.0f - 1.0f) * SENTRY_SPREAD;
    vec2 muzzle = v2_add(t->pos, v2_scale(t->facing, 0.8f));
    sim_spawn_projectile(s, (uint8_t)(OWNER_TURRET | idx), WEAPON_MACHINE_GUN, muzzle,
                         v2_rotate(t->facing, dev), SENTRY_SPEED, SENTRY_RANGE + 2.0f, SENTRY_DAMAGE);
    sim_push_event(s, EV_SHOT, WEAPON_MACHINE_GUN, muzzle, t->facing);
    t->ammo--;
    t->fire_cooldown = SENTRY_FIRE_INTERVAL;
}

static bool needs_ammo(const player_t *p)
{
    for (int i = 0; i < PLAYER_WEAPON_SLOTS; i++) {
        const weapon_slot_t *w = &p->weapons[i];
        const weapon_def_t *def = &weapon_defs[w->type];
        if (w->owned && (w->mag < def->mag_size || w->reserve < def->start_reserve))
            return true;
    }
    return false;
}

static void update_pickup(sim_t *s, pickup_t *k)
{
    if (--k->life == 0) {
        k->active = false;
        return;
    }
    const float reach = PICKUP_RADIUS + PLAYER_RADIUS;
    for (int i = 0; i < SIM_MAX_PLAYERS && k->active; i++) {
        player_t *p = &s->players[i];
        if (!sim_player_alive(p) || v2_dist2(p->pos, k->pos) > reach * reach)
            continue;
        if (k->type == PICKUP_RESUPPLY) {
            if (!needs_ammo(p))
                continue;
            sim_refill_ammo(s, i);
            k->active = --k->uses > 0;
        } else {
            // A support weapon: it takes the third slot (replacing whatever
            // support weapon was there).
            p->weapons[WEAPON_SLOT_SUPPORT] =
                weapon_slot_full(k->type == PICKUP_LAUNCHER ? WEAPON_LAUNCHER : WEAPON_MACHINE_GUN);
            p->weapon = WEAPON_SLOT_SUPPORT;
            p->reload_timer = 0;
            k->active = false;
        }
        sim_push_event(s, EV_PICKUP, k->type, k->pos, p->facing);
    }
}

// Everyone dead: an emergency reinforce drops after a delay while
// reinforcements remain; without them the squad is lost.
static void update_squad(sim_t *s)
{
    int active = 0, alive = 0;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        active += s->players[i].active;
        alive += sim_player_alive(&s->players[i]);
    }
    bool all_down = active > 0 && alive == 0;

    switch (s->squad) {
    case SQUAD_FIGHTING:
        if (!all_down)
            break;
        if (s->reinforcements > 0) {
            s->squad = SQUAD_DOWN;
            s->squad_timer = SQUAD_REINFORCE_TICKS;
        } else {
            s->squad = SQUAD_LOST;
            s->squad_timer = SQUAD_LOST_TICKS;
        }
        break;
    case SQUAD_DOWN:
        if (!all_down) { // a Reinforce thrown before the last death landed first
            s->squad = SQUAD_FIGHTING;
        } else if (--s->squad_timer == 0) {
            sim_explode(s, s->camera.target, DROP_CRUSH_RADIUS, DROP_CRUSH_DAMAGE, BLAST_IMPACT, SOURCE_NONE);
            reinforce_at(s, s->camera.target);
            s->squad = SQUAD_FIGHTING;
        }
        break;
    case SQUAD_LOST:
        if (!all_down) // someone joined and dropped in
            s->squad = SQUAD_FIGHTING;
        else if (s->squad_timer) // the mission fails at 0
            s->squad_timer--;
        break;
    }
}

static void update_mine(sim_t *s, mine_t *m)
{
    if (--m->life == 0) {
        m->active = false;
        return;
    }
    if (MINE_LIFE - m->life < MINE_ARM_TICKS)
        return;
    const float r = MINE_TRIGGER + ENEMY_MAX_RADIUS;
    grid_iter_t it;
    grid_iter_init(&it, s, v2(m->pos.x - r, m->pos.y - r), v2(m->pos.x + r, m->pos.y + r));
    int ei;
    while ((ei = grid_iter_next(&it)) >= 0) {
        const enemy_t *e = &s->enemies[ei];
        float reach = MINE_TRIGGER + enemy_defs[e->type].radius;
        if (e->active && v2_dist2(e->pos, m->pos) < reach * reach) {
            m->active = false;
            sim_explode(s, m->pos, MINE_RADIUS, MINE_DAMAGE, BLAST_MINE, m->owner);
            return;
        }
    }
}

// Burning napalm: every FIRE_TICK ticks, whatever stands in it takes
// FIRE_DPS's worth, bugs and troopers alike (its kills go to whoever threw it).
static void update_fire(sim_t *s, int idx)
{
    fire_t *f = &s->fires[idx];
    if (--f->life == 0) {
        f->active = false;
        return;
    }
    if ((f->life + (uint16_t)idx) % FIRE_TICK)
        return;
    const float dmg = FIRE_DPS * FIRE_TICK / SIM_HZ;
    const float r = FIRE_RADIUS + ENEMY_MAX_RADIUS;
    grid_iter_t it;
    grid_iter_init(&it, s, v2(f->pos.x - r, f->pos.y - r), v2(f->pos.x + r, f->pos.y + r));
    int ei;
    while ((ei = grid_iter_next(&it)) >= 0) {
        const enemy_t *e = &s->enemies[ei];
        float reach = FIRE_RADIUS + enemy_defs[e->type].radius;
        if (e->active && v2_dist2(e->pos, f->pos) < reach * reach)
            enemy_damage(s, ei, dmg, e->pos, v2(0.0f, 0.0f), f->owner);
    }
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &s->players[i];
        float reach = FIRE_RADIUS + PLAYER_RADIUS;
        if (sim_player_alive(p) && v2_dist2(p->pos, f->pos) < reach * reach)
            player_damage(s, i, dmg);
    }
}

void calldowns_update(sim_t *s)
{
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        for (int t = 0; t < CALLDOWN_COUNT; t++)
            if (s->cooldowns[i][t])
                s->cooldowns[i][t]--;

    for (int i = 0; i < SIM_MAX_BEACONS; i++)
        if (s->beacons[i].active)
            update_beacon(s, &s->beacons[i]);
    for (int i = 0; i < SIM_MAX_STRIKES; i++)
        if (s->strikes[i].active)
            update_strike(s, &s->strikes[i]);
    for (int i = 0; i < SIM_MAX_TURRETS; i++)
        if (s->turrets[i].active)
            update_turret(s, i);
    for (int i = 0; i < SIM_MAX_PICKUPS; i++)
        if (s->pickups[i].active)
            update_pickup(s, &s->pickups[i]);
    for (int i = 0; i < SIM_MAX_MINES; i++)
        if (s->mines[i].active)
            update_mine(s, &s->mines[i]);
    for (int i = 0; i < SIM_MAX_FIRES; i++)
        if (s->fires[i].active)
            update_fire(s, i);
    update_squad(s);
}
