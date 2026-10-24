#ifndef N64_HUD_H
#define N64_HUD_H

#include <stdbool.h>

#include "sim.h"

// Player status (health, weapon, ammo), kill count, reinforcements, the
// calldown panel while R is held, beacon countdowns, death, squad and pause
// notices. Call after the 3D scene, before the debug menu and overlay.
void hud_draw(const sim_t *sim, bool paused);

#endif
