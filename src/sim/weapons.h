#ifndef SIM_WEAPONS_H
#define SIM_WEAPONS_H

#include "sim.h"

typedef struct {
    const char *name;
    uint8_t  mag_size;
    uint8_t  pellets;        // projectiles per shot
    float    damage;         // per projectile
    uint16_t fire_interval;  // ticks between shots
    uint16_t reload_ticks;
    uint16_t start_reserve;  // rounds carried besides the magazine
    float    speed;          // projectile speed, m/s
    float    range;          // projectile travel, m
    float    spread;         // max random deviation, radians (each side)
} weapon_def_t;

extern const weapon_def_t weapon_defs[WEAPON_COUNT];

// Fresh, full slot of the given weapon.
weapon_slot_t weapon_slot_full(weapon_type_t type);

// Bend `dir` towards the best enemy inside the scheme's aim-assist cone.
// Returns `dir` unchanged when nothing qualifies.
vec2 aim_assist(const sim_t *s, vec2 origin, vec2 dir, control_scheme_t scheme);

// Per-tick weapon handling for one player: cooldowns, reload, switching and
// firing. `aim` is the (unassisted) direction the player wants to shoot.
void weapon_update(sim_t *s, int idx, bool fire, bool reload, bool switch_weapon, vec2 aim);

#endif
