#include "arena.h"
#include "test.h"

#include <string.h>

void test_arena_rocks_valid(void)
{
    for (uint32_t seed = 1; seed <= 20; seed++) {
        rng_t rng;
        rng_seed(&rng, seed);
        arena_t a;
        arena_generate(&a, &rng);
        CHECK(a.obstacle_count > ARENA_ROCK_COUNT / 2);
        CHECK(a.obstacle_count <= ARENA_ROCK_COUNT);
        for (int i = 0; i < a.obstacle_count; i++) {
            const obstacle_t *o = &a.obstacles[i];
            CHECK(v2_len(o->pos) >= ARENA_CLEAR_RADIUS);                 // drop zone clear
            CHECK(o->pos.x - o->radius > a.min.x && o->pos.x + o->radius < a.max.x);
            CHECK(o->pos.y - o->radius > a.min.y && o->pos.y + o->radius < a.max.y);
            CHECK_NEAR(o->radius, rock_radius[o->kind], 1e-6);
            for (int j = i + 1; j < a.obstacle_count; j++)
                CHECK(v2_len(v2_sub(o->pos, a.obstacles[j].pos)) > o->radius + a.obstacles[j].radius);
        }
    }
}

void test_arena_deterministic(void)
{
    rng_t r1, r2;
    rng_seed(&r1, 77);
    rng_seed(&r2, 77);
    arena_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    arena_generate(&a, &r1);
    arena_generate(&b, &r2);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
}
