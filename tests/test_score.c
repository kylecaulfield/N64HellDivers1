#include <string.h>

#include "hiscore.h"
#include "mission.h"
#include "test.h"
#include "test_util.h"

void test_score_breakdown(void)
{
    sim_t s;
    setup_empty(&s);
    mission_t *m = &s.mission;
    m->stats.kills[ENEMY_SKITTER] = 10;
    m->stats.kills[ENEMY_GNASHER] = 4;
    m->stats.kills[ENEMY_BULWARK] = 1;
    m->zones[0].captured = true;
    m->stats.reinforcements = 2;
    mission_score_t sc = mission_score(&s);
    CHECK_EQ_INT(sc.kills, 10 * SCORE_KILL_SKITTER + 4 * SCORE_KILL_GNASHER + SCORE_KILL_BULWARK);
    CHECK_EQ_INT(sc.zones, SCORE_ZONE);
    CHECK_EQ_INT(sc.extraction, 0); // not extracted (yet)
    CHECK_EQ_INT(sc.time, 0);
    CHECK_EQ_INT(sc.reinforcements, -2 * SCORE_REINFORCEMENT);
    CHECK_EQ_INT(sc.total, sc.kills + sc.zones + sc.reinforcements);

    // A success: the dropship bonus, each trooper aboard, and the time bonus.
    m->zones[1].captured = true;
    m->phase = MISSION_SUCCESS;
    m->extracted = 0x5; // two troopers
    m->stats.ticks = SIM_HZ * (SCORE_PAR_SECONDS - 100);
    sc = mission_score(&s);
    CHECK_EQ_INT(sc.extraction, SCORE_EXTRACTED + 2 * SCORE_ABOARD);
    CHECK_EQ_INT(sc.time, 100 * SCORE_PER_SECOND);
    m->stats.ticks = SIM_HZ * (SCORE_PAR_SECONDS + 5);
    CHECK_EQ_INT(mission_score(&s).time, 0);

    // Never negative.
    memset(&m->stats.kills, 0, sizeof(m->stats.kills));
    m->phase = MISSION_FAILED;
    m->zones[0].captured = m->zones[1].captured = false;
    m->stats.reinforcements = 5;
    CHECK_EQ_INT(mission_score(&s).total, 0);
}

// Any debug aid during a mission keeps it off the table.
void test_assisted_missions(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, 5);
    CHECK(!s.mission.assisted);
    s.settings.god_mode = true;
    sim_step(&s, in);
    s.settings.god_mode = false;
    sim_step(&s, in);
    CHECK(s.mission.assisted); // sticks for the rest of the mission

    sim_restart(&s, 5);
    CHECK(!s.mission.assisted);
    mission_complete_objective(&s);
    CHECK(s.mission.assisted);
}

// The attract demo: four bots, nobody playing, and the leader works through
// the objective on its own.
void test_demo_plays_itself(void)
{
    sim_t s;
    sim_init(&s, 9);
    sim_demo(&s, 21);
    CHECK_EQ_INT(s.humans, 0);
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        CHECK(s.players[i].active && s.players[i].bot);
    s.settings.god_mode = true;
    s.settings.quick_mission = true;
    sim_input_t in[SIM_MAX_PLAYERS];
    memset(in, 0, sizeof(in));
    for (int t = 0; t < SIM_HZ * 120 && s.mission.phase < MISSION_EXTRACT; t++)
        sim_step(&s, in);
    CHECK_EQ_INT(s.mission.phase, MISSION_EXTRACT);
    // A real mission afterwards has player 1 back.
    sim_restart(&s, 22);
    CHECK_EQ_INT(s.humans, 1);
    CHECK(!s.players[0].bot);
}

void test_hiscore_table(void)
{
    hiscore_table_t t;
    hiscore_defaults(&t);
    for (int i = 1; i < HISCORE_COUNT; i++)
        CHECK(t.entries[i - 1].score > t.entries[i].score);
    uint32_t top = t.entries[0].score, last = t.entries[HISCORE_COUNT - 1].score;
    CHECK_EQ_INT(hiscore_rank(&t, top + 1), 0);
    CHECK_EQ_INT(hiscore_rank(&t, last), -1);       // a tie with the last doesn't make it
    CHECK_EQ_INT(hiscore_rank(&t, last + 1), HISCORE_COUNT - 1);
    CHECK_EQ_INT(hiscore_rank(&t, 0), -1);

    // A tie goes below the equal score; the rest shift down, the last drops.
    uint32_t third = t.entries[2].score;
    hiscore_entry_t e = { .score = third, .initials = { 'N', 'E', 'W' }, .waves = 3, .squad = 2 };
    CHECK_EQ_INT(hiscore_rank(&t, third), 3);
    uint32_t ninth = t.entries[HISCORE_COUNT - 2].score;
    CHECK_EQ_INT(hiscore_insert(&t, &e), 3);
    CHECK(memcmp(t.entries[3].initials, "NEW", 3) == 0);
    CHECK_EQ_INT((int)t.entries[2].score, (int)third);
    CHECK_EQ_INT((int)t.entries[HISCORE_COUNT - 1].score, (int)ninth);
    CHECK_EQ_INT(hiscore_insert(&t, &(hiscore_entry_t){ .score = 1 }), -1);
}

// Whatever comes back from storage is made usable.
void test_hiscore_sanitize(void)
{
    hiscore_table_t t;
    hiscore_defaults(&t);
    t.entries[7].score = 999999;
    t.entries[7].initials[1] = '\x07';
    hiscore_sanitize(&t);
    CHECK_EQ_INT((int)t.entries[0].score, 999999);
    CHECK_EQ_INT(t.entries[0].initials[1], ' ');
    for (int i = 1; i < HISCORE_COUNT; i++)
        CHECK(t.entries[i - 1].score >= t.entries[i].score);
}
