#include "camera.h"
#include "test.h"
#include "test_util.h"

static bool on_screen(const sim_t *s, vec2 p)
{
    // Inside the visible ground: below/above the target, within the
    // (conservative) bottom-edge width.
    cam_extent_t e = camera_extent();
    const camera_t *c = &s->camera;
    return p.x >= c->target.x - c->dist * e.half_w && p.x <= c->target.x + c->dist * e.half_w &&
           p.y >= c->target.y - c->dist * e.below && p.y <= c->target.y + c->dist * e.above;
}

void test_camera_extent_is_sane(void)
{
    cam_extent_t e = camera_extent();
    CHECK(e.below > 0.0f);
    CHECK(e.above > e.below);          // pitched camera sees further ahead
    CHECK(e.half_w_top > e.half_w);    // trapezoid, wider at the top
}

void test_camera_follows_single_player(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].stick_x = 85;
    step_n(&s, in, SIM_HZ * 2);
    in[0].stick_x = 0;
    step_n(&s, in, SIM_HZ * 2); // let the smoothing settle
    CHECK_NEAR(s.camera.target.x, s.players[0].pos.x, 0.05);
    CHECK_NEAR(s.camera.target.y, s.players[0].pos.y, 0.05);
    CHECK_NEAR(s.camera.dist, CAM_DIST_MIN, 0.01);
}

void test_camera_zooms_out_to_fit_two(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(14.0f, 0.0f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, SIM_HZ * 3);
    CHECK(s.camera.dist > CAM_DIST_MIN + 1.0f);
    CHECK(s.camera.dist < CAM_DIST_MAX);
    CHECK_NEAR(s.camera.target.x, 7.0f, 0.1);
    CHECK(on_screen(&s, s.players[0].pos));
    CHECK(on_screen(&s, s.players[1].pos));
    // Not leashed: they are where we put them.
    CHECK_NEAR(s.players[1].pos.x, 14.0f, 1e-4);
}

void test_camera_leashes_at_max_zoom(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(30.0f, 0.0f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].stick_x = -85; // P1 walks west, away from P2
    step_n(&s, in, SIM_HZ * 6);
    CHECK_NEAR(s.camera.dist, CAM_DIST_MAX, 0.05);
    float spread = s.players[1].pos.x - s.players[0].pos.x;
    cam_extent_t e = camera_extent();
    CHECK(spread <= 2.0f * (CAM_DIST_MAX * e.half_w - CAM_MARGIN) + 0.01f);
    CHECK(on_screen(&s, s.players[0].pos));
    CHECK(on_screen(&s, s.players[1].pos));
}

void test_camera_ignores_dead_players(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(14.0f, 0.0f));
    player_damage(&s, 1, PLAYER_HP);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, SIM_HZ * 2);
    CHECK_NEAR(s.camera.target.x, 0.0f, 0.1);
    CHECK_NEAR(s.camera.dist, CAM_DIST_MIN, 0.05);
}

void test_camera_leash_stops_the_mover_only(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(15.0f, 0.0f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[1].connected = true;
    in[1].stick_x = 85; // P2 walks east, P1 stands still
    step_n(&s, in, SIM_HZ * 4);
    cam_extent_t e = camera_extent();
    float max_spread = 2.0f * (CAM_DIST_MAX * e.half_w - CAM_MARGIN);
    CHECK_NEAR(s.players[0].pos.x, 0.0f, 1e-4);              // not dragged
    CHECK_NEAR(s.players[1].pos.x, max_spread, 0.2);          // stopped at the edge
}
