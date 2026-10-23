#include "arena.h"
#include "collide.h"

const float rock_radius[ROCK_KIND_COUNT] = {
    [ROCK_A] = 0.9f,
    [ROCK_B] = 1.35f,
    [ROCK_C] = 0.55f,
};

void arena_generate(arena_t *a, rng_t *rng)
{
    a->min = v2(-ARENA_HALF_SIZE, -ARENA_HALF_SIZE);
    a->max = v2(ARENA_HALF_SIZE, ARENA_HALF_SIZE);
    a->obstacle_count = 0;

    const float edge = 2.0f;  // keep rocks off the boundary
    const float gap = 3.0f;   // walkable space between rocks
    for (int tries = 0; tries < 400 && a->obstacle_count < ARENA_ROCK_COUNT; tries++) {
        uint8_t kind = (uint8_t)rng_range(rng, ROCK_KIND_COUNT);
        float r = rock_radius[kind];
        vec2 p = v2(a->min.x + edge + rng_float(rng) * (2.0f * (ARENA_HALF_SIZE - edge)),
                    a->min.y + edge + rng_float(rng) * (2.0f * (ARENA_HALF_SIZE - edge)));
        if (v2_len2(p) < (ARENA_CLEAR_RADIUS + r) * (ARENA_CLEAR_RADIUS + r))
            continue;
        bool ok = true;
        for (int i = 0; i < a->obstacle_count && ok; i++) {
            float min = a->obstacles[i].radius + r + gap;
            ok = v2_dist2(a->obstacles[i].pos, p) >= min * min;
        }
        if (!ok)
            continue;
        a->obstacles[a->obstacle_count++] = (obstacle_t){
            .pos = p, .radius = r, .kind = kind, .yaw = rng_float(rng) * 6.2832f,
        };
    }
}

void arena_resolve(const arena_t *a, vec2 *pos, float radius)
{
    for (int i = 0; i < a->obstacle_count; i++)
        push_out_circle(pos, radius, a->obstacles[i].pos, a->obstacles[i].radius);
    arena_clamp(a, pos, radius);
}

void arena_clamp(const arena_t *a, vec2 *pos, float radius)
{
    pos->x = clampf(pos->x, a->min.x + radius, a->max.x - radius);
    pos->y = clampf(pos->y, a->min.y + radius, a->max.y - radius);
}
