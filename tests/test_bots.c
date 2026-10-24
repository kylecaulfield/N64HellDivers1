#include "bot.h"
#include "calldown.h"
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

void test_bots_reinforce_a_dead_teammate(void)
{
    sim_t s;
    setup_empty(&s);
    sim_set_bots(&s, true);
    player_damage(&s, 2, PLAYER_HP);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    // A bot enters the code (4 inputs, every other tick), throws, and the
    // pod lands after the flight and the call-in delay.
    int budget = 8 + BEACON_FLIGHT_TICKS + calldown_defs[CALLDOWN_REINFORCE].delay + 10;
    int t = 0;
    while (!sim_player_alive(&s.players[2]) && t < budget) {
        sim_step(&s, in);
        t++;
    }
    CHECK(sim_player_alive(&s.players[2]));
    CHECK(s.players[2].bot); // still a bot after coming back
    CHECK_EQ_INT(s.reinforcements, REINFORCE_POOL - 1);
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

void test_bot_holds_fire_with_a_teammate_in_line(void)
{
    sim_t s;
    setup_empty(&s);
    sim_set_bots(&s, true);
    s.players[0].pos = v2(0.0f, 0.0f);
    s.players[1].pos = v2(0.0f, -4.0f);
    s.players[2].pos = v2(-10.0f, -10.0f); // the other bots well clear
    s.players[3].pos = v2(10.0f, -10.0f);
    sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.0f, 6.0f)); // player 1 stands between
    player_intent_t it = bot_intent(&s, 1);
    CHECK(!it.fire);
    CHECK(v2_len(it.move) > 0.5f); // steps aside instead
    s.players[0].pos = v2(5.0f, 0.0f);
    it = bot_intent(&s, 1);
    CHECK(it.fire);
}

// Once the dropship is down, bots head for the LZ and board, wherever they
// were, even after everyone else has gone aboard.
void test_bots_board_the_dropship(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.god_mode = true;
    sim_set_bots(&s, true);
    s.players[0].pos = s.arena.lz;
    for (int i = 1; i < SIM_MAX_PLAYERS; i++)
        s.players[i].pos = v2_add(s.arena.lz, v2(-14.0f + 7.0f * (float)i, -12.0f));
    s.mission.phase = MISSION_BOARDING;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, SIM_HZ * 15);
    CHECK_EQ_INT(s.mission.phase, MISSION_SUCCESS);
    CHECK_EQ_INT(s.mission.extracted, 0xF);
}
