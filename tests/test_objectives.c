#include "difficulty.h"
#include "mission.h"
#include "test.h"
#include "test_util.h"
#include "waves.h"
#include "weapons.h"

// A mission with `objective` at `difficulty`, past the drop-in, with no
// rocks, no waves and no bugs: player 1 at the origin.
static void setup_objective(sim_t *s, objective_type_t objective, int difficulty)
{
    sim_init(s, 1);
    s->settings.objective = (uint8_t)objective;
    s->settings.difficulty = (uint8_t)difficulty;
    sim_restart(s, 4321);
    s->mission.phase = MISSION_OBJECTIVE;
    s->mission.timer = 0;
    s->players[0].state = PSTATE_NORMAL;
    s->players[0].state_timer = 0;
    s->players[0].pos = v2(0.0f, 0.0f);
    s->arena.obstacle_count = 0;
    flow_init(&s->flow, &s->arena);
    s->settings.waves = false;
    sim_kill_all_enemies(s);
    sim_clear_events(s);
}

// Hold R, press each input of `code` on its own tick, then release R.
static void enter_inputs(sim_t *s, sim_input_t in[SIM_MAX_PLAYERS], int port, const uint8_t *code, int len)
{
    for (int k = 0; k < len; k++) {
        in[port].held = SIM_BTN_R | CODE_BUTTON[code[k]];
        in[port].pressed = CODE_BUTTON[code[k]];
        sim_step(s, in);
        in[port].held = SIM_BTN_R;
        in[port].pressed = 0;
        sim_step(s, in);
    }
    in[port].held = 0;
    sim_step(s, in);
}

static int beacons_out(const sim_t *s)
{
    int n = 0;
    for (int i = 0; i < SIM_MAX_BEACONS; i++)
        n += s->beacons[i].active;
    return n;
}

// --- Nests ---------------------------------------------------------------------------

void test_nests_take_bullets_at_half_and_explosions_in_full(void)
{
    sim_t s;
    setup_objective(&s, OBJ_NESTS, DIFFICULTY_STANDARD);
    CHECK_EQ_INT(s.arena.site_count, difficulty_defs[DIFFICULTY_STANDARD].nests);
    vec2 c = s.arena.sites[0];
    CHECK_NEAR(s.mission.nests[0].hp, NEST_HP, 1e-3);

    // A rifle round from 6 m away.
    vec2 dir = v2_norm_or(v2_sub(v2(0.0f, 0.0f), c), v2(1.0f, 0.0f));
    s.players[0].pos = v2_add(c, v2_scale(dir, 6.0f));
    s.players[0].facing = v2_scale(dir, -1.0f);
    int p = sim_spawn_projectile(&s, 0, WEAPON_RIFLE, s.players[0].pos, s.players[0].facing, 60.0f, 30.0f, 20.0f);
    REQUIRE(p >= 0);
    for (int t = 0; t < 10 && s.projectiles[p].active; t++)
        projectiles_update(&s);
    CHECK_NEAR(s.mission.nests[0].hp, NEST_HP - 20.0f * NEST_BULLET_MULT, 1e-3);
    CHECK_EQ_INT(count_events(&s, EV_NEST_HIT), 1);
    CHECK_EQ_INT(s.mission.stats.players[0].hits, 1);

    // A bomb on top of it does its full damage (the centre is inside the nest).
    sim_explode(&s, c, AIRSTRIKE_RADIUS, AIRSTRIKE_DAMAGE, BLAST_BOMB, 0);
    CHECK_NEAR(s.mission.nests[0].hp, NEST_HP - 10.0f - AIRSTRIKE_DAMAGE, 1e-3);

    // Destroyed: one event, then it takes nothing and stops bullets no more.
    mission_damage_nest(&s, 0, 10000.0f, c);
    CHECK(s.mission.nests[0].destroyed);
    CHECK_EQ_INT(count_events(&s, EV_NEST_DESTROYED), 1);
    mission_damage_nest(&s, 0, 10.0f, c);
    CHECK_EQ_INT(count_events(&s, EV_NEST_DESTROYED), 1);
    p = sim_spawn_projectile(&s, 0, WEAPON_RIFLE, s.players[0].pos, s.players[0].facing, 60.0f, 30.0f, 20.0f);
    for (int t = 0; t < 3; t++)
        projectiles_update(&s);
    CHECK(s.projectiles[p].active || s.projectiles[p].range < 30.0f - 6.0f - NEST_RADIUS);
    CHECK_EQ_INT(s.mission.stats.players[0].hits, 1);
}

