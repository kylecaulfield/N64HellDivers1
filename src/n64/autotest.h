#ifndef N64_AUTOTEST_H
#define N64_AUTOTEST_H

// Scripted-input smoke test, only in `make AUTOTEST=1` builds
// (hivebreakers-autotest.z64). It drives controller 1 through every M1
// feature (moving, both control schemes, firing at bugs, diving, the debug
// menu, pausing) so a ROM can be exercised in ares with no controller, and
// logs "AUTOTEST PASS/FAIL" with a summary over the debug log.

#include <stdint.h>

#include "input.h"
#include "sim.h"

// Overwrite this frame's input with the script. `frame` counts rendered frames.
void autotest_input(input_frame_t *in, uint32_t frame, sim_t *sim);
// Look at what the sim did this frame (call before its events are cleared).
void autotest_observe(const sim_t *sim);

#endif
