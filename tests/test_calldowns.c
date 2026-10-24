#include "calldown.h"
#include "test.h"
#include "test_util.h"
#include "weapons.h"

static const uint16_t CODE_BUTTON[] = {
    [CODE_UP] = SIM_BTN_CUP, [CODE_DOWN] = SIM_BTN_CDOWN,
    [CODE_LEFT] = SIM_BTN_CLEFT, [CODE_RIGHT] = SIM_BTN_CRIGHT,
};

// Hold R, press each input of `type`'s code on its own tick, then release R
// (the throw happens on the release tick).
static void enter_code(sim_t *s, sim_input_t in[SIM_MAX_PLAYERS], int port, calldown_type_t type)
{
    const calldown_def_t *d = &calldown_defs[type];
    for (int k = 0; k < d->code_len; k++) {
        in[port].held = SIM_BTN_R | CODE_BUTTON[d->code[k]];
        in[port].pressed = CODE_BUTTON[d->code[k]];
        sim_step(s, in);
        in[port].held = SIM_BTN_R;
        in[port].pressed = 0;
        sim_step(s, in);
    }
    in[port].held = 0;
    sim_step(s, in);
}

static const beacon_t *find_beacon(const sim_t *s, calldown_type_t type)
{
    for (int i = 0; i < SIM_MAX_BEACONS; i++)
        if (s->beacons[i].active && s->beacons[i].type == type)
            return &s->beacons[i];
    return NULL;
}

// Steps until no beacon or strike is left (or `max` ticks pass).
static void run_calldowns(sim_t *s, sim_input_t in[SIM_MAX_PLAYERS], int max)
{
    for (int t = 0; t < max; t++) {
        bool busy = false;
        for (int i = 0; i < SIM_MAX_BEACONS; i++)
            busy |= s->beacons[i].active;
        for (int i = 0; i < SIM_MAX_STRIKES; i++)
            busy |= s->strikes[i].active;
        if (!busy)
            return;
        sim_step(s, in);
    }
}

// Where a beacon thrown by `port` comes down in an open field.
static vec2 landing_point(const sim_t *s, int port)
{
    const player_t *p = &s->players[port];
    float dist = PLAYER_RADIUS + 0.2f + BEACON_THROW_SPEED * BEACON_FLIGHT_TICKS * SIM_DT;
    return v2_add(p->pos, v2_scale(p->facing, dist));
}

// --- Codes ---------------------------------------------------------------------------

void test_calldown_codes_are_valid_and_prefix_free(void)
{
    for (int a = 0; a < CALLDOWN_COUNT; a++) {
        const calldown_def_t *da = &calldown_defs[a];
        CHECK(da->code_len >= 3 && da->code_len <= CALLDOWN_CODE_MAX);
        CHECK(da->cooldown > 0 && da->delay > 0);
        for (int k = 0; k < da->code_len; k++)
            CHECK(da->code[k] >= CODE_UP && da->code[k] <= CODE_RIGHT);
        CHECK_EQ_INT(calldown_match(da->code, da->code_len), a);
        for (int b = 0; b < CALLDOWN_COUNT; b++) {
            const calldown_def_t *db = &calldown_defs[b];
            if (a == b || db->code_len < da->code_len)
                continue;
            int k = 0;
            while (k < da->code_len && da->code[k] == db->code[k])
                k++;
            CHECK(k < da->code_len); // a is not a prefix of b
        }
    }
}

void test_calldown_match_and_prefix(void)
{
    const calldown_def_t *d = &calldown_defs[CALLDOWN_AIRSTRIKE];
    CHECK_EQ_INT(calldown_match(d->code, d->code_len), CALLDOWN_AIRSTRIKE);
    CHECK_EQ_INT(calldown_match(d->code, d->code_len - 1), -1);
    CHECK(calldown_prefix(d->code, d->code_len - 1));
    CHECK(calldown_prefix(d->code, 0));
    uint8_t junk[CALLDOWN_CODE_MAX] = { CODE_UP, CODE_UP, CODE_UP, CODE_UP, CODE_UP, CODE_UP };
    CHECK(!calldown_prefix(junk, CALLDOWN_CODE_MAX));
    CHECK_EQ_INT(calldown_match(junk, 4), -1);
}