void test_nest_wakes_when_a_trooper_comes_near_and_spawns_groups(void)
{
    sim_t s;
    setup_objective(&s, OBJ_NESTS, DIFFICULTY_STANDARD);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    vec2 c = s.arena.sites[0];

    // Far away (the sites are 15+ m from the origin and each other, so stand
    // on the far side of the drop zone): nothing stirs.
    s.players[0].pos = v2_scale(v2_norm_or(c, v2(1.0f, 0.0f)), -8.0f);
    step_n(&s, in, NEST_SPAWN_TICKS * 2);
    CHECK_EQ_INT(s.counts.enemies, 0);
    CHECK(!s.mission.nests[0].awake);

    // Close: the first group after NEST_FIRST_TICKS, the next after the
    // interval. (The other nests are gone, so only this one answers.)
    for (int n = 1; n < s.arena.site_count; n++)
        s.mission.nests[n].destroyed = true;
    s.players[0].pos = v2_add(c, v2_scale(v2_norm_or(c, v2(1.0f, 0.0f)), -(NEST_WAKE_RADIUS - 2.0f)));
    s.settings.god_mode = true;
    step_n(&s, in, 1);
    CHECK(s.mission.nests[0].awake);
    step_n(&s, in, NEST_FIRST_TICKS);
    int group = difficulty_defs[DIFFICULTY_STANDARD].nest_group;
    CHECK_EQ_INT(s.counts.enemies, group);
    for (int n = 0; n < s.active_count; n++)
        CHECK(v2_len(v2_sub(s.enemies[s.active[n]].pos, c)) < NEST_RADIUS + 2.0f);
    sim_kill_all_enemies(&s);
    step_n(&s, in, NEST_SPAWN_TICKS);
    CHECK_EQ_INT(s.counts.enemies, group);

    // Destroyed: no more.
    sim_kill_all_enemies(&s);
    mission_damage_nest(&s, 0, 1e6f, c);
    step_n(&s, in, NEST_SPAWN_TICKS * 2);
    CHECK_EQ_INT(s.counts.enemies, 0);
}

void test_hurting_a_nest_from_afar_wakes_it(void)
{
    sim_t s;
    setup_objective(&s, OBJ_NESTS, DIFFICULTY_STANDARD);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    s.players[0].pos = v2_scale(v2_norm_or(s.arena.sites[1], v2(1.0f, 0.0f)), -8.0f);
    mission_damage_nest(&s, 1, 50.0f, s.arena.sites[1]);
    step_n(&s, in, 1);
    CHECK(s.mission.nests[1].awake);
    CHECK(!s.mission.nests[0].awake);
}

void test_destroying_every_nest_opens_extraction(void)
{
    sim_t s;
    setup_objective(&s, OBJ_NESTS, 3); // SEVERE: four nests
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    int total = 0;
    CHECK_EQ_INT(mission_parts(&s, &total), 0);
    CHECK_EQ_INT(total, 4);
    for (int n = 0; n < 3; n++)
        mission_damage_nest(&s, n, 1e6f, s.arena.sites[n]);
    step_n(&s, in, 2);
    CHECK_EQ_INT(s.mission.phase, MISSION_OBJECTIVE);
    CHECK_EQ_INT(mission_parts(&s, NULL), 3);
    mission_damage_nest(&s, 3, 1e6f, s.arena.sites[3]);
    step_n(&s, in, 1);
    CHECK_EQ_INT(s.mission.phase, MISSION_EXTRACT);
    CHECK_EQ_INT(mission_score(&s).objective, 4 * SCORE_NEST);
}

