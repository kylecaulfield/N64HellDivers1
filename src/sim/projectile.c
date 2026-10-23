#include "collide.h"
#include "sim_internal.h"

// Bullets move up to 2 m per tick, more than a bug is wide, so every step is
// a swept segment test against everything it could hit.
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
    int hit_enemy = -1;
    bool hit_world = false;

    for (int i = 0; i < SIM_MAX_ENEMIES; i++) {
        const enemy_t *e = &s->enemies[i];
        float t;
        if (e->active && segment_circle(p0, p1, e->pos, GNASHER_RADIUS, &t) && t < best_t) {
            best_t = t;
            hit_enemy = i;
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

    if (best_t <= 1.0f) {
        vec2 at = v2_add(p0, v2_scale(step, best_t));
        if (hit_enemy >= 0)
            enemy_damage(s, hit_enemy, pr->damage, at);
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
