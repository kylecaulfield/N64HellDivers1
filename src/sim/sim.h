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
typedef enum { WEAPON_RIFLE, WEAPON_SHOTGUN, WEAPON_MACHINE_GUN, WEAPON_COUNT } weapon_type_t;
typedef enum { ENEMY_SKITTER, ENEMY_GNASHER, ENEMY_BULWARK, ENEMY_TYPE_COUNT } enemy_type_t;

// --- Players -----------------------------------------------------------------

typedef enum {
    PSTATE_NORMAL,
    PSTATE_DIVING,  // committed lunge, no shooting
    PSTATE_PRONE,   // getting up after a dive
    PSTATE_DEAD,
    PSTATE_DROPPING, // riding a pod down at the start of the mission: can't act or be hurt
} player_state_t;

typedef struct {
    bool     owned;    // empty slots are skipped when switching
    uint8_t  type;     // weapon_type_t
    uint8_t  mag;      // rounds in the magazine
    uint16_t reserve;  // rounds left to reload from
} weapon_slot_t;

// Rifle, shotgun, and a support weapon slot filled by calldown pickups.
#define PLAYER_WEAPON_SLOTS 3
#define WEAPON_SLOT_SUPPORT 2

// --- Calldowns -------------------------------------------------------------------

typedef enum {
    CALLDOWN_REINFORCE,
    CALLDOWN_RESUPPLY,
    CALLDOWN_MACHINE_GUN,
    CALLDOWN_AIRSTRIKE,
    CALLDOWN_BARRAGE,
    CALLDOWN_SENTRY,
    CALLDOWN_EXTRACTION,  // once the objective is done, from inside the LZ
    CALLDOWN_COUNT
} calldown_type_t;

// Code inputs: the C-buttons while R is held. 0 = no input.
typedef enum { CODE_NONE, CODE_UP, CODE_DOWN, CODE_LEFT, CODE_RIGHT } code_input_t;
#define CALLDOWN_CODE_MAX 6

// A player's code entry in progress. A wrong input clears it; keep holding R
// and start again.
typedef struct {
    bool    entering;                  // R was held last tick
    uint8_t len;                       // inputs so far
    uint8_t inputs[CALLDOWN_CODE_MAX];
} code_entry_t;

typedef enum {
    SQUAD_FIGHTING,
    SQUAD_DOWN,     // everyone is dead: emergency reinforce when squad_timer runs out
    SQUAD_LOST,     // everyone is dead and no reinforcements are left
} squad_state_t;

typedef enum {
    BEACON_FLYING,  // thrown, still travelling
    BEACON_ARMED,   // on the ground, counting down the call-in delay
} beacon_state_t;

typedef struct {
    bool     active;
    uint8_t  type;   // calldown_type_t
    uint8_t  owner;  // player index
    uint8_t  state;  // beacon_state_t
    uint16_t timer;  // flight ticks left, then call-in ticks left
    vec2     pos;
    vec2     vel;
    vec2     dir;    // the thrower's facing (an airstrike flies along it)
} beacon_t;

// Explosions spread over time: an airstrike's bombs or a barrage's shells.
typedef struct {
    bool     active;
    uint8_t  type;   // CALLDOWN_AIRSTRIKE or CALLDOWN_BARRAGE
    uint8_t  owner;
    uint8_t  done;   // explosions so far
    uint16_t timer;  // ticks to the next one
    vec2     center;
    vec2     dir;
} strike_t;

typedef struct {
    bool     active;
    uint8_t  owner;
    uint16_t ammo;
    uint16_t life;           // ticks left
    uint16_t fire_cooldown;
    int16_t  target;         // enemy index, -1 = none
    vec2     pos;
    vec2     facing;
} turret_t;

typedef enum { PICKUP_RESUPPLY, PICKUP_MACHINE_GUN } pickup_type_t;

