#include "calldown.h"
#include "campaign.h"
#include "mission.h"
#include "test.h"
#include "test_util.h"
#include "weapons.h"

// Steps until no beacon or strike is left (or `max` ticks pass).
static void settle(sim_t *s, sim_input_t in[SIM_MAX_PLAYERS], int max)
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

static int beacons_out(const sim_t *s)
{
    int n = 0;
    for (int i = 0; i < SIM_MAX_BEACONS; i++)
        n += s->beacons[i].active;
    return n;
}

static int mines_out(const sim_t *s)
{
    int n = 0;
    for (int i = 0; i < SIM_MAX_MINES; i++)
        n += s->mines[i].active;
    return n;
}

static void give_loadout(sim_t *s, int port, calldown_type_t a, calldown_type_t b, calldown_type_t c, calldown_type_t d)
{
    uint8_t *l = s->settings.loadout[port];
    l[0] = (uint8_t)a, l[1] = (uint8_t)b, l[2] = (uint8_t)c, l[3] = (uint8_t)d;
}

// --- Loadouts ------------------------------------------------------------------------

void test_only_the_loadout_can_be_called(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    // The default takes the machine gun, airstrike, sentry and barrage.
    for (int k = 0; k < LOADOUT_SLOTS; k++)
        CHECK(calldown_in_loadout(&s, 0, (calldown_type_t)LOADOUT_DEFAULT[k]));
    CHECK(!calldown_in_loadout(&s, 0, CALLDOWN_MINEFIELD));
    CHECK(calldown_in_loadout(&s, 0, CALLDOWN_REINFORCE));
    CHECK(calldown_in_loadout(&s, 0, CALLDOWN_RESUPPLY));
    CHECK(calldown_in_loadout(&s, 0, CALLDOWN_EXTRACTION));

    // Not brought: the code is entered but nothing is thrown.
    enter_code(&s, in, 0, CALLDOWN_NAPALM);
    CHECK_EQ_INT(beacons_out(&s), 0);
    CHECK_EQ_INT(count_events(&s, EV_CODE_REJECTED), 1);
    // Brought: thrown. Resupply always is.
    give_loadout(&s, 0, CALLDOWN_NAPALM, CALLDOWN_MINEFIELD, CALLDOWN_LAUNCHER, CALLDOWN_SENTRY);
    enter_code(&s, in, 0, CALLDOWN_NAPALM);
    CHECK_EQ_INT(beacons_out(&s), 1);
    enter_code(&s, in, 0, CALLDOWN_RESUPPLY);
    CHECK_EQ_INT(beacons_out(&s), 2);
    CHECK(!calldown_available(&s, 0, CALLDOWN_AIRSTRIKE));
    // Each port has its own.
    CHECK(calldown_in_loadout(&s, 1, CALLDOWN_AIRSTRIKE));
}

void test_campaign_unlocks_calldowns(void)
{
    campaign_t c;
    campaign_reset(&c);
    CHECK(campaign_unlocked(&c, CALLDOWN_MACHINE_GUN) && campaign_unlocked(&c, CALLDOWN_AIRSTRIKE));
    CHECK(campaign_unlocked(&c, CALLDOWN_BARRAGE) && campaign_unlocked(&c, CALLDOWN_SENTRY));
    CHECK(!campaign_unlocked(&c, CALLDOWN_MINEFIELD));
    CHECK(!campaign_unlocked(&c, CALLDOWN_LAUNCHER));
    CHECK(!campaign_unlocked(&c, CALLDOWN_NAPALM));
    // The ones everyone brings never go in a loadout.
    CHECK(!campaign_unlocked(&c, CALLDOWN_REINFORCE) && !campaign_unlocked(&c, CALLDOWN_RESUPPLY));
    CHECK(!campaign_unlocked(&c, CALLDOWN_EXTRACTION));
    c.liberation[campaign_reward_planet(CALLDOWN_MINEFIELD)] = 100;
    CHECK(campaign_unlocked(&c, CALLDOWN_MINEFIELD));
    CHECK(!campaign_unlocked(&c, CALLDOWN_NAPALM));
    // Every new calldown has a planet that unlocks it.
    CHECK(campaign_reward_planet(CALLDOWN_LAUNCHER) >= 0);
    CHECK(campaign_reward_planet(CALLDOWN_NAPALM) >= 0);
    CHECK_EQ_INT(campaign_reward_planet(CALLDOWN_SENTRY), -1);
}

// --- Minefield ---------------------------------------------------------------------------

