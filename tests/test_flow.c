#include "test.h"
#include "test_util.h"

// Run builds until one completes; returns the number of flow_step calls.
static int build_full(sim_t *s, int budget)
{
    uint32_t start = s->flow.builds;
    int steps = 0;
    while (s->flow.builds == start && steps < 10000) {
        flow_step(s, budget);
        steps++;
    }
    return steps;
}

void test_flow_open_field_points_at_player(void)
{
    sim_t s;
    setup_empty(&s);
    build_full(&s, FLOW_CELLS * 2);
    REQUIRE(s.flow.ready);
    CHECK_EQ_INT(s.flow.overflows, 0);
    CHECK_EQ_INT(flow_distance(&s.flow, v2(0.2f, 0.2f)), 0);
    // Distance grows away from the player, ~2 per metre.
    uint16_t d5 = flow_distance(&s.flow, v2(5.5f, 0.5f));
    uint16_t d10 = flow_distance(&s.flow, v2(10.5f, 0.5f));
    CHECK(d5 > 0 && d10 > d5);
    CHECK_NEAR(d10, 20, 1.0);

    // From several places, the direction points (roughly) at the origin.
    const vec2 from[] = { { 10, 0 }, { -12, 3 }, { 7, -9 }, { -20, -20 }, { 0, 25 } };
    for (unsigned i = 0; i < sizeof(from) / sizeof(from[0]); i++) {
        vec2 dir = flow_direction(&s.flow, from[i]);
        vec2 want = v2_norm_or(v2_scale(from[i], -1.0f), v2(0, 0));
        CHECK(v2_dot(dir, want) > 0.9f);
    }
    CHECK_NEAR(v2_len(flow_direction(&s.flow, v2(0.3f, 0.3f))), 0.0f, 1e-6); // at the player
}

void test_flow_incremental_budget(void)
{
    sim_t s;
    setup_empty(&s);
    flow_step(&s, 100);
    CHECK(!s.flow.ready); // 100 cells is not a whole field
    int steps = 1 + build_full(&s, 100);
    CHECK(s.flow.ready);
    CHECK(steps >= FLOW_CELLS / 100);
    CHECK(steps <= FLOW_CELLS / 100 + 2);
    // While the next build runs, readers keep the last complete field.
    uint16_t before = flow_distance(&s.flow, v2(10.5f, 0.5f));
    flow_step(&s, 100);
    CHECK_EQ_INT(flow_distance(&s.flow, v2(10.5f, 0.5f)), before);
}

void test_flow_goes_around_a_wall(void)
{
    sim_t s;
    setup_empty(&s);
    // A wall of rocks along y = 5 from x = -10 to 10, player at the origin.
    int n = 0;
    for (float x = -10.0f; x <= 10.0f; x += 1.5f)
        s.arena.obstacles[n++] = (obstacle_t){ .pos = v2(x, 5.0f), .radius = 0.8f };
    s.arena.obstacle_count = n;
    flow_init(&s.flow, &s.arena);
    build_full(&s, FLOW_CELLS * 2);
    REQUIRE(s.flow.ready);

    // Straight behind the wall: the walk is much longer than 10 m, and the
    // way out is sideways, not through the rocks.
    vec2 behind = v2(0.5f, 10.5f);
    CHECK(flow_distance(&s.flow, behind) > 2 * 18);
    vec2 dir = flow_direction(&s.flow, behind);
    CHECK(fabsf(dir.x) > 0.5f);

    // A gnasher released behind the wall gets around it and reaches the
    // player (it would grind on the rocks forever walking straight).
    int e = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.5f, 9.0f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    s.settings.god_mode = true;
    bool reached = false;
    for (int t = 0; t < SIM_HZ * 15 && !reached; t++) {
        sim_step(&s, in);
        reached = s.enemies[e].state != ESTATE_CHASE;
    }
    CHECK(reached);
}

void test_flow_follows_nearest_player(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(20.0f, 0.0f));
    build_full(&s, FLOW_CELLS * 2);
    // Halfway-plus towards P2: flows to P2, not back to P1.
    vec2 dir = flow_direction(&s.flow, v2(14.0f, 0.5f));
    CHECK(dir.x > 0.9f);
    dir = flow_direction(&s.flow, v2(6.0f, 0.5f));
    CHECK(dir.x < -0.9f);
}

void test_flow_dead_players_are_not_sources(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(20.0f, 0.0f));
    player_damage(&s, 1, PLAYER_HP);
    build_full(&s, FLOW_CELLS * 2);
    vec2 dir = flow_direction(&s.flow, v2(18.0f, 0.5f));
    CHECK(dir.x < -0.9f); // only P1 at the origin pulls
}

