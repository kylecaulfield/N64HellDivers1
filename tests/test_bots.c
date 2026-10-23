#include "camera.h"
#include "test.h"
#include "test_util.h"

void test_bots_join_and_leave(void)
{
    sim_t s;
    setup_empty(&s);
    sim_set_bots(&s, true);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    sim_step(&s, in);
    CHECK_EQ_INT(s.counts.players, 4);
    for (int i = 1; i < SIM_MAX_PLAYERS; i++) {
        CHECK(s.players[i].active);
        CHECK(s.players[i].bot);
    }
    CHECK(!s.players[0].bot);
    sim_set_bots(&s, false);
    sim_step(&s, in);
    CHECK_EQ_INT(s.counts.players, 1);
}

void test_bots_shoot_bugs_in_range(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.god_mode = true;
    sim_set_bots(&s, true);
    sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.0f, 12.0f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, SIM_HZ * 3);
    CHECK(count_events(&s, EV_SHOT) >= 3); // a gnasher takes 3 rifle hits
    CHECK_EQ_INT(s.kills, 1);
    // Nothing left in range: they stop shooting.
    sim_clear_events(&s);
    step_n(&s, in, SIM_HZ);
    CHECK_EQ_INT(count_events(&s, EV_SHOT), 0);
}

void test_bots_follow_the_leader(void)
{
    sim_t s;
    setup_empty(&s);
    sim_set_bots(&s, true);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].stick_x = 85; // P1 walks east for 3 s
    step_n(&s, in, SIM_HZ * 3);
    in[0].stick_x = 0;
    step_n(&s, in, SIM_HZ * 2);
    for (int i = 1; i < SIM_MAX_PLAYERS; i++)
        CHECK(v2_len(v2_sub(s.players[i].pos, s.players[0].pos)) < 7.0f);
}

void test_bot_stays_a_bot_after_respawn(void)
{
    sim_t s;
    setup_empty(&s);
    sim_set_bots(&s, true);
    player_damage(&s, 2, PLAYER_HP);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, PLAYER_RESPAWN_TICKS + 1);
    CHECK(sim_player_alive(&s.players[2]));
    CHECK(s.players[2].bot);
}

void test_force_max_zoom(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.force_max_zoom = true;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, SIM_HZ * 3);
    CHECK_NEAR(s.camera.dist, CAM_DIST_MAX, 0.05);
}

static int marks[SIM_PHASE_COUNT * 2];
static int nmarks;
static void record(void *ctx, sim_phase_t phase)
{
    (void)ctx;
    if (nmarks < (int)(sizeof(marks) / sizeof(marks[0])))
        marks[nmarks++] = phase;
}

void test_sim_phase_marks_in_order(void)
{
    sim_t s;
    setup_empty(&s);
    s.mark = record;
    nmarks = 0;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    sim_step(&s, in);
    CHECK_EQ_INT(nmarks, SIM_PHASE_COUNT);
    for (int i = 0; i < nmarks; i++)
        CHECK_EQ_INT(marks[i], i);
}
