#ifndef N64_HUD_H
#define N64_HUD_H

#include <stdbool.h>
#include <stdint.h>

#include "sim.h"

// A panel per player (health, weapon, ammo, or why they are not playing;
// `connected` has a bit per port with a controller, which can join), kill
// count, reinforcements, each player's calldown panel while they hold R,
// beacon countdowns, squad notices and the pause screen. Call after the 3D
// scene, before the debug menu and overlay.
void hud_draw(const sim_t *sim, bool paused, uint8_t connected);

#endif