typedef struct {
    bool     active;
    uint8_t  type;   // pickup_type_t
    uint8_t  uses;   // players it can still serve
    uint16_t life;   // ticks left
    vec2     pos;
} pickup_t;

// Projectile owners: players 0-3, or a sentry turret.
#define OWNER_TURRET 0x80   // | turret index

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
    bool     bot;            // driven by the sim's bot logic instead of a controller
    code_entry_t code;       // calldown code being entered
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
    // The flow direction depends only on the cell, so it is kept until the
    // bug changes cell or a new field completes.
    uint16_t flow_cell;   // FLOW_CELLS = nothing cached
    uint32_t flow_build;
    vec2     flow_dir;
} enemy_t;

// --- Projectiles ---------------------------------------------------------------

typedef struct {
    bool    active;
    uint8_t owner;   // player index, or OWNER_TURRET | turret index
    uint8_t weapon;  // weapon_type_t
    float   damage;
    vec2    pos;
    vec2    vel;     // m/s
    float   range;   // remaining travel, m
} projectile_t;

// --- Spatial hash: uniform grid over the arena, rebuilt every tick ---------------

#define GRID_CELL  2.5f   // >= the largest interaction distance (two bulwarks)
#define GRID_W     26     // ceil(2 * ARENA_HALF_SIZE / GRID_CELL)
#define GRID_CELLS (GRID_W * GRID_W)

typedef struct {
    int16_t head[GRID_CELLS];       // first enemy in each cell, -1 = empty
    int16_t next[SIM_MAX_ENEMIES];  // next enemy in the same cell
} grid_t;

// Separation scratch: a copy of the bugs' bodies in grid-cell order, one
// 16-byte D-cache line each, so neighbours are neighbours in memory too
// (the N64's 8 KB direct-mapped D-cache thrashes on scattered enemy_t).
typedef struct {
    float x, y, radius, mass;
} crowd_body_t;

typedef struct {
    _Alignas(16) crowd_body_t body[SIM_MAX_ENEMIES];
    uint16_t enemy[SIM_MAX_ENEMIES];     // enemy index of each body
    uint16_t start[GRID_CELLS + 1];      // first body of each cell
} crowd_t;

// --- Flow field: shared distance-to-nearest-player map, built incrementally ----

#define FLOW_CELL  1.0f
#define FLOW_W     64
#define FLOW_CELLS (FLOW_W * FLOW_W)
#define FLOW_QCAP  4096             // per-bucket queue capacity (power of two)
#define FLOW_INF   0xFFFF
#define FLOW_ROCKS_ALL 0xFF         // rocks[cell][0]: too many nearby, test every rock

typedef struct {
    uint16_t dist[2][FLOW_CELLS];   // [cur] is read by enemies, [!cur] is being built
    uint8_t  blocked[FLOW_CELLS];   // rocks, inflated by a margin
    uint8_t  steps[FLOW_CELLS];     // bit k: step k to a neighbour is allowed (see flow.c)
    uint8_t  rocks[FLOW_CELLS][2];  // rocks (index + 1, 0 = none) a bug in this cell can touch
    uint16_t queue[4][FLOW_QCAP];   // Dial's buckets: distance mod 4
    uint16_t qhead[4], qcount[4];
    uint32_t level;                 // distance currently being expanded
    uint32_t limit;                 // the build stops past this distance
    uint32_t started;               // tick the current/last build started
    uint8_t  cur;
    bool     building;
    bool     ready;                 // a complete field exists in dist[cur]
    uint32_t builds;                // completed builds
    uint32_t overflows;             // queue overflows (should stay 0)
} flow_t;

// --- Waves -----------------------------------------------------------------------

typedef enum { WAVE_BREAK, WAVE_ACTIVE } wave_state_t;

typedef struct {
    uint16_t number;                       // current wave, 0 before the first
    uint8_t  state;                        // wave_state_t
    uint16_t timer;                        // ticks to the next group / next wave
    uint16_t remaining[ENEMY_TYPE_COUNT];  // still to spawn in this wave
    uint16_t total;                        // size of the current wave
} waves_t;

