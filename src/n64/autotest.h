#ifndef N64_AUTOTEST_H
#define N64_AUTOTEST_H

// Scripted-input smoke test, only in `make AUTOTEST=1` builds
// (hivebreakers-autotest.z64). It drives controller 1 through every feature
// so far (moving, both control schemes, firing at bugs, diving, the debug
// menu, pausing, every calldown, picking up the machine gun and a resupply,
// dying, and bots reinforcing each other) so a ROM can be exercised in ares
// with no controller, and logs "AUTOTEST PASS/FAIL" with a summary.

#include <stdint.h>

#include "input.h"
#include "sim.h"

// Overwrite this frame's input with the script. `frame` counts rendered frames.
void autotest_input(input_frame_t *in, uint32_t frame, sim_t *sim);
// Look at what the sim did this frame (call before its events are cleared).
void autotest_observe(const sim_t *sim);

#endif