void test_nests_and_the_terminal_are_solid(void)
{
    sim_t s;
    setup_objective(&s, OBJ_NESTS, DIFFICULTY_STANDARD);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    vec2 c = s.arena.sites[0];
    s.players[0].pos = v2_add(c, v2(0.3f, 0.0f));
    step_n(&s, in, 1);
    CHECK(v2_len(v2_sub(s.players[0].pos, c)) >= NEST_RADIUS + PLAYER_RADIUS - 1e-3f);
    // A destroyed nest is a crater: walk right in.
    mission_damage_nest(&s, 0, 1e6f, c);
    s.players[0].pos = v2_add(c, v2(0.3f, 0.0f));
    step_n(&s, in, 1);
    CHECK(v2_len(v2_sub(s.players[0].pos, c)) < 0.5f);

    setup_objective(&s, OBJ_UPLOAD, DIFFICULTY_STANDARD);
    c = s.arena.sites[0];
    s.players[0].pos = v2_add(c, v2(0.0f, 0.2f));
    step_n(&s, in, 1);
    CHECK(v2_len(v2_sub(s.players[0].pos, c)) >= TERMINAL_RADIUS + PLAYER_RADIUS - 1e-3f);
}

void test_aim_assist_finds_nests(void)
{
    sim_t s;
    setup_objective(&s, OBJ_NESTS, DIFFICULTY_STANDARD);
    vec2 c = s.arena.sites[0];
    vec2 from = v2_add(c, v2(0.0f, -10.0f));
    // 20 degrees off with an 8-way aim: pulled onto the nest.
    vec2 aim = v2_rotate(v2(0.0f, 1.0f), 0.35f);
    vec2 got = aim_assist(&s, from, aim, SCHEME_B);
    CHECK(v2_dot(got, v2(0.0f, 1.0f)) > 0.999f);
    // Destroyed: nothing to pull towards.
    mission_damage_nest(&s, 0, 1e6f, c);
    got = aim_assist(&s, from, aim, SCHEME_B);
    CHECK(v2_dot(got, aim) > 0.999f);
}

// --- Upload --------------------------------------------------------------------------------

void test_terminal_codes_never_start_with_a_calldown(void)
{
    for (uint32_t seed = 1; seed < 200; seed++) {
        sim_t s;
        sim_init(&s, 1);
        s.settings.objective = OBJ_UPLOAD;
        sim_restart(&s, seed);
        const upload_t *u = &s.mission.upload;
        CHECK_EQ_INT(u->code_len, UPLOAD_CODE_LEN);
        for (int k = 0; k < u->code_len; k++)
            CHECK(u->code[k] >= CODE_UP && u->code[k] <= CODE_RIGHT);
        for (int len = 1; len <= u->code_len; len++)
            CHECK(calldown_match(u->code, len) < 0);
    }
}