// --- World ---------------------------------------------------------------------

typedef enum { ROCK_A, ROCK_B, ROCK_C, ROCK_KIND_COUNT } rock_kind_t;

typedef struct {
    vec2    pos;
    float   radius;
    uint8_t kind;    // rock_kind_t, picks the model
    float   yaw;     // visual only
} obstacle_t;

#define MISSION_ZONES 2

typedef struct {
    vec2       min, max;
    obstacle_t obstacles[SIM_MAX_OBSTACLES];
    int        obstacle_count;
    // Mission sites, placed before the rocks, which keep clear of them. The
    // drop zone is the origin.
    vec2       zones[MISSION_ZONES];
    vec2       lz;
} arena_t;

// --- Mission -----------------------------------------------------------------------

typedef enum {
    MISSION_DROP,       // players riding pods down
    MISSION_OBJECTIVE,  // hold both zones
    MISSION_EXTRACT,    // objective done: call extraction from inside the LZ
    MISSION_HOLD,       // extraction called: survive until the dropship lands
    MISSION_BOARDING,   // dropship down: living players get into the LZ
    MISSION_SUCCESS,
    MISSION_FAILED,
} mission_phase_t;

typedef struct {
    uint16_t held;        // ticks held so far
    bool     captured;
    uint8_t  players_in;  // this tick
    uint8_t  bugs_in;
} zone_state_t;

typedef struct {
    uint32_t ticks;                    // mission time
    uint16_t kills[ENEMY_TYPE_COUNT];
    uint32_t shots;                    // player trigger pulls (a shotgun blast is one)
    uint32_t hits;                     // player bullets that hit a bug
    uint16_t friendly_hits;            // player bullets that hit a teammate
    uint16_t deaths;
    uint16_t reinforcements;           // used
    uint16_t calldowns;                // beacons thrown
} mission_stats_t;

typedef struct {
    uint8_t         phase;    // mission_phase_t
    uint16_t        timer;    // ticks left in DROP / HOLD
    zone_state_t    zones[MISSION_ZONES];
    uint8_t         extracted; // bit per player aboard the dropship
    mission_stats_t stats;
} mission_t;

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
    EV_EXPLOSION,     // pos = centre, a = blast_kind_t
    EV_CODE_INPUT,    // a = player index (a code input was accepted)
    EV_CODE_REJECTED, // a = player index (released R on no code / unavailable)
    EV_BEACON_THROWN, // pos = thrower, dir = throw direction, a = calldown type
    EV_CALLDOWN,      // pos = beacon, a = calldown type (it arrived)
    EV_PICKUP,        // pos = pickup, a = pickup type
    EV_REINFORCED,    // pos = drop point, a = player index
    EV_ZONE_CAPTURED, // pos = zone, a = zone index
    EV_MISSION,       // a = the mission_phase_t just entered
    EV_EXTRACTED,     // pos = player, a = player index (boarded the dropship)
} sim_event_type_t;

typedef enum {
    BLAST_IMPACT,   // something dropped from orbit landed
    BLAST_BOMB,     // airstrike
    BLAST_SHELL,    // orbital barrage
} blast_kind_t;

// EV_HIT_ENEMY: `a` = enemy type, plus this flag when armor absorbed the hit.
#define EV_HIT_ARMORED 0x80

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
    bool     waves;                   // wave spawner on
    bool     force_max_zoom;          // stress testing: camera fully zoomed out
    bool     free_calldowns;          // debug: no cooldowns
    bool     quick_mission;           // debug: capture and hold times divided by QUICK_MISSION_DIV
    uint16_t enemy_cap;               // <= SIM_MAX_ENEMIES
    uint16_t flow_budget;             // flow-field cells expanded per tick
} sim_settings_t;

