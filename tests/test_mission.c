#include "mission.h"
#include "test.h"
#include "test_util.h"

// Keep player `port` standing at `pos` for `n` ticks.
static void hold_at(sim_t *s, sim_input_t in[SIM_MAX_PLAYERS], int port, vec2 pos, int n)
{
    for (int t = 0; t < n; t++) {
        if (s->players[port].active)
            s->players[port].pos = pos;
        sim_step(s, in);
    }
}

static void capture_all(sim_t *s, sim_input_t in[SIM_MAX_PLAYERS])
{
    for (int z = 0; z < MISSION_ZONES; z++)
        hold_at(s, in, 0, s->arena.zones[z], mission_capture_ticks(s) + 1);
}

// --- Sites and the drop-in -----------------------------------------------------------

void test_mission_sites_are_spaced_and_clear_of_rocks(void)
{
    for (uint32_t seed = 1; seed < 40; seed++) {
        sim_t s;
        sim_init(&s, seed * 7919u);
        vec2 sites[MISSION_ZONES + 1] = { s.arena.zones[0], s.arena.zones[1], s.arena.lz };
        for (int i = 0; i <= MISSION_ZONES; i++) {
            float d = v2_len(sites[i]);
            CHECK(d >= ZONE_DIST_MIN - 1e-3f && d <= ZONE_DIST_MAX + 1e-3f);
            CHECK(fabsf(sites[i].x) <= ARENA_HALF_SIZE - SITE_EDGE + 1e-3f);
            CHECK(fabsf(sites[i].y) <= ARENA_HALF_SIZE - SITE_EDGE + 1e-3f);
            for (int j = 0; j < i; j++)
                CHECK(v2_len(v2_sub(sites[i], sites[j])) >= SITE_SPACING - 1e-3f);
            float clear = i == MISSION_ZONES ? ARENA_CLEAR_RADIUS : ZONE_CLEAR_RADIUS;
            for (int r = 0; r < s.arena.obstacle_count; r++) {
                const obstacle_t *o = &s.arena.obstacles[r];
                CHECK(v2_len(v2_sub(o->pos, sites[i])) >= clear + o->radius - 1e-3f);
            }
        }
    }
}

void test_mission_starts_with_a_drop_in(void)
{
    sim_t s;
    sim_init(&s, 42);
    CHECK_EQ_INT(s.mission.phase, MISSION_DROP);
    CHECK_EQ_INT(s.players[0].state, PSTATE_DROPPING);
    CHECK(sim_player_alive(&s.players[0]));
    int e = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(0.3f, 0.0f)); // under the pod
    player_damage(&s, 0, 50.0f);
    CHECK_NEAR(s.players[0].hp, PLAYER_HP, 1e-6); // riders can't be hurt

    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].stick_x = 80; // can't walk out of a pod
    step_n(&s, in, DROP_IN_TICKS - 1);
    CHECK_NEAR(s.players[0].pos.x, 0.0f, 0.01);
    sim_clear_events(&s);
    sim_step(&s, in);
    CHECK_EQ_INT(s.players[0].state, PSTATE_NORMAL);
    CHECK_EQ_INT(s.mission.phase, MISSION_OBJECTIVE);
    CHECK_EQ_INT(count_events(&s, EV_EXPLOSION), 1);
    CHECK(!s.enemies[e].active); // crushed by the landing
}

void test_waves_wait_for_the_landing(void)
{
    sim_t s;
    sim_init(&s, 42);
    s.settings.waves = true;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, DROP_IN_TICKS);
    CHECK_EQ_INT(s.waves.number, 0);
    step_n(&s, in, WAVE_FIRST_DELAY + 2);
    CHECK_EQ_INT(s.waves.number, 1);
}

// --- Objective --------------------------------------------------------------------------

void test_zone_fills_while_held_and_keeps_its_progress(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    vec2 z = s.arena.zones[0];
    int need = mission_capture_ticks(&s);
    hold_at(&s, in, 0, z, need / 2);
    CHECK_EQ_INT(s.mission.zones[0].held, need / 2);
    hold_at(&s, in, 0, v2(0.0f, 0.0f), 30); // step out: progress stays
    CHECK_EQ_INT(s.mission.zones[0].held, need / 2);
    hold_at(&s, in, 0, z, need - need / 2);
    CHECK(s.mission.zones[0].captured);
    CHECK_EQ_INT(count_events(&s, EV_ZONE_CAPTURED), 1);
    CHECK_EQ_INT(s.mission.phase, MISSION_OBJECTIVE); // the other zone is still open
}

void test_bugs_in_a_zone_stop_it(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    s.settings.god_mode = true;
    vec2 z = s.arena.zones[1];
    int e = sim_spawn_enemy(&s, ENEMY_BULWARK, v2(z.x + 2.0f, z.y));
    for (int t = 0; t < 60; t++) {
        s.enemies[e].pos = v2(z.x + 2.0f, z.y);
        s.players[0].pos = v2(z.x - 2.0f, z.y);
        sim_step(&s, in);
    }
    CHECK_EQ_INT(s.mission.zones[1].held, 0);
    CHECK_EQ_INT(s.mission.zones[1].bugs_in, 1);
    CHECK_EQ_INT(s.mission.zones[1].players_in, 1);
}

