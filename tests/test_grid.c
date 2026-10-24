#include "test.h"
#include "test_util.h"

static int query_count(const sim_t *s, vec2 mn, vec2 mx, int want)
{
    grid_iter_t it;
    grid_iter_init(&it, s, mn, mx);
    int n = 0, i, found = 0;
    while ((i = grid_iter_next(&it)) >= 0) {
        n++;
        if (i == want)
            found = 1;
    }
    return want >= 0 ? found : n;
}

void test_grid_finds_nearby_not_far(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.enemy_cap = SIM_MAX_ENEMIES;
    int near = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(5.0f, 5.0f));
    int far = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(-20.0f, 20.0f));
    refresh_world(&s);
    CHECK(query_count(&s, v2(3.0f, 3.0f), v2(7.0f, 7.0f), near));
    CHECK(!query_count(&s, v2(3.0f, 3.0f), v2(7.0f, 7.0f), far));
    CHECK(query_count(&s, v2(-21.0f, 19.0f), v2(-19.0f, 21.0f), far));
}

void test_grid_lists_everyone_once(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.enemy_cap = SIM_MAX_ENEMIES;
    // Many bugs piled into a few cells.
    for (int i = 0; i < 100; i++)
        sim_spawn_enemy(&s, ENEMY_SKITTER, v2((float)(i % 10) * 0.3f, (float)(i / 10) * 0.3f));
    refresh_world(&s);
    int whole = query_count(&s, v2(-40.0f, -40.0f), v2(40.0f, 40.0f), -1);
    CHECK_EQ_INT(whole, 100);
}

void test_grid_clamps_out_of_range_queries(void)
{
    sim_t s;
    setup_empty(&s);
    int e = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(ARENA_HALF_SIZE - 1.0f, ARENA_HALF_SIZE - 1.0f));
    refresh_world(&s);
    CHECK(query_count(&s, v2(28.0f, 28.0f), v2(100.0f, 100.0f), e));
}
