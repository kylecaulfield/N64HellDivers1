#ifndef SIM_CAMPAIGN_H
#define SIM_CAMPAIGN_H

// The campaign: a sector of planets held by the swarm. Each successful
// mission on a planet raises its liberation (more on harder levels); at
// 100% it is liberated, which opens the planets beyond it. Liberating the
// last one, the hive world, wins the campaign. Pure data and logic here;
// the N64 layer keeps the progress in EEPROM and draws the sector map.

#include <stdbool.h>
#include <stdint.h>

#include "sim.h"

#define PLANET_COUNT 6
#define PLANET_NONE  0xFF

typedef struct {
    const char *name;
    uint8_t biome;        // biome_t: how its ground and light look
    uint8_t threat;       // the level it is meant for (shown on the map)
    uint8_t x, y;         // position on the sector map, 0..100
    uint8_t needs[2];     // opens once either of these is liberated (PLANET_NONE: no need)
    uint8_t objectives;   // bit per objective_type_t its missions can have
} planet_def_t;

extern const planet_def_t planet_defs[PLANET_COUNT];

// Kept in EEPROM: 16 bytes, room to grow.
typedef struct {
    uint8_t liberation[8];  // percent per planet (0..100); [PLANET_COUNT..] unused
    uint8_t version;
    uint8_t pad[7];
} campaign_t;

#define CAMPAIGN_VERSION 1

void campaign_reset(campaign_t *c);
// Repair whatever came back from storage.
void campaign_sanitize(campaign_t *c);
bool campaign_liberated(const campaign_t *c, int planet);
// The squad can deploy there: the first planet always, the others once a
// planet they need is liberated.
bool campaign_open(const campaign_t *c, int planet);
// Liberation a mission earns: a success 20% plus 5% per level above RECON;
// a failure that finished the objective 5%; otherwise nothing.
int  campaign_gain(int difficulty, bool success, bool objective_done);
// Add a mission's gain to `planet`. Returns the gain (0 if it was already
// liberated); `liberated_now` says whether this mission finished it.
int  campaign_record(campaign_t *c, int planet, int difficulty, bool success, bool objective_done,
                     bool *liberated_now);
// Every planet liberated.
bool campaign_complete(const campaign_t *c);
// A random objective allowed on `planet`, from `roll`.
objective_type_t campaign_objective(int planet, uint32_t roll);

#endif
