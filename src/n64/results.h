#ifndef N64_RESULTS_H
#define N64_RESULTS_H

// The results screen once the mission is over (success or failure), and a
// one-line RESULT summary over the debug log. START starts a new mission
// (handled by main).

#include "sim.h"

void results_draw(const sim_t *sim);
// Log the RESULT line; call once when the mission ends.
void results_log(const sim_t *sim);

#endif
