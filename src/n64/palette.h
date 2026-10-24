#ifndef N64_PALETTE_H
#define N64_PALETTE_H

// Colours shared by the renderer and the 2D screens.

#include <stdint.h>

#include "sim.h"

// Each port's colour: the trooper's accents, aim dots, HUD label and
// results row. (Text uses STYLE_P1 + port, registered from these.)
static const uint8_t PLAYER_RGB[SIM_MAX_PLAYERS][3] = {
    { 250, 210, 60 },   // P1 yellow
    { 80, 150, 255 },   // P2 blue
    { 100, 225, 100 },  // P3 green
    { 250, 100, 170 },  // P4 pink
};

#endif
