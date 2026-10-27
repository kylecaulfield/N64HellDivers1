#ifndef SIM_FLOW_H
#define SIM_FLOW_H

// One shared flow field for every bug: the walking distance from each 1 m
// cell to the nearest living player, around rocks. Bugs just roll downhill.
//
// It is rebuilt incrementally: each tick expands at most
// `settings.flow_budget` cells of a fresh field (Dial's algorithm: Dijkstra
// with integer costs 2 straight / 3 diagonal and a 4-bucket queue), while
// bugs keep reading the last completed one. A build only reaches a margin
// past the farthest bug, and sim_step starts one at most every
// FLOW_REBUILD_TICKS while any bug is alive.

#include "sim.h"

// Mark the cells rocks block, and which rocks are near each cell. Call
// after the arena is generated or its rocks change.
void flow_init(flow_t *f, const arena_t *a);
// Continue (or start) building the next field. Returns cells expanded.
int flow_step(sim_t *s, int budget);
// Direction to walk from `pos` (unit), or zero if the field has nothing
// useful there (not built yet, unreachable, or already at a player).
vec2 flow_direction(const flow_t *f, vec2 pos);
// Field distance at `pos` (2 per metre straight), FLOW_INF if unknown.
uint16_t flow_distance(const flow_t *f, vec2 pos);
int flow_cell_of(vec2 pos);

#endif