void test_quick_mission_divides_the_times(void)
{
    sim_t s;
    setup_empty(&s);
    CHECK_EQ_INT(mission_capture_ticks(&s), ZONE_CAPTURE_TICKS);
    CHECK_EQ_INT(mission_hold_ticks(&s), EXTRACT_HOLD_TICKS);
    s.settings.quick_mission = true;
    CHECK_EQ_INT(mission_capture_ticks(&s), ZONE_CAPTURE_TICKS / QUICK_MISSION_DIV);
    CHECK_EQ_INT(mission_hold_ticks(&s), EXTRACT_HOLD_TICKS / QUICK_MISSION_DIV);
}

void test_both_zones_unlock_extraction_from_the_lz(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.quick_mission = true;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    s.players[0].pos = s.arena.lz;
    CHECK(!calldown_available(&s, 0, CALLDOWN_EXTRACTION)); // objective not done
    capture_all(&s, in);
    CHECK_EQ_INT(s.mission.phase, MISSION_EXTRACT);
    s.players[0].pos = v2(0.0f, 0.0f);
    CHECK(!calldown_available(&s, 0, CALLDOWN_EXTRACTION)); // not in the LZ
    s.players[0].pos = s.arena.lz;
    CHECK(calldown_available(&s, 0, CALLDOWN_EXTRACTION));
}

