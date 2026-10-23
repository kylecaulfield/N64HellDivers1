#ifndef N64_INPUT_H
#define N64_INPUT_H

// Reads all 4 controller ports and converts them to the sim's input format.
// Also detects the debug button combos (L is unused by both control schemes,
// so every debug combo starts with holding L).

#include <stdbool.h>

#include "sim.h"

typedef struct {
    sim_input_t ports[SIM_MAX_PLAYERS];
    bool toggle_overlay; // L + D-Up pressed on any controller
} input_frame_t;

void input_init(void);
// Poll once per rendered frame.
void input_poll(input_frame_t *out);

#endif
