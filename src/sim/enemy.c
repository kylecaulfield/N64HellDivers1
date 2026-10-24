#include "enemy.h"
#include "arena.h"
#include "collide.h"
#include "difficulty.h"
#include "flow.h"
#include "grid.h"
#include "sim_internal.h"

const enemy_def_t enemy_defs[ENEMY_TYPE_COUNT] = {
    [ENEMY_SKITTER] = {
        .name = "SKITTER", .radius = SKITTER_RADIUS, .hp = SKITTER_HP, .speed = SKITTER_SPEED,
        .melee_range = SKITTER_MELEE_RANGE, .melee_reach = SKITTER_MELEE_REACH, .damage = SKITTER_DAMAGE,
        .windup = SKITTER_WINDUP, .recover = SKITTER_RECOVER, .front_armor = 1.0f, .mass = 0.5f,
    },
    [ENEMY_GNASHER] = {
        .name = "GNASHER", .radius = GNASHER_RADIUS, .hp = GNASHER_HP, .speed = GNASHER_SPEED,
        .melee_range = GNASHER_MELEE_RANGE, .melee_reach = GNASHER_MELEE_REACH, .damage = GNASHER_DAMAGE,
        .windup = GNASHER_WINDUP, .recover = GNASHER_RECOVER, .front_armor = 1.0f, .mass = 1.0f,
    },
    [ENEMY_BULWARK] = {
        .name = "BULWARK", .radius = BULWARK_RADIUS, .hp = BULWARK_HP, .speed = BULWARK_SPEED,
        .melee_range = BULWARK_MELEE_RANGE, .melee_reach = BULWARK_MELEE_REACH, .damage = BULWARK_DAMAGE,
        .windup = BULWARK_WINDUP, .recover = BULWARK_RECOVER, .front_armor = BULWARK_FRONT_ARMOR, .mass = 6.0f,
    },
};

void enemies_collect_active(sim_t *s)
{
    uint16_t n = 0;
    for (int i = 0; i < SIM_MAX_ENEMIES; i++)
        if (s->enemies[i].active)
            s->active[n++] = (uint16_t)i;
    s->active_count = n;
}

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
            .hp = enemy_defs[type].hp,
            .flow_cell = FLOW_CELLS,
        };
        arena_resolve(&s->arena, &e->pos, enemy_defs[type].radius);
        s->counts.enemies++;
        // Visible to this tick's list users (aim assist, bots) right away; the
        // grid picks it up at its next rebuild.
        s->active[s->active_count++] = (uint16_t)i;
        return i;
    }
    return -1;
}

static vec2 ring_point(sim_t *s, vec2 center, float r_min, float r_max)
{
    float ang = rng_float(&s->rng) * 6.2832f;
    float r = r_min + rng_float(&s->rng) * (r_max - r_min);
    return v2_add(center, v2_rotate(v2(r, 0.0f), ang));
}

int sim_spawn_enemies_around(sim_t *s, enemy_type_t type, int count, vec2 center)
{
    int spawned = 0;
    for (int n = 0; n < count; n++) {
        if (sim_spawn_enemy(s, type, ring_point(s, center, SPAWN_RING_MIN, SPAWN_RING_MAX)) < 0)
            break;
        spawned++;
    }
    return spawned;
}

