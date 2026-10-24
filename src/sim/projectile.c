#include "collide.h"
#include "enemy.h"
#include "grid.h"
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
    int hit_enemy = -1, hit_player = -1;
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
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *pl = &s->players[i];
        float t;
        if (i != pr->owner && sim_player_alive(pl) &&
            segment_circle(p0, p1, pl->pos, PLAYER_RADIUS, &t) && t < best_t) {
            best_t = t;
            hit_enemy = -1;
            hit_world = false;
            hit_player = i;
        }
    }

    if (best_t <= 1.0f) {
        vec2 at = v2_add(p0, v2_scale(step, best_t));
        if (hit_player >= 0)
            player_damage(s, hit_player, pr->damage);
        else if (hit_enemy >= 0)
            enemy_damage(s, hit_enemy, pr->damage, at, v2_norm_or(step, v2(0.0f, 1.0f)));
        else if (hit_world)
            sim_push_event(s, EV_HIT_WORLD, 0, at, v2_norm_or(step, v2(0.0f, 1.0f)));
        pr->active = false;
        return;
    }

    pr->pos = p1;
    pr->range -= step_len;
    const arena_t *a = &s->arena;
    if (pr->range <= 0.0f || p1.x < a->min.x || p1.x > a->max.x || p1.y < a->min.y || p1.y > a->max.y)
        pr->active = false;
}

void projectiles_update(sim_t *s)
{
    for (int i = 0; i < SIM_MAX_PROJECTILES; i++)
        if (s->projectiles[i].active)
            update_one(s, &s->projectiles[i]);
}
