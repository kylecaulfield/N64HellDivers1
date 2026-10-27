#ifndef SIM_BOT_H
#define SIM_BOT_H

// Stand-in players for the ports nobody has joined (and for stress
// testing): they follow player 1 in formation, shoot the nearest bug and
// back off when one gets close. They produce the same intents a controller
// would, so they cost exactly what a human player costs, and get scheme B's
// aim assist (they aim like C-button players).

#include "controls.h"

player_intent_t bot_intent(const sim_t *s, int idx);

#endif
