#include "campaign.h"

#include <string.h>

#define ALL_OBJ ((1u << OBJ_ZONES) | (1u << OBJ_NESTS) | (1u << OBJ_UPLOAD))

// Original names. The map runs left to right, the hive world last.
const planet_def_t planet_defs[PLANET_COUNT] = {
    { "KAVEL PRIME",    BIOME_DUST,  0, 10, 55, { PLANET_NONE, PLANET_NONE }, ALL_OBJ },
    { "ORBRE IV",       BIOME_ASH,   1, 32, 25, { 0, PLANET_NONE },           ALL_OBJ },
    { "THESSA MINOR",   BIOME_FROST, 1, 34, 80, { 0, PLANET_NONE },           ALL_OBJ },
    { "MARDOR VII",     BIOME_DUST,  2, 58, 50, { 1, 2 },                     ALL_OBJ },
    { "QUENEX",         BIOME_FROST, 3, 76, 22, { 3, PLANET_NONE },           ALL_OBJ },
    { "THE GNASH HIVE", BIOME_ASH,   4, 90, 66, { 4, PLANET_NONE },           1u << OBJ_NESTS },
};

void campaign_reset(campaign_t *c)
{
    memset(c, 0, sizeof(*c));
    c->version = CAMPAIGN_VERSION;
}

void campaign_sanitize(campaign_t *c)
{
    if (c->version != CAMPAIGN_VERSION) {
        campaign_reset(c);
        return;
    }
    for (int i = 0; i < (int)sizeof(c->liberation); i++)
        if (c->liberation[i] > 100 || i >= PLANET_COUNT)
            c->liberation[i] = i >= PLANET_COUNT ? 0 : 100;
}

bool campaign_liberated(const campaign_t *c, int planet)
{
    return planet >= 0 && planet < PLANET_COUNT && c->liberation[planet] >= 100;
}

bool campaign_open(const campaign_t *c, int planet)
{
    if (planet < 0 || planet >= PLANET_COUNT)
        return false;
    const planet_def_t *p = &planet_defs[planet];
    if (p->needs[0] == PLANET_NONE && p->needs[1] == PLANET_NONE)
        return true;
    for (int k = 0; k < 2; k++)
        if (p->needs[k] != PLANET_NONE && campaign_liberated(c, p->needs[k]))
            return true;
    return false;
}

int campaign_gain(int difficulty, bool success, bool objective_done)
{
    if (difficulty < 0) difficulty = 0;
    if (difficulty >= DIFFICULTY_COUNT) difficulty = DIFFICULTY_COUNT - 1;
    if (success)
        return 20 + 5 * difficulty;
    return objective_done ? 5 : 0;
}

int campaign_record(campaign_t *c, int planet, int difficulty, bool success, bool objective_done,
                    bool *liberated_now)
{
    if (liberated_now)
        *liberated_now = false;
    if (planet < 0 || planet >= PLANET_COUNT || campaign_liberated(c, planet))
        return 0;
    int gain = campaign_gain(difficulty, success, objective_done);
    int now = c->liberation[planet] + gain;
    if (now >= 100) {
        gain -= now - 100;
        now = 100;
        if (liberated_now)
            *liberated_now = true;
    }
    c->liberation[planet] = (uint8_t)now;
    return gain;
}

bool campaign_complete(const campaign_t *c)
{
    for (int i = 0; i < PLANET_COUNT; i++)
        if (!campaign_liberated(c, i))
            return false;
    return true;
}

objective_type_t campaign_objective(int planet, uint32_t roll)
{
    uint8_t mask = planet >= 0 && planet < PLANET_COUNT ? planet_defs[planet].objectives : ALL_OBJ;
    int allowed[OBJ_COUNT], n = 0;
    for (int o = 0; o < OBJ_COUNT; o++)
        if (mask & (1u << o))
            allowed[n++] = o;
    return n ? (objective_type_t)allowed[roll % (uint32_t)n] : OBJ_ZONES;
}
