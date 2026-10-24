#ifndef N64_AUTOTEST_H
#define N64_AUTOTEST_H

// Scripted-input smoke test, only in `make AUTOTEST=1` builds
// (hivebreakers-autotest.z64). It drives controller 1 through every feature
// so far (moving, both control schemes, firing at bugs, diving, the debug
// menu, pausing and switching schemes, every calldown, picking up the
// machine gun and a resupply, dying, bots reinforcing each other, a quick
// mission through to the results screen and the next mission), and plugs in
// controllers 2 and 4 to join, fight and leave. Around that it goes through
// the front end: splash, title, menu, briefing (setting the difficulty),
// initials for the high score and the table. Then two more missions, the
// other objectives (nests, upload), played by bots on every port. So a ROM
// can be exercised in ares with no controller. Logs "AUTOTEST PASS/FAIL"
// with a summary.

#include <stdint.h>

#include "frontend.h"
#include "input.h"
#include "sim.h"

// Overwrite this frame's input with the script, which follows the screens:
// `screen_frames` counts frames on the current one, `mission_frames` frames
// into the current mission.
void autotest_input(input_frame_t *in, sim_t *sim, screen_t screen, uint32_t screen_frames, uint32_t mission_frames);
// Look at what the sim did this frame (call before its events are cleared).
void autotest_observe(const sim_t *sim);
// The objective for the next mission (called as its briefing starts): the
// script plays each one in turn.
int autotest_objective(void);

#endif