void test_minefield_scatters_mines_that_bugs_set_off(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    give_loadout(&s, 0, CALLDOWN_MINEFIELD, CALLDOWN_AIRSTRIKE, CALLDOWN_SENTRY, CALLDOWN_BARRAGE);
    s.settings.god_mode = true;
    enter_code(&s, in, 0, CALLDOWN_MINEFIELD);
    settle(&s, in, 300);
    CHECK_EQ_INT(mines_out(&s), MINE_COUNT);
    // Find the beacon's spot from the mines: all within the spread of their centre.
    vec2 c = v2(0.0f, 0.0f);
    for (int i = 0; i < SIM_MAX_MINES; i++)
        if (s.mines[i].active)
            c = v2_add(c, v2_scale(s.mines[i].pos, 1.0f / MINE_COUNT));
    for (int i = 0; i < SIM_MAX_MINES; i++)
        if (s.mines[i].active)
            CHECK(v2_len(v2_sub(s.mines[i].pos, c)) <= 2.0f * MINE_SPREAD);

    // A trooper walks over one: nothing.
    const mine_t *m = NULL;
    for (int i = 0; i < SIM_MAX_MINES && !m; i++)
        if (s.mines[i].active)
            m = &s.mines[i];
    REQUIRE(m);
    vec2 at = m->pos;
    s.players[0].pos = at;
    step_n(&s, in, MINE_ARM_TICKS + 2);
    CHECK(m->active);
    // A bug does: it goes off and kills it.
    s.players[0].pos = v2_add(at, v2(8.0f, 0.0f));
    int bug = sim_spawn_enemy(&s, ENEMY_GNASHER, at);
    REQUIRE(bug >= 0);
    s.enemies[bug].pos = at;
    sim_clear_events(&s);
    sim_step(&s, in);
    CHECK(!m->active);
    CHECK(!s.enemies[bug].active);
    int mine_blasts = 0;
    for (int i = 0; i < s.event_count; i++)
        mine_blasts += s.events[i].type == EV_EXPLOSION && s.events[i].a == BLAST_MINE;
    CHECK(mine_blasts >= 1);
    CHECK(s.mission.stats.players[0].kills >= 1);
}

void test_mines_expire(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    s.mines[0] = (mine_t){ .active = true, .owner = 0, .life = 5, .pos = v2(10.0f, 10.0f) };
    step_n(&s, in, 6);
    CHECK(!s.mines[0].active);
}

// --- Grenade launcher ----------------------------------------------------------------------

void test_launcher_case_gives_the_explosive_support_weapon(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    give_loadout(&s, 0, CALLDOWN_LAUNCHER, CALLDOWN_AIRSTRIKE, CALLDOWN_SENTRY, CALLDOWN_BARRAGE);
    enter_code(&s, in, 0, CALLDOWN_LAUNCHER);
    settle(&s, in, 300);
    REQUIRE(s.pickups[0].active);
    CHECK_EQ_INT(s.pickups[0].type, PICKUP_LAUNCHER);
    s.players[0].pos = s.pickups[0].pos;
    sim_step(&s, in);
    const player_t *p = &s.players[0];
    CHECK_EQ_INT(p->weapons[WEAPON_SLOT_SUPPORT].type, WEAPON_LAUNCHER);
    CHECK_EQ_INT(p->weapon, WEAPON_SLOT_SUPPORT);
    CHECK_EQ_INT(p->weapons[WEAPON_SLOT_SUPPORT].mag, weapon_defs[WEAPON_LAUNCHER].mag_size);
}

void test_grenades_explode_on_what_they_hit(void)
{
    sim_t s;
    setup_empty(&s);
    // Three gnashers bunched 8 m east, a teammate next to them.
    int a = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(8.0f, 0.0f));
    int b = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(8.6f, 1.2f));
    int c = sim_spawn_enemy(&s, ENEMY_GNASHER, v2(8.6f, -1.2f));
    sim_spawn_player(&s, 1, v2(9.5f, 0.0f));
    s.players[1].state = PSTATE_NORMAL;
    refresh_world(&s);
    int pr = sim_spawn_projectile(&s, 0, WEAPON_LAUNCHER, v2(0.6f, 0.0f), v2(1.0f, 0.0f), 22.0f, 22.0f, 0.0f);
    REQUIRE(pr >= 0);
    for (int t = 0; t < 30 && s.projectiles[pr].active; t++)
        projectiles_update(&s);
    CHECK(!s.projectiles[pr].active);
    CHECK(!s.enemies[a].active && !s.enemies[b].active && !s.enemies[c].active);
    CHECK(s.players[1].hp < PLAYER_HP); // friendly fire
    int grenades = 0;
    for (int i = 0; i < s.event_count; i++)
        grenades += s.events[i].type == EV_EXPLOSION && s.events[i].a == BLAST_GRENADE;
    CHECK_EQ_INT(grenades, 1);
    CHECK_EQ_INT(s.mission.stats.players[0].hits, 1);
}