void test_complete_objective_debug(void)
{
    sim_t s;
    setup_empty(&s);
    mission_complete_objective(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    sim_step(&s, in);
    CHECK_EQ_INT(s.mission.phase, MISSION_EXTRACT);
}

// --- Extraction ----------------------------------------------------------------------------

// Objective done, player 1 in the LZ, extraction called and arrived.
static void start_hold(sim_t *s, sim_input_t in[SIM_MAX_PLAYERS])
{
    s->settings.quick_mission = true;
    s->settings.god_mode = true;
    mission_complete_objective(s);
    sim_step(s, in);
    s->players[0].pos = s->arena.lz;
    s->players[0].facing = v2_norm_or(v2_scale(s->arena.lz, -1.0f), v2(0.0f, 1.0f)); // throw inwards
    enter_code(s, in, 0, CALLDOWN_EXTRACTION);
    hold_at(s, in, 0, s->arena.lz, BEACON_FLIGHT_TICKS + calldown_defs[CALLDOWN_EXTRACTION].delay + 1);
}

void test_extraction_holds_then_the_dropship_lands(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    start_hold(&s, in);
    REQUIRE(s.mission.phase == MISSION_HOLD);
    CHECK(s.mission.timer > mission_hold_ticks(&s) - 10);
    // Away from the LZ until touchdown, so nobody boards early.
    hold_at(&s, in, 0, v2(0.0f, 0.0f), s.mission.timer);
    CHECK_EQ_INT(s.mission.phase, MISSION_BOARDING);
    hold_at(&s, in, 0, v2(0.0f, 0.0f), 10);
    CHECK_EQ_INT(s.mission.phase, MISSION_BOARDING); // the ship waits
    hold_at(&s, in, 0, s.arena.lz, 1);
    CHECK_EQ_INT(s.mission.phase, MISSION_SUCCESS);
    CHECK_EQ_INT(s.mission.extracted, 1);
    CHECK(!s.players[0].active);
    CHECK_EQ_INT(count_events(&s, EV_EXTRACTED), 1);
    CHECK(mission_over(&s));

    // The world stops behind the results screen.
    uint32_t tick = s.tick;
    step_n(&s, in, 10);
    CHECK_EQ_INT(s.tick, tick);
}

void test_hold_waves_come_faster(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.waves = true;
    s.mission.phase = MISSION_HOLD;
    s.mission.timer = 1000;
    s.waves = (waves_t){ .number = 3, .state = WAVE_ACTIVE, .timer = 0, .total = 20 };
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    sim_step(&s, in); // wave cleared: the break starts
    CHECK_EQ_INT(s.waves.state, WAVE_BREAK);
    CHECK_EQ_INT(s.waves.timer, WAVE_BREAK_TICKS / HOLD_WAVE_SPEEDUP);
}

void test_boarding_waits_for_every_living_player(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(1.0f, 0.0f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[1].connected = true;
    s.mission.phase = MISSION_BOARDING;
    s.players[0].pos = s.arena.lz;
    s.players[1].pos = v2(0.0f, 0.0f);
    sim_step(&s, in);
    CHECK_EQ_INT(s.mission.extracted, 1);
    CHECK_EQ_INT(s.mission.phase, MISSION_BOARDING);
    s.players[1].pos = s.arena.lz;
    sim_step(&s, in);
    CHECK_EQ_INT(s.mission.extracted, 3);
    CHECK_EQ_INT(s.mission.phase, MISSION_SUCCESS);
}

void test_the_dead_do_not_hold_up_the_dropship(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(1.0f, 0.0f));
    player_damage(&s, 1, PLAYER_HP);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    s.mission.phase = MISSION_BOARDING;
    s.players[0].pos = s.arena.lz;
    sim_step(&s, in);
    CHECK_EQ_INT(s.mission.phase, MISSION_SUCCESS);
    CHECK_EQ_INT(s.mission.extracted, 1);
}

void test_squad_lost_fails_the_mission(void)
{
    sim_t s;
    setup_empty(&s);
    s.reinforcements = 0;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    player_damage(&s, 0, PLAYER_HP);
    step_n(&s, in, SQUAD_LOST_TICKS);
    CHECK_EQ_INT(s.mission.phase, MISSION_OBJECTIVE); // "SQUAD LOST" shows for a while
    step_n(&s, in, 2);
    CHECK_EQ_INT(s.mission.phase, MISSION_FAILED);
    CHECK(mission_over(&s));
}

// --- Results -----------------------------------------------------------------------------

void test_mission_stats(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    // A skitter right in front of player 1 (facing north), shot with the rifle.
    int e = sim_spawn_enemy(&s, ENEMY_SKITTER, v2(0.0f, 4.0f));
    s.enemies[e].state = ESTATE_RECOVER;
    s.enemies[e].timer = 1000;
    in[0].held = SIM_BTN_Z;
    step_n(&s, in, 12);
    in[0].held = 0;
    step_n(&s, in, 10);
    const mission_stats_t *st = &s.mission.stats;
    const player_stats_t *p1 = &st->players[0], *p2 = &st->players[1];
    CHECK(p1->shots >= 1);
    CHECK(p1->hits >= 1);
    CHECK_EQ_INT(p1->kills, 1);
    CHECK_EQ_INT(st->kills[ENEMY_SKITTER], 1);
    CHECK(st->ticks >= 22);

    // A teammate in the line of fire.
    sim_spawn_player(&s, 1, v2(0.0f, 3.0f));
    in[1].connected = true;
    in[0].held = SIM_BTN_Z;
    step_n(&s, in, 5);
    in[0].held = 0;
    CHECK(p1->friendly_hits >= 1);
    CHECK_EQ_INT(p2->friendly_hits, 0);

    sim_kill_player(&s, 1);
    CHECK_EQ_INT(p2->deaths, 1);
    CHECK_EQ_INT(p1->deaths, 0);
    enter_code(&s, in, 0, CALLDOWN_REINFORCE);
    step_n(&s, in, BEACON_FLIGHT_TICKS + calldown_defs[CALLDOWN_REINFORCE].delay + 2);
    CHECK_EQ_INT(p1->calldowns, 1);
    CHECK_EQ_INT(st->reinforcements, 1);

    player_stats_t sum = mission_squad_stats(st);
    CHECK_EQ_INT(sum.kills, 1);
    CHECK_EQ_INT(sum.deaths, 1);
    CHECK_EQ_INT(sum.calldowns, 1);
    CHECK_EQ_INT((int)sum.shots, (int)p1->shots);
}

// Kills go to whoever dealt the killing blow: bullets, a sentry's bullets
// (to the player who called it in), explosions; the emergency pod's are
// nobody's.
void test_kill_credit(void)
{
    sim_t s;
    setup_empty(&s);
    const mission_stats_t *st = &s.mission.stats;
    int e = sim_spawn_enemy(&s, ENEMY_SKITTER, v2(5.0f, 5.0f));
    refresh_world(&s);
    sim_explode(&s, v2(5.0f, 5.0f), 2.0f, 500.0f, BLAST_BOMB, 2);
    CHECK(!s.enemies[e].active);
    CHECK_EQ_INT(st->players[2].kills, 1);

    e = sim_spawn_enemy(&s, ENEMY_SKITTER, v2(-5.0f, 5.0f));
    refresh_world(&s);
    sim_explode(&s, v2(-5.0f, 5.0f), 2.0f, 500.0f, BLAST_IMPACT, SOURCE_NONE);
    CHECK(!s.enemies[e].active);
    CHECK_EQ_INT(st->kills[ENEMY_SKITTER], 2);
    CHECK_EQ_INT(mission_squad_stats(st).kills, 1);

    // Player 4's sentry, shooting a bug in front of it.
    s.turrets[1] = (turret_t){ .active = true, .owner = 3, .ammo = 50, .life = 1000, .target = -1,
                              .pos = v2(0.0f, -8.0f), .facing = v2(0.0f, 1.0f) };
    e = sim_spawn_enemy(&s, ENEMY_SKITTER, v2(0.0f, -4.0f));
    s.enemies[e].state = ESTATE_RECOVER;
    s.enemies[e].timer = 1000;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    for (int i = 0; i < SIM_HZ * 3 && s.enemies[e].active; i++)
        sim_step(&s, in);
    CHECK(!s.enemies[e].active);
    CHECK_EQ_INT(st->players[3].kills, 1);
    CHECK_EQ_INT(st->players[3].hits, 0); // hits count a player's own bullets
}