void test_controls_r_turns_c_buttons_into_code_inputs(void)
{
    sim_input_t in = { .connected = true, .held = SIM_BTN_R | SIM_BTN_CLEFT | SIM_BTN_Z, .pressed = SIM_BTN_CLEFT };
    uint16_t b = 0;
    player_intent_t a = controls_read(SCHEME_A, &in, &b);
    CHECK(a.code_mode);
    CHECK_EQ_INT(a.code_input, CODE_LEFT);
    CHECK(!a.fire);
    player_intent_t bb = controls_read(SCHEME_B, &in, &b);
    CHECK_EQ_INT(bb.code_input, CODE_LEFT);
    CHECK(!bb.fire);
    CHECK_NEAR(v2_len(bb.aim), 0.0f, 1e-6);
    // Without R the same press aims and fires (scheme B) and is no code input.
    in.held &= (uint16_t)~SIM_BTN_R;
    bb = controls_read(SCHEME_B, &in, &b);
    CHECK_EQ_INT(bb.code_input, CODE_NONE);
    CHECK(bb.fire);
}

// --- Entry and the throw ----------------------------------------------------------------

void test_code_entry_throws_a_beacon_on_release(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_AIRSTRIKE);
    const beacon_t *b = find_beacon(&s, CALLDOWN_AIRSTRIKE);
    REQUIRE(b != NULL);
    CHECK_EQ_INT(b->state, BEACON_FLYING);
    CHECK_EQ_INT(b->owner, 0);
    CHECK_EQ_INT(count_events(&s, EV_BEACON_THROWN), 1);
    CHECK_EQ_INT(count_events(&s, EV_CODE_INPUT), calldown_defs[CALLDOWN_AIRSTRIKE].code_len);
    CHECK(s.cooldowns[0][CALLDOWN_AIRSTRIKE] > 0);
    CHECK_EQ_INT(s.players[0].code.len, 0);
    CHECK(!s.players[0].code.entering);
}

void test_wrong_input_starts_the_code_over(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    // Airstrike starts with RIGHT; UP, RIGHT is no code's start.
    in[0].held = SIM_BTN_R | SIM_BTN_CUP;
    in[0].pressed = SIM_BTN_CUP;
    sim_step(&s, in); // UP starts the sentry code: fine
    CHECK_EQ_INT(s.players[0].code.len, 1);
    in[0].held = SIM_BTN_R | SIM_BTN_CUP;
    in[0].pressed = SIM_BTN_CUP;
    sim_step(&s, in); // UP, UP: no code
    CHECK_EQ_INT(s.players[0].code.len, 0);
    CHECK_EQ_INT(count_events(&s, EV_CODE_REJECTED), 1);
    // Still holding R: a correct code from scratch works.
    enter_code(&s, in, 0, CALLDOWN_AIRSTRIKE);
    CHECK(find_beacon(&s, CALLDOWN_AIRSTRIKE) != NULL);
}

void test_releasing_on_a_partial_code_throws_nothing(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    const calldown_def_t *d = &calldown_defs[CALLDOWN_BARRAGE];
    for (int k = 0; k < 3; k++) {
        in[0].held = SIM_BTN_R;
        in[0].pressed = CODE_BUTTON[d->code[k]];
        sim_step(&s, in);
    }
    in[0].held = in[0].pressed = 0;
    sim_step(&s, in);
    CHECK_EQ_INT(s.counts.calldowns, 0);
    CHECK_EQ_INT(count_events(&s, EV_CODE_REJECTED), 1);
    CHECK_EQ_INT(s.cooldowns[0][CALLDOWN_BARRAGE], 0);
}

