#include "test.h"
#include "test_util.h"
#include "weapons.h"

// One rifle bullet fired straight north from the origin.
static void fire_one(sim_t *s, vec2 at)
{
    s->projectiles[0] = (projectile_t){
        .active = true, .owner = 0, .weapon = WEAPON_RIFLE, .damage = 20.0f,
        .pos = at, .vel = v2(0.0f, weapon_defs[WEAPON_RIFLE].speed),
        .range = weapon_defs[WEAPON_RIFLE].range,
    };
}

void test_combat_swept_hit_no_tunnelling(void)
{
    sim_t s;
    setup_empty(&s);
    s.players[0].pos = v2(20.0f, -20.0f); // out of the way
    // The bullet moves 2 m per tick. Put the bug between two sample points
    // so a point test would miss it entirely.
    int e = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.0f, 3.0f));
    REQUIRE(e >= 0);
    fire_one(&s, v2(0.0f, 0.0f));
    s.enemies[e].pos = v2(0.0f, 3.0f); // sample points are y = 0, 2, 4
    refresh_world(&s);
    projectiles_update(&s);
    projectiles_update(&s);
    CHECK_EQ_INT(count_events(&s, EV_HIT_ENEMY), 1);
    CHECK_NEAR(s.enemies[e].hp, GNASHER_HP - 20.0f, 1e-4);
    CHECK(!s.projectiles[0].active);
}

void test_combat_rifle_kills_gnasher_in_three(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.enemy_cap = 1;
    int e = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.0f, 8.0f));
    REQUIRE(e >= 0);
    refresh_world(&s);
    for (int shot = 0; shot < 3; shot++) {
        fire_one(&s, v2(0.0f, 0.0f));
        for (int t = 0; t < 10; t++)
            projectiles_update(&s);
    }
    CHECK(!s.enemies[e].active);
    CHECK_EQ_INT(count_events(&s, EV_ENEMY_DIED), 1);
    CHECK_EQ_INT(s.kills, 1);
    CHECK_EQ_INT(s.counts.enemies, 0);
    // The slot is free again: the cap of 1 allows a new spawn.
    CHECK(sim_spawn_enemy(&s, ENEMY_GNASHER, v2(5.0f, 5.0f)) >= 0);
}

void test_combat_rock_blocks_bullet(void)
{
    sim_t s;
    setup_empty(&s);
    s.arena.obstacles[0] = (obstacle_t){ .pos = v2(0.0f, 5.0f), .radius = 1.0f };
    s.arena.obstacle_count = 1;
    int e = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.0f, 10.0f));
    refresh_world(&s);
    fire_one(&s, v2(0.0f, 0.0f));
    for (int t = 0; t < 10; t++)
        projectiles_update(&s);
    CHECK_EQ_INT(count_events(&s, EV_HIT_WORLD), 1);
    CHECK_EQ_INT(count_events(&s, EV_HIT_ENEMY), 0);
    CHECK_NEAR(s.enemies[e].hp, GNASHER_HP, 1e-6);
}

void test_combat_bullet_expires_at_range(void)
{
    sim_t s;
    setup_empty(&s);
    fire_one(&s, v2(0.0f, -30.0f));
    // 30 m at 2 m per tick: alive for 14 updates, gone after the 15th.
    for (int t = 0; t < 14; t++)
        projectiles_update(&s);
    CHECK(s.projectiles[0].active);
    projectiles_update(&s);
    CHECK(!s.projectiles[0].active);
}

void test_combat_bullet_leaves_arena(void)
{
    sim_t s;
    setup_empty(&s);
    fire_one(&s, v2(0.0f, ARENA_HALF_SIZE - 1.0f));
    projectiles_update(&s);
    CHECK(!s.projectiles[0].active);
}

void test_enemy_chases_nearest_player(void)
{
    sim_t s;
    setup_empty(&s);
    // Both players well inside the camera leash, so neither gets moved.
    sim_spawn_player(&s, 1, v2(8.0f, 0.0f));
    int e = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(8.0f, 10.0f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, SIM_HZ);
    CHECK_EQ_INT(s.enemies[e].target, 1);          // P2 is 10 m away, P1 ~12.8
    // Straight at P2 at full speed (via the flow field, so allow a little drift).
    CHECK_NEAR(s.enemies[e].pos.x, 8.0f, 0.15);
    CHECK_NEAR(s.enemies[e].pos.y, 10.0f - GNASHER_SPEED, 0.15);
}

void test_enemy_melee_windup_then_hit(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.0f, 1.2f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    // First tick starts the windup; the blow lands GNASHER_WINDUP ticks later.
    step_n(&s, in, GNASHER_WINDUP);
    CHECK_NEAR(s.players[0].hp, PLAYER_HP, 1e-6);
    sim_step(&s, in);
    CHECK_NEAR(s.players[0].hp, PLAYER_HP - GNASHER_DAMAGE, 1e-4);
    CHECK_EQ_INT(count_events(&s, EV_PLAYER_HURT), 1);
}

void test_enemy_melee_dodged_by_leaving_reach(void)
{
    sim_t s;
    setup_empty(&s);
    int e = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.0f, 1.2f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    sim_step(&s, in);
    REQUIRE(s.enemies[e].state == ESTATE_WINDUP);
    s.players[0].pos = v2(0.0f, -3.0f); // e.g. dived away
    step_n(&s, in, GNASHER_WINDUP);
    CHECK_NEAR(s.players[0].hp, PLAYER_HP, 1e-6);
    CHECK_EQ_INT(s.enemies[e].state, ESTATE_RECOVER);
}

void test_god_mode_blocks_damage(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.god_mode = true;
    player_damage(&s, 0, 1000.0f);
    CHECK_NEAR(s.players[0].hp, PLAYER_HP, 1e-6);
    CHECK(sim_player_alive(&s.players[0]));
}

void test_dead_player_stays_down_while_a_teammate_lives(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(3.0f, 0.0f)); // a human teammate who calls nothing
    player_damage(&s, 0, PLAYER_HP);
    CHECK(!sim_player_alive(&s.players[0]));
    CHECK_EQ_INT(count_events(&s, EV_PLAYER_DIED), 1);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[1].connected = true;
    step_n(&s, in, SIM_HZ * 20);
    CHECK(!sim_player_alive(&s.players[0])); // no respawn without a Reinforce
    CHECK_EQ_INT(s.squad, SQUAD_FIGHTING);
    CHECK_EQ_INT(s.reinforcements, REINFORCE_POOL);
}

void test_enemy_cap_is_enforced(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.enemy_cap = 3;
    int n = sim_spawn_enemies_around(&s, ENEMY_GNASHER, 5, v2(0.0f, 0.0f));
    CHECK_EQ_INT(n, 3);
    CHECK_EQ_INT(s.counts.enemies, 3);
}

void test_enemies_separate(void)
{
    sim_t s;
    setup_empty(&s);
    s.players[0].pos = v2(0.0f, -25.0f);
    int a = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.0f, 10.0f));
    int b = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.1f, 10.0f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, 10);
    float d = v2_len(v2_sub(s.enemies[a].pos, s.enemies[b].pos));
    CHECK(d > 2.0f * GNASHER_RADIUS * 0.8f);
}