void test_terminal_code_starts_the_upload_only_inside_the_ring(void)
{
    sim_t s;
    setup_objective(&s, OBJ_UPLOAD, DIFFICULTY_STANDARD);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    vec2 c = s.arena.sites[0];
    const upload_t *u = &s.mission.upload;

    // Outside the ring: the code means nothing.
    s.players[0].pos = v2_add(c, v2(0.0f, -(UPLOAD_RADIUS + 2.0f)));
    CHECK(!mission_terminal_open(&s, 0));
    enter_inputs(&s, in, 0, u->code, u->code_len);
    CHECK(!u->running);
    CHECK(count_events(&s, EV_CODE_REJECTED) >= 1);

    // Inside: it starts the stage, and throws nothing.
    sim_clear_events(&s);
    s.players[0].pos = v2_add(c, v2(0.0f, -(UPLOAD_RADIUS - 2.0f)));
    CHECK(mission_terminal_open(&s, 0));
    enter_inputs(&s, in, 0, u->code, u->code_len);
    CHECK(u->running);
    CHECK_EQ_INT(count_events(&s, EV_UPLOAD_START), 1);
    CHECK_EQ_INT(count_events(&s, EV_CODE_REJECTED), 0);
    CHECK_EQ_INT(beacons_out(&s), 0);
    CHECK(!mission_terminal_open(&s, 0)); // nothing to enter while it uploads

    // Calldowns still work in the ring.
    enter_code(&s, in, 0, CALLDOWN_RESUPPLY);
    CHECK_EQ_INT(beacons_out(&s), 1);
}

void test_upload_needs_someone_in_the_ring_and_runs_in_stages(void)
{
    sim_t s;
    setup_objective(&s, OBJ_UPLOAD, DIFFICULTY_STANDARD);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    vec2 c = s.arena.sites[0];
    upload_t *u = &s.mission.upload;
    vec2 inside = v2_add(c, v2(0.0f, -(UPLOAD_RADIUS - 2.0f)));
    vec2 outside = v2_add(c, v2(0.0f, -(UPLOAD_RADIUS + 2.0f)));
    const int stages = difficulty_defs[DIFFICULTY_STANDARD].stages;
    CHECK_EQ_INT(u->stages, stages);

    s.players[0].pos = inside;
    enter_inputs(&s, in, 0, u->code, u->code_len);
    REQUIRE(u->running);
    int half = mission_stage_ticks(&s) / 2;
    step_n(&s, in, half);
    int at = u->progress;
    CHECK(at >= half);
    // Nobody in the ring: it pauses (never drains).
    s.players[0].pos = outside;
    step_n(&s, in, 60);
    CHECK_EQ_INT(u->progress, at);
    s.players[0].pos = inside;
    sim_clear_events(&s);
    step_n(&s, in, mission_stage_ticks(&s));
    CHECK_EQ_INT(u->stage, 1);
    CHECK(!u->running);
    CHECK_EQ_INT(count_events(&s, EV_UPLOAD_STAGE), 1);
    CHECK_EQ_INT(mission_parts(&s, NULL), 1);

    // The rest: a fresh code each time, then extraction opens.
    for (int k = 1; k < stages; k++) {
        enter_inputs(&s, in, 0, u->code, u->code_len);
        REQUIRE(u->running);
        step_n(&s, in, mission_stage_ticks(&s));
    }
    CHECK_EQ_INT(u->stage, stages);
    CHECK_EQ_INT(s.mission.phase, MISSION_EXTRACT);
    CHECK_EQ_INT(mission_score(&s).objective, stages * SCORE_STAGE);
}

void test_waves_come_faster_while_uploading(void)
{
    sim_t s;
    setup_objective(&s, OBJ_UPLOAD, DIFFICULTY_STANDARD);
    s.settings.waves = true;
    s.waves.state = WAVE_ACTIVE;
    s.waves.remaining[ENEMY_SKITTER] = 30;
    s.waves.timer = 0;
    waves_update(&s);
    CHECK_EQ_INT(s.waves.timer, WAVE_GROUP_INTERVAL);
    s.mission.upload.running = true;
    s.waves.timer = 0;
    waves_update(&s);
    CHECK_EQ_INT(s.waves.timer, WAVE_GROUP_INTERVAL / UPLOAD_WAVE_SPEEDUP);
}

// --- Difficulty ------------------------------------------------------------------------------