void test_no_firing_while_holding_r(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_R | SIM_BTN_Z;
    step_n(&s, in, 10);
    CHECK_EQ_INT(count_projectiles(&s), 0);
    in[0].held = SIM_BTN_Z;
    step_n(&s, in, 2);
    CHECK(count_projectiles(&s) > 0);
}

void test_cooldown_blocks_a_second_throw(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_SENTRY);
    sim_clear_events(&s);
    enter_code(&s, in, 0, CALLDOWN_SENTRY);
    CHECK_EQ_INT(count_events(&s, EV_BEACON_THROWN), 0);
    CHECK_EQ_INT(count_events(&s, EV_CODE_REJECTED), 1);

    // The debug switch skips cooldowns.
    s.settings.free_calldowns = true;
    s.cooldowns[0][CALLDOWN_SENTRY] = 0;
    enter_code(&s, in, 0, CALLDOWN_SENTRY);
    enter_code(&s, in, 0, CALLDOWN_SENTRY);
    CHECK_EQ_INT(count_events(&s, EV_BEACON_THROWN), 2);
}

void test_cooldown_runs_out(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_AIRSTRIKE);
    step_n(&s, in, calldown_defs[CALLDOWN_AIRSTRIKE].cooldown);
    CHECK(calldown_available(&s, 0, CALLDOWN_AIRSTRIKE));
}

// --- Beacons ------------------------------------------------------------------------------

void test_beacon_lands_ahead_then_waits_the_delay(void)
{
    sim_t s;
    setup_empty(&s);
    s.players[0].facing = v2(1.0f, 0.0f);
    vec2 expect = landing_point(&s, 0);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_RESUPPLY);
    step_n(&s, in, BEACON_FLIGHT_TICKS);
    const beacon_t *b = find_beacon(&s, CALLDOWN_RESUPPLY);
    REQUIRE(b != NULL);
    CHECK_EQ_INT(b->state, BEACON_ARMED);
    CHECK_NEAR(b->pos.x, expect.x, 0.5);
    CHECK_NEAR(b->pos.y, expect.y, 0.5);
    step_n(&s, in, calldown_defs[CALLDOWN_RESUPPLY].delay - 2);
    CHECK(find_beacon(&s, CALLDOWN_RESUPPLY) != NULL);
    step_n(&s, in, 2);
    CHECK(find_beacon(&s, CALLDOWN_RESUPPLY) == NULL);
    CHECK_EQ_INT(count_events(&s, EV_CALLDOWN), 1);
}

void test_beacon_stops_at_a_rock(void)
{
    sim_t s;
    setup_empty(&s);
    s.arena.obstacles[0] = (obstacle_t){ .pos = v2(0.0f, 3.5f), .radius = 1.0f };
    s.arena.obstacle_count = 1;
    flow_init(&s.flow, &s.arena);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_RESUPPLY); // facing north, into the rock
    step_n(&s, in, BEACON_FLIGHT_TICKS);
    const beacon_t *b = find_beacon(&s, CALLDOWN_RESUPPLY);
    REQUIRE(b != NULL);
    CHECK_EQ_INT(b->state, BEACON_ARMED);
    CHECK(b->pos.y < 2.5f + 0.01f); // in front of the rock, not past it
}

// --- What arrives ----------------------------------------------------------------------------

