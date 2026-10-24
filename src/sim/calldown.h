#ifndef SIM_CALLDOWN_H
#define SIM_CALLDOWN_H

// Calldowns: hold R and enter a directional code on the C-buttons, then
// release R on a complete code to throw a beacon in your facing direction.
// After the call-in delay the calldown arrives at the beacon:
//
//   Reinforce        drop pod with every dead teammate, one shared
//                    reinforcement each
//   Resupply         crate that refills ammo, RESUPPLY_USES times
//   Machine gun      case with the support weapon
//   Airstrike        bombs in a line along the throw direction
//   Orbital barrage  shells at random inside an area, over several seconds
//   Sentry turret    shoots the nearest bug; its bullets hit players too
//   Extraction       once the objective is done, thrown from inside the LZ:
//                    starts the hold until the dropship lands (mission.h)
//
// Whatever drops from orbit crushes what it lands on. Friendly fire is on
// for everything.

#include "controls.h"
#include "sim.h"

typedef struct {
    const char *name;
    uint8_t  code[CALLDOWN_CODE_MAX];  // code_input_t
    uint8_t  code_len;
    uint16_t cooldown;  // ticks, counted from the throw
    uint16_t delay;     // call-in ticks, counted from the beacon landing
} calldown_def_t;

extern const calldown_def_t calldown_defs[CALLDOWN_COUNT];

// The calldown whose code is exactly `inputs`, or -1.
int  calldown_match(const uint8_t *inputs, int len);
// Does any calldown's code start with `inputs`?
bool calldown_prefix(const uint8_t *inputs, int len);
// Can player `idx` throw `type` now? Its cooldown must be done; Reinforce
// also needs a reinforcement left, someone dead and no Reinforce already out.
bool calldown_available(const sim_t *s, int idx, calldown_type_t type);

// Code entry and the throw, once per tick for each living player.
// `can_act` is false while diving or getting up: inputs are ignored and a
// release throws nothing.
void calldown_player_input(sim_t *s, int idx, const player_intent_t *in, bool can_act);
// Beacons, strikes, turrets, pickups, cooldowns and the squad-down rules.
void calldowns_update(sim_t *s);

// A blast: damages bugs and players (friendly fire) within `radius`, from
// full at the centre down to EXPLOSION_EDGE at the edge. Ignores armor.
void sim_explode(sim_t *s, vec2 pos, float radius, float damage, blast_kind_t kind);

#endif
