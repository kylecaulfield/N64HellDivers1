#include "campaign.h"
#include "test.h"

#include <string.h>

void test_campaign_starts_with_the_first_planet_open(void)
{
    campaign_t c;
    campaign_reset(&c);
    CHECK(campaign_open(&c, 0));
    for (int i = 1; i < PLANET_COUNT; i++)
        CHECK(!campaign_open(&c, i));
    CHECK(!campaign_complete(&c));
    CHECK(!campaign_open(&c, -1));
    CHECK(!campaign_open(&c, PLANET_COUNT));
}

void test_campaign_gain_by_level_and_outcome(void)
{
    CHECK_EQ_INT(campaign_gain(0, true, true), 20);
    CHECK_EQ_INT(campaign_gain(DIFFICULTY_STANDARD, true, true), 25);
    CHECK_EQ_INT(campaign_gain(DIFFICULTY_COUNT - 1, true, true), 40);
    CHECK_EQ_INT(campaign_gain(DIFFICULTY_STANDARD, false, true), 5);
    CHECK_EQ_INT(campaign_gain(DIFFICULTY_STANDARD, false, false), 0);
}

void test_liberating_a_planet_opens_the_next(void)
{
    campaign_t c;
    campaign_reset(&c);
    bool now = true;
    // Four STANDARD successes: 25% each.
    for (int k = 0; k < 3; k++) {
        CHECK_EQ_INT(campaign_record(&c, 0, DIFFICULTY_STANDARD, true, true, &now), 25);
        CHECK(!now);
    }
    CHECK(!campaign_open(&c, 1));
    CHECK_EQ_INT(campaign_record(&c, 0, DIFFICULTY_STANDARD, true, true, &now), 25);
    CHECK(now);
    CHECK(campaign_liberated(&c, 0));
    CHECK(campaign_open(&c, 1) && campaign_open(&c, 2));
    CHECK(!campaign_open(&c, 3));
    // A liberated planet earns nothing more.
    CHECK_EQ_INT(campaign_record(&c, 0, DIFFICULTY_STANDARD, true, true, &now), 0);
    CHECK(!now);
    // Mardor needs either Orbre or Thessa.
    for (int k = 0; k < 5; k++)
        campaign_record(&c, 2, DIFFICULTY_STANDARD, true, true, NULL);
    CHECK(campaign_open(&c, 3));
    CHECK(!campaign_open(&c, 4));
}

void test_liberation_caps_at_100(void)
{
    campaign_t c;
    campaign_reset(&c);
    c.liberation[0] = 90;
    bool now = false;
    CHECK_EQ_INT(campaign_record(&c, 0, DIFFICULTY_COUNT - 1, true, true, &now), 10);
    CHECK(now);
    CHECK_EQ_INT(c.liberation[0], 100);
}

void test_campaign_complete_needs_every_planet(void)
{
    campaign_t c;
    campaign_reset(&c);
    for (int i = 0; i < PLANET_COUNT - 1; i++)
        c.liberation[i] = 100;
    CHECK(campaign_open(&c, PLANET_COUNT - 1));
    CHECK(!campaign_complete(&c));
    c.liberation[PLANET_COUNT - 1] = 100;
    CHECK(campaign_complete(&c));
}

void test_every_planet_is_reachable(void)
{
    // Liberating planets in map order opens each one before it is needed.
    campaign_t c;
    campaign_reset(&c);
    for (int i = 0; i < PLANET_COUNT; i++) {
        CHECK(campaign_open(&c, i));
        c.liberation[i] = 100;
    }
}

void test_campaign_sanitize_and_objectives(void)
{
    campaign_t c;
    memset(&c, 0xEE, sizeof(c));
    campaign_sanitize(&c); // wrong version: a fresh campaign
    CHECK_EQ_INT(c.version, CAMPAIGN_VERSION);
    CHECK_EQ_INT(c.liberation[0], 0);
    c.liberation[1] = 250;
    c.liberation[PLANET_COUNT] = 7;
    campaign_sanitize(&c);
    CHECK_EQ_INT(c.liberation[1], 100);
    CHECK_EQ_INT(c.liberation[PLANET_COUNT], 0);

    // The hive world is nests only; the others can be anything.
    for (uint32_t r = 0; r < 12; r++)
        CHECK_EQ_INT(campaign_objective(PLANET_COUNT - 1, r), OBJ_NESTS);
    bool seen[OBJ_COUNT] = { false };
    for (uint32_t r = 0; r < 12; r++)
        seen[campaign_objective(0, r)] = true;
    for (int o = 0; o < OBJ_COUNT; o++)
        CHECK(seen[o]);
}
