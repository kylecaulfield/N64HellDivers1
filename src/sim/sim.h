#ifndef SIM_SIM_H
#define SIM_SIM_H

// Pure-C game simulation. No libdragon includes are allowed anywhere under
// src/sim/: it must build with a host compiler for the unit tests.
//
// Everything is sized for 4 players, indexed by controller port, even when
// only one controller is plugged in. All storage is fixed-size: the sim never
// allocates.

#include <stdbool.h>
#include <stdint.h>

#include "rng.h"
#include "tuning.h"
#include "vec2.h"

#define SIM_MAX_PLAYERS 4

// Controller buttons as the sim sees them. The N64 layer maps the hardware
// joypad state onto these bits, so the sim never sees libdragon types.
enum {
    SIM_BTN_A       = 1 << 0,
    SIM_BTN_B       = 1 << 1,
    SIM_BTN_Z       = 1 << 2,
    SIM_BTN_START   = 1 << 3,
    SIM_BTN_DUP     = 1 << 4,
    SIM_BTN_DDOWN   = 1 << 5,
    SIM_BTN_DLEFT   = 1 << 6,
    SIM_BTN_DRIGHT  = 1 << 7,
    SIM_BTN_L       = 1 << 8,
    SIM_BTN_R       = 1 << 9,
    SIM_BTN_CUP     = 1 << 10,
    SIM_BTN_CDOWN   = 1 << 11,
    SIM_BTN_CLEFT   = 1 << 12,
    SIM_BTN_CRIGHT  = 1 << 13,
};

typedef struct {
    bool     connected;
    uint16_t held;     // SIM_BTN_* currently down
    uint16_t pressed;  // SIM_BTN_* that went down this tick
    int8_t   stick_x;  // -127..127, right is positive
    int8_t   stick_y;  // -127..127, up is positive
} sim_input_t;

typedef enum { SCHEME_A, SCHEME_B, SCHEME_COUNT } control_scheme_t;
typedef enum { WEAPON_RIFLE, WEAPON_SHOTGUN, WEAPON_COUNT } weapon_type_t;
typedef enum { ENEMY_GNASHER, ENEMY_TYPE_COUNT } enemy_type_t;

// --- Players -----------------------------------------------------------------

typedef enum {
    PSTATE_NORMAL,
    PSTATE_DIVING,  // committed lunge, no shooting
    PSTATE_PRONE,   // getting up after a dive
    PSTATE_DEAD,
} player_state_t;

typedef struct {
    uint8_t  type;     // weapon_type_t
    uint8_t  mag;      // rounds in the magazine
    uint16_t reserve;  // rounds left to reload from
} weapon_slot_t;

#define PLAYER_WEAPON_SLOTS 2

typedef struct {
    bool     present;        // a controller is plugged into this port
    bool     active;         // spawned into the mission
    uint8_t  state;          // player_state_t
    uint16_t state_timer;
    vec2     pos;
    vec2     vel;
    vec2     facing;         // unit vector
    float    hp;
    uint8_t  weapon;         // index into weapons[]
    weapon_slot_t weapons[PLAYER_WEAPON_SLOTS];
    uint16_t fire_cooldown;  // ticks until the next shot is allowed
    uint16_t reload_timer;   // ticks left in the current reload, 0 = not reloading
    uint16_t b_held;         // ticks B has been held (tap = reload, hold = switch)
    uint16_t dive_cooldown;
} player_t;

// --- Enemies -------------------------------------------------------------------

typedef enum {
    ESTATE_CHASE,
    ESTATE_WINDUP,   // telegraphing a melee attack
    ESTATE_RECOVER,
} enemy_state_t;

typedef struct {
    bool     active;
    uint8_t  type;    // enemy_type_t
    uint8_t  state;   // enemy_state_t
    uint8_t  target;  // player index
    uint16_t timer;
    vec2     pos;
    vec2     facing;
    float    hp;
} enemy_t;

// --- Projectiles ---------------------------------------------------------------