void test_airstrike_bombs_a_line_through_bugs_and_teammates(void)
{
    sim_t s;
    setup_empty(&s);
    s.players[0].facing = v2(1.0f, 0.0f);
    vec2 c = landing_point(&s, 0);
    int bugs[AIRSTRIKE_BOMBS];
    for (int k = 0; k < AIRSTRIKE_BOMBS; k++) {
        float along = ((float)k - (AIRSTRIKE_BOMBS - 1) * 0.5f) * AIRSTRIKE_SPACING;
        bugs[k] = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(c.x + along, c.y + 5.0f)); // off the line
    }
    // Bugs stand still for the test: moved onto the line just before impact.
    sim_spawn_player(&s, 1, v2(c.x + AIRSTRIKE_SPACING, c.y));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[1].connected = true;
    enter_code(&s, in, 0, CALLDOWN_AIRSTRIKE);
    // (The throw tick already moves the beacon, and the first bomb falls on
    // the arrival tick: stop a few ticks short.)
    step_n(&s, in, BEACON_FLIGHT_TICKS + calldown_defs[CALLDOWN_AIRSTRIKE].delay - 3);
    for (int k = 0; k < AIRSTRIKE_BOMBS; k++) {
        float along = ((float)k - (AIRSTRIKE_BOMBS - 1) * 0.5f) * AIRSTRIKE_SPACING;
        s.enemies[bugs[k]].pos = v2(c.x + along, c.y);
    }
    s.players[1].pos = v2(c.x + AIRSTRIKE_SPACING, c.y);
    refresh_world(&s);
    sim_clear_events(&s);
    run_calldowns(&s, in, 60);
    int bombs = 0;
    for (int i = 0; i < s.event_count; i++)
        bombs += s.events[i].type == EV_EXPLOSION && s.events[i].a == BLAST_BOMB;
    CHECK_EQ_INT(bombs, AIRSTRIKE_BOMBS);
    for (int k = 0; k < AIRSTRIKE_BOMBS; k++)
        CHECK(!s.enemies[bugs[k]].active);
    CHECK(!sim_player_alive(&s.players[1])); // friendly fire
}

void test_barrage_drops_every_shell_inside_its_area(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    s.settings.god_mode = true;
    enter_code(&s, in, 0, CALLDOWN_BARRAGE);
    step_n(&s, in, BEACON_FLIGHT_TICKS);
    vec2 c = find_beacon(&s, CALLDOWN_BARRAGE)->pos;
    sim_clear_events(&s);
    int shells = 0, outside = 0;
    for (int t = 0; t < calldown_defs[CALLDOWN_BARRAGE].delay + BARRAGE_SHELLS * BARRAGE_INTERVAL + 5; t++) {
        sim_step(&s, in);
        for (int i = 0; i < s.event_count; i++) {
            if (s.events[i].type != EV_EXPLOSION || s.events[i].a != BLAST_SHELL)
                continue;
            shells++;
            outside += v2_len(v2_sub(s.events[i].pos, c)) > BARRAGE_AREA + 1e-3f;
        }
        sim_clear_events(&s);
    }
    CHECK_EQ_INT(shells, BARRAGE_SHELLS);
    CHECK_EQ_INT(outside, 0);
    CHECK_EQ_INT(s.counts.calldowns, 0);
}

void test_resupply_crate_refills_ammo(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_RESUPPLY);
    run_calldowns(&s, in, 200);
    const pickup_t *k = &s.pickups[0];
    REQUIRE(k->active);
    CHECK_EQ_INT(k->type, PICKUP_RESUPPLY);
    // Full ammo: walking over it takes nothing.
    s.players[0].pos = k->pos;
    sim_step(&s, in);
    CHECK_EQ_INT(k->uses, RESUPPLY_USES);
    s.players[0].weapons[0].mag = 3;
    s.players[0].weapons[0].reserve = 0;
    sim_step(&s, in);
    CHECK_EQ_INT(s.players[0].weapons[0].mag, weapon_defs[WEAPON_RIFLE].mag_size);
    CHECK_EQ_INT(s.players[0].weapons[0].reserve, weapon_defs[WEAPON_RIFLE].start_reserve);
    CHECK_EQ_INT(k->uses, RESUPPLY_USES - 1);
    CHECK_EQ_INT(count_events(&s, EV_PICKUP), 1);
}