int sim_spawn_mix(sim_t *s, int count, vec2 center, float r_min, float r_max)
{
    int spawned = 0;
    for (int n = 0; n < count; n++) {
        // 50% skitters, 35% gnashers, 15% bulwarks, in a fixed rotation.
        static const uint8_t MIX[20] = {
            0, 1, 0, 0, 1, 2, 0, 1, 0, 1, 0, 0, 1, 2, 0, 1, 0, 1, 0, 2,
        };
        if (sim_spawn_enemy(s, (enemy_type_t)MIX[n % 20], ring_point(s, center, r_min, r_max)) < 0)
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
    s->active_count = 0;
}

void enemy_damage(sim_t *s, int idx, float amount, vec2 hit_pos, vec2 shot_dir, int by)
{
    enemy_t *e = &s->enemies[idx];
    if (!e->active)
        return;
    const enemy_def_t *def = &enemy_defs[e->type];
    // A shot travelling against the bug's facing hits its front arc.
    bool armored = def->front_armor < 1.0f && -v2_dot(shot_dir, e->facing) > FRONT_ARC_COS;
    e->hp -= armored ? amount * def->front_armor : amount;
    sim_push_event(s, EV_HIT_ENEMY, (uint8_t)(e->type | (armored ? EV_HIT_ARMORED : 0)), hit_pos, shot_dir);
    if (e->hp <= 0.0f) {
        e->active = false;
        s->counts.enemies--;
        s->kills++;
        s->mission.stats.kills[e->type]++;
        if (by >= 0 && by < SIM_MAX_PLAYERS)
            s->mission.stats.players[by].kills++;
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
    const enemy_def_t *def = &enemy_defs[e->type];
    float d2;
    int target = nearest_player(s, e->pos, &d2);
    if (target < 0) {
        e->state = ESTATE_CHASE; // nobody alive: wait
        return;
    }
    e->target = (uint8_t)target;
    vec2 to = v2_sub(s->players[target].pos, e->pos);

    switch (e->state) {
    case ESTATE_CHASE: {
        if (d2 < def->melee_range * def->melee_range) {
            e->facing = v2_norm_or(to, e->facing);
            e->state = ESTATE_WINDUP;
            e->timer = def->windup;
            break;
        }
        // Far away: follow the shared flow field around rocks. Close in:
        // head straight for the player.
        vec2 dir = v2(0.0f, 0.0f);
        if (d2 > FLOW_DIRECT_RANGE * FLOW_DIRECT_RANGE) {
            int cell = flow_cell_of(e->pos);
            if (cell != e->flow_cell || s->flow.builds != e->flow_build) {
                e->flow_cell = (uint16_t)cell;
                e->flow_build = s->flow.builds;
                e->flow_dir = flow_direction(&s->flow, e->pos);
            }
            dir = e->flow_dir;
        }
        if (v2_len2(dir) == 0.0f)
            dir = v2_norm_or(to, e->facing);
        e->facing = dir;
        e->pos = v2_add(e->pos, v2_scale(dir, def->speed * SIM_DT));
        break;
    }

    case ESTATE_WINDUP:
        // Telegraph: rooted, tracking the target. The blow lands only if the
        // player is still within reach, so diving away dodges it.
        e->facing = v2_norm_or(to, e->facing);
        if (--e->timer == 0) {
            if (d2 < def->melee_reach * def->melee_reach)
                player_damage(s, target, def->damage * (float)sim_difficulty(s)->damage / 100.0f);
            e->state = ESTATE_RECOVER;
            e->timer = def->recover;
        }
        break;

    case ESTATE_RECOVER:
        if (--e->timer == 0)
            e->state = ESTATE_CHASE;
        break;
    }
}

static void separate_pair(crowd_body_t *a, crowd_body_t *b)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    float min = a->radius + b->radius;
    float dist2 = dx * dx + dy * dy;
    if (dist2 >= min * min)
        return;
    float len = sqrtf(dist2);
    float nx = 1.0f, ny = 0.0f;
    if (len > 1e-6f) {
        float inv = 1.0f / len;
        nx = dx * inv;
        ny = dy * inv;
    }
    float overlap = (min - len) * ENEMY_SEPARATION;
    float push_a = overlap * b->mass / (a->mass + b->mass), push_b = overlap - push_a;
    a->x -= nx * push_a;
    a->y -= ny * push_a;
    b->x += nx * push_b;
    b->y += ny * push_b;
}

// Overlapping bugs push each other apart, heavier ones less. Pairs come from
// the grid: each cell against itself and its E, NE, N and NW neighbours, so
// every nearby pair is seen once. The grid was built at the start of the
// tick; bugs move < 0.25 m a tick, inside the cell's slack over the largest
// contact distance. The work happens on crowd_t's compact copy.
static void separate(sim_t *s)
{
    static const int8_t NX[4] = { 1, 1, 0, -1 }, NY[4] = { 0, 1, 1, 1 };
    const grid_t *g = &s->grid;
    crowd_t *c = &s->crowd;

    int n = 0;
    for (int cell = 0; cell < GRID_CELLS; cell++) {
        c->start[cell] = (uint16_t)n;
        for (int i = g->head[cell]; i >= 0; i = g->next[i]) {
            const enemy_t *e = &s->enemies[i];
            if (!e->active)
                continue;
            const enemy_def_t *def = &enemy_defs[e->type];
            c->body[n] = (crowd_body_t){ e->pos.x, e->pos.y, def->radius, def->mass };
            c->enemy[n++] = (uint16_t)i;
        }
    }
    c->start[GRID_CELLS] = (uint16_t)n;

    for (int y = 0; y < GRID_W; y++) {
        for (int x = 0; x < GRID_W; x++) {
            int cell = y * GRID_W + x;
            int end = c->start[cell + 1];
            for (int a = c->start[cell]; a < end; a++) {
                crowd_body_t *ba = &c->body[a];
                for (int b = a + 1; b < end; b++)
                    separate_pair(ba, &c->body[b]);
                for (int k = 0; k < 4; k++) {
                    int nx = x + NX[k], ny = y + NY[k];
                    if (nx < 0 || nx >= GRID_W || ny >= GRID_W)
                        continue;
                    int other = ny * GRID_W + nx;
                    for (int b = c->start[other]; b < c->start[other + 1]; b++)
                        separate_pair(ba, &c->body[b]);
                }
            }
        }
    }

    for (int i = 0; i < n; i++)
        s->enemies[c->enemy[i]].pos = v2(c->body[i].x, c->body[i].y);
}

// Push a bug out of rocks: the flow field lists the (at most two) rocks near
// each cell, so most bugs test none.
static void resolve_rocks(const sim_t *s, enemy_t *e)
{
    float radius = enemy_defs[e->type].radius;
    const uint8_t *near = s->flow.rocks[flow_cell_of(e->pos)];
    if (near[0] == FLOW_ROCKS_ALL) {
        arena_resolve(&s->arena, &e->pos, radius);
        return;
    }
    for (int k = 0; k < 2 && near[k]; k++) {
        const obstacle_t *o = &s->arena.obstacles[near[k] - 1];
        push_out_circle(&e->pos, radius, o->pos, o->radius);
    }
    arena_clamp(&s->arena, &e->pos, radius);
}

void enemies_update(sim_t *s)
{
    for (int n = 0; n < s->active_count; n++) {
        enemy_t *e = &s->enemies[s->active[n]];
        if (e->active)
            update_one(s, e);
    }
    sim_mark(s, SIM_PHASE_CROWD);
    separate(s);
    for (int n = 0; n < s->active_count; n++) {
        enemy_t *e = &s->enemies[s->active[n]];
        if (e->active)
            resolve_rocks(s, e);
    }
    grid_build(s); // positions changed: bullets and players query the fresh grid
}
