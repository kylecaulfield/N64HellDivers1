#include "test.h"
#include "test_util.h"
#include "weapons.h"

void test_weapon_rifle_fire_rate_and_ammo(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_Z; // scheme A: fire

    step_n(&s, in, 30);
    // Fires on the first tick, then every fire_interval ticks.
    int expected = (30 - 1) / weapon_defs[WEAPON_RIFLE].fire_interval + 1;
    CHECK_EQ_INT(count_events(&s, EV_SHOT), expected);
    CHECK_EQ_INT(s.players[0].weapons[0].mag, weapon_defs[WEAPON_RIFLE].mag_size - expected);
}

void test_weapon_tap_b_reloads_from_reserve(void)
{
    sim_t s;
    setup_empty(&s);
    player_t *p = &s.players[0];
    p->weapons[0].mag = 10;
    uint16_t reserve = p->weapons[0].reserve;

    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_B;
    sim_step(&s, in);
    in[0].held = 0;
    sim_step(&s, in); // release -> reload starts
    CHECK_EQ_INT(count_events(&s, EV_RELOAD), 1);
    CHECK(p->reload_timer > 0);

    step_n(&s, in, weapon_defs[WEAPON_RIFLE].reload_ticks);
    CHECK_EQ_INT(p->reload_timer, 0);
    CHECK_EQ_INT(p->weapons[0].mag, 30);
    CHECK_EQ_INT(p->weapons[0].reserve, reserve - 20);
}

void test_weapon_partial_reserve(void)
{
    sim_t s;
    setup_empty(&s);
    player_t *p = &s.players[0];
    p->weapons[0].mag = 0;
    p->weapons[0].reserve = 5;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_Z; // firing on empty auto-reloads
    sim_step(&s, in);
    CHECK(p->reload_timer > 0);
    CHECK_EQ_INT(count_projectiles(&s), 0);
    in[0].held = 0;
    step_n(&s, in, weapon_defs[WEAPON_RIFLE].reload_ticks);
    CHECK_EQ_INT(p->weapons[0].mag, 5);
    CHECK_EQ_INT(p->weapons[0].reserve, 0);
}

void test_weapon_no_fire_while_reloading(void)
{
    sim_t s;
    setup_empty(&s);
    player_t *p = &s.players[0];
    p->weapons[0].mag = 0;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_Z;
    step_n(&s, in, weapon_defs[WEAPON_RIFLE].reload_ticks - 1);
    CHECK_EQ_INT(count_events(&s, EV_SHOT), 0);
    step_n(&s, in, 2); // reload completes, firing resumes
    CHECK_EQ_INT(count_events(&s, EV_SHOT), 1);
}

void test_weapon_dry_fire(void)
{
    sim_t s;
    setup_empty(&s);
    player_t *p = &s.players[0];
    p->weapons[0].mag = 0;
    p->weapons[0].reserve = 0;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_Z;
    sim_step(&s, in);
    CHECK_EQ_INT(count_events(&s, EV_DRY_FIRE), 1);
    CHECK_EQ_INT(count_projectiles(&s), 0);
    CHECK_EQ_INT(p->reload_timer, 0);
}

void test_weapon_switch_to_shotgun_fires_pellets(void)
{
    sim_t s;
    setup_empty(&s);
    player_t *p = &s.players[0];
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_B;
    step_n(&s, in, HOLD_B_SWITCH);
    CHECK_EQ_INT(p->weapon, 1);
    CHECK_EQ_INT(p->weapons[1].type, WEAPON_SHOTGUN);

    in[0].held = SIM_BTN_Z;
    step_n(&s, in, PLAYER_SWITCH_TICKS + 1);
    CHECK_EQ_INT(count_events(&s, EV_SHOT), 1);
    CHECK_EQ_INT(count_projectiles(&s), weapon_defs[WEAPON_SHOTGUN].pellets);
    CHECK_EQ_INT(p->weapons[1].mag, weapon_defs[WEAPON_SHOTGUN].mag_size - 1);
}

void test_weapon_switch_cancels_reload(void)
{
    sim_t s;
    setup_empty(&s);
    player_t *p = &s.players[0];
    p->weapons[0].mag = 0;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_Z;
    sim_step(&s, in);
    CHECK(p->reload_timer > 0);
    in[0].held = SIM_BTN_B;
    step_n(&s, in, HOLD_B_SWITCH);
    CHECK_EQ_INT(p->reload_timer, 0);
    CHECK_EQ_INT(p->weapons[0].mag, 0);
}

void test_weapon_infinite_ammo(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.infinite_ammo = true;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_Z;
    step_n(&s, in, 60);
    CHECK(count_events(&s, EV_SHOT) > 10);
    CHECK_EQ_INT(s.players[0].weapons[0].mag, 30);
}

void test_aim_assist_bends_towards_target(void)
{
    sim_t s;
    setup_empty(&s);
    // Target 5 degrees right of straight north, 10 m away.
    vec2 tdir = v2_rotate(v2(0.0f, 1.0f), -0.087f);
    sim_spawn_enemy(&s, ENEMY_GNASHER, v2_scale(tdir, 10.0f));

    vec2 a = aim_assist(&s, v2(0.0f, 0.0f), v2(0.0f, 1.0f), SCHEME_A);
    CHECK(a.x > 0.0f);                 // bent towards it
    CHECK(a.x < tdir.x + 1e-4f);       // but not past it
    CHECK_NEAR(v2_len(a), 1.0f, 1e-4);

    vec2 b = aim_assist(&s, v2(0.0f, 0.0f), v2(0.0f, 1.0f), SCHEME_B);
    CHECK_NEAR(b.x, tdir.x, 1e-3);     // scheme B snaps
}

void test_aim_assist_respects_cone_and_range(void)
{
    sim_t s;
    setup_empty(&s);
    // 20 degrees off: outside A's cone, inside B's.
    vec2 off20 = v2_rotate(v2(0.0f, 1.0f), -0.35f);
    sim_spawn_enemy(&s, ENEMY_GNASHER, v2_scale(off20, 8.0f));
    vec2 a = aim_assist(&s, v2(0.0f, 0.0f), v2(0.0f, 1.0f), SCHEME_A);
    CHECK_NEAR(a.x, 0.0f, 1e-6);
    vec2 b = aim_assist(&s, v2(0.0f, 0.0f), v2(0.0f, 1.0f), SCHEME_B);
    CHECK(b.x > 0.3f);

    // Dead ahead but out of range: ignored.
    sim_kill_all_enemies(&s);
    sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.5f, AIM_RANGE + 2.0f));
    b = aim_assist(&s, v2(0.0f, 0.0f), v2(0.0f, 1.0f), SCHEME_B);
    CHECK_NEAR(b.x, 0.0f, 1e-6);
}

void test_aim_assist_prefers_aligned_target(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_enemy(&s, ENEMY_GNASHER, v2_scale(v2_rotate(v2(0.0f, 1.0f), 0.3f), 6.0f));
    vec2 aligned = v2_rotate(v2(0.0f, 1.0f), -0.05f);
    sim_spawn_enemy(&s, ENEMY_GNASHER, v2_scale(aligned, 12.0f));
    vec2 b = aim_assist(&s, v2(0.0f, 0.0f), v2(0.0f, 1.0f), SCHEME_B);
    CHECK_NEAR(b.x, aligned.x, 1e-3);
}