// Profiling hook: the N64 layer can time each phase of sim_step without the
// sim knowing anything about timers. Called at the start of each phase.
typedef enum {
    SIM_PHASE_PLAYERS,
    SIM_PHASE_FLOW,
    SIM_PHASE_ENEMIES,      // AI and movement
    SIM_PHASE_CROWD,        // separation, rock collision, grid rebuild
    SIM_PHASE_PROJECTILES,
    SIM_PHASE_CALLDOWNS,    // beacons, strikes, turrets, pickups, reinforcements
    SIM_PHASE_SPAWN,        // spawner, mission, camera, counts
    SIM_PHASE_END,
    SIM_PHASE_COUNT
} sim_phase_t;
typedef void (*sim_mark_fn)(void *ctx, sim_phase_t phase);

// Live entity counts, reported by the debug overlay and the stats log.
typedef struct {
    uint16_t players;
    uint16_t enemies;
    uint16_t projectiles;
    uint16_t particles;   // filled in by the renderer (effects are visual only)
    uint16_t calldowns;   // beacons, strikes, turrets and pickups in play
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
    uint16_t       active[SIM_MAX_ENEMIES]; // indices of active enemies, rebuilt each tick
    uint16_t       active_count;
    grid_t         grid;
    crowd_t        crowd;             // scratch for separation, see crowd_t
    flow_t         flow;
    waves_t        waves;
    beacon_t       beacons[SIM_MAX_BEACONS];
    strike_t       strikes[SIM_MAX_STRIKES];
    turret_t       turrets[SIM_MAX_TURRETS];
    pickup_t       pickups[SIM_MAX_PICKUPS];
    uint16_t       cooldowns[SIM_MAX_PLAYERS][CALLDOWN_COUNT]; // kept through death
    uint8_t        reinforcements;    // shared pool
    uint8_t        squad;             // squad_state_t
    uint16_t       squad_timer;       // SQUAD_DOWN / SQUAD_LOST countdown
    mission_t      mission;
    sim_event_t    events[SIM_MAX_EVENTS];
    uint16_t       event_count;       // cleared by the consumer, see sim_clear_events
    uint16_t       events_dropped;
    uint32_t       kills;
    sim_counts_t   counts;
    sim_mark_fn    mark;              // optional profiling hook
    void          *mark_ctx;
} sim_t;

void sim_init(sim_t *s, uint32_t seed);
// A new mission with the same settings (and bots) as the current one.
void sim_restart(sim_t *s, uint32_t seed);
void sim_step(sim_t *s, const sim_input_t inputs[SIM_MAX_PLAYERS]);

// Events accumulate across ticks until the consumer clears them (the renderer
// does once per frame), so effects from catch-up ticks are not lost.
void sim_clear_events(sim_t *s);

// Spawning and debug helpers.
void sim_spawn_player(sim_t *s, int port, vec2 pos);   // keeps `present` and `bot`
int  sim_spawn_enemy(sim_t *s, enemy_type_t type, vec2 pos);   // index or -1
int  sim_spawn_enemies_around(sim_t *s, enemy_type_t type, int count, vec2 center);
// Mixed swarm (half skitters, a third gnashers, the rest bulwarks) on a
// ring between r_min and r_max metres around `center`.
int  sim_spawn_mix(sim_t *s, int count, vec2 center, float r_min, float r_max);
void sim_kill_all_enemies(sim_t *s);
void sim_refill_ammo(sim_t *s, int port);
// Kills a player even in god mode (to test reinforcing).
void sim_kill_player(sim_t *s, int port);
// Bots take ports 2-4 (stress testing until real players join in M5).
void sim_set_bots(sim_t *s, bool on);

// Internal, exposed for the unit tests.
void sim_push_event(sim_t *s, sim_event_type_t type, uint8_t a, vec2 pos, vec2 dir);
void sim_update_counts(sim_t *s);
bool sim_player_alive(const player_t *p);

#endif