void test_difficulty_scales_the_waves(void)
{
    for (int w = 1; w <= 12; w++) {
        int prev = -1;
        for (int d = 0; d < DIFFICULTY_COUNT; d++) {
            uint16_t c[ENEMY_TYPE_COUNT];
            wave_composition(w, d, 3, c);
            int total = c[0] + c[1] + c[2];
            CHECK(total > prev);
            prev = total;
        }
    }
    uint16_t easy[ENEMY_TYPE_COUNT], hard[ENEMY_TYPE_COUNT];
    wave_composition(1, 0, 3, easy);
    wave_composition(1, DIFFICULTY_COUNT - 1, 3, hard);
    CHECK_EQ_INT(easy[ENEMY_BULWARK], 0);
    CHECK(hard[ENEMY_BULWARK] >= 1); // the top level starts further along the ramp
}

void test_difficulty_sets_reinforcements_and_bug_damage(void)
{
    for (int d = 0; d < DIFFICULTY_COUNT; d++) {
        sim_t s;
        setup_objective(&s, OBJ_ZONES, d);
        CHECK_EQ_INT(s.reinforcements, difficulty_defs[d].reinforcements);
        CHECK_EQ_INT(s.arena.site_count, difficulty_defs[d].zones);

        // A gnasher's blow, at this level.
        sim_input_t in[SIM_MAX_PLAYERS];
        inputs_none(in);
        int g = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(1.0f, 0.0f));
        REQUIRE(g >= 0);
        float before = s.players[0].hp;
        for (int t = 0; t < SIM_HZ * 2 && s.players[0].hp == before; t++) {
            s.players[0].pos = v2(0.0f, 0.0f);
            sim_step(&s, in);
        }
        CHECK_NEAR(before - s.players[0].hp, GNASHER_DAMAGE * difficulty_defs[d].damage / 100.0f, 1e-3);
    }
    CHECK_EQ_INT(difficulty_defs[DIFFICULTY_STANDARD].reinforcements, REINFORCE_POOL);
}

void test_difficulty_multiplies_the_score(void)
{
    sim_t s;
    setup_objective(&s, OBJ_ZONES, DIFFICULTY_STANDARD);
    s.mission.stats.kills[ENEMY_GNASHER] = 20;
    s.mission.zones[0].captured = true;
    mission_score_t standard = mission_score(&s);
    CHECK_EQ_INT(standard.multiplier, 100);
    CHECK_EQ_INT(standard.total, 20 * SCORE_KILL_GNASHER + SCORE_ZONE);
    s.mission.difficulty = DIFFICULTY_COUNT - 1;
    mission_score_t top = mission_score(&s);
    CHECK_EQ_INT(top.multiplier, difficulty_defs[DIFFICULTY_COUNT - 1].score);
    CHECK_EQ_INT(top.total, standard.total * top.multiplier / 100);
}

// --- Bots ----------------------------------------------------------------------------------------

// The attract demo plays every objective through to the dropship by itself.
void test_demo_completes_every_objective(void)
{
    for (int obj = 0; obj < OBJ_COUNT; obj++) {
        sim_t s;
        sim_init(&s, 9);
        s.settings.objective = (uint8_t)obj;
        sim_demo(&s, 31 + (uint32_t)obj);
        s.settings.god_mode = true;
        s.settings.quick_mission = true;
        sim_input_t in[SIM_MAX_PLAYERS];
        memset(in, 0, sizeof(in));
        int t = 0;
        for (; t < SIM_HZ * 300 && !mission_over(&s); t++)
            sim_step(&s, in);
        CHECK_EQ_INT(s.mission.phase, MISSION_SUCCESS);
        CHECK(t < SIM_HZ * 120);
    }
}

void test_waves_scale_with_the_squad(void)
{
    for (int w = 1; w <= 10; w++) {
        int prev = -1;
        for (int n = 1; n <= SIM_MAX_PLAYERS; n++) {
            uint16_t c[ENEMY_TYPE_COUNT];
            wave_composition(w, DIFFICULTY_STANDARD, n, c);
            int total = c[0] + c[1] + c[2];
            CHECK(total > prev);
            prev = total;
        }
    }
}
