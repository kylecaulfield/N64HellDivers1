#ifndef N64_RESULTS_H
#define N64_RESULTS_H

// The results screen once the mission is over (success or failure): the
// squad's numbers, a row per player, and the score. Plus a one-line RESULT
// summary over the debug log. What START does is main's business.

#include "mission.h"
#include "sim.h"

// `rank`: where the score lands on the high score table, -1 if it doesn't,
// -2 if it can't (debug aids were used).
void results_draw(const sim_t *sim, const mission_score_t *score, int rank);
// Log the RESULT line; call once when the mission ends.
void results_log(const sim_t *sim, const mission_score_t *score);

#endif
