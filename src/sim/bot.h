#ifndef SIM_BOT_H
#define SIM_BOT_H

// Stand-in players for stress testing (M2) until real players can join (M5):
// they follow player 1 in formation, shoot the nearest bug and back off
// when one gets close. They produce the same intents a controller would,
// so they cost exactly what a human player costs.

#include "controls.h"

player_intent_t bot_intent(const sim_t *s, int idx);

#endif