void test_a_grenade_with_nothing_to_hit_goes_off_at_its_range(void)
{
    sim_t s;
    setup_empty(&s);
    sim_clear_events(&s);
    int pr = sim_spawn_projectile(&s, 0, WEAPON_LAUNCHER, v2(0.0f, 0.0f), v2(0.0f, 1.0f), 22.0f, 10.0f, 0.0f);
    REQUIRE(pr >= 0);
    for (int t = 0; t < 30 && s.projectiles[pr].active; t++)
        projectiles_update(&s);
    CHECK(!s.projectiles[pr].active);
    REQUIRE(count_events(&s, EV_EXPLOSION) == 1);
    for (int i = 0; i < s.event_count; i++)
        if (s.events[i].type == EV_EXPLOSION)
            CHECK_NEAR(s.events[i].pos.y, 10.0f, 0.01);
}

void test_grenades_do_full_damage_to_nests(void)
{
    sim_t s;
    sim_init(&s, 1);
    s.settings.objective = OBJ_NESTS;
    sim_restart(&s, 77);
    s.mission.phase = MISSION_OBJECTIVE;
    s.players[0].state = PSTATE_NORMAL;
    s.arena.obstacle_count = 0; // nothing in the way
    vec2 n = s.arena.sites[0];
    vec2 from = v2_add(n, v2(-6.0f, 0.0f));
    float before = s.mission.nests[0].hp;
    int pr = sim_spawn_projectile(&s, 0, WEAPON_LAUNCHER, from, v2(1.0f, 0.0f), 22.0f, 22.0f, 0.0f);
    REQUIRE(pr >= 0);
    for (int t = 0; t < 30 && s.projectiles[pr].active; t++)
        projectiles_update(&s);
    CHECK_NEAR(before - s.mission.nests[0].hp, LAUNCHER_DAMAGE, 1.0); // the burst is on its edge: full damage
}

// --- Napalm ------------------------------------------------------------------------------------

void test_napalm_leaves_fires_that_burn_bugs_and_troopers(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    give_loadout(&s, 0, CALLDOWN_NAPALM, CALLDOWN_AIRSTRIKE, CALLDOWN_SENTRY, CALLDOWN_BARRAGE);
    s.players[0].facing = v2(1.0f, 0.0f);
    enter_code(&s, in, 0, CALLDOWN_NAPALM);
    settle(&s, in, 300);
    int fires = 0;
    for (int i = 0; i < SIM_MAX_FIRES; i++)
        fires += s.fires[i].active;
    CHECK_EQ_INT(fires, NAPALM_PATCHES);
    // The patches lie on a line along the throw (east): same y.
    float y = 0.0f;
    bool first = true;
    for (int i = 0; i < SIM_MAX_FIRES; i++) {
        if (!s.fires[i].active)
            continue;
        if (first)
            y = s.fires[i].pos.y, first = false;
        CHECK_NEAR(s.fires[i].pos.y, y, 0.01);
    }
    // A gnasher standing in a fire burns to death in a few seconds.
    const fire_t *f = &s.fires[0];
    REQUIRE(f->active);
    int bug = sim_spawn_enemy(&s, ENEMY_GNASHER, f->pos);
    REQUIRE(bug >= 0);
    s.players[0].pos = v2_add(f->pos, v2(0.0f, 10.0f)); // out of it
    int t = 0;
    for (; t < SIM_HZ * 3 && s.enemies[bug].active; t++) {
        s.enemies[bug].pos = f->pos; // keep it in the fire
        sim_step(&s, in);
    }
    CHECK(!s.enemies[bug].active);
    CHECK(t <= (int)(GNASHER_HP / FIRE_DPS * SIM_HZ) + FIRE_TICK + 1);
    // A trooper in it gets hurt too.
    float hp = s.players[0].hp;
    s.players[0].pos = f->pos;
    step_n(&s, in, FIRE_TICK + 1);
    CHECK(s.players[0].hp < hp);
    // And it burns out.
    step_n(&s, in, FIRE_LIFE);
    for (int i = 0; i < SIM_MAX_FIRES; i++)
        CHECK(!s.fires[i].active);
}