typedef struct {
    bool    active;
    uint8_t owner;   // player index
    uint8_t weapon;  // weapon_type_t
    float   damage;
    vec2    pos;
    vec2    vel;     // m/s
    float   range;   // remaining travel, m
} projectile_t;

// --- World ---------------------------------------------------------------------

typedef enum { ROCK_A, ROCK_B, ROCK_C, ROCK_KIND_COUNT } rock_kind_t;

typedef struct {
    vec2    pos;
    float   radius;
    uint8_t kind;    // rock_kind_t, picks the model
    float   yaw;     // visual only
} obstacle_t;

typedef struct {
    vec2       min, max;
    obstacle_t obstacles[SIM_MAX_OBSTACLES];
    int        obstacle_count;
} arena_t;

typedef struct {
    vec2  target;     // ground point the camera looks at (screen centre)
    float dist;       // distance from the target along the view direction, m
    vec2  leash_min;  // players are kept inside this ground rectangle
    vec2  leash_max;
} camera_t;

// --- Events: what happened this tick, for effects and (later) audio ---------

typedef enum {
    EV_SHOT,          // pos = muzzle, dir = shot direction, a = weapon
    EV_HIT_ENEMY,     // pos = impact point
    EV_HIT_WORLD,     // pos = impact point
    EV_ENEMY_DIED,    // pos = enemy position, a = enemy type
    EV_PLAYER_HURT,   // a = player index
    EV_PLAYER_DIED,   // a = player index
    EV_DRY_FIRE,      // a = player index (out of ammo)
    EV_RELOAD,        // a = player index
} sim_event_type_t;

typedef struct {
    uint8_t type;  // sim_event_type_t
    uint8_t a;
    vec2    pos;
    vec2    dir;
} sim_event_t;

// --- Settings (debug menu) and counters ------------------------------------

typedef struct {
    uint8_t  scheme[SIM_MAX_PLAYERS]; // control_scheme_t per port
    bool     god_mode;
    bool     infinite_ammo;
    bool     trickle_spawn;           // M1 stand-in for the wave spawner
    uint16_t enemy_cap;               // <= SIM_MAX_ENEMIES
} sim_settings_t;

// Live entity counts, reported by the debug overlay and the stats log.
typedef struct {
    uint16_t players;
    uint16_t enemies;
    uint16_t projectiles;
    uint16_t particles;   // filled in by the renderer (effects are visual only)
    uint16_t calldowns;
} sim_counts_t;

typedef struct {
    uint32_t       tick;
    rng_t          rng;
    sim_settings_t settings;
    arena_t        arena;
    camera_t       camera;
    player_t       players[SIM_MAX_PLAYERS];
    enemy_t        enemies[SIM_MAX_ENEMIES];
    projectile_t   projectiles[SIM_MAX_PROJECTILES];
    sim_event_t    events[SIM_MAX_EVENTS];
    uint16_t       event_count;       // cleared by the consumer, see sim_clear_events
    uint16_t       events_dropped;
    uint16_t       spawn_timer;
    uint32_t       kills;
    sim_counts_t   counts;
} sim_t;

void sim_init(sim_t *s, uint32_t seed);
void sim_step(sim_t *s, const sim_input_t inputs[SIM_MAX_PLAYERS]);

// Events accumulate across ticks until the consumer clears them (the renderer
// does once per frame), so effects from catch-up ticks are not lost.
void sim_clear_events(sim_t *s);

// Spawning and debug helpers.
void sim_spawn_player(sim_t *s, int port, vec2 pos);
int  sim_spawn_enemy(sim_t *s, enemy_type_t type, vec2 pos);   // index or -1
int  sim_spawn_enemies_around(sim_t *s, enemy_type_t type, int count, vec2 center);
void sim_kill_all_enemies(sim_t *s);
void sim_refill_ammo(sim_t *s, int port);

// Internal, exposed for the unit tests.
void sim_push_event(sim_t *s, sim_event_type_t type, uint8_t a, vec2 pos, vec2 dir);
void sim_update_counts(sim_t *s);
bool sim_player_alive(const player_t *p);

#endif
