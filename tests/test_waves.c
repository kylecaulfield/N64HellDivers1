#include "test.h"
#include "test_util.h"
#include "waves.h"

void test_wave_composition(void)
{
    uint16_t c[ENEMY_TYPE_COUNT];
    wave_composition(1, DIFFICULTY_STANDARD, 3, c);
    CHECK_EQ_INT(c[ENEMY_SKITTER], 6);
    CHECK_EQ_INT(c[ENEMY_GNASHER], 3);
    CHECK_EQ_INT(c[ENEMY_BULWARK], 0);
    wave_composition(3, DIFFICULTY_STANDARD, 3, c);
    CHECK_EQ_INT(c[ENEMY_BULWARK], 1);
    wave_composition(7, DIFFICULTY_STANDARD, 3, c);
    CHECK_EQ_INT(c[ENEMY_SKITTER], 30);
    CHECK_EQ_INT(c[ENEMY_GNASHER], 15);
    CHECK_EQ_INT(c[ENEMY_BULWARK], 3);
    // Waves only grow.
    for (int w = 1; w < 20; w++) {
        uint16_t a[ENEMY_TYPE_COUNT], b[ENEMY_TYPE_COUNT];
        wave_composition(w, DIFFICULTY_STANDARD, 3, a);
        wave_composition(w + 1, DIFFICULTY_STANDARD, 3, b);
        for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
            CHECK(b[t] >= a[t]);
    }
}

static void count_types(const sim_t *s, int out[ENEMY_TYPE_COUNT])
{
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
        out[t] = 0;
    for (int i = 0; i < SIM_MAX_ENEMIES; i++)
        if (s->enemies[i].active)
            out[s->enemies[i].type]++;
}

void test_wave_one_spawns_its_composition(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.waves = true;
    s.settings.god_mode = true;
    waves_reset(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, WAVE_FIRST_DELAY + 2);
    CHECK_EQ_INT(s.waves.number, 1);
    CHECK_EQ_INT(s.waves.state, WAVE_ACTIVE);
    step_n(&s, in, WAVE_GROUP_INTERVAL * 3);
    int c[ENEMY_TYPE_COUNT];
    count_types(&s, c);
    uint16_t want[ENEMY_TYPE_COUNT];
    wave_composition(1, DIFFICULTY_STANDARD, 1, want); // one trooper
    CHECK_EQ_INT(c[ENEMY_SKITTER], want[ENEMY_SKITTER]);
    CHECK_EQ_INT(c[ENEMY_GNASHER], want[ENEMY_GNASHER]);
    CHECK_EQ_INT(c[ENEMY_BULWARK], 0);
    CHECK(want[ENEMY_SKITTER] < 6); // a smaller wave than the standard three's
    for (int i = 0; i < SIM_MAX_ENEMIES; i++) {
        const enemy_t *e = &s.enemies[i];
        if (!e->active)
            continue;
        CHECK(e->pos.x > s.arena.min.x && e->pos.x < s.arena.max.x);
        CHECK(e->pos.y > s.arena.min.y && e->pos.y < s.arena.max.y);
    }
}

void test_wave_waits_for_cap_and_loses_nothing(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.waves = true;
    s.settings.god_mode = true;
    s.settings.enemy_cap = 4;
    waves_reset(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, WAVE_FIRST_DELAY + 2 + WAVE_GROUP_INTERVAL * 4);
    int total = s.waves.total;
    CHECK(total > 4);
    CHECK_EQ_INT(s.counts.enemies, 4);
    CHECK_EQ_INT(s.waves.remaining[ENEMY_SKITTER] + s.waves.remaining[ENEMY_GNASHER], total - 4);
    // Make room: the rest of the wave arrives.
    s.settings.enemy_cap = 64;
    step_n(&s, in, WAVE_GROUP_INTERVAL * 3);
    CHECK_EQ_INT(s.counts.enemies, total);
    CHECK_EQ_INT(s.waves.remaining[ENEMY_SKITTER] + s.waves.remaining[ENEMY_GNASHER], 0);
}

void test_wave_advances_after_clearing(void)
{
    sim_t s;
    setup_empty(&s);
    s.settings.waves = true;
    s.settings.god_mode = true;
    waves_reset(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, WAVE_FIRST_DELAY + 2 + WAVE_GROUP_INTERVAL * 3);
    REQUIRE(s.waves.number == 1);
    sim_kill_all_enemies(&s);
    step_n(&s, in, 2);
    CHECK_EQ_INT(s.waves.state, WAVE_BREAK);
    step_n(&s, in, WAVE_BREAK_TICKS + 2);
    CHECK_EQ_INT(s.waves.number, 2);
}

void test_waves_off_spawns_nothing(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, SIM_HZ * 20);
    CHECK_EQ_INT(s.counts.enemies, 0);
}
