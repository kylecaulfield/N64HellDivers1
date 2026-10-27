#include "campaign.h"

#include <string.h>

#define ALL_OBJ ((1u << OBJ_ZONES) | (1u << OBJ_NESTS) | (1u << OBJ_UPLOAD))

// Original names. The map runs left to right, the hive world last.
const planet_def_t planet_defs[PLANET_COUNT] = {
    { "KAVEL PRIME",    BIOME_DUST,  0, 10, 55, { PLANET_NONE, PLANET_NONE }, ALL_OBJ, CALLDOWN_MINEFIELD },
    { "ORBRE IV",       BIOME_ASH,   1, 32, 25, { 0, PLANET_NONE },           ALL_OBJ, CALLDOWN_LAUNCHER },
    { "THESSA MINOR",   BIOME_FROST, 1, 34, 80, { 0, PLANET_NONE },           ALL_OBJ, CALLDOWN_NAPALM },
    { "MARDOR VII",     BIOME_DUST,  2, 58, 50, { 1, 2 },                     ALL_OBJ, CALLDOWN_COUNT },
    { "QUENEX",         BIOME_FROST, 3, 76, 22, { 3, PLANET_NONE },           ALL_OBJ, CALLDOWN_COUNT },
    { "THE GNASH HIVE", BIOME_ASH,   4, 90, 66, { 4, PLANET_NONE },           1u << OBJ_NESTS, CALLDOWN_COUNT },
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

int campaign_reward_planet(calldown_type_t type)
{
    for (int i = 0; i < PLANET_COUNT; i++)
        if (planet_defs[i].reward == type)
            return i;
    return -1;
}

bool campaign_unlocked(const campaign_t *c, calldown_type_t type)
{
    switch (type) {
    case CALLDOWN_MACHINE_GUN:
    case CALLDOWN_AIRSTRIKE:
    case CALLDOWN_BARRAGE:
    case CALLDOWN_SENTRY:
        return true;
    case CALLDOWN_REINFORCE:
    case CALLDOWN_RESUPPLY:
    case CALLDOWN_EXTRACTION:
    case CALLDOWN_COUNT:
        return false;
    default: {
        int p = campaign_reward_planet(type);
        return p >= 0 && campaign_liberated(c, p);
    }
    }
}

static bool in_other_slot(const uint8_t l[LOADOUT_SLOTS], int slot, int type)
{
    for (int k = 0; k < LOADOUT_SLOTS; k++)
        if (k != slot && l[k] == type)
            return true;
    return false;
}

void campaign_fix_loadout(const campaign_t *c, uint8_t l[LOADOUT_SLOTS])
{
    for (int k = 0; k < LOADOUT_SLOTS; k++) {
        bool repeat = false;
        for (int j = 0; j < k; j++)
            repeat |= l[j] == l[k];
        if (repeat || l[k] >= CALLDOWN_COUNT || !campaign_unlocked(c, (calldown_type_t)l[k]))
            l[k] = CALLDOWN_COUNT; // empty
    }
    for (int k = 0; k < LOADOUT_SLOTS; k++) {
        if (l[k] != CALLDOWN_COUNT)
            continue;
        // The defaults first, then anything else unlocked.
        for (int pass = 0; pass < 2 && l[k] == CALLDOWN_COUNT; pass++) {
            for (int i = 0; i < (pass ? CALLDOWN_COUNT : LOADOUT_SLOTS) && l[k] == CALLDOWN_COUNT; i++) {
                int t = pass ? i : LOADOUT_DEFAULT[i];
                if (campaign_unlocked(c, (calldown_type_t)t) && !in_other_slot(l, k, t))
                    l[k] = (uint8_t)t;
            }
        }
    }
}

void campaign_cycle_loadout(const campaign_t *c, uint8_t l[LOADOUT_SLOTS], int slot, int dir)
{
    int t = l[slot];
    for (int n = 0; n < CALLDOWN_COUNT; n++) {
        t = (t + (dir < 0 ? CALLDOWN_COUNT - 1 : 1)) % CALLDOWN_COUNT;
        if (campaign_unlocked(c, (calldown_type_t)t) && !in_other_slot(l, slot, t)) {
            l[slot] = (uint8_t)t;
            return;
        }
    }
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