void test_machine_gun_case_gives_the_support_weapon(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    CHECK(!s.players[0].weapons[WEAPON_SLOT_SUPPORT].owned);
    enter_code(&s, in, 0, CALLDOWN_MACHINE_GUN);
    run_calldowns(&s, in, 200);
    REQUIRE(s.pickups[0].active);
    s.players[0].pos = s.pickups[0].pos;
    sim_step(&s, in);
    CHECK(!s.pickups[0].active);
    const player_t *p = &s.players[0];
    CHECK(p->weapons[WEAPON_SLOT_SUPPORT].owned);
    CHECK_EQ_INT(p->weapons[WEAPON_SLOT_SUPPORT].type, WEAPON_MACHINE_GUN);
    CHECK_EQ_INT(p->weapon, WEAPON_SLOT_SUPPORT);
}

void test_weapon_switch_cycles_through_owned_slots_only(void)
{
    sim_t s;
    setup_empty(&s);
    player_t *p = &s.players[0];
    weapon_update(&s, 0, false, false, true, p->facing);
    CHECK_EQ_INT(p->weapon, 1);
    weapon_update(&s, 0, false, false, true, p->facing);
    CHECK_EQ_INT(p->weapon, 0); // no support weapon: skipped
    p->weapons[WEAPON_SLOT_SUPPORT] = weapon_slot_full(WEAPON_MACHINE_GUN);
    weapon_update(&s, 0, false, false, true, p->facing);
    weapon_update(&s, 0, false, false, true, p->facing);
    CHECK_EQ_INT(p->weapon, WEAPON_SLOT_SUPPORT);
}

void test_sentry_shoots_the_nearest_bug(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_SENTRY);
    run_calldowns(&s, in, 200);
    const turret_t *t = &s.turrets[0];
    REQUIRE(t->active);
    int e = sim_spawn_enemy(&s, ENEMY_GNASHER, v2_add(t->pos, v2(8.0f, 0.0f)));
    s.enemies[e].state = ESTATE_RECOVER; // keep it in place
    s.enemies[e].timer = 1000;
    step_n(&s, in, SIM_HZ * 2);
    CHECK(!s.enemies[e].active);
    CHECK(t->ammo < SENTRY_AMMO);
}

void test_sentry_bullets_hit_players_in_the_way(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_SENTRY);
    run_calldowns(&s, in, 200);
    const turret_t *t = &s.turrets[0];
    REQUIRE(t->active);
    int e = sim_spawn_enemy(&s, ENEMY_BULWARK, v2_add(t->pos, v2(10.0f, 0.0f)));
    s.enemies[e].state = ESTATE_RECOVER;
    s.enemies[e].timer = 1000;
    s.players[0].pos = v2_add(t->pos, v2(4.0f, 0.0f)); // between turret and bug
    for (int k = 0; k < SIM_HZ; k++) {
        s.players[0].pos = v2_add(t->pos, v2(4.0f, 0.0f));
        sim_step(&s, in);
    }
    CHECK(s.players[0].hp < PLAYER_HP);
}

void test_turret_runs_out(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_SENTRY);
    run_calldowns(&s, in, 200);
    REQUIRE(s.turrets[0].active);
    step_n(&s, in, SENTRY_LIFE);
    CHECK(!s.turrets[0].active);
}

void test_pickup_expires(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_RESUPPLY);
    run_calldowns(&s, in, 200);
    REQUIRE(s.pickups[0].active);
    step_n(&s, in, RESUPPLY_LIFE);
    CHECK(!s.pickups[0].active);
}

