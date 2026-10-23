#include "arena.h"
#include "collide.h"
#include "sim_internal.h"

int sim_spawn_enemy(sim_t *s, enemy_type_t type, vec2 pos)
{
    if (s->counts.enemies >= s->settings.enemy_cap)
        return -1;
    for (int i = 0; i < SIM_MAX_ENEMIES; i++) {
        enemy_t *e = &s->enemies[i];
        if (e->active)
            continue;
        *e = (enemy_t){
            .active = true,
            .type = (uint8_t)type,
            .state = ESTATE_CHASE,
            .pos = pos,
            .facing = v2(0.0f, -1.0f),
            .hp = GNASHER_HP,
        };
        arena_resolve(&s->arena, &e->pos, GNASHER_RADIUS);
        s->counts.enemies++;
        return i;
    }
    return -1;
}

int sim_spawn_enemies_around(sim_t *s, enemy_type_t type, int count, vec2 center)
{
    int spawned = 0;
    for (int n = 0; n < count; n++) {
        float ang = rng_float(&s->rng) * 6.2832f;
        float r = SPAWN_RING_MIN + rng_float(&s->rng) * (SPAWN_RING_MAX - SPAWN_RING_MIN);
        vec2 pos = v2_add(center, v2_rotate(v2(r, 0.0f), ang));
        if (sim_spawn_enemy(s, type, pos) < 0)
            break;
        spawned++;
    }
    return spawned;
}

void sim_kill_all_enemies(sim_t *s)
{
    for (int i = 0; i < SIM_MAX_ENEMIES; i++)
        s->enemies[i].active = false;
    s->counts.enemies = 0;
}

void enemy_damage(sim_t *s, int idx, float amount, vec2 hit_pos)
{
    enemy_t *e = &s->enemies[idx];
    if (!e->active)
        return;
    e->hp -= amount;
    sim_push_event(s, EV_HIT_ENEMY, (uint8_t)e->type, hit_pos, e->facing);
    if (e->hp <= 0.0f) {
        e->active = false;
        s->counts.enemies--;
        s->kills++;
        sim_push_event(s, EV_ENEMY_DIED, e->type, e->pos, e->facing);
    }
}

static int nearest_player(const sim_t *s, vec2 pos, float *dist2)
{
    int best = -1;
    float best_d2 = 0.0f;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &s->players[i];
        if (!sim_player_alive(p))
            continue;
        float d2 = v2_dist2(p->pos, pos);
        if (best < 0 || d2 < best_d2) {
            best = i;
            best_d2 = d2;
        }
    }
    *dist2 = best_d2;
    return best;
}

static void update_one(sim_t *s, enemy_t *e)
{
    float d2;
    int target = nearest_player(s, e->pos, &d2);
    if (target < 0) {
        // Nobody alive: idle in place.
        e->state = ESTATE_CHASE;
        return;
    }
    e->target = (uint8_t)target;
    vec2 tpos = s->players[target].pos;
    vec2 to = v2_sub(tpos, e->pos);

    switch (e->state) {
    case ESTATE_CHASE:
        e->facing = v2_norm_or(to, e->facing);
        if (d2 < GNASHER_MELEE_RANGE * GNASHER_MELEE_RANGE) {
            e->state = ESTATE_WINDUP;
            e->timer = GNASHER_WINDUP;
        } else {
            // Straight line for now; the shared flow field arrives in M2.
            e->pos = v2_add(e->pos, v2_scale(e->facing, GNASHER_SPEED * SIM_DT));
        }
        break;

    case ESTATE_WINDUP:
        // Telegraph: rooted, tracking the target. The blow lands only if the
        // player is still within reach, so diving away dodges it.
        e->facing = v2_norm_or(to, e->facing);
        if (--e->timer == 0) {
            if (d2 < GNASHER_MELEE_REACH * GNASHER_MELEE_REACH)
                player_damage(s, target, GNASHER_DAMAGE);
            e->state = ESTATE_RECOVER;
            e->timer = GNASHER_RECOVER;
        }
        break;

    case ESTATE_RECOVER:
        if (--e->timer == 0)
            e->state = ESTATE_CHASE;
        break;
    }
}

void enemies_update(sim_t *s)
{
    for (int i = 0; i < SIM_MAX_ENEMIES; i++)
        if (s->enemies[i].active)
            update_one(s, &s->enemies[i]);

    // Separation: overlapping bugs push each other apart. O(n^2) is fine for
    // M1's handful; M2 replaces this with the spatial hash.
    for (int i = 0; i < SIM_MAX_ENEMIES; i++) {
        enemy_t *a = &s->enemies[i];
        if (!a->active)
            continue;
        for (int j = i + 1; j < SIM_MAX_ENEMIES; j++) {
            enemy_t *b = &s->enemies[j];
            if (!b->active)
                continue;
            vec2 d = v2_sub(b->pos, a->pos);
            float min = 2.0f * GNASHER_RADIUS;
            float d2 = v2_len2(d);
            if (d2 >= min * min)
                continue;
            float len = sqrtf(d2);
            vec2 n = len > 1e-6f ? v2_scale(d, 1.0f / len) : v2(1.0f, 0.0f);
            vec2 push = v2_scale(n, (min - len) * 0.5f * ENEMY_SEPARATION);
            a->pos = v2_sub(a->pos, push);
            b->pos = v2_add(b->pos, push);
        }
    }
    for (int i = 0; i < SIM_MAX_ENEMIES; i++)
        if (s->enemies[i].active)
            arena_resolve(&s->arena, &s->enemies[i].pos, GNASHER_RADIUS);
}

void spawner_update(sim_t *s)
{
    if (!s->settings.trickle_spawn)
        return;
    if (s->spawn_timer) {
        s->spawn_timer--;
        return;
    }
    s->spawn_timer = TRICKLE_INTERVAL;
    if (s->counts.enemies < TRICKLE_MAX_ALIVE)
        sim_spawn_enemies_around(s, ENEMY_GNASHER, 1, s->camera.target);
}
