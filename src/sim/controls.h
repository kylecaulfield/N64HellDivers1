#ifndef SIM_CONTROLS_H
#define SIM_CONTROLS_H

// Control schemes: raw controller state -> what the player wants this tick.
//
// Scheme A: stick moves and aims (facing follows movement). Hold Z to fire;
//           facing locks while Z is held, so you can strafe. A = dive.
// Scheme B: stick moves; C-buttons aim and fire in 8 directions (two
//           adjacent C-buttons = diagonal). Z = dive.
// Both:     B tap = reload, B hold = switch weapon. Holding R puts the
//           C-buttons in calldown-code mode: no firing, each C-button press
//           is a code input, releasing R throws the calldown.

#include "sim.h"

typedef struct {
    vec2 move;           // desired movement, magnitude 0..1
    vec2 aim;            // explicit aim direction (unit), or zero = none
    bool fire;
    bool lock_facing;    // keep the current facing (strafe)
    bool dive;
    bool reload;
    bool switch_weapon;
    bool code_mode;      // R held
    uint8_t code_input;  // code_input_t pressed this tick (code mode only)
} player_intent_t;

// Stick -> unit-disc vector with the deadzone removed.
vec2 controls_stick(int8_t x, int8_t y);
// Held C-buttons -> one of 8 unit directions, or zero if none / opposing.
vec2 controls_cbuttons_dir(uint16_t held);
// Reads one player's input. `b_held` is per-player state for tap/hold on B.
player_intent_t controls_read(control_scheme_t scheme, const sim_input_t *in, uint16_t *b_held);

#endif