void test_flow_step_masks_match_rocks(void)
{
    sim_t s;
    sim_init(&s, 99); // the generated arena, rocks and all
    static const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    int bad = 0, open = 0;
    for (int c = 0; c < FLOW_CELLS; c++) {
        int x = c % FLOW_W, y = c / FLOW_W;
        for (int k = 0; k < 8; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            bool want = nx >= 0 && ny >= 0 && nx < FLOW_W && ny < FLOW_W &&
                        !s.flow.blocked[ny * FLOW_W + nx] &&
                        (k < 4 || (!s.flow.blocked[y * FLOW_W + nx] && !s.flow.blocked[ny * FLOW_W + x]));
            bool have = (s.flow.steps[c] >> k) & 1;
            bad += want != have;
            open += have;
        }
    }
    CHECK_EQ_INT(bad, 0);
    CHECK(open > FLOW_CELLS * 6); // most of the arena is open ground
}

void test_flow_idles_while_no_bugs_alive(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    uint32_t builds = s.flow.builds;
    step_n(&s, in, 30);
    CHECK_EQ_INT(s.flow.builds, builds);
    CHECK(!s.flow.building);

    sim_spawn_enemy(&s, ENEMY_GNASHER, v2(15.0f, 0.0f));
    step_n(&s, in, 10);
    CHECK(s.flow.builds > builds);
    CHECK(s.flow.ready);
}

void test_flow_build_stops_past_the_farthest_bug(void)
{
    sim_t s;
    setup_empty(&s);
    build_full(&s, FLOW_CELLS * 2); // no previous field: the whole arena
    CHECK(flow_distance(&s.flow, v2(25.5f, 0.5f)) != FLOW_INF);

    sim_spawn_enemy(&s, ENEMY_GNASHER, v2(5.5f, 0.5f));
    int steps = build_full(&s, 64);
    CHECK(steps < FLOW_CELLS / 64 / 2); // far less than a full build
    uint16_t at_bug = flow_distance(&s.flow, v2(5.5f, 0.5f));
    CHECK_NEAR(at_bug, 10, 1.0);
    CHECK(flow_distance(&s.flow, v2(5.5f + 0.5f * FLOW_LIMIT_MARGIN - 1.0f, 0.5f)) != FLOW_INF);
    CHECK_EQ_INT(flow_distance(&s.flow, v2(25.5f, 0.5f)), FLOW_INF);
    CHECK(v2_dot(flow_direction(&s.flow, v2(5.5f, 0.5f)), v2(-1.0f, 0.0f)) > 0.9f);

    // A bug where the limited field never reached forces a full build.
    sim_spawn_enemy(&s, ENEMY_SKITTER, v2(25.5f, 0.5f));
    build_full(&s, FLOW_CELLS * 2);
    CHECK(flow_distance(&s.flow, v2(25.5f, 0.5f)) != FLOW_INF);
}

void test_flow_rebuilds_at_most_every_interval(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    sim_spawn_enemy(&s, ENEMY_BULWARK, v2(10.0f, 10.0f));
    step_n(&s, in, 5 * FLOW_REBUILD_TICKS);
    // A small field builds within a tick, so builds track the interval.
    CHECK(s.flow.builds >= 4);
    CHECK(s.flow.builds <= 6);
}

void test_flow_rock_table_lists_every_touching_rock(void)
{
    sim_t s;
    sim_init(&s, 7);
    rng_t r;
    rng_seed(&r, 99);
    int missing = 0, checked = 0;
    for (int n = 0; n < 20000; n++) {
        vec2 p = v2((rng_float(&r) * 2.0f - 1.0f) * ARENA_HALF_SIZE, (rng_float(&r) * 2.0f - 1.0f) * ARENA_HALF_SIZE);
        const uint8_t *near = s.flow.rocks[flow_cell_of(p)];
        if (near[0] == FLOW_ROCKS_ALL)
            continue;
        for (int i = 0; i < s.arena.obstacle_count; i++) {
            float reach = s.arena.obstacles[i].radius + ENEMY_MAX_RADIUS;
            if (v2_dist2(p, s.arena.obstacles[i].pos) >= reach * reach)
                continue;
            checked++;
            missing += near[0] != i + 1 && near[1] != i + 1;
        }
    }
    CHECK(checked > 100);
    CHECK_EQ_INT(missing, 0);
}
