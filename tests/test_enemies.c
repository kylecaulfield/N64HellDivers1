#include "test.h"
#include "test_util.h"

// Distance closed on player 1 in one second of chasing from 15 m away.
static float closed_in_one_second(enemy_type_t type)
{
    sim_t s;
    setup_empty(&s);
    int e = sim_spawn_enemy(&s, type, v2(15.0f, 0.0f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, SIM_HZ);
    return 15.0f - v2_len(s.enemies[e].pos);
}

void test_enemy_speeds_by_type(void)
{
    CHECK_NEAR(closed_in_one_second(ENEMY_SKITTER), SKITTER_SPEED, 0.2);
    CHECK_NEAR(closed_in_one_second(ENEMY_GNASHER), GNASHER_SPEED, 0.2);
    CHECK_NEAR(closed_in_one_second(ENEMY_BULWARK), BULWARK_SPEED, 0.2);
}

void test_enemy_hp_by_type(void)
{
    sim_t s;
    setup_empty(&s);
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++) {
        int e = sim_spawn_enemy(&s, (enemy_type_t)t, v2(5.0f + 3.0f * (float)t, 5.0f));
        REQUIRE(e >= 0);
        CHECK_NEAR(s.enemies[e].hp, enemy_defs[t].hp, 1e-6);
    }
    CHECK(enemy_defs[ENEMY_SKITTER].hp < enemy_defs[ENEMY_GNASHER].hp);
    CHECK(enemy_defs[ENEMY_GNASHER].hp < enemy_defs[ENEMY_BULWARK].hp);
}

void test_bulwark_front_armor(void)
{
    sim_t s;
    setup_empty(&s);
    int e = sim_spawn_enemy(&s, ENEMY_BULWARK, v2(0.0f, 10.0f));
    s.enemies[e].facing = v2(0.0f, -1.0f); // facing south, towards the shooter

    // From the front (shot travelling north, into its face): reduced.
    enemy_damage(&s, e, 100.0f, v2(0.0f, 9.0f), v2(0.0f, 1.0f), SOURCE_NONE);
    CHECK_NEAR(s.enemies[e].hp, BULWARK_HP - 100.0f * BULWARK_FRONT_ARMOR, 1e-3);
    CHECK(s.events[s.event_count - 1].a & EV_HIT_ARMORED);

    // From behind (shot travelling south): full damage.
    float hp = s.enemies[e].hp;
    enemy_damage(&s, e, 100.0f, v2(0.0f, 11.0f), v2(0.0f, -1.0f), SOURCE_NONE);
    CHECK_NEAR(s.enemies[e].hp, hp - 100.0f, 1e-3);
    CHECK(!(s.events[s.event_count - 1].a & EV_HIT_ARMORED));

    // From the side: full damage.
    hp = s.enemies[e].hp;
    enemy_damage(&s, e, 50.0f, v2(-1.0f, 10.0f), v2(1.0f, 0.0f), SOURCE_NONE);
    CHECK_NEAR(s.enemies[e].hp, hp - 50.0f, 1e-3);
}

void test_gnasher_has_no_armor(void)
{
    sim_t s;
    setup_empty(&s);
    int e = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.0f, 10.0f));
    s.enemies[e].facing = v2(0.0f, -1.0f);
    enemy_damage(&s, e, 20.0f, v2(0.0f, 9.0f), v2(0.0f, 1.0f), SOURCE_NONE);
    CHECK_NEAR(s.enemies[e].hp, GNASHER_HP - 20.0f, 1e-4);
}

void test_separation_heavy_moves_less(void)
{
    sim_t s;
    setup_empty(&s);
    s.players[0].pos = v2(0.0f, -20.0f);
    int big = sim_spawn_enemy(&s, ENEMY_BULWARK, v2(0.0f, 10.0f));
    int small = sim_spawn_enemy(&s, ENEMY_SKITTER, v2(0.5f, 10.0f));
    vec2 big0 = s.enemies[big].pos, small0 = s.enemies[small].pos;
    refresh_world(&s);
    enemies_update(&s); // one tick: both step south, then separate
    float big_moved = fabsf(s.enemies[big].pos.x - big0.x);
    float small_moved = fabsf(s.enemies[small].pos.x - small0.x);
    CHECK(small_moved > big_moved * 4.0f);
}

void test_spawn_mix_proportions(void)
{
    sim_t s;
    setup_empty(&s);
    s.players[0].pos = v2(0.0f, 0.0f);
    int n = sim_spawn_mix(&s, 20, v2(0.0f, 0.0f), SPAWN_RING_MIN, SPAWN_RING_MAX);
    CHECK_EQ_INT(n, 20);
    int count[ENEMY_TYPE_COUNT] = {0};
    for (int i = 0; i < SIM_MAX_ENEMIES; i++)
        if (s.enemies[i].active)
            count[s.enemies[i].type]++;
    CHECK_EQ_INT(count[ENEMY_SKITTER], 10);
    CHECK_EQ_INT(count[ENEMY_GNASHER], 7);
    CHECK_EQ_INT(count[ENEMY_BULWARK], 3);
}

void test_skitter_melee_is_quick(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_enemy(&s, ENEMY_SKITTER, v2(0.0f, 0.85f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, SKITTER_WINDUP + 1);
    CHECK_NEAR(s.players[0].hp, PLAYER_HP - SKITTER_DAMAGE, 1e-4);
}