void test_drops_crush_what_they_land_on(void)
{
    sim_t s;
    setup_empty(&s);
    vec2 at = landing_point(&s, 0);
    int e = sim_spawn_enemy(&s, ENEMY_BULWARK, at);
    sim_spawn_player(&s, 1, v2(at.x + 0.5f, at.y));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[1].connected = true;
    enter_code(&s, in, 0, CALLDOWN_RESUPPLY);
    for (int t = 0; t < BEACON_FLIGHT_TICKS + calldown_defs[CALLDOWN_RESUPPLY].delay + 2; t++) {
        // Pin both in place under the drop.
        s.enemies[e].pos = at;
        s.enemies[e].state = ESTATE_RECOVER;
        s.enemies[e].timer = 100;
        if (sim_player_alive(&s.players[1]))
            s.players[1].pos = v2(at.x + 0.5f, at.y);
        sim_step(&s, in);
    }
    CHECK(!s.enemies[e].active);
    CHECK(!sim_player_alive(&s.players[1]));
    CHECK(sim_player_alive(&s.players[0]));
}

// --- Friendly fire and explosions --------------------------------------------------------------

void test_friendly_fire_bullets_hit_teammates_not_the_shooter(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(0.0f, 5.0f)); // in front of player 1
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[1].connected = true;
    in[0].held = SIM_BTN_Z; // scheme A: fire north
    step_n(&s, in, 5);
    CHECK(s.players[1].hp < PLAYER_HP);
    CHECK_NEAR(s.players[0].hp, PLAYER_HP, 1e-6);
}

void test_explosion_damage_falls_off_and_ignores_armor(void)
{
    sim_t s;
    setup_empty(&s);
    int near = sim_spawn_enemy(&s, ENEMY_BULWARK, v2(10.0f, 0.0f));
    int far = sim_spawn_enemy(&s, ENEMY_BULWARK, v2(10.0f + BULWARK_RADIUS + 2.0f, 0.0f));
    s.enemies[near].facing = s.enemies[far].facing = v2(-1.0f, 0.0f); // fronts towards the blast
    refresh_world(&s);
    sim_explode(&s, v2(10.0f, 0.0f), 2.5f, 100.0f, BLAST_SHELL);
    float near_loss = BULWARK_HP - s.enemies[near].hp;
    float far_loss = BULWARK_HP - s.enemies[far].hp;
    CHECK_NEAR(near_loss, 100.0f, 1e-3);           // full damage, no armor reduction
    CHECK(far_loss > 100.0f * EXPLOSION_EDGE - 1e-3f);
    CHECK(far_loss < near_loss);
    CHECK_EQ_INT(count_events(&s, EV_EXPLOSION), 1);
}

void test_explosions_hurt_players(void)
{
    sim_t s;
    setup_empty(&s);
    sim_explode(&s, v2(1.0f, 0.0f), 2.0f, 40.0f, BLAST_BOMB);
    CHECK(s.players[0].hp < PLAYER_HP);
    s.settings.god_mode = true;
    float hp = s.players[0].hp;
    sim_explode(&s, v2(1.0f, 0.0f), 2.0f, 40.0f, BLAST_BOMB);
    CHECK_NEAR(s.players[0].hp, hp, 1e-6);
}

// --- Death and reinforcements -------------------------------------------------------------------

void test_reinforce_brings_back_every_dead_teammate(void)
{
    sim_t s;
    setup_empty(&s);
    for (int i = 1; i < SIM_MAX_PLAYERS; i++)
        sim_spawn_player(&s, i, v2((float)i * 2.0f, -3.0f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    for (int i = 1; i < SIM_MAX_PLAYERS; i++)
        in[i].connected = true;
    player_damage(&s, 2, PLAYER_HP);
    player_damage(&s, 3, PLAYER_HP);
    CHECK(calldown_available(&s, 0, CALLDOWN_REINFORCE));
    enter_code(&s, in, 0, CALLDOWN_REINFORCE);
    vec2 drop = landing_point(&s, 0);
    run_calldowns(&s, in, 200);
    CHECK(sim_player_alive(&s.players[2]));
    CHECK(sim_player_alive(&s.players[3]));
    CHECK(v2_len(v2_sub(s.players[2].pos, drop)) < 3.0f);
    CHECK_EQ_INT(s.reinforcements, REINFORCE_POOL - 2);
    CHECK_EQ_INT(count_events(&s, EV_REINFORCED), 2);
}

void test_reinforce_needs_someone_dead_and_the_pool(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(2.0f, 0.0f));
    CHECK(!calldown_available(&s, 0, CALLDOWN_REINFORCE)); // nobody down
    player_damage(&s, 1, PLAYER_HP);
    CHECK(calldown_available(&s, 0, CALLDOWN_REINFORCE));
    s.reinforcements = 0;
    CHECK(!calldown_available(&s, 0, CALLDOWN_REINFORCE));

    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_REINFORCE);
    CHECK(find_beacon(&s, CALLDOWN_REINFORCE) == NULL);
    CHECK_EQ_INT(count_events(&s, EV_CODE_REJECTED), 1);
}

