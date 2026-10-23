#ifndef SIM_SIM_H
#define SIM_SIM_H

// Pure-C game simulation. No libdragon includes are allowed anywhere under
// src/sim/: it must build with a host compiler for the unit tests.
//
// Everything is sized for 4 players, indexed by controller port, even when
// only one controller is plugged in.

#include <stdbool.h>
#include <stdint.h>

#include "rng.h"

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

typedef struct {
    bool present; // a controller is plugged into this port
} sim_player_t;

// Live entity counts, reported by the debug overlay and the stats log.
typedef struct {
    uint16_t players;
    uint16_t enemies;
    uint16_t projectiles;
    uint16_t particles;
    uint16_t calldowns;
} sim_counts_t;

typedef struct {
    uint32_t     tick;
    rng_t        rng;
    sim_player_t players[SIM_MAX_PLAYERS];
    sim_counts_t counts;
} sim_t;

void sim_init(sim_t *s, uint32_t seed);
void sim_step(sim_t *s, const sim_input_t inputs[SIM_MAX_PLAYERS]);

#endif
