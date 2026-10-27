#include "calldown.h"
#include "collide.h"
#include "enemy.h"
#include "grid.h"
#include "mission.h"
#include "sim_internal.h"

int sim_spawn_projectile(sim_t *s, uint8_t owner, uint8_t weapon, vec2 pos, vec2 dir,
                         float speed, float range, float damage)
{
    for (int i = 0; i < SIM_MAX_PROJECTILES; i++) {
        projectile_t *pr = &s->projectiles[i];
        if (pr->active)
            continue;
        *pr = (projectile_t){
            .active = true,
            .owner = owner,
            .weapon = weapon,
            .damage = damage,
            .pos = pos,
            .vel = v2_scale(dir, speed),
            .range = range,
        };
        return i;
    }
    return -1;
}

// Bullets move up to 2 m per tick, more than a bug is wide, so every step is
// a swept segment test against everything it could hit. Friendly fire is on:
// players other than the shooter are targets too.
static void update_one(sim_t *s, projectile_t *pr)
{
    vec2 step = v2_scale(pr->vel, SIM_DT);
    float step_len = v2_len(step);
    if (step_len > pr->range) {
        step = v2_scale(step, pr->range / step_len);
        step_len = pr->range;
    }
    vec2 p0 = pr->pos, p1 = v2_add(p0, step);

    float best_t = 2.0f;
    int hit_enemy = -1, hit_player = -1, hit_nest = -1;
    bool hit_world = false;

    // Only bugs in grid cells the segment could touch.
    const float r = ENEMY_MAX_RADIUS;
    grid_iter_t it;
    grid_iter_init(&it, s, v2(fminf(p0.x, p1.x) - r, fminf(p0.y, p1.y) - r),
                   v2(fmaxf(p0.x, p1.x) + r, fmaxf(p0.y, p1.y) + r));
    int ei;
    while ((ei = grid_iter_next(&it)) >= 0) {
        const enemy_t *e = &s->enemies[ei];
        float t;
        if (e->active && segment_circle(p0, p1, e->pos, enemy_defs[e->type].radius, &t) && t < best_t) {
            best_t = t;
            hit_enemy = ei;
        }
    }
    for (int i = 0; i < s->arena.obstacle_count; i++) {
        const obstacle_t *o = &s->arena.obstacles[i];
        float t;
        if (segment_circle(p0, p1, o->pos, o->radius, &t) && t < best_t) {
            best_t = t;
            hit_enemy = -1;
            hit_world = true;
        }
    }
    // The objective's structures: live nests take the hit, the terminal
    // stops it like a rock.
    const mission_t *m = &s->mission;
    for (int n = 0; n < s->arena.site_count; n++) {
        float t;
        float radius = m->objective == OBJ_UPLOAD && n == 0 ? TERMINAL_RADIUS : NEST_RADIUS;
        bool solid = m->objective == OBJ_UPLOAD ? n == 0 : mission_nest_live(s, n);
        if (solid && segment_circle(p0, p1, s->arena.sites[n], radius, &t) && t < best_t) {
            best_t = t;
            hit_enemy = -1;
            hit_world = m->objective == OBJ_UPLOAD;
            hit_nest = hit_world ? -1 : n;
        }
    }
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *pl = &s->players[i];
        float t;
        if (i != pr->owner && sim_player_alive(pl) &&
            segment_circle(p0, p1, pl->pos, PLAYER_RADIUS, &t) && t < best_t) {
            best_t = t;
            hit_enemy = -1;
            hit_world = false;
            hit_nest = -1;
            hit_player = i;
        }
    }

    // A sentry's bullets count as whoever called the sentry in.
    bool by_player = pr->owner < SIM_MAX_PLAYERS;
    int by = by_player ? pr->owner : s->turrets[pr->owner & ~OWNER_TURRET].owner;
    bool explosive = pr->weapon == WEAPON_LAUNCHER;
    if (best_t <= 1.0f) {
        vec2 at = v2_add(p0, v2_scale(step, best_t));
        if (explosive) { // a grenade bursts on whatever it hits
            if (by_player && (hit_enemy >= 0 || hit_nest >= 0))
                s->mission.stats.players[by].hits++;
            pr->active = false;
            sim_explode(s, at, LAUNCHER_RADIUS, LAUNCHER_DAMAGE, BLAST_GRENADE, by);
            return;
        }
        if (hit_player >= 0) {
            if (by_player)
                s->mission.stats.players[by].friendly_hits++;
            player_damage(s, hit_player, pr->damage);
        } else if (hit_enemy >= 0) {
            if (by_player)
                s->mission.stats.players[by].hits++;
            enemy_damage(s, hit_enemy, pr->damage, at, v2_norm_or(step, v2(0.0f, 1.0f)), by);
        } else if (hit_nest >= 0) {
            if (by_player)
                s->mission.stats.players[by].hits++;
            mission_damage_nest(s, hit_nest, pr->damage * NEST_BULLET_MULT, at);
        } else if (hit_world)
            sim_push_event(s, EV_HIT_WORLD, 0, at, v2_norm_or(step, v2(0.0f, 1.0f)));
        pr->active = false;
        return;
    }

    pr->pos = p1;
    pr->range -= step_len;
    const arena_t *a = &s->arena;
    if (pr->range <= 0.0f || p1.x < a->min.x || p1.x > a->max.x || p1.y < a->min.y || p1.y > a->max.y) {
        pr->active = false;
        if (explosive) // out of range: it goes off where it is
            sim_explode(s, p1, LAUNCHER_RADIUS, LAUNCHER_DAMAGE, BLAST_GRENADE, by);
    }
}

void projectiles_update(sim_t *s)
{
    for (int i = 0; i < SIM_MAX_PROJECTILES; i++)
        if (s->projectiles[i].active)
            update_one(s, &s->projectiles[i]);
}