void test_loadouts_are_repaired_against_the_unlocks(void)
{
    campaign_t c;
    campaign_reset(&c);
    // Zeros (a blank save): Reinforce isn't pickable, so the defaults go in.
    uint8_t l[LOADOUT_SLOTS] = { 0, 0, 0, 0 };
    campaign_fix_loadout(&c, l);
    for (int k = 0; k < LOADOUT_SLOTS; k++)
        CHECK_EQ_INT(l[k], LOADOUT_DEFAULT[k]);
    // A locked pick and a repeat are replaced; the rest stay.
    uint8_t m[LOADOUT_SLOTS] = { CALLDOWN_NAPALM, CALLDOWN_SENTRY, CALLDOWN_SENTRY, CALLDOWN_AIRSTRIKE };
    campaign_fix_loadout(&c, m);
    CHECK_EQ_INT(m[1], CALLDOWN_SENTRY);
    CHECK_EQ_INT(m[3], CALLDOWN_AIRSTRIKE);
    for (int k = 0; k < LOADOUT_SLOTS; k++) {
        CHECK(campaign_unlocked(&c, (calldown_type_t)m[k]));
        for (int j = 0; j < k; j++)
            CHECK(m[j] != m[k]);
    }
    // Unlocked, it stays.
    c.liberation[campaign_reward_planet(CALLDOWN_NAPALM)] = 100;
    uint8_t n[LOADOUT_SLOTS] = { CALLDOWN_NAPALM, CALLDOWN_SENTRY, CALLDOWN_BARRAGE, CALLDOWN_AIRSTRIKE };
    campaign_fix_loadout(&c, n);
    CHECK_EQ_INT(n[0], CALLDOWN_NAPALM);
}

void test_cycling_a_slot_skips_locked_and_taken(void)
{
    campaign_t c;
    campaign_reset(&c);
    // Only the four basic ones are unlocked and all are taken: nothing to step to.
    uint8_t l[LOADOUT_SLOTS];
    for (int k = 0; k < LOADOUT_SLOTS; k++)
        l[k] = LOADOUT_DEFAULT[k];
    campaign_cycle_loadout(&c, l, 0, 1);
    CHECK_EQ_INT(l[0], LOADOUT_DEFAULT[0]);
    // Unlock everything: slot 0 steps through the free ones and back.
    for (int i = 0; i < PLANET_COUNT; i++)
        c.liberation[i] = 100;
    bool seen[CALLDOWN_COUNT] = { false };
    for (int n = 0; n < 8; n++) {
        campaign_cycle_loadout(&c, l, 0, 1);
        seen[l[0]] = true;
        for (int k = 1; k < LOADOUT_SLOTS; k++)
            CHECK(l[0] != l[k]);
        CHECK(campaign_unlocked(&c, (calldown_type_t)l[0]));
    }
    CHECK(seen[CALLDOWN_MINEFIELD] && seen[CALLDOWN_LAUNCHER] && seen[CALLDOWN_NAPALM]);
    CHECK(!seen[CALLDOWN_RESUPPLY] && !seen[CALLDOWN_REINFORCE] && !seen[CALLDOWN_EXTRACTION]);
    uint8_t before = l[0];
    campaign_cycle_loadout(&c, l, 0, 1);
    campaign_cycle_loadout(&c, l, 0, -1);
    CHECK_EQ_INT(l[0], before);
}

// Four players emptying their loadouts at once never run out of beacons.
void test_a_full_squad_can_call_everything_at_once(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.god_mode = true;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    for (int p = 1; p < SIM_MAX_PLAYERS; p++) {
        sim_spawn_player(&s, p, v2((float)p * 6.0f, 0.0f));
        s.players[p].state = PSTATE_NORMAL;
        in[p].connected = true;
    }
    sim_clear_events(&s);
    int thrown = 0;
    for (int p = 0; p < SIM_MAX_PLAYERS; p++) {
        for (int k = 0; k < LOADOUT_SLOTS; k++) {
            enter_code(&s, in, p, (calldown_type_t)s.settings.loadout[p][k]);
            thrown++;
        }
        enter_code(&s, in, p, CALLDOWN_RESUPPLY);
        thrown++;
    }
    CHECK_EQ_INT(count_events(&s, EV_CODE_REJECTED), 0);
    CHECK_EQ_INT(count_events(&s, EV_BEACON_THROWN), thrown);
}