void test_only_one_reinforce_beacon_at_a_time(void)
{
    sim_t s;
    setup_empty(&s);
    sim_spawn_player(&s, 1, v2(2.0f, 0.0f));
    sim_spawn_player(&s, 2, v2(-2.0f, 0.0f));
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[1].connected = in[2].connected = true;
    player_damage(&s, 2, PLAYER_HP);
    enter_code(&s, in, 0, CALLDOWN_REINFORCE);
    CHECK(!calldown_available(&s, 1, CALLDOWN_REINFORCE));
}

void test_whole_squad_down_gets_an_emergency_reinforce(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    player_damage(&s, 0, PLAYER_HP);
    sim_step(&s, in);
    CHECK_EQ_INT(s.squad, SQUAD_DOWN);
    step_n(&s, in, SQUAD_REINFORCE_TICKS - 2);
    CHECK(!sim_player_alive(&s.players[0]));
    step_n(&s, in, 2);
    CHECK(sim_player_alive(&s.players[0]));
    CHECK_EQ_INT(s.squad, SQUAD_FIGHTING);
    CHECK_EQ_INT(s.reinforcements, REINFORCE_POOL - 1);
}

void test_squad_lost_without_reinforcements(void)
{
    sim_t s;
    setup_empty(&s);
    s.reinforcements = 0;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    player_damage(&s, 0, PLAYER_HP);
    sim_step(&s, in);
    CHECK_EQ_INT(s.squad, SQUAD_LOST);
    step_n(&s, in, SQUAD_LOST_TICKS + 10);
    CHECK_EQ_INT(s.squad, SQUAD_LOST);
    CHECK_EQ_INT(s.squad_timer, 0); // the N64 layer restarts the mission now
    CHECK(!sim_player_alive(&s.players[0]));
}

void test_cooldowns_survive_death(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    enter_code(&s, in, 0, CALLDOWN_AIRSTRIKE);
    player_damage(&s, 0, PLAYER_HP);
    step_n(&s, in, SQUAD_REINFORCE_TICKS + 1);
    REQUIRE(sim_player_alive(&s.players[0]));
    CHECK(!calldown_available(&s, 0, CALLDOWN_AIRSTRIKE));
}

void test_restart_keeps_settings_and_bots(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.god_mode = true;
    s.settings.enemy_cap = 40;
    s.settings.scheme[0] = SCHEME_B;
    sim_set_bots(&s, true);
    s.reinforcements = 0;
    player_damage(&s, 0, PLAYER_HP);
    sim_restart(&s, 77);
    CHECK(s.settings.god_mode);
    CHECK_EQ_INT(s.settings.enemy_cap, 40);
    CHECK_EQ_INT(s.settings.scheme[0], SCHEME_B);
    CHECK(s.players[1].bot && sim_player_alive(&s.players[1]));
    CHECK(sim_player_alive(&s.players[0]));
    CHECK_EQ_INT(s.reinforcements, REINFORCE_POOL);
    CHECK_EQ_INT(s.squad, SQUAD_FIGHTING);
}
