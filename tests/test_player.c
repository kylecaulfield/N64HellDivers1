#include "arena.h"
#include "test.h"
#include "test_util.h"

void test_player_moves_at_full_speed(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].stick_x = 85;
    step_n(&s, in, SIM_HZ);
    CHECK_NEAR(s.players[0].pos.x, PLAYER_SPEED, 0.01);
    CHECK_NEAR(s.players[0].pos.y, 0.0f, 1e-4);
    CHECK_NEAR(s.players[0].facing.x, 1.0f, 1e-4); // facing follows movement
}

void test_player_scheme_a_strafe_keeps_facing(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.infinite_ammo = true;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_Z;
    in[0].stick_x = 85;
    step_n(&s, in, SIM_HZ);
    CHECK_NEAR(s.players[0].facing.x, 0.0f, 1e-4); // still facing north
    CHECK_NEAR(s.players[0].facing.y, 1.0f, 1e-4);
    CHECK_NEAR(s.players[0].pos.x, PLAYER_STRAFE_SPEED, 0.01);
    CHECK(count_events(&s, EV_SHOT) > 0);
    // Every shot went north (no enemies, so no aim assist).
    for (int i = 0; i < s.event_count; i++)
        if (s.events[i].type == EV_SHOT)
            CHECK_NEAR(s.events[i].dir.y, 1.0f, 1e-4);
}

void test_player_scheme_b_aims_with_c(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.scheme[0] = SCHEME_B;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_CDOWN | SIM_BTN_CRIGHT;
    in[0].stick_y = 85; // walking north, shooting south-east
    sim_step(&s, in);
    CHECK_NEAR(s.players[0].facing.x, 0.7071f, 1e-3);
    CHECK_NEAR(s.players[0].facing.y, -0.7071f, 1e-3);
    CHECK_EQ_INT(count_events(&s, EV_SHOT), 1);
    CHECK(s.players[0].pos.y > 0.0f);
}

void test_player_dive(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].stick_x = 85;
    in[0].pressed = SIM_BTN_A;
    sim_step(&s, in);
    CHECK_EQ_INT(s.players[0].state, PSTATE_DIVING);

    // Mid-dive: fire is ignored and steering does nothing.
    in[0].pressed = 0;
    in[0].held = SIM_BTN_Z;
    in[0].stick_x = 0;
    in[0].stick_y = 85;
    step_n(&s, in, PLAYER_DIVE_TICKS - 1);
    CHECK_EQ_INT(count_events(&s, EV_SHOT), 0);
    CHECK_NEAR(s.players[0].pos.y, 0.0f, 1e-4);
    CHECK_NEAR(s.players[0].pos.x, PLAYER_DIVE_SPEED * PLAYER_DIVE_TICKS * SIM_DT, 0.05);

    // Then prone, then back on their feet.
    sim_step(&s, in);
    CHECK_EQ_INT(s.players[0].state, PSTATE_PRONE);
    step_n(&s, in, PLAYER_PRONE_TICKS);
    CHECK_EQ_INT(s.players[0].state, PSTATE_NORMAL);
}

void test_player_dive_cooldown(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].pressed = SIM_BTN_A;
    sim_step(&s, in);
    in[0].pressed = 0;
    step_n(&s, in, PLAYER_DIVE_TICKS + PLAYER_PRONE_TICKS);
    REQUIRE(s.players[0].state == PSTATE_NORMAL);
    in[0].pressed = SIM_BTN_A;
    sim_step(&s, in);
    CHECK_EQ_INT(s.players[0].state, PSTATE_NORMAL); // still cooling down
}

void test_player_blocked_by_rock_and_bounds(void)
{
    sim_t s;
    setup_empty(&s);
    s.arena.obstacles[0] = (obstacle_t){ .pos = v2(3.0f, 0.0f), .radius = 1.0f };
    s.arena.obstacle_count = 1;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].stick_x = 85;
    step_n(&s, in, SIM_HZ);
    CHECK(s.players[0].pos.x <= 3.0f - 1.0f - PLAYER_RADIUS + 1e-4f);

    vec2 p = v2(100.0f, -100.0f);
    arena_resolve(&s.arena, &p, PLAYER_RADIUS);
    CHECK_NEAR(p.x, ARENA_HALF_SIZE - PLAYER_RADIUS, 1e-4);
    CHECK_NEAR(p.y, -ARENA_HALF_SIZE + PLAYER_RADIUS, 1e-4);
}

void test_player_bugs_shove(void)
{
    sim_t s;
    setup_empty(&s);
    int e = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.3f, 0.0f));
    REQUIRE(e >= 0);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    sim_step(&s, in);
    float d = v2_len(v2_sub(s.players[0].pos, s.enemies[e].pos));
    CHECK(d >= PLAYER_RADIUS + GNASHER_RADIUS - 1e-3f);
}
