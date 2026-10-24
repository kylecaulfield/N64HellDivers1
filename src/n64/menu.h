#ifndef N64_MENU_H
#define N64_MENU_H

// Debug menu (hold L, press D-Down). The game is paused while it is open.
// D-Up/D-Down select, A or D-Left/D-Right change, B or L+D-Down closes.

#include <stdbool.h>
#include <stdint.h>

#include "sim.h"

bool menu_is_open(void);
void menu_toggle(void);
// `pressed` is SIM_BTN_* pressed this frame on any controller.
void menu_update(uint16_t pressed, sim_t *sim);
void menu_draw(const sim_t *sim);

#endif
