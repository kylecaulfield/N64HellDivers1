#include "camera.h"
#include "mission.h"
#include "test.h"
#include "test_util.h"

// Joining an empty port drops a new trooper in by pod, near the squad, for
// free.
void test_join_drops_a_trooper_in(void)
{
    sim_t s;
    setup_empty(&s);
    CHECK_EQ_INT(s.humans, 1);
    int pool = s.reinforcements;
    CHECK(sim_player_join(&s, 2));
    const player_t *p = &s.players[2];
    CHECK(p->active && !p->bot);
    CHECK_EQ_INT(p->state, PSTATE_DROPPING);
    CHECK_EQ_INT(s.humans, 1 | 4);
    CHECK_EQ_INT(s.reinforcements, pool);
    CHECK(v2_dist2(p->pos, s.players[0].pos) < 5.0f * 5.0f);

    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[2].connected = true;
    step_n(&s, in, DROP_IN_TICKS);
    CHECK_EQ_INT(p->state, PSTATE_NORMAL);
    // Now it plays: stick right walks right.
    float x = p->pos.x;
    in[2].stick_x = 100;
    step_n(&s, in, 10);
    CHECK(p->pos.x > x + 0.5f);
    // Joining twice changes nothing.
    CHECK(sim_player_join(&s, 2));
    CHECK_EQ_INT(p->state, PSTATE_NORMAL);
}

// The pod never lands on a rock.
void test_join_pod_avoids_rocks(void)
{
    sim_t s;
    setup_empty(&s);
    // Rocks on the first candidate spots around the squad.
    s.arena.obstacles[0] = (obstacle_t){ .pos = v2(0.0f, -2.5f), .radius = 1.0f };
    s.arena.obstacles[1] = (obstacle_t){ .pos = v2(2.5f, -1.5f), .radius = 1.0f };
    s.arena.obstacle_count = 2;
    CHECK(sim_player_join(&s, 1));
    const player_t *p = &s.players[1];
    for (int i = 0; i < s.arena.obstacle_count; i++) {
        const obstacle_t *o = &s.arena.obstacles[i];
        CHECK(v2_dist2(p->pos, o->pos) > (o->radius + PLAYER_RADIUS) * (o->radius + PLAYER_RADIUS));
    }
}

// A bot's trooper is handed over as it stands, and keeps the person's scheme.
void test_join_takes_over_a_bot(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.scheme[1] = SCHEME_A;
    sim_set_bots(&s, true);
    CHECK_EQ_INT(s.settings.scheme[1], SCHEME_A); // bots don't overwrite it
    player_t *p = &s.players[1];
    p->hp = 40.0f;
    vec2 at = p->pos;
    CHECK(sim_player_join(&s, 1));
    CHECK(!p->bot && p->active);
    CHECK_NEAR(p->hp, 40.0f, 1e-6);
    CHECK_NEAR(p->pos.x, at.x, 1e-6);
    CHECK_EQ_INT(p->state, PSTATE_NORMAL);
    CHECK(s.players[2].bot && s.players[3].bot);

    // It stops moving on its own: no input, no movement.
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[1].connected = true;
    step_n(&s, in, 10);
    CHECK_NEAR(p->pos.x, at.x, 1e-4);
    CHECK_NEAR(p->pos.y, at.y, 1e-4);
}

void test_leave(void)
{
    sim_t s;
    setup_empty(&s);
    sim_player_join(&s, 1);
    sim_player_leave(&s, 1);
    CHECK(!s.players[1].active);
    CHECK_EQ_INT(s.humans, 1);
    // Player 1 never leaves.
    sim_player_leave(&s, 0);
    CHECK(s.players[0].active);
    CHECK_EQ_INT(s.humans, 1);

    // With bots on, a bot carries on with the trooper.
    sim_set_bots(&s, true);
    sim_player_join(&s, 3);
    s.players[3].hp = 25.0f;
    sim_player_leave(&s, 3);
    CHECK(s.players[3].active && s.players[3].bot);
    CHECK_NEAR(s.players[3].hp, 25.0f, 1e-6);
}

// Once the dropship is down there is no dropping in: they start with the
// next mission, which keeps everyone who is playing, and the bots.
void test_join_late_waits_for_the_next_mission(void)
{
    sim_t s;
    setup_empty(&s);
    s.mission.phase = MISSION_BOARDING;
    CHECK(!sim_player_join(&s, 2));
    CHECK(!s.players[2].active);
    CHECK_EQ_INT(s.humans, 1 | 4);

    sim_set_bots(&s, true);
    sim_restart(&s, 99);
    CHECK_EQ_INT(s.humans, 1 | 4);
    CHECK_EQ_INT(s.mission.phase, MISSION_DROP);
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        CHECK(s.players[i].active);
        CHECK_EQ_INT(s.players[i].state, PSTATE_DROPPING);
        CHECK_EQ_INT(s.players[i].bot, i == 1 || i == 3);
    }
    // Pods land apart.
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        for (int j = i + 1; j < SIM_MAX_PLAYERS; j++)
            CHECK(v2_dist2(s.players[i].pos, s.players[j].pos) > 1.5f * 1.5f);
}

// A fresh trooper dropping in saves a squad that was lost.
void test_join_rescues_a_lost_squad(void)
{
    sim_t s;
    setup_empty(&s);
    s.reinforcements = 0;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    sim_kill_player(&s, 0);
    sim_step(&s, in);
    CHECK_EQ_INT(s.squad, SQUAD_LOST);
    CHECK(sim_player_join(&s, 1));
    in[1].connected = true;
    step_n(&s, in, SQUAD_LOST_TICKS + 2);
    CHECK_EQ_INT(s.squad, SQUAD_FIGHTING);
    CHECK(!mission_over(&s));
}

// Four people pulling apart can't leave the shared screen.
void test_four_players_stay_on_screen(void)
{
    sim_t s;
    setup_empty(&s);
    // All three join at once: their pods land apart, and crush nobody.
    for (int i = 1; i < SIM_MAX_PLAYERS; i++)
        sim_player_join(&s, i);
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        for (int j = i + 1; j < SIM_MAX_PLAYERS; j++)
            CHECK(v2_dist2(s.players[i].pos, s.players[j].pos) > (DROP_CRUSH_RADIUS + PLAYER_RADIUS) *
                                                                (DROP_CRUSH_RADIUS + PLAYER_RADIUS));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        in[i].connected = true;
    step_n(&s, in, DROP_IN_TICKS);
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        CHECK(sim_player_alive(&s.players[i]) && s.players[i].state == PSTATE_NORMAL);
    // Each heads for a different corner.
    const int8_t dir[SIM_MAX_PLAYERS][2] = { { -100, 100 }, { 100, 100 }, { -100, -100 }, { 100, -100 } };
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        in[i].stick_x = dir[i][0];
        in[i].stick_y = dir[i][1];
    }
    step_n(&s, in, SIM_HZ * 8);
    cam_extent_t e = camera_extent();
    float max_w = 2.0f * CAM_DIST_MAX * e.half_w, max_h = 2.0f * CAM_DIST_MAX * e.below;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        for (int j = 0; j < SIM_MAX_PLAYERS; j++) {
            CHECK(fabsf(s.players[i].pos.x - s.players[j].pos.x) <= max_w + 0.01f);
            CHECK(fabsf(s.players[i].pos.y - s.players[j].pos.y) <= max_h + 0.01f);
        }
    // And they did spread out as far as that allows.
    CHECK(fabsf(s.players[1].pos.x - s.players[0].pos.x) > max_w * 0.5f);
}
